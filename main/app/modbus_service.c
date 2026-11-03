#include "modbus_service.h"

#include <string.h>

#include "sdkconfig.h"
#include "User_Settings.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#if CONFIG_USER_MODBUS_RTU && CONFIG_FMB_COMM_MODE_RTU_EN
#include "driver/uart.h"
#include "esp_modbus_master.h"
#endif

static const char *TAG = "modbus_service";
static portMUX_TYPE snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
static modbus_service_mode_t service_mode;
static modbus_point_state_t snapshots[GATEWAY_MAX_POINTS];
static bool pending[GATEWAY_MAX_POINTS];
static modbus_counters_t counters;
static QueueHandle_t write_queue;

#if CONFIG_USER_MODBUS_RTU && CONFIG_FMB_COMM_MODE_RTU_EN
static modbus_engine_t engine;
static mb_parameter_descriptor_t descriptors[GATEWAY_MAX_POINTS];
static void *master;
static TaskHandle_t service_task;

static uint64_t clock_ms(void *context)
{
    (void)context;
    return (uint64_t)esp_timer_get_time() / 1000U;
}

static modbus_result_t request_transfer(void *context, modbus_request_t *request)
{
    (void)context;
    mb_param_request_t params = {
        .slave_addr = request->slave,
        .command = request->function,
        .reg_start = request->address,
        .reg_size = request->quantity,
    };
    esp_err_t err = mbc_master_send_request(master, &params, &request->data);
    if (err == ESP_OK) {
        return MODBUS_OK;
    }
    ESP_LOGW(TAG, "Slave %u FC%02u address=%u count=%u: %s",
        request->slave, request->function, request->address, request->quantity,
        esp_err_to_name(err));
    switch (err) {
    case ESP_ERR_TIMEOUT: return MODBUS_TIMEOUT;
    case ESP_ERR_INVALID_RESPONSE: return MODBUS_BAD_RESPONSE;
    case ESP_FAIL:
    case ESP_ERR_NOT_SUPPORTED: return MODBUS_EXCEPTION;
    default: return MODBUS_IO_ERROR;
    }
}

static void publish(void)
{
    for (size_t i = 0; i < engine.count; ++i) {
        if (engine.state[i].last_result == MODBUS_INVALID_VALUE &&
            engine.state[i].reads_failed != snapshots[i].reads_failed) {
            ESP_LOGE(TAG, "Invalid decoded value: point=%u slave=%u address=%u",
                (unsigned)i, engine.points[i].slave_address, engine.points[i].address);
        }
    }
    portENTER_CRITICAL(&snapshot_lock);
    memcpy(snapshots, engine.state, engine.count * sizeof(snapshots[0]));
    counters = engine.counters;
    portEXIT_CRITICAL(&snapshot_lock);
}

