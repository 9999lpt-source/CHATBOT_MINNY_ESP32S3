#include "wsClient.h"
#include "config.h"
#include "protocol.h"
#include <Arduino.h>
#include "freertos/stream_buffer.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include <atomic>
#include <WebSocketsClient.h>

// ===== DỮ LIỆU NỘI BỘ =====
static WebSocketsClient ws;
static WsStatusCallback statusCallback = NULL;
static WsMessageCallback messageCallback = NULL;
static volatile bool wantConnected = false;
static bool began = false;
static volatile bool connected = false;

//===== BỘ NHỚ ĐỆM ÂM THANH =====
static uint8_t* audioStorage = nullptr; 			// Con trỏ đến vùng chứa PCM trong PSRAM
static StaticStreamBuffer_t audioStruct;			// Cau truc quan ly
static StreamBufferHandle_t audioStream = nullptr;	// Handle các Streambuffer sử dụng.

// TRạng thái PCM (chờ, gửi, xả)
enum class AudioPhase : uint8_t
{
	Waiting,
	Streaming,
	Draining
};
static AudioPhase audioPhase = AudioPhase::Waiting;

static int32_t audioTurn = NO_TURN;
static SemaphoreHandle_t audioMutex = nullptr;
static std::atomic<int32_t> failedAudioTurn(NO_TURN);


//===== CẤU TRÚC CLIENT MESSAGE =====
struct ControlMsg
{
	ClientMsg type;
	int32_t turn;
};
static QueueHandle_t ctrlQueue = NULL;

// ===== HÀM CHỐNG LỖI LƯỢT CŨ ĐÈ LỖI LƯỢT MỚI =====
static void markAudioFailure(int32_t turn)
{
	if (turn < 0) return;

	int32_t previous = failedAudioTurn.load();

	while(previous < turn)
		if (failedAudioTurn.compare_exchange_weak(previous, turn)) break;
}

// ===== SẴN SÀNG NHẬN PCM TỪ BÊN NGOÀI =====
bool wsAudioReady(int32_t turn)
{
	if (audioMutex == nullptr)
		return false;
	if (xSemaphoreTake(audioMutex, 0) != pdTRUE)
		return false;

	bool ready = connected &&							// đã kết nối
				audioPhase == AudioPhase::Streaming &&	// đang gửi PCM
				audioTurn == turn &&					// đúng lượt hiện tại
				failedAudioTurn.load() != turn;			// không phải lượt lỗi

	xSemaphoreGive(audioMutex);
	return ready;
}

// ===== NHẬN PCM =====
bool wsPushAudio(const uint8_t *data, size_t len, int32_t turn)
{
	// 1. Kiểm tra đầu vào
	if (data == nullptr || len == 0 || (len % 2) != 0)
		return false;
	if (audioMutex == nullptr || audioStream == nullptr)
		return false;
	
	// 2. chiếm quyền truy cập không chờ đợi (audioMutex, 0)
	if (xSemaphoreTake(audioMutex, 0) != pdTRUE)
	{
		markAudioFailure(turn);
		return false;
	}

	// 3. Đảm bảo kết nối, đúng phiên, đúng lượt
	bool valid = connected &&
				audioPhase == AudioPhase::Streaming &&
				audioTurn == turn &&
				failedAudioTurn.load() != turn;
	if (!valid)
	{
		xSemaphoreGive(audioMutex);
		return false;
	}

	// 4. Chống chàn & ghi nguyên khung
	if (xStreamBufferSpacesAvailable(audioStream) < len) //kiểm tra chỗ trống
	{
		markAudioFailure(turn);
		xSemaphoreGive(audioMutex);
		return false;
	}

	// ghi vào bộ đệm
	size_t written = xStreamBufferSend(audioStream, data, len, 0);

	if (written != len)
		markAudioFailure(turn);

	xSemaphoreGive(audioMutex);
	return written == len;
}

void wsFlagAudioFailure(int32_t turn)
{
	markAudioFailure(turn);
}

