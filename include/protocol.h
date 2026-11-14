#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

enum class ClientMsg : uint8_t
{
	AudioStart,
	AudioEnd,
	Cancel
};

constexpr int32_t NO_TURN = -1;

size_t buildClientMsg(char *out, size_t outSize, ClientMsg type, int32_t turn);

// ===== TIN SERVER -> ESP32 =====
enum class ServerMsg: uint8_t
{
	State,
	Command,
	TtsStart,
	TtsEnd,
	Error,
	MusicTitle
};

// ===== CÁC GIÁ TRỊ CỦA STATE =====
enum class ServerState: uint8_t
{
	None,
	Transcribing,
	Thingking
};

// ===== CÁC GIÁ TRỊ CỦA ERROR =====
enum class ErrorCode: uint8_t
{
	None,
	Unknown,
	BadMessage,
	AudioTooShort,
	SttFailed,
	SttEmpty,
	LlmFailed,
	TtsFailed,
	MusicNotFound,
	Internal
};

// ===== CẤU TRÚC TYPE TỪ SERVER =====
struct ServerMessage {
	ServerMsg type;
	int32_t turn;		// NO_TURN khi không có turn
	ServerState state;	// Chỉ có nghĩa khi type = state
	ErrorCode code;		// Chỉ có nghĩa khi type = error
	bool recoverable;	// Chỉ có nghĩa khi type = error
};

constexpr size_t MAX_SERVER_MSG = 512; // tin dài hơn thì bỏ qua

// TRUE: thành công -> out = kết quả, FALSE: thất bại -> out không đổi.
bool parseServerMessage(const char *json, size_t len, ServerMessage &out);

#endif // PROTOCOL_H
