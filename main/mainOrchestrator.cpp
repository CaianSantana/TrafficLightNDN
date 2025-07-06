#include "../include/YamlParser.hpp"
#include "../include/Orchestrator.hpp" 

int main() {
    YamlParser parser("../config/test.yaml");

    auto trafficLights = parser.getTrafficLights();
    auto intersections = parser.getIntersections();
    auto greenWaves = parser.getGreenWaves();

    Orchestrator orch = Orchestrator(); 
    orch.loadTopology(trafficLights, intersections, greenWaves);
    orch.setup("/central");
    orch.run();

    return 0;
}
