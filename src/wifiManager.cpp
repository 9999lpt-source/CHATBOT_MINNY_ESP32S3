#include "wifiManager.h"
#include "config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

// ===== DỮ LIỆU =====
struct SavedNetwork
{
	String ssid;
	String pass;
};

static SavedNetwork saved[net::MAX_SAVED_WIFI];
static int savedCount = 0;

// ===== KHAI BÁO CÁC THÀNH PHẦN =====
static Preferences prefs;
static WebServer server(80);
static DNSServer dns;
static bool leaveRequested = false; // handler đặt true để thoát portal

static WifiStatusCallback statusCallback = NULL; // gọi hàm bên ngoài khi thay đổi currentStatus
static volatile WifiStatus currentStatus = WifiStatus::Scanning;

// ===== HÀM CẬP NHẬT TRẠNG THÁI WIFI =====
static void setStatus(WifiStatus s)
{
	currentStatus = s;
	if (statusCallback != NULL)
		statusCallback(s);
}

// ===== LẤY DỮ LIỆU WIFI VÀO FLASH =====
static void loadNetworks()
{
	prefs.begin("minny_wifi", true);
	savedCount = prefs.getInt("count", 0);
	if (savedCount > net::MAX_SAVED_WIFI)
		savedCount = net::MAX_SAVED_WIFI;
	for (int i = 0; i < savedCount; i++)
	{
		saved[i].ssid = prefs.getString(("ssid_" + String(i)).c_str(), "");
		saved[i].pass = prefs.getString(("pass_" + String(i)).c_str(), "");
	}
	prefs.end();
	Serial.printf("[WIFI] Doc %d mang tu flash\n", savedCount);
}

// ===== LƯU DỮ LIỆU WIFI TỪ FLASH =====
static void saveNetworks()
{
	prefs.begin("minny_wifi", false);
	prefs.putInt("count", savedCount);
	for (int i = 0; i < savedCount; i++)
	{
		prefs.putString(("ssid_" + String(i)).c_str(), saved[i].ssid);
		prefs.putString(("pass_" + String(i)).c_str(), saved[i].pass);
	}
	prefs.end();
}

// ===== KẾT NỐI ĐẾN WIFI ĐÃ LƯU (true/false) =====
static bool connectToKnownNetwork()
{
	//=== TRẠNG THÁI QUÉT ===
	setStatus(WifiStatus::Scanning);
	WiFi.mode(WIFI_STA);
	WiFi.disconnect();
	int n = WiFi.scanNetworks();
	Serial.printf("[WIFI] Quet thay %d mang\n", n);

	// nếu không tìm thấy wifi nào -> trả về false
	if (n <= 0)
		return false;

	// nếu tìm thấy wifi -> kiểm tra -> thử kết nối
	for (int i = 0; i < n; i++)
	{
		String scanned = WiFi.SSID(i);

		// lặp qua từng ssid tìm thấy
		for (int j = 0; j < savedCount; j++)
		{
			// chưa lưu -> bỏ qua
			if (scanned != saved[j].ssid)
				continue;

			// === TRẠNG THÁI ĐANG KẾT NỐI ===
			Serial.printf("[WIFI] Thu noi '%s'\n", scanned.c_str());
			setStatus(WifiStatus::Connecting);
			WiFi.begin(saved[j].ssid.c_str(), saved[j].pass.c_str());

			uint32_t start = millis();
			while (WiFi.status() != WL_CONNECTED &&
				   millis() - start < net::WIFI_CONNECT_TIMEOUT_MS)
			{
				vTaskDelay(pdMS_TO_TICKS(200));
			}

			// kết nối thành công
			if (WiFi.status() == WL_CONNECTED)
			{
				WiFi.scanDelete();
				Serial.print("[WIFI] OK, IP: ");
				Serial.println(WiFi.localIP());
				return true;
			}
			WiFi.disconnect();
		}
	}

	// không có wifi nào đã lưu -> xóa danh sách quét -> trả về false
	WiFi.scanDelete();
	return false;
}

