#include "source.h"

#include "analyzer.h"
#include "smoother.h"

#include <base/math.h>

#include <cstdint>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#if defined(CONF_FAMILY_WINDOWS) && defined(BC_MUSICPLAYER_HAS_WINRT) && BC_MUSICPLAYER_HAS_WINRT
#include <winrt/base.h>
#if !defined(NOBITMAP)
#define BC_VISUALIZER_DEFINED_NOBITMAP
#define NOBITMAP
#endif
#define IStorage BCVisualizerIStorage
#include <audioclient.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#undef IStorage
#if defined(BC_VISUALIZER_DEFINED_NOBITMAP)
#undef BC_VISUALIZER_DEFINED_NOBITMAP
#undef NOBITMAP
#endif
#endif

namespace BestClientVisualizer
{

namespace
{

#if defined(CONF_FAMILY_WINDOWS) && defined(BC_MUSICPLAYER_HAS_WINRT) && BC_MUSICPLAYER_HAS_WINRT
class CWasapiVisualizerSource final : public IVisualizerSource
{
	struct SWaveFormatInfo
	{
		WAVEFORMATEX *m_pFormat = nullptr;
		int m_Channels = 0;
		int m_BitsPerSample = 0;
		bool m_Float = false;
	};

	std::thread m_WorkerThread;
	std::mutex m_Mutex;
	bool m_Shutdown = false;
	SVisualizerFrame m_LatestFrame;
	SVisualizerConfig m_Config;
	int64_t m_ConfigRevision = 0;

	static bool ExtractWaveFormatInfo(const WAVEFORMATEX *pFormat, SWaveFormatInfo &Out)
	{
		if(!pFormat || pFormat->nChannels == 0 || pFormat->wBitsPerSample == 0)
			return false;
		Out.m_pFormat = const_cast<WAVEFORMATEX *>(pFormat);
		Out.m_Channels = maximum<int>(1, pFormat->nChannels);
		Out.m_BitsPerSample = pFormat->wBitsPerSample;
		Out.m_Float = pFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
		if(pFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
		{
			const auto *pExt = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(pFormat);
			Out.m_Float = pExt->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
			Out.m_BitsPerSample = pExt->Samples.wValidBitsPerSample > 0 ? pExt->Samples.wValidBitsPerSample : pFormat->wBitsPerSample;
		}
		return true;
	}

	static int StoreMonoSamples(const BYTE *pData, UINT32 FrameCount, const SWaveFormatInfo &Format, std::vector<float> &vOut)
	{
		if(!pData || FrameCount == 0 || Format.m_Channels <= 0)
			return 0;
		const int Channels = Format.m_Channels;
		vOut.resize(FrameCount);

		if(Format.m_Float && Format.m_BitsPerSample == 32)
		{
			const float *pSamples = reinterpret_cast<const float *>(pData);
			for(UINT32 Frame = 0; Frame < FrameCount; ++Frame)
			{
				float Sum = 0.0f;
				for(int Ch = 0; Ch < Channels; ++Ch)
					Sum += pSamples[Frame * Channels + Ch];
				vOut[Frame] = std::clamp(Sum / Channels, -1.0f, 1.0f);
			}
			return (int)FrameCount;
		}
		if(!Format.m_Float && Format.m_BitsPerSample == 16)
		{
			const int16_t *pSamples = reinterpret_cast<const int16_t *>(pData);
			for(UINT32 Frame = 0; Frame < FrameCount; ++Frame)
			{
				float Sum = 0.0f;
				for(int Ch = 0; Ch < Channels; ++Ch)
					Sum += pSamples[Frame * Channels + Ch] / 32768.0f;
				vOut[Frame] = std::clamp(Sum / Channels, -1.0f, 1.0f);
			}
			return (int)FrameCount;
		}
		if(!Format.m_Float && Format.m_BitsPerSample == 32)
		{
			const int32_t *pSamples = reinterpret_cast<const int32_t *>(pData);
			for(UINT32 Frame = 0; Frame < FrameCount; ++Frame)
			{
				float Sum = 0.0f;
				for(int Ch = 0; Ch < Channels; ++Ch)
					Sum += (float)(pSamples[Frame * Channels + Ch] / 2147483648.0);
				vOut[Frame] = std::clamp(Sum / Channels, -1.0f, 1.0f);
			}
			return (int)FrameCount;
		}
		return 0;
	}

	void StoreFrame(const SVisualizerFrame &Frame)
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		m_LatestFrame = Frame;
	}

