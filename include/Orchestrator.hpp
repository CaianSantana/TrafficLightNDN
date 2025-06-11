#ifndef ORCHESTRATOR_HPP
#define ORCHESTRATOR_HPP

#define MIN_PRIORITY 20

#include "ProConInterface.hpp"
#include <ndn-cxx/name.hpp>
#include <ndn-cxx/face.hpp>
#include <ndn-cxx/security/key-chain.hpp>
#include <ndn-cxx/security/signing-helpers.hpp>
#include <ndn-cxx/security/validator-config.hpp>
#include <ndn-cxx/util/scheduler.hpp>
#include <ndn-cxx/util/time.hpp>
#include <boost/asio/io_context.hpp>


#include <unordered_map>
#include <string>
#include <chrono>
#include <mutex>
#include <iostream>
#include "Structs.hpp"

using namespace ndn;
using namespace ndn::security;

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

  ndn::Interest createInterest(ndn::Name& name, bool mustBeFresh, bool canBePrefix,
                               ndn::time::milliseconds lifetime) override;
  void sendInterest(const ndn::Interest& interest) override;

  void onInterest(const ndn::Interest& interest) override;
  void onRegisterFailed(const ndn::Name& prefix, const std::string& reason) override;

private:
  void produceClockData(const ndn::Interest& interest);
  void produceCommand(const std::string& trafficLightName, const ndn::Interest& interest);
  void reviewPriorities();
  std::vector<std::string> getHigherPrioritySTL();
  bool processIntersections(const std::vector<std::string>& candidates);
  void processGreenWave();

private:
  std::map<std::string, TrafficLightState> trafficLights_;
  std::map<std::string, Intersection> intersections_;
  std::map<std::string, std::vector<std::string>> waveGroups_;
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
