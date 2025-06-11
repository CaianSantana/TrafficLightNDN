#ifndef ORCHESTRATOR_HPP
#define ORCHESTRATOR_HPP

#include "ProConInterface.hpp"
#include <ndn-cxx/name.hpp>
#include <ndn-cxx/security/key-chain.hpp>
#include <ndn-cxx/util/scheduler.hpp>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <string>
#include <chrono>
#include <mutex>

struct TrafficLightState {
  std::string name;
  std::string state; 
  std::chrono::steady_clock::time_point endTime;
  int priority; 
  std::string command; 
  bool intersection;
};

class Orchestrator : public ndn::ProConInterface {
public:
  Orchestrator();
  ~Orchestrator() override;

  void setup(std::string prefix, int id) override;
  void run(const std::string& url) override;

protected:
  void runProducer(const std::string& suffix); // alterado para aceitar sufixo
  void runConsumer(const std::string& suffix) override;

  void onData(const ndn::Interest&, const ndn::Data& data) override;
  void onNack(const ndn::Interest& interest, const ndn::lp::Nack& nack) override;
  void onTimeout(const ndn::Interest& interest) override;

  ndn::Interest createInterest(ndn::Name& name, bool mustBeFresh, bool canBePrefix, ndn::time::milliseconds lifetime);
  void sendInterest(const ndn::Interest& interest) override;

  void onInterest(const ndn::Interest& interest) override;
  void onRegisterFailed(const ndn::Name& prefix, const std::string& reason) override;

private:
  void produceClockData(const ndn::Interest& interest);
  void produceCommand(std::string semaforoName, const ndn::Interest& interest);
  void loadTopology();
  void reviewPriorities();

private:
  std::unordered_map<std::string, TrafficLightState> trafficLights_;
  ndn::KeyChain keyChain_;
  ndn::Face face_;
  ndn::Scheduler scheduler_;
  std::string prefix_;
  int id_;
  std::unordered_map<std::string, std::chrono::steady_clock::time_point> interestTimestamps_;
  std::mutex mutex_; // para sincronizar acesso a trafficLights_
  uint64_t clockTimestampMs_ = 0;
};

#endif // ORCHESTRATOR_HPP
