# Modbus to BACnet Point Mappings

## Configuration source

[`USER_POINT_MAPPINGS`](main/User_Settings.c) is the single compiled source
for Modbus addresses, conversion settings, permissions, timing, and BACnet
object definitions. The old sensor roles, fixed object counts, and parallel
metadata arrays have been removed.

The active table is intentionally **empty** until an actual register map is
provided. Only the BACnet Device object is exposed. Add entries before the
final `{0}` sentinel; leave that sentinel in place. The point count is derived
automatically, excluding the sentinel. Up to 64 points are supported.

The schema and validator are in
[`point_mapping.h`](main/app/point_mapping.h) and
[`point_mapping.c`](main/app/point_mapping.c). Startup validates the complete
table before initializing communication hardware. An invalid mapping logs its
zero-based table index and reason, then stops startup.

## Fields

| Field | Meaning |
|---|---|
| `slave_address` | Modbus unit address, 1..247; broadcast is not allowed |
| `register_type` | `GATEWAY_COIL`, `GATEWAY_DISCRETE_INPUT`, `GATEWAY_INPUT_REGISTER`, or `GATEWAY_HOLDING_REGISTER` |
| `address` | Zero-based protocol address, 0..65535 |
| `data_type` | `GATEWAY_BOOL`, `GATEWAY_UINT16`, `GATEWAY_INT16`, `GATEWAY_UINT32`, `GATEWAY_INT32`, or `GATEWAY_FLOAT32` |
| `byte_order` | Bytes within each 16-bit register: `GATEWAY_BYTES_BIG_ENDIAN` or `GATEWAY_BYTES_LITTLE_ENDIAN` |
| `word_order` | Two-register values: `GATEWAY_HIGH_WORD_FIRST` or `GATEWAY_LOW_WORD_FIRST` |
| `bit_index` | `GATEWAY_NO_BIT` normally; 0..15 for a Boolean extracted from a register, with bit 0 the least significant bit after byte ordering |
| `scale`, `offset` | Engineering value = decoded numeric value * scale + offset |
| `access` | `GATEWAY_READ_ONLY` or `GATEWAY_READ_WRITE` |
| `poll_interval_ms` | Per-point polling interval, 100..3600000 ms |
| `stale_timeout_ms` | At least the poll interval, at most 86400000 ms |
| `object_type` | `GATEWAY_OBJECT_AI`, `GATEWAY_OBJECT_AV`, `GATEWAY_OBJECT_BI`, or `GATEWAY_OBJECT_BV` |
| `instance` | BACnet instance, 0..4194302, independent of table position |
| `name`, `description` | Required string pointers; name nonempty, max 64 characters; description max 128 |
| `units` | BACnet engineering-units enumeration, e.g. `UNITS_DEGREES_CELSIUS` |
| `cov_increment` | Finite and greater than zero for numeric points; zero for Boolean points |
| `active_text`, `inactive_text` | Required nonempty text for Boolean points, max 64 characters each |

Numeric scale must be finite and nonzero; offset must be finite. Negative
scaling is supported. Boolean points require scale 1 and offset 0.
Byte/word-order fields are explicit even where word ordering has no effect.
The low/high word settings refer to value significance, not wire arrival order
within a word.

### Address notation

Convert a vendor's reference notation before entering `address`:

| Vendor notation (conventional) | Register type | Protocol address |
|---|---|---:|
| Coil 00001 | Coil | 0 |
| Discrete input 10001 | Discrete input | 0 |
| Input register 30001 | Input register | 0 |
| Holding register 40001 | Holding register | 0 |
| Holding register 40010 | Holding register | 9 |

Some manuals already give zero-based addresses, or use extended notation.
Always verify the manual: do not subtract 40001 from a raw protocol address.
A 32-bit value occupies two consecutive registers and cannot begin at 65535.

### Object and permission rules

| Modbus value | Read-only BACnet representation | Read/write representation |
|---|---|---|
| Numeric register value | AI or read-only AV | AV, holding registers only |
| Coil | BI or read-only BV | BV |
| Discrete input | BI or read-only BV | Not allowed |
| Bit extracted from a register | BI or read-only BV | Not supported |

AI/BI cannot be configured read/write. Register-bit writes are rejected because
an uncoordinated read-modify-write could overwrite other bits. AO/BO priority
semantics are not part of this table version.

BACnet object type/instance pairs and object names must be unique, including
the Device object name. The same
instance may be used for different object types. Nonconsecutive instances and
instance zero are supported. Read-only register aliases are allowed. Overlapping
address spans on the same slave/register table are rejected if either mapping
is writable, including overlaps with the second word of a 32-bit value.

## Examples (not enabled)

Insert entries like these before the sentinel only after checking the actual
device manual. These are illustrative device addresses, not discovered hardware:

```c
{
    .slave_address = 1,
    .register_type = GATEWAY_INPUT_REGISTER,
    .address = 0, /* Conventional reference 30001. */
    .data_type = GATEWAY_INT16,
    .byte_order = GATEWAY_BYTES_BIG_ENDIAN,
    .word_order = GATEWAY_HIGH_WORD_FIRST,
    .bit_index = GATEWAY_NO_BIT,
    .scale = 0.1f,
    .offset = 0.0f,
    .access = GATEWAY_READ_ONLY,
    .poll_interval_ms = 1000,
    .stale_timeout_ms = 5000,
    .object_type = GATEWAY_OBJECT_AI,
    .instance = 101,
    .name = "Room Temperature",
    .description = "Signed input register in tenths of a degree",
    .units = UNITS_DEGREES_CELSIUS,
    .cov_increment = 0.1f,
},
{
    .slave_address = 1,
    .register_type = GATEWAY_HOLDING_REGISTER,
    .address = 9, /* Conventional reference 40010. */
    .data_type = GATEWAY_FLOAT32,
    .byte_order = GATEWAY_BYTES_BIG_ENDIAN,
    .word_order = GATEWAY_LOW_WORD_FIRST,
    .bit_index = GATEWAY_NO_BIT,
    .scale = 1.0f,
    .offset = 0.0f,
    .access = GATEWAY_READ_WRITE,
    .poll_interval_ms = 2000,
    .stale_timeout_ms = 10000,
    .object_type = GATEWAY_OBJECT_AV,
    .instance = 205,
    .name = "Temperature Setpoint",
    .description = "Two-register IEEE 754 setpoint, low word first",
    .units = UNITS_DEGREES_CELSIUS,
    .cov_increment = 0.1f,
},
{
    .slave_address = 2,
    .register_type = GATEWAY_COIL,
    .address = 0,
    .data_type = GATEWAY_BOOL,
    .byte_order = GATEWAY_BYTES_BIG_ENDIAN,
    .word_order = GATEWAY_HIGH_WORD_FIRST,
    .bit_index = GATEWAY_NO_BIT,
    .scale = 1.0f,
    .offset = 0.0f,
    .access = GATEWAY_READ_WRITE,
    .poll_interval_ms = 1000,
    .stale_timeout_ms = 5000,
    .object_type = GATEWAY_OBJECT_BV,
    .instance = 301,
    .name = "Fan Enable",
    .description = "Example writable coil",
    .units = UNITS_NO_UNITS,
    .cov_increment = 0.0f,
    .active_text = "Enabled",
    .inactive_text = "Disabled",
},
```

For a register bit, use `GATEWAY_BOOL`, a register table, `bit_index = 0..15`,
read-only access, and BI/BV. All other Boolean requirements still apply.

## Current runtime behavior

[`mapped_objects.c`](main/bacnet/mapped_objects.c) creates exactly one object
per mapping. Objects start with `Reliability = communication-failure`; their
default Present_Value is **not acquired data**. No sensor, LoRa acquisition,
Adafruit IO publishing, or BO1 synchronization task runs.

The Modbus service polls configured points and retains typed engineering values
and acquisition diagnostics in snapshots. The active table remains empty until
you supply real device mappings; this starts no Modbus UART driver or task.
The existing BACnet dispatcher consumes snapshots under its existing object/
datalink lock before COV processing (nominally every 100 ms). The legacy
non-dispatcher configuration consumes them in its existing COV task. No new task
or transport is introduced, and no UART I/O runs in a BACnet handler.

- Good, acquired samples update AI/AV/BI/BV Present_Value using the stack's
  standard setters, preserving existing COV detection.
