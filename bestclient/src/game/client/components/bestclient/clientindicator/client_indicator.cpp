#include "client_indicator.h"

#include "protocol.h"

#include <base/logger.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/shared/network.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <cstdarg>

namespace
{
constexpr const char *LOG_SCOPE = "clientindicator-cl";
constexpr const char *OLD_BC_BROWSER_URL = "http://150.241.70.188:8779/users.json";
constexpr const char *OLD_BC_TOKEN_URL = "http://150.241.70.188:8779/token.json";
constexpr const char *NEW_BC_BROWSER_URL = "https://150.241.70.188:8779/users.json";
constexpr const char *NEW_BC_TOKEN_URL = "https://150.241.70.188:8779/token.json";
constexpr int PACKET_DUMP_BYTES_PER_LINE = 64;

int64_t SlowPacketProcessTicks()
{
	return time_freq() / 500; // ~2ms
}

void EnsureBestClientHttpsDefaults()
{
	if(g_Config.m_BcClientIndicatorBrowserUrl[0] == '\0' || str_comp(g_Config.m_BcClientIndicatorBrowserUrl, OLD_BC_BROWSER_URL) == 0)
		str_copy(g_Config.m_BcClientIndicatorBrowserUrl, NEW_BC_BROWSER_URL, sizeof(g_Config.m_BcClientIndicatorBrowserUrl));
	if(g_Config.m_BcClientIndicatorTokenUrl[0] == '\0' || str_comp(g_Config.m_BcClientIndicatorTokenUrl, OLD_BC_TOKEN_URL) == 0)
		str_copy(g_Config.m_BcClientIndicatorTokenUrl, NEW_BC_TOKEN_URL, sizeof(g_Config.m_BcClientIndicatorTokenUrl));
}

bool IsBlockedIndicatorAddress(const NETADDR &Addr)
{
	return net_addr_is_local(&Addr);
}

const char *PacketTypeName(int PacketType)
{
	switch(PacketType)
	{
	case BestClientIndicator::PACKET_JOIN:
		return "join";
	case BestClientIndicator::PACKET_HEARTBEAT:
		return "heartbeat";
	case BestClientIndicator::PACKET_LEAVE:
		return "leave";
	case BestClientIndicator::PACKET_PEER_STATE:
		return "peer_state";
	case BestClientIndicator::PACKET_PEER_REMOVE:
		return "peer_remove";
	case BestClientIndicator::PACKET_PEER_LIST:
		return "peer_list";
	default:
		return "unknown";
	}
}

void DumpUdpPacketBytes(const char *pDirection, const NETADDR &Addr, const void *pData, int DataSize)
{
	if(!pDirection || !pData || DataSize <= 0)
		return;

	char aAddr[NETADDR_MAXSTRSIZE];
	net_addr_str(&Addr, aAddr, sizeof(aAddr), true);
	log_info(LOG_SCOPE, "%s udp packet bytes=%d addr=%s", pDirection, DataSize, aAddr);

	const auto *pBytes = static_cast<const uint8_t *>(pData);
	for(int Offset = 0; Offset < DataSize; Offset += PACKET_DUMP_BYTES_PER_LINE)
	{
		const int ChunkSize = minimum(PACKET_DUMP_BYTES_PER_LINE, DataSize - Offset);
		char aHex[PACKET_DUMP_BYTES_PER_LINE * 2 + 1];
		str_hex(aHex, sizeof(aHex), pBytes + Offset, ChunkSize);
		log_info(LOG_SCOPE, "%s udp dump offset=%d size=%d hex=%s", pDirection, Offset, ChunkSize, aHex);
	}
}
}

CClientIndicator::CClientIndicator()
{
	OnReset();
}

void CClientIndicator::OnInit()
{
	EnsureBestClientHttpsDefaults();
	if(m_ClientInstanceId == UUID_ZEROED)
		m_ClientInstanceId = RandomUuid();
	DebugLogF("init server=%s token_url=%s browser_url=%s", g_Config.m_BcClientIndicatorServerAddress, g_Config.m_BcClientIndicatorTokenUrl, g_Config.m_BcClientIndicatorBrowserUrl);
}

void CClientIndicator::OnReset()
{
	ResetPresenceState();
	ResetTokenState();
}

