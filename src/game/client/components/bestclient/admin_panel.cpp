/* Copyright © 2026 BestProject Team */
#include "admin_panel.h"

#include <base/math.h>
#include <base/system.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/font_icons.h>
#include <engine/shared/localization.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/gamecore.h>
#include <game/localization.h>

#include <algorithm>
#include <ctime>
#include <utility>
#include <vector>

namespace
{
constexpr float PANEL_PADDING = 10.0f;
constexpr float HEADER_HEIGHT = 26.0f;
constexpr float TAB_HEIGHT = 24.0f;
constexpr float LIST_ROW_HEIGHT = 26.0f;
constexpr float ACTION_BUTTON_HEIGHT = 22.0f;
constexpr float ACTION_SPACING = 4.0f;
constexpr float ACTION_LABEL_HEIGHT = 16.0f;
constexpr float ACTION_BLOCK_MARGIN = 8.0f;
constexpr float INFO_ROW_HEIGHT = 16.0f;
constexpr float INFO_BLOCK_HEIGHT = 38.0f;
constexpr float LOGIN_ROW_HEIGHT = 24.0f;
constexpr int MAX_LOG_LINES = 200;
constexpr int MAX_LOG_LENGTH = 256;

constexpr ColorRGBA PANEL_COLOR(0.05f, 0.06f, 0.08f, 0.82f);
constexpr ColorRGBA SECTION_COLOR(0.0f, 0.0f, 0.0f, 0.32f);
constexpr ColorRGBA SECTION_DARK_COLOR(0.0f, 0.0f, 0.0f, 0.42f);
constexpr ColorRGBA INFO_BLOCK_COLOR(0.0f, 0.0f, 0.0f, 0.45f);
constexpr ColorRGBA INFO_LABEL_COLOR(0.62f, 0.62f, 0.62f, 1.0f);
constexpr ColorRGBA DISABLED_TEXT_COLOR(0.65f, 0.65f, 0.65f, 0.85f);

const CAdminPanel::SActionSpec s_aActionSpecs[] = {
	{CAdminPanel::EAction::SAY, "say", CAdminPanel::AUTH_FALLBACK_HELPER, false, CAdminPanel::EActionField::MESSAGE},
	{CAdminPanel::EAction::SAY_TEAM, "say_team", CAdminPanel::AUTH_FALLBACK_HELPER, false, CAdminPanel::EActionField::MESSAGE},
	{CAdminPanel::EAction::BROADCAST, "broadcast", CAdminPanel::AUTH_FALLBACK_HELPER, false, CAdminPanel::EActionField::MESSAGE},
	{CAdminPanel::EAction::MUTE, "muteid", CAdminPanel::AUTH_FALLBACK_HELPER, true, CAdminPanel::EActionField::REASON_DURATION_SECONDS},
	{CAdminPanel::EAction::BAN, "ban", CAdminPanel::AUTH_FALLBACK_MOD, true, CAdminPanel::EActionField::REASON_DURATION_MINUTES},
	{CAdminPanel::EAction::KICK, "kick", CAdminPanel::AUTH_FALLBACK_MOD, true, CAdminPanel::EActionField::REASON},
	{CAdminPanel::EAction::RESPAWN, "respawn", CAdminPanel::AUTH_FALLBACK_MOD, true, CAdminPanel::EActionField::REASON},
	{CAdminPanel::EAction::FORCE_PAUSE, "force_pause", CAdminPanel::AUTH_FALLBACK_MOD, true, CAdminPanel::EActionField::DURATION_SECONDS},
};

ColorRGBA ButtonTextColor(bool Enabled)
{
	return Enabled ? ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f) : DISABLED_TEXT_COLOR;
}

const char *ActionLabel(CAdminPanel::EAction Action)
{
	switch(Action)
	{
	case CAdminPanel::EAction::SAY:
		return Localize("Say");
	case CAdminPanel::EAction::SAY_TEAM:
		return Localize("Say team");
	case CAdminPanel::EAction::BROADCAST:
		return Localize("Broadcast");
	case CAdminPanel::EAction::MUTE:
		return Localize("Mute");
	case CAdminPanel::EAction::BAN:
		return Localize("Ban");
	case CAdminPanel::EAction::KICK:
		return Localize("Kick");
	case CAdminPanel::EAction::RESPAWN:
		return Localize("Respawn");
	case CAdminPanel::EAction::FORCE_PAUSE:
		return Localize("Force pause");
	case CAdminPanel::EAction::NONE:
		break;
	}
	return "";
}

const char *DirectActionLabel(const char *pCommand, CButtonContainer *pButton, CButtonContainer *pVoteMuteButton, CButtonContainer *pTeleportButton, CButtonContainer *pTeleportToPlayerButton, CButtonContainer *pSpectateButton, CButtonContainer *pUnspectateButton)
{
	if(str_comp(pCommand, "unmuteid") == 0)
		return Localize("Unmute");
	if(pButton == pVoteMuteButton)
		return Localize("Vote mute (10 min)");
	if(str_comp(pCommand, "vote_unmuteid") == 0)
		return Localize("Vote unmute");
	if(pButton == pTeleportButton)
		return Localize("Teleport to me");
	if(pButton == pTeleportToPlayerButton)
		return Localize("Teleport to player");
	if(str_comp(pCommand, "force_unpause") == 0)
		return Localize("Force unpause");
	if(pButton == pSpectateButton)
		return Localize("Move to spectators");
	if(pButton == pUnspectateButton)
		return Localize("Return to game");
	return "";
}
} // namespace

CAdminPanel::CAdminPanel()
{
	OnReset();
}

void CAdminPanel::OnConsoleInit()
{
	Console()->Register("toggle_admin_panel", "", CFGFLAG_CLIENT, ConToggleAdminPanel, this, "Toggle admin panel");
}

void CAdminPanel::OnReset()
{
	m_Active = false;
	m_MouseUnlocked = false;
	m_LastMousePos = std::nullopt;
	m_SelectedClientId = -1;
	m_ActiveTab = ETab::PLAYERS;
	m_SelectedTuning = -1;
	m_LastSelectedTuning = -1;
	m_RconLogLines.clear();
	m_LogStickToBottom = true;
	m_LogScrollToBottomPending = false;
	CloseActionPopup();
	m_ActionPopupType = EAction::NONE;
	m_ActionPopupClientId = -1;
	m_pActionPopupSpec = nullptr;

	m_RconUserInput.Clear();
	m_RconPassInput.Clear();
	m_TuningSearchInput.Clear();
	m_TuningValueInput.Clear();
	m_ActionReasonInput.Clear();
	m_ActionDurationInput.Clear();

	m_RconUserInput.SetEmptyText(Localize("Username"));
	m_RconPassInput.SetEmptyText(Localize("Password"));
	m_RconPassInput.SetHidden(true);
	m_TuningSearchInput.SetEmptyText(Localize("Search tunings"));
	m_TuningValueInput.SetEmptyText(Localize("Value"));
	m_ActionReasonInput.SetEmptyText(Localize("Reason"));
	m_ActionDurationInput.SetEmptyText(Localize("Duration"));
}

void CAdminPanel::OnRelease()
{
	SetActive(false);
}

void CAdminPanel::ConToggleAdminPanel(IConsole::IResult *pResult, void *pUserData)
{
	CAdminPanel *pSelf = static_cast<CAdminPanel *>(pUserData);
	pSelf->SetActive(!pSelf->m_Active);
}

