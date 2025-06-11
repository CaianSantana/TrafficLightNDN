#include "YamlParser.hpp"
#include "Orchestrator.hpp" 

int main() {
    YamlParser parser("../test.yaml");

    auto semaforos = parser.getTrafficLights();
    auto cruzamentos = parser.getIntersections();

    Orchestrator orch = Orchestrator(); 
    orch.loadTopology(semaforos, cruzamentos);
    orch.setup("/central");
    orch.run();

    return 0;
}
