#include "User_Settings.h"
#include "bacnet/bacenum.h"
#include "bacnet/basic/object/device.h"
#include "esp_log.h"

#include <inttypes.h>
#include <string.h>

#include "User_Private_Settings.h"

const char USER_BACNET_FIRMWARE_REVISION[] = "1.0 2026_11_03";

const bool USER_ENABLE_BACNET_IP = true;
const bool USER_WIFI_USE_STATIC_IP = true;
const char USER_WIFI_STATIC_IP_ADDR[] = "10.120.245.96";
const char USER_WIFI_STATIC_IP_GATEWAY[] = "10.120.245.254";
const char USER_WIFI_STATIC_IP_NETMASK[] = "255.255.255.0";
const char USER_WIFI_STATIC_DNS[] = "8.8.8.8";

const char USER_BACNET_DEVICE_NAME[] = "55534";
const uint32_t USER_BACNET_DEVICE_INSTANCE = 55534;
const int USER_OVERRIDE_NVS_ON_FLASH = 0;
const char USER_BACNET_DEVICE_DESCRIPTION[] = "Modbus-BACnet-GW-01";
const char USER_BACNET_MODEL_NAME[] = "ESP32-55534";
const char USER_BACNET_VENDOR_NAME[] = "ESCAP FMS";
const uint16_t USER_BACNET_VENDOR_ID = 260;
const char USER_BACNET_LOCATION[] = "SEC-B Ground Floor FMS";
const char *USER_BACNET_APPLICATION_SOFTWARE_VERSION = USER_BACNET_FIRMWARE_REVISION;
const char USER_BACNET_SERIAL_NUMBER[] = "ESP32-55534-0001";

const bool USER_ENABLE_BACNET_MSTP = true;
const uint8_t USER_MSTP_MAC_ADDRESS = 34;
const uint8_t USER_MSTP_MAX_INFO_FRAMES = 1;
const uint8_t USER_MSTP_MAX_MASTER = 126;
const uint32_t USER_MSTP_BAUD_RATE = 38400U;

const uint8_t USER_BBMD_IP_OCTET_1 = 192;
const uint8_t USER_BBMD_IP_OCTET_2 = 168;
const uint8_t USER_BBMD_IP_OCTET_3 = 1;
const uint8_t USER_BBMD_IP_OCTET_4 = 1;
const uint16_t USER_BBMD_PORT = 0xBAC0;
const uint16_t USER_BBMD_TTL_SECONDS = 600;

/* Add mappings before the final sentinel. See OBJECTS_CONFIGURATION.md.
 *
 * Simulated test device: PC running a Modbus RTU slave simulator (unit ID 1)
 * via a USB-to-RS485 converter.
 * FC04 input registers 0..3 hold raw values 235, 567, 1234, 2500; with
 * scale 0.1 they read as 23.5, 56.7, 123.4 and 250.0.
 * FC02 discrete inputs 0..3 hold initial values 0, 1, 0, 1.
 */
/* Add mappings before the final sentinel. See OBJECTS_CONFIGURATION.md.
 *
 * Simulated test device: PC running a Modbus RTU slave simulator (unit ID 1)
 * via a USB-to-RS485 converter.
 * FC04 input registers 0..3 hold raw values 235, 567, 1234, 2500; with
 * scale 0.1 they read as 23.5, 56.7, 123.4 and 250.0.
 * FC02 discrete inputs 0..3 hold initial values 0, 1, 0, 1.
 */
