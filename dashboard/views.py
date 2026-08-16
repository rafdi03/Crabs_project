from django.shortcuts import render, redirect, get_object_or_404
from django.contrib.auth import login, authenticate, logout
from django.contrib.auth.forms import UserCreationForm, AuthenticationForm
from django.contrib.auth.decorators import login_required
from django.db.models import Avg
from django.utils import timezone
from datetime import timedelta, datetime
from django.http import HttpResponse, JsonResponse
from .models import Lokasi, Alat, DataSensor, RelayState
from django.db.models.functions import ExtractMonth, ExtractYear, TruncDate, TruncMonth, TruncHour
from channels.layers import get_channel_layer
from asgiref.sync import async_to_sync
import paho.mqtt.publish as publish
import csv
import json
import os
from collections import defaultdict

MQTT_BROKER = os.environ.get('MQTT_BROKER', 'broker.emqx.io')
MQTT_PORT = int(os.environ.get('MQTT_PORT', 1883))

# 1. Login
def login_view(request):
    if request.method == 'POST':
        form = AuthenticationForm(data=request.POST)
        if form.is_valid():
            user = form.get_user()
            login(request, user)
            return redirect('halaman_utama')
    else:
        form = AuthenticationForm()
    return render(request, 'dashboard/login.html', {'form': form})

# 2. Registrasi
def register_view(request):
    if request.method == 'POST':
        form = UserCreationForm(request.POST)
        if form.is_valid():
            user = form.save()
            login(request, user)
            return redirect('halaman_utama')
    else:
        form = UserCreationForm()
    return render(request, 'dashboard/register.html', {'form': form})

# 3. Logout
def logout_view(request):
    logout(request)
    return redirect('login')

# 4. Halaman Utama
@login_required(login_url='login')
def halaman_utama(request):
    semua_lokasi = Lokasi.objects.all()
    return render(request, 'dashboard/home.html', {'daftar_lokasi': semua_lokasi})

# 5. Detail Lokasi (Dengan Inisialisasi RelayState per Alat)
@login_required(login_url='login')
def detail_lokasi(request, lokasi_id):
    lokasi_terpilih = get_object_or_404(Lokasi, id=lokasi_id)
    daftar_alat = Alat.objects.filter(lokasi=lokasi_terpilih, status_aktif=True)
    alat_data_list = []
    for alat in daftar_alat:
        semua_history = alat.data_sensor.all()[:30]
        history_grafik = alat.data_sensor.all()[:20][::-1]
        
        waktu = [d.timestamp.strftime('%H:%M') for d in history_grafik]
        do_data = [d.do_level for d in history_grafik]
        tds_data = [d.tds_level for d in history_grafik]
        suhu_air_data = [d.suhu_air for d in history_grafik]
        suhu_lingkungan_data = [d.suhu_lingkungan for d in history_grafik]

        chart_data = json.dumps({
            'waktu': waktu,
            'do': do_data,
            'tds': tds_data,
            'suhu_air': suhu_air_data,
            'suhu_lingkungan': suhu_lingkungan_data
        })

        # Pastikan status relay tersedia
        relay_state, _ = RelayState.objects.get_or_create(alat=alat)

        alat_data_list.append({
            'alat': alat,
            'sensor_terbaru': alat.data_sensor.first(),
            'chart_data': chart_data,
            'tabel_riwayat': semua_history,
            'relay_state': relay_state,
        })

    context = {
        'lokasi': lokasi_terpilih,
        'alat_data_list': alat_data_list
    }
    return render(request, 'dashboard/detail_lokasi.html', context)

# 6. API Kontrol Relay (Web -> MQTT & WebSockets)
@login_required(login_url='login')
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
        except Exception as e:
            print(f"⚠️ Gagal publish MQTT: {e}")

        # Broadcast update status relay ke seluruh klien WebSocket
        channel_layer = get_channel_layer()
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
@login_required(login_url='login')
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

# 8. Download CSV
@login_required(login_url='login')
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
                data.timestamp.strftime('%Y-%m-%d %H:%M:%S'),
                data.do_level,
                data.tds_level,
                data.jsn_distance,
                data.suhu_air,
                data.suhu_lingkungan,
                data.kelembaban_udara
            ])
    return response

# 9. API chart-bulanan
@login_required(login_url='login')
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
            'suhu_air': [round(d['avg_suhu_air'], 2) for d in data_harian],
            'suhu_lingkungan': [round(d['avg_suhu_lingkungan'], 2) for d in data_harian],
            'do': [round(d['avg_do'], 2) for d in data_harian],
            'tds': [round(d['avg_tds'], 2) for d in data_harian],
            'jsn': [round(d['avg_jsn'], 2) for d in data_harian],
        }
    else:
        data = DataSensor.objects.filter(
            alat=alat,
            timestamp__gte=start_date,
            timestamp__lte=end_date
        ).order_by('timestamp')
        result = {
            'labels': [d.timestamp.strftime('%d %H:%M') for d in data],
            'suhu_air': [d.suhu_air for d in data],
            'suhu_lingkungan': [d.suhu_lingkungan for d in data],
            'do': [d.do_level for d in data],
            'tds': [d.tds_level for d in data],
            'jsn': [d.jsn_distance for d in data],
        }
    return JsonResponse(result)

# 10. API chart-data (Support 10 Menit & Anti-Lag)
@login_required
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
        
        labels = [d.timestamp.strftime('%H:%M') for d in data]
        result = {
            'labels': labels,
            'suhu_air': [d.suhu_air for d in data],
            'suhu_lingkungan': [d.suhu_lingkungan for d in data],
            'do': [d.do_level for d in data],
            'tds': [d.tds_level for d in data],
            'jsn': [d.jsn_distance for d in data],
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
            minute_bucket = (d.timestamp.minute // interval_menit) * interval_menit
            t = d.timestamp.replace(minute=minute_bucket, second=0, microsecond=0)
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