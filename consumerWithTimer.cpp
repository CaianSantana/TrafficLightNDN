#include <ndn-cxx/face.hpp>
#include <ndn-cxx/util/scheduler.hpp>
#include <boost/asio/io_context.hpp>

#include <thread>
#include <iostream>

namespace ndn {
namespace exemplo {

class ConsumerWithTimer
{
public:
  void run()
  {
    Name interestName("/exemplo/appTeste/dadoAleatorio");
    interestName.appendVersion();

    Interest interest(interestName);
    interest.setMustBeFresh(true);
    interest.setInterestLifetime(2_s);

    std::cout << "Sending Interest " << interest << std::endl;
    m_face.expressInterest(interest,
                           std::bind(&ConsumerWithTimer::onData, this, _1, _2),
                           std::bind(&ConsumerWithTimer::onNack, this, _1, _2),
                           std::bind(&ConsumerWithTimer::onTimeout, this, _1));

    
    this->scheduleNextInterest();
    m_ioCtx.run();
  }

private:
  void onData(const Interest&, const Data& data) const
  {
    std::cout << "Received Data " << data << std::endl;
  }

  void onNack(const Interest& interest, const lp::Nack& nack) const
  {
    std::cout << "Received Nack with reason " << nack.getReason()
              << " for " << interest << std::endl;
  }

  void onTimeout(const Interest& interest) const
  {
    std::cout << "Timeout for " << interest << std::endl;
  }

  void delayedInterest()
  {
    std::cout << "One more Interest, delayed by the scheduler" << std::endl;

    Name interestName("/exemplo/appTeste/dadoAleatorio");
    interestName.appendVersion();

    Interest interest(interestName);
    interest.setMustBeFresh(true);
    interest.setInterestLifetime(2_s);

    std::cout << "Sending Interest " << interest << std::endl;
    m_face.expressInterest(interest,
                           std::bind(&ConsumerWithTimer::onData, this, _1, _2),
                           std::bind(&ConsumerWithTimer::onNack, this, _1, _2),
                           std::bind(&ConsumerWithTimer::onTimeout, this, _1));
  }
  void scheduleNextInterest()
    {
    m_scheduler.schedule(3_s, [this] {
        this->delayedInterest();
        this->scheduleNextInterest(); // ⬅️ agenda o próximo envio
    });
    }

private:
  boost::asio::io_context m_ioCtx;
  Face m_face{m_ioCtx};
  Scheduler m_scheduler{m_ioCtx};
};

} // namespace exemplo
} // namespace ndn

int main(int argc, char** argv)
{
  try {
    ndn::exemplo::ConsumerWithTimer consumer;
    consumer.run();
    return 0;
  }
  catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    return 1;
  }
}
