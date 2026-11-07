#include "wsClient.h"
#include "config.h"
#include "protocol.h"
#include <Arduino.h>
#include <WebSocketsClient.h>

// ===== DỮ LIỆU NỘI BỘ =====
static WebSocketsClient ws;
static WsStatusCallback statusCallback = NULL;
static volatile bool wantConnected = false;
static bool began = false;
static volatile bool connected = false;

//===== CẤU TRÚC CLIENT MESSAGE =====
struct ControlMsg
{
	ClientMsg type;
	int32_t turn;
};
static QueueHandle_t ctrlQueue = NULL;

// ===== CẬP NHẬT TRẠNG THÁI KẾT NỐI =====
static void setConnected(bool value)
{
	if (connected == value)
		return;

	// Xả hàng đợi TRƯỚC khi nối server
	if (value)
		xQueueReset(ctrlQueue);
	connected = value;

	// Xả hàng đợi SAU khi ngắt nối server
	if (!value)
		xQueueReset(ctrlQueue);

	if (statusCallback != NULL)
		statusCallback(value ? WsStatus::Connected : WsStatus::Disconnected);
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
		Serial.println("[WS] Mat ket noi");
		setConnected(false);
		break;

	default:
		break;
	}
}

//===== NETWORK TASK (TASK CHÍNH) =====
static void wsTask(void *pvParameters)
{
	ws.onEvent(onWsEvent);
	ws.setReconnectInterval(net::WS_RECONNECT_MS);

	for (;;)
	{
		bool want = wantConnected;

		// có yêu cầu nối khi chưa kết nối
		if (want && !began)
		{
			Serial.printf("[WS] bat dau ket noi %s:%u\n", net::WS_HOST, net::WS_PORT);
			if (net::WS_USE_SSL)
				ws.beginSSL(net::WS_HOST, net::WS_PORT, net::WS_PATH);
			else
				ws.begin(net::WS_HOST, net::WS_PORT, net::WS_PATH);
			began = true;
		}

		// có yêu cầu ngắt khi đã nối
		else if (!want && began)
		{
			ws.disconnect();
			began = false;
			setConnected(false);
		}

		// đang nối
		if (began)
			ws.loop();

		// Hàng gửi
		if (began && connected)
		{
			// Đọc tin trong Queue
			ControlMsg m;
			char buf[net::WS_MSG_BUF_SIZE];
			while (xQueueReceive(ctrlQueue, &m, 0) == pdTRUE)
			{
				// Dựng json
				size_t len = buildClientMsg(buf, sizeof(buf), m.type, m.turn);
				if (len == 0)
				{
					Serial.println("[WS] Dung tin that bai, bo qua");
					continue;
				}

				// Gửi qua server
				Serial.printf("[WS] Gui: %s\n", buf);
				if (!ws.sendTXT(buf, len))
					Serial.print("[WS] Gui that bai");
			}
		}

		vTaskDelay(pdMS_TO_TICKS(net::WS_LOOP_MS));
	}
}

// ===== CẤU HÌNH WEBSOCKET =====
void initWs(WsStatusCallback onChange)
{
	ctrlQueue = xQueueCreate(net::WS_CTRL_QUEUE_LEN, sizeof(ControlMsg));
	if (ctrlQueue == NULL)
	{
		Serial.println("[WS] Khong tao duoc Queue");
		return;
	}
	statusCallback = onChange;
	xTaskCreatePinnedToCore(wsTask, task::NETWORK.name, task::NETWORK.stack,
							NULL, task::NETWORK.prio, NULL, task::NETWORK.core);
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