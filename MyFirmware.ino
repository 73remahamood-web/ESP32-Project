#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h"
#include "esp_system.h"

const char* STA_SSID = "Osama";
const char* STA_PASS = "123456789";

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

bool tryCamera(int xclk_hz, framesize_t size, bool use_psram) {
    Serial.println("-----------------------------");
    Serial.printf("محاولة: XCLK=%d Hz, Size=%d, PSRAM=%d\n", xclk_hz, size, use_psram);
    
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
    config.xclk_freq_hz = xclk_hz;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = size;
    config.jpeg_quality = 12;
    config.fb_count = use_psram ? 2 : 1;
    config.fb_location = use_psram ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("❌ فشل التهيئة: 0x%x\n", err);
        return false;
    }

    sensor_t *s = esp_camera_sensor_get();
    Serial.printf("✅ Sensor PID: 0x%02X\n", s->id.PID);
    if (s->id.PID == 0x26) Serial.println("   → OV2640 (المناسب)");
    else if (s->id.PID == 0x21) Serial.println("   → OV7670 (غير مناسب)");
    else Serial.printf("   → غير معروف (0x%02X)\n", s->id.PID);

    Serial.println("محاولة التقاط اطار...");
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("❌ فشل التقاط اطار - الكاميرا لا تُرسل بيانات!");
        esp_camera_deinit();
        return false;
    }
    Serial.printf("✅ نجح! حجم الاإطار: %d bytes\n", fb->len);
    esp_camera_fb_return(fb);
    return true;
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println();
    Serial.println("=================================");
    Serial.println("ESP32-CAM التشخيص الشامل");
    Serial.println("=================================");

    pinMode(FLASH_LED, OUTPUT);
    digitalWrite(FLASH_LED, LOW);

    Serial.print("PSRAM: ");
    Serial.println(psramFound() ? "موجودة" : "غير موجودة");

    WiFi.mode(WIFI_STA);
    WiFi.begin(STA_SSID, STA_PASS);
    Serial.print("اتصال WiFi");
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 20) {
        delay(500); Serial.print("."); tries++;
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("فشل الاتصال");
    }

    Serial.println();
    Serial.println("===== بدء الاختبارات =====");

    bool ok = false;
    int attempt = 0;

    // محاولة 1: الإعداد الافتراضي 20MHz + VGA + PSRAM
    attempt++;
    Serial.printf("\n[%d/6] المحاولة الرئيسية\n", attempt);
    if (tryCamera(20000000, FRAMESIZE_VGA, true)) { ok = true; goto done; }
    delay(1000);

    // محاولة 2: 20MHz + QVGA + PSRAM
    attempt++;
    Serial.printf("\n[%d/6] QVGA بدلا من VGA\n", attempt);
    if (tryCamera(20000000, FRAMESIZE_QVGA, true)) { ok = true; goto done; }
    delay(1000);

    // محاولة 3: 10MHz + VGA + PSRAM (خفض التردد)
    attempt++;
    Serial.printf("\n[%d/6] خفض XCLK الى 10MHz\n", attempt);
    if (tryCamera(10000000, FRAMESIZE_VGA, true)) { ok = true; goto done; }
    delay(1000);

    // محاولة 4: 10MHz + QVGA + PSRAM
    attempt++;
    Serial.printf("\n[%d/6] 10MHz + QVGA\n", attempt);
    if (tryCamera(10000000, FRAMESIZE_QVGA, true)) { ok = true; goto done; }
    delay(1000);

    // محاولة 5: 8MHz + QVGA + DRAM (بدون PSRAM)
    attempt++;
    Serial.printf("\n[%d/6] 8MHz + QVGA + DRAM\n", attempt);
    if (tryCamera(8000000, FRAMESIZE_QVGA, false)) { ok = true; goto done; }
    delay(1000);

    // محاولة 6: 5MHz + QVGA + DRAM
    attempt++;
    Serial.printf("\n[%d/6] 5MHz + QVGA + DRAM\n", attempt);
    if (tryCamera(5000000, FRAMESIZE_QVGA, false)) { ok = true; goto done; }

done:
    Serial.println();
    Serial.println("=================================");
    if (ok) {
        Serial.println("✅ الكاميرا تعمل!");
        Serial.println("=================================");
        Serial.println("🔍 افحص Serial Monitor لرؤية أي إعداد نجح");
    } else {
        Serial.println("❌ فشلت جميع المحاولات!");
        Serial.println("=================================");
        Serial.println("الاسباب المحتملة:");
        Serial.println("1) الكيبل غير مثبت جيدا - اعد تركيبه");
        Serial.println("2) الكاميرا تالفة");
        Serial.println("3) طاقة غير كافية");
    }
}

void loop() {
    delay(10000);
    Serial.printf("الذاكرة الحرة: %u bytes | PSRAM: %u bytes\n",
                  ESP.getFreeHeap(), ESP.getPsramSize());
}
