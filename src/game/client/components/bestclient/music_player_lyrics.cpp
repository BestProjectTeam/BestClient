/* Copyright © 2026 BestProject Team */
#include "music_player_lyrics.h"

#include <base/math.h>
#include <base/system.h>

#include <engine/http.h>
#include <engine/shared/json.h>
#include <engine/textrender.h>

#include <game/client/components/bestclient/version.h>
#include <game/client/ui.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
	static constexpr float LYRICS_SLOT_WIDTH = 70.0f;
	static constexpr float LYRICS_LINE_SLIDE_MS = 260.0f;
	static constexpr float LYRICS_TITLE_MARQUEE_GAP_FACTOR = 2.5f;
	static constexpr ColorRGBA LYRICS_PASSED_COLOR(1.0f, 1.0f, 1.0f, 1.0f);
	static constexpr ColorRGBA LYRICS_UPCOMING_COLOR(0.45f, 0.45f, 0.48f, 1.0f);
	static constexpr int64_t LYRICS_OFFLINE_RETRY_MS = 15000;
	static constexpr size_t LYRICS_CACHE_MAX = 64;

	static float EaseOutCubic(float T)
	{
		T = std::clamp(T, 0.0f, 1.0f);
		const float Inv = 1.0f - T;
		return 1.0f - Inv * Inv * Inv;
	}

	static const char *JsonStringOrEmpty(const json_value *pValue)
	{
		if(pValue == nullptr || pValue == &json_value_none || pValue->type != json_string)
			return "";
		const char *pStr = json_string_get(pValue);
		return pStr != nullptr ? pStr : "";
	}
} // namespace

float CMusicPlayerLyrics::LyricsTextSlotWidth(float Scale, float WidthScale)
{
	return LYRICS_SLOT_WIDTH * Scale * WidthScale;
}

void CMusicPlayerLyrics::TickDisplay(float Delta)
{
	if(m_DisplayState == EDisplayState::NotFound)
		m_NotFoundDisplayMs += maximum(0.0f, Delta) * 1000.0f;
}

int CMusicPlayerLyrics::ResolveDisplayLineIndex() const
{
	if(m_DisplayState == EDisplayState::NotFound)
		return (m_NotFoundDisplayMs < (float)NOT_FOUND_HOLD_MS) ? FALLBACK_NOT_FOUND : FALLBACK_TITLE;

	if(m_DisplayState != EDisplayState::Ready)
		return LINE_NONE;

	const int64_t PositionMs = CurrentPositionMs();
	int LineIndex = FindLineIndex(PositionMs);
	if(LineIndex < 0 && !m_vLines.empty())
	{
		const int64_t CountdownRemainingMs = m_vLines.front().m_StartMs - PositionMs;
		if(CountdownRemainingMs > 2000)
			return -3;
		if(CountdownRemainingMs > 1000)
			return -2;
		if(CountdownRemainingMs > 0)
			return -1;
		return 0;
	}
	if(m_ClockPlaying && m_CurrentLineIndex >= 0 && LineIndex >= 0 && LineIndex < m_CurrentLineIndex)
		return m_CurrentLineIndex;
	return LineIndex;
}

float CMusicPlayerLyrics::PreferredTextSlotWidth(ITextRender *pTextRender, float FontSize, float MaxWidth, float Scale, float WidthScale) const
{
	const float ClampedMax = maximum(0.0f, MaxWidth);
	if(ClampedMax <= 0.0f)
		return 0.0f;

	// Only the track title shrinks to content; lyrics, errors, and countdown keep full width.
	if(m_DisplayState != EDisplayState::NotFound || ResolveDisplayLineIndex() != FALLBACK_TITLE)
		return ClampedMax;

	const char *pTitle = FallbackText(FALLBACK_TITLE);
	if(pTextRender == nullptr || pTitle == nullptr || pTitle[0] == '\0')
		return ClampedMax;

	const float Pad = 1.2f * Scale * WidthScale;
	const float TextW = pTextRender->TextWidth(FontSize, pTitle, -1, -1.0f);
	return std::clamp(TextW + Pad * 2.0f, 0.0f, ClampedMax);
}

void CMusicPlayerLyrics::Reset()
{
	AbortRequest();
	m_ActiveKey.clear();
	m_RequestKey.clear();
	m_DisplayState = EDisplayState::Idle;
	m_vLines.clear();
	m_OfflineRetryAt = 0;
	ClearActiveTrack();
}

