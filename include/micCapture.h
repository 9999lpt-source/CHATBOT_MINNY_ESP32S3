#ifndef MIC_CAPTURE_H
#define MIC_CAPTURE_H

#include <stdint.h>
#include <stddef.h>

typedef void (*MicFrameCallback)(const int16_t *samples, size_t count); // callback khi có frame mới, count = audio::FRAME_SAMPLES

void initMic(MicFrameCallback onFrame);  // Cai I2S, tạo Mic_Task, callback chạy trong Mic_Task
uint16_t micGetLevel(); // RMS của 100 ms gần nhất
uint16_t micGetPeak();  // đỉnh |mẫu| của 100 ms gần nhất

#endif // MIC_CAPTURE_H