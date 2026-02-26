/*
 * Copyright 2020 The TensorFlow Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef TENSORFLOW_LITE_MICRO_EXAMPLES_HELLO_WORLD_MAIN_FUNCTIONS_H_
#define TENSORFLOW_LITE_MICRO_EXAMPLES_HELLO_WORLD_MAIN_FUNCTIONS_H_

#ifdef __cplusplus
extern "C" {
#endif

/* One-time setup: load model and allocate interpreter. Call before fill or predict. */
void tflm_sine_setup(void);

/* Run inference for input x in [0, kXrange] (0..2*pi). Returns predicted y (sine). */
float tflm_sine_predict(float x);

/* Fill the overlay buffer with model outputs (call from inference thread only). */
void tflm_sine_fill_overlay_buffer(void);

/* Get precomputed y values for overlay drawing. *out_num_points is set to valid count. */
void tflm_sine_overlay_get(const float **out_y_values, int *out_num_points);

/* True after tflm_sine_fill_overlay_buffer() has completed (safe to read from overlay). */
int tflm_sine_overlay_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* TENSORFLOW_LITE_MICRO_EXAMPLES_HELLO_WORLD_MAIN_FUNCTIONS_H_ */
