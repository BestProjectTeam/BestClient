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
constexpr float PANEL_PADDING = 12.0f;
constexpr float HEADER_HEIGHT = 28.0f;
constexpr float TAB_HEIGHT = 22.0f;
constexpr float LIST_ROW_HEIGHT = 28.0f;
constexpr float ACTION_BUTTON_HEIGHT = 22.0f;
constexpr float ACTION_SPACING = 6.0f;
constexpr float ACTION_LABEL_HEIGHT = 18.0f;
constexpr float ACTION_BLOCK_MARGIN = 10.0f;
constexpr float INFO_ROW_HEIGHT = 18.0f;
constexpr float LOGIN_ROW_HEIGHT = 24.0f;
constexpr int MAX_LOG_LINES = 200;
constexpr int MAX_LOG_LENGTH = 256;

constexpr ColorRGBA PANEL_COLOR(0.0f, 0.0f, 0.0f, 0.68f);
constexpr ColorRGBA SECTION_COLOR(0.0f, 0.0f, 0.0f, 0.28f);
constexpr ColorRGBA SECTION_DARK_COLOR(0.0f, 0.0f, 0.0f, 0.38f);
constexpr ColorRGBA DISABLED_TEXT_COLOR(0.65f, 0.65f, 0.65f, 0.85f);

