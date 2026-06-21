import json
import re
import time
import ssl
import os
import signal
from datetime import datetime
from django.utils import timezone
from django.db import close_old_connections, OperationalError, IntegrityError
from django.core.cache import cache
from django.core.management.base import BaseCommand
from dashboard.models import Alat, DataSensor
import paho.mqtt.client as mqtt
import certifi 

class Command(BaseCommand):
    help = 'Production MQTT Listener v3.0 - Thread-Safe & Cross-Platform'

    # Configuration
    BROKER = os.environ.get('MQTT_BROKER', 'broker.emqx.io')
    PORT = int(os.environ.get('MQTT_PORT', 1883)) # Gunakan 8883 jika pakai TLS/SSL
    MQTT_USER = os.environ.get('MQTT_USERNAME', '')
    MQTT_PASS = os.environ.get('MQTT_PASSWORD', '')
    TOPIC = 'tambak/+/sensor'
    RATE_LIMIT_SECONDS = int(os.environ.get('RATE_LIMIT', 5))
    
    # Regex diperbaiki agar fleksibel untuk berbagai nama ID Alat
    VALID_DEVICE_PATTERN = r'^[a-zA-Z0-9_-]+$'
    MAX_DATA_AGE_SECONDS = int(os.environ.get('MAX_DATA_AGE', 300))

    def validate_payload(self, payload, device_id):
        device_ts = None
        
        # Validasi Umur Data (Timezone-Aware)
        if 'timestamp' in payload:
            try:
                timestamp_str = payload['timestamp']
                
                if '+' in timestamp_str or timestamp_str.endswith('Z'):
                    device_ts = datetime.fromisoformat(timestamp_str)
                else:
                    device_ts = datetime.fromisoformat(timestamp_str)
                    device_ts = device_ts.replace(tzinfo=timezone.utc)
                
                time_diff = abs((timezone.now() - device_ts).total_seconds())
                if time_diff > self.MAX_DATA_AGE_SECONDS:
                    raise ValueError(f"Data too old ({device_id}): {time_diff:.0f}s")
                    
            except ValueError as e:
                if "Invalid isoformat string" not in str(e):
                    self.stdout.write(self.style.WARNING(f"Invalid timestamp: {e}"))
                raise
        
        # Validasi Rentang Sensor
        ranges = {
            'do': (0.0, 20.0, "DO (mg/L)"),
            'tds': (0.0, 5000.0, "TDS (ppm)"),
            'suhu_air': (15.0, 40.0, "Water temp (°C)"),
            'suhu_lingkungan': (20.0, 45.0, "Air temp (°C)"),
            'jsn': (10.0, 600.0, "JSN distance (cm)"),
        }
        
        for field, (min_val, max_val, label) in ranges.items():
            value = payload.get(field, 0.0)
            if not (min_val <= value <= max_val):
                raise ValueError(f"{label} unrealistic: {value:.2f} (range: {min_val}-{max_val})")
        
        return device_ts 

    def handle(self, *args, **kwargs):
        def on_connect(client, userdata, flags, rc):
            if rc == 0:
                self.stdout.write(self.style.SUCCESS(f"🔒 Connected to {self.BROKER}:{self.PORT}"))
                client.subscribe(self.TOPIC)
            else:
                self.stdout.write(self.style.ERROR(f"❌ Connection failed (rc={rc})"))

        def on_message(client, userdata, msg):
            try:
                topic_parts = msg.topic.split('/')
                device_id = topic_parts[1]
                
                if not re.match(self.VALID_DEVICE_PATTERN, device_id):
                    self.stdout.write(self.style.WARNING(f"⚠️ Rejected ID: {device_id}"))
                    return

                # Rate limiting
                cache_key = f"mqtt_rate_{device_id}"
                last_data = cache.get(cache_key)
                if last_data:
                    elapsed = (timezone.now() - last_data).total_seconds()
                    if elapsed < self.RATE_LIMIT_SECONDS:
                        return 

                # Parse & Validate
                payload = json.loads(msg.payload.decode('utf-8'))
                device_ts = self.validate_payload(payload, device_id)
                
                # Cek Database
                try:
                    alat = Alat.objects.get(id_alat=device_id, status_aktif=True)
                except Alat.DoesNotExist:
                    self.stdout.write(self.style.WARNING(f"❓ Unknown device: {device_id}"))
                    return

                # Retry Logic & Save
                max_retries = 3
                saved = False
                
                for attempt in range(max_retries):
                    try:
                        close_old_connections()
                        
                        DataSensor.objects.create(
                            alat=alat,
                            do_level=payload.get('do', 0.0),
                            tds_level=payload.get('tds', 0.0),
                            jsn_distance=payload.get('jsn', 0.0),
                            suhu_air=payload.get('suhu_air', 0.0),
                            suhu_lingkungan=payload.get('suhu_lingkungan', 0.0),
                            device_timestamp=device_ts 
                        )
                        
                        cache.set(cache_key, timezone.now(), self.RATE_LIMIT_SECONDS)
                        self.stdout.write(self.style.SUCCESS(f"✅ [{device_id}] Saved"))
                        saved = True
                        break
                        
                    except OperationalError as e:
                        if "gone away" in str(e).lower() or "2006" in str(e).lower():
                            self.stdout.write(self.style.WARNING(f"🔄 DB reconnect attempt {attempt+1}/{max_retries}"))
                            close_old_connections()
                            time.sleep(2)
                        elif attempt == max_retries - 1:
                            raise
                    except IntegrityError:
                        break
                
                if not saved:
                    self.stdout.write(self.style.ERROR(f"💥 Failed to save {device_id}"))

            except json.JSONDecodeError:
                self.stdout.write(self.style.ERROR(f"❌ Invalid JSON"))
            except ValueError as e:
                self.stdout.write(self.style.WARNING(f"⚠️ Validation: {e}"))
            except Exception as e:
                self.stdout.write(self.style.ERROR(f"💥 Critical: {type(e).__name__}: {e}"))

        client = mqtt.Client()
        client.on_connect = on_connect
        client.on_message = on_message

        if self.MQTT_USER and self.MQTT_PASS:
            client.username_pw_set(self.MQTT_USER, self.MQTT_PASS)

        if self.PORT == 8883:
            ca_path = os.environ.get('MQTT_CA_CERT', certifi.where())
            client.tls_set(ca_certs=ca_path, cert_reqs=ssl.CERT_REQUIRED, tls_version=ssl.PROTOCOL_TLSv1_2)

        def signal_handler(sig, frame):
            self.stdout.write("\n🛑 Shutting down gracefully...")
            client.disconnect()
            exit(0)
        
        signal.signal(signal.SIGINT, signal_handler)
        signal.signal(signal.SIGTERM, signal_handler)

        self.stdout.write(self.style.SUCCESS("🚀 MQTT Listener v3.0 starting..."))
        
        while True:
            try:
                client.connect(self.BROKER, self.PORT, 60)
                client.loop_forever()
            except ConnectionRefusedError:
                self.stdout.write(self.style.ERROR(f"🔄 Retrying in 10s..."))
                time.sleep(10)
            except Exception as e:
                self.stdout.write(self.style.ERROR(f"Connection error: {e}"))
                time.sleep(5)