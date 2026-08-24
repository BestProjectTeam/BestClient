/* Copyright © 2026 BestProject Team */
#include "show_points.h"

#include <base/system.h>

#include <engine/client.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/http.h>
#include <engine/shared/json.h>

#include <game/client/gameclient.h>

namespace
{
bool IsJsonObject(const json_value *pValue)
{
	return pValue && pValue != &json_value_none && pValue->type == json_object;
}

bool ParsePoints(CShowPoints::EProvider Provider, const json_value *pRoot, int *pPoints)
{
	if(!IsJsonObject(pRoot) || !pPoints)
		return false;

	if(Provider == CShowPoints::EProvider::Ego)
	{
		const json_value *pPointsVal = json_object_get(pRoot, "points");
		if(pPointsVal == &json_value_none)
			return false;
		if(pPointsVal->type == json_integer)
		{
			*pPoints = (int)pPointsVal->u.integer;
			return true;
		}
		if(pPointsVal->type == json_double)
		{
			*pPoints = (int)pPointsVal->u.dbl;
			return true;
		}
		return false;
	}

	const json_value *pPointsObj = json_object_get(pRoot, "points");
	if(!IsJsonObject(pPointsObj))
		return false;

	const json_value *pPointsVal = json_object_get(pPointsObj, "points");
	if(pPointsVal == &json_value_none)
		return false;
	if(pPointsVal->type == json_integer)
	{
		*pPoints = (int)pPointsVal->u.integer;
		return true;
	}
	if(pPointsVal->type == json_double)
	{
		*pPoints = (int)pPointsVal->u.dbl;
		return true;
	}
	return false;
}
}

bool CShowPoints::Enabled() const
{
	return g_Config.m_BcShowPointsInTab != 0;
}

int CShowPoints::ProviderIndex(EProvider Provider)
{
	return Provider == EProvider::Ego ? 1 : 0;
}

const char *CShowPoints::CurrentCommunityId() const
{
	m_aCommunityIdBuf[0] = '\0';

	CServerInfo ServerInfo;
	mem_zero(&ServerInfo, sizeof(ServerInfo));
	Client()->GetServerInfo(&ServerInfo);

	if(ServerInfo.m_aCommunityId[0] != '\0')
	{
		str_copy(m_aCommunityIdBuf, ServerInfo.m_aCommunityId, sizeof(m_aCommunityIdBuf));
		return m_aCommunityIdBuf;
	}

	if(GameClient()->m_ConnectServerInfo.has_value() && GameClient()->m_ConnectServerInfo->m_aCommunityId[0] != '\0')
	{
		str_copy(m_aCommunityIdBuf, GameClient()->m_ConnectServerInfo->m_aCommunityId, sizeof(m_aCommunityIdBuf));
		return m_aCommunityIdBuf;
	}

	const IServerBrowser::CServerEntry *pEntry = ServerBrowser()->Find(Client()->ServerAddress());
	if(pEntry && pEntry->m_Info.m_aCommunityId[0] != '\0')
	{
		str_copy(m_aCommunityIdBuf, pEntry->m_Info.m_aCommunityId, sizeof(m_aCommunityIdBuf));
		return m_aCommunityIdBuf;
	}

	return m_aCommunityIdBuf;
}

CShowPoints::EProvider CShowPoints::CurrentProvider() const
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return EProvider::None;

	const char *pCommunityId = CurrentCommunityId();
	if(pCommunityId[0] != '\0')
	{
		if(str_comp_nocase(pCommunityId, IServerBrowser::COMMUNITY_DDNET) == 0)
			return EProvider::Ddnet;
		if(str_comp_nocase(pCommunityId, "ego") == 0)
			return EProvider::Ego;
	}

	CServerInfo ServerInfo;
	mem_zero(&ServerInfo, sizeof(ServerInfo));
	Client()->GetServerInfo(&ServerInfo);
	// Avoid matching unrelated names that merely contain "ego" (e.g. "Diego").
	if(str_find_nocase(ServerInfo.m_aName, "EGO |") ||
		str_find_nocase(ServerInfo.m_aName, "eternal-gores") ||
		str_find_nocase(ServerInfo.m_aName, "eternal gores"))
	{
		return EProvider::Ego;
	}

	return EProvider::None;
}

bool CShowPoints::ActiveOnCurrentServer() const
{
	return Enabled() && CurrentProvider() != EProvider::None;
}

void CShowPoints::MakeLowerAscii(char *pBuf, int Size, const char *pSrc)
{
	if(!pBuf || Size <= 0)
		return;

	int Out = 0;
	for(int i = 0; pSrc && pSrc[i] && Out + 1 < Size; i++)
	{
		const unsigned char C = (unsigned char)pSrc[i];
		if(C >= 'A' && C <= 'Z')
			pBuf[Out++] = (char)(C - 'A' + 'a');
		else
			pBuf[Out++] = (char)C;
	}
	pBuf[Out] = '\0';
}

bool CShowPoints::IsCacheFresh(const SCacheEntry &Entry) const
{
	const int64_t AgeMs = (time_get() - Entry.m_FetchedAt) * 1000 / time_freq();
	if(Entry.m_Failed)
		return AgeMs < FAILURE_TTL_MS;
	return AgeMs < SUCCESS_TTL_MS;
}

bool CShowPoints::IsQueuedOrInFlight(const std::string &Name) const
{
	for(const std::string &Queued : m_Queue)
	{
		if(Queued == Name)
			return true;
	}
	for(const SInFlight &InFlight : m_vInFlight)
	{
		if(InFlight.m_Name == Name)
			return true;
	}
	return false;
}

