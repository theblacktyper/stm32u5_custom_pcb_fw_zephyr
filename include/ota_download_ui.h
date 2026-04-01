/*
 * OTA download progress for standby/updating display (bytes from fw_update thread).
 */
#ifndef OTA_DOWNLOAD_UI_H_
#define OTA_DOWNLOAD_UI_H_

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

void ota_download_ui_set_progress(size_t downloaded, size_t total);
void ota_download_ui_clear(void);
/** True while rebooting into new image (title: "Applying Update"). */
void ota_download_ui_set_applying(bool applying);
bool ota_download_ui_is_applying(void);
unsigned ota_download_ui_percent(void);
bool ota_download_ui_has_total(void);

#endif /* OTA_DOWNLOAD_UI_H_ */
