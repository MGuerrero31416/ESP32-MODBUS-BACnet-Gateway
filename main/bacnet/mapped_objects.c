#include "mapped_objects.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "User_Settings.h"
#include "bacnet/basic/object/ai.h"
#include "bacnet/basic/object/av.h"
#include "bacnet/basic/object/bi.h"
#include "bacnet/basic/object/bo.h"
#include "bacnet/basic/object/bv.h"
#include "bacnet/basic/object/device.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "mapped_objects";
static const char *NVS_NAMESPACE = "gw_points_v1";
static char saved_names[GATEWAY_MAX_POINTS][65];
static char saved_descriptions[GATEWAY_MAX_POINTS][129];

static const gateway_point_mapping_t *find_mapping(
    gateway_object_type_t type, uint32_t instance)
{
    return gateway_mapping_find(
        USER_POINT_MAPPINGS, USER_POINT_MAPPING_COUNT, type, instance);
}

static bool mapped_write_property(BACNET_WRITE_PROPERTY_DATA *wp)
{
    gateway_object_type_t type;
    switch (wp->object_type) {
    case OBJECT_ANALOG_INPUT: type = GATEWAY_OBJECT_AI; break;
    case OBJECT_ANALOG_VALUE: type = GATEWAY_OBJECT_AV; break;
    case OBJECT_BINARY_INPUT: type = GATEWAY_OBJECT_BI; break;
    case OBJECT_BINARY_VALUE: type = GATEWAY_OBJECT_BV; break;
    default:
        wp->error_class = ERROR_CLASS_OBJECT;
        wp->error_code = ERROR_CODE_UNKNOWN_OBJECT;
        return false;
    }
    const gateway_point_mapping_t *point = find_mapping(type, wp->object_instance);
    if (!point) {
        wp->error_class = ERROR_CLASS_OBJECT;
        wp->error_code = ERROR_CODE_UNKNOWN_OBJECT;
        return false;
    }
    if (wp->object_property == PROP_PRESENT_VALUE) {
        wp->error_class = ERROR_CLASS_PROPERTY;
        wp->error_code = point->access == GATEWAY_READ_ONLY ?
            ERROR_CODE_WRITE_ACCESS_DENIED :
            ERROR_CODE_OPTIONAL_FUNCTIONALITY_NOT_SUPPORTED;
        ESP_LOGW(TAG, "Present_Value write rejected for %s: %s", point->name,
            point->access == GATEWAY_READ_ONLY ? "read-only point" :
            "Modbus write-through is not implemented");
        return false;
    }
    switch (type) {
    case GATEWAY_OBJECT_AI: return Analog_Input_Write_Property(wp);
    case GATEWAY_OBJECT_AV: return Analog_Value_Write_Property(wp);
    case GATEWAY_OBJECT_BI: return Binary_Input_Write_Property(wp);
    case GATEWAY_OBJECT_BV: return Binary_Value_Write_Property(wp);
    }
    return false;
}

static bool make_key(char key[16], gateway_object_type_t type,
    uint32_t instance, const char *property)
{
    int size = snprintf(key, 16, "%u_%lu_%s",
        (unsigned)type, (unsigned long)instance, property);
    if (size < 0 || size >= 16) {
        ESP_LOGE(TAG, "NVS key exceeds the 15-character limit");
        return false;
    }
    return true;
}

static void save_property(gateway_object_type_t type, uint32_t instance,
    const char *property, const void *value, size_t size, bool text)
{
    if (!find_mapping(type, instance)) {
        ESP_LOGE(TAG, "Cannot persist property for unmapped instance %lu",
            (unsigned long)instance);
        return;
    }
    char key[16];
    if (!make_key(key, type, instance, property)) {
        return;
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        if (text) {
            char buffer[129];
            if (!value || size >= sizeof(buffer)) {
                ESP_LOGE(TAG, "Invalid metadata text length %zu", size);
                nvs_close(handle);
                return;
            }
            memcpy(buffer, value, size);
            buffer[size] = '\0';
            err = nvs_set_str(handle, key, buffer);
        } else {
            err = nvs_set_blob(handle, key, value, size);
        }
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot persist %s: %s", key, esp_err_to_name(err));
    }
}

static esp_err_t load_property(nvs_handle_t handle, const gateway_point_mapping_t *p,
    const char *property, void *value, size_t size, bool text)
{
    char key[16];
    if (!make_key(key, p->object_type, p->instance, property)) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t loaded_size = size;
    esp_err_t err = text ? nvs_get_str(handle, key, value, &loaded_size) :
        nvs_get_blob(handle, key, value, &loaded_size);
    if (err == ESP_OK && !text && loaded_size != size) {
        err = ESP_ERR_INVALID_SIZE;
    }
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Cannot load %s: %s", key, esp_err_to_name(err));
    }
    return err;
}

