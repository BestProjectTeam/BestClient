/* Copyright © 2026 BestProject Team */
#include "snap_tap.h"

#include <base/str.h>
#include <base/time.h>

#include <engine/shared/config.h>
#include <engine/serverbrowser.h>

#include <game/client/gameclient.h>

void CSnapTap::OnReset()
{
	Reset(0);
	Reset(1);
}

void CSnapTap::Reset(int Dummy)
{
	m_aAppliedDirection[Dummy] = 0;
	m_aLastPressedDirection[Dummy] = 0;
	m_aLastPressedTime[Dummy] = 0;
	m_aPrevLeft[Dummy] = 0;
	m_aPrevRight[Dummy] = 0;
}

bool CSnapTap::IsActive() const
{
	return g_Config.m_BcSnapTap != 0 && !IsBlockedByCommunity();
}

void CSnapTap::UpdateState(int Dummy, bool LeftPressed, bool RightPressed)
{
	const int64_t Now = time_get();
	if(LeftPressed && !m_aPrevLeft[Dummy])
	{
		m_aLastPressedDirection[Dummy] = -1;
		m_aLastPressedTime[Dummy] = Now;
	}
	if(RightPressed && !m_aPrevRight[Dummy])
	{
		m_aLastPressedDirection[Dummy] = 1;
		m_aLastPressedTime[Dummy] = Now;
	}

	m_aPrevLeft[Dummy] = LeftPressed ? 1 : 0;
	m_aPrevRight[Dummy] = RightPressed ? 1 : 0;
}

int CSnapTap::ResolveMovementDirection(int Dummy, bool LeftPressed, bool RightPressed, bool ShouldUpdateState)
{
	if(ShouldUpdateState)
		UpdateState(Dummy, LeftPressed, RightPressed);

	return ResolveDirection(Dummy, LeftPressed, RightPressed);
}

int CSnapTap::ResolveDirection(int Dummy, bool LeftPressed, bool RightPressed)
{
	if(LeftPressed == RightPressed)
	{
		if(!LeftPressed)
		{
			m_aAppliedDirection[Dummy] = 0;
			return 0;
		}

		if(!IsActive())
			return 0;

		int CandidateDirection = m_aLastPressedDirection[Dummy];
		if(CandidateDirection != -1 && CandidateDirection != 1)
			CandidateDirection = m_aAppliedDirection[Dummy] != 0 ? m_aAppliedDirection[Dummy] : -1;

		if(m_aAppliedDirection[Dummy] == 0)
		{
			m_aAppliedDirection[Dummy] = CandidateDirection;
		}
		else if(m_aAppliedDirection[Dummy] != CandidateDirection)
		{
			const int64_t Delay = (time_freq() * (int64_t)g_Config.m_BcSnapTapDelay) / 1000;
			if(time_get() - m_aLastPressedTime[Dummy] >= Delay)
				m_aAppliedDirection[Dummy] = CandidateDirection;
		}

		return m_aAppliedDirection[Dummy];
	}

	m_aAppliedDirection[Dummy] = LeftPressed ? -1 : 1;
	return m_aAppliedDirection[Dummy];
}

bool CSnapTap::IsBlockedByCommunity() const
{
	auto IsBlockedGameType = [](const char *pGameType) -> bool {
		return pGameType != nullptr && pGameType[0] != '\0' &&
			(str_find_nocase(pGameType, "ddracenet") != nullptr ||
				str_find_nocase(pGameType, "0xf") != nullptr);
	};

	const char *pCommunityId = nullptr;

	const CServerInfo &ServerInfo = Client()->ServerInfo();
	if(ServerInfo.m_aCommunityId[0] != '\0')
		pCommunityId = ServerInfo.m_aCommunityId;
	else if(GameClient()->m_ConnectServerInfo.has_value() && GameClient()->m_ConnectServerInfo->m_aCommunityId[0] != '\0')
		pCommunityId = GameClient()->m_ConnectServerInfo->m_aCommunityId;

	const auto *pEntry = ServerBrowser()->Find(Client()->ServerAddress());
	if(pCommunityId == nullptr)
	{
		if(pEntry && pEntry->m_Info.m_aCommunityId[0] != '\0')
			pCommunityId = pEntry->m_Info.m_aCommunityId;
	}

	if(pCommunityId != nullptr && str_comp_nocase(pCommunityId, IServerBrowser::COMMUNITY_DDNET) == 0)
		return true;

	if(IsBlockedGameType(ServerInfo.m_aGameType))
		return true;
	if(GameClient()->m_ConnectServerInfo.has_value() && IsBlockedGameType(GameClient()->m_ConnectServerInfo->m_aGameType))
		return true;
	if(pEntry && IsBlockedGameType(pEntry->m_Info.m_aGameType))
		return true;
	if(IsBlockedGameType(GameClient()->m_GameInfo.m_aGameType))
		return true;

	return false;
}
