/* Copyright © 2026 BestProject Team */
#include "source.h"

#include "analyzer.h"
#include "smoother.h"
#include "source_priority.h"

#include <base/detect.h>
#include <base/math.h>

#include <algorithm>

template<typename T>
constexpr T minimum(T a, T b)
{
	return (std::min)(a, b);
}

template<typename T>
constexpr T maximum(T a, T b)
{
	return (std::max)(a, b);
}

template<typename T>
constexpr T minimum(T a, T b, T c)
{
	return (std::min)(a, (std::min)(b, c));
}

template<typename T>
constexpr T maximum(T a, T b, T c)
{
	return (std::max)(a, (std::max)(b, c));
}

#include <base/dbg.h>
#include <base/str.h>
#include <base/time.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(CONF_FAMILY_WINDOWS) && __has_include(<winrt/base.h>)
#define BC_VISUALIZER_HAS_WASAPI 1
#include <winrt/base.h>
#if !defined(NOBITMAP)
#define BC_VISUALIZER_DEFINED_NOBITMAP
#define NOBITMAP
#endif
#define IStorage BCVisualizerIStorage
#include <windows.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <propsys.h>
#include <tlhelp32.h>
#undef IStorage
#if defined(BC_VISUALIZER_DEFINED_NOBITMAP)
#undef BC_VISUALIZER_DEFINED_NOBITMAP
#undef NOBITMAP
#endif

#if !defined(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK)
#define VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK L"VAD\\Process_Loopback"
enum PROCESS_LOOPBACK_MODE
{
	PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE = 0,
	PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE = 1,
};
struct AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS
{
	DWORD TargetProcessId;
	PROCESS_LOOPBACK_MODE ProcessLoopbackMode;
};
enum AUDIOCLIENT_ACTIVATION_TYPE
{
	AUDIOCLIENT_ACTIVATION_TYPE_DEFAULT = 0,
	AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK = 1,
};
struct AUDIOCLIENT_ACTIVATION_PARAMS
{
	AUDIOCLIENT_ACTIVATION_TYPE ActivationType;
	union
	{
		AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS ProcessLoopbackParams;
	};
};
#endif
#else
#define BC_VISUALIZER_HAS_WASAPI 0
#endif

namespace BestClientVisualizer
{

namespace
{
	constexpr int WASAPI_ANALYZE_FRAMES = 1024;
	constexpr float AUDIBLE_SESSION_PEAK_THRESHOLD = 0.00025f;
	constexpr auto AUDIBLE_SESSION_GRACE = std::chrono::milliseconds(1200);
	constexpr auto SESSION_SCAN_INTERVAL = std::chrono::milliseconds(450);
	constexpr auto CAPTURE_INIT_COOLDOWN = std::chrono::milliseconds(1250);

#if BC_VISUALIZER_HAS_WASAPI
class CProcessLoopbackActivationHandler final : public IActivateAudioInterfaceCompletionHandler, public IAgileObject
{
public:
	CProcessLoopbackActivationHandler()
	{
		m_Event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	}

	~CProcessLoopbackActivationHandler()
	{
		if(m_Event != nullptr)
			CloseHandle(m_Event);
	}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID Riid, void **ppvObject) override
	{
		if(ppvObject == nullptr)
			return E_POINTER;
		*ppvObject = nullptr;
		if(IsEqualIID(Riid, __uuidof(IUnknown)) || IsEqualIID(Riid, __uuidof(IActivateAudioInterfaceCompletionHandler)))
		{
			*ppvObject = static_cast<IActivateAudioInterfaceCompletionHandler *>(this);
			AddRef();
			return S_OK;
		}
		if(IsEqualIID(Riid, __uuidof(IAgileObject)))
		{
			*ppvObject = static_cast<IAgileObject *>(this);
			AddRef();
			return S_OK;
		}
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return ++m_RefCount;
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		return --m_RefCount;
	}

	HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation *pOperation) override
	{
		if(pOperation == nullptr)
		{
			m_ActivateHr = E_POINTER;
		}
		else
		{
			IUnknown *pActivatedInterface = nullptr;
			m_ActivateHr = pOperation->GetActivateResult(&m_ActivateResultHr, &pActivatedInterface);
			if(SUCCEEDED(m_ActivateHr))
			{
				m_ActivateHr = m_ActivateResultHr;
				m_ActivatedInterface.attach(pActivatedInterface);
			}
			else if(pActivatedInterface != nullptr)
			{
				pActivatedInterface->Release();
			}
		}

		if(m_Event != nullptr)
			SetEvent(m_Event);
		return S_OK;
	}

