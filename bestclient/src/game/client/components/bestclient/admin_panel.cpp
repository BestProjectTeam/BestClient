/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "admin_panel.h"

#include <base/math.h>
#include <base/system.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/font_icons.h>
#include <engine/shared/localization.h>
#include <engine/textrender.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/gamecore.h>
#include <game/localization.h>

#include <cmath>
#include <vector>

namespace
{
constexpr float PANEL_PADDING = 12.0f;
constexpr float HEADER_HEIGHT = 26.0f;
constexpr float TAB_HEIGHT = 22.0f;
constexpr float LIST_ROW_HEIGHT = 28.0f;
constexpr float ACTION_BUTTON_HEIGHT = 22.0f;
constexpr float ACTION_SPACING = 6.0f;
constexpr float ACTION_LABEL_HEIGHT = 18.0f;
constexpr float ACTION_BLOCK_MARGIN = 10.0f;
constexpr float INFO_ROW_HEIGHT = 18.0f;
constexpr float LOGIN_ROW_HEIGHT = 24.0f;
constexpr float ANIM_DURATION = 0.18f;
constexpr float POPUP_ANIM_DURATION = 0.15f;

enum
{
	TAB_PLAYERS = 0,
	TAB_INFO = 1,
	TAB_TUNINGS = 2,
	TAB_FAST_ACTIONS = 3,
	TAB_LOGS = 4,
	TAB_COUNT = 5,
};

enum
{
	ACTION_NONE = 0,
	ACTION_MUTE,
	ACTION_BAN,
	ACTION_KICK,
	ACTION_RESPAWN,
	ACTION_FORCEPAUSE,
	ACTION_SAY,
	ACTION_SAY_TEAM,
	ACTION_BROADCAST,
	ACTION_SETTINGS,
};

float QuadEaseInOut(float t)
{
	if(t == 0.0f)
		return 0.0f;
	if(t == 1.0f)
		return 1.0f;
	return (t < 0.5f) ? (2.0f * t * t) : (1.0f - std::pow(-2.0f * t + 2.0f, 2) / 2.0f);
}

ColorHSLA AdminPanelDoColorPicker(CUi *pUI, CUi::SColorPickerPopupContext &Context, const CUIRect *pRect, unsigned int *pHslaColor, bool Alpha)
{
	ColorHSLA HslaColor = ColorHSLA(*pHslaColor, Alpha);

	ColorRGBA Outline(1.0f, 1.0f, 1.0f, 0.25f);
	Outline.a *= pUI->ButtonColorMul(pHslaColor);

	CUIRect Rect;
	pRect->Margin(3.0f, &Rect);

	pRect->Draw(Outline, IGraphics::CORNER_ALL, 4.0f);
	Rect.Draw(color_cast<ColorRGBA>(HslaColor), IGraphics::CORNER_ALL, 4.0f);

	if(pUI->DoButtonLogic(pHslaColor, 0, pRect, BUTTONFLAG_LEFT))
	{
		Context.m_pHslaColor = pHslaColor;
		Context.m_HslaColor = HslaColor;
		Context.m_HsvaColor = color_cast<ColorHSVA>(HslaColor);
		Context.m_RgbaColor = color_cast<ColorRGBA>(Context.m_HsvaColor);
		Context.m_Alpha = Alpha;
		pUI->ShowPopupColorPicker(pUI->MouseX(), pUI->MouseY(), &Context);
	}
	else if(pUI->IsPopupOpen(&Context) && Context.m_pHslaColor == pHslaColor)
	{
		HslaColor = color_cast<ColorHSLA>(Context.m_HsvaColor);
	}

	return HslaColor;
}

void AdminPanelDoColorLine(CUi *pUI, CMenus *pMenus, CUi::SColorPickerPopupContext &Context, CButtonContainer &ResetButton, CScrollRegion *pScroll, CUIRect *pMainRect, const char *pText, unsigned int *pColorValue, const ColorRGBA &DefaultColor)
{
	CUIRect Section, ColorPickerButton, ResetRect, Label;

	pMainRect->HSplitTop(LOGIN_ROW_HEIGHT, &Section, pMainRect);
	if(pScroll)
		pScroll->AddRect(Section);
	CUIRect Space;
	pMainRect->HSplitTop(4.0f, &Space, pMainRect);
	if(pScroll)
		pScroll->AddRect(Space);

	Section.VSplitRight(60.0f, &Section, &ResetRect);
	Section.VSplitRight(8.0f, &Section, nullptr);
	Section.VSplitRight(Section.h, &Section, &ColorPickerButton);
	Section.VSplitRight(8.0f, &Label, nullptr);

	pUI->DoLabel(&Label, pText, 12.0f, TEXTALIGN_ML);

	AdminPanelDoColorPicker(pUI, Context, &ColorPickerButton, pColorValue, true);

	ResetRect.HMargin(2.0f, &ResetRect);
	if(pMenus->DoButton_Menu(&ResetButton, Localize("Reset"), 0, &ResetRect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.1f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f)))
		*pColorValue = color_cast<ColorHSLA>(DefaultColor).Pack(true);
}

char *GetFastActionSlot(int Index)
{
	switch(Index)
	{
	case 0: return g_Config.m_BcAdminFastAction0;
	case 1: return g_Config.m_BcAdminFastAction1;
	case 2: return g_Config.m_BcAdminFastAction2;
	case 3: return g_Config.m_BcAdminFastAction3;
	case 4: return g_Config.m_BcAdminFastAction4;
	case 5: return g_Config.m_BcAdminFastAction5;
	case 6: return g_Config.m_BcAdminFastAction6;
	case 7: return g_Config.m_BcAdminFastAction7;
	case 8: return g_Config.m_BcAdminFastAction8;
	case 9: return g_Config.m_BcAdminFastAction9;
	default: return nullptr;
	}
}

const char *GetFastActionSlotConst(int Index)
{
	return GetFastActionSlot(Index);
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
	m_ActiveTab = 0;
	m_OpenAnimation = 0.0f;
	m_SizeAnimation = 0.0f;
	m_SelectedTuning = -1;
	m_LastSelectedTuning = -1;
	m_RconLogLines.clear();
	m_ActionPopupType = ACTION_NONE;
	m_ActionPopupClientId = -1;
	m_ActionPopupAnim = 0.0f;
	m_ActionPopupClosing = false;

	m_RconUserInput.Clear();
	m_RconPassInput.Clear();
	m_FastActionInput.Clear();
	m_FastActionSearchInput.Clear();
	m_FastActionEditIndex = -1;
	m_ColorPickerPopupContext.m_ColorMode = CUi::SColorPickerPopupContext::MODE_UNSET;
	m_ColorPickerPopupContext.m_pHslaColor = nullptr;
	m_RconPassInput.SetHidden(true);
	m_RconUserInput.SetEmptyText(Localize("Username (auth_add)"));
	m_RconPassInput.SetEmptyText(Localize("Password"));
	m_TuningSearchInput.SetEmptyText(Localize("Search tunings"));
	m_TuningValueInput.SetEmptyText(Localize("Value"));
	m_FastActionInput.SetEmptyText(Localize("Command"));
	m_FastActionSearchInput.SetEmptyText(Localize("Search commands"));

	m_ActionReasonInput.Clear();
	m_ActionDurationInput.Clear();
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
	if(pSelf->m_Active)
		pSelf->SetActive(false);
	else
		pSelf->SetActive(true);
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
		if(g_Config.m_BcAdminPanelRememberTab)
			m_ActiveTab = minimum(TAB_COUNT - 1, maximum(0, g_Config.m_BcAdminPanelLastTab));
		else
			m_ActiveTab = TAB_PLAYERS;
		m_LastMousePos = Ui()->MousePos();
		SetUiMousePos(Ui()->Screen()->Center());
	}
	else
	{
		if(m_MouseUnlocked)
		{
			Ui()->ClosePopupMenus();
			m_MouseUnlocked = false;
			if(m_LastMousePos.has_value())
				SetUiMousePos(m_LastMousePos.value());
			m_LastMousePos = Ui()->MousePos();
		}
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
	if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_ESCAPE)
	{
		if(m_ActionPopupType != ACTION_NONE)
			CloseActionPopup();
		else
			SetActive(false);
		return true;
	}

	return true;
}