void CMusicPlayerLyrics::ClearActiveTrack()
{
	m_CurrentLineIndex = LINE_NONE;
	m_OutgoingLineIndex = LINE_NONE;
	m_LineTransitionT = 1.0f;
	m_LayoutValid = false;
	m_LayoutText.clear();
	m_vCharMetrics.clear();
	m_BaseLineWidth = 0.0f;
	m_NotFoundDisplayMs = 0.0f;
	m_TitleMarqueeOffset = 0.0f;
	m_ClockPositionMs = 0;
	m_ClockTick = 0;
	m_ClockPlaying = false;
	m_ClockDurationMs = 0;
}

void CMusicPlayerLyrics::ClearLayoutState()
{
	m_CurrentLineIndex = LINE_NONE;
	m_OutgoingLineIndex = LINE_NONE;
	m_LineTransitionT = 1.0f;
	m_LayoutValid = false;
	m_LayoutText.clear();
	m_vCharMetrics.clear();
	m_BaseLineWidth = 0.0f;
}

void CMusicPlayerLyrics::Disable()
{
	if(!IsActive() && m_DisplayState == EDisplayState::Idle)
		return;
	AbortRequest();
	m_ActiveKey.clear();
	m_DisplayState = EDisplayState::Idle;
	m_vLines.clear();
	m_OfflineRetryAt = 0;
	ClearActiveTrack();
}

void CMusicPlayerLyrics::AbortRequest()
{
	if(m_pRequest)
	{
		m_pRequest->Abort();
		m_pRequest.reset();
	}
	m_RequestKey.clear();
}

std::string CMusicPlayerLyrics::BuildCacheKey(const char *pTitle, const char *pArtist, int64_t DurationMs)
{
	const int DurationSec = (int)((maximum<int64_t>(0, DurationMs) + 500) / 1000);
	std::string Key;
	Key.reserve(128);
	Key += "v2|";
	Key += pArtist ? pArtist : "";
	Key += '|';
	Key += pTitle ? pTitle : "";
	Key += '|';
	Key += std::to_string(DurationSec);
	return Key;
}

bool CMusicPlayerLyrics::ParseLrcTimestamp(const char *pText, int64_t &OutMs, const char **ppEnd)
{
	if(pText == nullptr || pText[0] != '[')
		return false;

	int Minutes = 0;
	int Seconds = 0;
	int Fraction = 0;
	int FractionDigits = 0;
	const char *p = pText + 1;
	if(!std::isdigit((unsigned char)*p))
		return false;

	while(std::isdigit((unsigned char)*p))
	{
		Minutes = Minutes * 10 + (*p - '0');
		++p;
	}
	if(*p != ':')
		return false;
	++p;
	if(!std::isdigit((unsigned char)*p))
		return false;
	while(std::isdigit((unsigned char)*p))
	{
		Seconds = Seconds * 10 + (*p - '0');
		++p;
	}
	if(*p == '.' || *p == ',')
	{
		++p;
		while(std::isdigit((unsigned char)*p) && FractionDigits < 3)
		{
			Fraction = Fraction * 10 + (*p - '0');
			++FractionDigits;
			++p;
		}
		while(std::isdigit((unsigned char)*p))
			++p;
	}
	if(*p != ']')
		return false;
	++p;

	while(FractionDigits > 0 && FractionDigits < 3)
	{
		Fraction *= 10;
		++FractionDigits;
	}

	OutMs = (int64_t)Minutes * 60000 + (int64_t)Seconds * 1000 + Fraction;
	if(ppEnd)
		*ppEnd = p;
	return true;
}

bool CMusicPlayerLyrics::ParseSyncedLyrics(const char *pSyncedLyrics, std::vector<SLine> &vOut)
{
	vOut.clear();
	if(pSyncedLyrics == nullptr || pSyncedLyrics[0] == '\0')
		return false;

	const char *p = pSyncedLyrics;
	while(*p)
	{
		while(*p == '\r' || *p == '\n')
			++p;
		if(*p == '\0')
			break;

		const char *pLineStart = p;
		while(*p && *p != '\n' && *p != '\r')
			++p;
		const char *pLineEnd = p;

		std::vector<int64_t> vTimestamps;
		const char *pCursor = pLineStart;
		while(pCursor < pLineEnd)
		{
			int64_t TimestampMs = 0;
			const char *pAfter = nullptr;
			if(!ParseLrcTimestamp(pCursor, TimestampMs, &pAfter) || pAfter == nullptr || pAfter > pLineEnd)
				break;
			vTimestamps.push_back(TimestampMs);
			pCursor = pAfter;
		}

		while(pCursor < pLineEnd && (*pCursor == ' ' || *pCursor == '\t'))
			++pCursor;

		if(vTimestamps.empty())
			continue;

		std::string Text(pCursor, pLineEnd);
		while(!Text.empty() && (Text.back() == ' ' || Text.back() == '\t'))
			Text.pop_back();

		if(Text.empty())
			continue;

		for(int64_t TimestampMs : vTimestamps)
		{
			SLine Line;
			Line.m_StartMs = TimestampMs;
			Line.m_Text = Text;
			vOut.push_back(std::move(Line));
		}
	}

	if(vOut.empty())
		return false;

	std::stable_sort(vOut.begin(), vOut.end(), [](const SLine &A, const SLine &B) {
		return A.m_StartMs < B.m_StartMs;
	});
	MergeConsecutiveIdenticalLines(vOut);
	return !vOut.empty();
}

