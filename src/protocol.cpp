#include "protocol.h"
#include <stdio.h>

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