/*
 * Copyright 2020 The TensorFlow Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "main_functions.h"
#include "constants.h"
#include "model.hpp"
#include "output_handler.hpp"
#include <cmath>
#include <cstdlib>

#include <zephyr/sys/atomic.h>

#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/core/api/flatbuffer_conversions.h"

namespace {

const tflite::Model *model = nullptr;
tflite::MicroInterpreter *interpreter = nullptr;
TfLiteTensor *input = nullptr;
TfLiteTensor *output = nullptr;

constexpr int kTensorArenaSize = 2000;
alignas(16) uint8_t tensor_arena[kTensorArenaSize];

bool setup_done = false;

/* Precomputed overlay: filled by inference thread, read by display. */
float s_y_values[TFLM_SINE_OVERLAY_MAX_POINTS];
int s_num_points = 0;
atomic_t s_overlay_ready = ATOMIC_INIT(0);

}  /* namespace */

void tflm_sine_setup(void)
{
	if (setup_done) {
		return;
	}

	model = tflite::GetModel(g_model);
	if (model->version() != TFLITE_SCHEMA_VERSION) {
		MicroPrintf("Model schema version %d not supported (need %d)",
			    model->version(), TFLITE_SCHEMA_VERSION);
		return;
	}

	static tflite::MicroMutableOpResolver<1> resolver;
	resolver.AddFullyConnected();

	static tflite::MicroInterpreter static_interpreter(
		model, resolver, tensor_arena, kTensorArenaSize);
	interpreter = &static_interpreter;

	if (interpreter->AllocateTensors() != kTfLiteOk) {
		MicroPrintf("AllocateTensors() failed");
		return;
	}

	input = interpreter->input(0);
	output = interpreter->output(0);
	setup_done = true;
}

float tflm_sine_predict(float x)
{
	if (!setup_done || input == nullptr || output == nullptr) {
		return 0.0f;
	}

	int8_t x_quantized = static_cast<int8_t>(
		std::round(x / input->params.scale) + input->params.zero_point);
	input->data.int8[0] = x_quantized;

	if (interpreter->Invoke() != kTfLiteOk) {
		return 0.0f;
	}

	int8_t y_quantized = output->data.int8[0];
	float y = (y_quantized - output->params.zero_point) * output->params.scale;
	return y;
}

void tflm_sine_fill_overlay_buffer(void)
{
	tflm_sine_setup();
	if (!setup_done) {
		return;
	}
	const int n = TFLM_SINE_OVERLAY_MAX_POINTS;
	for (int i = 0; i < n; i++) {
		float x = (float)i / (float)(n - 1) * kXrange;
		s_y_values[i] = tflm_sine_predict(x);
	}
	s_num_points = n;
	atomic_set(&s_overlay_ready, 1);
}

void tflm_sine_overlay_get(const float **out_y_values, int *out_num_points)
{
	*out_y_values = s_y_values;
	*out_num_points = s_num_points;
}

int tflm_sine_overlay_is_ready(void)
{
	return atomic_get(&s_overlay_ready) != 0;
}
