#include "mapped_objects.h"
#include "modbus_service.h"
#include "User_Settings.h"
#include "nvs.h"
#include "bacnet/bacapp.h"
#include "bacnet/bacdcode.h"
#include "bacnet/basic/object/ai.h"
#include "bacnet/basic/object/av.h"
#include "bacnet/basic/object/bi.h"
#include "bacnet/basic/object/bv.h"
#include "bacnet/basic/object/device.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define NUMERIC(table, type, object, n, label, permission) { \
    .slave_address = 1, .register_type = table, .data_type = type, \
    .address = n, .bit_index = -1, .scale = 1, .access = permission, \
    .poll_interval_ms = 1000, .stale_timeout_ms = 5000, .object_type = object, \
    .instance = n, .name = label, .description = "Test only", .cov_increment = 1 }
#define BINARY(table, object, n, label, permission) { \
    .slave_address = 1, .register_type = table, .data_type = GATEWAY_BOOL, \
    .address = n, .bit_index = -1, .scale = 1, .access = permission, \
    .poll_interval_ms = 1000, .stale_timeout_ms = 5000, .object_type = object, \
    .instance = n, .name = label, .description = "Test only", \
    .active_text = "On", .inactive_text = "Off" }

const gateway_point_mapping_t USER_POINT_MAPPINGS[] = {
    NUMERIC(GATEWAY_INPUT_REGISTER, GATEWAY_INT16, GATEWAY_OBJECT_AI, 41, "AI", GATEWAY_READ_ONLY),
    NUMERIC(GATEWAY_HOLDING_REGISTER, GATEWAY_INT16, GATEWAY_OBJECT_AV, 102, "AV", GATEWAY_READ_WRITE),
    BINARY(GATEWAY_DISCRETE_INPUT, GATEWAY_OBJECT_BI, 205, "BI", GATEWAY_READ_ONLY),
    BINARY(GATEWAY_COIL, GATEWAY_OBJECT_BV, 701, "BV", GATEWAY_READ_WRITE),
    NUMERIC(GATEWAY_HOLDING_REGISTER, GATEWAY_UINT32, GATEWAY_OBJECT_AV, 900, "U32", GATEWAY_READ_WRITE),
};
const size_t USER_POINT_MAPPING_COUNT = TEST_EMPTY ? 0 : 5;
static modbus_point_state_t snapshots[5];
static unsigned snapshot_calls, queued, persisted;
static esp_err_t queue_error, snapshot_error;
static modbus_write_command_t last_command;
static object_functions_t functions[] = {
    {.Object_Type = OBJECT_ANALOG_INPUT},
    {.Object_Type = OBJECT_ANALOG_VALUE},
    {.Object_Type = OBJECT_BINARY_INPUT},
    {.Object_Type = OBJECT_BINARY_VALUE},
    {.Object_Type = MAX_BACNET_OBJECT_TYPE},
};

const char *esp_err_to_name(esp_err_t error) { (void)error; return "mock"; }
void test_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle)
{
    assert(strcmp(name, "gw_points_v1") == 0);
    if (mode == NVS_READONLY) return ESP_ERR_NVS_NOT_FOUND;
    ++persisted;
    *handle = 1;
    return ESP_OK;
}
esp_err_t nvs_get_str(nvs_handle_t h, const char *k, char *s, size_t *n)
{ (void)h; (void)k; (void)s; (void)n; return ESP_ERR_NVS_NOT_FOUND; }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *k, void *s, size_t *n)
{ (void)h; (void)k; (void)s; (void)n; return ESP_ERR_NVS_NOT_FOUND; }
esp_err_t nvs_set_str(nvs_handle_t h, const char *k, const char *s)
{ (void)h; (void)k; (void)s; return ESP_OK; }
esp_err_t nvs_set_blob(nvs_handle_t h, const char *k, const void *s, size_t n)
{ (void)h; (void)k; (void)s; (void)n; return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }
object_functions_t *Device_Object_Functions(void) { return functions; }
unsigned Device_Object_List_Count(void) { return 0; }
bool Device_Object_List_Identifier(uint32_t n, BACNET_OBJECT_TYPE *type, uint32_t *instance)
{ (void)n; (void)type; (void)instance; return false; }
bool Device_Object_Name_Copy(BACNET_OBJECT_TYPE t, uint32_t n, BACNET_CHARACTER_STRING *s)
{ (void)t; (void)n; (void)s; return false; }
bool Device_Valid_Object_Name(const BACNET_CHARACTER_STRING *s, BACNET_OBJECT_TYPE *t, uint32_t *n)
{ (void)s; (void)t; (void)n; return false; }

