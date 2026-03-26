#ifndef DISPLAY_OVERLAY_H_
#define DISPLAY_OVERLAY_H_

#include <stdbool.h>
#include <stdint.h>

#include "camera_pipeline.h"
#include "display_common.h"

#ifndef USE_160x120_CROP
#define USE_160x120_CROP 1
#endif

#if defined(USE_160x120_CROP) && USE_160x120_CROP && defined(CAMERA_RES_320x240)
#define FRAME_DISP_W 160
#define FRAME_DISP_H 120
#define FRAME_CROP_X0 80
#define FRAME_CROP_Y0 60
#elif (CAMERA_W <= DISPLAY_W && CAMERA_H <= DISPLAY_H)
#define FRAME_DISP_W CAMERA_W
#define FRAME_DISP_H CAMERA_H
#elif (CAMERA_W * DISPLAY_H <= CAMERA_H * DISPLAY_W)
#define FRAME_DISP_H DISPLAY_H
#define FRAME_DISP_W (CAMERA_W * DISPLAY_H / CAMERA_H)
#else
#define FRAME_DISP_W DISPLAY_W
#define FRAME_DISP_H (CAMERA_H * DISPLAY_W / CAMERA_W)
#endif

#define FRAME_X_OFFSET ((DISPLAY_W - FRAME_DISP_W) / 2)
#define FRAME_Y_OFFSET ((DISPLAY_H - FRAME_DISP_H) / 2)
#define PERSON_DET_THRES_PCT 80

void copy_frame_to_display(const uint8_t *src, uint8_t *dst,
			   int8_t person_score, int8_t no_person_score,
			   bool inference_enabled);

#endif /* DISPLAY_OVERLAY_H_ */
