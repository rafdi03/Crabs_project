#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SPI.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>

// ================= 1. ALOKASI PIN LCD ILI9342 =================
#define TFT_CS        13  
#define TFT_DC        12  
#define TFT_RST       14  
#define TFT_MOSI      19  
#define TFT_SCLK      18  

// ================= 2. ALOKASI PIN SENSOR =================
#define DS18B20_PIN   15  
#define DHT_PIN       4   
#define DHT_TYPE      DHT22 
#define TDS_ADC_PIN   34  
#define TRIG_PIN      32   
#define ECHO_PIN      36  

// ================= 3. ALOKASI PIN RELAY =================
#define RELAY_1       25  
#define RELAY_2       22  
#define RELAY_3       32  
#define RELAY_4       33  
#define RELAY_5       21  

// DEFINISI WARNA MONOKROM
#define COLOR_BG          ILI9341_BLACK // Hitam
#define COLOR_TEXT        ILI9341_WHITE // Putih
#define COLOR_TEXT_MUTED  0xC618        // Abu-abu Terang
#define COLOR_BORDER      ILI9341_WHITE // Border Putih

const int ADC_AIR_MURNI  = 3800; 
const int PPM_AIR_MURNI  = 10;    
const int ADC_AIR_KERAN  = 1500; 
const int PPM_AIR_KERAN  = 150;   

#define SCOUNT 20
int analogBuffer[SCOUNT];
int analogBufferIndex = 0;

float lastSuhuAir = -999.0;
int lastTdsPPM = -999;
float lastJarakJSN = -999.0;
float validJarakJSN = -999.0; // Menyimpan sampel valid JSN terakhir
float lastSuhuUdara = -999.0;
float lastLembapUdara = -999.0;
float lastDO = -999.0;

// Status Koneksi (Simulasi untuk Tampilan)
bool isWifiConnected = false;
bool isBrokerConnected = false;
int animFrame = 0;

enum SensorState { BACA_SUHU_AIR, BACA_TDS, BACA_JSN, BACA_DHT, BACA_DO };
SensorState currentState = BACA_SUHU_AIR;

unsigned long lastReadTime = 0;
// Interval dinaikkan agar 1 siklus penuh (5 sensor) memakan waktu 2 detik (aman untuk DHT22)
const unsigned long INTERVAL_GANTIAN = 400; 

class ILI9342_Full : public Adafruit_ILI9341 {
  public:
    ILI9342_Full(int8_t cs, int8_t dc, int8_t rst) : Adafruit_ILI9341(cs, dc, rst) {}
    void setFullResolution() { _width = 320; _height = 240; }
};

ILI9342_Full tft = ILI9342_Full(TFT_CS, TFT_DC, TFT_RST);
OneWire oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);
DHT dht(DHT_PIN, DHT_TYPE);

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

int hitungPPM(int adcRaw) {
  long ppm = map(adcRaw, ADC_AIR_MURNI, ADC_AIR_KERAN, PPM_AIR_MURNI, PPM_AIR_KERAN);
  if (ppm < 0) ppm = 0;
  return (int)ppm;
}

float bacaJarakJSN() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(5);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(20);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 40000);
  
  // Jika timeout atau gagal baca pulsa
  if (duration <= 0) return -1.0;
  
  float dist = (duration * 0.0343 / 2.0);
  
  // Validasi rentang fisik JSN-SR04T (10 cm s/d 450 cm)
  if (dist < 10.0 || dist > 450.0) {
    return -1.0; // Nilai tidak valid
  }
  
  return dist;
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

// ================= FUNGSI ANIMASI INDIKATOR KONEKSI =================

void updateConnectionStatus() {
  // Area Status Bar Atas
  tft.fillRect(0, 0, 320, 22, COLOR_BG);
  tft.setTextSize(1);
  
  // 1. Status WiFi
  tft.setCursor(6, 7);
  tft.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
  tft.print("WIFI: ");
  
  if (isWifiConnected) {
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.print("[OK]");
  } else {
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.print("CONNECTING");
    // Efek titik animasi ( .  ..  ... )
    for (int i = 0; i < (animFrame % 4); i++) {
      tft.print(".");
    }
  }

  // 2. Status Broker MQTT
  tft.setCursor(180, 7);
  tft.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
  tft.print("MQTT: ");
  
  if (isBrokerConnected) {
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.print("[CONNECTED]");
  } else {
    tft.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    if (!isWifiConnected) {
      tft.print("WAIT WIFI");
    } else {
      tft.setTextColor(COLOR_TEXT, COLOR_BG);
      tft.print("CONNECTING");
      for (int i = 0; i < (animFrame % 4); i++) {
        tft.print(".");
      }
    }
  }

  tft.drawFastHLine(0, 22, 320, COLOR_BORDER);
  
  animFrame++;
  
  // Simulasi otomatis terhubung setelah beberapa detik (Bisa dihapus jika disambungkan ke fungsi WiFi/MQTT asli)
  if (animFrame == 6) isWifiConnected = true;
  if (animFrame == 10) isBrokerConnected = true;
}

