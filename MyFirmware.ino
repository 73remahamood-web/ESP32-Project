#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "esp_system.h"
#include "esp_err.h"

// ============================================================
//                 إعدادات Wi-Fi
// ============================================================
const char* STA_SSID = "Osama";
const char* STA_PASS = "123456789";

const char* AP_SSID  = "ESP32-DIAG";
const char* AP_PASS  = "12345678";

// ============================================================
//              AI-Thinker ESP32-CAM PINOUT
// ============================================================
#define FLASH_LED         4

#define PWDN_GPIO_NUM    32
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM     0
#define SIOD_GPIO_NUM    26
#define SIOC_GPIO_NUM    27

#define Y9_GPIO_NUM      35
#define Y8_GPIO_NUM      34
#define Y7_GPIO_NUM      39
#define Y6_GPIO_NUM      36
#define Y5_GPIO_NUM      21
#define Y4_GPIO_NUM      19
#define Y3_GPIO_NUM      18
#define Y2_GPIO_NUM       5

#define VSYNC_GPIO_NUM   25
#define HREF_GPIO_NUM    23
#define PCLK_GPIO_NUM    22

// ============================================================
//                       النظام
// ============================================================
WebServer server(80);
Preferences prefs;

unsigned long bootMillis = 0;

uint32_t bootCount = 0;
uint32_t brownoutCount = 0;

esp_reset_reason_t bootResetReason = ESP_RST_UNKNOWN;

// ============================================================
//                       Wi-Fi
// ============================================================
bool apActive = false;

String staIp = "-";
String apIp  = "-";

int rssiValue = 0;

unsigned long lastWiFiCheck = 0;

// ============================================================
//                       الفلاش
// ============================================================
bool flashState = false;

// ============================================================
//                       الكاميرا
// ============================================================
bool cameraReady = false;

esp_err_t lastCameraErr = ESP_OK;

String cameraDetails = "لم تُهيّأ";
String frameDetails  = "لا يوجد إطار بعد";

uint16_t cameraPid = 0;

uint32_t frameCount = 0;
uint32_t frameFailCount = 0;

uint32_t cameraFailureStreak = 0;

unsigned long lastFrameMs = 0;
unsigned long lastAutoTestMs = 0;
unsigned long lastCameraInitAttemptMs = 0;

float diagnosticFps = 0.0f;

String autoTestResult = "لم يبدأ";

// ============================================================
//                     سجل الأحداث
// ============================================================
static const uint8_t LOG_CAP = 40;

String logBuffer[LOG_CAP];

uint8_t logHead = 0;
uint8_t logCount = 0;

// ============================================================
//                      إضافة سجل
// ============================================================
void addLog(const String& msg) {

    String line =
        "[" +
        String(millis() / 1000) +
        "s] " +
        msg;

    Serial.println(line);

    logBuffer[logHead] = line;

    logHead =
        (logHead + 1) % LOG_CAP;

    if (logCount < LOG_CAP) {
        logCount++;
    }
}

// ============================================================
//                  سبب إعادة التشغيل
// ============================================================
String resetReasonText(esp_reset_reason_t r) {

    switch (r) {

        case ESP_RST_POWERON:
            return "تشغيل بالطاقة";

        case ESP_RST_EXT:
            return "زر/إشارة Reset خارجية";

        case ESP_RST_SW:
            return "إعادة تشغيل برمجية";

        case ESP_RST_PANIC:
            return "Panic / انهيار برمجي";

        case ESP_RST_INT_WDT:
            return "Internal Watchdog";

        case ESP_RST_TASK_WDT:
            return "Task Watchdog";

        case ESP_RST_WDT:
            return "Watchdog";

        case ESP_RST_DEEPSLEEP:
            return "استيقاظ من Deep Sleep";

        case ESP_RST_BROWNOUT:
            return "انخفاض جهد BROWNOUT";

        case ESP_RST_SDIO:
            return "SDIO Reset";

        default:
            return "سبب آخر/غير معروف";
    }
}

// ============================================================
//                       JSON Escape
// ============================================================
String jsonEscape(const String& in) {

    String out;

    out.reserve(
        in.length() + 16
    );

    for (size_t i = 0; i < in.length(); ++i) {

        char c = in[i];

        if (
            c == '\\' ||
            c == '"'
        ) {

            out += '\\';
            out += c;

        } else if (c == '\n') {

            out += "\\n";

        } else if (c == '\r') {

            out += "\\r";

        } else if (c == '\t') {

            out += "\\t";

        } else {

            out += c;
        }
    }

    return out;
}

// ============================================================
//                     اسم حساس الكاميرا
// ============================================================
String sensorName(uint16_t pid) {

    if (pid == 0x26) {
        return "OV2640";
    }

    if (pid == 0x76) {
        return "OV7670";
    }

    return
        "Sensor PID 0x" +
        String(pid, HEX);
}

