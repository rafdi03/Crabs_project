# Monitoring Tambak - Crabs Aquasense

Project ini adalah sistem monitoring tambak real-time berbasis Django, WebSocket, MQTT, dan sensor ESP32. Project ini digunakan untuk menampilkan data sensor seperti suhu air, suhu lingkungan, DO, TDS, dan JSN secara real-time di dashboard web.

## 1. Apa yang bisa dilakukan project ini?

Project ini memungkinkan Anda untuk:
- Menampilkan data sensor dari ESP32 ke dashboard web
- Menggunakan real-time update tanpa refresh halaman
- Menyimpan data sensor ke database SQLite
- Menampilkan data historis per alat dan lokasi
- Mengelola lokasi, alat, dan data melalui admin Django

## 2. Teknologi yang dipakai

- Python
- Django
- Django Channels + WebSocket
- MQTT (broker public seperti broker.emqx.io)
- ESP32 / ESP-IDF / Arduino
- SQLite (default database)

## 3. Persyaratan sistem

Sebelum memulai, pastikan perangkat Anda sudah punya:
- Python 3.10+ terinstall
- Git terinstall
- Akses internet untuk MQTT dan jika ingin dipakai lewat ngrok
- ESP32 yang sudah terpasang sensor

## 4. Setup awal di Windows

### 4.1 Clone repository

```powershell
git clone <url-repository>
cd monitoring_tambak
```

### 4.2 Buat virtual environment

```powershell
py -m venv .env
.\.env\Scripts\Activate.ps1
```

