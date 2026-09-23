#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h"
#include "esp_system.h"

const char* STA_SSID = "Osama";
const char* STA_PASS = "123456789";
const char* AP_SSID  = "ESP32-CAM";
const char* AP_PASS  = "12345678";

#define FLASH_LED 4

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

httpd_handle_t stream_httpd = NULL;
bool flashState = false;
unsigned long bootMillis = 0;
unsigned long frameCount = 0;
unsigned long framesInWindow = 0;
unsigned long lastFpsUpdate = 0;
float currentFps = 0;

const char PAGE_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32-CAM</title>
<style>
body{font-family:Arial;text-align:center;padding:15px;background:#f0f0f0;margin:0}
h1{color:#333;font-size:22px;margin:10px}
.stream{max-width:100%;width:640px;border-radius:10px;box-shadow:0 2px 8px rgba(0,0,0,0.2);background:#000;display:block;margin:10px auto;min-height:200px}
.btn{font-size:16px;padding:14px 24px;margin:6px;border:none;border-radius:10px;color:#fff;cursor:pointer;font-weight:bold}
.on{background:#4CAF50}
.off{background:#f44336}
.cap{background:#2196F3}
.info{background:#fff;padding:15px;margin:15px auto;max-width:420px;border-radius:8px;text-align:right;direction:rtl;box-shadow:0 2px 6px rgba(0,0,0,0.1);font-size:14px;line-height:1.8}
.info b{color:#333}
</style>
</head>
<body>
<h1>ESP32-CAM</h1>
<img id="stream" class="stream" src="/stream">
<div>
<button id="fb" class="btn on" onclick="toggleFlash()">تشغيل الفلاش</button>
<button class="btn cap" onclick="capture()">التقاط صورة</button>
</div>
<div class="info" id="info">جاري التحميل...</div>
<script>
function toggleFlash(){
  fetch('/flash/toggle').then(r=>r.text()).then(s=>{
    var b=document.getElementById('fb');
    if(s=='1'){b.innerHTML='إطفاء الفلاش';b.className='btn off';}
    else{b.innerHTML='تشغيل الفلاش';b.className='btn on';}
  });
}
function capture(){
  window.open('/capture','_blank');
}
function getInfo(){
  fetch('/info').then(r=>r.text()).then(t=>{document.getElementById('info').innerHTML=t;});
}
function updFlash(){
  fetch('/status').then(r=>r.text()).then(s=>{
    var b=document.getElementById('fb');
    if(s=='1'){b.innerHTML='إطفاء الفلاش';b.className='btn off';}
    else{b.innerHTML='تشغيل الفلاش';b.className='btn on';}
  });
}
function restartStream(){
  document.getElementById('stream').src='/stream?t='+Date.now();
}
updFlash();getInfo();
setInterval(getInfo,5000);
setInterval(updFlash,3000);
setTimeout(restartStream, 300000);
</script>
</body>
</html>
)HTML";

bool initCamera() {
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
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;

    if(psramFound()){
        config.frame_size = FRAMESIZE_VGA;
        config.jpeg_quality = 12;
        config.fb_count = 2;
        config.fb_location = CAMERA_FB_IN_PSRAM;
        Serial.println("جودة: VGA 640x480 (PSRAM)");
    } else {
        config.frame_size = FRAMESIZE_QVGA;
        config.jpeg_quality = 15;
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_DRAM;
        Serial.println("جودة: QVGA 320x240 (DRAM)");
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("فشل تشغيل الكاميرا: 0x%x\n", err);
        return false;
    }
    return true;
}

static esp_err_t stream_handler(httpd_req_t *req) {
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t * _jpg_buf = NULL;
    char part_buf[64];

    res = httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
    if(res != ESP_OK) return res;
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    Serial.println("بدأ البث");
    while(true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            Serial.println("فشل التقاط اطار");
            res = ESP_FAIL;
        } else {
            if(fb->format != PIXFORMAT_JPEG){
                bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
                esp_camera_fb_return(fb);
                fb = NULL;
                if(!jpeg_converted) res = ESP_FAIL;
            } else {
                _jpg_buf_len = fb->len;
                _jpg_buf = fb->buf;
            }
        }
        if(res == ESP_OK) {
            size_t hlen = snprintf(part_buf, 64, "\r\n--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", _jpg_buf_len);
            res = httpd_resp_send_chunk(req, part_buf, hlen);
        }
        if(res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }
        if(res == ESP_OK) {
            res = httpd_resp_send_chunk(req, "\r\n", 2);
        }
        if(fb){
            esp_camera_fb_return(fb);
            fb = NULL;
            _jpg_buf = NULL;
        } else if(_jpg_buf){
            free(_jpg_buf);
            _jpg_buf = NULL;
        }
        if(res != ESP_OK) break;

        frameCount++;
        framesInWindow++;
        if (millis() - lastFpsUpdate >= 1000) {
            currentFps = framesInWindow * 1000.0 / (millis() - lastFpsUpdate);
            framesInWindow = 0;
            lastFpsUpdate = millis();
        }
    }
    Serial.println("انتهى البث");
    return res;
}

static esp_err_t capture_handler(httpd_req_t *req) {
    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    esp_err_t res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    Serial.println("التقاط صورة");
    return res;
}

static esp_err_t page_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    Serial.println("طلب الصفحة الرئيسية");
    return httpd_resp_send(req, PAGE_HTML, strlen(PAGE_HTML));
}

static esp_err_t flash_toggle_handler(httpd_req_t *req) {
    flashState = !flashState;
    digitalWrite(FLASH_LED, flashState ? HIGH : LOW);
    httpd_resp_set_type(req, "text/plain");
    Serial.print("الفلاش: ");
    Serial.println(flashState ? "مضاء" : "مطفأ");
    return httpd_resp_send(req, flashState ? "1" : "0", 1);
}

static esp_err_t status_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, flashState ? "1" : "0", 1);
}

static esp_err_t info_handler(httpd_req_t *req) {
    char info[900];
    unsigned long uptime = (millis() - bootMillis) / 1000;
    bool isAP = (WiFi.getMode() == WIFI_AP);
    String ipStr = isAP ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    String netName = isAP ? String(AP_SSID) : WiFi.SSID();

    snprintf(info, sizeof(info),
        "<b>الفلاش:</b> %s<br>"
        "<b>الشبكة:</b> %s<br>"
        "<b>IP:</b> %s<br>"
        "<b>الاشارة:</b> %d dBm<br>"
        "<b>الذاكرة الحرة:</b> %u bytes<br>"
        "<b>PSRAM الحرة:</b> %u bytes<br>"
        "<b>الدقة:</b> %s<br>"
        "<b>معدل الاطارات:</b> %.1f fps<br>"
        "<b>عدد الاطارات:</b> %lu<br>"
        "<b>وقت التشغيل:</b> %lu ثانية",
        flashState ? "مضاء" : "مطفأ",
        netName.c_str(),
        ipStr.c_str(),
        WiFi.RSSI(),
        (unsigned int)ESP.getFreeHeap(),
        (unsigned int)ESP.getFreePsram(),
        psramFound() ? "VGA 640x480" : "QVGA 320x240",
        currentFps,
        frameCount,
        uptime
    );

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, info, strlen(info));
}

void startServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 12;
    config.stack_size = 8192;
    config.lru_purge_enable = true;

    httpd_uri_t uri_page       = { "/",             HTTP_GET, page_handler,         NULL };
    httpd_uri_t uri_stream     = { "/stream",       HTTP_GET, stream_handler,       NULL };
    httpd_uri_t uri_capture    = { "/capture",      HTTP_GET, capture_handler,      NULL };
    httpd_uri_t uri_info       = { "/info",         HTTP_GET, info_handler,         NULL };
    httpd_uri_t uri_status     = { "/status",       HTTP_GET, status_handler,       NULL };
    httpd_uri_t uri_flash_tog  = { "/flash/toggle", HTTP_GET, flash_toggle_handler, NULL };

    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &uri_page);
        httpd_register_uri_handler(stream_httpd, &uri_stream);
        httpd_register_uri_handler(stream_httpd, &uri_capture);
        httpd_register_uri_handler(stream_httpd, &uri_info);
        httpd_register_uri_handler(stream_httpd, &uri_status);
        httpd_register_uri_handler(stream_httpd, &uri_flash_tog);
        Serial.println("خادم الويب على المنفذ 80");
    } else {
        Serial.println("فشل تشغيل الخادم");
    }
}

