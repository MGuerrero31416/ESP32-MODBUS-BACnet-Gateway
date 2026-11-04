#include "modbus_engine.h"

#include <float.h>
#include <math.h>
#include <string.h>

_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 &&
    FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128, "Modbus float32 requires IEEE 754");

static uint16_t ordered_word(const gateway_point_mapping_t *p, uint16_t word)
{
    return p->byte_order == GATEWAY_BYTES_LITTLE_ENDIAN ?
        (uint16_t)((word >> 8) | (word << 8)) : word;
}

bool modbus_decode_value(
    const gateway_point_mapping_t *p, const uint16_t words[2], double *value)
{
    if (!p || !words || !value) {
        return false;
    }
    uint32_t raw = ordered_word(p, words[0]);
    if (gateway_mapping_register_count(p->data_type) == 2) {
        uint32_t second = ordered_word(p, words[1]);
        raw = p->word_order == GATEWAY_LOW_WORD_FIRST ?
            (second << 16) | raw : (raw << 16) | second;
    }
    double decoded;
    switch (p->data_type) {
    case GATEWAY_BOOL:
        if (p->register_type == GATEWAY_COIL ||
            p->register_type == GATEWAY_DISCRETE_INPUT) {
            decoded = words[0] != 0;
        } else {
            if (p->bit_index < 0 || p->bit_index > 15) {
                return false;
            }
            decoded = (raw >> p->bit_index) & 1U;
        }
        break;
    case GATEWAY_UINT16: decoded = raw; break;
    case GATEWAY_INT16: decoded = raw & 0x8000U ? (double)raw - 65536.0 : raw; break;
    case GATEWAY_UINT32: decoded = raw; break;
    case GATEWAY_INT32: decoded = raw & 0x80000000U ?
        (double)raw - 4294967296.0 : raw; break;
    case GATEWAY_FLOAT32: {
        float number;
        memcpy(&number, &raw, sizeof(number));
        decoded = number;
        break;
    }
    default: return false;
    }
    *value = decoded * (double)p->scale + p->offset;
    return isfinite(*value);
}

bool modbus_encode_value(
    const gateway_point_mapping_t *p, double value, uint16_t words[2])
{
    if (!p || !words || !isfinite(value) ||
        !isfinite(p->scale) || p->scale == 0 || !isfinite(p->offset)) {
        return false;
    }
    if (p->data_type == GATEWAY_BOOL) {
        if (p->register_type != GATEWAY_COIL || (value != 0 && value != 1)) {
            return false;
        }
        words[0] = (uint16_t)value;
        words[1] = 0;
        return true;
    }
    double raw = (value - p->offset) / p->scale;
    if (!isfinite(raw)) {
        return false;
    }
    uint32_t bits;
    if (p->data_type == GATEWAY_FLOAT32) {
        if (fabs(raw) > FLT_MAX) {
            return false;
        }
        float number = (float)raw;
        memcpy(&bits, &number, sizeof(bits));
    } else {
        double low, high;
        switch (p->data_type) {
        case GATEWAY_UINT16: low = 0; high = 65535; break;
        case GATEWAY_INT16: low = -32768; high = 32767; break;
        case GATEWAY_UINT32: low = 0; high = 4294967295.0; break;
        case GATEWAY_INT32: low = -2147483648.0; high = 2147483647.0; break;
        default: return false;
        }
        double integral = round(raw);
        /* Permit float32 engineering-unit roundoff, not arbitrary truncation. */
        double tolerance = fmin(0.001, 4.0 * FLT_EPSILON * fmax(1.0, fabs(raw)));
        if (fabs(raw - integral) > tolerance || integral < low || integral > high) {
            return false;
        }
        bits = integral < 0 ? (uint32_t)(integral + 4294967296.0) : (uint32_t)integral;
    }
    unsigned width = gateway_mapping_register_count(p->data_type);
    uint16_t high_word = (uint16_t)(bits >> 16), low_word = (uint16_t)bits;
    words[0] = ordered_word(p, width == 2 && p->word_order == GATEWAY_HIGH_WORD_FIRST ?
        high_word : low_word);
    words[1] = ordered_word(p, p->word_order == GATEWAY_HIGH_WORD_FIRST ?
        low_word : high_word);
    return true;
}

