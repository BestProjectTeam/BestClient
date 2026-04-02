/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_VOICE_VOICE_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_VOICE_VOICE_H

#include "protocol.h"
#include "../subsystem_runtime.h"

#include <game/client/component.h>
#include <game/client/ui.h>

#include <base/net.h>
#include <base/system.h>

#include <engine/console.h>

#include <SDL_audio.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct OpusEncoder;
struct OpusDecoder;
class CHttpRequest;

class CVoiceChat : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnReset() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnUpdate() override;
	void OnRelease() override;
	void OnRender() override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	bool OnInput(const IInput::CEvent &Event) override;
	void OnShutdown() override;
	bool IsClientTalking(int ClientId) const;
	std::optional<int> GetClientVolumePercent(int ClientId) const;
	void SetClientVolumePercent(int ClientId, int VolumePercent);
	void RenderHudTalkingIndicator(float HudWidth, float HudHeight, bool ForcePreview = false);
	void RenderHudMuteStatusIndicator(float HudWidth, float HudHeight, bool ForcePreview = false);
	CUIRect GetHudTalkingIndicatorRect(float HudWidth, float HudHeight, bool ForcePreview = false) const;
	CUIRect GetHudMuteStatusIndicatorRect(float HudWidth, float HudHeight, bool ForcePreview = false) const;
	// Renders the voice panel inside menus/settings (independent from the in-game toggle state).
	void RenderMenuPanel(const CUIRect &View);
	// Renders only the voice settings block (no popup frame).
	void RenderMenuSettingsBlock(const CUIRect &View, float RevealPhase = 1.0f);
	// Returns dynamic height for the voice settings block in menus.
	float GetMenuSettingsBlockHeight(float RevealPhase = 1.0f) const;
	// Renders binds for voice controls used in settings pages.
	void RenderMenuControlBinds(const CUIRect &View);
	// Renders a bind row for toggling the voice panel (used by the settings menu).
	void RenderMenuPanelToggleBind(const CUIRect &View);
	// Handles chat commands starting with "!voice". Returns true if consumed locally (not sent to server).
	bool TryHandleChatCommand(const char *pLine);