Jika PowerShell memblokir aktifasi, jalankan dulu:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy RemoteSigned
```

### 4.3 Install dependency yang dibutuhkan

```powershell
pip install django channels channels-redis daphne paho-mqtt certifi
```

Jika Anda ingin menggunakan simulator untuk testing sebelum ESP32 aktif, juga bisa install:

```powershell
pip install paho-mqtt
```

## 5. Jalankan project

### 5.1 Jalankan migrasi database

```powershell
python manage.py migrate
```

### 5.2 Buat akun admin

```powershell
python manage.py createsuperuser
```

Masukkan username, email, dan password sesuai instruksi.

### 5.3 Jalankan server Django

Buka terminal pertama:

```powershell
.\.env\Scripts\Activate.ps1
python manage.py runserver
```

Akses aplikasi di browser:
- http://127.0.0.1:8000/

### 5.4 Jalankan listener MQTT

Buka terminal kedua:

```powershell
.\.env\Scripts\Activate.ps1
python manage.py mqtt_listener
```

Listener ini akan membaca data dari MQTT dan mengirimkan ke dashboard secara real-time.

### 5.5 (Opsional) Jalankan simulator untuk testing

Kalau Anda belum punya ESP32 atau ingin test dulu, jalankan:

```powershell
py simulator.py
```

Simulator ini akan mengirim data dummy ke sistem sehingga Anda bisa melihat update dashboard.

## 6. Akses dashboard dan admin

### 6.1 Dashboard utama

Setelah server berjalan, buka:

```text
http://127.0.0.1:8000/
```

### 6.2 Halaman admin Django

Buka:

```text
http://127.0.0.1:8000/admin/
```

Login dengan akun superuser yang sudah dibuat tadi.

## 7. Cara mengelola data melalui admin

Setelah login ke admin, Anda dapat mengatur tiga hal penting:

### 7.1 Tambah Lokasi

Masuk ke menu Lokasi, lalu klik Add.

Contoh:
- Nama daerah: Tajurhalang
- Keterangan: Area kolam budidaya

### 7.2 Tambah Alat / ESP32

Masuk ke menu Alat, lalu klik Add.

Kolom yang perlu diisi:
- ID Alat: isi dengan ID yang sama dengan ESP32 Anda, misalnya `ESP32-001`
- Lokasi: pilih lokasi yang sudah dibuat sebelumnya
- Nama Kolam: misalnya `Kolam A`
- Status Aktif: centang jika alat aktif

Penting: ID alat harus sama dengan topic MQTT yang dikirim ESP32.

### 7.3 Lihat data sensor

Data sensor akan muncul otomatis setelah ESP32 mengirimkan data ke MQTT.

## 8. Cara menyiapkan ESP32

### 8.1 Jika memakai kode ESP-IDF

Buka folder project ESP32 Anda, lalu edit bagian konfigurasi Wi-Fi dan MQTT di file utama.

Bagian yang perlu diubah:

```c
#define WIFI_SSID      "NamaWiFi"
#define WIFI_PASS      "PasswordWiFi"
#define MQTT_BROKER    "mqtt://broker.emqx.io:1883"
#define DEVICE_ID      "ESP32-001"
#define MQTT_TOPIC     "tambak/ESP32-001/sensor"
```

Aturan penting:
- `DEVICE_ID` harus unik untuk setiap ESP32
- `MQTT_TOPIC` harus mengikuti format:

```text
tambak/<ID_ALAT>/sensor
```

Contoh:
```c
#define DEVICE_ID      "ESP32-002"
#define MQTT_TOPIC     "tambak/ESP32-002/sensor"
```

### 8.2 Build dan upload ke ESP32

Jika memakai ESP-IDF, jalankan:

```powershell
idf.py build
idf.py flash
idf.py monitor
```

### 8.3 Cek apakah ESP32 sudah mengirim data

Setelah terhubung ke Wi-Fi dan MQTT, ESP32 akan mengirim JSON seperti ini:

```json
{"tds":0.0,"jsn":0.0,"nitrat":0.0,"do":0.0,"suhu_air":28.5,"suhu_lingkungan":31.2,"timestamp":"2026-08-06T10:00:00Z"}
```

Jika log di monitor menampilkan bahwa data dikirim, maka sistem siap menerima.

## 9. Cara agar update otomatis muncul

Project ini sudah mendukung update real-time melalui WebSocket.

Agar update otomatis bisa muncul:
1. Jalankan server Django
2. Jalankan listener MQTT
3. Buka dashboard di browser
4. Pastikan ESP32 mengirim data ke broker MQTT

Jika data tidak muncul:
- cek apakah `mqtt_listener` sedang berjalan
- cek apakah topic MQTT cocok dengan `DEVICE_ID`
- cek apakah alat sudah dibuat di admin dengan status aktif
- cek apakah WebSocket berhasil terhubung di browser console

## 10. Jika ingin diakses dari luar jaringan (ngrok)

Kalau ingin membuka project dari HP atau komputer lain, Anda bisa pakai ngrok.

Buka terminal baru lalu jalankan:

```powershell
ngrok http 8000
```

Lalu gunakan URL yang diberikan ngrok ke browser.

Catatan: untuk WebSocket real-time, pastikan URL yang dipakai mendukung `wss://`.

## 11. Troubleshooting umum

### WebSocket tidak terhubung
- Pastikan Django server sedang berjalan
- Pastikan browser membuka halaman yang sama dengan server
- Cek console browser untuk melihat pesan error

### Data tidak masuk ke dashboard
- Pastikan `mqtt_listener` sedang berjalan
- Pastikan topic MQTT sesuai format `tambak/<id>/sensor`
- Pastikan alat sudah dibuat di admin dan status aktif

### ESP32 tidak terkoneksi
- Periksa SSID dan password Wi-Fi
- Periksa broker MQTT yang dipakai
- Periksa kabel sensor dan pin GPIO yang digunakan

## 12. Ringkasan alur kerja paling sederhana

1. Jalankan server Django
2. Jalankan MQTT listener
3. Buat lokasi dan alat di admin
4. Konfigurasi ESP32 dengan Wi-Fi, broker MQTT, dan topic yang benar
5. Flash ESP32
6. Lihat data masuk di dashboard secara real-time

Selamat mencoba!
