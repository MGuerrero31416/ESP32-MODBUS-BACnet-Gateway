#include "point_mapping.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static gateway_point_mapping_t numeric(void)
{
    return (gateway_point_mapping_t) {
        .slave_address = 1,
        .register_type = GATEWAY_HOLDING_REGISTER,
        .address = 0,
        .data_type = GATEWAY_INT16,
        .bit_index = GATEWAY_NO_BIT,
        .scale = 0.1f,
        .access = GATEWAY_READ_ONLY,
        .poll_interval_ms = 1000,
        .stale_timeout_ms = 5000,
        .object_type = GATEWAY_OBJECT_AI,
        .instance = 1234,
        .name = "Numeric",
        .description = "Test register",
        .units = 95,
        .cov_increment = 0.1f,
    };
}

static void valid(gateway_point_mapping_t *points, size_t count)
{
    gateway_mapping_error_t error = {99, "old"};
    assert(gateway_mapping_validate(points, count, &error));
    assert(error.reason == NULL);
}

static void invalid(gateway_point_mapping_t *points, size_t count, size_t index)
{
    gateway_mapping_error_t error = {99, NULL};
    assert(!gateway_mapping_validate(points, count, &error));
    assert(error.index == index);
    assert(error.reason && error.reason[0]);
    assert(!gateway_mapping_validate(points, count, NULL));
}

#define REJECT(field, value) do { \
    gateway_point_mapping_t bad = numeric(); \
    bad.field = value; \
    invalid(&bad, 1, 0); \
} while (0)

int main(void)
{
    valid(NULL, 0);
    invalid(NULL, 1, 0);
    invalid(NULL, GATEWAY_MAX_POINTS + 1, 0);
    gateway_point_mapping_t p = numeric();
    valid(&p, 1);
    REJECT(slave_address, 0);
    REJECT(slave_address, 248);
    REJECT(register_type, 99);
    REJECT(register_type, GATEWAY_COIL);
    REJECT(register_type, GATEWAY_DISCRETE_INPUT);
    REJECT(data_type, 99);
    REJECT(byte_order, 99);
    REJECT(byte_order, -1);
    REJECT(word_order, 99);
    REJECT(access, 99);
    REJECT(object_type, 99);
    REJECT(object_type, GATEWAY_OBJECT_BI);
    REJECT(instance, 4194303U);
    REJECT(bit_index, 0);
    REJECT(scale, 0);
    REJECT(scale, NAN);
    REJECT(offset, INFINITY);
    REJECT(poll_interval_ms, 99);
    REJECT(poll_interval_ms, 3600001);
    REJECT(stale_timeout_ms, 999);
    REJECT(stale_timeout_ms, 86400001);
    REJECT(stale_timeout_ms, 0);
    REJECT(name, NULL);
    REJECT(name, "");
    REJECT(description, NULL);
    REJECT(cov_increment, 0);
    REJECT(cov_increment, NAN);
    REJECT(cov_increment, -1);
    REJECT(access, GATEWAY_READ_WRITE);

    p.slave_address = 247;
    p.address = 65535;
    p.instance = 4194302;
    p.scale = -0.5f;
    valid(&p, 1);
    p.data_type = GATEWAY_FLOAT32;
    invalid(&p, 1, 0);
    p.address = 65534;
    p.byte_order = GATEWAY_BYTES_LITTLE_ENDIAN;
    p.word_order = GATEWAY_LOW_WORD_FIRST;
    valid(&p, 1);
    for (int type = GATEWAY_UINT16; type <= GATEWAY_FLOAT32; ++type) {
        p.data_type = (gateway_data_type_t)type;
        valid(&p, 1);
        assert(gateway_mapping_register_count(p.data_type) ==
            (type <= GATEWAY_INT16 ? 1U : 2U));
    }
    assert(gateway_mapping_register_count((gateway_data_type_t)99) == 0);

    p = numeric();
    p.access = GATEWAY_READ_WRITE;
    p.object_type = GATEWAY_OBJECT_AV;
    valid(&p, 1);
    p.register_type = GATEWAY_INPUT_REGISTER;
    invalid(&p, 1, 0);

    p = numeric();
    p.register_type = GATEWAY_COIL;
    p.data_type = GATEWAY_BOOL;
    p.object_type = GATEWAY_OBJECT_BI;
    p.scale = 1;
    p.cov_increment = 0;
    p.active_text = "On";
    p.inactive_text = "Off";
    valid(&p, 1);
    p.access = GATEWAY_READ_WRITE;
    invalid(&p, 1, 0);
    p.object_type = GATEWAY_OBJECT_BV;
    valid(&p, 1);
    p.register_type = GATEWAY_DISCRETE_INPUT;
    invalid(&p, 1, 0);
    p.access = GATEWAY_READ_ONLY;
    valid(&p, 1);
    p.register_type = GATEWAY_HOLDING_REGISTER;
    invalid(&p, 1, 0);
    p.bit_index = 15;
    valid(&p, 1);
    p.bit_index = 16;
    invalid(&p, 1, 0);
    p.bit_index = 0;
    p.access = GATEWAY_READ_WRITE;
    invalid(&p, 1, 0);
    p.access = GATEWAY_READ_ONLY;
    p.scale = 2;
    invalid(&p, 1, 0);
    p.scale = 1;
    p.active_text = NULL;
    invalid(&p, 1, 0);

    gateway_point_mapping_t pair[] = {numeric(), numeric()};
    invalid(pair, 2, 1);
    pair[1].instance = 0;
    invalid(pair, 2, 1); /* Names must also be unique. */
    pair[1].name = "Second";
    valid(pair, 2); /* Read-only aliases are allowed. */
    pair[1].object_type = GATEWAY_OBJECT_AV;
    pair[1].instance = pair[0].instance;
    valid(pair, 2); /* Same instance across different object types is legal. */
    pair[1].access = GATEWAY_READ_WRITE;
    invalid(pair, 2, 1);
    pair[1].address = 1;
    valid(pair, 2);
    pair[0].data_type = GATEWAY_FLOAT32;
    invalid(pair, 2, 1); /* Second word of a two-register value. */
    pair[1].slave_address = 2;
    valid(pair, 2);
    pair[1].slave_address = 1;
    pair[1].access = GATEWAY_READ_ONLY;
    pair[1].register_type = GATEWAY_INPUT_REGISTER;
    valid(pair, 2);
    pair[1].instance = 0;
    assert(gateway_mapping_find(pair, 2, GATEWAY_OBJECT_AI, 1234) == &pair[0]);
    assert(gateway_mapping_find(pair, 2, GATEWAY_OBJECT_AV, 0) == &pair[1]);
    assert(!gateway_mapping_find(pair, 2, GATEWAY_OBJECT_AI, 0));
    assert(!gateway_mapping_find(NULL, 0, GATEWAY_OBJECT_AI, 0));

    char name[66];
    memset(name, 'x', sizeof(name));
    name[64] = '\0';
    p = numeric();
    p.name = name;
    valid(&p, 1);
    name[64] = 'x';
    name[65] = '\0';
    invalid(&p, 1, 0);

    gateway_point_mapping_t full[GATEWAY_MAX_POINTS];
    char names[GATEWAY_MAX_POINTS][16];
    for (size_t i = 0; i < GATEWAY_MAX_POINTS; ++i) {
        full[i] = numeric();
        snprintf(names[i], sizeof(names[i]), "Point %u", (unsigned)i);
        full[i].name = names[i];
        full[i].instance = (uint32_t)i * 7U;
    }
    valid(full, GATEWAY_MAX_POINTS);
    puts("Point mapping tests passed");
    return 0;
}
