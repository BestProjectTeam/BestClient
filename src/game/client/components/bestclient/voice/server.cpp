/* (c) BestClient */
#include "protocol.h"

#include <base/logger.h>
#include <base/math.h>
#include <base/system.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <string>
#include <vector>

namespace
{
constexpr int MAX_CLIENTS = 256;
constexpr int64_t CLIENT_TIMEOUT_SECONDS = 30;
constexpr int64_t PEER_LIST_SAFETY_BROADCAST_SECONDS = 5;

constexpr float PACKETS_PER_SECOND = 90.0f;
constexpr float PACKETS_BURST = 140.0f;
constexpr float BYTES_PER_SECOND = 75000.0f;
constexpr float BYTES_BURST = 110000.0f;

struct CClientSlot
{
	bool m_Used = false;
	NETADDR m_Addr = NETADDR_ZEROED;
	uint16_t m_ClientId = 0;
	std::string m_RoomKey;
	int m_GameClientId = BestClientVoice::INVALID_GAME_CLIENT_ID;
	int64_t m_LastSeen = 0;
	int64_t m_LastTokenRefill = 0;
	float m_PacketTokens = PACKETS_BURST;
	float m_ByteTokens = BYTES_BURST;
};

void RefillTokens(CClientSlot &Slot, int64_t Now)
{
	if(Slot.m_LastTokenRefill == 0)
		Slot.m_LastTokenRefill = Now;
	const double Delta = (double)(Now - Slot.m_LastTokenRefill) / time_freq();
	if(Delta <= 0.0)
		return;
	Slot.m_PacketTokens = minimum(PACKETS_BURST, Slot.m_PacketTokens + (float)(PACKETS_PER_SECOND * Delta));
	Slot.m_ByteTokens = minimum(BYTES_BURST, Slot.m_ByteTokens + (float)(BYTES_PER_SECOND * Delta));
	Slot.m_LastTokenRefill = Now;
}

bool ConsumeTokens(CClientSlot &Slot, int PacketSize, int64_t Now)
{
	RefillTokens(Slot, Now);
	if(Slot.m_PacketTokens < 1.0f || Slot.m_ByteTokens < PacketSize)
		return false;
	Slot.m_PacketTokens -= 1.0f;
	Slot.m_ByteTokens -= PacketSize;
	return true;
}

CClientSlot *FindClientSlot(std::array<CClientSlot, MAX_CLIENTS> &aSlots, const NETADDR &Addr)
{
	for(auto &Slot : aSlots)
	{
		if(Slot.m_Used && net_addr_comp(&Slot.m_Addr, &Addr) == 0)
			return &Slot;
	}
	return nullptr;
}

CClientSlot *FindClientSlotByIdentity(std::array<CClientSlot, MAX_CLIENTS> &aSlots, const std::string &RoomKey, int GameClientId)
{
	if(GameClientId == BestClientVoice::INVALID_GAME_CLIENT_ID)
		return nullptr;

	for(auto &Slot : aSlots)
	{
		if(!Slot.m_Used)
			continue;
		if(Slot.m_RoomKey == RoomKey && Slot.m_GameClientId == GameClientId)
			return &Slot;
	}
	return nullptr;
}

void ResetClientSlotTrafficState(CClientSlot &Slot, int64_t Now)
{
	Slot.m_LastSeen = Now;
	Slot.m_LastTokenRefill = Now;
	Slot.m_PacketTokens = PACKETS_BURST;
	Slot.m_ByteTokens = BYTES_BURST;
}

CClientSlot *AcquireClientSlot(std::array<CClientSlot, MAX_CLIENTS> &aSlots, const NETADDR &Addr, const std::string &RoomKey, int GameClientId, uint16_t &NextClientId, int64_t Now, bool &NewSlot)
{
	NewSlot = false;
	CClientSlot *pSlot = FindClientSlot(aSlots, Addr);
	if(pSlot)
	{
		ResetClientSlotTrafficState(*pSlot, Now);
		return pSlot;
	}

	pSlot = FindClientSlotByIdentity(aSlots, RoomKey, GameClientId);
	if(pSlot)
	{
		pSlot->m_Addr = Addr;
		ResetClientSlotTrafficState(*pSlot, Now);
		return pSlot;
	}

	for(auto &Slot : aSlots)
	{
		if(!Slot.m_Used)
		{
			Slot.m_Used = true;
			Slot.m_Addr = Addr;
			Slot.m_ClientId = NextClientId++;
			if(NextClientId == 0)
				NextClientId = 1;
			ResetClientSlotTrafficState(Slot, Now);
			NewSlot = true;
			return &Slot;
		}
	}
	return nullptr;
}

bool IsSameRoom(const CClientSlot &Left, const CClientSlot &Right)
{
	return Left.m_RoomKey == Right.m_RoomKey;
}

void SendHelloAck(NETSOCKET Socket, const NETADDR &Addr, uint16_t ClientId)
{
	std::vector<uint8_t> vPacket;
	vPacket.reserve(16);
	BestClientVoice::WriteHeader(vPacket, BestClientVoice::PACKET_HELLO_ACK);
	BestClientVoice::WriteU16(vPacket, ClientId);
	net_udp_send(Socket, &Addr, vPacket.data(), (int)vPacket.size());
}

void SendPeerListToClient(NETSOCKET Socket, const CClientSlot &Recipient, const std::array<CClientSlot, MAX_CLIENTS> &aSlots)
{
	uint16_t PeerCount = 0;
	for(const auto &Slot : aSlots)
	{
		if(Slot.m_Used && IsSameRoom(Slot, Recipient))
			++PeerCount;
	}

	std::vector<uint8_t> vPacket;
	vPacket.reserve(16 + PeerCount * 2);
	BestClientVoice::WriteHeader(vPacket, BestClientVoice::PACKET_PEER_LIST);
	BestClientVoice::WriteU16(vPacket, PeerCount);
	for(const auto &Slot : aSlots)
	{
		if(!Slot.m_Used || !IsSameRoom(Slot, Recipient))
			continue;
		BestClientVoice::WriteU16(vPacket, Slot.m_ClientId);
	}

	net_udp_send(Socket, &Recipient.m_Addr, vPacket.data(), (int)vPacket.size());
}

void SendPeerListExToClient(NETSOCKET Socket, const CClientSlot &Recipient, const std::array<CClientSlot, MAX_CLIENTS> &aSlots)
{
	uint16_t PeerCount = 0;
	for(const auto &Slot : aSlots)
	{
		if(Slot.m_Used && IsSameRoom(Slot, Recipient))
			++PeerCount;
	}

	std::vector<uint8_t> vPacket;
	vPacket.reserve(16 + PeerCount * 4);
	BestClientVoice::WriteHeader(vPacket, BestClientVoice::PACKET_PEER_LIST_EX);
	BestClientVoice::WriteU16(vPacket, PeerCount);
	for(const auto &Slot : aSlots)
	{
		if(!Slot.m_Used || !IsSameRoom(Slot, Recipient))
			continue;
		BestClientVoice::WriteU16(vPacket, Slot.m_ClientId);
		BestClientVoice::WriteS16(vPacket, Slot.m_GameClientId);
	}

	net_udp_send(Socket, &Recipient.m_Addr, vPacket.data(), (int)vPacket.size());
}

void BroadcastPeerLists(NETSOCKET Socket, const std::array<CClientSlot, MAX_CLIENTS> &aSlots)
{
	for(const auto &Slot : aSlots)
	{
		if(!Slot.m_Used)
			continue;
		SendPeerListToClient(Socket, Slot, aSlots);
		SendPeerListExToClient(Socket, Slot, aSlots);
	}
}

bool RemoveClientSlot(std::array<CClientSlot, MAX_CLIENTS> &aSlots, const NETADDR &Addr)
{
	for(auto &Slot : aSlots)
	{
		if(Slot.m_Used && net_addr_comp(&Slot.m_Addr, &Addr) == 0)
		{
			Slot = CClientSlot();
			return true;
		}
	}
	return false;
}

void BroadcastVoice(
	NETSOCKET Socket,
	const std::array<CClientSlot, MAX_CLIENTS> &aSlots,
	const CClientSlot &Sender,
	int16_t Team,
	int32_t PosX,
	int32_t PosY,
	uint16_t Sequence,
	const uint8_t *pVoiceData,
	uint16_t VoiceDataSize)
{
	std::vector<uint8_t> vPacket;
	vPacket.reserve(32 + VoiceDataSize);
	BestClientVoice::WriteHeader(vPacket, BestClientVoice::PACKET_VOICE_RELAY);
	BestClientVoice::WriteU16(vPacket, Sender.m_ClientId);
	BestClientVoice::WriteS16(vPacket, Team);
	BestClientVoice::WriteS32(vPacket, PosX);
	BestClientVoice::WriteS32(vPacket, PosY);
	BestClientVoice::WriteU16(vPacket, Sequence);
	BestClientVoice::WriteU16(vPacket, VoiceDataSize);
	vPacket.insert(vPacket.end(), pVoiceData, pVoiceData + VoiceDataSize);

	for(const auto &Slot : aSlots)
	{
		if(!Slot.m_Used || net_addr_comp(&Slot.m_Addr, &Sender.m_Addr) == 0 || !IsSameRoom(Slot, Sender))
			continue;
		net_udp_send(Socket, &Slot.m_Addr, vPacket.data(), (int)vPacket.size());
	}
}
}