static esp_err_t load_metadata(size_t index)
{
    const gateway_point_mapping_t *p = &USER_POINT_MAPPINGS[index];
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot open gateway metadata: %s", esp_err_to_name(err));
        return err;
    }
    char *buffers[] = {saved_names[index], saved_descriptions[index]};
    const char *properties[] = {"name", "desc"};
    size_t sizes[] = {sizeof(saved_names[index]), sizeof(saved_descriptions[index])};
    for (size_t j = 0; j < 2; ++j) {
        err = load_property(handle, p, properties[j], buffers[j], sizes[j], true);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            continue;
        }
        if (err != ESP_OK) {
            nvs_close(handle);
            return err;
        }
        if (j == 0 && !buffers[j][0]) {
            ESP_LOGE(TAG, "Saved object name is empty for %s", p->name);
            nvs_close(handle);
            return ESP_ERR_INVALID_ARG;
        }
        bool applied = false;
        switch (p->object_type) {
        case GATEWAY_OBJECT_AI:
            applied = j ? Analog_Input_Description_Set(p->instance, buffers[j]) :
                Analog_Input_Name_Set(p->instance, buffers[j]);
            break;
        case GATEWAY_OBJECT_AV:
            applied = j ? Analog_Value_Description_Set(p->instance, buffers[j]) :
                Analog_Value_Name_Set(p->instance, buffers[j]);
            break;
        case GATEWAY_OBJECT_BI:
            applied = j ? Binary_Input_Description_Set(p->instance, buffers[j]) :
                Binary_Input_Name_Set(p->instance, buffers[j]);
            break;
        case GATEWAY_OBJECT_BV:
            applied = j ? Binary_Value_Description_Set(p->instance, buffers[j]) :
                Binary_Value_Name_Set(p->instance, buffers[j]);
            break;
        }
        if (!applied) {
            ESP_LOGE(TAG, "Cannot apply saved metadata to %s", p->name);
            nvs_close(handle);
            return ESP_FAIL;
        }
    }
    if (p->object_type == GATEWAY_OBJECT_AI || p->object_type == GATEWAY_OBJECT_AV) {
        uint16_t units;
        err = load_property(handle, p, "unit", &units, sizeof(units), false);
        if (err == ESP_OK) {
            bool applied = p->object_type == GATEWAY_OBJECT_AI ?
                Analog_Input_Units_Set(p->instance, units) :
                Analog_Value_Units_Set(p->instance, units);
            if (!applied) {
                err = ESP_FAIL;
            }
        }
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGE(TAG, "Cannot restore units for %s", p->name);
            nvs_close(handle);
            return err;
        }
    }
    if (p->object_type == GATEWAY_OBJECT_AI) {
        float cov;
        err = load_property(handle, p, "cov", &cov, sizeof(cov), false);
        if (err == ESP_OK) {
            if (!isfinite(cov) || cov <= 0) {
                err = ESP_ERR_INVALID_ARG;
            } else {
                Analog_Input_COV_Increment_Set(p->instance, cov);
            }
        }
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGE(TAG, "Cannot restore COV increment for %s", p->name);
            nvs_close(handle);
            return err;
        }
    }
    nvs_close(handle);
    return ESP_OK;
}

