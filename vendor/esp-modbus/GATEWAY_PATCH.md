# esp-modbus 2.1.4 — gateway patch

This directory is a copy of `espressif/esp-modbus` 2.1.4 (build tree only:
`modbus/`, `CMakeLists.txt`, `Kconfig`, `linker.lf`, manifest, license) selected
through `override_path` in `main/idf_component.yml`. It carries one additive
patch; everything else is upstream.

## Why

`mbc_master_send_request()` returns `ESP_ERR_INVALID_RESPONSE` for both a slave
exception response (`MB_EILLFUNC`) and a corrupted or mismatched frame
(`MB_ERECVDATA`), and discards the exception code. The gateway needs to tell
them apart: an exception is a protocol answer over a healthy link and must not
trigger communication-failure backoff.

## What (all additive, marked `Gateway patch`)

| File | Change |
|---|---|
| `modbus/mb_objects/mb_master.c` | `mbm_object_t` gains `last_exception` / `last_exception_is_remote`; set in `mbm_check_invoke_handler()` (remote for FC\|0x80 frames, local for handler rejections), cleared at `EV_FRAME_TRANSMIT`; new `mbm_get_last_exception()`. |
| `modbus/mb_objects/include/mb_master.h` | Prototype for `mbm_get_last_exception()`. |
| `modbus/mb_controller/common/mbc_master.h` | `mb_master_options_t` gains `last_mb_error`, `last_exception`, `last_exception_is_remote`. |
| `modbus/mb_controller/serial/mbc_serial_master.c`, `modbus/mb_controller/tcp/mbc_tcp_master.c` | `send_request` records the raw `mb_err_enum_t` (and the exception when `MB_EILLFUNC`) while still holding `mbm_sema`. |
| `modbus/mb_controller/common/include/esp_modbus_master.h` | Public `mb_master_last_error_t` and `mbc_master_get_last_error()`. |
| `modbus/mb_controller/common/esp_modbus_master.c` | Implementation of `mbc_master_get_last_error()`. |

## Semantics

Call `mbc_master_get_last_error()` right after `mbc_master_send_request()` from
the same task. `slave_exception` is true only for `MB_EILLFUNC` produced by a
well-formed exception response; `MB_EILLFUNC` from a locally rejected frame
(wrong byte count, unsupported function) reports `slave_exception == false`.

## Upgrading

Re-apply the rows above to the new upstream version, then update `version` in
`main/idf_component.yml` and this file's title.
