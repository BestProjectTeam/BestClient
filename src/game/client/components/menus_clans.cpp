/* Copyright © 2026 BestProject Team */
#include <base/color.h>
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

#include <algorithm>
#include <cstdlib>
#include <cstring>
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

static const char *ClanIconGlyph(int IconId)
{
	static const char *s_apGlyphs[8] = {
		FontIcon::STAR,
		FontIcon::HEART,
		FontIcon::BOMB,
		FontIcon::FLAG_CHECKERED,
		FontIcon::HOUSE,
		FontIcon::KEY,
		FontIcon::SNAKE,
		FontIcon::CHESS_KING,
	};
	return s_apGlyphs[std::clamp(IconId, 0, 7)];
}

static ColorRGBA ClanColorRgb(unsigned Color)
{
	Color &= 0xFFFFFF;
	return ColorRGBA(((Color >> 16) & 0xFF) / 255.0f, ((Color >> 8) & 0xFF) / 255.0f, (Color & 0xFF) / 255.0f, 1.0f);
}

static void RenderClanIcon(CUi *pUi, ITextRender *pTextRender, CUIRect Box, int IconId, unsigned Color)
{
	pTextRender->SetFontPreset(EFontPreset::ICON_FONT);
	pTextRender->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	pTextRender->TextColor(ClanColorRgb(Color));
	pUi->DoLabel(&Box, ClanIconGlyph(IconId), minimum(Box.w, Box.h) * 0.7f, TEXTALIGN_MC);
	pTextRender->TextColor(pTextRender->DefaultTextColor());
	pTextRender->SetRenderFlags(0);
	pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);
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

static void RenderOwnTee(CGameClient *pGameClient, CRenderTools *pRenderTools, CUIRect TeeBox)
{
	CClans::SSkin Skin;
	str_copy(Skin.m_aName, g_Config.m_ClPlayerSkin, sizeof(Skin.m_aName));
	Skin.m_ColorBody = g_Config.m_ClPlayerColorBody;
	Skin.m_ColorFeet = g_Config.m_ClPlayerColorFeet;
	Skin.m_UseCustomColor = g_Config.m_ClPlayerUseCustomColor != 0;
	RenderClanMemberTee(pGameClient, pRenderTools, TeeBox, Skin);
}

static bool DoClansIconBtn(CUi *pUi, CButtonContainer *pId, const char *pIcon, const CUIRect *pRect)
{
	return pUi->DoButton_FontIcon(pId, pIcon, 0, pRect, BUTTONFLAG_LEFT) != 0;
}

static void DrawClansAccountBarRect(CGameClient *pGameClient, CRenderTools *pRenderTools, CUi *pUi, CClans &Clans, CUIRect Acc)
{
	Acc.Draw(ColorRGBA(0, 0, 0, 0.28f), IGraphics::CORNER_ALL, 4.0f);
	Acc.Margin(3.0f, &Acc);

	CUIRect Tee, Text, LogoutBtn;
	const float LogoutSize = Acc.h;
	Acc.VSplitRight(LogoutSize, &Acc, &LogoutBtn);
	Acc.VSplitRight(4.0f, &Acc, nullptr);
	Acc.VSplitLeft(Acc.h, &Tee, &Text);
	Text.VSplitLeft(4.0f, nullptr, &Text);

	RenderOwnTee(pGameClient, pRenderTools, Tee);

	CUIRect Nick, Sub;
	Text.HSplitMid(&Nick, &Sub);
	pUi->DoLabel(&Nick, Clans.Nickname(), 12.0f, TEXTALIGN_ML);
	{
		char aSub[48];
		if(Clans.InClan() && Clans.Clan().m_aTag[0])
			str_format(aSub, sizeof(aSub), "[%s]", Clans.Clan().m_aTag);
		else if(Clans.InClan() && Clans.ClanId()[0])
			str_copy(aSub, Localize("In a clan"), sizeof(aSub));
		else
			str_copy(aSub, Localize("no clan"), sizeof(aSub));
		pUi->DoLabel(&Sub, aSub, 10.0f, TEXTALIGN_ML);
	}

	static CButtonContainer s_Logout;
	if(DoClansIconBtn(pUi, &s_Logout, FontIcon::RIGHT_FROM_BRACKET, &LogoutBtn))
		Clans.Logout();
}

static void DrawClansAccountBar(CGameClient *pGameClient, CRenderTools *pRenderTools, CUi *pUi, CClans &Clans, CUIRect *pMainView)
{
	CUIRect Acc;
	pMainView->HSplitBottom(34.0f, pMainView, &Acc);
	const float AccW = 210.0f;
	if(Acc.w > AccW)
		Acc.VSplitLeft(Acc.w - AccW, nullptr, &Acc);
	DrawClansAccountBarRect(pGameClient, pRenderTools, pUi, Clans, Acc);
}

