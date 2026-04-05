#include "service.h"

#include "source.h"

#include <base/math.h>

#include <engine/shared/config.h>

#include <algorithm>
#include <cmath>

namespace BestClientVisualizer
{

CRealtimeMusicVisualizer::CRealtimeMusicVisualizer()
{
#if defined(CONF_PLATFORM_LINUX) && defined(BC_MUSICPLAYER_HAS_PULSE) && BC_MUSICPLAYER_HAS_PULSE
	m_pSource = CreatePulseVisualizerSource();
#elif defined(CONF_FAMILY_WINDOWS) && defined(BC_MUSICPLAYER_HAS_WINRT) && BC_MUSICPLAYER_HAS_WINRT
	m_pSource = CreateWasapiVisualizerSource();
#else
	m_pSource = CreatePassiveVisualizerSource();
#endif
	RefreshConfig();
}

CRealtimeMusicVisualizer::~CRealtimeMusicVisualizer() = default;

void CRealtimeMusicVisualizer::RefreshConfig()
{
	SVisualizerConfig Config;
	Config.m_SampleRate = maximum(8000, m_ConfigInitialized ? m_Config.m_SampleRate : 48000);
	Config.m_BandCount = MAX_VISUALIZER_BANDS;
	Config.m_LowCutHz = 50;
	Config.m_HighCutHz = 10000;
	Config.m_BassSplitHz = 100;
	Config.m_NoiseReduction = std::clamp(g_Config.m_BcMusicPlayerVisualizerSmoothing / 100.0f, 0.0f, 0.99f);
	const float RawSensitivity = std::clamp(g_Config.m_BcMusicPlayerVisualizerSensitivity / 100.0f, 0.5f, 20.0f);
	Config.m_Sensitivity = powf(RawSensitivity, 1.35f);
	m_Config = Config;
	m_ConfigInitialized = true;
	if(m_pSource)
		m_pSource->SetConfig(m_Config);
}

void CRealtimeMusicVisualizer::SetPlaybackHint(const SVisualizerPlaybackHint &Hint)
{
	RefreshConfig();
	if(m_pSource)
		m_pSource->SetPlaybackHint(Hint);
}

bool CRealtimeMusicVisualizer::PollFrame(SVisualizerFrame &OutFrame)
{
	RefreshConfig();
	if(!m_pSource)
	{
		OutFrame = SVisualizerFrame();
		return false;
	}
	return m_pSource->PollFrame(OutFrame);
}

} // namespace BestClientVisualizer
