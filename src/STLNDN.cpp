#include "../include/SmartTrafficLight.hpp"
#include "../include/ProConInterface.hpp"

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
        ~STLNDN() {
            stopConsumerThread();
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
                    } else if (color_copy == traffic::Color::RED && !recentConsume) {
                        runConsumer(sufix);
                    }

                    last_color = color_copy;
                    recentConsume = false;
                }

                m_face.processEvents(ndn::time::milliseconds(10));
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }




    protected:

        void stopConsumerThread() {
            m_stopRequested = true;
            if (m_consumerThread.joinable()) {
                m_consumerThread.join();
            }
        }
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
        
        void onData(const Interest&, const Data& data){
            std::cout << "Received Data " << data << std::endl;
            m_validator.validate(
                data,
                [] (const Data&) {
                    std::cout << "Data conforms to trust schema" << std::endl;
                },
                [] (const Data&, const security::ValidationError& error) {
                    std::cout << "Error authenticating data: " << error << std::endl;
                    return;
                }
            );

            std::string content(reinterpret_cast<const char*>(data.getContent().value()),
                                data.getContent().value_size());
            std::cout << ">> Content: " << content << std::endl;

            if (content.find("Accepted") != std::string::npos) {
                std::cout << ">>> Request accepted. Proceeding with adjustments..." << std::endl;
                recentConsume = true;
                this->changeTime(true);
            }
            else if (content.find("Rejected") != std::string::npos) {
                std::cout << ">>> Request rejected. Reason: " << content << std::endl;
            }
            else {
                std::cout << ">>> Unknown response content." << std::endl;
            }

            
        }

        void onNack(const Interest& interest, const lp::Nack& nack){
            std::cout << "Received Nack with reason " << nack.getReason() << std::endl;
                    if (nack.getReason() == lp::NackReason::NO_ROUTE){
                        //interestStatus = InterestStatus::NOROUTE;
                        return;
                    }
                    //interestStatus = InterestStatus::NACK;
        }

        void onTimeout(const Interest& interest){
            std::cout << "Timeout for " << interest << std::endl;
            //interestStatus = InterestStatus::TIMEOUT;
        }

        Interest createInterest(const std::string& sufix) {
            Name interestName(prefix + "/" + sufix + "/request-green/" + std::to_string(priority));
            interestName.appendVersion();

            Interest interest(interestName);
            interest.setMustBeFresh(true);
            interest.setInterestLifetime(1_s);
            return interest;
        }

        void sendInterest(const Interest& interest){
            m_face.expressInterest(interest,
                                    std::bind(&STLNDN::onData, this, _1, _2),
                                    std::bind(&STLNDN::onNack, this, _1, _2),
                                    std::bind(&STLNDN::onTimeout, this, _1));
        }

        void runConsumer(const std::string& sufix) {
            std::cout << "Consuming to " << prefix + "/" + sufix << std::endl;

            if (m_consumerThread.joinable()) {
                m_stopRequested = true;
                m_consumerThread.join();
                m_stopRequested = false;
            }

            m_consumerThread = std::thread([this, sufix] {
                while (!m_stopRequested) {
                    {
                        std::lock_guard<std::mutex> lock(mtx);
                        if (current_color != traffic::Color::RED || recentConsume) {
                            break;
                        }
                    }

                    this->calculatePriority();
                    Interest interest = this->createInterest(sufix);
                    this->sendInterest(interest);

                    for (int i = 0; i < 60 && !m_stopRequested; ++i) // Espera até 6s, em passos de 100ms
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            });
        }


        void onInterest(const Interest& interest){
            std::cout << "interest received\n";
            std::cout << ">> I: " << interest << std::endl;

            const Name& name = interest.getName();
            std::string lastComponent = name.at(name.size() - 2).toUri(); // prioridade
            std::string command = name.at(name.size() - 3).toUri();       // request-green, discovery, etc.

            std::cout << "Command: " << command << ", Priority: " << lastComponent << std::endl;

            auto data = std::make_shared<Data>();

            data->setName(interest.getName());
            data->setFreshnessPeriod(1_s);

            if (command == "discovery") {
                data->setContent(std::to_string(id));
            } else if (command == "request-green") {
                try {
                    float otherPriority = std::stof(lastComponent);
                    if(this->reviewRequest(otherPriority)){
                        data->setContent("Accepted.");
                    }else{
                        data->setContent("Rejected.");
                    }
                    
                } catch (const std::exception& e) {
                    std::cerr << "Error parsing priority: " << e.what() << std::endl;
                    data->setContent("Invalid priority format");
                }
            } else {
                data->setContent("Unknown request");
            }
            m_keyChain.sign(*data);
            std::cout << "<< D: " << *data << std::endl;
            m_face.put(*data);
        }

        void onRegisterFailed(const Name& prefix, const std::string& reason){
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
        std::string prefix;
        int id;
        bool recentConsume = false;
        std::thread m_consumerThread;
        std::atomic<bool> m_stopRequested = false;

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

