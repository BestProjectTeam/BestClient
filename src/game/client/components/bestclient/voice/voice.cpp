/* (c) BestClient */
#include "voice.h"

#include "protocol.h"

#include <base/color.h>
#include <base/log.h>
#include <base/math.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/font_icons.h>
#include <engine/shared/config.h>
#include <engine/shared/http.h>
#include <engine/shared/json.h>
#include <engine/textrender.h>

#include <game/client/animstate.h>
#include <game/client/components/hud_layout.h>
#include <game/client/gameclient.h>
#include <game/client/components/countryflags.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <unordered_set>

#include <SDL.h>
#include <opus.h>

namespace
{
constexpr int CAPTURE_READ_SAMPLES = 4096;
constexpr int MAX_RECEIVE_PACKETS_PER_TICK = 64;
constexpr int MAX_CAPTURE_QUEUE_SAMPLES = BestClientVoice::SAMPLE_RATE * 2;
constexpr int MAX_DECODED_QUEUE_SAMPLES = BestClientVoice::FRAME_SIZE * 8;
constexpr int MAX_MIC_MONITOR_QUEUE_SAMPLES = BestClientVoice::SAMPLE_RATE * 2;
constexpr int PLAYBACK_TARGET_FRAMES = BestClientVoice::FRAME_SIZE * 3;
constexpr int PLAYBACK_MAX_RESYNC_FRAMES = BestClientVoice::FRAME_SIZE * 4;
constexpr int MAX_PACKET_GAP_FOR_PLC = 3;
constexpr int PEER_TIMEOUT_SECONDS = 10;
constexpr int VOICE_TALKING_TIMEOUT_MS = 350;
constexpr int VOICE_MAX_BITRATE_KBPS = 96;
constexpr int VOICE_HEARTBEAT_SECONDS = 5;
constexpr int VOICE_SERVER_STALE_SECONDS = 10;
constexpr int VOICE_IDLE_SHUTDOWN_SECONDS = 5;
constexpr int VOICE_START_RETRY_SECONDS = 5;
constexpr float PANEL_PADDING = 14.0f;
constexpr float PANEL_HEADER_HEIGHT = 34.0f;
constexpr float PANEL_SECTION_BUTTON_SIZE = 34.0f;
constexpr float PANEL_ROW_HEIGHT = 48.0f;
constexpr int SERVER_LIST_PING_TIMEOUT_SEC = 2;
constexpr int SERVER_LIST_PING_INTERVAL_SEC = 30;
constexpr const char *VOICE_MASTER_LIST_URL = "https://150.241.70.188:3000/voice/servers.json";

enum
{
	VOICE_SECTION_SERVERS = 0,
	VOICE_SECTION_MEMBERS,
	VOICE_SECTION_SETTINGS,
};

ColorRGBA VoiceSectionBgColor()
{
	return ColorRGBA(0.0f, 0.0f, 0.0f, 0.18f);
}

ColorRGBA VoiceCardBgColor()
{
	return ColorRGBA(0.02f, 0.02f, 0.03f, 0.24f);
}

ColorRGBA VoiceRowBgColor()
{
	return ColorRGBA(0.03f, 0.03f, 0.04f, 0.24f);
}

ColorRGBA VoiceRowHotColor()
{
	return ColorRGBA(0.10f, 0.11f, 0.13f, 0.30f);
}

ColorRGBA VoiceRowSelectedColor()
{
	return ColorRGBA(0.16f, 0.18f, 0.22f, 0.40f);
}

ColorRGBA VoiceIconButtonColor(bool Active)
{
	return Active ? ColorRGBA(0.18f, 0.20f, 0.24f, 0.34f) : ColorRGBA(0.02f, 0.02f, 0.03f, 0.22f);
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

void WriteVoiceString(std::vector<uint8_t> &vOut, const std::string &Str, int MaxLen = 128)
{
	const uint16_t Size = (uint16_t)minimum<size_t>(Str.size(), (size_t)MaxLen);
	BestClientVoice::WriteU16(vOut, Size);
	vOut.insert(vOut.end(), Str.begin(), Str.begin() + Size);
}

ColorRGBA VoiceMuteButtonColor(bool Active)
{
	return Active ? ColorRGBA(0.45f, 0.10f, 0.10f, 0.34f) : ColorRGBA(0.02f, 0.02f, 0.03f, 0.22f);
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
	opus_encoder_ctl(pEncoder, OPUS_SET_VBR_CONSTRAINT(1));
	opus_encoder_ctl(pEncoder, OPUS_SET_COMPLEXITY(10));
	opus_encoder_ctl(pEncoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));

	// Client-side resilience. Server just relays Opus payloads.
	// FEC is only used when packet loss percentage is > 0.
	opus_encoder_ctl(pEncoder, OPUS_SET_INBAND_FEC(1));
	opus_encoder_ctl(pEncoder, OPUS_SET_PACKET_LOSS_PERC(5));
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

float VoiceHudAlpha(CGameClient *pGameClient)
{
	(void)pGameClient;
	return 1.0f;
}

ColorRGBA ApplyVoiceHudAlpha(CGameClient *pGameClient, ColorRGBA Color)
{
	Color.a *= VoiceHudAlpha(pGameClient);
	return Color;
}

int VoiceHudBackgroundCorners(CGameClient *pGameClient, int Module, int DefaultCorners, float RectX, float RectY, float RectW, float RectH, float CanvasWidth, float CanvasHeight)
{
	(void)pGameClient;
	(void)Module;
	return HudLayout::BackgroundCorners(DefaultCorners, RectX, RectY, RectW, RectH, CanvasWidth, CanvasHeight);
}

}

void CVoiceChat::OnConsoleInit()
{
	Console()->Register("voice_connect", "?s[address]", CFGFLAG_CLIENT, ConVoiceConnect, this, "Connect to voice server");
	Console()->Register("voice_disconnect", "", CFGFLAG_CLIENT, ConVoiceDisconnect, this, "Disconnect from voice server");
	Console()->Register("voice_status", "", CFGFLAG_CLIENT, ConVoiceStatus, this, "Show voice status");
	Console()->Register("toggle_voice_panel", "", CFGFLAG_CLIENT, ConToggleVoicePanel, this, "Toggle voice panel");
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
	m_WasTransmitActive = false;
	m_LastHelloTick = 0;
	m_LastServerPacketTick = 0;
	m_LastHeartbeatTick = 0;
	m_LastBitrate = -1;
	m_HelloResetPending = false;
	m_AdvertisedRoomKey.clear();
	m_AdvertisedGameClientId = BestClientVoice::INVALID_GAME_CLIENT_ID - 1;
	m_AdvertisedTeam = std::numeric_limits<int>::min();
	ClearPeerState();
	m_CapturePcm.Clear();
	m_MicMonitorPcm.Clear();
	InvalidatePeerCaches();
}

void CVoiceChat::OnStateChange(int NewState, int OldState)
{
	(void)OldState;
	if(NewState == IClient::STATE_OFFLINE)
	{
		SetPanelActive(false);
		StopVoice();
		m_ServerRowButtons.clear();
		m_vServerEntries.clear();
		ResetServerListTask();
		CloseServerListPingSocket();
	}
	else if(NewState == IClient::STATE_ONLINE)
	{
		FetchServerList();
	}
}

void CVoiceChat::OnUpdate()
{
	const int64_t PerfStart = time_get();
	const bool Online = Client()->State() == IClient::STATE_ONLINE;

	if(g_Config.m_BcVoiceChatHeadphonesMuted && !g_Config.m_BcVoiceChatMicMuted)
		g_Config.m_BcVoiceChatMicMuted = 1;

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

	const bool ServerChanged = str_comp(m_aLastServerAddr, g_Config.m_BcVoiceChatServerAddress) != 0;
	const bool DeviceChanged = m_LastInputDevice != g_Config.m_BcVoiceChatInputDevice || m_LastOutputDevice != g_Config.m_BcVoiceChatOutputDevice;

	if(m_pServerListTask && m_pServerListTask->State() == EHttpState::DONE)
	{
		FinishServerList();
		ResetServerListTask();
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
		str_copy(m_aLastServerAddr, g_Config.m_BcVoiceChatServerAddress, sizeof(m_aLastServerAddr));
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
	}

	const std::string RoomKey = CurrentRoomKey();
	const int GameClientId = LocalGameClientId();
	const int VoiceTeam = LocalVoiceTeam();
	const bool RoomChanged = RoomKey != m_AdvertisedRoomKey;
	const bool TeamChanged = VoiceTeam != m_AdvertisedTeam;
	const bool IdentityChanged = RoomChanged || GameClientId != m_AdvertisedGameClientId || TeamChanged;
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
			dbg_msg("voice/perf", "update last=%.3fms avg=%.3fms max=%.3fms samples=%lld socket=%d peers=%zu",
				m_LastUpdateCostTick * 1000.0 / (double)time_freq(),
				m_UpdateSamples > 0 ? (m_TotalUpdateCostTick * 1000.0 / (double)time_freq()) / (double)m_UpdateSamples : 0.0,
				m_MaxUpdateCostTick * 1000.0 / (double)time_freq(),
				(long long)m_UpdateSamples,
				m_Socket != nullptr ? 1 : 0,
				m_Peers.size());
			m_LastPerfReportTick = Now;
			m_TotalUpdateCostTick = 0;
			m_UpdateSamples = 0;
			m_MaxUpdateCostTick = 0;
		}
	}
}

void CVoiceChat::OnShutdown()
{
	SetPanelActive(false);
	StopVoice();
	ResetServerListTask();
	CloseServerListPingSocket();
	m_ServerRowButtons.clear();
	m_vServerEntries.clear();
	m_vOnlineServers.clear();
	m_SelectedServerIndex = -1;
}

void CVoiceChat::OnRelease()
{
	SetPanelActive(false);
}

bool CVoiceChat::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!m_PanelActive || !m_MouseUnlocked)
		return false;

	Ui()->ConvertMouseMove(&x, &y, CursorType);
	Ui()->OnCursorMove(x, y);
	return true;
}

bool CVoiceChat::OnInput(const IInput::CEvent &Event)
{
	if(!m_PanelActive)
		return false;

	if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_ESCAPE)
	{
		SetPanelActive(false);
		Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE);
		return true;
	}
	Ui()->OnInput(Event);
	return true;
}

void CVoiceChat::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		if(m_PanelActive)
			SetPanelActive(false);
		return;
	}

	if(!m_PanelActive)
		return;

	if(Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
	{
		SetPanelActive(false);
		return;
	}

	Ui()->StartCheck();
	Ui()->Update();

	const CUIRect Screen = *Ui()->Screen();
	Ui()->MapScreen();

	RenderPanel(Screen, true);
	Ui()->RenderPopupMenus();
	RenderTools()->RenderCursor(Ui()->MousePos(), 24.0f);

	Ui()->FinishCheck();
	Ui()->ClearHotkeys();
}

void CVoiceChat::RenderMenuPanel(const CUIRect &View)
{
	RenderPanel(View, false);
}

