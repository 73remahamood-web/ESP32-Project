#include <WiFi.h>
#include <WebServer.h>

// ============================================================
// NES ESP32-CAM MOTOR CONTROLLER
// No Camera
// No ENA / ENB control
// L298N ENA + ENB jumpers must stay installed
// ============================================================

#define FW_VERSION "0.3.0-ESP32CAM-DIRECTION"
#define DEVICE_NAME "NEXT-ESP32-CAM"

// ---------------- Wi-Fi ----------------

const char* WIFI_SSID = "Osama";
const char* WIFI_PASSWORD = "123456789";

// ---------------- L298N ----------------
//
// LEFT:
// IN1 = GPIO13
// IN2 = GPIO14
//
// RIGHT:
// IN3 = GPIO15
// IN4 = GPIO2
//
// ENA and ENB:
// Do NOT connect them to ESP32-CAM.
// Keep their jumpers installed on L298N.
//

constexpr uint8_t LEFT_IN1  = 13;
constexpr uint8_t LEFT_IN2  = 14;

constexpr uint8_t RIGHT_IN1 = 15;
constexpr uint8_t RIGHT_IN2 = 2;

// ---------------- Safety ----------------

constexpr uint32_t DEFAULT_TTL_MS = 700;
constexpr uint32_t MIN_TTL_MS = 200;
constexpr uint32_t MAX_TTL_MS = 1200;

WebServer server(80);

bool armed = false;
bool estop = false;

int currentLeft = 0;
int currentRight = 0;

uint32_t driveDeadlineMs = 0;

bool wifiWasConnected = false;

// ============================================================
// Helpers
// ============================================================

void addCors() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void motorStopRaw() {
  digitalWrite(LEFT_IN1, LOW);
  digitalWrite(LEFT_IN2, LOW);

  digitalWrite(RIGHT_IN1, LOW);
  digitalWrite(RIGHT_IN2, LOW);

  currentLeft = 0;
  currentRight = 0;
  driveDeadlineMs = 0;
}

void setLeftMotor(int value) {
  if (value > 0) {
    digitalWrite(LEFT_IN1, HIGH);
    digitalWrite(LEFT_IN2, LOW);
  }
  else if (value < 0) {
    digitalWrite(LEFT_IN1, LOW);
    digitalWrite(LEFT_IN2, HIGH);
  }
  else {
    digitalWrite(LEFT_IN1, LOW);
    digitalWrite(LEFT_IN2, LOW);
  }
}

void setRightMotor(int value) {
  if (value > 0) {
    digitalWrite(RIGHT_IN1, HIGH);
    digitalWrite(RIGHT_IN2, LOW);
  }
  else if (value < 0) {
    digitalWrite(RIGHT_IN1, LOW);
    digitalWrite(RIGHT_IN2, HIGH);
  }
  else {
    digitalWrite(RIGHT_IN1, LOW);
    digitalWrite(RIGHT_IN2, LOW);
  }
}

void applyDrive(int left, int right, uint32_t ttl) {

  if (!armed || estop) {
    motorStopRaw();
    return;
  }

  left = constrain(left, -100, 100);
  right = constrain(right, -100, 100);

  ttl = constrain(ttl, MIN_TTL_MS, MAX_TTL_MS);

  setLeftMotor(left);
  setRightMotor(right);

  currentLeft = left;
  currentRight = right;

  if (left == 0 && right == 0) {
    driveDeadlineMs = 0;
  } else {
    driveDeadlineMs = millis() + ttl;
  }
}

String boolJson(bool value) {
  return value ? "true" : "false";
}

String stateJson() {

  String ip = "0.0.0.0";

  if (WiFi.status() == WL_CONNECTED) {
    ip = WiFi.localIP().toString();
  }

  String json = "{";

  json += "\"device\":\"";
  json += DEVICE_NAME;
  json += "\",";

  json += "\"firmware\":\"";
  json += FW_VERSION;
  json += "\",";

  json += "\"board\":\"ESP32-CAM AI-Thinker\",";

  json += "\"cameraEnabled\":false,";

  json += "\"motorMode\":\"direction-only\",";

  json += "\"enaControlled\":false,";
  json += "\"enbControlled\":false,";

  json += "\"armed\":";
  json += boolJson(armed);
  json += ",";

  json += "\"estop\":";
  json += boolJson(estop);
  json += ",";

  json += "\"left\":";
  json += String(currentLeft);
  json += ",";

  json += "\"right\":";
  json += String(currentRight);
  json += ",";

  json += "\"wifi\":";
  json += boolJson(WiFi.status() == WL_CONNECTED);
  json += ",";

  json += "\"ssid\":\"";
  json += WIFI_SSID;
  json += "\",";

  json += "\"ip\":\"";
  json += ip;
  json += "\",";

  json += "\"rssi\":";

  if (WiFi.status() == WL_CONNECTED) {
    json += String(WiFi.RSSI());
  } else {
    json += "null";
  }

  json += ",";

  json += "\"pins\":{";
  json += "\"leftIn1\":13,";
  json += "\"leftIn2\":14,";
  json += "\"rightIn1\":15,";
  json += "\"rightIn2\":2";
  json += "}";

  json += "}";

  return json;
}

void sendJson(int code, const String& body) {
  addCors();
  server.send(code, "application/json; charset=utf-8", body);
}

