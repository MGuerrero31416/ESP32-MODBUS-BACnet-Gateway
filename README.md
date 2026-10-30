# ESP32 Modbus BACnet Gateway

Repository: <https://github.com/MGuerrero31416/ESP32-MODBUS-BACnet-Gateway>

ESP32/ESP32-S3 gateway under conversion: a Modbus RTU client/master on one
RS485 bus, with mapped points exposed over BACnet/IP (Wi-Fi) and BACnet MS/TP
on a second RS485 bus. Board, display, and touch hardware profiles are retained.

## Conversion status

- **Step 1 complete:** independent serial configuration, startup GPIO/UART
  conflict checks, and Ideaspark WROOM32 1.9-inch defaults.
- **Step 2 complete:** a single validated compiled point-mapping table replaces
  sensor roles and parallel object arrays. BACnet objects are created from
  mappings. Sensor, LoRa acquisition, and Adafruit IO application tasks are
  disconnected.
- **Not implemented yet:** Modbus polling/decoding, write-through, full
  dual-transport COV verification, and complete gateway UI layouts.

The active mapping table is deliberately **empty** until an actual device
register map is supplied. The BACnet Device object remains available; no
invented Modbus devices or process points are enabled. Configured points start
in communication-failure, not as valid zero readings. Present_Value writes
are rejected until Modbus write-through is implemented.

See [Point Mapping Configuration](OBJECTS_CONFIGURATION.md) for all fields,
validation rules, address conventions, and disabled examples.

## Current board and wiring

Ideaspark ESP32-WROOM-32 with a 1.9-inch ST7789 display:

| Bus | UART | TX | RX | DE + /RE |
|---|---:|---:|---:|---:|
| Modbus RTU | 1 | GPIO25 | GPIO26 | GPIO33 |
| BACnet MS/TP | 2 | GPIO17 | GPIO16 | GPIO5 |

Modbus serial defaults: 9600 baud, 8E1. MS/TP defaults: 38400 baud, 8N1.
The two RS485 buses are electrically separate. Use ESP32-compatible logic
levels and check the [Ideaspark wiring guide](docs/profiles/ideaspark.md)
before connecting converters.

Other profiles remain selectable in menuconfig under `Project hardware`.
Allocate Modbus GPIOs explicitly before enabling it on another board.

## Configuration

- [`main/User_Settings.c`](main/User_Settings.c): BACnet identity/network
  configuration and `USER_POINT_MAPPINGS`.
- [`main/app/point_mapping.h`](main/app/point_mapping.h): mapping schema.
- Menuconfig: board/display, both UART/GPIO allocations, Modbus serial defaults.
- `main/User_Private_Settings.h`: local Wi-Fi credentials, ignored by Git.
  Create it from [the example](main/User_Private_Settings.example.h).

Object metadata overrides use the `gw_points_v1` NVS namespace. Old sensor
object settings are not loaded, and process values/commands are not restored.
Normally leave `USER_OVERRIDE_NVS_ON_FLASH = 0`.

## Build and tests

Environment: ESP-IDF 6.0.2 and Arduino-ESP32 3.3.11.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\tools\build_idf60.ps1 -B build\gateway-step1 build
```

The separate build directory avoids the stale CMake cache inherited from the
old project. Building does not flash the device.

See [Build Instructions](docs/BUILD_INSTRUCTIONS.md) for configuration and C
mapping-validator tests, and [Setup](SETUP.md) for commissioning guidance.

## Source layout

```text
main/
  app/point_mapping.*       Mapping model, lookup, and validation
  app/app_storage.*         NVS initialization
  app/app_supervisor.*      Status/display maintenance
  bacnet/mapped_objects.*   Mapping-driven BACnet object setup
  bacnet/bacnet_app.*       BACnet transports and dispatcher
  platform/                Wi-Fi and RS485 hardware configuration
  ui/                      Preserved display and touch profiles
  User_Settings.c          Public configuration and active table
  main.c                   Validated startup
```

Some retained low-level drivers and non-Ideaspark display documentation still
describe the inherited sensor/radio application. They do not enable acquisition
in the gateway firmware; their cleanup and full display conversion are later
steps.
