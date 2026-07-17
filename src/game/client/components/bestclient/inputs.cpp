/* Copyright © 2026 BestProject Team */
#include "inputs.h"

#include <base/math.h>
#include <engine/shared/config.h>

#include <algorithm>
#include <cmath>

namespace
{
	// Fast: legacy ms-based prediction (20ms per tick).
	float FastModeOffsetTicks()
	{
		if(g_Config.m_BcInputs != BC_INPUTS_FAST || g_Config.m_TcFastInputAmount <= 0)
			return 0.0f;
		return g_Config.m_TcFastInputAmount / 20.0f;
	}

	// Best: tick-based prediction (0.01 ticks) with smoothing and latency compensation.
	float BestModeOffsetTicks()
	{
		if(g_Config.m_BcInputs != BC_INPUTS_BEST || g_Config.m_BcBestInputAmount <= 0)
			return 0.0f;

		float Offset = g_Config.m_BcBestInputAmount / 100.0f;

		if(g_Config.m_BcBestInputSmoothing > 0)
			Offset *= 1.0f - (g_Config.m_BcBestInputSmoothing / 200.0f);

		if(g_Config.m_BcBestInputLatencyComp > 0)
			Offset *= 1.0f + (g_Config.m_BcBestInputLatencyComp / 100.0f);

		return Offset;
	}

	// Saiko: tick-based prediction (0.01 ticks).
	float SaikoModeOffsetTicks()
	{
		if(g_Config.m_BcInputs != BC_INPUTS_SAIKO || g_Config.m_BcSaikoInputAmount <= 0)
			return 0.0f;
		return g_Config.m_BcSaikoInputAmount / 100.0f;
	}

	// Delta: tick-based prediction (0.01 ticks).
	float DeltaModeOffsetTicks()
	{
		if(g_Config.m_BcInputs != BC_INPUTS_DELTA || g_Config.m_BcDeltaInputAmount <= 0)
			return 0.0f;
		return g_Config.m_BcDeltaInputAmount / 100.0f;
	}

	// F: original fclient fast-input prediction (0.001 ticks), see CGameClient::GetFastInputPos for the velocity-extrapolation logic.
	float FModeOffsetTicks()
	{
		if(g_Config.m_BcInputs != BC_INPUTS_F || g_Config.m_BcFInputAmount <= 0)
			return 0.0f;
		return g_Config.m_BcFInputAmount / 1000.0f;
	}
} // namespace

float BcInputs::EffectiveOffsetTicks()
{
	switch(g_Config.m_BcInputs)
	{
	case BC_INPUTS_FAST: return FastModeOffsetTicks();
	case BC_INPUTS_BEST: return BestModeOffsetTicks();
	case BC_INPUTS_SAIKO: return SaikoModeOffsetTicks();
	case BC_INPUTS_DELTA: return DeltaModeOffsetTicks();
	case BC_INPUTS_F: return FModeOffsetTicks();
	default: return 0.0f;
	}
}

int BcInputs::PredictionTicks(float OffsetTicks)
{
	if(OffsetTicks <= 0.0f)
		return 0;
	if(g_Config.m_BcInputs == BC_INPUTS_SAIKO)
		return (int)std::ceil(OffsetTicks + 1.0f);
	return (int)std::ceil(OffsetTicks);
}

int BcInputs::PredictionTicksOthers(float OffsetTicks)
{
	if(OffsetTicks <= 0.0f)
		return 0;
	if(g_Config.m_BcInputs == BC_INPUTS_SAIKO)
		return (int)std::ceil(OffsetTicks);
	return PredictionTicks(OffsetTicks);
}

void BcInputs::ApplyOffset(float OffsetTicks, int &Tick, float &Intra)
{
	if(OffsetTicks <= 0.0f)
		return;

	const int WholeTicks = (int)OffsetTicks;
	const float OffsetIntra = OffsetTicks - WholeTicks;

	const float CombinedIntra = Intra + OffsetIntra;
	const int CarryOverTicks = (int)CombinedIntra;

	Tick += WholeTicks + CarryOverTicks;
	Intra = CombinedIntra - CarryOverTicks;
}

float BcInputs::BestInterpolationAmount(float Fraction, float DeltaLength, bool Enable)
{
	if(!Enable)
		return Fraction;

	const float T = std::clamp(Fraction, 0.0f, 1.0f);
	const float T2 = T * T;
	const float CubicT = 3.0f * T2 - 2.0f * T2 * T;
	switch(std::clamp(g_Config.m_BcBestInputInterpolation, 1, 3))
	{
	case 2:
		return CubicT;
	case 3:
		return mix(T, CubicT, std::clamp(DeltaLength / 1000.0f, 0.0f, 1.0f));
	default:
		return T;
	}
}

vec2 BcInputs::BestInterpolate(vec2 PrevPos, vec2 CurPos, float Fraction, bool Enable)
{
	return mix(PrevPos, CurPos, BestInterpolationAmount(Fraction, length(CurPos - PrevPos), Enable));
}

float BcInputs::BestInterpolate(float PrevPos, float CurPos, float Fraction, bool Enable)
{
	return mix(PrevPos, CurPos, BestInterpolationAmount(Fraction, absolute(CurPos - PrevPos), Enable));
}

bool BcInputs::FastOthers()
{
	return g_Config.m_BcInputs == BC_INPUTS_FAST && g_Config.m_TcFastInputOthers != 0;
}

bool BcInputs::BestOthers()
{
	return g_Config.m_BcInputs == BC_INPUTS_BEST && g_Config.m_BcBestInputOthers != 0;
}

bool BcInputs::SaikoOthers()
{
	return g_Config.m_BcInputs == BC_INPUTS_SAIKO && g_Config.m_BcSaikoInputOthers != 0;
}

bool BcInputs::DeltaOthers()
{
	return g_Config.m_BcInputs == BC_INPUTS_DELTA && g_Config.m_BcDeltaInputOthers != 0;
}

bool BcInputs::FOthers()
{
	return g_Config.m_BcInputs == BC_INPUTS_F && g_Config.m_BcFInputOthers != 0;
}

bool BcInputs::AnyOthers()
{
	return FastOthers() || BestOthers() || SaikoOthers() || DeltaOthers() || FOthers();
}

bool BcInputs::ImmediateOthers()
{
	return BestOthers() || SaikoOthers() || DeltaOthers();
}
