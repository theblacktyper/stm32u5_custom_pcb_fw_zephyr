/*
 * Signals when WiFi association has succeeded (wifi_autoconnect path).
 * Golioth / DHCP should wait on this so DHCP does not run before the link is up.
 */
#ifndef WIFI_LINK_H_
#define WIFI_LINK_H_

#include <stdbool.h>

void wifi_link_wait_ready(void);
void wifi_link_signal_connected(void);

/* True after successful WiFi association (for UI, e.g. preview overlay). */
bool wifi_link_preview_connected(void);

#endif /* WIFI_LINK_H_ */
