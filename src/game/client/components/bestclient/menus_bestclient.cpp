/* Copyright © 2026 BestProject Team */
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/client.h>
#include <engine/keys.h>
#include <engine/updater.h>

#include <base/math.h>
#include <base/str.h>
#include <base/time.h>

#include <game/client/bc_menu_badges.h>
#include <game/client/bc_ui_animations.h>
#include <game/client/components/bestclient/fast_actions.h>
#include <game/client/components/bestclient/gradient.h>
#include <game/client/components/bestclient/bestclient.h>
#include <game/client/components/bestclient/cursor_trail.h>
#include <game/client/components/bestclient/physicball.h>
#include <game/client/components/bestclient/media_decoder.h>
#include <game/client/components/bestclient/menu_media_background.h>
#include <game/client/components/bestclient/settings_search.h>
#include <game/client/components/bestclient/ui_theme/style.h>
#include <game/client/components/bestclient/ui_theme/widgets.h>
#include <game/client/components/hud_layout.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <generated/client_data.h>
#include <generated/protocol.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// bestclient
enum
{
	BESTCLIENT_TAB_VISUALS = 0,
	BESTCLIENT_TAB_GAMEPLAY,
	BESTCLIENT_TAB_OTHERS,
	BESTCLIENT_TAB_INFO,
	NUM_BESTCLIENT_TABS,
};

static int s_CurBestClientTab = BESTCLIENT_TAB_VISUALS;
static bool s_ShowBestClientFun = false;

void CMenus::SetBestClientSettingsTab(int Tab)
{
	if(Tab < BESTCLIENT_TAB_VISUALS || Tab >= NUM_BESTCLIENT_TABS)
		Tab = BESTCLIENT_TAB_VISUALS;
	s_CurBestClientTab = Tab;
}

int CMenus::GetBestClientSettingsTab() const
{
	return s_CurBestClientTab;
}

void CMenus::OpenBestClientFun()
{
	s_CurBestClientTab = BESTCLIENT_TAB_INFO;
	s_ShowBestClientFun = true;
}

void CMenus::CloseBestClientFun()
{
	s_ShowBestClientFun = false;
}

void CMenus::FinishWelcomeToStylePicker()
{
	m_Popup = g_Config.m_BcShowStylePicker ? POPUP_STYLE_PICKER : POPUP_NONE;
}

void CMenus::RenderPopupStylePicker(CUIRect Box)
{
	CUIRect Buttons, Left, Right, Label, Row, Content, Tabs;
	Box.HSplitBottom(8.0f, &Box, nullptr);
	Box.HSplitBottom(28.0f, &Box, &Buttons);
	Box.HSplitBottom(8.0f, &Box, nullptr);
	Box.VMargin(16.0f, &Box);
	Buttons.VMargin(40.0f, &Buttons);

	Box.VSplitMid(&Left, &Right, 16.0f);

	const int SavedStyle = g_Config.m_BcMenuUiStyle;
	const int SavedCheckbox = g_Config.m_BcMenuUiNewCheckbox;
	const int SavedScrollbar = g_Config.m_BcMenuUiNewScrollbar;
	const int SavedColorPicker = g_Config.m_BcMenuUiNewColorPicker;
	const int SavedTabOption = g_Config.m_BcMenuUiNewTabOption;
	const int SavedTabSide = g_Config.m_BcMenuUiTabSide;
	const int SavedBetterFont = g_Config.m_BcMenuUiBetterFont;

	static int s_LegacyCheck = 1;
	static int s_NewCheck = 1;
	static float s_LegacyScroll = 0.35f;
	static float s_NewScroll = 0.65f;
	static unsigned s_LegacyColor = 0xE4A046AFU;
	static unsigned s_NewColor = 0xE4A046AFU;
	static CButtonContainer s_LegacyColorReset;
	static CButtonContainer s_NewColorReset;
	static CButtonContainer s_LegacyTabGeneral;
	static CButtonContainer s_LegacyTabAssets;
	static CButtonContainer s_LegacyTabTee;
	static CButtonContainer s_LegacyTabBestClient;
	static CButtonContainer s_NewTabDdnet;
	static CButtonContainer s_NewTabTclient;
	static CButtonContainer s_NewTabBestClient;
	static int s_LegacyTab = 0;
	static int s_NewTab = 2;

	const auto ApplyPreviewStyle = [&](bool BetterSide) {
		g_Config.m_BcMenuUiStyle = BetterSide ? 1 : 0;
		g_Config.m_BcMenuUiNewCheckbox = 1;
		g_Config.m_BcMenuUiNewScrollbar = 1;
		g_Config.m_BcMenuUiNewColorPicker = BetterSide ? 1 : 0;
		g_Config.m_BcMenuUiNewTabOption = BetterSide ? 1 : 0;
		g_Config.m_BcMenuUiTabSide = 1;
		g_Config.m_BcMenuUiBetterFont = BetterSide ? 1 : 0;
		if(BetterSide)
			TextRender()->SetCustomFace(BESTCLIENT_BETTER_FONT_FACE);
		else
			TextRender()->SetCustomFace("DejaVu Sans");
	};

	const auto DrawSide = [&](CUIRect Panel, bool BetterSide, int *pCheck, float *pScroll, unsigned *pColor, CButtonContainer *pColorReset, const char *pTitle) {
		Panel.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.35f), IGraphics::CORNER_ALL, 10.0f);
		Panel.Margin(10.0f, &Panel);

		ApplyPreviewStyle(BetterSide);

		Panel.HSplitTop(20.0f, &Label, &Panel);
		Ui()->DoLabel(&Label, pTitle, 18.0f, TEXTALIGN_MC);
		Panel.HSplitTop(8.0f, nullptr, &Panel);

		Panel.VSplitRight(100.0f, &Content, &Tabs);
		Content.VSplitRight(8.0f, &Content, nullptr);

		if(BetterSide)
		{
			Tabs.HSplitTop(26.0f, &Row, &Tabs);
			if(DoButton_MenuTab(&s_NewTabDdnet, BcLocalize("DDNet"), s_NewTab == 0, &Row, IGraphics::CORNER_R, nullptr, nullptr, nullptr, nullptr, 10.0f))
				s_NewTab = 0;
			Tabs.HSplitTop(8.0f, nullptr, &Tabs);
			Tabs.HSplitTop(26.0f, &Row, &Tabs);
			if(DoButton_MenuTab(&s_NewTabTclient, "TClient", s_NewTab == 1, &Row, IGraphics::CORNER_R, nullptr, nullptr, nullptr, nullptr, 10.0f))
				s_NewTab = 1;
			Tabs.HSplitTop(8.0f, nullptr, &Tabs);
			Tabs.HSplitTop(26.0f, &Row, &Tabs);
			if(DoButton_MenuTab(&s_NewTabBestClient, BcLocalize("BestClient"), s_NewTab == 2, &Row, IGraphics::CORNER_R, nullptr, nullptr, nullptr, nullptr, 10.0f))
				s_NewTab = 2;
		}
		else
		{
			Tabs.HSplitTop(22.0f, &Row, &Tabs);
			if(DoButton_MenuTab(&s_LegacyTabGeneral, BcLocalize("General"), s_LegacyTab == 0, &Row, IGraphics::CORNER_R, nullptr, nullptr, nullptr, nullptr, 4.0f))
				s_LegacyTab = 0;
			Tabs.HSplitTop(4.0f, nullptr, &Tabs);
			Tabs.HSplitTop(22.0f, &Row, &Tabs);
			if(DoButton_MenuTab(&s_LegacyTabAssets, BcLocalize("Assets"), s_LegacyTab == 1, &Row, IGraphics::CORNER_R, nullptr, nullptr, nullptr, nullptr, 4.0f))
				s_LegacyTab = 1;
			Tabs.HSplitTop(4.0f, nullptr, &Tabs);
			Tabs.HSplitTop(22.0f, &Row, &Tabs);
			if(DoButton_MenuTab(&s_LegacyTabTee, BcLocalize("Tee"), s_LegacyTab == 2, &Row, IGraphics::CORNER_R, nullptr, nullptr, nullptr, nullptr, 4.0f))
				s_LegacyTab = 2;
			Tabs.HSplitTop(4.0f, nullptr, &Tabs);
			Tabs.HSplitTop(22.0f, &Row, &Tabs);
			if(DoButton_MenuTab(&s_LegacyTabBestClient, BcLocalize("BestClient"), s_LegacyTab == 3, &Row, IGraphics::CORNER_R, nullptr, nullptr, nullptr, nullptr, 4.0f))
				s_LegacyTab = 3;
		}

		Content.HSplitTop(22.0f, &Row, &Content);
		if(DoButton_CheckBox(pCheck, BcLocalize("Checkbox"), *pCheck, &Row))
			*pCheck ^= 1;

		Content.HSplitTop(6.0f, nullptr, &Content);
		Content.HSplitTop(18.0f, &Row, &Content);
		Ui()->DoLabel(&Row, BcLocalize("Sample text Aa"), 16.0f, TEXTALIGN_ML);

		Content.HSplitTop(6.0f, nullptr, &Content);
		Content.HSplitTop(16.0f, &Row, &Content);
		*pScroll = Ui()->DoScrollbarH(pScroll, &Row, *pScroll);

		Content.HSplitTop(6.0f, nullptr, &Content);
		DoLine_ColorPicker(pColorReset, 25.0f, 13.0f, 0.0f, &Content, BcLocalize("Color"), pColor, color_cast<ColorRGBA>(ColorHSLA(0xE4A046AFU, true)), false, nullptr, true);
	};

	DrawSide(Left, false, &s_LegacyCheck, &s_LegacyScroll, &s_LegacyColor, &s_LegacyColorReset, BcLocalize("Legacy"));
	DrawSide(Right, true, &s_NewCheck, &s_NewScroll, &s_NewColor, &s_NewColorReset, BcLocalize("Better UI"));

	g_Config.m_BcMenuUiStyle = SavedStyle;
	g_Config.m_BcMenuUiNewCheckbox = SavedCheckbox;
	g_Config.m_BcMenuUiNewScrollbar = SavedScrollbar;
	g_Config.m_BcMenuUiNewColorPicker = SavedColorPicker;
	g_Config.m_BcMenuUiNewTabOption = SavedTabOption;
	g_Config.m_BcMenuUiTabSide = SavedTabSide;
	g_Config.m_BcMenuUiBetterFont = SavedBetterFont;
	BestClientApplyMenuFont(TextRender());

	if(Ui()->IsPopupOpen(&m_ColorPickerPopupContext))
	{
		if(m_ColorPickerPopupContext.m_pHslaColor == &s_LegacyColor)
		{
			g_Config.m_BcMenuUiStyle = 0;
			g_Config.m_BcMenuUiNewColorPicker = 0;
		}
		else if(m_ColorPickerPopupContext.m_pHslaColor == &s_NewColor)
		{
			g_Config.m_BcMenuUiStyle = 1;
			g_Config.m_BcMenuUiNewColorPicker = 1;
		}
	}

	CUIRect LegacyBtn, NewBtn;
	Buttons.VSplitMid(&LegacyBtn, &NewBtn, 20.0f);
	static CButtonContainer s_LegacyButton;
	static CButtonContainer s_NewButton;
	if(DoButton_Menu(&s_LegacyButton, BcLocalize("Choose Legacy"), 0, &LegacyBtn))
	{
		g_Config.m_BcMenuUiStyle = 0;
		g_Config.m_BcMenuUiBetterFont = 0;
		g_Config.m_BcShowStylePicker = 0;
		Ui()->ClosePopupMenus();
		BestClientRefreshMenuFont(GameClient());
		m_Popup = POPUP_NONE;
	}
	if(DoButton_Menu(&s_NewButton, BcLocalize("Choose Better UI"), 0, &NewBtn))
	{
		g_Config.m_BcMenuUiStyle = 1;
		g_Config.m_BcMenuUiNewCheckbox = 1;
		g_Config.m_BcMenuUiNewScrollbar = 1;
		g_Config.m_BcMenuUiNewColorPicker = 1;
		g_Config.m_BcMenuUiNewTabOption = 1;
		g_Config.m_BcMenuUiTabSide = 1;
		g_Config.m_BcMenuUiBetterFont = 1;
		g_Config.m_BcShowStylePicker = 0;
		Ui()->ClosePopupMenus();
		BestClientRefreshMenuFont(GameClient());
		m_Popup = POPUP_NONE;
	}
}

static unsigned BcUiThemeParseHex(const char *pValue)
{
	if(pValue[0] == '$')
		++pValue;
	return (unsigned)str_toulong_base(pValue, 16);
}

static bool BcUiThemeParseKvInt(const char *pToken, const char *pKey, int *pOut)
{
	const int KeyLen = str_length(pKey);
	if(str_comp_nocase_num(pToken, pKey, KeyLen) != 0 || pToken[KeyLen] != '=')
		return false;
	*pOut = str_toint(pToken + KeyLen + 1);
	return true;
}

static bool BcUiThemeParseKvColor(const char *pToken, const char *pKey, unsigned *pOut)
{
	const int KeyLen = str_length(pKey);
	if(str_comp_nocase_num(pToken, pKey, KeyLen) != 0 || pToken[KeyLen] != '=')
		return false;
	*pOut = BcUiThemeParseHex(pToken + KeyLen + 1);
	return true;
}

static bool BcUiThemeParseElemLine(const char *pLine, int *pEnable, unsigned *pColor, int *pGradient, unsigned *pGradientColor, int *pAccent, unsigned *pAccentColor, int *pOpacity)
{
	char aTok[128];
	const char *pCursor = pLine;
	bool First = true;
	while((pCursor = str_next_token(pCursor, " \t", aTok, sizeof(aTok))))
	{
		if(First)
		{
			First = false;
			continue;
		}
		BcUiThemeParseKvInt(aTok, "enable", pEnable) ||
			BcUiThemeParseKvColor(aTok, "color", pColor) ||
			BcUiThemeParseKvInt(aTok, "gradient", pGradient) ||
			BcUiThemeParseKvColor(aTok, "gradient_color", pGradientColor) ||
			BcUiThemeParseKvInt(aTok, "accent", pAccent) ||
			BcUiThemeParseKvColor(aTok, "accent_color", pAccentColor) ||
			(pOpacity && BcUiThemeParseKvInt(aTok, "opacity", pOpacity));
	}
	return true;
}

static bool BcUiThemeImportFromText(const char *pText)
{
	if(!pText || !str_startswith(pText, "BC_UI_THEME_EXPORT"))
		return false;

	const char *p = pText;
	while(*p)
	{
		char aLine[512];
		int i = 0;
		while(*p && *p != '\n' && *p != '\r' && i < (int)sizeof(aLine) - 1)
			aLine[i++] = *p++;
		aLine[i] = '\0';
		while(*p == '\n' || *p == '\r')
			++p;
		if(aLine[0] == '\0' || str_startswith(aLine, "BC_UI_THEME_EXPORT"))
			continue;

		if(str_startswith(aLine, "animate_speed="))
		{
			g_Config.m_BcCustomUiGradientAnimateSpeed = std::clamp(str_toint(aLine + 14), 0, 100);
			continue;
		}
		if(str_startswith(aLine, "background="))
		{
			g_Config.m_BcCustomUiBackground = str_toint(aLine + 11) != 0 ? 1 : 0;
			continue;
		}
		if(str_startswith(aLine, "ui_color="))
		{
			g_Config.m_UiColor = BcUiThemeParseHex(aLine + 9);
			continue;
		}

		auto ApplyElem = [&](const char *pName, int *pEnable, unsigned *pColor, int *pGradient, unsigned *pGradientColor, int *pAccent, unsigned *pAccentColor, int *pOpacity = nullptr) {
			if(str_startswith(aLine, pName) && (aLine[str_length(pName)] == ' ' || aLine[str_length(pName)] == '\t'))
				BcUiThemeParseElemLine(aLine, pEnable, pColor, pGradient, pGradientColor, pAccent, pAccentColor, pOpacity);
		};
		ApplyElem("text", &g_Config.m_BcCustomUiText, &g_Config.m_BcCustomUiTextColor, &g_Config.m_BcCustomUiTextGradient, &g_Config.m_BcCustomUiTextGradientColor, &g_Config.m_BcCustomUiTextUseAccent, &g_Config.m_BcCustomUiTextAccentColor);
		ApplyElem("checkbox", &g_Config.m_BcCustomUiCheckbox, &g_Config.m_BcCustomUiCheckboxColor, &g_Config.m_BcCustomUiCheckboxGradient, &g_Config.m_BcCustomUiCheckboxGradientColor, &g_Config.m_BcCustomUiCheckboxUseAccent, &g_Config.m_BcCustomUiCheckboxAccentColor);
		ApplyElem("scrollbar", &g_Config.m_BcCustomUiScrollbar, &g_Config.m_BcCustomUiScrollbarColor, &g_Config.m_BcCustomUiScrollbarGradient, &g_Config.m_BcCustomUiScrollbarGradientColor, &g_Config.m_BcCustomUiScrollbarUseAccent, &g_Config.m_BcCustomUiScrollbarAccentColor);
		ApplyElem("button", &g_Config.m_BcCustomUiButton, &g_Config.m_BcCustomUiButtonColor, &g_Config.m_BcCustomUiButtonGradient, &g_Config.m_BcCustomUiButtonGradientColor, &g_Config.m_BcCustomUiButtonUseAccent, &g_Config.m_BcCustomUiButtonAccentColor);
		ApplyElem("droplist", &g_Config.m_BcCustomUiDropdown, &g_Config.m_BcCustomUiDropdownColor, &g_Config.m_BcCustomUiDropdownGradient, &g_Config.m_BcCustomUiDropdownGradientColor, &g_Config.m_BcCustomUiDropdownUseAccent, &g_Config.m_BcCustomUiDropdownAccentColor);
		ApplyElem("input", &g_Config.m_BcCustomUiEditBox, &g_Config.m_BcCustomUiEditBoxColor, &g_Config.m_BcCustomUiEditBoxGradient, &g_Config.m_BcCustomUiEditBoxGradientColor, &g_Config.m_BcCustomUiEditBoxUseAccent, &g_Config.m_BcCustomUiEditBoxAccentColor);
		ApplyElem("block", &g_Config.m_BcCustomUiBlock, &g_Config.m_BcCustomUiBlockColor, &g_Config.m_BcCustomUiBlockGradient, &g_Config.m_BcCustomUiBlockGradientColor, &g_Config.m_BcCustomUiBlockUseAccent, &g_Config.m_BcCustomUiBlockAccentColor, &g_Config.m_BcCustomUiBlockOpacity);
		ApplyElem("panel", &g_Config.m_BcCustomUiMainPanel, &g_Config.m_BcCustomUiMainPanelColor, &g_Config.m_BcCustomUiMainPanelGradient, &g_Config.m_BcCustomUiMainPanelGradientColor, &g_Config.m_BcCustomUiMainPanelUseAccent, &g_Config.m_BcCustomUiMainPanelAccentColor, &g_Config.m_BcCustomUiMainPanelOpacity);
		ApplyElem("nav", &g_Config.m_BcCustomUiNavTab, &g_Config.m_BcCustomUiNavTabColor, &g_Config.m_BcCustomUiNavTabGradient, &g_Config.m_BcCustomUiNavTabGradientColor, &g_Config.m_BcCustomUiNavTabUseAccent, &g_Config.m_BcCustomUiNavTabAccentColor, &g_Config.m_BcCustomUiNavTabOpacity);
		ApplyElem("badge", &g_Config.m_BcCustomUiBadge, &g_Config.m_BcCustomUiBadgeColor, &g_Config.m_BcCustomUiBadgeGradient, &g_Config.m_BcCustomUiBadgeGradientColor, &g_Config.m_BcCustomUiBadgeUseAccent, &g_Config.m_BcCustomUiBadgeAccentColor);
		ApplyElem("binds", &g_Config.m_BcCustomUiBind, &g_Config.m_BcCustomUiBindColor, &g_Config.m_BcCustomUiBindGradient, &g_Config.m_BcCustomUiBindGradientColor, &g_Config.m_BcCustomUiBindUseAccent, &g_Config.m_BcCustomUiBindAccentColor);
	}
	g_Config.m_BcCustomUiBlockOpacity = std::clamp(g_Config.m_BcCustomUiBlockOpacity, 0, 100);
	g_Config.m_BcCustomUiMainPanelOpacity = std::clamp(g_Config.m_BcCustomUiMainPanelOpacity, 0, 100);
	g_Config.m_BcCustomUiNavTabOpacity = std::clamp(g_Config.m_BcCustomUiNavTabOpacity, 0, 100);
	return true;
}

void CMenus::RenderSettingsBestClientChatMediaBlock(CUIRect &Column)
{
	const float LineSize = 20.0f;
	const float MarginSmall = 5.0f;
	const float HeadlineFontSize = 20.0f;
	const float MarginBetweenViews = 30.0f;
	const float BlockPadding = MarginBetweenViews * 0.6666f;

	const bool Expanded = g_Config.m_BcChatMediaPreview != 0;
	const float DomainsHeight = g_Config.m_BcChatMediaContentFilter ? 2.0f * (MarginSmall + LineSize) : 0.0f;
	const float ExpandedTargetHeight = 5.0f * (MarginSmall + LineSize) + DomainsHeight;
	const float ExpandedHeight = BC_MODULE_REVEAL(Expanded, ExpandedTargetHeight, Client()->RenderFrameTime());
	const float HeaderHeight = LineSize + MarginSmall + LineSize;
	const float BlockHeight = HeaderHeight + ExpandedHeight;

	CUIRect Block;
	Column.HSplitTop(BlockHeight, &Block, &Column);
	CUIRect BlockBg = Block;
	BlockBg.w += BlockPadding;
	BlockBg.h += BlockPadding;
	BlockBg.x -= BlockPadding * 0.5f;
	BlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&BlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcChatMediaPreview, &BlockBg);

	CUIRect Content, Label, Button;
	Block.HSplitTop(LineSize, &Label, &Block);
	Ui()->DoLabel(&Label, BcLocalize("Chat Media"), HeadlineFontSize, TEXTALIGN_ML);
	Block.HSplitTop(MarginSmall, nullptr, &Block);

	CChat &Chat = GameClient()->m_Chat;
	Block.HSplitTop(LineSize, &Content, &Block);
	if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatMediaPreview, BcLocalize("Render media previews from chat links"), &g_Config.m_BcChatMediaPreview, &Content, LineSize))
		Chat.RebuildChat();

	if(ExpandedHeight <= 0.5f)
		return;

	CUIRect Visible = Block;
	Visible.h = ExpandedHeight;
	Ui()->ClipEnable(&Visible);

	Block.HSplitTop(MarginSmall, nullptr, &Block);
	Block.HSplitTop(LineSize, &Content, &Block);
	if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatMediaPhotos, BcLocalize("Show photos in chat media"), &g_Config.m_BcChatMediaPhotos, &Content, LineSize))
		Chat.RebuildChat();

	Block.HSplitTop(MarginSmall, nullptr, &Block);
	Block.HSplitTop(LineSize, &Content, &Block);
	if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatMediaGifs, BcLocalize("Show GIFs in chat media"), &g_Config.m_BcChatMediaGifs, &Content, LineSize))
		Chat.RebuildChat();

	Block.HSplitTop(MarginSmall, nullptr, &Block);
	Block.HSplitTop(LineSize, &Content, &Block);
	if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatMediaContentFilter, BcLocalize("Content filtering"), &g_Config.m_BcChatMediaContentFilter, &Content, LineSize))
		Chat.RebuildChat();

	if(g_Config.m_BcChatMediaContentFilter)
	{
		Block.HSplitTop(MarginSmall, nullptr, &Block);
		Block.HSplitTop(LineSize, &Label, &Block);
		Ui()->DoLabel(&Label, BcLocalize("Allowed media domains"), 12.0f, TEXTALIGN_ML);
		Block.HSplitTop(MarginSmall, nullptr, &Block);
		Block.HSplitTop(LineSize, &Button, &Block);
		static CLineInput DomainsInput(g_Config.m_BcChatMediaAllowedDomains, sizeof(g_Config.m_BcChatMediaAllowedDomains));
		DomainsInput.SetEmptyText("tenor.com; imgur.com; giphy.com; gifs.teeworlds.xyz");
		if(Ui()->DoClearableEditBox(&DomainsInput, &Button, 14.0f))
			Chat.RebuildChat();
	}

	Block.HSplitTop(MarginSmall, nullptr, &Block);
	Block.HSplitTop(LineSize, &Button, &Block);
	if(Ui()->DoScrollbarOption(&g_Config.m_BcChatMediaPreviewMaxWidth, &g_Config.m_BcChatMediaPreviewMaxWidth, &Button, BcLocalize("Media preview width"), 120, 400))
		Chat.RebuildChat();

	Block.HSplitTop(MarginSmall, nullptr, &Block);
	Block.HSplitTop(LineSize, &Label, &Block);
	static CButtonContainer HideMediaBindReader, HideMediaBindClear;
	DoLine_KeyReader(Label, HideMediaBindReader, HideMediaBindClear, BcLocalize("Hide media bind"), "toggle_chat_media_hidden");
	Ui()->ClipDisable();
}

static void SetBestClientTabFlag(int32_t &Flags, int Tab, bool Hidden)
{
	if(Hidden)
		Flags |= (1 << Tab);
	else
		Flags &= ~(1 << Tab);
}

static bool IsBestClientTabFlagSet(int32_t Flags, int Tab)
{
	return (Flags & (1 << Tab)) != 0;
}
// bestclient

