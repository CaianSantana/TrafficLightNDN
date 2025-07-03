#include "../include/Orchestrator.hpp"

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

            for (auto& [interName, intersectionRef] : intersections_) { // Usa uma referência para poder modificar
                
                // =======================================================
                // FASE 1: WATCHDOG DE INATIVIDADE
                // =======================================================
                bool allLightsAreRed = true;
                for (const auto& lightName : intersectionRef.trafficLightNames) {
                    const auto& state = trafficLights_.at(lightName).state;
                    if (state == "GREEN" || state == "YELLOW") {
                        allLightsAreRed = false;
                        break;
                    }
                }

                if (allLightsAreRed) {
                    m_allRedCounter[interName]++;
                    if (m_allRedCounter[interName] >= allRedTimeoutCycles) {
                        forceCycleStart(interName);
                        m_allRedCounter[interName] = 0;
                    }
                } else {
                    m_allRedCounter[interName] = 0;
                }

                // =======================================================
                // FASE 2: GERAÇÃO DE COMANDOS
                // =======================================================
                for (const auto& lightName : intersectionRef.trafficLightNames) {
                    auto& light = trafficLights_.at(lightName);
                    light.command = "";
                    generateSyncCommand(intersectionRef, lightName);
                }

                // =======================================================
                // FASE 3: LIMPEZA DA FLAG DE NORMALIZAÇÃO
                // =======================================================
                // Se o cruzamento precisava de normalização, os comandos já foram gerados.
                // Agora desativamos a flag para que no próximo ciclo a lógica normal seja executada.
                if (intersectionRef.needsNormalization) {
                    // Modifica o objeto real no mapa através da referência
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

// Em Orchestrator.cpp

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
              intersections_.at(intersection->name).needsNormalization = true; // ATIVA A NORMALIZAÇÃO
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

  if (tokens.size() < 3) { // Alterado para < 3 para incluir a prioridade
    std::cerr << "Invalid message format: " << content << std::endl;
    return;
  }

  std::string state = tokens[0];
  int remainingMs = std::stoi(tokens[1]);

  int correctedRemainingMs = remainingMs - recordRTT(data.getName().toUri());
  interestTimestamps_.erase(it);
  if (correctedRemainingMs < 0)
    correctedRemainingMs = 0;

  // Atualiza o estado do semáforo com os dados recém-recebidos
  tl.state = state;
  tl.endTime = now + milliseconds(correctedRemainingMs);
  tl.priority = std::stof(tokens[2]);
  tl.timeOutCounter = 0; // Zera o contador de timeout, pois a comunicação foi bem-sucedida
}

void Orchestrator::assembleCommandFor(const std::string& name) {
  auto now = std::chrono::steady_clock::now();

  auto& s = trafficLights_.at(name);
  std::string priorityCommand = "";

  if (s.priority < config::MIN_PRIORITY && !s.isUnknown() && !s.isAlert()) {
      priorityCommand = ";set_time:DEFAULT";
  } else if (s.isAlert()) {
      priorityCommand = ";set_state:GREEN;set_time:DEFAULT";
  } else if (s.isUnknown()) {
      priorityCommand = ";set_state:ALERT";
  } else if (s.state == "GREEN" && s.priority >= this->getAveragePrioritySTL()) {
      priorityCommand = ";increase_time:5000";
  } else if (s.state == "RED" && s.priority >= this->getAveragePrioritySTL()) {
      priorityCommand = ";decrease_time:3000";
  }
  
  if (!priorityCommand.empty()) {
      m_lastPriorityCommandTime[name] = now;
  }
  s.command += priorityCommand;
  return;
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
        requesterTL.command = ";set_state:RED;set_current_time:" + std::to_string(config::RECOVERY_RED_TIME_MS);
        requesterTL.endTime = now + std::chrono::milliseconds(config::RECOVERY_RED_TIME_MS);
        return; 
    }
    if (intersection.isCompromised) {
        requesterTL.command = ";set_state:ALERT";
        return; 
    }

    // =================================================================================
    // ETAPA 1: IDENTIFICAR O LÍDER ATIVO REAL (QUEM ESTÁ VERDE/AMARELO)
    // =================================================================================
    std::string activeLightName = "";
    for (const auto& name : intersection.trafficLightNames) {
        if (trafficLights_.at(name).state == "GREEN" || trafficLights_.at(name).state == "YELLOW") {
            activeLightName = name;
            break;
        }
    }

    if (activeLightName.empty()) {
        return;
    }

    if (requesterName == activeLightName) {
        return;
    }

    // Se chegamos aqui, o requisitante é um SEGUIDOR e precisa ser sincronizado.
    // Isso agora inclui o semáforo de maior prioridade quando for a vez dele esperar.
    const auto& activeTL = trafficLights_.at(activeLightName);

    int activeRemainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(activeTL.endTime - now).count();
    if (activeTL.state == "GREEN") {
        activeRemainingMs += config::YELLOW_TIME_MS;
    }

    int finalCommandTime = activeRemainingMs - avgRttOneWay;
    if (finalCommandTime < 0) finalCommandTime = 0;

    int currentRemainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(requesterTL.endTime - now).count();

    // Só envia comando se a diferença for significativa para evitar jitter.
    if (std::abs(currentRemainingMs - activeRemainingMs) > 1000) {
        requesterTL.command = ";set_state:RED;set_current_time:" + std::to_string(finalCommandTime);
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

    leaderTL.command = ";set_state:GREEN;set_time:" + std::to_string(finalCommandTime);
    leaderTL.endTime = now + std::chrono::milliseconds(config::GREEN_BASE_TIME_MS);

    std::cout << "[WATCHDOG] Cruzamento " << intersectionName << " inativo. Forçando início com " << leaderName << std::endl;
}


void Orchestrator::onInterest(const ndn::Interest& interest) {
  std::lock_guard<std::mutex> lock(mutex_);

  const auto& name = interest.getName();
  //std::cout << name.toUri() << std::endl;

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
    std::lock_guard<std::mutex> lock(mutex_); // Garante a segurança de thread

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
                        std::bind(&Orchestrator::onData, this, std::placeholders::_1, std::placeholders::_2),
                        std::bind(&Orchestrator::onNack, this, std::placeholders::_1, std::placeholders::_2),
                        std::bind(&Orchestrator::onTimeout, this, std::placeholders::_1));
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
            auto interest = createInterest(interestName, true, false, 900_ms);
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
    rttMs = rttMs/2;

    rttHistory_.push_back(rttMs);
    
    if (rttHistory_.size() > config::RTT_WINDOW_SIZE) {
        rttHistory_.erase(rttHistory_.begin());
    }

    return rttMs;
}

