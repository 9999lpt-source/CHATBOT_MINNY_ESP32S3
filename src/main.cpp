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
static volatile int32_t requestedTestTurn = NO_TURN; //<-----

// ===== HÀM GỌI KHI NHẤN NÚT =====
static void onButtonPress()
{
	int32_t turn = turnCounter + 1;
	turnCounter = turn;

	if (wsSendControl(ClientMsg::AudioStart, turn))
		requestedTestTurn = turn;
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
		else if (m.state == ServerState::Thinking)
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
    static int32_t activeTurn = NO_TURN;
    static int32_t completedTurn = NO_TURN;
    static uint32_t framesSent = 0;
    static uint32_t nextFrameMs = 0;
    static bool started = false;

    constexpr uint32_t FRAME_MS =
        audio::FRAME_SAMPLES * 1000 / audio::SAMPLE_RATE;

    constexpr uint32_t TOTAL_FRAMES =
        audio::TEST_TONE_MS / FRAME_MS;

    constexpr uint32_t PERIOD_SAMPLES =
        audio::SAMPLE_RATE / audio::TEST_TONE_HZ;

    static_assert(PERIOD_SAMPLES == 16,
                  "Bang song sin can 16 mau moi chu ky");

    static const int16_t tone[16] = {
        0, 2296, 4243, 5543,
        6000, 5543, 4243, 2296,
        0, -2296, -4243, -5543,
        -6000, -5543, -4243, -2296
    };

    static int16_t pcm[audio::FRAME_SAMPLES];

    int32_t requested = requestedTestTurn;

    if (requested == NO_TURN || requested == completedTurn)
        return;

    // Có yêu cầu lượt mới
    if (requested != activeTurn)
    {
        activeTurn = requested;
        framesSent = 0;
        started = false;
    }

    // Đợi Network_Task mở lượt
    if (!started)
    {
        if (!wsAudioReady(activeTurn))
            return;

        started = true;
        nextFrameMs = millis();

        Serial.printf(
            "[TEST] Bat dau tao PCM luot %ld\n",
            (long)activeTurn
        );
    }

    // Phát một khung mỗi 20 ms
    if ((int32_t)(millis() - nextFrameMs) < 0)
        return;

    for (size_t i = 0; i < audio::FRAME_SAMPLES; i++)
    {
        pcm[i] = tone[i % PERIOD_SAMPLES];
    }

    bool ok = wsPushAudio(
        reinterpret_cast<const uint8_t*>(pcm),
        sizeof(pcm),
        activeTurn
    );

    if (!ok)
    {
        Serial.printf(
            "[TEST] Push PCM FAIL luot %ld\n",
            (long)activeTurn
        );

        wsFlagAudioFailure(activeTurn);
        completedTurn = activeTurn;
        activeTurn = NO_TURN;
        return;
    }

    framesSent++;
    nextFrameMs += FRAME_MS;

    if (framesSent == TOTAL_FRAMES)
    {
        bool ended = wsSendControl(
            ClientMsg::AudioEnd,
            activeTurn
        );

        if (!ended)
            wsFlagAudioFailure(activeTurn);

        Serial.printf(
            "[TEST] Luot %ld: %lu frames, %lu bytes, end=%d\n",
            (long)activeTurn,
            (unsigned long)framesSent,
            (unsigned long)(framesSent * sizeof(pcm)),
            ended ? 1 : 0
        );

        completedTurn = activeTurn;
        activeTurn = NO_TURN;
    }
}