// ============================================================
//              عداد الإقلاع/Brownout الدائم
// ============================================================
void loadPersistentCounters() {

    prefs.begin(
        "diag",
        false
    );

    bootCount =
        prefs.getUInt(
            "boots",
            0
        ) + 1;

    prefs.putUInt(
        "boots",
        bootCount
    );

    brownoutCount =
        prefs.getUInt(
            "brownouts",
            0
        );

    if (
        bootResetReason ==
        ESP_RST_BROWNOUT
    ) {

        brownoutCount++;

        prefs.putUInt(
            "brownouts",
            brownoutCount
        );
    }

    prefs.end();
}

// ============================================================
//                    AP للطوارئ
// ============================================================
void startFallbackAP() {

    if (apActive) {
        return;
    }

    WiFi.mode(
        WIFI_AP_STA
    );

    if (
        WiFi.softAP(
            AP_SSID,
            AP_PASS
        )
    ) {

        apActive = true;

        apIp =
            WiFi
            .softAPIP()
            .toString();

        addLog(
            "تم تشغيل نقطة الطوارئ AP: " +
            apIp
        );

    } else {

        addLog(
            "فشل تشغيل نقطة الطوارئ AP"
        );
    }
}

// ============================================================
//                     اتصال Wi-Fi
// ============================================================
void connectWiFi() {

    WiFi.persistent(false);

    WiFi.mode(
        WIFI_STA
    );

    WiFi.setAutoReconnect(
        true
    );

    WiFi.begin(
        STA_SSID,
        STA_PASS
    );

    addLog(
        "محاولة الاتصال بشبكة Wi-Fi: " +
        String(STA_SSID)
    );

    unsigned long start =
        millis();

    while (
        WiFi.status() != WL_CONNECTED &&
        millis() - start < 15000
    ) {

        delay(250);
    }

    if (
        WiFi.status() ==
        WL_CONNECTED
    ) {

        staIp =
            WiFi
            .localIP()
            .toString();

        rssiValue =
            WiFi.RSSI();

        addLog(
            "Wi-Fi متصل: " +
            staIp +
            " / RSSI " +
            String(rssiValue) +
            " dBm"
        );

    } else {

        addLog(
            "لم ينجح STA خلال 15 ثانية؛ تشغيل AP للطوارئ"
        );

        startFallbackAP();
    }
}

// ============================================================
//                  مراقبة Wi-Fi المستمرة
// ============================================================
void maintainWiFi() {

    if (
        millis() - lastWiFiCheck <
        10000
    ) {

        return;
    }

    lastWiFiCheck =
        millis();

    if (
        WiFi.status() ==
        WL_CONNECTED
    ) {

        staIp =
            WiFi
            .localIP()
            .toString();

        rssiValue =
            WiFi.RSSI();

    } else {

        staIp = "-";
        rssiValue = 0;

        startFallbackAP();

        WiFi.reconnect();
    }

    if (apActive) {

        apIp =
            WiFi
            .softAPIP()
            .toString();
    }
}

// ============================================================
//                   تهيئة الكاميرا
// ============================================================
bool initCamera(bool isRetry) {

    lastCameraInitAttemptMs =
        millis();

    if (
        isRetry ||
        cameraReady
    ) {

        esp_camera_deinit();

        delay(120);
    }

    // مهم جداً:
    // تصفير جميع حقول الهيكل أولاً.
    camera_config_t config = {};

    config.ledc_channel =
        LEDC_CHANNEL_0;

    config.ledc_timer =
        LEDC_TIMER_0;

    config.pin_d0 =
        Y2_GPIO_NUM;

    config.pin_d1 =
        Y3_GPIO_NUM;

    config.pin_d2 =
        Y4_GPIO_NUM;

    config.pin_d3 =
        Y5_GPIO_NUM;

    config.pin_d4 =
        Y6_GPIO_NUM;

    config.pin_d5 =
        Y7_GPIO_NUM;

    config.pin_d6 =
        Y8_GPIO_NUM;

    config.pin_d7 =
        Y9_GPIO_NUM;

    config.pin_xclk =
        XCLK_GPIO_NUM;

    config.pin_pclk =
        PCLK_GPIO_NUM;

    config.pin_vsync =
        VSYNC_GPIO_NUM;

    config.pin_href =
        HREF_GPIO_NUM;

    config.pin_sccb_sda =
        SIOD_GPIO_NUM;

    config.pin_sccb_scl =
        SIOC_GPIO_NUM;

    config.pin_pwdn =
        PWDN_GPIO_NUM;

    config.pin_reset =
        RESET_GPIO_NUM;

    // القيمة القياسية للتشخيص.
    config.xclk_freq_hz =
        20000000;

    config.pixel_format =
        PIXFORMAT_JPEG;

    // نبدأ بدقة صغيرة لزيادة احتمال الاستقرار.
    config.frame_size =
        FRAMESIZE_QVGA;

    config.jpeg_quality =
        15;

    config.fb_count =
        1;

    config.grab_mode =
        CAMERA_GRAB_WHEN_EMPTY;

    config.jpeg_buffer_size =
        0;

    config.fb_location =
        psramFound()
        ? CAMERA_FB_IN_PSRAM
        : CAMERA_FB_IN_DRAM;

    addLog(
        String(
            isRetry
            ? "إعادة تهيئة"
            : "تهيئة"
        ) +
        " الكاميرا عند XCLK=20MHz..."
    );

    lastCameraErr =
        esp_camera_init(
            &config
        );

    if (
        lastCameraErr !=
        ESP_OK
    ) {

        cameraReady = false;

        cameraPid = 0;

        cameraDetails =
            "فشل esp_camera_init: 0x" +
            String(
                (uint32_t)lastCameraErr,
                HEX
            ) +
            " / " +
            String(
                esp_err_to_name(
                    lastCameraErr
                )
            );

        addLog(
            cameraDetails
        );

        return false;
    }

    sensor_t* s =
        esp_camera_sensor_get();

    if (!s) {

        cameraReady = false;

        cameraDetails =
            "esp_camera_sensor_get() أعاد NULL";

        addLog(
            cameraDetails
        );

        return false;
    }

    cameraPid =
        s->id.PID;

    cameraDetails =
        sensorName(
            cameraPid
        ) +
        " / PID=0x" +
        String(
            cameraPid,
            HEX
        ) +
        " / QVGA JPEG / XCLK=20MHz";

    cameraReady = true;

    cameraFailureStreak = 0;

    addLog(
        "الكاميرا جاهزة: " +
        cameraDetails
    );

    return true;
}