void CClientIndicator::OnStateChange(int NewState, int OldState)
{
	(void)OldState;
	if(NewState == IClient::STATE_OFFLINE)
	{
		StopPresence(true);
		ResetTokenState();
	}
	else if(NewState == IClient::STATE_ONLINE)
	{
		DebugLog("state -> online, refreshing token/browser cache");
		if(g_Config.m_BcClientIndicator)
		{
			RefreshBrowserCache(false);
			RefreshToken(false);
		}
	}
}

void CClientIndicator::OnShutdown()
{
	StopPresence(true);
	ResetBrowserTask();
	ResetTokenTask();
}

bool CClientIndicator::IsPlayerBestClient(int ClientId) const
{
	if(Client()->State() != IClient::STATE_ONLINE || !g_Config.m_BcClientIndicator)
		return false;
	for(const int LocalId : GameClient()->m_aLocalIds)
	{
		if(LocalId >= 0 && ClientId == LocalId)
			return true;
	}
	return m_PresenceCache.IsPresent(ClientId);
}

void CClientIndicator::RefreshBrowserCache(bool Force)
{
	if(g_Config.m_BcClientIndicator == 0)
		return;
	EnsureBestClientHttpsDefaults();
	if(g_Config.m_BcClientIndicatorBrowserUrl[0] == '\0')
	{
		DebugLog("browser refresh skipped: browser url is empty");
		return;
	}
	if(m_pBrowserTask && !m_pBrowserTask->Done())
	{
		if(!Force)
		{
			DebugLog("browser refresh skipped: request already running");
			return;
		}
		DebugLog("browser refresh forcing reset of running request");
		ResetBrowserTask();
	}

	m_pBrowserTask = HttpGet(g_Config.m_BcClientIndicatorBrowserUrl);
	m_pBrowserTask->Timeout(CTimeout{10000, 0, 500, 5});
	m_pBrowserTask->IpResolve(IPRESOLVE::V4);
	// The indicator web endpoint is deployed with a self-signed certificate by default.
	m_pBrowserTask->VerifyPeer(false);
	m_pBrowserTask->LogProgress(HTTPLOG::FAILURE);
	DebugLogF("starting browser request url=%s", g_Config.m_BcClientIndicatorBrowserUrl);
	m_LastBrowserRefreshTick = time_get();
	Http()->Run(m_pBrowserTask);
}

void CClientIndicator::RefreshToken(bool Force)
{
	if(g_Config.m_BcClientIndicator == 0)
		return;
	EnsureBestClientHttpsDefaults();
	if(g_Config.m_BcClientIndicatorTokenUrl[0] == '\0')
	{
		DebugLog("token refresh skipped: token url is empty");
		return;
	}
	if(m_pTokenTask && !m_pTokenTask->Done())
	{
		if(!Force)
		{
			DebugLog("token refresh skipped: request already running");
			return;
		}
		DebugLog("token refresh forcing reset of running request");
		ResetTokenTask();
	}

	m_pTokenTask = HttpGet(g_Config.m_BcClientIndicatorTokenUrl);
	m_pTokenTask->Timeout(CTimeout{10000, 0, 500, 5});
	m_pTokenTask->IpResolve(IPRESOLVE::V4);
	// Keep token bootstrap aligned with the self-signed indicator deployment.
	m_pTokenTask->VerifyPeer(false);
	m_pTokenTask->LogProgress(HTTPLOG::FAILURE);
	DebugLogF("starting token request url=%s", g_Config.m_BcClientIndicatorTokenUrl);
	m_LastTokenRefreshTick = time_get();
	Http()->Run(m_pTokenTask);
}

