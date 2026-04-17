/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_BESTCLIENT_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_BESTCLIENT_H

#include <engine/shared/console.h>
#include <engine/shared/http.h>

#include <game/client/component.h>

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

class CBestClient : public CComponent
{
	class SHookComboPopup
	{
	public:
		int m_Sequence = 1;
		float m_Age = 0.0f;
	};

	std::array<int, 7> m_aHookComboSoundIds{};
	std::vector<SHookComboPopup> m_vHookComboPopups;
	int m_HookComboCounter = 0;
	float m_HookComboLastHookTime = -1.0f;
	int m_HookComboTrackedClientId = -1;
	int m_HookComboLastHookedPlayer = -1;
	bool m_HookComboSoundErrorShown = false;

	void LoadHookComboSounds(bool LogErrors = true);
	void UnloadHookComboSounds();
	void ResetHookComboState();
	void UpdateHookCombo();
	void TriggerHookComboStep();
	bool HasHookComboWork() const;

	static void ConToggle45Degrees(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleSmallSens(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleDeepfly(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleCinematicCamera(IConsole::IResult *pResult, void *pUserData);

	int m_45degreestoggle = 0;
	int m_45degreestogglelastinput = 0;
	int m_45degreesEnabled = 0;
	int m_Smallsenstoggle = 0;
	int m_Smallsenstogglelastinput = 0;
	int m_SmallsensEnabled = 0;
	char m_Oldmouse1Bind[128] = {};
	bool m_StreamerWordsLoaded = false;
	std::vector<std::string> m_vStreamerBlockedWords;

	void FinishBestClientInfo();
	void ResetBestClientInfoTask();
	void EnsureStreamerWordsLoaded();
	void SaveStreamerWords() const;

public:
	enum EStreamerFlags
	{
		STREAMER_HIDE_SERVER_IP = 1 << 0,
		STREAMER_HIDE_CHAT = 1 << 1,
		STREAMER_HIDE_FRIEND_WHISPER = 1 << 2,
		STREAMER_HIDE_OWN_NAME = 1 << 3,
		STREAMER_HIDE_OTHER_NAMES = 1 << 4,
		STREAMER_HIDE_TAB_NAMES = 1 << 5,
		STREAMER_HIDE_LOGIN = 1 << 6,
	};

	enum EBestClientComponent
	{
		COMPONENT_VISUALS_MUSIC_PLAYER = 0,
		COMPONENT_VISUALS_LEGACY_RESERVED_1,
		COMPONENT_VISUALS_CRYSTAL_LASER,
		COMPONENT_VISUALS_MEDIA_BACKGROUND,
		COMPONENT_VISUALS_MAGIC_PARTICLES,
		COMPONENT_VISUALS_ORBIT_AURA,
		COMPONENT_VISUALS_OPTIMIZER,
		COMPONENT_VISUALS_ANIMATIONS,
		COMPONENT_VISUALS_CAMERA_DRIFT,
		COMPONENT_VISUALS_DYNAMIC_FOV,
		COMPONENT_VISUALS_AFTERIMAGE,
		COMPONENT_VISUALS_FOCUS_MODE,
		COMPONENT_VISUALS_CHAT_BUBBLES,
		COMPONENT_VISUALS_3D_PARTICLES,
		COMPONENT_VISUALS_ASPECT_RATIO,
		COMPONENT_GAMEPLAY_INPUT,
		COMPONENT_GAMEPLAY_FAST_ACTIONS,
		COMPONENT_GAMEPLAY_SPEEDRUN_TIMER,
		COMPONENT_GAMEPLAY_FINISH_PREDICTION,
		COMPONENT_GAMEPLAY_AUTO_TEAM_LOCK,
		COMPONENT_GAMEPLAY_GORES_MODE,
		COMPONENT_GAMEPLAY_HOOK_COMBO,
		COMPONENT_OTHERS_CLIENT_INDICATOR,
		COMPONENT_TCLIENT_SETTINGS_TAB,
		COMPONENT_TCLIENT_BIND_WHEEL_TAB,
		COMPONENT_TCLIENT_WAR_LIST_TAB,
		COMPONENT_TCLIENT_CHAT_BINDS_TAB,
		COMPONENT_TCLIENT_STATUS_BAR_TAB,
		COMPONENT_TCLIENT_INFO_TAB,
		COMPONENT_TCLIENT_PROFILES_PAGE,
		COMPONENT_TCLIENT_CONFIGS_PAGE,
		COMPONENT_TCLIENT_SETTINGS_VISUAL,
		COMPONENT_TCLIENT_SETTINGS_ANTI_LATENCY,
		COMPONENT_TCLIENT_SETTINGS_ANTI_PING_SMOOTHING,
		COMPONENT_TCLIENT_SETTINGS_AUTO_EXECUTE,
		COMPONENT_TCLIENT_SETTINGS_VOTING,
		COMPONENT_TCLIENT_SETTINGS_AUTO_REPLY,
		COMPONENT_TCLIENT_SETTINGS_PLAYER_INDICATOR,
		COMPONENT_TCLIENT_SETTINGS_PET,
		COMPONENT_TCLIENT_SETTINGS_HUD,
		COMPONENT_TCLIENT_SETTINGS_FROZEN_TEE_DISPLAY,
		COMPONENT_TCLIENT_SETTINGS_TILE_OUTLINES,
		COMPONENT_TCLIENT_SETTINGS_GHOST_TOOLS,
		COMPONENT_TCLIENT_SETTINGS_RAINBOW,
		COMPONENT_TCLIENT_SETTINGS_TEE_TRAILS,
		COMPONENT_TCLIENT_SETTINGS_BACKGROUND_DRAW,
		COMPONENT_TCLIENT_SETTINGS_FINISH_NAME,
		COMPONENT_OTHERS_MISC,
		COMPONENT_OTHERS_CHAT_MEDIA,
		COMPONENT_OTHERS_VOICE_SETTINGS,
		COMPONENT_OTHERS_VOICE_BINDS,
		COMPONENT_OTHERS_STREAMER,
		COMPONENT_VISUALS_JELLY_TEE,
		COMPONENT_VISUALS_PLAYER_TRAIL,
		NUM_COMPONENTS_EDITOR_COMPONENTS,
	};

	CBestClient();
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;
	void OnReset() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnRender() override;
	void OnConsoleInit() override;
	bool IsStreamerModeEnabled() const;
	bool HasStreamerFlag(int Flag) const;
	const char *MaskServerAddress(const char *pAddress, char *pOutput, size_t OutputSize) const;
	bool IsLocalClientId(int ClientId) const;
	bool ShouldHidePlayerName(int ClientId, bool InScoreboard) const;
	void AddStreamerBlockedWord(const char *pWord);
	void RemoveStreamerBlockedWord(int Index);
	const std::vector<std::string> &StreamerBlockedWords();
	int StreamerBlockedWordCount();
	bool SanitizeSensitiveCommand(const char *pInput, char *pOutput, size_t OutputSize) const;
	void SanitizeText(const char *pInput, char *pOutput, size_t OutputSize);
	void SanitizePlayerName(const char *pInput, char *pOutput, size_t OutputSize, int ClientId, bool InScoreboard = false);
	bool IsComponentDisabled(EBestClientComponent Component) const;
	static bool IsComponentDisabledByMask(int Component, int MaskLo, int MaskHi);
	void RenderHookCombo(bool ForcePreview = false);

	std::shared_ptr<CHttpRequest> m_pBestClientInfoTask = nullptr;
	void FetchBestClientInfo();
	bool NeedUpdate();
	bool m_FetchedBestClientInfo = false;
	char m_aVersionStr[64] = "0";
};

#endif
