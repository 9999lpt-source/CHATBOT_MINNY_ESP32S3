#include <Arduino.h>
#include "controlOled.h"
#include "button.h"
#include "wifiManager.h"
#include "wsClient.h"
#include "protocol.h"
#include "vad.h"
#include "micCapture.h"
#include "config.h"
#include <WiFi.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <atomic>

// ===== BỘ ĐẾM LƯỢT =====
static volatile int32_t turnCounter = 0;
static volatile int32_t requestedMicTurn = NO_TURN;

// ===== HÀM GỌI KHI NHẤN NÚT =====
static void onButtonPress()
{
	int32_t turn = turnCounter + 1;
	turnCounter = turn;

	if (!wsSendControl(ClientMsg::AudioStart, turn))
		return;
        
    requestedMicTurn = turn;
}

// ===== KẾT QUẢ THU MICRO =====
enum class MicResultReason : uint8_t
{
    Success = 0,
    InvalidFrame = 1,
    PushFailed = 2,
    EndFailed = 3,
    Replaced = 4
};

struct MicResult
{
    int32_t turn;
    uint32_t frames;
    uint32_t elapsedMs;
    bool ok;
    MicResultReason reason;
    WsPushError pushError;
};

static QueueHandle_t micResultQueue = nullptr;
static std::atomic<uint32_t> droppedMicResults{0};  //phát hiện queue kết quả đầy

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

// ===== HÀM HỖ TRỢ KIỂM TRA LỖI MIC =====
static void publishMicResult(const MicResult &result)
{
    if (micResultQueue == nullptr || xQueueSend(micResultQueue, &result, 0) != pdTRUE)
        droppedMicResults.fetch_add(1);
}

// ===== CALLBACK CHAY TRONG MIC_TASK =====
static void onMicFrame(const int16_t *samples, size_t count)
{
    static int32_t activeTurn = NO_TURN;
    static int32_t completedTurn = NO_TURN;
    static uint32_t framesSent = 0;
    static uint32_t captureStartMs = 0;

    static int32_t guardTurn = NO_TURN;
    static uint32_t guardStartedMs = 0;

    //hoan tat bao cao 1 luot, khong gui mang
    auto finishTurn = [&](int32_t turn, bool ok, MicResultReason reason, WsPushError pushError)
    {
        MicResult result = {
            turn,
            framesSent,
            framesSent > 0 ? millis() - captureStartMs : 0,
            ok,
            reason,
            pushError
        };
        publishMicResult(result);
    };

    int32_t requested = requestedMicTurn;
    if (requested == NO_TURN || requested == completedTurn)
        return;

    // === LƯỢT MỚI ===
    if (requested != guardTurn)
    {
        //Ghi nhận lượt cũ bị thay thế nếu đang thu
        if (activeTurn != NO_TURN)
            finishTurn(activeTurn, false, MicResultReason::Replaced, WsPushError::None);

        guardTurn = requested;
        guardStartedMs = millis();

        activeTurn = NO_TURN;
        framesSent = 0;
        captureStartMs = 0;
    }

    // === BẢO VỆ TIẾNG NÚT ===
    if ((uint32_t)(millis() - guardStartedMs) < audio::BUTTON_AUDIO_GUARD_MS)
        return;

    // === ĐỢI NETWORK_TASK MỞ LƯỢT ===
    if (activeTurn != requested)
    {
        if (!wsAudioReady(requested))
            return;
        activeTurn = requested;
    }

    // === KIỂM TRA KHUNG PCM ===
    if (samples == nullptr || count !=  audio::FRAME_SAMPLES)
    {
        wsFlagAudioFailure(activeTurn);
        finishTurn(activeTurn, false, MicResultReason::InvalidFrame, WsPushError::None);
        
        completedTurn = activeTurn;
        activeTurn = NO_TURN;
        return;
    }

    // === DAY PCM ===
    if (framesSent == 0)
        captureStartMs = millis();

    WsPushError pushError = WsPushError::None;

    bool pushed = wsPushAudio(
        reinterpret_cast<const uint8_t*>(samples),
        count * sizeof(int16_t),
        activeTurn,
        &pushError
    );

    if (!pushed)
    {
        wsFlagAudioFailure(activeTurn);
        finishTurn(activeTurn, false, MicResultReason::PushFailed, pushError);
        completedTurn = activeTurn;
        activeTurn = NO_TURN;
        return;
    }

    framesSent++;

    // === CHƯA ĐỦ 250 KHUNG ===
    if (framesSent < audio::MIC_TEST_FRAMES)
        return;

    bool ended = wsSendControl(ClientMsg::AudioEnd, activeTurn);
    if (!ended)
        wsFlagAudioFailure(activeTurn);

    finishTurn(
        activeTurn, ended,
        ended ? MicResultReason::Success :
                MicResultReason::EndFailed,
        WsPushError::None
    );

    completedTurn = activeTurn;
    activeTurn = NO_TURN;
}

