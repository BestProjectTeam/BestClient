#include "voice.h"

#include <base/log.h>
#include <base/system.h>
#include <base/str.h>
#include <base/vmath.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/console.h>

#include <game/client/gameclient.h>

#if defined(CONF_PLATFORM_ANDROID)
#include <android/android_main.h>
#endif

#include <opus/opus.h>

#if defined(CONF_RNNOISE)
#include <rnnoise.h>
#endif

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

// !!WARNING!!
// Voice full wrote by AI don't use that pls

static bool VoiceRememberLogMessage(char *pLastMessage, size_t LastMessageSize, const char *pMessage)
{
	if(str_comp(pLastMessage, pMessage) == 0)
		return false;

	str_copy(pLastMessage, pMessage, LastMessageSize);
	return true;
}

static void VoiceLogErrorOnce(char *pLastMessage, size_t LastMessageSize, const char *pMessage)
{
	if(!VoiceRememberLogMessage(pLastMessage, LastMessageSize, pMessage))
		return;

	log_error("voice", "%s", pMessage);
}

static bool VoiceListMatch(const char *pList, const char *pName)
{
	if(!pList || pList[0] == '\0')
		return false;

	const char *p = pList;
	while(*p)
	{
		while(*p == ',' || *p == ' ' || *p == '\t')
			p++;
		if(*p == '\0')
			break;

		const char *pStart = p;
		while(*p && *p != ',')
			p++;
		int Len = (int)(p - pStart);
		while(Len > 0 && std::isspace((unsigned char)pStart[Len - 1]))
			Len--;
		if(Len <= 0)
			continue;

		char aToken[MAX_NAME_LENGTH];
		str_truncate(aToken, sizeof(aToken), pStart, Len);
		if(str_comp_nocase(aToken, pName) == 0)
			return true;
	}

	return false;
}

static bool VoiceNameVolume(const char *pList, const char *pName, int &OutPercent)
{
	if(!pList || pList[0] == '\0' || !pName || pName[0] == '\0')
		return false;

	const char *p = pList;
	while(*p)
	{
		while(*p == ',' || *p == ' ' || *p == '\t')
			p++;
		if(*p == '\0')
			break;

		const char *pStart = p;
		while(*p && *p != ',')
			p++;
		const char *pEnd = p;
		while(pEnd > pStart && std::isspace((unsigned char)pEnd[-1]))
			pEnd--;
		if(pEnd <= pStart)
			continue;

		const char *pSep = nullptr;
		for(const char *q = pStart; q < pEnd; q++)	
		{
			if(*q == '=' || *q == ':')
			{
				pSep = q;
				break;
			}
		}
		if(!pSep)
			continue;

		const char *pNameEnd = pSep;
		while(pNameEnd > pStart && std::isspace((unsigned char)pNameEnd[-1]))
			pNameEnd--;
		const char *pValueStart = pSep + 1;
		while(pValueStart < pEnd && std::isspace((unsigned char)*pValueStart))
			pValueStart++;

		const int NameLen = (int)(pNameEnd - pStart);
		const int ValueLen = (int)(pEnd - pValueStart);
		if(NameLen <= 0 || ValueLen <= 0)
			continue;

		char aToken[MAX_NAME_LENGTH];
		str_truncate(aToken, sizeof(aToken), pStart, NameLen);
		if(str_comp_nocase(aToken, pName) != 0)
			continue;

		char aValue[16];
		str_truncate(aValue, sizeof(aValue), pValueStart, ValueLen);
		int Percent = str_toint(aValue);
		Percent = std::clamp(Percent, 0, 200);
		OutPercent = Percent;
		return true;
	}

	return false;
}

static constexpr char VOICE_MAGIC[4] = {'R', 'V', '0', '1'};
static constexpr uint8_t VOICE_VERSION = 3;
static constexpr uint8_t VOICE_TYPE_AUDIO = 1;
static constexpr uint8_t VOICE_TYPE_PING = 2;
static constexpr uint8_t VOICE_TYPE_PONG = 3;
static constexpr int VOICE_SAMPLE_RATE = 48000;
static constexpr int VOICE_CHANNELS = 1;
static constexpr int VOICE_FRAME_SAMPLES = 960;
static constexpr int VOICE_FRAME_BYTES = VOICE_FRAME_SAMPLES * sizeof(int16_t);
static constexpr int VOICE_MAX_PACKET = 1200;
static constexpr int VOICE_HEADER_SIZE = sizeof(VOICE_MAGIC) + 1 + 1 + 2 + 4 + 4 + 1 + 2 + 2 + 4 + 4;
static constexpr int VOICE_MAX_PAYLOAD = VOICE_MAX_PACKET - VOICE_HEADER_SIZE;
static constexpr uint32_t VOICE_GROUP_MASK = 0x3fffffff;
static constexpr uint32_t VOICE_MODE_SHIFT = 30;
static constexpr uint32_t VOICE_MODE_MASK = 0x3u;
static constexpr uint8_t VOICE_FLAG_VAD = 1 << 0;
static constexpr uint8_t VOICE_FLAG_LOOPBACK = 1 << 1;
#if defined(CONF_RNNOISE)
static constexpr int RNNOISE_FRAME_SAMPLES = 480;
#endif

static uint8_t VoiceProtocolVersion(const SRClientVoiceConfigSnapshot &Config)
{
	const int ProtocolVersion = Config.m_RiVoiceProtocolVersion > 0 ? Config.m_RiVoiceProtocolVersion : VOICE_VERSION;
	return (uint8_t)std::clamp(ProtocolVersion, 1, 255);
}

static uint32_t VoicePackToken(uint32_t GroupHash, uint32_t Mode)
{
	return (GroupHash & VOICE_GROUP_MASK) | ((Mode & VOICE_MODE_MASK) << VOICE_MODE_SHIFT);
}

static uint32_t VoiceTokenGroup(uint32_t TokenHash)
{
	return TokenHash & VOICE_GROUP_MASK;
}

static uint32_t VoiceTokenMode(uint32_t TokenHash)
{
	return (TokenHash >> VOICE_MODE_SHIFT) & VOICE_MODE_MASK;
}

static bool VoiceSenderTxAll(uint32_t Mode)
{
	return Mode == 0 || Mode == 3;
}

static bool VoiceReceiverRxAll(uint32_t Mode)
{
	return Mode == 0 || Mode == 2;
}

static bool VoiceShouldHear(uint32_t SenderGroup, uint32_t SenderMode, uint32_t ReceiverGroup, uint32_t ReceiverMode)
{
	if(VoiceSenderTxAll(SenderMode) && VoiceReceiverRxAll(ReceiverMode))
		return true;
	return SenderGroup == ReceiverGroup;
}

static void WriteU16(uint8_t *pBuf, uint16_t Value)
{
	pBuf[0] = Value & 0xff;
	pBuf[1] = (Value >> 8) & 0xff;
}

static void WriteU32(uint8_t *pBuf, uint32_t Value)
{
	pBuf[0] = Value & 0xff;
	pBuf[1] = (Value >> 8) & 0xff;
	pBuf[2] = (Value >> 16) & 0xff;
	pBuf[3] = (Value >> 24) & 0xff;
}

static void WriteFloat(uint8_t *pBuf, float Value)
{
	static_assert(sizeof(float) == 4, "float must be 4 bytes");
	uint32_t Bits = 0;
	mem_copy(&Bits, &Value, sizeof(Bits));
	WriteU32(pBuf, Bits);
}

static uint16_t ReadU16(const uint8_t *pBuf)
{
	return (uint16_t)pBuf[0] | ((uint16_t)pBuf[1] << 8);
}

static uint32_t ReadU32(const uint8_t *pBuf)
{
	return (uint32_t)pBuf[0] | ((uint32_t)pBuf[1] << 8) | ((uint32_t)pBuf[2] << 16) | ((uint32_t)pBuf[3] << 24);
}

static float ReadFloat(const uint8_t *pBuf)
{
	uint32_t Bits = ReadU32(pBuf);
	float Value = 0.0f;
	mem_copy(&Value, &Bits, sizeof(Value));
	return Value;
}

static float SanitizeFloat(float Value)
{
	if(!std::isfinite(Value))
		return 0.0f;
	if(Value > 1000000.0f)
		return 1000000.0f;
	if(Value < -1000000.0f)
		return -1000000.0f;
	return Value;
}

static float VoiceFramePeak(const int16_t *pSamples, int Count)
{
	int Peak = 0;
	for(int i = 0; i < Count; i++)
	{
		const int Sample = pSamples[i];
		const int Abs = Sample == -32768 ? 32768 : (Sample < 0 ? -Sample : Sample);
		if(Abs > Peak)
			Peak = Abs;
	}
	return Peak / 32768.0f;
}

static void ApplyMicGain(const SRClientVoiceConfigSnapshot &Config, int16_t *pSamples, int Count)
{
	const float Gain = std::clamp(Config.m_RiVoiceMicVolume / 100.0f, 0.0f, 3.0f);
	if(Gain == 1.0f)
		return;

	for(int i = 0; i < Count; i++)
	{
		const float Out = pSamples[i] * Gain;
		const int Sample = (int)std::clamp(Out, -32768.0f, 32767.0f);
		pSamples[i] = (int16_t)Sample;
	}
}

static void ApplyHpfCompressor(const SRClientVoiceConfigSnapshot &Config, int16_t *pSamples, int Count, float &PrevIn, float &PrevOut, float &Env)
{
	if(!Config.m_RiVoiceFilterEnable)
		return;

	const float CutoffHz = 120.0f;
	const float Rc = 1.0f / (2.0f * 3.14159265f * CutoffHz);
	const float Dt = 1.0f / VOICE_SAMPLE_RATE;
	const float Alpha = Rc / (Rc + Dt);

	const float Threshold = std::clamp(Config.m_RiVoiceCompThreshold / 100.0f, 0.01f, 1.0f);
	const float Ratio = std::max(1.0f, Config.m_RiVoiceCompRatio / 10.0f);
	const float AttackSec = std::max(0.001f, Config.m_RiVoiceCompAttackMs / 1000.0f);
	const float ReleaseSec = std::max(0.001f, Config.m_RiVoiceCompReleaseMs / 1000.0f);
	const float MakeupGain = std::max(0.0f, Config.m_RiVoiceCompMakeup / 100.0f);
	const float NoiseFloor = 0.02f;
	const float Limiter = std::clamp(Config.m_RiVoiceLimiter / 100.0f, 0.05f, 1.0f);
	const float AttackCoeff = 1.0f - std::exp(-1.0f / (AttackSec * VOICE_SAMPLE_RATE));
	const float ReleaseCoeff = 1.0f - std::exp(-1.0f / (ReleaseSec * VOICE_SAMPLE_RATE));

	for(int i = 0; i < Count; i++)
	{
		const float x = pSamples[i] / 32768.0f;
		const float y = Alpha * (PrevOut + x - PrevIn);
		PrevIn = x;
		PrevOut = SanitizeFloat(y);

		const float AbsY = std::fabs(PrevOut);
		if(AbsY > Env)
			Env += (AbsY - Env) * AttackCoeff;
		else
			Env += (AbsY - Env) * ReleaseCoeff;

		float Gain = 1.0f;
		if(Env > Threshold)
			Gain = (Threshold + (Env - Threshold) / Ratio) / Env;
		if(Env > NoiseFloor)
			Gain *= MakeupGain;

		const float Out = std::clamp(PrevOut * Gain, -Limiter, Limiter);
		const int Sample = (int)std::clamp(Out * 32767.0f, -32768.0f, 32767.0f);
		pSamples[i] = (int16_t)Sample;
	}
}

