#include "../include/Orchestrator.hpp"

Orchestrator::Orchestrator() 
   : m_validator(m_face)
{
    m_validator.load("../config/trust-schema.conf");
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
    std::cout << "[DEBUG] Semáforos carregados:" << std::endl;
    for (const auto& [name, _] : trafficLights_) {
      std::cout << " - " << name << std::endl;
    }

    //waveGroups_.clear();

    //for (const auto& [name, state] : trafficLights_) {
    //    std::size_t lastSlash = name.rfind('/');
    //    if (lastSlash == std::string::npos) continue;
    //
    //    std::string prefix = name.substr(0, lastSlash);
    //    waveGroups_[prefix].push_back(name);
    //}

    //for (auto& [prefix, group] : waveGroups_) {
    //    std::sort(group.begin(), group.end(), [](const std::string& a, const std::string& b) {
    //        int idA = std::stoi(a.substr(a.rfind('/') + 1));
    //        int idB = std::stoi(b.substr(b.rfind('/') + 1));
    //        return idA < idB;
    //    });
    //}

    //std::cout << "[loadTopology] Green waves detected:\n";
    //for (const auto& [prefix, group] : waveGroups_) {
    //    std::cout << " - " << prefix << ": ";
    //    for (const auto& name : group) std::cout << name << " ";
    //    std::cout << std::endl;
    //}
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

  
  auto delimiter = '|';
  std::istringstream iss(content);
  std::string token;

  std::vector<std::string> tokens;
  while (std::getline(iss, token, delimiter)) {
    tokens.push_back(token);
  }

  if (tokens.size() < 2) {
    std::cerr << "Invalid message format: " << content << std::endl;
    return;
  }

  std::string state = tokens[0];
  int remainingMs = std::stoi(tokens[1]);

  int correctedRemainingMs = remainingMs - duration_cast<milliseconds>(rtt).count() / 2;
  if (correctedRemainingMs < 0)
    correctedRemainingMs = 0;

  auto& tl = trafficLights_[trafficLightName];
  tl.state = state;
  tl.endTime = now + milliseconds(correctedRemainingMs);

  // Prioridade é opcional
  if (tokens.size() >= 3) {
    tl.priority = std::stoi(tokens[2]);
  }
  tl.timeOutCounter=0;

  delegateCommandTo(trafficLightName);
  }

void Orchestrator::delegateCommandTo(const std::string& name) {
  auto it = trafficLights_.find(name);
  if (it == trafficLights_.end()) return;

  auto& s = it->second;

  if (s.priority < MIN_PRIORITY && !s.isUnknown() && !s.isAlert()) {
    s.command = "set_time:DEFAULT";
    return;
  }
  if (s.priority < this->getAveragePrioritySTL()) {
    return; 
  }

 
  for (const auto& [interName, inter] : intersections_) {
    if (inter.contains(name)) {
      handleIntersectionLogic(interName);
      return;
    }
  }

  if (s.state == "green") {
    s.command = "increase_time:5000";
  } else if (s.state == "red") {
    s.command = "decrease_time:3000"; 
  } else if (s.isAlert()) {
    s.command = "set_state:green;set_time:21000";
  } else if (s.isUnknown()) {
    s.command = "set_state:ALERT";
  } else {
    s.command = "do_nothing";
  }
}


int Orchestrator::getAveragePrioritySTL() {
  std::vector<std::string> candidates;
  int averageSum;
  for (const auto& [name, state] : trafficLights_) {
    candidates.push_back(name);
    averageSum+=state.priority;
  }
  return averageSum/candidates.size();
}

