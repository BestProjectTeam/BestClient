#ifndef ENGINE_SHARED_BESTCLIENT_INDICATOR_PROTOCOL_H
#define ENGINE_SHARED_BESTCLIENT_INDICATOR_PROTOCOL_H

#include <base/hash.h>
#include <base/system.h>

#include <engine/shared/uuid_manager.h>

#include <cstdint>
#include <string>
#include <vector>

namespace BestClientIndicator
{
constexpr uint32_t PROTOCOL_MAGIC = 0x42434931u; // BCI1
constexpr uint8_t PROTOCOL_VERSION = 1;
constexpr int DEFAULT_PORT = 8778;
constexpr int MAX_RECEIVE_PACKETS_PER_TICK = 32;
constexpr int CLIENT_PACKET_PROOF_SIZE = SHA256_DIGEST_LENGTH;

enum EPacketType : uint8_t
{
	PACKET_JOIN = 1,
	PACKET_HEARTBEAT = 2,
	PACKET_LEAVE = 3,
	PACKET_PEER_STATE = 4,
	PACKET_PEER_REMOVE = 5,
	PACKET_PEER_LIST = 6,
};

struct CClientPresencePacket
{
	EPacketType m_Type = PACKET_JOIN;
	CUuid m_ClientInstanceId = UUID_ZEROED;
	CUuid m_Nonce = UUID_ZEROED;
	uint64_t m_Timestamp = 0;
	std::string m_ServerAddress;
	std::string m_PlayerName;
	int m_ClientId = -1;
};

struct CPeerState
{
	std::string m_ServerAddress;
	std::string m_PlayerName;
	int m_ClientId = -1;
};

struct CPeerList
{
	std::string m_ServerAddress;
	std::vector<int> m_vClientIds;
};

void WriteU8(std::vector<uint8_t> &vOut, uint8_t Value);
void WriteU16(std::vector<uint8_t> &vOut, uint16_t Value);
void WriteS16(std::vector<uint8_t> &vOut, int16_t Value);
void WriteU64(std::vector<uint8_t> &vOut, uint64_t Value);
void WriteString(std::vector<uint8_t> &vOut, const char *pValue);
void WriteRaw(std::vector<uint8_t> &vOut, const void *pData, int DataSize);
void WriteUuid(std::vector<uint8_t> &vOut, CUuid Uuid);
void WriteHeader(std::vector<uint8_t> &vOut, EPacketType Type);

bool ReadU8(const uint8_t *pData, int DataSize, int &Offset, uint8_t &Out);
bool ReadU16(const uint8_t *pData, int DataSize, int &Offset, uint16_t &Out);
bool ReadS16(const uint8_t *pData, int DataSize, int &Offset, int16_t &Out);
bool ReadU64(const uint8_t *pData, int DataSize, int &Offset, uint64_t &Out);
bool ReadString(const uint8_t *pData, int DataSize, int &Offset, std::string &Out);
bool ReadUuid(const uint8_t *pData, int DataSize, int &Offset, CUuid &Out);
bool ReadHeader(const uint8_t *pData, int DataSize, EPacketType &Type, int &Offset);

SHA256_DIGEST ComputeProof(const char *pSharedToken, const uint8_t *pPacketData, int PacketDataSize);
void AppendProof(std::vector<uint8_t> &vPacket, const char *pSharedToken);
bool ValidateProof(const char *pSharedToken, const uint8_t *pPacketData, int PacketDataSize);

bool ParseAddress(const char *pAddress, int DefaultPort, NETADDR &Out);

bool ReadClientPresencePacket(const uint8_t *pData, int DataSize, CClientPresencePacket &Out);

bool ReadPeerStatePacket(const uint8_t *pData, int DataSize, CPeerState &Out);
bool ReadPeerRemovePacket(const uint8_t *pData, int DataSize, CPeerState &Out);
bool ReadPeerListPacket(const uint8_t *pData, int DataSize, CPeerList &Out);

void WritePeerStatePacket(std::vector<uint8_t> &vOut, EPacketType Type, const char *pServerAddress, const char *pPlayerName, int ClientId);
void WritePeerListPacket(std::vector<uint8_t> &vOut, const char *pServerAddress, const std::vector<int> &vClientIds);
}

#endif