// ===== RESET BỘ ĐỆM, TRẠNG THÁI =====
static bool resetLocalAudio()
{
	if (audioMutex == nullptr || audioStream == nullptr)
		return false;
	if (xSemaphoreTake(audioMutex, pdMS_TO_TICKS(20)) != pdTRUE)
		return false;

	audioPhase = AudioPhase::Waiting;
	audioTurn = NO_TURN;

	BaseType_t ok = xStreamBufferReset(audioStream);

	xSemaphoreGive(audioMutex);
	return ok == pdPASS;
}

// ===== HÀM GỬI TIN JSON =====
static bool sendControlNow(ClientMsg type, int32_t turn)
{
	char buf[net::WS_MSG_BUF_SIZE];
	size_t len = buildClientMsg(buf, sizeof(buf), type, turn);
	
	if (len == 0)
		return false;

	bool ok = ws.sendTXT(buf, len);

	if (ok && connected)
		Serial.printf("[WS] GUI: %s\n", buf);

	return ok && connected;
}

// ===== CẬP NHẬT TRẠNG THÁI KẾT NỐI =====
static void setConnected(bool value)
{
	if (connected == value)
		return;

	if (value) // XẢ HÀNG ĐỢI, RESET BUFFER KHI NỐI
	{
		xQueueReset(ctrlQueue);
		
		if (!resetLocalAudio())
		{
			Serial.println("[WS] reset audio khi noi that bai");
			return;
		}

		connected = true;
	}
	else // XẢ HÀNG ĐỢI, RESET BUFFER KHI NGẮT
	{
		connected = false;
		xQueueReset(ctrlQueue);
		if (!resetLocalAudio())
			Serial.println("[WS] reset audio khi ngat that bai");
	}

	if (statusCallback != NULL)
		statusCallback(value ? WsStatus::Connected : WsStatus::Disconnected);
}

// ===== XỬ LÝ LỖI TRUYỀN MẠNG =====
static void failTransport()
{
	Serial.println("[WS] Loi truyen - ngat ket noi");
	ws.disconnect();
	setConnected(false);
}

// ====== LẮNG NGHE SỰ KIỆN WS =====
static void onWsEvent(WStype_t type, uint8_t *payload, size_t length)
{
	switch (type)
	{
	// có kết nối
	case WStype_CONNECTED:
		Serial.printf("[WS] Da ket noi den: %s\n", (const char *)payload);
		setConnected(true);
		break;

	// ngắt kết nối
	case WStype_DISCONNECTED:
		// Serial.println("[WS] Mat ket noi");
		setConnected(false);
		break;

	// nhận được ti mới
	case WStype_TEXT:
	{
		ServerMessage m;
		if (parseServerMessage((const char *)payload, length, m))
		{
			if (messageCallback != NULL)
				messageCallback(m);
		}
		else
		{
			size_t n = (length < 100) ? length : 100;
			Serial.printf("[WS] Tin khong hop le (%u byte): %.*s\n",
							(unsigned)length, (int)n, (const char *)payload);
		}
		break;
	}

	default:
		break;
	}
}

// ===== CÁC HÀM CON TRONG wsTask() =====
// === TRẠNG THÁI KẾT NỐI WS =====
static void wsTaskConnection()
{
	bool want = wantConnected;

	if (want && !began) // NỐI
	{
		if (net::WS_USE_SSL)
			ws.beginSSL(net::WS_HOST, net::WS_PORT, net::WS_PATH);
		else
			ws.begin(net::WS_HOST, net::WS_PORT, net::WS_PATH);

		ws.enableHeartbeat(net::WS_PING_INTERVAL_MS, net::WS_PONG_TIMEOUT_MS, net::WS_PONG_MISS_MAX);
		began = true;
	}
	else if (!want && began) // NGẮT
	{
		ws.disconnect();
		began = false;
		setConnected(false);
	}

	if (began) // ĐANG NỐI
		ws.loop();
}

static void dropTransport()
{
    failTransport();
    began = false;
}

// === XỬ LÝ TÍN HIỆU audio_start ===
static void wsTaskAudioStart(ControlMsg m, int32_t &lastStartedTurn)
{
	if (m.turn > lastStartedTurn)
	{
		if (!resetLocalAudio() || !sendControlNow(ClientMsg::AudioStart, m.turn))
			dropTransport();
		else
		{
			lastStartedTurn = m.turn;
			if (xSemaphoreTake(audioMutex, pdMS_TO_TICKS(20)) == pdTRUE)
			{
				audioTurn = m.turn;
				audioPhase = AudioPhase::Streaming;
				xSemaphoreGive(audioMutex);
			}
			else dropTransport();
		}
	}
}