private:
	template<size_t Capacity>
	class CFixedSampleRingBuffer
	{
	public:
		size_t PushBack(const int16_t *pData, size_t Count)
		{
			if(Count == 0 || pData == nullptr || Capacity == 0)
				return 0;

			if(Count >= Capacity)
			{
				pData += Count - Capacity;
				Count = Capacity;
				Clear();
			}

			const size_t Overflow = m_Size + Count > Capacity ? m_Size + Count - Capacity : 0;
			if(Overflow > 0)
				DiscardFront(Overflow);

			size_t Tail = (m_Head + m_Size) % Capacity;
			size_t Remaining = Count;
			size_t Offset = 0;
			while(Remaining > 0)
			{
				const size_t Chunk = minimum(Remaining, Capacity - Tail);
				for(size_t i = 0; i < Chunk; ++i)
					m_aData[Tail + i] = pData[Offset + i];
				Tail = (Tail + Chunk) % Capacity;
				Offset += Chunk;
				Remaining -= Chunk;
			}
			m_Size += Count;
			return Overflow;
		}

		size_t PopFront(int16_t *pDst, size_t Count)
		{
			if(Count == 0 || pDst == nullptr)
				return 0;
			const size_t Actual = minimum(Count, m_Size);
			for(size_t i = 0; i < Actual; ++i)
				pDst[i] = m_aData[(m_Head + i) % Capacity];
			DiscardFront(Actual);
			return Actual;
		}

		size_t DiscardFront(size_t Count)
		{
			const size_t Actual = minimum(Count, m_Size);
			m_Head = (m_Head + Actual) % Capacity;
			m_Size -= Actual;
			return Actual;
		}

		size_t Size() const { return m_Size; }
		constexpr size_t CapacityValue() const { return Capacity; }
		void Clear()
		{
			m_Head = 0;
			m_Size = 0;
		}

	private:
		std::array<int16_t, Capacity> m_aData = {};
		size_t m_Head = 0;
		size_t m_Size = 0;
	};

	enum ERuntimeState
	{
		RUNTIME_STOPPED = 0,
		RUNTIME_STARTING,
		RUNTIME_REGISTERED,
		RUNTIME_STALE,
		RUNTIME_RECONNECTING,
	};

	struct STalkingEntry
	{
		int m_ClientId = -1;
		uint16_t m_PeerId = 0;
		bool m_IsLocal = false;
	};

	struct CRemotePeer
	{
		OpusDecoder *m_pDecoder = nullptr;
		CFixedSampleRingBuffer<BestClientVoice::FRAME_SIZE * 8> m_DecodedPcm;
		int64_t m_LastReceiveTick = 0;
		int64_t m_LastArrivalTick = 0;
		uint16_t m_LastSequence = 0;
		bool m_HasSequence = false;
		vec2 m_Position = vec2(0.0f, 0.0f);
		int m_Team = 0;
		int64_t m_LastVoiceTick = 0;
		int m_AnnouncedGameClientId = BestClientVoice::INVALID_GAME_CLIENT_ID;
		float m_JitterMs = 0.0f;
		float m_LossEwma = 0.0f;
		int m_ConsecutiveDecodeFails = 0;
	};
	struct CVoiceServerEntry
	{
		std::string m_Name;
		std::string m_Address;
		int m_Flag = 0;
		NETADDR m_Addr = NETADDR_ZEROED;
		bool m_HasAddr = false;
		int m_PingMs = -1;
		uint16_t m_PingToken = 0;
		int64_t m_LastPingSendTick = 0;
		bool m_PingInFlight = false;
	};

	NETSOCKET m_Socket = nullptr;
	NETSOCKET m_SecondarySocket = nullptr;
	NETSOCKET m_ServerListPingSocket = nullptr;
	NETADDR m_ServerAddr = NETADDR_ZEROED;
	bool m_HasServerAddr = false;
	bool m_Registered = false;
	bool m_SecondaryRegistered = false;
	uint16_t m_ClientVoiceId = 0;
	uint16_t m_SecondaryClientVoiceId = 0;
	int64_t m_LastHelloTick = 0;
	int64_t m_SecondaryLastHelloTick = 0;
	int64_t m_LastServerPacketTick = 0;
	int64_t m_SecondaryLastServerPacketTick = 0;
	int64_t m_LastHeartbeatTick = 0;
	int64_t m_SecondaryLastHeartbeatTick = 0;
	uint16_t m_SendSequence = 0;
	uint16_t m_SecondarySendSequence = 0;
	int m_LastBitrate = -1;
	bool m_HelloResetPending = false;
	bool m_SecondaryHelloResetPending = false;
	ERuntimeState m_RuntimeState = RUNTIME_STOPPED;
	ESubsystemRuntimeState m_SubsystemState = ESubsystemRuntimeState::DISABLED;

	SDL_AudioDeviceID m_CaptureDevice = 0;
	SDL_AudioDeviceID m_PlaybackDevice = 0;
	SDL_AudioSpec m_CaptureSpec = {};
	SDL_AudioSpec m_PlaybackSpec = {};
	OpusEncoder *m_pEncoder = nullptr;

	CFixedSampleRingBuffer<BestClientVoice::SAMPLE_RATE * 2> m_CapturePcm;
	CFixedSampleRingBuffer<BestClientVoice::SAMPLE_RATE * 2> m_MicMonitorPcm;
	std::unordered_map<uint16_t, CRemotePeer> m_Peers;
	std::unordered_map<uint16_t, int> m_PeerVolumePercent;
	std::unordered_map<uint16_t, CButtonContainer> m_PeerVolumeSliderButtons;
	std::unordered_set<std::string> m_MutedNameKeys;
	std::unordered_map<std::string, int> m_NameVolumePercent;
	char m_aLastMutedNames[512] = {};
	char m_aLastPersistedMutedNames[512] = {};
	char m_aLastNameVolumes[512] = {};
	bool m_MutedNamesLoadedFromFile = false;
	bool m_PushToTalkPressed = false;
	int64_t m_AutoActivationUntilTick = 0;
	float m_MicLevel = 0.0f;
	float m_MicLimiterGain = 1.0f;
	float m_AutoNsNoiseFloor = 0.0f;
	float m_AutoNsGate = 1.0f;
	float m_AutoHpfPrevIn = 0.0f;
	float m_AutoHpfPrevOut = 0.0f;
	float m_AutoCompEnv = 0.0f;
	float m_VadNoiseFloor = 0.0f;
	float m_VadSpeechScore = 0.0f;
	float m_VadLastActivationLevel = 0.0f;
	bool m_WasTransmitActive = false;
	bool m_WasEnabled = false;
	char m_aLastServerAddr[128] = "";
	int64_t m_LastStartAttempt = 0;
	int64_t m_LastEncoderTuneTick = 0;
	int m_LastEncoderLossPerc = -1;
	int m_LastEncoderFec = -1;
	bool m_PlaybackQueueErrorLogged = false;
	int64_t m_LastServerListPingSweepTick = 0;
	int64_t m_LastServerListAutoFetchTick = 0;
	int64_t m_LastActiveTick = 0;
	int64_t m_LastProcessNetworkTick = 0;
	int64_t m_LastProcessCaptureTick = 0;
	int64_t m_LastProcessPlaybackTick = 0;
	int64_t m_LastPerfReportTick = 0;
	int64_t m_LastUpdateCostTick = 0;
	int64_t m_MaxUpdateCostTick = 0;
	int64_t m_TotalUpdateCostTick = 0;
	int64_t m_UpdateSamples = 0;
	int m_LastInputDevice = -2;
	int m_LastOutputDevice = -2;
	bool m_PanelActive = false;
	bool m_MouseUnlocked = false;
	std::optional<vec2> m_LastMousePos;
	int m_ActiveSection = 0;
	std::vector<std::string> m_vOnlineServers;
	std::vector<CVoiceServerEntry> m_vServerEntries;
	std::vector<CButtonContainer> m_ServerRowButtons;
	std::shared_ptr<CHttpRequest> m_pServerListTask = nullptr;
	int m_SelectedServerIndex = -1;
	std::string m_AdvertisedRoomKey;
	int m_AdvertisedGameClientId = BestClientVoice::INVALID_GAME_CLIENT_ID - 1;
	int m_AdvertisedTeam = std::numeric_limits<int>::min();
	std::string m_SecondaryAdvertisedRoomKey;
	int m_SecondaryAdvertisedGameClientId = BestClientVoice::INVALID_GAME_CLIENT_ID - 1;
	int m_SecondaryAdvertisedTeam = std::numeric_limits<int>::min();
	float m_EnableYourGroupRevealPhase = 0.0f;
	bool m_LastUseTeam0Mode = false;
	bool m_LastEnableYourGroup = false;

	std::vector<uint16_t> m_vSortedPeerIds;
	std::vector<uint16_t> m_vVisibleMemberPeerIds;
	std::vector<STalkingEntry> m_vTalkingEntries;
	std::unordered_map<uint16_t, int> m_PeerResolvedClientIds;
	bool m_PeerListDirty = true;
	bool m_SnapMappingDirty = true;
	bool m_TalkingStateDirty = true;
	CUi::SDropDownState m_InputDeviceDropDownState;
	CUi::SDropDownState m_OutputDeviceDropDownState;

	CButtonContainer m_SectionRoomButton;
	CButtonContainer m_SectionMembersButton;
	CButtonContainer m_SectionSettingsButton;
	CButtonContainer m_ClosePanelButton;
	CButtonContainer m_MicMuteButton;
	CButtonContainer m_HeadphonesMuteButton;
	CButtonContainer m_MicCheckButton;
	CButtonContainer m_EnableVoiceButton;
	CButtonContainer m_InGameOnlyButton;
	CButtonContainer m_UseTeam0Button;
	CButtonContainer m_EnableYourGroupButton;
	CButtonContainer m_ActivationModeButton;
	CButtonContainer m_RadiusFilterButton;
	CButtonContainer m_ReloadServerListButton;
	CButtonContainer m_ReconnectButton;
	CButtonContainer m_DisconnectButton;
	CButtonContainer m_PttBindReaderButton;
	CButtonContainer m_PttBindClearButton;
	CButtonContainer m_PanelBindReaderButton;
	CButtonContainer m_PanelBindClearButton;
	CButtonContainer m_MicMuteBindReaderButton;
	CButtonContainer m_MicMuteBindClearButton;
	CButtonContainer m_HeadphonesMuteBindReaderButton;
	CButtonContainer m_HeadphonesMuteBindClearButton;

	void StartVoice();
	void StopVoice();
	bool ShouldStartVoicePipeline(bool Online) const;
	bool ShouldKeepVoicePipelineActive() const;
	bool ShouldUseSecondaryTeamConnection() const;
	bool HasPendingPlaybackAudio() const;
	bool HasRecentVoiceActivity(int64_t Now) const;
	bool OpenNetworking();
	bool OpenSecondaryNetworking();
	void CloseNetworking();
	void CloseSecondaryNetworking();
	bool OpenAudioDevices();
	void CloseAudioDevices();
	bool CreateEncoder();
	void DestroyEncoder();
	void TuneEncoderForNetwork();
	void ClearPeerState();
	void SendHello();
	void SendHelloSecondary();
	void SendGoodbye();
	void SendGoodbyeSecondary();
	void SendVoiceFrame(const uint8_t *pOpusData, int OpusSize, int Team, vec2 Position);
	void SendVoiceFrameSecondary(const uint8_t *pOpusData, int OpusSize, int Team, vec2 Position);
	void ProcessNetwork();
	void ProcessSecondaryNetwork();
	void ProcessVoiceRelayPacket(const uint8_t *pRawData, int DataSize, int Offset, uint16_t SelfVoiceId);
	void ProcessServerListPing();
	void ProcessCapture();
	void ProcessPlayback();
	void CleanupPeers();
	bool ShouldTransmit() const;
	bool IsInGameOnlyBlocked() const;
	int LocalTeam() const;
	int LocalVoiceTeam() const;
	int LocalOwnVoiceTeam() const;
	bool IsUseTeam0Mode() const;
	bool IsEnableYourGroupMode() const;
	bool IsVoiceTeamAudible(int Team) const;
	vec2 LocalPosition() const;
	std::string CurrentRoomKey() const;
	int LocalGameClientId() const;
	void BeginReconnect();
	void InvalidatePeerCaches(bool MappingDirty = true, bool TalkingDirty = true);
	void RefreshPeerCaches();
	void RefreshPeerMappingCache();
	void RefreshTalkingCache();
	int ResolvePeerClientId(const CRemotePeer &Peer) const;
	bool ShouldShowPeerInMembers(const CRemotePeer &Peer) const;
	float ComputePeerGain(const CRemotePeer &Peer) const;
	bool IsRadiusFilterEnabled() const;
	float RadiusFilterDistanceUnits() const;
	bool IsPositionWithinRadiusFilter(vec2 Position) const;
	void SetPanelActive(bool Active);
	void SetUiMousePos(vec2 Pos);
	void RenderPanel(const CUIRect &Screen, bool ShowCloseButton);
	void RenderServersSection(CUIRect View);
	void RenderMembersSection(CUIRect View);
	void RenderSettingsSection(CUIRect View);
	std::vector<uint16_t> SortedPeerIds() const;
	void FetchServerList();
	void FinishServerList();
	void ResetServerListTask();
	void StartServerListPings();
	void CloseServerListPingSocket();
	void LoadMutedNamesFromFile();
	void SaveMutedNamesToFile();

	static void ConVoiceConnect(IConsole::IResult *pResult, void *pUserData);
	static void ConVoiceDisconnect(IConsole::IResult *pResult, void *pUserData);
	static void ConVoiceStatus(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleVoicePanel(IConsole::IResult *pResult, void *pUserData);
	static void ConKeyVoiceTalk(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleVoiceMicMute(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleVoiceHeadphonesMute(IConsole::IResult *pResult, void *pUserData);
};

#endif
