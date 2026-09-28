#include <WiFi.h>
#include <WebServer.h>

// ======================================================
// NEXT ESP32 DIRECT HARDWARE BRIDGE
// Firmware 0.2.0
// ESP32 DevKit V1 + L298N + 4 TT Motors
// ======================================================

const char* WIFI_SSID = "Osama";
const char* WIFI_PASSWORD = "123456789";

const char* DEVICE_NAME = "NEXT-ESP32";
const char* FIRMWARE_VERSION = "0.2.0";

WebServer server(80);

// ======================================================
// L298N pins
// ======================================================

constexpr uint8_t PIN_ENA = 25;
constexpr uint8_t PIN_IN1 = 26;
constexpr uint8_t PIN_IN2 = 27;

constexpr uint8_t PIN_ENB = 33;
constexpr uint8_t PIN_IN3 = 32;
constexpr uint8_t PIN_IN4 = 23;

// إذا كانت جهة كاملة تدور بالعكس، غيّر false إلى true.
constexpr bool LEFT_INVERT  = false;
constexpr bool RIGHT_INVERT = false;

// PWM
constexpr uint32_t PWM_FREQ = 18000;
constexpr uint8_t PWM_RESOLUTION = 8;

// نبدأ بحد آمن منخفض نسبيًا.
// المجال الكامل 0..255، لكننا لا نسمح بأكثر من 180 الآن.
constexpr int MAX_SAFE_PWM = 180;

// Dead-man timeout:
// إذا لم يصل أمر حركة جديد خلال TTL تتوقف المحركات تلقائيًا.
constexpr unsigned long DEFAULT_TTL_MS = 700;
constexpr unsigned long MIN_TTL_MS = 100;
constexpr unsigned long MAX_TTL_MS = 1200;

// ======================================================
// Runtime state
// ======================================================

bool armed = false;
bool estop = false;
bool motionActive = false;

int currentLeftPwm = 0;
int currentRightPwm = 0;

unsigned long lastDriveAt = 0;
unsigned long activeTtlMs = DEFAULT_TTL_MS;

String lastCommand = "BOOT_STOP";

// ======================================================
// Helpers
// ======================================================

String boolJson(bool value) {
  return value ? "true" : "false";
}

void sendJson(int status, const String& body) {
  server.send(status, "application/json", body);
}

void setOneMotorSide(
  uint8_t enablePin,
  uint8_t inA,
  uint8_t inB,
  int value,
  bool invert
) {
  value = constrain(value, -MAX_SAFE_PWM, MAX_SAFE_PWM);

  if (invert) {
    value = -value;
  }

  if (value > 0) {
    digitalWrite(inA, HIGH);
    digitalWrite(inB, LOW);
    ledcWrite(enablePin, value);
  }
  else if (value < 0) {
    digitalWrite(inA, LOW);
    digitalWrite(inB, HIGH);
    ledcWrite(enablePin, -value);
  }
  else {
    digitalWrite(inA, LOW);
    digitalWrite(inB, LOW);
    ledcWrite(enablePin, 0);
  }
}

void applyDrive(int left, int right) {
  left = constrain(left, -MAX_SAFE_PWM, MAX_SAFE_PWM);
  right = constrain(right, -MAX_SAFE_PWM, MAX_SAFE_PWM);

  setOneMotorSide(
    PIN_ENA,
    PIN_IN1,
    PIN_IN2,
    left,
    LEFT_INVERT
  );

  setOneMotorSide(
    PIN_ENB,
    PIN_IN3,
    PIN_IN4,
    right,
    RIGHT_INVERT
  );

  currentLeftPwm = left;
  currentRightPwm = right;

  motionActive = (left != 0 || right != 0);
}

void stopMotors(const String& reason) {
  applyDrive(0, 0);

  motionActive = false;
  currentLeftPwm = 0;
  currentRightPwm = 0;

  lastCommand = reason;

  Serial.print("[MOTOR] STOP: ");
  Serial.println(reason);
}

