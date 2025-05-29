#include <ndn-cxx/face.hpp>
#include <ndn-cxx/security/validator-config.hpp>

#include <iostream>

namespace ndn{
    namespace exemplo{
        class Consumer{
            public:
                Consumer()
                    : m_validator(m_face) // <- inicialização correta do membro
                {
                    m_validator.load("trust-schema.conf");
                }

                void 
                run(){
                    Name interestName("/exemplo/appTeste/dadoAleatorio");
                    interestName.appendVersion();

                    Interest interest(interestName);
                    interest.setMustBeFresh(true);
                    interest.setInterestLifetime(6_s);

                    std::cout << "Sending interest " << interest << std::endl;
                    m_face.expressInterest(interest,
                                            std::bind(&Consumer::onData, this, _1, _2),
                                            std::bind(&Consumer::onNack, this, _1, _2),
                                            std::bind(&Consumer::onTimeout, this, _1));
                    m_face.processEvents();
                }
            private:
                void
                onData(const Interest&, const Data& data){
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
                void
                onNack(const Interest& interest, const lp::Nack& nack) const
                {
                    std::cout << "Received Nack with reason " << nack.getReason() << std::endl;
                }

                void
                onTimeout(const Interest& interest) const
                {
                    std::cout << "Timeout for " << interest << std::endl;
                }

            private:
                Face m_face;
                ValidatorConfig m_validator;
            };
    }
}

int
main(int argc, char** argv)
{
    try{
        ndn::exemplo::Consumer consumer;
        consumer.run();
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }
}