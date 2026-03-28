#ifndef DFU_STATE_H_
#define DFU_STATE_H_

#include <stdbool.h>

int dfu_state_init(void);
bool dfu_is_mode_active(void);
bool dfu_is_in_progress(void);
bool dfu_should_suspend(void);
void dfu_set_mode_active(bool active);

/* True only when the "Updating FW..." display should show (wired backup path or cloud first block). */
bool dfu_show_fw_updating_screen(void);

#if defined(CONFIG_GOLIOTH_OTA)
void dfu_cloud_ota_set_active(bool active);
void dfu_cloud_ota_fw_screen_set(bool active);
#endif

#endif /* DFU_STATE_H_ */
