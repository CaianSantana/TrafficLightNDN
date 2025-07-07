#include "../include/YamlParser.hpp"
#include "../include/Orchestrator.hpp"
#include <iostream> 

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Uso: " << argv[0] << " <caminho_yaml> <log_level>" << std::endl;
        std::cerr << "Níveis de log disponíveis: NONE, ERROR, INFO, DEBUG" << std::endl;
        return 1;
    }

    YamlParser parser(argv[1]);
    LogLevel logLevel = parseLogLevel(argv[2]);

    auto trafficLights = parser.getTrafficLights();
    auto intersections = parser.getIntersections();
    auto greenWaves = parser.getGreenWaves();
    auto syncGroups = parser.getSyncGroups();

    Orchestrator orch = Orchestrator();
    orch.setup("/central");
    orch.loadConfig(trafficLights, intersections, greenWaves, syncGroups, logLevel);
    orch.run();

    return 0;
}