void CAdminPanel::SetUiMousePos(vec2 Pos)
{
	const vec2 WindowSize = vec2(Graphics()->WindowWidth(), Graphics()->WindowHeight());
	const CUIRect *pScreen = Ui()->Screen();
	const vec2 UpdatedMousePos = Ui()->UpdatedMousePos();
	Pos = Pos / vec2(pScreen->w, pScreen->h) * WindowSize;
	Ui()->OnCursorMove(Pos.x - UpdatedMousePos.x, Pos.y - UpdatedMousePos.y);
}

void CAdminPanel::SetActive(bool Active)
{
	if(m_Active == Active)
		return;

	m_Active = Active;
	if(m_Active)
	{
		m_MouseUnlocked = true;
		m_LastMousePos = Ui()->MousePos();
		SetUiMousePos(Ui()->Screen()->Center());
	}
	else if(m_MouseUnlocked)
	{
		Ui()->ClosePopupMenus();
		Ui()->ClearHotkeys();
		CloseActionPopup();
		m_MouseUnlocked = false;
		if(m_LastMousePos.has_value())
			SetUiMousePos(m_LastMousePos.value());
		m_LastMousePos = Ui()->MousePos();
	}
}

bool CAdminPanel::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!m_Active || !m_MouseUnlocked)
		return false;

	Ui()->ConvertMouseMove(&x, &y, CursorType);
	Ui()->OnCursorMove(x, y);
	return true;
}

bool CAdminPanel::OnInput(const IInput::CEvent &Event)
{
	if(!m_Active)
		return false;

	Ui()->OnInput(Event);
	if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_ESCAPE)
	{
		if(m_ActionPopupType != EAction::NONE)
			CloseActionPopup();
		else
			SetActive(false);
		return true;
	}
	return true;
}

bool CAdminPanel::HasPlayer(int ClientId) const
{
	return ClientId >= 0 && ClientId < MAX_CLIENTS && GameClient()->m_Snap.m_apPlayerInfos[ClientId];
}

int CAdminPanel::LocalAuthLevel() const
{
	if(GameClient()->m_Snap.m_LocalClientId < 0)
		return AUTHED_NO;
	return GameClient()->m_aClients[GameClient()->m_Snap.m_LocalClientId].m_AuthLevel;
}

bool CAdminPanel::HasCommand(const char *pCommand, int FallbackAuth) const
{
	if(!Client()->RconAuthed())
		return false;
	if(Client()->UseTempRconCommands())
	{
		if(Console()->GetCommandInfo(pCommand, CFGFLAG_SERVER, true) != nullptr)
			return true;
		// Catalog still downloading — allow actions immediately (rcon accepts typed cmds anyway).
		// Do NOT use GotRconCommandsPercentage(): it returns -1 when idle, and -1 < 1 is always true.
		return Client()->ReceivingRconCommands();
	}
	return LocalAuthLevel() >= FallbackAuth;
}

bool CAdminPanel::HasActionCommand(const SActionSpec &Spec) const
{
	return HasCommand(Spec.m_pCommand, Spec.m_FallbackAuth);
}

bool CAdminPanel::IsActionEnabled(const SActionSpec &Spec, int ClientId) const
{
	return (!Spec.m_NeedsPlayer || HasPlayer(ClientId)) && HasActionCommand(Spec);
}

void CAdminPanel::OpenActionPopup(const SActionSpec &Spec, int ClientId)
{
	if(Spec.m_NeedsPlayer && !HasPlayer(ClientId))
		return;

	m_ActionPopupType = Spec.m_Action;
	m_ActionPopupClientId = ClientId;
	m_pActionPopupSpec = &Spec;
	m_ActionReasonInput.Clear();
	m_ActionDurationInput.Clear();

	if(Spec.m_Action == EAction::MUTE)
	{
		m_ActionReasonInput.Set(Localize("Muted by admin panel"));
		m_ActionDurationInput.Set("600");
		m_ActionDurationInput.SetEmptyText(Localize("Seconds"));
	}
	else if(Spec.m_Action == EAction::BAN)
	{
		m_ActionReasonInput.Set(Localize("Banned by admin panel"));
		m_ActionDurationInput.Set("10");
		m_ActionDurationInput.SetEmptyText(Localize("Minutes"));
	}
	else if(Spec.m_Action == EAction::KICK)
	{
		m_ActionReasonInput.Set(Localize("Kicked by admin panel"));
		m_ActionDurationInput.SetEmptyText(Localize("Duration"));
	}
	else if(Spec.m_Action == EAction::RESPAWN)
	{
		m_ActionReasonInput.Set(Localize("Respawned by admin panel"));
		m_ActionDurationInput.SetEmptyText(Localize("Duration"));
	}
	else if(Spec.m_Action == EAction::FORCE_PAUSE)
	{
		m_ActionDurationInput.Set("30");
		m_ActionDurationInput.SetEmptyText(Localize("Seconds"));
	}
	else
	{
		m_ActionReasonInput.SetEmptyText(Localize("Message"));
		m_ActionDurationInput.SetEmptyText(Localize("Duration"));
	}
}

void CAdminPanel::CloseActionPopup()
{
	m_ActionPopupType = EAction::NONE;
	m_ActionPopupClientId = -1;
	m_pActionPopupSpec = nullptr;
}

bool CAdminPanel::TryBuildActionCommand(char *pBuffer, int BufferSize) const
{
	if(!m_pActionPopupSpec)
		return false;

	const char *pReason = m_ActionReasonInput.GetString();
	const char *pDuration = m_ActionDurationInput.GetString();
	if(m_ActionPopupType == EAction::MUTE)
	{
		str_format(pBuffer, BufferSize, "muteid %d %s %s", m_ActionPopupClientId, m_ActionDurationInput.IsEmpty() ? "600" : pDuration, m_ActionReasonInput.IsEmpty() ? "Muted by admin panel" : pReason);
		return true;
	}
	if(m_ActionPopupType == EAction::BAN)
	{
		str_format(pBuffer, BufferSize, "ban %d %s %s", m_ActionPopupClientId, m_ActionDurationInput.IsEmpty() ? "10" : pDuration, m_ActionReasonInput.IsEmpty() ? "Banned by admin panel" : pReason);
		return true;
	}
	if(m_ActionPopupType == EAction::KICK)
	{
		str_format(pBuffer, BufferSize, "kick %d %s", m_ActionPopupClientId, m_ActionReasonInput.IsEmpty() ? "Kicked by admin panel" : pReason);
		return true;
	}
	if(m_ActionPopupType == EAction::RESPAWN)
	{
		str_format(pBuffer, BufferSize, "respawn %d %s", m_ActionPopupClientId, m_ActionReasonInput.IsEmpty() ? "Respawned by admin panel" : pReason);
		return true;
	}
	if(m_ActionPopupType == EAction::FORCE_PAUSE)
	{
		str_format(pBuffer, BufferSize, "force_pause %d %s", m_ActionPopupClientId, m_ActionDurationInput.IsEmpty() ? "30" : pDuration);
		return true;
	}
	if(m_ActionPopupType == EAction::SAY)
	{
		if(m_ActionReasonInput.IsEmpty())
			return false;
		str_format(pBuffer, BufferSize, "say %s", pReason);
		return true;
	}
	if(m_ActionPopupType == EAction::SAY_TEAM)
	{
		if(m_ActionReasonInput.IsEmpty())
			return false;
		str_format(pBuffer, BufferSize, "say_team %s", pReason);
		return true;
	}
	if(m_ActionPopupType == EAction::BROADCAST)
	{
		if(m_ActionReasonInput.IsEmpty())
			return false;
		str_format(pBuffer, BufferSize, "broadcast %s", pReason);
		return true;
	}
	return false;
}

