#ifndef ENUMS_HPP
#define ENUMS_HPP

#include <string>

enum class Status { NONE = 1, LOW = 2, MEDIUM = 5, HIGH = 8 };

enum class Color {
    GREEN,
    YELLOW,
    RED,
    ALERT,
    UNKNOWN
};

inline Color parseColor(const std::string& str) {
    if (str == "GREEN") return Color::GREEN;
    if (str == "YELLOW") return Color::YELLOW;
    if (str == "RED") return Color::RED;
    return Color::ALERT;  
}

inline std::string ToString(Color color) {
    switch (color) {
        case Color::GREEN: return "GREEN";
        case Color::YELLOW: return "YELLOW";
        case Color::RED: return "RED";
        case Color::ALERT: return "ALERT";
        default: return "UNKNOWN";
    }
}

inline Status parseIntensity(const std::string& str) {
    if (str == "LOW") return Status::LOW;
    if (str == "MEDIUM") return Status::MEDIUM;
    if (str == "HIGH") return Status::HIGH;
    else return Status::NONE;
}

#endif