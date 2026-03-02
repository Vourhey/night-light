#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "led_strip_control.h"
#include "zigbee_light.h"

static const char *TAG = "main";

void app_main(void)
{
    /* NVS is required by the Zigbee stack */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Initialise LED strip and start LED task */
    led_strip_control_init();

    /* Start Zigbee task pinned to core 0 (required by ZBOSS) */
    xTaskCreatePinnedToCore(zigbee_task, "zigbee_task",
                            8192, NULL, 5, NULL, 0);

    ESP_LOGI(TAG, "Night Light started");
}
