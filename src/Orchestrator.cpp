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
      state.command = "";
    }

    intersections_ = intersections;
    std::cout << "[SETUP] Semáforos carregados:" << std::endl;
    for (const auto& [name, _] : trafficLights_) {
      std::cout << " - " << name << std::endl;
    }

    std::cout << "[SETUP] Cruzamentos carregados:" << std::endl;
    for (const auto& [name, intersection] : intersections_) {
        std::cout << " - " << name << ": ";
        for (const auto& light : intersection.trafficLightNames) {
            std::cout << light << " ";
        }
        std::cout << std::endl;
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
  //std::cout << "[Consumer] Received Data from: " << interest.getName().toUri() << std::endl;
  using namespace std::chrono;
  std::lock_guard<std::mutex> lock(mutex_);

  std::string content(reinterpret_cast<const char*>(data.getContent().value()), data.getContent().value_size());
  std::string nameStr = interest.getName().toUri();
  std::string trafficLightName = interest.getName().toUri();

  auto it = interestTimestamps_.find(nameStr);
  if (it == interestTimestamps_.end()) {
    std::cerr << "Interest timestamp not found: " << nameStr << std::endl;
    return;
  }

  steady_clock::time_point now = steady_clock::now();
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

  int correctedRemainingMs = remainingMs - recordRTT(data.getName().toUri());
  interestTimestamps_.erase(it);
  if (correctedRemainingMs < 0)
    correctedRemainingMs = 0;

  auto& tl = trafficLights_[trafficLightName];
  tl.state = state;
  tl.endTime = now + milliseconds(correctedRemainingMs);
  tl.priority = std::stof(tokens[2]);
  tl.timeOutCounter=0;
  }

std::string Orchestrator::delegateCommandTo(const std::string& name) {
  auto it = trafficLights_.find(name);
  if (it == trafficLights_.end()) return "";

  auto& s = it->second;
  s.command = "";

  for (const auto& [interName, inter] : intersections_) {
    if (inter.contains(name)) {
      s.command += handleIntersectionLogic(interName, s.name);
      s.command += synchronize(interName, s.name);
      return s.command;
    }
  }

  if (s.priority < MIN_PRIORITY && !s.isUnknown() && !s.isAlert()) {
    s.command = "set_time:DEFAULT";
    //std::cout << "default" << std::endl;
  } else if (s.isAlert()) {
    s.command = "set_state:GREEN;set_time:DEFAULT";
    //std::cout << "saindo do alerta" << std::endl;
  } else if (s.isUnknown()) {
    s.command = "set_state:ALERT";
    //std::cout << "colocando em alerta" << std::endl;
  } else if (s.priority < this->getAveragePrioritySTL()) {
    //std::cout << "Prioridade: " << s.priority << " Média: " << this->getAveragePrioritySTL() << std::endl;
  } else if (s.state == "GREEN") {
    s.command = "increase_time:5000";
    //std::cout << "aumento verde em 5s" << std::endl;
  } else if (s.state == "RED") {
    s.command = "decrease_time:3000"; 
    //std::cout << "diminui vermelho em 3s" << std::endl;
  }
  return s.command;

}


float Orchestrator::getAveragePrioritySTL() {
  std::vector<std::string> candidates;
  float averageSum = 0;
  for (const auto& [name, state] : trafficLights_) {
    candidates.push_back(name);
    averageSum+=state.priority;
  }
  return averageSum/candidates.size();
}

std::string Orchestrator::handleIntersectionLogic(const std::string& intersectionName, const std::string& trafficLightName) {
  updatePriorityList(intersectionName);

  auto& sortedByPriority = sortedPriorityCache_[intersectionName];
  if (sortedByPriority.empty())
    return "";


  size_t NSP = sortedByPriority.size();

  // Encontra a posição do semáforo na lista
  auto it = std::find_if(sortedByPriority.begin(), sortedByPriority.end(),
                         [&](const auto& pair) { return pair.first == trafficLightName; });

  if (it == sortedByPriority.end())
    return "";

  size_t i = std::distance(sortedByPriority.begin(), it);

  int green, red;

  if (i == 0) {
    green = TVD_BASE + TVD_BONUS;
    red = (TVD_BASE + TA) * (NSP - 1);
  } else {
    red = TVD_BASE + TVD_BONUS + TA + (i - 1) * (TVD_BASE + TA);
    green = TVD_BASE;
  }


  return ";set_GREEN_time:" + std::to_string(green) +
         ";set_RED_time:" + std::to_string(red);
}

