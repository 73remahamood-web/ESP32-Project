#include <WiFi.h>
#include <WebServer.h>

// ============================================================
// Wi-Fi
// ============================================================
const char* WIFI_SSID = "Osama";
const char* WIFI_PASSWORD = "123456789";

// ============================================================
// Target ESP32 UART
// ============================================================
// المشتبه بها TX0/GPIO1 -> مقاومة 4.7k -> GPIO16 في اللوحة السليمة
static const int TARGET_RX_PIN = 16;
static const uint32_t TARGET_BAUD = 115200;

HardwareSerial TargetSerial(2);
WebServer server(80);

// ============================================================
// Logging
// ============================================================
String logBuffer;
String targetLine;
String diagnosis = "Waiting for target ESP32 UART data...";

const size_t MAX_LOG_SIZE = 40000;

uint32_t receivedBytes = 0;
unsigned long lastByteAt = 0;
unsigned long bootTime = 0;

bool firstTargetByteReceived = false;
bool silenceReported = false;

// ============================================================
// Utility
// ============================================================
String uptimeString() {
  unsigned long s = millis() / 1000;

  unsigned long h = s / 3600;
  unsigned long m = (s % 3600) / 60;
  unsigned long sec = s % 60;

  char buf[32];
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", h, m, sec);

  return String(buf);
}

void trimLog() {
  if (logBuffer.length() > MAX_LOG_SIZE) {
    size_t removeCount = logBuffer.length() - MAX_LOG_SIZE;

    logBuffer.remove(0, removeCount);

    logBuffer =
      "\n[SYS] --- Older log data removed because buffer was full ---\n"
      + logBuffer;
  }
}

void appendRaw(char c) {
  logBuffer += c;
  trimLog();
}

void systemLog(const String& message) {
  String line =
    "[" + uptimeString() + "] [SYS] " + message + "\n";

  Serial.print(line);

  logBuffer += line;
  trimLog();
}

// ============================================================
// Analyze boot messages from target ESP32
// ============================================================
void analyzeTargetLine(const String& line) {

  if (line.indexOf("DOWNLOAD_BOOT") >= 0 ||
      line.indexOf("DOWNLOAD(USB/UART0)") >= 0) {

    diagnosis =
      "GOOD: ESP32 ROM bootloader detected. "
      "CPU/ROM/UART are alive and the board entered download mode.";

    systemLog("DIAGNOSIS: ROM DOWNLOAD BOOT detected.");
  }

  else if (line.indexOf("SPI_FAST_FLASH_BOOT") >= 0) {

    diagnosis =
      "GOOD: CPU and ROM are alive. "
      "The ESP32 is attempting a normal boot from SPI flash.";

    systemLog("DIAGNOSIS: Normal SPI flash boot detected.");
  }

  else if (line.indexOf("Brownout") >= 0 ||
           line.indexOf("brownout") >= 0) {

    diagnosis =
      "POWER WARNING: Brownout message detected. "
      "Power supply, cable or voltage stability may be the problem.";

    systemLog("DIAGNOSIS: Brownout detected.");
  }

  else if (line.indexOf("invalid header") >= 0) {

    diagnosis =
      "FLASH WARNING: Invalid firmware/flash header detected. "
      "The CPU is alive but flash contents may be corrupted.";

    systemLog("DIAGNOSIS: Invalid flash header detected.");
  }

  else if (line.indexOf("flash read err") >= 0 ||
           line.indexOf("SPI flash") >= 0 &&
           line.indexOf("error") >= 0) {

    diagnosis =
      "FLASH WARNING: ESP32 reported a flash read problem.";

    systemLog("DIAGNOSIS: Flash read problem detected.");
  }

  else if (line.indexOf("rst:") >= 0) {

    diagnosis =
      "ESP32 reset/boot message detected. "
      "The processor ROM is responding.";

    systemLog("DIAGNOSIS: ESP32 reset message detected.");
  }

  else if (line.indexOf("Guru Meditation") >= 0) {

    diagnosis =
      "APPLICATION CRASH: ESP32 is running but firmware is crashing.";

    systemLog("DIAGNOSIS: Guru Meditation detected.");
  }

  else if (line.indexOf("watchdog") >= 0 ||
           line.indexOf("WDT") >= 0) {

    diagnosis =
      "APPLICATION WARNING: Watchdog/reset condition detected.";

    systemLog("DIAGNOSIS: Watchdog indication detected.");
  }
}