esp_err_t modbus_service_get_point(size_t index, modbus_point_state_t *state, bool *pending)
{
    assert(index < USER_POINT_MAPPING_COUNT);
    ++snapshot_calls;
    if (snapshot_error) return snapshot_error;
    *state = snapshots[index];
    *pending = false;
    return ESP_OK;
}
esp_err_t modbus_service_queue_write(const modbus_write_command_t *command)
{
    assert(command->id && command->count == 1);
    modbus_request_t request;
    if (!modbus_build_write(USER_POINT_MAPPINGS, USER_POINT_MAPPING_COUNT, command, &request))
        return ESP_ERR_INVALID_ARG;
    if (queue_error) return queue_error;
    last_command = *command;
    ++queued;
    return ESP_OK;
}

static bool write_value(BACNET_WRITE_PROPERTY_DATA *wp)
{
    for (unsigned i = 0; i < 4; ++i) {
        if (functions[i].Object_Type == wp->object_type)
            return functions[i].Object_Write_Property(wp);
    }
    assert(false);
    return false;
}

static BACNET_WRITE_PROPERTY_DATA request(BACNET_OBJECT_TYPE type, uint32_t instance,
    uint8_t *data, int length)
{
    BACNET_WRITE_PROPERTY_DATA wp = {
        .object_type = type, .object_instance = instance,
        .object_property = PROP_PRESENT_VALUE, .array_index = BACNET_ARRAY_ALL,
        .priority = 16, .application_data_len = length,
    };
    memcpy(wp.application_data, data, (size_t)length);
    return wp;
}

static void check_flags(BACNET_OBJECT_TYPE type, uint32_t instance, bool fault, bool oos)
{
    uint8_t data[32];
    BACNET_READ_PROPERTY_DATA rp = {
        .object_type = type, .object_instance = instance,
        .object_property = PROP_STATUS_FLAGS, .array_index = BACNET_ARRAY_ALL,
        .application_data = data, .application_data_len = sizeof(data),
    };
    int length = -1;
    switch (type) {
    case OBJECT_ANALOG_INPUT: length = Analog_Input_Read_Property(&rp); break;
    case OBJECT_ANALOG_VALUE: length = Analog_Value_Read_Property(&rp); break;
    case OBJECT_BINARY_INPUT: length = Binary_Input_Read_Property(&rp); break;
    case OBJECT_BINARY_VALUE: length = Binary_Value_Read_Property(&rp); break;
    default: assert(false);
    }
    BACNET_APPLICATION_DATA_VALUE value = {0};
    assert(length > 0 && bacapp_decode_application_data(data, (unsigned)length, &value) == length);
    assert(value.tag == BACNET_APPLICATION_TAG_BIT_STRING);
    assert(bitstring_bit(&value.type.Bit_String, STATUS_FLAG_FAULT) == fault);
    assert(bitstring_bit(&value.type.Bit_String, STATUS_FLAG_OUT_OF_SERVICE) == oos);
}

static void set_oos(BACNET_OBJECT_TYPE type, uint32_t instance, bool state)
{
    uint8_t data[8];
    BACNET_WRITE_PROPERTY_DATA wp = request(type, instance, data,
        encode_application_boolean(data, state));
    wp.object_property = PROP_OUT_OF_SERVICE;
    assert(write_value(&wp));
}

