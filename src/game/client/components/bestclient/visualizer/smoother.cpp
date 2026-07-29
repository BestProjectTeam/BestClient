/* Copyright © 2026 BestProject Team */
#include "smoother.h"

#include <base/math.h>

#include <algorithm>

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
	// Analyzer already applies attack/release + peak normalization.
	// Keep this as a pass-through so we don't double-smooth.
	OutFrame = RawFrame;
	OutFrame.m_IsPassiveFallback = RawFrame.m_BackendStatus == EVisualizerBackendStatus::FALLBACK ||
		RawFrame.m_BackendStatus == EVisualizerBackendStatus::UNAVAILABLE;

	const int BandCount = minimum((int)m_vMem.size(), m_Config.m_BandCount);
	for(int Band = 0; Band < BandCount; ++Band)
		OutFrame.m_aBands[Band] = std::clamp(RawFrame.m_aBands[Band], 0.0f, 1.0f);

	if(!RawFrame.m_HasRealtimeSignal && RawFrame.m_BackendStatus == EVisualizerBackendStatus::LIVE)
	{
		OutFrame.m_Peak = 0.0f;
		OutFrame.m_Rms = 0.0f;
	}
}

} // namespace BestClientVisualizer
