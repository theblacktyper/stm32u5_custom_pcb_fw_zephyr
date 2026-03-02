/*
 * Person detection TFLM integration.
 * Input: 96x96 grayscale int8 (from 160x120 RGB565 center crop).
 * Output: 2 classes (not person, person); int8 scores.
 */

#include "main_functions.h"
#include "model_settings.h"
#include "person_detect_model_data.hpp"

#include <cstring>
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace {

const tflite::Model *model = nullptr;
tflite::MicroInterpreter *interpreter = nullptr;
TfLiteTensor *input = nullptr;
TfLiteTensor *output = nullptr;

constexpr int kTensorArenaSize = 136 * 1024;
alignas(16) uint8_t tensor_arena[kTensorArenaSize];

bool setup_done = false;

}  // namespace

void tflm_person_detection_setup(void)
{
	if (setup_done) {
		return;
	}

	model = tflite::GetModel(g_person_detect_model_data);
	if (model->version() != TFLITE_SCHEMA_VERSION) {
		MicroPrintf("Model schema version %d not supported (need %d)",
			    model->version(), TFLITE_SCHEMA_VERSION);
		return;
	}

	static tflite::MicroMutableOpResolver<5> resolver;
	resolver.AddAveragePool2D(tflite::Register_AVERAGE_POOL_2D_INT8());
	resolver.AddConv2D(tflite::Register_CONV_2D_INT8());
	resolver.AddDepthwiseConv2D(tflite::Register_DEPTHWISE_CONV_2D_INT8());
	resolver.AddReshape();
	resolver.AddSoftmax(tflite::Register_SOFTMAX_INT8());

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

/*
 * Convert 160x120 RGB565 frame to 96x96 grayscale int8 (center crop).
 * Model expects signed int8; subtract 128 from grayscale [0,255].
 */
static void rgb565_160x120_to_96x96_grayscale_int8(const uint16_t *rgb565,
						   int8_t *out_grayscale)
{
	const int crop_x = (160 - kNumCols) / 2;
	const int crop_y = (120 - kNumRows) / 2;

	for (int dy = 0; dy < kNumRows; dy++) {
		for (int dx = 0; dx < kNumCols; dx++) {
			int sx = crop_x + dx;
			int sy = crop_y + dy;
			uint16_t px = rgb565[sy * 160 + sx];
			/* RGB565: R=bits 11-15, G=5-10, B=0-4. Approximate grayscale. */
			uint8_t r = (uint8_t)((px >> 11) * 255 / 31);
			uint8_t g = (uint8_t)(((px >> 5) & 0x3f) * 255 / 63);
			uint8_t b = (uint8_t)((px & 0x1f) * 255 / 31);
			uint8_t gray = (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
			*out_grayscale++ = (int8_t)((int)gray - 128);
		}
	}
}

int tflm_person_detection_run(const uint8_t *frame_rgb565_160x120,
			      int8_t *out_person_score,
			      int8_t *out_no_person_score)
{
	if (!setup_done || input == nullptr || output == nullptr) {
		return -1;
	}

	const uint16_t *rgb565 = (const uint16_t *)frame_rgb565_160x120;
	rgb565_160x120_to_96x96_grayscale_int8(rgb565, input->data.int8);

	if (interpreter->Invoke() != kTfLiteOk) {
		return -1;
	}

	*out_no_person_score = output->data.int8[kNotAPersonIndex];
	*out_person_score = output->data.int8[kPersonIndex];
	return 0;
}

int tflm_person_detection_ready(void)
{
	return setup_done ? 1 : 0;
}
