#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <yaml-cpp/yaml.h>
#include "Structs.hpp"

class YamlParser {
public:
    explicit YamlParser(const std::string& filepath);

    const std::vector<std::pair<std::string, TrafficLightState>>& getTrafficLights() const;
    
    const std::map<std::string, Intersection>& getIntersections() const;
    const std::vector<GreenWaveGroup>& getGreenWaves() const;
    const std::vector<SyncGroup>& getSyncGroups() const;
    std::optional<TrafficLightState> getTrafficLightByIndex(int index) const;

private:
    void parse(const YAML::Node& config);

    std::vector<std::pair<std::string, TrafficLightState>> trafficLights;
    
    std::map<std::string, Intersection> intersections;
    std::vector<GreenWaveGroup> greenWaves;
    std::vector<SyncGroup> syncGroups;
};