// ============================================================
// Web UI
// ============================================================
const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ar" dir="rtl">
<head>
<meta charset="UTF-8">
<meta name="viewport"
      content="width=device-width,initial-scale=1">

<title>ESP32 Diagnostic Monitor</title>

<style>
*{
  box-sizing:border-box;
}

body{
  margin:0;
  background:#07101f;
  color:#f8fafc;
  font-family:Arial,Tahoma,sans-serif;
}

.container{
  max-width:1100px;
  margin:auto;
  padding:16px;
}

h1{
  margin:0 0 8px;
  font-size:25px;
}

.subtitle{
  color:#94a3b8;
  margin-bottom:18px;
}

.card{
  background:#111827;
  border:1px solid #334155;
  border-radius:14px;
  padding:15px;
  margin-bottom:14px;
}

.status{
  font-family:monospace;
  direction:ltr;
  text-align:left;
  white-space:pre-wrap;
  color:#22d3ee;
}

.buttons{
  display:flex;
  flex-wrap:wrap;
  gap:8px;
  margin-bottom:10px;
}

button,a.button{
  border:0;
  border-radius:9px;
  padding:11px 15px;
  font-weight:bold;
  cursor:pointer;
  text-decoration:none;
  background:#2563eb;
  color:white;
}

button.secondary{
  background:#4338ca;
}

button.danger{
  background:#991b1b;
}

#copyStatus{
  color:#22d3ee;
  margin:8px 0;
  min-height:20px;
}

pre{
  background:#020617;
  border:1px solid #334155;
  border-radius:10px;
  padding:12px;
  width:100%;
  height:55vh;
  overflow:auto;
  white-space:pre-wrap;
  overflow-wrap:anywhere;
  direction:ltr;
  text-align:left;
  font-family:monospace;
  font-size:13px;
}

.small{
  color:#94a3b8;
  font-size:13px;
}
</style>
</head>

<body>

<div class="container">

<h1>ESP32 Diagnostic Monitor</h1>

<div class="subtitle">
مراقبة UART للوحة ESP32 المشتبه بها
</div>

<div class="card">
<strong>التشخيص الحالي</strong>
<div id="diag" class="status">
Waiting...
</div>
</div>

<div class="card">

<div class="buttons">

<button onclick="copyLog()">
نسخ السجل كاملًا
</button>

<a class="button secondary"
   href="/download">
تنزيل السجل
</a>

<button class="danger"
        onclick="clearLog()">
مسح السجل
</button>

</div>

<div id="copyStatus"></div>

<div class="small">
يتم تحديث السجل تلقائيًا.
اضغط EN أو نفّذ اختبار BOOT على اللوحة المشتبه بها أثناء بقاء هذه الصفحة مفتوحة.
</div>

<pre id="log">Loading...</pre>

</div>

</div>

<script>

let autoScroll = true;

const logBox = document.getElementById("log");

logBox.addEventListener("scroll", () => {
  const distance =
    logBox.scrollHeight -
    logBox.scrollTop -
    logBox.clientHeight;

  autoScroll = distance < 80;
});

async function refreshLog(){

  try{

    const r = await fetch(
      "/api/log?t=" + Date.now()
    );

    const text = await r.text();

    logBox.textContent = text;

    if(autoScroll){
      logBox.scrollTop = logBox.scrollHeight;
    }

  }catch(e){}
}

