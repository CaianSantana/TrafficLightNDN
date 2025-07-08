#include "../include/Orchestrator.hpp"
#include <numeric> // Necessário para std::accumulate
#include <iostream>
#include <sstream>

Orchestrator::Orchestrator()
  : m_face(m_ioCtx),
    m_validator(m_face),
    m_scheduler(m_ioCtx),
    m_metricsFilename("metrics/rtt.csv") 
{
  m_validator.load("config/trust-schema.conf");
}

Orchestrator::~Orchestrator() {
  m_face.shutdown();
}

void Orchestrator::setup(const std::string& prefix) {
  prefix_ = std::move(prefix);
}

void Orchestrator::loadConfig(const std::map<std::string, TrafficLightState>& trafficLights,
                                const std::map<std::string, Intersection>& intersections,
                                const std::vector<GreenWaveGroup>& greenWaves,
                                const std::vector<SyncGroup>& syncGroups,
                                LogLevel level)
{
  this->m_logLevel = level;  
  trafficLights_ = trafficLights;
  for (auto& [name, state] : trafficLights_) {
    state.command = "";
  }
  intersections_ = intersections;
  greenWaves_ = greenWaves;
  syncGroups_ = syncGroups;

  for (const auto& [intersectionName, intersectionData] : intersections_) {
      for (const std::string& lightName : intersectionData.trafficLightNames) {
          auto it = trafficLights_.find(lightName);
          if (it != trafficLights_.end()) {
              it->second.partOfIntersection = true;
          }
      }
  }

  for (const auto& wave : greenWaves_) {
      for (const std::string& lightName : wave.trafficLightNames) {
          auto it = trafficLights_.find(lightName);
          if (it != trafficLights_.end()) {
              it->second.partOfGreenWave = true;
          }
      }
  }

  for (const auto& group : syncGroups_) {
      for (const std::string& lightName : group.trafficLightNames) {
          auto it = trafficLights_.find(lightName);
          if (it != trafficLights_.end()) {
              it->second.partOfSyncGroup = true;
          }
      }
  }

  std::stringstream ss;
  ss << "Configuração carregada. " << trafficLights_.size() << " semáforos, "
     << intersections_.size() << " cruzamentos, " << greenWaves_.size() << " ondas verdes e "
     << syncGroups_.size() << " grupos de sincronia.";
  log(LogLevel::INFO, ss.str());

  for (const auto& pair : trafficLights_) {
      log(LogLevel::DEBUG, " - Semáforo: " + pair.first + 
          " (Cruzamento: " + (pair.second.partOfIntersection ? "S" : "N") +
          ", Onda Verde: " + (pair.second.partOfGreenWave ? "S" : "N") +
          ", Grupo Sync: " + (pair.second.partOfSyncGroup ? "S" : "N") + ")");
  }
}

void Orchestrator::run() {
  // Garante que o arquivo de métricas tenha um cabeçalho no início da execução
  std::ofstream outFile(m_metricsFilename, std::ios_base::trunc); // trunc apaga o conteúdo anterior
  if (outFile.is_open()) {
    outFile << "rtt_ms\n"; // Escreve o cabeçalho do CSV
    outFile.close();
    log(LogLevel::INFO, "Arquivo de métricas '" + m_metricsFilename + "' inicializado.");
  } else {
    log(LogLevel::ERROR, "Não foi possível inicializar o arquivo de métricas: " + m_metricsFilename);
  }

  m_scheduler.schedule(ndn::time::seconds(1), [this]{ runConsumer(); });
  runProducer("command");
  m_cycleThread = std::jthread([this] { this->cycle(); }); 
  m_face.processEvents();
}

void Orchestrator::appendToMetricsFile(int rtt_ms) {
    std::ofstream outFile(m_metricsFilename, std::ios_base::app);
    if (!outFile.is_open()) {
        log(LogLevel::ERROR, "Falha ao abrir o arquivo de métricas para escrita: " + m_metricsFilename);
        return;
    }
    outFile << rtt_ms << "\n";
    outFile.close();
}

void Orchestrator::log(LogLevel level, const std::string& message) {
    if (level <= m_logLevel) {
        std::string levelStr;
        switch (level) {
            case LogLevel::ERROR: levelStr = "[ERROR]"; break;
            case LogLevel::INFO:  levelStr = "[INFO] "; break;
            case LogLevel::DEBUG: levelStr = "[DEBUG]"; break;
            default: return;
        }
        
        auto& stream = (level == LogLevel::ERROR) ? std::cerr : std::cout;
        stream << levelStr << " [" << this->prefix_ << "] " << message << std::endl;
    }
}

