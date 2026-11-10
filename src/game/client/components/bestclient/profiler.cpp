/* Copyright © 2026 BestProject Team */
#include "profiler.h"

#include <base/color.h>
#include <base/io.h>
#include <base/math.h>
#include <base/mem.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#if defined(CONF_FAMILY_WINDOWS)
// clang-format off
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
// clang-format on
#elif defined(CONF_PLATFORM_MACOS) || defined(CONF_PLATFORM_IOS)
#include <mach/mach.h>
#include <unistd.h>
#elif defined(CONF_FAMILY_UNIX)
#include <unistd.h>
#endif

static constexpr int PROFILER_MAX_COMPONENTS = 256;
static constexpr int PROFILER_MAX_ROWS = 22;
static constexpr int PROFILER_HISTORY_CAP = 120;

struct SProfilerRow
{
	const char *m_pName = nullptr;
	float m_UpdateUs = 0.0f;
	float m_RenderUs = 0.0f;
};

static const CComponent *s_apComponent[PROFILER_MAX_COMPONENTS] = {};
static const char *s_apName[PROFILER_MAX_COMPONENTS] = {};
static int64_t s_aUpdateFrameNs[PROFILER_MAX_COMPONENTS] = {};
static int64_t s_aRenderFrameNs[PROFILER_MAX_COMPONENTS] = {};
static int64_t s_aUpdateAccNs[PROFILER_MAX_COMPONENTS] = {};
static int64_t s_aRenderAccNs[PROFILER_MAX_COMPONENTS] = {};
static int s_ComponentCount = 0;
static int s_PassIndex = 0;
static int s_WindowFrames = 0;
static int64_t s_HeaderStart = 0;
static int64_t s_RowStart = 0;
static bool s_HeaderSeen = false;
static bool s_RowsSeen = false;

static SProfilerRow s_aRows[PROFILER_MAX_COMPONENTS];
static int s_NumRows = 0;

static int64_t s_PendingUpdateNs = 0;
static int64_t s_StageUpdateAccNs = 0;
static int64_t s_StageRenderAccNs = 0;
static int64_t s_StageSwapAccNs = 0;
static int s_StageSamples = 0;
static int64_t s_RowStageUpdateAccNs = 0;
static int64_t s_RowStageRenderAccNs = 0;
static int64_t s_RowStageSwapAccNs = 0;
static int s_RowStageSamples = 0;
static double s_FrameTimeAcc = 0.0;
static int s_FrameSamples = 0;
static double s_RowFrameTimeAcc = 0.0;
static int s_RowFrameSamples = 0;
static int s_RowFpsMin = 0;
static int s_RowFpsMax = 0;
static int s_NetSamples = 0;
static int s_TeeSamples = 0;
static int s_PingSamples = 0;
static int s_PingSum = 0;
static int s_PingMin = 0;
static int s_PingMax = 0;
static int s_PredSum = 0;
static int s_PredMin = 0;
static int s_PredMax = 0;
static int s_LeadSum = 0;
static int s_LeadMin = 0;
static int s_LeadMax = 0;
static int s_SnapGapMax = 0;
static int s_SnapMisses = 0;
static int s_TickJumpMax = 0;
static int s_PredErrSamples = 0;
static int s_PredErrHits = 0;
static float s_PredErrJump = -1.0f;
static bool s_HavePrevErr = false;
static float s_PrevPredErr = 0.0f;
static double s_PredErrSum = 0.0;
static float s_PredErrMax = 0.0f;
static float s_StepMax = -1.0f;
static float s_IntraMin = 0.0f;
static float s_IntraMax = 0.0f;
static bool s_HavePrevPos = false;
static bool s_HavePrevTick = false;
static bool s_HavePrevSpec = false;
static bool s_PrevSpec = false;
static vec2 s_PrevPos = vec2(0.0f, 0.0f);
static int s_PrevGameTick = 0;
static int s_PrevLocalId = -1;

struct SProfilerHistory
{
	time_t m_Unix = 0;
	int m_Fps = 0;
	int m_FpsMin = 0;
	int m_FpsMax = 0;
	float m_FrameUs = 0.0f;
	float m_UpdateUs = 0.0f;
	float m_RenderUs = 0.0f;
	float m_SwapUs = 0.0f;
	float m_RamMiB = 0.0f;
	float m_TexMiB = 0.0f;
	float m_BufMiB = 0.0f;
	float m_StreamMiB = 0.0f;
	float m_StagingMiB = 0.0f;
	int m_NetSamples = 0;
	int m_TeeSamples = 0;
	int m_Ping = -1;
	int m_PingMin = -1;
	int m_PingMax = -1;
	int m_PredMs = 0;
	int m_PredMsMin = 0;
	int m_PredMsMax = 0;
	int m_Lead = 0;
	int m_LeadMin = 0;
	int m_LeadMax = 0;
	int m_SnapGapMax = 0;
	int m_SnapMisses = 0;
	int m_TickJumpMax = 0;
	int m_PredErrHits = 0;
	float m_PredErrJump = -1.0f;
	float m_PredErr = -1.0f;
	float m_PredErrMax = -1.0f;
	float m_StepMax = -1.0f;
	float m_IntraMin = 0.0f;
	float m_IntraMax = 0.0f;
	int m_NumRows = 0;
	SProfilerRow m_aRows[PROFILER_MAX_ROWS];
};

static SProfilerHistory s_aHistory[PROFILER_HISTORY_CAP];
static int s_HistoryCount = 0;
static int s_HistoryNext = 0;
static char s_aNotice[192] = {};
static int64_t s_NoticeUntil = 0;
static bool s_NoticeArmed = false;
static uint64_t s_TexBytes = 0;
static uint64_t s_BufBytes = 0;
static uint64_t s_StreamBytes = 0;
static uint64_t s_StagingBytes = 0;

static int s_PublishedFps = 0;
static float s_PublishedFrameUs = 0.0f;
static float s_PublishedUpdateUs = 0.0f;
static float s_PublishedRenderUs = 0.0f;
static float s_PublishedSwapUs = 0.0f;
static float s_PublishedRamMiB = 0.0f;
static float s_PublishedTexMiB = 0.0f;
static float s_PublishedBufMiB = 0.0f;
static float s_PublishedStreamMiB = 0.0f;
static float s_PublishedStagingMiB = 0.0f;

static int EnsureComponent(const CComponent *pComponent, const char *pName)
{
	if(s_PassIndex < s_ComponentCount && s_apComponent[s_PassIndex] == pComponent)
		return s_PassIndex++;

	for(int Index = 0; Index < s_ComponentCount; Index++)
	{
		if(s_apComponent[Index] == pComponent)
		{
			s_PassIndex = Index + 1;
			return Index;
		}
	}

	if(s_ComponentCount >= PROFILER_MAX_COMPONENTS)
		return -1;

	const int Index = s_ComponentCount++;
	s_apComponent[Index] = pComponent;
	s_apName[Index] = pName;
	s_aUpdateFrameNs[Index] = 0;
	s_aRenderFrameNs[Index] = 0;
	s_aUpdateAccNs[Index] = 0;
	s_aRenderAccNs[Index] = 0;
	s_PassIndex = Index + 1;
	return Index;
}

