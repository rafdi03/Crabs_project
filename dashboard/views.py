from django.shortcuts import render, get_object_or_404
from django.db.models import Avg
from django.utils import timezone
from datetime import timedelta, datetime
from django.http import HttpResponse, JsonResponse
from .models import Lokasi, Alat, DataSensor, RelayState, Firmware
from django.db.models.functions import ExtractMonth, ExtractYear, TruncDate, TruncMonth, TruncHour
from channels.layers import get_channel_layer
from asgiref.sync import async_to_sync
import paho.mqtt.publish as publish
import csv
import json
import os
from collections import defaultdict
from urllib.parse import urlparse

MQTT_BROKER = os.environ.get('MQTT_BROKER', 'broker.emqx.io')
MQTT_PORT = int(os.environ.get('MQTT_PORT', 1883))


# 1. Halaman Utama — dashboard bersifat terbuka tanpa login.
def halaman_utama(request):
    semua_lokasi = Lokasi.objects.all()
    return render(request, 'dashboard/home.html', {'daftar_lokasi': semua_lokasi})

# 2. Detail Lokasi (Dengan Inisialisasi RelayState per Alat)
def detail_lokasi(request, lokasi_id):
    lokasi_terpilih = get_object_or_404(Lokasi, id=lokasi_id)
    daftar_alat = Alat.objects.filter(lokasi=lokasi_terpilih, status_aktif=True)
    alat_data_list = []
    for alat in daftar_alat:
        semua_history = alat.data_sensor.all()[:30]
        history_grafik = alat.data_sensor.all()[:20][::-1]
        
        waktu = [timezone.localtime(d.timestamp).strftime('%H:%M') for d in history_grafik]
        do_data = [round(float(d.do_level), 2) for d in history_grafik]
        tds_data = [round(float(d.tds_level), 2) for d in history_grafik]
        suhu_air_data = [round(float(d.suhu_air), 2) for d in history_grafik]
        suhu_lingkungan_data = [round(float(d.suhu_lingkungan), 2) for d in history_grafik]

        chart_data = json.dumps({
            'waktu': waktu,
            'do': do_data,
            'tds': tds_data,
            'suhu_air': suhu_air_data,
            'suhu_lingkungan': suhu_lingkungan_data
        })

        # Pastikan status relay tersedia
        relay_state, _ = RelayState.objects.get_or_create(alat=alat)
        firmware_list = Firmware.objects.filter(device_id=alat.id_alat)[:10]

        alat_data_list.append({
            'alat': alat,
            'sensor_terbaru': alat.data_sensor.first(),
            'chart_data': chart_data,
            'tabel_riwayat': semua_history,
            'relay_state': relay_state,
            'firmware_list': firmware_list,
        })

    context = {
        'lokasi': lokasi_terpilih,
        'alat_data_list': alat_data_list
    }
    return render(request, 'dashboard/detail_lokasi.html', context)