void CMenus::RenderSettingsBestClient(CUIRect MainView)
{
	// bestclient
	const float TopExtend = BestClientUiTheme::IsNewTabOption() ? 8.0f : 20.0f;
	MainView.y -= TopExtend;
	MainView.h += TopExtend;
	// bestclient

	static CButtonContainer s_aPageTabs[NUM_BESTCLIENT_TABS] = {};

	MainView.HSplitTop(BestClientUiTheme::IsNewTabOption() ? 6.0f : 8.0f, nullptr, &MainView);

	auto IsTabHidden = [&](int Tab) {
		return Tab != BESTCLIENT_TAB_INFO && IsBestClientTabFlagSet(g_Config.m_BcBestClientSettingsTabs, Tab);
	};

	const char *apTabNames[NUM_BESTCLIENT_TABS] = {
		BcLocalize("Visuals"),
		BcLocalize("Gameplay"),
		BcLocalize("Others"),
		BcLocalize("Info"),
	};
	const int aTabOrder[NUM_BESTCLIENT_TABS] = {
		BESTCLIENT_TAB_VISUALS,
		BESTCLIENT_TAB_GAMEPLAY,
		BESTCLIENT_TAB_OTHERS,
		BESTCLIENT_TAB_INFO,
	};

	CButtonContainer *apVisibleButtons[NUM_BESTCLIENT_TABS];
	const char *apVisibleNames[NUM_BESTCLIENT_TABS];
	int aVisibleValues[NUM_BESTCLIENT_TABS];
	int VisibleCount = 0;
	int FirstVisibleTab = -1;
	for(const int Tab : aTabOrder)
	{
		if(IsTabHidden(Tab))
			continue;
		if(FirstVisibleTab == -1)
			FirstVisibleTab = Tab;
		apVisibleButtons[VisibleCount] = &s_aPageTabs[Tab];
		apVisibleNames[VisibleCount] = apTabNames[Tab];
		aVisibleValues[VisibleCount] = Tab;
		++VisibleCount;
	}

	if(FirstVisibleTab == -1)
	{
		s_CurBestClientTab = BESTCLIENT_TAB_INFO;
		FirstVisibleTab = BESTCLIENT_TAB_INFO;
		VisibleCount = 1;
		apVisibleButtons[0] = &s_aPageTabs[BESTCLIENT_TAB_INFO];
		apVisibleNames[0] = apTabNames[BESTCLIENT_TAB_INFO];
		aVisibleValues[0] = BESTCLIENT_TAB_INFO;
	}

	if(s_CurBestClientTab < BESTCLIENT_TAB_VISUALS || s_CurBestClientTab >= NUM_BESTCLIENT_TABS || IsTabHidden(s_CurBestClientTab))
		s_CurBestClientTab = FirstVisibleTab;

	int SelectedVisibleIndex = 0;
	// bestclient
	if(!BestClientUiTheme::IsNewTabOption())
	{
		CUIRect TabBar, TabButton;
		MainView.HSplitTop(24.0f, &TabBar, &MainView);
		const float TabWidth = TabBar.w / (float)VisibleCount;
		for(int i = 0; i < VisibleCount; ++i)
		{
			TabBar.VSplitLeft(TabWidth, &TabButton, &TabBar);
			const int Corners = i == 0 ? IGraphics::CORNER_L : (i == VisibleCount - 1 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
			if(DoButton_MenuTab(apVisibleButtons[i], apVisibleNames[i], s_CurBestClientTab == aVisibleValues[i], &TabButton, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
			{
				s_CurBestClientTab = aVisibleValues[i];
				s_ShowBestClientFun = false;
			}
			if(aVisibleValues[i] == s_CurBestClientTab)
				SelectedVisibleIndex = i;
		}
		MainView.HSplitTop(10.0f, nullptr, &MainView);
	}
	else
	{
		for(int i = 0; i < VisibleCount; ++i)
		{
			if(aVisibleValues[i] == s_CurBestClientTab)
				SelectedVisibleIndex = i;
		}
	}
	// bestclient

	if(s_ShowBestClientFun)
	{
		RenderSettingsBestClientFun(MainView);
		return;
	}

	auto RenderTabContent = [&](int Tab, CUIRect Content) {
		if(Tab == BESTCLIENT_TAB_VISUALS)
			RenderSettingsBestClientVisuals(Content);
		else if(Tab == BESTCLIENT_TAB_GAMEPLAY)
			RenderSettingsBestClientGameplay(Content);
		else if(Tab == BESTCLIENT_TAB_OTHERS)
			RenderSettingsBestClientOthers(Content);
		else if(Tab == BESTCLIENT_TAB_INFO)
			RenderSettingsBestClientInfo(Content);
	};

	// bestclient
	if(BestClientUiTheme::IsNewTabOption())
	{
		RenderTabContent(s_CurBestClientTab, MainView);
		return;
	}
	// bestclient

	const BCUiAnimations::STabSwitch TabSwitch = BCUiAnimations::AnimateTabSwitch(SelectedVisibleIndex, VisibleCount, Client()->RenderFrameTime(), 0);
	const int FromTab = aVisibleValues[TabSwitch.m_FromIndex];
	const int ToTab = aVisibleValues[TabSwitch.m_ToIndex];
	if(TabSwitch.m_Progress >= 1.0f || FromTab == ToTab)
	{
		RenderTabContent(ToTab, MainView);
		return;
	}

	float FromOffset = 0.0f;
	float ToOffset = 0.0f;
	BCUiAnimations::TabSwitchContentOffsets(TabSwitch.m_Progress, TabSwitch.m_FromIndex, TabSwitch.m_ToIndex, MainView.w, FromOffset, ToOffset);

	CUIRect FromView = MainView;
	CUIRect ToView = MainView;
	FromView.x += FromOffset;
	ToView.x += ToOffset;

	Ui()->ClipEnable(&MainView);
	RenderTabContent(FromTab, FromView);
	RenderTabContent(ToTab, ToView);
	Ui()->ClipDisable();
}

void CMenus::RenderSettingsBestClientVisuals(CUIRect MainView)
{
	const float LineSize = 20.0f;
	const float MarginSmall = 5.0f;
	const float HeadlineFontSize = 20.0f;
	const float MarginBetweenViews = 30.0f;
	const float BlockPadding = MarginBetweenViews * 0.6666f;
	const auto DoHighFpsIntakeTooltip = [&](const void *pId, const CUIRect &Row) {
		GameClient()->m_Tooltips.DoToolTip(pId, &Row, BcLocalize("high fps intake"));
		GameClient()->m_Tooltips.SetFadeTime(pId, 0.0f);
		GameClient()->m_Tooltips.SetTextColor(pId, ColorRGBA(1.0f, 0.55f, 0.16f, 1.0f));
	};

	{
		CUIRect HudButtonRow;
		MainView.HSplitTop(24.0f, &HudButtonRow, &MainView);
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		static CButtonContainer s_HudEditorButton;
		const bool CanOpen = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
		if(DoButton_MenuTab(&s_HudEditorButton, BcLocalize("HUD editor"), 0, &HudButtonRow, IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 4.0f) && CanOpen)
		{
			SetActive(false);
			GameClient()->m_HudEditor.Activate();
		}
		GameClient()->m_Tooltips.DoToolTip(&s_HudEditorButton, &HudButtonRow, CanOpen ? BcLocalize("Open in HUD editor") : BcLocalize("Join a game first"));
		GameClient()->m_Tooltips.SetFadeTime(&s_HudEditorButton, 0.0f);
	}

	static CScrollRegion s_VisualsScrollRegion;
	CScrollRegionParams VisualsScrollParams;
	VisualsScrollParams.m_ScrollUnit = 60.0f;
	VisualsScrollParams.m_ScrollbarMargin = 5.0f;
	s_VisualsScrollRegion.Begin(&MainView, &VisualsScrollParams);
	MainView.VSplitRight(5.0f, &MainView, nullptr);
	MainView.VSplitLeft(5.0f, nullptr, &MainView);
	const float VisualsScrollContentX = MainView.x;
	const float VisualsScrollContentW = MainView.w;

	CUIRect Content, Label, Button, LeftView, RightView, Column;

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	Column = LeftView;
	Column.HSplitTop(10.0f, nullptr, &Column);

	const float RealHitboxColorLineSize = 25.0f;
	const float RealHitboxColorLineSpacing = 5.0f;
	const float RealHitboxColorHeight = g_Config.m_BcShowRealHitbox ? RealHitboxColorLineSize + RealHitboxColorLineSpacing : 0.0f;
	const float VisualsQoLBlockHeight =
		LineSize + MarginSmall +
		8.0f * LineSize +
		RealHitboxColorHeight +
		3.0f * LineSize;
	CUIRect VisualsQoLBlock;
	Column.HSplitTop(VisualsQoLBlockHeight, &VisualsQoLBlock, &Column);

	CUIRect VisualsQoLBlockBg = VisualsQoLBlock;
	VisualsQoLBlockBg.w += BlockPadding;
	VisualsQoLBlockBg.h += BlockPadding;
	VisualsQoLBlockBg.x -= BlockPadding * 0.5f;
	VisualsQoLBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&VisualsQoLBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcServerMapPreview, &VisualsQoLBlockBg);

	VisualsQoLBlock.HSplitTop(LineSize, &Label, &VisualsQoLBlock);
	Ui()->DoLabel(&Label, BcLocalize("Visuals QoL"), HeadlineFontSize, TEXTALIGN_ML);
	VisualsQoLBlock.HSplitTop(MarginSmall, nullptr, &VisualsQoLBlock);

	{
		CUIRect MapPreviewRow;
		VisualsQoLBlock.HSplitTop(LineSize, &MapPreviewRow, &VisualsQoLBlock);
		BcMenuBadges::DrawNew(Graphics(), Ui(), TextRender(), &MapPreviewRow, MarginSmall);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcServerMapPreview, BcLocalize("Map preview in server list"), &g_Config.m_BcServerMapPreview, &MapPreviewRow, LineSize);
	}
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcShowRealHitbox, BcLocalize("Show real hitbox"), &g_Config.m_BcShowRealHitbox, &VisualsQoLBlock, LineSize);
	if(g_Config.m_BcShowRealHitbox)
	{
		static CButtonContainer s_RealHitboxDotColorButton;
		DoLine_ColorPicker(&s_RealHitboxDotColorButton, RealHitboxColorLineSize, 13.0f, RealHitboxColorLineSpacing, &VisualsQoLBlock, BcLocalize("Real hitbox dot color"), &g_Config.m_BcShowRealHitboxColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::BcShowRealHitboxColor, true)), false, nullptr, true);
	}
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcShowhudDummyCoordIndicator, BcLocalize("Show player below indicator"), &g_Config.m_BcShowhudDummyCoordIndicator, &VisualsQoLBlock, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcShowCorrectCheckpoint, BcLocalize("Show current checkpoint in hud"), &g_Config.m_BcShowCorrectCheckpoint, &VisualsQoLBlock, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcShowPointsInTab, BcLocalize("Show points in tab"), &g_Config.m_BcShowPointsInTab, &VisualsQoLBlock, LineSize);
	{
		CUIRect SpectatorNamesRow;
		VisualsQoLBlock.HSplitTop(LineSize, &SpectatorNamesRow, &VisualsQoLBlock);
		BcMenuBadges::DrawBeta(Graphics(), Ui(), TextRender(), &SpectatorNamesRow, MarginSmall);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcShowSpectatorNames, BcLocalize("Show spectator nicknames"), &g_Config.m_BcShowSpectatorNames, &SpectatorNamesRow, LineSize);
	}
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcShowVotePercentage, BcLocalize("Show vote percentage"), &g_Config.m_BcShowVotePercentage, &VisualsQoLBlock, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcDiscordRPC, BcLocalize("Bestclient discord rpc (restart requires)"), &g_Config.m_BcDiscordRPC, &VisualsQoLBlock, LineSize);
	VisualsQoLBlock.HSplitTop(LineSize, &Button, &VisualsQoLBlock);
	Ui()->DoScrollbarOption(&g_Config.m_BcWheelScale, &g_Config.m_BcWheelScale, &Button, BcLocalize("Bind/Emote wheel scale"), 50, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, "%");
	VisualsQoLBlock.HSplitTop(LineSize, &Button, &VisualsQoLBlock);
	Ui()->DoScrollbarOption(&g_Config.m_BcScoreboardScale, &g_Config.m_BcScoreboardScale, &Button, BcLocalize("Scoreboard scale"), 50, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, "%");
	VisualsQoLBlock.HSplitTop(LineSize, &Button, &VisualsQoLBlock);
	Ui()->DoScrollbarOption(&g_Config.m_UiScale, &g_Config.m_UiScale, &Button, BcLocalize("UI scale"), 50, 110, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, "%");

	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	const bool JellyExpanded = g_Config.m_BcJellyTee != 0;
	const float JellyHeaderHeight = LineSize + MarginSmall + LineSize;
	const float JellyExpandedTargetHeight = MarginSmall + LineSize + MarginSmall + LineSize + MarginSmall + LineSize;
	const float JellyExpandedHeight = BC_MODULE_REVEAL(JellyExpanded, JellyExpandedTargetHeight, Client()->RenderFrameTime());
	const float JellyBlockHeight = JellyHeaderHeight + JellyExpandedHeight;

	CUIRect JellyBlock;
	Column.HSplitTop(JellyBlockHeight, &JellyBlock, &Column);

	CUIRect JellyBlockBg = JellyBlock;
	JellyBlockBg.w += BlockPadding;
	JellyBlockBg.h += BlockPadding;
	JellyBlockBg.x -= BlockPadding * 0.5f;
	JellyBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&JellyBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcJellyTee, &JellyBlockBg);

	MainView = JellyBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	CUIRect JellyTitleLabel, JellyResetButton;
	Label.VSplitRight(LineSize + 8.0f, &JellyTitleLabel, &JellyResetButton);
	static CButtonContainer s_JellyTeeResetButton;
	const bool JellyTeeResetClicked = Ui()->DoButton_FontIcon(&s_JellyTeeResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &JellyResetButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_JellyTeeResetButton, &JellyResetButton, BcLocalize("Reset to defaults"));
	if(JellyTeeResetClicked)
	{
		g_Config.m_BcJellyTeeOthers = DefaultConfig::BcJellyTeeOthers;
		g_Config.m_BcJellyTeeStrength = DefaultConfig::BcJellyTeeStrength;
		g_Config.m_BcJellyTeeDuration = DefaultConfig::BcJellyTeeDuration;
	}
	Ui()->DoLabel(&JellyTitleLabel, BcLocalize("Jelly Tee"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcJellyTee, BcLocalize("Enable Jelly Tee"), &g_Config.m_BcJellyTee, &Content, LineSize);

	if(JellyExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = JellyExpandedHeight;
		Ui()->ClipEnable(&Visible);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcJellyTeeOthers, BcLocalize("Jelly Others"), &g_Config.m_BcJellyTeeOthers, &Content, LineSize);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcJellyTeeStrength, &g_Config.m_BcJellyTeeStrength, &Button, BcLocalize("Jelly strength"), 1, 500);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcJellyTeeDuration, &g_Config.m_BcJellyTeeDuration, &Button, BcLocalize("Jelly duration"), 1, 80);

		Ui()->ClipDisable();
	}

	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	RenderSettingsBestClientWeaponGlow(Column);

	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	const bool CornerIndicatorEnabled = g_Config.m_BcCornerIndicator != 0;
	const bool CornerIndicatorShowSizes = CornerIndicatorEnabled && g_Config.m_BcCornerIndicatorProximity == 0;
	const bool CornerIndicatorShowCustomColors = CornerIndicatorEnabled && g_Config.m_BcCornerIndicatorCustomColors != 0;
	const float CornerIndicatorColorPickerLineSize = 25.0f;
	const float CornerIndicatorColorPickerSpacing = 5.0f;
	const float CornerIndicatorExpandedTargetHeight =
		7.0f * (MarginSmall + LineSize) +
		(CornerIndicatorShowSizes ? 3.0f * (MarginSmall + LineSize) : 0.0f) +
		(CornerIndicatorShowCustomColors ? 3.0f * (CornerIndicatorColorPickerSpacing + CornerIndicatorColorPickerLineSize) : 0.0f);
	const float CornerIndicatorExpandedHeight = BC_MODULE_REVEAL(CornerIndicatorEnabled, CornerIndicatorExpandedTargetHeight, Client()->RenderFrameTime());
	const float CornerIndicatorBlockHeight = LineSize + MarginSmall + LineSize + CornerIndicatorExpandedHeight;
	CUIRect CornerIndicatorBlock;
	Column.HSplitTop(CornerIndicatorBlockHeight, &CornerIndicatorBlock, &Column);

	CUIRect CornerIndicatorBlockBg = CornerIndicatorBlock;
	CornerIndicatorBlockBg.w += BlockPadding;
	CornerIndicatorBlockBg.h += BlockPadding;
	CornerIndicatorBlockBg.x -= BlockPadding * 0.5f;
	CornerIndicatorBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&CornerIndicatorBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcCornerIndicator, &CornerIndicatorBlockBg);

	CUIRect CornerIndicatorView = CornerIndicatorBlock;
	CornerIndicatorView.HSplitTop(LineSize, &Label, &CornerIndicatorView);
	CUIRect CornerIndicatorTitleLabel, CornerIndicatorResetButton;
	Label.VSplitRight(LineSize + 8.0f, &CornerIndicatorTitleLabel, &CornerIndicatorResetButton);
	static CButtonContainer s_CornerIndicatorResetButton;
	const bool CornerIndicatorResetClicked = Ui()->DoButton_FontIcon(&s_CornerIndicatorResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &CornerIndicatorResetButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_CornerIndicatorResetButton, &CornerIndicatorResetButton, BcLocalize("Reset to defaults"));
	if(CornerIndicatorResetClicked)
	{
		g_Config.m_BcCornerIndicatorOnlyInTeam = DefaultConfig::BcCornerIndicatorOnlyInTeam;
		g_Config.m_BcCornerIndicatorShowAlive = DefaultConfig::BcCornerIndicatorShowAlive;
		g_Config.m_BcCornerIndicatorShowFrozen = DefaultConfig::BcCornerIndicatorShowFrozen;
		g_Config.m_BcCornerIndicatorIndicateLastTeammate = DefaultConfig::BcCornerIndicatorIndicateLastTeammate;
		g_Config.m_BcCornerIndicatorProximity = DefaultConfig::BcCornerIndicatorProximity;
		g_Config.m_BcCornerIndicatorProximityRange = DefaultConfig::BcCornerIndicatorProximityRange;
		g_Config.m_BcCornerIndicatorProximityMinSize = DefaultConfig::BcCornerIndicatorProximityMinSize;
		g_Config.m_BcCornerIndicatorNinjaFrozen = DefaultConfig::BcCornerIndicatorNinjaFrozen;
		g_Config.m_BcCornerIndicatorSizeAlive = DefaultConfig::BcCornerIndicatorSizeAlive;
		g_Config.m_BcCornerIndicatorSizeFrozen = DefaultConfig::BcCornerIndicatorSizeFrozen;
		g_Config.m_BcCornerIndicatorLastTeammateSize = DefaultConfig::BcCornerIndicatorLastTeammateSize;
		g_Config.m_BcCornerIndicatorCustomColors = DefaultConfig::BcCornerIndicatorCustomColors;
		g_Config.m_BcCornerIndicatorColorAlive = DefaultConfig::BcCornerIndicatorColorAlive;
		g_Config.m_BcCornerIndicatorColorFrozen = DefaultConfig::BcCornerIndicatorColorFrozen;
		g_Config.m_BcCornerIndicatorLastTeammateColor = DefaultConfig::BcCornerIndicatorLastTeammateColor;
	}
	CUIRect CornerIndicatorAuthorBadge;
	static CButtonContainer s_CornerIndicatorAuthorBadge;
	CornerIndicatorTitleLabel.VSplitRight(6.0f, &CornerIndicatorTitleLabel, nullptr);
	BcMenuBadges::DrawAuthor(Graphics(), Ui(), TextRender(), &CornerIndicatorTitleLabel, 4.0f, &CornerIndicatorAuthorBadge);
	Ui()->DoButtonLogic(&s_CornerIndicatorAuthorBadge, 0, &CornerIndicatorAuthorBadge, BUTTONFLAG_NONE);
	GameClient()->m_Tooltips.DoToolTip(&s_CornerIndicatorAuthorBadge, &CornerIndicatorAuthorBadge, "blanc");
	GameClient()->m_Tooltips.SetFadeTime(&s_CornerIndicatorAuthorBadge, 0.0f);
	BcMenuBadges::DrawNew(Graphics(), Ui(), TextRender(), &CornerIndicatorTitleLabel, 4.0f);
	Ui()->DoLabel(&CornerIndicatorTitleLabel, BcLocalize("Corner indicator"), HeadlineFontSize, TEXTALIGN_ML);
	CornerIndicatorView.HSplitTop(MarginSmall, nullptr, &CornerIndicatorView);

	CornerIndicatorView.HSplitTop(LineSize, &Content, &CornerIndicatorView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcCornerIndicator, BcLocalize("Enable corner indicator"), &g_Config.m_BcCornerIndicator, &Content, LineSize);

	if(CornerIndicatorExpandedHeight > 0.5f)
	{
		CUIRect Visible = CornerIndicatorView;
		Visible.h = CornerIndicatorExpandedHeight;
		Ui()->ClipEnable(&Visible);

		CornerIndicatorView.HSplitTop(MarginSmall, nullptr, &CornerIndicatorView);
		CornerIndicatorView.HSplitTop(LineSize, &Content, &CornerIndicatorView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcCornerIndicatorOnlyInTeam, BcLocalize("Only in team"), &g_Config.m_BcCornerIndicatorOnlyInTeam, &Content, LineSize);

		CornerIndicatorView.HSplitTop(MarginSmall, nullptr, &CornerIndicatorView);
		CornerIndicatorView.HSplitTop(LineSize, &Content, &CornerIndicatorView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcCornerIndicatorShowAlive, BcLocalize("Show alive teammates"), &g_Config.m_BcCornerIndicatorShowAlive, &Content, LineSize);

		CornerIndicatorView.HSplitTop(MarginSmall, nullptr, &CornerIndicatorView);
		CornerIndicatorView.HSplitTop(LineSize, &Content, &CornerIndicatorView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcCornerIndicatorShowFrozen, BcLocalize("Show frozen teammates"), &g_Config.m_BcCornerIndicatorShowFrozen, &Content, LineSize);

		CornerIndicatorView.HSplitTop(MarginSmall, nullptr, &CornerIndicatorView);
		CornerIndicatorView.HSplitTop(LineSize, &Content, &CornerIndicatorView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcCornerIndicatorIndicateLastTeammate, BcLocalize("Indicate last teammate"), &g_Config.m_BcCornerIndicatorIndicateLastTeammate, &Content, LineSize);

		CornerIndicatorView.HSplitTop(MarginSmall, nullptr, &CornerIndicatorView);
		CornerIndicatorView.HSplitTop(LineSize, &Content, &CornerIndicatorView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcCornerIndicatorProximity, BcLocalize("Scale by distance"), &g_Config.m_BcCornerIndicatorProximity, &Content, LineSize);

		CornerIndicatorView.HSplitTop(MarginSmall, nullptr, &CornerIndicatorView);
		CornerIndicatorView.HSplitTop(LineSize, &Content, &CornerIndicatorView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcCornerIndicatorNinjaFrozen, BcLocalize("Ninja skin when frozen"), &g_Config.m_BcCornerIndicatorNinjaFrozen, &Content, LineSize);

		if(CornerIndicatorShowSizes)
		{
			CornerIndicatorView.HSplitTop(MarginSmall, nullptr, &CornerIndicatorView);
			CornerIndicatorView.HSplitTop(LineSize, &Button, &CornerIndicatorView);
			Ui()->DoScrollbarOption(&g_Config.m_BcCornerIndicatorSizeAlive, &g_Config.m_BcCornerIndicatorSizeAlive, &Button, BcLocalize("Alive size"), 12, 128);

			CornerIndicatorView.HSplitTop(MarginSmall, nullptr, &CornerIndicatorView);
			CornerIndicatorView.HSplitTop(LineSize, &Button, &CornerIndicatorView);
			Ui()->DoScrollbarOption(&g_Config.m_BcCornerIndicatorSizeFrozen, &g_Config.m_BcCornerIndicatorSizeFrozen, &Button, BcLocalize("Frozen size"), 12, 128);

			CornerIndicatorView.HSplitTop(MarginSmall, nullptr, &CornerIndicatorView);
			CornerIndicatorView.HSplitTop(LineSize, &Button, &CornerIndicatorView);
			Ui()->DoScrollbarOption(&g_Config.m_BcCornerIndicatorLastTeammateSize, &g_Config.m_BcCornerIndicatorLastTeammateSize, &Button, BcLocalize("Last teammate size"), 12, 128);
		}

		CornerIndicatorView.HSplitTop(MarginSmall, nullptr, &CornerIndicatorView);
		CornerIndicatorView.HSplitTop(LineSize, &Content, &CornerIndicatorView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcCornerIndicatorCustomColors, BcLocalize("Use custom colors"), &g_Config.m_BcCornerIndicatorCustomColors, &Content, LineSize);

		if(CornerIndicatorShowCustomColors)
		{
			static CButtonContainer s_CornerIndicatorColorAliveButton;
			static CButtonContainer s_CornerIndicatorColorFrozenButton;
			static CButtonContainer s_CornerIndicatorLastTeammateColorButton;
			DoLine_ColorPicker(&s_CornerIndicatorColorAliveButton, CornerIndicatorColorPickerLineSize, 13.0f, CornerIndicatorColorPickerSpacing, &CornerIndicatorView, BcLocalize("Alive color"), &g_Config.m_BcCornerIndicatorColorAlive, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::BcCornerIndicatorColorAlive, true)), false, nullptr, true);
			DoLine_ColorPicker(&s_CornerIndicatorColorFrozenButton, CornerIndicatorColorPickerLineSize, 13.0f, CornerIndicatorColorPickerSpacing, &CornerIndicatorView, BcLocalize("Frozen color"), &g_Config.m_BcCornerIndicatorColorFrozen, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::BcCornerIndicatorColorFrozen, true)), false, nullptr, true);
			DoLine_ColorPicker(&s_CornerIndicatorLastTeammateColorButton, CornerIndicatorColorPickerLineSize, 13.0f, CornerIndicatorColorPickerSpacing, &CornerIndicatorView, BcLocalize("Last teammate color"), &g_Config.m_BcCornerIndicatorLastTeammateColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::BcCornerIndicatorLastTeammateColor, true)), false, nullptr, true);
		}

		Ui()->ClipDisable();
	}

	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	RenderSettingsBestClientChatMediaBlock(Column);

	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	const bool ChatBubblesExpanded = g_Config.m_BcChatBubbles != 0;
	const bool ChatBubblesShowCustomColors = ChatBubblesExpanded && g_Config.m_BcChatBubbleCustomColors != 0;
	const float ChatBubbleColorPickerLineSize = 25.0f;
	const float ChatBubbleColorPickerSpacing = 5.0f;
	const float ChatBubblesExpandedTargetHeight =
		9.0f * (MarginSmall + LineSize) +
		(ChatBubblesShowCustomColors ? 3.0f * (ChatBubbleColorPickerSpacing + ChatBubbleColorPickerLineSize) : 0.0f);
	const float ChatBubblesExpandedHeight = BC_MODULE_REVEAL(ChatBubblesExpanded, ChatBubblesExpandedTargetHeight, Client()->RenderFrameTime());
	const float ChatBubblesBlockHeight = LineSize + MarginSmall + LineSize + ChatBubblesExpandedHeight;

	CUIRect ChatBubblesBlock;
	Column.HSplitTop(ChatBubblesBlockHeight, &ChatBubblesBlock, &Column);

	CUIRect ChatBubblesBlockBg = ChatBubblesBlock;
	ChatBubblesBlockBg.w += BlockPadding;
	ChatBubblesBlockBg.h += BlockPadding;
	ChatBubblesBlockBg.x -= BlockPadding * 0.5f;
	ChatBubblesBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&ChatBubblesBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcChatBubbles, &ChatBubblesBlockBg);

	MainView = ChatBubblesBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	CUIRect ChatBubblesTitleLabel, ChatBubblesResetButton;
	Label.VSplitRight(LineSize + 8.0f, &ChatBubblesTitleLabel, &ChatBubblesResetButton);
	static CButtonContainer s_ChatBubblesResetButton;
	const bool ChatBubblesResetClicked = Ui()->DoButton_FontIcon(&s_ChatBubblesResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &ChatBubblesResetButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_ChatBubblesResetButton, &ChatBubblesResetButton, BcLocalize("Reset to defaults"));
	if(ChatBubblesResetClicked)
	{
		g_Config.m_BcChatBubblesSelf = DefaultConfig::BcChatBubblesSelf;
		g_Config.m_BcChatBubblesDemo = DefaultConfig::BcChatBubblesDemo;
		g_Config.m_BcChatBubbleSize = DefaultConfig::BcChatBubbleSize;
		g_Config.m_BcChatBubbleShowTime = DefaultConfig::BcChatBubbleShowTime;
		g_Config.m_BcChatBubbleFadeOut = DefaultConfig::BcChatBubbleFadeOut;
		g_Config.m_BcChatBubbleFadeIn = DefaultConfig::BcChatBubbleFadeIn;
		g_Config.m_BcChatBubbleAnimation = DefaultConfig::BcChatBubbleAnimation;
		g_Config.m_BcChatBubbleCustomColors = DefaultConfig::BcChatBubbleCustomColors;
		g_Config.m_BcChatBubbleBgColor = DefaultConfig::BcChatBubbleBgColor;
		g_Config.m_BcChatBubbleTextColor = DefaultConfig::BcChatBubbleTextColor;
		g_Config.m_BcChatBubbleOutlineColor = DefaultConfig::BcChatBubbleOutlineColor;
		g_Config.m_BcChatBubbleRounding = DefaultConfig::BcChatBubbleRounding;
	}
	ChatBubblesTitleLabel.VSplitRight(MarginSmall, &ChatBubblesTitleLabel, nullptr);
	BcMenuBadges::DrawEClient(Graphics(), Ui(), TextRender(), &ChatBubblesTitleLabel, MarginSmall);
	Ui()->DoLabel(&ChatBubblesTitleLabel, BcLocalize("Chat Bubbles"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatBubbles, BcLocalize("Show chat bubbles above players"), &g_Config.m_BcChatBubbles, &Content, LineSize);

	if(ChatBubblesExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = ChatBubblesExpandedHeight;
		Ui()->ClipEnable(&Visible);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatBubblesSelf, BcLocalize("Show chat bubbles above you"), &g_Config.m_BcChatBubblesSelf, &Content, LineSize);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatBubblesDemo, BcLocalize("Show chat bubbles in demo"), &g_Config.m_BcChatBubblesDemo, &Content, LineSize);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcChatBubbleSize, &g_Config.m_BcChatBubbleSize, &Button, BcLocalize("Chat bubble size"), 20, 30);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcChatBubbleShowTime, &g_Config.m_BcChatBubbleShowTime, &Button, BcLocalize("Show for"), 100, 1000);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcChatBubbleFadeIn, &g_Config.m_BcChatBubbleFadeIn, &Button, BcLocalize("Fade in"), 15, 100);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcChatBubbleFadeOut, &g_Config.m_BcChatBubbleFadeOut, &Button, BcLocalize("Fade out"), 15, 100);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatBubbleAnimation, BcLocalize("Stack animation"), &g_Config.m_BcChatBubbleAnimation, &Content, LineSize);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcChatBubbleRounding, &g_Config.m_BcChatBubbleRounding, &Button, BcLocalize("Corner rounding"), 0, 30);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatBubbleCustomColors, BcLocalize("Custom colors"), &g_Config.m_BcChatBubbleCustomColors, &Content, LineSize);

		if(ChatBubblesShowCustomColors)
		{
			static CButtonContainer s_ChatBubbleBgColorButton;
			DoLine_ColorPicker(&s_ChatBubbleBgColorButton, ChatBubbleColorPickerLineSize, 13.0f, ChatBubbleColorPickerSpacing, &MainView, BcLocalize("Background"), &g_Config.m_BcChatBubbleBgColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::BcChatBubbleBgColor, true)), false, nullptr, true);

			static CButtonContainer s_ChatBubbleTextColorButton;
			DoLine_ColorPicker(&s_ChatBubbleTextColorButton, ChatBubbleColorPickerLineSize, 13.0f, ChatBubbleColorPickerSpacing, &MainView, BcLocalize("Text"), &g_Config.m_BcChatBubbleTextColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::BcChatBubbleTextColor, true)), false, nullptr, true);

			static CButtonContainer s_ChatBubbleOutlineColorButton;
			DoLine_ColorPicker(&s_ChatBubbleOutlineColorButton, ChatBubbleColorPickerLineSize, 13.0f, ChatBubbleColorPickerSpacing, &MainView, BcLocalize("Outline"), &g_Config.m_BcChatBubbleOutlineColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::BcChatBubbleOutlineColor, true)), false, nullptr, true);
		}

		Ui()->ClipDisable();
	}

	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	const bool GradientNicknames = g_Config.m_BcNameplateGradient != 0;
	const bool GradientClans = g_Config.m_BcNameplateGradientClan != 0;
	const bool GradientSkin = g_Config.m_BcNameplateGradientSkin != 0;
	const bool GradientEverything = g_Config.m_BcNameplateGradientEverything != 0;
	const bool GradientExpanded = GradientNicknames || GradientClans || GradientSkin || GradientEverything;
	const bool GradientShowCustomColors = GradientExpanded && g_Config.m_BcNameplateGradientMode == BC_GRADIENT_MODE_CUSTOM;
	const float GradientColorPickerLineSize = 25.0f;
	const float GradientColorPickerSpacing = 5.0f;
	const int GradientCustomColorCount = std::clamp(g_Config.m_BcNameplateGradientColorCount, 2, 4);
	const float GradientHeaderHeight = LineSize + MarginSmall + 4.0f * LineSize;
	const float GradientTargetRadioHeight = 2.0f + LineSize;
	const float GradientExpandedTargetHeight = MarginSmall + LineSize + MarginSmall + LineSize + LineSize + MarginSmall + GradientTargetRadioHeight + (GradientShowCustomColors ? MarginSmall + LineSize + LineSize + MarginSmall + GradientCustomColorCount * (GradientColorPickerLineSize + GradientColorPickerSpacing) : 0.0f);
	const float GradientExpandedHeight = BC_MODULE_REVEAL(GradientExpanded, GradientExpandedTargetHeight, Client()->RenderFrameTime());
	const float GradientBlockHeight = GradientHeaderHeight + GradientExpandedHeight;

	CUIRect GradientBlock;
	Column.HSplitTop(GradientBlockHeight, &GradientBlock, &Column);

	CUIRect GradientBlockBg = GradientBlock;
	GradientBlockBg.w += BlockPadding;
	GradientBlockBg.h += BlockPadding;
	GradientBlockBg.x -= BlockPadding * 0.5f;
	GradientBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&GradientBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcNameplateGradient, &GradientBlockBg);

	MainView = GradientBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	CUIRect GradientTitleLabel, GradientResetButton;
	Label.VSplitRight(LineSize + 8.0f, &GradientTitleLabel, &GradientResetButton);
	static CButtonContainer s_GradientResetButton;
	const bool GradientResetClicked = Ui()->DoButton_FontIcon(&s_GradientResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &GradientResetButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_GradientResetButton, &GradientResetButton, BcLocalize("Reset to defaults"));
	if(GradientResetClicked)
	{
		g_Config.m_BcNameplateGradient = DefaultConfig::BcNameplateGradient;
		g_Config.m_BcNameplateGradientClan = DefaultConfig::BcNameplateGradientClan;
		g_Config.m_BcNameplateGradientMode = DefaultConfig::BcNameplateGradientMode;
		g_Config.m_BcNameplateGradientColorCount = DefaultConfig::BcNameplateGradientColorCount;
		g_Config.m_BcNameplateGradientColor1 = DefaultConfig::BcNameplateGradientColor1;
		g_Config.m_BcNameplateGradientColor2 = DefaultConfig::BcNameplateGradientColor2;
		g_Config.m_BcNameplateGradientColor3 = DefaultConfig::BcNameplateGradientColor3;
		g_Config.m_BcNameplateGradientColor4 = DefaultConfig::BcNameplateGradientColor4;
		g_Config.m_BcNameplateGradientSkin = DefaultConfig::BcNameplateGradientSkin;
		g_Config.m_BcNameplateGradientEverything = DefaultConfig::BcNameplateGradientEverything;
		g_Config.m_BcNameplateGradientAnimateSpeed = DefaultConfig::BcNameplateGradientAnimateSpeed;
		g_Config.m_BcNameplateGradientTarget = DefaultConfig::BcNameplateGradientTarget;
	}
	GradientTitleLabel.VSplitRight(MarginSmall, &GradientTitleLabel, nullptr);
	Ui()->DoLabel(&GradientTitleLabel, BcLocalize("Gradient"), HeadlineFontSize, TEXTALIGN_ML);

	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcNameplateGradient, BcLocalize("Gradient nicknames"), &g_Config.m_BcNameplateGradient, &Content, LineSize);
	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcNameplateGradientClan, BcLocalize("Gradient clans"), &g_Config.m_BcNameplateGradientClan, &Content, LineSize);
	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcNameplateGradientSkin, BcLocalize("Gradient skin"), &g_Config.m_BcNameplateGradientSkin, &Content, LineSize);
	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcNameplateGradientEverything, BcLocalize("Gradient everything"), &g_Config.m_BcNameplateGradientEverything, &Content, LineSize);

	if(GradientExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = GradientExpandedHeight;
		Ui()->ClipEnable(&Visible);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcNameplateGradientAnimateSpeed, &g_Config.m_BcNameplateGradientAnimateSpeed, &Button, BcLocalize("Animate speed"), 1, 100, &CUi::ms_LinearScrollbarScale, 0, "%");

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		CUIRect GradientModeLabel, GradientModeRow;
		MainView.HSplitTop(LineSize, &GradientModeLabel, &MainView);
		Ui()->DoLabel(&GradientModeLabel, BcLocalize("Mode"), 14.0f, TEXTALIGN_ML);

		MainView.HSplitTop(LineSize, &GradientModeRow, &MainView);
		{
			static CButtonContainer s_GradientModeSkin, s_GradientModeCustom, s_GradientModeRainbow;
			CUIRect SkinButton, CustomButton, RainbowButton;
			GradientModeRow.VSplitLeft(GradientModeRow.w / 3.0f, &SkinButton, &GradientModeRow);
			GradientModeRow.VSplitLeft(GradientModeRow.w / 2.0f, &CustomButton, &RainbowButton);
			g_Config.m_BcNameplateGradientMode = std::clamp(g_Config.m_BcNameplateGradientMode, 0, 2);
			if(DoButton_Menu(&s_GradientModeSkin, BcLocalize("Skin"), g_Config.m_BcNameplateGradientMode == BC_GRADIENT_MODE_SKIN, &SkinButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
				g_Config.m_BcNameplateGradientMode = BC_GRADIENT_MODE_SKIN;
			if(DoButton_Menu(&s_GradientModeCustom, BcLocalize("Custom"), g_Config.m_BcNameplateGradientMode == BC_GRADIENT_MODE_CUSTOM, &CustomButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
				g_Config.m_BcNameplateGradientMode = BC_GRADIENT_MODE_CUSTOM;
			if(DoButton_Menu(&s_GradientModeRainbow, BcLocalize("Rainbow"), g_Config.m_BcNameplateGradientMode == BC_GRADIENT_MODE_RAINBOW, &RainbowButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
				g_Config.m_BcNameplateGradientMode = BC_GRADIENT_MODE_RAINBOW;
		}

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		static std::vector<CButtonContainer> s_vGradientTargetButtons = {{}, {}, {}};
		DoLine_RadioMenu(MainView, Localize("Apply to", "Gradient"),
			s_vGradientTargetButtons,
			{Localize("Own", "Gradient"), Localize("Others", "Gradient"), Localize("All", "Gradient")},
			{BC_GRADIENT_TARGET_OWN, BC_GRADIENT_TARGET_OTHERS, BC_GRADIENT_TARGET_ALL},
			g_Config.m_BcNameplateGradientTarget);

		if(GradientShowCustomColors)
		{
			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			CUIRect GradientColorCountLabel, GradientColorCountRow;
			MainView.HSplitTop(LineSize, &GradientColorCountLabel, &MainView);
			Ui()->DoLabel(&GradientColorCountLabel, BcLocalize("Color count"), 14.0f, TEXTALIGN_ML);

			MainView.HSplitTop(LineSize, &GradientColorCountRow, &MainView);
			{
				static CButtonContainer s_GradientColorCount2, s_GradientColorCount3, s_GradientColorCount4;
				CUIRect Count2Button, Count3Button, Count4Button;
				GradientColorCountRow.VSplitLeft(GradientColorCountRow.w / 3.0f, &Count2Button, &GradientColorCountRow);
				GradientColorCountRow.VSplitLeft(GradientColorCountRow.w / 2.0f, &Count3Button, &Count4Button);
				g_Config.m_BcNameplateGradientColorCount = std::clamp(g_Config.m_BcNameplateGradientColorCount, 2, 4);
				if(DoButton_Menu(&s_GradientColorCount2, "2", g_Config.m_BcNameplateGradientColorCount == 2, &Count2Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
					g_Config.m_BcNameplateGradientColorCount = 2;
				if(DoButton_Menu(&s_GradientColorCount3, "3", g_Config.m_BcNameplateGradientColorCount == 3, &Count3Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
					g_Config.m_BcNameplateGradientColorCount = 3;
				if(DoButton_Menu(&s_GradientColorCount4, "4", g_Config.m_BcNameplateGradientColorCount == 4, &Count4Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
					g_Config.m_BcNameplateGradientColorCount = 4;
			}

			static CButtonContainer s_GradientColor1Button;
			static CButtonContainer s_GradientColor2Button;
			static CButtonContainer s_GradientColor3Button;
			static CButtonContainer s_GradientColor4Button;
			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			DoLine_ColorPicker(&s_GradientColor1Button, GradientColorPickerLineSize, 13.0f, GradientColorPickerSpacing, &MainView, BcLocalize("Color 1"), &g_Config.m_BcNameplateGradientColor1, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::BcNameplateGradientColor1, true)), false);
			DoLine_ColorPicker(&s_GradientColor2Button, GradientColorPickerLineSize, 13.0f, GradientColorPickerSpacing, &MainView, BcLocalize("Color 2"), &g_Config.m_BcNameplateGradientColor2, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::BcNameplateGradientColor2, true)), false);
			if(GradientCustomColorCount >= 3)
				DoLine_ColorPicker(&s_GradientColor3Button, GradientColorPickerLineSize, 13.0f, GradientColorPickerSpacing, &MainView, BcLocalize("Color 3"), &g_Config.m_BcNameplateGradientColor3, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::BcNameplateGradientColor3, true)), false);
			if(GradientCustomColorCount >= 4)
				DoLine_ColorPicker(&s_GradientColor4Button, GradientColorPickerLineSize, 13.0f, GradientColorPickerSpacing, &MainView, BcLocalize("Color 4"), &g_Config.m_BcNameplateGradientColor4, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::BcNameplateGradientColor4, true)), false);
		}

		Ui()->ClipDisable();
	}

	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	const bool FlyingNamePlatesExpanded = g_Config.m_BcFlyingNamePlates != 0;
	const float FlyingNamePlatesHeaderHeight = LineSize + MarginSmall + LineSize;
	const float FlyingNamePlatesExpandedTargetHeight = MarginSmall + LineSize + MarginSmall + LineSize + MarginSmall + LineSize + MarginSmall + LineSize;
	const float FlyingNamePlatesExpandedHeight = BC_MODULE_REVEAL(FlyingNamePlatesExpanded, FlyingNamePlatesExpandedTargetHeight, Client()->RenderFrameTime());
	const float FlyingNamePlatesBlockHeight = FlyingNamePlatesHeaderHeight + FlyingNamePlatesExpandedHeight;

	CUIRect FlyingNamePlatesBlock;
	Column.HSplitTop(FlyingNamePlatesBlockHeight, &FlyingNamePlatesBlock, &Column);

	CUIRect FlyingNamePlatesBlockBg = FlyingNamePlatesBlock;
	FlyingNamePlatesBlockBg.w += BlockPadding;
	FlyingNamePlatesBlockBg.h += BlockPadding;
	FlyingNamePlatesBlockBg.x -= BlockPadding * 0.5f;
	FlyingNamePlatesBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&FlyingNamePlatesBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcFlyingNamePlates, &FlyingNamePlatesBlockBg);

	MainView = FlyingNamePlatesBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	CUIRect FlyingNamePlatesTitleLabel, FlyingNamePlatesResetButton;
	Label.VSplitRight(LineSize + 8.0f, &FlyingNamePlatesTitleLabel, &FlyingNamePlatesResetButton);
	static CButtonContainer s_FlyingNamePlatesResetButton;
	const bool FlyingNamePlatesResetClicked = Ui()->DoButton_FontIcon(&s_FlyingNamePlatesResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &FlyingNamePlatesResetButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_FlyingNamePlatesResetButton, &FlyingNamePlatesResetButton, BcLocalize("Reset to defaults"));
	if(FlyingNamePlatesResetClicked)
	{
		g_Config.m_BcFlyingNamePlatesHideLine = DefaultConfig::BcFlyingNamePlatesHideLine;
		g_Config.m_BcFlyingNamePlatesLift = DefaultConfig::BcFlyingNamePlatesLift;
		g_Config.m_BcFlyingNamePlatesDrag = DefaultConfig::BcFlyingNamePlatesDrag;
		g_Config.m_BcFlyingNamePlatesFollow = DefaultConfig::BcFlyingNamePlatesFollow;
	}
	FlyingNamePlatesTitleLabel.VSplitRight(MarginSmall, &FlyingNamePlatesTitleLabel, nullptr);
	Ui()->DoLabel(&FlyingNamePlatesTitleLabel, BcLocalize("Flying Name Plates"), HeadlineFontSize, TEXTALIGN_ML);

	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcFlyingNamePlates, BcLocalize("Enable flying name plates"), &g_Config.m_BcFlyingNamePlates, &Content, LineSize);

	if(FlyingNamePlatesExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = FlyingNamePlatesExpandedHeight;
		Ui()->ClipEnable(&Visible);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcFlyingNamePlatesHideLine, BcLocalize("Hide line"), &g_Config.m_BcFlyingNamePlatesHideLine, &Content, LineSize);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcFlyingNamePlatesLift, &g_Config.m_BcFlyingNamePlatesLift, &Button, BcLocalize("Lift above player"), 0, 120);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcFlyingNamePlatesDrag, &g_Config.m_BcFlyingNamePlatesDrag, &Button, BcLocalize("Movement drag"), 0, 200);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcFlyingNamePlatesFollow, &g_Config.m_BcFlyingNamePlatesFollow, &Button, BcLocalize("Follow speed"), 1, 100);

		Ui()->ClipDisable();
	}

	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	const float Particles3DColorPickerLineSize = 25.0f;
	const float Particles3DColorPickerLabelSize = 13.0f;
	const float Particles3DColorPickerSpacing = 5.0f;
	const bool Particles3DEnabled = g_Config.m_Bc3dParticles != 0;
	const bool Particles3DShowCustomColor = Particles3DEnabled && g_Config.m_Bc3dParticlesColorMode == 1;
	const float Particles3DExpandedTargetHeight =
		(MarginSmall + LineSize) * 5.0f + (Particles3DShowCustomColor ? MarginSmall + Particles3DColorPickerLineSize + Particles3DColorPickerSpacing : 0.0f);
	const float Particles3DExpandedHeight = BC_MODULE_REVEAL(Particles3DEnabled, Particles3DExpandedTargetHeight, Client()->RenderFrameTime());
	const float Particles3DContentHeight = LineSize + MarginSmall + LineSize + Particles3DExpandedHeight;

	CUIRect Particles3DBlock;
	Column.HSplitTop(Particles3DContentHeight, &Particles3DBlock, &Column);

	CUIRect Particles3DBlockBg = Particles3DBlock;
	Particles3DBlockBg.w += BlockPadding;
	Particles3DBlockBg.h += BlockPadding;
	Particles3DBlockBg.x -= BlockPadding * 0.5f;
	Particles3DBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&Particles3DBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_Bc3dParticles, &Particles3DBlockBg);

	MainView = Particles3DBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	CUIRect Particles3DTitleLabel, Particles3DResetButton;
	Label.VSplitRight(LineSize + 8.0f, &Particles3DTitleLabel, &Particles3DResetButton);
	static CButtonContainer s_3DParticlesResetButton;
	if(Ui()->DoButton_FontIcon(&s_3DParticlesResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &Particles3DResetButton, BUTTONFLAG_LEFT))
	{
		g_Config.m_Bc3dParticlesType = DefaultConfig::Bc3dParticlesType;
		g_Config.m_Bc3dParticlesDensity = DefaultConfig::Bc3dParticlesDensity;
		g_Config.m_Bc3dParticlesSizeMax = DefaultConfig::Bc3dParticlesSizeMax;
		g_Config.m_Bc3dParticlesSpeed = DefaultConfig::Bc3dParticlesSpeed;
		g_Config.m_Bc3dParticlesDepth = DefaultConfig::Bc3dParticlesDepth;
		g_Config.m_Bc3dParticlesAlpha = DefaultConfig::Bc3dParticlesAlpha;
		g_Config.m_Bc3dParticlesPushRadius = DefaultConfig::Bc3dParticlesPushRadius;
		g_Config.m_Bc3dParticlesPushStrength = DefaultConfig::Bc3dParticlesPushStrength;
		g_Config.m_Bc3dParticlesCollide = DefaultConfig::Bc3dParticlesCollide;
		g_Config.m_Bc3dParticlesViewMargin = DefaultConfig::Bc3dParticlesViewMargin;
		g_Config.m_Bc3dParticlesColorMode = DefaultConfig::Bc3dParticlesColorMode;
		g_Config.m_Bc3dParticlesColor = DefaultConfig::Bc3dParticlesColor;
		g_Config.m_Bc3dParticlesGlow = DefaultConfig::Bc3dParticlesGlow;
	}
	GameClient()->m_Tooltips.DoToolTip(&s_3DParticlesResetButton, &Particles3DResetButton, BcLocalize("Reset to defaults"));
	Ui()->DoLabel(&Particles3DTitleLabel, BcLocalize("3D Particles"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_Bc3dParticles, BcLocalize("Enable 3D Particles"), &g_Config.m_Bc3dParticles, &Content, LineSize);

	if(Particles3DExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = Particles3DExpandedHeight;
		Ui()->ClipEnable(&Visible);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesDensity, &g_Config.m_Bc3dParticlesDensity, &Button, BcLocalize("Density"), 1, 600);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		CUIRect Particles3DTypeLabel, Particles3DTypeSelect;
		Button.VSplitLeft(150.0f, &Particles3DTypeLabel, &Particles3DTypeSelect);
		Ui()->DoLabel(&Particles3DTypeLabel, BcLocalize("Particle type"), 14.0f, TEXTALIGN_ML);

		static CUi::SDropDownState s_3DParticlesTypeState;
		static CScrollRegion s_3DParticlesTypeScrollRegion;
		s_3DParticlesTypeState.m_SelectionPopupContext.m_pScrollRegion = &s_3DParticlesTypeScrollRegion;
		const char *Ap3DParticleTypes[3] = {
			BcLocalize("Cube"),
			BcLocalize("Heart"),
			BcLocalize("Mixed"),
		};
		g_Config.m_Bc3dParticlesType = Ui()->DoDropDown(&Particles3DTypeSelect, g_Config.m_Bc3dParticlesType - 1, Ap3DParticleTypes, (int)std::size(Ap3DParticleTypes), s_3DParticlesTypeState) + 1;

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesSizeMax, &g_Config.m_Bc3dParticlesSizeMax, &Button, BcLocalize("Size"), 2, 200);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		CUIRect Particles3DColorModeLabel, Particles3DColorModeSelect;
		Button.VSplitLeft(150.0f, &Particles3DColorModeLabel, &Particles3DColorModeSelect);
		Ui()->DoLabel(&Particles3DColorModeLabel, BcLocalize("Color mode"), 14.0f, TEXTALIGN_ML);

		static CUi::SDropDownState s_3DParticlesColorModeState;
		static CScrollRegion s_3DParticlesColorModeScrollRegion;
		s_3DParticlesColorModeState.m_SelectionPopupContext.m_pScrollRegion = &s_3DParticlesColorModeScrollRegion;
		const char *Ap3DParticleColorModes[2] = {
			BcLocalize("Custom"),
			BcLocalize("Random"),
		};
		g_Config.m_Bc3dParticlesColorMode = Ui()->DoDropDown(&Particles3DColorModeSelect, g_Config.m_Bc3dParticlesColorMode - 1, Ap3DParticleColorModes, (int)std::size(Ap3DParticleColorModes), s_3DParticlesColorModeState) + 1;

		if(Particles3DShowCustomColor)
		{
			static CButtonContainer s_3DParticlesColorButton;
			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			DoLine_ColorPicker(&s_3DParticlesColorButton, Particles3DColorPickerLineSize, Particles3DColorPickerLabelSize, Particles3DColorPickerSpacing, &MainView, BcLocalize("Color"), &g_Config.m_Bc3dParticlesColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
		}

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_Bc3dParticlesGlow, BcLocalize("Glow"), &g_Config.m_Bc3dParticlesGlow, &Content, LineSize);

		Ui()->ClipDisable();
	}

	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	const float MediaBackgroundBlockHeight = LineSize + MarginSmall + LineSize + MarginSmall + LineSize + MarginSmall + LineSize + MarginSmall + LineSize + MarginSmall + LineSize;

	CUIRect MediaBackgroundBlock;
	Column.HSplitTop(MediaBackgroundBlockHeight, &MediaBackgroundBlock, &Column);

	CUIRect MediaBackgroundBlockBg = MediaBackgroundBlock;
	MediaBackgroundBlockBg.w += BlockPadding;
	MediaBackgroundBlockBg.h += BlockPadding;
	MediaBackgroundBlockBg.x -= BlockPadding * 0.5f;
	MediaBackgroundBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&MediaBackgroundBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcMenuMediaBackground, &MediaBackgroundBlockBg);

	MainView = MediaBackgroundBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	Ui()->DoLabel(&Label, BcLocalize("Media Background"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Content, &MainView);
	const bool MenuMediaChanged = DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMenuMediaBackground, BcLocalize("Enable to main menu"), &g_Config.m_BcMenuMediaBackground, &Content, LineSize);

	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	MainView.HSplitTop(LineSize, &Content, &MainView);
	const bool GameMediaChanged = DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcGameMediaBackground, BcLocalize("Enable to game background"), &g_Config.m_BcGameMediaBackground, &Content, LineSize);

	auto ReloadMenuMediaFromConfig = [&]() {
		CMenuMediaBackground &MediaBackground = GameClient()->m_BestClient.MenuMediaBackground();
		const bool MediaStatusInGame = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
		if(MediaStatusInGame)
			MediaBackground.ReloadFromConfig(g_Config.m_BcGameMediaBackground, g_Config.m_BcMenuMediaBackgroundPath);
		else
			MediaBackground.ReloadFromConfig(g_Config.m_BcMenuMediaBackground, g_Config.m_BcMenuMediaBackgroundPath);
	};

	if(MenuMediaChanged || GameMediaChanged)
		ReloadMenuMediaFromConfig();

	struct SMenuMediaFileListContext
	{
		std::vector<std::string> *m_pLabels;
		std::vector<std::string> *m_pPaths;
	};

	auto MenuMediaFileListScan = [](const char *pName, int IsDir, int StorageType, void *pUser) {
		(void)StorageType;
		if(IsDir)
			return 0;

		auto *pContext = static_cast<SMenuMediaFileListContext *>(pUser);
		const std::string Ext = MediaDecoder::ExtractExtensionLower(pName);
		const bool SupportedImage = Ext == "png" || Ext == "jpg" || Ext == "jpeg" || Ext == "webp" || Ext == "bmp" || Ext == "avif" || Ext == "gif";
		const bool SupportedVideo = Ext == "mp4" || Ext == "webm" || Ext == "mov" || Ext == "m4v" || Ext == "mkv" || Ext == "avi";
		if(!SupportedImage && !SupportedVideo)
			return 0;

		pContext->m_pLabels->emplace_back(pName);
		pContext->m_pPaths->emplace_back(std::string("BestClient/backgrounds/") + pName);
		return 0;
	};

	Storage()->CreateFolder("BestClient", IStorage::TYPE_SAVE);
	Storage()->CreateFolder("BestClient/backgrounds", IStorage::TYPE_SAVE);

	static std::vector<std::string> s_vMenuMediaFileLabels;
	static std::vector<std::string> s_vMenuMediaFilePaths;
	s_vMenuMediaFileLabels.clear();
	s_vMenuMediaFilePaths.clear();
	SMenuMediaFileListContext MenuMediaContext{&s_vMenuMediaFileLabels, &s_vMenuMediaFilePaths};
	Storage()->ListDirectory(IStorage::TYPE_SAVE, "BestClient/backgrounds", MenuMediaFileListScan, &MenuMediaContext);

	std::vector<int> vSortedIndices(s_vMenuMediaFileLabels.size());
	for(size_t i = 0; i < vSortedIndices.size(); ++i)
		vSortedIndices[i] = (int)i;
	std::sort(vSortedIndices.begin(), vSortedIndices.end(), [&](int Left, int Right) {
		return str_comp_nocase(s_vMenuMediaFileLabels[Left].c_str(), s_vMenuMediaFileLabels[Right].c_str()) < 0;
	});

	static std::vector<std::string> s_vMenuMediaDropDownLabels;
	static std::vector<const char *> s_vMenuMediaDropDownLabelPtrs;
	s_vMenuMediaDropDownLabels.clear();
	s_vMenuMediaDropDownLabelPtrs.clear();
	for(int SortedIndex : vSortedIndices)
		s_vMenuMediaDropDownLabels.push_back(s_vMenuMediaFileLabels[SortedIndex]);
	for(const std::string &LabelString : s_vMenuMediaDropDownLabels)
		s_vMenuMediaDropDownLabelPtrs.push_back(LabelString.c_str());

	int SelectedMediaFile = -1;
	for(size_t i = 0; i < vSortedIndices.size(); ++i)
	{
		const int SortedIndex = vSortedIndices[i];
		if(str_comp(g_Config.m_BcMenuMediaBackgroundPath, s_vMenuMediaFilePaths[SortedIndex].c_str()) == 0)
		{
			SelectedMediaFile = (int)i;
			break;
		}
	}

	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	CUIRect MediaPathRow, MediaFileDropDown, MediaReloadButton, MediaFolderButton;
	MainView.HSplitTop(LineSize, &MediaPathRow, &MainView);
	MediaPathRow.VSplitRight(20.0f, &MediaPathRow, &MediaFolderButton);
	MediaPathRow.VSplitRight(MarginSmall, &MediaPathRow, nullptr);
	MediaPathRow.VSplitRight(20.0f, &MediaPathRow, &MediaReloadButton);
	MediaPathRow.VSplitRight(MarginSmall, &MediaPathRow, nullptr);
	MediaFileDropDown = MediaPathRow;

	if(s_vMenuMediaDropDownLabelPtrs.empty())
	{
		static CButtonContainer s_MenuMediaEmptyButton;
		DoButton_Menu(&s_MenuMediaEmptyButton, BcLocalize("No media files in backgrounds folder"), -1, &MediaFileDropDown);
	}
	else
	{
		static CUi::SDropDownState s_MenuMediaFileDropDownState;
		static CScrollRegion s_MenuMediaFileDropDownScrollRegion;
		s_MenuMediaFileDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_MenuMediaFileDropDownScrollRegion;
		const int NewSelectedMediaFile = Ui()->DoDropDown(&MediaFileDropDown, SelectedMediaFile, s_vMenuMediaDropDownLabelPtrs.data(), s_vMenuMediaDropDownLabelPtrs.size(), s_MenuMediaFileDropDownState);
		if(NewSelectedMediaFile != SelectedMediaFile && NewSelectedMediaFile >= 0 && NewSelectedMediaFile < (int)vSortedIndices.size())
		{
			const int SortedIndex = vSortedIndices[NewSelectedMediaFile];
			str_copy(g_Config.m_BcMenuMediaBackgroundPath, s_vMenuMediaFilePaths[SortedIndex].c_str(), sizeof(g_Config.m_BcMenuMediaBackgroundPath));
			ReloadMenuMediaFromConfig();
		}
	}

	static CButtonContainer s_MenuMediaReloadButton;
	if(Ui()->DoButton_FontIcon(&s_MenuMediaReloadButton, FontIcon::ARROW_ROTATE_RIGHT, 0, &MediaReloadButton, BUTTONFLAG_LEFT))
		ReloadMenuMediaFromConfig();

	static CButtonContainer s_MenuMediaFolderButton;
	if(Ui()->DoButton_FontIcon(&s_MenuMediaFolderButton, FontIcon::FOLDER, 0, &MediaFolderButton, BUTTONFLAG_LEFT))
	{
		Storage()->CreateFolder("BestClient", IStorage::TYPE_SAVE);
		Storage()->CreateFolder("BestClient/backgrounds", IStorage::TYPE_SAVE);
		char aBuf[IO_MAX_PATH_LENGTH];
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, "BestClient/backgrounds", aBuf, sizeof(aBuf));
		Client()->ViewFile(aBuf);
	}

	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	MainView.HSplitTop(LineSize, &Button, &MainView);
	Ui()->DoScrollbarOption(&g_Config.m_BcGameMediaBackgroundOffset, &g_Config.m_BcGameMediaBackgroundOffset, &Button, BcLocalize("Map offset"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");
	GameClient()->m_Tooltips.DoToolTip(&g_Config.m_BcGameMediaBackgroundOffset, &Button, BcLocalize("0 keeps the image fixed to the screen. Higher values add camera parallax. Move the camera to see the effect."));

	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	MainView.HSplitTop(LineSize, &Button, &MainView);
	const bool MediaStatusInGame = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
	const CMenuMediaBackground &MediaStatusSource = GameClient()->m_BestClient.MenuMediaBackground();
	const char *pMediaStatusText = MediaStatusSource.StatusText();
	char aMediaStatusBuf[256];
	if(MediaStatusInGame && MediaStatusSource.IsLoaded() && g_Config.m_BcGameMediaBackground && g_Config.m_ClOverlayEntities == 0)
	{
		str_format(aMediaStatusBuf, sizeof(aMediaStatusBuf), "%s %s", pMediaStatusText, BcLocalize("Hidden: overlay entities is 0."));
		pMediaStatusText = aMediaStatusBuf;
		TextRender()->TextColor(ColorRGBA(1.0f, 0.85f, 0.45f, 1.0f));
	}
	else if(MediaStatusSource.HasError())
		TextRender()->TextColor(ColorRGBA(1.0f, 0.45f, 0.45f, 1.0f));
	else if(MediaStatusSource.IsLoaded())
		TextRender()->TextColor(ColorRGBA(0.55f, 1.0f, 0.55f, 1.0f));
	Ui()->DoLabel(&Button, pMediaStatusText, 11.0f, TEXTALIGN_ML);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	const bool CursorTrailExpanded = g_Config.m_BcCursorTrail != 0;
	const bool CursorTrailCustom = CursorTrailExpanded && g_Config.m_BcCursorTrailMode == 1;
	const float CursorTrailTargetHeight = 6.0f * (MarginSmall + LineSize) + (CursorTrailCustom ? MarginSmall + LineSize : 0.0f);
	const float CursorTrailExpandedHeight = BC_MODULE_REVEAL(CursorTrailExpanded, CursorTrailTargetHeight, Client()->RenderFrameTime());
	const float CursorTrailBlockHeight = 2.0f * LineSize + MarginSmall + CursorTrailExpandedHeight;
	CUIRect CursorTrailBlock;
	Column.HSplitTop(CursorTrailBlockHeight, &CursorTrailBlock, &Column);
	CUIRect CursorTrailBlockBg = CursorTrailBlock;
	CursorTrailBlockBg.w += BlockPadding;
	CursorTrailBlockBg.h += BlockPadding;
	CursorTrailBlockBg.x -= BlockPadding * 0.5f;
	CursorTrailBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&CursorTrailBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcCursorTrail, &CursorTrailBlockBg);
	MainView = CursorTrailBlock;
	MainView.HSplitTop(LineSize, &Label, &MainView);
	CUIRect CursorTrailTitleLabel, CursorTrailResetButton;
	Label.VSplitRight(LineSize + 8.0f, &CursorTrailTitleLabel, &CursorTrailResetButton);
	static CButtonContainer s_CursorTrailResetButton;
	if(Ui()->DoButton_FontIcon(&s_CursorTrailResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &CursorTrailResetButton, BUTTONFLAG_LEFT))
	{
		g_Config.m_BcCursorTrailMode = DefaultConfig::BcCursorTrailMode;
		g_Config.m_BcCursorTrailTrailImage[0] = '\0';
		g_Config.m_BcCursorTrailTrailSize = DefaultConfig::BcCursorTrailTrailSize;
		g_Config.m_BcCursorTrailNumberOfFrames = DefaultConfig::BcCursorTrailNumberOfFrames;
		g_Config.m_BcCursorTrailOpacity = DefaultConfig::BcCursorTrailOpacity;
		g_Config.m_BcCursorTrailSamplingFps = DefaultConfig::BcCursorTrailSamplingFps;
		g_Config.m_BcCursorTrailDisableMovement = DefaultConfig::BcCursorTrailDisableMovement;
		GameClient()->m_CursorTrail.Reload();
	}
	CUIRect CursorTrailAuthorBadge;
	static CButtonContainer s_CursorTrailAuthorBadge;
	CursorTrailTitleLabel.VSplitRight(MarginSmall, &CursorTrailTitleLabel, nullptr);
	BcMenuBadges::DrawAuthor(Graphics(), Ui(), TextRender(), &CursorTrailTitleLabel, 4.0f, &CursorTrailAuthorBadge);
	Ui()->DoButtonLogic(&s_CursorTrailAuthorBadge, 0, &CursorTrailAuthorBadge, BUTTONFLAG_NONE);
	GameClient()->m_Tooltips.DoToolTip(&s_CursorTrailAuthorBadge, &CursorTrailAuthorBadge, "sneznyyy");
	GameClient()->m_Tooltips.SetFadeTime(&s_CursorTrailAuthorBadge, 0.0f);
	BcMenuBadges::DrawNew(Graphics(), Ui(), TextRender(), &CursorTrailTitleLabel, 2.0f);
	Ui()->DoLabel(&CursorTrailTitleLabel, BcLocalize("Cursor Trail"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcCursorTrail, BcLocalize("Enable"), &g_Config.m_BcCursorTrail, &Content, LineSize);

	if(CursorTrailExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = CursorTrailExpandedHeight;
		Ui()->ClipEnable(&Visible);
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		static CUi::SDropDownState s_CursorTrailModeState;
		static CScrollRegion s_CursorTrailModeScrollRegion;
		s_CursorTrailModeState.m_SelectionPopupContext.m_pScrollRegion = &s_CursorTrailModeScrollRegion;
		const char *apCursorTrailModes[] = {BcLocalize("Cursor"), BcLocalize("Custom")};
		CUIRect ModeLabel, ModeRow;
		Content.VSplitLeft(110.0f, &ModeLabel, &ModeRow);
		Ui()->DoLabel(&ModeLabel, BcLocalize("Mode"), 12.0f, TEXTALIGN_ML);
		g_Config.m_BcCursorTrailMode = Ui()->DoDropDown(&ModeRow, g_Config.m_BcCursorTrailMode, apCursorTrailModes, std::size(apCursorTrailModes), s_CursorTrailModeState);
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcCursorTrailTrailSize, &g_Config.m_BcCursorTrailTrailSize, &Button, BcLocalize("Trail size"), 10, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcCursorTrailNumberOfFrames, &g_Config.m_BcCursorTrailNumberOfFrames, &Button, BcLocalize("Number of frames"), 1, 10);
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcCursorTrailOpacity, &g_Config.m_BcCursorTrailOpacity, &Button, BcLocalize("Opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcCursorTrailSamplingFps, &g_Config.m_BcCursorTrailSamplingFps, &Button, BcLocalize("Sampling FPS"), 24, 120);
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcCursorTrailDisableMovement, BcLocalize("Disable movement"), &g_Config.m_BcCursorTrailDisableMovement, &Content, LineSize);
		if(CursorTrailCustom)
		{
			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			struct SCursorTrailFileListContext
			{
				std::vector<std::string> *m_pLabels;
				std::vector<std::string> *m_pPaths;
			};
			auto CursorTrailFileListScan = [](const char *pName, int IsDir, int StorageType, void *pUser) {
				(void)StorageType;
				if(IsDir)
					return 0;
				const std::string Ext = MediaDecoder::ExtractExtensionLower(pName);
				if(Ext != "png" && Ext != "jpg" && Ext != "jpeg" && Ext != "webp" && Ext != "bmp" && Ext != "avif")
					return 0;
				auto *pContext = static_cast<SCursorTrailFileListContext *>(pUser);
				pContext->m_pLabels->emplace_back(pName);
				pContext->m_pPaths->emplace_back(std::string("BestClient/CTrails/") + pName);
				return 0;
			};
			Storage()->CreateFolder("BestClient", IStorage::TYPE_SAVE);
			Storage()->CreateFolder("BestClient/CTrails", IStorage::TYPE_SAVE);
			static std::vector<std::string> s_vCursorTrailFileLabels, s_vCursorTrailFilePaths;
			s_vCursorTrailFileLabels.clear();
			s_vCursorTrailFilePaths.clear();
			SCursorTrailFileListContext CursorTrailFileContext{&s_vCursorTrailFileLabels, &s_vCursorTrailFilePaths};
			Storage()->ListDirectory(IStorage::TYPE_SAVE, "BestClient/CTrails", CursorTrailFileListScan, &CursorTrailFileContext);
			std::vector<int> vSortedCursorTrailIndices(s_vCursorTrailFileLabels.size());
			for(size_t i = 0; i < vSortedCursorTrailIndices.size(); ++i)
				vSortedCursorTrailIndices[i] = (int)i;
			std::sort(vSortedCursorTrailIndices.begin(), vSortedCursorTrailIndices.end(), [&](int Left, int Right) {
				return str_comp_nocase(s_vCursorTrailFileLabels[Left].c_str(), s_vCursorTrailFileLabels[Right].c_str()) < 0;
			});
			static std::vector<std::string> s_vCursorTrailDropDownLabels;
			static std::vector<const char *> s_vCursorTrailDropDownLabelPtrs;
			s_vCursorTrailDropDownLabels.clear();
			s_vCursorTrailDropDownLabelPtrs.clear();
			for(int Index : vSortedCursorTrailIndices)
				s_vCursorTrailDropDownLabels.push_back(s_vCursorTrailFileLabels[Index]);
			for(const std::string &LabelString : s_vCursorTrailDropDownLabels)
				s_vCursorTrailDropDownLabelPtrs.push_back(LabelString.c_str());
			int SelectedCursorTrailFile = -1;
			for(size_t i = 0; i < vSortedCursorTrailIndices.size(); ++i)
				if(str_comp(g_Config.m_BcCursorTrailTrailImage, s_vCursorTrailFilePaths[vSortedCursorTrailIndices[i]].c_str()) == 0)
					SelectedCursorTrailFile = (int)i;
			CUIRect CursorTrailPathRow, CursorTrailFolderButton, CursorTrailReloadButton;
			MainView.HSplitTop(LineSize, &CursorTrailPathRow, &MainView);
			CursorTrailPathRow.VSplitRight(20.0f, &CursorTrailPathRow, &CursorTrailFolderButton);
			CursorTrailPathRow.VSplitRight(MarginSmall, &CursorTrailPathRow, nullptr);
			CursorTrailPathRow.VSplitRight(20.0f, &CursorTrailPathRow, &CursorTrailReloadButton);
			CursorTrailPathRow.VSplitRight(MarginSmall, &CursorTrailPathRow, nullptr);
			if(s_vCursorTrailDropDownLabelPtrs.empty())
			{
				static CButtonContainer s_CursorTrailEmptyButton;
				CUIRect CursorTrailPathLabel, CursorTrailEmptyRow;
				CursorTrailPathRow.VSplitLeft(110.0f, &CursorTrailPathLabel, &CursorTrailEmptyRow);
				Ui()->DoLabel(&CursorTrailPathLabel, BcLocalize("Trail image"), 12.0f, TEXTALIGN_ML);
				DoButton_Menu(&s_CursorTrailEmptyButton, BcLocalize("No images in CTrails folder"), -1, &CursorTrailEmptyRow);
			}
			else
			{
				static CUi::SDropDownState s_CursorTrailFileDropDownState;
				static CScrollRegion s_CursorTrailFileDropDownScrollRegion;
				s_CursorTrailFileDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_CursorTrailFileDropDownScrollRegion;
				CUIRect CursorTrailPathLabel, CursorTrailDropDown;
				CursorTrailPathRow.VSplitLeft(110.0f, &CursorTrailPathLabel, &CursorTrailDropDown);
				Ui()->DoLabel(&CursorTrailPathLabel, BcLocalize("Trail image"), 12.0f, TEXTALIGN_ML);
				const int NewSelectedCursorTrailFile = Ui()->DoDropDown(&CursorTrailDropDown, SelectedCursorTrailFile, s_vCursorTrailDropDownLabelPtrs.data(), s_vCursorTrailDropDownLabelPtrs.size(), s_CursorTrailFileDropDownState);
				if(NewSelectedCursorTrailFile != SelectedCursorTrailFile && NewSelectedCursorTrailFile >= 0 && NewSelectedCursorTrailFile < (int)vSortedCursorTrailIndices.size())
				{
					const int SortedIndex = vSortedCursorTrailIndices[NewSelectedCursorTrailFile];
					str_copy(g_Config.m_BcCursorTrailTrailImage, s_vCursorTrailFilePaths[SortedIndex].c_str(), sizeof(g_Config.m_BcCursorTrailTrailImage));
					GameClient()->m_CursorTrail.Reload();
				}
			}
			static CButtonContainer s_CursorTrailFolderButton, s_CursorTrailReloadButton;
			if(Ui()->DoButton_FontIcon(&s_CursorTrailReloadButton, FontIcon::ARROW_ROTATE_RIGHT, 0, &CursorTrailReloadButton, BUTTONFLAG_LEFT))
				GameClient()->m_CursorTrail.Reload();
			if(Ui()->DoButton_FontIcon(&s_CursorTrailFolderButton, FontIcon::FOLDER, 0, &CursorTrailFolderButton, BUTTONFLAG_LEFT))
			{
				Storage()->CreateFolder("BestClient", IStorage::TYPE_SAVE);
				Storage()->CreateFolder("BestClient/CTrails", IStorage::TYPE_SAVE);
				char aBuf[IO_MAX_PATH_LENGTH];
				Storage()->GetCompletePath(IStorage::TYPE_SAVE, "BestClient/CTrails", aBuf, sizeof(aBuf));
				Client()->ViewFile(aBuf);
			}
		}
		Ui()->ClipDisable();
	}

	const float LeftColumnEndY = Column.y;

	CUIRect RightColumn = RightView;
	RightColumn.HSplitTop(10.0f, nullptr, &RightColumn);

	const bool NewUiExpanded = g_Config.m_BcMenuUiStyle != 0;
	const bool TabOptionExpanded = NewUiExpanded && g_Config.m_BcMenuUiNewTabOption != 0;
	const float ColorPickerLineSize = 25.0f;
	const float ColorPickerSpacing = 5.0f;
	const float NewUiHeaderHeight = LineSize + MarginSmall + LineSize;
	const float NewUiExpandedTargetHeight = 5.0f * (MarginSmall + LineSize) + (TabOptionExpanded ? (MarginSmall + LineSize) : 0.0f);
	const float NewUiExpandedHeight = BC_MODULE_REVEAL(NewUiExpanded, NewUiExpandedTargetHeight, Client()->RenderFrameTime());
	const float NewUiBlockHeight = NewUiHeaderHeight + NewUiExpandedHeight;
	CUIRect NewUiBlock;
	RightColumn.HSplitTop(NewUiBlockHeight, &NewUiBlock, &RightColumn);

	CUIRect NewUiBlockBg = NewUiBlock;
	NewUiBlockBg.w += BlockPadding;
	NewUiBlockBg.h += BlockPadding;
	NewUiBlockBg.x -= BlockPadding * 0.5f;
	NewUiBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&NewUiBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcMenuUiStyle, &NewUiBlockBg);

	MainView = NewUiBlock;
	MainView.HSplitTop(LineSize, &Label, &MainView);
	CUIRect NewUiTitleLabel, NewUiResetButton;
	Label.VSplitRight(LineSize + 8.0f, &NewUiTitleLabel, &NewUiResetButton);
	static CButtonContainer s_NewUiResetButton;
	const bool NewUiResetClicked = Ui()->DoButton_FontIcon(&s_NewUiResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &NewUiResetButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_NewUiResetButton, &NewUiResetButton, BcLocalize("Reset to defaults"));
	if(NewUiResetClicked)
	{
		g_Config.m_BcMenuUiStyle = 0;
		g_Config.m_BcMenuUiNewCheckbox = 1;
		g_Config.m_BcMenuUiNewScrollbar = 1;
		g_Config.m_BcMenuUiNewTabOption = 1;
		g_Config.m_BcMenuUiTabSide = 1;
		g_Config.m_BcMenuUiNewColorPicker = 1;
		g_Config.m_BcMenuUiBetterFont = 0;
		BestClientRefreshMenuFont(GameClient());
	}
	NewUiTitleLabel.VSplitRight(MarginSmall, &NewUiTitleLabel, nullptr);
	BcMenuBadges::DrawNew(Graphics(), Ui(), TextRender(), &NewUiTitleLabel, 4.0f);
	Ui()->DoLabel(&NewUiTitleLabel, BcLocalize("Better UI"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	MainView.HSplitTop(LineSize, &Content, &MainView);
	const int BetterUiOld = g_Config.m_BcMenuUiStyle;
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMenuUiStyle, BcLocalize("Enable better UI"), &g_Config.m_BcMenuUiStyle, &Content, LineSize);
	if(BetterUiOld != g_Config.m_BcMenuUiStyle)
	{
		g_Config.m_BcMenuUiBetterFont = g_Config.m_BcMenuUiStyle != 0 ? 1 : 0;
		BestClientRefreshMenuFont(GameClient());
	}

	if(NewUiExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = NewUiExpandedHeight;
		Ui()->ClipEnable(&Visible);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMenuUiNewCheckbox, BcLocalize("Better checkbox"), &g_Config.m_BcMenuUiNewCheckbox, &Content, LineSize);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMenuUiNewScrollbar, BcLocalize("Better scrollbar"), &g_Config.m_BcMenuUiNewScrollbar, &Content, LineSize);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMenuUiNewTabOption, BcLocalize("Better tab option"), &g_Config.m_BcMenuUiNewTabOption, &Content, LineSize);

		if(TabOptionExpanded)
		{
			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			MainView.HSplitTop(LineSize, &Button, &MainView);
			static CButtonContainer s_TabSideLeft;
			static CButtonContainer s_TabSideRight;
			CUIRect LeftSideButton, RightSideButton;
			Button.VSplitMid(&LeftSideButton, &RightSideButton, 2.0f);
			LeftSideButton.HMargin(2.0f, &LeftSideButton);
			RightSideButton.HMargin(2.0f, &RightSideButton);
			if(DoButton_Menu(&s_TabSideLeft, BcLocalize("Left"), g_Config.m_BcMenuUiTabSide == 0, &LeftSideButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
				g_Config.m_BcMenuUiTabSide = 0;
			if(DoButton_Menu(&s_TabSideRight, BcLocalize("Right"), g_Config.m_BcMenuUiTabSide == 1, &RightSideButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
				g_Config.m_BcMenuUiTabSide = 1;
		}

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMenuUiNewColorPicker, BcLocalize("Better color picker"), &g_Config.m_BcMenuUiNewColorPicker, &Content, LineSize);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		const int BetterFontOld = g_Config.m_BcMenuUiBetterFont;
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMenuUiBetterFont, BcLocalize("Better font"), &g_Config.m_BcMenuUiBetterFont, &Content, LineSize);
		if(BetterFontOld != g_Config.m_BcMenuUiBetterFont)
			BestClientRefreshMenuFont(GameClient());

		Ui()->ClipDisable();
	}

	RightColumn.HSplitTop(MarginBetweenViews, nullptr, &RightColumn);

	const bool UiThemesExpanded = g_Config.m_BcUiThemes != 0;
	const float UiThemesHeaderHeight = LineSize + MarginSmall + LineSize;
	const float UiThemesBackgroundHeight = MarginSmall + LineSize + (g_Config.m_BcCustomUiBackground ? (ColorPickerSpacing + ColorPickerLineSize) : 0.0f);
	const float UiThemesExportImportHeight = MarginSmall + LineSize;
	float UiThemesCustomHeight = 0.0f;
	if(UiThemesExpanded)
	{
		g_Config.m_BcUiThemeElement = std::clamp(g_Config.m_BcUiThemeElement, 0, BestClientUiTheme::UI_THEME_ELEM_NUM - 1);
		const int Elem = g_Config.m_BcUiThemeElement;
		const int aEnabled[] = {
			g_Config.m_BcCustomUiText, g_Config.m_BcCustomUiCheckbox, g_Config.m_BcCustomUiScrollbar, g_Config.m_BcCustomUiButton,
			g_Config.m_BcCustomUiDropdown, g_Config.m_BcCustomUiEditBox, g_Config.m_BcCustomUiBlock, g_Config.m_BcCustomUiMainPanel,
			g_Config.m_BcCustomUiNavTab, g_Config.m_BcCustomUiBadge, g_Config.m_BcCustomUiBind};
		const int aGradient[] = {
			g_Config.m_BcCustomUiTextGradient, g_Config.m_BcCustomUiCheckboxGradient, g_Config.m_BcCustomUiScrollbarGradient, g_Config.m_BcCustomUiButtonGradient,
			g_Config.m_BcCustomUiDropdownGradient, g_Config.m_BcCustomUiEditBoxGradient, g_Config.m_BcCustomUiBlockGradient, g_Config.m_BcCustomUiMainPanelGradient,
			g_Config.m_BcCustomUiNavTabGradient, g_Config.m_BcCustomUiBadgeGradient, g_Config.m_BcCustomUiBindGradient};
		const int aUseAccent[] = {
			g_Config.m_BcCustomUiTextUseAccent, g_Config.m_BcCustomUiCheckboxUseAccent, g_Config.m_BcCustomUiScrollbarUseAccent, g_Config.m_BcCustomUiButtonUseAccent,
			g_Config.m_BcCustomUiDropdownUseAccent, g_Config.m_BcCustomUiEditBoxUseAccent, g_Config.m_BcCustomUiBlockUseAccent, g_Config.m_BcCustomUiMainPanelUseAccent,
			g_Config.m_BcCustomUiNavTabUseAccent, g_Config.m_BcCustomUiBadgeUseAccent, g_Config.m_BcCustomUiBindUseAccent};
		UiThemesCustomHeight = MarginSmall + LineSize;
		UiThemesCustomHeight += MarginSmall + (aEnabled[Elem] ? ColorPickerLineSize : LineSize);
		if(aEnabled[Elem])
		{
			UiThemesCustomHeight += MarginSmall + (aGradient[Elem] ? ColorPickerLineSize : LineSize);
			if(aGradient[Elem])
				UiThemesCustomHeight += MarginSmall + LineSize;
			UiThemesCustomHeight += MarginSmall + (aUseAccent[Elem] ? ColorPickerLineSize : LineSize);
			if(Elem == BestClientUiTheme::UI_THEME_ELEM_BLOCK || Elem == BestClientUiTheme::UI_THEME_ELEM_PANEL || Elem == BestClientUiTheme::UI_THEME_ELEM_NAV)
				UiThemesCustomHeight += MarginSmall + LineSize;
		}
	}
	const float UiThemesExpandedTargetHeight = UiThemesCustomHeight + UiThemesBackgroundHeight + UiThemesExportImportHeight;
	const float UiThemesExpandedHeight = BC_MODULE_REVEAL(UiThemesExpanded, UiThemesExpandedTargetHeight, Client()->RenderFrameTime());
	const float UiThemesBlockHeight = UiThemesHeaderHeight + UiThemesExpandedHeight;
	CUIRect UiThemesBlock;
	RightColumn.HSplitTop(UiThemesBlockHeight, &UiThemesBlock, &RightColumn);

	CUIRect UiThemesBlockBg = UiThemesBlock;
	UiThemesBlockBg.w += BlockPadding;
	UiThemesBlockBg.h += BlockPadding;
	UiThemesBlockBg.x -= BlockPadding * 0.5f;
	UiThemesBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&UiThemesBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcUiThemes, &UiThemesBlockBg);

	MainView = UiThemesBlock;
	MainView.HSplitTop(LineSize, &Label, &MainView);
	CUIRect UiThemesTitleLabel, UiThemesResetButton;
	Label.VSplitRight(LineSize + 8.0f, &UiThemesTitleLabel, &UiThemesResetButton);
	static CButtonContainer s_UiThemesResetButton;
	const bool UiThemesResetClicked = Ui()->DoButton_FontIcon(&s_UiThemesResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &UiThemesResetButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_UiThemesResetButton, &UiThemesResetButton, BcLocalize("Reset to defaults"));
	if(UiThemesResetClicked)
	{
		g_Config.m_BcUiThemeElement = 0;
		g_Config.m_BcCustomUiBackground = 0;
		g_Config.m_BcCustomUiText = 0;
		g_Config.m_BcCustomUiTextColor = 0xFFFFFFFFU;
		g_Config.m_BcCustomUiTextGradient = 0;
		g_Config.m_BcCustomUiTextGradientColor = 0xFFA0A0A0U;
		g_Config.m_BcCustomUiTextUseAccent = 0;
		g_Config.m_BcCustomUiTextAccentColor = 0xFFFFFFFFU;
		g_Config.m_BcCustomUiCheckbox = 0;
		g_Config.m_BcCustomUiCheckboxColor = 0xFFFFFFFFU;
		g_Config.m_BcCustomUiCheckboxGradient = 0;
		g_Config.m_BcCustomUiCheckboxGradientColor = 0xFFA0A0A0U;
		g_Config.m_BcCustomUiCheckboxUseAccent = 0;
		g_Config.m_BcCustomUiCheckboxAccentColor = 0xFFFFFFFFU;
		g_Config.m_BcCustomUiScrollbar = 0;
		g_Config.m_BcCustomUiScrollbarColor = 0xFFFFFFFFU;
		g_Config.m_BcCustomUiScrollbarGradient = 0;
		g_Config.m_BcCustomUiScrollbarGradientColor = 0xFFA0A0A0U;
		g_Config.m_BcCustomUiScrollbarUseAccent = 0;
		g_Config.m_BcCustomUiScrollbarAccentColor = 0xFFFFFFFFU;
		g_Config.m_BcCustomUiButton = 0;
		g_Config.m_BcCustomUiButtonColor = 0xFFFFFFFFU;
		g_Config.m_BcCustomUiButtonGradient = 0;
		g_Config.m_BcCustomUiButtonGradientColor = 0xFFA0A0A0U;
		g_Config.m_BcCustomUiButtonUseAccent = 0;
		g_Config.m_BcCustomUiButtonAccentColor = 0xFFFFFFFFU;
		g_Config.m_BcCustomUiDropdown = 0;
		g_Config.m_BcCustomUiDropdownColor = 0xFFFFFFFFU;
		g_Config.m_BcCustomUiDropdownGradient = 0;
		g_Config.m_BcCustomUiDropdownGradientColor = 0xFFA0A0A0U;
		g_Config.m_BcCustomUiDropdownUseAccent = 0;
		g_Config.m_BcCustomUiDropdownAccentColor = 0xFFFFFFFFU;
		g_Config.m_BcCustomUiEditBox = 0;
		g_Config.m_BcCustomUiEditBoxColor = 0xFFFFFFFFU;
		g_Config.m_BcCustomUiEditBoxGradient = 0;
		g_Config.m_BcCustomUiEditBoxGradientColor = 0xFFA0A0A0U;
		g_Config.m_BcCustomUiEditBoxUseAccent = 0;
		g_Config.m_BcCustomUiEditBoxAccentColor = 0xFFFFFFFFU;
		g_Config.m_BcCustomUiBlock = 0;
		g_Config.m_BcCustomUiBlockColor = 0xFF000000U;
		g_Config.m_BcCustomUiBlockGradient = 0;
		g_Config.m_BcCustomUiBlockGradientColor = 0xFF000000U;
		g_Config.m_BcCustomUiBlockUseAccent = 0;
		g_Config.m_BcCustomUiBlockAccentColor = 0xFF000000U;
		g_Config.m_BcCustomUiBlockOpacity = 25;
		g_Config.m_BcCustomUiMainPanel = 0;
		g_Config.m_BcCustomUiMainPanelColor = 0xFF000000U;
		g_Config.m_BcCustomUiMainPanelGradient = 0;
		g_Config.m_BcCustomUiMainPanelGradientColor = 0xFF000000U;
		g_Config.m_BcCustomUiMainPanelUseAccent = 0;
		g_Config.m_BcCustomUiMainPanelAccentColor = 0xFF000000U;
		g_Config.m_BcCustomUiMainPanelOpacity = 50;
		g_Config.m_BcCustomUiNavTab = 0;
		g_Config.m_BcCustomUiNavTabColor = 0xFF000000U;
		g_Config.m_BcCustomUiNavTabGradient = 0;
		g_Config.m_BcCustomUiNavTabGradientColor = 0xFF000000U;
		g_Config.m_BcCustomUiNavTabUseAccent = 0;
		g_Config.m_BcCustomUiNavTabAccentColor = 0xFF000000U;
		g_Config.m_BcCustomUiNavTabOpacity = 50;
		g_Config.m_BcCustomUiBadge = 0;
		g_Config.m_BcCustomUiBadgeColor = 0xFFFFFFFFU;
		g_Config.m_BcCustomUiBadgeGradient = 0;
		g_Config.m_BcCustomUiBadgeGradientColor = 0xFFA0A0A0U;
		g_Config.m_BcCustomUiBadgeUseAccent = 0;
		g_Config.m_BcCustomUiBadgeAccentColor = 0xFFFFFFFFU;
		g_Config.m_BcCustomUiBind = 0;
		g_Config.m_BcCustomUiBindColor = 0xFFFFFFFFU;
		g_Config.m_BcCustomUiBindGradient = 0;
		g_Config.m_BcCustomUiBindGradientColor = 0xFFA0A0A0U;
		g_Config.m_BcCustomUiBindUseAccent = 0;
		g_Config.m_BcCustomUiBindAccentColor = 0xFFFFFFFFU;
		g_Config.m_BcCustomUiGradientAnimateSpeed = 35;
		g_Config.m_UiColor = 0xE4A046AFU;
	}
	UiThemesTitleLabel.VSplitRight(MarginSmall, &UiThemesTitleLabel, nullptr);
	BcMenuBadges::DrawNew(Graphics(), Ui(), TextRender(), &UiThemesTitleLabel, 4.0f);
	Ui()->DoLabel(&UiThemesTitleLabel, BcLocalize("UI Themes"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcUiThemes, BcLocalize("Enable"), &g_Config.m_BcUiThemes, &Content, LineSize);

	if(UiThemesExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = UiThemesExpandedHeight;
		Ui()->ClipEnable(&Visible);

		int *apEnable[] = {
			&g_Config.m_BcCustomUiText, &g_Config.m_BcCustomUiCheckbox, &g_Config.m_BcCustomUiScrollbar, &g_Config.m_BcCustomUiButton,
			&g_Config.m_BcCustomUiDropdown, &g_Config.m_BcCustomUiEditBox, &g_Config.m_BcCustomUiBlock, &g_Config.m_BcCustomUiMainPanel,
			&g_Config.m_BcCustomUiNavTab, &g_Config.m_BcCustomUiBadge, &g_Config.m_BcCustomUiBind};
		unsigned *apColor[] = {
			&g_Config.m_BcCustomUiTextColor, &g_Config.m_BcCustomUiCheckboxColor, &g_Config.m_BcCustomUiScrollbarColor, &g_Config.m_BcCustomUiButtonColor,
			&g_Config.m_BcCustomUiDropdownColor, &g_Config.m_BcCustomUiEditBoxColor, &g_Config.m_BcCustomUiBlockColor, &g_Config.m_BcCustomUiMainPanelColor,
			&g_Config.m_BcCustomUiNavTabColor, &g_Config.m_BcCustomUiBadgeColor, &g_Config.m_BcCustomUiBindColor};
		int *apGradient[] = {
			&g_Config.m_BcCustomUiTextGradient, &g_Config.m_BcCustomUiCheckboxGradient, &g_Config.m_BcCustomUiScrollbarGradient, &g_Config.m_BcCustomUiButtonGradient,
			&g_Config.m_BcCustomUiDropdownGradient, &g_Config.m_BcCustomUiEditBoxGradient, &g_Config.m_BcCustomUiBlockGradient, &g_Config.m_BcCustomUiMainPanelGradient,
			&g_Config.m_BcCustomUiNavTabGradient, &g_Config.m_BcCustomUiBadgeGradient, &g_Config.m_BcCustomUiBindGradient};
		unsigned *apGradientColor[] = {
			&g_Config.m_BcCustomUiTextGradientColor, &g_Config.m_BcCustomUiCheckboxGradientColor, &g_Config.m_BcCustomUiScrollbarGradientColor, &g_Config.m_BcCustomUiButtonGradientColor,
			&g_Config.m_BcCustomUiDropdownGradientColor, &g_Config.m_BcCustomUiEditBoxGradientColor, &g_Config.m_BcCustomUiBlockGradientColor, &g_Config.m_BcCustomUiMainPanelGradientColor,
			&g_Config.m_BcCustomUiNavTabGradientColor, &g_Config.m_BcCustomUiBadgeGradientColor, &g_Config.m_BcCustomUiBindGradientColor};
		int *apUseAccent[] = {
			&g_Config.m_BcCustomUiTextUseAccent, &g_Config.m_BcCustomUiCheckboxUseAccent, &g_Config.m_BcCustomUiScrollbarUseAccent, &g_Config.m_BcCustomUiButtonUseAccent,
			&g_Config.m_BcCustomUiDropdownUseAccent, &g_Config.m_BcCustomUiEditBoxUseAccent, &g_Config.m_BcCustomUiBlockUseAccent, &g_Config.m_BcCustomUiMainPanelUseAccent,
			&g_Config.m_BcCustomUiNavTabUseAccent, &g_Config.m_BcCustomUiBadgeUseAccent, &g_Config.m_BcCustomUiBindUseAccent};
		unsigned *apAccentColor[] = {
			&g_Config.m_BcCustomUiTextAccentColor, &g_Config.m_BcCustomUiCheckboxAccentColor, &g_Config.m_BcCustomUiScrollbarAccentColor, &g_Config.m_BcCustomUiButtonAccentColor,
			&g_Config.m_BcCustomUiDropdownAccentColor, &g_Config.m_BcCustomUiEditBoxAccentColor, &g_Config.m_BcCustomUiBlockAccentColor, &g_Config.m_BcCustomUiMainPanelAccentColor,
			&g_Config.m_BcCustomUiNavTabAccentColor, &g_Config.m_BcCustomUiBadgeAccentColor, &g_Config.m_BcCustomUiBindAccentColor};
		const ColorRGBA aDefaults[] = {
			ColorRGBA(1, 1, 1, 1), ColorRGBA(1, 1, 1, 1), ColorRGBA(1, 1, 1, 1), ColorRGBA(1, 1, 1, 1),
			ColorRGBA(1, 1, 1, 1), ColorRGBA(1, 1, 1, 1), ColorRGBA(0, 0, 0, 1), ColorRGBA(0, 0, 0, 1),
			ColorRGBA(0, 0, 0, 1), ColorRGBA(1, 1, 1, 1), ColorRGBA(1, 1, 1, 1)};

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		const char *apUiThemeElements[] = {
			BcLocalize("Text"), BcLocalize("Checkbox"), BcLocalize("Scrollbar"), BcLocalize("Button"), BcLocalize("Droplist"),
			BcLocalize("Input field"), BcLocalize("Function block"), BcLocalize("UI background"), BcLocalize("Settings tabs"),
			BcLocalize("Badge"), BcLocalize("Binds")};
		static CUi::SDropDownState s_UiThemeElementDropDownState;
		static CScrollRegion s_UiThemeElementDropDownScrollRegion;
		s_UiThemeElementDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_UiThemeElementDropDownScrollRegion;
		g_Config.m_BcUiThemeElement = Ui()->DoDropDown(&Content, g_Config.m_BcUiThemeElement, apUiThemeElements, (int)std::size(apUiThemeElements), s_UiThemeElementDropDownState);
		g_Config.m_BcUiThemeElement = std::clamp(g_Config.m_BcUiThemeElement, 0, BestClientUiTheme::UI_THEME_ELEM_NUM - 1);
		const int Elem = g_Config.m_BcUiThemeElement;

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		{
			const float EnableRowH = *apEnable[Elem] ? ColorPickerLineSize : LineSize;
			MainView.HSplitTop(EnableRowH, &Content, &MainView);
			CUIRect CheckArea, ColorPicker;
			Content.VSplitRight(*apEnable[Elem] ? ColorPickerLineSize : 0.0f, &CheckArea, &ColorPicker);
			CUIRect CheckLine = CheckArea;
			CheckLine.HMargin((CheckArea.h - LineSize) * 0.5f, &CheckLine);
			DoButton_CheckBoxAutoVMarginAndSet(apEnable[Elem], BcLocalize("Enable"), apEnable[Elem], &CheckLine, LineSize);
			if(*apEnable[Elem])
			{
				DoButton_ColorPicker(&ColorPicker, apColor[Elem], true, aDefaults[Elem], true);
				BestClientSettingsSearch::DrawItemHighlight(apColor[Elem], &ColorPicker);
			}
		}

		if(*apEnable[Elem])
		{
			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			{
				const float GradientRowH = *apGradient[Elem] ? ColorPickerLineSize : LineSize;
				MainView.HSplitTop(GradientRowH, &Content, &MainView);
				CUIRect CheckArea, GradientPicker;
				Content.VSplitRight(*apGradient[Elem] ? ColorPickerLineSize : 0.0f, &CheckArea, &GradientPicker);
				CUIRect CheckLine = CheckArea;
				CheckLine.HMargin((CheckArea.h - LineSize) * 0.5f, &CheckLine);
				const int PrevGradient = *apGradient[Elem];
				DoButton_CheckBoxAutoVMarginAndSet(apGradient[Elem], BcLocalize("Gradient"), apGradient[Elem], &CheckLine, LineSize);
				if(PrevGradient == 0 && *apGradient[Elem] != 0)
					*apGradientColor[Elem] = BestClientUiTheme::DarkenConfigColor(*apColor[Elem]);
				if(*apGradient[Elem])
				{
					DoButton_ColorPicker(&GradientPicker, apGradientColor[Elem], true, aDefaults[Elem], true);
					BestClientSettingsSearch::DrawItemHighlight(apGradientColor[Elem], &GradientPicker);
				}
			}

			if(*apGradient[Elem])
			{
				MainView.HSplitTop(MarginSmall, nullptr, &MainView);
				MainView.HSplitTop(LineSize, &Button, &MainView);
				Ui()->DoScrollbarOption(&g_Config.m_BcCustomUiGradientAnimateSpeed, &g_Config.m_BcCustomUiGradientAnimateSpeed, &Button, BcLocalize("Animate speed"), 0, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, "%");
			}

			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			{
				const float AccentRowH = *apUseAccent[Elem] ? ColorPickerLineSize : LineSize;
				MainView.HSplitTop(AccentRowH, &Content, &MainView);
				CUIRect CheckArea, AccentPicker;
				Content.VSplitRight(*apUseAccent[Elem] ? ColorPickerLineSize : 0.0f, &CheckArea, &AccentPicker);
				CUIRect CheckLine = CheckArea;
				CheckLine.HMargin((CheckArea.h - LineSize) * 0.5f, &CheckLine);
				DoButton_CheckBoxAutoVMarginAndSet(apUseAccent[Elem], BcLocalize("Accent"), apUseAccent[Elem], &CheckLine, LineSize);
				if(*apUseAccent[Elem])
				{
					DoButton_ColorPicker(&AccentPicker, apAccentColor[Elem], true, aDefaults[Elem], true);
					BestClientSettingsSearch::DrawItemHighlight(apAccentColor[Elem], &AccentPicker);
				}
			}

			if(Elem == BestClientUiTheme::UI_THEME_ELEM_BLOCK)
			{
				MainView.HSplitTop(MarginSmall, nullptr, &MainView);
				MainView.HSplitTop(LineSize, &Button, &MainView);
				Ui()->DoScrollbarOption(&g_Config.m_BcCustomUiBlockOpacity, &g_Config.m_BcCustomUiBlockOpacity, &Button, BcLocalize("Opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, "%");
			}
			else if(Elem == BestClientUiTheme::UI_THEME_ELEM_PANEL)
			{
				MainView.HSplitTop(MarginSmall, nullptr, &MainView);
				MainView.HSplitTop(LineSize, &Button, &MainView);
				Ui()->DoScrollbarOption(&g_Config.m_BcCustomUiMainPanelOpacity, &g_Config.m_BcCustomUiMainPanelOpacity, &Button, BcLocalize("Opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, "%");
			}
			else if(Elem == BestClientUiTheme::UI_THEME_ELEM_NAV)
			{
				MainView.HSplitTop(MarginSmall, nullptr, &MainView);
				MainView.HSplitTop(LineSize, &Button, &MainView);
				Ui()->DoScrollbarOption(&g_Config.m_BcCustomUiNavTabOpacity, &g_Config.m_BcCustomUiNavTabOpacity, &Button, BcLocalize("Opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, "%");
			}
		}

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		static char s_aSavedMenuMap[sizeof(g_Config.m_ClMenuMap)] = "";
		static bool s_HasSavedMenuMap = false;
		const int PrevBackground = g_Config.m_BcCustomUiBackground;
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcCustomUiBackground, BcLocalize("Enable colorable background"), &g_Config.m_BcCustomUiBackground, &Content, LineSize);
		if(PrevBackground == 0 && g_Config.m_BcCustomUiBackground != 0)
		{
			str_copy(s_aSavedMenuMap, g_Config.m_ClMenuMap);
			s_HasSavedMenuMap = true;
			str_copy(g_Config.m_ClMenuMap, "");
			GameClient()->m_MenuBackground.LoadMenuBackground(false, false);
		}
		if(PrevBackground != 0 && g_Config.m_BcCustomUiBackground == 0)
		{
			if(s_HasSavedMenuMap)
			{
				str_copy(g_Config.m_ClMenuMap, s_aSavedMenuMap);
				s_HasSavedMenuMap = false;
			}
			GameClient()->m_MenuBackground.LoadMenuBackground();
		}
		if(g_Config.m_BcCustomUiBackground)
		{
			MainView.HSplitTop(ColorPickerSpacing, nullptr, &MainView);
			static CButtonContainer s_BackgroundColorResetId;
			DoLine_ColorPicker(&s_BackgroundColorResetId, ColorPickerLineSize, 13.0f, 0.0f, &MainView, BcLocalize("Background color"), &g_Config.m_UiColor, color_cast<ColorRGBA>(ColorHSLA(0xE4A046AFU, true)), false, nullptr, true);
		}

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		CUIRect ExportButton, ImportButton;
		Button.VSplitMid(&ExportButton, &ImportButton, MarginSmall);
		static CButtonContainer s_UiThemeExportButton;
		static CButtonContainer s_UiThemeImportButton;
		static int64_t s_UiThemeExportFeedbackTime = 0;
		static int64_t s_UiThemeImportFeedbackTime = 0;
		const int64_t Now = time_get();
		const int64_t FeedbackDuration = time_freq();
		const char *pExportLabel = (s_UiThemeExportFeedbackTime != 0 && Now - s_UiThemeExportFeedbackTime < FeedbackDuration) ? BcLocalize("Exported!") : BcLocalize("Export");
		const char *pImportLabel = (s_UiThemeImportFeedbackTime != 0 && Now - s_UiThemeImportFeedbackTime < FeedbackDuration) ? BcLocalize("Imported!") : BcLocalize("Import");
		if(DoButton_Menu(&s_UiThemeExportButton, pExportLabel, 0, &ExportButton, BUTTONFLAG_LEFT))
		{
			char aBuf[4096];
			aBuf[0] = '\0';
			auto AppendLine = [&](const char *pLine) {
				str_append(aBuf, pLine, sizeof(aBuf));
				str_append(aBuf, "\n", sizeof(aBuf));
			};
			auto AppendElem = [&](const char *pName, int Enable, unsigned Color, int Gradient, unsigned GradientColor, int Accent, unsigned AccentColor, int Opacity = -1) {
				char aLine[256];
				if(Opacity >= 0)
					str_format(aLine, sizeof(aLine), "%s enable=%d color=$%08X gradient=%d gradient_color=$%08X accent=%d accent_color=$%08X opacity=%d", pName, Enable, Color, Gradient, GradientColor, Accent, AccentColor, Opacity);
				else
					str_format(aLine, sizeof(aLine), "%s enable=%d color=$%08X gradient=%d gradient_color=$%08X accent=%d accent_color=$%08X", pName, Enable, Color, Gradient, GradientColor, Accent, AccentColor);
				AppendLine(aLine);
			};
			AppendLine("BC_UI_THEME_EXPORT v1");
			{
				char aLine[128];
				str_format(aLine, sizeof(aLine), "animate_speed=%d", g_Config.m_BcCustomUiGradientAnimateSpeed);
				AppendLine(aLine);
				str_format(aLine, sizeof(aLine), "background=%d", g_Config.m_BcCustomUiBackground);
				AppendLine(aLine);
				str_format(aLine, sizeof(aLine), "ui_color=$%08X", g_Config.m_UiColor);
				AppendLine(aLine);
			}
			AppendElem("text", g_Config.m_BcCustomUiText, g_Config.m_BcCustomUiTextColor, g_Config.m_BcCustomUiTextGradient, g_Config.m_BcCustomUiTextGradientColor, g_Config.m_BcCustomUiTextUseAccent, g_Config.m_BcCustomUiTextAccentColor);
			AppendElem("checkbox", g_Config.m_BcCustomUiCheckbox, g_Config.m_BcCustomUiCheckboxColor, g_Config.m_BcCustomUiCheckboxGradient, g_Config.m_BcCustomUiCheckboxGradientColor, g_Config.m_BcCustomUiCheckboxUseAccent, g_Config.m_BcCustomUiCheckboxAccentColor);
			AppendElem("scrollbar", g_Config.m_BcCustomUiScrollbar, g_Config.m_BcCustomUiScrollbarColor, g_Config.m_BcCustomUiScrollbarGradient, g_Config.m_BcCustomUiScrollbarGradientColor, g_Config.m_BcCustomUiScrollbarUseAccent, g_Config.m_BcCustomUiScrollbarAccentColor);
			AppendElem("button", g_Config.m_BcCustomUiButton, g_Config.m_BcCustomUiButtonColor, g_Config.m_BcCustomUiButtonGradient, g_Config.m_BcCustomUiButtonGradientColor, g_Config.m_BcCustomUiButtonUseAccent, g_Config.m_BcCustomUiButtonAccentColor);
			AppendElem("droplist", g_Config.m_BcCustomUiDropdown, g_Config.m_BcCustomUiDropdownColor, g_Config.m_BcCustomUiDropdownGradient, g_Config.m_BcCustomUiDropdownGradientColor, g_Config.m_BcCustomUiDropdownUseAccent, g_Config.m_BcCustomUiDropdownAccentColor);
			AppendElem("input", g_Config.m_BcCustomUiEditBox, g_Config.m_BcCustomUiEditBoxColor, g_Config.m_BcCustomUiEditBoxGradient, g_Config.m_BcCustomUiEditBoxGradientColor, g_Config.m_BcCustomUiEditBoxUseAccent, g_Config.m_BcCustomUiEditBoxAccentColor);
			AppendElem("block", g_Config.m_BcCustomUiBlock, g_Config.m_BcCustomUiBlockColor, g_Config.m_BcCustomUiBlockGradient, g_Config.m_BcCustomUiBlockGradientColor, g_Config.m_BcCustomUiBlockUseAccent, g_Config.m_BcCustomUiBlockAccentColor, g_Config.m_BcCustomUiBlockOpacity);
			AppendElem("panel", g_Config.m_BcCustomUiMainPanel, g_Config.m_BcCustomUiMainPanelColor, g_Config.m_BcCustomUiMainPanelGradient, g_Config.m_BcCustomUiMainPanelGradientColor, g_Config.m_BcCustomUiMainPanelUseAccent, g_Config.m_BcCustomUiMainPanelAccentColor, g_Config.m_BcCustomUiMainPanelOpacity);
			AppendElem("nav", g_Config.m_BcCustomUiNavTab, g_Config.m_BcCustomUiNavTabColor, g_Config.m_BcCustomUiNavTabGradient, g_Config.m_BcCustomUiNavTabGradientColor, g_Config.m_BcCustomUiNavTabUseAccent, g_Config.m_BcCustomUiNavTabAccentColor, g_Config.m_BcCustomUiNavTabOpacity);
			AppendElem("badge", g_Config.m_BcCustomUiBadge, g_Config.m_BcCustomUiBadgeColor, g_Config.m_BcCustomUiBadgeGradient, g_Config.m_BcCustomUiBadgeGradientColor, g_Config.m_BcCustomUiBadgeUseAccent, g_Config.m_BcCustomUiBadgeAccentColor);
			AppendElem("binds", g_Config.m_BcCustomUiBind, g_Config.m_BcCustomUiBindColor, g_Config.m_BcCustomUiBindGradient, g_Config.m_BcCustomUiBindGradientColor, g_Config.m_BcCustomUiBindUseAccent, g_Config.m_BcCustomUiBindAccentColor);
			Input()->SetClipboardText(aBuf);
			s_UiThemeExportFeedbackTime = Now;
		}
		GameClient()->m_Tooltips.DoToolTip(&s_UiThemeExportButton, &ExportButton, BcLocalize("Copy theme to clipboard"));
		if(DoButton_Menu(&s_UiThemeImportButton, pImportLabel, 0, &ImportButton, BUTTONFLAG_LEFT))
		{
			const std::string Clipboard = Input()->GetClipboardText();
			if(BcUiThemeImportFromText(Clipboard.c_str()))
			{
				if(g_Config.m_BcCustomUiBackground)
				{
					str_copy(g_Config.m_ClMenuMap, "");
					GameClient()->m_MenuBackground.LoadMenuBackground(false, false);
				}
				s_UiThemeImportFeedbackTime = Now;
			}
		}
		GameClient()->m_Tooltips.DoToolTip(&s_UiThemeImportButton, &ImportButton, BcLocalize("Paste theme from clipboard"));

		Ui()->ClipDisable();
	}

	RightColumn.HSplitTop(MarginBetweenViews, nullptr, &RightColumn);

	RenderSettingsBestClientFx(RightColumn, true);

	const bool MotionBlurExpanded = g_Config.m_BcMotionBlur != 0;
	const float MotionBlurExpandedTargetHeight = MarginSmall + LineSize;
	const float MotionBlurExpandedHeight = BC_MODULE_REVEAL(MotionBlurExpanded, MotionBlurExpandedTargetHeight, Client()->RenderFrameTime());
	const float MotionBlurBlockHeight = LineSize + MarginSmall + LineSize + MotionBlurExpandedHeight;

	CUIRect MotionBlurBlock;
	RightColumn.HSplitTop(MotionBlurBlockHeight, &MotionBlurBlock, &RightColumn);

	CUIRect MotionBlurBlockBg = MotionBlurBlock;
	MotionBlurBlockBg.w += BlockPadding;
	MotionBlurBlockBg.h += BlockPadding;
	MotionBlurBlockBg.x -= BlockPadding * 0.5f;
	MotionBlurBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&MotionBlurBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcMotionBlur, &MotionBlurBlockBg);

	MainView = MotionBlurBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	CUIRect MotionBlurTitleLabel, MotionBlurResetButton;
	Label.VSplitRight(LineSize + 8.0f, &MotionBlurTitleLabel, &MotionBlurResetButton);
	static CButtonContainer s_MotionBlurResetButton;
	const bool MotionBlurResetClicked = Ui()->DoButton_FontIcon(&s_MotionBlurResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &MotionBlurResetButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_MotionBlurResetButton, &MotionBlurResetButton, BcLocalize("Reset to defaults"));
	if(MotionBlurResetClicked)
		g_Config.m_BcMotionBlurStrength = DefaultConfig::BcMotionBlurStrength;
	MotionBlurTitleLabel.VSplitRight(MarginSmall, &MotionBlurTitleLabel, nullptr);
	BcMenuBadges::DrawBeta(Graphics(), Ui(), TextRender(), &MotionBlurTitleLabel, MarginSmall);
	Ui()->DoLabel(&MotionBlurTitleLabel, BcLocalize("Motion Blur"), HeadlineFontSize, TEXTALIGN_ML);

	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMotionBlur, BcLocalize("Enable motion blur"), &g_Config.m_BcMotionBlur, &Content, LineSize);

	if(MotionBlurExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = MotionBlurExpandedHeight;
		Ui()->ClipEnable(&Visible);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		DoSliderWithScaledValue(&g_Config.m_BcMotionBlurStrength, &g_Config.m_BcMotionBlurStrength, &Button, BcLocalize("Blend strength"), 0, 95, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "%");

		Ui()->ClipDisable();
	}

	RightColumn.HSplitTop(MarginBetweenViews, nullptr, &RightColumn);

	const bool AnimationsExpanded = g_Config.m_BcAnimations != 0;
	const float AnimationsExpandedTargetHeight = 12.0f * (MarginSmall + LineSize);
	const float AnimationsExpandedHeight = BC_MODULE_REVEAL(AnimationsExpanded, AnimationsExpandedTargetHeight, Client()->RenderFrameTime());
	const float AnimationsBlockHeight = LineSize + MarginSmall + LineSize + AnimationsExpandedHeight;

	CUIRect AnimationsBlock;
	RightColumn.HSplitTop(AnimationsBlockHeight, &AnimationsBlock, &RightColumn);

	CUIRect AnimationsBlockBg = AnimationsBlock;
	AnimationsBlockBg.w += BlockPadding;
	AnimationsBlockBg.h += BlockPadding;
	AnimationsBlockBg.x -= BlockPadding * 0.5f;
	AnimationsBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&AnimationsBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcAnimations, &AnimationsBlockBg);

	MainView = AnimationsBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	CUIRect AnimationsTitleLabel, AnimationsResetButton;
	Label.VSplitRight(LineSize + 8.0f, &AnimationsTitleLabel, &AnimationsResetButton);
	static CButtonContainer s_AnimationsResetButton;
	const bool AnimationsResetClicked = Ui()->DoButton_FontIcon(&s_AnimationsResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &AnimationsResetButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_AnimationsResetButton, &AnimationsResetButton, BcLocalize("Reset to defaults"));
	if(AnimationsResetClicked)
	{
		g_Config.m_BcAnimations = DefaultConfig::BcAnimations;
		g_Config.m_BcModuleUiRevealAnimation = DefaultConfig::BcModuleUiRevealAnimation;
		g_Config.m_BcModuleUiRevealAnimationMs = DefaultConfig::BcModuleUiRevealAnimationMs;
		g_Config.m_BcChatAnimation = DefaultConfig::BcChatAnimation;
		g_Config.m_BcChatAnimationMs = DefaultConfig::BcChatAnimationMs;
		g_Config.m_BcChatOpenAnimation = DefaultConfig::BcChatOpenAnimation;
		g_Config.m_BcChatOpenAnimationMs = DefaultConfig::BcChatOpenAnimationMs;
		g_Config.m_BcChatTypingAnimation = DefaultConfig::BcChatTypingAnimation;
		g_Config.m_BcChatTypingAnimationMs = DefaultConfig::BcChatTypingAnimationMs;
		g_Config.m_BcKillfeedAnimation = DefaultConfig::BcKillfeedAnimation;
		g_Config.m_BcKillfeedAnimationMs = DefaultConfig::BcKillfeedAnimationMs;
		g_Config.m_BcChatAnimationType = DefaultConfig::BcChatAnimationType;
		g_Config.m_BcMainMenuAnimation = DefaultConfig::BcMainMenuAnimation;
		g_Config.m_BcMainMenuAnimationSpeed = DefaultConfig::BcMainMenuAnimationSpeed;
	}
	Ui()->DoLabel(&AnimationsTitleLabel, BcLocalize("Animations"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcAnimations, BcLocalize("Enable animations"), &g_Config.m_BcAnimations, &Content, LineSize);

	if(AnimationsExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = AnimationsExpandedHeight;
		Ui()->ClipEnable(&Visible);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcModuleUiRevealAnimation, BcLocalize("Settings animation"), &g_Config.m_BcModuleUiRevealAnimation, &Content, LineSize);
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcModuleUiRevealAnimationMs, &g_Config.m_BcModuleUiRevealAnimationMs, &Button, BcLocalize("Settings (ms)"), 1, 500);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatAnimation, BcLocalize("Chat message animations"), &g_Config.m_BcChatAnimation, &Content, LineSize);
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcChatAnimationMs, &g_Config.m_BcChatAnimationMs, &Button, BcLocalize("Chat message (ms)"), 1, 500);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatOpenAnimation, BcLocalize("Chat open animation"), &g_Config.m_BcChatOpenAnimation, &Content, LineSize);
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcChatOpenAnimationMs, &g_Config.m_BcChatOpenAnimationMs, &Button, BcLocalize("Chat open (ms)"), 1, 500);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatTypingAnimation, BcLocalize("Chat typing animation"), &g_Config.m_BcChatTypingAnimation, &Content, LineSize);
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcChatTypingAnimationMs, &g_Config.m_BcChatTypingAnimationMs, &Button, BcLocalize("Chat typing (ms)"), 1, 500);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcKillfeedAnimation, BcLocalize("Killfeed animation"), &g_Config.m_BcKillfeedAnimation, &Content, LineSize);
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcKillfeedAnimationMs, &g_Config.m_BcKillfeedAnimationMs, &Button, BcLocalize("Killfeed (ms)"), 1, 500);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMainMenuAnimation, BcLocalize("Main menu animation"), &g_Config.m_BcMainMenuAnimation, &Content, LineSize);
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcMainMenuAnimationSpeed, &g_Config.m_BcMainMenuAnimationSpeed, &Button, BcLocalize("Main menu speed"), 1, 50);

		Ui()->ClipDisable();
	}

	RightColumn.HSplitTop(MarginBetweenViews, nullptr, &RightColumn);

	const float MusicPlayerColorPickerLineSize = 25.0f;
	const float MusicPlayerColorPickerLabelSize = 13.0f;
	const float MusicPlayerColorPickerSpacing = 5.0f;
	const bool MusicPlayerEnabled = g_Config.m_BcMusicPlayer != 0;
	const bool MusicPlayerShowStaticColor = MusicPlayerEnabled && g_Config.m_BcMusicPlayerColorMode == 0;
	const bool MusicPlayerShowLyrics = MusicPlayerEnabled && g_Config.m_BcMusicPlayerShowLyrics != 0;
	const float MusicPlayerBaseRows = 6.0f;
	const float MusicPlayerLyricsTargetHeight = MarginSmall + LineSize;
	const float MusicPlayerLyricsExpandedHeight = BC_MODULE_REVEAL(MusicPlayerShowLyrics, MusicPlayerLyricsTargetHeight, Client()->RenderFrameTime());
	const float MusicPlayerExpandedTargetHeight =
		(MarginSmall + LineSize) * MusicPlayerBaseRows +
		(MusicPlayerShowStaticColor ? MusicPlayerColorPickerSpacing + MusicPlayerColorPickerLineSize : 0.0f) +
		MusicPlayerLyricsExpandedHeight;
	const float MusicPlayerExpandedHeight = BC_MODULE_REVEAL(MusicPlayerEnabled, MusicPlayerExpandedTargetHeight, Client()->RenderFrameTime());
	const float MusicPlayerContentHeight = LineSize + MarginSmall + LineSize + MusicPlayerExpandedHeight;

	CUIRect MusicPlayerBlock;
	RightColumn.HSplitTop(MusicPlayerContentHeight, &MusicPlayerBlock, &RightColumn);

	CUIRect MusicPlayerBlockBg = MusicPlayerBlock;
	MusicPlayerBlockBg.w += BlockPadding;
	MusicPlayerBlockBg.h += BlockPadding;
	MusicPlayerBlockBg.x -= BlockPadding * 0.5f;
	MusicPlayerBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&MusicPlayerBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcMusicPlayer, &MusicPlayerBlockBg);

	MainView = MusicPlayerBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	CUIRect MusicPlayerTitleLabel, MusicPlayerHudEditorButton, MusicPlayerResetButton;
	Label.VSplitRight(LineSize + 8.0f, &Label, &MusicPlayerResetButton);
	Label.VSplitRight(MarginSmall, &Label, nullptr);
	Label.VSplitRight(LineSize + 8.0f, &MusicPlayerTitleLabel, &MusicPlayerHudEditorButton);
	static CButtonContainer s_MusicPlayerResetButton;
	if(Ui()->DoButton_FontIcon(&s_MusicPlayerResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &MusicPlayerResetButton, BUTTONFLAG_LEFT))
	{
		g_Config.m_BcMusicPlayerColorMode = DefaultConfig::BcMusicPlayerColorMode;
		g_Config.m_BcMusicPlayerStaticColor = DefaultConfig::BcMusicPlayerStaticColor;
		g_Config.m_BcMusicPlayerTextScale = DefaultConfig::BcMusicPlayerTextScale;
		g_Config.m_BcMusicPlayerVisualizerMode = DefaultConfig::BcMusicPlayerVisualizerMode;
		g_Config.m_BcMusicPlayerVisualizerRounding = DefaultConfig::BcMusicPlayerVisualizerRounding;
		g_Config.m_BcMusicPlayerVisualizerColumns = DefaultConfig::BcMusicPlayerVisualizerColumns;
		g_Config.m_BcMusicPlayerShowLyrics = DefaultConfig::BcMusicPlayerShowLyrics;
		g_Config.m_BcMusicPlayerShowCurrentTime = DefaultConfig::BcMusicPlayerShowCurrentTime;
	}
	GameClient()->m_Tooltips.DoToolTip(&s_MusicPlayerResetButton, &MusicPlayerResetButton, BcLocalize("Reset to defaults"));
	static CButtonContainer s_MusicPlayerHudEditorButton;
	const bool MusicPlayerCanOpenHudEditor = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
	const bool MusicPlayerHudEditorClicked = Ui()->DoButton_FontIcon(&s_MusicPlayerHudEditorButton, FontIcon::UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, MusicPlayerCanOpenHudEditor ? 0 : -1, &MusicPlayerHudEditorButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_MusicPlayerHudEditorButton, &MusicPlayerHudEditorButton, MusicPlayerCanOpenHudEditor ? BcLocalize("Open in HUD editor") : BcLocalize("Join a game first"));
	GameClient()->m_Tooltips.SetFadeTime(&s_MusicPlayerHudEditorButton, 0.0f);
	if(MusicPlayerHudEditorClicked && MusicPlayerCanOpenHudEditor)
	{
		SetActive(false);
		GameClient()->m_HudEditor.Activate();
	}
	Ui()->DoLabel(&MusicPlayerTitleLabel, BcLocalize("Music Player"), HeadlineFontSize, TEXTALIGN_ML);

	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	MainView.HSplitTop(LineSize, &Content, &MainView);
	const CUIRect MusicPlayerEnableRow = Content;
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMusicPlayer, BcLocalize("Enable music player"), &g_Config.m_BcMusicPlayer, &Content, LineSize);
	DoHighFpsIntakeTooltip(&g_Config.m_BcMusicPlayer, MusicPlayerEnableRow);

	if(MusicPlayerExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = MusicPlayerExpandedHeight;
		Ui()->ClipEnable(&Visible);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);

		MainView.HSplitTop(LineSize, &Button, &MainView);
		CUIRect MusicPlayerColorModeLabel, MusicPlayerColorModeSelect;
		Button.VSplitLeft(150.0f, &MusicPlayerColorModeLabel, &MusicPlayerColorModeSelect);
		Ui()->DoLabel(&MusicPlayerColorModeLabel, BcLocalize("Color mode"), 14.0f, TEXTALIGN_ML);
		static CUi::SDropDownState s_MusicPlayerColorModeState;
		static CScrollRegion s_MusicPlayerColorModeScrollRegion;
		s_MusicPlayerColorModeState.m_SelectionPopupContext.m_pScrollRegion = &s_MusicPlayerColorModeScrollRegion;
		const char *apMusicPlayerColorModes[2] = {
			BcLocalize("Static"),
			BcLocalize("Cover"),
		};
		g_Config.m_BcMusicPlayerColorMode = std::clamp(g_Config.m_BcMusicPlayerColorMode, 0, 1);
		g_Config.m_BcMusicPlayerColorMode = Ui()->DoDropDown(&MusicPlayerColorModeSelect, g_Config.m_BcMusicPlayerColorMode, apMusicPlayerColorModes, (int)std::size(apMusicPlayerColorModes), s_MusicPlayerColorModeState);

		if(MusicPlayerShowStaticColor)
		{
			static CButtonContainer s_MusicPlayerStaticColorButton;
			MainView.HSplitTop(MusicPlayerColorPickerSpacing, nullptr, &MainView);
			DoLine_ColorPicker(&s_MusicPlayerStaticColorButton, MusicPlayerColorPickerLineSize, MusicPlayerColorPickerLabelSize, MusicPlayerColorPickerSpacing, &MainView, BcLocalize("Static color"), &g_Config.m_BcMusicPlayerStaticColor, ColorRGBA(0.34f, 0.53f, 0.79f, 1.0f), false);
		}

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		CUIRect MusicPlayerVisualizerModeLabel, MusicPlayerVisualizerModeSelect;
		Button.VSplitLeft(150.0f, &MusicPlayerVisualizerModeLabel, &MusicPlayerVisualizerModeSelect);
		Ui()->DoLabel(&MusicPlayerVisualizerModeLabel, BcLocalize("Visualizer mode"), 14.0f, TEXTALIGN_ML);
		static CUi::SDropDownState s_MusicPlayerVisualizerModeState;
		static CScrollRegion s_MusicPlayerVisualizerModeScrollRegion;
		s_MusicPlayerVisualizerModeState.m_SelectionPopupContext.m_pScrollRegion = &s_MusicPlayerVisualizerModeScrollRegion;
		const char *apMusicPlayerVisualizerModes[3] = {
			BcLocalize("Bottom"),
			BcLocalize("Center"),
			BcLocalize("Up"),
		};
		g_Config.m_BcMusicPlayerVisualizerMode = std::clamp(g_Config.m_BcMusicPlayerVisualizerMode, 0, 2);
		g_Config.m_BcMusicPlayerVisualizerMode = Ui()->DoDropDown(&MusicPlayerVisualizerModeSelect, g_Config.m_BcMusicPlayerVisualizerMode, apMusicPlayerVisualizerModes, (int)std::size(apMusicPlayerVisualizerModes), s_MusicPlayerVisualizerModeState);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcMusicPlayerTextScale, &g_Config.m_BcMusicPlayerTextScale, &Button, BcLocalize("Text scale"), 70, 150, &CUi::ms_LinearScrollbarScale, 0u, "%");

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcMusicPlayerVisualizerColumns, &g_Config.m_BcMusicPlayerVisualizerColumns, &Button, BcLocalize("Columns"), 5, 10);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		CUIRect MusicPlayerRoundingLabel, MusicPlayerRoundingButtons;
		Button.VSplitLeft(150.0f, &MusicPlayerRoundingLabel, &MusicPlayerRoundingButtons);
		Ui()->DoLabel(&MusicPlayerRoundingLabel, BcLocalize("Rounding"), 14.0f, TEXTALIGN_ML);
		static CButtonContainer s_MusicPlayerVisualizerRoundingCube;
		static CButtonContainer s_MusicPlayerVisualizerRoundingSoft;
		if(g_Config.m_BcMusicPlayerVisualizerRounding > 200)
			g_Config.m_BcMusicPlayerVisualizerRounding = 200;
		const int MusicPlayerRoundingPreset = g_Config.m_BcMusicPlayerVisualizerRounding < 100 ? 0 : 1;
		CUIRect MusicPlayerCubeButton, MusicPlayerSoftButton;
		const float MusicPlayerRoundingSpacing = 2.0f;
		const float MusicPlayerRoundingButtonWidth = (MusicPlayerRoundingButtons.w - MusicPlayerRoundingSpacing) / 2.0f;
		MusicPlayerRoundingButtons.VSplitLeft(MusicPlayerRoundingButtonWidth, &MusicPlayerCubeButton, &MusicPlayerSoftButton);
		MusicPlayerSoftButton.VSplitLeft(MusicPlayerRoundingSpacing, nullptr, &MusicPlayerSoftButton);
		if(DoButton_Menu(&s_MusicPlayerVisualizerRoundingCube, BcLocalize("Cube"), MusicPlayerRoundingPreset == 0, &MusicPlayerCubeButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
			g_Config.m_BcMusicPlayerVisualizerRounding = 0;
		if(DoButton_Menu(&s_MusicPlayerVisualizerRoundingSoft, BcLocalize("Soft"), MusicPlayerRoundingPreset == 1, &MusicPlayerSoftButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
			g_Config.m_BcMusicPlayerVisualizerRounding = 200;

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		{
			CUIRect LyricsRow = Content;
			BcMenuBadges::DrawBeta(Graphics(), Ui(), TextRender(), &LyricsRow, MarginSmall);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMusicPlayerShowLyrics, BcLocalize("Show song lyrics"), &g_Config.m_BcMusicPlayerShowLyrics, &LyricsRow, LineSize);
		}

		if(MusicPlayerLyricsExpandedHeight > 0.5f)
		{
			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			MainView.HSplitTop(LineSize, &Content, &MainView);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMusicPlayerShowCurrentTime, BcLocalize("Show current time"), &g_Config.m_BcMusicPlayerShowCurrentTime, &Content, LineSize);
		}

		Ui()->ClipDisable();
	}

	RightColumn.HSplitTop(MarginBetweenViews, nullptr, &RightColumn);

	const bool KeystrokesIsMinecraft = g_Config.m_BcKeystrokesStyle == 1;
	const bool KeystrokesKeyboardExpanded = g_Config.m_BcKeystrokesKeyboard != 0;
	const bool KeystrokesMouseExpanded = !KeystrokesIsMinecraft && g_Config.m_BcKeystrokesMouse != 0;
	const float KeystrokesMcOptionsRows = 2.0f + (g_Config.m_BcKeystrokesMcLayout == 1 ? 3.0f : 0.0f);
	const float KeystrokesClassicPresetHeight = BC_MODULE_REVEAL(!KeystrokesIsMinecraft && KeystrokesKeyboardExpanded, MarginSmall + LineSize, Client()->RenderFrameTime());
	const float KeystrokesMouseExpandedHeight = BC_MODULE_REVEAL(KeystrokesMouseExpanded, MarginSmall + LineSize, Client()->RenderFrameTime());
	const float KeystrokesMinecraftExpandedHeight = BC_MODULE_REVEAL(KeystrokesIsMinecraft && KeystrokesKeyboardExpanded, (MarginSmall + LineSize) * KeystrokesMcOptionsRows, Client()->RenderFrameTime());
	const float KeystrokesClassicBodyHeight = LineSize + KeystrokesClassicPresetHeight + MarginSmall + LineSize + KeystrokesMouseExpandedHeight;
	const float KeystrokesMinecraftBodyHeight = LineSize + KeystrokesMinecraftExpandedHeight;
	const float KeystrokesBodyHeight = KeystrokesIsMinecraft ? KeystrokesMinecraftBodyHeight : KeystrokesClassicBodyHeight;
	const float KeystrokesBlockHeight = LineSize + MarginSmall + LineSize + MarginSmall + KeystrokesBodyHeight;

	CUIRect KeystrokesBlock;
	RightColumn.HSplitTop(KeystrokesBlockHeight, &KeystrokesBlock, &RightColumn);

	CUIRect KeystrokesBlockBg = KeystrokesBlock;
	KeystrokesBlockBg.w += BlockPadding;
	KeystrokesBlockBg.h += BlockPadding;
	KeystrokesBlockBg.x -= BlockPadding * 0.5f;
	KeystrokesBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&KeystrokesBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcKeystrokesKeyboard, &KeystrokesBlockBg);

	MainView = KeystrokesBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	CUIRect KeystrokesTitleLabel, KeystrokesHudEditorButton, KeystrokesResetButton;
	Label.VSplitRight(LineSize + 8.0f, &Label, &KeystrokesResetButton);
	Label.VSplitRight(MarginSmall, &Label, nullptr);
	Label.VSplitRight(LineSize + 8.0f, &KeystrokesTitleLabel, &KeystrokesHudEditorButton);
	static CButtonContainer s_KeystrokesResetButton;
	if(Ui()->DoButton_FontIcon(&s_KeystrokesResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &KeystrokesResetButton, BUTTONFLAG_LEFT))
	{
		g_Config.m_BcKeystrokesStyle = DefaultConfig::BcKeystrokesStyle;
		g_Config.m_BcKeystrokesKeyboardPreset = DefaultConfig::BcKeystrokesKeyboardPreset;
		g_Config.m_BcKeystrokesMousePreset = DefaultConfig::BcKeystrokesMousePreset;
		g_Config.m_BcKeystrokesMcLayout = DefaultConfig::BcKeystrokesMcLayout;
		g_Config.m_BcKeystrokesMcShowLmb = DefaultConfig::BcKeystrokesMcShowLmb;
		g_Config.m_BcKeystrokesMcShowRmb = DefaultConfig::BcKeystrokesMcShowRmb;
		g_Config.m_BcKeystrokesMcShowSpace = DefaultConfig::BcKeystrokesMcShowSpace;
		g_Config.m_BcKeystrokesMcPressedOpacity = DefaultConfig::BcKeystrokesMcPressedOpacity;
	}
	GameClient()->m_Tooltips.DoToolTip(&s_KeystrokesResetButton, &KeystrokesResetButton, BcLocalize("Reset to defaults"));
	static CButtonContainer s_KeystrokesHudEditorButton;
	const bool KeystrokesCanOpenHudEditor = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
	const bool KeystrokesHudEditorClicked = Ui()->DoButton_FontIcon(&s_KeystrokesHudEditorButton, FontIcon::UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, KeystrokesCanOpenHudEditor ? 0 : -1, &KeystrokesHudEditorButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_KeystrokesHudEditorButton, &KeystrokesHudEditorButton, KeystrokesCanOpenHudEditor ? BcLocalize("Open in HUD editor") : BcLocalize("Join a game first"));
	GameClient()->m_Tooltips.SetFadeTime(&s_KeystrokesHudEditorButton, 0.0f);
	if(KeystrokesHudEditorClicked && KeystrokesCanOpenHudEditor)
	{
		SetActive(false);
		GameClient()->m_HudEditor.Activate();
	}
	Ui()->DoLabel(&KeystrokesTitleLabel, BcLocalize("Keystrokes"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Button, &MainView);
	static CButtonContainer s_KeystrokesStyleClassic;
	static CButtonContainer s_KeystrokesStyleMinecraft;
	CUIRect ClassicStyleButton, MinecraftStyleButton;
	Button.VSplitMid(&ClassicStyleButton, &MinecraftStyleButton, 2.0f);
	ClassicStyleButton.HMargin(2.0f, &ClassicStyleButton);
	MinecraftStyleButton.HMargin(2.0f, &MinecraftStyleButton);
	if(DoButton_Menu(&s_KeystrokesStyleClassic, BcLocalize("Classic"), g_Config.m_BcKeystrokesStyle == 0, &ClassicStyleButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
		g_Config.m_BcKeystrokesStyle = 0;
	if(DoButton_Menu(&s_KeystrokesStyleMinecraft, BcLocalize("Minecraft"), g_Config.m_BcKeystrokesStyle == 1, &MinecraftStyleButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
		g_Config.m_BcKeystrokesStyle = 1;
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Content, &MainView);
	const CUIRect KeystrokesKeyboardEnableRow = Content;
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcKeystrokesKeyboard, BcLocalize("Enable keyboard"), &g_Config.m_BcKeystrokesKeyboard, &Content, LineSize);
	DoHighFpsIntakeTooltip(&g_Config.m_BcKeystrokesKeyboard, KeystrokesKeyboardEnableRow);
	if(g_Config.m_BcKeystrokesKeyboard && !HudLayout::IsEnabled(HudLayout::MODULE_KEYSTROKES_KEYBOARD))
		HudLayout::SetEnabled(HudLayout::MODULE_KEYSTROKES_KEYBOARD, true);

	if(!KeystrokesIsMinecraft && KeystrokesClassicPresetHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = KeystrokesClassicPresetHeight;
		Ui()->ClipEnable(&Visible);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);

		static CButtonContainer s_KeyboardPresetFull;
		static CButtonContainer s_KeyboardPresetMinimal;
		static CButtonContainer s_KeyboardPresetMicro;
		CUIRect FullButton, Rest, MinimalButton, MicroButton;
		const float KeyboardPresetSpacing = 2.0f;
		const float KeyboardPresetButtonWidth = (Button.w - KeyboardPresetSpacing * 2.0f) / 3.0f;
		Button.VSplitLeft(KeyboardPresetButtonWidth, &FullButton, &Rest);
		Rest.VSplitLeft(KeyboardPresetSpacing, nullptr, &Rest);
		Rest.VSplitLeft(KeyboardPresetButtonWidth, &MinimalButton, &Rest);
		Rest.VSplitLeft(KeyboardPresetSpacing, nullptr, &Rest);
		MicroButton = Rest;
		FullButton.HMargin(2.0f, &FullButton);
		MinimalButton.HMargin(2.0f, &MinimalButton);
		MicroButton.HMargin(2.0f, &MicroButton);
		if(DoButton_Menu(&s_KeyboardPresetFull, BcLocalize("Full"), g_Config.m_BcKeystrokesKeyboardPreset == 1, &FullButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
		{
			g_Config.m_BcKeystrokesKeyboardPreset = 1;
			HudLayout::ResetPosition(HudLayout::MODULE_KEYSTROKES_MOUSE);
		}
		if(DoButton_Menu(&s_KeyboardPresetMinimal, BcLocalize("Minimal"), g_Config.m_BcKeystrokesKeyboardPreset == 0, &MinimalButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
		{
			g_Config.m_BcKeystrokesKeyboardPreset = 0;
			HudLayout::ResetPosition(HudLayout::MODULE_KEYSTROKES_MOUSE);
		}
		if(DoButton_Menu(&s_KeyboardPresetMicro, BcLocalize("Micro"), g_Config.m_BcKeystrokesKeyboardPreset == 2, &MicroButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
		{
			g_Config.m_BcKeystrokesKeyboardPreset = 2;
			HudLayout::ResetPosition(HudLayout::MODULE_KEYSTROKES_MOUSE);
		}

		Ui()->ClipDisable();
	}

	if(KeystrokesIsMinecraft && KeystrokesMinecraftExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = KeystrokesMinecraftExpandedHeight;
		Ui()->ClipEnable(&Visible);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		static CButtonContainer s_McLayoutFull;
		static CButtonContainer s_McLayoutOnlyAd;
		CUIRect McFullButton, McOnlyAdButton;
		Button.VSplitMid(&McFullButton, &McOnlyAdButton, 2.0f);
		McFullButton.HMargin(2.0f, &McFullButton);
		McOnlyAdButton.HMargin(2.0f, &McOnlyAdButton);
		if(DoButton_Menu(&s_McLayoutFull, BcLocalize("Full"), g_Config.m_BcKeystrokesMcLayout == 0, &McFullButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
			g_Config.m_BcKeystrokesMcLayout = 0;
		if(DoButton_Menu(&s_McLayoutOnlyAd, BcLocalize("Only A/D"), g_Config.m_BcKeystrokesMcLayout == 1, &McOnlyAdButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
			g_Config.m_BcKeystrokesMcLayout = 1;

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcKeystrokesMcPressedOpacity, &g_Config.m_BcKeystrokesMcPressedOpacity, &Button, BcLocalize("Pressed opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");

		if(g_Config.m_BcKeystrokesMcLayout == 1)
		{
			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			MainView.HSplitTop(LineSize, &Content, &MainView);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcKeystrokesMcShowLmb, BcLocalize("Enable LMB"), &g_Config.m_BcKeystrokesMcShowLmb, &Content, LineSize);

			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			MainView.HSplitTop(LineSize, &Content, &MainView);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcKeystrokesMcShowRmb, BcLocalize("Enable RMB"), &g_Config.m_BcKeystrokesMcShowRmb, &Content, LineSize);

			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			MainView.HSplitTop(LineSize, &Content, &MainView);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcKeystrokesMcShowSpace, BcLocalize("Enable Space"), &g_Config.m_BcKeystrokesMcShowSpace, &Content, LineSize);
		}

		Ui()->ClipDisable();
	}

	if(!KeystrokesIsMinecraft)
	{
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		const CUIRect KeystrokesMouseEnableRow = Content;
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcKeystrokesMouse, BcLocalize("Enable mouse"), &g_Config.m_BcKeystrokesMouse, &Content, LineSize);
		DoHighFpsIntakeTooltip(&g_Config.m_BcKeystrokesMouse, KeystrokesMouseEnableRow);
		if(g_Config.m_BcKeystrokesMouse && !HudLayout::IsEnabled(HudLayout::MODULE_KEYSTROKES_MOUSE))
			HudLayout::SetEnabled(HudLayout::MODULE_KEYSTROKES_MOUSE, true);

		if(KeystrokesMouseExpandedHeight > 0.5f)
		{
			CUIRect Visible = MainView;
			Visible.h = KeystrokesMouseExpandedHeight;
			Ui()->ClipEnable(&Visible);

			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			MainView.HSplitTop(LineSize, &Button, &MainView);

			static CButtonContainer s_MousePresetArrow;
			static CButtonContainer s_MousePresetDotDot;
			static CButtonContainer s_MousePresetNothing;
			CUIRect ArrowButton, Rest, DotDotButton, NothingButton;
			const float MousePresetSpacing = 2.0f;
			const float MousePresetButtonWidth = (Button.w - MousePresetSpacing * 2.0f) / 3.0f;
			Button.VSplitLeft(MousePresetButtonWidth, &ArrowButton, &Rest);
			Rest.VSplitLeft(MousePresetSpacing, nullptr, &Rest);
			Rest.VSplitLeft(MousePresetButtonWidth, &DotDotButton, &Rest);
			Rest.VSplitLeft(MousePresetSpacing, nullptr, &Rest);
			NothingButton = Rest;
			ArrowButton.HMargin(2.0f, &ArrowButton);
			DotDotButton.HMargin(2.0f, &DotDotButton);
			NothingButton.HMargin(2.0f, &NothingButton);
			if(DoButton_Menu(&s_MousePresetArrow, BcLocalize("Arrow"), g_Config.m_BcKeystrokesMousePreset == 1, &ArrowButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
				g_Config.m_BcKeystrokesMousePreset = 1;
			if(DoButton_Menu(&s_MousePresetDotDot, BcLocalize("Dot Dot"), g_Config.m_BcKeystrokesMousePreset == 2, &DotDotButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
				g_Config.m_BcKeystrokesMousePreset = 2;
			if(DoButton_Menu(&s_MousePresetNothing, BcLocalize("Nothing"), g_Config.m_BcKeystrokesMousePreset == 3, &NothingButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
				g_Config.m_BcKeystrokesMousePreset = 3;

			Ui()->ClipDisable();
		}
	}

	RightColumn.HSplitTop(MarginBetweenViews, nullptr, &RightColumn);

	const bool AspectBlocked = GameClient()->m_AspectRatio.IsBlockedByFng();
	const int AspectMode = g_Config.m_BcCustomAspectRatioMode >= 0 ? g_Config.m_BcCustomAspectRatioMode : (g_Config.m_BcCustomAspectRatio > 0 ? 1 : 0);
	const bool AspectCustomMode = AspectMode == 2;
	const float AspectHeaderHeight = LineSize + MarginSmall + LineSize + MarginSmall + LineSize;
	const float AspectExpandedHeight = AspectCustomMode ? (MarginSmall + LineSize + MarginSmall + LineSize) : 0.0f;
	const float AspectBlockedHintHeight = AspectBlocked ? (MarginSmall + LineSize) : 0.0f;
	const float AspectBlockHeight = AspectHeaderHeight + AspectExpandedHeight + AspectBlockedHintHeight;

	CUIRect AspectBlock;
	RightColumn.HSplitTop(AspectBlockHeight, &AspectBlock, &RightColumn);

	CUIRect AspectBlockBg = AspectBlock;
	AspectBlockBg.w += BlockPadding;
	AspectBlockBg.h += BlockPadding;
	AspectBlockBg.x -= BlockPadding * 0.5f;
	AspectBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&AspectBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcCustomAspectRatioMode, &AspectBlockBg);

	MainView = AspectBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	Ui()->DoLabel(&Label, BcLocalize("Aspect Ratio"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	const auto SplitRowLabelControl = [&](CUIRect &InRow, CUIRect &OutLabel, CUIRect &OutControl) {
		const float LabelWidth = std::clamp(InRow.w * 0.40f, 90.0f, 170.0f);
		InRow.VSplitLeft(LabelWidth, &OutLabel, &OutControl);
	};

	const char *apAspectPresetNames[] = {
		BcLocalize("Off (default)"),
		"5:4",
		"4:3",
		"3:2",
		BcLocalize("Custom"),
	};
	static const int s_aAspectPresetValues[] = {0, 125, 133, 150};
	static CUi::SDropDownState s_AspectPresetState;
	static CScrollRegion s_AspectPresetScrollRegion;
	s_AspectPresetState.m_SelectionPopupContext.m_pScrollRegion = &s_AspectPresetScrollRegion;

	auto GetAspectPresetIndex = [&]() -> int {
		const int CustomPresetIndex = (int)std::size(apAspectPresetNames) - 1;
		if(AspectMode <= 0 || g_Config.m_BcCustomAspectRatio == 0)
			return 0;
		if(AspectMode == 2)
			return CustomPresetIndex;

		int BestIndex = 1;
		int BestDiff = absolute(g_Config.m_BcCustomAspectRatio - s_aAspectPresetValues[BestIndex]);
		for(size_t i = 1; i < std::size(s_aAspectPresetValues); ++i)
		{
			const int CurDiff = absolute(g_Config.m_BcCustomAspectRatio - s_aAspectPresetValues[i]);
			if(CurDiff < BestDiff)
			{
				BestDiff = CurDiff;
				BestIndex = (int)i;
			}
		}
		return BestIndex;
	};

	const int CurrentPreset = GetAspectPresetIndex();
	CUIRect PresetLabel, PresetDropDown;
	MainView.HSplitTop(LineSize, &Button, &MainView);
	SplitRowLabelControl(Button, PresetLabel, PresetDropDown);
	Ui()->DoLabel(&PresetLabel, BcLocalize("Preset"), 14.0f, TEXTALIGN_ML);
	const int NewPreset = Ui()->DoDropDown(&PresetDropDown, CurrentPreset, apAspectPresetNames, (int)std::size(apAspectPresetNames), s_AspectPresetState);
	const int CustomPresetIndex = (int)std::size(apAspectPresetNames) - 1;
	if(NewPreset != CurrentPreset)
	{
		if(NewPreset == 0)
		{
			g_Config.m_BcCustomAspectRatioMode = 0;
			g_Config.m_BcCustomAspectRatio = 0;
		}
		else if(NewPreset == CustomPresetIndex)
		{
			g_Config.m_BcCustomAspectRatioMode = 2;
			if(g_Config.m_BcCustomAspectRatio < 100)
				g_Config.m_BcCustomAspectRatio = 178;
			if(g_Config.m_BcCustomAspectRatioNum <= 0 || g_Config.m_BcCustomAspectRatioDen <= 0)
			{
				g_Config.m_BcCustomAspectRatioNum = 16;
				g_Config.m_BcCustomAspectRatioDen = 9;
				g_Config.m_BcCustomAspectRatio = 178;
			}
		}
		else
		{
			g_Config.m_BcCustomAspectRatioMode = 1;
			g_Config.m_BcCustomAspectRatio = s_aAspectPresetValues[NewPreset];
		}
		GameClient()->m_TClient.SetForcedAspect();
	}

	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	CUIRect ApplyLabel, ApplyDropDown;
	MainView.HSplitTop(LineSize, &Button, &MainView);
	SplitRowLabelControl(Button, ApplyLabel, ApplyDropDown);
	Ui()->DoLabel(&ApplyLabel, BcLocalize("Apply"), 14.0f, TEXTALIGN_ML);
	const char *apAspectApplyNames[3] = {
		BcLocalize("Game only"),
		BcLocalize("Full"),
		BcLocalize("Game no HUD"),
	};
	static CUi::SDropDownState s_AspectApplyState;
	static CScrollRegion s_AspectApplyScrollRegion;
	s_AspectApplyState.m_SelectionPopupContext.m_pScrollRegion = &s_AspectApplyScrollRegion;
	const int CurrentApplyMode = g_Config.m_BcCustomAspectRatioApplyMode;
	const int NewApplyMode = Ui()->DoDropDown(&ApplyDropDown, CurrentApplyMode, apAspectApplyNames, (int)std::size(apAspectApplyNames), s_AspectApplyState);
	if(NewApplyMode != CurrentApplyMode)
	{
		g_Config.m_BcCustomAspectRatioApplyMode = NewApplyMode;
		GameClient()->m_TClient.SetForcedAspect();
	}

	const int EffectiveAspectMode = g_Config.m_BcCustomAspectRatioMode >= 0 ? g_Config.m_BcCustomAspectRatioMode : (g_Config.m_BcCustomAspectRatio > 0 ? 1 : 0);
	static CLineInputNumber s_CustomAspectNumeratorInput;
	static CLineInputNumber s_CustomAspectDenominatorInput;
	static bool s_CustomAspectInitialized = false;
	static int s_LastSyncedNum = -1;
	static int s_LastSyncedDen = -1;
	if(EffectiveAspectMode == 2)
	{
		const int CfgNum = g_Config.m_BcCustomAspectRatioNum > 0 ? g_Config.m_BcCustomAspectRatioNum : 16;
		const int CfgDen = g_Config.m_BcCustomAspectRatioDen > 0 ? g_Config.m_BcCustomAspectRatioDen : 9;
		if(!s_CustomAspectNumeratorInput.IsActive() && !s_CustomAspectDenominatorInput.IsActive() &&
			(!s_CustomAspectInitialized || s_LastSyncedNum != CfgNum || s_LastSyncedDen != CfgDen))
		{
			s_CustomAspectNumeratorInput.SetInteger(CfgNum);
			s_CustomAspectDenominatorInput.SetInteger(CfgDen);
			s_LastSyncedNum = CfgNum;
			s_LastSyncedDen = CfgDen;
			s_CustomAspectInitialized = true;
		}

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		CUIRect RatioLabel, RatioControls;
		MainView.HSplitTop(LineSize, &Button, &MainView);
		SplitRowLabelControl(Button, RatioLabel, RatioControls);
		Ui()->DoLabel(&RatioLabel, BcLocalize("Custom size"), 14.0f, TEXTALIGN_ML);

		CUIRect NumeratorRect, SeparatorRect, DenominatorRect;
		const float Gap = std::min(6.0f, RatioControls.w * 0.08f);
		const float SeparatorWidth = std::min(12.0f, RatioControls.w * 0.18f);
		const float FieldWidth = std::max(1.0f, (RatioControls.w - SeparatorWidth - 2.0f * Gap) / 2.0f);
		RatioControls.VSplitLeft(FieldWidth, &NumeratorRect, &RatioControls);
		RatioControls.VSplitLeft(Gap, nullptr, &RatioControls);
		RatioControls.VSplitLeft(SeparatorWidth, &SeparatorRect, &RatioControls);
		RatioControls.VSplitLeft(Gap, nullptr, &RatioControls);
		RatioControls.VSplitLeft(FieldWidth, &DenominatorRect, nullptr);

		Ui()->DoEditBox(&s_CustomAspectNumeratorInput, &NumeratorRect, 14.0f);
		Ui()->DoLabel(&SeparatorRect, ":", 14.0f, TEXTALIGN_MC);
		Ui()->DoEditBox(&s_CustomAspectDenominatorInput, &DenominatorRect, 14.0f);

		const int InputNum = std::max(1, s_CustomAspectNumeratorInput.GetInteger());
		const int InputDen = std::max(1, s_CustomAspectDenominatorInput.GetInteger());
		const bool HasPendingCustomChange = InputNum != g_Config.m_BcCustomAspectRatioNum || InputDen != g_Config.m_BcCustomAspectRatioDen;

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		CUIRect ButtonSpace, ApplyButton;
		MainView.HSplitTop(LineSize, &Button, &MainView);
		SplitRowLabelControl(Button, ButtonSpace, ApplyButton);
		(void)ButtonSpace;
		static CButtonContainer s_AspectApplyButton;
		if(DoButton_Menu(&s_AspectApplyButton, BcLocalize("Apply"), HasPendingCustomChange ? 0 : -1, &ApplyButton) && HasPendingCustomChange)
		{
			g_Config.m_BcCustomAspectRatioNum = InputNum;
			g_Config.m_BcCustomAspectRatioDen = InputDen;
			g_Config.m_BcCustomAspectRatio = std::clamp((int)std::lround((double)InputNum * 100.0 / (double)InputDen), 100, 1000);
			s_LastSyncedNum = InputNum;
			s_LastSyncedDen = InputDen;
			GameClient()->m_TClient.SetForcedAspect();
		}
	}
	else
	{
		s_CustomAspectInitialized = false;
		s_LastSyncedNum = -1;
		s_LastSyncedDen = -1;
	}

	if(AspectBlocked)
	{
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Label, &MainView);
		TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
		Ui()->DoLabel(&Label, BcLocalize("Looks like you're on a server where this feature is forbidden"), 14.0f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}

	RightColumn.HSplitTop(MarginBetweenViews, nullptr, &RightColumn);

	const float PhysicBallsBlockHeight = LineSize + MarginSmall + LineSize + MarginSmall + LineSize + MarginSmall + LineSize + MarginSmall + 25.0f;
	CUIRect PhysicBallsBlock;
	RightColumn.HSplitTop(PhysicBallsBlockHeight, &PhysicBallsBlock, &RightColumn);

	CUIRect PhysicBallsBlockBg = PhysicBallsBlock;
	PhysicBallsBlockBg.w += BlockPadding;
	PhysicBallsBlockBg.h += BlockPadding;
	PhysicBallsBlockBg.x -= BlockPadding * 0.5f;
	PhysicBallsBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&PhysicBallsBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcPhysicBallsSpawnCount, &PhysicBallsBlockBg);

	MainView = PhysicBallsBlock;
	MainView.HSplitTop(LineSize, &Label, &MainView);
	CUIRect PhysicBallsTitleLabel = Label;
	PhysicBallsTitleLabel.VSplitRight(MarginSmall, &PhysicBallsTitleLabel, nullptr);
	BcMenuBadges::DrawEClient(Graphics(), Ui(), TextRender(), &PhysicBallsTitleLabel, MarginSmall);
	Ui()->DoLabel(&PhysicBallsTitleLabel, BcLocalize("Physic Balls"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Button, &MainView);
	char aBallAmount[64];
	str_format(aBallAmount, sizeof(aBallAmount), BcLocalize("Ball amount: %d"), (int)GameClient()->m_PhysicBalls.GetBallCount());
	CUIRect BallAmountLabel, ClearBallsButton;
	Button.VSplitRight(LineSize + 8.0f, &BallAmountLabel, &ClearBallsButton);
	Ui()->DoLabel(&BallAmountLabel, aBallAmount, 14.0f, TEXTALIGN_ML);
	static CButtonContainer s_ClearPhysicBallsButton;
	if(Ui()->DoButton_FontIcon(&s_ClearPhysicBallsButton, FontIcon::TRASH, 0, &ClearBallsButton, BUTTONFLAG_LEFT))
		GameClient()->m_PhysicBalls.OnReset();
	GameClient()->m_Tooltips.DoToolTip(&s_ClearPhysicBallsButton, &ClearBallsButton, BcLocalize("Clear all balls"));

	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	MainView.HSplitTop(LineSize, &Button, &MainView);
	{
		static CLineInput s_PhysicBallsSkinInput;
		s_PhysicBallsSkinInput.SetBuffer(g_Config.m_BcPhysicBallsSkin, sizeof(g_Config.m_BcPhysicBallsSkin));
		s_PhysicBallsSkinInput.SetEmptyText("volleyball");
		CUIRect SkinLabel, SkinField;
		Button.VSplitLeft(70.0f, &SkinLabel, &Button);
		Button.VSplitLeft(MarginSmall, nullptr, &Button);
		Button.VSplitLeft(140.0f, &SkinField, nullptr);
		Ui()->DoLabel(&SkinLabel, BcLocalize("Ball skin"), 14.0f, TEXTALIGN_ML);
		Ui()->DoEditBox(&s_PhysicBallsSkinInput, &SkinField, 14.0f);
	}

	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	MainView.HSplitTop(LineSize, &Button, &MainView);
	Ui()->DoScrollbarOption(&g_Config.m_BcPhysicBallsSpawnCount, &g_Config.m_BcPhysicBallsSpawnCount, &Button, BcLocalize("Spawn count"), 1, 50);

	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	MainView.HSplitTop(25.0f, &Button, &MainView);
	CUIRect SpawnBallButton, SpawnCursorButton;
	Button.VSplitLeft(110.0f, &SpawnBallButton, &Button);
	Button.VSplitLeft(MarginSmall, nullptr, &Button);
	Button.VSplitLeft(130.0f, &SpawnCursorButton, nullptr);
	static CButtonContainer s_SpawnPhysicBallButton;
	static CButtonContainer s_SpawnPhysicBallCursorButton;
	const ColorRGBA PhysicBallButtonColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f);
	const int PhysicBallsSpawnCount = std::clamp(g_Config.m_BcPhysicBallsSpawnCount, 1, 50);
	if(DoButton_Menu(&s_SpawnPhysicBallButton, BcLocalize("New Ball"), 0, &SpawnBallButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, PhysicBallButtonColor))
	{
		for(int i = 0; i < PhysicBallsSpawnCount; i++)
			GameClient()->m_PhysicBalls.NewBallPlayer(60.0f);
	}
	if(DoButton_Menu(&s_SpawnPhysicBallCursorButton, BcLocalize("New Ball Cursor"), 0, &SpawnCursorButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, PhysicBallButtonColor))
	{
		for(int i = 0; i < PhysicBallsSpawnCount; i++)
			GameClient()->m_PhysicBalls.NewBallCursor(60.0f);
	}

	const float RightColumnEndY = RightColumn.y;
	CUIRect VisualsScrollContentRect;
	VisualsScrollContentRect.x = VisualsScrollContentX;
	VisualsScrollContentRect.y = std::max(LeftColumnEndY, RightColumnEndY) + MarginSmall * 2.0f;
	VisualsScrollContentRect.w = VisualsScrollContentW;
	VisualsScrollContentRect.h = 0.0f;
	s_VisualsScrollRegion.AddRect(VisualsScrollContentRect);
	s_VisualsScrollRegion.End();
}

void CMenus::DoTickAmountSlider(int *pValue, const CUIRect *pRect, const char *pLabel, int Min, int Max, int Scale)
{
	CUIRect Button = *pRect;
	int Value = std::clamp(*pValue, Min, Max);

	const int Increment = std::max(1, (Max - Min) / 35);
	if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_UP) && Ui()->MouseInside(&Button))
		Value = std::clamp(Value + Increment, Min, Max);
	if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN) && Ui()->MouseInside(&Button))
		Value = std::clamp(Value - Increment, Min, Max);

	char aBuf[256];
	char aValueBuf[64];
	// bestclient
	if(BestClientUiTheme::IsNewScrollbar())
	{
		str_format(aValueBuf, sizeof(aValueBuf), "%.2f %s", Value / (float)Scale, BcLocalize("ticks"));
		str_copy(aBuf, pLabel);
	}
	else
		str_format(aBuf, sizeof(aBuf), "%s: %.2f %s", pLabel, Value / (float)Scale, BcLocalize("ticks"));
	// bestclient

	CUIRect Label, ScrollBar;
	Button.VSplitMid(&Label, &ScrollBar, std::min(10.0f, Button.w * 0.05f));
	Ui()->DoLabel(&Label, aBuf, Label.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_ML);

	const float Rel = (Value - Min) / (float)(Max - Min);
	// bestclient
	const float NewRel = Ui()->DoScrollbarH(pValue, &ScrollBar, Rel, nullptr, BestClientUiTheme::IsNewScrollbar() ? aValueBuf : nullptr);
	// bestclient
	Value = (int)(Min + NewRel * (Max - Min) + 0.5f);
	*pValue = std::clamp(Value, Min, Max);
}

void CMenus::RenderSettingsBestClientGameplay(CUIRect MainView)
{
	const float LineSize = 20.0f;
	const float MarginSmall = 5.0f;
	const float HeadlineFontSize = 20.0f;
	const float FontSize = 14.0f;
	const float EditBoxFontSize = 12.0f;
	const float MarginBetweenViews = 30.0f;
	const float BlockPadding = MarginBetweenViews * 0.6666f;
	const float WheelPreviewHeight = 96.0f;

	static CScrollRegion s_GameplayScrollRegion;
	CScrollRegionParams GameplayScrollParams;
	GameplayScrollParams.m_ScrollUnit = 60.0f;
	GameplayScrollParams.m_ScrollbarMargin = 5.0f;
	s_GameplayScrollRegion.Begin(&MainView, &GameplayScrollParams);
	MainView.VSplitRight(5.0f, &MainView, nullptr);
	MainView.VSplitLeft(5.0f, nullptr, &MainView);
	const float GameplayScrollContentX = MainView.x;
	const float GameplayScrollContentW = MainView.w;

	CUIRect Content, Label, Button, LeftView, RightView;

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	CUIRect LeftColumn = LeftView;
	LeftColumn.HSplitTop(10.0f, nullptr, &LeftColumn);

	const int InputsMode = g_Config.m_BcInputs;
	const bool InputsEnabled = InputsMode != BC_INPUTS_OFF;
	const bool InputsBestMode = InputsMode == BC_INPUTS_BEST;
	const float InputsModeFieldsHeight = (InputsBestMode ? 6.0f : 2.0f) * (MarginSmall + LineSize);
	const float InputsExpandedTargetHeight = (MarginSmall + LineSize) + InputsModeFieldsHeight;
	const float InputsExpandedHeight = BC_MODULE_REVEAL(InputsEnabled, InputsExpandedTargetHeight, Client()->RenderFrameTime());
	const float InputsHeaderHeight = LineSize + MarginSmall + LineSize;
	const float InputsFooterLines = g_Config.m_ClSubTickAiming ? 2.0f : 3.0f;
	const float InputsBlockHeight = InputsHeaderHeight + InputsExpandedHeight + InputsFooterLines * (MarginSmall + LineSize);

	CUIRect InputsBlock;
	LeftColumn.HSplitTop(InputsBlockHeight, &InputsBlock, &LeftColumn);

	CUIRect InputsBlockBg = InputsBlock;
	InputsBlockBg.w += BlockPadding;
	InputsBlockBg.h += BlockPadding;
	InputsBlockBg.x -= BlockPadding * 0.5f;
	InputsBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&InputsBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcInputs, &InputsBlockBg);

	InputsBlock.HSplitTop(LineSize, &Label, &InputsBlock);
	Ui()->DoLabel(&Label, BcLocalize("Inputs"), HeadlineFontSize, TEXTALIGN_ML);
	InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);

	InputsBlock.HSplitTop(LineSize, &Content, &InputsBlock);
	{
		static CButtonContainer s_InputsEnabledButton;
		static int s_LastNonOffMode = BC_INPUTS_FAST;
		if(InputsEnabled)
			s_LastNonOffMode = InputsMode;
		if(DoButton_CheckBox(&s_InputsEnabledButton, BcLocalize("Enable Inputs"), InputsEnabled, &Content))
			g_Config.m_BcInputs = InputsEnabled ? BC_INPUTS_OFF : s_LastNonOffMode;
	}

	if(InputsExpandedHeight > 0.5f)
	{
		CUIRect Visible = InputsBlock;
		Visible.h = InputsExpandedHeight;
		Ui()->ClipEnable(&Visible);

		InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
		InputsBlock.HSplitTop(LineSize, &Button, &InputsBlock);
		{
			static CButtonContainer s_InputsFast, s_InputsBest, s_InputsSaiko, s_InputsDelta, s_InputsF, s_InputsCloud;
			CUIRect ButtonsRect = Button;
			const float Spacing = 2.0f;
			const float InputButtonWidth = (ButtonsRect.w - Spacing * 5.0f) / 6.0f;

			CUIRect FastButton, BestButton, SaikoButton, DeltaButton, FButton, CloudButton;
			ButtonsRect.VSplitLeft(InputButtonWidth, &FastButton, &ButtonsRect);
			ButtonsRect.VSplitLeft(Spacing, nullptr, &ButtonsRect);
			ButtonsRect.VSplitLeft(InputButtonWidth, &BestButton, &ButtonsRect);
			ButtonsRect.VSplitLeft(Spacing, nullptr, &ButtonsRect);
			ButtonsRect.VSplitLeft(InputButtonWidth, &SaikoButton, &ButtonsRect);
			ButtonsRect.VSplitLeft(Spacing, nullptr, &ButtonsRect);
			ButtonsRect.VSplitLeft(InputButtonWidth, &DeltaButton, &ButtonsRect);
			ButtonsRect.VSplitLeft(Spacing, nullptr, &ButtonsRect);
			ButtonsRect.VSplitLeft(InputButtonWidth, &FButton, &ButtonsRect);
			ButtonsRect.VSplitLeft(Spacing, nullptr, &ButtonsRect);
			CloudButton = ButtonsRect;

			FastButton.HMargin(2.0f, &FastButton);
			BestButton.HMargin(2.0f, &BestButton);
			SaikoButton.HMargin(2.0f, &SaikoButton);
			DeltaButton.HMargin(2.0f, &DeltaButton);
			FButton.HMargin(2.0f, &FButton);
			CloudButton.HMargin(2.0f, &CloudButton);

			if(DoButton_Menu(&s_InputsFast, BcLocalize("Fast"), InputsMode == BC_INPUTS_FAST, &FastButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
				g_Config.m_BcInputs = BC_INPUTS_FAST;
			if(DoButton_Menu(&s_InputsBest, BcLocalize("Best"), InputsMode == BC_INPUTS_BEST, &BestButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
				g_Config.m_BcInputs = BC_INPUTS_BEST;
			if(DoButton_Menu(&s_InputsSaiko, BcLocalize("Saiko"), InputsMode == BC_INPUTS_SAIKO, &SaikoButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
				g_Config.m_BcInputs = BC_INPUTS_SAIKO;
			if(DoButton_Menu(&s_InputsDelta, BcLocalize("Delta"), InputsMode == BC_INPUTS_DELTA, &DeltaButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
				g_Config.m_BcInputs = BC_INPUTS_DELTA;
			if(DoButton_Menu(&s_InputsF, BcLocalize("F"), InputsMode == BC_INPUTS_F, &FButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
				g_Config.m_BcInputs = BC_INPUTS_F;
			if(DoButton_Menu(&s_InputsCloud, BcLocalize("Cloud"), InputsMode == BC_INPUTS_CLOUD, &CloudButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
				g_Config.m_BcInputs = BC_INPUTS_CLOUD;
		}

		if(InputsMode == BC_INPUTS_FAST)
		{
			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Button, &InputsBlock);
			DoSliderWithScaledValue(&g_Config.m_TcFastInputAmount, &g_Config.m_TcFastInputAmount, &Button, BcLocalize("Prediction offset"), 1, 40, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");

			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Content, &InputsBlock);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFastInputOthers, BcLocalize("Fast Input others"), &g_Config.m_TcFastInputOthers, &Content, LineSize);
		}
		else if(InputsMode == BC_INPUTS_BEST)
		{
			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Button, &InputsBlock);
			DoTickAmountSlider(&g_Config.m_BcBestInputAmount, &Button, BcLocalize("Prediction offset"), 0, 1000);

			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Button, &InputsBlock);
			Ui()->DoScrollbarOption(&g_Config.m_BcBestInputSmoothing, &g_Config.m_BcBestInputSmoothing, &Button, BcLocalize("Smoothing"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");

			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Button, &InputsBlock);
			Ui()->DoScrollbarOption(&g_Config.m_BcBestInputLatencyComp, &g_Config.m_BcBestInputLatencyComp, &Button, BcLocalize("Latency compensation"), 0, 50, &CUi::ms_LinearScrollbarScale, 0, "%");

			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Label, &InputsBlock);
			Ui()->DoLabel(&Label, BcLocalize("Interpolation"), Label.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_ML);

			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Button, &InputsBlock);
			{
				static CButtonContainer s_aInterpolationButtons[3];
				static const char *s_apInterpolationNames[] = {"Linear", "Cubic", "Smooth"};
				static const int s_aInterpolationValues[] = {1, 2, 3};

				CUIRect ButtonsRect = Button;
				const float Spacing = 2.0f;
				const float InterpolationButtonWidth = (ButtonsRect.w - Spacing * 2.0f) / 3.0f;
				for(int i = 0; i < 3; ++i)
				{
					CUIRect InterpolationButton;
					if(i < 2)
					{
						ButtonsRect.VSplitLeft(InterpolationButtonWidth, &InterpolationButton, &ButtonsRect);
						ButtonsRect.VSplitLeft(Spacing, nullptr, &ButtonsRect);
					}
					else
						InterpolationButton = ButtonsRect;
					InterpolationButton.HMargin(2.0f, &InterpolationButton);

					int Corners = IGraphics::CORNER_NONE;
					if(i == 0)
						Corners = IGraphics::CORNER_L;
					else if(i == 2)
						Corners = IGraphics::CORNER_R;

					if(DoButton_Menu(&s_aInterpolationButtons[i], BcLocalize(s_apInterpolationNames[i]), g_Config.m_BcBestInputInterpolation == s_aInterpolationValues[i], &InterpolationButton, BUTTONFLAG_LEFT, nullptr, Corners))
						g_Config.m_BcBestInputInterpolation = s_aInterpolationValues[i];
				}
			}

			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Content, &InputsBlock);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcBestInputOthers, BcLocalize("Best input others"), &g_Config.m_BcBestInputOthers, &Content, LineSize);
		}
		else if(InputsMode == BC_INPUTS_SAIKO)
		{
			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Button, &InputsBlock);
			DoTickAmountSlider(&g_Config.m_BcSaikoInputAmount, &Button, BcLocalize("Prediction offset"), 0, 500);

			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Content, &InputsBlock);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcSaikoInputOthers, BcLocalize("Saiko input others"), &g_Config.m_BcSaikoInputOthers, &Content, LineSize);
		}
		else if(InputsMode == BC_INPUTS_DELTA)
		{
			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Button, &InputsBlock);
			DoTickAmountSlider(&g_Config.m_BcDeltaInputAmount, &Button, BcLocalize("Prediction offset"), 0, 500);

			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Content, &InputsBlock);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcDeltaInputOthers, BcLocalize("Delta input others"), &g_Config.m_BcDeltaInputOthers, &Content, LineSize);
		}
		else if(InputsMode == BC_INPUTS_F)
		{
			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Button, &InputsBlock);
			DoTickAmountSlider(&g_Config.m_BcFInputAmount, &Button, BcLocalize("Prediction offset"), 0, 5000, 1000);

			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Content, &InputsBlock);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcFInputOthers, BcLocalize("F input others"), &g_Config.m_BcFInputOthers, &Content, LineSize);
		}
		else if(InputsMode == BC_INPUTS_CLOUD)
		{
			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Button, &InputsBlock);
			DoTickAmountSlider(&g_Config.m_BcCloudInputAmount, &Button, BcLocalize("Prediction offset"), 0, 500);

			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Content, &InputsBlock);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcCloudInputOthers, BcLocalize("Cloud input others"), &g_Config.m_BcCloudInputOthers, &Content, LineSize);
		}

		Ui()->ClipDisable();
	}

	InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
	InputsBlock.HSplitTop(LineSize, &Content, &InputsBlock);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClSubTickAiming, BcLocalize("Sub-Tick aiming"), &g_Config.m_ClSubTickAiming, &Content, LineSize);

	if(!g_Config.m_ClSubTickAiming)
	{
		InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
		InputsBlock.HSplitTop(LineSize, &Content, &InputsBlock);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFastInputRepredictHook, BcLocalize("Repredict on mid-tick hook direction change (may lag)"), &g_Config.m_TcFastInputRepredictHook, &Content, LineSize);
	}

	InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
	InputsBlock.HSplitTop(LineSize, &Content, &InputsBlock);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcAutoMargin, BcLocalize("Auto margin"), &g_Config.m_BcAutoMargin, &Content, LineSize);

	LeftColumn.HSplitTop(MarginBetweenViews, nullptr, &LeftColumn);

	const bool SnapTapBlocked = GameClient()->m_SnapTap.IsBlockedByCommunity();
	const bool SnapTapExpanded = g_Config.m_BcSnapTap != 0;
	const float SnapTapHeaderHeight = LineSize + MarginSmall + LineSize;
	const float SnapTapExpandedTargetHeight = MarginSmall + LineSize;
	const float SnapTapExpandedHeight = BC_MODULE_REVEAL(SnapTapExpanded, SnapTapExpandedTargetHeight, Client()->RenderFrameTime());
	const float SnapTapBlockedHintHeight = SnapTapBlocked ? (MarginSmall + LineSize) : 0.0f;
	const float SnapTapBlockHeight = SnapTapHeaderHeight + SnapTapExpandedHeight + SnapTapBlockedHintHeight;

	CUIRect SnapTapBlock;
	LeftColumn.HSplitTop(SnapTapBlockHeight, &SnapTapBlock, &LeftColumn);

	CUIRect SnapTapBlockBg = SnapTapBlock;
	SnapTapBlockBg.w += BlockPadding;
	SnapTapBlockBg.h += BlockPadding;
	SnapTapBlockBg.x -= BlockPadding * 0.5f;
	SnapTapBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&SnapTapBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcSnapTap, &SnapTapBlockBg);

	SnapTapBlock.HSplitTop(LineSize, &Label, &SnapTapBlock);
	Ui()->DoLabel(&Label, BcLocalize("Snap Tap"), HeadlineFontSize, TEXTALIGN_ML);
	SnapTapBlock.HSplitTop(MarginSmall, nullptr, &SnapTapBlock);

	SnapTapBlock.HSplitTop(LineSize, &Content, &SnapTapBlock);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcSnapTap, BcLocalize("Enable"), &g_Config.m_BcSnapTap, &Content, LineSize);

	if(SnapTapExpandedHeight > 0.5f)
	{
		CUIRect Visible = SnapTapBlock;
		Visible.h = SnapTapExpandedHeight;
		Ui()->ClipEnable(&Visible);

		SnapTapBlock.HSplitTop(MarginSmall, nullptr, &SnapTapBlock);
		SnapTapBlock.HSplitTop(LineSize, &Button, &SnapTapBlock);

		const int Min = 0;
		const int Max = 200;
		int Value = std::clamp(g_Config.m_BcSnapTapDelay, Min, Max);
		const int Increment = std::max(1, (Max - Min) / 35);
		if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_UP) && Ui()->MouseInside(&Button))
			Value = std::clamp(Value + Increment, Min, Max);
		if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN) && Ui()->MouseInside(&Button))
			Value = std::clamp(Value - Increment, Min, Max);

		char aBuf[256];
		char aValueBuf[64];
		// bestclient
		if(BestClientUiTheme::IsNewScrollbar())
		{
			if(Value == 0)
				str_copy(aValueBuf, BcLocalize("Off"));
			else
				str_format(aValueBuf, sizeof(aValueBuf), "%dms", Value);
			str_copy(aBuf, BcLocalize("Delay"));
		}
		else if(Value == 0)
			str_format(aBuf, sizeof(aBuf), "%s: %s", BcLocalize("Delay"), BcLocalize("Off"));
		else
			str_format(aBuf, sizeof(aBuf), "%s: %dms", BcLocalize("Delay"), Value);
		// bestclient

		CUIRect DelayLabel, ScrollBar;
		Button.VSplitMid(&DelayLabel, &ScrollBar, std::min(10.0f, Button.w * 0.05f));
		Ui()->DoLabel(&DelayLabel, aBuf, DelayLabel.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_ML);

		const float Rel = (Value - Min) / (float)(Max - Min);
		// bestclient
		const float NewRel = Ui()->DoScrollbarH(&g_Config.m_BcSnapTapDelay, &ScrollBar, Rel, nullptr, BestClientUiTheme::IsNewScrollbar() ? aValueBuf : nullptr);
		// bestclient
		Value = (int)(Min + NewRel * (Max - Min) + 0.5f);
		g_Config.m_BcSnapTapDelay = std::clamp(Value, Min, Max);

		Ui()->ClipDisable();
	}

	if(SnapTapBlocked)
	{
		SnapTapBlock.HSplitTop(MarginSmall, nullptr, &SnapTapBlock);
		SnapTapBlock.HSplitTop(LineSize, &Label, &SnapTapBlock);
		TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
		Ui()->DoLabel(&Label, BcLocalize("Looks like you're on a server where this feature is forbidden"), FontSize, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}

	LeftColumn.HSplitTop(MarginBetweenViews, nullptr, &LeftColumn);

	const bool GoresModeExpanded = g_Config.m_BcGoresMode != 0;
	const float GoresModeHeaderHeight = LineSize + MarginSmall + LineSize;
	const float GoresModeExpandedTargetHeight = MarginSmall + LineSize;
	const float GoresModeExpandedHeight = BC_MODULE_REVEAL(GoresModeExpanded, GoresModeExpandedTargetHeight, Client()->RenderFrameTime());
	const float GoresModeBlockHeight = GoresModeHeaderHeight + GoresModeExpandedHeight;

	CUIRect GoresModeBlock;
	LeftColumn.HSplitTop(GoresModeBlockHeight, &GoresModeBlock, &LeftColumn);

	CUIRect GoresModeBlockBg = GoresModeBlock;
	GoresModeBlockBg.w += BlockPadding;
	GoresModeBlockBg.h += BlockPadding;
	GoresModeBlockBg.x -= BlockPadding * 0.5f;
	GoresModeBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&GoresModeBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcGoresMode, &GoresModeBlockBg);

	CUIRect GoresModeView = GoresModeBlock;
	GoresModeView.HSplitTop(LineSize, &Label, &GoresModeView);
	Ui()->DoLabel(&Label, BcLocalize("Gores mode"), HeadlineFontSize, TEXTALIGN_ML);
	GoresModeView.HSplitTop(MarginSmall, nullptr, &GoresModeView);

	GoresModeView.HSplitTop(LineSize, &Content, &GoresModeView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcGoresMode, BcLocalize("Enable gores mode"), &g_Config.m_BcGoresMode, &Content, LineSize);

	if(GoresModeExpandedHeight > 0.5f)
	{
		CUIRect Visible = GoresModeView;
		Visible.h = GoresModeExpandedHeight;
		Ui()->ClipEnable(&Visible);

		GoresModeView.HSplitTop(MarginSmall, nullptr, &GoresModeView);
		GoresModeView.HSplitTop(LineSize, &Content, &GoresModeView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcGoresModeDisableIfWeapons, BcLocalize("Disable if you have shotgun, grenade or laser"), &g_Config.m_BcGoresModeDisableIfWeapons, &Content, LineSize);

		Ui()->ClipDisable();
	}

	LeftColumn.HSplitTop(MarginBetweenViews, nullptr, &LeftColumn);

	const bool OptimizerExpanded = g_Config.m_BcOptimizer != 0;
	const bool OptimizerFpsFogExpanded = OptimizerExpanded && g_Config.m_BcOptimizerFpsFog != 0;
	const float OptimizerHeaderHeight = LineSize + MarginSmall + LineSize;
	const float OptimizerFpsFogTargetHeight = 4.0f * (MarginSmall + LineSize);
	const float OptimizerFpsFogExpandedHeight = BC_MODULE_REVEAL_DUR(OptimizerFpsFogExpanded, OptimizerFpsFogTargetHeight, Client()->RenderFrameTime(), 0.16f);
	const float OptimizerExpandedTargetHeight = 4.0f * (MarginSmall + LineSize);
	const float OptimizerExpandedHeight = BC_MODULE_REVEAL(OptimizerExpanded, OptimizerExpandedTargetHeight, Client()->RenderFrameTime()) + OptimizerFpsFogExpandedHeight;
	const float OptimizerBlockHeight = OptimizerHeaderHeight + OptimizerExpandedHeight;

	CUIRect OptimizerBlock;
	LeftColumn.HSplitTop(OptimizerBlockHeight, &OptimizerBlock, &LeftColumn);

	CUIRect OptimizerBlockBg = OptimizerBlock;
	OptimizerBlockBg.w += BlockPadding;
	OptimizerBlockBg.h += BlockPadding;
	OptimizerBlockBg.x -= BlockPadding * 0.5f;
	OptimizerBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&OptimizerBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcOptimizer, &OptimizerBlockBg);

	CUIRect MainLeft = OptimizerBlock;
	MainLeft.HSplitTop(LineSize, &Label, &MainLeft);
	Ui()->DoLabel(&Label, BcLocalize("Optimizer"), HeadlineFontSize, TEXTALIGN_ML);
	MainLeft.HSplitTop(MarginSmall, nullptr, &MainLeft);

	MainLeft.HSplitTop(LineSize, &Content, &MainLeft);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizer, BcLocalize("Enable optimizer"), &g_Config.m_BcOptimizer, &Content, LineSize);

	if(OptimizerExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainLeft;
		Visible.h = OptimizerExpandedHeight;
		Ui()->ClipEnable(&Visible);

		MainLeft.HSplitTop(MarginSmall, nullptr, &MainLeft);
		MainLeft.HSplitTop(LineSize, &Content, &MainLeft);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerDisableParticles, BcLocalize("Disable all particles render"), &g_Config.m_BcOptimizerDisableParticles, &Content, LineSize);

		MainLeft.HSplitTop(MarginSmall, nullptr, &MainLeft);
		MainLeft.HSplitTop(LineSize, &Content, &MainLeft);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerDisableQuads, BcLocalize("Disable map quads render"), &g_Config.m_BcOptimizerDisableQuads, &Content, LineSize);

		MainLeft.HSplitTop(MarginSmall, nullptr, &MainLeft);
		MainLeft.HSplitTop(LineSize, &Content, &MainLeft);
		if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerDisableHighDetail, BcLocalize("Disable high detail"), &g_Config.m_BcOptimizerDisableHighDetail, &Content, LineSize))
			GameClient()->m_Optimizer.OnOptimizerDisableHighDetailToggled();

		MainLeft.HSplitTop(MarginSmall, nullptr, &MainLeft);
		MainLeft.HSplitTop(LineSize, &Content, &MainLeft);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerFpsFog, BcLocalize("FPS fog (cull outside limit)"), &g_Config.m_BcOptimizerFpsFog, &Content, LineSize);

		if(OptimizerFpsFogExpandedHeight > 0.5f)
		{
			CUIRect FogVisible = MainLeft;
			FogVisible.h = OptimizerFpsFogExpandedHeight;
			Ui()->ClipEnable(&FogVisible);

			MainLeft.HSplitTop(MarginSmall, nullptr, &MainLeft);
			MainLeft.HSplitTop(LineSize, &Content, &MainLeft);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerFpsFogRenderRect, BcLocalize("Render FPS fog rectangle"), &g_Config.m_BcOptimizerFpsFogRenderRect, &Content, LineSize);

			MainLeft.HSplitTop(MarginSmall, nullptr, &MainLeft);
			MainLeft.HSplitTop(LineSize, &Content, &MainLeft);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerFpsFogCullMapTiles, BcLocalize("Cull map tiles outside FPS fog"), &g_Config.m_BcOptimizerFpsFogCullMapTiles, &Content, LineSize);

			MainLeft.HSplitTop(MarginSmall, nullptr, &MainLeft);
			MainLeft.HSplitTop(LineSize, &Button, &MainLeft);
			static CButtonContainer s_OptimizerFogModeRadius;
			static CButtonContainer s_OptimizerFogModeZoom;
			CUIRect RadiusButton, ZoomButton;
			Button.VSplitMid(&RadiusButton, &ZoomButton, 2.0f);
			RadiusButton.HMargin(2.0f, &RadiusButton);
			ZoomButton.HMargin(2.0f, &ZoomButton);
			if(DoButton_Menu(&s_OptimizerFogModeRadius, BcLocalize("Radius"), g_Config.m_BcOptimizerFpsFogMode == 0, &RadiusButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
				g_Config.m_BcOptimizerFpsFogMode = 0;
			if(DoButton_Menu(&s_OptimizerFogModeZoom, BcLocalize("Zoom %"), g_Config.m_BcOptimizerFpsFogMode == 1, &ZoomButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
				g_Config.m_BcOptimizerFpsFogMode = 1;

			MainLeft.HSplitTop(MarginSmall, nullptr, &MainLeft);
			MainLeft.HSplitTop(LineSize, &Button, &MainLeft);
			if(g_Config.m_BcOptimizerFpsFogMode == 0)
				Ui()->DoScrollbarOption(&g_Config.m_BcOptimizerFpsFogRadiusTiles, &g_Config.m_BcOptimizerFpsFogRadiusTiles, &Button, BcLocalize("Radius (tiles)"), 5, 300);
			else
				Ui()->DoScrollbarOption(&g_Config.m_BcOptimizerFpsFogZoomPercent, &g_Config.m_BcOptimizerFpsFogZoomPercent, &Button, BcLocalize("Visible area (%)"), 10, 120, &CUi::ms_LinearScrollbarScale, 0u, "%");

			Ui()->ClipDisable();
		}

		Ui()->ClipDisable();
	}

#if defined(CONF_FAMILY_WINDOWS)
	LeftColumn.HSplitTop(MarginBetweenViews, nullptr, &LeftColumn);

	const float PerformanceBlockHeight = LineSize + MarginSmall + LineSize + MarginSmall + LineSize;
	CUIRect PerformanceBlock;
	LeftColumn.HSplitTop(PerformanceBlockHeight, &PerformanceBlock, &LeftColumn);

	CUIRect PerformanceBlockBg = PerformanceBlock;
	PerformanceBlockBg.w += BlockPadding;
	PerformanceBlockBg.h += BlockPadding;
	PerformanceBlockBg.x -= BlockPadding * 0.5f;
	PerformanceBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&PerformanceBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcHighProcessPriority, &PerformanceBlockBg);

	MainLeft = PerformanceBlock;
	MainLeft.HSplitTop(LineSize, &Label, &MainLeft);
	Ui()->DoLabel(&Label, BcLocalize("Performance"), HeadlineFontSize, TEXTALIGN_ML);
	MainLeft.HSplitTop(MarginSmall, nullptr, &MainLeft);

	MainLeft.HSplitTop(LineSize, &Content, &MainLeft);
	if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcHighProcessPriority, BcLocalize("High DDNet Process Priority"), &g_Config.m_BcHighProcessPriority, &Content, LineSize))
		GameClient()->m_ProcessPriority.SetDDNetProcessPriority(g_Config.m_BcHighProcessPriority);

	MainLeft.HSplitTop(MarginSmall, nullptr, &MainLeft);
	MainLeft.HSplitTop(LineSize, &Content, &MainLeft);
	if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcDiscordNormalProcessPriority, BcLocalize("Lower Discords Process Priority"), &g_Config.m_BcDiscordNormalProcessPriority, &Content, LineSize))
	{
		if(g_Config.m_BcDiscordNormalProcessPriority)
			GameClient()->m_ProcessPriority.StartDiscordPriorityThread();
	}