void Orchestrator::handleIntersectionLogic(const std::string& intersectionName) {
  updatePriorityList(intersectionName);

  auto& sortedByPriority = sortedPriorityCache_[intersectionName];
  if (sortedByPriority.empty())
    return;

  const int TVD_BASE = 15000;
  const int TVD_BONUS = 5000;
  const int TA = 3000;

  size_t NSP = sortedByPriority.size();

  for (size_t i = 0; i < NSP; ++i) {
    const auto& [name, _] = sortedByPriority[i];
    auto& s = trafficLights_[name];

    if (i == 0) {
      int green = TVD_BASE + TVD_BONUS;
      int red = (TVD_BASE + TA) * (NSP - 1);

      s.command = "set_green_time:" + std::to_string(green) +
                  ";set_red_time:" + std::to_string(red);
    } else {
      int red = TVD_BASE + TVD_BONUS + TA + (i - 1) * (TVD_BASE + TA);
      int green = TVD_BASE;
      s.command = "set_red_time:" + std::to_string(red) +
                  ";set_green_time:" + std::to_string(green);
    }
  }
}


void Orchestrator::updatePriorityList(const std::string& intersectionName, const std::optional<std::string>& updatedLight) {
  auto it = intersections_.find(intersectionName);
  if (it == intersections_.end()) return;

  auto& list = sortedPriorityCache_[intersectionName];

  // Caso especial: não há cache ou semáforo foi removido
  if (!updatedLight || list.empty() || std::find(it->second.trafficLightNames.begin(), it->second.trafficLightNames.end(), *updatedLight) == it->second.trafficLightNames.end()) {
    list.clear();
    for (const auto& name : it->second.trafficLightNames) {
      list.emplace_back(name, trafficLights_[name].priority);
    }

    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
      return a.second > b.second;
    });
    return;
  }

  // Atualiza posição do semáforo modificado
  const std::string& name = *updatedLight;
  int newPriority = trafficLights_[name].priority;

  auto comp = [](const auto& a, const auto& b) { return a.second > b.second; };

  // Remove o atual
  list.erase(std::remove_if(list.begin(), list.end(),
                            [&](const auto& p) { return p.first == name; }),
             list.end());

  // Inserção ordenada com busca binária
  auto pos = std::lower_bound(list.begin(), list.end(), std::make_pair("", newPriority),
                              [&](const auto& a, const auto& b) {
                                return a.second > b.second;
                              });
  list.insert(pos, {name, newPriority});
}




bool Orchestrator::processGreenWave() {
  if (lastModified.empty()) return false;

  const std::string& baseName = lastModified;
  std::size_t lastSlash = baseName.rfind('/');
  if (lastSlash == std::string::npos) return false;

  std::string prefix = baseName.substr(0, lastSlash);
  auto it = waveGroups_.find(prefix);
  if (it == waveGroups_.end()) return false;

  const std::vector<std::string>& group = it->second;

  for (const auto& name : group) {
    if (trafficLights_[name].isUnknown()) {
      return false;
    }
  }

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

  if (baseIndex == -1) return false;

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
      s.command = "set_time:" + std::to_string(expectedTime);
    } else {
      s.command = "do_nothing";
    }
  }

  lastModified = "";
  return true;
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
  trafficLights_[trafficLightName].command = "do_nothing";
}

void Orchestrator::onNack(const ndn::Interest& interest, const ndn::lp::Nack& nack) {
  std::cerr << "Nack received: " << interest.getName().toUri() << " Reason: " << nack.getReason() << std::endl;
  std::string trafficLightName = interest.getName().toUri();
  auto& tl = trafficLights_[trafficLightName];
  tl.command="set_state:UNKNOWN";
}

void Orchestrator::onTimeout(const ndn::Interest& interest) {
  std::cerr << "Interest timeout: " << interest.getName().toUri() << std::endl;
  std::string trafficLightName = interest.getName().toUri();
  auto& tl = trafficLights_[trafficLightName];
  tl.timeOutCounter++;
  if (tl.timeOutCounter>=2){
    tl.command="set_state:UNKNOWN";
  } 
}

ndn::Interest Orchestrator::createInterest(const ndn::Name& name, bool mustBeFresh, bool canBePrefix, ndn::time::milliseconds lifetime) {
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
      if (state.isUnknown()){
        continue;
      }
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