static void worker(void *context)
{
    (void)context;
    uint64_t last_report = clock_ms(NULL);
    for (;;) {
        modbus_write_command_t command;
        if (xQueueReceive(write_queue, &command, 0) == pdTRUE) {
            modbus_result_t result = modbus_engine_write(&engine, &command);
            if (result != MODBUS_OK) {
                ESP_LOGW(TAG, "Write id=%lu point=%u count=%u failed: result=%u",
                    (unsigned long)command.id, (unsigned)command.first_point,
                    (unsigned)command.count, (unsigned)result);
            }
            /* Publish completion before allowing another command on these points. */
            portENTER_CRITICAL(&snapshot_lock);
            for (size_t i = 0; i < command.count; ++i) {
                size_t index = command.first_point + i;
                snapshots[index] = engine.state[index];
                pending[index] = false;
            }
            portEXIT_CRITICAL(&snapshot_lock);
        }
        modbus_engine_poll(&engine);
        publish();
        uint64_t now = clock_ms(NULL);
        if (now - last_report >= 30000) {
            modbus_service_status_t status;
            modbus_service_get_status(&status);
            ESP_LOGI(TAG, "Good=%u faulted=%u transactions=%lu errors=%lu timeouts=%lu writes=%lu/%lu",
                status.good_points, status.faulted_points,
                (unsigned long)status.counters.transactions,
                (unsigned long)status.counters.errors,
                (unsigned long)status.counters.timeouts,
                (unsigned long)status.counters.writes_ok,
                (unsigned long)status.counters.writes_failed);
            last_report = now;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static esp_err_t cleanup(esp_err_t original_error, bool started)
{
    ESP_LOGE(TAG, "Modbus service startup failed: %s", esp_err_to_name(original_error));
    if (master) {
        if (started) {
            esp_err_t err = mbc_master_stop(master);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Cannot stop failed Modbus startup: %s", esp_err_to_name(err));
            }
        }
        esp_err_t err = mbc_master_delete(master);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Cannot release Modbus controller: %s", esp_err_to_name(err));
        }
        master = NULL;
    }
    if (write_queue) {
        vQueueDelete(write_queue);
        write_queue = NULL;
    }
    return original_error;
}
#endif

esp_err_t modbus_service_start(void)
{
    gateway_mapping_error_t error;
    if (!gateway_mapping_validate(USER_POINT_MAPPINGS, USER_POINT_MAPPING_COUNT, &error)) {
        ESP_LOGE(TAG, "Invalid mapping %u: %s", (unsigned)error.index, error.reason);
        return ESP_ERR_INVALID_ARG;
    }
#if CONFIG_USER_MODBUS_RTU && CONFIG_FMB_COMM_MODE_RTU_EN
    if (service_task || master) {
        ESP_LOGE(TAG, "Modbus service already started");
        return ESP_ERR_INVALID_STATE;
    }
    if (USER_POINT_MAPPING_COUNT == 0) {
        portENTER_CRITICAL(&snapshot_lock);
        service_mode = MODBUS_SERVICE_IDLE;
        portEXIT_CRITICAL(&snapshot_lock);
        ESP_LOGI(TAG, "Empty point table: Modbus idle; no UART driver or traffic");
        return ESP_OK;
    }
    if (CONFIG_FMB_MASTER_MAX_API_BLOCKING_TIME_MS <
        CONFIG_USER_MODBUS_RESPONSE_TIMEOUT_MS + CONFIG_FMB_MASTER_TIMEOUT_COOLDOWN_MS + 1000) {
        ESP_LOGE(TAG, "Enable RTU and increase FMB master API blocking time to cover response timeout + cooldown + 1000 ms");
        return ESP_ERR_INVALID_ARG;
    }
    const modbus_engine_config_t config = {
        .retries = CONFIG_USER_MODBUS_RETRIES,
        .max_read_registers = CONFIG_USER_MODBUS_MAX_READ_REGISTERS,
        .max_read_bits = CONFIG_USER_MODBUS_MAX_READ_BITS,
        .backoff_max_ms = CONFIG_USER_MODBUS_BACKOFF_MAX_MS,
    };
    if (!modbus_engine_init(&engine, USER_POINT_MAPPINGS, USER_POINT_MAPPING_COUNT,
            &config, clock_ms, request_transfer, NULL)) {
        ESP_LOGE(TAG, "Invalid Modbus engine configuration");
        return ESP_ERR_INVALID_ARG;
    }
    write_queue = xQueueCreate(8, sizeof(modbus_write_command_t));
    if (!write_queue) {
        return cleanup(ESP_ERR_NO_MEM, false);
    }
#if CONFIG_USER_MODBUS_PARITY_EVEN
    const uart_parity_t parity = UART_PARITY_EVEN;
#elif CONFIG_USER_MODBUS_PARITY_ODD
    const uart_parity_t parity = UART_PARITY_ODD;
#else
    const uart_parity_t parity = UART_PARITY_DISABLE;
#endif
    mb_communication_info_t comm = {
        .ser_opts.port = CONFIG_USER_MODBUS_UART_PORT,
        .ser_opts.mode = MB_RTU,
        .ser_opts.baudrate = CONFIG_USER_MODBUS_BAUD_RATE,
        .ser_opts.parity = parity,
        .ser_opts.uid = 0,
        .ser_opts.response_tout_ms = CONFIG_USER_MODBUS_RESPONSE_TIMEOUT_MS,
        .ser_opts.data_bits = UART_DATA_8_BITS,
        .ser_opts.stop_bits = CONFIG_USER_MODBUS_STOP_BITS == 2 ?
            UART_STOP_BITS_2 : UART_STOP_BITS_1,
    };
    esp_err_t err = mbc_master_create_serial(&comm, &master);
    if (err != ESP_OK) {
        return cleanup(err, false);
    }
    /* ESP-Modbus 2.x requires a dictionary even when using raw requests only. */
    static const mb_param_type_t tables[] = {
        MB_PARAM_COIL, MB_PARAM_DISCRETE, MB_PARAM_INPUT, MB_PARAM_HOLDING
    };
    for (size_t i = 0; i < USER_POINT_MAPPING_COUNT; ++i) {
        const gateway_point_mapping_t *p = &USER_POINT_MAPPINGS[i];
        bool bits = p->register_type <= GATEWAY_DISCRETE_INPUT;
        unsigned width = gateway_mapping_register_count(p->data_type);
        descriptors[i] = (mb_parameter_descriptor_t) {
            .cid = (uint16_t)i,
            .param_key = p->name,
            .param_units = "",
            .mb_slave_addr = p->slave_address,
            .mb_param_type = tables[p->register_type],
            .mb_reg_start = p->address,
            .mb_size = (uint16_t)width,
            .param_type = bits ? PARAM_TYPE_U8 : PARAM_TYPE_U16,
            .param_size = bits ? 1 : width * sizeof(uint16_t),
            .access = p->access == GATEWAY_READ_WRITE ?
                PAR_PERMS_READ_WRITE : PAR_PERMS_READ,
        };
    }
    err = mbc_master_set_descriptor(master, descriptors, (uint16_t)USER_POINT_MAPPING_COUNT);
    if (err == ESP_OK) {
        err = uart_set_pin(CONFIG_USER_MODBUS_UART_PORT,
            CONFIG_USER_MODBUS_UART_TX_GPIO, CONFIG_USER_MODBUS_UART_RX_GPIO,
            CONFIG_USER_MODBUS_UART_DE_GPIO, UART_PIN_NO_CHANGE);
    }
    if (err == ESP_OK) {
        err = uart_set_mode(CONFIG_USER_MODBUS_UART_PORT, UART_MODE_RS485_HALF_DUPLEX);
    }
    if (err == ESP_OK) {
        err = mbc_master_start(master);
    }
    if (err != ESP_OK) {
        return cleanup(err, false);
    }
    if (xTaskCreate(worker, "modbus_service", 6144, NULL, 3, &service_task) != pdPASS) {
        return cleanup(ESP_ERR_NO_MEM, true);
    }
    portENTER_CRITICAL(&snapshot_lock);
    service_mode = MODBUS_SERVICE_RUNNING;
    portEXIT_CRITICAL(&snapshot_lock);
    ESP_LOGI(TAG, "RTU master started on UART%d; %u points; DE/RE driven by UART RTS",
        CONFIG_USER_MODBUS_UART_PORT, (unsigned)USER_POINT_MAPPING_COUNT);
    return ESP_OK;
#else
#if CONFIG_USER_MODBUS_RTU
    ESP_LOGE(TAG, "Modbus service requires enabling ESP-Modbus RTU support");
    return ESP_ERR_INVALID_STATE;
#else
    if (USER_POINT_MAPPING_COUNT) {
        ESP_LOGE(TAG, "Point mappings require enabling the Modbus RTU service");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "Modbus RTU disabled");
    return ESP_OK;
#endif
#endif
}

void modbus_service_get_status(modbus_service_status_t *status)
{
    if (!status) {
        ESP_LOGE(TAG, "NULL status output");
        return;
    }
    memset(status, 0, sizeof(*status));
    uint64_t now = (uint64_t)esp_timer_get_time() / 1000U;
    portENTER_CRITICAL(&snapshot_lock);
    status->mode = service_mode;
    status->counters = counters;
    for (size_t i = 0; i < USER_POINT_MAPPING_COUNT; ++i) {
        modbus_point_state_t state = snapshots[i];
        modbus_point_check_stale(&state, &USER_POINT_MAPPINGS[i], now);
        if (state.quality == MODBUS_POINT_GOOD) {
            ++status->good_points;
        } else {
            ++status->faulted_points;
        }
    }
    portEXIT_CRITICAL(&snapshot_lock);
}

esp_err_t modbus_service_get_point(
    size_t index, modbus_point_state_t *state, bool *write_pending)
{
    if (!state || !write_pending || index >= USER_POINT_MAPPING_COUNT) {
        ESP_LOGE(TAG, "Invalid point snapshot request: index=%u", (unsigned)index);
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&snapshot_lock);
    *state = snapshots[index];
    *write_pending = pending[index];
    portEXIT_CRITICAL(&snapshot_lock);
    modbus_point_check_stale(state, &USER_POINT_MAPPINGS[index],
        (uint64_t)esp_timer_get_time() / 1000U);
    return ESP_OK;
}

esp_err_t modbus_service_queue_write(const modbus_write_command_t *command)
{
    modbus_request_t request;
    if (!command || !command->id ||
        !modbus_build_write(USER_POINT_MAPPINGS, USER_POINT_MAPPING_COUNT, command, &request)) {
        ESP_LOGE(TAG, "Invalid Modbus write command, mapping, permission or value");
        return ESP_ERR_INVALID_ARG;
    }
    bool busy = false;
    portENTER_CRITICAL(&snapshot_lock);
    bool running = service_mode == MODBUS_SERVICE_RUNNING;
    for (size_t i = 0; i < command->count; ++i) {
        busy |= pending[command->first_point + i];
    }
    if (running && !busy) {
        for (size_t i = 0; i < command->count; ++i) {
            pending[command->first_point + i] = true;
        }
    }
    portEXIT_CRITICAL(&snapshot_lock);
    if (!running || busy) {
        ESP_LOGW(TAG, "Cannot queue write: %s", busy ? "point already pending" : "service not running");
        return ESP_ERR_INVALID_STATE;
    }
    if (xQueueSend(write_queue, command, 0) != pdTRUE) {
        portENTER_CRITICAL(&snapshot_lock);
        for (size_t i = 0; i < command->count; ++i) {
            pending[command->first_point + i] = false;
        }
        portEXIT_CRITICAL(&snapshot_lock);
        ESP_LOGW(TAG, "Modbus write queue is full");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
