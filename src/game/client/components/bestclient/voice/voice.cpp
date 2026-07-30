/* Copyright © 2026 BestProject Team */
#include "voice.h"

#include "../version.h"
#include "protocol.h"

#include <base/color.h>
#include <base/log.h>
#include <base/math.h>
#include <base/str.h>
#include <base/system.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/shared/bestclient_indicator_protocol.h>
#include <engine/shared/config.h>
#include <engine/shared/http.h>
#include <engine/shared/json.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/animstate.h>
#include <game/client/bc_ui_animations.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/hud_layout.h>
#include <game/client/gameclient.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <SDL.h>
#include <opus.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <unordered_set>

namespace
{
	constexpr int CAPTURE_READ_SAMPLES = 4096;
	constexpr int MAX_RECEIVE_PACKETS_PER_TICK = 64;
	constexpr int PLAYBACK_TARGET_FRAMES = BestClientVoice::FRAME_SIZE * 3;
	constexpr int PLAYBACK_MAX_RESYNC_FRAMES = BestClientVoice::FRAME_SIZE * 4;
	constexpr int MAX_PACKET_GAP_FOR_PLC = 3;
	constexpr int PEER_TIMEOUT_SECONDS = 10;
	constexpr int VOICE_TALKING_TIMEOUT_MS = 350;
	constexpr int VOICE_MAX_BITRATE_KBPS = 128;
	constexpr int VOICE_HEARTBEAT_SECONDS = 5;
	constexpr int VOICE_SERVER_STALE_SECONDS = 10;
	constexpr int VOICE_IDLE_SHUTDOWN_SECONDS = 5;
	constexpr int VOICE_START_RETRY_SECONDS = 5;
	constexpr float VOICE_TILE_WORLD_SIZE = 32.0f;
	constexpr int SERVER_LIST_PING_TIMEOUT_SEC = 2;
	constexpr int SERVER_LIST_PING_INTERVAL_SEC = 30;
	constexpr const char *MANAGED_VOICE_SERVER_CONFIG = "managed";
	constexpr const char *VOICE_MUTED_CFG_PATH = "BestClient/voice_muted.cfg";
	constexpr uint8_t OBFUSCATION_KEY = 0x5a;
	constexpr std::array<uint8_t, 19> OBFUSCATED_DEFAULT_VOICE_SERVER_ADDRESS = {
		107, 99, 105, 116, 104, 105, 116, 104, 106, 107, 116, 107, 104, 111, 96, 98, 109, 109, 109};
	constexpr std::array<uint8_t, 23> OBFUSCATED_VOICE_AUTH_KEY = {
		56, 57, 44, 110, 119, 40, 63, 54, 119, 107, 109, 107, 119, 44, 53, 51, 57, 63, 119, 54, 53, 57, 49};
	constexpr std::array<uint8_t, 46> OBFUSCATED_VOICE_MASTER_LIST_URL = {
		50, 46, 46, 42, 41, 96, 117, 117, 107, 111, 106, 116, 104, 110, 107, 116, 109, 106, 116, 107, 98, 98, 96, 105,
		106, 106, 106, 117, 44, 53, 51, 57, 63, 117, 41, 63, 40, 44, 63, 40, 41, 116, 48, 41, 53, 52};

	template<size_t N>
	std::string DecodeObfuscatedString(const std::array<uint8_t, N> &aData)
	{
		std::string Result;
		Result.resize(N);
		for(size_t i = 0; i < N; ++i)
			Result[i] = (char)(aData[i] ^ OBFUSCATION_KEY);
		return Result;
	}

	const std::string &DefaultVoiceServerAddress()
	{
		static const std::string s_Address = DecodeObfuscatedString(OBFUSCATED_DEFAULT_VOICE_SERVER_ADDRESS);
		return s_Address;
	}

	const std::string &VoiceAuthKey()
	{
		static const std::string s_Key = DecodeObfuscatedString(OBFUSCATED_VOICE_AUTH_KEY);
		return s_Key;
	}

	const std::string &VoiceMasterListUrl()
	{
		static const std::string s_Url = DecodeObfuscatedString(OBFUSCATED_VOICE_MASTER_LIST_URL);
		return s_Url;
	}

	void WriteVoiceString(std::vector<uint8_t> &vOut, const char *pStr, int MaxLen = 128)
	{
		int Len = (int)str_length(pStr);
		if(Len > MaxLen) Len = MaxLen;
		BestClientVoice::WriteU16(vOut, (uint16_t)Len);
		for(int i = 0; i < Len; i++)
			vOut.push_back((uint8_t)pStr[i]);
	}

	bool ReadVoiceString(const uint8_t *pData, int DataSize, int &Offset, std::string &Out, int MaxLen = 128)
	{
		uint16_t Size = 0;
		if(!BestClientVoice::ReadU16(pData, DataSize, Offset, Size))
			return false;
		if(Size > (uint16_t)MaxLen)
			return false;
		if(Offset + (int)Size > DataSize)
			return false;
		Out.assign((const char *)pData + Offset, (size_t)Size);
		Offset += (int)Size;
		return true;
	}

	void ToggleVoiceMicMute()
	{
		if(g_Config.m_BcVoiceChatMicMuted)
		{
			if(g_Config.m_BcVoiceChatHeadphonesMuted)
				return;
			g_Config.m_BcVoiceChatMicMuted = 0;
		}
		else
		{
			g_Config.m_BcVoiceChatMicMuted = 1;
		}
	}

	void ToggleVoiceHeadphonesMute()
	{
		const bool Muted = g_Config.m_BcVoiceChatHeadphonesMuted == 0;
		g_Config.m_BcVoiceChatHeadphonesMuted = Muted ? 1 : 0;
		if(Muted)
			g_Config.m_BcVoiceChatMicMuted = 1;
		else
			g_Config.m_BcVoiceChatMicMuted = 0;
	}

	bool IsLegacyVoiceServerAddress(const char *pAddress)
	{
		return !pAddress || pAddress[0] == '\0' || str_comp(pAddress, MANAGED_VOICE_SERVER_CONFIG) == 0 ||
		       str_comp(pAddress, "127.0.0.1:8777") == 0 || str_comp(pAddress, "localhost:8777") == 0;
	}

	const char *ResolvedVoiceServerAddress(const char *pAddress)
	{
		return IsLegacyVoiceServerAddress(pAddress) ? DefaultVoiceServerAddress().c_str() : pAddress;
	}

	void EnsureDefaultVoiceServerAddress()
	{
		if(IsLegacyVoiceServerAddress(g_Config.m_BcVoiceChatServerAddress))
			str_copy(g_Config.m_BcVoiceChatServerAddress, MANAGED_VOICE_SERVER_CONFIG, sizeof(g_Config.m_BcVoiceChatServerAddress));
	}

	const char *GetAudioDeviceNameByIndex(int IsCapture, int Index)
	{
		const int DeviceCount = SDL_GetNumAudioDevices(IsCapture);
		if(DeviceCount <= 0 || Index < 0 || Index >= DeviceCount)
			return nullptr;
		return SDL_GetAudioDeviceName(Index, IsCapture);
	}

	bool IsForwardSequence(uint16_t LastSequence, uint16_t NewSequence)
	{
		const uint16_t Delta = (uint16_t)(NewSequence - LastSequence);
		return Delta != 0 && Delta < 0x8000;
	}

