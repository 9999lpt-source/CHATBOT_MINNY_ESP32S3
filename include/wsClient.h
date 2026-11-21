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

bool wsAudioReady(int32_t turn);									// sẵn sàng nhận PCM
bool wsPushAudio(const uint8_t *data, size_t len, int32_t turn);	// Nhận PCM (TRUE: toàn bô PCM -> khung)

void wsFlagAudioFailure(int32_t turn);

#endif // WS_CLIENT_H