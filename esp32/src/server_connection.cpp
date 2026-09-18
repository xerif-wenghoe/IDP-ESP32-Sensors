#include "server_connection.h"
#include "config.h"
#include "motor.h"
#include "storage.h"
#include "wifi_manager.h"

#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <WiFi.h>

static WebSocketsClient webSocket;

static String deviceId;
static String pairingSecret;
static bool wsConnected = false;

static unsigned long lastWsAttempt = 0;
static unsigned long lastHeartbeat = 0;
static unsigned long lastSensor = 0;

static const char *SERVER_HOST = "idp-smart-drying-rack-server.onrender.com";  // CHANGE THIS
static const uint16_t SERVER_PORT = 443;
static const char *SERVER_PATH = "/ws/device";

size_t offlineBatchAwaitingAck = 0;

static String randomSecret() {
    const char *alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    String out;
    out.reserve(8);

    for (int i = 0; i < 8; ++i) {
        out += alphabet[esp_random() % 32];
    }

    return out;
}

static String isoTimestamp() {
    struct tm timeinfo;

    if (!getLocalTime(&timeinfo, 100)) {
        return "";
    }

    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);

    return String(buf);
}

static bool readSensors(
    bool &rain,
    float &temperature,
    float &pressure,
    float &humidity
) {
    extern bool readAllSensors(bool &, float &, float &, float &);
    return readAllSensors(rain, temperature, pressure, humidity);
}

static void sendSensor() {
    if (!wsConnected) return;

    bool rain;
    float temperature;
    float pressure;
    float humidity;

    if (!readSensors(rain, temperature, pressure, humidity)) {
        return;
    }

    JsonDocument doc;

    doc["type"] = "sensor";
    doc["id"] = String(esp_random(), HEX);
    doc["rain"] = rain;
    doc["temperature"] = temperature;
    doc["pressure"] = pressure;
    doc["humidity"] = humidity;
    doc["timestamp"] = isoTimestamp();

    String output;
    serializeJson(doc, output);

    webSocket.sendTXT(output);

    lastSensor = millis();
}

static void sendOfflineBatch() {
    if (!wsConnected) return;

    if (Storage::count() == 0) return;

    JsonDocument doc;

    if (!Storage::readBatch(doc, OFFLINE_BATCH_SIZE)) {
        return;
    }

    doc["type"] = "offline_batch";

    offlineBatchAwaitingAck = doc["items"].size();

    String output;
    serializeJson(doc, output);

    webSocket.sendTXT(output);
}

static void webSocketEvent(
    WStype_t type,
    uint8_t *payload,
    size_t length
) {
    switch (type) {
        case WStype_DISCONNECTED:
            wsConnected = false;
            setServerLed(false);
            Serial.println("WebSocket disconnected");
            break;

        case WStype_CONNECTED: {
            wsConnected = true;
            setServerLed(true);
            
            JsonDocument doc;
            doc["type"] = "hello";
            doc["device_id"] = deviceId;
            doc["firmware_version"] = FIRMWARE_VERSION;
            doc["role"] = DEVICE_ROLE;
            doc["system_esp_count"] = SYSTEM_ESP_COUNT;

            String out;
            serializeJson(doc, out);
            webSocket.sendTXT(out);

            break;
        }

        case WStype_TEXT: {
            JsonDocument doc;

            if (deserializeJson(doc, payload, length)) {
                return;
            }

            const char *messageType = doc["type"] | "";

            if (strcmp(messageType, "hello_ack") == 0) {
                bool paired = doc["paired"] | false;

                Serial.printf(
                    "Server acknowledged hello. Paired=%d\n",
                    paired
                );

                if (pairingSecret.length() > 0) {
                    JsonDocument p;
                    p["type"] = "pairing_secret";
                    p["secret"] = pairingSecret;

                    String out;
                    serializeJson(p, out);

                    webSocket.sendTXT(out);
                }

                sendOfflineBatch();
            }

            else if (strcmp(messageType, "offline_batch_ack") == 0) {
                if (offlineBatchAwaitingAck > 0) {
                    Storage::removeBatch(offlineBatchAwaitingAck);
                    offlineBatchAwaitingAck = 0;
                }

                sendOfflineBatch();
            }

            else if (strcmp(messageType, "pair_result") == 0) {
                bool success = doc["success"] | false;

                if (success) {
                    Serial.println("Pairing accepted.");
                    pairingSecret = "";
                    WifiManager::stopPairingAccessPoint();
                }
            }

            else if (strcmp(messageType, "motor") == 0) {
                const char *action = doc["action"] | "";

                if (strcmp(action, "move_to_shelter") == 0) {
                    Motor::moveToShelter();
                }
                else if (strcmp(action, "move_outside") == 0) {
                    Motor::moveOutside();
                }

                JsonDocument ack;
                ack["type"] = "motor_done";
                ack["action"] = action;

                String out;
                serializeJson(ack, out);

                webSocket.sendTXT(out);
            }

            else if (strcmp(messageType, "unpaired") == 0) {
                Serial.println("Device was unpaired by server.");
            }

            break;
        }

        default:
            break;
    }
}

