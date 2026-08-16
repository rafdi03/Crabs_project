#define TINY_GSM_MODEM_SIM800 // Tipe modem onboard TTGO T-Call

#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SPI.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>

// ================= 1. KONFIGURASI GSM KARTU TELKOMSEL & MQTT =================
// APN Telkomsel Indonesia
const char apn[]      = "internet"; 
const char gprsUser[] = "";
const char gprsPass[] = "";

// IMEI Baru Resmi Terdaftar (Hasil Konfirmasi Sebelumnya)
const char* REGISTERED_IMEI = "864043050823850";

// Konfigurasi MQTT Broker
const char* MQTT_BROKER = "broker.emqx.io";
const int   MQTT_PORT   = 1883;
const char* DEVICE_ID   = "ESP32-001";
const char* MQTT_TOPIC  = "tambak/ESP32-001/sensor";

// ================= 2. ALOKASI PIN TTGO ONBOARD GSM =================
#define MODEM_RST            5
#define MODEM_PWKEY          4
#define MODEM_POWER_ON       23
#define MODEM_TX             27
#define MODEM_RX             26

// Hardware Serial1 untuk Modem GSM TTGO
#define SerialAT Serial1

// ================= 3. ALOKASI PIN LCD ILI9342 =================
#define TFT_CS        13  
#define TFT_DC        12  
#define TFT_RST       14  
#define TFT_MOSI      19  
#define TFT_SCLK      18  

// ================= 4. ALOKASI PIN SENSOR =================
#define DS18B20_PIN   15  
#define DHT_PIN       2   // (GPIO 4 dipakai PWKEY SIM800 TTGO)
#define DHT_TYPE      DHT22 
#define TDS_ADC_PIN   34  
#define TRIG_PIN      32  
#define ECHO_PIN      36  

// ================= 5. ALOKASI PIN RELAY =================
#define RELAY_1       25  
#define RELAY_2       22  
#define RELAY_3       0   
#define RELAY_4       33  
#define RELAY_5       21  

// DEFINISI WARNA (HIGH-CONTRAST)
#define COLOR_BG          ILI9341_BLACK 
#define COLOR_TEXT        ILI9341_WHITE 
#define COLOR_TEXT_MUTED  0xC618        
#define COLOR_BORDER      ILI9341_WHITE 

const int ADC_AIR_MURNI  = 3800; 
const int PPM_AIR_MURNI  = 10;    
const int ADC_AIR_KERAN  = 1500; 
const int PPM_AIR_KERAN  = 150;   

#define SCOUNT 20
int analogBuffer[SCOUNT];
int analogBufferIndex = 0;

// Filter Median 5 Data untuk JSN-SR04T
#define JSN_SCOUNT 5
float jsnBuffer[JSN_SCOUNT];
int jsnBufferIndex = 0;
bool jsnBufferInitialized = false;

float lastSuhuAir = -999.0;
int lastTdsPPM = -999;
float lastJarakJSN = -999.0;
float lastSuhuUdara = -999.0;
float lastLembapUdara = -999.0;
float lastDO = -999.0;

// State Machine untuk Sensor
enum SensorState { BACA_SUHU_AIR, BACA_TDS, BACA_DHT, BACA_DO };
SensorState currentState = BACA_SUHU_AIR;

unsigned long lastReadTime = 0;
const unsigned long INTERVAL_GANTIAN = 400; 

// Jalur Cepat JSN (Tiap 80ms)
unsigned long lastJSNTime = 0;
const unsigned long INTERVAL_JSN = 80;

bool heartBeatState = false;

// Objek GSM & MQTT Client
TinyGsm modem(SerialAT);
TinyGsmClient gsmClient(modem);
PubSubClient mqttClient(gsmClient);

class ILI9342_Full : public Adafruit_ILI9341 {
  public:
    ILI9342_Full(int8_t cs, int8_t dc, int8_t rst) : Adafruit_ILI9341(cs, dc, rst) {}
    void setFullResolution() { _width = 320; _height = 240; }
};

ILI9342_Full tft = ILI9342_Full(TFT_CS, TFT_DC, TFT_RST);
OneWire oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);
DHT dht(DHT_PIN, DHT_TYPE);

// ================= FUNGSI SENSOR =================
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

