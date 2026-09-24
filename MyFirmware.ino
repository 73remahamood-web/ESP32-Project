#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include "esp_system.h"

// ================= إعدادات =================
const char* STA_SSID = "Osama";
const char* STA_PASS = "123456789";
const char* AP_SSID  = "ESP32-DIAG";
const char* AP_PASS  = "12345678";
#define FLASH_LED 4

// ================= دبابيس الكاميرا =================
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ================= حالة النظام =================
WebServer server(80);
bool flashState = false;
unsigned long bootMillis = 0;
int bootCount = 0;

String testWiFi   = "لم يُختبر";
String testPSRAM  = "لم يُختبر";
String testCamera = "لم يُختبر";
String testFrame  = "لم يُختبر";
String testFlash  = "لم يُختبر";
String testReason = "غير معروف";
String cameraDetails = "-";
String frameDetails = "-";
String ipAddress = "-";
int rssiValue = 0;
bool cameraReady = false;

// ================= سبب الإقلاع =================
String getResetReason() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:  return "طاقة جديدة ✅";
        case ESP_RST_BROWNOUT: return "انخفاض جهد 🔋 (BROWNOUT)";
        case ESP_RST_PANIC:    return "انهيار ⚠️";
        case ESP_RST_INT_WDT:  return "Watchdog داخلي";
        case ESP_RST_TASK_WDT: return "Task Watchdog";
        case ESP_RST_WDT:      return "Watchdog";
        case ESP_RST_SW:       return "إعادة برمجية";
        case ESP_RST_EXT:      return "زر RST";
        default:               return "أخرى";
    }
}

// ================= صفحة الويب =================
String buildPage() {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<meta http-equiv='refresh' content='3'>";
    html += "<title>ESP32 Diagnostic</title>";
    html += "<style>";
    html += "body{font-family:Arial;padding:15px;background:#f0f0f0;text-align:center;margin:0}";
    html += "h1{color:#333;font-size:20px}";
    html += "table{margin:15px auto;background:#fff;border-radius:8px;padding:15px;max-width:520px;width:100%;box-shadow:0 2px 6px rgba(0,0,0,0.1);direction:rtl;text-align:right;border-collapse:collapse}";
    html += "td{padding:8px 5px;border-bottom:1px solid #eee;font-size:14px}";
    html += "td:first-child{font-weight:bold;color:#555;width:40%}";
    html += ".ok{color:#4CAF50;font-weight:bold}";
    html += ".fail{color:#f44336;font-weight:bold}";
    html += ".warn{color:#FF9800;font-weight:bold}";
    html += ".btn{font-size:18px;padding:14px 30px;margin:10px;border:none;border-radius:10px;color:#fff;cursor:pointer;font-weight:bold}";
    html += ".on{background:#4CAF50}.off{background:#f44336}";
    html += "</style></head><body>";
    html += "<h1>🔍 ESP32-CAM Diagnostic</h1>";

    // جدول الحالة
    html += "<table>";
    html += "<tr><td>سبب الإقلاع</td><td>" + testReason + "</td></tr>";
    html += "<tr><td>وقت التشغيل</td><td>" + String((millis() - bootMillis) / 1000) + " ثانية</td></tr>";
    html += "<tr><td>WiFi</td><td>" + testWiFi + "</td></tr>";
    html += "<tr><td>IP</td><td>" + ipAddress + "</td></tr>";
    html += "<tr><td>قوة الإشارة</td><td>" + String(rssiValue) + " dBm</td></tr>";
    html += "<tr><td>PSRAM</td><td>" + testPSRAM + "</td></tr>";
    html += "<tr><td>الكاميرا</td><td>" + testCamera + "</td></tr>";
    html += "<tr><td>تفاصيل الكاميرا</td><td>" + cameraDetails + "</td></tr>";
    html += "<tr><td>التقاط إطار</td><td>" + testFrame + "</td></tr>";
    html += "<tr><td>تفاصيل الإطار</td><td>" + frameDetails + "</td></tr>";
    html += "<tr><td>الفلاش</td><td>" + testFlash + "</td></tr>";
    html += "<tr><td>الذاكرة الحرة</td><td>" + String(ESP.getFreeHeap()) + " bytes</td></tr>";
    html += "<tr><td>PSRAM الحرة</td><td>" + String(ESP.getFreePsram()) + " bytes</td></tr>";
    html += "<tr><td>عدد الإقلاعات</td><td>" + String(bootCount) + "</td></tr>";
    html += "</table>";

    // زر الفلاش
    html += "<button id='fb' class='btn " + String(flashState ? "off" : "on") + "' onclick='toggleFlash()'>";
    html += flashState ? "إطفاء الفلاش" : "تشغيل الفلاش";
    html += "</button>";

    // سكربت
    html += "<script>";
    html += "function toggleFlash(){";
    html += "fetch('/flash/toggle').then(r=>r.text()).then(s=>{";
    html += "var b=document.getElementById('fb');";
    html += "if(s=='1'){b.innerHTML='إطفاء الفلاش';b.className='btn off';}";
    html += "else{b.innerHTML='تشغيل الفلاش';b.className='btn on';}";
    html += "});}";
    html += "</script>";

    html += "</body></html>";
    return html;
}