const CAdminPanel::SActionSpec s_aActionSpecs[] = {
	{CAdminPanel::EAction::SAY, "say", CAdminPanel::AUTH_FALLBACK_HELPER, false, CAdminPanel::EActionField::MESSAGE},
	{CAdminPanel::EAction::SAY_TEAM, "say_team", CAdminPanel::AUTH_FALLBACK_HELPER, false, CAdminPanel::EActionField::MESSAGE},
	{CAdminPanel::EAction::BROADCAST, "broadcast", CAdminPanel::AUTH_FALLBACK_HELPER, false, CAdminPanel::EActionField::MESSAGE},
	{CAdminPanel::EAction::MUTE, "muteid", CAdminPanel::AUTH_FALLBACK_HELPER, true, CAdminPanel::EActionField::REASON_DURATION_SECONDS},
	{CAdminPanel::EAction::BAN, "ban", CAdminPanel::AUTH_FALLBACK_MOD, true, CAdminPanel::EActionField::REASON_DURATION_MINUTES},
	{CAdminPanel::EAction::KICK, "kick", CAdminPanel::AUTH_FALLBACK_MOD, true, CAdminPanel::EActionField::REASON},
	{CAdminPanel::EAction::RESPAWN, "kill_pl", CAdminPanel::AUTH_FALLBACK_MOD, true, CAdminPanel::EActionField::REASON},
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
		return Console()->GetCommandInfo(pCommand, CFGFLAG_SERVER, true) != nullptr;
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
		str_format(pBuffer, BufferSize, "kill_pl %d %s", m_ActionPopupClientId, m_ActionReasonInput.IsEmpty() ? "Respawned by admin panel" : pReason);
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
	Ui()->DoLabel(&Header, aTitle, 14.0f, TEXTALIGN_ML);
	View.HSplitTop(ACTION_SPACING, nullptr, &View);

	static CScrollRegion s_ActionScroll;
	static vec2 s_ActionScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 30.0f;
	ScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	s_ActionScroll.Begin(&View, &s_ActionScrollOffset, &ScrollParams);
	View.y += s_ActionScrollOffset.y;

	auto DoActionPopupButton = [&](CButtonContainer &Button, const SActionSpec &Spec) {
		CUIRect ButtonRect;
		View.HSplitTop(ACTION_BUTTON_HEIGHT, &ButtonRect, &View);
		const bool Enabled = IsActionEnabled(Spec, m_SelectedClientId);
		if(s_ActionScroll.AddRect(ButtonRect))
		{
			TextRender()->TextColor(ButtonTextColor(Enabled));
			if(GameClient()->m_Menus.DoButton_Menu(&Button, ActionLabel(Spec.m_Action), Enabled ? 0 : -1, &ButtonRect) && Enabled)
				OpenActionPopup(Spec, Spec.m_NeedsPlayer ? m_SelectedClientId : -1);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
		View.HSplitTop(ACTION_SPACING, nullptr, &View);
	};

	DoActionPopupButton(m_SayButton, s_aActionSpecs[0]);
	DoActionPopupButton(m_SayTeamButton, s_aActionSpecs[1]);
	DoActionPopupButton(m_BroadcastButton, s_aActionSpecs[2]);
	DoActionPopupButton(m_MuteButton, s_aActionSpecs[3]);
	DoActionPopupButton(m_BanButton, s_aActionSpecs[4]);
	DoActionPopupButton(m_KickButton, s_aActionSpecs[5]);
	DoActionPopupButton(m_RespawnButton, s_aActionSpecs[6]);
	DoActionPopupButton(m_ForcePauseButton, s_aActionSpecs[7]);

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

	for(int i = 0; i < (int)(sizeof(aDirectActions) / sizeof(aDirectActions[0])); ++i)
	{
		const SDirectAction &Action = aDirectActions[i];
		CUIRect ButtonRect;
		View.HSplitTop(ACTION_BUTTON_HEIGHT, &ButtonRect, &View);
		const bool Enabled = HasPlayer(m_SelectedClientId) && HasCommand(Action.m_pCommand, Action.m_FallbackAuth);
		if(s_ActionScroll.AddRect(ButtonRect))
		{
			TextRender()->TextColor(ButtonTextColor(Enabled));
			const char *pLabel = DirectActionLabel(Action.m_pCommand, Action.m_pButton, &m_VoteMuteButton, &m_TeleportButton, &m_TeleportToPlayerButton, &m_SpectateButton, &m_UnspectateButton);
			if(GameClient()->m_Menus.DoButton_Menu(Action.m_pButton, pLabel, Enabled ? 0 : -1, &ButtonRect) && Enabled)
			{
				char aCmd[128];
				if(Action.m_pButton == &m_VoteMuteButton)
					str_format(aCmd, sizeof(aCmd), "vote_muteid %d 600 Muted by admin panel", m_SelectedClientId);
				else if(Action.m_pButton == &m_TeleportButton)
					str_format(aCmd, sizeof(aCmd), "tele %d %d", m_SelectedClientId, GameClient()->m_Snap.m_LocalClientId);
				else if(Action.m_pButton == &m_TeleportToPlayerButton)
					str_format(aCmd, sizeof(aCmd), "tele %d %d", GameClient()->m_Snap.m_LocalClientId, m_SelectedClientId);
				else if(Action.m_pButton == &m_SpectateButton)
					str_format(aCmd, sizeof(aCmd), "set_team %d -1 0", m_SelectedClientId);
				else if(Action.m_pButton == &m_UnspectateButton)
					str_format(aCmd, sizeof(aCmd), "set_team %d 0 0", m_SelectedClientId);
				else
					str_format(aCmd, sizeof(aCmd), "%s %d", Action.m_pCommand, m_SelectedClientId);
				Client()->Rcon(aCmd);
			}
			TextRender()->TextColor(TextRender()->DefaultTextColor());
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

	auto RenderRow = [&](const char *pLabel, const char *pValue) {
		CUIRect Row, Label, Value;
		View.HSplitTop(INFO_ROW_HEIGHT, &Row, &View);
		Row.VSplitLeft(115.0f, &Label, &Value);
		Value.VMargin(6.0f, &Value);
		SLabelProperties ValueProps;
		ValueProps.m_MaxWidth = Value.w;
		ValueProps.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Label, pLabel, 12.0f, TEXTALIGN_ML);
		Ui()->DoLabel(&Value, pValue, 12.0f, TEXTALIGN_ML, ValueProps);
		View.HSplitTop(2.0f, nullptr, &View);
	};

	char aBuf[256];
	RenderRow(Localize("Name"), ClientData.m_aName);
	RenderRow(Localize("Clan"), ClientData.m_aClan[0] ? ClientData.m_aClan : "-");
	str_format(aBuf, sizeof(aBuf), "%d", ClientId);
	RenderRow(Localize("Client ID"), aBuf);
	RenderRow(Localize("Team"), pInfo->m_Team == TEAM_SPECTATORS ? Localize("Spectators") : Localize("Game"));
	str_format(aBuf, sizeof(aBuf), "%d", pInfo->m_Score);
	RenderRow(Localize("Score"), aBuf);
	str_format(aBuf, sizeof(aBuf), "%d", std::clamp(pInfo->m_Latency, 0, 999));
	RenderRow(Localize("Ping"), aBuf);

	const char *pAuth = Localize("None");
	if(ClientData.m_AuthLevel == AUTHED_ADMIN)
		pAuth = Localize("Admin");
	else if(ClientData.m_AuthLevel == AUTHED_MOD)
		pAuth = Localize("Moderator");
	else if(ClientData.m_AuthLevel == AUTHED_HELPER)
		pAuth = Localize("Helper");
	RenderRow(Localize("Auth"), pAuth);

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
	RenderRow(Localize("Status"), aStatus);
}

void CAdminPanel::RenderPlayerListAndInfo(CUIRect View)
{
	CUIRect List, Info;
	View.HSplitTop(View.h * 0.55f, &List, &Info);
	const CUIRect InfoClip = Info;
	List.Draw(SECTION_DARK_COLOR, IGraphics::CORNER_ALL, 6.0f);
	Info.Draw(SECTION_COLOR, IGraphics::CORNER_ALL, 6.0f);
	List.Margin(ACTION_BLOCK_MARGIN, &List);
	Info.Margin(ACTION_BLOCK_MARGIN, &Info);

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

	if(NumOptions == 0)
	{
		Ui()->DoLabel(&List, Localize("No other players"), 14.0f, TEXTALIGN_ML);
		m_SelectedClientId = -1;
	}
	else
	{
		static CListBox s_ListBox;
		s_ListBox.SetActive(true);
		s_ListBox.DoStart(LIST_ROW_HEIGHT, NumOptions, 1, 6, Selected, &List, false, IGraphics::CORNER_ALL);

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
				Ui()->DoLabel(&NameRect, GameClient()->m_aClients[aPlayerIds[i]].m_aName, 14.0f, TEXTALIGN_ML);
				TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
				TextRender()->TextColor(PlayerAuth == AUTHED_ADMIN ? ColorRGBA(1.0f, 0.7f, 0.2f, 1.0f) : PlayerAuth == AUTHED_MOD ? ColorRGBA(0.4f, 0.8f, 1.0f, 1.0f) : ColorRGBA(0.5f, 1.0f, 0.5f, 1.0f));
				Ui()->DoLabel(&AuthRect, FontIcon::LOCK, AuthRect.h * 0.65f, TEXTALIGN_MC);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
				TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			}
			else
				Ui()->DoLabel(&Label, GameClient()->m_aClients[aPlayerIds[i]].m_aName, 14.0f, TEXTALIGN_ML);
		}

		Selected = s_ListBox.DoEnd();
		if(Selected != -1)
			m_SelectedClientId = aPlayerIds[Selected];
		else if(!HasPlayer(m_SelectedClientId))
			m_SelectedClientId = -1;
	}

	Ui()->ClipEnable(&InfoClip);
	Info.HSplitTop(ACTION_LABEL_HEIGHT, &List, &Info);
	Ui()->DoLabel(&List, Localize("Player info"), 14.0f, TEXTALIGN_ML);
	Info.HSplitTop(ACTION_SPACING, nullptr, &Info);
	RenderPlayerInfo(Info, m_SelectedClientId);
	Ui()->ClipDisable();
}