void CAdminPanel::RenderRconLogin(CUIRect View)
{
	const bool UsernameRequired = GameClient()->m_GameConsole.RconUsernameRequired();
	CUIRect Box = View;
	Box.VMargin(View.w * 0.22f, &Box);
	Box.HSplitTop(10.0f, nullptr, &Box);

	CUIRect Row;
	Box.HSplitTop(28.0f, &Row, &Box);
	Ui()->DoLabel(&Row, UsernameRequired ? Localize("RCON login") : Localize("RCON password"), 18.0f, TEXTALIGN_ML);
	Box.HSplitTop(10.0f, nullptr, &Box);

	if(UsernameRequired)
	{
		CUIRect Label, Field;
		Box.HSplitTop(LOGIN_ROW_HEIGHT, &Row, &Box);
		Row.VSplitLeft(120.0f, &Label, &Field);
		Ui()->DoLabel(&Label, Localize("Username"), 12.0f, TEXTALIGN_ML);
		Ui()->DoEditBox(&m_RconUserInput, &Field, 12.0f);
		Box.HSplitTop(6.0f, nullptr, &Box);
	}

	CUIRect Label, Field;
	Box.HSplitTop(LOGIN_ROW_HEIGHT, &Row, &Box);
	Row.VSplitLeft(120.0f, &Label, &Field);
	Ui()->DoLabel(&Label, Localize("Password"), 12.0f, TEXTALIGN_ML);
	Ui()->DoEditBox(&m_RconPassInput, &Field, 12.0f);
	Box.HSplitTop(10.0f, nullptr, &Box);

	Box.HSplitTop(LOGIN_ROW_HEIGHT, &Row, &Box);
	Row.VSplitLeft(120.0f, nullptr, &Field);
	Field.VSplitLeft(140.0f, &Field, nullptr);
	const bool CanSubmit = !m_RconPassInput.IsEmpty() && (!UsernameRequired || !m_RconUserInput.IsEmpty());
	TextRender()->TextColor(ButtonTextColor(CanSubmit));
	bool Submit = GameClient()->m_Menus.DoButton_Menu(&m_RconLoginButton, Localize("Login"), CanSubmit ? 0 : -1, &Field);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	Submit = Submit || (CanSubmit && m_RconPassInput.IsActive() && Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER));
	if(Submit && CanSubmit)
	{
		const char *pUser = UsernameRequired ? m_RconUserInput.GetString() : "";
		Client()->RconAuth(pUser, m_RconPassInput.GetString(), g_Config.m_ClDummy);
	}

	Box.HSplitTop(8.0f, nullptr, &Box);
	if(Client()->ReceivingRconCommands())
	{
		char aLoading[64];
		str_format(aLoading, sizeof(aLoading), "%s %.0f%%", Localize("Loading commands"), Client()->GotRconCommandsPercentage() * 100.0f);
		Ui()->DoLabel(&Box, aLoading, 12.0f, TEXTALIGN_ML);
	}
	else
	{
		Ui()->DoLabel(&Box, UsernameRequired ? Localize("Server uses username and password rcon.") : Localize("Server uses password-only rcon."), 12.0f, TEXTALIGN_ML);
	}
}

void CAdminPanel::RenderTabs(CUIRect TabBar)
{
	TabBar.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.28f), IGraphics::CORNER_ALL, 6.0f);
	TabBar.VMargin(2.0f, &TabBar);
	const float TabWidth = TabBar.w / 3.0f;

	CUIRect Button;
	TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
	if(GameClient()->m_Menus.DoButton_MenuTab(&m_TabPlayersButton, Localize("Players"), m_ActiveTab == ETab::PLAYERS, &Button, IGraphics::CORNER_L))
		m_ActiveTab = ETab::PLAYERS;
	TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
	if(GameClient()->m_Menus.DoButton_MenuTab(&m_TabTuningsButton, Localize("Tunings"), m_ActiveTab == ETab::TUNINGS, &Button, IGraphics::CORNER_NONE))
		m_ActiveTab = ETab::TUNINGS;
	if(GameClient()->m_Menus.DoButton_MenuTab(&m_TabLogsButton, Localize("Logs"), m_ActiveTab == ETab::LOGS, &TabBar, IGraphics::CORNER_R))
		m_ActiveTab = ETab::LOGS;
}

