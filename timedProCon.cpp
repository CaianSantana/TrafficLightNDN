#include <ndn-cxx/face.hpp>

#include <ndn-cxx/util/scheduler.hpp>
#include <boost/asio/io_context.hpp>

#include <ndn-cxx/security/key-chain.hpp>
#include <ndn-cxx/security/signing-helpers.hpp>
#include <ndn-cxx/security/validator-config.hpp>

#include <iostream>
#include <string>


namespace ndn {
    namespace exemplo {
        class TimedProCon {
            public:
                TimedProCon()
                    : m_validator(m_face) // <- inicialização correta do membro
                {
                    m_validator.load("trust-schema.conf");
                }
                void run() {
                    std::string nome = "/exemplo/appTeste/dadoAleatorio";
                    Name interestName(nome);
                    interestName.appendVersion();

                    Interest interest(interestName);
                    interest.setMustBeFresh(true);
                    interest.setInterestLifetime(6_s);
                    
                    std::cout << "Sending interest " << interest << std::endl;
                    m_face.expressInterest(interest,
                                            std::bind(&TimedProCon::onData, this, _1, _2),
                                            std::bind(&TimedProCon::onNack, this, _1, _2),
                                            std::bind(&TimedProCon::onTimeout, this, _1));

                    m_face.setInterestFilter(nome,
                                            std::bind(&TimedProCon::onInterest, this, _2),
                                            nullptr, // RegisterPrefixSuccessCallback is optional
                                            std::bind(&TimedProCon::onRegisterFailed, this, _1, _2));

                    auto cert = m_keyChain.getPib().getDefaultIdentity().getDefaultKey().getDefaultCertificate();
                    m_certServeHandle = m_face.setInterestFilter(security::extractIdentityFromCertName(cert.getName()),
                                                                [this, cert] (auto&&...) {
                                                                m_face.put(cert);
                                                                },
                                                                std::bind(&TimedProCon::onRegisterFailed, this, _1, _2));
                    this->scheduleNextInterest();
                    m_face.processEvents();
                }
            private:
                void onData(const Interest&, const Data& data) {
                    std::cout << "Received Data " << data << std::endl;

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
                void onNack(const Interest& interest, const lp::Nack& nack) const {
                    std::cout << "Received Nack with reason " << nack.getReason() << std::endl;
                }

                void onTimeout(const Interest& interest) const {
                    std::cout << "Timeout for " << interest << std::endl;
                }
                void delayedInterest()
                {
                    std::cout << "One more Interest, delayed by the scheduler" << std::endl;

                    Name interestName("/exemplo/appTeste/dadoAleatorio");
                    interestName.appendVersion();

                    Interest interest(interestName);
                    interest.setMustBeFresh(true);
                    interest.setInterestLifetime(2_s);

                    std::cout << "Sending Interest " << interest << std::endl;
                    m_face.expressInterest(interest,
                                        std::bind(&TimedProCon::onData, this, _1, _2),
                                        std::bind(&TimedProCon::onNack, this, _1, _2),
                                        std::bind(&TimedProCon::onTimeout, this, _1));
                }
                void scheduleNextInterest()
                    {
                    m_scheduler.schedule(3_s, [this] {
                        this->delayedInterest();
                        this->scheduleNextInterest(); // ⬅️ agenda o próximo envio
                    });
                    }
                            private:
                void onInterest(const Interest& interest) {
                        std::cout << ">> I: " << interest << std::endl;

                        // Create Data packet
                        auto data = std::make_shared<Data>();
                        data->setName(interest.getName());
                        data->setFreshnessPeriod(10_s);
                        data->setContent("Hello, world!");

                        // In order for the consumer application to be able to validate the packet, you need to setup
                        // the following keys:
                        // 1. Generate example trust anchor
                        //
                        //         ndnsec key-gen /example
                        //         ndnsec cert-dump -i /example > example-trust-anchor.cert
                        //
                        // 2. Create a key for the producer and sign it with the example trust anchor
                        //
                        //         ndnsec key-gen /example/testApp
                        //         ndnsec sign-req /example/testApp | ndnsec cert-gen -s /example -i example | ndnsec cert-install -

                        // Sign Data packet with default identity
                        m_keyChain.sign(*data);
                        // m_keyChain.sign(*data, signingByIdentity(<identityName>));
                        // m_keyChain.sign(*data, signingByKey(<keyName>));
                        // m_keyChain.sign(*data, signingByCertificate(<certName>));
                        // m_keyChain.sign(*data, signingWithSha256());

                        // Return Data packet to the requester
                        std::cout << "<< D: " << *data << std::endl;
                        m_face.put(*data);
                    }

                    void onRegisterFailed(const Name& prefix, const std::string& reason) {
                        std::cerr << "ERROR: Failed to register prefix '" << prefix
                                << "' with the local forwarder (" << reason << ")\n";
                        m_face.shutdown();
                    }

            private:
                boost::asio::io_context m_ioCtx;
                Face m_face{m_ioCtx};
                KeyChain m_keyChain;
                ScopedRegisteredPrefixHandle m_certServeHandle;
                ValidatorConfig m_validator;
                Scheduler m_scheduler{m_ioCtx};
        };
    }
}

int
main(int argc, char** argv)
{
    try{
        ndn::exemplo::TimedProCon TimedProCon;
        TimedProCon.run();
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }
}