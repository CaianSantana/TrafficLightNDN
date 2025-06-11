#ifndef SMARTTRAFFICLIGHT_HPP
#define SMARTTRAFFICLIGHT_HPP

#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <utility>

namespace traffic {

    enum class Status { NONE = 1, WEAK = 2, MEDIUM = 5, INTENSE = 8 };
    enum class Color {GREEN = 0, YELLOW = 1, RED = 2, NONE = 3 };

    class SmartTrafficLight {
    public:
        SmartTrafficLight(int columns, int lines, traffic::Status intensityLevel, traffic::Color start_color = traffic::Color::GREEN);
        void start();
        bool reviewRequest(float otherPriority);

    protected:
        std::vector<std::pair<std::string, int>> colors_vector;
        int columns;
        int lines;
        int capacity;
        int vehicles = 0;
        int full_cicle_vehicles_quantity = 0;
        int time_left;
        float priority = 0.0f;
        traffic::Status intensity;
        traffic::Color start_color;
        std::mutex mtx;
        traffic::Color current_color = traffic::Color::GREEN;
        void changeTime(bool isgreen);
        void calculatePriority();

    private:
        
        int generateNumber(int min, int max);
        void generateTraffic();
        bool hasLowerPriorityThan(float otherPriority);
        
    };
}

#endif // SMARTTRAFFICLIGHT_HPP