void CAdminPanel::RenderPlayerActions(CUIRect View)
{
	char aTitle[128];
	if(HasPlayer(m_SelectedClientId))
		str_format(aTitle, sizeof(aTitle), Localize("Actions for %s"), GameClient()->m_aClients[m_SelectedClientId].m_aName);
	else
		str_copy(aTitle, Localize("Player actions"));

	CUIRect Header;
	View.HSplitTop(ACTION_LABEL_HEIGHT, &Header, &View);
	Ui()->DoLabel(&Header, aTitle, 13.0f, TEXTALIGN_ML);
	View.HSplitTop(ACTION_SPACING, nullptr, &View);

	static CScrollRegion s_ActionScroll;
	static vec2 s_ActionScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 30.0f;
	ScrollParams.m_ScrollbarThickness = 14.0f;
	ScrollParams.m_ScrollbarMargin = 3.0f;
	s_ActionScroll.Begin(&View, &ScrollParams);
	auto DoActionPopupButton = [&](CButtonContainer &Button, const SActionSpec &Spec, CUIRect ButtonRect) {
		const bool Enabled = IsActionEnabled(Spec, m_SelectedClientId);
		if(s_ActionScroll.AddRect(ButtonRect))
		{
			TextRender()->TextColor(ButtonTextColor(Enabled));
			if(GameClient()->m_Menus.DoButton_Menu(&Button, ActionLabel(Spec.m_Action), Enabled ? 0 : -1, &ButtonRect) && Enabled)
				OpenActionPopup(Spec, Spec.m_NeedsPlayer ? m_SelectedClientId : -1);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
	};

	auto DoActionRow = [&](CButtonContainer &LeftButton, const SActionSpec &LeftSpec, CButtonContainer *pRightButton, const SActionSpec *pRightSpec) {
		CUIRect Row, Left, Right;
		View.HSplitTop(ACTION_BUTTON_HEIGHT, &Row, &View);
		if(pRightButton && pRightSpec)
		{
			Row.VSplitMid(&Left, &Right, ACTION_SPACING);
			DoActionPopupButton(LeftButton, LeftSpec, Left);
			DoActionPopupButton(*pRightButton, *pRightSpec, Right);
		}
		else
		{
			DoActionPopupButton(LeftButton, LeftSpec, Row);
		}
		View.HSplitTop(ACTION_SPACING, nullptr, &View);
	};

	DoActionRow(m_SayButton, s_aActionSpecs[0], &m_SayTeamButton, &s_aActionSpecs[1]);
	DoActionRow(m_BroadcastButton, s_aActionSpecs[2], nullptr, nullptr);
	DoActionRow(m_MuteButton, s_aActionSpecs[3], &m_BanButton, &s_aActionSpecs[4]);
	DoActionRow(m_KickButton, s_aActionSpecs[5], &m_RespawnButton, &s_aActionSpecs[6]);
	DoActionRow(m_ForcePauseButton, s_aActionSpecs[7], nullptr, nullptr);

	struct SDirectAction
	{
		CButtonContainer *m_pButton;
		const char *m_pCommand;
		int m_FallbackAuth;
	};
	SDirectAction aDirectActions[] = {
		{&m_UnmuteButton, "unmuteid", AUTH_FALLBACK_HELPER},
		{&m_VoteMuteButton, "vote_muteid", AUTH_FALLBACK_HELPER},
		{&m_VoteUnmuteButton, "vote_unmuteid", AUTH_FALLBACK_HELPER},
		{&m_TeleportButton, "tele", AUTH_FALLBACK_MOD},
		{&m_TeleportToPlayerButton, "tele", AUTH_FALLBACK_MOD},
		{&m_ForceUnpauseButton, "force_unpause", AUTH_FALLBACK_MOD},
		{&m_SpectateButton, "set_team", AUTH_FALLBACK_MOD},
		{&m_UnspectateButton, "set_team", AUTH_FALLBACK_MOD},
	};
	const int NumDirectActions = (int)(sizeof(aDirectActions) / sizeof(aDirectActions[0]));

	for(int i = 0; i < NumDirectActions;)
	{
		CUIRect Row, Left, Right;
		View.HSplitTop(ACTION_BUTTON_HEIGHT, &Row, &View);
		const bool HasPair = i + 1 < NumDirectActions;
		if(HasPair)
			Row.VSplitMid(&Left, &Right, ACTION_SPACING);
		else
			Left = Row;

		auto RenderDirect = [&](const SDirectAction &Action, CUIRect ButtonRect) {
			bool Enabled = HasPlayer(m_SelectedClientId);
			if(Enabled)
			{
				if(Action.m_pButton == &m_TeleportToPlayerButton)
					Enabled = HasCommand("move_raw", Action.m_FallbackAuth) || HasCommand("tele", Action.m_FallbackAuth);
				else
					Enabled = HasCommand(Action.m_pCommand, Action.m_FallbackAuth);
			}
			if(!s_ActionScroll.AddRect(ButtonRect))
				return;
			TextRender()->TextColor(ButtonTextColor(Enabled));
			const char *pLabel = DirectActionLabel(Action.m_pCommand, Action.m_pButton, &m_VoteMuteButton, &m_TeleportButton, &m_TeleportToPlayerButton, &m_SpectateButton, &m_UnspectateButton);
			if(GameClient()->m_Menus.DoButton_Menu(Action.m_pButton, pLabel, Enabled ? 0 : -1, &ButtonRect) && Enabled)
			{
				char aCmd[128] = "";
				const int LocalId = GameClient()->m_Snap.m_LocalClientId;
				if(Action.m_pButton == &m_VoteMuteButton)
					str_format(aCmd, sizeof(aCmd), "vote_muteid %d 600 Muted by admin panel", m_SelectedClientId);
				else if(Action.m_pButton == &m_TeleportButton && LocalId >= 0)
					str_format(aCmd, sizeof(aCmd), "tele %d %d", m_SelectedClientId, LocalId);
				else if(Action.m_pButton == &m_TeleportToPlayerButton)
				{
					// Prefer predicted pos; fall back to snap so we still tele when prediction is cold.
					vec2 TargetPos = GameClient()->m_aClients[m_SelectedClientId].m_Predicted.m_Pos;
					if(GameClient()->m_Snap.m_aCharacters[m_SelectedClientId].m_Active)
					{
						TargetPos.x = GameClient()->m_Snap.m_aCharacters[m_SelectedClientId].m_Cur.m_X;
						TargetPos.y = GameClient()->m_Snap.m_aCharacters[m_SelectedClientId].m_Cur.m_Y;
					}
					const vec2 Diff = TargetPos - GameClient()->m_LocalCharacterPos;
					if(HasCommand("move_raw", AUTH_FALLBACK_MOD) || Client()->ReceivingRconCommands())
						str_format(aCmd, sizeof(aCmd), "move_raw %d %d", round_to_int(Diff.x), round_to_int(Diff.y));
					else if(LocalId >= 0)
						str_format(aCmd, sizeof(aCmd), "tele %d %d", LocalId, m_SelectedClientId);
				}
				else if(Action.m_pButton == &m_SpectateButton)
					str_format(aCmd, sizeof(aCmd), "set_team %d -1 0", m_SelectedClientId);
				else if(Action.m_pButton == &m_UnspectateButton)
					str_format(aCmd, sizeof(aCmd), "set_team %d 0 0", m_SelectedClientId);
				else if(Action.m_pButton != &m_TeleportButton)
					str_format(aCmd, sizeof(aCmd), "%s %d", Action.m_pCommand, m_SelectedClientId);
				if(aCmd[0] != '\0')
					Client()->Rcon(aCmd);
			}
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		};

		RenderDirect(aDirectActions[i], Left);
		++i;
		if(HasPair)
		{
			RenderDirect(aDirectActions[i], Right);
			++i;
		}
		View.HSplitTop(ACTION_SPACING, nullptr, &View);
	}

	if(!HasPlayer(m_SelectedClientId))
	{
		CUIRect Hint;
		View.HSplitTop(ACTION_LABEL_HEIGHT, &Hint, &View);
		if(s_ActionScroll.AddRect(Hint))
			Ui()->DoLabel(&Hint, Localize("Select a player to enable player actions"), 12.0f, TEXTALIGN_ML);
	}

	CUIRect ScrollEnd = {View.x, View.y + ACTION_SPACING, View.w, 0.0f};
	s_ActionScroll.AddRect(ScrollEnd);
	s_ActionScroll.End();
}

void CAdminPanel::RenderPlayerInfo(CUIRect View, int ClientId)
{
	if(!HasPlayer(ClientId))
	{
		Ui()->DoLabel(&View, Localize("Select a player"), 14.0f, TEXTALIGN_ML);
		return;
	}

	const CNetObj_PlayerInfo *pInfo = GameClient()->m_Snap.m_apPlayerInfos[ClientId];
	const CGameClient::CClientData &ClientData = GameClient()->m_aClients[ClientId];

	auto RenderBlock = [&](CUIRect Block, const char *pLabel, const char *pValue) {
		Block.Draw(INFO_BLOCK_COLOR, IGraphics::CORNER_ALL, 5.0f);
		Block.VMargin(8.0f, &Block);
		Block.HMargin(4.0f, &Block);
		CUIRect Label, Value;
		Block.HSplitTop(11.0f, &Label, &Value);
		TextRender()->TextColor(INFO_LABEL_COLOR);
		Ui()->DoLabel(&Label, pLabel, 10.0f, TEXTALIGN_ML);
		TextRender()->TextColor(ColorRGBA(0.95f, 0.95f, 0.95f, 1.0f));
		SLabelProperties ValueProps;
		ValueProps.m_MaxWidth = maximum(1.0f, Value.w);
		ValueProps.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Value, pValue, 12.0f, TEXTALIGN_ML, ValueProps);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	};

	char aId[32];
	char aScore[32];
	char aPing[32];
	str_format(aId, sizeof(aId), "%d", ClientId);
	str_format(aScore, sizeof(aScore), "%d", pInfo->m_Score);
	str_format(aPing, sizeof(aPing), "%d", std::clamp(pInfo->m_Latency, 0, 999));

	char aStatus[256];
	aStatus[0] = '\0';
	auto AddStatus = [&](bool Condition, const char *pName) {
		if(!Condition)
			return;
		if(aStatus[0] != '\0')
			str_append(aStatus, ", ");
		str_append(aStatus, pName);
	};
	AddStatus(ClientData.m_Super, Localize("Super"));
	AddStatus(ClientData.m_Invincible, Localize("Invincible"));
	AddStatus(ClientData.m_Jetpack, Localize("Jetpack"));
	AddStatus(ClientData.m_EndlessJump, Localize("Endless jump"));
	AddStatus(ClientData.m_EndlessHook, Localize("Endless hook"));
	AddStatus(ClientData.m_Solo, Localize("Solo"));
	AddStatus(ClientData.m_DeepFrozen, Localize("Deep frozen"));
	AddStatus(ClientData.m_LiveFrozen, Localize("Live freeze"));
	AddStatus(ClientData.m_FreezeEnd > 0, Localize("Frozen"));
	if(aStatus[0] == '\0')
		str_copy(aStatus, Localize("Normal"));

	const float RowH = (View.h - ACTION_SPACING * 2.0f) / 3.0f;
	CUIRect Row, Left, Right;

	View.HSplitTop(RowH, &Row, &View);
	View.HSplitTop(ACTION_SPACING, nullptr, &View);
	Row.VSplitMid(&Left, &Right, PANEL_PADDING);
	RenderBlock(Left, Localize("Name"), ClientData.m_aName);
	RenderBlock(Right, Localize("Score"), aScore);

	View.HSplitTop(RowH, &Row, &View);
	View.HSplitTop(ACTION_SPACING, nullptr, &View);
	Row.VSplitMid(&Left, &Right, PANEL_PADDING);
	RenderBlock(Left, Localize("Clan"), ClientData.m_aClan[0] ? ClientData.m_aClan : "-");
	RenderBlock(Right, Localize("Ping"), aPing);

	View.HSplitTop(RowH, &Row, &View);
	Row.VSplitMid(&Left, &Right, PANEL_PADDING);
	RenderBlock(Left, Localize("Client ID"), aId);
	RenderBlock(Right, Localize("Status"), aStatus);
}