void CAdminPanel::RenderPlayersTab(CUIRect View)
{
	CUIRect Left, Right;
	View.VSplitMid(&Left, &Right, PANEL_PADDING);
	Left.Draw(SECTION_COLOR, IGraphics::CORNER_ALL, 6.0f);
	Left.Margin(ACTION_BLOCK_MARGIN, &Left);
	RenderPlayerActions(Left);
	RenderPlayerListAndInfo(Right);
}

void CAdminPanel::RenderTunings(CUIRect View)
{
	CUIRect Top, Search, Left, Right;
	View.HSplitTop(ACTION_LABEL_HEIGHT, &Top, &View);
	Ui()->DoLabel(&Top, Localize("Tunings"), 14.0f, TEXTALIGN_ML);
	View.HSplitTop(6.0f, nullptr, &View);
	View.HSplitTop(LOGIN_ROW_HEIGHT, &Search, &View);
	Ui()->DoEditBox_Search(&m_TuningSearchInput, &Search, 12.0f, !Ui()->IsPopupOpen());
	View.HSplitTop(8.0f, nullptr, &View);

	View.VSplitMid(&Left, &Right, PANEL_PADDING);
	Left.Draw(SECTION_COLOR, IGraphics::CORNER_ALL, 6.0f);
	Right.Draw(SECTION_COLOR, IGraphics::CORNER_ALL, 6.0f);
	Left.Margin(ACTION_BLOCK_MARGIN, &Left);
	Right.Margin(ACTION_BLOCK_MARGIN, &Right);

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
		Item.m_Rect.VSplitLeft(Item.m_Rect.w * 0.7f, &Name, &Value);
		Ui()->DoLabel(&Name, CTuningParams::Name(s_vTuneIndices[i]), 12.0f, TEXTALIGN_ML);
		Ui()->DoLabel(&Value, aValue, 12.0f, TEXTALIGN_MR);
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

	if(m_SelectedTuning == -1)
	{
		Ui()->DoLabel(&Right, Localize("Select a tuning"), 14.0f, TEXTALIGN_ML);
		return;
	}

	CUIRect Row, Label, Field;
	Right.HSplitTop(ACTION_LABEL_HEIGHT, &Row, &Right);
	Ui()->DoLabel(&Row, CTuningParams::Name(m_SelectedTuning), 14.0f, TEXTALIGN_ML);
	Right.HSplitTop(8.0f, nullptr, &Right);

	float CurrentValue = 0.0f;
	pTuning->Get(m_SelectedTuning, &CurrentValue);
	char aCurrent[64];
	str_format(aCurrent, sizeof(aCurrent), "%s: %.2f", Localize("Current"), CurrentValue);
	Right.HSplitTop(INFO_ROW_HEIGHT, &Row, &Right);
	Ui()->DoLabel(&Row, aCurrent, 12.0f, TEXTALIGN_ML);
	Right.HSplitTop(8.0f, nullptr, &Right);

	Right.HSplitTop(LOGIN_ROW_HEIGHT, &Row, &Right);
	Row.VSplitLeft(120.0f, &Label, &Field);
	Ui()->DoLabel(&Label, Localize("New value"), 12.0f, TEXTALIGN_ML);
	Ui()->DoEditBox(&m_TuningValueInput, &Field, 12.0f);
	Right.HSplitTop(10.0f, nullptr, &Right);

	CUIRect Buttons, Apply, Reset, ResetAll;
	Right.HSplitTop(LOGIN_ROW_HEIGHT, &Buttons, &Right);
	const float Gap = 6.0f;
	const float ButtonWidth = maximum(60.0f, (Buttons.w - 2.0f * Gap) / 3.0f);
	Buttons.VSplitLeft(ButtonWidth, &Apply, &Buttons);
	Buttons.VSplitLeft(Gap, nullptr, &Buttons);
	Buttons.VSplitLeft(ButtonWidth, &Reset, &Buttons);
	Buttons.VSplitLeft(Gap, nullptr, &Buttons);
	Buttons.VSplitLeft(ButtonWidth, &ResetAll, &Buttons);

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

	CUIRect Header, ClearButton;
	View.HSplitTop(LOGIN_ROW_HEIGHT, &Header, &View);
	Header.VSplitRight(100.0f, &Header, &ClearButton);
	Ui()->DoLabel(&Header, Localize("RCON log"), 14.0f, TEXTALIGN_ML);
	if(GameClient()->m_Menus.DoButton_Menu(&m_ClearLogsButton, Localize("Clear"), 0, &ClearButton))
		m_RconLogLines.clear();
	View.HSplitTop(6.0f, nullptr, &View);

	if(m_RconLogLines.empty())
	{
		Ui()->DoLabel(&View, Localize("No log entries yet"), 12.0f, TEXTALIGN_ML);
		return;
	}

	static CScrollRegion s_LogScroll;
	static vec2 s_LogScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 40.0f;
	ScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	s_LogScroll.Begin(&View, &s_LogScrollOffset, &ScrollParams);
	View.y += s_LogScrollOffset.y;

	for(const SLogLine &Entry : m_RconLogLines)
	{
		CUIRect Row;
		View.HSplitTop(14.0f, &Row, &View);
		if(!s_LogScroll.AddRect(Row))
			continue;
		CUIRect TimeRect, TextRect;
		Row.VSplitLeft(54.0f, &TimeRect, &TextRect);
		TextRender()->TextColor(ColorRGBA(0.62f, 0.62f, 0.62f, 1.0f));
		Ui()->DoLabel(&TimeRect, Entry.m_aTime, 11.0f, TEXTALIGN_ML);
		TextRender()->TextColor(ColorRGBA(0.9f, 0.9f, 0.9f, 1.0f));
		Ui()->DoLabel(&TextRect, Entry.m_Text.c_str(), 12.0f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}

	CUIRect ScrollEnd = {View.x, View.y + 14.0f, View.w, 0.0f};
	s_LogScroll.AddRect(ScrollEnd);
	s_LogScroll.ScrollHere(CScrollRegion::SCROLLHERE_BOTTOM);
	s_LogScroll.End();
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

	CUIRect Popup = Screen;
	Popup.VMargin(Screen.w * 0.28f, &Popup);
	Popup.HMargin(Screen.h * 0.28f, &Popup);
	Popup.Draw(ColorRGBA(0.08f, 0.08f, 0.08f, 0.92f), IGraphics::CORNER_ALL, 8.0f);
	Popup.Margin(PANEL_PADDING, &Popup);

	CUIRect Header;
	Popup.HSplitTop(HEADER_HEIGHT, &Header, &Popup);
	Ui()->DoLabel(&Header, ActionLabel(m_pActionPopupSpec->m_Action), 18.0f, TEXTALIGN_ML);

	if(m_pActionPopupSpec->m_NeedsPlayer && HasPlayer(m_ActionPopupClientId))
	{
		CUIRect NameRow;
		Popup.HSplitTop(INFO_ROW_HEIGHT, &NameRow, &Popup);
		Ui()->DoLabel(&NameRow, GameClient()->m_aClients[m_ActionPopupClientId].m_aName, 12.0f, TEXTALIGN_ML);
	}
	Popup.HSplitTop(8.0f, nullptr, &Popup);

	const bool IsMessage = m_pActionPopupSpec->m_Field == EActionField::MESSAGE;
	if(m_pActionPopupSpec->m_Field != EActionField::DURATION_SECONDS)
	{
		CUIRect Row, Label, Field;
		Popup.HSplitTop(LOGIN_ROW_HEIGHT, &Row, &Popup);
		Row.VSplitLeft(120.0f, &Label, &Field);
		Ui()->DoLabel(&Label, IsMessage ? Localize("Message") : Localize("Reason"), 12.0f, TEXTALIGN_ML);
		Ui()->DoEditBox(&m_ActionReasonInput, &Field, 12.0f);
		Popup.HSplitTop(6.0f, nullptr, &Popup);
	}

	if(m_pActionPopupSpec->m_Field == EActionField::REASON_DURATION_SECONDS ||
		m_pActionPopupSpec->m_Field == EActionField::REASON_DURATION_MINUTES ||
		m_pActionPopupSpec->m_Field == EActionField::DURATION_SECONDS)
	{
		CUIRect Row, Label, Field;
		Popup.HSplitTop(LOGIN_ROW_HEIGHT, &Row, &Popup);
		Row.VSplitLeft(120.0f, &Label, &Field);
		Ui()->DoLabel(&Label, m_pActionPopupSpec->m_Field == EActionField::REASON_DURATION_MINUTES ? Localize("Duration (min)") : Localize("Duration (sec)"), 12.0f, TEXTALIGN_ML);
		Ui()->DoEditBox(&m_ActionDurationInput, &Field, 12.0f);
		Popup.HSplitTop(8.0f, nullptr, &Popup);

		if(m_pActionPopupSpec->m_Field != EActionField::DURATION_SECONDS)
		{
			Popup.HSplitTop(LOGIN_ROW_HEIGHT, &Row, &Popup);
			CUIRect Short, Mid, Long;
			Row.VSplitLeft(120.0f, &Short, &Row);
			Row.VSplitLeft(10.0f, nullptr, &Row);
			Row.VSplitLeft(120.0f, &Mid, &Row);
			Row.VSplitLeft(10.0f, nullptr, &Row);
			Row.VSplitLeft(120.0f, &Long, &Row);
			const bool Ban = m_pActionPopupSpec->m_Field == EActionField::REASON_DURATION_MINUTES;
			if(GameClient()->m_Menus.DoButton_Menu(&m_ActionPresetShortButton, Ban ? Localize("5 min") : Localize("30 sec"), 0, &Short))
				m_ActionDurationInput.Set(Ban ? "5" : "30");
			if(GameClient()->m_Menus.DoButton_Menu(&m_ActionPresetMidButton, Ban ? Localize("10 min") : Localize("60 sec"), 0, &Mid))
				m_ActionDurationInput.Set(Ban ? "10" : "60");
			if(GameClient()->m_Menus.DoButton_Menu(&m_ActionPresetLongButton, Ban ? Localize("60 min") : Localize("300 sec"), 0, &Long))
				m_ActionDurationInput.Set(Ban ? "60" : "300");
			Popup.HSplitTop(8.0f, nullptr, &Popup);
		}
	}

	CUIRect Row, Cancel, Confirm;
	Popup.HSplitTop(LOGIN_ROW_HEIGHT, &Row, &Popup);
	Row.VSplitLeft(Row.w * 0.5f - 5.0f, &Cancel, &Row);
	Row.VSplitLeft(10.0f, nullptr, &Confirm);
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
	const float PanelW = Screen.w * 0.76f;
	const float PanelH = Screen.h * (Client()->RconAuthed() ? 0.72f : 0.58f);
	CUIRect Panel = {(Screen.w - PanelW) / 2.0f, (Screen.h - PanelH) / 2.0f, PanelW, PanelH};
	Panel.Draw(PANEL_COLOR, IGraphics::CORNER_ALL, 8.0f);
	Panel.Margin(PANEL_PADDING, &Panel);

	CUIRect Header, HeaderLeft, HeaderRight;
	Panel.HSplitTop(HEADER_HEIGHT, &Header, &Panel);
	Header.VSplitLeft(Header.w * 0.5f, &HeaderLeft, &HeaderRight);
	Ui()->DoLabel(&HeaderLeft, Localize("Admin Panel"), 18.0f, TEXTALIGN_ML);

	if(Client()->RconAuthed())
	{
		const int LocalAuth = LocalAuthLevel();
		const char *pAuth = LocalAuth == AUTHED_ADMIN ? Localize("Admin") : LocalAuth == AUTHED_MOD ? Localize("Moderator") : LocalAuth == AUTHED_HELPER ? Localize("Helper") : Localize("RCON");
		CUIRect LogoutButton;
		HeaderRight.VSplitRight(110.0f, &HeaderRight, &LogoutButton);
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
}
