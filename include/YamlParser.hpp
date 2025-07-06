#pragma once

#include <yaml-cpp/yaml.h>
#include <string>
#include <map>
#include <iostream>
#include <optional>
#include "Structs.hpp"

class YamlParser {
public:
    YamlParser(const std::string& filepath);

    const std::map<std::string, TrafficLightState>& getTrafficLights() const;
    const std::map<std::string, Intersection>& getIntersections() const;
    const std::vector<GreenWaveGroup>& getGreenWaves() const;
    const std::vector<SyncGroup>& getSyncGroups() const;
    std::optional<TrafficLightState> getTrafficLightByIndex(int index) const;


private:
    std::map<std::string, TrafficLightState> trafficLights;
    std::map<std::string, Intersection> intersections;
    std::vector<GreenWaveGroup> greenWaves;
    std::vector<SyncGroup> syncGroups;

    void parse(const YAML::Node& config);
};