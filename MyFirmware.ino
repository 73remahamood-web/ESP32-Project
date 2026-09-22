// ==========================================
// كود اختبار فلاش ESP32-CAM فقط
// ==========================================

// دبوس الفلاش الأبيض في لوحة AI-Thinker / ESP32-CAM-MB
#define FLASH_LED 4

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("=================================");
    Serial.println("اختبار فلاش ESP32-CAM");
    Serial.println("=================================");

    pinMode(FLASH_LED, OUTPUT);
    digitalWrite(FLASH_LED, LOW);  // ابدأ بإطفاء الفلاش
}

void loop() {
    Serial.println("💡 الفلاش مضاء");
    digitalWrite(FLASH_LED, HIGH);   // تشغيل
    delay(1000);                     // ثانية

    Serial.println("🌑 الفلاش مطفأ");
    digitalWrite(FLASH_LED, LOW);    // إطفاء
    delay(1000);                     // ثانية
}
