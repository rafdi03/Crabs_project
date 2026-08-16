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