void CAdminPanel::RenderPlayerActions(CUIRect View, int ClientId)
{
	const bool HasPlayer = ClientId >= 0 && ClientId < MAX_CLIENTS && GameClient()->m_Snap.m_apPlayerInfos[ClientId];

	int LocalAuth = AUTHED_NO;
	if(GameClient()->m_Snap.m_LocalClientId >= 0)
		LocalAuth = GameClient()->m_aClients[GameClient()->m_Snap.m_LocalClientId].m_AuthLevel;

	const bool CanMute = HasPlayer && LocalAuth >= AUTHED_HELPER;
	const bool CanKick = HasPlayer && LocalAuth >= AUTHED_MOD;
	const bool CanBan = HasPlayer && LocalAuth >= AUTHED_MOD;
	const bool CanForcePause = HasPlayer && LocalAuth >= AUTHED_MOD;
	const bool CanSpectate = HasPlayer && LocalAuth >= AUTHED_MOD;
	const bool CanTeleport = HasPlayer && LocalAuth >= AUTHED_MOD;
	const bool CanRespawn = HasPlayer && LocalAuth >= AUTHED_MOD;
	const bool CanChat = Client()->RconAuthed();

	char aTitle[128];
	if(HasPlayer)
		str_format(aTitle, sizeof(aTitle), Localize("Actions for %s"), GameClient()->m_aClients[ClientId].m_aName);
	else
		str_copy(aTitle, Localize("Player actions"));
	CUIRect Header;
	View.HSplitTop(ACTION_LABEL_HEIGHT, &Header, &View);
	Ui()->DoLabel(&Header, aTitle, 14.0f, TEXTALIGN_ML);
	View.HSplitTop(ACTION_SPACING, nullptr, &View);

	static CScrollRegion s_ActionScroll;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 30.0f;
	ScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	s_ActionScroll.Begin(&View, &ScrollOffset, &ScrollParams);
	View.y += ScrollOffset.y;

	auto DoActionButton = [&](CButtonContainer &Button, const char *pLabel, const char *pCommand, bool Enabled) {
		CUIRect ButtonRect;
		View.HSplitTop(ACTION_BUTTON_HEIGHT, &ButtonRect, &View);
		if(s_ActionScroll.AddRect(ButtonRect))
		{
			const int Style = Enabled ? 0 : -1;
			if(GameClient()->m_Menus.DoButton_Menu(&Button, pLabel, Style, &ButtonRect))
			{
				if(Enabled && Client()->RconAuthed())
					Client()->Rcon(pCommand);
			}
		}
		View.HSplitTop(ACTION_SPACING, nullptr, &View);
	};

	auto DoPopupButton = [&](CButtonContainer &Button, const char *pLabel, int PopupType, bool Enabled, int PopupClientId) {
		CUIRect ButtonRect;
		View.HSplitTop(ACTION_BUTTON_HEIGHT, &ButtonRect, &View);
		if(s_ActionScroll.AddRect(ButtonRect))
		{
			const int Style = Enabled ? 0 : -1;
			if(GameClient()->m_Menus.DoButton_Menu(&Button, pLabel, Style, &ButtonRect))
			{
				if(Enabled)
					OpenActionPopup(PopupClientId, PopupType);
			}
		}
		View.HSplitTop(ACTION_SPACING, nullptr, &View);
	};

	DoPopupButton(m_SayButton, Localize("Say"), ACTION_SAY, CanChat, -1);
	DoPopupButton(m_SayTeamButton, Localize("Say team"), ACTION_SAY_TEAM, CanChat, -1);
	DoPopupButton(m_BroadcastButton, Localize("Broadcast"), ACTION_BROADCAST, CanChat, -1);

	DoPopupButton(m_MuteButton, Localize("Mute"), ACTION_MUTE, CanMute, ClientId);
	DoPopupButton(m_BanButton, Localize("Ban"), ACTION_BAN, CanBan, ClientId);
	DoPopupButton(m_KickButton, Localize("Kick"), ACTION_KICK, CanKick, ClientId);
	DoPopupButton(m_KillButton, Localize("Respawn"), ACTION_RESPAWN, CanRespawn, ClientId);
	DoPopupButton(m_ForcePauseButton, Localize("Force pause"), ACTION_FORCEPAUSE, CanForcePause, ClientId);

	char aCmd[128];
	str_format(aCmd, sizeof(aCmd), "unmuteid %d", ClientId);
	DoActionButton(m_UnmuteButton, Localize("Unmute"), aCmd, CanMute);

	str_format(aCmd, sizeof(aCmd), "vote_muteid %d 600 Muted by admin panel", ClientId);
	DoActionButton(m_VoteMuteButton, Localize("Vote mute (10 min)"), aCmd, CanMute);

	str_format(aCmd, sizeof(aCmd), "vote_unmuteid %d", ClientId);
	DoActionButton(m_VoteUnmuteButton, Localize("Vote unmute"), aCmd, CanMute);

	str_format(aCmd, sizeof(aCmd), "tele %d %d", ClientId, GameClient()->m_Snap.m_LocalClientId);
	DoActionButton(m_TeleportButton, Localize("Teleport to me"), aCmd, CanTeleport);

	str_format(aCmd, sizeof(aCmd), "tele %d %d", GameClient()->m_Snap.m_LocalClientId, ClientId);
	DoActionButton(m_TeleportToPlayerButton, Localize("Teleport to player"), aCmd, CanTeleport);

	str_format(aCmd, sizeof(aCmd), "force_unpause %d", ClientId);
	DoActionButton(m_ForceUnpauseButton, Localize("Force unpause"), aCmd, CanForcePause);

	str_format(aCmd, sizeof(aCmd), "set_team %d -1 0", ClientId);
	DoActionButton(m_SpectateButton, Localize("Move to spectators"), aCmd, CanSpectate);

	str_format(aCmd, sizeof(aCmd), "set_team %d 0 0", ClientId);
	DoActionButton(m_UnspectateButton, Localize("Return to game"), aCmd, CanSpectate);

	if(!HasPlayer)
	{
		CUIRect Hint;
		View.HSplitTop(ACTION_LABEL_HEIGHT, &Hint, &View);
		if(s_ActionScroll.AddRect(Hint))
			Ui()->DoLabel(&Hint, Localize("Select a player to enable player actions"), 12.0f, TEXTALIGN_ML);
		View.HSplitTop(ACTION_SPACING, nullptr, &View);
	}

	CUIRect ScrollRegion;
	ScrollRegion.x = View.x;
	ScrollRegion.y = View.y + ACTION_SPACING;
	ScrollRegion.w = View.w;
	ScrollRegion.h = 0.0f;
	s_ActionScroll.AddRect(ScrollRegion);
	s_ActionScroll.End();
}