	void WorkerMain()
	{
		const HRESULT CoInitResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		if(FAILED(CoInitResult))
			return;

		winrt::com_ptr<IMMDeviceEnumerator> pEnumerator;
		winrt::com_ptr<IMMDevice> pDevice;
		winrt::com_ptr<IAudioClient> pAudioClient;
		winrt::com_ptr<IAudioCaptureClient> pCaptureClient;
		WAVEFORMATEX *pFormat = nullptr;
		SWaveFormatInfo FormatInfo;
		int SampleRate = 48000;
		std::vector<float> vMonoBuffer;
		CVisualizerAnalyzer Analyzer;
		CVisualizerSmoother Smoother;
		int64_t AppliedConfigRevision = -1;
		int ValidatedReads = 0;
		int ActiveSignalReads = 0;
		int SilentReads = 0;
		bool LatchedSignal = false;
		bool ValidatedLive = false;

		auto Cleanup = [&]() {
			if(pAudioClient)
				pAudioClient->Stop();
			pCaptureClient = nullptr;
			pAudioClient = nullptr;
			pDevice = nullptr;
			pEnumerator = nullptr;
			if(pFormat)
			{
				CoTaskMemFree(pFormat);
				pFormat = nullptr;
			}
			FormatInfo = SWaveFormatInfo();
			ValidatedReads = 0;
			ActiveSignalReads = 0;
			SilentReads = 0;
			LatchedSignal = false;
			ValidatedLive = false;
		};

		auto ApplyConfig = [&](const SVisualizerConfig &Config) {
			SVisualizerConfig RuntimeConfig = Config;
			RuntimeConfig.m_SampleRate = SampleRate;
			Analyzer.Configure(RuntimeConfig);
			Smoother.Configure(RuntimeConfig);
		};

		auto EnsureCapture = [&]() -> bool {
			if(pCaptureClient && pAudioClient)
				return true;
			Cleanup();
			if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(pEnumerator.put()))))
				return false;
			if(FAILED(pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, pDevice.put())))
				return false;
			if(FAILED(pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(pAudioClient.put()))))
				return false;
			if(FAILED(pAudioClient->GetMixFormat(&pFormat)))
				return false;
			if(!ExtractWaveFormatInfo(pFormat, FormatInfo))
				return false;
			SampleRate = maximum<int>(1, pFormat->nSamplesPerSec);
			if(FAILED(pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 0, 0, pFormat, nullptr)))
				return false;
			if(FAILED(pAudioClient->GetService(IID_PPV_ARGS(pCaptureClient.put()))))
				return false;
			if(FAILED(pAudioClient->Start()))
				return false;
			SVisualizerConfig ConfigSnapshot;
			{
				std::lock_guard<std::mutex> Lock(m_Mutex);
				ConfigSnapshot = m_Config;
			}
			ApplyConfig(ConfigSnapshot);
			return true;
		};

		while(true)
		{
			{
				std::lock_guard<std::mutex> Lock(m_Mutex);
				if(m_Shutdown)
					break;
				if(m_ConfigRevision != AppliedConfigRevision)
				{
					AppliedConfigRevision = m_ConfigRevision;
					ApplyConfig(m_Config);
				}
			}

			if(!EnsureCapture())
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(250));
				continue;
			}

			UINT32 PacketLength = 0;
			if(FAILED(pCaptureClient->GetNextPacketSize(&PacketLength)))
			{
				Cleanup();
				std::this_thread::sleep_for(std::chrono::milliseconds(80));
				continue;
			}

			bool HadPacket = false;
			while(PacketLength > 0)
			{
				BYTE *pData = nullptr;
				UINT32 NumFrames = 0;
				DWORD Flags = 0;
				if(FAILED(pCaptureClient->GetBuffer(&pData, &NumFrames, &Flags, nullptr, nullptr)))
				{
					Cleanup();
					break;
				}

				HadPacket = true;
				if((Flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0)
				{
					vMonoBuffer.assign(NumFrames, 0.0f);
					Analyzer.PushMonoSamples(vMonoBuffer.data(), (int)NumFrames);
				}
				else
				{
					const int Stored = StoreMonoSamples(pData, NumFrames, FormatInfo, vMonoBuffer);
					if(Stored > 0)
						Analyzer.PushMonoSamples(vMonoBuffer.data(), Stored);
				}
				pCaptureClient->ReleaseBuffer(NumFrames);

				if(FAILED(pCaptureClient->GetNextPacketSize(&PacketLength)))
				{
					Cleanup();
					break;
				}
			}

			if(!HadPacket)
			{
				float aSilence[256] = {};
				Analyzer.PushMonoSamples(aSilence, 256);
			}

			SVisualizerFrame RawFrame;
			Analyzer.Analyze(RawFrame);
			if(RawFrame.m_HasRealtimeSignal)
			{
				ValidatedReads++;
				ActiveSignalReads = minimum(ActiveSignalReads + 1, 32);
				SilentReads = 0;
			}
			else
			{
				ActiveSignalReads = 0;
				SilentReads++;
			}
			if(!ValidatedLive && ValidatedReads >= 3)
				ValidatedLive = true;
			if(!LatchedSignal && ActiveSignalReads >= 2)
				LatchedSignal = true;
			else if(LatchedSignal && SilentReads >= 10)
				LatchedSignal = false;
			RawFrame.m_HasRealtimeSignal = LatchedSignal;
			RawFrame.m_BackendStatus = ValidatedLive ? (LatchedSignal ? EVisualizerBackendStatus::LIVE : EVisualizerBackendStatus::SILENT) : EVisualizerBackendStatus::SILENT;
			RawFrame.m_IsPassiveFallback = false;
			SVisualizerFrame SmoothedFrame;
			Smoother.Process(RawFrame, SmoothedFrame);
			SmoothedFrame.m_BackendStatus = RawFrame.m_BackendStatus;
			SmoothedFrame.m_IsPassiveFallback = false;
			StoreFrame(SmoothedFrame);
			std::this_thread::sleep_for(std::chrono::milliseconds(HadPacket ? 12 : 24));
		}

		Cleanup();
		CoUninitialize();
	}

