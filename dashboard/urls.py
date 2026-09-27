from django.urls import path
from . import views

urlpatterns = [
    # Rute lama tetap menuju dashboard agar bookmark lama tidak rusak.
    path('login/', views.halaman_utama, name='login'),
    path('register/', views.halaman_utama, name='register'),
    path('logout/', views.halaman_utama, name='logout'),
    path('', views.halaman_utama, name='halaman_utama'),
    path('lokasi/<int:lokasi_id>/', views.detail_lokasi, name='detail_lokasi'),
    path('download-csv-lokasi/<int:lokasi_id>/', views.download_csv_lokasi, name='download_csv_lokasi'),
    path('api/chart-bulanan/<str:alat_id>/', views.chart_bulanan, name='chart_bulanan'),
    path('api/chart-data/<str:alat_id>/', views.chart_data, name='chart_data'),
    path('api/relay-control/', views.relay_control, name='relay_control'),
    path('api/relay-status/<str:alat_id>/', views.get_relay_status, name='get_relay_status'),
    path('api/sensor-terbaru/<str:alat_id>/', views.get_sensor_terbaru, name='get_sensor_terbaru'),

    # ===== OTA FIRMWARE =====
    path('api/firmware/upload/', views.firmware_upload, name='firmware_upload'),
    path('api/firmware/list/<str:device_id>/', views.firmware_list, name='firmware_list'),
    path('api/firmware/trigger/<int:firmware_id>/', views.firmware_trigger_ota, name='firmware_trigger_ota'),
    path('api/firmware/delete/<int:firmware_id>/', views.firmware_delete, name='firmware_delete'),
    path('api/firmware/status/<str:device_id>/', views.firmware_status, name='firmware_status'),
]