static float VoiceFrameRms(const int16_t *pSamples, int Count)
{
	if(Count <= 0)
		return 0.0f;
	double Sum = 0.0;
	for(int i = 0; i < Count; i++)
	{
		const float x = pSamples[i] / 32768.0f;
		Sum += x * x;
	}
	return (float)std::sqrt(Sum / (double)Count);
}

static void ApplyNoiseSuppressorSimple(const SRClientVoiceConfigSnapshot &Config, int16_t *pSamples, int Count, float &NoiseFloor, float &Gate)
{
	if(!Config.m_RiVoiceNoiseSuppressEnable)
		return;

	const float Strength = std::clamp(Config.m_RiVoiceNoiseSuppressStrength / 100.0f, 0.0f, 1.0f);
	if(Strength <= 0.0f)
		return;

	const float Rms = VoiceFrameRms(pSamples, Count);
	if(!std::isfinite(Rms))
		return;

	if(NoiseFloor <= 0.0f)
		NoiseFloor = Rms;

	// Update noise floor estimate only when signal is close to noise.
	const float UpdateFast = 0.2f;
	const float UpdateSlow = 0.05f;
	if(Rms < NoiseFloor * 1.2f)
		NoiseFloor += (Rms - NoiseFloor) * UpdateFast;
	else if(Rms < NoiseFloor * 1.5f)
		NoiseFloor += (Rms - NoiseFloor) * UpdateSlow;

	NoiseFloor = std::clamp(NoiseFloor, 1.0f / 32768.0f, 0.5f);

	const float MinGain = 1.0f - Strength * 0.9f;
	const float Low = 1.2f;
	const float High = 2.5f;
	const float Snr = Rms / (NoiseFloor + 1e-6f);

	float Target = 1.0f;
	if(Snr <= Low)
		Target = MinGain;
	else if(Snr >= High)
		Target = 1.0f;
	else
	{
		const float T = (Snr - Low) / (High - Low);
		Target = MinGain + (1.0f - MinGain) * T;
	}

	const float Dt = Count / (float)VOICE_SAMPLE_RATE;
	const float AttackSec = 0.01f;
	const float ReleaseSec = 0.08f;
	const float AttackCoeff = 1.0f - std::exp(-Dt / AttackSec);
	const float ReleaseCoeff = 1.0f - std::exp(-Dt / ReleaseSec);

	if(Target > Gate)
		Gate += (Target - Gate) * AttackCoeff;
	else
		Gate += (Target - Gate) * ReleaseCoeff;

	Gate = std::clamp(Gate, MinGain, 1.0f);

	if(Gate >= 0.999f)
		return;

	for(int i = 0; i < Count; i++)
	{
		const float Out = pSamples[i] * Gate;
		pSamples[i] = (int16_t)std::clamp(Out, -32768.0f, 32767.0f);
	}
}

static void ApplyNoiseSuppressor(const SRClientVoiceConfigSnapshot &Config, int16_t *pSamples, int Count, float &NoiseFloor, float &Gate, DenoiseState *&pState)
{
	if(!Config.m_RiVoiceNoiseSuppressEnable)
		return;

	const float Strength = std::clamp(Config.m_RiVoiceNoiseSuppressStrength / 100.0f, 0.0f, 1.0f);
	if(Strength <= 0.0f)
		return;

#if defined(CONF_RNNOISE)
	if(!pState)
		pState = rnnoise_create(nullptr);
	if(!pState)
	{
		ApplyNoiseSuppressorSimple(Config, pSamples, Count, NoiseFloor, Gate);
		return;
	}

	const int FrameSize = rnnoise_get_frame_size();
	if(FrameSize != RNNOISE_FRAME_SAMPLES || Count < FrameSize)
		return;

	const int Frames = Count / FrameSize;
	for(int f = 0; f < Frames; f++)
	{
		float aIn[RNNOISE_FRAME_SAMPLES];
		float aOut[RNNOISE_FRAME_SAMPLES];
		const int Base = f * FrameSize;
		for(int i = 0; i < FrameSize; i++)
			aIn[i] = (float)pSamples[Base + i];

		rnnoise_process_frame(pState, aOut, aIn);

		for(int i = 0; i < FrameSize; i++)
		{
			// Use RNNoise output only to avoid combing/doubling from dry+wet mix.
			const float y = aOut[i];
			pSamples[Base + i] = (int16_t)std::clamp(y, -32768.0f, 32767.0f);
		}
	}
#else
	ApplyNoiseSuppressorSimple(Config, pSamples, Count, NoiseFloor, Gate);
#endif
}

void CRClientVoice::SDLAudioCallback(void *pUserData, Uint8 *pStream, int Len)
{
	auto *pThis = static_cast<CRClientVoice *>(pUserData);
	if(!pThis || Len <= 0)
		return;

	const int OutputChannels = std::max(1, pThis->m_OutputChannels.load());
	const int Samples = Len / (int)(sizeof(int16_t) * OutputChannels);
	if(Samples <= 0)
	{
		mem_zero(pStream, Len);
		return;
	}

	pThis->MixAudio(reinterpret_cast<int16_t *>(pStream), Samples, OutputChannels);
}

static bool ParseHostPort(const char *pAddrStr, char *pHost, size_t HostSize, int &Port)
{
	const char *pColon = str_rchr(pAddrStr, ':');
	if(!pColon || pColon == pAddrStr || *(pColon + 1) == '\0')
		return false;

	str_truncate(pHost, HostSize, pAddrStr, pColon - pAddrStr);
	if(pHost[0] == '[')
	{
		const int Len = str_length(pHost);
		if(Len >= 2 && pHost[Len - 1] == ']')
		{
			mem_move(pHost, pHost + 1, Len - 2);
			pHost[Len - 2] = '\0';
		}
	}

	Port = str_toint(pColon + 1);
	return Port > 0 && Port <= 65535;
}

void CRClientVoice::Init(CGameClient *pGameClient, IClient *pClient, IConsole *pConsole)
{
	m_pGameClient = pGameClient;
	m_pClient = pClient;
	m_pConsole = pConsole;
	m_pGraphics = m_pGameClient ? m_pGameClient->Kernel()->RequestInterface<IEngineGraphics>() : nullptr;
	m_ShutdownDone = false;
}

void CRClientVoice::OnShutdown()
{
	Shutdown();
}

void CRClientVoice::SetPttActive(bool Active)
{
	const bool WasActive = m_PttActive.exchange(Active);
	if(Active)
	{
		m_PttReleaseDeadline.store(0);
		return;
	}

	if(WasActive)
	{
		const int DelayMs = std::clamp(g_Config.m_RiVoicePttReleaseDelayMs, 0, 1000);
		if(DelayMs > 0)
			m_PttReleaseDeadline.store(time_get() + (int64_t)time_freq() * DelayMs / 1000);
		else
			m_PttReleaseDeadline.store(0);
	}
}

static int ClampJitterTarget(float JitterMs)
{
	if(JitterMs <= 8.0f)
		return 2;
	if(JitterMs <= 14.0f)
		return 3;
	if(JitterMs <= 22.0f)
		return 4;
	if(JitterMs <= 32.0f)
		return 5;
	return 6;
}

static int SeqDelta(uint16_t NewSeq, uint16_t OldSeq)
{
	return (int)(int16_t)(NewSeq - OldSeq);
}

static bool SeqLess(uint16_t A, uint16_t B)
{
	return (int16_t)(A - B) < 0;
}

bool CRClientVoice::EnsureSocket()
{
	if(m_Socket)
		return true;

	NETADDR BindAddr = NETADDR_ZEROED;
	BindAddr.type = NETTYPE_IPV4 | NETTYPE_IPV6;
	m_Socket = net_udp_create(BindAddr);
	if(!m_Socket)
	{
		VoiceLogErrorOnce(m_aSocketErrorLog, sizeof(m_aSocketErrorLog), "Failed to open UDP socket");
		return false;
	}
	m_aSocketErrorLog[0] = '\0';
	return true;
}

