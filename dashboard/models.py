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
    id_alat = models.CharField(max_length=50, unique=True) # ID unik, misal MAC Address atau "ESP32-TAMBAK-01"
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
    device_timestamp = models.DateTimeField(null=True, blank=True)

    class Meta:
        ordering = ['-timestamp']
        verbose_name_plural = "Data Sensor"

    def __str__(self):
        return f"Data {self.alat.id_alat} pada {self.timestamp.strftime('%Y-%m-%d %H:%M')}"