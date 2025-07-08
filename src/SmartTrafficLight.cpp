#include "../include/SmartTrafficLight.hpp"

using namespace std::chrono;

SmartTrafficLight::SmartTrafficLight() 
   : m_validator(m_face)
{
    m_validator.load("config/trust-schema.conf");
}

SmartTrafficLight::~SmartTrafficLight() {
  m_stopFlag = true;
  if (m_cycleThread.joinable()) {
    m_cycleThread.join();
  }
}

void SmartTrafficLight::setup(const std::string& prefix){
    central = prefix;
}

void SmartTrafficLight::loadConfig(const TrafficLightState& config, LogLevel level) {
    this->m_logLevel = level;

    constexpr int TA = 3; 

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
    
    log(LogLevel::INFO, "Configuração carregada com sucesso.");
}

void SmartTrafficLight::log(LogLevel level, const std::string& message) {
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

void SmartTrafficLight::run() {
    index = static_cast<size_t>(start_color);
    runProducer("");
    runConsumer();
    m_cycleThread = std::thread([this] { this->cycle(); });
    m_face.processEvents();
}

void SmartTrafficLight::cycle() {
  while (!m_stopFlag) {
    if (current_color == Color::ALERT) {
      log(LogLevel::DEBUG, "Estado de ALERTA ativo.");
      generateTraffic();
      {
        std::lock_guard<std::mutex> lock(m_mutex);
        passVehicles();
      }
      std::this_thread::sleep_for(std::chrono::seconds(1));
      continue;
    }

    const auto& [color_str, seconds] = colors_vector[static_cast<size_t>(current_color)];
    Color initial_color = current_color;
    time_left = seconds;
    int count = 0;
    log(LogLevel::INFO, ToString(current_color));
    while (time_left > 0 && !m_stopFlag) {
      if(current_color != initial_color){
        log(LogLevel::INFO, ToString(current_color));
        initial_color=current_color;
      } 
      log(LogLevel::DEBUG, ToString(current_color) + ": " + std::to_string(time_left) + " segundos");
      log(LogLevel::DEBUG, "Veículos no semáforo: " + std::to_string(vehicles));
      generateTraffic();

      if (current_color != Color::RED) {
        if (current_color == Color::GREEN && count == 0) {
          full_cicle_vehicles_quantity = 0;
        }
        if (vehicles > 0 && count >= 2) {
          std::lock_guard<std::mutex> lock(m_mutex);
          passVehicles();
        } else {
          count++;
        }
      } else {
        count = 0;
      }

      time_left--;
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Troca de cor
    switch (current_color) {
      case Color::GREEN:  current_color = Color::YELLOW; break;
      case Color::YELLOW: current_color = Color::RED;    break;
      case Color::RED:    current_color = Color::GREEN;  break;
      default: break;
    }
  }

  log(LogLevel::INFO, "Thread de ciclo finalizada.");
}


void SmartTrafficLight::passVehicles(){
    int vehicles_to_pass = std::min(vehicles, columns);
    vehicles_to_pass = generateNumber(0, vehicles_to_pass);
    vehicles -= vehicles_to_pass;
    log(LogLevel::DEBUG, "Veículos passaram: " + std::to_string(vehicles_to_pass));
}

float SmartTrafficLight::calculatePriority() {
    float basePriority = full_cicle_vehicles_quantity * 0.5f
                         + (static_cast<float>(vehicles) / capacity) * 5;

    log(LogLevel::DEBUG, "Prioridade: " + std::to_string(basePriority));
    return basePriority;
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
    }
}


void SmartTrafficLight::runConsumer() {
    m_scheduler.schedule(1000_ms, [this] {
        auto interestCommand = createInterest(central + "/command"+prefix_, true, false, 4000_ms);
        sendInterest(interestCommand);
        runConsumer();
    });
}

void SmartTrafficLight::runProducer(const std::string& suffix){
  ndn::Name nameSuffix = ndn::Name(prefix_).append(suffix);
  log(LogLevel::INFO, "Registrando produtor para o prefixo: " + nameSuffix.toUri());
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
}

void SmartTrafficLight::onInterest(const ndn::Interest& interest) {
    log(LogLevel::DEBUG, "Recebeu Interest para: " + interest.getName().toUri());

    std::string currentStateStr = ToString(current_color); 
    int remainingMs = time_left * 1000; 
    float priority = calculatePriority();

    std::ostringstream oss;
    oss << currentStateStr << "|" << remainingMs << "|" << priority;

    std::string content = oss.str();

    auto data = std::make_shared<ndn::Data>(interest.getName());
    data->setContent(ndn::make_span(reinterpret_cast<const uint8_t*>(content.data()), content.size()));
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
    std::lock_guard<std::mutex> lock(m_mutex);
    lastInterestTimestamp_= std::chrono::steady_clock::now();
  }
  m_face.expressInterest(interest,
                        std::bind(&SmartTrafficLight::onData, this, std::placeholders::_1, std::placeholders::_2),
                        std::bind(&SmartTrafficLight::onNack, this, std::placeholders::_1, std::placeholders::_2),
                        std::bind(&SmartTrafficLight::onTimeout, this, std::placeholders::_1));
  log(LogLevel::DEBUG, "Enviando Interest para: " + interest.getName().toUri());
}


