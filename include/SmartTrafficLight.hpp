#ifndef SMARTTRAFFICLIGHT_HPP
#define SMARTTRAFFICLIGHT_HPP


#include "Structs.hpp"
#include "ProConInterface.hpp"
#include "LogLevel.hpp" 

#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <utility>
#include <algorithm>

using namespace std::chrono;

class SmartTrafficLight : public ndn::ProConInterface {
public:
    SmartTrafficLight();
    ~SmartTrafficLight();
    void setup(const std::string& prefix) override;
    void loadConfig(const TrafficLightState& config, LogLevel level);
    void run() override;

protected:
  void runProducer(const std::string& suffix) override;
  void runConsumer() override;

  void onData(const ndn::Interest&, const ndn::Data& data) override;
  void onNack(const ndn::Interest& interest, const ndn::lp::Nack& nack) override;
  void onTimeout(const ndn::Interest& interest) override;

  ndn::Interest createInterest(const ndn::Name& name, bool mustBeFresh, bool canBePrefix, ndn::time::milliseconds lifetime) override;                            
  void sendInterest(const ndn::Interest& interest) override;

  void onInterest(const ndn::Interest& interest) override;
  void onRegisterFailed(const ndn::Name& prefix, const std::string& reason) override;

private:
    void startCycle();
    void cycle();

    void generateTraffic();
    void passVehicles();
    int generateNumber(int min, int max);

    float calculatePriority();

    std::vector<Command> parseContent(const std::string& rawCommand);
    bool applyCommand(const Command& cmd);

    void updateColorVectorTime(Color color, int newTime);
    int getDefaultColorTime(Color color) const;

    void adjustTime(uint64_t correctedCentralTime);
    uint64_t correctCentralTime(uint64_t centralTime);

    void log(LogLevel level, const std::string& message);


private:
    std::thread m_cycleThread;
    std::atomic_bool m_stopFlag{false};
    boost::asio::io_context m_ioCtx;
    ndn::Face m_face{m_ioCtx};
    ndn::KeyChain m_keyChain;
    ndn::ScopedRegisteredPrefixHandle m_certServeHandle;
    ndn::ValidatorConfig m_validator;
    ndn::Scheduler m_scheduler{m_ioCtx};
    std::mutex m_mutex; 

    std::string central;
    std::string prefix_;

    Color start_color = Color::UNKNOWN;
    Color current_color = Color::UNKNOWN;
    size_t index;

    int cycle_time = 0;
    int columns = 0;
    int lines = 0;
    int capacity = 0;
    Status intensity;
    int vehicles = 0;
    int full_cicle_vehicles_quantity = 0;
    bool isBootStrap = true;

    int time_left = 0;
    uint64_t stateChangeTimestamp = 0;

    std::vector<std::pair<std::string, int>> colors_vector;
    std::vector<std::pair<std::string, int>> default_colors_vector;

    std::chrono::steady_clock::time_point lastInterestTimestamp_ = steady_clock::now();

    LogLevel m_logLevel = LogLevel::NONE;

};

#endif // SMART_TRAFFIC_LIGHT_HPP
