# Crabs Aquasense - Smart Pond Monitoring System

Crabs Aquasense is a real-time aquaculture monitoring and control system designed to monitor pond conditions using ESP32-based sensor nodes, MQTT communication, and a Django web dashboard. The system collects water and environmental parameters such as water temperature, ambient temperature, dissolved oxygen (DO), total dissolved solids (TDS), and water level, then displays them live through a web interface.

This project is intended for aquaculture management, especially for shrimp or crab pond monitoring, where continuous observation of water quality is essential to maintain stable cultivation conditions and prevent operational losses.

## 1. Project Objective

This project aims to provide a practical monitoring solution that allows users to:

- Monitor pond conditions in real time from a web dashboard
- Receive sensor data automatically without refreshing the page
- Store measurement history in a database for analysis and reporting
- Manage multiple pond locations and devices from a centralized system
- Control irrigation or water management equipment through relay output signals
- Detect abnormal conditions early through threshold-based assessment

## 2. What the System Does

The system works as a complete monitoring pipeline:

1. ESP32 devices collect sensor readings from the pond environment.
2. Each device sends measurement data through MQTT to a broker.
3. A Django management command listens to the MQTT topic and stores incoming data into the database.
4. The web application receives updates and pushes them to the frontend in real time using WebSockets.
5. Users can observe pond status, historical graphs, and latest values from the dashboard.
6. Relay statuses can be controlled remotely via the web interface and published back to the device using MQTT.

This makes the project suitable for remote aquaculture monitoring, especially in locations where direct physical inspection is difficult.

## 3. Core Features

- Real-time sensor monitoring dashboard
- Live updates using Django Channels and WebSockets
- MQTT-based device communication
- Sensor history tracking per pond and per device
- Site and device management through Django admin
- Relay control for water management functions
- Automatic status classification based on measurement thresholds
- SQLite database support for local deployment
- Optional simulator for testing without actual hardware

## 4. Technical Stack

- Python
- Django
- Django Channels
- WebSockets
- MQTT protocol
- ESP32 / ESP-IDF / Arduino-based modules
- SQLite database
- JavaScript for frontend interactivity
- MQTT broker such as EMQX

## 5. System Architecture

The application is structured in the following flow:

- Sensor devices: ESP32 sends sensor values to the MQTT broker
- MQTT broker: receives and forwards messages from devices
- Backend listener: Django command reads incoming MQTT messages and inserts them into the database
- Web layer: Django views render dashboard pages and APIs
- Real-time layer: Django Channels pushes updates to browser clients
- Database: stores sensor records, locations, devices, and relay states

In practical terms, the project brings together embedded hardware, message-based communication, backend data processing, and browser-based monitoring in one integrated system.

## 6. Supported Sensor Data

The system processes and stores the following parameters:

- Water temperature
- Ambient temperature
- Dissolved oxygen (DO)
- Total dissolved solids (TDS)
- Water level / depth indicator (JSN)
- Air humidity
- Device timestamp

These values are stored in the database and used to construct dynamic charts and status summaries on the dashboard.

## 7. Hardware and Device Model

Each ESP32 device represents one monitoring unit linked to a specific pond or location. Device configuration includes:

- Unique device ID
- Assigned location
- Pond name
- Active/inactive status
- MQTT topic mapping

The system assumes that the device sends sensor payloads to a topic following the pattern:

```text
tambak/<DEVICE_ID>/sensor
```

Example:

```text
tambak/ESP32-001/sensor
```

## 8. Project Structure

Key directories in this repository include:

- dashboard/: Django application containing models, dashboard logic, MQTT listener, routes, and templates
- core/: main project configuration for Django settings, URLs, and ASGI setup
- esp32/: ESP32 firmware project files and embedded code
- esp32_arduino/: Arduino-based project for device development
- simulator.py: script used to generate test data when hardware is unavailable
- db.sqlite3: local SQLite database file

## 9. Requirements

Before running the project, make sure the following are available:

- Python 3.10 or newer
- Git
- Internet access for MQTT broker connectivity
- ESP32 hardware or a simulator for testing
- Optional: ngrok for exposing the app to external networks

## 10. Windows Setup

### 10.1 Clone the repository

```powershell
git clone <repository-url>
cd Crabs_project
```

### 10.2 Create a virtual environment

```powershell
py -m venv .env
.\.env\Scripts\Activate.ps1
```

