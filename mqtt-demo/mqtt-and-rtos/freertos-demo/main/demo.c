#include "demo.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_tls.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "led_strip.h"
#include "mqtt_client.h"

/* Wi-Fi */
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;

/* MQTT */
#if CONFIG_MQTT_BROKER_CERTIFICATE_OVERRIDDEN == 1
static const uint8_t server_cacertificate_start[]  = "-----BEGIN CERTIFICATE-----\n" CONFIG_MQTT_BROKER_CERTIFICATE_OVERRIDE "\n-----END CERTIFICATE-----";
#else
extern const uint8_t server_cacertificate_start[]   asm("_binary_emqxsl_ca_crt_start");
#endif
extern const uint8_t server_cacertificate_end[]   asm("_binary_emqxsl_ca_crt_end");

static subscription_t subscription_list[] = {
    {TOPIC_CMND_POWER, 0, false, handle_cmnd_power},
    {TOPIC_CMND_HUE, 0, false, handle_cmnd_hue},
    {TOPIC_CMND_HSB, 0, false, handle_cmnd_hsb},
    {TOPIC_CMND_MODE, 0, false, handle_cmnd_mode}
};

static subscriptions_t s_subscriptions = {
    .subscription = &subscription_list[0],
    .size = 4
};

static char s_out_msg[128] = {0};

/* LED */
static QueueHandle_t s_led_state_queue = NULL;
static SemaphoreHandle_t s_led_state_lock = NULL;

static volatile led_state_t s_led_state = {
    .power = LED_POWER_ON,
    .hue = 359,
    .saturation = 255,
    .brightness = 255,
    .mode = LED_HUE_RAINBOW,
    .on_time = 500,
    .off_time = 500,
    .speed = RAINBOW_NORMAL
};

/* General */
static const char *TAG = "demo";

led_strip_handle_t configure_led(void)
{
    ESP_LOGI(TAG, "Example configured to blink addressable LED!");

    /* LED strip initialization with the GPIO and pixels number*/
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO,
        .max_leds = LED_STRIP_LED_NUMBERS,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812, // LED strip model
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT, // APB CLK
        .resolution_hz = LED_STRIP_RMT_RES_HZ,
        .flags.with_dma = false,
    };

    led_strip_handle_t led_strip;
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    led_strip_clear(led_strip);

    return led_strip;
}

void set_led_hsb(led_strip_handle_t led_strip, uint16_t hue, uint8_t saturation, uint8_t brightness)
{
    led_strip_set_pixel_hsv(led_strip, 0, hue, saturation, brightness);
    led_strip_refresh(led_strip);
}

void get_value_from_nvs(nvs_handle_t handle, const char *key, void *value, nvs_type_t datatype)
{
    esp_err_t err;

    switch(datatype) {
        case NVS_TYPE_U8:
            err = nvs_get_u8(handle, key, (uint8_t *)value);
            break;
        case NVS_TYPE_U16:
            err = nvs_get_u16(handle, key, (uint16_t *)value);
            break;
        default:
            ESP_LOGW(TAG, "invalid or unsupported data type: %d", datatype);
            return;
    }

    if(err == ESP_OK) {
        ESP_LOGI(TAG, "using value(%s) stored in NVS: %hu", key, datatype == NVS_TYPE_U8 ? *(uint8_t *)value : *(uint16_t *)value);
    }
    else {
        ESP_LOGI(TAG, "using default value(%s): %hu", key,  datatype == NVS_TYPE_U8 ? *(uint8_t *)value : *(uint16_t *)value);
        if(err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "failed to read %s from nvs with error(%s)", key, esp_err_to_name(err));
        }
    }
}