void Orchestrator::cycle() {
    const auto cycleInterval = std::chrono::seconds(1);
    const int allRedTimeoutCycles = 5; 

    while (!m_stopFlag) {
        auto cycleStart = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto now = std::chrono::steady_clock::now();

            if (syncGroups_.size()>0) processSyncGroups();
            assignPriorityCommands();
            if (intersections_.size()>0) processIntersections(allRedTimeoutCycles);
            if (greenWaves_.size()>0) processGreenWaves();
        }
        std::this_thread::sleep_for(cycleInterval);
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
  log(LogLevel::INFO, "Registrando produtor para o prefixo: " + nameSuffix.toUri());                                                            
}


void Orchestrator::onInterest(const ndn::Interest& interest) {
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
      log(LogLevel::ERROR, "Comando recebido para semáforo desconhecido: " + trafficLightName);
      return;
    }
    return produce(trafficLightName, interest);
  } else {
    log(LogLevel::ERROR, "Interest com sufixo inválido recebido: " + name.toUri());
    return;
  }
}

void Orchestrator::produce(const std::string& trafficLightName, const ndn::Interest& interest) {
  log(LogLevel::INFO, "Processando comando para " + trafficLightName);
  std::string command;
  auto data = std::make_shared<ndn::Data>(interest.getName());
    {
        std::lock_guard<std::mutex> guard(mutex_);
        command = trafficLights_.at(trafficLightName).command;
        trafficLights_.at(trafficLightName).command = "";
    }
  data->setContent(std::string_view(command)); 
  data->setFreshnessPeriod(ndn::time::seconds(1));

  m_keyChain.sign(*data);
  m_face.put(*data);
}



void Orchestrator::onRegisterFailed(const ndn::Name& nome, const std::string& reason) {
  log(LogLevel::ERROR, "Falha ao registrar prefixo: " + nome.toUri() + " Motivo: " + reason);
}



void Orchestrator::runConsumer() {
    m_scheduler.schedule(1000_ms, [this] {
    m_cycleCount++;
    for (const auto& [name, state] : trafficLights_) {
      if (!state.isUnknown()) {
        Name interestName(name);
        auto interest = createInterest(interestName, true, false, 4000_ms); 
        sendInterest(interest);
      }
      else {
        if (m_cycleCount % 5 == 0) {
            log(LogLevel::INFO, "Tentando contactar o nó falho: " + name);
            Name interestName(name);
            auto interest = createInterest(name, true, false, 4000_ms);
            sendInterest(interest);
        }
      }
    }
    runConsumer();
  });
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

void Orchestrator::onData(const ndn::Interest& interest, const ndn::Data& data) {
  using namespace std::chrono;
  std::lock_guard<std::mutex> lock(mutex_);

  std::string trafficLightName = interest.getName().toUri();
  if (trafficLights_.find(trafficLightName) == trafficLights_.end()) {
    return;
  }
  
  auto& tl = trafficLights_.at(trafficLightName);
  if (tl.state == "UNKNOWN") {
    log(LogLevel::INFO, "Semáforo " + trafficLightName + " voltou a comunicar.");
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
              log(LogLevel::INFO, "Cruzamento " + intersection->name + " operacional. Iniciando fase de normalização.");
        }
    }
  }

  std::string content(reinterpret_cast<const char*>(data.getContent().value()), data.getContent().value_size());
  std::string nameStr = interest.getName().toUri();
  log(LogLevel::DEBUG, "Recebeu Data de: " + data.getName().toUri());
  auto it = interestTimestamps_.find(nameStr);
  if (it == interestTimestamps_.end()) {
    log(LogLevel::ERROR, "Interest timestamp not found:  " + nameStr);
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
    log(LogLevel::ERROR, "Invalid message format:  " + content);
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

void Orchestrator::onNack(const ndn::Interest& interest, const ndn::lp::Nack& nack) {
    std::stringstream ss;
    ss << "Nack recebido para " << interest.getName().toUri() << ". Motivo: " << nack.getReason();
    log(LogLevel::ERROR, ss.str());

    std::lock_guard<std::mutex> lock(mutex_);
    std::string failedLightName = interest.getName().toUri();

    if (trafficLights_.count(failedLightName)) {
        auto& tl = trafficLights_.at(failedLightName);
        tl.state = "UNKNOWN"; 
        const auto* intersection = findIntersectionFor(failedLightName);
        if (intersection) {
            intersections_.at(intersection->name).isCompromised = true;
            log(LogLevel::ERROR, "Cruzamento " + intersection->name 
                      + " comprometido devido a Nack em " + failedLightName);
        }
    }
}


void Orchestrator::onTimeout(const ndn::Interest& interest) {
    log(LogLevel::ERROR, "Timeout no Interest para: " + interest.getName().toUri());
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
                log(LogLevel::ERROR, "Cruzamento " + intersection->name 
                          + " comprometido devido a Timeouts em " + failedLightName);
            }
        }
    }
}