- Unacquired, stale, and failed reads retain the last BACnet value and set
  `Reliability = communication-failure`. Invalid decoded values or values outside
  BACnet REAL's finite range set `unreliable-other` instead. A fresh valid read
  recovers acquisition reliability. The stack derives Status_Flags.FAULT from
  reliability. The COV service retains each subscriber's ingress transport;
  [dual-transport commissioning](SETUP.md#dual-transport-validation) remains required.
- Analog Present_Value is BACnet REAL (float32). Large 32-bit integers can lose
  precision when exposed; the engine retains the full double-precision value.
  A client writing an integer through REAL must account for float32 rounding.

### BACnet writes and Out_Of_Service

The mapped Device dispatch handlers are shared by BACnet/IP and MS/TP.
In service, read-only points reject Present_Value writes with
`write-access-denied`. Writable AV/BV mappings enqueue exactly one command per
WriteProperty. The engine applies inverse scaling, byte/word order and range/
integer checks; it never silently truncates a register value.

- Analog writes accept the retained numeric application types (REAL, DOUBLE,
  unsigned, signed, enumerated), but reject nonfinite/out-of-REAL-range values.
  Binary writes require ENUMERATED inactive(0)/active(1), not BOOLEAN.
- Scalars require no array index. Empty, malformed, truncated or extra payload
  data is rejected. Unsupported types return `invalid-data-type`; invalid values
  return `value-out-of-range`. Queue-full, already-pending or inactive-service
  errors return resources/`device-busy` without acknowledging the command.
- A successful BACnet acknowledgment means **queue acceptance only**. It is not
  delayed for serial I/O and does not change Present_Value optimistically.
  Polling supplies readback, not the requested command value. No automatic
  equality check assumes the device must echo a command register.
- A failed command sets communication-failure (timeout, bad response, I/O error,
  offline) or process-error (other command errors). Even successful reads do not
  erase the latest failed command result; a later successful command clears that
  command fault. Per-point completion IDs/results remain available through the
  service snapshots, not new proprietary BACnet properties.
- These AV/BV objects remain **non-commandable**: no Priority_Array, relinquish
  default, NULL relinquish or AO/BO semantics are added. Priorities 1..16 except
  reserved 6 are accepted without arbitration, matching the retained AV behavior.
- Setting Out_Of_Service freezes BACnet snapshot updates, including reliability;
  Modbus polling continues. Local simulation Present_Value writes are allowed
  for all mapped types, even read-only points, without enqueueing Modbus traffic.
  A valid local simulation sets reliability to no-fault-detected. Out_Of_Service
  setters preserve the stack's status-flag/COV change detection.
- Clearing Out_Of_Service resumes the latest snapshot on the next BACnet tick,
  including any outstanding read/command fault. Commands already accepted before
  Out_Of_Service may still execute; the flag does not cancel the existing queue.
  Simulation values and Out_Of_Service state are not persisted.

This step does not implement BACnet priority arrays, runtime mapping editing,
or a full gateway UI. Ideaspark/HW657A show mapping count, link state, and
Modbus good/total acquisition count (or No points/Disabled). Other hardware profiles remain
selectable; their complete gateway layouts are a later conversion step.

## Modbus acquisition and command API

[`modbus_service.h`](main/app/modbus_service.h) exposes thread-safe point
snapshots, service counters, and nonblocking queued writes. Only its dedicated
worker accesses the Modbus controller; callers must not use that controller or
UART directly.
The controller's required raw-register dictionary is derived automatically
from the mapping table; conversion and scheduling remain in the engine.

- FC01/02 read packed coils/discrete inputs; FC03/04 read holding/input registers.
  Register payload words are numeric host-endian values; mapping byte/word order
  is applied explicitly before scaling.
- Only due points on the same slave, register table, and poll interval are
  grouped, without gaps. Menuconfig caps reads at 2..125 registers (default 2)
  or 1..2000 bits (default 1). A grouped remote exception switches that slave to
  individual point reads for the rest of the boot. The library exposes a generic
  exception result, not the specific Modbus exception code.
- Timeout/bad-response reads retry up to the configured count (default two
  retries after the first attempt). Communication failures back off all points
  on that slave from 1 second, doubling to the configured cap (default 30 s).
  Other slaves remain eligible; the shared serial bus still serializes requests.
- Snapshots distinguish unacquired, good, stale, and error. Stale starts at
  `stale_timeout_ms` after the last successful sample. Failed/invalid reads keep
  the last good value, but never report it as good. Nonfinite float data is an
  explicit acquisition error. Poll intervals are best-effort under bus load and
  are measured from transaction completion.
- `modbus_service_queue_write()` requires a nonzero caller ID and 1..16
  consecutive table entries with contiguous wire addresses, on one slave/table.
  Every point must permit writes; values must encode without integer truncation
  or range overflow. FC05/06 handle single coil/register writes; FC15/16 handle
  multiple coils/registers, including single 32-bit points.
- Queue depth is eight; inactive, invalid, already-pending, and full requests
  return explicit errors. Acceptance is **not execution confirmation**. Inspect
  the point snapshot's pending flag, `write_completed`, `last_write_id`, and
  `last_write_result`; only the most recent completion per point is retained.
  Avoid ID reuse while matching completions.
- Writes are **never automatically retried**: a timeout can mean the device
  applied the command without returning an acknowledgment. Offline slaves reject
  commands without bus traffic. Completion does not overwrite readback; affected
  points become due for a confirming poll. A transport acknowledgment alone does
  not prove the device's process value changed.
- No command, acquired value, or offline state is persisted or replayed on boot.

The Modbus timeout plus ESP-Modbus cooldown plus 1000 ms must fit within
`CONFIG_FMB_MASTER_MAX_API_BLOCKING_TIME_MS`; startup reports an error otherwise.
Enable `CONFIG_FMB_COMM_MODE_RTU_EN` when the gateway service is enabled.

## Persistence

Mappings are compiled configuration, not runtime NVS data. Mapped object names,
descriptions, supported analog units, and AI COV increments retain the stack's
metadata-write behavior in the new `gw_points_v1` NVS namespace. These metadata
overrides take precedence on reboot for the same object type/instance.
Startup also rejects duplicate names caused by saved metadata overrides.
AV COV increments follow the existing volatile stack behavior.

No old `bacnet` sensor namespace is loaded. Polled values and commands are never
restored or replayed. Existing Wi-Fi credentials are preserved. As before,
`USER_OVERRIDE_NVS_ON_FLASH = 1` erases the entire NVS partition at boot; normally
leave it at zero.