#endif

	CUIRect RightColumn = RightView;
	RightColumn.HSplitTop(10.0f, nullptr, &RightColumn);

	const float CinematicStrengthHeight = g_Config.m_BcCinematicCamera ? LineSize : 0.0f;
	const float BetterSpectateDelayHeight = g_Config.m_BcBetterSpectate ? LineSize : 0.0f;
	const float SpecMovedNotifyTextHeight = g_Config.m_BcSpecMovedNotify ? LineSize : 0.0f;
	const float AutoLockDelayHeight = g_Config.m_BcAutoTeamLock ? LineSize : 0.0f;
#if defined(CONF_AUTOUPDATE)
	const float AutoUpdateHeight = LineSize;
#else
	const float AutoUpdateHeight = 0.0f;
#endif
	const float GameplayQoLBlockHeight =
		LineSize + MarginSmall +
		7.0f * LineSize +
		AutoUpdateHeight +
		CinematicStrengthHeight + BetterSpectateDelayHeight + SpecMovedNotifyTextHeight + AutoLockDelayHeight;
	CUIRect GameplayQoLBlock;
	RightColumn.HSplitTop(GameplayQoLBlockHeight, &GameplayQoLBlock, &RightColumn);

	CUIRect GameplayQoLBlockBg = GameplayQoLBlock;
	GameplayQoLBlockBg.w += BlockPadding;
	GameplayQoLBlockBg.h += BlockPadding;
	GameplayQoLBlockBg.x -= BlockPadding * 0.5f;
	GameplayQoLBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&GameplayQoLBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcAutoUpdate, &GameplayQoLBlockBg);

	GameplayQoLBlock.HSplitTop(LineSize, &Label, &GameplayQoLBlock);
	Ui()->DoLabel(&Label, BcLocalize("Gameplay QoL"), HeadlineFontSize, TEXTALIGN_ML);
	GameplayQoLBlock.HSplitTop(MarginSmall, nullptr, &GameplayQoLBlock);

