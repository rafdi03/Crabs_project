# Monitoring Tambak ESP-IDF Base

Proyek ini adalah kerangka dasar ESP-IDF untuk monitoring tambak.

## Struktur proyek
- `CMakeLists.txt` - file proyek utama ESP-IDF.
- `main/CMakeLists.txt` - konfigurasi komponen utama.
- `main/main.c` - titik masuk aplikasi dan logika dasar.

## Langkah penggunaan
1. Buka terminal PowerShell.
2. Masuk ke folder proyek:
   ```powershell
   cd "c:\Users\Rafdi\OneDrive\Desktop\Monitoring_tambak"
   ```
3. Aktifkan lingkungan ESP-IDF dengan skrip `export.bat` dari instalasi ESP-IDF Anda.
   - Jika Anda menggunakan shortcut `IDF_v6.0_Powershell.lnk`, buka dari sana.
4. Set target board (misalnya `esp32`):
   ```powershell
   idf.py set-target esp32
   ```
5. Build proyek:
   ```powershell
   idf.py build
   ```
6. Flash ke perangkat:
   ```powershell
   idf.py -p COM3 flash
   ```
7. Monitor output serial:
   ```powershell
   idf.py -p COM3 monitor
   ```

## Lokasi yang bisa diedit
- `main/main.c`:
  - `read_sensor_data()` untuk menangkap sensor.
  - `process_measurement()` untuk logika pemrosesan.
  - `update_actuator()` untuk mengendalikan aktuator.
  - `send_data()` untuk mengirim data ke server atau logger.

Sesuaikan pin dan metode komunikasi dengan perangkat Anda.
