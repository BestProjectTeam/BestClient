#include "source.h"

#include "analyzer.h"
#include "source_priority.h"
#include "smoother.h"

#include <base/system.h>
#include <base/time.h>

#include <engine/shared/config.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <chrono>
#include <mutex>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#if defined(CONF_PLATFORM_LINUX) && defined(BC_MUSICPLAYER_HAS_PULSE) && BC_MUSICPLAYER_HAS_PULSE
#include <pulse/error.h>
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>
#endif

namespace BestClientVisualizer
{

namespace
{

#if defined(CONF_PLATFORM_LINUX) && defined(BC_MUSICPLAYER_HAS_PULSE) && BC_MUSICPLAYER_HAS_PULSE
static bool VisualizerDebugEnabled(int Level)
{
	return g_Config.m_DbgMusicPlayer >= Level;
}

static void VisualizerDebugLog(int Level, const char *pFmt, ...)
{
	if(!VisualizerDebugEnabled(Level))
		return;

	char aBuf[1024];
	va_list Args;
	va_start(Args, pFmt);
	vsnprintf(aBuf, sizeof(aBuf), pFmt, Args);
	va_end(Args);
	dbg_msg("music_player", "[visualizer_pulse] %s", aBuf);
}

class CPulseVisualizerSource final : public IVisualizerSource
{
	struct SPulseCaptureTarget
	{
		uint32_t m_SinkIndex = PA_INVALID_INDEX;
		std::string m_DefaultSink;
		std::string m_SinkName;
		std::string m_MonitorSource;
		int m_SourceState = -1;
		int m_SampleRate = 44100;
		uint8_t m_Channels = 2;
		int m_Score = 0;
		bool m_FromActiveSinkInput = false;
		bool m_MatchesPlaybackHint = false;
		std::string m_AppName;
		std::string m_AppBinary;
		std::string m_MediaRole;
	};

	struct SPulseSinkInfo
	{
		uint32_t m_Index = PA_INVALID_INDEX;
		std::string m_Name;
		std::string m_MonitorSource;
		int m_SampleRate = 44100;
		uint8_t m_Channels = 2;
	};

	struct SPulseSourceInfo
	{
		std::string m_Name;
		uint32_t m_MonitorOfSink = PA_INVALID_INDEX;
		int m_State = -1;
	};

	struct SPulseSinkInputInfo
	{
		uint32_t m_SinkIndex = PA_INVALID_INDEX;
		std::string m_AppName;
		std::string m_AppBinary;
		std::string m_MediaRole;
		bool m_Corked = false;
		bool m_Muted = false;
	};

	struct SPulseDiscoveryRequest
	{
		bool m_ServerDone = false;
		bool m_SinksDone = false;
		bool m_SourcesDone = false;
		bool m_SinkInputsDone = false;
		std::string m_DefaultSink;
		std::vector<SPulseSinkInfo> m_vSinks;
		std::vector<SPulseSourceInfo> m_vSources;
		std::vector<SPulseSinkInputInfo> m_vSinkInputs;
	};

	std::thread m_WorkerThread;
	std::mutex m_Mutex;
	bool m_Shutdown = false;
	SVisualizerFrame m_LatestFrame;
	SVisualizerConfig m_Config;
	SVisualizerPlaybackHint m_Hint;
	int64_t m_ConfigRevision = 0;
	int64_t m_HintRevision = 0;
	int64_t m_AppliedHintRevision = -1;
	int m_ConsecutiveFailures = 0;

	static void PulseServerInfoCallback(pa_context *, const pa_server_info *pInfo, void *pUserData)
	{
		auto *pRequest = static_cast<SPulseDiscoveryRequest *>(pUserData);
		if(!pRequest)
			return;
		pRequest->m_DefaultSink = pInfo && pInfo->default_sink_name ? pInfo->default_sink_name : "";
		pRequest->m_ServerDone = true;
	}

	static void PulseSinkInfoCallback(pa_context *, const pa_sink_info *pInfo, int Eol, void *pUserData)
	{
		auto *pRequest = static_cast<SPulseDiscoveryRequest *>(pUserData);
		if(!pRequest)
			return;
		if(Eol != 0)
		{
			pRequest->m_SinksDone = true;
			return;
		}
		if(pInfo && pInfo->monitor_source_name)
		{
			SPulseSinkInfo Info;
			Info.m_Index = pInfo->index;
			Info.m_Name = pInfo->name ? pInfo->name : "";
			Info.m_MonitorSource = pInfo->monitor_source_name;
			Info.m_SampleRate = maximum(8000, (int)pInfo->sample_spec.rate);
			Info.m_Channels = std::clamp<uint8_t>(pInfo->sample_spec.channels, 1, 8);
			pRequest->m_vSinks.push_back(std::move(Info));
		}
	}