void CClientIndicator::OnUpdate()
{
	const int64_t PerfStart = time_get();
	if(!IsBrowserSnapshotEnabled())
	{
		m_RuntimeState = ESubsystemRuntimeState::DISABLED;
		if(m_WasPresenceEnabled || m_Socket || HasPendingNetworkTask() || m_aWebSharedToken[0] != '\0')
		{
			StopPresence(true);
			ResetBrowserTask();
			ResetTokenState();
		}
		ClearBrowserSnapshot();
		SetPresenceBlockReason("presence update skipped: indicator disabled");
		m_WasPresenceEnabled = false;
		return;
	}

	const bool PresenceEnabled = IsPresenceEnabled();
	const int64_t Now = time_get();
	const int64_t BrowserRefreshInterval = 30 * time_freq();
	const int64_t TokenRefreshInterval = 60 * time_freq();
	if(PresenceEnabled && !m_WasPresenceEnabled)
	{
		m_RuntimeState = ESubsystemRuntimeState::ARMED;
		RefreshBrowserCache(false);
		RefreshToken(false);
	}
	else if(PresenceEnabled)
	{
		m_RuntimeState = m_Socket ? ESubsystemRuntimeState::ACTIVE : ESubsystemRuntimeState::ARMED;
		if(!m_pBrowserTask && (m_LastBrowserRefreshTick == 0 || Now - m_LastBrowserRefreshTick >= BrowserRefreshInterval))
			RefreshBrowserCache(false);
		if(!m_pTokenTask && (m_LastTokenRefreshTick == 0 || Now - m_LastTokenRefreshTick >= TokenRefreshInterval))
			RefreshToken(false);
	}
	else if(!PresenceEnabled)
	{
		m_RuntimeState = ESubsystemRuntimeState::COOLDOWN;
		if(m_WasPresenceEnabled || m_Socket || m_HasServerAddr || m_pTokenTask || m_aWebSharedToken[0] != '\0')
		{
			StopPresence(true);
			ResetTokenState();
		}
		SetPresenceBlockReason("presence update skipped: client offline");
		m_WasPresenceEnabled = false;
	}

	if(m_pBrowserTask && m_pBrowserTask->State() == EHttpState::DONE)
	{
		DebugLogF("browser request done http_status=%d", m_pBrowserTask->StatusCode());
		FinishBrowserCacheRefresh();
		ResetBrowserTask();
	}
	else if(m_pBrowserTask && (m_pBrowserTask->State() == EHttpState::ERROR || m_pBrowserTask->State() == EHttpState::ABORTED))
	{
		DebugLogF("browser request ended with state=%d", (int)m_pBrowserTask->State());
		ResetBrowserTask();
	}

	if(m_pTokenTask && m_pTokenTask->State() == EHttpState::DONE)
	{
		DebugLogF("token request done http_status=%d", m_pTokenTask->StatusCode());
		FinishTokenRefresh();
		ResetTokenTask();
	}
	else if(m_pTokenTask && m_pTokenTask->State() == EHttpState::ERROR)
	{
		DebugLog("token request ended with error");
		char aOldEffectiveToken[sizeof(m_aWebSharedToken)];
		str_copy(aOldEffectiveToken, EffectiveSharedToken(), sizeof(aOldEffectiveToken));
		m_aWebSharedToken[0] = '\0';
		if(str_comp(aOldEffectiveToken, EffectiveSharedToken()) != 0)
			StopPresence(true);
		ResetTokenTask();
	}
	else if(m_pTokenTask && m_pTokenTask->State() == EHttpState::ABORTED)
	{
		DebugLog("token request aborted");
		ResetTokenTask();
	}

	if(PresenceEnabled)
		UpdatePresence();

	m_LastUpdateCostTick = time_get() - PerfStart;
	m_MaxUpdateCostTick = maximum(m_MaxUpdateCostTick, m_LastUpdateCostTick);
	m_TotalUpdateCostTick += m_LastUpdateCostTick;
	++m_UpdateSamples;
	if(g_Config.m_DbgClientIndicator >= 1)
	{
		if(m_LastPerfReportTick == 0 || Now - m_LastPerfReportTick >= time_freq())
		{
			DebugLogF("perf last=%.3fms avg=%.3fms max=%.3fms samples=%lld socket=%d",
				m_LastUpdateCostTick * 1000.0 / (double)time_freq(),
				m_UpdateSamples > 0 ? (m_TotalUpdateCostTick * 1000.0 / (double)time_freq()) / (double)m_UpdateSamples : 0.0,
				m_MaxUpdateCostTick * 1000.0 / (double)time_freq(),
				(long long)m_UpdateSamples,
				m_Socket != nullptr ? 1 : 0);
			m_LastPerfReportTick = Now;
			m_TotalUpdateCostTick = 0;
			m_UpdateSamples = 0;
			m_MaxUpdateCostTick = 0;
		}
	}
}

