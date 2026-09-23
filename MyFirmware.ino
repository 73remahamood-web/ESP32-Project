// ==========================================
// ESP32-CAM - تحكم الفلاش عبر الويب + Serial
// ==========================================
#include <WiFi.h>
#include <WebServer.h>

const char* STA_SSID = "Osama";
const char* STA_PASS = "123456789";
const char* AP_SSID  = "ESP32-FLASH";
const char* AP_PASS  = "12345678";

#define LED_PIN 4

WebServer server(80);
bool ledState = false;
unsigned long bootMillis = 0;
int toggleCount = 0;

const char PAGE_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32-CAM Flash</title>
<style>
body{font-family:Arial;text-align:center;padding:20px;background:#f0f0f0;margin:0}
h1{color:#333}
.btn{font-size:24px;padding:20px 40px;margin:10px;border:none;border-radius:10px;color:#fff;cursor:pointer}
.on{background:#4CAF50}
.off{background:#f44336}
.info{background:#fff;padding:15px;margin:20px auto;max-width:400px;border-radius:8px;text-align:right;direction:rtl;box-shadow:0 2px 6px rgba(0,0,0,0.1)}
.info b{color:#333}
</style>
</head>
<body>
<h1>ESP32-CAM Flash</h1>
<button id="b" class="btn" onclick="toggle()">...</button>
<div class="info" id="info">جاري التحميل...</div>
<script>
function upd(){
  fetch('/state').then(r=>r.text()).then(s=>{
    var b=document.getElementById('b');
    if(s=='1'){b.innerHTML='إطفاء الفلاش';b.className='btn on';}
    else{b.innerHTML='تشغيل الفلاش';b.className='btn off';}
  });
}
function toggle(){fetch('/toggle').then(()=>{upd();getInfo();});}
function getInfo(){
  fetch('/info').then(r=>r.text()).then(t=>{document.getElementById('info').innerHTML=t;});
}
upd();getInfo();
setInterval(getInfo,5000);
</script>
</body>
</html>
)HTML";

void setLED(bool state) {
    ledState = state;
    digitalWrite(LED_PIN, state ? HIGH : LOW);
    Serial.print("💡 الفلاش الآن: ");
    Serial.println(state ? "مضاء" : "مطفأ");
}

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

void handleRoot() {
    server.send_P(200, "text/html", PAGE_HTML);
    Serial.println("📄 طلب الصفحة الرئيسية");
}

void handleToggle() {
    setLED(!ledState);
    toggleCount++;
    server.send(200, "text/plain", ledState ? "1" : "0");
}

void handleState() {
    server.send(200, "text/plain", ledState ? "1" : "0");
}

void handleInfo() {
    String info = "<b>الحالة:</b> " + String(ledState ? "مضاء 💡" : "مطفأ 🌑") + "<br>";
    info += "<b>الشبكة:</b> " + String(WiFi.getMode() == WIFI_AP ? AP_SSID : WiFi.SSID()) + "<br>";
    info += "<b>IP:</b> " + (WiFi.getMode() == WIFI_AP ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) + "<br>";
    info += "<b>قوة الإشارة:</b> " + String(WiFi.RSSI()) + " dBm<br>";
    info += "<b>الذاكرة الحرة:</b> " + String(ESP.getFreeHeap()) + " bytes<br>";
    info += "<b>وقت التشغيل:</b> " + String((millis() - bootMillis) / 1000) + " ثانية<br>";
    info += "<b>عدد المرات:</b> " + String(toggleCount);
    server.send(200, "text/html; charset=utf-8", info);
    Serial.println("📊 استعلام معلومات");
}

void setup() {
    Serial.begin(115200);
    delay(500);
    bootMillis = millis();

    Serial.println();
    Serial.println("=================================");
    Serial.println("🚀 ESP32-CAM - Flash Control");
    Serial.println("=================================");

    Serial.print("🔎 سبب الإقلاع: ");
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:  Serial.println("طاقة جديدة"); break;
        case ESP_RST_BROWNOUT: Serial.println("⚠️ انخفاض جهد!"); break;
        case ESP_RST_PANIC:    Serial.println("⚠️ انهيار!"); break;
        case ESP_RST_SW:       Serial.println("إعادة برمجية"); break;
        case ESP_RST_EXT:      Serial.println("زر RST"); break;
        default:               Serial.println("أخرى"); break;
    }

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    for (int i = 0; i < 3; i++) {
        digitalWrite(LED_PIN, HIGH); delay(100);
        digitalWrite(LED_PIN, LOW);  delay(100);
    }
    Serial.println("✅ اختبار الفلاش: نجح");

    connectWiFi();

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

void loop() {
    server.handleClient();

    static unsigned long lastCheck = 0;
    if (millis() - lastCheck > 30000) {
        if (WiFi.getMode() == WIFI_STA && WiFi.status() != WL_CONNECTED) {
            Serial.println("⚠️ فقد الاتصال، إعادة محاولة...");
            WiFi.reconnect();
        }
        lastCheck = millis();
    }
}