void CAdminPanel::RenderPlayerInfo(CUIRect View, int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !GameClient()->m_Snap.m_apPlayerInfos[ClientId])
	{
		Ui()->DoLabel(&View, Localize("Select a player"), 14.0f, TEXTALIGN_ML);
		return;
	}

	const CNetObj_PlayerInfo *pInfo = GameClient()->m_Snap.m_apPlayerInfos[ClientId];
	const CGameClient::CClientData &ClientData = GameClient()->m_aClients[ClientId];

	auto RenderRow = [&](const char *pLabel, const char *pValue) {
		CUIRect Row, Label, Value;
		View.HSplitTop(INFO_ROW_HEIGHT, &Row, &View);
		Row.VSplitLeft(120.0f, &Label, &Value);
		Ui()->DoLabel(&Label, pLabel, 12.0f, TEXTALIGN_ML);
		Ui()->DoLabel(&Value, pValue, 12.0f, TEXTALIGN_MR);
		View.HSplitTop(2.0f, nullptr, &View);
	};

	char aBuf[128];
	RenderRow(Localize("Name"), ClientData.m_aName);
	RenderRow(Localize("Clan"), ClientData.m_aClan[0] ? ClientData.m_aClan : "-");
	str_format(aBuf, sizeof(aBuf), "%d", ClientData.m_Country);
	RenderRow(Localize("Country"), aBuf);
	RenderRow(Localize("Skin"), ClientData.m_aSkinName);

	str_format(aBuf, sizeof(aBuf), "%d", ClientId);
	RenderRow(Localize("Client ID"), aBuf);

	const char *pTeam = pInfo->m_Team == TEAM_SPECTATORS ? Localize("Spectators") : Localize("Game");
	RenderRow(Localize("Team"), pTeam);

	str_format(aBuf, sizeof(aBuf), "%d", pInfo->m_Score);
	RenderRow(Localize("Score"), aBuf);

	int Ping = pInfo->m_Latency;
	if(Ping < 0)
		Ping = 0;
	else if(Ping > 999)
		Ping = 999;
	str_format(aBuf, sizeof(aBuf), "%d", Ping);
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

void CAdminPanel::RenderPlayerList(CUIRect View)
{
	int NumOptions = 0;
	int Selected = -1;
	int aPlayerIds[MAX_CLIENTS];
	for(const auto &pInfoByName : GameClient()->m_Snap.m_apInfoByName)
	{
		if(!pInfoByName)
			continue;

		const int Index = pInfoByName->m_ClientId;
		if(Index == GameClient()->m_Snap.m_LocalClientId)
			continue;

		if(m_SelectedClientId == Index)
			Selected = NumOptions;

		aPlayerIds[NumOptions] = Index;
		NumOptions++;
	}

	if(NumOptions == 0)
	{
		Ui()->DoLabel(&View, Localize("No other players"), 14.0f, TEXTALIGN_ML);
		m_SelectedClientId = -1;
		return;
	}

	static CListBox s_ListBox;
	s_ListBox.SetActive(true);
	s_ListBox.DoStart(LIST_ROW_HEIGHT, NumOptions, 1, 6, Selected, &View, false, IGraphics::CORNER_ALL);

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
		vec2 TeeRenderPos(TeeRect.x + TeeInfo.m_Size / 2, TeeRect.y + TeeInfo.m_Size / 2 + OffsetToMid.y);

		RenderTools()->RenderTee(pIdleState, &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRenderPos);

		Ui()->DoLabel(&Label, GameClient()->m_aClients[aPlayerIds[i]].m_aName, 14.0f, TEXTALIGN_ML);
	}

	Selected = s_ListBox.DoEnd();
	m_SelectedClientId = Selected != -1 ? aPlayerIds[Selected] : -1;
}

void CAdminPanel::RenderRconLogin(CUIRect View)
{
	const bool UsernameReq = GameClient()->m_GameConsole.RconUsernameRequired();
	CUIRect Box = View;
	Box.VMargin(View.w * 0.2f, &Box);
	Box.HSplitTop(8.0f, nullptr, &Box);

	CUIRect Header;
	Box.HSplitTop(26.0f, &Header, &Box);
	Ui()->DoLabel(&Header, UsernameReq ? Localize("RCON login") : Localize("RCON password"), 16.0f, TEXTALIGN_ML);

	Box.HSplitTop(6.0f, nullptr, &Box);

	CUIRect Row, Label, Field;
	if(UsernameReq)
	{
		Box.HSplitTop(LOGIN_ROW_HEIGHT, &Row, &Box);
		Row.VSplitLeft(120.0f, &Label, &Field);
		Ui()->DoLabel(&Label, Localize("Username"), 12.0f, TEXTALIGN_ML);
		Ui()->DoEditBox(&m_RconUserInput, &Field, 12.0f);

		Box.HSplitTop(6.0f, nullptr, &Box);
	}
	Box.HSplitTop(LOGIN_ROW_HEIGHT, &Row, &Box);
	Row.VSplitLeft(120.0f, &Label, &Field);
	Ui()->DoLabel(&Label, Localize("Password"), 12.0f, TEXTALIGN_ML);
	Ui()->DoEditBox(&m_RconPassInput, &Field, 12.0f);

	Box.HSplitTop(10.0f, nullptr, &Box);
	Box.HSplitTop(LOGIN_ROW_HEIGHT, &Row, &Box);
	Row.VSplitLeft(120.0f, nullptr, &Field);
	Field.VSplitLeft(140.0f, &Field, nullptr);

	bool Submit = GameClient()->m_Menus.DoButton_Menu(&m_RconLoginButton, Localize("Login"), 0, &Field);
	Submit = Submit || (m_RconPassInput.IsActive() && Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER));
	if(Submit && !m_RconPassInput.IsEmpty())
	{
		const char *pUser = UsernameReq ? m_RconUserInput.GetString() : "";
		Client()->RconAuth(pUser, m_RconPassInput.GetString(), g_Config.m_ClDummy);
	}

	Box.HSplitTop(8.0f, nullptr, &Box);
	Ui()->DoLabel(&Box, UsernameReq ? Localize("Server uses auth_add: enter username and password.") : Localize("Server uses password-only rcon."), 12.0f, TEXTALIGN_ML);
}

void CAdminPanel::RenderLogs(CUIRect View)
{
	const float Anim = QuadEaseInOut(m_OpenAnimation);
	View.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f * Anim), IGraphics::CORNER_ALL, 6.0f);
	View.Margin(ACTION_BLOCK_MARGIN, &View);

	CUIRect Header;
	View.HSplitTop(ACTION_LABEL_HEIGHT, &Header, &View);
	Ui()->DoLabel(&Header, Localize("RCON log"), 14.0f, TEXTALIGN_ML);
	View.HSplitTop(4.0f, nullptr, &View);

	const float LineHeight = 14.0f;

	auto LogColor = [](const char *pLine) -> ColorRGBA {
		if(str_find_nocase(pLine, "error") || str_find_nocase(pLine, "ошиб") || str_find_nocase(pLine, "fail"))
			return ColorRGBA(1.0f, 0.25f, 0.25f, 1.0f);
		if(str_find_nocase(pLine, "warn") || str_find_nocase(pLine, "предуп"))
			return ColorRGBA(1.0f, 0.85f, 0.2f, 1.0f);
		return ColorRGBA(0.9f, 0.9f, 0.9f, 1.0f);
	};

	if(m_RconLogLines.empty())
	{
		Ui()->DoLabel(&View, Localize("No log entries yet"), 12.0f, TEXTALIGN_ML);
		return;
	}

	static CScrollRegion s_LogScroll;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 40.0f;
	ScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	s_LogScroll.Begin(&View, &ScrollOffset, &ScrollParams);
	View.y += ScrollOffset.y;

	for(const std::string &Line : m_RconLogLines)
	{
		CUIRect Row;
		View.HSplitTop(LineHeight, &Row, &View);
		if(!s_LogScroll.AddRect(Row))
			continue;
		TextRender()->TextColor(LogColor(Line.c_str()));
		Ui()->DoLabel(&Row, Line.c_str(), 12.0f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}

	CUIRect ScrollRegion;
	ScrollRegion.x = View.x;
	ScrollRegion.y = View.y + LineHeight;
	ScrollRegion.w = View.w;
	ScrollRegion.h = 0.0f;
	s_LogScroll.AddRect(ScrollRegion);
if(g_Config.m_BcAdminPanelAutoScroll)
		s_LogScroll.ScrollHere(CScrollRegion::SCROLLHERE_BOTTOM);
	s_LogScroll.End();
}

