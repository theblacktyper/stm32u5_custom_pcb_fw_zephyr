#include "golioth_ui.h"

#include "display_common.h"
#if defined(CONFIG_WIFI_AUTOCONNECT)
#include "wifi_link.h"
#endif

#include <zephyr/sys/atomic.h>

static atomic_t golioth_cloud_connected;
static atomic_t golioth_has_connected_once;
static atomic_t golioth_client_started;
static atomic_t golioth_reboot_pending_after_ota_confirm;

void golioth_ui_set_cloud_connected(bool connected)
{
	atomic_set(&golioth_cloud_connected, connected ? 1 : 0);
	if (connected) {
		atomic_set(&golioth_has_connected_once, 1);
	}
}

bool golioth_ui_cloud_connected(void)
{
	return atomic_get(&golioth_cloud_connected) != 0;
}

void golioth_ui_set_client_started(bool started)
{
	atomic_set(&golioth_client_started, started ? 1 : 0);
}

void golioth_ui_set_pending_reboot_after_ota_confirm(void)
{
	atomic_set(&golioth_reboot_pending_after_ota_confirm, 1);
}

bool golioth_ui_consume_pending_reboot_after_ota_confirm(void)
{
	return atomic_cas(&golioth_reboot_pending_after_ota_confirm, 1, 0);
}

static uint8_t standby_compute_phase(void)
{
#if defined(CONFIG_WIFI_AUTOCONNECT)
	const bool wifi_ok = wifi_link_preview_connected();
#else
	const bool wifi_ok = true;
#endif

	if (!wifi_ok) {
		return (uint8_t)GOLIOTH_UI_STANDBY_PHASE_WIFI;
	}
	if (atomic_get(&golioth_client_started) == 0) {
		return (uint8_t)GOLIOTH_UI_STANDBY_PHASE_CLIENT_CONN;
	}

	const bool connected = atomic_get(&golioth_cloud_connected) != 0;
	const bool has_connected = atomic_get(&golioth_has_connected_once) != 0;

	/* Update? until first disconnect after client started (includes DTLS before first connect). */
	if (!has_connected || connected) {
		return (uint8_t)GOLIOTH_UI_STANDBY_PHASE_UPDATE;
	}

	return (uint8_t)GOLIOTH_UI_STANDBY_PHASE_BUTTON2;
}

unsigned golioth_ui_standby_phase_id(void)
{
	return (unsigned)standby_compute_phase();
}

void golioth_ui_standby_get_line(struct golioth_ui_standby_line *out)
{
	if (!out) {
		return;
	}

	switch (standby_compute_phase()) {
	case GOLIOTH_UI_STANDBY_PHASE_WIFI:
		out->text = "Connecting WiFi";
		out->fg = COLOR_YELLOW;
		out->bg = COLOR_BLACK;
		break;
	case GOLIOTH_UI_STANDBY_PHASE_CLIENT_CONN:
		out->text = "Connecting Client";
		out->fg = COLOR_YELLOW;
		out->bg = COLOR_BLACK;
		break;
	case GOLIOTH_UI_STANDBY_PHASE_UPDATE:
		out->text = "Checking for Update";
		out->fg = COLOR_YELLOW;
		out->bg = COLOR_BLACK;
		break;
	case GOLIOTH_UI_STANDBY_PHASE_BUTTON2:
	default:
		out->text = "Firmware up-to-date";
		out->fg = COLOR_YELLOW;
		out->bg = COLOR_BLACK;
		break;
	}
}