int main(int argc, const char **argv)
{
	CCmdlineFix CmdlineFix(&argc, &argv);
	log_set_global_logger_default();

	net_init();

	NETADDR BindAddr = NETADDR_ZEROED;
	BindAddr.type = NETTYPE_ALL;
	BindAddr.port = BestClientVoice::DEFAULT_PORT;
	if(argc >= 2)
	{
		if(!BestClientVoice::ParseAddress(argv[1], BestClientVoice::DEFAULT_PORT, BindAddr))
		{
			log_error("voice-server", "invalid bind address '%s'", argv[1]);
			return -1;
		}
	}

	NETSOCKET Socket = net_udp_create(BindAddr);
	if(!Socket)
	{
		log_error("voice-server", "failed to create UDP socket");
		return -1;
	}
	net_set_non_blocking(Socket);

	char aBind[NETADDR_MAXSTRSIZE];
	net_addr_str(&BindAddr, aBind, sizeof(aBind), true);
	log_info("voice-server", "listening on %s", aBind);

	std::array<CClientSlot, MAX_CLIENTS> aSlots = {};
	uint16_t NextClientId = 1;
	int64_t LastPeerListBroadcast = 0;

	while(true)
	{
		using namespace std::chrono_literals;
		net_socket_read_wait(Socket, 100ms);

		while(true)
		{
			NETADDR From = NETADDR_ZEROED;
			unsigned char *pRawData = nullptr;
			const int DataSize = net_udp_recv(Socket, &From, &pRawData);
			if(DataSize <= 0 || !pRawData)
				break;
			if(DataSize > 1400)
				continue;

			int Offset = 0;
			BestClientVoice::EPacketType Type;
			if(!BestClientVoice::ReadHeader(pRawData, DataSize, Type, Offset))
				continue;
			if(Type != BestClientVoice::PACKET_HELLO && Type != BestClientVoice::PACKET_VOICE && Type != BestClientVoice::PACKET_PING && Type != BestClientVoice::PACKET_GOODBYE)
				continue;

			const int64_t Now = time_get();

			if(Type == BestClientVoice::PACKET_HELLO)
			{
				uint16_t ClientVersion = 0;
				if(!BestClientVoice::ReadU16(pRawData, DataSize, Offset, ClientVersion))
					continue;
				(void)ClientVersion;

				std::string RoomKey;
				int16_t GameClientId = BestClientVoice::INVALID_GAME_CLIENT_ID;
				if(Offset < DataSize)
				{
					uint16_t RoomKeySize = 0;
					if(!BestClientVoice::ReadU16(pRawData, DataSize, Offset, RoomKeySize))
						continue;
					if(RoomKeySize > BestClientVoice::MAX_ROOM_KEY_LENGTH || Offset + RoomKeySize > DataSize)
						continue;
					RoomKey.assign((const char *)(pRawData + Offset), (const char *)(pRawData + Offset + RoomKeySize));
					Offset += RoomKeySize;
					if(Offset < DataSize && !BestClientVoice::ReadS16(pRawData, DataSize, Offset, GameClientId))
						continue;
				}

				bool NewSlot = false;
				CClientSlot *pSlot = AcquireClientSlot(aSlots, From, RoomKey, GameClientId, NextClientId, Now, NewSlot);
				if(!pSlot)
					continue;
				const bool RoomChanged = pSlot->m_RoomKey != RoomKey;
				const bool GameClientChanged = pSlot->m_GameClientId != GameClientId;
				pSlot->m_RoomKey = RoomKey;
				pSlot->m_GameClientId = GameClientId;
				SendHelloAck(Socket, From, pSlot->m_ClientId);
				if(NewSlot || RoomChanged || GameClientChanged)
				{
					BroadcastPeerLists(Socket, aSlots);
					LastPeerListBroadcast = Now;
				}
				continue;
			}

			if(Type == BestClientVoice::PACKET_PING)
			{
				uint16_t Token = 0;
				if(!BestClientVoice::ReadU16(pRawData, DataSize, Offset, Token))
					continue;
				std::vector<uint8_t> vPacket;
				vPacket.reserve(16);
				BestClientVoice::WriteHeader(vPacket, BestClientVoice::PACKET_PONG);
				BestClientVoice::WriteU16(vPacket, Token);
				net_udp_send(Socket, &From, vPacket.data(), (int)vPacket.size());
				continue;
			}

			if(Type == BestClientVoice::PACKET_GOODBYE)
			{
				if(RemoveClientSlot(aSlots, From))
				{
					BroadcastPeerLists(Socket, aSlots);
					LastPeerListBroadcast = Now;
				}
				continue;
			}

			CClientSlot *pSender = FindClientSlot(aSlots, From);
			if(!pSender)
				continue;
			if(!ConsumeTokens(*pSender, DataSize, Now))
				continue;
			pSender->m_LastSeen = Now;

			int16_t Team = 0;
			int32_t PosX = 0;
			int32_t PosY = 0;
			uint16_t Sequence = 0;
			uint16_t VoiceDataSize = 0;
			if(!BestClientVoice::ReadS16(pRawData, DataSize, Offset, Team) ||
				!BestClientVoice::ReadS32(pRawData, DataSize, Offset, PosX) ||
				!BestClientVoice::ReadS32(pRawData, DataSize, Offset, PosY) ||
				!BestClientVoice::ReadU16(pRawData, DataSize, Offset, Sequence) ||
				!BestClientVoice::ReadU16(pRawData, DataSize, Offset, VoiceDataSize))
			{
				continue;
			}
			if(VoiceDataSize == 0 || VoiceDataSize > BestClientVoice::MAX_OPUS_PACKET_SIZE)
				continue;
			if(Offset + VoiceDataSize > DataSize)
				continue;

			BroadcastVoice(Socket, aSlots, *pSender, Team, PosX, PosY, Sequence, pRawData + Offset, VoiceDataSize);
		}

		const int64_t Now = time_get();
		if(LastPeerListBroadcast == 0 || Now - LastPeerListBroadcast > PEER_LIST_SAFETY_BROADCAST_SECONDS * time_freq())
		{
			BroadcastPeerLists(Socket, aSlots);
			LastPeerListBroadcast = Now;
		}

		bool TimedOutClient = false;
		for(auto &Slot : aSlots)
		{
			if(!Slot.m_Used)
				continue;
			if(Now - Slot.m_LastSeen > CLIENT_TIMEOUT_SECONDS * time_freq())
			{
				Slot = CClientSlot();
				TimedOutClient = true;
			}
		}
		if(TimedOutClient)
		{
			BroadcastPeerLists(Socket, aSlots);
			LastPeerListBroadcast = Now;
		}
	}
}
