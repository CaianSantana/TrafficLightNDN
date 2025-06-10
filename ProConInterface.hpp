#ifndef PROCONINTERFACE_HPP
#define PROCONINTERFACE_HPP

#include <ndn-cxx/face.hpp>
#include <ndn-cxx/util/scheduler.hpp>
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

namespace ndn {
class ProConInterface {
public:
    virtual ~ProConInterface() = default;

    virtual void setup(std::string prefix, int id) = 0;
    virtual void run(const std::string& url) = 0;

protected:

    virtual void runConsumer(const std::string& sufix) = 0;

    virtual void onData(const Interest&, const Data& data) = 0;

    virtual void onNack(const Interest& interest, const lp::Nack& nack) = 0;

    virtual void onTimeout(const Interest& interest) = 0;

    virtual Interest createInterest(ndn::Name& name, bool& mustBeFresh, bool& canBePrefix,  ndn::time& time) = 0;

    virtual void sendInterest(const Interest& interest) = 0;

    virtual void runProducer(std::string& sufix = "") = 0;

    virtual void onInterest(const Interest& interest) = 0;

    virtual void onRegisterFailed(const Name& prefix, const std::string& reason) = 0;
};
}

#endif 
