#include <Arduino.h>
#include "controlOled.h"
#include "button.h"
#include "wifiManager.h"
#include "wsClient.h"
#include "protocol.h"
#include <WiFi.h>

int pressCount = 0;

// ===== HÀM GỌI KHI NHẤN NÚT =====
static void onButtonPress()
{
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

void setup()
{
	Serial.begin(115200);
	delay(1500);
	initOled();
	initButton(onButtonPress);
	oledSetText("0");
	initWs(onWsStatus);
	initWifi(onWifiStatus);
}

void loop()
{
	vTaskDelay(pdMS_TO_TICKS(1000));
}