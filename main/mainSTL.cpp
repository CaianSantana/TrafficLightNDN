#include "../include/YamlParser.hpp"
#include "../include/SmartTrafficLight.hpp" 
#include "../include/ProConInterface.hpp" 


int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Uso: " << argv[0] << " <caminho_yaml> <id_semaforo> <log_level>" << std::endl;
        std::cerr << "Níveis de log disponíveis: NONE, ERROR, INFO, DEBUG" << std::endl;
        return 1;
    }

    std::string yaml_path = argv[1];
    int traffic_light_id = -1;
    try {
        traffic_light_id = std::stoi(argv[2]);
    } catch (const std::exception& e) {
        std::cerr << "Erro: ID do semáforo inválido." << std::endl;
        return 1;
    }
    LogLevel logLevel = parseLogLevel(argv[3]);

    try {
        YamlParser parser(yaml_path);
        auto maybeLight = parser.getTrafficLightByIndex(traffic_light_id);

        if (!maybeLight) {
            std::cerr << "Erro: Semáforo com ID " << traffic_light_id << " não encontrado no arquivo." << std::endl;
            return 1;
        }

        SmartTrafficLight light;

        light.setup("/central"); 
        light.loadConfig(maybeLight.value(), logLevel); 
        
        light.run();

    } catch (const std::runtime_error& e) {
        std::cerr << "Erro na inicialização: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}



