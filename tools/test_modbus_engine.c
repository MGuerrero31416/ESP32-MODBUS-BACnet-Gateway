#include "modbus_engine.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint64_t now;
    uint32_t duration;
    unsigned calls;
    unsigned failures_left;
    modbus_result_t failure;
    bool nan_payload;
    modbus_request_t requests[128];
} fixture_t;

static gateway_point_mapping_t point(unsigned index)
{
    static const char *names[] = {
        "P0", "P1", "P2", "P3", "P4", "P5", "P6", "P7",
        "P8", "P9", "P10", "P11", "P12", "P13", "P14", "P15"
    };
    assert(index < 16);
    return (gateway_point_mapping_t) {
        .slave_address = 1, .register_type = GATEWAY_HOLDING_REGISTER,
        .address = (uint16_t)index, .data_type = GATEWAY_UINT16,
        .bit_index = GATEWAY_NO_BIT, .scale = 1,
        .poll_interval_ms = 1000, .stale_timeout_ms = 5000,
        .object_type = GATEWAY_OBJECT_AI, .instance = index,
        .name = names[index], .description = "Test", .cov_increment = 1,
    };
}

static void binary(gateway_point_mapping_t *p, gateway_register_type_t table)
{
    p->register_type = table;
    p->data_type = GATEWAY_BOOL;
    p->object_type = GATEWAY_OBJECT_BI;
    p->cov_increment = 0;
    p->active_text = "On";
    p->inactive_text = "Off";
}

static void writable(gateway_point_mapping_t *p)
{
    p->access = GATEWAY_READ_WRITE;
    p->object_type = p->data_type == GATEWAY_BOOL ?
        GATEWAY_OBJECT_BV : GATEWAY_OBJECT_AV;
}

static uint64_t clock_ms(void *context)
{
    return ((fixture_t *)context)->now;
}

static modbus_result_t transfer(void *context, modbus_request_t *request)
{
    fixture_t *f = context;
    assert(f->calls < 128);
    f->requests[f->calls++] = *request;
    f->now += f->duration;
    if (f->failures_left) {
        --f->failures_left;
        return f->failure;
    }
    if (request->function <= 2) {
        memset(request->data.bits, 0xA5, sizeof(request->data.bits));
    } else if (request->function <= 4) {
        for (unsigned i = 0; i < request->quantity; ++i) {
            request->data.words[i] = f->nan_payload ?
                (i == 0 ? 0x7FC0 : 0) : (uint16_t)(100 + request->address + i);
        }
    }
    return MODBUS_OK;
}

static void init(modbus_engine_t *e, gateway_point_mapping_t *p, size_t count,
    fixture_t *f, unsigned registers, unsigned bits, unsigned retries)
{
    modbus_engine_config_t config = {
        .max_read_registers = registers, .max_read_bits = bits,
        .retries = retries, .backoff_max_ms = 4000,
    };
    memset(f, 0, sizeof(*f));
    assert(modbus_engine_init(e, p, count, &config, clock_ms, transfer, f));
}

