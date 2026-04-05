#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_VISUALIZER_TYPES_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_VISUALIZER_TYPES_H

#include <array>
#include <string>

namespace BestClientVisualizer
{
static constexpr int MAX_VISUALIZER_BANDS = 32;

enum class EVisualizerChannelMode
{
	MONO,
};

enum class EVisualizerBackendStatus
{
	UNAVAILABLE,
	CONNECTING,
	SILENT,
	LIVE,
	FALLBACK,
};

struct SVisualizerPlaybackHint
{
	std::string m_ServiceId;
	std::string m_Title;
	std::string m_Artist;
	bool m_Playing = false;
};

struct SVisualizerConfig
{
	int m_SampleRate = 48000;
	EVisualizerChannelMode m_ChannelMode = EVisualizerChannelMode::MONO;
	int m_BandCount = MAX_VISUALIZER_BANDS;
	int m_LowCutHz = 50;
	int m_HighCutHz = 10000;
	int m_BassSplitHz = 100;
	float m_NoiseReduction = 0.77f;
	float m_Sensitivity = 1.0f;
};

struct SVisualizerFrame
{
	bool m_HasRealtimeSignal = false;
	bool m_IsPassiveFallback = true;
	EVisualizerBackendStatus m_BackendStatus = EVisualizerBackendStatus::FALLBACK;
	float m_Peak = 0.0f;
	float m_Rms = 0.0f;
	int m_SampleRate = 0;
	std::array<float, MAX_VISUALIZER_BANDS> m_aBands{};
};

const char *VisualizerBackendStatusName(EVisualizerBackendStatus Status);

} // namespace BestClientVisualizer

#endif
