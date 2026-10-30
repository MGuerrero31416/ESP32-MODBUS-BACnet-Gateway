#ifndef USER_SETTINGS_H
#define USER_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "point_mapping.h"

#ifndef USER_SETTINGS_PRINT_ENABLE
#define USER_SETTINGS_PRINT_ENABLE 1
#endif

extern const bool USER_ENABLE_BACNET_IP;
extern const char USER_WIFI_SSID[];
extern const char USER_WIFI_PASS[];
extern const bool USER_WIFI_USE_STATIC_IP;
extern const char USER_WIFI_STATIC_IP_ADDR[];
extern const char USER_WIFI_STATIC_IP_GATEWAY[];
extern const char USER_WIFI_STATIC_IP_NETMASK[];
extern const char USER_WIFI_STATIC_DNS[];

extern const char USER_BACNET_DEVICE_NAME[];
extern const char USER_BACNET_DEVICE_DESCRIPTION[];
extern const char USER_BACNET_MODEL_NAME[];
extern const char USER_BACNET_VENDOR_NAME[];
extern const uint16_t USER_BACNET_VENDOR_ID;
extern const char USER_BACNET_LOCATION[];
extern const char USER_BACNET_FIRMWARE_REVISION[];
extern const char *USER_BACNET_APPLICATION_SOFTWARE_VERSION;
extern const char USER_BACNET_SERIAL_NUMBER[];
extern const uint32_t USER_BACNET_DEVICE_INSTANCE;
extern const int USER_OVERRIDE_NVS_ON_FLASH;

extern const uint8_t USER_BBMD_IP_OCTET_1;
extern const uint8_t USER_BBMD_IP_OCTET_2;
extern const uint8_t USER_BBMD_IP_OCTET_3;
extern const uint8_t USER_BBMD_IP_OCTET_4;
extern const uint16_t USER_BBMD_PORT;
extern const uint16_t USER_BBMD_TTL_SECONDS;

extern const bool USER_ENABLE_BACNET_MSTP;
extern const uint8_t USER_MSTP_MAC_ADDRESS;
extern const uint8_t USER_MSTP_MAX_INFO_FRAMES;
extern const uint8_t USER_MSTP_MAX_MASTER;
extern const uint32_t USER_MSTP_BAUD_RATE;

extern const gateway_point_mapping_t USER_POINT_MAPPINGS[];
extern const size_t USER_POINT_MAPPING_COUNT;

void User_Settings_Print(void);
void User_Settings_InitDeviceIdentity(void);

#endif
