#include <iostream> 
#include <string>
#include <unistd.h>
#include <thread>
#include <vector>


namespace traffic{
    class SmartTrafficLight{
        public:
            SmartTrafficLight() {
                colors_vector.push_back({"green", 24});
                colors_vector.push_back({"yellow", 3});
                colors_vector.push_back({"red", 24});
                capacity=columns*lines;
            }

            void run() {
                while (true) {
                    for (const auto& [color, seconds] : colors_vector) {
                        int time_left = seconds;
                        for (; time_left > 0; time_left--) {
                            std::cout << color << ": " << time_left << std::endl;
                            sleep(1);
                        }
                    }
                }
            }

        private:
            void calculatePriority(){
                priority=(vehicles/capacity)+colors_vector[2].second;
            }
        private:
            std::vector<std::pair<std::string, int>> colors_vector;
            int columns;
            int lines;
            int capacity;
            int vehicles;
            int priority;
            
    };
}

int main(int argc, char** argv)
{
    traffic::SmartTrafficLight stl;
    std::thread temporizer(&traffic::SmartTrafficLight::run, &stl);
    temporizer.join();
    return 0;
}