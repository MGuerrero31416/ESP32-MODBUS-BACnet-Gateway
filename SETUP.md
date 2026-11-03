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
Read grouping defaults to two registers or one bit, with a 30-second maximum
offline-slave backoff. Increase grouping only after checking device capabilities.
ESP-Modbus RTU support must be enabled. Its master API blocking limit must cover
the response timeout plus cooldown plus 1000 ms; startup checks this.

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
6. Start BACnet processing.
7. Start the Modbus UART worker if mappings exist; otherwise leave Modbus idle.
8. Run status supervision.

No sensor, LoRa acquisition, Adafruit IO, or BO1 GPIO synchronization task runs.
Modbus polling, decoding, recovery, snapshots, and queued commands are available.
The existing BACnet dispatcher publishes good Modbus snapshots and updates
reliability/status flags. Writable AV/BV Present_Value requests enter the
existing nonblocking queue; acknowledgment means acceptance, not remote
execution. Read-only points reject in-service writes. Out_Of_Service freezes
BACnet acquisition and permits local simulation without new Modbus commands.
See [write/reliability behavior](OBJECTS_CONFIGURATION.md#bacnet-writes-and-out_of_service).

Ideaspark/HW657A show point count, Wi-Fi state, recent MS/TP traffic, and
Modbus good/total acquisition count (or No points/Disabled). Recent traffic is not proof of complete BACnet service
health. Other profiles retain their hardware drivers and await full gateway UI
conversion.

When hardware commissioning is authorized, use YABE or another BACnet client
for discovery and Device property reads. BACnet/IP uses UDP 47808 by default.
Host protocol tests validate transport selection for simultaneous IP/MS/TP
services. Wire-level COV delivery, Modbus simulator/physical traffic,
disconnect recovery, and command readback still need commissioning.
Do not interpret a successful build as hardware or protocol commissioning.

## Dual-transport validation

### Inspection and targeted corrections

Already supported before this phase:

- Both datalinks initialize independently and share the same Device/object
  database. Existing receive tasks preserve ingress-link identifiers in queued
  frames; the BACnet dispatcher selects that link under its existing mutex.
- Startup I-Am is sent on both enabled links. Who-Is and ReadProperty handlers
  run in the selected request context, so their replies use the ingress link.
- COV handlers, the subscription lifecycle, and a bounded full-FSM scan already
  exist. Step 4 object setters retain the stack's COV change detection.

Two deferred-send defects were reproduced and corrected:

1. The periodic COV scan preferred MS/TP whenever available; an IP subscription
   could therefore be notified on MS/TP. COV addresses now retain the ingress
   transport, including matching for renewal/cancellation. Each notification
   selects that transport before encoding its source NPDU and sending, then
   restores the caller's context. One shared scan still marks all subscribers
   before clearing object change flags; changes are not consumed by just one link.
2. Confirmed COV retries used the datalink active at timer execution, not the
   original transaction link. TSM transactions now retain the originating link.
   The existing 100-ms dispatcher invokes retry timers under the same datalink
   mutex as requests and COV sends.

No new task, dependency, router, object, mapping, pin or display behavior was
introduced. These changes do not bridge BACnet networks or forward Who-Is
between transports. Both interfaces expose the same Device instance.

The [host protocol tests](docs/BUILD_INSTRUCTIONS.md#dual-transport-protocol-tests)
pass IP-only, MS/TP-only and interleaved dual-link scenarios. Replacing either
the COV or TSM implementation with its pre-fix version independently reproduces
the wrong-link assertion. Physical operation is **not yet validated**.

### Remaining hardware/network checks

Perform only when device access and commissioning are authorized; do not flash
as part of host validation. Keep the existing UART/pin allocation and use
separate RS485 buses.

| Check | Required evidence |
|---|---|
| Startup and discovery | Observe startup I-Am and matching Device instance on both interfaces; issue unrestricted and ranged Who-Is from IP and MS/TP clients concurrently. |
| ReadProperty | Interleave Device identity/Object_List reads on both links; confirm requester address and invoke ID in replies, with no cross-link response. Empty mappings mean Device-only testing. |
| Shared mapped data | Only after a real register map is supplied, read the same point from both clients while the actual Modbus slave changes; confirm consistent values, reliability and fault flags. Do not add dummy active points for this check. |
| Unconfirmed COV | Subscribe both clients to the same real point, using the same process ID if supported; capture initial and changed-value/status notifications on each subscriber's own link. |
| COV lifecycle | Renew/cancel one client's subscription while the other remains live; verify expiration, no duplicate notifications, and Present_Value property-subscription behavior. |
| Confirmed COV | ACK each client's notifications; then suppress ACK on one client and capture retries only on that client's link while reads/COV on the other remain functional. Verify retry exhaustion and recovery. |
| Physical/load behavior | Verify MS/TP token participation, MAC uniqueness/Max Master, baud/termination/polarity, RS485 direction timing, UDP 47808/firewall and Wi-Fi reconnect. Check BBMD/FDR separately if needed by the network. |
| Combined load/recovery | Run sustained reads and COV with Modbus polling, then interrupt each connection independently. Observe queue drops, latency, subscription renewal and recovery without starving the other link. |

Limitations: host tests replace physical transport and Device data-provider
boundaries; they do not prove token-ring timing, UDP/BVLC/BBMD behavior or RTOS
contention. The active point table remains empty. COV capacity remains the
existing stack limits (128 subscriptions, 16 subscriber addresses); segmentation
and full SubscribeCOVProperty conformance are not added. The retained property
handler maps Present_Value subscriptions to ordinary object COV rather than
implementing independent per-property increments. These are not claims of BACnet
conformance or hardware commissioning.

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
- **BACnet points show a fault:** inspect snapshot quality and the latest command
  result. Good reads clear acquisition faults, but a failed command remains
  faulted until a later successful command. Do not retry commands blindly.
- **BACnet writes are rejected:** check mapping access, application type/value,
  priority, pending commands and queue availability. Read-only points allow only
  local simulation while Out_Of_Service.
- **Modbus snapshots show errors:** check slave/address/table, format and byte/
  word ordering, serial settings, direction wiring, termination, and power.
  Logs include request context; counters and per-point snapshots expose failures.
- **Objects retain edited names/metadata:** existing gateway NVS overrides
  take precedence for the same type/instance. Old sensor metadata is ignored.
- **BACnet/IP is unavailable:** check credentials, subnet/static addressing,
  UDP firewall rules, and BBMD settings where applicable.