float getMedianFloat(float bArray[], int iFilterLen) {
  float bTab[iFilterLen];
  for (byte i = 0; i < iFilterLen; i++) bTab[i] = bArray[i];
  int i, j;
  float bTemp;
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
  else bTemp = (bTab[iFilterLen / 2] + bTab[iFilterLen / 2 - 1]) / 2.0;
  return bTemp;
}

int hitungPPM(int adcRaw) {
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
  if (duration == 0) return -1.0;
  return (duration * 0.0343 / 2.0);
}

float hitungDummyDO(float suhuAir) {
  float baseDO = 6.5;
  if (suhuAir > 0 && suhuAir < 50) {
    baseDO = 10.0 - (0.15 * suhuAir); 
  }
  float noise = (random(-10, 11) / 100.0); 
  float result = baseDO + noise;
  if (result < 3.0) result = 3.0;
  if (result > 9.0) result = 9.0;
  return result;
}

void setILI9342Rotation() {
  uint8_t madctl = 0xC8; 
  tft.sendCommand(0x36, &madctl, 1);
  tft.setFullResolution();
}

void toggleHeartbeat() {
  heartBeatState = !heartBeatState;
  tft.fillCircle(305, 13, 4, heartBeatState ? COLOR_TEXT : COLOR_BG);
}

