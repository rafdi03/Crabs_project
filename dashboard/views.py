from django.shortcuts import render, redirect, get_object_or_404
from django.contrib.auth import login, authenticate, logout
from django.contrib.auth.forms import UserCreationForm, AuthenticationForm
from django.contrib.auth.decorators import login_required
from django.db.models import Avg
from django.utils import timezone
from datetime import timedelta
from django.http import HttpResponse, JsonResponse
from .models import Lokasi, Alat, DataSensor
from django.db.models.functions import ExtractMonth, ExtractYear, TruncDate
import csv
import json

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

# 5. Detail Lokasi (dengan tabel dan grafik real-time)
@login_required(login_url='login')
def detail_lokasi(request, lokasi_id):
    lokasi_terpilih = get_object_or_404(Lokasi, id=lokasi_id)
    daftar_alat = Alat.objects.filter(lokasi=lokasi_terpilih, status_aktif=True)
    
    alat_data_list = []
    for alat in daftar_alat:
        # 30 data terakhir untuk tabel
        semua_history = alat.data_sensor.all()[:30]
        # 20 data terakhir untuk grafik (dibalik agar urutan waktu naik)
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

        alat_data_list.append({
            'alat': alat,
            'sensor_terbaru': alat.data_sensor.first(),
            'chart_data': chart_data,
            'tabel_riwayat': semua_history
        })
    
    context = {
        'lokasi': lokasi_terpilih,
        'alat_data_list': alat_data_list
    }
    return render(request, 'dashboard/detail_lokasi.html', context)

# 6. Download CSV
@login_required(login_url='login')
def download_csv_lokasi(request, lokasi_id):
    lokasi = get_object_or_404(Lokasi, id=lokasi_id)
    response = HttpResponse(content_type='text/csv')
    response['Content-Disposition'] = f'attachment; filename="Data_Area_{lokasi.nama_daerah}.csv"'
    writer = csv.writer(response)
    writer.writerow(['Nama Kolam/Alat', 'Timestamp', 'DO (mg/L)', 'TDS (ppm)', 'Jarak JSN (cm)', 'Suhu Air (C)', 'Suhu Lingkungan (C)'])
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
                data.suhu_lingkungan
            ])
    return response

@login_required(login_url='login')
def chart_bulanan(request, alat_id):
    # 1. Ambil filter dari request (default: bulan & tahun sekarang)
    bulan = request.GET.get('bulan', timezone.now().month)
    tahun = request.GET.get('tahun', timezone.now().year)

    try:
        alat = Alat.objects.get(id_alat=alat_id, status_aktif=True)
    except Alat.DoesNotExist:
        return JsonResponse({'error': 'Alat tidak ditemukan'}, status=404)
    
    # 2. Ambil data mentah (raw) tanpa rata-rata
    data = DataSensor.objects.filter(
        alat=alat,
        timestamp__month=bulan,
        timestamp__year=tahun
    ).order_by('timestamp') # Urutkan dari yang terlama ke terbaru
    
    # 3. Format ke JSON
    result = {
        'labels': [d.timestamp.strftime('%d %H:%M') for d in data],
        'suhu_air': [d.suhu_air for d in data],
        'suhu_lingkungan': [d.suhu_lingkungan for d in data],
        'do': [d.do_level for d in data],
        'tds': [d.tds_level for d in data],
        'jsn': [d.jsn_distance for d in data],
    }
    return JsonResponse(result)