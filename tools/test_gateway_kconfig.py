"""Check gateway hardware defaults using ESP-IDF's installed Kconfig parser."""

import os
from pathlib import Path
import tempfile
import unittest

import kconfiglib


ROOT = Path(__file__).resolve().parents[1]


class GatewayKconfigTests(unittest.TestCase):
    def setUp(self):
        self.previous_srctree = os.environ.get("srctree")
        os.environ["srctree"] = str(ROOT)
        self.directory = tempfile.TemporaryDirectory()
        root = Path(self.directory.name) / "Kconfig"
        root.write_text(
            'config IDF_TARGET_ESP32\n'
            '    bool\n'
            '    default y\n'
            'source "main/Kconfig.projbuild"\n',
            encoding="ascii",
        )
        self.config = kconfiglib.Kconfig(str(root), warn_to_stderr=False)

    def tearDown(self):
        self.directory.cleanup()
        if self.previous_srctree is None:
            os.environ.pop("srctree", None)
        else:
            os.environ["srctree"] = self.previous_srctree

    def select(self, profile):
        self.assertTrue(self.config.syms[profile].set_value("y"))

    def value(self, name):
        return self.config.syms[name].str_value

    def test_ideaspark_dual_uart_defaults(self):
        self.select("USER_DISPLAY_IDEASPARK")
        expected = {
            "USER_MODBUS_RTU": "y",
            "USER_MODBUS_UART_PORT": "1",
            "USER_MODBUS_UART_TX_GPIO": "25",
            "USER_MODBUS_UART_RX_GPIO": "26",
            "USER_MODBUS_UART_DE_GPIO": "33",
            "USER_MSTP_UART_PORT": "2",
            "USER_MSTP_UART_TX_GPIO": "17",
            "USER_MSTP_UART_RX_GPIO": "16",
            "USER_MSTP_UART_DE_GPIO": "5",
            "USER_MODBUS_BAUD_RATE": "9600",
            "USER_MODBUS_PARITY_EVEN": "y",
            "USER_MODBUS_STOP_BITS": "1",
            "USER_MODBUS_RESPONSE_TIMEOUT_MS": "1000",
            "USER_MODBUS_RETRIES": "2",
            "USER_MODBUS_POLL_INTERVAL_MS": "1000",
            "USER_MODBUS_MAX_READ_REGISTERS": "2",
            "USER_MODBUS_MAX_READ_BITS": "1",
            "USER_MODBUS_BACKOFF_MAX_MS": "30000",
        }
        for name, value in expected.items():
            with self.subTest(setting=name):
                self.assertEqual(self.value(name), value)
        pins = [int(self.value(name)) for name in expected if name.endswith("_GPIO")]
        self.assertEqual(len(pins), len(set(pins)))
        self.assertTrue(set(pins).isdisjoint({1, 2, 3, 4, 13, 14, 15, 18, 23, 27, 32}))

    def test_other_profiles_remain_selectable_without_modbus(self):
        for name in (
            "USER_DISPLAY_ST7796S", "USER_DISPLAY_ST7796S_TEST",
            "USER_DISPLAY_LVGL_TDISPLAY_S3", "USER_DISPLAY_ST7789_GMT020",
            "USER_DISPLAY_ST7789_HW657A", "USER_DISPLAY_LORA_GATEWAY",
            "USER_DISPLAY_NONE",
        ):
            with self.subTest(profile=name):
                self.select(name)
                self.assertEqual(self.value(name), "y")
                self.assertEqual(self.value("USER_MODBUS_RTU"), "n")

    def test_other_boards_require_explicit_modbus_pins(self):
        self.select("USER_DISPLAY_NONE")
        self.config.syms["USER_MODBUS_RTU"].set_value("y")
        for signal in ("TX", "RX", "DE"):
            self.assertEqual(self.value(f"USER_MODBUS_UART_{signal}_GPIO"), "-1")

    def test_sensor_options_are_removed(self):
        for name in (
            "USER_SENSOR_DS18B20", "USER_SEN54_I2C_PORT",
            "USER_SEN54_SDA_GPIO", "USER_SEN54_SCL_GPIO",
        ):
            self.assertNotIn(name, self.config.syms)

    def test_no_parity_defaults_to_two_stop_bits(self):
        self.select("USER_DISPLAY_IDEASPARK")
        self.select("USER_MODBUS_PARITY_NONE")
        self.assertEqual(self.value("USER_MODBUS_STOP_BITS"), "2")
        self.select("USER_MODBUS_PARITY_ODD")
        self.assertEqual(self.value("USER_MODBUS_STOP_BITS"), "1")

    def test_uart_zero_and_invalid_timing_are_not_effective(self):
        self.select("USER_DISPLAY_IDEASPARK")
        for name, invalid, default in (
            ("USER_MODBUS_UART_PORT", "0", "1"),
            ("USER_MSTP_UART_PORT", "0", "2"),
            ("USER_MODBUS_BAUD_RATE", "0", "9600"),
            ("USER_MODBUS_RESPONSE_TIMEOUT_MS", "0", "1000"),
            ("USER_MODBUS_RETRIES", "-1", "2"),
            ("USER_MODBUS_POLL_INTERVAL_MS", "0", "1000"),
            ("USER_MODBUS_MAX_READ_REGISTERS", "1", "2"),
            ("USER_MODBUS_MAX_READ_REGISTERS", "126", "2"),
            ("USER_MODBUS_MAX_READ_BITS", "0", "1"),
            ("USER_MODBUS_MAX_READ_BITS", "2001", "1"),
            ("USER_MODBUS_BACKOFF_MAX_MS", "999", "30000"),
            ("USER_MODBUS_BACKOFF_MAX_MS", "60001", "30000"),
        ):
            with self.subTest(setting=name):
                self.config.syms[name].set_value(invalid)
                self.assertEqual(self.value(name), default)

    def test_modbus_group_limits_and_backoff_are_configurable(self):
        self.select("USER_DISPLAY_IDEASPARK")
        for name, value in (
            ("USER_MODBUS_MAX_READ_REGISTERS", "125"),
            ("USER_MODBUS_MAX_READ_BITS", "2000"),
            ("USER_MODBUS_BACKOFF_MAX_MS", "60000"),
            ("USER_MODBUS_RETRIES", "0"),
        ):
            with self.subTest(setting=name):
                self.config.syms[name].set_value(value)
                self.assertEqual(self.value(name), value)

    def test_ideaspark_backlight_variants_do_not_use_serial_pins(self):
        self.select("USER_DISPLAY_IDEASPARK")
        for option, pin in (("GPIO32", "32"), ("GPIO27", "27")):
            self.select(f"USER_IDEASPARK_BACKLIGHT_{option}")
            self.assertEqual(self.value("USER_IDEASPARK_BACKLIGHT_GPIO"), pin)
            for bus in ("MODBUS", "MSTP"):
                for signal in ("TX", "RX", "DE"):
                    self.assertNotEqual(self.value(f"USER_{bus}_UART_{signal}_GPIO"), pin)


if __name__ == "__main__":
    unittest.main()
