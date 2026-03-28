/*
 * Binary handoff: wifi_autoconnect signals once associated; cloud stack waits before DHCP/DTLS.
 */

#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include "wifi_link.h"

static K_SEM_DEFINE(wifi_link_ready_sem, 0, 1);
static atomic_t wifi_preview_connected;

void wifi_link_wait_ready(void)
{
	k_sem_take(&wifi_link_ready_sem, K_FOREVER);
}

void wifi_link_signal_connected(void)
{
	atomic_set(&wifi_preview_connected, 1);
	k_sem_give(&wifi_link_ready_sem);
}

bool wifi_link_preview_connected(void)
{
	return atomic_get(&wifi_preview_connected) != 0;
}