	void ConfigureVoiceOpusEncoder(OpusEncoder *pEncoder, int BitrateKbps)
	{
		if(!pEncoder)
			return;

		const int ClampedBitrate = std::clamp(BitrateKbps, 6, VOICE_MAX_BITRATE_KBPS);
		opus_encoder_ctl(pEncoder, OPUS_SET_BITRATE(ClampedBitrate * 1000));
		opus_encoder_ctl(pEncoder, OPUS_SET_VBR(1));
		opus_encoder_ctl(pEncoder, OPUS_SET_VBR_CONSTRAINT(0));
		opus_encoder_ctl(pEncoder, OPUS_SET_COMPLEXITY(10));
		opus_encoder_ctl(pEncoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
		opus_encoder_ctl(pEncoder, OPUS_SET_DTX(0));

		// Client-side resilience. Server just relays Opus payloads.
		// Start with full-quality mode and enable FEC/loss adaptation only when needed.
		opus_encoder_ctl(pEncoder, OPUS_SET_INBAND_FEC(0));
		opus_encoder_ctl(pEncoder, OPUS_SET_PACKET_LOSS_PERC(0));
	}

	float SanitizeAudioValue(float Value)
	{
		if(!std::isfinite(Value))
			return 0.0f;
		if(Value > 1000000.0f)
			return 1000000.0f;
		if(Value < -1000000.0f)
			return -1000000.0f;
		return Value;
	}

	float VoiceFrameRms(const int16_t *pSamples, int Count)
	{
		if(!pSamples || Count <= 0)
			return 0.0f;
		double Sum = 0.0;
		for(int i = 0; i < Count; ++i)
		{
			const float X = pSamples[i] / 32768.0f;
			Sum += X * X;
		}
		return (float)std::sqrt(Sum / (double)Count);
	}

	void ApplyAutoNoiseSuppressorSimple(int16_t *pSamples, int Count, float Strength, float &NoiseFloor, float &Gate)
	{
		if(!pSamples || Count <= 0)
			return;

		Strength = std::clamp(Strength, 0.0f, 1.0f);
		if(Strength <= 0.0f)
			return;

		const float Rms = VoiceFrameRms(pSamples, Count);
		if(!std::isfinite(Rms))
			return;

		if(NoiseFloor <= 0.0f)
			NoiseFloor = Rms;

		const float UpdateFast = 0.2f;
		const float UpdateSlow = 0.05f;
		if(Rms < NoiseFloor * 1.2f)
			NoiseFloor += (Rms - NoiseFloor) * UpdateFast;
		else if(Rms < NoiseFloor * 1.5f)
			NoiseFloor += (Rms - NoiseFloor) * UpdateSlow;

		NoiseFloor = std::clamp(NoiseFloor, 1.0f / 32768.0f, 0.5f);

		const float MinGain = 1.0f - Strength * 0.9f;
		const float LowSnr = 1.2f;
		const float HighSnr = 2.5f;
		const float Snr = Rms / (NoiseFloor + 1e-6f);

		float Target = 1.0f;
		if(Snr <= LowSnr)
			Target = MinGain;
		else if(Snr >= HighSnr)
			Target = 1.0f;
		else
		{
			const float T = (Snr - LowSnr) / (HighSnr - LowSnr);
			Target = MinGain + (1.0f - MinGain) * T;
		}

		const float Dt = Count / (float)BestClientVoice::SAMPLE_RATE;
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

		for(int i = 0; i < Count; ++i)
		{
			const float Out = pSamples[i] * Gate;
			pSamples[i] = (int16_t)std::clamp(Out, -32768.0f, 32767.0f);
		}
	}

	void ApplyAutoHpfCompressor(int16_t *pSamples, int Count, float &PrevIn, float &PrevOut, float &Env)
	{
		if(!pSamples || Count <= 0)
			return;

		const float CutoffHz = 120.0f;
		const float Rc = 1.0f / (2.0f * 3.14159265f * CutoffHz);
		const float Dt = 1.0f / BestClientVoice::SAMPLE_RATE;
		const float Alpha = Rc / (Rc + Dt);

		// Rushie defaults.
		const float Threshold = 0.20f;
		const float Ratio = 2.5f;
		const float AttackSec = 0.02f;
		const float ReleaseSec = 0.20f;
		const float MakeupGain = 1.6f;
		const float NoiseFloor = 0.02f;
		const float Limiter = 0.50f;
		const float AttackCoeff = 1.0f - std::exp(-1.0f / (AttackSec * BestClientVoice::SAMPLE_RATE));
		const float ReleaseCoeff = 1.0f - std::exp(-1.0f / (ReleaseSec * BestClientVoice::SAMPLE_RATE));

		for(int i = 0; i < Count; ++i)
		{
			const float X = pSamples[i] / 32768.0f;
			const float Y = Alpha * (PrevOut + X - PrevIn);
			PrevIn = X;
			PrevOut = SanitizeAudioValue(Y);

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

	std::string NormalizeVoiceNameKey(const char *pName)
	{
		if(!pName)
			return {};
		const char *pBegin = pName;
		const char *pEnd = pName + str_length(pName);
		while(pBegin < pEnd && std::isspace((unsigned char)*pBegin))
			pBegin++;
		while(pEnd > pBegin && std::isspace((unsigned char)pEnd[-1]))
			pEnd--;

		std::string Key;
		Key.reserve((size_t)(pEnd - pBegin));
		for(const char *p = pBegin; p < pEnd; ++p)
			Key.push_back((char)std::tolower((unsigned char)*p));
		return Key;
	}

	void ParseVoiceNameList(const char *pList, std::unordered_set<std::string> &Out)
	{
		Out.clear();
		if(!pList || pList[0] == '\0')
			return;

		const char *p = pList;
		while(*p)
		{
			while(*p == ',' || std::isspace((unsigned char)*p))
				p++;
			if(*p == '\0')
				break;

			const char *pStart = p;
			while(*p && *p != ',')
				p++;

			const char *pEnd = p;
			while(pEnd > pStart && std::isspace((unsigned char)pEnd[-1]))
				pEnd--;
			std::string Key;
			Key.reserve((size_t)(pEnd - pStart));
			for(const char *q = pStart; q < pEnd; ++q)
				Key.push_back((char)std::tolower((unsigned char)*q));
			if(!Key.empty())
				Out.insert(std::move(Key));
		}
	}

	void ParseVoiceMutedConfigFile(const char *pData, unsigned DataSize, std::unordered_set<std::string> &Out)
	{
		Out.clear();
		if(!pData || DataSize == 0)
			return;

		const char *p = pData;
		const char *pEnd = pData + DataSize;
		while(p < pEnd)
		{
			const char *pLineStart = p;
			while(p < pEnd && *p != '\n')
				++p;
			const char *pLineEnd = p;
			if(p < pEnd && *p == '\n')
				++p;

			while(pLineStart < pLineEnd && std::isspace((unsigned char)*pLineStart))
				++pLineStart;
			while(pLineEnd > pLineStart && std::isspace((unsigned char)pLineEnd[-1]))
				--pLineEnd;
			if(pLineStart >= pLineEnd)
				continue;
			if(*pLineStart == '#' || *pLineStart == ';')
				continue;

			char aName[128];
			str_truncate(aName, sizeof(aName), pLineStart, (int)(pLineEnd - pLineStart));
			const std::string Key = NormalizeVoiceNameKey(aName);
			if(!Key.empty())
				Out.insert(Key);
		}
	}

	void ParseVoiceNameVolumeList(const char *pList, std::unordered_map<std::string, int> &Out)
	{
		Out.clear();
		if(!pList || pList[0] == '\0')
			return;

		const char *p = pList;
		while(*p)
		{
			while(*p == ',' || std::isspace((unsigned char)*p))
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
			for(const char *q = pStart; q < pEnd; ++q)
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

			if(pNameEnd <= pStart || pValueStart >= pEnd)
				continue;

			char aName[128];
			str_truncate(aName, sizeof(aName), pStart, (int)(pNameEnd - pStart));
			std::string Key = NormalizeVoiceNameKey(aName);
			if(Key.empty())
				continue;

			char aValue[16];
			str_truncate(aValue, sizeof(aValue), pValueStart, (int)(pEnd - pValueStart));
			int Percent = std::clamp(str_toint(aValue), 0, 200);
			Out[std::move(Key)] = Percent;
		}
	}

	void WriteVoiceNameList(const std::unordered_set<std::string> &Set, char *pOut, int OutSize)
	{
		if(!pOut || OutSize <= 0)
			return;
		pOut[0] = '\0';

		std::vector<const char *> vpKeys;
		vpKeys.reserve(Set.size());
		for(const auto &Key : Set)
			vpKeys.push_back(Key.c_str());
		std::sort(vpKeys.begin(), vpKeys.end(), [](const char *pA, const char *pB) { return str_comp(pA, pB) < 0; });

		bool First = true;
		for(const char *pKey : vpKeys)
		{
			if(!First)
				str_append(pOut, ",", OutSize);
			First = false;
			str_append(pOut, pKey, OutSize);
		}
	}

	void WriteVoiceNameVolumeList(const std::unordered_map<std::string, int> &Map, char *pOut, int OutSize)
	{
		if(!pOut || OutSize <= 0)
			return;
		pOut[0] = '\0';

		std::vector<std::pair<const char *, int>> vItems;
		vItems.reserve(Map.size());
		for(const auto &Pair : Map)
			vItems.emplace_back(Pair.first.c_str(), Pair.second);
		std::sort(vItems.begin(), vItems.end(), [](const auto &A, const auto &B) { return str_comp(A.first, B.first) < 0; });

		bool First = true;
		for(const auto &Pair : vItems)
		{
			if(!First)
				str_append(pOut, ",", OutSize);
			First = false;
			str_append(pOut, Pair.first, OutSize);
			str_append(pOut, "=", OutSize);
			char aValue[16];
			str_format(aValue, sizeof(aValue), "%d", std::clamp(Pair.second, 0, 200));
			str_append(pOut, aValue, OutSize);
		}
	}

	ColorRGBA ApplyVoiceHudAlpha(CGameClient *pGameClient, ColorRGBA Color)
	{
		(void)pGameClient;
		return Color;
	}

	ColorRGBA VoiceHudThemeColor(CGameClient *pGameClient, ColorRGBA Fallback, bool ForcePreview, float MixAmount)
	{
		ColorRGBA ThemeColor;
		if(pGameClient != nullptr && pGameClient->m_MusicPlayer.GetHudThemeColor(ThemeColor, ForcePreview))
		{
			const float Blend = std::clamp(MixAmount, 0.0f, 1.0f);
			return ColorRGBA(
				mix(Fallback.r, ThemeColor.r, Blend),
				mix(Fallback.g, ThemeColor.g, Blend),
				mix(Fallback.b, ThemeColor.b, Blend),
				mix(Fallback.a, ThemeColor.a, Blend));
		}
		return Fallback;
	}

	int VoiceHudBackgroundCorners(int Module, float RectX, float RectY, float RectW, float RectH, float CanvasWidth, float CanvasHeight)
	{
		return HudLayout::BackgroundCorners(IGraphics::CORNER_ALL, RectX, RectY, RectW, RectH, CanvasWidth, CanvasHeight);
	}

}

void CVoiceChat::OnConsoleInit()
{
	Console()->Register("voice_connect", "?s[address]", CFGFLAG_CLIENT, ConVoiceConnect, this, "Connect to voice server");
	Console()->Register("voice_disconnect", "", CFGFLAG_CLIENT, ConVoiceDisconnect, this, "Disconnect from voice server");
	Console()->Register("voice_status", "", CFGFLAG_CLIENT, ConVoiceStatus, this, "Show voice status");
	Console()->Register("+voicechat", "", CFGFLAG_CLIENT, ConKeyVoiceTalk, this, "Push-to-talk");
	Console()->Register("toggle_voice_mic_mute", "", CFGFLAG_CLIENT, ConToggleVoiceMicMute, this, "Toggle voice microphone mute");
	Console()->Register("toggle_voice_headphones_mute", "", CFGFLAG_CLIENT, ConToggleVoiceHeadphonesMute, this, "Toggle voice headphones mute");
}

void CVoiceChat::OnReset()
{
	m_PushToTalkPressed = false;
	m_AutoActivationUntilTick = 0;
	m_SendSequence = 0;
	m_MicLevel = 0.0f;
	m_MicLimiterGain = 1.0f;
	m_AutoNsNoiseFloor = 0.0f;
	m_AutoNsGate = 1.0f;
	m_AutoHpfPrevIn = 0.0f;
	m_AutoHpfPrevOut = 0.0f;
	m_AutoCompEnv = 0.0f;
	m_WasTransmitActive = false;
	m_LastHelloTick = 0;
	m_SecondaryLastHelloTick = 0;
	m_LastServerPacketTick = 0;
	m_SecondaryLastServerPacketTick = 0;
	m_LastHeartbeatTick = 0;
	m_SecondaryLastHeartbeatTick = 0;
	m_SecondarySendSequence = 0;
	m_SecondaryRegistered = false;
	m_SecondaryClientVoiceId = 0;
	m_SecondaryHelloResetPending = false;
	m_LastBitrate = -1;
	m_LastEncoderTuneTick = 0;
	m_LastEncoderLossPerc = -1;
	m_LastEncoderFec = -1;
	m_PlaybackQueueErrorLogged = false;
	m_LastServerListAutoFetchTick = 0;
	m_HelloResetPending = false;
	m_AdvertisedRoomKey.clear();
	m_AdvertisedPlayerName.clear();
	m_AdvertisedGameClientId = BestClientVoice::INVALID_GAME_CLIENT_ID - 1;
	m_AdvertisedTeam = std::numeric_limits<int>::min();
	m_SecondaryAdvertisedRoomKey.clear();
	m_SecondaryAdvertisedPlayerName.clear();
	m_SecondaryAdvertisedGameClientId = BestClientVoice::INVALID_GAME_CLIENT_ID - 1;
	m_SecondaryAdvertisedTeam = std::numeric_limits<int>::min();
	m_EnableYourGroupRevealPhase = g_Config.m_BcVoiceChatUseTeam0 ? 1.0f : 0.0f;
	m_LastUseTeam0Mode = g_Config.m_BcVoiceChatUseTeam0 != 0;
	m_LastEnableYourGroup = g_Config.m_BcVoiceChatUseTeam0 != 0 && g_Config.m_BcVoiceChatEnableYourGroup != 0;
	ClearPeerState();
	m_CapturePcm.Clear();
	m_MicMonitorPcm.Clear();
	InvalidatePeerCaches();
}

void CVoiceChat::LoadMutedNamesFromFile()
{
	if(m_MutedNamesLoadedFromFile)
		return;
	m_MutedNamesLoadedFromFile = true;

	void *pData = nullptr;
	unsigned DataSize = 0;
	if(!Storage()->ReadFile(VOICE_MUTED_CFG_PATH, IStorage::TYPE_SAVE, &pData, &DataSize))
	{
		// No legacy file yet: persist current state so future sessions use the dedicated file.
		SaveMutedNamesToFile();
		str_copy(m_aLastPersistedMutedNames, g_Config.m_BcVoiceChatMutedNames, sizeof(m_aLastPersistedMutedNames));
		return;
	}

	std::unordered_set<std::string> LoadedMutedNames;
	ParseVoiceMutedConfigFile((const char *)pData, DataSize, LoadedMutedNames);
	free(pData);

	m_MutedNameKeys = std::move(LoadedMutedNames);
	char aOut[512];
	WriteVoiceNameList(m_MutedNameKeys, aOut, sizeof(aOut));
	str_copy(g_Config.m_BcVoiceChatMutedNames, aOut, sizeof(g_Config.m_BcVoiceChatMutedNames));
	str_copy(m_aLastMutedNames, g_Config.m_BcVoiceChatMutedNames, sizeof(m_aLastMutedNames));
	str_copy(m_aLastPersistedMutedNames, g_Config.m_BcVoiceChatMutedNames, sizeof(m_aLastPersistedMutedNames));
	m_PeerListDirty = true;
}

void CVoiceChat::SaveMutedNamesToFile()
{
	Storage()->CreateFolder("BestClient", IStorage::TYPE_SAVE);
	IOHANDLE File = Storage()->OpenFile(VOICE_MUTED_CFG_PATH, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return;

	std::vector<const char *> vpKeys;
	vpKeys.reserve(m_MutedNameKeys.size());
	for(const auto &Key : m_MutedNameKeys)
		vpKeys.push_back(Key.c_str());
	std::sort(vpKeys.begin(), vpKeys.end(), [](const char *pA, const char *pB) { return str_comp(pA, pB) < 0; });

	for(const char *pKey : vpKeys)
	{
		io_write(File, pKey, str_length(pKey));
		io_write_newline(File);
	}
	io_close(File);
}

void CVoiceChat::OnStateChange(int NewState, int OldState)
{
	(void)OldState;
	if(NewState == IClient::STATE_OFFLINE)
	{
		StopVoice();
		m_ServerRowButtons.clear();
		m_vServerEntries.clear();
		ResetServerListTask();
		CloseServerListPingSocket();
		m_LastServerListAutoFetchTick = 0;
	}
	else if(NewState == IClient::STATE_ONLINE)
	{
		ResetServerListTask();
		CloseServerListPingSocket();
		m_vServerEntries.clear();
		m_ServerRowButtons.clear();
		m_LastServerListAutoFetchTick = time_get();
		FetchServerList();
	}
}

void CVoiceChat::OnUpdate()
{
	const int64_t PerfStart = time_get();
	const bool Online = Client()->State() == IClient::STATE_ONLINE;
	EnsureDefaultVoiceServerAddress();

	if(g_Config.m_BcVoiceChatHeadphonesMuted && !g_Config.m_BcVoiceChatMicMuted)
		g_Config.m_BcVoiceChatMicMuted = 1;

	const bool UseTeam0Mode = g_Config.m_BcVoiceChatUseTeam0 != 0;
	if(!UseTeam0Mode && g_Config.m_BcVoiceChatEnableYourGroup != 0)
		g_Config.m_BcVoiceChatEnableYourGroup = 0;
	const bool EnableYourGroup = UseTeam0Mode && g_Config.m_BcVoiceChatEnableYourGroup != 0;

	const bool ModuleUiRevealAnimationsEnabled = BCUiAnimations::Enabled() && g_Config.m_BcModuleUiRevealAnimation != 0;
	if(ModuleUiRevealAnimationsEnabled)
	{
		BCUiAnimations::UpdatePhase(
			m_EnableYourGroupRevealPhase,
			UseTeam0Mode ? 1.0f : 0.0f,
			Client()->RenderFrameTime(),
			BCUiAnimations::MsToSeconds(g_Config.m_BcModuleUiRevealAnimationMs));
	}
	else
	{
		m_EnableYourGroupRevealPhase = UseTeam0Mode ? 1.0f : 0.0f;
	}

	const bool TeamModeChanged = UseTeam0Mode != m_LastUseTeam0Mode || EnableYourGroup != m_LastEnableYourGroup;
	if(TeamModeChanged)
	{
		m_LastUseTeam0Mode = UseTeam0Mode;
		m_LastEnableYourGroup = EnableYourGroup;
		if(m_PlaybackDevice)
			SDL_ClearQueuedAudio(m_PlaybackDevice);
		ClearPeerState();
		InvalidatePeerCaches();
	}

	if(str_comp(m_aLastMutedNames, g_Config.m_BcVoiceChatMutedNames) != 0)
	{
		ParseVoiceNameList(g_Config.m_BcVoiceChatMutedNames, m_MutedNameKeys);
		str_copy(m_aLastMutedNames, g_Config.m_BcVoiceChatMutedNames, sizeof(m_aLastMutedNames));
		m_PeerListDirty = true;
	}
	if(str_comp(m_aLastNameVolumes, g_Config.m_BcVoiceChatNameVolumes) != 0)
	{
		ParseVoiceNameVolumeList(g_Config.m_BcVoiceChatNameVolumes, m_NameVolumePercent);
		str_copy(m_aLastNameVolumes, g_Config.m_BcVoiceChatNameVolumes, sizeof(m_aLastNameVolumes));
		m_PeerListDirty = true;
	}
	LoadMutedNamesFromFile();
	if(str_comp(m_aLastPersistedMutedNames, g_Config.m_BcVoiceChatMutedNames) != 0)
	{
		str_copy(m_aLastPersistedMutedNames, g_Config.m_BcVoiceChatMutedNames, sizeof(m_aLastPersistedMutedNames));
		m_MutedListDirty = true;
	}
	if(m_MutedListDirty)
	{
		const int64_t Now = time_get();
		if(m_LastMuteSaveTime == 0 || Now - m_LastMuteSaveTime >= time_freq())
		{
			SaveMutedNamesToFile();
			m_LastMuteSaveTime = Now;
			m_MutedListDirty = false;
		}
	}

	const std::string EffectiveServerAddr = EffectiveServerAddress();
	const bool ServerChanged = str_comp(m_aLastServerAddr, EffectiveServerAddr.c_str()) != 0;
	const bool DeviceChanged = m_LastInputDevice != g_Config.m_BcVoiceChatInputDevice || m_LastOutputDevice != g_Config.m_BcVoiceChatOutputDevice;

	if(m_pServerListTask && m_pServerListTask->State() == EHttpState::DONE)
	{
		FinishServerList();
		ResetServerListTask();
	}
	else if(m_pServerListTask && (m_pServerListTask->State() == EHttpState::ERROR || m_pServerListTask->State() == EHttpState::ABORTED))
	{
		ResetServerListTask();
	}

	if(Online && m_vServerEntries.empty() && !m_pServerListTask)
	{
		const int64_t Now = time_get();
		if(m_LastServerListAutoFetchTick == 0 || Now - m_LastServerListAutoFetchTick >= time_freq())
		{
			m_LastServerListAutoFetchTick = Now;
			FetchServerList();
		}
	}

	ProcessServerListPing();

	const bool Enabled = g_Config.m_BcVoiceChatEnable != 0;
	if(!Enabled)
	{
		m_SubsystemState = ESubsystemRuntimeState::DISABLED;
		if(m_Socket)
			StopVoice();
		return;
	}
	m_SubsystemState = ESubsystemRuntimeState::ARMED;

	if(ServerChanged || DeviceChanged)
	{
		StopVoice();
		if(Online)
			StartVoice();
		str_copy(m_aLastServerAddr, EffectiveServerAddr.c_str(), sizeof(m_aLastServerAddr));
		m_LastInputDevice = g_Config.m_BcVoiceChatInputDevice;
		m_LastOutputDevice = g_Config.m_BcVoiceChatOutputDevice;
	}

	if(!Online)
		return;

	if(!m_Socket)
	{
		const int64_t Now = time_get();
		const int64_t RetryDelay = m_RuntimeState == RUNTIME_RECONNECTING ? time_freq() : VOICE_START_RETRY_SECONDS * time_freq();
		if(ShouldStartVoicePipeline(Online) && (m_LastStartAttempt == 0 || Now - m_LastStartAttempt > RetryDelay))
		{
			m_LastStartAttempt = Now;
			m_RuntimeState = m_RuntimeState == RUNTIME_RECONNECTING ? RUNTIME_RECONNECTING : RUNTIME_STARTING;
			m_SubsystemState = ESubsystemRuntimeState::ARMED;
			StartVoice();
		}
		if(!m_Socket)
			return;
	}

	const int64_t Now = time_get();
	if(m_Registered && m_LastServerPacketTick > 0 && Now - m_LastServerPacketTick > VOICE_SERVER_STALE_SECONDS * time_freq())
	{
		m_RuntimeState = RUNTIME_STALE;
		BeginReconnect();
		return;
	}

	ProcessNetwork();
	m_SubsystemState = ESubsystemRuntimeState::ACTIVE;

	if(!m_pEncoder)
		return;

	const int ClampedBitrate = std::clamp(g_Config.m_BcVoiceChatBitrate, 6, VOICE_MAX_BITRATE_KBPS);
	if(m_LastBitrate != ClampedBitrate)
	{
		ConfigureVoiceOpusEncoder(m_pEncoder, ClampedBitrate);
		m_LastBitrate = ClampedBitrate;
		m_LastEncoderLossPerc = 0;
		m_LastEncoderFec = 0;
		m_LastEncoderTuneTick = 0;
	}
	TuneEncoderForNetwork();

	const std::string RoomKey = CurrentRoomKey();
	const int GameClientId = LocalGameClientId();
	const int VoiceTeam = LocalVoiceTeam();
	const char *pCurrentName = "";
	if(GameClientId >= 0 && GameClientId < MAX_CLIENTS)
		pCurrentName = GameClient()->m_aClients[GameClientId].m_aName;
	const bool RoomChanged = RoomKey != m_AdvertisedRoomKey;
	const bool TeamChanged = VoiceTeam != m_AdvertisedTeam;
	const bool NameChanged = m_AdvertisedPlayerName != pCurrentName;
	const bool IdentityChanged = RoomChanged || GameClientId != m_AdvertisedGameClientId || TeamChanged || NameChanged;
	const bool NeedsRegistrationHello = !m_Registered && (m_LastHelloTick == 0 || Now - m_LastHelloTick > time_freq());
	const bool NeedsHeartbeatHello = m_Registered && (IdentityChanged || m_LastHeartbeatTick == 0 || Now - m_LastHeartbeatTick > VOICE_HEARTBEAT_SECONDS * time_freq());
	if(RoomChanged || TeamChanged)
	{
		ClearPeerState();
		if(m_PlaybackDevice)
			SDL_ClearQueuedAudio(m_PlaybackDevice);
	}
	if(IdentityChanged)
		m_HelloResetPending = true;
	if(NeedsRegistrationHello || NeedsHeartbeatHello)
		SendHello();

	const bool UseSecondaryConnection = ShouldUseSecondaryTeamConnection();
	if(!UseSecondaryConnection)
	{
		if(m_SecondarySocket)
		{
			SendGoodbyeSecondary();
			CloseSecondaryNetworking();
		}
	}
	else
	{
		if(!m_SecondarySocket)
			OpenSecondaryNetworking();

		if(m_SecondarySocket)
		{
			if(m_SecondaryRegistered && m_SecondaryLastServerPacketTick > 0 && Now - m_SecondaryLastServerPacketTick > VOICE_SERVER_STALE_SECONDS * time_freq())
			{
				CloseSecondaryNetworking();
				OpenSecondaryNetworking();
			}

			ProcessSecondaryNetwork();

			const int SecondaryTeam = LocalOwnVoiceTeam();
			const bool SecondaryRoomChanged = RoomKey != m_SecondaryAdvertisedRoomKey;
			const bool SecondaryTeamChanged = SecondaryTeam != m_SecondaryAdvertisedTeam;
			const bool SecondaryNameChanged = m_SecondaryAdvertisedPlayerName != pCurrentName;
			const bool SecondaryIdentityChanged = SecondaryRoomChanged || GameClientId != m_SecondaryAdvertisedGameClientId || SecondaryTeamChanged || SecondaryNameChanged;
			const bool SecondaryNeedsRegistrationHello = !m_SecondaryRegistered && (m_SecondaryLastHelloTick == 0 || Now - m_SecondaryLastHelloTick > time_freq());
			const bool SecondaryNeedsHeartbeatHello = m_SecondaryRegistered && (SecondaryIdentityChanged || m_SecondaryLastHeartbeatTick == 0 || Now - m_SecondaryLastHeartbeatTick > VOICE_HEARTBEAT_SECONDS * time_freq());
			if(SecondaryIdentityChanged)
				m_SecondaryHelloResetPending = true;
			if(SecondaryNeedsRegistrationHello || SecondaryNeedsHeartbeatHello)
				SendHelloSecondary();
		}
	}

	ProcessCapture();
	ProcessPlayback();
	CleanupPeers();
	m_SnapMappingDirty = true;
	m_TalkingStateDirty = true;
	RefreshPeerCaches();

	if(ShouldKeepVoicePipelineActive())
	{
		m_LastActiveTick = Now;
	}
	else if(m_Socket && m_LastActiveTick > 0 && Now - m_LastActiveTick > VOICE_IDLE_SHUTDOWN_SECONDS * time_freq())
	{
		m_SubsystemState = ESubsystemRuntimeState::COOLDOWN;
		StopVoice();
	}

	m_LastUpdateCostTick = time_get() - PerfStart;
	m_MaxUpdateCostTick = maximum(m_MaxUpdateCostTick, m_LastUpdateCostTick);
	m_TotalUpdateCostTick += m_LastUpdateCostTick;
	++m_UpdateSamples;
	if(g_Config.m_Debug)
	{
		if(m_LastPerfReportTick == 0 || Now - m_LastPerfReportTick >= time_freq())
		{
			dbg_msg("voice/perf", "update last=%.3fms avg=%.3fms max=%.3fms samples=%lld socket=%d peers=%d",
				m_LastUpdateCostTick * 1000.0 / (double)time_freq(),
				m_UpdateSamples > 0 ? (m_TotalUpdateCostTick * 1000.0 / (double)time_freq()) / (double)m_UpdateSamples : 0.0,
				m_MaxUpdateCostTick * 1000.0 / (double)time_freq(),
				(long long)m_UpdateSamples,
				m_Socket != nullptr ? 1 : 0,
				(int)m_Peers.size());
			m_LastPerfReportTick = Now;
			m_TotalUpdateCostTick = 0;
			m_UpdateSamples = 0;
			m_MaxUpdateCostTick = 0;
		}
	}
}

void CVoiceChat::OnShutdown()
{
	StopVoice();
	ResetServerListTask();
	CloseServerListPingSocket();
	m_ServerRowButtons.clear();
	m_vServerEntries.clear();
}

namespace
{
	constexpr float kVoiceMenuOuterMargin = 2.0f;
	constexpr float kVoiceMenuTitleRowHeight = 24.0f;
	constexpr float kVoiceMenuTitleToEnableSpacing = 4.0f;
	constexpr float kVoiceMenuEnableRowHeight = 22.0f;
	constexpr float kVoiceMenuServerRowHeight = 22.0f;
	constexpr float kVoiceMenuServerRowGap = 3.0f;

	float VoiceMenuExpandedHeightForServerCount(int ServerCount, bool RadiusFilterEnabled, bool AutomaticMode, float Team0GroupRevealPhase)
	{
		Team0GroupRevealPhase = std::clamp(Team0GroupRevealPhase, 0.0f, 1.0f);
		const int VisibleRows = std::clamp(ServerCount > 0 ? ServerCount : 1, 1, 2);
		const float ServerListHeight = 2.0f + VisibleRows * kVoiceMenuServerRowHeight + maximum(0, VisibleRows - 1) * kVoiceMenuServerRowGap;
		return 4.0f + 20.0f + // In-Game only checkbox row.
		       4.0f + 20.0f + // Use team0 checkbox row.
		       (4.0f + 20.0f) * Team0GroupRevealPhase + // Enable your group row (animated reveal).
		       4.0f + 20.0f + // Radius filter checkbox row.
		       (RadiusFilterEnabled ? (3.0f + 20.0f) : 0.0f) + // Radius slider row.
		       4.0f + 18.0f + // Activation mode label.
		       3.0f + 22.0f + // Activation mode segmented control.
		       (AutomaticMode ? (3.0f + 20.0f + 3.0f + 20.0f) : 0.0f) + // VAD threshold + release delay rows.
		       4.0f + 20.0f + // Mic check checkbox row.
		       4.0f + 16.0f + // Microphone level meter row.
		       3.0f + 20.0f + // Mic gain slider row.
		       3.0f + 20.0f + // Voice volume slider row.
		       5.0f + 20.0f + 2.0f + 24.0f + // Microphone device.
		       5.0f + 20.0f + 2.0f + 24.0f + // Headphones device.
		       6.0f + 16.0f + // Status.
		       4.0f + 22.0f + // Reload button.
		       5.0f + 16.0f + 2.0f + // Servers label.
		       ServerListHeight +
		       5.0f + 16.0f + 2.0f + 14.0f + 2.0f + 14.0f + 2.0f + 14.0f; // Command hints.
	}
}

float CVoiceChat::GetMenuSettingsBlockHeight(float RevealPhase) const
{
	RevealPhase = std::clamp(RevealPhase, 0.0f, 1.0f);
	const float HeaderHeight =
		kVoiceMenuTitleRowHeight +
		kVoiceMenuTitleToEnableSpacing +
		kVoiceMenuEnableRowHeight;
	const int ServerCount = (int)m_vServerEntries.size();
	const bool RadiusFilterEnabled = g_Config.m_BcVoiceChatRadiusEnabled != 0;
	const bool AutomaticMode = g_Config.m_BcVoiceChatActivationMode == 0;
	const float Team0GroupRevealPhase = BCUiAnimations::EaseOutCubic(std::clamp(m_EnableYourGroupRevealPhase, 0.0f, 1.0f));
	const float ExpandedHeight = VoiceMenuExpandedHeightForServerCount(ServerCount, RadiusFilterEnabled, AutomaticMode, Team0GroupRevealPhase);
	return HeaderHeight + ExpandedHeight * BCUiAnimations::EaseOutCubic(RevealPhase) + kVoiceMenuOuterMargin * 2.0f;
}

void CVoiceChat::RenderMenuSettingsBlock(const CUIRect &View, float RevealPhase)
{
	RevealPhase = std::clamp(RevealPhase, 0.0f, 1.0f);
	CUIRect Area = View;
	Area.Margin(kVoiceMenuOuterMargin, &Area);

	auto ReloadServerList = [&]() {
		ResetServerListTask();
		CloseServerListPingSocket();
		m_vServerEntries.clear();
		m_ServerRowButtons.clear();
		m_LastServerListAutoFetchTick = time_get();
		FetchServerList();
	};

	const bool NeedAutoReload = g_Config.m_BcVoiceChatEnable && Client()->State() == IClient::STATE_ONLINE && m_vServerEntries.empty() &&
				    (!m_pServerListTask || m_pServerListTask->Done() || m_pServerListTask->State() == EHttpState::ERROR || m_pServerListTask->State() == EHttpState::ABORTED);
	if(NeedAutoReload)
		ReloadServerList();

	auto ConnectToServer = [&](const char *pAddress) {
		if(!pAddress || pAddress[0] == '\0')
			return;
		if(str_comp(g_Config.m_BcVoiceChatServerAddress, pAddress) != 0)
		{
			str_copy(g_Config.m_BcVoiceChatServerAddress, pAddress, sizeof(g_Config.m_BcVoiceChatServerAddress));
			str_copy(m_aLastServerAddr, ResolvedVoiceServerAddress(pAddress), sizeof(m_aLastServerAddr));
			if(Client()->State() == IClient::STATE_ONLINE && g_Config.m_BcVoiceChatEnable)
			{
				if(m_Socket)
					StopVoice();
				m_RuntimeState = RUNTIME_RECONNECTING;
				StartVoice();
			}
		}
	};

	auto AddSpacing = [&](float Height) {
		CUIRect Spacing;
		Area.HSplitTop(Height, &Spacing, &Area);
	};

	auto AddRow = [&](float Height, CUIRect &Row) {
		Area.HSplitTop(Height, &Row, &Area);
		return true;
	};

	auto RenderDeviceDropDown = [&](CUIRect &TargetArea, const char *pLabel, int IsCapture, int &ConfigDeviceIndex, CUi::SDropDownState &DropDownState, CScrollRegion &DropDownScrollRegion) {
		auto AddTargetSpacing = [&](float Height) {
			CUIRect Spacing;
			TargetArea.HSplitTop(Height, &Spacing, &TargetArea);
		};
		auto AddTargetRow = [&](float Height, CUIRect &OutRow) {
			TargetArea.HSplitTop(Height, &OutRow, &TargetArea);
			return true;
		};

		CUIRect LabelRow, DropDownRow;
		if(AddTargetRow(20.0f, LabelRow))
			Ui()->DoLabel(&LabelRow, pLabel, 14.0f, TEXTALIGN_ML);
		AddTargetSpacing(2.0f);
		if(!AddTargetRow(24.0f, DropDownRow))
			return;

		int DeviceCount = SDL_GetNumAudioDevices(IsCapture);
		if(DeviceCount < 0)
			DeviceCount = 0;

		std::vector<std::string> vDeviceNames;
		vDeviceNames.reserve((size_t)DeviceCount + 1);
		vDeviceNames.emplace_back(Localize("System default"));
		for(int i = 0; i < DeviceCount; ++i)
		{
			const char *pDeviceName = SDL_GetAudioDeviceName(i, IsCapture);
			if(pDeviceName && pDeviceName[0] != '\0')
				vDeviceNames.emplace_back(pDeviceName);
			else
			{
				char aDevice[32];
				str_format(aDevice, sizeof(aDevice), "Device #%d", i + 1);
				vDeviceNames.emplace_back(aDevice);
			}
		}

		std::vector<const char *> vpDeviceNames;
		vpDeviceNames.reserve(vDeviceNames.size());
		for(const std::string &DeviceName : vDeviceNames)
			vpDeviceNames.push_back(DeviceName.c_str());

		int Selection = ConfigDeviceIndex + 1;
		if(Selection < 0 || Selection >= (int)vpDeviceNames.size())
			Selection = 0;

		DropDownState.m_SelectionPopupContext.m_pScrollRegion = &DropDownScrollRegion;
		const int NewSelection = Ui()->DoDropDown(&DropDownRow, Selection, vpDeviceNames.data(), (int)vpDeviceNames.size(), DropDownState);
		if(NewSelection >= 0 && NewSelection < (int)vpDeviceNames.size() && NewSelection != Selection)
			ConfigDeviceIndex = NewSelection - 1;
	};

	CUIRect Row;
	if(AddRow(kVoiceMenuTitleRowHeight, Row))
		Ui()->DoLabel(&Row, Localize("Voice"), 20.0f, TEXTALIGN_ML);

	AddSpacing(kVoiceMenuTitleToEnableSpacing);
	if(AddRow(kVoiceMenuEnableRowHeight, Row))
	{
		if(GameClient()->m_Menus.DoButton_CheckBox(&m_EnableVoiceButton, Localize("Enable voice chat"), g_Config.m_BcVoiceChatEnable, &Row))
		{
			g_Config.m_BcVoiceChatEnable ^= 1;
			if(!g_Config.m_BcVoiceChatEnable && m_Socket)
				StopVoice();
		}
	}

	if(!g_Config.m_BcVoiceChatEnable || RevealPhase <= 0.0f)
		return;

	const int InitialServerCount = (int)m_vServerEntries.size();
	const bool RadiusFilterEnabled = g_Config.m_BcVoiceChatRadiusEnabled != 0;
	const bool AutomaticMode = g_Config.m_BcVoiceChatActivationMode == 0;
	const float Team0GroupRevealPhase = BCUiAnimations::EaseOutCubic(std::clamp(m_EnableYourGroupRevealPhase, 0.0f, 1.0f));
	const float ExpandedTargetHeight = VoiceMenuExpandedHeightForServerCount(InitialServerCount, RadiusFilterEnabled, AutomaticMode, Team0GroupRevealPhase);
	const float ExpandedVisibleHeight = ExpandedTargetHeight * BCUiAnimations::EaseOutCubic(RevealPhase);

	CUIRect ExpandedVisible;
	Area.HSplitTop(ExpandedVisibleHeight, &ExpandedVisible, &Area);
	Ui()->ClipEnable(&ExpandedVisible);
	struct SScopedClip
	{
		CUi *m_pUi;
		~SScopedClip() { m_pUi->ClipDisable(); }
	} ClipGuard{Ui()};

	CUIRect ExpandedArea = {ExpandedVisible.x, ExpandedVisible.y, ExpandedVisible.w, ExpandedTargetHeight};
	auto AddExpandedSpacing = [&](float Height) {
		CUIRect Spacing;
		ExpandedArea.HSplitTop(Height, &Spacing, &ExpandedArea);
	};
	auto AddExpandedRow = [&](float Height, CUIRect &OutRow) {
		ExpandedArea.HSplitTop(Height, &OutRow, &ExpandedArea);
		return true;
	};

	AddExpandedSpacing(4.0f);
	if(AddExpandedRow(20.0f, Row))
	{
		if(GameClient()->m_Menus.DoButton_CheckBox(&m_InGameOnlyButton, Localize("In-Game Only"), g_Config.m_BcVoiceChatInGameOnly, &Row))
			g_Config.m_BcVoiceChatInGameOnly ^= 1;
	}

	AddExpandedSpacing(4.0f);
	if(AddExpandedRow(20.0f, Row))
	{
		if(GameClient()->m_Menus.DoButton_CheckBox(&m_UseTeam0Button, Localize("Use team0"), g_Config.m_BcVoiceChatUseTeam0, &Row))
		{
			g_Config.m_BcVoiceChatUseTeam0 ^= 1;
			if(g_Config.m_BcVoiceChatUseTeam0 == 0)
				g_Config.m_BcVoiceChatEnableYourGroup = 0;
		}
	}

	const float YourGroupRowPhase = BCUiAnimations::EaseOutCubic(std::clamp(m_EnableYourGroupRevealPhase, 0.0f, 1.0f));
	if(YourGroupRowPhase > 0.0f)
	{
		AddExpandedSpacing(4.0f * YourGroupRowPhase);
		CUIRect ClippedRow;
		if(AddExpandedRow(20.0f * YourGroupRowPhase, ClippedRow) && ClippedRow.h > 0.0f)
		{
			if(GameClient()->m_Menus.DoButton_CheckBox(&m_EnableYourGroupButton, Localize("Enable your group"), g_Config.m_BcVoiceChatEnableYourGroup, &ClippedRow))
				g_Config.m_BcVoiceChatEnableYourGroup ^= 1;
		}
	}

	AddExpandedSpacing(4.0f);
	if(AddExpandedRow(20.0f, Row))
	{
		if(GameClient()->m_Menus.DoButton_CheckBox(&m_RadiusFilterButton, Localize("Radius filter"), g_Config.m_BcVoiceChatRadiusEnabled, &Row))
			g_Config.m_BcVoiceChatRadiusEnabled ^= 1;
	}

	if(g_Config.m_BcVoiceChatRadiusEnabled)
	{
		AddExpandedSpacing(3.0f);
		if(AddExpandedRow(20.0f, Row))
		{
			Ui()->DoScrollbarOption(&g_Config.m_BcVoiceChatRadiusTiles, &g_Config.m_BcVoiceChatRadiusTiles, &Row, Localize("Radius (tiles)"), 1, 500);
		}
	}

	AddExpandedSpacing(4.0f);
	if(AddExpandedRow(18.0f, Row))
		Ui()->DoLabel(&Row, Localize("Activation mode"), 14.0f, TEXTALIGN_ML);
	AddExpandedSpacing(3.0f);
	if(AddExpandedRow(22.0f, Row))
	{
		static CButtonContainer s_ModeAutomaticButton;
		static CButtonContainer s_ModePttButton;
		CUIRect Left, Right;
		Row.VSplitMid(&Left, &Right, 1.0f);
		const bool Automatic = g_Config.m_BcVoiceChatActivationMode == 0;
		const bool Ptt = g_Config.m_BcVoiceChatActivationMode == 1;
		if(GameClient()->m_Menus.DoButton_Menu(&s_ModeAutomaticButton, Localize("Automatic"), Automatic, &Left, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
			g_Config.m_BcVoiceChatActivationMode = 0;
		if(GameClient()->m_Menus.DoButton_Menu(&s_ModePttButton, Localize("Push-to-talk"), Ptt, &Right, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
			g_Config.m_BcVoiceChatActivationMode = 1;
	}
	if(g_Config.m_BcVoiceChatActivationMode == 0)
	{
		AddExpandedSpacing(3.0f);
		if(AddExpandedRow(20.0f, Row))
			Ui()->DoScrollbarOption(&g_Config.m_BcVoiceChatVadThreshold, &g_Config.m_BcVoiceChatVadThreshold, &Row, Localize("VAD threshold (%)"), 0, 100);

		AddExpandedSpacing(3.0f);
		if(AddExpandedRow(20.0f, Row))
			Ui()->DoScrollbarOption(&g_Config.m_BcVoiceChatVadReleaseDelayMs, &g_Config.m_BcVoiceChatVadReleaseDelayMs, &Row, Localize("VAD release delay (ms)"), 0, 1000);
	}

	// Mic check (local loopback), microphone level meter, mic gain and voice volume were only
	// reachable from the removed in-game voice panel in the original client. They live here now
	// so they stay accessible from the settings menu.
	AddExpandedSpacing(4.0f);
	if(AddExpandedRow(20.0f, Row))
	{
		if(GameClient()->m_Menus.DoButton_CheckBox(&m_MicCheckButton, Localize("Mic check (loopback)"), g_Config.m_BcVoiceChatMicCheck, &Row))
			g_Config.m_BcVoiceChatMicCheck ^= 1;
	}

	AddExpandedSpacing(4.0f);
	if(AddExpandedRow(16.0f, Row))
	{
		CUIRect MeterLabel, MeterBarWrap, MeterBar;
		Row.VSplitLeft(100.0f, &MeterLabel, &MeterBarWrap);
		Ui()->DoLabel(&MeterLabel, Localize("Mic level"), 12.0f, TEXTALIGN_ML);
		MeterBarWrap.VSplitLeft(6.0f, nullptr, &MeterBarWrap);
		MeterBarWrap.HMargin(2.0f, &MeterBar);
		MeterBar.Draw(ColorRGBA(0.02f, 0.02f, 0.03f, 0.28f), IGraphics::CORNER_ALL, 3.0f);
		CUIRect Fill = MeterBar;
		Fill.w *= std::clamp(m_MicLevel, 0.0f, 1.0f);
		Fill.Draw(ColorRGBA(0.30f, 0.70f, 0.42f, 0.78f), IGraphics::CORNER_ALL, 3.0f);
	}

	AddExpandedSpacing(3.0f);
	if(AddExpandedRow(20.0f, Row))
		Ui()->DoScrollbarOption(&g_Config.m_BcVoiceChatMicGain, &g_Config.m_BcVoiceChatMicGain, &Row, Localize("Mic gain"), 0, 300, &CUi::ms_LinearScrollbarScale, 0u, "%");

	AddExpandedSpacing(3.0f);
	if(AddExpandedRow(20.0f, Row))
		Ui()->DoScrollbarOption(&g_Config.m_BcVoiceChatVolume, &g_Config.m_BcVoiceChatVolume, &Row, Localize("Voice volume"), 0, 200, &CUi::ms_LogarithmicScrollbarScale, 0u, "%");

	AddExpandedSpacing(5.0f);
	static CScrollRegion s_InputDeviceDropDownScrollRegion;
	static CScrollRegion s_OutputDeviceDropDownScrollRegion;
	RenderDeviceDropDown(ExpandedArea, Localize("Microphone"), 1, g_Config.m_BcVoiceChatInputDevice, m_InputDeviceDropDownState, s_InputDeviceDropDownScrollRegion);
	AddExpandedSpacing(5.0f);
	RenderDeviceDropDown(ExpandedArea, Localize("Headphones"), 0, g_Config.m_BcVoiceChatOutputDevice, m_OutputDeviceDropDownState, s_OutputDeviceDropDownScrollRegion);

	AddExpandedSpacing(6.0f);
	if(AddExpandedRow(16.0f, Row))
	{
		char aStatus[256];
		str_format(aStatus, sizeof(aStatus), "%s: %s",
			Localize("Status"),
			m_Registered ? Localize("Connected") : Localize("Offline"));
		Ui()->DoLabel(&Row, aStatus, 12.0f, TEXTALIGN_ML);
	}

	AddExpandedSpacing(4.0f);
	if(AddExpandedRow(22.0f, Row))
	{
		if(GameClient()->m_Menus.DoButton_Menu(&m_ReloadServerListButton, Localize("Reload servers"), 0, &Row))
			ReloadServerList();
	}

	AddExpandedSpacing(5.0f);
	if(AddExpandedRow(16.0f, Row))
		Ui()->DoLabel(&Row, Localize("Available servers"), 14.0f, TEXTALIGN_ML);
	AddExpandedSpacing(2.0f);

	const int ServerCount = (int)m_vServerEntries.size();
	const int VisibleRows = std::clamp(ServerCount > 0 ? ServerCount : 1, 1, 2);
	const int RowsToRender = minimum(ServerCount, VisibleRows);
	const float ServerListHeight = 2.0f + VisibleRows * kVoiceMenuServerRowHeight + maximum(0, VisibleRows - 1) * kVoiceMenuServerRowGap;
	CUIRect ServerListView;
	if(AddExpandedRow(ServerListHeight, ServerListView))
	{
		if(ServerCount <= 0)
		{
			CUIRect EmptyRow;
			ServerListView.HSplitTop(kVoiceMenuServerRowHeight, &EmptyRow, &ServerListView);
			const bool IsLoadingServerList = m_pServerListTask && !m_pServerListTask->Done();
			Ui()->DoLabel(&EmptyRow, IsLoadingServerList ? Localize("Loading server list...") : Localize("No servers loaded"), 12.0f, TEXTALIGN_ML);
		}
		else
		{
			if(m_ServerRowButtons.size() < m_vServerEntries.size())
				m_ServerRowButtons.resize(m_vServerEntries.size());
			for(int i = 0; i < RowsToRender; ++i)
			{
				const auto &Entry = m_vServerEntries[(size_t)i];
				CUIRect ServerRow;
				ServerListView.HSplitTop(kVoiceMenuServerRowHeight, &ServerRow, &ServerListView);
				char aServerLabel[256];
				if(Entry.m_PingMs >= 0)
					str_format(aServerLabel, sizeof(aServerLabel), "%s (%dms)", Entry.m_Name.c_str(), Entry.m_PingMs);
				else
					str_format(aServerLabel, sizeof(aServerLabel), "%s (--)", Entry.m_Name.c_str());
				const bool Selected = str_comp(ResolvedVoiceServerAddress(Entry.m_Address.c_str()), EffectiveServerAddress().c_str()) == 0;
				if(GameClient()->m_Menus.DoButton_Menu(&m_ServerRowButtons[(size_t)i], aServerLabel, Selected, &ServerRow))
					ConnectToServer(Entry.m_Address.c_str());
				ServerListView.HSplitTop(kVoiceMenuServerRowGap, nullptr, &ServerListView);
			}
		}
	}

	AddExpandedSpacing(5.0f);
	if(AddExpandedRow(16.0f, Row))
		Ui()->DoLabel(&Row, Localize("Voice commands"), 12.0f, TEXTALIGN_ML);
	AddExpandedSpacing(2.0f);
	if(AddExpandedRow(14.0f, Row))
		Ui()->DoLabel(&Row, Localize("!vmute \"name\" / !vunmute \"name\""), 11.0f, TEXTALIGN_ML);
	AddExpandedSpacing(2.0f);
	if(AddExpandedRow(14.0f, Row))
		Ui()->DoLabel(&Row, Localize("!volume \"name\" 0-100"), 11.0f, TEXTALIGN_ML);
	AddExpandedSpacing(2.0f);
	if(AddExpandedRow(14.0f, Row))
		Ui()->DoLabel(&Row, Localize("!vradius on/off/<tiles>"), 11.0f, TEXTALIGN_ML);
}

bool CVoiceChat::TryHandleChatCommand(const char *pLine)
{
	if(!pLine)
		return false;

	const char *p = str_utf8_skip_whitespaces(pLine);
	if(!p || p[0] != '!')
		return false;

	auto SkipWs = [&](const char *&pCur) { pCur = str_utf8_skip_whitespaces(pCur); };
	auto ReadToken = [&](const char *&pCur, char *pOut, int OutSize) -> bool {
		SkipWs(pCur);
		if(!pCur || pCur[0] == '\0')
			return false;
		const char *pStart = pCur;
		if(*pCur == '"')
		{
			pStart = ++pCur;
			while(*pCur && *pCur != '"')
				++pCur;
			const char *pEnd = pCur;
			if(*pCur == '"')
				++pCur;
			str_truncate(pOut, OutSize, pStart, (int)(pEnd - pStart));
			return true;
		}
		while(*pCur && !std::isspace((unsigned char)*pCur))
			++pCur;
		str_truncate(pOut, OutSize, pStart, (int)(pCur - pStart));
		return true;
	};

	// Matches a command keyword at the start of the line and advances p past it.
	// Returns false (without consuming) if the keyword doesn't match a whole token.
	auto MatchCommand = [&](const char *pCommand) -> bool {
		const int Len = str_length(pCommand);
		if(str_comp_nocase_num(p, pCommand, Len) != 0)
			return false;
		const char NextChar = p[Len];
		if(NextChar != '\0' && !std::isspace((unsigned char)NextChar))
			return false;
		p += Len;
		return true;
	};

	if(MatchCommand("!vmute"))
	{
		char aName[128];
		if(!ReadToken(p, aName, sizeof(aName)))
		{
			GameClient()->m_Chat.Echo(Localize("Usage: !vmute \"nickname\""));
			return true;
		}

		const std::string Key = NormalizeVoiceNameKey(aName);
		if(Key.empty())
		{
			GameClient()->m_Chat.Echo(Localize("Voice mute: invalid nickname"));
			return true;
		}

		if(m_MutedNameKeys.find(Key) != m_MutedNameKeys.end())
		{
			GameClient()->m_Chat.Echo(Localize("Voice mute: already muted"));
			return true;
		}
		m_MutedNameKeys.insert(Key);

		char aOut[512];
		WriteVoiceNameList(m_MutedNameKeys, aOut, sizeof(aOut));
		str_copy(g_Config.m_BcVoiceChatMutedNames, aOut, sizeof(g_Config.m_BcVoiceChatMutedNames));
		str_copy(m_aLastMutedNames, g_Config.m_BcVoiceChatMutedNames, sizeof(m_aLastMutedNames));
		m_PeerListDirty = true;

		GameClient()->m_Chat.Echo(Localize("Voice mute: ok"));
		return true;
	}
	if(MatchCommand("!vunmute"))
	{
		char aName[128];
		if(!ReadToken(p, aName, sizeof(aName)))
		{
			GameClient()->m_Chat.Echo(Localize("Usage: !vunmute \"nickname\""));
			return true;
		}

		const std::string Key = NormalizeVoiceNameKey(aName);
		if(Key.empty())
		{
			GameClient()->m_Chat.Echo(Localize("Voice unmute: invalid nickname"));
			return true;
		}

		if(m_MutedNameKeys.find(Key) == m_MutedNameKeys.end())
		{
			GameClient()->m_Chat.Echo(Localize("Voice unmute: nickname not muted"));
			return true;
		}

		m_MutedNameKeys.erase(Key);
		char aOut[512];
		WriteVoiceNameList(m_MutedNameKeys, aOut, sizeof(aOut));
		str_copy(g_Config.m_BcVoiceChatMutedNames, aOut, sizeof(g_Config.m_BcVoiceChatMutedNames));
		str_copy(m_aLastMutedNames, g_Config.m_BcVoiceChatMutedNames, sizeof(m_aLastMutedNames));
		m_PeerListDirty = true;
		GameClient()->m_Chat.Echo(Localize("Voice unmute: ok"));
		return true;
	}
	if(MatchCommand("!volume"))
	{
		char aName[128];
		char aValue[32];
		if(!ReadToken(p, aName, sizeof(aName)) || !ReadToken(p, aValue, sizeof(aValue)))
		{
			GameClient()->m_Chat.Echo(Localize("Usage: !volume \"nickname\" <0-100>"));
			return true;
		}

		const std::string Key = NormalizeVoiceNameKey(aName);
		if(Key.empty())
		{
			GameClient()->m_Chat.Echo(Localize("Voice volume: invalid nickname"));
			return true;
		}

		int Percent = 0;
		if(!str_toint(aValue, &Percent) || Percent < 0 || Percent > 100)
		{
			GameClient()->m_Chat.Echo(Localize("Voice volume: value must be 0-100"));
			return true;
		}
		if(Percent == 100)
			m_NameVolumePercent.erase(Key);
		else
			m_NameVolumePercent[Key] = Percent;

		char aOut[512];
		WriteVoiceNameVolumeList(m_NameVolumePercent, aOut, sizeof(aOut));
		str_copy(g_Config.m_BcVoiceChatNameVolumes, aOut, sizeof(g_Config.m_BcVoiceChatNameVolumes));
		str_copy(m_aLastNameVolumes, g_Config.m_BcVoiceChatNameVolumes, sizeof(m_aLastNameVolumes));
		m_PeerListDirty = true;

		char aMsg[128];
		str_format(aMsg, sizeof(aMsg), Localize("Voice volume: %d%%"), Percent);
		GameClient()->m_Chat.Echo(aMsg);
		return true;
	}
	if(MatchCommand("!vradius"))
	{
		char aArg[32];
		if(!ReadToken(p, aArg, sizeof(aArg)))
		{
			char aMsg[128];
			str_format(aMsg, sizeof(aMsg), Localize("Voice radius: %s (%d tiles)"),
				g_Config.m_BcVoiceChatRadiusEnabled ? Localize("On") : Localize("Off"),
				std::clamp(g_Config.m_BcVoiceChatRadiusTiles, 1, 500));
			GameClient()->m_Chat.Echo(aMsg);
			GameClient()->m_Chat.Echo(Localize("Usage: !vradius on/off/<tiles>"));
			return true;
		}

		auto ParseTiles = [&](const char *pValue, int &OutTiles) -> bool {
			if(!pValue || pValue[0] == '\0')
				return false;
			int Tiles = 0;
			if(!str_toint(pValue, &Tiles))
				return false;
			if(Tiles < 1 || Tiles > 500)
				return false;
			OutTiles = Tiles;
			return true;
		};

		if(str_comp_nocase(aArg, "off") == 0)
		{
			g_Config.m_BcVoiceChatRadiusEnabled = 0;
			char aMsg[128];
			str_format(aMsg, sizeof(aMsg), Localize("Voice radius: %s (%d tiles)"), Localize("Off"), std::clamp(g_Config.m_BcVoiceChatRadiusTiles, 1, 500));
			GameClient()->m_Chat.Echo(aMsg);
			return true;
		}

		if(str_comp_nocase(aArg, "on") == 0)
		{
			int Tiles = std::clamp(g_Config.m_BcVoiceChatRadiusTiles, 1, 500);
			char aTiles[32];
			if(ReadToken(p, aTiles, sizeof(aTiles)))
			{
				if(!ParseTiles(aTiles, Tiles))
				{
					GameClient()->m_Chat.Echo(Localize("Voice radius: tiles must be 1-500"));
					return true;
				}
			}
			g_Config.m_BcVoiceChatRadiusTiles = Tiles;
			g_Config.m_BcVoiceChatRadiusEnabled = 1;

			char aMsg[128];
			str_format(aMsg, sizeof(aMsg), Localize("Voice radius: %s (%d tiles)"), Localize("On"), Tiles);
			GameClient()->m_Chat.Echo(aMsg);
			return true;
		}

		int Tiles = 0;
		if(!ParseTiles(aArg, Tiles))
		{
			GameClient()->m_Chat.Echo(Localize("Usage: !vradius on/off/<tiles>"));
			return true;
		}

		g_Config.m_BcVoiceChatRadiusTiles = Tiles;
		g_Config.m_BcVoiceChatRadiusEnabled = 1;
		char aMsg[128];
	str_format(aMsg, sizeof(aMsg), Localize("Voice radius: %s (%d tiles)"), Localize("On"), Tiles);
		GameClient()->m_Chat.Echo(aMsg);
		return true;
	}

	return false;
}

void CVoiceChat::RenderHudMuteStatusIndicator(float HudWidth, float HudHeight, bool ForcePreview)
{
	const bool ShowMicMuted = g_Config.m_BcVoiceChatMicMuted != 0;
	const bool ShowHeadphonesMuted = g_Config.m_BcVoiceChatHeadphonesMuted != 0;
	if(!ForcePreview && (!HudLayout::IsEnabled(HudLayout::MODULE_VOICE_STATUS) || (!ShowMicMuted && !ShowHeadphonesMuted)))
		return;
	const CUIRect Rect = GetHudMuteStatusIndicatorRect(HudWidth, HudHeight, ForcePreview);
	const auto Layout = HudLayout::Get(HudLayout::MODULE_VOICE_STATUS, HudWidth, HudHeight);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	const float BoxHeight = Rect.h;
	const float BoxWidth = Rect.w;
	const float IconSize = 5.8f * Scale;
	const float Gap = 3.4f * Scale;
	const float Padding = 2.6f * Scale;
	const float DrawX = Rect.x;
	const float DrawY = Rect.y;

	ColorRGBA BackgroundColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.32f);
	if(Layout.m_BackgroundEnabled)
		BackgroundColor = color_cast<ColorRGBA>(ColorHSLA(Layout.m_BackgroundColor, true));
	BackgroundColor = VoiceHudThemeColor(GameClient(), BackgroundColor, ForcePreview, 1.0f);
	BackgroundColor = ApplyVoiceHudAlpha(GameClient(), BackgroundColor);
	const int Corners = VoiceHudBackgroundCorners(HudLayout::MODULE_VOICE_STATUS, DrawX, DrawY, BoxWidth, BoxHeight, HudWidth, HudHeight);
	Graphics()->DrawRect(DrawX, DrawY, BoxWidth, BoxHeight, BackgroundColor, Corners, 2.3f * Scale);

	struct SVoiceStatusIcon
	{
		const char *m_pIcon;
		bool m_Muted;
	};
	const SVoiceStatusIcon aIcons[2] = {
		{FontIcon::MICROPHONE, ShowMicMuted},
		{FontIcon::HEADPHONES, ShowHeadphonesMuted},
	};

	const float TextSize = 5.8f * Scale;
	const float CrossSize = 4.1f * Scale;
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	for(int i = 0; i < 2; ++i)
	{
		const float IconX = DrawX + Padding + i * (IconSize + Gap);
		const float CenterX = IconX + IconSize * 0.5f;
		const float CenterY = DrawY + BoxHeight * 0.5f;
		const bool Muted = ForcePreview ? (i == 0 ? true : ShowHeadphonesMuted || ShowMicMuted) : aIcons[i].m_Muted;
		const ColorRGBA IconColor = Muted ? ColorRGBA(1.0f, 0.35f, 0.35f, 1.0f) : ColorRGBA(1.0f, 1.0f, 1.0f, ForcePreview ? 0.65f : 0.9f);
		TextRender()->TextColor(ApplyVoiceHudAlpha(GameClient(), IconColor));
		const float IconWidth = TextRender()->TextWidth(TextSize, aIcons[i].m_pIcon, -1, -1.0f);
		TextRender()->Text(CenterX - IconWidth * 0.5f, CenterY - TextSize * 0.5f, TextSize, aIcons[i].m_pIcon, -1.0f);
		if(Muted)
		{
			TextRender()->TextColor(ApplyVoiceHudAlpha(GameClient(), ColorRGBA(1.0f, 0.25f, 0.25f, 1.0f)));
			const float CrossWidth = TextRender()->TextWidth(CrossSize, FontIcon::XMARK, -1, -1.0f);
			TextRender()->Text(CenterX - CrossWidth * 0.5f, CenterY - CrossSize * 0.5f, CrossSize, FontIcon::XMARK, -1.0f);
		}
	}
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
}

CUIRect CVoiceChat::GetHudMuteStatusIndicatorRect(float HudWidth, float HudHeight, bool ForcePreview) const
{
	const bool ShowMicMuted = g_Config.m_BcVoiceChatMicMuted != 0;
	const bool ShowHeadphonesMuted = g_Config.m_BcVoiceChatHeadphonesMuted != 0;
	if(!ForcePreview && (!HudLayout::IsEnabled(HudLayout::MODULE_VOICE_STATUS) || (!ShowMicMuted && !ShowHeadphonesMuted)))
		return CUIRect{0.0f, 0.0f, 0.0f, 0.0f};

	const auto Layout = HudLayout::Get(HudLayout::MODULE_VOICE_STATUS, HudWidth, HudHeight);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	const float BoxHeight = 11.0f * Scale;
	const float IconSize = 5.8f * Scale;
	const float Gap = 3.4f * Scale;
	const float Padding = 2.6f * Scale;
	const float BoxWidth = IconSize * 2.0f + Gap + Padding * 2.0f;
	CUIRect Rect = {
		std::clamp(Layout.m_X, 0.0f, maximum(0.0f, HudWidth - BoxWidth)),
		std::clamp(Layout.m_Y, 0.0f, maximum(0.0f, HudHeight - BoxHeight)),
		BoxWidth,
		BoxHeight};
	const bool MusicPlayerComponentDisabled = false;
	const CMusicPlayer::SHudReservation MusicReservation = GameClient()->m_MusicPlayer.HudReservation();
	const bool MusicPlayerHudActive = !MusicPlayerComponentDisabled && g_Config.m_BcMusicPlayer != 0 && MusicReservation.m_Visible && MusicReservation.m_Active;
	if(MusicPlayerHudActive)
	{
		const vec2 Offset = GameClient()->m_MusicPlayer.GetHudPushOffsetForRect(Rect, HudWidth, HudHeight, 2.0f);
		Rect.x += Offset.x;
		Rect.y += Offset.y;
	}

	Rect.x = std::clamp(Rect.x, 0.0f, maximum(0.0f, HudWidth - Rect.w));
	Rect.y = std::clamp(Rect.y, 0.0f, maximum(0.0f, HudHeight - Rect.h));
	return Rect;
}

void CVoiceChat::RenderHudTalkingIndicator(float HudWidth, float HudHeight, bool ForcePreview)
{
	if(!ForcePreview && (!HudLayout::IsEnabled(HudLayout::MODULE_VOICE_TALKERS) || g_Config.m_BcVoiceChatEnable == 0))
		return;
	if(!ForcePreview && g_Config.m_BcVoiceChatHeadphonesMuted != 0)
		return;

	const std::vector<STalkingEntry> &vEntries = m_vTalkingEntries;
	const int LocalClientId = GameClient()->m_Snap.m_LocalClientId;

	if(ForcePreview && vEntries.empty())
	{
		std::vector<STalkingEntry> vPreviewEntries;
		if(LocalClientId >= 0 && LocalClientId < MAX_CLIENTS)
			vPreviewEntries.push_back({LocalClientId, 0, true});

		int PreviewClientId = -1;
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
		{
			if(ClientId == LocalClientId)
				continue;
			if(GameClient()->m_aClients[ClientId].m_Active)
			{
				PreviewClientId = ClientId;
				break;
			}
		}

		if(PreviewClientId >= 0)
			vPreviewEntries.push_back({PreviewClientId, 0, false});

		if(vPreviewEntries.empty())
		{
			vPreviewEntries.push_back({-1, 1, true});
			vPreviewEntries.push_back({-1, 2, false});
		}

		if(vPreviewEntries.empty())
			return;

		const auto Layout = HudLayout::Get(HudLayout::MODULE_VOICE_TALKERS, HudWidth, HudHeight);
		const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.2f, 2.0f);
		const float RowHeight = 12.0f * Scale;
		const float RowGap = 0.8f * Scale;
		const float RowPadding = 1.6f * Scale;
		const float AvatarSize = 7.8f * Scale;
		const float NameGap = 2.3f * Scale;
		const float IconSize = 4.4f * Scale;
		const float IconWidth = 6.8f * Scale;
		const float BoxWidth = 58.0f * Scale;
		const int RenderCount = minimum((int)vPreviewEntries.size(), 2);
		const float BoxHeight = RenderCount * RowHeight + maximum(0, RenderCount - 1) * RowGap;
		float DrawX = Layout.m_X;
		float DrawY = Layout.m_Y;
		DrawX = std::clamp(DrawX, 0.0f, maximum(0.0f, HudWidth - BoxWidth));
		DrawY = std::clamp(DrawY, 0.0f, maximum(0.0f, HudHeight - BoxHeight));

		const bool BackgroundEnabled = Layout.m_BackgroundEnabled;
		const ColorRGBA BackgroundColor = VoiceHudThemeColor(GameClient(), color_cast<ColorRGBA>(ColorHSLA(Layout.m_BackgroundColor, true)), ForcePreview, 1.0f);
		if(BackgroundEnabled)
		{
			const int Corners = VoiceHudBackgroundCorners(HudLayout::MODULE_VOICE_TALKERS, DrawX, DrawY, BoxWidth, BoxHeight, HudWidth, HudHeight);
			Graphics()->DrawRect(DrawX, DrawY, BoxWidth, BoxHeight, ApplyVoiceHudAlpha(GameClient(), BackgroundColor.WithMultipliedAlpha(0.88f)), Corners, 3.1f * Scale);
		}

		for(int Index = 0; Index < RenderCount; ++Index)
		{
			const STalkingEntry &Entry = vPreviewEntries[Index];
			const float RowY = DrawY + Index * (RowHeight + RowGap);
			const int RowCorners = VoiceHudBackgroundCorners(HudLayout::MODULE_VOICE_TALKERS, DrawX, RowY, BoxWidth, RowHeight, HudWidth, HudHeight);
			Graphics()->DrawRect(DrawX, RowY, BoxWidth, RowHeight, ApplyVoiceHudAlpha(GameClient(), VoiceHudThemeColor(GameClient(), ColorRGBA(0.06f, 0.07f, 0.09f, 0.60f), ForcePreview, 1.0f)), RowCorners, 3.1f * Scale);

			const float AvatarX = DrawX + RowPadding;
			const float AvatarY = RowY + (RowHeight - AvatarSize) * 0.5f;
			const float MainX = AvatarX + AvatarSize + NameGap;
			const float MicX = DrawX + BoxWidth - RowPadding - IconWidth;

			char aName[128];
			aName[0] = '\0';
			if(Entry.m_ClientId >= 0 && Entry.m_ClientId < MAX_CLIENTS)
			{
				const auto &ClientData = GameClient()->m_aClients[Entry.m_ClientId];
				str_copy(aName, ClientData.m_aName, sizeof(aName));
				if(ClientData.m_RenderInfo.Valid())
				{
					CTeeRenderInfo TeeInfo = ClientData.m_RenderInfo;
					TeeInfo.m_Size = AvatarSize;
					RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), vec2(AvatarX + AvatarSize * 0.5f, AvatarY + AvatarSize * 0.5f));
				}
			}
			else
			{
				str_format(aName, sizeof(aName), "%s #%u", Localize("Participant"), Entry.m_PeerId);
			}

			if(aName[0] == '\0')
				str_copy(aName, Localize("Participant"), sizeof(aName));

			float NameFontSize = 6.0f * Scale;
			const float MinNameFontSize = 3.5f * Scale;
			const float MaxNameWidth = maximum(0.0f, MicX - MainX - 1.0f * Scale);
			while(NameFontSize > MinNameFontSize && TextRender()->TextWidth(NameFontSize, aName, -1, -1.0f) > MaxNameWidth)
				NameFontSize -= 0.25f * Scale;

			const float TextBaseline = RowY + (RowHeight - NameFontSize) * 0.5f;
			TextRender()->TextColor(ApplyVoiceHudAlpha(GameClient(), ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f)));
			CTextCursor NameCursor;
			NameCursor.m_StartX = MainX;
			NameCursor.m_X = MainX;
			NameCursor.m_StartY = TextBaseline;
			NameCursor.m_Y = TextBaseline;
			NameCursor.m_FontSize = NameFontSize;
			NameCursor.m_LineWidth = MaxNameWidth;
			NameCursor.m_Flags = TEXTFLAG_RENDER | TEXTFLAG_STOP_AT_END | TEXTFLAG_DISALLOW_NEWLINE;
			TextRender()->TextEx(&NameCursor, aName, -1);

			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			const ColorRGBA MicColor = VoiceHudThemeColor(GameClient(), ColorRGBA(0.68f, 1.0f, 0.68f, 0.85f), ForcePreview, 1.0f);
			TextRender()->TextColor(ApplyVoiceHudAlpha(GameClient(), MicColor));
			const float MicGlyphWidth = TextRender()->TextWidth(IconSize, FontIcon::MICROPHONE, -1, -1.0f);
			TextRender()->Text(MicX + (IconWidth - MicGlyphWidth) * 0.5f, RowY + (RowHeight - IconSize) * 0.5f, IconSize, FontIcon::MICROPHONE, -1.0f);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		}

		TextRender()->TextColor(TextRender()->DefaultTextColor());
		return;
	}

	if(vEntries.empty())
		return;

	const auto Layout = HudLayout::Get(HudLayout::MODULE_VOICE_TALKERS, HudWidth, HudHeight);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.2f, 2.0f);
	const float RowHeight = 12.0f * Scale;
	const float RowGap = 0.8f * Scale;
	const float RowPadding = 1.6f * Scale;
	const float AvatarSize = 7.8f * Scale;
	const float NameGap = 2.3f * Scale;
	const float IconSize = 4.4f * Scale;
	const float IconWidth = 6.8f * Scale;
	const float BoxWidth = 58.0f * Scale;
	const int RenderCount = minimum((int)vEntries.size(), ForcePreview ? 2 : 5);
	const float BoxHeight = RenderCount * RowHeight + maximum(0, RenderCount - 1) * RowGap;
	float DrawX = Layout.m_X;
	float DrawY = Layout.m_Y;
	DrawX = std::clamp(DrawX, 0.0f, maximum(0.0f, HudWidth - BoxWidth));
	DrawY = std::clamp(DrawY, 0.0f, maximum(0.0f, HudHeight - BoxHeight));

	const bool BackgroundEnabled = Layout.m_BackgroundEnabled;
	const ColorRGBA BackgroundColor = VoiceHudThemeColor(GameClient(), color_cast<ColorRGBA>(ColorHSLA(Layout.m_BackgroundColor, true)), ForcePreview, 1.0f);
	if(BackgroundEnabled)
	{
		const int Corners = VoiceHudBackgroundCorners(HudLayout::MODULE_VOICE_TALKERS, DrawX, DrawY, BoxWidth, BoxHeight, HudWidth, HudHeight);
		Graphics()->DrawRect(DrawX, DrawY, BoxWidth, BoxHeight, ApplyVoiceHudAlpha(GameClient(), BackgroundColor.WithMultipliedAlpha(0.88f)), Corners, 3.1f * Scale);
	}

	for(int Index = 0; Index < RenderCount; ++Index)
	{
		const STalkingEntry &Entry = vEntries[Index];
		const float RowY = DrawY + Index * (RowHeight + RowGap);
		const int RowCorners = VoiceHudBackgroundCorners(HudLayout::MODULE_VOICE_TALKERS, DrawX, RowY, BoxWidth, RowHeight, HudWidth, HudHeight);
		Graphics()->DrawRect(DrawX, RowY, BoxWidth, RowHeight, ApplyVoiceHudAlpha(GameClient(), VoiceHudThemeColor(GameClient(), ColorRGBA(0.06f, 0.07f, 0.09f, 0.60f), ForcePreview, 1.0f)), RowCorners, 3.1f * Scale);

		const float AvatarX = DrawX + RowPadding;
		const float AvatarY = RowY + (RowHeight - AvatarSize) * 0.5f;
		const float MainX = AvatarX + AvatarSize + NameGap;
		const float MicX = DrawX + BoxWidth - RowPadding - IconWidth;

		char aName[128];
		aName[0] = '\0';
		if(Entry.m_ClientId >= 0 && Entry.m_ClientId < MAX_CLIENTS)
		{
			const auto &ClientData = GameClient()->m_aClients[Entry.m_ClientId];
			str_copy(aName, ClientData.m_aName, sizeof(aName));
			if(ClientData.m_RenderInfo.Valid())
			{
				CTeeRenderInfo TeeInfo = ClientData.m_RenderInfo;
				TeeInfo.m_Size = AvatarSize;
				RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), vec2(AvatarX + AvatarSize * 0.5f, AvatarY + AvatarSize * 0.5f));
			}
		}
		else
		{
			str_format(aName, sizeof(aName), "%s #%u", Localize("Participant"), Entry.m_PeerId);
		}

		if(aName[0] == '\0')
		{
			str_copy(aName, Localize("Participant"), sizeof(aName));
		}

		float NameFontSize = 6.0f * Scale;
		const float MinNameFontSize = 3.5f * Scale;
		const float MaxNameWidth = maximum(0.0f, MicX - MainX - 1.0f * Scale);
		while(NameFontSize > MinNameFontSize && TextRender()->TextWidth(NameFontSize, aName, -1, -1.0f) > MaxNameWidth)
			NameFontSize -= 0.25f * Scale;

		const float TextBaseline = RowY + (RowHeight - NameFontSize) * 0.5f;
		TextRender()->TextColor(ApplyVoiceHudAlpha(GameClient(), ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f)));
		CTextCursor NameCursor;
		NameCursor.m_StartX = MainX;
		NameCursor.m_X = MainX;
		NameCursor.m_StartY = TextBaseline;
		NameCursor.m_Y = TextBaseline;
		NameCursor.m_FontSize = NameFontSize;
		NameCursor.m_LineWidth = MaxNameWidth;
		NameCursor.m_Flags = TEXTFLAG_RENDER | TEXTFLAG_STOP_AT_END | TEXTFLAG_DISALLOW_NEWLINE;
		TextRender()->TextEx(&NameCursor, aName, -1);

		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		const ColorRGBA MicColor = VoiceHudThemeColor(GameClient(), ColorRGBA(0.68f, 1.0f, 0.68f, ForcePreview ? 0.85f : 0.92f), ForcePreview, 1.0f);
		TextRender()->TextColor(ApplyVoiceHudAlpha(GameClient(), MicColor));
		const float MicGlyphWidth = TextRender()->TextWidth(IconSize, FontIcon::MICROPHONE, -1, -1.0f);
		TextRender()->Text(MicX + (IconWidth - MicGlyphWidth) * 0.5f, RowY + (RowHeight - IconSize) * 0.5f, IconSize, FontIcon::MICROPHONE, -1.0f);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	}

	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