void CMusicPlayerLyrics::MergeConsecutiveIdenticalLines(std::vector<SLine> &vLines)
{
	if(vLines.size() < 2)
		return;

	// Only drop near-duplicate timestamps of the same text (e.g. [01:00.00][01:00.05]).
	// Merging repeats that span seconds makes karaoke progress crawl through one line.
	static constexpr int64_t NearDuplicateMs = 150;

	std::vector<SLine> vMerged;
	vMerged.reserve(vLines.size());
	for(SLine &Line : vLines)
	{
		if(!vMerged.empty() &&
			vMerged.back().m_Text == Line.m_Text &&
			Line.m_StartMs - vMerged.back().m_StartMs <= NearDuplicateMs)
		{
			continue;
		}
		vMerged.push_back(std::move(Line));
	}
	vLines = std::move(vMerged);
}

void CMusicPlayerLyrics::ApplyCacheEntry(const SCacheEntry &Entry)
{
	m_DisplayState = Entry.m_State;
	m_vLines = Entry.m_vLines;
	MergeConsecutiveIdenticalLines(m_vLines);
	ClearLayoutState();
	if(Entry.m_State == EDisplayState::NotFound)
		m_NotFoundDisplayMs = 0.0f;
}

void CMusicPlayerLyrics::StartRequest(IHttp *pHttp, const char *pTitle, const char *pArtist, const char *pAlbum, int64_t DurationMs)
{
	if(pHttp == nullptr)
	{
		m_DisplayState = EDisplayState::Offline;
		return;
	}

	AbortRequest();

	char aEscapedTitle[512];
	char aEscapedArtist[512];
	char aEscapedAlbum[512];
	EscapeUrl(aEscapedTitle, pTitle ? pTitle : "");
	EscapeUrl(aEscapedArtist, pArtist ? pArtist : "");
	EscapeUrl(aEscapedAlbum, pAlbum ? pAlbum : "");

	const int DurationSec = (int)((maximum<int64_t>(0, DurationMs) + 500) / 1000);
	char aUrl[2048];
	if(DurationSec >= 1 && DurationSec <= 3600)
	{
		str_format(aUrl, sizeof(aUrl),
			"https://lrclib.net/api/get?track_name=%s&artist_name=%s&album_name=%s&duration=%d",
			aEscapedTitle, aEscapedArtist, aEscapedAlbum, DurationSec);
	}
	else
	{
		str_format(aUrl, sizeof(aUrl),
			"https://lrclib.net/api/get?track_name=%s&artist_name=%s&album_name=%s",
			aEscapedTitle, aEscapedArtist, aEscapedAlbum);
	}

	m_pRequest = HttpGet(aUrl);
	m_pRequest->Timeout(CTimeout{3000, 8000, 500, 5});
	m_pRequest->LogProgress(HTTPLOG::FAILURE);
	m_pRequest->FailOnErrorStatus(false);
	m_pRequest->HeaderString("Lrclib-Client", "BestClient/" BESTCLIENT_VERSION " (https://github.com/BestProjectTeam/BestClient)");
	m_RequestKey = m_ActiveKey;
	m_DisplayState = EDisplayState::Loading;
	pHttp->Run(m_pRequest);
}