# 6. API Kontrol Relay (Web -> MQTT & WebSockets)
def relay_control(request):
    if request.method != 'POST':
        return JsonResponse({'error': 'Metode request harus POST'}, status=405)
    
    try:
        data = json.loads(request.body.decode('utf-8'))
        id_alat = data.get('id_alat')
        relay_num = int(data.get('relay_num', 1)) # 1..5 atau 0 untuk all
        state = bool(data.get('state', False))    # True = ON, False = OFF

        alat = get_object_or_404(Alat, id_alat=id_alat)
        relay_state, _ = RelayState.objects.get_or_create(alat=alat)

        # Update status relay di database
        if relay_num == 0:
            # Kontrol Semua Relay
            relay_state.relay1 = state
            relay_state.relay2 = state
            relay_state.relay3 = state
            relay_state.relay4 = state
            relay_state.relay5 = state
            mqtt_topic = f"tambak/{id_alat}/relay/all/set"
            mqtt_payload = "1" if state else "0"
        elif relay_num == 1:
            relay_state.relay1 = state
            mqtt_topic = f"tambak/{id_alat}/relay/1/set"
            mqtt_payload = "1" if state else "0"
        elif relay_num == 2:
            relay_state.relay2 = state
            mqtt_topic = f"tambak/{id_alat}/relay/2/set"
            mqtt_payload = "1" if state else "0"
        elif relay_num == 3:
            relay_state.relay3 = state
            mqtt_topic = f"tambak/{id_alat}/relay/3/set"
            mqtt_payload = "1" if state else "0"
        elif relay_num == 4:
            relay_state.relay4 = state
            mqtt_topic = f"tambak/{id_alat}/relay/4/set"
            mqtt_payload = "1" if state else "0"
        elif relay_num == 5:
            relay_state.relay5 = state
            mqtt_topic = f"tambak/{id_alat}/relay/5/set"
            mqtt_payload = "1" if state else "0"
        else:
            return JsonResponse({'error': 'Nomor relay tidak valid (1-5)'}, status=400)

        relay_state.save()

        # Publish perintah ke MQTT Broker EMQX
        try:
            publish.single(mqtt_topic, mqtt_payload, hostname=MQTT_BROKER, port=MQTT_PORT, keepalive=10)
        except Exception:
            pass

        # Broadcast update status relay ke seluruh klien WebSocket (opsional / fallback ke polling)
        try:
            channel_layer = get_channel_layer()
            if channel_layer:
                async_to_sync(channel_layer.group_send)(
                    'sensor_data',
                    {
                        'type': 'send_relay_data',
                        'data': {
                            'type': 'relay_update',
                            'id_alat': id_alat,
                            'relay1': relay_state.relay1,
                            'relay2': relay_state.relay2,
                            'relay3': relay_state.relay3,
                            'relay4': relay_state.relay4,
                            'relay5': relay_state.relay5,
                        }
                    }
                )
        except Exception:
            pass

        return JsonResponse({
            'success': True,
            'id_alat': id_alat,
            'relay_num': relay_num,
            'state': state,
            'relay_states': {
                'relay1': relay_state.relay1,
                'relay2': relay_state.relay2,
                'relay3': relay_state.relay3,
                'relay4': relay_state.relay4,
                'relay5': relay_state.relay5,
            }
        })

    except Exception as e:
        return JsonResponse({'error': str(e)}, status=500)

# 7. API Get Relay Status
def get_relay_status(request, alat_id):
    try:
        alat = get_object_or_404(Alat, id_alat=alat_id)
        relay_state, _ = RelayState.objects.get_or_create(alat=alat)
        return JsonResponse({
            'success': True,
            'id_alat': alat_id,
            'relay1': relay_state.relay1,
            'relay2': relay_state.relay2,
            'relay3': relay_state.relay3,
            'relay4': relay_state.relay4,
            'relay5': relay_state.relay5,
        })
    except Exception as e:
        return JsonResponse({'error': str(e)}, status=500)

# 8. API Get Latest Sensor Data (Smart Fallback Polling jika WebSocket bermasalah)
def get_sensor_terbaru(request, alat_id):
    try:
        alat = get_object_or_404(Alat, id_alat=alat_id, status_aktif=True)
        sensor = alat.data_sensor.first()
        relay_state, _ = RelayState.objects.get_or_create(alat=alat)

        if not sensor:
            return JsonResponse({'success': False, 'message': 'Belum ada data sensor'})

        # Nilai Sensor Real (Dibulatkan seragam 2 angka di belakang koma :.2f)
        suhu_air_val = round(float(sensor.suhu_air), 2)
        suhu_lingk_val = round(float(sensor.suhu_lingkungan), 2)
        do_val = round(float(sensor.do_level), 2)
        tds_val = round(float(sensor.tds_level), 2)
        jsn_val = round(float(sensor.jsn_distance), 2)
        lembap_val = round(float(sensor.kelembaban_udara), 2)

        # Evaluasi Status Berdasarkan Threshold
        css_suhu = "success" if 25 <= suhu_air_val <= 35 else ("warning" if 20 <= suhu_air_val < 25 else "danger")
        css_do = "success" if do_val >= 4.0 else ("warning" if do_val >= 3.0 else "danger")
        css_tds = "success" if tds_val <= 500 else ("warning" if tds_val <= 800 else "danger")

        # Skor Tambak
        skor_suhu = 5 if css_suhu == "success" else (3 if css_suhu == "warning" else 1)
        skor_do = 5 if css_do == "success" else (3 if css_do == "warning" else 1)
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

        local_ts = timezone.localtime(sensor.timestamp)

        return JsonResponse({
            'success': True,
            'id_alat': alat_id,
            'suhu_air': suhu_air_val,
            'suhu_lingkungan': suhu_lingk_val,
            'suhu_udara': suhu_lingk_val,
            'do': do_val,
            'do_mg': do_val,
            'tds': tds_val,
            'tds_ppm': tds_val,
            'jsn': jsn_val,
            'jarak_cm': jsn_val,
            'lembap_udr': lembap_val,
            'kelembaban_udara': lembap_val,
            'timestamp': local_ts.strftime('%d %b %Y, %H:%M:%S'),
            'timestamp_iso': local_ts.isoformat(),
            'time_only': local_ts.strftime('%H:%M:%S'),
            'status_tambak': status_tambak,
            'badge_color': badge_color,
            'suhu_css': css_suhu,
            'do_css': css_do,
            'tds_css': css_tds,
            'relay_states': {
                'relay1': relay_state.relay1,
                'relay2': relay_state.relay2,
                'relay3': relay_state.relay3,
                'relay4': relay_state.relay4,
                'relay5': relay_state.relay5,
            }
        })
    except Exception as e:
        return JsonResponse({'success': False, 'error': str(e)}, status=500)

