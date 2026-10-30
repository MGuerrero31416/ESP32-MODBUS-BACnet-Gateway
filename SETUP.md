# ESP32 Modbus BACnet Gateway Setup

## Environment

- ESP-IDF 6.0.2, installed at `C:\esp\v6.0.2\esp-idf` for the supplied wrapper.
- Arduino-ESP32 3.3.11, selected by the component manifest.
- Git and Visual Studio Code with the ESP-IDF extension.

```powershell
git clone https://github.com/MGuerrero31416/ESP32-MODBUS-BACnet-Gateway.git
Set-Location ESP32-MODBUS-BACnet-Gateway
```

Create `main/User_Private_Settings.h` from
[`main/User_Private_Settings.example.h`](main/User_Private_Settings.example.h)
and supply Wi-Fi credentials. The private file is ignored by Git; do not commit
it. Adafruit IO publishing is no longer started.

## Select hardware

The current board is **Ideaspark ESP32-WROOM-32 1.9-inch**:

1. Select the ESP-IDF target `esp32`.
2. Select `Project hardware > Display profile > Ideaspark` in menuconfig.
3. Check backlight GPIO32, or GPIO27 for the corresponding board revision.
4. Verify the independent serial allocations:

| Bus | UART | TX | RX | DE + /RE |
|---|---:|---:|---:|---:|
| Modbus RTU | 1 | GPIO25 | GPIO26 | GPIO33 |
| BACnet MS/TP | 2 | GPIO17 | GPIO16 | GPIO5 |

See [Ideaspark wiring](docs/profiles/ideaspark.md) for electrical safety,
termination, biasing, boot-strapping pins, and logic-voltage requirements.
Two converters are required; do not join their RS485 A/B pairs together.

The ESP32 target defaults to Ideaspark for fresh configurations. Existing
`sdkconfig` selections are preserved. Other display/board profiles remain
available; ESP32-S3 profiles require the `esp32s3` target.

Use `Project hardware > Modbus RTU hardware` for Modbus serial settings.
Defaults are 9600 baud, 8E1, 1000 ms response timeout, two retries, and 1000 ms
default polling interval. The per-point table contains explicit polling/stale
timings. MS/TP MAC, baud, Max Master, and Max Info Frames remain in
[`main/User_Settings.c`](main/User_Settings.c).

On other profiles, explicitly allocate unused Modbus GPIOs before enabling
the port. Startup rejects conflicting allocations. Physical wiring still
requires verification against the actual board revision.

## Configure identity, networking, and mappings

Edit [`main/User_Settings.c`](main/User_Settings.c) for:

- BACnet identity and unique device instance.
- BACnet/IP and MS/TP enable flags.
- Static IP/DHCP selection and static addressing.
- BBMD foreign-device registration settings, where required.
- `USER_POINT_MAPPINGS`.

The default mapping table is empty, exposing only the Device object.
Read [Point Mapping Configuration](OBJECTS_CONFIGURATION.md) before adding
device addresses, register formats, permissions, and BACnet objects. Examples
are documentation only, not active device definitions.

There are no fixed sensor role indexes or parallel BACnet object arrays.
Invalid mappings log their table index and reason and stop startup before
network or serial initialization.

## Build without flashing

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\tools\build_idf60.ps1 -B build\gateway-step1 build
```

From an initialized ESP-IDF 6.0.2 terminal:

```powershell
idf.py -B build\gateway-step1 build
```

Do not reuse the copied old project's root build cache. After changing target,
display profile, TFT setup, or GPIO mapping, use a fresh build directory or
clean the selected build directory and rebuild. See
[Build Instructions](docs/BUILD_INSTRUCTIONS.md) for focused tests.

Do not flash unless explicitly instructed and the wiring has been checked.

## Current runtime and commissioning limits

Startup:

1. Validate all mappings.
2. Validate UART/GPIO allocations.
3. Initialize NVS and diagnostics.
4. Initialize BACnet/IP and MS/TP as enabled.
5. Create mapped BACnet objects and initialize the selected display.
6. Start BACnet processing and status supervision.

No sensor, LoRa acquisition, Adafruit IO, or BO1 GPIO synchronization task runs.
No Modbus driver or polling task is implemented yet. Configured process points
have communication-failure reliability until the later acquisition step.
Present_Value writes are explicitly rejected pending write-through support.

Ideaspark/HW657A show point count, Wi-Fi state, recent MS/TP traffic, and
inactive Modbus status. Recent traffic is not proof of complete BACnet service
health. Other profiles retain their hardware drivers and await full gateway UI
conversion.

When hardware commissioning is authorized, use YABE or another BACnet client
for discovery and Device property reads. BACnet/IP uses UDP 47808 by default.
Further conversion steps must validate simultaneous IP/MS/TP COV delivery,
Modbus simulator/physical traffic, disconnect recovery, and command readback.
Do not interpret a successful build as hardware or protocol commissioning.

## Persistence and troubleshooting

Normally leave `USER_OVERRIDE_NVS_ON_FLASH = 0`. Setting it to 1 erases the
entire NVS partition on every boot; return it to zero after an intentional
reset. Gateway metadata uses `gw_points_v1`, not the old sensor `bacnet`
namespace. No process values or pending commands are restored or replayed.

- **Only Device appears:** expected for an empty active mapping table.
- **Mapping validation fails:** check the reported index and reason against
  [the schema and examples](OBJECTS_CONFIGURATION.md).
- **UART/GPIO validation fails:** check both ports, display/touch,
  console, flash/PSRAM, and board-reserved pins.
- **Points show a fault or writes are rejected:** expected at this stage;
  polling and write-through have not been implemented.
- **Objects retain edited names/metadata:** existing gateway NVS overrides
  take precedence for the same type/instance. Old sensor metadata is ignored.
- **BACnet/IP is unavailable:** check credentials, subnet/static addressing,
  UDP firewall rules, and BBMD settings where applicable.
