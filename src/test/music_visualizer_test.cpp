#include <gtest/gtest.h>

#include <game/client/components/bestclient/visualizer/analyzer.h>
#include <game/client/components/bestclient/visualizer/source_priority.h>
#include <game/client/components/bestclient/visualizer/smoother.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

using namespace BestClientVisualizer;

namespace
{
constexpr float TEST_PI = 3.14159265358979323846f;

SVisualizerFrame AnalyzeSine(float Frequency, int SampleRate = 48000)
{
	CVisualizerAnalyzer Analyzer;
	SVisualizerConfig Config;
	Config.m_SampleRate = SampleRate;
	Analyzer.Configure(Config);

	std::vector<float> vSamples(8192, 0.0f);
	for(size_t i = 0; i < vSamples.size(); ++i)
		vSamples[i] = 0.8f * sinf(2.0f * TEST_PI * Frequency * i / SampleRate);

	Analyzer.PushMonoSamples(vSamples.data(), (int)vSamples.size());
	SVisualizerFrame Frame;
	Analyzer.Analyze(Frame);
	return Frame;
}

int MaxBandIndex(const SVisualizerFrame &Frame)
{
	return (int)(std::max_element(Frame.m_aBands.begin(), Frame.m_aBands.end()) - Frame.m_aBands.begin());
}

} // namespace

TEST(MusicVisualizer, FrequencyBandsIncreaseWithFrequency)
{
	const SVisualizerFrame Hz60 = AnalyzeSine(60.0f);
	const SVisualizerFrame Hz250 = AnalyzeSine(250.0f);
	const SVisualizerFrame Hz1000 = AnalyzeSine(1000.0f);
	const SVisualizerFrame Hz6000 = AnalyzeSine(6000.0f);

	EXPECT_LT(MaxBandIndex(Hz60), MaxBandIndex(Hz250));
	EXPECT_LT(MaxBandIndex(Hz250), MaxBandIndex(Hz1000));
	EXPECT_LT(MaxBandIndex(Hz1000), MaxBandIndex(Hz6000));
}

TEST(MusicVisualizer, SilenceIsFiniteAndQuiet)
{
	CVisualizerAnalyzer Analyzer;
	Analyzer.Configure(SVisualizerConfig());

	std::vector<float> vSamples(8192, 0.0f);
	Analyzer.PushMonoSamples(vSamples.data(), (int)vSamples.size());

	SVisualizerFrame Frame;
	Analyzer.Analyze(Frame);

	EXPECT_FLOAT_EQ(Frame.m_Peak, 0.0f);
	EXPECT_FLOAT_EQ(Frame.m_Rms, 0.0f);
	for(float Band : Frame.m_aBands)
	{
		EXPECT_TRUE(std::isfinite(Band));
		EXPECT_LE(Band, 0.001f);
	}
}

TEST(MusicVisualizer, RenderBarsResampleMonotonic)
{
	SVisualizerFrame Frame;
	Frame.m_BackendStatus = EVisualizerBackendStatus::LIVE;
	for(int i = 0; i < MAX_VISUALIZER_BANDS; ++i)
		Frame.m_aBands[i] = i / (float)(MAX_VISUALIZER_BANDS - 1);

	std::array<float, 5> aBars{};
	BuildRenderBars(Frame, aBars.data(), (int)aBars.size());

	for(size_t i = 1; i < aBars.size(); ++i)
		EXPECT_LE(aBars[i - 1], aBars[i]);
}

TEST(MusicVisualizer, CompactBarsPreserveBassToTrebleZones)
{
	SVisualizerFrame BassFrame;
	BassFrame.m_BackendStatus = EVisualizerBackendStatus::LIVE;
	for(int i = 0; i < 4; ++i)
		BassFrame.m_aBands[i] = 1.0f;

	SVisualizerFrame TrebleFrame;
	TrebleFrame.m_BackendStatus = EVisualizerBackendStatus::LIVE;
	for(int i = MAX_VISUALIZER_BANDS - 6; i < MAX_VISUALIZER_BANDS; ++i)
		TrebleFrame.m_aBands[i] = 1.0f;

	std::array<float, 5> aBassBars{};
	std::array<float, 5> aTrebleBars{};
	BuildRenderBars(BassFrame, aBassBars.data(), (int)aBassBars.size());
	BuildRenderBars(TrebleFrame, aTrebleBars.data(), (int)aTrebleBars.size());

	EXPECT_GE(aBassBars[0], aBassBars[1]);
	EXPECT_GT(aBassBars[0], aBassBars[4]);
	EXPECT_GT(aTrebleBars[4], aTrebleBars[3]);
	EXPECT_GT(aTrebleBars[4], aTrebleBars[0]);
}