esp_err_t set_value_to_nvs(nvs_handle_t handle, const char *key, void *value, nvs_type_t datatype)
{
    esp_err_t err;

    switch(datatype) {
        case NVS_TYPE_U8:
            err = nvs_set_u8(handle, key, *(uint8_t *)value);
            break;
        case NVS_TYPE_U16:
            err = nvs_set_u16(handle, key, *(uint16_t *)value);
            break;
        default:
            ESP_LOGW(TAG, "invalid or unsupported data type: %d", datatype);
            return ESP_FAIL;
    }

    if(err != ESP_OK) {
        ESP_LOGW(TAG, "failed to write %s to nvs with error(%s)", key, esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

void store_led_state_in_nvs(nvs_handle_t handle, led_state_t *led_state)
{
    set_value_to_nvs(handle, "power", &led_state->power, NVS_TYPE_U8);
    set_value_to_nvs(handle, "hue", &led_state->hue, NVS_TYPE_U16);
    set_value_to_nvs(handle, "saturation", &led_state->saturation, NVS_TYPE_U8);
    set_value_to_nvs(handle, "brightness", &led_state->brightness, NVS_TYPE_U8);
    set_value_to_nvs(handle, "mode", &led_state->mode, NVS_TYPE_U8);
    set_value_to_nvs(handle, "on_time", &led_state->on_time, NVS_TYPE_U16);
    set_value_to_nvs(handle, "off_time", &led_state->off_time, NVS_TYPE_U16);
    set_value_to_nvs(handle, "speed", &led_state->speed, NVS_TYPE_U8);

    esp_err_t err = nvs_commit(handle);
    ESP_LOGI(TAG, "committing updates in NVS ... %s", (err != ESP_OK) ? "Failed!" : "Done!");
}

void init_led_state(void)
{
    nvs_handle_t handle;

    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &handle);
    if(err != ESP_OK) {
        ESP_LOGW(TAG, "error (%s) opening NVS handle!", esp_err_to_name(err));
    }
    else {
        get_value_from_nvs(handle, "power", (void *)&s_led_state.power, NVS_TYPE_U8);
        get_value_from_nvs(handle, "hue", (void *)&s_led_state.hue, NVS_TYPE_U16);
        get_value_from_nvs(handle, "saturation", (void *)&s_led_state.saturation, NVS_TYPE_U8);
        get_value_from_nvs(handle, "brightness", (void *)&s_led_state.brightness, NVS_TYPE_U8);
        get_value_from_nvs(handle, "mode", (void *)&s_led_state.mode, NVS_TYPE_U8);
        get_value_from_nvs(handle, "on_time", (void *)&s_led_state.on_time, NVS_TYPE_U16);
        get_value_from_nvs(handle, "off_time", (void *)&s_led_state.off_time, NVS_TYPE_U16);
        get_value_from_nvs(handle, "speed", (void *)&s_led_state.speed, NVS_TYPE_U8);
    }
}

void sync_led_state(esp_mqtt_client_handle_t client, led_state_t *st)
{
    sprintf(s_out_msg, "%hu,%hhu,%hhu", st->hue, st->saturation, st->brightness);
    if(esp_mqtt_client_enqueue(client, TOPIC_STAT_HSB, s_out_msg, 0, QOS_1, RETAIN, true) >= 0) {
        ESP_LOGI(TAG, "publish to %s: %s", TOPIC_STAT_HSB, s_out_msg);
    }
    if(esp_mqtt_client_enqueue(client, TOPIC_STAT_POWER, st->power ? LED_POWER_ON_STR : LED_POWER_OFF_STR, 0, QOS_1, RETAIN, true) >= 0) {
        ESP_LOGI(TAG, "publish to %s: %s", TOPIC_STAT_POWER, st->power ? LED_POWER_ON_STR : LED_POWER_OFF_STR);
    }

    switch(st->mode) {
        case LED_BLINK:
            sprintf(s_out_msg, "blink,%hu,%hu", st->on_time, st->off_time);
            break;
        case LED_HUE_RAINBOW:
            sprintf(s_out_msg, "hue_rainbow,%s", (st->speed == RAINBOW_SLOW ? "slow" : (st->speed == RAINBOW_NORMAL ? "normal" : "quick")));
            break;
    }
    if(esp_mqtt_client_enqueue(client, TOPIC_STAT_MODE, s_out_msg, 0, QOS_1, RETAIN, true) >= 0) {
        ESP_LOGI(TAG, "publish to %s: %s", TOPIC_STAT_MODE, s_out_msg);
    }
}

void led_task(esp_mqtt_client_handle_t client)
{
    led_strip_handle_t led_strip = configure_led();

    led_state_t led_state = s_led_state;
    uint8_t cmnd_arrived = 0;
    uint16_t ticks = 0;
    uint16_t duration = led_state.on_time;
    bool light = false;
    uint16_t hue = 0;
    nvs_handle_t handle;

    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &handle);
    if(err != ESP_OK) {
        ESP_LOGW(TAG, "error (%s) opening NVS handle!", esp_err_to_name(err));
    }

    while(1) {

        if(xQueueReceive(s_led_state_queue, &led_state, 0) == pdTRUE) {
            cmnd_arrived = 1;

            ticks = 0;
            duration = led_state.on_time;
            hue = 0;
            light = false;
        }

        if(led_state.power == LED_POWER_ON) {
            switch(led_state.mode) {
                case LED_BLINK:
                    if(ticks == 0 && duration != 0) {
                        set_led_hsb(led_strip, led_state.hue, led_state.saturation, light ? 0 : led_state.brightness);
                        light = !light;
                    }

                    if(ticks * LED_TASK_INTERVAL < duration - 1) {
                        ticks++;
                    }
                    else {
                        duration = light ? led_state.off_time : led_state.on_time;
                        ticks = 0;
                    }
                    break;
                case LED_HUE_RAINBOW:
                        set_led_hsb(led_strip, hue, led_state.saturation, led_state.brightness);
                        
                        // Control the speed of the rainbow
                        if(ticks < 2) {
                            ticks++;
                        }
                        else {
                            hue += led_state.speed;
                            ticks = 0;

                            if(hue >= 360) {
                                hue = 0;
                            }
                        }
                        
                    break;
            }
        }
        else {
            set_led_hsb(led_strip, led_state.hue, led_state.saturation, 0);
        }
        
        if(cmnd_arrived) {
            ESP_LOGI(TAG, "update global led state");

            // Update global led state
            xSemaphoreTake(s_led_state_lock, portMAX_DELAY);
            s_led_state = led_state;
            xSemaphoreGive(s_led_state_lock);

            // Store LED state in NVS
            store_led_state_in_nvs(handle, &led_state);
            sync_led_state(client, &led_state);

            cmnd_arrived = 0;
        }

        vTaskDelay(LED_TASK_INTERVAL / portTICK_PERIOD_MS);
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < ESP_WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = ESP_WIFI_SSID,
            .password = ESP_WIFI_PASS,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (pasword len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
             * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 ESP_WIFI_SSID, ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 ESP_WIFI_SSID, ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

esp_err_t send_led_state_to_queue(led_state_t *led_state)
{
    if(xQueueSend(s_led_state_queue, led_state, 0) != pdTRUE) {
        ESP_LOGE(TAG, "failed to send to led state queue");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t handle_cmnd_hsb2(uint16_t *hue, uint8_t *saturation, uint8_t *brightness)
{
    xSemaphoreTake(s_led_state_lock, portMAX_DELAY);
    led_state_t led_state = s_led_state;
    xSemaphoreGive(s_led_state_lock);

    if(hue != NULL) {
        led_state.hue = *hue;
    }

    if(saturation != NULL) {
        led_state.saturation = *saturation;
    }

    if(brightness != NULL) {
        led_state.brightness = *brightness;
    }

    return send_led_state_to_queue(&led_state);
}

static esp_err_t handle_cmnd_power(char *data, int data_len)
{
    xSemaphoreTake(s_led_state_lock, portMAX_DELAY);
    led_state_t led_state = s_led_state;
    xSemaphoreGive(s_led_state_lock);

    if(!strncmp(data, LED_POWER_ON_STR, data_len)) {
        led_state.power = LED_POWER_ON;
    }
    else if(!strncmp(data, LED_POWER_OFF_STR, data_len)) {
        led_state.power = LED_POWER_OFF;
    }
    else {
        ESP_LOGW(TAG, "invalid command: %.*s", data_len, data);
        return ESP_FAIL;
    }

    return send_led_state_to_queue(&led_state);
}

static esp_err_t handle_cmnd_hue(char *data, int data_len)
{
    uint16_t hue = 0;

    if(sscanf(data, "%hu", &hue) < 1) {
        ESP_LOGW(TAG, "invalid command: %.*s", data_len, data);
        return ESP_FAIL;
    }

    if(hue <= 360) {
        return handle_cmnd_hsb2(&hue, NULL, NULL);
    }
    else {
        ESP_LOGW(TAG, "out of range: %d", hue);
        return ESP_FAIL;
    }
}

static esp_err_t handle_cmnd_hsb(char *data, int data_len)
{
    uint16_t hue = 0;
    uint8_t saturation = 0, brightness = 0;

    if(sscanf(data, "%hu,%hhu,%hhu", &hue, &saturation, &brightness) < 3) {
        ESP_LOGW(TAG, "invalid command: %.*s", data_len, data);
        return ESP_FAIL;
    }

    if(hue <= 360) {
        return handle_cmnd_hsb2(&hue, &saturation, &brightness);
    }
    else {
        ESP_LOGW(TAG, "out of range: %.*s", data_len, data);
        return ESP_FAIL;
    }
}

static esp_err_t handle_cmnd_mode(char *data, int data_len)
{
    xSemaphoreTake(s_led_state_lock, portMAX_DELAY);
    led_state_t led_state = s_led_state;
    xSemaphoreGive(s_led_state_lock);
    
    if(!strncmp(data, "blink", 5)) {
        led_state.mode = LED_BLINK;
        if(sscanf(data, "blink,%hu,%hu", &led_state.on_time, &led_state.off_time) < 2) {
            ESP_LOGW(TAG, "invalid command: %.*s", data_len, data);
            return ESP_FAIL;
        }

        led_state.on_time = led_state.on_time - led_state.on_time % LED_TASK_INTERVAL;
        led_state.off_time = led_state.off_time - led_state.off_time % LED_TASK_INTERVAL;
    }
    else if(!strncmp(data, "hue_rainbow,slow", data_len)) {
        led_state.mode = LED_HUE_RAINBOW;
        led_state.speed = RAINBOW_SLOW;
    }
    else if(!strncmp(data, "hue_rainbow,normal", data_len)) {
        led_state.mode = LED_HUE_RAINBOW;
        led_state.speed = RAINBOW_NORMAL;
    }
    else if(!strncmp(data, "hue_rainbow,quick", data_len)) {
        led_state.mode = LED_HUE_RAINBOW;
        led_state.speed = RAINBOW_QUICK;
    }
    else {
        ESP_LOGW(TAG, "invalid command: %.*s", data_len, data);
        return ESP_FAIL;
    }

    return send_led_state_to_queue(&led_state);
}

static void mqtt5_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    subscriptions_t *subscriptions = (subscriptions_t *)handler_args;

    ESP_LOGD(TAG, "free heap size is %" PRIu32 ", minimum %" PRIu32, esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

        if(event->session_present == false) {
            // Re-subscribe
            for(int i = 0; i < subscriptions->size; i++)
            {
                subscriptions->subscription[i].msg_id = esp_mqtt_client_subscribe(client, (char *)subscriptions->subscription[i].topic, 0);
                subscriptions->subscription[i].subscribed = false;
                ESP_LOGI(TAG, "subscribing to topic=%s, msg_id=%d",
                    subscriptions->subscription[i].topic, subscriptions->subscription[i].msg_id);
            }
        }

        // Sync status with the server
        xSemaphoreTake(s_led_state_lock, portMAX_DELAY);
        led_state_t led_state = s_led_state;
        xSemaphoreGive(s_led_state_lock);
        sync_led_state(client, &led_state);

        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        for(int i = 0; i < subscriptions->size; i++) {
            if(subscriptions->subscription[i].msg_id == event->msg_id) {
                ESP_LOGI(TAG, "subscribed to topic=%s, msg_id=%d", subscriptions->subscription[i].topic, event->msg_id);
                break;
            }
        }
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        ESP_LOGI(TAG, "received from %.*s: %.*s", event->topic_len, event->topic, event->data_len, event->data);

        /* The length of the actual requested memory is buffer.size + 1 byte,
         * so event->data[event->data_len] will not read the memory out of bounds. */
        event->data[event->data_len] = '\0';
        for(int i = 0; i < subscriptions->size; i++) {
            uint16_t len = strlen(subscriptions->subscription[i].topic);
            if(!strncmp(subscriptions->subscription[i].topic, event->topic, len < event->topic_len ? len : event->topic_len)) {
                subscriptions->subscription[i].handler(event->data, event->data_len);
                break;
            }
        }
        
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        switch(event->error_handle->error_type) {
            case MQTT_ERROR_TYPE_TCP_TRANSPORT:
                ESP_LOGI(TAG, "last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
                ESP_LOGI(TAG, "last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
                ESP_LOGI(TAG, "last captured errno : %d (%s)",  event->error_handle->esp_transport_sock_errno,
                        strerror(event->error_handle->esp_transport_sock_errno));
                break;
            case MQTT_ERROR_TYPE_SUBSCRIBE_FAILED:
                for(int i = 0; i < subscriptions->size; i++) {
                    if(subscriptions->subscription[i].msg_id == event->msg_id) {
                        ESP_LOGW(TAG, "subscribe to %s failed", subscriptions->subscription[i].topic);
                        break;
                    }
                }
                break;
            case MQTT_ERROR_TYPE_CONNECTION_REFUSED:
                ESP_LOGI(TAG, "connection refused error: 0x%x", event->error_handle->connect_return_code);
                break;
            default:
                ESP_LOGW(TAG, "unknown error type: 0x%x", event->error_handle->error_type);
                break;
        }
        break;
    default:
        ESP_LOGI(TAG, "other event id:%d", event->event_id);
        break;
    }
}

static esp_mqtt_client_handle_t mqtt5_app_start(void)
{
    esp_mqtt5_connection_property_config_t connect_property = {
        .session_expiry_interval = 300,
        .maximum_packet_size = 256,
        .receive_maximum = 65535
    };

    esp_mqtt_client_config_t mqtt5_cfg = {
        .broker.address.uri = MQTT_BROKER_URL,
        .broker.verification.certificate = (const char *)server_cacertificate_start,
        .session.protocol_ver = MQTT_PROTOCOL_V_5,
        .session.disable_clean_session = false,
        .session.keepalive = 60,
        .network.disable_auto_reconnect = false,
        .network.reconnect_timeout_ms = MQTT_RECONNECT_INTERVAL,
        .credentials.username = MQTT_USERNAME,
        .credentials.authentication.password = MQTT_PASSWORD,
        .buffer.size = 256
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt5_cfg);

    esp_mqtt5_client_set_connect_property(client, &connect_property);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt5_event_handler, &s_subscriptions);
    esp_mqtt_client_start(client);

    return client;
}

void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("transport_base", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("transport", ESP_LOG_VERBOSE);
    esp_log_level_set("outbox", ESP_LOG_VERBOSE);

    s_led_state_queue = xQueueCreate(10, sizeof(led_state_t));
    if (!s_led_state_queue) {
        ESP_LOGE(TAG, "create led state queue failed");
        abort();
    }

    s_led_state_lock = xSemaphoreCreateMutex();
    if(!s_led_state_lock) {
        ESP_LOGE(TAG, "create mutex failed");
        abort();
    }

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

    init_led_state();

    esp_mqtt_client_handle_t client = mqtt5_app_start();

    xTaskCreatePinnedToCore((TaskFunction_t)led_task, "led_task", 4096, client, CONFIG_LED_TASK_PRIORITY, NULL, 0);

}


