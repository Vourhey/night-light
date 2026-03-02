#pragma once

#include <stdint.h>
#include <stdbool.h>

#define LED_STRIP_GPIO      8
#define LED_STRIP_LED_COUNT 30

/* Shared state written by Zigbee task, read by LED task */
typedef struct {
    bool    on;
    uint8_t level;      /* ZCL CurrentLevel: 0–254 */
    uint8_t hue;        /* ZCL CurrentHue: 0–254 (maps to 0–360°) */
    uint8_t saturation; /* ZCL CurrentSaturation: 0–254 */
} night_light_state_t;

/**
 * @brief Initialise RMT + LED strip and start the LED FreeRTOS task.
 *        Must be called once from app_main before the Zigbee task starts.
 */
void led_strip_control_init(void);

/**
 * @brief Update the shared LED state and signal the LED task to refresh.
 *        Thread-safe; may be called from any task (including Zigbee task).
 */
void led_strip_control_update(const night_light_state_t *state);
