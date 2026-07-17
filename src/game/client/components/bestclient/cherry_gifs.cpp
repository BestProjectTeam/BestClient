/* Copyright © 2026 BestProject Team */
#include "cherry_gifs.h"
#include "cherry_gifs_cache.h"

#include <base/log.h>
#include <base/mem.h>

#include <engine/shared/http.h>
#include <engine/shared/json.h>
#include <engine/storage.h>

#include <game/client/components/media_decoder.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <utility>

static constexpr const char *CHERRYGIFS_DEFAULT_ENDPOINT = "https://gifs.teeworlds.xyz";
static constexpr const char *CHERRYGIFS_API_KEY = "cg_qivUPzQzcHCDPos1246UsRUXlARb1Hk4";
static constexpr const char *CHERRYGIFS_HOST = "gifs.teeworlds.xyz";
static constexpr int CHERRYGIFS_LIST_LIMIT = 30;
static constexpr float CHERRYGIFS_SEARCH_DEBOUNCE = 0.4f;
static constexpr float CHERRYGIFS_LIST_MIN_INTERVAL = 0.5f;
static constexpr int CHERRYGIFS_MAX_CONCURRENT_THUMBNAILS = 4;
// Defense in depth against a compromised/malicious API response: even though we only ever ask
// for CHERRYGIFS_LIST_LIMIT items, a hostile server could ignore that and send a huge array or
// body to burn memory/CPU on the client. These caps bound the damage regardless of what it sends.
static constexpr int64_t CHERRYGIFS_LIST_MAX_RESPONSE_BYTES = 4 * 1024 * 1024;
static constexpr int CHERRYGIFS_MAX_RESULTS_PER_RESPONSE = 100;
static constexpr int CHERRYGIFS_MAX_TAGS_PER_GIF = 16;
// Hard ceiling on accumulated in-memory results across repeated "Load more" clicks, so a server
// that just keeps claiming hasMore=true can't bait an open-ended session into unbounded growth.
static constexpr int CHERRYGIFS_MAX_TOTAL_RESULTS = 500;
// The nsfw-by-url cache outlives individual searches (that's the point), so it needs its own,
// more generous cap to bound a very long browsing session.
static constexpr int CHERRYGIFS_MAX_NSFW_CACHE_ENTRIES = 5000;

// The API response is not trusted to only ever point back at itself - a compromised or malicious
// CherryGifs backend could return a "gif" url pointing anywhere (internal network, some exploit
// host, etc.) and we'd otherwise happily fetch and decode whatever's there. Every url that comes
// out of a parsed response is checked against this before we ever issue a follow-up request for
// it, so the only host this client will ever download gif bytes from is the one it already trusts
// with the API key.
// Downloaded bytes are never trusted just because the host/response-status checked out - format
// is verified by magic bytes before anything is handed to the image decoder. Anything that isn't
// a recognized still-image container (whatever the server claims its Content-Type is, whatever
// the url's extension is) is rejected outright rather than passed to the decoder as a "maybe".
static bool IsGifSignature(const unsigned char *pData, size_t DataSize)
{
	return DataSize >= 6 && (mem_comp(pData, "GIF87a", 6) == 0 || mem_comp(pData, "GIF89a", 6) == 0);
}

static bool IsPngSignature(const unsigned char *pData, size_t DataSize)
{
	static const unsigned char s_aPngSig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
	return DataSize >= 8 && mem_comp(pData, s_aPngSig, 8) == 0;
}

static bool IsJpegSignature(const unsigned char *pData, size_t DataSize)
{
	return DataSize >= 3 && pData[0] == 0xff && pData[1] == 0xd8 && pData[2] == 0xff;
}

static bool IsWebpSignature(const unsigned char *pData, size_t DataSize)
{
	return DataSize >= 12 && mem_comp(pData, "RIFF", 4) == 0 && mem_comp(pData + 8, "WEBP", 4) == 0;
}

static bool IsBmpSignature(const unsigned char *pData, size_t DataSize)
{
	return DataSize >= 2 && pData[0] == 'B' && pData[1] == 'M';
}

static bool IsRecognizedImageSignature(const unsigned char *pData, size_t DataSize)
{
	return IsGifSignature(pData, DataSize) || IsPngSignature(pData, DataSize) || IsJpegSignature(pData, DataSize) || IsWebpSignature(pData, DataSize) || IsBmpSignature(pData, DataSize);
}

