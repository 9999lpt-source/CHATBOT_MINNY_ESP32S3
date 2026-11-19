#include <Arduino.h>
#include "controlOled.h"
#include "button.h"
#include "wifiManager.h"
#include "wsClient.h"
#include "protocol.h"
#include "micCapture.h"
#include "config.h"
#include <WiFi.h>
#include <stddef.h>

int pressCount = 0;

// ===== BỘ ĐẾM LƯỢT =====
static volatile int32_t turnCounter = 0;

// ===== HÀM GỌI KHI NHẤN NÚT =====
static void onButtonPress()
{
	int32_t turn = turnCounter + 1;
	turnCounter = turn;
	if (!wsSendControl(ClientMsg::AudioStart, turn)) {
		Serial.println("[MAIN] nhan nut nhung chua gui duoc");
		oledSetText("Chưa kết nối server, chưa thể gửi tin");
		return;
	}
	Serial.printf("[%lu] [MAIN] Nhan nut -> audio_start luot %ld\n", (unsigned long)millis(), (long)turn);
}

// ===== HÀM GỌI KHI THAY ĐỔI TRẠNG THÁI WIFI =====
static void onWifiStatus(WifiStatus s)
{
	switch (s)
	{
	case WifiStatus::Scanning:
		oledSetText("Đang quét Wifi...");
		break;
	case WifiStatus::Connecting:
		oledSetText("Đang kết nối Wifi...");
		break;
	case WifiStatus::Connected:
		oledSetText("IP: " + WiFi.localIP().toString());
		break;
	case WifiStatus::Portal:
		oledSetText("Kết nối vào WiFi: MINNY_SETUP_WIFI");
		break;
	default:
		break;
	}
	if (s == WifiStatus::Connected)
		wsStart();
	else
		wsStop();
}

// ===== HÀM GỌI KHI THAY ĐỔI TRẠNG THÁI WEBSOCKET =====
static void onWsStatus(WsStatus s)
{
    Serial.printf("[%lu] [MAIN] WS status=%d\n", (unsigned long)millis(), (int)s);
	switch (s)
	{
	case WsStatus::Connected:
		oledSetText("Đã kết nối Server!");
		break;
	case WsStatus::Disconnected:
		if (WiFi.status() == WL_CONNECTED)
			oledSetText("Mất kết nối Server");
		break;
	default:
		break;
	}
}

// ===== LỌC LỜI THOẠI KHI CÓ LỖI =====
static const char *errorText(ErrorCode c)
{
	switch (c)
	{
	case ErrorCode::AudioTooShort:
		return "Bạn nói quá ngắn";
	case ErrorCode::SttFailed:
		return "không nghe rõ, thử lại";
	case ErrorCode::SttEmpty:
		return "Không nhận ra giọng nói";
	case ErrorCode::LlmFailed:
		return "Lỗi khi suy ";
	case ErrorCode::TtsFailed:
		return "Lỗi tạo giọng nói";
	case ErrorCode::MusicNotFound:
		return "Không tìm thấy bài hát";
	default:
		return "Không có lỗi xảy ra";
	}
}

// ===== HÀM GỌI KHI CÓ TIN MỚI TỪ SERVER =====
static void onWsMessage(const ServerMessage &m)
{
	int32_t current = turnCounter;
	if (m.turn != NO_TURN && m.turn != current)
	{
		Serial.printf("[MAIN] Bo tin luot cu: %ld (luot hien tai %ld)\n", (long)m.turn, (long)current);
		return;
	}

	switch (m.type)
	{
	case ServerMsg::State:
		if (m.state == ServerState::Transcribing)
			oledSetText("Đang nhận dạng giọng nói...");
		else if (m.state == ServerState::Thingking)
			oledSetText("Đang suy nghĩ...");
		break;
	case ServerMsg::Error:
		oledSetText(errorText(m.code));
	default:
		break;
	}
}

// ===== THỬ TẠM 4b-2a =====
static volatile uint32_t micFrames = 0;
static volatile uint32_t micSamples = 0;
static volatile uint32_t micShort = 0;
static volatile uint32_t micStackLeft = 0;

static void onMicFrame(const int16_t *samples, size_t count)
{
    (void)samples;
    micFrames++;
    micSamples += count;
    if (count != audio::FRAME_SAMPLES)
        micShort++;
    micStackLeft = uxTaskGetStackHighWaterMark(NULL);
}

// ===== HÀM SETUP =====
void setup()
{
	Serial.begin(115200);
	delay(1500);
	initOled();
	initButton(onButtonPress);
	initWs(onWsStatus, onWsMessage);
	initWifi(onWifiStatus);
	initMic(onMicFrame);
}

// ===== HÀM LOOP =====
void loop()
{
	static uint32_t lastPrint = 0;
    static uint32_t lastFrames = 0;
    static uint32_t lastSamples = 0;
    if (millis() - lastPrint >= 1000)
    {
        lastPrint = millis();
        uint32_t f = micFrames;
        uint32_t s = micSamples;
        Serial.printf("[TEST] khung/giay=%u mau/giay=%u khung_ngan=%u stack_con=%u\n",
                      (unsigned)(f - lastFrames), (unsigned)(s - lastSamples),
                      (unsigned)micShort, (unsigned)micStackLeft);
        lastFrames = f;
        lastSamples = s;
    }
}