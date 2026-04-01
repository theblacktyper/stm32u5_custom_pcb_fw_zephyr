#include "input_buttons.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "camera_runtime.h"
#include "dfu_state.h"

LOG_MODULE_DECLARE(main, LOG_LEVEL_INF);

#define SW0_NODE DT_ALIAS(sw0)
#define SW1_NODE DT_ALIAS(sw1)
#define DFU_MODE_HOLD_MS 3000
#define DFU_MODE_POLL_MS 100

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(SW0_NODE, gpios);
static const struct gpio_dt_spec button_sec = GPIO_DT_SPEC_GET(SW1_NODE, gpios);
static struct gpio_callback button_cb_data;
static struct gpio_callback button_sec_cb_data;

static void start_capture_work_handler(struct k_work *work);
static K_WORK_DEFINE(start_capture_work, start_capture_work_handler);

static void button_pressed_cb(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	if (!camera_is_capture_started()) {
		k_work_submit(&start_capture_work);
	}
}

static void button_sec_pressed_cb(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
}

static void start_capture_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	camera_request_capture_start();
}

void input_buttons_init(void)
{
	int ret;

	if (!gpio_is_ready_dt(&button)) {
		LOG_ERR("Button 2 device not ready");
		return;
	}
	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure button 2: %d", ret);
		return;
	}
	ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure button 2 interrupt: %d", ret);
		return;
	}
	gpio_init_callback(&button_cb_data, button_pressed_cb, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);

	if (!gpio_is_ready_dt(&button_sec)) {
		LOG_ERR("Button 1 device not ready");
		return;
	}
	ret = gpio_pin_configure_dt(&button_sec, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure button 1: %d", ret);
		return;
	}
	ret = gpio_pin_interrupt_configure_dt(&button_sec, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure button 1 interrupt: %d", ret);
		return;
	}
	gpio_init_callback(&button_sec_cb_data, button_sec_pressed_cb, BIT(button_sec.pin));
	gpio_add_callback(button_sec.port, &button_sec_cb_data);
}

void input_dfu_mode_detector_thread(void)
{
	uint32_t hold_count = 0;
	const uint32_t hold_threshold = DFU_MODE_HOLD_MS / DFU_MODE_POLL_MS;

	while (1) {
		k_msleep(DFU_MODE_POLL_MS);
		if (dfu_should_suspend()) {
			k_thread_suspend(k_current_get());
		}
		if (!device_is_ready(button_sec.port)) {
			continue;
		}
		if (dfu_is_mode_active()) {
			hold_count = 0;
			continue;
		}
		int val = gpio_pin_get_dt(&button_sec);
		if (val > 0) {
			hold_count++;
			if (hold_count >= hold_threshold) {
				dfu_set_mode_active(true);
				hold_count = 0;
			}
		} else {
			hold_count = 0;
		}
	}
}
