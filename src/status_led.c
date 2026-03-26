#include "status_led.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include "display_overlay.h"

#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)

struct led {
	struct gpio_dt_spec spec;
	uint8_t num;
};

static const struct led led0 = {
	.spec = GPIO_DT_SPEC_GET_OR(LED0_NODE, gpios, {0}),
	.num = 0,
};

static const struct led led1 = {
	.spec = GPIO_DT_SPEC_GET_OR(LED1_NODE, gpios, {0}),
	.num = 1,
};

int status_led_init(void)
{
	if (gpio_is_ready_dt(&led0.spec)) {
		gpio_pin_configure_dt(&led0.spec, GPIO_OUTPUT);
		gpio_pin_set_dt(&led0.spec, 0);
	}
	if (gpio_is_ready_dt(&led1.spec)) {
		gpio_pin_configure_dt(&led1.spec, GPIO_OUTPUT);
		gpio_pin_set_dt(&led1.spec, 0);
	}
	return 0;
}

void status_led_update_detection_pct(int pct)
{
	int person_detected = (pct >= PERSON_DET_THRES_PCT);
	if (gpio_is_ready_dt(&led0.spec)) {
		gpio_pin_set_dt(&led0.spec, person_detected);
	}
	if (gpio_is_ready_dt(&led1.spec)) {
		gpio_pin_set_dt(&led1.spec, 0);
	}
}
