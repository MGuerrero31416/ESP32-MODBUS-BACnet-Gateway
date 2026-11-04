#pragma once

#include "point_mapping.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MODBUS_MAX_READ_REGISTERS 125U
#define MODBUS_MAX_READ_BITS 2000U
#define MODBUS_MAX_WRITE_POINTS 16U

typedef enum {
    MODBUS_OK,
    MODBUS_TIMEOUT,
    MODBUS_BAD_RESPONSE,
    MODBUS_EXCEPTION,
    MODBUS_IO_ERROR,
    MODBUS_INVALID_VALUE,
    MODBUS_OFFLINE
} modbus_result_t;

typedef enum {
    MODBUS_POINT_UNACQUIRED,
    MODBUS_POINT_GOOD,
    MODBUS_POINT_STALE,
    MODBUS_POINT_ERROR
} modbus_quality_t;

/* Numeric words are host-endian register values. Bits are packed LSB first. */
typedef union {
    uint16_t words[MODBUS_MAX_READ_REGISTERS];
    uint8_t bits[(MODBUS_MAX_READ_BITS + 7U) / 8U];
} modbus_payload_t;

typedef struct {
    uint8_t slave;
    uint8_t function;
    uint16_t address;
    uint16_t quantity;
    modbus_payload_t data;
} modbus_request_t;

typedef struct {
    double value;
    bool acquired;
    modbus_quality_t quality;
    uint64_t last_success_ms;
    uint64_t next_poll_ms;
    modbus_result_t last_result;
    uint32_t reads_ok;
    uint32_t reads_failed;
    uint32_t last_write_id;
    modbus_result_t last_write_result;
    bool write_completed;
} modbus_point_state_t;

typedef struct {
    uint64_t next_probe_ms;
    uint8_t failures;
    bool single_reads;
} modbus_slave_state_t;

typedef struct {
    unsigned retries;
    unsigned max_read_registers;
    unsigned max_read_bits;
    uint32_t backoff_max_ms;
} modbus_engine_config_t;

typedef struct {
    uint32_t transactions;
    uint32_t timeouts;
    uint32_t errors;
    uint32_t exceptions; /* Slave exception responses; included in errors. */
    uint32_t writes_ok;
    uint32_t writes_failed;
} modbus_counters_t;

typedef uint64_t (*modbus_clock_fn)(void *context);
typedef modbus_result_t (*modbus_transfer_fn)(
    void *context, modbus_request_t *request);

typedef struct {
    const gateway_point_mapping_t *points;
    size_t count;
    modbus_point_state_t state[GATEWAY_MAX_POINTS];
    modbus_slave_state_t slaves[248];
    modbus_engine_config_t config;
    modbus_counters_t counters;
    size_t cursor;
    modbus_clock_fn clock;
    modbus_transfer_fn transfer;
    void *context;
} modbus_engine_t;

typedef struct {
    size_t first_point;
    size_t count; /* Consecutive table entries with contiguous wire addresses. */
    double values[MODBUS_MAX_WRITE_POINTS];
    uint32_t id;
} modbus_write_command_t;

bool modbus_decode_value(
    const gateway_point_mapping_t *point, const uint16_t words[2], double *value);
bool modbus_encode_value(
    const gateway_point_mapping_t *point, double value, uint16_t words[2]);
bool modbus_engine_init(modbus_engine_t *engine,
    const gateway_point_mapping_t *points, size_t count,
    const modbus_engine_config_t *config,
    modbus_clock_fn clock, modbus_transfer_fn transfer, void *context);
bool modbus_engine_poll(modbus_engine_t *engine);
bool modbus_build_write(const gateway_point_mapping_t *points, size_t count,
    const modbus_write_command_t *command, modbus_request_t *request);
modbus_result_t modbus_engine_write(
    modbus_engine_t *engine, const modbus_write_command_t *command);
void modbus_point_check_stale(modbus_point_state_t *state,
    const gateway_point_mapping_t *point, uint64_t now_ms);

#ifdef __cplusplus
}
#endif
