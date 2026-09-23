/* Copyright © 2026 BestProject Team */
#include "client_indicator.h"

#include "protocol.h"
#include "version_label.h"
#include "../subsystem_runtime.h"
#include "../version.h"

#include <base/log.h>
#include <base/math.h>
#include <base/mem.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/http.h>
#include <engine/serverbrowser.h>
#include <engine/shared/bestclient_indicator_protocol.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/shared/network.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <cstdarg>
#include <cstring>

namespace
{
	constexpr const char *LOG_SCOPE = "clientindicator-cl";
	constexpr const char *OLD_BC_BROWSER_URL = "http://150.241.70.188:8779/users.json";
	constexpr const char *OLD_BC_TOKEN_URL = "http://150.241.70.188:8779/token.json";
	constexpr const char *NEW_BC_BROWSER_URL = "https://150.241.70.188:8779/users.json";
	constexpr const char *NEW_BC_TOKEN_URL = "https://150.241.70.188:8779/token.json";
	constexpr int PACKET_DUMP_BYTES_PER_LINE = 64;

	void TrimConfigString(char *pValue, int Size)
	{
		if(!pValue || Size <= 0)
			return;

		const int Length = str_length(pValue);
		int Start = 0;
		while(Start < Length && str_isspace(pValue[Start]))
			++Start;

		int End = Length;
		while(End > Start && str_isspace(pValue[End - 1]))
			--End;

		if(Start == 0 && End == Length)
			return;

		const std::string Trimmed(pValue + Start, End - Start);
		str_copy(pValue, Trimmed.c_str(), Size);
	}

	int64_t SlowPacketProcessTicks()
	{
		return time_freq() / 500; // ~2ms
	}

	void NormalizeBestClientIndicatorConfig()
	{
		TrimConfigString(g_Config.m_BcClientIndicatorServerAddress, sizeof(g_Config.m_BcClientIndicatorServerAddress));
		TrimConfigString(g_Config.m_BcClientIndicatorBrowserUrl, sizeof(g_Config.m_BcClientIndicatorBrowserUrl));
		TrimConfigString(g_Config.m_BcClientIndicatorTokenUrl, sizeof(g_Config.m_BcClientIndicatorTokenUrl));
		TrimConfigString(g_Config.m_BcClientIndicatorSharedToken, sizeof(g_Config.m_BcClientIndicatorSharedToken));
		TrimConfigString(g_Config.m_BcClientIndicatorSecretKey, sizeof(g_Config.m_BcClientIndicatorSecretKey));

		if(g_Config.m_BcClientIndicatorBrowserUrl[0] == '\0' || str_comp(g_Config.m_BcClientIndicatorBrowserUrl, OLD_BC_BROWSER_URL) == 0)
			str_copy(g_Config.m_BcClientIndicatorBrowserUrl, NEW_BC_BROWSER_URL, sizeof(g_Config.m_BcClientIndicatorBrowserUrl));
		if(g_Config.m_BcClientIndicatorTokenUrl[0] == '\0' || str_comp(g_Config.m_BcClientIndicatorTokenUrl, OLD_BC_TOKEN_URL) == 0)
			str_copy(g_Config.m_BcClientIndicatorTokenUrl, NEW_BC_TOKEN_URL, sizeof(g_Config.m_BcClientIndicatorTokenUrl));
	}

	bool IsBlockedIndicatorAddress(const NETADDR &Addr)
	{
		return net_addr_is_local(&Addr);
	}

	void StripVersionProof(char *pVersion)
	{
		BestClientStripVersionProof(pVersion);
	}

	void CopyVersionLabel(const char *pRaw, char *pVersion, int VersionSize)
	{
		str_copy(pVersion, pRaw ? pRaw : "", VersionSize);
		StripVersionProof(pVersion);
	}

	bool HasOfficialCode()
	{
		return BESTCLIENT_OFFICIAL_CODE[0] != '\0';
	}

