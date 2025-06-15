#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <algorithm>
#include "Enums.hpp"

struct TrafficLightState {
    std::string name;
    std::string state = "RED";
    int cycle;
    std::chrono::steady_clock::time_point endTime;
    int priority = 0;
    std::string command;
    int timeOutCounter = 0;
    int columns = 0;
    int lines = 0;
    Status intensity = Status::NONE; 

    bool isUnknown() const {
        return state == "UNKNOWN";
    }

    bool isAlert() const {
        return state == "ALERT";
    }
};

struct Intersection {
    std::string name;
    std::vector<std::string> trafficLightNames;

    bool contains(const std::string& name) const {
        return std::find(trafficLightNames.begin(), trafficLightNames.end(), name) != trafficLightNames.end();
    }
};

struct Command {
    std::string type;
    std::string value;
};