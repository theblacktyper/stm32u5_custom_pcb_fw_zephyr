/*
 * UI hint for Golioth cloud connection (e.g. preview bar color). Updated from the Golioth client
 * path; safe to read from the camera/display thread.
 */
#ifndef GOLIOTH_UI_H_
#define GOLIOTH_UI_H_

#include <stdbool.h>
#include <stdint.h>

void golioth_ui_set_cloud_connected(bool connected);
bool golioth_ui_cloud_connected(void);
void golioth_ui_set_client_started(bool started);

/** Standby third-line phases (Golioth OTA builds): WiFi → DHCP/client → cloud → local prompt. */
#define GOLIOTH_UI_STANDBY_PHASE_WIFI        0U
#define GOLIOTH_UI_STANDBY_PHASE_CLIENT_CONN 1U
#define GOLIOTH_UI_STANDBY_PHASE_UPDATE      2U
#define GOLIOTH_UI_STANDBY_PHASE_BUTTON2     3U

unsigned golioth_ui_standby_phase_id(void);

struct golioth_ui_standby_line {
	const char *text;
	uint32_t fg;
	uint32_t bg;
};

void golioth_ui_standby_get_line(struct golioth_ui_standby_line *out);

#endif /* GOLIOTH_UI_H_ */
