#include <WiFi.h>
#include <WebServer.h>

// ============================================================
// Wi-Fi
// ============================================================
const char* WIFI_SSID = "Osama";
const char* WIFI_PASSWORD = "123456789";

// ============================================================
// HC-SR04
// ============================================================
static const int TRIG_PIN = 4;
static const int ECHO_PIN = 34;

// يسمح بقياس المسافات البعيدة دون انتظار طويل جداً
static const unsigned long ECHO_TIMEOUT_US = 30000;

// الفترة الافتراضية بين القياسات
unsigned long measurementIntervalMs = 100;

// ============================================================
// Web Server
// ============================================================
WebServer server(80);

// ============================================================
// Measurement Data
// ============================================================
volatile unsigned long totalMeasurements = 0;
volatile unsigned long validMeasurements = 0;
volatile unsigned long timeoutCount = 0;

unsigned long lastEchoUs = 0;
unsigned long lastMeasurementAtMs = 0;
unsigned long lastMeasurementStartUs = 0;
unsigned long lastMeasurementGapUs = 0;
unsigned long measurementExecutionUs = 0;

float lastDistanceCm = -1.0;
float minDistanceCm = 99999.0;
float maxDistanceCm = 0.0;
double distanceSumCm = 0.0;

bool lastValid = false;
bool measurementsEnabled = true;

// ============================================================
// Utility
// ============================================================
String jsonBool(bool v) {
  return v ? "true" : "false";
}

String floatOrNull(float value, int decimals = 2) {
  if (value < 0) {
    return "null";
  }

  return String(value, decimals);
}

// ============================================================
// HC-SR04 Measurement
// ============================================================
void measureUltrasonic() {

  unsigned long startUs = micros();

  if (lastMeasurementStartUs != 0) {
    lastMeasurementGapUs =
      startUs - lastMeasurementStartUs;
  }

  lastMeasurementStartUs = startUs;

  // تأكد أن TRIG منخفض قبل النبضة
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(3);

  // نبضة التشغيل
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // انتظار نبضة Echo
  unsigned long echoUs =
    pulseIn(
      ECHO_PIN,
      HIGH,
      ECHO_TIMEOUT_US
    );

  measurementExecutionUs =
    micros() - startUs;

  lastEchoUs = echoUs;
  totalMeasurements++;

  if (echoUs == 0) {

    lastValid = false;
    lastDistanceCm = -1.0;
    timeoutCount++;

    return;
  }

  // زمن Echo هو زمن الرحلة ذهاباً وإياباً
  float distanceCm =
    (echoUs * 0.0343f) / 2.0f;

  lastDistanceCm = distanceCm;
  lastValid = true;

  validMeasurements++;

  distanceSumCm += distanceCm;

  if (distanceCm < minDistanceCm) {
    minDistanceCm = distanceCm;
  }

  if (distanceCm > maxDistanceCm) {
    maxDistanceCm = distanceCm;
  }
}

// ============================================================
// Statistics
// ============================================================
float averageDistance() {

  if (validMeasurements == 0) {
    return -1.0;
  }

  return
    distanceSumCm /
    (double)validMeasurements;
}

float actualReadingRateHz() {

  if (lastMeasurementGapUs == 0) {
    return 0.0;
  }

  return
    1000000.0f /
    (float)lastMeasurementGapUs;
}

void resetStatistics() {

  totalMeasurements = 0;
  validMeasurements = 0;
  timeoutCount = 0;

  lastEchoUs = 0;
  lastDistanceCm = -1;

  minDistanceCm = 99999;
  maxDistanceCm = 0;

  distanceSumCm = 0;

  lastMeasurementGapUs = 0;

  lastValid = false;
}

// ============================================================
// Web Page
// ============================================================
const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ar" dir="rtl">

<head>

<meta charset="UTF-8">

<meta name="viewport"
      content="width=device-width,initial-scale=1">

<title>HC-SR04 Test</title>

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
  max-width:1000px;
  margin:auto;
  padding:16px;
}

h1{
  margin:0;
  font-size:26px;
}

.subtitle{
  color:#94a3b8;
  margin:7px 0 18px;
}

.grid{
  display:grid;
  grid-template-columns:
    repeat(auto-fit,minmax(180px,1fr));
  gap:10px;
}

.card{
  background:#111827;
  border:1px solid #334155;
  border-radius:14px;
  padding:15px;
}

.label{
  color:#94a3b8;
  font-size:13px;
}

.value{
  margin-top:7px;
  font-size:25px;
  font-weight:bold;
  direction:ltr;
}

.good{
  color:#22c55e;
}

.bad{
  color:#ef4444;
}

.cyan{
  color:#22d3ee;
}

.controls{
  margin-top:12px;
  display:flex;
  flex-wrap:wrap;
  gap:8px;
}

