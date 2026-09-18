#pragma once

#include <Arduino.h>

namespace ServerConnection {
    void begin();
    void loop();
    bool connected();
    void forceReconnect();
    void startPairingMode();
    String getPairingSecret();
}