// ===== chặn ký tự đặc biệt chèn vào HTML =====
static String esc(const String &s)
{
	String o;
	for (unsigned i = 0; i < s.length(); i++)
	{
		char c = s[i];
		if (c == '<')
			o += "&lt;";
		else if (c == '>')
			o += "&gt;";
		else if (c == '&')
			o += "&amp;";
		else if (c == '\'')
			o += "&#39;";
		else if (c == '"')
			o += "&quot;";
		else
			o += c;
	}
	return o;
}

// ===== HÀM LẮNG NGHE KHI KẾT NỐI VÀO SERVER (ESP32) =====
static void handleRoot()
{
	String h = "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
			   "<meta name='viewport' content='width=device-width,initial-scale=1'>"
			   "<title>MINNY WiFi</title><style>"
			   "body{font-family:Arial;background:#121212;color:#fff;padding:15px}"
			   ".c{background:#1e1e1e;padding:20px;border-radius:12px;max-width:400px;margin:auto}"
			   "h2,h3{color:#00e676;text-align:center}"
			   "input{width:90%;padding:10px;margin:5px 0;background:#2a2a2a;color:#fff;border:1px solid #444;border-radius:6px}"
			   "input[type=submit],.b{background:#00e676;color:#000;font-weight:bold;border:none;cursor:pointer;width:100%;padding:10px;margin-top:10px;border-radius:6px;display:block;text-align:center;text-decoration:none}"
			   ".i{background:#2a2a2a;padding:10px;margin:8px 0;border-radius:6px}"
			   ".d{background:#ff5252;color:#fff;padding:4px 8px;border-radius:4px;text-decoration:none;float:right}"
			   "</style></head><body><div class='c'><h2>MINNY WIFI</h2>";

	h += "<h3>Đã lưu (" + String(savedCount) + "/" + String(net::MAX_SAVED_WIFI) + ")</h3>";
	if (savedCount == 0)
		h += "<p style='text-align:center;color:#aaa'>Chưa có WiFi nào</p>";
	for (int i = 0; i < savedCount; i++)
		h += "<div class='i'><b>" + esc(saved[i].ssid) + "</b><a class='d' href='/delete?id=" + String(i) + "'>Xóa</a></div>";

	if (savedCount < net::MAX_SAVED_WIFI)
	{
		h += "<h3>Thêm WiFi</h3><form action='/save' method='POST'>"
			 "<input name='ssid' placeholder='Tên WiFi' required>"
			 "<input type='password' name='pass' placeholder='Mật khẩu'>"
			 "<input type='submit' value='Lưu'></form>";
	}
	h += "<a class='b' href='/connect'>KẾT NỐI NGAY</a></div></body></html>";
	server.send(200, "text/html", h);
}

// ===== TRẢ VỀ TRANG CHÍNH CỦA CẤU HÌNH =====
static void redirectHome()
{
	server.sendHeader("Location", "/");
	server.send(302, "text/plain", "");
}

