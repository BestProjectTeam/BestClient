/* (c) BestClient */
#include "audio_visualizer.h"

#include <game/client/components/bestclient/audio_capture_device_score.h>
#include <game/client/gameclient.h>

#include <base/log.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/shared/config.h>

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace
{
static constexpr int VIS_SAMPLE_RATE = 48000;
static constexpr float PI = 3.14159265358979323846f;

static float ApproachAnim(float Current, float Target, float Delta, float Speed)
{
	const float Step = 1.0f - expf(-maximum(0.0f, Speed) * maximum(0.0f, Delta));
	return mix(Current, Target, std::clamp(Step, 0.0f, 1.0f));
}

static bool IsPowerOfTwo(int v)
{
	return v > 0 && (v & (v - 1)) == 0;
}

static int ClampFftSize(int Size)
{
	if(Size <= 1024)
		return 1024;
	if(Size <= 2048)
		return 2048;
	return 4096;
}

static void FftRadix2(std::vector<CAudioVisualizer::SComplex> &vData)
{
	const int n = (int)vData.size();
	if(n <= 1 || !IsPowerOfTwo(n))
		return;

	for(int i = 1, j = 0; i < n; i++)
	{
		int bit = n >> 1;
		for(; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if(i < j)
			std::swap(vData[i], vData[j]);
	}

	for(int len = 2; len <= n; len <<= 1)
	{
		const float ang = -2.0f * PI / (float)len;
		for(int i = 0; i < n; i += len)
		{
			for(int j = 0; j < len / 2; ++j)
			{
				const float a = ang * (float)j;
				const float wr = cosf(a);
				const float wi = sinf(a);

				const int i0 = i + j;
				const int i1 = i + j + len / 2;
				const auto &u = vData[i0];
				const auto &x = vData[i1];
				const float vr = x.r * wr - x.i * wi;
				const float vi = x.r * wi + x.i * wr;

				vData[i0].r = u.r + vr;
				vData[i0].i = u.i + vi;
				vData[i1].r = u.r - vr;
				vData[i1].i = u.i - vi;
			}
		}
	}
}

static int ResolveAutoLoopbackCaptureDeviceIndex(int *pOutScore)
{
	const int DeviceCount = SDL_GetNumAudioDevices(1);
	int BestIndex = -1;
	int BestScore = -100000;
	for(int i = 0; i < DeviceCount; ++i)
	{
		const char *pName = SDL_GetAudioDeviceName(i, 1);
		const int Score = ScoreCaptureDeviceName(pName);
		if(Score > BestScore)
		{
			BestScore = Score;
			BestIndex = i;
		}
	}

	// Only auto-select if there is a reasonably strong signal that it's a loopback/monitor device.
	// Lower than the old keyword-only threshold so names like "Аналоговый стерео" can still be picked.
	static constexpr int MIN_ACCEPT_SCORE = 20;
	if(pOutScore != nullptr)
		*pOutScore = BestScore;
	return BestScore >= MIN_ACCEPT_SCORE ? BestIndex : -1;
}
} // namespace

void CAudioVisualizer::CPcmRingBuffer::Reset(size_t CapacitySamples)
{
	m_vData.assign(CapacitySamples, 0);
	Clear();
}

void CAudioVisualizer::CPcmRingBuffer::Clear()
{
	m_Head = 0;
	m_Size = 0;
}

void CAudioVisualizer::CPcmRingBuffer::PushBack(const int16_t *pSamples, size_t Count)
{
	if(pSamples == nullptr || Count == 0 || m_vData.empty())
		return;

	if(Count >= m_vData.size())
	{
		pSamples += Count - m_vData.size();
		Count = m_vData.size();
		Clear();
	}

	const size_t Overflow = m_Size + Count > m_vData.size() ? m_Size + Count - m_vData.size() : 0;
	if(Overflow > 0)
		DiscardFront(Overflow);

	size_t Tail = (m_Head + m_Size) % m_vData.size();
	size_t Remaining = Count;
	size_t Offset = 0;
	while(Remaining > 0)
	{
		const size_t Chunk = minimum(Remaining, m_vData.size() - Tail);
		for(size_t i = 0; i < Chunk; ++i)
			m_vData[Tail + i] = pSamples[Offset + i];
		Tail = (Tail + Chunk) % m_vData.size();
		Offset += Chunk;
		Remaining -= Chunk;
	}
	m_Size += Count;
}

void CAudioVisualizer::CPcmRingBuffer::DiscardFront(size_t Count)
{
	const size_t Actual = minimum(Count, m_Size);
	if(m_vData.empty())
		return;
	m_Head = (m_Head + Actual) % m_vData.size();
	m_Size -= Actual;
}

void CAudioVisualizer::CPcmRingBuffer::CopyLast(size_t Count, int16_t *pDst) const
{
	if(pDst == nullptr || Count == 0)
		return;

	const size_t Actual = minimum(Count, m_Size);
	const size_t Start = (m_Head + m_Size - Actual) % m_vData.size();
	for(size_t i = 0; i < Actual; ++i)
		pDst[i] = m_vData[(Start + i) % m_vData.size()];
	for(size_t i = Actual; i < Count; ++i)
		pDst[i] = 0;
}

void CAudioVisualizer::OnReset()
{
	CloseCapture();
	m_Pcm.Clear();
	m_HasSignal = false;
	m_DumpedDevices = false;
	m_LastRms = 0.0f;
	m_LastMaxAbs = 0.0f;
	m_LastSpectrumUpdateTick = 0;
	m_RuntimeState = ESubsystemRuntimeState::DISABLED;
	m_LastPerfReportTick = 0;
	m_LastUpdateCostTick = 0;
	m_MaxUpdateCostTick = 0;
	m_TotalUpdateCostTick = 0;
	m_UpdateSamples = 0;
	for(float &B : m_vBands)
		B = 0.0f;
}

void CAudioVisualizer::OnShutdown()
{
	CloseCapture();
}

void CAudioVisualizer::OnStateChange(int NewState, int OldState)
{
	(void)OldState;
	if(NewState != IClient::STATE_ONLINE && NewState != IClient::STATE_DEMOPLAYBACK)
		OnReset();
}

void CAudioVisualizer::CloseCapture()
{
	if(m_CaptureDevice != 0)
	{
		SDL_CloseAudioDevice(m_CaptureDevice);
		m_CaptureDevice = 0;
	}
	m_CaptureSpec = {};
	m_CaptureValid = false;
	m_CaptureDeviceName.clear();
	m_LastCaptureConfigIndex = -2;
}

bool CAudioVisualizer::NeedCapture() const
{
	const bool CavaWantsCapture = g_Config.m_BcCavaEnable != 0 || g_Config.m_DbgCava != 0;
	const bool CavaAllowed = Client()->State() == IClient::STATE_ONLINE && GameClient() && !GameClient()->m_Menus.IsActive();
	if(CavaWantsCapture && CavaAllowed)
		return true;

	if(GameClient() == nullptr)
		return false;

	CMusicPlayer::SNowPlayingInfo NowPlaying;
	return GameClient()->m_MusicPlayer.GetNowPlayingInfo(NowPlaying) && NowPlaying.m_Playing;
}

bool CAudioVisualizer::EnsureCaptureOpen()
{
	const int ConfigCaptureIndex = g_Config.m_BcCavaCaptureDevice;
	if(m_CaptureValid && m_CaptureDevice != 0 && m_LastCaptureConfigIndex == ConfigCaptureIndex)
		return true;

	CloseCapture();

#ifndef SDL_HINT_AUDIO_INCLUDE_MONITORS
#define SDL_HINT_AUDIO_INCLUDE_MONITORS "SDL_AUDIO_INCLUDE_MONITORS"
#endif
	SDL_SetHint(SDL_HINT_AUDIO_INCLUDE_MONITORS, "1");

	if(SDL_WasInit(SDL_INIT_AUDIO) == 0)
	{
		if(SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
		{
			dbg_msg("audiovis", "failed to init SDL audio: %s", SDL_GetError());
			return false;
		}
	}

	SDL_AudioSpec Wanted = {};
	Wanted.freq = VIS_SAMPLE_RATE;
	Wanted.format = AUDIO_S16SYS;
	Wanted.channels = 2;
	Wanted.samples = 1024;
	Wanted.callback = nullptr;

	const int DeviceCount = SDL_GetNumAudioDevices(1);
	const char *pManualName = (ConfigCaptureIndex >= 0 && ConfigCaptureIndex < DeviceCount) ? SDL_GetAudioDeviceName(ConfigCaptureIndex, 1) : nullptr;
	const bool ManualOk = pManualName != nullptr && IsSystemLoopbackDeviceName(pManualName);

	int AutoScore = 0;
	const int AutoIndex = ResolveAutoLoopbackCaptureDeviceIndex(&AutoScore);
	const char *pDeviceName = nullptr;
	if(ManualOk)
		pDeviceName = pManualName;
	else
		pDeviceName = AutoIndex >= 0 ? SDL_GetAudioDeviceName(AutoIndex, 1) : nullptr;

	m_LastCaptureConfigIndex = ConfigCaptureIndex;

	if(pDeviceName == nullptr || pDeviceName[0] == '\0')
	{
		m_CaptureDeviceName = "no loopback/monitor device";
		return false;
	}

	m_CaptureDeviceName = pDeviceName;
	m_CaptureDevice = SDL_OpenAudioDevice(pDeviceName, 1, &Wanted, &m_CaptureSpec, SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
	if(m_CaptureDevice == 0)
	{
		dbg_msg("audiovis", "failed to open capture device: %s", SDL_GetError());
		m_CaptureDeviceName.clear();
		return false;
	}
	if(m_CaptureSpec.freq != VIS_SAMPLE_RATE || m_CaptureSpec.format != AUDIO_S16SYS || m_CaptureSpec.channels <= 0)
	{
		dbg_msg("audiovis", "capture format unsupported (need 48kHz s16)");
		CloseCapture();
		return false;
	}

	SDL_ClearQueuedAudio(m_CaptureDevice);
	SDL_PauseAudioDevice(m_CaptureDevice, 0);
	m_CaptureValid = true;
	return true;
}

void CAudioVisualizer::EnsureBuffers()
{
	const int FftSize = ClampFftSize(g_Config.m_BcCavaFftSize);
	const int Bands = std::clamp(g_Config.m_BcCavaBars, 4, 128);

	if(m_LastFftSize != FftSize)
	{
		m_LastFftSize = FftSize;
		m_vWindow.resize(FftSize);
		for(int i = 0; i < FftSize; ++i)
		{
			const float t = (float)i / (float)(maximum(1, FftSize - 1));
			m_vWindow[i] = 0.5f - 0.5f * cosf(2.0f * PI * t);
		}
		m_vFft.resize(FftSize);
	}

	if(m_LastBands != Bands)
	{
		m_LastBands = Bands;
		m_vBands.assign(Bands, 0.0f);
	}

	if(m_vMixedSamples.size() < 8192)
		m_vMixedSamples.resize(8192);
	if(m_vFrameSamples.size() != (size_t)FftSize)
		m_vFrameSamples.resize(FftSize);

	if(m_Pcm.Capacity() == 0 && m_LastFftSize > 0)
		m_Pcm.Reset((size_t)VIS_SAMPLE_RATE * 2);
}

void CAudioVisualizer::CaptureAudio()
{
	if(!m_CaptureValid || m_CaptureDevice == 0)
		return;

	int16_t aTemp[8192];
	const int Channels = maximum(1, (int)m_CaptureSpec.channels);

	for(;;)
	{
		const int BytesRead = SDL_DequeueAudio(m_CaptureDevice, aTemp, (int)sizeof(aTemp));
		if(BytesRead <= 0)
			break;

		const int SamplesRead = BytesRead / (int)sizeof(int16_t);
		if(Channels <= 1)
		{
			m_Pcm.PushBack(aTemp, (size_t)SamplesRead);
		}
		else
		{
			const int Frames = SamplesRead / Channels;
			for(int f = 0; f < Frames; ++f)
			{
				int Sum = 0;
				for(int c = 0; c < Channels; ++c)
					Sum += aTemp[f * Channels + c];
				m_vMixedSamples[f] = (int16_t)(Sum / Channels);
			}
			m_Pcm.PushBack(m_vMixedSamples.data(), (size_t)Frames);
		}
	}
}

void CAudioVisualizer::ComputeBands(float Delta, bool Force)
{
	EnsureBuffers();
	m_HasSignal = false;
	m_LastRms = 0.0f;
	m_LastMaxAbs = 0.0f;

	const int FftSize = m_LastFftSize;
	const int Bands = m_LastBands;
	if(FftSize <= 0 || Bands <= 0)
		return;

	const int64_t Now = time_get();
	const int64_t SpectrumInterval = time_freq() / 40;
	if(!CSubsystemTicker::ShouldRunPeriodic(Now, m_LastSpectrumUpdateTick, SpectrumInterval, Force))
		return;

	if(m_Pcm.Size() < (size_t)FftSize)
	{
		for(float &B : m_vBands)
			B = ApproachAnim(B, 0.0f, Delta, 6.0f);
		return;
	}

	m_Pcm.CopyLast(FftSize, m_vFrameSamples.data());
	// Don't discard here: render FPS can be higher than audio chunk rate (e.g. 60 FPS vs 1024 samples @ 48kHz),
	// and discarding every frame can cause underflow flicker (rms=0) even while audio is playing.

	float Rms = 0.0f;
	float MaxAbs = 0.0f;
	for(int i = 0; i < FftSize; ++i)
	{
		const float s = (float)m_vFrameSamples[i] / (float)std::numeric_limits<int16_t>::max();
		Rms += s * s;
		MaxAbs = maximum(MaxAbs, absolute(s));
		m_vFft[i].r = s * m_vWindow[i];
		m_vFft[i].i = 0.0f;
	}
	Rms = sqrtf(Rms / maximum(1.0f, (float)FftSize));
	m_LastRms = Rms;
	m_LastMaxAbs = MaxAbs;
	// Conservative thresholds (avoid flicker). If you have loopback audio and still see signal=0, lower these.
	m_HasSignal = Rms > 0.0010f || MaxAbs > 0.010f;

	FftRadix2(m_vFft);

	const float Gain = std::clamp(g_Config.m_BcCavaGain / 100.0f, 0.1f, 10.0f);
	const float AttackSpeed = 2.0f + (std::clamp(g_Config.m_BcCavaAttack, 1, 100) / 100.0f) * 18.0f;
	const float DecaySpeed = 1.0f + (std::clamp(g_Config.m_BcCavaDecay, 1, 100) / 100.0f) * 12.0f;

	const int LowHz = std::clamp(g_Config.m_BcCavaLowHz, 20, 500);
	const int HighHz = std::clamp(g_Config.m_BcCavaHighHz, 1000, 20000);
	const float Nyquist = 0.5f * (float)VIS_SAMPLE_RATE;
	const float F0 = std::clamp((float)LowHz, 1.0f, Nyquist);
	const float F1 = std::clamp((float)HighHz, F0 + 1.0f, Nyquist);
	const float Ratio = powf(F1 / F0, 1.0f / (float)Bands);

	const int MaxBin = FftSize / 2;
	const float BinHz = (float)VIS_SAMPLE_RATE / (float)FftSize;
	const float MagNorm = 1.0f / maximum(1.0f, (float)FftSize);

	for(int b = 0; b < Bands; ++b)
	{
		const float BandStart = F0 * powf(Ratio, (float)b);
		const float BandEnd = F0 * powf(Ratio, (float)(b + 1));
		int k0 = (int)std::ceil(BandStart / BinHz);
		int k1 = (int)std::floor(BandEnd / BinHz);
		k0 = std::clamp(k0, 1, MaxBin);
		k1 = std::clamp(k1, k0, MaxBin);

		float Acc = 0.0f;
		int Count = 0;
		for(int k = k0; k <= k1; ++k)
		{
			const float mr = m_vFft[k].r;
			const float mi = m_vFft[k].i;
			// FFT output is not normalized (magnitude grows with FFT size). Normalize so the dB mapping doesn't
			// saturate to 1.0 on typical signals.
			const float Mag = sqrtf(mr * mr + mi * mi) * MagNorm;
			Acc += Mag;
			++Count;
		}
		const float Avg = Count > 0 ? Acc / (float)Count : 0.0f;
		const float Db = 20.0f * log10f(Avg + 1e-6f);
		float Target = std::clamp((Db + 60.0f) / 60.0f, 0.0f, 1.0f);
		Target = std::clamp(Target * Gain, 0.0f, 1.0f);
		const float Speed = Target > m_vBands[b] ? AttackSpeed : DecaySpeed;
		m_vBands[b] = ApproachAnim(m_vBands[b], Target, Delta, Speed);
	}
}

void CAudioVisualizer::OnUpdate()
{
	const int64_t PerfStart = time_get();
	if(!NeedCapture())
	{
		m_RuntimeState = ESubsystemRuntimeState::DISABLED;
		if(m_CaptureDevice != 0)
			OnReset();
		return;
	}

	EnsureBuffers();
	m_RuntimeState = ESubsystemRuntimeState::ARMED;
	if(!EnsureCaptureOpen())
	{
		m_CaptureValid = false;
		m_HasSignal = false;
		const float Delta = std::clamp(Client()->RenderFrameTime(), 0.0f, 0.1f);
		for(float &B : m_vBands)
			B = ApproachAnim(B, 0.0f, Delta, 6.0f);
		return;
	}
	m_RuntimeState = ESubsystemRuntimeState::ACTIVE;

	if(g_Config.m_DbgCava >= 2 && !m_DumpedDevices)
	{
		const char *pDriver = SDL_GetCurrentAudioDriver();
		dbg_msg("cava", "audio driver=%s", pDriver ? pDriver : "(null)");
		const int DeviceCount = SDL_GetNumAudioDevices(1);
		dbg_msg("cava", "capture devices=%d", DeviceCount);
		for(int i = 0; i < DeviceCount; ++i)
		{
			const char *pName = SDL_GetAudioDeviceName(i, 1);
			dbg_msg("cava", "  dev[%d]=%s", i, pName ? pName : "(null)");
		}
		int AutoScore = 0;
		const int LoopbackIndex = ResolveAutoLoopbackCaptureDeviceIndex(&AutoScore);
		dbg_msg("cava", "auto loopback index=%d score=%d", LoopbackIndex, AutoScore);
		m_DumpedDevices = true;
	}

	CaptureAudio();
	const float Delta = std::clamp(Client()->RenderFrameTime(), 0.0f, 0.1f);
	ComputeBands(Delta, false);

	if(g_Config.m_DbgCava >= 2)
	{
		const int64_t Now = time_get();
		if(m_LastDebugPrintTick == 0 || Now - m_LastDebugPrintTick >= time_freq())
		{
			const char *pDevName = !m_CaptureDeviceName.empty() ? m_CaptureDeviceName.c_str() : "(default/unknown)";
			dbg_msg("cava", "capture=%s dev=%s freq=%d ch=%d pcm=%zu signal=%d rms=%.5f max=%.5f bands=%zu",
				m_CaptureValid ? "ok" : "no",
				pDevName,
				m_CaptureSpec.freq,
				m_CaptureSpec.channels,
				m_Pcm.Size(),
				m_HasSignal ? 1 : 0,
				(double)m_LastRms,
				(double)m_LastMaxAbs,
				m_vBands.size());
			m_LastDebugPrintTick = Now;
		}
	}

	m_LastUpdateCostTick = time_get() - PerfStart;
	m_MaxUpdateCostTick = maximum(m_MaxUpdateCostTick, m_LastUpdateCostTick);
	m_TotalUpdateCostTick += m_LastUpdateCostTick;
	++m_UpdateSamples;
	if(g_Config.m_DbgCava >= 1)
	{
		const int64_t Now = time_get();
		if(m_LastPerfReportTick == 0 || Now - m_LastPerfReportTick >= time_freq())
		{
			dbg_msg("cava/perf", "update last=%.3fms avg=%.3fms max=%.3fms samples=%lld capture=%d",
				m_LastUpdateCostTick * 1000.0 / (double)time_freq(),
				m_UpdateSamples > 0 ? (m_TotalUpdateCostTick * 1000.0 / (double)time_freq()) / (double)m_UpdateSamples : 0.0,
				m_MaxUpdateCostTick * 1000.0 / (double)time_freq(),
				(long long)m_UpdateSamples,
				m_CaptureDevice != 0 ? 1 : 0);
			m_LastPerfReportTick = Now;
			m_TotalUpdateCostTick = 0;
			m_UpdateSamples = 0;
			m_MaxUpdateCostTick = 0;
		}
	}
}

bool CAudioVisualizer::GetBands(int Bands, float *pOut) const
{
	if(pOut == nullptr || Bands <= 0)
		return false;
	if(m_vBands.empty())
	{
		for(int i = 0; i < Bands; ++i)
			pOut[i] = 0.0f;
		return false;
	}

	if(Bands == (int)m_vBands.size())
	{
		for(int i = 0; i < Bands; ++i)
			pOut[i] = m_vBands[i];
		return true;
	}

	for(int i = 0; i < Bands; ++i)
	{
		const float t0 = i / (float)Bands;
		const float t1 = (i + 1) / (float)Bands;
		const int j0 = (int)std::floor(t0 * m_vBands.size());
		const int j1 = (int)std::ceil(t1 * m_vBands.size());
		const int a = std::clamp(j0, 0, (int)m_vBands.size() - 1);
		const int b = std::clamp(j1, a + 1, (int)m_vBands.size());
		float Acc = 0.0f;
		for(int j = a; j < b; ++j)
			Acc += m_vBands[j];
		pOut[i] = (b > a) ? Acc / (float)(b - a) : m_vBands[a];
	}
	return true;
}
