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
static volatile int32_t requestedMicTurn = NO_TURN; //<-----

static volatile uint32_t micStartMs = 0;
static volatile uint32_t micEndMs = 0;

static volatile uint8_t micPushError = 0;

// 0: Thanh cong
// 1: Khung mic sai kich thuoc
// 2: wsPushAudio that bai
// 3: Khong gui duoc AudioEnd
static volatile uint8_t micResultReason = 0;

// ===== HÀM GỌI KHI NHẤN NÚT =====
static void onButtonPress()
{
	int32_t turn = turnCounter + 1;
	turnCounter = turn;

	if (!wsSendControl(ClientMsg::AudioStart, turn))
		return;
        
    requestedMicTurn = turn;
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

// ===== THỬ TẠM 4b-2c =====
// ===== KET QUA THU MICRO =====
static volatile int32_t micResultTurn = NO_TURN;
static volatile uint32_t micResultFrames = 0;
static volatile bool micResultOk = false;

// ===== CALLBACK CHAY TRONG MIC_TASK =====
static void onMicFrame(const int16_t *samples, size_t count)
{
    static int32_t activeTurn = NO_TURN;
    static int32_t completedTurn = NO_TURN;
    static uint32_t framesSent = 0;

    int32_t requested = requestedMicTurn;

    // Chua co yeu cau thu
    if (requested == NO_TURN)
        return;

    // Luot nay da hoan tat
    if (requested == completedTurn)
        return;

    // Co luot moi thay the luot cu
    if (requested != activeTurn)
    {
        micStartMs = 0;
        micEndMs = 0;
        micResultReason = 0;

        activeTurn = NO_TURN;
        framesSent = 0;

        micPushError = 0;

        // Network_Task chua mo luot moi
        if (!wsAudioReady(requested))
            return;

        activeTurn = requested;
    }

    // Trong bai thu nay, moi khung phai du 320 mau
    if (samples == nullptr ||
        count != audio::FRAME_SAMPLES)
    {
        wsFlagAudioFailure(activeTurn);

        micResultFrames = framesSent;
        micResultOk = false;

        micResultReason = 1; micEndMs = millis();
        micResultTurn = activeTurn;

        completedTurn = activeTurn;
        activeTurn = NO_TURN;
        return;
    }

    if (framesSent == 0)
        micStartMs = millis();

    WsPushError pushError = WsPushError::None;

    // Ghi nguyen khung PCM vao StreamBuffer
    bool ok = wsPushAudio(
        reinterpret_cast<const uint8_t*>(samples),
        count * sizeof(int16_t),
        activeTurn,
        &pushError
    );

    if (!ok)
    {
        wsFlagAudioFailure(activeTurn);

        micResultFrames = framesSent;
        micResultOk = false;
        micResultReason = 2; micEndMs = millis();
        micResultTurn = activeTurn;

        completedTurn = activeTurn;
        activeTurn = NO_TURN;

        micPushError = static_cast<uint8_t>(pushError);
        return;
    }

    framesSent++;

    // Chua du 5 giay, tiep tuc thu
    if (framesSent < audio::MIC_TEST_FRAMES)
        return;

    // Khung thu 250 da ghi thanh cong.
    // Yeu cau ket thuc ngay trong Mic_Task.
    bool ended = wsSendControl(
        ClientMsg::AudioEnd,
        activeTurn
    );

    if (!ended)
    {
        micResultReason = 3; micEndMs = millis();
        wsFlagAudioFailure(activeTurn);
    } else {
        micResultReason = 0; micEndMs = millis();
    }

    micResultFrames = framesSent;
    micResultOk = ended;
    micResultTurn = activeTurn;

    completedTurn = activeTurn;
    activeTurn = NO_TURN;
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

void loop()
{
    static int32_t lastReportedTurn = NO_TURN;

    int32_t result = micResultTurn;

    if (result != NO_TURN &&
        result != lastReportedTurn)
    {
        lastReportedTurn = result;

        uint32_t start = micStartMs;
        uint32_t end = micEndMs;
        uint8_t reason = micResultReason;
        uint32_t frames = micResultFrames;
        bool ok = micResultOk;

        uint8_t pushError = micPushError;

        Serial.printf(
            "[%lu] [TEST] MIC turn=%ld frames=%lu "
            "elapsed=%lu ms ok=%d reason=%u pushError=%u\n",
            (unsigned long)millis(),
            (long)result,
            (unsigned long)frames,
            (unsigned long)(end - start),
            ok ? 1 : 0,
            (unsigned)reason,
            (unsigned)pushError
        );
    }

    delay(10);
}

// ===== HÀM LOOP =====

/*
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
*/