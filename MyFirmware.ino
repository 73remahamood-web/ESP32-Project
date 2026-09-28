#include <WiFi.h>
#include <WebServer.h>

// ======================================================
// NEXT ESP32 DIRECT HARDWARE BRIDGE
// Stage 1: Wi-Fi + Ping + State only
// Board: ESP32 DevKit V1 / ESP-WROOM-32
// ======================================================

// ضع بيانات شبكة الـ Hotspot هنا
const char* WIFI_SSID = "Osama";
const char* WIFI_PASSWORD = "123456789";

// HTTP server
WebServer server(80);

// معلومات الجهاز
const char* DEVICE_NAME = "NEXT-ESP32";
const char* FIRMWARE_VERSION = "0.1.0";

// ======================================================
// JSON Helpers
// ======================================================

String jsonBool(bool value) {
  return value ? "true" : "false";
}

// ======================================================
// GET /
// ======================================================

void handleRoot() {
  String json = "{";
  json += "\"ok\":true,";
  json += "\"device\":\"" + String(DEVICE_NAME) + "\",";
  json += "\"firmware\":\"" + String(FIRMWARE_VERSION) + "\",";
  json += "\"message\":\"NEXT ESP32 hardware bridge is online\"";
  json += "}";

  server.send(200, "application/json", json);
}

// ======================================================
// GET /ping
// ======================================================

void handlePing() {
  String json = "{";
  json += "\"ok\":true,";
  json += "\"device\":\"" + String(DEVICE_NAME) + "\",";
  json += "\"firmware\":\"" + String(FIRMWARE_VERSION) + "\",";
  json += "\"mode\":\"hardware\",";
  json += "\"uptimeMs\":" + String(millis());
  json += "}";

  server.send(200, "application/json", json);
}

// ======================================================
// GET /state
// ======================================================

void handleState() {
  bool wifiConnected = (WiFi.status() == WL_CONNECTED);

  String json = "{";
  json += "\"ok\":true,";
  json += "\"device\":\"" + String(DEVICE_NAME) + "\",";
  json += "\"firmware\":\"" + String(FIRMWARE_VERSION) + "\",";
  json += "\"wifiConnected\":" + jsonBool(wifiConnected) + ",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"armed\":false,";
  json += "\"estop\":false,";
  json += "\"motors\":\"disconnected\",";
  json += "\"uptimeMs\":" + String(millis());
  json += "}";

  server.send(200, "application/json", json);
}

// ======================================================
// 404
// ======================================================

void handleNotFound() {
  String json = "{";
  json += "\"ok\":false,";
  json += "\"error\":\"NOT_FOUND\",";
  json += "\"path\":\"" + server.uri() + "\"";
  json += "}";

  server.send(404, "application/json", json);
}

// ======================================================
// Wi-Fi
// ======================================================

void connectWiFi() {
  Serial.println();
  Serial.println("======================================");
  Serial.println("NEXT ESP32");
  Serial.println("Connecting to Wi-Fi...");
  Serial.println("======================================");

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startedAt = millis();

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");

    // بعد 30 ثانية أعد المحاولة
    if (millis() - startedAt > 30000) {
      Serial.println();
      Serial.println("[WIFI] Connection timeout. Retrying...");

      WiFi.disconnect();
      delay(1000);

      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      startedAt = millis();
    }
  }

  Serial.println();
  Serial.println("[WIFI] CONNECTED");
  Serial.print("[WIFI] SSID: ");
  Serial.println(WiFi.SSID());

  Serial.print("[WIFI] IP: ");
  Serial.println(WiFi.localIP());

  Serial.print("[WIFI] RSSI: ");
  Serial.println(WiFi.RSSI());
}

// ======================================================
// Setup
// ======================================================

void setup() {
  Serial.begin(115200);

  delay(800);

  connectWiFi();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/ping", HTTP_GET, handlePing);
  server.on("/state", HTTP_GET, handleState);

  server.onNotFound(handleNotFound);

  server.begin();

  Serial.println();
  Serial.println("[HTTP] Server started");
  Serial.println("[HTTP] Available endpoints:");
  Serial.println("       GET /");
  Serial.println("       GET /ping");
  Serial.println("       GET /state");
  Serial.println();

  Serial.print("[NEXT] Open: http://");
  Serial.print(WiFi.localIP());
  Serial.println("/ping");
}

// ======================================================
// Loop
// ======================================================

void loop() {
  // إذا انقطعت الشبكة حاول استعادتها
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WIFI] Connection lost.");

    connectWiFi();
  }

  server.handleClient();

  delay(2);
}
