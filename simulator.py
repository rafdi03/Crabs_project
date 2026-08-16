import paho.mqtt.client as mqtt
import time
import json
import random
from datetime import datetime, timezone # Ditambahkan untuk generate waktu ISO

# Konfigurasi EMQX 
BROKER = 'broker.emqx.io'
PORT = 1883

ID_ALAT = 'ESP32-002' 

TOPIC = f'tambak/{ID_ALAT}/sensor'

client = mqtt.Client()
client.connect(BROKER, PORT, 60)

print(f"📡 Memulai Simulasi ESP32 ke topic: {TOPIC}")
print("Tekan CTRL + C untuk menghentikan.\n")

try:
    while True:
        # Membuat data sensor acak untuk simulasi sesuai payload baru ESP32
        payload = {
            "device_id": ID_ALAT,
            "suhu_air": round(random.uniform(25.0, 32.0), 1),        # Celcius
            "tds_ppm": random.randint(300, 700),                     # ppm
            "jarak_cm": round(random.uniform(28.0, 36.0), 1),        # cm (Jarak realistis sensor ke air tambak)
            "suhu_udara": round(random.uniform(27.0, 33.0), 1),      # Celcius
            "lembap_udr": round(random.uniform(60.0, 85.0), 1),      # %
            "do_mg": round(random.uniform(4.5, 7.5), 1),             # mg/L
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