#include "point_mapping.h"

#include <math.h>
#include <string.h>

static bool invalid(
    gateway_mapping_error_t *error, size_t index, const char *reason)
{
    if (error) {
        error->index = index;
        error->reason = reason;
    }
    return false;
}

unsigned gateway_mapping_register_count(gateway_data_type_t type)
{
    switch (type) {
    case GATEWAY_BOOL:
    case GATEWAY_UINT16:
    case GATEWAY_INT16:
        return 1;
    case GATEWAY_UINT32:
    case GATEWAY_INT32:
    case GATEWAY_FLOAT32:
        return 2;
    default:
        return 0;
    }
}

const gateway_point_mapping_t *gateway_mapping_find(
    const gateway_point_mapping_t *points, size_t count,
    gateway_object_type_t type, uint32_t instance)
{
    for (size_t i = 0; i < count; ++i) {
        if (points[i].object_type == type && points[i].instance == instance) {
            return &points[i];
        }
    }
    return NULL;
}

static bool valid_text(const char *text, size_t maximum, bool nonempty)
{
    return text && (!nonempty || text[0]) && strlen(text) <= maximum;
}

bool gateway_mapping_validate(
    const gateway_point_mapping_t *points, size_t count,
    gateway_mapping_error_t *error)
{
    if (error) {
        error->index = 0;
        error->reason = NULL;
    }
    if (count > GATEWAY_MAX_POINTS || (count && !points)) {
        return invalid(error, 0, "table is NULL or exceeds GATEWAY_MAX_POINTS");
    }
    for (size_t i = 0; i < count; ++i) {
        const gateway_point_mapping_t *p = &points[i];
        unsigned width = gateway_mapping_register_count(p->data_type);
        bool binary = p->data_type == GATEWAY_BOOL;
        bool bit_table = p->register_type == GATEWAY_COIL ||
            p->register_type == GATEWAY_DISCRETE_INPUT;
        bool writable_table = p->register_type == GATEWAY_COIL ||
            p->register_type == GATEWAY_HOLDING_REGISTER;
        if (p->slave_address < 1 || p->slave_address > 247) {
            return invalid(error, i, "slave address must be 1..247 (no broadcast)");
        }
        if (p->register_type < GATEWAY_COIL ||
            p->register_type > GATEWAY_HOLDING_REGISTER || !width) {
            return invalid(error, i, "unsupported register or data type");
        }
        if ((uint32_t)p->address + width > 65536U) {
            return invalid(error, i, "value extends beyond address 65535");
        }
        if (p->byte_order < GATEWAY_BYTES_BIG_ENDIAN ||
            p->byte_order > GATEWAY_BYTES_LITTLE_ENDIAN ||
            p->word_order < GATEWAY_HIGH_WORD_FIRST ||
            p->word_order > GATEWAY_LOW_WORD_FIRST) {
            return invalid(error, i, "unsupported byte or word order");
        }
        if (p->access < GATEWAY_READ_ONLY || p->access > GATEWAY_READ_WRITE ||
            (p->access == GATEWAY_READ_WRITE && !writable_table)) {
            return invalid(error, i, "write access requires coils or holding registers");
        }
        if ((bit_table && (!binary || p->bit_index != GATEWAY_NO_BIT)) ||
            (!bit_table && binary && (p->bit_index < 0 || p->bit_index > 15)) ||
            (!binary && p->bit_index != GATEWAY_NO_BIT)) {
            return invalid(error, i, "incompatible Boolean, register or bit selection");
        }
        if (binary && !bit_table && p->access == GATEWAY_READ_WRITE) {
            return invalid(error, i, "register-bit writes are unsupported (unsafe read-modify-write)");
        }
        if (!isfinite(p->scale) || p->scale == 0.0f || !isfinite(p->offset) ||
            (binary && (p->scale != 1.0f || p->offset != 0.0f))) {
            return invalid(error, i, "scale/offset must be finite; Boolean requires 1/0");
        }
        if (p->poll_interval_ms < 100 || p->poll_interval_ms > 3600000 ||
            p->stale_timeout_ms < p->poll_interval_ms ||
            p->stale_timeout_ms > 86400000) {
            return invalid(error, i, "invalid poll interval or stale timeout");
        }
        if (p->object_type < GATEWAY_OBJECT_AI || p->object_type > GATEWAY_OBJECT_BV ||
            (binary != (p->object_type == GATEWAY_OBJECT_BI ||
                        p->object_type == GATEWAY_OBJECT_BV)) ||
            (p->access == GATEWAY_READ_WRITE &&
             p->object_type != GATEWAY_OBJECT_AV && p->object_type != GATEWAY_OBJECT_BV)) {
            return invalid(error, i, "incompatible BACnet object type or write access");
        }
        if (p->instance >= 4194303U ||
            !valid_text(p->name, 64, true) ||
            !valid_text(p->description, 128, false) ||
            !isfinite(p->cov_increment) ||
            (binary ? p->cov_increment != 0.0f : p->cov_increment <= 0.0f) ||
            (binary && (!valid_text(p->active_text, 64, true) ||
                        !valid_text(p->inactive_text, 64, true)))) {
            return invalid(error, i, "invalid BACnet instance, text or COV increment");
        }
        for (size_t j = 0; j < i; ++j) {
            if (p->object_type == points[j].object_type &&
                p->instance == points[j].instance) {
                return invalid(error, i, "duplicate BACnet object type/instance");
            }
            if (strcmp(p->name, points[j].name) == 0) {
                return invalid(error, i, "duplicate BACnet object name");
            }
            unsigned other_width = gateway_mapping_register_count(points[j].data_type);
            if (p->slave_address == points[j].slave_address &&
                p->register_type == points[j].register_type &&
                p->address < (uint32_t)points[j].address + other_width &&
                points[j].address < (uint32_t)p->address + width &&
                (p->access == GATEWAY_READ_WRITE ||
                 points[j].access == GATEWAY_READ_WRITE)) {
                return invalid(error, i, "overlapping mappings include a writable point");
            }
        }
    }
    return true;
}