static uint64_t QueryWorkingSet();

static void ShowNotice(const char *pText)
{
	str_copy(s_aNotice, pText, sizeof(s_aNotice));
	s_NoticeUntil = time_get() + time_freq() * 4;
	s_NoticeArmed = true;
}

static void PublishHeader()
{
	if(s_StageSamples > 0)
	{
		const float InvSamples = 1.0f / static_cast<float>(s_StageSamples);
		s_PublishedUpdateUs = static_cast<float>(s_StageUpdateAccNs / 1000.0) * InvSamples;
		s_PublishedRenderUs = static_cast<float>(s_StageRenderAccNs / 1000.0) * InvSamples;
		s_PublishedSwapUs = static_cast<float>(s_StageSwapAccNs / 1000.0) * InvSamples;
	}
	s_StageUpdateAccNs = 0;
	s_StageRenderAccNs = 0;
	s_StageSwapAccNs = 0;
	s_StageSamples = 0;

	if(s_FrameSamples > 0)
	{
		const float Average = static_cast<float>(s_FrameTimeAcc / s_FrameSamples);
		const float SafeAverage = std::max(Average, 0.000001f);
		s_PublishedFrameUs = SafeAverage * 1000000.0f;
		s_PublishedFps = round_to_int(1.0f / SafeAverage);
	}
	s_FrameTimeAcc = 0.0;
	s_FrameSamples = 0;

	const float ToMiB = 1.0f / (1024.0f * 1024.0f);
	s_PublishedRamMiB = static_cast<float>(QueryWorkingSet()) * ToMiB;
	s_PublishedTexMiB = static_cast<float>(s_TexBytes) * ToMiB;
	s_PublishedBufMiB = static_cast<float>(s_BufBytes) * ToMiB;
	s_PublishedStreamMiB = static_cast<float>(s_StreamBytes) * ToMiB;
	s_PublishedStagingMiB = static_cast<float>(s_StagingBytes) * ToMiB;
	s_HeaderSeen = true;
}

static void ClearNetWindow()
{
	s_NetSamples = 0;
	s_TeeSamples = 0;
	s_PingSamples = 0;
	s_PingSum = 0;
	s_PingMin = 0;
	s_PingMax = 0;
	s_PredSum = 0;
	s_PredMin = 0;
	s_PredMax = 0;
	s_LeadSum = 0;
	s_LeadMin = 0;
	s_LeadMax = 0;
	s_SnapGapMax = 0;
	s_SnapMisses = 0;
	s_TickJumpMax = 0;
	s_PredErrSamples = 0;
	s_PredErrHits = 0;
	s_PredErrJump = -1.0f;
	s_PredErrSum = 0.0;
	s_PredErrMax = 0.0f;
	s_StepMax = -1.0f;
	s_IntraMin = 0.0f;
	s_IntraMax = 0.0f;
}

static void PushHistory()
{
	SProfilerHistory &Sample = s_aHistory[s_HistoryNext];
	Sample.m_Unix = time(nullptr);
	Sample.m_Fps = s_PublishedFps;
	Sample.m_FpsMin = s_RowFrameSamples > 0 ? s_RowFpsMin : s_PublishedFps;
	Sample.m_FpsMax = s_RowFrameSamples > 0 ? s_RowFpsMax : s_PublishedFps;
	if(s_RowFrameSamples > 0)
	{
		const float Average = static_cast<float>(s_RowFrameTimeAcc / s_RowFrameSamples);
		const float SafeAverage = std::max(Average, 0.000001f);
		Sample.m_FrameUs = SafeAverage * 1000000.0f;
		Sample.m_Fps = round_to_int(1.0f / SafeAverage);
	}
	else
	{
		Sample.m_FrameUs = s_PublishedFrameUs;
	}
	if(s_RowStageSamples > 0)
	{
		const float InvSamples = 1.0f / static_cast<float>(s_RowStageSamples);
		Sample.m_UpdateUs = static_cast<float>(s_RowStageUpdateAccNs / 1000.0) * InvSamples;
		Sample.m_RenderUs = static_cast<float>(s_RowStageRenderAccNs / 1000.0) * InvSamples;
		Sample.m_SwapUs = static_cast<float>(s_RowStageSwapAccNs / 1000.0) * InvSamples;
	}
	else
	{
		Sample.m_UpdateUs = s_PublishedUpdateUs;
		Sample.m_RenderUs = s_PublishedRenderUs;
		Sample.m_SwapUs = s_PublishedSwapUs;
	}
	Sample.m_RamMiB = s_PublishedRamMiB;
	Sample.m_TexMiB = s_PublishedTexMiB;
	Sample.m_BufMiB = s_PublishedBufMiB;
	Sample.m_StreamMiB = s_PublishedStreamMiB;
	Sample.m_StagingMiB = s_PublishedStagingMiB;
	Sample.m_NetSamples = s_NetSamples;
	Sample.m_TeeSamples = s_TeeSamples;
	if(s_PingSamples > 0)
	{
		Sample.m_Ping = s_PingSum / s_PingSamples;
		Sample.m_PingMin = s_PingMin;
		Sample.m_PingMax = s_PingMax;
	}
	else
	{
		Sample.m_Ping = -1;
		Sample.m_PingMin = -1;
		Sample.m_PingMax = -1;
	}
	if(s_NetSamples > 0)
	{
		Sample.m_PredMs = s_PredSum / s_NetSamples;
		Sample.m_PredMsMin = s_PredMin;
		Sample.m_PredMsMax = s_PredMax;
		Sample.m_Lead = s_LeadSum / s_NetSamples;
		Sample.m_LeadMin = s_LeadMin;
		Sample.m_LeadMax = s_LeadMax;
		Sample.m_IntraMin = s_IntraMin;
		Sample.m_IntraMax = s_IntraMax;
	}
	else
	{
		Sample.m_PredMs = 0;
		Sample.m_PredMsMin = 0;
		Sample.m_PredMsMax = 0;
		Sample.m_Lead = 0;
		Sample.m_LeadMin = 0;
		Sample.m_LeadMax = 0;
		Sample.m_IntraMin = 0.0f;
		Sample.m_IntraMax = 0.0f;
	}
	Sample.m_SnapGapMax = s_SnapGapMax;
	Sample.m_SnapMisses = s_SnapMisses;
	Sample.m_TickJumpMax = s_TickJumpMax;
	Sample.m_PredErrHits = s_PredErrHits;
	Sample.m_PredErrJump = s_PredErrJump;
	if(s_PredErrSamples > 0)
	{
		Sample.m_PredErr = static_cast<float>(s_PredErrSum / s_PredErrSamples);
		Sample.m_PredErrMax = s_PredErrMax;
	}
	else
	{
		Sample.m_PredErr = -1.0f;
		Sample.m_PredErrMax = -1.0f;
	}
	Sample.m_StepMax = s_StepMax;
	Sample.m_NumRows = std::min(s_NumRows, PROFILER_MAX_ROWS);
	for(int Index = 0; Index < Sample.m_NumRows; Index++)
		Sample.m_aRows[Index] = s_aRows[Index];

	s_HistoryNext = (s_HistoryNext + 1) % PROFILER_HISTORY_CAP;
	if(s_HistoryCount < PROFILER_HISTORY_CAP)
		s_HistoryCount++;

	s_RowStageUpdateAccNs = 0;
	s_RowStageRenderAccNs = 0;
	s_RowStageSwapAccNs = 0;
	s_RowStageSamples = 0;
	s_RowFrameTimeAcc = 0.0;
	s_RowFrameSamples = 0;
	s_RowFpsMin = 0;
	s_RowFpsMax = 0;
	ClearNetWindow();
}

