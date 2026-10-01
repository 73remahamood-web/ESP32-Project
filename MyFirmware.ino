#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

// ============================================================
// NES NEXT ESP32-CAM DYNAMIC HARDWARE BRIDGE
// Firmware 0.4.0
//
// Board:
//   AI-Thinker ESP32-CAM
//
// Main goals:
//   - NO fixed motor GPIO mapping.
//   - Motor GPIO assignment configurable over Wi-Fi.
//   - Configuration persisted in ESP32 NVS.
//   - Pin registry for the complete exposed board headers.
//   - Motor pin validation.
//   - Dynamic motor diagnostics.
//   - Dead-man TTL.
//   - STOP / E-STOP.
//   - No camera or microSD initialized in this firmware.
//   - Future-ready GPIO registry.
//
// IMPORTANT:
//   On first boot this firmware DOES NOT guess motor pins.
//   Configure IN1/IN2/IN3/IN4 through /config/motors.
// ============================================================


// ============================================================
// NETWORK
// ============================================================

const char* WIFI_SSID = "Osama";

// GitHub Actions replaces this from the existing secret.
const char* WIFI_PASSWORD = "__WIFI_PASSWORD__";

const char* DEVICE_NAME = "NEXT-ESP32CAM-MOTOR";
const char* FIRMWARE_VERSION = "0.4.0";

WebServer server(80);
Preferences prefs;


// ============================================================
// COMPLETE ESP32-CAM HEADER / PIN CATALOG
// ============================================================
//
// LEFT HEADER — according to the physical board supplied:
//
// VCC
//   Regulated output.
//   Board-specific solder selection may change its voltage.
//
// GND
//   Ground.
//
// GPIO16
//   Chip:
//     RTC_GPIO
//   Board:
//     PSRAM CS#
//   HARD BLOCKED.
//   Never assign as general motor GPIO.
//
// GPIO0
//   Chip:
//     ADC2_CH1
//     TOUCH1
//     RTC_GPIO11
//     CLK_OUT1
//     EMAC_TX_CLK
//   Board:
//     Camera XCLK
//     Boot/programming strap.
//   GPIO0 -> GND is used during flashing.
//
// GPIO2
//   Chip:
//     ADC2_CH2
//     TOUCH2
//     RTC_GPIO12
//     HSPIWP
//     HS2_DATA0
//     SD_DATA0
//   Board:
//     SD_DATA0
//     Boot-sensitive.
//
// GPIO4
//   Chip:
//     ADC2_CH0
//     TOUCH0
//     RTC_GPIO10
//     HSPIHD
//     HS2_DATA1
//     SD_DATA1
//     EMAC_TX_ER
//   Board:
//     SD_DATA1
//     White flash LED transistor.
//
// GND
//   Ground.
//
// 5V
//   Main 5V board supply input.
//
// ------------------------------------------------------------
//
// RIGHT HEADER:
//
// 3V3
//   Regulated 3.3V output.
//
// GND
//   Ground.
//
// GPIO12
//   Chip:
//     ADC2_CH5
//     TOUCH5
//     RTC_GPIO15
//     MTDI
//     HSPIQ
//     HS2_DATA2
//     SD_DATA2
//     EMAC_TXD3
//   Board:
//     SD_DATA2
//     Boot strapping pin.
//
// GPIO13
//   Chip:
//     ADC2_CH4
//     TOUCH4
//     RTC_GPIO14
//     MTCK
//     HSPID
//     HS2_DATA3
//     SD_DATA3
//     EMAC_RX_ER
//   Board:
//     SD_DATA3
//
// GPIO14
//   Chip:
//     ADC2_CH6
//     TOUCH6
//     RTC_GPIO16
//     MTMS
//     HSPICLK
//     HS2_CLK
//     SD_CLK
//     EMAC_TXD2
//   Board:
//     SD_CLK
//
// GPIO15
//   Chip:
//     ADC2_CH3
//     TOUCH3
//     RTC_GPIO13
//     MTDO
//     HSPICS0
//     HS2_CMD
//     SD_CMD
//     EMAC_RXD3
//   Board:
//     SD_CMD
//
// GPIO1 / TX
//   Chip:
//     U0TXD
//     CLK_OUT3
//     EMAC_RXD2
//   Board:
//     UART0 TX
//     Programming / Serial.
//
// GPIO3 / RX
//   Chip:
//     U0RXD
//     CLK_OUT2
//   Board:
//     UART0 RX
//     Programming / Serial.
//
// ------------------------------------------------------------
//
// INTERNAL:
//
// GPIO33
//   Internal red indicator LED.
//   Not externally exposed.
//   Active LOW:
//       LOW  = LED ON
//       HIGH = LED OFF
//
// ============================================================


// ============================================================
// BOARD PIN REGISTRY
// ============================================================

struct BoardPinInfo {
  int gpio;
  const char* header;
  const char* label;
  const char* functions;
  const char* boardUse;
  bool externallyExposed;
  bool hardBlocked;
  bool risky;
};