esp_err_t bacnet_create_mapped_objects(void)
{
    gateway_mapping_error_t error;
    if (!gateway_mapping_validate(USER_POINT_MAPPINGS, USER_POINT_MAPPING_COUNT, &error)) {
        ESP_LOGE(TAG, "Invalid mapping %zu: %s", error.index, error.reason);
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < USER_POINT_MAPPING_COUNT; ++i) {
        const gateway_point_mapping_t *p = &USER_POINT_MAPPINGS[i];
        uint32_t n = p->instance;
        bool ok = false;
        switch (p->object_type) {
        case GATEWAY_OBJECT_AI:
            ok = Analog_Input_Create(n) == n &&
                Analog_Input_Name_Set(n, p->name) &&
                Analog_Input_Description_Set(n, p->description) &&
                Analog_Input_Units_Set(n, p->units) &&
                Analog_Input_Reliability_Set(n, RELIABILITY_COMMUNICATION_FAILURE);
            if (ok) {
                Analog_Input_COV_Increment_Set(n, p->cov_increment);
            }
            break;
        case GATEWAY_OBJECT_AV:
            ok = Analog_Value_Create(n) == n &&
                Analog_Value_Name_Set(n, p->name) &&
                Analog_Value_Description_Set(n, p->description) &&
                Analog_Value_Units_Set(n, p->units) &&
                Analog_Value_Reliability_Set(n, RELIABILITY_COMMUNICATION_FAILURE);
            if (ok) {
                Analog_Value_COV_Increment_Set(n, p->cov_increment);
            }
            break;
        case GATEWAY_OBJECT_BI:
            ok = Binary_Input_Create(n) == n &&
                Binary_Input_Name_Set(n, p->name) &&
                Binary_Input_Description_Set(n, p->description) &&
                Binary_Input_Active_Text_Set(n, p->active_text) &&
                Binary_Input_Inactive_Text_Set(n, p->inactive_text) &&
                Binary_Input_Reliability_Set(n, RELIABILITY_COMMUNICATION_FAILURE);
            break;
        case GATEWAY_OBJECT_BV:
            ok = Binary_Value_Create(n) == n &&
                Binary_Value_Name_Set(n, p->name) &&
                Binary_Value_Description_Set(n, p->description) &&
                Binary_Value_Active_Text_Set(n, p->active_text) &&
                Binary_Value_Inactive_Text_Set(n, p->inactive_text) &&
                Binary_Value_Reliability_Set(n, RELIABILITY_COMMUNICATION_FAILURE);
            break;
        }
        if (!ok) {
            ESP_LOGE(TAG, "Cannot create/configure mapping %zu (%s)", i, p->name);
            return ESP_FAIL;
        }
        esp_err_t err = load_metadata(i);
        if (err != ESP_OK) {
            return err;
        }
    }
    for (unsigned index = 1; index <= Device_Object_List_Count(); ++index) {
        BACNET_OBJECT_TYPE type, found_type;
        uint32_t instance, found_instance;
        BACNET_CHARACTER_STRING name;
        if (!Device_Object_List_Identifier(index, &type, &instance) ||
            !Device_Object_Name_Copy(type, instance, &name) ||
            !Device_Valid_Object_Name(&name, &found_type, &found_instance) ||
            type != found_type || instance != found_instance) {
            ESP_LOGE(TAG, "Duplicate or invalid object name after gateway metadata restore");
            return ESP_ERR_INVALID_ARG;
        }
    }
    for (object_functions_t *object = Device_Object_Functions();
         object->Object_Type != MAX_BACNET_OBJECT_TYPE; ++object) {
        if (object->Object_Type == OBJECT_ANALOG_INPUT ||
            object->Object_Type == OBJECT_ANALOG_VALUE ||
            object->Object_Type == OBJECT_BINARY_INPUT ||
            object->Object_Type == OBJECT_BINARY_VALUE) {
            object->Object_Write_Property = mapped_write_property;
        }
    }
    ESP_LOGI(TAG, "Created %zu mapped objects; no acquired Modbus values yet",
        USER_POINT_MAPPING_COUNT);
    return ESP_OK;
}

#define METADATA_CALLBACKS(prefix, type) \
    void bacnet_nvs_save_##prefix##_name(uint32_t n, const char *s, uint16_t len) \
    { save_property(type, n, "name", s, len, true); } \
    void bacnet_nvs_save_##prefix##_desc(uint32_t n, const char *s, uint16_t len) \
    { save_property(type, n, "desc", s, len, true); }

METADATA_CALLBACKS(ai, GATEWAY_OBJECT_AI)
METADATA_CALLBACKS(av, GATEWAY_OBJECT_AV)
METADATA_CALLBACKS(bi, GATEWAY_OBJECT_BI)
METADATA_CALLBACKS(bv, GATEWAY_OBJECT_BV)

void bacnet_nvs_save_ai_units(uint32_t n, uint16_t units)
{
    save_property(GATEWAY_OBJECT_AI, n, "unit", &units, sizeof(units), false);
}

void bacnet_nvs_save_av_units(uint32_t n, uint16_t units)
{
    save_property(GATEWAY_OBJECT_AV, n, "unit", &units, sizeof(units), false);
}

void bacnet_nvs_save_ai_cov_increment(uint32_t n, float cov)
{
    save_property(GATEWAY_OBJECT_AI, n, "cov", &cov, sizeof(cov), false);
}

void bacnet_nvs_save_av_pv(uint32_t n, float value)
{
    (void)value;
    ESP_LOGE(TAG, "Unexpected attempt to persist AV%lu process value", (unsigned long)n);
}

/* The stack links BO handlers, but commandable outputs are not mapped yet. */
void bacnet_nvs_save_bo_name(uint32_t n, const char *s, uint16_t len)
{
    (void)s;
    (void)len;
    ESP_LOGE(TAG, "Cannot persist unmapped BO%lu name", (unsigned long)n);
}

void bacnet_nvs_save_bo_desc(uint32_t n, const char *s, uint16_t len)
{
    (void)s;
    (void)len;
    ESP_LOGE(TAG, "Cannot persist unmapped BO%lu description", (unsigned long)n);
}
