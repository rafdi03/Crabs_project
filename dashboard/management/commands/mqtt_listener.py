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
from dashboard.models import Alat, DataSensor, RelayState
import paho.mqtt.client as mqtt
import certifi 
from channels.layers import get_channel_layer
from asgiref.sync import async_to_sync

class Command(BaseCommand):
    help = 'Production MQTT Listener v3.1 - Thread-Safe & Unrestricted Sensor Value Acceptance'

    # Configuration
    BROKER = os.environ.get('MQTT_BROKER', 'broker.emqx.io')
    PORT = int(os.environ.get('MQTT_PORT', 1883))
    MQTT_USER = os.environ.get('MQTT_USERNAME', '')
    MQTT_PASS = os.environ.get('MQTT_PASSWORD', '')
    TOPIC_SENSOR = 'tambak/+/sensor'
    TOPIC_RELAY = 'tambak/+/relay/#'
    RATE_LIMIT_SECONDS = int(os.environ.get('RATE_LIMIT', 3))
    
    VALID_DEVICE_PATTERN = r'^[a-zA-Z0-9_-]+$'
    MAX_DATA_AGE_SECONDS = int(os.environ.get('MAX_DATA_AGE', 86400)) # 24 Jam toleransi

    def validate_payload(self, payload, device_id):
        device_ts = None
        
        # Validasi Timestamp (Jika ada)
        if 'timestamp' in payload and payload['timestamp']:
            try:
                timestamp_str = str(payload['timestamp'])
                if '+' in timestamp_str or timestamp_str.endswith('Z'):
                    device_ts = datetime.fromisoformat(timestamp_str.replace('Z', '+00:00'))
                else:
                    device_ts = datetime.fromisoformat(timestamp_str)
                    device_ts = device_ts.replace(tzinfo=timezone.utc)
            except Exception as e:
                device_ts = timezone.now()
        else:
            device_ts = timezone.now()
        
        # CATATAN: Validasi rentang nilai (ranges) telah DIHAPUS sesuai instruksi.
        # Seluruh nilai (termasuk JSN 0.09 cm, 0.0, dsb.) akan langsung diterima dan disimpan.
        return device_ts

    def handle(self, *args, **kwargs):
        def on_connect(client, userdata, flags, rc):
            if rc == 0:
                self.stdout.write(self.style.SUCCESS(f"🔒 Connected to {self.BROKER}:{self.PORT}"))
                client.subscribe([(self.TOPIC_SENSOR, 0), (self.TOPIC_RELAY, 0)])
                self.stdout.write(self.style.SUCCESS(f"📡 Subscribed to {self.TOPIC_SENSOR} & {self.TOPIC_RELAY}"))
            else:
                self.stdout.write(self.style.ERROR(f"❌ Connection failed (rc={rc})"))

        def handle_relay_message(device_id, topic, payload_str):
            try:
                close_old_connections()
                try:
                    alat = Alat.objects.get(id_alat=device_id, status_aktif=True)
                except Alat.DoesNotExist:
                    return

                relay_state, _ = RelayState.objects.get_or_create(alat=alat)
                
                # Cek jika payload dalam bentuk JSON status dari ESP32 (misal: {"relay1":1, "relay2":0, ...})
                if payload_str.startswith('{') and payload_str.endswith('}'):
                    data = json.loads(payload_str)
                    if 'relay1' in data: relay_state.relay1 = bool(data['relay1'])
                    if 'relay2' in data: relay_state.relay2 = bool(data['relay2'])
                    if 'relay3' in data: relay_state.relay3 = bool(data['relay3'])
                    if 'relay4' in data: relay_state.relay4 = bool(data['relay4'])
                    if 'relay5' in data: relay_state.relay5 = bool(data['relay5'])
                    relay_state.save()
                elif 'set' in topic:
                    # Parse dari topic misal "tambak/ESP32-001/relay/1/set"
                    parts = topic.split('/')
                    if len(parts) >= 4 and parts[2].isdigit():
                        r_num = int(parts[2])
                        st = (payload_str in ['1', 'ON', 'on', 'true', 'TRUE'])
                        if r_num == 1: relay_state.relay1 = st
                        elif r_num == 2: relay_state.relay2 = st
                        elif r_num == 3: relay_state.relay3 = st
                        elif r_num == 4: relay_state.relay4 = st
                        elif r_num == 5: relay_state.relay5 = st
                        relay_state.save()
                    elif 'all' in topic:
                        st = (payload_str in ['1', 'ON', 'on', 'true', 'TRUE'])
                        relay_state.relay1 = st
                        relay_state.relay2 = st
                        relay_state.relay3 = st
                        relay_state.relay4 = st
                        relay_state.relay5 = st
                        relay_state.save()

                # Broadcast relay update ke WebSockets
                channel_layer = get_channel_layer()
                async_to_sync(channel_layer.group_send)(
                    'sensor_data',
                    {
                        'type': 'send_relay_data',
                        'data': {
                            'type': 'relay_update',
                            'id_alat': device_id,
                            'relay1': relay_state.relay1,
                            'relay2': relay_state.relay2,
                            'relay3': relay_state.relay3,
                            'relay4': relay_state.relay4,
                            'relay5': relay_state.relay5,
                        }
                    }
                )
                self.stdout.write(self.style.SUCCESS(f"🔌 [{device_id}] Relay State Synced: R1={relay_state.relay1} R2={relay_state.relay2} R3={relay_state.relay3} R4={relay_state.relay4} R5={relay_state.relay5}"))
            except Exception as e:
                self.stdout.write(self.style.ERROR(f"❌ Relay parse error: {e}"))

        def on_message(client, userdata, msg):
            try:
                topic = msg.topic
                topic_parts = topic.split('/')
                device_id = topic_parts[1]
                
                if not re.match(self.VALID_DEVICE_PATTERN, device_id):
                    return

                payload_str = msg.payload.decode('utf-8')

                # Jika pesan adalah topik Relay
                if '/relay' in topic:
                    handle_relay_message(device_id, topic, payload_str)
                    return

                # Rate limiting untuk sensor
                cache_key = f"mqtt_rate_{device_id}"
                last_data = cache.get(cache_key)
                if last_data:
                    elapsed = (timezone.now() - last_data).total_seconds()
                    if elapsed < self.RATE_LIMIT_SECONDS:
                        return 

                # Parse JSON Sensor
                payload = json.loads(payload_str)
                device_ts = self.validate_payload(payload, device_id)
                
                # Cek Database Alat
                try:
                    alat = Alat.objects.get(id_alat=device_id, status_aktif=True)
                except Alat.DoesNotExist:
                    self.stdout.write(self.style.WARNING(f"❓ Unknown device: {device_id}"))
                    return

                # Ekstraksi seluruh nilai sensor (menerima float/int berapapun)
                do_val = float(payload.get('do', 0.0))
                tds_val = float(payload.get('tds', 0.0))
                jsn_val = float(payload.get('jsn', 0.0))
                suhu_air_val = float(payload.get('suhu_air', 0.0))
                suhu_lingkungan_val = float(payload.get('suhu_lingkungan', 0.0))

                # Retry Logic & Save
                max_retries = 3
                saved = False
                
                for attempt in range(max_retries):
                    try:
                        close_old_connections()
                        
                        DataSensor.objects.create(
                            alat=alat,
                            do_level=do_val,
                            tds_level=tds_val,
                            jsn_distance=jsn_val,
                            suhu_air=suhu_air_val,
                            suhu_lingkungan=suhu_lingkungan_val,
                            device_timestamp=device_ts 
                        )
                        
                        cache.set(cache_key, timezone.now(), self.RATE_LIMIT_SECONDS)

                        # 1. Evaluasi Suhu
                        if 25 <= suhu_air_val <= 35:
                            skor_suhu = 5; css_suhu = "success"
                        elif 20 <= suhu_air_val < 25:
                            skor_suhu = 3; css_suhu = "warning"
                        else:
                            skor_suhu = 1; css_suhu = "danger"

                        # 2. Evaluasi DO
                        if do_val > 4:
                            skor_do = 5; css_do = "success"
                        elif 3 <= do_val <= 4:
                            skor_do = 3; css_do = "warning"
                        else:
                            skor_do = 1; css_do = "danger"

                        # 3. Evaluasi TDS
                        if tds_val <= 500:
                            css_tds = "success"
                        elif tds_val <= 800:
                            css_tds = "warning"
                        else:
                            css_tds = "danger"

                        # 4. Status Tambak Keseluruhan
                        total_skor = (skor_suhu * 2) + (skor_do * 2)
                        
                        if total_skor >= 16:
                            status_tambak = "KONDISI PRIMA (AMAN)"
                            badge_color = "bg-success"
                        elif total_skor >= 10:
                            status_tambak = "WASPADA (SEDANG)"
                            badge_color = "bg-warning text-dark"
                        else:
                            status_tambak = "KRITIS (BAHAYA)"
                            badge_color = "bg-danger"

                        # Broadcast data ke WebSockets
                        channel_layer = get_channel_layer()
                        async_to_sync(channel_layer.group_send)(
                            'sensor_data',
                            {
                                'type': 'send_sensor_data',
                                'data': {
                                    'id_alat': device_id,
                                    'do': do_val,
                                    'do_css': css_do,
                                    'suhu_air': suhu_air_val,
                                    'suhu_css': css_suhu,
                                    'tds': tds_val,
                                    'tds_css': css_tds,
                                    'jsn': jsn_val,
                                    'suhu_lingkungan': suhu_lingkungan_val,
                                    'status_tambak': status_tambak,
                                    'badge_color': badge_color
                                }
                            }
                        )
                        self.stdout.write(self.style.SUCCESS(f"✅ [{device_id}] Saved: JSN={jsn_val}cm TDS={tds_val}ppm DO={do_val} SuhuAir={suhu_air_val}C"))
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
            except Exception as e:
                self.stdout.write(self.style.ERROR(f"💥 Error on_message: {type(e).__name__}: {e}"))

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

        self.stdout.write(self.style.SUCCESS("🚀 MQTT Listener v3.1 starting (Validation Removed & Relay Synced)..."))
        
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