// ============================================================
//                  تسجيل إطار ناجح
// ============================================================
void noteFrame(
    camera_fb_t* fb
) {

    if (!fb) {
        return;
    }

    frameCount++;

    cameraFailureStreak = 0;

    lastFrameMs =
        millis();

    frameDetails =
        String(fb->len) +
        " bytes / " +
        String(fb->width) +
        "x" +
        String(fb->height) +
        " / format=" +
        String(
            (int)fb->format
        );
}

// ============================================================
//                    اختبار إطار واحد
// ============================================================
bool singleCaptureTest(
    bool verboseLog
) {

    if (!cameraReady) {

        if (verboseLog) {

            addLog(
                "اختبار الالتقاط أُلغي: الكاميرا غير جاهزة"
            );
        }

        return false;
    }

    camera_fb_t* fb =
        esp_camera_fb_get();

    if (!fb) {

        frameFailCount++;

        cameraFailureStreak++;

        frameDetails =
            "esp_camera_fb_get() أعاد NULL";

        if (verboseLog) {

            addLog(
                "فشل التقاط إطار"
            );
        }

        return false;
    }

    noteFrame(fb);

    esp_camera_fb_return(
        fb
    );

    if (verboseLog) {

        addLog(
            "نجح التقاط إطار: " +
            frameDetails
        );
    }

    return true;
}

// ============================================================
//                اختبار مجموعة إطارات
// ============================================================
void runBurstDiagnostic(
    uint8_t attempts,
    bool verboseLog
) {

    if (!cameraReady) {

        diagnosticFps =
            0.0f;

        autoTestResult =
            "الكاميرا غير جاهزة";

        return;
    }

    unsigned long t0 =
        millis();

    uint8_t ok = 0;

    for (
        uint8_t i = 0;
        i < attempts;
        ++i
    ) {

        if (
            singleCaptureTest(false)
        ) {

            ok++;
        }

        delay(20);
    }

    unsigned long elapsed =
        millis() - t0;

    diagnosticFps =
        elapsed > 0
        ? (
            ok *
            1000.0f /
            elapsed
        )
        : 0.0f;

    autoTestResult =
        String(ok) +
        "/" +
        String(attempts) +
        " إطارات نجحت";

    if (verboseLog) {

        addLog(
            "اختبار الكاميرا: " +
            autoTestResult +
            " / " +
            String(
                diagnosticFps,
                2
            ) +
            " fps تقريبي"
        );
    }
}

// ============================================================
//                    الفحص التلقائي
// ============================================================
void autoDiagnostics() {

    // اختبار كل 30 ثانية.
    if (
        millis() -
        lastAutoTestMs <
        30000
    ) {

        return;
    }

    lastAutoTestMs =
        millis();

    if (cameraReady) {

        bool ok =
            singleCaptureTest(
                false
            );

        autoTestResult =
            ok
            ? "اختبار تلقائي ناجح"
            : "اختبار تلقائي فشل";

    } else {

        autoTestResult =
            "الكاميرا غير جاهزة — محاولة إصلاح تلقائي";
    }

    // إذا استمرت المشكلة:
    // لا نعيد التهيئة بشكل سريع ومتكرر.
    if (
        (
            !cameraReady ||
            cameraFailureStreak >= 3
        ) &&
        millis() -
        lastCameraInitAttemptMs >
        60000
    ) {

        addLog(
            "بدء محاولة إصلاح تلقائي للكاميرا"
        );

        if (
            initCamera(true)
        ) {

            runBurstDiagnostic(
                2,
                true
            );
        }
    }
}

