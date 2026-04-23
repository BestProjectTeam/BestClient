/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_CLIENTINDICATOR_CLIENT_INDICATOR_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_CLIENTINDICATOR_CLIENT_INDICATOR_H

#include "client_indicator_sync.h"
#include "../subsystem_runtime.h"
#include "browser_cache.h"
#include "presence_cache.h"

#include <base/net.h>

#include <game/client/component.h>

#include <engine/shared/http.h>
#include <engine/shared/uuid_manager.h>

#include <memory>
#include <string>
#include <unordered_set>

class CClientIndicator : public CComponent
{
public:
	CClientIndicator();
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnReset() override;
	void OnUpdate() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnShutdown() override;

	bool IsPlayerBestClient(int ClientId) const;
	bool IsPlayerBClient(int ClientId) { return IsPlayerBestClient(ClientId); }
	bool IsPlayerDeveloper(int ClientId) const;

	void RefreshBrowserCache(bool Force);
	void RefreshToken(bool Force);
	void ReapplyBrowserSnapshot();

private:
	NETSOCKET m_Socket = nullptr;
	NETADDR m_ServerAddr{};
	bool m_HasServerAddr = false;
	char m_aLastPresenceServerAddr[256] = "";
	char m_aLastGameServerAddr[NETADDR_MAXSTRSIZE] = "";
	char m_aLastBlockedGameServerAddr[NETADDR_MAXSTRSIZE] = "";
	bool m_WasPresenceEnabled = false;
	ESubsystemRuntimeState m_RuntimeState = ESubsystemRuntimeState::DISABLED;
	int64_t m_LastHeartbeatTick = 0;
	int64_t m_LastPresenceStartAttempt = 0;
	int64_t m_LastBrowserRefreshTick = 0;
	int64_t m_LastTokenRefreshTick = 0;
	int64_t m_NextPresenceBrowserRefreshTick = 0;
	int64_t m_LastPresencePollTick = 0;
	int64_t m_LastRegistrationSyncTick = 0;
	int64_t m_LastPerfReportTick = 0;
	int64_t m_LastUpdateCostTick = 0;
	int64_t m_MaxUpdateCostTick = 0;
	int64_t m_TotalUpdateCostTick = 0;
	int64_t m_UpdateSamples = 0;
	CUuid m_ClientInstanceId = UUID_ZEROED;
	std::unordered_set<int> m_RegisteredClientIds;
	std::unordered_set<int> m_DeveloperClientIds;
	CPresenceCache m_PresenceCache;

	std::shared_ptr<CHttpRequest> m_pBrowserTask = nullptr;
	std::shared_ptr<CHttpRequest> m_pTokenTask = nullptr;
	CBrowserCache m_BrowserCache;
	char m_aWebSharedToken[256] = "";
	std::string m_LastPresenceBlockReason;

	void OpenPresenceSocket();
	void ClosePresenceSocket();
	void StopPresence(bool SendLeavePackets);
	void EnsurePresenceSocket();
	void UpdatePresence();
	void ProcessIncomingPackets(bool Force = false);
	void SyncLocalRegistrations(bool Force = false);
	void SendPresencePacket(int ClientId, int PacketType);
	void SendDevAuthPacket(int ClientId);
	void SendLeaveForAll();
	const char *CurrentGameServerAddress();
	const char *PlayerNameForClient(int ClientId) const;

	void FinishBrowserCacheRefresh();
	void ResetBrowserTask();
	void FinishTokenRefresh();
	void ResetTokenTask();
	void ResetPresenceState();
	void ResetTokenState();
	void ClearBrowserSnapshot();
	void ApplyBrowserSnapshot();
	void SchedulePresenceBrowserRefresh();
	bool HasPendingNetworkTask() const;
	bool IsBrowserSnapshotEnabled() const;
	bool IsPresenceEnabled() const;
	const char *EffectiveSharedToken() const;
	void DebugLog(const char *pText) const;
	void DebugLogF(const char *pFormat, ...) const;
	void SetPresenceBlockReason(const char *pReason);
	void ClearPresenceBlockReason();
};

#endif