#if defined(CONF_AUTOUPDATE)
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcAutoUpdate, BcLocalize("Automatic update"), &g_Config.m_BcAutoUpdate, &GameplayQoLBlock, LineSize);
#endif
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcCinematicCamera, BcLocalize("Cinematic camera"), &g_Config.m_BcCinematicCamera, &GameplayQoLBlock, LineSize);
	if(g_Config.m_BcCinematicCamera)
	{
		GameplayQoLBlock.HSplitTop(LineSize, &Button, &GameplayQoLBlock);
		Ui()->DoScrollbarOption(&g_Config.m_BcCinematicCameraStrength, &g_Config.m_BcCinematicCameraStrength, &Button, BcLocalize("Strength"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	}
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcExtendZoom, BcLocalize("Extend zoom (0.5 steps)"), &g_Config.m_BcExtendZoom, &GameplayQoLBlock, LineSize);
	{
		CUIRect BetterSpectateRow;
		GameplayQoLBlock.HSplitTop(LineSize, &BetterSpectateRow, &GameplayQoLBlock);
		BcMenuBadges::DrawEClient(Graphics(), Ui(), TextRender(), &BetterSpectateRow, MarginSmall);
		if(DoButton_CheckBox(&g_Config.m_BcBetterSpectate, BcLocalize("Better spectate"), g_Config.m_BcBetterSpectate, &BetterSpectateRow))
		{
			g_Config.m_BcBetterSpectate ^= 1;
			GameClient()->m_SpecPauseRadio.SyncSpectateBinds(g_Config.m_BcBetterSpectate != 0);
		}
		GameClient()->m_Tooltips.DoToolTip(&g_Config.m_BcBetterSpectate, &BetterSpectateRow, BcLocalize("Replace your say /pause bind (default: Q) with +specpause (pause/spec radio)"));
	}
	if(g_Config.m_BcBetterSpectate)
	{
		GameplayQoLBlock.HSplitTop(LineSize, &Button, &GameplayQoLBlock);
		Ui()->DoScrollbarOption(&g_Config.m_BcSpecPauseShowDelay, &g_Config.m_BcSpecPauseShowDelay, &Button, BcLocalize("Show delay"), 0, 1000, &CUi::ms_LinearScrollbarScale, 0, "ms");
	}
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcSpecMovedNotify, BcLocalize("Notify when moved in spec"), &g_Config.m_BcSpecMovedNotify, &GameplayQoLBlock, LineSize);
	if(g_Config.m_BcSpecMovedNotify)
	{
		static CLineInput s_SpecMovedNotifyTextInput;
		s_SpecMovedNotifyTextInput.SetBuffer(g_Config.m_BcSpecMovedNotifyText, sizeof(g_Config.m_BcSpecMovedNotifyText));
		s_SpecMovedNotifyTextInput.SetEmptyText("you moved in game");
		GameplayQoLBlock.HSplitTop(LineSize, &Button, &GameplayQoLBlock);
		CUIRect TextLabel, TextField;
		Button.VSplitMid(&TextLabel, &TextField, std::min(10.0f, Button.w * 0.05f));
		Ui()->DoLabel(&TextLabel, BcLocalize("Notification text"), 14.0f, TEXTALIGN_ML);
		Ui()->DoEditBox(&s_SpecMovedNotifyTextInput, &TextField, 14.0f);
	}
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcAutoTeamLock, BcLocalize("Lock team automatically after joining"), &g_Config.m_BcAutoTeamLock, &GameplayQoLBlock, LineSize);
	if(g_Config.m_BcAutoTeamLock)
	{
		GameplayQoLBlock.HSplitTop(LineSize, &Button, &GameplayQoLBlock);
		Ui()->DoScrollbarOption(&g_Config.m_BcAutoTeamLockDelay, &g_Config.m_BcAutoTeamLockDelay, &Button, BcLocalize("Auto lock delay"), 0, 30, &CUi::ms_LinearScrollbarScale, 0, "s");
	}
	{
		CUIRect ConfirmQuitRow;
		GameplayQoLBlock.HSplitTop(LineSize, &ConfirmQuitRow, &GameplayQoLBlock);
		if(DoButton_CheckBox(&g_Config.m_BcConfirmQuit, BcLocalize("Confirm before quitting"), g_Config.m_BcConfirmQuit, &ConfirmQuitRow))
			g_Config.m_BcConfirmQuit ^= 1;
	}
	{
		CUIRect EgoTilesAntiLagRow;
		GameplayQoLBlock.HSplitTop(LineSize, &EgoTilesAntiLagRow, &GameplayQoLBlock);
		BcMenuBadges::DrawBeta(Graphics(), Ui(), TextRender(), &EgoTilesAntiLagRow, MarginSmall);
		if(DoButton_CheckBox(&g_Config.m_TcEgoTilesAntiLag, BcLocalize("Moving/ego tiles prediction"), g_Config.m_TcEgoTilesAntiLag, &EgoTilesAntiLagRow))
			g_Config.m_TcEgoTilesAntiLag ^= 1;
	}

	RightColumn.HSplitTop(MarginBetweenViews, nullptr, &RightColumn);

	const bool FastActionsExpanded = g_Config.m_BcFastActions != 0;
	const float FastActionsHeaderHeight = LineSize + MarginSmall + LineSize;
	const float FastActionsExpandedTargetHeight = MarginSmall + WheelPreviewHeight + MarginSmall + LineSize + MarginSmall + LineSize + MarginSmall + LineSize + MarginSmall + LineSize + MarginSmall + LineSize * 0.8f + MarginSmall + LineSize;
	const float FastActionsExpandedHeight = BC_MODULE_REVEAL(FastActionsExpanded, FastActionsExpandedTargetHeight, Client()->RenderFrameTime());
	const float FastActionsBlockHeight = FastActionsHeaderHeight + FastActionsExpandedHeight;

	CUIRect FastActionsBlock;
	RightColumn.HSplitTop(FastActionsBlockHeight, &FastActionsBlock, &RightColumn);

	CUIRect FastActionsBlockBg = FastActionsBlock;
	FastActionsBlockBg.w += BlockPadding;
	FastActionsBlockBg.h += BlockPadding;
	FastActionsBlockBg.x -= BlockPadding * 0.5f;
	FastActionsBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&FastActionsBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcFastActions, &FastActionsBlockBg);

	MainView = FastActionsBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	Ui()->DoLabel(&Label, BcLocalize("Fast Actions"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcFastActions, BcLocalize("Enable Fast Actions"), &g_Config.m_BcFastActions, &Content, LineSize);

	if(FastActionsExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = FastActionsExpandedHeight;
		Ui()->ClipEnable(&Visible);

		static char s_aBindName[FAST_ACTIONS_MAX_NAME] = "";
		static char s_aBindCommand[FAST_ACTIONS_MAX_CMD] = "";
		static int s_SelectedBindIndex = 0;
		static int s_LastSelectedBindIndex = -1;

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		CUIRect WheelPreview;
		MainView.HSplitTop(WheelPreviewHeight, &WheelPreview, &MainView);
		const vec2 Center = WheelPreview.Center();
		const float LineInset = 18.0f;
		const float LineHalfWidth = std::max(40.0f, WheelPreview.w / 2.0f - LineInset);
		const float LineHeight = std::min(WheelPreview.h * 0.78f, 44.0f);
		const float SelectBandHalfHeight = LineHeight * 1.2f;
		const float LabelW = 52.0f;
		const float LabelH = 52.0f;
		const float TextHalfRange = std::max(0.0f, LineHalfWidth - LabelW / 2.0f - 2.0f);

		Graphics()->DrawRect(Center.x - LineHalfWidth, Center.y - LineHeight / 2.0f, LineHalfWidth * 2.0f, LineHeight, ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f), IGraphics::CORNER_ALL, 8.0f);

		int HoveringIndex = -1;
		const vec2 MouseDelta = Ui()->MousePos() - Center;
		const int SegmentCount = static_cast<int>(GameClient()->m_FastActions.m_vBinds.size());
		const auto IsLegacySlotName = [](const char *pName, int SlotIndex) {
			if(pName[0] == '\0')
				return false;
			char aSlotName[16];
			str_format(aSlotName, sizeof(aSlotName), "%d", SlotIndex + 1);
			return str_comp(pName, aSlotName) == 0;
		};
		const bool HoverInsideLine = absolute(MouseDelta.x) <= LineHalfWidth && absolute(MouseDelta.y) <= SelectBandHalfHeight;
		if(HoverInsideLine && SegmentCount > 0)
		{
			const float HoverPos01 = TextHalfRange > 0.0f ? (MouseDelta.x + TextHalfRange) / (2.0f * TextHalfRange) : 0.5f;
			HoveringIndex = std::clamp((int)std::round(HoverPos01 * (SegmentCount - 1)), 0, SegmentCount - 1);

			if(Ui()->MouseButtonClicked(0) || Ui()->MouseButtonClicked(2))
			{
				s_SelectedBindIndex = HoveringIndex;
				const CFastActions::CBind &Bind = GameClient()->m_FastActions.m_vBinds[HoveringIndex];
				if(IsLegacySlotName(Bind.m_aName, HoveringIndex))
					s_aBindName[0] = '\0';
				else
					str_copy(s_aBindName, Bind.m_aName);
				str_copy(s_aBindCommand, GameClient()->m_FastActions.m_vBinds[HoveringIndex].m_aCommand);
			}
		}

		s_SelectedBindIndex = std::clamp(s_SelectedBindIndex, 0, std::max(0, SegmentCount - 1));
		if(s_SelectedBindIndex != s_LastSelectedBindIndex &&
			s_SelectedBindIndex < static_cast<int>(GameClient()->m_FastActions.m_vBinds.size()))
		{
			const CFastActions::CBind &Bind = GameClient()->m_FastActions.m_vBinds[s_SelectedBindIndex];
			if(IsLegacySlotName(Bind.m_aName, s_SelectedBindIndex))
				s_aBindName[0] = '\0';
			else
				str_copy(s_aBindName, Bind.m_aName);
			str_copy(s_aBindCommand, GameClient()->m_FastActions.m_vBinds[s_SelectedBindIndex].m_aCommand);
			s_LastSelectedBindIndex = s_SelectedBindIndex;
		}

		for(int i = 0; i < static_cast<int>(GameClient()->m_FastActions.m_vBinds.size()); i++)
		{
			TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
			float SegmentFontSize = FontSize * 1.1f;
			if(i == s_SelectedBindIndex)
			{
				SegmentFontSize = FontSize * 1.7f;
				TextRender()->TextColor(ColorRGBA(0.5f, 1.0f, 0.75f, 1.0f));
			}
			else if(i == HoveringIndex)
			{
				SegmentFontSize = FontSize * 1.35f;
			}

			const float Pos01 = GameClient()->m_FastActions.m_vBinds.size() <= 1 ? 0.5f : (float)i / (float)(GameClient()->m_FastActions.m_vBinds.size() - 1);
			const vec2 Pos = vec2(Center.x - TextHalfRange + Pos01 * (TextHalfRange * 2.0f), Center.y);
			const CUIRect Rect = CUIRect{Pos.x - LabelW / 2.0f, Pos.y - LabelH / 2.0f, LabelW, LabelH};
			char aBindPreviewText[16];
			str_format(aBindPreviewText, sizeof(aBindPreviewText), "%d", i + 1);
			Ui()->DoLabel(&Rect, aBindPreviewText, SegmentFontSize, TEXTALIGN_MC);
		}
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		char aSlotLabel[64];
		str_format(aSlotLabel, sizeof(aSlotLabel), "%s %d", BcLocalize("Selected slot"), s_SelectedBindIndex + 1);
		Ui()->DoLabel(&Button, aSlotLabel, FontSize, TEXTALIGN_ML);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Button.VSplitLeft(150.0f, &Label, &Button);
		Ui()->DoLabel(&Label, BcLocalize("Name:"), FontSize, TEXTALIGN_ML);
		static CLineInput s_BindNameInput;
		s_BindNameInput.SetBuffer(s_aBindName, sizeof(s_aBindName));
		s_BindNameInput.SetEmptyText(BcLocalize("Name (optional)"));
		Ui()->DoEditBox(&s_BindNameInput, &Button, EditBoxFontSize);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Button.VSplitLeft(150.0f, &Label, &Button);
		Ui()->DoLabel(&Label, BcLocalize("Command:"), FontSize, TEXTALIGN_ML);
		static CLineInput s_BindCommandInput;
		s_BindCommandInput.SetBuffer(s_aBindCommand, sizeof(s_aBindCommand));
		s_BindCommandInput.SetEmptyText(BcLocalize("Command"));
		Ui()->DoEditBox(&s_BindCommandInput, &Button, EditBoxFontSize);

		if(s_SelectedBindIndex < static_cast<int>(GameClient()->m_FastActions.m_vBinds.size()))
		{
			str_copy(GameClient()->m_FastActions.m_vBinds[s_SelectedBindIndex].m_aName, s_aBindName);
			str_copy(GameClient()->m_FastActions.m_vBinds[s_SelectedBindIndex].m_aCommand, s_aBindCommand);
		}

		static CButtonContainer s_FastActionsClearButton;
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		if(DoButton_Menu(&s_FastActionsClearButton, BcLocalize("Clear command"), 0, &Button) &&
			s_SelectedBindIndex < static_cast<int>(GameClient()->m_FastActions.m_vBinds.size()))
		{
			GameClient()->m_FastActions.m_vBinds[s_SelectedBindIndex].m_aCommand[0] = '\0';
			s_aBindCommand[0] = '\0';
		}

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize * 0.8f, &Label, &MainView);
		Ui()->DoLabel(&Label, BcLocalize("In game: hold bind key, press 1..6, release key to execute"), FontSize * 0.8f, TEXTALIGN_ML);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Label, &MainView);
		static CButtonContainer s_FastActionsReaderButton;
		static CButtonContainer s_FastActionsClearKeyButton;
		DoLine_KeyReader(Label, s_FastActionsReaderButton, s_FastActionsClearKeyButton, BcLocalize("Fast Actions key"), "+fa");

		Ui()->ClipDisable();
	}

	RightColumn.HSplitTop(MarginBetweenViews, nullptr, &RightColumn);

	const bool FinishPredictionEnabled = g_Config.m_BcFinishPrediction != 0;
	const bool FinishPredictionShowTimeOptions = FinishPredictionEnabled && g_Config.m_BcFinishPredictionShowTime != 0;
	const float FinishPredictionTimeExpandedHeight = BC_MODULE_REVEAL(FinishPredictionShowTimeOptions, (MarginSmall + LineSize) * 2.0f, Client()->RenderFrameTime());
	const float FinishPredictionExpandedTargetHeight = (MarginSmall + LineSize) + FinishPredictionTimeExpandedHeight + (MarginSmall + LineSize) * 3.0f;
	const float FinishPredictionExpandedHeight = BC_MODULE_REVEAL(FinishPredictionEnabled, FinishPredictionExpandedTargetHeight, Client()->RenderFrameTime());
	const float FinishPredictionBlockHeight = LineSize + MarginSmall + LineSize + FinishPredictionExpandedHeight;
	CUIRect FinishPredictionBlock;
	RightColumn.HSplitTop(FinishPredictionBlockHeight, &FinishPredictionBlock, &RightColumn);

	CUIRect FinishPredictionBlockBg = FinishPredictionBlock;
	FinishPredictionBlockBg.w += BlockPadding;
	FinishPredictionBlockBg.h += BlockPadding;
	FinishPredictionBlockBg.x -= BlockPadding * 0.5f;
	FinishPredictionBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&FinishPredictionBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcFinishPrediction, &FinishPredictionBlockBg);

	CUIRect FinishPredictionView = FinishPredictionBlock;
	FinishPredictionView.HSplitTop(LineSize, &Label, &FinishPredictionView);
	CUIRect FinishPredictionTitleLabel, FinishPredictionHudEditorButton;
	Label.VSplitRight(LineSize + 8.0f, &FinishPredictionTitleLabel, &FinishPredictionHudEditorButton);
	static CButtonContainer s_FinishPredictionHudEditorButton;
	const bool FinishPredictionCanOpenHudEditor = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
	const bool FinishPredictionHudEditorClicked = Ui()->DoButton_FontIcon(&s_FinishPredictionHudEditorButton, FontIcon::UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, FinishPredictionCanOpenHudEditor ? 0 : -1, &FinishPredictionHudEditorButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_FinishPredictionHudEditorButton, &FinishPredictionHudEditorButton, FinishPredictionCanOpenHudEditor ? BcLocalize("Open in HUD editor") : BcLocalize("Join a game first"));
	GameClient()->m_Tooltips.SetFadeTime(&s_FinishPredictionHudEditorButton, 0.0f);
	if(FinishPredictionHudEditorClicked && FinishPredictionCanOpenHudEditor)
	{
		SetActive(false);
		GameClient()->m_HudEditor.Activate();
	}
	Ui()->DoLabel(&FinishPredictionTitleLabel, BcLocalize("Finish Prediction"), HeadlineFontSize, TEXTALIGN_ML);
	FinishPredictionView.HSplitTop(MarginSmall, nullptr, &FinishPredictionView);

	FinishPredictionView.HSplitTop(LineSize, &Content, &FinishPredictionView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcFinishPrediction, BcLocalize("Enable finish prediction"), &g_Config.m_BcFinishPrediction, &Content, LineSize);
	HudLayout::SetEnabled(HudLayout::MODULE_FINISH_PREDICTION, g_Config.m_BcFinishPrediction != 0);

	if(FinishPredictionExpandedHeight > 0.5f)
	{
		CUIRect Visible = FinishPredictionView;
		Visible.h = FinishPredictionExpandedHeight;
		Ui()->ClipEnable(&Visible);

		FinishPredictionView.HSplitTop(MarginSmall, nullptr, &FinishPredictionView);
		FinishPredictionView.HSplitTop(LineSize, &Content, &FinishPredictionView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcFinishPredictionShowTime, BcLocalize("Show time"), &g_Config.m_BcFinishPredictionShowTime, &Content, LineSize);

		if(FinishPredictionTimeExpandedHeight > 0.5f)
		{
			CUIRect TimeVisible = FinishPredictionView;
			TimeVisible.h = FinishPredictionTimeExpandedHeight;
			Ui()->ClipEnable(&TimeVisible);

			FinishPredictionView.HSplitTop(MarginSmall, nullptr, &FinishPredictionView);
			FinishPredictionView.HSplitTop(LineSize, &Button, &FinishPredictionView);
			static CButtonContainer s_FinishPredictionRemainingButton;
			static CButtonContainer s_FinishPredictionFinishTimeButton;
			CUIRect Left, Right;
			Button.VSplitMid(&Left, &Right);
			if(DoButton_Menu(&s_FinishPredictionRemainingButton, BcLocalize("Time left"), g_Config.m_BcFinishPredictionTimeMode == 0, &Left, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
				g_Config.m_BcFinishPredictionTimeMode = 0;
			if(DoButton_Menu(&s_FinishPredictionFinishTimeButton, BcLocalize("Finish time"), g_Config.m_BcFinishPredictionTimeMode == 1, &Right, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
				g_Config.m_BcFinishPredictionTimeMode = 1;

			FinishPredictionView.HSplitTop(MarginSmall, nullptr, &FinishPredictionView);
			FinishPredictionView.HSplitTop(LineSize, &Content, &FinishPredictionView);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcFinishPredictionShowMillis, BcLocalize("Show milliseconds"), &g_Config.m_BcFinishPredictionShowMillis, &Content, LineSize);

			Ui()->ClipDisable();
		}

		FinishPredictionView.HSplitTop(MarginSmall, nullptr, &FinishPredictionView);
		FinishPredictionView.HSplitTop(LineSize, &Content, &FinishPredictionView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcFinishPredictionShowPercentage, BcLocalize("Show percentage"), &g_Config.m_BcFinishPredictionShowPercentage, &Content, LineSize);

		FinishPredictionView.HSplitTop(MarginSmall, nullptr, &FinishPredictionView);
		FinishPredictionView.HSplitTop(LineSize, &Content, &FinishPredictionView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcFinishPredictionShowAlways, BcLocalize("Show always"), &g_Config.m_BcFinishPredictionShowAlways, &Content, LineSize);

		FinishPredictionView.HSplitTop(MarginSmall, nullptr, &FinishPredictionView);
		FinishPredictionView.HSplitTop(LineSize, &Content, &FinishPredictionView);
		BcMenuBadges::DrawBeta(Graphics(), Ui(), TextRender(), &Content, MarginSmall);
		const int PrevConsiderFreezeWalls = g_Config.m_BcFinishPredictionConsiderFreezeWalls;
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcFinishPredictionConsiderFreezeWalls, BcLocalize("Consider freeze walls"), &g_Config.m_BcFinishPredictionConsiderFreezeWalls, &Content, LineSize);
		if(PrevConsiderFreezeWalls != g_Config.m_BcFinishPredictionConsiderFreezeWalls)
			GameClient()->m_FinishPrediction.RequestPathRebuild();

		Ui()->ClipDisable();
	}

	RightColumn.HSplitTop(MarginBetweenViews, nullptr, &RightColumn);

	const float EdgeInfoColorPickerLineSize = 25.0f;
	const float EdgeInfoBlockHeight = LineSize + MarginSmall + LineSize + MarginSmall + 2.0f * (LineSize + MarginSmall) + 3.0f * (EdgeInfoColorPickerLineSize + MarginSmall);
	CUIRect EdgeInfoBlock;
	RightColumn.HSplitTop(EdgeInfoBlockHeight, &EdgeInfoBlock, &RightColumn);

	CUIRect EdgeInfoBlockBg = EdgeInfoBlock;
	EdgeInfoBlockBg.w += BlockPadding;
	EdgeInfoBlockBg.h += BlockPadding;
	EdgeInfoBlockBg.x -= BlockPadding * 0.5f;
	EdgeInfoBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&EdgeInfoBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_RiEdgeInfoCords, &EdgeInfoBlockBg);

	EdgeInfoBlock.HSplitTop(LineSize, &Label, &EdgeInfoBlock);
	CUIRect EdgeInfoTitleLabel, EdgeInfoHudEditorButton;
	Label.VSplitRight(LineSize + 8.0f, &EdgeInfoTitleLabel, &EdgeInfoHudEditorButton);
	static CButtonContainer s_EdgeInfoHudEditorButton;
	const bool EdgeInfoCanOpenHudEditor = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
	const bool EdgeInfoHudEditorClicked = Ui()->DoButton_FontIcon(&s_EdgeInfoHudEditorButton, FontIcon::UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, EdgeInfoCanOpenHudEditor ? 0 : -1, &EdgeInfoHudEditorButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_EdgeInfoHudEditorButton, &EdgeInfoHudEditorButton, EdgeInfoCanOpenHudEditor ? BcLocalize("Open in HUD editor") : BcLocalize("Join a game first"));
	GameClient()->m_Tooltips.SetFadeTime(&s_EdgeInfoHudEditorButton, 0.0f);
	if(EdgeInfoHudEditorClicked && EdgeInfoCanOpenHudEditor)
	{
		SetActive(false);
		GameClient()->m_HudEditor.Activate();
	}
	Ui()->DoLabel(&EdgeInfoTitleLabel, BcLocalize("Edge Info"), HeadlineFontSize, TEXTALIGN_ML);
	EdgeInfoBlock.HSplitTop(MarginSmall, nullptr, &EdgeInfoBlock);

	EdgeInfoBlock.HSplitTop(LineSize, &Label, &EdgeInfoBlock);
	static CButtonContainer s_EdgeInfoBindReader;
	static CButtonContainer s_EdgeInfoBindClear;
	DoLine_KeyReader(Label, s_EdgeInfoBindReader, s_EdgeInfoBindClear, BcLocalize("Show edge info"), "ri_toggle_edgeinfo");

	EdgeInfoBlock.HSplitTop(MarginSmall, nullptr, &EdgeInfoBlock);
	EdgeInfoBlock.HSplitTop(LineSize, &Content, &EdgeInfoBlock);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiEdgeInfoCords, BcLocalize("Show edge info about freeze"), &g_Config.m_RiEdgeInfoCords, &Content, LineSize);

	EdgeInfoBlock.HSplitTop(MarginSmall, nullptr, &EdgeInfoBlock);
	EdgeInfoBlock.HSplitTop(LineSize, &Content, &EdgeInfoBlock);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiEdgeInfoJump, BcLocalize("Show edge info about jumps"), &g_Config.m_RiEdgeInfoJump, &Content, LineSize);

	static CButtonContainer s_EdgeInfoFreezeColorButton;
	static CButtonContainer s_EdgeInfoKillColorButton;
	static CButtonContainer s_EdgeInfoSafeColorButton;
	EdgeInfoBlock.HSplitTop(MarginSmall, nullptr, &EdgeInfoBlock);
	DoLine_ColorPicker(&s_EdgeInfoFreezeColorButton, EdgeInfoColorPickerLineSize, 13.0f, MarginSmall, &EdgeInfoBlock, BcLocalize("Color when over freeze"), &g_Config.m_RiEdgeInfoColorFreeze, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::RiEdgeInfoColorFreeze)), false);
	DoLine_ColorPicker(&s_EdgeInfoKillColorButton, EdgeInfoColorPickerLineSize, 13.0f, MarginSmall, &EdgeInfoBlock, BcLocalize("Color when over kill"), &g_Config.m_RiEdgeInfoColorKill, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::RiEdgeInfoColorKill)), false);
	DoLine_ColorPicker(&s_EdgeInfoSafeColorButton, EdgeInfoColorPickerLineSize, 13.0f, MarginSmall, &EdgeInfoBlock, BcLocalize("Color when falling safely"), &g_Config.m_RiEdgeInfoColorSafe, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::RiEdgeInfoColorSafe)), false);

	const float LeftColumnEndY = LeftColumn.y;
	const float RightColumnEndY = RightColumn.y;
	CUIRect GameplayScrollContentRect;
	GameplayScrollContentRect.x = GameplayScrollContentX;
	GameplayScrollContentRect.y = std::max(LeftColumnEndY, RightColumnEndY) + MarginSmall * 2.0f;
	GameplayScrollContentRect.w = GameplayScrollContentW;
	GameplayScrollContentRect.h = 0.0f;
	s_GameplayScrollRegion.AddRect(GameplayScrollContentRect);
	s_GameplayScrollRegion.End();
}

