from django.db import models

# 1. Tabel Lokasi Tambak
class Lokasi(models.Model):
    nama_daerah = models.CharField(max_length=100, unique=True) # Contoh: "Tajurhalang, Bogor"
    keterangan = models.TextField(blank=True, null=True)

    class Meta:
        verbose_name_plural = "Daftar Lokasi"

    def __str__(self):
        return self.nama_daerah

# 2. Tabel Alat / Hardware (ESP32)
class Alat(models.Model):
    id_alat = models.CharField(max_length=50, unique=True) # ID unik, misal "ESP32-001"
    lokasi = models.ForeignKey(Lokasi, on_delete=models.CASCADE, related_name='daftar_alat')
    nama_kolam = models.CharField(max_length=100, blank=True) # Contoh: "Kolam Udang A1"
    status_aktif = models.BooleanField(default=True)

    class Meta:
        verbose_name_plural = "Daftar Alat"

    def __str__(self):
        return f"{self.id_alat} - {self.nama_kolam}"

# 3. Tabel Data Sensor
class DataSensor(models.Model):
    alat = models.ForeignKey(Alat, on_delete=models.CASCADE, related_name='data_sensor')
    timestamp = models.DateTimeField(auto_now_add=True, db_index=True) 
    
    # Data dari Sensor Air
    do_level = models.FloatField(verbose_name="Dissolved Oxygen (mg/L)")
    tds_level = models.FloatField(verbose_name="Total Dissolved Solids (ppm)")
    jsn_distance = models.FloatField(verbose_name="Ketinggian Air JSN (cm)")
    suhu_air = models.FloatField(verbose_name="Suhu Air (°C)")
    suhu_lingkungan = models.FloatField(verbose_name="Suhu Lingkungan (°C)")
    kelembaban_udara = models.FloatField(default=0.0, verbose_name="Kelembaban Udara (%)")
    device_timestamp = models.DateTimeField(null=True, blank=True)

    class Meta:
        ordering = ['-timestamp']
        verbose_name_plural = "Data Sensor"

    def __str__(self):
        return f"Data {self.alat.id_alat} pada {self.timestamp.strftime('%Y-%m-%d %H:%M')}"

# 4. Tabel Status Kontrol 5-Channel Relay (FreeRTOS)
class RelayState(models.Model):
    alat = models.OneToOneField(Alat, on_delete=models.CASCADE, related_name='relay_state')
    relay1 = models.BooleanField(default=False, verbose_name="Relay 1 (D25 - Pompa 1)")
    relay2 = models.BooleanField(default=False, verbose_name="Relay 2 (D16 - Pompa 2)")
    relay3 = models.BooleanField(default=False, verbose_name="Relay 3 (D17 - Heater)")
    relay4 = models.BooleanField(default=False, verbose_name="Relay 4 (D13 - Feeder)")
    relay5 = models.BooleanField(default=False, verbose_name="Relay 5 (D14 - Solenoid Valve)")
    updated_at = models.DateTimeField(auto_now=True)

    class Meta:
        verbose_name_plural = "Status Relay"

    def __str__(self):
        return f"Relay {self.alat.id_alat} [R1:{self.relay1}, R2:{self.relay2}, R3:{self.relay3}, R4:{self.relay4}, R5:{self.relay5}]"
    
# 5. Tabel Firmware OTA (untuk Update via MQTT + URL)
class Firmware(models.Model):
    """Firmware binary untuk OTA update."""
    
    def firmware_upload_path(instance, filename):
        return f'firmware/{instance.device_id}/{filename}'
    
    device_id = models.CharField(
        max_length=50, db_index=True,
        help_text="Target device, harus match Alat.id_alat (mis: ESP32-001)"
    )
    version = models.CharField(
        max_length=32,
        help_text="Versi firmware, mis: v1.2.0"
    )
    file = models.FileField(
        upload_to=firmware_upload_path,
        help_text="File .bin hasil build ESP-IDF"
    )
    notes = models.TextField(
        blank=True, null=True,
        help_text="Catatan rilis (opsional)"
    )
    
    # Tracking OTA
    STATUS_CHOICES = [
        ('idle', 'Idle - Siap dikirim'),
        ('queued', 'Dikirim ke ESP32...'),
        ('started', 'ESP32 Memulai OTA'),
        ('downloading', 'Mengunduh Firmware'),
        ('verifying', 'Verifikasi Firmware'),
        ('success', 'Berhasil! Menunggu Reboot'),
        ('error', 'Gagal'),
    ]
    status = models.CharField(max_length=16, choices=STATUS_CHOICES, default='idle')
    progress = models.IntegerField(default=0)
    last_error = models.CharField(max_length=128, blank=True, null=True)
    
    triggered_at = models.DateTimeField(null=True, blank=True)
    completed_at = models.DateTimeField(null=True, blank=True)
    uploaded_at = models.DateTimeField(auto_now_add=True)
    
    class Meta:
        ordering = ['-uploaded_at']
        verbose_name_plural = "Firmware OTA"
    
    def __str__(self):
        return f"{self.device_id} - {self.version}"
    
    def file_size_kb(self):
        try:
            return round(self.file.size / 1024)
        except Exception:
            return 0