async function refreshDiag(){

  try{

    const r = await fetch(
      "/api/diag?t=" + Date.now()
    );

    document.getElementById("diag").textContent =
      await r.text();

  }catch(e){}
}

async function copyLog(){

  const text = logBox.textContent;
  const status =
    document.getElementById("copyStatus");

  try{

    if(
      navigator.clipboard &&
      window.isSecureContext
    ){

      await navigator.clipboard.writeText(text);

    }else{

      const area =
        document.createElement("textarea");

      area.value = text;

      area.style.position = "fixed";
      area.style.left = "-9999px";

      document.body.appendChild(area);

      area.focus();
      area.select();

      document.execCommand("copy");

      area.remove();
    }

    status.textContent =
      "تم نسخ السجل كاملًا.";

  }catch(e){

    status.textContent =
      "تعذر النسخ التلقائي. استخدم زر تنزيل السجل.";

  }
}

async function clearLog(){

  await fetch(
    "/clear",
    {method:"POST"}
  );

  await refreshLog();
}

setInterval(refreshLog,500);
setInterval(refreshDiag,700);

refreshLog();
refreshDiag();

</script>

</body>
</html>
)rawliteral";

// ============================================================
// Wi-Fi
// ============================================================
void startWiFi() {

  systemLog("Starting Wi-Fi...");
  systemLog("SSID: " + String(WIFI_SSID));

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  Serial.print("[WIFI] Connecting");

  unsigned long start =
    millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - start < 20000
  ) {

    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (
    WiFi.status() ==
    WL_CONNECTED
  ) {

    systemLog(
      "Wi-Fi CONNECTED"
    );

    systemLog(
      "IP Address: " +
      WiFi.localIP().toString()
    );

    systemLog(
      "Open in browser: http://" +
      WiFi.localIP().toString()
    );

    Serial.println();
    Serial.println(
      "================================"
    );

    Serial.print(
      "WEB IP: "
    );

    Serial.println(
      WiFi.localIP()
    );

    Serial.println(
      "================================"
    );

    Serial.println();

  } else {

    systemLog(
      "WARNING: Failed to connect to Osama hotspot."
    );

    systemLog(
      "Starting emergency diagnostic Access Point."
    );

    WiFi.disconnect(true);
    delay(500);

    WiFi.mode(WIFI_AP);

    WiFi.softAP(
      "ESP32-DIAG",
      "12345678"
    );

    IPAddress ip =
      WiFi.softAPIP();

    systemLog(
      "Fallback AP: ESP32-DIAG"
    );

    systemLog(
      "Fallback password: 12345678"
    );

    systemLog(
      "Fallback IP: " +
      ip.toString()
    );
  }
}

// ============================================================
// Web server
// ============================================================
void startWebServer() {

  server.on(
    "/",
    HTTP_GET,
    [](){

      server.send_P(
        200,
        "text/html; charset=utf-8",
        PAGE
      );
    }
  );

  server.on(
    "/api/log",
    HTTP_GET,
    [](){

      server.send(
        200,
        "text/plain; charset=utf-8",
        logBuffer
      );
    }
  );

  server.on(
    "/api/diag",
    HTTP_GET,
    [](){

      String result;

      result +=
        "Diagnosis: " +
        diagnosis +
        "\n";

      result +=
        "UART bytes received: " +
        String(receivedBytes) +
        "\n";

      if(lastByteAt > 0){

        result +=
          "Last UART activity: " +
          String(
            (millis() - lastByteAt) /
            1000
          ) +
          " seconds ago\n";

      }else{

        result +=
          "Last UART activity: NONE\n";
      }

      result +=
        "Listening pin: GPIO16 / RX2\n";

      result +=
        "UART baud: 115200\n";

      if(
        WiFi.status() ==
        WL_CONNECTED
      ){

        result +=
          "Web IP: " +
          WiFi.localIP().toString();
      }

      server.send(
        200,
        "text/plain; charset=utf-8",
        result
      );
    }
  );

  server.on(
    "/download",
    HTTP_GET,
    [](){

      server.sendHeader(
        "Content-Disposition",
        "attachment; filename=esp32-diagnostic-log.txt"
      );

      server.send(
        200,
        "text/plain; charset=utf-8",
        logBuffer
      );
    }
  );

  server.on(
    "/clear",
    HTTP_POST,
    [](){

      logBuffer = "";
      targetLine = "";

      systemLog(
        "Web log manually cleared."
      );

      server.send(
        200,
        "text/plain",
        "OK"
      );
    }
  );

  server.begin();

  systemLog(
    "Web diagnostic server started."
  );
}