bool CRClientVoice::EnsureAudio()
{
	SDL_AudioSpec WantCapture = {};
	WantCapture.freq = VOICE_SAMPLE_RATE;
	WantCapture.format = AUDIO_S16;
	WantCapture.channels = VOICE_CHANNELS;
	WantCapture.samples = VOICE_FRAME_SAMPLES;
	WantCapture.callback = nullptr;

	const bool WantStereo = g_Config.m_RiVoiceStereo != 0;
	const int DesiredOutputChannels = WantStereo ? 2 : 1;

	SDL_AudioSpec WantOutput = {};
	WantOutput.freq = VOICE_SAMPLE_RATE;
	WantOutput.format = AUDIO_S16;
	WantOutput.channels = DesiredOutputChannels;
	WantOutput.samples = VOICE_FRAME_SAMPLES;
	WantOutput.callback = SDLAudioCallback;
	WantOutput.userdata = this;

	const bool BackendChanged = str_comp(m_aAudioBackend, g_Config.m_RiVoiceAudioBackend) != 0;
	if(BackendChanged)
	{
		if(m_CaptureDevice)
		{
			SDL_CloseAudioDevice(m_CaptureDevice);
			m_CaptureDevice = 0;
		}
		if(m_OutputDevice)
		{
			SDL_CloseAudioDevice(m_OutputDevice);
			m_OutputDevice = 0;
		}
		m_CaptureSpec = {};
		m_OutputSpec = {};
		m_OutputChannels.store(0);
		m_MixBuffer.clear();
		str_copy(m_aAudioBackend, g_Config.m_RiVoiceAudioBackend, sizeof(m_aAudioBackend));
		m_aAudioBackendMismatchReq[0] = '\0';
		m_aAudioBackendMismatchCur[0] = '\0';
		m_aAudioInitLoggedBackend[0] = '\0';
		m_aAudioErrorLog[0] = '\0';
		m_aEncoderErrorLog[0] = '\0';
		m_LogDeviceChange = true;
		m_CaptureUnavailable = false;
		m_OutputUnavailable = false;
		m_LastAudioRetryAttempt = 0;
	}

	const char *pRequestedBackend = g_Config.m_RiVoiceAudioBackend[0] ? g_Config.m_RiVoiceAudioBackend : nullptr;
	if((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0)
	{
		if(SDL_AudioInit(pRequestedBackend) < 0)
		{
			if(pRequestedBackend)
			{
				char aError[256];
				str_format(aError, sizeof(aError), "Failed to init audio backend '%s': %s", pRequestedBackend, SDL_GetError());
				VoiceLogErrorOnce(m_aAudioErrorLog, sizeof(m_aAudioErrorLog), aError);
			}
			else
			{
				char aError[256];
				str_format(aError, sizeof(aError), "Failed to init audio: %s", SDL_GetError());
				VoiceLogErrorOnce(m_aAudioErrorLog, sizeof(m_aAudioErrorLog), aError);
			}
			return false;
		}
		m_AudioSubsystemInitializedByVoice = true;
		const char *pDriver = SDL_GetCurrentAudioDriver();
		if(pDriver && pDriver[0] != '\0')
		{
			if(str_comp_nocase(m_aAudioInitLoggedBackend, pDriver) != 0)
			{
				log_info("voice", "audio initialized using backend '%s'", pDriver);
				str_copy(m_aAudioInitLoggedBackend, pDriver, sizeof(m_aAudioInitLoggedBackend));
			}
		}
		else if(m_aAudioInitLoggedBackend[0] == '\0')
		{
			log_info("voice", "audio initialized");
			str_copy(m_aAudioInitLoggedBackend, "<unknown>", sizeof(m_aAudioInitLoggedBackend));
		}
	}
	else if(pRequestedBackend && pRequestedBackend[0] != '\0')
	{
		const char *pDriver = SDL_GetCurrentAudioDriver();
		if(pDriver && str_comp_nocase(pDriver, pRequestedBackend) != 0)
		{
			const bool ReqChanged = str_comp_nocase(m_aAudioBackendMismatchReq, pRequestedBackend) != 0;
			const bool CurChanged = str_comp_nocase(m_aAudioBackendMismatchCur, pDriver) != 0;
			if(ReqChanged || CurChanged)
			{
				log_info("voice", "audio backend already initialized as '%s' (requested '%s')", pDriver, pRequestedBackend);
				str_copy(m_aAudioBackendMismatchReq, pRequestedBackend, sizeof(m_aAudioBackendMismatchReq));
				str_copy(m_aAudioBackendMismatchCur, pDriver, sizeof(m_aAudioBackendMismatchCur));
			}
		}
		else
		{
			m_aAudioBackendMismatchReq[0] = '\0';
			m_aAudioBackendMismatchCur[0] = '\0';
		}
	}

	const bool HadCapture = m_CaptureDevice != 0;
	const bool HadOutput = m_OutputDevice != 0;
	const bool HadEncoder = m_pEncoder != nullptr;

	if(str_comp(m_aInputDeviceName, g_Config.m_RiVoiceInputDevice) != 0)
	{
		if(m_CaptureDevice)
		{
			SDL_CloseAudioDevice(m_CaptureDevice);
			m_CaptureDevice = 0;
		}
		str_copy(m_aInputDeviceName, g_Config.m_RiVoiceInputDevice, sizeof(m_aInputDeviceName));
		m_LogDeviceChange = true;
		m_CaptureUnavailable = false;
		m_LastAudioRetryAttempt = 0;
	}

	if(str_comp(m_aOutputDeviceName, g_Config.m_RiVoiceOutputDevice) != 0)
	{
		if(m_OutputDevice)
		{
			SDL_CloseAudioDevice(m_OutputDevice);
			m_OutputDevice = 0;
		}
		str_copy(m_aOutputDeviceName, g_Config.m_RiVoiceOutputDevice, sizeof(m_aOutputDeviceName));
		m_LogDeviceChange = true;
		m_OutputUnavailable = false;
		m_LastAudioRetryAttempt = 0;
	}

	if(m_OutputStereo != WantStereo)
	{
		if(m_OutputDevice)
		{
			SDL_CloseAudioDevice(m_OutputDevice);
			m_OutputDevice = 0;
		}
		m_OutputStereo = WantStereo;
		m_LogDeviceChange = true;
		m_OutputUnavailable = false;
		m_LastAudioRetryAttempt = 0;
	}

	if(HadCapture && HadOutput && HadEncoder && m_CaptureDevice && m_OutputDevice && m_pEncoder)
	{
		return true;
	}

	const char *pInputName = FindDeviceName(true, m_aInputDeviceName);
	const char *pOutputName = FindDeviceName(false, m_aOutputDeviceName);

	if(!m_pEncoder)
	{
		int Error = 0;
		m_pEncoder = opus_encoder_create(VOICE_SAMPLE_RATE, VOICE_CHANNELS, OPUS_APPLICATION_VOIP, &Error);
		if(!m_pEncoder || Error != OPUS_OK)
		{
			char aError[256];
			str_format(aError, sizeof(aError), "Failed to create Opus encoder: %d", Error);
			VoiceLogErrorOnce(m_aEncoderErrorLog, sizeof(m_aEncoderErrorLog), aError);
			return false;
		}
		m_aEncoderErrorLog[0] = '\0';
		m_EncBitrate = 24000;
		m_EncLossPerc = 0;
		m_EncFec = false;
		m_LastEncUpdate = 0;
		opus_encoder_ctl(m_pEncoder, OPUS_SET_BITRATE(m_EncBitrate));
		opus_encoder_ctl(m_pEncoder, OPUS_SET_PACKET_LOSS_PERC(m_EncLossPerc));
		opus_encoder_ctl(m_pEncoder, OPUS_SET_INBAND_FEC(m_EncFec ? 1 : 0));
		opus_encoder_ctl(m_pEncoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
	}

	if(!m_OutputDevice)
	{
		const bool OutputMissing = m_aOutputDeviceName[0] != '\0' && pOutputName == nullptr;
		const bool NoOutputDevices = SDL_GetNumAudioDevices(0) <= 0;

		if(OutputMissing)
		{
			if(!m_OutputUnavailable)
			{
				char aError[256];
				str_format(aError, sizeof(aError), "Output device not found: '%s'", m_aOutputDeviceName);
				VoiceLogErrorOnce(m_aAudioErrorLog, sizeof(m_aAudioErrorLog), aError);
			}
			m_OutputUnavailable = true;
		}
		else if(NoOutputDevices)
		{
			if(!m_OutputUnavailable)
				VoiceLogErrorOnce(m_aAudioErrorLog, sizeof(m_aAudioErrorLog), "No output devices available");
			m_OutputUnavailable = true;
		}
		else
		{
			log_info("voice", "attempting to open output device '%s'", pOutputName ? pOutputName : "<default>");
			m_OutputDevice = SDL_OpenAudioDevice(pOutputName, 0, &WantOutput, &m_OutputSpec, 0);
			if(!m_OutputDevice)
			{
				if(!m_OutputUnavailable)
				{
					char aError[256];
					str_format(aError, sizeof(aError), "Failed to open output device: %s", SDL_GetError());
					VoiceLogErrorOnce(m_aAudioErrorLog, sizeof(m_aAudioErrorLog), aError);
				}
				m_OutputUnavailable = true;
			}
			else
			{
				const int Channels = m_OutputSpec.channels > 0 ? m_OutputSpec.channels : (WantStereo ? 2 : 1);
				log_info("voice", "output device opened '%s' %dch@%d",
					pOutputName ? pOutputName : "<default>",
					Channels,
					m_OutputSpec.freq);
				m_OutputChannels.store(Channels);
				m_MixBuffer.resize((size_t)m_OutputSpec.samples * Channels);
				SDL_PauseAudioDevice(m_OutputDevice, 0);
				ClearPeerFrames();
				m_OutputUnavailable = false;
			}
		}
	}
	else
	{
		m_OutputUnavailable = false;
	}

	if(!m_CaptureDevice)
	{
#if defined(CONF_PLATFORM_ANDROID)
		if(m_AndroidRecordPermissionKnown && !m_AndroidRecordPermissionGranted)
		{
			m_CaptureUnavailable = true;
		}
		else
#endif
		{
		const bool InputMissing = m_aInputDeviceName[0] != '\0' && pInputName == nullptr;
		const bool NoCaptureDevices = SDL_GetNumAudioDevices(1) <= 0;

		if(InputMissing)
		{
			if(!m_CaptureUnavailable)
			{
				char aError[256];
				str_format(aError, sizeof(aError), "Input device not found: '%s'", m_aInputDeviceName);
				VoiceLogErrorOnce(m_aAudioErrorLog, sizeof(m_aAudioErrorLog), aError);
			}
			m_CaptureUnavailable = true;
		}
		else if(NoCaptureDevices)
		{
			if(!m_CaptureUnavailable)
				VoiceLogErrorOnce(m_aAudioErrorLog, sizeof(m_aAudioErrorLog), "No capture devices available");
			m_CaptureUnavailable = true;
		}
		else
		{
			log_info("voice", "attempting to open capture device '%s'", pInputName ? pInputName : "<default>");
			m_CaptureDevice = SDL_OpenAudioDevice(pInputName, 1, &WantCapture, &m_CaptureSpec, 0);
			if(!m_CaptureDevice)
			{
				if(!m_CaptureUnavailable)
				{
					char aError[256];
					str_format(aError, sizeof(aError), "Failed to open capture device: %s", SDL_GetError());
					VoiceLogErrorOnce(m_aAudioErrorLog, sizeof(m_aAudioErrorLog), aError);
				}
				m_CaptureUnavailable = true;
			}
			else
			{
				log_info("voice", "capture device opened '%s' %dch@%d",
					pInputName ? pInputName : "<default>",
					m_CaptureSpec.channels,
					m_CaptureSpec.freq);
				SDL_PauseAudioDevice(m_CaptureDevice, 0);
				m_CaptureUnavailable = false;
			}
		}
		}
	}
	else
	{
		m_CaptureUnavailable = false;
	}

	if(m_LogDeviceChange)
	{
		const char *pInputReq = m_aInputDeviceName[0] ? m_aInputDeviceName : "<default>";
		const char *pOutputReq = m_aOutputDeviceName[0] ? m_aOutputDeviceName : "<default>";
		const char *pInputResolved = pInputName ? pInputName : "<default>";
		const char *pOutputResolved = pOutputName ? pOutputName : "<default>";
		log_info("voice", "audio devices set input='%s' resolved='%s' output='%s' resolved='%s' capture=%dch@%d output=%dch@%d",
			pInputReq, pInputResolved, pOutputReq, pOutputResolved,
			m_CaptureSpec.channels, m_CaptureSpec.freq,
			m_OutputSpec.channels, m_OutputSpec.freq);
		m_LogDeviceChange = false;
	}

	if(m_CaptureUnavailable || m_OutputUnavailable)
		m_LastAudioRetryAttempt = time_get();
	else
		m_LastAudioRetryAttempt = 0;

	m_aAudioErrorLog[0] = '\0';
	m_aEncoderErrorLog[0] = '\0';
	return true;
}

void CRClientVoice::PushPeerFrame(int PeerId, const int16_t *pPcm, int Samples, float LeftGain, float RightGain)
{
	if(PeerId < 0 || PeerId >= MAX_CLIENTS)
		return;
	if(Samples <= 0)
		return;

	SVoicePeer &Peer = m_aPeers[PeerId];
	if(Peer.m_FrameCount >= SVoicePeer::MAX_FRAMES)
	{
		Peer.m_FrameHead = (Peer.m_FrameHead + 1) % SVoicePeer::MAX_FRAMES;
		Peer.m_FrameCount--;
		Peer.m_FrameReadPos = 0;
	}

	SVoicePeer::SVoiceFrame &Frame = Peer.m_aFrames[Peer.m_FrameTail];
	const int CopySamples = std::min(Samples, VOICE_FRAME_SAMPLES);
	mem_copy(Frame.m_aPcm, pPcm, CopySamples * sizeof(int16_t));
	Frame.m_Samples = CopySamples;
	Frame.m_LeftGain = LeftGain;
	Frame.m_RightGain = RightGain;
	Peer.m_FrameTail = (Peer.m_FrameTail + 1) % SVoicePeer::MAX_FRAMES;
	Peer.m_FrameCount++;
}

void CRClientVoice::MixAudio(int16_t *pOut, int Samples, int OutputChannels)
{
	if(Samples <= 0 || OutputChannels <= 0)
		return;

	const int Needed = Samples * OutputChannels;
	if((int)m_MixBuffer.size() < Needed)
	{
		mem_zero(pOut, (size_t)Needed * sizeof(int16_t));
		return;
	}
	std::fill(m_MixBuffer.begin(), m_MixBuffer.begin() + Needed, 0);

	for(auto &Peer : m_aPeers)
	{
		int FrameIdx = Peer.m_FrameHead;
		int FrameCount = Peer.m_FrameCount;
		int ReadPos = Peer.m_FrameReadPos;
		if(FrameCount <= 0)
			continue;

		for(int i = 0; i < Samples; i++)
		{
			if(FrameCount <= 0)
				break;

			SVoicePeer::SVoiceFrame &Frame = Peer.m_aFrames[FrameIdx];
			const int16_t Pcm = Frame.m_aPcm[ReadPos];
			const float LeftGain = Frame.m_LeftGain;
			const float RightGain = Frame.m_RightGain;

			const int Base = i * OutputChannels;
			if(OutputChannels == 1)
			{
				const float MonoGain = 0.5f * (LeftGain + RightGain);
				m_MixBuffer[Base] += (int32_t)(Pcm * MonoGain);
			}
			else
			{
				m_MixBuffer[Base] += (int32_t)(Pcm * LeftGain);
				m_MixBuffer[Base + 1] += (int32_t)(Pcm * RightGain);
				if(OutputChannels > 2)
				{
					const int32_t Center = (int32_t)(Pcm * 0.5f * (LeftGain + RightGain));
					for(int ch = 2; ch < OutputChannels; ch++)
						m_MixBuffer[Base + ch] += Center;
				}
			}

			ReadPos++;
			if(ReadPos >= Frame.m_Samples)
			{
				ReadPos = 0;
				FrameIdx = (FrameIdx + 1) % SVoicePeer::MAX_FRAMES;
				FrameCount--;
			}
		}

		Peer.m_FrameHead = FrameIdx;
		Peer.m_FrameCount = FrameCount;
		Peer.m_FrameReadPos = ReadPos;
	}

	for(int i = 0; i < Needed; i++)
	{
		pOut[i] = (int16_t)std::clamp(m_MixBuffer[i], -32768, 32767);
	}
}

void CRClientVoice::ClearPeerFrames()
{
	if(m_OutputDevice)
		SDL_LockAudioDevice(m_OutputDevice);
	for(auto &Peer : m_aPeers)
	{
		for(auto &Pkt : Peer.m_aPackets)
		{
			Pkt.m_Valid = false;
			Pkt.m_Size = 0;
			Pkt.m_Seq = 0;
			Pkt.m_LeftGain = 1.0f;
			Pkt.m_RightGain = 1.0f;
		}
		Peer.m_QueuedPackets = 0;
		Peer.m_LastSeq = 0;
		Peer.m_HasSeq = false;
		Peer.m_HasNextSeq = false;
		Peer.m_NextSeq = 0;
		Peer.m_HasLastRecvSeq = false;
		Peer.m_LastRecvSeq = 0;
		Peer.m_LastRecvTime = 0;
		Peer.m_JitterMs = 0.0f;
		Peer.m_TargetFrames = 3;
		Peer.m_LastGainLeft = 1.0f;
		Peer.m_LastGainRight = 1.0f;
		Peer.m_LossEwma = 0.0f;
		if(Peer.m_pDecoder)
			opus_decoder_ctl(Peer.m_pDecoder, OPUS_RESET_STATE);
		Peer.m_FrameHead = 0;
		Peer.m_FrameTail = 0;
		Peer.m_FrameCount = 0;
		Peer.m_FrameReadPos = 0;
	}
	if(m_OutputDevice)
		SDL_UnlockAudioDevice(m_OutputDevice);
}

void CRClientVoice::ResetPeer(SVoicePeer &Peer)
{
	if(m_OutputDevice)
		SDL_LockAudioDevice(m_OutputDevice);
	Peer.m_FrameHead = 0;
	Peer.m_FrameTail = 0;
	Peer.m_FrameCount = 0;
	Peer.m_FrameReadPos = 0;
	for(auto &Pkt : Peer.m_aPackets)
	{
		Pkt.m_Valid = false;
		Pkt.m_Size = 0;
		Pkt.m_Seq = 0;
		Pkt.m_LeftGain = 1.0f;
		Pkt.m_RightGain = 1.0f;
	}
	Peer.m_QueuedPackets = 0;
	Peer.m_LastSeq = 0;
	Peer.m_HasSeq = false;
	Peer.m_HasNextSeq = false;
	Peer.m_NextSeq = 0;
	Peer.m_HasLastRecvSeq = false;
	Peer.m_LastRecvSeq = 0;
	Peer.m_LastRecvTime = 0;
	Peer.m_JitterMs = 0.0f;
	Peer.m_TargetFrames = 3;
	Peer.m_LastGainLeft = 1.0f;
	Peer.m_LastGainRight = 1.0f;
	Peer.m_LossEwma = 0.0f;
	if(Peer.m_pDecoder)
		opus_decoder_ctl(Peer.m_pDecoder, OPUS_RESET_STATE);
	if(m_OutputDevice)
		SDL_UnlockAudioDevice(m_OutputDevice);
}

const char *CRClientVoice::FindDeviceName(bool Capture, const char *pDesired) const
{
	if(!pDesired || pDesired[0] == '\0')
		return nullptr;

	const int Num = SDL_GetNumAudioDevices(Capture ? 1 : 0);
	for(int i = 0; i < Num; i++)
	{
		const char *pName = SDL_GetAudioDeviceName(i, Capture ? 1 : 0);
		if(pName && str_comp_nocase(pName, pDesired) == 0)
			return pName;
	}
	return nullptr;
}

void CRClientVoice::ListDevices()
{
	if(!m_pConsole)
		return;

	m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "voice", "Input devices:");
	const int NumInputs = SDL_GetNumAudioDevices(1);
	for(int i = 0; i < NumInputs; i++)
	{
		const char *pName = SDL_GetAudioDeviceName(i, 1);
		if(pName)
			m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "voice", pName);
	}

	m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "voice", "Output devices:");
	const int NumOutputs = SDL_GetNumAudioDevices(0);
	for(int i = 0; i < NumOutputs; i++)
	{
		const char *pName = SDL_GetAudioDeviceName(i, 0);
		if(pName)
			m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "voice", pName);
	}
}