namespace ServerConnection {

void begin() {
    deviceId = Storage::getDeviceId();

    pinMode(LED_SERVER, OUTPUT);
    setServerLed(false);

    forceReconnect();
}

void forceReconnect() {
    if (!WifiManager::connected()) return;

    Serial.printf(
        "WebSocket: ws://%s:%u%s\n",
        SERVER_HOST,
        SERVER_PORT,
        SERVER_PATH
    );

    webSocket.disconnect();
    webSocket.beginSSL(
        SERVER_HOST,
        SERVER_PORT,
        SERVER_PATH
    );
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(WS_RETRY_INTERVAL_MS);
}

void startPairingMode() {
    if (!WifiManager::connected()) {
        Serial.println("Cannot start pairing mode: main ESP32 is not connected to Wi-Fi.");
        return;
    }

    pairingSecret = randomSecret();

    // Start a temporary SoftAP while retaining STA mode. This allows the user's
    // phone to read the System ID + rotating secret at 192.168.4.1 without
    // interrupting the existing WebSocket connection to FastAPI.
    if (!WifiManager::startPairingAccessPoint(deviceId, pairingSecret)) {
        pairingSecret = "";
        return;
    }

    Serial.println();
    Serial.println("========== SYSTEM PAIRING ==========");
    Serial.print("System ID: ");
    Serial.println(deviceId);
    Serial.print("Secret: ");
    Serial.println(pairingSecret);
    Serial.print("Connect phone to: ");
    Serial.println(WifiManager::pairingAccessPointName());
    Serial.print("Then open: http://");
    Serial.println(WifiManager::pairingAccessPointIp());
    Serial.println("WebSocket remains connected through station Wi-Fi.");
    Serial.println("====================================");

    if (wsConnected) {
        JsonDocument doc;
        doc["type"] = "pairing_secret";
        doc["secret"] = pairingSecret;

        String out;
        serializeJson(doc, out);

        webSocket.sendTXT(out);
    } else {
        Serial.println("Warning: pairing AP is active, but WebSocket is not connected yet.");
    }
}

void loop() {
    if (!WifiManager::connected()) {
        setServerLed(false);
        return;
    }

    if (!wsConnected &&
        millis() - lastWsAttempt >= WS_RETRY_INTERVAL_MS) {

        lastWsAttempt = millis();
        forceReconnect();
    }

    webSocket.loop();

    if (!wsConnected) return;

    if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
        JsonDocument doc;
        doc["type"] = "heartbeat";
        doc["timestamp"] = isoTimestamp();

        String out;
        serializeJson(doc, out);

        webSocket.sendTXT(out);

        lastHeartbeat = millis();
    }

    if (millis() - lastSensor >= SENSOR_INTERVAL_MS) {
        sendSensor();
    }
}

bool connected() {
    return wsConnected;
}

String getPairingSecret() {
    return pairingSecret;
}

}