// ============================================================
//                      صفحة الويب
// ============================================================
const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>

<html
    lang="ar"
    dir="rtl"
>

<head>

<meta charset="utf-8">

<meta
    name="viewport"
    content="width=device-width,initial-scale=1,viewport-fit=cover"
>

<title>
ESP32-CAM Diagnostic Center
</title>

<style>

:root {
    --bg:#0b1020;
    --card:#121a2e;
    --card2:#0f1729;
    --text:#eef3ff;
    --muted:#9fb0cf;
    --line:#24304d;
    --ok:#2bd576;
    --bad:#ff5d6c;
    --warn:#ffbc42;
    --blue:#4da3ff;
}

* {
    box-sizing:border-box;
}

body {

    margin:0;

    background:
        linear-gradient(
            160deg,
            #09101d,
            #0e1630 55%,
            #08101d
        );

    color:var(--text);

    font-family:
        system-ui,
        -apple-system,
        "Segoe UI",
        Tahoma,
        Arial,
        sans-serif;

    min-height:100vh;
}

.wrap {

    max-width:1080px;

    margin:auto;

    padding:16px;
}

.top {

    display:flex;

    gap:12px;

    align-items:center;

    justify-content:
        space-between;

    flex-wrap:wrap;

    margin-bottom:14px;
}

h1 {

    font-size:22px;

    margin:0;
}

.live {

    display:inline-flex;

    align-items:center;

    gap:7px;

    color:var(--ok);

    font-size:13px;
}

.dot {

    width:9px;

    height:9px;

    border-radius:50%;

    background:currentColor;

    box-shadow:
        0 0 14px currentColor;
}

.grid {

    display:grid;

    grid-template-columns:
        repeat(
            12,
            1fr
        );

    gap:12px;
}

.card {

    grid-column:span 4;

    background:
        rgba(
            18,
            26,
            46,
            .94
        );

    border:
        1px solid var(--line);

    border-radius:18px;

    padding:14px;

    box-shadow:
        0 10px 30px
        rgba(
            0,
            0,
            0,
            .20
        );
}

.card.wide {

    grid-column:
        span 8;
}

.card.full {

    grid-column:
        1/-1;
}

.card h2 {

    font-size:15px;

    margin:
        0 0 12px;

    color:#d9e5ff;
}

.rows {

    display:grid;

    gap:8px;
}

.row {

    display:flex;

    justify-content:
        space-between;

    gap:12px;

    border-bottom:
        1px dashed #253250;

    padding-bottom:7px;
}

.row:last-child {

    border-bottom:0;
}

.k {

    color:
        var(--muted);

    font-size:13px;
}

.v {

    font-size:13px;

    text-align:left;

    word-break:
        break-word;
}

.preview {

    background:#020409;

    border:
        1px solid #253250;

    border-radius:14px;

    aspect-ratio:4/3;

    overflow:hidden;

    display:flex;

    align-items:center;

    justify-content:center;
}

.preview img {

    width:100%;

    height:100%;

    object-fit:contain;
}

.hint {

    font-size:12px;

    color:
        var(--muted);

    line-height:1.7;

    margin-top:10px;
}

.actions {

    display:flex;

    flex-wrap:wrap;

    gap:8px;

    margin-top:12px;
}

.btn {

    border:0;

    border-radius:12px;

    padding:
        11px 14px;

    font-weight:700;

    color:white;

    background:#263450;

    cursor:pointer;
}

.btn.blue {
    background:#1976d2;
}

.btn.green {
    background:#159957;
}

.btn.orange {
    background:#c77700;
}

.btn.red {
    background:#b93d4a;
}

.btn:active {

    transform:
        translateY(1px);
}

pre {

    margin:0;

    background:#060a13;

    border:
        1px solid #253250;

    border-radius:14px;

    padding:12px;

    min-height:220px;

    max-height:330px;

    overflow:auto;

    white-space:
        pre-wrap;

    direction:ltr;

    text-align:left;

    color:#c9d7ef;

    font-size:12px;
}

@media(
    max-width:820px
) {

    .card,
    .card.wide {

        grid-column:
            1/-1;
    }

    .wrap {

        padding:10px;
    }

    h1 {

        font-size:19px;
    }
}

</style>

</head>

<body>

<div class="wrap">

<div class="top">

<div>

<h1>
ESP32-CAM Diagnostic Center
</h1>

<div class="hint">
مراقبة مباشرة للكاميرا، الإقلاع، الذاكرة، Wi-Fi والفلاش
</div>

