#include "Orchestrator.hpp"
#include <ndn-cxx/util/time.hpp>
#include <iostream>
#include <chrono>

Orchestrator::Orchestrator()
  : face_(boost::asio::make_strand(boost::asio::io_context())),
    scheduler_(face_.getIoService())
{}

Orchestrator::~Orchestrator() {
  face_.shutdown();
}

void Orchestrator::setup(std::string prefix, int id) {
  prefix_ = std::move(prefix);
  id_ = id;

  loadTopology();
}

void Orchestrator::loadTopology(){
    // TODO: carregar topologia do arquivo YAML (mock exemplo)
    trafficLights_["semaforoA"] = {"semaforoA", "RED", std::chrono::steady_clock::now(), std::chrono::steady_clock::now(), 0, ""};
    trafficLights_["semaforoB"] = {"semaforoB", "GREEN", std::chrono::steady_clock::now(), std::chrono::steady_clock::now(), 0, ""};
}

void Orchestrator::run(const std::string& url) {
    scheduler_.schedule(ndn::time::seconds(1), [this]{ runConsumer(); });
    runProducer("command");
    runProducer("clock");
    face_.processEvents();
}

void Orchestrator::runProducer(const std::string& suffix){
  ndn::Name nameSufix = ndn::Name(prefix_).append(sufix);
  face_.setInterestFilter(nameSufix,
      [this](const ndn::InterestFilter& filter, const ndn::Interest& interest) {
        this->onInterest(interest);
      },
      [this](const ndn::Name& nameSufix, const std::string& reason) {
        this->onRegisterFailed(nameSufix, reason);
      });
}


void Orchestrator::onData(const ndn::Interest& interest, const ndn::Data& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string content(reinterpret_cast<const char*>(data.getContent().value()), data.getContent().value_size());

    std::string semaforoName = interest.getName().at(-2).toUri();

    try {
        int priority = std::stoi(content);
        trafficLights_[semaforoName].priority = priority;
    } catch (const std::exception& e) {
        std::cerr << "Erro ao converter prioridade recebida: " << e.what() << std::endl;
    }

    if (trafficLights_.count(semaforoName)) {
        trafficLights_[semaforoName].priority = priority;
    }

    checkPriorities();
    }

void Orchestrator::checkPriorities() {
  // Exemplo: só printa o maior valor
  int maxPriority = -1;
  std::string maxName;
  for (const auto& [name, state] : trafficLights_) {
    if (state.priority > maxPriority) {
      maxPriority = state.priority;
      maxName = name;
    }
  }
  std::cout << "Semáforo com maior prioridade: " << maxName << " = " << maxPriority << std::endl;

  // TODO: definir comandos para cada semáforo baseado na prioridade
  // Exemplo: se maior prioridade, comando "increase_time", outros "do_nothing"
  for (auto& [name, state] : trafficLights_) {
    if (name == maxName) {
      state.command = "increase_time";
    }
    else {
      state.command = "do_nothing";
    }
  }
}

void Orchestrator::onInterest(const ndn::Interest& interest) {
  std::lock_guard<std::mutex> lock(mutex_);

    if (interest.getName().size() >= 1 
    && interest.getName().at(-1).toUri() == "clock"){
        return produceClockData(interest);
    } else if (interest.getName().size() >= 2 
    && interest.getName().get(-2).toUri() == "command"){
        std::string semaforoName = interest.getName().at(-1).toUri();

        if (!trafficLights_.count(semaforoName)) {
            std::cerr << "Semáforo desconhecido para comando: " << semaforoName << std::endl;
            return;
        }
        return produceCommand(semaforoName, interest);
    } else{
        std::cerr << "Sufixo inválido.\n";
        return;
    }
}

void Orchestrator::produceClockData(const ndn::Interest& interest) {
  auto now = std::chrono::system_clock::now();
  auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

  std::string timeStr = std::to_string(nowMs);
  clockTimestampMs_ = nowMs;


  auto data = std::make_shared<ndn::Data>(interest.getName());
  data->setContent(reinterpret_cast<const uint8_t*>(timeStr.data()), timeStr.size());
  data->setFreshnessPeriod(ndn::time::seconds(1));

  keyChain_.sign(*data);
  face_.put(*data);
}

void Orchestrator::produceCommand(std::string semaforoName, const ndn::Interest& interest){
    std::string cmd = trafficLights_[semaforoName].command;
    auto data = std::make_shared<ndn::Data>(interest.getName());
    data->setContent(reinterpret_cast<const uint8_t*>(cmd.data()), cmd.size());
    data->setFreshnessPeriod(ndn::time::seconds(1));

    keyChain_.sign(*data);
    face_.put(*data);
}

void Orchestrator::onNack(const ndn::Interest& interest, const ndn::lp::Nack& nack) {
  std::cerr << "Nack recebido: " << interest.getName().toUri() << " Reason: " << nack.getReason() << std::endl;
}

void Orchestrator::onTimeout(const ndn::Interest& interest) {
  std::cerr << "Timeout de Interest: " << interest.getName().toUri() << std::endl;
}

ndn::Interest Orchestrator::createInterest(ndn::Name& name, bool mustBeFresh, bool canBePrefix, ndn::time::milliseconds lifetime) {
    ndn::Interest interest(name);
    interest.setMustBeFresh(mustBeFresh);
    interest.setCanBePrefix(canBePrefix);
    interest.setInterestLifetime(lifetime);
    return interest;
}

void Orchestrator::sendInterest(const ndn::Interest& interest) {
  face_.expressInterest(interest,
    [this](const ndn::Interest& interest, const ndn::Data& data) { onData(interest, data); },
    [this](const ndn::Interest& interest, const ndn::lp::Nack& nack) { onNack(interest, nack); },
    [this](const ndn::Interest& interest) { onTimeout(interest); });
}

void Orchestrator::runConsumer() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [name, state] : trafficLights_) {
        ndn::Name interestName(prefix_);
        interestName.append(name).append("priority");

        sendInterest(createInterest(interestName, true, false, ndn::time::seconds(2)));
    }
    scheduler_.schedule(ndn::time::seconds(2), [this]{ runConsumer(); });
}

void Orchestrator::onRegisterFailed(const ndn::Name& nome, const std::string& reason) {
  std::cerr << "Falha ao registrar nome: " << nome.toUri() << " motivo: " << reason << std::endl;
}