void CMenus::RenderSettingsBestClientOthers(CUIRect MainView)
{
	const float LineSize = 20.0f;
	const float MarginSmall = 5.0f;
	const float HeadlineFontSize = 20.0f;
	const float EditBoxFontSize = 12.0f;
	const float MarginBetweenViews = 30.0f;
	const float BlockPadding = MarginBetweenViews * 0.6666f;

	static CScrollRegion s_OthersScrollRegion;
	CScrollRegionParams OthersScrollParams;
	OthersScrollParams.m_ScrollUnit = 60.0f;
	OthersScrollParams.m_ScrollbarMargin = 5.0f;
	s_OthersScrollRegion.Begin(&MainView, &OthersScrollParams);
	MainView.VSplitRight(5.0f, &MainView, nullptr);
	MainView.VSplitLeft(5.0f, nullptr, &MainView);
	const float OthersScrollContentX = MainView.x;
	const float OthersScrollContentW = MainView.w;

	CUIRect Content, Label, Button, LeftView, RightView, Column;

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	Column = LeftView;
	CUIRect RightColumn = RightView;
	Column.HSplitTop(10.0f, nullptr, &Column);
	RightColumn.HSplitTop(10.0f, nullptr, &RightColumn);

	const float ChatQoLBlockHeight = LineSize + MarginSmall + 5.0f * LineSize;
	CUIRect ChatQoLBlock;
	Column.HSplitTop(ChatQoLBlockHeight, &ChatQoLBlock, &Column);

	CUIRect ChatQoLBlockBg = ChatQoLBlock;
	ChatQoLBlockBg.w += BlockPadding;
	ChatQoLBlockBg.h += BlockPadding;
	ChatQoLBlockBg.x -= BlockPadding * 0.5f;
	ChatQoLBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&ChatQoLBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ClChatReset, &ChatQoLBlockBg);

	ChatQoLBlock.HSplitTop(LineSize, &Label, &ChatQoLBlock);
	Ui()->DoLabel(&Label, BcLocalize("Chat QoL"), HeadlineFontSize, TEXTALIGN_ML);
	ChatQoLBlock.HSplitTop(MarginSmall, nullptr, &ChatQoLBlock);

	{
		CUIRect SaveDraftRow;
		ChatQoLBlock.HSplitTop(LineSize, &SaveDraftRow, &ChatQoLBlock);
		if(DoButton_CheckBox(&g_Config.m_ClChatReset, BcLocalize("Save unsent messages"), g_Config.m_ClChatReset == 0, &SaveDraftRow))
			g_Config.m_ClChatReset ^= 1;
	}
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcSilentTyping, BcLocalize("Silent typing"), &g_Config.m_BcSilentTyping, &ChatQoLBlock, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatAltCommandLayout, BcLocalize("Commands in other layout"), &g_Config.m_BcChatAltCommandLayout, &ChatQoLBlock, LineSize);
	{
		CUIRect MessageActionsRow;
		ChatQoLBlock.HSplitTop(LineSize, &MessageActionsRow, &ChatQoLBlock);
		BcMenuBadges::DrawNew(Graphics(), Ui(), TextRender(), &MessageActionsRow, MarginSmall);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatMessageActions, BcLocalize("Reply/copy message menu"), &g_Config.m_BcChatMessageActions, &MessageActionsRow, LineSize);
	}
	{
		CUIRect NotifySavesRow;
		ChatQoLBlock.HSplitTop(LineSize, &NotifySavesRow, &ChatQoLBlock);
		BcMenuBadges::DrawNew(Graphics(), Ui(), TextRender(), &NotifySavesRow, MarginSmall);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcNotifySavesOnMap, BcLocalize("Notify about saves on map"), &g_Config.m_BcNotifySavesOnMap, &NotifySavesRow, LineSize);
	}

	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	const float TwitchLogLineSize = 14.0f;
	const float TwitchChatBlockHeight = LineSize + MarginSmall + LineSize + MarginSmall + LineSize;

	CUIRect TwitchChatBlock;
	Column.HSplitTop(TwitchChatBlockHeight, &TwitchChatBlock, &Column);

	CUIRect TwitchChatBlockBg = TwitchChatBlock;
	TwitchChatBlockBg.w += BlockPadding;
	TwitchChatBlockBg.h += BlockPadding;
	TwitchChatBlockBg.x -= BlockPadding * 0.5f;
	TwitchChatBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&TwitchChatBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcTwitchChatNick, &TwitchChatBlockBg);

	TwitchChatBlock.HSplitTop(LineSize, &Label, &TwitchChatBlock);
	CUIRect TwitchTitleLabel = Label;
	TwitchTitleLabel.VSplitRight(MarginSmall, &TwitchTitleLabel, nullptr);
	Ui()->DoLabel(&TwitchTitleLabel, BcLocalize("Twitch Chat"), HeadlineFontSize, TEXTALIGN_ML);
	TwitchChatBlock.HSplitTop(MarginSmall, nullptr, &TwitchChatBlock);

	TwitchChatBlock.HSplitTop(LineSize, &Button, &TwitchChatBlock);
	static CLineInput s_TwitchChatNickInput;
	s_TwitchChatNickInput.SetBuffer(g_Config.m_BcTwitchChatNick, sizeof(g_Config.m_BcTwitchChatNick));
	s_TwitchChatNickInput.SetEmptyText("channel");
	CUIRect TwitchNickLabel, TwitchNickField;
	Button.VSplitLeft(std::min(70.0f, Button.w * 0.28f), &TwitchNickLabel, &TwitchNickField);
	Ui()->DoLabel(&TwitchNickLabel, BcLocalize("Nick"), 14.0f, TEXTALIGN_ML);
	Ui()->DoClearableEditBox(&s_TwitchChatNickInput, &TwitchNickField, 14.0f);
	TwitchChatBlock.HSplitTop(MarginSmall, nullptr, &TwitchChatBlock);

	TwitchChatBlock.HSplitTop(LineSize, &Button, &TwitchChatBlock);
	CUIRect TwitchLogRect, TwitchStartButton;
	Button.VSplitRight(100.0f, &TwitchLogRect, &TwitchStartButton);
	TwitchLogRect.VSplitRight(MarginSmall, &TwitchLogRect, nullptr);
	static CButtonContainer s_TwitchStartButton;
	const bool TwitchActive = GameClient()->m_TwitchChat.IsActive();
	if(DoButton_Menu(&s_TwitchStartButton, TwitchActive ? BcLocalize("Stop") : BcLocalize("Start"), 0, &TwitchStartButton))
	{
		if(TwitchActive)
			GameClient()->m_TwitchChat.Stop();
		else
			GameClient()->m_TwitchChat.Start();
	}
	char aTwitchStatus[128];
	GameClient()->m_TwitchChat.GetStatusText(aTwitchStatus, sizeof(aTwitchStatus));
	Ui()->DoLabel(&TwitchLogRect, aTwitchStatus, TwitchLogLineSize, TEXTALIGN_ML);

	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	const float BrowserUtilsBlockHeight = 6.0f * LineSize + 5.0f * MarginSmall;

	CUIRect BrowserUtilsBlock;
	Column.HSplitTop(BrowserUtilsBlockHeight, &BrowserUtilsBlock, &Column);

	CUIRect BrowserUtilsBlockBg = BrowserUtilsBlock;
	BrowserUtilsBlockBg.w += BlockPadding;
	BrowserUtilsBlockBg.h += BlockPadding;
	BrowserUtilsBlockBg.x -= BlockPadding * 0.5f;
	BrowserUtilsBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&BrowserUtilsBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcAutoServerListRefresh, &BrowserUtilsBlockBg);

	BrowserUtilsBlock.HSplitTop(LineSize, &Label, &BrowserUtilsBlock);
	Ui()->DoLabel(&Label, BcLocalize("Browser Utils"), HeadlineFontSize, TEXTALIGN_ML);
	BrowserUtilsBlock.HSplitTop(MarginSmall, nullptr, &BrowserUtilsBlock);

	BrowserUtilsBlock.HSplitTop(LineSize, &Content, &BrowserUtilsBlock);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcAutoServerListRefresh, BcLocalize("Auto server list refresh"), &g_Config.m_BcAutoServerListRefresh, &Content, LineSize);

	BrowserUtilsBlock.HSplitTop(MarginSmall, nullptr, &BrowserUtilsBlock);
	BrowserUtilsBlock.HSplitTop(LineSize, &Button, &BrowserUtilsBlock);
	Ui()->DoScrollbarOption(&g_Config.m_BcAutoServerListRefreshSeconds, &g_Config.m_BcAutoServerListRefreshSeconds, &Button, BcLocalize("Seconds"), 1, 300, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, " s");

	BrowserUtilsBlock.HSplitTop(MarginSmall, nullptr, &BrowserUtilsBlock);
	BrowserUtilsBlock.HSplitTop(LineSize, &Content, &BrowserUtilsBlock);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcUseShortKogServerName, BcLocalize("Short EGO/KoG server name"), &g_Config.m_BcUseShortKogServerName, &Content, LineSize);

	BrowserUtilsBlock.HSplitTop(MarginSmall, nullptr, &BrowserUtilsBlock);
	BrowserUtilsBlock.HSplitTop(LineSize, &Content, &BrowserUtilsBlock);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcShowFinishedMapOnEgo, BcLocalize("Show finished map on EGO"), &g_Config.m_BcShowFinishedMapOnEgo, &Content, LineSize);

	BrowserUtilsBlock.HSplitTop(MarginSmall, nullptr, &BrowserUtilsBlock);
	BrowserUtilsBlock.HSplitTop(LineSize, &Content, &BrowserUtilsBlock);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMastersrv, BcLocalize("Use BestClient MasterServer"), &g_Config.m_BcMastersrv, &Content, LineSize);

	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	const bool RollbackDemoExpanded = g_Config.m_ClReplays != 0;
	const float RollbackDemoHeaderHeight = LineSize + MarginSmall + LineSize;
	const float RollbackDemoExpandedTargetHeight = MarginSmall + LineSize + MarginSmall + LineSize;
	const float RollbackDemoExpandedHeight = BC_MODULE_REVEAL(RollbackDemoExpanded, RollbackDemoExpandedTargetHeight, Client()->RenderFrameTime());
	const float RollbackDemoBlockHeight = RollbackDemoHeaderHeight + RollbackDemoExpandedHeight;

	CUIRect RollbackDemoBlock;
	Column.HSplitTop(RollbackDemoBlockHeight, &RollbackDemoBlock, &Column);

	CUIRect RollbackDemoBlockBg = RollbackDemoBlock;
	RollbackDemoBlockBg.w += BlockPadding;
	RollbackDemoBlockBg.h += BlockPadding;
	RollbackDemoBlockBg.x -= BlockPadding * 0.5f;
	RollbackDemoBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&RollbackDemoBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ClReplays, &RollbackDemoBlockBg);

	{
		CUIRect RollbackMain = RollbackDemoBlock;
		RollbackMain.HSplitTop(LineSize, &Label, &RollbackMain);
		Label.VSplitRight(MarginSmall, &Label, nullptr);
		BcMenuBadges::DrawNew(Graphics(), Ui(), TextRender(), &Label, 4.0f);
		Ui()->DoLabel(&Label, BcLocalize("Rollback Demo"), HeadlineFontSize, TEXTALIGN_ML);
		RollbackMain.HSplitTop(MarginSmall, nullptr, &RollbackMain);

		if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClReplays, BcLocalize("Enable rollback demo recording"), &g_Config.m_ClReplays, &RollbackMain, LineSize))
		{
			if(Client()->State() == IClient::STATE_ONLINE)
				Client()->DemoRecorder_UpdateReplayRecorder();
		}

		if(RollbackDemoExpandedHeight > 0.5f)
		{
			CUIRect Visible;
			RollbackMain.HSplitTop(RollbackDemoExpandedHeight, &Visible, &RollbackMain);
			Ui()->ClipEnable(&Visible);

			CUIRect Expand = {Visible.x, Visible.y, Visible.w, RollbackDemoExpandedTargetHeight};
			Expand.HSplitTop(MarginSmall, nullptr, &Expand);

			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_ClReplayLength, &g_Config.m_ClReplayLength, &Button, BcLocalize("Rollback length"), 10, 300, &CUi::ms_LinearScrollbarScale, 0, "s");

			Expand.HSplitTop(MarginSmall, nullptr, &Expand);
			Expand.HSplitTop(LineSize, &Label, &Expand);
			static CButtonContainer s_RollbackBindReader;
			static CButtonContainer s_RollbackBindClear;
			DoLine_KeyReader(Label, s_RollbackBindReader, s_RollbackBindClear, BcLocalize("Rollback bind"), "BC_save_rollback");

			Ui()->ClipDisable();
		}
	}

	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	const bool FocusModeEnabled = g_Config.m_ClFocusMode != 0;
	const bool FocusModeAutoReplyEnabled = g_Config.m_ClFocusModeAutoReply != 0;
	const float FocusModeHeaderHeight = LineSize + MarginSmall + LineSize;
	const float FocusModeExpandedTargetHeight = (9.0f + (FocusModeAutoReplyEnabled ? 1.0f : 0.0f)) * (MarginSmall + LineSize);
	const float FocusModeExpandedHeight = BC_MODULE_REVEAL(FocusModeEnabled, FocusModeExpandedTargetHeight, Client()->RenderFrameTime());
	const float FocusModeBlockHeight = FocusModeHeaderHeight + FocusModeExpandedHeight;

	CUIRect FocusModeBlock;
	Column.HSplitTop(FocusModeBlockHeight, &FocusModeBlock, &Column);

	CUIRect FocusModeBlockBg = FocusModeBlock;
	FocusModeBlockBg.w += BlockPadding;
	FocusModeBlockBg.h += BlockPadding;
	FocusModeBlockBg.x -= BlockPadding * 0.5f;
	FocusModeBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&FocusModeBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ClFocusMode, &FocusModeBlockBg);

	{
		CUIRect FocusMain = FocusModeBlock;
		FocusMain.HSplitTop(LineSize, &Label, &FocusMain);
		Ui()->DoLabel(&Label, BcLocalize("Focus Mode"), HeadlineFontSize, TEXTALIGN_ML);
		FocusMain.HSplitTop(MarginSmall, nullptr, &FocusMain);

		FocusMain.HSplitTop(LineSize, &Content, &FocusMain);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusMode, BcLocalize("Enable Focus Mode"), &g_Config.m_ClFocusMode, &Content, LineSize);

		if(FocusModeExpandedHeight > 0.5f)
		{
			CUIRect Visible = FocusMain;
			Visible.h = FocusModeExpandedHeight;
			Ui()->ClipEnable(&Visible);

			FocusMain.HSplitTop(MarginSmall, nullptr, &FocusMain);
			FocusMain.HSplitTop(LineSize, &Content, &FocusMain);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideNames, BcLocalize("Hide Player Names"), &g_Config.m_ClFocusModeHideNames, &Content, LineSize);

			FocusMain.HSplitTop(MarginSmall, nullptr, &FocusMain);
			FocusMain.HSplitTop(LineSize, &Content, &FocusMain);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideEffects, BcLocalize("Hide Visual Effects"), &g_Config.m_ClFocusModeHideEffects, &Content, LineSize);

			FocusMain.HSplitTop(MarginSmall, nullptr, &FocusMain);
			FocusMain.HSplitTop(LineSize, &Content, &FocusMain);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideHud, BcLocalize("Hide HUD"), &g_Config.m_ClFocusModeHideHud, &Content, LineSize);

			FocusMain.HSplitTop(MarginSmall, nullptr, &FocusMain);
			FocusMain.HSplitTop(LineSize, &Content, &FocusMain);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideSongPlayer, BcLocalize("Hide Song Player"), &g_Config.m_ClFocusModeHideSongPlayer, &Content, LineSize);

			FocusMain.HSplitTop(MarginSmall, nullptr, &FocusMain);
			FocusMain.HSplitTop(LineSize, &Content, &FocusMain);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideUI, BcLocalize("Hide Unnecessary UI"), &g_Config.m_ClFocusModeHideUI, &Content, LineSize);

			FocusMain.HSplitTop(MarginSmall, nullptr, &FocusMain);
			FocusMain.HSplitTop(LineSize, &Content, &FocusMain);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideChat, BcLocalize("Hide Chat"), &g_Config.m_ClFocusModeHideChat, &Content, LineSize);

			FocusMain.HSplitTop(MarginSmall, nullptr, &FocusMain);
			FocusMain.HSplitTop(LineSize, &Content, &FocusMain);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideScoreboard, BcLocalize("Hide Scoreboard"), &g_Config.m_ClFocusModeHideScoreboard, &Content, LineSize);

			FocusMain.HSplitTop(MarginSmall, nullptr, &FocusMain);
			FocusMain.HSplitTop(LineSize, &Content, &FocusMain);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeAutoReply, BcLocalize("Auto reply when chat is hidden"), &g_Config.m_ClFocusModeAutoReply, &Content, LineSize);

			if(FocusModeAutoReplyEnabled)
			{
				FocusMain.HSplitTop(MarginSmall, nullptr, &FocusMain);
				FocusMain.HSplitTop(LineSize, &Content, &FocusMain);
				static CLineInput s_FocusModeAutoReplyMessage(g_Config.m_ClFocusModeAutoReplyMessage, sizeof(g_Config.m_ClFocusModeAutoReplyMessage));
				s_FocusModeAutoReplyMessage.SetEmptyText("my chat is hidden");
				Ui()->DoEditBox(&s_FocusModeAutoReplyMessage, &Content, EditBoxFontSize);
			}

			FocusMain.HSplitTop(MarginSmall, nullptr, &FocusMain);
			FocusMain.HSplitTop(LineSize, &Label, &FocusMain);
			static CButtonContainer s_FocusModeBindReader;
			static CButtonContainer s_FocusModeBindClear;
			DoLine_KeyReader(Label, s_FocusModeBindReader, s_FocusModeBindClear, BcLocalize("Focus mode bind"), "toggle p_focus_mode 0 1");

			Ui()->ClipDisable();
		}
	}

	const bool VoiceExpanded = g_Config.m_BcVoiceChatEnable != 0;
	static float s_VoiceRevealPhase = 0.0f;
	BCUiAnimations::UpdateModuleRevealPhase(s_VoiceRevealPhase, VoiceExpanded, Client()->RenderFrameTime());
	const float VoiceSettingsBlockHeight = GameClient()->m_VoiceChat.GetMenuSettingsBlockHeight(s_VoiceRevealPhase) + LineSize + MarginSmall;

	CUIRect VoiceSettingsBlock;
	RightColumn.HSplitTop(VoiceSettingsBlockHeight, &VoiceSettingsBlock, &RightColumn);
	CUIRect VoiceSettingsBlockBg = VoiceSettingsBlock;
	VoiceSettingsBlockBg.w += BlockPadding;
	VoiceSettingsBlockBg.h += BlockPadding;
	VoiceSettingsBlockBg.x -= BlockPadding * 0.5f;
	VoiceSettingsBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&VoiceSettingsBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcVoiceChatEnable, &VoiceSettingsBlockBg);

	CUIRect VoiceSettingsContent = VoiceSettingsBlock;
	VoiceSettingsContent.HSplitBottom(LineSize, &VoiceSettingsContent, &Button);
	Button.VSplitRight(150.0f, nullptr, &Button);
	static CButtonContainer s_VoiceModerationButton;
	if(DoButton_Menu(&s_VoiceModerationButton, BcLocalize("Voice Moderation"), 0, &Button))
	{
		static SPopupMenuId s_PopupId;
		static SPopupVoiceModerationContext s_Context;
		s_Context.m_pMenus = this;
		Ui()->DoPopupMenu(&s_PopupId, Button.x, Button.y + Button.h, 300.0f, 260.0f, &s_Context, PopupVoiceModeration);
	}
	GameClient()->m_VoiceChat.RenderMenuSettingsBlock(VoiceSettingsContent, s_VoiceRevealPhase);
	RightColumn.HSplitTop(MarginBetweenViews, nullptr, &RightColumn);

	const float VoiceBindsBlockHeight = LineSize + MarginSmall + 3.0f * LineSize + 2.0f * MarginSmall;
	CUIRect VoiceBindsBlock;
	RightColumn.HSplitTop(VoiceBindsBlockHeight, &VoiceBindsBlock, &RightColumn);
	CUIRect VoiceBindsBlockBg = VoiceBindsBlock;
	VoiceBindsBlockBg.w += BlockPadding;
	VoiceBindsBlockBg.h += BlockPadding;
	VoiceBindsBlockBg.x -= BlockPadding * 0.5f;
	VoiceBindsBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&VoiceBindsBlockBg, IGraphics::CORNER_ALL, 10.0f);
	VoiceBindsBlock.HSplitTop(LineSize, &Label, &VoiceBindsBlock);
	Ui()->DoLabel(&Label, BcLocalize("Voice Binds"), HeadlineFontSize, TEXTALIGN_ML);
	VoiceBindsBlock.HSplitTop(MarginSmall, nullptr, &VoiceBindsBlock);
	static CButtonContainer s_PttBindReader;
	static CButtonContainer s_PttBindClear;
	static CButtonContainer s_MicMuteBindReader;
	static CButtonContainer s_MicMuteBindClear;
	static CButtonContainer s_HeadphonesMuteBindReader;
	static CButtonContainer s_HeadphonesMuteBindClear;
	VoiceBindsBlock.HSplitTop(LineSize, &Button, &VoiceBindsBlock);
	DoLine_KeyReader(Button, s_PttBindReader, s_PttBindClear, BcLocalize("Push-to-talk"), "+voicechat");
	VoiceBindsBlock.HSplitTop(MarginSmall, nullptr, &VoiceBindsBlock);
	VoiceBindsBlock.HSplitTop(LineSize, &Button, &VoiceBindsBlock);
	DoLine_KeyReader(Button, s_MicMuteBindReader, s_MicMuteBindClear, BcLocalize("Mute microphone"), "toggle_voice_mic_mute");
	VoiceBindsBlock.HSplitTop(MarginSmall, nullptr, &VoiceBindsBlock);
	VoiceBindsBlock.HSplitTop(LineSize, &Button, &VoiceBindsBlock);
	DoLine_KeyReader(Button, s_HeadphonesMuteBindReader, s_HeadphonesMuteBindClear, BcLocalize("Mute headphones"), "toggle_voice_headphones_mute");

	RightColumn.HSplitTop(MarginBetweenViews, nullptr, &RightColumn);
	const bool ShowNamePlateSettings = g_Config.m_BcClientIndicatorInNamePlate != 0;
	const bool ShowScoreboardSettings = g_Config.m_BcClientIndicatorInScoreboard != 0;
	const float NamePlateSettingsHeight = ShowNamePlateSettings ? 2.0f * LineSize : 0.0f;
	const float ScoreboardSettingsHeight = ShowScoreboardSettings ? LineSize : 0.0f;
	const float ClientIndicatorBlockHeight = LineSize + MarginSmall + 2.0f * LineSize + NamePlateSettingsHeight + ScoreboardSettingsHeight;

	CUIRect ClientIndicatorBlock;
	RightColumn.HSplitTop(ClientIndicatorBlockHeight, &ClientIndicatorBlock, &RightColumn);

	CUIRect ClientIndicatorBlockBg = ClientIndicatorBlock;
	ClientIndicatorBlockBg.w += BlockPadding;
	ClientIndicatorBlockBg.h += BlockPadding;
	ClientIndicatorBlockBg.x -= BlockPadding * 0.5f;
	ClientIndicatorBlockBg.y -= BlockPadding * 0.5f;
	BestClientUiTheme::DrawUiBlock(&ClientIndicatorBlockBg, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcClientIndicatorInNamePlate, &ClientIndicatorBlockBg);

	ClientIndicatorBlock.HSplitTop(LineSize, &Label, &ClientIndicatorBlock);
	Ui()->DoLabel(&Label, BcLocalize("Client Indicator"), HeadlineFontSize, TEXTALIGN_ML);
	ClientIndicatorBlock.HSplitTop(MarginSmall, nullptr, &ClientIndicatorBlock);

	ClientIndicatorBlock.HSplitTop(LineSize, &Content, &ClientIndicatorBlock);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcClientIndicatorInNamePlate, BcLocalize("Show indicator in name plates"), &g_Config.m_BcClientIndicatorInNamePlate, &Content, LineSize);

	if(ShowNamePlateSettings)
	{
		ClientIndicatorBlock.HSplitTop(LineSize, &Content, &ClientIndicatorBlock);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcClientIndicatorInNamePlateAboveSelf, BcLocalize("Show above yourself"), &g_Config.m_BcClientIndicatorInNamePlateAboveSelf, &Content, LineSize);

		ClientIndicatorBlock.HSplitTop(LineSize, &Button, &ClientIndicatorBlock);
		Ui()->DoScrollbarOption(&g_Config.m_BcClientIndicatorInNamePlateSize, &g_Config.m_BcClientIndicatorInNamePlateSize, &Button, BcLocalize("Name plate indicator size"), -50, 100);
	}

	ClientIndicatorBlock.HSplitTop(LineSize, &Content, &ClientIndicatorBlock);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcClientIndicatorInScoreboard, BcLocalize("Show indicator in scoreboard"), &g_Config.m_BcClientIndicatorInScoreboard, &Content, LineSize);

	if(ShowScoreboardSettings)
	{
		ClientIndicatorBlock.HSplitTop(LineSize, &Button, &ClientIndicatorBlock);
		Ui()->DoScrollbarOption(&g_Config.m_BcClientIndicatorInSoreboardSize, &g_Config.m_BcClientIndicatorInSoreboardSize, &Button, BcLocalize("Scoreboard indicator size"), -50, 100);
	}

	CUIRect OthersScrollContentRect;
	OthersScrollContentRect.x = OthersScrollContentX;
	OthersScrollContentRect.y = std::max(Column.y, RightColumn.y) + MarginSmall * 2.0f;
	OthersScrollContentRect.w = OthersScrollContentW;
	OthersScrollContentRect.h = 0.0f;
	s_OthersScrollRegion.AddRect(OthersScrollContentRect);
	s_OthersScrollRegion.End();
}