</div>

<div class="live">

<span class="dot"></span>

<span id="liveText">
جاري الاتصال...
</span>

</div>

</div>

<div class="grid">

<section class="card wide">

<h2>
📷 الكاميرا
</h2>

<div class="preview">

<img
    id="cam"
    alt="Camera preview"
>

</div>

<div class="actions">

<button
    class="btn blue"
    onclick="captureNow()"
>
التقاط الآن
</button>

<button
    class="btn orange"
    onclick="runTest()"
>
اختبار شامل الآن
</button>

<button
    class="btn red"
    onclick="reinitCamera()"
>
إعادة تهيئة الكاميرا
</button>

<button
    id="flashBtn"
    class="btn green"
    onclick="toggleFlash()"
>
تشغيل الفلاش
</button>

</div>

<div class="hint">
المعاينة عبارة عن صور JPEG متجددة باستمرار.
استخدمت هذه الطريقة في مرحلة التشخيص لأنها أبسط وأكثر ثباتًا من إبقاء اتصال MJPEG مفتوحًا طوال الوقت.
</div>

</section>

<section class="card">

<h2>
⚡ الطاقة وإعادة الإقلاع
</h2>

<div class="rows">

<div class="row">
<span class="k">سبب الإقلاع</span>
<span class="v" id="resetReason">-</span>
</div>

<div class="row">
<span class="k">عدد الإقلاعات</span>
<span class="v" id="bootCount">-</span>
</div>

<div class="row">
<span class="k">Brownout المسجل</span>
<span class="v" id="brownouts">-</span>
</div>

<div class="row">
<span class="k">حالة الجهد</span>
<span class="v" id="voltage">-</span>
</div>

<div class="row">
<span class="k">وقت التشغيل</span>
<span class="v" id="uptime">-</span>
</div>

<div class="row">
<span class="k">حرارة ESP32</span>
<span class="v" id="temp">-</span>
</div>

</div>

</section>

<section class="card">

<h2>
📡 Wi-Fi
</h2>

<div class="rows">

<div class="row">
<span class="k">الحالة</span>
<span class="v" id="wifi">-</span>
</div>

<div class="row">
<span class="k">STA IP</span>
<span class="v" id="staIp">-</span>
</div>

<div class="row">
<span class="k">RSSI</span>
<span class="v" id="rssi">-</span>
</div>

<div class="row">
<span class="k">AP طوارئ</span>
<span class="v" id="ap">-</span>
</div>

<div class="row">
<span class="k">AP IP</span>
<span class="v" id="apIp">-</span>
</div>

</div>

</section>

<section class="card">

<h2>
🧠 الذاكرة والنظام
</h2>

<div class="rows">

<div class="row">
<span class="k">الشريحة</span>
<span class="v" id="chip">-</span>
</div>

<div class="row">
<span class="k">CPU</span>
<span class="v" id="cpu">-</span>
</div>

<div class="row">
<span class="k">Heap حر</span>
<span class="v" id="heap">-</span>
</div>

<div class="row">
<span class="k">أقل Heap</span>
<span class="v" id="minHeap">-</span>
</div>

<div class="row">
<span class="k">PSRAM</span>
<span class="v" id="psram">-</span>
</div>

<div class="row">
<span class="k">PSRAM حرة</span>
<span class="v" id="psramFree">-</span>
</div>

</div>

</section>

<section class="card full">

<h2>
🔬 نتائج الكاميرا والفحص التلقائي
</h2>

<div class="rows">

<div class="row">
<span class="k">جاهزية الكاميرا</span>
<span class="v" id="cameraReady">-</span>
</div>

<div class="row">
<span class="k">تفاصيل الحساس</span>
<span class="v" id="cameraDetails">-</span>
</div>

<div class="row">
<span class="k">آخر إطار</span>
<span class="v" id="frameDetails">-</span>
</div>

<div class="row">
<span class="k">الإطارات الناجحة/الفاشلة</span>
<span class="v" id="frames">-</span>
</div>

<div class="row">
<span class="k">FPS اختبار قصير</span>
<span class="v" id="fps">-</span>
</div>

<div class="row">
<span class="k">آخر اختبار تلقائي</span>
<span class="v" id="autoTest">-</span>
</div>

<div class="row">
<span class="k">الفلاش</span>
<span class="v" id="flash">-</span>
</div>

</div>

</section>

<section class="card full">

<h2>
🧾 السجل المباشر
</h2>

<pre id="logs">
جاري تحميل السجل...
</pre>

</section>

</div>

</div>

<script>

let lastStatus = null;

const $ =
    id =>
    document.getElementById(id);

const set =
    (id, value) => {
        $(id).textContent =
            value;
    };

function fmtBytes(n) {

    if (n < 1024) {
        return n + " B";
    }

    if (n < 1048576) {
        return (
            n / 1024
        ).toFixed(1) + " KB";
    }

    return (
        n / 1048576
    ).toFixed(2) + " MB";
}

