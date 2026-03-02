/*
 * Person detection model settings (96x96 grayscale, 2 classes).
 * Must match the person_detect.tflite model from tflite-micro.
 * C-compatible (no constexpr).
 */
#ifndef MODEL_SETTINGS_H_
#define MODEL_SETTINGS_H_

#define kNumCols        96
#define kNumRows        96
#define kNumChannels    1

#define kMaxImageSize   (kNumCols * kNumRows * kNumChannels)

#define kCategoryCount  2
#define kPersonIndex    1
#define kNotAPersonIndex 0

extern const char *kCategoryLabels[kCategoryCount];

#endif /* MODEL_SETTINGS_H_ */