# 8. Download CSV (Format .2f untuk semua data numerik sensor)
def download_csv_lokasi(request, lokasi_id):
    lokasi = get_object_or_404(Lokasi, id=lokasi_id)
    response = HttpResponse(content_type='text/csv')
    response['Content-Disposition'] = f'attachment; filename="Data_Area_{lokasi.nama_daerah}.csv"'
    writer = csv.writer(response)
    writer.writerow(['Nama Kolam/Alat', 'Timestamp', 'DO (mg/L)', 'TDS (ppm)', 'Jarak JSN (cm)', 'Suhu Air (C)', 'Suhu Udara (C)', 'Kelembaban Udara (%)'])
    daftar_alat = Alat.objects.filter(lokasi=lokasi, status_aktif=True)
    for alat in daftar_alat:
        for data in alat.data_sensor.all():
            writer.writerow([
                alat.nama_kolam,
                timezone.localtime(data.timestamp).strftime('%Y-%m-%d %H:%M:%S'),
                f"{data.do_level:.2f}",
                f"{data.tds_level:.2f}",
                f"{data.jsn_distance:.2f}",
                f"{data.suhu_air:.2f}",
                f"{data.suhu_lingkungan:.2f}",
                f"{data.kelembaban_udara:.2f}"
            ])
    return response

# 9. API chart-bulanan
def chart_bulanan(request, alat_id):
    try:
        alat = Alat.objects.get(id_alat=alat_id, status_aktif=True)
    except Alat.DoesNotExist:
        return JsonResponse({'error': 'Alat tidak ditemukan'}, status=404)
    mode = request.GET.get('mode', 'raw')
    end_date = timezone.now()
    start_date = end_date - timedelta(days=30)

    if mode == 'aggregate':
        data_harian = (
            DataSensor.objects
            .filter(alat=alat, timestamp__gte=start_date, timestamp__lte=end_date)
            .annotate(hari=TruncDate('timestamp'))
            .values('hari')
            .annotate(
                avg_suhu_air=Avg('suhu_air'),
                avg_suhu_lingkungan=Avg('suhu_lingkungan'),
                avg_do=Avg('do_level'),
                avg_tds=Avg('tds_level'),
                avg_jsn=Avg('jsn_distance')
            )
            .order_by('hari')
        )
        result = {
            'labels': [d['hari'].strftime('%d %b') for d in data_harian],
            'suhu_air': [round(float(d['avg_suhu_air']), 2) for d in data_harian],
            'suhu_lingkungan': [round(float(d['avg_suhu_lingkungan']), 2) for d in data_harian],
            'do': [round(float(d['avg_do']), 2) for d in data_harian],
            'tds': [round(float(d['avg_tds']), 2) for d in data_harian],
            'jsn': [round(float(d['avg_jsn']), 2) for d in data_harian],
        }
    else:
        data = DataSensor.objects.filter(
            alat=alat,
            timestamp__gte=start_date,
            timestamp__lte=end_date
        ).order_by('timestamp')
        result = {
            'labels': [timezone.localtime(d.timestamp).strftime('%d %H:%M') for d in data],
            'suhu_air': [round(float(d.suhu_air), 2) for d in data],
            'suhu_lingkungan': [round(float(d.suhu_lingkungan), 2) for d in data],
            'do': [round(float(d.do_level), 2) for d in data],
            'tds': [round(float(d.tds_level), 2) for d in data],
            'jsn': [round(float(d.jsn_distance), 2) for d in data],
        }
    return JsonResponse(result)