void CClientIndicator::OpenPresenceSocket()
{
	if(m_Socket)
	{
		DebugLog("presence socket already open");
		return;
	}
	if(g_Config.m_BcClientIndicatorServerAddress[0] == '\0')
	{
		SetPresenceBlockReason("presence socket open skipped: server address is empty");
		return;
	}
	if(!BestClientIndicator::ParseAddress(g_Config.m_BcClientIndicatorServerAddress, BestClientIndicator::DEFAULT_PORT, m_ServerAddr))
	{
		char aBuf[512];
		str_format(aBuf, sizeof(aBuf), "presence socket open failed: cannot parse server address '%s'", g_Config.m_BcClientIndicatorServerAddress);
		SetPresenceBlockReason(aBuf);
		return;
	}
	if(IsBlockedIndicatorAddress(m_ServerAddr))
	{
		char aServerAddr[NETADDR_MAXSTRSIZE];
		net_addr_str(&m_ServerAddr, aServerAddr, sizeof(aServerAddr), true);
		char aBuf[512];
		str_format(aBuf, sizeof(aBuf), "presence socket open blocked: target address %s is local", aServerAddr);
		SetPresenceBlockReason(aBuf);
		return;
	}

	NETADDR Bind = NETADDR_ZEROED;
	Bind.type = NETTYPE_ALL;
	Bind.port = 0;
	m_Socket = net_udp_create(Bind);
	if(!m_Socket)
	{
		SetPresenceBlockReason("presence socket open failed: net_udp_create returned null");
		return;
	}
	net_set_non_blocking(m_Socket);
	m_HasServerAddr = true;
	str_copy(m_aLastPresenceServerAddr, g_Config.m_BcClientIndicatorServerAddress, sizeof(m_aLastPresenceServerAddr));
	char aServerAddr[NETADDR_MAXSTRSIZE];
	net_addr_str(&m_ServerAddr, aServerAddr, sizeof(aServerAddr), true);
	ClearPresenceBlockReason();
	DebugLogF("presence socket opened, udp target=%s", aServerAddr);
}

void CClientIndicator::ClosePresenceSocket()
{
	if(m_Socket)
	{
		DebugLog("closing presence socket");
		net_udp_close(m_Socket);
		m_Socket = nullptr;
	}
	m_HasServerAddr = false;
	m_aLastPresenceServerAddr[0] = '\0';
}

void CClientIndicator::StopPresence(bool SendLeavePackets)
{
	const bool HadPresenceState = m_Socket || !m_RegisteredClientIds.empty() || m_aLastPresenceServerAddr[0] != '\0';
	if(HadPresenceState && SendLeavePackets)
	{
		DebugLog("stopping presence and sending leave packets");
		SendLeaveForAll();
	}
	else if(HadPresenceState)
	{
		DebugLog("stopping presence without leave packets");
	}
	ClosePresenceSocket();
	ResetPresenceState();
}

void CClientIndicator::EnsurePresenceSocket()
{
	if(!g_Config.m_BcClientIndicator || Client()->State() != IClient::STATE_ONLINE)
	{
		SetPresenceBlockReason("presence socket skipped: indicator disabled or client offline");
		return;
	}

	const bool HadPresenceServer = m_aLastPresenceServerAddr[0] != '\0' || m_Socket || m_HasServerAddr;
	const bool ServerChanged = HadPresenceServer && str_comp(m_aLastPresenceServerAddr, g_Config.m_BcClientIndicatorServerAddress) != 0;
	if(ServerChanged)
	{
		DebugLogF("presence server address changed, resetting state old=%s new=%s", m_aLastPresenceServerAddr, g_Config.m_BcClientIndicatorServerAddress);
		StopPresence(true);
	}

	if(m_Socket || g_Config.m_BcClientIndicatorServerAddress[0] == '\0')
	{
		if(!m_Socket && g_Config.m_BcClientIndicatorServerAddress[0] == '\0')
			SetPresenceBlockReason("presence socket skipped: indicator server address is empty");
		return;
	}

	const int64_t Now = time_get();
	if(m_LastPresenceStartAttempt != 0 && Now - m_LastPresenceStartAttempt < time_freq())
	{
		if(m_LastPresenceBlockReason.empty())
			SetPresenceBlockReason("presence socket skipped: retry throttled");
		return;
	}
	m_LastPresenceStartAttempt = Now;
	OpenPresenceSocket();
}