// ============================================================
// Setup
// ============================================================
void setup() {

  Serial.begin(115200);

  delay(1200);

  logBuffer.reserve(
    MAX_LOG_SIZE + 1024
  );

  targetLine.reserve(512);

  bootTime = millis();

  Serial.println();
  Serial.println();
  Serial.println(
    "============================================"
  );
  Serial.println(
    " ESP32 SAFE UART DIAGNOSTIC MONITOR"
  );
  Serial.println(
    "============================================"
  );

  systemLog(
    "Diagnostic ESP32 started."
  );

  systemLog(
    "IMPORTANT: This board is LISTENING ONLY."
  );

  systemLog(
    "Target TX0 -> 4.7k resistor -> GPIO16."
  );

  systemLog(
    "Target GND -> Diagnostic GND."
  );

  systemLog(
    "DO NOT connect 5V/3V3/VIN between boards."
  );

  TargetSerial.begin(
    TARGET_BAUD,
    SERIAL_8N1,
    TARGET_RX_PIN,
    -1
  );

  systemLog(
    "UART2 RX initialized on GPIO16 at 115200 baud."
  );

  startWiFi();
  startWebServer();

  systemLog(
    "READY."
  );

  systemLog(
    "Now reset the TARGET ESP32 using EN."
  );

  systemLog(
    "Then test BOOT + EN on the TARGET."
  );
}

// ============================================================
// Loop
// ============================================================
void loop() {

  server.handleClient();

  while(
    TargetSerial.available()
  ){

    int value =
      TargetSerial.read();

    if(value < 0)
      break;

    char c =
      (char)value;

    receivedBytes++;
    lastByteAt = millis();

    if(
      !firstTargetByteReceived
    ){

      firstTargetByteReceived = true;

      systemLog(
        "FIRST UART DATA RECEIVED FROM TARGET."
      );

      Serial.println(
        "----- TARGET UART START -----"
      );
    }

    // كل بايت من اللوحة المعطلة يطبع
    // مباشرة أيضاً على Serial Monitor
    Serial.write(
      (uint8_t)value
    );

    appendRaw(c);

    if(
      c == '\n' ||
      c == '\r'
    ){

      if(
        targetLine.length() > 0
      ){

        analyzeTargetLine(
          targetLine
        );

        targetLine = "";
      }

    }else{

      if(
        c >= 32 &&
        c <= 126
      ){

        targetLine += c;

      }else{

        // Non-printable byte marker
        char hexbuf[8];

        snprintf(
          hexbuf,
          sizeof(hexbuf),
          "<%02X>",
          (uint8_t)value
        );

        targetLine +=
          String(hexbuf);
      }

      if(
        targetLine.length() >
        600
      ){

        analyzeTargetLine(
          targetLine
        );

        targetLine = "";
      }
    }
  }

  // No signal warning
  if(
    !firstTargetByteReceived &&
    !silenceReported &&
    millis() - bootTime > 15000
  ){

    silenceReported = true;

    diagnosis =
      "NO UART DATA YET. "
      "Reset the target ESP32 with EN. "
      "If still silent, try BOOT+EN.";

    systemLog(
      "No target UART bytes detected during first 15 seconds."
    );
  }

  delay(1);
}