void CAdminPanel::RenderFastActions(CUIRect View)
{
	CUIRect Header;
	View.HSplitTop(ACTION_LABEL_HEIGHT, &Header, &View);
	Ui()->DoLabel(&Header, Localize("Fast actions"), 14.0f, TEXTALIGN_ML);
	View.HSplitTop(6.0f, nullptr, &View);

	CUIRect Search;
	View.HSplitTop(LOGIN_ROW_HEIGHT, &Search, &View);
	Ui()->DoEditBox_Search(&m_FastActionSearchInput, &Search, 12.0f, !Ui()->IsPopupOpen());
	View.HSplitTop(6.0f, nullptr, &View);

	CUIRect AddRow, Input, AddButton;
	View.HSplitTop(LOGIN_ROW_HEIGHT, &AddRow, &View);
	AddRow.VSplitRight(60.0f, &Input, &AddButton);
	Input.VSplitRight(6.0f, &Input, nullptr);
	Ui()->DoEditBox(&m_FastActionInput, &Input, 12.0f);

	const bool IsEditing = m_FastActionEditIndex >= 0 && m_FastActionEditIndex < MAX_FAST_ACTIONS;
	bool Add = false;
	if(IsEditing)
		Add = GameClient()->m_Menus.DoButton_Menu(&m_FastActionAddButton, Localize("Save"), 0, &AddButton);
	else
	{
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		Add = GameClient()->m_Menus.DoButton_Menu(&m_FastActionAddButton, FontIcon::PLUS, 0, &AddButton);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	}
	Add = Add || (m_FastActionInput.IsActive() && Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER));

	if(Add && !m_FastActionInput.IsEmpty())
	{
		const char *pCmd = str_skip_whitespaces_const(m_FastActionInput.GetString());
		if(pCmd[0] != '\0')
		{
			if(IsEditing)
			{
				char *pSlot = GetFastActionSlot(m_FastActionEditIndex);
				if(pSlot)
					str_copy(pSlot, pCmd, sizeof(g_Config.m_BcAdminFastAction0));
				m_FastActionEditIndex = -1;
				m_FastActionInput.Clear();
			}
			else
			{
				for(int i = 0; i < MAX_FAST_ACTIONS; i++)
				{
					char *pSlot = GetFastActionSlot(i);
					if(pSlot && pSlot[0] == '\0')
					{
						str_copy(pSlot, pCmd, sizeof(g_Config.m_BcAdminFastAction0));
						m_FastActionInput.Clear();
						break;
					}
				}
			}
		}
	}

	View.HSplitTop(8.0f, nullptr, &View);

	const char *pSearch = m_FastActionSearchInput.GetString();
	int TotalActions = 0;
	int VisibleActions = 0;
	for(int i = 0; i < MAX_FAST_ACTIONS; i++)
	{
		const char *pCmd = GetFastActionSlotConst(i);
		if(pCmd && pCmd[0] != '\0')
		{
			TotalActions++;
			if(pSearch[0] == '\0' || str_find_nocase(pCmd, pSearch))
				VisibleActions++;
		}
	}

	if(TotalActions == 0)
	{
		Ui()->DoLabel(&View, Localize("No fast actions yet"), 12.0f, TEXTALIGN_ML);
		return;
	}
	if(VisibleActions == 0)
	{
		Ui()->DoLabel(&View, Localize("No matches"), 12.0f, TEXTALIGN_ML);
		return;
	}

	static CScrollRegion s_FastScroll;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 30.0f;
	ScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	s_FastScroll.Begin(&View, &ScrollOffset, &ScrollParams);
	View.y += ScrollOffset.y;

	for(int i = 0; i < MAX_FAST_ACTIONS; i++)
	{
		char *pCmd = GetFastActionSlot(i);
		if(!pCmd || pCmd[0] == '\0')
			continue;
		if(pSearch[0] != '\0' && !str_find_nocase(pCmd, pSearch))
			continue;

		CUIRect Row;
		View.HSplitTop(ACTION_BUTTON_HEIGHT, &Row, &View);
		if(s_FastScroll.AddRect(Row))
		{
			CUIRect Run, Edit, Remove;
			Row.VSplitRight(Row.h, &Row, &Remove);
			Row.VSplitRight(6.0f, &Row, nullptr);
			Row.VSplitRight(Row.h, &Run, &Edit);
			Run.VSplitRight(6.0f, &Run, nullptr);

			if(GameClient()->m_Menus.DoButton_Menu(&m_FastActionRunButtons[i], pCmd, 0, &Run))
			{
				if(Client()->RconAuthed())
					Client()->Rcon(pCmd);
			}

			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			if(GameClient()->m_Menus.DoButton_Menu(&m_FastActionEditButtons[i], FontIcon::PEN_TO_SQUARE, 0, &Edit))
			{
				m_FastActionEditIndex = i;
				m_FastActionInput.Set(pCmd);
			}
			if(GameClient()->m_Menus.DoButton_Menu(&m_FastActionRemoveButtons[i], FontIcon::TRASH, 0, &Remove))
			{
				pCmd[0] = '\0';
				if(m_FastActionEditIndex == i)
				{
					m_FastActionEditIndex = -1;
					m_FastActionInput.Clear();
				}
			}
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		}
		View.HSplitTop(ACTION_SPACING, nullptr, &View);
	}

	CUIRect ScrollRegion;
	ScrollRegion.x = View.x;
	ScrollRegion.y = View.y + ACTION_SPACING;
	ScrollRegion.w = View.w;
	ScrollRegion.h = 0.0f;
	s_FastScroll.AddRect(ScrollRegion);
	s_FastScroll.End();
}

void CAdminPanel::OnRconLine(const char *pLine)
{
	if(pLine == nullptr || pLine[0] == '\0')
		return;

	constexpr int MAX_LOG_LENGTH = 256;

if(m_RconLogLines.size() >= (size_t)g_Config.m_BcAdminPanelLogLines)
		while(m_RconLogLines.size() >= (size_t)g_Config.m_BcAdminPanelLogLines)
			m_RconLogLines.pop_front();

	if(str_length(pLine) > MAX_LOG_LENGTH)
		m_RconLogLines.emplace_back(std::string(pLine, pLine + MAX_LOG_LENGTH));
	else
		m_RconLogLines.emplace_back(pLine);
}

