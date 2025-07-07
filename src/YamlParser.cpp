#include "../include/YamlParser.hpp"

YamlParser::YamlParser(const std::string& filepath) {
    YAML::Node config = YAML::LoadFile(filepath);
    parse(config);
}

void YamlParser::parse(const YAML::Node& config) {
    constexpr int TA = 3;

    for (const auto& node : config["traffic-lights"]) {
        std::string name = node["name"].as<std::string>();
        int cycleTime = node["cycle_time"].as<int>();
        std::string state = node["state"].as<std::string>();

        int columns = 0;
        int lines = 0;
        if (node["columns"]) columns = node["columns"].as<int>();
        if (node["lines"]) lines = node["lines"].as<int>();

        Status intensity = Status::NONE;
        if (node["intensity"]) {
            intensity = parseIntensity(node["intensity"].as<std::string>());
        }

        TrafficLightState light;
        light.name = name;
        light.state = state;
        light.cycle = cycleTime;
        light.columns = columns;
        light.lines = lines;
        light.intensity = intensity;

        int duration;
        if (state == "GREEN") {
            duration = (cycleTime / 2) - TA;
        } else if (state == "RED") {
            duration = cycleTime / 2;
        } else {
            duration = TA;
        }

        light.endTime = std::chrono::steady_clock::now() + std::chrono::seconds(duration);
        trafficLights[name] = light;
    }
    if (config["intersections"]) {
        for (const auto& node : config["intersections"]) {
            std::string crossName = node["name"].as<std::string>();
            std::vector<std::string> sems = node["traffic-lights"].as<std::vector<std::string>>();

            Intersection cross;
            cross.name = crossName;
            cross.trafficLightNames = sems;
            intersections[crossName] = cross;
        }
    }
    if (config["green_waves"]) {
        for (const auto& node : config["green_waves"]) {
            GreenWaveGroup wave;
            wave.name = node["name"].as<std::string>();
            wave.trafficLightNames = node["traffic_lights"].as<std::vector<std::string>>();
            wave.travelTimeMs = node["travel_time_ms"].as<int>();
            greenWaves[wave.name]=wave;
        }
    }
    if (config["sync_groups"]) {
        for (const auto& node : config["sync_groups"]) {
            SyncGroup sync;
            sync.name = node["name"].as<std::string>();
            sync.trafficLightNames = node["traffic_lights"].as<std::vector<std::string>>();
            syncGroups[sync.name]=sync;
        }
    }
}



const std::map<std::string, TrafficLightState>& YamlParser::getTrafficLights() const {
    return trafficLights;
}

const std::map<std::string, Intersection>& YamlParser::getIntersections() const {
    return intersections;
}

const std::map<std::string, GreenWaveGroup>& YamlParser::getGreenWaves()  const {
    return greenWaves;
}

const std::map<std::string, SyncGroup>& YamlParser::getSyncGroups() const {
    return syncGroups;
}

std::optional<TrafficLightState> YamlParser::getTrafficLightByIndex(int index) const {
    if (index < 0 || index >= static_cast<int>(trafficLights.size())) {
        return std::nullopt;
    }

    auto it = trafficLights.begin();
    std::advance(it, index);
    return it->second;
}
