#ifndef DFU_STATE_H_
#define DFU_STATE_H_

#include <stdbool.h>

int dfu_state_init(void);
bool dfu_is_mode_active(void);
bool dfu_is_in_progress(void);
bool dfu_should_suspend(void);
void dfu_set_mode_active(bool active);

#endif /* DFU_STATE_H_ */
