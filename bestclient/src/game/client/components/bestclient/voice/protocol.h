/* (c) BestClient */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_VOICE_PROTOCOL_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_VOICE_PROTOCOL_H

#include <base/net.h>
#include <base/system.h>

#include <cstdint>
#include <vector>

namespace BestClientVoice
{
constexpr uint32_t PROTOCOL_MAGIC = 0x42564331u; // BVC1
constexpr uint8_t PROTOCOL_VERSION = 3;

constexpr int SAMPLE_RATE = 48000;
constexpr int CHANNELS = 1;
constexpr int FRAME_SIZE = 960; // 20ms @ 48kHz
constexpr int MAX_OPUS_PACKET_SIZE = 400;
constexpr int DEFAULT_PORT = 8777;
constexpr int MAX_ROOM_KEY_LENGTH = 128;
constexpr int INVALID_GAME_CLIENT_ID = -1;

enum EPacketType : uint8_t
{
	PACKET_HELLO = 1,
	PACKET_HELLO_ACK = 2,
	PACKET_VOICE = 3,
	PACKET_VOICE_RELAY = 4,
	PACKET_PEER_LIST = 5,
	PACKET_PING = 6,
	PACKET_PONG = 7,
	PACKET_PEER_LIST_EX = 8,
	PACKET_GOODBYE = 9,
};

inline void WriteU8(std::vector<uint8_t> &vOut, uint8_t Value)
{
	vOut.push_back(Value);
}

inline void WriteU16(std::vector<uint8_t> &vOut, uint16_t Value)
{
	vOut.push_back((uint8_t)((Value >> 8) & 0xff));
	vOut.push_back((uint8_t)(Value & 0xff));
}

inline void WriteS16(std::vector<uint8_t> &vOut, int16_t Value)
{
	WriteU16(vOut, (uint16_t)Value);
}

inline void WriteU32(std::vector<uint8_t> &vOut, uint32_t Value)
{
	vOut.push_back((uint8_t)((Value >> 24) & 0xff));
	vOut.push_back((uint8_t)((Value >> 16) & 0xff));
	vOut.push_back((uint8_t)((Value >> 8) & 0xff));
	vOut.push_back((uint8_t)(Value & 0xff));
}

inline void WriteS32(std::vector<uint8_t> &vOut, int32_t Value)
{
	WriteU32(vOut, (uint32_t)Value);
}

inline bool ReadU8(const uint8_t *pData, int DataSize, int &Offset, uint8_t &Out)
{
	if(Offset + 1 > DataSize)
		return false;
	Out = pData[Offset];
	Offset += 1;
	return true;
}

inline bool ReadU16(const uint8_t *pData, int DataSize, int &Offset, uint16_t &Out)
{
	if(Offset + 2 > DataSize)
		return false;
	Out = (uint16_t)(pData[Offset] << 8) | (uint16_t)pData[Offset + 1];
	Offset += 2;
	return true;
}

inline bool ReadS16(const uint8_t *pData, int DataSize, int &Offset, int16_t &Out)
{
	uint16_t Value;
	if(!ReadU16(pData, DataSize, Offset, Value))
		return false;
	Out = (int16_t)Value;
	return true;
}

inline bool ReadU32(const uint8_t *pData, int DataSize, int &Offset, uint32_t &Out)
{
	if(Offset + 4 > DataSize)
		return false;
	Out = ((uint32_t)pData[Offset] << 24) | ((uint32_t)pData[Offset + 1] << 16) | ((uint32_t)pData[Offset + 2] << 8) | (uint32_t)pData[Offset + 3];
	Offset += 4;
	return true;
}

inline bool ReadS32(const uint8_t *pData, int DataSize, int &Offset, int32_t &Out)
{
	uint32_t Value;
	if(!ReadU32(pData, DataSize, Offset, Value))
		return false;
	Out = (int32_t)Value;
	return true;
}

inline void WriteHeader(std::vector<uint8_t> &vOut, EPacketType Type)
{
	WriteU32(vOut, PROTOCOL_MAGIC);
	WriteU8(vOut, (uint8_t)Type);
	WriteU8(vOut, PROTOCOL_VERSION);
}

inline bool ReadHeader(const uint8_t *pData, int DataSize, EPacketType &Type, int &Offset)
{
	Offset = 0;
	uint32_t Magic = 0;
	uint8_t RawType = 0;
	uint8_t Version = 0;
	if(!ReadU32(pData, DataSize, Offset, Magic) || !ReadU8(pData, DataSize, Offset, RawType) || !ReadU8(pData, DataSize, Offset, Version))
		return false;
	if(Magic != PROTOCOL_MAGIC || Version != PROTOCOL_VERSION)
		return false;
	Type = (EPacketType)RawType;
	return true;
}

inline bool ParseAddress(const char *pAddress, int DefaultPort, NETADDR &Out)
{
	if(net_addr_from_str(&Out, pAddress) == 0)
	{
		if(Out.port == 0)
			Out.port = DefaultPort;
		return true;
	}
	if(net_host_lookup(pAddress, &Out, NETTYPE_ALL) == 0)
	{
		if(Out.port == 0)
			Out.port = DefaultPort;
		return true;
	}
	return false;
}
}

#endif