void CAdminPanel::RenderPlayersTab(CUIRect View)
{
	CUIRect Top, InfoBottom, Left, Right;
	const float InfoHeight = std::clamp(View.h * 0.38f, 180.0f, 260.0f);
	View.HSplitBottom(InfoHeight, &Top, &InfoBottom);
	InfoBottom.HSplitTop(ACTION_SPACING, nullptr, &InfoBottom);

	Top.VSplitLeft(Top.w * 0.42f, &Left, &Right);
	Right.VSplitLeft(PANEL_PADDING, nullptr, &Right);

	Left.Draw(SECTION_COLOR, IGraphics::CORNER_ALL, 6.0f);
	Left.Margin(ACTION_BLOCK_MARGIN, &Left);
	RenderPlayerActions(Left);

	Right.Draw(SECTION_DARK_COLOR, IGraphics::CORNER_ALL, 6.0f);
	Right.Margin(ACTION_BLOCK_MARGIN, &Right);

	int NumOptions = 0;
	int Selected = -1;
	int aPlayerIds[MAX_CLIENTS];
	for(const auto &pInfoByName : GameClient()->m_Snap.m_apInfoByName)
	{
		if(!pInfoByName)
			continue;
		const int ClientId = pInfoByName->m_ClientId;
		if(ClientId == GameClient()->m_Snap.m_LocalClientId)
			continue;
		if(m_SelectedClientId == ClientId)
			Selected = NumOptions;
		aPlayerIds[NumOptions++] = ClientId;
	}

	CUIRect ListHeader;
	Right.HSplitTop(ACTION_LABEL_HEIGHT, &ListHeader, &Right);
	Ui()->DoLabel(&ListHeader, Localize("Players"), 13.0f, TEXTALIGN_ML);
	Right.HSplitTop(ACTION_SPACING, nullptr, &Right);

	if(NumOptions == 0)
	{
		Ui()->DoLabel(&Right, Localize("No other players"), 13.0f, TEXTALIGN_ML);
		m_SelectedClientId = -1;
	}
	else
	{
		static CListBox s_ListBox;
		s_ListBox.SetActive(true);
		s_ListBox.DoStart(LIST_ROW_HEIGHT, NumOptions, 1, 6, Selected, &Right, false, IGraphics::CORNER_ALL);

		for(int i = 0; i < NumOptions; i++)
		{
			const CListboxItem Item = s_ListBox.DoNextItem(&aPlayerIds[i], Selected == i);
			if(!Item.m_Visible)
				continue;

			CUIRect TeeRect, Label;
			Item.m_Rect.VSplitLeft(Item.m_Rect.h, &TeeRect, &Label);
			CTeeRenderInfo TeeInfo = GameClient()->m_aClients[aPlayerIds[i]].m_RenderInfo;
			TeeInfo.m_Size = TeeRect.h;

			const CAnimState *pIdleState = CAnimState::GetIdle();
			vec2 OffsetToMid;
			CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);
			const vec2 TeeRenderPos(TeeRect.x + TeeInfo.m_Size / 2, TeeRect.y + TeeInfo.m_Size / 2 + OffsetToMid.y);
			RenderTools()->RenderTee(pIdleState, &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRenderPos);

			const int PlayerAuth = GameClient()->m_aClients[aPlayerIds[i]].m_AuthLevel;
			if(PlayerAuth > AUTHED_NO)
			{
				CUIRect NameRect, AuthRect;
				Label.VSplitRight(Label.h, &NameRect, &AuthRect);
				Ui()->DoLabel(&NameRect, GameClient()->m_aClients[aPlayerIds[i]].m_aName, 13.0f, TEXTALIGN_ML);
				TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
				TextRender()->TextColor(PlayerAuth == AUTHED_ADMIN ? ColorRGBA(1.0f, 0.7f, 0.2f, 1.0f) : PlayerAuth == AUTHED_MOD ? ColorRGBA(0.4f, 0.8f, 1.0f, 1.0f) : ColorRGBA(0.5f, 1.0f, 0.5f, 1.0f));
				Ui()->DoLabel(&AuthRect, FontIcon::LOCK, AuthRect.h * 0.65f, TEXTALIGN_MC);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
				TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			}
			else
				Ui()->DoLabel(&Label, GameClient()->m_aClients[aPlayerIds[i]].m_aName, 13.0f, TEXTALIGN_ML);
		}

		Selected = s_ListBox.DoEnd();
		if(Selected != -1)
			m_SelectedClientId = aPlayerIds[Selected];
		else if(!HasPlayer(m_SelectedClientId))
			m_SelectedClientId = -1;
	}

	InfoBottom.Draw(SECTION_COLOR, IGraphics::CORNER_ALL, 6.0f);
	InfoBottom.Margin(ACTION_BLOCK_MARGIN, &InfoBottom);
	const CUIRect InfoClip = InfoBottom;
	Ui()->ClipEnable(&InfoClip);
	CUIRect InfoHeader;
	InfoBottom.HSplitTop(ACTION_LABEL_HEIGHT, &InfoHeader, &InfoBottom);
	Ui()->DoLabel(&InfoHeader, Localize("Player info"), 13.0f, TEXTALIGN_ML);
	InfoBottom.HSplitTop(ACTION_SPACING, nullptr, &InfoBottom);
	RenderPlayerInfo(InfoBottom, m_SelectedClientId);
	Ui()->ClipDisable();
}

