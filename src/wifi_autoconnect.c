/*
 * WiFi auto-connect: attempt to join one of up to two networks configured
 * via Kconfig (CONFIG_WIFI_AUTOCONNECT_SSID / _PSK, _SSID2 / _PSK2).
 *
 * Runs in its own low-priority thread so it never blocks camera, display,
 * or inference.  On each retry round the routine tries the primary network
 * first, then the secondary (if configured).  Retries continue for
 * CONFIG_WIFI_AUTOCONNECT_RETRY_TIMEOUT_S seconds, then gives up until
 * the next power cycle.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_event.h>

LOG_MODULE_REGISTER(wifi_autoconnect, LOG_LEVEL_INF);

#if defined(CONFIG_WIFI_AUTOCONNECT)

#define RETRY_TIMEOUT_S    CONFIG_WIFI_AUTOCONNECT_RETRY_TIMEOUT_S
#define CONNECT_ATTEMPT_MS 8000
#define SCAN_DWELL_MS      5000

struct wifi_network {
	const char *ssid;
	const char *psk;
};

static const struct wifi_network networks[] = {
	{ CONFIG_WIFI_AUTOCONNECT_SSID,  CONFIG_WIFI_AUTOCONNECT_PSK  },
#if defined(CONFIG_WIFI_AUTOCONNECT_SSID2)
	{ CONFIG_WIFI_AUTOCONNECT_SSID2, CONFIG_WIFI_AUTOCONNECT_PSK2 },
#endif
};

#define NUM_NETWORKS ARRAY_SIZE(networks)

static K_SEM_DEFINE(wifi_event_sem, 0, 1);
static volatile int wifi_connect_status;
static struct net_mgmt_event_callback wifi_mgmt_cb;

static void wifi_event_handler(struct net_mgmt_event_callback *cb,
			       uint64_t mgmt_event, struct net_if *iface)
{
	if (mgmt_event == NET_EVENT_WIFI_CONNECT_RESULT) {
		const struct wifi_status *st =
			(const struct wifi_status *)cb->info;

		wifi_connect_status = st->status;
		k_sem_give(&wifi_event_sem);
	}
}

static struct net_if *get_wifi_iface(void)
{
	struct net_if *iface = net_if_get_first_wifi();

	if (iface == NULL) {
		iface = net_if_get_default();
	}
	return iface;
}

static int try_connect(struct net_if *iface, const struct wifi_network *net)
{
	struct wifi_connect_req_params params = { 0 };

	params.ssid = (const uint8_t *)net->ssid;
	params.ssid_length = strlen(net->ssid);
	params.channel = WIFI_CHANNEL_ANY;
	params.band = WIFI_FREQ_BAND_UNKNOWN;

	if (strlen(net->psk) > 0) {
		params.psk = (const uint8_t *)net->psk;
		params.psk_length = strlen(net->psk);
		params.security = WIFI_SECURITY_TYPE_PSK;
	} else {
		params.security = WIFI_SECURITY_TYPE_NONE;
	}

	k_sem_reset(&wifi_event_sem);
	wifi_connect_status = -1;

	int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface,
			   &params, sizeof(params));
	if (ret) {
		LOG_WRN("Connect request failed: %d", ret);
		return ret;
	}

	if (k_sem_take(&wifi_event_sem, K_MSEC(CONNECT_ATTEMPT_MS + SCAN_DWELL_MS)) != 0) {
		LOG_WRN("Connect timed out waiting for result");
		return -ETIMEDOUT;
	}

	return wifi_connect_status;
}

static int count_configured_networks(void)
{
	int count = 0;

	for (int i = 0; i < (int)NUM_NETWORKS; i++) {
		if (strlen(networks[i].ssid) > 0) {
			count++;
		}
	}
	return count;
}

static void wifi_autoconnect_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	int num_nets = count_configured_networks();

	if (num_nets == 0) {
		LOG_INF("No SSIDs configured — skipping auto-connect");
		return;
	}

	net_mgmt_init_event_callback(&wifi_mgmt_cb, wifi_event_handler,
				     NET_EVENT_WIFI_CONNECT_RESULT);
	net_mgmt_add_event_callback(&wifi_mgmt_cb);

	struct net_if *iface = get_wifi_iface();

	if (iface == NULL) {
		LOG_ERR("No WiFi interface found");
		return;
	}

	k_msleep(2000);

	LOG_INF("Auto-connect: %d network(s) configured, retry timeout %d s",
		num_nets, RETRY_TIMEOUT_S);

	int64_t deadline = k_uptime_get() + (int64_t)RETRY_TIMEOUT_S * 1000;
	int attempt = 0;

	while (true) {
		attempt++;

		for (int i = 0; i < (int)NUM_NETWORKS; i++) {
			if (strlen(networks[i].ssid) == 0) {
				continue;
			}

			LOG_INF("Attempt %d: trying \"%s\" ...",
				attempt, networks[i].ssid);

			int ret = try_connect(iface, &networks[i]);

			if (ret == 0) {
				LOG_INF("WiFi connected to \"%s\"",
					networks[i].ssid);
				return;
			}

			LOG_WRN("Failed to connect to \"%s\" (err %d)",
				networks[i].ssid, ret);
		}

		if (RETRY_TIMEOUT_S == 0) {
			break;
		}

		if (k_uptime_get() >= deadline) {
			break;
		}

		int64_t remaining = deadline - k_uptime_get();

		if (remaining <= 0) {
			break;
		}

		int64_t wait = (attempt < 3) ? 3000 : 5000;

		if (wait > remaining) {
			wait = remaining;
		}

		k_msleep((int32_t)wait);
	}

	LOG_WRN("Gave up WiFi auto-connect after %d round(s)", attempt);
}

#define WIFI_AUTOCONNECT_STACKSIZE 2048
#define WIFI_AUTOCONNECT_PRIORITY  14

K_THREAD_DEFINE(wifi_autoconnect_id, WIFI_AUTOCONNECT_STACKSIZE,
		wifi_autoconnect_thread, NULL, NULL, NULL,
		WIFI_AUTOCONNECT_PRIORITY, 0, 0);

#endif /* CONFIG_WIFI_AUTOCONNECT */
