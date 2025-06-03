#include "SmartTrafficLight.hpp"
#include <random>
#include <chrono>
#include <unistd.h>

namespace traffic {

    SmartTrafficLight::SmartTrafficLight::SmartTrafficLight(int columns, int lines, traffic::Status intensityLevel, traffic::Color start_color)
        : intensity(intensityLevel) 
    {
        colors_vector.push_back({"green", 24});
        colors_vector.push_back({"yellow", 3});
        colors_vector.push_back({"red", 24});
        this->columns = columns;
        this->lines = lines;
        capacity = columns * lines;
        this->current_color, this->start_color = start_color;
    }

    void SmartTrafficLight::start() {
        size_t index = static_cast<size_t>(start_color);
        while (true) {
            const auto& [color_str, seconds] = colors_vector[index];
            current_color = static_cast<traffic::Color>(index);

            time_left = seconds;
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

            index = (index + 1) % colors_vector.size();
        }
    }

    void SmartTrafficLight::reviewRequest(float otherPriority){
        if (this->hasLowerPriorityThan(otherPriority) && this->current_color == traffic::Color::GREEN) {
            this->changeTime(true); 
        } else if (this->hasLowerPriorityThan(otherPriority) && this->current_color == traffic::Color::RED){
            this->changeTime(false); 
        }
    }

    void SmartTrafficLight::calculatePriority() {
        priority = full_cicle_vehicles_quantity * 0.5f
                 + (static_cast<float>(vehicles) / capacity) * 10
                 + colors_vector[2].second;
    }

    int SmartTrafficLight::generateNumber(int min, int max) {
        static std::mt19937 rng(std::chrono::system_clock::now().time_since_epoch().count());
        std::uniform_int_distribution<int> dist(min, max);
        return dist(rng);
    }

    void SmartTrafficLight::generateTraffic() {
        if (generateNumber(1, 10) < static_cast<int>(intensity) && vehicles < capacity) {
            vehicles++;
            full_cicle_vehicles_quantity++;
            std::cout << "A new vehicle arrived!" << std::endl;
            std::cout << "Vehicles: " << vehicles << std::endl;
        } else if (vehicles == capacity) {
            std::cout << "Max capacity achieved!" << std::endl;
        }
    }

    bool SmartTrafficLight::hasLowerPriorityThan(float otherPriority){
        return priority < otherPriority;
    }

    void SmartTrafficLight::changeTime(bool isgreen){
        if (colors_vector[0].second < 36 &&
            colors_vector[2].second > 12 && 
            !isgreen) {
            colors_vector[0].second += 6;
            colors_vector[2].second -= 6;
            std::cout << "Green time increased to " << colors_vector[0].second << " and Red time decreased to " << colors_vector[2].second << std::endl;
        } else if(colors_vector[2].second < 36 &&
                  colors_vector[0].second > 12 && 
                  isgreen){
            colors_vector[2].second += 6;
            colors_vector[0].second -= 6;
            std::cout << "Red time increased to " << colors_vector[2].second << " and Green time decreased to " << colors_vector[0].second << std::endl;
        } else {
            std::cerr << "Invalid Operation." << std::endl;
        }
    }

}