	static void PulseSourceInfoCallback(pa_context *, const pa_source_info *pInfo, int Eol, void *pUserData)
	{
		auto *pRequest = static_cast<SPulseDiscoveryRequest *>(pUserData);
		if(!pRequest)
			return;
		if(Eol != 0)
		{
			pRequest->m_SourcesDone = true;
			return;
		}
		if(pInfo && pInfo->name && pInfo->monitor_of_sink != PA_INVALID_INDEX)
		{
			SPulseSourceInfo Info;
			Info.m_Name = pInfo->name;
			Info.m_MonitorOfSink = pInfo->monitor_of_sink;
			Info.m_State = (int)pInfo->state;
			pRequest->m_vSources.push_back(std::move(Info));
		}
	}

	static void PulseSinkInputInfoCallback(pa_context *, const pa_sink_input_info *pInfo, int Eol, void *pUserData)
	{
		auto *pRequest = static_cast<SPulseDiscoveryRequest *>(pUserData);
		if(!pRequest)
			return;
		if(Eol != 0)
		{
			pRequest->m_SinkInputsDone = true;
			return;
		}
		if(!pInfo)
			return;
		SPulseSinkInputInfo Info;
		Info.m_SinkIndex = pInfo->sink;
		Info.m_Corked = pInfo->corked != 0;
		Info.m_Muted = pInfo->mute != 0;
		if(pInfo->proplist)
		{
			if(const char *pValue = pa_proplist_gets(pInfo->proplist, PA_PROP_APPLICATION_NAME))
				Info.m_AppName = pValue;
			if(const char *pValue = pa_proplist_gets(pInfo->proplist, PA_PROP_APPLICATION_PROCESS_BINARY))
				Info.m_AppBinary = pValue;
			if(const char *pValue = pa_proplist_gets(pInfo->proplist, PA_PROP_MEDIA_ROLE))
				Info.m_MediaRole = pValue;
		}
		pRequest->m_vSinkInputs.push_back(std::move(Info));
	}

	static bool WaitForPulseContext(pa_mainloop *pMainloop, pa_context *pContext)
	{
		while(true)
		{
			const pa_context_state_t State = pa_context_get_state(pContext);
			if(State == PA_CONTEXT_READY)
				return true;
			if(State == PA_CONTEXT_FAILED || State == PA_CONTEXT_TERMINATED)
				return false;
			int Ret = 0;
			if(pa_mainloop_iterate(pMainloop, 1, &Ret) < 0)
				return false;
		}
	}

	static bool WaitForPulseRequest(pa_mainloop *pMainloop, pa_operation *pOperation, bool &DoneFlag)
	{
		while(pOperation && pa_operation_get_state(pOperation) == PA_OPERATION_RUNNING && !DoneFlag)
		{
			int Ret = 0;
			if(pa_mainloop_iterate(pMainloop, 1, &Ret) < 0)
			{
				pa_operation_unref(pOperation);
				return false;
			}
		}
		if(pOperation)
			pa_operation_unref(pOperation);
		return DoneFlag;
	}

