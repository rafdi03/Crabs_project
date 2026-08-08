from django.contrib import admin
from .models import Lokasi, Alat, DataSensor, RelayState

# Daftarkan model agar muncul di dashboard admin Django
admin.site.register(Lokasi)
admin.site.register(Alat)
admin.site.register(DataSensor)
admin.site.register(RelayState)