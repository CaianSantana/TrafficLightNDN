#include <iostream> 
#include <string>
#include <unistd.h>
#include <thread>
#include <vector>
#include <random>
#include <chrono> 
#include <mutex>



namespace traffic{
    enum class Status { NONE = 1, WEAK = 2, MEDIUM = 5, INTENSE = 8 };
    class SmartTrafficLight{
        public:
            SmartTrafficLight(int columns, int lines, traffic::Status intensityLevel)
                : intensity(intensityLevel) 
            {
                colors_vector.push_back({"green", 24});
                colors_vector.push_back({"yellow", 3});
                colors_vector.push_back({"red", 24});
                capacity = columns * lines;
                std::cout << "Capacidade: " << capacity << std::endl;
                std::cout << "Intensidade: " << static_cast<int>(intensity) << std::endl;
            }

            void run() {
                while (true) {
                    for (const auto& [color, seconds] : colors_vector) {
                        int time_left = seconds;
                        for (; time_left > 0; time_left--) {
                            std::cout << color << ": " << time_left << std::endl;
                            if (color != "red")
                            {
                                if (vehicles>0)
                                {
                                    std::lock_guard<std::mutex> lock(mtx);
                                    int vehicles_to_pass = std::min(vehicles, columns);
                                    vehicles -= vehicles_to_pass;
                                    std::cout << "Vehicles passed: " << vehicles_to_pass << std::endl;
                                }
                            }else{
                                this->calculatePriority();
                                std::cout << "priority: " << std::to_string(priority) << std::endl;
                            }
                            sleep(1);
                        }
                    }
                }
            }
            void generateTraffic(){
                while (true) {
                    if (generateNumber() < static_cast<int>(intensity)) {
                        std::lock_guard<std::mutex> lock(mtx);
                        vehicles += 1;
                        std::cout << "A new vehicle arrived!" << std::endl;
                    }
                    {
                        std::lock_guard<std::mutex> lock(mtx);
                        std::cout << "Vehicles: " << vehicles << std::endl;
                    }
                    sleep(2);
                }
            }





        private:
            void calculatePriority(){
                priority = (static_cast<float>(vehicles) / capacity) + colors_vector[2].second;
            }
            int generateNumber() {
                    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
                    std::mt19937 rng(seed); 
                    std::uniform_int_distribution<int> dist(1, 10);
                    return dist(rng);
                }
        private:
            std::vector<std::pair<std::string, int>> colors_vector;
            int columns;
            int lines;
            int capacity;
            int vehicles = 0;
            float priority;
            traffic::Status intensity;
            std::mutex mtx;
    };
}

int main(int argc, char** argv)
{
    if (argc < 4) {
        std::cerr << "Uso: ./stl <columns> <lines> <intensity: 1-8>\n";
        return 1;
    }

    int columns = std::stoi(argv[1]);
    int lines = std::stoi(argv[2]);
    int intensityValue = std::stoi(argv[3]);

    // Verifica se é um valor válido
    if (intensityValue != 1 && intensityValue != 2 && intensityValue != 5 && intensityValue != 8) {
        std::cerr << "Valores válidos para intensidade: 1 (NONE), 2 (WEAK), 5 (MEDIUM), 8 (INTENSE)\n";
        return 1;
    }

    traffic::Status intensity = static_cast<traffic::Status>(intensityValue);

    traffic::SmartTrafficLight stl(columns, lines, intensity);
    std::thread temporizer(&traffic::SmartTrafficLight::run, &stl);
    std::thread traffic(&traffic::SmartTrafficLight::generateTraffic, &stl);
    temporizer.join();
    traffic.join();

    return 0;
}