void CAdminPanel::RenderTunings(CUIRect View)
{
	CUIRect SearchBar, Body, Left, Right;
	View.HSplitTop(LOGIN_ROW_HEIGHT + ACTION_BLOCK_MARGIN * 2.0f, &SearchBar, &Body);
	SearchBar.Draw(SECTION_COLOR, IGraphics::CORNER_ALL, 6.0f);
	SearchBar.Margin(ACTION_BLOCK_MARGIN, &SearchBar);
	Ui()->DoEditBox_Search(&m_TuningSearchInput, &SearchBar, 12.0f, !Ui()->IsPopupOpen());
	Body.HSplitTop(ACTION_SPACING, nullptr, &Body);

	Body.VSplitLeft(Body.w * 0.48f, &Left, &Right);
	Right.VSplitLeft(PANEL_PADDING, nullptr, &Right);

	Left.Draw(SECTION_DARK_COLOR, IGraphics::CORNER_ALL, 6.0f);
	Right.Draw(SECTION_COLOR, IGraphics::CORNER_ALL, 6.0f);
	Left.Margin(ACTION_BLOCK_MARGIN, &Left);
	Right.Margin(ACTION_BLOCK_MARGIN, &Right);

	CUIRect ListHeader;
	Left.HSplitTop(ACTION_LABEL_HEIGHT, &ListHeader, &Left);
	Ui()->DoLabel(&ListHeader, Localize("Tunings"), 13.0f, TEXTALIGN_ML);
	Left.HSplitTop(ACTION_SPACING, nullptr, &Left);

	static std::vector<int> s_vTuneIndices;
	s_vTuneIndices.clear();
	s_vTuneIndices.reserve(CTuningParams::Num());
	const char *pSearch = m_TuningSearchInput.GetString();
	for(int i = 0; i < CTuningParams::Num(); i++)
	{
		const char *pName = CTuningParams::Name(i);
		if(pSearch[0] == '\0' || str_find_nocase(pName, pSearch))
			s_vTuneIndices.push_back(i);
	}

	int Selected = -1;
	for(int i = 0; i < (int)s_vTuneIndices.size(); i++)
	{
		if(s_vTuneIndices[i] == m_SelectedTuning)
			Selected = i;
	}
	if(Selected == -1 && !s_vTuneIndices.empty())
	{
		m_SelectedTuning = s_vTuneIndices[0];
		Selected = 0;
	}

	static CListBox s_TuningList;
	s_TuningList.SetActive(true);
	s_TuningList.DoStart(LIST_ROW_HEIGHT, (int)s_vTuneIndices.size(), 1, 6, Selected, &Left, false, IGraphics::CORNER_ALL);
	const CTuningParams *pTuning = GameClient()->GetTuning(0);
	for(int i = 0; i < (int)s_vTuneIndices.size(); i++)
	{
		const CListboxItem Item = s_TuningList.DoNextItem(&s_vTuneIndices[i], Selected == i);
		if(!Item.m_Visible)
			continue;
		float CurrentValue = 0.0f;
		pTuning->Get(s_vTuneIndices[i], &CurrentValue);
		char aValue[32];
		str_format(aValue, sizeof(aValue), "%.2f", CurrentValue);
		CUIRect Name, Value;
		Item.m_Rect.VSplitLeft(Item.m_Rect.w * 0.68f, &Name, &Value);
		Ui()->DoLabel(&Name, CTuningParams::Name(s_vTuneIndices[i]), 12.0f, TEXTALIGN_ML);
		TextRender()->TextColor(INFO_LABEL_COLOR);
		Ui()->DoLabel(&Value, aValue, 12.0f, TEXTALIGN_MR);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
	Selected = s_TuningList.DoEnd();
	if(Selected != -1)
		m_SelectedTuning = s_vTuneIndices[Selected];

	if(m_SelectedTuning != -1 && m_SelectedTuning != m_LastSelectedTuning)
	{
		float CurrentValue = 0.0f;
		if(pTuning->Get(m_SelectedTuning, &CurrentValue))
		{
			char aValue[32];
			str_format(aValue, sizeof(aValue), "%.2f", CurrentValue);
			m_TuningValueInput.Set(aValue);
		}
		m_LastSelectedTuning = m_SelectedTuning;
	}

	CUIRect EditorHeader, ButtonsBar;
	Right.HSplitTop(ACTION_LABEL_HEIGHT, &EditorHeader, &Right);
	Ui()->DoLabel(&EditorHeader, Localize("Tuning editor"), 13.0f, TEXTALIGN_ML);
	Right.HSplitTop(ACTION_SPACING, nullptr, &Right);
	Right.HSplitBottom(ACTION_BUTTON_HEIGHT, &Right, &ButtonsBar);

	if(m_SelectedTuning == -1)
	{
		Ui()->DoLabel(&Right, Localize("Select a tuning"), 13.0f, TEXTALIGN_ML);
		return;
	}

	auto RenderInfoBlock = [&](CUIRect &Area, const char *pLabel, const char *pValue, float Height) {
		CUIRect Block, Label, Value;
		Area.HSplitTop(Height, &Block, &Area);
		Area.HSplitTop(ACTION_SPACING, nullptr, &Area);
		Block.Draw(INFO_BLOCK_COLOR, IGraphics::CORNER_ALL, 5.0f);
		Block.VMargin(8.0f, &Block);
		Block.HMargin(4.0f, &Block);
		Block.HSplitTop(11.0f, &Label, &Value);
		TextRender()->TextColor(INFO_LABEL_COLOR);
		Ui()->DoLabel(&Label, pLabel, 10.0f, TEXTALIGN_ML);
		TextRender()->TextColor(ColorRGBA(0.95f, 0.95f, 0.95f, 1.0f));
		SLabelProperties ValueProps;
		ValueProps.m_MaxWidth = maximum(1.0f, Value.w);
		ValueProps.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Value, pValue, 12.0f, TEXTALIGN_ML, ValueProps);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	};

	RenderInfoBlock(Right, Localize("Parameter"), CTuningParams::Name(m_SelectedTuning), INFO_BLOCK_HEIGHT);

	float CurrentValue = 0.0f;
	pTuning->Get(m_SelectedTuning, &CurrentValue);
	char aCurrent[64];
	str_format(aCurrent, sizeof(aCurrent), "%.2f", CurrentValue);
	RenderInfoBlock(Right, Localize("Current"), aCurrent, INFO_BLOCK_HEIGHT);

	// Stretch the new-value block across leftover editor space so the panel doesn't look empty.
	CUIRect ValueBlock, ValueLabel, ValueField;
	ValueBlock = Right;
	if(ValueBlock.h > ACTION_SPACING)
		ValueBlock.HSplitBottom(ACTION_SPACING, &ValueBlock, nullptr);
	ValueBlock.Draw(INFO_BLOCK_COLOR, IGraphics::CORNER_ALL, 5.0f);
	ValueBlock.VMargin(8.0f, &ValueBlock);
	ValueBlock.HMargin(6.0f, &ValueBlock);
	ValueBlock.HSplitTop(12.0f, &ValueLabel, &ValueField);
	ValueField.HSplitTop(LOGIN_ROW_HEIGHT, &ValueField, nullptr);
	TextRender()->TextColor(INFO_LABEL_COLOR);
	Ui()->DoLabel(&ValueLabel, Localize("New value"), 10.0f, TEXTALIGN_ML);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	Ui()->DoEditBox(&m_TuningValueInput, &ValueField, 12.0f);

	CUIRect Apply, Reset, ResetAll;
	ButtonsBar.VSplitMid(&Apply, &ButtonsBar, ACTION_SPACING);
	ButtonsBar.VSplitMid(&Reset, &ResetAll, ACTION_SPACING);

	const bool CanTune = HasCommand("tune", AUTH_FALLBACK_ADMIN);
	const bool CanTuneReset = HasCommand("tune_reset", AUTH_FALLBACK_ADMIN);
	TextRender()->TextColor(ButtonTextColor(CanTune));
	if(GameClient()->m_Menus.DoButton_Menu(&m_TuningApplyButton, Localize("Apply"), CanTune ? 0 : -1, &Apply) && CanTune && !m_TuningValueInput.IsEmpty())
	{
		char aCmd[128];
		str_format(aCmd, sizeof(aCmd), "tune %s %s", CTuningParams::Name(m_SelectedTuning), m_TuningValueInput.GetString());
		Client()->Rcon(aCmd);
	}
	TextRender()->TextColor(ButtonTextColor(CanTuneReset));
	if(GameClient()->m_Menus.DoButton_Menu(&m_TuningResetButton, Localize("Reset"), CanTuneReset ? 0 : -1, &Reset) && CanTuneReset)
	{
		char aCmd[128];
		str_format(aCmd, sizeof(aCmd), "tune_reset %s", CTuningParams::Name(m_SelectedTuning));
		Client()->Rcon(aCmd);
	}
	if(GameClient()->m_Menus.DoButton_Menu(&m_TuningResetAllButton, Localize("Reset all"), CanTuneReset ? 0 : -1, &ResetAll) && CanTuneReset)
		Client()->Rcon("tune_reset");
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

void CAdminPanel::RenderLogs(CUIRect View)
{
	View.Draw(SECTION_COLOR, IGraphics::CORNER_ALL, 6.0f);
	View.Margin(ACTION_BLOCK_MARGIN, &View);

	static CScrollRegion s_LogScroll;
	static vec2 s_LogScrollOffset(0.0f, 0.0f);

	CUIRect Header, ClearButton;
	View.HSplitTop(LOGIN_ROW_HEIGHT, &Header, &View);
	Header.VSplitRight(90.0f, &Header, &ClearButton);
	Ui()->DoLabel(&Header, Localize("RCON log"), 13.0f, TEXTALIGN_ML);
	if(GameClient()->m_Menus.DoButton_Menu(&m_ClearLogsButton, Localize("Clear"), 0, &ClearButton))
	{
		m_RconLogLines.clear();
		m_LogStickToBottom = true;
		m_LogScrollToBottomPending = false;
		s_LogScroll.Reset();
		s_LogScrollOffset = vec2(0.0f, 0.0f);
	}
	View.HSplitTop(6.0f, nullptr, &View);

	if(m_RconLogLines.empty())
	{
		s_LogScroll.Reset();
		s_LogScrollOffset = vec2(0.0f, 0.0f);
		Ui()->DoLabel(&View, Localize("No log entries yet"), 12.0f, TEXTALIGN_ML);
		return;
	}

	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 40.0f;
	ScrollParams.m_ScrollbarThickness = 14.0f;
	ScrollParams.m_ScrollbarMargin = 3.0f;
	ScrollParams.m_ClipBgColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.18f);
	s_LogScroll.Begin(&View, &ScrollParams);
	const float ClipH = View.h;
	const float LineHeight = 15.0f;
	const int NumLines = (int)m_RconLogLines.size();
	int LineIndex = 0;
	for(const SLogLine &Entry : m_RconLogLines)
	{
		CUIRect Row;
		View.HSplitTop(LineHeight, &Row, &View);
		const bool IsLast = ++LineIndex == NumLines;
		if(s_LogScroll.AddRect(Row))
		{
			CUIRect TimeRect, TextRect;
			Row.VSplitLeft(54.0f, &TimeRect, &TextRect);
			TextRender()->TextColor(ColorRGBA(0.62f, 0.62f, 0.62f, 1.0f));
			Ui()->DoLabel(&TimeRect, Entry.m_aTime, 11.0f, TEXTALIGN_ML);
			TextRender()->TextColor(ColorRGBA(0.92f, 0.92f, 0.92f, 1.0f));
			SLabelProperties Props;
			Props.m_MaxWidth = TextRect.w;
			Props.m_EllipsisAtEnd = true;
			Ui()->DoLabel(&TextRect, Entry.m_Text.c_str(), 12.0f, TEXTALIGN_ML, Props);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
		if(IsLast && m_LogScrollToBottomPending)
		{
			s_LogScroll.ScrollHere(CScrollRegion::SCROLLHERE_BOTTOM);
			m_LogScrollToBottomPending = false;
		}
	}

	s_LogScroll.End();

	if(!s_LogScroll.ScrollbarShown())
	{
		m_LogStickToBottom = true;
	}
	else
	{
		// Match CScrollRegion::AddRect content height for the last real line (no phantom spacer).
		const float ContentH = NumLines * LineHeight + 1.0f;
		const float MaxScroll = maximum(0.0f, ContentH - ClipH);
		m_LogStickToBottom = (-s_LogScrollOffset.y) >= MaxScroll - 2.0f;
	}
}

void CAdminPanel::RenderActionPopup(const CUIRect &Screen)
{
	if(m_ActionPopupType == EAction::NONE || !m_pActionPopupSpec)
		return;

	CUIRect Overlay = Screen;
	Overlay.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.42f), IGraphics::CORNER_ALL, 0.0f);
	// Steal HotItem so clicks cannot pass through to buttons under the popup.
	static int s_ActionPopupOverlayId;
	Ui()->DoButtonLogic(&s_ActionPopupOverlayId, -1, &Overlay, BUTTONFLAG_LEFT);

	const bool NeedsPlayer = m_pActionPopupSpec->m_NeedsPlayer && HasPlayer(m_ActionPopupClientId);
	const bool IsMessage = m_pActionPopupSpec->m_Field == EActionField::MESSAGE;
	const bool HasReasonOrMessage = m_pActionPopupSpec->m_Field != EActionField::DURATION_SECONDS;
	const bool HasDuration = m_pActionPopupSpec->m_Field == EActionField::REASON_DURATION_SECONDS ||
				 m_pActionPopupSpec->m_Field == EActionField::REASON_DURATION_MINUTES ||
				 m_pActionPopupSpec->m_Field == EActionField::DURATION_SECONDS;
	const bool HasPresets = HasDuration && m_pActionPopupSpec->m_Field != EActionField::DURATION_SECONDS;

	float ContentH = PANEL_PADDING * 2.0f + HEADER_HEIGHT + 8.0f + LOGIN_ROW_HEIGHT + 4.0f;
	if(NeedsPlayer)
		ContentH += INFO_ROW_HEIGHT + 4.0f;
	if(HasReasonOrMessage)
		ContentH += LOGIN_ROW_HEIGHT + 8.0f;
	if(HasDuration)
		ContentH += LOGIN_ROW_HEIGHT + 8.0f;
	if(HasPresets)
		ContentH += LOGIN_ROW_HEIGHT + 8.0f;

	CUIRect Popup;
	Popup.w = minimum(460.0f, Screen.w * 0.55f);
	Popup.h = ContentH;
	Popup.x = Screen.x + (Screen.w - Popup.w) * 0.5f;
	Popup.y = Screen.y + (Screen.h - Popup.h) * 0.5f;
	Popup.Draw(ColorRGBA(0.08f, 0.08f, 0.08f, 0.94f), IGraphics::CORNER_ALL, 8.0f);
	Popup.Margin(PANEL_PADDING, &Popup);

	CUIRect Buttons;
	Popup.HSplitBottom(LOGIN_ROW_HEIGHT, &Popup, &Buttons);

	CUIRect Header;
	Popup.HSplitTop(HEADER_HEIGHT, &Header, &Popup);
	Ui()->DoLabel(&Header, ActionLabel(m_pActionPopupSpec->m_Action), 16.0f, TEXTALIGN_ML);

	if(NeedsPlayer)
	{
		CUIRect NameRow;
		Popup.HSplitTop(INFO_ROW_HEIGHT, &NameRow, &Popup);
		TextRender()->TextColor(ColorRGBA(0.75f, 0.75f, 0.75f, 1.0f));
		Ui()->DoLabel(&NameRow, GameClient()->m_aClients[m_ActionPopupClientId].m_aName, 12.0f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		Popup.HSplitTop(4.0f, nullptr, &Popup);
	}
	Popup.HSplitTop(8.0f, nullptr, &Popup);

	if(HasReasonOrMessage)
	{
		CUIRect Row, Label, Field;
		Popup.HSplitTop(LOGIN_ROW_HEIGHT, &Row, &Popup);
		Row.VSplitLeft(100.0f, &Label, &Field);
		Ui()->DoLabel(&Label, IsMessage ? Localize("Message") : Localize("Reason"), 12.0f, TEXTALIGN_ML);
		Ui()->DoEditBox(&m_ActionReasonInput, &Field, 12.0f);
		Popup.HSplitTop(8.0f, nullptr, &Popup);
	}

	if(HasDuration)
	{
		CUIRect Row, Label, Field;
		Popup.HSplitTop(LOGIN_ROW_HEIGHT, &Row, &Popup);
		Row.VSplitLeft(100.0f, &Label, &Field);
		Ui()->DoLabel(&Label, m_pActionPopupSpec->m_Field == EActionField::REASON_DURATION_MINUTES ? Localize("Duration (min)") : Localize("Duration (sec)"), 12.0f, TEXTALIGN_ML);
		Ui()->DoEditBox(&m_ActionDurationInput, &Field, 12.0f);
		Popup.HSplitTop(8.0f, nullptr, &Popup);

		if(HasPresets)
		{
			Popup.HSplitTop(LOGIN_ROW_HEIGHT, &Row, &Popup);
			CUIRect Short, Mid, Long;
			const float Gap = 8.0f;
			const float Bw = (Row.w - Gap * 2.0f) / 3.0f;
			Row.VSplitLeft(Bw, &Short, &Row);
			Row.VSplitLeft(Gap, nullptr, &Row);
			Row.VSplitLeft(Bw, &Mid, &Row);
			Row.VSplitLeft(Gap, nullptr, &Row);
			Long = Row;
			const bool Ban = m_pActionPopupSpec->m_Field == EActionField::REASON_DURATION_MINUTES;
			if(GameClient()->m_Menus.DoButton_Menu(&m_ActionPresetShortButton, Ban ? Localize("5 min") : Localize("30 sec"), 0, &Short))
				m_ActionDurationInput.Set(Ban ? "5" : "30");
			if(GameClient()->m_Menus.DoButton_Menu(&m_ActionPresetMidButton, Ban ? Localize("10 min") : Localize("60 sec"), 0, &Mid))
				m_ActionDurationInput.Set(Ban ? "10" : "60");
			if(GameClient()->m_Menus.DoButton_Menu(&m_ActionPresetLongButton, Ban ? Localize("60 min") : Localize("300 sec"), 0, &Long))
				m_ActionDurationInput.Set(Ban ? "60" : "300");
		}
	}

	CUIRect Cancel, Confirm;
	Buttons.VSplitMid(&Cancel, &Confirm, 8.0f);
	if(GameClient()->m_Menus.DoButton_Menu(&m_ActionCancelButton, Localize("Cancel"), 0, &Cancel))
		CloseActionPopup();
	if(GameClient()->m_Menus.DoButton_Menu(&m_ActionConfirmButton, Localize("Apply"), 0, &Confirm))
	{
		if(IsActionEnabled(*m_pActionPopupSpec, m_ActionPopupClientId))
		{
			char aCmd[256];
			if(TryBuildActionCommand(aCmd, sizeof(aCmd)))
				Client()->Rcon(aCmd);
		}
		CloseActionPopup();
	}
}