void CRClientVoice::UpdateMicLevel(float Peak)
{
	const float Prev = m_MicLevel.load();
	if(Peak < 0.0f)
	{
		m_MicLevel.store(Prev * 0.97f);
		return;
	}
	Peak = std::clamp(Peak, 0.0f, 1.0f);
	const float Next = Peak >= Prev ? Peak : (Prev * 0.9f);
	m_MicLevel.store(Next);
}

void CRClientVoice::Shutdown()
{
	if(m_ShutdownDone)
		return;
	m_ShutdownDone = true;

	StopWorker();

	if(m_CaptureDevice)
	{
		SDL_CloseAudioDevice(m_CaptureDevice);
		m_CaptureDevice = 0;
	}
	if(m_OutputDevice)
	{
		SDL_CloseAudioDevice(m_OutputDevice);
		m_OutputDevice = 0;
	}
	m_OutputChannels.store(0);
	m_MixBuffer.clear();
	m_CaptureUnavailable = false;
	m_OutputUnavailable = false;
#if defined(CONF_PLATFORM_ANDROID)
	m_AndroidRecordPermissionKnown = false;
	m_AndroidRecordPermissionGranted = false;
#endif
	m_LastAudioRetryAttempt = 0;
	m_AudioRefreshRequested.store(true);
	if(m_pEncoder)
	{
		opus_encoder_destroy(m_pEncoder);
		m_pEncoder = nullptr;
	}
	for(auto &Peer : m_aPeers)
	{
		if(Peer.m_pDecoder)
		{
			opus_decoder_destroy(Peer.m_pDecoder);
			Peer.m_pDecoder = nullptr;
		}
	}
	if(m_Socket)
	{
		net_udp_close(m_Socket);
		m_Socket = nullptr;
	}
	ClearPeerFrames();
	m_ServerAddrValid.store(false);
	m_ServerAddrResolveRequested.store(true);
	m_aServerAddrStr[0] = '\0';
	m_LastServerResolveAttempt = 0;
	m_HpfPrevIn = 0.0f;
	m_HpfPrevOut = 0.0f;
	m_CompEnv = 0.0f;
	m_NsNoiseFloor = 0.0f;
	m_NsGain = 1.0f;
#if defined(CONF_RNNOISE)
	if(m_pNoiseSuppress)
	{
		rnnoise_destroy(m_pNoiseSuppress);
		m_pNoiseSuppress = nullptr;
	}
#endif
	m_aAudioBackend[0] = '\0';
	m_aAudioBackendMismatchReq[0] = '\0';
	m_aAudioBackendMismatchCur[0] = '\0';
	m_aAudioInitLoggedBackend[0] = '\0';
	m_aSocketErrorLog[0] = '\0';
	m_aAudioErrorLog[0] = '\0';
	m_aEncoderErrorLog[0] = '\0';
	m_aServerAddrErrorLog[0] = '\0';
	m_aDecoderErrorLog[0] = '\0';
	m_AudioSubsystemInitializedByVoice = false;
	m_PingMs.store(-1);
	m_MicLevel.store(0.0f);
	m_LastPingSentTime = 0;
	m_LastPingSeq = 0;
}

void CRClientVoice::UpdateServerAddrConfig()
{
	bool AddrChanged = false;
	{
		std::lock_guard<std::mutex> Guard(m_ServerAddrMutex);
		AddrChanged = str_comp(m_aServerAddrStr, g_Config.m_RiVoiceServer) != 0;
		if(AddrChanged)
			str_copy(m_aServerAddrStr, g_Config.m_RiVoiceServer, sizeof(m_aServerAddrStr));
	}

	if(!AddrChanged)
		return;

	m_ServerAddrValid.store(false);
	m_LastServerResolveAttempt = 0;
	m_ServerAddrResolveRequested.store(true);
}