void Orchestrator::updatePriorityList(const std::string& intersectionName) {
  auto it = intersections_.find(intersectionName);
  if (it == intersections_.end()) return;

  auto& list = sortedPriorityCache_[intersectionName];

  // Se lista estiver travada
  if (priorityLocked_[intersectionName]) {
    if (list.empty()) {
      std::cout << "[PRIORITY] Lista está vazia e travada para " << intersectionName << ". Nada a fazer." << std::endl;
      return;
    }

    const auto& lastName = lastToGreen_[intersectionName];
    if (trafficLights_.count(lastName) && trafficLights_.at(lastName).state == "GREEN") {
      // Último da lista ficou verde → destrava e apaga
      priorityLocked_[intersectionName] = false;
      list.clear();
      lastToGreen_.erase(intersectionName);
      std::cout << "[PRIORITY] Lista destravada e APAGADA para " << intersectionName << ". Será recriada na próxima chamada." << std::endl;
      return;
    }

    // Evita front em lista vazia
    if (!list.empty()) {
      const auto& frontName = list.front().first;
      if (trafficLights_.count(frontName) && trafficLights_.at(frontName).state == "RED") {
        std::cout << "[PRIORITY] Semáforo " << frontName << " ficou vermelho. Rotacionando a lista em " << intersectionName << "." << std::endl;
        std::rotate(list.begin(), list.begin() + 1, list.end());
        std::cout << "[PRIORITY] Nova ordem: " << list.front().first << " é o primeiro. Aguardando " << lastName << " ficar verde." << std::endl;
      }
    }

    return; // lista ainda travada, não reordena
  }

  list.clear();
  for (const auto& name : it->second.trafficLightNames) {
    list.emplace_back(name, trafficLights_[name].priority);
  }

  std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
    return a.second > b.second;
  });

  if (!list.empty()) {
    lastToGreen_[intersectionName] = list.back().first; // guarda último da nova lista
  }

  priorityLocked_[intersectionName] = true;
  std::cout << "[PRIORITY] Lista reordenada e TRAVADA para " << intersectionName << std::endl;
}


std::string Orchestrator::synchronize(const std::string& intersectionName, const std::string& requester) {
  auto it = intersections_.find(intersectionName);
  if (it == intersections_.end()) return "";

  auto now = std::chrono::steady_clock::now();
  auto& requesterTL = trafficLights_[requester];
  auto& sorted = sortedPriorityCache_[intersectionName];
  if (sorted.empty()) {
    std::cerr << "[BUG] Lista de prioridade vazia para " << intersectionName << " no synchronize()" << std::endl;
    return "";
  }

  std::string highestPriority = sorted.front().first;

  const int RTT = getAverageRTT();

  // Verifica conflitos (mais de um semáforo ativo)
  int active = 0;
  for (const auto& [name, _] : sorted) {
    const auto& tl = trafficLights_[name];
    if (tl.state == "GREEN" || tl.state == "YELLOW")
      active++;
  }


  if (active > 1 && requester != highestPriority && 
      (requesterTL.state == "GREEN" || requesterTL.state == "YELLOW")) {

    const auto& top = trafficLights_[highestPriority];
    int remaining = std::chrono::duration_cast<std::chrono::milliseconds>(top.endTime - now).count();
    if (top.state == "GREEN")
      remaining += TA;

    requesterTL.endTime = now + std::chrono::milliseconds(remaining);
    return ";set_state:RED;set_current_time:" + std::to_string(remaining);
  }

  auto itReq = std::find_if(sorted.begin(), sorted.end(),
                            [&](const auto& p) { return p.first == requester; });

  if (itReq == sorted.end())
    return "";

  if (requesterTL.state != "RED")
    return "";

  size_t index = std::distance(sorted.begin(), itReq);
  std::cout << "index: " << index << std::endl;

  if (index == 0){
    const auto& previous = trafficLights_[sorted[sorted.size() - 1].first];
    auto baseTime = previous.endTime;
    int remainingInPrevious = std::chrono::duration_cast<std::chrono::milliseconds>(previous.endTime - now).count();
    int offsetMs = (TVD_BASE + TA) * (sorted.size() - 1); 
    auto newEndTime = now + std::chrono::milliseconds(remainingInPrevious + offsetMs);
    int newRemaining = std::chrono::duration_cast<std::chrono::milliseconds>(newEndTime - now).count();
    int requesterRemaining = std::chrono::duration_cast<std::chrono::milliseconds>(requesterTL.endTime - now).count();
    
    if (std::abs(newRemaining - requesterRemaining) > 1000) {
      requesterTL.endTime = newEndTime;
      std::cout << "A: Ajustado para " << newRemaining / 1000 << " segundos" << std::endl;
      return ";set_current_time:" + std::to_string(newRemaining);
    }
  }
  else if (index == 1) {
    const auto& first = trafficLights_[sorted[index - 1].first];
    auto targetTime = first.endTime + std::chrono::milliseconds(TA+RTT);
    int newRemaining = std::chrono::duration_cast<std::chrono::milliseconds>(targetTime - now).count();
    int requesterRemaining = std::chrono::duration_cast<std::chrono::milliseconds>(requesterTL.endTime - now).count();
    
    if (std::abs(newRemaining - requesterRemaining) > 1000) {
      requesterTL.endTime = targetTime;
      std::cout << "B: " << newRemaining/1000 << std::endl;
      return ";if_state:RED;set_current_time:" + std::to_string(newRemaining);
    }
  }
  else if (index >= 2) {
    const auto& previous = trafficLights_[sorted[index - 1].first];
    auto now = std::chrono::steady_clock::now();

    int remainingInPrevious = std::chrono::duration_cast<std::chrono::milliseconds>(previous.endTime - now).count();

    int offsetMs = (TVD_BASE + TA) * (index-1);  // exemplo 2 ciclos de verde+amarelo

    auto newEndTime = now + std::chrono::milliseconds(remainingInPrevious + offsetMs);

    int newRemaining = std::chrono::duration_cast<std::chrono::milliseconds>(newEndTime - now).count();
    int requesterRemaining = std::chrono::duration_cast<std::chrono::milliseconds>(requesterTL.endTime - now).count();

    std::cout << "C: Tempo restante anterior B (ms): " << remainingInPrevious << std::endl;
    std::cout << "C: Offset extra (ms): " << offsetMs << std::endl;
    std::cout << "C: Novo tempo restante C (ms): " << newRemaining << std::endl;
    newRemaining = (newRemaining*999)/1000;
    std::cout << "C: Novo tempo restante arredondado (ms): " << newRemaining << std::endl;

    if (std::abs(newRemaining - requesterRemaining) > 1000) {
      requesterTL.endTime = newEndTime;
      std::cout << "C: Ajustado para " << newRemaining / 1000 << " segundos" << std::endl;
      return ";set_current_time:" + std::to_string(newRemaining);
    }
  }

  return "";
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
      s.command = "";
    }
  }

  lastModified = "";
  return true;
}