unsigned long ttlRemaining() {
  if (!motionActive) {
    return 0;
  }

  unsigned long elapsed = millis() - lastDriveAt;

  if (elapsed >= activeTtlMs) {
    return 0;
  }

  return activeTtlMs - elapsed;
}

// ======================================================
// HTTP GET /
// ======================================================

void handleRoot() {
  String json = "{";
  json += "\"ok\":true,";
  json += "\"device\":\"" + String(DEVICE_NAME) + "\",";
  json += "\"firmware\":\"" + String(FIRMWARE_VERSION) + "\",";
  json += "\"message\":\"NEXT ESP32 motor bridge online\"";
  json += "}";

  sendJson(200, json);
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

  sendJson(200, json);
}

// ======================================================
// GET /state
// ======================================================

void handleState() {
  bool wifiConnected = WiFi.status() == WL_CONNECTED;

  String json = "{";

  json += "\"ok\":true,";
  json += "\"device\":\"" + String(DEVICE_NAME) + "\",";
  json += "\"firmware\":\"" + String(FIRMWARE_VERSION) + "\",";

  json += "\"wifiConnected\":" + boolJson(wifiConnected) + ",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";

  json += "\"armed\":" + boolJson(armed) + ",";
  json += "\"estop\":" + boolJson(estop) + ",";

  json += "\"motion\":\"";
  json += motionActive ? "moving" : "stopped";
  json += "\",";

  json += "\"leftPwm\":" + String(currentLeftPwm) + ",";
  json += "\"rightPwm\":" + String(currentRightPwm) + ",";

  json += "\"ttlRemainingMs\":" + String(ttlRemaining()) + ",";
  json += "\"lastCommand\":\"" + lastCommand + "\",";

  json += "\"uptimeMs\":" + String(millis());

  json += "}";

  sendJson(200, json);
}

// ======================================================
// POST /arm
// ======================================================

void handleArm() {
  if (estop) {
    sendJson(
      423,
      "{\"ok\":false,\"error\":\"ESTOP_ACTIVE\"}"
    );
    return;
  }

  stopMotors("ARM_STOP");

  armed = true;

  Serial.println("[SAFETY] ARMED");

  sendJson(
    200,
    "{\"ok\":true,\"armed\":true,\"motion\":\"stopped\"}"
  );
}

// ======================================================
// POST /disarm
// ======================================================

void handleDisarm() {
  stopMotors("DISARM");

  armed = false;

  Serial.println("[SAFETY] DISARMED");

  sendJson(
    200,
    "{\"ok\":true,\"armed\":false,\"motion\":\"stopped\"}"
  );
}

// ======================================================
// POST /stop
// ======================================================

void handleStop() {
  stopMotors("STOP");

  sendJson(
    200,
    "{\"ok\":true,\"motion\":\"stopped\"}"
  );
}

// ======================================================
// POST /estop
// ======================================================

void handleEstop() {
  stopMotors("ESTOP");

  estop = true;
  armed = false;

  Serial.println("[SAFETY] ESTOP ACTIVE");

  sendJson(
    200,
    "{\"ok\":true,\"estop\":true,\"armed\":false,\"motion\":\"stopped\"}"
  );
}

// ======================================================
// POST /estop/clear
// ======================================================

void handleEstopClear() {
  stopMotors("ESTOP_CLEAR");

  estop = false;
  armed = false;

  Serial.println("[SAFETY] ESTOP CLEARED");

  sendJson(
    200,
    "{\"ok\":true,\"estop\":false,\"armed\":false,\"motion\":\"stopped\"}"
  );
}

// ======================================================
// POST /drive?left=90&right=90&ttl=700
// ======================================================