async function refreshStatus() {

    try {

        const r =
            await fetch(
                "/api/status?t=" +
                Date.now(),
                {
                    cache:
                        "no-store"
                }
            );

        if (!r.ok) {

            throw new Error(
                "HTTP " +
                r.status
            );
        }

        const s =
            await r.json();

        lastStatus = s;

        set(
            "liveText",
            "متصل — تحديث لحظي"
        );

        set(
            "resetReason",
            s.resetReason
        );

        set(
            "bootCount",
            s.bootCount
        );

        set(
            "brownouts",
            s.brownoutCount
        );

        set(
            "voltage",
            s.voltageStatus
        );

        set(
            "uptime",
            s.uptime +
            " ثانية"
        );

        set(
            "temp",
            s.temperature.toFixed(1) +
            " °C"
        );

        set(
            "wifi",
            s.wifiStatus
        );

        set(
            "staIp",
            s.staIp
        );

        set(
            "rssi",
            s.rssi +
            " dBm"
        );

        set(
            "ap",
            s.apActive
            ? "يعمل"
            : "متوقف"
        );

        set(
            "apIp",
            s.apIp
        );

        set(
            "chip",
            s.chip
        );

        set(
            "cpu",
            s.cpuMHz +
            " MHz"
        );

        set(
            "heap",
            fmtBytes(
                s.heapFree
            )
        );

        set(
            "minHeap",
            fmtBytes(
                s.minHeap
            )
        );

        set(
            "psram",
            s.psramFound
            ? (
                fmtBytes(
                    s.psramSize
                ) +
                " ✅"
            )
            : "غير موجودة ❌"
        );

        set(
            "psramFree",
            fmtBytes(
                s.psramFree
            )
        );

        set(
            "cameraReady",
            s.cameraReady
            ? "جاهزة ✅"
            : "غير جاهزة ❌"
        );

        set(
            "cameraDetails",
            s.cameraDetails
        );

        set(
            "frameDetails",
            s.frameDetails
        );

        set(
            "frames",
            s.frameCount +
            " / " +
            s.frameFailCount
        );

        set(
            "fps",
            s.diagnosticFps
            .toFixed(2)
        );

        set(
            "autoTest",
            s.autoTestResult
        );

        set(
            "flash",
            s.flash
            ? "مضاء"
            : "مطفأ"
        );

        $("flashBtn").textContent =
            s.flash
            ? "إطفاء الفلاش"
            : "تشغيل الفلاش";

    } catch(e) {

        set(
            "liveText",
            "انقطع الاتصال: " +
            e.message
        );
    }
}

async function refreshLogs() {

    try {

        const r =
            await fetch(
                "/api/logs?t=" +
                Date.now(),
                {
                    cache:
                        "no-store"
                }
            );

        $("logs").textContent =
            await r.text();

        $("logs").scrollTop =
            $("logs").scrollHeight;

    } catch(e) {

    }
}

function refreshCamera() {

    if (
        lastStatus &&
        lastStatus.cameraReady
    ) {

        $("cam").src =
            "/capture?t=" +
            Date.now();
    }
}

async function toggleFlash() {

    await fetch(
        "/flash/toggle",
        {
            method:"POST"
        }
    );

    await refreshStatus();
}

async function runTest() {

    await fetch(
        "/diagnostic/run",
        {
            method:"POST"
        }
    );

    await refreshStatus();

    await refreshLogs();

    refreshCamera();
}

async function reinitCamera() {

    await fetch(
        "/camera/reinit",
        {
            method:"POST"
        }
    );

    await refreshStatus();

    await refreshLogs();

    refreshCamera();
}

function captureNow() {

    window.open(
        "/capture?t=" +
        Date.now(),
        "_blank"
    );
}

$("cam").onerror =
    () => {};

refreshStatus();

refreshLogs();

setInterval(
    refreshStatus,
    1000
);

setInterval(
    refreshLogs,
    2000
);

setInterval(
    refreshCamera,
    1500
);

setTimeout(
    refreshCamera,
    800
);

</script>

</body>

</html>
)HTML";

// ============================================================
//                         الصفحة
// ============================================================
void handleRoot() {

    server.send_P(
        200,
        "text/html; charset=utf-8",
        INDEX_HTML
    );
}

// ============================================================
//                 وصف حالة الجهد
// ============================================================
String currentVoltageStatus() {

    if (
        bootResetReason ==
        ESP_RST_BROWNOUT
    ) {

        return
            "آخر إقلاع جاء بعد BROWNOUT؛ افحص مصدر 5V والكابل";
    }

    return
        "لا يمكن قياس 5V فعلياً برمجياً دون دائرة قياس؛ لا يوجد BROWNOUT في سبب الإقلاع الحالي";
}