void CVoiceChat::RenderMenuPanelToggleBind(const CUIRect &View)
{
	auto RenderBindRow = [&](const char *pLabel, const char *pCommand, CButtonContainer &Reader, CButtonContainer &Clear) {
		CBindSlot CurrentBind(KEY_UNKNOWN, KeyModifier::NONE);
		bool Found = false;
		for(int Mod = 0; Mod < KeyModifier::COMBINATION_COUNT && !Found; ++Mod)
		{
			for(int KeyId = 0; KeyId < KEY_LAST; ++KeyId)
			{
				const char *pBind = GameClient()->m_Binds.Get(KeyId, Mod);
				if(!pBind[0])
					continue;
				if(str_comp(pBind, pCommand) == 0)
				{
					CurrentBind = CBindSlot(KeyId, Mod);
					Found = true;
					break;
				}
			}
		}

		CUIRect Row = View;
		CUIRect LabelRect, BindRect;
		Row.VSplitLeft(170.0f, &LabelRect, &BindRect);
		Ui()->DoLabel(&LabelRect, pLabel, 12.0f, TEXTALIGN_ML);
		BindRect.VSplitLeft(6.0f, nullptr, &BindRect);

		const auto Result = GameClient()->m_KeyBinder.DoKeyReader(&Reader, &Clear, &BindRect, CurrentBind, false);
		if(Result.m_Bind != CurrentBind)
		{
			if(CurrentBind.m_Key != KEY_UNKNOWN)
				GameClient()->m_Binds.Bind(CurrentBind.m_Key, "", false, CurrentBind.m_ModifierMask);
			if(Result.m_Bind.m_Key != KEY_UNKNOWN)
				GameClient()->m_Binds.Bind(Result.m_Bind.m_Key, pCommand, false, Result.m_Bind.m_ModifierMask);
		}
	};

	RenderBindRow(Localize("Voice panel bind"), "toggle_voice_panel", m_PanelBindReaderButton, m_PanelBindClearButton);
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

	// !voice ...
	if(!str_startswith_nocase(p, "!voice"))
		return false;
	p += 6;
	if(p[0] != '\0' && !std::isspace((unsigned char)p[0]))
		return false;

	char aSub[32];
	if(!ReadToken(p, aSub, sizeof(aSub)))
	{
		GameClient()->m_Chat.Echo("Usage: !voice on/off/status/mode/server/mute/unmute/volume");
		return true;
	}

	auto RestartIfOnline = [&]() {
		if(Client()->State() == IClient::STATE_ONLINE && g_Config.m_BcVoiceChatEnable)
		{
			if(m_Socket)
				StopVoice();
			m_RuntimeState = RUNTIME_RECONNECTING;
			StartVoice();
		}
	};

	if(str_comp_nocase(aSub, "on") == 0)
	{
		g_Config.m_BcVoiceChatEnable = 1;
		if(Client()->State() == IClient::STATE_ONLINE && !m_Socket)
			StartVoice();
		GameClient()->m_Chat.Echo("Voice: enabled");
		return true;
	}
	if(str_comp_nocase(aSub, "off") == 0)
	{
		g_Config.m_BcVoiceChatEnable = 0;
		if(m_Socket)
			StopVoice();
		GameClient()->m_Chat.Echo("Voice: disabled");
		return true;
	}
	if(str_comp_nocase(aSub, "status") == 0)
	{
		char aBuf[256];
		const char *pMode = g_Config.m_BcVoiceChatActivationMode == 1 ? "ppt" : "automatic";
		str_format(aBuf, sizeof(aBuf), "Voice: %s | %s | server=%s | mode=%s",
			g_Config.m_BcVoiceChatEnable ? "on" : "off",
			m_Registered ? "connected" : "disconnected",
			g_Config.m_BcVoiceChatServerAddress,
			pMode);
		GameClient()->m_Chat.Echo(aBuf);

		if(m_vServerEntries.empty())
		{
			GameClient()->m_Chat.Echo("Voice servers: not loaded (open panel or use !voice server reload)");
			return true;
		}

		GameClient()->m_Chat.Echo("Voice servers:");
		for(size_t i = 0; i < m_vServerEntries.size(); ++i)
		{
			const auto &Entry = m_vServerEntries[i];
			char aPing[16];
			if(Entry.m_PingMs >= 0)
				str_format(aPing, sizeof(aPing), "%dms", Entry.m_PingMs);
			else
				str_copy(aPing, "--", sizeof(aPing));
			str_format(aBuf, sizeof(aBuf), "  %d) %s (%s) %s", (int)i + 1, Entry.m_Name.c_str(), Entry.m_Address.c_str(), aPing);
			GameClient()->m_Chat.Echo(aBuf);
		}
		return true;
	}
	if(str_comp_nocase(aSub, "mode") == 0)
	{
		char aArg[32];
		if(!ReadToken(p, aArg, sizeof(aArg)))
		{
			GameClient()->m_Chat.Echo(g_Config.m_BcVoiceChatActivationMode == 1 ? "Voice mode: ppt" : "Voice mode: automatic");
			return true;
		}

		if(str_comp_nocase(aArg, "ppt") == 0 || str_comp_nocase(aArg, "ptt") == 0)
		{
			g_Config.m_BcVoiceChatActivationMode = 1;
			GameClient()->m_Chat.Echo("Voice mode: ppt");
			return true;
		}
		if(str_comp_nocase(aArg, "automatic") == 0 || str_comp_nocase(aArg, "auto") == 0)
		{
			g_Config.m_BcVoiceChatActivationMode = 0;
			GameClient()->m_Chat.Echo("Voice mode: automatic");
			return true;
		}

		GameClient()->m_Chat.Echo("Usage: !voice mode automatic|ppt");
		return true;
	}
	if(str_comp_nocase(aSub, "server") == 0)
	{
		char aArg[128];
		if(!ReadToken(p, aArg, sizeof(aArg)))
		{
			GameClient()->m_Chat.Echo("Usage: !voice server <index|address|reload>");
			if(!m_vServerEntries.empty())
				GameClient()->m_Chat.Echo("Tip: !voice status shows server list with indices");
			return true;
		}

		if(str_comp_nocase(aArg, "reload") == 0)
		{
			ResetServerListTask();
			CloseServerListPingSocket();
			m_vServerEntries.clear();
			m_ServerRowButtons.clear();
			m_SelectedServerIndex = -1;
			FetchServerList();
			GameClient()->m_Chat.Echo("Voice servers: reloading...");
			return true;
		}

		const int Index = str_toint(aArg);
		if(Index > 0 && Index <= (int)m_vServerEntries.size())
		{
			const auto &Entry = m_vServerEntries[(size_t)Index - 1];
			if(str_comp(g_Config.m_BcVoiceChatServerAddress, Entry.m_Address.c_str()) != 0)
			{
				str_copy(g_Config.m_BcVoiceChatServerAddress, Entry.m_Address.c_str(), sizeof(g_Config.m_BcVoiceChatServerAddress));
				str_copy(m_aLastServerAddr, g_Config.m_BcVoiceChatServerAddress, sizeof(m_aLastServerAddr));
				RestartIfOnline();
			}
			char aMsg[256];
			str_format(aMsg, sizeof(aMsg), "Voice server: %s (%s)", Entry.m_Name.c_str(), Entry.m_Address.c_str());
			GameClient()->m_Chat.Echo(aMsg);
			return true;
		}

		// Allow setting a raw address directly.
		str_copy(g_Config.m_BcVoiceChatServerAddress, aArg, sizeof(g_Config.m_BcVoiceChatServerAddress));
		str_copy(m_aLastServerAddr, g_Config.m_BcVoiceChatServerAddress, sizeof(m_aLastServerAddr));
		RestartIfOnline();
		GameClient()->m_Chat.Echo("Voice server: updated");
		return true;
	}
	if(str_comp_nocase(aSub, "mute") == 0)
	{
		char aName[128];
		if(!ReadToken(p, aName, sizeof(aName)))
		{
			GameClient()->m_Chat.Echo("Usage: !voice mute \"nickname\"");
			return true;
		}

		const std::string Key = NormalizeVoiceNameKey(aName);
		if(Key.empty())
		{
			GameClient()->m_Chat.Echo("Voice mute: invalid nickname");
			return true;
		}

		if(m_MutedNameKeys.find(Key) != m_MutedNameKeys.end())
			m_MutedNameKeys.erase(Key);
		else
			m_MutedNameKeys.insert(Key);

		char aOut[512];
		WriteVoiceNameList(m_MutedNameKeys, aOut, sizeof(aOut));
		str_copy(g_Config.m_BcVoiceChatMutedNames, aOut, sizeof(g_Config.m_BcVoiceChatMutedNames));
		str_copy(m_aLastMutedNames, g_Config.m_BcVoiceChatMutedNames, sizeof(m_aLastMutedNames));
		m_PeerListDirty = true;

		GameClient()->m_Chat.Echo(m_MutedNameKeys.find(Key) != m_MutedNameKeys.end() ? "Voice mute: on" : "Voice mute: off");
		return true;
	}
	if(str_comp_nocase(aSub, "unmute") == 0)
	{
		char aName[128];
		if(!ReadToken(p, aName, sizeof(aName)))
		{
			GameClient()->m_Chat.Echo("Usage: !voice unmute \"nickname\"");
			return true;
		}

		const std::string Key = NormalizeVoiceNameKey(aName);
		if(Key.empty())
		{
			GameClient()->m_Chat.Echo("Voice unmute: invalid nickname");
			return true;
		}

		if(m_MutedNameKeys.find(Key) == m_MutedNameKeys.end())
		{
			GameClient()->m_Chat.Echo("Voice unmute: nickname not muted");
			return true;
		}

		m_MutedNameKeys.erase(Key);
		char aOut[512];
		WriteVoiceNameList(m_MutedNameKeys, aOut, sizeof(aOut));
		str_copy(g_Config.m_BcVoiceChatMutedNames, aOut, sizeof(g_Config.m_BcVoiceChatMutedNames));
		str_copy(m_aLastMutedNames, g_Config.m_BcVoiceChatMutedNames, sizeof(m_aLastMutedNames));
		m_PeerListDirty = true;
		GameClient()->m_Chat.Echo("Voice unmute: ok");
		return true;
	}
	if(str_comp_nocase(aSub, "volume") == 0)
	{
		char aName[128];
		char aValue[32];
		if(!ReadToken(p, aName, sizeof(aName)) || !ReadToken(p, aValue, sizeof(aValue)))
		{
			GameClient()->m_Chat.Echo("Usage: !voice volume \"nickname\" <0-200>");
			return true;
		}

		const std::string Key = NormalizeVoiceNameKey(aName);
		if(Key.empty())
		{
			GameClient()->m_Chat.Echo("Voice volume: invalid nickname");
			return true;
		}

		const int Percent = std::clamp(str_toint(aValue), 0, 200);
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
		str_format(aMsg, sizeof(aMsg), "Voice volume: %d%%", Percent);
		GameClient()->m_Chat.Echo(aMsg);
		return true;
	}

	GameClient()->m_Chat.Echo("Usage: !voice on/off/status/mode/server/mute/unmute/volume");
	return true;
}

void CVoiceChat::RenderHudMuteStatusIndicator(float HudWidth, float HudHeight, bool ForcePreview)
{
	const bool ShowMicMuted = g_Config.m_BcVoiceChatMicMuted != 0;
	const bool ShowHeadphonesMuted = g_Config.m_BcVoiceChatHeadphonesMuted != 0;
	if(!ForcePreview && !ShowMicMuted && !ShowHeadphonesMuted)
		return;

	const float TimeAnchorX = (HudWidth / 7.0f) * 3.0f;
	char aTimeStr[16];
	str_timestamp_format(aTimeStr, sizeof(aTimeStr), g_Config.m_TcShowLocalTimeSeconds ? "%H:%M.%S" : "%H:%M");
	const float TimeWidth = std::round(TextRender()->TextBoundingBox(5.0f, aTimeStr).m_W);
	const float TimeRectX = TimeAnchorX - (TimeWidth + 15.0f);

	const float BoxHeight = 12.5f;
	const float IconSize = 6.8f;
	const float Gap = 4.0f;
	const float Padding = 3.0f;
	const float BoxWidth = IconSize * 2.0f + Gap + Padding * 2.0f;
	const float SpacingToClock = 4.0f;
	float DrawX = TimeRectX - SpacingToClock - BoxWidth;
	float DrawY = 0.0f;
	DrawX = std::clamp(DrawX, 0.0f, maximum(0.0f, HudWidth - BoxWidth));
	DrawY = std::clamp(DrawY, 0.0f, maximum(0.0f, HudHeight - BoxHeight));

	const ColorRGBA BackgroundColor = ApplyVoiceHudAlpha(GameClient(), ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f));
	Graphics()->DrawRect(DrawX, DrawY, BoxWidth, BoxHeight, BackgroundColor, IGraphics::CORNER_B, 3.75f);

	struct SVoiceStatusIcon
	{
		const char *m_pIcon;
		bool m_Muted;
	};
	const SVoiceStatusIcon aIcons[2] = {
			{FontIcon::MICROPHONE, ShowMicMuted},
			{FontIcon::HEADPHONES, ShowHeadphonesMuted},
	};

	const float TextSize = 6.4f;
	const float CrossSize = 4.6f;
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

