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

// =====
static void setConnected(bool value)
{
	if (connected == value)
		return;
	connected = value;
	statusCallback(value ? WsStatus::Connected : WsStatus::Disconnected);
}

// ======
static void onWsEvent(WStype_t type, uint8_t *payload, size_t length)
{
	switch (type)
	{

	case WStype_CONNECTED:
		Serial.printf("[WS] Da ket noi den: %s\n", (const char *)payload);
		break;

	case WStype_DISCONNECTED:
		Serial.println("[WS] Mat ket noi");
		setConnected(false);
		break;

	default:
		break;
	}
}

//=====
static void wsTask(void *pvParameters)
{
	ws.onEvent(onWsEvent);
	ws.setReconnectInterval(net::WS_RECONNECT_MS);

	for (;;)
	{
		bool want = wantConnected;

		if (want && !began)
		{
			Serial.printf("[WS] bat dau ket noi %s:%u\n", net::WS_HOST, net::WS_PORT);
			if (net::WS_USE_SSL)
				ws.beginSSL(net::WS_HOST, net::WS_PORT, net::WS_PATH);
			else
				ws.begin(net::WS_HOST, net::WS_PORT, net::WS_PATH);
			began = true;
		}
		else if (!want && began)
		{
			ws.disconnect();
			began = false;
			setConnected(false);
		}
	}
}