// ================= FUNGSI UPDATE DISPLAY LCD =================

void updateDisplaySuhuAir(float suhu) {
  tft.fillRect(6, 52, 150, 38, COLOR_BG);
  tft.setCursor(12, 57);
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
  tft.fillRect(164, 52, 150, 38, COLOR_BG);
  tft.setCursor(170, 57);
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
  tft.fillRect(6, 118, 150, 38, COLOR_BG);
  tft.setCursor(12, 123);
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
  tft.fillRect(164, 118, 150, 38, COLOR_BG);
  tft.setCursor(170, 123);
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
  tft.fillRect(6, 182, 308, 50, COLOR_BG);
  
  if (isnan(tUdara) || isnan(hUdara) || tUdara == -999.0) {
    tft.setCursor(100, 197);
    tft.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    tft.setTextSize(2);
    tft.print("WAITING...");
  } else {
    tft.setCursor(12, 186);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.setTextSize(2);
    tft.print("Suhu Udara : ");
    tft.print(tUdara, 1);
    tft.print(" C");

    tft.setCursor(12, 210);
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
  // Mode Non-Blocking untuk DS18B20 agar tidak ada jeda 750ms
  ds18b20.setWaitForConversion(false); 
  ds18b20.requestTemperatures();       
  
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

  // --- CARD 1: Suhu Air ---
  tft.drawRoundRect(4, 30, 154, 60, 4, COLOR_BORDER);
  tft.setCursor(10, 35);
  tft.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
  tft.setTextSize(1);
  tft.println("SUHU AIR (DS18B20)");

  // --- CARD 2: TDS ---
  tft.drawRoundRect(162, 30, 154, 60, 4, COLOR_BORDER);
  tft.setCursor(168, 35);
  tft.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
  tft.setTextSize(1);
  tft.println("TDS (ESTIMASI PPM)");

  // --- CARD 3: Ketinggian Air ---
  tft.drawRoundRect(4, 96, 154, 60, 4, COLOR_BORDER);
  tft.setCursor(10, 101);
  tft.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
  tft.setTextSize(1);
  tft.println("AIR (JSN-SR04T)");

  // --- CARD 4: DO Air ---
  tft.drawRoundRect(162, 96, 154, 60, 4, COLOR_BORDER);
  tft.setCursor(168, 101);
  tft.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
  tft.setTextSize(1);
  tft.println("DO SENSOR (DUMMY)");

  // --- CARD 5: Udara (DHT22) ---
  tft.drawRoundRect(4, 162, 312, 74, 4, COLOR_BORDER);
  tft.setCursor(10, 167);
  tft.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
  tft.setTextSize(1);
  tft.println("UDARA (DHT22)");

  // Tampilan Status Awal
  updateConnectionStatus();
  updateDisplaySuhuAir(lastSuhuAir);
  updateDisplayTDS(lastTdsPPM);
  updateDisplayJSN(lastJarakJSN);
  updateDisplayDO(lastDO);
  updateDisplayDHT(lastSuhuUdara, lastLembapUdara);
}

void loop() {
  static unsigned long sampleTime = millis();
  if (millis() - sampleTime > 30U) {
    sampleTime = millis();
    analogBuffer[analogBufferIndex] = analogRead(TDS_ADC_PIN);
    analogBufferIndex++;
    if (analogBufferIndex == SCOUNT) analogBufferIndex = 0;
  }

  if (millis() - lastReadTime >= INTERVAL_GANTIAN) {
    lastReadTime = millis();
    
    updateConnectionStatus();

    switch (currentState) {
      case BACA_SUHU_AIR:
        // Ambil suhu dari request sebelumnya
        lastSuhuAir = ds18b20.getTempCByIndex(0);
        updateDisplaySuhuAir(lastSuhuAir);
        
        // Request suhu SEKARANG untuk diambil pada rotasi berikutnya (Asynchronous)
        ds18b20.requestTemperatures(); 
        
        currentState = BACA_TDS;
        break;

      case BACA_TDS: {
        int medianADC = getMedianNum(analogBuffer, SCOUNT);
        lastTdsPPM = hitungPPM(medianADC);
        updateDisplayTDS(lastTdsPPM);
        currentState = BACA_JSN;
        break;
      }

      case BACA_JSN: {
        float curJarak = bacaJarakJSN();
        if (curJarak > 0.0) {
          // Sampel baru valid: simpan sebagai sampel valid terakhir
          validJarakJSN = curJarak;
          lastJarakJSN = curJarak;
        } else {
          // Sampel baru tidak valid: gunakan sampel valid sebelumnya agar tidak muncul WAITING
          if (validJarakJSN > 0.0) {
            lastJarakJSN = validJarakJSN;
          } else {
            lastJarakJSN = -999.0; // Hanya WAITING jika belum pernah terbaca sejak boot awal
          }
        }
        updateDisplayJSN(lastJarakJSN);
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
        currentState = BACA_SUHU_AIR;
        break;
    }
  }
}