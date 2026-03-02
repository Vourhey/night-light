#include "led_strip_control.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "led_strip.h"

static const char *TAG = "led_strip";

#define LED_UPDATE_BIT BIT0

static led_strip_handle_t   s_strip;
static night_light_state_t  s_state;
static SemaphoreHandle_t    s_mutex;
static EventGroupHandle_t   s_events;

/* ---------- HSV → RGB (integer-only) ---------- */

typedef struct { uint8_t r, g, b; } rgb_t;

static rgb_t hsv_to_rgb(uint8_t h8, uint8_t s8, uint8_t v8)
{
    /* h8: 0–254 → 0–360°; s8, v8: 0–254 → 0–255 */
    if (s8 == 0) {
        return (rgb_t){ v8, v8, v8 };
    }

    uint32_t h = (uint32_t)h8 * 360 / 254; /* 0–360 */
    uint32_t s = s8;                        /* 0–254 */
    uint32_t v = v8;                        /* 0–254 */

    uint32_t sector = h / 60;              /* 0–5  */
    uint32_t frac   = h % 60;             /* 0–59 */

    uint32_t p = v * (254 - s) / 254;
    uint32_t q = v * (254 - s * frac / 60) / 254;
    uint32_t t = v * (254 - s * (60 - frac) / 60) / 254;

    switch (sector) {
        case 0: return (rgb_t){ v, t, p };
        case 1: return (rgb_t){ q, v, p };
        case 2: return (rgb_t){ p, v, t };
        case 3: return (rgb_t){ p, q, v };
        case 4: return (rgb_t){ t, p, v };
        default:return (rgb_t){ v, p, q };
    }
}

/* ---------- LED FreeRTOS task ---------- */

static void led_task(void *arg)
{
    while (1) {
        xEventGroupWaitBits(s_events, LED_UPDATE_BIT,
                            pdTRUE,  /* clear on exit */
                            pdFALSE, /* any bit */
                            portMAX_DELAY);

        night_light_state_t state;
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        memcpy(&state, &s_state, sizeof(state));
        xSemaphoreGive(s_mutex);

        if (!state.on) {
            for (int i = 0; i < LED_STRIP_LED_COUNT; i++) {
                led_strip_set_pixel(s_strip, i, 0, 0, 0);
            }
        } else {
            /* Scale value by level (0–254) */
            uint8_t v = (uint8_t)((uint32_t)state.level * 255 / 254);
            rgb_t rgb = hsv_to_rgb(state.hue, state.saturation, v);
            for (int i = 0; i < LED_STRIP_LED_COUNT; i++) {
                led_strip_set_pixel(s_strip, i, rgb.r, rgb.g, rgb.b);
            }
        }
        led_strip_refresh(s_strip);
    }
}

/* ---------- Public API ---------- */

void led_strip_control_init(void)
{
    s_mutex  = xSemaphoreCreateMutex();
    s_events = xEventGroupCreate();

    /* Sensible power-on defaults */
    s_state.on         = false;
    s_state.level      = 254;
    s_state.hue        = 0;
    s_state.saturation = 0;

    led_strip_config_t strip_cfg = {
        .strip_gpio_num   = LED_STRIP_GPIO,
        .max_leds         = LED_STRIP_LED_COUNT,
        .led_model        = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = false,
        },
    };

    led_strip_rmt_config_t rmt_cfg = {
        .clk_src        = RMT_CLK_SRC_DEFAULT,
        .resolution_hz  = 10000000, /* 10 MHz */
        .mem_block_symbols = 64,
        .flags = {
            .with_dma = false,
        },
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip));
    led_strip_clear(s_strip);

    xTaskCreate(led_task, "led_task", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "LED strip initialised: GPIO %d, %d LEDs",
             LED_STRIP_GPIO, LED_STRIP_LED_COUNT);
}

void led_strip_control_update(const night_light_state_t *state)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memcpy(&s_state, state, sizeof(s_state));
    xSemaphoreGive(s_mutex);
    xEventGroupSetBits(s_events, LED_UPDATE_BIT);
}