void connectWiFi() {
    Serial.println("=================================");
    Serial.print("جاري الاتصال بـ: ");
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
        Serial.println("تم الاتصال بشبكتك!");
        Serial.print("IP: http://");
        Serial.println(WiFi.localIP());
        Serial.print("قوة الاشارة: ");
        Serial.print(WiFi.RSSI());
        Serial.println(" dBm");
    } else {
        Serial.println("فشل الاتصال، تشغيل شبكة خاصة...");
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASS);
        Serial.print("شبكة: ");
        Serial.println(AP_SSID);
        Serial.print("كلمة المرور: ");
        Serial.println(AP_PASS);
        Serial.print("IP: http://");
        Serial.println(WiFi.softAPIP());
    }
    Serial.println("=================================");
}

void setup() {
    Serial.begin(115200);
    delay(500);
    bootMillis = millis();

    Serial.println();
    Serial.println("=================================");
    Serial.println("ESP32-CAM - كاميرا + فلاش");
    Serial.println("=================================");

    Serial.print("سبب الاقلاع: ");
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:  Serial.println("طاقة جديدة"); break;
        case ESP_RST_BROWNOUT: Serial.println("انخفاض جهد!"); break;
        case ESP_RST_PANIC:    Serial.println("انهيار!"); break;
        case ESP_RST_SW:       Serial.println("اعادة برمجية"); break;
        case ESP_RST_EXT:      Serial.println("زر RST"); break;
        default:               Serial.println("اخرى"); break;
    }

    pinMode(FLASH_LED, OUTPUT);
    digitalWrite(FLASH_LED, LOW);

    Serial.print("PSRAM: ");
    if (psramFound()) {
        Serial.print("موجودة - ");
        Serial.print(ESP.getPsramSize() / 1024);
        Serial.println(" KB");
    } else {
        Serial.println("غير موجودة - جودة منخفضة");
    }

    for (int i = 0; i < 3; i++) {
        digitalWrite(FLASH_LED, HIGH); delay(100);
        digitalWrite(FLASH_LED, LOW);  delay(100);
    }
    Serial.println("اختبار الفلاش: نجح");

    Serial.println("جاري تهيئة الكاميرا...");
    if (!initCamera()) {
        Serial.println("فشل الكاميرا!");
        while(1) { delay(1000); }
    }
    Serial.println("الكاميرا تعمل!");

    connectWiFi();
    startServer();

    Serial.println("=================================");
    Serial.println("كل شيء جاهز!");
    Serial.println("=================================");
}

void loop() {
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck > 30000) {
        if (WiFi.getMode() == WIFI_STA && WiFi.status() != WL_CONNECTED) {
            Serial.println("فقد الاتصال، اعادة محاولة...");
            WiFi.reconnect();
        }
        lastCheck = millis();
    }
    delay(1000);
}