	bool BuildVersionAnnounceFields(char *pVersionOut, int VersionOutSize, char *pOfficialCodeOut, int OfficialCodeOutSize)
	{
		if(!pVersionOut || VersionOutSize <= 0 || !pOfficialCodeOut || OfficialCodeOutSize <= 0)
			return false;
		pVersionOut[0] = '\0';
		pOfficialCodeOut[0] = '\0';
		if(BESTCLIENT_VERSION[0] == '\0')
			return false;

		str_copy(pVersionOut, BESTCLIENT_VERSION, VersionOutSize);
		str_copy(pOfficialCodeOut, BESTCLIENT_OFFICIAL_CODE, OfficialCodeOutSize);
		return true;
	}

	bool IsValidPeerClientId(int ClientId)
	{
		return ClientId >= 0 && ClientId < MAX_CLIENTS;
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
		case BestClientIndicator::PACKET_DEV_AUTH:
			return "dev_auth";
		case BestClientIndicator::PACKET_PEER_DEV_STATE:
			return "peer_dev_state";
		case BestClientIndicator::PACKET_PEER_DEV_LIST:
			return "peer_dev_list";
		case BestClientIndicator::PACKET_DEV_AUTH_RESULT:
			return "dev_auth_result";
		case BestClientIndicator::PACKET_VERSION_ANNOUNCE:
			return "version_announce";
		case BestClientIndicator::PACKET_PEER_VERSION_STATE:
			return "peer_version_state";
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
			const int ChunkSize = std::min(PACKET_DUMP_BYTES_PER_LINE, DataSize - Offset);
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
	NormalizeBestClientIndicatorConfig();
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
		RefreshBrowserCache(false);
		RefreshToken(false);
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
	if(Client()->State() != IClient::STATE_ONLINE)
		return false;
	for(const int LocalId : GameClient()->m_aLocalIds)
	{
		if(LocalId >= 0 && ClientId == LocalId)
			return true;
	}
	if(m_PresenceCache.IsPresent(ClientId))
		return true;

	const char *pPlayerName = PlayerNameForClient(ClientId);
	if(pPlayerName[0] == '\0')
		return false;

	const char *pServerAddress = m_aCachedGameServerAddr[0] != '\0' ? m_aCachedGameServerAddr : nullptr;
	char aFallbackAddress[NETADDR_MAXSTRSIZE];
	if(!pServerAddress)
	{
		net_addr_str(&Client()->ServerAddress(), aFallbackAddress, sizeof(aFallbackAddress), true);
		pServerAddress = aFallbackAddress;
	}
	if(m_BrowserCache.HasPlayer(pServerAddress, pPlayerName))
		return true;

	const IServerBrowser::CServerEntry *pCurrentServer = ServerBrowser()->Find(Client()->ServerAddress());
	if(!pCurrentServer)
		return false;

	for(const auto &ClientInfo : pCurrentServer->m_Info.m_vClients)
	{
		if(ClientInfo.m_BestClient && str_comp(ClientInfo.m_aName, pPlayerName) == 0)
			return true;
	}

	return false;
}

bool CClientIndicator::IsPlayerDeveloper(int ClientId) const
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return false;
	if(m_DeveloperClientIds.find(ClientId) != m_DeveloperClientIds.end())
		return true;

	const char *pPlayerName = PlayerNameForClient(ClientId);
	if(pPlayerName[0] == '\0')
		return false;

	const char *pServerAddress = m_aCachedGameServerAddr[0] != '\0' ? m_aCachedGameServerAddr : nullptr;
	char aFallbackAddress[NETADDR_MAXSTRSIZE];
	if(!pServerAddress)
	{
		net_addr_str(&Client()->ServerAddress(), aFallbackAddress, sizeof(aFallbackAddress), true);
		pServerAddress = aFallbackAddress;
	}
	bool BrowserDeveloper = false;
	if(m_BrowserCache.HasPlayer(pServerAddress, pPlayerName, &BrowserDeveloper) && BrowserDeveloper)
		return true;

	const IServerBrowser::CServerEntry *pCurrentServer = ServerBrowser()->Find(Client()->ServerAddress());
	if(!pCurrentServer)
		return false;