const gateway_point_mapping_t USER_POINT_MAPPINGS[] = {
    {
        .slave_address = 1,
        .register_type = GATEWAY_INPUT_REGISTER,
        .address = 0,
        .data_type = GATEWAY_UINT16,
        .byte_order = GATEWAY_BYTES_BIG_ENDIAN,
        .word_order = GATEWAY_HIGH_WORD_FIRST,
        .bit_index = GATEWAY_NO_BIT,
        .scale = 0.1f,
        .offset = 0.0f,
        .access = GATEWAY_READ_ONLY,
        .poll_interval_ms = 1000,
        .stale_timeout_ms = 5000,
        .object_type = GATEWAY_OBJECT_AI,
        .instance = 1,
        .name = "SIM_AI_01",
        .description = "Simulated test input register 0 (raw 235 -> 23.5)",
        .units = UNITS_NO_UNITS,
        .cov_increment = 0.1f,
    },
    {
        .slave_address = 1,
        .register_type = GATEWAY_INPUT_REGISTER,
        .address = 1,
        .data_type = GATEWAY_UINT16,
        .byte_order = GATEWAY_BYTES_BIG_ENDIAN,
        .word_order = GATEWAY_HIGH_WORD_FIRST,
        .bit_index = GATEWAY_NO_BIT,
        .scale = 0.1f,
        .offset = 0.0f,
        .access = GATEWAY_READ_ONLY,
        .poll_interval_ms = 1000,
        .stale_timeout_ms = 5000,
        .object_type = GATEWAY_OBJECT_AI,
        .instance = 2,
        .name = "SIM_AI_02",
        .description = "Simulated test input register 1 (raw 567 -> 56.7)",
        .units = UNITS_NO_UNITS,
        .cov_increment = 0.1f,
    },
    {
        .slave_address = 1,
        .register_type = GATEWAY_INPUT_REGISTER,
        .address = 2,
        .data_type = GATEWAY_UINT16,
        .byte_order = GATEWAY_BYTES_BIG_ENDIAN,
        .word_order = GATEWAY_HIGH_WORD_FIRST,
        .bit_index = GATEWAY_NO_BIT,
        .scale = 0.1f,
        .offset = 0.0f,
        .access = GATEWAY_READ_ONLY,
        .poll_interval_ms = 1000,
        .stale_timeout_ms = 5000,
        .object_type = GATEWAY_OBJECT_AI,
        .instance = 3,
        .name = "SIM_AI_03",
        .description = "Simulated test input register 2 (raw 1234 -> 123.4)",
        .units = UNITS_NO_UNITS,
        .cov_increment = 0.1f,
    },
    {
        .slave_address = 1,
        .register_type = GATEWAY_INPUT_REGISTER,
        .address = 3,
        .data_type = GATEWAY_UINT16,
        .byte_order = GATEWAY_BYTES_BIG_ENDIAN,
        .word_order = GATEWAY_HIGH_WORD_FIRST,
        .bit_index = GATEWAY_NO_BIT,
        .scale = 0.1f,
        .offset = 0.0f,
        .access = GATEWAY_READ_ONLY,
        .poll_interval_ms = 1000,
        .stale_timeout_ms = 5000,
        .object_type = GATEWAY_OBJECT_AI,
        .instance = 4,
        .name = "SIM_AI_04",
        .description = "Simulated test input register 3 (raw 2500 -> 250.0)",
        .units = UNITS_NO_UNITS,
        .cov_increment = 0.1f,
    },
    {
        .slave_address = 1,
        .register_type = GATEWAY_DISCRETE_INPUT,
        .address = 0,
        .data_type = GATEWAY_BOOL,
        .byte_order = GATEWAY_BYTES_BIG_ENDIAN,
        .word_order = GATEWAY_HIGH_WORD_FIRST,
        .bit_index = GATEWAY_NO_BIT,
        .scale = 1.0f,
        .offset = 0.0f,
        .access = GATEWAY_READ_ONLY,
        .poll_interval_ms = 1000,
        .stale_timeout_ms = 5000,
        .object_type = GATEWAY_OBJECT_BI,
        .instance = 1,
        .name = "SIM_BI_01",
        .description = "Simulated test discrete input 0 (initial 0)",
        .units = UNITS_NO_UNITS,
        .cov_increment = 0.0f,
        .active_text = "Active",
        .inactive_text = "Inactive",
    },
    {
        .slave_address = 1,
        .register_type = GATEWAY_DISCRETE_INPUT,
        .address = 1,
        .data_type = GATEWAY_BOOL,
        .byte_order = GATEWAY_BYTES_BIG_ENDIAN,
        .word_order = GATEWAY_HIGH_WORD_FIRST,
        .bit_index = GATEWAY_NO_BIT,
        .scale = 1.0f,
        .offset = 0.0f,
        .access = GATEWAY_READ_ONLY,
        .poll_interval_ms = 1000,
        .stale_timeout_ms = 5000,
        .object_type = GATEWAY_OBJECT_BI,
        .instance = 2,
        .name = "SIM_BI_02",
        .description = "Simulated test discrete input 1 (initial 1)",
        .units = UNITS_NO_UNITS,
        .cov_increment = 0.0f,
        .active_text = "Active",
        .inactive_text = "Inactive",
    },
    {
        .slave_address = 1,
        .register_type = GATEWAY_DISCRETE_INPUT,
        .address = 2,
        .data_type = GATEWAY_BOOL,
        .byte_order = GATEWAY_BYTES_BIG_ENDIAN,
        .word_order = GATEWAY_HIGH_WORD_FIRST,
        .bit_index = GATEWAY_NO_BIT,
        .scale = 1.0f,
        .offset = 0.0f,
        .access = GATEWAY_READ_ONLY,
        .poll_interval_ms = 1000,
        .stale_timeout_ms = 5000,
        .object_type = GATEWAY_OBJECT_BI,
        .instance = 3,
        .name = "SIM_BI_03",
        .description = "Simulated test discrete input 2 (initial 0)",
        .units = UNITS_NO_UNITS,
        .cov_increment = 0.0f,
        .active_text = "Active",
        .inactive_text = "Inactive",
    },
    {
        .slave_address = 1,
        .register_type = GATEWAY_DISCRETE_INPUT,
        .address = 3,
        .data_type = GATEWAY_BOOL,
        .byte_order = GATEWAY_BYTES_BIG_ENDIAN,
        .word_order = GATEWAY_HIGH_WORD_FIRST,
        .bit_index = GATEWAY_NO_BIT,
        .scale = 1.0f,
        .offset = 0.0f,
        .access = GATEWAY_READ_ONLY,
        .poll_interval_ms = 1000,
        .stale_timeout_ms = 5000,
        .object_type = GATEWAY_OBJECT_BI,
        .instance = 4,
        .name = "SIM_BI_04",
        .description = "Simulated test discrete input 3 (initial 1)",
        .units = UNITS_NO_UNITS,
        .cov_increment = 0.0f,
        .active_text = "Active",
        .inactive_text = "Inactive",
    },
    {0}
};
const size_t USER_POINT_MAPPING_COUNT =
    sizeof(USER_POINT_MAPPINGS) / sizeof(USER_POINT_MAPPINGS[0]) - 1U;