const BoardPinInfo BOARD_PINS[] = {

  {
    16,
    "LEFT",
    "GPIO16",
    "RTC_GPIO",
    "PSRAM_CS",
    true,
    true,
    true
  },

  {
    0,
    "LEFT",
    "GPIO0",
    "ADC2_CH1,TOUCH1,RTC_GPIO11,CLK_OUT1,EMAC_TX_CLK",
    "CAMERA_XCLK,BOOT_STRAP",
    true,
    false,
    true
  },

  {
    2,
    "LEFT",
    "GPIO2",
    "ADC2_CH2,TOUCH2,RTC_GPIO12,HSPIWP,HS2_DATA0,SD_DATA0",
    "SD_DATA0,BOOT_SENSITIVE",
    true,
    false,
    true
  },

  {
    4,
    "LEFT",
    "GPIO4",
    "ADC2_CH0,TOUCH0,RTC_GPIO10,HSPIHD,HS2_DATA1,SD_DATA1,EMAC_TX_ER",
    "SD_DATA1,FLASH_LED",
    true,
    false,
    true
  },

  {
    12,
    "RIGHT",
    "GPIO12",
    "ADC2_CH5,TOUCH5,RTC_GPIO15,MTDI,HSPIQ,HS2_DATA2,SD_DATA2,EMAC_TXD3",
    "SD_DATA2,BOOT_STRAP",
    true,
    false,
    true
  },

  {
    13,
    "RIGHT",
    "GPIO13",
    "ADC2_CH4,TOUCH4,RTC_GPIO14,MTCK,HSPID,HS2_DATA3,SD_DATA3,EMAC_RX_ER",
    "SD_DATA3",
    true,
    false,
    false
  },

  {
    14,
    "RIGHT",
    "GPIO14",
    "ADC2_CH6,TOUCH6,RTC_GPIO16,MTMS,HSPICLK,HS2_CLK,SD_CLK,EMAC_TXD2",
    "SD_CLK",
    true,
    false,
    false
  },

  {
    15,
    "RIGHT",
    "GPIO15",
    "ADC2_CH3,TOUCH3,RTC_GPIO13,MTDO,HSPICS0,HS2_CMD,SD_CMD,EMAC_RXD3",
    "SD_CMD",
    true,
    false,
    false
  },

  {
    1,
    "RIGHT",
    "GPIO1_TX",
    "U0TXD,CLK_OUT3,EMAC_RXD2",
    "UART0_TX",
    true,
    false,
    true
  },

  {
    3,
    "RIGHT",
    "GPIO3_RX",
    "U0RXD,CLK_OUT2",
    "UART0_RX",
    true,
    false,
    true
  },

  {
    33,
    "INTERNAL",
    "GPIO33",
    "DIGITAL_GPIO",
    "INTERNAL_RED_LED_ACTIVE_LOW",
    false,
    true,
    false
  }
};

constexpr size_t BOARD_PIN_COUNT =
  sizeof(BOARD_PINS) /
  sizeof(BOARD_PINS[0]);


// ============================================================
// INTERNAL STATUS LED
// ============================================================

constexpr int STATUS_LED_GPIO = 33;

void statusLed(bool on) {
  digitalWrite(
    STATUS_LED_GPIO,
    on ? LOW : HIGH
  );
}


// ============================================================
// MOTOR CONFIGURATION
// ============================================================

struct MotorConfig {
  bool configured;

  int in1;
  int in2;
  int in3;
  int in4;

  bool leftInvert;
  bool rightInvert;

  int maxPwm;
  int defaultPwm;

  unsigned long defaultTtlMs;
};

MotorConfig motorConfig;


// No guessed motor GPIO defaults.
// Configuration must be explicitly supplied after installation.

constexpr int FACTORY_MAX_PWM = 180;
constexpr int FACTORY_DEFAULT_PWM = 170;

constexpr unsigned long FACTORY_DEFAULT_TTL_MS = 900;

constexpr unsigned long MIN_TTL_MS = 100;
constexpr unsigned long MAX_TTL_MS = 2000;

constexpr uint32_t PWM_FREQ = 12000;
constexpr uint8_t PWM_RESOLUTION = 8;


// ============================================================
// RUNTIME MOTOR STATE
// ============================================================

bool motorPinsAttached = false;

bool armed = false;
bool estop = false;
bool motionActive = false;

int currentLeftPwm = 0;
int currentRightPwm = 0;

unsigned long lastDriveAt = 0;
unsigned long activeTtlMs = FACTORY_DEFAULT_TTL_MS;

String lastCommand = "BOOT";


// ============================================================
// MOTOR TEST STATE
// ============================================================

bool motorTestActive = false;

unsigned long motorTestEndsAt = 0;

String motorTestName = "";


// ============================================================
// RESTART STATE
// ============================================================

bool restartPending = false;
unsigned long restartAt = 0;


// ============================================================
// GENERIC HELPERS
// ============================================================

String boolJson(bool value) {
  return value ? "true" : "false";
}

void sendJson(
  int status,
  const String& body
) {
  server.send(
    status,
    "application/json",
    body
  );
}

bool stringToBool(
  String value,
  bool fallback
) {
  value.trim();
  value.toLowerCase();

  if (
    value == "1" ||
    value == "true" ||
    value == "yes" ||
    value == "on"
  ) {
    return true;
  }

  if (
    value == "0" ||
    value == "false" ||
    value == "no" ||
    value == "off"
  ) {
    return false;
  }

  return fallback;
}


// ============================================================
// PIN REGISTRY HELPERS
// ============================================================

const BoardPinInfo* findBoardPin(
  int gpio
) {
  for (
    size_t i = 0;
    i < BOARD_PIN_COUNT;
    i++
  ) {
    if (
      BOARD_PINS[i].gpio == gpio
    ) {
      return &BOARD_PINS[i];
    }
  }

  return nullptr;
}

bool pinExists(
  int gpio
) {
  return findBoardPin(gpio) != nullptr;
}

bool pinRisky(
  int gpio
) {
  const BoardPinInfo* info =
    findBoardPin(gpio);

  return info
    ? info->risky
    : true;
}

bool validateOneMotorPin(
  int gpio,
  bool allowRisky,
  String& reason
) {
  const BoardPinInfo* info =
    findBoardPin(gpio);

  if (!info) {
    reason =
      "GPIO_NOT_IN_BOARD_REGISTRY";

    return false;
  }

  if (!info->externallyExposed) {
    reason =
      "GPIO_NOT_EXTERNALLY_EXPOSED";

    return false;
  }

  if (info->hardBlocked) {
    reason =
      "GPIO_HARD_BLOCKED";

    return false;
  }

  if (
    info->risky &&
    !allowRisky
  ) {
    reason =
      "GPIO_REQUIRES_ALLOW_RISKY";

    return false;
  }

  return true;
}