button{
  border:0;
  border-radius:9px;
  padding:11px 14px;
  font-weight:bold;
  cursor:pointer;
  background:#2563eb;
  color:white;
}

button.secondary{
  background:#4338ca;
}

button.danger{
  background:#991b1b;
}

button.green{
  background:#047857;
}

.report{
  margin-top:12px;
  min-height:20px;
  color:#22d3ee;
}

canvas{
  width:100%;
  height:240px;
  background:#020617;
  border-radius:10px;
  margin-top:12px;
}

.small{
  color:#94a3b8;
  font-size:13px;
}

</style>
</head>

<body>

<div class="container">

<h1>HC-SR04 Diagnostic Test</h1>

<div class="subtitle">
اختبار حساس واحد — Front Ultrasonic
</div>

<div class="grid">

<div class="card">
<div class="label">المسافة الحالية</div>
<div class="value cyan" id="distance">--</div>
</div>

<div class="card">
<div class="label">Echo Time</div>
<div class="value" id="echo">--</div>
</div>

<div class="card">
<div class="label">الحالة</div>
<div class="value" id="valid">--</div>
</div>

<div class="card">
<div class="label">معدل القياس الفعلي</div>
<div class="value" id="rate">--</div>
</div>

<div class="card">
<div class="label">أقل مسافة</div>
<div class="value" id="min">--</div>
</div>

<div class="card">
<div class="label">أكبر مسافة</div>
<div class="value" id="max">--</div>
</div>

<div class="card">
<div class="label">متوسط المسافة</div>
<div class="value" id="avg">--</div>
</div>

<div class="card">
<div class="label">عدد القراءات</div>
<div class="value" id="total">--</div>
</div>

<div class="card">
<div class="label">Timeouts</div>
<div class="value" id="timeouts">--</div>
</div>

<div class="card">
<div class="label">مدة تنفيذ القياس</div>
<div class="value" id="execution">--</div>
</div>

<div class="card">
<div class="label">Wi-Fi RSSI</div>
<div class="value" id="rssi">--</div>
</div>

<div class="card">
<div class="label">زمن HTTP</div>
<div class="value" id="httpLatency">--</div>
</div>

</div>


<div class="card" style="margin-top:12px">

<div class="label">
الفترة بين القياسات
</div>

<div class="controls">

<button onclick="setIntervalMs(60)">
60 ms
</button>

<button onclick="setIntervalMs(100)">
100 ms
</button>

<button onclick="setIntervalMs(200)">
200 ms
</button>

<button onclick="setIntervalMs(500)">
500 ms
</button>

<button class="green"
        onclick="toggleMeasurements()">
تشغيل / إيقاف القياس
</button>

<button class="danger"
        onclick="resetStats()">
تصفير الإحصاءات
</button>

<button class="secondary"
        onclick="copyReport()">
نسخ التقرير
</button>

</div>

<div class="report"
     id="message">
</div>

<canvas id="graph"
        width="900"
        height="240">
</canvas>

<div class="small">
الرسم يعرض آخر القراءات الصحيحة.
</div>

</div>

</div>


<script>

let history = [];

let latestData = null;

const canvas =
  document.getElementById("graph");

const ctx =
  canvas.getContext("2d");


function numberOrDash(v, decimals=1){

  if(
    v === null ||
    v === undefined
  ){
    return "--";
  }

  return Number(v)
    .toFixed(decimals);
}


function drawGraph(){

  ctx.clearRect(
    0,
    0,
    canvas.width,
    canvas.height
  );

  if(history.length < 2)
    return;

  let max =
    Math.max(...history);

  let min =
    Math.min(...history);

  if(max === min){
    max += 1;
    min -= 1;
  }

  ctx.beginPath();

  history.forEach(
    (v,i)=>{

      let x =
        i /
        (history.length-1) *
        canvas.width;

      let y =
        canvas.height -
        (
          (v-min) /
          (max-min)
        ) *
        (canvas.height-20)
        -10;

      if(i === 0)
        ctx.moveTo(x,y);
      else
        ctx.lineTo(x,y);
    }
  );

  ctx.strokeStyle =
    "#22d3ee";

  ctx.lineWidth = 2;

  ctx.stroke();

  ctx.fillStyle =
    "#94a3b8";

  ctx.font =
    "13px monospace";

  ctx.fillText(
    "MAX " +
    max.toFixed(1) +
    " cm",
    10,
    18
  );

  ctx.fillText(
    "MIN " +
    min.toFixed(1) +
    " cm",
    10,
    canvas.height-8
  );
}


