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

enum class InterestStatus { NONE, DATA, NACK, TIMEOUT, NOROUTE };
std::string to_string(InterestStatus status) {
    switch (status) {
        case InterestStatus::NONE:
            return "NONE";
        case InterestStatus::DATA:
            return "DATA";
        case InterestStatus::NACK:
            return "NACK";
        case InterestStatus::TIMEOUT:
            return "TIMEOUT";
        case InterestStatus::NOROUTE:
            return "NOROUTE";
        default:
            return "UNKNOWN";
    }
}


namespace ndn {
    namespace exemplo {
        class TimedProCon {
            public:
                TimedProCon()
                    : m_validator(m_face)
                {
                    m_validator.load("trust-schema.conf");
                }

                
                void setup() {
                    while (interestStatus != InterestStatus::TIMEOUT && interestStatus != InterestStatus::NOROUTE)
                    {
                        id = this->generateId();
                        std::string url=std::to_string(id);
                        std::cout << url << std::endl;
                        m_scheduler.schedule(3_s, [this, url] {
                            this->sendInterest(this->createInterest(url));
                        });
                        m_face.processEvents();

                    }
                }

                void run() {
                    this->runProducer();
                    this->runConsumer();
                   
                    m_face.processEvents();
                }
            private:
                
                void runProducer(){
                    std::cout << "Producing to prefix " << prefix << std::endl;
                   
                    std::string selfPrefix = prefix+"/"+std::to_string(id);
                    m_face.setInterestFilter(selfPrefix,
                                            std::bind(&TimedProCon::onInterest, this, _2),
                                            nullptr,
                                            std::bind(&TimedProCon::onRegisterFailed, this, _1, _2));
                    std::string prefixDiscovery = prefix+"/discovery/";
                    m_face.setInterestFilter(prefixDiscovery,
                                            std::bind(&TimedProCon::onInterest, this, _2),
                                            nullptr,
                                            std::bind(&TimedProCon::onRegisterFailed, this, _1, _2));
                    auto cert = m_keyChain.getPib().getDefaultIdentity().getDefaultKey().getDefaultCertificate();
                    m_certServeHandle = m_face.setInterestFilter(security::extractIdentityFromCertName(cert.getName()),
                                                                [this, cert] (auto&&...) {
                                                                m_face.put(cert);
                                                                },
                                                                std::bind(&TimedProCon::onRegisterFailed, this, _1, _2));
                }
                
                void onData(const Interest&, const Data& data) {
                    std::cout << "Received Data " << data << std::endl;
                    interestStatus = InterestStatus::DATA;

                    m_validator.validate(
                        data,
                        [] (const Data&) {
                            std::cout << "Data conforms to trust schema" << std::endl;
                        },
                        [] (const Data&, const security::ValidationError& error) {
                            std::cout << "Error authenticating data: " << error << std::endl;
                        }
                    );
                }
                
                void onNack(const Interest& interest, const lp::Nack& nack) {
                    std::cout << "Received Nack with reason " << nack.getReason() << std::endl;
                    if (nack.getReason() == lp::NackReason::NO_ROUTE){
                        interestStatus = InterestStatus::NOROUTE;
                        return;
                    }
                    interestStatus = InterestStatus::NACK;
                }

                void onTimeout(const Interest& interest) {
                    std::cout << "Timeout for " << interest << std::endl;
                    interestStatus = InterestStatus::TIMEOUT;
                }
                
                Interest createInterest(std::string url="") {
                    Name interestName(prefix+"/"+url);
                    interestName.appendVersion();

                    Interest interest(interestName);
                    interest.setMustBeFresh(true);
                    interest.setInterestLifetime(6_s);
                    lastNonce = interest.getNonce();

                    return interest;
                }

                void sendInterest(const Interest& interest) {
                    logConsumer("Sending interest ");
                    m_face.expressInterest(interest,
                                            std::bind(&TimedProCon::onData, this, _1, _2),
                                            std::bind(&TimedProCon::onNack, this, _1, _2),
                                            std::bind(&TimedProCon::onTimeout, this, _1));
                }
                
                int generateId() {
                    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
                    std::mt19937 rng(seed); 
                    std::uniform_int_distribution<int> dist(10000, 99999);
                    return dist(rng);
                }

                void runConsumer(std::string url="")
                    {
                    m_scheduler.schedule(3_s, [this, url] {
                        this->sendInterest(this->createInterest(url));
                        this->runConsumer();
                    });
                    }
            private:
                void onInterest(const Interest& interest) {
                        logProducer("interest received");
                        std::cout << ">> I: " << interest << std::endl;
                        std::cout << interest.toUri() << std::endl;
                        Name interestName(prefix+"/"+std::to_string(id));

                        if (lastNonce == interest.getNonce()) {
                            sleep(1);
                            return;
                        }

                        auto data = std::make_shared<Data>();
                        data->setName(interest.getName());
                        data->setFreshnessPeriod(10_s);
                        if (interest.getName().toUri() ==(prefix + "/discovery/")){
                            data->setContent(std::to_string(id));
                        }
                        else{
                            data->setContent("Hello world!");
                        }

                        m_keyChain.sign(*data);
                        logProducer("Data Sent");
                        std::cout << "<< D: " << *data << std::endl;
                        m_face.put(*data);
                    }

                void onRegisterFailed(const Name& prefix, const std::string& reason) {
                        std::cerr << "ERROR: Failed to register prefix '" << prefix
                                << "' with the local forwarder (" << reason << ")\n";
                        m_face.shutdown();
                    }

                std::string logConsumer(std::string phrase){
                    std::string logMsg = "[CONSUMER] " + phrase;
                    std::cout << logMsg << std::endl;
                    return logMsg; 
                }
                std::string logProducer(std::string phrase){
                    std::string logMsg = "[PRODUCER] " + phrase;
                    std::cout << logMsg << std::endl;
                    return logMsg; 
                }
            private:
                boost::asio::io_context m_ioCtx;
                Face m_face{m_ioCtx};
                KeyChain m_keyChain;
                ScopedRegisteredPrefixHandle m_certServeHandle;
                ValidatorConfig m_validator;
                Scheduler m_scheduler{m_ioCtx};
                std::string prefix = "/exemplo/appTeste/dadoAleatorio";
                int id;
                InterestStatus interestStatus = InterestStatus::NONE;
                ndn::Interest::Nonce lastNonce;
        };
    }
}

int
main(int argc, char** argv)
{
    try{
        ndn::exemplo::TimedProCon TimedProCon;
        TimedProCon.setup();
        TimedProCon.run();
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }
}