CUi::EPopupMenuFunctionResult CMenus::PopupVoiceModeration(void *pContext, CUIRect View, bool Active)
{
	SPopupVoiceModerationContext *pPopupContext = static_cast<SPopupVoiceModerationContext *>(pContext);
	CMenus *pMenus = pPopupContext->m_pMenus;
	CVoiceChat &Voice = pMenus->GameClient()->m_VoiceChat;
	const float RowH = 24.0f;
	const float Pad = 6.0f;

	(void)Active;
	View.Margin(10.0f, &View);

	CUIRect Row;
	if(!Voice.IsVoiceRegistered())
	{
		View.HSplitTop(RowH, &Row, &View);
		pMenus->Ui()->DoLabel(&Row, BcLocalize("Not connected to voice server"), 13.0f, TEXTALIGN_MC);
		return CUi::POPUP_KEEP_OPEN;
	}

	static CLineInputBuffered<128> s_VoiceModKeyInput;
	static CButtonContainer s_VoiceModAuthButton;
	static CButtonContainer s_VoiceModRefreshButton;
	static std::vector<CButtonContainer> s_vVoiceModMuteButtons;
	static int64_t s_LastVoiceModRefreshTick = 0;

	if(!Voice.IsVoiceModAuthed())
	{
		if(s_VoiceModKeyInput.IsEmpty() && g_Config.m_BcVoiceModKey[0] != '\0')
			s_VoiceModKeyInput.Set(g_Config.m_BcVoiceModKey);

		View.HSplitTop(RowH, &Row, &View);
		pMenus->Ui()->DoLabel(&Row, BcLocalize("Voice Moderator Login"), 14.0f, TEXTALIGN_MC);
		View.HSplitTop(Pad, nullptr, &View);

		CUIRect LabelRect, FieldRect;
		View.HSplitTop(RowH, &Row, &View);
		Row.VSplitLeft(80.0f, &LabelRect, &FieldRect);
		pMenus->Ui()->DoLabel(&LabelRect, BcLocalize("Mod key:"), 12.0f, TEXTALIGN_ML);
		FieldRect.HMargin(2.0f, &FieldRect);
		s_VoiceModKeyInput.SetHidden(true);
		pMenus->Ui()->DoEditBox(&s_VoiceModKeyInput, &FieldRect, 12.0f);

		View.HSplitTop(Pad, nullptr, &View);
		View.HSplitTop(RowH, &Row, &View);

		auto DoLogin = [&]() {
			const char *pKey = s_VoiceModKeyInput.GetString();
			str_copy(g_Config.m_BcVoiceModKey, pKey, sizeof(g_Config.m_BcVoiceModKey));
			Voice.VoiceModAuth(pKey);
		};

		if(Voice.IsVoiceModAuthPending())
		{
			pMenus->Ui()->DoLabel(&Row, BcLocalize("Authenticating..."), 12.0f, TEXTALIGN_MC);
		}
		else if(Voice.IsVoiceModAuthFailed())
		{
			CUIRect MsgRect, BtnRect;
			Row.VSplitRight(110.0f, &MsgRect, &BtnRect);
			pMenus->TextRender()->TextColor(ColorRGBA(1.0f, 0.3f, 0.3f, 1.0f));
			pMenus->Ui()->DoLabel(&MsgRect, BcLocalize("Wrong key"), 12.0f, TEXTALIGN_ML);
			pMenus->TextRender()->TextColor(pMenus->TextRender()->DefaultTextColor());
			if(pMenus->DoButton_Menu(&s_VoiceModAuthButton, BcLocalize("Try again"), 0, &BtnRect))
				DoLogin();
		}
		else if(pMenus->DoButton_Menu(&s_VoiceModAuthButton, BcLocalize("Login as Voice Mod"), 0, &Row))
		{
			DoLogin();
		}
		return CUi::POPUP_KEEP_OPEN;
	}

	View.HSplitTop(RowH, &Row, &View);
	CUIRect TitleRect, RefreshBtn;
	Row.VSplitRight(90.0f, &TitleRect, &RefreshBtn);
	pMenus->Ui()->DoLabel(&TitleRect, BcLocalize("Voice players on this server"), 13.0f, TEXTALIGN_ML);
	if(pMenus->DoButton_Menu(&s_VoiceModRefreshButton, BcLocalize("Refresh"), 0, &RefreshBtn))
	{
		Voice.VoiceModRefresh();
		s_LastVoiceModRefreshTick = time_get();
	}

	const int64_t Now = time_get();
	const int64_t Interval = time_freq() * 3;
	if(s_LastVoiceModRefreshTick == 0 || Now - s_LastVoiceModRefreshTick > Interval)
	{
		Voice.VoiceModRefresh();
		s_LastVoiceModRefreshTick = Now;
	}

	View.HSplitTop(Pad, nullptr, &View);
	const auto &Players = Voice.GetVoiceModPlayers();
	if(Players.empty())
	{
		View.HSplitTop(RowH, &Row, &View);
		pMenus->Ui()->DoLabel(&Row, BcLocalize("No players in current voice room"), 12.0f, TEXTALIGN_MC);
		return CUi::POPUP_KEEP_OPEN;
	}

	if(s_vVoiceModMuteButtons.size() != Players.size())
		s_vVoiceModMuteButtons.resize(Players.size());

	View.HSplitTop(18.0f, &Row, &View);
	CUIRect NameHeader, ActionHeader;
	Row.VSplitRight(80.0f, &NameHeader, &ActionHeader);
	pMenus->TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f));
	pMenus->Ui()->DoLabel(&NameHeader, BcLocalize("Player"), 11.0f, TEXTALIGN_ML);
	pMenus->Ui()->DoLabel(&ActionHeader, BcLocalize("Action"), 11.0f, TEXTALIGN_MC);
	pMenus->TextRender()->TextColor(pMenus->TextRender()->DefaultTextColor());

	static CScrollRegion s_VoiceModScroll;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = RowH + 4.0f;
	ScrollParams.m_ScrollbarMargin = 4.0f;
	s_VoiceModScroll.Begin(&View, &ScrollParams);

	for(size_t i = 0; i < Players.size(); ++i)
	{
		const CVoiceChat::SModPlayer &Player = Players[i];
		CUIRect PlayerRow;
		View.HSplitTop(RowH, &PlayerRow, &View);
		const bool Visible = s_VoiceModScroll.AddRect(PlayerRow);
		CUIRect Spacing;
		View.HSplitTop(4.0f, &Spacing, &View);
		s_VoiceModScroll.AddRect(Spacing);
		if(!Visible)
			continue;

		PlayerRow.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 4.0f);
		CUIRect NameRect, MuteBtn;
		PlayerRow.VSplitRight(80.0f, &NameRect, &MuteBtn);
		NameRect.VMargin(4.0f, &NameRect);
		MuteBtn.HMargin(3.0f, &MuteBtn);

		char aName[96];
		if(Player.m_Name.empty())
			str_format(aName, sizeof(aName), BcLocalize("slot %d"), (int)Player.m_GameClientId);
		else
			str_copy(aName, Player.m_Name.c_str(), sizeof(aName));

		if(Player.m_IsMuted)
			pMenus->TextRender()->TextColor(ColorRGBA(1.0f, 0.4f, 0.4f, 1.0f));
		pMenus->Ui()->DoLabel(&NameRect, aName, 11.5f, TEXTALIGN_ML);
		if(Player.m_IsMuted)
			pMenus->TextRender()->TextColor(pMenus->TextRender()->DefaultTextColor());

		const char *pBtnLabel = Player.m_IsMuted ? BcLocalize("Unmute") : BcLocalize("Mute");
		if(pMenus->DoButton_Menu(&s_vVoiceModMuteButtons[i], pBtnLabel, 0, &MuteBtn))
			Voice.VoiceModMute(Player.m_SessionId, !Player.m_IsMuted);
	}

	s_VoiceModScroll.End();
	return CUi::POPUP_KEEP_OPEN;
}