static void test_codecs(void)
{
    gateway_point_mapping_t p = point(0);
    uint16_t words[2];
    double value;
    static const struct {
        gateway_data_type_t type;
        double value;
        uint16_t high, low;
    } cases[] = {
        {GATEWAY_UINT16, 65535, 0, 0xFFFF},
        {GATEWAY_INT16, -32768, 0xFFFF, 0x8000},
        {GATEWAY_INT16, 32767, 0, 0x7FFF},
        {GATEWAY_UINT32, 4294967295.0, 0xFFFF, 0xFFFF},
        {GATEWAY_INT32, -2147483648.0, 0x8000, 0},
        {GATEWAY_INT32, 2147483647.0, 0x7FFF, 0xFFFF},
        {GATEWAY_FLOAT32, -12.5, 0xC148, 0},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        p.data_type = cases[i].type;
        unsigned width = gateway_mapping_register_count(p.data_type);
        for (unsigned bytes = 0; bytes < 2; ++bytes) {
            for (unsigned order = 0; order < 2; ++order) {
                p.byte_order = (gateway_byte_order_t)bytes;
                p.word_order = (gateway_word_order_t)order;
                assert(modbus_encode_value(&p, cases[i].value, words));
                uint16_t expected[2] = {width == 2 && order == 0 ?
                    cases[i].high : cases[i].low,
                    order == 0 ? cases[i].low : cases[i].high};
                for (unsigned j = 0; j < width; ++j) {
                    if (bytes) {
                        expected[j] = (uint16_t)((expected[j] >> 8) | (expected[j] << 8));
                    }
                    assert(words[j] == expected[j]);
                }
                assert(modbus_decode_value(&p, words, &value));
                assert(value == cases[i].value);
            }
        }
    }
    p = point(0);
    p.data_type = GATEWAY_INT16;
    p.scale = 0.1f;
    p.offset = -10;
    assert(modbus_encode_value(&p, 2.3, words) && words[0] == 123);
    assert(modbus_decode_value(&p, words, &value) && fabs(value - 2.3) < 0.00001);
    assert(!modbus_encode_value(&p, 2.35, words));
    p.scale = -2;
    p.offset = 10;
    assert(modbus_encode_value(&p, 14, words) && words[0] == 0xFFFE);
    assert(modbus_decode_value(&p, words, &value) && value == 14);
    p = point(0);
    assert(!modbus_encode_value(&p, -1, words));
    assert(!modbus_encode_value(&p, 65536, words));
    assert(!modbus_encode_value(&p, 0.5, words));
    assert(!modbus_encode_value(&p, NAN, words));
    assert(!modbus_encode_value(&p, INFINITY, words));
    p.scale = 0;
    assert(!modbus_encode_value(&p, 1, words));
    p = point(0);
    p.data_type = GATEWAY_UINT32;
    assert(!modbus_encode_value(&p, 4294967296.0, words));
    p.data_type = GATEWAY_INT32;
    assert(!modbus_encode_value(&p, -2147483649.0, words));
    p.data_type = GATEWAY_FLOAT32;
    assert(!modbus_encode_value(&p, (double)FLT_MAX * 2, words));
    words[0] = 0x7FC0;
    words[1] = 0;
    assert(!modbus_decode_value(&p, words, &value));
    words[0] = 0x7F80;
    assert(!modbus_decode_value(&p, words, &value));
    binary(&p, GATEWAY_COIL);
    assert(modbus_encode_value(&p, 1, words) && words[0] == 1);
    assert(modbus_decode_value(&p, words, &value) && value == 1);
    assert(!modbus_encode_value(&p, 2, words));
    p.register_type = GATEWAY_INPUT_REGISTER;
    p.bit_index = 15;
    words[0] = 0x8000;
    assert(modbus_decode_value(&p, words, &value) && value == 1);
    p.bit_index = 14;
    assert(modbus_decode_value(&p, words, &value) && value == 0);
    assert(!modbus_encode_value(&p, 1, words));
    assert(!modbus_decode_value(NULL, words, &value));
}

static void test_reads_and_grouping(void)
{
    modbus_engine_t e;
    fixture_t f;
    gateway_point_mapping_t p[4] = {point(0), point(1), point(2), point(3)};
    p[1].address = 2;
    p[2].address = 1;
    p[3].address = 4;
    init(&e, p, 4, &f, 3, 10, 0);
    f.duration = 50;
    assert(modbus_engine_poll(&e));
    assert(f.calls == 1 && f.requests[0].function == 3 && f.requests[0].quantity == 3);
    assert(e.state[0].value == 100 && e.state[1].value == 102 && e.state[2].value == 101);
    assert(e.state[0].next_poll_ms == 1050 && e.state[0].last_success_ms == 50);
    assert(modbus_engine_poll(&e) && f.requests[1].address == 4);
    assert(!modbus_engine_poll(&e));
    f.now = 1050;
    assert(modbus_engine_poll(&e));
    assert(f.requests[2].quantity <= 3);

    p[0] = point(0);
    p[1] = point(1);
    init(&e, p, 2, &f, 2, 1, 0);
    e.state[1].next_poll_ms = 100;
    assert(modbus_engine_poll(&e) && f.requests[0].quantity == 1);
    init(&e, p, 2, &f, 2, 1, 0);
    p[1].poll_interval_ms = 2000;
    assert(modbus_engine_poll(&e) && f.requests[0].quantity == 1);
    p[1].poll_interval_ms = 1000;
    p[1].slave_address = 2;
    init(&e, p, 2, &f, 2, 1, 0);
    assert(modbus_engine_poll(&e) && f.requests[0].quantity == 1);
    assert(modbus_engine_poll(&e) && f.requests[1].slave == 2);
    p[1].slave_address = 1;
    p[1].register_type = GATEWAY_INPUT_REGISTER;
    init(&e, p, 2, &f, 2, 1, 0);
    assert(modbus_engine_poll(&e) && f.requests[0].function == 3);
    assert(modbus_engine_poll(&e) && f.requests[1].function == 4);

    for (unsigned table = 0; table < 2; ++table) {
        for (unsigned i = 0; i < 4; ++i) {
            p[i] = point(i);
            binary(&p[i], (gateway_register_type_t)table);
        }
        init(&e, p, 4, &f, 2, 3, 0);
        assert(modbus_engine_poll(&e));
        assert(f.requests[0].function == table + 1 && f.requests[0].quantity == 3);
        assert(e.state[0].value == 1 && e.state[1].value == 0 && e.state[2].value == 1);
        assert(!e.state[3].acquired);
    }
    p[0] = point(0);
    p[0].address = 65534;
    p[0].data_type = GATEWAY_UINT32;
    init(&e, p, 1, &f, 2, 1, 0);
    assert(modbus_engine_poll(&e) && f.requests[0].quantity == 2);
    init(&e, NULL, 0, &f, 2, 1, 0);
    assert(!modbus_engine_poll(&e) && f.calls == 0);

    gateway_point_mapping_t maximum[GATEWAY_MAX_POINTS];
    char names[GATEWAY_MAX_POINTS][16];
    for (unsigned i = 0; i < GATEWAY_MAX_POINTS; ++i) {
        maximum[i] = point(i % 16);
        snprintf(names[i], sizeof(names[i]), "Maximum %u", i);
        maximum[i].name = names[i];
        maximum[i].instance = i;
        maximum[i].address = (uint16_t)(i * 2);
        maximum[i].data_type = GATEWAY_UINT32;
    }
    init(&e, maximum, GATEWAY_MAX_POINTS, &f, 125, 2000, 0);
    assert(modbus_engine_poll(&e));
    assert(f.requests[0].quantity == 124 && e.state[61].acquired && !e.state[62].acquired);
    assert(modbus_engine_poll(&e));
    assert(f.requests[1].address == 124 && f.requests[1].quantity == 4 && e.state[63].acquired);
}