	HRESULT WaitForCompletion()
	{
		if(m_Event == nullptr)
			return HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);
		const DWORD WaitResult = WaitForSingleObject(m_Event, 1000);
		if(WaitResult != WAIT_OBJECT_0)
			return WaitResult == WAIT_TIMEOUT ? HRESULT_FROM_WIN32(ERROR_TIMEOUT) : HRESULT_FROM_WIN32(GetLastError());
		return m_ActivateHr;
	}

	IUnknown *ActivatedInterface() const
	{
		return m_ActivatedInterface.get();
	}

private:
	std::atomic<ULONG> m_RefCount{1};
	HANDLE m_Event = nullptr;
	HRESULT m_ActivateHr = E_PENDING;
	HRESULT m_ActivateResultHr = E_PENDING;
	winrt::com_ptr<IUnknown> m_ActivatedInterface;
};

class CWasapiVisualizerSource final : public IVisualizerSource
{
	enum class ECaptureMode
	{
		NONE,
		PROCESS,
		DEVICE,
	};

	struct SWaveFormatInfo
	{
		WAVEFORMATEX *m_pFormat = nullptr;
		int m_Channels = 0;
		int m_BitsPerSample = 0;
		int m_ContainerBitsPerSample = 0;
		int m_BlockAlign = 0;
		bool m_Float = false;
	};

	struct SProcessSnapshotInfo
	{
		DWORD m_ParentPid = 0;
		std::string m_NameLower;
	};

	std::thread m_WorkerThread;
	std::mutex m_Mutex;
	bool m_Shutdown = false;
	SVisualizerFrame m_LatestFrame;
	SVisualizerConfig m_Config;
	SVisualizerPlaybackHint m_Hint;
	int64_t m_ConfigRevision = 0;

	static std::string ToLowerAscii(std::string Str)
	{
		for(char &Ch : Str)
			Ch = (char)std::tolower((unsigned char)Ch);
		return Str;
	}

	static std::string ProcessBaseNameLower(DWORD Pid)
	{
		if(Pid == 0)
			return {};

		HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, Pid);
		if(hProcess == nullptr)
			return {};

