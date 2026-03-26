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
	LOG_INF("Press Button 2 to start capture...\n");

	return 0;
}