static void test_recovery_and_staleness(void)
{
    modbus_engine_t e;
    fixture_t f;
    gateway_point_mapping_t p[2] = {point(0), point(1)};
    p[1].slave_address = 2;
    init(&e, p, 2, &f, 2, 1, 2);
    f.failure = MODBUS_BAD_RESPONSE;
    f.failures_left = 1;
    assert(modbus_engine_poll(&e) && f.calls == 2 &&
        e.counters.errors == 1 && e.state[0].quality == MODBUS_POINT_GOOD);
    init(&e, p, 2, &f, 2, 1, 2);
    f.failure = MODBUS_TIMEOUT;
    f.failures_left = 3;
    f.duration = 100;
    assert(modbus_engine_poll(&e) && f.calls == 3);
    assert(e.counters.timeouts == 3 && e.state[0].reads_failed == 1);
    assert(e.slaves[1].next_probe_ms == 1300);
    assert(modbus_engine_poll(&e) && f.requests[3].slave == 2);
    assert(!modbus_engine_poll(&e));
    for (unsigned i = 0; i < 4; ++i) {
        f.now = e.slaves[1].next_probe_ms;
        e.cursor = 0;
        f.failure = MODBUS_BAD_RESPONSE;
        f.failures_left = 3;
        uint64_t before = f.now;
        assert(modbus_engine_poll(&e));
        uint64_t delay = i == 0 ? 2000 : 4000;
        assert(e.slaves[1].next_probe_ms == before + 300 + delay);
    }
    f.now = e.slaves[1].next_probe_ms;
    e.cursor = 0;
    assert(modbus_engine_poll(&e) && e.state[0].quality == MODBUS_POINT_GOOD);
    assert(e.slaves[1].failures == 0 && e.slaves[1].next_probe_ms == 0);
    uint64_t success = e.state[0].last_success_ms;
    modbus_point_check_stale(&e.state[0], &p[0], success - 1);
    assert(e.state[0].quality == MODBUS_POINT_GOOD);
    modbus_point_check_stale(&e.state[0], &p[0], success + 4999);
    assert(e.state[0].quality == MODBUS_POINT_GOOD);
    modbus_point_check_stale(&e.state[0], &p[0], success + 5000);
    assert(e.state[0].quality == MODBUS_POINT_STALE && e.state[0].value == 100);
    f.now = success + 5000;
    f.failure = MODBUS_IO_ERROR;
    f.failures_left = 1;
    e.cursor = 0;
    unsigned calls = f.calls;
    assert(modbus_engine_poll(&e) && f.calls == calls + 1);
    assert(e.state[0].acquired && e.state[0].value == 100 &&
        e.state[0].quality == MODBUS_POINT_ERROR);

    p[1].slave_address = 1;
    init(&e, p, 2, &f, 2, 1, 2);
    f.failure = MODBUS_EXCEPTION;
    f.failures_left = 1;
    assert(modbus_engine_poll(&e) && f.calls == 1 && e.slaves[1].single_reads);
    assert(e.counters.exceptions == 1 && e.counters.errors == 1 &&
        e.slaves[1].next_probe_ms == 0 && e.slaves[1].failures == 0);
    f.now = 1000;
    assert(modbus_engine_poll(&e) && f.requests[1].quantity == 1);
    assert(modbus_engine_poll(&e) && f.requests[2].quantity == 1);
    assert(e.state[0].acquired && e.state[1].acquired);

    p[0].data_type = GATEWAY_FLOAT32;
    init(&e, p, 1, &f, 2, 1, 0);
    assert(modbus_engine_poll(&e));
    double last_good = e.state[0].value;
    f.now = 1000;
    f.nan_payload = true;
    assert(modbus_engine_poll(&e));
    assert(e.state[0].quality == MODBUS_POINT_ERROR &&
        e.state[0].last_result == MODBUS_INVALID_VALUE &&
        e.state[0].value == last_good && e.state[0].reads_failed == 1);
}