void SmartTrafficLight::onData(const ndn::Interest& interest, const ndn::Data& data) {
    auto now = std::chrono::steady_clock::now();
    const auto content = std::string(reinterpret_cast<const char*>(data.getContent().value()), data.getContent().value_size());
    const auto DUPLICATION_WINDOW = std::chrono::seconds(4);

    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!content.empty() && content == m_lastCommandContent && (now - m_lastCommandTimestamp) < DUPLICATION_WINDOW) {
        return; 
    }
    
    
    if (!content.empty()) {
        m_lastCommandContent = content;
        m_lastCommandTimestamp = now;
    }

    if (m_timeoutCounter > 0) {
        log(LogLevel::DEBUG, "Contador de timeout zerado após comunicação bem-sucedida.");
        m_timeoutCounter = 0;
    }

    log(LogLevel::DEBUG, "Recebeu Data de: " + data.getName().toUri());
    std::vector<Command> commands = parseContent(content);
    for (const auto& cmd : commands) {
        if(!applyCommand(cmd))
            break;
    }
}

std::vector<Command> SmartTrafficLight::parseContent(const std::string& rawCommand) {
    std::vector<Command> commands;
    std::istringstream ss(rawCommand);
    std::string commandStr;

    // Ignora o primeiro caractere se for um delimitador
    if (!ss.str().empty() && ss.str()[0] == ';') {
        ss.ignore(1);
    }

    while (std::getline(ss, commandStr, ';')) {
        if (commandStr.empty()) continue;
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

bool SmartTrafficLight::applyCommand(const Command& cmd) {
  if (cmd.type.empty()) return false;
  if (cmd.type == "set_state") {
      Color new_color = parseColor(cmd.value);
      if (new_color != current_color) {
        current_color = new_color;
        log(LogLevel::DEBUG, "Cor alterada para " + cmd.value);
      }
  }else if (cmd.type == "set_time") {
      int newTime;
      if (cmd.value == "DEFAULT") {
          newTime = getDefaultColorTime(current_color);
      } else {
          newTime = std::stoi(cmd.value);
      }
      if (current_color == Color::ALERT){
          time_left = newTime;
      }
      updateColorVectorTime(current_color, newTime);
  }else if (cmd.type == "set_default_duration"){
      updateColorVectorTime(Color::GREEN, getDefaultColorTime(Color::GREEN));
      updateColorVectorTime(Color::YELLOW, getDefaultColorTime(Color::YELLOW));
      updateColorVectorTime(Color::RED, getDefaultColorTime(Color::RED));
      log(LogLevel::INFO, "Durações de ciclo reconfiguradas para o padrão inicial.");
  }
  else if (cmd.type == "set_green_duration"){
      int newTime = std::stoi(cmd.value)/1000;
      updateColorVectorTime(Color::GREEN, newTime);
      log(LogLevel::DEBUG, "Tempo verde alterado para " + std::to_string(newTime));
  }
  else if (cmd.type == "set_red_duration"){
      int newTime = std::stoi(cmd.value)/1000;
      updateColorVectorTime(Color::RED, newTime);
      log(LogLevel::DEBUG, "Tempo vermelho alterado para " + std::to_string(newTime));
  }
  else if (cmd.type == "increase_green_duration"){
      size_t index = static_cast<size_t>(Color::GREEN);
      int increment = std::stoi(cmd.value)/1000;
      colors_vector[index].second += increment;
      log(LogLevel::DEBUG, "Duração do VERDE aumentada em " + std::to_string(increment) + "s. Nova duração: " + std::to_string(colors_vector[index].second) + "s.");
  }
  else if (cmd.type == "decrease_green_duration"){
      size_t index = static_cast<size_t>(Color::GREEN);
      int decrement = std::stoi(cmd.value)/1000;
      if (colors_vector[index].second > decrement + 5) {
          colors_vector[index].second -= decrement;
          log(LogLevel::DEBUG, "Duração do VERDE diminuída em " + std::to_string(decrement) + "s. Nova duração: " + std::to_string(colors_vector[index].second) + "s.");
      }
  }
  else if (cmd.type == "increase_red_duration"){
      size_t index = static_cast<size_t>(Color::RED);
      int increment = std::stoi(cmd.value)/1000;
      colors_vector[index].second += increment;
      log(LogLevel::DEBUG, "Duração do VERMELHO aumentada em " + std::to_string(increment) + "s. Nova duração: " + std::to_string(colors_vector[index].second) + "s.");
  }
  else if (cmd.type == "decrease_red_duration"){
      size_t index = static_cast<size_t>(Color::RED);
      int decrement = std::stoi(cmd.value)/1000;
      if (colors_vector[index].second > decrement + 5) {
          colors_vector[index].second -= decrement;
          log(LogLevel::DEBUG, "Duração do VERMELHO diminuída em " + std::to_string(decrement) + "s. Nova duração: " + std::to_string(colors_vector[index].second) + "s.");
      }
  }
  else if (cmd.type == "set_current_time"){
      int newTime = std::stoi(cmd.value); 
      time_left = newTime / 1000; 
      log(LogLevel::DEBUG, "Tempo alterado para " + std::to_string(time_left));
      auto now = std::chrono::steady_clock::now();
  }
  else if (cmd.type == "increase_time") {
      int increment = std::stoi(cmd.value);
      time_left += increment/1000;
      log(LogLevel::DEBUG, "Tempo aumentado em " + std::to_string(increment) + "s.");
  }
  else if (cmd.type == "decrease_time") {
      int decrement = std::stoi(cmd.value);
      time_left -= decrement/1000; 
      log(LogLevel::DEBUG, "Tempo diminuido em " + std::to_string(decrement) + "s.");
  }
  return true;
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
  std::stringstream ss;
  ss << "NACK para " << interest.getName().toUri() << ". Motivo: " << nack.getReason();
  log(LogLevel::ERROR, ss.str());

  std::lock_guard<std::mutex> lock(m_mutex);
  current_color = Color::ALERT;
  log(LogLevel::INFO, "Entrando em modo de ALERTA devido a NACK na comunicação.");
}

void SmartTrafficLight::onTimeout(const ndn::Interest& interest) {
  log(LogLevel::ERROR, "Timeout para " + interest.getName().toUri());

  std::lock_guard<std::mutex> lock(m_mutex);
  m_timeoutCounter++;
  log(LogLevel::DEBUG, "Contador de timeout: " + std::to_string(m_timeoutCounter));

  if (m_timeoutCounter >= TIMEOUT_THRESHOLD) {
    if (current_color != Color::ALERT) {
      current_color = Color::ALERT;
      log(LogLevel::INFO, "Entrando em modo de ALERTA após " + std::to_string(TIMEOUT_THRESHOLD) + " timeouts consecutivos.");
    }
  }
}

void SmartTrafficLight::onRegisterFailed(const ndn::Name& nome, const std::string& reason) {
  log(LogLevel::ERROR, "Falha CRÍTICA ao registrar prefixo: " + nome.toUri() + ". Motivo: " + reason);
  log(LogLevel::ERROR, "Não é possível continuar. Encerrando a aplicação.");

  m_stopFlag = true; 
  m_face.shutdown();
}