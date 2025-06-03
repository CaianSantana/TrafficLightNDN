#include "SmartTrafficLight.hpp"
#include "ProConInterface.hpp"

namespace ndn{
    namespace traffic{
        class STLNDN : public traffic::SmartTrafficLight, public ndn::ProConInterface {
            public:
                void setup(std::string newPrefix = "/prefixo/exemplo", int newId){
                    m_validator.load("trust-schema.conf");
                    m_validator(m_face);
                    this->prefix = newPrefix;
                    this->id = newId;
                }
                void run(const std::string& sufix) {
                    traffic::Color last_color = traffic::Color::NONE; // ou algum estado neutro inicial

                    while (true) {
                        if (current_color != last_color) {
                            if (current_color == traffic::Color::GREEN) {
                                runProducer();
                            } else if (current_color == traffic::Color::RED) {
                                runConsumer("/solicitacao/tempo-verde");
                            }

                            last_color = current_color;
                        }

                        m_face.processEvents(ndn::time::milliseconds(10));
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                }



            protected:
                void runProducer(){
                    std::cout << "Producing to prefix " << prefix << std::endl;
                   
                    std::string selfPrefix = prefix+"/"+std::to_string(id);
                    m_face.setInterestFilter(selfPrefix,
                                            std::bind(&TimedProCon::onInterest, this, _2),
                                            nullptr,
                                            std::bind(&TimedProCon::onRegisterFailed, this, _1, _2));
                    auto cert = m_keyChain.getPib().getDefaultIdentity().getDefaultKey().getDefaultCertificate();
                    m_certServeHandle = m_face.setInterestFilter(security::extractIdentityFromCertName(cert.getName()),
                                                                [this, cert] (auto&&...) {
                                                                m_face.put(cert);
                                                                },
                                                                std::bind(&TimedProCon::onRegisterFailed, this, _1, _2));
                };
                
                void onData(const Interest&, const Data& data){}

                void onNack(const Interest& interest, const lp::Nack& nack){}

                void onTimeout(const Interest& interest){}

                Interest createInterest(std::string sufix){
                    Name interestName(prefix+"/"+sufix);
                    interestName.appendVersion();

                    Interest interest(interestName);
                    interest.setMustBeFresh(true);
                    interest.setInterestLifetime(6_s);
                    lastNonce = interest.getNonce();

                    return interest;
                }

                void sendInterest(const Interest& interest){
                    m_face.expressInterest(interest,
                                            std::bind(&TimedProCon::onData, this, _1, _2),
                                            std::bind(&TimedProCon::onNack, this, _1, _2),
                                            std::bind(&TimedProCon::onTimeout, this, _1));
                }

                void runConsumer(const std::string& sufix) {
                    std::cout << "Consuming to " << prefix+"/"+sufix << std::endl;
                    m_scheduler.schedule(6_s, [this, sufix] {
                        this->sendInterest(this->createInterest(sufix));

                        if (current_color == traffic::Color::RED) {
                            runConsumer(sufix);
                        }
                    });
                }

                void onInterest(const Interest& interest){}

                void onRegisterFailed(const Name& prefix, const std::string& reason){}

            private:
                boost::asio::io_context m_ioCtx;
                Face m_face{m_ioCtx};
                KeyChain m_keyChain;
                ScopedRegisteredPrefixHandle m_certServeHandle;
                ValidatorConfig m_validator;
                Scheduler m_scheduler{m_ioCtx};
                std::string prefix;
                int id;

        };
    }
}
    


int main(){
    ndn::traffic::STLNDN teste(2, 6, traffic::Status::WEAK);
    teste.start();
}
