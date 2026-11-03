"""Exercise the actual service against deterministic UART/RTOS/controller mocks."""

import argparse
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]

PLATFORM = r"""
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "modbus_engine.h"
typedef int esp_err_t;
enum { ESP_OK, ESP_FAIL, ESP_ERR_TIMEOUT, ESP_ERR_INVALID_RESPONSE,
    ESP_ERR_NOT_SUPPORTED, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE, ESP_ERR_NO_MEM };
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
typedef void *QueueHandle_t;
typedef void *TaskHandle_t;
typedef int uart_parity_t;
enum { UART_PARITY_DISABLE, UART_PARITY_EVEN, UART_PARITY_ODD, UART_DATA_8_BITS,
    UART_STOP_BITS_1, UART_STOP_BITS_2, UART_MODE_RS485_HALF_DUPLEX, UART_PIN_NO_CHANGE = -1 };
enum { MB_RTU, MB_PARAM_COIL, MB_PARAM_DISCRETE, MB_PARAM_INPUT, MB_PARAM_HOLDING,
    PARAM_TYPE_U8, PARAM_TYPE_U16, PAR_PERMS_READ, PAR_PERMS_READ_WRITE };
typedef int mb_param_type_t;
typedef struct {
    uint16_t cid; const char *param_key, *param_units;
    uint8_t mb_slave_addr; int mb_param_type; uint16_t mb_reg_start, mb_size;
    int param_type; size_t param_size; int access;
} mb_parameter_descriptor_t;
typedef struct {
    uint8_t slave_addr, command; uint16_t reg_start, reg_size;
} mb_param_request_t;
typedef struct {
    struct { int port, mode, baudrate, parity, uid, response_tout_ms, data_bits, stop_bits; } ser_opts;
} mb_communication_info_t;
extern const gateway_point_mapping_t USER_POINT_MAPPINGS[];
extern const size_t USER_POINT_MAPPING_COUNT;
int64_t esp_timer_get_time(void);
const char *esp_err_to_name(int);
void test_log(const char *, const char *, ...);
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_LOGW(...) test_log(__VA_ARGS__)
#define ESP_LOGE(...) test_log(__VA_ARGS__)
QueueHandle_t xQueueCreate(unsigned, size_t);
void vQueueDelete(QueueHandle_t);
int xQueueSend(QueueHandle_t, const void *, unsigned);
int xQueueReceive(QueueHandle_t, void *, unsigned);
int xTaskCreate(void (*)(void *), const char *, unsigned, void *, unsigned, TaskHandle_t *);
void vTaskDelay(unsigned);
esp_err_t mbc_master_create_serial(mb_communication_info_t *, void **);
esp_err_t mbc_master_set_descriptor(void *, const mb_parameter_descriptor_t *, uint16_t);
esp_err_t mbc_master_start(void *);
esp_err_t mbc_master_stop(void *);
esp_err_t mbc_master_delete(void *);
esp_err_t mbc_master_send_request(void *, mb_param_request_t *, void *);
esp_err_t uart_set_pin(int, int, int, int, int);
esp_err_t uart_set_mode(int, int);
"""

CONFIG = r"""
#pragma once
#if !TEST_DISABLED
#define CONFIG_USER_MODBUS_RTU 1
#endif
#if !TEST_NO_RTU
#define CONFIG_FMB_COMM_MODE_RTU_EN 1
#endif
#define CONFIG_USER_MODBUS_RETRIES 2
#define CONFIG_USER_MODBUS_MAX_READ_REGISTERS 2
#define CONFIG_USER_MODBUS_MAX_READ_BITS 1
#define CONFIG_USER_MODBUS_BACKOFF_MAX_MS 30000
#define CONFIG_FMB_MASTER_MAX_API_BLOCKING_TIME_MS 6000
#define CONFIG_USER_MODBUS_RESPONSE_TIMEOUT_MS 1000
#define CONFIG_FMB_MASTER_TIMEOUT_COOLDOWN_MS 150
#define CONFIG_USER_MODBUS_PARITY_EVEN 1
#define CONFIG_USER_MODBUS_UART_PORT 1
#define CONFIG_USER_MODBUS_BAUD_RATE 9600
#define CONFIG_USER_MODBUS_STOP_BITS 1
#define CONFIG_USER_MODBUS_UART_TX_GPIO 25
#define CONFIG_USER_MODBUS_UART_RX_GPIO 26
#define CONFIG_USER_MODBUS_UART_DE_GPIO 33
"""

