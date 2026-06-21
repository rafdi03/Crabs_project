import paho.mqtt.client as mqtt
import time
import json
import random
from datetime import datetime, timezone # Ditambahkan untuk generate waktu ISO

# Konfigurasi EMQX 
BROKER = 'broker.emqx.io'
PORT = 1883

ID_ALAT = 'ESP32-001' 

TOPIC = f'tambak/{ID_ALAT}/sensor'

client = mqtt.Client()
client.connect(BROKER, PORT, 60)

print(f"📡 Memulai Simulasi ESP32 ke topic: {TOPIC}")
print("Tekan CTRL + C untuk menghentikan.\n")

try:
    while True:
        # Membuat data sensor acak untuk simulasi
        payload = {
            "do": round(random.uniform(2.5, 6.0), 1),               # mg/L
            "tds": random.randint(300, 900),                        # ppm
            "jsn": round(random.uniform(10.0, 50.0), 1),            # cm
            "suhu_air": round(random.uniform(24.0, 32.0), 1),       # Celcius
            "suhu_lingkungan": round(random.uniform(28.0, 34.0), 1),# Celcius
            
            # Tambahan Waktu (Timestamp) agar lolos validasi MAX_DATA_AGE
            "timestamp": datetime.now(timezone.utc).isoformat()
        }
        
        # Mengubah data dictionary Python ke format JSON string
        pesan_json = json.dumps(payload)
        
        # Publish (Kirim) ke EMQX
        client.publish(TOPIC, pesan_json)
        print(f"📤 Data Terkirim: {pesan_json}")
        
        # Tunggu 5 detik sebelum mengirim data berikutnya
        time.sleep(5) 
        
except KeyboardInterrupt:
    print("\n🛑 Simulasi dihentikan.")
    client.disconnect()