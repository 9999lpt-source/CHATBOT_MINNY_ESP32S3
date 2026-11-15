#ifndef MIC_CAPTURE_H
#define MIC_CAPTURE_H

#include <stdint.h>

void initMic();         // cài I2S RX và tạo Mic_Task, không chặn
uint16_t micGetLevel(); // RMS của 100 ms gần nhất
uint16_t micGetPeak();  // đỉnh |mẫu| của 100 ms gần nhất

#endif // MIC_CAPTURE_H