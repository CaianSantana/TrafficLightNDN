#include <iostream> 
#include <string>
#include <unistd.h>
#include <thread>
#include <vector>
#include <random>
#include <chrono> 
#include <mutex>

namespace traffic {
    enum class Status { NONE = 1, WEAK = 2, MEDIUM = 5, INTENSE = 8 };
    enum class Color { GREEN = 0, YELLOW = 1, RED = 2 };

    class SmartTrafficLight {
    public:
        SmartTrafficLight(int columns, int lines, traffic::Status intensityLevel)
            : intensity(intensityLevel) 
        {
            colors_vector.push_back({"green", 24});
            colors_vector.push_back({"yellow", 3});
            colors_vector.push_back({"red", 24});
            this->columns = columns;
            this->lines = lines;
            capacity = columns * lines;
        }

        void run(traffic::Color start_color = traffic::Color::GREEN) {
            size_t index = static_cast<size_t>(start_color);
            while (true) {
                const auto& [color_str, seconds] = colors_vector[index];
                current_color = static_cast<traffic::Color>(index);

                int time_left = seconds;
                int count = 0;

                for (; time_left > 0; time_left--) {
                    std::cout << color_str << ": " << time_left << std::endl;
                    generateTraffic();

                    if (current_color != traffic::Color::RED) {
                        if (current_color == traffic::Color::GREEN && count == 0) {
                            full_cicle_vehicles_quantity = 0;
                        }
                        if (vehicles > 0 && count >= 2) {
                            std::lock_guard<std::mutex> lock(mtx);
                            int vehicles_to_pass = std::min(vehicles, columns);
                            vehicles_to_pass = generateNumber(0, vehicles_to_pass);
                            vehicles -= vehicles_to_pass;
                            std::cout << "Vehicles passed: " << vehicles_to_pass << std::endl;
                        } else {
                            count++;
                        }
                    } else {
                        count = 0;
                        calculatePriority();
                        std::cout << "priority: " << std::to_string(priority) << std::endl;
                    }

                    std::this_thread::sleep_for(std::chrono::seconds(1)); 
                }

                index = (index + 1) % colors_vector.size();  // Próxima cor no ciclo
            }
        }

    private:
        void calculatePriority() {
            priority = full_cicle_vehicles_quantity * 0.5f
                     + (static_cast<float>(vehicles) / capacity) * 10
                     + colors_vector[2].second;  // tempo do vermelho
        }

        int generateNumber(int min, int max) {
            static std::mt19937 rng(std::chrono::system_clock::now().time_since_epoch().count());
            std::uniform_int_distribution<int> dist(min, max);
            return dist(rng);
        }

        void generateTraffic() {
            if (generateNumber(1, 10) < static_cast<int>(intensity) && vehicles < capacity) {
                vehicles++;
                full_cicle_vehicles_quantity++;
                std::cout << "A new vehicle arrived!" << std::endl;
                std::cout << "Vehicles: " << vehicles << std::endl;
            } else if (vehicles == capacity) {
                std::cout << "Max capacity achieved!" << std::endl;
            }
        }

    private:
        std::vector<std::pair<std::string, int>> colors_vector;
        int columns;
        int lines;
        int capacity;
        int vehicles = 0;
        int full_cicle_vehicles_quantity = 0;
        float priority = 0.0f;
        traffic::Status intensity;
        std::mutex mtx;
        traffic::Color current_color = traffic::Color::GREEN;
    };
}

int main(int argc, char** argv)
{
    if (argc < 4 || argc > 5) {
        std::cerr << "Uso: ./stl <columns> <lines> <intensity: NONE, WEAK, MEDIUM, INTENSE> [start_color: GREEN or RED]\n";
        return 1;
    }

    int columns = std::stoi(argv[1]);
    int lines = std::stoi(argv[2]);
    std::string intensityValue = argv[3];
    traffic::Status intensity;

    if (intensityValue == "NONE") intensity = traffic::Status::NONE;
    else if (intensityValue == "WEAK") intensity = traffic::Status::WEAK;
    else if (intensityValue == "MEDIUM") intensity = traffic::Status::MEDIUM;
    else if (intensityValue == "INTENSE") intensity = traffic::Status::INTENSE;
    else {
        std::cerr << "Intensidade inválida. Use: NONE, WEAK, MEDIUM, INTENSE\n";
        return 1;
    }

    traffic::Color start_color = traffic::Color::GREEN;
    if (argc == 5) {
        std::string color_arg = argv[4];
        if (color_arg == "GREEN") start_color = traffic::Color::GREEN;
        else if (color_arg == "RED") start_color = traffic::Color::RED;
        else {
            std::cerr << "Cor inicial inválida. Use: GREEN ou RED\n";
            return 1;
        }
    }

    traffic::SmartTrafficLight stl(columns, lines, intensity);
    std::thread temporizer(&traffic::SmartTrafficLight::run, &stl, start_color);
    temporizer.join();

    return 0;
}
