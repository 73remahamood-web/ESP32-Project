#include <WiFi.h>
#include <WebServer.h>

// ======================================================
// NEXT ESP32-CAM DIRECT MOTOR BRIDGE
// Firmware 0.3.0
// AI-Thinker ESP32-CAM + L298N
//
// لا كاميرا
// لا microSD
// لا توصيل ESP32 إلى ENA/ENB
//
// ENA + ENB jumpers remain installed on L298N.
//
// Speed control is performed directly on IN1..IN4 using PWM.
// ======================================================

const char* WIFI_SSID = "Osama";
const char* WIFI_PASSWORD = "__WIFI_PASSWORD__";

const char* DEVICE_NAME = "NEXT-ESP32CAM-MOTOR";
const char* FIRMWARE_VERSION = "0.3.0";

WebServer server(80);

// ======================================================
// L298N
// ======================================================
//
// LEFT:
// GPIO13 -> IN1
// GPIO14 -> IN2
//
// RIGHT:
// GPIO15 -> IN3
// GPIO2  -> IN4
//
// ENA / ENB:
// Jumpers installed, no wires to ESP32-CAM.
// ======================================================

constexpr uint8_t PIN_IN1 = 13;
constexpr uint8_t PIN_IN2 = 14;
constexpr uint8_t PIN_IN3 = 15;
constexpr uint8_t PIN_IN4 = 2;

constexpr bool LEFT_INVERT  = false;
constexpr bool RIGHT_INVERT = false;

// PWM directly on direction pins.
constexpr uint32_t PWM_FREQ = 12000;
constexpr uint8_t PWM_RESOLUTION = 8;

// 0..255 possible.
// Start conservatively.
constexpr int MAX_SAFE_PWM = 180;

constexpr unsigned long DEFAULT_TTL_MS = 700;
constexpr unsigned long MIN_TTL_MS = 100;
constexpr unsigned long MAX_TTL_MS = 1200;

// ======================================================
// Runtime
// ======================================================

bool armed = false;
bool estop = false;
bool motionActive = false;

int currentLeftPwm = 0;
int currentRightPwm = 0;

unsigned long lastDriveAt = 0;
unsigned long activeTtlMs = DEFAULT_TTL_MS;

String lastCommand = "BOOT";

// ======================================================
// Helpers
// ======================================================

String boolJson(bool value) {
  return value ? "true" : "false";
}

void sendJson(int status, const String& body) {
  server.send(status, "application/json", body);
}

void allMotorPinsOff() {
  ledcWrite(PIN_IN1, 0);
  ledcWrite(PIN_IN2, 0);
  ledcWrite(PIN_IN3, 0);
  ledcWrite(PIN_IN4, 0);
}

// value:
//  +PWM = forward
//  -PWM = reverse
//     0 = stop
//
// Because ENA/ENB are permanently enabled by jumper,
// PWM is applied to the active direction input itself.
void setMotorSide(
  uint8_t pinForward,
  uint8_t pinReverse,
  int value,
  bool invert
) {
  value = constrain(value, -MAX_SAFE_PWM, MAX_SAFE_PWM);

  if (invert) {
    value = -value;
  }

  if (value > 0) {
    ledcWrite(pinReverse, 0);
    ledcWrite(pinForward, value);
  }
  else if (value < 0) {
    ledcWrite(pinForward, 0);
    ledcWrite(pinReverse, -value);
  }
  else {
    ledcWrite(pinForward, 0);
    ledcWrite(pinReverse, 0);
  }
}

