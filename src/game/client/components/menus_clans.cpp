/* Copyright © 2026 BestProject Team */
#include <base/math.h>
#include <base/system.h>

#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include "menus.h"

#include <engine/font_icons.h>
#include <vector>

static const char *RoleLabel(CClans::ERole Role)
{
	switch(Role)
	{
	case CClans::ERole::PRESIDENT: return Localize("President");
	case CClans::ERole::VICE_PRESIDENT: return Localize("Vice-President");
	case CClans::ERole::VETERAN: return Localize("Veteran");
	case CClans::ERole::MEMBER: return Localize("Member");
	default: return "";
	}
}

static void RenderClanMemberTee(CGameClient *pGameClient, CRenderTools *pRenderTools, CUIRect TeeBox, const CClans::SSkin &Skin)
{
	const CSkin *pDefault = pGameClient->m_Skins.Find("default");
	const CSkins::CSkinContainer *pCont = pGameClient->m_Skins.FindContainerOrNullptr(Skin.m_aName[0] ? Skin.m_aName : "default");
	CTeeRenderInfo Info;
	Info.Apply(pCont == nullptr || pCont->Skin() == nullptr ? pDefault : pCont->Skin().get());
	Info.ApplyColors(Skin.m_UseCustomColor, Skin.m_ColorBody, Skin.m_ColorFeet);
	Info.m_Size = minimum(TeeBox.w, TeeBox.h) * 0.9f;
	vec2 OffsetToMid;
	CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &Info, OffsetToMid);
	const vec2 Pos = vec2(TeeBox.x + TeeBox.w / 2.0f, TeeBox.y + TeeBox.h / 2.0f + OffsetToMid.y);
	pRenderTools->RenderTee(CAnimState::GetIdle(), &Info, EMOTE_NORMAL, vec2(1.0f, 0.0f), Pos);
}

