/* Copyright © 2026 BestProject Team */
#include "automargin.h"

#include <engine/client.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace
{
constexpr int BASE_MARGIN = 90;
constexpr int MAX_MARGIN = 3000;
constexpr float CHECK_INTERVAL_SECONDS = 0.5f;
constexpr float PING_SMOOTH_FACTOR_RISE = 0.45f;
constexpr float PING_SMOOTH_FACTOR_FALL = 0.20f;
constexpr float JITTER_SMOOTH_FACTOR_RISE = 0.35f;
constexpr float JITTER_SMOOTH_FACTOR_FALL = 0.15f;
constexpr int HIGH_PING_ENTER_MS = 50;
constexpr int HIGH_PING_EXIT_MS = 42;
constexpr float JITTER_ENTER_MS = 4.0f;
constexpr float JITTER_EXIT_MS = 2.0f;
constexpr int MARGIN_DEADZONE = 10;
constexpr int MARGIN_STEP_DOWN = 50;

float MarginFromPing(float Ping)
{
	// Returns margin in 0.1 ms units
	return std::max((float)BASE_MARGIN, Ping * 2.5f);
}
}

void CBcAutoMargin::ResetState()
{
	m_CheckTimer = 0.0f;
	m_LatestPing = -1.0f;
	m_IntervalPingSum = 0.0f;
	m_IntervalPingSamples = 0;
	m_IntervalMinPing = -1.0f;
	m_IntervalPeakPing = -1.0f;
	m_SmoothedPing = -1.0f;
	m_SmoothedJitter = 0.0f;
	m_HighPing = false;
}

void CBcAutoMargin::OnReset()
{
	ResetState();
}

void CBcAutoMargin::OnStateChange(int NewState, int OldState)
{
	if(NewState != OldState)
		ResetState();
}

void CBcAutoMargin::OnNewSnapshot()
{
	if(Client()->State() != IClient::STATE_ONLINE || !GameClient()->m_Snap.m_pLocalInfo)
	{
		ResetState();
		return;
	}

	const float CurrentPing = (float)std::max(0, GameClient()->m_Snap.m_pLocalInfo->m_Latency);
	m_LatestPing = CurrentPing;
	m_IntervalPingSum += CurrentPing;
	++m_IntervalPingSamples;
	if(m_IntervalMinPing < 0.0f || CurrentPing < m_IntervalMinPing)
		m_IntervalMinPing = CurrentPing;
	if(m_IntervalPeakPing < 0.0f || CurrentPing > m_IntervalPeakPing)
		m_IntervalPeakPing = CurrentPing;
}

void CBcAutoMargin::OnUpdate()
{
	if(!g_Config.m_BcAutoMargin || Client()->State() != IClient::STATE_ONLINE || !GameClient()->m_Snap.m_pLocalInfo)
	{
		ResetState();
		return;
	}

	m_CheckTimer -= Client()->RenderFrameTime();
	if(m_CheckTimer > 0.0f)
		return;
	m_CheckTimer += CHECK_INTERVAL_SECONDS;
	if(m_CheckTimer < 0.0f)
		m_CheckTimer = 0.0f;

	const float CurrentPing = m_LatestPing >= 0.0f ? m_LatestPing : (float)std::max(0, GameClient()->m_Snap.m_pLocalInfo->m_Latency);
	const float IntervalAveragePing = m_IntervalPingSamples > 0 ? m_IntervalPingSum / (float)m_IntervalPingSamples : CurrentPing;
	const float IntervalMinPing = m_IntervalMinPing >= 0.0f ? m_IntervalMinPing : CurrentPing;
	const float SamplePing = m_IntervalPeakPing >= 0.0f ? m_IntervalPeakPing : CurrentPing;
	m_IntervalPingSum = 0.0f;
	m_IntervalPingSamples = 0;
	m_IntervalMinPing = -1.0f;
	m_IntervalPeakPing = -1.0f;
	const float IntervalJitter = std::max(0.0f, SamplePing - IntervalMinPing);

	if(m_SmoothedPing < 0.0f)
		m_SmoothedPing = IntervalAveragePing;
	else
	{
		const float SmoothFactor = IntervalAveragePing > m_SmoothedPing ? PING_SMOOTH_FACTOR_RISE : PING_SMOOTH_FACTOR_FALL;
		m_SmoothedPing += (IntervalAveragePing - m_SmoothedPing) * SmoothFactor;
	}

	const float JitterSmoothFactor = IntervalJitter > m_SmoothedJitter ? JITTER_SMOOTH_FACTOR_RISE : JITTER_SMOOTH_FACTOR_FALL;
	m_SmoothedJitter += (IntervalJitter - m_SmoothedJitter) * JitterSmoothFactor;

	const float EffectivePing = std::max(std::max(m_SmoothedPing, CurrentPing), IntervalAveragePing + m_SmoothedJitter * 0.25f);
	const bool WantsDynamicMargin = EffectivePing >= (float)HIGH_PING_ENTER_MS || SamplePing >= (float)HIGH_PING_ENTER_MS || m_SmoothedJitter >= JITTER_ENTER_MS;

	if(m_HighPing)
	{
		if(EffectivePing < (float)HIGH_PING_EXIT_MS && m_SmoothedJitter <= JITTER_EXIT_MS)
			m_HighPing = false;
	}
	else if(WantsDynamicMargin)
	{
		m_HighPing = true;
	}

	int TargetMargin = BASE_MARGIN;
	if(m_HighPing)
	{
		const float StableMargin = MarginFromPing(EffectivePing);
		const float JitterMargin = std::max(m_SmoothedJitter * 9.0f, IntervalJitter * 3.5f);
		TargetMargin = std::clamp((int)std::round(StableMargin + JitterMargin), BASE_MARGIN, MAX_MARGIN);
	}

	int CurrentMargin = std::clamp(g_Config.m_ClPredictionMargin, BASE_MARGIN, MAX_MARGIN);
	const int MarginDelta = TargetMargin - CurrentMargin;
	if(std::abs(MarginDelta) <= MARGIN_DEADZONE)
		return;

	if(MarginDelta > 0)
		CurrentMargin = TargetMargin;
	else
		CurrentMargin -= std::min(-MarginDelta, MARGIN_STEP_DOWN);

	CurrentMargin = std::clamp(CurrentMargin, BASE_MARGIN, MAX_MARGIN);
	if(g_Config.m_ClPredictionMargin != CurrentMargin)
		g_Config.m_ClPredictionMargin = CurrentMargin;
}
