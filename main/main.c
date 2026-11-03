#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_storage.h"
#include "app_supervisor.h"
#include "bacnet_app.h"
#include "display.h"
#include "gateway_hardware.h"
#include "modbus_service.h"
#include "stack_profiler.h"
#include "User_Settings.h"
#include <string.h>

static TaskHandle_t bacnet_rx_task_handle;
static TaskHandle_t bacnet_mstp_rx_task_handle;
static TaskHandle_t bacnet_core_task_handle;
static TaskHandle_t bacnet_cov_task_handle;

void app_main(void)
{
    gateway_mapping_error_t mapping_error;
    if (!gateway_mapping_validate(
            USER_POINT_MAPPINGS, USER_POINT_MAPPING_COUNT, &mapping_error)) {
        ESP_LOGE("gateway", "Invalid point mapping %zu: %s",
            mapping_error.index, mapping_error.reason);
        ESP_ERROR_CHECK(ESP_ERR_INVALID_ARG);
    }
    for (size_t i = 0; i < USER_POINT_MAPPING_COUNT; ++i) {
        if (strcmp(USER_POINT_MAPPINGS[i].name, USER_BACNET_DEVICE_NAME) == 0) {
            ESP_LOGE("gateway", "Point mapping %zu duplicates the Device object name", i);
            ESP_ERROR_CHECK(ESP_ERR_INVALID_ARG);
        }
    }
    ESP_ERROR_CHECK(gateway_hardware_validate());
    ESP_ERROR_CHECK(app_storage_init());
    User_Settings_Print();

    const stack_profiler_task_handle_refs_t profiler_task_handles = {
        .bacnet_rx = &bacnet_rx_task_handle,
        .bacnet_mstp_rx = &bacnet_mstp_rx_task_handle,
        .bacnet_core = &bacnet_core_task_handle,
        .bacnet_cov = &bacnet_cov_task_handle,
    };
    stack_profiler_init(&profiler_task_handles);
    ESP_ERROR_CHECK(bacnet_app_init(stack_profiler_bacnet_callback));
    display_init();

    bacnet_app_task_handle_refs_t task_handles = {
        .bip_rx = &bacnet_rx_task_handle,
        .mstp_rx = &bacnet_mstp_rx_task_handle,
        .core = &bacnet_core_task_handle,
        .cov = &bacnet_cov_task_handle,
    };
    ESP_ERROR_CHECK(bacnet_app_start(&task_handles));
    ESP_ERROR_CHECK(modbus_service_start());
    app_supervisor_run();
}
