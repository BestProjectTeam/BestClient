#include "analyzer.h"

#include "source_priority.h"

#include <base/math.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace BestClientVisualizer
{

namespace
{
constexpr float PI = 3.14159265358979323846f;

struct SCompactBarRange
{
	int m_Start;
	int m_End;
	float m_Emphasis;
};

static constexpr SCompactBarRange gs_aCompactBarRanges[5] = {
	{0, 4, 1.28f},
	{4, 9, 1.16f},
	{9, 15, 1.03f},
	{15, 23, 0.96f},
	{23, MAX_VISUALIZER_BANDS, 0.90f},
};
} // namespace

const char *VisualizerBackendStatusName(EVisualizerBackendStatus Status)
{
	switch(Status)
	{
	case EVisualizerBackendStatus::UNAVAILABLE: return "unavailable";
	case EVisualizerBackendStatus::CONNECTING: return "connecting";
	case EVisualizerBackendStatus::SILENT: return "silent";
	case EVisualizerBackendStatus::LIVE: return "live";
	case EVisualizerBackendStatus::FALLBACK: return "fallback";
	}
	return "unknown";
}

CVisualizerAnalyzer::CVisualizerAnalyzer()
{
	Configure(SVisualizerConfig());
}

int CVisualizerAnalyzer::ResolveMainFftSize(int SampleRate)
{
	int MainFftSize = 1024;
	if(SampleRate > 75000 && SampleRate <= 150000)
		MainFftSize *= 2;
	else if(SampleRate > 150000 && SampleRate <= 300000)
		MainFftSize *= 4;
	else if(SampleRate > 300000)
		MainFftSize *= 8;
	return MainFftSize;
}

void CVisualizerAnalyzer::Configure(const SVisualizerConfig &Config)
{
	SVisualizerConfig Sanitized = Config;
	Sanitized.m_SampleRate = maximum(8000, Sanitized.m_SampleRate);
	Sanitized.m_BandCount = std::clamp(Sanitized.m_BandCount, 1, MAX_VISUALIZER_BANDS);
	Sanitized.m_LowCutHz = maximum(20, Sanitized.m_LowCutHz);
	Sanitized.m_HighCutHz = maximum(Sanitized.m_LowCutHz + 100, Sanitized.m_HighCutHz);
	Sanitized.m_BassSplitHz = std::clamp(Sanitized.m_BassSplitHz, Sanitized.m_LowCutHz, Sanitized.m_HighCutHz);
	Sanitized.m_NoiseReduction = std::clamp(Sanitized.m_NoiseReduction, 0.0f, 0.99f);
	Sanitized.m_Sensitivity = maximum(0.05f, Sanitized.m_Sensitivity);
	m_Config = Sanitized;
	RebuildPlan();
}

void CVisualizerAnalyzer::RebuildPlan()
{
	m_MainFftSize = ResolveMainFftSize(m_Config.m_SampleRate);
	m_BassFftSize = m_MainFftSize * 2;
	m_RingWritePos = 0;
	m_RingCount = 0;
	m_vRingBuffer.assign(m_BassFftSize, 0.0f);
	m_vMainWindow.assign(m_MainFftSize, 0.0f);
	m_vBassWindow.assign(m_BassFftSize, 0.0f);
	m_vMainSamples.assign(m_MainFftSize, 0.0f);
	m_vBassSamples.assign(m_BassFftSize, 0.0f);
	m_vMainBuffer.assign(m_MainFftSize, std::complex<float>(0.0f, 0.0f));
	m_vBassBuffer.assign(m_BassFftSize, std::complex<float>(0.0f, 0.0f));
	m_vMainLowerCutOff.assign(m_Config.m_BandCount, 0);
	m_vMainUpperCutOff.assign(m_Config.m_BandCount, 0);
	m_vBassLowerCutOff.assign(m_Config.m_BandCount, 0);
	m_vBassUpperCutOff.assign(m_Config.m_BandCount, 0);
	m_vEq.assign(m_Config.m_BandCount, 0.0f);
	m_vCutOffFrequency.assign(m_Config.m_BandCount + 1, 0.0f);
	BuildWindows();
	BuildTwiddles(m_MainFftSize, m_vMainTwiddles, m_vMainBitReverse);
	BuildTwiddles(m_BassFftSize, m_vBassTwiddles, m_vBassBitReverse);
	ComputeBandDistribution();
}

void CVisualizerAnalyzer::BuildWindows()
{
	for(int i = 0; i < m_MainFftSize; ++i)
		m_vMainWindow[i] = 0.5f * (1.0f - cosf(2.0f * PI * i / maximum(1, m_MainFftSize - 1)));
	for(int i = 0; i < m_BassFftSize; ++i)
		m_vBassWindow[i] = 0.5f * (1.0f - cosf(2.0f * PI * i / maximum(1, m_BassFftSize - 1)));
}

void CVisualizerAnalyzer::BuildTwiddles(int FftSize, std::vector<std::complex<float>> &vTwiddles, std::vector<int> &vBitReverse)
{
	vTwiddles.assign(FftSize / 2, std::complex<float>(0.0f, 0.0f));
	for(int i = 0; i < FftSize / 2; ++i)
	{
		const float Angle = -2.0f * PI * i / FftSize;
		vTwiddles[i] = std::complex<float>(cosf(Angle), sinf(Angle));
	}

	int Bits = 0;
	while((1 << Bits) < FftSize)
		++Bits;
	vBitReverse.assign(FftSize, 0);
	for(int i = 0; i < FftSize; ++i)
	{
		int Reversed = 0;
		for(int Bit = 0; Bit < Bits; ++Bit)
			Reversed = (Reversed << 1) | ((i >> Bit) & 1);
		vBitReverse[i] = Reversed;
	}
}

void CVisualizerAnalyzer::ComputeBandDistribution()
{
	const int MainNyquist = m_MainFftSize / 2;
	const int BassNyquist = m_BassFftSize / 2;
	const float LowerCutOff = (float)m_Config.m_LowCutHz;
	const float UpperCutOff = (float)minimum(m_Config.m_HighCutHz, m_Config.m_SampleRate / 2 - 1);
	const float BassSplit = (float)m_Config.m_BassSplitHz;
	const float FrequencyConstant = log10f(LowerCutOff / UpperCutOff) /
		(1.0f / ((float)m_Config.m_BandCount + 1.0f) - 1.0f);

	std::vector<float> vRelativeCutOff(m_Config.m_BandCount + 1, 0.0f);
	const float MinBandwidthHz = m_Config.m_SampleRate / (float)m_BassFftSize;
	m_BassBandCount = 0;
	bool FirstBand = true;

	for(int Band = 0; Band <= m_Config.m_BandCount; ++Band)
	{
		float Distribution = -FrequencyConstant;
		Distribution += ((float)Band + 1.0f) / ((float)m_Config.m_BandCount + 1.0f) * FrequencyConstant;
		m_vCutOffFrequency[Band] = UpperCutOff * powf(10.0f, Distribution);
		if(Band > 0 && m_vCutOffFrequency[Band - 1] >= m_vCutOffFrequency[Band])
			m_vCutOffFrequency[Band] = m_vCutOffFrequency[Band - 1] + MinBandwidthHz;

		vRelativeCutOff[Band] = m_vCutOffFrequency[Band] / (m_Config.m_SampleRate / 2.0f);
		if(m_vCutOffFrequency[Band] < BassSplit)
		{
			if(Band < m_Config.m_BandCount)
				m_vBassLowerCutOff[Band] = std::clamp((int)(vRelativeCutOff[Band] * BassNyquist), 0, BassNyquist);
			m_BassBandCount++;
			if(m_BassBandCount > 1)
				FirstBand = false;
		}
		else
		{
			if(Band < m_Config.m_BandCount)
				m_vMainLowerCutOff[Band] = std::clamp((int)ceilf(vRelativeCutOff[Band] * MainNyquist), 0, MainNyquist);
			if(Band == m_BassBandCount)
				FirstBand = true;
			else
				FirstBand = false;
		}

		if(Band > 0)
		{
			const int PrevBand = Band - 1;
			if(PrevBand < m_BassBandCount)
			{
				if(Band < m_BassBandCount)
					m_vBassUpperCutOff[PrevBand] = m_vBassLowerCutOff[Band] - 1;
				else
					m_vBassUpperCutOff[PrevBand] = std::clamp((int)(vRelativeCutOff[Band] * BassNyquist) - 1, m_vBassLowerCutOff[PrevBand], BassNyquist);

				if(!FirstBand && Band < m_BassBandCount && m_vBassLowerCutOff[Band] <= m_vBassLowerCutOff[PrevBand])
					m_vBassLowerCutOff[Band] = minimum(m_vBassLowerCutOff[PrevBand] + 1, BassNyquist);
				if(m_vBassUpperCutOff[PrevBand] < m_vBassLowerCutOff[PrevBand])
					m_vBassUpperCutOff[PrevBand] = minimum(m_vBassLowerCutOff[PrevBand] + 1, BassNyquist);
			}
			else if(PrevBand < m_Config.m_BandCount)
			{
				if(Band < m_Config.m_BandCount)
					m_vMainUpperCutOff[PrevBand] = minimum(m_vMainLowerCutOff[Band] - 1, MainNyquist);
				else
					m_vMainUpperCutOff[PrevBand] = MainNyquist;
				if(!FirstBand && Band < m_Config.m_BandCount && m_vMainLowerCutOff[Band] <= m_vMainLowerCutOff[PrevBand])
					m_vMainLowerCutOff[Band] = minimum(m_vMainLowerCutOff[PrevBand] + 1, MainNyquist);
				if(m_vMainUpperCutOff[PrevBand] < m_vMainLowerCutOff[PrevBand])
					m_vMainUpperCutOff[PrevBand] = minimum(m_vMainLowerCutOff[PrevBand] + 1, MainNyquist);
			}
		}

		const float Relative = Band < m_BassBandCount ?
			(float)m_vBassLowerCutOff[minimum(Band, m_Config.m_BandCount - 1)] / maximum(1.0f, (float)BassNyquist) :
			(float)m_vMainLowerCutOff[minimum(Band, m_Config.m_BandCount - 1)] / maximum(1.0f, (float)MainNyquist);
		m_vCutOffFrequency[Band] = Relative * (m_Config.m_SampleRate / 2.0f);
	}

	for(int Band = 0; Band < m_Config.m_BandCount; ++Band)
	{
		const bool IsBass = Band < m_BassBandCount;
		const int Lower = IsBass ? m_vBassLowerCutOff[Band] : m_vMainLowerCutOff[Band];
		const int Upper = IsBass ? m_vBassUpperCutOff[Band] : m_vMainUpperCutOff[Band];
		const float Width = (float)maximum(1, Upper - Lower + 1);
		const float CenterFrequency = maximum(40.0f, (m_vCutOffFrequency[Band] + m_vCutOffFrequency[Band + 1]) * 0.5f);
		const float FreqBoost = powf(CenterFrequency, 0.76f);
		const float WidthNorm = powf(Width, 0.78f);
		const float SizeNorm = powf(maximum(1.0f, log2f((float)(IsBass ? m_BassFftSize : m_MainFftSize))), 0.88f);
		const float RelativeFrequency = std::clamp(CenterFrequency / maximum(1.0f, UpperCutOff), 0.0f, 1.0f);
		const float HighTilt = 1.02f + 0.28f * powf(RelativeFrequency, 0.62f);
		const float LowLift = 1.0f + 0.38f * powf(1.0f - RelativeFrequency, 1.18f);
		const float BassWindowLift = IsBass ? 1.12f : 1.0f;
		m_vEq[Band] = HighTilt * LowLift * BassWindowLift * FreqBoost / maximum(1.0f, WidthNorm * SizeNorm);
	}
}

void CVisualizerAnalyzer::PushMonoSamples(const float *pSamples, int NumSamples)
{
	if(pSamples == nullptr || NumSamples <= 0 || m_vRingBuffer.empty())
		return;

	for(int i = 0; i < NumSamples; ++i)
	{
		m_vRingBuffer[m_RingWritePos] = std::clamp(pSamples[i], -1.0f, 1.0f);
		m_RingWritePos = (m_RingWritePos + 1) % m_BassFftSize;
		m_RingCount = minimum(m_RingCount + 1, m_BassFftSize);
	}
}

void CVisualizerAnalyzer::CopyLatestSamples(float *pDst, int Count) const
{
	if(pDst == nullptr || Count <= 0)
		return;

	const int Available = minimum(Count, m_RingCount);
	const int Missing = Count - Available;
	if(Missing > 0)
		std::fill_n(pDst, Missing, 0.0f);

	const int Start = (m_RingWritePos - Available + m_BassFftSize) % m_BassFftSize;
	for(int i = 0; i < Available; ++i)
		pDst[Missing + i] = m_vRingBuffer[(Start + i) % m_BassFftSize];
}

void CVisualizerAnalyzer::RunFft(std::vector<std::complex<float>> &vBuffer, const std::vector<std::complex<float>> &vTwiddles, const std::vector<int> &vBitReverse) const
{
	const int Size = (int)vBuffer.size();
	for(int i = 0; i < Size; ++i)
	{
		const int Target = vBitReverse[i];
		if(Target > i)
			std::swap(vBuffer[i], vBuffer[Target]);
	}

	for(int Length = 2; Length <= Size; Length <<= 1)
	{
		const int HalfLength = Length >> 1;
		const int TwiddleStep = Size / Length;
		for(int Start = 0; Start < Size; Start += Length)
		{
			for(int Offset = 0; Offset < HalfLength; ++Offset)
			{
				const std::complex<float> Twiddle = vTwiddles[Offset * TwiddleStep];
				const std::complex<float> Even = vBuffer[Start + Offset];
				const std::complex<float> Odd = vBuffer[Start + Offset + HalfLength] * Twiddle;
				vBuffer[Start + Offset] = Even + Odd;
				vBuffer[Start + Offset + HalfLength] = Even - Odd;
			}
		}
	}
}

void CVisualizerAnalyzer::Analyze(SVisualizerFrame &OutFrame)
{
	OutFrame = SVisualizerFrame();
	OutFrame.m_BackendStatus = EVisualizerBackendStatus::LIVE;
	OutFrame.m_IsPassiveFallback = false;
	OutFrame.m_SampleRate = m_Config.m_SampleRate;
	if(m_vRingBuffer.empty())
		return;

	CopyLatestSamples(m_vMainSamples.data(), m_MainFftSize);
	CopyLatestSamples(m_vBassSamples.data(), m_BassFftSize);

	float Peak = 0.0f;
	double RmsAccum = 0.0;
	for(int i = 0; i < m_MainFftSize; ++i)
	{
		const float Sample = m_vMainSamples[i];
		Peak = maximum(Peak, absolute(Sample));
		RmsAccum += (double)Sample * (double)Sample;
		m_vMainBuffer[i] = std::complex<float>(Sample * m_vMainWindow[i], 0.0f);
	}
	for(int i = 0; i < m_BassFftSize; ++i)
		m_vBassBuffer[i] = std::complex<float>(m_vBassSamples[i] * m_vBassWindow[i], 0.0f);

	RunFft(m_vMainBuffer, m_vMainTwiddles, m_vMainBitReverse);
	RunFft(m_vBassBuffer, m_vBassTwiddles, m_vBassBitReverse);

	OutFrame.m_Peak = Peak;
	OutFrame.m_Rms = sqrtf((float)(RmsAccum / maximum(1, m_MainFftSize)));
	for(int Band = 0; Band < m_Config.m_BandCount; ++Band)
	{
		const bool IsBass = Band < m_BassBandCount;
		const int Lower = IsBass ? m_vBassLowerCutOff[Band] : m_vMainLowerCutOff[Band];
		const int Upper = IsBass ? m_vBassUpperCutOff[Band] : m_vMainUpperCutOff[Band];
		float MagnitudeSum = 0.0f;
		float PeakMagnitude = 0.0f;
		for(int Bin = Lower; Bin <= Upper; ++Bin)
		{
			const std::complex<float> Value = IsBass ? m_vBassBuffer[Bin] : m_vMainBuffer[Bin];
			const float BinMagnitude = std::abs(Value);
			MagnitudeSum += BinMagnitude;
			PeakMagnitude = maximum(PeakMagnitude, BinMagnitude);
		}
		float Magnitude = (MagnitudeSum + PeakMagnitude * 0.35f) / maximum(1.0f, (float)(IsBass ? m_BassFftSize : m_MainFftSize));
		if(!std::isfinite(Magnitude))
			Magnitude = 0.0f;
		OutFrame.m_aBands[Band] = maximum(0.0f, Magnitude * m_vEq[Band]);
	}

	const float SignalThreshold = 0.0032f / m_Config.m_Sensitivity;
	const float PeakThreshold = 0.0085f / m_Config.m_Sensitivity;
	OutFrame.m_HasRealtimeSignal = OutFrame.m_Rms >= SignalThreshold || OutFrame.m_Peak >= PeakThreshold;
}

void BuildRenderBars(const SVisualizerFrame &Frame, float *pOutBars, int RequestedBarCount)
{
	if(pOutBars == nullptr || RequestedBarCount <= 0)
		return;

	for(int i = 0; i < RequestedBarCount; ++i)
		pOutBars[i] = 0.0f;

	if(RequestedBarCount == 0)
		return;

	if(RequestedBarCount == 5)
	{
		for(int Bar = 0; Bar < RequestedBarCount; ++Bar)
		{
			const SCompactBarRange &Range = gs_aCompactBarRanges[Bar];
			float Sum = 0.0f;
			float WeightSum = 0.0f;
			float Peak = 0.0f;
			const int Width = maximum(1, Range.m_End - Range.m_Start);
			for(int Band = Range.m_Start; Band < Range.m_End; ++Band)
			{
				const float LocalT = Width > 1 ? (Band - Range.m_Start) / (float)(Width - 1) : 0.0f;
				const float Weight = 1.28f - 0.34f * LocalT;
				const float Value = Frame.m_aBands[Band];
				Sum += Value * Weight;
				WeightSum += Weight;
				Peak = maximum(Peak, Value);
			}
			const float Average = WeightSum > 0.0f ? Sum / WeightSum : 0.0f;
			float CompactValue = (Average * 0.46f + Peak * 0.54f) * Range.m_Emphasis * 4.45f;
			CompactValue = std::pow(std::clamp(CompactValue, 0.0f, 1.0f), 0.92f);
			pOutBars[Bar] = std::clamp(CompactValue, 0.0f, 1.0f);
		}
		return;
	}

	for(int Bar = 0; Bar < RequestedBarCount; ++Bar)
	{
		const float Start = (float)Bar * MAX_VISUALIZER_BANDS / RequestedBarCount;
		const float End = (float)(Bar + 1) * MAX_VISUALIZER_BANDS / RequestedBarCount;
		const int IndexStart = std::clamp((int)floorf(Start), 0, MAX_VISUALIZER_BANDS - 1);
		const int IndexEnd = std::clamp((int)ceilf(End), IndexStart + 1, MAX_VISUALIZER_BANDS);
		float Sum = 0.0f;
		float WeightSum = 0.0f;
		for(int Band = IndexStart; Band < IndexEnd; ++Band)
		{
			const float SegmentStart = maximum(Start, (float)Band);
			const float SegmentEnd = minimum(End, (float)(Band + 1));
			const float Span = maximum(0.001f, SegmentEnd - SegmentStart);
			const float Focus = 0.85f + 0.45f * (1.0f - Band / maximum(1.0f, (float)(MAX_VISUALIZER_BANDS - 1)));
			Sum += Frame.m_aBands[Band] * Span * Focus;
			WeightSum += Span * Focus;
		}
		if(WeightSum > 0.0f)
			pOutBars[Bar] = std::clamp(Sum / WeightSum, 0.0f, 1.0f);
	}
}

} // namespace BestClientVisualizer