void CMusicPlayerLyrics::ProcessRequest()
{
	if(!m_pRequest || !m_pRequest->Done())
		return;

	std::shared_ptr<IHttpRequest> pFinished = m_pRequest;
	m_pRequest.reset();
	const std::string FinishedKey = m_RequestKey;
	m_RequestKey.clear();

	if(FinishedKey != m_ActiveKey)
		return;

	// Done() is also true for ERROR/ABORTED — must not call StatusCode() unless DONE.
	if(pFinished->State() != EHttpState::DONE)
	{
		m_DisplayState = EDisplayState::Offline;
		m_vLines.clear();
		ClearActiveTrack();
		m_OfflineRetryAt = time_get() + time_freq() * LYRICS_OFFLINE_RETRY_MS / 1000;
		return;
	}

	const int StatusCode = pFinished->StatusCode();
	if(StatusCode == 0)
	{
		m_DisplayState = EDisplayState::Offline;
		m_vLines.clear();
		ClearActiveTrack();
		m_OfflineRetryAt = time_get() + time_freq() * LYRICS_OFFLINE_RETRY_MS / 1000;
		return;
	}

	if(StatusCode == 404)
	{
		SCacheEntry Entry;
		Entry.m_State = EDisplayState::NotFound;
		if(m_Cache.size() >= LYRICS_CACHE_MAX)
			m_Cache.clear();
		m_Cache[FinishedKey] = Entry;
		ApplyCacheEntry(Entry);
		return;
	}

	if(StatusCode < 200 || StatusCode >= 300)
	{
		m_DisplayState = EDisplayState::Offline;
		m_vLines.clear();
		ClearActiveTrack();
		m_OfflineRetryAt = time_get() + time_freq() * LYRICS_OFFLINE_RETRY_MS / 1000;
		return;
	}

	json_value *pRoot = pFinished->ResultJson();
	SCacheEntry Entry;
	Entry.m_State = EDisplayState::NotFound;
	if(pRoot != nullptr && pRoot != &json_value_none && pRoot->type == json_object)
	{
		const char *pSynced = JsonStringOrEmpty(json_object_get(pRoot, "syncedLyrics"));
		if(ParseSyncedLyrics(pSynced, Entry.m_vLines))
			Entry.m_State = EDisplayState::Ready;
	}
	if(pRoot)
		json_value_free(pRoot);

	if(m_Cache.size() >= LYRICS_CACHE_MAX)
		m_Cache.clear();
	m_Cache[FinishedKey] = Entry;
	ApplyCacheEntry(Entry);
}

int64_t CMusicPlayerLyrics::CurrentPositionMs() const
{
	int64_t Position = maximum<int64_t>(0, m_ClockPositionMs);
	if(m_ClockPlaying && m_ClockTick > 0)
		Position += ((time_get() - m_ClockTick) * 1000) / time_freq();
	if(m_ClockDurationMs > 0)
		Position = minimum(Position, m_ClockDurationMs);
	return Position;
}

void CMusicPlayerLyrics::SyncMediaClock(int64_t SnapshotPositionMs, int64_t DurationMs, bool Playing, bool ForceReset)
{
	SnapshotPositionMs = maximum<int64_t>(0, SnapshotPositionMs);
	m_ClockDurationMs = maximum<int64_t>(0, DurationMs);
	const int64_t Now = time_get();

	if(ForceReset || m_ClockTick == 0 || !Playing)
	{
		m_ClockPositionMs = SnapshotPositionMs;
		m_ClockTick = Now;
		m_ClockPlaying = Playing;
		return;
	}

	// Follow extrapolated media position 1:1. ApplySnapshot no longer sawtooths
	// backwards on stale polls, so this stays smooth and on timing.
	m_ClockPositionMs = SnapshotPositionMs;
	m_ClockTick = Now;
	m_ClockPlaying = true;
	if(m_ClockDurationMs > 0)
		m_ClockPositionMs = minimum(m_ClockPositionMs, m_ClockDurationMs);
}

void CMusicPlayerLyrics::Update(IHttp *pHttp, const char *pTitle, const char *pArtist, const char *pAlbum, int64_t DurationMs, int64_t SnapshotPositionMs, bool Playing)
{
	ProcessRequest();

	m_TrackTitle = (pTitle != nullptr && pTitle[0] != '\0') ? pTitle : "";

	const bool HasIdentity = (pTitle && pTitle[0] != '\0') || (pArtist && pArtist[0] != '\0');
	if(!HasIdentity)
	{
		if(!m_ActiveKey.empty())
		{
			AbortRequest();
			m_ActiveKey.clear();
			m_DisplayState = EDisplayState::Idle;
			m_vLines.clear();
			m_TrackTitle.clear();
			ClearActiveTrack();
		}
		return;
	}

	const std::string Key = BuildCacheKey(pTitle, pArtist, DurationMs);
	const bool NewTrack = Key != m_ActiveKey;
	if(NewTrack)
	{
		AbortRequest();
		m_ActiveKey = Key;
		m_vLines.clear();
		ClearActiveTrack();
		m_OfflineRetryAt = 0;
		SyncMediaClock(SnapshotPositionMs, DurationMs, Playing, true);

		const auto It = m_Cache.find(Key);
		if(It != m_Cache.end())
		{
			ApplyCacheEntry(It->second);
			return;
		}

		m_DisplayState = EDisplayState::Loading;
		StartRequest(pHttp, pTitle, pArtist, pAlbum, DurationMs);
		return;
	}

	SyncMediaClock(SnapshotPositionMs, DurationMs, Playing, false);

	if(m_DisplayState == EDisplayState::Offline && !m_pRequest)
	{
		if(m_OfflineRetryAt == 0 || time_get() >= m_OfflineRetryAt)
			StartRequest(pHttp, pTitle, pArtist, pAlbum, DurationMs);
	}
}

