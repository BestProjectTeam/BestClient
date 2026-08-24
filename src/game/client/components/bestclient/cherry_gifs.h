/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_CHERRY_GIFS_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_CHERRY_GIFS_H

#include <engine/console.h>
#include <engine/graphics.h>

#include <game/client/component.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class CHttpRequest;

struct SCherryGif
{
	char m_aId[32] = "";
	char m_aUrl[256] = "";
	char m_aPreviewUrl[256] = "";
	char m_aCaption[128] = "";
	int m_Likes = 0;
	bool m_Nsfw = false;
	std::vector<std::string> m_vTags;

	IGraphics::CTextureHandle m_Thumbnail;
	bool m_ThumbnailRequested = false;
	bool m_ThumbnailFailed = false;
};

// Client for the CherryGifs gif browser API (https://gifs.teeworlds.xyz).
// Key and endpoint are intentionally hardcoded in the .cpp, not user-facing config.
class CCherryGifs : public CComponent
{
	std::vector<SCherryGif> m_vResults;
	int m_Offset = 0;
	int m_Total = 0;
	bool m_HasMore = false;
	bool m_Loading = false;
	bool m_Error = false;
	char m_aErrorText[128] = "";

	bool m_Initialized = false;
	char m_aAppliedQuery[128] = "";
	bool m_AppliedSortTop = false;
	bool m_AppliedIncludeNsfw = false;

	char m_aPendingQuery[128] = "";
	bool m_PendingSortTop = false;
	bool m_PendingIncludeNsfw = false;
	float m_PendingChangeTimer = -1.0f;

	std::shared_ptr<CHttpRequest> m_pListRequest;
	float m_ListRequestCooldown = 0.0f;

	struct SThumbnailJob
	{
		std::shared_ptr<CHttpRequest> m_pRequest;
		char m_aGifId[32] = "";
	};
	std::vector<SThumbnailJob> m_vThumbnailJobs;

	// Remembers the nsfw flag for every gif url we've ever seen from the API this session (not
	// just the currently loaded page), so chat can hide/mark a CherryGifs link as nsfw even after
	// the browser has paged past it.
	std::unordered_map<std::string, bool> m_NsfwByUrl;

	void ApplyFilters();
	void StartListRequest(bool Reset);
	void PollListRequest();
	void PollThumbnails();

	static void ConCherryGifsTestList(IConsole::IResult *pResult, void *pUserData);

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnRender() override;

	// Called every frame by the UI with the currently desired filters; debounces internally.
	void SetFilters(const char *pQuery, bool SortTop, bool IncludeNsfw);
	void LoadMore();
	void RequestThumbnail(int Index);

	// Returns true if we know whether pUrl is nsfw (filling OutNsfw), false if we've never seen
	// this url from the API (e.g. someone else posted a CherryGifs link we haven't browsed to).
	bool TryGetNsfw(const char *pUrl, bool &OutNsfw) const;

	const std::vector<SCherryGif> &Results() const { return m_vResults; }
	bool IsLoading() const { return m_Loading; }
	bool HasMore() const { return m_HasMore; }
	bool HasError() const { return m_Error; }
	const char *ErrorText() const { return m_aErrorText; }
};

#endif // GAME_CLIENT_COMPONENTS_BESTCLIENT_CHERRY_GIFS_H