// === XỬ LÝ TÍN HIỆU audio_end ===
static void wsTaskAudioEnd(ControlMsg m)
{
	if (xSemaphoreTake(audioMutex, pdMS_TO_TICKS(20)) == pdTRUE)
	{
		if (audioTurn == m.turn && audioPhase == AudioPhase::Streaming)
			audioPhase = AudioPhase::Draining;
		xSemaphoreGive(audioMutex);
	}
	else dropTransport();
}

// === XỬ LÝ TÍN HIỆU cancel ===
static void wsTaskCancel(ControlMsg m)
{
	int32_t oldTurn = NO_TURN;
	if (xSemaphoreTake(audioMutex, pdMS_TO_TICKS(20)) == pdTRUE)
	{
		if (audioTurn != NO_TURN && (m.turn == NO_TURN || m.turn == audioTurn))
			oldTurn = audioTurn;
		xSemaphoreGive(audioMutex);
	}
	else dropTransport();

	if (oldTurn != NO_TURN && connected)
	{
		if (!resetLocalAudio() || !sendControlNow(ClientMsg::Cancel, oldTurn))
			dropTransport();
	}
}

// === TRUYỀN PCM VÀ KẾT THÚC LƯỢT ===
static void wsTaskPumpAudio(uint8_t *chunk, size_t chunkSize)
{
    size_t n = 0;
    int32_t turn = NO_TURN;
    bool broken = false;
    bool endReady = false;

	//giữ mutex
	if (xSemaphoreTake(audioMutex, pdMS_TO_TICKS(20)) == pdTRUE)
	{
		turn = audioTurn;
		if (audioPhase != AudioPhase::Waiting)
		{
			broken = (failedAudioTurn.load() == turn);
			if (!broken)
			{
				n = xStreamBufferReceive(audioStream, chunk, chunkSize, 0);
				endReady = (n == 0 && audioPhase == AudioPhase::Draining);
			}
		}
		xSemaphoreGive(audioMutex);
	}
	else dropTransport();

	if (!began && !connected)
		return;

	// 1. Lượt hiện tại bị đánh dấu lỗi PCM
	if (broken)
	{
		Serial.printf("[WS] huy luot loi PCM: %ld\n", (long)turn);
		if (!resetLocalAudio() || !sendControlNow(ClientMsg::Cancel, turn))
			dropTransport();
	}

	// 2. Lượt bình thường (n byte PCM đọc từ PSRAM)
	else if (n > 0)
	{
		// Không giữ mutex khi gửi mạng
		if (!ws.sendBIN(chunk, n))
			dropTransport();
	}

	 // 3. Gửi PCM xong
	else if (endReady)
	{
		// Kiểm tra lần cuối
		if (failedAudioTurn.load() == turn)
		{
			if (!resetLocalAudio()||!sendControlNow(ClientMsg::Cancel, turn))
				dropTransport();
		}
		// Gửi audio_end
		else if (!sendControlNow(ClientMsg::AudioEnd, turn))
			dropTransport();

		// Reset trạng thái, buffer
		else if(!resetLocalAudio())
    		dropTransport();
	}
}

//===== NETWORK TASK (TASK CHÍNH) =====
static void wsTask(void *pvParameters)
{
	ws.onEvent(onWsEvent);
	ws.setReconnectInterval(net::WS_RECONNECT_MS);

	int32_t lastStartedTurn = NO_TURN;
	uint8_t chunk[audio::WS_AUDIO_CHUNK_BYTES];

	for (;;)
	{
		// === NỐI, NGẮT, GIỮ WS ===
		wsTaskConnection();

		// === XỬ LÝ TÍN HIỆU ĐIỀU KHIỂN ===
		if (began && connected)
		{
			ControlMsg m;
			if (xQueueReceive(ctrlQueue, &m, 0) == pdTRUE)
			{
				if (m.type == ClientMsg::AudioStart)
					wsTaskAudioStart(m, lastStartedTurn);
				else if (m.type == ClientMsg::AudioEnd)
					wsTaskAudioEnd(m);
				else if (m.type == ClientMsg::Cancel)
					wsTaskCancel(m);
			}
		}

		// === TRUYỀN PCM / LỆNH KẾT THÚC ===
		if (began && connected)
		{
			wsTaskPumpAudio(chunk, sizeof(chunk));
		}
		vTaskDelay(pdMS_TO_TICKS(net::WS_LOOP_MS));
	}
}

