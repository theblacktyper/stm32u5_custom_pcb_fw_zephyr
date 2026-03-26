#ifndef CAMERA_RUNTIME_H_
#define CAMERA_RUNTIME_H_

#include <stdbool.h>

void camera_runtime_thread(void);
void inference_thread_entry(void);
bool camera_is_capture_started(void);
void camera_request_capture_start(void);
bool camera_is_showing_frame(void);
void camera_wake_inference_thread(void);

#endif /* CAMERA_RUNTIME_H_ */
