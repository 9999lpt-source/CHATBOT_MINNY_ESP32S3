#ifndef WIFI_MANAGE_H
#define WIFI_MANAGE_H

enum class WifiStatus
{
	Scanning,	// Đang tìm kiếm
	Connecting, // Đang kết nối
	Connected,	// Đã kết nối
	Portal		// Đang phát cấu hình
};

typedef void (*WifiStatusCallback)(WifiStatus status);

void initWifi(WifiStatusCallback onChange); // Hàm cấu hình Wifi
bool isWifiConnected();						// hàm báo trạng thái Wifi
WifiStatus wifiGetStatus();					// hàm lấy trạng thái Wifi

#endif // WIFI_MANAGE_H