void CMenus::RenderClans(CUIRect MainView)
{
	CClans &Clans = GameClient()->m_Clans;

	// Toasts
	{
		CUIRect ToastArea = MainView;
		ToastArea.HSplitTop(0.0f, nullptr, &ToastArea);
		float Y = 4.0f;
		for(const auto &Toast : Clans.Toasts())
		{
			CUIRect ToastRect;
			ToastArea.HSplitTop(Y, nullptr, &ToastRect);
			ToastRect.HSplitTop(22.0f, &ToastRect, nullptr);
			ToastRect.VMargin(MainView.w * 0.25f, &ToastRect);
			ToastRect.Draw(ColorRGBA(0.1f, 0.1f, 0.1f, 0.85f), IGraphics::CORNER_ALL, 4.0f);
			Ui()->DoLabel(&ToastRect, Toast.m_aText, 12.0f, TEXTALIGN_MC);
			Y += 26.0f;
		}
	}

	MainView.Draw(ms_ColorTabbarActive, IGraphics::CORNER_B, 10.0f);
	MainView.Margin(10.0f, &MainView);

	if(!Clans.IsLoggedIn())
	{
		RenderClansAuth(MainView);
		return;
	}

	{
		CUIRect Bar, RefreshBtn, Msg;
		MainView.HSplitTop(28.0f, &Bar, &MainView);
		Bar.VSplitRight(100.0f, &Bar, &RefreshBtn);
		static CButtonContainer s_RefreshAll;
		if(DoButton_Menu(&s_RefreshAll, Localize("Refresh"), 0, &RefreshBtn) && !Clans.IsBusy())
			Clans.RefreshCurrentView();
		Bar.VSplitRight(8.0f, &Msg, nullptr);
		if(Clans.ErrorMessage()[0])
		{
			TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
			Ui()->DoLabel(&Msg, Clans.ErrorMessage(), 12.0f, TEXTALIGN_MR);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
		else if(Clans.StatusMessage()[0])
			Ui()->DoLabel(&Msg, Clans.StatusMessage(), 12.0f, TEXTALIGN_MR);
		else if(Clans.IsBusy())
			Ui()->DoLabel(&Msg, Localize("Loading..."), 12.0f, TEXTALIGN_MR);
		MainView.HSplitTop(6.0f, nullptr, &MainView);
	}

	switch(Clans.View())
	{
	case CClans::EView::SETUP:
		RenderClansSetup(MainView);
		break;
	case CClans::EView::APPLICATIONS:
		RenderClansApplications(MainView);
		break;
	case CClans::EView::ANNOUNCEMENTS:
		RenderClansAnnouncements(MainView);
		break;
	case CClans::EView::RECENT:
		RenderClansRecent(MainView);
		break;
	case CClans::EView::CLAN:
		RenderClansPage(MainView);
		break;
	case CClans::EView::BROWSE:
	case CClans::EView::LANDING:
	default:
		RenderClansLanding(MainView);
		break;
	}
}

void CMenus::RenderClansAuth(CUIRect MainView)
{
	CClans &Clans = GameClient()->m_Clans;
	static CLineInput s_Nick;
	static CLineInput s_Pass;
	static char s_aNick[32];
	static char s_aPass[64];
	static bool s_Init = false;
	if(!s_Init)
	{
		str_copy(s_aNick, g_Config.m_PlayerName, sizeof(s_aNick));
		s_aPass[0] = '\0';
		s_Init = true;
	}
	s_Nick.SetBuffer(s_aNick, sizeof(s_aNick));
	s_Pass.SetBuffer(s_aPass, sizeof(s_aPass));
	s_Pass.SetHidden(true);

	CUIRect Box, Label, Button;
	MainView.VMargin(MainView.w * 0.28f, &Box);
	Box.HMargin(Box.h * 0.2f, &Box);
	Box.Draw(ColorRGBA(0, 0, 0, 0.35f), IGraphics::CORNER_ALL, 8.0f);
	Box.Margin(16.0f, &Box);

	Box.HSplitTop(28.0f, &Label, &Box);
	Ui()->DoLabel(&Label, Localize("Clans account"), 18.0f, TEXTALIGN_MC);

	Box.HSplitTop(12.0f, nullptr, &Box);
	Box.HSplitTop(20.0f, &Label, &Box);
	Ui()->DoLabel(&Label, Localize("Nickname"), 14.0f, TEXTALIGN_ML);
	Box.HSplitTop(4.0f, nullptr, &Box);
	Box.HSplitTop(24.0f, &Button, &Box);
	Ui()->DoEditBox(&s_Nick, &Button, 14.0f);

	Box.HSplitTop(10.0f, nullptr, &Box);
	Box.HSplitTop(20.0f, &Label, &Box);
	Ui()->DoLabel(&Label, Localize("Password"), 14.0f, TEXTALIGN_ML);
	Box.HSplitTop(4.0f, nullptr, &Box);
	Box.HSplitTop(24.0f, &Button, &Box);
	Ui()->DoEditBox(&s_Pass, &Button, 14.0f);

	Box.HSplitTop(16.0f, nullptr, &Box);
	CUIRect Left, Right;
	Box.HSplitTop(28.0f, &Button, &Box);
	Button.VSplitMid(&Left, &Right, 8.0f);
	static CButtonContainer s_Login, s_Register;
	if(DoButton_Menu(&s_Login, Localize("Login"), 0, &Left) && !Clans.IsBusy())
		Clans.Login(s_aNick, s_aPass);
	if(DoButton_Menu(&s_Register, Localize("Register"), 0, &Right) && !Clans.IsBusy())
		Clans.Register(s_aNick, s_aPass);

	if(Clans.ErrorMessage()[0])
	{
		Box.HSplitTop(12.0f, nullptr, &Box);
		Box.HSplitTop(20.0f, &Label, &Box);
		TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
		Ui()->DoLabel(&Label, Clans.ErrorMessage(), 13.0f, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		if(!str_comp(Clans.ErrorMessage(), Localize("Please open a ticket")))
		{
			Box.HSplitTop(4.0f, nullptr, &Box);
			Box.HSplitTop(22.0f, &Button, &Box);
			static CButtonContainer s_Discord;
			if(DoButton_Menu(&s_Discord, "discord.gg/bestclient", 0, &Button))
				Client()->ViewLink("https://discord.gg/bestclient");
		}
	}
	if(Clans.StatusMessage()[0])
	{
		Box.HSplitTop(8.0f, nullptr, &Box);
		Box.HSplitTop(18.0f, &Label, &Box);
		Ui()->DoLabel(&Label, Clans.StatusMessage(), 12.0f, TEXTALIGN_MC);
	}
}

void CMenus::RenderClansLanding(CUIRect MainView)
{
	CClans &Clans = GameClient()->m_Clans;
	static CLineInput s_Search;
	static CLineInput s_Code;
	static char s_aSearch[64];
	static char s_aCode[32];
	static int s_Selected = -1;
	s_Search.SetBuffer(s_aSearch, sizeof(s_aSearch));
	s_Code.SetBuffer(s_aCode, sizeof(s_aCode));

	CUIRect Left, Right, Row, Button, Label;
	MainView.VSplitLeft(MainView.w * 0.38f, &Left, &Right);
	Left.VSplitRight(8.0f, &Left, nullptr);

	// Account bar bottom of left
	CUIRect AccountBar;
	Left.HSplitBottom(36.0f, &Left, &AccountBar);
	AccountBar.Draw(ColorRGBA(0, 0, 0, 0.25f), IGraphics::CORNER_ALL, 4.0f);
	AccountBar.Margin(4.0f, &AccountBar);
	AccountBar.VSplitRight(90.0f, &Label, &Button);
	Ui()->DoLabel(&Label, Clans.Nickname(), 13.0f, TEXTALIGN_ML);
	static CButtonContainer s_Logout, s_Recent;
	CUIRect LogoutBtn, RecentBtn;
	Button.VSplitMid(&RecentBtn, &LogoutBtn, 4.0f);
	if(DoButton_Menu(&s_Recent, Localize("Recent"), 0, &RecentBtn))
	{
		Clans.RefreshRecentClans();
		Clans.SetView(CClans::EView::RECENT);
	}
	if(DoButton_Menu(&s_Logout, Localize("Logout"), 0, &LogoutBtn))
		Clans.Logout();

	Left.HSplitTop(24.0f, &Row, &Left);
	Ui()->DoLabel(&Row, Localize("Search"), 14.0f, TEXTALIGN_ML);
	Left.HSplitTop(4.0f, nullptr, &Left);
	Left.HSplitTop(24.0f, &Button, &Left);
	Ui()->DoEditBox(&s_Search, &Button, 14.0f);

	Left.HSplitTop(10.0f, nullptr, &Left);
	Left.HSplitTop(28.0f, &Button, &Left);
	static CButtonContainer s_Create;
	if(DoButton_Menu(&s_Create, Localize("Create clan"), 0, &Button))
		Clans.SetView(CClans::EView::SETUP);

	Left.HSplitTop(10.0f, nullptr, &Left);
	Left.HSplitTop(20.0f, &Label, &Left);
	Ui()->DoLabel(&Label, Localize("Join with code"), 14.0f, TEXTALIGN_ML);
	Left.HSplitTop(4.0f, nullptr, &Left);
	Left.HSplitTop(24.0f, &Row, &Left);
	Row.VSplitRight(70.0f, &Button, &Row);
	Ui()->DoEditBox(&s_Code, &Button, 14.0f);
	static CButtonContainer s_JoinCode;
	if(DoButton_Menu(&s_JoinCode, Localize("Join"), 0, &Row) && s_aCode[0] && !Clans.IsBusy())
		Clans.JoinCode(s_aCode);

	static bool s_ShowApplyPopup = false;
	static char s_aApplyClanId[64] = "";
	static char s_aApplyText[288] = "";
	static CLineInput s_ApplyInput;
	s_ApplyInput.SetBuffer(s_aApplyText, sizeof(s_aApplyText));

	if(s_ShowApplyPopup)
	{
		CUIRect Overlay = MainView;
		Overlay.Draw(ColorRGBA(0, 0, 0, 0.55f), IGraphics::CORNER_ALL, 0.0f);
		CUIRect Popup;
		Overlay.VMargin(Overlay.w * 0.22f, &Popup);
		Popup.HMargin(Popup.h * 0.28f, &Popup);
		Popup.Draw(ColorRGBA(0.12f, 0.12f, 0.12f, 0.98f), IGraphics::CORNER_ALL, 8.0f);
		Popup.Margin(14.0f, &Popup);
		CUIRect Label, Button, Row;
		Popup.HSplitTop(24.0f, &Label, &Popup);
		Ui()->DoLabel(&Label, Localize("Application text"), 16.0f, TEXTALIGN_MC);
		Popup.HSplitTop(8.0f, nullptr, &Popup);
		Popup.HSplitTop(80.0f, &Button, &Popup);
		Ui()->DoEditBox(&s_ApplyInput, &Button, 14.0f);
		Popup.HSplitTop(12.0f, nullptr, &Popup);
		Popup.HSplitTop(28.0f, &Row, &Popup);
		CUIRect Send, Cancel;
		Row.VSplitMid(&Send, &Cancel, 8.0f);
		static CButtonContainer s_SendApply, s_CancelApply;
		if(DoButton_Menu(&s_SendApply, Localize("Send"), 0, &Send) && !Clans.IsBusy())
		{
			Clans.Apply(s_aApplyClanId, s_aApplyText);
			s_ShowApplyPopup = false;
			s_aApplyText[0] = '\0';
		}
		if(DoButton_Menu(&s_CancelApply, Localize("Cancel"), 0, &Cancel))
		{
			s_ShowApplyPopup = false;
			s_aApplyText[0] = '\0';
		}
		return;
	}

	if(s_Selected >= 0 && s_Selected < (int)Clans.Catalog().size())
	{
		const auto &Entry = Clans.Catalog()[s_Selected];
		Left.HSplitTop(12.0f, nullptr, &Left);
		Left.Draw(ColorRGBA(0, 0, 0, 0.2f), IGraphics::CORNER_ALL, 4.0f);
		CUIRect Preview = Left;
		Preview.Margin(8.0f, &Preview);
		Preview.HSplitTop(20.0f, &Label, &Preview);
		Ui()->DoLabel(&Label, Entry.m_aName, 15.0f, TEXTALIGN_ML);
		Preview.HSplitTop(40.0f, &Label, &Preview);
		Ui()->DoLabel(&Label, Entry.m_aDescription, 12.0f, TEXTALIGN_TL);
		Preview.HSplitTop(8.0f, nullptr, &Preview);
		Preview.HSplitTop(28.0f, &Button, &Preview);
		static CButtonContainer s_JoinOpen, s_Apply;
		if(!str_comp(Entry.m_aJoinPolicy, "open"))
		{
			if(DoButton_Menu(&s_JoinOpen, Localize("Join"), 0, &Button) && !Clans.IsBusy() && !Clans.InClan())
				Clans.Join(Entry.m_aClanId);
		}
		else if(!str_comp(Entry.m_aJoinPolicy, "request"))
		{
			if(DoButton_Menu(&s_Apply, Localize("Apply"), 0, &Button) && !Clans.IsBusy() && !Clans.InClan())
			{
				str_copy(s_aApplyClanId, Entry.m_aClanId, sizeof(s_aApplyClanId));
				s_aApplyText[0] = '\0';
				s_ShowApplyPopup = true;
			}
		}
	}

	if(Clans.InClan())
	{
		CUIRect Back;
		MainView.HSplitTop(0.0f, nullptr, nullptr);
		Right.HSplitTop(28.0f, &Back, &Right);
		Back.VSplitRight(120.0f, nullptr, &Back);
		static CButtonContainer s_BackClan;
		if(DoButton_Menu(&s_BackClan, Localize("Back to clan"), 0, &Back))
			Clans.SetView(CClans::EView::CLAN);
	}

	Right.HSplitTop(24.0f, &Label, &Right);
	Ui()->DoLabel(&Label, Localize("Clan catalog"), 16.0f, TEXTALIGN_ML);
	Right.HSplitTop(6.0f, nullptr, &Right);

	static CScrollRegion s_Scroll;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams Params;
	s_Scroll.Begin(&Right, &ScrollOffset, &Params);
	Right.y += ScrollOffset.y;
	int Index = 0;
	for(const auto &Entry : Clans.Catalog())
	{
		if(s_aSearch[0] && !str_find_nocase(Entry.m_aName, s_aSearch) && !str_find_nocase(Entry.m_aTag, s_aSearch))
		{
			Index++;
			continue;
		}
		CUIRect Item;
		Right.HSplitTop(32.0f, &Item, &Right);
		if(s_Scroll.AddRect(Item))
		{
			const bool Selected = s_Selected == Index;
			Item.Draw(Selected ? ColorRGBA(1, 1, 1, 0.2f) : ColorRGBA(0, 0, 0, 0.15f), IGraphics::CORNER_ALL, 3.0f);
			CUIRect Name, Meta;
			Item.Margin(4.0f, &Item);
			Item.VSplitRight(110.0f, &Name, &Meta);
			char aLine[96];
			str_format(aLine, sizeof(aLine), "[%s] %s", Entry.m_aTag, Entry.m_aName);
			Ui()->DoLabel(&Name, aLine, 13.0f, TEXTALIGN_ML);
			str_format(aLine, sizeof(aLine), "%d/%d · %s", Entry.m_OnlineCount, Entry.m_MemberCount, Entry.m_aJoinPolicy);
			Ui()->DoLabel(&Meta, aLine, 11.0f, TEXTALIGN_MR);
			if(Ui()->DoButtonLogic(&Clans.Catalog()[Index], 0, &Item, BUTTONFLAG_LEFT))
				s_Selected = Index;
		}
		Right.HSplitTop(4.0f, nullptr, &Right);
		Index++;
	}
	s_Scroll.End();

	if(Clans.ErrorMessage()[0])
	{
		CUIRect Err;
		MainView.HSplitBottom(22.0f, nullptr, &Err);
		TextRender()->TextColor(1, 0.4f, 0.4f, 1);
		Ui()->DoLabel(&Err, Clans.ErrorMessage(), 12.0f, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
}

void CMenus::RenderClansSetup(CUIRect MainView)
{
	CClans &Clans = GameClient()->m_Clans;
	static CLineInput s_Name, s_Tag, s_Desc;
	static char s_aName[64], s_aTag[16], s_aDesc[256];
	static int s_Policy = 0; // 0 open 1 request 2 closed
	static int s_MaxMembers = 50;
	static int s_IconId = 0;
	static bool s_Init = false;
	if(!s_Init)
	{
		s_aName[0] = s_aTag[0] = s_aDesc[0] = '\0';
		str_copy(s_aTag, g_Config.m_PlayerClan, sizeof(s_aTag));
		s_IconId = 0;
		s_Init = true;
	}
	s_Name.SetBuffer(s_aName, sizeof(s_aName));
	s_Tag.SetBuffer(s_aTag, sizeof(s_aTag));
	s_Desc.SetBuffer(s_aDesc, sizeof(s_aDesc));

	CUIRect Box, Label, Button, Row;
	MainView.VMargin(MainView.w * 0.2f, &Box);
	Box.Draw(ColorRGBA(0, 0, 0, 0.35f), IGraphics::CORNER_ALL, 8.0f);
	Box.Margin(16.0f, &Box);

	Box.HSplitTop(24.0f, &Label, &Box);
	Ui()->DoLabel(&Label, Localize("Create clan"), 18.0f, TEXTALIGN_MC);

	auto Field = [&](const char *pLabel, CLineInput *pInput) {
		Box.HSplitTop(8.0f, nullptr, &Box);
		Box.HSplitTop(18.0f, &Label, &Box);
		Ui()->DoLabel(&Label, pLabel, 13.0f, TEXTALIGN_ML);
		Box.HSplitTop(4.0f, nullptr, &Box);
		Box.HSplitTop(24.0f, &Button, &Box);
		Ui()->DoEditBox(pInput, &Button, 14.0f);
	};
	Field(Localize("Name"), &s_Name);
	Field(Localize("Tag"), &s_Tag);
	Box.HSplitTop(4.0f, nullptr, &Box);
	Box.HSplitTop(24.0f, &Button, &Box);
	static CButtonContainer s_UseCurrent;
	if(DoButton_Menu(&s_UseCurrent, Localize("Use current Tee clan"), 0, &Button))
		str_copy(s_aTag, g_Config.m_PlayerClan, sizeof(s_aTag));
	Field(Localize("Description"), &s_Desc);

	Box.HSplitTop(10.0f, nullptr, &Box);
	Box.HSplitTop(18.0f, &Label, &Box);
	Ui()->DoLabel(&Label, Localize("Icon"), 13.0f, TEXTALIGN_ML);
	Box.HSplitTop(4.0f, nullptr, &Box);
	Box.HSplitTop(28.0f, &Row, &Box);
	static CButtonContainer s_aIcons[8];
	static const unsigned s_aIconColors[8] = {
		0xFFFFFF, 0xFF5555, 0x55FF55, 0x5555FF, 0xFFFF55, 0xFF55FF, 0x55FFFF, 0xFFAA33};
	for(int i = 0; i < 8; i++)
	{
		CUIRect IconBtn;
		Row.VSplitLeft(Row.h + 4.0f, &IconBtn, &Row);
		IconBtn.VSplitRight(4.0f, &IconBtn, nullptr);
		const float R = ((s_aIconColors[i] >> 16) & 0xFF) / 255.0f;
		const float G = ((s_aIconColors[i] >> 8) & 0xFF) / 255.0f;
		const float B = (s_aIconColors[i] & 0xFF) / 255.0f;
		IconBtn.Draw(ColorRGBA(R, G, B, s_IconId == i ? 1.0f : 0.45f), IGraphics::CORNER_ALL, 4.0f);
		if(Ui()->DoButtonLogic(&s_aIcons[i], s_IconId == i, &IconBtn, BUTTONFLAG_LEFT))
			s_IconId = i;
	}

	Box.HSplitTop(10.0f, nullptr, &Box);
	Box.HSplitTop(20.0f, &Label, &Box);
	Ui()->DoLabel(&Label, Localize("Join policy"), 13.0f, TEXTALIGN_ML);
	Box.HSplitTop(4.0f, nullptr, &Box);
	Box.HSplitTop(26.0f, &Row, &Box);
	static CButtonContainer s_Open, s_Req, s_Closed;
	CUIRect A, B, C;
	Row.VSplitMid(&A, &Row, 4.0f);
	Row.VSplitMid(&B, &C, 4.0f);
	if(DoButton_Menu(&s_Open, Localize("Open"), s_Policy == 0, &A))
		s_Policy = 0;
	if(DoButton_Menu(&s_Req, Localize("Request"), s_Policy == 1, &B))
		s_Policy = 1;
	if(DoButton_Menu(&s_Closed, Localize("Closed"), s_Policy == 2, &C))
		s_Policy = 2;

	Box.HSplitTop(10.0f, nullptr, &Box);
	Box.HSplitTop(20.0f, &Button, &Box);
	Ui()->DoScrollbarOption(&s_MaxMembers, &s_MaxMembers, &Button, Localize("Max members"), 2, 200);

	Box.HSplitTop(16.0f, nullptr, &Box);
	Box.HSplitTop(30.0f, &Row, &Box);
	CUIRect CreateBtn, CancelBtn;
	Row.VSplitMid(&CreateBtn, &CancelBtn, 8.0f);
	static CButtonContainer s_Create, s_Cancel;
	const char *apPolicy[] = {"open", "request", "closed"};
	if(DoButton_Menu(&s_Create, Localize("Create"), 0, &CreateBtn) && !Clans.IsBusy())
	{
		Clans.CreateClan(s_aName, s_aTag, s_aDesc, s_IconId, s_aIconColors[s_IconId], apPolicy[s_Policy], s_MaxMembers);
		s_Init = false;
	}
	if(DoButton_Menu(&s_Cancel, Localize("Cancel"), 0, &CancelBtn))
	{
		s_Init = false;
		Clans.SetView(Clans.InClan() ? CClans::EView::CLAN : CClans::EView::LANDING);
	}

	if(Clans.ErrorMessage()[0])
	{
		Box.HSplitTop(10.0f, nullptr, &Box);
		Box.HSplitTop(40.0f, &Label, &Box);
		TextRender()->TextColor(1, 0.4f, 0.4f, 1);
		Ui()->DoLabel(&Label, Clans.ErrorMessage(), 13.0f, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		if(!str_comp(Clans.ErrorMessage(), Localize("Please open a ticket")))
		{
			Box.HSplitTop(4.0f, nullptr, &Box);
			Box.HSplitTop(22.0f, &Button, &Box);
			static CButtonContainer s_DiscordSetup;
			if(DoButton_Menu(&s_DiscordSetup, "discord.gg/bestclient", 0, &Button))
				Client()->ViewLink("https://discord.gg/bestclient");
		}
	}
}

void CMenus::RenderClansPage(CUIRect MainView)
{
	CClans &Clans = GameClient()->m_Clans;
	const auto &Clan = Clans.Clan();

	CUIRect TopBar, Left, Right, Label, Button, Row;
	MainView.HSplitTop(32.0f, &TopBar, &MainView);

	CUIRect B1, B2, B3;
	TopBar.VSplitRight(100.0f, &TopBar, &B3);
	TopBar.VSplitRight(8.0f, &TopBar, nullptr);
	TopBar.VSplitRight(120.0f, &TopBar, &B2);
	TopBar.VSplitRight(8.0f, &TopBar, nullptr);
	TopBar.VSplitRight(100.0f, &TopBar, &B1);
	static CButtonContainer s_Catalog, s_Ann, s_Apps;
	if(DoButton_Menu(&s_Catalog, Localize("Catalog"), 0, &B1))
	{
		Clans.RefreshCatalog();
		Clans.SetView(CClans::EView::BROWSE);
	}
	if(DoButton_Menu(&s_Ann, Localize("Announcements"), 0, &B2))
	{
		Clans.RefreshAnnouncements();
		Clans.SetView(CClans::EView::ANNOUNCEMENTS);
	}
	const bool CanApps = !str_comp(Clan.m_aJoinPolicy, "request") &&
			     (Clans.Role() == CClans::ERole::PRESIDENT || Clans.Role() == CClans::ERole::VICE_PRESIDENT || Clans.Role() == CClans::ERole::VETERAN);
	if(CanApps && DoButton_Menu(&s_Apps, Localize("Applications"), 0, &B3))
	{
		Clans.RefreshApplications();
		Clans.SetView(CClans::EView::APPLICATIONS);
	}

	MainView.VSplitLeft(MainView.w * 0.36f, &Left, &Right);
	Left.VSplitRight(8.0f, &Left, nullptr);
	Left.Draw(ColorRGBA(0, 0, 0, 0.3f), IGraphics::CORNER_ALL, 6.0f);
	Left.Margin(10.0f, &Left);

	Left.HSplitTop(26.0f, &Label, &Left);
	Ui()->DoLabel(&Label, Clan.m_aName[0] ? Clan.m_aName : Localize("Loading..."), 18.0f, TEXTALIGN_ML);
	Left.HSplitTop(18.0f, &Label, &Left);
	char aMeta[80];
	str_format(aMeta, sizeof(aMeta), "[%s] · %s", Clan.m_aTag, Clan.m_aJoinPolicy);
	Ui()->DoLabel(&Label, aMeta, 13.0f, TEXTALIGN_ML);
	Left.HSplitTop(8.0f, nullptr, &Left);
	Left.HSplitTop(50.0f, &Label, &Left);
	Ui()->DoLabel(&Label, Clan.m_aDescription, 12.0f, TEXTALIGN_TL);

	if(Clan.m_HasInviteCode && (Clans.Role() == CClans::ERole::PRESIDENT || Clans.Role() == CClans::ERole::VICE_PRESIDENT))
	{
		Left.HSplitTop(8.0f, nullptr, &Left);
		Left.HSplitTop(20.0f, &Label, &Left);
		str_format(aMeta, sizeof(aMeta), "%s: %s", Localize("Invite"), Clan.m_aInviteCode);
		Ui()->DoLabel(&Label, aMeta, 12.0f, TEXTALIGN_ML);
		Left.HSplitTop(4.0f, nullptr, &Left);
		Left.HSplitTop(26.0f, &Row, &Left);
		CUIRect Copy, Rotate;
		Row.VSplitMid(&Copy, &Rotate, 4.0f);
		static CButtonContainer s_Copy, s_Rotate;
		if(DoButton_Menu(&s_Copy, Localize("Copy"), 0, &Copy))
			Input()->SetClipboardText(Clan.m_aInviteCode);
		if(DoButton_Menu(&s_Rotate, Localize("Rotate"), 0, &Rotate) && !Clans.IsBusy())
			Clans.RotateInvite();
	}

	int Online = 0;
	for(const auto &M : Clan.m_vMembers)
		if(M.m_Online)
			Online++;
	Left.HSplitTop(10.0f, nullptr, &Left);
	Left.HSplitTop(18.0f, &Label, &Left);
	str_format(aMeta, sizeof(aMeta), "Main online %d/%d", Online, (int)Clan.m_vMembers.size());
	Ui()->DoLabel(&Label, aMeta, 12.0f, TEXTALIGN_ML);

	CUIRect Acc;
	Left.HSplitBottom(70.0f, &Left, &Acc);
	Acc.HSplitTop(28.0f, &Button, &Acc);
	static CButtonContainer s_Leave;
	if(DoButton_Menu(&s_Leave, Localize("Leave"), 0, &Button) && !Clans.IsBusy())
		Clans.Leave();
	if(Clans.Role() == CClans::ERole::PRESIDENT)
	{
		Acc.HSplitTop(4.0f, nullptr, &Acc);
		Acc.HSplitTop(28.0f, &Button, &Acc);
		static CButtonContainer s_Disband;
		if(DoButton_Menu(&s_Disband, Localize("Disband"), 0, &Button) && !Clans.IsBusy())
			Clans.Disband();
	}
	Acc.HSplitTop(4.0f, nullptr, &Acc);
	CUIRect Nick, Logout;
	Acc.HSplitTop(24.0f, &Row, &Acc);
	Row.VSplitRight(80.0f, &Nick, &Logout);
	Ui()->DoLabel(&Nick, Clans.Nickname(), 12.0f, TEXTALIGN_ML);
	static CButtonContainer s_Logout2, s_Recent2;
	if(DoButton_Menu(&s_Logout2, Localize("Logout"), 0, &Logout))
		Clans.Logout();

	// Members
	Right.HSplitTop(22.0f, &Label, &Right);
	Ui()->DoLabel(&Label, Localize("Main"), 15.0f, TEXTALIGN_ML);
	Right.HSplitTop(4.0f, nullptr, &Right);

	static CScrollRegion s_Scroll;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams Params;
	s_Scroll.Begin(&Right, &ScrollOffset, &Params);
	Right.y += ScrollOffset.y;
	static std::vector<CButtonContainer> s_vKickButtons;
	static std::vector<CButtonContainer> s_vPromoteButtons;
	static std::vector<CButtonContainer> s_vDemoteButtons;
	static int s_BanHours = 24;
	if(s_vKickButtons.size() < Clan.m_vMembers.size())
	{
		s_vKickButtons.resize(Clan.m_vMembers.size());
		s_vPromoteButtons.resize(Clan.m_vMembers.size());
		s_vDemoteButtons.resize(Clan.m_vMembers.size());
	}

	if(Clans.Role() == CClans::ERole::PRESIDENT)
	{
		CUIRect BanRow;
		Right.HSplitTop(24.0f, &BanRow, &Right);
		if(s_Scroll.AddRect(BanRow))
		{
			CUIRect L;
			BanRow.VSplitLeft(110.0f, &L, &BanRow);
			Ui()->DoLabel(&L, Localize("Kick ban:"), 11.0f, TEXTALIGN_ML);
			static const int s_aBanOpts[] = {0, 24, 48, 72, -1};
			static const char *s_apBanLabels[] = {"0h", "24h", "48h", "72h", "perm"};
			static CButtonContainer s_aBanBtns[5];
			for(int i = 0; i < 5; i++)
			{
				CUIRect B;
				BanRow.VSplitLeft(42.0f, &B, &BanRow);
				BanRow.VSplitLeft(3.0f, nullptr, &BanRow);
				if(DoButton_Menu(&s_aBanBtns[i], s_apBanLabels[i], s_BanHours == s_aBanOpts[i], &B))
					s_BanHours = s_aBanOpts[i];
			}
		}
		Right.HSplitTop(4.0f, nullptr, &Right);
	}

	size_t MemberIndex = 0;
	for(const auto &M : Clan.m_vMembers)
	{
		CUIRect Item;
		Right.HSplitTop(42.0f, &Item, &Right);
		if(s_Scroll.AddRect(Item))
		{
			Item.Draw(ColorRGBA(0, 0, 0, 0.18f), IGraphics::CORNER_ALL, 3.0f);
			Item.Margin(4.0f, &Item);
			CUIRect Tee, Name, Status, Actions;
			Item.VSplitLeft(36.0f, &Tee, &Item);
			Item.VSplitLeft(4.0f, nullptr, &Item);
			Item.VSplitRight(170.0f, &Name, &Actions);
			Name.VSplitRight(150.0f, &Name, &Status);
			RenderClanMemberTee(GameClient(), RenderTools(), Tee, M.m_Skin);
			char aName[64];
			str_format(aName, sizeof(aName), "%s (%s)", M.m_aNickname, RoleLabel(M.m_Role));
			Ui()->DoLabel(&Name, aName, 12.0f, TEXTALIGN_ML);
			if(M.m_Online)
			{
				char aSt[96];
				if(M.m_aMap[0])
					str_format(aSt, sizeof(aSt), Localize("playing %s %d/%d"), M.m_aMap, M.m_Players, M.m_MaxPlayers);
				else
					str_copy(aSt, Localize("online"), sizeof(aSt));
				Ui()->DoLabel(&Status, aSt, 11.0f, TEXTALIGN_MR);
			}
			else
				Ui()->DoLabel(&Status, Localize("offline"), 11.0f, TEXTALIGN_MR);

			const bool IsSelf = !str_comp(M.m_aUserId, Clans.UserId());
			const bool CanModerate = !IsSelf && (Clans.Role() == CClans::ERole::PRESIDENT || Clans.Role() == CClans::ERole::VICE_PRESIDENT || Clans.Role() == CClans::ERole::VETERAN);
			if(CanModerate)
			{
				CUIRect KickBtn, PromoteBtn, DemoteBtn;
				Actions.VSplitRight(44.0f, &Actions, &KickBtn);
				Actions.VSplitRight(3.0f, &Actions, nullptr);
				Actions.VSplitRight(44.0f, &Actions, &DemoteBtn);
				Actions.VSplitRight(3.0f, &Actions, nullptr);
				Actions.VSplitRight(44.0f, &Actions, &PromoteBtn);
				const bool CanPromote = Clans.Role() == CClans::ERole::PRESIDENT || Clans.Role() == CClans::ERole::VICE_PRESIDENT;
				const bool CanDemote = Clans.Role() == CClans::ERole::PRESIDENT;
				if(CanPromote && DoButton_Menu(&s_vPromoteButtons[MemberIndex], "+", 0, &PromoteBtn) && !Clans.IsBusy())
					Clans.Promote(M.m_aUserId);
				if(CanDemote && DoButton_Menu(&s_vDemoteButtons[MemberIndex], "-", 0, &DemoteBtn) && !Clans.IsBusy())
					Clans.Demote(M.m_aUserId);
				if(DoButton_Menu(&s_vKickButtons[MemberIndex], Localize("Kick"), 0, &KickBtn) && !Clans.IsBusy())
					Clans.Kick(M.m_aUserId, Clans.Role() == CClans::ERole::PRESIDENT ? s_BanHours : 0);
			}
		}
		Right.HSplitTop(3.0f, nullptr, &Right);
		MemberIndex++;
	}

	Right.HSplitTop(10.0f, nullptr, &Right);
	Right.HSplitTop(20.0f, &Label, &Right);
	std::vector<CClans::SUnleashedPlayer> vUnleashed;
	Clans.CollectUnleashed(&vUnleashed);
	char aUnlHead[64];
	str_format(aUnlHead, sizeof(aUnlHead), "%s (%d)", Localize("Unleashed"), (int)vUnleashed.size());
	Ui()->DoLabel(&Label, aUnlHead, 13.0f, TEXTALIGN_ML);
	for(const auto &U : vUnleashed)
	{
		CUIRect Item;
		Right.HSplitTop(26.0f, &Item, &Right);
		if(s_Scroll.AddRect(Item))
		{
			Item.Draw(ColorRGBA(0.2f, 0.15f, 0.05f, 0.25f), IGraphics::CORNER_ALL, 3.0f);
			Item.Margin(4.0f, &Item);
			CUIRect Name, Meta;
			Item.VSplitRight(180.0f, &Name, &Meta);
			Ui()->DoLabel(&Name, U.m_aName, 12.0f, TEXTALIGN_ML);
			char aMeta[128];
			str_format(aMeta, sizeof(aMeta), Localize("playing %s %d/%d"), U.m_aMap, U.m_Players, U.m_MaxPlayers);
			Ui()->DoLabel(&Meta, aMeta, 11.0f, TEXTALIGN_MR);
		}
		Right.HSplitTop(2.0f, nullptr, &Right);
	}
	s_Scroll.End();

	if(Clans.ErrorMessage()[0])
	{
		CUIRect Err;
		MainView.HSplitBottom(20.0f, nullptr, &Err);
		TextRender()->TextColor(1, 0.4f, 0.4f, 1);
		Ui()->DoLabel(&Err, Clans.ErrorMessage(), 12.0f, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
}

void CMenus::RenderClansApplications(CUIRect MainView)
{
	CClans &Clans = GameClient()->m_Clans;
	CUIRect Top, Label, Button;
	MainView.HSplitTop(30.0f, &Top, &MainView);
	Top.VSplitRight(100.0f, &Label, &Button);
	Ui()->DoLabel(&Label, Localize("Applications"), 16.0f, TEXTALIGN_ML);
	static CButtonContainer s_Back;
	if(DoButton_Menu(&s_Back, Localize("Back"), 0, &Button))
		Clans.SetView(CClans::EView::CLAN);

	static CScrollRegion s_Scroll;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams Params;
	s_Scroll.Begin(&MainView, &ScrollOffset, &Params);
	MainView.y += ScrollOffset.y;
	static std::vector<CButtonContainer> s_vAccept;
	static std::vector<CButtonContainer> s_vReject;
	if(s_vAccept.size() < Clans.Applications().size())
	{
		s_vAccept.resize(Clans.Applications().size());
		s_vReject.resize(Clans.Applications().size());
	}
	size_t AppIndex = 0;
	for(const auto &App : Clans.Applications())
	{
		CUIRect Item, Text, Actions, Accept, Reject;
		MainView.HSplitTop(70.0f, &Item, &MainView);
		if(s_Scroll.AddRect(Item))
		{
			Item.Draw(ColorRGBA(0, 0, 0, 0.25f), IGraphics::CORNER_ALL, 4.0f);
			Item.Margin(8.0f, &Item);
			Item.HSplitTop(18.0f, &Label, &Item);
			Ui()->DoLabel(&Label, App.m_aNickname, 14.0f, TEXTALIGN_ML);
			Item.HSplitBottom(28.0f, &Text, &Actions);
			Ui()->DoLabel(&Text, App.m_aText, 12.0f, TEXTALIGN_TL);
			Actions.VSplitRight(90.0f, &Actions, &Reject);
			Actions.VSplitRight(8.0f, &Actions, nullptr);
			Actions.VSplitRight(90.0f, nullptr, &Accept);
			if(DoButton_Menu(&s_vAccept[AppIndex], Localize("Accept"), 0, &Accept) && !Clans.IsBusy())
				Clans.ApproveApplication(App.m_aId);
			if(DoButton_Menu(&s_vReject[AppIndex], Localize("Reject"), 0, &Reject) && !Clans.IsBusy())
				Clans.RejectApplication(App.m_aId);
		}
		MainView.HSplitTop(6.0f, nullptr, &MainView);
		AppIndex++;
	}
	s_Scroll.End();
}

void CMenus::RenderClansAnnouncements(CUIRect MainView)
{
	CClans &Clans = GameClient()->m_Clans;
	CUIRect Top, Label, Button, InputRow;
	MainView.HSplitTop(30.0f, &Top, &MainView);
	Top.VSplitRight(100.0f, &Label, &Button);
	Ui()->DoLabel(&Label, Localize("Announcements"), 16.0f, TEXTALIGN_ML);
	static CButtonContainer s_Back;
	if(DoButton_Menu(&s_Back, Localize("Back"), 0, &Button))
		Clans.SetView(CClans::EView::CLAN);

	const bool CanPost = Clans.Role() == CClans::ERole::PRESIDENT || Clans.Role() == CClans::ERole::VICE_PRESIDENT;
	if(CanPost)
	{
		MainView.HSplitBottom(40.0f, &MainView, &InputRow);
		static CLineInput s_Text;
		static char s_aText[500];
		s_Text.SetBuffer(s_aText, sizeof(s_aText));
		CUIRect Edit, Send;
		InputRow.VSplitRight(90.0f, &Edit, &Send);
		Ui()->DoEditBox(&s_Text, &Edit, 14.0f);
		static CButtonContainer s_Send;
		if(DoButton_Menu(&s_Send, Localize("Send"), 0, &Send) && s_aText[0] && !Clans.IsBusy())
		{
			Clans.PostAnnouncement(s_aText);
			s_aText[0] = '\0';
		}
	}

	static CScrollRegion s_Scroll;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams Params;
	s_Scroll.Begin(&MainView, &ScrollOffset, &Params);
	MainView.y += ScrollOffset.y;
	for(const auto &Ann : Clans.Announcements())
	{
		CUIRect Item;
		MainView.HSplitTop(55.0f, &Item, &MainView);
		if(s_Scroll.AddRect(Item))
		{
			Item.Draw(ColorRGBA(0, 0, 0, 0.2f), IGraphics::CORNER_ALL, 4.0f);
			Item.Margin(6.0f, &Item);
			char aHead[96];
			str_format(aHead, sizeof(aHead), "%s · %s", Ann.m_aAuthorNick, Ann.m_aCreatedAt);
			Item.HSplitTop(16.0f, &Label, &Item);
			Ui()->DoLabel(&Label, aHead, 11.0f, TEXTALIGN_ML);
			Ui()->DoLabel(&Item, Ann.m_aText, 13.0f, TEXTALIGN_TL);
		}
		MainView.HSplitTop(4.0f, nullptr, &MainView);
	}
	s_Scroll.End();
}

void CMenus::RenderClansRecent(CUIRect MainView)
{
	CClans &Clans = GameClient()->m_Clans;
	CUIRect Top, Label, Button;
	MainView.HSplitTop(30.0f, &Top, &MainView);
	Top.VSplitRight(100.0f, &Label, &Button);
	Ui()->DoLabel(&Label, Localize("Recent clans"), 16.0f, TEXTALIGN_ML);
	static CButtonContainer s_Back;
	if(DoButton_Menu(&s_Back, Localize("Back"), 0, &Button))
		Clans.SetView(Clans.InClan() ? CClans::EView::CLAN : CClans::EView::LANDING);

	static std::vector<CButtonContainer> s_vRejoin;
	if(s_vRejoin.size() < Clans.RecentClans().size())
		s_vRejoin.resize(Clans.RecentClans().size());
	size_t RecentIndex = 0;
	for(const auto &R : Clans.RecentClans())
	{
		CUIRect Item, Act;
		MainView.HSplitTop(36.0f, &Item, &MainView);
		Item.Draw(ColorRGBA(0, 0, 0, 0.2f), IGraphics::CORNER_ALL, 4.0f);
		Item.Margin(6.0f, &Item);
		Item.VSplitRight(120.0f, &Label, &Act);
		char aLine[96];
		str_format(aLine, sizeof(aLine), "[%s] %s", R.m_aTag, R.m_aName);
		Ui()->DoLabel(&Label, aLine, 13.0f, TEXTALIGN_ML);
		if(R.m_WasPresident && !Clans.InClan())
		{
			if(DoButton_Menu(&s_vRejoin[RecentIndex], Localize("Return as President"), 0, &Act) && !Clans.IsBusy())
				Clans.RejoinAsPresident(R.m_aClanId);
		}
		MainView.HSplitTop(4.0f, nullptr, &MainView);
		RecentIndex++;
	}
}