bool modbus_engine_init(modbus_engine_t *e,
    const gateway_point_mapping_t *points, size_t count,
    const modbus_engine_config_t *config,
    modbus_clock_fn clock, modbus_transfer_fn transfer, void *context)
{
    if (!e || !config || !clock || !transfer ||
        !gateway_mapping_validate(points, count, NULL) ||
        config->retries > 10 || config->max_read_registers < 2 ||
        config->max_read_registers > MODBUS_MAX_READ_REGISTERS ||
        config->max_read_bits < 1 || config->max_read_bits > MODBUS_MAX_READ_BITS ||
        config->backoff_max_ms < 1000 || config->backoff_max_ms > 60000) {
        return false;
    }
    memset(e, 0, sizeof(*e));
    e->points = points;
    e->count = count;
    e->config = *config;
    e->clock = clock;
    e->transfer = transfer;
    e->context = context;
    return true;
}

void modbus_point_check_stale(modbus_point_state_t *s,
    const gateway_point_mapping_t *p, uint64_t now)
{
    if (s->acquired && s->quality == MODBUS_POINT_GOOD && now >= s->last_success_ms &&
        now - s->last_success_ms >= p->stale_timeout_ms) {
        s->quality = MODBUS_POINT_STALE;
    }
}

static modbus_result_t transfer(modbus_engine_t *e, modbus_request_t *request)
{
    modbus_result_t result = e->transfer(e->context, request);
    ++e->counters.transactions;
    if (result != MODBUS_OK) {
        ++e->counters.errors;
        if (result == MODBUS_TIMEOUT) {
            ++e->counters.timeouts;
        } else if (result == MODBUS_EXCEPTION) {
            ++e->counters.exceptions;
        }
    }
    return result;
}

static void slave_result(modbus_engine_t *e, uint8_t slave, modbus_result_t result)
{
    modbus_slave_state_t *s = &e->slaves[slave];
    if (result == MODBUS_OK) {
        s->failures = 0;
        s->next_probe_ms = 0;
    } else if (result == MODBUS_TIMEOUT || result == MODBUS_IO_ERROR ||
               result == MODBUS_BAD_RESPONSE) {
        if (s->failures < 7) {
            ++s->failures;
        }
        uint32_t delay = 1000U << (s->failures - 1);
        if (delay > e->config.backoff_max_ms) {
            delay = e->config.backoff_max_ms;
        }
        s->next_probe_ms = e->clock(e->context) + delay;
    }
}

static bool due(const modbus_engine_t *e, size_t index, uint64_t now)
{
    const gateway_point_mapping_t *p = &e->points[index];
    return e->state[index].next_poll_ms <= now &&
        e->slaves[p->slave_address].next_probe_ms <= now;
}

bool modbus_engine_poll(modbus_engine_t *e)
{
    uint64_t now = e->clock(e->context);
    size_t first = e->count;
    for (size_t i = 0; i < e->count; ++i) {
        modbus_point_check_stale(&e->state[i], &e->points[i], now);
        size_t candidate = (e->cursor + i) % e->count;
        if (first == e->count && due(e, candidate, now)) {
            first = candidate;
        }
    }
    if (first == e->count) {
        return false;
    }
    const gateway_point_mapping_t *p = &e->points[first];
    bool bits = p->register_type <= GATEWAY_DISCRETE_INPUT;
    static const uint8_t functions[] = {1, 2, 4, 3};
    modbus_request_t request = {
        .slave = p->slave_address,
        .function = functions[p->register_type],
        .address = p->address,
        .quantity = (uint16_t)gateway_mapping_register_count(p->data_type),
    };
    bool selected[GATEWAY_MAX_POINTS] = {false};
    selected[first] = true;
    unsigned limit = bits ? e->config.max_read_bits : e->config.max_read_registers;
    if (!e->slaves[p->slave_address].single_reads) {
        bool extended;
        do {
            extended = false;
            for (size_t i = 0; i < e->count; ++i) {
                const gateway_point_mapping_t *q = &e->points[i];
                unsigned width = gateway_mapping_register_count(q->data_type);
                uint32_t end = (uint32_t)q->address + width;
                if (!selected[i] && due(e, i, now) &&
                    q->slave_address == p->slave_address &&
                    q->register_type == p->register_type &&
                    q->poll_interval_ms == p->poll_interval_ms &&
                    q->address >= request.address &&
                    q->address <= (uint32_t)request.address + request.quantity &&
                    end - request.address <= limit) {
                    selected[i] = true;
                    if (end > (uint32_t)request.address + request.quantity) {
                        request.quantity = (uint16_t)(end - request.address);
                    }
                    extended = true;
                }
            }
        } while (extended);
    }
    modbus_result_t result;
    unsigned attempt = 0;
    do {
        memset(&request.data, 0, sizeof(request.data));
        result = transfer(e, &request);
        if (result != MODBUS_TIMEOUT && result != MODBUS_BAD_RESPONSE) {
            break;
        }
    } while (attempt++ < e->config.retries);
    slave_result(e, p->slave_address, result);
    if (result == MODBUS_EXCEPTION &&
        request.quantity > gateway_mapping_register_count(p->data_type)) {
        /* Some devices prohibit otherwise legal grouped reads. */
        e->slaves[p->slave_address].single_reads = true;
    }
    now = e->clock(e->context);
    for (size_t i = 0; i < e->count; ++i) {
        if (!selected[i]) {
            continue;
        }
        const gateway_point_mapping_t *q = &e->points[i];
        modbus_point_state_t *s = &e->state[i];
        s->next_poll_ms = now + q->poll_interval_ms;
        s->last_result = result;
        double value;
        unsigned offset = q->address - request.address;
        uint16_t words[2] = {0};
        if (result == MODBUS_OK) {
            if (bits) {
                words[0] = (request.data.bits[offset / 8] >> (offset % 8)) & 1U;
            } else {
                words[0] = request.data.words[offset];
                if (gateway_mapping_register_count(q->data_type) == 2) {
                    words[1] = request.data.words[offset + 1];
                }
            }
        }
        if (result == MODBUS_OK && modbus_decode_value(q, words, &value)) {
            s->value = value;
            s->acquired = true;
            s->quality = MODBUS_POINT_GOOD;
            s->last_success_ms = now;
            ++s->reads_ok;
        } else {
            s->quality = MODBUS_POINT_ERROR;
            if (result == MODBUS_OK) {
                s->last_result = MODBUS_INVALID_VALUE;
            }
            ++s->reads_failed;
        }
    }
    e->cursor = (first + 1) % e->count;
    return true;
}

