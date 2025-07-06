#include "../include/Orchestrator.hpp"
#include <numeric> // Necessário para std::accumulate
#include <iostream>
#include <sstream>

Orchestrator::Orchestrator()
  : m_face(m_ioCtx),
    m_validator(m_face),
    m_scheduler(m_ioCtx)
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
                                const std::map<std::string, Intersection>& intersections,
                                const std::vector<GreenWaveGroup>& greenWaves)
{
    trafficLights_ = trafficLights;
    for (auto& [name, state] : trafficLights_) {
      state.command = "";
    }
    intersections_ = intersections;
    greenWaves_ = greenWaves;

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
 
    std::cout << "[SETUP] Ondas Verdes carregadas:" << std::endl;
    for (const auto& wave : greenWaves_) {
        std::cout << " - " << wave.name << " (Tempo de viagem: " << wave.travelTimeMs << "ms): ";
        for (const auto& light : wave.trafficLightNames) {
            std::cout << light << " ";
        }
        std::cout << std::endl;
    }
}

void Orchestrator::run() {
  m_scheduler.schedule(ndn::time::seconds(1), [this]{ runConsumer(); });
  runProducer("command");
  m_cycleThread = std::jthread([this] { this->cycle(); }); 
  m_face.processEvents();
}

void Orchestrator::cycle() {
    const auto cycleInterval = std::chrono::seconds(1);
    const int allRedTimeoutCycles = 3; 

    for (const auto& [interName, intersection] : intersections_) {
        updatePriorityList(interName);
    }

    while (!m_stopFlag) {
        auto cycleStart = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto now = std::chrono::steady_clock::now();

            for (auto& [interName, intersectionRef] : intersections_) {
                bool isIntersectionActive = false;
                for (const auto& lightName : intersectionRef.trafficLightNames) {
                    const auto& state = trafficLights_.at(lightName).state;
                    if (state == "GREEN" || state == "YELLOW") {
                        isIntersectionActive = true;
                        break;
                    }
                }

                if (!isIntersectionActive) {
                    m_allRedCounter[interName]++;
                    if (m_allRedCounter[interName] >= allRedTimeoutCycles) {
                        forceCycleStart(interName);
                        m_allRedCounter[interName] = 0;
                    }
                } else {
                    m_allRedCounter[interName] = 0;
                }

                for (auto& wave : greenWaves_) {
                    if (wave.trafficLightNames.empty()) continue;

                    const std::string& waveLeaderName = wave.trafficLightNames.front();
                    const auto& waveLeaderTL = trafficLights_.at(waveLeaderName);

                    if (waveLeaderTL.state == "GREEN" && !wave.hasBeenTriggered) {
                        processActiveGreenWave(wave);
                        wave.hasBeenTriggered = true;
                    }
                    else if (waveLeaderTL.state != "GREEN") {
                        wave.hasBeenTriggered = false;
                    }
                }

                for (const auto& lightName : intersectionRef.trafficLightNames) {
                    auto& light = trafficLights_.at(lightName);
                    if (light.command.empty()){
                      generateSyncCommand(intersectionRef, lightName);
                    }
                }
                
                if (intersectionRef.needsNormalization) {
                    intersectionRef.needsNormalization = false;
                    std::cout << "[RECOVERY] Fase de normalização concluída para " << interName << ". Retomando ciclo normal." << std::endl;
                }
            }
        }

        auto cycleDuration = std::chrono::steady_clock::now() - cycleStart;
        if (cycleDuration < cycleInterval) {
            std::this_thread::sleep_for(cycleInterval - cycleDuration);
        }
    }
}