static void PublishRows()
{
	s_NumRows = 0;
	if(s_WindowFrames > 0)
	{
		const float InvFrames = 1.0f / static_cast<float>(s_WindowFrames);
		for(int Index = 0; Index < s_ComponentCount; Index++)
		{
			SProfilerRow &Row = s_aRows[s_NumRows++];
			Row.m_pName = s_apName[Index];
			Row.m_UpdateUs = static_cast<float>(s_aUpdateAccNs[Index] / 1000.0) * InvFrames;
			Row.m_RenderUs = static_cast<float>(s_aRenderAccNs[Index] / 1000.0) * InvFrames;
		}
		std::sort(s_aRows, s_aRows + s_NumRows, [](const SProfilerRow &A, const SProfilerRow &B) {
			return (A.m_UpdateUs + A.m_RenderUs) > (B.m_UpdateUs + B.m_RenderUs);
		});
	}
	mem_zero(s_aUpdateAccNs, sizeof(s_aUpdateAccNs));
	mem_zero(s_aRenderAccNs, sizeof(s_aRenderAccNs));
	s_WindowFrames = 0;
	PushHistory();
	s_RowsSeen = s_NumRows > 0;
}

static void TryPublishSnapshot()
{
	const int64_t Now = time_get();
	if(s_HeaderStart == 0)
		s_HeaderStart = Now;
	if(s_RowStart == 0)
		s_RowStart = Now;

	const int64_t HeaderInterval = s_HeaderSeen ? time_freq() / 5 : time_freq() / 8;
	if(Now - s_HeaderStart >= HeaderInterval)
	{
		PublishHeader();
		s_HeaderStart = Now;
	}

	const int64_t RowInterval = s_RowsSeen ? time_freq() / 2 : time_freq() / 4;
	if(Now - s_RowStart >= RowInterval)
	{
		PublishRows();
		s_RowStart = Now;
	}
}

void BestClientProfilerReset()
{
	s_ComponentCount = 0;
	s_PassIndex = 0;
	s_WindowFrames = 0;
	s_HeaderStart = 0;
	s_RowStart = 0;
	s_HeaderSeen = false;
	s_RowsSeen = false;
	s_NumRows = 0;
	s_PendingUpdateNs = 0;
	s_StageUpdateAccNs = 0;
	s_StageRenderAccNs = 0;
	s_StageSwapAccNs = 0;
	s_StageSamples = 0;
	s_RowStageUpdateAccNs = 0;
	s_RowStageRenderAccNs = 0;
	s_RowStageSwapAccNs = 0;
	s_RowStageSamples = 0;
	s_FrameTimeAcc = 0.0;
	s_FrameSamples = 0;
	s_RowFrameTimeAcc = 0.0;
	s_RowFrameSamples = 0;
	s_RowFpsMin = 0;
	s_RowFpsMax = 0;
	ClearNetWindow();
	s_HavePrevPos = false;
	s_HavePrevTick = false;
	s_HavePrevErr = false;
	s_HavePrevSpec = false;
	s_PrevSpec = false;
	s_PrevPredErr = 0.0f;
	s_PrevPos = vec2(0.0f, 0.0f);
	s_PrevGameTick = 0;
	s_PrevLocalId = -1;
	s_TexBytes = 0;
	s_BufBytes = 0;
	s_StreamBytes = 0;
	s_StagingBytes = 0;
	s_PublishedFps = 0;
	s_PublishedFrameUs = 0.0f;
	s_PublishedUpdateUs = 0.0f;
	s_PublishedRenderUs = 0.0f;
	s_PublishedSwapUs = 0.0f;
	s_PublishedRamMiB = 0.0f;
	s_PublishedTexMiB = 0.0f;
	s_PublishedBufMiB = 0.0f;
	s_PublishedStreamMiB = 0.0f;
	s_PublishedStagingMiB = 0.0f;
	mem_zero(s_apComponent, sizeof(s_apComponent));
	mem_zero(s_apName, sizeof(s_apName));
	mem_zero(s_aUpdateFrameNs, sizeof(s_aUpdateFrameNs));
	mem_zero(s_aRenderFrameNs, sizeof(s_aRenderFrameNs));
	mem_zero(s_aUpdateAccNs, sizeof(s_aUpdateAccNs));
	mem_zero(s_aRenderAccNs, sizeof(s_aRenderAccNs));
}

void BestClientProfilerAddClientUpdate(int64_t Nanoseconds)
{
	s_PendingUpdateNs += Nanoseconds;
}

void BestClientProfilerSetFrameStages(int64_t RenderNanoseconds, int64_t SwapNanoseconds)
{
	s_StageUpdateAccNs += s_PendingUpdateNs;
	s_RowStageUpdateAccNs += s_PendingUpdateNs;
	s_PendingUpdateNs = 0;
	s_StageRenderAccNs += RenderNanoseconds;
	s_StageSwapAccNs += SwapNanoseconds;
	s_RowStageRenderAccNs += RenderNanoseconds;
	s_RowStageSwapAccNs += SwapNanoseconds;
	s_StageSamples++;
	s_RowStageSamples++;
	TryPublishSnapshot();
}

void BestClientProfilerBeginPass()
{
	s_PassIndex = 0;
}

static bool AddSample(const CComponent *pComponent, int64_t Nanoseconds, int64_t *pSamples)
{
	const int Index = EnsureComponent(pComponent, nullptr);
	if(Index < 0)
		return false;
	pSamples[Index] += Nanoseconds;
	return s_apName[Index] == nullptr;
}

bool BestClientProfilerAddUpdate(const CComponent *pComponent, int64_t Nanoseconds)
{
	return AddSample(pComponent, Nanoseconds, s_aUpdateFrameNs);
}

bool BestClientProfilerAddRender(const CComponent *pComponent, int64_t Nanoseconds)
{
	return AddSample(pComponent, Nanoseconds, s_aRenderFrameNs);
}

void BestClientProfilerSetComponentName(const CComponent *pComponent, const char *pName)
{
	if(s_PassIndex <= 0 || pName == nullptr)
		return;
	const int Index = s_PassIndex - 1;
	if(s_apComponent[Index] == pComponent && s_apName[Index] == nullptr)
		s_apName[Index] = pName;
}

