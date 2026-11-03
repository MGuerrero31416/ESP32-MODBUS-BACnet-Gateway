#pragma once

#include "esp_err.h"

esp_err_t bacnet_create_mapped_objects(void);
/* Call only while holding the existing BACnet datalink/object lock. */
void bacnet_update_mapped_objects(void);
