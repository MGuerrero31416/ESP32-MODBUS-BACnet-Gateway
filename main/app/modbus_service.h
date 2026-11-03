#pragma once

#include "esp_err.h"
#include "modbus_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MODBUS_SERVICE_DISABLED,
    MODBUS_SERVICE_IDLE,
    MODBUS_SERVICE_RUNNING
} modbus_service_mode_t;

typedef struct {
    modbus_service_mode_t mode;
    modbus_counters_t counters;
    unsigned good_points;
    unsigned faulted_points;
} modbus_service_status_t;

esp_err_t modbus_service_start(void);
void modbus_service_get_status(modbus_service_status_t *status);
esp_err_t modbus_service_get_point(size_t index,
    modbus_point_state_t *state, bool *write_pending);
/* Nonblocking acceptance only, not proof of execution. A nonzero id is required. */
esp_err_t modbus_service_queue_write(const modbus_write_command_t *command);

#ifdef __cplusplus
}
#endif
