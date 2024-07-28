#include "demo.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "led_strip.h"
#include "esp_system.h"
#include <uros_network_interfaces.h>
#include <rcl/rcl.h>
#include <rcl/error_handling.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>
#include <demo_interfaces/msg/hsb.h>

#ifdef CONFIG_MICRO_ROS_ESP_XRCE_DDS_MIDDLEWARE
#include <rmw_microros/rmw_microros.h>
#endif

rcl_publisher_t publisher;
demo_interfaces__msg__Hsb pub_msg;
demo_interfaces__msg__Hsb sub_msg;
static led_strip_handle_t s_led_strip;

static led_state_t s_led_state = {
    .hue = 180,
    .saturation = 255,
    .brightness = 255
};

static const char *node_name = CONFIG_MICRO_ROS_APP_NODE_NAME;

#define RCCHECK(fn) { rcl_ret_t temp_rc = fn; if((temp_rc != RCL_RET_OK)){ ESP_LOGE(node_name, "Failed status on line %d: %d. Aborting.", __LINE__, (int)temp_rc); vTaskDelete(NULL); }}
#define RCSOFTCHECK(fn) { rcl_ret_t temp_rc = fn; if((temp_rc != RCL_RET_OK)){ ESP_LOGW(node_name, "Failed status on line %d: %d. Continuing.", __LINE__, (int)temp_rc); }}

void set_led_hsb(led_strip_handle_t led_strip, uint16_t hue, uint8_t saturation, uint8_t brightness)
{
    led_strip_set_pixel_hsv(led_strip, 0, hue, saturation, brightness);
    led_strip_refresh(led_strip);
}

void configure_led(led_strip_handle_t *led_strip, led_state_t *default_state)
{
#ifdef CONFIG_MICRO_ROS_APP_LED
    ESP_LOGI(node_name, "Config addressable LED...");

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

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, led_strip));
    led_strip_clear(*led_strip);

	set_led_hsb(*led_strip, default_state->hue, default_state->saturation, default_state->brightness);
#endif
}

void timer_callback(rcl_timer_t * timer, int64_t last_call_time)
{
	RCLC_UNUSED(last_call_time);
	if (timer != NULL) {
		pub_msg.hue = s_led_state.hue;
		pub_msg.saturation = s_led_state.saturation;
		pub_msg.brightness = s_led_state.brightness;
		rcl_ret_t rc = rcl_publish(&publisher, &pub_msg, NULL);
		if(rc == RCL_RET_OK) {
			ESP_LOGI(node_name, "Published: %hu(hue), %hhu(saturation), %hhu(brightness)", pub_msg.hue, pub_msg.saturation, pub_msg.brightness);
		}
		else {
			ESP_LOGW(node_name, "Error in timer_callback: Message could not be published");
		}
	}
	else {
		ESP_LOGW(node_name, "Error in timer_callback: timer parameter is NULL");
	}
}

void subscriber_callback(const void * msgin)
{
	const demo_interfaces__msg__Hsb *msg = (const demo_interfaces__msg__Hsb *)msgin;
	if (msg == NULL) {
		ESP_LOGW(node_name, "Callback: msg NULL");
	} else {
		ESP_LOGI(node_name, "Received message: %hu(hue), %hhu(saturation), %hhu(brightness)", msg->hue, msg->saturation, msg->brightness);
#ifdef CONFIG_MICRO_ROS_APP_LED
		set_led_hsb(s_led_strip, msg->hue, msg->saturation, msg->brightness);
#endif
		s_led_state.hue = msg->hue;
		s_led_state.saturation = msg->saturation;
		s_led_state.brightness = msg->brightness;
	}
}

void micro_ros_task(void * arg)
{
	configure_led(&s_led_strip, &s_led_state);

	rcl_allocator_t allocator = rcl_get_default_allocator();
	rclc_support_t support;

	rcl_init_options_t init_options = rcl_get_zero_initialized_init_options();
	RCCHECK(rcl_init_options_init(&init_options, allocator));

#ifdef CONFIG_MICRO_ROS_ESP_XRCE_DDS_MIDDLEWARE
	rmw_init_options_t* rmw_options = rcl_init_options_get_rmw_init_options(&init_options);

	// Static Agent IP and port can be used instead of autodisvery.
	RCCHECK(rmw_uros_options_set_udp_address(CONFIG_MICRO_ROS_AGENT_IP, CONFIG_MICRO_ROS_AGENT_PORT, rmw_options));
	//RCCHECK(rmw_uros_discover_agent(rmw_options));
#endif

	// create init_options
	RCCHECK(rclc_support_init_with_options(&support, 0, NULL, &init_options, &allocator));

	// create node
	rcl_node_t node = rcl_get_zero_initialized_node();
	RCCHECK(rclc_node_init_default(&node, node_name, "", &support));

	// create publisher
	RCCHECK(rclc_publisher_init_default(
		&publisher,
		&node,
		ROSIDL_GET_MSG_TYPE_SUPPORT(demo_interfaces, msg, Hsb),
		TOPIC_STAT_HSB));
	ESP_LOGI(node_name, "Created publisher %s.", TOPIC_STAT_HSB);

	// create timer,
	rcl_timer_t timer = rcl_get_zero_initialized_timer();
#ifdef CONFIG_MICRO_ROS_APP_LED
	const unsigned int timer_timeout = CONFIG_MICRO_ROS_APP_LED_STATE_MESSAGE_INTERVAL;
#else
	const unsigned int timer_timeout = 5000;
#endif
	RCCHECK(rclc_timer_init_default(
		&timer,
		&support,
		RCL_MS_TO_NS(timer_timeout),
		timer_callback));
	ESP_LOGI(node_name, "Created timer with timeout %d ms.", timer_timeout);

	// create subscriber
	rcl_subscription_t subscriber = rcl_get_zero_initialized_subscription();
	RCCHECK(rclc_subscription_init_default(
		&subscriber,
		&node,
		ROSIDL_GET_MSG_TYPE_SUPPORT(demo_interfaces, msg, Hsb),
		TOPIC_CMND_HSB));
	ESP_LOGI(node_name, "Created subscriber %s.", TOPIC_CMND_HSB);

	// create executor
	rclc_executor_t executor = rclc_executor_get_zero_initialized_executor();
	RCCHECK(rclc_executor_init(&executor, &support.context, 2, &allocator));

	// add timer and subscriber to executor
	RCCHECK(rclc_executor_add_timer(&executor, &timer));
	RCCHECK(rclc_executor_add_subscription(&executor, &subscriber, &sub_msg, &subscriber_callback, ON_NEW_DATA));

	while(1){
		rclc_executor_spin_some(&executor, RCL_MS_TO_NS(100));
		usleep(10000);
	}

	// free resources
	RCCHECK(rcl_publisher_fini(&publisher, &node));
	RCCHECK(rcl_node_fini(&node));

  	vTaskDelete(NULL);
}

void app_main(void)
{
#if defined(CONFIG_MICRO_ROS_ESP_NETIF_WLAN) || defined(CONFIG_MICRO_ROS_ESP_NETIF_ENET)
    ESP_ERROR_CHECK(uros_network_interface_initialize());
#endif

    //pin micro-ros task in APP_CPU to make PRO_CPU to deal with wifi:
    xTaskCreate(micro_ros_task,
            "uros_task",
            CONFIG_MICRO_ROS_APP_STACK,
            NULL,
            CONFIG_MICRO_ROS_APP_TASK_PRIO,
            NULL);
}