CUIRect CVoiceChat::GetHudTalkingIndicatorRect(float HudWidth, float HudHeight, bool ForcePreview) const
{
	if(!ForcePreview && !HudLayout::IsEnabled(HudLayout::MODULE_VOICE_TALKERS))
		return CUIRect{0.0f, 0.0f, 0.0f, 0.0f};
	if(!ForcePreview && g_Config.m_BcVoiceChatHeadphonesMuted != 0)
		return CUIRect{0.0f, 0.0f, 0.0f, 0.0f};

	const std::vector<STalkingEntry> &vEntries = m_vTalkingEntries;
	const int EntryCount = ForcePreview ? 2 : minimum((int)vEntries.size(), 5);
	if(!ForcePreview && EntryCount <= 0)
		return CUIRect{0.0f, 0.0f, 0.0f, 0.0f};

	const auto Layout = HudLayout::Get(HudLayout::MODULE_VOICE_TALKERS, HudWidth, HudHeight);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.2f, 2.0f);
	const float RowHeight = 12.0f * Scale;
	const float RowGap = 0.8f * Scale;
	const float BoxWidth = 58.0f * Scale;
	const float BoxHeight = EntryCount * RowHeight + maximum(0, EntryCount - 1) * RowGap;
	return {std::clamp(Layout.m_X, 0.0f, maximum(0.0f, HudWidth - BoxWidth)), std::clamp(Layout.m_Y, 0.0f, maximum(0.0f, HudHeight - BoxHeight)), BoxWidth, BoxHeight};
}

