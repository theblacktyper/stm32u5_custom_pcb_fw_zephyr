#include "camera_runtime.h"

#include <string.h>

#include <zephyr/drivers/display.h>
#include <zephyr/drivers/video.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "camera_pipeline.h"
#include "dfu_state.h"
#include "display_overlay.h"
#include "status_led.h"

#if defined(CONFIG_APP_ENABLE_INFERENCE) && CONFIG_APP_ENABLE_INFERENCE
#include "main_functions.h"
#include "tflm_hello_world/model_settings.h"
#endif

LOG_MODULE_REGISTER(app_stats, LOG_LEVEL_INF);

static volatile uint8_t is_cam_capture_started;
static atomic_t show_camera_frame = ATOMIC_INIT(0);
static K_SEM_DEFINE(capture_sem, 0, 1);

/* FPS measurement (disabled: log printout) */
#if 0
static uint32_t frame_count;
static int64_t fps_start_ms;
static float fps_current;
static float fps_last_logged = -1.0f;
#endif

/* Most recent frame handed off from camera_thread to inference_thread. */
#if defined(CONFIG_APP_ENABLE_INFERENCE) && CONFIG_APP_ENABLE_INFERENCE
static struct video_buffer *inference_vbuf;
K_MUTEX_DEFINE(camera_runtime_inference_vbuf_mutex);
static atomic_t inference_pending = ATOMIC_INIT(0);
#if (CONFIG_VIDEO_BUFFER_POOL_NUM_MAX == 1)
static K_SEM_DEFINE(buffer_returned_sem, 0, 1);
#endif
static K_SEM_DEFINE(inference_frame_ready_sem, 0, 1);
#define INFERENCE_EVERY_N_FRAMES 2//if things slow down, restore this value to 4
static uint32_t inference_frame_counter;
static bool last_frame_handed_off;
static int8_t latest_person_score;
static int8_t latest_no_person_score;
#else
static int8_t latest_person_score;
static int8_t latest_no_person_score;
#endif

#if (CONFIG_VIDEO_BUFFER_POOL_NUM_MAX >= 1)
static uint8_t __aligned(64) camera_frame_buf[CAMERA_FRAME_BYTES];
static struct video_buffer camera_static_vbuf = {
	.buffer = camera_frame_buf,
	.size = sizeof(camera_frame_buf),
	.type = VIDEO_BUF_TYPE_OUTPUT,
};
#if (CONFIG_VIDEO_BUFFER_POOL_NUM_MAX >= 2)
static uint8_t __aligned(64) camera_frame_buf1[CAMERA_FRAME_BYTES];
static struct video_buffer camera_static_vbuf1 = {
	.buffer = camera_frame_buf1,
	.size = sizeof(camera_frame_buf1),
	.type = VIDEO_BUF_TYPE_OUTPUT,
};
#endif
#endif

bool camera_is_capture_started(void)
{
	return is_cam_capture_started != 0;
}

void camera_request_capture_start(void)
{
	k_sem_give(&capture_sem);
}

bool camera_is_showing_frame(void)
{
	return atomic_get(&show_camera_frame) != 0;
}

void camera_wake_inference_thread(void)
{
#if defined(CONFIG_APP_ENABLE_INFERENCE) && CONFIG_APP_ENABLE_INFERENCE
	k_sem_give(&inference_frame_ready_sem);
#endif
}

void camera_runtime_thread(void)
{
	int ret;
	const struct device *disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(disp)) {
		LOG_ERR("> Display device not ready in camera thread");
		return;
	}

	struct video_buffer *vbufs[CONFIG_VIDEO_BUFFER_POOL_NUM_MAX];
#if (CONFIG_VIDEO_BUFFER_POOL_NUM_MAX == 1)
	vbufs[0] = &camera_static_vbuf;
#elif (CONFIG_VIDEO_BUFFER_POOL_NUM_MAX == 2)
	vbufs[0] = &camera_static_vbuf;
	vbufs[1] = &camera_static_vbuf1;