void CRClientVoice::ResolveServerAddr()
{
	const int64_t Now = time_get();
	const bool ShouldRetry = !m_ServerAddrValid.load() && (m_LastServerResolveAttempt == 0 || Now - m_LastServerResolveAttempt > time_freq() * 5);
	if(!m_ServerAddrResolveRequested.load() && !ShouldRetry)
		return;

	char aServerAddrStr[sizeof(m_aServerAddrStr)];
	{
		std::lock_guard<std::mutex> Guard(m_ServerAddrMutex);
		str_copy(aServerAddrStr, m_aServerAddrStr, sizeof(aServerAddrStr));
	}

	m_ServerAddrResolveRequested.store(false);
	if(aServerAddrStr[0] == '\0')
	{
		m_ServerAddrValid.store(false);
		return;
	}

	m_LastServerResolveAttempt = Now;

	NETADDR NewAddr = NETADDR_ZEROED;
	if(net_addr_from_str(&NewAddr, aServerAddrStr) == 0)
	{
		{
			std::lock_guard<std::mutex> Guard(m_ServerAddrMutex);
			m_ServerAddr = NewAddr;
		}
		m_ServerAddrValid.store(true);
		m_aServerAddrErrorLog[0] = '\0';
		return;
	}

	char aHost[128];
	int Port = 0;
	if(!ParseHostPort(aServerAddrStr, aHost, sizeof(aHost), Port))
	{
		char aError[256];
		str_format(aError, sizeof(aError), "Invalid voice server address '%s'", aServerAddrStr);
		VoiceLogErrorOnce(m_aServerAddrErrorLog, sizeof(m_aServerAddrErrorLog), aError);
		return;
	}

	if(net_host_lookup(aHost, &NewAddr, NETTYPE_IPV4) == 0 || net_host_lookup(aHost, &NewAddr, NETTYPE_IPV6) == 0)
	{
		NewAddr.port = Port;
		{
			std::lock_guard<std::mutex> Guard(m_ServerAddrMutex);
			m_ServerAddr = NewAddr;
		}
		m_ServerAddrValid.store(true);
		m_aServerAddrErrorLog[0] = '\0';
		return;
	}

	char aError[256];
	str_format(aError, sizeof(aError), "Failed to resolve voice server '%s'", aServerAddrStr);
	VoiceLogErrorOnce(m_aServerAddrErrorLog, sizeof(m_aServerAddrErrorLog), aError);
}

bool CRClientVoice::UpdateContext()
{
	const uint32_t Old = m_ContextHash.load();
	if(!m_pClient || m_pClient->State() != IClient::STATE_ONLINE)
	{
		m_ContextHash.store(0);
		return Old != 0;
	}
	char aAddr[NETADDR_MAXSTRSIZE];
	net_addr_str(&m_pClient->ServerAddress(), aAddr, sizeof(aAddr), true);
	const uint32_t NewHash = str_quickhash(aAddr);
	m_ContextHash.store(NewHash);
	return NewHash != Old;
}

void CRClientVoice::UpdateClientSnapshot()
{
	std::lock_guard<std::mutex> Guard(m_SnapshotMutex);
	if(!m_pClient || !m_pGameClient || m_pClient->State() != IClient::STATE_ONLINE)
	{
		m_OnlineSnap = false;
		m_LocalClientIdSnap = -1;
		m_SpecActiveSnap = false;
		m_SpecPosSnap = vec2(0.0f, 0.0f);
		return;
	}

	m_OnlineSnap = true;
	m_LocalClientIdSnap = m_pGameClient->m_Snap.m_LocalClientId;
	m_SpecActiveSnap = m_pGameClient->m_Snap.m_SpecInfo.m_Active;
	if(m_SpecActiveSnap)
		m_SpecPosSnap = m_pGameClient->m_Camera.m_Center;
	if(m_LocalClientIdSnap < 0 || m_LocalClientIdSnap >= MAX_CLIENTS)
	{
		m_OnlineSnap = false;
		m_LocalClientIdSnap = -1;
		m_SpecActiveSnap = false;
		m_SpecPosSnap = vec2(0.0f, 0.0f);
		return;
	}
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		m_aClientPosSnap[i] = m_pGameClient->m_aClients[i].m_RenderPos;
		str_copy(m_aClientNameSnap[i].data(), m_pGameClient->m_aClients[i].m_aName, MAX_NAME_LENGTH);
		m_aClientOtherTeamSnap[i] = m_pGameClient->m_Teams.Team(i) != m_pGameClient->m_Teams.Team(m_LocalClientIdSnap);
		m_aClientActiveSnap[i] = m_pGameClient->m_Snap.m_aCharacters[i].m_Active;
		m_aClientSpecSnap[i] = m_pGameClient->m_aClients[i].m_Spec;
	}
}

void CRClientVoice::ProcessCapture()
{
	if(!m_CaptureDevice || !m_pEncoder || !m_ServerAddrValid.load() || !m_Socket)
		return;

	SRClientVoiceConfigSnapshot Config;
	GetConfigSnapshot(Config);
	const int TestMode = std::clamp(Config.m_RiVoiceTestMode, 0, 2);
	const bool TestLocal = TestMode == 1;
	const bool ShowMicLevel = TestMode != 0;
	const bool MicMuted = Config.m_RiVoiceMicMute != 0;
	const float TestGain = std::clamp(Config.m_RiVoiceVolume / 100.0f, 0.0f, 4.0f);

	int LocalClientId = -1;
	vec2 LocalPos = vec2(0.0f, 0.0f);
	bool Online = false;
	{
		std::lock_guard<std::mutex> Guard(m_SnapshotMutex);
		Online = m_OnlineSnap;
		LocalClientId = m_LocalClientIdSnap;
		if(LocalClientId >= 0 && LocalClientId < MAX_CLIENTS)
			LocalPos = m_aClientPosSnap[LocalClientId];
	}
	if(!Online || LocalClientId < 0 || LocalClientId >= MAX_CLIENTS)
		return;

	const int64_t Now = time_get();
	const bool UseVad = Config.m_RiVoiceVadEnable != 0;
	if(!UseVad)
	{
		m_VadActive = false;
		m_VadReleaseDeadline = 0;
	}
	int64_t ReleaseDeadline = 0;
	bool PttHeld = false;
	if(!UseVad)
	{
		ReleaseDeadline = m_PttReleaseDeadline.load();
		PttHeld = m_PttActive.load() || (ReleaseDeadline != 0 && Now < ReleaseDeadline);
	}
	else if(m_VadActive && m_VadReleaseDeadline != 0 && Now >= m_VadReleaseDeadline)
	{
		m_VadActive = false;
		m_VadReleaseDeadline = 0;
	}
	const bool TokenChanged = Config.m_RiVoiceTokenHash != m_LastTokenHashSent;
	const bool NeedKeepalive = m_LastKeepalive == 0 || Now - m_LastKeepalive > time_freq() * 2;
	const bool TxActiveSnapshot = UseVad ? m_VadActive : PttHeld;
	const uint8_t ProtocolVersion = VoiceProtocolVersion(Config);
	uint8_t TxFlags = UseVad ? VOICE_FLAG_VAD : 0;
	if(TestMode == 2)
		TxFlags |= VOICE_FLAG_LOOPBACK;

	if(TokenChanged || (!TxActiveSnapshot && NeedKeepalive))
	{
		NETADDR ServerAddrLocal = NETADDR_ZEROED;
		{
			std::lock_guard<std::mutex> Guard(m_ServerAddrMutex);
			ServerAddrLocal = m_ServerAddr;
		}
		uint8_t aPacket[VOICE_MAX_PACKET];
		size_t Offset = 0;
		mem_copy(aPacket + Offset, VOICE_MAGIC, sizeof(VOICE_MAGIC));
		Offset += sizeof(VOICE_MAGIC);
		aPacket[Offset++] = ProtocolVersion;
		aPacket[Offset++] = VOICE_TYPE_PING;
		WriteU16(aPacket + Offset, 0);
		Offset += sizeof(uint16_t);
		WriteU32(aPacket + Offset, m_ContextHash.load());
		Offset += sizeof(uint32_t);
		WriteU32(aPacket + Offset, Config.m_RiVoiceTokenHash);
		Offset += sizeof(uint32_t);
		aPacket[Offset++] = TxFlags;
		WriteU16(aPacket + Offset, 0);
		Offset += sizeof(uint16_t);
		WriteU16(aPacket + Offset, m_Sequence);
		Offset += sizeof(uint16_t);
		WriteFloat(aPacket + Offset, 0.0f);
		Offset += sizeof(float);
		WriteFloat(aPacket + Offset, 0.0f);
		Offset += sizeof(float);
		net_udp_send(m_Socket, &ServerAddrLocal, aPacket, (int)Offset);
		m_LastPingSentTime = Now;
		m_LastPingSeq = m_Sequence;
		m_LastKeepalive = Now;
		m_LastTokenHashSent = Config.m_RiVoiceTokenHash;
	}

	if(MicMuted)
	{
		UpdateMicLevel(0.0f);
		m_VadActive = false;
		m_VadReleaseDeadline = 0;
		m_PttReleaseDeadline.store(0);
		m_TxWasActive = false;
		SDL_ClearQueuedAudio(m_CaptureDevice);
		return;
	}

	if(!UseVad && !PttHeld)
	{
		if(ShowMicLevel)
		{
			bool UpdatedMicLevel = false;
			while(SDL_GetQueuedAudioSize(m_CaptureDevice) >= VOICE_FRAME_BYTES)
			{
				int16_t aPcm[VOICE_FRAME_SAMPLES];
				SDL_DequeueAudio(m_CaptureDevice, aPcm, VOICE_FRAME_BYTES);
				ApplyMicGain(Config, aPcm, VOICE_FRAME_SAMPLES);
				ApplyNoiseSuppressor(Config, aPcm, VOICE_FRAME_SAMPLES, m_NsNoiseFloor, m_NsGain, m_pNoiseSuppress);
				ApplyHpfCompressor(Config, aPcm, VOICE_FRAME_SAMPLES, m_HpfPrevIn, m_HpfPrevOut, m_CompEnv);
				const float Peak = VoiceFramePeak(aPcm, VOICE_FRAME_SAMPLES);
				UpdateMicLevel(Peak);
				UpdatedMicLevel = true;
			}
			if(!UpdatedMicLevel)
				UpdateMicLevel(-1.0f);
		}
		else
		{
			UpdateMicLevel(0.0f);
		}
		if(ReleaseDeadline != 0 && Now >= ReleaseDeadline)
			m_PttReleaseDeadline.store(0);
		m_TxWasActive = false;
		SDL_ClearQueuedAudio(m_CaptureDevice);
		return;
	}



	const int ClientId = LocalClientId;
	const vec2 Pos = LocalPos;
	const float VadThreshold = std::clamp(Config.m_RiVoiceVadThreshold / 100.0f, 0.0f, 1.0f);
	const int VadReleaseMs = std::clamp(Config.m_RiVoiceVadReleaseDelayMs, 0, 1000);
	const int64_t VadReleaseTicks = (int64_t)time_freq() * VadReleaseMs / 1000;
	if(!TxActiveSnapshot)
		m_TxWasActive = false;

	uint8_t aPacket[VOICE_MAX_PACKET];
	uint8_t aPayload[VOICE_MAX_PAYLOAD];

	bool UpdatedMicLevel = false;
	while(SDL_GetQueuedAudioSize(m_CaptureDevice) >= VOICE_FRAME_BYTES)
	{
		int16_t aPcm[VOICE_FRAME_SAMPLES];
		SDL_DequeueAudio(m_CaptureDevice, aPcm, VOICE_FRAME_BYTES);
		ApplyMicGain(Config, aPcm, VOICE_FRAME_SAMPLES);
		ApplyNoiseSuppressor(Config, aPcm, VOICE_FRAME_SAMPLES, m_NsNoiseFloor, m_NsGain, m_pNoiseSuppress);
		ApplyHpfCompressor(Config, aPcm, VOICE_FRAME_SAMPLES, m_HpfPrevIn, m_HpfPrevOut, m_CompEnv);

		const float Peak = VoiceFramePeak(aPcm, VOICE_FRAME_SAMPLES);
		if(ShowMicLevel)
		{
			UpdateMicLevel(Peak);
			UpdatedMicLevel = true;
		}

		if(UseVad)
		{
			const bool Trigger = VadThreshold <= 0.0f || Peak >= VadThreshold;
			const int64_t FrameNow = time_get();
			if(Trigger)
			{
				m_VadActive = true;
				if(VadReleaseTicks > 0)
					m_VadReleaseDeadline = FrameNow + VadReleaseTicks;
				else
					m_VadReleaseDeadline = 0;
			}
			else if(m_VadActive)
			{
				if(m_VadReleaseDeadline == 0 || FrameNow >= m_VadReleaseDeadline)
				{
					m_VadActive = false;
					m_VadReleaseDeadline = 0;
				}
			}
		}

		const bool TxActive = UseVad ? m_VadActive : PttHeld;
		if(!TxActive)
		{
			m_TxWasActive = false;
			continue;
		}
		if(!m_TxWasActive)
		{
			if(m_pEncoder)
				opus_encoder_ctl(m_pEncoder, OPUS_RESET_STATE);
			m_HpfPrevIn = 0.0f;
			m_HpfPrevOut = 0.0f;
			m_CompEnv = 0.0f;
			m_Sequence += 1000;
			m_TxWasActive = true;
		}

		if(TestLocal)
		{
			if(m_OutputDevice && TestGain > 0.0f)
			{
				SDL_LockAudioDevice(m_OutputDevice);
				PushPeerFrame(LocalClientId, aPcm, VOICE_FRAME_SAMPLES, TestGain, TestGain);
				SDL_UnlockAudioDevice(m_OutputDevice);
			}
			continue;
		}

		const int EncSize = opus_encode(m_pEncoder, aPcm, VOICE_FRAME_SAMPLES, aPayload, (int)sizeof(aPayload));
		if(EncSize <= 0)
			continue;
		if(EncSize > VOICE_MAX_PAYLOAD)
			continue;

		size_t Offset = 0;
		mem_copy(aPacket + Offset, VOICE_MAGIC, sizeof(VOICE_MAGIC));
		Offset += sizeof(VOICE_MAGIC);
		aPacket[Offset++] = ProtocolVersion;
		aPacket[Offset++] = VOICE_TYPE_AUDIO;
		WriteU16(aPacket + Offset, (uint16_t)EncSize);
		Offset += sizeof(uint16_t);
		WriteU32(aPacket + Offset, m_ContextHash.load());
		Offset += sizeof(uint32_t);
		WriteU32(aPacket + Offset, Config.m_RiVoiceTokenHash);
		Offset += sizeof(uint32_t);
		aPacket[Offset++] = TxFlags;
		WriteU16(aPacket + Offset, (uint16_t)ClientId);
		Offset += sizeof(uint16_t);
		WriteU16(aPacket + Offset, m_Sequence++);
		Offset += sizeof(uint16_t);
		WriteFloat(aPacket + Offset, Pos.x);
		Offset += sizeof(float);
		WriteFloat(aPacket + Offset, Pos.y);
		Offset += sizeof(float);
		mem_copy(aPacket + Offset, aPayload, EncSize);
		Offset += EncSize;

		NETADDR ServerAddrLocal = NETADDR_ZEROED;
		{
			std::lock_guard<std::mutex> Guard(m_ServerAddrMutex);
			ServerAddrLocal = m_ServerAddr;
		}
		net_udp_send(m_Socket, &ServerAddrLocal, aPacket, (int)Offset);
		m_aLastHeard[ClientId].store(Now);
		if(Config.m_RiVoiceDebug)
		{
			m_TxPackets++;
			if(Now - m_TxLastLog > time_freq())
			{
				log_info("voice", "tx packets=%d ctx=0x%08x", m_TxPackets, m_ContextHash.load());
				m_TxLastLog = Now;
				m_TxPackets = 0;
			}
		}
	}

	if(ShowMicLevel)
	{
		if(!UpdatedMicLevel)
			UpdateMicLevel(-1.0f);
	}
	else
	{
		UpdateMicLevel(0.0f);
	}
}

