#ifndef PROCONINTERFACE_HPP
#define PROCONINTERFACE_HPP

#include <ndn-cxx/name.hpp>
#include <ndn-cxx/face.hpp>
#include <ndn-cxx/lp/nack.hpp>
#include <ndn-cxx/util/scheduler.hpp>
#include <ndn-cxx/util/logger.hpp>
#include <boost/asio/io_context.hpp>
#include <ndn-cxx/security/key-chain.hpp>
#include <ndn-cxx/security/signing-helpers.hpp>
#include <ndn-cxx/security/validator-config.hpp>

#include <iostream>
#include <string>
#include <random>
#include <chrono> 
#include <unistd.h>
#include <typeinfo>



using namespace std::chrono_literals;
using namespace ndn;
using namespace ndn::security;


namespace ndn {
class ProConInterface {
public:
  virtual ~ProConInterface() = default;

  virtual void setup(const std::string& prefix) = 0;
  virtual void run() = 0;

protected:
  virtual void runProducer(const std::string& suffix) = 0;
  virtual void runConsumer() = 0;

  virtual void onData(const ndn::Interest&, const ndn::Data&) = 0;
  virtual void onNack(const ndn::Interest&, const ndn::lp::Nack&) = 0;
  virtual void onTimeout(const ndn::Interest&) = 0;

  virtual ndn::Interest createInterest(const ndn::Name& name, bool mustBeFresh, bool canBePrefix, ndn::time::milliseconds lifetime) = 0;
  virtual void sendInterest(const ndn::Interest&) = 0;

  virtual void onInterest(const ndn::Interest&) = 0;
  virtual void onRegisterFailed(const ndn::Name& prefix, const std::string& reason) = 0;
};
}

#endif 
