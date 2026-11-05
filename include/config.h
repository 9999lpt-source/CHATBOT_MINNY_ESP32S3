#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

// ===== CHÂN (GPIO) =====
namespace pin
{
	// oled
	constexpr int OLED_SDA = 41;
	constexpr int OLED_SCL = 42;

	// button
	constexpr int BUTTON = 2;

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
	constexpr const char *WS_HOST = USE_LOCAL_SERVER ? "192.168.1.211" : "chatbot-minny.onrender.com";
	constexpr uint16_t WS_PORT = USE_LOCAL_SERVER ? 8000 : 443;
	constexpr bool WS_USE_SSL = !USE_LOCAL_SERVER;
	constexpr const char *WS_PATH = "ws/minny";
	constexpr uint32_t WS_RECONNECT_MS = 3000; // Chờ bao lâu thì thử lại
	constexpr uint32_t WS_LOOP_MS = 5;
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

} // namespace task

#endif // CONFIG_H