void CVoiceChat::StartVoice()
{
	if(m_LastStartAttempt == 0)
		m_LastStartAttempt = time_get();
	m_RuntimeState = m_RuntimeState == RUNTIME_RECONNECTING ? RUNTIME_RECONNECTING : RUNTIME_STARTING;
	if(!OpenNetworking())
		return;
	if(!OpenAudioDevices())
	{
		CloseNetworking();
		return;
	}
	if(!CreateEncoder())
	{
		CloseAudioDevices();
		CloseNetworking();
		return;
	}
	m_LastStartAttempt = 0;
	m_HelloResetPending = true;
	SendHello();
}

void CVoiceChat::StopVoice()
{
	DestroyEncoder();
	CloseAudioDevices();
	SendGoodbyeSecondary();
	SendGoodbye();
	CloseNetworking();
	OnReset();
	m_LastStartAttempt = 0;
	m_RuntimeState = RUNTIME_STOPPED;
}

bool CVoiceChat::OpenNetworking()
{
	const std::string ServerAddress = EffectiveServerAddress();
	if(!BestClientVoice::ParseAddress(ServerAddress.c_str(), BestClientVoice::DEFAULT_PORT, m_ServerAddr))
	{
		dbg_msg("voice", "invalid server address '%s'", EffectiveServerLabel());
		return false;
	}

	NETADDR Bind = NETADDR_ZEROED;
	Bind.type = NETTYPE_ALL;
	Bind.port = 0;
	m_Socket = net_udp_create(Bind);
	if(!m_Socket)
	{
		dbg_msg("voice", "failed to open UDP socket");
		return false;
	}
	net_set_non_blocking(m_Socket);
	m_HasServerAddr = true;
	m_Registered = false;
	m_ClientVoiceId = 0;
	m_LastHelloTick = 0;
	m_LastServerPacketTick = 0;
	m_LastHeartbeatTick = 0;
	m_SecondaryLastHelloTick = 0;
	m_SecondaryLastServerPacketTick = 0;
	m_SecondaryLastHeartbeatTick = 0;
	m_SecondaryRegistered = false;
	m_SecondaryClientVoiceId = 0;
	m_SecondarySendSequence = 0;
	m_SecondaryHelloResetPending = false;
	m_AdvertisedTeam = std::numeric_limits<int>::min();
	m_SecondaryAdvertisedTeam = std::numeric_limits<int>::min();
	m_SecondaryAdvertisedRoomKey.clear();
	m_SecondaryAdvertisedPlayerName.clear();
	m_SecondaryAdvertisedGameClientId = BestClientVoice::INVALID_GAME_CLIENT_ID - 1;
	return true;
}