void CVoiceChat::RenderHudTalkingIndicator(float HudWidth, float HudHeight, bool ForcePreview)
{
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
		const float RowHeight = 14.0f * Scale;
		const float RowGap = 1.0f * Scale;
		const float RowPadding = 2.0f * Scale;
		const float AvatarSize = 9.0f * Scale;
		const float NameGap = 3.0f * Scale;
		const float IconSize = 5.0f * Scale;
		const float IconWidth = 8.0f * Scale;
		const float BoxWidth = 68.0f * Scale;
		const int RenderCount = minimum((int)vPreviewEntries.size(), 2);
		const float BoxHeight = RenderCount * RowHeight + maximum(0, RenderCount - 1) * RowGap;
		float DrawX = Layout.m_X;
		float DrawY = Layout.m_Y;
		DrawX = std::clamp(DrawX, 0.0f, maximum(0.0f, HudWidth - BoxWidth));
		DrawY = std::clamp(DrawY, 0.0f, maximum(0.0f, HudHeight - BoxHeight));

		const bool BackgroundEnabled = Layout.m_BackgroundEnabled;
		const ColorRGBA BackgroundColor = color_cast<ColorRGBA>(ColorHSLA(Layout.m_BackgroundColor, true));
		if(BackgroundEnabled)
		{
			const int Corners = VoiceHudBackgroundCorners(GameClient(), HudLayout::MODULE_VOICE_TALKERS, IGraphics::CORNER_ALL, DrawX, DrawY, BoxWidth, BoxHeight, HudWidth, HudHeight);
			Graphics()->DrawRect(DrawX, DrawY, BoxWidth, BoxHeight, ApplyVoiceHudAlpha(GameClient(), BackgroundColor), Corners, 4.0f * Scale);
		}

		for(int Index = 0; Index < RenderCount; ++Index)
		{
			const STalkingEntry &Entry = vPreviewEntries[Index];
			const float RowY = DrawY + Index * (RowHeight + RowGap);
			Graphics()->DrawRect(DrawX, RowY, BoxWidth, RowHeight, ApplyVoiceHudAlpha(GameClient(), ColorRGBA(0.06f, 0.07f, 0.09f, 0.72f)), IGraphics::CORNER_ALL, 4.0f * Scale);

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
			const ColorRGBA MicColor = ColorRGBA(0.68f, 1.0f, 0.68f, 0.85f);
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
	const float RowHeight = 14.0f * Scale;
	const float RowGap = 1.0f * Scale;
	const float RowPadding = 2.0f * Scale;
	const float AvatarSize = 9.0f * Scale;
	const float NameGap = 3.0f * Scale;
	const float IconSize = 5.0f * Scale;
	const float IconWidth = 8.0f * Scale;
	const float BoxWidth = 68.0f * Scale;
	const int RenderCount = minimum((int)vEntries.size(), ForcePreview ? 2 : 5);
	const float BoxHeight = RenderCount * RowHeight + maximum(0, RenderCount - 1) * RowGap;
	float DrawX = Layout.m_X;
	float DrawY = Layout.m_Y;
	DrawX = std::clamp(DrawX, 0.0f, maximum(0.0f, HudWidth - BoxWidth));
	DrawY = std::clamp(DrawY, 0.0f, maximum(0.0f, HudHeight - BoxHeight));

	const bool BackgroundEnabled = Layout.m_BackgroundEnabled;
	const ColorRGBA BackgroundColor = color_cast<ColorRGBA>(ColorHSLA(Layout.m_BackgroundColor, true));
	if(BackgroundEnabled)
	{
		const int Corners = VoiceHudBackgroundCorners(GameClient(), HudLayout::MODULE_VOICE_TALKERS, IGraphics::CORNER_ALL, DrawX, DrawY, BoxWidth, BoxHeight, HudWidth, HudHeight);
		Graphics()->DrawRect(DrawX, DrawY, BoxWidth, BoxHeight, ApplyVoiceHudAlpha(GameClient(), BackgroundColor), Corners, 4.0f * Scale);
	}

	for(int Index = 0; Index < RenderCount; ++Index)
	{
		const STalkingEntry &Entry = vEntries[Index];
		const float RowY = DrawY + Index * (RowHeight + RowGap);
		Graphics()->DrawRect(DrawX, RowY, BoxWidth, RowHeight, ApplyVoiceHudAlpha(GameClient(), ColorRGBA(0.06f, 0.07f, 0.09f, 0.72f)), IGraphics::CORNER_ALL, 4.0f * Scale);

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
		const ColorRGBA MicColor = ColorRGBA(0.68f, 1.0f, 0.68f, ForcePreview ? 0.85f : 0.92f);
		TextRender()->TextColor(ApplyVoiceHudAlpha(GameClient(), MicColor));
		const float MicGlyphWidth = TextRender()->TextWidth(IconSize, FontIcon::MICROPHONE, -1, -1.0f);
		TextRender()->Text(MicX + (IconWidth - MicGlyphWidth) * 0.5f, RowY + (RowHeight - IconSize) * 0.5f, IconSize, FontIcon::MICROPHONE, -1.0f);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	}

	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

void CVoiceChat::SetUiMousePos(vec2 Pos)
{
	const vec2 WindowSize = vec2(Graphics()->WindowWidth(), Graphics()->WindowHeight());
	const CUIRect *pScreen = Ui()->Screen();
	const vec2 UpdatedMousePos = Ui()->UpdatedMousePos();
	Pos = Pos / vec2(pScreen->w, pScreen->h) * WindowSize;
	Ui()->OnCursorMove(Pos.x - UpdatedMousePos.x, Pos.y - UpdatedMousePos.y);
}

void CVoiceChat::SetPanelActive(bool Active)
{
	if(m_PanelActive == Active)
		return;

	m_PanelActive = Active;
	if(m_PanelActive)
	{
		m_MouseUnlocked = true;
		m_LastMousePos = Ui()->MousePos();
		SetUiMousePos(Ui()->Screen()->Center());
	}
	else if(m_MouseUnlocked)
	{
		Ui()->ClosePopupMenus();
		m_MouseUnlocked = false;
		if(m_LastMousePos.has_value())
			SetUiMousePos(m_LastMousePos.value());
		m_LastMousePos = Ui()->MousePos();
	}
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
	SendGoodbye();
	CloseNetworking();
	OnReset();
	m_LastStartAttempt = 0;
	m_RuntimeState = RUNTIME_STOPPED;
}

bool CVoiceChat::OpenNetworking()
{
	if(!BestClientVoice::ParseAddress(g_Config.m_BcVoiceChatServerAddress, BestClientVoice::DEFAULT_PORT, m_ServerAddr))
	{
		dbg_msg("voice", "invalid server address '%s'", g_Config.m_BcVoiceChatServerAddress);
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
	m_vOnlineServers.clear();
	m_SelectedServerIndex = -1;
	m_AdvertisedTeam = std::numeric_limits<int>::min();
	return true;
}

void CVoiceChat::CloseNetworking()
{
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
	m_AdvertisedGameClientId = BestClientVoice::INVALID_GAME_CLIENT_ID - 1;
	m_AdvertisedTeam = std::numeric_limits<int>::min();
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

	SDL_AudioSpec WantedCapture = {};
	WantedCapture.freq = BestClientVoice::SAMPLE_RATE;
	WantedCapture.format = AUDIO_S16SYS;
	WantedCapture.channels = 1;
	WantedCapture.samples = BestClientVoice::FRAME_SIZE;
	WantedCapture.callback = nullptr;

	const char *pCaptureDeviceName = GetAudioDeviceNameByIndex(1, g_Config.m_BcVoiceChatInputDevice);
	m_CaptureDevice = SDL_OpenAudioDevice(pCaptureDeviceName, 1, &WantedCapture, nullptr, 0);
	if(m_CaptureDevice == 0 && pCaptureDeviceName)
	{
		dbg_msg("voice", "failed to open selected capture device, fallback to default: %s", SDL_GetError());
		m_CaptureDevice = SDL_OpenAudioDevice(nullptr, 1, &WantedCapture, nullptr, 0);
	}
	if(m_CaptureDevice == 0)
	{
		dbg_msg("voice", "failed to open capture device: %s", SDL_GetError());
		return false;
	}
	m_CaptureSpec = WantedCapture;

	SDL_AudioSpec WantedPlayback = {};
	WantedPlayback.freq = BestClientVoice::SAMPLE_RATE;
	WantedPlayback.format = AUDIO_S16SYS;
	WantedPlayback.channels = 2;
	WantedPlayback.samples = BestClientVoice::FRAME_SIZE;
	WantedPlayback.callback = nullptr;

	const char *pPlaybackDeviceName = GetAudioDeviceNameByIndex(0, g_Config.m_BcVoiceChatOutputDevice);
	m_PlaybackDevice = SDL_OpenAudioDevice(pPlaybackDeviceName, 0, &WantedPlayback, nullptr, 0);
	if(m_PlaybackDevice == 0 && pPlaybackDeviceName)
	{
		dbg_msg("voice", "failed to open selected playback device, fallback to default: %s", SDL_GetError());
		m_PlaybackDevice = SDL_OpenAudioDevice(nullptr, 0, &WantedPlayback, nullptr, 0);
	}
	if(m_PlaybackDevice == 0)
	{
		dbg_msg("voice", "failed to open playback device: %s", SDL_GetError());
		CloseAudioDevices();
		return false;
	}
	m_PlaybackSpec = WantedPlayback;

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
	if(m_PlaybackDevice != 0)
	{
		SDL_CloseAudioDevice(m_PlaybackDevice);
		m_PlaybackDevice = 0;
	}
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
	return true;
}

void CVoiceChat::DestroyEncoder()
{
	if(m_pEncoder)
	{
		opus_encoder_destroy(m_pEncoder);
		m_pEncoder = nullptr;
	}
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
	m_PeerVolumeSliderButtons.clear();
	InvalidatePeerCaches();
}

#if 0
void CVoiceChat::SendGroupInvite(const std::string &Nick)
{
	if(!m_Socket || !m_Registered || !m_HasServerAddr || !m_ServerSupportsGroups)
		return;
	if(m_CurrentVoiceGroupId == 0)
	{
		GameClient()->m_Chat.Echo("Voicegroup: нельзя приглашать в team0");
		return;
	}

	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	int TargetId = -1;
	int TargetCount = 0;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(!GameClient()->m_aClients[ClientId].m_Active)
			continue;
		if(ClientId == LocalId)
			continue;
		if(str_comp(GameClient()->m_aClients[ClientId].m_aName, Nick.c_str()) == 0)
		{
			TargetId = ClientId;
			TargetCount++;
		}
	}
	if(TargetId < 0)
	{
		// Fallback: case-insensitive match.
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
		{
			if(!GameClient()->m_aClients[ClientId].m_Active)
				continue;
			if(ClientId == LocalId)
				continue;
			if(str_comp_nocase(GameClient()->m_aClients[ClientId].m_aName, Nick.c_str()) == 0)
			{
				TargetId = ClientId;
				TargetCount++;
			}
		}
	}
	if(TargetId < 0)
	{
		GameClient()->m_Chat.Echo("Voicegroup invite: игрок не найден");
		return;
	}
	if(TargetCount > 1)
		GameClient()->m_Chat.Echo("Voicegroup invite: найдено несколько игроков, выбран первый");

	const int16_t TargetGameClientId = (int16_t)TargetId;

	std::vector<uint8_t> vPacket;
	vPacket.reserve(14);
	BestClientVoice::WriteHeader(vPacket, BestClientVoice::PACKET_GROUP_INVITE_REQ);
	BestClientVoice::WriteU16(vPacket, m_CurrentVoiceGroupId);
	BestClientVoice::WriteS16(vPacket, TargetGameClientId);
	net_udp_send(m_Socket, &m_ServerAddr, vPacket.data(), (int)vPacket.size());
	GameClient()->m_Chat.Echo("Voicegroup invite: отправлено");
}

void CVoiceChat::UpdateAutoVoiceGroup()
{
	if(!m_ServerSupportsGroups || !m_Registered)
		return;
	if(m_ManualGroupActive)
		return;

	const int Team = LocalTeam();
	char aName[32];
	if(Team == TEAM_SPECTATORS || Team <= 0)
		str_copy(aName, "team0", sizeof(aName));
	else
		str_format(aName, sizeof(aName), "team%d", Team);

	const std::string DesiredKey = NormalizeVoiceGroupNameKey(aName);
	if(DesiredKey == m_CurrentVoiceGroupNameKey)
		return;
	if(!m_PendingCreateNameKey.empty() && m_PendingCreateNameKey == DesiredKey)
		return;
	if(!m_PendingJoinNameKey.empty() && !m_PendingJoinManual && m_PendingJoinNameKey == DesiredKey)
		return;
	const int64_t NowTick = time_get();
	if(m_LastAutoTeamCreateTick > 0 && m_LastAutoTeamCreateNameKey == DesiredKey && NowTick - m_LastAutoTeamCreateTick < time_freq() / 2)
		return;

	// Try joining; if the server reports "not found", we'll auto-create.
	SendGroupJoinByName(aName, false);
}

#endif
void CVoiceChat::SendHello()
{
	if(!m_Socket || !m_HasServerAddr)
		return;

	const std::string RoomKey = CurrentRoomKey();
	const int LocalClientId = LocalGameClientId();
	const int VoiceTeam = LocalVoiceTeam();
	std::vector<uint8_t> vPacket;
	const uint16_t RoomKeySize = (uint16_t)minimum<size_t>(RoomKey.size(), BestClientVoice::MAX_ROOM_KEY_LENGTH);
	vPacket.reserve(24 + RoomKeySize);
	BestClientVoice::WriteHeader(vPacket, BestClientVoice::PACKET_HELLO);
	BestClientVoice::WriteU16(vPacket, 1);
	BestClientVoice::WriteU16(vPacket, RoomKeySize);
	vPacket.insert(vPacket.end(), RoomKey.begin(), RoomKey.begin() + RoomKeySize);
	BestClientVoice::WriteS16(vPacket, (int16_t)LocalClientId);
	BestClientVoice::WriteS16(vPacket, (int16_t)VoiceTeam);
	net_udp_send(m_Socket, &m_ServerAddr, vPacket.data(), (int)vPacket.size());
	m_LastHelloTick = time_get();
	m_LastHeartbeatTick = m_LastHelloTick;
	m_AdvertisedRoomKey.assign(RoomKey.begin(), RoomKey.begin() + RoomKeySize);
	m_AdvertisedGameClientId = LocalClientId;
	m_AdvertisedTeam = VoiceTeam;
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

				char aAddr[NETADDR_MAXSTRSIZE];
				net_addr_str(&m_ServerAddr, aAddr, sizeof(aAddr), true);
				const std::string ServerAddr(aAddr);
				auto It = std::find(m_vOnlineServers.begin(), m_vOnlineServers.end(), ServerAddr);
				if(It == m_vOnlineServers.end())
					m_vOnlineServers.push_back(ServerAddr);
				if(!m_vServerEntries.empty())
				{
					for(size_t i = 0; i < m_vServerEntries.size(); ++i)
					{
						if(str_comp(m_vServerEntries[i].m_Address.c_str(), ServerAddr.c_str()) == 0)
						{
							m_SelectedServerIndex = (int)i;
							break;
						}
					}
				}
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

			for(uint16_t i = 0; i < PeerCount; ++i)
			{
				uint16_t PeerId = 0;
				if(!BestClientVoice::ReadU16(pRawData, DataSize, Offset, PeerId))
				{
					vSeenPeerIds.clear();
					break;
				}
				if(PeerId == 0 || PeerId == m_ClientVoiceId)
					continue;
				vSeenPeerIds.insert(PeerId);
				CRemotePeer &Peer = m_Peers[PeerId];
				Peer.m_LastReceiveTick = Now;
			}

			for(auto It = m_Peers.begin(); It != m_Peers.end();)
			{
				if(vSeenPeerIds.find(It->first) == vSeenPeerIds.end())
				{
					if(It->second.m_pDecoder)
						opus_decoder_destroy(It->second.m_pDecoder);
					m_PeerVolumePercent.erase(It->first);
					m_PeerVolumeSliderButtons.erase(It->first);
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
			for(uint16_t i = 0; i < PeerCount; ++i)
			{
				uint16_t PeerId = 0;
				int16_t AnnouncedGameClientId = BestClientVoice::INVALID_GAME_CLIENT_ID;
				if(!BestClientVoice::ReadU16(pRawData, DataSize, Offset, PeerId) ||
					!BestClientVoice::ReadS16(pRawData, DataSize, Offset, AnnouncedGameClientId))
				{
					vSeenPeerIds.clear();
					break;
				}
				if(PeerId == 0 || PeerId == m_ClientVoiceId)
					continue;
				vSeenPeerIds.insert(PeerId);
				CRemotePeer &Peer = m_Peers[PeerId];
				Peer.m_LastReceiveTick = Now;
				Peer.m_AnnouncedGameClientId = AnnouncedGameClientId;
			}

			for(auto It = m_Peers.begin(); It != m_Peers.end();)
			{
				if(vSeenPeerIds.find(It->first) == vSeenPeerIds.end())
				{
					if(It->second.m_pDecoder)
						opus_decoder_destroy(It->second.m_pDecoder);
					m_PeerVolumePercent.erase(It->first);
					m_PeerVolumeSliderButtons.erase(It->first);
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

		#if 0
		if(Type == BestClientVoice::PACKET_GROUP_LIST)
		{
			if(!m_ServerSupportsGroups)
				continue;

			uint16_t GroupCount = 0;
			if(!BestClientVoice::ReadU16(pRawData, DataSize, Offset, GroupCount))
				continue;

			m_VoiceGroups.clear();
			m_VoiceGroupNameToId.clear();
			for(uint16_t i = 0; i < GroupCount; ++i)
			{
				uint16_t GroupId = 0;
				uint8_t Privacy = VOICE_GROUP_PRIVATE;
				uint16_t Members = 0;
				std::string Name;
				if(!BestClientVoice::ReadU16(pRawData, DataSize, Offset, GroupId) ||
					!BestClientVoice::ReadU8(pRawData, DataSize, Offset, Privacy) ||
					!BestClientVoice::ReadU16(pRawData, DataSize, Offset, Members) ||
					!ReadVoiceString(pRawData, DataSize, Offset, Name))
				{
					m_VoiceGroups.clear();
					m_VoiceGroupNameToId.clear();
					break;
				}

				SVoiceGroupInfo Info;
				Info.m_Name = Name;
				Info.m_Private = Privacy == VOICE_GROUP_PRIVATE;
				Info.m_Members = (int)Members;
				m_VoiceGroups[GroupId] = Info;

				const std::string Key = NormalizeVoiceGroupNameKey(Name.c_str());
				if(!Key.empty())
					m_VoiceGroupNameToId[Key] = GroupId;
			}

			if(m_PrintGroupListOnNext)
			{
				m_PrintGroupListOnNext = false;
				GameClient()->m_Chat.Echo("Voice groups:");
				std::vector<uint16_t> vIds;
				vIds.reserve(m_VoiceGroups.size());
				for(const auto &Pair : m_VoiceGroups)
					vIds.push_back(Pair.first);
				std::sort(vIds.begin(), vIds.end());
				for(uint16_t GroupId : vIds)
				{
					const auto It = m_VoiceGroups.find(GroupId);
					if(It == m_VoiceGroups.end())
						continue;
					const SVoiceGroupInfo &Info = It->second;
					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "  %s (%s) members=%d", Info.m_Name.c_str(), Info.m_Private ? "private" : "public", Info.m_Members);
					GameClient()->m_Chat.Echo(aBuf);
				}
			}
			continue;
		}

		if(Type == BestClientVoice::PACKET_GROUP_INVITE_EVT)
		{
			if(!m_ServerSupportsGroups)
				continue;

			uint16_t GroupId = 0;
			uint8_t Privacy = VOICE_GROUP_PRIVATE;
			std::string Name;
			int16_t InviterGameClientId = BestClientVoice::INVALID_GAME_CLIENT_ID;
			if(!BestClientVoice::ReadU16(pRawData, DataSize, Offset, GroupId) ||
				!BestClientVoice::ReadU8(pRawData, DataSize, Offset, Privacy) ||
				!ReadVoiceString(pRawData, DataSize, Offset, Name) ||
				!BestClientVoice::ReadS16(pRawData, DataSize, Offset, InviterGameClientId))
				continue;

			m_PendingInviteGroupId = GroupId;
			m_PendingInviteGroupName = Name;
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "Вас пригласили в voicegroup \"%s\" (%s). Напишите !voicegroup join", Name.c_str(), Privacy == VOICE_GROUP_PRIVATE ? "private" : "public");
			GameClient()->m_Chat.Echo(aBuf);
			continue;
		}

		if(Type == BestClientVoice::PACKET_GROUP_CREATE_ACK)
		{
			if(!m_ServerSupportsGroups)
				continue;

			uint8_t Status = VOICE_GROUP_STATUS_INVALID;
			uint16_t GroupId = 0;
			uint8_t Privacy = VOICE_GROUP_PRIVATE;
			std::string Name;
			if(!BestClientVoice::ReadU8(pRawData, DataSize, Offset, Status) ||
				!BestClientVoice::ReadU16(pRawData, DataSize, Offset, GroupId) ||
				!BestClientVoice::ReadU8(pRawData, DataSize, Offset, Privacy) ||
				!ReadVoiceString(pRawData, DataSize, Offset, Name))
				continue;

			const std::string NameKey = NormalizeVoiceGroupNameKey(Name.c_str());
			if(Status == VOICE_GROUP_STATUS_OK)
			{
				SVoiceGroupInfo Info;
				Info.m_Name = Name;
				Info.m_Private = Privacy == VOICE_GROUP_PRIVATE;
				Info.m_Members = 0;
				m_VoiceGroups[GroupId] = Info;
				if(!NameKey.empty())
					m_VoiceGroupNameToId[NameKey] = GroupId;

				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "Voicegroup create: ok (%s)", Name.c_str());
				GameClient()->m_Chat.Echo(aBuf);

				if(!m_PendingCreateNameKey.empty() && m_PendingCreateNameKey == NameKey)
				{
					const bool Manual = m_PendingCreateManual;
					m_PendingCreateNameKey.clear();
					m_PendingCreateManual = false;
					SendGroupJoinById(GroupId, Manual, false);
				}
			}
			else
			{
				GameClient()->m_Chat.Echo(Status == VOICE_GROUP_STATUS_EXISTS ? "Voicegroup create: уже существует" : "Voicegroup create: ошибка");
				m_PendingCreateNameKey.clear();
				m_PendingCreateManual = false;
			}
			continue;
		}

		if(Type == BestClientVoice::PACKET_GROUP_JOIN_ACK)
		{
			if(!m_ServerSupportsGroups)
				continue;

			uint8_t Status = VOICE_GROUP_STATUS_INVALID;
			uint16_t GroupId = 0;
			uint8_t Privacy = VOICE_GROUP_PRIVATE;
			std::string Name;
			if(!BestClientVoice::ReadU8(pRawData, DataSize, Offset, Status) ||
				!BestClientVoice::ReadU16(pRawData, DataSize, Offset, GroupId) ||
				!BestClientVoice::ReadU8(pRawData, DataSize, Offset, Privacy) ||
				!ReadVoiceString(pRawData, DataSize, Offset, Name))
				continue;

			const std::string NameKey = NormalizeVoiceGroupNameKey(Name.c_str());
			const bool PendingManual = m_PendingJoinManual;
			const bool PendingConsumeInvite = m_PendingJoinConsumeInvite;
			const std::string PendingJoinKey = m_PendingJoinNameKey;
			m_PendingJoinNameKey.clear();
			m_PendingJoinManual = false;
			m_PendingJoinConsumeInvite = false;

			if(Status == VOICE_GROUP_STATUS_OK)
			{
				m_CurrentVoiceGroupId = GroupId;
				m_CurrentVoiceGroupNameKey = NameKey;
				m_ManualGroupActive = PendingManual;
				if(PendingConsumeInvite)
				{
					m_PendingInviteGroupId.reset();
					m_PendingInviteGroupName.clear();
					m_ManualGroupActive = true;
				}

				SVoiceGroupInfo Info;
				Info.m_Name = Name;
				Info.m_Private = Privacy == VOICE_GROUP_PRIVATE;
				Info.m_Members = 0;
				m_VoiceGroups[GroupId] = Info;
				if(!NameKey.empty())
					m_VoiceGroupNameToId[NameKey] = GroupId;

				// Drop old peers immediately (switching groups must isolate audio right away).
				ClearPeerState();
				if(m_PlaybackDevice)
					SDL_ClearQueuedAudio(m_PlaybackDevice);

				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "Voicegroup: joined %s (%s)", Name.c_str(), Info.m_Private ? "private" : "public");
				GameClient()->m_Chat.Echo(aBuf);
			}
			else
			{
				if(Status == VOICE_GROUP_STATUS_NOT_INVITED)
					GameClient()->m_Chat.Echo("Voicegroup join: нужен инвайт");
				else if(Status == VOICE_GROUP_STATUS_FORBIDDEN)
					GameClient()->m_Chat.Echo("Voicegroup join: запрещено");
				else if(Status == VOICE_GROUP_STATUS_NOT_FOUND)
					GameClient()->m_Chat.Echo("Voicegroup join: не найдено");
				else
					GameClient()->m_Chat.Echo("Voicegroup join: ошибка");

				// Auto team group: if missing, create it once.
				if(!PendingManual && Status == VOICE_GROUP_STATUS_NOT_FOUND && !PendingJoinKey.empty())
				{
					if(str_startswith(PendingJoinKey.c_str(), "team") && PendingJoinKey != "team0")
					{
						const int64_t NowTick = time_get();
						const bool Recently = (m_LastAutoTeamCreateTick > 0 && NowTick - m_LastAutoTeamCreateTick < time_freq() * 2 && m_LastAutoTeamCreateNameKey == PendingJoinKey);
						if(!Recently)
						{
							m_LastAutoTeamCreateTick = NowTick;
							m_LastAutoTeamCreateNameKey = PendingJoinKey;
							m_PendingCreateNameKey = PendingJoinKey;
							m_PendingCreateManual = false;
							SendGroupCreate(PendingJoinKey, true);
						}
					}
				}
			}
			continue;
		}

		if(Type == BestClientVoice::PACKET_GROUP_SET_PRIVACY_ACK)
		{
			if(!m_ServerSupportsGroups)
				continue;

			uint8_t Status = VOICE_GROUP_STATUS_INVALID;
			uint16_t GroupId = 0;
			uint8_t Privacy = VOICE_GROUP_PRIVATE;
			if(!BestClientVoice::ReadU8(pRawData, DataSize, Offset, Status) ||
				!BestClientVoice::ReadU16(pRawData, DataSize, Offset, GroupId) ||
				!BestClientVoice::ReadU8(pRawData, DataSize, Offset, Privacy))
				continue;

			if(Status == VOICE_GROUP_STATUS_OK)
			{
				auto It = m_VoiceGroups.find(GroupId);
				if(It != m_VoiceGroups.end())
					It->second.m_Private = Privacy == VOICE_GROUP_PRIVATE;
				GameClient()->m_Chat.Echo(Privacy == VOICE_GROUP_PRIVATE ? "Voicegroup: set private" : "Voicegroup: set public");
			}
			else
			{
				GameClient()->m_Chat.Echo("Voicegroup privacy: ошибка");
			}
			continue;
		}

		#endif
		if(Type != BestClientVoice::PACKET_VOICE_RELAY)
			continue;

		if(!m_Registered || m_ClientVoiceId == 0)
			continue;

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
			continue;
		}
		if(Offset + OpusSize > DataSize || OpusSize == 0 || OpusSize > BestClientVoice::MAX_OPUS_PACKET_SIZE)
			continue;
		if(SenderId == m_ClientVoiceId)
			continue;

		auto ItPeer = m_Peers.find(SenderId);
		if(ItPeer == m_Peers.end())
			continue; // Ignore stale/unknown sender ids not present in current peer list.
		CRemotePeer &Peer = ItPeer->second;
		if(m_PeerVolumePercent.find(SenderId) == m_PeerVolumePercent.end())
			m_PeerVolumePercent[SenderId] = 100;
		Peer.m_Team = Team;
		Peer.m_Position = vec2((float)PosX, (float)PosY);
		const int64_t Now = time_get();
		Peer.m_LastReceiveTick = Now;
		Peer.m_LastVoiceTick = Now;
		m_TalkingStateDirty = true;

		if(!Peer.m_pDecoder)
		{
			int Error = 0;
			Peer.m_pDecoder = opus_decoder_create(BestClientVoice::SAMPLE_RATE, BestClientVoice::CHANNELS, &Error);
			if(Error != OPUS_OK || !Peer.m_pDecoder)
			{
				Peer.m_pDecoder = nullptr;
				continue;
			}
		}

		int16_t aDecoded[BestClientVoice::FRAME_SIZE];
		if(Peer.m_HasSequence)
		{
			if(!IsForwardSequence(Peer.m_LastSequence, Sequence))
				continue;

			const uint16_t Expected = (uint16_t)(Peer.m_LastSequence + 1);
			const uint16_t MissingPackets = (uint16_t)(Sequence - Expected);
			if(MissingPackets > 0)
			{
				if(MissingPackets > MAX_PACKET_GAP_FOR_PLC)
				{
					Peer.m_DecodedPcm.Clear();
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
			continue;

		Peer.m_LastSequence = Sequence;
		Peer.m_HasSequence = true;

		const size_t Dropped = Peer.m_DecodedPcm.PushBack(aDecoded, (size_t)DecodedSamples);
		if(Dropped > 0 && Peer.m_DecodedPcm.Size() > PLAYBACK_MAX_RESYNC_FRAMES)
			Peer.m_DecodedPcm.DiscardFront(Peer.m_DecodedPcm.Size() - PLAYBACK_MAX_RESYNC_FRAMES);
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
	if(g_Config.m_BcVoiceChatActivationMode == 1 && !m_PushToTalkPressed && !g_Config.m_BcVoiceChatMicCheck)
	{
		m_WasTransmitActive = false;
		m_AutoActivationUntilTick = 0;
		m_MicLevel = mix(m_MicLevel, 0.0f, 0.25f);
		if(SDL_GetQueuedAudioSize(m_CaptureDevice) > 0)
			SDL_ClearQueuedAudio(m_CaptureDevice);
		return;
	}

	int16_t aCaptureRaw[CAPTURE_READ_SAMPLES * 2];
	const int BytesRead = SDL_DequeueAudio(m_CaptureDevice, aCaptureRaw, sizeof(aCaptureRaw));
	if(BytesRead > 0)
	{
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

	while(m_CapturePcm.Size() >= (size_t)BestClientVoice::FRAME_SIZE)
	{
		int16_t aFrameRaw[BestClientVoice::FRAME_SIZE];
		int16_t aFrame[BestClientVoice::FRAME_SIZE];
		m_CapturePcm.PopFront(aFrameRaw, BestClientVoice::FRAME_SIZE);

		// Apply mic gain with a per-frame limiter to avoid hard clipping.
		const int MicGainPercent = std::clamp(g_Config.m_BcVoiceChatMicGain, 0, 300);
		int Peak = 0;
		int aScaled[BestClientVoice::FRAME_SIZE];
		for(int i = 0; i < BestClientVoice::FRAME_SIZE; ++i)
		{
			const int Scaled = (aFrameRaw[i] * MicGainPercent) / 100;
			aScaled[i] = Scaled;
			Peak = maximum(Peak, absolute(Scaled));
		}
		const int TargetPeak = 30000;
		const float LimiterScale = (Peak > TargetPeak && Peak > 0) ? (TargetPeak / (float)Peak) : 1.0f;
		for(int i = 0; i < BestClientVoice::FRAME_SIZE; ++i)
		{
			const int Out = round_to_int(aScaled[i] * LimiterScale);
			aFrame[i] = (int16_t)std::clamp(Out, (int)std::numeric_limits<int16_t>::min(), (int)std::numeric_limits<int16_t>::max());
		}

		int64_t AvgLevel = 0;
		for(int i = 0; i < BestClientVoice::FRAME_SIZE; ++i)
			AvgLevel += absolute(aFrame[i]);
		AvgLevel /= BestClientVoice::FRAME_SIZE;
		const float LevelLinear = std::clamp((float)AvgLevel / 32767.0f, 0.0f, 1.0f);
		const float PeakLinear = std::clamp((float)Peak / 32767.0f, 0.0f, 1.0f);
		const float MicLevelScale = 2.0f;
		const float LevelLinearScaled = std::clamp(LevelLinear * MicLevelScale, 0.0f, 1.0f);
		const float ActivationLevel = maximum(LevelLinearScaled, PeakLinear * 0.85f);
		m_MicLevel = mix(m_MicLevel, LevelLinearScaled, 0.2f);

		if(g_Config.m_BcVoiceChatMicCheck)
			m_MicMonitorPcm.PushBack(aFrame, BestClientVoice::FRAME_SIZE);

		if(!ShouldTransmit())
		{
			m_WasTransmitActive = false;
			continue;
		}

		const int64_t NowTick = time_get();
		bool Active = false;
		if(g_Config.m_BcVoiceChatActivationMode == 1)
		{
			// Push-to-talk
			Active = m_PushToTalkPressed;
		}
		else
		{
			// Automatic activation (VAD): trigger by RMS/peak level and keep it alive shortly to avoid choppy speech.
			const float StartThreshold = 0.035f;
			const int64_t HangoverTicks = (time_freq() * 500) / 1000;
			if(ActivationLevel >= StartThreshold)
				m_AutoActivationUntilTick = NowTick + HangoverTicks;
			Active = m_AutoActivationUntilTick > 0 && NowTick <= m_AutoActivationUntilTick;
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
			SendVoiceFrame(aEncoded, EncodedSize, LocalVoiceTeam(), LocalPosition());
	}
}

void CVoiceChat::ProcessPlayback()
{
	if(!m_PlaybackDevice)
		return;
	if(!HasPendingPlaybackAudio() && SDL_GetQueuedAudioSize(m_PlaybackDevice) == 0)
		return;

	const Uint32 QueuedBytes = SDL_GetQueuedAudioSize(m_PlaybackDevice);
	const Uint32 TargetBytes = (Uint32)(PLAYBACK_TARGET_FRAMES * 2 * sizeof(int16_t));
	if(QueuedBytes >= TargetBytes)
		return;

	const float MasterVolume = g_Config.m_BcVoiceChatHeadphonesMuted ? 0.0f : (g_Config.m_BcVoiceChatVolume / 100.0f);

	while(SDL_GetQueuedAudioSize(m_PlaybackDevice) < TargetBytes)
	{
		int16_t aOut[BestClientVoice::FRAME_SIZE * 2];
		mem_zero(aOut, sizeof(aOut));
		int aMix[BestClientVoice::FRAME_SIZE * 2];
		mem_zero(aMix, sizeof(aMix));

		const float MicCheckGain = g_Config.m_BcVoiceChatMicCheck ? 0.75f * MasterVolume : 0.0f;
		if(MicCheckGain <= 0.0f)
		{
			m_MicMonitorPcm.DiscardFront(minimum(m_MicMonitorPcm.Size(), (size_t)BestClientVoice::FRAME_SIZE));
		}
		else
		{
			int16_t aMicFrame[BestClientVoice::FRAME_SIZE] = {};
			const size_t MicSamples = m_MicMonitorPcm.PopFront(aMicFrame, BestClientVoice::FRAME_SIZE);
			for(size_t i = 0; i < MicSamples; ++i)
			{
				const int Mixed = (int)(aMicFrame[i] * MicCheckGain);
				aMix[i * 2u] += Mixed;
				aMix[i * 2u + 1] += Mixed;
			}
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
				Peer.m_DecodedPcm.DiscardFront(minimum(Peer.m_DecodedPcm.Size(), (size_t)BestClientVoice::FRAME_SIZE));
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
		}

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
		SDL_QueueAudio(m_PlaybackDevice, aOut, sizeof(aOut));
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
			m_PeerVolumeSliderButtons.erase(It->first);
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
				else if(m_PeerVolumePercent.find(PeerId) == m_PeerVolumePercent.end())
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
		if(ClientId >= 0 && ClientId < MAX_CLIENTS)
		{
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
	return true;
}

bool CVoiceChat::ShouldStartVoicePipeline(bool Online) const
{
	if(!Online || g_Config.m_BcVoiceChatEnable == 0)
		return false;
	if(g_Config.m_BcVoiceChatActivationMode == 0)
		return true;
	return m_PanelActive || m_PushToTalkPressed;
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

bool CVoiceChat::HasRecentVoiceActivity(int64_t Now) const
{
	if(m_PushToTalkPressed)
		return true;
	if(g_Config.m_BcVoiceChatActivationMode == 0 && m_AutoActivationUntilTick > 0 && Now <= m_AutoActivationUntilTick)
		return true;
	if(HasPendingPlaybackAudio())
		return true;
	for(const auto &PeerPair : m_Peers)
	{
		if(PeerPair.second.m_LastVoiceTick > 0 && Now - PeerPair.second.m_LastVoiceTick <= VOICE_IDLE_SHUTDOWN_SECONDS * time_freq())
			return true;
	}
	return false;
}

bool CVoiceChat::ShouldKeepVoicePipelineActive() const
{
	if(m_PanelActive)
		return true;
	if(g_Config.m_BcVoiceChatActivationMode == 0)
		return true;
	return HasRecentVoiceActivity(time_get());
}

int CVoiceChat::LocalTeam() const
{
	if(GameClient()->m_Snap.m_LocalClientId < 0)
		return 0;
	return GameClient()->m_Teams.Team(GameClient()->m_Snap.m_LocalClientId);
}

int CVoiceChat::LocalVoiceTeam() const
{
	return maximum(LocalTeam(), 0);
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

	// Don't guess by proximity until we have an actual voice packet with position.
	if(Peer.m_LastVoiceTick <= 0)
		return -1;

	int BestClientId = -1;
	float BestDistSq = std::numeric_limits<float>::max();
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		const CGameClient::CClientData &ClientData = GameClient()->m_aClients[ClientId];
		if(!ClientData.m_Active)
			continue;
		const CNetObj_PlayerInfo *pInfo = GameClient()->m_Snap.m_apPlayerInfos[ClientId];
		if(pInfo && pInfo->m_Local)
			continue;

		vec2 Pos = ClientData.m_RenderPos;
		if(GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
		{
			Pos = vec2(
				(float)GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur.m_X,
				(float)GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur.m_Y);
		}

		const vec2 Diff = Pos - Peer.m_Position;
		const float DistSq = Diff.x * Diff.x + Diff.y * Diff.y;
		if(DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestClientId = ClientId;
		}
	}

	// If no client is reasonably close, keep it as unknown participant id.
	const float MaxMatchDist = 700.0f;
	if(BestClientId < 0 || BestDistSq > MaxMatchDist * MaxMatchDist)
		return -1;

	return BestClientId;
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
	(void)Peer;
	return 1.0f;
}

bool CVoiceChat::IsClientTalking(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;

	const CNetObj_PlayerInfo *pPlayerInfo = GameClient()->m_Snap.m_apPlayerInfos[ClientId];
	const bool IsLocalClient = (pPlayerInfo && pPlayerInfo->m_Local) || ClientId == GameClient()->m_Snap.m_LocalClientId;

	// Local speaking state should not depend on peer mapping.
	if(IsLocalClient)
	{
		if(g_Config.m_BcVoiceChatMicMuted)
			return false;

		// Light up while push-to-talk is held.
		if(g_Config.m_BcVoiceChatActivationMode == 1)
			return m_PushToTalkPressed;

		// Automatic activation: light up while VAD is active (incl. hangover).
		const int64_t NowTick = time_get();
		return m_AutoActivationUntilTick > 0 && NowTick <= m_AutoActivationUntilTick;
	}

	if(!m_Registered)
		return false;

	for(const STalkingEntry &Entry : m_vTalkingEntries)
	{
		if(!Entry.m_IsLocal && Entry.m_ClientId == ClientId)
			return true;
	}

	return false;
}

std::optional<int> CVoiceChat::GetClientVolumePercent(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return std::nullopt;

	for(const auto &ResolvedPair : m_PeerResolvedClientIds)
	{
		if(ResolvedPair.second != ClientId)
			continue;

		if(const auto It = m_PeerVolumePercent.find(ResolvedPair.first); It != m_PeerVolumePercent.end())
			return std::clamp(It->second, 0, 200);
		return 100;
	}

	return std::nullopt;
}

void CVoiceChat::SetClientVolumePercent(int ClientId, int VolumePercent)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	const int ClampedVolume = std::clamp(VolumePercent, 0, 200);
	for(const auto &ResolvedPair : m_PeerResolvedClientIds)
	{
		if(ResolvedPair.second == ClientId)
			m_PeerVolumePercent[ResolvedPair.first] = ClampedVolume;
	}
}

std::vector<uint16_t> CVoiceChat::SortedPeerIds() const
{
	return m_vSortedPeerIds;
}

void CVoiceChat::RenderServersSection(CUIRect View)
{
	CUIRect Top;
	View.HSplitTop(24.0f, &Top, &View);
	Ui()->DoLabel(&Top, Localize("Voice servers"), 15.0f, TEXTALIGN_ML);
	View.HSplitTop(8.0f, nullptr, &View);

	CUIRect RoomCard;
	View.HSplitTop(68.0f, &RoomCard, &View);
	RoomCard.Draw(VoiceCardBgColor(), IGraphics::CORNER_ALL, 6.0f);
	CUIRect CardInner = RoomCard;
	CardInner.Margin(10.0f, &CardInner);

	CUIRect CardIcon, CardText;
	CardInner.VSplitLeft(24.0f, &CardIcon, &CardText);
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	Ui()->DoLabel(&CardIcon, FontIcon::NETWORK_WIRED, 14.0f, TEXTALIGN_MC);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

	CUIRect CardTitle, CardLine;
	CardText.HSplitTop(24.0f, &CardTitle, &CardLine);
	Ui()->DoLabel(&CardTitle, Localize("Servers"), 14.0f, TEXTALIGN_ML);

	auto ReloadServerList = [&]() {
		ResetServerListTask();
		CloseServerListPingSocket();
		m_vServerEntries.clear();
		m_ServerRowButtons.clear();
		m_SelectedServerIndex = -1;
		FetchServerList();
	};

	CUIRect StatusLine, ReloadButton;
	CardLine.VSplitRight(92.0f, &StatusLine, &ReloadButton);
	char aStatus[192];
	str_format(aStatus, sizeof(aStatus), "%s: %s", Localize("Current"), m_Registered ? Localize("Connected") : Localize("Offline"));
	Ui()->DoLabel(&StatusLine, aStatus, 11.0f, TEXTALIGN_ML);
	if(GameClient()->m_Menus.DoButton_Menu(&m_ReloadServerListButton, Localize("Reload"), 0, &ReloadButton))
		ReloadServerList();
	View.HSplitTop(10.0f, nullptr, &View);

	CUIRect ListLabel;
	View.HSplitTop(20.0f, &ListLabel, &View);
	Ui()->DoLabel(&ListLabel, Localize("Available servers"), 12.0f, TEXTALIGN_ML);
	View.HSplitTop(4.0f, nullptr, &View);

	if(m_vServerEntries.empty())
	{
		const bool IsLoadingServerList = m_pServerListTask && !m_pServerListTask->Done();
		Ui()->DoLabel(&View, IsLoadingServerList ? Localize("Loading server list...") : Localize("No servers loaded. Press Reload"), 12.0f, TEXTALIGN_ML);
		return;
	}

	static CScrollRegion s_ServerListScrollRegion;
	static vec2 s_ServerListScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 30.0f;
	ScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	ScrollParams.m_ScrollbarMargin = 4.0f;
	s_ServerListScrollRegion.Begin(&View, &s_ServerListScrollOffset, &ScrollParams);
	View.y += s_ServerListScrollOffset.y;

	for(size_t i = 0; i < m_vServerEntries.size(); ++i)
	{
		const auto &Entry = m_vServerEntries[i];
		CUIRect Row;
		View.HSplitTop(30.0f, &Row, &View);
		const bool RowVisible = s_ServerListScrollRegion.AddRect(Row);
		CUIRect Spacing;
		View.HSplitTop(4.0f, &Spacing, &View);
		s_ServerListScrollRegion.AddRect(Spacing);
		if(!RowVisible)
			continue;

		CButtonContainer &Button = m_ServerRowButtons[i];
		const bool Selected = (int)i == m_SelectedServerIndex;
		const int Clicked = Ui()->DoButtonLogic(&Button, Selected, &Row, BUTTONFLAG_LEFT);
		const bool Hot = Ui()->HotItem() == &Button;

		const ColorRGBA RowColor = Selected ? VoiceRowSelectedColor() :
						      (Hot ? VoiceRowHotColor() : VoiceRowBgColor());
		Row.Draw(RowColor, IGraphics::CORNER_ALL, 5.0f);

		CUIRect Inner = Row;
		Inner.Margin(4.0f, &Inner);
		const float FlagAspect = 2.0f; // countryflags textures are 2:1
		CUIRect FlagRect, ServerInfoRect;
		Inner.VSplitLeft(Inner.h * FlagAspect, &FlagRect, &ServerInfoRect);
		ServerInfoRect.VSplitLeft(6.0f, nullptr, &ServerInfoRect);
		CUIRect NameRect, PingRect;
		ServerInfoRect.VSplitRight(56.0f, &NameRect, &PingRect);
		PingRect.VSplitLeft(6.0f, nullptr, &PingRect);

		char aPing[32];
		if(Entry.m_PingMs >= 0)
			str_format(aPing, sizeof(aPing), "%d", Entry.m_PingMs);
		else
			str_copy(aPing, "--", sizeof(aPing));

		ColorRGBA PingColor = TextRender()->DefaultTextColor();
		if(Entry.m_PingMs >= 0)
		{
			const float PingRatio = std::clamp((Entry.m_PingMs - 20.0f) / 180.0f, 0.0f, 1.0f);
			PingColor = ColorRGBA(0.25f + PingRatio * 0.70f, 0.90f - PingRatio * 0.60f, 0.30f, 1.0f);
		}

		GameClient()->m_CountryFlags.Render(Entry.m_Flag, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), FlagRect.x, FlagRect.y, FlagRect.w, FlagRect.h);
		Ui()->DoLabel(&NameRect, Entry.m_Name.c_str(), 11.0f, TEXTALIGN_ML);
		TextRender()->TextColor(PingColor);
		Ui()->DoLabel(&PingRect, aPing, 11.0f, TEXTALIGN_MR);
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		if(Clicked)
		{
			m_SelectedServerIndex = (int)i;
			if(str_comp(g_Config.m_BcVoiceChatServerAddress, Entry.m_Address.c_str()) != 0)
			{
				str_copy(g_Config.m_BcVoiceChatServerAddress, Entry.m_Address.c_str(), sizeof(g_Config.m_BcVoiceChatServerAddress));
				StopVoice();
				m_RuntimeState = RUNTIME_RECONNECTING;
				StartVoice();
			}
		}
	}

	s_ServerListScrollRegion.AddRect(View);
	s_ServerListScrollRegion.End();
}

void CVoiceChat::RenderMembersSection(CUIRect View)
{
	CUIRect Header;
	View.HSplitTop(24.0f, &Header, &View);
	Ui()->DoLabel(&Header, Localize("Участники"), 15.0f, TEXTALIGN_ML);
	View.HSplitTop(6.0f, nullptr, &View);

	const bool HasLocalParticipant = m_Registered && LocalGameClientId() >= 0 && LocalGameClientId() < MAX_CLIENTS;
	if(m_vVisibleMemberPeerIds.empty() && !HasLocalParticipant)
	{
		Ui()->DoLabel(&View, Localize("Нет подключенных участников."), 12.0f, TEXTALIGN_ML);
		return;
	}

	static CScrollRegion s_MembersScrollRegion;
	static vec2 s_MembersScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = PANEL_ROW_HEIGHT;
	ScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	ScrollParams.m_ScrollbarMargin = 4.0f;
	s_MembersScrollRegion.Begin(&View, &s_MembersScrollOffset, &ScrollParams);
	View.y += s_MembersScrollOffset.y;

	auto RenderMemberRow = [&](const char *pName, const CTeeRenderInfo *pTeeInfo, const char *pInfoText, bool ShowSlider, uint16_t PeerId) {
		CUIRect Row;
		View.HSplitTop(PANEL_ROW_HEIGHT, &Row, &View);
		const bool RowVisible = s_MembersScrollRegion.AddRect(Row);
		CUIRect Spacing;
		View.HSplitTop(4.0f, &Spacing, &View);
		s_MembersScrollRegion.AddRect(Spacing);
		if(!RowVisible)
			return;

		Row.Draw(VoiceRowBgColor(), IGraphics::CORNER_ALL, 5.0f);

		CUIRect RowInner = Row;
		RowInner.Margin(6.0f, &RowInner);
		CUIRect Avatar, Main, Right;
		RowInner.VSplitLeft(34.0f, &Avatar, &Main);
		Main.VSplitRight(84.0f, &Main, &Right);
		Main.VSplitLeft(6.0f, nullptr, &Main);

		CUIRect NameRow, SliderRow;
		Main.HSplitTop(18.0f, &NameRow, &SliderRow);
		SliderRow.HSplitTop(2.0f, nullptr, &SliderRow);
		SliderRow.HSplitTop(16.0f, &SliderRow, nullptr);

		if(pTeeInfo && pTeeInfo->Valid())
		{
			CTeeRenderInfo TeeInfo = *pTeeInfo;
			TeeInfo.m_Size = Avatar.h;
			RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), Avatar.Center());
		}

		Ui()->DoLabel(&NameRow, pName, 11.0f, TEXTALIGN_ML);

		if(ShowSlider)
		{
			auto [VolumeIt, Inserted] = m_PeerVolumePercent.emplace(PeerId, 100);
			(void)Inserted;
			int &PeerVolume = VolumeIt->second;
			PeerVolume = std::clamp(PeerVolume, 0, 200);
			CButtonContainer &VolumeSlider = m_PeerVolumeSliderButtons[PeerId];
			const float CurrentRel = PeerVolume / 200.0f;
			const float NewRel = Ui()->DoScrollbarH(&VolumeSlider, &SliderRow, CurrentRel);
			PeerVolume = std::clamp(round_to_int(NewRel * 200.0f), 0, 200);
		}
		else
		{
			Ui()->DoLabel(&SliderRow, Localize("Local participant"), 10.0f, TEXTALIGN_ML);
		}

		Ui()->DoLabel(&Right, pInfoText, 10.0f, TEXTALIGN_MR);
	};

	if(HasLocalParticipant)
	{
		const int LocalClientId = LocalGameClientId();
		char aName[128];
		str_format(aName, sizeof(aName), "%s (you)", GameClient()->m_aClients[LocalClientId].m_aName);
		char aInfo[128];
		if(LocalTeam() == TEAM_SPECTATORS)
			str_format(aInfo, sizeof(aInfo), "%s %s", Localize("Team"), Localize("spec"));
		else
			str_format(aInfo, sizeof(aInfo), "%s %d", Localize("Team"), LocalTeam());
		RenderMemberRow(aName, &GameClient()->m_aClients[LocalClientId].m_RenderInfo, aInfo, false, 0);
	}

	for(uint16_t PeerId : m_vVisibleMemberPeerIds)
	{
		auto It = m_Peers.find(PeerId);
		if(It == m_Peers.end())
			continue;

		const CRemotePeer &Peer = It->second;
		const auto ItResolved = m_PeerResolvedClientIds.find(PeerId);
		const int MatchedClientId = ItResolved == m_PeerResolvedClientIds.end() ? -1 : ItResolved->second;
		const CTeeRenderInfo *pTeeInfo = nullptr;
		CTeeRenderInfo TeeInfo;
		if(MatchedClientId >= 0 && GameClient()->m_aClients[MatchedClientId].m_RenderInfo.Valid())
		{
			TeeInfo = GameClient()->m_aClients[MatchedClientId].m_RenderInfo;
			pTeeInfo = &TeeInfo;
		}

		char aPeerName[128];
		if(MatchedClientId >= 0)
		{
			str_copy(aPeerName, GameClient()->m_aClients[MatchedClientId].m_aName, sizeof(aPeerName));
		}
		else
		{
			str_format(aPeerName, sizeof(aPeerName), "%s #%u", Localize("Участник"), PeerId);
		}
		const int PeerVolume = m_PeerVolumePercent.find(PeerId) == m_PeerVolumePercent.end() ? 100 : std::clamp(m_PeerVolumePercent[PeerId], 0, 200);

		const int GainPercent = (int)(ComputePeerGain(Peer) * (PeerVolume / 100.0f) * 100.0f);
		char aInfo[128];
		str_format(aInfo, sizeof(aInfo), "Team %d  %d%%", (int)Peer.m_Team, maximum(0, GainPercent));
		RenderMemberRow(aPeerName, pTeeInfo, aInfo, true, PeerId);
	}

	s_MembersScrollRegion.AddRect(View);
	s_MembersScrollRegion.End();
}

void CVoiceChat::RenderSettingsSection(CUIRect View)
{
	CUIRect Header;
	View.HSplitTop(24.0f, &Header, &View);
	Ui()->DoLabel(&Header, Localize("Voice settings"), 15.0f, TEXTALIGN_ML);
	View.HSplitTop(8.0f, nullptr, &View);

	// Simplified settings: enable, mode, devices (+ server list below).
	{
		CUIRect OptionsCard;
		const float OptionsInnerHeight = 28.0f + 4.0f + 28.0f + 4.0f + 24.0f + 4.0f + 24.0f;
		const float OptionsHeight = OptionsInnerHeight + 20.0f;
		View.HSplitTop(OptionsHeight, &OptionsCard, &View);
		OptionsCard.Draw(VoiceCardBgColor(), IGraphics::CORNER_ALL, 6.0f);
		CUIRect Options = OptionsCard;
		Options.Margin(10.0f, &Options);

		auto AddSpacing = [&](float Height) {
			CUIRect Spacing;
			Options.HSplitTop(Height, &Spacing, &Options);
		};

		auto RenderDeviceDropDownRow = [&](CUIRect Row, const char *pLabel, int IsCapture, int &ConfigDeviceIndex, CUi::SDropDownState &DropDownState, CScrollRegion &DropDownScrollRegion) {
			CUIRect LabelRect, DropDownRect;
			Row.VSplitLeft(170.0f, &LabelRect, &DropDownRect);
			Ui()->DoLabel(&LabelRect, pLabel, 12.0f, TEXTALIGN_ML);
			DropDownRect.VSplitLeft(6.0f, nullptr, &DropDownRect);

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
			const int NewSelection = Ui()->DoDropDown(&DropDownRect, Selection, vpDeviceNames.data(), (int)vpDeviceNames.size(), DropDownState);
			if(NewSelection >= 0 && NewSelection < (int)vpDeviceNames.size() && NewSelection != Selection)
				ConfigDeviceIndex = NewSelection - 1;
		};

		static CScrollRegion s_InputDeviceDropDownScrollRegion;
		static CScrollRegion s_OutputDeviceDropDownScrollRegion;

		CUIRect Row;
		Options.HSplitTop(28.0f, &Row, &Options);
		if(GameClient()->m_Menus.DoButton_Menu(&m_EnableVoiceButton, g_Config.m_BcVoiceChatEnable ? Localize("Voice: On") : Localize("Voice: Off"), 0, &Row))
		{
			g_Config.m_BcVoiceChatEnable ^= 1;
			if(!g_Config.m_BcVoiceChatEnable && m_Socket)
				StopVoice();
		}

			AddSpacing(4.0f);
			Options.HSplitTop(28.0f, &Row, &Options);
			if(GameClient()->m_Menus.DoButton_Menu(&m_ActivationModeButton, g_Config.m_BcVoiceChatActivationMode == 1 ? Localize("Mode: Push-to-talk") : Localize("Mode: Automatic activation"), 0, &Row))
				g_Config.m_BcVoiceChatActivationMode = g_Config.m_BcVoiceChatActivationMode == 1 ? 0 : 1;

			AddSpacing(4.0f);
			Options.HSplitTop(24.0f, &Row, &Options);
			RenderDeviceDropDownRow(Row, Localize("Microphone"), 1, g_Config.m_BcVoiceChatInputDevice, m_InputDeviceDropDownState, s_InputDeviceDropDownScrollRegion);
		AddSpacing(4.0f);
		Options.HSplitTop(24.0f, &Row, &Options);
		RenderDeviceDropDownRow(Row, Localize("Headphones"), 0, g_Config.m_BcVoiceChatOutputDevice, m_OutputDeviceDropDownState, s_OutputDeviceDropDownScrollRegion);
	}

	View.HSplitTop(10.0f, nullptr, &View);
	RenderServersSection(View);
	return;

	CUIRect StatusCard;
	View.HSplitTop(102.0f, &StatusCard, &View);
	StatusCard.Draw(VoiceCardBgColor(), IGraphics::CORNER_ALL, 6.0f);
	CUIRect StatusInner = StatusCard;
	StatusInner.Margin(8.0f, &StatusInner);

	char aLine[192];
	str_format(aLine, sizeof(aLine), "%s: %s", Localize("Connection"), m_Registered ? Localize("Connected") : Localize("Connecting"));
	CUIRect Line;
	StatusInner.HSplitTop(20.0f, &Line, &StatusInner);
	Ui()->DoLabel(&Line, aLine, 11.0f, TEXTALIGN_ML);
		str_format(aLine, sizeof(aLine), "%s: %s", Localize("Mode"), g_Config.m_BcVoiceChatActivationMode == 1 ? Localize("Push-to-talk") : Localize("Automatic activation"));
		StatusInner.HSplitTop(20.0f, &Line, &StatusInner);
		Ui()->DoLabel(&Line, aLine, 11.0f, TEXTALIGN_ML);
	int ParticipantCount = (m_Registered && LocalGameClientId() >= 0 && LocalGameClientId() < MAX_CLIENTS ? 1 : 0) + (int)m_vVisibleMemberPeerIds.size();
	str_format(aLine, sizeof(aLine), "%s: %d", Localize("Участники"), ParticipantCount);
	StatusInner.HSplitTop(20.0f, &Line, &StatusInner);
	Ui()->DoLabel(&Line, aLine, 11.0f, TEXTALIGN_ML);
	str_format(aLine, sizeof(aLine), "%s: %s  |  %s: %s",
		Localize("Microphone"), g_Config.m_BcVoiceChatMicMuted ? Localize("Muted") : Localize("On"),
		Localize("Headphones"), g_Config.m_BcVoiceChatHeadphonesMuted ? Localize("Muted") : Localize("On"));
	StatusInner.HSplitTop(20.0f, &Line, &StatusInner);
	Ui()->DoLabel(&Line, aLine, 11.0f, TEXTALIGN_ML);

	View.HSplitTop(10.0f, nullptr, &View);

	static CScrollRegion s_SettingsScrollRegion;
	static vec2 s_SettingsScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 26.0f;
	ScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	ScrollParams.m_ScrollbarMargin = 4.0f;
	s_SettingsScrollRegion.Begin(&View, &s_SettingsScrollOffset, &ScrollParams);
	View.y += s_SettingsScrollOffset.y;

	auto AddSpacing = [&](float Height) {
		CUIRect Spacing;
		View.HSplitTop(Height, &Spacing, &View);
		s_SettingsScrollRegion.AddRect(Spacing);
	};

	auto AddRow = [&](float Height, CUIRect &Row) {
		View.HSplitTop(Height, &Row, &View);
		return s_SettingsScrollRegion.AddRect(Row);
	};

	CUIRect Button;
	if(AddRow(20.0f, Button))
		Ui()->DoScrollbarOption(&g_Config.m_BcVoiceChatVolume, &g_Config.m_BcVoiceChatVolume, &Button, Localize("Voice volume"), 0, 200, &CUi::ms_LogarithmicScrollbarScale, 0u, "%");

	AddSpacing(4.0f);
	if(AddRow(28.0f, Button) && GameClient()->m_Menus.DoButton_Menu(&m_ActivationModeButton, g_Config.m_BcVoiceChatActivationMode == 1 ? Localize("Mode: Push-to-talk") : Localize("Mode: Automatic activation"), 0, &Button))
		g_Config.m_BcVoiceChatActivationMode = g_Config.m_BcVoiceChatActivationMode == 1 ? 0 : 1;

	AddSpacing(4.0f);
	if(AddRow(28.0f, Button) && GameClient()->m_Menus.DoButton_Menu(&m_MicCheckButton, g_Config.m_BcVoiceChatMicCheck ? Localize("Mic check: On") : Localize("Mic check: Off"), 0, &Button))
		g_Config.m_BcVoiceChatMicCheck ^= 1;

	AddSpacing(5.0f);
	CUIRect MicLevelRow;
	if(AddRow(42.0f, MicLevelRow))
	{
		CUIRect MeterRow, VolumeRow;
		MicLevelRow.HSplitTop(16.0f, &MeterRow, &MicLevelRow);
		MicLevelRow.HSplitTop(6.0f, nullptr, &MicLevelRow);
		MicLevelRow.HSplitTop(16.0f, &VolumeRow, nullptr);

		CUIRect MicLevelLabel, MicLevelMeterWrap, MicLevelMeter;
		MeterRow.VSplitLeft(170.0f, &MicLevelLabel, &MicLevelMeterWrap);
		Ui()->DoLabel(&MicLevelLabel, Localize("Microphone level"), 12.0f, TEXTALIGN_ML);
		MicLevelMeterWrap.VSplitLeft(6.0f, nullptr, &MicLevelMeterWrap);
		MicLevelMeterWrap.HSplitTop(2.0f, nullptr, &MicLevelMeterWrap);
		MicLevelMeterWrap.HSplitTop(12.0f, &MicLevelMeter, nullptr);

		MicLevelMeter.Draw(ColorRGBA(0.02f, 0.02f, 0.03f, 0.28f), IGraphics::CORNER_ALL, 4.0f);
		CUIRect Fill = MicLevelMeter;
		Fill.w *= std::clamp(m_MicLevel, 0.0f, 1.0f);
		Fill.Draw(ColorRGBA(0.30f, 0.70f, 0.42f, 0.78f), IGraphics::CORNER_ALL, 4.0f);

		CUIRect MicVolumeLabel, MicVolumeControls;
		VolumeRow.VSplitLeft(170.0f, &MicVolumeLabel, &MicVolumeControls);
		Ui()->DoLabel(&MicVolumeLabel, Localize("Mic volume"), 12.0f, TEXTALIGN_ML);
		MicVolumeControls.VSplitLeft(6.0f, nullptr, &MicVolumeControls);
		CUIRect MicVolumeSlider, MicVolumeValue;
		MicVolumeControls.VSplitRight(110.0f, &MicVolumeSlider, &MicVolumeValue);
		MicVolumeSlider.VSplitRight(8.0f, &MicVolumeSlider, nullptr);
		MicVolumeValue.VSplitLeft(8.0f, nullptr, &MicVolumeValue);
		const float MicVolumeRel = std::clamp(g_Config.m_BcVoiceChatMicGain / 300.0f, 0.0f, 1.0f);
		const float NewMicVolumeRel = Ui()->DoScrollbarH(&g_Config.m_BcVoiceChatMicGain, &MicVolumeSlider, MicVolumeRel);
		g_Config.m_BcVoiceChatMicGain = std::clamp(round_to_int(NewMicVolumeRel * 300.0f), 0, 300);

		char aMicVolume[32];
		str_format(aMicVolume, sizeof(aMicVolume), "Mic volume: %d%%", g_Config.m_BcVoiceChatMicGain);
		Ui()->DoLabel(&MicVolumeValue, aMicVolume, 10.0f, TEXTALIGN_MR);
	}

	auto RenderDeviceDropDown = [&](const char *pLabel, int IsCapture, int &ConfigDeviceIndex, CUi::SDropDownState &DropDownState, CScrollRegion &DropDownScrollRegion) {
		AddSpacing(5.0f);
		CUIRect Row;
		if(!AddRow(24.0f, Row))
			return;

		CUIRect LabelRect, DropDownRect;
		Row.VSplitLeft(170.0f, &LabelRect, &DropDownRect);
		Ui()->DoLabel(&LabelRect, pLabel, 12.0f, TEXTALIGN_ML);
		DropDownRect.VSplitLeft(6.0f, nullptr, &DropDownRect);

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
			{
				vDeviceNames.emplace_back(pDeviceName);
			}
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
		const int NewSelection = Ui()->DoDropDown(&DropDownRect, Selection, vpDeviceNames.data(), (int)vpDeviceNames.size(), DropDownState);
		if(NewSelection >= 0 && NewSelection < (int)vpDeviceNames.size() && NewSelection != Selection)
			ConfigDeviceIndex = NewSelection - 1;
	};

	static CScrollRegion s_InputDeviceDropDownScrollRegion;
	static CScrollRegion s_OutputDeviceDropDownScrollRegion;
	RenderDeviceDropDown(Localize("Microphone"), 1, g_Config.m_BcVoiceChatInputDevice, m_InputDeviceDropDownState, s_InputDeviceDropDownScrollRegion);
	RenderDeviceDropDown(Localize("Headphones"), 0, g_Config.m_BcVoiceChatOutputDevice, m_OutputDeviceDropDownState, s_OutputDeviceDropDownScrollRegion);

	AddSpacing(4.0f);
	if(AddRow(28.0f, Button) && GameClient()->m_Menus.DoButton_Menu(&m_ReconnectButton, Localize("Reconnect"), 0, &Button))
	{
		StopVoice();
		m_RuntimeState = RUNTIME_RECONNECTING;
		StartVoice();
	}

	AddSpacing(8.0f);
	auto RenderBindRow = [&](const char *pLabel, const char *pCommand, CButtonContainer &Reader, CButtonContainer &Clear) {
		CUIRect BindRow;
		if(!AddRow(24.0f, BindRow))
			return;

		CBindSlot CurrentBind(KEY_UNKNOWN, KeyModifier::NONE);
		bool Found = false;
		for(int Mod = 0; Mod < KeyModifier::COMBINATION_COUNT && !Found; ++Mod)
		{
			for(int KeyId = 0; KeyId < KEY_LAST; ++KeyId)
			{
				const char *pBind = GameClient()->m_Binds.Get(KeyId, Mod);
				if(!pBind[0])
					continue;
				if(str_comp(pBind, pCommand) == 0)
				{
					CurrentBind = CBindSlot(KeyId, Mod);
					Found = true;
					break;
				}
			}
		}

		CUIRect LabelRect, BindRect;
		BindRow.VSplitLeft(170.0f, &LabelRect, &BindRect);
		Ui()->DoLabel(&LabelRect, pLabel, 12.0f, TEXTALIGN_ML);
		BindRect.VSplitLeft(6.0f, nullptr, &BindRect);

		const auto Result = GameClient()->m_KeyBinder.DoKeyReader(&Reader, &Clear, &BindRect, CurrentBind, false);
		if(Result.m_Bind != CurrentBind)
		{
			if(CurrentBind.m_Key != KEY_UNKNOWN)
				GameClient()->m_Binds.Bind(CurrentBind.m_Key, "", false, CurrentBind.m_ModifierMask);
			if(Result.m_Bind.m_Key != KEY_UNKNOWN)
				GameClient()->m_Binds.Bind(Result.m_Bind.m_Key, pCommand, false, Result.m_Bind.m_ModifierMask);
		}
		AddSpacing(4.0f);
	};

	RenderBindRow(Localize("PTT bind"), "+voicechat", m_PttBindReaderButton, m_PttBindClearButton);
	RenderBindRow(Localize("Mute microphone bind"), "toggle_voice_mic_mute", m_MicMuteBindReaderButton, m_MicMuteBindClearButton);
	RenderBindRow(Localize("Mute headphones bind"), "toggle_voice_headphones_mute", m_HeadphonesMuteBindReaderButton, m_HeadphonesMuteBindClearButton);

	s_SettingsScrollRegion.AddRect(View);
	s_SettingsScrollRegion.End();
}

void CVoiceChat::RenderPanel(const CUIRect &Screen, bool ShowCloseButton)
{
	const float PanelW = minimum(Screen.w * 0.88f, 1280.0f);
	const float PanelH = minimum(Screen.h * 0.86f, 820.0f);
	CUIRect Panel = {Screen.x + (Screen.w - PanelW) / 2.0f, Screen.y + (Screen.h - PanelH) / 2.0f, PanelW, PanelH};
	const ColorRGBA Bg = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcAdminPanelBgColor, true));
	Panel.Draw(Bg, IGraphics::CORNER_ALL, 8.0f);

	CUIRect Header, Body;
	Panel.HSplitTop(PANEL_HEADER_HEIGHT, &Header, &Body);

	CUIRect HeaderInner = Header;
	HeaderInner.Margin(8.0f, &HeaderInner);
	CUIRect Left, Right;
	HeaderInner.VSplitRight(PANEL_HEADER_HEIGHT - 4.0f, &Left, &Right);

	CUIRect HeaderIcon, HeaderTitle;
	Left.VSplitLeft(20.0f, &HeaderIcon, &HeaderTitle);
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	Ui()->DoLabel(&HeaderIcon, FontIcon::NETWORK_WIRED, 12.0f, TEXTALIGN_MC);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	Ui()->DoLabel(&HeaderTitle, Localize("Voice chat"), 13.0f, TEXTALIGN_ML);

	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	if(ShowCloseButton)
	{
		const bool Close = GameClient()->m_Menus.DoButton_Menu(&m_ClosePanelButton, FontIcon::XMARK, 0, &Right);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		if(Close)
			SetPanelActive(false);
	}
	else
	{
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	}

	Body.Margin(PANEL_PADDING, &Body);
	CUIRect Footer;
	Body.HSplitBottom(40.0f, &Body, &Footer);

	// Simplified panel: only settings (includes server list).
	{
		CUIRect Content = Body;
		Content.Draw(VoiceSectionBgColor(), IGraphics::CORNER_ALL, 6.0f);
		Content.Margin(10.0f, &Content);
		RenderSettingsSection(Content);

		CUIRect FooterInner = Footer;
		FooterInner.Margin(2.0f, &FooterInner);
		CUIRect ButtonsRow;
		FooterInner.VSplitLeft(64.0f, &ButtonsRow, nullptr);
		CUIRect MicButton;
		ButtonsRow.VSplitLeft(28.0f, &MicButton, &ButtonsRow);
		ButtonsRow.VSplitLeft(8.0f, nullptr, &ButtonsRow);
		CUIRect HeadphonesButton;
		ButtonsRow.VSplitLeft(28.0f, &HeadphonesButton, nullptr);

		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		if(GameClient()->m_Menus.DoButton_Menu(&m_MicMuteButton, FontIcon::MICROPHONE, 0, &MicButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, VoiceMuteButtonColor(g_Config.m_BcVoiceChatMicMuted != 0)))
			ToggleVoiceMicMute();
		if(GameClient()->m_Menus.DoButton_Menu(&m_HeadphonesMuteButton, FontIcon::HEADPHONES, 0, &HeadphonesButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, VoiceMuteButtonColor(g_Config.m_BcVoiceChatHeadphonesMuted != 0)))
			ToggleVoiceHeadphonesMute();

		// Show mute state as a cross overlay instead of darkening the button.
		TextRender()->TextColor(1.0f, 0.25f, 0.25f, 1.0f);
		if(g_Config.m_BcVoiceChatMicMuted)
			Ui()->DoLabel(&MicButton, FontIcon::XMARK, 8.0f, TEXTALIGN_MC);
		if(g_Config.m_BcVoiceChatHeadphonesMuted)
			Ui()->DoLabel(&HeadphonesButton, FontIcon::XMARK, 8.0f, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	}
	return;

	CUIRect Rail;
	Body.VSplitLeft(48.0f, &Rail, &Body);
	Rail.Draw(VoiceSectionBgColor(), IGraphics::CORNER_ALL, 6.0f);

	CUIRect RailInner = Rail;
	RailInner.Margin(6.0f, &RailInner);
	CUIRect RailButton;

	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	RailInner.HSplitTop(PANEL_SECTION_BUTTON_SIZE, &RailButton, &RailInner);
	if(GameClient()->m_Menus.DoButton_Menu(&m_SectionRoomButton, FontIcon::NETWORK_WIRED, 0, &RailButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, VoiceIconButtonColor(m_ActiveSection == VOICE_SECTION_SERVERS)))
		m_ActiveSection = VOICE_SECTION_SERVERS;
	RailInner.HSplitTop(6.0f, nullptr, &RailInner);
	RailInner.HSplitTop(PANEL_SECTION_BUTTON_SIZE, &RailButton, &RailInner);
	if(GameClient()->m_Menus.DoButton_Menu(&m_SectionMembersButton, FontIcon::ICON_USERS, 0, &RailButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, VoiceIconButtonColor(m_ActiveSection == VOICE_SECTION_MEMBERS)))
		m_ActiveSection = VOICE_SECTION_MEMBERS;
	RailInner.HSplitTop(6.0f, nullptr, &RailInner);
	RailInner.HSplitTop(PANEL_SECTION_BUTTON_SIZE, &RailButton, &RailInner);
	if(GameClient()->m_Menus.DoButton_Menu(&m_SectionSettingsButton, FontIcon::GEAR, 0, &RailButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, VoiceIconButtonColor(m_ActiveSection == VOICE_SECTION_SETTINGS)))
		m_ActiveSection = VOICE_SECTION_SETTINGS;
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

	Body.VSplitLeft(10.0f, nullptr, &Body);
	Body.Draw(VoiceSectionBgColor(), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(10.0f, &Body);

	if(m_ActiveSection == VOICE_SECTION_MEMBERS)
		RenderMembersSection(Body);
	else if(m_ActiveSection == VOICE_SECTION_SETTINGS)
		RenderSettingsSection(Body);
	else
		RenderServersSection(Body);

	CUIRect FooterInner = Footer;
	FooterInner.Margin(2.0f, &FooterInner);
	CUIRect ButtonsRow;
	FooterInner.VSplitLeft(64.0f, &ButtonsRow, nullptr);
	CUIRect MicButton;
	ButtonsRow.VSplitLeft(28.0f, &MicButton, &ButtonsRow);
	ButtonsRow.VSplitLeft(8.0f, nullptr, &ButtonsRow);
	CUIRect HeadphonesButton;
	ButtonsRow.VSplitLeft(28.0f, &HeadphonesButton, nullptr);

	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	if(GameClient()->m_Menus.DoButton_Menu(&m_MicMuteButton, FontIcon::MICROPHONE, 0, &MicButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, VoiceMuteButtonColor(g_Config.m_BcVoiceChatMicMuted != 0)))
		ToggleVoiceMicMute();
	if(GameClient()->m_Menus.DoButton_Menu(&m_HeadphonesMuteButton, FontIcon::HEADPHONES, 0, &HeadphonesButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, VoiceMuteButtonColor(g_Config.m_BcVoiceChatHeadphonesMuted != 0)))
		ToggleVoiceHeadphonesMute();

	// Show mute state as a cross overlay instead of darkening the button.
	TextRender()->TextColor(1.0f, 0.25f, 0.25f, 1.0f);
	if(g_Config.m_BcVoiceChatMicMuted)
		Ui()->DoLabel(&MicButton, FontIcon::XMARK, 8.0f, TEXTALIGN_MC);
	if(g_Config.m_BcVoiceChatHeadphonesMuted)
		Ui()->DoLabel(&HeadphonesButton, FontIcon::XMARK, 8.0f, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
}

void CVoiceChat::FetchServerList()
{
	if(m_pServerListTask && !m_pServerListTask->Done())
		return;

	m_pServerListTask = HttpGet(VOICE_MASTER_LIST_URL);
	m_pServerListTask->Timeout(CTimeout{10000, 0, 500, 5});
	m_pServerListTask->IpResolve(IPRESOLVE::V4);
	m_pServerListTask->VerifyPeer(false); // allow self-signed/local TLS endpoint
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
			if(BestClientVoice::ParseAddress(Entry.m_Address.c_str(), BestClientVoice::DEFAULT_PORT, Entry.m_Addr))
				Entry.m_HasAddr = true;

			m_vServerEntries.push_back(Entry);
		}
	}

	json_value_free(pJson);

	if(!m_vServerEntries.empty())
	{
		m_ServerRowButtons.resize(m_vServerEntries.size());
		m_SelectedServerIndex = 0;
		for(size_t i = 0; i < m_vServerEntries.size(); ++i)
		{
			if(str_comp(m_vServerEntries[i].m_Address.c_str(), g_Config.m_BcVoiceChatServerAddress) == 0)
			{
				m_SelectedServerIndex = (int)i;
				break;
			}
		}
		StartServerListPings();
	}
	else
	{
		m_SelectedServerIndex = -1;
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
	str_copy(pSelf->m_aLastServerAddr, g_Config.m_BcVoiceChatServerAddress, sizeof(pSelf->m_aLastServerAddr));
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
	dbg_msg("voice", "enabled=%d connected=%d participants=%d server='%s' ptt=%d",
		1, pSelf->m_Registered ? 1 : 0, (int)pSelf->m_vVisibleMemberPeerIds.size(), g_Config.m_BcVoiceChatServerAddress, pSelf->m_PushToTalkPressed ? 1 : 0);
}

void CVoiceChat::ConToggleVoicePanel(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	CVoiceChat *pSelf = static_cast<CVoiceChat *>(pUserData);
	pSelf->SetPanelActive(!pSelf->m_PanelActive);
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