static void test_snapshots(void)
{
    bacnet_update_mapped_objects();
    assert(Analog_Input_Reliability(41) == RELIABILITY_COMMUNICATION_FAILURE);
    assert(Analog_Value_Reliability(102) == RELIABILITY_COMMUNICATION_FAILURE);
    assert(Binary_Input_Reliability(205) == RELIABILITY_COMMUNICATION_FAILURE);
    assert(Binary_Value_Reliability(701) == RELIABILITY_COMMUNICATION_FAILURE);
    check_flags(OBJECT_ANALOG_INPUT, 41, true, false);
    check_flags(OBJECT_ANALOG_VALUE, 102, true, false);
    check_flags(OBJECT_BINARY_INPUT, 205, true, false);
    check_flags(OBJECT_BINARY_VALUE, 701, true, false);
    for (unsigned i = 0; i < 5; ++i) {
        snapshots[i] = (modbus_point_state_t) {
            .acquired = true, .quality = MODBUS_POINT_GOOD, .value = i < 2 ? 12.5 : 1,
        };
    }
    snapshots[4].value = 4294967295.0;
    bacnet_update_mapped_objects();
    assert(Analog_Input_Present_Value(41) == 12.5f);
    assert(Analog_Value_Present_Value(102) == 12.5f);
    assert(Binary_Input_Present_Value(205) == BINARY_ACTIVE);
    assert(Binary_Value_Present_Value(701) == BINARY_ACTIVE);
    assert(Analog_Value_Present_Value(900) == (float)4294967295.0);
    assert(Analog_Input_Reliability(41) == RELIABILITY_NO_FAULT_DETECTED);
    assert(Analog_Value_Reliability(102) == RELIABILITY_NO_FAULT_DETECTED);
    assert(Binary_Input_Reliability(205) == RELIABILITY_NO_FAULT_DETECTED);
    assert(Binary_Value_Reliability(701) == RELIABILITY_NO_FAULT_DETECTED);
    check_flags(OBJECT_ANALOG_INPUT, 41, false, false);
    check_flags(OBJECT_ANALOG_VALUE, 102, false, false);
    check_flags(OBJECT_BINARY_INPUT, 205, false, false);
    check_flags(OBJECT_BINARY_VALUE, 701, false, false);
    assert(Analog_Input_Change_Of_Value(41) && Binary_Value_Change_Of_Value(701));
    Analog_Input_Change_Of_Value_Clear(41);
    snapshots[0].quality = MODBUS_POINT_STALE;
    snapshots[0].value = 999;
    snapshots[1].quality = MODBUS_POINT_ERROR;
    snapshots[2].quality = MODBUS_POINT_UNACQUIRED;
    snapshots[3].quality = MODBUS_POINT_ERROR;
    bacnet_update_mapped_objects();
    assert(Analog_Input_Present_Value(41) == 12.5f &&
        Analog_Input_Reliability(41) == RELIABILITY_COMMUNICATION_FAILURE &&
        Analog_Input_Change_Of_Value(41));
    assert(Analog_Value_Reliability(102) == RELIABILITY_COMMUNICATION_FAILURE);
    assert(Binary_Input_Reliability(205) == RELIABILITY_COMMUNICATION_FAILURE);
    assert(Binary_Value_Reliability(701) == RELIABILITY_COMMUNICATION_FAILURE);
    snapshots[0].quality = MODBUS_POINT_GOOD;
    snapshots[0].value = (double)FLT_MAX * 2;
    snapshots[1].last_result = MODBUS_INVALID_VALUE;
    snapshots[3].quality = MODBUS_POINT_GOOD;
    snapshots[3].value = 2;
    bacnet_update_mapped_objects();
    assert(Analog_Input_Present_Value(41) == 12.5f &&
        Analog_Input_Reliability(41) == RELIABILITY_UNRELIABLE_OTHER);
    assert(Analog_Value_Reliability(102) == RELIABILITY_UNRELIABLE_OTHER);
    assert(Binary_Value_Present_Value(701) == BINARY_ACTIVE &&
        Binary_Value_Reliability(701) == RELIABILITY_UNRELIABLE_OTHER);
    snapshots[0].value = NAN;
    bacnet_update_mapped_objects();
    assert(Analog_Input_Reliability(41) == RELIABILITY_UNRELIABLE_OTHER);
    snapshots[0].value = 7;
    snapshots[0].write_completed = true;
    snapshots[0].last_write_result = MODBUS_EXCEPTION;
    bacnet_update_mapped_objects();
    assert(Analog_Input_Present_Value(41) == 7 &&
        Analog_Input_Reliability(41) == RELIABILITY_PROCESS_ERROR);
    bacnet_update_mapped_objects();
    assert(Analog_Input_Reliability(41) == RELIABILITY_PROCESS_ERROR);
    snapshots[0].last_write_result = MODBUS_TIMEOUT;
    bacnet_update_mapped_objects();
    assert(Analog_Input_Reliability(41) == RELIABILITY_COMMUNICATION_FAILURE);
    snapshots[0].last_write_result = MODBUS_OK;
    bacnet_update_mapped_objects();
    assert(Analog_Input_Reliability(41) == RELIABILITY_NO_FAULT_DETECTED);
    snapshot_error = ESP_FAIL;
    bacnet_update_mapped_objects();
    assert(Analog_Input_Reliability(41) == RELIABILITY_COMMUNICATION_FAILURE);
    snapshot_error = ESP_OK;
}