void CVoiceChat::CloseNetworking()
{
	CloseSecondaryNetworking();
	if(m_Socket)
	{
		net_udp_close(m_Socket);
		m_Socket = nullptr;
	}
	m_HasServerAddr = false;
	m_Registered = false;
	m_ClientVoiceId = 0;
	m_LastServerPacketTick = 0;
	m_LastHeartbeatTick = 0;
	m_AdvertisedRoomKey.clear();
	m_AdvertisedPlayerName.clear();
	m_AdvertisedGameClientId = BestClientVoice::INVALID_GAME_CLIENT_ID - 1;
	m_AdvertisedTeam = std::numeric_limits<int>::min();
	// Reset mod state on disconnect
	m_ModAuthed = false;
	m_ModAuthFailed = false;
	m_ModAuthPending = false;
	m_PendingModKey.clear();
	m_vModPlayers.clear();
	m_LastModPlayerListReqTick = 0;
}

bool CVoiceChat::OpenSecondaryNetworking()
{
	if(!m_HasServerAddr)
		return false;
	if(m_SecondarySocket)
		return true;

	NETADDR Bind = NETADDR_ZEROED;
	Bind.type = NETTYPE_ALL;
	Bind.port = 0;
	m_SecondarySocket = net_udp_create(Bind);
	if(!m_SecondarySocket)
	{
		dbg_msg("voice", "failed to open secondary UDP socket");
		return false;
	}
	net_set_non_blocking(m_SecondarySocket);

	m_SecondaryRegistered = false;
	m_SecondaryClientVoiceId = 0;
	m_SecondaryLastHelloTick = 0;
	m_SecondaryLastServerPacketTick = 0;
	m_SecondaryLastHeartbeatTick = 0;
	m_SecondarySendSequence = 0;
	m_SecondaryHelloResetPending = true;
	m_SecondaryAdvertisedRoomKey.clear();
	m_SecondaryAdvertisedGameClientId = BestClientVoice::INVALID_GAME_CLIENT_ID - 1;
	m_SecondaryAdvertisedTeam = std::numeric_limits<int>::min();
	return true;
}

void CVoiceChat::CloseSecondaryNetworking()
{
	if(m_SecondarySocket)
	{
		net_udp_close(m_SecondarySocket);
		m_SecondarySocket = nullptr;
	}
	m_SecondaryRegistered = false;
	m_SecondaryClientVoiceId = 0;
	m_SecondaryLastHelloTick = 0;
	m_SecondaryLastServerPacketTick = 0;
	m_SecondaryLastHeartbeatTick = 0;
	m_SecondarySendSequence = 0;
	m_SecondaryHelloResetPending = false;
	m_SecondaryAdvertisedRoomKey.clear();
	m_SecondaryAdvertisedPlayerName.clear();
	m_SecondaryAdvertisedGameClientId = BestClientVoice::INVALID_GAME_CLIENT_ID - 1;
	m_SecondaryAdvertisedTeam = std::numeric_limits<int>::min();
}

void CVoiceChat::CloseServerListPingSocket()
{
	if(m_ServerListPingSocket)
	{
		net_udp_close(m_ServerListPingSocket);
		m_ServerListPingSocket = nullptr;
	}
	m_LastServerListPingSweepTick = 0;
	for(auto &Entry : m_vServerEntries)
	{
		Entry.m_PingInFlight = false;
		Entry.m_LastPingSendTick = 0;
		Entry.m_PingMs = -1;
	}
}