bool validateMotorPins(
  int in1,
  int in2,
  int in3,
  int in4,
  bool allowRisky,
  String& reason
) {
  int values[4] = {
    in1,
    in2,
    in3,
    in4
  };

  for (int i = 0; i < 4; i++) {
    if (
      !validateOneMotorPin(
        values[i],
        allowRisky,
        reason
      )
    ) {
      reason =
        "GPIO" +
        String(values[i]) +
        ":" +
        reason;

      return false;
    }
  }

  for (int a = 0; a < 4; a++) {
    for (
      int b = a + 1;
      b < 4;
      b++
    ) {
      if (
        values[a] ==
        values[b]
      ) {
        reason =
          "MOTOR_PINS_MUST_BE_UNIQUE";

        return false;
      }
    }
  }

  return true;
}


// ============================================================
// NVS CONFIGURATION
// ============================================================

void loadMotorConfig() {
  prefs.begin(
    "nes_motor",
    true
  );

  motorConfig.configured =
    prefs.getBool(
      "configured",
      false
    );

  if (
    motorConfig.configured
  ) {
    motorConfig.in1 =
      prefs.getInt("in1", -1);

    motorConfig.in2 =
      prefs.getInt("in2", -1);

    motorConfig.in3 =
      prefs.getInt("in3", -1);

    motorConfig.in4 =
      prefs.getInt("in4", -1);

    motorConfig.leftInvert =
      prefs.getBool(
        "leftInv",
        false
      );

    motorConfig.rightInvert =
      prefs.getBool(
        "rightInv",
        false
      );

    motorConfig.maxPwm =
      prefs.getInt(
        "maxPwm",
        FACTORY_MAX_PWM
      );

    motorConfig.defaultPwm =
      prefs.getInt(
        "defPwm",
        FACTORY_DEFAULT_PWM
      );

    motorConfig.defaultTtlMs =
      prefs.getULong(
        "defTtl",
        FACTORY_DEFAULT_TTL_MS
      );
  }
  else {
    motorConfig.in1 = -1;
    motorConfig.in2 = -1;
    motorConfig.in3 = -1;
    motorConfig.in4 = -1;

    motorConfig.leftInvert = false;
    motorConfig.rightInvert = false;

    motorConfig.maxPwm =
      FACTORY_MAX_PWM;

    motorConfig.defaultPwm =
      FACTORY_DEFAULT_PWM;

    motorConfig.defaultTtlMs =
      FACTORY_DEFAULT_TTL_MS;
  }

  prefs.end();
}

bool saveMotorConfig(
  const MotorConfig& cfg
) {
  if (
    !prefs.begin(
      "nes_motor",
      false
    )
  ) {
    return false;
  }

  bool ok = true;

  ok &= prefs.putBool(
    "configured",
    true
  ) > 0;

  ok &= prefs.putInt(
    "in1",
    cfg.in1
  ) > 0;

  ok &= prefs.putInt(
    "in2",
    cfg.in2
  ) > 0;

  ok &= prefs.putInt(
    "in3",
    cfg.in3
  ) > 0;

  ok &= prefs.putInt(
    "in4",
    cfg.in4
  ) > 0;

  prefs.putBool(
    "leftInv",
    cfg.leftInvert
  );

  prefs.putBool(
    "rightInv",
    cfg.rightInvert
  );

  ok &= prefs.putInt(
    "maxPwm",
    cfg.maxPwm
  ) > 0;

  ok &= prefs.putInt(
    "defPwm",
    cfg.defaultPwm
  ) > 0;

  ok &= prefs.putULong(
    "defTtl",
    cfg.defaultTtlMs
  ) > 0;

  prefs.end();

  return ok;
}

bool clearMotorConfig() {
  if (
    !prefs.begin(
      "nes_motor",
      false
    )
  ) {
    return false;
  }

  bool ok =
    prefs.clear();

  prefs.end();

  return ok;
}


// ============================================================
// MOTOR PWM ATTACH / DETACH
// ============================================================

void detachMotorPins() {
  if (!motorPinsAttached) {
    return;
  }

  ledcWrite(
    motorConfig.in1,
    0
  );

  ledcWrite(
    motorConfig.in2,
    0
  );

  ledcWrite(
    motorConfig.in3,
    0
  );

  ledcWrite(
    motorConfig.in4,
    0
  );

  ledcDetach(
    motorConfig.in1
  );

  ledcDetach(
    motorConfig.in2
  );

  ledcDetach(
    motorConfig.in3
  );

  ledcDetach(
    motorConfig.in4
  );

  motorPinsAttached = false;
}

bool attachMotorPins() {
  if (
    !motorConfig.configured
  ) {
    return false;
  }

  String reason;

  // Stored configuration has already been explicitly accepted,
  // so risky pins do not need to be acknowledged again here.
  if (
    !validateMotorPins(
      motorConfig.in1,
      motorConfig.in2,
      motorConfig.in3,
      motorConfig.in4,
      true,
      reason
    )
  ) {
    Serial.print(
      "[MOTOR] Invalid stored config: "
    );

    Serial.println(reason);

    return false;
  }

  pinMode(
    motorConfig.in1,
    OUTPUT
  );

  pinMode(
    motorConfig.in2,
    OUTPUT
  );

  pinMode(
    motorConfig.in3,
    OUTPUT
  );

  pinMode(
    motorConfig.in4,
    OUTPUT
  );

  bool ok1 =
    ledcAttach(
      motorConfig.in1,
      PWM_FREQ,
      PWM_RESOLUTION
    );

  bool ok2 =
    ledcAttach(
      motorConfig.in2,
      PWM_FREQ,
      PWM_RESOLUTION
    );

  bool ok3 =
    ledcAttach(
      motorConfig.in3,
      PWM_FREQ,
      PWM_RESOLUTION
    );

  bool ok4 =
    ledcAttach(
      motorConfig.in4,
      PWM_FREQ,
      PWM_RESOLUTION
    );

  motorPinsAttached =
    ok1 &&
    ok2 &&
    ok3 &&
    ok4;

  if (!motorPinsAttached) {
    Serial.println(
      "[MOTOR] PWM attach failure."
    );

    if (ok1) {
      ledcDetach(
        motorConfig.in1
      );
    }

    if (ok2) {
      ledcDetach(
        motorConfig.in2
      );
    }

    if (ok3) {
      ledcDetach(
        motorConfig.in3
      );
    }

    if (ok4) {
      ledcDetach(
        motorConfig.in4
      );
    }

    return false;
  }

  ledcWrite(
    motorConfig.in1,
    0
  );

  ledcWrite(
    motorConfig.in2,
    0
  );

  ledcWrite(
    motorConfig.in3,
    0
  );

  ledcWrite(
    motorConfig.in4,
    0
  );

  Serial.println(
    "[MOTOR] Dynamic GPIO mapping:"
  );

  Serial.print(
    "  IN1 = GPIO"
  );
  Serial.println(
    motorConfig.in1
  );

  Serial.print(
    "  IN2 = GPIO"
  );
  Serial.println(
    motorConfig.in2
  );

  Serial.print(
    "  IN3 = GPIO"
  );
  Serial.println(
    motorConfig.in3
  );

  Serial.print(
    "  IN4 = GPIO"
  );
  Serial.println(
    motorConfig.in4
  );

  return true;
}


