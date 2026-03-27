#include "playermenu.h"

#include "game/client/animstate.h"
#include "game/localization.h"

#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <generated/protocol.h>

#include "engine/font_icons.h"

#include <game/client/components/camera.h>
#include <game/client/components/chat.h>
#include <game/client/components/console.h>
#include <game/client/components/controls.h>
#include <game/client/components/emoticon.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/ui.h>
struct SAdminPanelProperties
{
	static constexpr float ms_Width = 200.0f;

	static constexpr float ms_HeadlineFontSize = 8.0f;
	static constexpr float ms_FontSize = 8.0f;
	static constexpr float ms_IconFontSize = 11.0f;
	static constexpr float ms_Padding = 6.0f;
	static constexpr float ms_Rounding = 3.0f;

	static constexpr float ms_ItemSpacing = 2.0f;
	static constexpr float ms_GroupSpacing = 5.0f;

	static constexpr float ms_RconActionHeight = 25.0f;
	static constexpr float ms_RconActionWidth = 75.0f;
	static constexpr float ms_ReadyButtonsWidth = 80.0f;
	static constexpr float ms_RconTimersWidth = 50.0f;
	static constexpr float ms_ButtonHeight = 12.0f;

	static constexpr float ms_PlayerBtnWidth = 60.0f;

	static ColorRGBA WindowColor() { return ColorRGBA(0.451f, 0.451f, 0.451f, 0.9f); };
	static ColorRGBA WindowColorDark() { return ColorRGBA(0.2f, 0.2f, 0.2f, 0.9f); };
};

static void RenderTeeCute(CRenderTools *pRenderTools, const CAnimState *pAnim, const CTeeRenderInfo *pInfo, int Emote, vec2 Dir, vec2 Pos, bool CuteEyes, float Alpha = 1.0f)
{
	if(pInfo->m_Size > 0.0f)
		Dir /= pInfo->m_Size;

	const float Length = length(Dir);
	if(Length > 1.0f)
		Dir /= Length;
	if(CuteEyes && Length < 0.4f)
		Emote = 2;

	pRenderTools->RenderTee(pAnim, pInfo, Emote, Dir, Pos, Alpha);
}

void CPlayerMenu::DoIconLabeledButton(CUIRect *pRect, const char *pTitle, const char *pIcon, float TextSize, float Height, ColorRGBA IconColor) const
{
	CUIRect Label;
	pRect->VSplitLeft(Height, &Label, pRect);
	DoIconButton(&Label, pIcon, TextSize, IconColor);
	Ui()->DoLabel(pRect, pTitle, TextSize, TEXTALIGN_MC);
}

void CPlayerMenu::DoIconLabeledButtonDown(CUIRect *pRect, const char *pTitle, const char *pIcon, float IconSize, float TextSize, float Height, float Dif, ColorRGBA IconColor) const
{
	CUIRect Icon, Label;
	pRect->HSplitTop(Height, &Icon, &Label);
	DoIconButton(&Icon, pIcon, IconSize, IconColor);
	Label.HSplitTop(Dif, nullptr, &Label);
	Label.HSplitTop(Label.h / 2, &Label, nullptr);
	Ui()->DoLabel(&Label, pTitle, TextSize, TEXTALIGN_MC);
}

void CPlayerMenu::DoLabelLabeledButtonDown(CUIRect *pRect, const char *pTitleDown, const char *pTitle, float TextSize, float TextSizeDown, float Height, float Dif) const
{
	CUIRect Label, LabelDown;
	pRect->HSplitTop(Height, &Label, &LabelDown);
	Ui()->DoLabel(&Label, pTitle, TextSize, TEXTALIGN_MC);
	LabelDown.HSplitTop(Dif, nullptr, &LabelDown);
	LabelDown.HSplitTop(LabelDown.h / 2, &LabelDown, nullptr);
	Ui()->DoLabel(&LabelDown, pTitleDown, TextSizeDown, TEXTALIGN_MC);
}

