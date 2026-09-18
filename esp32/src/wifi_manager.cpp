#include "wifi_manager.h"
#include "config.h"
#include "storage.h"

#include <WiFi.h>
#include <WebServer.h>

static WebServer server(80);

enum class PortalMode {
    NONE,
    WIFI_PROVISIONING,
    PAIRING
};

static PortalMode portalMode = PortalMode::NONE;
static bool webServerStarted = false;
static unsigned long lastAttempt = 0;
static unsigned long pairingStartedAt = 0;

static String currentPairingSystemId;
static String currentPairingSecret;
static String currentPairingApName;

static String htmlEscape(const String &input) {
    String out;
    out.reserve(input.length() + 16);

    for (size_t i = 0; i < input.length(); ++i) {
        const char c = input[i];
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '\"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c; break;
        }
    }

    return out;
}

static String pageShell(const String &title, const String &body) {
    return
        "<!doctype html><html><head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>" + htmlEscape(title) + "</title>"
        "<style>"
        "body{font-family:Arial,sans-serif;background:#f4f6f8;margin:0;padding:24px;color:#17202a;}"
        ".card{max-width:520px;margin:40px auto;background:white;border-radius:16px;padding:28px;"
        "box-shadow:0 8px 30px rgba(0,0,0,.10);}"
        "h1{font-size:24px;margin-top:0;}"
        ".label{font-size:13px;color:#667085;margin-top:20px;}"
        ".value{font-family:monospace;font-size:25px;font-weight:700;letter-spacing:1px;"
        "background:#f2f4f7;border-radius:10px;padding:13px;margin-top:6px;word-break:break-all;}"
        ".secret{font-size:34px;letter-spacing:4px;text-align:center;}"
        ".ok{background:#ecfdf3;border:1px solid #abefc6;border-radius:10px;padding:12px;margin:18px 0;}"
        ".note{font-size:14px;color:#475467;line-height:1.55;}"
        "input{box-sizing:border-box;width:100%;padding:11px;margin-top:5px;border:1px solid #ccc;border-radius:8px;}"
        "button{margin-top:18px;padding:12px 18px;border:0;border-radius:8px;background:#111827;color:white;font-weight:600;}"
        "</style></head><body><div class='card'>" + body + "</div></body></html>";
}

static void rootPage() {
    if (portalMode == PortalMode::PAIRING) {
        const String body =
            "<h1>Smart Drying Rack Pairing</h1>"
            "<div class='ok'>The main ESP32 is still connected to Wi-Fi and the FastAPI WebSocket server.</div>"
            "<div class='label'>System ID</div>"
            "<div class='value'>" + htmlEscape(currentPairingSystemId) + "</div>"
            "<div class='label'>One-time secret</div>"
            "<div class='value secret'>" + htmlEscape(currentPairingSecret) + "</div>"
            "<p class='note'>Open the Telegram bot, choose Connect, then enter the System ID and this secret. "
            "The secret changes every time pairing mode is started and the temporary AP closes after pairing or timeout.</p>";

        server.send(200, "text/html", pageShell("Smart Rack Pairing", body));
        return;
    }

    if (portalMode == PortalMode::WIFI_PROVISIONING) {
        const String body =
            "<h1>Wi-Fi Provisioning</h1>"
            "<p class='note'>Enter the Wi-Fi network that the Smart Drying Rack should use.</p>"
            "<form method='POST' action='/save'>"
            "<div class='label'>SSID</div><input name='ssid' required>"
            "<div class='label'>Password</div><input name='password' type='password'>"
            "<button type='submit'>Save and Connect</button>"
            "</form>";

        server.send(200, "text/html", pageShell("Smart Rack Wi-Fi", body));
        return;
    }

    server.send(404, "text/plain", "No local portal is active.");
}

static void saveProvision() {
    if (portalMode != PortalMode::WIFI_PROVISIONING) {
        server.send(404, "text/plain", "Wi-Fi provisioning is not active.");
        return;
    }

    String ssid = server.arg("ssid");
    String password = server.arg("password");

    if (ssid.isEmpty()) {
        server.send(400, "text/plain", "SSID required");
        return;
    }

    Storage::saveWifi(ssid, password);

    server.send(
        200,
        "text/html",
        pageShell(
            "Wi-Fi Saved",
            "<h1>Saved</h1><p class='note'>ESP32 will restart and connect to the new Wi-Fi.</p>"
        )
    );

    delay(1000);
    ESP.restart();
}

static void ensureWebServerStarted() {
    if (webServerStarted) return;

    server.on("/", HTTP_GET, rootPage);
    server.on("/pair", HTTP_GET, rootPage);
    server.on("/save", HTTP_POST, saveProvision);
    server.onNotFound(rootPage);
    server.begin();
    webServerStarted = true;
}