void CAdminPanel::OpenActionPopup(int ClientId, int ActionType)
{
	const bool NeedsClient = ActionType != ACTION_SETTINGS &&
		ActionType != ACTION_SAY &&
		ActionType != ACTION_SAY_TEAM &&
		ActionType != ACTION_BROADCAST;
	if(NeedsClient && (ClientId < 0 || ClientId >= MAX_CLIENTS))
		return;

	m_ActionPopupType = ActionType;
	m_ActionPopupClientId = ClientId;
	m_ActionPopupClosing = false;
	m_ActionPopupAnim = 0.0f;

	m_ActionReasonInput.Clear();
	m_ActionDurationInput.Clear();

	if(ActionType == ACTION_MUTE)
	{
		m_ActionReasonInput.Set(Localize("Muted by admin panel"));
		m_ActionDurationInput.Set("600");
		m_ActionDurationInput.SetEmptyText(Localize("Seconds"));
	}
	else if(ActionType == ACTION_BAN)
	{
		m_ActionReasonInput.Set(Localize("Banned by admin panel"));
		m_ActionDurationInput.Set("10");
		m_ActionDurationInput.SetEmptyText(Localize("Minutes"));
	}
	else if(ActionType == ACTION_KICK)
	{
		m_ActionReasonInput.Set(Localize("Kicked by admin panel"));
		m_ActionDurationInput.SetEmptyText(Localize("Duration"));
	}
	else if(ActionType == ACTION_RESPAWN)
	{
		m_ActionReasonInput.Set(Localize("Respawned by admin panel"));
		m_ActionDurationInput.SetEmptyText(Localize("Duration"));
	}
	else if(ActionType == ACTION_FORCEPAUSE)
	{
		m_ActionReasonInput.Clear();
		m_ActionDurationInput.Set("30");
		m_ActionDurationInput.SetEmptyText(Localize("Seconds"));
	}
	else if(ActionType == ACTION_SAY || ActionType == ACTION_SAY_TEAM || ActionType == ACTION_BROADCAST)
	{
		m_ActionReasonInput.Clear();
		m_ActionReasonInput.SetEmptyText(Localize("Message"));
		m_ActionDurationInput.Clear();
		m_ActionDurationInput.SetEmptyText(Localize("Duration"));
	}
}

void CAdminPanel::CloseActionPopup()
{
	if(m_ActionPopupType == ACTION_NONE)
		return;
	m_ActionPopupClosing = true;
}

