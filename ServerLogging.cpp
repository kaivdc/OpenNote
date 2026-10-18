#include "ServerLogging.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace {

std::mutex serverLogMutex;
std::ofstream serverLogFile;

}

// Opens the server log file in append mode
bool openServerLog() {
    std::lock_guard<std::mutex> lock(serverLogMutex);
    serverLogFile.open("opennote.log", std::ios::app);
    return serverLogFile.is_open();
}

// Writes one timestamped server message to the console and log file
void logServerMessage(const std::string& level, const std::string& message) {
    const std::chrono::system_clock::time_point currentTime = std::chrono::system_clock::now();
    const std::time_t currentTimeValue = std::chrono::system_clock::to_time_t(currentTime);
    std::tm localTime{};
    if (localtime_r(&currentTimeValue, &localTime) == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(serverLogMutex);
    std::ostringstream logEntry;
    logEntry << std::put_time(&localTime, "%Y-%m-%dT%H:%M:%S")
             << " [" << level << "] : " << message;
    const std::string logLine = logEntry.str();

    if (level == "ERROR" || level == "WARN") {
        std::cerr << logLine << "\n";
        std::cerr.flush();
    } else {
        std::cout << logLine << "\n";
        std::cout.flush();
    }

    if (serverLogFile.is_open()) {
        serverLogFile << logLine << "\n";
        serverLogFile.flush();
    }
}
