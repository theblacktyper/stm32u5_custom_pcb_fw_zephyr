#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdbool.h>

#include "camera_runtime.h"
#include "dfu_state.h"
#include "display_screen.h"
#include "input_buttons.h"

LOG_MODULE_DECLARE(main, LOG_LEVEL_INF);

/* size of stack area used by threads */
#define DEFAULT_STACKSIZE    1024
#define INFERENCE_STACKSIZE  2048
#define CAMERA_STACKSIZE     1536
#define DFU_DETECTOR_STACKSIZE 512

/* scheduling priority: lower number = higher priority in Zephyr */
#define EQUAL_PRIORITY    7
#define PRIORITY_CAMERA   EQUAL_PRIORITY

static void display_thread(void)
{
	const struct device *display_dev;
	struct display_capabilities capabilities;
	struct display_screen_ctx screen_ctx = {0};

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Device %s not found. Aborting sample.",
			display_dev->name);
		return;
	}

	if (display_screen_prepare(display_dev, &capabilities, &screen_ctx) != 0) {
		LOG_ERR("Display screen prepare failed");
		return;
	}
	if (display_screen_render_boot(display_dev, &capabilities, &screen_ctx) != 0) {
		LOG_WRN("Could not render boot text");
	}

#if defined(CONFIG_MCUMGR_SMP_COMMAND_STATUS_HOOKS)
	static bool updating_shown;
#endif
	while (1) {
		if (dfu_is_mode_active()) {
			display_screen_render_dfu_mode(display_dev, &capabilities, &screen_ctx);
			while (dfu_is_mode_active()) {
				k_msleep(1000);
			}
			continue;
		}
#if defined(CONFIG_MCUMGR_SMP_COMMAND_STATUS_HOOKS)
		if (dfu_is_in_progress()) {
			if (!updating_shown && screen_ctx.text_buf) {
				updating_shown = true;
				display_screen_render_updating(display_dev, &capabilities, &screen_ctx);
			}
			k_msleep(100);
			continue;
		}
		updating_shown = false;
#endif
		if (camera_is_showing_frame()) {
			k_msleep(200);
			continue;
		}

		k_msleep(screen_ctx.grey_scale_sleep);
	}
}

#if defined(CONFIG_APP_ENABLE_INFERENCE) && CONFIG_APP_ENABLE_INFERENCE
K_THREAD_DEFINE(inference_id, INFERENCE_STACKSIZE, inference_thread_entry, NULL, NULL, NULL,
		PRIORITY_CAMERA, 0, 0);
#endif /* CONFIG_APP_ENABLE_INFERENCE */
K_THREAD_DEFINE(display_id, DEFAULT_STACKSIZE, display_thread, NULL, NULL, NULL,
		PRIORITY_CAMERA, 0, 0);
K_THREAD_DEFINE(camera_id, CAMERA_STACKSIZE, camera_runtime_thread, NULL, NULL, NULL,
		PRIORITY_CAMERA, 0, 0);
K_THREAD_DEFINE(dfu_mode_detector_id, DFU_DETECTOR_STACKSIZE, input_dfu_mode_detector_thread, NULL, NULL, NULL,
		PRIORITY_CAMERA, 0, 0);