// ============================================================
// MOTOR CONTROL
// ============================================================

void allMotorPinsOff() {
  if (!motorPinsAttached) {
    return;
  }

  ledcWrite(
    motorConfig.in1,
    0
  );

  ledcWrite(
    motorConfig.in2,
    0
  );

  ledcWrite(
    motorConfig.in3,
    0
  );

  ledcWrite(
    motorConfig.in4,
    0
  );
}

void setMotorSide(
  int pinForward,
  int pinReverse,
  int value,
  bool invert
) {
  if (!motorPinsAttached) {
    return;
  }

  value =
    constrain(
      value,
      -motorConfig.maxPwm,
      motorConfig.maxPwm
    );

  if (invert) {
    value = -value;
  }

  if (value > 0) {
    ledcWrite(
      pinReverse,
      0
    );

    ledcWrite(
      pinForward,
      value
    );
  }
  else if (value < 0) {
    ledcWrite(
      pinForward,
      0
    );

    ledcWrite(
      pinReverse,
      -value
    );
  }
  else {
    ledcWrite(
      pinForward,
      0
    );

    ledcWrite(
      pinReverse,
      0
    );
  }
}

void applyDrive(
  int left,
  int right
) {
  if (!motorPinsAttached) {
    return;
  }

  left =
    constrain(
      left,
      -motorConfig.maxPwm,
      motorConfig.maxPwm
    );

  right =
    constrain(
      right,
      -motorConfig.maxPwm,
      motorConfig.maxPwm
    );

  setMotorSide(
    motorConfig.in1,
    motorConfig.in2,
    left,
    motorConfig.leftInvert
  );

  setMotorSide(
    motorConfig.in3,
    motorConfig.in4,
    right,
    motorConfig.rightInvert
  );

  currentLeftPwm =
    left;

  currentRightPwm =
    right;

  motionActive =
    left != 0 ||
    right != 0;
}

void stopMotors(
  const String& reason
) {
  allMotorPinsOff();

  currentLeftPwm = 0;
  currentRightPwm = 0;

  motionActive = false;

  motorTestActive = false;
  motorTestName = "";

  lastCommand =
    reason;

  Serial.print(
    "[MOTOR] STOP: "
  );

  Serial.println(reason);
}

unsigned long ttlRemaining() {
  if (!motionActive) {
    return 0;
  }

  unsigned long elapsed =
    millis() -
    lastDriveAt;

  if (
    elapsed >=
    activeTtlMs
  ) {
    return 0;
  }

  return
    activeTtlMs -
    elapsed;
}


// ============================================================
// CONFIGURATION REQUIRED GUARD
// ============================================================

bool requireMotorConfig() {
  if (
    !motorConfig.configured ||
    !motorPinsAttached
  ) {
    sendJson(
      409,
      "{\"ok\":false,"
      "\"error\":\"MOTOR_CONFIG_REQUIRED\","
      "\"message\":\"Configure motor GPIO mapping through /config/motors first\"}"
    );

    return false;
  }

  return true;
}


// ============================================================
// GET /
// ============================================================

void handleRoot() {
  String json = "{";

  json +=
    "\"ok\":true,";

  json +=
    "\"device\":\"" +
    String(DEVICE_NAME) +
    "\",";

  json +=
    "\"firmware\":\"" +
    String(FIRMWARE_VERSION) +
    "\",";

  json +=
    "\"board\":\"AI-Thinker ESP32-CAM\",";

  json +=
    "\"mode\":\"dynamic-hardware\",";

  json +=
    "\"cameraInitialized\":false,";

  json +=
    "\"microSdInitialized\":false,";

  json +=
    "\"motorConfigured\":" +
    boolJson(
      motorConfig.configured &&
      motorPinsAttached
    ) +
    ",";

  json +=
    "\"apis\":["
      "\"/ping\","
      "\"/state\","
      "\"/board/pins\","
      "\"/config/motors\","
      "\"/config/motors/reset\","
      "\"/config/motors/test\","
      "\"/arm\","
      "\"/disarm\","
      "\"/drive\","
      "\"/stop\","
      "\"/estop\","
      "\"/estop/clear\""
    "]";

  json += "}";

  sendJson(
    200,
    json
  );
}


// ============================================================
// GET /ping
// ============================================================

void handlePing() {
  String json = "{";

  json +=
    "\"ok\":true,";

  json +=
    "\"device\":\"" +
    String(DEVICE_NAME) +
    "\",";

  json +=
    "\"firmware\":\"" +
    String(FIRMWARE_VERSION) +
    "\",";

  json +=
    "\"mode\":\"hardware\",";

  json +=
    "\"configured\":" +
    boolJson(
      motorConfig.configured
    ) +
    ",";

  json +=
    "\"uptimeMs\":" +
    String(millis());

  json += "}";

  sendJson(
    200,
    json
  );
}


// ============================================================
// GET /board/pins
// ============================================================

