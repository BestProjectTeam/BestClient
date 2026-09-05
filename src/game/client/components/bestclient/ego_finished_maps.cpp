/* Copyright © 2026 BestProject Team */
#include "ego_finished_maps.h"

#include <base/system.h>

#include <engine/client.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/http.h>
#include <engine/shared/json.h>

namespace
{
bool IsEgoServer(const CServerInfo *pServer)
{
	return pServer &&
		(str_comp_nocase(pServer->m_aCommunityId, "ego") == 0 ||
			str_find_nocase(pServer->m_aGameType, "e-gores") != nullptr ||
			str_find_nocase(pServer->m_aGameType, "e_gores") != nullptr ||
			str_find_nocase(pServer->m_aName, "EGO |") != nullptr ||
			str_find_nocase(pServer->m_aName, "eternal-gores") != nullptr ||
			str_find_nocase(pServer->m_aName, "eternal gores") != nullptr);
}

bool ParseFinishedMaps(const json_value *pRoot, std::unordered_set<std::string> *pFinishedMaps)
{
	if(!pRoot || pRoot == &json_value_none || pRoot->type != json_object || !pFinishedMaps)
		return false;

	const json_value *pRecords = json_object_get(pRoot, "records");
	if(pRecords == &json_value_none || pRecords->type != json_array)
		return false;

	for(unsigned RecordIndex = 0; RecordIndex < pRecords->u.array.length; ++RecordIndex)
	{
		const json_value *pRecord = pRecords->u.array.values[RecordIndex];
		if(!pRecord || pRecord->type != json_object)
			continue;

		const json_value *pMapName = json_object_get(pRecord, "raw_map_name");
		if(pMapName == &json_value_none)
			pMapName = json_object_get(pRecord, "map_name");
		if(pMapName && pMapName != &json_value_none && pMapName->type == json_string && pMapName->u.string.ptr[0] != '\0')
			pFinishedMaps->emplace(pMapName->u.string.ptr);
	}

	return true;
}
}

bool CEgoFinishedMaps::Enabled() const
{
	return g_Config.m_BcShowFinishedMapOnEgo != 0;
}

bool CEgoFinishedMaps::IsFinishedMap(const CServerInfo *pServer) const
{
	return Enabled() && IsEgoServer(pServer) && m_FinishedMaps.contains(pServer->m_aMap);
}

void CEgoFinishedMaps::ResetRequest()
{
	if(m_pTask)
		m_pTask->Abort();
	m_pTask.reset();
}

void CEgoFinishedMaps::OnReset()
{
	ResetRequest();
}

void CEgoFinishedMaps::StartRequest()
{
	char aEscapedPlayerName[256];
	EscapeUrl(aEscapedPlayerName, sizeof(aEscapedPlayerName), m_aPlayerName);

	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "https://eternal-gores.com/api/profiles/by-nick/%s", aEscapedPlayerName);

	m_pTask = HttpGet(aUrl);
	m_pTask->Timeout(CTimeout{8000, 0, 500, 5});
	m_pTask->LogProgress(HTTPLOG::ALL);
	m_pTask->FailOnErrorStatus(false);
	Http()->Run(m_pTask);
}

void CEgoFinishedMaps::ProcessRequest()
{
	if(!m_pTask || !m_pTask->Done())
		return;

	bool Success = false;
	if(m_pTask->State() == EHttpState::DONE && m_pTask->StatusCode() == 200)
	{
		json_value *pRoot = m_pTask->ResultJson();
		std::unordered_set<std::string> FinishedMaps;
		Success = ParseFinishedMaps(pRoot, &FinishedMaps);
		if(Success)
		{
			m_FinishedMaps = std::move(FinishedMaps);
			str_copy(m_aLoadedPlayerName, m_aPlayerName, sizeof(m_aLoadedPlayerName));
		}
		if(pRoot)
			json_value_free(pRoot);
	}

	if(!Success)
		m_NextRetry = time_get() + RETRY_DELAY_SECONDS * time_freq();
	m_pTask.reset();
}

void CEgoFinishedMaps::OnUpdate()
{
	if(!Enabled())
	{
		ResetRequest();
		m_FinishedMaps.clear();
		m_aPlayerName[0] = '\0';
		m_aLoadedPlayerName[0] = '\0';
		m_NextRetry = 0;
		return;
	}

	const char *pPlayerName = Client()->PlayerName();
	if(str_comp(m_aPlayerName, pPlayerName) != 0)
	{
		ResetRequest();
		m_FinishedMaps.clear();
		m_aLoadedPlayerName[0] = '\0';
		m_NextRetry = 0;
		str_copy(m_aPlayerName, pPlayerName, sizeof(m_aPlayerName));
	}

	ProcessRequest();

	if(!m_aPlayerName[0] || m_pTask || str_comp(m_aPlayerName, m_aLoadedPlayerName) == 0 || time_get() < m_NextRetry)
		return;

	StartRequest();
}
