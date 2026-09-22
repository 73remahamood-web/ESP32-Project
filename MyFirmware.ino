// ==========================================
// ESP32 DevKit - تحكم LED عبر الويب + Serial
// خفيف جداً - لا مكتبات خارجية
// ==========================================

#include <WiFi.h>
#include <WebServer.h>

// -------- إعدادات WiFi --------
const char* STA_SSID = "Osama";         // شبكتك
const char* STA_PASS = "123456789";     // كلمة المرور
const char* AP_SSID  = "ESP32-LED";     // لو فشل الاتصال
const char* AP_PASS  = "12345678";      // 8 خانات على الأقل

// -------- دبوس الـ LED --------
#define LED_PIN 2

// -------- متغيرات --------
WebServer server(80);
bool ledState = false;
unsigned long bootMillis = 0;

// ==========================================
// صفحة HTML (خفيفة - بدون CSS ثقيل)
// ==========================================
const char PAGE_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 LED</title>
<style>
body{font-family:Arial;text-align:center;padding:20px;background:#f0f0f0}
h1{color:#333}
.btn{font-size:24px;padding:20px 40px;margin:10px;border:none;border-radius:10px;color:#fff;cursor:pointer}
.on{background:#4CAF50}
.off{background:#f44336}
.info{background:#fff;padding:15px;margin:20px auto;max-width:400px;border-radius:8px;text-align:right;direction:rtl}
</style>
</head>
<body>
<h1>ESP32 LED Control</h1>
<button id="b" class="btn" onclick="toggle()">...</button>
<div class="info" id="info"></div>
<script>
function upd(){
  fetch('/state').then(r=>r.text()).then(s=>{
    var b=document.getElementById('b');
    if(s=='1'){b.innerHTML='إطفاء الـ LED';b.className='btn on';}
    else{b.innerHTML='تشغيل الـ LED';b.className='btn off';}
  });
}
function toggle(){fetch('/toggle').then(()=>upd());}
function getInfo(){
  fetch('/info').then(r=>r.text()).then(t=>{document.getElementById('info').innerHTML=t;});
}
upd();getInfo();
setInterval(getInfo,5000);
</script>
</body>
</html>
)HTML";

// ==========================================
// دالة تشغيل LED
// ==========================================
void setLED(bool state) {
    ledState = state;
    digitalWrite(LED_PIN, state ? HIGH : LOW);
    Serial.print("💡 LED الآن: ");
    Serial.println(state ? "مضاء" : "مطفأ");
}

// ==========================================
// اتصال WiFi
// ==========================================
void connectWiFi() {
    Serial.println("=================================");
    Serial.print("📡 جاري الاتصال بـ: ");
    Serial.println(STA_SSID);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(STA_SSID, STA_PASS);
    
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 20) {
        delay(500);
        Serial.print(".");
        tries++;
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("✅ تم الاتصال بشبكتك!");
        Serial.print("🌐 IP: http://");
        Serial.println(WiFi.localIP());
        Serial.print("📶 قوة الإشارة: ");
        Serial.print(WiFi.RSSI());
        Serial.println(" dBm");
    } else {
        Serial.println("⚠️ فشل الاتصال، تشغيل شبكة خاصة...");
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASS);
        Serial.print("✅ شبكة: ");
        Serial.println(AP_SSID);
        Serial.print("🔑 كلمة المرور: ");
        Serial.println(AP_PASS);
        Serial.print("🌐 IP: http://");
        Serial.println(WiFi.softAPIP());
    }
    Serial.println("=================================");
}

// ==========================================
// معالجات الويب
// ==========================================
void handleRoot() {
    server.send_P(200, "text/html", PAGE_HTML);
    Serial.println("📄 طلب الصفحة الرئيسية");
}

void handleToggle() {
    setLED(!ledState);
    server.send(200, "text/plain", ledState ? "1" : "0");
}

void handleState() {
    server.send(200, "text/plain", ledState ? "1" : "0");
}

void handleInfo() {
    String info = "<b>الحالة:</b> " + String(ledState ? "مضاء 💡" : "مطفأ 🌑") + "<br>";
    info += "<b>الشبكة:</b> " + String(WiFi.SSID()) + "<br>";
    info += "<b>IP:</b> " + WiFi.localIP().toString() + "<br>";
    info += "<b>قوة الإشارة:</b> " + String(WiFi.RSSI()) + " dBm<br>";
    info += "<b>الذاكرة الحرة:</b> " + String(ESP.getFreeHeap()) + " bytes<br>";
    info += "<b>وقت التشغيل:</b> " + String((millis() - bootMillis) / 1000) + " ثانية";
    server.send(200, "text/html; charset=utf-8", info);
    Serial.println("📊 استعلام معلومات");
}

// ==========================================
// Setup
// ==========================================
void setup() {
    Serial.begin(115200);
    delay(500);
    bootMillis = millis();
    
    Serial.println();
    Serial.println("=================================");
    Serial.println("🚀 ESP32 DevKit - LED Control");
    Serial.println("=================================");
    
    // سبب إعادة التشغيل
    Serial.print("🔎 سبب الإقلاع: ");
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:  Serial.println("طاقة جديدة"); break;
        case ESP_RST_BROWNOUT: Serial.println("⚠️ انخفاض جهد!"); break;
        case ESP_RST_PANIC:    Serial.println("⚠️ انهيار!"); break;
        case ESP_RST_SW:       Serial.println("إعادة برمجية"); break;
        default:               Serial.println("أخرى"); break;
    }
    
    // تهيئة LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    
    // وميض تعريفي
    for (int i = 0; i < 3; i++) {
        digitalWrite(LED_PIN, HIGH); delay(100);
        digitalWrite(LED_PIN, LOW);  delay(100);
    }
    Serial.println("✅ اختبار LED: نجح");
    
    // اتصال WiFi
    connectWiFi();
    
    // مسارات الويب
    server.on("/", handleRoot);
    server.on("/toggle", handleToggle);
    server.on("/state", handleState);
    server.on("/info", handleInfo);
    server.onNotFound([](){
        server.send(404, "text/plain", "Not Found");
    });
    
    server.begin();
    Serial.println("🌐 خادم الويب يعمل على المنفذ 80");
    Serial.println("=================================");
}

// ==========================================
// Loop
// ==========================================
void loop() {
    server.handleClient();
    
    // مراقبة الاتصال كل 30 ثانية
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck > 30000) {
        if (WiFi.getMode() == WIFI_STA && WiFi.status() != WL_CONNECTED) {
            Serial.println("⚠️ فقد الاتصال، جاري إعادة المحاولة...");
            WiFi.reconnect();
        }
        lastCheck = millis();
    }
}