void handleBoardPins() {
  String json;

  json.reserve(5000);

  json = "{";

  json +=
    "\"ok\":true,";

  json +=
    "\"board\":\"AI-Thinker ESP32-CAM\",";

  json +=
    "\"headers\":{";

  json +=
    "\"left\":["
      "\"VCC\","
      "\"GND\","
      "\"GPIO16\","
      "\"GPIO0\","
      "\"GPIO2\","
      "\"GPIO4\","
      "\"GND\","
      "\"5V\""
    "],";

  json +=
    "\"right\":["
      "\"3V3\","
      "\"GND\","
      "\"GPIO12\","
      "\"GPIO13\","
      "\"GPIO14\","
      "\"GPIO15\","
      "\"GPIO1_TX\","
      "\"GPIO3_RX\""
    "]";

  json += "},";

  json +=
    "\"internal\":["
      "\"GPIO33_RED_LED_ACTIVE_LOW\""
    "],";

  json +=
    "\"pins\":[";

  for (
    size_t i = 0;
    i < BOARD_PIN_COUNT;
    i++
  ) {
    const BoardPinInfo& p =
      BOARD_PINS[i];

    if (i > 0) {
      json += ",";
    }

    json += "{";

    json +=
      "\"gpio\":" +
      String(p.gpio) +
      ",";

    json +=
      "\"header\":\"" +
      String(p.header) +
      "\",";

    json +=
      "\"label\":\"" +
      String(p.label) +
      "\",";

    json +=
      "\"functions\":\"" +
      String(p.functions) +
      "\",";

    json +=
      "\"boardUse\":\"" +
      String(p.boardUse) +
      "\",";

    json +=
      "\"externallyExposed\":" +
      boolJson(
        p.externallyExposed
      ) +
      ",";

    json +=
      "\"hardBlocked\":" +
      boolJson(
        p.hardBlocked
      ) +
      ",";

    json +=
      "\"risky\":" +
      boolJson(
        p.risky
      );

    json += "}";
  }

  json += "]";

  json += "}";

  sendJson(
    200,
    json
  );
}


// ============================================================
// GET /state
// ============================================================

void handleState() {
  String json;

  json.reserve(1800);

  json = "{";

  json +=
    "\"ok\":true,";

  json +=
    "\"device\":\"" +
    String(DEVICE_NAME) +
    "\",";

  json +=
    "\"firmware\":\"" +
    String(FIRMWARE_VERSION) +
    "\",";

  json +=
    "\"wifiConnected\":" +
    boolJson(
      WiFi.status() ==
      WL_CONNECTED
    ) +
    ",";

  json +=
    "\"ip\":\"" +
    WiFi.localIP().toString() +
    "\",";

  json +=
    "\"rssi\":" +
    String(
      WiFi.RSSI()
    ) +
    ",";

  json +=
    "\"motorConfigured\":" +
    boolJson(
      motorConfig.configured
    ) +
    ",";

  json +=
    "\"motorReady\":" +
    boolJson(
      motorPinsAttached
    ) +
    ",";

  json +=
    "\"armed\":" +
    boolJson(armed) +
    ",";

  json +=
    "\"estop\":" +
    boolJson(estop) +
    ",";

  json +=
    "\"motion\":\"" +
    String(
      motionActive
        ? "moving"
        : "stopped"
    ) +
    "\",";

  json +=
    "\"leftPwm\":" +
    String(
      currentLeftPwm
    ) +
    ",";

  json +=
    "\"rightPwm\":" +
    String(
      currentRightPwm
    ) +
    ",";

  json +=
    "\"ttlRemainingMs\":" +
    String(
      ttlRemaining()
    ) +
    ",";

  json +=
    "\"lastCommand\":\"" +
    lastCommand +
    "\",";

  json +=
    "\"motorTestActive\":" +
    boolJson(
      motorTestActive
    ) +
    ",";

  json +=
    "\"motorTest\":\"" +
    motorTestName +
    "\",";

  json +=
    "\"pins\":{";

  json +=
    "\"in1\":" +
    String(
      motorConfig.in1
    ) +
    ",";

  json +=
    "\"in2\":" +
    String(
      motorConfig.in2
    ) +
    ",";

  json +=
    "\"in3\":" +
    String(
      motorConfig.in3
    ) +
    ",";

  json +=
    "\"in4\":" +
    String(
      motorConfig.in4
    );

  json += "},";

  json +=
    "\"leftInvert\":" +
    boolJson(
      motorConfig.leftInvert
    ) +
    ",";

  json +=
    "\"rightInvert\":" +
    boolJson(
      motorConfig.rightInvert
    ) +
    ",";

  json +=
    "\"maxPwm\":" +
    String(
      motorConfig.maxPwm
    ) +
    ",";

  json +=
    "\"defaultPwm\":" +
    String(
      motorConfig.defaultPwm
    ) +
    ",";

  json +=
    "\"defaultTtlMs\":" +
    String(
      motorConfig.defaultTtlMs
    ) +
    ",";

  json +=
    "\"uptimeMs\":" +
    String(millis());

  json += "}";

  sendJson(
    200,
    json
  );
}


// ============================================================
// GET /config/motors
// ============================================================

void handleGetMotorConfig() {
  String json = "{";

  json +=
    "\"ok\":true,";

  json +=
    "\"configured\":" +
    boolJson(
      motorConfig.configured
    ) +
    ",";

  json +=
    "\"ready\":" +
    boolJson(
      motorPinsAttached
    ) +
    ",";

  json +=
    "\"in1\":" +
    String(
      motorConfig.in1
    ) +
    ",";

  json +=
    "\"in2\":" +
    String(
      motorConfig.in2
    ) +
    ",";

  json +=
    "\"in3\":" +
    String(
      motorConfig.in3
    ) +
    ",";

  json +=
    "\"in4\":" +
    String(
      motorConfig.in4
    ) +
    ",";

  json +=
    "\"leftInvert\":" +
    boolJson(
      motorConfig.leftInvert
    ) +
    ",";

  json +=
    "\"rightInvert\":" +
    boolJson(
      motorConfig.rightInvert
    ) +
    ",";

  json +=
    "\"maxPwm\":" +
    String(
      motorConfig.maxPwm
    ) +
    ",";

  json +=
    "\"defaultPwm\":" +
    String(
      motorConfig.defaultPwm
    ) +
    ",";

  json +=
    "\"defaultTtlMs\":" +
    String(
      motorConfig.defaultTtlMs
    );

  json += "}";

  sendJson(
    200,
    json
  );
}


