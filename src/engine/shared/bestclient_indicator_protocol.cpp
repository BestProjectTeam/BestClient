#include "bestclient_indicator_protocol.h"

#include <base/hash_ctxt.h>
#include <base/net.h>

namespace BestClientIndicator
{
void WriteU8(std::vector<uint8_t> &vOut, uint8_t Value)
{
	vOut.push_back(Value);
}

void WriteU16(std::vector<uint8_t> &vOut, uint16_t Value)
{
	vOut.push_back((uint8_t)((Value >> 8) & 0xff));
	vOut.push_back((uint8_t)(Value & 0xff));
}

void WriteS16(std::vector<uint8_t> &vOut, int16_t Value)
{
	WriteU16(vOut, (uint16_t)Value);
}

void WriteU64(std::vector<uint8_t> &vOut, uint64_t Value)
{
	for(int Shift = 56; Shift >= 0; Shift -= 8)
		vOut.push_back((uint8_t)((Value >> Shift) & 0xff));
}

void WriteString(std::vector<uint8_t> &vOut, const char *pValue)
{
	const int Length = pValue ? str_length(pValue) : 0;
	WriteU16(vOut, (uint16_t)Length);
	if(Length > 0)
		WriteRaw(vOut, pValue, Length);
}

void WriteRaw(std::vector<uint8_t> &vOut, const void *pData, int DataSize)
{
	if(DataSize <= 0 || pData == nullptr)
		return;
	const auto *pBytes = static_cast<const uint8_t *>(pData);
	vOut.insert(vOut.end(), pBytes, pBytes + DataSize);
}

void WriteUuid(std::vector<uint8_t> &vOut, CUuid Uuid)
{
	WriteRaw(vOut, &Uuid, sizeof(Uuid));
}

void WriteHeader(std::vector<uint8_t> &vOut, EPacketType Type)
{
	vOut.push_back((uint8_t)((PROTOCOL_MAGIC >> 24) & 0xff));
	vOut.push_back((uint8_t)((PROTOCOL_MAGIC >> 16) & 0xff));
	vOut.push_back((uint8_t)((PROTOCOL_MAGIC >> 8) & 0xff));
	vOut.push_back((uint8_t)(PROTOCOL_MAGIC & 0xff));
	WriteU8(vOut, (uint8_t)Type);
	WriteU8(vOut, PROTOCOL_VERSION);
}

bool ReadU8(const uint8_t *pData, int DataSize, int &Offset, uint8_t &Out)
{
	if(Offset + 1 > DataSize)
		return false;
	Out = pData[Offset++];
	return true;
}

bool ReadU16(const uint8_t *pData, int DataSize, int &Offset, uint16_t &Out)
{
	if(Offset + 2 > DataSize)
		return false;
	Out = ((uint16_t)pData[Offset] << 8) | (uint16_t)pData[Offset + 1];
	Offset += 2;
	return true;
}

bool ReadS16(const uint8_t *pData, int DataSize, int &Offset, int16_t &Out)
{
	uint16_t Value = 0;
	if(!ReadU16(pData, DataSize, Offset, Value))
		return false;
	Out = (int16_t)Value;
	return true;
}

bool ReadU64(const uint8_t *pData, int DataSize, int &Offset, uint64_t &Out)
{
	if(Offset + 8 > DataSize)
		return false;
	Out = 0;
	for(int i = 0; i < 8; ++i)
	{
		Out = (Out << 8) | (uint64_t)pData[Offset + i];
	}
	Offset += 8;
	return true;
}

bool ReadString(const uint8_t *pData, int DataSize, int &Offset, std::string &Out)
{
	uint16_t Length = 0;
	if(!ReadU16(pData, DataSize, Offset, Length) || Offset + Length > DataSize)
		return false;
	Out.assign(reinterpret_cast<const char *>(pData + Offset), Length);
	Offset += Length;
	return true;
}

bool ReadUuid(const uint8_t *pData, int DataSize, int &Offset, CUuid &Out)
{
	if(Offset + (int)sizeof(Out) > DataSize)
		return false;
	mem_copy(&Out, pData + Offset, sizeof(Out));
	Offset += sizeof(Out);
	return true;
}

bool ReadHeader(const uint8_t *pData, int DataSize, EPacketType &Type, int &Offset)
{
	Offset = 0;
	if(DataSize < 6)
		return false;
	const uint32_t Magic = ((uint32_t)pData[0] << 24) | ((uint32_t)pData[1] << 16) | ((uint32_t)pData[2] << 8) | (uint32_t)pData[3];
	if(Magic != PROTOCOL_MAGIC || pData[5] != PROTOCOL_VERSION)
		return false;
	Type = (EPacketType)pData[4];
	Offset = 6;
	return true;
}

SHA256_DIGEST ComputeProof(const char *pSharedToken, const uint8_t *pPacketData, int PacketDataSize)
{
	SHA256_CTX Sha256;
	sha256_init(&Sha256);
	if(pSharedToken && pSharedToken[0] != '\0')
		sha256_update(&Sha256, pSharedToken, str_length(pSharedToken));
	if(pPacketData && PacketDataSize > 0)
		sha256_update(&Sha256, pPacketData, PacketDataSize);
	return sha256_finish(&Sha256);
}

void AppendProof(std::vector<uint8_t> &vPacket, const char *pSharedToken)
{
	const SHA256_DIGEST Proof = ComputeProof(pSharedToken, vPacket.data(), (int)vPacket.size());
	WriteRaw(vPacket, Proof.data, sizeof(Proof.data));
}

bool ValidateProof(const char *pSharedToken, const uint8_t *pPacketData, int PacketDataSize)
{
	if(!pPacketData || PacketDataSize < CLIENT_PACKET_PROOF_SIZE)
		return false;

	const int PayloadSize = PacketDataSize - CLIENT_PACKET_PROOF_SIZE;
	const SHA256_DIGEST Expected = ComputeProof(pSharedToken, pPacketData, PayloadSize);
	SHA256_DIGEST Actual{};
	mem_copy(Actual.data, pPacketData + PayloadSize, sizeof(Actual.data));
	return Actual == Expected;
}

bool ParseAddress(const char *pAddress, int DefaultPort, NETADDR &Out)
{
	if(!pAddress || pAddress[0] == '\0')
		return false;
	if(net_addr_from_url(&Out, pAddress, nullptr, 0) == 0 || net_addr_from_str(&Out, pAddress) == 0)
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

static bool ReadPeerPacketCommon(const uint8_t *pData, int DataSize, CPeerState &Out, EPacketType WantedType)
{
	int Offset = 0;
	EPacketType Type;
	if(!ReadHeader(pData, DataSize, Type, Offset) || Type != WantedType)
		return false;
	int16_t ClientId = -1;
	if(!ReadString(pData, DataSize, Offset, Out.m_ServerAddress) ||
		!ReadString(pData, DataSize, Offset, Out.m_PlayerName) ||
		!ReadS16(pData, DataSize, Offset, ClientId))
	{
		return false;
	}
	Out.m_ClientId = ClientId;
	return Offset == DataSize;
}

bool ReadClientPresencePacket(const uint8_t *pData, int DataSize, CClientPresencePacket &Out)
{
	int Offset = 0;
	EPacketType Type;
	if(!ReadHeader(pData, DataSize, Type, Offset) ||
		(Type != PACKET_JOIN && Type != PACKET_HEARTBEAT && Type != PACKET_LEAVE))
	{
		return false;
	}

	int16_t ClientId = -1;
	if(!ReadUuid(pData, DataSize, Offset, Out.m_ClientInstanceId) ||
		!ReadUuid(pData, DataSize, Offset, Out.m_Nonce) ||
		!ReadU64(pData, DataSize, Offset, Out.m_Timestamp) ||
		!ReadString(pData, DataSize, Offset, Out.m_ServerAddress) ||
		!ReadString(pData, DataSize, Offset, Out.m_PlayerName) ||
		!ReadS16(pData, DataSize, Offset, ClientId))
	{
		return false;
	}

	Out.m_Type = Type;
	Out.m_ClientId = ClientId;
	return Offset + CLIENT_PACKET_PROOF_SIZE == DataSize;
}

bool ReadPeerStatePacket(const uint8_t *pData, int DataSize, CPeerState &Out)
{
	return ReadPeerPacketCommon(pData, DataSize, Out, PACKET_PEER_STATE);
}

bool ReadPeerRemovePacket(const uint8_t *pData, int DataSize, CPeerState &Out)
{
	return ReadPeerPacketCommon(pData, DataSize, Out, PACKET_PEER_REMOVE);
}

bool ReadPeerListPacket(const uint8_t *pData, int DataSize, CPeerList &Out)
{
	int Offset = 0;
	EPacketType Type;
	if(!ReadHeader(pData, DataSize, Type, Offset) || Type != PACKET_PEER_LIST)
		return false;

	uint16_t Count = 0;
	if(!ReadString(pData, DataSize, Offset, Out.m_ServerAddress) || !ReadU16(pData, DataSize, Offset, Count))
		return false;

	Out.m_vClientIds.clear();
	Out.m_vClientIds.reserve(Count);
	for(uint16_t i = 0; i < Count; ++i)
	{
		int16_t ClientId = -1;
		if(!ReadS16(pData, DataSize, Offset, ClientId))
			return false;
		Out.m_vClientIds.push_back(ClientId);
	}
	return Offset == DataSize;
}

void WritePeerStatePacket(std::vector<uint8_t> &vOut, EPacketType Type, const char *pServerAddress, const char *pPlayerName, int ClientId)
{
	vOut.clear();
	WriteHeader(vOut, Type);
	WriteString(vOut, pServerAddress);
	WriteString(vOut, pPlayerName);
	WriteS16(vOut, ClientId);
}

void WritePeerListPacket(std::vector<uint8_t> &vOut, const char *pServerAddress, const std::vector<int> &vClientIds)
{
	vOut.clear();
	WriteHeader(vOut, PACKET_PEER_LIST);
	WriteString(vOut, pServerAddress);
	WriteU16(vOut, (uint16_t)vClientIds.size());
	for(const int ClientId : vClientIds)
		WriteS16(vOut, ClientId);
}
}