static bool IsCherryGifsUrl(const char *pUrl)
{
	const char *pHost = str_startswith(pUrl, "https://");
	if(!pHost)
		return false;
	const char *pAfterHost = str_startswith(pHost, CHERRYGIFS_HOST);
	if(!pAfterHost)
		return false;
	return *pAfterHost == '\0' || *pAfterHost == '/' || *pAfterHost == ':' || *pAfterHost == '?';
}

static void UrlEncodeQuery(const char *pText, char *pOut, size_t Size)
{
	if(Size == 0)
		return;
	size_t OutPos = 0;
	for(const char *p = pText; *p && OutPos < Size - 1; ++p)
	{
		const unsigned char c = (unsigned char)*p;
		if(isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
		{
			pOut[OutPos++] = (char)c;
		}
		else if(OutPos + 3 < Size)
		{
			snprintf(pOut + OutPos, 4, "%%%02X", c);
			OutPos += 3;
		}
		else
		{
			break;
		}
	}
	pOut[OutPos] = '\0';
}

void CCherryGifs::OnConsoleInit()
{
	Console()->Register("cherrygifs_test_list", "", CFGFLAG_CLIENT, ConCherryGifsTestList, this, "Debug: fetch the first page of the CherryGifs list and log it");
}

void CCherryGifs::ConCherryGifsTestList(IConsole::IResult *pResult, void *pUserData)
{
	CCherryGifs *pThis = static_cast<CCherryGifs *>(pUserData);
	log_info("cherrygifs", "Requesting first page (query='', sort=new, nsfw=exclude)...");
	pThis->StartListRequest(true);
}

void CCherryGifs::SetFilters(const char *pQuery, bool SortTop, bool IncludeNsfw)
{
	if(!m_Initialized)
	{
		m_Initialized = true;
		str_copy(m_aAppliedQuery, pQuery);
		m_AppliedSortTop = SortTop;
		m_AppliedIncludeNsfw = IncludeNsfw;
		str_copy(m_aPendingQuery, pQuery);
		m_PendingSortTop = SortTop;
		m_PendingIncludeNsfw = IncludeNsfw;
		m_PendingChangeTimer = -1.0f;
		StartListRequest(true);
		return;
	}

	if(str_comp(pQuery, m_aAppliedQuery) == 0 && SortTop == m_AppliedSortTop && IncludeNsfw == m_AppliedIncludeNsfw)
	{
		m_PendingChangeTimer = -1.0f;
		return;
	}

	// Only (re)start the debounce timer when the pending values actually change - SetFilters is
	// called every frame from the UI, so unconditionally resetting it here would mean it never
	// reaches CHERRYGIFS_SEARCH_DEBOUNCE and the filters would never actually get applied.
	if(str_comp(pQuery, m_aPendingQuery) != 0 || SortTop != m_PendingSortTop || IncludeNsfw != m_PendingIncludeNsfw)
	{
		str_copy(m_aPendingQuery, pQuery);
		m_PendingSortTop = SortTop;
		m_PendingIncludeNsfw = IncludeNsfw;
		m_PendingChangeTimer = 0.0f;
	}
}

void CCherryGifs::ApplyFilters()
{
	str_copy(m_aAppliedQuery, m_aPendingQuery);
	m_AppliedSortTop = m_PendingSortTop;
	m_AppliedIncludeNsfw = m_PendingIncludeNsfw;
	m_PendingChangeTimer = -1.0f;
	StartListRequest(true);
}

void CCherryGifs::LoadMore()
{
	if(m_Loading || !m_HasMore || (int)m_vResults.size() >= CHERRYGIFS_MAX_TOTAL_RESULTS)
		return;
	StartListRequest(false);
}

void CCherryGifs::StartListRequest(bool Reset)
{
	if(m_pListRequest)
	{
		m_pListRequest->Abort();
		m_pListRequest = nullptr;
	}

	if(Reset)
	{
		m_vResults.clear();
		m_vThumbnailJobs.clear();
		m_Offset = 0;
		m_Total = 0;
		m_HasMore = false;
		m_Error = false;
		m_aErrorText[0] = '\0';
	}

	char aEncodedQuery[192] = "";
	UrlEncodeQuery(m_aAppliedQuery, aEncodedQuery, sizeof(aEncodedQuery));

	char aUrl[256];
	str_format(aUrl, sizeof(aUrl), "%s/api/v1/gifs?limit=%d&offset=%d&sort=%s&nsfw=%s",
		CHERRYGIFS_DEFAULT_ENDPOINT, CHERRYGIFS_LIST_LIMIT, m_Offset,
		m_AppliedSortTop ? "top" : "new", m_AppliedIncludeNsfw ? "include" : "exclude");
	if(aEncodedQuery[0] != '\0' && str_length(aUrl) + str_length(aEncodedQuery) + 4 < (int)sizeof(aUrl))
	{
		str_append(aUrl, "&q=");
		str_append(aUrl, aEncodedQuery);
	}

	char aAuthHeader[64];
	str_format(aAuthHeader, sizeof(aAuthHeader), "Bearer %s", CHERRYGIFS_API_KEY);

	std::shared_ptr<CHttpRequest> pGet = HttpGet(aUrl);
	pGet->HeaderString("Authorization", aAuthHeader);
	pGet->Timeout(CTimeout{8000, 0, 500, 10});
	pGet->MaxResponseSize(CHERRYGIFS_LIST_MAX_RESPONSE_BYTES);
	pGet->FailOnErrorStatus(false);
	pGet->LogProgress(HTTPLOG::FAILURE);

	m_pListRequest = pGet;
	m_Loading = true;
	m_ListRequestCooldown = CHERRYGIFS_LIST_MIN_INTERVAL;
	Http()->Run(pGet);
}

void CCherryGifs::PollListRequest()
{
	if(!m_pListRequest)
		return;
	if(m_pListRequest->State() == EHttpState::RUNNING || m_pListRequest->State() == EHttpState::QUEUED)
		return;

	m_Loading = false;

	if(m_pListRequest->State() == EHttpState::ABORTED)
	{
		m_pListRequest = nullptr;
		return;
	}

	if(m_pListRequest->State() != EHttpState::DONE)
	{
		m_Error = true;
		str_copy(m_aErrorText, Localize("Connection error, see console"));
		log_error("cherrygifs", "List request failed (state=%d)", (int)m_pListRequest->State());
		m_pListRequest = nullptr;
		return;
	}

	json_value *pRoot = m_pListRequest->ResultJson();
	const int StatusCode = m_pListRequest->StatusCode();

	if(StatusCode != 200)
	{
		m_Error = true;
		const json_value *pErrorField = pRoot && pRoot->type == json_object ? json_object_get(pRoot, "error") : &json_value_none;
		if(pErrorField != &json_value_none && pErrorField->type == json_string)
			str_format(m_aErrorText, sizeof(m_aErrorText), Localize("CherryGifs error %d: %s"), StatusCode, json_string_get(pErrorField));
		else
			str_format(m_aErrorText, sizeof(m_aErrorText), Localize("CherryGifs HTTP error %d"), StatusCode);
		log_error("cherrygifs", "%s", m_aErrorText);
		json_value_free(pRoot);
		m_pListRequest = nullptr;
		return;
	}

	if(!pRoot || pRoot->type != json_object)
	{
		m_Error = true;
		str_copy(m_aErrorText, Localize("Malformed response (not an object)"));
		json_value_free(pRoot);
		m_pListRequest = nullptr;
		return;
	}

	const json_value *pTotal = json_object_get(pRoot, "total");
	if(pTotal != &json_value_none && pTotal->type == json_integer)
		m_Total = json_int_get(pTotal);

	const json_value *pGifs = json_object_get(pRoot, "gifs");
	int AddedCount = 0;
	if(pGifs != &json_value_none && pGifs->type == json_array)
	{
		const int Count = json_array_length(pGifs);
		const int ParseCount = std::min(Count, CHERRYGIFS_MAX_RESULTS_PER_RESPONSE);
		for(int i = 0; i < ParseCount; ++i)
		{
			const json_value *pItem = json_array_get(pGifs, i);
			if(!pItem || pItem->type != json_object)
				continue;

			const json_value *pId = json_object_get(pItem, "id");
			const json_value *pUrl = json_object_get(pItem, "url");
			if(pId == &json_value_none || pId->type != json_string || pUrl == &json_value_none || pUrl->type != json_string)
				continue;
			// Never accept a gif whose url doesn't point back at the trusted host - see
			// IsCherryGifsUrl for why.
			if(!IsCherryGifsUrl(json_string_get(pUrl)))
				continue;

			SCherryGif Gif;
			str_copy(Gif.m_aId, json_string_get(pId), sizeof(Gif.m_aId));
			str_copy(Gif.m_aUrl, json_string_get(pUrl), sizeof(Gif.m_aUrl));

			const json_value *pPreviewUrl = json_object_get(pItem, "previewUrl");
			const char *pPreviewUrlStr = (pPreviewUrl != &json_value_none && pPreviewUrl->type == json_string) ? json_string_get(pPreviewUrl) : nullptr;
			str_copy(Gif.m_aPreviewUrl, (pPreviewUrlStr && IsCherryGifsUrl(pPreviewUrlStr)) ? pPreviewUrlStr : Gif.m_aUrl, sizeof(Gif.m_aPreviewUrl));

			const json_value *pCaption = json_object_get(pItem, "caption");
			if(pCaption != &json_value_none && pCaption->type == json_string)
				str_copy(Gif.m_aCaption, json_string_get(pCaption), sizeof(Gif.m_aCaption));

			const json_value *pLikes = json_object_get(pItem, "likes");
			if(pLikes != &json_value_none && pLikes->type == json_integer)
				Gif.m_Likes = json_int_get(pLikes);

			const json_value *pTags = json_object_get(pItem, "tags");
			if(pTags != &json_value_none && pTags->type == json_array)
			{
				const int TagCount = std::min(json_array_length(pTags), CHERRYGIFS_MAX_TAGS_PER_GIF);
				for(int t = 0; t < TagCount; ++t)
				{
					const json_value *pTag = json_array_get(pTags, t);
					if(pTag && pTag->type == json_string)
						Gif.m_vTags.emplace_back(json_string_get(pTag));
				}
			}

			const json_value *pNsfw = json_object_get(pItem, "nsfw");
			if(pNsfw != &json_value_none && pNsfw->type == json_boolean)
				Gif.m_Nsfw = json_boolean_get(pNsfw) != 0;

			if((int)m_NsfwByUrl.size() < CHERRYGIFS_MAX_NSFW_CACHE_ENTRIES)
				m_NsfwByUrl[Gif.m_aUrl] = Gif.m_Nsfw;

			m_vResults.push_back(std::move(Gif));
			++AddedCount;
		}
		m_Offset += Count;
	}

	// "total" is not filter-aware on the CherryGifs backend (it stays the whole-catalog count even
	// with a search query applied), so it can't be trusted alone to decide there's more to load -
	// a page that came back empty is treated as the actual end, regardless of what "total" claims.
	m_HasMore = AddedCount > 0 && m_Offset < m_Total && (int)m_vResults.size() < CHERRYGIFS_MAX_TOTAL_RESULTS;
	log_info("cherrygifs", "List request done: +%d gifs (total known=%d, have=%d, hasMore=%d)", AddedCount, m_Total, (int)m_vResults.size(), m_HasMore ? 1 : 0);

	json_value_free(pRoot);
	m_pListRequest = nullptr;
}

void CCherryGifs::RequestThumbnail(int Index)
{
	if(Index < 0 || Index >= (int)m_vResults.size())
		return;
	SCherryGif &Gif = m_vResults[Index];
	if(Gif.m_Thumbnail.IsValid() || Gif.m_ThumbnailRequested || Gif.m_ThumbnailFailed)
		return;
	if((int)m_vThumbnailJobs.size() >= CHERRYGIFS_MAX_CONCURRENT_THUMBNAILS)
		return;
	if(Gif.m_aPreviewUrl[0] == '\0')
		return;
	// Re-check even though PollListRequest already filtered on ingest - cheap, and it means this
	// function stays safe to call on its own if that ever changes.
	if(!IsCherryGifsUrl(Gif.m_aPreviewUrl))
	{
		Gif.m_ThumbnailFailed = true;
		return;
	}

	// Disk cache hit: decode straight from the cached bytes, no network round trip at all. This
	// is what makes reopening the browser (or restarting the client) not re-fetch everything.
	std::vector<unsigned char> vCached;
	if(CherryGifsCache::Load(Storage(), Gif.m_aId, vCached) && IsRecognizedImageSignature(vCached.data(), vCached.size()))
	{
		Gif.m_ThumbnailRequested = true;
		SMediaDecodedFrames DecodedFrames;
		if(MediaDecoder::DecodeStaticImageCpu(Graphics(), vCached.data(), vCached.size(), Gif.m_aId, DecodedFrames, 256))
		{
			std::vector<SMediaFrame> vFrames;
			if(MediaDecoder::UploadFrames(Graphics(), DecodedFrames, vFrames, Gif.m_aId) && !vFrames.empty())
			{
				Gif.m_Thumbnail = vFrames.front().m_Texture;
				return;
			}
		}
		Gif.m_ThumbnailFailed = true;
		return;
	}

	Gif.m_ThumbnailRequested = true;

	SThumbnailJob Job;
	str_copy(Job.m_aGifId, Gif.m_aId, sizeof(Job.m_aGifId));

	std::shared_ptr<CHttpRequest> pGet = HttpGet(Gif.m_aPreviewUrl);
	pGet->Timeout(CTimeout{8000, 0, 4096, 8});
	pGet->MaxResponseSize(8 * 1024 * 1024);
	pGet->FailOnErrorStatus(false);
	pGet->LogProgress(HTTPLOG::NONE);
	Job.m_pRequest = pGet;
	Http()->Run(pGet);

	m_vThumbnailJobs.push_back(std::move(Job));
}

void CCherryGifs::PollThumbnails()
{
	for(size_t i = 0; i < m_vThumbnailJobs.size();)
	{
		SThumbnailJob &Job = m_vThumbnailJobs[i];
		if(!Job.m_pRequest->Done())
		{
			++i;
			continue;
		}

		SCherryGif *pGif = nullptr;
		for(SCherryGif &Candidate : m_vResults)
		{
			if(str_comp(Candidate.m_aId, Job.m_aGifId) == 0)
			{
				pGif = &Candidate;
				break;
			}
		}

		if(pGif && Job.m_pRequest->State() == EHttpState::DONE && Job.m_pRequest->StatusCode() == 200)
		{
			unsigned char *pData = nullptr;
			size_t DataSize = 0;
			Job.m_pRequest->Result(&pData, &DataSize);
			if(pData && DataSize > 0 && IsRecognizedImageSignature(pData, DataSize))
			{
				SMediaDecodedFrames DecodedFrames;
				if(MediaDecoder::DecodeStaticImageCpu(Graphics(), pData, DataSize, pGif->m_aId, DecodedFrames, 256))
				{
					std::vector<SMediaFrame> vFrames;
					if(MediaDecoder::UploadFrames(Graphics(), DecodedFrames, vFrames, pGif->m_aId) && !vFrames.empty())
					{
						pGif->m_Thumbnail = vFrames.front().m_Texture;
						CherryGifsCache::Save(Storage(), pGif->m_aId, pData, DataSize);
					}
					else
					{
						pGif->m_ThumbnailFailed = true;
					}
				}
				else
				{
					pGif->m_ThumbnailFailed = true;
				}
			}
			else
			{
				pGif->m_ThumbnailFailed = true;
			}
		}
		else if(pGif)
		{
			pGif->m_ThumbnailFailed = true;
		}

		m_vThumbnailJobs.erase(m_vThumbnailJobs.begin() + i);
	}
}

bool CCherryGifs::TryGetNsfw(const char *pUrl, bool &OutNsfw) const
{
	const auto It = m_NsfwByUrl.find(pUrl);
	if(It == m_NsfwByUrl.end())
		return false;
	OutNsfw = It->second;
	return true;
}

void CCherryGifs::OnRender()
{
	if(m_ListRequestCooldown > 0.0f)
		m_ListRequestCooldown = std::max(0.0f, m_ListRequestCooldown - Client()->RenderFrameTime());

	PollListRequest();
	PollThumbnails();

	if(m_PendingChangeTimer >= 0.0f)
	{
		m_PendingChangeTimer += Client()->RenderFrameTime();
		if(m_PendingChangeTimer >= CHERRYGIFS_SEARCH_DEBOUNCE && !m_Loading && m_ListRequestCooldown <= 0.0f)
			ApplyFilters();
	}
}