#else
	size_t frame_size = CAMERA_FRAME_BYTES;
	for (int i = 0; i < ARRAY_SIZE(vbufs); i++) {
		vbufs[i] = video_buffer_aligned_alloc(frame_size, CONFIG_VIDEO_BUFFER_POOL_ALIGN, K_FOREVER);
		if (vbufs[i] == NULL) {
			LOG_ERR("> Failed to allocate video buffer %d", i);
			return;
		}
		vbufs[i]->type = VIDEO_BUF_TYPE_OUTPUT;
	}
#endif

	uint8_t *disp_buf = k_malloc(DISPLAY_W * DISPLAY_H * sizeof(uint16_t));
	if (disp_buf == NULL) {
		LOG_ERR("> Failed to allocate camera display buffer");
		return;
	}

	while (k_sem_take(&capture_sem, K_MSEC(500)) != 0) {
		if (dfu_should_suspend()) {
			k_thread_suspend(k_current_get());
		}
	}
	if (!is_cam_capture_started) {
		is_cam_capture_started = 1;
	}

	for (int i = 0; i < ARRAY_SIZE(vbufs); i++) {
		ret = video_enqueue(video_dev, vbufs[i]);
		if (ret < 0) {
			LOG_ERR("> video_enqueue[%d] failed: %d", i, ret);
			return;
		}
	}

#if 0
	frame_count = 0;
	fps_start_ms = k_uptime_get();
#endif
#if (CONFIG_VIDEO_BUFFER_POOL_NUM_MAX == 1) && defined(CONFIG_APP_ENABLE_INFERENCE) && CONFIG_APP_ENABLE_INFERENCE
	static bool first_capture = true;
#endif

	while (1) {
		if (dfu_should_suspend()) {
			k_thread_suspend(k_current_get());
		}
		if (dfu_is_mode_active() || dfu_is_in_progress()) {
			k_msleep(100);
			continue;
		}

		struct video_buffer *vbuf;
#if (CONFIG_VIDEO_BUFFER_POOL_NUM_MAX == 1) && defined(CONFIG_APP_ENABLE_INFERENCE) && CONFIG_APP_ENABLE_INFERENCE
		if (!first_capture && last_frame_handed_off) {
			k_sem_take(&buffer_returned_sem, K_FOREVER);
		}
#endif
		ret = video_stream_start(video_dev, VIDEO_BUF_TYPE_OUTPUT);
		if (ret < 0) {
			k_msleep(100);
			continue;
		}
		ret = video_dequeue(video_dev, &vbuf, K_MSEC(100));
		if (ret < 0) {
			video_stream_stop(video_dev, VIDEO_BUF_TYPE_OUTPUT);
			k_msleep(1);
			continue;
		}
		video_stream_stop(video_dev, VIDEO_BUF_TYPE_OUTPUT);
#if (CONFIG_VIDEO_BUFFER_POOL_NUM_MAX == 1) && defined(CONFIG_APP_ENABLE_INFERENCE) && CONFIG_APP_ENABLE_INFERENCE
		first_capture = false;
#endif

		int8_t person_score = latest_person_score;
		int8_t no_person_score = latest_no_person_score;
		copy_frame_to_display(vbuf->buffer, disp_buf, person_score, no_person_score, true);

		int pct = (int)(person_score + 128) * 100 / 256;
		if (pct < 0) pct = 0;
		if (pct > 100) pct = 100;
		status_led_update_detection_pct(pct);

		atomic_set(&show_camera_frame, 1);
		struct display_buffer_descriptor desc = {
			.buf_size = DISPLAY_W * DISPLAY_H * sizeof(uint16_t),
			.width = DISPLAY_W,
			.height = DISPLAY_H,
			.pitch = DISPLAY_W,
		};
		display_write(disp, 0, 0, &desc, disp_buf);

#if 0
		frame_count++;
		int64_t elapsed = k_uptime_get() - fps_start_ms;
		if (elapsed >= 1000) {
			fps_current = (float)frame_count * 1000.0f / (float)elapsed;
			frame_count = 0;
			fps_start_ms = k_uptime_get();
			if (fps_last_logged < 0 || fabsf(fps_current - fps_last_logged) >= 0.05f) {
				LOG_INF("FPS: %.1f", (double)fps_current);
				fps_last_logged = fps_current;
			}
		}
#endif

#if defined(CONFIG_APP_ENABLE_INFERENCE) && CONFIG_APP_ENABLE_INFERENCE
		bool run_inference = (inference_frame_counter % INFERENCE_EVERY_N_FRAMES) == 0;
		if (run_inference && !atomic_get(&inference_pending)) {
			k_mutex_lock(&camera_runtime_inference_vbuf_mutex, K_FOREVER);
			inference_vbuf = vbuf;
			atomic_set(&inference_pending, 1);
			k_mutex_unlock(&camera_runtime_inference_vbuf_mutex);
			k_sem_give(&inference_frame_ready_sem);
			last_frame_handed_off = true;
		} else {
			video_enqueue(video_dev, vbuf);
			last_frame_handed_off = false;
		}
		inference_frame_counter++;
#else
		video_enqueue(video_dev, vbuf);
#endif
	}
}

