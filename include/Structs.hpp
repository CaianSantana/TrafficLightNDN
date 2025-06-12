#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <algorithm>

struct TrafficLightState {
    std::string name;
    std::string state = "RED";
    std::chrono::steady_clock::time_point endTime;
    int priority = 0;
    std::string command;
    bool intersection = false;
    int timeOutCounter = 0;

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