	for(const auto &ClientInfo : pCurrentServer->m_Info.m_vClients)
	{
		if(ClientInfo.m_BestClientDeveloper && str_comp(ClientInfo.m_aName, pPlayerName) == 0)
			return true;
	}

	return false;
}

bool CClientIndicator::IsPlayerFakeVersion(int ClientId) const
{
	if(!IsPlayerBestClient(ClientId))
		return false;

	for(const int LocalId : GameClient()->m_aLocalIds)
	{
		if(LocalId >= 0 && ClientId == LocalId)
			return !HasOfficialCode();
	}

	if(m_FakeClientIds.find(ClientId) != m_FakeClientIds.end())
		return true;

	const char *pPlayerName = PlayerNameForClient(ClientId);
	if(pPlayerName[0] != '\0')
	{
		const char *pServerAddress = m_aCachedGameServerAddr[0] != '\0' ? m_aCachedGameServerAddr : nullptr;
		char aFallbackAddress[NETADDR_MAXSTRSIZE];
		if(!pServerAddress)
		{
			net_addr_str(&Client()->ServerAddress(), aFallbackAddress, sizeof(aFallbackAddress), true);
			pServerAddress = aFallbackAddress;
		}
		bool BrowserFake = false;
		if(m_BrowserCache.HasPlayer(pServerAddress, pPlayerName, nullptr, &BrowserFake) && BrowserFake)
			return true;

		const IServerBrowser::CServerEntry *pCurrentServer = ServerBrowser()->Find(Client()->ServerAddress());
		if(pCurrentServer)
		{
			for(const auto &ClientInfo : pCurrentServer->m_Info.m_vClients)
			{
				if(ClientInfo.m_BestClientFake && str_comp(ClientInfo.m_aName, pPlayerName) == 0)
					return true;
			}
		}
	}

	return false;
}

bool CClientIndicator::GetPlayerVersionLabel(int ClientId, char *pVersion, int VersionSize) const
{
	if(!pVersion || VersionSize <= 0)
		return false;
	pVersion[0] = '\0';

	if(Client()->State() != IClient::STATE_ONLINE)
		return false;

	for(const int LocalId : GameClient()->m_aLocalIds)
	{
		if(LocalId >= 0 && ClientId == LocalId)
		{
			str_copy(pVersion, BESTCLIENT_VERSION, VersionSize);
			return true;
		}
	}

	const char *pPlayerName = PlayerNameForClient(ClientId);
	if(pPlayerName[0] == '\0')
		return false;

	char aCurrentServerAddress[NETADDR_MAXSTRSIZE];
	net_addr_str(&Client()->ServerAddress(), aCurrentServerAddress, sizeof(aCurrentServerAddress), true);
	const auto It = m_ClientVersions.find(ClientId);
	if(It != m_ClientVersions.end() && !It->second.empty())
	{
		CopyVersionLabel(It->second.c_str(), pVersion, VersionSize);
		return true;
	}
	if(m_BrowserCache.GetPlayerVersion(aCurrentServerAddress, pPlayerName, pVersion, VersionSize))
	{
		StripVersionProof(pVersion);
		return true;
	}

	if(IsPlayerBestClient(ClientId))
	{
		str_copy(pVersion, "under", VersionSize);
		return true;
	}

	return false;
}

void CClientIndicator::RefreshBrowserCache(bool Force)
{
#if defined(CONF_HEADLESS_CLIENT)
	(void)Force;
	return;
#endif
	NormalizeBestClientIndicatorConfig();
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
	m_pBrowserTask->VerifyPeer(false);
	m_pBrowserTask->FailOnErrorStatus(false);
	m_pBrowserTask->LogProgress(HTTPLOG::FAILURE);
	DebugLogF("starting browser request url=%s", g_Config.m_BcClientIndicatorBrowserUrl);
	m_LastBrowserRefreshTick = time_get();
	Http()->Run(m_pBrowserTask);
}