// ===== HÀM ĐĂNG KÝ CÁC DƯỜNG TRUYỀN HTTP =====
static void registerRoutes()
{
	// trang chính
	server.on("/", HTTP_GET, handleRoot);

	// nhấn nút lưu
	server.on("/save", HTTP_POST, []()
			  {
		String ssid = server.arg("ssid");
		String pass = server.arg("pass");
		if (ssid.length() > 0)
		{
			int idx = -1;
			for (int i = 0; i < savedCount; i++)
				if (saved[i].ssid == ssid) idx = i;

			if (idx < 0 && savedCount < net::MAX_SAVED_WIFI)
				idx = savedCount++;

			if (idx >= 0)
			{
				saved[idx].ssid = ssid;
				saved[idx].pass = pass;
				saveNetworks();
			}
		}
		redirectHome(); });

	// Nhấn nút xóa
	server.on("/delete", HTTP_GET, []()
			  {
		int id = server.arg("id").toInt();
		if (server.hasArg("id") && id >= 0 && id < savedCount)
		{
			for (int i = id; i < savedCount - 1; i++)
				saved[i] = saved[i + 1];
			savedCount--;
			saveNetworks();
		}
		redirectHome(); });

	// Nhấn nút "kết nối ngay"
	server.on("/connect", HTTP_GET, []()
			  {
		server.send(200, "text/html", "<meta charset='UTF-8'><h2>Đang kết nối...</h2>");
		leaveRequested = true; });

	// captive portal: mọi địa chỉ đều về trang chủ
	server.onNotFound(handleRoot);
}

// ===== MỞ CẤU HÌNH =====
static void runPortal()
{
	Serial.println("[WIFI] Mo portal cau hinh");
	// === TRẠNG THÁI CẤU HÌNH ===
	setStatus(WifiStatus::Portal);

	WiFi.mode(WIFI_AP);
	WiFi.softAP(net::AP_SSID);

	dns.start(53, "*", WiFi.softAPIP());
	server.begin();

	leaveRequested = false;
	uint32_t lastActivity = millis();

	while (!leaveRequested)
	{
		dns.processNextRequest();
		server.handleClient();

		if (WiFi.softAPgetStationNum() > 0)
			lastActivity = millis(); // có người đang kết nối vào: không hết giờ
		else if (savedCount > 0 && millis() - lastActivity >= net::PORTAL_TIMEOUT_MS)
			break; // có mạng đã lưu mà không ai cấu hình: quay lại thử kết nối

		vTaskDelay(pdMS_TO_TICKS(10));
	}

	server.stop();
	dns.stop();
	WiFi.softAPdisconnect(true);
	WiFi.mode(WIFI_STA);
}

// ===== TASK CHÍNH =====
static void wifiTask(void *pvParameters)
{
	// === lấy danh sách wifi đã lưu ===
	loadNetworks();

	for (;;)
	{
		bool connected = false;

		// === quét - kết nối wifi ===
		if (savedCount > 0)
		{
			for (int round = 0; round < net::WIFI_MAX_ROUNDS && !connected; round++)
			{
				connected = connectToKnownNetwork();
				if (!connected)
					vTaskDelay(pdMS_TO_TICKS(net::WIFI_RETRY_MS));
			}
		}

		// === mất kết nối / quét không ra -> mở cấu hình ===
		if (!connected)
		{
			runPortal();
			// cấu hình xong -> quay trở lại quét
			continue;
		}

		// ===== TRẠNG THÁI ĐÃ KẾT NỐI =====
		setStatus(WifiStatus::Connected);
		// lặp cho đến khi mất kết nối
		while (WiFi.status() == WL_CONNECTED)
			vTaskDelay(pdMS_TO_TICKS(500));

		Serial.println("[WIFI] Mat ket noi!");
	}
}

// ===== HÀM CẤU HÌNH WIFI =====
void initWifi(WifiStatusCallback onChange)
{
	statusCallback = onChange;
	WiFi.persistent(false);		  // không ghi thông tin WiFi vào flash mỗi lần begin()
	WiFi.setAutoReconnect(false); // ta tự quản việc nối lại
	registerRoutes();

	xTaskCreatePinnedToCore(wifiTask, task::WIFI.name, task::WIFI.stack,
							NULL, task::WIFI.prio, NULL, task::WIFI.core);
}

// ===== HÀM BÁO TRẠNG THÁI KẾT NỐI WIFI =====
bool isWifiConnected() { return WiFi.status() == WL_CONNECTED; }

// ===== HÀM LẤY TRẠNG THÁI KẾT NỐI WIFI =====
WifiStatus wifiGetStatus() { return currentStatus; }