void Orchestrator::runProducer(const std::string& suffix){
  ndn::Name nameSuffix = ndn::Name(prefix_).append(suffix);
  m_face.setInterestFilter(nameSuffix,
      [this](const ndn::InterestFilter&, const ndn::Interest& interest) {
        this->onInterest(interest);
      },
      [this](const ndn::Name& name, const std::string& reason) {
        this->onRegisterFailed(name, reason);
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

  std::string trafficLightName = interest.getName().toUri();
  if (trafficLights_.find(trafficLightName) == trafficLights_.end()) {
    return;
  }
  
  auto& tl = trafficLights_.at(trafficLightName);
  if (tl.state == "UNKNOWN") {
    std::cout << "[RECOVERY] Semáforo " << trafficLightName << " voltou a comunicar." << std::endl;
    
    const auto* intersection = findIntersectionFor(trafficLightName);
    if (intersection && intersection->isCompromised) {
        bool allLightsOk = true;
        for (const auto& peerLightName : intersection->trafficLightNames) {
            if (trafficLights_.at(peerLightName).state == "UNKNOWN" && peerLightName != trafficLightName) {
                allLightsOk = false;
                break;
            }
        }
        if (allLightsOk) {
              intersections_.at(intersection->name).isCompromised = false;
              intersections_.at(intersection->name).needsNormalization = true;
              std::cout << "[RECOVERY] Cruzamento " << intersection->name << " operacional. Iniciando fase de normalização." << std::endl;
        }
    }
  }

  std::string content(reinterpret_cast<const char*>(data.getContent().value()), data.getContent().value_size());
  std::string nameStr = interest.getName().toUri();
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

  if (tokens.size() < 3) {
    std::cerr << "Invalid message format: " << content << std::endl;
    return;
  }

  std::string state = tokens[0];
  int remainingMs = std::stoi(tokens[1]);

  int correctedRemainingMs = remainingMs - recordRTT(data.getName().toUri());
  interestTimestamps_.erase(it);
  if (correctedRemainingMs < 0)
    correctedRemainingMs = 0;

  tl.state = state;
  tl.endTime = now + milliseconds(correctedRemainingMs);
  tl.priority = std::stof(tokens[2]);
  tl.timeOutCounter = 0;
}

void Orchestrator::updatePriorityList(const std::string& intersectionName) {
    auto it = intersections_.find(intersectionName);
    if (it == intersections_.end()) return;
    auto& list = sortedPriorityCache_[intersectionName];
    list.clear();

    for (const auto& name : it->second.trafficLightNames) {
        list.emplace_back(name, trafficLights_[name].priority);
    }
    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });
}

void Orchestrator::generateSyncCommand(const Intersection& intersection, const std::string& requesterName) {
    auto now = std::chrono::steady_clock::now();
    auto& requesterTL = trafficLights_.at(requesterName);
    int avgRttOneWay = getAverageRTT() / 2;

    if (intersection.needsNormalization) {
        requesterTL.command += ";set_state:RED;set_current_time:" + std::to_string(config::RECOVERY_RED_TIME_MS);
        requesterTL.endTime = now + std::chrono::milliseconds(config::RECOVERY_RED_TIME_MS);
        return; 
    }
    if (intersection.isCompromised) {
        requesterTL.command += ";set_state:ALERT";
        return; 
    }

    std::string activeLightName = "";
    for (const auto& name : intersection.trafficLightNames) {
        if (trafficLights_.at(name).state == "GREEN" || trafficLights_.at(name).state == "YELLOW") {
            activeLightName = name;
            break;
        }
    }

    if (activeLightName.empty() || requesterName == activeLightName) {
        return;
    }

    const auto& activeTL = trafficLights_.at(activeLightName);
    int activeRemainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(activeTL.endTime - now).count();
    if (activeTL.state == "GREEN") {
        activeRemainingMs += config::YELLOW_TIME_MS;
    }

    int finalCommandTime = activeRemainingMs - avgRttOneWay;
    if (finalCommandTime < 0) finalCommandTime = 0;

    int currentRemainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(requesterTL.endTime - now).count();

    if (std::abs(currentRemainingMs - activeRemainingMs) > 1000) {
        requesterTL.command += ";set_state:RED;set_current_time:" + std::to_string(finalCommandTime);
        requesterTL.endTime = now + std::chrono::milliseconds(activeRemainingMs);
    }
}

void Orchestrator::forceCycleStart(const std::string& intersectionName) {
    const auto& priorityList = sortedPriorityCache_.at(intersectionName);
    if (priorityList.empty()) return;

    const std::string& leaderName = priorityList.front().first;
    auto& leaderTL = trafficLights_.at(leaderName);
    auto now = std::chrono::steady_clock::now();

    int avgRttOneWay = getAverageRTT() / 2;
    int finalCommandTime = config::GREEN_BASE_TIME_MS - avgRttOneWay;
    if (finalCommandTime < 0) finalCommandTime = 0;

    leaderTL.command += ";set_state:GREEN;set_current_time:" + std::to_string(finalCommandTime);
    leaderTL.endTime = now + std::chrono::milliseconds(config::GREEN_BASE_TIME_MS);
    leaderTL.state = "GREEN";

    std::cout << "[WATCHDOG] Cruzamento " << intersectionName << " inativo. Forçando início com " << leaderName << std::endl;
}