void CAdminPanel::RenderPanel(const CUIRect &Screen)
{
	const float PanelW = Screen.w * 0.80f;
	const float PanelH = Screen.h * (Client()->RconAuthed() ? 0.78f : 0.56f);
	CUIRect Panel = {(Screen.w - PanelW) / 2.0f, (Screen.h - PanelH) / 2.0f, PanelW, PanelH};
	Panel.Draw(PANEL_COLOR, IGraphics::CORNER_ALL, 8.0f);
	Panel.Margin(PANEL_PADDING, &Panel);

	CUIRect Header, HeaderLeft, HeaderRight;
	Panel.HSplitTop(HEADER_HEIGHT, &Header, &Panel);
	Header.VSplitLeft(Header.w * 0.55f, &HeaderLeft, &HeaderRight);
	Ui()->DoLabel(&HeaderLeft, Localize("Admin Panel"), 17.0f, TEXTALIGN_ML);

	if(Client()->RconAuthed())
	{
		const int LocalAuth = LocalAuthLevel();
		const char *pAuth = LocalAuth == AUTHED_ADMIN ? Localize("Admin") : LocalAuth == AUTHED_MOD ? Localize("Moderator") : LocalAuth == AUTHED_HELPER ? Localize("Helper") : Localize("RCON");
		CUIRect LogoutButton;
		HeaderRight.VSplitRight(100.0f, &HeaderRight, &LogoutButton);
		HeaderRight.VSplitRight(8.0f, &HeaderRight, nullptr);
		Ui()->DoLabel(&HeaderRight, pAuth, 12.0f, TEXTALIGN_MR);
		if(GameClient()->m_Menus.DoButton_Menu(&m_RconLogoutButton, Localize("Logout"), HasCommand("logout", AUTH_FALLBACK_HELPER) ? 0 : -1, &LogoutButton) && HasCommand("logout", AUTH_FALLBACK_HELPER))
			Client()->Rcon("logout");
	}
	else
		Ui()->DoLabel(&HeaderRight, Localize("RCON not authenticated"), 12.0f, TEXTALIGN_MR);

	Panel.HSplitTop(8.0f, nullptr, &Panel);
	if(!Client()->RconAuthed())
	{
		RenderRconLogin(Panel);
		RenderActionPopup(Screen);
		return;
	}

	CUIRect TabBar;
	Panel.HSplitTop(TAB_HEIGHT, &TabBar, &Panel);
	RenderTabs(TabBar);
	Panel.HSplitTop(8.0f, nullptr, &Panel);

	if(m_ActiveTab == ETab::TUNINGS)
		RenderTunings(Panel);
	else if(m_ActiveTab == ETab::LOGS)
		RenderLogs(Panel);
	else
		RenderPlayersTab(Panel);
	RenderActionPopup(Screen);
}

