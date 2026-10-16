#include "legit_finished_maps.h"

#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/http.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>

namespace
{
enum class ELegitMode
{
	NONE,
	GORES,
	DDRACE,
};

ELegitMode LegitMode(const CServerInfo *pServer)
{
	if(!pServer || (str_comp_nocase(pServer->m_aCommunityId, "legit") != 0 && str_find_nocase(pServer->m_aName, "Legit Network") == nullptr))
		return ELegitMode::NONE;

	if(str_find_nocase(pServer->m_aGameType, "Gores") != nullptr || str_find_nocase(pServer->m_aName, "Gores") != nullptr)
		return ELegitMode::GORES;
	if(str_find_nocase(pServer->m_aGameType, "DDraceNetwork") != nullptr)
		return ELegitMode::DDRACE;
	return ELegitMode::NONE;
}

bool ParseMapArray(const json_value *pRoot, const char *pKey, std::unordered_set<std::string> *pMaps)
{
	const json_value *pArray = json_object_get(pRoot, pKey);
	if(pArray == &json_value_none || pArray->type != json_array)
		return false;

	for(unsigned i = 0; i < pArray->u.array.length; ++i)
	{
		const json_value *pMap = pArray->u.array.values[i];
		if(pMap && pMap->type == json_string && pMap->u.string.ptr[0] != '\0')
			pMaps->emplace(pMap->u.string.ptr);
	}
	return true;
}

bool ParseFinishedMaps(const json_value *pRoot, std::unordered_set<std::string> *pGoresMaps, std::unordered_set<std::string> *pDDraceMaps)
{
	if(!pRoot || pRoot == &json_value_none || pRoot->type != json_object)
		return false;
	return ParseMapArray(pRoot, "gores", pGoresMaps) && ParseMapArray(pRoot, "ddrace", pDDraceMaps);
}
}

bool CLegitFinishedMaps::Enabled() const
{
	return g_Config.m_BcShowFinishedMapOnLegit != 0;
}

bool CLegitFinishedMaps::IsFinishedMap(const CServerInfo *pServer) const
{
	switch(LegitMode(pServer))
	{
	case ELegitMode::GORES:
		return Enabled() && m_GoresFinishedMaps.contains(pServer->m_aMap);
	case ELegitMode::DDRACE:
		return Enabled() && m_DDraceFinishedMaps.contains(pServer->m_aMap);
	case ELegitMode::NONE:
		return false;
	}
	return false;
}

void CLegitFinishedMaps::ResetRequest()
{
	if(m_pTask)
		m_pTask->Abort();
	m_pTask.reset();
}

void CLegitFinishedMaps::OnReset()
{
	ResetRequest();
}

void CLegitFinishedMaps::StartRequest()
{
	char aEscapedPlayerName[256];
	EscapeUrl(aEscapedPlayerName, sizeof(aEscapedPlayerName), m_aPlayerName);

	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "https://legit.tw/api_finished_maps.php?name=%s", aEscapedPlayerName);

	m_pTask = HttpGet(aUrl);
	m_pTask->Timeout(CTimeout{8000, 0, 500, 5});
	m_pTask->LogProgress(HTTPLOG::FAILURE);
	m_pTask->FailOnErrorStatus(false);
	Http()->Run(m_pTask);
}

void CLegitFinishedMaps::ProcessRequest()
{
	if(!m_pTask || !m_pTask->Done())
		return;

	bool Success = false;
	if(m_pTask->State() == EHttpState::DONE && m_pTask->StatusCode() == 200)
	{
		json_value *pRoot = m_pTask->ResultJson();
		std::unordered_set<std::string> GoresMaps;
		std::unordered_set<std::string> DDraceMaps;
		Success = ParseFinishedMaps(pRoot, &GoresMaps, &DDraceMaps);
		if(Success)
		{
			m_GoresFinishedMaps = std::move(GoresMaps);
			m_DDraceFinishedMaps = std::move(DDraceMaps);
			str_copy(m_aLoadedPlayerName, m_aPlayerName, sizeof(m_aLoadedPlayerName));
		}
		if(pRoot)
			json_value_free(pRoot);
	}

	if(!Success)
		m_NextRetry = time_get() + RETRY_DELAY_SECONDS * time_freq();
	m_pTask.reset();
}

void CLegitFinishedMaps::OnUpdate()
{
	if(!Enabled())
	{
		ResetRequest();
		m_GoresFinishedMaps.clear();
		m_DDraceFinishedMaps.clear();
		m_aPlayerName[0] = '\0';
		m_aLoadedPlayerName[0] = '\0';
		m_NextRetry = 0;
		return;
	}

	const char *pPlayerName = Client()->PlayerName();
	if(str_comp(m_aPlayerName, pPlayerName) != 0)
	{
		ResetRequest();
		m_GoresFinishedMaps.clear();
		m_DDraceFinishedMaps.clear();
		m_aLoadedPlayerName[0] = '\0';
		m_NextRetry = 0;
		str_copy(m_aPlayerName, pPlayerName, sizeof(m_aPlayerName));
	}

	ProcessRequest();

	if(!m_aPlayerName[0] || m_pTask || str_comp(m_aPlayerName, m_aLoadedPlayerName) == 0 || time_get() < m_NextRetry)
		return;

	StartRequest();
}
