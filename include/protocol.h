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

#endif // PROTOCOL_H