namespace WifiManager {

void begin() {
    pinMode(LED_WIFI, OUTPUT);
    setWifiLed(false);

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);

    if (Storage::getWifiSsid().isEmpty()) {
        startProvisioning();
    } else {
        reconnect();
    }
}

void reconnect() {
    String ssid = Storage::getWifiSsid();
    String password = Storage::getWifiPassword();

    if (ssid.isEmpty()) {
        startProvisioning();
        return;
    }

    // Do not tear down the pairing AP. WIFI_AP_STA keeps both interfaces alive.
    if (portalMode == PortalMode::PAIRING) {
        WiFi.mode(WIFI_AP_STA);
    } else {
        portalMode = PortalMode::NONE;
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
    }

    WiFi.begin(ssid.c_str(), password.c_str());
    lastAttempt = millis();

    Serial.printf("Connecting to Wi-Fi: %s\n", ssid.c_str());
}

void startProvisioning() {
    // Provisioning is only used when there is no usable station connection.
    // It intentionally uses AP-only mode.
    portalMode = PortalMode::WIFI_PROVISIONING;
    currentPairingSystemId = "";
    currentPairingSecret = "";
    currentPairingApName = "";

    String id = Storage::getDeviceId();
    String apName = "SmartRack-" + id.substring(max(0, (int)id.length() - 6));

    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apName.c_str());

    ensureWebServerStarted();

    Serial.println("Wi-Fi provisioning AP started.");
    Serial.print("SSID: ");
    Serial.println(apName);
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
}

bool startPairingAccessPoint(const String &systemId, const String &secret) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Cannot start pairing AP: station Wi-Fi is not connected.");
        return false;
    }

    // Preserve the station connection used by WebSocket while also enabling SoftAP.
    WiFi.mode(WIFI_AP_STA);

    currentPairingSystemId = systemId;
    currentPairingSecret = secret;
    currentPairingApName =
        "SmartRack-Pair-" + systemId.substring(max(0, (int)systemId.length() - 6));

    // Configure/reconfigure only the SoftAP side. Do not disconnect the station
    // interface because it carries the FastAPI WebSocket.
    if (!WiFi.softAP(currentPairingApName.c_str())) {
        Serial.println("Failed to start pairing SoftAP.");
        currentPairingApName = "";
        return false;
    }

    portalMode = PortalMode::PAIRING;
    pairingStartedAt = millis();
    ensureWebServerStarted();

    Serial.println();
    Serial.println("Pairing AP started without disconnecting station Wi-Fi/WebSocket.");
    Serial.print("Pairing SSID: ");
    Serial.println(currentPairingApName);
    Serial.print("Pairing page: http://");
    Serial.println(WiFi.softAPIP());
    Serial.print("Station IP remains: ");
    Serial.println(WiFi.localIP());

    return true;
}

void stopPairingAccessPoint() {
    if (portalMode != PortalMode::PAIRING) return;

    WiFi.softAPdisconnect(true);
    portalMode = PortalMode::NONE;
    currentPairingSystemId = "";
    currentPairingSecret = "";
    currentPairingApName = "";
    pairingStartedAt = 0;

    // Return to STA-only mode. The existing station connection is retained/re-established
    // automatically by the ESP32 Wi-Fi stack if needed.
    WiFi.mode(WIFI_STA);

    Serial.println("Pairing AP stopped; station/WebSocket operation continues.");
}

bool pairingAccessPointActive() {
    return portalMode == PortalMode::PAIRING;
}

String pairingAccessPointName() {
    return currentPairingApName;
}

String pairingAccessPointIp() {
    if (portalMode != PortalMode::PAIRING) return "";
    return WiFi.softAPIP().toString();
}

void loop() {
    if (portalMode != PortalMode::NONE) {
        server.handleClient();
    }

    if (portalMode == PortalMode::WIFI_PROVISIONING) {
        setWifiLed(millis() % 1000 < 500);
        return;
    }

    if (portalMode == PortalMode::PAIRING &&
        millis() - pairingStartedAt >= PAIRING_AP_TIMEOUT_MS) {
        Serial.println("Pairing AP timed out.");
        stopPairingAccessPoint();
    }

    if (WiFi.status() == WL_CONNECTED) {
        setWifiLed(true);
        return;
    }

    setWifiLed(millis() % 1000 < 500);

    if (millis() - lastAttempt >= WIFI_RETRY_INTERVAL_MS) {
        reconnect();
    }
}

bool connected() {
    // PAIRING uses AP+STA, therefore it is still considered connected whenever
    // the station interface is connected.
    return portalMode != PortalMode::WIFI_PROVISIONING &&
           WiFi.status() == WL_CONNECTED;
}

String ipAddress() {
    if (!connected()) return "";
    return WiFi.localIP().toString();
}

}