void User_Settings_InitDeviceIdentity(void)
{
    Device_Object_Name_ANSI_Init(USER_BACNET_DEVICE_NAME);
    Device_Set_Description(USER_BACNET_DEVICE_DESCRIPTION,
        strlen(USER_BACNET_DEVICE_DESCRIPTION));
    Device_Set_Model_Name(USER_BACNET_MODEL_NAME, strlen(USER_BACNET_MODEL_NAME));
    Device_Set_Vendor_Identifier(USER_BACNET_VENDOR_ID);
    Device_Set_Location(USER_BACNET_LOCATION, strlen(USER_BACNET_LOCATION));
    Device_Set_Firmware_Revision(USER_BACNET_FIRMWARE_REVISION,
        strlen(USER_BACNET_FIRMWARE_REVISION));
    Device_Set_Application_Software_Version(USER_BACNET_APPLICATION_SOFTWARE_VERSION,
        strlen(USER_BACNET_APPLICATION_SOFTWARE_VERSION));
    Device_Serial_Number_Set(USER_BACNET_SERIAL_NUMBER,
        strlen(USER_BACNET_SERIAL_NUMBER));
}

void User_Settings_Print(void)
{
#if USER_SETTINGS_PRINT_ENABLE
    const char *tag = "User_Settings";
    ESP_LOGI(tag, "Firmware: %s", USER_BACNET_FIRMWARE_REVISION);
    ESP_LOGI(tag, "Device: %s instance=%" PRIu32,
        USER_BACNET_DEVICE_NAME, USER_BACNET_DEVICE_INSTANCE);
    ESP_LOGI(tag, "BACnet/IP: %s, MS/TP: %s",
        USER_ENABLE_BACNET_IP ? "enabled" : "disabled",
        USER_ENABLE_BACNET_MSTP ? "enabled" : "disabled");
    ESP_LOGI(tag, "MS/TP MAC=%u baud=%" PRIu32 " Max Master=%u Max Info Frames=%u",
        USER_MSTP_MAC_ADDRESS, USER_MSTP_BAUD_RATE,
        USER_MSTP_MAX_MASTER, USER_MSTP_MAX_INFO_FRAMES);
    ESP_LOGI(tag, "Point mappings: %zu (maximum %u)",
        USER_POINT_MAPPING_COUNT, GATEWAY_MAX_POINTS);
#endif
}