async function refresh(){

  try{

    let start =
      performance.now();

    let response =
      await fetch(
        "/api/status?t=" +
        Date.now()
      );

    let data =
      await response.json();

    let end =
      performance.now();

    latestData = data;

    document.getElementById(
      "distance"
    ).textContent =
      data.valid
      ? numberOrDash(
          data.distance_cm,
          2
        ) + " cm"
      : "--";

    document.getElementById(
      "echo"
    ).textContent =
      data.echo_us +
      " µs";

    let valid =
      document.getElementById(
        "valid"
      );

    if(data.valid){

      valid.textContent =
        "VALID";

      valid.className =
        "value good";

    }else{

      valid.textContent =
        "TIMEOUT";

      valid.className =
        "value bad";
    }

    document.getElementById(
      "rate"
    ).textContent =
      numberOrDash(
        data.reading_hz,
        2
      ) + " Hz";

    document.getElementById(
      "min"
    ).textContent =
      data.min_cm === null
      ? "--"
      : numberOrDash(
          data.min_cm,
          2
        ) + " cm";

    document.getElementById(
      "max"
    ).textContent =
      data.max_cm === null
      ? "--"
      : numberOrDash(
          data.max_cm,
          2
        ) + " cm";

    document.getElementById(
      "avg"
    ).textContent =
      data.avg_cm === null
      ? "--"
      : numberOrDash(
          data.avg_cm,
          2
        ) + " cm";

    document.getElementById(
      "total"
    ).textContent =
      data.total;

    document.getElementById(
      "timeouts"
    ).textContent =
      data.timeouts;

    document.getElementById(
      "execution"
    ).textContent =
      data.measurement_us +
      " µs";

    document.getElementById(
      "rssi"
    ).textContent =
      data.rssi +
      " dBm";

    document.getElementById(
      "httpLatency"
    ).textContent =
      Math.round(end-start) +
      " ms";

    if(
      data.valid &&
      data.distance_cm !== null
    ){

      history.push(
        data.distance_cm
      );

      if(history.length > 80)
        history.shift();

      drawGraph();
    }

  }catch(e){

    document.getElementById(
      "message"
    ).textContent =
      "الاتصال مع ESP32 مفقود.";
  }
}


async function setIntervalMs(ms){

  await fetch(
    "/api/interval?ms=" + ms
  );

  document.getElementById(
    "message"
  ).textContent =
    "تم ضبط فترة القياس على " +
    ms +
    " ms";
}


async function resetStats(){

  await fetch(
    "/api/reset",
    {method:"POST"}
  );

  history = [];

  drawGraph();

  document.getElementById(
    "message"
  ).textContent =
    "تم تصفير الإحصاءات.";
}


async function toggleMeasurements(){

  let r =
    await fetch(
      "/api/toggle",
      {method:"POST"}
    );

  let data =
    await r.json();

  document.getElementById(
    "message"
  ).textContent =
    data.enabled
    ? "القياس يعمل."
    : "تم إيقاف القياس.";
}


async function copyReport(){

  if(!latestData)
    return;

  let text =

`HC-SR04 TEST REPORT

Distance: ${latestData.distance_cm} cm
Echo: ${latestData.echo_us} us
Valid: ${latestData.valid}

Minimum: ${latestData.min_cm} cm
Maximum: ${latestData.max_cm} cm
Average: ${latestData.avg_cm} cm

Total readings: ${latestData.total}
Valid readings: ${latestData.valid_count}
Timeouts: ${latestData.timeouts}

Reading rate: ${latestData.reading_hz} Hz
Measurement execution: ${latestData.measurement_us} us
Configured interval: ${latestData.interval_ms} ms

WiFi RSSI: ${latestData.rssi} dBm
Uptime: ${latestData.uptime_ms} ms
IP: ${latestData.ip}
`;

  try{

    await navigator
      .clipboard
      .writeText(text);

    document.getElementById(
      "message"
    ).textContent =
      "تم نسخ تقرير الاختبار.";

  }catch(e){

    document.getElementById(
      "message"
    ).textContent =
      text;
  }
}


setInterval(
  refresh,
  250
);

refresh();

</script>

</body>
</html>
)rawliteral";