const Intersection* Orchestrator::findIntersectionFor(const std::string& lightName) const
{
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

void Orchestrator::triggerGreenWave(const std::string& baseLightName) {
  auto now = std::chrono::steady_clock::now();

  // Encontra a qual onda verde (se houver) o semáforo base pertence
  for (const auto& wave : m_greenWaves) {
      auto it = std::find(wave.trafficLightNames.begin(), wave.trafficLightNames.end(), baseLightName);
      
      // Se o semáforo não pertence a esta onda, continua para a próxima
      if (it == wave.trafficLightNames.end()) {
          continue;
      }

      std::cout << "[GREEN_WAVE] Acionando '" << wave.name << "' a partir de " << baseLightName << std::endl;

      // Pega o tempo final do semáforo base como referência
      const auto& baseTL = trafficLights_.at(baseLightName);
      auto baseEndTime = baseTL.endTime;
      size_t baseIndex = std::distance(wave.trafficLightNames.begin(), it);

      // Itera sobre TODOS os semáforos na onda para sincronizá-los
      for (size_t i = 0; i < wave.trafficLightNames.size(); ++i) {
          const std::string& followerName = wave.trafficLightNames[i];
          
          // Não precisa de se sincronizar consigo mesmo
          if (followerName == baseLightName) continue;

          auto& followerTL = trafficLights_.at(followerName);

          // Calcula o atraso (offset) com base na posição na onda
          int offsetMultiplier = static_cast<int>(i) - static_cast<int>(baseIndex);
          auto offset = std::chrono::milliseconds(offsetMultiplier * wave.travelTimeMs);

          // O tempo de verde para os seguidores pode ser um pouco menor para segurança
          auto greenDuration = std::chrono::milliseconds(config::GREEN_BASE_TIME_MS);

          // Define o novo estado e tempo para o seguidor
          // IMPORTANTE: O comando da onda verde SOBRESCREVE qualquer outro comando
          followerTL.command = ";set_state:GREEN;set_time:" + std::to_string(greenDuration.count());
          followerTL.endTime = baseEndTime + offset;
      }
      
      break;
  }
}