// ================= POWER ON MODEM TTGO & KONEKSI TELKOMSEL =================
void setupGSMTTGO() {
  pinMode(MODEM_PWKEY, OUTPUT);
  pinMode(MODEM_RST, OUTPUT);
  pinMode(MODEM_POWER_ON, OUTPUT);

  digitalWrite(MODEM_POWER_ON, HIGH);
  digitalWrite(MODEM_RST, HIGH);
  
  // Sequence Power On SIM800
  digitalWrite(MODEM_PWKEY, LOW);
  delay(100);
  digitalWrite(MODEM_PWKEY, HIGH);
  delay(1000);
  digitalWrite(MODEM_PWKEY, LOW);

  Serial.println("Memulai komunikasi serial ke SIM800L...");
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(3000);

  // Inisialisasi Kunci Band & IMEI Telkomsel
  Serial.println("Memastikan IMEI dan Band Telkomsel...");
  modem.sendAT("+CBAND=\"ALL_BAND\"");
  modem.waitResponse(1000);

  String setImeiCmd = "+EGMR=1,7,\"" + String(REGISTERED_IMEI) + "\"";
  modem.sendAT(setImeiCmd);
  modem.waitResponse(1000);

  Serial.println("Inisialisasi Modem...");
  if (!modem.restart()) {
    Serial.println("Gagal Restart Modem TTGO!");
    return;
  }

  Serial.print("Mencari Sinyal Kartu Telkomsel...");
  if (!modem.waitForNetwork(60000L)) {
    Serial.println(" Sinyal Telkomsel tidak ditemukan!");
    return;
  }
  Serial.println(" Sinyal Terhubung!");

  Serial.print("Menghubungkan GPRS APN Telkomsel (internet)...");
  if (!modem.gprsConnect(apn, gprsUser, gprsPass)) {
    Serial.println(" Gagal Koneksi GPRS!");
    return;
  }
  Serial.println(" GPRS Telkomsel Berhasil Terhubung!");
// ================= FUNGSI KONTROL RELAY =================
void setRelayState(uint8_t relayNum, bool state) {
  if (relayNum >= 1 && relayNum <= NUM_RELAYS) {
    int idx = relayNum - 1;
    relayStates[idx] = state;
    digitalWrite(RELAY_PINS[idx], state ? RELAY_ON : RELAY_OFF);
    Serial.print("[RELAY] Relay ");
    Serial.print(relayNum);
    Serial.print(" (GPIO ");
    Serial.print(RELAY_PINS[idx]);
    Serial.print(") -> ");
    Serial.println(state ? "ON (LOW)" : "OFF (HIGH)");
  } else if (relayNum == 0) { // 0 = Semua Relay Sekaligus
    for (int i = 0; i < NUM_RELAYS; i++) {
      relayStates[i] = state;
      digitalWrite(RELAY_PINS[i], state ? RELAY_ON : RELAY_OFF);
    }
    Serial.print("[RELAY] Semua Relay (1-5) -> ");
    Serial.println(state ? "ON (LOW)" : "OFF (HIGH)");
  }
}

// Kirim konfirmasi status relay ke MQTT Broker
void publishRelayStatus() {
  if (!mqttClient.connected()) return;

  char statusTopic[64];
  snprintf(statusTopic, sizeof(statusTopic), "tambak/%s/relay/status", DEVICE_ID);

  char statusPayload[128];
  snprintf(statusPayload, sizeof(statusPayload),
    "{\"relay1\":%d,\"relay2\":%d,\"relay3\":%d,\"relay4\":%d,\"relay5\":%d}",
    relayStates[0] ? 1 : 0,
    relayStates[1] ? 1 : 0,
    relayStates[2] ? 1 : 0,
    relayStates[3] ? 1 : 0,
    relayStates[4] ? 1 : 0
  );

  mqttClient.publish(statusTopic, statusPayload);
  Serial.print("[RELAY] Sync Status -> ");
  Serial.println(statusPayload);
}

bool parseStatePayload(const String& payload) {
  if (payload == "1" || payload == "ON" || payload == "on" || payload == "true" || payload == "TRUE") {
    return true;
  }
  return false;
}

// Callback MQTT saat menerima pesan dari Website / Broker
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char payloadStr[length + 1];
  memcpy(payloadStr, payload, length);
  payloadStr[length] = '\0';
  String strPayload = String(payloadStr);
  strPayload.trim();

  Serial.print("[MQTT Perintah] Topik: ");
  Serial.print(topic);
  Serial.print(" | Payload: ");
  Serial.println(strPayload);

  String strTopic = String(topic);

  // 1. Topic Relay Tunggal: tambak/<DEVICE_ID>/relay/1/set s/d 5/set
  for (int i = 1; i <= NUM_RELAYS; i++) {
    String sub = "/relay/" + String(i) + "/set";
    if (strTopic.endsWith(sub) || strTopic.indexOf(sub) != -1) {
      bool st = parseStatePayload(strPayload);
      setRelayState(i, st);
      publishRelayStatus();
      return;
    }
  }

  // 2. Topic Semua Relay: tambak/<DEVICE_ID>/relay/all/set
  if (strTopic.endsWith("/relay/all/set") || strTopic.indexOf("/relay/all") != -1) {
    bool st = parseStatePayload(strPayload);
    setRelayState(0, st);
    publishRelayStatus();
    return;
  }

  // 3. Payload Format JSON: {"relay": 1, "state": 1} atau {"relay1": 1, ...}
  if (strPayload.startsWith("{") && strPayload.endsWith("}")) {
    StaticJsonDocument<128> doc;
    DeserializationError error = deserializeJson(doc, strPayload);
    if (!error) {
      if (doc.containsKey("relay") && doc.containsKey("state")) {
        int rNum = doc["relay"];
        bool st = doc["state"].as<bool>() || (doc["state"] == 1);
        setRelayState(rNum, st);
        publishRelayStatus();
        return;
      }
      bool changed = false;
      for (int i = 1; i <= NUM_RELAYS; i++) {
        String key = "relay" + String(i);
        if (doc.containsKey(key)) {
          bool st = doc[key].as<bool>() || (doc[key] == 1);
          setRelayState(i, st);
          changed = true;
        }
      }
      if (changed) publishRelayStatus();
    }
  }
}

void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Menghubungkan MQTT ke ");
    Serial.print(MQTT_BROKER);
    Serial.print("...");
    if (mqttClient.connect(DEVICE_ID)) {
      Serial.println(" Terhubung!");

      // Subscribe otomatis ke topik perintah relay untuk device ini
      char subTopic[64];
      snprintf(subTopic, sizeof(subTopic), "tambak/%s/relay/#", DEVICE_ID);
      mqttClient.subscribe(subTopic);
      Serial.print("[MQTT] Subscribed ke: ");
      Serial.println(subTopic);

      // Kirim status awal relay ke broker
      publishRelayStatus();
    } else {
      Serial.print(" Gagal, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" Coba lagi dalam 3 detik...");
      delay(3000);
    }
  }
}