void CRClientVoice::ProcessIncoming()
{
	if(!m_OutputDevice || !m_Socket)
		return;

	SRClientVoiceConfigSnapshot Config;
	GetConfigSnapshot(Config);
	const int TestMode = std::clamp(Config.m_RiVoiceTestMode, 0, 2);
	const bool TestServer = TestMode == 2;
	const uint8_t ProtocolVersion = VoiceProtocolVersion(Config);



	while(net_socket_read_wait(m_Socket, std::chrono::nanoseconds(0)) > 0)
	{
		NETADDR Addr;
		unsigned char *pData = nullptr;
		int Bytes = net_udp_recv(m_Socket, &Addr, &pData);
		if(Bytes <= 0 || !pData)
			break;

		NETADDR ServerAddrLocal = NETADDR_ZEROED;
		{
			std::lock_guard<std::mutex> Guard(m_ServerAddrMutex);
			ServerAddrLocal = m_ServerAddr;
		}
		if(net_addr_comp(&Addr, &ServerAddrLocal) != 0)
			continue;

		if(Bytes < VOICE_HEADER_SIZE)
			continue;

		size_t Offset = 0;
		if(mem_comp(pData, VOICE_MAGIC, sizeof(VOICE_MAGIC)) != 0)
			continue;
		Offset += sizeof(VOICE_MAGIC);

		const uint8_t Version = pData[Offset++];
		const uint8_t Type = pData[Offset++];
		if(Version != ProtocolVersion)
			continue;
		if(Type != VOICE_TYPE_AUDIO && Type != VOICE_TYPE_PING && Type != VOICE_TYPE_PONG)
			continue;
		if(Bytes < VOICE_HEADER_SIZE)
			continue;

		const uint16_t PayloadSize = ReadU16(pData + Offset);
		Offset += sizeof(uint16_t);
		const uint32_t ContextHash = ReadU32(pData + Offset);
		Offset += sizeof(uint32_t);
		const uint32_t TokenHash = ReadU32(pData + Offset);
		Offset += sizeof(uint32_t);
		const uint8_t Flags = pData[Offset++];
		const uint16_t SenderId = ReadU16(pData + Offset);
		Offset += sizeof(uint16_t);
		const uint16_t Sequence = ReadU16(pData + Offset);
		Offset += sizeof(uint16_t);
		const float PosX = SanitizeFloat(ReadFloat(pData + Offset));
		Offset += sizeof(float);
		const float PosY = SanitizeFloat(ReadFloat(pData + Offset));
		Offset += sizeof(float);

		const uint32_t LocalContextHash = m_ContextHash.load();
		if(ContextHash == 0 || ContextHash != LocalContextHash)
		{
			m_RxDropContext++;
			continue;
		}
		if(Type == VOICE_TYPE_PING || Type == VOICE_TYPE_PONG)
		{
			if(TokenHash != 0 && TokenHash != Config.m_RiVoiceTokenHash)
				continue;
			if(m_LastPingSentTime != 0 && Sequence == m_LastPingSeq)
			{
				const int64_t Now = time_get();
				const int RttMs = (int)std::clamp((Now - m_LastPingSentTime) * 1000 / time_freq(), (int64_t)0, (int64_t)9999);
				m_PingMs.store(RttMs);
			}
			continue;
		}
		if(Type != VOICE_TYPE_AUDIO)
			continue;
		const uint32_t LocalToken = Config.m_RiVoiceTokenHash;
		const uint32_t LocalGroup = VoiceTokenGroup(LocalToken);
		const uint32_t LocalMode = VoiceTokenMode(LocalToken);
		const uint32_t SenderGroup = VoiceTokenGroup(TokenHash);
		const uint32_t SenderMode = VoiceTokenMode(TokenHash);
		if(!VoiceShouldHear(SenderGroup, SenderMode, LocalGroup, LocalMode))
			continue;
		if(SenderId >= MAX_CLIENTS)
			continue;

		int LocalId = -1;
		vec2 LocalPos = vec2(0.0f, 0.0f);
		bool SpecActive = false;
		vec2 SpecPos = vec2(0.0f, 0.0f);
		char aSenderName[MAX_NAME_LENGTH];
		bool SenderOtherTeam = false;
		bool SenderActive = false;
		bool SenderSpec = false;
		{
			std::lock_guard<std::mutex> Guard(m_SnapshotMutex);
			if(!m_OnlineSnap)
				continue;
			LocalId = m_LocalClientIdSnap;
			if(LocalId < 0 || LocalId >= MAX_CLIENTS)
				continue;
			LocalPos = m_aClientPosSnap[LocalId];
			SpecActive = m_SpecActiveSnap;
			SpecPos = m_SpecPosSnap;
			str_copy(aSenderName, m_aClientNameSnap[SenderId].data(), sizeof(aSenderName));
			SenderOtherTeam = m_aClientOtherTeamSnap[SenderId] != 0;
			SenderActive = m_aClientActiveSnap[SenderId] != 0;
			SenderSpec = m_aClientSpecSnap[SenderId] != 0;
		}

		if(SpecActive && Config.m_RiVoiceHearOnSpecPos)
			LocalPos = SpecPos;

		const bool IsSelf = SenderId == LocalId;
		if(IsSelf && !TestServer)
			continue;

		const bool SameGroup = LocalGroup != 0 && SenderGroup == LocalGroup;
		const bool IgnoreDistance = Config.m_RiVoiceIgnoreDistance || (Config.m_RiVoiceGroupGlobal && SameGroup);
		const char *pSenderName = aSenderName;
		if(!IsSelf)
		{
			const bool AllowObserver = Config.m_RiVoiceHearPeoplesInSpectate && !SenderActive && !SenderSpec;
			if(Config.m_RiVoiceVisibilityMode == 0)
			{
				if(!IgnoreDistance && !SenderActive && !AllowObserver)
					continue;
			}
			else if(Config.m_RiVoiceVisibilityMode == 1)
			{
				if(SenderOtherTeam && !AllowObserver)
					continue;
			}

			if(VoiceListMatch(Config.m_aRiVoiceMute, pSenderName))
				continue;
			if(Config.m_RiVoiceListMode == 1 && !VoiceListMatch(Config.m_aRiVoiceWhitelist, pSenderName))
				continue;
			if(Config.m_RiVoiceListMode == 2 && VoiceListMatch(Config.m_aRiVoiceBlacklist, pSenderName))
				continue;
			const bool SenderUsesVad = (Flags & VOICE_FLAG_VAD) != 0;
			if(SenderUsesVad && !Config.m_RiVoiceHearVad && !VoiceListMatch(Config.m_aRiVoiceVadAllow, pSenderName))
				continue;
		}
		m_aLastHeard[SenderId].store(time_get());

		if(PayloadSize > (uint16_t)(VOICE_MAX_PACKET - VOICE_HEADER_SIZE))
			continue;
		if(Offset + PayloadSize > (size_t)Bytes)
			continue;

		const vec2 SenderPos = vec2(PosX, PosY);
		const float Radius = std::max(1, Config.m_RiVoiceRadius) * 32.0f;
		const float Dist = distance(LocalPos, SenderPos);
		if(!IgnoreDistance && Dist > Radius)
		{
			m_RxDropRadius++;
			continue;
		}

		const float RadiusFactor = IgnoreDistance ? 1.0f : (1.0f - (Dist / Radius));
		float Volume = std::clamp(RadiusFactor * (Config.m_RiVoiceVolume / 100.0f), 0.0f, 4.0f);
		if(Volume <= 0.0f)
			continue;

		int NameVolume = 100;
		if(VoiceNameVolume(Config.m_aRiVoiceNameVolumes, pSenderName, NameVolume))
		{
			Volume *= (NameVolume / 100.0f);
			if(Volume <= 0.0f)
				continue;
		}

		const bool StereoEnabled = Config.m_RiVoiceStereo != 0;
		const float StereoWidth = std::clamp(Config.m_RiVoiceStereoWidth / 100.0f, 0.0f, 2.0f);
		const float Pan = StereoEnabled ? std::clamp(((SenderPos.x - LocalPos.x) / Radius) * StereoWidth, -1.0f, 1.0f) : 0.0f;
		const float LeftGain = Volume * (Pan <= 0.0f ? 1.0f : (1.0f - Pan));
		const float RightGain = Volume * (Pan >= 0.0f ? 1.0f : (1.0f + Pan));

		SVoicePeer &Peer = m_aPeers[SenderId];
		const int64_t Now = time_get();
		bool ResetStream = false;
		if(Peer.m_LastRecvTime != 0)
		{
			const int64_t Gap = Now - Peer.m_LastRecvTime;
			if(Gap > time_freq() * 2)
				ResetStream = true;
			else if(Peer.m_HasLastRecvSeq)
			{
				const int Delta = SeqDelta(Sequence, Peer.m_LastRecvSeq);
				if(Delta > SVoicePeer::MAX_JITTER_PACKETS * 8)
					ResetStream = true;
			}
		}
		if(ResetStream)
			ResetPeer(Peer);

		if(Peer.m_LastRecvTime != 0)
		{
			const float DeltaMs = (float)((Now - Peer.m_LastRecvTime) * 1000.0 / (double)time_freq());
			const float Deviation = std::fabs(DeltaMs - 20.0f);
			Peer.m_JitterMs = 0.9f * Peer.m_JitterMs + 0.1f * Deviation;
		}
		Peer.m_LastRecvTime = Now;

		int Target = ClampJitterTarget(Peer.m_JitterMs);
		if(Peer.m_HasLastRecvSeq)
		{
			const uint16_t Expected = (uint16_t)(Peer.m_LastRecvSeq + 1);
			if(Sequence != Expected)
				Target = std::min(Target + 1, 6);
		}
		Peer.m_TargetFrames = Target;
		if(Peer.m_HasLastRecvSeq)
		{
			const int Delta = SeqDelta(Sequence, Peer.m_LastRecvSeq);
			if(Delta > 0 && Delta < 1000)
			{
				const int Lost = std::max(0, Delta - 1);
				const float LossRatio = std::clamp(Lost / (float)Delta, 0.0f, 1.0f);
				Peer.m_LossEwma = 0.9f * Peer.m_LossEwma + 0.1f * LossRatio;
			}
		}
		if(!Peer.m_HasLastRecvSeq || SeqLess(Peer.m_LastRecvSeq, Sequence))
			Peer.m_LastRecvSeq = Sequence;
		Peer.m_HasLastRecvSeq = true;
		Peer.m_LastGainLeft = LeftGain;
		Peer.m_LastGainRight = RightGain;

		const int Slot = Sequence % SVoicePeer::MAX_JITTER_PACKETS;
		SVoicePeer::SJitterPacket &Pkt = Peer.m_aPackets[Slot];
		if(Pkt.m_Valid && Pkt.m_Seq != Sequence)
			Peer.m_QueuedPackets = std::max(0, Peer.m_QueuedPackets - 1);
		if(!Pkt.m_Valid || Pkt.m_Seq != Sequence)
			Peer.m_QueuedPackets = std::min(Peer.m_QueuedPackets + 1, SVoicePeer::MAX_JITTER_PACKETS);
		Pkt.m_Valid = true;
		Pkt.m_Seq = Sequence;
		Pkt.m_Size = PayloadSize;
		Pkt.m_LeftGain = LeftGain;
		Pkt.m_RightGain = RightGain;
		mem_copy(Pkt.m_aData, pData + Offset, PayloadSize);

		if(Config.m_RiVoiceDebug)
		{
			m_RxPackets++;
			if(Now - m_RxLastLog > time_freq())
			{
				log_info("voice", "rx packets=%d drop_ctx=%d drop_radius=%d", m_RxPackets, m_RxDropContext, m_RxDropRadius);
				m_RxLastLog = Now;
				m_RxPackets = 0;
				m_RxDropContext = 0;
				m_RxDropRadius = 0;
			}
		}
	}
}

