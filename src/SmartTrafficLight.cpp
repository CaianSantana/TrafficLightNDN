#include "../include/SmartTrafficLight.hpp"




SmartTrafficLight::SmartTrafficLight() 
   : m_validator(m_face)
{
    m_validator.load("../config/trust-schema.conf");
}

void SmartTrafficLight::setup(const std::string& prefix){
    central = prefix;
}

void SmartTrafficLight::loadConfig(const TrafficLightState& config) {
    constexpr int TA = 3; // Tempo amarelo em segundos

    this->prefix_ = config.name;
    this->start_color = parseColor(config.state);
    this->current_color = this->start_color;
    this->cycle_time = config.cycle;

    this->columns = config.columns;
    this->lines = config.lines;
    this->capacity = columns * lines;
    this->intensity = config.intensity;

    colors_vector = {
        {"GREEN", (cycle_time / 2) - TA},
        {"YELLOW", TA},
        {"RED", cycle_time / 2}
    };
    default_colors_vector = colors_vector;
}

void SmartTrafficLight::run() {
    size_t index = static_cast<size_t>(start_color);
    runProducer("");
    m_scheduler.schedule(ndn::time::milliseconds(0), [this, index] { this->cycle(index); });
    m_face.processEvents();
}

void SmartTrafficLight::cycle(size_t index) {
    if (current_color == Color::ALERT) {
        std::cout << prefix_ << " - ALERT state active." << std::endl;

        generateTraffic();
        m_scheduler.schedule(ndn::time::seconds(1), [this, index] {
            this->cycle(index); 
        });

        return; 
    }

    const auto& [color_str, seconds] = colors_vector[index];
    current_color = static_cast<Color>(index);
    time_left = seconds;

    int count = 0;

    for (; time_left > 0; time_left--) {
        std::cout << prefix_ << " - " << color_str << ": " << time_left << std::endl;
        generateTraffic();

        if (current_color != Color::RED) {
            if (current_color == Color::GREEN && count == 0) {
                full_cicle_vehicles_quantity = 0;
            }
            if (vehicles > 0 && count >= 2) {
                std::lock_guard<std::mutex> lock(mtx);
                int vehicles_to_pass = std::min(vehicles, columns);
                vehicles_to_pass = generateNumber(0, vehicles_to_pass);
                vehicles -= vehicles_to_pass;
                std::cout << "Vehicles passed: " << vehicles_to_pass << std::endl;
            } else {
                count++;
            }
        } else {
            count = 0;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    size_t nextIndex = (index + 1) % colors_vector.size();
    m_scheduler.schedule(ndn::time::milliseconds(0), [this, nextIndex] { this->cycle(nextIndex); });
}


float SmartTrafficLight::calculatePriority() {
    return full_cicle_vehicles_quantity * 0.5f
           + (static_cast<float>(vehicles) / capacity) * 10
           + colors_vector[2].second;
}

int SmartTrafficLight::generateNumber(int min, int max) {
    static std::mt19937 rng(std::chrono::system_clock::now().time_since_epoch().count());
    std::uniform_int_distribution<int> dist(min, max);
    return dist(rng);
}

void SmartTrafficLight::generateTraffic() {
    if (generateNumber(1, 10) < static_cast<int>(intensity) && vehicles < capacity) {
        vehicles++;
        full_cicle_vehicles_quantity++;
        std::cout << "A new vehicle arrived!" << std::endl;
        std::cout << "Vehicles: " << vehicles << std::endl;
    } else if (vehicles == capacity) {
        std::cout << "Max capacity achieved!" << std::endl;
    }
}


void SmartTrafficLight::runConsumer() {
    m_scheduler.schedule(ndn::time::seconds(5), [this] {
        auto interestClock = createInterest(central + "/clock", true, false, 500_ms);
        auto interestCommand = createInterest(central + "/command", true, false, 2000_ms);
        sendInterest(interestClock);
        sendInterest(interestCommand);
        runConsumer();
    });
}

void SmartTrafficLight::runProducer(const std::string& suffix){
    ndn::Name nameSuffix = ndn::Name(prefix_).append(suffix);
  m_face.setInterestFilter(nameSuffix,
      [this](const ndn::InterestFilter& filter, const ndn::Interest& interest) {
        this->onInterest(interest);
      },
      [this](const ndn::Name& nameSuffix, const std::string& reason) {
        this->onRegisterFailed(nameSuffix, reason);
      });
  auto cert = m_keyChain.getPib().getDefaultIdentity().getDefaultKey().getDefaultCertificate();
  m_certServeHandle = m_face.setInterestFilter(security::extractIdentityFromCertName(cert.getName()),
                                                [this, cert] (auto&&...) {
                                                  m_face.put(cert);
                                                },
                                                std::bind(&SmartTrafficLight::onRegisterFailed, this, _1, _2));
  std::cout << "Producing to " << nameSuffix << std::endl;                                                                
}

void SmartTrafficLight::onInterest(const ndn::Interest& interest) {
    std::cout << prefix_ << " - Received Interest: " << interest.getName().toUri() << std::endl;

    
    std::string priority = std::to_string(calculatePriority());

    auto data = std::make_shared<ndn::Data>(interest.getName());
    data->setContent(std::string_view(priority));  
    data->setFreshnessPeriod(ndn::time::seconds(1));

    m_keyChain.sign(*data);
    m_face.put(*data);
}

ndn::Interest SmartTrafficLight::createInterest(const ndn::Name& name, bool mustBeFresh, bool canBePrefix, ndn::time::milliseconds lifetime) {
  ndn::Interest interest(name);
  interest.setMustBeFresh(mustBeFresh);
  interest.setCanBePrefix(canBePrefix);
  interest.setInterestLifetime(lifetime);
  return interest;
}

void SmartTrafficLight::sendInterest(const ndn::Interest& interest) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    interestTimestamps_[interest.getName().toUri()] = std::chrono::steady_clock::now();
  }
  m_face.expressInterest(interest,
                        std::bind(&SmartTrafficLight::onData, this, std::placeholders::_1, std::placeholders::_2),
                        std::bind(&SmartTrafficLight::onNack, this, std::placeholders::_1, std::placeholders::_2),
                        std::bind(&SmartTrafficLight::onTimeout, this, std::placeholders::_1));
}


