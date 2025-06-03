#include <ndn-cxx/face.hpp>
#include <ndn-cxx/security/key-chain.hpp>
#include <ndn-cxx/util/scheduler.hpp>
#include <ndn-cxx/security/validator-config.hpp>
#include <ndn-cxx/lp/nack.hpp>
#include <ndn-cxx/util/logger.hpp>
#include <ndn-cxx/name.hpp>


#include "SmartTrafficLight.hpp"
#include "ProConInterface.hpp"

using namespace ndn;
using namespace traffic;

class STLNDN : public traffic::SmartTrafficLight, public ndn::ProConInterface {
    public:
        STLNDN(int columns, int lines, traffic::Status intensityLevel, traffic::Color start_color = traffic::Color::GREEN)
        : SmartTrafficLight(columns, lines, intensityLevel, start_color),
          m_face(m_ioCtx),
          m_scheduler(m_ioCtx),
          m_validator(m_face)
        {
            m_validator.load("trust-schema.conf");
        }

        void setup(std::string newPrefix, int newId){
            this->prefix = newPrefix;
            this->id = newId;
        }

        void run(const std::string& sufix) {
            traffic::Color last_color = traffic::Color::NONE;

            while (true) {
                traffic::Color color_copy;

                {
                    std::lock_guard<std::mutex> lock(mtx);
                    color_copy = current_color;
                }

                if (color_copy != last_color) {
                    if (color_copy == traffic::Color::GREEN) {
                        runProducer();
                    } else if (color_copy == traffic::Color::RED) {
                        runConsumer(sufix);
                    }

                    last_color = color_copy;
                }

                m_face.processEvents(ndn::time::milliseconds(10));
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }




    protected:
        void runProducer(){
            std::cout << "Producing to prefix " << prefix+"/"+std::to_string(id) << std::endl;
            
            std::string selfPrefix = prefix+"/"+std::to_string(id);
            m_face.setInterestFilter(selfPrefix,
                                    std::bind(&STLNDN::onInterest, this, _2),
                                    nullptr,
                                    std::bind(&STLNDN::onRegisterFailed, this, _1, _2));
            auto cert = m_keyChain.getPib().getDefaultIdentity().getDefaultKey().getDefaultCertificate();
            m_certServeHandle = m_face.setInterestFilter(security::extractIdentityFromCertName(cert.getName()),
                                                        [this, cert] (auto&&...) {
                                                        m_face.put(cert);
                                                        },
                                                        std::bind(&STLNDN::onRegisterFailed, this, _1, _2));
        };
        
        void onData(const Interest&, const Data& data){}

        void onNack(const Interest& interest, const lp::Nack& nack){}

        void onTimeout(const Interest& interest){}

        Interest createInterest(const std::string& sufix){
            Name interestName(prefix+"/"+sufix);
            interestName.appendVersion();

            Interest interest(interestName);
            interest.setMustBeFresh(true);
            interest.setInterestLifetime(6_s);
            return interest;
        }

        void sendInterest(const Interest& interest){
            m_face.expressInterest(interest,
                                    std::bind(&STLNDN::onData, this, _1, _2),
                                    std::bind(&STLNDN::onNack, this, _1, _2),
                                    std::bind(&STLNDN::onTimeout, this, _1));
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

    


traffic::Color parseColor(const std::string& str) {
    if (str == "GREEN") return traffic::Color::GREEN;
    if (str == "YELLOW") return traffic::Color::YELLOW;
    if (str == "RED") return traffic::Color::RED;
    return traffic::Color::NONE;
}

traffic::Status parseStatus(const std::string& str) {
    if (str == "NONE") return traffic::Status::NONE;
    if (str == "WEAK") return traffic::Status::WEAK;
    if (str == "MEDIUM") return traffic::Status::MEDIUM;
    if (str == "INTENSE") return traffic::Status::INTENSE;
    return traffic::Status::NONE;
}

int main(int argc, char* argv[]) {
    if (argc < 7) {
        std::cerr << "Uso: " << argv[0] << " <start_color> <columns> <lines> <intensity> <prefix> <id> <suffix>" << std::endl;
        return 1;
    }

    traffic::Color startColor = parseColor(argv[1]);
    int columns = std::stoi(argv[2]);
    int lines = std::stoi(argv[3]);
    traffic::Status intensity = parseStatus(argv[4]);
    std::string prefix = argv[5];
    int id = std::stoi(argv[6]);  
    std::string suffix = argv[7];

    STLNDN teste(columns, lines, intensity, startColor);
    std::thread temporizer(&traffic::SmartTrafficLight::start, &teste);
    
    teste.setup(prefix, id);
    teste.run(suffix);

    temporizer.join();

    return 0;
}