		char aPath[1024];
		DWORD PathSize = sizeof(aPath);
		std::string Result;
		if(QueryFullProcessImageNameA(hProcess, 0, aPath, &PathSize))
		{
			const char *pBase1 = std::strrchr(aPath, '\\');
			const char *pBase2 = std::strrchr(aPath, '/');
			const char *pBase = pBase1 != nullptr ? pBase1 : pBase2;
			Result = pBase != nullptr ? pBase + 1 : aPath;
			Result = ToLowerAscii(Result);
		}
		CloseHandle(hProcess);
		return Result;
	}

	static SProcessSnapshotInfo ProcessSnapshotInfo(DWORD Pid)
	{
		SProcessSnapshotInfo Result;
		if(Pid == 0)
			return Result;

		HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if(hSnapshot == INVALID_HANDLE_VALUE)
			return Result;

		PROCESSENTRY32 Entry{};
		Entry.dwSize = sizeof(Entry);
		if(Process32First(hSnapshot, &Entry))
		{
			do
			{
				if(Entry.th32ProcessID == Pid)
				{
					Result.m_ParentPid = Entry.th32ParentProcessID;
					Result.m_NameLower = ProcessBaseNameLower(Pid);
					break;
				}
			} while(Process32Next(hSnapshot, &Entry));
		}

		CloseHandle(hSnapshot);
		return Result;
	}

	static bool ShouldUseParentForProcessLoopback(const std::string &ChildNameLower, const std::string &ParentNameLower)
	{
		if(ChildNameLower.empty() || ParentNameLower.empty())
			return false;
		if(ChildNameLower == ParentNameLower)
			return true;
		if(ChildNameLower == "chrome_pwa_launcher.exe" && ParentNameLower == "chrome.exe")
			return true;
		if(ChildNameLower == "msedge_proxy.exe" && ParentNameLower == "msedge.exe")
			return true;
		if(ChildNameLower == "spotifywebhelper.exe" && ParentNameLower == "spotify.exe")
			return true;
		return false;
	}

	static DWORD ResolveProcessLoopbackRootPid(DWORD Pid)
	{
		DWORD RootPid = Pid;
		SProcessSnapshotInfo Current = ProcessSnapshotInfo(Pid);
		for(int Depth = 0; Depth < 16 && Current.m_ParentPid != 0; ++Depth)
		{
			SProcessSnapshotInfo Parent = ProcessSnapshotInfo(Current.m_ParentPid);
			if(!ShouldUseParentForProcessLoopback(Current.m_NameLower, Parent.m_NameLower))
				break;
			RootPid = Current.m_ParentPid;
			Current = std::move(Parent);
		}
		return RootPid;
	}

	static bool IsBlockedAudioProcess(const std::string &ProcessNameLower)
	{
		if(ProcessNameLower.empty())
			return true;

		static constexpr const char *s_apBlockedProcesses[] = {
			"discord.exe",
			"discordcanary.exe",
			"discordptb.exe",
			"ddnet.exe",
			"bestclient.exe",
			"tclient.exe",
			"steam.exe",
			"steamservice.exe",
			"steamwebhelper.exe",
			"teeworlds.exe",
		};
		for(const char *pBlocked : s_apBlockedProcesses)
		{
			if(ProcessNameLower == pBlocked)
				return true;
		}
		return false;
	}

	static bool IsAllowedMusicAudioProcess(const std::string &ProcessNameLower)
	{
		if(IsBlockedAudioProcess(ProcessNameLower))
			return false;

		static constexpr const char *s_apAllowedProcesses[] = {
			"aimp.exe",
			"audiomack.exe",
			"apple music preview.exe",
			"applemusic.exe",
			"applemusicpreview.exe",
			"apple music.exe",
			"arc.exe",
			"brave.exe",
			"browser.exe",
			"chrome.exe",
			"chrome_pwa_launcher.exe",
			"chromium.exe",
			"cider.exe",
			"clementine.exe",
			"deezer.exe",
			"dopamine.exe",
			"firefox.exe",
			"foobar2000.exe",
			"floorp.exe",
			"itunes.exe",
			"librewolf.exe",
			"materialgram.exe",
			"mediamonkey.exe",
			"mpc-be.exe",
			"mpc-hc.exe",
			"mpv.exe",
			"mts music.exe",
			"mtsmusic.exe",
			"msedge.exe",
			"msedge_proxy.exe",
			"music.ui.exe",
			"musicbee.exe",
			"nuclear.exe",
			"opera.exe",
			"opera_gx.exe",
			"potplayermini.exe",
			"potplayermini64.exe",
			"qobuz.exe",
			"qobuzdesktop.exe",
			"quodlibet.exe",
			"sberzvuk.exe",
			"soundcloud.exe",
			"spotify.exe",
			"spotifywebhelper.exe",
			"strawberry.exe",
			"tidal.exe",
			"tdesktop.exe",
			"telegram.exe",
			"vivaldi.exe",
			"vlc.exe",
			"vk music.exe",
			"vkmusic.exe",
			"vkmp.exe",
			"vkmpconsole.exe",
			"waterfox.exe",
			"winamp.exe",
			"wmplayer.exe",
			"yamusic.exe",
			"yandex music.exe",
			"yandexmusicapp.exe",
			"yandexmusic.exe",
			"youtube music desktop app.exe",
			"youtube music.exe",
			"youtube-music.exe",
			"ytmusic.exe",
			"youtubemusic.exe",
			"ytmdesktop.exe",
			"zvooq.exe",
			"zvuk.exe",
			"zen.exe",
		};
		for(const char *pAllowed : s_apAllowedProcesses)
		{
			if(ProcessNameLower == pAllowed)
				return true;
		}
		return false;
	}

	static bool ExtractWaveFormatInfo(const WAVEFORMATEX *pFormat, SWaveFormatInfo &Out)
	{
		if(!pFormat || pFormat->nChannels == 0 || pFormat->wBitsPerSample == 0)
			return false;
		Out.m_pFormat = const_cast<WAVEFORMATEX *>(pFormat);
		Out.m_Channels = maximum<int>(1, pFormat->nChannels);
		Out.m_BlockAlign = maximum<int>(pFormat->nBlockAlign, pFormat->nChannels);
		Out.m_ContainerBitsPerSample = pFormat->wBitsPerSample;
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
		const int MixedChannels = Channels > 2 ? 2 : Channels;
		vOut.resize(FrameCount);

		if(Format.m_Float && Format.m_BitsPerSample == 32)
		{
			const float *pSamples = reinterpret_cast<const float *>(pData);
			for(UINT32 Frame = 0; Frame < FrameCount; ++Frame)
			{
				float Sum = 0.0f;
				for(int Ch = 0; Ch < MixedChannels; ++Ch)
					Sum += pSamples[Frame * Channels + Ch];
				vOut[Frame] = std::clamp(Sum / MixedChannels, -1.0f, 1.0f);
			}
			return (int)FrameCount;
		}
		if(!Format.m_Float && Format.m_BitsPerSample == 16)
		{
			const int16_t *pSamples = reinterpret_cast<const int16_t *>(pData);
			for(UINT32 Frame = 0; Frame < FrameCount; ++Frame)
			{
				float Sum = 0.0f;
				for(int Ch = 0; Ch < MixedChannels; ++Ch)
					Sum += pSamples[Frame * Channels + Ch] / 32768.0f;
				vOut[Frame] = std::clamp(Sum / MixedChannels, -1.0f, 1.0f);
			}
			return (int)FrameCount;
		}
		if(!Format.m_Float && Format.m_BitsPerSample == 24)
		{
			const int BytesPerSample = maximum(1, Format.m_BlockAlign / Channels);
			const uint8_t *pSamples = reinterpret_cast<const uint8_t *>(pData);
			for(UINT32 Frame = 0; Frame < FrameCount; ++Frame)
			{
				float Sum = 0.0f;
				const uint8_t *pFrame = pSamples + Frame * Format.m_BlockAlign;
				for(int Ch = 0; Ch < MixedChannels; ++Ch)
				{
					const uint8_t *pSample = pFrame + Ch * BytesPerSample;
					int32_t Sample = 0;
					if(BytesPerSample >= 4 || Format.m_ContainerBitsPerSample >= 32)
					{
						Sample = (int32_t)((uint32_t)pSample[0] | ((uint32_t)pSample[1] << 8) | ((uint32_t)pSample[2] << 16) | ((uint32_t)pSample[3] << 24));
						Sample >>= 8;
					}
					else if(BytesPerSample >= 3)
					{
						Sample = (int32_t)((uint32_t)pSample[0] | ((uint32_t)pSample[1] << 8) | ((uint32_t)pSample[2] << 16));
						if((Sample & 0x00800000) != 0)
							Sample |= ~0x00FFFFFF;
					}
					Sum += Sample / 8388608.0f;
				}
				vOut[Frame] = std::clamp(Sum / MixedChannels, -1.0f, 1.0f);
			}
			return (int)FrameCount;
		}
		if(!Format.m_Float && Format.m_BitsPerSample == 32)
		{
			const int32_t *pSamples = reinterpret_cast<const int32_t *>(pData);
			for(UINT32 Frame = 0; Frame < FrameCount; ++Frame)
			{
				float Sum = 0.0f;
				for(int Ch = 0; Ch < MixedChannels; ++Ch)
					Sum += (float)(pSamples[Frame * Channels + Ch] / 2147483648.0);
				vOut[Frame] = std::clamp(Sum / MixedChannels, -1.0f, 1.0f);
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

	void StoreBackendState(EVisualizerBackendStatus Status)
	{
		SVisualizerFrame Frame;
		Frame.m_BackendStatus = Status;
		Frame.m_IsPassiveFallback = Status == EVisualizerBackendStatus::FALLBACK || Status == EVisualizerBackendStatus::UNAVAILABLE;
		StoreFrame(Frame);
	}

	void WorkerMain()
	{
		const HRESULT CoInitResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		if(FAILED(CoInitResult) && CoInitResult != RPC_E_CHANGED_MODE)
		{
			StoreBackendState(EVisualizerBackendStatus::UNAVAILABLE);
			return;
		}

		winrt::com_ptr<IMMDeviceEnumerator> pEnumerator;
		winrt::com_ptr<IMMDevice> pDevice;
		winrt::com_ptr<IAudioClient> pAudioClient;
		winrt::com_ptr<IAudioCaptureClient> pCaptureClient;
		WAVEFORMATEX *pFormat = nullptr;
		SWaveFormatInfo FormatInfo;
		int SampleRate = 48000;
		std::vector<float> vMonoBuffer;
		std::vector<float> vIdleSilenceBuffer;
		CVisualizerAnalyzer Analyzer;
		CVisualizerSmoother Smoother;
		int64_t AppliedConfigRevision = -1;
		int ValidatedReads = 0;
		int ActiveSignalReads = 0;
		int SilentReads = 0;
		int PendingAnalyzeFrames = 0;
		int ConsecutiveFailures = 0;
		bool LatchedSignal = false;
		bool ValidatedLive = false;
		ECaptureMode CaptureMode = ECaptureMode::NONE;
		DWORD CaptureTargetPid = 0;
		DWORD TargetPidCache = 0;
		bool HasAllowedSessionCache = false;
		auto LastPacketAt = std::chrono::steady_clock::now();
		auto LastSyntheticSilenceAt = LastPacketAt;
		auto LastAudioSessionScan = std::chrono::steady_clock::time_point{};
		auto LastAudibleSessionTime = std::chrono::steady_clock::time_point{};
		auto NextInitTry = std::chrono::steady_clock::time_point{};

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
			CaptureMode = ECaptureMode::NONE;
			CaptureTargetPid = 0;
			ValidatedReads = 0;
			ActiveSignalReads = 0;
			SilentReads = 0;
			PendingAnalyzeFrames = 0;
			LatchedSignal = false;
			ValidatedLive = false;
			LastPacketAt = std::chrono::steady_clock::now();
			LastSyntheticSilenceAt = LastPacketAt;
		};

		auto ApplyConfig = [&](const SVisualizerConfig &Config) {
			SVisualizerConfig RuntimeConfig = Config;
			RuntimeConfig.m_SampleRate = SampleRate;
			Analyzer.Configure(RuntimeConfig);
			Smoother.Configure(RuntimeConfig);
		};

		auto StoreAnalyzerFrame = [&]() {
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
		};

		auto QueryTargetAudioProcessId = [&]() -> DWORD {
			std::string HintServiceId;
			{
				std::lock_guard<std::mutex> Lock(m_Mutex);
				HintServiceId = m_Hint.m_ServiceId;
			}

			winrt::com_ptr<IMMDeviceEnumerator> Enumerator;
			if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(Enumerator.put()))))
				return 0;

			winrt::com_ptr<IMMDevice> Device;
			if(FAILED(Enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, Device.put())) &&
				FAILED(Enumerator->GetDefaultAudioEndpoint(eRender, eConsole, Device.put())))
			{
				return 0;
			}

			winrt::com_ptr<IAudioSessionManager2> SessionManager;
			if(FAILED(Device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, SessionManager.put_void())))
				return 0;

			winrt::com_ptr<IAudioSessionEnumerator> SessionEnum;
			if(FAILED(SessionManager->GetSessionEnumerator(SessionEnum.put())) || !SessionEnum)
				return 0;

			int SessionCount = 0;
			if(FAILED(SessionEnum->GetCount(&SessionCount)) || SessionCount <= 0)
				return 0;

			const DWORD SelfPid = GetCurrentProcessId();
			DWORD BestPid = 0;
			DWORD BestHintPid = 0;
			DWORD MeterlessPid = 0;
			float BestPeak = 0.0f;
			float BestHintPeak = 0.0f;
			for(int i = 0; i < SessionCount; ++i)
			{
				winrt::com_ptr<IAudioSessionControl> SessionCtrl;
				if(FAILED(SessionEnum->GetSession(i, SessionCtrl.put())) || !SessionCtrl)
					continue;

				AudioSessionState SessionState = AudioSessionStateInactive;
				if(FAILED(SessionCtrl->GetState(&SessionState)) || SessionState != AudioSessionStateActive)
					continue;

				winrt::com_ptr<IAudioSessionControl2> SessionCtrl2;
				if(FAILED(SessionCtrl->QueryInterface(__uuidof(IAudioSessionControl2), SessionCtrl2.put_void())) || !SessionCtrl2)
					continue;

				DWORD Pid = 0;
				if(FAILED(SessionCtrl2->GetProcessId(&Pid)) || Pid == 0 || Pid == SelfPid)
					continue;

				const std::string ProcessNameLower = ProcessBaseNameLower(Pid);
				if(!IsAllowedMusicAudioProcess(ProcessNameLower))
					continue;
				const DWORD TargetPid = ResolveProcessLoopbackRootPid(Pid);
				if(TargetPid == 0 || TargetPid == SelfPid || IsBlockedAudioProcess(ProcessBaseNameLower(TargetPid)))
					continue;

				winrt::com_ptr<ISimpleAudioVolume> SessionVolume;
				if(FAILED(SessionCtrl->QueryInterface(__uuidof(ISimpleAudioVolume), SessionVolume.put_void())) || !SessionVolume)
					continue;

				BOOL Muted = FALSE;
				float Volume = 0.0f;
				if(FAILED(SessionVolume->GetMute(&Muted)) || FAILED(SessionVolume->GetMasterVolume(&Volume)))
					continue;
				if(Muted || Volume <= 0.001f)
					continue;

				const bool HintMatch = HintMatchesPlaybackSource(HintServiceId, ProcessNameLower, ProcessBaseNameLower(TargetPid));

				winrt::com_ptr<IAudioMeterInformation> Meter;
				if(FAILED(SessionCtrl->QueryInterface(__uuidof(IAudioMeterInformation), Meter.put_void())) || !Meter)
				{
					if(MeterlessPid == 0 || HintMatch)
						MeterlessPid = TargetPid;
					continue;
				}

				float Peak = 0.0f;
				if(SUCCEEDED(Meter->GetPeakValue(&Peak)) && Peak > AUDIBLE_SESSION_PEAK_THRESHOLD)
				{
					if(Peak > BestPeak)
					{
						BestPid = TargetPid;
						BestPeak = Peak;
					}
					if(HintMatch && Peak > BestHintPeak)
					{
						BestHintPid = TargetPid;
						BestHintPeak = Peak;
					}
				}
			}

			if(BestHintPid != 0)
				BestPid = BestHintPid;

			const auto Now = std::chrono::steady_clock::now();
			if(BestPid != 0)
			{
				LastAudibleSessionTime = Now;
				return BestPid;
			}

			if(CaptureTargetPid != 0 && Now - LastAudibleSessionTime < AUDIBLE_SESSION_GRACE)
				return CaptureTargetPid;

			if(CaptureTargetPid == 0 && MeterlessPid != 0)
			{
				LastAudibleSessionTime = Now;
				return MeterlessPid;
			}

			return 0;
		};

		auto ResolveTargetPid = [&](const std::chrono::steady_clock::time_point &Now) -> DWORD {
			if(Now - LastAudioSessionScan < SESSION_SCAN_INTERVAL)
				return HasAllowedSessionCache ? TargetPidCache : 0;

			LastAudioSessionScan = Now;
			TargetPidCache = QueryTargetAudioProcessId();
			HasAllowedSessionCache = TargetPidCache != 0;
			return TargetPidCache;
		};

		auto SetProcessLoopbackPcm16Format = [&](int DesiredSampleRate) -> bool {
			if(pFormat)
			{
				CoTaskMemFree(pFormat);
				pFormat = nullptr;
			}

			auto *pNewFormat = static_cast<WAVEFORMATEX *>(CoTaskMemAlloc(sizeof(WAVEFORMATEX)));
			if(!pNewFormat)
				return false;

			std::memset(pNewFormat, 0, sizeof(*pNewFormat));
			pNewFormat->wFormatTag = WAVE_FORMAT_PCM;
			pNewFormat->nChannels = 2;
			pNewFormat->nSamplesPerSec = (DWORD)DesiredSampleRate;
			pNewFormat->wBitsPerSample = 16;
			pNewFormat->nBlockAlign = (WORD)(pNewFormat->nChannels * pNewFormat->wBitsPerSample / 8);
			pNewFormat->nAvgBytesPerSec = pNewFormat->nSamplesPerSec * pNewFormat->nBlockAlign;
			pNewFormat->cbSize = 0;
			pFormat = pNewFormat;
			return ExtractWaveFormatInfo(pFormat, FormatInfo);
		};

		auto ActivateProcessLoopbackAudioClient = [&](DWORD TargetProcessId) -> bool {
			AUDIOCLIENT_ACTIVATION_PARAMS ActivationParams{};
			ActivationParams.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
			ActivationParams.ProcessLoopbackParams.TargetProcessId = TargetProcessId;
			ActivationParams.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

			PROPVARIANT ActivationParamsVariant;
			PropVariantInit(&ActivationParamsVariant);
			ActivationParamsVariant.vt = VT_BLOB;
			ActivationParamsVariant.blob.cbSize = sizeof(ActivationParams);
			ActivationParamsVariant.blob.pBlobData = reinterpret_cast<BYTE *>(&ActivationParams);

			CProcessLoopbackActivationHandler CompletionHandler;
			winrt::com_ptr<IActivateAudioInterfaceAsyncOperation> ActivationOperation;
			HRESULT Hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &ActivationParamsVariant, &CompletionHandler, ActivationOperation.put());
			if(FAILED(Hr))
				return false;

			Hr = CompletionHandler.WaitForCompletion();
			if(FAILED(Hr))
				return false;

			IUnknown *pActivatedInterface = CompletionHandler.ActivatedInterface();
			if(pActivatedInterface == nullptr)
				return false;

			return SUCCEEDED(pActivatedInterface->QueryInterface(__uuidof(IAudioClient), pAudioClient.put_void()));
		};

		auto TryStartProcessCapture = [&](DWORD TargetProcessId) -> bool {
			if(TargetProcessId == 0)
				return false;
			if(!ActivateProcessLoopbackAudioClient(TargetProcessId))
				return false;

			const int aSampleRates[] = {44100, 48000};
			bool Initialized = false;
			for(const int Rate : aSampleRates)
			{
				if(!SetProcessLoopbackPcm16Format(Rate))
					return false;
				if(SUCCEEDED(pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 0, 0, pFormat, nullptr)))
				{
					Initialized = true;
					break;
				}
			}
			if(!Initialized)
				return false;

			if(FAILED(pAudioClient->GetService(IID_PPV_ARGS(pCaptureClient.put()))))
				return false;
			if(FAILED(pAudioClient->Start()))
				return false;

			SampleRate = maximum<int>(1, (int)pFormat->nSamplesPerSec);
			CaptureMode = ECaptureMode::PROCESS;
			CaptureTargetPid = TargetProcessId;
			return true;
		};

		auto TryStartDeviceCapture = [&]() -> bool {
			if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(pEnumerator.put()))))
				return false;
			if(FAILED(pEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, pDevice.put())) &&
				FAILED(pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, pDevice.put())))
			{
				return false;
			}
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

			CaptureMode = ECaptureMode::DEVICE;
			CaptureTargetPid = 0;
			return true;
		};

		auto EnsureCapture = [&](const std::chrono::steady_clock::time_point &Now) -> bool {
			const DWORD TargetPid = ResolveTargetPid(Now);
			const bool WantProcess = TargetPid != 0;

			if(pCaptureClient && pAudioClient)
			{
				if(CaptureMode == ECaptureMode::PROCESS && WantProcess && CaptureTargetPid == TargetPid)
					return true;
				if(CaptureMode == ECaptureMode::DEVICE && !WantProcess)
					return true;
				if(CaptureMode == ECaptureMode::DEVICE && WantProcess && Now < NextInitTry)
					return true;
			}

			if(Now < NextInitTry && !(pCaptureClient && pAudioClient))
				return false;
			NextInitTry = Now + CAPTURE_INIT_COOLDOWN;

			const ECaptureMode PreviousMode = CaptureMode;
			Cleanup();

			bool Started = false;
			if(WantProcess)
				Started = TryStartProcessCapture(TargetPid);
			if(!Started)
				Started = TryStartDeviceCapture();

			if(!Started)
			{
				Cleanup();
				++ConsecutiveFailures;
				StoreBackendState(ConsecutiveFailures >= 6 ? EVisualizerBackendStatus::UNAVAILABLE : EVisualizerBackendStatus::CONNECTING);
				return false;
			}

			if(PreviousMode != CaptureMode || WantProcess)
			{
				ValidatedReads = 0;
				ActiveSignalReads = 0;
				SilentReads = 0;
				PendingAnalyzeFrames = 0;
				LatchedSignal = false;
				ValidatedLive = false;
			}

			SVisualizerConfig ConfigSnapshot;
			{
				std::lock_guard<std::mutex> Lock(m_Mutex);
				ConfigSnapshot = m_Config;
			}
			ConsecutiveFailures = 0;
			ApplyConfig(ConfigSnapshot);
			LastPacketAt = Now;
			LastSyntheticSilenceAt = Now;
			StoreBackendState(EVisualizerBackendStatus::CONNECTING);
			return true;
		};

		StoreBackendState(EVisualizerBackendStatus::CONNECTING);

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

			const auto Now = std::chrono::steady_clock::now();
			if(!EnsureCapture(Now))
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(250));
				continue;
			}

			UINT32 PacketLength = 0;
			if(FAILED(pCaptureClient->GetNextPacketSize(&PacketLength)))
			{
				Cleanup();
				StoreBackendState(EVisualizerBackendStatus::CONNECTING);
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
					StoreBackendState(EVisualizerBackendStatus::CONNECTING);
					break;
				}

				HadPacket = true;
				LastPacketAt = std::chrono::steady_clock::now();
				if((Flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0)
				{
					vMonoBuffer.assign(NumFrames, 0.0f);
					Analyzer.PushMonoSamples(vMonoBuffer.data(), (int)NumFrames);
					PendingAnalyzeFrames += (int)NumFrames;
				}
				else
				{
					const int Stored = StoreMonoSamples(pData, NumFrames, FormatInfo, vMonoBuffer);
					if(Stored > 0)
					{
						Analyzer.PushMonoSamples(vMonoBuffer.data(), Stored);
						PendingAnalyzeFrames += Stored;
					}
				}
				pCaptureClient->ReleaseBuffer(NumFrames);

				if(FAILED(pCaptureClient->GetNextPacketSize(&PacketLength)))
				{
					Cleanup();
					StoreBackendState(EVisualizerBackendStatus::CONNECTING);
					break;
				}
			}

			if(!HadPacket)
			{
				const auto IdleNow = std::chrono::steady_clock::now();
				const bool CaptureIdle = IdleNow - LastPacketAt >= std::chrono::milliseconds(20);
				const bool SilenceStepDue = IdleNow - LastSyntheticSilenceAt >= std::chrono::milliseconds(8);
				if(CaptureIdle && SilenceStepDue)
				{
					const int SilenceFrames = std::clamp(SampleRate / 100, 128, 1024);
					vIdleSilenceBuffer.assign(SilenceFrames, 0.0f);
					Analyzer.PushMonoSamples(vIdleSilenceBuffer.data(), SilenceFrames);
					PendingAnalyzeFrames += SilenceFrames;
					LastSyntheticSilenceAt = IdleNow;
					while(PendingAnalyzeFrames >= WASAPI_ANALYZE_FRAMES)
					{
						StoreAnalyzerFrame();
						PendingAnalyzeFrames -= WASAPI_ANALYZE_FRAMES;
					}
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				continue;
			}

			LastSyntheticSilenceAt = LastPacketAt;
			while(PendingAnalyzeFrames >= WASAPI_ANALYZE_FRAMES)
			{
				StoreAnalyzerFrame();
				PendingAnalyzeFrames -= WASAPI_ANALYZE_FRAMES;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		Cleanup();
		if(CoInitResult == S_OK)
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
		std::lock_guard<std::mutex> Lock(m_Mutex);
		m_Hint = Hint;
	}

	void SetConfig(const SVisualizerConfig &Config) override
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		if(m_Config.m_SampleRate == Config.m_SampleRate &&
			m_Config.m_BandCount == Config.m_BandCount &&
			m_Config.m_LowCutHz == Config.m_LowCutHz &&
			m_Config.m_HighCutHz == Config.m_HighCutHz &&
			m_Config.m_BassSplitHz == Config.m_BassSplitHz &&
			m_Config.m_NoiseReduction == Config.m_NoiseReduction &&
			m_Config.m_Sensitivity == Config.m_Sensitivity &&
			m_Config.m_ChannelMode == Config.m_ChannelMode)
		{
			return;
		}
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
#if BC_VISUALIZER_HAS_WASAPI
	return std::make_unique<CWasapiVisualizerSource>();
#else
	return CreatePassiveVisualizerSource();
#endif
}

} // namespace BestClientVisualizer
