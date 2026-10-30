#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GATEWAY_MAX_POINTS 64U
#define GATEWAY_NO_BIT (-1)

typedef enum {
    GATEWAY_COIL,
    GATEWAY_DISCRETE_INPUT,
    GATEWAY_INPUT_REGISTER,
    GATEWAY_HOLDING_REGISTER
} gateway_register_type_t;

typedef enum {
    GATEWAY_BOOL,
    GATEWAY_UINT16,
    GATEWAY_INT16,
    GATEWAY_UINT32,
    GATEWAY_INT32,
    GATEWAY_FLOAT32
} gateway_data_type_t;

typedef enum {
    GATEWAY_BYTES_BIG_ENDIAN,
    GATEWAY_BYTES_LITTLE_ENDIAN
} gateway_byte_order_t;

typedef enum {
    GATEWAY_HIGH_WORD_FIRST,
    GATEWAY_LOW_WORD_FIRST
} gateway_word_order_t;

typedef enum {
    GATEWAY_READ_ONLY,
    GATEWAY_READ_WRITE
} gateway_access_t;

typedef enum {
    GATEWAY_OBJECT_AI,
    GATEWAY_OBJECT_AV,
    GATEWAY_OBJECT_BI,
    GATEWAY_OBJECT_BV
} gateway_object_type_t;

typedef struct {
    uint8_t slave_address;
    gateway_register_type_t register_type;
    uint16_t address; /* Zero-based Modbus protocol address. */
    gateway_data_type_t data_type;
    gateway_byte_order_t byte_order;
    gateway_word_order_t word_order;
    int8_t bit_index; /* GATEWAY_NO_BIT, or 0..15 for a register bit. */
    float scale;
    float offset;
    gateway_access_t access;
    uint32_t poll_interval_ms;
    uint32_t stale_timeout_ms;
    gateway_object_type_t object_type;
    uint32_t instance;
    const char *name;
    const char *description;
    uint16_t units;
    float cov_increment;
    const char *active_text;
    const char *inactive_text;
} gateway_point_mapping_t;

typedef struct {
    size_t index;
    const char *reason;
} gateway_mapping_error_t;

/* A failed validation always supplies a diagnostic when error is non-NULL. */
bool gateway_mapping_validate(
    const gateway_point_mapping_t *points, size_t count,
    gateway_mapping_error_t *error);
const gateway_point_mapping_t *gateway_mapping_find(
    const gateway_point_mapping_t *points, size_t count,
    gateway_object_type_t type, uint32_t instance);
unsigned gateway_mapping_register_count(gateway_data_type_t type);

#ifdef __cplusplus
}
#endif