TEST(MusicVisualizer, CompactBarsKeepContrastForSeparatedBands)
{
	SVisualizerFrame Frame;
	Frame.m_BackendStatus = EVisualizerBackendStatus::LIVE;
	Frame.m_aBands[1] = 1.0f;
	Frame.m_aBands[9] = 0.8f;
	Frame.m_aBands[26] = 0.9f;

	std::array<float, 5> aBars{};
	BuildRenderBars(Frame, aBars.data(), (int)aBars.size());

	EXPECT_GT(aBars[0], 0.15f);
	EXPECT_GT(aBars[2], 0.12f);
	EXPECT_GT(aBars[4], 0.15f);
	EXPECT_GT(aBars[0], aBars[1]);
	EXPECT_GT(aBars[4], aBars[3]);
}

TEST(MusicVisualizer, CompactBarsProduceUsableLevelForModerateSignal)
{
	const SVisualizerFrame Frame = AnalyzeSine(220.0f);
	std::array<float, 5> aBars{};
	BuildRenderBars(Frame, aBars.data(), (int)aBars.size());

	const float MaxBar = *std::max_element(aBars.begin(), aBars.end());
	EXPECT_GT(MaxBar, 0.20f);
}

TEST(MusicVisualizer, CompactBarsKeepBassSensitivityCompetitive)
{
	const SVisualizerFrame BassFrame = AnalyzeSine(60.0f);
	const SVisualizerFrame TrebleFrame = AnalyzeSine(6000.0f);

	std::array<float, 5> aBassBars{};
	std::array<float, 5> aTrebleBars{};
	BuildRenderBars(BassFrame, aBassBars.data(), (int)aBassBars.size());
	BuildRenderBars(TrebleFrame, aTrebleBars.data(), (int)aTrebleBars.size());

	EXPECT_GT(aBassBars[0], 0.18f);
	EXPECT_GT(aTrebleBars[4], 0.18f);
	EXPECT_GE(aBassBars[0], aTrebleBars[4] * 0.82f);
}

TEST(MusicVisualizer, PlayerPriorityHelpersClassifySources)
{
	EXPECT_TRUE(LooksLikeDedicatedPlayer("org.mpris.MediaPlayer2.spotify"));
	EXPECT_TRUE(LooksLikeBrowserPlayer("org.mpris.MediaPlayer2.chromium.instance42"));
	EXPECT_TRUE(LooksLikeDiscordPlayer("org.mpris.MediaPlayer2.discord"));
	EXPECT_GT(PlayerSourcePriority("org.mpris.MediaPlayer2.spotify"), PlayerSourcePriority("org.mpris.MediaPlayer2.chromium"));
	EXPECT_GT(PlayerSourcePriority("org.mpris.MediaPlayer2.chromium"), PlayerSourcePriority("org.mpris.MediaPlayer2.foobar"));
	EXPECT_LT(PlayerSourcePriority("org.mpris.MediaPlayer2.discord"), 0);
}

TEST(MusicVisualizer, SmootherReleasesGradually)
{
	CVisualizerSmoother Smoother;
	Smoother.Configure(SVisualizerConfig());

	SVisualizerFrame LoudFrame;
	LoudFrame.m_BackendStatus = EVisualizerBackendStatus::LIVE;
	LoudFrame.m_HasRealtimeSignal = true;
	LoudFrame.m_Peak = 0.8f;
	LoudFrame.m_Rms = 0.6f;
	LoudFrame.m_aBands.fill(0.9f);

	SVisualizerFrame SilentFrame;
	SilentFrame.m_BackendStatus = EVisualizerBackendStatus::LIVE;
	SilentFrame.m_HasRealtimeSignal = false;
	SilentFrame.m_aBands.fill(0.0f);

	SVisualizerFrame Attack;
	SVisualizerFrame Release1;
	SVisualizerFrame Release2;
	Smoother.Process(LoudFrame, Attack);
	Smoother.Process(SilentFrame, Release1);
	Smoother.Process(SilentFrame, Release2);

	EXPECT_GT(Attack.m_aBands[0], Release1.m_aBands[0]);
	EXPECT_GT(Release1.m_aBands[0], Release2.m_aBands[0]);
	EXPECT_GE(Release1.m_aBands[0], 0.0f);
	EXPECT_GE(Release2.m_aBands[0], 0.0f);
}