// ============================================================
// POST /config/motors
//
// Required:
//   in1
//   in2
//   in3
//   in4
//
// Optional:
//   leftInvert
//   rightInvert
//   maxPwm
//   defaultPwm
//   defaultTtlMs
//   allowRisky=1
//
// Example structure:
//
// POST /config/motors?
//      in1=...&in2=...&in3=...&in4=...
//
// No default GPIO values are assumed.
// ============================================================

void handleSetMotorConfig() {
  if (
    armed ||
    motionActive ||
    motorTestActive
  ) {
    sendJson(
      409,
      "{\"ok\":false,"
      "\"error\":\"MOTORS_MUST_BE_STOPPED_AND_DISARMED\"}"
    );

    return;
  }

  if (
    !server.hasArg("in1") ||
    !server.hasArg("in2") ||
    !server.hasArg("in3") ||
    !server.hasArg("in4")
  ) {
    sendJson(
      400,
      "{\"ok\":false,"
      "\"error\":\"IN1_IN2_IN3_IN4_REQUIRED\"}"
    );

    return;
  }

  MotorConfig candidate;

  candidate.configured = true;

  candidate.in1 =
    server.arg("in1").toInt();

  candidate.in2 =
    server.arg("in2").toInt();

  candidate.in3 =
    server.arg("in3").toInt();

  candidate.in4 =
    server.arg("in4").toInt();

  candidate.leftInvert =
    server.hasArg(
      "leftInvert"
    )
      ? stringToBool(
          server.arg(
            "leftInvert"
          ),
          false
        )
      : false;

  candidate.rightInvert =
    server.hasArg(
      "rightInvert"
    )
      ? stringToBool(
          server.arg(
            "rightInvert"
          ),
          false
        )
      : false;

  candidate.maxPwm =
    server.hasArg("maxPwm")
      ? server.arg(
          "maxPwm"
        ).toInt()
      : FACTORY_MAX_PWM;

  candidate.maxPwm =
    constrain(
      candidate.maxPwm,
      1,
      255
    );

  candidate.defaultPwm =
    server.hasArg(
      "defaultPwm"
    )
      ? server.arg(
          "defaultPwm"
        ).toInt()
      : FACTORY_DEFAULT_PWM;

  candidate.defaultPwm =
    constrain(
      candidate.defaultPwm,
      1,
      candidate.maxPwm
    );

  candidate.defaultTtlMs =
    server.hasArg(
      "defaultTtlMs"
    )
      ? (unsigned long)
        server.arg(
          "defaultTtlMs"
        ).toInt()
      : FACTORY_DEFAULT_TTL_MS;

  candidate.defaultTtlMs =
    constrain(
      candidate.defaultTtlMs,
      MIN_TTL_MS,
      MAX_TTL_MS
    );

  bool allowRisky =
    server.hasArg(
      "allowRisky"
    ) &&
    stringToBool(
      server.arg(
        "allowRisky"
      ),
      false
    );

  String reason;

  if (
    !validateMotorPins(
      candidate.in1,
      candidate.in2,
      candidate.in3,
      candidate.in4,
      allowRisky,
      reason
    )
  ) {
    sendJson(
      400,
      "{\"ok\":false,"
      "\"error\":\"INVALID_MOTOR_PIN_CONFIG\","
      "\"reason\":\"" +
      reason +
      "\"}"
    );

    return;
  }

  stopMotors(
    "CONFIG_UPDATE"
  );

  armed = false;

  if (
    !saveMotorConfig(
      candidate
    )
  ) {
    sendJson(
      500,
      "{\"ok\":false,"
      "\"error\":\"NVS_SAVE_FAILED\"}"
    );

    return;
  }

  String json = "{";

  json +=
    "\"ok\":true,";

  json +=
    "\"saved\":true,";

  json +=
    "\"restarting\":true,";

  json +=
    "\"in1\":" +
    String(candidate.in1) +
    ",";

  json +=
    "\"in2\":" +
    String(candidate.in2) +
    ",";

  json +=
    "\"in3\":" +
    String(candidate.in3) +
    ",";

  json +=
    "\"in4\":" +
    String(candidate.in4);

  json += "}";

  sendJson(
    200,
    json
  );

  restartPending = true;

  restartAt =
    millis() + 800;
}


// ============================================================
// POST /config/motors/reset
// ============================================================

void handleResetMotorConfig() {
  stopMotors(
    "CONFIG_RESET"
  );

  armed = false;
  estop = false;

  if (
    !clearMotorConfig()
  ) {
    sendJson(
      500,
      "{\"ok\":false,"
      "\"error\":\"NVS_CLEAR_FAILED\"}"
    );

    return;
  }

  sendJson(
    200,
    "{\"ok\":true,"
    "\"reset\":true,"
    "\"restarting\":true}"
  );

  restartPending = true;

  restartAt =
    millis() + 800;
}


// ============================================================
// POST /config/motors/test
//
// Requires:
//   configured motor pins
//   armed=true
//
// Args:
//   side=left|right
//   direction=forward|reverse
//
// Optional:
//   pwm=1..maxPwm
//   ms=100..2000
//
// Test returns immediately.
// The loop stops the test after the configured duration.
// ============================================================

