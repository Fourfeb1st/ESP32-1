/*
 * ESP32 Sensing Node: วัดความชื้นในดิน + ความชื้น/อุณหภูมิอากาศ แล้วส่งขึ้น MQTT
 *
 * อุปกรณ์:
 *   - ESP32 DevKit
 *   - Capacitive Soil Moisture Sensor v1.2 (แนะนำแบบ capacitive เพราะไม่กัดกร่อน)
 *   - DHT22 (AM2302)
 *
 * การต่อสาย:
 *   Soil Sensor : VCC -> 3V3, GND -> GND, AOUT -> GPIO34
 *   DHT22       : VCC -> 3V3, GND -> GND, DATA -> GPIO4 (ถ้าไม่ใช่โมดูล ใส่ R 10k ดึงขึ้น 3V3)
 *   * ใช้ขา ADC1 (GPIO32-39) เท่านั้น เพราะ ADC2 ใช้ไม่ได้ตอนเปิด WiFi
 *
 * Library (ติดตั้งผ่าน Library Manager):
 *   - PubSubClient (Nick O'Leary)
 *   - DHT sensor library (Adafruit) + Adafruit Unified Sensor
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// ===== ตั้งค่า WiFi / MQTT =====
const char* WIFI_SSID     = "FourFeb";
const char* WIFI_PASS     = "12345678";
const char* MQTT_SERVER   = "broker.mqtt.cool";   // เปลี่ยนเป็น IP ของ broker ทีม (เช่น Raspberry Pi)
const int   MQTT_PORT     = 1883;
const char* MQTT_TOPIC    = "tgr2026/team01/sensor";

// ===== ขาและเซนเซอร์ =====
#define SOIL_PIN  34
#define DHT_PIN   4
#define DHT_TYPE  DHT11

// ===== ค่าคาลิเบรต (วัดจริงแล้วแก้) =====
// SOIL_DRY = ค่าตอนเซนเซอร์อยู่ในอากาศ/ดินแห้ง, SOIL_WET = ค่าตอนจุ่มน้ำ
int SOIL_DRY = 3200;
int SOIL_WET = 1300;

const unsigned long INTERVAL_MS = 5000;

DHT dht(DHT_PIN, DHT_TYPE);
WiFiClient espClient;
PubSubClient mqtt(espClient);
unsigned long lastSend = 0;

void connectWiFi() {
  Serial.printf("Connecting to %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nWiFi OK, IP: %s\n", WiFi.localIP().toString().c_str());
}

void connectMQTT() {
  while (!mqtt.connected()) {
    String clientId = "esp32-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    Serial.print("Connecting MQTT... ");
    if (mqtt.connect(clientId.c_str())) {
      Serial.println("OK");
    } else {
      Serial.printf("failed rc=%d, retry in 2s\n", mqtt.state());
      delay(2000);
    }
  }
}

// อ่านค่า ADC หลายครั้งแล้วเฉลี่ย เพื่อลด noise
int readSoilRaw() {
  long sum = 0;
  for (int i = 0; i < 20; i++) {
    sum += analogRead(SOIL_PIN);
    delay(5);
  }
  return sum / 20;
}

float soilPercent(int raw) {
  float pct = (float)(SOIL_DRY - raw) * 100.0 / (SOIL_DRY - SOIL_WET);
  return constrain(pct, 0, 100);
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);         // 0-4095
  analogSetAttenuation(ADC_11db);   // อ่านได้ถึง ~3.3V
  dht.begin();
  connectWiFi();
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();

  if (millis() - lastSend < INTERVAL_MS) return;
  lastSend = millis();

  int   soilRaw = readSoilRaw();
  float soilPct = soilPercent(soilRaw);
  float airHum  = dht.readHumidity();
  float airTemp = dht.readTemperature();

  if (isnan(airHum) || isnan(airTemp)) {
    Serial.println("อ่าน DHT22 ไม่ได้ ตรวจสายไฟ");
    return;
  }

  char payload[160];
  snprintf(payload, sizeof(payload),
           "{\"soil_raw\":%d,\"soil_pct\":%.1f,\"air_hum\":%.1f,\"air_temp\":%.1f}",
           soilRaw, soilPct, airHum, airTemp);

  mqtt.publish(MQTT_TOPIC, payload);
  Serial.println(payload);
}
