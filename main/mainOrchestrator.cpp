#include "../include/YamlParser.hpp"
#include "../include/Orchestrator.hpp" 

int main() {
    YamlParser parser("../config/test.yaml");

    auto semaforos = parser.getTrafficLights();
    auto cruzamentos = parser.getIntersections();

    Orchestrator orch = Orchestrator(); 
    orch.loadTopology(semaforos, cruzamentos);
    orch.setup("/central");
    orch.run();

    return 0;
}
