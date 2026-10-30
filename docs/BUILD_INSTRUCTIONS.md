# ESP32 Modbus BACnet Gateway - Build Instructions

This project uses ESP-IDF 6.0.2 and Arduino-ESP32 3.3.11. The complete
environment and configuration procedure is documented in
[`SETUP.md`](../SETUP.md).

## Prerequisites

- ESP-IDF 6.0.2 installed at `C:\esp\v6.0.2\esp-idf`, or an initialized
  ESP-IDF 6.0.2 terminal
- Visual Studio Code with the ESP-IDF extension
- Wi-Fi credentials in `main/User_Private_Settings.h`

Create the private settings file from
`main/User_Private_Settings.example.h`; do not commit the private file.

## Build Steps

From the repository root, use the supplied wrapper when ESP-IDF is installed
at the required path:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\tools\build_idf60.ps1 build
```

From an initialized ESP-IDF 6.0.2 terminal, the equivalent command is:

```powershell
idf.py build
```

The current ESP32 board default is Ideaspark WROOM32 1.9-inch. Hardware
configuration tests use the Kconfig parser already installed with ESP-IDF:

```powershell
python .\tools\test_gateway_kconfig.py
```

These tests verify profile defaults, serial allocation, parity/stop-bit
defaults, configuration bounds, and removal of sensor options.
They do not test physical RS485 traffic.

If this checkout was copied from a differently named project, avoid its stale
CMake cache by using a fresh build directory (also ignored by Git):

```powershell
idf.py -B build\gateway-step1 build
```

Select the ESP-IDF target and display profile before building. After changing
the target, display profile, TFT setup, or GPIO mapping, run:

```powershell
idf.py fullclean
idf.py build
```

## Flash and Monitor

Replace `COMx` with the device serial port:

```powershell
idf.py -p COMx flash monitor
```

Do not flash a device until the selected target, display profile, and wiring
have been checked.

## Point mapping tests

The mapping validator is portable C. Compile and run its actual implementation
with a host C11 compiler (GCC or Clang), not the ESP32 cross-compiler:

```powershell
gcc -std=c11 -Wall -Wextra -Werror -I main\app `
  main\app\point_mapping.c tools\test_point_mapping.c `
  -o build\test_point_mapping.exe
if ($LASTEXITCODE -eq 0) { .\build\test_point_mapping.exe }
```

For Zig's bundled C compiler, replace `gcc` with `zig cc`. The tests cover
empty tables, slave/address limits, multi-register spans, all numeric formats,
Boolean bits, permissions, finite scaling, timing, metadata, duplicate objects,
writable overlaps, nonconsecutive instances, lookup, and the 64-point limit.
These tests do not require a connected device.

## BACnet Object Model

The default active table is empty, so only the Device object is exposed.
`USER_POINT_MAPPINGS` in `main/User_Settings.c` defines any additional AI/AV/BI/BV
objects; fixed sensor object arrays and role indexes have been removed.
Configured values remain faulted until polling is implemented, and
Present_Value writes are rejected until write-through exists.

For BACnet verification, use tools such as YABE, BACnet Scanner, or Visual
Test Shell. See [`OBJECTS_CONFIGURATION.md`](../OBJECTS_CONFIGURATION.md) for
the object roles and persistence behavior.
