#ifndef CAMERA_PIPELINE_H_
#define CAMERA_PIPELINE_H_

#include <zephyr/device.h>

/*
 * Camera resolution toggle:
 *   Define CAMERA_RES_320x240 for 320x240, otherwise 160x120.
 *   Can be overridden via compiler definitions.
 */
#ifndef CAMERA_RES_320x240
#define CAMERA_RES_320x240
#endif

#if defined(CAMERA_RES_320x240)
#define CAMERA_W 320
#define CAMERA_H 240
#else
#define CAMERA_W 160
#define CAMERA_H 120
#endif

#define CAMERA_FRAME_BYTES (CAMERA_W * CAMERA_H * sizeof(uint16_t))

extern const struct device *ov5640;
extern const struct device *video_dev;

int camera_pipeline_configure(void);
void camera_enable_scaler(void);

#endif /* CAMERA_PIPELINE_H_ */
