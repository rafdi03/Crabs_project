/*
 * Monitoring Tambak - ESP32 Firmware (Arduino IDE)
 * Integrasi Lengkap: DS18B20, DHT22, TDS, JSN-SR04T, MQTT, dan LCD TFT ILI9342 / ILI9341
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SPI.h>

// ================= KONFIGURASI WIFI & MQTT =================
const char* WIFI_SSID   = "Bayu";
const char* WIFI_PASS   = "12345678";

const char* MQTT_BROKER = "broker.emqx.io";
const int   MQTT_PORT   = 1883;
const char* DEVICE_ID   = "ESP32-001";
const char* MQTT_TOPIC  = "tambak/ESP32-001/sensor";

// ================= DEFINISI PIN LCD ILI9342 =================
#define TFT_CS       5   
#define TFT_DC       17  
#define TFT_RST      16  

// ================= DEFINISI PIN SENSOR =================
#define DS18B20_PIN  33  // Suhu Air
#define DHT_PIN      32  // Suhu & Kelembaban Lingkungan
#define DHT_TYPE     DHT22 // Ubah ke DHT11 jika pakai DHT11
#define TDS_ADC_PIN  34  // Analog Out Modul TDS / Hujan
#define TRIG_PIN     14  // JSN-SR04T Trig
#define ECHO_PIN     27  // JSN-SR04T Echo

// ================= KALIBRASI ADC SENSOR HUJAN / TDS =================
const int ADC_AIR_MURNI  = 3800; 
const int PPM_AIR_MURNI  = 10;   
const int ADC_AIR_KERAN  = 1500; 
const int PPM_AIR_KERAN  = 150;  

#define SCOUNT 20                

// Class Custom Resolusi Full 320x240
class ILI9342_Full : public Adafruit_ILI9341 {
  public:
    ILI9342_Full(int8_t cs, int8_t dc, int8_t rst) : Adafruit_ILI9341(cs, dc, rst) {}
    void setFullResolution() { _width = 320; _height = 240; }
};

ILI9342_Full tft = ILI9342_Full(TFT_CS, TFT_DC, TFT_RST);
OneWire oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);
DHT dht(DHT_PIN, DHT_TYPE);

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Timing control
unsigned long lastPublishTime = 0;
const unsigned long PUBLISH_INTERVAL = 10000; // 10 Detik

// Buffer Penyaring Noise ADC
int analogBuffer[SCOUNT];
int analogBufferIndex = 0;

// Filter Median
int getMedianNum(int bArray[], int iFilterLen) {
  int bTab[iFilterLen];
  for (byte i = 0; i < iFilterLen; i++) bTab[i] = bArray[i];
  int i, j, bTemp;
  for (j = 0; j < iFilterLen - 1; j++) {
    for (i = 0; i < iFilterLen - j - 1; i++) {
      if (bTab[i] > bTab[i + 1]) {
        bTemp = bTab[i];
        bTab[i] = bTab[i + 1];
        bTab[i + 1] = bTemp;
      }
    }
  }
  if ((iFilterLen & 1) > 0) bTemp = bTab[(iFilterLen - 1) / 2];
  else bTemp = (bTab[iFilterLen / 2] + bTab[iFilterLen / 2 - 1]) / 2;
  return bTemp;
}

int hitungPPMdarimodulHujan(int adcRaw) {
  long ppm = map(adcRaw, ADC_AIR_MURNI, ADC_AIR_KERAN, PPM_AIR_MURNI, PPM_AIR_KERAN);
  if (ppm < 0) ppm = 0;
  return (int)ppm;
}

float bacaJarakJSN() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000); 
  if (duration == 0) return 0.0f;
  return (duration * 0.0343f / 2.0f);
}

void setILI9342RotationMode2() {
  uint8_t madctl = 0xC8;             
  tft.sendCommand(0x36, &madctl, 1);
  tft.setFullResolution();
}

void gambarLayoutUI() {
  tft.fillScreen(ILI9341_BLACK);

  // Header 320px
  tft.fillRect(0, 0, 320, 28, 0x0015); 
  tft.drawFastHLine(0, 28, 320, ILI9341_CYAN);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(50, 6);
  tft.println("MONITORING TAMBAK");

  // Grid 1: SUHU AIR & DHT (Atas Kiri)
  tft.drawRoundRect(4, 32, 154, 98, 5, ILI9341_YELLOW);
  tft.setTextColor(ILI9341_YELLOW);
  tft.setTextSize(1);
  tft.setCursor(10, 38);
  tft.println("SUHU AIR & DHT");

  // Grid 2: TDS (Atas Kanan)
  tft.drawRoundRect(162, 32, 154, 98, 5, ILI9341_GREEN);
  tft.setTextColor(ILI9341_GREEN);
  tft.setTextSize(1);
  tft.setCursor(170, 38);
  tft.println("TDS (ESTIMASI PPM)");

  // Grid 3: JARAK JSN (Bawah Kiri)
  tft.drawRoundRect(4, 134, 154, 102, 5, ILI9341_CYAN);
  tft.setTextColor(ILI9341_CYAN);
  tft.setTextSize(1);
  tft.setCursor(10, 140);
  tft.println("KETINGGIAN AIR");

  // Grid 4: OKSIGEN / DO (Bawah Kanan)
  tft.drawRoundRect(162, 134, 154, 102, 5, ILI9341_MAGENTA);
  tft.setTextColor(ILI9341_MAGENTA);
  tft.setTextSize(1);
  tft.setCursor(170, 140);
  tft.println("OKSIGEN (DO)");
}

// ================= KONEKSI NETWORK =================
void setupWiFi() {
  Serial.print("Menghubungkan ke WiFi ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Terhubung!");
}

void setupNTP() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  struct tm timeinfo;
  int retry = 0;
  while (!getLocalTime(&timeinfo) && retry < 5) {
    delay(1000);
    retry++;
  }
}

String getTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "1970-01-01T00:00:00Z";
  char timeBuffer[64];
  strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(timeBuffer);
}

void reconnectMQTT() {
  while (!mqttClient.connected()) {
    if (mqttClient.connect(DEVICE_ID)) {
      Serial.println("MQTT Terhubung!");
    } else {
      delay(3000);
    }
  }
}

void setup() {
  Serial.begin(115200);

  // Setup Pin Hardware
  pinMode(TDS_ADC_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Init LCD
  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_RST, HIGH); delay(50);
  digitalWrite(TFT_RST, LOW);  delay(150);
  digitalWrite(TFT_RST, HIGH); delay(150);

  tft.begin();
  SPI.setFrequency(10000000);
  setILI9342RotationMode2();
  tft.invertDisplay(true);

  gambarLayoutUI();

  // Init Sensor & Network
  ds18b20.begin();
  dht.begin();
  setupWiFi();
  setupNTP();

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
}

void loop() {
  // Sampling ADC TDS (Background Process)
  static unsigned long sampleTime = millis();
  if (millis() - sampleTime > 30U) {
    sampleTime = millis();
    analogBuffer[analogBufferIndex] = analogRead(TDS_ADC_PIN);
    analogBufferIndex++;
    if (analogBufferIndex == SCOUNT) analogBufferIndex = 0;
  }

  // Koneksi Network Maintenance
  if (WiFi.status() != WL_CONNECTED) setupWiFi();
  if (!mqttClient.connected()) reconnectMQTT();
  mqttClient.loop();

  // Task Rutin: Baca Sensor, Update LCD & Publish MQTT
  unsigned long now = millis();
  if (now - lastPublishTime >= PUBLISH_INTERVAL) {
    lastPublishTime = now;

    // 1. Baca Suhu Air (DS18B20)
    ds18b20.requestTemperatures();
    float suhu_air = ds18b20.getTempCByIndex(0);
    if (suhu_air == DEVICE_DISCONNECTED_C || suhu_air == -127.0) suhu_air = 0.0f;

    // 2. Baca DHT
    float suhu_lingkungan = dht.readTemperature();
    float humidity = dht.readHumidity();
    if (isnan(suhu_lingkungan)) suhu_lingkungan = 0.0f;
    if (isnan(humidity)) humidity = 0.0f;

    // 3. Baca TDS ADC
    int medianADC = getMedianNum(analogBuffer, SCOUNT);
    float tds_val = (float)hitungPPMdarimodulHujan(medianADC);

    // 4. Data Dummy Khusus JSN Jarak Air Tambak (Masuk akal: 30 - 35 cm dengan riak halus)
    static float jsn_baseline = 32.5f;
    float riak_air = (float)random(-35, 36) / 100.0f; // Fluktuasi riak air ±0.35 cm
    jsn_baseline += (float)random(-5, 6) / 100.0f;     // Drift pasang-surut halus
    if (jsn_baseline < 29.0f) jsn_baseline = 29.0f;
    if (jsn_baseline > 35.0f) jsn_baseline = 35.0f;
    float jsn_val = jsn_baseline + riak_air;

    // 5. Placeholder / Dummy Values
    float nitrat_val = 0.0f;
    float do_val = 6.8f; // Dissolved Oxygen

    // ================= UPDATE TAMPILAN LCD =================
    
    // A. SUHU AIR & DHT
    tft.fillRect(8, 52, 146, 74, ILI9341_BLACK); 
    tft.setCursor(12, 54);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    if (suhu_air == 0.0f) {
      tft.print("DS : ERR");
    } else {
      tft.print("DS :"); tft.print(suhu_air, 1); tft.println(" C");
    }

    tft.setCursor(12, 76);
    tft.setTextSize(1);
    tft.setTextColor(ILI9341_ORANGE);
    tft.print("DHT :"); tft.print(suhu_lingkungan, 1); tft.println(" C");
    tft.setCursor(12, 92);
    tft.print("Hum :"); tft.print(humidity, 1); tft.println(" %");

    // B. TDS
    tft.fillRect(166, 52, 146, 74, ILI9341_BLACK); 
    tft.setCursor(170, 65);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(3);
    tft.print((int)tds_val);
    tft.setTextSize(2);
    tft.setTextColor(ILI9341_GREEN);
    tft.print(" PPM");

    // C. JARAK JSN
    tft.fillRect(8, 155, 146, 75, ILI9341_BLACK); 
    tft.setCursor(12, 168);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(3); 
    tft.print(jsn_val, 1);
    tft.setTextSize(2);
    tft.setTextColor(ILI9341_CYAN);
    tft.println(" cm");

    // D. OKSIGEN DO
    tft.fillRect(166, 155, 146, 75, ILI9341_BLACK); 
    tft.setCursor(170, 168);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(3);
    tft.print(do_val, 1);
    tft.setTextSize(2);
    tft.setTextColor(ILI9341_MAGENTA);
    tft.print(" mg/L");

    // ================= PUBLISH MQTT JSON =================
    StaticJsonDocument<256> doc;
    doc["device_id"]  = DEVICE_ID;
    doc["suhu_air"]   = (suhu_air != -999.0 && suhu_air != -127.0 && suhu_air != 0.0f) ? suhu_air : 0.0;
    doc["tds_ppm"]    = (tds_val != -999) ? (int)tds_val : 0;
    doc["jarak_cm"]   = (jsn_val > 0) ? jsn_val : 0.0;
    doc["suhu_udara"] = (!isnan(suhu_lingkungan)) ? suhu_lingkungan : 0.0;
    doc["lembap_udr"] = (!isnan(humidity)) ? humidity : 0.0;
    doc["do_mg"]      = (do_val != -999.0) ? do_val : 0.0;

    char jsonBuffer[256];
    serializeJson(doc, jsonBuffer);

    Serial.print("Publish MQTT: ");
    Serial.println(jsonBuffer);
    mqttClient.publish(MQTT_TOPIC, jsonBuffer);
  }
}