bool CShowPoints::TryGetPoints(const char *pName, int *pPoints) const
{
	if(!pName || !pName[0] || !pPoints)
		return false;

	const EProvider Provider = CurrentProvider();
	if(Provider == EProvider::None)
		return false;

	const auto &Cache = m_aCache[ProviderIndex(Provider)];
	const auto It = Cache.find(pName);
	if(It == Cache.end())
		return false;
	if(!IsCacheFresh(It->second) || It->second.m_Failed || !It->second.m_HasPoints)
		return false;

	*pPoints = It->second.m_Points;
	return true;
}

void CShowPoints::RequestPoints(const char *pName)
{
	if(!pName || !pName[0] || !Enabled())
		return;

	const EProvider Provider = CurrentProvider();
	if(Provider == EProvider::None)
		return;

	const auto &Cache = m_aCache[ProviderIndex(Provider)];
	const auto It = Cache.find(pName);
	if(It != Cache.end() && IsCacheFresh(It->second))
		return;

	if(IsQueuedOrInFlight(pName))
		return;

	if((int)m_Queue.size() >= MAX_QUEUE)
		return;

	m_Queue.emplace_back(pName);
}

void CShowPoints::ClearPending()
{
	for(SInFlight &InFlight : m_vInFlight)
	{
		if(InFlight.m_pTask)
			InFlight.m_pTask->Abort();
	}
	m_vInFlight.clear();
	m_Queue.clear();
}

void CShowPoints::OnReset()
{
	ClearPending();
}

void CShowPoints::OnStateChange(int NewState, int OldState)
{
	(void)OldState;
	if(NewState == IClient::STATE_OFFLINE || NewState == IClient::STATE_QUITTING || NewState == IClient::STATE_RESTARTING)
	{
		ClearPending();
		m_LastProvider = EProvider::None;
	}
}

void CShowPoints::StartRequest(const std::string &Name, EProvider Provider)
{
	char aUrl[512];
	if(Provider == EProvider::Ego)
	{
		char aLower[64];
		MakeLowerAscii(aLower, sizeof(aLower), Name.c_str());
		char aEscapedLower[256];
		EscapeUrl(aEscapedLower, sizeof(aEscapedLower), aLower);
		str_format(aUrl, sizeof(aUrl), "https://eternal-gores.com/profile/%s.json", aEscapedLower);
	}
	else
	{
		char aEscaped[256];
		EscapeUrl(aEscaped, sizeof(aEscaped), Name.c_str());
		str_format(aUrl, sizeof(aUrl), "https://ru.ddnet.org/players/?json2=%s", aEscaped);
	}

	std::shared_ptr<CHttpRequest> pReq = HttpGet(aUrl);
	pReq->Timeout(CTimeout{8000, 0, 500, 5});
	pReq->LogProgress(HTTPLOG::FAILURE);
	pReq->FailOnErrorStatus(false);

	SInFlight Job;
	Job.m_Name = Name;
	Job.m_Provider = Provider;
	Job.m_pTask = std::move(pReq);
	Http()->Run(Job.m_pTask);
	m_vInFlight.push_back(std::move(Job));
}

void CShowPoints::ProcessInFlight()
{
	for(size_t i = 0; i < m_vInFlight.size();)
	{
		SInFlight &Job = m_vInFlight[i];
		if(!Job.m_pTask || !Job.m_pTask->Done())
		{
			++i;
			continue;
		}

		SCacheEntry Entry;
		Entry.m_FetchedAt = time_get();

		if(Job.m_pTask->State() == EHttpState::DONE && Job.m_pTask->StatusCode() == 200)
		{
			json_value *pRoot = Job.m_pTask->ResultJson();
			int Points = 0;
			if(pRoot && ParsePoints(Job.m_Provider, pRoot, &Points))
			{
				Entry.m_HasPoints = true;
				Entry.m_Points = Points;
			}
			else
			{
				Entry.m_Failed = true;
			}
			if(pRoot)
				json_value_free(pRoot);
		}
		else
		{
			Entry.m_Failed = true;
		}

		m_aCache[ProviderIndex(Job.m_Provider)][Job.m_Name] = Entry;
		m_vInFlight.erase(m_vInFlight.begin() + i);
	}
}

void CShowPoints::PumpQueue()
{
	const EProvider Provider = CurrentProvider();
	if(Provider == EProvider::None)
	{
		ClearPending();
		m_LastProvider = EProvider::None;
		return;
	}

	// Only clear when switching between real providers — not on first activation
	// (m_LastProvider starts as None and would wipe a freshly filled queue).
	if(m_LastProvider != EProvider::None && m_LastProvider != Provider)
		ClearPending();
	m_LastProvider = Provider;

	while((int)m_vInFlight.size() < MAX_CONCURRENT && !m_Queue.empty())
	{
		const std::string Name = std::move(m_Queue.front());
		m_Queue.pop_front();

		const auto &Cache = m_aCache[ProviderIndex(Provider)];
		const auto It = Cache.find(Name);
		if(It != Cache.end() && IsCacheFresh(It->second))
			continue;
		if(IsQueuedOrInFlight(Name))
			continue;

		StartRequest(Name, Provider);
	}
}

void CShowPoints::OnUpdate()
{
	if(!Enabled())
	{
		ClearPending();
		return;
	}

	if(CurrentProvider() == EProvider::None)
	{
		ClearPending();
		m_LastProvider = EProvider::None;
		return;
	}

	ProcessInFlight();

	// Only dequeue while the scoreboard is open — avoids background spam.
	if(GameClient()->m_Scoreboard.IsActive())
		PumpQueue();
}