void Orchestrator::onInterest(const ndn::Interest& interest) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto& name = interest.getName();
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
    return produce(trafficLightName, interest);
  } else {
    std::cerr << "Invalid suffix." << std::endl;
    return;
  }
}

void Orchestrator::produce(const std::string& trafficLightName, const ndn::Interest& interest) {
  auto data = std::make_shared<ndn::Data>(interest.getName());
  data->setContent(std::string_view(trafficLights_.at(trafficLightName).command)); 
  data->setFreshnessPeriod(ndn::time::seconds(1));
  trafficLights_.at(trafficLightName).command = "";
  m_keyChain.sign(*data);
  m_face.put(*data);
}

void Orchestrator::onNack(const ndn::Interest& interest, const ndn::lp::Nack& nack) {
    std::cerr << "Nack received: " << interest.getName().toUri() 
              << " Reason: " << nack.getReason() << std::endl;

    std::lock_guard<std::mutex> lock(mutex_);
    std::string failedLightName = interest.getName().toUri();

    if (trafficLights_.count(failedLightName)) {
        auto& tl = trafficLights_.at(failedLightName);
        tl.state = "UNKNOWN"; 
        const auto* intersection = findIntersectionFor(failedLightName);
        if (intersection) {
            intersections_.at(intersection->name).isCompromised = true;
            std::cout << "[FAULT] Cruzamento " << intersection->name 
                      << " comprometido devido a Nack em " << failedLightName << std::endl;
        }
    }
}

void Orchestrator::onTimeout(const ndn::Interest& interest) {
    std::cerr << "Interest timeout: " << interest.getName().toUri() << std::endl;
    std::lock_guard<std::mutex> lock(mutex_); 

    std::string failedLightName = interest.getName().toUri();

    if (trafficLights_.count(failedLightName)) {
        auto& tl = trafficLights_.at(failedLightName);
        tl.timeOutCounter++;

        if (tl.timeOutCounter >= 2) {
            tl.state = "UNKNOWN";
            const auto* intersection = findIntersectionFor(failedLightName);
            if (intersection) {
                intersections_.at(intersection->name).isCompromised = true;
                std::cout << "[FAULT] Cruzamento " << intersection->name 
                          << " comprometido devido a Timeouts em " << failedLightName << std::endl;
            }
        }
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
                        std::bind(&Orchestrator::onData, this, _1, _2),
                        std::bind(&Orchestrator::onNack, this, _1, _2),
                        std::bind(&Orchestrator::onTimeout, this, _1));
}

