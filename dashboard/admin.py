from django.contrib import admin
from .models import Lokasi, Alat, DataSensor, RelayState, Firmware

admin.site.register(Lokasi)
admin.site.register(Alat)
admin.site.register(DataSensor)
admin.site.register(RelayState)

@admin.register(Firmware)
class FirmwareAdmin(admin.ModelAdmin):
    list_display = ['device_id', 'version', 'file_size_kb',
                    'status', 'progress', 'uploaded_at', 'triggered_at']
    list_filter = ['status', 'device_id']
    search_fields = ['device_id', 'version']
    readonly_fields = ['status', 'progress', 'last_error',
                       'triggered_at', 'completed_at', 'uploaded_at']