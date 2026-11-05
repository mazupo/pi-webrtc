#include "signaling/mqtt_service.h"

#include <unistd.h>

int main(int argc, char *argv[]) {
    auto mqtt_service =
        MqttService::Create({.username = "hakunamatata", .password = "wonderful"}, nullptr);
    mqtt_service->Connect();

    return 0;
}
