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

## Modbus engine tests

The portable engine uses an injected clock and mock transport. Test the actual
C codec, scheduler, and write planner with a host compiler:

```powershell
gcc -std=c11 -O2 -Wall -Wextra -Werror -I main\app `
  main\app\point_mapping.c main\app\modbus_engine.c `
  tools\test_modbus_engine.c -lm -o build\test_modbus_engine.exe
if ($LASTEXITCODE -eq 0) { .\build\test_modbus_engine.exe }
```

For Zig, replace `gcc` with `zig cc`. Tests cover typed/endian codecs and scaling,
range/nonfinite rejection, all read functions, grouping limits and isolation,
completion-based timing, retry counts, backoff/recovery, stale boundaries,
invalid samples, exception fallback, exact FC05/06/15/16 payloads, permissions,
and write completion without retries or optimistic readback. Assertions remain
enabled in optimized builds. These are mock-transport tests, not RS485 simulator
or physical-device commissioning.

The service tests compile the actual service against deterministic
UART/FreeRTOS/ESP-Modbus mocks:

```powershell
python tools\test_modbus_service.py
# Or use Zig:
python tools\test_modbus_service.py --compiler zig cc
```

They verify idle/disabled behavior, UART configuration, descriptor registration
before controller startup, startup-failure cleanup, queue bounds and pending
flags, command completion/readback, and snapshot staleness. Mock headers are
temporary and do not replace the real ESP-IDF firmware build.

## BACnet Object Model

Run the mapped-object bridge tests with a host C11 compiler:

```powershell
python tools\test_mapped_objects.py
# Or use Zig:
python tools\test_mapped_objects.py --compiler zig cc
```

These compile the actual gateway bridge, Modbus encoder, BACnet application
codecs and AI/AV/BI/BV implementations. Only ESP/NVS/Modbus service boundaries
and Device list enumeration are mocked. Assertions cover the empty table,
nonconsecutive instances, value and reliability recovery, encoded Status_Flags,
COV change detection, command acceptance/errors without optimistic readback,
strict payload decoding, integer/range checks, Out_Of_Service simulation and
metadata delegation. This does not test network APDUs or physical RS485 devices.

The default active table is empty, so only the Device object is exposed.
`USER_POINT_MAPPINGS` in `main/User_Settings.c` defines any additional AI/AV/BI/BV
objects; fixed sensor object arrays and role indexes have been removed.
The existing BACnet dispatcher publishes Modbus snapshots and reliability.
Writable AV/BV points accept asynchronous queued commands; readback remains the
source of Present_Value. Both BACnet transports use the same mapped handlers;
simultaneous transport/COV hardware commissioning remains outstanding.

## Dual-transport protocol tests

```powershell
python tools\test_bacnet_transports.py
# Or use Zig:
python tools\test_bacnet_transports.py --compiler zig cc
```

The runner compiles the real BACnet APDU dispatcher, discovery/ReadProperty/COV
handlers, NPDU/application codecs, transaction state machine and runtime datalink
switch. UDP and MS/TP send/receive boundaries and the Device/object data provider
are mocked. Fixtures are test-only; the active mapping table is not modified.
All scenarios use the same multiple-datalink build: IP alone, MS/TP alone, and
interleaved IP/MS/TP requests.

Assertions inspect encoded packets and egress links for unrestricted/ranged
Who-Is/I-Am, Device identification and mapped-value ReadProperty, unknown-object
errors, initial/changed COV on both links, SubscribeCOVProperty Present_Value,
renewal/cancellation/expiration, confirmed ACKs, retries/exhaustion, failed sends,
and transport-context restoration. The same object/process can be subscribed
on both links; cancellation on one does not remove the other. The tests also
exercise equal address bytes in separate transport contexts to guard against
address-only subscription matching.

These are deterministic in-process protocol tests, not UDP socket, BVLC/BBMD,
FreeRTOS concurrency, token-ring or electrical tests. See
[the physical validation checklist](../SETUP.md#dual-transport-validation).

For a clean ESP-IDF 6.0.2 build, use a fresh build directory:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\tools\build_idf60.ps1 -B build\gateway-step5 build
```

If reusing that directory, pass `fullclean build` instead of `build`. Neither
command flashes the device.

For BACnet verification, use tools such as YABE, BACnet Scanner, or Visual
Test Shell. See [`OBJECTS_CONFIGURATION.md`](../OBJECTS_CONFIGURATION.md) for
the object roles and persistence behavior.
