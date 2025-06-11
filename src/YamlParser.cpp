#include "../include/YamlParser.hpp"

YamlParser::YamlParser(const std::string& filepath) {
    YAML::Node config = YAML::LoadFile(filepath);
    parse(config);
}

void YamlParser::parse(const YAML::Node& config) {
    constexpr int TA = 3; // Tempo amarelo em segundos

    for (const auto& node : config["traffic-lights"]) {
        std::string name = node["name"].as<std::string>();
        int priority = node["priority"].as<int>();
        int cycleTime = node["cycle_time"].as<int>();
        std::string state = node["state"].as<std::string>();

        TrafficLightState light;
        light.name = name;
        light.priority = priority;
        light.state = state;
        light.command = state;
        light.intersection = false;

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

    for (const auto& node : config["intersections"]) {
        std::string crossName = node["name"].as<std::string>();
        std::vector<std::string> sems = node["traffic-lights"].as<std::vector<std::string>>();

        Intersection cross;
        cross.name = crossName;
        cross.trafficLightNames = sems;
        intersections[crossName] = cross;

        for (const auto& s : sems) {
            if (trafficLights.find(s) != trafficLights.end()) {
                trafficLights[s].intersection = true;
            } else {
                std::cerr << "Aviso: semáforo '" << s << "' no cruzamento '"
                          << crossName << "' não foi definido na seção 'traffic-lights'.\n";
            }
        }
    }
}


const std::map<std::string, TrafficLightState>& YamlParser::getTrafficLights() const {
    return trafficLights;
}

const std::map<std::string, Intersection>& YamlParser::getIntersections() const {
    return intersections;
}