void BestClientProfilerObserve(CGameClient *pGameClient)
{
	if(pGameClient == nullptr)
		return;

	IClient *pClient = pGameClient->Client();
	const int State = pClient->State();
	if(State != IClient::STATE_ONLINE && State != IClient::STATE_DEMOPLAYBACK)
	{
		s_HavePrevPos = false;
		s_HavePrevTick = false;
		s_HavePrevErr = false;
		s_HavePrevSpec = false;
		return;
	}

	const int Conn = g_Config.m_ClDummy;
	const int GameTick = pClient->GameTick(Conn);
	const int PrevTick = pClient->PrevGameTick(Conn);
	const int Lead = pClient->PredGameTick(Conn) - GameTick;
	const int PredMs = pClient->GetPredictionTime();
	const float Intra = pClient->IntraGameTick(Conn);

	int SnapGap = 0;
	if(PrevTick > 0 && GameTick >= PrevTick)
		SnapGap = GameTick - PrevTick;

	int TickJump = 0;
	if(s_HavePrevTick && GameTick >= s_PrevGameTick)
		TickJump = GameTick - s_PrevGameTick;
	s_PrevGameTick = GameTick;
	s_HavePrevTick = GameTick > 0;

	const int LocalId = pGameClient->m_Snap.m_LocalClientId;
	const bool Spec = pGameClient->m_Snap.m_SpecInfo.m_Active;
	float PredErr = -1.0f;
	float Step = -1.0f;
	bool HavePos = false;
	vec2 Pos = pGameClient->m_LocalCharacterPos;
	if(!Spec && LocalId >= 0 && pGameClient->m_Snap.m_aCharacters[LocalId].m_Active)
	{
		HavePos = true;
		const auto &Character = pGameClient->m_Snap.m_aCharacters[LocalId];
		const vec2 ServerPos = mix(
			vec2(Character.m_Prev.m_X, Character.m_Prev.m_Y),
			vec2(Character.m_Cur.m_X, Character.m_Cur.m_Y),
			Intra);
		PredErr = distance(Pos, ServerPos);
		if(s_HavePrevErr && s_HavePrevPos && s_PrevLocalId == LocalId)
		{
			const float ErrJump = absolute(PredErr - s_PrevPredErr);
			s_PredErrJump = std::max(s_PredErrJump, ErrJump);
			if(ErrJump >= 32.0f)
				s_PredErrHits++;
		}
		s_PrevPredErr = PredErr;
		s_HavePrevErr = true;
	}
	else if(Spec && pGameClient->m_Snap.m_SpecInfo.m_UsePosition)
	{
		Pos = pGameClient->m_Snap.m_SpecInfo.m_Position;
		HavePos = true;
		s_HavePrevErr = false;
	}
	else
	{
		s_HavePrevErr = false;
	}

	if(HavePos)
	{
		if(s_HavePrevPos && s_PrevLocalId == LocalId && s_HavePrevSpec && s_PrevSpec == Spec)
			Step = distance(Pos, s_PrevPos);
		s_PrevSpec = Spec;
		s_HavePrevSpec = true;
		s_PrevPos = Pos;
		s_HavePrevPos = true;
		s_PrevLocalId = LocalId;
	}
	else
	{
		s_HavePrevPos = false;
		s_HavePrevErr = false;
		s_HavePrevSpec = false;
		s_PrevLocalId = -1;
	}

	if(s_NetSamples == 0)
	{
		s_PredMin = PredMs;
		s_PredMax = PredMs;
		s_LeadMin = Lead;
		s_LeadMax = Lead;
		s_IntraMin = Intra;
		s_IntraMax = Intra;
	}
	else
	{
		s_PredMin = std::min(s_PredMin, PredMs);
		s_PredMax = std::max(s_PredMax, PredMs);
		s_LeadMin = std::min(s_LeadMin, Lead);
		s_LeadMax = std::max(s_LeadMax, Lead);
		s_IntraMin = std::min(s_IntraMin, Intra);
		s_IntraMax = std::max(s_IntraMax, Intra);
	}
	s_NetSamples++;
	s_PredSum += PredMs;
	s_LeadSum += Lead;
	s_SnapGapMax = std::max(s_SnapGapMax, SnapGap);
	if(SnapGap >= 3)
		s_SnapMisses++;
	s_TickJumpMax = std::max(s_TickJumpMax, TickJump);

	if(pGameClient->m_Snap.m_pLocalInfo != nullptr)
	{
		const int Ping = std::max(pGameClient->m_Snap.m_pLocalInfo->m_Latency, 0);
		if(s_PingSamples == 0)
		{
			s_PingMin = Ping;
			s_PingMax = Ping;
		}
		else
		{
			s_PingMin = std::min(s_PingMin, Ping);
			s_PingMax = std::max(s_PingMax, Ping);
		}
		s_PingSamples++;
		s_PingSum += Ping;
	}

	if(!Spec && PredErr >= 0.0f)
	{
		s_TeeSamples++;
		s_PredErrSum += PredErr;
		s_PredErrSamples++;
		s_PredErrMax = std::max(s_PredErrMax, PredErr);
	}
	if(Step >= 0.0f)
		s_StepMax = std::max(s_StepMax, Step);
}

void BestClientProfilerCommitFrame()
{
	for(int Index = 0; Index < s_ComponentCount; Index++)
	{
		s_aUpdateAccNs[Index] += s_aUpdateFrameNs[Index];
		s_aRenderAccNs[Index] += s_aRenderFrameNs[Index];
		s_aUpdateFrameNs[Index] = 0;
		s_aRenderFrameNs[Index] = 0;
	}
	s_WindowFrames++;
}

static uint64_t QueryWorkingSet()
{
#if defined(CONF_FAMILY_WINDOWS)
	PROCESS_MEMORY_COUNTERS Counters;
	mem_zero(&Counters, sizeof(Counters));
	Counters.cb = sizeof(Counters);
	if(!GetProcessMemoryInfo(GetCurrentProcess(), &Counters, sizeof(Counters)))
		return 0;
	return Counters.WorkingSetSize;
#elif defined(CONF_PLATFORM_MACOS) || defined(CONF_PLATFORM_IOS)
	mach_task_basic_info Info;
	mach_msg_type_number_t Count = MACH_TASK_BASIC_INFO_COUNT;
	if(task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&Info), &Count) != KERN_SUCCESS)
		return 0;
	return Info.resident_size;
#elif defined(CONF_FAMILY_UNIX)
	FILE *pFile = std::fopen("/proc/self/statm", "r");
	if(pFile == nullptr)
		return 0;
	unsigned long SizePages = 0;
	unsigned long ResidentPages = 0;
	const int Read = std::fscanf(pFile, "%lu %lu", &SizePages, &ResidentPages);
	std::fclose(pFile);
	(void)SizePages;
	if(Read != 2)
		return 0;
	const long PageSize = sysconf(_SC_PAGESIZE);
	if(PageSize <= 0)
		return 0;
	return static_cast<uint64_t>(ResidentPages) * static_cast<uint64_t>(PageSize);
#else
	return 0;
#endif
}

static void FormatUs(char *pBuf, int Size, float Us)
{
	if(Us >= 100.0f)
		str_format(pBuf, Size, "%.0f us", Us);
	else
		str_format(pBuf, Size, "%.1f us", Us);
}