If PowerShell blocks activation, run:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy RemoteSigned
```

### 10.3 Install dependencies

```powershell
pip install django channels channels-redis daphne paho-mqtt certifi
```

If you want to use the simulator before connecting physical hardware, the MQTT library is also required:

```powershell
pip install paho-mqtt
```

## 11. Running the Application

### 11.1 Apply database migrations

```powershell
python manage.py migrate
```

### 11.2 Create an admin account

```powershell
python manage.py createsuperuser
```

Follow the prompts to enter a username, email, and password.

### 11.3 Start the Django server

Open a terminal and run:

```powershell
.\.env\Scripts\Activate.ps1
python manage.py runserver
```

Then open the application in your browser:

```text
http://127.0.0.1:8000/
```

### 11.4 Start the MQTT listener

Open a second terminal and run:

```powershell
.\.env\Scripts\Activate.ps1
python manage.py mqtt_listener
```

This command listens for incoming MQTT messages and writes the sensor data into the system database.

### 11.5 Optional: run the simulator

If hardware is not yet available, you can test the system with a simulation script:

```powershell
py simulator.py
```

The simulator publishes dummy data so the dashboard can be verified before real device deployment.

## 12. Accessing the Dashboard and Admin Panel

### Dashboard

```text
http://127.0.0.1:8000/
```

### Django admin

```text
http://127.0.0.1:8000/admin/
```

Log in with the superuser account created earlier.

## 13. Managing Data Through the Admin Panel

After logging into the admin UI, you can configure the main application entities:

### 13.1 Add a Location

Create a pond location such as:

- Name: Tajurhalang
- Description: Aquaculture area / shrimp pond zone

### 13.2 Add a Device / ESP32

Create a device record with:

- Device ID: e.g. `ESP32-001`
- Location: the pond or area assigned to the device
- Pond name: e.g. `Pond A`
- Active status: enabled when the device is in use

Important: the device ID must match the MQTT topic generated by the ESP32 device.

### 13.3 View Live Sensor Data

Once ESP32 sends sensor readings to MQTT, the dashboard automatically updates and displays the latest readings and chart history.

## 14. ESP32 Setup

### 14.1 Configuration for ESP-IDF project

Open the main firmware file and set the Wi-Fi and MQTT configuration values:

```c
#define WIFI_SSID      "YourWiFiName"
#define WIFI_PASS      "YourWiFiPassword"
#define MQTT_BROKER    "mqtt://broker.emqx.io:1883"
#define DEVICE_ID      "ESP32-001"
#define MQTT_TOPIC     "tambak/ESP32-001/sensor"
```

Notes:

- `DEVICE_ID` must be unique for each device
- `MQTT_TOPIC` must follow this format:

```text
tambak/<DEVICE_ID>/sensor
```

Example:

```c
#define DEVICE_ID      "ESP32-002"
#define MQTT_TOPIC     "tambak/ESP32-002/sensor"
```

### 14.2 Build and upload to the device

If using ESP-IDF, run:

```powershell
idf.py build
idf.py flash
idf.py monitor
```

### 14.3 Verify data transmission

After connecting to Wi-Fi and MQTT, the ESP32 publishes data in JSON format similar to:

```json
{"tds":0.0,"jsn":0.0,"nitrat":0.0,"do":0.0,"suhu_air":28.5,"suhu_lingkungan":31.2,"timestamp":"2026-08-06T10:00:00Z"}
```

If the logs show successful publishing, the backend is ready to receive and process the data.

## 15. Real-Time Update Behavior

This project supports real-time updates through WebSockets.

To ensure the live dashboard works properly:

1. Start the Django server
2. Start the MQTT listener
3. Open the dashboard in a browser
4. Make sure the ESP32 publishes data to the MQTT broker

If readings do not appear:

- Verify that `mqtt_listener` is running
- Confirm that the MQTT topic matches the configured device ID
- Ensure the device is created in the admin panel and marked active
- Check browser console logs for WebSocket connection issues

## 16. External Access (ngrok)

If you want to access the project from another device or network, you can use ngrok.

Run:

```powershell
ngrok http 8000
```

Then use the URL provided by ngrok in the browser.

Note: for real-time WebSocket communication, the externally exposed URL should support `wss://`.

## 17. Common Troubleshooting

### WebSocket connection fails

- Confirm the Django server is running
- Ensure the browser is using the same application host
- Check browser developer console for connection errors

### Data does not appear on the dashboard

- Confirm the MQTT listener is active
- Verify the device topic matches the configured ID
- Check whether the data is being published to the broker
- Ensure the corresponding device exists in the admin panel

### ESP32 does not connect

- Check Wi-Fi credentials and signal strength
- Validate MQTT broker URL and port
- Confirm the device is flashed correctly
- Inspect serial monitor logs for connection failures

## 18. Summary

Crabs Aquasense is a practical aquaculture monitoring solution that combines embedded sensors, MQTT communication, real-time dashboards, and relay control. It is designed to help pond operators monitor water quality continuously, identify abnormal conditions earlier, and manage aquaculture environments more efficiently.

This project is especially useful for farmers, aquaculture managers, or developers building a small-scale smart pond monitoring system using affordable ESP32 hardware and a Django-based backend.

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
