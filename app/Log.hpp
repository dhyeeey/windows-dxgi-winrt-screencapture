#pragma once

#include <iostream>
#include <mutex>
#include <string>

inline std::mutex& LogMutex() {
    static std::mutex mutex;
    return mutex;
}

inline void Log(const char* threadName, const std::string& msg) {
    std::lock_guard<std::mutex> lock(LogMutex());
    std::cout << "[" << threadName << "] " << msg << std::endl;
}