const char *CClientIndicator::CurrentGameServerAddress()
{
	if(Client()->State() != IClient::STATE_ONLINE)
	{
		DebugLog("current game server address unavailable: client offline");
		return "";
	}
	if(IsBlockedIndicatorAddress(Client()->ServerAddress()))
	{
		char aAddr[NETADDR_MAXSTRSIZE];
		net_addr_str(&Client()->ServerAddress(), aAddr, sizeof(aAddr), true);
		DebugLogF("current game server address blocked: %s is local", aAddr);
		return "";
	}
	char aAddr[NETADDR_MAXSTRSIZE];
	net_addr_str(&Client()->ServerAddress(), aAddr, sizeof(aAddr), true);
	str_copy(m_aLastGameServerAddr, aAddr, sizeof(m_aLastGameServerAddr));
	return m_aLastGameServerAddr;
}

const char *CClientIndicator::PlayerNameForClient(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return "";
	if(GameClient()->m_aClients[ClientId].m_Active && GameClient()->m_aClients[ClientId].m_aName[0] != '\0')
		return GameClient()->m_aClients[ClientId].m_aName;
	if(ClientId == GameClient()->m_aLocalIds[IClient::CONN_MAIN])
		return Client()->PlayerName();
	if(ClientId == GameClient()->m_aLocalIds[IClient::CONN_DUMMY])
		return Client()->DummyName();
	return "";
}