void SmartTrafficLight::onData(const ndn::Interest& interest, const ndn::Data& data) {
    using namespace std::chrono;

    // Proteja o acesso a interestTimestamps_ se estiver acessando em múltiplas threads
    // std::lock_guard<std::mutex> lock(mutex_); // Descomente se tiver mutex

    std::string content(reinterpret_cast<const char*>(data.getContent().value()), data.getContent().value_size());
    std::string interestUri = interest.getName().toUri();

    // Procura timestamp do Interest para calcular RTT
    auto it = interestTimestamps_.find(interestUri);
    if (it == interestTimestamps_.end()) {
        std::cerr << prefix_ << " - Interest timestamp not found: " << interestUri << std::endl;
        return;
    }

    steady_clock::time_point now = steady_clock::now();
    steady_clock::duration rtt = now - it->second;
    interestTimestamps_.erase(it);  // Limpa o timestamp após calcular RTT

    if (interestUri.find("/clock") != std::string::npos) {
        uint64_t centralTime = 0;
        try {
            centralTime = std::stoull(content);
        } catch (const std::exception& e) {
            std::cerr << prefix_ << " - Failed to parse central time: " << e.what() << std::endl;
            return;
        }

        uint64_t correctedCentralTime = centralTime + duration_cast<milliseconds>(rtt).count() / 2;

        if (stateChangeTimestamp > 0) {
            uint64_t expectedEnd = correctedCentralTime + time_left * 1000;
            int64_t delta = static_cast<int64_t>(expectedEnd) - static_cast<int64_t>(stateChangeTimestamp);
            if (std::abs(delta) > 1000) {
                time_left += delta / 1000;
                std::cout << prefix_ << " - Clock adjusted by " << delta / 1000 << "s\n";
            }
        } else {
            stateChangeTimestamp = correctedCentralTime + time_left * 1000;
        }
    } else if (interestUri.find("/command") != std::string::npos) {
        if (content == "do_nothing")
            return;

        std::vector<Command> commands = parseCommands(content);
        for (const auto& cmd : commands) {
            applyCommand(cmd);
        }
    }
}

std::vector<Command> SmartTrafficLight::parseCommands(const std::string& rawCommand) {
    std::vector<Command> commands;
    std::istringstream ss(rawCommand);
    std::string commandStr;

    while (std::getline(ss, commandStr, ';')) {
        auto sep = commandStr.find(':');
        if (sep == std::string::npos)
            continue;

        Command cmd;
        cmd.type = commandStr.substr(0, sep);
        cmd.value = commandStr.substr(sep + 1);
        commands.push_back(cmd);
    }

    return commands;
}

void SmartTrafficLight::applyCommand(const Command& cmd) {
    if (cmd.type == "set_state") {
        if (cmd.value == "GREEN") current_color = Color::GREEN;
        else if (cmd.value == "YELLOW") current_color = Color::YELLOW;
        else if (cmd.value == "RED") current_color = Color::RED;
        else current_color = Color::ALERT;
    }
    else if (cmd.type == "set_time") {
        int newTime;
        if (cmd.value == "DEFAULT") {
            newTime = getDefaultColorTime(current_color);
        } else {
            newTime = std::stoi(cmd.value);
        }
        time_left = newTime;
        updateColorVectorTime(current_color, newTime);
    }
    else if (cmd.type == "increase_time") {
        int increment = std::stoi(cmd.value);
        time_left += increment;
        updateColorVectorTime(current_color, time_left);
    }
    else if (cmd.type == "decrease_time") {
        int decrement = std::stoi(cmd.value);
        time_left = std::max(1, time_left - decrement);
        updateColorVectorTime(current_color, time_left);
    }
}

void SmartTrafficLight::updateColorVectorTime(Color color, int newTime) {
    size_t index = static_cast<size_t>(color);
    if (index < colors_vector.size()) {
        colors_vector[index].second = newTime;
    }
}

int SmartTrafficLight::getDefaultColorTime(Color color) const {
    size_t index = static_cast<size_t>(color);
    if (index < default_colors_vector.size()) {
        return default_colors_vector[index].second;
    }
    return 10;
}

void SmartTrafficLight::onNack(const ndn::Interest& interest, const ndn::lp::Nack& nack) {
    std::cerr << "[Nack] " << interest.getName().toUri() << " Reason: " << nack.getReason() << std::endl;
}

void SmartTrafficLight::onTimeout(const ndn::Interest& interest) {
    std::cerr << "[Timeout] " << interest.getName().toUri() << std::endl;
}
void SmartTrafficLight::onRegisterFailed(const ndn::Name& nome, const std::string& reason) {
  std::cerr << "Failed to register name: " << nome.toUri() << " Reason: " << reason << std::endl;
}

