/*
 * Person detection TFLM API.
 * Run inference on 160x120 RGB565 frames; model uses 96x96 grayscale center crop.
 */

#ifndef MAIN_FUNCTIONS_H_
#define MAIN_FUNCTIONS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One-time setup: load model and allocate interpreter. Call before run. */
void tflm_person_detection_setup(void);

/* Run inference on a 160x120 RGB565 frame. Fills out_* with int8 scores. Returns 0 on success. */
int tflm_person_detection_run(const uint8_t *frame_rgb565_160x120,
			      int8_t *out_person_score,
			      int8_t *out_no_person_score);

/* True after setup succeeded. */
int tflm_person_detection_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_FUNCTIONS_H_ */