void CPlayerMenu::DoIconButton(CUIRect *pRect, const char *pIcon, float TextSize, ColorRGBA IconColor) const
{
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	TextRender()->TextColor(IconColor);
	Ui()->DoLabel(pRect, pIcon, TextSize, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
}

CPlayerMenu::CPlayerMenu()
{
	OnReset();
}

void CPlayerMenu::OnConsoleInit()
{
	Console()->Register("toggle_playermenu", "", CFGFLAG_CLIENT, ConTogglePlayerMenu, this, "Toggle player menu");
}

void CPlayerMenu::ConTogglePlayerMenu(IConsole::IResult *pResult, void *pUserData)
{
	CPlayerMenu *pSelf = (CPlayerMenu *)pUserData;
	pSelf->SetActive(!pSelf->IsActive());
}

void CPlayerMenu::SetActive(bool Active)
{
	if(m_Active == Active)
		return;

	const bool WasActive = m_Active;
	m_Active = Active;
	if(m_Active)
	{
		m_Mouse.m_Unlocked = true;
		m_WasSpecActive = GameClient()->m_Snap.m_SpecInfo.m_Active;
		Console()->ExecuteLine("say /spec", IConsole::CLIENT_ID_UNSPECIFIED);
	}
	else
	{
		ResetState();
		if(WasActive)
			Console()->ExecuteLine("say /spec", IConsole::CLIENT_ID_UNSPECIFIED);
	}
}

void CPlayerMenu::OnReset()
{
	const bool WasActive = m_Active;
	ResetState();
	if(WasActive)
		Console()->ExecuteLine("say /spec", IConsole::CLIENT_ID_UNSPECIFIED);
}

void CPlayerMenu::OnRelease()
{
	const bool WasActive = m_Active;
	ResetState();
	if(WasActive)
		Console()->ExecuteLine("say /spec", IConsole::CLIENT_ID_UNSPECIFIED);
}

bool CPlayerMenu::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!IsActive() || !m_Mouse.m_Unlocked)
		return false;

	if(!GameClient()->m_Snap.m_SpecInfo.m_Active)
		return false;

	Ui()->ConvertMouseMove(&x, &y, CursorType);

	vec2 OldPos = m_Mouse.m_Position;
	m_Mouse.m_Position.x += x / 3.6;
	m_Mouse.m_Position.y += y / 3.6;

	// Check if we're dragging
	if(m_Mouse.m_MouseInput && !m_Mouse.m_IsDragging)
	{
		float DragThreshold = 5.0f;
		if(distance(OldPos, m_Mouse.m_Position) > DragThreshold)
		{
			m_Mouse.m_IsDragging = true;
			m_Mouse.m_DragStart = OldPos;
		}
	}

	const float ScreenWidth = 100.0f * 3.0f * Graphics()->ScreenAspect();
	const float ScreenHeight = 100.0f * 3.0f;
	m_Mouse.clampPosition(ScreenWidth, ScreenHeight);

	return true;
}

bool CPlayerMenu::OnInput(const IInput::CEvent &Event)
{
	if(!IsActive())
		return false;

	if(GameClient()->m_GameConsole.IsActive() || GameClient()->m_Menus.IsActive() || GameClient()->m_Chat.IsActive() || GameClient()->m_Emoticon.IsActive())
		return false;

	if(!GameClient()->m_Snap.m_SpecInfo.m_Active)
		return false;

	if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_MOUSE_2)
	{
		// find closest player to mouse
		if(!m_Popup.m_Visible)
		{
			if(m_HoveredPlayerId != -1)
			{
				m_Popup.m_PlayerId = m_HoveredPlayerId;
				m_Popup.m_Visible = true;
				m_FindHoursRequestedPlayerId = -1;

				return true;
			}
		}
		else
		{
			m_Popup.m_Visible = !m_Popup.m_Visible;
			RIReset();
		}
	}

	if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_ESCAPE)
	{
		OnReset();
		return true;
	}

	return m_Mouse.m_Clicked;
}

void CPlayerMenu::OnRender()
{
	if(!IsActive())
		return;

	const bool SpecActive = GameClient()->m_Snap.m_SpecInfo.m_Active;
	if(!SpecActive)
	{
		// Allow initial transition into spectator mode; only auto-close if we were already in spec.
		if(m_WasSpecActive)
			ResetState();
		return;
	}
	m_WasSpecActive = true;

	if(GameClient()->m_Snap.m_SpecInfo.m_Active && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == SPEC_FREEVIEW)
	{
		float Speed = 75.0f * 32.0f * (GameClient()->m_Camera.m_Zoom * 6 / g_Config.m_ClDefaultZoom) * (g_Config.m_RiSpectatorMoveSpeed / 100.0f); // Adjusted for frame-time independence
		float FrameTime = Client()->RenderFrameTime();
		if(Input()->KeyIsPressed(KEY_W))
			GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy].y -= Speed * FrameTime;
		if(Input()->KeyIsPressed(KEY_S))
			GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy].y += Speed * FrameTime;
		if(Input()->KeyIsPressed(KEY_A))
			GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy].x -= Speed * FrameTime;
		if(Input()->KeyIsPressed(KEY_D))
			GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy].x += Speed * FrameTime;
	}

	m_Mouse.m_LastMouseInput = m_Mouse.m_MouseInput;
	m_Mouse.m_MouseInput = Input()->KeyIsPressed(KEY_MOUSE_1);
	m_Mouse.m_Clicked = !m_Mouse.m_LastMouseInput && m_Mouse.m_MouseInput;
	// find closest player to mouse for highlighting
	float ClosestDist = 15.0f;
	m_HoveredPlayerId = -1;
	const float ScreenWidth = 100.0f * 3.0f * Graphics()->ScreenAspect();
	const float ScreenHeight = 100.0f * 3.0f;
	float WorldWidth = 0.0f, WorldHeight = 0.0f;
	Graphics()->CalcScreenParams(Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom, &WorldWidth, &WorldHeight);
	const vec2 CameraCenter = GameClient()->m_Camera.m_Center;
	const vec2 WorldToScreen = vec2(ScreenWidth / WorldWidth, ScreenHeight / WorldHeight);
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(!GameClient()->m_Snap.m_apPlayerInfos[i])
			continue;

		vec2 PlayerPos = GameClient()->m_aClients[i].m_RenderPos;
		if(PlayerPos.x == 0 && PlayerPos.y == 0)
			continue;
		m_PlayerScreenPos = vec2(
			ScreenWidth / 2 + (PlayerPos.x - CameraCenter.x) * WorldToScreen.x,
			ScreenHeight / 2 + (PlayerPos.y - CameraCenter.y) * WorldToScreen.y);

		float Dist = distance(vec2(m_Mouse.m_Position.x, m_Mouse.m_Position.y), m_PlayerScreenPos);
		if(Dist < ClosestDist)
		{
			ClosestDist = Dist;
			m_HoveredPlayerId = i;
			m_ClosestScreenPlayerPos = m_PlayerScreenPos;
		}
	}

	if(m_HoveredPlayerId != -1)
	{
		// Draw a highlight circle
		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.5f);
		Graphics()->DrawCircle(m_ClosestScreenPlayerPos.x, m_ClosestScreenPlayerPos.y, 10.0f, 32);
		Graphics()->QuadsEnd();
	}

	if(m_Popup.m_Visible)
	{
		RenderPlayerMenuPopUp();
	}
	if(m_Mouse.m_Unlocked)
	{
		RenderTools()->RenderCursor(m_Mouse.m_Position, 12.0f);
	}
}