void handleMotorTest() {
  if (!requireMotorConfig()) {
    return;
  }

  if (estop) {
    sendJson(
      423,
      "{\"ok\":false,"
      "\"error\":\"ESTOP_ACTIVE\"}"
    );

    return;
  }

  if (!armed) {
    sendJson(
      409,
      "{\"ok\":false,"
      "\"error\":\"ROBOT_NOT_ARMED\"}"
    );

    return;
  }

  if (
    !server.hasArg("side") ||
    !server.hasArg("direction")
  ) {
    sendJson(
      400,
      "{\"ok\":false,"
      "\"error\":\"SIDE_AND_DIRECTION_REQUIRED\"}"
    );

    return;
  }

  String side =
    server.arg("side");

  String direction =
    server.arg("direction");

  side.toLowerCase();
  direction.toLowerCase();

  if (
    side != "left" &&
    side != "right"
  ) {
    sendJson(
      400,
      "{\"ok\":false,"
      "\"error\":\"INVALID_SIDE\"}"
    );

    return;
  }

  if (
    direction != "forward" &&
    direction != "reverse"
  ) {
    sendJson(
      400,
      "{\"ok\":false,"
      "\"error\":\"INVALID_DIRECTION\"}"
    );

    return;
  }

  int pwm =
    server.hasArg("pwm")
      ? server.arg("pwm").toInt()
      : motorConfig.defaultPwm;

  pwm =
    constrain(
      pwm,
      1,
      motorConfig.maxPwm
    );

  int durationMs =
    server.hasArg("ms")
      ? server.arg("ms").toInt()
      : 500;

  durationMs =
    constrain(
      durationMs,
      100,
      2000
    );

  int value =
    direction == "forward"
      ? pwm
      : -pwm;

  int left = 0;
  int right = 0;

  if (side == "left") {
    left = value;
  }
  else {
    right = value;
  }

  stopMotors(
    "TEST_PREPARE"
  );

  applyDrive(
    left,
    right
  );

  motorTestActive = true;

  motorTestEndsAt =
    millis() +
    durationMs;

  motorTestName =
    side +
    "_" +
    direction;

  lastCommand =
    "MOTOR_TEST";

  String json = "{";

  json +=
    "\"ok\":true,";

  json +=
    "\"test\":\"" +
    motorTestName +
    "\",";

  json +=
    "\"pwm\":" +
    String(pwm) +
    ",";

  json +=
    "\"durationMs\":" +
    String(durationMs);

  json += "}";

  sendJson(
    200,
    json
  );
}


// ============================================================
// POST /arm
// ============================================================

void handleArm() {
  if (!requireMotorConfig()) {
    return;
  }

  if (estop) {
    sendJson(
      423,
      "{\"ok\":false,"
      "\"error\":\"ESTOP_ACTIVE\"}"
    );

    return;
  }

  stopMotors(
    "ARM_STOP"
  );

  armed = true;

  Serial.println(
    "[SAFETY] ARMED"
  );

  sendJson(
    200,
    "{\"ok\":true,"
    "\"armed\":true,"
    "\"motion\":\"stopped\"}"
  );
}


// ============================================================
// POST /disarm
// ============================================================

void handleDisarm() {
  stopMotors(
    "DISARM"
  );

  armed = false;

  Serial.println(
    "[SAFETY] DISARMED"
  );

  sendJson(
    200,
    "{\"ok\":true,"
    "\"armed\":false,"
    "\"motion\":\"stopped\"}"
  );
}


// ============================================================
// POST /stop
// ============================================================

void handleStop() {
  stopMotors(
    "STOP"
  );

  sendJson(
    200,
    "{\"ok\":true,"
    "\"motion\":\"stopped\"}"
  );
}


// ============================================================
// POST /estop
// ============================================================

void handleEstop() {
  stopMotors(
    "ESTOP"
  );

  estop = true;
  armed = false;

  statusLed(true);

  Serial.println(
    "[SAFETY] ESTOP ACTIVE"
  );

  sendJson(
    200,
    "{\"ok\":true,"
    "\"estop\":true,"
    "\"armed\":false,"
    "\"motion\":\"stopped\"}"
  );
}


// ============================================================
// POST /estop/clear
// ============================================================

void handleEstopClear() {
  stopMotors(
    "ESTOP_CLEAR"
  );

  estop = false;
  armed = false;

  statusLed(false);

  Serial.println(
    "[SAFETY] ESTOP CLEARED"
  );

  sendJson(
    200,
    "{\"ok\":true,"
    "\"estop\":false,"
    "\"armed\":false,"
    "\"motion\":\"stopped\"}"
  );
}


// ============================================================
// POST /drive
//
// /drive?left=170&right=170&ttl=900
// ============================================================

void handleDrive() {
  if (!requireMotorConfig()) {
    return;
  }

  if (estop) {
    sendJson(
      423,
      "{\"ok\":false,"
      "\"error\":\"ESTOP_ACTIVE\"}"
    );

    return;
  }

  if (!armed) {
    sendJson(
      409,
      "{\"ok\":false,"
      "\"error\":\"ROBOT_NOT_ARMED\"}"
    );

    return;
  }

  if (
    !server.hasArg("left") ||
    !server.hasArg("right")
  ) {
    sendJson(
      400,
      "{\"ok\":false,"
      "\"error\":\"LEFT_RIGHT_REQUIRED\"}"
    );

    return;
  }

  int left =
    server.arg(
      "left"
    ).toInt();

  int right =
    server.arg(
      "right"
    ).toInt();

  left =
    constrain(
      left,
      -motorConfig.maxPwm,
      motorConfig.maxPwm
    );

  right =
    constrain(
      right,
      -motorConfig.maxPwm,
      motorConfig.maxPwm
    );

  unsigned long ttl =
    motorConfig.defaultTtlMs;

  if (
    server.hasArg("ttl")
  ) {
    long requested =
      server.arg(
        "ttl"
      ).toInt();

    requested =
      constrain(
        requested,
        (long)MIN_TTL_MS,
        (long)MAX_TTL_MS
      );

    ttl =
      (unsigned long)
      requested;
  }

  motorTestActive = false;

  activeTtlMs = ttl;
  lastDriveAt = millis();

  applyDrive(
    left,
    right
  );

  lastCommand =
    left == 0 &&
    right == 0
      ? "DRIVE_ZERO"
      : "DRIVE";

  String json = "{";

  json +=
    "\"ok\":true,";

  json +=
    "\"left\":" +
    String(left) +
    ",";

  json +=
    "\"right\":" +
    String(right) +
    ",";

  json +=
    "\"ttlMs\":" +
    String(ttl);

  json += "}";

  sendJson(
    200,
    json
  );
}