// ============================================================
//                    وصف Wi-Fi
// ============================================================
String currentWiFiStatus() {

    if (
        WiFi.status() ==
        WL_CONNECTED
    ) {

        return
            "STA متصل بالشبكة";
    }

    if (apActive) {

        return
            "STA غير متصل / AP طوارئ يعمل";
    }

    return
        "غير متصل";
}

// ============================================================
//                       API Status
// ============================================================
void handleStatus() {

    String json;

    json.reserve(
        2600
    );

    json += "{";

    json +=
        "\"uptime\":" +
        String(
            (
                millis() -
                bootMillis
            ) /
            1000
        ) +
        ",";

    json +=
        "\"resetReason\":\"" +
        jsonEscape(
            resetReasonText(
                bootResetReason
            )
        ) +
        "\",";

    json +=
        "\"bootCount\":" +
        String(
            bootCount
        ) +
        ",";

    json +=
        "\"brownoutCount\":" +
        String(
            brownoutCount
        ) +
        ",";

    json +=
        "\"voltageStatus\":\"" +
        jsonEscape(
            currentVoltageStatus()
        ) +
        "\",";

    json +=
        "\"wifiStatus\":\"" +
        jsonEscape(
            currentWiFiStatus()
        ) +
        "\",";

    json +=
        "\"staIp\":\"" +
        jsonEscape(
            staIp
        ) +
        "\",";

    json +=
        "\"rssi\":" +
        String(
            rssiValue
        ) +
        ",";

    json +=
        "\"apActive\":" +
        String(
            apActive
            ? "true"
            : "false"
        ) +
        ",";

    json +=
        "\"apIp\":\"" +
        jsonEscape(
            apIp
        ) +
        "\",";

    json +=
        "\"psramFound\":" +
        String(
            psramFound()
            ? "true"
            : "false"
        ) +
        ",";

    json +=
        "\"psramSize\":" +
        String(
            ESP.getPsramSize()
        ) +
        ",";

    json +=
        "\"psramFree\":" +
        String(
            ESP.getFreePsram()
        ) +
        ",";

    json +=
        "\"heapFree\":" +
        String(
            ESP.getFreeHeap()
        ) +
        ",";

    json +=
        "\"minHeap\":" +
        String(
            ESP.getMinFreeHeap()
        ) +
        ",";

    json +=
        "\"chip\":\"" +
        jsonEscape(
            String(
                ESP.getChipModel()
            )
        ) +
        "\",";

    json +=
        "\"cpuMHz\":" +
        String(
            ESP.getCpuFreqMHz()
        ) +
        ",";

    json +=
        "\"temperature\":" +
        String(
            temperatureRead(),
            1
        ) +
        ",";

    json +=
        "\"cameraReady\":" +
        String(
            cameraReady
            ? "true"
            : "false"
        ) +
        ",";

    json +=
        "\"cameraDetails\":\"" +
        jsonEscape(
            cameraDetails
        ) +
        "\",";

    json +=
        "\"frameDetails\":\"" +
        jsonEscape(
            frameDetails
        ) +
        "\",";

    json +=
        "\"frameCount\":" +
        String(
            frameCount
        ) +
        ",";

    json +=
        "\"frameFailCount\":" +
        String(
            frameFailCount
        ) +
        ",";

    json +=
        "\"diagnosticFps\":" +
        String(
            diagnosticFps,
            2
        ) +
        ",";

    json +=
        "\"autoTestResult\":\"" +
        jsonEscape(
            autoTestResult
        ) +
        "\",";

    json +=
        "\"flash\":" +
        String(
            flashState
            ? "true"
            : "false"
        );

    json += "}";

    server.sendHeader(
        "Cache-Control",
        "no-store"
    );

    server.send(
        200,
        "application/json; charset=utf-8",
        json
    );
}

// ============================================================
//                        API Logs
// ============================================================
void handleLogs() {

    String out;

    out.reserve(
        5000
    );

    uint8_t start =
        (
            logHead +
            LOG_CAP -
            logCount
        ) %
        LOG_CAP;

    for (
        uint8_t i = 0;
        i < logCount;
        ++i
    ) {

        uint8_t idx =
            (
                start +
                i
            ) %
            LOG_CAP;

        out +=
            logBuffer[idx];

        out += '\n';
    }

    server.sendHeader(
        "Cache-Control",
        "no-store"
    );

    server.send(
        200,
        "text/plain; charset=utf-8",
        out
    );
}

// ============================================================
//                      Toggle Flash
// ============================================================
void handleToggleFlash() {

    flashState =
        !flashState;

    digitalWrite(
        FLASH_LED,
        flashState
        ? HIGH
        : LOW
    );

    addLog(
        String(
            "الفلاش: "
        ) +
        (
            flashState
            ? "مضاء"
            : "مطفأ"
        )
    );

    server.send(
        200,
        "text/plain; charset=utf-8",
        flashState
        ? "1"
        : "0"
    );
}

