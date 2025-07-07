#pragma once
#include <string>
#include <algorithm> 

enum class LogLevel {
    NONE = 0,
    ERROR = 1,
    INFO = 2,
    DEBUG = 3
};

inline LogLevel parseLogLevel(const std::string& levelStr) {
    std::string upperLevel = levelStr;
    std::transform(upperLevel.begin(), upperLevel.end(), upperLevel.begin(), ::toupper);

    if (upperLevel == "DEBUG") return LogLevel::DEBUG;
    if (upperLevel == "INFO") return LogLevel::INFO;
    if (upperLevel == "ERROR") return LogLevel::ERROR;
    return LogLevel::NONE;
}