// ===== HÀM HỖ TRỢ KHỞI TẠO, DỌN TÀI NGUYÊN =====
static void cleanupWsInit()
{
	if (audioMutex != nullptr)
	{
		vSemaphoreDelete(audioMutex);
		audioMutex = nullptr;
	}

	if (ctrlQueue != nullptr)
	{
		vQueueDelete(ctrlQueue);
		ctrlQueue = nullptr;
	}

	if (audioStream != nullptr)
	{
		vStreamBufferDelete(audioStream);
		audioStream = nullptr;
	}

	if (audioStorage != nullptr)
	{
		heap_caps_free(audioStorage);
		audioStorage = nullptr;
	}
}

// ===== CẤU HÌNH WEBSOCKET =====
void initWs(WsStatusCallback onStatus, WsMessageCallback onMessage)
{
	if (audioStorage != nullptr || ctrlQueue != nullptr)
		return;

	// CẤP PHÁT BỘ NHỚ CHỨ PCM TRONG PSRAM
	audioStorage = static_cast<uint8_t*>(heap_caps_malloc(audio::STREAM_STORAGE_BYTES,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
	if (audioStorage != nullptr) Serial.println("[WS] PSRAM OK");
	else if (audioStorage == nullptr)
	{
		Serial.println("[WS] PSRAM FAIL");
		return;
	}

	// TẠO STREAMBUFFER
	audioStream = xStreamBufferCreateStatic
	(
		audio::STREAM_STORAGE_BYTES,
		audio::STREAM_TRIGGER_BYTES,
		audioStorage, &audioStruct
	);
	if (audioStream != nullptr) Serial.println("[WS] STREAMBUFFER OK");
	else if (audioStream == nullptr)
	{
		Serial.println("[WS] StreamBuffer FAIL");
		cleanupWsInit();
		return;
	}

	// TẠO HÀNG ĐỢI
	ctrlQueue = xQueueCreate(net::WS_CTRL_QUEUE_LEN, sizeof(ControlMsg));
	if (ctrlQueue != NULL) Serial.println("[WS] QUEUE OK");
	else if (ctrlQueue == NULL)
	{
		Serial.println("[WS] Queue FAIL");
		cleanupWsInit();
		return;
	}

	// CHUẨN BỊ MUTEX
	audioMutex = xSemaphoreCreateMutex();
	if (audioMutex != nullptr) Serial.println("[WS] Audio mutex OK");
	else if (audioMutex == nullptr)
	{
		Serial.println("[WS] Audio mutex FAIL");
		cleanupWsInit();
		return;
	}

	// TẠO Ws_Task
	statusCallback = onStatus;
	messageCallback = onMessage;
	BaseType_t ok = xTaskCreatePinnedToCore(
		wsTask,
		task::NETWORK.name,
		task::NETWORK.stack,
		nullptr,
		task::NETWORK.prio,
		nullptr,
		task::NETWORK.core
	);

	// Khởi tạo Network_Task
	if (ok == pdPASS) Serial.println("[WS] Network_Task OK");
	else if (ok != pdPASS)
	{
		Serial.println("[WS] Network_Task FAIL");
		cleanupWsInit();
		return;
	}

	Serial.printf("[WS] Audio ready, capacity=%u bytes\n",(unsigned)xStreamBufferSpacesAvailable(audioStream));
}

// ===== MỞ KẾT NỐI ĐẾN SERVER =====
void wsStart() { wantConnected = true; }
// ===== NGẮT KẾT NỐI SERVER =====
void wsStop() { wantConnected = false; }

// ===== LƯU TIN VÀO QUEUE =====
bool wsSendControl(ClientMsg type, int32_t turn)
{
	if (ctrlQueue == NULL || !connected)
		return false;
	ControlMsg m = {type, turn};
	return xQueueSend(ctrlQueue, &m, 0) == pdTRUE;
}