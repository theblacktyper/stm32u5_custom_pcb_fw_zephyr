#include "dfu_state.h"

#include <zephyr/kernel.h>
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>

#include "camera_runtime.h"

static atomic_t dfu_mode_active = ATOMIC_INIT(0);
#if defined(CONFIG_MCUMGR_SMP_COMMAND_STATUS_HOOKS)
static atomic_t dfu_in_progress = ATOMIC_INIT(0);
static struct mgmt_callback smp_cmd_cb_struct;
#endif
#if defined(CONFIG_MCUMGR_GRP_IMG_STATUS_HOOKS)
static atomic_t dfu_suspend_requested = ATOMIC_INIT(0);
static struct mgmt_callback img_dfu_cb_struct;
#endif

#if defined(CONFIG_MCUMGR_SMP_COMMAND_STATUS_HOOKS)
static enum mgmt_cb_return smp_cmd_cb(uint32_t event, enum mgmt_cb_return prev_status,
				      int32_t *rc, uint16_t *group, bool *abort_more,
				      void *data, size_t data_size)
{
	ARG_UNUSED(prev_status);
	ARG_UNUSED(rc);
	ARG_UNUSED(group);
	ARG_UNUSED(abort_more);
	ARG_UNUSED(data);
	ARG_UNUSED(data_size);

	if (event == MGMT_EVT_OP_CMD_RECV) {
		atomic_set(&dfu_in_progress, 1);
	} else if (event == MGMT_EVT_OP_CMD_DONE) {
#if defined(CONFIG_MCUMGR_GRP_IMG_STATUS_HOOKS)
		if (!atomic_get(&dfu_suspend_requested))
#endif
		{
			atomic_set(&dfu_in_progress, 0);
		}
	}
	return MGMT_CB_OK;
}
#endif

#if defined(CONFIG_MCUMGR_GRP_IMG_STATUS_HOOKS)
static enum mgmt_cb_return img_dfu_cb(uint32_t event, enum mgmt_cb_return prev_status,
				      int32_t *rc, uint16_t *group, bool *abort_more,
				      void *data, size_t data_size)
{
	ARG_UNUSED(prev_status);
	ARG_UNUSED(rc);
	ARG_UNUSED(group);
	ARG_UNUSED(abort_more);
	ARG_UNUSED(data);
	ARG_UNUSED(data_size);

	if (event == MGMT_EVT_OP_IMG_MGMT_DFU_STARTED) {
#if defined(CONFIG_MCUMGR_SMP_COMMAND_STATUS_HOOKS)
		atomic_set(&dfu_in_progress, 1);
#endif
		atomic_set(&dfu_suspend_requested, 1);
		camera_wake_inference_thread();
	}
	return MGMT_CB_OK;
}
#endif

int dfu_state_init(void)
{
#if defined(CONFIG_MCUMGR_SMP_COMMAND_STATUS_HOOKS)
	smp_cmd_cb_struct.callback = smp_cmd_cb;
	smp_cmd_cb_struct.event_id = (MGMT_EVT_OP_CMD_RECV | MGMT_EVT_OP_CMD_DONE);
	mgmt_callback_register(&smp_cmd_cb_struct);
#endif
#if defined(CONFIG_MCUMGR_GRP_IMG_STATUS_HOOKS)
	img_dfu_cb_struct.callback = img_dfu_cb;
	img_dfu_cb_struct.event_id =
		(MGMT_EVT_OP_IMG_MGMT_DFU_STARTED | MGMT_EVT_OP_IMG_MGMT_DFU_STOPPED);
	mgmt_callback_register(&img_dfu_cb_struct);
#endif
	return 0;
}

bool dfu_is_mode_active(void)
{
	return atomic_get(&dfu_mode_active) != 0;
}

bool dfu_is_in_progress(void)
{
#if defined(CONFIG_MCUMGR_SMP_COMMAND_STATUS_HOOKS)
	return atomic_get(&dfu_in_progress) != 0;
#else
	return false;
#endif
}

bool dfu_should_suspend(void)
{
#if defined(CONFIG_MCUMGR_GRP_IMG_STATUS_HOOKS)
	return atomic_get(&dfu_suspend_requested) != 0;
#else
	return false;
#endif
}

void dfu_set_mode_active(bool active)
{
	atomic_set(&dfu_mode_active, active ? 1 : 0);
}
