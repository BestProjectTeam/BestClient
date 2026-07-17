/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_ADMIN_PANEL_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_ADMIN_PANEL_H

#include <engine/console.h>

#include <game/client/component.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>

#include <array>
#include <deque>
#include <optional>
#include <string>

class CAdminPanel : public CComponent
{
public:
	enum class EAction
	{
		NONE = 0,
		MUTE,
		BAN,
		KICK,
		RESPAWN,
		FORCE_PAUSE,
		SAY,
		SAY_TEAM,
		BROADCAST,
	};

	enum class EActionField
	{
		NONE = 0,
		MESSAGE,
		REASON,
		REASON_DURATION_SECONDS,
		REASON_DURATION_MINUTES,
		DURATION_SECONDS,
	};

	enum
	{
		AUTH_FALLBACK_HELPER = 1,
		AUTH_FALLBACK_MOD = 2,
		AUTH_FALLBACK_ADMIN = 3,
	};

	struct SActionSpec
	{
		EAction m_Action;
		const char *m_pCommand;
		int m_FallbackAuth;
		bool m_NeedsPlayer;
		EActionField m_Field;
	};

	CAdminPanel();
	int Sizeof() const override { return sizeof(*this); }

	void OnConsoleInit() override;
	void OnReset() override;
	void OnRelease() override;
	void OnRender() override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	bool OnInput(const IInput::CEvent &Event) override;
	void OnRconLine(const char *pLine);

	bool IsActive() const { return m_Active; }

private:
	enum class ETab
	{
		PLAYERS = 0,
		TUNINGS,
		LOGS,
		COUNT,
	};

	struct SLogLine
	{
		std::string m_Text;
		char m_aTime[9];
	};

	static void ConToggleAdminPanel(IConsole::IResult *pResult, void *pUserData);

	void SetActive(bool Active);
	void SetUiMousePos(vec2 Pos);
	void CloseActionPopup();
	void OpenActionPopup(const SActionSpec &Spec, int ClientId);

	void RenderPanel(const CUIRect &Screen);
	void RenderRconLogin(CUIRect View);
	void RenderTabs(CUIRect TabBar);
	void RenderPlayersTab(CUIRect View);
	void RenderPlayerActions(CUIRect View);
	void RenderPlayerListAndInfo(CUIRect View);
	void RenderPlayerInfo(CUIRect View, int ClientId);
	void RenderTunings(CUIRect View);
	void RenderLogs(CUIRect View);
	void RenderActionPopup(const CUIRect &Screen);

	bool HasCommand(const char *pCommand, int FallbackAuth) const;
	bool HasActionCommand(const SActionSpec &Spec) const;
	bool IsActionEnabled(const SActionSpec &Spec, int ClientId) const;
	bool HasPlayer(int ClientId) const;
	int LocalAuthLevel() const;
	bool TryBuildActionCommand(char *pBuffer, int BufferSize) const;

	bool m_Active = false;
	bool m_MouseUnlocked = false;
	std::optional<vec2> m_LastMousePos;
	int m_SelectedClientId = -1;
	ETab m_ActiveTab = ETab::PLAYERS;
	int m_SelectedTuning = -1;
	int m_LastSelectedTuning = -1;

	CLineInputBuffered<64> m_RconUserInput;
	CLineInputBuffered<64> m_RconPassInput;
	CLineInputBuffered<64> m_TuningSearchInput;
	CLineInputBuffered<64> m_TuningValueInput;
	CLineInputBuffered<96> m_ActionReasonInput;
	CLineInputBuffered<16> m_ActionDurationInput;

	std::deque<SLogLine> m_RconLogLines;

	CButtonContainer m_TabPlayersButton;
	CButtonContainer m_TabTuningsButton;
	CButtonContainer m_TabLogsButton;
	CButtonContainer m_RconLoginButton;
	CButtonContainer m_RconLogoutButton;
	CButtonContainer m_ClearLogsButton;

	CButtonContainer m_SayButton;
	CButtonContainer m_SayTeamButton;
	CButtonContainer m_BroadcastButton;
	CButtonContainer m_MuteButton;
	CButtonContainer m_UnmuteButton;
	CButtonContainer m_VoteMuteButton;
	CButtonContainer m_VoteUnmuteButton;
	CButtonContainer m_BanButton;
	CButtonContainer m_KickButton;
	CButtonContainer m_RespawnButton;
	CButtonContainer m_ForcePauseButton;
	CButtonContainer m_ForceUnpauseButton;
	CButtonContainer m_SpectateButton;
	CButtonContainer m_UnspectateButton;
	CButtonContainer m_TeleportButton;
	CButtonContainer m_TeleportToPlayerButton;

	CButtonContainer m_TuningApplyButton;
	CButtonContainer m_TuningResetButton;
	CButtonContainer m_TuningResetAllButton;

	EAction m_ActionPopupType = EAction::NONE;
	int m_ActionPopupClientId = -1;
	const SActionSpec *m_pActionPopupSpec = nullptr;
	CButtonContainer m_ActionConfirmButton;
	CButtonContainer m_ActionCancelButton;
	CButtonContainer m_ActionPresetShortButton;
	CButtonContainer m_ActionPresetMidButton;
	CButtonContainer m_ActionPresetLongButton;
};

#endif // GAME_CLIENT_COMPONENTS_BESTCLIENT_ADMIN_PANEL_H