# 10. API chart-data (Support 10 Menit & Anti-Lag)
def chart_data(request, alat_id):
    try:
        alat = Alat.objects.get(id_alat=alat_id, status_aktif=True)
    except Alat.DoesNotExist:
        return JsonResponse({'error': 'Alat tidak ditemukan'}, status=404)

    period = request.GET.get('period', 'monthly')
    filter_val = request.GET.get('filter', '')

    if period == 'daily':
        try:
            dt = datetime.strptime(filter_val, '%Y-%m-%d')
        except (ValueError, TypeError):
            dt = timezone.now().date()
        start_date = dt
        end_date = dt + timedelta(days=1)
        
        data = DataSensor.objects.filter(
            alat=alat,
            timestamp__gte=start_date,
            timestamp__lt=end_date
        ).order_by('timestamp')
        
        labels = [timezone.localtime(d.timestamp).strftime('%H:%M') for d in data]
        result = {
            'labels': labels,
            'suhu_air': [round(float(d.suhu_air), 2) for d in data],
            'suhu_lingkungan': [round(float(d.suhu_lingkungan), 2) for d in data],
            'do': [round(float(d.do_level), 2) for d in data],
            'tds': [round(float(d.tds_level), 2) for d in data],
            'jsn': [round(float(d.jsn_distance), 2) for d in data],
        }
        return JsonResponse(result)

    elif period == 'weekly':
        try:
            if 'W' in filter_val:
                parts = filter_val.split('-')
                year = int(parts[0])
                week = int(parts[1].replace('W', ''))
            else:
                year, week = map(int, filter_val.split('-'))
            start_date = datetime.fromisocalendar(year, week, 1)
            end_date = start_date + timedelta(days=7)
        except:
            today = timezone.now().date()
            start_date = today - timedelta(days=today.weekday())
            end_date = start_date + timedelta(days=7)

    elif period == 'monthly':
        try:
            year, month = map(int, filter_val.split('-'))
            start_date = datetime(year, month, 1)
            if month == 12:
                end_date = datetime(year+1, 1, 1)
            else:
                end_date = datetime(year, month+1, 1)
        except:
            today = timezone.now()
            start_date = today.replace(day=1, hour=0, minute=0, second=0, microsecond=0)
            if today.month == 12:
                end_date = today.replace(year=today.year+1, month=1, day=1)
            else:
                end_date = today.replace(month=today.month+1, day=1)

    elif period == 'yearly':
        try:
            year = int(filter_val)
            start_date = datetime(year, 1, 1)
            end_date = datetime(year+1, 1, 1)
        except:
            year = timezone.now().year
            start_date = datetime(year, 1, 1)
            end_date = datetime(year+1, 1, 1)

    # Fetch data mentah dari database
    data_qs = DataSensor.objects.filter(
        alat=alat,
        timestamp__gte=start_date,
        timestamp__lt=end_date
    ).order_by('timestamp')

    def aggregate_data(qs, interval_menit):
        buckets = defaultdict(list)
        for d in qs:
            local_t = timezone.localtime(d.timestamp)
            minute_bucket = (local_t.minute // interval_menit) * interval_menit
            t = local_t.replace(minute=minute_bucket, second=0, microsecond=0)
            buckets[t].append(d)
            
        aggregated = []
        for t in sorted(buckets.keys()):
            items = buckets[t]
            def get_avg(attr):
                vals = [getattr(i, attr) for i in items if getattr(i, attr) is not None]
                return round(sum(vals) / len(vals), 2) if vals else None
                
            aggregated.append({
                'timestamp': t,
                'do_level': get_avg('do_level'),
                'suhu_air': get_avg('suhu_air'),
                'tds_level': get_avg('tds_level'),
                'jsn_distance': get_avg('jsn_distance'),
                'suhu_lingkungan': get_avg('suhu_lingkungan'),
            })
        return aggregated

    if period in ['weekly', 'monthly']:
        aggregated = aggregate_data(data_qs, 10)
        if period == 'weekly':
            labels = [d['timestamp'].strftime('%a %H:%M') for d in aggregated]
        else:
            labels = [d['timestamp'].strftime('%d %b %H:%M') for d in aggregated]
            
    elif period == 'yearly':
        aggregated = aggregate_data(data_qs, 60) 
        labels = [d['timestamp'].strftime('%d %b %H:%M') for d in aggregated]

    result = {
        'labels': labels,
        'suhu_air': [d['suhu_air'] for d in aggregated],
        'suhu_lingkungan': [d['suhu_lingkungan'] for d in aggregated],
        'do': [d['do_level'] for d in aggregated],
        'tds': [d['tds_level'] for d in aggregated],
        'jsn': [d['jsn_distance'] for d in aggregated],
    }
    return JsonResponse(result)

def firmware_upload(request):
    """Upload file .bin firmware baru."""
    if request.method != 'POST':
        return JsonResponse({'error': 'Method not allowed'}, status=405)

    try:
        device_id = request.POST.get('device_id', '').strip()
        version = request.POST.get('version', '').strip()
        notes = request.POST.get('notes', '').strip()
        file = request.FILES.get('file')

        if not device_id:
            return JsonResponse({'error': 'Device ID wajib diisi'}, status=400)
        if not version:
            return JsonResponse({'error': 'Version wajib diisi'}, status=400)
        if not file:
            return JsonResponse({'error': 'File firmware wajib diupload'}, status=400)
        if not file.name.lower().endswith('.bin'):
            return JsonResponse({'error': 'File harus berformat .bin'}, status=400)
        if file.size > 3 * 1024 * 1024:
            return JsonResponse({'error': 'File terlalu besar (max 3MB)'}, status=400)

        try:
            Alat.objects.get(id_alat=device_id, status_aktif=True)
        except Alat.DoesNotExist:
            return JsonResponse({'error': f'Device {device_id} tidak terdaftar atau tidak aktif'}, status=404)

        fw = Firmware.objects.create(
            device_id=device_id,
            version=version,
            file=file,
            notes=notes,
        )

        return JsonResponse({
            'success': True,
            'id': fw.id,
            'device_id': fw.device_id,
            'version': fw.version,
            'file_size_kb': fw.file_size_kb(),
            'file_url': fw.file.url,
        })

    except Exception as e:
        return JsonResponse({'error': str(e)}, status=500)


def firmware_list(request, device_id):
    """List semua firmware untuk device tertentu."""
    try:
        firmwares = Firmware.objects.filter(device_id=device_id)[:20]
        data = []
        for fw in firmwares:
            data.append({
                'id': fw.id,
                'version': fw.version,
                'file_size_kb': fw.file_size_kb(),
                'status': fw.status,
                'progress': fw.progress,
                'last_error': fw.last_error,
                'uploaded_at': timezone.localtime(fw.uploaded_at).strftime('%d %b %Y, %H:%M'),
                'triggered_at': timezone.localtime(fw.triggered_at).strftime('%d %b %Y, %H:%M') if fw.triggered_at else None,
                'notes': fw.notes or '',
            })
        return JsonResponse({'success': True, 'firmwares': data})
    except Exception as e:
        return JsonResponse({'success': False, 'error': str(e)}, status=500)


def firmware_trigger_ota(request, firmware_id):
    """Publish URL firmware ke MQTT → ESP32 akan download & flash."""
    if request.method != 'POST':
        return JsonResponse({'error': 'Method not allowed'}, status=405)

    try:
        fw = Firmware.objects.get(id=firmware_id)
    except Firmware.DoesNotExist:
        return JsonResponse({'error': 'Firmware tidak ditemukan'}, status=404)

    try:
        alat = Alat.objects.get(id_alat=fw.device_id, status_aktif=True)
    except Alat.DoesNotExist:
        return JsonResponse({'error': f'Alat {fw.device_id} tidak aktif'}, status=404)

    # Build URL publik firmware
    from django.conf import settings
    site_url = getattr(settings, 'SITE_URL', 'http://localhost:8000').rstrip('/')
    parsed_site_url = urlparse(site_url)
    if parsed_site_url.scheme not in {'http', 'https'} or not parsed_site_url.netloc:
        return JsonResponse({'error': 'SITE_URL harus berupa URL http:// atau https:// yang valid.'}, status=400)
    if parsed_site_url.hostname in {'localhost', '127.0.0.1', '::1'}:
        return JsonResponse({
            'error': 'SITE_URL masih localhost. Atur SITE_URL ke alamat publik/tunnel atau IP LAN yang dapat dijangkau ESP32.'
        }, status=400)
    firmware_url = f"{site_url}{fw.file.url}"

    topic = f"tambak/{fw.device_id}/ota/url"
    fw.status = 'queued'
    fw.progress = 0
    fw.triggered_at = timezone.now()
    fw.completed_at = None
    fw.last_error = None
    fw.save(update_fields=['status', 'progress', 'triggered_at', 'completed_at', 'last_error'])

    try:
        auth = None
        mqtt_username = os.environ.get('MQTT_USERNAME', '')
        mqtt_password = os.environ.get('MQTT_PASSWORD', '')
        if mqtt_username and mqtt_password:
            auth = {'username': mqtt_username, 'password': mqtt_password}
        tls = {} if MQTT_PORT == 8883 else None
        publish.single(
            topic,
            firmware_url,
            hostname=MQTT_BROKER,
            port=MQTT_PORT,
            qos=1,
            keepalive=15,
            auth=auth,
            tls=tls,
        )
        print(f"📡 [OTA] Published to {topic}: {firmware_url}")
    except Exception as e:
        fw.status = 'error'
        fw.last_error = str(e)[:128]
        fw.completed_at = timezone.now()
        fw.save(update_fields=['status', 'last_error', 'completed_at'])
        return JsonResponse({'error': f'MQTT publish gagal: {e}'}, status=500)

    # Broadcast ke WebSocket langsung
    try:
        channel_layer = get_channel_layer()
        if channel_layer:
            async_to_sync(channel_layer.group_send)(
                'sensor_data',
                {
                    'type': 'send_ota_status',
                    'data': {
                        'type': 'ota_status',
                        'firmware_id': fw.id,
                        'id_alat': fw.device_id,
                        'version': fw.version,
                        'state': 'queued',
                        'progress': 0,
                        'msg': 'URL dikirim ke ESP32...',
                    }
                }
            )
    except Exception:
        pass

    return JsonResponse({
        'success': True,
        'firmware_id': fw.id,
        'topic': topic,
        'firmware_url': firmware_url,
        'version': fw.version,
    })


def firmware_delete(request, firmware_id):
    """Hapus file firmware."""
    if request.method != 'POST':
        return JsonResponse({'error': 'Method not allowed'}, status=405)
    try:
        fw = Firmware.objects.get(id=firmware_id)
        fw.file.delete(save=False)
        fw.delete()
        return JsonResponse({'success': True})
    except Firmware.DoesNotExist:
        return JsonResponse({'error': 'Tidak ditemukan'}, status=404)
    except Exception as e:
        return JsonResponse({'error': str(e)}, status=500)


def firmware_status(request, device_id):
    """Polling status OTA terbaru (fallback jika WebSocket tidak jalan)."""
    try:
        fw = Firmware.objects.filter(
            device_id=device_id
        ).exclude(status='idle').order_by('-triggered_at').first()

        if not fw:
            return JsonResponse({'success': True, 'has_ota': False})

        return JsonResponse({
            'success': True,
            'has_ota': True,
            'firmware_id': fw.id,
            'version': fw.version,
            'status': fw.status,
            'progress': fw.progress,
            'last_error': fw.last_error,
        })
    except Exception as e:
        return JsonResponse({'success': False, 'error': str(e)}, status=500)