// ============================================================
// API
// ============================================================
void handleStatus() {

  float avg =
    averageDistance();

  String json = "{";

  json += "\"valid\":";
  json += jsonBool(lastValid);

  json += ",\"distance_cm\":";
  json +=
    lastValid
    ? String(lastDistanceCm, 3)
    : "null";

  json += ",\"echo_us\":";
  json += String(lastEchoUs);

  json += ",\"min_cm\":";

  if (validMeasurements > 0)
    json += String(minDistanceCm, 3);
  else
    json += "null";

  json += ",\"max_cm\":";

  if (validMeasurements > 0)
    json += String(maxDistanceCm, 3);
  else
    json += "null";

  json += ",\"avg_cm\":";

  if (avg >= 0)
    json += String(avg, 3);
  else
    json += "null";

  json += ",\"total\":";
  json += String(totalMeasurements);

  json += ",\"valid_count\":";
  json += String(validMeasurements);

  json += ",\"timeouts\":";
  json += String(timeoutCount);

  json += ",\"reading_hz\":";
  json += String(
    actualReadingRateHz(),
    3
  );

  json += ",\"measurement_us\":";
  json += String(
    measurementExecutionUs
  );

  json += ",\"interval_ms\":";
  json += String(
    measurementIntervalMs
  );

  json += ",\"enabled\":";
  json += jsonBool(
    measurementsEnabled
  );

  json += ",\"rssi\":";
  json += String(
    WiFi.RSSI()
  );

  json += ",\"uptime_ms\":";
  json += String(
    millis()
  );

  json += ",\"ip\":\"";
  json +=
    WiFi.localIP()
    .toString();

  json += "\"}";

  server.send(
    200,
    "application/json",
    json
  );
}


void handleInterval() {

  if (
    server.hasArg("ms")
  ) {

    long requested =
      server.arg("ms")
      .toInt();

    // لا نسمح بزمن قصير جداً
    if (requested < 60)
      requested = 60;

    if (requested > 2000)
      requested = 2000;

    measurementIntervalMs =
      requested;
  }

  server.send(
    200,
    "application/json",
    "{\"ok\":true}"
  );
}


void handleReset() {

  resetStatistics();

  server.send(
    200,
    "application/json",
    "{\"ok\":true}"
  );
}


void handleToggle() {

  measurementsEnabled =
    !measurementsEnabled;

  String result =
    "{\"enabled\":" +
    jsonBool(
      measurementsEnabled
    ) +
    "}";

  server.send(
    200,
    "application/json",
    result
  );
}


// ============================================================
// Wi-Fi
// ============================================================
void startWiFi() {

  Serial.println();
  Serial.println(
    "Connecting to Wi-Fi..."
  );

  WiFi.mode(
    WIFI_STA
  );

  WiFi.setSleep(
    false
  );

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  unsigned long start =
    millis();

  while (
    WiFi.status() !=
      WL_CONNECTED &&
    millis() - start <
      20000
  ) {

    delay(500);

    Serial.print(".");
  }

  Serial.println();

  if (
    WiFi.status() ==
    WL_CONNECTED
  ) {

    Serial.println(
      "Wi-Fi CONNECTED"
    );

    Serial.print(
      "WEB IP: "
    );

    Serial.println(
      WiFi.localIP()
    );

  } else {

    Serial.println(
      "Wi-Fi failed."
    );

    Serial.println(
      "Starting fallback AP."
    );

    WiFi.disconnect(
      true
    );

    delay(300);

    WiFi.mode(
      WIFI_AP
    );

    WiFi.softAP(
      "ESP32-HCSR04",
      "12345678"
    );

    Serial.print(
      "Fallback IP: "
    );

    Serial.println(
      WiFi.softAPIP()
    );
  }
}


// ============================================================
// Web Server
// ============================================================
void startWebServer() {

  server.on(
    "/",
    HTTP_GET,
    []() {

      server.send_P(
        200,
        "text/html; charset=utf-8",
        PAGE
      );
    }
  );

  server.on(
    "/api/status",
    HTTP_GET,
    handleStatus
  );

  server.on(
    "/api/interval",
    HTTP_GET,
    handleInterval
  );

  server.on(
    "/api/reset",
    HTTP_POST,
    handleReset
  );

  server.on(
    "/api/toggle",
    HTTP_POST,
    handleToggle
  );

  server.begin();

  Serial.println(
    "Web server started."
  );
}


// ============================================================
// Setup
// ============================================================
void setup() {

  Serial.begin(
    115200
  );

  delay(1000);

  pinMode(
    TRIG_PIN,
    OUTPUT
  );

  pinMode(
    ECHO_PIN,
    INPUT
  );

  digitalWrite(
    TRIG_PIN,
    LOW
  );

  Serial.println();
  Serial.println(
    "======================================"
  );

  Serial.println(
    " NES HC-SR04 SINGLE SENSOR TEST"
  );

  Serial.println(
    "======================================"
  );

  Serial.println(
    "TRIG: GPIO4"
  );

  Serial.println(
    "ECHO: GPIO34"
  );

  Serial.println(
    "IMPORTANT: Echo must use 1k/2k divider."
  );

  startWiFi();

  startWebServer();

  Serial.println(
    "READY."
  );
}


// ============================================================
// Loop
// ============================================================
void loop() {

  server.handleClient();

  if (
    measurementsEnabled &&
    millis() -
      lastMeasurementAtMs >=
      measurementIntervalMs
  ) {

    lastMeasurementAtMs =
      millis();

    measureUltrasonic();
  }

  delay(1);
}
