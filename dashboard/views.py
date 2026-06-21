from django.shortcuts import render, redirect, get_object_or_404
from django.contrib.auth import login, authenticate, logout
from django.contrib.auth.forms import UserCreationForm, AuthenticationForm
from django.contrib.auth.decorators import login_required
from django.http import HttpResponse
from .models import Lokasi, Alat
import csv
import json

# 1. Halaman Login
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

# 2. Halaman Registrasi
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

# 3. Fitur Logout
def logout_view(request):
    logout(request)
    return redirect('login')

# 4. Halaman Utama (Dashboard / Pilih Lokasi)
@login_required(login_url='login')
def halaman_utama(request):
    semua_lokasi = Lokasi.objects.all()
    context = {
        'daftar_lokasi': semua_lokasi
    }
    return render(request, 'dashboard/home.html', context)

# 5. Halaman Detail Lokasi (Daftar Alat & Grafik)
@login_required(login_url='login')
def detail_lokasi(request, lokasi_id):
    lokasi_terpilih = get_object_or_404(Lokasi, id=lokasi_id)
    daftar_alat = Alat.objects.filter(lokasi=lokasi_terpilih, status_aktif=True)
    
    # Membungkus data alat beserta history-nya untuk grafik
    alat_data_list = []
    for alat in daftar_alat:
        # Ambil 20 data terakhir, lalu balik urutannya ([::-1]) agar yang terlama di kiri grafik
        history_sensor = alat.data_sensor.all()[:20][::-1]
        
        # Siapkan array untuk Chart.js
        waktu = [d.timestamp.strftime('%H:%M') for d in history_sensor]
        do_data = [d.do_level for d in history_sensor]
        tds_data = [d.tds_level for d in history_sensor]
        suhu_air_data = [d.suhu_air for d in history_sensor]
        suhu_lingkungan_data = [d.suhu_lingkungan for d in history_sensor]

        chart_data = json.dumps({
            'waktu': waktu,
            'do': do_data,
            'tds': tds_data,
            'suhu_air': suhu_air_data,
            'suhu_lingkungan': suhu_lingkungan_data
        })

        alat_data_list.append({
            'alat': alat,
            'sensor_terbaru': alat.data_sensor.first(), # Ambil 1 data teratas untuk kartu real-time
            'chart_data': chart_data
        })
    
    context = {
        'lokasi': lokasi_terpilih,
        'alat_data_list': alat_data_list
    }
    return render(request, 'dashboard/detail_lokasi.html', context)


@login_required(login_url='login')
def detail_lokasi(request, lokasi_id):
    lokasi_terpilih = get_object_or_404(Lokasi, id=lokasi_id)
    daftar_alat = Alat.objects.filter(lokasi=lokasi_terpilih, status_aktif=True)
    
    alat_data_list = []
    for alat in daftar_alat:
        # Mengambil semua data untuk tabel (dibatasi `30` terakhir agar web tidak berat)
        semua_history = alat.data_sensor.all()[:30] 
        
        # Mengambil 20 data untuk grafik, dibalik urutannya
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
            'tabel_riwayat': semua_history # Menambahkan data riwayat ke HTML
        })
    
    context = {
        'lokasi': lokasi_terpilih,
        'alat_data_list': alat_data_list
    }
    return render(request, 'dashboard/detail_lokasi.html', context)


# 6. Fungsi Khusus untuk Download CSV (Satu Lokasi Penuh)
@login_required(login_url='login')
def download_csv_lokasi(request, lokasi_id):
    lokasi = get_object_or_404(Lokasi, id=lokasi_id)
    
    response = HttpResponse(content_type='text/csv')
    response['Content-Disposition'] = f'attachment; filename="Data_Area_{lokasi.nama_daerah}.csv"'
    
    writer = csv.writer(response)
    # Tambahkan kolom 'Nama Kolam/Alat' agar data tidak tertukar di Excel
    writer.writerow(['Nama Kolam/Alat', 'Timestamp', 'DO (mg/L)', 'TDS (ppm)', 'Jarak JSN (cm)', 'Suhu Air (C)', 'Suhu Lingkungan (C)'])
    
    # Ambil semua alat di lokasi tersebut, lalu tulis datanya ke 1 file CSV
    daftar_alat = Alat.objects.filter(lokasi=lokasi, status_aktif=True)
    for alat in daftar_alat:
        semua_data = alat.data_sensor.all()
        for data in semua_data:
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