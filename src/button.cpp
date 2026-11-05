#include "button.h"
#include "config.h"
#include <Arduino.h>

static ButtonCallback pressCallback = NULL;

// ===== HÀM XỬ LÝ NÚT NHẤN (đọc tín hiệu & chống dội) =====
static void buttonTask(void *pvParameters)
{
	bool stableState = HIGH;	// trạng thái được nhấn (nhấn = LOW)
	bool lastReading = HIGH;	// lần đọc trước
	uint32_t lastChange_ms = 0; // thờ điểm đọc gần nhất

	for (;;)
	{
		// đọc tín hiệu mới
		bool reading = digitalRead(pin::BUTTON);
		if (reading != lastReading)
		{
			lastChange_ms = millis();
			lastReading = reading;
		}

		// xác nhận tín hiệu
		if ((millis() - lastChange_ms) >= input::DEBOUNCE_MS && reading != stableState)
		{
			stableState = reading;
			if (stableState == LOW && pressCallback != NULL)
			{
				pressCallback();
			}
		}

		vTaskDelay(pdMS_TO_TICKS(input::POLL_MS));
	}
}

// ===== HÀM CẤU HÌNH NÚT NHẤN =====
void initButton(ButtonCallback onPress)
{
	pressCallback = onPress;
	pinMode(pin::BUTTON, INPUT_PULLUP);
	xTaskCreatePinnedToCore(buttonTask, task::BUTTON.name, task::BUTTON.stack, NULL, task::BUTTON.prio, NULL, task::BUTTON.core);
}