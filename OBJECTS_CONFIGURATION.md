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

Modbus polling and decoding will be implemented in step 3. BACnet Present_Value
writes are explicitly rejected for now, including while Out_Of_Service:
read-only points return write-access-denied; configured writable points return
optional-functionality-not-supported until write-through exists. The guard is
installed in the Device object dispatch table, so both BACnet transports use it.

This step does not implement BACnet priority arrays, command readback, runtime
mapping editing, or a full gateway UI. Ideaspark/HW657A show mapping count and
link state, with Modbus shown as not active. Other hardware profiles remain
selectable; their complete gateway layouts are a later conversion step.

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