static ColorRGBA CostColor(float Share)
{
	if(Share >= 0.15f)
		return ColorRGBA(1.0f, 0.38f, 0.32f, 1.0f);
	if(Share >= 0.05f)
		return ColorRGBA(1.0f, 0.82f, 0.28f, 1.0f);
	return ColorRGBA(0.48f, 0.9f, 0.48f, 1.0f);
}

void BestClientRenderProfiler(IGraphics *pGraphics, ITextRender *pTextRender, float FrameTimeAverage)
{
	s_FrameTimeAcc += FrameTimeAverage;
	s_FrameSamples++;
	s_RowFrameTimeAcc += FrameTimeAverage;
	s_RowFrameSamples++;
	{
		const int InstantFps = round_to_int(1.0f / std::max(FrameTimeAverage, 0.000001f));
		if(s_RowFrameSamples == 1 || InstantFps < s_RowFpsMin)
			s_RowFpsMin = InstantFps;
		if(InstantFps > s_RowFpsMax)
			s_RowFpsMax = InstantFps;
	}
	s_TexBytes = pGraphics->TextureMemoryUsage();
	s_BufBytes = pGraphics->BufferMemoryUsage();
	s_StreamBytes = pGraphics->StreamedMemoryUsage();
	s_StagingBytes = pGraphics->StagingMemoryUsage();

	pGraphics->MapScreenToSize(pGraphics->ScreenWidth(), pGraphics->ScreenHeight());
	const float ScreenW = pGraphics->ScreenWidth();
	const float ScreenH = pGraphics->ScreenHeight();
	const float Margin = std::round(ScreenW / 100.0f);
	const float FontSize = 12.0f;
	const float Line = FontSize + 4.0f;
	const float PanelW = std::round(std::min(ScreenW * 0.36f, 520.0f));
	const float X = Margin;
	const float Y = Margin;
	const float Pad = 8.0f;
	const float NameW = 150.0f;
	const float ValueW = 118.0f;
	const float BarX = X + Pad + NameW;
	const float BarW = std::max(24.0f, PanelW - Pad * 2.0f - NameW - ValueW);
	const int FitRows = std::max(4, (int)((ScreenH - Y - Line * 8.0f - Margin) / Line));
	const int RowLimit = std::min(PROFILER_MAX_ROWS, FitRows);

	int Visible = 0;
	float MaxUs = 1.0f;
	for(int Index = 0; Index < s_NumRows && Visible < RowLimit; Index++)
	{
		const float TotalUs = s_aRows[Index].m_UpdateUs + s_aRows[Index].m_RenderUs;
		if(Visible >= 8 && TotalUs < 1.0f)
			break;
		MaxUs = std::max(MaxUs, TotalUs);
		Visible++;
	}

	const float HeaderH = Line * 6.0f + 8.0f;
	const float PanelH = HeaderH + (Visible > 0 ? Visible * Line : Line) + Pad;
	const float FrameUs = std::max(s_PublishedFrameUs, 1.0f);
	const int Fps = s_PublishedFps;

	pGraphics->TextureClear();
	pGraphics->QuadsBegin();
	pGraphics->SetColor(0.0f, 0.0f, 0.0f, 0.72f);
	{
		const IGraphics::CQuadItem Panel(X, Y, PanelW, PanelH);
		pGraphics->QuadsDrawTL(&Panel, 1);
	}

	const ColorRGBA RenderColor(0.25f, 0.72f, 0.95f, 1.0f);
	const ColorRGBA UpdateColor(0.95f, 0.55f, 0.20f, 1.0f);
	float RowY = Y + HeaderH;
	for(int Index = 0; Index < Visible; Index++)
	{
		const float RenderUs = s_aRows[Index].m_RenderUs;
		const float UpdateUs = s_aRows[Index].m_UpdateUs;
		const float TotalUs = RenderUs + UpdateUs;
		const float Span = std::max(TotalUs / MaxUs, 0.0f) * BarW;
		const float RenderW = TotalUs > 0.0f ? Span * (RenderUs / TotalUs) : 0.0f;
		const float UpdateW = Span - RenderW;
		pGraphics->SetColor(1.0f, 1.0f, 1.0f, 0.08f);
		const IGraphics::CQuadItem Track(BarX, RowY + 3.0f, BarW, FontSize - 2.0f);
		pGraphics->QuadsDrawTL(&Track, 1);
		if(RenderW > 0.0f)
		{
			pGraphics->SetColor(RenderColor);
			const IGraphics::CQuadItem RenderBar(BarX, RowY + 3.0f, RenderW, FontSize - 2.0f);
			pGraphics->QuadsDrawTL(&RenderBar, 1);
		}
		if(UpdateW > 0.0f)
		{
			pGraphics->SetColor(UpdateColor);
			const IGraphics::CQuadItem UpdateBar(BarX + RenderW, RowY + 3.0f, UpdateW, FontSize - 2.0f);
			pGraphics->QuadsDrawTL(&UpdateBar, 1);
		}
		RowY += Line;
	}
	pGraphics->QuadsEnd();

	pTextRender->TextColor(pTextRender->DefaultTextColor());
	float TextY = Y + 4.0f;
	pTextRender->Text(X + Pad, TextY, FontSize, Localize("Press Ctrl+Shift+P to disable profiler."));
	TextY += Line;

	char aBuf[192];
	str_format(aBuf, sizeof(aBuf), "FPS %d    Frame %.0f us", Fps, FrameUs);
	pTextRender->Text(X + Pad, TextY, FontSize, aBuf);
	TextY += Line;

	char aUpdate[32];
	char aRender[32];
	char aSwap[32];
	FormatUs(aUpdate, sizeof(aUpdate), s_PublishedUpdateUs);
	FormatUs(aRender, sizeof(aRender), s_PublishedRenderUs);
	FormatUs(aSwap, sizeof(aSwap), s_PublishedSwapUs);
	str_format(aBuf, sizeof(aBuf), "Update %s    Render %s    Swap %s", aUpdate, aRender, aSwap);
	pTextRender->Text(X + Pad, TextY, FontSize, aBuf);
	TextY += Line;

	str_format(aBuf, sizeof(aBuf), "RAM %.1f MiB    Textures %.1f    Buffers %.1f",
		s_PublishedRamMiB,
		s_PublishedTexMiB,
		s_PublishedBufMiB);
	pTextRender->Text(X + Pad, TextY, FontSize, aBuf);
	TextY += Line;
	str_format(aBuf, sizeof(aBuf), "Streamed %.1f MiB    Staging %.1f",
		s_PublishedStreamMiB,
		s_PublishedStagingMiB);
	pTextRender->Text(X + Pad, TextY, FontSize, aBuf);
	TextY += Line;

	pTextRender->TextColor(RenderColor);
	pTextRender->Text(X + Pad, TextY, FontSize, "Render");
	pTextRender->TextColor(UpdateColor);
	pTextRender->Text(X + Pad + 64.0f, TextY, FontSize, "Update");
	pTextRender->TextColor(pTextRender->DefaultTextColor());
	pTextRender->Text(X + Pad + 140.0f, TextY, FontSize, Localize("share of the frame"));
	TextY += Line + 4.0f;

	if(Visible == 0)
	{
		pTextRender->Text(X + Pad, TextY, FontSize, Localize("Collecting samples..."));
		return;
	}

	for(int Index = 0; Index < Visible; Index++)
	{
		const SProfilerRow &Row = s_aRows[Index];
		const float TotalUs = Row.m_UpdateUs + Row.m_RenderUs;
		const float Share = FrameUs > 0.0f ? TotalUs / FrameUs : 0.0f;
		char aName[22];
		str_copy(aName, Row.m_pName != nullptr ? Row.m_pName : "Unknown", sizeof(aName));
		pTextRender->TextColor(CostColor(Share));
		pTextRender->Text(X + Pad, TextY, FontSize, aName);
		char aValue[48];
		char aUs[32];
		FormatUs(aUs, sizeof(aUs), TotalUs);
		str_format(aValue, sizeof(aValue), "%s  %.1f%%", aUs, Share * 100.0f);
		pTextRender->TextColor(pTextRender->DefaultTextColor());
		pTextRender->Text(BarX + BarW + 6.0f, TextY, FontSize, aValue);
		TextY += Line;
	}
}

