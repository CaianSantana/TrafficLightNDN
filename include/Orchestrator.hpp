#ifndef ORCHESTRATOR_HPP
#define ORCHESTRATOR_HPP

#define MIN_PRIORITY 20

#include <unordered_map>
#include <mutex>
#include "ProConInterface.hpp"
#include <ndn-cxx/util/time.hpp>



#include "Structs.hpp"



class Orchestrator : public ndn::ProConInterface {
public:
  Orchestrator();
  ~Orchestrator() override;

  void loadTopology(const std::map<std::string, TrafficLightState>& trafficLights,
                    const std::map<std::string, Intersection>& intersections);

  void setup(const std::string& prefix) override;
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
  void produceClockData(const ndn::Interest& interest);
  void produceCommand(const std::string& trafficLightName, const ndn::Interest& interest);
  void delegateCommandTo(const std::string& name);
  int getAveragePrioritySTL();
  void handleIntersectionLogic(const std::string& intersectionName);
  bool processGreenWave();
  void updatePriorityList(const std::string& intersectionName, const std::optional<std::string>& updatedLight = std::nullopt);

private:
  std::map<std::string, TrafficLightState> trafficLights_;
  std::map<std::string, Intersection> intersections_;
  std::map<std::string, std::vector<std::string>> waveGroups_;
  std::map<std::string, std::vector<std::pair<std::string, int>>> sortedPriorityCache_;
  boost::asio::io_context m_ioCtx;
  ndn::Face m_face{m_ioCtx};
  ndn::KeyChain m_keyChain;
  ndn::ScopedRegisteredPrefixHandle m_certServeHandle;
  ndn::ValidatorConfig m_validator;
  ndn::Scheduler m_scheduler{m_ioCtx};
  std::string prefix_;
  int id_;
  std::unordered_map<std::string, std::chrono::steady_clock::time_point> interestTimestamps_;
  std::mutex mutex_; 
  uint64_t clockTimestampMs_ = 0;
  std::string lastModified;
};

#endif // ORCHESTRATOR_HPP
