#ifndef ORCHESTRATOR_HPP
#define ORCHESTRATOR_HPP

// =================================================================================
// Arquivo de Configuração
// =================================================================================
namespace config {
  constexpr int INCREASE_GREEN_MS = 5000;
  constexpr int DECREASE_RED_MS = 3000;
  constexpr float MIN_PRIORITY = 20.0;
  constexpr int YELLOW_TIME_MS = 3000;
  constexpr int GREEN_BASE_TIME_MS = 15000;
  constexpr int RTT_WINDOW_SIZE = 10;
  constexpr int RECOVERY_RED_TIME_MS = 5000; 
  constexpr double LOW_PRIORITY_WAVE_FACTOR = 0.75; 

}

// =================================================================================
// Includes da Standard Library
// =================================================================================
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <mutex>
#include <thread> 
#include <atomic> 
#include <chrono> 
#include <optional>
#include <cstddef>

// =================================================================================
// Includes da Biblioteca NDN-CXX
// =================================================================================
#include <ndn-cxx/face.hpp>
#include <ndn-cxx/interest.hpp>
#include <ndn-cxx/data.hpp>
#include <ndn-cxx/lp/nack.hpp>
#include <ndn-cxx/security/key-chain.hpp>
#include <ndn-cxx/security/validator-config.hpp>
#include <ndn-cxx/util/scheduler.hpp>
#include <ndn-cxx/util/time.hpp>

// =================================================================================
// Includes do Projeto
// =================================================================================
#include "ProConInterface.hpp" 
#include "Structs.hpp"   
#include "LogLevel.hpp"       

class Orchestrator : public ndn::ProConInterface {
public:
  Orchestrator();
  ~Orchestrator() override;

  Orchestrator(const Orchestrator&) = delete;
  Orchestrator& operator=(const Orchestrator&) = delete;

  void loadConfig(const std::map<std::string, TrafficLightState>& trafficLights,
                    const std::map<std::string, Intersection>& intersections,
                    const std::vector<GreenWaveGroup>& greenWaves,
                    const std::vector<SyncGroup>& syncGroups,
                    LogLevel level);

  void setup(const std::string& prefix) override;
  void run() override;

protected:
  void runProducer(const std::string& suffix) override;
  void runConsumer() override;
  
  void onInterest(const ndn::Interest& interest) override;
  void onData(const ndn::Interest&, const ndn::Data& data) override;
  void onNack(const ndn::Interest& interest, const ndn::lp::Nack& nack) override;
  void onTimeout(const ndn::Interest& interest) override;
  void onRegisterFailed(const ndn::Name& prefix, const std::string& reason) override;
  
  ndn::Interest createInterest(const ndn::Name& name, bool mustBeFresh, bool canBePrefix, ndn::time::milliseconds lifetime) override;
  void sendInterest(const ndn::Interest& interest) override;

private:
  void cycle();
  void produce(const std::string& trafficLightName, const ndn::Interest& interest);
  
  void generateIntersectionCommand(const Intersection& intersection, const std::string& requesterName);
  void forceCycleStart(const std::string& intersectionName);
  void processGreenWaves();
  void processSyncGroups();
  void assignPriorityCommands();
  void processIntersections(const int& allRedTimeoutCycles);

  void updatePriorityList(const std::string& intersectionName);
  
  int recordRTT(const std::string& interestName);
  int getAverageRTT() const;
  const Intersection* findIntersectionFor(const std::string& lightName) const;

  void log(LogLevel level, const std::string& message);

private:
  boost::asio::io_context m_ioCtx;
  long long m_cycleCount = 0;
  ndn::Face m_face;
  ndn::KeyChain m_keyChain;
  ndn::ValidatorConfig m_validator;
  ndn::Scheduler m_scheduler;
  ndn::ScopedRegisteredPrefixHandle m_certServeHandle;
  
  std::jthread m_cycleThread;
  std::atomic_bool m_stopFlag{false};
  mutable std::mutex mutex_;

  std::string prefix_;
  std::map<std::string, TrafficLightState> trafficLights_;
  std::map<std::string, Intersection> intersections_;
  std::vector<GreenWaveGroup> greenWaves_;
  std::vector<SyncGroup> syncGroups_;
  std::map<std::string, std::vector<std::pair<std::string, int>>> sortedPriorityCache_;
  std::map<std::string, std::chrono::steady_clock::time_point> m_lastPriorityCommandTime;
  std::map<std::string, std::string> m_activeLightPerIntersection;

  
  std::string lastModified;
  std::unordered_map<std::string, std::chrono::steady_clock::time_point> interestTimestamps_;
  std::map<std::string, int> m_allRedCounter;
  std::vector<int> rttHistory_;

  LogLevel m_logLevel = LogLevel::NONE;
};

#endif // ORCHESTRATOR_HPP