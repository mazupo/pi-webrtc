#include "signaling/whep_service.h"

int main() {
    boost::asio::io_context ioc;

    boost::asio::steady_timer timer(ioc, std::chrono::seconds(5));
    timer.async_wait([&ioc](const boost::system::error_code &ec) {
        if (!ec) {
            std::cout << "no events in io_context" << std::endl;
            ioc.stop();
        }
    });

    auto whep_service = WhepService::Create({}, nullptr, ioc);
    whep_service->Connect();

    ioc.run();
    return 0;
}