	static bool ContainsI(std::string Haystack, std::string Needle)
	{
		if(Haystack.empty() || Needle.empty())
			return false;
		std::transform(Haystack.begin(), Haystack.end(), Haystack.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		std::transform(Needle.begin(), Needle.end(), Needle.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		return Haystack.find(Needle) != std::string::npos;
	}

	static bool LooksLikeMediaRole(const std::string &Role)
	{
		return ContainsI(Role, "music") || ContainsI(Role, "video") || ContainsI(Role, "movie") || ContainsI(Role, "multimedia");
	}

	static std::vector<SPulseCaptureTarget> ResolveCaptureTargets(const SVisualizerPlaybackHint &Hint)
	{
		std::vector<SPulseCaptureTarget> vTargets;
		pa_mainloop *pMainloop = pa_mainloop_new();
		if(!pMainloop)
			return vTargets;

		pa_context *pContext = pa_context_new(pa_mainloop_get_api(pMainloop), "DDNet music visualizer");
		if(!pContext)
		{
			pa_mainloop_free(pMainloop);
			return vTargets;
		}

		if(pa_context_connect(pContext, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0 || !WaitForPulseContext(pMainloop, pContext))
		{
			pa_context_unref(pContext);
			pa_mainloop_free(pMainloop);
			return vTargets;
		}

		SPulseDiscoveryRequest Discovery;
		pa_operation *pServerOp = pa_context_get_server_info(pContext, PulseServerInfoCallback, &Discovery);
		pa_operation *pSinksOp = pa_context_get_sink_info_list(pContext, PulseSinkInfoCallback, &Discovery);
		pa_operation *pSourcesOp = pa_context_get_source_info_list(pContext, PulseSourceInfoCallback, &Discovery);
		pa_operation *pInputsOp = pa_context_get_sink_input_info_list(pContext, PulseSinkInputInfoCallback, &Discovery);
		const bool ServerOk = WaitForPulseRequest(pMainloop, pServerOp, Discovery.m_ServerDone);
		const bool SinksOk = WaitForPulseRequest(pMainloop, pSinksOp, Discovery.m_SinksDone);
		const bool SourcesOk = WaitForPulseRequest(pMainloop, pSourcesOp, Discovery.m_SourcesDone);
		const bool InputsOk = WaitForPulseRequest(pMainloop, pInputsOp, Discovery.m_SinkInputsDone);
		pa_context_disconnect(pContext);
		pa_context_unref(pContext);
		pa_mainloop_free(pMainloop);
		if(!ServerOk || !SinksOk || !SourcesOk || !InputsOk)
			return vTargets;

		auto FindSourceState = [&](const SPulseSinkInfo &Sink) {
			for(const auto &Source : Discovery.m_vSources)
			{
				if(Source.m_MonitorOfSink == Sink.m_Index || Source.m_Name == Sink.m_MonitorSource)
					return Source.m_State;
			}
			return -1;
		};

		auto UpsertTarget = [&](SPulseCaptureTarget Target) {
			for(auto &Existing : vTargets)
			{
				if(Existing.m_MonitorSource == Target.m_MonitorSource)
				{
					if(Target.m_Score > Existing.m_Score)
						Existing = std::move(Target);
					return;
				}
			}
			vTargets.push_back(std::move(Target));
		};

		for(const auto &Sink : Discovery.m_vSinks)
		{
			const int SourceState = FindSourceState(Sink);
			if(SourceState >= 0 && !PA_SOURCE_IS_OPENED((pa_source_state_t)SourceState))
				continue;
			SPulseCaptureTarget Base;
			Base.m_SinkIndex = Sink.m_Index;
			Base.m_DefaultSink = Discovery.m_DefaultSink;
			Base.m_SinkName = Sink.m_Name;
			Base.m_MonitorSource = Sink.m_MonitorSource;
			Base.m_SourceState = SourceState;
			Base.m_SampleRate = Sink.m_SampleRate;
			Base.m_Channels = Sink.m_Channels;
			if(Sink.m_Name == Discovery.m_DefaultSink)
				Base.m_Score += 30;
			UpsertTarget(Base);
		}

		for(const auto &Input : Discovery.m_vSinkInputs)
		{
			if(Input.m_Corked || Input.m_Muted)
				continue;
			if(LooksLikeDiscordPlayer(Input.m_AppName, Input.m_AppBinary))
				continue;
			for(const auto &Sink : Discovery.m_vSinks)
			{
				if(Sink.m_Index != Input.m_SinkIndex || Sink.m_MonitorSource.empty())
					continue;
				const int SourceState = FindSourceState(Sink);
				if(SourceState >= 0 && !PA_SOURCE_IS_OPENED((pa_source_state_t)SourceState))
					continue;

				SPulseCaptureTarget Target;
				Target.m_SinkIndex = Sink.m_Index;
				Target.m_DefaultSink = Discovery.m_DefaultSink;
				Target.m_SinkName = Sink.m_Name;
				Target.m_MonitorSource = Sink.m_MonitorSource;
				Target.m_SourceState = SourceState;
				Target.m_SampleRate = Sink.m_SampleRate;
				Target.m_Channels = Sink.m_Channels;
				Target.m_FromActiveSinkInput = true;
				Target.m_AppName = Input.m_AppName;
				Target.m_AppBinary = Input.m_AppBinary;
				Target.m_MediaRole = Input.m_MediaRole;
				Target.m_Score = 60;
				const int SourcePriority = PlayerSourcePriority(Input.m_AppName, Input.m_AppBinary, Input.m_MediaRole);
				if(SourcePriority <= (int)EMediaSourcePriority::DISCORD)
					continue;
				Target.m_Score += SourcePriority - (int)EMediaSourcePriority::GENERIC;
				if(LooksLikeMediaRole(Input.m_MediaRole))
					Target.m_Score += 30;
				if(Sink.m_Name == Discovery.m_DefaultSink)
					Target.m_Score += 10;
				if(HintMatchesPlaybackSource(Hint.m_ServiceId, Input.m_AppName, Input.m_AppBinary))
				{
					Target.m_MatchesPlaybackHint = true;
					Target.m_Score += 90;
				}
				UpsertTarget(std::move(Target));
			}
		}

		std::sort(vTargets.begin(), vTargets.end(), [](const SPulseCaptureTarget &A, const SPulseCaptureTarget &B) {
			if(A.m_Score != B.m_Score)
				return A.m_Score > B.m_Score;
			return A.m_MonitorSource < B.m_MonitorSource;
		});
		return vTargets;
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
		pa_simple *pSimple = nullptr;
		std::vector<float> vReadBuffer;
		std::vector<float> vMonoBuffer;
		CVisualizerAnalyzer Analyzer;
		CVisualizerSmoother Smoother;
		SPulseCaptureTarget CurrentTarget;
		int CurrentSampleRate = 48000;
		int CurrentChannels = 2;
		int64_t NextReconnectTick = 0;
		int ValidatedReads = 0;
		int ActiveSignalReads = 0;
		int SilentReads = 0;
		bool LatchedSignal = false;
		bool ValidatedLive = false;
		int64_t AppliedConfigRevision = -1;

		auto ReadState = [&]() {
			std::lock_guard<std::mutex> Lock(m_Mutex);
			return std::tuple<SVisualizerPlaybackHint, SVisualizerConfig, int64_t, int64_t>(m_Hint, m_Config, m_HintRevision, m_ConfigRevision);
		};

		auto Cleanup = [&]() {
			if(pSimple)
			{
				pa_simple_free(pSimple);
				pSimple = nullptr;
			}
			CurrentTarget = SPulseCaptureTarget();
			ValidatedReads = 0;
			ActiveSignalReads = 0;
			SilentReads = 0;
			LatchedSignal = false;
			ValidatedLive = false;
			NextReconnectTick = 0;
		};

		auto ApplyAnalyzerConfig = [&](const SVisualizerConfig &Config) {
			SVisualizerConfig RuntimeConfig = Config;
			RuntimeConfig.m_SampleRate = CurrentSampleRate;
			Analyzer.Configure(RuntimeConfig);
			Smoother.Configure(RuntimeConfig);
		};

		auto EnsureCapture = [&]() -> bool {
			if(pSimple)
				return true;

			Cleanup();
			const auto [Hint, Config, HintRevision, ConfigRevision] = ReadState();
			m_AppliedHintRevision = HintRevision;
			AppliedConfigRevision = ConfigRevision;
			const std::vector<SPulseCaptureTarget> vCandidates = ResolveCaptureTargets(Hint);
			if(vCandidates.empty())
			{
				++m_ConsecutiveFailures;
				StoreBackendState(m_ConsecutiveFailures >= 6 ? EVisualizerBackendStatus::UNAVAILABLE : EVisualizerBackendStatus::CONNECTING);
				return false;
			}

			for(const auto &Candidate : vCandidates)
			{
				CurrentTarget = Candidate;
				pa_sample_spec Spec;
				Spec.format = PA_SAMPLE_FLOAT32LE;
				Spec.rate = maximum(8000, Candidate.m_SampleRate);
				Spec.channels = Candidate.m_Channels;
				pa_buffer_attr Attr;
				Attr.maxlength = (uint32_t)-1;
				Attr.tlength = (uint32_t)-1;
				Attr.prebuf = (uint32_t)-1;
				Attr.minreq = (uint32_t)-1;
				const size_t FramesPerRead = 1024;
				vReadBuffer.resize(FramesPerRead * Spec.channels);
				vMonoBuffer.resize(FramesPerRead);
				Attr.fragsize = (uint32_t)(sizeof(float) * vReadBuffer.size());
				int Error = 0;
				pSimple = pa_simple_new(nullptr, "DDNet", PA_STREAM_RECORD, Candidate.m_MonitorSource.c_str(), "music visualizer", &Spec, nullptr, &Attr, &Error);
				if(!pSimple)
					continue;

				CurrentSampleRate = Spec.rate;
				CurrentChannels = maximum(1, (int)Spec.channels);
				m_ConsecutiveFailures = 0;
				ApplyAnalyzerConfig(Config);
				StoreBackendState(EVisualizerBackendStatus::CONNECTING);
				NextReconnectTick = time_get() + time_freq() * 2;
				VisualizerDebugLog(1, "capture ready monitor='%s' rate=%d channels=%d", CurrentTarget.m_MonitorSource.c_str(), CurrentSampleRate, CurrentChannels);
				return true;
			}

			++m_ConsecutiveFailures;
			StoreBackendState(m_ConsecutiveFailures >= 6 ? EVisualizerBackendStatus::UNAVAILABLE : EVisualizerBackendStatus::CONNECTING);
			return false;
		};

		StoreBackendState(EVisualizerBackendStatus::CONNECTING);
		while(true)
		{
			{
				std::lock_guard<std::mutex> Lock(m_Mutex);
				if(m_Shutdown)
					break;
			}

			if(!EnsureCapture())
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(350));
				continue;
			}

			const auto [Hint, Config, HintRevision, ConfigRevision] = ReadState();
			if(ConfigRevision != AppliedConfigRevision)
			{
				AppliedConfigRevision = ConfigRevision;
				ApplyAnalyzerConfig(Config);
			}

			int Error = 0;
			if(pa_simple_read(pSimple, vReadBuffer.data(), vReadBuffer.size() * sizeof(float), &Error) < 0)
			{
				Cleanup();
				++m_ConsecutiveFailures;
				StoreBackendState(m_ConsecutiveFailures >= 6 ? EVisualizerBackendStatus::UNAVAILABLE : EVisualizerBackendStatus::CONNECTING);
				std::this_thread::sleep_for(std::chrono::milliseconds(120));
				continue;
			}

			const int Channels = maximum(1, CurrentChannels);
			const int FramesRead = (int)vReadBuffer.size() / Channels;
			for(int FrameIndex = 0; FrameIndex < FramesRead; ++FrameIndex)
			{
				float MonoSample = 0.0f;
				for(int Channel = 0; Channel < Channels; ++Channel)
					MonoSample += vReadBuffer[FrameIndex * Channels + Channel];
				vMonoBuffer[FrameIndex] = std::clamp(MonoSample / Channels, -1.0f, 1.0f);
			}
			Analyzer.PushMonoSamples(vMonoBuffer.data(), FramesRead);

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

			const int64_t Now = time_get();
			if(NextReconnectTick > 0 && Now >= NextReconnectTick)
			{
				NextReconnectTick = Now + time_freq() * 2;
				if(HintRevision != m_AppliedHintRevision)
				{
					Cleanup();
					StoreBackendState(EVisualizerBackendStatus::CONNECTING);
					std::this_thread::sleep_for(std::chrono::milliseconds(80));
					continue;
				}
			}
		}

		Cleanup();
	}

public:
	CPulseVisualizerSource()
	{
		m_Config = SVisualizerConfig();
		m_WorkerThread = std::thread([this]() { WorkerMain(); });
	}

	~CPulseVisualizerSource() override
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
		if(m_Hint.m_ServiceId != Hint.m_ServiceId || m_Hint.m_Title != Hint.m_Title || m_Hint.m_Artist != Hint.m_Artist || m_Hint.m_Playing != Hint.m_Playing)
		{
			m_Hint = Hint;
			++m_HintRevision;
		}
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
		return OutFrame.m_BackendStatus == EVisualizerBackendStatus::LIVE || OutFrame.m_HasRealtimeSignal || OutFrame.m_Rms > 0.0f || OutFrame.m_Peak > 0.0f;
	}
};
#endif

} // namespace

std::unique_ptr<IVisualizerSource> CreatePulseVisualizerSource()
{
#if defined(CONF_PLATFORM_LINUX) && defined(BC_MUSICPLAYER_HAS_PULSE) && BC_MUSICPLAYER_HAS_PULSE
	return std::make_unique<CPulseVisualizerSource>();
#else
	return CreatePassiveVisualizerSource();
#endif
}

} // namespace BestClientVisualizer
