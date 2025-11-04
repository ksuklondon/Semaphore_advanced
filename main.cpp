#include "monitor.h"
#include <iostream>
#include <queue>
#include <unistd.h>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <pthread.h>
#include <vector>
#include <stdexcept>
#include <sys/stat.h>

// Kolejka FIFO
std::queue<int> fifo;

// Semafory binarne
Semaphore mutex(1);
Semaphore prodSem(1);
Semaphore consSem(1);
Semaphore evenSem(1);
Semaphore oddSem(1);

// Liczniki
int evenCount = 0;
int oddCount = 0;
volatile bool stopThreads = false;

// Statystyki
struct Statistics {
    int evenProduced = 0;
    int oddProduced = 0;
    int evenConsumed = 0;
    int oddConsumed = 0;
    double executionTime = 0.0;
} stats;

// Helper do formatowania czasu
std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << "[" << std::put_time(std::localtime(&now_c), "%H:%M:%S") << "]";
    return ss.str();
}

void log(const std::string& message) {
    std::string timestamp = getCurrentTimestamp();
    std::cout << timestamp << " " << message << std::endl;
}

void printFifo() {
    std::stringstream ss;
    ss << "Bufor [" << fifo.size() << "]: ";
    std::queue<int> temp = fifo;
    while (!temp.empty()) {
        ss << temp.front();
        temp.pop();
        if (!temp.empty()) ss << ", ";
    }
    log(ss.str());
}

int generateNumber(bool even) {
    static int evenNum = 2;
    static int oddNum = 1;
    if (even) {
        int result = evenNum;
        evenNum = (evenNum + 2) % 50;
        if (evenNum == 0) evenNum = 2;
        return result;
    } else {
        int result = oddNum;
        oddNum = (oddNum + 2) % 50;
        if (oddNum == 1) oddNum = 3;
        return result;
    }
}

void* producerEven(void*) {
    std::string threadId = "[ProducerEven-" + std::to_string(pthread_self()) + "]";
    while(!stopThreads) {
        mutex.p();
        if (evenCount < 10 && fifo.size() < 15) {
            int num = generateNumber(true);
            fifo.push(num);
            stats.evenProduced++;
            evenCount++;
            log(threadId + " Dodano liczbę parzystą: " + std::to_string(num));
            printFifo();
        }
        mutex.v();
        usleep(100000);
    }
    return nullptr;
}

void* producerOdd(void*) {
    std::string threadId = "[ProducerOdd-" + std::to_string(pthread_self()) + "]";
    while(!stopThreads) {
        mutex.p();
        if ((fifo.empty() || evenCount >= oddCount) && fifo.size() < 15) {
            int num = generateNumber(false);
            fifo.push(num);
            stats.oddProduced++;
            oddCount++;
            log(threadId + " Dodano liczbę nieparzystą: " + std::to_string(num));
            printFifo();
        }
        mutex.v();
        usleep(100000);
    }
    return nullptr;
}

void* consumerEven(void*) {
    std::string threadId = "[ConsumerEven-" + std::to_string(pthread_self()) + "]";
    while(!stopThreads) {
        mutex.p();
        if (!fifo.empty()) {
            int frontValue = fifo.front();
            if (frontValue % 2 == 0) {
                fifo.pop();
                stats.evenConsumed++;
                evenCount--;
                log(threadId + " Pobrano liczbę parzystą: " + std::to_string(frontValue));
                printFifo();
                mutex.v();
                continue;
            }
        }
        mutex.v();
        usleep(100000);
    }
    return nullptr;
}

void* consumerOdd(void*) {
    std::string threadId = "[ConsumerOdd-" + std::to_string(pthread_self()) + "]";
    while(!stopThreads) {
        mutex.p();
        if (!fifo.empty()) {
            int frontValue = fifo.front();
            if (frontValue % 2 != 0) {
                fifo.pop();
                stats.oddConsumed++;
                oddCount--;
                log(threadId + " Pobrano liczbę nieparzystą: " + std::to_string(frontValue));
                printFifo();
                mutex.v();
                continue;
            }
        }
        mutex.v();
        usleep(100000);
    }
    return nullptr;
}

void printStats(const std::string& testName) {
    std::cout << "\nStatystyki testu " << testName << ":\n"
              << std::setw(30) << std::left << "Czas wykonania: " << std::fixed << std::setprecision(5) << stats.executionTime << "s\n"
              << std::setw(30) << std::left << "Wyprodukowano parzystych: " << stats.evenProduced << "\n"
              << std::setw(30) << std::left << "Wyprodukowano nieparzystych: " << stats.oddProduced << "\n"
              << std::setw(30) << std::left << "Skonsumowano parzystych: " << stats.evenConsumed << "\n"
              << std::setw(30) << std::left << "Skonsumowano nieparzystych: " << stats.oddConsumed << "\n"
              << std::string(50, '-') << std::endl;
}

