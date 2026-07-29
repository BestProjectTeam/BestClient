/* Copyright © 2026 BestProject Team */
#include "analyzer.h"

#include <base/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace BestClientVisualizer
{

namespace
{
constexpr float PI = 3.14159265358979323846f;
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

void CVisualizerAnalyzer::ResetBandState()
{
	m_aNoiseFloor.fill(1e-5f);
	m_aBandPeak.fill(0.002f);
	m_aSmoothedBands.fill(0.0f);
	m_BandEnergyPeak = 1e-6f;
}

void CVisualizerAnalyzer::FadeBandsToSilence(float Factor)
{
	for(float &Value : m_aSmoothedBands)
	{
		Value *= Factor;
		if(Value < 0.001f)
			Value = 0.0f;
	}
}

float CVisualizerAnalyzer::BandEdgeHz(float T)
{
	static constexpr float s_aBandEdges[7] = {20.0f, 220.0f, 520.0f, 1200.0f, 2800.0f, 7000.0f, 17000.0f};
	T = std::clamp(T, 0.0f, 1.0f);
	const float Pos = T * 6.0f;
	const int Seg = std::clamp((int)Pos, 0, 5);
	const float Frac = std::clamp(Pos - (float)Seg, 0.0f, 1.0f);
	const float A = logf(maximum(1.0f, s_aBandEdges[Seg]));
	const float B = logf(maximum(1.0f, s_aBandEdges[Seg + 1]));
	return expf(A + (B - A) * Frac);
}

float CVisualizerAnalyzer::BandEq(float BandT)
{
	static constexpr float s_aBandEq[6] = {1.08f, 1.06f, 1.02f, 1.10f, 1.40f, 1.58f};
	BandT = std::clamp(BandT, 0.0f, 1.0f);
	const float Pos = BandT * 5.0f;
	const int Seg = std::clamp((int)Pos, 0, 4);
	const float Frac = std::clamp(Pos - (float)Seg, 0.0f, 1.0f);
	return mix(s_aBandEq[Seg], s_aBandEq[minimum(Seg + 1, 5)], Frac);
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

	const bool PlanChanged =
		m_MainFftSize == 0 ||
		Sanitized.m_SampleRate != m_Config.m_SampleRate ||
		Sanitized.m_BandCount != m_Config.m_BandCount ||
		Sanitized.m_LowCutHz != m_Config.m_LowCutHz ||
		Sanitized.m_HighCutHz != m_Config.m_HighCutHz ||
		Sanitized.m_BassSplitHz != m_Config.m_BassSplitHz;

	m_Config = Sanitized;
	if(PlanChanged)
	{
		ResetBandState();
		RebuildPlan();
	}
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

	const int BandCount = std::clamp(m_Config.m_BandCount, 1, MAX_VISUALIZER_BANDS);
	const int AnalyzeWindow = m_MainFftSize;
	if(m_vRingBuffer.empty() || AnalyzeWindow < 4)
	{
		FadeBandsToSilence(0.90f);
		for(int Band = 0; Band < BandCount; ++Band)
			OutFrame.m_aBands[Band] = m_aSmoothedBands[Band];
		OutFrame.m_HasRealtimeSignal = false;
		return;
	}

	CopyLatestSamples(m_vMainSamples.data(), AnalyzeWindow);

	float Mean = 0.0f;
	for(int i = 0; i < AnalyzeWindow; ++i)
		Mean += m_vMainSamples[i];
	Mean /= (float)AnalyzeWindow;

	float InputEnergy = 0.0f;
	float Peak = 0.0f;
	for(int i = 0; i < AnalyzeWindow; ++i)
	{
		const float Sample = m_vMainSamples[i] - Mean;
		Peak = maximum(Peak, absolute(Sample));
		InputEnergy += Sample * Sample;
		const float WindowMul = 0.5f - 0.5f * cosf((2.0f * PI * i) / maximum(1, AnalyzeWindow - 1));
		m_vMainBuffer[i] = std::complex<float>(Sample * WindowMul, 0.0f);
	}
	for(int i = AnalyzeWindow; i < m_MainFftSize; ++i)
		m_vMainBuffer[i] = std::complex<float>(0.0f, 0.0f);

	const float InputRms = sqrtf(InputEnergy / (float)AnalyzeWindow);
	OutFrame.m_Peak = Peak;
	OutFrame.m_Rms = InputRms;

	constexpr float AUDIBLE_INPUT_RMS_THRESHOLD = 0.00018f;
	if(InputRms < AUDIBLE_INPUT_RMS_THRESHOLD)
	{
		m_aSmoothedBands.fill(0.0f);
		OutFrame.m_HasRealtimeSignal = false;
		return;
	}

	if(InputRms > m_BandEnergyPeak)
		m_BandEnergyPeak = m_BandEnergyPeak * 0.88f + InputRms * 0.12f;
	else
		m_BandEnergyPeak = m_BandEnergyPeak * 0.995f + InputRms * 0.005f;
	m_BandEnergyPeak = std::clamp(m_BandEnergyPeak, 1e-6f, 0.25f);
	// Slight headroom for quieter loopback levels without flattening all bars.
	const float GlobalRmsGain = std::clamp(0.018f / m_BandEnergyPeak, 0.35f, 4.5f);

	RunFft(m_vMainBuffer, m_vMainTwiddles, m_vMainBitReverse);

	const float SampleRate = (float)m_Config.m_SampleRate;
	const float BinHz = SampleRate / (float)AnalyzeWindow;
	std::array<float, MAX_VISUALIZER_BANDS> aBandNormalized{};
	std::array<float, MAX_VISUALIZER_BANDS> aBandClean{};
	aBandNormalized.fill(0.0f);
	aBandClean.fill(0.0f);

	for(int Band = 0; Band < BandCount; ++Band)
	{
		const float T0 = Band / (float)BandCount;
		const float T1 = (Band + 1) / (float)BandCount;
		const float FMin = maximum(BandEdgeHz(T0), BinHz);
		const float FMax = minimum(BandEdgeHz(T1), SampleRate * 0.49f);
		if(FMax <= FMin)
			continue;

		const int KMin = std::clamp((int)floorf(FMin / BinHz), 1, AnalyzeWindow / 2 - 1);
		const int KMax = std::clamp((int)ceilf(FMax / BinHz), KMin, AnalyzeWindow / 2 - 1);
		const float Center = (FMin + FMax) * 0.5f;
		const float Half = maximum(1.0f, (FMax - FMin) * 0.5f);
		float WeightedPower = 0.0f;
		float WeightSum = 0.0f;
		for(int K = KMin; K <= KMax; ++K)
		{
			const float Freq = K * BinHz;
			const float DistNorm = absolute(Freq - Center) / Half;
			const float Weight = maximum(0.15f, 1.0f - DistNorm);
			const float Mag = std::abs(m_vMainBuffer[K]) * (2.0f / (float)AnalyzeWindow);
			const float Power = Mag * Mag;
			WeightedPower += Power * Weight;
			WeightSum += Weight;
		}
		if(WeightSum <= 0.0f)
			continue;

		const float Level = sqrtf(WeightedPower / WeightSum);
		float &Noise = m_aNoiseFloor[Band];
		if(Level < Noise)
			Noise = Noise * 0.92f + Level * 0.08f;
		else
			Noise = Noise * 0.998f + Level * 0.002f;

		const float Clean = maximum(0.0f, (Level - Noise * 1.25f) * GlobalRmsGain);
		aBandClean[Band] = Clean;

		float &BandPeak = m_aBandPeak[Band];
		if(Clean > BandPeak)
			BandPeak = Clean;
		else
			BandPeak = BandPeak * 0.9985f + Clean * 0.0015f;
		BandPeak = maximum(BandPeak, 1e-5f);

		const float Normalized = std::clamp(Clean / BandPeak, 0.0f, 1.0f);
		aBandNormalized[Band] = powf(Normalized, 0.62f);
	}

	float SumClean = 0.0f;
	for(int Band = 0; Band < BandCount; ++Band)
		SumClean += aBandClean[Band];
	if(SumClean > 1e-8f)
	{
		const float InvSum = 1.0f / SumClean;
		for(int Band = 0; Band < BandCount; ++Band)
		{
			const float BandT = BandCount > 1 ? Band / (float)(BandCount - 1) : 0.0f;
			const float Share = std::clamp((aBandClean[Band] * InvSum) * (float)BandCount, 0.0f, 1.35f);
			const float ShareWeighted = powf(Share, 0.78f);
			const float MixVal = aBandNormalized[Band] * 0.74f + ShareWeighted * 0.26f;
			aBandNormalized[Band] = std::clamp(MixVal * BandEq(BandT), 0.0f, 1.0f);
		}
	}

	if(BandCount >= 3)
	{
		const float Bass = aBandClean[0];
		const float LowMid = aBandClean[1] + aBandClean[2];
		if(Bass > 0.0f && LowMid > 0.0f)
		{
			const float Dominance = Bass / LowMid;
			const float Damp = std::clamp((Dominance - 1.08f) * 0.34f, 0.0f, 0.40f);
			aBandNormalized[1] *= (1.0f - Damp);
		}
	}

	std::array<float, MAX_VISUALIZER_BANDS> aBandCrossSmoothed{};
	aBandCrossSmoothed.fill(0.0f);
	for(int Band = 0; Band < BandCount; ++Band)
	{
		const float Prev = aBandNormalized[Band > 0 ? Band - 1 : Band];
		const float Curr = aBandNormalized[Band];
		const float Next = aBandNormalized[Band + 1 < BandCount ? Band + 1 : Band];
		aBandCrossSmoothed[Band] = Prev * 0.06f + Curr * 0.88f + Next * 0.06f;
	}

	if(BandCount >= 2)
	{
		float OthersSum = 0.0f;
		for(int Band = 1; Band < BandCount; ++Band)
			OthersSum += aBandCrossSmoothed[Band];
		const float OthersMean = OthersSum / (float)(BandCount - 1);
		const float BassLimit = OthersMean > 0.03f ? (OthersMean * 3.20f + 0.22f) : 1.00f;
		if(aBandCrossSmoothed[0] > BassLimit)
			aBandCrossSmoothed[0] = BassLimit + (aBandCrossSmoothed[0] - BassLimit) * 0.85f;
	}

	for(int Band = 0; Band < BandCount; ++Band)
	{
		const float BandT = BandCount > 1 ? Band / (float)(BandCount - 1) : 0.0f;
		const float BandGain = Band == 0 ? 1.20f : (BandT <= 0.55f ? 1.22f : 1.30f);
		const float Target = std::clamp(aBandCrossSmoothed[Band] * BandGain, 0.0f, 1.0f);
		const float Attack = 0.86f;
		const float Release = 0.30f;
		const float Blend = Target > m_aSmoothedBands[Band] ? Attack : Release;
		m_aSmoothedBands[Band] += (Target - m_aSmoothedBands[Band]) * Blend;
		OutFrame.m_aBands[Band] = std::clamp(m_aSmoothedBands[Band], 0.0f, 1.0f);
	}

	OutFrame.m_HasRealtimeSignal = true;
}

void BuildRenderBars(const SVisualizerFrame &Frame, float *pOutBars, int RequestedBarCount)
{
	if(pOutBars == nullptr || RequestedBarCount <= 0)
		return;

	for(int i = 0; i < RequestedBarCount; ++i)
		pOutBars[i] = 0.0f;

	RequestedBarCount = std::clamp(RequestedBarCount, 1, MAX_VISUALIZER_BANDS);
	for(int Bar = 0; Bar < RequestedBarCount; ++Bar)
	{
		const int Start = (Bar * MAX_VISUALIZER_BANDS) / RequestedBarCount;
		const int End = maximum(Start + 1, ((Bar + 1) * MAX_VISUALIZER_BANDS) / RequestedBarCount);
		float Sum = 0.0f;
		float Peak = 0.0f;
		int Count = 0;
		for(int Band = Start; Band < End; ++Band)
		{
			const float Value = Frame.m_aBands[Band];
			Sum += Value;
			Peak = maximum(Peak, Value);
			++Count;
		}
		const float Average = Count > 0 ? Sum / (float)Count : 0.0f;
		pOutBars[Bar] = std::clamp(Average * 0.40f + Peak * 0.60f, 0.0f, 1.0f);
	}
}

} // namespace BestClientVisualizer
