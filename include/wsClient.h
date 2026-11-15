#ifndef WS_CLIENT_H
#define WS_CLIENT_H

#include "protocol.h"

enum class WsStatus
{
	Disconnected,
	Connected
};

typedef void (*WsStatusCallback)(WsStatus status);
typedef void (*WsMessageCallback)(const ServerMessage &msg);

void initWs(WsStatusCallback onStatus, WsMessageCallback onMessage);
void wsStart(); // muốn kết nói (chỉ gắn cờ)
void wsStop();	// muốn ngắt (chỉ gắn cờ)

bool wsSendControl(ClientMsg type, int32_t turn); // false: chưa nối / queue đầy

#endif // WS_CLIENT_H