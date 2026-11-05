#include "signaling/livekit_service.h"

int main() {
    // The service reads the fields derived from --livekit-url, so set them directly rather
    // than linking the whole parser in just to expand a URL. Designated initializers must
    // follow the declaration order in LiveKitConfig.
    LiveKitConfig config{
        .host = "localhost",
        .room = "",
        .key = "",
        .uid = "test_device",
    };

    net::io_context ioc;

    boost::asio::steady_timer timer(ioc, std::chrono::seconds(5));
    timer.async_wait([&ioc](const boost::system::error_code &ec) {
        if (!ec) {
            std::cout << "no events in io_context" << std::endl;
            ioc.stop();
        }
    });

    auto ws_service = LiveKitService::Create(config, nullptr, ioc);
    ws_service->Connect();

    ioc.run();
    return 0;
}
