"""Build the gateway bridge with real BACnet codecs/objects and mocked ESP services."""

import argparse
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]

ESP = r"""
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
enum { ESP_OK, ESP_FAIL, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE,
    ESP_ERR_NO_MEM, ESP_ERR_INVALID_SIZE, ESP_ERR_NVS_NOT_FOUND };
const char *esp_err_to_name(esp_err_t error);
void test_log(const char *tag, const char *format, ...);
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_LOGW(...) test_log(__VA_ARGS__)
#define ESP_LOGE(...) test_log(__VA_ARGS__)
typedef int nvs_handle_t;
enum { NVS_READONLY, NVS_READWRITE };
esp_err_t nvs_open(const char *, int, nvs_handle_t *);
esp_err_t nvs_get_str(nvs_handle_t, const char *, char *, size_t *);
esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *);
esp_err_t nvs_set_str(nvs_handle_t, const char *, const char *);
esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *, size_t);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", nargs="+", default=["gcc"])
    args = parser.parse_args()
    stack = ROOT / "components" / "bacnet-stack" / "src"
    with tempfile.TemporaryDirectory() as directory:
        temp = Path(directory)
        for header in ("esp_err.h", "esp_log.h", "nvs.h"):
            (temp / header).write_text('#include "test_esp.h"\n', encoding="ascii")
        (temp / "test_esp.h").write_text(ESP, encoding="ascii")
        (temp / "User_Settings.h").write_text(
            '#include "point_mapping.h"\n'
            'extern const gateway_point_mapping_t USER_POINT_MAPPINGS[];\n'
            'extern const size_t USER_POINT_MAPPING_COUNT;\n',
            encoding="ascii",
        )
        sources = list((stack / "bacnet").glob("*.c"))
        sources += [
            stack / "bacnet" / "basic" / "object" / f"{name}.c"
            for name in ("ai", "av", "bi", "bv")
        ]
        sources += [
            stack / "bacnet" / "basic" / "sys" / f"{name}.c"
            for name in ("keylist", "days", "debug")
        ]
        common = [
            "-std=c11", "-O2", "-ffunction-sections", "-fdata-sections",
            "-Wall", "-Wextra", "-Werror",
            "-Wno-deprecated-declarations", "-Wno-sign-compare",
            "-I", str(temp), "-I", str(stack),
            "-I", str(ROOT / "main" / "app"),
            "-I", str(ROOT / "main" / "bacnet"),
        ]
        for empty in (0, 1):
            executable = temp / f"mapped-{empty}.exe"
            command = args.compiler + common + [
                f"-DTEST_EMPTY={empty}",
                str(ROOT / "main" / "app" / "point_mapping.c"),
                str(ROOT / "main" / "app" / "modbus_engine.c"),
                str(ROOT / "main" / "bacnet" / "mapped_objects.c"),
                str(ROOT / "tools" / "test_mapped_objects.c"),
                *map(str, sources), "-Wl,--gc-sections", "-lm",
                "-o", str(executable),
            ]
            subprocess.run(command, check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
