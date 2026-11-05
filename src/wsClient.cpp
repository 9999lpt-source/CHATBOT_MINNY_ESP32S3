#include "wsClient.h"
#include <Arduino.h>
#include <WebSocketsClient.h>
#include "config.h"

// ===== DỮ LIỆU NỘI BỘ =====
static WebSocketsClient ws;
static WsStatusCallback statusCallback = NULL;
static volatile bool wantConnected = false;
static bool began = false;
static bool connected = false;

// ===== CẬP NHẬT TRẠNG THÁI KẾT NỐI =====
static void setConnected(bool value)
{
	if (connected == value)
		return;
	connected = value;
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

		vTaskDelay(pdMS_TO_TICKS(net::WS_LOOP_MS));
	}
}

// ===== CẤU HÌNH WEBSOCKET =====
void initWs(WsStatusCallback onChange)
{
	statusCallback = onChange;
	xTaskCreatePinnedToCore(wsTask, task::NETWORK.name, task::NETWORK.stack,
							NULL, task::NETWORK.prio, NULL, task::NETWORK.core);
}

// =====
void wsStart() { wantConnected = true; }
void wsStop() { wantConnected = false; }