void handleDrive() {
  if (estop) {
    sendJson(
      423,
      "{\"ok\":false,\"error\":\"ESTOP_ACTIVE\"}"
    );
    return;
  }

  if (!armed) {
    sendJson(
      409,
      "{\"ok\":false,\"error\":\"ROBOT_NOT_ARMED\"}"
    );
    return;
  }

  if (!server.hasArg("left") || !server.hasArg("right")) {
    sendJson(
      400,
      "{\"ok\":false,\"error\":\"LEFT_RIGHT_REQUIRED\"}"
    );
    return;
  }

  int left = server.arg("left").toInt();
  int right = server.arg("right").toInt();

  left = constrain(left, -MAX_SAFE_PWM, MAX_SAFE_PWM);
  right = constrain(right, -MAX_SAFE_PWM, MAX_SAFE_PWM);

  unsigned long ttl = DEFAULT_TTL_MS;

  if (server.hasArg("ttl")) {
    long requested = server.arg("ttl").toInt();

    if (requested < (long)MIN_TTL_MS) {
      requested = MIN_TTL_MS;
    }

    if (requested > (long)MAX_TTL_MS) {
      requested = MAX_TTL_MS;
    }

    ttl = (unsigned long)requested;
  }

  activeTtlMs = ttl;
  lastDriveAt = millis();

  applyDrive(left, right);

  if (left == 0 && right == 0) {
    lastCommand = "DRIVE_ZERO";
  } else {
    lastCommand = "DRIVE";
  }

  String json = "{";
  json += "\"ok\":true,";
  json += "\"left\":" + String(left) + ",";
  json += "\"right\":" + String(right) + ",";
  json += "\"ttlMs\":" + String(ttl);
  json += "}";

  sendJson(200, json);
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

  sendJson(404, json);
}

// ======================================================
// Wi-Fi
// ======================================================

void connectWiFi() {
  stopMotors("WIFI_CONNECT");

  armed = false;

  Serial.println();
  Serial.println("======================================");
  Serial.println("NEXT ESP32 MOTOR BRIDGE");
  Serial.println("Connecting to Wi-Fi...");
  Serial.println("======================================");

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startedAt = millis();

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");

    if (millis() - startedAt > 30000) {
      Serial.println();
      Serial.println("[WIFI] Timeout. Retrying...");

      WiFi.disconnect();
      delay(1000);

      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      startedAt = millis();
    }
  }

  Serial.println();
  Serial.println("[WIFI] CONNECTED");

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

  pinMode(PIN_IN1, OUTPUT);
  pinMode(PIN_IN2, OUTPUT);
  pinMode(PIN_IN3, OUTPUT);
  pinMode(PIN_IN4, OUTPUT);

  ledcAttach(
    PIN_ENA,
    PWM_FREQ,
    PWM_RESOLUTION
  );

  ledcAttach(
    PIN_ENB,
    PWM_FREQ,
    PWM_RESOLUTION
  );

  stopMotors("BOOT");

  delay(500);

  connectWiFi();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/ping", HTTP_GET, handlePing);
  server.on("/state", HTTP_GET, handleState);

  server.on("/arm", HTTP_POST, handleArm);
  server.on("/disarm", HTTP_POST, handleDisarm);
  server.on("/stop", HTTP_POST, handleStop);

  server.on("/estop", HTTP_POST, handleEstop);
  server.on("/estop/clear", HTTP_POST, handleEstopClear);

  server.on("/drive", HTTP_POST, handleDrive);

  server.onNotFound(handleNotFound);

  server.begin();

  Serial.println();
  Serial.println("[HTTP] Server started");

  Serial.println("[HTTP] GET  /ping");
  Serial.println("[HTTP] GET  /state");
  Serial.println("[HTTP] POST /arm");
  Serial.println("[HTTP] POST /disarm");
  Serial.println("[HTTP] POST /stop");
  Serial.println("[HTTP] POST /drive");
  Serial.println("[HTTP] POST /estop");
  Serial.println("[HTTP] POST /estop/clear");

  Serial.print("[NEXT] http://");
  Serial.print(WiFi.localIP());
  Serial.println("/state");
}

// ======================================================
// Loop
// ======================================================

void loop() {
  if (
    motionActive &&
    millis() - lastDriveAt >= activeTtlMs
  ) {
    stopMotors("COMMAND_TTL_EXPIRED");
  }

  if (WiFi.status() != WL_CONNECTED) {
    stopMotors("WIFI_LOST");

    armed = false;

    Serial.println("[WIFI] Connection lost.");

    connectWiFi();
  }

  server.handleClient();

  delay(2);
}
