#include "../include/YamlParser.hpp"
#include "../include/Orchestrator.hpp"
#include <iostream> 

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Erro: O caminho para o arquivo .yaml de configuração não foi fornecido." << std::endl;
        std::cerr << "Uso: " << argv[0] << " <caminho_para_o_arquivo.yaml>" << std::endl;
        return 1; // Retorna um código de erro
    }

    YamlParser parser(argv[1]);

    auto trafficLights = parser.getTrafficLights();
    auto intersections = parser.getIntersections();
    auto greenWaves = parser.getGreenWaves();
    auto syncGroups = parser.getSyncGroups();

    Orchestrator orch = Orchestrator();
    orch.loadTopology(trafficLights, intersections, greenWaves, syncGroups);
    orch.setup("/central");
    orch.run();

    return 0;
}