void applyDrive(int left, int right) {
  left = constrain(left, -MAX_SAFE_PWM, MAX_SAFE_PWM);
  right = constrain(right, -MAX_SAFE_PWM, MAX_SAFE_PWM);

  setMotorSide(
    PIN_IN1,
    PIN_IN2,
    left,
    LEFT_INVERT
  );

  setMotorSide(
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
  allMotorPinsOff();

  currentLeftPwm = 0;
  currentRightPwm = 0;
  motionActive = false;

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
// GET /
// ======================================================

void handleRoot() {
  String json = "{";
  json += "\"ok\":true,";
  json += "\"device\":\"" + String(DEVICE_NAME) + "\",";
  json += "\"firmware\":\"" + String(FIRMWARE_VERSION) + "\",";
  json += "\"board\":\"AI-Thinker ESP32-CAM\",";
  json += "\"camera\":false,";
  json += "\"motorControl\":\"direction-pin-pwm\",";
  json += "\"enablePins\":\"L298N jumpers\"";
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
  json += "\"board\":\"esp32cam\",";
  json += "\"mode\":\"hardware\",";
  json += "\"uptimeMs\":" + String(millis());
  json += "}";

  sendJson(200, json);
}

// ======================================================
// GET /state
// ======================================================

void handleState() {
  bool wifiConnected =
    WiFi.status() == WL_CONNECTED;

  String json = "{";

  json += "\"ok\":true,";
  json += "\"device\":\"" + String(DEVICE_NAME) + "\",";
  json += "\"firmware\":\"" + String(FIRMWARE_VERSION) + "\",";

  json += "\"wifiConnected\":" +
    boolJson(wifiConnected) + ",";

  json += "\"ip\":\"" +
    WiFi.localIP().toString() + "\",";

  json += "\"rssi\":" +
    String(WiFi.RSSI()) + ",";

  json += "\"armed\":" +
    boolJson(armed) + ",";

  json += "\"estop\":" +
    boolJson(estop) + ",";

  json += "\"motion\":\"";
  json += motionActive ? "moving" : "stopped";
  json += "\",";

  json += "\"leftPwm\":" +
    String(currentLeftPwm) + ",";

  json += "\"rightPwm\":" +
    String(currentRightPwm) + ",";

  json += "\"ttlRemainingMs\":" +
    String(ttlRemaining()) + ",";

  json += "\"lastCommand\":\"" +
    lastCommand + "\",";

  json += "\"pins\":{";
  json += "\"in1\":13,";
  json += "\"in2\":14,";
  json += "\"in3\":15,";
  json += "\"in4\":2";
  json += "},";

  json += "\"enablePins\":\"jumpers\",";
  json += "\"cameraEnabled\":false,";
  json += "\"uptimeMs\":" +
    String(millis());

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
// POST /drive?left=80&right=80&ttl=500
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

  if (
    !server.hasArg("left") ||
    !server.hasArg("right")
  ) {
    sendJson(
      400,
      "{\"ok\":false,\"error\":\"LEFT_RIGHT_REQUIRED\"}"
    );
    return;
  }

  int left = server.arg("left").toInt();
  int right = server.arg("right").toInt();

  left = constrain(
    left,
    -MAX_SAFE_PWM,
    MAX_SAFE_PWM
  );

  right = constrain(
    right,
    -MAX_SAFE_PWM,
    MAX_SAFE_PWM
  );

  unsigned long ttl =
    DEFAULT_TTL_MS;

  if (server.hasArg("ttl")) {
    long requested =
      server.arg("ttl").toInt();

    requested = constrain(
      requested,
      (long)MIN_TTL_MS,
      (long)MAX_TTL_MS
    );

    ttl =
      (unsigned long)requested;
  }

  activeTtlMs = ttl;
  lastDriveAt = millis();

  applyDrive(left, right);

  lastCommand =
    (left == 0 && right == 0)
      ? "DRIVE_ZERO"
      : "DRIVE";

  String json = "{";

  json += "\"ok\":true,";
  json += "\"left\":" +
    String(left) + ",";
  json += "\"right\":" +
    String(right) + ",";
  json += "\"ttlMs\":" +
    String(ttl);

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
  json += "\"path\":\"" +
    server.uri() + "\"";

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
  Serial.println("========================================");
  Serial.println("NEXT ESP32-CAM MOTOR BRIDGE");
  Serial.println("Camera disabled");
  Serial.println("ENA/ENB controlled by L298N jumpers");
  Serial.println("Connecting to Wi-Fi...");
  Serial.println("========================================");

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setHostname(DEVICE_NAME);

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  unsigned long startedAt =
    millis();

  while (
    WiFi.status() != WL_CONNECTED
  ) {
    delay(500);
    Serial.print(".");

    if (
      millis() - startedAt >
      30000
    ) {
      Serial.println();
      Serial.println(
        "[WIFI] Timeout. Retrying..."
      );

      WiFi.disconnect();

      delay(1000);

      WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
      );

      startedAt =
        millis();
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
// Motor setup
// ======================================================

void setupMotors() {
  pinMode(PIN_IN1, OUTPUT);
  pinMode(PIN_IN2, OUTPUT);
  pinMode(PIN_IN3, OUTPUT);
  pinMode(PIN_IN4, OUTPUT);

  bool ok1 = ledcAttach(
    PIN_IN1,
    PWM_FREQ,
    PWM_RESOLUTION
  );

  bool ok2 = ledcAttach(
    PIN_IN2,
    PWM_FREQ,
    PWM_RESOLUTION
  );

  bool ok3 = ledcAttach(
    PIN_IN3,
    PWM_FREQ,
    PWM_RESOLUTION
  );

  bool ok4 = ledcAttach(
    PIN_IN4,
    PWM_FREQ,
    PWM_RESOLUTION
  );

  allMotorPinsOff();

  if (!(ok1 && ok2 && ok3 && ok4)) {
    Serial.println(
      "[FATAL] PWM channel allocation failed."
    );

    while (true) {
      allMotorPinsOff();
      delay(1000);
    }
  }

  Serial.println("[MOTOR] GPIO13 -> IN1");
  Serial.println("[MOTOR] GPIO14 -> IN2");
  Serial.println("[MOTOR] GPIO15 -> IN3");
  Serial.println("[MOTOR] GPIO2  -> IN4");
  Serial.println("[MOTOR] ENA/ENB -> jumpers");
}

// ======================================================
// Setup
// ======================================================

void setup() {
  Serial.begin(115200);

  delay(200);

  setupMotors();

  stopMotors("BOOT");

  delay(500);

  connectWiFi();

  server.on(
    "/",
    HTTP_GET,
    handleRoot
  );

  server.on(
    "/ping",
    HTTP_GET,
    handlePing
  );

  server.on(
    "/state",
    HTTP_GET,
    handleState
  );

  server.on(
    "/arm",
    HTTP_POST,
    handleArm
  );

  server.on(
    "/disarm",
    HTTP_POST,
    handleDisarm
  );

  server.on(
    "/stop",
    HTTP_POST,
    handleStop
  );

  server.on(
    "/estop",
    HTTP_POST,
    handleEstop
  );

  server.on(
    "/estop/clear",
    HTTP_POST,
    handleEstopClear
  );

  server.on(
    "/drive",
    HTTP_POST,
    handleDrive
  );

  server.onNotFound(
    handleNotFound
  );

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
  // Dead-man safety.
  if (
    motionActive &&
    millis() - lastDriveAt >= activeTtlMs
  ) {
    stopMotors(
      "COMMAND_TTL_EXPIRED"
    );
  }

  if (
    WiFi.status() != WL_CONNECTED
  ) {
    stopMotors("WIFI_LOST");

    armed = false;

    Serial.println(
      "[WIFI] Connection lost."
    );

    connectWiFi();
  }

  server.handleClient();

  delay(2);
}