public:
	CWasapiVisualizerSource()
	{
		m_Config = SVisualizerConfig();
		m_WorkerThread = std::thread([this]() { WorkerMain(); });
	}

	~CWasapiVisualizerSource() override
	{
		{
			std::lock_guard<std::mutex> Lock(m_Mutex);
			m_Shutdown = true;
		}
		if(m_WorkerThread.joinable())
			m_WorkerThread.join();
	}

	void SetPlaybackHint(const SVisualizerPlaybackHint &Hint) override
	{
		(void)Hint;
	}

	void SetConfig(const SVisualizerConfig &Config) override
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		m_Config = Config;
		++m_ConfigRevision;
	}

	bool PollFrame(SVisualizerFrame &OutFrame) override
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		OutFrame = m_LatestFrame;
		return OutFrame.m_HasRealtimeSignal || OutFrame.m_Rms > 0.0f || OutFrame.m_Peak > 0.0f;
	}
};
#endif

} // namespace

std::unique_ptr<IVisualizerSource> CreateWasapiVisualizerSource()
{
#if defined(CONF_FAMILY_WINDOWS) && defined(BC_MUSICPLAYER_HAS_WINRT) && BC_MUSICPLAYER_HAS_WINRT
	return std::make_unique<CWasapiVisualizerSource>();
#else
	return CreatePassiveVisualizerSource();
#endif
}

} // namespace BestClientVisualizer
