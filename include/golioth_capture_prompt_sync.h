#ifndef GOLIOTH_CAPTURE_PROMPT_SYNC_H_
#define GOLIOTH_CAPTURE_PROMPT_SYNC_H_

#include <zephyr/kernel.h>

/*
 * fw_update gives this after "Golioth client stopped..." (idle disconnect) and
 * before starting an OTA download, so main can print "Press Button 2..." after
 * cloud teardown / in the download case.
 */
extern struct k_sem golioth_capture_prompt_sem;

void golioth_capture_prompt_release(void);

#endif /* GOLIOTH_CAPTURE_PROMPT_SYNC_H_ */