// ============================================================
//                       Capture JPEG
// ============================================================
void handleCapture() {

    if (!cameraReady) {

        server.send(
            503,
            "text/plain; charset=utf-8",
            "Camera not ready: " +
            cameraDetails
        );

        return;
    }

    camera_fb_t* fb =
        esp_camera_fb_get();

    if (!fb) {

        frameFailCount++;

        cameraFailureStreak++;

        frameDetails =
            "esp_camera_fb_get() أعاد NULL";

        server.send(
            500,
            "text/plain; charset=utf-8",
            "esp_camera_fb_get failed"
        );

        return;
    }

    noteFrame(
        fb
    );

    WiFiClient client =
        server.client();

    client.print(
        "HTTP/1.1 200 OK\r\n"
    );

    client.print(
        "Content-Type: image/jpeg\r\n"
    );

    client.print(
        "Cache-Control: no-store, no-cache, must-revalidate\r\n"
    );

    client.print(
        "Pragma: no-cache\r\n"
    );

    client.print(
        "Content-Length: "
    );

    client.print(
        fb->len
    );

    client.print(
        "\r\n"
        "Connection: close\r\n"
        "\r\n"
    );

    client.write(
        fb->buf,
        fb->len
    );

    esp_camera_fb_return(
        fb
    );
}

// ============================================================
//                    اختبار يدوي
// ============================================================
void handleRunDiagnostic() {

    runBurstDiagnostic(
        3,
        true
    );

    server.send(
        200,
        "text/plain; charset=utf-8",
        autoTestResult
    );
}

// ============================================================
//                إعادة تهيئة الكاميرا
// ============================================================
void handleReinitCamera() {

    bool ok =
        initCamera(
            true
        );

    if (ok) {

        runBurstDiagnostic(
            2,
            true
        );
    }

    server.send(
        ok
        ? 200
        : 500,
        "text/plain; charset=utf-8",
        cameraDetails
    );
}

// ============================================================
//                    Web routes
// ============================================================
void setupWebServer() {

    server.on(
        "/",
        HTTP_GET,
        handleRoot
    );

    server.on(
        "/api/status",
        HTTP_GET,
        handleStatus
    );

    server.on(
        "/api/logs",
        HTTP_GET,
        handleLogs
    );

    server.on(
        "/capture",
        HTTP_GET,
        handleCapture
    );

    server.on(
        "/flash/toggle",
        HTTP_POST,
        handleToggleFlash
    );

    server.on(
        "/diagnostic/run",
        HTTP_POST,
        handleRunDiagnostic
    );

    server.on(
        "/camera/reinit",
        HTTP_POST,
        handleReinitCamera
    );

    server.onNotFound(
        []() {

            server.send(
                404,
                "text/plain; charset=utf-8",
                "Not found"
            );
        }
    );

    server.begin();

    addLog(
        "خادم الويب بدأ على المنفذ 80"
    );
}

// ============================================================
//                         SETUP
// ============================================================
void setup() {

    Serial.begin(
        115200
    );

    delay(
        800
    );

    bootMillis =
        millis();

    bootResetReason =
        esp_reset_reason();

    loadPersistentCounters();

    // ---------------- Flash LED ----------------
    pinMode(
        FLASH_LED,
        OUTPUT
    );

    digitalWrite(
        FLASH_LED,
        LOW
    );

    flashState =
        false;

    // ---------------- Startup log ----------------
    addLog(
        "========================================"
    );

    addLog(
        "ESP32-CAM Diagnostic Center"
    );

    addLog(
        "سبب الإقلاع: " +
        resetReasonText(
            bootResetReason
        )
    );

    addLog(
        "Boot #" +
        String(
            bootCount
        ) +
        " / Brownouts total: " +
        String(
            brownoutCount
        )
    );

    addLog(
        String(
            "PSRAM: "
        ) +
        (
            psramFound()
            ? "موجودة"
            : "غير موجودة"
        ) +
        " / total=" +
        String(
            ESP.getPsramSize()
        ) +
        " / free=" +
        String(
            ESP.getFreePsram()
        )
    );

    // ---------------- Wi-Fi ----------------
    connectWiFi();

    // ---------------- Web server ----------------
    setupWebServer();

    // ---------------- Camera ----------------
    if (
        initCamera(false)
    ) {

        runBurstDiagnostic(
            2,
            true
        );

    } else {

        autoTestResult =
            "فشل التهيئة الأولية؛ ستتم إعادة المحاولة تلقائياً";
    }

    String access =
        WiFi.status() ==
        WL_CONNECTED
        ? staIp
        : apIp;

    addLog(
        "افتح في المتصفح: http://" +
        access
    );

    addLog(
        "========================================"
    );
}

// ============================================================
//                          LOOP
// ============================================================
void loop() {

    server.handleClient();

    maintainWiFi();

    autoDiagnostics();

    delay(
        2
    );
}