// ============================================================
// 404
// ============================================================

void handleNotFound() {
  String json = "{";

  json +=
    "\"ok\":false,";

  json +=
    "\"error\":\"NOT_FOUND\",";

  json +=
    "\"path\":\"" +
    server.uri() +
    "\"";

  json += "}";

  sendJson(
    404,
    json
  );
}


// ============================================================
// WI-FI
// ============================================================

void connectWiFi() {
  stopMotors(
    "WIFI_CONNECT"
  );

  armed = false;

  Serial.println();
  Serial.println(
    "========================================"
  );

  Serial.println(
    "NES NEXT ESP32-CAM DYNAMIC BRIDGE"
  );

  Serial.print(
    "Firmware: "
  );

  Serial.println(
    FIRMWARE_VERSION
  );

  Serial.println(
    "Camera: not initialized"
  );

  Serial.println(
    "microSD: not initialized"
  );

  Serial.println(
    "Motor GPIO: dynamic / NVS"
  );

  Serial.println(
    "Connecting to Wi-Fi..."
  );

  Serial.println(
    "========================================"
  );

  WiFi.mode(
    WIFI_STA
  );

  WiFi.setSleep(
    false
  );

  WiFi.setHostname(
    DEVICE_NAME
  );

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  unsigned long startedAt =
    millis();

  while (
    WiFi.status() !=
    WL_CONNECTED
  ) {
    delay(500);

    Serial.print(".");

    if (
      millis() -
      startedAt >
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

  Serial.println(
    "[WIFI] CONNECTED"
  );

  Serial.print(
    "[WIFI] IP: "
  );

  Serial.println(
    WiFi.localIP()
  );

  Serial.print(
    "[WIFI] RSSI: "
  );

  Serial.println(
    WiFi.RSSI()
  );
}


// ============================================================
// HTTP ROUTES
// ============================================================

void setupHttpRoutes() {
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
    "/board/pins",
    HTTP_GET,
    handleBoardPins
  );

  server.on(
    "/config/motors",
    HTTP_GET,
    handleGetMotorConfig
  );

  server.on(
    "/config/motors",
    HTTP_POST,
    handleSetMotorConfig
  );

  server.on(
    "/config/motors/reset",
    HTTP_POST,
    handleResetMotorConfig
  );

  server.on(
    "/config/motors/test",
    HTTP_POST,
    handleMotorTest
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
}


// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(
    115200
  );

  delay(200);

  pinMode(
    STATUS_LED_GPIO,
    OUTPUT
  );

  statusLed(true);

  loadMotorConfig();

  if (
    motorConfig.configured
  ) {
    Serial.println(
      "[CONFIG] Motor mapping found in NVS."
    );

    if (
      !attachMotorPins()
    ) {
      Serial.println(
        "[CONFIG] Stored motor mapping is invalid or unavailable."
      );
    }
  }
  else {
    Serial.println(
      "[CONFIG] No motor GPIO mapping configured."
    );

    Serial.println(
      "[CONFIG] Use POST /config/motors"
    );
  }

  stopMotors(
    "BOOT"
  );

  delay(300);

  connectWiFi();

  setupHttpRoutes();

  server.begin();

  statusLed(false);

  Serial.println();

  Serial.println(
    "[HTTP] Server started"
  );

  Serial.println(
    "[HTTP] GET  /"
  );

  Serial.println(
    "[HTTP] GET  /ping"
  );

  Serial.println(
    "[HTTP] GET  /state"
  );

  Serial.println(
    "[HTTP] GET  /board/pins"
  );

  Serial.println(
    "[HTTP] GET  /config/motors"
  );

  Serial.println(
    "[HTTP] POST /config/motors"
  );

  Serial.println(
    "[HTTP] POST /config/motors/reset"
  );

  Serial.println(
    "[HTTP] POST /config/motors/test"
  );

  Serial.println(
    "[HTTP] POST /arm"
  );

  Serial.println(
    "[HTTP] POST /disarm"
  );

  Serial.println(
    "[HTTP] POST /drive"
  );

  Serial.println(
    "[HTTP] POST /stop"
  );

  Serial.println(
    "[HTTP] POST /estop"
  );

  Serial.println(
    "[HTTP] POST /estop/clear"
  );

  Serial.println();

  Serial.print(
    "[NEXT] http://"
  );

  Serial.print(
    WiFi.localIP()
  );

  Serial.println(
    "/state"
  );
}


// ============================================================
// LOOP
// ============================================================

void loop() {

  // ----------------------------------------------------------
  // Scheduled motor diagnostic stop
  // ----------------------------------------------------------

  if (
    motorTestActive &&
    (long)(
      millis() -
      motorTestEndsAt
    ) >= 0
  ) {
    stopMotors(
      "MOTOR_TEST_COMPLETE"
    );
  }


  // ----------------------------------------------------------
  // Dead-man TTL
  // ----------------------------------------------------------

  if (
    motionActive &&
    !motorTestActive &&
    millis() -
    lastDriveAt >=
    activeTtlMs
  ) {
    stopMotors(
      "COMMAND_TTL_EXPIRED"
    );
  }


  // ----------------------------------------------------------
  // Wi-Fi loss
  // ----------------------------------------------------------

  if (
    WiFi.status() !=
    WL_CONNECTED
  ) {
    stopMotors(
      "WIFI_LOST"
    );

    armed = false;

    Serial.println(
      "[WIFI] Connection lost."
    );

    connectWiFi();
  }


  // ----------------------------------------------------------
  // HTTP
  // ----------------------------------------------------------

  server.handleClient();


  // ----------------------------------------------------------
  // Deferred restart after HTTP response
  // ----------------------------------------------------------

  if (
    restartPending &&
    (long)(
      millis() -
      restartAt
    ) >= 0
  ) {
    stopMotors(
      "RESTART"
    );

    armed = false;

    delay(100);

    ESP.restart();
  }


  delay(2);
}
