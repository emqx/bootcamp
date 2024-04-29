#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "sdkconfig.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Wi-Fi */
#define ESP_WIFI_SSID           CONFIG_ESP_WIFI_SSID
#define ESP_WIFI_PASS           CONFIG_ESP_WIFI_PASSWORD
#define ESP_WIFI_MAXIMUM_RETRY  CONFIG_ESP_WIFI_MAXIMUM_RETRY

#if CONFIG_ESP_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define EXAMPLE_H2E_IDENTIFIER ""
#elif CONFIG_ESP_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif
#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

/* MQTT */
#define MQTT_BROKER_URL         CONFIG_MQTT_BROKER_URL
#define MQTT_USERNAME           CONFIG_MQTT_USERNAME
#define MQTT_PASSWORD           CONFIG_MQTT_PASSWORD
#define MQTT_RECONNECT_INTERVAL CONFIG_MQTT_RECONNECT_INTERVAL

#define TOPIC_CMND_POWER "cmnd/led/power"
#define TOPIC_CMND_HUE   "cmnd/led/hue"
#define TOPIC_CMND_HSB   "cmnd/led/hsb"
#define TOPIC_CMND_MODE  "cmnd/led/mode"

#define TOPIC_STAT_POWER "stat/led/power"
#define TOPIC_STAT_HSB   "stat/led/hsb"
#define TOPIC_STAT_MODE  "stat/led/mode"

#define QOS_0 0
#define QOS_1 1
#define QOS_2 2

#define RETAIN 1

/* LED */
#define LED_STRIP_GPIO  CONFIG_LED_STRIP_GPIO
/* Numbers of the LED in the strip */
#define LED_STRIP_LED_NUMBERS 1
/* 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution) */
#define LED_STRIP_RMT_RES_HZ  (10 * 1000 * 1000)

#define LED_POWER_ON_STR "on"
#define LED_POWER_OFF_STR "off"

#define LED_POWER_ON 1
#define LED_POWER_OFF 0

#define LED_TASK_INTERVAL 10

typedef esp_err_t (*message_arrived_handler_t)(char *data, int data_len);

typedef struct {
    const char *topic;
    int msg_id;
    bool subscribed;
    message_arrived_handler_t handler;
} subscription_t;

typedef struct {
    subscription_t *subscription;
    uint8_t size;
} subscriptions_t;

typedef enum {
    LED_BLINK = 1,
    LED_HUE_RAINBOW = 2
} led_mode_t;

typedef enum {
    RAINBOW_SLOW = 1,
    RAINBOW_NORMAL = 4,
    RAINBOW_QUICK = 12
} led_speed_t;

typedef struct {
    uint8_t power;
    uint16_t hue;
    uint8_t saturation;
    uint8_t brightness;
    uint8_t mode;
    uint16_t on_time;
    uint16_t off_time;
    led_speed_t speed;
} led_state_t;

#define STORAGE_NAMESPACE "demo"

static esp_err_t handle_cmnd_power(char *data, int data_len);
static esp_err_t handle_cmnd_hue(char *data, int data_len);
static esp_err_t handle_cmnd_hsb(char *data, int data_len);
static esp_err_t handle_cmnd_mode(char *data, int data_len);

#ifdef __cplusplus
}
#endif