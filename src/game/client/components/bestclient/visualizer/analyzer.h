#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_VISUALIZER_ANALYZER_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_VISUALIZER_ANALYZER_H

#include "types.h"

#include <complex>
#include <vector>

namespace BestClientVisualizer
{

class CVisualizerAnalyzer
{
	SVisualizerConfig m_Config;
	int m_MainFftSize = 0;
	int m_BassFftSize = 0;
	int m_BassBandCount = 0;
	int m_RingWritePos = 0;
	int m_RingCount = 0;
	std::vector<float> m_vRingBuffer;
	std::vector<float> m_vMainWindow;
	std::vector<float> m_vBassWindow;
	std::vector<float> m_vMainSamples;
	std::vector<float> m_vBassSamples;
	std::vector<int> m_vMainLowerCutOff;
	std::vector<int> m_vMainUpperCutOff;
	std::vector<int> m_vBassLowerCutOff;
	std::vector<int> m_vBassUpperCutOff;
	std::vector<float> m_vEq;
	std::vector<float> m_vCutOffFrequency;
	std::vector<std::complex<float>> m_vMainBuffer;
	std::vector<std::complex<float>> m_vBassBuffer;
	std::vector<std::complex<float>> m_vMainTwiddles;
	std::vector<std::complex<float>> m_vBassTwiddles;
	std::vector<int> m_vMainBitReverse;
	std::vector<int> m_vBassBitReverse;

	void RebuildPlan();
	void BuildWindows();
	void BuildTwiddles(int FftSize, std::vector<std::complex<float>> &vTwiddles, std::vector<int> &vBitReverse);
	void ComputeBandDistribution();
	void CopyLatestSamples(float *pDst, int Count) const;
	void RunFft(std::vector<std::complex<float>> &vBuffer, const std::vector<std::complex<float>> &vTwiddles, const std::vector<int> &vBitReverse) const;
	static int ResolveMainFftSize(int SampleRate);

public:
	CVisualizerAnalyzer();

	void Configure(const SVisualizerConfig &Config);
	void PushMonoSamples(const float *pSamples, int NumSamples);
	void Analyze(SVisualizerFrame &OutFrame);
};

void BuildRenderBars(const SVisualizerFrame &Frame, float *pOutBars, int RequestedBarCount);

} // namespace BestClientVisualizer

#endif
