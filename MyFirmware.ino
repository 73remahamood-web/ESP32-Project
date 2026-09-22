// ==========================================
// كود اختبار الـ LED المدمج في ESP32 العادي
// ==========================================

// معظم لوحات ESP32 DevKit تحتوي LED على GPIO 2
// بعض اللوحات تستخدم GPIO 5 أو 15 أو 16
#define BUILTIN_LED 2

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("=================================");
    Serial.println("اختبار الـ LED المدمج");
    Serial.println("=================================");

    pinMode(BUILTIN_LED, OUTPUT);
    digitalWrite(BUILTIN_LED, LOW);
}

void loop() {
    Serial.println("💡 الـ LED مضاء");
    digitalWrite(BUILTIN_LED, HIGH);
    delay(500);                       // نصف ثانية

    Serial.println("🌑 الـ LED مطفأ");
    digitalWrite(BUILTIN_LED, LOW);
    delay(500);                       // نصف ثانية
}