int CMusicPlayerLyrics::FindLineIndex(int64_t PositionMs) const
{
	if(m_vLines.empty())
		return -1;
	if(PositionMs < m_vLines.front().m_StartMs)
		return -1;

	int Lo = 0;
	int Hi = (int)m_vLines.size() - 1;
	int Best = 0;
	while(Lo <= Hi)
	{
		const int Mid = Lo + (Hi - Lo) / 2;
		if(m_vLines[Mid].m_StartMs <= PositionMs)
		{
			Best = Mid;
			Lo = Mid + 1;
		}
		else
		{
			Hi = Mid - 1;
		}
	}
	return Best;
}

float CMusicPlayerLyrics::LineProgress(int LineIndex, int64_t PositionMs) const
{
	if(LineIndex < 0 || LineIndex >= (int)m_vLines.size())
		return 0.0f;

	const int64_t StartMs = m_vLines[LineIndex].m_StartMs;
	int64_t EndMs = StartMs + 4000;
	if(LineIndex + 1 < (int)m_vLines.size())
		EndMs = maximum(StartMs + 1, m_vLines[LineIndex + 1].m_StartMs);

	if(PositionMs <= StartMs)
		return 0.0f;
	if(PositionMs >= EndMs)
		return 1.0f;
	return (float)(PositionMs - StartMs) / (float)(EndMs - StartMs);
}

float CMusicPlayerLyrics::CountdownProgress(int CountdownIndex, int64_t RemainingMs) const
{
	if(CountdownIndex == -3)
	{
		if(RemainingMs >= 3000)
			return 0.0f;
		return std::clamp((3000.0f - (float)RemainingMs) / 1000.0f, 0.0f, 1.0f);
	}
	if(CountdownIndex == -2)
		return std::clamp((2000.0f - (float)RemainingMs) / 1000.0f, 0.0f, 1.0f);
	if(CountdownIndex == -1)
		return std::clamp((1000.0f - (float)RemainingMs) / 1000.0f, 0.0f, 1.0f);
	return 0.0f;
}

const char *CMusicPlayerLyrics::FallbackText(int Index) const
{
	if(Index == FALLBACK_NOT_FOUND)
		return "Lyrics not found";
	if(Index == FALLBACK_TITLE)
		return m_TrackTitle.empty() ? "Unknown title" : m_TrackTitle.c_str();
	return "";
}

void CMusicPlayerLyrics::EnsureLayout(ITextRender *pTextRender, float FontSize, int LineIndex)
{
	if(pTextRender == nullptr)
	{
		m_LayoutValid = false;
		return;
	}

	std::string Text;
	if(IsCountdownIndex(LineIndex))
	{
		char aDigit[4];
		str_format(aDigit, sizeof(aDigit), "%d", CountdownDigit(LineIndex));
		Text = aDigit;
	}
	else if(IsFallbackIndex(LineIndex))
	{
		Text = FallbackText(LineIndex);
	}
	else if(LineIndex >= 0 && LineIndex < (int)m_vLines.size())
	{
		Text = m_vLines[LineIndex].m_Text;
	}
	else
	{
		m_LayoutValid = false;
		return;
	}

	if(Text.empty())
	{
		m_LayoutValid = false;
		return;
	}

	if(m_LayoutValid && m_LayoutText == Text && std::fabs(m_LayoutFontSize - FontSize) < 0.01f)
		return;

	m_LayoutText = Text;
	m_LayoutFontSize = FontSize;
	m_vCharMetrics.clear();
	m_BaseLineWidth = pTextRender->TextWidth(FontSize, Text.c_str(), -1, -1.0f);

	// Prefix widths via full-string slices so kerning/bearing match the final draw.
	const char *p = Text.c_str();
	while(*p)
	{
		const char *pPrev = p;
		const int Code = str_utf8_decode(&p);
		if(Code == 0)
			break;

		SCharMetric Metric;
		Metric.m_ByteOffset = (int)(pPrev - Text.c_str());
		Metric.m_ByteLength = (int)(p - pPrev);
		Metric.m_PrefixWidth = pTextRender->TextWidth(FontSize, Text.c_str(), Metric.m_ByteOffset, -1.0f);
		m_vCharMetrics.push_back(Metric);
	}

	m_LayoutValid = !m_vCharMetrics.empty();
}