void CMenus::RenderClans(CUIRect MainView)
{
	CClans &Clans = GameClient()->m_Clans;

	{
		CUIRect ToastArea = MainView;
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
	MainView.Margin(8.0f, &MainView);

	if(!Clans.IsLoggedIn())
	{
		RenderClansAuth(MainView);
		return;
	}

	// Errors overlay without changing layout (no Loading — it caused a jump every poll).
	if(Clans.ErrorMessage()[0])
	{
		CUIRect Msg = MainView;
		Msg.HSplitTop(18.0f, &Msg, nullptr);
		Msg.VSplitRight(120.0f, &Msg, nullptr);
		TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
		Ui()->DoLabel(&Msg, Clans.ErrorMessage(), 12.0f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
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
	case CClans::EView::PREVIEW:
		RenderClansPreview(MainView);
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
	}
}

void CMenus::RenderClansLanding(CUIRect MainView)
{
	CClans &Clans = GameClient()->m_Clans;
	static CLineInput s_Search;
	static CLineInput s_Code;
	static char s_aSearch[64];
	static char s_aCode[32];
	s_Search.SetBuffer(s_aSearch, sizeof(s_aSearch));
	s_Code.SetBuffer(s_aCode, sizeof(s_aCode));
	s_Search.SetEmptyText(Localize("Search clans"));
	s_Code.SetEmptyText(Localize("Join with code"));

	CUIRect Left, Right, Row, Button, Label;
	MainView.VSplitLeft(MainView.w * 0.34f, &Left, &Right);
	Left.VSplitRight(8.0f, &Left, nullptr);

	// Left sidebar — mockup layout
	Left.Draw(ColorRGBA(0, 0, 0, 0.22f), IGraphics::CORNER_ALL, 6.0f);
	Left.Margin(10.0f, &Left);

	// Account (top)
	CUIRect Acc;
	Left.HSplitTop(36.0f, &Acc, &Left);
	DrawClansAccountBarRect(GameClient(), RenderTools(), Ui(), Clans, Acc);
	Left.HSplitTop(8.0f, nullptr, &Left);

	// Search
	CUIRect SearchBox;
	Left.HSplitTop(26.0f, &SearchBox, &Left);
	Ui()->DoEditBox(&s_Search, &SearchBox, 13.0f);
	Left.HSplitTop(8.0f, nullptr, &Left);

	// Join with code + Join (one row)
	CUIRect CodeInput, JoinBtn;
	Left.HSplitTop(26.0f, &Row, &Left);
	Row.VSplitRight(64.0f, &CodeInput, &JoinBtn);
	CodeInput.VSplitRight(6.0f, &CodeInput, nullptr);
	Ui()->DoEditBox(&s_Code, &CodeInput, 13.0f);
	static CButtonContainer s_JoinCode;
	if(DoButton_Menu(&s_JoinCode, Localize("Join"), 0, &JoinBtn) && s_aCode[0] && !Clans.IsBusy())
		Clans.JoinCode(s_aCode);
	Left.HSplitTop(8.0f, nullptr, &Left);

	// Create clan
	CUIRect CreateBtn;
	Left.HSplitTop(28.0f, &CreateBtn, &Left);
	static CButtonContainer s_Create;
	if(DoButton_Menu(&s_Create, Localize("+ Create clan"), 0, &CreateBtn))
		Clans.SetView(CClans::EView::SETUP);

	if(Clans.InClan())
	{
		Left.HSplitTop(6.0f, nullptr, &Left);
		Left.HSplitTop(26.0f, &Button, &Left);
		static CButtonContainer s_BackClan;
		if(DoButton_Menu(&s_BackClan, Localize("Back to clan"), 0, &Button))
			Clans.SetView(CClans::EView::CLAN);
	}

	Left.HSplitTop(12.0f, nullptr, &Left);

	// Recent clans (fills remaining, Refresh at bottom)
	CUIRect RefreshRow;
	Left.HSplitBottom(28.0f, &Left, &RefreshRow);
	Left.HSplitBottom(8.0f, &Left, nullptr);

	Left.HSplitTop(16.0f, &Label, &Left);
	Ui()->DoLabel(&Label, Localize("Recent clans"), 12.0f, TEXTALIGN_ML);
	Left.HSplitTop(4.0f, nullptr, &Left);

	{
		static bool s_TriedRecentFetch = false;
		if(!s_TriedRecentFetch && Clans.RecentClans().empty() && !Clans.IsBusy())
		{
			s_TriedRecentFetch = true;
			Clans.RefreshRecentClans();
		}

		CUIRect ListBox = Left;
		ListBox.Draw(ColorRGBA(0, 0, 0, 0.20f), IGraphics::CORNER_ALL, 4.0f);
		ListBox.Margin(1.0f, &ListBox);

		if(Clans.RecentClans().empty())
		{
			Ui()->DoLabel(&ListBox, Localize("No recent clans yet"), 11.0f, TEXTALIGN_MC);
		}
		else
		{
			static CScrollRegion s_RecentScroll;
			vec2 RecentOffset(0.0f, 0.0f);
			CScrollRegionParams RecentParams;
			s_RecentScroll.Begin(&ListBox, &RecentOffset, &RecentParams);
			ListBox.y += RecentOffset.y;

			static std::vector<CButtonContainer> s_vLandingRejoin;
			if(s_vLandingRejoin.size() < Clans.RecentClans().size())
				s_vLandingRejoin.resize(Clans.RecentClans().size());

			for(size_t Ri = 0; Ri < Clans.RecentClans().size(); Ri++)
			{
				const auto &R = Clans.RecentClans()[Ri];
				CUIRect Item, IconBox, Name, Meta, Act;
				ListBox.HSplitTop(32.0f, &Item, &ListBox);
				if(s_RecentScroll.AddRect(Item))
				{
					Item.Margin(4.0f, &Item);
					Item.VSplitLeft(18.0f, &IconBox, &Item);
					Item.VSplitLeft(4.0f, nullptr, &Item);

					TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
					TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
					Ui()->DoLabel(&IconBox, FontIcon::ICON_USERS, 11.0f, TEXTALIGN_MC);
					TextRender()->SetRenderFlags(0);
					TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

					char aName[80];
					str_format(aName, sizeof(aName), "[%s] %s", R.m_aTag, R.m_aName);

					int Members = -1;
					int MaxMembers = -1;
					for(const auto &Cat : Clans.Catalog())
					{
						if(!str_comp(Cat.m_aClanId, R.m_aClanId))
						{
							Members = Cat.m_MemberCount;
							MaxMembers = Cat.m_MaxMembers;
							break;
						}
					}

					if(R.m_WasPresident && !Clans.InClan())
					{
						Item.VSplitRight(64.0f, &Item, &Act);
						Ui()->DoLabel(&Item, aName, 12.0f, TEXTALIGN_ML);
						if(DoButton_Menu(&s_vLandingRejoin[Ri], Localize("Return"), 0, &Act) && !Clans.IsBusy())
							Clans.RejoinAsPresident(R.m_aClanId);
					}
					else
					{
						if(Members >= 0)
						{
							Item.VSplitRight(48.0f, &Name, &Meta);
							Ui()->DoLabel(&Name, aName, 12.0f, TEXTALIGN_ML);
							char aMeta[24];
							str_format(aMeta, sizeof(aMeta), "%d/%d", Members, MaxMembers);
							Ui()->DoLabel(&Meta, aMeta, 11.0f, TEXTALIGN_MR);
						}
						else
						{
							Ui()->DoLabel(&Item, aName, 12.0f, TEXTALIGN_ML);
						}
						if(Ui()->DoButtonLogic(&Clans.RecentClans()[Ri], 0, &Item, BUTTONFLAG_LEFT))
						{
							if(Clans.InClan() && !str_comp(R.m_aClanId, Clans.ClanId()))
								Clans.SetView(CClans::EView::CLAN);
							else
								Clans.OpenPreview(R.m_aClanId);
						}
					}
				}
				// separator
				CUIRect Sep;
				ListBox.HSplitTop(1.0f, &Sep, &ListBox);
				if(Ri + 1 < Clans.RecentClans().size())
					Sep.Draw(ColorRGBA(1, 1, 1, 0.08f), IGraphics::CORNER_NONE, 0.0f);
			}
			s_RecentScroll.End();
		}
	}

	static CButtonContainer s_RefreshList;
	if(DoButton_Menu(&s_RefreshList, Localize("Refresh list"), 0, &RefreshRow) && !Clans.IsBusy())
		Clans.RefreshCurrentView();

	static bool s_ShowApplyPopup = false;
	static char s_aApplyClanId[64] = "";
	static char s_aApplyText[288] = "";
	static CLineInput s_ApplyInput;
	s_ApplyInput.SetBuffer(s_aApplyText, sizeof(s_aApplyText));
	if(s_ShowApplyPopup)
	{
		CUIRect Overlay = *Ui()->Screen();
		Overlay.Draw(ColorRGBA(0, 0, 0, 0.55f), IGraphics::CORNER_ALL, 0.0f);
		CUIRect Popup;
		MainView.VMargin(MainView.w * 0.15f, &Popup);
		Popup.HMargin(Popup.h * 0.22f, &Popup);
		Popup.Draw(ColorRGBA(0.12f, 0.12f, 0.12f, 0.98f), IGraphics::CORNER_ALL, 8.0f);
		Popup.Margin(14.0f, &Popup);
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

	// Catalog list — separate visual block
	Right.Draw(ColorRGBA(0, 0, 0, 0.22f), IGraphics::CORNER_ALL, 6.0f);
	Right.Margin(10.0f, &Right);

	Right.HSplitTop(22.0f, &Label, &Right);
	Ui()->DoLabel(&Label, Localize("Clan catalog"), 15.0f, TEXTALIGN_ML);
	Right.HSplitTop(6.0f, nullptr, &Right);

	static CScrollRegion s_Scroll;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams Params;
	s_Scroll.Begin(&Right, &ScrollOffset, &Params);
	Right.y += ScrollOffset.y;

	bool LastVisible = false;
	int MatchCount = 0;
	for(size_t Index = 0; Index < Clans.Catalog().size(); Index++)
	{
		const auto &Entry = Clans.Catalog()[Index];
		if(s_aSearch[0] &&
			!str_find_nocase(Entry.m_aName, s_aSearch) &&
			!str_find_nocase(Entry.m_aTag, s_aSearch) &&
			!str_find_nocase(Entry.m_aDescription, s_aSearch))
		{
			continue;
		}
		MatchCount++;
		CUIRect Item;
		Right.HSplitTop(36.0f, &Item, &Right);
		const bool Visible = s_Scroll.AddRect(Item);
		LastVisible = Visible;
		if(Visible)
		{
			Item.Draw(ColorRGBA(0, 0, 0, 0.18f), IGraphics::CORNER_ALL, 3.0f);
			Item.Margin(4.0f, &Item);
			CUIRect IconBox, Name, Meta;
			Item.VSplitLeft(28.0f, &IconBox, &Item);
			Item.VSplitLeft(4.0f, nullptr, &Item);
			Item.VSplitRight(100.0f, &Name, &Meta);
			RenderClanIcon(Ui(), TextRender(), IconBox, Entry.m_IconId, Entry.m_Color);
			char aLine[96];
			str_format(aLine, sizeof(aLine), "[%s] %s", Entry.m_aTag, Entry.m_aName);
			Ui()->DoLabel(&Name, aLine, 13.0f, TEXTALIGN_ML);
			str_format(aLine, sizeof(aLine), "%d/%d · %s", Entry.m_OnlineCount, Entry.m_MemberCount, Entry.m_aJoinPolicy);
			Ui()->DoLabel(&Meta, aLine, 11.0f, TEXTALIGN_MR);
			if(Ui()->DoButtonLogic(&Clans.Catalog()[Index], 0, &Item, BUTTONFLAG_LEFT))
			{
				if(Clans.InClan() && !str_comp(Entry.m_aClanId, Clans.ClanId()))
					Clans.SetView(CClans::EView::CLAN);
				else
					Clans.OpenPreview(Entry.m_aClanId);
			}
		}
		Right.HSplitTop(3.0f, nullptr, &Right);
	}
	s_Scroll.End();

	static int64_t s_LastBottomRefresh = 0;
	if(LastVisible && s_Scroll.ScrollbarShown() && MatchCount > 0 && !Clans.IsBusy())
	{
		const int64_t Now = time_get();
		if(Now - s_LastBottomRefresh > time_freq() * 3)
		{
			s_LastBottomRefresh = Now;
			Clans.RefreshCatalog();
		}
	}
}

static unsigned ClanRgbFromHsla(unsigned HslaPacked)
{
	const ColorRGBA Rgba = color_cast<ColorRGBA>(ColorHSLA(HslaPacked, false));
	const unsigned R = (unsigned)(std::clamp(Rgba.r, 0.0f, 1.0f) * 255.0f + 0.5f);
	const unsigned G = (unsigned)(std::clamp(Rgba.g, 0.0f, 1.0f) * 255.0f + 0.5f);
	const unsigned B = (unsigned)(std::clamp(Rgba.b, 0.0f, 1.0f) * 255.0f + 0.5f);
	return (R << 16) | (G << 8) | B;
}

void CMenus::RenderClansSetup(CUIRect MainView)
{
	CClans &Clans = GameClient()->m_Clans;
	static CLineInput s_Name, s_Tag, s_Desc;
	static char s_aName[64], s_aTag[16], s_aDesc[256];
	static int s_Policy = 0;
	static int s_MaxMembers = 50;
	static int s_IconId = 7; // crown-ish default
	static unsigned s_ColorHsla = 0;
	static bool s_Init = false;
	if(!s_Init)
	{
		s_aName[0] = s_aTag[0] = s_aDesc[0] = '\0';
		str_copy(s_aTag, g_Config.m_PlayerClan, sizeof(s_aTag));
		str_copy(s_aName, g_Config.m_PlayerClan, sizeof(s_aName));
		s_IconId = 7;
		s_ColorHsla = color_cast<ColorHSLA>(ColorRGBA(0.33f, 0.55f, 1.0f, 1.0f)).Pack(false);
		s_Init = true;
	}
	s_Name.SetBuffer(s_aName, sizeof(s_aName));
	s_Tag.SetBuffer(s_aTag, sizeof(s_aTag));
	s_Desc.SetBuffer(s_aDesc, sizeof(s_aDesc));
	s_Name.SetEmptyText(Localize("Clan name"));
	s_Tag.SetEmptyText(Localize("Tag"));
	s_Desc.SetEmptyText(Localize("Description"));

	const unsigned ClanRgb = ClanRgbFromHsla(s_ColorHsla);
	static const char *apPolicy[] = {"open", "request", "closed"};
	const char *pPolicyLabel = Localize("Open");
	if(s_Policy == 1)
		pPolicyLabel = Localize("Request");
	else if(s_Policy == 2)
		pPolicyLabel = Localize("Closed");

	CUIRect Left, Right, Label, Button, Row;
	MainView.VSplitLeft(MainView.w * 0.58f, &Left, &Right);
	Left.VSplitRight(10.0f, &Left, nullptr);

	Left.Draw(ColorRGBA(0, 0, 0, 0.28f), IGraphics::CORNER_ALL, 6.0f);
	Left.Margin(12.0f, &Left);

	// Bottom actions first so form uses remaining space
	CUIRect Actions;
	Left.HSplitBottom(30.0f, &Left, &Actions);
	Left.HSplitBottom(10.0f, &Left, nullptr);
	CUIRect CreateBtn, BackBtn;
	Actions.VSplitMid(&CreateBtn, &BackBtn, 8.0f);
	static CButtonContainer s_Create, s_Back;
	if(DoButton_Menu(&s_Create, Localize("Create clan"), 0, &CreateBtn) && s_aName[0] && s_aTag[0] && !Clans.IsBusy())
		Clans.CreateClan(s_aName, s_aTag, s_aDesc, s_IconId, ClanRgb, apPolicy[s_Policy], s_MaxMembers);
	if(DoButton_Menu(&s_Back, Localize("Back"), 0, &BackBtn))
		Clans.SetView(Clans.InClan() ? CClans::EView::CLAN : CClans::EView::LANDING);

	// Name + Tag row
	CUIRect NameCol, TagCol;
	Left.HSplitTop(16.0f, &Label, &Left);
	Label.VSplitMid(&NameCol, &TagCol, 8.0f);
	Ui()->DoLabel(&NameCol, Localize("Name"), 12.0f, TEXTALIGN_ML);
	Ui()->DoLabel(&TagCol, Localize("Tag"), 12.0f, TEXTALIGN_ML);
	Left.HSplitTop(4.0f, nullptr, &Left);
	Left.HSplitTop(26.0f, &Row, &Left);
	Row.VSplitMid(&NameCol, &TagCol, 8.0f);
	Ui()->DoEditBox(&s_Name, &NameCol, 13.0f);
	Ui()->DoEditBox(&s_Tag, &TagCol, 13.0f);

	Left.HSplitTop(14.0f, nullptr, &Left);
	Left.HSplitTop(14.0f, &Label, &Left);
	Ui()->DoLabel(&Label, Localize("Description"), 12.0f, TEXTALIGN_ML);
	Left.HSplitTop(4.0f, nullptr, &Left);
	Left.HSplitTop(32.0f, &Button, &Left);
	{
		const float DescFont = 11.0f;
		const float DescSpacing = 1.0f;
		const float DescLineWidth = maximum(1.0f, Button.w - 4.0f);
		if(Ui()->DoEditBox(&s_Desc, &Button, DescFont, IGraphics::CORNER_ALL, {}, DescLineWidth, DescSpacing))
		{
			for(;;)
			{
				STextSizeProperties Props;
				int LineCount = 0;
				Props.m_pLineCount = &LineCount;
				TextRender()->TextWidth(DescFont, s_aDesc, -1, DescLineWidth, 0, Props);
				if(LineCount <= 2 || !s_aDesc[0])
					break;
				const int Len = str_length(s_aDesc);
				s_aDesc[Len - 1] = '\0';
				str_utf8_fix_truncation(s_aDesc);
			}
		}
	}

	Left.HSplitTop(8.0f, nullptr, &Left);
	Left.HSplitTop(24.0f, &Button, &Left);
	static CButtonContainer s_UseCurrent;
	if(DoButton_Menu(&s_UseCurrent, Localize("Use current tee clan"), 0, &Button))
	{
		str_copy(s_aTag, g_Config.m_PlayerClan, sizeof(s_aTag));
		str_copy(s_aName, g_Config.m_PlayerClan, sizeof(s_aName));
	}

	Left.HSplitTop(10.0f, nullptr, &Left);
	Left.HSplitTop(16.0f, &Label, &Left);
	Ui()->DoLabel(&Label, Localize("Icon and color"), 12.0f, TEXTALIGN_ML);
	Left.HSplitTop(4.0f, nullptr, &Left);
	Left.HSplitTop(32.0f, &Row, &Left);
	CUIRect IconsRow, ColorRow;
	Row.VSplitRight(100.0f, &IconsRow, &ColorRow);
	ColorRow.VSplitLeft(8.0f, nullptr, &ColorRow);
	static CButtonContainer s_aIcons[8];
	for(int i = 0; i < 8; i++)
	{
		CUIRect IconBtn;
		IconsRow.VSplitLeft(IconsRow.h + 4.0f, &IconBtn, &IconsRow);
		IconBtn.VSplitRight(4.0f, &IconBtn, nullptr);
		IconBtn.Draw(s_IconId == i ? ColorRGBA(0.35f, 0.55f, 1.0f, 0.45f) : ColorRGBA(0, 0, 0, 0.22f), IGraphics::CORNER_ALL, 4.0f);
		RenderClanIcon(Ui(), TextRender(), IconBtn, i, ClanRgb);
		if(Ui()->DoButtonLogic(&s_aIcons[i], s_IconId == i, &IconBtn, BUTTONFLAG_LEFT))
			s_IconId = i;
	}
	{
		CUIRect ColorBtn, ResetBtn;
		ColorRow.VSplitRight(56.0f, &ColorRow, &ResetBtn);
		ColorRow.VSplitRight(4.0f, &ColorRow, nullptr);
		const float Sq = minimum(ColorRow.w, ColorRow.h);
		ColorRow.VMargin(maximum(0.0f, (ColorRow.w - Sq) * 0.5f), &ColorBtn);
		ColorBtn.HMargin(maximum(0.0f, (ColorBtn.h - Sq) * 0.5f), &ColorBtn);
		DoButton_ColorPicker(&ColorBtn, &s_ColorHsla, false);
		static CButtonContainer s_ColorReset;
		ResetBtn.HMargin(2.0f, &ResetBtn);
		if(DoButton_Menu(&s_ColorReset, Localize("Reset"), 0, &ResetBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.1f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f)))
			s_ColorHsla = color_cast<ColorHSLA>(ColorRGBA(0.33f, 0.55f, 1.0f, 1.0f)).Pack(false);
	}

	Left.HSplitTop(10.0f, nullptr, &Left);
	CUIRect PolicyCol, MaxCol;
	Left.HSplitTop(16.0f, &Label, &Left);
	Label.VSplitMid(&PolicyCol, &MaxCol, 10.0f);
	Ui()->DoLabel(&PolicyCol, Localize("Join policy"), 12.0f, TEXTALIGN_ML);
	Ui()->DoLabel(&MaxCol, Localize("Max members"), 12.0f, TEXTALIGN_ML);
	Left.HSplitTop(4.0f, nullptr, &Left);

	const float PolicyBtnH = 24.0f;
	const float PolicyBlockH = PolicyBtnH * 3.0f + 8.0f;
	CUIRect PolicyBlock;
	Left.HSplitTop(PolicyBlockH, &PolicyBlock, &Left);
	PolicyBlock.VSplitMid(&PolicyCol, &MaxCol, 10.0f);

	static CButtonContainer s_Open, s_Req, s_Closed;
	CUIRect PolicyBtn;
	PolicyCol.HSplitTop(PolicyBtnH, &PolicyBtn, &PolicyCol);
	if(DoButton_Menu(&s_Open, Localize("Open"), s_Policy == 0, &PolicyBtn))
		s_Policy = 0;
	PolicyCol.HSplitTop(4.0f, nullptr, &PolicyCol);
	PolicyCol.HSplitTop(PolicyBtnH, &PolicyBtn, &PolicyCol);
	if(DoButton_Menu(&s_Req, Localize("Request"), s_Policy == 1, &PolicyBtn))
		s_Policy = 1;
	PolicyCol.HSplitTop(4.0f, nullptr, &PolicyCol);
	PolicyCol.HSplitTop(PolicyBtnH, &PolicyBtn, &PolicyCol);
	if(DoButton_Menu(&s_Closed, Localize("Closed"), s_Policy == 2, &PolicyBtn))
		s_Policy = 2;

	MaxCol.Draw(ColorRGBA(0, 0, 0, 0.16f), IGraphics::CORNER_ALL, 4.0f);
	MaxCol.Margin(6.0f, &MaxCol);
	CUIRect MaxValue, MaxSlider;
	MaxCol.HSplitTop(22.0f, &MaxValue, &MaxCol);
	char aMaxBuf[16];
	str_format(aMaxBuf, sizeof(aMaxBuf), "%d", s_MaxMembers);
	Ui()->DoLabel(&MaxValue, aMaxBuf, 16.0f, TEXTALIGN_MC);
	MaxCol.HSplitTop(4.0f, nullptr, &MaxCol);
	MaxCol.HSplitTop(22.0f, &MaxSlider, &MaxCol);
	Ui()->DoScrollbarOption(&s_MaxMembers, &s_MaxMembers, &MaxSlider, "", 2, 200);

	// ---- Preview panel ----
	Right.Draw(ColorRGBA(0, 0, 0, 0.28f), IGraphics::CORNER_ALL, 6.0f);
	Right.Margin(12.0f, &Right);
	Right.HSplitTop(16.0f, &Label, &Right);
	Ui()->DoLabel(&Label, Localize("PREVIEW"), 12.0f, TEXTALIGN_ML);
	Right.HSplitTop(8.0f, nullptr, &Right);

	CUIRect Card = Right;
	Card.Draw(ColorRGBA(0, 0, 0, 0.22f), IGraphics::CORNER_ALL, 5.0f);
	Card.Margin(10.0f, &Card);

	CUIRect Header, IconBox, TextCol;
	Card.HSplitTop(40.0f, &Header, &Card);
	Header.VSplitLeft(36.0f, &IconBox, &TextCol);
	TextCol.VSplitLeft(8.0f, nullptr, &TextCol);
	IconBox.Draw(ColorRGBA(0, 0, 0, 0.35f), IGraphics::CORNER_ALL, 4.0f);
	RenderClanIcon(Ui(), TextRender(), IconBox, s_IconId, ClanRgb);

	CUIRect TitleR, MetaR;
	TextCol.HSplitMid(&TitleR, &MetaR);
	char aTitle[96];
	const char *pName = s_aName[0] ? s_aName : Localize("Clan name");
	const char *pTag = s_aTag[0] ? s_aTag : "???";
	str_format(aTitle, sizeof(aTitle), "[%s] %s", pTag, pName);
	Ui()->DoLabel(&TitleR, aTitle, 14.0f, TEXTALIGN_ML);
	str_format(aTitle, sizeof(aTitle), "%s · 0/%d", pPolicyLabel, s_MaxMembers);
	Ui()->DoLabel(&MetaR, aTitle, 11.0f, TEXTALIGN_ML);

	Card.HSplitTop(8.0f, nullptr, &Card);
	CUIRect Sep;
	Card.HSplitTop(1.0f, &Sep, &Card);
	Sep.Draw(ColorRGBA(1, 1, 1, 0.10f), IGraphics::CORNER_NONE, 0.0f);
	Card.HSplitTop(10.0f, nullptr, &Card);

	Card.HSplitTop(28.0f, &Label, &Card);
	{
		const char *pDesc = s_aDesc[0] ? s_aDesc : Localize("This is how your clan will appear in the catalog list to other players.");
		SLabelProperties DescProps;
		DescProps.m_MaxWidth = Label.w;
		Ui()->DoLabel(&Label, pDesc, 10.0f, TEXTALIGN_TL, DescProps);
	}

	Card.HSplitTop(6.0f, nullptr, &Card);
	CUIRect FootIcon, FootText;
	Card.HSplitTop(18.0f, &Row, &Card);
	Row.VSplitLeft(16.0f, &FootIcon, &FootText);
	FootText.VSplitLeft(4.0f, nullptr, &FootText);
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	Ui()->DoLabel(&FootIcon, FontIcon::ICON_USERS, 11.0f, TEXTALIGN_MC);
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	str_format(aTitle, sizeof(aTitle), Localize("Up to %d members"), s_MaxMembers);
	Ui()->DoLabel(&FootText, aTitle, 11.0f, TEXTALIGN_ML);

	Card.HSplitTop(10.0f, nullptr, &Card);
	Card.HSplitTop(1.0f, &Sep, &Card);
	Sep.Draw(ColorRGBA(1, 1, 1, 0.10f), IGraphics::CORNER_NONE, 0.0f);
	Card.HSplitTop(8.0f, nullptr, &Card);

	// You as president (preview member row)
	CUIRect MemberRow, TeeBox, MemberText, OnlineBox;
	Card.HSplitTop(36.0f, &MemberRow, &Card);
	MemberRow.Draw(ColorRGBA(0, 0, 0, 0.18f), IGraphics::CORNER_ALL, 4.0f);
	MemberRow.Margin(4.0f, &MemberRow);
	MemberRow.VSplitLeft(28.0f, &TeeBox, &MemberText);
	MemberText.VSplitLeft(6.0f, nullptr, &MemberText);
	MemberText.VSplitRight(52.0f, &MemberText, &OnlineBox);
	RenderOwnTee(GameClient(), RenderTools(), TeeBox);

	CUIRect NickR, RoleR;
	MemberText.HSplitMid(&NickR, &RoleR);
	Ui()->DoLabel(&NickR, Clans.Nickname()[0] ? Clans.Nickname() : g_Config.m_PlayerName, 12.0f, TEXTALIGN_ML);
	Ui()->DoLabel(&RoleR, Localize("President"), 10.0f, TEXTALIGN_ML);

	CUIRect OnlineDot, OnlineLbl;
	OnlineBox.VSplitLeft(10.0f, &OnlineDot, &OnlineLbl);
	OnlineDot.Margin(2.0f, &OnlineDot);
	OnlineDot.HMargin(maximum(0.0f, (OnlineDot.h - OnlineDot.w) * 0.5f), &OnlineDot);
	OnlineDot.Draw(ColorRGBA(0.35f, 0.9f, 0.4f, 1.0f), IGraphics::CORNER_ALL, OnlineDot.w * 0.5f);
	Ui()->DoLabel(&OnlineLbl, Localize("Online"), 10.0f, TEXTALIGN_MR);
}

void CMenus::RenderClansPage(CUIRect MainView)
{
	CClans &Clans = GameClient()->m_Clans;
	const auto &Clan = Clans.Clan();

	static bool s_ShowDisband = false;
	static int s_aDisbandOrder[4] = {0, 1, 2, 3};
	static int s_MemberMenu = -1;
	static vec2 s_MemberMenuPos;
	static int s_KickUser = -1;
	static int s_BanHours = 24;
	static char s_aKickUserId[64] = "";

	CUIRect TopBar;
	MainView.HSplitTop(28.0f, &TopBar, &MainView);
	CUIRect I1, I2, I3, I4;
	TopBar.VSplitRight(28.0f, &TopBar, &I4);
	TopBar.VSplitRight(4.0f, &TopBar, nullptr);
	TopBar.VSplitRight(28.0f, &TopBar, &I3);
	TopBar.VSplitRight(4.0f, &TopBar, nullptr);
	TopBar.VSplitRight(28.0f, &TopBar, &I2);
	TopBar.VSplitRight(4.0f, &TopBar, nullptr);
	TopBar.VSplitRight(28.0f, &TopBar, &I1);

	static CButtonContainer s_Refresh, s_Catalog, s_Ann, s_Apps;
	if(DoClansIconBtn(Ui(), &s_Refresh, FontIcon::ARROW_ROTATE_RIGHT, &I1) && !Clans.IsBusy())
		Clans.RefreshCurrentView();
	if(DoClansIconBtn(Ui(), &s_Catalog, FontIcon::LIST_UL, &I2))
	{
		Clans.RefreshCatalog();
		Clans.SetView(CClans::EView::BROWSE);
	}
	if(DoClansIconBtn(Ui(), &s_Ann, FontIcon::NEWSPAPER, &I3))
	{
		Clans.RefreshAnnouncements();
		Clans.SetView(CClans::EView::ANNOUNCEMENTS);
	}
	const bool CanApps = !str_comp(Clan.m_aJoinPolicy, "request") &&
			     (Clans.Role() == CClans::ERole::PRESIDENT || Clans.Role() == CClans::ERole::VICE_PRESIDENT || Clans.Role() == CClans::ERole::VETERAN);
	if(CanApps && DoClansIconBtn(Ui(), &s_Apps, FontIcon::COMMENT, &I4))
	{
		Clans.RefreshApplications();
		Clans.SetView(CClans::EView::APPLICATIONS);
	}

	CUIRect Left, Right, Label, Button, Row;
	MainView.VSplitLeft(MainView.w * 0.36f, &Left, &Right);
	Left.VSplitRight(8.0f, &Left, nullptr);
	Left.Draw(ColorRGBA(0, 0, 0, 0.3f), IGraphics::CORNER_ALL, 6.0f);
	Left.Margin(10.0f, &Left);

	CUIRect TitleRow, IconBox;
	Left.HSplitTop(28.0f, &TitleRow, &Left);
	TitleRow.VSplitLeft(28.0f, &IconBox, &TitleRow);
	TitleRow.VSplitLeft(6.0f, nullptr, &TitleRow);
	RenderClanIcon(Ui(), TextRender(), IconBox, Clan.m_IconId, Clan.m_Color);
	Ui()->DoLabel(&TitleRow, Clan.m_aName[0] ? Clan.m_aName : Localize("Loading..."), 17.0f, TEXTALIGN_ML);

	Left.HSplitTop(18.0f, &Label, &Left);
	char aMeta[80];
	str_format(aMeta, sizeof(aMeta), "[%s] · %s", Clan.m_aTag, Clan.m_aJoinPolicy);
	Ui()->DoLabel(&Label, aMeta, 13.0f, TEXTALIGN_ML);
	Left.HSplitTop(12.0f, nullptr, &Left);
	Left.HSplitTop(28.0f, &Label, &Left);
	{
		SLabelProperties DescProps;
		DescProps.m_MaxWidth = Label.w;
		Ui()->DoLabel(&Label, Clan.m_aDescription, 10.0f, TEXTALIGN_TL, DescProps);
	}

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
		{
			s_ShowDisband = true;
			for(int i = 0; i < 4; i++)
				s_aDisbandOrder[i] = i;
			for(int i = 3; i > 0; i--)
			{
				const int j = rand() % (i + 1);
				std::swap(s_aDisbandOrder[i], s_aDisbandOrder[j]);
			}
		}
	}

	Right.HSplitTop(22.0f, &Label, &Right);
	Ui()->DoLabel(&Label, Localize("Main"), 15.0f, TEXTALIGN_ML);
	Right.HSplitTop(4.0f, nullptr, &Right);

	static CScrollRegion s_Scroll;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams Params;
	s_Scroll.Begin(&Right, &ScrollOffset, &Params);
	Right.y += ScrollOffset.y;

	size_t MemberIndex = 0;
	static int s_aMemberIds[128];
	for(const auto &M : Clan.m_vMembers)
	{
		CUIRect Item;
		Right.HSplitTop(42.0f, &Item, &Right);
		if(s_Scroll.AddRect(Item))
		{
			Item.Draw(ColorRGBA(0, 0, 0, 0.18f), IGraphics::CORNER_ALL, 3.0f);
			Item.Margin(4.0f, &Item);
			CUIRect Tee, Name, Status;
			Item.VSplitLeft(36.0f, &Tee, &Item);
			Item.VSplitLeft(4.0f, nullptr, &Item);
			Item.VSplitRight(150.0f, &Name, &Status);
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
			if(CanModerate && MemberIndex < std::size(s_aMemberIds) && Ui()->DoButtonLogic(&s_aMemberIds[MemberIndex], 0, &Item, BUTTONFLAG_RIGHT))
			{
				s_MemberMenu = (int)MemberIndex;
				s_MemberMenuPos = Ui()->MousePos();
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
			char aUMeta[128];
			str_format(aUMeta, sizeof(aUMeta), Localize("playing %s %d/%d"), U.m_aMap, U.m_Players, U.m_MaxPlayers);
			Ui()->DoLabel(&Meta, aUMeta, 11.0f, TEXTALIGN_MR);
		}
		Right.HSplitTop(2.0f, nullptr, &Right);
	}
	s_Scroll.End();

	// Member RMB menu
	if(s_MemberMenu >= 0 && s_MemberMenu < (int)Clan.m_vMembers.size())
	{
		const auto &M = Clan.m_vMembers[s_MemberMenu];
		CUIRect Menu;
		Menu.x = s_MemberMenuPos.x;
		Menu.y = s_MemberMenuPos.y;
		Menu.w = 140.0f;
		Menu.h = 100.0f;
		if(Menu.x + Menu.w > Ui()->Screen()->w)
			Menu.x = Ui()->Screen()->w - Menu.w - 4.0f;
		Menu.Draw(ColorRGBA(0.1f, 0.1f, 0.1f, 0.96f), IGraphics::CORNER_ALL, 4.0f);
		Menu.Margin(6.0f, &Menu);
		CUIRect Btn;
		static CButtonContainer s_Promote, s_Demote, s_Kick;
		const bool CanPromote = Clans.Role() == CClans::ERole::PRESIDENT || Clans.Role() == CClans::ERole::VICE_PRESIDENT;
		const bool CanDemote = Clans.Role() == CClans::ERole::PRESIDENT;
		if(CanPromote)
		{
			Menu.HSplitTop(26.0f, &Btn, &Menu);
			if(DoButton_Menu(&s_Promote, Localize("Promote"), 0, &Btn) && !Clans.IsBusy())
			{
				Clans.Promote(M.m_aUserId);
				s_MemberMenu = -1;
			}
			Menu.HSplitTop(4.0f, nullptr, &Menu);
		}
		if(CanDemote)
		{
			Menu.HSplitTop(26.0f, &Btn, &Menu);
			if(DoButton_Menu(&s_Demote, Localize("Demote"), 0, &Btn) && !Clans.IsBusy())
			{
				Clans.Demote(M.m_aUserId);
				s_MemberMenu = -1;
			}
			Menu.HSplitTop(4.0f, nullptr, &Menu);
		}
		Menu.HSplitTop(26.0f, &Btn, &Menu);
		if(DoButton_Menu(&s_Kick, Localize("Kick"), 0, &Btn))
		{
			str_copy(s_aKickUserId, M.m_aUserId, sizeof(s_aKickUserId));
			s_KickUser = s_MemberMenu;
			s_BanHours = 24;
			s_MemberMenu = -1;
		}
		if(Ui()->MouseButtonClicked(0) && !Ui()->MouseHovered(&Menu))
			s_MemberMenu = -1;
	}

	// Kick confirm popup
	if(s_KickUser >= 0 && s_aKickUserId[0])
	{
		CUIRect Overlay = *Ui()->Screen();
		Overlay.Draw(ColorRGBA(0, 0, 0, 0.55f), IGraphics::CORNER_ALL, 0.0f);
		CUIRect Popup;
		MainView.VMargin(MainView.w * 0.22f, &Popup);
		Popup.HMargin(Popup.h * 0.28f, &Popup);
		Popup.Draw(ColorRGBA(0.12f, 0.12f, 0.12f, 0.98f), IGraphics::CORNER_ALL, 8.0f);
		Popup.Margin(14.0f, &Popup);
		Popup.HSplitTop(24.0f, &Label, &Popup);
		Ui()->DoLabel(&Label, Localize("Kick member"), 16.0f, TEXTALIGN_MC);
		Popup.HSplitTop(10.0f, nullptr, &Popup);

		if(Clans.Role() == CClans::ERole::PRESIDENT)
		{
			Popup.HSplitTop(18.0f, &Label, &Popup);
			Ui()->DoLabel(&Label, Localize("Ban duration"), 13.0f, TEXTALIGN_ML);
			Popup.HSplitTop(6.0f, nullptr, &Popup);
			Popup.HSplitTop(28.0f, &Row, &Popup);
			static const int s_aBanOpts[] = {0, 24, 48, 72, -1};
			static const char *s_apBanLabels[] = {"0h", "24h", "48h", "72h", "perm"};
			static CButtonContainer s_aBanBtns[5];
			for(int i = 0; i < 5; i++)
			{
				CUIRect B;
				Row.VSplitLeft(52.0f, &B, &Row);
				Row.VSplitLeft(4.0f, nullptr, &Row);
				if(DoButton_Menu(&s_aBanBtns[i], s_apBanLabels[i], s_BanHours == s_aBanOpts[i], &B))
					s_BanHours = s_aBanOpts[i];
			}
			Popup.HSplitTop(12.0f, nullptr, &Popup);
		}

		Popup.HSplitTop(30.0f, &Row, &Popup);
		CUIRect Confirm, Cancel;
		Row.VSplitMid(&Confirm, &Cancel, 8.0f);
		static CButtonContainer s_ConfirmKick, s_CancelKick;
		if(DoButton_Menu(&s_ConfirmKick, Localize("Kick"), 0, &Confirm) && !Clans.IsBusy())
		{
			Clans.Kick(s_aKickUserId, Clans.Role() == CClans::ERole::PRESIDENT ? s_BanHours : 0);
			s_KickUser = -1;
			s_aKickUserId[0] = '\0';
		}
		if(DoButton_Menu(&s_CancelKick, Localize("Cancel"), 0, &Cancel))
		{
			s_KickUser = -1;
			s_aKickUserId[0] = '\0';
		}
	}

	// Disband confirm (4 shuffled answers)
	if(s_ShowDisband)
	{
		CUIRect Overlay = *Ui()->Screen();
		Overlay.Draw(ColorRGBA(0, 0, 0, 0.6f), IGraphics::CORNER_ALL, 0.0f);
		CUIRect Popup;
		MainView.VMargin(MainView.w * 0.12f, &Popup);
		Popup.HMargin(Popup.h * 0.18f, &Popup);
		Popup.Draw(ColorRGBA(0.12f, 0.12f, 0.12f, 0.98f), IGraphics::CORNER_ALL, 8.0f);
		Popup.Margin(16.0f, &Popup);
		Popup.HSplitTop(22.0f, &Label, &Popup);
		Ui()->DoLabel(&Label, Localize("Disband clan?"), 18.0f, TEXTALIGN_MC);
		Popup.HSplitTop(8.0f, nullptr, &Popup);
		Popup.HSplitTop(18.0f, &Label, &Popup);
		Ui()->DoLabel(&Label, Localize("Click the exact phrase:"), 13.0f, TEXTALIGN_MC);
		Popup.HSplitTop(4.0f, nullptr, &Popup);
		Popup.HSplitTop(28.0f, &Label, &Popup);
		Label.Draw(ColorRGBA(0.8f, 0.25f, 0.25f, 0.35f), IGraphics::CORNER_ALL, 4.0f);
		Ui()->DoLabel(&Label, Localize("Yes I really want to disband the clan!"), 13.0f, TEXTALIGN_MC);
		Popup.HSplitTop(12.0f, nullptr, &Popup);

		const char *apAnswers[4] = {
			Localize("Yes I really want to disband the clan!"),
			Localize("Yes, disband the clan"),
			Localize("I want to disband the clan"),
			Localize("Disband this clan now"),
		};
		static CButtonContainer s_aAns[4];
		for(int i = 0; i < 4; i++)
		{
			const int Idx = s_aDisbandOrder[i];
			Popup.HSplitTop(30.0f, &Button, &Popup);
			Popup.HSplitTop(6.0f, nullptr, &Popup);
			if(DoButton_Menu(&s_aAns[i], apAnswers[Idx], 0, &Button) && !Clans.IsBusy())
			{
				if(Idx == 0)
					Clans.Disband();
				s_ShowDisband = false;
			}
		}
		Popup.HSplitTop(8.0f, nullptr, &Popup);
		Popup.HSplitTop(28.0f, &Button, &Popup);
		static CButtonContainer s_CancelDisband;
		if(DoButton_Menu(&s_CancelDisband, Localize("Cancel"), 0, &Button))
			s_ShowDisband = false;
	}
}

void CMenus::RenderClansPreview(CUIRect MainView)
{
	CClans &Clans = GameClient()->m_Clans;
	const auto &Clan = Clans.Preview();

	static bool s_ShowApplyPopup = false;
	static char s_aApplyText[288] = "";
	static CLineInput s_ApplyInput;
	s_ApplyInput.SetBuffer(s_aApplyText, sizeof(s_aApplyText));

	DrawClansAccountBar(GameClient(), RenderTools(), Ui(), Clans, &MainView);

	CUIRect TopBar, BackBtn, RefreshBtn;
	MainView.HSplitTop(28.0f, &TopBar, &MainView);
	TopBar.VSplitRight(28.0f, &TopBar, &RefreshBtn);
	TopBar.VSplitRight(4.0f, &TopBar, nullptr);
	TopBar.VSplitRight(28.0f, &TopBar, &BackBtn);
	static CButtonContainer s_Back, s_Refresh;
	if(DoClansIconBtn(Ui(), &s_Back, FontIcon::CHEVRON_LEFT, &BackBtn))
	{
		Clans.ClearPreview();
		Clans.SetView(Clans.InClan() ? CClans::EView::BROWSE : CClans::EView::LANDING);
	}
	if(DoClansIconBtn(Ui(), &s_Refresh, FontIcon::ARROW_ROTATE_RIGHT, &RefreshBtn) && !Clans.IsBusy())
		Clans.RefreshCurrentView();

	if(s_ShowApplyPopup)
	{
		CUIRect Overlay = *Ui()->Screen();
		Overlay.Draw(ColorRGBA(0, 0, 0, 0.55f), IGraphics::CORNER_ALL, 0.0f);
		CUIRect Popup, Label, Button, Row;
		MainView.VMargin(MainView.w * 0.15f, &Popup);
		Popup.HMargin(Popup.h * 0.22f, &Popup);
		Popup.Draw(ColorRGBA(0.12f, 0.12f, 0.12f, 0.98f), IGraphics::CORNER_ALL, 8.0f);
		Popup.Margin(14.0f, &Popup);
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
			Clans.Apply(Clans.PreviewClanId(), s_aApplyText);
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

	CUIRect Left, Right, Label, Button;
	MainView.VSplitLeft(MainView.w * 0.36f, &Left, &Right);
	Left.VSplitRight(8.0f, &Left, nullptr);
	Left.Draw(ColorRGBA(0, 0, 0, 0.3f), IGraphics::CORNER_ALL, 6.0f);
	Left.Margin(10.0f, &Left);

	CUIRect TitleRow, IconBox;
	Left.HSplitTop(28.0f, &TitleRow, &Left);
	TitleRow.VSplitLeft(28.0f, &IconBox, &TitleRow);
	TitleRow.VSplitLeft(6.0f, nullptr, &TitleRow);
	RenderClanIcon(Ui(), TextRender(), IconBox, Clan.m_IconId, Clan.m_Color);
	Ui()->DoLabel(&TitleRow, Clan.m_aName[0] ? Clan.m_aName : Localize("Loading..."), 17.0f, TEXTALIGN_ML);

	Left.HSplitTop(18.0f, &Label, &Left);
	char aMeta[80];
	str_format(aMeta, sizeof(aMeta), "[%s] · %s", Clan.m_aTag, Clan.m_aJoinPolicy);
	Ui()->DoLabel(&Label, aMeta, 13.0f, TEXTALIGN_ML);
	Left.HSplitTop(12.0f, nullptr, &Left);
	Left.HSplitTop(28.0f, &Label, &Left);
	{
		SLabelProperties DescProps;
		DescProps.m_MaxWidth = Label.w;
		Ui()->DoLabel(&Label, Clan.m_aDescription, 10.0f, TEXTALIGN_TL, DescProps);
	}

	int Online = 0;
	for(const auto &M : Clan.m_vMembers)
		if(M.m_Online)
			Online++;
	Left.HSplitTop(10.0f, nullptr, &Left);
	Left.HSplitTop(18.0f, &Label, &Left);
	str_format(aMeta, sizeof(aMeta), "Main online %d/%d", Online, (int)Clan.m_vMembers.size());
	Ui()->DoLabel(&Label, aMeta, 12.0f, TEXTALIGN_ML);

	if(!Clans.InClan() && Clan.m_aClanId[0])
	{
		Left.HSplitBottom(36.0f, &Left, &Button);
		static CButtonContainer s_JoinOpen, s_Apply;
		if(!str_comp(Clan.m_aJoinPolicy, "open"))
		{
			if(DoButton_Menu(&s_JoinOpen, Localize("Join"), 0, &Button) && !Clans.IsBusy())
				Clans.Join(Clan.m_aClanId);
		}
		else if(!str_comp(Clan.m_aJoinPolicy, "request"))
		{
			if(DoButton_Menu(&s_Apply, Localize("Apply"), 0, &Button) && !Clans.IsBusy())
			{
				s_aApplyText[0] = '\0';
				s_ShowApplyPopup = true;
			}
		}
		else
			Ui()->DoLabel(&Button, Localize("Invite only"), 13.0f, TEXTALIGN_MC);
	}

	Right.HSplitTop(22.0f, &Label, &Right);
	Ui()->DoLabel(&Label, Localize("Main"), 15.0f, TEXTALIGN_ML);
	Right.HSplitTop(4.0f, nullptr, &Right);

	static CScrollRegion s_Scroll;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams Params;
	s_Scroll.Begin(&Right, &ScrollOffset, &Params);
	Right.y += ScrollOffset.y;
	for(const auto &M : Clan.m_vMembers)
	{
		CUIRect Item;
		Right.HSplitTop(42.0f, &Item, &Right);
		if(s_Scroll.AddRect(Item))
		{
			Item.Draw(ColorRGBA(0, 0, 0, 0.18f), IGraphics::CORNER_ALL, 3.0f);
			Item.Margin(4.0f, &Item);
			CUIRect Tee, Name, Status;
			Item.VSplitLeft(36.0f, &Tee, &Item);
			Item.VSplitLeft(4.0f, nullptr, &Item);
			Item.VSplitRight(150.0f, &Name, &Status);
			RenderClanMemberTee(GameClient(), RenderTools(), Tee, M.m_Skin);
			char aName[64];
			str_format(aName, sizeof(aName), "%s (%s)", M.m_aNickname, RoleLabel(M.m_Role));
			Ui()->DoLabel(&Name, aName, 12.0f, TEXTALIGN_ML);
			Ui()->DoLabel(&Status, M.m_Online ? Localize("online") : Localize("offline"), 11.0f, TEXTALIGN_MR);
		}
		Right.HSplitTop(3.0f, nullptr, &Right);
	}
	s_Scroll.End();
}

void CMenus::RenderClansApplications(CUIRect MainView)
{
	CClans &Clans = GameClient()->m_Clans;
	CUIRect Top, Label, Button, BackBtn, RefreshBtn;
	MainView.HSplitTop(28.0f, &Top, &MainView);
	Top.VSplitRight(28.0f, &Top, &RefreshBtn);
	Top.VSplitRight(4.0f, &Top, nullptr);
	Top.VSplitRight(28.0f, &Label, &BackBtn);
	Ui()->DoLabel(&Label, Localize("Applications"), 16.0f, TEXTALIGN_ML);
	static CButtonContainer s_Back, s_Refresh;
	if(DoClansIconBtn(Ui(), &s_Back, FontIcon::CHEVRON_LEFT, &BackBtn))
		Clans.SetView(CClans::EView::CLAN);
	if(DoClansIconBtn(Ui(), &s_Refresh, FontIcon::ARROW_ROTATE_RIGHT, &RefreshBtn) && !Clans.IsBusy())
		Clans.RefreshCurrentView();

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
	CUIRect Top, Label, Button, InputRow, BackBtn, RefreshBtn;
	MainView.HSplitTop(28.0f, &Top, &MainView);
	Top.VSplitRight(28.0f, &Top, &RefreshBtn);
	Top.VSplitRight(4.0f, &Top, nullptr);
	Top.VSplitRight(28.0f, &Label, &BackBtn);
	Ui()->DoLabel(&Label, Localize("Announcements"), 16.0f, TEXTALIGN_ML);
	static CButtonContainer s_Back, s_Refresh;
	if(DoClansIconBtn(Ui(), &s_Back, FontIcon::CHEVRON_LEFT, &BackBtn))
		Clans.SetView(CClans::EView::CLAN);
	if(DoClansIconBtn(Ui(), &s_Refresh, FontIcon::ARROW_ROTATE_RIGHT, &RefreshBtn) && !Clans.IsBusy())
		Clans.RefreshCurrentView();

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
			Ui()->DoLabel(&Item, Ann.m_aText, 12.0f, TEXTALIGN_TL);
		}
		MainView.HSplitTop(6.0f, nullptr, &MainView);
	}
	s_Scroll.End();
}

void CMenus::RenderClansRecent(CUIRect MainView)
{
	CClans &Clans = GameClient()->m_Clans;
	CUIRect Top, Label, BackBtn, RefreshBtn;
	MainView.HSplitTop(28.0f, &Top, &MainView);
	Top.VSplitRight(28.0f, &Top, &RefreshBtn);
	Top.VSplitRight(4.0f, &Top, nullptr);
	Top.VSplitRight(28.0f, &Label, &BackBtn);
	Ui()->DoLabel(&Label, Localize("Recent clans"), 16.0f, TEXTALIGN_ML);
	static CButtonContainer s_Back, s_Refresh;
	if(DoClansIconBtn(Ui(), &s_Back, FontIcon::CHEVRON_LEFT, &BackBtn))
		Clans.SetView(Clans.InClan() ? CClans::EView::CLAN : CClans::EView::LANDING);
	if(DoClansIconBtn(Ui(), &s_Refresh, FontIcon::ARROW_ROTATE_RIGHT, &RefreshBtn) && !Clans.IsBusy())
		Clans.RefreshCurrentView();

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