void publishSensorData() {
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }

  StaticJsonDocument<256> doc;
  doc["device_id"]  = DEVICE_ID;
  doc["suhu_air"]   = (lastSuhuAir != -999.0 && lastSuhuAir != -127.0) ? lastSuhuAir : 0.0;
  doc["tds_ppm"]    = (lastTdsPPM != -999) ? lastTdsPPM : 0;
  doc["jarak_cm"]   = (lastJarakJSN > 0) ? lastJarakJSN : 0.0;
  doc["suhu_udara"] = (!isnan(lastSuhuUdara)) ? lastSuhuUdara : 0.0;
  doc["lembap_udr"] = (!isnan(lastLembapUdara)) ? lastLembapUdara : 0.0;
  doc["do_mg"]      = (lastDO != -999.0) ? lastDO : 0.0;

  char jsonBuffer[256];
  serializeJson(doc, jsonBuffer);

  mqttClient.publish(MQTT_TOPIC, jsonBuffer);
  Serial.print("Published via Telkomsel -> ");
  Serial.println(jsonBuffer);
}

// ================= UPDATE DISPLAY LCD =================
void updateDisplaySuhuAir(float suhu) {
  tft.fillRect(6, 50, 150, 38, COLOR_BG);
  tft.setCursor(12, 55);
  if (suhu == DEVICE_DISCONNECTED_C || suhu == -127.0 || suhu == -999.0) {
    tft.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    tft.setTextSize(2);
    tft.print("WAITING...");
  } else {
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.setTextSize(3);
    tft.print(suhu, 1);
    tft.setTextSize(2);
    tft.print(" C");
  }
}

void updateDisplayTDS(int ppm) {
  tft.fillRect(164, 50, 150, 38, COLOR_BG);
  tft.setCursor(170, 55);
  if (ppm == -999) {
    tft.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    tft.setTextSize(2);
    tft.print("WAITING...");
  } else {
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.setTextSize(3);
    tft.print(ppm);
    tft.setTextSize(2);
    tft.print(" PPM");
  }
}

void updateDisplayJSN(float jarak) {
  tft.fillRect(6, 116, 150, 38, COLOR_BG);
  tft.setCursor(12, 121);
  if (jarak < 0 || jarak == -999.0) {
    tft.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    tft.setTextSize(2);
    tft.print("WAITING...");
  } else {
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.setTextSize(3);
    tft.print(jarak, 1);
    tft.setTextSize(2);
    tft.print(" cm");
  }
}

void updateDisplayDO(float doVal) {
  tft.fillRect(164, 116, 150, 38, COLOR_BG);
  tft.setCursor(170, 121);
  if (doVal == -999.0) {
    tft.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    tft.setTextSize(2);
    tft.print("WAITING...");
  } else {
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.setTextSize(3);
    tft.print(doVal, 2);
    tft.setTextSize(2);
    tft.print(" mg");
  }
}