void Orchestrator::onInterest(const ndn::Interest& interest) {
  std::lock_guard<std::mutex> lock(mutex_);

  const auto& name = interest.getName();
  //std::cout << name.toUri() << std::endl;

  bool isSync = false;
  bool isCommand = false;
  std::string trafficLightName;

  for (size_t i = 0; i < name.size(); ++i) {
    const auto& component = name.get(i).toUri();
    if (component == "command" && i + 1 < name.size()) {
      isCommand = true;
      std::ostringstream oss;
      for (size_t j = i + 1; j < name.size(); ++j) {
        oss << "/" << name.get(j).toUri();
      }
      trafficLightName = oss.str();
      break;
    }
  }
if (isCommand) {
    if (!trafficLights_.count(trafficLightName)) {
      std::cerr << "Unknown traffic light for command: " << trafficLightName << std::endl;
      return;
    }
    return produceCommand(trafficLightName, interest);
  } else {
    std::cerr << "Invalid suffix." << std::endl;
    return;
  }
}

void Orchestrator::produceCommand(const std::string& trafficLightName, const ndn::Interest& interest) {
  std::string cmd = delegateCommandTo(trafficLightName);

  auto data = std::make_shared<ndn::Data>(interest.getName());
  data->setContent(std::string_view(cmd));  
  data->setFreshnessPeriod(ndn::time::seconds(1));

  m_keyChain.sign(*data);
  m_face.put(*data);
  trafficLights_[trafficLightName].command = "";
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
     m_scheduler.schedule(1000_ms, [this] {
    for (const auto& [name, state] : trafficLights_) {
      if (state.isUnknown()){
        continue;
      }
      Name interestName(name);
      auto interest = createInterest(interestName, true, false, 1000_ms);
      sendInterest(interest);
      //std::cout << "[Consumer] Asking Data from " << name << std::endl;
    }
    runConsumer();
  });
}

void Orchestrator::onRegisterFailed(const ndn::Name& nome, const std::string& reason) {
  std::cerr << "Failed to register name: " << nome.toUri() << " Reason: " << reason << std::endl;
}

int Orchestrator::recordRTT(const std::string& interestName) {
    auto now = std::chrono::steady_clock::now();

    auto it = interestTimestamps_.find(interestName);
    if (it == interestTimestamps_.end()) {
        std::cerr << "[WARN] Interest não encontrado para calcular RTT: " << interestName << std::endl;
        return -1; 
    }

    int rttMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second).count() / 2;

    // Armazena no histórico
    rttHistory_.push_back(rttMs);
    if (rttHistory_.size() > RTT_WINDOW_SIZE) {
        rttHistory_.erase(rttHistory_.begin());
    }

    std::cout << "[INFO] RTT registrado: " << rttMs << " ms\n";
    return rttMs;
}


int Orchestrator::getAverageRTT() const {
    if (rttHistory_.empty()) return 0;
    int total = std::accumulate(rttHistory_.begin(), rttHistory_.end(), 0);
    return total / static_cast<int>(rttHistory_.size());
}