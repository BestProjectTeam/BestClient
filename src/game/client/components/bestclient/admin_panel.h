/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_ADMIN_PANEL_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_ADMIN_PANEL_H

#include <engine/console.h>

#include <game/client/component.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

class CAdminPanel : public CComponent
{
public:
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
	static void ConToggleAdminPanel(IConsole::IResult *pResult, void *pUserData);

	void SetActive(bool Active);
	void SetUiMousePos(vec2 Pos);

	void RenderPanel(const CUIRect &Screen);
	void RenderPlayerActions(CUIRect View, int ClientId, int LocalAuth);
	void RenderPlayerInfo(CUIRect View, int ClientId);
	void RenderPlayerList(CUIRect View);
	void RenderRconLogin(CUIRect View);
	void RenderLogs(CUIRect View);
	void RenderFastActions(CUIRect View, int LocalAuth);
	void RenderTunings(CUIRect View, int LocalAuth);
	void RenderVoiceMod(CUIRect View);
	void RenderActionPopup(const CUIRect &Screen, int LocalAuth);
	void OpenActionPopup(int ClientId, int ActionType);
	void CloseActionPopup();

	bool m_Active = false;
	bool m_MouseUnlocked = false;
	std::optional<vec2> m_LastMousePos;
	int m_SelectedClientId = -1;
	int m_ActiveTab = 0;
	float m_OpenAnimation = 0.0f;
	float m_SizeAnimation = 0.0f;
	static constexpr int MAX_FAST_ACTIONS = 10;

	CLineInputBuffered<64> m_RconUserInput;
	CLineInputBuffered<64> m_RconPassInput;
	CLineInputBuffered<64> m_TuningSearchInput;
	CLineInputBuffered<64> m_TuningValueInput;
	CLineInputBuffered<96> m_FastActionInput;
	CLineInputBuffered<64> m_FastActionSearchInput;
	int m_FastActionEditIndex = -1;
	int m_SelectedTuning = -1;
	int m_LastSelectedTuning = -1;
	struct SLogLine
	{
		std::string m_Text;
		char m_aTime[9]; // "HH:MM:SS"
	};
	std::deque<SLogLine> m_RconLogLines;

	CButtonContainer m_TabPlayersButton;
	CButtonContainer m_TabInfoButton;
	CButtonContainer m_TabTuningsButton;
	CButtonContainer m_TabFastActionsButton;
	CButtonContainer m_TabLogsButton;
	CButtonContainer m_TabVoiceButton;

	CButtonContainer m_RconLoginButton;
	CButtonContainer m_RconLogoutButton;
	CButtonContainer m_RconLogButton;
	CButtonContainer m_SettingsButton;
	CButtonContainer m_FastActionAddButton;

	CButtonContainer m_MuteButton;
	CButtonContainer m_SayButton;
	CButtonContainer m_SayTeamButton;
	CButtonContainer m_BroadcastButton;
	CButtonContainer m_UnmuteButton;
	CButtonContainer m_VoteMuteButton;
	CButtonContainer m_VoteUnmuteButton;
	CButtonContainer m_KickButton;
	CButtonContainer m_BanButton;
	CButtonContainer m_KillButton;
	CButtonContainer m_ForcePauseButton;
	CButtonContainer m_ForceUnpauseButton;
	CButtonContainer m_SpectateButton;
	CButtonContainer m_UnspectateButton;
	CButtonContainer m_TeleportButton;
	CButtonContainer m_TeleportToPlayerButton;

	CButtonContainer m_TuningApplyButton;
	CButtonContainer m_TuningResetButton;
	CButtonContainer m_TuningResetAllButton;

	std::array<CButtonContainer, MAX_FAST_ACTIONS> m_FastActionRunButtons;
	std::array<CButtonContainer, MAX_FAST_ACTIONS> m_FastActionEditButtons;
	std::array<CButtonContainer, MAX_FAST_ACTIONS> m_FastActionRemoveButtons;

	CLineInputBuffered<96> m_ActionReasonInput;
	CLineInputBuffered<16> m_ActionDurationInput;
	CUi::SColorPickerPopupContext m_ColorPickerPopupContext;
	int m_ActionPopupType = 0;
	int m_ActionPopupClientId = -1;
	float m_ActionPopupAnim = 0.0f;
	bool m_ActionPopupClosing = false;

	CButtonContainer m_ActionConfirmButton;
	CButtonContainer m_ActionCancelButton;
	CButtonContainer m_ActionPresetShortButton;
	CButtonContainer m_ActionPresetMidButton;
	CButtonContainer m_ActionPresetLongButton;

	// Voice mod tab
	CLineInputBuffered<128> m_VoiceModKeyInput;
	CButtonContainer m_VoiceModAuthButton;
	CButtonContainer m_VoiceModRefreshButton;
	std::vector<CButtonContainer> m_vVoiceModMuteButtons;
	int64_t m_LastVoiceModRefreshTick = 0;
};

#endif // GAME_CLIENT_COMPONENTS_BESTCLIENT_ADMIN_PANEL_H
