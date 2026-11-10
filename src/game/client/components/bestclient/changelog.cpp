/* Copyright © 2026 BestProject Team */
#include "changelog.h"

#include <base/str.h>
#include <base/time.h>

#include <engine/http.h>
#include <engine/shared/json.h>

#include <game/version.h>

static constexpr int CHANGELOG_MAX_BODY = 8000;

static void TruncateUtf8(std::string &Text, size_t MaxBytes)
{
	if(Text.size() <= MaxBytes)
		return;

	Text.resize(MaxBytes);
	while(!Text.empty() && (static_cast<unsigned char>(Text.back()) & 0xC0) == 0x80)
		Text.pop_back();
	if(!Text.empty() && (static_cast<unsigned char>(Text.back()) & 0xC0) == 0xC0)
		Text.pop_back();
}

static bool SameReleaseLabel(const char *pLeft, const char *pRight)
{
	if(!pLeft)
		pLeft = "";
	if(!pRight)
		pRight = "";
	while(*pLeft == ' ' || *pLeft == '\t')
		++pLeft;
	while(*pRight == ' ' || *pRight == '\t')
		++pRight;
	if((pLeft[0] == 'v' || pLeft[0] == 'V') && pLeft[1] >= '0' && pLeft[1] <= '9')
		++pLeft;
	if((pRight[0] == 'v' || pRight[0] == 'V') && pRight[1] >= '0' && pRight[1] <= '9')
		++pRight;
	return str_comp_nocase(pLeft, pRight) == 0;
}

static void FormatReleaseDate(const char *pIso, std::string &Date)
{
	Date.clear();
	if(!pIso || str_length(pIso) < 10)
		return;
	if(pIso[4] != '-' || pIso[7] != '-')
		return;
	Date.assign(pIso, 10);
}

static std::string ReleaseBody(const json_value *pBody)
{
	if(!pBody || pBody->type != json_string)
		return {};

	const char *pText = json_string_get(pBody);
	if(!pText)
		return {};

	std::string Body;
	Body.reserve(str_length(pText));
	for(; *pText != '\0'; ++pText)
	{
		if(*pText != '\r')
			Body.push_back(*pText);
	}
	TruncateUtf8(Body, CHANGELOG_MAX_BODY);
	return Body;
}

static const char *JsonString(const json_value *pValue)
{
	if(!pValue || pValue->type != json_string)
		return nullptr;
	return json_string_get(pValue);
}

static bool ParseRelease(const json_value *pJson, CChangelog::CRelease &Release, bool Latest)
{
	if(!pJson || pJson->type != json_object)
		return false;

	const json_value *pDraft = json_object_get(pJson, "draft");
	if(pDraft && pDraft->type == json_boolean && json_boolean_get(pDraft))
		return false;

	const char *pTag = JsonString(json_object_get(pJson, "tag_name"));
	const char *pName = JsonString(json_object_get(pJson, "name"));
	if((!pTag || pTag[0] == '\0') && (!pName || pName[0] == '\0'))
		return false;

	Release = {};
	Release.m_Tag = pTag && pTag[0] != '\0' ? pTag : pName;
	if(pName && pName[0] != '\0' && !SameReleaseLabel(pName, Release.m_Tag.c_str()))
		Release.m_Title = pName;
	FormatReleaseDate(JsonString(json_object_get(pJson, "published_at")), Release.m_Date);
	Release.m_Body = ReleaseBody(json_object_get(pJson, "body"));
	const char *pUrl = JsonString(json_object_get(pJson, "html_url"));
	if(pUrl)
		Release.m_Url = pUrl;

	const json_value *pPrerelease = json_object_get(pJson, "prerelease");
	Release.m_Prerelease = pPrerelease && pPrerelease->type == json_boolean && json_boolean_get(pPrerelease);
	Release.m_Latest = Latest;
	return true;
}

void CChangelog::Update()
{
	m_OpenLastFrame = m_OpenThisFrame;
	m_OpenThisFrame = false;

	if(!m_pTask || !m_pTask->Done())
		return;

	std::shared_ptr<IHttpRequest> pTask = std::move(m_pTask);
	m_Loading = false;
	if(pTask->State() == EHttpState::ABORTED)
		return;

	if(pTask->State() != EHttpState::DONE)
	{
		m_Error = "Couldn't load releases";
		return;
	}

	const int Status = pTask->StatusCode();
	if(Status == 403 || Status == 429)
	{
		m_Error = "GitHub rate limit reached";
		return;
	}
	if(Status != 200)
	{
		m_Error = "Couldn't load releases";
		return;
	}

	json_value *pJson = pTask->ResultJson();
	if(!pJson || pJson->type != json_array)
	{
		if(pJson)
			json_value_free(pJson);
		m_Error = "Couldn't parse releases";
		return;
	}

	std::vector<CRelease> vReleases;
	for(int i = 0; i < json_array_length(pJson) && (int)vReleases.size() < MAX_RELEASES; ++i)
	{
		CRelease Release;
		if(ParseRelease(json_array_get(pJson, i), Release, vReleases.empty()))
			vReleases.push_back(std::move(Release));
	}
	json_value_free(pJson);

	m_vReleases = std::move(vReleases);
	m_Error.clear();
}

void CChangelog::Shutdown()
{
	if(m_pTask)
	{
		m_pTask->Abort();
		m_pTask = nullptr;
	}
	m_Loading = false;
}

void CChangelog::Start(IHttp *pHttp)
{
	if(!pHttp)
		return;
	if(m_pTask && m_pTask->Done())
		Update();
	if(m_pTask)
	{
		m_pTask->Abort();
		m_pTask = nullptr;
	}

	m_Loading = true;
	m_Error.clear();
	m_LastFetchTime = time_get();

	char aUrl[192];
	str_format(aUrl, sizeof(aUrl), "https://api.github.com/repos/BestProjectTeam/BestClient/releases?per_page=%d&t=%lld", MAX_RELEASES, (long long)time_timestamp());
	m_pTask = HttpGet(aUrl);
	m_pTask->HeaderString("Accept", "application/vnd.github+json");
	m_pTask->HeaderString("User-Agent", CLIENT_NAME);
	m_pTask->HeaderString("X-GitHub-Api-Version", "2022-11-28");
	m_pTask->HeaderString("Cache-Control", "no-cache");
	m_pTask->HeaderString("Pragma", "no-cache");
	m_pTask->Timeout(CTimeout{10000, 0, 500, 10});
	m_pTask->IpResolve(IPRESOLVE::V4);
	m_pTask->FailOnErrorStatus(false);
	m_pTask->MaxResponseSize(2 * 1024 * 1024);
	m_pTask->LogProgress(HTTPLOG::FAILURE);
	pHttp->Run(m_pTask);
}

void CChangelog::NotifyPageOpen(IHttp *pHttp)
{
	const bool Entered = !m_OpenLastFrame && !m_OpenThisFrame;
	m_OpenThisFrame = true;
	if(!Entered)
		return;
	if(m_pTask && !m_pTask->Done())
		return;
	if(m_LastFetchTime != 0 && time_get() - m_LastFetchTime < time_freq() * 20)
		return;
	Start(pHttp);
}

void CChangelog::RequestRefresh(IHttp *pHttp)
{
	if(m_pTask && !m_pTask->Done())
		return;
	Start(pHttp);
}