void sendOk(const String& action) {

  String json = "{";
  json += "\"ok\":true,";
  json += "\"action\":\"";
  json += action;
  json += "\",";
  json += "\"state\":";
  json += stateJson();
  json += "}";

  sendJson(200, json);
}

void sendError(int code, const String& error) {

  String json = "{";
  json += "\"ok\":false,";
  json += "\"error\":\"";
  json += error;
  json += "\"";
  json += "}";

  sendJson(code, json);
}

// ============================================================
// HTTP routes
// ============================================================

void setupRoutes() {

  server.on("/", HTTP_GET, []() {

    String text =
      "NES ESP32-CAM Motor Controller\n"
      "Firmware: " FW_VERSION "\n"
      "Camera: disabled\n"
      "Motor mode: direction only\n";

    addCors();
    server.send(200, "text/plain; charset=utf-8", text);
  });

  server.on("/ping", HTTP_GET, []() {

    String json = "{";
    json += "\"ok\":true,";
    json += "\"device\":\"";
    json += DEVICE_NAME;
    json += "\",";
    json += "\"firmware\":\"";
    json += FW_VERSION;
    json += "\"";
    json += "}";

    sendJson(200, json);
  });

  server.on("/state", HTTP_GET, []() {
    sendJson(200, stateJson());
  });

  server.on("/arm", HTTP_ANY, []() {

    if (estop) {
      sendError(409, "ESTOP_ACTIVE");
      return;
    }

    motorStopRaw();
    armed = true;

    sendOk("arm");
  });

  server.on("/disarm", HTTP_ANY, []() {

    motorStopRaw();
    armed = false;

    sendOk("disarm");
  });

  server.on("/stop", HTTP_ANY, []() {

    motorStopRaw();

    sendOk("stop");
  });

  server.on("/estop", HTTP_ANY, []() {

    motorStopRaw();

    estop = true;
    armed = false;

    sendOk("estop");
  });

  server.on("/estop/clear", HTTP_ANY, []() {

    motorStopRaw();

    estop = false;
    armed = false;

    sendOk("estop-clear");
  });

  server.on("/drive", HTTP_ANY, []() {

    if (estop) {
      sendError(409, "ESTOP_ACTIVE");
      return;
    }

    if (!armed) {
      sendError(409, "NOT_ARMED");
      return;
    }

    if (!server.hasArg("left") || !server.hasArg("right")) {
      sendError(
        400,
        "left and right parameters are required"
      );
      return;
    }

    int left = server.arg("left").toInt();
    int right = server.arg("right").toInt();

    uint32_t ttl = DEFAULT_TTL_MS;

    if (server.hasArg("ttl")) {
      long requested = server.arg("ttl").toInt();

      if (requested > 0) {
        ttl = constrain(
          requested,
          (long)MIN_TTL_MS,
          (long)MAX_TTL_MS
        );
      }
    }

    applyDrive(left, right, ttl);

    sendOk("drive");
  });

  server.onNotFound([]() {

    if (server.method() == HTTP_OPTIONS) {
      addCors();
      server.send(204);
      return;
    }

    sendError(404, "NOT_FOUND");
  });
}

// ============================================================
// Wi-Fi
// ============================================================

void connectWifi() {

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);

  uint32_t lastPrint = 0;

  while (WiFi.status() != WL_CONNECTED) {

    delay(100);

    if (millis() - lastPrint >= 500) {
      lastPrint = millis();
      Serial.print(".");
    }
  }

  wifiWasConnected = true;

  Serial.println();
  Serial.println("Wi-Fi connected");

  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

// ============================================================
// Setup
// ============================================================

void setup() {

  // Motors MUST be safe immediately.
  pinMode(LEFT_IN1, OUTPUT);
  pinMode(LEFT_IN2, OUTPUT);

  pinMode(RIGHT_IN1, OUTPUT);
  pinMode(RIGHT_IN2, OUTPUT);

  motorStopRaw();

  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("==============================");
  Serial.println("NES ESP32-CAM Motor Controller");
  Serial.println(FW_VERSION);
  Serial.println("==============================");

  Serial.println("Camera: DISABLED");
  Serial.println("ENA/ENB GPIO: NOT USED");

  connectWifi();

  setupRoutes();

  server.begin();

  Serial.println("HTTP server ready");

  Serial.print("Open: http://");
  Serial.println(WiFi.localIP());
}

// ============================================================
// Loop
// ============================================================

void loop() {

  server.handleClient();

  // ----------------------------------------------------------
  // Deadman
  // ----------------------------------------------------------

  if (
    driveDeadlineMs != 0 &&
    (int32_t)(millis() - driveDeadlineMs) >= 0
  ) {
    motorStopRaw();
  }

  // ----------------------------------------------------------
  // Wi-Fi safety
  // ----------------------------------------------------------

  bool connected = WiFi.status() == WL_CONNECTED;

  if (!connected && wifiWasConnected) {

    motorStopRaw();

    armed = false;

    wifiWasConnected = false;

    Serial.println("Wi-Fi lost -> motors stopped");
  }

  if (connected && !wifiWasConnected) {

    wifiWasConnected = true;

    Serial.print("Wi-Fi restored: ");
    Serial.println(WiFi.localIP());
  }

  delay(2);
}