void CMusicPlayerLyrics::BuildColorSplits(float ProgressChars, float Alpha, std::vector<STextColorSplit> &vOut) const
{
	vOut.clear();
	if(m_vCharMetrics.empty())
		return;

	const int CharCount = (int)m_vCharMetrics.size();
	const float Clamped = std::clamp(ProgressChars, 0.0f, (float)CharCount);
	const int FullyPassed = (int)Clamped;
	const float Frac = Clamped - (float)FullyPassed;

	auto WithA = [Alpha](ColorRGBA Color) {
		return Color.WithAlpha(Color.a * Alpha);
	};

	if(FullyPassed <= 0 && Frac <= 0.001f)
	{
		vOut.emplace_back(0, -1, WithA(LYRICS_UPCOMING_COLOR));
		return;
	}

	if(FullyPassed >= CharCount)
	{
		vOut.emplace_back(0, -1, WithA(LYRICS_PASSED_COLOR));
		return;
	}

	const int PassedEndByte = m_vCharMetrics[FullyPassed].m_ByteOffset;
	if(PassedEndByte > 0)
		vOut.emplace_back(0, PassedEndByte, WithA(LYRICS_PASSED_COLOR));

	const SCharMetric &Current = m_vCharMetrics[FullyPassed];
	if(Frac > 0.001f && Frac < 0.999f)
	{
		const ColorRGBA Blend(
			LYRICS_PASSED_COLOR.r + (LYRICS_UPCOMING_COLOR.r - LYRICS_PASSED_COLOR.r) * (1.0f - Frac),
			LYRICS_PASSED_COLOR.g + (LYRICS_UPCOMING_COLOR.g - LYRICS_PASSED_COLOR.g) * (1.0f - Frac),
			LYRICS_PASSED_COLOR.b + (LYRICS_UPCOMING_COLOR.b - LYRICS_PASSED_COLOR.b) * (1.0f - Frac),
			1.0f);
		vOut.emplace_back(Current.m_ByteOffset, Current.m_ByteLength, WithA(Blend));
		const int Next = FullyPassed + 1;
		if(Next < CharCount)
			vOut.emplace_back(m_vCharMetrics[Next].m_ByteOffset, -1, WithA(LYRICS_UPCOMING_COLOR));
	}
	else if(Frac >= 0.999f)
	{
		vOut.emplace_back(Current.m_ByteOffset, Current.m_ByteLength, WithA(LYRICS_PASSED_COLOR));
		const int Next = FullyPassed + 1;
		if(Next < CharCount)
			vOut.emplace_back(m_vCharMetrics[Next].m_ByteOffset, -1, WithA(LYRICS_UPCOMING_COLOR));
	}
	else
	{
		vOut.emplace_back(Current.m_ByteOffset, -1, WithA(LYRICS_UPCOMING_COLOR));
	}
}

float CMusicPlayerLyrics::PlayheadXInLine(float ProgressChars) const
{
	if(m_vCharMetrics.empty())
		return 0.0f;

	const int CharCount = (int)m_vCharMetrics.size();
	const float Clamped = std::clamp(ProgressChars, 0.0f, (float)CharCount);
	const int Index = minimum((int)Clamped, CharCount - 1);
	const float Frac = Clamped - (float)Index;

	const float Prefix = m_vCharMetrics[Index].m_PrefixWidth;
	float CharWidth = 0.0f;
	if(Index + 1 < CharCount)
		CharWidth = m_vCharMetrics[Index + 1].m_PrefixWidth - Prefix;
	else
		CharWidth = maximum(0.0f, m_BaseLineWidth - Prefix);

	return Prefix + CharWidth * std::clamp(Frac, 0.0f, 1.0f);
}

float CMusicPlayerLyrics::ComputeTextStartX(float AreaLeft, float AreaWidth, float CenterX, float PlayheadX) const
{
	const float IdealStartX = CenterX - PlayheadX;

	if(m_BaseLineWidth <= AreaWidth)
	{
		// Short line: keep whole line inside the area; playhead walks inside it.
		const float MinStartX = AreaLeft + AreaWidth - m_BaseLineWidth;
		const float MaxStartX = AreaLeft;
		if(MinStartX >= MaxStartX)
			return AreaLeft + (AreaWidth - m_BaseLineWidth) * 0.5f;
		return std::clamp(IdealStartX, MinStartX, MaxStartX);
	}

	// Long line: start pinned at center, end pinned so last glyph stays on the right.
	const float MaxStartX = CenterX; // progress 0: first char at center
	const float MinStartX = AreaLeft + AreaWidth - m_BaseLineWidth; // progress 1: last char at right edge
	return std::clamp(IdealStartX, MinStartX, MaxStartX);
}

