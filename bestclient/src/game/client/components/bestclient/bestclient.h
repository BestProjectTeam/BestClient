#ifndef GAME_CLIENT_COMPONENTS_BestClient_BestClient_H
#define GAME_CLIENT_COMPONENTS_BestClient_BestClient_H

#include <engine/client/enums.h>
#include <engine/external/regex.h>
#include <engine/shared/console.h>
#include <engine/shared/http.h>

#include <game/client/component.h>

#include <array>
#include <deque>
#include <vector>

class CBestClient : public CComponent
{
	std::deque<vec2> m_aAirRescuePositions[NUM_DUMMIES];
	void AirRescue();
	static void ConAirRescue(IConsole::IResult *pResult, void *pUserData);

	static void ConToggle45Degrees(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleSmallSens(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleDeepfly(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleCinematicCamera(IConsole::IResult *pResult, void *pUserData);

	static void ConCalc(IConsole::IResult *pResult, void *pUserData);
	static void ConRandomTee(IConsole::IResult *pResult, void *pUserData);
	static void ConchainRandomColor(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void RandomBodyColor();
	static void RandomFeetColor();
	static void RandomSkin(void *pUserData);
	static void RandomFlag(void *pUserData);

	static void ConSpecId(IConsole::IResult *pResult, void *pUserData);
	void SpecId(int ClientId);

	int m_EmoteCycle = 0;
	static void ConEmoteCycle(IConsole::IResult *pResult, void *pUserData);

	class IEngineGraphics *m_pGraphics = nullptr;

	char m_PreviousOwnMessage[2048] = {};

	bool SendNonDuplicateMessage(int Team, const char *pLine);

	void OptimizerUpdateProcessPriorities();

	unsigned long m_OptimizerDdnetPrevPriorityClass = 0;
	bool m_OptimizerDdnetPriorityHighActive = false;
	bool m_OptimizerDiscordPriorityBelowNormalActive = false;
	float m_OptimizerDiscordPriorityLastUpdateTime = -1.0f;

	void UpdateAutoTeamLock();
	void UpdateAutoDummyJoin();
	void SendChatCommandToConn(int Conn, const char *pLine);
	int m_aAutoTeamLockLastTeam[NUM_DUMMIES] = {};
	int64_t m_aAutoTeamLockDeadlineTick[NUM_DUMMIES] = {};
	bool m_aAutoTeamLockPending[NUM_DUMMIES] = {};
	int m_aAutoDummyJoinLastTeam[NUM_DUMMIES] = {};
	int m_aAutoDummyJoinPendingTeam[NUM_DUMMIES] = {};
	int64_t m_aAutoDummyJoinRetryTick[NUM_DUMMIES] = {};

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
	bool HasRenderWork() const;
	void RenderOptimizerFpsFogRect();

	//45 degrees
	int m_45degreestoggle = 0;
	int m_45degreestogglelastinput = 0;
	int m_45degreesEnabled = 0;
	// Small sens
	int m_Smallsenstoggle = 0;
	int m_Smallsenstogglelastinput = 0;
	int m_SmallsensEnabled = 0;
	//Deepfly
	char m_Oldmouse1Bind[128];

	bool ServerCommandExists(const char *pCommand);

public:
	enum EBestClientComponent
	{
		COMPONENT_VISUALS_MUSIC_PLAYER = 0,
		COMPONENT_VISUALS_CAVA,
		COMPONENT_VISUALS_SWEAT_WEAPON,
		COMPONENT_VISUALS_MEDIA_BACKGROUND,
		COMPONENT_VISUALS_PET,
		COMPONENT_VISUALS_MAGIC_PARTICLES,
		COMPONENT_VISUALS_ORBIT_AURA,
		COMPONENT_VISUALS_OPTIMIZER,
		COMPONENT_VISUALS_TEE_TRAILS,
		COMPONENT_VISUALS_ANIMATIONS,
		COMPONENT_VISUALS_CAMERA_DRIFT,
		COMPONENT_VISUALS_DYNAMIC_FOV,
		COMPONENT_VISUALS_AFTERIMAGE,
		COMPONENT_VISUALS_FOCUS_MODE,
		COMPONENT_VISUALS_CHAT_BUBBLES,
		COMPONENT_VISUALS_3D_PARTICLES,
		COMPONENT_VISUALS_ASPECT_RATIO,
		COMPONENT_RACE_BINDSYSTEM,
		COMPONENT_RACE_SPEEDRUN_TIMER,
		COMPONENT_RACE_AUTO_TEAM_LOCK,
		COMPONENT_GORES_INPUT,
		COMPONENT_GORES_GORES_MODE,
		COMPONENT_GORES_HOOK_COMBO,
		NUM_COMPONENTS_EDITOR_COMPONENTS,
	};

	CBestClient();
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	void OnConsoleInit() override;
	void OnRender() override;

	void OnStateChange(int OldState, int NewState) override;
	void OnNewSnapshot() override;
	void SetForcedAspect();
	void ReloadWindowModeForAspect();

	bool OptimizerEnabled() const;
	bool OptimizerDisableParticles() const;
	bool OptimizerFpsFogEnabled() const;
	void OptimizerFpsFogHalfExtents(float &HalfW, float &HalfH) const;
	bool OptimizerAllowRenderPos(vec2 WorldPos) const;
	void OptimizerSetDdnetPriorityHigh();
	void OptimizerSetDiscordPriorityBelowNormal();
	bool IsComponentDisabled(EBestClientComponent Component) const;
	static bool IsComponentDisabledByMask(int Component, int MaskLo, int MaskHi);

	std::shared_ptr<CHttpRequest> m_pBestClientInfoTask = nullptr;
	void FetchBestClientInfo();
	void FinishBestClientInfo();
	void ResetBestClientInfoTask();
	bool NeedUpdate();

	void RenderHookCombo(bool ForcePreview = false);

	bool ChatDoSpecId(const char *pInput);
	bool InfoTaskDone() { return m_pBestClientInfoTask && m_pBestClientInfoTask->State() == EHttpState::DONE; }
	bool m_FetchedBestClientInfo = false;
	char m_aVersionStr[64] = "0";

	Regex m_RegexChatIgnore;
};

#endif
