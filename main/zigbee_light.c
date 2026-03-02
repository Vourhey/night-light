#include "zigbee_light.h"
#include "led_strip_control.h"

#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_zigbee_core.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "zcl/esp_zigbee_zcl_common.h"

static const char *TAG = "zb_light";

/* ---------- Shared state ---------- */

static night_light_state_t s_nl_state = {
    .on         = false,
    .level      = DEFAULT_LEVEL,
    .hue        = DEFAULT_HUE,
    .saturation = DEFAULT_SATURATION,
};

/* ---------- Apply current state to LEDs ---------- */

static void apply_state(void)
{
    led_strip_control_update(&s_nl_state);
}

/* ---------- Read restored ZCL attributes and push to LED ---------- */

static void apply_state_from_zcl(void)
{
    esp_zb_zcl_attr_t *attr;

    /* ON/OFF */
    attr = esp_zb_zcl_get_attribute(HA_ENDPOINT,
                                    ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
                                    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                    ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID);
    if (attr && attr->data_p) {
        s_nl_state.on = *(uint8_t *)attr->data_p;
    }

    /* Level */
    attr = esp_zb_zcl_get_attribute(HA_ENDPOINT,
                                    ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL,
                                    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                    ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID);
    if (attr && attr->data_p) {
        s_nl_state.level = *(uint8_t *)attr->data_p;
    }

    /* Hue */
    attr = esp_zb_zcl_get_attribute(HA_ENDPOINT,
                                    ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL,
                                    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                    ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_HUE_ID);
    if (attr && attr->data_p) {
        s_nl_state.hue = *(uint8_t *)attr->data_p;
    }

    /* Saturation */
    attr = esp_zb_zcl_get_attribute(HA_ENDPOINT,
                                    ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL,
                                    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                    ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_SATURATION_ID);
    if (attr && attr->data_p) {
        s_nl_state.saturation = *(uint8_t *)attr->data_p;
    }

    ESP_LOGI(TAG, "Restored state: on=%d level=%d hue=%d sat=%d",
             s_nl_state.on, s_nl_state.level,
             s_nl_state.hue, s_nl_state.saturation);
    apply_state();
}

/* ---------- Attribute write callback ---------- */

static esp_err_t zb_attribute_handler(const esp_zb_zcl_set_attr_value_message_t *message)
{
    if (!message) {
        return ESP_ERR_INVALID_ARG;
    }
    if (message->info.status != ESP_ZB_ZCL_STATUS_SUCCESS) {
        return ESP_OK;
    }

    uint16_t cluster = message->info.cluster;
    uint16_t attr_id = message->attribute.id;
    void    *val     = message->attribute.data.value;

    if (cluster == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF) {
        if (attr_id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID && val) {
            s_nl_state.on = *(bool *)val;
            ESP_LOGI(TAG, "ON/OFF -> %d", s_nl_state.on);
        }
    } else if (cluster == ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL) {
        if (attr_id == ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID && val) {
            s_nl_state.level = *(uint8_t *)val;
            ESP_LOGI(TAG, "Level -> %d", s_nl_state.level);
        }
    } else if (cluster == ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL) {
        if (attr_id == ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_HUE_ID && val) {
            s_nl_state.hue = *(uint8_t *)val;
            ESP_LOGI(TAG, "Hue -> %d", s_nl_state.hue);
        } else if (attr_id == ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_SATURATION_ID && val) {
            s_nl_state.saturation = *(uint8_t *)val;
            ESP_LOGI(TAG, "Saturation -> %d", s_nl_state.saturation);
        }
    }

    apply_state();
    return ESP_OK;
}

/* ---------- Zigbee action handler ---------- */

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    switch (callback_id) {
        case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
            return zb_attribute_handler((const esp_zb_zcl_set_attr_value_message_t *)message);
        default:
            ESP_LOGW(TAG, "Unhandled action callback: 0x%04x", callback_id);
            return ESP_OK;
    }
}

/* ---------- Network steering retry alarm ---------- */

static void steering_retry(uint8_t param)
{
    ESP_LOGW(TAG, "Retrying network steering...");
    esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
}

/* ---------- Zigbee signal handler ---------- */

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    esp_zb_app_signal_type_t sig = *(esp_zb_app_signal_type_t *)signal_struct->p_app_signal;
    esp_err_t err = signal_struct->esp_err_status;

    switch (sig) {
        case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
            ESP_LOGI(TAG, "Zigbee stack initialized");
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
            break;

        case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
        case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
            if (err == ESP_OK) {
                if (sig == ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START) {
                    ESP_LOGI(TAG, "Factory-new device, starting network steering");
                    esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
                } else {
                    ESP_LOGI(TAG, "Device rebooted, restoring state from ZCL");
                    apply_state_from_zcl();
                }
            } else {
                ESP_LOGE(TAG, "BDB initialization failed: %s", esp_err_to_name(err));
            }
            break;

        case ESP_ZB_BDB_SIGNAL_STEERING:
            if (err == ESP_OK) {
                esp_zb_ieee_addr_t extended_pan_id;
                esp_zb_get_extended_pan_id(extended_pan_id);
                ESP_LOGI(TAG, "Joined network. PAN ID: 0x%04hx, Channel: %d",
                         esp_zb_get_pan_id(), esp_zb_get_current_channel());
            } else {
                ESP_LOGW(TAG, "Steering failed (%s), retrying in 1s", esp_err_to_name(err));
                esp_zb_scheduler_alarm(steering_retry, 0, 1000);
            }
            break;

        case ESP_ZB_ZDO_SIGNAL_LEAVE:
            ESP_LOGI(TAG, "Left network, restarting steering");
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            break;

        default:
            ESP_LOGD(TAG, "ZDO signal: 0x%04x, status: %s",
                     sig, esp_err_to_name(err));
            break;
    }
}