int main(int argc, char* argv[]) {
    try {
        if (argc < 2) {
            std::cerr << "Użycie: " << argv[0] << " <numer_testu> [-full]\n";
            return 1;
        }

        mkdir("results", 0777);
        
        int testNumber = std::stoi(argv[1]);
        bool fullMode = (argc > 2 && std::string(argv[2]) == "-full");
        
        if (testNumber < 1 || testNumber > 10) {
            throw std::invalid_argument("Numer testu musi być z zakresu 1-10");
        }
        
        std::string testName = "test" + std::to_string(testNumber) + (fullMode ? "_full" : "_empty");

        // Reset przed testem
        stats = Statistics();
        evenCount = 0;
        oddCount = 0;
        while (!fifo.empty()) fifo.pop();

        auto fillBuffer = []() {
            log("Wypełnianie bufora...");
            for (int i = 0; i < 10; i++) {
                int num;
                if (i % 2 == 0) {
                    num = generateNumber(true);
                    fifo.push(num);
                    stats.evenProduced++;
                    evenCount++;
                } else {
                    num = generateNumber(false);
                    fifo.push(num);
                    stats.oddProduced++;
                    oddCount++;
                }
            }
            log("Bufor wypełniony.");
            printFifo();
            usleep(500000);
        };

        if (fullMode) {
            fillBuffer();
        }

        log("Uruchamianie testu " + std::to_string(testNumber) + 
            (fullMode ? " (pełny bufor)" : " (pusty bufor)"));

        auto startTime = std::chrono::system_clock::now();
        stopThreads = false;

        std::vector<pthread_t> threads;

        switch(testNumber) {
            case 1: {
                log("Test: 1 prodEven");
                pthread_t tid;
                pthread_create(&tid, NULL, producerEven, NULL);
                threads.push_back(tid);
                break;
            }
            case 2: {
                log("Test: 1 prodOdd");
                pthread_t tid;
                pthread_create(&tid, NULL, producerOdd, NULL);
                threads.push_back(tid);
                break;
            }
            case 3: {
                log("Test: 1 consEven");
                pthread_t tid;
                pthread_create(&tid, NULL, consumerEven, NULL);
                threads.push_back(tid);
                break;
            }
            case 4: {
                log("Test: 1 consOdd");
                pthread_t tid;
                pthread_create(&tid, NULL, consumerOdd, NULL);
                threads.push_back(tid);
                break;
            }
            case 5: {
                log("Test: 1 prodEven, 1 prodOdd");
                pthread_t tid1, tid2;
                pthread_create(&tid1, NULL, producerEven, NULL);
                pthread_create(&tid2, NULL, producerOdd, NULL);
                threads.push_back(tid1);
                threads.push_back(tid2);
                break;
            }
            case 6: {
                log("Test: 1 consEven, 1 consOdd");
                pthread_t tid1, tid2;
                pthread_create(&tid1, NULL, consumerEven, NULL);
                pthread_create(&tid2, NULL, consumerOdd, NULL);
                threads.push_back(tid1);
                threads.push_back(tid2);
                break;
            }
            case 7: {
                log("Test: 1 prodEven, 1 consEven");
                pthread_t tid1, tid2;
                pthread_create(&tid1, NULL, producerEven, NULL);
                pthread_create(&tid2, NULL, consumerEven, NULL);
                threads.push_back(tid1);
                threads.push_back(tid2);
                break;
            }
            case 8: {
                log("Test: 1 prodOdd, 1 consOdd");
                pthread_t tid1, tid2;
                pthread_create(&tid1, NULL, producerOdd, NULL);
                pthread_create(&tid2, NULL, consumerOdd, NULL);
                threads.push_back(tid1);
                threads.push_back(tid2);
                break;
            }
            case 9: {
                log("Test: 1 prodEven, 1 prodOdd, 1 consEven, 1 consOdd");
                pthread_t tid1, tid2, tid3, tid4;
                pthread_create(&tid1, NULL, producerEven, NULL);
                pthread_create(&tid2, NULL, producerOdd, NULL);
                pthread_create(&tid3, NULL, consumerEven, NULL);
                pthread_create(&tid4, NULL, consumerOdd, NULL);
                threads.push_back(tid1);
                threads.push_back(tid2);
                threads.push_back(tid3);
                threads.push_back(tid4);
                break;
            }
            case 10: {
                log("Test: 2 prodEven, 2 prodOdd, 2 consEven, 2 consOdd");
                for (int i = 0; i < 2; i++) {
                    pthread_t tid1, tid2, tid3, tid4;
                    pthread_create(&tid1, NULL, producerEven, NULL);
                    pthread_create(&tid2, NULL, producerOdd, NULL);
                    pthread_create(&tid3, NULL, consumerEven, NULL);
                    pthread_create(&tid4, NULL, consumerOdd, NULL);
                    threads.push_back(tid1);
                    threads.push_back(tid2);
                    threads.push_back(tid3);
                    threads.push_back(tid4);
                }
                break;
            }
            default:
                throw std::invalid_argument("Nieprawidłowy numer testu");
        }

        sleep(3);
        stopThreads = true;

        for (auto& thread : threads) {
            pthread_join(thread, NULL);
        }

        auto endTime = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsed = endTime - startTime;
        stats.executionTime = elapsed.count();

        printStats(testName);
        
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Błąd: " << e.what() << "\n"
                  << "Użycie: " << argv[0] << " <numer_testu> [-full]\n";
        return 1;
    }
}