static void WriteLine(IOHANDLE File, const char *pLine)
{
	io_write(File, pLine, str_length(pLine));
	io_write_newline(File);
}

static void AppendFlag(char *pBuf, int Size, const char *pFlag)
{
	if(pBuf[0] != '\0')
		str_append(pBuf, ", ", Size);
	str_append(pBuf, pFlag, Size);
}

static void PlayFlags(char *pBuf, int Size, const SProfilerHistory &Sample)
{
	pBuf[0] = '\0';
	if(Sample.m_NetSamples <= 0)
		return;
	if(Sample.m_PingMin >= 0 && Sample.m_PingMax - Sample.m_PingMin >= 25)
		AppendFlag(pBuf, Size, "ping spike");
	if(Sample.m_SnapGapMax >= 3)
		AppendFlag(pBuf, Size, "missed snapshots");
	if(Sample.m_LeadMin <= 1 && Sample.m_PredMsMax >= 10)
		AppendFlag(pBuf, Size, "prediction caught up");
	if(Sample.m_PredErrJump >= 32.0f)
		AppendFlag(pBuf, Size, "prediction correction");
	if(Sample.m_FpsMin < 30 || Sample.m_FrameUs >= 50000.0f)
		AppendFlag(pBuf, Size, "frame hitch");
	if(Sample.m_StepMax >= 96.0f)
		AppendFlag(pBuf, Size, "large position step");
	if(Sample.m_IntraMax > 2.0f || Sample.m_IntraMin < -0.5f)
		AppendFlag(pBuf, Size, "interpolation outside snapshots");
}

static void WritePlaySample(IOHANDLE File, const SProfilerHistory &Sample)
{
	char aLine[512];
	if(Sample.m_NetSamples <= 0)
	{
		WriteLine(File, "Play: not in game");
		return;
	}

	if(Sample.m_Ping >= 0)
		str_format(aLine, sizeof(aLine), "Ping %d (%d-%d) ms", Sample.m_Ping, Sample.m_PingMin, Sample.m_PingMax);
	else
		str_copy(aLine, "Ping n/a", sizeof(aLine));
	WriteLine(File, aLine);

	str_format(aLine, sizeof(aLine), "Prediction %d (%d-%d) ms   Tick lead %d (%d-%d)   Snapshot gap %d (%d frames)   Tick jump %d",
		Sample.m_PredMs,
		Sample.m_PredMsMin,
		Sample.m_PredMsMax,
		Sample.m_Lead,
		Sample.m_LeadMin,
		Sample.m_LeadMax,
		Sample.m_SnapGapMax,
		Sample.m_SnapMisses,
		Sample.m_TickJumpMax);
	WriteLine(File, aLine);

	if(Sample.m_PredErr >= 0.0f && Sample.m_PredErrJump >= 0.0f)
		str_format(aLine, sizeof(aLine), "Prediction error avg %.1f max %.1f   largest correction %.1f (%d frames over 1 tile)",
			Sample.m_PredErr,
			Sample.m_PredErrMax,
			Sample.m_PredErrJump,
			Sample.m_PredErrHits);
	else if(Sample.m_PredErr >= 0.0f)
		str_format(aLine, sizeof(aLine), "Prediction error avg %.1f max %.1f   largest correction n/a",
			Sample.m_PredErr,
			Sample.m_PredErrMax);
	else
		str_copy(aLine, "Prediction error n/a", sizeof(aLine));
	WriteLine(File, aLine);

	if(Sample.m_StepMax >= 0.0f)
		str_format(aLine, sizeof(aLine), "%s step max %.1f   Intra %.2f..%.2f",
			Sample.m_TeeSamples > 0 ? "Tee" : "Camera",
			Sample.m_StepMax,
			Sample.m_IntraMin,
			Sample.m_IntraMax);
	else
		str_format(aLine, sizeof(aLine), "Position step n/a   Intra %.2f..%.2f", Sample.m_IntraMin, Sample.m_IntraMax);
	WriteLine(File, aLine);

	PlayFlags(aLine, sizeof(aLine), Sample);
	if(aLine[0] != '\0')
	{
		char aFlags[512];
		str_format(aFlags, sizeof(aFlags), "Hints: %s", aLine);
		WriteLine(File, aFlags);
	}
}

bool BestClientProfilerHasNotice()
{
	return s_NoticeArmed;
}

void BestClientRenderProfilerNotice(IGraphics *pGraphics, ITextRender *pTextRender)
{
	if(!s_NoticeArmed)
		return;
	if(time_get() >= s_NoticeUntil)
	{
		s_aNotice[0] = '\0';
		s_NoticeArmed = false;
		return;
	}

	pGraphics->MapScreenToSize(pGraphics->ScreenWidth(), pGraphics->ScreenHeight());
	const float FontSize = 16.0f;
	const float TextW = pTextRender->TextWidth(FontSize, s_aNotice);
	const float Pad = 12.0f;
	const float Width = TextW + Pad * 2.0f;
	const float Height = FontSize + Pad;
	const float X = std::round((pGraphics->ScreenWidth() - Width) * 0.5f);
	const float Y = 24.0f;

	pGraphics->TextureClear();
	pGraphics->QuadsBegin();
	pGraphics->SetColor(0.05f, 0.12f, 0.08f, 0.92f);
	const IGraphics::CQuadItem Panel(X, Y, Width, Height);
	pGraphics->QuadsDrawTL(&Panel, 1);
	pGraphics->SetColor(0.35f, 0.9f, 0.45f, 1.0f);
	const IGraphics::CQuadItem Accent(X, Y, 4.0f, Height);
	pGraphics->QuadsDrawTL(&Accent, 1);
	pGraphics->QuadsEnd();

	pTextRender->TextColor(ColorRGBA(0.75f, 1.0f, 0.8f, 1.0f));
	pTextRender->Text(X + Pad, Y + (Height - FontSize) * 0.5f, FontSize, s_aNotice);
	pTextRender->TextColor(pTextRender->DefaultTextColor());
}

