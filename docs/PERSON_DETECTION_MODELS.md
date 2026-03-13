# Person Detection Models for TFLite Micro

This document describes available pre-trained person detection models and how to integrate them.

## Current Model

The project uses the **official TensorFlow 96×96 grayscale** person detection model from [tflite-micro](https://github.com/tensorflow/tflite-micro). It is well-tested on ESP32, Arduino, and other embedded platforms.

---

## Option 1: Official TensorFlow Person Detection Model (Recommended)

**Source:** [TensorFlow Lite Micro](https://github.com/tensorflow/tflite-micro)

| Spec | Value |
|------|-------|
| Input size | **96×96** grayscale |
| Format | INT8 quantized |
| Size | ~250 KB |
| Architecture | MobileNet v1 (VWW) |
| Proven | Used in ESP32, Arduino Nano 33 BLE, many embedded demos |

### Download

```bash
# Clone tflite-micro (or download just the model)
git clone --depth 1 https://github.com/tensorflow/tflite-micro.git
cp tflite-micro/tensorflow/lite/micro/models/person_detect.tflite tools/
```

Or download directly:
- https://github.com/tensorflow/tflite-micro/raw/main/tensorflow/lite/micro/models/person_detect.tflite

### Integration Steps

1. **Convert to C array:**
   ```bash
   cd tools
   python tflite_to_c_array.py person_detect.tflite ../src/tflm_hello_world/person_detect_model_data.c
   ```

2. **Update `model_settings.h`:** Change `kNumCols` and `kNumRows` from 128 to 96.

3. **Update preprocessing:** The center crop/scale logic in `main_functions.cpp` uses `kNumCols`/`kNumRows`, so it will automatically use 96×96 once model_settings is updated.

4. **Op resolver:** The official 96×96 model typically uses a smaller op set (Conv2D, DepthwiseConv2D, AveragePool2D, Reshape, Softmax, FullyConnected). If you get "Didn't find op for builtin opcode" errors, remove PAD, ADD, MUL, MEAN from the resolver and reduce the resolver size.

5. **Arena size:** The 96×96 model is smaller; you may reduce `kTensorArenaSize` (e.g. 120 KB) if needed.

---

## Option 2: Other Pre-trained Models

| Model | Input | Size | Notes |
|-------|-------|------|-------|
| **Qualcomm Person-Foot-Detection** | varies | ~10 MB | Too large for typical MCU RAM |
| **MediaPipe Face Detection** | 256×256 | large | Face, not full person |
| **MarcSue/person_detection** | ONNX | - | Not TFLite, would need conversion |

**Conclusion:** For TFLite Micro on STM32, the **official 96×96 model** is the best pre-trained option.

---

## Option 3: Retrain Your Own 128×128 Model

If you prefer to keep 128×128 (more resolution), retrain with:

1. **More epochs** – current script uses 20; try 30–40.
2. **Data augmentation** – the script already uses flip, brightness, contrast.
3. **Your own images** – add photos from your camera (with/without person) to the training set for better domain match.
4. **Different threshold** – the model may output 40–60% when uncertain; you could lower the green threshold (currently 50%).

```bash
cd tools
python train_vww_128.py --epochs 30 --data_dir ./vww_data
python tflite_to_c_array.py person_detect_128.tflite ../src/tflm_hello_world/person_detect_model_data.c
```

---

## Summary

| Approach | Effort | Expected result |
|----------|--------|-----------------|
| **Switch to official 96×96** | Low | Well-tested, good baseline |
| **Retrain 128×128** | Medium | May improve with more data/epochs |
| **Add your camera images to training** | Medium | Best match to your setup |

**Recommendation:** Try the official 96×96 model first. It is widely used and should give reliable person detection. If you need 128×128 for resolution, retrain with additional data from your camera.
