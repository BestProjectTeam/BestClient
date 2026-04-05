#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_VISUALIZER_SOURCE_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_VISUALIZER_SOURCE_H

#include "types.h"

#include <memory>

namespace BestClientVisualizer
{

class IVisualizerSource
{
public:
	virtual ~IVisualizerSource() = default;

	virtual void SetPlaybackHint(const SVisualizerPlaybackHint &Hint) = 0;
	virtual void SetConfig(const SVisualizerConfig &Config) = 0;
	virtual bool PollFrame(SVisualizerFrame &OutFrame) = 0;
};

std::unique_ptr<IVisualizerSource> CreatePulseVisualizerSource();
std::unique_ptr<IVisualizerSource> CreateWasapiVisualizerSource();
std::unique_ptr<IVisualizerSource> CreatePassiveVisualizerSource();

} // namespace BestClientVisualizer

#endif
