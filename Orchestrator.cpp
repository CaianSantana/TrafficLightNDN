#include "Orchestrator.hpp"

Orchestrator::Orchestrator() 
   : m_validator(m_face)
{
    m_validator.load("../trust-schema.conf");
}

Orchestrator::~Orchestrator() {
  m_face.shutdown();
}

void Orchestrator::setup(const std::string& prefix) {
  prefix_ = std::move(prefix);
}

void Orchestrator::loadTopology(const std::map<std::string, TrafficLightState>& trafficLights,
                    const std::map<std::string, Intersection>& intersections)
{
    trafficLights_ = trafficLights;
    for (auto& [name, state] : trafficLights_) {
      state.command = "do_nothing";
    }

    intersections_ = intersections;
    waveGroups_.clear();

    for (const auto& [name, state] : trafficLights_) {
        std::size_t lastSlash = name.rfind('/');
        if (lastSlash == std::string::npos) continue;

        std::string prefix = name.substr(0, lastSlash);
        waveGroups_[prefix].push_back(name);
    }

    for (auto& [prefix, group] : waveGroups_) {
        std::sort(group.begin(), group.end(), [](const std::string& a, const std::string& b) {
            int idA = std::stoi(a.substr(a.rfind('/') + 1));
            int idB = std::stoi(b.substr(b.rfind('/') + 1));
            return idA < idB;
        });
    }

    std::cout << "[loadTopology] Green waves detected:\n";
    for (const auto& [prefix, group] : waveGroups_) {
        std::cout << " - " << prefix << ": ";
        for (const auto& name : group) std::cout << name << " ";
        std::cout << std::endl;
    }
}

void Orchestrator::run() {
    m_scheduler.schedule(ndn::time::seconds(1), [this]{ runConsumer(); });
    runProducer("command");
    runProducer("clock");
    m_face.processEvents();
}

void Orchestrator::runProducer(const std::string& suffix){
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
                                                std::bind(&Orchestrator::onRegisterFailed, this, _1, _2));
  std::cout << "Producing to " << nameSuffix << std::endl;                                                                
}

void Orchestrator::onData(const ndn::Interest& interest, const ndn::Data& data) {
  using namespace std::chrono;
  std::lock_guard<std::mutex> lock(mutex_);

  std::string content(reinterpret_cast<const char*>(data.getContent().value()), data.getContent().value_size());
  std::string nameStr = interest.getName().toUri();
  std::string trafficLightName = interest.getName().at(-2).toUri();

  auto it = interestTimestamps_.find(nameStr);
  if (it == interestTimestamps_.end()) {
    std::cerr << "Interest timestamp not found: " << nameStr << std::endl;
    return;
  }

  steady_clock::time_point now = steady_clock::now();
  steady_clock::duration rtt = now - it->second;
  interestTimestamps_.erase(it);

  try {
    auto json = nlohmann::json::parse(content);
    std::string state = json.at("state").get<std::string>();
    int remainingMs = json.at("remaining").get<int>();

    int correctedRemainingMs = remainingMs - duration_cast<milliseconds>(rtt).count() / 2;
    if (correctedRemainingMs < 0)
      correctedRemainingMs = 0;

    auto& tl = trafficLights_[trafficLightName];
    tl.state = state;
    tl.endTime = now + milliseconds(correctedRemainingMs);

    if (json.contains("priority")) {
      tl.priority = json.at("priority").get<int>();
    }

    reviewPriorities();

  } catch (const std::exception& e) {
    std::cerr << "Error parsing Data content: " << e.what() << std::endl;
  }
}

void Orchestrator::reviewPriorities() {
  auto candidates = getHigherPrioritySTL();
  if (candidates.empty()) return;

  if (processIntersections(candidates)) return;

  processGreenWave();
}

std::vector<std::string> Orchestrator::getHigherPrioritySTL() {
  std::vector<std::string> candidates;
  for (const auto& [name, state] : trafficLights_) {
    if (state.priority >= MIN_PRIORITY) {
      candidates.push_back(name);
    }
  }
  return candidates;
}

bool Orchestrator::processIntersections(const std::vector<std::string>& candidates) {
  for (const auto& [interName, inter] : intersections_) {
    std::vector<std::string> open, closed;
    std::string highestPriority;
    int maxPriority = -1;

    for (const std::string& name : inter.trafficLightNames) {
      if (std::find(candidates.begin(), candidates.end(), name) != candidates.end()) {
        const auto& s = trafficLights_[name];

        if (s.state == "green") open.push_back(name);
        else closed.push_back(name);

        if (s.priority > maxPriority) {
          maxPriority = s.priority;
          highestPriority = name;
        }
      }
    }

    if (!open.empty() && !closed.empty()) {
      auto now = std::chrono::steady_clock::now();

      for (const std::string& name : inter.trafficLightNames) {
        auto& s = trafficLights_[name];

        auto dur = s.endTime - now;
        uint64_t remainingTime = (s.endTime > now)
          ? std::chrono::duration_cast<std::chrono::milliseconds>(dur).count()
          : 0;

        if (name == highestPriority) {
          s.command = "increase_time";
        } else {
          s.command = "decrease_time";
        }

        if (remainingTime == 0) {
          s.command = "do_nothing";
        }
      }

      lastModified = highestPriority;
      return true;
    }
  }
  return false;
}

