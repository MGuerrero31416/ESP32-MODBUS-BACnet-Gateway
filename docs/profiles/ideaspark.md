# Ideaspark

ESP32-WROOM-32 with a 1.9-inch ST7789 LCD, native 170x320 resolution.
The compact four-row UI is shared with HW657A and runs in 320x170 landscape.
It shows mapped point count, Wi-Fi state, recent MS/TP traffic, Modbus good/total
acquisition count (or No points/Disabled), and the BACnet device ID/MS/TP MAC.
There are no touch controls.

## Selection

Select the `esp32` target, then in menuconfig select:

```text
Project hardware
  Display profile
    Ideaspark - ESP32-WROOM-32 ST7789 1.9in 170x320
  Ideaspark backlight GPIO
    GPIO 32
```

The profile symbol is `CONFIG_USER_DISPLAY_IDEASPARK`.
Backlight defaults to GPIO32; select GPIO27 for revisions wired to that pin.
`CONFIG_USER_IDEASPARK_BACKLIGHT_GPIO` is derived from this choice.
The backlight is active high.

## Display wiring

| Signal | GPIO |
|---|---:|
| MOSI / SDA | 23 |
| SCLK / SCL | 18 |
| CS | 15 |
| DC | 2 |
| RST | 4 |
| BLK | 32 (or selectable 27) |
| MISO | Not used |

SPI runs at 20 MHz with BGR colour order and inversion enabled.
Check orientation, colours, and backlight on the actual board after building.

## Other hardware defaults

| Setting | Value |
|---|---|
| Sensor acquisition | Removed |
| Touch | Disabled |
| MS/TP TX / RX / DE | GPIO17 / GPIO16 / GPIO5 |

## Dual-RS485 gateway allocation

The ESP32 target defaults to this profile for new configurations. Existing
`sdkconfig` selections are preserved. Other board/display profiles remain
available.

| Interface | UART | ESP TX to converter DI | Converter RO to ESP RX | ESP direction to DE + /RE |
|---|---:|---:|---:|---:|
| BACnet MS/TP | 2 | GPIO17 | GPIO16 | GPIO5 |
| Modbus RTU | 1 | GPIO25 | GPIO26 | GPIO33 |
| USB-to-UART console | 0 | GPIO1 | GPIO3 | Not applicable |

These are two separate RS485 buses: never join their A/B terminals together.
Modbus defaults to 9600 baud, 8 data bits, even parity, and one stop bit (8E1).
Response timeout is 1000 ms, with two retries and a default polling interval of
1000 ms. Match these settings to the connected Modbus devices. No-parity Modbus
normally uses two stop bits; changing parity in an existing configuration may
require updating the stored stop-bit selection manually.

MS/TP remains 8N1 with baud, MAC, Max Master, and Max Info Frames in
`main/User_Settings.c`; its default baud is 38400.

In menuconfig, use `Project hardware` for the MS/TP UART/GPIO settings and
`Project hardware > Modbus RTU hardware` for Modbus settings. Modbus hardware
configuration defaults on for Ideaspark and off for other profiles. On other
profiles, select free Modbus GPIOs explicitly; no unverified pin defaults are
assigned. Startup validates pin capabilities, pin sharing, display/touch,
memory and console reservations before initializing buses.
The DE/RE convention is high to transmit, low to receive.

**Conversion stage:** the compiled point table now defines BACnet objects.
Its active configuration is empty until a register map is supplied, so no Modbus
driver or polling task starts by default. With mappings, the dedicated worker
polls using UART RTS for DE/RE control; the existing BACnet dispatcher publishes
snapshots/reliability and queues permitted writes. Sensor tasks and sensor role indexes
have been removed; GPIO13/14 are no longer reserved for sensor I2C.
No hardware commissioning has been performed.

### Electrical requirements

- Use ESP32-compatible 3.3 V logic transceivers (for example, suitable
  MAX3485-based modules). Do not connect a 5 V MAX485 receiver output directly
  to the ESP32. Verify the actual module supply and logic specifications.
- This allocation assumes manual direction control with DE and active-low RE
  tied together. Auto-direction modules need a different wiring/driver policy.
- Connect A/B to the corresponding bus pair; vendor A/B labels can differ.
- Use a daisy-chain twisted pair, with termination only at each physical end
  of each bus, matched to the cable (typically 120 ohms).
- Coordinate biasing with existing bus equipment; do not enable biasing on
  every converter.
- Provide the required signal reference/common according to the transceiver
  design. Prefer isolated transceivers and isolated power where ground
  potentials differ; do not bridge isolation with an unintended ground wire.
- GPIO5 is a boot-strapping pin. Retained MS/TP wiring must not force an
  unsuitable reset level. GPIO2/15 are already used by the LCD; the new Modbus
  allocation deliberately avoids boot-strapping pins.
- Confirm the board revision and accessible header pins before connecting.
  Build validation does not replace checking the actual board and converter.

GPIO4 is reserved for LCD reset.
GPIO2 and GPIO15 are ESP32 boot-strapping pins: avoid external circuitry
driving them during reset. If UART download mode fails, disconnect the LCD
DC or display power while entering download mode.

## Source files

- [Shared compact UI](../../main/ui/profiles/display_st7789_hw657a.cpp)
- [Ideaspark TFT setup](../../components/TFT_eSPI/User_Setups/Setup_Project_ST7789_Ideaspark.h)

After changing the profile or backlight choice, perform a clean build as
described in [Build Instructions](../BUILD_INSTRUCTIONS.md).
Building does not require flashing the device.
