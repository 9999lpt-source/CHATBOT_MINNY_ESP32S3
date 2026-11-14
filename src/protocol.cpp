#include "protocol.h"
#include <stdio.h>
#include <ArduinoJson.h>
#include <string.h>

// ========== ESP32 -> SERVER ========== 
static const char *typeName(ClientMsg type)
{
	switch (type)
	{
	case ClientMsg::AudioStart:
		return "audio_start";
	case ClientMsg::AudioEnd:
		return "audio_end";
	case ClientMsg::Cancel:
		return "cancel";
	}
	return NULL;
}

size_t buildClientMsg(char *out, size_t outSize, ClientMsg type, int32_t turn)
{
	// Kiểm tra vùng nhớ được cấp
	if (out == NULL || outSize == 0)
		return 0;

	// Chống tin rác nếu lỗi tin
	out[0] = '\0';

	// Kiểm tra loại tin
	const char *name = typeName(type);
	if (name == NULL)
		return 0;

	// Kiểm tra turn
	bool hasTurn = (turn >= 0);
	if (!hasTurn && name != "cancel")
		return 0;

	// Tạo tin
	int n;
	if (hasTurn)
		n = snprintf(out, outSize, "{\"type\":\"%s\",\"turn\":%ld}", name, (long)turn);
	else
		n = snprintf(out, outSize, "{\"type\":\"%s\"}", name);

	// Lọc tin bị cắt / lỗi
	if (n < 0 || (size_t)n >= outSize)
	{
		out[0] = '\0';
		return 0;
	}

	return (size_t)n;
}

// ========== PHÂN GIẢI TIN SERVER -> ESP32 ==========

//===== PHÂN GIẢI TYPE =====
static bool parseType(const char *s, ServerMsg &out)
{
	if (strcmp(s, "state") == 0)
		out = ServerMsg::State;
	else if (strcmp(s, "command") == 0)
		out = ServerMsg::Command;
	else if (strcmp(s, "error") == 0)
		out = ServerMsg::Error;
	else if (strcmp(s, "tts_start") == 0)
		out = ServerMsg::TtsStart;
	else if (strcmp(s, "tts_end") == 0)
		out = ServerMsg::TtsEnd;
	else if (strcmp(s, "music_title") == 0)
		out = ServerMsg::MusicTitle;
	else 
		return false;
	return true;
}

// ===== PHÂN GIẢI STATE =====
static bool parseState(const char *s, ServerState &out)
{
	if (strcmp(s, "transcribing") == 0)
		out = ServerState::Transcribing;
	else if (strcmp(s, "thinking") == 0)
		out = ServerState::Thingking;
	else
		return false;
	return true;
};

// ===== PHÂN GIẢI ERROR =====
static ErrorCode parseErrorCode(const char *s)
{
	if (strcmp(s, "BAD_MESSAGE") == 0)
		return ErrorCode::BadMessage;
	if (strcmp(s, "AUDIO_TOO_SHORT") == 0)
		return ErrorCode::AudioTooShort;
	if (strcmp(s, "STT_FAILED") == 0)
		return ErrorCode::SttFailed;
	if (strcmp(s, "STT_EMPTY") == 0)
		return ErrorCode::SttEmpty;
	if (strcmp(s, "LLM_FAILED") == 0)
		return ErrorCode::LlmFailed;
	if (strcmp(s, "TTS_FAILED") == 0)
		return ErrorCode::TtsFailed;
	if (strcmp(s, "MUSIC_NOT_FOUND") == 0)
		return ErrorCode::MusicNotFound;
	if (strcmp(s, "INTERNAL") == 0)
		return ErrorCode::Internal;
	return ErrorCode::Unknown;
};

//===== PHÂN GIẢI TOÀN BỘ TIN =====
bool parseServerMessage(const char *json, size_t len, ServerMessage &out)
{
	if (json == NULL || len == 0 || len > MAX_SERVER_MSG)
		return false;

	JsonDocument doc;
	if (deserializeJson(doc, json, len))
		return false;

	ServerMessage m;
	m.turn = NO_TURN;
	m.state = ServerState::None;
	m.code = ErrorCode::None;
	m.recoverable = true;

	// đọc type
	if (!parseType(doc["type"] | "", m.type))
		return false;

	// đọc turn
	if (!doc["turn"].isNull())
	{
		if(!doc["turn"].is<int32_t>())
			return false;
		m.turn = doc["turn"].as<int32_t>();
	}

	// đọc state
	if (m.type == ServerMsg::State)
	{
		if (!parseState(doc["value"] | "", m.state))
			return false;
	}

	//đọc error
	else if (m.type == ServerMsg::Error)
	{
		m.code = parseErrorCode(doc["code"] | "");
		m.recoverable = doc["recoverable"] | true;
	}

	out = m;
	return true;
}