bool BestClientProfilerDump(IStorage *pStorage)
{
	const time_t Cutoff = time(nullptr) - 70;
	int aIndex[PROFILER_HISTORY_CAP];
	int Num = 0;
	const int Start = s_HistoryCount < PROFILER_HISTORY_CAP ? 0 : s_HistoryNext;
	for(int Step = 0; Step < s_HistoryCount; Step++)
	{
		const int Index = (Start + Step) % PROFILER_HISTORY_CAP;
		if(s_aHistory[Index].m_Unix >= Cutoff)
			aIndex[Num++] = Index;
	}
	if(Num <= 0)
	{
		ShowNotice(Localize("No profiler data for the last minute"));
		return false;
	}

	char aTimestamp[32];
	str_timestamp(aTimestamp, sizeof(aTimestamp));
	char aFilename[IO_MAX_PATH_LENGTH];
	str_format(aFilename, sizeof(aFilename), "dumps/profiler_dump_%s.txt", aTimestamp);
	IOHANDLE File = pStorage->OpenFile(aFilename, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
	{
		ShowNotice(Localize("Could not save profiler dump"));
		return false;
	}

	double FpsSum = 0.0;
	int FpsMin = s_aHistory[aIndex[0]].m_FpsMin;
	int FpsMax = s_aHistory[aIndex[0]].m_FpsMax;
	double FrameSum = 0.0;
	double UpdateSum = 0.0;
	double RenderSum = 0.0;
	double SwapSum = 0.0;
	for(int SampleIndex = 0; SampleIndex < Num; SampleIndex++)
	{
		const SProfilerHistory &Sample = s_aHistory[aIndex[SampleIndex]];
		FpsSum += Sample.m_Fps;
		FpsMin = std::min(FpsMin, Sample.m_FpsMin);
		FpsMax = std::max(FpsMax, Sample.m_FpsMax);
		FrameSum += Sample.m_FrameUs;
		UpdateSum += Sample.m_UpdateUs;
		RenderSum += Sample.m_RenderUs;
		SwapSum += Sample.m_SwapUs;
	}
	const double Inv = 1.0 / Num;

	struct SComponentSum
	{
		const char *m_pName = nullptr;
		double m_UpdateUs = 0.0;
		double m_RenderUs = 0.0;
	};
	SComponentSum aSums[PROFILER_MAX_COMPONENTS];
	int NumSums = 0;
	for(int SampleIndex = 0; SampleIndex < Num; SampleIndex++)
	{
		const SProfilerHistory &Sample = s_aHistory[aIndex[SampleIndex]];
		for(int Row = 0; Row < Sample.m_NumRows; Row++)
		{
			const char *pName = Sample.m_aRows[Row].m_pName != nullptr ? Sample.m_aRows[Row].m_pName : "Unknown";
			int Found = -1;
			for(int Sum = 0; Sum < NumSums; Sum++)
			{
				if(str_comp(aSums[Sum].m_pName, pName) == 0)
				{
					Found = Sum;
					break;
				}
			}
			if(Found < 0)
			{
				if(NumSums >= PROFILER_MAX_COMPONENTS)
					continue;
				Found = NumSums++;
				aSums[Found].m_pName = pName;
			}
			aSums[Found].m_UpdateUs += Sample.m_aRows[Row].m_UpdateUs;
			aSums[Found].m_RenderUs += Sample.m_aRows[Row].m_RenderUs;
		}
	}
	std::sort(aSums, aSums + NumSums, [](const SComponentSum &A, const SComponentSum &B) {
		return (A.m_UpdateUs + A.m_RenderUs) > (B.m_UpdateUs + B.m_RenderUs);
	});

	char aFrom[32];
	char aTo[32];
	str_timestamp_ex(s_aHistory[aIndex[0]].m_Unix, aFrom, sizeof(aFrom), "%H:%M:%S");
	str_timestamp_ex(s_aHistory[aIndex[Num - 1]].m_Unix, aTo, sizeof(aTo), "%H:%M:%S");

	char aLine[256];
	WriteLine(File, "BestClient profiler dump");
	str_format(aLine, sizeof(aLine), "Samples: %d", Num);
	WriteLine(File, aLine);
	str_format(aLine, sizeof(aLine), "From: %s", aFrom);
	WriteLine(File, aLine);
	str_format(aLine, sizeof(aLine), "To: %s", aTo);
	WriteLine(File, aLine);
	str_format(aLine, sizeof(aLine), "FPS avg/min/max: %.0f / %d / %d", FpsSum * Inv, FpsMin, FpsMax);
	WriteLine(File, aLine);
	str_format(aLine, sizeof(aLine), "Frame avg: %.0f us", FrameSum * Inv);
	WriteLine(File, aLine);
	str_format(aLine, sizeof(aLine), "Update/Render/Swap avg: %.0f / %.0f / %.0f us", UpdateSum * Inv, RenderSum * Inv, SwapSum * Inv);
	WriteLine(File, aLine);
	WriteLine(File, "");
	WriteLine(File, "Play");
	WriteLine(File, "Ping is the scoreboard latency in ms. A wide min-max is a ping spike.");
	WriteLine(File, "Prediction is how many ms the local tee is simulated ahead of the server.");
	WriteLine(File, "Tick lead is the predicted tick minus the server tick. 0 or 1 means prediction caught up and a correction becomes visible.");
	WriteLine(File, "Snapshot gap is the tick distance of the last two snapshots. 1 or 2 is normal. Above 2 means snapshots were missed.");
	WriteLine(File, "Tick jump is how many server ticks passed between two frames.");
	WriteLine(File, "Prediction error is the distance between the rendered tee and the server position. 32 units is one tile. A stable value while moving is anti-ping lead. A sudden correction looks like a teleport.");
	WriteLine(File, "Position step is how far the rendered tee or spectated camera moved in one frame. A large step in a short frame is a visual teleport. The same step in a long frame is a hitch.");
	WriteLine(File, "Intra is snapshot interpolation. Far outside 0..1 means the client ran off the snapshots it has.");

	int PlayCount = 0;
	int PlayPingCount = 0;
	int PlayErrCount = 0;
	double PlayPingSum = 0.0;
	double PlayPredSum = 0.0;
	double PlayLeadSum = 0.0;
	double PlayErrSum = 0.0;
	int PlayPingMin = 0;
	int PlayPingMax = 0;
	int PlayPredMin = 0;
	int PlayPredMax = 0;
	int PlayLeadMin = 0;
	int PlayLeadMax = 0;
	int PlaySnapGap = 0;
	int PlaySnapMisses = 0;
	int PlayTickJump = 0;
	int PlayErrHits = 0;
	float PlayErrMax = -1.0f;
	float PlayErrJump = -1.0f;
	float PlayStepMax = -1.0f;
	float PlayIntraMin = 0.0f;
	float PlayIntraMax = 0.0f;
	int PlayTeeSamples = 0;
	for(int SampleIndex = 0; SampleIndex < Num; SampleIndex++)
	{
		const SProfilerHistory &Sample = s_aHistory[aIndex[SampleIndex]];
		if(Sample.m_NetSamples <= 0)
			continue;
		if(PlayCount == 0)
		{
			PlayPredMin = Sample.m_PredMsMin;
			PlayPredMax = Sample.m_PredMsMax;
			PlayLeadMin = Sample.m_LeadMin;
			PlayLeadMax = Sample.m_LeadMax;
			PlayIntraMin = Sample.m_IntraMin;
			PlayIntraMax = Sample.m_IntraMax;
		}
		else
		{
			PlayPredMin = std::min(PlayPredMin, Sample.m_PredMsMin);
			PlayPredMax = std::max(PlayPredMax, Sample.m_PredMsMax);
			PlayLeadMin = std::min(PlayLeadMin, Sample.m_LeadMin);
			PlayLeadMax = std::max(PlayLeadMax, Sample.m_LeadMax);
			PlayIntraMin = std::min(PlayIntraMin, Sample.m_IntraMin);
			PlayIntraMax = std::max(PlayIntraMax, Sample.m_IntraMax);
		}
		PlayCount++;
		PlayPredSum += Sample.m_PredMs;
		PlayLeadSum += Sample.m_Lead;
		PlaySnapGap = std::max(PlaySnapGap, Sample.m_SnapGapMax);
		PlaySnapMisses += Sample.m_SnapMisses;
		PlayTickJump = std::max(PlayTickJump, Sample.m_TickJumpMax);
		PlayErrHits += Sample.m_PredErrHits;
		PlayTeeSamples += Sample.m_TeeSamples;
		PlayStepMax = std::max(PlayStepMax, Sample.m_StepMax);
		if(Sample.m_Ping >= 0)
		{
			if(PlayPingCount == 0)
			{
				PlayPingMin = Sample.m_PingMin;
				PlayPingMax = Sample.m_PingMax;
			}
			else
			{
				PlayPingMin = std::min(PlayPingMin, Sample.m_PingMin);
				PlayPingMax = std::max(PlayPingMax, Sample.m_PingMax);
			}
			PlayPingCount++;
			PlayPingSum += Sample.m_Ping;
		}
		if(Sample.m_PredErr >= 0.0f)
		{
			PlayErrCount++;
			PlayErrSum += Sample.m_PredErr;
			PlayErrMax = std::max(PlayErrMax, Sample.m_PredErrMax);
			PlayErrJump = std::max(PlayErrJump, Sample.m_PredErrJump);
		}
	}

	if(PlayCount <= 0)
	{
		WriteLine(File, "No ingame samples.");
	}
	else
	{
		const double PlayInv = 1.0 / PlayCount;
		SProfilerHistory Overview;
		Overview.m_NetSamples = PlayCount;
		Overview.m_TeeSamples = PlayTeeSamples;
		Overview.m_FpsMin = FpsMin;
		Overview.m_FrameUs = static_cast<float>(FrameSum * Inv);
		if(PlayPingCount > 0)
		{
			Overview.m_Ping = round_to_int(static_cast<float>(PlayPingSum / PlayPingCount));
			Overview.m_PingMin = PlayPingMin;
			Overview.m_PingMax = PlayPingMax;
		}
		Overview.m_PredMs = round_to_int(static_cast<float>(PlayPredSum * PlayInv));
		Overview.m_PredMsMin = PlayPredMin;
		Overview.m_PredMsMax = PlayPredMax;
		Overview.m_Lead = round_to_int(static_cast<float>(PlayLeadSum * PlayInv));
		Overview.m_LeadMin = PlayLeadMin;
		Overview.m_LeadMax = PlayLeadMax;
		Overview.m_SnapGapMax = PlaySnapGap;
		Overview.m_SnapMisses = PlaySnapMisses;
		Overview.m_TickJumpMax = PlayTickJump;
		Overview.m_PredErrHits = PlayErrHits;
		Overview.m_PredErrJump = PlayErrJump;
		if(PlayErrCount > 0)
		{
			Overview.m_PredErr = static_cast<float>(PlayErrSum / PlayErrCount);
			Overview.m_PredErrMax = PlayErrMax;
		}
		Overview.m_StepMax = PlayStepMax;
		Overview.m_IntraMin = PlayIntraMin;
		Overview.m_IntraMax = PlayIntraMax;
		WritePlaySample(File, Overview);
	}

	WriteLine(File, "");
	WriteLine(File, "Average components");
	const int SummaryRows = std::min(NumSums, 30);
	for(int Sum = 0; Sum < SummaryRows; Sum++)
	{
		const double Total = (aSums[Sum].m_UpdateUs + aSums[Sum].m_RenderUs) * Inv;
		if(Total < 1.0 && Sum >= 8)
			break;
		str_format(aLine, sizeof(aLine), "  %s  total %.1f us  render %.1f  update %.1f",
			aSums[Sum].m_pName,
			Total,
			aSums[Sum].m_RenderUs * Inv,
			aSums[Sum].m_UpdateUs * Inv);
		WriteLine(File, aLine);
	}

	for(int SampleIndex = 0; SampleIndex < Num; SampleIndex++)
	{
		const SProfilerHistory &Sample = s_aHistory[aIndex[SampleIndex]];
		char aTime[32];
		str_timestamp_ex(Sample.m_Unix, aTime, sizeof(aTime), "%H:%M:%S");
		WriteLine(File, "");
		str_format(aLine, sizeof(aLine), "[%s] FPS %d (%d-%d)  Frame %.0f us", aTime, Sample.m_Fps, Sample.m_FpsMin, Sample.m_FpsMax, Sample.m_FrameUs);
		WriteLine(File, aLine);
		str_format(aLine, sizeof(aLine), "Update %.0f us  Render %.0f us  Swap %.0f us", Sample.m_UpdateUs, Sample.m_RenderUs, Sample.m_SwapUs);
		WriteLine(File, aLine);
		str_format(aLine, sizeof(aLine), "RAM %.1f MiB  Textures %.1f  Buffers %.1f  Streamed %.1f  Staging %.1f",
			Sample.m_RamMiB, Sample.m_TexMiB, Sample.m_BufMiB, Sample.m_StreamMiB, Sample.m_StagingMiB);
		WriteLine(File, aLine);
		WritePlaySample(File, Sample);
		for(int Row = 0; Row < Sample.m_NumRows; Row++)
		{
			const float TotalUs = Sample.m_aRows[Row].m_UpdateUs + Sample.m_aRows[Row].m_RenderUs;
			if(TotalUs < 1.0f && Row >= 8)
				break;
			const float Share = Sample.m_FrameUs > 0.0f ? TotalUs / Sample.m_FrameUs * 100.0f : 0.0f;
			str_format(aLine, sizeof(aLine), "  %s  total %.1f us  render %.1f  update %.1f  %.1f%%",
				Sample.m_aRows[Row].m_pName != nullptr ? Sample.m_aRows[Row].m_pName : "Unknown",
				TotalUs,
				Sample.m_aRows[Row].m_RenderUs,
				Sample.m_aRows[Row].m_UpdateUs,
				Share);
			WriteLine(File, aLine);
		}
	}

	io_close(File);
	str_format(aLine, sizeof(aLine), "%s: %s", Localize("Profiler dump saved"), aFilename);
	ShowNotice(aLine);
	return true;
}