static void test_writes(void)
{
    uint8_t data[32];
    int len = encode_application_real(data, 25);
    BACNET_WRITE_PROPERTY_DATA wp = request(OBJECT_ANALOG_VALUE, 102, data, len);
    float previous = Analog_Value_Present_Value(102);
    assert(write_value(&wp) && queued == 1 && last_command.first_point == 1 &&
        last_command.values[0] == 25 && Analog_Value_Present_Value(102) == previous);
    uint32_t id = last_command.id;
    assert(write_value(&wp) && last_command.id != id);
    queue_error = ESP_ERR_NO_MEM;
    assert(!write_value(&wp) && wp.error_class == ERROR_CLASS_RESOURCES &&
        wp.error_code == ERROR_CODE_DEVICE_BUSY);
    queue_error = ESP_ERR_INVALID_STATE;
    assert(!write_value(&wp) && wp.error_code == ERROR_CODE_DEVICE_BUSY);
    queue_error = ESP_OK;
    wp.priority = 6;
    assert(!write_value(&wp) && wp.error_code == ERROR_CODE_WRITE_ACCESS_DENIED);
    wp.priority = 0;
    assert(!write_value(&wp));
    wp.priority = 17;
    assert(!write_value(&wp));
    wp.priority = 16;
    wp.array_index = 0;
    assert(!write_value(&wp) && wp.error_code == ERROR_CODE_PROPERTY_IS_NOT_AN_ARRAY);
    wp.array_index = BACNET_ARRAY_ALL;
    wp.application_data_len = encode_application_real(wp.application_data, 0.5f);
    assert(!write_value(&wp) && wp.error_code == ERROR_CODE_VALUE_OUT_OF_RANGE);
    wp.application_data_len = encode_application_real(wp.application_data, 32768);
    assert(!write_value(&wp));
    wp.application_data_len = encode_application_real(wp.application_data, NAN);
    assert(!write_value(&wp));
    wp.application_data_len = encode_application_double(wp.application_data, INFINITY);
    assert(!write_value(&wp));
    wp.application_data_len = encode_application_double(wp.application_data, (double)FLT_MAX * 2);
    assert(!write_value(&wp));
    wp.application_data_len = encode_application_boolean(wp.application_data, true);
    assert(!write_value(&wp) && wp.error_code == ERROR_CODE_INVALID_DATA_TYPE);
    wp.application_data_len = encode_application_null(wp.application_data);
    assert(!write_value(&wp));
    wp.application_data_len = 0;
    assert(!write_value(&wp));
    wp.application_data_len = encode_application_real(wp.application_data, 25) - 1;
    assert(!write_value(&wp));
    wp.application_data_len = encode_application_real(wp.application_data, 25);
    wp.application_data[wp.application_data_len++] = 0;
    assert(!write_value(&wp));
    wp = request(OBJECT_ANALOG_INPUT, 41, data, encode_application_real(data, 25));
    assert(!write_value(&wp) && wp.error_code == ERROR_CODE_WRITE_ACCESS_DENIED);
    wp = request(OBJECT_BINARY_INPUT, 205, data, encode_application_enumerated(data, 1));
    assert(!write_value(&wp) && wp.error_code == ERROR_CODE_WRITE_ACCESS_DENIED);
    wp = request(OBJECT_BINARY_VALUE, 701, data, encode_application_enumerated(data, 1));
    assert(write_value(&wp) && last_command.first_point == 3 && last_command.values[0] == 1);
    wp.application_data_len = encode_application_enumerated(wp.application_data, 2);
    assert(!write_value(&wp) && wp.error_code == ERROR_CODE_VALUE_OUT_OF_RANGE);
    wp.application_data_len = encode_application_boolean(wp.application_data, true);
    assert(!write_value(&wp) && wp.error_code == ERROR_CODE_INVALID_DATA_TYPE);
    wp.object_instance = 999;
    assert(!write_value(&wp) && wp.error_code == ERROR_CODE_UNKNOWN_OBJECT);
    wp = request(OBJECT_ANALOG_VALUE, 900, data, encode_application_unsigned(data, UINT32_MAX));
    assert(write_value(&wp) && last_command.values[0] == 4294967295.0);
    wp = request(OBJECT_ANALOG_VALUE, 102, data, encode_application_signed(data, -123));
    assert(write_value(&wp) && last_command.values[0] == -123);
    wp.application_data_len = encode_application_double(wp.application_data, 123);
    assert(write_value(&wp) && last_command.values[0] == 123);
    wp.application_data_len = encode_application_enumerated(wp.application_data, 123);
    assert(write_value(&wp) && last_command.values[0] == 123);
    assert(persisted == 0);
}

