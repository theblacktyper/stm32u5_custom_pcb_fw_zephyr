#include "golioth_capture_prompt_sync.h"

K_SEM_DEFINE(golioth_capture_prompt_sem, 0, 1);

void golioth_capture_prompt_release(void)
{
	k_sem_give(&golioth_capture_prompt_sem);
}
