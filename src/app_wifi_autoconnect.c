/*
 * Boot-time STA connect (replaces Golioth sample WiFi when APP_WIFI_AUTOCONNECT=y).
 * Deferred slightly so the ESP-AT WiFi driver can finish init before NET_REQUEST_WIFI_CONNECT.
 */

#include <errno.h>

#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>

LOG_MODULE_REGISTER(app_wifi_autoconnect, LOG_LEVEL_INF);

static uint8_t wifi_ssid[] = CONFIG_APP_WIFI_AUTOCONNECT_SSID;
static uint8_t wifi_psk[] = CONFIG_APP_WIFI_AUTOCONNECT_PSK;

static void app_wifi_connect_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(app_wifi_connect_work, app_wifi_connect_work_handler);

static void app_wifi_connect_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	struct net_if *iface = net_if_get_first_wifi();

	if (iface == NULL) {
		LOG_ERR("No WiFi interface");
		return;
	}

	size_t ssid_len = sizeof(wifi_ssid) - 1U;
	size_t psk_len = sizeof(wifi_psk) - 1U;

	struct wifi_connect_req_params params = {
		.ssid = wifi_ssid,
		.ssid_length = ssid_len,
		.psk = wifi_psk,
		.psk_length = psk_len,
		.channel = WIFI_CHANNEL_ANY,
		.security = psk_len > 0U ? WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE,
	};

	int ret = net_if_up(iface);

	if (ret < 0 && ret != -EALREADY) {
		LOG_ERR("net_if_up failed: %d", ret);
	}

	LOG_INF("WiFi autoconnect: requesting association");
	ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
	if (ret < 0 && ret != -EALREADY) {
		LOG_ERR("WiFi connect request failed: %d", ret);
	}
}

static int app_wifi_autoconnect_init(void)
{
	/* Defer: ESP-AT / net_if may not be ready in early APPLICATION init. */
	(void)k_work_schedule(&app_wifi_connect_work, K_MSEC(150));
	return 0;
}

/*
 * Must use a priority that exists in the linker init table. Expressions like
 * CONFIG_WIFI_INIT_PRIORITY + N can land on an unregistered slot and fail with
 * ld: "Undefined initialization levels used".
 */
SYS_INIT(app_wifi_autoconnect_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