void inference_thread_entry(void)
{
#if defined(CONFIG_APP_ENABLE_INFERENCE) && CONFIG_APP_ENABLE_INFERENCE
	tflm_person_detection_setup();
#if 0
	uint32_t inference_completed_count;
	int64_t inference_stats_start_ms;

	inference_completed_count = 0;
	inference_stats_start_ms = k_uptime_get();
#endif
	while (1) {
		if (dfu_should_suspend()) {
			k_thread_suspend(k_current_get());
		}
		k_sem_take(&inference_frame_ready_sem, K_FOREVER);
		struct video_buffer *vbuf = NULL;
		k_mutex_lock(&camera_runtime_inference_vbuf_mutex, K_FOREVER);
		if (atomic_get(&inference_pending) && inference_vbuf != NULL) {
			vbuf = inference_vbuf;
			inference_vbuf = NULL;
			atomic_set(&inference_pending, 0);
		}
		k_mutex_unlock(&camera_runtime_inference_vbuf_mutex);
		if (vbuf == NULL) {
			continue;
		}
		if (dfu_should_suspend() || dfu_is_mode_active() || dfu_is_in_progress()) {
			video_enqueue(video_dev, vbuf);
#if (CONFIG_VIDEO_BUFFER_POOL_NUM_MAX == 1)
			k_sem_give(&buffer_returned_sem);
#endif
			continue;
		}
		if (!tflm_person_detection_ready()) {
			video_enqueue(video_dev, vbuf);
#if (CONFIG_VIDEO_BUFFER_POOL_NUM_MAX == 1)
			k_sem_give(&buffer_returned_sem);
#endif
			continue;
		}

		int8_t person_score = 0;
		int8_t no_person_score = 0;
		int rc = tflm_person_detection_run((const uint8_t *)vbuf->buffer, CAMERA_W, CAMERA_H,
						   &person_score, &no_person_score);
		if (rc == 0) {
			latest_person_score = person_score;
			latest_no_person_score = no_person_score;
		}
#if 0
		inference_completed_count++;
		{
			int64_t elapsed_inf = k_uptime_get() - inference_stats_start_ms;

			if (elapsed_inf >= 1000) {
				float inf_per_sec =
					(float)inference_completed_count * 1000.0f / (float)elapsed_inf;

				LOG_INF("Inference: %.1f/s (completed %u in %" PRId64 " ms)",
					(double)inf_per_sec, inference_completed_count,
					elapsed_inf);
				inference_completed_count = 0;
				inference_stats_start_ms = k_uptime_get();
			}
		}
#endif
		video_enqueue(video_dev, vbuf);
#if (CONFIG_VIDEO_BUFFER_POOL_NUM_MAX == 1)
		k_sem_give(&buffer_returned_sem);
#endif
	}
#else
	while (1) {
		k_sleep(K_FOREVER);
	}
#endif
}