// ================= معالجات الويب =================
void handleRoot() {
    server.send(200, "text/html; charset=utf-8", buildPage());
    Serial.println("📄 طلب الصفحة");
}

void handleToggle() {
    flashState = !flashState;
    digitalWrite(FLASH_LED, flashState ? HIGH : LOW);
    testFlash = flashState ? "مضاء 💡" : "مطفأ 🌑";
    Serial.print("💡 الفلاش: ");
    Serial.println(flashState ? "مضاء" : "مطفأ");
    server.send(200, "text/plain", flashState ? "1" : "0");
}

// ================= تهيئة الكاميرا =================
bool initCameraSafe() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 10000000;  // 10MHz لتقليل الحرارة
    config.pixel_format = PIXFORMAT_JPEG;

    if (psramFound()) {
        config.frame_size = FRAMESIZE_QVGA;  // 320x240 لتقليل الطاقة
        config.jpeg_quality = 15;
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_PSRAM;
    } else {
        config.frame_size = FRAMESIZE_QVGA;
        config.jpeg_quality = 15;
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_DRAM;
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        cameraDetails = "فشل التهيئة: 0x" + String(err, HEX);
        return false;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL) {
        cameraDetails = "sensor NULL";
        return false;
    }

    String pidStr = "0x" + String(s->id.PID, HEX);
    if (s->id.PID == 0x26) cameraDetails = "OV2640 (PID=" + pidStr + ") ✅";
    else if (s->id.PID == 0x21) cameraDetails = "OV7670 (PID=" + pidStr + ") ⚠️";
    else cameraDetails = "PID=" + pidStr;

    return true;
}

// ================= Setup =================
void setup() {
    Serial.begin(115200);
    delay(2000);
    bootMillis = millis();
    bootCount++;

    Serial.println();
    Serial.println("=================================");
    Serial.println("🔍 ESP32-CAM Diagnostic");
    Serial.println("=================================");

    testReason = getResetReason();
    Serial.print("🔎 سبب الإقلاع: ");
    Serial.println(testReason);

    // 1) الفلاش
    pinMode(FLASH_LED, OUTPUT);
    digitalWrite(FLASH_LED, LOW);
    testFlash = "مطفأ 🌑";
    Serial.println("💡 الفلاش: جاهز");

    // 2) PSRAM
    if (psramFound()) {
        testPSRAM = "موجودة (" + String(ESP.getPsramSize() / 1024) + " KB) ✅";
    } else {
        testPSRAM = "غير موجودة ❌";
    }
    Serial.print("💾 PSRAM: ");
    Serial.println(testPSRAM);

    // 3) WiFi (الأخف أولاً)
    Serial.println("📡 جاري الاتصال بـ " + String(STA_SSID) + "...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(STA_SSID, STA_PASS);

    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 30) {
        delay(500);
        Serial.print(".");
        tries++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        ipAddress = WiFi.localIP().toString();
        rssiValue = WiFi.RSSI();
        testWiFi = "متصل ✅";
        Serial.println("✅ WiFi متصل: " + ipAddress);
    } else {
        Serial.println("⚠️ فشل WiFi، تشغيل AP...");
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASS);
        ipAddress = WiFi.softAPIP().toString();
        testWiFi = "AP فقط ⚠️ (فشل الاتصال بـ " + String(STA_SSID) + ")";
        rssiValue = 0;
        Serial.println("✅ AP: " + ipAddress);
    }

    // 4) تشغيل الخادم
    server.on("/", handleRoot);
    server.on("/flash/toggle", handleToggle);
    server.onNotFound([]() {
        server.send(404, "text/plain", "Not found");
    });
    server.begin();
    Serial.println("🌐 الخادم يعمل");
    Serial.println("🌐 افتح: http://" + ipAddress);
    Serial.println("=================================");

    // 5) اختبار الكاميرا (آخر شيء، لأنها الأثقل)
    Serial.println("📷 جاري تهيئة الكاميرا...");
    delay(500);
    if (initCameraSafe()) {
        testCamera = "جاهزة ✅";
        cameraReady = true;
        Serial.println("✅ الكاميرا جاهزة");

        // اختبار التقاط
        Serial.println("📸 اختبار التقاط إطار...");
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) {
            frameDetails = String(fb->len) + " bytes, " + String(fb->width) + "x" + String(fb->height);
            testFrame = "نجح ✅";
            esp_camera_fb_return(fb);
            Serial.println("✅ التقاط نجح: " + frameDetails);
        } else {
            testFrame = "فشل ❌";
            frameDetails = "esp_camera_fb_get() رجع NULL";
            Serial.println("❌ فشل التقاط");
        }
    } else {
        testCamera = "فشلت ❌";
        cameraReady = false;
        Serial.println("❌ فشل: " + cameraDetails);
    }

    Serial.println("=================================");
    Serial.println("✅ التشخيص انتهى — افتح الصفحة");
    Serial.println("=================================");
}

// ================= Loop =================
void loop() {
    server.handleClient();

    static unsigned long lastCheck = 0;
    if (millis() - lastCheck > 30000) {
        if (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED) {
            rssiValue = WiFi.RSSI();
            ipAddress = WiFi.localIP().toString();
        }
        lastCheck = millis();
    }
    delay(2);
}
