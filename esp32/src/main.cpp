#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <time.h>

#include "config.h"
#include "motor.h"
#include "server_connection.h"
#include "storage.h"
#include "wifi_manager.h"

Adafruit_BME280 bme;
bool bmeAvailable = false;

struct ButtonState {
    uint8_t pin;
    bool previous;
    unsigned long pressedAt;
    bool longTriggered;
};

ButtonState b1{B1_WIFI_BUTTON, HIGH, 0, false};
ButtonState b2{B2_SERVER_BUTTON, HIGH, 0, false};

static void handleButton(
    ButtonState &button,
    void (*shortPress)(),
    void (*longPress)()
) {
    bool current = digitalRead(button.pin);

    if (button.previous == HIGH && current == LOW) {
        button.pressedAt = millis();
        button.longTriggered = false;
    }

    if (button.previous == LOW && current == LOW) {
        if (!button.longTriggered &&
            millis() - button.pressedAt >= BUTTON_LONG_PRESS_MS) {

            button.longTriggered = true;
            longPress();
        }
    }

    if (button.previous == LOW && current == HIGH) {
        if (!button.longTriggered) {
            shortPress();
        }
    }

    button.previous = current;
}

void wifiShortPress() {
    Serial.println("B1 short press: reconnect Wi-Fi");
    WifiManager::reconnect();
}

void wifiLongPress() {
    Serial.println("B1 long press: Wi-Fi provisioning");
    WifiManager::startProvisioning();
}

void serverShortPress() {
    Serial.println("B2 short press: WebSocket reconnect");
    ServerConnection::forceReconnect();
}

void serverLongPress() {
    Serial.println("B2 long press: start pairing AP + generate new secret");
    ServerConnection::startPairingMode();
}

bool readAllSensors(
    bool &rain,
    float &temperature,
    float &pressure,
    float &humidity
) {
    rain = (digitalRead(RAIN_DO_PIN) == RAIN_ACTIVE_LEVEL);

    if (!bmeAvailable) {
        temperature = NAN;
        pressure = NAN;
        humidity = NAN;
        return false;
    }

    temperature = bme.readTemperature();
    pressure = bme.readPressure() / 100.0F;
    humidity = bme.readHumidity();

    return !isnan(temperature) &&
           !isnan(pressure) &&
           !isnan(humidity);
}

void setupTime() {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");

    struct tm timeinfo;

    Serial.print("Waiting for NTP");

    for (int i = 0; i < 20; ++i) {
        if (getLocalTime(&timeinfo, 500)) {
            Serial.println(" OK");
            return;
        }

        Serial.print(".");
    }

    Serial.println();
}

void setup() {
    Serial.begin(115200);
    delay(500);

    pinMode(B1_WIFI_BUTTON, INPUT_PULLUP);
    pinMode(B2_SERVER_BUTTON, INPUT_PULLUP);
    pinMode(RAIN_DO_PIN, INPUT);

    pinMode(LED_WIFI, OUTPUT);
    pinMode(LED_SERVER, OUTPUT);
    pinMode(ALWAYS_ON_GPIO, OUTPUT);
    digitalWrite(ALWAYS_ON_GPIO, HIGH);

    Serial.println("LED self-test: both LEDs should flash now.");
    digitalWrite(LED_WIFI, HIGH);
    digitalWrite(LED_SERVER, HIGH);
    delay(400);
    digitalWrite(LED_WIFI, LOW);
    digitalWrite(LED_SERVER, LOW);
    delay(400);

    // Apply the configured OFF polarity before normal state control begins.
    setWifiLed(false);
    setServerLed(false);

    Wire.begin(I2C_SDA, I2C_SCL);

    bmeAvailable = bme.begin(0x76);

    if (!bmeAvailable) {
        bmeAvailable = bme.begin(0x77);
    }

    if (bmeAvailable) {
        Serial.println("BME280 detected.");
    } else {
        Serial.println("BME280 not detected.");
    }

    Storage::begin();
    Motor::begin();

    Serial.println();
    Serial.println("==================================");
    Serial.println(" Smart Drying Rack MAIN ESP32 (3-device system)");
    Serial.printf(
        " Device ID: %s\n",
        Storage::getDeviceId().c_str()
    );
    Serial.printf(
        " Firmware: %s\n",
        FIRMWARE_VERSION
    );
    Serial.println("==================================");

    WifiManager::begin();
}

void loop() {
    WifiManager::loop();

    static bool serverStarted = false;

    if (WifiManager::connected() && !serverStarted) {
        setupTime();
        ServerConnection::begin();
        serverStarted = true;
    }

    if (!WifiManager::connected()) {
        serverStarted = false;
    }

    ServerConnection::loop();

    handleButton(
        b1,
        wifiShortPress,
        wifiLongPress
    );

    handleButton(
        b2,
        serverShortPress,
        serverLongPress
    );

    delay(5);
}
