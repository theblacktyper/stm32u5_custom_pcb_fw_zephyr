/*
 * Person detection TFLM integration.
 * Input: 96x96 grayscale int8 (center crop from camera RGB565 frame).
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

constexpr int kTensorArenaSize = 120 * 1024;
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

	/* Official 96x96 model uses only these 5 ops (see tools/list_model_ops.py) */
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
 * Convert RGB565 pixel to grayscale int8 for model input.
 * Model expects: (int8+128)/255 = gray/255, so int8 = gray - 128.
 */
static inline int8_t rgb565_to_gray_int8(uint16_t px)
{
	uint8_t r = (uint8_t)((px >> 11) * 255 / 31);
	uint8_t g = (uint8_t)(((px >> 5) & 0x3f) * 255 / 63);
	uint8_t b = (uint8_t)((px & 0x1f) * 255 / 31);
	uint8_t gray = (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
	return (int8_t)((int)gray - 128);
}

/*
 * Crop center kNumCols x kNumRows from RGB565 frame and convert to grayscale int8.
 * No scaling - preserves full resolution of center region.
 * Falls back to scale-down when frame is smaller than model input.
 */
static void rgb565_to_model_input(const uint16_t *rgb565,
				  int src_w, int src_h,
				  int8_t *out_grayscale)
{
	if (src_w >= kNumCols && src_h >= kNumRows) {
		/* Center crop: take 128x128 from center */
		int off_x = (src_w - kNumCols) / 2;
		int off_y = (src_h - kNumRows) / 2;

		for (int dy = 0; dy < kNumRows; dy++) {
			const uint16_t *src_row = rgb565 + (off_y + dy) * src_w + off_x;
			for (int dx = 0; dx < kNumCols; dx++) {
				*out_grayscale++ = rgb565_to_gray_int8(src_row[dx]);
			}
		}
	} else {
		/* Frame smaller than 128x128: scale up to fill */
		for (int dy = 0; dy < kNumRows; dy++) {
			int sy = dy * src_h / kNumRows;
			const uint16_t *src_row = rgb565 + sy * src_w;
			for (int dx = 0; dx < kNumCols; dx++) {
				int sx = dx * src_w / kNumCols;
				*out_grayscale++ = rgb565_to_gray_int8(src_row[sx]);
			}
		}
	}
}

int tflm_person_detection_run(const uint8_t *frame_rgb565,
			      int frame_w, int frame_h,
			      int8_t *out_person_score,
			      int8_t *out_no_person_score)
{
	if (!setup_done || input == nullptr || output == nullptr) {
		return -1;
	}

	const uint16_t *rgb565 = (const uint16_t *)frame_rgb565;
	rgb565_to_model_input(rgb565, frame_w, frame_h, input->data.int8);

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

int tflm_get_output_quant_params(float *out_scale, int32_t *out_zero_point)
{
	if (!setup_done || output == nullptr || out_scale == nullptr ||
	    out_zero_point == nullptr) {
		return -1;
	}
	*out_scale = output->params.scale;
	*out_zero_point = output->params.zero_point;
	return 0;
}
