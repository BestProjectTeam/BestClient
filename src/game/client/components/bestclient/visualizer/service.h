#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_VISUALIZER_SERVICE_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_VISUALIZER_SERVICE_H

#include "types.h"

#include <memory>

namespace BestClientVisualizer
{

class IVisualizerSource;

class CRealtimeMusicVisualizer
{
	std::unique_ptr<IVisualizerSource> m_pSource;
	SVisualizerConfig m_Config;
	bool m_ConfigInitialized = false;

	void RefreshConfig();

public:
	CRealtimeMusicVisualizer();
	~CRealtimeMusicVisualizer();

	void SetPlaybackHint(const SVisualizerPlaybackHint &Hint);
	bool PollFrame(SVisualizerFrame &OutFrame);
};

} // namespace BestClientVisualizer

#endif
