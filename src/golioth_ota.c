/*
 * Golioth cloud OTA: wait for IPv4 (DHCP), connect DTLS client, run firmware update service.
 * UART MCUmgr DFU remains available as a separate path (see docs/GOLIOTH_OTA.md).
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_mgmt.h>

#include <golioth/client.h>

#include "app_version.h"
#include "dfu_state.h"
#include "fw_update.h"
#include "golioth_ui.h"
#if defined(CONFIG_WIFI_AUTOCONNECT)
#include "wifi_link.h"
#endif

/* Name must differ from Golioth SDK's ota.c (LOG_MODULE_REGISTER(golioth_ota)) — duplicate symbols at link. */
LOG_MODULE_REGISTER(dbg_golioth_ota, LOG_LEVEL_INF);

static K_SEM_DEFINE(ipv4_ready, 0, 1);
static K_SEM_DEFINE(golioth_connected, 0, 1);

static struct net_mgmt_event_callback ipv4_cb;

static void ipv4_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			       struct net_if *iface)
{
	ARG_UNUSED(cb);
	ARG_UNUSED(iface);

	if (mgmt_event == NET_EVENT_IPV4_ADDR_ADD) {
		k_sem_give(&ipv4_ready);
	}
}

static void wait_for_ipv4_address(struct net_if *iface)
{
	net_mgmt_init_event_callback(&ipv4_cb, ipv4_event_handler, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&ipv4_cb);

	if (!net_if_is_up(iface)) {
		int ret = net_if_up(iface);

		if (ret < 0 && ret != -EALREADY) {
			LOG_ERR("net_if_up failed: %d", ret);
			net_mgmt_del_event_callback(&ipv4_cb);
			for (;;) {
				k_sleep(K_FOREVER);
			}
		}
	}

	net_dhcpv4_start(iface);

	if (k_sem_take(&ipv4_ready, K_SECONDS(120)) != 0) {
		LOG_ERR("Timeout waiting for IPv4 (WiFi/DHCP)");
		net_mgmt_del_event_callback(&ipv4_cb);
		for (;;) {
			k_sleep(K_FOREVER);
		}
	}

	net_mgmt_del_event_callback(&ipv4_cb);
}

static void on_client_event(struct golioth_client *client, enum golioth_client_event event,
			     void *arg)
{
	ARG_UNUSED(client);
	ARG_UNUSED(arg);

	if (event == GOLIOTH_CLIENT_EVENT_CONNECTED) {
		golioth_ui_set_cloud_connected(true);
		LOG_DBG("Golioth client connected (cloud indicator: green)");
		k_sem_give(&golioth_connected);
	} else if (event == GOLIOTH_CLIENT_EVENT_DISCONNECTED) {
		golioth_ui_set_cloud_connected(false);
		LOG_DBG("Golioth client disconnected (cloud indicator: white)");
	}
}

static void golioth_ota_state_cb(enum golioth_ota_state state, enum golioth_ota_reason reason,
				 void *user_arg)
{
	ARG_UNUSED(user_arg);
	ARG_UNUSED(reason);

	switch (state) {
	case GOLIOTH_OTA_STATE_DOWNLOADING:
	case GOLIOTH_OTA_STATE_DOWNLOADED:
	case GOLIOTH_OTA_STATE_UPDATING:
		dfu_cloud_ota_set_active(true);
		break;
	case GOLIOTH_OTA_STATE_IDLE:
	default:
		dfu_cloud_ota_set_active(false);
		break;
	}
}

static struct golioth_client_config client_config = {
	.credentials =
		{
			.auth_type = GOLIOTH_TLS_AUTH_TYPE_PSK,
			.psk =
				{
					.psk_id = CONFIG_GOLIOTH_APP_PSK_ID,
					.psk_id_len = sizeof(CONFIG_GOLIOTH_APP_PSK_ID) - 1,
					.psk = CONFIG_GOLIOTH_APP_PSK,
					.psk_len = sizeof(CONFIG_GOLIOTH_APP_PSK) - 1,
				},
		},
};

static void golioth_ota_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

#if defined(CONFIG_WIFI_AUTOCONNECT)
	/* Do not run DHCP until WiFi is associated — avoids ESP-AT / stack races with wifi_autoconnect. */
	wifi_link_wait_ready();
#endif

	struct net_if *iface = net_if_get_default();

	if (iface == NULL) {
		LOG_ERR("No default network interface");
		for (;;) {
			k_sleep(K_FOREVER);
		}
	}

	wait_for_ipv4_address(iface);

	struct golioth_client *client = golioth_client_create(&client_config);

	if (client == NULL) {
		LOG_ERR("golioth_client_create failed");
		for (;;) {
			k_sleep(K_FOREVER);
		}
	}

	golioth_ui_set_client_started(true);

	golioth_client_register_event_callback(client, on_client_event, NULL);

	golioth_fw_update_register_state_change_callback(golioth_ota_state_cb, NULL);

	struct golioth_fw_update_config fw_cfg = {
		.current_version = APP_VERSION,
		.fw_package_name = CONFIG_GOLIOTH_FW_UPDATE_PACKAGE_NAME,
	};

	golioth_fw_update_init_with_config(client, &fw_cfg);

	k_sem_take(&golioth_connected, K_FOREVER);

	for (;;) {
		k_sleep(K_FOREVER);
	}
}

#define GOLIOTH_OTA_THREAD_STACKSIZE 3072
#define GOLIOTH_OTA_THREAD_PRIORITY   10

K_THREAD_DEFINE(golioth_ota_thread_id, GOLIOTH_OTA_THREAD_STACKSIZE, golioth_ota_thread, NULL, NULL,
		NULL, GOLIOTH_OTA_THREAD_PRIORITY, 0, 0);
