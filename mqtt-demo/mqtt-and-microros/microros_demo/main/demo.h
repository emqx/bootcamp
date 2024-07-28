#pragma once

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include "sdkconfig.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TOPIC_CMND_HSB   "command/led/hsb"
#define TOPIC_STAT_HSB   "state/led/hsb"

/* LED */
#ifdef CONFIG_MICRO_ROS_APP_LED
#define LED_STRIP_GPIO  CONFIG_MICRO_ROS_APP_LED_STRIP_GPIO
/* Numbers of the LED in the strip */
#define LED_STRIP_LED_NUMBERS 1
/* 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution) */
#define LED_STRIP_RMT_RES_HZ  (10 * 1000 * 1000)
#endif

typedef struct {
    uint16_t hue;
    uint8_t saturation;
    uint8_t brightness;
} led_state_t;

#ifdef __cplusplus
}
#endif