// === TẠO QUEUE GỬI KẾT QUẢ TỪ MIC_TASK -> LOOP ===
static void createdMicResultQueue()
{
    micResultQueue = xQueueCreate(audio::MIC_RESULT_QUEUE_LEN, sizeof(MicResult));
    if (micResultQueue == nullptr)
        Serial.println("[MAIN] mic result queue FAIL");
    else
        Serial.println("[MAIN] mic result queue OK");  

}

static void testVadRms()
{
    static int16_t samples[audio::FRAME_SAMPLES];

    vadReset();
    vadStartListening();

    // A: Im lang
    for (size_t i = 0; i < audio::FRAME_SAMPLES; i++)
        samples[i] = 0;

    vadProcessFrame(samples, audio::FRAME_SAMPLES);

    Serial.printf("[VAD] A rms=%u\n",
                  (unsigned)vadGetStatus().rms);

    // B: Bien do duong khong doi
    for (size_t i = 0; i < audio::FRAME_SAMPLES; i++)
        samples[i] = 1000;

    vadProcessFrame(samples, audio::FRAME_SAMPLES);

    Serial.printf("[VAD] B rms=%u\n",
                  (unsigned)vadGetStatus().rms);

    // C: Xen ke duong/am
    for (size_t i = 0; i < audio::FRAME_SAMPLES; i++)
        samples[i] = (i % 2 == 0) ? 1000 : -1000;

    vadProcessFrame(samples, audio::FRAME_SAMPLES);

    VadStatus status = vadGetStatus();

    Serial.printf(
        "[VAD] C rms=%u frames=%lu state=%u\n",
        (unsigned)status.rms,
        (unsigned long)status.framesProcessed,
        (unsigned)status.state
    );

    vadReset();
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
    testVadRms();
    createdMicResultQueue();
	initMic(onMicFrame);
}

// ===== HÀM LOOP =====
void loop()
{
    MicResult result;

    if (micResultQueue != nullptr)
    {
        while (xQueueReceive(micResultQueue, &result, 0) == pdTRUE)
        {
            bool isError =
                result.reason != MicResultReason::Success &&
                result.reason != MicResultReason::Replaced;

            bool shouldLog =
                (result.ok && audio::LOG_MIC_SUCCESS) ||
                (isError && audio::LOG_MIC_ERROR);

            if (shouldLog)
            {
                Serial.printf(
                    "[%lu] [MIC] turn=%ld frames=%lu "
                    "elapsed=%lu ms ok=%d reason=%u pushError=%u\n",
                    (unsigned long)millis(),
                    (long)result.turn,
                    (unsigned long)result.frames,
                    (unsigned long)result.elapsedMs,
                    result.ok ? 1 : 0,
                    (unsigned)result.reason,
                    (unsigned)result.pushError
                );
            }
        }
    }

    uint32_t lost = droppedMicResults.exchange(0);

    if (lost > 0)
    {
        Serial.printf(
            "[MIC] Mat %lu ban ghi ket qua\n",
            (unsigned long)lost
        );
    }

    delay(10);
}