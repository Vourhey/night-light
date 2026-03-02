#pragma once

#include "esp_zigbee_core.h"

/* Zigbee endpoint / profile */
#define HA_ENDPOINT              1
#define HA_PROFILE_ID            ESP_ZB_AF_HA_PROFILE_ID
#define EXTENDED_COLOR_LIGHT_ID  0x010D

/* Attribute defaults */
#define DEFAULT_POWER_ON_STATE   0              /* off */
#define DEFAULT_LEVEL            254
#define DEFAULT_HUE              0
#define DEFAULT_SATURATION       0
#define DEFAULT_COLOR_MODE       0x00           /* HS mode */

/**
 * @brief Entry point for the Zigbee task.
 *        Created by app_main and pinned to core 0.
 */
void zigbee_task(void *arg);