void CClientIndicator::RefreshToken(bool Force)
{
#if defined(CONF_HEADLESS_CLIENT)
	(void)Force;
	return;
#endif
	NormalizeBestClientIndicatorConfig();
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
	m_pTokenTask->VerifyPeer(false);
	m_pTokenTask->FailOnErrorStatus(false);
	m_pTokenTask->LogProgress(HTTPLOG::FAILURE);
	DebugLogF("starting token request url=%s", g_Config.m_BcClientIndicatorTokenUrl);
	m_LastTokenRefreshTick = time_get();
	Http()->Run(m_pTokenTask);
}

void CClientIndicator::OnUpdate()
{
	const bool PresenceEnabled = IsPresenceEnabled();
	const int64_t Now = time_get();
	const int64_t PerfStart = g_Config.m_DbgClientIndicator >= 2 ? Now : 0;
	const int64_t TokenRefreshInterval = 5 * 60 * time_freq();

	if(!m_pBrowserTask && m_LastBrowserRefreshTick == 0)
		RefreshBrowserCache(false);

	if(PresenceEnabled && !m_WasPresenceEnabled)
	{
		RefreshToken(false);
	}
	else if(PresenceEnabled)
	{
		if(!m_pTokenTask && (m_LastTokenRefreshTick == 0 || Now - m_LastTokenRefreshTick >= TokenRefreshInterval))
			RefreshToken(false);
	}
	else
	{
		if(m_WasPresenceEnabled || m_Socket || m_HasServerAddr || m_pTokenTask || m_aWebSharedToken[0] != '\0')
		{
			StopPresence(true);
			ResetTokenState();
		}
		SetPresenceBlockReason("presence update skipped: client offline");
		m_WasPresenceEnabled = false;
		m_aCachedGameServerAddr[0] = '\0';
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

	if(g_Config.m_DbgClientIndicator >= 2)
	{
		m_LastUpdateCostTick = time_get() - PerfStart;
		m_MaxUpdateCostTick = std::max(m_MaxUpdateCostTick, m_LastUpdateCostTick);
		m_TotalUpdateCostTick += m_LastUpdateCostTick;
		++m_UpdateSamples;
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
	NormalizeBestClientIndicatorConfig();
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
	NormalizeBestClientIndicatorConfig();
	if(Client()->State() != IClient::STATE_ONLINE)
	{
		SetPresenceBlockReason("presence socket skipped: client offline");
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
		m_aLastBlockedGameServerAddr[0] = '\0';
		DebugLog("current game server address unavailable: client offline");
		return "";
	}
	if(IsBlockedIndicatorAddress(Client()->ServerAddress()))
	{
		char aAddr[NETADDR_MAXSTRSIZE];
		net_addr_str(&Client()->ServerAddress(), aAddr, sizeof(aAddr), true);
		if(str_comp(m_aLastBlockedGameServerAddr, aAddr) != 0)
		{
			DebugLogF("current game server address blocked: %s is local", aAddr);
			str_copy(m_aLastBlockedGameServerAddr, aAddr, sizeof(m_aLastBlockedGameServerAddr));
		}
		return "";
	}
	m_aLastBlockedGameServerAddr[0] = '\0';
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

void CClientIndicator::SendDevAuthPacket(int ClientId)
{
	NormalizeBestClientIndicatorConfig();
	if(!m_Socket || !m_HasServerAddr || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	if(g_Config.m_BcClientIndicatorSecretKey[0] == '\0')
		return;

	std::vector<uint8_t> vPacket;
	vPacket.reserve(256);
	BestClientIndicator::WriteHeader(vPacket, BestClientIndicator::PACKET_DEV_AUTH);
	BestClientIndicator::WriteUuid(vPacket, m_ClientInstanceId);
	const CUuid Nonce = RandomUuid();
	BestClientIndicator::WriteUuid(vPacket, Nonce);
	BestClientIndicator::WriteU64(vPacket, (uint64_t)time_timestamp());
	BestClientIndicator::WriteString(vPacket, CurrentGameServerAddress());
	BestClientIndicator::WriteString(vPacket, PlayerNameForClient(ClientId));
	BestClientIndicator::WriteS16(vPacket, (int16_t)ClientId);
	BestClientIndicator::AppendHmacSha256(vPacket, g_Config.m_BcClientIndicatorSecretKey);

	if(g_Config.m_DbgClientIndicator >= 2)
		DumpUdpPacketBytes("sent", m_ServerAddr, vPacket.data(), (int)vPacket.size());

	const int Sent = net_udp_send(m_Socket, &m_ServerAddr, vPacket.data(), (int)vPacket.size());
	char aServerAddr[NETADDR_MAXSTRSIZE];
	net_addr_str(&m_ServerAddr, aServerAddr, sizeof(aServerAddr), true);
	DebugLogF("sent dev_auth packet client_id=%d player='%s' game_server=%s indicator_server=%s bytes=%d result=%d",
		ClientId, PlayerNameForClient(ClientId), CurrentGameServerAddress(), aServerAddr, (int)vPacket.size(), Sent);
}

void CClientIndicator::SendVersionPacket(int ClientId)
{
	if(!m_Socket || !m_HasServerAddr)
		return;

	const char *pSharedToken = EffectiveSharedToken();
	if(pSharedToken[0] == '\0' || BESTCLIENT_VERSION[0] == '\0')
		return;

	char aVersion[64];
	char aOfficialCode[128];
	if(!BuildVersionAnnounceFields(aVersion, sizeof(aVersion), aOfficialCode, sizeof(aOfficialCode)))
		return;

	std::vector<uint8_t> vPacket;
	vPacket.reserve(256);
	BestClientIndicator::WriteHeader(vPacket, BestClientIndicator::PACKET_VERSION_ANNOUNCE);
	BestClientIndicator::WriteUuid(vPacket, m_ClientInstanceId);
	CUuid Nonce = RandomUuid();
	BestClientIndicator::WriteUuid(vPacket, Nonce);
	BestClientIndicator::WriteU64(vPacket, (uint64_t)time_timestamp());
	BestClientIndicator::WriteString(vPacket, CurrentGameServerAddress());
	BestClientIndicator::WriteString(vPacket, PlayerNameForClient(ClientId));
	BestClientIndicator::WriteS16(vPacket, (int16_t)ClientId);
	BestClientIndicator::WriteString(vPacket, aVersion);
	BestClientIndicator::WriteString(vPacket, aOfficialCode);
	BestClientIndicator::AppendProof(vPacket, pSharedToken);

	if(g_Config.m_DbgClientIndicator >= 2)
		DumpUdpPacketBytes("sent", m_ServerAddr, vPacket.data(), (int)vPacket.size());

	const int Sent = net_udp_send(m_Socket, &m_ServerAddr, vPacket.data(), (int)vPacket.size());
	char aServerAddr[NETADDR_MAXSTRSIZE];
	net_addr_str(&m_ServerAddr, aServerAddr, sizeof(aServerAddr), true);
	DebugLogF("sent %s packet client_id=%d version='%s' official_code=%s player='%s' game_server=%s indicator_server=%s bytes=%d result=%d",
		PacketTypeName(BestClientIndicator::PACKET_VERSION_ANNOUNCE), ClientId, aVersion, aOfficialCode[0] != '\0' ? "set" : "empty", PlayerNameForClient(ClientId), CurrentGameServerAddress(), aServerAddr, (int)vPacket.size(), Sent);
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
			if(IsValidPeerClientId(PeerState.m_ClientId) && PeerState.m_ServerAddress == m_PresenceCache.ServerAddress())
			{
				m_PresenceCache.SetPresent(PeerState.m_ClientId, true);
			}
			continue;
		}

		if(BestClientIndicator::ReadPeerRemovePacket(pRawData, DataSize, PeerState))
		{
			DebugLogF("received peer_remove client_id=%d player='%s' server=%s", PeerState.m_ClientId, PeerState.m_PlayerName.c_str(), PeerState.m_ServerAddress.c_str());
			if(IsValidPeerClientId(PeerState.m_ClientId) && PeerState.m_ServerAddress == m_PresenceCache.ServerAddress())
			{
				m_PresenceCache.SetPresent(PeerState.m_ClientId, false);
				m_DeveloperClientIds.erase(PeerState.m_ClientId);
				m_FakeClientIds.erase(PeerState.m_ClientId);
				m_ClientVersions.erase(PeerState.m_ClientId);
			}
			continue;
		}

		BestClientIndicator::CPeerList PeerList;
		if(BestClientIndicator::ReadPeerListPacket(pRawData, DataSize, PeerList) &&
			PeerList.m_ServerAddress == m_PresenceCache.ServerAddress())
		{
			DebugLogF("received peer_list server=%s count=%llu", PeerList.m_ServerAddress.c_str(), (unsigned long long)PeerList.m_vClientIds.size());
			PeerList.m_vClientIds.erase(std::remove_if(PeerList.m_vClientIds.begin(), PeerList.m_vClientIds.end(),
						    [](int ClientId) { return !IsValidPeerClientId(ClientId); }),
				PeerList.m_vClientIds.end());
			m_PresenceCache.Replace(PeerList.m_vClientIds);
			continue;
		}

		if(BestClientIndicator::ReadPeerDevStatePacket(pRawData, DataSize, PeerState))
		{
			DebugLogF("received peer_dev_state client_id=%d developer=%d player='%s' server=%s", PeerState.m_ClientId, PeerState.m_Developer ? 1 : 0, PeerState.m_PlayerName.c_str(), PeerState.m_ServerAddress.c_str());
			if(IsValidPeerClientId(PeerState.m_ClientId) && PeerState.m_ServerAddress == m_PresenceCache.ServerAddress())
			{
				if(PeerState.m_Developer)
					m_DeveloperClientIds.insert(PeerState.m_ClientId);
				else
					m_DeveloperClientIds.erase(PeerState.m_ClientId);
			}
			continue;
		}

		if(BestClientIndicator::ReadPeerDevListPacket(pRawData, DataSize, PeerList) &&
			PeerList.m_ServerAddress == m_PresenceCache.ServerAddress())
		{
			DebugLogF("received peer_dev_list server=%s count=%llu", PeerList.m_ServerAddress.c_str(), (unsigned long long)PeerList.m_vClientIds.size());
			m_DeveloperClientIds.clear();
			for(const int ClientId : PeerList.m_vClientIds)
			{
				if(IsValidPeerClientId(ClientId))
					m_DeveloperClientIds.insert(ClientId);
			}
			continue;
		}

		BestClientIndicator::CPeerVersionState PeerVersionState;
		if(BestClientIndicator::ReadPeerVersionStatePacket(pRawData, DataSize, PeerVersionState))
		{
			DebugLogF("received peer_version_state client_id=%d version='%s' fake=%d player='%s' server=%s", PeerVersionState.m_ClientId, PeerVersionState.m_ClientVersion.c_str(), PeerVersionState.m_Fake ? 1 : 0, PeerVersionState.m_PlayerName.c_str(), PeerVersionState.m_ServerAddress.c_str());
			if(IsValidPeerClientId(PeerVersionState.m_ClientId) && PeerVersionState.m_ServerAddress == m_PresenceCache.ServerAddress())
			{
				m_ClientVersions[PeerVersionState.m_ClientId] = PeerVersionState.m_ClientVersion;
				if(PeerVersionState.m_Fake)
					m_FakeClientIds.insert(PeerVersionState.m_ClientId);
				else
					m_FakeClientIds.erase(PeerVersionState.m_ClientId);
			}
			continue;
		}

		BestClientIndicator::CDevAuthResult DevAuthResult;
		if(BestClientIndicator::ReadDevAuthResultPacket(pRawData, DataSize, DevAuthResult) &&
			DevAuthResult.m_ServerAddress == m_PresenceCache.ServerAddress())
		{
			DebugLogF("received dev_auth_result client_id=%d success=%d server=%s", DevAuthResult.m_ClientId, DevAuthResult.m_Success ? 1 : 0, DevAuthResult.m_ServerAddress.c_str());
			if(IsValidPeerClientId(DevAuthResult.m_ClientId))
			{
				if(DevAuthResult.m_Success)
				{
					m_DeveloperClientIds.insert(DevAuthResult.m_ClientId);
				}
				else
					m_DeveloperClientIds.erase(DevAuthResult.m_ClientId);
			}
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
			SendVersionPacket(ClientId);
			SendDevAuthPacket(ClientId);
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
			m_DeveloperClientIds.erase(*It);
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
	if(Client()->State() != IClient::STATE_ONLINE)
	{
		if(m_WasPresenceEnabled || m_Socket)
			StopPresence(true);
		SetPresenceBlockReason("presence update skipped: client offline");
		m_WasPresenceEnabled = false;
		return;
	}

	if(EffectiveSharedToken()[0] == '\0')
	{
		SetPresenceBlockReason("presence update skipped: effective shared token is empty");
		m_WasPresenceEnabled = true;
		return;
	}

	EnsurePresenceSocket();
	if(!m_Socket || !m_HasServerAddr)
	{
		if(m_LastPresenceBlockReason.empty())
			SetPresenceBlockReason("presence update skipped: udp socket is not ready");
		m_WasPresenceEnabled = true;
		return;
	}

	const char *pCurrentGameServer = CurrentGameServerAddress();
	if(pCurrentGameServer[0] == '\0')
	{
		SetPresenceBlockReason("presence update skipped: current game server address is empty");
		if(m_PresenceCache.SetServerAddress(""))
		{
			m_RegisteredClientIds.clear();
			m_DeveloperClientIds.clear();
			m_LastHeartbeatTick = 0;
		}
		m_WasPresenceEnabled = true;
		return;
	}
	if(m_PresenceCache.SetServerAddress(pCurrentGameServer))
	{
		DebugLogF("presence server changed to game server %s", pCurrentGameServer);
		m_RegisteredClientIds.clear();
		m_DeveloperClientIds.clear();
		m_FakeClientIds.clear();
		m_ClientVersions.clear();
		m_LastHeartbeatTick = 0;
	}
	str_copy(m_aCachedGameServerAddr, pCurrentGameServer, sizeof(m_aCachedGameServerAddr));
	ClearPresenceBlockReason();

	SyncLocalRegistrations(false);
	ProcessIncomingPackets(false);

	const int64_t Now = time_get();
	if(m_LastHeartbeatTick == 0 || Now - m_LastHeartbeatTick > time_freq() * 5)
	{
		const bool SendDevAuth = g_Config.m_BcClientIndicatorSecretKey[0] != '\0';
		for(const int ClientId : m_RegisteredClientIds)
		{
			SendPresencePacket(ClientId, BestClientIndicator::PACKET_HEARTBEAT);
			SendVersionPacket(ClientId);
			if(SendDevAuth)
				SendDevAuthPacket(ClientId);
		}
		m_LastHeartbeatTick = Now;
		DebugLogF("heartbeat tick sent for %llu local clients", (unsigned long long)m_RegisteredClientIds.size());
	}

	m_WasPresenceEnabled = true;
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
	DebugLogF("browser cache loaded %llu entries", (unsigned long long)m_BrowserCache.Players().size());
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
	m_DeveloperClientIds.clear();
	m_FakeClientIds.clear();
	m_ClientVersions.clear();
	m_PresenceCache.Clear();
	m_aLastGameServerAddr[0] = '\0';
	m_aLastBlockedGameServerAddr[0] = '\0';
	m_aCachedGameServerAddr[0] = '\0';
	m_LastPresenceBlockReason.clear();
}

void CClientIndicator::ResetTokenState()
{
	ResetTokenTask();
	m_aWebSharedToken[0] = '\0';
}

void CClientIndicator::ReapplyBrowserSnapshot()
{
	ApplyBrowserSnapshot();
}

bool CClientIndicator::IsBrowserCacheRefreshing() const
{
	return m_pBrowserTask && !m_pBrowserTask->Done();
}

bool CClientIndicator::IsPresenceEnabled() const
{
	return Client()->State() == IClient::STATE_ONLINE;
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
