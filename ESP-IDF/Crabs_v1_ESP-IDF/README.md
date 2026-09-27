# ESP32 Real-Time Firmware Framework

Reusable ESP-IDF firmware for ESP32 applications that need periodic real-time work, modular sensor drivers, several communication transports, browser OTA updates, and persistent data logging.

This guide describes the current source tree. The shortest mental model is:

```text
app_main()
  -> RAF_SchedulerInit()
  -> RAF_ApplicationInit()
       -> initialize communication infrastructure
       -> process the s_modules[] descriptors
  -> RAF_SchedulerStart()
```

To add hardware, create a driver, add a module descriptor in `main/application_system/application.c`, list the source in `main/CMakeLists.txt`, and build. The scheduler controls timing; the driver owns hardware state; communication and logging consume the driver's data.

## Contents

- [Features](#features)
- [Prerequisites](#prerequisites)
- [Build, Flash, and Monitor](#build-flash-and-monitor)
- [Startup Flow](#startup-flow)
- [Project Structure](#project-structure)
- [Module System](#module-system)
- [Tutorial: Add an I2C Sensor](#tutorial-add-an-i2c-sensor)
- [SPI, UART, and GPIO Modules](#spi-uart-and-gpio-modules)
- [Scheduler](#scheduler)
- [Communication Hub](#communication-hub)
- [Wi-Fi, MQTT, Modbus TCP, and OTA](#wi-fi-mqtt-modbus-tcp-and-ota)
- [Data Logger](#data-logger)
- [Built-in Drivers and Pins](#built-in-drivers-and-pins)
- [Configuration](#configuration)
- [Flash Layout](#flash-layout)
- [Testing](#testing)
- [Troubleshooting](#troubleshooting)
- [Source Reference](#source-reference)
- [License](#license)

## Features

| Area | Current implementation |
|---|---|
| Application lifecycle | `app_main()`, scheduler initialization, module registration, scheduler start |
| Scheduler | Up to 16 registered tasks with period, phase, deadline, priority, core affinity, enable/disable control, and metrics |
| Sensors | MPU6050 over I2C with acceleration, gyroscope, temperature, and raw values |
| Display | HD44780-compatible LCD through a PCF8574 I2C expander |
| Communications | UART, Modbus RTU/TCP, LoRa, Wi-Fi HTTP, MQTT, CAN/TWAI, BLE placeholder, and ESP-NOW modules |
| Routing | One Com Hub for binary requests and JSON requests |
| IPC | FreeRTOS no-split ring buffer with task/ISR APIs and statistics |
| Integrity | CRC-16 communication validation and CRC-32 log validation utilities |
| OTA | Browser upload at `/update` after Wi-Fi obtains an IP address |
| Logging | Ping-pong buffers, background storage writer, boot recovery, and CSV schema export |
| Examples | I2C, SPI, UART parser, and GPIO ISR templates |

## Prerequisites

- ESP-IDF 5.x or a compatible version.
- Python 3.8 or newer.
- CMake 3.22 or newer and Ninja.
- ESP32 board and USB cable.

On Windows, run from an ESP-IDF PowerShell or Command Prompt:

```powershell
idf.py --version
python --version
echo $env:IDF_PATH
```

## Build, Flash, and Monitor

Run commands from this repository root, where `CMakeLists.txt` is located:

```powershell
idf.py set-target esp32
idf.py build
idf.py -p COM3 flash
idf.py -p COM3 monitor
```

Replace `COM3` with the board's serial port. Flash and monitor in one command:

```powershell
idf.py -p COM3 flash monitor
```

The generated application is under `build/`. Do not edit that directory. The root CMake file currently names the project `hello_world`; this is only the ESP-IDF project name.

### First boot

1. Change Wi-Fi defaults in `main/include/main.h`.
2. Check the pin map in this README.
3. Build, flash, and open the serial monitor.
4. Look for module initialization messages.
5. If Wi-Fi connects, open `http://<ESP32_IP>/update`.

## Startup Flow

`main/application_system/main.c` owns `app_main()`:

1. `RAF_SchedulerInit()` creates the task registry and configures the 3-second task watchdog.
2. `RAF_ApplicationInit()` creates the communication ring buffer, starts the Com Hub, registers IoT handlers, starts Wi-Fi, and processes `s_modules[]`.
3. Each module is initialized, its optional ISR is installed, and its optional periodic task is registered.
4. `RAF_SchedulerStart()` starts the registered tasks.

For each descriptor, initialization happens before ISR installation and task registration. A module with `.required = true` can stop startup when initialization fails. An optional module with `.required = false` is skipped and the rest of the application continues.

## Project Structure

```text
ESP32-0.1v/
├── CMakeLists.txt                 # ESP-IDF project entry point
├── partitions.csv                 # Custom 4 MB partition table
├── sdkconfig.defaults             # Default ESP-IDF settings
├── pytest_hello_world.py          # pytest-embedded tests
├── examples/                      # Copyable module templates
└── main/
    ├── CMakeLists.txt             # Sources and ESP-IDF dependencies
    ├── include/                   # Public headers and board defaults
    ├── application_system/
    │   ├── main.c                 # app_main()
    │   ├── application.c          # s_modules[] and application startup
    │   ├── Scheduler.c            # Descriptor-based FreeRTOS scheduler
    │   └── task.c                 # Reserved job callback stubs
    ├── input/                     # Sensor, display, and IoT response code
    ├── communication/             # Com Hub, transports, Wi-Fi, and OTA
    └── data_logger/               # Record API, log engine, storage backend
```

Every new `.c` file must be added to the `SRCS` list in `main/CMakeLists.txt`. Public headers normally belong in `main/include/`.

## Module System

The main extension point is `RAF_AppModule_t` in `main/include/app_module.h`:

```c
typedef struct {
    const char *name;
    esp_err_t (*init)(void);
    void (*deinit)(void);
    RAF_AppIsrConfig_t isr;
    RAF_AppTaskConfig_t task;
    bool required;
} RAF_AppModule_t;
```

A descriptor can own initialization, a GPIO interrupt, and a periodic task. The task fields are:

| Field | Meaning |
|---|---|
| `name` | Scheduler name; maximum 15 useful characters |
| `period_ms` | Callback period; must be greater than zero |
| `phase_ms` | Initial delay before the first callback |
| `deadline_ms` | Optional execution deadline; zero disables checking |
| `priority` | `RAF_RT_PRIO_BACKGROUND` through `RAF_RT_PRIO_REALTIME` |
| `core` | `RAF_RT_CORE_0`, `RAF_RT_CORE_1`, or `RAF_RT_CORE_ANY` |
| `stack_size` | Zero for default, otherwise at least 2048 bytes |
| `callback` | `void callback(void *arg)` function |
| `arg` | Context passed to the callback |
| `enabled_on_boot` | Initial enabled state |

### Minimal periodic module

```c
#include "esp_err.h"

static bool s_ready;

esp_err_t RAF_TemperatureInit(void) {
    // Configure and verify the sensor here.
    s_ready = true;
    return ESP_OK;
}

void RAF_TemperatureJob(void *arg) {
    (void)arg;
    if (!s_ready) return;
    // Read and publish/store the sensor value here.
}
```

Register it inside `s_modules[]` in `main/application_system/application.c`:

```c
{
    .name = "TEMPERATURE",
    .init = RAF_TemperatureInit,
    .task = {
        .name = "TempPoll",
        .period_ms = 100,
        .phase_ms = 0,
        .deadline_ms = 10,
        .priority = RAF_RT_PRIO_LOW,
        .core = RAF_RT_CORE_0,
        .stack_size = 3072,
        .callback = RAF_TemperatureJob,
        .arg = NULL,
        .enabled_on_boot = true,
    },
    .required = false,
},
```

The current application has four descriptors: `COM_Dispatch`, `SYS_Diag`, `IMU_MPU6050`, and `LCD`. A `_Static_assert` checks that the descriptor count matches `RAF_TASK_IDX_COUNT`; update the enum only when application code needs a module's task ID through `RAF_TaskRegistryGetId()`.

### Module rules

- Keep a callback shorter than its period. The scheduler records overruns but cannot fix blocking code.
- Do not perform long flash, network, or serial operations in a high-priority callback unless the operation is non-blocking.
- ISR handlers must be short, non-blocking, and declared with `IRAM_ATTR` when required by the ESP-IDF driver.
- Use an ISR-safe queue or ring buffer to hand work from an ISR to a task.
- Use `.required = false` for optional hardware that should not prevent the firmware from booting.

## Tutorial: Add an I2C Sensor

This is the complete workflow for a new periodic I2C sensor.

### 1. Add board pins

Add a preset to `main/include/main.h` or `bsp_pins.h`:

```c
#define I2C_PINS_TEMP ((bsp_i2c_pins_t){ .sda = 18, .scl = 19 })
```

Do not reuse pins already assigned to another active bus unless the devices intentionally share that bus.

### 2. Create the public API

Create `main/include/temperature.h`:

```c
#pragma once

#include "esp_err.h"
#include "bsp_pins.h"

typedef struct {
    float temperature_c;
    float humidity_percent;
} temperature_data_t;

typedef struct {
    bsp_i2c_pins_t pins;
    uint8_t address;
} temperature_config_t;

esp_err_t temperature_init(const temperature_config_t *config);
void temperature_update(void);
const temperature_data_t *temperature_get_data(void);
```

### 3. Implement the driver

Create `main/input/temperature.c`. Keep scheduling and communication out of the driver:

```c
#include "temperature.h"

static temperature_data_t s_data;
static bool s_ready;

esp_err_t temperature_init(const temperature_config_t *config) {
    if (config == NULL) return ESP_ERR_INVALID_ARG;
    // Create the I2C bus, add the device, and verify its identity.
    s_ready = true;
    return ESP_OK;
}

void temperature_update(void) {
    if (!s_ready) return;
    // Read registers and convert raw values into engineering units.
}

const temperature_data_t *temperature_get_data(void) {
    return &s_data;
}
```

Replace the marked sections with the ESP-IDF I2C master API. Return the real error if bus or device setup fails. Do not add `vTaskDelay()` to the driver; the module descriptor supplies timing.

### 4. Connect it to the application

Add wrappers in `application.c`:

```c
static esp_err_t RAF_ApplicationInitTemperature(void) {
    const temperature_config_t config = {
        .pins = I2C_PINS_TEMP,
        .address = 0x40,
    };
    return temperature_init(&config);
}

static void RAF_ApplicationJobTemperature(void *arg) {
    (void)arg;
    temperature_update();
}
```

Then use those functions in a descriptor like the one above. The driver is now initialized once and polled every 100 ms.

### 5. Register and build

Add `"input/temperature.c"` to `main/CMakeLists.txt`, then run:

```powershell
idf.py reconfigure
idf.py build
idf.py -p COM3 flash monitor
```

Look for `[TEMPERATURE] init OK`. If it is optional and disconnected, the expected behavior is a warning and continued application startup.

### 6. Use the data

Read data through the getter rather than accessing private driver state:

```c
const temperature_data_t *data = temperature_get_data();
ESP_LOGI("APP", "Temperature: %.2f C", data->temperature_c);
```

To expose it over the network, add it to the registered JSON or binary command handler. To persist it, copy it into `device_datalog_t` before calling `data_logger_write_record()`.

## SPI, UART, and GPIO Modules

The `examples/` templates are intended to be copied into `main/input/`, renamed, registered in `s_modules[]`, and added to CMake:

- `module_i2c_simple.template.c`: periodic I2C device.
- `module_spi_simple.template.c`: SPI bus/device initialization and periodic transaction.
- `module_uart_parser.template.c`: UART receive buffering and frame parsing.
- `module_gpio_isr.template.c`: GPIO edge ISR with task handoff.

For GPIO interrupts, configure the `.isr` field in the descriptor. The ISR should capture the event only; parse or act on it in the periodic task. For UART, validate frame length and checksum before pushing data to the Com Hub.

## Scheduler

`Scheduler.c` creates one FreeRTOS task per registered descriptor, up to `RAF_RT_MAX_TASKS` (16). Each task uses `vTaskDelayUntil()` and the ESP task watchdog.

| Priority | Intended use | Typical core |
|---|---|---|
| `RAF_RT_PRIO_BACKGROUND` (1) | Diagnostics and housekeeping | Core 0 |
| `RAF_RT_PRIO_LOW` (2) | Telemetry and slow work | Core 0 |
| `RAF_RT_PRIO_MID` (3) | General application tasks | Core 1 |
| `RAF_RT_PRIO_HIGH` (4) | Fast sensors and control | Core 1 |
| `RAF_RT_PRIO_REALTIME` (5) | Time-critical dispatch | Core 1 |

Runtime control and metrics are available through:

```c
RAF_TaskEnable(task_id);
RAF_TaskDisable(task_id);
RAF_TaskGetStats(task_id, &stats);
RAF_TaskEnableByName("TempPoll");
RAF_TaskDisableByName("TempPoll");
RAF_TaskGetStatsByName("TempPoll", &stats);
RAF_SchedulerResetMetrics(task_id);
RAF_SchedulerPrintStats();
```

`RAF_TaskStats_t` reports execution count, overruns, deadline misses, missed periods, minimum/maximum/average/last execution time, maximum jitter, and stack high-water usage.

`main/application_system/task.c` still contains empty `job_1ms()` through `job_1000ms()` functions. They are reserved compatibility slots; new hardware should normally use `s_modules[]`, not assume that adding code to `task.c` registers a task.

## Communication Hub

All transports use the same routing path:

```text
transport RX callback
  -> com_push_incoming_request(interface, data, length)
  -> ring buffer
  -> COM_Dispatch task at 1 ms
  -> binary or JSON handler
  -> TX handler for the originating interface
```

Supported interface values are `COM_IF_UART`, `COM_IF_MODBUS`, `COM_IF_LORA`, `COM_IF_WIFI_HTTP`, `COM_IF_MQTT`, `COM_IF_CAN`, `COM_IF_BLE`, and `COM_IF_ESPNOW`.

### Binary packet format

The current inbound type is:

```c
typedef struct {
    uint8_t preamble;       // COMM_PACKET_PREAMBLE: 0xAA
    uint8_t cmd_code;
    uint8_t payload_len;    // 0..32
    uint8_t payload[32];
    uint16_t crc16;
    uint8_t iface_source;   // assigned by com_push_incoming_request()
} __attribute__((packed)) com_inbound_req_t;
```

Responses use `comm_packet_t`: preamble 0xAA, message type (`cmd_code | 0x80`), payload length, up to 64 payload bytes, and CRC-16. CRC is checked before a binary request is dispatched. `CMD_REQ_PING` is handled internally and returns the four-byte string `PONG`.

Current command codes:

| Code | Enum | Current behavior |
|---|---|---|
| `0x01` | `CMD_REQ_PING` | Built-in `PONG` |
| `0x02` | `CMD_REQ_ALL_SENSORS` | IMU acceleration XYZ as three floats |
| `0x03` | `CMD_REQ_IMU` | IMU acceleration XYZ as three floats |
| `0x04` | `CMD_REQ_GPS` | Reserved; no current handler response |
| `0x05` | `CMD_REQ_SYS_STATUS` | Reserved; no current handler response |
| `0x0F` | `CMD_REQ_CUSTOM` | Reserved for application code |

Use `com_register_cmd_handler()` for binary providers and `com_register_json_handler()` for text requests. A binary handler receives the command and input payload and fills no more than 64 output bytes.

### JSON requests

`iot_response_init()` registers the current handler. Recognized requests are:

```json
{"req":"ping"}
{"req":"imu"}
{"req":"accel"}
{"req":"temp"}
{"req":"suhu"}
{"req":"all"}
```

IMU JSON includes acceleration, gyroscope, roll, pitch, and currently fixed `yaw: 0.00`. Temperature JSON includes `temp_c` and unit `C`. The parser uses compact string matching, so follow the key spelling exactly.

### Add a transport

1. Create `init`, `send`, and RX functions in `main/communication/`.
2. Add an enum before `COM_IF_MAX`.
3. Register the TX function with `com_register_tx_handler()`.
4. Push RX data with `com_push_incoming_request()`.
5. Ensure data is valid JSON or a packed binary request with a correct CRC.

`com_push_incoming_request()` rejects null, zero-length, and oversized input. Check `ringbuf_com_get_stats()` when adding a high-rate transport.

## Wi-Fi, MQTT, Modbus TCP, and OTA

`RAF_ApplicationInit()` starts Wi-Fi station mode. After `IP_EVENT_STA_GOT_IP`, `com_wifi.c` starts the OTA server, MQTT client, and configured Modbus TCP master. Wi-Fi reconnects after disconnect.

MQTT subscribes to the configured request topic and publishes responses to the configured response topic. The current direct Modbus request format is:

```json
{"device":1,"sensor":3}
```

Both numbers range from 1 to 10. Periodic MQTT output includes online Modbus device values.

### OTA procedure

1. Run `idf.py build`.
2. Read the board IP from the serial monitor.
3. Open `http://<ESP32_IP>/update`.
4. Select the generated `.bin` file from `build/`.
5. Upload and wait for the board to restart.

The server validates the ESP32 image magic byte (`0xE9`), writes the next OTA partition, verifies it, selects it as the boot partition, and restarts. Do not remove power during the upload.

## Data Logger

The logger has three layers:

1. `data_logger.c`: public record API and schema dictionary.
2. `log_system.c`: ping-pong buffers, chunk creation, background writer, and boot recovery.
3. `app_log.c`: storage backend and pre-erase behavior.

Initialize it once:

```c
esp_err_t err = data_logger_init();
if (err != ESP_OK) {
    ESP_LOGE("APP", "Logger init failed: %s", esp_err_to_name(err));
}
```

Write a record from a periodic callback:

```c
static void RAF_ApplicationJobLog(void *arg) {
    (void)arg;
    const imu_data_t *imu = imu_mpu_get_data();
    device_datalog_t record = {
        .log_param1 = imu->accel_x,
        .log_param2 = imu->accel_y,
        .log_param3 = imu->accel_z,
        .log_param4 = imu->gyro_x,
        .log_param5 = imu->gyro_y,
    };
    data_logger_write_record(&record);
}
```

`data_logger_write_record()` rejects null data and refuses writes while reading is enabled. `Write_Datalog()` is a convenience wrapper that updates the runtime counter and timestamp. Call `data_logger_sync()` before a controlled shutdown or when pending buffered data must be flushed.

`device_datalog_t` contains five application floats, a float timestamp in seconds, and a `uint32_t` sample counter. The current seven schema entries are defined in `data_logger.c`. Export the CSV header with:

```c
char header[256];
data_logger_export_csv_header(header, sizeof(header));
```

Use `data_logger_get_schema(&count)` for programmatic metadata. If the record layout changes, update `NUMBER_OF_LOGDATA`, `device_datalog_t`, and `Logger_data[]` together. Existing records may no longer match the new layout.

The engine writes 4 KB chunks with start/end markers and CRC-32. Boot recovery scans for the highest valid sequence. `app_log_get_backend()` exposes the storage abstraction, and the configured partition is `log_data`.

## Built-in Drivers and Pins

### MPU6050

Files: `main/input/IMU_MPU.c` and `main/include/IMU_MPU.h`.

```c
esp_err_t imu_mpu_init(const imu_mpu_config_t *config); // NULL uses defaults
esp_err_t imu_mpu_init_pins(bsp_i2c_pins_t pins);
void imu_mpu_update(void);
const imu_data_t *imu_mpu_get_data(void);
```

The driver reads 14 bytes from register `0x3B`, uses the +/-2 g scale (16384 LSB/g), uses 131 LSB/(degree/s) for the gyroscope, and converts temperature with the MPU6050 formula. It tries the configured address and the alternate 0x68/0x69 address.

### LCD

Files: `main/input/LCD.c` and `main/include/LCD.h`. It drives an HD44780-compatible display through a PCF8574 I2C expander. Public functions include `lcd_init()`, `lcd_clear()`, `lcd_put_cur()`, `lcd_send_string()`, `lcd_send_cmd()`, and `lcd_send_data()`.

### Default pins

| Device | Macro | Pins |
|---|---|---|
| LCD I2C | `I2C_PINS_LCD` | SDA 26, SCL 25 |
| MPU6050 I2C | `I2C_PINS_IMU` | SDA 14, SCL 12 |
| GPS UART | `UART_PINS_GPS` | TX 17, RX 16 |
| LoRa SPI | `SPI_PINS_LORA` | MOSI 23, MISO 19, SCK 18, CS 5 |
| CAN/TWAI | `CAN_PINS_DEFAULT` | TX 21, RX 22 |

I2C defaults to 400 kHz for the IMU and LCD. Confirm voltage, pull-ups, address jumpers, and pin capabilities on the real board.

## Configuration

Edit `main/include/main.h` for local defaults:

```c
#define WIFI_SSID_DEFAULT       "your-ssid"
#define WIFI_PASS_DEFAULT       "your-password"
#define MQTT_BROKER_URI_DEFAULT "mqtt://broker.example:1883"
#define MQTT_CLIENT_ID_DEFAULT  "ESP32_NODE"
#define MQTT_TOPIC_REQ_DEFAULT  "esp32/node/request"
#define MQTT_TOPIC_RESP_DEFAULT "esp32/node/response"
```

Do not commit production passwords or broker credentials. Use local ignored configuration for deployment secrets.

`sdkconfig.defaults` selects the custom partition table and 4 MB flash. Use `idf.py menuconfig` for ESP-IDF settings and keep board/application defaults in project headers.

## Flash Layout

`partitions.csv` currently defines:

| Name | Type | Offset | Size | Purpose |
|---|---|---:|---:|---|
| `nvs` | data | `0x9000` | 16 KB | NVS |
| `otadata` | data | `0xD000` | 8 KB | OTA boot selection |
| `phy_init` | data | `0xF000` | 4 KB | RF calibration |
| `ota_0` | app | `0x10000` | 1536 KB | Application slot 0 |
| `ota_1` | app | `0x190000` | 1536 KB | Application slot 1 |
| `log_data` | data | `0x310000` | 896 KB | Persistent log storage |

Changing partition sizes changes OTA and log capacity. Reflash the partition table after changing it; old log data may no longer be compatible.

## Testing

Install pytest-embedded packages in the ESP-IDF Python environment:

```powershell
python -m pip install pytest pytest-embedded pytest-embedded-idf pytest-embedded-qemu
pytest pytest_hello_world.py -v
```

The current test file still contains original hello-world expectations. Update those assertions as application boot logs evolve, and add hardware tests for new modules.

## Troubleshooting

**New source is not compiled:** add the `.c` file to `main/CMakeLists.txt`, verify `INCLUDE_DIRS`, then run `idf.py reconfigure`.

**A module does not start:** inspect its named log message, check the return value from `.init`, and verify task name, callback, period, stack size, priority, and core.

**MPU6050 is not found:** verify 3.3 V, GND, SDA/SCL, pull-ups, address jumper, and the configured pin macro.

**Packets are dropped:** inspect `ringbuf_com_get_stats()` or `ringbuf_com_print_stats()`. Every pointer returned by `ringbuf_com_receive()` must be passed to `ringbuf_com_free()`.

**OTA page is unavailable:** OTA starts after the board receives an IP, not merely after Wi-Fi initialization. Confirm both devices are on the same network.

**Scheduler overruns:** reduce callback work, avoid blocking calls, move slow work to a lower-priority task, or increase the period. A larger stack does not solve timing overruns.

## Source Reference

| Path | Responsibility |
|---|---|
| `main/application_system/main.c` | `app_main()` |
| `main/application_system/application.c` | Application startup and module registry |
| `main/application_system/Scheduler.c` | Task registry, timing, watchdog, metrics |
| `main/application_system/task.c` | Reserved periodic job stubs |
| `main/include/app_module.h` | Module descriptor types |
| `main/include/Scheduler.h` | Scheduler lifecycle, control, and statistics API |
| `main/include/main.h` | Wi-Fi, MQTT, bus speed, and board defaults |
| `main/include/COM.h` | Communication enums, packets, handlers, and hub API |
| `main/include/ringbuff_com.h` | Ring buffer, statistics, and CRC API |
| `main/communication/COM.c` | Binary/JSON dispatch and response routing |
| `main/communication/com_wifi.c` | Wi-Fi station and network service startup |
| `main/communication/com_mqtt.c` | MQTT connection and publishing |
| `main/communication/com_ota.c` | HTTP OTA server |
| `main/communication/com_uart.c` | UART/RS485 transport hook |
| `main/communication/com_can.c` | CAN/TWAI transport |
| `main/communication/com_espnow.c` | ESP-NOW transport |
| `main/communication/com_lora.c` | LoRa transport |
| `main/input/IMU_MPU.c` | MPU6050 I2C driver |
| `main/input/LCD.c` | LCD driver |
| `main/input/IoT_Response.c` | Built-in binary and JSON sensor responses |
| `main/data_logger/data_logger.c` | Record API and schema |
| `main/data_logger/log_system.c` | Chunk engine and recovery |
| `main/data_logger/app_log.c` | Storage backend |
| `examples/` | Module templates |
| `partitions.csv` | OTA and log partition layout |
| `main/CMakeLists.txt` | Sources and ESP-IDF dependencies |


## Author

**Rafdi** — Created September 2026.
