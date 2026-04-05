#include "smoother.h"

#include <base/math.h>
#include <base/time.h>

#include <algorithm>
#include <cmath>

namespace BestClientVisualizer
{

CVisualizerSmoother::CVisualizerSmoother()
{
	Configure(SVisualizerConfig());
}

void CVisualizerSmoother::Configure(const SVisualizerConfig &Config)
{
	m_Config = Config;
	const int BandCount = std::clamp(m_Config.m_BandCount, 1, MAX_VISUALIZER_BANDS);
	m_vFall.assign(BandCount, 0.0f);
	m_vMem.assign(BandCount, 0.0f);
	m_vPeak.assign(BandCount, 0.0f);
	m_vPrev.assign(BandCount, 0.0f);
	m_Framerate = 75.0f;
	m_Autosens = 1.0f;
	m_SensInit = true;
	m_LastTick = 0;
}

void CVisualizerSmoother::Process(const SVisualizerFrame &RawFrame, SVisualizerFrame &OutFrame)
{
	OutFrame = RawFrame;
	OutFrame.m_IsPassiveFallback = RawFrame.m_BackendStatus == EVisualizerBackendStatus::FALLBACK ||
		RawFrame.m_BackendStatus == EVisualizerBackendStatus::UNAVAILABLE;

	const int BandCount = minimum((int)m_vMem.size(), m_Config.m_BandCount);
	if(BandCount <= 0)
		return;

	const int64_t Now = time_get();
	if(m_LastTick == 0)
		m_LastTick = Now;
	const float DeltaSeconds = std::clamp((Now - m_LastTick) / (float)time_freq(), 1.0f / 240.0f, 0.2f);
	m_LastTick = Now;

	const float InstantFramerate = 1.0f / DeltaSeconds;
	m_Framerate -= m_Framerate / 64.0f;
	m_Framerate += InstantFramerate / 64.0f;

	const float FramerateMod = 66.0f / maximum(1.0f, m_Framerate);
	const float NoiseReduction = std::clamp(m_Config.m_NoiseReduction, 0.0f, 0.99f);
	const float GravityMod = powf(FramerateMod, 2.5f) * 2.0f / maximum(NoiseReduction, 0.12f);
	const float IntegralMod = powf(FramerateMod, 0.1f);
	const float AttackFollow = std::clamp((0.50f + 0.28f * (1.0f - NoiseReduction)) / maximum(0.85f, IntegralMod), 0.05f, 1.0f);
	const float ReleaseFollow = std::clamp((0.09f + 0.18f * (1.0f - NoiseReduction)) / maximum(0.85f, IntegralMod), 0.01f, 1.0f);

	bool Overshoot = false;
	for(int Band = 0; Band < BandCount; ++Band)
	{
		const float RawValue = RawFrame.m_aBands[Band] * m_Autosens * m_Config.m_Sensitivity;
		float Target = RawValue;
		if(RawValue < m_vPrev[Band] && NoiseReduction > 0.1f)
		{
			Target = m_vPeak[Band] * (1.0f - (m_vFall[Band] * m_vFall[Band] * GravityMod));
			Target = maximum(0.0f, Target);
			const float DecayStep = std::clamp(0.03f * maximum(0.5f, GravityMod), 0.01f, 0.35f);
			Target = minimum(Target, m_vMem[Band] * (1.0f - DecayStep));
			m_vFall[Band] += 0.028f;
		}
		else
		{
			m_vPeak[Band] = RawValue;
			m_vFall[Band] = 0.0f;
		}

		m_vPrev[Band] = RawValue;
		float Value = m_vMem[Band];
		const float Follow = Target > Value ? AttackFollow : ReleaseFollow;
		Value += (Target - Value) * Follow;
		m_vMem[Band] = maximum(0.0f, Value);
		if(Value > 1.0f)
		{
			Overshoot = true;
			Value = 1.0f;
		}
		OutFrame.m_aBands[Band] = std::clamp(Value, 0.0f, 1.0f);
	}

	const bool Silence = !RawFrame.m_HasRealtimeSignal;
	if(Overshoot)
	{
		m_Autosens *= (1.0f - (0.02f * FramerateMod));
		m_SensInit = false;
	}
	else if(!Silence)
	{
		m_Autosens *= (1.0f + (0.001f * FramerateMod));
		if(m_SensInit)
			m_Autosens *= (1.0f + (0.1f * FramerateMod));
	}
	m_Autosens = std::clamp(m_Autosens, 0.02f, 250.0f);

	if(Silence && RawFrame.m_BackendStatus == EVisualizerBackendStatus::LIVE)
	{
		OutFrame.m_Peak = 0.0f;
		OutFrame.m_Rms = 0.0f;
	}
}

} // namespace BestClientVisualizer