/* ---------- Zigbee stack + endpoint configuration ---------- */

static void zb_stack_init(void)
{
    /* Platform config */
    esp_zb_platform_config_t platform_cfg = {
        .radio_config = {
            .radio_mode = ZB_RADIO_MODE_NATIVE,
        },
        .host_config = {
            .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE,
        },
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&platform_cfg));

    /* Stack config: Sleepy End Device */
    esp_zb_cfg_t zb_cfg = {
        .esp_zb_role         = ESP_ZB_DEVICE_TYPE_ED,
        .install_code_policy = false,
        .nwk_cfg.zed_cfg = {
            .ed_timeout  = ESP_ZB_ED_AGING_TIMEOUT_64MIN,
            .keep_alive  = 4000, /* poll parent every 4 s while sleeping */
        },
    };
    esp_zb_init(&zb_cfg);
    esp_zb_sleep_enable(true);

    /* ---- Build cluster list for Extended Color Light ---- */
    esp_zb_color_dimmable_light_cfg_t light_cfg = {
        .basic_cfg = {
            .zcl_version     = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
            .power_source    = 0x03, /* battery */
        },
        .identify_cfg = {
            .identify_time   = ESP_ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE,
        },
        .groups_cfg = {
            .groups_name_support_id = 0,
        },
        .scenes_cfg = {
            .scenes_count    = ESP_ZB_ZCL_SCENES_SCENE_COUNT_DEFAULT_VALUE,
            .current_scene   = ESP_ZB_ZCL_SCENES_CURRENT_SCENE_DEFAULT_VALUE,
            .current_group   = ESP_ZB_ZCL_SCENES_CURRENT_GROUP_DEFAULT_VALUE,
            .scene_valid     = ESP_ZB_ZCL_SCENES_SCENE_VALID_DEFAULT_VALUE,
            .name_support    = 0,
        },
        .on_off_cfg = {
            .on_off          = DEFAULT_POWER_ON_STATE,
        },
        .level_cfg = {
            .current_level   = DEFAULT_LEVEL,
        },
        .color_cfg = {
            .current_x           = ESP_ZB_ZCL_COLOR_CONTROL_CURRENT_X_DEF_VALUE,
            .current_y           = ESP_ZB_ZCL_COLOR_CONTROL_CURRENT_Y_DEF_VALUE,
            .color_mode          = ESP_ZB_ZCL_COLOR_CONTROL_COLOR_MODE_DEFAULT_VALUE,
            .enhanced_color_mode = ESP_ZB_ZCL_COLOR_CONTROL_ENHANCED_COLOR_MODE_DEFAULT_VALUE,
        },
    };

    esp_zb_cluster_list_t *cluster_list =
        esp_zb_color_dimmable_light_clusters_create(&light_cfg);

    /* ---- Add ManufacturerName + ModelIdentifier to Basic cluster ----
     * ZCL character strings are length-prefixed (first byte = length).
     * Z2M reads these during interview to look up the device converter. */
    static char manufacturer[] = {9, 'E', 's', 'p', 'r', 'e', 's', 's', 'i', 'f'};
    static char model_id[]     = {11, 'n', 'i', 'g', 'h', 't', '-', 'l', 'i', 'g', 'h', 't'};
    esp_zb_attribute_list_t *basic_cluster = esp_zb_cluster_list_get_cluster(
        cluster_list, ESP_ZB_ZCL_CLUSTER_ID_BASIC, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_basic_cluster_add_attr(basic_cluster,
        ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, manufacturer);
    esp_zb_basic_cluster_add_attr(basic_cluster,
        ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, model_id);

    /* ---- Endpoint config ---- */
    esp_zb_endpoint_config_t ep_cfg = {
        .endpoint        = HA_ENDPOINT,
        .app_profile_id  = HA_PROFILE_ID,
        .app_device_id   = EXTENDED_COLOR_LIGHT_ID,
        .app_device_version = 0,
    };

    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    esp_zb_ep_list_add_ep(ep_list, cluster_list, ep_cfg);

    esp_zb_device_register(ep_list);

    /* ---- Callbacks ---- */
    esp_zb_core_action_handler_register(zb_action_handler);
    esp_zb_set_primary_network_channel_set(ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK);
}

/* ---------- Task entry point ---------- */

void zigbee_task(void *arg)
{
    zb_stack_init();

    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop(); /* never returns */
}
