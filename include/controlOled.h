#ifndef CONTROL_OLED_H
#define CONTROL_OLED_H

#include <Arduino.h>

// ===== KHAI BÁO CÁC HÀM =====
void initOled();
void oledSetText(const String &text);

#endif // CONTROL_OLED_H