static void test_writes(void)
{
    modbus_engine_t e;
    fixture_t f;
    modbus_request_t request;
    gateway_point_mapping_t p[16];
    for (unsigned i = 0; i < 16; ++i) {
        p[i] = point(i);
        binary(&p[i], GATEWAY_COIL);
        writable(&p[i]);
    }
    modbus_write_command_t command = {.id = 42, .count = 1, .values = {1}};
    assert(modbus_build_write(p, 16, &command, &request));
    assert(request.function == 5 && request.quantity == 1 && request.data.words[0] == 0xFF00);
    command.values[0] = 0;
    assert(modbus_build_write(p, 16, &command, &request) && request.data.words[0] == 0);
    command.count = 16;
    for (unsigned i = 0; i < 16; ++i) {
        command.values[i] = i % 2;
    }
    assert(modbus_build_write(p, 16, &command, &request));
    assert(request.function == 15 && request.quantity == 16 &&
        request.data.bits[0] == 0xAA && request.data.bits[1] == 0xAA);
    command.values[15] = 2;
    assert(!modbus_build_write(p, 16, &command, &request));
    p[0] = point(0);
    writable(&p[0]);
    command.count = 1;
    command.values[0] = 65535;
    assert(modbus_build_write(p, 16, &command, &request) &&
        request.function == 6 && request.data.words[0] == 65535);
    p[0].data_type = GATEWAY_UINT32;
    command.values[0] = 0x12345678;
    assert(modbus_build_write(p, 16, &command, &request));
    assert(request.function == 16 && request.quantity == 2 &&
        request.data.words[0] == 0x1234 && request.data.words[1] == 0x5678);
    p[1] = point(1);
    p[1].address = 2;
    p[1].data_type = GATEWAY_INT16;
    writable(&p[1]);
    command.count = 2;
    command.values[1] = -1;
    assert(modbus_build_write(p, 2, &command, &request) &&
        request.quantity == 3 && request.data.words[2] == 0xFFFF);
    p[1].address = 3;
    assert(!modbus_build_write(p, 2, &command, &request));
    p[1].address = 2;
    p[1].slave_address = 2;
    assert(!modbus_build_write(p, 2, &command, &request));
    p[1].slave_address = 1;
    p[1].access = GATEWAY_READ_ONLY;
    assert(!modbus_build_write(p, 2, &command, &request));
    writable(&p[1]);
    init(&e, p, 2, &f, 3, 1, 2);
    assert(modbus_engine_poll(&e));
    double readback = e.state[0].value;
    assert(modbus_engine_write(&e, &command) == MODBUS_OK);
    assert(e.state[0].value == readback && e.state[0].next_poll_ms == 0 &&
        e.state[1].last_write_id == 42 && e.state[1].write_completed &&
        e.state[1].last_write_result == MODBUS_OK && e.counters.writes_ok == 1);
    f.failure = MODBUS_TIMEOUT;
    f.failures_left = 10;
    unsigned calls = f.calls;
    command.id = 43;
    assert(modbus_engine_write(&e, &command) == MODBUS_TIMEOUT && f.calls == calls + 1);
    assert(e.state[0].last_write_id == 43 && e.state[0].last_write_result == MODBUS_TIMEOUT);
    command.id = 44;
    assert(modbus_engine_write(&e, &command) == MODBUS_OFFLINE && f.calls == calls + 1);
    assert(e.counters.writes_failed == 2 && e.state[0].value == readback);
    command.first_point = 2;
    assert(modbus_engine_write(&e, &command) == MODBUS_INVALID_VALUE && f.calls == calls + 1);
    command.first_point = 0;
    command.count = 17;
    assert(!modbus_build_write(p, 2, &command, &request));
    command.count = 0;
    assert(!modbus_build_write(p, 2, &command, &request));
}

int main(void)
{
    test_codecs();
    test_reads_and_grouping();
    test_recovery_and_staleness();
    test_writes();
    puts("Modbus engine: codecs, reads/grouping, recovery/staleness, writes passed");
    return 0;
}