bool CVoiceChat::OpenAudioDevices()
{
	if(SDL_WasInit(SDL_INIT_AUDIO) == 0)
	{
#ifndef SDL_HINT_AUDIO_INCLUDE_MONITORS
#define SDL_HINT_AUDIO_INCLUDE_MONITORS "SDL_AUDIO_INCLUDE_MONITORS"
#endif
		SDL_SetHint(SDL_HINT_AUDIO_INCLUDE_MONITORS, "1");
		if(SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
		{
			dbg_msg("voice", "failed to init SDL audio: %s", SDL_GetError());
			return false;
		}
	}

	m_CaptureDevice = 0;
	m_PlaybackDevice = 0;
	m_CaptureSpec = {};
	m_PlaybackSpec = {};

	SDL_AudioSpec WantedCapture = {};
	WantedCapture.freq = BestClientVoice::SAMPLE_RATE;
	WantedCapture.format = AUDIO_S16SYS;
	WantedCapture.channels = 1;
	WantedCapture.samples = BestClientVoice::FRAME_SIZE;
	WantedCapture.callback = nullptr;

	const int CaptureDeviceCount = SDL_GetNumAudioDevices(1);
	if(CaptureDeviceCount < 0)
	{
		dbg_msg("voice", "failed to query capture devices, voice transmit disabled: %s", SDL_GetError());
	}
	else if(CaptureDeviceCount == 0)
	{
		// Keep voice chat usable for playback when the system has no microphone.
		dbg_msg("voice", "no capture devices available, voice transmit disabled");
	}
	else
	{
		SDL_AudioSpec ObtainedCapture = {};
		const char *pCaptureDeviceName = GetAudioDeviceNameByIndex(1, g_Config.m_BcVoiceChatInputDevice);
		m_CaptureDevice = SDL_OpenAudioDevice(pCaptureDeviceName, 1, &WantedCapture, &ObtainedCapture, 0);
		if(m_CaptureDevice == 0 && pCaptureDeviceName)
		{
			dbg_msg("voice", "failed to open selected capture device, fallback to default: %s", SDL_GetError());
			m_CaptureDevice = SDL_OpenAudioDevice(nullptr, 1, &WantedCapture, &ObtainedCapture, 0);
		}
		if(m_CaptureDevice == 0)
		{
			dbg_msg("voice", "failed to open capture device, voice transmit disabled: %s", SDL_GetError());
		}
		else
		{
			m_CaptureSpec = ObtainedCapture;
		}
	}

	SDL_AudioSpec WantedPlayback = {};
	WantedPlayback.freq = BestClientVoice::SAMPLE_RATE;
	WantedPlayback.format = AUDIO_S16SYS;
	WantedPlayback.channels = 2;
	WantedPlayback.samples = BestClientVoice::FRAME_SIZE;
	WantedPlayback.callback = nullptr;

	SDL_AudioSpec ObtainedPlayback = {};
	const char *pPlaybackDeviceName = GetAudioDeviceNameByIndex(0, g_Config.m_BcVoiceChatOutputDevice);
	m_PlaybackDevice = SDL_OpenAudioDevice(pPlaybackDeviceName, 0, &WantedPlayback, &ObtainedPlayback, 0);
	if(m_PlaybackDevice == 0 && pPlaybackDeviceName)
	{
		dbg_msg("voice", "failed to open selected playback device, fallback to default: %s", SDL_GetError());
		m_PlaybackDevice = SDL_OpenAudioDevice(nullptr, 0, &WantedPlayback, &ObtainedPlayback, 0);
	}
	if(m_PlaybackDevice == 0)
	{
		dbg_msg("voice", "failed to open playback device: %s", SDL_GetError());
		CloseAudioDevices();
		return false;
	}
	m_PlaybackSpec = ObtainedPlayback;

	if(m_CaptureDevice != 0)
		SDL_PauseAudioDevice(m_CaptureDevice, 0);
	SDL_PauseAudioDevice(m_PlaybackDevice, 0);
	return true;
}

void CVoiceChat::CloseAudioDevices()
{
	if(m_CaptureDevice != 0)
	{
		SDL_CloseAudioDevice(m_CaptureDevice);
		m_CaptureDevice = 0;
	}
	m_CaptureSpec = {};
	if(m_PlaybackDevice != 0)
	{
		SDL_CloseAudioDevice(m_PlaybackDevice);
		m_PlaybackDevice = 0;
	}
	m_PlaybackSpec = {};
}

bool CVoiceChat::CreateEncoder()
{
	int Error = 0;
	m_pEncoder = opus_encoder_create(BestClientVoice::SAMPLE_RATE, BestClientVoice::CHANNELS, OPUS_APPLICATION_VOIP, &Error);
	if(Error != OPUS_OK || !m_pEncoder)
	{
		dbg_msg("voice", "failed to create opus encoder: %d", Error);
		m_pEncoder = nullptr;
		return false;
	}
	m_LastBitrate = std::clamp(g_Config.m_BcVoiceChatBitrate, 6, VOICE_MAX_BITRATE_KBPS);
	ConfigureVoiceOpusEncoder(m_pEncoder, m_LastBitrate);
	m_LastEncoderLossPerc = 0;
	m_LastEncoderFec = 0;
	m_LastEncoderTuneTick = 0;
	return true;
}

void CVoiceChat::DestroyEncoder()
{
	if(m_pEncoder)
	{
		opus_encoder_destroy(m_pEncoder);
		m_pEncoder = nullptr;
	}
	m_LastEncoderLossPerc = -1;
	m_LastEncoderFec = -1;
	m_LastEncoderTuneTick = 0;
}

void CVoiceChat::TuneEncoderForNetwork()
{
	if(!m_pEncoder)
		return;

	const int64_t Now = time_get();
	if(m_LastEncoderTuneTick != 0 && Now - m_LastEncoderTuneTick < time_freq())
		return;

	float LossAvg = 0.0f;
	float JitterMax = 0.0f;
	int ActivePeerCount = 0;
	for(const auto &PeerPair : m_Peers)
	{
		const CRemotePeer &Peer = PeerPair.second;
		if(Peer.m_LastReceiveTick <= 0 || Now - Peer.m_LastReceiveTick > 5 * time_freq())
			continue;

		LossAvg += std::clamp(Peer.m_LossEwma, 0.0f, 1.0f);
		JitterMax = maximum(JitterMax, Peer.m_JitterMs);
		++ActivePeerCount;
	}
	if(ActivePeerCount > 0)
		LossAvg /= (float)ActivePeerCount;

	int TargetLossPerc = 0;
	int TargetFec = 0;
	if(ActivePeerCount > 0)
	{
		if(LossAvg <= 0.01f && JitterMax < 8.0f)
		{
			TargetLossPerc = 0;
			TargetFec = 0;
		}
		else if(LossAvg <= 0.03f)
		{
			TargetLossPerc = 5;
			TargetFec = 1;
		}
		else if(LossAvg <= 0.07f)
		{
			TargetLossPerc = 10;
			TargetFec = 1;
		}
		else
		{
			TargetLossPerc = 20;
			TargetFec = 1;
		}
	}

	if(m_LastEncoderLossPerc != TargetLossPerc)
	{
		opus_encoder_ctl(m_pEncoder, OPUS_SET_PACKET_LOSS_PERC(TargetLossPerc));
		m_LastEncoderLossPerc = TargetLossPerc;
	}
	if(m_LastEncoderFec != TargetFec)
	{
		opus_encoder_ctl(m_pEncoder, OPUS_SET_INBAND_FEC(TargetFec));
		m_LastEncoderFec = TargetFec;
	}
	m_LastEncoderTuneTick = Now;
}

void CVoiceChat::ClearPeerState()
{
	for(auto &PeerPair : m_Peers)
	{
		if(PeerPair.second.m_pDecoder)
		{
			opus_decoder_destroy(PeerPair.second.m_pDecoder);
			PeerPair.second.m_pDecoder = nullptr;
		}
	}
	m_Peers.clear();
	m_PeerVolumePercent.clear();
	m_PeerResolvedClientIds.clear();
	m_vSortedPeerIds.clear();
	m_vVisibleMemberPeerIds.clear();
	m_vTalkingEntries.clear();
	InvalidatePeerCaches();
}

void CVoiceChat::SendHello()
{
	if(!m_Socket || !m_HasServerAddr)
		return;

	const std::string RoomKey = CurrentRoomKey();
	const int LocalClientId = LocalGameClientId();
	const int VoiceTeam = LocalVoiceTeam();
	const uint64_t AuthTimestamp = CurrentHelloAuthTimestamp();
	const uint16_t RoomKeySize = (uint16_t)minimum<size_t>(RoomKey.size(), BestClientVoice::MAX_ROOM_KEY_LENGTH);

	// Build hello body (without HMAC — server will challenge us)
	std::vector<uint8_t> vBody;
	vBody.reserve(20 + RoomKeySize + 2 + BestClientVoice::MAX_PLAYER_NAME_LENGTH);
	BestClientVoice::WriteU16(vBody, BESTCLIENT_VERSIONNR);
	BestClientVoice::WriteU16(vBody, RoomKeySize);
	vBody.insert(vBody.end(), RoomKey.begin(), RoomKey.begin() + RoomKeySize);
	BestClientVoice::WriteS16(vBody, (int16_t)LocalClientId);
	BestClientVoice::WriteS16(vBody, (int16_t)VoiceTeam);
	BestClientVoice::WriteU64(vBody, AuthTimestamp);
	// Append optional player name (new protocol extension; ignored by old servers)
	if(LocalClientId >= 0 && LocalClientId < MAX_CLIENTS)
	{
		const char *pName = GameClient()->m_aClients[LocalClientId].m_aName;
		WriteVoiceString(vBody, pName, BestClientVoice::MAX_PLAYER_NAME_LENGTH);
	}

	// Save for PACKET_HELLO_RESPONSE
	m_PendingHelloPayload = vBody;
	m_ChallengeActive = false;

	std::vector<uint8_t> vPacket;
	vPacket.reserve(8 + vBody.size());
	BestClientVoice::WriteHeader(vPacket, BestClientVoice::PACKET_HELLO);
	vPacket.insert(vPacket.end(), vBody.begin(), vBody.end());

	net_udp_send(m_Socket, &m_ServerAddr, vPacket.data(), (int)vPacket.size());
	m_LastHelloTick = time_get();
	m_LastHeartbeatTick = m_LastHelloTick;
	m_AdvertisedRoomKey.assign(RoomKey.begin(), RoomKey.begin() + RoomKeySize);
	m_AdvertisedGameClientId = LocalClientId;
	m_AdvertisedTeam = VoiceTeam;
	if(LocalClientId >= 0 && LocalClientId < MAX_CLIENTS)
		m_AdvertisedPlayerName = GameClient()->m_aClients[LocalClientId].m_aName;
	else
		m_AdvertisedPlayerName.clear();
}

void CVoiceChat::SendHelloSecondary()
{
	if(!m_SecondarySocket || !m_HasServerAddr)
		return;

	const std::string RoomKey = CurrentRoomKey();
	const int LocalClientId = LocalGameClientId();
	const int VoiceTeam = LocalOwnVoiceTeam();
	const uint64_t AuthTimestamp = CurrentHelloAuthTimestamp();
	const uint16_t RoomKeySize = (uint16_t)minimum<size_t>(RoomKey.size(), BestClientVoice::MAX_ROOM_KEY_LENGTH);

	std::vector<uint8_t> vBody;
	vBody.reserve(20 + RoomKeySize + 2 + BestClientVoice::MAX_PLAYER_NAME_LENGTH);
	BestClientVoice::WriteU16(vBody, BESTCLIENT_VERSIONNR);
	BestClientVoice::WriteU16(vBody, RoomKeySize);
	vBody.insert(vBody.end(), RoomKey.begin(), RoomKey.begin() + RoomKeySize);
	BestClientVoice::WriteS16(vBody, (int16_t)LocalClientId);
	BestClientVoice::WriteS16(vBody, (int16_t)VoiceTeam);
	BestClientVoice::WriteU64(vBody, AuthTimestamp);
	if(LocalClientId >= 0 && LocalClientId < MAX_CLIENTS)
	{
		const char *pName = GameClient()->m_aClients[LocalClientId].m_aName;
		WriteVoiceString(vBody, pName, BestClientVoice::MAX_PLAYER_NAME_LENGTH);
	}

	m_SecondaryPendingHelloPayload = vBody;
	m_SecondaryChallengeActive = false;

	std::vector<uint8_t> vPacket;
	vPacket.reserve(8 + vBody.size());
	BestClientVoice::WriteHeader(vPacket, BestClientVoice::PACKET_HELLO);
	vPacket.insert(vPacket.end(), vBody.begin(), vBody.end());

	net_udp_send(m_SecondarySocket, &m_ServerAddr, vPacket.data(), (int)vPacket.size());
	m_SecondaryLastHelloTick = time_get();
	m_SecondaryLastHeartbeatTick = m_SecondaryLastHelloTick;
	m_SecondaryAdvertisedRoomKey.assign(RoomKey.begin(), RoomKey.begin() + RoomKeySize);
	m_SecondaryAdvertisedGameClientId = LocalClientId;
	m_SecondaryAdvertisedTeam = VoiceTeam;
	if(LocalClientId >= 0 && LocalClientId < MAX_CLIENTS)
		m_SecondaryAdvertisedPlayerName = GameClient()->m_aClients[LocalClientId].m_aName;
	else
		m_SecondaryAdvertisedPlayerName.clear();
}

void CVoiceChat::SendHelloResponse(NETSOCKET Socket, const uint8_t *pNonce, const std::vector<uint8_t> &vHelloPayload)
{
	if(!Socket || !m_HasServerAddr)
		return;

	const std::string AuthKey = VoiceAuthKey();

	// Build message: nonce ‖ hello_payload — HMAC input
	std::vector<uint8_t> vHmacInput;
	vHmacInput.reserve(BestClientVoice::CHALLENGE_NONCE_SIZE + vHelloPayload.size());
	vHmacInput.insert(vHmacInput.end(), pNonce, pNonce + BestClientVoice::CHALLENGE_NONCE_SIZE);
	vHmacInput.insert(vHmacInput.end(), vHelloPayload.begin(), vHelloPayload.end());

	const SHA256_DIGEST Proof = BestClientIndicator::ComputeHmacSha256(
		AuthKey.c_str(), vHmacInput.data(), (int)vHmacInput.size());

	// PACKET_HELLO_RESPONSE: header + [u16 hello_payload_len] + hello_payload + [32-byte HMAC]
	std::vector<uint8_t> vPacket;
	vPacket.reserve(8 + 2 + vHelloPayload.size() + SHA256_DIGEST_LENGTH);
	BestClientVoice::WriteHeader(vPacket, BestClientVoice::PACKET_HELLO_RESPONSE);
	BestClientVoice::WriteU16(vPacket, (uint16_t)vHelloPayload.size());
	vPacket.insert(vPacket.end(), vHelloPayload.begin(), vHelloPayload.end());
	vPacket.insert(vPacket.end(), Proof.data, Proof.data + SHA256_DIGEST_LENGTH);

	net_udp_send(Socket, &m_ServerAddr, vPacket.data(), (int)vPacket.size());
}

void CVoiceChat::SendGoodbye()
{
	if(!m_Socket || !m_HasServerAddr)
		return;

	std::vector<uint8_t> vPacket;
	vPacket.reserve(8);
	BestClientVoice::WriteHeader(vPacket, BestClientVoice::PACKET_GOODBYE);
	net_udp_send(m_Socket, &m_ServerAddr, vPacket.data(), (int)vPacket.size());
}

void CVoiceChat::SendGoodbyeSecondary()
{
	if(!m_SecondarySocket || !m_HasServerAddr)
		return;

	std::vector<uint8_t> vPacket;
	vPacket.reserve(8);
	BestClientVoice::WriteHeader(vPacket, BestClientVoice::PACKET_GOODBYE);
	net_udp_send(m_SecondarySocket, &m_ServerAddr, vPacket.data(), (int)vPacket.size());
}

void CVoiceChat::VoiceModAuth(const char *pKey)
{
	if(!m_Socket || !m_HasServerAddr || !m_Registered)
		return;
	// Store key locally — it is NEVER sent over the network.
	// We send only a challenge request; the server replies with a nonce
	// and we prove knowledge of the key via HMAC-SHA256(nonce, key).
	m_PendingModKey = pKey;
	m_ModAuthPending = true;
	m_ModAuthFailed = false;

	std::vector<uint8_t> vPacket;
	vPacket.reserve(6);
	BestClientVoice::WriteHeader(vPacket, BestClientVoice::PACKET_MOD_AUTH_REQ);
	net_udp_send(m_Socket, &m_ServerAddr, vPacket.data(), (int)vPacket.size());
}

void CVoiceChat::SendModPlayerListReq()
{
	if(!m_Socket || !m_HasServerAddr || !m_Registered || !m_ModAuthed)
		return;
	std::vector<uint8_t> vPacket;
	vPacket.reserve(6);
	BestClientVoice::WriteHeader(vPacket, BestClientVoice::PACKET_MOD_PLAYER_LIST_REQ);
	net_udp_send(m_Socket, &m_ServerAddr, vPacket.data(), (int)vPacket.size());
}

void CVoiceChat::SendModMuteReq(uint16_t SessionId, bool Mute)
{
	if(!m_Socket || !m_HasServerAddr || !m_Registered || !m_ModAuthed)
		return;
	std::vector<uint8_t> vPacket;
	vPacket.reserve(9);
	BestClientVoice::WriteHeader(vPacket, BestClientVoice::PACKET_MOD_MUTE_REQ);
	BestClientVoice::WriteU16(vPacket, SessionId);
	BestClientVoice::WriteU8(vPacket, Mute ? 1 : 0);
	net_udp_send(m_Socket, &m_ServerAddr, vPacket.data(), (int)vPacket.size());
}

void CVoiceChat::SendVoiceFrame(const uint8_t *pOpusData, int OpusSize, int Team, vec2 Position)
{
	if(!m_Socket || !m_Registered || !m_HasServerAddr)
		return;
	if(OpusSize <= 0 || OpusSize > BestClientVoice::MAX_OPUS_PACKET_SIZE)
		return;

	std::vector<uint8_t> vPacket;
	vPacket.reserve(32 + OpusSize);
	BestClientVoice::WriteHeader(vPacket, BestClientVoice::PACKET_VOICE);
	BestClientVoice::WriteS16(vPacket, (int16_t)Team);
	BestClientVoice::WriteS32(vPacket, round_to_int(Position.x));
	BestClientVoice::WriteS32(vPacket, round_to_int(Position.y));
	BestClientVoice::WriteU16(vPacket, m_SendSequence++);
	BestClientVoice::WriteU16(vPacket, (uint16_t)OpusSize);
	vPacket.insert(vPacket.end(), pOpusData, pOpusData + OpusSize);
	net_udp_send(m_Socket, &m_ServerAddr, vPacket.data(), (int)vPacket.size());
}

void CVoiceChat::SendVoiceFrameSecondary(const uint8_t *pOpusData, int OpusSize, int Team, vec2 Position)
{
	if(!m_SecondarySocket || !m_SecondaryRegistered || !m_HasServerAddr)
		return;
	if(OpusSize <= 0 || OpusSize > BestClientVoice::MAX_OPUS_PACKET_SIZE)
		return;

	std::vector<uint8_t> vPacket;
	vPacket.reserve(32 + OpusSize);
	BestClientVoice::WriteHeader(vPacket, BestClientVoice::PACKET_VOICE);
	BestClientVoice::WriteS16(vPacket, (int16_t)Team);
	BestClientVoice::WriteS32(vPacket, round_to_int(Position.x));
	BestClientVoice::WriteS32(vPacket, round_to_int(Position.y));
	BestClientVoice::WriteU16(vPacket, m_SecondarySendSequence++);
	BestClientVoice::WriteU16(vPacket, (uint16_t)OpusSize);
	vPacket.insert(vPacket.end(), pOpusData, pOpusData + OpusSize);
	net_udp_send(m_SecondarySocket, &m_ServerAddr, vPacket.data(), (int)vPacket.size());
}

void CVoiceChat::ProcessVoiceRelayPacket(const uint8_t *pRawData, int DataSize, int Offset, uint16_t SelfVoiceId)
{
	uint16_t SenderId = 0;
	int16_t Team = 0;
	int32_t PosX = 0;
	int32_t PosY = 0;
	uint16_t Sequence = 0;
	uint16_t OpusSize = 0;
	if(!BestClientVoice::ReadU16(pRawData, DataSize, Offset, SenderId) ||
		!BestClientVoice::ReadS16(pRawData, DataSize, Offset, Team) ||
		!BestClientVoice::ReadS32(pRawData, DataSize, Offset, PosX) ||
		!BestClientVoice::ReadS32(pRawData, DataSize, Offset, PosY) ||
		!BestClientVoice::ReadU16(pRawData, DataSize, Offset, Sequence) ||
		!BestClientVoice::ReadU16(pRawData, DataSize, Offset, OpusSize))
	{
		return;
	}
	if(Offset + OpusSize > DataSize || OpusSize == 0 || OpusSize > BestClientVoice::MAX_OPUS_PACKET_SIZE)
		return;
	if(SenderId == SelfVoiceId)
		return;

	auto ItPeer = m_Peers.find(SenderId);
	if(ItPeer == m_Peers.end())
	{
		if((int)m_Peers.size() >= BestClientVoice::MAX_VOICE_PEERS)
			return;
		// Recover quickly even if peer-list packets are delayed/lost for a while.
		CRemotePeer &NewPeer = m_Peers[SenderId];
		NewPeer.m_LastReceiveTick = time_get();
		m_PeerVolumePercent.emplace(SenderId, 100);
		InvalidatePeerCaches();
		ItPeer = m_Peers.find(SenderId);
		if(ItPeer == m_Peers.end())
			return;
	}
	CRemotePeer &Peer = ItPeer->second;
	if(m_PeerVolumePercent.find(SenderId) == m_PeerVolumePercent.end())
		m_PeerVolumePercent[SenderId] = 100;
	Peer.m_Team = Team;
	Peer.m_Position = vec2((float)PosX, (float)PosY);
	if(!IsVoiceTeamAudible(Team))
	{
		// Ignore packets from channels we are not subscribed to, but keep decoder state for
		// the active channel to avoid breaking dual-stream team0 + own-team reception.
		return;
	}

	const int64_t Now = time_get();
	if(Peer.m_LastArrivalTick > 0)
	{
		const float DeltaMs = (float)((Now - Peer.m_LastArrivalTick) * 1000.0 / (double)time_freq());
		const float Deviation = std::fabs(DeltaMs - 20.0f);
		Peer.m_JitterMs = Peer.m_JitterMs <= 0.0f ? Deviation : (0.9f * Peer.m_JitterMs + 0.1f * Deviation);
	}
	Peer.m_LastArrivalTick = Now;
	Peer.m_LastReceiveTick = Now;
	if(!IsPositionWithinRadiusFilter(Peer.m_Position))
	{
		const bool HadBufferedAudio = Peer.m_DecodedPcm.Size() > 0;
		const bool WasTalking = Peer.m_LastVoiceTick > 0;
		Peer.m_LastVoiceTick = 0;
		if(HadBufferedAudio)
			Peer.m_DecodedPcm.Clear();
		if(Peer.m_HasSequence)
		{
			Peer.m_HasSequence = false;
			if(Peer.m_pDecoder)
				opus_decoder_ctl(Peer.m_pDecoder, OPUS_RESET_STATE);
		}
		if(WasTalking || HadBufferedAudio)
			m_TalkingStateDirty = true;
		return;
	}

	if(!Peer.m_pDecoder)
	{
		int Error = 0;
		Peer.m_pDecoder = opus_decoder_create(BestClientVoice::SAMPLE_RATE, BestClientVoice::CHANNELS, &Error);
		if(Error != OPUS_OK || !Peer.m_pDecoder)
		{
			Peer.m_pDecoder = nullptr;
			return;
		}
	}

	int16_t aDecoded[BestClientVoice::FRAME_SIZE];
	if(Peer.m_HasSequence)
	{
		if(!IsForwardSequence(Peer.m_LastSequence, Sequence))
			return;

		const uint16_t Expected = (uint16_t)(Peer.m_LastSequence + 1);
		const uint16_t MissingPackets = (uint16_t)(Sequence - Expected);
		const int DeltaPackets = (int)MissingPackets + 1;
		const float LossRatio = std::clamp(MissingPackets / (float)maximum(DeltaPackets, 1), 0.0f, 1.0f);
		Peer.m_LossEwma = Peer.m_LossEwma <= 0.0f ? LossRatio : (0.9f * Peer.m_LossEwma + 0.1f * LossRatio);
		if(MissingPackets > 0)
		{
			if(MissingPackets > MAX_PACKET_GAP_FOR_PLC)
			{
				Peer.m_DecodedPcm.Clear();
				if(Peer.m_pDecoder)
					opus_decoder_ctl(Peer.m_pDecoder, OPUS_RESET_STATE);
				Peer.m_HasSequence = false;
			}
			else
			{
				// If only one packet is missing, attempt Opus in-band FEC from the current packet.
				if(MissingPackets == 1)
				{
					const int FecSamples = opus_decode(Peer.m_pDecoder, pRawData + Offset, OpusSize, aDecoded, BestClientVoice::FRAME_SIZE, 1);
					if(FecSamples > 0)
					{
						const size_t Dropped = Peer.m_DecodedPcm.PushBack(aDecoded, (size_t)FecSamples);
						if(Dropped > 0 && Peer.m_DecodedPcm.Size() > PLAYBACK_MAX_RESYNC_FRAMES)
							Peer.m_DecodedPcm.DiscardFront(Peer.m_DecodedPcm.Size() - PLAYBACK_MAX_RESYNC_FRAMES);
					}
					else
					{
						const int PlcSamples = opus_decode(Peer.m_pDecoder, nullptr, 0, aDecoded, BestClientVoice::FRAME_SIZE, 0);
						if(PlcSamples > 0)
						{
							const size_t Dropped = Peer.m_DecodedPcm.PushBack(aDecoded, (size_t)PlcSamples);
							if(Dropped > 0 && Peer.m_DecodedPcm.Size() > PLAYBACK_MAX_RESYNC_FRAMES)
								Peer.m_DecodedPcm.DiscardFront(Peer.m_DecodedPcm.Size() - PLAYBACK_MAX_RESYNC_FRAMES);
						}
					}
				}
				else
				{
					for(uint16_t Missing = 0; Missing < MissingPackets; ++Missing)
					{
						const int PlcSamples = opus_decode(Peer.m_pDecoder, nullptr, 0, aDecoded, BestClientVoice::FRAME_SIZE, 0);
						if(PlcSamples <= 0)
							break;
						const size_t Dropped = Peer.m_DecodedPcm.PushBack(aDecoded, (size_t)PlcSamples);
						if(Dropped > 0 && Peer.m_DecodedPcm.Size() > PLAYBACK_MAX_RESYNC_FRAMES)
							Peer.m_DecodedPcm.DiscardFront(Peer.m_DecodedPcm.Size() - PLAYBACK_MAX_RESYNC_FRAMES);
					}
				}
			}
		}
	}

	const int DecodedSamples = opus_decode(Peer.m_pDecoder, pRawData + Offset, OpusSize, aDecoded, BestClientVoice::FRAME_SIZE, 0);
	if(DecodedSamples <= 0)
	{
		++Peer.m_ConsecutiveDecodeFails;
		if(Peer.m_ConsecutiveDecodeFails >= 3 && Peer.m_pDecoder)
		{
			opus_decoder_ctl(Peer.m_pDecoder, OPUS_RESET_STATE);
			Peer.m_ConsecutiveDecodeFails = 0;
		}
		return;
	}

	Peer.m_ConsecutiveDecodeFails = 0;
	Peer.m_LastSequence = Sequence;
	Peer.m_HasSequence = true;
	Peer.m_LastVoiceTick = Now;
	m_TalkingStateDirty = true;

	const size_t Dropped = Peer.m_DecodedPcm.PushBack(aDecoded, (size_t)DecodedSamples);
	if(Dropped > 0 && Peer.m_DecodedPcm.Size() > PLAYBACK_MAX_RESYNC_FRAMES)
		Peer.m_DecodedPcm.DiscardFront(Peer.m_DecodedPcm.Size() - PLAYBACK_MAX_RESYNC_FRAMES);
}

void CVoiceChat::ProcessNetwork()
{
	for(int PacketCount = 0; PacketCount < MAX_RECEIVE_PACKETS_PER_TICK; ++PacketCount)
	{
		NETADDR From = NETADDR_ZEROED;
		unsigned char *pRawData = nullptr;
		const int DataSize = net_udp_recv(m_Socket, &From, &pRawData);
		if(DataSize <= 0 || !pRawData)
			break;

		if(net_addr_comp(&From, &m_ServerAddr) != 0)
			continue;

		int Offset = 0;
		BestClientVoice::EPacketType Type;
		if(!BestClientVoice::ReadHeader(pRawData, DataSize, Type, Offset))
			continue;
		m_LastServerPacketTick = time_get();

		if(Type == BestClientVoice::PACKET_HELLO_CHALLENGE)
		{
			// Server is challenging us — read nonce and send HMAC response
			if((int)(Offset + BestClientVoice::CHALLENGE_NONCE_SIZE) > DataSize)
				continue;
			mem_copy(m_ChallengeNonce, pRawData + Offset, BestClientVoice::CHALLENGE_NONCE_SIZE);
			m_ChallengeActive = true;
			SendHelloResponse(m_Socket, m_ChallengeNonce, m_PendingHelloPayload);
			continue;
		}

		if(Type == BestClientVoice::PACKET_HELLO_ACK)
		{
			uint16_t VoiceId = 0;
			if(BestClientVoice::ReadU16(pRawData, DataSize, Offset, VoiceId))
			{
				const bool FullReset = m_HelloResetPending || !m_Registered || (m_ClientVoiceId != 0 && m_ClientVoiceId != VoiceId);
				if(FullReset)
				{
					ClearPeerState();
					m_SendSequence = 0;
					if(m_PlaybackDevice)
						SDL_ClearQueuedAudio(m_PlaybackDevice);
				}
				m_ClientVoiceId = VoiceId;
				m_Registered = true;
				m_RuntimeState = RUNTIME_REGISTERED;
				m_HelloResetPending = false;
			}
			continue;
		}

		if(Type == BestClientVoice::PACKET_PEER_LIST)
		{
			if(!m_Registered || m_ClientVoiceId == 0)
				continue;

			uint16_t PeerCount = 0;
			if(!BestClientVoice::ReadU16(pRawData, DataSize, Offset, PeerCount))
				continue;

			std::unordered_set<uint16_t> vSeenPeerIds;
			vSeenPeerIds.reserve(PeerCount);
			const int64_t Now = time_get();
			bool ParseOk = true;

			for(uint16_t i = 0; i < PeerCount; ++i)
			{
				uint16_t PeerId = 0;
				if(!BestClientVoice::ReadU16(pRawData, DataSize, Offset, PeerId))
				{
					ParseOk = false;
					break;
				}
				if(PeerId == 0 || PeerId == m_ClientVoiceId)
					continue;
				vSeenPeerIds.insert(PeerId);
				if(m_Peers.find(PeerId) == m_Peers.end() && (int)m_Peers.size() >= BestClientVoice::MAX_VOICE_PEERS)
					continue;
				CRemotePeer &Peer = m_Peers[PeerId];
				Peer.m_LastReceiveTick = Now;
			}
			if(!ParseOk)
				continue;

			for(auto It = m_Peers.begin(); It != m_Peers.end();)
			{
				if(vSeenPeerIds.find(It->first) == vSeenPeerIds.end())
				{
					if(It->second.m_pDecoder)
						opus_decoder_destroy(It->second.m_pDecoder);
					m_PeerVolumePercent.erase(It->first);
					It = m_Peers.erase(It);
				}
				else
				{
					++It;
				}
			}
			InvalidatePeerCaches();
			continue;
		}

		if(Type == BestClientVoice::PACKET_PEER_LIST_EX)
		{
			if(!m_Registered || m_ClientVoiceId == 0)
				continue;

			uint16_t PeerCount = 0;
			if(!BestClientVoice::ReadU16(pRawData, DataSize, Offset, PeerCount))
				continue;

			std::unordered_set<uint16_t> vSeenPeerIds;
			vSeenPeerIds.reserve(PeerCount);
			const int64_t Now = time_get();
			bool ParseOk = true;
			for(uint16_t i = 0; i < PeerCount; ++i)
			{
				uint16_t PeerId = 0;
				int16_t AnnouncedGameClientId = BestClientVoice::INVALID_GAME_CLIENT_ID;
				if(!BestClientVoice::ReadU16(pRawData, DataSize, Offset, PeerId) ||
					!BestClientVoice::ReadS16(pRawData, DataSize, Offset, AnnouncedGameClientId))
				{
					ParseOk = false;
					break;
				}
				if(PeerId == 0 || PeerId == m_ClientVoiceId)
					continue;
				vSeenPeerIds.insert(PeerId);
				if(m_Peers.find(PeerId) == m_Peers.end() && (int)m_Peers.size() >= BestClientVoice::MAX_VOICE_PEERS)
					continue;
				CRemotePeer &Peer = m_Peers[PeerId];
				Peer.m_LastReceiveTick = Now;
				Peer.m_AnnouncedGameClientId = AnnouncedGameClientId;
			}
			if(!ParseOk)
				continue;

			for(auto It = m_Peers.begin(); It != m_Peers.end();)
			{
				if(vSeenPeerIds.find(It->first) == vSeenPeerIds.end())
				{
					if(It->second.m_pDecoder)
						opus_decoder_destroy(It->second.m_pDecoder);
					m_PeerVolumePercent.erase(It->first);
					It = m_Peers.erase(It);
				}
				else
				{
					++It;
				}
			}
			InvalidatePeerCaches();
			continue;
		}

		// Moderator control packets
		if(Type == BestClientVoice::PACKET_MOD_AUTH_CHALLENGE)
		{
			// Server sent a nonce — respond with HMAC-SHA256(nonce, mod_key)
			// The raw key is never sent over the network
			if(!m_ModAuthPending || m_PendingModKey.empty())
				continue;
			if(DataSize - Offset < BestClientVoice::MOD_NONCE_SIZE)
				continue;
			const uint8_t *pNonce = pRawData + Offset;

			const SHA256_DIGEST Proof = BestClientIndicator::ComputeHmacSha256(
				m_PendingModKey.c_str(), pNonce, BestClientVoice::MOD_NONCE_SIZE);
			m_PendingModKey.clear(); // key no longer needed; erase from memory

			std::vector<uint8_t> vResp;
			vResp.reserve(6 + SHA256_DIGEST_LENGTH);
			BestClientVoice::WriteHeader(vResp, BestClientVoice::PACKET_MOD_AUTH_RESPONSE);
			vResp.insert(vResp.end(), Proof.data, Proof.data + SHA256_DIGEST_LENGTH);
			net_udp_send(m_Socket, &m_ServerAddr, vResp.data(), (int)vResp.size());
			continue;
		}

		if(Type == BestClientVoice::PACKET_MOD_AUTH_ACK)
		{
			uint8_t Status = 0;
			if(BestClientVoice::ReadU8(pRawData, DataSize, Offset, Status))
			{
				m_ModAuthPending = false;
				m_PendingModKey.clear();
				if(Status == 0)
				{
					m_ModAuthed = true;
					m_ModAuthFailed = false;
					m_LastModPlayerListReqTick = 0; // trigger immediate refresh
				}
				else
				{
					m_ModAuthed = false;
					m_ModAuthFailed = true;
				}
			}
			continue;
		}

		if(Type == BestClientVoice::PACKET_MOD_PLAYER_LIST)
		{
			if(!m_ModAuthed)
				continue;
			uint16_t Count = 0;
			if(!BestClientVoice::ReadU16(pRawData, DataSize, Offset, Count))
				continue;
			m_vModPlayers.clear();
			m_vModPlayers.reserve(Count);
			bool ParseOk = true;
			for(uint16_t i = 0; i < Count; ++i)
			{
				uint16_t SessionId = 0;
				int16_t GameClientId = -1;
				std::string Name;
				uint8_t IsMuted = 0;
				if(!BestClientVoice::ReadU16(pRawData, DataSize, Offset, SessionId) ||
					!BestClientVoice::ReadS16(pRawData, DataSize, Offset, GameClientId) ||
					!ReadVoiceString(pRawData, DataSize, Offset, Name, BestClientVoice::MAX_PLAYER_NAME_LENGTH) ||
					!BestClientVoice::ReadU8(pRawData, DataSize, Offset, IsMuted))
				{
					ParseOk = false;
					break;
				}
				SModPlayer Player;
				Player.m_SessionId = SessionId;
				Player.m_GameClientId = GameClientId;
				Player.m_Name = std::move(Name);
				Player.m_IsMuted = IsMuted != 0;
				m_vModPlayers.push_back(std::move(Player));
			}
			if(!ParseOk)
				m_vModPlayers.clear();
			continue;
		}

		if(Type == BestClientVoice::PACKET_MOD_MUTE_ACK)
		{
			uint16_t SessionId = 0;
			uint8_t IsMuted = 0;
			if(BestClientVoice::ReadU16(pRawData, DataSize, Offset, SessionId) &&
				BestClientVoice::ReadU8(pRawData, DataSize, Offset, IsMuted))
			{
				for(auto &Player : m_vModPlayers)
				{
					if(Player.m_SessionId == SessionId)
					{
						Player.m_IsMuted = IsMuted != 0;
						break;
					}
				}
			}
			continue;
		}

		if(Type == BestClientVoice::PACKET_YOU_ARE_MUTED)
		{
			const int64_t NowTick = time_get();
			m_IsMutedByMod = true;
			// Show chat notification at most once every 3 seconds to avoid spam
			if(m_MutedByModNotifyTick == 0 || NowTick - m_MutedByModNotifyTick > time_freq() * 3)
			{
				m_MutedByModNotifyTick = NowTick;
				GameClient()->m_Chat.Echo(Localize("You are muted. Your voice was muted by a moderator."));
			}
			continue;
		}

		if(Type != BestClientVoice::PACKET_VOICE_RELAY)
			continue;

		if(!m_Registered || m_ClientVoiceId == 0)
			continue;
		if(IsInGameOnlyBlocked())
			continue;

		ProcessVoiceRelayPacket(pRawData, DataSize, Offset, m_ClientVoiceId);
	}
}

void CVoiceChat::ProcessSecondaryNetwork()
{
	if(!m_SecondarySocket)
		return;

	for(int PacketCount = 0; PacketCount < MAX_RECEIVE_PACKETS_PER_TICK; ++PacketCount)
	{
		NETADDR From = NETADDR_ZEROED;
		unsigned char *pRawData = nullptr;
		const int DataSize = net_udp_recv(m_SecondarySocket, &From, &pRawData);
		if(DataSize <= 0 || !pRawData)
			break;

		if(net_addr_comp(&From, &m_ServerAddr) != 0)
			continue;

		int Offset = 0;
		BestClientVoice::EPacketType Type;
		if(!BestClientVoice::ReadHeader(pRawData, DataSize, Type, Offset))
			continue;
		m_SecondaryLastServerPacketTick = time_get();

		if(Type == BestClientVoice::PACKET_HELLO_CHALLENGE)
		{
			if((int)(Offset + BestClientVoice::CHALLENGE_NONCE_SIZE) > DataSize)
				continue;
			mem_copy(m_SecondaryChallengeNonce, pRawData + Offset, BestClientVoice::CHALLENGE_NONCE_SIZE);
			m_SecondaryChallengeActive = true;
			SendHelloResponse(m_SecondarySocket, m_SecondaryChallengeNonce, m_SecondaryPendingHelloPayload);
			continue;
		}

		if(Type == BestClientVoice::PACKET_HELLO_ACK)
		{
			uint16_t VoiceId = 0;
			if(BestClientVoice::ReadU16(pRawData, DataSize, Offset, VoiceId))
			{
				const bool FullReset = m_SecondaryHelloResetPending || !m_SecondaryRegistered || (m_SecondaryClientVoiceId != 0 && m_SecondaryClientVoiceId != VoiceId);
				if(FullReset)
					m_SecondarySendSequence = 0;
				m_SecondaryClientVoiceId = VoiceId;
				m_SecondaryRegistered = true;
				m_SecondaryHelloResetPending = false;
			}
			continue;
		}

		if(Type != BestClientVoice::PACKET_VOICE_RELAY)
			continue;

		if(!m_SecondaryRegistered || m_SecondaryClientVoiceId == 0)
			continue;
		if(IsInGameOnlyBlocked())
			continue;

		ProcessVoiceRelayPacket(pRawData, DataSize, Offset, m_SecondaryClientVoiceId);
	}
}

void CVoiceChat::ProcessServerListPing()
{
	if(!m_ServerListPingSocket || m_vServerEntries.empty())
		return;

	const int64_t Now = time_get();
	if(m_LastProcessNetworkTick != 0 && Now - m_LastProcessNetworkTick < time_freq() / 10)
		return;
	m_LastProcessNetworkTick = Now;

	const int64_t TimeoutTicks = SERVER_LIST_PING_TIMEOUT_SEC * time_freq();
	const int64_t SweepTicks = SERVER_LIST_PING_INTERVAL_SEC * time_freq();
	if(m_LastServerListPingSweepTick == 0 || Now - m_LastServerListPingSweepTick >= SweepTicks)
		StartServerListPings();

	for(auto &Entry : m_vServerEntries)
	{
		if(Entry.m_PingInFlight && Entry.m_LastPingSendTick > 0 && Now - Entry.m_LastPingSendTick > TimeoutTicks)
		{
			Entry.m_PingInFlight = false;
			Entry.m_PingMs = -1;
		}
	}

	for(int PacketCount = 0; PacketCount < MAX_RECEIVE_PACKETS_PER_TICK; ++PacketCount)
	{
		NETADDR From = NETADDR_ZEROED;
		unsigned char *pRawData = nullptr;
		const int DataSize = net_udp_recv(m_ServerListPingSocket, &From, &pRawData);
		if(DataSize <= 0 || !pRawData)
			break;

		int Offset = 0;
		BestClientVoice::EPacketType Type;
		if(!BestClientVoice::ReadHeader(pRawData, DataSize, Type, Offset))
			continue;
		if(Type != BestClientVoice::PACKET_PONG)
			continue;
		uint16_t Token = 0;
		if(!BestClientVoice::ReadU16(pRawData, DataSize, Offset, Token))
			continue;

		for(auto &Entry : m_vServerEntries)
		{
			if(!Entry.m_HasAddr)
				continue;
			if(net_addr_comp(&From, &Entry.m_Addr) != 0)
				continue;
			if(!Entry.m_PingInFlight || Entry.m_LastPingSendTick <= 0 || Entry.m_PingToken != Token)
				continue;
			const int LatencyMs = (int)((time_get() - Entry.m_LastPingSendTick) * 1000 / time_freq());
			Entry.m_PingMs = std::clamp(LatencyMs, 0, 999);
			Entry.m_PingInFlight = false;
			break;
		}
	}
}

void CVoiceChat::ProcessCapture()
{
	if(!m_CaptureDevice || !m_pEncoder)
		return;
	if(IsInGameOnlyBlocked())
	{
		m_LastProcessCaptureTick = time_get();
		m_WasTransmitActive = false;
		m_AutoActivationUntilTick = 0;
		m_AutoNsNoiseFloor = 0.0f;
		m_AutoNsGate = 1.0f;
		m_AutoHpfPrevIn = 0.0f;
		m_AutoHpfPrevOut = 0.0f;
		m_AutoCompEnv = 0.0f;
		m_MicLevel = mix(m_MicLevel, 0.0f, 0.25f);
		m_CapturePcm.Clear();
		m_MicMonitorPcm.Clear();
		if(SDL_GetQueuedAudioSize(m_CaptureDevice) > 0)
			SDL_ClearQueuedAudio(m_CaptureDevice);
		return;
	}
	if(g_Config.m_BcVoiceChatActivationMode == 1 && !m_PushToTalkPressed && !g_Config.m_BcVoiceChatMicCheck)
	{
		m_LastProcessCaptureTick = time_get();
		m_WasTransmitActive = false;
		m_AutoActivationUntilTick = 0;
		m_AutoNsNoiseFloor = 0.0f;
		m_AutoNsGate = 1.0f;
		m_AutoHpfPrevIn = 0.0f;
		m_AutoHpfPrevOut = 0.0f;
		m_AutoCompEnv = 0.0f;
		m_MicLevel = mix(m_MicLevel, 0.0f, 0.25f);
		if(SDL_GetQueuedAudioSize(m_CaptureDevice) > 0)
			SDL_ClearQueuedAudio(m_CaptureDevice);
		return;
	}

	int16_t aCaptureRaw[CAPTURE_READ_SAMPLES * 2];
	const int BytesRead = SDL_DequeueAudio(m_CaptureDevice, aCaptureRaw, sizeof(aCaptureRaw));
	if(BytesRead > 0)
	{
		m_LastProcessCaptureTick = time_get();
		const int SamplesRead = BytesRead / (int)sizeof(int16_t);
		if(m_CaptureSpec.channels <= 1)
		{
			m_CapturePcm.PushBack(aCaptureRaw, (size_t)SamplesRead);
		}
		else
		{
			const int ChannelCount = m_CaptureSpec.channels;
			const int Frames = SamplesRead / ChannelCount;
			int16_t aMixedCapture[CAPTURE_READ_SAMPLES];
			for(int i = 0; i < Frames; ++i)
			{
				int Sum = 0;
				for(int c = 0; c < ChannelCount; ++c)
					Sum += aCaptureRaw[i * ChannelCount + c];
				aMixedCapture[i] = (int16_t)(Sum / ChannelCount);
			}
			m_CapturePcm.PushBack(aMixedCapture, (size_t)Frames);
		}
	}
	else if(m_CapturePcm.Size() == 0)
	{
		const int64_t Now = time_get();
		const bool ExpectingCaptureData = m_Registered && !g_Config.m_BcVoiceChatMicMuted &&
						  (g_Config.m_BcVoiceChatMicCheck || g_Config.m_BcVoiceChatActivationMode == 0 || m_PushToTalkPressed);
		if(ExpectingCaptureData)
		{
			if(m_LastProcessCaptureTick == 0)
			{
				m_LastProcessCaptureTick = Now;
			}
			else if(Now - m_LastProcessCaptureTick > 3 * time_freq())
			{
				dbg_msg("voice", "capture stalled, restarting voice pipeline");
				StopVoice();
				m_RuntimeState = RUNTIME_RECONNECTING;
				StartVoice();
				m_LastProcessCaptureTick = Now;
				return;
			}
		}
		else
		{
			m_LastProcessCaptureTick = Now;
		}

		m_MicLevel = mix(m_MicLevel, 0.0f, 0.08f);
	}

	while(m_CapturePcm.Size() >= (size_t)BestClientVoice::FRAME_SIZE)
	{
		int16_t aFrameRaw[BestClientVoice::FRAME_SIZE];
		int16_t aFrame[BestClientVoice::FRAME_SIZE];
		m_CapturePcm.PopFront(aFrameRaw, BestClientVoice::FRAME_SIZE);

		const int MicGainPercent = std::clamp(g_Config.m_BcVoiceChatMicGain, 0, 300);
		int PeakBeforeLimiter = 0;
		int aScaled[BestClientVoice::FRAME_SIZE];
		for(int i = 0; i < BestClientVoice::FRAME_SIZE; ++i)
		{
			const int Scaled = (aFrameRaw[i] * MicGainPercent) / 100;
			aScaled[i] = Scaled;
			PeakBeforeLimiter = maximum(PeakBeforeLimiter, absolute(Scaled));
		}
		const int TargetPeak = 30000;
		const float RequiredLimiterScale = (PeakBeforeLimiter > TargetPeak && PeakBeforeLimiter > 0) ? (TargetPeak / (float)PeakBeforeLimiter) : 1.0f;
		if(RequiredLimiterScale < m_MicLimiterGain)
			m_MicLimiterGain = mix(m_MicLimiterGain, RequiredLimiterScale, 0.45f);
		else
			m_MicLimiterGain = mix(m_MicLimiterGain, RequiredLimiterScale, 0.05f);
		m_MicLimiterGain = std::clamp(m_MicLimiterGain, 0.10f, 1.0f);

		int Peak = 0;
		for(int i = 0; i < BestClientVoice::FRAME_SIZE; ++i)
		{
			const int Out = round_to_int(aScaled[i] * m_MicLimiterGain);
			aFrame[i] = (int16_t)std::clamp(Out, (int)std::numeric_limits<int16_t>::min(), (int)std::numeric_limits<int16_t>::max());
			Peak = maximum(Peak, absolute(Out));
		}

		const bool AutoMode = g_Config.m_BcVoiceChatActivationMode == 0;
		if(AutoMode)
		{
			// Keep the pre-compressor Peak for the VAD threshold comparison below:
			// ApplyAutoHpfCompressor hard-limits its output to 50% of full scale, which would
			// otherwise make the upper half of the VAD threshold slider (50-100%) unreachable.
			ApplyAutoNoiseSuppressorSimple(aFrame, BestClientVoice::FRAME_SIZE, 0.50f, m_AutoNsNoiseFloor, m_AutoNsGate);
			ApplyAutoHpfCompressor(aFrame, BestClientVoice::FRAME_SIZE, m_AutoHpfPrevIn, m_AutoHpfPrevOut, m_AutoCompEnv);
		}
		else
		{
			m_AutoNsNoiseFloor = 0.0f;
			m_AutoNsGate = 1.0f;
			m_AutoHpfPrevIn = 0.0f;
			m_AutoHpfPrevOut = 0.0f;
			m_AutoCompEnv = 0.0f;
		}

		int64_t AvgLevel = 0;
		for(int i = 0; i < BestClientVoice::FRAME_SIZE; ++i)
			AvgLevel += absolute(aFrame[i]);
		AvgLevel /= BestClientVoice::FRAME_SIZE;
		const float LevelLinear = std::clamp((float)AvgLevel / 32767.0f, 0.0f, 1.0f);
		const float PeakLinear = std::clamp((float)Peak / 32767.0f, 0.0f, 1.0f);
		const float MicLevelScale = 2.0f;
		const float LevelLinearScaled = std::clamp(LevelLinear * MicLevelScale, 0.0f, 1.0f);
		m_MicLevel = mix(m_MicLevel, LevelLinearScaled, 0.2f);

		if(g_Config.m_BcVoiceChatMicCheck)
			m_MicMonitorPcm.PushBack(aFrame, BestClientVoice::FRAME_SIZE);

		if(!ShouldTransmit())
		{
			m_WasTransmitActive = false;
			m_AutoActivationUntilTick = 0;
			m_AutoNsNoiseFloor = 0.0f;
			m_AutoNsGate = 1.0f;
			m_AutoHpfPrevIn = 0.0f;
			m_AutoHpfPrevOut = 0.0f;
			m_AutoCompEnv = 0.0f;
			continue;
		}

		const int64_t NowTick = time_get();
		bool Active = false;
		if(g_Config.m_BcVoiceChatActivationMode == 1)
		{
			Active = m_PushToTalkPressed;
		}
		else
		{
			const float VadThreshold = std::clamp(g_Config.m_BcVoiceChatVadThreshold / 100.0f, 0.0f, 1.0f);
			const int VadReleaseMs = std::clamp(g_Config.m_BcVoiceChatVadReleaseDelayMs, 0, 1000);
			const int64_t VadReleaseTicks = (int64_t)time_freq() * VadReleaseMs / 1000;
			const bool Trigger = VadThreshold <= 0.0f || PeakLinear >= VadThreshold;

			if(Trigger)
			{
				Active = true;
				// Always land in the future (even with 0ms release delay) so IsClientTalking(),
				// which requires m_AutoActivationUntilTick > 0, reflects the current frame's activity.
				m_AutoActivationUntilTick = NowTick + maximum<int64_t>(VadReleaseTicks, 1);
			}
			else if(VadReleaseTicks > 0 && m_AutoActivationUntilTick > 0 && NowTick <= m_AutoActivationUntilTick)
			{
				Active = true;
			}
			else
			{
				Active = false;
				m_AutoActivationUntilTick = 0;
			}
		}
		if(!Active)
		{
			m_WasTransmitActive = false;
			continue;
		}

		if(!m_WasTransmitActive)
		{
			opus_encoder_ctl(m_pEncoder, OPUS_RESET_STATE);
			m_WasTransmitActive = true;
		}

		uint8_t aEncoded[BestClientVoice::MAX_OPUS_PACKET_SIZE];
		const int EncodedSize = opus_encode(m_pEncoder, aFrame, BestClientVoice::FRAME_SIZE, aEncoded, (int)sizeof(aEncoded));
		if(EncodedSize > 0)
		{
			const vec2 Position = LocalPosition();
			const int PrimaryTeam = LocalVoiceTeam();
			SendVoiceFrame(aEncoded, EncodedSize, PrimaryTeam, Position);

			if(ShouldUseSecondaryTeamConnection())
			{
				const int OwnTeam = LocalOwnVoiceTeam();
				SendVoiceFrameSecondary(aEncoded, EncodedSize, OwnTeam, Position);
			}
		}
	}
}

void CVoiceChat::ProcessPlayback()
{
	if(!m_PlaybackDevice)
		return;
	if(IsInGameOnlyBlocked())
	{
		if(SDL_GetQueuedAudioSize(m_PlaybackDevice) > 0)
			SDL_ClearQueuedAudio(m_PlaybackDevice);
		m_MicMonitorPcm.Clear();
		for(auto &PeerPair : m_Peers)
		{
			PeerPair.second.m_DecodedPcm.Clear();
			PeerPair.second.m_LastVoiceTick = 0;
		}
		m_TalkingStateDirty = true;
		return;
	}
	if(!HasPendingPlaybackAudio() && SDL_GetQueuedAudioSize(m_PlaybackDevice) == 0)
		return;

	Uint32 QueuedBytes = SDL_GetQueuedAudioSize(m_PlaybackDevice);
	const Uint32 TargetBytes = (Uint32)(PLAYBACK_TARGET_FRAMES * 2 * sizeof(int16_t));
	const Uint32 MaxQueuedBytes = TargetBytes * 3u;
	if(QueuedBytes > MaxQueuedBytes)
	{
		SDL_ClearQueuedAudio(m_PlaybackDevice);
		QueuedBytes = 0;
	}
	if(QueuedBytes >= TargetBytes)
		return;

	const float MasterVolume = g_Config.m_BcVoiceChatHeadphonesMuted ? 0.0f : (g_Config.m_BcVoiceChatVolume / 100.0f);

	while(SDL_GetQueuedAudioSize(m_PlaybackDevice) < TargetBytes)
	{
		int16_t aOut[BestClientVoice::FRAME_SIZE * 2];
		mem_zero(aOut, sizeof(aOut));
		int aMix[BestClientVoice::FRAME_SIZE * 2];
		mem_zero(aMix, sizeof(aMix));

		bool MixedAnySamples = false;
		const float MicCheckGain = g_Config.m_BcVoiceChatMicCheck ? 0.75f * MasterVolume : 0.0f;
		if(MicCheckGain <= 0.0f)
		{
			m_MicMonitorPcm.DiscardFront(minimum(m_MicMonitorPcm.Size(), (size_t)BestClientVoice::FRAME_SIZE));
		}
		else if(m_MicMonitorPcm.Size() >= (size_t)BestClientVoice::FRAME_SIZE)
		{
			// Only pop once a full frame has accumulated; popping a short frame and zero-padding
			// the rest (as before) spliced silent gaps into the loopback whenever capture hadn't
			// caught up yet, which is audible as choppy/glitchy playback.
			int16_t aMicFrame[BestClientVoice::FRAME_SIZE] = {};
			const size_t MicSamples = m_MicMonitorPcm.PopFront(aMicFrame, BestClientVoice::FRAME_SIZE);
			for(size_t i = 0; i < MicSamples; ++i)
			{
				const int Mixed = (int)(aMicFrame[i] * MicCheckGain);
				aMix[i * 2u] += Mixed;
				aMix[i * 2u + 1] += Mixed;
			}
			MixedAnySamples = true;
		}

		for(auto &PeerPair : m_Peers)
		{
			const auto ItPeerVolume = m_PeerVolumePercent.find(PeerPair.first);
			const int PeerVolume = ItPeerVolume == m_PeerVolumePercent.end() ? 100 : std::clamp(ItPeerVolume->second, 0, 200);
			CRemotePeer &Peer = PeerPair.second;
			if(Peer.m_DecodedPcm.Size() == 0)
				continue;

			float Gain = ComputePeerGain(Peer);
			Gain *= MasterVolume * (PeerVolume / 100.0f);
			if(Gain <= 0.0f)
			{
				if(Peer.m_DecodedPcm.Size() > 0)
					Peer.m_DecodedPcm.Clear();
				continue;
			}

			int16_t aPeerFrame[BestClientVoice::FRAME_SIZE] = {};
			const size_t PeerSamples = Peer.m_DecodedPcm.PopFront(aPeerFrame, BestClientVoice::FRAME_SIZE);
			for(size_t i = 0; i < PeerSamples; ++i)
			{
				const int Mixed = (int)(aPeerFrame[i] * Gain);
				aMix[i * 2u] += Mixed;
				aMix[i * 2u + 1] += Mixed;
			}
			if(PeerSamples > 0)
				MixedAnySamples = true;
		}

		if(!MixedAnySamples)
			break;

		int64_t Peak = 0;
		for(int i = 0; i < BestClientVoice::FRAME_SIZE * 2; ++i)
		{
			const int64_t Sample = aMix[i];
			const int64_t Abs = Sample < 0 ? -Sample : Sample;
			Peak = maximum<int64_t>(Peak, Abs);
		}

		const float ClipScale = Peak > (int)std::numeric_limits<int16_t>::max() ? ((float)std::numeric_limits<int16_t>::max() / (float)Peak) : 1.0f;
		for(int i = 0; i < BestClientVoice::FRAME_SIZE * 2; ++i)
		{
			const int Out = round_to_int(aMix[i] * ClipScale);
			aOut[i] = (int16_t)std::clamp(Out, (int)std::numeric_limits<int16_t>::min(), (int)std::numeric_limits<int16_t>::max());
		}
		if(SDL_QueueAudio(m_PlaybackDevice, aOut, sizeof(aOut)) < 0)
		{
			if(!m_PlaybackQueueErrorLogged)
			{
				dbg_msg("voice", "SDL_QueueAudio failed: %s", SDL_GetError());
				m_PlaybackQueueErrorLogged = true;
			}
			break;
		}
		m_PlaybackQueueErrorLogged = false;
	}
}

void CVoiceChat::CleanupPeers()
{
	const int64_t Now = time_get();
	bool PeersChanged = false;
	for(auto It = m_Peers.begin(); It != m_Peers.end();)
	{
		CRemotePeer &Peer = It->second;
		if(Peer.m_LastReceiveTick > 0 && Now - Peer.m_LastReceiveTick > PEER_TIMEOUT_SECONDS * time_freq())
		{
			if(Peer.m_pDecoder)
				opus_decoder_destroy(Peer.m_pDecoder);
			m_PeerVolumePercent.erase(It->first);
			It = m_Peers.erase(It);
			PeersChanged = true;
		}
		else
		{
			++It;
		}
	}
	if(PeersChanged)
		InvalidatePeerCaches();
}

void CVoiceChat::BeginReconnect()
{
	m_RuntimeState = RUNTIME_RECONNECTING;
	StopVoice();
	m_RuntimeState = RUNTIME_RECONNECTING;
	StartVoice();
}

void CVoiceChat::InvalidatePeerCaches(bool MappingDirty, bool TalkingDirty)
{
	m_PeerListDirty = m_PeerListDirty || MappingDirty;
	m_SnapMappingDirty = m_SnapMappingDirty || MappingDirty;
	m_TalkingStateDirty = m_TalkingStateDirty || TalkingDirty;
}

void CVoiceChat::RefreshPeerCaches()
{
	if(m_PeerListDirty || m_SnapMappingDirty)
		RefreshPeerMappingCache();
	if(m_TalkingStateDirty)
		RefreshTalkingCache();
}

void CVoiceChat::RefreshPeerMappingCache()
{
	m_vSortedPeerIds.clear();
	m_vSortedPeerIds.reserve(m_Peers.size());
	for(const auto &PeerPair : m_Peers)
		m_vSortedPeerIds.push_back(PeerPair.first);
	std::sort(m_vSortedPeerIds.begin(), m_vSortedPeerIds.end());

	m_PeerResolvedClientIds.clear();
	m_vVisibleMemberPeerIds.clear();
	m_vVisibleMemberPeerIds.reserve(m_vSortedPeerIds.size());
	const int64_t Now = time_get();
	const int64_t ConnectedTimeoutTicks = 3 * time_freq();
	for(uint16_t PeerId : m_vSortedPeerIds)
	{
		const auto It = m_Peers.find(PeerId);
		if(It == m_Peers.end())
			continue;

		const CRemotePeer &Peer = It->second;
		const int ResolvedClientId = ResolvePeerClientId(Peer);
		m_PeerResolvedClientIds[PeerId] = ResolvedClientId;
		if(ResolvedClientId >= 0 && ResolvedClientId < MAX_CLIENTS)
		{
			const std::string NameKey = NormalizeVoiceNameKey(GameClient()->m_aClients[ResolvedClientId].m_aName);
			if(!NameKey.empty())
			{
				const bool Muted = m_MutedNameKeys.find(NameKey) != m_MutedNameKeys.end();
				const auto ItVolume = m_NameVolumePercent.find(NameKey);
				if(Muted)
				{
					m_PeerVolumePercent[PeerId] = 0;
				}
				else if(ItVolume != m_NameVolumePercent.end())
				{
					m_PeerVolumePercent[PeerId] = std::clamp(ItVolume->second, 0, 200);
				}
				else
				{
					m_PeerVolumePercent[PeerId] = 100;
				}
			}
		}
		if(Peer.m_LastReceiveTick > 0 && Now - Peer.m_LastReceiveTick <= ConnectedTimeoutTicks && ShouldShowPeerInMembers(Peer))
			m_vVisibleMemberPeerIds.push_back(PeerId);
	}
	m_PeerListDirty = false;
	m_SnapMappingDirty = false;
	m_TalkingStateDirty = true;
}

void CVoiceChat::RefreshTalkingCache()
{
	m_vTalkingEntries.clear();
	m_vTalkingEntries.reserve(m_vSortedPeerIds.size() + 1);
	std::array<bool, MAX_CLIENTS> aClientAdded = {};

	const int LocalClientId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalClientId >= 0 && LocalClientId < MAX_CLIENTS && IsClientTalking(LocalClientId))
	{
		aClientAdded[LocalClientId] = true;
		m_vTalkingEntries.push_back({LocalClientId, 0, true});
	}

	const int64_t Now = time_get();
	const int64_t TalkingTimeoutTicks = (VOICE_TALKING_TIMEOUT_MS * time_freq()) / 1000;
	for(uint16_t PeerId : m_vSortedPeerIds)
	{
		const auto It = m_Peers.find(PeerId);
		if(It == m_Peers.end())
			continue;

		const CRemotePeer &Peer = It->second;
		if(Peer.m_LastVoiceTick <= 0 || Now - Peer.m_LastVoiceTick > TalkingTimeoutTicks)
			continue;

		const auto ItResolved = m_PeerResolvedClientIds.find(PeerId);
		const int ClientId = ItResolved == m_PeerResolvedClientIds.end() ? -1 : ItResolved->second;
		const auto ItPeerVolume = m_PeerVolumePercent.find(PeerId);
		const int PeerVolume = ItPeerVolume == m_PeerVolumePercent.end() ? 100 : std::clamp(ItPeerVolume->second, 0, 200);
		if(PeerVolume <= 0)
			continue;
		if(ComputePeerGain(Peer) <= 0.0f)
			continue;

		if(ClientId >= 0 && ClientId < MAX_CLIENTS)
		{
			const std::string NameKey = NormalizeVoiceNameKey(GameClient()->m_aClients[ClientId].m_aName);
			if(!NameKey.empty() && m_MutedNameKeys.find(NameKey) != m_MutedNameKeys.end())
				continue;
			if(aClientAdded[ClientId])
				continue;
			aClientAdded[ClientId] = true;
		}

		m_vTalkingEntries.push_back({ClientId, PeerId, false});
	}
	m_TalkingStateDirty = false;
}

