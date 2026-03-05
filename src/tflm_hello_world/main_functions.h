/*
 * Person detection TFLM API.
 * Run inference on an RGB565 camera frame; model uses 96x96 grayscale.
 */

#ifndef MAIN_FUNCTIONS_H_
#define MAIN_FUNCTIONS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One-time setup: load model and allocate interpreter. Call before run. */
void tflm_person_detection_setup(void);

/*
 * Run inference on an RGB565 frame of size frame_w x frame_h.
 * Uses center 96x96 crop (no scaling) when frame >= 96x96.
 * Fills out_* with int8 scores.  Returns 0 on success.
 */
int tflm_person_detection_run(const uint8_t *frame_rgb565,
			      int frame_w, int frame_h,
			      int8_t *out_person_score,
			      int8_t *out_no_person_score);

/* True after setup succeeded. */
int tflm_person_detection_ready(void);

/*
 * Get output tensor quantization params (scale, zero_point).
 * Call after setup. Returns 0 on success.
 */
int tflm_get_output_quant_params(float *out_scale, int32_t *out_zero_point);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_FUNCTIONS_H_ */