void CClientIndicator::SendPresencePacket(int ClientId, int PacketType)
{
	if(!m_Socket || !m_HasServerAddr || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	const char *pSharedToken = EffectiveSharedToken();
	if(!pSharedToken || pSharedToken[0] == '\0')
		return;

	std::vector<uint8_t> vPacket;
	vPacket.reserve(256);
	BestClientIndicator::WriteHeader(vPacket, (BestClientIndicator::EPacketType)PacketType);
	BestClientIndicator::WriteUuid(vPacket, m_ClientInstanceId);
	const CUuid Nonce = RandomUuid();
	BestClientIndicator::WriteUuid(vPacket, Nonce);
	BestClientIndicator::WriteU64(vPacket, (uint64_t)time_timestamp());
	BestClientIndicator::WriteString(vPacket, CurrentGameServerAddress());
	BestClientIndicator::WriteString(vPacket, PlayerNameForClient(ClientId));
	BestClientIndicator::WriteS16(vPacket, (int16_t)ClientId);
	BestClientIndicator::AppendProof(vPacket, pSharedToken);

	if(g_Config.m_DbgClientIndicator >= 2)
		DumpUdpPacketBytes("sent", m_ServerAddr, vPacket.data(), (int)vPacket.size());

	const int Sent = net_udp_send(m_Socket, &m_ServerAddr, vPacket.data(), (int)vPacket.size());
	char aServerAddr[NETADDR_MAXSTRSIZE];
	net_addr_str(&m_ServerAddr, aServerAddr, sizeof(aServerAddr), true);
	DebugLogF("sent %s packet client_id=%d player='%s' game_server=%s indicator_server=%s bytes=%d result=%d",
		PacketTypeName(PacketType), ClientId, PlayerNameForClient(ClientId), CurrentGameServerAddress(), aServerAddr, (int)vPacket.size(), Sent);
}

void CClientIndicator::SendLeaveForAll()
{
	if(!m_Socket || !m_HasServerAddr)
		return;
	for(const int ClientId : m_RegisteredClientIds)
		SendPresencePacket(ClientId, BestClientIndicator::PACKET_LEAVE);
}

void CClientIndicator::ProcessIncomingPackets(bool Force)
{
	if(!m_Socket || !m_HasServerAddr)
		return;

	const int64_t StartTick = time_get();
	if(!CSubsystemTicker::ShouldRunPeriodic(StartTick, m_LastPresencePollTick, time_freq() / 20, Force))
		return;
	int ReceivedPackets = 0;
	int ProcessedPackets = 0;

	for(int PacketCount = 0; PacketCount < BestClientIndicator::MAX_RECEIVE_PACKETS_PER_TICK; ++PacketCount)
	{
		NETADDR From = NETADDR_ZEROED;
		unsigned char *pRawData = nullptr;
		const int DataSize = net_udp_recv(m_Socket, &From, &pRawData);
		if(DataSize <= 0 || !pRawData)
			break;
		ReceivedPackets++;
		if(g_Config.m_DbgClientIndicator >= 2)
		{
			char aFrom[NETADDR_MAXSTRSIZE];
			net_addr_str(&From, aFrom, sizeof(aFrom), true);
			BestClientIndicator::EPacketType Type = (BestClientIndicator::EPacketType)0;
			int Offset = 0;
			const bool HasHeader = BestClientIndicator::ReadHeader(pRawData, DataSize, Type, Offset);
			char aServerAddr[NETADDR_MAXSTRSIZE];
			net_addr_str(&m_ServerAddr, aServerAddr, sizeof(aServerAddr), true);
			if(HasHeader)
				DebugLogF("received udp packet from=%s bytes=%d type=%d(%s) expected_indicator_server=%s", aFrom, DataSize, (int)Type, PacketTypeName((int)Type), aServerAddr);
			else
				DebugLogF("received udp packet from=%s bytes=%d type=invalid expected_indicator_server=%s", aFrom, DataSize, aServerAddr);

			DumpUdpPacketBytes("recv", From, pRawData, DataSize);
		}

		if(net_addr_comp(&From, &m_ServerAddr) != 0)
		{
			if(g_Config.m_DbgClientIndicator >= 2)
			{
				char aFrom[NETADDR_MAXSTRSIZE];
				char aServerAddr[NETADDR_MAXSTRSIZE];
				net_addr_str(&From, aFrom, sizeof(aFrom), true);
				net_addr_str(&m_ServerAddr, aServerAddr, sizeof(aServerAddr), true);
				DebugLogF("ignoring udp packet from=%s (expected indicator_server=%s)", aFrom, aServerAddr);
			}
			continue;
		}

		ProcessedPackets++;
		BestClientIndicator::CPeerState PeerState;
		if(BestClientIndicator::ReadPeerStatePacket(pRawData, DataSize, PeerState))
		{
			DebugLogF("received peer_state client_id=%d player='%s' server=%s", PeerState.m_ClientId, PeerState.m_PlayerName.c_str(), PeerState.m_ServerAddress.c_str());
			if(PeerState.m_ServerAddress == m_PresenceCache.ServerAddress())
				m_PresenceCache.SetPresent(PeerState.m_ClientId, true);
			continue;
		}

		if(BestClientIndicator::ReadPeerRemovePacket(pRawData, DataSize, PeerState))
		{
			DebugLogF("received peer_remove client_id=%d player='%s' server=%s", PeerState.m_ClientId, PeerState.m_PlayerName.c_str(), PeerState.m_ServerAddress.c_str());
			if(PeerState.m_ServerAddress == m_PresenceCache.ServerAddress())
				m_PresenceCache.SetPresent(PeerState.m_ClientId, false);
			continue;
		}

		BestClientIndicator::CPeerList PeerList;
		if(BestClientIndicator::ReadPeerListPacket(pRawData, DataSize, PeerList) &&
			PeerList.m_ServerAddress == m_PresenceCache.ServerAddress())
		{
			DebugLogF("received peer_list server=%s count=%zu", PeerList.m_ServerAddress.c_str(), PeerList.m_vClientIds.size());
			m_PresenceCache.Replace(PeerList.m_vClientIds);
			continue;
		}

		if(g_Config.m_DbgClientIndicator >= 2)
			DebugLogF("received udp packet from indicator server but did not match known packet formats bytes=%d", DataSize);
	}

	const int64_t EndTick = time_get();
	const int64_t Delta = EndTick - StartTick;
	if(g_Config.m_DbgClientIndicator && Delta > SlowPacketProcessTicks())
	{
		const int64_t Ms = (Delta * 1000) / time_freq();
		DebugLogF("ProcessIncomingPackets slow: %lldms received=%d processed=%d", (long long)Ms, ReceivedPackets, ProcessedPackets);
	}
}

void CClientIndicator::SyncLocalRegistrations(bool Force)
{
	if(!m_Socket || !m_HasServerAddr)
		return;

	const int64_t Now = time_get();
	if(!CSubsystemTicker::ShouldRunPeriodic(Now, m_LastRegistrationSyncTick, time_freq() / 4, Force))
		return;

	std::array<bool, MAX_CLIENTS> aClientActive{};
	for(const int ClientId : GameClient()->m_aLocalIds)
	{
		if(ClientId >= 0 && ClientId < MAX_CLIENTS)
			aClientActive[ClientId] = GameClient()->m_aClients[ClientId].m_Active;
	}
	const auto DesiredClientIds = BestClientIndicatorClient::CollectActiveLocalClientIds(GameClient()->m_aLocalIds, aClientActive);

	for(const int ClientId : DesiredClientIds.m_vClientIds)
	{
		if(m_RegisteredClientIds.find(ClientId) == m_RegisteredClientIds.end())
		{
			SendPresencePacket(ClientId, BestClientIndicator::PACKET_JOIN);
			m_RegisteredClientIds.insert(ClientId);
			m_PresenceCache.SetPresent(ClientId, true);
			DebugLogF("registered local client_id=%d name='%s'", ClientId, PlayerNameForClient(ClientId));
		}
	}

	for(auto It = m_RegisteredClientIds.begin(); It != m_RegisteredClientIds.end();)
	{
		if(!DesiredClientIds.Contains(*It))
		{
			SendPresencePacket(*It, BestClientIndicator::PACKET_LEAVE);
			m_PresenceCache.SetPresent(*It, false);
			DebugLogF("unregistered local client_id=%d", *It);
			It = m_RegisteredClientIds.erase(It);
		}
		else
		{
			++It;
		}
	}
}

void CClientIndicator::UpdatePresence()
{
	const bool PresenceEnabled = g_Config.m_BcClientIndicator != 0;
	if(!PresenceEnabled || Client()->State() != IClient::STATE_ONLINE)
	{
		if(m_WasPresenceEnabled || m_Socket)
			StopPresence(true);
		SetPresenceBlockReason("presence update skipped: indicator disabled or client offline");
		m_WasPresenceEnabled = false;
		return;
	}

	if(EffectiveSharedToken()[0] == '\0')
	{
		SetPresenceBlockReason("presence update skipped: effective shared token is empty");
		m_WasPresenceEnabled = PresenceEnabled;
		return;
	}

	EnsurePresenceSocket();
	if(!m_Socket || !m_HasServerAddr)
	{
		if(m_LastPresenceBlockReason.empty())
			SetPresenceBlockReason("presence update skipped: udp socket is not ready");
		m_WasPresenceEnabled = PresenceEnabled;
		return;
	}

	const char *pCurrentGameServer = CurrentGameServerAddress();
	if(pCurrentGameServer[0] == '\0')
	{
		SetPresenceBlockReason("presence update skipped: current game server address is empty");
		if(m_PresenceCache.SetServerAddress(""))
		{
			m_RegisteredClientIds.clear();
			m_LastHeartbeatTick = 0;
		}
		m_WasPresenceEnabled = PresenceEnabled;
		return;
	}
	if(m_PresenceCache.SetServerAddress(pCurrentGameServer))
	{
		DebugLogF("presence server changed to game server %s", pCurrentGameServer);
		m_RegisteredClientIds.clear();
		m_LastHeartbeatTick = 0;
	}
	ClearPresenceBlockReason();

	SyncLocalRegistrations(false);
	ProcessIncomingPackets(false);

	const int64_t Now = time_get();
	if(m_LastHeartbeatTick == 0 || Now - m_LastHeartbeatTick > time_freq() * 5)
	{
		for(const int ClientId : m_RegisteredClientIds)
			SendPresencePacket(ClientId, BestClientIndicator::PACKET_HEARTBEAT);
		m_LastHeartbeatTick = Now;
		DebugLogF("heartbeat tick sent for %zu local clients", m_RegisteredClientIds.size());
	}

	m_WasPresenceEnabled = PresenceEnabled;
}

void CClientIndicator::FinishBrowserCacheRefresh()
{
	if(!m_pBrowserTask)
		return;
	json_value *pJson = m_pBrowserTask->ResultJson();
	if(!pJson)
	{
		DebugLog("browser request completed but JSON parsing returned null");
		return;
	}
	m_BrowserCache.Load(*pJson);
	json_value_free(pJson);
	DebugLogF("browser cache loaded %zu entries", m_BrowserCache.Players().size());
	ApplyBrowserSnapshot();
}

void CClientIndicator::ResetBrowserTask()
{
	if(m_pBrowserTask)
	{
		m_pBrowserTask->Abort();
		m_pBrowserTask = nullptr;
	}
}

void CClientIndicator::FinishTokenRefresh()
{
	if(!m_pTokenTask)
		return;
	json_value *pJson = m_pTokenTask->ResultJson();
	char aOldEffectiveToken[sizeof(m_aWebSharedToken)];
	str_copy(aOldEffectiveToken, EffectiveSharedToken(), sizeof(aOldEffectiveToken));
	m_aWebSharedToken[0] = '\0';

	if(pJson)
	{
		const char *pToken = nullptr;
		if(pJson->type == json_object)
		{
			const json_value &Token = (*pJson)["token"];
			if(Token.type == json_string)
				pToken = Token.u.string.ptr;
		}

		if(pToken && pToken[0] != '\0')
			str_copy(m_aWebSharedToken, pToken, sizeof(m_aWebSharedToken));

		json_value_free(pJson);
	}
	else
	{
		DebugLog("token request completed but JSON parsing returned null");
	}

	if(m_aWebSharedToken[0] != '\0')
		DebugLogF("token refresh succeeded, web token length=%d", str_length(m_aWebSharedToken));
	else
		DebugLogF("token refresh produced empty web token, fallback token length=%d", str_length(g_Config.m_BcClientIndicatorSharedToken));

	if(str_comp(aOldEffectiveToken, EffectiveSharedToken()) != 0)
	{
		DebugLog("effective token changed, restarting presence state");
		StopPresence(true);
	}
}

void CClientIndicator::ResetTokenTask()
{
	if(m_pTokenTask)
	{
		m_pTokenTask->Abort();
		m_pTokenTask = nullptr;
	}
}

void CClientIndicator::ApplyBrowserSnapshot()
{
	ServerBrowser()->SetBestClientPlayers(m_BrowserCache.Players());
}

void CClientIndicator::ResetPresenceState()
{
	m_LastHeartbeatTick = 0;
	m_LastPresenceStartAttempt = 0;
	m_WasPresenceEnabled = false;
	m_RegisteredClientIds.clear();
	m_PresenceCache.Clear();
	m_aLastGameServerAddr[0] = '\0';
	m_LastPresenceBlockReason.clear();
}

void CClientIndicator::ResetTokenState()
{
	ResetTokenTask();
	m_aWebSharedToken[0] = '\0';
}

void CClientIndicator::ClearBrowserSnapshot()
{
	if(m_BrowserCache.Players().empty())
		return;
	m_BrowserCache.Clear();
	ApplyBrowserSnapshot();
}

void CClientIndicator::ReapplyBrowserSnapshot()
{
	ApplyBrowserSnapshot();
}

bool CClientIndicator::HasPendingNetworkTask() const
{
	return (m_pBrowserTask && !m_pBrowserTask->Done()) || (m_pTokenTask && !m_pTokenTask->Done());
}

bool CClientIndicator::IsBrowserSnapshotEnabled() const
{
	return g_Config.m_BcClientIndicator != 0;
}

bool CClientIndicator::IsPresenceEnabled() const
{
	return g_Config.m_BcClientIndicator != 0 && Client()->State() == IClient::STATE_ONLINE;
}

const char *CClientIndicator::EffectiveSharedToken() const
{
	if(m_aWebSharedToken[0] != '\0')
		return m_aWebSharedToken;
	return g_Config.m_BcClientIndicatorSharedToken;
}

void CClientIndicator::DebugLog(const char *pText) const
{
	if(!g_Config.m_DbgClientIndicator)
		return;
	log_info(LOG_SCOPE, "%s", pText);
}

void CClientIndicator::DebugLogF(const char *pFormat, ...) const
{
	if(!g_Config.m_DbgClientIndicator)
		return;
	char aBuf[1024];
	va_list Args;
	va_start(Args, pFormat);
	str_format_v(aBuf, sizeof(aBuf), pFormat, Args);
	va_end(Args);
	log_info(LOG_SCOPE, "%s", aBuf);
}

void CClientIndicator::SetPresenceBlockReason(const char *pReason)
{
	if(m_LastPresenceBlockReason == pReason)
		return;
	m_LastPresenceBlockReason = pReason;
	DebugLog(pReason);
}

void CClientIndicator::ClearPresenceBlockReason()
{
	m_LastPresenceBlockReason.clear();
}