void CPlayerMenu::RenderPlayerMenuPopUp()
{
	CUIRect Base;

	Base.h = 100.0f * 3.0f / 1.5;
	Base.w = 100.0f * 3.0f * Graphics()->ScreenAspect() / 1.5 + SAdminPanelProperties::ms_ButtonHeight;
	Base.x = 100.0f * 3.0f * Graphics()->ScreenAspect() / 2 - Base.w / 2;
	Base.y = (100.0f * 3.0f) / 2 - Base.h / 2;

	vec2 ScreenTL, ScreenBR;
	Graphics()->GetScreen(&ScreenTL.x, &ScreenTL.y, &ScreenBR.x, &ScreenBR.y);

	if(Base.y + Base.h > ScreenBR.y)
	{
		Base.y -= Base.y + Base.h - ScreenBR.y;
	}
	if(Base.x + Base.w > ScreenBR.x)
	{
		Base.x -= Base.x + Base.w - ScreenBR.x;
	}

	Base.Draw(SAdminPanelProperties::WindowColorDark(), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
	m_Popup.m_Rect = Base;
	Base.Margin(SAdminPanelProperties::ms_Padding, &Base);
	CUIRect SkinInfo, QuickActions, PlayerInfo;
	Base.VSplitLeft( Base.w / 3 - SAdminPanelProperties::ms_GroupSpacing, &SkinInfo, &Base);
	Base.VSplitLeft(SAdminPanelProperties::ms_GroupSpacing, nullptr, &Base);
	Base.VSplitRight(Base.w / 2 - SAdminPanelProperties::ms_GroupSpacing, &QuickActions, &PlayerInfo);
	PlayerInfo.VSplitLeft(SAdminPanelProperties::ms_GroupSpacing, nullptr, &PlayerInfo);

	SkinInfo.Draw(ColorRGBA(0.15f, 0.15f, 0.15f, 0.55f), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
	QuickActions.Draw(ColorRGBA(0.15f, 0.15f, 0.15f, 0.55f), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
	PlayerInfo.Draw(ColorRGBA(0.15f, 0.15f, 0.15f, 0.55f), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
	SkinInfo.Margin(SAdminPanelProperties::ms_Padding, &SkinInfo);
	QuickActions.Margin(SAdminPanelProperties::ms_Padding, &QuickActions);
	PlayerInfo.Margin(SAdminPanelProperties::ms_Padding, &PlayerInfo);

	RenderPlayerMenuPopUpSkinInfo(&SkinInfo);
	RenderPlayerMenuPopUpQuickActions(&QuickActions);
	RenderPlayerMenuPopUpPlayerInfo(&PlayerInfo);
}

void CPlayerMenu::RenderPlayerMenuPopUpSkinInfo(CUIRect *pBase)
{
	if(m_Popup.m_PlayerId < 0 || m_Popup.m_PlayerId >= MAX_CLIENTS)
		return;

	const CGameClient::CClientData &Client = GameClient()->m_aClients[m_Popup.m_PlayerId];
	CUIRect Label, Preview, Row;

	pBase->HSplitTop(SAdminPanelProperties::ms_HeadlineFontSize + 4.0f, &Label, pBase);
	Ui()->DoLabel(&Label, Localize("Skin"), 10.0f, TEXTALIGN_MC);
	pBase->HSplitTop(SAdminPanelProperties::ms_ItemSpacing, nullptr, pBase);

	pBase->HSplitTop(52.0f, &Preview, pBase);
	Preview.Draw(ColorRGBA(0.18f, 0.18f, 0.18f, 0.8f), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
	{
		CTeeRenderInfo TeeInfo = Client.m_RenderInfo;
		TeeInfo.m_Size *= 1;
		vec2 OffsetToMid;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &TeeInfo, OffsetToMid);
		const vec2 TeeRenderPos = vec2(Preview.x + Preview.w / 2.0f, Preview.y + Preview.h / 2.0f + OffsetToMid.y);
		RenderTeeCute(RenderTools(), CAnimState::GetIdle(), &TeeInfo, EMOTE_NORMAL, m_Mouse.m_Position - TeeRenderPos, TeeRenderPos, true);
	}

	pBase->HSplitTop(SAdminPanelProperties::ms_ItemSpacing + 1.0f, nullptr, pBase);
	char aBuf[128];

	pBase->HSplitTop(SAdminPanelProperties::ms_ButtonHeight, &Row, pBase);
	str_format(aBuf, sizeof(aBuf), "Name: %s", Client.m_aSkinName);
	Ui()->DoLabel(&Row, aBuf, SAdminPanelProperties::ms_FontSize, TEXTALIGN_ML);

	pBase->HSplitTop(SAdminPanelProperties::ms_ItemSpacing, nullptr, pBase);
	pBase->HSplitTop(SAdminPanelProperties::ms_ButtonHeight, &Row, pBase);
	str_format(aBuf, sizeof(aBuf), "Custom: %s", Client.m_UseCustomColor ? "Yes" : "No");
	Ui()->DoLabel(&Row, aBuf, SAdminPanelProperties::ms_FontSize, TEXTALIGN_ML);

	pBase->HSplitTop(SAdminPanelProperties::ms_ItemSpacing, nullptr, pBase);
	pBase->HSplitTop(SAdminPanelProperties::ms_ButtonHeight, &Row, pBase);
	str_format(aBuf, sizeof(aBuf), "Body: %d", Client.m_ColorBody);
	Ui()->DoLabel(&Row, aBuf, SAdminPanelProperties::ms_FontSize, TEXTALIGN_ML);

	pBase->HSplitTop(SAdminPanelProperties::ms_ItemSpacing, nullptr, pBase);
	pBase->HSplitTop(SAdminPanelProperties::ms_ButtonHeight, &Row, pBase);
	str_format(aBuf, sizeof(aBuf), "Feet: %d", Client.m_ColorFeet);
	Ui()->DoLabel(&Row, aBuf, SAdminPanelProperties::ms_FontSize, TEXTALIGN_ML);
}

void CPlayerMenu::RenderPlayerMenuPopUpQuickActions(CUIRect *pBase)
{
	if(m_Popup.m_PlayerId < 0 || m_Popup.m_PlayerId >= MAX_CLIENTS)
		return;

	CGameClient::CClientData &Client = GameClient()->m_aClients[m_Popup.m_PlayerId];

	CUIRect Label, Row, Action;
	pBase->HSplitTop(SAdminPanelProperties::ms_HeadlineFontSize + 4.0f, &Label, pBase);
	Ui()->DoLabel(&Label, Localize("Quick Actions"), 10.0f, TEXTALIGN_MC);
	pBase->HSplitTop(SAdminPanelProperties::ms_ItemSpacing * 2.0f, nullptr, pBase);

	const float QuickActionSpacing = SAdminPanelProperties::ms_ItemSpacing;
	auto RenderQuickAction = [&](CUIRect *pRow, float ActionWidth, const char *pIcon, ColorRGBA ColorInactive, ColorRGBA ColorActive, bool IsActive, auto &&OnClick) {
		pRow->VSplitLeft(ActionWidth, &Action, pRow);
		if(pRow->w > 0.0f)
			pRow->VSplitLeft(QuickActionSpacing, nullptr, pRow);

		const bool IsHovered = Hovered(&Action);
		ColorRGBA Color = IsActive ? ColorActive : ColorInactive;
		if(IsHovered)
			Color = ColorRGBA(std::clamp(Color.r + 0.08f, 0.0f, 1.0f), std::clamp(Color.g + 0.08f, 0.0f, 1.0f), std::clamp(Color.b + 0.08f, 0.0f, 1.0f), Color.a);

		Action.Draw(Color, IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
		DoIconButton(&Action, pIcon, 10.0f, TextRender()->DefaultTextColor());
		if(DoButtonLogic(&Action))
			OnClick();
	};

	pBase->HSplitTop(20.0f, &Row, pBase);
	{
		CUIRect Row1 = Row;
		const float ActionWidth = (Row1.w - QuickActionSpacing * 2.0f) / 3.0f;
		RenderQuickAction(&Row1, ActionWidth, FontIcon::HEART, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f), ColorRGBA(0.95f, 0.3f, 0.3f, 0.85f), Client.m_Friend, [&]() {
			if(Client.m_Friend)
				GameClient()->Friends()->RemoveFriend(Client.m_aName, Client.m_aClan);
			else
				GameClient()->Friends()->AddFriend(Client.m_aName, Client.m_aClan);
		});

		RenderQuickAction(&Row1, ActionWidth, FontIcon::BAN, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f), ColorRGBA(0.95f, 0.3f, 0.3f, 0.85f), Client.m_ChatIgnore, [&]() {
			Client.m_ChatIgnore ^= 1;
		});

		RenderQuickAction(&Row1, ActionWidth, Client.m_EmoticonIgnore ? FontIcon::COMMENT_SLASH : FontIcon::COMMENT,
			ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f), ColorRGBA(0.95f, 0.3f, 0.3f, 0.85f), Client.m_EmoticonIgnore, [&]() {
				Client.m_EmoticonIgnore ^= 1;
			});
	}

	pBase->HSplitTop(SAdminPanelProperties::ms_ItemSpacing * 2.0f, nullptr, pBase);
	pBase->HSplitTop(20.0f, &Row, pBase);
	{
		CUIRect Row2 = Row;
		const float ActionWidth = (Row2.w - QuickActionSpacing * 2.0f) / 3.0f;
		const bool IsTracked = GameClient()->m_RClient.IsTracked(Client.ClientId());
		RenderQuickAction(&Row2, ActionWidth, FontIcon::RC_LIST_TRACK, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f), ColorRGBA(0.53f, 0.78f, 0.53f, 0.8f), IsTracked, [&]() {
			if(IsTracked)
				GameClient()->m_RClient.TargetPlayerPosRemove(Client.m_aName);
			else
				GameClient()->m_RClient.TargetPlayerPosAdd(Client.m_aName);
		});

		const bool IsInTeamList = GameClient()->m_RClient.IsInWarlist(Client.ClientId(), 2);
		RenderQuickAction(&Row2, ActionWidth, FontIcon::ICON_USERS, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f), ColorRGBA(0.53f, 0.78f, 0.53f, 0.8f), IsInTeamList, [&]() {
			if(IsInTeamList)
				GameClient()->m_WarList.RemoveWarEntryInGame(2, Client.m_aName, false);
			else
				GameClient()->m_WarList.AddWarEntryInGame(2, Client.m_aName, "", false);
		});

		const bool IsInWarList = GameClient()->m_RClient.IsInWarlist(Client.ClientId(), 1);
		RenderQuickAction(&Row2, ActionWidth, FontIcon::RC_PERSON_RIFLE, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f), ColorRGBA(1.0f, 0.42f, 0.42f, 0.8f), IsInWarList, [&]() {
			if(IsInWarList)
				GameClient()->m_WarList.RemoveWarEntryInGame(1, Client.m_aName, false);
			else
				GameClient()->m_WarList.AddWarEntryInGame(1, Client.m_aName, "", false);
		});
	}

	pBase->HSplitTop(SAdminPanelProperties::ms_ItemSpacing * 2.0f, nullptr, pBase);

	auto RenderBottomButtonsRow = [&](const char *pLeftText, auto &&LeftOnClick, const char *pRightText, auto &&RightOnClick) {
		CUIRect ButtonsRow, LeftButton, RightButton;
		pBase->HSplitTop(SAdminPanelProperties::ms_ButtonHeight, &ButtonsRow, pBase);
		ButtonsRow.VSplitMid(&LeftButton, &RightButton, SAdminPanelProperties::ms_ItemSpacing);

		const bool LeftHovered = Hovered(&LeftButton);
		LeftButton.Draw(LeftHovered ? ColorRGBA(0.34f, 0.34f, 0.34f, 0.85f) : ColorRGBA(0.28f, 0.28f, 0.28f, 0.8f), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
		Ui()->DoLabel(&LeftButton, pLeftText, SAdminPanelProperties::ms_FontSize, TEXTALIGN_MC);
		if(DoButtonLogic(&LeftButton))
			LeftOnClick();

		const bool RightHovered = Hovered(&RightButton);
		RightButton.Draw(RightHovered ? ColorRGBA(0.34f, 0.34f, 0.34f, 0.85f) : ColorRGBA(0.28f, 0.28f, 0.28f, 0.8f), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
		Ui()->DoLabel(&RightButton, pRightText, SAdminPanelProperties::ms_FontSize, TEXTALIGN_MC);
		if(DoButtonLogic(&RightButton))
			RightOnClick();

		pBase->HSplitTop(SAdminPanelProperties::ms_ItemSpacing, nullptr, pBase);
	};

	RenderBottomButtonsRow(Localize("Spectate"), [&]() {
		if(GameClient()->m_Snap.m_SpecInfo.m_Active)
			GameClient()->m_Spectator.Spectate(m_Popup.m_PlayerId);
		else
		{
			char aEscapedCommand[2 * MAX_NAME_LENGTH + 32];
			str_copy(aEscapedCommand, "say /spec \"");
			char *pDst = aEscapedCommand + str_length(aEscapedCommand);
			str_escape(&pDst, Client.m_aName, aEscapedCommand + sizeof(aEscapedCommand));
			str_append(aEscapedCommand, "\"");
			Console()->ExecuteLine(aEscapedCommand, IConsole::CLIENT_ID_UNSPECIFIED);
		}
	}, Localize("Profile"), [&]() {
		CServerInfo ServerInfo;
		GameClient()->Client()->GetServerInfo(&ServerInfo);
		int Community = (str_comp(ServerInfo.m_aCommunityId, "kog") == 0) ? 1 : (str_comp(ServerInfo.m_aCommunityId, "unique") == 0) ? 2 : 0;
		char aCommunityLink[512];
		char aEncodedName[256];
		EscapeUrl(aEncodedName, sizeof(aEncodedName), Client.m_aName);
		if(Community == 1)
			str_format(aCommunityLink, sizeof(aCommunityLink), "https://kog.tw/#p=players&player=%s", aEncodedName);
		else if(Community == 2)
			str_format(aCommunityLink, sizeof(aCommunityLink), "https://uniqueclan.net/ranks/player/%s", aEncodedName);
		else
			str_format(aCommunityLink, sizeof(aCommunityLink), "https://ddnet.org/players/%s", aEncodedName);
		GameClient()->Client()->ViewLink(aCommunityLink);
	});

	RenderBottomButtonsRow(Localize("Whisper"), [&]() {
		char aWhisperBuf[512];
		str_format(aWhisperBuf, sizeof(aWhisperBuf), "chat all /whisper %s ", Client.m_aName);
		Console()->ExecuteLine(aWhisperBuf, IConsole::CLIENT_ID_UNSPECIFIED);
	}, Localize("Copy Skin"), [&]() {
		GameClient()->m_RClient.CopySkin(Client.m_aName);
	});

	RenderBottomButtonsRow(Localize("Vote Kick"), [&]() {
		GameClient()->m_Voting.CallvoteKick(m_Popup.m_PlayerId, "");
	}, Localize("Find Hours"), [&]() {
		GameClient()->m_RClient.FetchFindHours(Client.m_aName, "");
		m_FindHoursRequestedPlayerId = m_Popup.m_PlayerId;
	});

	RenderBottomButtonsRow(Localize("Clip Name"), [&]() {
		Input()->SetClipboardText(Client.m_aName);
	}, Localize("/Swap"), [&]() {
		char aSwapBuf[256];
		str_format(aSwapBuf, sizeof(aSwapBuf), "say /swap %s", Client.m_aName);
		Console()->ExecuteLine(aSwapBuf, IConsole::CLIENT_ID_UNSPECIFIED);
	});
}

void CPlayerMenu::RenderPlayerMenuPopUpPlayerInfo(CUIRect *pBase)
{
	if(m_Popup.m_PlayerId < 0 || m_Popup.m_PlayerId >= MAX_CLIENTS)
		return;

	const CGameClient::CClientData &Client = GameClient()->m_aClients[m_Popup.m_PlayerId];
	const CNetObj_PlayerInfo *pPlayerInfo = GameClient()->m_Snap.m_apPlayerInfos[m_Popup.m_PlayerId];

	CUIRect Label, Row;
	pBase->HSplitTop(SAdminPanelProperties::ms_HeadlineFontSize + 4.0f, &Label, pBase);
	Ui()->DoLabel(&Label, Localize("Player Info"), 10.0f, TEXTALIGN_MC);
	pBase->HSplitTop(SAdminPanelProperties::ms_ItemSpacing + 1.0f, nullptr, pBase);

	int Hours = -1;
	int Points = 0;
	const bool HasFindHoursResult = GameClient()->m_RClient.GetFindHoursResult(Client.m_aName, &Hours, &Points);
	const bool FindHoursPending = GameClient()->m_RClient.IsFindHoursInProgress(Client.m_aName);
	if(!HasFindHoursResult && !FindHoursPending && m_FindHoursRequestedPlayerId != m_Popup.m_PlayerId)
	{
		GameClient()->m_RClient.FetchFindHours(Client.m_aName, "");
		m_FindHoursRequestedPlayerId = m_Popup.m_PlayerId;
	}

	static int s_VoiceVolumeDragPlayerId = -1;

	char aBuf[128];
	pBase->HSplitTop(SAdminPanelProperties::ms_ButtonHeight, &Row, pBase);
	if(HasFindHoursResult)
		str_format(aBuf, sizeof(aBuf), "Points: %d", Points);
	else if(FindHoursPending)
		str_copy(aBuf, "Points: loading...", sizeof(aBuf));
	else if(pPlayerInfo)
		str_format(aBuf, sizeof(aBuf), "Points: %d", pPlayerInfo->m_Score);
	else
		str_copy(aBuf, "Points: unknown", sizeof(aBuf));
	Ui()->DoLabel(&Row, aBuf, SAdminPanelProperties::ms_FontSize, TEXTALIGN_ML);

	pBase->HSplitTop(SAdminPanelProperties::ms_ItemSpacing, nullptr, pBase);
	pBase->HSplitTop(SAdminPanelProperties::ms_ButtonHeight, &Row, pBase);
	if(HasFindHoursResult)
		str_format(aBuf, sizeof(aBuf), "Hours: %d", Hours);
	else if(FindHoursPending)
		str_copy(aBuf, "Hours: loading...", sizeof(aBuf));
	else
		str_copy(aBuf, "Hours: unknown", sizeof(aBuf));
	Ui()->DoLabel(&Row, aBuf, SAdminPanelProperties::ms_FontSize, TEXTALIGN_ML);

	pBase->HSplitTop(SAdminPanelProperties::ms_ItemSpacing, nullptr, pBase);
	pBase->HSplitTop(SAdminPanelProperties::ms_ButtonHeight, &Row, pBase);
	str_format(aBuf, sizeof(aBuf), "Nickname: %s", Client.m_aName);
	Ui()->DoLabel(&Row, aBuf, SAdminPanelProperties::ms_FontSize, TEXTALIGN_ML);

	pBase->HSplitTop(SAdminPanelProperties::ms_ItemSpacing, nullptr, pBase);
	pBase->HSplitTop(SAdminPanelProperties::ms_ButtonHeight, &Row, pBase);
	str_format(aBuf, sizeof(aBuf), "Clan: %s", Client.m_aClan[0] ? Client.m_aClan : "-");
	Ui()->DoLabel(&Row, aBuf, SAdminPanelProperties::ms_FontSize, TEXTALIGN_ML);

	const CNetObj_PlayerInfo *pPingInfo = pPlayerInfo;
	if(!pPingInfo)
	{
		for(const CNetObj_PlayerInfo *pInfo : GameClient()->m_Snap.m_apInfoByScore)
		{
			if(pInfo && pInfo->m_ClientId == m_Popup.m_PlayerId)
			{
				pPingInfo = pInfo;
				break;
			}
		}
	}
	static int s_aCachedPing[MAX_CLIENTS];
	static bool s_CachedPingInit = false;
	if(!s_CachedPingInit)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
			s_aCachedPing[i] = -1;
		s_CachedPingInit = true;
	}
	if(pPingInfo)
		s_aCachedPing[m_Popup.m_PlayerId] = std::clamp(pPingInfo->m_Latency, 0, 999);
	const int Ping = pPingInfo ? std::clamp(pPingInfo->m_Latency, 0, 999) : s_aCachedPing[m_Popup.m_PlayerId];
	pBase->HSplitTop(SAdminPanelProperties::ms_ItemSpacing, nullptr, pBase);
	pBase->HSplitTop(SAdminPanelProperties::ms_ButtonHeight, &Row, pBase);
	if(Ping >= 0)
		str_format(aBuf, sizeof(aBuf), "Ping: %d", Ping);
	else
		str_copy(aBuf, "Ping: unknown", sizeof(aBuf));
	Ui()->DoLabel(&Row, aBuf, SAdminPanelProperties::ms_FontSize, TEXTALIGN_ML);

	pBase->HSplitTop(SAdminPanelProperties::ms_ItemSpacing, nullptr, pBase);
	pBase->HSplitTop(SAdminPanelProperties::ms_ButtonHeight, &Row, pBase);
	{
		CUIRect CountryText, FlagArea, FlagRect;
		const float FlagWidth = Row.h * 1.6f;
		Row.VSplitRight(FlagWidth + 2.0f, &CountryText, &FlagArea);
		FlagArea.VSplitLeft(2.0f, nullptr, &FlagRect);
		str_format(aBuf, sizeof(aBuf), "Country: %d", Client.m_Country);
		Ui()->DoLabel(&CountryText, aBuf, SAdminPanelProperties::ms_FontSize, TEXTALIGN_ML);
		GameClient()->m_CountryFlags.Render(Client.m_Country, ColorRGBA(1.0f, 1.0f, 1.0f, 0.75f), FlagRect.x, FlagRect.y + 1.0f, FlagRect.w, FlagRect.h - 2.0f);
	}

	pBase->HSplitTop(SAdminPanelProperties::ms_GroupSpacing, nullptr, pBase);
	pBase->HSplitTop(SAdminPanelProperties::ms_ButtonHeight, &Label, pBase);
	Ui()->DoLabel(&Label, Localize("Voice"), SAdminPanelProperties::ms_FontSize, TEXTALIGN_MC);

	const bool VoiceActive = GameClient()->m_RClient.IsVoiceActive(m_Popup.m_PlayerId);
	const bool VoiceMuted = CRClient::VoiceListHasName(g_Config.m_RiVoiceMute, Client.m_aName);

	pBase->HSplitTop(SAdminPanelProperties::ms_ItemSpacing, nullptr, pBase);
	pBase->HSplitTop(SAdminPanelProperties::ms_ButtonHeight, &Row, pBase);
	str_format(aBuf, sizeof(aBuf), "Speaking: %s", VoiceActive ? "Yes" : "No");
	Ui()->DoLabel(&Row, aBuf, SAdminPanelProperties::ms_FontSize, TEXTALIGN_ML);

	pBase->HSplitTop(SAdminPanelProperties::ms_ItemSpacing, nullptr, pBase);
	pBase->HSplitTop(SAdminPanelProperties::ms_ButtonHeight, &Row, pBase);
	{
		const bool IsHovered = Hovered(&Row);
		ColorRGBA ButtonColor = VoiceMuted ? ColorRGBA(0.52f, 0.24f, 0.24f, 0.8f) : ColorRGBA(0.24f, 0.42f, 0.24f, 0.8f);
		if(IsHovered)
			ButtonColor = ColorRGBA(std::clamp(ButtonColor.r + 0.08f, 0.0f, 1.0f), std::clamp(ButtonColor.g + 0.08f, 0.0f, 1.0f), std::clamp(ButtonColor.b + 0.08f, 0.0f, 1.0f), 0.85f);
		Row.Draw(ButtonColor, IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
		Ui()->DoLabel(&Row, VoiceMuted ? Localize("Unmute Voice") : Localize("Mute Voice"), SAdminPanelProperties::ms_FontSize, TEXTALIGN_MC);
		if(DoButtonLogic(&Row))
		{
			if(VoiceMuted)
				CRClient::VoiceListRemoveName(g_Config.m_RiVoiceMute, sizeof(g_Config.m_RiVoiceMute), Client.m_aName);
			else
				CRClient::VoiceListAddName(g_Config.m_RiVoiceMute, sizeof(g_Config.m_RiVoiceMute), Client.m_aName);
		}
	}

	pBase->HSplitTop(SAdminPanelProperties::ms_ItemSpacing, nullptr, pBase);
	pBase->HSplitTop(SAdminPanelProperties::ms_ButtonHeight, &Row, pBase);
	{
		const int CurrentVolume = GameClient()->m_RClient.VoiceNameVolume(Client.m_aName, 100);
		CUIRect VolumeLabel, Slider;
		Row.VSplitLeft(56.0f, &VolumeLabel, &Slider);
		str_format(aBuf, sizeof(aBuf), "Vol: %d%%", CurrentVolume);
		Ui()->DoLabel(&VolumeLabel, aBuf, SAdminPanelProperties::ms_FontSize, TEXTALIGN_ML);

		// Custom slider for this menu (uses unlocked spectator cursor, not Ui()->DoScrollbarH).
		const bool SliderHovered = Hovered(&Slider);
		if(m_Mouse.m_Clicked && SliderHovered)
			s_VoiceVolumeDragPlayerId = m_Popup.m_PlayerId;
		if(!m_Mouse.m_MouseInput && s_VoiceVolumeDragPlayerId == m_Popup.m_PlayerId)
			s_VoiceVolumeDragPlayerId = -1;

		const float CurrentNorm = CurrentVolume / 200.0f;
		const bool IsDragging = s_VoiceVolumeDragPlayerId == m_Popup.m_PlayerId;
		float NewNorm = CurrentNorm;
		if(IsDragging)
			NewNorm = std::clamp((m_Mouse.m_Position.x - Slider.x) / std::max(Slider.w, 1.0f), 0.0f, 1.0f);

		Slider.Draw(ColorRGBA(0.2f, 0.2f, 0.2f, 0.8f), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
		CUIRect Filled = Slider;
		Filled.w = Slider.w * NewNorm;
		if(Filled.w > 0.0f)
			Filled.Draw(ColorRGBA(0.45f, 0.72f, 0.45f, 0.9f), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);

		CUIRect Knob = Slider;
		const float KnobW = 4.0f;
		Knob.x = Slider.x + Slider.w * NewNorm - KnobW / 2.0f;
		Knob.w = KnobW;
		Knob.Draw(ColorRGBA(0.95f, 0.95f, 0.95f, 0.95f), IGraphics::CORNER_ALL, 2.0f);

		const int NewVolume = std::clamp((int)(NewNorm * 200.0f + 0.5f), 0, 200);
		if(NewVolume != CurrentVolume)
		{
			if(NewVolume == 100)
				GameClient()->m_RClient.VoiceNameVolumeClear(Client.m_aName);
			else
				GameClient()->m_RClient.VoiceNameVolumeSet(Client.m_aName, NewVolume);
		}
	}
}

bool CPlayerMenu::IsActive() const
{
	if(m_Active)
		return true;

	return false;
}

bool CPlayerMenu::IsActivePopup() const
{
	if(m_Popup.m_Visible)
		return true;

	return false;
}
