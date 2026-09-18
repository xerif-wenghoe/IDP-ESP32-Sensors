#pragma once

#include <Arduino.h>

namespace WifiManager {
    void begin();
    void loop();
    bool connected();
    void reconnect();
    void startProvisioning();

    // Pairing portal runs in WIFI_AP_STA mode, so the normal station
    // connection (and WebSocket) remains active while the phone connects
    // to the temporary local AP.
    bool startPairingAccessPoint(const String &systemId, const String &secret);
    void stopPairingAccessPoint();
    bool pairingAccessPointActive();
    String pairingAccessPointName();
    String pairingAccessPointIp();

    String ipAddress();
}