int Orchestrator::recordRTT(const std::string& interestName) {
    auto now = std::chrono::steady_clock::now();
    auto it = interestTimestamps_.find(interestName);
    if (it == interestTimestamps_.end()) {
        log(LogLevel::ERROR, "Timestamp do Interest não encontrado para calcular RTT: " + interestName);
        return 0; 
    }

    int rttMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second).count();
    
    appendToMetricsFile(rttMs);

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

void Orchestrator::processIntersections(const int& allRedTimeoutCycles) {
    for (auto& [interName, intersectionRef] : intersections_) {
        updatePriorityList(interName);
        bool isIntersectionActive = false;

        for (const auto& lightName : intersectionRef.trafficLightNames) {
            auto& light = trafficLights_.at(lightName);
            const auto& state = light.state;

            if (light.command.empty()){
              generateIntersectionCommand(intersectionRef, lightName);
            }

            if (state == "GREEN" || state == "YELLOW") {
                isIntersectionActive = true;
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
        
        if (intersectionRef.needsNormalization) {
            intersectionRef.needsNormalization = false;
            log(LogLevel::DEBUG, "Fase de normalização concluída para " + interName + ". Retomando ciclo normal.");
        }
    }
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

float Orchestrator::calculateAveragePriority() const {
    if (trafficLights_.empty()) {
        return 15.0f; 
    }

    double sumOfPriorities = 0.0;
    for (const auto& pair : trafficLights_) {
        sumOfPriorities += pair.second.priority;
    }

    return static_cast<float>(sumOfPriorities / trafficLights_.size());
}


void Orchestrator::generateIntersectionCommand(const Intersection& intersection, const std::string& requesterName) {
    auto now = std::chrono::steady_clock::now();
    auto& requesterTL = trafficLights_.at(requesterName);
    int avgRttOneWay = getAverageRTT() / 2;

    if (intersection.needsNormalization) {
        requesterTL.command += ";set_state:RED;set_current_time:" + std::to_string(config::RECOVERY_RED_TIME_MS);
        requesterTL.endTime = now + std::chrono::milliseconds(config::RECOVERY_RED_TIME_MS);
        log(LogLevel::DEBUG, "Comando de normalização para " + requesterName + ": " + requesterTL.command);
        return; 
    }
    if (intersection.isCompromised) {
        requesterTL.command += ";set_state:ALERT";
        log(LogLevel::DEBUG, "Comando de alerta (cruzamento comprometido) para " + requesterName + ": " + requesterTL.command);
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
    if (finalCommandTime < 0) return;

    int currentRemainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(requesterTL.endTime - now).count();

    if (std::abs(currentRemainingMs - activeRemainingMs) > 2000) {
        requesterTL.command += ";set_state:RED;set_current_time:" + std::to_string(finalCommandTime);
        requesterTL.endTime = activeTL.endTime;
        log(LogLevel::DEBUG, "Comando de sincronia para " + requesterName + ": " + requesterTL.command);
        return;
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
    if (finalCommandTime < 0) return;

    leaderTL.command += ";set_state:GREEN;set_current_time:" + std::to_string(finalCommandTime);
    leaderTL.endTime = now + std::chrono::milliseconds(config::GREEN_BASE_TIME_MS);
    leaderTL.state = "GREEN";

    log(LogLevel::INFO, "Cruzamento " + intersectionName + " inativo. Forçando início com " + leaderName);
    log(LogLevel::DEBUG, "Comando gerado para " + leaderName + ": " + leaderTL.command);
}

void Orchestrator::processGreenWaves() {
    for (auto& wave : greenWaves_) {
        if (wave.trafficLightNames.empty()) {
            continue;
        }

        const std::string& waveLeaderName = wave.trafficLightNames.front();
        const auto& waveLeaderTL = trafficLights_.at(waveLeaderName);

        if (waveLeaderTL.state == "GREEN" && !wave.hasBeenTriggered) {
            wave.hasBeenTriggered = true; 

            auto now = std::chrono::steady_clock::now();
            log(LogLevel::INFO, "Processando '" + wave.name + "'.");

            int leaderRemainingTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(waveLeaderTL.endTime - now).count();
            if (leaderRemainingTimeMs < 0) leaderRemainingTimeMs = 0;

            for (size_t i = 0; i < wave.trafficLightNames.size(); ++i) {
                const std::string& memberName = wave.trafficLightNames[i];
                if (memberName == waveLeaderName) continue;
                int offsetMs = static_cast<int>(i) * wave.travelTimeMs;

                auto& memberTL = trafficLights_.at(memberName);
                const auto* intersection = findIntersectionFor(memberName);

                if (intersection) {
                    if (memberTL.state == "GREEN") {
                        int memberRemainingTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(memberTL.endTime - now).count();
                        int timeDiffMs = leaderRemainingTimeMs - memberRemainingTimeMs;

                        if (timeDiffMs <= offsetMs) {
                            memberTL.command += ";set_current_time:" + std::to_string(leaderRemainingTimeMs + offsetMs);
                            memberTL.endTime = waveLeaderTL.endTime;
                            log(LogLevel::DEBUG, "Comando gerado para " + memberTL.name + ": " + memberTL.command);
                        }
                        else if (timeDiffMs > offsetMs) {
                            memberTL.command += ";increase_time:" + std::to_string(offsetMs);
                            memberTL.endTime += std::chrono::seconds(5);
                            log(LogLevel::DEBUG, "Comando gerado para " + memberTL.name + ": " + memberTL.command);
                        }
                    }
                    double greenDurationFactor = 1.0;
                    const std::string& competitorName = (intersection->trafficLightNames[0] == memberName)
                                                      ? intersection->trafficLightNames[1]
                                                      : intersection->trafficLightNames[0];
                    if (memberTL.priority < trafficLights_.at(competitorName).priority) {
                        greenDurationFactor = config::LOW_PRIORITY_WAVE_FACTOR;
                    }
                    int finalGreenDurationMs = static_cast<int>(leaderRemainingTimeMs * greenDurationFactor);

                    memberTL.command += ";set_green_duration:" + std::to_string(finalGreenDurationMs + offsetMs);
                    log(LogLevel::DEBUG, "Comando gerado para " + memberTL.name + ": " + memberTL.command);
                }
                else { 
                    if (memberTL.state == "GREEN") {
                        int targetRemainingMs = leaderRemainingTimeMs + offsetMs;
                        int currentRemainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(memberTL.endTime - now).count();
                        if (std::abs(currentRemainingMs - targetRemainingMs) > 1000) {
                            memberTL.command = ";set_current_time:" + std::to_string(targetRemainingMs);
                            memberTL.endTime = now + std::chrono::milliseconds(targetRemainingMs);
                            log(LogLevel::DEBUG, "Comando gerado para " + memberTL.name + ": " + memberTL.command);
                        }
                    }
                    else if (memberTL.state == "RED") {
                        int memberRemainingTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(memberTL.endTime - now).count();
                        
                        if (memberRemainingTimeMs > 5000) {
                            memberTL.command = ";decrease_time:5000";
                            memberTL.endTime -= std::chrono::milliseconds(offsetMs);
                            log(LogLevel::DEBUG, "Comando gerado para " + memberTL.name + ": " + memberTL.command);
                        }
                        else {
                            int targetRemainingMs = leaderRemainingTimeMs + offsetMs;
                            int finalCommandTime = targetRemainingMs - (getAverageRTT() / 2);
                            if (finalCommandTime < 0) finalCommandTime = 0;

                            memberTL.command = ";set_state:GREEN;set_current_time:" + std::to_string(finalCommandTime);
                            memberTL.endTime = now + std::chrono::milliseconds(targetRemainingMs);
                            memberTL.state = "GREEN";
                            log(LogLevel::DEBUG, "Comando gerado para " + memberTL.name + ": " + memberTL.command);
                        }
                    }
                }
            }
        }
        else if (waveLeaderTL.state != "GREEN") {
            wave.hasBeenTriggered = false;
        }
    }
}


void Orchestrator::processSyncGroups() {
    auto now = std::chrono::steady_clock::now();
    int avgRttOneWay = getAverageRTT() / 2;

    for (const auto& group : syncGroups_) {
        if (group.trafficLightNames.size() < 2) {
            continue; 
        }

        const std::string& leaderName = group.trafficLightNames.front();
        const auto& leaderTL = trafficLights_.at(leaderName);

        for (size_t i = 1; i < group.trafficLightNames.size(); i++) {
            const std::string& followerName = group.trafficLightNames[i];
            auto& followerTL = trafficLights_.at(followerName);

            int remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(leaderTL.endTime - now).count();
            remainingMs -= avgRttOneWay;
            
            int followerRemainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(followerTL.endTime - now).count();

            if (followerTL.state != leaderTL.state && std::abs(followerRemainingMs - remainingMs) > 1000) {
                if (remainingMs < 0) {
                    continue; 
                }

                followerTL.command = ";set_state:" + leaderTL.state + ";set_current_time:" + std::to_string(remainingMs);
                
                followerTL.endTime = leaderTL.endTime;
                followerTL.state = leaderTL.state;

                std::stringstream ss;
                ss << "Forçando " << followerName << " a sincronizar com o líder " << leaderName;
                log(LogLevel::INFO, ss.str());
                log(LogLevel::DEBUG, "Comando gerado para " + followerName + ": " + followerTL.command);
            }
        }
    }
}


void Orchestrator::assignPriorityCommands() {
    float averagePriority = calculateAveragePriority();

    for (auto& [currentLightName, light] : trafficLights_) {
        if (light.isAlert() && !light.partOfIntersection) {
            light.command += ";set_default_duration;set_state:RED;set_current_time:15000"; 
            log(LogLevel::INFO, "Semáforo " + currentLightName + 
                                " em ALERTA. Enviando comando para RESETAR DURAÇÕES e ir para o estado VERMELHO.");
            light.adjustment_state = {0, true};
            continue; 
        }

        auto& adjustment = light.adjustment_state;
        const int MAX_ADJUSTMENTS = 3;
        const std::string ADJUSTMENT_VALUE_MS = "5000"; 

        if (light.priority > averagePriority) {
            if (adjustment.second == false) { 
                if (adjustment.first > 0) {
                    adjustment.first--;
                    log(LogLevel::DEBUG, light.name + " pagando débito. Contador: " + std::to_string(adjustment.first));
                } else { 
                    adjustment.second = true;
                    adjustment.first = 1;
                    light.command += ";increase_green_duration:" + ADJUSTMENT_VALUE_MS + ";decrease_red_duration:" + ADJUSTMENT_VALUE_MS;
                    log(LogLevel::INFO, light.name + " (P:" + std::to_string(light.priority) + ") inverteu tendência para GANHAR tempo.");
                }
            } else { 
                if (adjustment.first < MAX_ADJUSTMENTS) {
                    adjustment.first++;
                    light.command += ";increase_green_duration:" + ADJUSTMENT_VALUE_MS + ";decrease_red_duration:" + ADJUSTMENT_VALUE_MS;
                    log(LogLevel::DEBUG, light.name + " continua a ganhar tempo. Contador: " + std::to_string(adjustment.first));
                } else {
                    log(LogLevel::DEBUG, light.name + " no limite de ganho de tempo.");
                }
            }
        } else if (light.priority == averagePriority) continue;
        else {
            if (adjustment.second == true) {
                if (adjustment.first > 0) { 
                    adjustment.first--;
                    log(LogLevel::DEBUG, light.name + " pagando débito. Contador: " + std::to_string(adjustment.first));
                } else { 
                    adjustment.second = false;
                    adjustment.first = 1;
                    light.command += ";decrease_green_duration:" + ADJUSTMENT_VALUE_MS + ";increase_red_duration:" + ADJUSTMENT_VALUE_MS;
                    log(LogLevel::INFO, light.name + " (P:" + std::to_string(light.priority) + ") inverteu tendência para CEDER tempo.");
                }
            } else {
                if (adjustment.first < MAX_ADJUSTMENTS) {
                    adjustment.first++;
                    light.command += ";decrease_green_duration:" + ADJUSTMENT_VALUE_MS + ";increase_red_duration:" + ADJUSTMENT_VALUE_MS;
                    log(LogLevel::DEBUG, light.name + " continua a ceder tempo. Contador: " + std::to_string(adjustment.first));
                } else {
                    log(LogLevel::DEBUG, light.name + " no limite de cessão de tempo.");
                }
            }
        }
    } 
}
