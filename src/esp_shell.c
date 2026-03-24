/*
 * Custom shell commands for querying the ESP-AT WiFi module.
 *
 * Uses the driver's internal modem context (esp_driver_data) to send
 * AT commands and capture responses.  This couples to the esp_at driver
 * internals — acceptable for a board-level application.
 *
 * Compiled out when CONFIG_SHELL is disabled (e.g. MCUmgr-only builds).
 */

#include <zephyr/kernel.h>

#if defined(CONFIG_SHELL)

#include <zephyr/shell/shell.h>

/* ESP-AT driver private header — gives us esp_driver_data + esp_cmd_send */
#include "esp.h"

/* Shared buffer for capturing multi-line AT+GMR response */
static char gmr_buf[256];
static size_t gmr_offset;

static int on_cmd_gmr_line(struct modem_cmd_handler_data *data, uint16_t len,
			   uint8_t **argv, uint16_t argc)
{
	ARG_UNUSED(data);
	ARG_UNUSED(len);

	for (uint16_t i = 0; i < argc; i++) {
		int n = snprintk(gmr_buf + gmr_offset,
				 sizeof(gmr_buf) - gmr_offset,
				 "%s%s", (i > 0) ? " " : "", argv[i]);
		if (n > 0) {
			gmr_offset += n;
		}
	}

	if (gmr_offset < sizeof(gmr_buf) - 1) {
		gmr_buf[gmr_offset++] = '\n';
	}

	return 0;
}

static int cmd_esp_info(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	struct esp_data *dev = &esp_driver_data;

	/*
	 * Temporary command handlers that capture each response line from
	 * AT+GMR.  The ESP module returns lines like:
	 *   AT version:1.7.5.0(...)
	 *   SDK version:3.0.5(...)
	 *   compile time:...
	 *   Bin version:...
	 *   OK
	 */
	const struct modem_cmd handlers[] = {
		MODEM_CMD("AT version:", on_cmd_gmr_line, 1U, ""),
		MODEM_CMD("SDK version:", on_cmd_gmr_line, 1U, ""),
		MODEM_CMD("Compile time", on_cmd_gmr_line, 1U, ""),
		MODEM_CMD("Bin version:", on_cmd_gmr_line, 1U, ""),
	};

	gmr_offset = 0;
	memset(gmr_buf, 0, sizeof(gmr_buf));

	int ret = esp_cmd_send(dev, handlers, ARRAY_SIZE(handlers),
			       "AT+GMR", ESP_CMD_TIMEOUT);
	if (ret < 0) {
		shell_error(sh, "AT+GMR failed: %d", ret);
		return ret;
	}

	if (gmr_offset > 0) {
		gmr_buf[gmr_offset] = '\0';
		shell_print(sh, "%s", gmr_buf);
	} else {
		shell_warn(sh, "No version info returned");
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(esp_cmds,
	SHELL_CMD_ARG(info, NULL, "Query ESP-AT firmware version (AT+GMR)",
		      cmd_esp_info, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(esp, &esp_cmds, "ESP-AT module commands", NULL);

#endif /* CONFIG_SHELL */