bool CVoiceChat::ShouldTransmit() const
{
	if(!m_Registered)
		return false;
	if(g_Config.m_BcVoiceChatMicMuted)
		return false;
	if(IsInGameOnlyBlocked())
		return false;
	return true;
}

bool CVoiceChat::IsInGameOnlyBlocked() const
{
	if(g_Config.m_BcVoiceChatInGameOnly == 0)
		return false;
	IEngineGraphics *pEngineGraphics = Kernel()->RequestInterface<IEngineGraphics>();
	return pEngineGraphics && pEngineGraphics->WindowActive() == 0;
}

bool CVoiceChat::ShouldStartVoicePipeline(bool Online) const
{
	return Online && g_Config.m_BcVoiceChatEnable != 0;
}

bool CVoiceChat::HasPendingPlaybackAudio() const
{
	if(g_Config.m_BcVoiceChatMicCheck && m_MicMonitorPcm.Size() > 0)
		return true;
	for(const auto &PeerPair : m_Peers)
	{
		if(PeerPair.second.m_DecodedPcm.Size() > 0)
			return true;
	}
	return false;
}

bool CVoiceChat::ShouldKeepVoicePipelineActive() const
{
	return g_Config.m_BcVoiceChatEnable != 0;
}

bool CVoiceChat::ShouldUseSecondaryTeamConnection() const
{
	return IsEnableYourGroupMode() && LocalOwnVoiceTeam() > 0;
}