void CAdminPanel::RenderActionPopup(const CUIRect &Screen)
{
	if(m_ActionPopupType == ACTION_NONE && m_ActionPopupAnim <= 0.0f)
		return;

	const float Target = (m_ActionPopupType != ACTION_NONE && !m_ActionPopupClosing) ? 1.0f : 0.0f;
	const float Step = Client()->RenderFrameTime() / POPUP_ANIM_DURATION;
	if(Target > m_ActionPopupAnim)
		m_ActionPopupAnim = minimum(1.0f, m_ActionPopupAnim + Step);
	else
		m_ActionPopupAnim = maximum(0.0f, m_ActionPopupAnim - Step);

	if(m_ActionPopupAnim <= 0.0f)
	{
		if(m_ActionPopupClosing)
		{
			m_ActionPopupType = ACTION_NONE;
			m_ActionPopupClientId = -1;
			m_ActionPopupClosing = false;
		}
		return;
	}

	const float Anim = QuadEaseInOut(m_ActionPopupAnim);

	CUIRect Overlay = Screen;
	Overlay.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.35f * Anim), IGraphics::CORNER_ALL, 8.0f);

	CUIRect Popup = Screen;
	Popup.VMargin(Screen.w * 0.28f, &Popup);
	Popup.HMargin(Screen.h * 0.28f, &Popup);
	Popup.y += (1.0f - Anim) * 20.0f;
	Popup.Draw(ColorRGBA(0.1f, 0.1f, 0.1f, 0.85f * Anim), IGraphics::CORNER_ALL, 8.0f);
	Popup.Margin(PANEL_PADDING, &Popup);

	const char *pTitle = "";
	if(m_ActionPopupType == ACTION_MUTE)
		pTitle = Localize("Mute player");
	else if(m_ActionPopupType == ACTION_BAN)
		pTitle = Localize("Ban player");
	else if(m_ActionPopupType == ACTION_KICK)
		pTitle = Localize("Kick player");
	else if(m_ActionPopupType == ACTION_RESPAWN)
		pTitle = Localize("Respawn player");
	else if(m_ActionPopupType == ACTION_FORCEPAUSE)
		pTitle = Localize("Force pause player");
	else if(m_ActionPopupType == ACTION_SAY)
		pTitle = Localize("Say message");
	else if(m_ActionPopupType == ACTION_SAY_TEAM)
		pTitle = Localize("Say team message");
	else if(m_ActionPopupType == ACTION_BROADCAST)
		pTitle = Localize("Broadcast message");
	else if(m_ActionPopupType == ACTION_SETTINGS)
		pTitle = Localize("Admin panel settings");

	CUIRect Header;
	Popup.HSplitTop(HEADER_HEIGHT, &Header, &Popup);
	Ui()->DoLabel(&Header, pTitle, 18.0f, TEXTALIGN_ML);

	const bool NeedsClient = m_ActionPopupType != ACTION_SETTINGS &&
		m_ActionPopupType != ACTION_SAY &&
		m_ActionPopupType != ACTION_SAY_TEAM &&
		m_ActionPopupType != ACTION_BROADCAST;
	if(NeedsClient && m_ActionPopupClientId >= 0 && m_ActionPopupClientId < MAX_CLIENTS)
	{
		CUIRect NameRow;
		Popup.HSplitTop(18.0f, &NameRow, &Popup);
		Ui()->DoLabel(&NameRow, GameClient()->m_aClients[m_ActionPopupClientId].m_aName, 12.0f, TEXTALIGN_ML);
	}

	Popup.HSplitTop(8.0f, nullptr, &Popup);

	if(m_ActionPopupType == ACTION_SETTINGS)
	{
		CUIRect Footer;
		Popup.HSplitBottom(LOGIN_ROW_HEIGHT, &Popup, &Footer);
		if(GameClient()->m_Menus.DoButton_Menu(&m_ActionCancelButton, Localize("Close"), 0, &Footer))
			CloseActionPopup();

		static CScrollRegion s_SettingsScroll;
		vec2 ScrollOffset(0.0f, 0.0f);
		CScrollRegionParams ScrollParams;
		ScrollParams.m_ScrollUnit = 30.0f;
		ScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
		s_SettingsScroll.Begin(&Popup, &ScrollOffset, &ScrollParams);
		Popup.y += ScrollOffset.y;

		auto AddRow = [&](float Height, CUIRect &Row) {
			Popup.HSplitTop(Height, &Row, &Popup);
			s_SettingsScroll.AddRect(Row);
		};
		auto AddSpacing = [&](float Height) {
			CUIRect Space;
			Popup.HSplitTop(Height, &Space, &Popup);
			s_SettingsScroll.AddRect(Space);
		};

		CUIRect Row;
		AddRow(LOGIN_ROW_HEIGHT, Row);
		if(GameClient()->m_Menus.DoButton_CheckBox(&g_Config.m_BcAdminPanelAutoScroll, Localize("Auto-scroll logs"), g_Config.m_BcAdminPanelAutoScroll, &Row))
			g_Config.m_BcAdminPanelAutoScroll ^= 1;

		AddSpacing(6.0f);
		AddRow(LOGIN_ROW_HEIGHT, Row);
		if(GameClient()->m_Menus.DoButton_CheckBox(&g_Config.m_BcAdminPanelRememberTab, Localize("Remember last tab"), g_Config.m_BcAdminPanelRememberTab, &Row))
			g_Config.m_BcAdminPanelRememberTab ^= 1;

		AddSpacing(6.0f);
		AddRow(LOGIN_ROW_HEIGHT, Row);
		if(GameClient()->m_Menus.DoButton_CheckBox(&g_Config.m_BcAdminPanelDisableAnim, Localize("Disable animations"), g_Config.m_BcAdminPanelDisableAnim, &Row))
			g_Config.m_BcAdminPanelDisableAnim ^= 1;

		AddSpacing(6.0f);
		AddRow(LOGIN_ROW_HEIGHT, Row);
		Ui()->DoScrollbarOption(&g_Config.m_BcAdminPanelScale, &g_Config.m_BcAdminPanelScale, &Row, Localize("Panel scale"), 80, 120, &CUi::ms_LinearScrollbarScale, 0u, "%");

		AddSpacing(6.0f);
		AddRow(LOGIN_ROW_HEIGHT, Row);
		Ui()->DoScrollbarOption(&g_Config.m_BcAdminPanelLogLines, &g_Config.m_BcAdminPanelLogLines, &Row, Localize("Log lines"), 50, 500);

		AddSpacing(8.0f);
		static CButtonContainer s_PanelBgColorReset;
		static CButtonContainer s_TabInactiveColorReset;
		static CButtonContainer s_TabActiveColorReset;
		static CButtonContainer s_TabHoverColorReset;
		AdminPanelDoColorLine(Ui(), &GameClient()->m_Menus, m_ColorPickerPopupContext, s_PanelBgColorReset, &s_SettingsScroll, &Popup, Localize("Panel background"), &g_Config.m_BcAdminPanelBgColor, ColorRGBA(0.0f, 0.0f, 0.0f, 0.55f));
		AdminPanelDoColorLine(Ui(), &GameClient()->m_Menus, m_ColorPickerPopupContext, s_TabInactiveColorReset, &s_SettingsScroll, &Popup, Localize("Tab inactive"), &g_Config.m_BcAdminPanelTabInactiveColor, ColorRGBA(0.18f, 0.18f, 0.18f, 0.8f));
		AdminPanelDoColorLine(Ui(), &GameClient()->m_Menus, m_ColorPickerPopupContext, s_TabActiveColorReset, &s_SettingsScroll, &Popup, Localize("Tab active"), &g_Config.m_BcAdminPanelTabActiveColor, ColorRGBA(0.32f, 0.32f, 0.32f, 0.9f));
		AdminPanelDoColorLine(Ui(), &GameClient()->m_Menus, m_ColorPickerPopupContext, s_TabHoverColorReset, &s_SettingsScroll, &Popup, Localize("Tab hover"), &g_Config.m_BcAdminPanelTabHoverColor, ColorRGBA(0.24f, 0.24f, 0.24f, 0.9f));

		AddSpacing(6.0f);
		AddRow(LOGIN_ROW_HEIGHT, Row);
		CUIRect ClearBtn;
		Row.VSplitLeft(160.0f, &ClearBtn, &Row);
		static CButtonContainer s_ClearLogsButton;
		if(GameClient()->m_Menus.DoButton_Menu(&s_ClearLogsButton, Localize("Clear logs"), 0, &ClearBtn))
			m_RconLogLines.clear();

		CUIRect ScrollRegion;
		ScrollRegion.x = Popup.x;
		ScrollRegion.y = Popup.y + 6.0f;
		ScrollRegion.w = Popup.w;
		ScrollRegion.h = 0.0f;
		s_SettingsScroll.AddRect(ScrollRegion);
		s_SettingsScroll.End();
		return;
	}

	const bool IsMessageAction = m_ActionPopupType == ACTION_SAY || m_ActionPopupType == ACTION_SAY_TEAM || m_ActionPopupType == ACTION_BROADCAST;
	CUIRect Row, Label, Field;
	Popup.HSplitTop(LOGIN_ROW_HEIGHT, &Row, &Popup);
	Row.VSplitLeft(120.0f, &Label, &Field);
	Ui()->DoLabel(&Label, IsMessageAction ? Localize("Message") : Localize("Reason"), 12.0f, TEXTALIGN_ML);
	Ui()->DoEditBox(&m_ActionReasonInput, &Field, 12.0f);

	if(m_ActionPopupType == ACTION_MUTE || m_ActionPopupType == ACTION_BAN || m_ActionPopupType == ACTION_FORCEPAUSE)
	{
		Popup.HSplitTop(6.0f, nullptr, &Popup);
		Popup.HSplitTop(LOGIN_ROW_HEIGHT, &Row, &Popup);
		Row.VSplitLeft(120.0f, &Label, &Field);
		const char *pDurationLabel = Localize("Duration (sec)");
		if(m_ActionPopupType == ACTION_BAN)
			pDurationLabel = Localize("Duration (min)");
		Ui()->DoLabel(&Label, pDurationLabel, 12.0f, TEXTALIGN_ML);
		Ui()->DoEditBox(&m_ActionDurationInput, &Field, 12.0f);

		if(m_ActionPopupType != ACTION_FORCEPAUSE)
		{
			Popup.HSplitTop(8.0f, nullptr, &Popup);
			Popup.HSplitTop(LOGIN_ROW_HEIGHT, &Row, &Popup);
			CUIRect Short, Mid, Long;
			Row.VSplitLeft(120.0f, &Short, &Row);
			Row.VSplitLeft(10.0f, nullptr, &Row);
			Row.VSplitLeft(120.0f, &Mid, &Row);
			Row.VSplitLeft(10.0f, nullptr, &Row);
			Row.VSplitLeft(120.0f, &Long, &Row);

			if(GameClient()->m_Menus.DoButton_Menu(&m_ActionPresetShortButton, m_ActionPopupType == ACTION_BAN ? Localize("5 min") : Localize("30 sec"), 0, &Short))
				m_ActionDurationInput.Set(m_ActionPopupType == ACTION_BAN ? "5" : "30");
			if(GameClient()->m_Menus.DoButton_Menu(&m_ActionPresetMidButton, m_ActionPopupType == ACTION_BAN ? Localize("10 min") : Localize("60 sec"), 0, &Mid))
				m_ActionDurationInput.Set(m_ActionPopupType == ACTION_BAN ? "10" : "60");
			if(GameClient()->m_Menus.DoButton_Menu(&m_ActionPresetLongButton, m_ActionPopupType == ACTION_BAN ? Localize("60 min") : Localize("300 sec"), 0, &Long))
				m_ActionDurationInput.Set(m_ActionPopupType == ACTION_BAN ? "60" : "300");
		}
	}

	Popup.HSplitTop(10.0f, nullptr, &Popup);
	Popup.HSplitTop(LOGIN_ROW_HEIGHT, &Row, &Popup);
	CUIRect Cancel, Confirm;
	Row.VSplitLeft(Row.w * 0.5f - 5.0f, &Cancel, &Row);
	Row.VSplitLeft(10.0f, nullptr, &Row);
	Confirm = Row;

	if(GameClient()->m_Menus.DoButton_Menu(&m_ActionCancelButton, Localize("Cancel"), 0, &Cancel))
		CloseActionPopup();

	if(GameClient()->m_Menus.DoButton_Menu(&m_ActionConfirmButton, Localize("Apply"), 0, &Confirm))
	{
		const bool CanApply = Client()->RconAuthed() && (m_ActionPopupClientId >= 0 || IsMessageAction);
		if(CanApply)
		{
			char aCmd[256];
			if(m_ActionPopupType == ACTION_MUTE)
			{
				const char *pReason = m_ActionReasonInput.IsEmpty() ? "Muted by admin panel" : m_ActionReasonInput.GetString();
				const char *pDuration = m_ActionDurationInput.IsEmpty() ? "600" : m_ActionDurationInput.GetString();
				str_format(aCmd, sizeof(aCmd), "muteid %d %s %s", m_ActionPopupClientId, pDuration, pReason);
				Client()->Rcon(aCmd);
			}
			else if(m_ActionPopupType == ACTION_BAN)
			{
				const char *pReason = m_ActionReasonInput.IsEmpty() ? "Banned by admin panel" : m_ActionReasonInput.GetString();
				const char *pDuration = m_ActionDurationInput.IsEmpty() ? "10" : m_ActionDurationInput.GetString();
				str_format(aCmd, sizeof(aCmd), "ban %d %s %s", m_ActionPopupClientId, pDuration, pReason);
				Client()->Rcon(aCmd);
			}
			else if(m_ActionPopupType == ACTION_KICK)
			{
				const char *pReason = m_ActionReasonInput.IsEmpty() ? "Kicked by admin panel" : m_ActionReasonInput.GetString();
				str_format(aCmd, sizeof(aCmd), "kick %d %s", m_ActionPopupClientId, pReason);
				Client()->Rcon(aCmd);
			}
			else if(m_ActionPopupType == ACTION_RESPAWN)
			{
				const char *pReason = m_ActionReasonInput.IsEmpty() ? "Respawned by admin panel" : m_ActionReasonInput.GetString();
				str_format(aCmd, sizeof(aCmd), "kill_pl %d %s", m_ActionPopupClientId, pReason);
				Client()->Rcon(aCmd);
			}
			else if(m_ActionPopupType == ACTION_FORCEPAUSE)
			{
				const char *pDuration = m_ActionDurationInput.IsEmpty() ? "30" : m_ActionDurationInput.GetString();
				str_format(aCmd, sizeof(aCmd), "force_pause %d %s", m_ActionPopupClientId, pDuration);
				Client()->Rcon(aCmd);
			}
			else if(m_ActionPopupType == ACTION_SAY)
			{
				if(!m_ActionReasonInput.IsEmpty())
				{
					str_format(aCmd, sizeof(aCmd), "say %s", m_ActionReasonInput.GetString());
					Client()->Rcon(aCmd);
				}
			}
			else if(m_ActionPopupType == ACTION_SAY_TEAM)
			{
				if(!m_ActionReasonInput.IsEmpty())
				{
					str_format(aCmd, sizeof(aCmd), "say_team %s", m_ActionReasonInput.GetString());
					Client()->Rcon(aCmd);
				}
			}
			else if(m_ActionPopupType == ACTION_BROADCAST)
			{
				if(!m_ActionReasonInput.IsEmpty())
				{
					str_format(aCmd, sizeof(aCmd), "broadcast %s", m_ActionReasonInput.GetString());
					Client()->Rcon(aCmd);
				}
			}
		}
		CloseActionPopup();
	}
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

	Left.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 6.0f);
	Right.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.20f), IGraphics::CORNER_ALL, 6.0f);

	CUIRect LeftInner = Left;
	CUIRect RightInner = Right;
	LeftInner.Margin(ACTION_BLOCK_MARGIN, &LeftInner);
	RightInner.Margin(ACTION_BLOCK_MARGIN, &RightInner);

	static std::vector<int> s_vTuneIndices;
	s_vTuneIndices.clear();
	s_vTuneIndices.reserve(CTuningParams::Num());
	const char *pSearch = m_TuningSearchInput.GetString();
	for(int i = 0; i < CTuningParams::Num(); i++)
	{
		const char *pName = CTuningParams::Name(i);
		if(pSearch[0] != '\0' && str_find_nocase(pName, pSearch) == nullptr)
			continue;
		s_vTuneIndices.push_back(i);
	}
	const int NumOptions = static_cast<int>(s_vTuneIndices.size());

	int Selected = -1;
	for(int i = 0; i < NumOptions; i++)
	{
		if(s_vTuneIndices[i] == m_SelectedTuning)
		{
			Selected = i;
			break;
		}
	}
	if(Selected == -1 && NumOptions > 0)
	{
		m_SelectedTuning = s_vTuneIndices[0];
		Selected = 0;
	}

	static CListBox s_TuningList;
	s_TuningList.SetActive(true);
	s_TuningList.DoStart(LIST_ROW_HEIGHT, NumOptions, 1, 6, Selected, &LeftInner, false, IGraphics::CORNER_ALL);

	const CTuningParams *pTuning = GameClient()->GetTuning(0);
	for(int i = 0; i < NumOptions; i++)
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
	else if(NumOptions > 0 && m_SelectedTuning == -1)
		m_SelectedTuning = s_vTuneIndices[0];

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
		Ui()->DoLabel(&RightInner, Localize("Select a tuning"), 14.0f, TEXTALIGN_ML);
		return;
	}

	CUIRect Row, Label, Field, Buttons;
	RightInner.HSplitTop(ACTION_LABEL_HEIGHT, &Row, &RightInner);
	Ui()->DoLabel(&Row, CTuningParams::Name(m_SelectedTuning), 14.0f, TEXTALIGN_ML);
	RightInner.HSplitTop(6.0f, nullptr, &RightInner);

	float CurrentValue = 0.0f;
	pTuning->Get(m_SelectedTuning, &CurrentValue);
	char aCurrent[64];
	str_format(aCurrent, sizeof(aCurrent), "%s: %.2f", Localize("Current"), CurrentValue);
	RightInner.HSplitTop(INFO_ROW_HEIGHT, &Row, &RightInner);
	Ui()->DoLabel(&Row, aCurrent, 12.0f, TEXTALIGN_ML);
	RightInner.HSplitTop(8.0f, nullptr, &RightInner);

	RightInner.HSplitTop(LOGIN_ROW_HEIGHT, &Row, &RightInner);
	Row.VSplitLeft(120.0f, &Label, &Field);
	Ui()->DoLabel(&Label, Localize("New value"), 12.0f, TEXTALIGN_ML);
	Ui()->DoEditBox(&m_TuningValueInput, &Field, 12.0f);
	RightInner.HSplitTop(10.0f, nullptr, &RightInner);

	RightInner.HSplitTop(LOGIN_ROW_HEIGHT, &Buttons, &RightInner);
	CUIRect Apply, Reset, ResetAll;
	const float ButtonGap = 6.0f;
	const float ButtonWidth = maximum(60.0f, (Buttons.w - 2.0f * ButtonGap) / 3.0f);
	Buttons.VSplitLeft(ButtonWidth, &Apply, &Buttons);
	Buttons.VSplitLeft(ButtonGap, nullptr, &Buttons);
	Buttons.VSplitLeft(ButtonWidth, &Reset, &Buttons);
	Buttons.VSplitLeft(ButtonGap, nullptr, &Buttons);
	Buttons.VSplitLeft(ButtonWidth, &ResetAll, &Buttons);

	if(GameClient()->m_Menus.DoButton_Menu(&m_TuningApplyButton, Localize("Apply"), 0, &Apply))
	{
		if(Client()->RconAuthed() && !m_TuningValueInput.IsEmpty())
		{
			char aCmd[128];
			str_format(aCmd, sizeof(aCmd), "tune %s %s", CTuningParams::Name(m_SelectedTuning), m_TuningValueInput.GetString());
			Client()->Rcon(aCmd);
		}
	}

	if(GameClient()->m_Menus.DoButton_Menu(&m_TuningResetButton, Localize("Reset"), 0, &Reset))
	{
		if(Client()->RconAuthed())
		{
			char aCmd[128];
			str_format(aCmd, sizeof(aCmd), "tune_reset %s", CTuningParams::Name(m_SelectedTuning));
			Client()->Rcon(aCmd);
		}
	}

	if(GameClient()->m_Menus.DoButton_Menu(&m_TuningResetAllButton, Localize("Reset all"), 0, &ResetAll))
	{
		if(Client()->RconAuthed())
			Client()->Rcon("tune_reset");
	}

	RightInner.HSplitTop(10.0f, nullptr, &RightInner);
	Ui()->DoLabel(&RightInner, Localize("Changes apply to global tunings (tune)."), 12.0f, TEXTALIGN_ML);
}

