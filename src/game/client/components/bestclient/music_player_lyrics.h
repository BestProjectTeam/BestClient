/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_MUSIC_PLAYER_LYRICS_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_MUSIC_PLAYER_LYRICS_H

#include <base/color.h>

#include <game/client/ui_rect.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class IHttpRequest;
class CUi;
class IHttp;
class ITextRender;
struct STextColorSplit;

class CMusicPlayerLyrics
{
public:
	enum class EDisplayState
	{
		Idle,
		Loading,
		Ready,
		NotFound,
		Offline,
	};

	struct SLine
	{
		int64_t m_StartMs = 0;
		std::string m_Text;
	};

	void Reset();
	void ClearActiveTrack();
	void ClearLayoutState();
	void Disable();
	void Update(IHttp *pHttp, const char *pTitle, const char *pArtist, const char *pAlbum, int64_t DurationMs, int64_t SnapshotPositionMs, bool Playing);
	void AbortRequest();

	EDisplayState State() const { return m_DisplayState; }
	bool IsActive() const { return !m_ActiveKey.empty() || m_pRequest != nullptr; }
	bool HasSyncedLines() const { return !m_vLines.empty(); }
	int64_t CurrentPositionMs() const;

	static float LyricsTextSlotWidth(float Scale, float WidthScale);
	static bool ParseSyncedLyrics(const char *pSyncedLyrics, std::vector<SLine> &vOut);

	void TickDisplay(float Delta);
	float PreferredTextSlotWidth(ITextRender *pTextRender, float FontSize, float MaxWidth, float Scale, float WidthScale) const;

	void Render(ITextRender *pTextRender, CUi *pUi, const CUIRect &Area, float FontSize, float Delta);

private:
	static constexpr int LINE_NONE = -99;
	static constexpr int FALLBACK_NO_CONNECTION = -22;
	static constexpr int FALLBACK_ELLIPSIS = -21;
	static constexpr int FALLBACK_NOT_FOUND = -20;
	static constexpr int FALLBACK_TITLE = -19;
	static constexpr int FALLBACK_BRAND = -18;
	static constexpr int64_t NOT_FOUND_HOLD_MS = 5000;
	static constexpr int64_t OFFLINE_HOLD_MS = 5000;
	static constexpr int64_t BACKWARDS_SEEK_THRESHOLD_MS = 800;

	struct SCacheEntry
	{
		EDisplayState m_State = EDisplayState::NotFound;
		std::vector<SLine> m_vLines;
	};

	struct SCharMetric
	{
		int m_ByteOffset = 0;
		int m_ByteLength = 0;
		float m_PrefixWidth = 0.0f;
	};

	static std::string BuildCacheKey(const char *pTitle, const char *pArtist, int64_t DurationMs);
	static bool ParseLrcTimestamp(const char *pText, int64_t &OutMs, const char **ppEnd);
	static void MergeConsecutiveIdenticalLines(std::vector<SLine> &vLines);
	static bool IsCountdownIndex(int Index) { return Index >= -3 && Index <= -1; }
	static bool IsFallbackIndex(int Index)
	{
		return Index == FALLBACK_NO_CONNECTION || Index == FALLBACK_ELLIPSIS || Index == FALLBACK_NOT_FOUND || Index == FALLBACK_TITLE || Index == FALLBACK_BRAND;
	}
	static bool IsMutedFallback(int Index)
	{
		return Index == FALLBACK_NO_CONNECTION || Index == FALLBACK_ELLIPSIS || Index == FALLBACK_NOT_FOUND;
	}
	static int CountdownDigit(int Index) { return -Index; }
	const char *FallbackText(int Index) const;
	int ResolveDisplayLineIndex() const;
	void ApplyCacheEntry(const SCacheEntry &Entry);
	void StartRequest(IHttp *pHttp, const char *pTitle, const char *pArtist, const char *pAlbum, int64_t DurationMs);
	void ProcessRequest();
	void SyncMediaClock(int64_t SnapshotPositionMs, int64_t DurationMs, bool Playing, bool ForceReset);
	void EnsureLayout(ITextRender *pTextRender, float FontSize, int LineIndex);
	int FindLineIndex(int64_t PositionMs) const;
	float LineProgress(int LineIndex, int64_t PositionMs) const;
	float CountdownProgress(int CountdownIndex, int64_t RemainingMs) const;
	void BuildColorSplits(float ProgressChars, float Alpha, std::vector<STextColorSplit> &vOut) const;
	float PlayheadXInLine(float ProgressChars) const;
	float ComputeTextStartX(float AreaLeft, float AreaWidth, float CenterX, float PlayheadX) const;

	std::string m_ActiveKey;
	std::string m_RequestKey;
	std::string m_TrackTitle;
	EDisplayState m_DisplayState = EDisplayState::Idle;
	std::vector<SLine> m_vLines;
	std::shared_ptr<IHttpRequest> m_pRequest;
	std::unordered_map<std::string, SCacheEntry> m_Cache;
	int64_t m_OfflineRetryAt = 0;
	float m_NotFoundDisplayMs = 0.0f;
	float m_OfflineDisplayMs = 0.0f;
	float m_TitleMarqueeOffset = 0.0f;

	int64_t m_ClockPositionMs = 0;
	int64_t m_ClockTick = 0;
	bool m_ClockPlaying = false;
	int64_t m_ClockDurationMs = 0;

	int m_CurrentLineIndex = LINE_NONE;
	int m_OutgoingLineIndex = LINE_NONE;
	float m_LineTransitionT = 1.0f;

	std::string m_LayoutText;
	float m_LayoutFontSize = 0.0f;
	std::vector<SCharMetric> m_vCharMetrics;
	std::vector<STextColorSplit> m_vColorSplits;
	float m_BaseLineWidth = 0.0f;
	bool m_LayoutValid = false;
};

#endif
