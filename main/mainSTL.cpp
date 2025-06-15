#include "../include/YamlParser.hpp"
#include "../include/SmartTrafficLight.hpp" 
#include "../include/ProConInterface.hpp" 



int main(int argc, char* argv[]) {
    YamlParser parser("../config/test.yaml");

    int id = std::stoi(argv[1]);  
    auto maybeLight = parser.getTrafficLightByIndex(id);

    if (!maybeLight) {
        std::cerr << "Invalid traffic light ID\n";
        return 1;
    }

    SmartTrafficLight light;
    light.setup("/central");
    light.loadConfig(maybeLight.value());
    light.run();
}


