#include "ota_download_ui.h"

#include <zephyr/sys/atomic.h>

static atomic_t ota_bytes_done;
static atomic_t ota_bytes_total;

void ota_download_ui_set_progress(size_t downloaded, size_t total)
{
	atomic_set(&ota_bytes_done, (uint32_t)downloaded);
	atomic_set(&ota_bytes_total, (uint32_t)total);
}

void ota_download_ui_clear(void)
{
	atomic_set(&ota_bytes_done, 0);
	atomic_set(&ota_bytes_total, 0);
}

bool ota_download_ui_has_total(void)
{
	return atomic_get(&ota_bytes_total) != 0;
}

unsigned ota_download_ui_percent(void)
{
	uint32_t t = (uint32_t)atomic_get(&ota_bytes_total);
	uint32_t d = (uint32_t)atomic_get(&ota_bytes_done);

	if (t == 0U) {
		return 0U;
	}
	return (unsigned)((uint64_t)d * 100ULL / (uint64_t)t);
}