void CMusicPlayerLyrics::Render(ITextRender *pTextRender, CUi *pUi, const CUIRect &Area, float FontSize, float Delta)
{
	if(pTextRender == nullptr || pUi == nullptr || Area.w <= 0.0f || Area.h <= 0.0f)
		return;

	const char *pStatusText = nullptr;
	switch(m_DisplayState)
	{
	case EDisplayState::Idle:
	case EDisplayState::Loading:
		pStatusText = "…";
		break;
	case EDisplayState::NotFound:
		break;
	case EDisplayState::Offline:
		pStatusText = "No connection";
		break;
	case EDisplayState::Ready:
		break;
	}

	if(pStatusText != nullptr)
	{
		pTextRender->TextColor(LYRICS_UPCOMING_COLOR);
		const float Width = pTextRender->TextWidth(FontSize, pStatusText, -1, -1.0f);
		pTextRender->Text(Area.x + (Area.w - Width) * 0.5f, Area.y + (Area.h - FontSize) * 0.5f, FontSize, pStatusText, -1.0f);
		return;
	}

	const int64_t PositionMs = CurrentPositionMs();
	int LineIndex = ResolveDisplayLineIndex();
	int64_t CountdownRemainingMs = 0;
	if(IsCountdownIndex(LineIndex) && !m_vLines.empty())
		CountdownRemainingMs = m_vLines.front().m_StartMs - PositionMs;

	if(LineIndex != m_CurrentLineIndex)
	{
		const bool SequentialForward =
			m_CurrentLineIndex != LINE_NONE && LineIndex == m_CurrentLineIndex + 1;
		if(SequentialForward)
		{
			m_OutgoingLineIndex = m_CurrentLineIndex;
			m_LineTransitionT = 0.0f;
		}
		else
		{
			m_OutgoingLineIndex = LINE_NONE;
			m_LineTransitionT = 1.0f;
		}
		m_CurrentLineIndex = LineIndex;
		m_LayoutValid = false;
	}

	if(m_LineTransitionT < 1.0f)
	{
		m_LineTransitionT = std::clamp(m_LineTransitionT + Delta * 1000.0f / LYRICS_LINE_SLIDE_MS, 0.0f, 1.0f);
		if(m_LineTransitionT >= 1.0f)
			m_OutgoingLineIndex = LINE_NONE;
	}

	if(m_CurrentLineIndex == LINE_NONE)
		return;

	EnsureLayout(pTextRender, FontSize, m_CurrentLineIndex);
	if(!m_LayoutValid || m_vCharMetrics.empty())
		return;

	const bool FallbackMode = IsFallbackIndex(m_CurrentLineIndex);
	const bool TitleMarquee = m_CurrentLineIndex == FALLBACK_TITLE && m_BaseLineWidth > Area.w + 0.5f;
	if(TitleMarquee)
	{
		const float Gap = FontSize * LYRICS_TITLE_MARQUEE_GAP_FACTOR;
		const float LoopW = m_BaseLineWidth + Gap;
		const float Speed = maximum(22.0f, FontSize * 2.8f);
		m_TitleMarqueeOffset += Delta * Speed;
		if(m_TitleMarqueeOffset >= LoopW)
			m_TitleMarqueeOffset = fmodf(m_TitleMarqueeOffset, LoopW);
	}
	else if(m_CurrentLineIndex != FALLBACK_TITLE)
	{
		m_TitleMarqueeOffset = 0.0f;
	}

	const float Progress = IsCountdownIndex(m_CurrentLineIndex) ?
				       CountdownProgress(m_CurrentLineIndex, CountdownRemainingMs) :
				       (FallbackMode ? 0.0f : LineProgress(m_CurrentLineIndex, PositionMs));
	const float ProgressChars = Progress * (float)m_vCharMetrics.size();
	const float CenterX = Area.x + Area.w * 0.5f;
	float TextStartX;
	if(TitleMarquee)
	{
		TextStartX = Area.x - m_TitleMarqueeOffset;
	}
	else if(IsCountdownIndex(m_CurrentLineIndex) || FallbackMode)
	{
		TextStartX = CenterX - m_BaseLineWidth * 0.5f;
	}
	else
	{
		const float PlayheadX = PlayheadXInLine(ProgressChars);
		TextStartX = ComputeTextStartX(Area.x, Area.w, CenterX, PlayheadX);
	}

	const float SlideT = EaseOutCubic(m_LineTransitionT);
	const float BaseY = Area.y + (Area.h - FontSize) * 0.5f;
	const float IncomingY = BaseY + (1.0f - SlideT) * Area.h;
	const float OutgoingY = BaseY - SlideT * Area.h;

	auto DrawDisplay = [&](int DrawIndex, float X, float Y, float ProgressCharsForColor, float Alpha, int ColorMode) {
		// ColorMode: 0=karaoke wipe, 1=all upcoming (gray), 2=all passed (white)
		if(DrawIndex == LINE_NONE || Alpha <= 0.001f)
			return;
		if(!IsCountdownIndex(DrawIndex) && !IsFallbackIndex(DrawIndex) && (DrawIndex < 0 || DrawIndex >= (int)m_vLines.size()))
			return;

		const char *pText = nullptr;
		char aDigit[4];
		float TextW = 0.0f;
		if(IsCountdownIndex(DrawIndex))
		{
			if(DrawIndex == m_CurrentLineIndex)
			{
				pText = m_LayoutText.c_str();
				TextW = m_BaseLineWidth;
			}
			else
			{
				str_format(aDigit, sizeof(aDigit), "%d", CountdownDigit(DrawIndex));
				pText = aDigit;
				TextW = pTextRender->TextWidth(FontSize, aDigit, -1, -1.0f);
			}
		}
		else if(IsFallbackIndex(DrawIndex))
		{
			pText = (DrawIndex == m_CurrentLineIndex) ? m_LayoutText.c_str() : FallbackText(DrawIndex);
			TextW = (DrawIndex == m_CurrentLineIndex) ? m_BaseLineWidth : pTextRender->TextWidth(FontSize, pText, -1, -1.0f);
		}
		else
		{
			pText = m_vLines[DrawIndex].m_Text.c_str();
			TextW = m_BaseLineWidth;
		}

		CUIRect Clip = Area;
		pUi->ClipEnable(&Clip);

		std::vector<STextColorSplit> vSplits;
		if(ColorMode == 1)
			vSplits.emplace_back(0, -1, LYRICS_UPCOMING_COLOR.WithAlpha(Alpha));
		else if(ColorMode == 2)
			vSplits.emplace_back(0, -1, LYRICS_PASSED_COLOR.WithAlpha(Alpha));
		else if(IsCountdownIndex(DrawIndex) && DrawIndex == m_CurrentLineIndex)
			BuildColorSplits(ProgressCharsForColor, Alpha, vSplits);
		else if(IsCountdownIndex(DrawIndex) || IsFallbackIndex(DrawIndex))
			vSplits.emplace_back(0, -1, LYRICS_PASSED_COLOR.WithAlpha(Alpha));
		else
			BuildColorSplits(ProgressCharsForColor, Alpha, vSplits);

		auto DrawOnce = [&](float DrawX) {
			CTextCursor Cursor;
			Cursor.m_FontSize = FontSize;
			Cursor.m_Flags = TEXTFLAG_RENDER;
			Cursor.SetPosition(vec2(DrawX, Y));
			Cursor.m_vColorSplits = vSplits;
			pTextRender->TextColor(LYRICS_UPCOMING_COLOR.WithAlpha(Alpha));
			pTextRender->TextEx(&Cursor, pText, -1);
		};

		const bool MarqueeCopy = DrawIndex == FALLBACK_TITLE && TextW > Area.w + 0.5f;
		DrawOnce(X);
		if(MarqueeCopy)
			DrawOnce(X + TextW + FontSize * LYRICS_TITLE_MARQUEE_GAP_FACTOR);

		pUi->ClipDisable();
	};

	if(m_OutgoingLineIndex != LINE_NONE && m_LineTransitionT < 1.0f)
	{
		float OutWidth;
		if(IsCountdownIndex(m_OutgoingLineIndex))
		{
			char aDigit[4];
			str_format(aDigit, sizeof(aDigit), "%d", CountdownDigit(m_OutgoingLineIndex));
			OutWidth = pTextRender->TextWidth(FontSize, aDigit, -1, -1.0f);
		}
		else if(IsFallbackIndex(m_OutgoingLineIndex))
		{
			OutWidth = pTextRender->TextWidth(FontSize, FallbackText(m_OutgoingLineIndex), -1, -1.0f);
		}
		else
		{
			OutWidth = pTextRender->TextWidth(FontSize, m_vLines[m_OutgoingLineIndex].m_Text.c_str(), -1, -1.0f);
		}
		const float OutX = CenterX - OutWidth * 0.5f;
		const int OutColorMode = (m_OutgoingLineIndex == FALLBACK_NOT_FOUND) ? 1 : 2;
		DrawDisplay(m_OutgoingLineIndex, OutX, OutgoingY, 0.0f, 1.0f - SlideT, OutColorMode);
	}

	const float ActiveAlpha = m_OutgoingLineIndex != LINE_NONE ? SlideT : 1.0f;
	int ActiveColorMode = 0;
	if(m_CurrentLineIndex == FALLBACK_NOT_FOUND)
		ActiveColorMode = 1;
	else if(m_CurrentLineIndex == FALLBACK_TITLE)
		ActiveColorMode = 2;
	DrawDisplay(m_CurrentLineIndex, TextStartX, IncomingY, ProgressChars, ActiveAlpha, ActiveColorMode);
}