void Orchestrator::runConsumer() {
    m_scheduler.schedule(1000_ms, [this] {
    m_cycleCount++;
    for (const auto& [name, state] : trafficLights_) {
      if (!state.isUnknown()) {
        Name interestName(name);
        auto interest = createInterest(interestName, true, false, 900_ms); 
        sendInterest(interest);
      }
      else {
        if (m_cycleCount % 5 == 0) {
            std::cout << "[RECOVERY_PING] Tentando contactar o nó falho: " << name << std::endl;
            Name interestName(name);
            auto interest = createInterest(name, true, false, 900_ms);
            sendInterest(interest);
        }
      }
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
        return 0; 
    }

    int rttMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second).count();
    
    rttHistory_.push_back(rttMs);
    if (rttHistory_.size() > config::RTT_WINDOW_SIZE) {
        rttHistory_.erase(rttHistory_.begin());
    }
    return rttMs / 2;
}

const Intersection* Orchestrator::findIntersectionFor(const std::string& lightName) const {
    for (const auto& [intersectionName, intersection] : intersections_) {
        if (intersection.contains(lightName)) {
            return &intersection;
        }
    }
    return nullptr;
}

int Orchestrator::getAverageRTT() const {
    if (rttHistory_.empty()) return 0;
    int total = std::accumulate(rttHistory_.begin(), rttHistory_.end(), 0);
    return total / static_cast<int>(rttHistory_.size());
}

void Orchestrator::processActiveGreenWave(const GreenWaveGroup& wave) {
    if (wave.trafficLightNames.empty()) return;

    auto now = std::chrono::steady_clock::now();
    const std::string& waveLeaderName = wave.trafficLightNames.front();
    const auto& waveLeaderTL = trafficLights_.at(waveLeaderName);

    std::cout << "[GREEN_WAVE] Processando '" << wave.name << "' liderada por " << waveLeaderName << "." << std::endl;

    int leaderRemainingTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(waveLeaderTL.endTime - now).count();
    if (leaderRemainingTimeMs < 0) leaderRemainingTimeMs = 0;

    for (size_t i = 0; i < wave.trafficLightNames.size(); ++i) {
        const std::string& memberName = wave.trafficLightNames[i];
        if (memberName == waveLeaderName) continue;
        int offsetMs = static_cast<int>(i) * wave.travelTimeMs;

        auto& memberTL = trafficLights_.at(memberName);
        const auto* intersection = findIntersectionFor(memberName);

        std::cout << "  -> Avaliando membro: " << memberName << std::endl;

        if (intersection) {
            std::cout << "    - Status: Faz parte do cruzamento '" << intersection->name << "'." << std::endl;
            if (memberTL.state == "GREEN") {
                std::cout << "    - Estado Atual: VERDE." << std::endl;
                int memberRemainingTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(memberTL.endTime - now).count();
                int timeDiffMs = leaderRemainingTimeMs - memberRemainingTimeMs;
                
                std::cout << "    - Tempo restante (Líder/Membro): " << leaderRemainingTimeMs << "ms / " << memberRemainingTimeMs << "ms. Diferença: " << timeDiffMs << "ms." << std::endl;

                if (timeDiffMs <= offsetMs) {
                    
                    memberTL.command += ";set_current_time:" + std::to_string(leaderRemainingTimeMs+offsetMs);
                    memberTL.endTime = waveLeaderTL.endTime;
                    std::cout << "    - Ação: Sincronizar tempo com o líder. Comando: " << memberTL.command << std::endl;
                }
                else if (timeDiffMs > offsetMs) {
                    memberTL.command += ";increase_time:" + std::to_string(offsetMs);
                    memberTL.endTime += std::chrono::seconds(5);
                    std::cout << "    - Ação: Aumentar tempo em "<< offsetMs << "ms. " << "Comando: " << memberTL.command << std::endl;
                } else {
                    std::cout << "    - Ação: Nenhuma (tempo já é maior ou igual ao do líder)." << std::endl;
                }
            }
              std::cout << "    - Estado Atual: " << memberTL.state << "." << std::endl;
              double greenDurationFactor = 1.0;
              const std::string& competitorName = (intersection->trafficLightNames[0] == memberName)
                                                ? intersection->trafficLightNames[1]
                                                : intersection->trafficLightNames[0];
              if (memberTL.priority < trafficLights_.at(competitorName).priority) {
                  greenDurationFactor = config::LOW_PRIORITY_WAVE_FACTOR;
              }
              int finalGreenDurationMs = static_cast<int>(leaderRemainingTimeMs * greenDurationFactor);
              
              memberTL.command += ";set_green_duration:" + std::to_string(finalGreenDurationMs+offsetMs);
              std::cout << "    - Ação: Enviar comando 'suave' para pré-configurar o próximo verde. Comando: " << memberTL.command << std::endl;
        }
        else {
            std::cout << "    - Status: Semáforo livre (não está em um cruzamento)." << std::endl;
            
            if (memberTL.state == "GREEN") {
                 int targetRemainingMs = leaderRemainingTimeMs + offsetMs;
                 int currentRemainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(memberTL.endTime - now).count();
                 if (std::abs(currentRemainingMs - targetRemainingMs) > 1000) {
                     memberTL.command = ";set_current_time:" + std::to_string(targetRemainingMs);
                     memberTL.endTime = now + std::chrono::milliseconds(targetRemainingMs);
                 }
            }
            else if (memberTL.state == "RED") {
                int memberRemainingTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(memberTL.endTime - now).count();
                
                if (memberRemainingTimeMs > offsetMs) {
                    memberTL.command = ";decrease_time:5000";
                    memberTL.endTime -= std::chrono::seconds(5);
                    std::cout << "    - Ação: Sincronização suave. Reduzindo tempo de vermelho em " << offsetMs << "ms." << std::endl;
                }
                else {
                    int targetRemainingMs = leaderRemainingTimeMs + offsetMs;
                    int finalCommandTime = targetRemainingMs - (getAverageRTT() / 2);
                    if (finalCommandTime < 0) finalCommandTime = 0;

                    memberTL.command = ";set_state:GREEN;set_current_time:" + std::to_string(finalCommandTime);
                    memberTL.endTime = now + std::chrono::milliseconds(targetRemainingMs);
                    memberTL.state = "GREEN";
                    std::cout << "    - Ação: Fim da sincronização suave. Forçando entrada na onda verde." << std::endl;
                }
            }
        }
    }
}