int CVoiceChat::LocalTeam() const
{
	if(GameClient()->m_Snap.m_LocalClientId < 0)
		return 0;
	return GameClient()->m_Teams.Team(GameClient()->m_Snap.m_LocalClientId);
}

int CVoiceChat::LocalVoiceTeam() const
{
	return IsUseTeam0Mode() ? 0 : LocalOwnVoiceTeam();
}

int CVoiceChat::LocalOwnVoiceTeam() const
{
	return maximum(LocalTeam(), 0);
}

bool CVoiceChat::IsUseTeam0Mode() const
{
	return g_Config.m_BcVoiceChatUseTeam0 != 0;
}

bool CVoiceChat::IsEnableYourGroupMode() const
{
	return IsUseTeam0Mode() && g_Config.m_BcVoiceChatEnableYourGroup != 0;
}

bool CVoiceChat::IsVoiceTeamAudible(int Team) const
{
	const int NormalizedTeam = maximum(Team, 0);
	const int OwnTeam = LocalOwnVoiceTeam();

	if(IsUseTeam0Mode())
	{
		if(NormalizedTeam == 0)
			return true;
		return IsEnableYourGroupMode() && OwnTeam > 0 && NormalizedTeam == OwnTeam;
	}

	return NormalizedTeam == OwnTeam;
}

vec2 CVoiceChat::LocalPosition() const
{
	if(GameClient()->m_Snap.m_SpecInfo.m_Active)
		return GameClient()->m_Camera.m_Center;
	return GameClient()->m_LocalCharacterPos;
}

std::string CVoiceChat::CurrentRoomKey() const
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return "";

	char aAddr[NETADDR_MAXSTRSIZE];
	net_addr_str(&Client()->ServerAddress(), aAddr, sizeof(aAddr), true);
	return aAddr;
}

int CVoiceChat::LocalGameClientId() const
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return BestClientVoice::INVALID_GAME_CLIENT_ID;
	return GameClient()->m_Snap.m_LocalClientId;
}

std::string CVoiceChat::EffectiveServerAddress() const
{
	return ResolvedVoiceServerAddress(g_Config.m_BcVoiceChatServerAddress);
}

const char *CVoiceChat::EffectiveServerLabel() const
{
	return IsManagedServerConfig() ? "BestClient Voice" : g_Config.m_BcVoiceChatServerAddress;
}

bool CVoiceChat::IsManagedServerConfig() const
{
	return IsLegacyVoiceServerAddress(g_Config.m_BcVoiceChatServerAddress);
}

uint64_t CVoiceChat::CurrentHelloAuthTimestamp() const
{
	return (uint64_t)time_timestamp();
}

int CVoiceChat::ResolvePeerClientId(const CRemotePeer &Peer) const
{
	if(Peer.m_AnnouncedGameClientId >= 0 && Peer.m_AnnouncedGameClientId < MAX_CLIENTS)
	{
		if(Peer.m_AnnouncedGameClientId == LocalGameClientId())
			return -1;

		const CNetObj_PlayerInfo *pInfo = GameClient()->m_Snap.m_apPlayerInfos[Peer.m_AnnouncedGameClientId];
		if(!pInfo || !pInfo->m_Local)
			return Peer.m_AnnouncedGameClientId;
	}

	return -1;
}

bool CVoiceChat::ShouldShowPeerInMembers(const CRemotePeer &Peer) const
{
	if(Peer.m_AnnouncedGameClientId == LocalGameClientId())
		return false;

	if(Peer.m_AnnouncedGameClientId >= 0 && Peer.m_AnnouncedGameClientId < MAX_CLIENTS)
		return true;

	return Peer.m_LastVoiceTick > 0;
}

float CVoiceChat::ComputePeerGain(const CRemotePeer &Peer) const
{
	if(!IsPositionWithinRadiusFilter(Peer.m_Position))
		return 0.0f;
	return 1.0f;
}

bool CVoiceChat::IsRadiusFilterEnabled() const
{
	return g_Config.m_BcVoiceChatRadiusEnabled != 0;
}

float CVoiceChat::RadiusFilterDistanceUnits() const
{
	const int RadiusTiles = std::clamp(g_Config.m_BcVoiceChatRadiusTiles, 1, 500);
	return RadiusTiles * VOICE_TILE_WORLD_SIZE;
}

bool CVoiceChat::IsPositionWithinRadiusFilter(vec2 Position) const
{
	if(!IsRadiusFilterEnabled())
		return true;

	const vec2 LocalPos = LocalPosition();
	const vec2 Diff = Position - LocalPos;
	const float DistSq = Diff.x * Diff.x + Diff.y * Diff.y;
	const float Radius = RadiusFilterDistanceUnits();
	return DistSq <= Radius * Radius;
}

bool CVoiceChat::IsClientTalking(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;

	const CNetObj_PlayerInfo *pPlayerInfo = GameClient()->m_Snap.m_apPlayerInfos[ClientId];
	const bool IsLocalClient = (pPlayerInfo && pPlayerInfo->m_Local) || ClientId == GameClient()->m_Snap.m_LocalClientId;

	if(IsLocalClient)
	{
		if(g_Config.m_BcVoiceChatMicMuted)
			return false;

		if(g_Config.m_BcVoiceChatActivationMode == 1)
			return m_PushToTalkPressed;

		const int64_t NowTick = time_get();
		return m_AutoActivationUntilTick > 0 && NowTick <= m_AutoActivationUntilTick;
	}

	if(!m_Registered)
		return false;

	const std::string NameKey = NormalizeVoiceNameKey(GameClient()->m_aClients[ClientId].m_aName);
	if(!NameKey.empty() && m_MutedNameKeys.find(NameKey) != m_MutedNameKeys.end())
		return false;

	for(const STalkingEntry &Entry : m_vTalkingEntries)
	{
		if(!Entry.m_IsLocal && Entry.m_ClientId == ClientId)
			return true;
	}

	return false;
}

void CVoiceChat::FetchServerList()
{
	if(m_pServerListTask && !m_pServerListTask->Done())
		return;

	m_pServerListTask = HttpGet(VoiceMasterListUrl().c_str());
	m_pServerListTask->Timeout(CTimeout{10000, 0, 500, 5});
	m_pServerListTask->LogProgress(HTTPLOG::NONE);
	m_pServerListTask->IpResolve(IPRESOLVE::V4);
	m_pServerListTask->VerifyPeer(false);
	Http()->Run(m_pServerListTask);
}

void CVoiceChat::ResetServerListTask()
{
	if(m_pServerListTask)
	{
		m_pServerListTask->Abort();
		m_pServerListTask = nullptr;
	}
}

void CVoiceChat::FinishServerList()
{
	json_value *pJson = m_pServerListTask->ResultJson();
	if(!pJson)
		return;

	m_vServerEntries.clear();
	m_ServerRowButtons.clear();

	if(pJson->type == json_array)
	{
		for(unsigned int i = 0; i < pJson->u.array.length; ++i)
		{
			const json_value &Item = *pJson->u.array.values[i];
			if(Item.type != json_object)
				continue;

			const json_value &Name = Item["name"];
			const json_value &Address = Item["address"];
			const json_value &Ip = Item["ip"];
			const json_value &Flag = Item["flag"];
			if(Name.type != json_string)
				continue;
			const json_value *pAddressValue = nullptr;
			if(Address.type == json_string)
				pAddressValue = &Address;
			else if(Ip.type == json_string)
				pAddressValue = &Ip;
			else
				continue;

			CVoiceServerEntry Entry;
			Entry.m_Name = Name.u.string.ptr;
			Entry.m_Address = pAddressValue->u.string.ptr;
			if(Flag.type == json_integer)
				Entry.m_Flag = (int)Flag.u.integer;
			if(BestClientVoice::ParseAddress(ResolvedVoiceServerAddress(Entry.m_Address.c_str()), BestClientVoice::DEFAULT_PORT, Entry.m_Addr))
				Entry.m_HasAddr = true;

			m_vServerEntries.push_back(Entry);
		}
	}

	json_value_free(pJson);

	if(!m_vServerEntries.empty())
	{
		m_ServerRowButtons.resize(m_vServerEntries.size());
		StartServerListPings();
	}
}

void CVoiceChat::StartServerListPings()
{
	if(m_vServerEntries.empty())
		return;

	if(!m_ServerListPingSocket)
	{
		NETADDR Bind = NETADDR_ZEROED;
		Bind.type = NETTYPE_ALL;
		Bind.port = 0;
		m_ServerListPingSocket = net_udp_create(Bind);
		if(!m_ServerListPingSocket)
			return;
		net_set_non_blocking(m_ServerListPingSocket);
	}

	static uint16_t s_NextPingToken = 1;
	for(auto &Entry : m_vServerEntries)
	{
		if(!Entry.m_HasAddr)
			continue;
		std::vector<uint8_t> vPacket;
		vPacket.reserve(16);
		BestClientVoice::WriteHeader(vPacket, BestClientVoice::PACKET_PING);
		if(s_NextPingToken == 0)
			++s_NextPingToken;
		const uint16_t PingToken = s_NextPingToken++;
		BestClientVoice::WriteU16(vPacket, PingToken);
		net_udp_send(m_ServerListPingSocket, &Entry.m_Addr, vPacket.data(), (int)vPacket.size());
		Entry.m_LastPingSendTick = time_get();
		Entry.m_PingToken = PingToken;
		Entry.m_PingInFlight = true;
	}
	m_LastServerListPingSweepTick = time_get();
}

void CVoiceChat::ConVoiceConnect(IConsole::IResult *pResult, void *pUserData)
{
	CVoiceChat *pSelf = static_cast<CVoiceChat *>(pUserData);
	if(pResult->NumArguments() > 0)
		str_copy(g_Config.m_BcVoiceChatServerAddress, pResult->GetString(0), sizeof(g_Config.m_BcVoiceChatServerAddress));
	g_Config.m_BcVoiceChatEnable = 1;
	if(pSelf->Client()->State() != IClient::STATE_ONLINE)
		return;
	pSelf->StopVoice();
	pSelf->m_RuntimeState = RUNTIME_RECONNECTING;
	pSelf->StartVoice();
	str_copy(pSelf->m_aLastServerAddr, pSelf->EffectiveServerAddress().c_str(), sizeof(pSelf->m_aLastServerAddr));
	pSelf->m_LastInputDevice = g_Config.m_BcVoiceChatInputDevice;
	pSelf->m_LastOutputDevice = g_Config.m_BcVoiceChatOutputDevice;
}

void CVoiceChat::ConVoiceDisconnect(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	CVoiceChat *pSelf = static_cast<CVoiceChat *>(pUserData);
	g_Config.m_BcVoiceChatEnable = 0;
	if(pSelf->m_Socket)
		pSelf->StopVoice();
	dbg_msg("voice", "voice disconnected");
}

void CVoiceChat::ConVoiceStatus(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	CVoiceChat *pSelf = static_cast<CVoiceChat *>(pUserData);
	dbg_msg("voice", "enabled=%d connected=%d participants=%d server='%s' ptt=%d radius=%d radius_tiles=%d",
		g_Config.m_BcVoiceChatEnable ? 1 : 0,
		pSelf->m_Registered ? 1 : 0,
		(int)pSelf->m_vVisibleMemberPeerIds.size(),
		pSelf->EffectiveServerLabel(),
		pSelf->m_PushToTalkPressed ? 1 : 0,
		g_Config.m_BcVoiceChatRadiusEnabled ? 1 : 0,
		std::clamp(g_Config.m_BcVoiceChatRadiusTiles, 1, 500));
}

void CVoiceChat::ConKeyVoiceTalk(IConsole::IResult *pResult, void *pUserData)
{
	CVoiceChat *pSelf = static_cast<CVoiceChat *>(pUserData);
	pSelf->m_PushToTalkPressed = pResult->GetInteger(0) != 0;
}

void CVoiceChat::ConToggleVoiceMicMute(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	(void)pUserData;
	ToggleVoiceMicMute();
}

void CVoiceChat::ConToggleVoiceHeadphonesMute(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	(void)pUserData;
	ToggleVoiceHeadphonesMute();
}
