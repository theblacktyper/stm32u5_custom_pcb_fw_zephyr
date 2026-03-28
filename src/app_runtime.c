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
#if defined(CONFIG_GOLIOTH_OTA)
#include "golioth_ui.h"
#endif

LOG_MODULE_DECLARE(main, LOG_LEVEL_INF);

/* size of stack area used by threads */
#define DEFAULT_STACKSIZE    1024
#define INFERENCE_STACKSIZE  2048
#define CAMERA_STACKSIZE     1536
#define DFU_DETECTOR_STACKSIZE 512

/*
 * Priorities: lower number = higher priority (preempts higher numbers).
 * ESP-AT defaults RX + workq to priority 7 — same as camera was, so four app threads
 * (camera/display/inference/DFU) time-sliced with WiFi I/O and starved the link →
 * -ETIMEDOUT / send failures once TFLM + capture run. Keep multimedia below WiFi driver.
 */
#define PRIORITY_CAMERA   11

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

#if defined(CONFIG_GOLIOTH_OTA)
	unsigned standby_phase_prev = golioth_ui_standby_phase_id();
#endif

	while (1) {
#if defined(CONFIG_MCUMGR_SMP_COMMAND_STATUS_HOOKS) || defined(CONFIG_GOLIOTH_OTA)
		/* "Updating Firmware": wired SMP image + SW1 DFU mode, or cloud OTA from first block onward.
		 * Re-render when progress changes; invalidate on enter/leave so static paint state resets.
		 */
		{
			static bool display_prev_updating;
			const bool now_updating = dfu_show_fw_updating_screen();

			if (now_updating != display_prev_updating) {
				display_screen_updating_invalidate();
			}
			display_prev_updating = now_updating;

			if (now_updating) {
				if (screen_ctx.text_buf) {
					display_screen_render_updating(display_dev, &capabilities,
								       &screen_ctx);
				}
				k_msleep(100);
				continue;
			}
		}
#endif
		if (dfu_is_mode_active()) {
			display_screen_render_dfu_mode(display_dev, &capabilities, &screen_ctx);
			while (dfu_is_mode_active()) {
				k_msleep(1000);
			}
			continue;
		}
		if (camera_is_showing_frame()) {
			k_msleep(200);
			continue;
		}

#if defined(CONFIG_GOLIOTH_OTA)
		{
			const unsigned p = golioth_ui_standby_phase_id();

			if (p != standby_phase_prev) {
				standby_phase_prev = p;
				if (screen_ctx.text_buf) {
					display_screen_render_boot(display_dev, &capabilities, &screen_ctx);
				}
			}
			if (p == GOLIOTH_UI_STANDBY_PHASE_BUTTON2) {
				k_msleep(screen_ctx.grey_scale_sleep);
			} else {
				k_msleep(200);
			}
		}
#else
		k_msleep(screen_ctx.grey_scale_sleep);
#endif
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
