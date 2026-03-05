/*
 * Person detection model (official TFLite Micro, 96x96 grayscale).
 * Generate with: python tools/tflite_to_c_array.py person_detect.tflite person_detect_model_data.c
 */
#ifndef PERSON_DETECT_MODEL_DATA_H_
#define PERSON_DETECT_MODEL_DATA_H_

#ifdef __cplusplus
extern "C" {
#endif

extern const unsigned char g_person_detect_model_data[];
extern const int g_person_detect_model_data_len;

#ifdef __cplusplus
}
#endif

#endif /* PERSON_DETECT_MODEL_DATA_H_ */