HARNESS = r"""
#include "test_platform.h"
#include "modbus_service.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

const gateway_point_mapping_t USER_POINT_MAPPINGS[] = { MAPPING_ENTRIES };
const size_t USER_POINT_MAPPING_COUNT = TEST_EMPTY ? 0 : 10;
static modbus_write_command_t queued[8];
static unsigned head, tail, stage, fail_stage, releases, stops, transmissions;
static bool dictionary_ready, controller_started;
static uint64_t now;
static void (*task_fn)(void *);
static jmp_buf task_exit;
static int operation(void) { return ++stage == fail_stage ? ESP_FAIL : ESP_OK; }
int64_t esp_timer_get_time(void) { return (int64_t)now * 1000; }
const char *esp_err_to_name(int error) { (void)error; return "mock"; }
void test_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
QueueHandle_t xQueueCreate(unsigned depth, size_t size) {
    assert(depth == 8 && size == sizeof(queued[0]));
    return operation() == ESP_OK ? queued : NULL;
}
void vQueueDelete(QueueHandle_t q) { assert(q == queued); ++releases; }
int xQueueSend(QueueHandle_t q, const void *value, unsigned wait) {
    assert(q == queued && wait == 0);
    if (tail - head == 8) return 0;
    queued[tail++ % 8] = *(const modbus_write_command_t *)value;
    return pdTRUE;
}
int xQueueReceive(QueueHandle_t q, void *value, unsigned wait) {
    assert(q == queued && wait == 0);
    if (tail == head) return 0;
    *(modbus_write_command_t *)value = queued[head++ % 8];
    return pdTRUE;
}
int xTaskCreate(void (*fn)(void *), const char *name, unsigned stack,
    void *context, unsigned priority, TaskHandle_t *handle) {
    assert(fn && name && stack >= 6144 && context == NULL && priority == 3);
    if (operation() != ESP_OK) return 0;
    task_fn = fn; *handle = &task_fn; return pdPASS;
}
void vTaskDelay(unsigned delay) { assert(delay > 0); longjmp(task_exit, 1); }
esp_err_t mbc_master_create_serial(mb_communication_info_t *comm, void **master) {
    assert(comm->ser_opts.port == 1 && comm->ser_opts.baudrate == 9600 &&
        comm->ser_opts.parity == UART_PARITY_EVEN && comm->ser_opts.mode == MB_RTU);
    int result = operation();
    if (result == ESP_OK) *master = &stage;
    return result;
}
esp_err_t mbc_master_set_descriptor(void *master,
    const mb_parameter_descriptor_t *d, uint16_t count) {
    assert(master == &stage && count == 10);
    for (unsigned i = 0; i < count; ++i) {
        assert(d[i].cid == i && d[i].param_key && d[i].mb_size == 1 &&
            d[i].mb_reg_start == i && d[i].mb_slave_addr == 1 &&
            d[i].mb_param_type == MB_PARAM_HOLDING &&
            d[i].param_type == PARAM_TYPE_U16 && d[i].param_size == 2 &&
            d[i].access == PAR_PERMS_READ_WRITE);
    }
    int result = operation();
    dictionary_ready = result == ESP_OK;
    return result;
}
esp_err_t uart_set_pin(int port, int tx, int rx, int de, int cts) {
    assert(port == 1 && tx == 25 && rx == 26 && de == 33 && cts == UART_PIN_NO_CHANGE);
    return operation();
}
esp_err_t uart_set_mode(int port, int mode) {
    assert(port == 1 && mode == UART_MODE_RS485_HALF_DUPLEX); return operation();
}
esp_err_t mbc_master_start(void *master) {
    assert(master == &stage && dictionary_ready);
    int result = operation();
    controller_started = result == ESP_OK;
    return result;
}
esp_err_t mbc_master_stop(void *master) {
    assert(master == &stage && controller_started);
    ++stops; controller_started = false; return ESP_OK;
}
esp_err_t mbc_master_delete(void *master) {
    assert(master == &stage && !controller_started); ++releases; return ESP_OK;
}
esp_err_t mbc_master_send_request(void *master, mb_param_request_t *r, void *data) {
    assert(master == &stage && controller_started); ++transmissions;
    if (r->command == 3) {
        for (unsigned i = 0; i < r->reg_size; ++i) ((uint16_t *)data)[i] = 7;
    } else {
        assert(r->command == 6 && r->reg_size == 1 && ((uint16_t *)data)[0] == 77);
    }
    return ESP_OK;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    fail_stage = (unsigned)atoi(argv[1]);
    esp_err_t result = modbus_service_start();
    modbus_service_status_t status;
    modbus_service_get_status(&status);
    if (TEST_DISABLED || TEST_NO_RTU) {
        assert(result == (TEST_DISABLED && TEST_EMPTY ? ESP_OK : ESP_ERR_INVALID_STATE));
        assert(status.mode == MODBUS_SERVICE_DISABLED && stage == 0);
    } else if (TEST_EMPTY) {
        assert(result == ESP_OK && status.mode == MODBUS_SERVICE_IDLE && stage == 0);
    } else if (fail_stage) {
        assert(result != ESP_OK && status.mode == MODBUS_SERVICE_DISABLED && !task_fn);
        assert(stage == fail_stage && releases == (fail_stage == 1 ? 0 : fail_stage == 2 ? 1 : 2));
        assert(stops == (fail_stage == 7 ? 1U : 0U));
    } else {
        assert(result == ESP_OK && status.mode == MODBUS_SERVICE_RUNNING && task_fn);
        assert(modbus_service_start() == ESP_ERR_INVALID_STATE);
        modbus_write_command_t command = {.id = 0, .count = 1, .values = {77}};
        assert(modbus_service_queue_write(&command) == ESP_ERR_INVALID_ARG);
        command.id = 1;
        command.values[0] = 0.5;
        assert(modbus_service_queue_write(&command) == ESP_ERR_INVALID_ARG);
        command.values[0] = 77;
        for (unsigned i = 0; i < 8; ++i) {
            command.first_point = i; command.id = i + 1;
            assert(modbus_service_queue_write(&command) == ESP_OK);
        }
        assert(modbus_service_queue_write(&command) == ESP_ERR_INVALID_STATE);
        command.first_point = 8;
        assert(modbus_service_queue_write(&command) == ESP_ERR_NO_MEM);
        modbus_point_state_t state;
        bool pending;
        assert(modbus_service_get_point(8, &state, &pending) == ESP_OK && !pending);
        assert(modbus_service_get_point(0, &state, &pending) == ESP_OK && pending &&
            !state.write_completed && !state.acquired);
        assert(modbus_service_get_point(10, &state, &pending) == ESP_ERR_INVALID_ARG);
        if (setjmp(task_exit) == 0) task_fn(NULL);
        assert(modbus_service_get_point(0, &state, &pending) == ESP_OK && !pending &&
            state.write_completed && state.last_write_id == 1 &&
            state.last_write_result == MODBUS_OK && state.value == 7);
        assert(transmissions == 2);
        now = 5000;
        assert(modbus_service_get_point(0, &state, &pending) == ESP_OK &&
            state.quality == MODBUS_POINT_STALE);
        modbus_service_get_status(&status);
        assert(status.good_points == 0 && status.faulted_points == 10 &&
            status.counters.writes_ok == 1 && status.counters.transactions == 2);
        command.first_point = 0; command.id = 100;
        assert(modbus_service_queue_write(&command) == ESP_OK);
    }
    return 0;
}
"""


class ModbusServiceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        (cls.directory / "test_platform.h").write_text(PLATFORM, encoding="ascii")
        (cls.directory / "sdkconfig.h").write_text(CONFIG, encoding="ascii")
        for header in (
            "esp_err.h", "esp_log.h", "esp_timer.h", "User_Settings.h",
            "freertos/FreeRTOS.h", "freertos/queue.h", "freertos/task.h",
            "driver/uart.h", "esp_modbus_master.h",
        ):
            target = cls.directory / header
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text('#include "test_platform.h"\n', encoding="ascii")
        entries = ",\n".join(
            '{.slave_address=1, .register_type=GATEWAY_HOLDING_REGISTER, '
            f'.address={i}, .data_type=GATEWAY_UINT16, .bit_index=-1, .scale=1, '
            '.access=GATEWAY_READ_WRITE, .poll_interval_ms=1000, .stale_timeout_ms=5000, '
            f'.object_type=GATEWAY_OBJECT_AV, .instance={i}, .name="P{i}", '
            '.description="Test", .cov_increment=1}'
            for i in range(10)
        )
        (cls.directory / "harness.c").write_text(
            HARNESS.replace("MAPPING_ENTRIES", entries), encoding="ascii"
        )
        cls.executables = {}
        for name, empty, disabled, no_rtu in (
            ("running", 0, 0, 0), ("empty", 1, 0, 0),
            ("disabled", 0, 1, 0), ("disabled-empty", 1, 1, 0),
            ("no-rtu", 0, 0, 1),
        ):
            executable = cls.directory / f"{name}.exe"
            command = COMPILER + [
                "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                f"-DTEST_EMPTY={empty}", f"-DTEST_DISABLED={disabled}",
                f"-DTEST_NO_RTU={no_rtu}", "-I", str(cls.directory),
                "-I", str(ROOT / "main" / "app"),
                str(ROOT / "main" / "app" / "point_mapping.c"),
                str(ROOT / "main" / "app" / "modbus_engine.c"),
                str(ROOT / "main" / "app" / "modbus_service.c"),
                str(cls.directory / "harness.c"), "-lm", "-o", str(executable),
            ]
            subprocess.run(command, check=True)
            cls.executables[name] = executable

    def run_case(self, name, failure=0):
        subprocess.run([str(self.executables[name]), str(failure)], check=True)

    def test_queue_completion_and_snapshots(self):
        self.run_case("running")

    def test_empty_table_does_not_allocate_or_transmit(self):
        self.run_case("empty")

    def test_disabled_service_with_mappings_fails(self):
        self.run_case("disabled")

    def test_disabled_service_without_mappings_succeeds(self):
        self.run_case("disabled-empty")

    def test_library_rtu_disabled_fails_explicitly(self):
        self.run_case("no-rtu")

    def test_each_startup_failure_releases_resources(self):
        for stage in range(1, 8):
            with self.subTest(stage=stage):
                self.run_case("running", stage)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", nargs="+", default=["gcc"])
    COMPILER = parser.parse_args().compiler
    unittest.main(argv=[__file__])
