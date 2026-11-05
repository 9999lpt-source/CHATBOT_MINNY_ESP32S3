#ifndef WS_CLIENT_H
#define WS_CLIENT_H

enum class WsStatus
{
	Disconnected,
	Connected
};

typedef void (*WsStatusCallback)(WsStatus status);

void initWs(WsStatusCallback onChange);
void wsStart(); // muốn kết nói (chỉ gắn cờ)
void wsStop();	// muốn ngắt (chỉ gắn cờ)

#endif // WS_CLIENT_H