void CAdminPanel::RenderPanel(const CUIRect &Screen)
{
	const float Target = m_Active ? 1.0f : 0.0f;
	if(g_Config.m_BcAdminPanelDisableAnim)
		m_OpenAnimation = Target;
	else
	{
		const float Step = Client()->RenderFrameTime() / ANIM_DURATION;
		if(Target > m_OpenAnimation)
			m_OpenAnimation = minimum(1.0f, m_OpenAnimation + Step);
		else
			m_OpenAnimation = maximum(0.0f, m_OpenAnimation - Step);
	}

	if(m_OpenAnimation <= 0.0f)
		return;

	const float SizeTarget = Client()->RconAuthed() ? 1.0f : 0.0f;
	m_SizeAnimation = SizeTarget;

	const float Anim = QuadEaseInOut(m_OpenAnimation);
	const float PanelScale = g_Config.m_BcAdminPanelScale / 100.0f;
	const float PanelW = Screen.w * (0.55f + 0.20f * m_SizeAnimation) * PanelScale;
	const float PanelH = Screen.h * (0.50f + 0.20f * m_SizeAnimation) * PanelScale;
	CUIRect Panel = {(Screen.w - PanelW) / 2.0f, (Screen.h - PanelH) / 2.0f, PanelW, PanelH};
	Panel.y += (1.0f - Anim) * 10.0f;

	ColorRGBA Bg = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcAdminPanelBgColor, true)).WithMultipliedAlpha(Anim);
	Panel.Draw(Bg, IGraphics::CORNER_ALL, 8.0f);
	Panel.Margin(PANEL_PADDING, &Panel);

	CUIRect Header;
	Panel.HSplitTop(HEADER_HEIGHT, &Header, &Panel);

	CUIRect HeaderLeft, HeaderRight;
	Header.VSplitLeft(Header.w * 0.5f, &HeaderLeft, &HeaderRight);
	Ui()->DoLabel(&HeaderLeft, Localize("Admin Panel"), 18.0f, TEXTALIGN_ML);

	if(Client()->RconAuthed())
	{
		CUIRect Settings;
		HeaderRight.VSplitRight(HEADER_HEIGHT, &HeaderRight, &Settings);
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	const bool OpenSettings = GameClient()->m_Menus.DoButton_Menu(&m_SettingsButton, FontIcon::GEAR, 0, &Settings);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		if(OpenSettings)
			OpenActionPopup(-1, ACTION_SETTINGS);
	}
	else
	{
		Ui()->DoLabel(&HeaderRight, Localize("RCON not authenticated"), 12.0f, TEXTALIGN_MR);
	}

	Panel.HSplitTop(6.0f, nullptr, &Panel);
	if(Client()->RconAuthed())
	{
		CUIRect Footer;
		Panel.HSplitBottom(26.0f, &Panel, &Footer);
		Footer.Margin(6.0f, &Footer);

		int LocalAuth = AUTHED_NO;
		if(GameClient()->m_Snap.m_LocalClientId >= 0)
			LocalAuth = GameClient()->m_aClients[GameClient()->m_Snap.m_LocalClientId].m_AuthLevel;

		const char *pAuth = Localize("None");
		if(LocalAuth == AUTHED_ADMIN)
			pAuth = Localize("Admin");
		else if(LocalAuth == AUTHED_MOD)
			pAuth = Localize("Moderator");
		else if(LocalAuth == AUTHED_HELPER)
			pAuth = Localize("Helper");

		CUIRect LeftLabel, RightButton;
		Footer.VSplitLeft(Footer.w * 0.5f, &LeftLabel, &RightButton);
		Ui()->DoLabel(&LeftLabel, pAuth, 12.0f, TEXTALIGN_ML);
		RightButton.VSplitRight(110.0f, nullptr, &RightButton);
		if(GameClient()->m_Menus.DoButton_Menu(&m_RconLogoutButton, Localize("Logout"), 0, &RightButton))
			Client()->Rcon("logout");

		CUIRect TabBar;
		Panel.HSplitTop(TAB_HEIGHT, &TabBar, &Panel);
		TabBar.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f * Anim), IGraphics::CORNER_ALL, 6.0f);
		TabBar.VMargin(2.0f, &TabBar);
		const float TabWidth = TabBar.w / (float)TAB_COUNT;

		const ColorRGBA s_TabDefault = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcAdminPanelTabInactiveColor, true)).WithMultipliedAlpha(Anim);
		const ColorRGBA s_TabActive = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcAdminPanelTabActiveColor, true)).WithMultipliedAlpha(Anim);
		const ColorRGBA s_TabHover = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcAdminPanelTabHoverColor, true)).WithMultipliedAlpha(Anim);

		CUIRect Button;
		auto SetActiveTab = [&](int Tab) {
			m_ActiveTab = Tab;
			g_Config.m_BcAdminPanelLastTab = Tab;
		};
		TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
		if(GameClient()->m_Menus.DoButton_MenuTab(&m_TabPlayersButton, Localize("Players"), m_ActiveTab == TAB_PLAYERS, &Button, IGraphics::CORNER_L, nullptr, &s_TabDefault, &s_TabActive, &s_TabHover, 6.0f))
			SetActiveTab(TAB_PLAYERS);

		TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
		if(GameClient()->m_Menus.DoButton_MenuTab(&m_TabInfoButton, Localize("Info"), m_ActiveTab == TAB_INFO, &Button, IGraphics::CORNER_NONE, nullptr, &s_TabDefault, &s_TabActive, &s_TabHover, 6.0f))
			SetActiveTab(TAB_INFO);

		TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
		if(GameClient()->m_Menus.DoButton_MenuTab(&m_TabTuningsButton, Localize("Tunings"), m_ActiveTab == TAB_TUNINGS, &Button, IGraphics::CORNER_NONE, nullptr, &s_TabDefault, &s_TabActive, &s_TabHover, 6.0f))
			SetActiveTab(TAB_TUNINGS);

		TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
		if(GameClient()->m_Menus.DoButton_MenuTab(&m_TabFastActionsButton, Localize("Fast actions"), m_ActiveTab == TAB_FAST_ACTIONS, &Button, IGraphics::CORNER_NONE, nullptr, &s_TabDefault, &s_TabActive, &s_TabHover, 6.0f))
			SetActiveTab(TAB_FAST_ACTIONS);

		if(GameClient()->m_Menus.DoButton_MenuTab(&m_TabLogsButton, Localize("Logs"), m_ActiveTab == TAB_LOGS, &TabBar, IGraphics::CORNER_R, nullptr, &s_TabDefault, &s_TabActive, &s_TabHover, 6.0f))
			SetActiveTab(TAB_LOGS);

		Panel.HSplitTop(8.0f, nullptr, &Panel);
	}

	if(!Client()->RconAuthed())
	{
		RenderRconLogin(Panel);
		RenderActionPopup(Screen);
		return;
	}

	if(m_ActiveTab == TAB_TUNINGS)
	{
		RenderTunings(Panel);
		RenderActionPopup(Screen);
		return;
	}

	if(m_ActiveTab == TAB_FAST_ACTIONS)
	{
		RenderFastActions(Panel);
		RenderActionPopup(Screen);
		return;
	}

	if(m_ActiveTab == TAB_LOGS)
	{
		RenderLogs(Panel);
		RenderActionPopup(Screen);
		return;
	}

	CUIRect Left, Right;
	Panel.VSplitMid(&Left, &Right, PANEL_PADDING);

	Right.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.35f * Anim), IGraphics::CORNER_ALL, 6.0f);
	CUIRect RightInner = Right;
	RightInner.Margin(ACTION_BLOCK_MARGIN, &RightInner);
	RenderPlayerList(RightInner);

	Left.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.20f * Anim), IGraphics::CORNER_ALL, 6.0f);
	CUIRect LeftInner = Left;
	LeftInner.Margin(ACTION_BLOCK_MARGIN, &LeftInner);
	if(m_ActiveTab == TAB_PLAYERS)
		RenderPlayerActions(LeftInner, m_SelectedClientId);
	else
		RenderPlayerInfo(LeftInner, m_SelectedClientId);

	RenderActionPopup(Screen);
}

void CAdminPanel::OnRender()
{
	if(!m_Active && m_OpenAnimation <= 0.0f)
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