void CRClientVoice::UpdateConfigSnapshot()
{
	std::lock_guard<std::mutex> Guard(m_ConfigMutex);
	m_ConfigSnapshot.m_RiVoiceFilterEnable = g_Config.m_RiVoiceFilterEnable;
	m_ConfigSnapshot.m_RiVoiceProtocolVersion = g_Config.m_RiVoiceProtocolVersion;
	m_ConfigSnapshot.m_RiVoiceNoiseSuppressEnable = g_Config.m_RiVoiceNoiseSuppressEnable;
	m_ConfigSnapshot.m_RiVoiceNoiseSuppressStrength = g_Config.m_RiVoiceNoiseSuppressStrength;
	m_ConfigSnapshot.m_RiVoiceCompThreshold = g_Config.m_RiVoiceCompThreshold;
	m_ConfigSnapshot.m_RiVoiceCompRatio = g_Config.m_RiVoiceCompRatio;
	m_ConfigSnapshot.m_RiVoiceCompAttackMs = g_Config.m_RiVoiceCompAttackMs;
	m_ConfigSnapshot.m_RiVoiceCompReleaseMs = g_Config.m_RiVoiceCompReleaseMs;
	m_ConfigSnapshot.m_RiVoiceCompMakeup = g_Config.m_RiVoiceCompMakeup;
	m_ConfigSnapshot.m_RiVoiceLimiter = g_Config.m_RiVoiceLimiter;
	m_ConfigSnapshot.m_RiVoiceStereo = g_Config.m_RiVoiceStereo;
	m_ConfigSnapshot.m_RiVoiceStereoWidth = g_Config.m_RiVoiceStereoWidth;
	m_ConfigSnapshot.m_RiVoiceRadius = g_Config.m_RiVoiceRadius;
	m_ConfigSnapshot.m_RiVoiceVolume = g_Config.m_RiVoiceVolume;
	m_ConfigSnapshot.m_RiVoiceMicVolume = g_Config.m_RiVoiceMicVolume;
	m_ConfigSnapshot.m_RiVoiceMicMute = g_Config.m_RiVoiceMicMute;
	m_ConfigSnapshot.m_RiVoiceTestMode = g_Config.m_RiVoiceTestMode;
	m_ConfigSnapshot.m_RiVoiceVadEnable = g_Config.m_RiVoiceVadEnable;
	m_ConfigSnapshot.m_RiVoiceVadThreshold = g_Config.m_RiVoiceVadThreshold;
	m_ConfigSnapshot.m_RiVoiceVadReleaseDelayMs = g_Config.m_RiVoiceVadReleaseDelayMs;
	m_ConfigSnapshot.m_RiVoiceIgnoreDistance = g_Config.m_RiVoiceIgnoreDistance;
	m_ConfigSnapshot.m_RiVoiceGroupGlobal = g_Config.m_RiVoiceGroupGlobal;
	m_ConfigSnapshot.m_RiVoiceVisibilityMode = g_Config.m_RiVoiceVisibilityMode;
	m_ConfigSnapshot.m_RiVoiceListMode = g_Config.m_RiVoiceListMode;
	m_ConfigSnapshot.m_RiVoiceDebug = g_Config.m_RiVoiceDebug;
	m_ConfigSnapshot.m_RiVoiceGroupMode = g_Config.m_RiVoiceGroupMode;
	m_ConfigSnapshot.m_RiVoiceHearOnSpecPos = g_Config.m_RiVoiceHearOnSpecPos;
	m_ConfigSnapshot.m_RiVoiceHearPeoplesInSpectate = g_Config.m_RiVoiceHearPeoplesInSpectate;
	m_ConfigSnapshot.m_RiVoiceHearVad = g_Config.m_RiVoiceHearVad;
	m_ConfigSnapshot.m_ClShowOthers = g_Config.m_ClShowOthers;
	uint32_t GroupHash = g_Config.m_RiVoiceToken[0] != '\0' ? str_quickhash(g_Config.m_RiVoiceToken) : 0;
	uint32_t Mode = (uint32_t)std::clamp(g_Config.m_RiVoiceGroupMode, 0, 3);
	m_ConfigSnapshot.m_RiVoiceTokenHash = VoicePackToken(GroupHash, Mode);
	str_copy(m_ConfigSnapshot.m_aRiVoiceWhitelist, g_Config.m_RiVoiceWhitelist, sizeof(m_ConfigSnapshot.m_aRiVoiceWhitelist));
	str_copy(m_ConfigSnapshot.m_aRiVoiceBlacklist, g_Config.m_RiVoiceBlacklist, sizeof(m_ConfigSnapshot.m_aRiVoiceBlacklist));
	str_copy(m_ConfigSnapshot.m_aRiVoiceMute, g_Config.m_RiVoiceMute, sizeof(m_ConfigSnapshot.m_aRiVoiceMute));
	str_copy(m_ConfigSnapshot.m_aRiVoiceVadAllow, g_Config.m_RiVoiceVadAllow, sizeof(m_ConfigSnapshot.m_aRiVoiceVadAllow));
	str_copy(m_ConfigSnapshot.m_aRiVoiceNameVolumes, g_Config.m_RiVoiceNameVolumes, sizeof(m_ConfigSnapshot.m_aRiVoiceNameVolumes));
}

void CRClientVoice::GetConfigSnapshot(SRClientVoiceConfigSnapshot &Out) const
{
	std::lock_guard<std::mutex> Guard(m_ConfigMutex);
	Out = m_ConfigSnapshot;
}

void CRClientVoice::UpdateEncoderParams()
{
	if(!m_pEncoder)
		return;

	const int64_t Now = time_get();
	if(m_LastEncUpdate != 0 && Now - m_LastEncUpdate < time_freq())
		return;

	float LossAvg = 0.0f;
	float JitterMax = 0.0f;
	int Count = 0;
	for(const auto &Peer : m_aPeers)
	{
		if(Peer.m_LastRecvTime == 0)
			continue;
		if(Now - Peer.m_LastRecvTime > time_freq() * 5)
			continue;
		LossAvg += Peer.m_LossEwma;
		JitterMax = std::max(JitterMax, Peer.m_JitterMs);
		Count++;
	}
	if(Count > 0)
		LossAvg /= (float)Count;

	const int LossPerc = (int)std::clamp(LossAvg * 100.0f, 0.0f, 30.0f);

	int TargetBitrate = 24000;
	int TargetLoss = 0;
	bool TargetFec = false;

	if(LossPerc <= 2 && JitterMax < 8.0f)
	{
		TargetBitrate = 32000;
		TargetLoss = 0;
		TargetFec = false;
	}
	else if(LossPerc <= 5)
	{
		TargetBitrate = 24000;
		TargetLoss = 5;
		TargetFec = true;
	}
	else if(LossPerc <= 10)
	{
		TargetBitrate = 20000;
		TargetLoss = 10;
		TargetFec = true;
	}
	else
	{
		TargetBitrate = 16000;
		TargetLoss = 20;
		TargetFec = true;
	}

	if(TargetBitrate != m_EncBitrate)
	{
		opus_encoder_ctl(m_pEncoder, OPUS_SET_BITRATE(TargetBitrate));
		m_EncBitrate = TargetBitrate;
	}
	if(TargetLoss != m_EncLossPerc)
	{
		opus_encoder_ctl(m_pEncoder, OPUS_SET_PACKET_LOSS_PERC(TargetLoss));
		m_EncLossPerc = TargetLoss;
	}
	if(TargetFec != m_EncFec)
	{
		opus_encoder_ctl(m_pEncoder, OPUS_SET_INBAND_FEC(TargetFec ? 1 : 0));
		m_EncFec = TargetFec;
	}

	m_LastEncUpdate = Now;
}