void CAdminPanel::OnRender()
{
	if(!m_Active)
		return;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	Ui()->StartCheck();
	Ui()->Update();
	const CUIRect Screen = *Ui()->Screen();
	Ui()->MapScreen();
	RenderPanel(Screen);
	Ui()->RenderPopupMenus();
	RenderTools()->RenderCursor(Ui()->MousePos(), 24.0f);
	Ui()->FinishCheck();
	Ui()->ClearHotkeys();
}

void CAdminPanel::OnRconLine(const char *pLine)
{
	if(!pLine || pLine[0] == '\0')
		return;

	while(m_RconLogLines.size() >= MAX_LOG_LINES)
		m_RconLogLines.pop_front();

	SLogLine Entry;
	const std::time_t Now = std::time(nullptr);
	std::tm Tm;
#if defined(_WIN32)
	const bool TimeOk = localtime_s(&Tm, &Now) == 0;
#else
	const bool TimeOk = localtime_r(&Now, &Tm) != nullptr;
#endif
	if(TimeOk)
		std::strftime(Entry.m_aTime, sizeof(Entry.m_aTime), "%H:%M:%S", &Tm);
	else
		str_copy(Entry.m_aTime, "??:??:??");

	if(str_length(pLine) > MAX_LOG_LENGTH)
		Entry.m_Text = std::string(pLine, pLine + MAX_LOG_LENGTH);
	else
		Entry.m_Text = pLine;
	m_RconLogLines.push_back(std::move(Entry));
	if(m_LogStickToBottom)
		m_LogScrollToBottomPending = true;
}
