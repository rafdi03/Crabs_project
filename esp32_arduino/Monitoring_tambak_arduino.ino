/*
 * Monitoring Tambak - ESP32 Firmware (Arduino IDE)
 * 
 * Sensor:
 * - DS18B20 (Suhu Air) -> Pin GPIO 33
 * - DHT22 / DHT11 (Suhu Lingkungan & Kelembaban) -> Pin GPIO 32
 * 
 * Parameter lain (TDS, JSN, Nitrat, DO) diset 0.00 (placeholder).
 * Payload dikirim via MQTT dalam format JSON setiap 10 detik.
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <time.h>

// ==========================================
// KONFIGURASI WIFI & MQTT
// ==========================================
const char* WIFI_SSID   = "Bayu";
const char* WIFI_PASS   = "12345678";

const char* MQTT_BROKER = "broker.emqx.io";
const int   MQTT_PORT   = 1883;
const char* DEVICE_ID   = "ESP32-001";
const char* MQTT_TOPIC  = "tambak/ESP32-001/sensor";

// ==========================================
// KONFIGURASI PIN SENSOR
// ==========================================
#define DS18B20_PIN 33
#define DHT_PIN     32
#define DHT_TYPE    DHT22   // Ubah ke DHT11 jika menggunakan DHT11

// Inisialisasi Objek Sensor & Network
OneWire oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);
DHT dht(DHT_PIN, DHT_TYPE);

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Timing control
unsigned long lastPublishTime = 0;
const unsigned long PUBLISH_INTERVAL = 10000; // 10 detik

// ==========================================
// KONEKSI WIFI
// ==========================================
void setupWiFi() {
  Serial.print("Menghubungkan ke WiFi ");
  Serial.print(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Terhubung!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

// ==========================================
// SINKRONISASI WAKTU (NTP)
// ==========================================
void setupNTP() {
  Serial.println("Mengkonfigurasi sinkronisasi waktu (NTP)...");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  struct tm timeinfo;
  int retry = 0;
  while (!getLocalTime(&timeinfo) && retry < 10) {
    Serial.println("Menunggu sinkronisasi NTP...");
    delay(1000);
    retry++;
  }
  if (retry < 10) {
    Serial.println("Waktu berhasil disinkronkan!");
  } else {
    Serial.println("Gagal mendapatkan waktu NTP, melanjutkan...");
  }
}

// Mengambil ISO8601 Timestamp String (UTC)
String getTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "1970-01-01T00:00:00Z";
  }
  char timeBuffer[64];
  strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(timeBuffer);
}

// ==========================================
// KONEKSI MQTT
// ==========================================
void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Mencoba koneksi MQTT ke ");
    Serial.print(MQTT_BROKER);
    Serial.print("... ");

    if (mqttClient.connect(DEVICE_ID)) {
      Serial.println("Terhubung!");
    } else {
      Serial.print("Gagal, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" mencoba lagi dalam 5 detik...");
      delay(5000);
    }
  }
}

// ==========================================
// SETUP & LOOP
// ==========================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32 TAMBAK (DS18B20 & DHT) START ===");

  // Inisialisasi Sensor
  ds18b20.begin();
  dht.begin();

  // Koneksi Network
  setupWiFi();
  setupNTP();

  // Setup MQTT Client
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
}

void loop() {
  // Pelihara Koneksi WiFi & MQTT
  if (WiFi.status() != WL_CONNECTED) {
    setupWiFi();
  }
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop();

  // Kirim data setiap 10 detik
  unsigned long now = millis();
  if (now - lastPublishTime >= PUBLISH_INTERVAL) {
    lastPublishTime = now;

    // 1. Baca Suhu Air (DS18B20)
    ds18b20.requestTemperatures();
    float suhu_air = ds18b20.getTempCByIndex(0);
    if (suhu_air == DEVICE_DISCONNECTED_C) {
      Serial.println("Gagal membaca DS18B20!");
      suhu_air = 0.0f;
    } else {
      Serial.printf("Suhu Air (DS18B20): %.2f °C\n", suhu_air);
    }

    // 2. Baca Suhu Lingkungan & Kelembaban (DHT)
    float suhu_lingkungan = dht.readTemperature();
    float humidity = dht.readHumidity();
    if (isnan(suhu_lingkungan) || isnan(humidity)) {
      Serial.println("Gagal membaca DHT!");
      suhu_lingkungan = 0.0f;
      humidity = 0.0f;
    } else {
      Serial.printf("Suhu Lingkungan (DHT): %.2f °C | Kelembaban: %.2f %%\n", suhu_lingkungan, humidity);
    }

    // Sensor lain placeholder 0.0
    float tds_val = 0.0f;
    float jsn_val = 0.0f;
    float nitrat_val = 0.0f;
    float do_val = 0.0f;

    // Get Timestamp
    String timestampStr = getTimestamp();

    // Build JSON Payload
    StaticJsonDocument<256> doc;
    doc["tds"] = tds_val;
    doc["jsn"] = jsn_val;
    doc["nitrat"] = nitrat_val;
    doc["do"] = do_val;
    doc["suhu_air"] = suhu_air;
    doc["suhu_lingkungan"] = suhu_lingkungan;
    doc["timestamp"] = timestampStr;

    char jsonBuffer[256];
    serializeJson(doc, jsonBuffer);

    Serial.print("Publish MQTT Payload: ");
    Serial.println(jsonBuffer);

    // Publish to MQTT Topic
    if (mqttClient.publish(MQTT_TOPIC, jsonBuffer)) {
      Serial.println("Payload berhasil dikirim!");
    } else {
      Serial.println("Gagal mengirim payload via MQTT.");
    }
  }
}