void Orchestrator::processGreenWave() {
  if (lastModified.empty()) return;

  const std::string& baseName = lastModified;
  std::size_t lastSlash = baseName.rfind('/');
  if (lastSlash == std::string::npos) return;

  std::string prefix = baseName.substr(0, lastSlash);
  auto it = waveGroups_.find(prefix);
  if (it == waveGroups_.end()) return;

  const std::vector<std::string>& group = it->second;
  auto& baseTrafficLight = trafficLights_[baseName];
  auto now = std::chrono::steady_clock::now();

  uint64_t baseRemaining = (baseTrafficLight.endTime > now)
    ? std::chrono::duration_cast<std::chrono::milliseconds>(baseTrafficLight.endTime - now).count()
    : 0;

  constexpr int X = 5000;

  int baseIndex = -1;
  for (int i = 0; i < group.size(); ++i) {
    if (group[i] == baseName) {
      baseIndex = i;
      break;
    }
  }

  if (baseIndex == -1) return;

  for (int i = 0; i < group.size(); ++i) {
    const std::string& name = group[i];
    if (name == baseName) continue;

    auto& s = trafficLights_[name];
    int dist = i - baseIndex;
    uint64_t expectedTime = baseRemaining + dist * X;

    uint64_t currentTime = (s.endTime > now)
      ? std::chrono::duration_cast<std::chrono::milliseconds>(s.endTime - now).count()
      : 0;

    if (std::abs(static_cast<int64_t>(expectedTime - currentTime)) > 1000) {
      s.command = "set_time_" + std::to_string(expectedTime);
    } else {
      s.command = "do_nothing";
    }
  }
  lastModified = "";
}

void Orchestrator::onInterest(const ndn::Interest& interest) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (interest.getName().size() >= 1 
      && interest.getName().at(-1).toUri() == "clock") {
    return produceClockData(interest);
  } else if (interest.getName().size() >= 2 
             && interest.getName().get(-2).toUri() == "command") {
    std::string trafficLightName = interest.getName().at(-1).toUri();

    if (!trafficLights_.count(trafficLightName)) {
      std::cerr << "Unknown traffic light for command: " << trafficLightName << std::endl;
      return;
    }
    return produceCommand(trafficLightName, interest);
  } else {
    std::cerr << "Invalid suffix.\n";
    return;
  }
}

void Orchestrator::produceClockData(const ndn::Interest& interest) {
  auto now = std::chrono::system_clock::now();
  auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

  std::string timeStr = std::to_string(nowMs);
  clockTimestampMs_ = nowMs;

  auto data = std::make_shared<ndn::Data>(interest.getName());
  data->setContent(std::string_view(timeStr));  
  data->setFreshnessPeriod(ndn::time::seconds(1));

  m_keyChain.sign(*data);
  m_face.put(*data);
}

void Orchestrator::produceCommand(const std::string& trafficLightName, const ndn::Interest& interest) {
  std::string cmd = trafficLights_[trafficLightName].command;

  auto data = std::make_shared<ndn::Data>(interest.getName());
  data->setContent(std::string_view(cmd));  
  data->setFreshnessPeriod(ndn::time::seconds(1));

  m_keyChain.sign(*data);
  m_face.put(*data);
}

void Orchestrator::onNack(const ndn::Interest& interest, const ndn::lp::Nack& nack) {
  std::cerr << "Nack received: " << interest.getName().toUri() << " Reason: " << nack.getReason() << std::endl;
}

void Orchestrator::onTimeout(const ndn::Interest& interest) {
  std::cerr << "Interest timeout: " << interest.getName().toUri() << std::endl;
}

ndn::Interest Orchestrator::createInterest(ndn::Name& name, bool mustBeFresh, bool canBePrefix, ndn::time::milliseconds lifetime) {
  ndn::Interest interest(name);
  interest.setMustBeFresh(mustBeFresh);
  interest.setCanBePrefix(canBePrefix);
  interest.setInterestLifetime(lifetime);
  return interest;
}

void Orchestrator::sendInterest(const ndn::Interest& interest) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    interestTimestamps_[interest.getName().toUri()] = std::chrono::steady_clock::now();
  }
  m_face.expressInterest(interest,
                        std::bind(&Orchestrator::onData, this, std::placeholders::_1, std::placeholders::_2),
                        std::bind(&Orchestrator::onNack, this, std::placeholders::_1, std::placeholders::_2),
                        std::bind(&Orchestrator::onTimeout, this, std::placeholders::_1));
}

void Orchestrator::runConsumer() {
     m_scheduler.schedule(5000_ms, [this] {
    for (const auto& [name, state] : trafficLights_) {
      Name interestName(name);
      auto interest = createInterest(interestName, true, false, 1000_ms);
      sendInterest(interest);
      std::cout << "[Consumer] Asking Data from " << name << std::endl;
    }
    // Reagendar
    runConsumer();
  });
}

void Orchestrator::onRegisterFailed(const ndn::Name& nome, const std::string& reason) {
  std::cerr << "Failed to register name: " << nome.toUri() << " Reason: " << reason << std::endl;
}
