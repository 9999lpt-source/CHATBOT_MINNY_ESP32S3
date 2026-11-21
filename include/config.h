#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stddef.h>

// ===== CHÂN (GPIO) =====
namespace pin
{
	// oled
	constexpr int OLED_SDA = 41;
	constexpr int OLED_SCL = 42;

	// button
	constexpr int BUTTON = 2;

	// mic INMP441 (I2S_NUM_1, RX)
	constexpr int MIC_SCK = 5;
	constexpr int MIC_WS = 4;
	constexpr int MIC_SD = 6;

} // namespace pin

// ===== OLED =====
namespace oled
{
	constexpr int WIDTH = 128;
	constexpr uint32_t I2C_CLOCK_HZ = 400000;
} // namespace oled

// ===== INPUT (NÚT VÀ BIẾN TRỞ) =====
namespace input
{
	// nút nhấn
	constexpr uint32_t DEBOUNCE_MS = 30;
	constexpr uint32_t POLL_MS = 5; // chu kì đọc nút

} // namespace input

// ===== WIFI & WEBSOCKET =====
namespace net
{
	// ----- WIFI -----
	constexpr uint8_t MAX_SAVED_WIFI = 5;
	constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 10000;
	constexpr uint32_t WIFI_RETRY_MS = 3000;
	constexpr uint32_t WIFI_MAX_ROUNDS = 3;
	constexpr uint32_t PORTAL_TIMEOUT_MS = 180000;
	constexpr uint32_t USER_ACTION_TIMEOUT_MS = 10000;
	constexpr const char *AP_SSID = "MINNY_SETUP_WIFI";

	// ----- WEBSOCKET -----
	constexpr bool USE_LOCAL_SERVER = true;
	constexpr const char *WS_HOST = USE_LOCAL_SERVER ? "192.168.1.139" : "chatbot-minny.onrender.com";
	constexpr uint16_t WS_PORT = USE_LOCAL_SERVER ? 8000 : 443;
	constexpr bool WS_USE_SSL = !USE_LOCAL_SERVER;
	constexpr const char *WS_PATH = "/ws/minny";
	constexpr uint32_t WS_RECONNECT_MS = 3000; // Chờ bao lâu thì thử lại
	constexpr uint32_t WS_LOOP_MS = 5;

	constexpr uint8_t WS_CTRL_QUEUE_LEN = 4; // Tối đa 4 tin điều khiển đang chờ
	constexpr uint16_t WS_MSG_BUF_SIZE = 64; // mảng dựng json (dài nhất ~40 byte)

	constexpr uint32_t WS_PING_INTERVAL_MS = 15000;	// Chu kỳ ping
	constexpr uint32_t WS_PONG_TIMEOUT_MS = 5000;	// Thời gian chờ pong tối đa
	constexpr uint8_t WS_PONG_MISS_MAX = 2; 		// Trượt mấy lần thì ngắt
} // namespace net

// ===== TASK FreeRTOS =====
namespace task
{
	struct Cfg
	{
		const char *name;
		uint32_t stack;
		int prio;
		int core;
	};

	constexpr Cfg OLED = {"Oled_Task", 4096, 1, 1};
	constexpr Cfg BUTTON = {"Button_Task", 2048, 2, 1};
	constexpr Cfg WIFI = {"Wifi_Task", 6144, 2, 0};
	constexpr Cfg NETWORK = {"Network_Task", 8192, 3, 0};
	constexpr Cfg MIC = {"Mic_Task", 6144, 5, 1};

} // namespace task

// ===== AUDIO =====
namespace audio {
	constexpr uint32_t SAMPLE_RATE = 16000; // Hz
	constexpr uint32_t FRAME_SAMPLES = 320; // 20ms
	constexpr int MIC_DMA_COUNT = 4; // số khối DMA
	constexpr bool MIC_USE_LEFT = true; // dùng kênh trái của INMP441
	constexpr int MIC_SHIFT = 15;
	constexpr uint32_t MIC_READ_TIMEOUT_MS = 100; // thời gian chờ mic ổn định
	constexpr uint8_t LEVEL_FRAMES = 5; // 5 Khung x 20ms = 100ms	

	constexpr size_t STREAM_CAPACITY_BYTES = 160 * 1024;				// dung lượng PCM mục hiêu
	constexpr size_t STREAM_STORAGE_BYTES = STREAM_CAPACITY_BYTES + 1;	// Vùng nhớ cấp phát
	constexpr size_t STREAM_TRIGGER_BYTES = 1;							// Ngưỡng đánh thức task chờ đọc
}

#endif // CONFIG_H