void CRClientVoice::DecodeJitter()
{
	if(!m_OutputDevice)
		return;

	for(int PeerId = 0; PeerId < MAX_CLIENTS; PeerId++)
	{
		SVoicePeer &Peer = m_aPeers[PeerId];
		if(Peer.m_QueuedPackets <= 0)
			continue;

		if(!Peer.m_HasNextSeq)
		{
			if(Peer.m_QueuedPackets < Peer.m_TargetFrames)
				continue;
			bool Found = false;
			uint16_t StartSeq = 0;
			for(const auto &Pkt : Peer.m_aPackets)
			{
				if(!Pkt.m_Valid)
					continue;
				if(!Found)
				{
					StartSeq = Pkt.m_Seq;
					Found = true;
					continue;
				}
				if(SeqLess(Pkt.m_Seq, StartSeq))
					StartSeq = Pkt.m_Seq;
			}
			if(!Found)
				continue;
			Peer.m_NextSeq = StartSeq;
			Peer.m_HasNextSeq = true;
		}

		int FrameCount = 0;
		SDL_LockAudioDevice(m_OutputDevice);
		FrameCount = Peer.m_FrameCount;
		SDL_UnlockAudioDevice(m_OutputDevice);
		if(FrameCount >= SVoicePeer::MAX_FRAMES)
			continue;

		const int Slot = Peer.m_NextSeq % SVoicePeer::MAX_JITTER_PACKETS;
		SVoicePeer::SJitterPacket *pPkt = nullptr;
		if(Peer.m_aPackets[Slot].m_Valid && Peer.m_aPackets[Slot].m_Seq == Peer.m_NextSeq)
			pPkt = &Peer.m_aPackets[Slot];
		const int NextSlot = (uint16_t)(Peer.m_NextSeq + 1) % SVoicePeer::MAX_JITTER_PACKETS;
		SVoicePeer::SJitterPacket *pNextPkt = nullptr;
		if(Peer.m_aPackets[NextSlot].m_Valid && Peer.m_aPackets[NextSlot].m_Seq == (uint16_t)(Peer.m_NextSeq + 1))
			pNextPkt = &Peer.m_aPackets[NextSlot];

		if(!Peer.m_pDecoder)
		{
			int Error = 0;
			Peer.m_pDecoder = opus_decoder_create(VOICE_SAMPLE_RATE, VOICE_CHANNELS, &Error);
			if(!Peer.m_pDecoder || Error != OPUS_OK)
			{
				char aError[256];
				str_format(aError, sizeof(aError), "Failed to create Opus decoder: %d", Error);
				VoiceLogErrorOnce(m_aDecoderErrorLog, sizeof(m_aDecoderErrorLog), aError);
				continue;
			}
			m_aDecoderErrorLog[0] = '\0';
			Peer.m_HasSeq = false;
		}

		int16_t aPcm[VOICE_FRAME_SAMPLES];
		int Samples = 0;
		float LeftGain = Peer.m_LastGainLeft;
		float RightGain = Peer.m_LastGainRight;
		if(pPkt)
		{
			Samples = opus_decode(Peer.m_pDecoder, pPkt->m_aData, pPkt->m_Size, aPcm, VOICE_FRAME_SAMPLES, 0);
			if(Samples > 0)
			{
				LeftGain = pPkt->m_LeftGain;
				RightGain = pPkt->m_RightGain;
			}
			pPkt->m_Valid = false;
			Peer.m_QueuedPackets = std::max(0, Peer.m_QueuedPackets - 1);
		}
		else if(pNextPkt && Peer.m_LossEwma > 0.02f)
		{
			Samples = opus_decode(Peer.m_pDecoder, pNextPkt->m_aData, pNextPkt->m_Size, aPcm, VOICE_FRAME_SAMPLES, 1);
		}
		else if(Peer.m_HasSeq)
		{
			Samples = opus_decode(Peer.m_pDecoder, nullptr, 0, aPcm, VOICE_FRAME_SAMPLES, 1);
		}

		if(Samples > 0)
		{
			SDL_LockAudioDevice(m_OutputDevice);
			PushPeerFrame(PeerId, aPcm, Samples, LeftGain, RightGain);
			SDL_UnlockAudioDevice(m_OutputDevice);
		}

		Peer.m_LastSeq = Peer.m_NextSeq;
		Peer.m_HasSeq = true;
		Peer.m_NextSeq = (uint16_t)(Peer.m_NextSeq + 1);
	}
}

bool CRClientVoice::IsVoiceActive(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	const int64_t LastHeard = m_aLastHeard[ClientId].load();
	if(LastHeard == 0)
		return false;
	constexpr int64_t ACTIVE_WINDOW_MS = 40;
	return time_get() - LastHeard < (int64_t)time_freq() * ACTIVE_WINDOW_MS / 1000;
}

void CRClientVoice::StartWorker()
{
	if(m_Worker.joinable())
		return;
	m_WorkerStop.store(false);
	m_WorkerEnabled.store(true);
	m_Worker = std::thread(&CRClientVoice::WorkerLoop, this);
}

void CRClientVoice::StopWorker()
{
	m_WorkerEnabled.store(false);
	if(m_Worker.joinable())
	{
		m_WorkerStop.store(true);
		m_Worker.join();
	}
	m_WorkerStop.store(false);
}

void CRClientVoice::WorkerLoop()
{
	using namespace std::chrono_literals;
	while(!m_WorkerStop.load())
	{
		if(!m_WorkerEnabled.load())
		{
			std::this_thread::sleep_for(10ms);
			continue;
		}

		bool ShouldEnsureAudio = m_AudioRefreshRequested.exchange(false);
		bool CaptureNeedsRetry = m_CaptureUnavailable;
#if defined(CONF_PLATFORM_ANDROID)
		if(m_AndroidRecordPermissionKnown && !m_AndroidRecordPermissionGranted)
			CaptureNeedsRetry = false;
#endif
		if(!ShouldEnsureAudio && (CaptureNeedsRetry || m_OutputUnavailable))
		{
			const int64_t RetryInterval = time_freq();
			const int64_t Now = time_get();
			if(m_LastAudioRetryAttempt == 0 || Now - m_LastAudioRetryAttempt >= RetryInterval)
				ShouldEnsureAudio = true;
		}
		if(ShouldEnsureAudio)
			EnsureAudio();

		ResolveServerAddr();
		ProcessIncoming();
		DecodeJitter();
		UpdateEncoderParams();
		ProcessCapture();

		std::this_thread::sleep_for(2ms);
	}
}

void CRClientVoice::OnRender()
{
	if(!g_Config.m_RiVoiceEnable || !m_pGameClient || !m_pClient)
	{
		Shutdown();
		return;
	}
	m_ShutdownDone = false;

#if defined(CONF_PLATFORM_EMSCRIPTEN)
	if(!m_UnsupportedPlatformLogged)
	{
		log_info("voice", "voice runtime is unavailable on emscripten, skipping voice initialization");
		m_UnsupportedPlatformLogged = true;
	}
	Shutdown();
	return;
#endif

	if(g_Config.m_RiVoiceOffNonActive && m_pGraphics && !m_pGraphics->WindowActive())
	{
		StopWorker();
		if(m_CaptureDevice)
		{
			SDL_ClearQueuedAudio(m_CaptureDevice);
			SDL_PauseAudioDevice(m_CaptureDevice, 1);
		}
		if(m_OutputDevice)
			SDL_PauseAudioDevice(m_OutputDevice, 1);
		ClearPeerFrames();
		return;
	}
	if(m_CaptureDevice)
		SDL_PauseAudioDevice(m_CaptureDevice, 0);
	if(m_OutputDevice)
		SDL_PauseAudioDevice(m_OutputDevice, 0);

	UpdateServerAddrConfig();
	const bool ContextChanged = UpdateContext();
	UpdateClientSnapshot();
	UpdateConfigSnapshot();

#if defined(CONF_PLATFORM_ANDROID)
	if(!m_AndroidRecordPermissionKnown)
	{
		m_AndroidRecordPermissionGranted = RequestAndroidAudioRecordPermission();
		m_AndroidRecordPermissionKnown = true;
		if(!m_AndroidRecordPermissionGranted)
		{
			VoiceLogErrorOnce(m_aAudioErrorLog, sizeof(m_aAudioErrorLog), "Microphone permission denied on Android");
			m_CaptureUnavailable = true;
		}
	}
#endif

	const bool WantStereo = g_Config.m_RiVoiceStereo != 0;
	const int DesiredChannels = WantStereo ? 2 : 1;
	bool NeedReinit = false;
	if(str_comp(m_aAudioBackend, g_Config.m_RiVoiceAudioBackend) != 0)
		NeedReinit = true;
	if(str_comp(m_aInputDeviceName, g_Config.m_RiVoiceInputDevice) != 0)
		NeedReinit = true;
	if(str_comp(m_aOutputDeviceName, g_Config.m_RiVoiceOutputDevice) != 0)
		NeedReinit = true;
	if(m_OutputStereo != WantStereo)
		NeedReinit = true;
	if(m_OutputDevice && m_OutputSpec.channels > 0 && m_OutputSpec.channels != DesiredChannels)
	{
		SDL_CloseAudioDevice(m_OutputDevice);
		m_OutputDevice = 0;
		m_OutputSpec = {};
		m_OutputChannels.store(0);
		m_MixBuffer.clear();
		NeedReinit = true;
	}
	if(!m_pEncoder)
		NeedReinit = true;
	if(!m_OutputDevice)
	{
		if(!m_OutputUnavailable)
			NeedReinit = true;
	}
	if(!m_CaptureDevice)
	{
#if defined(CONF_PLATFORM_ANDROID)
		if(!(m_AndroidRecordPermissionKnown && !m_AndroidRecordPermissionGranted))
#endif
		if(!m_CaptureUnavailable)
			NeedReinit = true;
	}
	if(ContextChanged)
	{
		StopWorker();
		ClearPeerFrames();
	}

	if(NeedReinit)
	{
		StopWorker();
		m_AudioRefreshRequested.store(true);
	}
	if(!EnsureSocket())
	{
		StopWorker();
		return;
	}
	if(!m_Worker.joinable())
		m_AudioRefreshRequested.store(true);

	StartWorker();
}