void updateDisplayDHT(float tUdara, float hUdara) {
  tft.fillRect(6, 180, 308, 52, COLOR_BG);
  
  if (isnan(tUdara) || isnan(hUdara) || tUdara == -999.0) {
    tft.setCursor(100, 195);
    tft.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    tft.setTextSize(2);
    tft.print("WAITING...");
  } else {
    tft.setCursor(12, 184);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.setTextSize(2);
    tft.print("Suhu Udara : ");
    tft.print(tUdara, 1);
    tft.print(" C");

    tft.setCursor(12, 208);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.setTextSize(2);
    tft.print("Kelembapan : ");
    tft.print(hUdara, 1);
    tft.print(" %");
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(TDS_ADC_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  
  pinMode(RELAY_1, OUTPUT); digitalWrite(RELAY_1, HIGH);
  pinMode(RELAY_2, OUTPUT); digitalWrite(RELAY_2, HIGH);
  pinMode(RELAY_3, OUTPUT); digitalWrite(RELAY_3, HIGH);
  pinMode(RELAY_4, OUTPUT); digitalWrite(RELAY_4, HIGH);
  pinMode(RELAY_5, OUTPUT); digitalWrite(RELAY_5, HIGH);
  
  ds18b20.begin();
  ds18b20.setWaitForConversion(false);
  dht.begin();

  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_RST, HIGH); delay(50);
  digitalWrite(TFT_RST, LOW);  delay(150);
  digitalWrite(TFT_RST, HIGH); delay(150);

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  
  tft.begin();
  SPI.setFrequency(10000000);
  setILI9342Rotation();
  tft.invertDisplay(true); 

  tft.fillScreen(COLOR_BG);

  // Layout UI LCD
  tft.fillRect(0, 0, 320, 26, COLOR_BG);
  tft.drawFastHLine(0, 26, 320, COLOR_BORDER);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(15, 5);
  tft.println("MONITORING TAMBAK");

  tft.drawRoundRect(4, 30, 154, 60, 4, COLOR_BORDER);
  tft.setCursor(10, 35);
  tft.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
  tft.setTextSize(1);
  tft.println("SUHU AIR (DS18B20)");

  tft.drawRoundRect(162, 30, 154, 60, 4, COLOR_BORDER);
  tft.setCursor(168, 35);
  tft.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
  tft.setTextSize(1);
  tft.println("TDS (ESTIMASI PPM)");

  tft.drawRoundRect(4, 96, 154, 60, 4, COLOR_BORDER);
  tft.setCursor(10, 101);
  tft.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
  tft.setTextSize(1);
  tft.println("AIR (JSN-SR04T)");

  tft.drawRoundRect(162, 96, 154, 60, 4, COLOR_BORDER);
  tft.setCursor(168, 101);
  tft.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
  tft.setTextSize(1);
  tft.println("DO SENSOR");

  tft.drawRoundRect(4, 162, 312, 74, 4, COLOR_BORDER);
  tft.setCursor(10, 167);
  tft.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
  tft.setTextSize(1);
  tft.println("UDARA (DHT22)");

  updateDisplaySuhuAir(lastSuhuAir);
  updateDisplayTDS(lastTdsPPM);
  updateDisplayJSN(lastJarakJSN);
  updateDisplayDO(lastDO);
  updateDisplayDHT(lastSuhuUdara, lastLembapUdara);

  // Inisialisasi GSM TTGO Telkomsel & MQTT
  setupGSMTTGO();
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
}

void loop() {
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop();

  // Sampling Analog Buffer TDS
  static unsigned long sampleTime = millis();
  if (millis() - sampleTime > 30U) {
    sampleTime = millis();
    analogBuffer[analogBufferIndex] = analogRead(TDS_ADC_PIN);
    analogBufferIndex++;
    if (analogBufferIndex == SCOUNT) analogBufferIndex = 0;
  }

  // ================= 1. JALUR CEPAT: PEMBACAAN JSN (TIAP 80ms) =================
  if (millis() - lastJSNTime >= INTERVAL_JSN) {
    lastJSNTime = millis();
    float rawJarak = bacaJarakJSN();
    
    if (rawJarak > 0) {
      if (!jsnBufferInitialized) {
        for (int i = 0; i < JSN_SCOUNT; i++) {
          jsnBuffer[i] = rawJarak;
        }
        jsnBufferInitialized = true;
      } else {
        jsnBuffer[jsnBufferIndex] = rawJarak;
        jsnBufferIndex = (jsnBufferIndex + 1) % JSN_SCOUNT;
      }
      
      float jarakSetelahFilter = getMedianFloat(jsnBuffer, JSN_SCOUNT);
      lastJarakJSN = jarakSetelahFilter;
      updateDisplayJSN(lastJarakJSN);
    }
  }

  // ================= 2. JALUR BIASA: SENSOR LAIN (BERGANTIAN TIAP 400ms) =================
  if (millis() - lastReadTime >= INTERVAL_GANTIAN) {
    lastReadTime = millis();
    toggleHeartbeat();

    switch (currentState) {
      case BACA_SUHU_AIR:
        lastSuhuAir = ds18b20.getTempCByIndex(0);
        ds18b20.requestTemperatures();
        updateDisplaySuhuAir(lastSuhuAir);
        currentState = BACA_TDS;
        break;

      case BACA_TDS: {
        int medianADC = getMedianNum(analogBuffer, SCOUNT);
        lastTdsPPM = hitungPPM(medianADC);
        updateDisplayTDS(lastTdsPPM);
        currentState = BACA_DHT;
        break;
      }

      case BACA_DHT:
        lastSuhuUdara = dht.readTemperature();
        lastLembapUdara = dht.readHumidity();
        updateDisplayDHT(lastSuhuUdara, lastLembapUdara);
        currentState = BACA_DO;
        break;

      case BACA_DO:
        lastDO = hitungDummyDO(lastSuhuAir);
        updateDisplayDO(lastDO);
        
        // Kirim data via koneksi GPRS Telkomsel
        publishSensorData();
        
        currentState = BACA_SUHU_AIR;
        break;
    }
  }
}