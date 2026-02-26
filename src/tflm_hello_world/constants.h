/*
 * Copyright 2020 The TensorFlow Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef TENSORFLOW_LITE_MICRO_EXAMPLES_HELLO_WORLD_CONSTANTS_H_
#define TENSORFLOW_LITE_MICRO_EXAMPLES_HELLO_WORLD_CONSTANTS_H_

/* x range the model was trained on: 0 to ~2*Pi */
const float kXrange = 2.f * 3.14159265359f;

extern const int kInferencesPerCycle;

/* Max points for precomputed sine overlay buffer (e.g. camera width + 1). */
#define TFLM_SINE_OVERLAY_MAX_POINTS  161

#endif /* TENSORFLOW_LITE_MICRO_EXAMPLES_HELLO_WORLD_CONSTANTS_H_ */
