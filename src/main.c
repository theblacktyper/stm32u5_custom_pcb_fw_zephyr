/*
 * Copyright (c) 2019 Jan Van Winkel <jan.van_winkel@dxplore.eu>
 *
 * Based on ST7789V sample:
 * Copyright (c) 2019 Marc Reilly
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
#include "app_version.h"
LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#if defined(CONFIG_MCUMGR_SMP_COMMAND_STATUS_HOOKS) || defined(CONFIG_MCUMGR_GRP_IMG_STATUS_HOOKS)
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#endif
#include "camera_pipeline.h"
#include "dfu_state.h"
#include "input_buttons.h"
#include "status_led.h"
#if defined(CONFIG_GOLIOTH_OTA)
#include "golioth_capture_prompt_sync.h"
#endif

int main(void)
{
	LOG_INF("===== System Information =====");
	// LOG_INF("Board: %s", CONFIG_BOARD);
	// LOG_INF("Zephyr: %s", KERNEL_VERSION_STRING);
	LOG_INF("Clock: %.2f MHz", 0.000001 * sys_clock_hw_cycles_per_sec());
	LOG_INF("App version: %s", APP_VERSION);
	LOG_INF("Build: " __DATE__ " " __TIME__);
	LOG_INF("==============================\n");

	input_buttons_init();
	status_led_init();
	dfu_state_init();
	int ret = camera_pipeline_configure();
	if (ret != 0) {
		return ret;
	}

	// LOG_INF("====== Threads STARTING ======");
#if defined(CONFIG_GOLIOTH_OTA)
	/* After fw_update logs (e.g. "Golioth client stopped..." on idle disconnect). */
	if (k_sem_take(&golioth_capture_prompt_sem, K_SECONDS(120)) != 0) {
		LOG_WRN("Timed out waiting for fw_update; printing capture prompt anyway");
	}
#endif
	LOG_INF("Press Button 2 to start capture...\n");

	return 0;
}