static void test_simulation(void)
{
    uint8_t data[32];
    unsigned commands = queued;
    Analog_Value_Change_Of_Value_Clear(102);
    set_oos(OBJECT_ANALOG_INPUT, 41, true);
    set_oos(OBJECT_ANALOG_VALUE, 102, true);
    set_oos(OBJECT_BINARY_INPUT, 205, true);
    set_oos(OBJECT_BINARY_VALUE, 701, true);
    assert(Analog_Value_Change_Of_Value(102));
    BACNET_WRITE_PROPERTY_DATA wp = request(OBJECT_ANALOG_INPUT, 41, data,
        encode_application_real(data, 13.25f));
    assert(write_value(&wp) && Analog_Input_Present_Value(41) == 13.25f);
    wp.object_type = OBJECT_ANALOG_VALUE;
    wp.object_instance = 102;
    assert(write_value(&wp) && Analog_Value_Present_Value(102) == 13.25f);
    wp = request(OBJECT_BINARY_INPUT, 205, data, encode_application_enumerated(data, 0));
    assert(write_value(&wp) && Binary_Input_Present_Value(205) == BINARY_INACTIVE);
    wp.object_type = OBJECT_BINARY_VALUE;
    wp.object_instance = 701;
    assert(write_value(&wp) && Binary_Value_Present_Value(701) == BINARY_INACTIVE);
    check_flags(OBJECT_ANALOG_INPUT, 41, false, true);
    check_flags(OBJECT_ANALOG_VALUE, 102, false, true);
    check_flags(OBJECT_BINARY_INPUT, 205, false, true);
    check_flags(OBJECT_BINARY_VALUE, 701, false, true);
    wp.object_property = PROP_OUT_OF_SERVICE;
    assert(!write_value(&wp) && wp.error_code == ERROR_CODE_INVALID_DATA_TYPE);
    wp.application_data_len = encode_application_boolean(wp.application_data, true);
    wp.array_index = 0;
    assert(!write_value(&wp) && wp.error_code == ERROR_CODE_PROPERTY_IS_NOT_AN_ARRAY);
    snapshots[0].value = 99;
    snapshots[0].quality = MODBUS_POINT_GOOD;
    bacnet_update_mapped_objects();
    assert(Analog_Input_Present_Value(41) == 13.25f && queued == commands && persisted == 0);
    assert(Analog_Input_Reliability(41) == RELIABILITY_NO_FAULT_DETECTED);
    set_oos(OBJECT_ANALOG_INPUT, 41, false);
    set_oos(OBJECT_ANALOG_VALUE, 102, false);
    set_oos(OBJECT_BINARY_INPUT, 205, false);
    set_oos(OBJECT_BINARY_VALUE, 701, false);
    bacnet_update_mapped_objects();
    assert(Analog_Input_Present_Value(41) == 99);
    /* Metadata still uses the original BACnet handler and persistence callback. */
    wp = request(OBJECT_ANALOG_INPUT, 41, data, encode_application_enumerated(data, UNITS_NO_UNITS));
    wp.object_property = PROP_UNITS;
    assert(write_value(&wp) && persisted == 1);
}

int main(void)
{
    assert(gateway_mapping_validate(USER_POINT_MAPPINGS, USER_POINT_MAPPING_COUNT, NULL));
    assert(bacnet_create_mapped_objects() == ESP_OK);
    if (TEST_EMPTY) {
        bacnet_update_mapped_objects();
        assert(snapshot_calls == 0 && queued == 0 && persisted == 0);
        puts("Mapped objects: empty table passed");
    } else {
        test_snapshots();
        test_writes();
        test_simulation();
        puts("Mapped objects: real BACnet values/reliability/COV, writes, simulation, metadata passed");
    }
    return 0;
}
