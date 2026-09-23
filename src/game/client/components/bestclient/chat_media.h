/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_CHAT_MEDIA_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_CHAT_MEDIA_H

#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/http.h>
#include <engine/input.h>
#include <engine/shared/jobs.h>

#include <game/client/component.h>
#include <game/client/components/bestclient/media_decoder.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

class CChat;

enum class EChatMediaState
{
	NONE = 0,
	QUEUED,
	LOADING,
	DECODING,
	READY,
	FAILED,
};

enum class EChatMediaKind
{
	UNKNOWN = 0,
	PHOTO,
	ANIMATED,
	VIDEO,
};

struct SChatMediaRenderRect
{
	float m_X = 0.0f;
	float m_Y = 0.0f;
	float m_W = 0.0f;
	float m_H = 0.0f;
};

class CChatMediaDecodeJob;

struct SChatMediaLine
{
	SChatMediaLine();
	~SChatMediaLine();
	SChatMediaLine(SChatMediaLine &&) noexcept;
	SChatMediaLine &operator=(SChatMediaLine &&) noexcept;
	SChatMediaLine(const SChatMediaLine &) = delete;
	SChatMediaLine &operator=(const SChatMediaLine &) = delete;

	EChatMediaState m_MediaState = EChatMediaState::NONE;
	EChatMediaKind m_MediaKind = EChatMediaKind::UNKNOWN;
	char m_aMediaUrl[512] = "";
	std::vector<std::string> m_vMediaCandidates;
	int m_MediaCandidateIndex = -1;
	int m_MediaRetryCount = 0;
	std::shared_ptr<IHttpRequest> m_pMediaRequest;
	std::shared_ptr<CChatMediaDecodeJob> m_pMediaDecodeJob;
	std::optional<SMediaDecodedFrames> m_OptMediaDecodedFrames;
	int m_MediaUploadIndex = 0;
	std::vector<SMediaFrame> m_vMediaFrames;
	std::vector<int> m_vMediaFrameEndMs;
	int m_MediaTotalDurationMs = 0;
	bool m_MediaAnimated = false;
	bool m_MediaRevealed = false;
	int m_MediaWidth = 0;
	int m_MediaHeight = 0;
	int m_MediaResolveDepth = 0;
	int64_t m_MediaAnimationStart = 0;
	bool m_PendingLayoutRefresh = false;
	float m_aTextHeight[2] = {0.0f, 0.0f};
	float m_aMediaPreviewWidth[2] = {0.0f, 0.0f};
	float m_aMediaPreviewHeight[2] = {0.0f, 0.0f};
	SChatMediaRenderRect m_MediaPreviewRect;
	bool m_MediaPreviewRectValid = false;
	SChatMediaRenderRect m_MediaRetryRect;
	bool m_MediaRetryRectValid = false;
};

void DrawRoundedMediaPreview(IGraphics *pGraphics, const IGraphics::CTextureHandle &Texture, float X, float Y, float W, float H, float Rounding, float Alpha);
void ComputeChatMediaPreviewSize(int MediaWidth, int MediaHeight, float MaxPreviewWidth, float MaxPreviewHeight, float &PreviewW, float &PreviewH);

class CChatMedia : public CComponent
{
	bool m_HideMediaByBind = false;
	int m_FullscreenMediaLineIndex = -1;
	char m_aFullscreenMediaUrl[512] = "";

	static bool IsDirectMediaUrl(const char *pUrl);
	static void ExtractMediaUrlsFromText(const char *pText, std::vector<std::string> &vOutUrls);
	static EChatMediaKind MediaKindFromUrl(const char *pUrl);
	void SetMediaCandidates(SChatMediaLine &Media, const std::vector<std::string> &vCandidates);
	void InsertMediaCandidates(SChatMediaLine &Media, const std::vector<std::string> &vCandidates, int InsertIndex);
	bool QueueNextMediaCandidate(SChatMediaLine &Media, const char *pReason);
	bool RetryMediaLine(SChatMediaLine &Media);
	void ResetHiddenMediaReveals(CChat &Chat);
	void QueueMediaDownload(SChatMediaLine &Media);
	void StartMediaDownload(SChatMediaLine &Media);
	bool StartMediaDecode(SChatMediaLine &Media, EChatMediaKind MediaKind, const unsigned char *pData, size_t DataSize);
	bool AnyMediaAllowed() const;
	bool IsMediaKindAllowed(EChatMediaKind Kind) const;
	bool IsMediaUrlAllowed(const char *pUrl) const;
	bool HasAllowedMediaCandidates(const SChatMediaLine &Media) const;
	std::string MediaPlaceholderText(const SChatMediaLine &Media) const;
	void OpenFullscreenMedia(CChat &Chat, int LineIndex);
	void CloseFullscreenMedia();
	bool HasValidFullscreenMedia(CChat &Chat) const;
	vec2 ChatMousePos() const;

	static void ConToggleHideChatMedia(IConsole::IResult *pResult, void *pUserData);

public:
	int Sizeof() const override { return sizeof(*this); }

	void OnConsoleInit() override;

	void ResetLine(SChatMediaLine &Media, IGraphics *pGraphics);
	void OnChatLineAdded(CChat &Chat, SChatMediaLine &Media, const char *pText);
	void Update(CChat &Chat);
	bool OnInput(CChat &Chat, const IInput::CEvent &Event);
	void PrepareLineLayout(SChatMediaLine &Media, int OffsetType, float LineWidth, float MaxPreviewHeight, float FontSize, float TextHeight, float &TotalHeight);
	std::string BuildVisibleMessageText(const SChatMediaLine &Media, const char *pText, bool UseMediaLabelWhenEmpty) const;
	bool ShouldDisplayMediaSlot(const SChatMediaLine &Media) const;
	bool ShouldHideMediaPreview(const SChatMediaLine &Media) const;
	bool ShouldExpandCompactAreaForMedia(CChat &Chat, bool IsScoreBoardOpen, bool ShowLargeArea) const;
	float CompactMediaHeightLimit(float BottomY) const;
	void RenderLinePreview(SChatMediaLine &Media, float PreviewX, float PreviewY, float Blend, float FontSize, int OffsetType);
	void RenderFullscreen(CChat &Chat, float Width, float Height);
	bool GetCurrentFrameTexture(SChatMediaLine &Media, IGraphics::CTextureHandle &Texture) const;
	SChatMediaLine *FindReadyMediaForMessage(CChat &Chat, int ClientId, const char *pText);
};

#endif