bool modbus_build_write(const gateway_point_mapping_t *points, size_t count,
    const modbus_write_command_t *command, modbus_request_t *request)
{
    if (!points || !command || !request || !command->count ||
        command->count > MODBUS_MAX_WRITE_POINTS || command->first_point >= count ||
        command->count > count - command->first_point) {
        return false;
    }
    memset(request, 0, sizeof(*request));
    const gateway_point_mapping_t *first = &points[command->first_point];
    request->slave = first->slave_address;
    request->address = first->address;
    bool coils = first->register_type == GATEWAY_COIL;
    if (!coils && first->register_type != GATEWAY_HOLDING_REGISTER) {
        return false;
    }
    for (size_t i = 0; i < command->count; ++i) {
        const gateway_point_mapping_t *p = &points[command->first_point + i];
        uint16_t words[2];
        if (p->access != GATEWAY_READ_WRITE ||
            p->slave_address != request->slave ||
            p->register_type != first->register_type ||
            p->address != (uint32_t)request->address + request->quantity ||
            !modbus_encode_value(p, command->values[i], words)) {
            return false;
        }
        if (coils) {
            if (words[0]) {
                request->data.bits[i / 8] |= (uint8_t)(1U << (i % 8));
            }
        } else {
            request->data.words[request->quantity] = words[0];
            if (gateway_mapping_register_count(p->data_type) == 2) {
                request->data.words[request->quantity + 1] = words[1];
            }
        }
        request->quantity += (uint16_t)gateway_mapping_register_count(p->data_type);
    }
    if (coils) {
        request->function = request->quantity == 1 ? 5 : 15;
        if (request->function == 5) {
            request->data.words[0] = command->values[0] ? 0xFF00 : 0;
        }
    } else {
        request->function = request->quantity == 1 ? 6 : 16;
    }
    return true;
}

modbus_result_t modbus_engine_write(
    modbus_engine_t *e, const modbus_write_command_t *command)
{
    modbus_request_t request;
    if (!modbus_build_write(e->points, e->count, command, &request)) {
        return MODBUS_INVALID_VALUE;
    }
    modbus_result_t result = MODBUS_OFFLINE;
    if (e->slaves[request.slave].next_probe_ms <= e->clock(e->context)) {
        /* A timeout may mean the write applied: never blindly replay a command. */
        result = transfer(e, &request);
        slave_result(e, request.slave, result);
    }
    if (result == MODBUS_OK) {
        ++e->counters.writes_ok;
    } else {
        ++e->counters.writes_failed;
    }
    for (size_t i = 0; i < command->count; ++i) {
        modbus_point_state_t *s = &e->state[command->first_point + i];
        s->last_write_id = command->id;
        s->last_write_result = result;
        s->write_completed = true;
        /* Confirmation is transport-level only; do not overwrite polled readback. */
        s->next_poll_ms = 0;
    }
    return result;
}
