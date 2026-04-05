#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_VISUALIZER_SMOOTHER_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_VISUALIZER_SMOOTHER_H

#include "types.h"

#include <cstdint>
#include <vector>

namespace BestClientVisualizer
{

class CVisualizerSmoother
{
	SVisualizerConfig m_Config;
	std::vector<float> m_vFall;
	std::vector<float> m_vMem;
	std::vector<float> m_vPeak;
	std::vector<float> m_vPrev;
	float m_Framerate = 75.0f;
	float m_Autosens = 1.0f;
	bool m_SensInit = true;
	int64_t m_LastTick = 0;

public:
	CVisualizerSmoother();

	void Configure(const SVisualizerConfig &Config);
	void Process(const SVisualizerFrame &RawFrame, SVisualizerFrame &OutFrame);
};

} // namespace BestClientVisualizer

#endif