void CMenus::RenderSettingsBestClientInfo(CUIRect MainView)
{
	const float LineSize = 20.0f;
	const float MarginSmall = 5.0f;
	const float MarginBetweenViews = 30.0f;
	const float HeadlineFontSize = 20.0f;
	const float HeadlineHeight = HeadlineFontSize;

	CUIRect LeftView, RightView, Button, Label, LowerLeftView;
	CUIRect GamesButton;
	MainView.HSplitTop(24.0f, &GamesButton, &MainView);
	static CButtonContainer s_GamesButton;
	if(DoButton_MenuTab(&s_GamesButton, BcLocalize("Mini games"), 0, &GamesButton, IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 4.0f))
		OpenBestClientFun();
	MainView.HSplitTop(10.0f, nullptr, &MainView);
	MainView.HSplitTop(20.0f, nullptr, &MainView);

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);
	LeftView.HSplitMid(&LeftView, &LowerLeftView, 0.0f);

	// ── BestClient Links ───────────────────────────────────────────────────
	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, BcLocalize("BestClient Links"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	static CButtonContainer s_DiscordButton, s_WebsiteButton, s_TelegramButton;
	CUIRect ButtonLeft, ButtonRight;

	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&ButtonLeft, &ButtonRight, MarginSmall);
	if(DoButtonLineSize_Menu(&s_DiscordButton, BcLocalize("Discord"), 0, &ButtonLeft, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://discord.gg/bestclient");
	if(DoButtonLineSize_Menu(&s_TelegramButton, BcLocalize("Telegram"), 0, &ButtonRight, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://t.me/bestddnet");

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&ButtonLeft, &ButtonRight, MarginSmall);
	if(DoButtonLineSize_Menu(&s_WebsiteButton, BcLocalize("Website"), 0, &ButtonLeft, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://bestclient.fun");
#if defined(CONF_AUTOUPDATE)
	static CButtonContainer s_CheckUpdateButton;
	if(DoButtonLineSize_Menu(&s_CheckUpdateButton, BcLocalize("Check update"), 0, &ButtonRight, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Updater()->CheckForUpdate();
#endif

	// ── Config Files (anchored to the bottom of the left column) ──────────
	LeftView = LowerLeftView;
	LeftView.HSplitBottom(LineSize * 2.0f + MarginSmall * 2.0f + HeadlineFontSize, nullptr, &LeftView);
	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, BcLocalize("Config Files"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	char aBuf[128 + IO_MAX_PATH_LENGTH];
	CUIRect BestClientConfig;
	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	BestClientConfig = Button;

	static CButtonContainer s_Config;
	if(DoButtonLineSize_Menu(&s_Config, BcLocalize("BestClient Settings"), 0, &BestClientConfig, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::BESTCLIENT].m_aConfigPath, aBuf, sizeof(aBuf));
		Client()->ViewFile(aBuf);
	}

	// ── BestClient Developers ─────────────────────────────────────────────
	RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	Ui()->DoLabel(&Label, BcLocalize("BestClient Developers"), HeadlineFontSize, TEXTALIGN_ML);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);

	const float TeeSize = 64.0f;
	const float DevNameFontSize = 24.0f;
	const float CardSize = TeeSize + MarginSmall * 2.0f;
	CUIRect TeeRect, DevCardRect;
	static CButtonContainer s_LinkButton1, s_LinkButton2, s_LinkButton3;
	{
		RightView.HSplitTop(CardSize, &DevCardRect, &RightView);
		DevCardRect.VSplitLeft(CardSize, &TeeRect, &Label);
		Label.VSplitLeft(TextRender()->TextWidth(DevNameFontSize, "RoflikBEST"), &Label, &Button);
		Button.VSplitLeft(MarginSmall, nullptr, &Button);
		Button.w = DevNameFontSize;
		Button.h = DevNameFontSize;
		Button.y = Label.y + (Label.h / 2.0f - Button.h / 2.0f);
		Ui()->DoLabel(&Label, "RoflikBEST", DevNameFontSize, TEXTALIGN_ML);
		if(Ui()->DoButton_FontIcon(&s_LinkButton1, FontIcon::ARROW_UP_RIGHT_FROM_SQUARE, 0, &Button, IGraphics::CORNER_ALL))
			Client()->ViewLink("https://github.com/roflikbest");
		RenderDevSkin(TeeRect.Center(), TeeSize, "10Nanami_glow", "nanami", true, 0, 0, 0, false, true, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), ColorRGBA(0.94f, 0.74f, 0.92f, 1.0f));
	}
	{
		RightView.HSplitTop(CardSize, &DevCardRect, &RightView);
		DevCardRect.VSplitLeft(CardSize, &TeeRect, &Label);
		Label.VSplitLeft(TextRender()->TextWidth(DevNameFontSize, "noxygalaxy"), &Label, &Button);
		Button.VSplitLeft(MarginSmall, nullptr, &Button);
		Button.w = DevNameFontSize;
		Button.h = DevNameFontSize;
		Button.y = Label.y + (Label.h / 2.0f - Button.h / 2.0f);
		Ui()->DoLabel(&Label, "noxygalaxy", DevNameFontSize, TEXTALIGN_ML);
		if(Ui()->DoButton_FontIcon(&s_LinkButton3, FontIcon::ARROW_UP_RIGHT_FROM_SQUARE, 0, &Button, IGraphics::CORNER_ALL))
			Client()->ViewLink("https://github.com/noxygalaxy");
		RenderDevSkin(TeeRect.Center(), TeeSize, "Niko_OneShot", "Niko_OneShot", false, 0, 0, 0, false, true);
	}
	{
		RightView.HSplitTop(CardSize, &DevCardRect, &RightView);
		DevCardRect.VSplitLeft(CardSize, &TeeRect, &Label);
		Label.VSplitLeft(TextRender()->TextWidth(DevNameFontSize, "sqwinix"), &Label, &Button);
		Button.VSplitLeft(MarginSmall, nullptr, &Button);
		Button.w = DevNameFontSize;
		Button.h = DevNameFontSize;
		Button.y = Label.y + (Label.h / 2.0f - Button.h / 2.0f);
		Ui()->DoLabel(&Label, "sqwinix", DevNameFontSize, TEXTALIGN_ML);
		if(Ui()->DoButton_FontIcon(&s_LinkButton2, FontIcon::ARROW_UP_RIGHT_FROM_SQUARE, 0, &Button, IGraphics::CORNER_ALL))
			Client()->ViewLink("https://github.com/sqwinixxx");
		RenderDevSkin(TeeRect.Center(), TeeSize, "sticker_nanami", "sticker_nanami", true, 0, 0, 0, false, true, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	}

	// ── Hide Settings Tabs ────────────────────────────────────────────────
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	Ui()->DoLabel(&Label, BcLocalize("Hide Settings Tabs"), HeadlineFontSize, TEXTALIGN_ML);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);

	const char *apTabNames[] = {
		BcLocalize("Visuals"),
		BcLocalize("Gameplay"),
		BcLocalize("Others"),
		BcLocalize("Info"),
	};
	const int aTabOrder[NUM_BESTCLIENT_TABS] = {
		BESTCLIENT_TAB_VISUALS,
		BESTCLIENT_TAB_GAMEPLAY,
		BESTCLIENT_TAB_OTHERS,
		BESTCLIENT_TAB_INFO,
	};

	CUIRect LeftSettings, RightSettings;
	RightView.VSplitMid(&LeftSettings, &RightSettings, MarginSmall);

	static CButtonContainer s_aShowTabButtons[NUM_BESTCLIENT_TABS] = {};
	int HideableTabCount = 0;
	int HideableVisibleIndex = 0;
	for(const int Tab : aTabOrder)
	{
		if(Tab == BESTCLIENT_TAB_INFO)
			continue;

		++HideableTabCount;
		int Hidden = IsBestClientTabFlagSet(g_Config.m_BcBestClientSettingsTabs, Tab);
		CUIRect *pColumn = HideableVisibleIndex % 2 == 0 ? &LeftSettings : &RightSettings;
		DoButton_CheckBoxAutoVMarginAndSet(&s_aShowTabButtons[Tab], apTabNames[Tab], &Hidden, pColumn, LineSize);
		SetBestClientTabFlag(g_Config.m_BcBestClientSettingsTabs, Tab, Hidden);
		++HideableVisibleIndex;
	}
	const int HideableRows = (HideableTabCount + 1) / 2;
	RightView.HSplitTop(LineSize * (HideableRows + 0.5f), nullptr, &RightView);
}

void CMenus::RenderSettingsBestClientWeaponGlow(CUIRect &Column)
{
	constexpr float LineSize = 20.0f;
	constexpr float MarginSmall = 5.0f;
	constexpr float PreviewHeight = 106.0f;
	static int s_Weapon = 0;
	static CButtonContainer s_aWeaponButtons[3];
	static CButtonContainer s_aModeButtons[4];
	static CButtonContainer s_aResetColor[3];
	static CButtonContainer s_ResetButton;
	static CButtonContainer s_WeaponGlowAuthorBadge;
	const char *apTitles[] = {BcLocalize("Laser Glow"), BcLocalize("Shotgun Glow"), BcLocalize("Rocket Glow")};
	const char *apWeapons[] = {BcLocalize("Laser"), BcLocalize("Shotgun"), BcLocalize("Rocket")};
	const char *apModes[] = {BcLocalize("Classic"), BcLocalize("Prism"), BcLocalize("Pulse"), BcLocalize("Crystal")};
	int *apEnabled[] = {&g_Config.m_BcLaserGlow, &g_Config.m_BcShotgunGlow, &g_Config.m_BcRocketGlow};
	int *apPower[] = {&g_Config.m_BcLaserGlowPower, &g_Config.m_BcShotgunGlowPower, &g_Config.m_BcRocketGlowPower};
	int *apMode[] = {&g_Config.m_BcLaserGlowMode, &g_Config.m_BcShotgunGlowMode, &g_Config.m_BcRocketGlowMode};
	unsigned *apColor[] = {&g_Config.m_BcLaserGlowColor, &g_Config.m_BcShotgunGlowColor, &g_Config.m_BcRocketGlowColor};
	const unsigned aDefaultColors[] = {DefaultConfig::BcLaserGlowColor, DefaultConfig::BcShotgunGlowColor, DefaultConfig::BcRocketGlowColor};
	const float NukeHeight = s_Weapon == 2 ? (g_Config.m_BcMemeNukeExplosion ? 4 : 2) * (LineSize + MarginSmall) : 0.0f;
	const float Height = 6 * (LineSize + MarginSmall) + PreviewHeight + MarginSmall + NukeHeight;
	CUIRect Block;
	Column.HSplitTop(Height, &Block, &Column);
	CUIRect Background = Block;
	Background.Margin(-10.0f, &Background);
	BestClientUiTheme::DrawUiBlock(&Background, IGraphics::CORNER_ALL, 10.0f);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcLaserGlow, &Background);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcShotgunGlow, &Background);
	BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcRocketGlow, &Background);
	CUIRect Row, Button;
	Block.HSplitTop(LineSize, &Row, &Block);
	CUIRect ResetButton;
	Row.VSplitRight(LineSize + 8.0f, &Row, &ResetButton);
	const bool ResetClicked = Ui()->DoButton_FontIcon(&s_ResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &ResetButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_ResetButton, &ResetButton, BcLocalize("Reset to defaults"));
	CUIRect AuthorBadge;
	Row.VSplitRight(MarginSmall, &Row, nullptr);
	BcMenuBadges::DrawAuthor(Graphics(), Ui(), TextRender(), &Row, 8.0f, &AuthorBadge);
	Ui()->DoButtonLogic(&s_WeaponGlowAuthorBadge, 0, &AuthorBadge, BUTTONFLAG_NONE);
	GameClient()->m_Tooltips.DoToolTip(&s_WeaponGlowAuthorBadge, &AuthorBadge, "motya");
	GameClient()->m_Tooltips.SetFadeTime(&s_WeaponGlowAuthorBadge, 0.0f);
	if(s_Weapon == 0)
		BcMenuBadges::DrawNew(Graphics(), Ui(), TextRender(), &Row, 8.0f);
	Ui()->DoLabel(&Row, apTitles[s_Weapon], 20.0f, TEXTALIGN_ML);
	Block.HSplitTop(MarginSmall, nullptr, &Block);
	Block.HSplitTop(LineSize, &Row, &Block);
	const float WeaponWidth = Row.w / 3.0f;
	int NextWeapon = s_Weapon;
	for(int i = 0; i < 3; ++i)
	{
		Row.VSplitLeft(WeaponWidth, &Button, &Row);
		Button.HMargin(2.0f, &Button);
		const int Corners = i == 0 ? IGraphics::CORNER_L : (i == 2 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
		if(DoButton_Menu(&s_aWeaponButtons[i], apWeapons[i], s_Weapon == i, &Button, BUTTONFLAG_LEFT, nullptr, Corners))
			NextWeapon = i;
	}
	Block.HSplitTop(MarginSmall, nullptr, &Block);
	Block.HSplitTop(LineSize, &Row, &Block);
	DoButton_CheckBoxAutoVMarginAndSet(apEnabled[s_Weapon], BcLocalize("Enable"), apEnabled[s_Weapon], &Row, LineSize);
	Block.HSplitTop(MarginSmall, nullptr, &Block);
	Block.HSplitTop(LineSize, &Row, &Block);
	Ui()->DoScrollbarOption(apPower[s_Weapon], apPower[s_Weapon], &Row, BcLocalize("Power"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");
	Block.HSplitTop(MarginSmall, nullptr, &Block);
	DoLine_ColorPicker(&s_aResetColor[s_Weapon], LineSize, 15.0f, MarginSmall, &Block, BcLocalize("Color"), apColor[s_Weapon],
		color_cast<ColorRGBA>(ColorHSLA(aDefaultColors[s_Weapon])), false, nullptr, false);
	CUIRect Preview;
	Block.HSplitTop(PreviewHeight, &Preview, &Block);
	Ui()->ClipEnable(&Preview);
	if(s_Weapon == 0)
		DoLaserPreview(&Preview, ColorHSLA(g_Config.m_ClLaserRifleOutlineColor), ColorHSLA(g_Config.m_ClLaserRifleInnerColor), LASERTYPE_RIFLE);
	else if(s_Weapon == 1)
		DoLaserPreview(&Preview, ColorHSLA(g_Config.m_ClLaserShotgunOutlineColor), ColorHSLA(g_Config.m_ClLaserShotgunInnerColor), LASERTYPE_SHOTGUN);
	else
		DoRocketPreview(&Preview);
	Ui()->ClipDisable();
	Block.HSplitTop(MarginSmall, nullptr, &Block);
	Block.HSplitTop(LineSize, &Row, &Block);
	const float ModeWidth = Row.w / 4.0f;
	for(int i = 0; i < 4; ++i)
	{
		Row.VSplitLeft(ModeWidth, &Button, &Row);
		Button.HMargin(2.0f, &Button);
		const int Corners = i == 0 ? IGraphics::CORNER_L : (i == 3 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
		if(DoButton_Menu(&s_aModeButtons[i], apModes[i], *apMode[s_Weapon] == i, &Button, BUTTONFLAG_LEFT, nullptr, Corners))
			*apMode[s_Weapon] = i;
	}
	if(s_Weapon == 2)
	{
		Block.HSplitTop(MarginSmall, nullptr, &Block);
		Block.HSplitTop(LineSize, &Row, &Block);
		Ui()->DoLabel(&Row, BcLocalize("Nuke effects"), 16.0f, TEXTALIGN_ML);
		Block.HSplitTop(MarginSmall, nullptr, &Block);
		Block.HSplitTop(LineSize, &Row, &Block);
		const bool NukeExpanded = g_Config.m_BcMemeNukeExplosion != 0;
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMemeNukeExplosion, BcLocalize("Enable Nuke effects"), &g_Config.m_BcMemeNukeExplosion, &Row, LineSize);
		if(NukeExpanded)
		{
			Block.HSplitTop(MarginSmall, nullptr, &Block);
			Block.HSplitTop(LineSize, &Row, &Block);
			Ui()->DoScrollbarOption(&g_Config.m_BcMemeNukeShake, &g_Config.m_BcMemeNukeShake, &Row, BcLocalize("Screen shake"), 0, 200, &CUi::ms_LinearScrollbarScale, 0u, "%");
			Block.HSplitTop(MarginSmall, nullptr, &Block);
			Block.HSplitTop(LineSize, &Row, &Block);
			Ui()->DoScrollbarOption(&g_Config.m_BcMemeNukeScale, &g_Config.m_BcMemeNukeScale, &Row, BcLocalize("Nuke size"), 20, 300, &CUi::ms_LinearScrollbarScale, 0u, "%");
		}
	}
	if(ResetClicked)
	{
		const int aDefaultEnabled[] = {DefaultConfig::BcLaserGlow, DefaultConfig::BcShotgunGlow, DefaultConfig::BcRocketGlow};
		const int aDefaultPower[] = {DefaultConfig::BcLaserGlowPower, DefaultConfig::BcShotgunGlowPower, DefaultConfig::BcRocketGlowPower};
		const int aDefaultModes[] = {DefaultConfig::BcLaserGlowMode, DefaultConfig::BcShotgunGlowMode, DefaultConfig::BcRocketGlowMode};
		*apEnabled[s_Weapon] = aDefaultEnabled[s_Weapon];
		*apPower[s_Weapon] = aDefaultPower[s_Weapon];
		*apMode[s_Weapon] = aDefaultModes[s_Weapon];
		*apColor[s_Weapon] = aDefaultColors[s_Weapon];
		if(s_Weapon == 2)
		{
			g_Config.m_BcMemeNukeExplosion = DefaultConfig::BcMemeNukeExplosion;
			g_Config.m_BcMemeNukeShake = DefaultConfig::BcMemeNukeShake;
			g_Config.m_BcMemeNukeScale = DefaultConfig::BcMemeNukeScale;
		}
	}
	s_Weapon = NextWeapon;
}

void CMenus::DoRocketPreview(const CUIRect *pRect)
{
	CUIRect Section = *pRect;

	const vec2 From = vec2(Section.x + 36.0f, Section.y + Section.h * 0.5f);
	const vec2 Pos = vec2(Section.x + Section.w - 36.0f, Section.y + Section.h * 0.5f);

	if(g_Config.m_BcRocketGlow)
	{
		const float Power = (float)g_Config.m_BcRocketGlowPower;
		const int Mode = std::clamp(g_Config.m_BcRocketGlowMode, 0, 3);
		const int NumPoints = 16;
		vec2 aPoints[16];
		float aAlphas[16];
		const float GlobalTime = Client()->GlobalTime();

		for(int i = 0; i < NumPoints; ++i)
		{
			float t = (float)i / (float)(NumPoints - 1);
			float Arc = std::sin(t * pi) * 10.0f * std::sin(GlobalTime * 3.0f + t * 2.0f);
			aPoints[i] = mix(Pos, From, t) + vec2(0.0f, Arc);
			aAlphas[i] = 1.0f;
		}
		GameClient()->m_WeaponVfx.RenderRocketTrajectory(aPoints, aAlphas, NumPoints, Mode, Power);
	}

	Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteWeaponGrenade);
	Graphics()->SelectSprite(SPRITE_WEAPON_GRENADE_BODY);
	Graphics()->QuadsBegin();
	Graphics()->QuadsSetSubset(0, 0, 1, 1);
	Graphics()->DrawSprite(From.x, From.y, 48.0f);
	Graphics()->QuadsEnd();

	if(GameClient()->m_GameSkin.m_aSpriteWeaponProjectiles[WEAPON_GRENADE].IsValid())
	{
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_aSpriteWeaponProjectiles[WEAPON_GRENADE]);
		Graphics()->SelectSprite(SPRITE_WEAPON_GRENADE_PROJ);
		Graphics()->QuadsBegin();
		Graphics()->QuadsSetSubset(0, 0, 1, 1);
		Graphics()->DrawSprite(Pos.x, Pos.y, 24.0f);
		Graphics()->QuadsEnd();
	}
}
