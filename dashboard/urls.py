from django.urls import path
from . import views

urlpatterns = [
    path('login/', views.login_view, name='login'),
    path('register/', views.register_view, name='register'),
    path('logout/', views.logout_view, name='logout'),
    path('', views.halaman_utama, name='halaman_utama'),
    path('lokasi/<int:lokasi_id>/', views.detail_lokasi, name='detail_lokasi'),
    path('download-csv-lokasi/<int:lokasi_id>/', views.download_csv_lokasi, name='download_csv_lokasi'),
    path('api/chart-bulanan/<str:alat_id>/', views.chart_bulanan, name='chart_bulanan'),
    path('api/chart-data/<str:alat_id>/', views.chart_data, name='chart_data'),
    
]