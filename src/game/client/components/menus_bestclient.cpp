/* Copyright © 2026 BestProject Team */
#include <base/system.h>

#include <engine/config.h>
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <generated/protocol.h>

#include <game/client/bc_ui_animations.h>
#include <game/client/components/bestclient/gradient.h>
#include <game/client/components/hud_layout.h>
#include <game/client/components/media_decoder.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

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

static void UpdateModuleRevealPhase(float &Phase, bool Expanded, float Dt)
{
	if(BCUiAnimations::Enabled() && g_Config.m_BcModuleUiRevealAnimation != 0)
		BCUiAnimations::UpdatePhase(Phase, Expanded ? 1.0f : 0.0f, Dt, BCUiAnimations::MsToSeconds(g_Config.m_BcModuleUiRevealAnimationMs));
	else
		Phase = Expanded ? 1.0f : 0.0f;
}

static void DrawBcMenuBadge(IGraphics *pGraphics, CUi *pUi, ITextRender *pTextRender, CUIRect *pRow, const char *pText, float FontSize, const ColorRGBA &Top, const ColorRGBA &Bottom, float Gap)
{
	const float BadgeWidth = pTextRender->TextWidth(FontSize, pText) + 10.0f;
	CUIRect Badge;
	pRow->VSplitRight(BadgeWidth + Gap, pRow, &Badge);
	Badge.VSplitLeft(Gap, nullptr, &Badge);
	Badge.HMargin(2.0f, &Badge);
	pGraphics->DrawRect4(Badge.x, Badge.y, Badge.w, Badge.h, Top, Bottom, Top, Bottom, IGraphics::CORNER_ALL, 5.0f);
	pUi->DoLabel(&Badge, pText, FontSize, TEXTALIGN_MC);
}

enum
{
	BESTCLIENT_TAB_VISUALS = 0,
	BESTCLIENT_TAB_GAMEPLAY,
	BESTCLIENT_TAB_OTHERS,
	BESTCLIENT_TAB_FUN,
	BESTCLIENT_TAB_INFO,
	NUM_BESTCLIENT_TABS,
};

void CMenus::RenderSettingsBestClient(CUIRect MainView)
{
	if(m_AssetsEditorState.m_VisualsEditorOpen && m_AssetsEditorState.m_FullscreenOpen)
	{
		RenderAssetsEditorScreen(*Ui()->Screen());
		return;
	}

	// Match original old-layout: shift content up past the 20px margin so tab bar
	// appears 8px from the panel border instead of 20px from content area start.
	MainView.y -= 20.0f;
	MainView.h += 20.0f;

	static int s_CurTab = BESTCLIENT_TAB_VISUALS;
	static CButtonContainer s_aPageTabs[NUM_BESTCLIENT_TABS] = {};

	MainView.HSplitTop(8.0f, nullptr, &MainView);
	CUIRect TabBar, TabButton;
	MainView.HSplitTop(24.0f, &TabBar, &MainView);

	const char *apTabNames[NUM_BESTCLIENT_TABS] = {
		Localize("Visuals"),
		Localize("Gameplay"),
		Localize("Others"),
		Localize("Fun"),
		Localize("Info"),
	};
	const int aTabOrder[NUM_BESTCLIENT_TABS] = {
		BESTCLIENT_TAB_VISUALS,
		BESTCLIENT_TAB_GAMEPLAY,
		BESTCLIENT_TAB_OTHERS,
		BESTCLIENT_TAB_FUN,
		BESTCLIENT_TAB_INFO,
	};

	auto IsTabHidden = [&](int Tab) {
		return Tab != BESTCLIENT_TAB_INFO && IsBestClientTabFlagSet(g_Config.m_BcBestClientSettingsTabs, Tab);
	};

	int TabCount = 0;
	int FirstVisibleTab = -1;
	for(const int Tab : aTabOrder)
	{
		if(IsTabHidden(Tab))
			continue;
		if(FirstVisibleTab == -1)
			FirstVisibleTab = Tab;
		++TabCount;
	}

	if(FirstVisibleTab == -1)
	{
		s_CurTab = BESTCLIENT_TAB_INFO;
		FirstVisibleTab = BESTCLIENT_TAB_INFO;
		TabCount = 1;
	}

	if(s_CurTab < BESTCLIENT_TAB_VISUALS || s_CurTab >= NUM_BESTCLIENT_TABS || IsTabHidden(s_CurTab))
		s_CurTab = FirstVisibleTab;

	const float TabWidth = TabBar.w / (float)TabCount;
	int VisibleIndex = 0;
	for(const int Tab : aTabOrder)
	{
		if(IsTabHidden(Tab))
			continue;

		TabBar.VSplitLeft(TabWidth, &TabButton, &TabBar);
		const int Corners = VisibleIndex == 0 ? IGraphics::CORNER_L : (VisibleIndex == TabCount - 1 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
		if(DoButton_MenuTab(&s_aPageTabs[Tab], apTabNames[Tab], s_CurTab == Tab, &TabButton, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
			s_CurTab = Tab;
		VisibleIndex++;
	}

	MainView.HSplitTop(10.0f, nullptr, &MainView);

	if(s_CurTab == BESTCLIENT_TAB_VISUALS)
		RenderSettingsBestClientVisuals(MainView);
	else if(s_CurTab == BESTCLIENT_TAB_GAMEPLAY)
		RenderSettingsBestClientGameplay(MainView);
	else if(s_CurTab == BESTCLIENT_TAB_OTHERS)
		RenderSettingsBestClientOthers(MainView);
	else if(s_CurTab == BESTCLIENT_TAB_FUN)
		RenderSettingsBestClientFun(MainView);
	else if(s_CurTab == BESTCLIENT_TAB_INFO)
		RenderSettingsBestClientInfo(MainView);
}

void CMenus::RenderSettingsBestClientVisuals(CUIRect MainView)
{
	const float LineSize = 20.0f;
	const float MarginSmall = 5.0f;
	const float HeadlineFontSize = 20.0f;
	const float MarginBetweenViews = 30.0f;
	const float BlockPadding = MarginBetweenViews * 0.6666f;
	const ColorRGBA BlockColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f);

	{
		CUIRect HudButtonRow;
		MainView.HSplitTop(24.0f, &HudButtonRow, &MainView);
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		static CButtonContainer s_HudEditorButton;
		const bool CanOpen = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
		if(DoButton_MenuTab(&s_HudEditorButton, Localize("HUD editor"), 0, &HudButtonRow, IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 4.0f) && CanOpen)
		{
			SetActive(false);
			GameClient()->m_HudEditor.Activate();
		}
		GameClient()->m_Tooltips.DoToolTip(&s_HudEditorButton, &HudButtonRow, CanOpen ? Localize("Open in HUD editor") : Localize("Join a game first"));
		GameClient()->m_Tooltips.SetFadeTime(&s_HudEditorButton, 0.0f);
	}

	static CScrollRegion s_VisualsScrollRegion;
	vec2 VisualsScrollOffset(0.0f, 0.0f);
	CScrollRegionParams VisualsScrollParams;
	VisualsScrollParams.m_ScrollUnit = 60.0f;
	VisualsScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	VisualsScrollParams.m_ScrollbarMargin = 5.0f;
	s_VisualsScrollRegion.Begin(&MainView, &VisualsScrollOffset, &VisualsScrollParams);
	MainView.y += VisualsScrollOffset.y;
	MainView.VSplitRight(5.0f, &MainView, nullptr);
	MainView.VSplitLeft(5.0f, nullptr, &MainView);

	CUIRect LeftView, RightView;
	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	CUIRect Column = LeftView;
	Column.HSplitTop(10.0f, nullptr, &Column);

	RenderSettingsBestClientChatMediaBlock(Column);
	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	CUIRect Content, Label, Button;

	// Chat Bubbles (left column block)
	const bool ChatBubblesExpanded = g_Config.m_BcChatBubbles != 0;
	const bool ChatBubblesShowCustomColors = ChatBubblesExpanded && g_Config.m_BcChatBubbleCustomColors != 0;
	const float ChatBubbleColorPickerLineSize = 25.0f;
	const float ChatBubbleColorPickerSpacing = 5.0f;
	static float s_ChatBubblesRevealPhase = 0.0f;
	UpdateModuleRevealPhase(s_ChatBubblesRevealPhase, ChatBubblesExpanded, Client()->RenderFrameTime());
	const float ChatBubblesExpandedTargetHeight =
		9.0f * (MarginSmall + LineSize) +
		(ChatBubblesShowCustomColors ? 3.0f * (ChatBubbleColorPickerSpacing + ChatBubbleColorPickerLineSize) : 0.0f);
	const float ChatBubblesExpandedHeight = ChatBubblesExpandedTargetHeight * BCUiAnimations::EaseOutCubic(s_ChatBubblesRevealPhase);
	const float ChatBubblesBlockHeight = LineSize + MarginSmall + LineSize + ChatBubblesExpandedHeight;

	CUIRect ChatBubblesBlock;
	Column.HSplitTop(ChatBubblesBlockHeight, &ChatBubblesBlock, &Column);

	CUIRect ChatBubblesBlockBg = ChatBubblesBlock;
	ChatBubblesBlockBg.w += BlockPadding;
	ChatBubblesBlockBg.h += BlockPadding;
	ChatBubblesBlockBg.x -= BlockPadding * 0.5f;
	ChatBubblesBlockBg.y -= BlockPadding * 0.5f;
	ChatBubblesBlockBg.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);

	MainView = ChatBubblesBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	CUIRect ChatBubblesTitleLabel, ChatBubblesResetButton;
	Label.VSplitRight(LineSize + 8.0f, &ChatBubblesTitleLabel, &ChatBubblesResetButton);
	static CButtonContainer s_ChatBubblesResetButton;
	const bool ChatBubblesResetClicked = Ui()->DoButton_FontIcon(&s_ChatBubblesResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &ChatBubblesResetButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_ChatBubblesResetButton, &ChatBubblesResetButton, Localize("Reset to defaults"));
	if(ChatBubblesResetClicked)
	{
		g_Config.m_BcChatBubbles = DefaultConfig::BcChatBubbles;
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
	Ui()->DoLabel(&ChatBubblesTitleLabel, Localize("Chat Bubbles"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatBubbles, Localize("Show chat bubbles above players"), &g_Config.m_BcChatBubbles, &Content, LineSize);

	if(ChatBubblesExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = ChatBubblesExpandedHeight;
		Ui()->ClipEnable(&Visible);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatBubblesSelf, Localize("Show chat bubbles above you"), &g_Config.m_BcChatBubblesSelf, &Content, LineSize);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatBubblesDemo, Localize("Show chat bubbles in demo"), &g_Config.m_BcChatBubblesDemo, &Content, LineSize);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcChatBubbleSize, &g_Config.m_BcChatBubbleSize, &Button, Localize("Chat bubble size"), 20, 30);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		DoSliderWithScaledValue(&g_Config.m_BcChatBubbleShowTime, &g_Config.m_BcChatBubbleShowTime, &Button, Localize("Show for"), 200, 1000, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "s");

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		DoSliderWithScaledValue(&g_Config.m_BcChatBubbleFadeIn, &g_Config.m_BcChatBubbleFadeIn, &Button, Localize("Fade in"), 15, 100, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "s");

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		DoSliderWithScaledValue(&g_Config.m_BcChatBubbleFadeOut, &g_Config.m_BcChatBubbleFadeOut, &Button, Localize("Fade out"), 15, 100, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "s");

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatBubbleAnimation, Localize("Stack animation"), &g_Config.m_BcChatBubbleAnimation, &Content, LineSize);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcChatBubbleRounding, &g_Config.m_BcChatBubbleRounding, &Button, Localize("Corner rounding"), 0, 30);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatBubbleCustomColors, Localize("Custom colors"), &g_Config.m_BcChatBubbleCustomColors, &Content, LineSize);

		if(ChatBubblesShowCustomColors)
		{
			static CButtonContainer s_ChatBubbleBgColorButton;
			DoLine_ColorPicker(&s_ChatBubbleBgColorButton, ChatBubbleColorPickerLineSize, 13.0f, ChatBubbleColorPickerSpacing, &MainView, Localize("Background"), &g_Config.m_BcChatBubbleBgColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::BcChatBubbleBgColor, true)), false, nullptr, true);

			static CButtonContainer s_ChatBubbleTextColorButton;
			DoLine_ColorPicker(&s_ChatBubbleTextColorButton, ChatBubbleColorPickerLineSize, 13.0f, ChatBubbleColorPickerSpacing, &MainView, Localize("Text"), &g_Config.m_BcChatBubbleTextColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::BcChatBubbleTextColor, true)), false, nullptr, true);

			static CButtonContainer s_ChatBubbleOutlineColorButton;
			DoLine_ColorPicker(&s_ChatBubbleOutlineColorButton, ChatBubbleColorPickerLineSize, 13.0f, ChatBubbleColorPickerSpacing, &MainView, Localize("Outline"), &g_Config.m_BcChatBubbleOutlineColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::BcChatBubbleOutlineColor, true)), false, nullptr, true);
		}

		Ui()->ClipDisable();
	}

	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	// Gradient (left column block)
	const bool GradientNicknames = g_Config.m_BcNameplateGradient != 0;
	const bool GradientClans = g_Config.m_BcNameplateGradientClan != 0;
	const bool GradientSkin = g_Config.m_BcNameplateGradientSkin != 0;
	const bool GradientEverything = g_Config.m_BcNameplateGradientEverything != 0;
	const bool GradientExpanded = GradientNicknames || GradientClans || GradientSkin || GradientEverything;
	const bool GradientShowModeOptions = GradientExpanded;
	const bool GradientShowCustomColors = GradientShowModeOptions && g_Config.m_BcNameplateGradientMode == BC_GRADIENT_MODE_CUSTOM;
	const float GradientColorPickerLineSize = 25.0f;
	const float GradientColorPickerSpacing = 5.0f;
	const int GradientCustomColorCount = std::clamp(g_Config.m_BcNameplateGradientColorCount, 2, 4);
	static float s_GradientRevealPhase = 0.0f;
	UpdateModuleRevealPhase(s_GradientRevealPhase, GradientShowModeOptions, Client()->RenderFrameTime());
	const float GradientHeaderHeight = LineSize + MarginSmall + 4.0f * LineSize;
	// Expanded: animate speed + mode label/buttons (+ color count label/buttons + pickers when custom)
	const float GradientExpandedTargetHeight = MarginSmall + LineSize + MarginSmall + LineSize + LineSize + (GradientShowCustomColors ? MarginSmall + LineSize + LineSize + MarginSmall + GradientCustomColorCount * (GradientColorPickerLineSize + GradientColorPickerSpacing) : 0.0f);
	const float GradientExpandedHeight = GradientExpandedTargetHeight * BCUiAnimations::EaseOutCubic(s_GradientRevealPhase);
	const float GradientBlockHeight = GradientHeaderHeight + GradientExpandedHeight;

	CUIRect GradientBlock;
	Column.HSplitTop(GradientBlockHeight, &GradientBlock, &Column);

	CUIRect GradientBlockBg = GradientBlock;
	GradientBlockBg.w += BlockPadding;
	GradientBlockBg.h += BlockPadding;
	GradientBlockBg.x -= BlockPadding * 0.5f;
	GradientBlockBg.y -= BlockPadding * 0.5f;
	GradientBlockBg.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);

	MainView = GradientBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	CUIRect GradientTitleLabel, GradientResetButton;
	Label.VSplitRight(LineSize + 8.0f, &GradientTitleLabel, &GradientResetButton);
	static CButtonContainer s_GradientResetButton;
	const bool GradientResetClicked = Ui()->DoButton_FontIcon(&s_GradientResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &GradientResetButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_GradientResetButton, &GradientResetButton, Localize("Reset to defaults"));
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
	}
	GradientTitleLabel.VSplitRight(MarginSmall, &GradientTitleLabel, nullptr);
	DrawBcMenuBadge(Graphics(), Ui(), TextRender(), &GradientTitleLabel, Localize("NEW"), 12.0f,
		ColorRGBA(0.25f, 0.85f, 0.40f, 1.0f), ColorRGBA(0.10f, 0.60f, 0.25f, 1.0f), MarginSmall);
	Ui()->DoLabel(&GradientTitleLabel, Localize("Gradient"), HeadlineFontSize, TEXTALIGN_ML);

	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcNameplateGradient, Localize("Gradient nicknames"), &g_Config.m_BcNameplateGradient, &Content, LineSize);
	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcNameplateGradientClan, Localize("Gradient clans"), &g_Config.m_BcNameplateGradientClan, &Content, LineSize);
	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcNameplateGradientSkin, Localize("Gradient skin"), &g_Config.m_BcNameplateGradientSkin, &Content, LineSize);
	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcNameplateGradientEverything, Localize("Gradient everything"), &g_Config.m_BcNameplateGradientEverything, &Content, LineSize);

	if(GradientExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = GradientExpandedHeight;
		Ui()->ClipEnable(&Visible);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcNameplateGradientAnimateSpeed, &g_Config.m_BcNameplateGradientAnimateSpeed, &Button, Localize("Animate speed"), 1, 100, &CUi::ms_LinearScrollbarScale, 0, "%");

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		CUIRect GradientModeLabel, GradientModeRow;
		MainView.HSplitTop(LineSize, &GradientModeLabel, &MainView);
		Ui()->DoLabel(&GradientModeLabel, Localize("Mode"), 14.0f, TEXTALIGN_ML);

		MainView.HSplitTop(LineSize, &GradientModeRow, &MainView);
		{
			static CButtonContainer s_GradientModeSkin, s_GradientModeCustom, s_GradientModeRainbow;
			CUIRect SkinButton, CustomButton, RainbowButton;
			GradientModeRow.VSplitLeft(GradientModeRow.w / 3.0f, &SkinButton, &GradientModeRow);
			GradientModeRow.VSplitLeft(GradientModeRow.w / 2.0f, &CustomButton, &RainbowButton);
			g_Config.m_BcNameplateGradientMode = std::clamp(g_Config.m_BcNameplateGradientMode, 0, 2);
			if(DoButton_Menu(&s_GradientModeSkin, Localize("Skin"), g_Config.m_BcNameplateGradientMode == BC_GRADIENT_MODE_SKIN, &SkinButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
				g_Config.m_BcNameplateGradientMode = BC_GRADIENT_MODE_SKIN;
			if(DoButton_Menu(&s_GradientModeCustom, Localize("Custom"), g_Config.m_BcNameplateGradientMode == BC_GRADIENT_MODE_CUSTOM, &CustomButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
				g_Config.m_BcNameplateGradientMode = BC_GRADIENT_MODE_CUSTOM;
			if(DoButton_Menu(&s_GradientModeRainbow, Localize("Rainbow"), g_Config.m_BcNameplateGradientMode == BC_GRADIENT_MODE_RAINBOW, &RainbowButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
				g_Config.m_BcNameplateGradientMode = BC_GRADIENT_MODE_RAINBOW;
		}

		if(GradientShowCustomColors)
		{
			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			CUIRect GradientColorCountLabel, GradientColorCountRow;
			MainView.HSplitTop(LineSize, &GradientColorCountLabel, &MainView);
			Ui()->DoLabel(&GradientColorCountLabel, Localize("Color count"), 14.0f, TEXTALIGN_ML);

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
			DoLine_ColorPicker(&s_GradientColor1Button, GradientColorPickerLineSize, 13.0f, GradientColorPickerSpacing, &MainView, Localize("Color 1"), &g_Config.m_BcNameplateGradientColor1, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::BcNameplateGradientColor1, true)), false);
			DoLine_ColorPicker(&s_GradientColor2Button, GradientColorPickerLineSize, 13.0f, GradientColorPickerSpacing, &MainView, Localize("Color 2"), &g_Config.m_BcNameplateGradientColor2, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::BcNameplateGradientColor2, true)), false);
			if(GradientCustomColorCount >= 3)
				DoLine_ColorPicker(&s_GradientColor3Button, GradientColorPickerLineSize, 13.0f, GradientColorPickerSpacing, &MainView, Localize("Color 3"), &g_Config.m_BcNameplateGradientColor3, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::BcNameplateGradientColor3, true)), false);
			if(GradientCustomColorCount >= 4)
				DoLine_ColorPicker(&s_GradientColor4Button, GradientColorPickerLineSize, 13.0f, GradientColorPickerSpacing, &MainView, Localize("Color 4"), &g_Config.m_BcNameplateGradientColor4, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::BcNameplateGradientColor4, true)), false);
		}

		Ui()->ClipDisable();
	}

	// Hook combo (left column block)
	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	const bool Expanded = g_Config.m_BcHookCombo != 0;
	static float s_HookComboRevealPhase = 0.0f;
	UpdateModuleRevealPhase(s_HookComboRevealPhase, Expanded, Client()->RenderFrameTime());
	const float HeaderHeight = LineSize + MarginSmall + LineSize;
	const float ExpandedTargetHeight = MarginSmall + LineSize + LineSize + MarginSmall + LineSize + MarginSmall + LineSize + MarginSmall + LineSize;
	const float ExpandedHeight = ExpandedTargetHeight * BCUiAnimations::EaseOutCubic(s_HookComboRevealPhase);
	const float BlockHeight = HeaderHeight + ExpandedHeight;

	CUIRect Block;
	Column.HSplitTop(BlockHeight, &Block, &Column);

	CUIRect BlockBg = Block;
	BlockBg.w += BlockPadding;
	BlockBg.h += BlockPadding;
	BlockBg.x -= BlockPadding * 0.5f;
	BlockBg.y -= BlockPadding * 0.5f;
	BlockBg.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);

	MainView = Block;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	CUIRect TitleLabel, ResetButton;
	Label.VSplitRight(LineSize + 8.0f, &TitleLabel, &ResetButton);
	static CButtonContainer s_HookComboResetButton;
	const bool HookComboResetClicked = Ui()->DoButton_FontIcon(&s_HookComboResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &ResetButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_HookComboResetButton, &ResetButton, Localize("Reset to defaults"));
	if(HookComboResetClicked)
	{
		g_Config.m_BcHookComboMode = DefaultConfig::BcHookComboMode;
		g_Config.m_BcHookComboResetTime = DefaultConfig::BcHookComboResetTime;
		g_Config.m_BcHookComboSoundVolume = DefaultConfig::BcHookComboSoundVolume;
		g_Config.m_BcHookComboSize = DefaultConfig::BcHookComboSize;
	}
	Ui()->DoLabel(&TitleLabel, Localize("Hook combo"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcHookCombo, Localize("Enable Hook combo"), &g_Config.m_BcHookCombo, &Content, LineSize);

	if(ExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = ExpandedHeight;
		Ui()->ClipEnable(&Visible);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);

		CUIRect ModeLabel, ModeRow;
		MainView.HSplitTop(LineSize, &ModeLabel, &MainView);
		Ui()->DoLabel(&ModeLabel, Localize("Mode"), 14.0f, TEXTALIGN_ML);

		MainView.HSplitTop(LineSize, &ModeRow, &MainView);
		CUIRect HookButton, HammerButton, HookHammerButton;
		ModeRow.VSplitLeft(ModeRow.w / 3.0f, &HookButton, &ModeRow);
		ModeRow.VSplitLeft(ModeRow.w / 2.0f, &HammerButton, &HookHammerButton);

		static CButtonContainer s_HookComboModeHook;
		static CButtonContainer s_HookComboModeHammer;
		static CButtonContainer s_HookComboModeHookHammer;
		if(DoButton_Menu(&s_HookComboModeHook, Localize("hook"), g_Config.m_BcHookComboMode == 0, &HookButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
			g_Config.m_BcHookComboMode = 0;
		if(DoButton_Menu(&s_HookComboModeHammer, Localize("hammer"), g_Config.m_BcHookComboMode == 1, &HammerButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
			g_Config.m_BcHookComboMode = 1;
		if(DoButton_Menu(&s_HookComboModeHookHammer, Localize("hook&hammer"), g_Config.m_BcHookComboMode == 2, &HookHammerButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
			g_Config.m_BcHookComboMode = 2;

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		DoSliderWithScaledValue(&g_Config.m_BcHookComboResetTime, &g_Config.m_BcHookComboResetTime, &Button, Localize("Max time between hooks"), 100, 5000, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		DoSliderWithScaledValue(&g_Config.m_BcHookComboSoundVolume, &g_Config.m_BcHookComboSoundVolume, &Button, Localize("Hook combo sound volume"), 0, 100, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "%");

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		DoSliderWithScaledValue(&g_Config.m_BcHookComboSize, &g_Config.m_BcHookComboSize, &Button, Localize("Hook combo size"), 50, 200, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "%");

		Ui()->ClipDisable();
	}

	// Jelly tee (left column block)
	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	const bool JellyExpanded = g_Config.m_BcJellyTee != 0;
	static float s_JellyRevealPhase = 0.0f;
	UpdateModuleRevealPhase(s_JellyRevealPhase, JellyExpanded, Client()->RenderFrameTime());
	const float JellyHeaderHeight = LineSize + MarginSmall + LineSize;
	const float JellyExpandedTargetHeight = MarginSmall + LineSize + MarginSmall + LineSize + MarginSmall + LineSize;
	const float JellyExpandedHeight = JellyExpandedTargetHeight * BCUiAnimations::EaseOutCubic(s_JellyRevealPhase);
	const float JellyBlockHeight = JellyHeaderHeight + JellyExpandedHeight;

	CUIRect JellyBlock;
	Column.HSplitTop(JellyBlockHeight, &JellyBlock, &Column);

	CUIRect JellyBlockBg = JellyBlock;
	JellyBlockBg.w += BlockPadding;
	JellyBlockBg.h += BlockPadding;
	JellyBlockBg.x -= BlockPadding * 0.5f;
	JellyBlockBg.y -= BlockPadding * 0.5f;
	JellyBlockBg.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);

	MainView = JellyBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	CUIRect JellyTitleLabel, JellyResetButton;
	Label.VSplitRight(LineSize + 8.0f, &JellyTitleLabel, &JellyResetButton);
	static CButtonContainer s_JellyTeeResetButton;
	const bool JellyTeeResetClicked = Ui()->DoButton_FontIcon(&s_JellyTeeResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &JellyResetButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_JellyTeeResetButton, &JellyResetButton, Localize("Reset to defaults"));
	if(JellyTeeResetClicked)
	{
		g_Config.m_BcJellyTeeOthers = DefaultConfig::BcJellyTeeOthers;
		g_Config.m_BcJellyTeeStrength = DefaultConfig::BcJellyTeeStrength;
		g_Config.m_BcJellyTeeDuration = DefaultConfig::BcJellyTeeDuration;
	}
	Ui()->DoLabel(&JellyTitleLabel, Localize("Jelly Tee"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcJellyTee, Localize("Enable Jelly Tee"), &g_Config.m_BcJellyTee, &Content, LineSize);

	if(JellyExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = JellyExpandedHeight;
		Ui()->ClipEnable(&Visible);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcJellyTeeOthers, Localize("Jelly Others"), &g_Config.m_BcJellyTeeOthers, &Content, LineSize);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcJellyTeeStrength, &g_Config.m_BcJellyTeeStrength, &Button, Localize("Jelly strength"), 0, 1000);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcJellyTeeDuration, &g_Config.m_BcJellyTeeDuration, &Button, Localize("Jelly duration"), 1, 500);

		Ui()->ClipDisable();
	}

	// 3D particles (left column block)
	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	const float Particles3DColorPickerLineSize = 25.0f;
	const float Particles3DColorPickerLabelSize = 13.0f;
	const float Particles3DColorPickerSpacing = 5.0f;
	const bool Particles3DEnabled = g_Config.m_Bc3dParticles != 0;
	const bool Particles3DShowCustomColor = Particles3DEnabled && g_Config.m_Bc3dParticlesColorMode == 1;
	const bool Particles3DShowGlowOptions = Particles3DEnabled && g_Config.m_Bc3dParticlesGlow != 0;
	static float s_Particles3DRevealPhase = 0.0f;
	static float s_Particles3DGlowRevealPhase = 0.0f;
	UpdateModuleRevealPhase(s_Particles3DRevealPhase, Particles3DEnabled, Client()->RenderFrameTime());
	UpdateModuleRevealPhase(s_Particles3DGlowRevealPhase, Particles3DShowGlowOptions, Client()->RenderFrameTime());
	const float Particles3DGlowTargetHeight = (MarginSmall + LineSize) * 2.0f;
	const float Particles3DExpandedTargetHeight = (MarginSmall + LineSize) * 6.0f + (Particles3DShowCustomColor ? Particles3DColorPickerLineSize + Particles3DColorPickerSpacing : 0.0f) + MarginSmall + LineSize + Particles3DGlowTargetHeight * BCUiAnimations::EaseOutCubic(s_Particles3DGlowRevealPhase);
	const float Particles3DExpandedHeight = Particles3DExpandedTargetHeight * BCUiAnimations::EaseOutCubic(s_Particles3DRevealPhase);
	const float Particles3DContentHeight = LineSize + MarginSmall + LineSize + Particles3DExpandedHeight;

	CUIRect Particles3DBlock;
	Column.HSplitTop(Particles3DContentHeight, &Particles3DBlock, &Column);

	CUIRect Particles3DBlockBg = Particles3DBlock;
	Particles3DBlockBg.w += BlockPadding;
	Particles3DBlockBg.h += BlockPadding;
	Particles3DBlockBg.x -= BlockPadding * 0.5f;
	Particles3DBlockBg.y -= BlockPadding * 0.5f;
	Particles3DBlockBg.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);

	MainView = Particles3DBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	CUIRect Particles3DTitleLabel, Particles3DResetButton;
	Label.VSplitRight(LineSize + 8.0f, &Particles3DTitleLabel, &Particles3DResetButton);
	static CButtonContainer s_3DParticlesResetButton;
	const bool Particles3DResetClicked = Ui()->DoButton_FontIcon(&s_3DParticlesResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &Particles3DResetButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_3DParticlesResetButton, &Particles3DResetButton, Localize("Reset to defaults"));
	if(Particles3DResetClicked)
	{
		g_Config.m_Bc3dParticlesType = DefaultConfig::Bc3dParticlesType;
		g_Config.m_Bc3dParticlesCount = DefaultConfig::Bc3dParticlesCount;
		g_Config.m_Bc3dParticlesSizeMin = DefaultConfig::Bc3dParticlesSizeMin;
		g_Config.m_Bc3dParticlesSizeMax = DefaultConfig::Bc3dParticlesSizeMax;
		g_Config.m_Bc3dParticlesSpeed = DefaultConfig::Bc3dParticlesSpeed;
		g_Config.m_Bc3dParticlesDepth = DefaultConfig::Bc3dParticlesDepth;
		g_Config.m_Bc3dParticlesAlpha = DefaultConfig::Bc3dParticlesAlpha;
		g_Config.m_Bc3dParticlesFadeInMs = DefaultConfig::Bc3dParticlesFadeInMs;
		g_Config.m_Bc3dParticlesFadeOutMs = DefaultConfig::Bc3dParticlesFadeOutMs;
		g_Config.m_Bc3dParticlesPushRadius = DefaultConfig::Bc3dParticlesPushRadius;
		g_Config.m_Bc3dParticlesPushStrength = DefaultConfig::Bc3dParticlesPushStrength;
		g_Config.m_Bc3dParticlesCollide = DefaultConfig::Bc3dParticlesCollide;
		g_Config.m_Bc3dParticlesViewMargin = DefaultConfig::Bc3dParticlesViewMargin;
		g_Config.m_Bc3dParticlesColorMode = DefaultConfig::Bc3dParticlesColorMode;
		g_Config.m_Bc3dParticlesColor = DefaultConfig::Bc3dParticlesColor;
		g_Config.m_Bc3dParticlesGlow = DefaultConfig::Bc3dParticlesGlow;
		g_Config.m_Bc3dParticlesGlowAlpha = DefaultConfig::Bc3dParticlesGlowAlpha;
		g_Config.m_Bc3dParticlesGlowOffset = DefaultConfig::Bc3dParticlesGlowOffset;
	}
	Ui()->DoLabel(&Particles3DTitleLabel, Localize("3D Particles"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_Bc3dParticles, Localize("Enable 3D Particles"), &g_Config.m_Bc3dParticles, &Content, LineSize);

	if(Particles3DExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = Particles3DExpandedHeight;
		Ui()->ClipEnable(&Visible);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesCount, &g_Config.m_Bc3dParticlesCount, &Button, Localize("Particles count"), 1, 200);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		CUIRect Particles3DTypeLabel, Particles3DTypeSelect;
		Button.VSplitLeft(150.0f, &Particles3DTypeLabel, &Particles3DTypeSelect);
		Ui()->DoLabel(&Particles3DTypeLabel, Localize("Particle type"), 14.0f, TEXTALIGN_ML);

		static CUi::SDropDownState s_3DParticlesTypeState;
		static CScrollRegion s_3DParticlesTypeScrollRegion;
		s_3DParticlesTypeState.m_SelectionPopupContext.m_pScrollRegion = &s_3DParticlesTypeScrollRegion;
		const char *Ap3DParticleTypes[3] = {
			Localize("Cube"),
			Localize("Heart"),
			Localize("Mixed"),
		};
		g_Config.m_Bc3dParticlesType = Ui()->DoDropDown(&Particles3DTypeSelect, g_Config.m_Bc3dParticlesType - 1, Ap3DParticleTypes, (int)std::size(Ap3DParticleTypes), s_3DParticlesTypeState) + 1;

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesSizeMax, &g_Config.m_Bc3dParticlesSizeMax, &Button, Localize("Size"), 2, 200);
		g_Config.m_Bc3dParticlesSizeMin = std::max(2, g_Config.m_Bc3dParticlesSizeMax - 3);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesSpeed, &g_Config.m_Bc3dParticlesSpeed, &Button, Localize("Speed"), 1, 500);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesAlpha, &g_Config.m_Bc3dParticlesAlpha, &Button, Localize("Alpha"), 1, 100);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		CUIRect Particles3DColorModeLabel, Particles3DColorModeSelect;
		Button.VSplitLeft(150.0f, &Particles3DColorModeLabel, &Particles3DColorModeSelect);
		Ui()->DoLabel(&Particles3DColorModeLabel, Localize("Color mode"), 14.0f, TEXTALIGN_ML);

		static CUi::SDropDownState s_3DParticlesColorModeState;
		static CScrollRegion s_3DParticlesColorModeScrollRegion;
		s_3DParticlesColorModeState.m_SelectionPopupContext.m_pScrollRegion = &s_3DParticlesColorModeScrollRegion;
		const char *Ap3DParticleColorModes[2] = {
			Localize("Custom"),
			Localize("Random"),
		};
		g_Config.m_Bc3dParticlesColorMode = Ui()->DoDropDown(&Particles3DColorModeSelect, g_Config.m_Bc3dParticlesColorMode - 1, Ap3DParticleColorModes, (int)std::size(Ap3DParticleColorModes), s_3DParticlesColorModeState) + 1;

		if(Particles3DShowCustomColor)
		{
			static CButtonContainer s_3DParticlesColorButton;
			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			DoLine_ColorPicker(&s_3DParticlesColorButton, Particles3DColorPickerLineSize, Particles3DColorPickerLabelSize, Particles3DColorPickerSpacing, &MainView, Localize("Color"), &g_Config.m_Bc3dParticlesColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
		}

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_Bc3dParticlesGlow, Localize("Glow"), &g_Config.m_Bc3dParticlesGlow, &Content, LineSize);

		const float Particles3DGlowExpandedHeight = Particles3DGlowTargetHeight * BCUiAnimations::EaseOutCubic(s_Particles3DGlowRevealPhase);
		if(Particles3DGlowExpandedHeight > 0.5f)
		{
			CUIRect GlowVisible = MainView;
			GlowVisible.h = Particles3DGlowExpandedHeight;
			Ui()->ClipEnable(&GlowVisible);

			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			MainView.HSplitTop(LineSize, &Button, &MainView);
			Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesGlowAlpha, &g_Config.m_Bc3dParticlesGlowAlpha, &Button, Localize("Glow alpha"), 1, 100);

			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			MainView.HSplitTop(LineSize, &Button, &MainView);
			Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesGlowOffset, &g_Config.m_Bc3dParticlesGlowOffset, &Button, Localize("Glow offset"), 1, 20);

			Ui()->ClipDisable();
		}

		Ui()->ClipDisable();
	}

	// Media background (left column block)
	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	const float MediaBackgroundBlockHeight = LineSize + MarginSmall + LineSize + MarginSmall + LineSize + MarginSmall + LineSize + MarginSmall + LineSize + MarginSmall + LineSize;

	CUIRect MediaBackgroundBlock;
	Column.HSplitTop(MediaBackgroundBlockHeight, &MediaBackgroundBlock, &Column);

	CUIRect MediaBackgroundBlockBg = MediaBackgroundBlock;
	MediaBackgroundBlockBg.w += BlockPadding;
	MediaBackgroundBlockBg.h += BlockPadding;
	MediaBackgroundBlockBg.x -= BlockPadding * 0.5f;
	MediaBackgroundBlockBg.y -= BlockPadding * 0.5f;
	MediaBackgroundBlockBg.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);

	MainView = MediaBackgroundBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	Ui()->DoLabel(&Label, Localize("Media Background"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Content, &MainView);
	const bool MenuMediaChanged = DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMenuMediaBackground, Localize("Enable to main menu"), &g_Config.m_BcMenuMediaBackground, &Content, LineSize);

	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	MainView.HSplitTop(LineSize, &Content, &MainView);
	const bool GameMediaChanged = DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcGameMediaBackground, Localize("Enable to game background"), &g_Config.m_BcGameMediaBackground, &Content, LineSize);

	if(MenuMediaChanged || GameMediaChanged)
		m_MenuMediaBackground.ReloadFromConfig();

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
		DoButton_Menu(&s_MenuMediaEmptyButton, Localize("No media files in backgrounds folder"), -1, &MediaFileDropDown);
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
			m_MenuMediaBackground.ReloadFromConfig();
		}
	}

	static CButtonContainer s_MenuMediaReloadButton;
	if(Ui()->DoButton_FontIcon(&s_MenuMediaReloadButton, FontIcon::ARROW_ROTATE_RIGHT, 0, &MediaReloadButton, BUTTONFLAG_LEFT))
		m_MenuMediaBackground.ReloadFromConfig();

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
	Ui()->DoScrollbarOption(&g_Config.m_BcGameMediaBackgroundOffset, &g_Config.m_BcGameMediaBackgroundOffset, &Button, Localize("Map offset"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");
	GameClient()->m_Tooltips.DoToolTip(&g_Config.m_BcGameMediaBackgroundOffset, &Button, Localize("0 keeps the image fixed to the screen. 100 fixes it to the map for a full parallax effect."));

	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	MainView.HSplitTop(LineSize, &Button, &MainView);
	// In-game the menu instance is always unloaded, so show the game instance status instead.
	const bool MediaStatusInGame = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
	const CMenuMediaBackground &MediaStatusSource = MediaStatusInGame ? GameClient()->m_Background.MediaBackground() : m_MenuMediaBackground;
	const char *pMediaStatusText = MediaStatusSource.StatusText();
	char aMediaStatusBuf[256];
	if(MediaStatusInGame && MediaStatusSource.IsLoaded() && g_Config.m_BcGameMediaBackground && g_Config.m_ClOverlayEntities == 0)
	{
		str_format(aMediaStatusBuf, sizeof(aMediaStatusBuf), "%s %s", pMediaStatusText, Localize("Hidden: overlay entities is 0."));
		pMediaStatusText = aMediaStatusBuf;
		TextRender()->TextColor(ColorRGBA(1.0f, 0.85f, 0.45f, 1.0f));
	}
	else if(MediaStatusSource.HasError())
		TextRender()->TextColor(ColorRGBA(1.0f, 0.45f, 0.45f, 1.0f));
	else if(MediaStatusSource.IsLoaded())
		TextRender()->TextColor(ColorRGBA(0.55f, 1.0f, 0.55f, 1.0f));
	Ui()->DoLabel(&Button, pMediaStatusText, 11.0f, TEXTALIGN_ML);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	// Eye comfort (left column block)
	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	const bool EyeComfortExpanded = g_Config.m_BcEyeComfort != 0;
	static float s_EyeComfortRevealPhase = 0.0f;
	UpdateModuleRevealPhase(s_EyeComfortRevealPhase, EyeComfortExpanded, Client()->RenderFrameTime());
	const float EyeComfortHeaderHeight = LineSize + MarginSmall + LineSize;
	const float EyeComfortExpandedTargetHeight = MarginSmall + LineSize;
	const float EyeComfortExpandedHeight = EyeComfortExpandedTargetHeight * BCUiAnimations::EaseOutCubic(s_EyeComfortRevealPhase);
	const float EyeComfortBlockHeight = EyeComfortHeaderHeight + EyeComfortExpandedHeight;

	CUIRect EyeComfortBlock;
	Column.HSplitTop(EyeComfortBlockHeight, &EyeComfortBlock, &Column);

	CUIRect EyeComfortBlockBg = EyeComfortBlock;
	EyeComfortBlockBg.w += BlockPadding;
	EyeComfortBlockBg.h += BlockPadding;
	EyeComfortBlockBg.x -= BlockPadding * 0.5f;
	EyeComfortBlockBg.y -= BlockPadding * 0.5f;
	EyeComfortBlockBg.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);

	MainView = EyeComfortBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	CUIRect EyeComfortTitleLabel, EyeComfortResetButton;
	Label.VSplitRight(LineSize + 8.0f, &EyeComfortTitleLabel, &EyeComfortResetButton);
	static CButtonContainer s_EyeComfortResetButton;
	const bool EyeComfortResetClicked = Ui()->DoButton_FontIcon(&s_EyeComfortResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &EyeComfortResetButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_EyeComfortResetButton, &EyeComfortResetButton, Localize("Reset to defaults"));
	if(EyeComfortResetClicked)
		g_Config.m_BcEyeComfortStrength = DefaultConfig::BcEyeComfortStrength;
	Ui()->DoLabel(&EyeComfortTitleLabel, Localize("Eye Comfort"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcEyeComfort, Localize("Enable Eye Comfort"), &g_Config.m_BcEyeComfort, &Content, LineSize);

	if(EyeComfortExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = EyeComfortExpandedHeight;
		Ui()->ClipEnable(&Visible);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcEyeComfortStrength, &g_Config.m_BcEyeComfortStrength, &Button, Localize("Comfort level"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");

		Ui()->ClipDisable();
	}

	// Sweat weapon (left column block)
	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	const float SweatLaserPreviewHeight = 58.0f;
	const float SweatWeaponContentHeight = LineSize + MarginSmall + LineSize + MarginSmall + LineSize + SweatLaserPreviewHeight + MarginSmall + LineSize + SweatLaserPreviewHeight;

	CUIRect SweatWeaponBlock;
	Column.HSplitTop(SweatWeaponContentHeight, &SweatWeaponBlock, &Column);

	CUIRect SweatWeaponBlockBg = SweatWeaponBlock;
	SweatWeaponBlockBg.w += BlockPadding;
	SweatWeaponBlockBg.h += BlockPadding;
	SweatWeaponBlockBg.x -= BlockPadding * 0.5f;
	SweatWeaponBlockBg.y -= BlockPadding * 0.5f;
	SweatWeaponBlockBg.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);

	MainView = SweatWeaponBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	Ui()->DoLabel(&Label, Localize("Sweat Weapon"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcCrystalLaser, Localize("Enable Sweat Weapon"), &g_Config.m_BcCrystalLaser, &Content, LineSize);

	CUIRect SweatPreviewLabel, SweatPreviewRect;
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	MainView.HSplitTop(LineSize, &SweatPreviewLabel, &MainView);
	Ui()->DoLabel(&SweatPreviewLabel, Localize("Crystal Laser"), 14.0f, TEXTALIGN_ML);
	MainView.HSplitTop(SweatLaserPreviewHeight, &SweatPreviewRect, &MainView);
	DoLaserPreview(&SweatPreviewRect, ColorHSLA(g_Config.m_ClLaserRifleOutlineColor), ColorHSLA(g_Config.m_ClLaserRifleInnerColor), LASERTYPE_RIFLE);

	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	MainView.HSplitTop(LineSize, &SweatPreviewLabel, &MainView);
	Ui()->DoLabel(&SweatPreviewLabel, Localize("Sand Shotgun"), 14.0f, TEXTALIGN_ML);
	MainView.HSplitTop(SweatLaserPreviewHeight, &SweatPreviewRect, &MainView);
	DoLaserPreview(&SweatPreviewRect, ColorHSLA(g_Config.m_ClLaserShotgunOutlineColor), ColorHSLA(g_Config.m_ClLaserShotgunInnerColor), LASERTYPE_SHOTGUN);

	// Flying name plates (left column block)
	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	const bool FlyingNamePlatesExpanded = g_Config.m_BcFlyingNamePlates != 0;
	static float s_FlyingNamePlatesRevealPhase = 0.0f;
	UpdateModuleRevealPhase(s_FlyingNamePlatesRevealPhase, FlyingNamePlatesExpanded, Client()->RenderFrameTime());
	const float FlyingNamePlatesHeaderHeight = LineSize + MarginSmall + LineSize;
	const float FlyingNamePlatesExpandedTargetHeight = MarginSmall + LineSize + MarginSmall + LineSize + MarginSmall + LineSize;
	const float FlyingNamePlatesExpandedHeight = FlyingNamePlatesExpandedTargetHeight * BCUiAnimations::EaseOutCubic(s_FlyingNamePlatesRevealPhase);
	const float FlyingNamePlatesBlockHeight = FlyingNamePlatesHeaderHeight + FlyingNamePlatesExpandedHeight;

	CUIRect FlyingNamePlatesBlock;
	Column.HSplitTop(FlyingNamePlatesBlockHeight, &FlyingNamePlatesBlock, &Column);

	CUIRect FlyingNamePlatesBlockBg = FlyingNamePlatesBlock;
	FlyingNamePlatesBlockBg.w += BlockPadding;
	FlyingNamePlatesBlockBg.h += BlockPadding;
	FlyingNamePlatesBlockBg.x -= BlockPadding * 0.5f;
	FlyingNamePlatesBlockBg.y -= BlockPadding * 0.5f;
	FlyingNamePlatesBlockBg.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);

	MainView = FlyingNamePlatesBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	CUIRect FlyingNamePlatesTitleLabel, FlyingNamePlatesResetButton;
	Label.VSplitRight(LineSize + 8.0f, &FlyingNamePlatesTitleLabel, &FlyingNamePlatesResetButton);
	static CButtonContainer s_FlyingNamePlatesResetButton;
	const bool FlyingNamePlatesResetClicked = Ui()->DoButton_FontIcon(&s_FlyingNamePlatesResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &FlyingNamePlatesResetButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_FlyingNamePlatesResetButton, &FlyingNamePlatesResetButton, Localize("Reset to defaults"));
	if(FlyingNamePlatesResetClicked)
	{
		g_Config.m_BcFlyingNamePlatesLift = DefaultConfig::BcFlyingNamePlatesLift;
		g_Config.m_BcFlyingNamePlatesDrag = DefaultConfig::BcFlyingNamePlatesDrag;
		g_Config.m_BcFlyingNamePlatesFollow = DefaultConfig::BcFlyingNamePlatesFollow;
	}
	Ui()->DoLabel(&FlyingNamePlatesTitleLabel, Localize("Flying Name Plates"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcFlyingNamePlates, Localize("Enable flying name plates"), &g_Config.m_BcFlyingNamePlates, &Content, LineSize);

	if(FlyingNamePlatesExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = FlyingNamePlatesExpandedHeight;
		Ui()->ClipEnable(&Visible);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcFlyingNamePlatesLift, &g_Config.m_BcFlyingNamePlatesLift, &Button, Localize("Lift above player"), 0, 120);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcFlyingNamePlatesDrag, &g_Config.m_BcFlyingNamePlatesDrag, &Button, Localize("Movement drag"), 0, 200);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcFlyingNamePlatesFollow, &g_Config.m_BcFlyingNamePlatesFollow, &Button, Localize("Follow speed"), 1, 100);

		Ui()->ClipDisable();
	}

	const float LeftColumnEndY = Column.y;

	// Motion blur (right column block)
	CUIRect RightColumn = RightView;
	RightColumn.HSplitTop(10.0f, nullptr, &RightColumn);

	const bool MotionBlurExpanded = g_Config.m_BcMotionBlur != 0;
	static float s_MotionBlurRevealPhase = 0.0f;
	UpdateModuleRevealPhase(s_MotionBlurRevealPhase, MotionBlurExpanded, Client()->RenderFrameTime());
	const float MotionBlurExpandedHeight = (MarginSmall + LineSize) * BCUiAnimations::EaseOutCubic(s_MotionBlurRevealPhase);
	const float MotionBlurBlockHeight = LineSize + MarginSmall + LineSize + MotionBlurExpandedHeight;

	CUIRect MotionBlurBlock;
	RightColumn.HSplitTop(MotionBlurBlockHeight, &MotionBlurBlock, &RightColumn);

	CUIRect MotionBlurBlockBg = MotionBlurBlock;
	MotionBlurBlockBg.w += BlockPadding;
	MotionBlurBlockBg.h += BlockPadding;
	MotionBlurBlockBg.x -= BlockPadding * 0.5f;
	MotionBlurBlockBg.y -= BlockPadding * 0.5f;
	MotionBlurBlockBg.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);

	MainView = MotionBlurBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	CUIRect MotionBlurTitleLabel, MotionBlurResetButton;
	Label.VSplitRight(LineSize + 8.0f, &MotionBlurTitleLabel, &MotionBlurResetButton);
	static CButtonContainer s_MotionBlurResetButton;
	const bool MotionBlurResetClicked = Ui()->DoButton_FontIcon(&s_MotionBlurResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &MotionBlurResetButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_MotionBlurResetButton, &MotionBlurResetButton, Localize("Reset to defaults"));
	if(MotionBlurResetClicked)
		g_Config.m_BcMotionBlurStrength = DefaultConfig::BcMotionBlurStrength;
	MotionBlurTitleLabel.VSplitRight(MarginSmall, &MotionBlurTitleLabel, nullptr);
	DrawBcMenuBadge(Graphics(), Ui(), TextRender(), &MotionBlurTitleLabel, Localize("BETA"), 12.0f,
		ColorRGBA(0.95f, 0.25f, 0.25f, 1.0f), ColorRGBA(0.75f, 0.08f, 0.08f, 1.0f), MarginSmall);
	Ui()->DoLabel(&MotionBlurTitleLabel, Localize("Motion Blur"), HeadlineFontSize, TEXTALIGN_ML);

	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMotionBlur, Localize("Enable motion blur"), &g_Config.m_BcMotionBlur, &Content, LineSize);

	if(MotionBlurExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = MotionBlurExpandedHeight;
		Ui()->ClipEnable(&Visible);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		DoSliderWithScaledValue(&g_Config.m_BcMotionBlurStrength, &g_Config.m_BcMotionBlurStrength, &Button, Localize("Blend strength"), 0, 95, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "%");

		Ui()->ClipDisable();
	}

	// Animations (right column block)
	RightColumn.HSplitTop(MarginBetweenViews, nullptr, &RightColumn);

	const bool AnimationsExpanded = g_Config.m_BcAnimations != 0;
	static float s_AnimationsRevealPhase = 0.0f;
	UpdateModuleRevealPhase(s_AnimationsRevealPhase, AnimationsExpanded, Client()->RenderFrameTime());
	const float AnimationsExpandedTargetHeight = 12.0f * (MarginSmall + LineSize);
	const float AnimationsExpandedHeight = AnimationsExpandedTargetHeight * BCUiAnimations::EaseOutCubic(s_AnimationsRevealPhase);
	const float AnimationsBlockHeight = LineSize + MarginSmall + LineSize + AnimationsExpandedHeight;

	CUIRect AnimationsBlock;
	RightColumn.HSplitTop(AnimationsBlockHeight, &AnimationsBlock, &RightColumn);

	CUIRect AnimationsBlockBg = AnimationsBlock;
	AnimationsBlockBg.w += BlockPadding;
	AnimationsBlockBg.h += BlockPadding;
	AnimationsBlockBg.x -= BlockPadding * 0.5f;
	AnimationsBlockBg.y -= BlockPadding * 0.5f;
	AnimationsBlockBg.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);

	MainView = AnimationsBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	CUIRect AnimationsTitleLabel, AnimationsResetButton;
	Label.VSplitRight(LineSize + 8.0f, &AnimationsTitleLabel, &AnimationsResetButton);
	static CButtonContainer s_AnimationsResetButton;
	const bool AnimationsResetClicked = Ui()->DoButton_FontIcon(&s_AnimationsResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &AnimationsResetButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_AnimationsResetButton, &AnimationsResetButton, Localize("Reset to defaults"));
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
	Ui()->DoLabel(&AnimationsTitleLabel, Localize("Animations"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcAnimations, Localize("Enable animations"), &g_Config.m_BcAnimations, &Content, LineSize);

	if(AnimationsExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = AnimationsExpandedHeight;
		Ui()->ClipEnable(&Visible);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcModuleUiRevealAnimation, Localize("Module settings reveals"), &g_Config.m_BcModuleUiRevealAnimation, &Content, LineSize);
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcModuleUiRevealAnimationMs, &g_Config.m_BcModuleUiRevealAnimationMs, &Button, Localize("Module reveal time (ms)"), 1, 500);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatAnimation, Localize("Chat message animations"), &g_Config.m_BcChatAnimation, &Content, LineSize);
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcChatAnimationMs, &g_Config.m_BcChatAnimationMs, &Button, Localize("Chat message animation time (ms)"), 1, 500);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatOpenAnimation, Localize("Chat open animation"), &g_Config.m_BcChatOpenAnimation, &Content, LineSize);
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcChatOpenAnimationMs, &g_Config.m_BcChatOpenAnimationMs, &Button, Localize("Chat open animation time (ms)"), 1, 500);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatTypingAnimation, Localize("Chat typing animation"), &g_Config.m_BcChatTypingAnimation, &Content, LineSize);
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcChatTypingAnimationMs, &g_Config.m_BcChatTypingAnimationMs, &Button, Localize("Chat typing animation time (ms)"), 1, 500);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcKillfeedAnimation, Localize("Killfeed animation"), &g_Config.m_BcKillfeedAnimation, &Content, LineSize);
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcKillfeedAnimationMs, &g_Config.m_BcKillfeedAnimationMs, &Button, Localize("Killfeed animation time (ms)"), 1, 500);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMainMenuAnimation, Localize("Main menu animation"), &g_Config.m_BcMainMenuAnimation, &Content, LineSize);
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcMainMenuAnimationSpeed, &g_Config.m_BcMainMenuAnimationSpeed, &Button, Localize("Main menu animation speed"), 1, 50);

		Ui()->ClipDisable();
	}

	// Music player (right column block)
	RightColumn.HSplitTop(MarginBetweenViews, nullptr, &RightColumn);

	const float MusicPlayerColorPickerLineSize = 25.0f;
	const float MusicPlayerColorPickerLabelSize = 13.0f;
	const float MusicPlayerColorPickerSpacing = 5.0f;
	const bool MusicPlayerEnabled = g_Config.m_BcMusicPlayer != 0;
	const bool MusicPlayerShowStaticColor = MusicPlayerEnabled && g_Config.m_BcMusicPlayerColorMode == 0;
	static float s_MusicPlayerRevealPhase = 0.0f;
	UpdateModuleRevealPhase(s_MusicPlayerRevealPhase, MusicPlayerEnabled, Client()->RenderFrameTime());
	const float MusicPlayerExpandedTargetHeight = (MarginSmall + LineSize) * 8.0f + (MusicPlayerShowStaticColor ? MusicPlayerColorPickerSpacing + MusicPlayerColorPickerLineSize : 0.0f);
	const float MusicPlayerExpandedHeight = MusicPlayerExpandedTargetHeight * BCUiAnimations::EaseOutCubic(s_MusicPlayerRevealPhase);
	const float MusicPlayerContentHeight = LineSize + MarginSmall + LineSize + MusicPlayerExpandedHeight;

	CUIRect MusicPlayerBlock;
	RightColumn.HSplitTop(MusicPlayerContentHeight, &MusicPlayerBlock, &RightColumn);

	CUIRect MusicPlayerBlockBg = MusicPlayerBlock;
	MusicPlayerBlockBg.w += BlockPadding;
	MusicPlayerBlockBg.h += BlockPadding;
	MusicPlayerBlockBg.x -= BlockPadding * 0.5f;
	MusicPlayerBlockBg.y -= BlockPadding * 0.5f;
	MusicPlayerBlockBg.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);

	MainView = MusicPlayerBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	CUIRect MusicPlayerTitleLabel, MusicPlayerHudEditorButton, MusicPlayerResetButton;
	Label.VSplitRight(LineSize + 8.0f, &Label, &MusicPlayerResetButton);
	Label.VSplitRight(MarginSmall, &Label, nullptr);
	Label.VSplitRight(LineSize + 8.0f, &MusicPlayerTitleLabel, &MusicPlayerHudEditorButton);
	static CButtonContainer s_MusicPlayerResetButton;
	const bool MusicPlayerResetClicked = Ui()->DoButton_FontIcon(&s_MusicPlayerResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &MusicPlayerResetButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_MusicPlayerResetButton, &MusicPlayerResetButton, Localize("Reset to defaults"));
	if(MusicPlayerResetClicked)
	{
		g_Config.m_BcMusicPlayerColorMode = DefaultConfig::BcMusicPlayerColorMode;
		g_Config.m_BcMusicPlayerStaticColor = DefaultConfig::BcMusicPlayerStaticColor;
		g_Config.m_BcMusicPlayerSizeMode = DefaultConfig::BcMusicPlayerSizeMode;
		g_Config.m_BcMusicPlayerTextScale = DefaultConfig::BcMusicPlayerTextScale;
		g_Config.m_BcMusicPlayerVisualizerMode = DefaultConfig::BcMusicPlayerVisualizerMode;
		g_Config.m_BcMusicPlayerVisualizerSensitivity = DefaultConfig::BcMusicPlayerVisualizerSensitivity;
		g_Config.m_BcMusicPlayerVisualizerSmoothing = DefaultConfig::BcMusicPlayerVisualizerSmoothing;
		g_Config.m_BcMusicPlayerVisualizerRounding = DefaultConfig::BcMusicPlayerVisualizerRounding;
		g_Config.m_BcMusicPlayerVisualizerColumns = DefaultConfig::BcMusicPlayerVisualizerColumns;
	}
	static CButtonContainer s_MusicPlayerHudEditorButton;
	const bool MusicPlayerCanOpenHudEditor = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
	const bool MusicPlayerHudEditorClicked = Ui()->DoButton_FontIcon(&s_MusicPlayerHudEditorButton, FontIcon::UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, MusicPlayerCanOpenHudEditor ? 0 : -1, &MusicPlayerHudEditorButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_MusicPlayerHudEditorButton, &MusicPlayerHudEditorButton, MusicPlayerCanOpenHudEditor ? Localize("Open in HUD editor") : Localize("Join a game first"));
	GameClient()->m_Tooltips.SetFadeTime(&s_MusicPlayerHudEditorButton, 0.0f);
	if(MusicPlayerHudEditorClicked && MusicPlayerCanOpenHudEditor)
	{
		SetActive(false);
		GameClient()->m_HudEditor.Activate();
	}
	Ui()->DoLabel(&MusicPlayerTitleLabel, Localize("Music Player"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMusicPlayer, Localize("Enable music player"), &g_Config.m_BcMusicPlayer, &Content, LineSize);

	if(MusicPlayerExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = MusicPlayerExpandedHeight;
		Ui()->ClipEnable(&Visible);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		CUIRect MusicPlayerColorModeLabel, MusicPlayerColorModeSelect;
		Button.VSplitLeft(150.0f, &MusicPlayerColorModeLabel, &MusicPlayerColorModeSelect);
		Ui()->DoLabel(&MusicPlayerColorModeLabel, Localize("Color mode"), 14.0f, TEXTALIGN_ML);

		static CUi::SDropDownState s_MusicPlayerColorModeState;
		static CScrollRegion s_MusicPlayerColorModeScrollRegion;
		s_MusicPlayerColorModeState.m_SelectionPopupContext.m_pScrollRegion = &s_MusicPlayerColorModeScrollRegion;
		const char *apMusicPlayerColorModes[4] = {
			Localize("Static color"),
			Localize("Cover accent color"),
			Localize("Dominant cover color"),
			Localize("Translucent"),
		};
		g_Config.m_BcMusicPlayerColorMode = std::clamp(g_Config.m_BcMusicPlayerColorMode, 0, 3);
		g_Config.m_BcMusicPlayerColorMode = Ui()->DoDropDown(&MusicPlayerColorModeSelect, g_Config.m_BcMusicPlayerColorMode, apMusicPlayerColorModes, (int)std::size(apMusicPlayerColorModes), s_MusicPlayerColorModeState);

		if(MusicPlayerShowStaticColor)
		{
			static CButtonContainer s_MusicPlayerStaticColorButton;
			MainView.HSplitTop(MusicPlayerColorPickerSpacing, nullptr, &MainView);
			DoLine_ColorPicker(&s_MusicPlayerStaticColorButton, MusicPlayerColorPickerLineSize, MusicPlayerColorPickerLabelSize, MusicPlayerColorPickerSpacing, &MainView, Localize("Static color"), &g_Config.m_BcMusicPlayerStaticColor, ColorRGBA(0.34f, 0.53f, 0.79f, 1.0f), false);
		}

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		CUIRect MusicPlayerSizeModeLabel, MusicPlayerSizeModeSelect;
		Button.VSplitLeft(150.0f, &MusicPlayerSizeModeLabel, &MusicPlayerSizeModeSelect);
		Ui()->DoLabel(&MusicPlayerSizeModeLabel, Localize("Size mode"), 14.0f, TEXTALIGN_ML);

		static CUi::SDropDownState s_MusicPlayerSizeModeState;
		static CScrollRegion s_MusicPlayerSizeModeScrollRegion;
		s_MusicPlayerSizeModeState.m_SelectionPopupContext.m_pScrollRegion = &s_MusicPlayerSizeModeScrollRegion;
		const char *apMusicPlayerSizeModes[2] = {
			Localize("Normal"),
			Localize("Mini"),
		};
		g_Config.m_BcMusicPlayerSizeMode = std::clamp(g_Config.m_BcMusicPlayerSizeMode, 0, 1);
		g_Config.m_BcMusicPlayerSizeMode = Ui()->DoDropDown(&MusicPlayerSizeModeSelect, g_Config.m_BcMusicPlayerSizeMode, apMusicPlayerSizeModes, (int)std::size(apMusicPlayerSizeModes), s_MusicPlayerSizeModeState);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		CUIRect MusicPlayerVisualizerModeLabel, MusicPlayerVisualizerModeSelect;
		Button.VSplitLeft(150.0f, &MusicPlayerVisualizerModeLabel, &MusicPlayerVisualizerModeSelect);
		Ui()->DoLabel(&MusicPlayerVisualizerModeLabel, Localize("Visualizer mode"), 14.0f, TEXTALIGN_ML);

		static CUi::SDropDownState s_MusicPlayerVisualizerModeState;
		static CScrollRegion s_MusicPlayerVisualizerModeScrollRegion;
		s_MusicPlayerVisualizerModeState.m_SelectionPopupContext.m_pScrollRegion = &s_MusicPlayerVisualizerModeScrollRegion;
		const char *apMusicPlayerVisualizerModes[3] = {
			Localize("Bottom"),
			Localize("Center"),
			Localize("Up"),
		};
		g_Config.m_BcMusicPlayerVisualizerMode = std::clamp(g_Config.m_BcMusicPlayerVisualizerMode, 0, 2);
		g_Config.m_BcMusicPlayerVisualizerMode = Ui()->DoDropDown(&MusicPlayerVisualizerModeSelect, g_Config.m_BcMusicPlayerVisualizerMode, apMusicPlayerVisualizerModes, (int)std::size(apMusicPlayerVisualizerModes), s_MusicPlayerVisualizerModeState);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcMusicPlayerTextScale, &g_Config.m_BcMusicPlayerTextScale, &Button, Localize("Text scale"), 70, 150, &CUi::ms_LinearScrollbarScale, 0u, "%");

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcMusicPlayerVisualizerSensitivity, &g_Config.m_BcMusicPlayerVisualizerSensitivity, &Button, Localize("Sensitivity"), 50, 300, &CUi::ms_LinearScrollbarScale, 0u, "%");

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcMusicPlayerVisualizerSmoothing, &g_Config.m_BcMusicPlayerVisualizerSmoothing, &Button, Localize("Smoothing"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcMusicPlayerVisualizerColumns, &g_Config.m_BcMusicPlayerVisualizerColumns, &Button, Localize("Columns"), 5, 10);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		CUIRect MusicPlayerRoundingLabel, MusicPlayerRoundingButtons;
		Button.VSplitLeft(150.0f, &MusicPlayerRoundingLabel, &MusicPlayerRoundingButtons);
		Ui()->DoLabel(&MusicPlayerRoundingLabel, Localize("Rounding"), 14.0f, TEXTALIGN_ML);

		static CButtonContainer s_MusicPlayerVisualizerRoundingCube;
		static CButtonContainer s_MusicPlayerVisualizerRoundingSoft;
		static CButtonContainer s_MusicPlayerVisualizerRoundingPill;
		const int MusicPlayerRoundingPreset = g_Config.m_BcMusicPlayerVisualizerRounding < 100 ? 0 : (g_Config.m_BcMusicPlayerVisualizerRounding < 300 ? 1 : 2);
		CUIRect MusicPlayerCubeButton, MusicPlayerSoftButton, MusicPlayerPillButton, MusicPlayerRoundingRest;
		const float MusicPlayerRoundingSpacing = 2.0f;
		const float MusicPlayerRoundingButtonWidth = (MusicPlayerRoundingButtons.w - MusicPlayerRoundingSpacing * 2.0f) / 3.0f;
		MusicPlayerRoundingButtons.VSplitLeft(MusicPlayerRoundingButtonWidth, &MusicPlayerCubeButton, &MusicPlayerRoundingRest);
		MusicPlayerRoundingRest.VSplitLeft(MusicPlayerRoundingSpacing, nullptr, &MusicPlayerRoundingRest);
		MusicPlayerRoundingRest.VSplitLeft(MusicPlayerRoundingButtonWidth, &MusicPlayerSoftButton, &MusicPlayerRoundingRest);
		MusicPlayerRoundingRest.VSplitLeft(MusicPlayerRoundingSpacing, nullptr, &MusicPlayerRoundingRest);
		MusicPlayerPillButton = MusicPlayerRoundingRest;
		if(DoButton_Menu(&s_MusicPlayerVisualizerRoundingCube, Localize("Cube"), MusicPlayerRoundingPreset == 0, &MusicPlayerCubeButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
			g_Config.m_BcMusicPlayerVisualizerRounding = 0;
		if(DoButton_Menu(&s_MusicPlayerVisualizerRoundingSoft, Localize("Soft"), MusicPlayerRoundingPreset == 1, &MusicPlayerSoftButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
			g_Config.m_BcMusicPlayerVisualizerRounding = 200;
		if(DoButton_Menu(&s_MusicPlayerVisualizerRoundingPill, Localize("Pill"), MusicPlayerRoundingPreset == 2, &MusicPlayerPillButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
			g_Config.m_BcMusicPlayerVisualizerRounding = 400;

		Ui()->ClipDisable();
	}

	// Keystrokes (right column block)
	RightColumn.HSplitTop(MarginBetweenViews, nullptr, &RightColumn);

	const bool KeystrokesIsMinecraft = g_Config.m_BcKeystrokesStyle == 1;
	const bool KeystrokesKeyboardExpanded = g_Config.m_BcKeystrokesKeyboard != 0;
	const bool KeystrokesMouseExpanded = !KeystrokesIsMinecraft && g_Config.m_BcKeystrokesMouse != 0;
	static float s_KeystrokesKeyboardRevealPhase = 0.0f;
	static float s_KeystrokesMouseRevealPhase = 0.0f;
	static float s_KeystrokesMinecraftRevealPhase = 0.0f;
	UpdateModuleRevealPhase(s_KeystrokesKeyboardRevealPhase, !KeystrokesIsMinecraft && KeystrokesKeyboardExpanded, Client()->RenderFrameTime());
	UpdateModuleRevealPhase(s_KeystrokesMouseRevealPhase, KeystrokesMouseExpanded, Client()->RenderFrameTime());
	UpdateModuleRevealPhase(s_KeystrokesMinecraftRevealPhase, KeystrokesIsMinecraft && KeystrokesKeyboardExpanded, Client()->RenderFrameTime());

	const float KeystrokesClassicPresetHeight = (MarginSmall + LineSize) * BCUiAnimations::EaseOutCubic(s_KeystrokesKeyboardRevealPhase);
	const float KeystrokesMouseExpandedHeight = (MarginSmall + LineSize) * BCUiAnimations::EaseOutCubic(s_KeystrokesMouseRevealPhase);
	const float KeystrokesMcOptionsRows = 1.0f + (g_Config.m_BcKeystrokesMcLayout == 1 ? 3.0f : 0.0f); // layout + LMB/RMB/Space for Only A/D
	const float KeystrokesMinecraftExpandedHeight = (MarginSmall + LineSize) * KeystrokesMcOptionsRows * BCUiAnimations::EaseOutCubic(s_KeystrokesMinecraftRevealPhase);
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
	KeystrokesBlockBg.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);

	MainView = KeystrokesBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	CUIRect KeystrokesTitleLabel, KeystrokesHudEditorButton, KeystrokesResetButton;
	Label.VSplitRight(LineSize + 8.0f, &Label, &KeystrokesResetButton);
	Label.VSplitRight(MarginSmall, &Label, nullptr);
	Label.VSplitRight(LineSize + 8.0f, &KeystrokesTitleLabel, &KeystrokesHudEditorButton);
	static CButtonContainer s_KeystrokesResetButton;
	const bool KeystrokesResetClicked = Ui()->DoButton_FontIcon(&s_KeystrokesResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &KeystrokesResetButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_KeystrokesResetButton, &KeystrokesResetButton, Localize("Reset to defaults"));
	if(KeystrokesResetClicked)
	{
		g_Config.m_BcKeystrokesStyle = DefaultConfig::BcKeystrokesStyle;
		g_Config.m_BcKeystrokesKeyboardPreset = DefaultConfig::BcKeystrokesKeyboardPreset;
		g_Config.m_BcKeystrokesMousePreset = DefaultConfig::BcKeystrokesMousePreset;
		g_Config.m_BcKeystrokesMcLayout = DefaultConfig::BcKeystrokesMcLayout;
		g_Config.m_BcKeystrokesMcShowWs = DefaultConfig::BcKeystrokesMcShowWs;
		g_Config.m_BcKeystrokesMcShowLmb = DefaultConfig::BcKeystrokesMcShowLmb;
		g_Config.m_BcKeystrokesMcShowRmb = DefaultConfig::BcKeystrokesMcShowRmb;
		g_Config.m_BcKeystrokesMcShowSpace = DefaultConfig::BcKeystrokesMcShowSpace;
	}
	static CButtonContainer s_KeystrokesHudEditorButton;
	const bool KeystrokesCanOpenHudEditor = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
	const bool KeystrokesHudEditorClicked = Ui()->DoButton_FontIcon(&s_KeystrokesHudEditorButton, FontIcon::UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, KeystrokesCanOpenHudEditor ? 0 : -1, &KeystrokesHudEditorButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_KeystrokesHudEditorButton, &KeystrokesHudEditorButton, KeystrokesCanOpenHudEditor ? Localize("Open in HUD editor") : Localize("Join a game first"));
	GameClient()->m_Tooltips.SetFadeTime(&s_KeystrokesHudEditorButton, 0.0f);
	if(KeystrokesHudEditorClicked && KeystrokesCanOpenHudEditor)
	{
		SetActive(false);
		GameClient()->m_HudEditor.Activate();
	}
	Ui()->DoLabel(&KeystrokesTitleLabel, Localize("Keystrokes"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Button, &MainView);
	static CButtonContainer s_KeystrokesStyleClassic;
	static CButtonContainer s_KeystrokesStyleMinecraft;
	CUIRect ClassicStyleButton, MinecraftStyleButton;
	Button.VSplitMid(&ClassicStyleButton, &MinecraftStyleButton, 2.0f);
	ClassicStyleButton.HMargin(2.0f, &ClassicStyleButton);
	MinecraftStyleButton.HMargin(2.0f, &MinecraftStyleButton);
	if(DoButton_Menu(&s_KeystrokesStyleClassic, Localize("Classic"), g_Config.m_BcKeystrokesStyle == 0, &ClassicStyleButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
		g_Config.m_BcKeystrokesStyle = 0;
	if(DoButton_Menu(&s_KeystrokesStyleMinecraft, Localize("Minecraft"), g_Config.m_BcKeystrokesStyle == 1, &MinecraftStyleButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
		g_Config.m_BcKeystrokesStyle = 1;
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcKeystrokesKeyboard, Localize("Enable keyboard"), &g_Config.m_BcKeystrokesKeyboard, &Content, LineSize);
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
		// Changing the keyboard's size mode changes its width, so re-snap the mouse module
		// back to its own default position (which chases that width, see
		// HudLayout::DynamicDefaultLayout) instead of leaving it wherever it was relative to
		// the old width.
		if(DoButton_Menu(&s_KeyboardPresetFull, Localize("Full"), g_Config.m_BcKeystrokesKeyboardPreset == 1, &FullButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
		{
			g_Config.m_BcKeystrokesKeyboardPreset = 1;
			HudLayout::ResetPosition(HudLayout::MODULE_KEYSTROKES_MOUSE);
		}
		if(DoButton_Menu(&s_KeyboardPresetMinimal, Localize("Minimal"), g_Config.m_BcKeystrokesKeyboardPreset == 0, &MinimalButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
		{
			g_Config.m_BcKeystrokesKeyboardPreset = 0;
			HudLayout::ResetPosition(HudLayout::MODULE_KEYSTROKES_MOUSE);
		}
		if(DoButton_Menu(&s_KeyboardPresetMicro, Localize("Micro"), g_Config.m_BcKeystrokesKeyboardPreset == 2, &MicroButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
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
		if(DoButton_Menu(&s_McLayoutFull, Localize("Full"), g_Config.m_BcKeystrokesMcLayout == 0, &McFullButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
			g_Config.m_BcKeystrokesMcLayout = 0;
		if(DoButton_Menu(&s_McLayoutOnlyAd, Localize("Only A/D"), g_Config.m_BcKeystrokesMcLayout == 1, &McOnlyAdButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
			g_Config.m_BcKeystrokesMcLayout = 1;

		if(g_Config.m_BcKeystrokesMcLayout == 1)
		{
			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			MainView.HSplitTop(LineSize, &Content, &MainView);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcKeystrokesMcShowLmb, Localize("Enable LMB"), &g_Config.m_BcKeystrokesMcShowLmb, &Content, LineSize);

			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			MainView.HSplitTop(LineSize, &Content, &MainView);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcKeystrokesMcShowRmb, Localize("Enable RMB"), &g_Config.m_BcKeystrokesMcShowRmb, &Content, LineSize);

			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			MainView.HSplitTop(LineSize, &Content, &MainView);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcKeystrokesMcShowSpace, Localize("Enable Space"), &g_Config.m_BcKeystrokesMcShowSpace, &Content, LineSize);
		}

		Ui()->ClipDisable();
	}

	if(!KeystrokesIsMinecraft)
	{
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcKeystrokesMouse, Localize("Enable mouse"), &g_Config.m_BcKeystrokesMouse, &Content, LineSize);
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
			if(DoButton_Menu(&s_MousePresetArrow, Localize("Arrow"), g_Config.m_BcKeystrokesMousePreset == 1, &ArrowButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
				g_Config.m_BcKeystrokesMousePreset = 1;
			if(DoButton_Menu(&s_MousePresetDotDot, Localize("Dot Dot"), g_Config.m_BcKeystrokesMousePreset == 2, &DotDotButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
				g_Config.m_BcKeystrokesMousePreset = 2;
			if(DoButton_Menu(&s_MousePresetNothing, Localize("Nothing"), g_Config.m_BcKeystrokesMousePreset == 3, &NothingButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
				g_Config.m_BcKeystrokesMousePreset = 3;

			Ui()->ClipDisable();
		}
	}

	// Aspect ratio (right column block)
	RightColumn.HSplitTop(MarginBetweenViews, nullptr, &RightColumn);

	const int AspectMode = g_Config.m_BcCustomAspectRatioMode >= 0 ? g_Config.m_BcCustomAspectRatioMode : (g_Config.m_BcCustomAspectRatio > 0 ? 1 : 0);
	const bool AspectCustomMode = AspectMode == 2;
	const float AspectHeaderHeight = LineSize + MarginSmall + LineSize + MarginSmall + LineSize;
	const float AspectExpandedHeight = AspectCustomMode ? (MarginSmall + LineSize + MarginSmall + LineSize) : 0.0f;
	const float AspectBlockHeight = AspectHeaderHeight + AspectExpandedHeight;

	CUIRect AspectBlock;
	RightColumn.HSplitTop(AspectBlockHeight, &AspectBlock, &RightColumn);

	CUIRect AspectBlockBg = AspectBlock;
	AspectBlockBg.w += BlockPadding;
	AspectBlockBg.h += BlockPadding;
	AspectBlockBg.x -= BlockPadding * 0.5f;
	AspectBlockBg.y -= BlockPadding * 0.5f;
	AspectBlockBg.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);

	MainView = AspectBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	Ui()->DoLabel(&Label, Localize("Aspect Ratio"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	const auto SplitRowLabelControl = [&](CUIRect &InRow, CUIRect &OutLabel, CUIRect &OutControl) {
		const float LabelWidth = std::clamp(InRow.w * 0.40f, 90.0f, 170.0f);
		InRow.VSplitLeft(LabelWidth, &OutLabel, &OutControl);
	};

	const char *apAspectPresetNames[5] = {
		Localize("Off (default)"),
		"5:4",
		"4:3",
		"3:2",
		Localize("Custom"),
	};
	static const std::array<int, 4> s_aAspectPresetValues = {0, 125, 133, 150};
	static CUi::SDropDownState s_AspectPresetState;
	static CScrollRegion s_AspectPresetScrollRegion;
	s_AspectPresetState.m_SelectionPopupContext.m_pScrollRegion = &s_AspectPresetScrollRegion;

	auto GetAspectPresetIndex = [&]() -> int {
		const int CustomPresetIndex = (int)std::size(apAspectPresetNames) - 1;
		if(AspectMode <= 0 || g_Config.m_BcCustomAspectRatio == 0)
			return 0;
		if(AspectMode == 2)
			return CustomPresetIndex;

		for(size_t i = 1; i < s_aAspectPresetValues.size(); ++i)
		{
			if(g_Config.m_BcCustomAspectRatio == s_aAspectPresetValues[i])
				return (int)i;
		}

		int BestIndex = 1;
		int BestDiff = absolute(g_Config.m_BcCustomAspectRatio - s_aAspectPresetValues[BestIndex]);
		for(size_t i = 2; i < s_aAspectPresetValues.size(); ++i)
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
	Ui()->DoLabel(&PresetLabel, Localize("Preset"), 14.0f, TEXTALIGN_ML);
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
	Ui()->DoLabel(&ApplyLabel, Localize("Apply"), 14.0f, TEXTALIGN_ML);
	const char *apAspectApplyNames[3] = {
		Localize("Game only"),
		Localize("Full"),
		Localize("Game no HUD"),
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
		// The exact numerator/denominator the user typed are the source of truth,
		// so the displayed value is never normalized or rounded away.
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
		Ui()->DoLabel(&RatioLabel, Localize("Custom size"), 14.0f, TEXTALIGN_ML);

		CUIRect NumeratorRect, SeparatorRect, DenominatorRect;
		const float Gap = minimum(6.0f, RatioControls.w * 0.08f);
		const float SeparatorWidth = minimum(12.0f, RatioControls.w * 0.18f);
		const float FieldWidth = maximum(1.0f, (RatioControls.w - SeparatorWidth - 2.0f * Gap) / 2.0f);
		RatioControls.VSplitLeft(FieldWidth, &NumeratorRect, &RatioControls);
		RatioControls.VSplitLeft(Gap, nullptr, &RatioControls);
		RatioControls.VSplitLeft(SeparatorWidth, &SeparatorRect, &RatioControls);
		RatioControls.VSplitLeft(Gap, nullptr, &RatioControls);
		RatioControls.VSplitLeft(FieldWidth, &DenominatorRect, nullptr);

		Ui()->DoEditBox(&s_CustomAspectNumeratorInput, &NumeratorRect, 14.0f);
		Ui()->DoLabel(&SeparatorRect, ":", 14.0f, TEXTALIGN_MC);
		Ui()->DoEditBox(&s_CustomAspectDenominatorInput, &DenominatorRect, 14.0f);

		const int InputNum = maximum(1, s_CustomAspectNumeratorInput.GetInteger());
		const int InputDen = maximum(1, s_CustomAspectDenominatorInput.GetInteger());
		const bool HasPendingCustomChange = InputNum != g_Config.m_BcCustomAspectRatioNum || InputDen != g_Config.m_BcCustomAspectRatioDen;

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		CUIRect ButtonSpace, ApplyButton;
		MainView.HSplitTop(LineSize, &Button, &MainView);
		SplitRowLabelControl(Button, ButtonSpace, ApplyButton);
		(void)ButtonSpace;
		static CButtonContainer s_AspectApplyButton;
		if(DoButton_Menu(&s_AspectApplyButton, Localize("Apply"), HasPendingCustomChange ? 0 : -1, &ApplyButton) && HasPendingCustomChange)
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

	const float RightColumnEndY = RightColumn.y;
	CUIRect VisualsScrollContentRect;
	VisualsScrollContentRect.x = MainView.x;
	VisualsScrollContentRect.y = maximum(LeftColumnEndY, RightColumnEndY) + MarginSmall * 2.0f;
	VisualsScrollContentRect.w = MainView.w;
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
	str_format(aBuf, sizeof(aBuf), "%s: %.2f %s", pLabel, Value / (float)Scale, Localize("ticks"));

	CUIRect Label, ScrollBar;
	Button.VSplitMid(&Label, &ScrollBar, minimum(10.0f, Button.w * 0.05f));
	Ui()->DoLabel(&Label, aBuf, Label.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_ML);

	const float Rel = (Value - Min) / (float)(Max - Min);
	const float NewRel = Ui()->DoScrollbarH(pValue, &ScrollBar, Rel);
	Value = (int)(Min + NewRel * (Max - Min) + 0.5f);
	*pValue = std::clamp(Value, Min, Max);
}

void CMenus::RenderSettingsBestClientGameplay(CUIRect MainView)
{
	const float LineSize = 20.0f;
	const float MarginSmall = 5.0f;
	const float HeadlineFontSize = 20.0f;
	const float FontSize = 14.0f;
	const float EditBoxFontSize = 14.0f;
	const float MarginBetweenViews = 30.0f;
	const float BlockPadding = MarginBetweenViews * 0.6666f;
	const ColorRGBA BlockColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f);

	static CScrollRegion s_GameplayScrollRegion;
	vec2 GameplayScrollOffset(0.0f, 0.0f);
	CScrollRegionParams GameplayScrollParams;
	GameplayScrollParams.m_ScrollUnit = 60.0f;
	GameplayScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	GameplayScrollParams.m_ScrollbarMargin = 5.0f;
	s_GameplayScrollRegion.Begin(&MainView, &GameplayScrollOffset, &GameplayScrollParams);
	MainView.y += GameplayScrollOffset.y;
	MainView.VSplitRight(5.0f, &MainView, nullptr);
	MainView.VSplitLeft(5.0f, nullptr, &MainView);

	CUIRect LeftView, RightView;
	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	CUIRect Column = LeftView;
	Column.HSplitTop(10.0f, nullptr, &Column);

	CUIRect Content, Label, Button;

	// Inputs (left column block)
	const int InputsMode = g_Config.m_BcInputs;
	const bool InputsEnabled = InputsMode != BC_INPUTS_OFF;
	const bool InputsBestMode = InputsMode == BC_INPUTS_BEST;
	static float s_InputsRevealPhase = 0.0f;
	UpdateModuleRevealPhase(s_InputsRevealPhase, InputsEnabled, Client()->RenderFrameTime());
	const float InputsModeFieldsHeight = (InputsBestMode ? 6.0f : 2.0f) * (MarginSmall + LineSize);
	const float InputsExpandedTargetHeight = (MarginSmall + LineSize) + InputsModeFieldsHeight;
	const float InputsExpandedHeight = InputsExpandedTargetHeight * BCUiAnimations::EaseOutCubic(s_InputsRevealPhase);
	const float InputsHeaderHeight = LineSize + MarginSmall + LineSize;
	const float InputsBlockHeight = InputsHeaderHeight + InputsExpandedHeight + 2.0f * (MarginSmall + LineSize);

	CUIRect InputsBlock;
	Column.HSplitTop(InputsBlockHeight, &InputsBlock, &Column);

	CUIRect InputsBlockBg = InputsBlock;
	InputsBlockBg.w += BlockPadding;
	InputsBlockBg.h += BlockPadding;
	InputsBlockBg.x -= BlockPadding * 0.5f;
	InputsBlockBg.y -= BlockPadding * 0.5f;
	InputsBlockBg.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);

	InputsBlock.HSplitTop(LineSize, &Label, &InputsBlock);
	Ui()->DoLabel(&Label, Localize("Inputs"), HeadlineFontSize, TEXTALIGN_ML);
	InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);

	InputsBlock.HSplitTop(LineSize, &Content, &InputsBlock);
	{
		static CButtonContainer s_InputsEnabledButton;
		static int s_LastNonOffMode = BC_INPUTS_FAST;
		if(InputsEnabled)
			s_LastNonOffMode = InputsMode;
		if(DoButton_CheckBox(&s_InputsEnabledButton, Localize("Enable Inputs"), InputsEnabled, &Content))
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
			static CButtonContainer s_InputsFast, s_InputsBest, s_InputsSaiko, s_InputsDelta, s_InputsF;
			CUIRect ButtonsRect = Button;
			const float Spacing = 2.0f;
			const float InputButtonWidth = (ButtonsRect.w - Spacing * 4.0f) / 5.0f;

			CUIRect FastButton, BestButton, SaikoButton, DeltaButton, FButton;
			ButtonsRect.VSplitLeft(InputButtonWidth, &FastButton, &ButtonsRect);
			ButtonsRect.VSplitLeft(Spacing, nullptr, &ButtonsRect);
			ButtonsRect.VSplitLeft(InputButtonWidth, &BestButton, &ButtonsRect);
			ButtonsRect.VSplitLeft(Spacing, nullptr, &ButtonsRect);
			ButtonsRect.VSplitLeft(InputButtonWidth, &SaikoButton, &ButtonsRect);
			ButtonsRect.VSplitLeft(Spacing, nullptr, &ButtonsRect);
			ButtonsRect.VSplitLeft(InputButtonWidth, &DeltaButton, &ButtonsRect);
			ButtonsRect.VSplitLeft(Spacing, nullptr, &ButtonsRect);
			FButton = ButtonsRect;

			FastButton.HMargin(2.0f, &FastButton);
			BestButton.HMargin(2.0f, &BestButton);
			SaikoButton.HMargin(2.0f, &SaikoButton);
			DeltaButton.HMargin(2.0f, &DeltaButton);
			FButton.HMargin(2.0f, &FButton);

			if(DoButton_Menu(&s_InputsFast, "Fast", InputsMode == BC_INPUTS_FAST, &FastButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
				g_Config.m_BcInputs = BC_INPUTS_FAST;
			if(DoButton_Menu(&s_InputsBest, "Best", InputsMode == BC_INPUTS_BEST, &BestButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
				g_Config.m_BcInputs = BC_INPUTS_BEST;
			if(DoButton_Menu(&s_InputsSaiko, "Saiko", InputsMode == BC_INPUTS_SAIKO, &SaikoButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
				g_Config.m_BcInputs = BC_INPUTS_SAIKO;
			if(DoButton_Menu(&s_InputsDelta, "Delta", InputsMode == BC_INPUTS_DELTA, &DeltaButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
				g_Config.m_BcInputs = BC_INPUTS_DELTA;
			if(DoButton_Menu(&s_InputsF, "F", InputsMode == BC_INPUTS_F, &FButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
				g_Config.m_BcInputs = BC_INPUTS_F;
		}

		if(InputsMode == BC_INPUTS_FAST)
		{
			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Button, &InputsBlock);
			DoSliderWithScaledValue(&g_Config.m_TcFastInputAmount, &g_Config.m_TcFastInputAmount, &Button, Localize("Prediction offset"), 1, 40, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");

			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Content, &InputsBlock);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFastInputOthers, Localize("Fast Input others"), &g_Config.m_TcFastInputOthers, &Content, LineSize);
		}
		else if(InputsMode == BC_INPUTS_BEST)
		{
			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Button, &InputsBlock);
			DoTickAmountSlider(&g_Config.m_BcBestInputAmount, &Button, Localize("Prediction offset"), 0, 1000);

			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Button, &InputsBlock);
			Ui()->DoScrollbarOption(&g_Config.m_BcBestInputSmoothing, &g_Config.m_BcBestInputSmoothing, &Button, Localize("Smoothing"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");

			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Button, &InputsBlock);
			Ui()->DoScrollbarOption(&g_Config.m_BcBestInputLatencyComp, &g_Config.m_BcBestInputLatencyComp, &Button, Localize("Latency compensation"), 0, 50, &CUi::ms_LinearScrollbarScale, 0, "%");

			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Label, &InputsBlock);
			Ui()->DoLabel(&Label, Localize("Interpolation"), Label.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_ML);

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

					if(DoButton_Menu(&s_aInterpolationButtons[i], Localize(s_apInterpolationNames[i]), g_Config.m_BcBestInputInterpolation == s_aInterpolationValues[i], &InterpolationButton, BUTTONFLAG_LEFT, nullptr, Corners))
						g_Config.m_BcBestInputInterpolation = s_aInterpolationValues[i];
				}
			}

			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Content, &InputsBlock);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcBestInputOthers, Localize("Best input others"), &g_Config.m_BcBestInputOthers, &Content, LineSize);
		}
		else if(InputsMode == BC_INPUTS_SAIKO)
		{
			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Button, &InputsBlock);
			DoTickAmountSlider(&g_Config.m_BcSaikoInputAmount, &Button, Localize("Prediction offset"), 0, 500);

			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Content, &InputsBlock);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcSaikoInputOthers, Localize("Saiko input others"), &g_Config.m_BcSaikoInputOthers, &Content, LineSize);
		}
		else if(InputsMode == BC_INPUTS_DELTA)
		{
			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Button, &InputsBlock);
			DoTickAmountSlider(&g_Config.m_BcDeltaInputAmount, &Button, Localize("Prediction offset"), 0, 500);

			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Content, &InputsBlock);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcDeltaInputOthers, Localize("Delta input others"), &g_Config.m_BcDeltaInputOthers, &Content, LineSize);
		}
		else if(InputsMode == BC_INPUTS_F)
		{
			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Button, &InputsBlock);
			DoTickAmountSlider(&g_Config.m_BcFInputAmount, &Button, Localize("Prediction offset"), 0, 5000, 1000);

			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Content, &InputsBlock);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcFInputOthers, Localize("F input others"), &g_Config.m_BcFInputOthers, &Content, LineSize);
		}

		Ui()->ClipDisable();
	}

	InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
	InputsBlock.HSplitTop(LineSize, &Content, &InputsBlock);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClSubTickAiming, Localize("Sub-Tick aiming"), &g_Config.m_ClSubTickAiming, &Content, LineSize);

	InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
	InputsBlock.HSplitTop(LineSize, &Content, &InputsBlock);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcAutoMargin, Localize("Auto margin"), &g_Config.m_BcAutoMargin, &Content, LineSize);

	// Snap Tap (left column block)
	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	const bool SnapTapBlocked = GameClient()->IsSnapTapBlockedByCommunity();
	const bool SnapTapExpanded = g_Config.m_BcSnapTap != 0;
	static float s_SnapTapRevealPhase = 0.0f;
	UpdateModuleRevealPhase(s_SnapTapRevealPhase, SnapTapExpanded, Client()->RenderFrameTime());
	const float SnapTapHeaderHeight = LineSize + MarginSmall + LineSize;
	const float SnapTapExpandedHeight = (MarginSmall + LineSize) * BCUiAnimations::EaseOutCubic(s_SnapTapRevealPhase);
	const float SnapTapBlockedHintHeight = SnapTapBlocked ? (MarginSmall + LineSize) : 0.0f;
	const float SnapTapBlockHeight = SnapTapHeaderHeight + SnapTapExpandedHeight + SnapTapBlockedHintHeight;

	CUIRect SnapTapBlock;
	Column.HSplitTop(SnapTapBlockHeight, &SnapTapBlock, &Column);

	CUIRect SnapTapBlockBg = SnapTapBlock;
	SnapTapBlockBg.w += BlockPadding;
	SnapTapBlockBg.h += BlockPadding;
	SnapTapBlockBg.x -= BlockPadding * 0.5f;
	SnapTapBlockBg.y -= BlockPadding * 0.5f;
	SnapTapBlockBg.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);

	SnapTapBlock.HSplitTop(LineSize, &Label, &SnapTapBlock);
	Ui()->DoLabel(&Label, Localize("Snap Tap"), HeadlineFontSize, TEXTALIGN_ML);
	SnapTapBlock.HSplitTop(MarginSmall, nullptr, &SnapTapBlock);

	SnapTapBlock.HSplitTop(LineSize, &Content, &SnapTapBlock);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcSnapTap, Localize("Enable"), &g_Config.m_BcSnapTap, &Content, LineSize);

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
		if(Value == 0)
			str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Delay"), Localize("Off"));
		else
			str_format(aBuf, sizeof(aBuf), "%s: %dms", Localize("Delay"), Value);

		CUIRect DelayLabel, ScrollBar;
		Button.VSplitMid(&DelayLabel, &ScrollBar, minimum(10.0f, Button.w * 0.05f));
		Ui()->DoLabel(&DelayLabel, aBuf, DelayLabel.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_ML);

		const float Rel = (Value - Min) / (float)(Max - Min);
		const float NewRel = Ui()->DoScrollbarH(&g_Config.m_BcSnapTapDelay, &ScrollBar, Rel);
		Value = (int)(Min + NewRel * (Max - Min) + 0.5f);
		g_Config.m_BcSnapTapDelay = std::clamp(Value, Min, Max);

		Ui()->ClipDisable();
	}

	if(SnapTapBlocked)
	{
		SnapTapBlock.HSplitTop(MarginSmall, nullptr, &SnapTapBlock);
		SnapTapBlock.HSplitTop(LineSize, &Label, &SnapTapBlock);
		TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
		Ui()->DoLabel(&Label, Localize("Looks like you're on a server where this feature is forbidden"), FontSize, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}

	// Optimizer (left column block)
	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	const bool OptimizerExpanded = g_Config.m_BcOptimizer != 0;
	const bool OptimizerFpsFogExpanded = OptimizerExpanded && g_Config.m_BcOptimizerFpsFog != 0;
	static float s_OptimizerRevealPhase = 0.0f;
	static float s_OptimizerFpsFogRevealPhase = 0.0f;
	UpdateModuleRevealPhase(s_OptimizerRevealPhase, OptimizerExpanded, Client()->RenderFrameTime());
	if(BCUiAnimations::Enabled() && g_Config.m_BcModuleUiRevealAnimation != 0)
		BCUiAnimations::UpdatePhase(s_OptimizerFpsFogRevealPhase, OptimizerFpsFogExpanded ? 1.0f : 0.0f, Client()->RenderFrameTime(), 0.16f);
	else
		s_OptimizerFpsFogRevealPhase = OptimizerFpsFogExpanded ? 1.0f : 0.0f;
	const float OptimizerFpsFogExpandedHeight = (4.0f * (MarginSmall + LineSize)) * BCUiAnimations::EaseOutCubic(s_OptimizerFpsFogRevealPhase);
	const float OptimizerExpandedHeight = (4.0f * (MarginSmall + LineSize)) * BCUiAnimations::EaseOutCubic(s_OptimizerRevealPhase) + OptimizerFpsFogExpandedHeight;
	const float OptimizerHeaderHeight = LineSize + MarginSmall + LineSize;
	const float OptimizerBlockHeight = OptimizerHeaderHeight + OptimizerExpandedHeight;

	CUIRect OptimizerBlock;
	Column.HSplitTop(OptimizerBlockHeight, &OptimizerBlock, &Column);

	CUIRect OptimizerBlockBg = OptimizerBlock;
	OptimizerBlockBg.w += BlockPadding;
	OptimizerBlockBg.h += BlockPadding;
	OptimizerBlockBg.x -= BlockPadding * 0.5f;
	OptimizerBlockBg.y -= BlockPadding * 0.5f;
	OptimizerBlockBg.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);

	MainView = OptimizerBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	Ui()->DoLabel(&Label, Localize("Optimizer"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizer, Localize("Enable optimizer"), &g_Config.m_BcOptimizer, &Content, LineSize);

	if(OptimizerExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = OptimizerExpandedHeight;
		Ui()->ClipEnable(&Visible);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerDisableParticles, Localize("Disable all particles render"), &g_Config.m_BcOptimizerDisableParticles, &Content, LineSize);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerFpsFog, Localize("FPS fog (cull outside limit)"), &g_Config.m_BcOptimizerFpsFog, &Content, LineSize);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerDdnetPriorityHigh, Localize("DDNet priority: High"), &g_Config.m_BcOptimizerDdnetPriorityHigh, &Content, LineSize);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerDiscordPriorityBelowNormal, Localize("Discord priority: Below Normal"), &g_Config.m_BcOptimizerDiscordPriorityBelowNormal, &Content, LineSize);

		if(OptimizerFpsFogExpandedHeight > 0.5f)
		{
			CUIRect FogVisible = MainView;
			FogVisible.h = OptimizerFpsFogExpandedHeight;
			Ui()->ClipEnable(&FogVisible);

			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			MainView.HSplitTop(LineSize, &Content, &MainView);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerFpsFogRenderRect, Localize("Render FPS fog rectangle"), &g_Config.m_BcOptimizerFpsFogRenderRect, &Content, LineSize);

			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			MainView.HSplitTop(LineSize, &Content, &MainView);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerFpsFogCullMapTiles, Localize("Cull map tiles outside FPS fog"), &g_Config.m_BcOptimizerFpsFogCullMapTiles, &Content, LineSize);

			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			MainView.HSplitTop(LineSize, &Button, &MainView);
			static CButtonContainer s_OptimizerFogModeRadius;
			static CButtonContainer s_OptimizerFogModeZoom;
			CUIRect RadiusButton, ZoomButton;
			Button.VSplitMid(&RadiusButton, &ZoomButton, 2.0f);
			RadiusButton.HMargin(2.0f, &RadiusButton);
			ZoomButton.HMargin(2.0f, &ZoomButton);
			if(DoButton_Menu(&s_OptimizerFogModeRadius, Localize("Radius"), g_Config.m_BcOptimizerFpsFogMode == 0, &RadiusButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
				g_Config.m_BcOptimizerFpsFogMode = 0;
			if(DoButton_Menu(&s_OptimizerFogModeZoom, Localize("Zoom %"), g_Config.m_BcOptimizerFpsFogMode == 1, &ZoomButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
				g_Config.m_BcOptimizerFpsFogMode = 1;

			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			MainView.HSplitTop(LineSize, &Button, &MainView);
			if(g_Config.m_BcOptimizerFpsFogMode == 0)
				Ui()->DoScrollbarOption(&g_Config.m_BcOptimizerFpsFogRadiusTiles, &g_Config.m_BcOptimizerFpsFogRadiusTiles, &Button, Localize("Radius (tiles)"), 5, 300);
			else
				Ui()->DoScrollbarOption(&g_Config.m_BcOptimizerFpsFogZoomPercent, &g_Config.m_BcOptimizerFpsFogZoomPercent, &Button, Localize("Visible area (%)"), 10, 120, &CUi::ms_LinearScrollbarScale, 0u, "%");

			Ui()->ClipDisable();
		}

		Ui()->ClipDisable();
	}

	// Gores mode (left column block)
	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	const bool Expanded = g_Config.m_BcGoresMode != 0;
	static float s_GoresModeRevealPhase = 0.0f;
	UpdateModuleRevealPhase(s_GoresModeRevealPhase, Expanded, Client()->RenderFrameTime());
	const float HeaderHeight = LineSize + MarginSmall + LineSize;
	const float ExpandedHeight = (MarginSmall + LineSize) * BCUiAnimations::EaseOutCubic(s_GoresModeRevealPhase);
	const float BlockHeight = HeaderHeight + ExpandedHeight;

	CUIRect Block;
	Column.HSplitTop(BlockHeight, &Block, &Column);

	CUIRect BlockBg = Block;
	BlockBg.w += BlockPadding;
	BlockBg.h += BlockPadding;
	BlockBg.x -= BlockPadding * 0.5f;
	BlockBg.y -= BlockPadding * 0.5f;
	BlockBg.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);

	Block.HSplitTop(LineSize, &Label, &Block);
	Ui()->DoLabel(&Label, Localize("Gores mode"), HeadlineFontSize, TEXTALIGN_ML);
	Block.HSplitTop(MarginSmall, nullptr, &Block);

	Block.HSplitTop(LineSize, &Content, &Block);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcGoresMode, Localize("Enable gores mode"), &g_Config.m_BcGoresMode, &Content, LineSize);

	if(ExpandedHeight > 0.5f)
	{
		CUIRect Visible = Block;
		Visible.h = ExpandedHeight;
		Ui()->ClipEnable(&Visible);

		Block.HSplitTop(MarginSmall, nullptr, &Block);
		Block.HSplitTop(LineSize, &Content, &Block);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcGoresModeDisableIfWeapons, Localize("Disable if you have shotgun, grenade or laser"), &g_Config.m_BcGoresModeDisableIfWeapons, &Content, LineSize);

		Ui()->ClipDisable();
	}

	// Self timeCP (left column block)
	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	const bool SelfTimeCpEnabled = g_Config.m_BcSelfTimeCp != 0;
	static float s_SelfTimeCpRevealPhase = 0.0f;
	UpdateModuleRevealPhase(s_SelfTimeCpRevealPhase, SelfTimeCpEnabled, Client()->RenderFrameTime());
	const float SelfTimeCpColorLineSize = 25.0f;
	const float SelfTimeCpColorLineSpacing = 5.0f;
	const float SelfTimeCpKeyReaderExtra = 2.5f;
	const float SelfTimeCpExpandedTargetHeight = MarginSmall + LineSize + MarginSmall + SelfTimeCpColorLineSize + SelfTimeCpColorLineSpacing + 2.0f * (LineSize + SelfTimeCpKeyReaderExtra) + LineSize;
	const float SelfTimeCpExpandedHeight = SelfTimeCpExpandedTargetHeight * BCUiAnimations::EaseOutCubic(s_SelfTimeCpRevealPhase);
	const float SelfTimeCpHeaderHeight = LineSize + MarginSmall + LineSize;
	const float SelfTimeCpBlockHeight = SelfTimeCpHeaderHeight + SelfTimeCpExpandedHeight;

	CUIRect SelfTimeCpBlock;
	Column.HSplitTop(SelfTimeCpBlockHeight, &SelfTimeCpBlock, &Column);

	CUIRect SelfTimeCpBlockBg = SelfTimeCpBlock;
	SelfTimeCpBlockBg.w += BlockPadding;
	SelfTimeCpBlockBg.h += BlockPadding;
	SelfTimeCpBlockBg.x -= BlockPadding * 0.5f;
	SelfTimeCpBlockBg.y -= BlockPadding * 0.5f;
	SelfTimeCpBlockBg.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);

	SelfTimeCpBlock.HSplitTop(LineSize, &Label, &SelfTimeCpBlock);
	DrawBcMenuBadge(Graphics(), Ui(), TextRender(), &Label, Localize("NEW"), 12.0f,
		ColorRGBA(0.25f, 0.85f, 0.40f, 1.0f), ColorRGBA(0.10f, 0.60f, 0.25f, 1.0f), MarginSmall);
	Ui()->DoLabel(&Label, Localize("Self timeCP"), HeadlineFontSize, TEXTALIGN_ML);
	SelfTimeCpBlock.HSplitTop(MarginSmall, nullptr, &SelfTimeCpBlock);

	SelfTimeCpBlock.HSplitTop(LineSize, &Content, &SelfTimeCpBlock);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcSelfTimeCp, Localize("Enable self timeCP"), &g_Config.m_BcSelfTimeCp, &Content, LineSize);

	if(SelfTimeCpExpandedHeight > 0.5f)
	{
		CUIRect Visible = SelfTimeCpBlock;
		Visible.h = SelfTimeCpExpandedHeight;
		Ui()->ClipEnable(&Visible);

		SelfTimeCpBlock.HSplitTop(MarginSmall, nullptr, &SelfTimeCpBlock);
		SelfTimeCpBlock.HSplitTop(LineSize, &Button, &SelfTimeCpBlock);
		{
			CUIRect TeeButton, CursorButton;
			Button.HMargin(2.0f, &Button);
			Button.VSplitMid(&TeeButton, &CursorButton, 2.0f);
			static CButtonContainer s_SelfTimeCpModeTee;
			static CButtonContainer s_SelfTimeCpModeCursor;
			if(DoButton_Menu(&s_SelfTimeCpModeTee, Localize("Tee"), g_Config.m_BcSelfTimeCpPlaceMode == 0, &TeeButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
				g_Config.m_BcSelfTimeCpPlaceMode = 0;
			if(DoButton_Menu(&s_SelfTimeCpModeCursor, Localize("Cursor"), g_Config.m_BcSelfTimeCpPlaceMode == 1, &CursorButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
				g_Config.m_BcSelfTimeCpPlaceMode = 1;
		}

		SelfTimeCpBlock.HSplitTop(MarginSmall, nullptr, &SelfTimeCpBlock);
		static CButtonContainer s_SelfTimeCpColorButton;
		DoLine_ColorPicker(&s_SelfTimeCpColorButton, SelfTimeCpColorLineSize, 13.0f, SelfTimeCpColorLineSpacing, &SelfTimeCpBlock, Localize("Checkpoint color"), &g_Config.m_BcSelfTimeCpColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::BcSelfTimeCpColor, true)), false, nullptr, true);

		static CButtonContainer s_SelfTimeCpPlaceBindReader;
		static CButtonContainer s_SelfTimeCpPlaceBindClear;
		DoLine_KeyReader(SelfTimeCpBlock, s_SelfTimeCpPlaceBindReader, s_SelfTimeCpPlaceBindClear, Localize("Place bind"), "BC_place_time_cp");

		static CButtonContainer s_SelfTimeCpUndoBindReader;
		static CButtonContainer s_SelfTimeCpUndoBindClear;
		DoLine_KeyReader(SelfTimeCpBlock, s_SelfTimeCpUndoBindReader, s_SelfTimeCpUndoBindClear, Localize("Undo bind"), "BC_undo_time_cp");

		SelfTimeCpBlock.HSplitTop(LineSize, &Button, &SelfTimeCpBlock);
		static CButtonContainer s_SelfTimeCpClearButton;
		if(DoButton_Menu(&s_SelfTimeCpClearButton, Localize("Clear timeCP"), 0, &Button, BUTTONFLAG_LEFT))
			Console()->ExecuteLine("BC_clear_time_cp", IConsole::CLIENT_ID_UNSPECIFIED);

		Ui()->ClipDisable();
	}

	const float LeftColumnEndY = Column.y;

	// Fast Actions (right column block)
	const float WheelPreviewHeight = 96.0f;

	CUIRect RightColumn = RightView;
	RightColumn.HSplitTop(10.0f, nullptr, &RightColumn);

	const bool FastActionsExpanded = g_Config.m_BcFastActions != 0;
	static float s_FastActionsRevealPhase = 0.0f;
	UpdateModuleRevealPhase(s_FastActionsRevealPhase, FastActionsExpanded, Client()->RenderFrameTime());
	const float FastActionsHeaderHeight = LineSize + MarginSmall + LineSize;
	const float FastActionsExpandedTargetHeight = MarginSmall + WheelPreviewHeight + MarginSmall + LineSize + MarginSmall + LineSize + MarginSmall + LineSize + MarginSmall + LineSize + MarginSmall + LineSize * 0.8f + MarginSmall + LineSize;
	const float FastActionsExpandedHeight = FastActionsExpandedTargetHeight * BCUiAnimations::EaseOutCubic(s_FastActionsRevealPhase);
	const float FastActionsBlockHeight = FastActionsHeaderHeight + FastActionsExpandedHeight;

	CUIRect FastActionsBlock;
	RightColumn.HSplitTop(FastActionsBlockHeight, &FastActionsBlock, &RightColumn);

	CUIRect FastActionsBlockBg = FastActionsBlock;
	FastActionsBlockBg.w += BlockPadding;
	FastActionsBlockBg.h += BlockPadding;
	FastActionsBlockBg.x -= BlockPadding * 0.5f;
	FastActionsBlockBg.y -= BlockPadding * 0.5f;
	FastActionsBlockBg.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);

	MainView = FastActionsBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	Ui()->DoLabel(&Label, Localize("Fast Actions"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcFastActions, Localize("Enable Fast Actions"), &g_Config.m_BcFastActions, &Content, LineSize);

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
		const float LineHalfWidth = maximum(40.0f, WheelPreview.w / 2.0f - LineInset);
		const float LineHeight = minimum(WheelPreview.h * 0.78f, 44.0f);
		const float SelectBandHalfHeight = LineHeight * 1.2f;
		const float LabelW = 52.0f;
		const float LabelH = 52.0f;
		const float TextHalfRange = maximum(0.0f, LineHalfWidth - LabelW / 2.0f - 2.0f);

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

		s_SelectedBindIndex = std::clamp(s_SelectedBindIndex, 0, maximum(0, SegmentCount - 1));
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
		str_format(aSlotLabel, sizeof(aSlotLabel), "%s %d", Localize("Selected slot"), s_SelectedBindIndex + 1);
		Ui()->DoLabel(&Button, aSlotLabel, FontSize, TEXTALIGN_ML);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Button.VSplitLeft(150.0f, &Label, &Button);
		Ui()->DoLabel(&Label, Localize("Name:"), FontSize, TEXTALIGN_ML);
		static CLineInput s_BindNameInput;
		s_BindNameInput.SetBuffer(s_aBindName, sizeof(s_aBindName));
		s_BindNameInput.SetEmptyText(Localize("Name (optional)"));
		Ui()->DoEditBox(&s_BindNameInput, &Button, EditBoxFontSize);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Button.VSplitLeft(150.0f, &Label, &Button);
		Ui()->DoLabel(&Label, Localize("Command:"), FontSize, TEXTALIGN_ML);
		static CLineInput s_BindCommandInput;
		s_BindCommandInput.SetBuffer(s_aBindCommand, sizeof(s_aBindCommand));
		s_BindCommandInput.SetEmptyText(Localize("Command"));
		Ui()->DoEditBox(&s_BindCommandInput, &Button, EditBoxFontSize);

		if(s_SelectedBindIndex < static_cast<int>(GameClient()->m_FastActions.m_vBinds.size()))
		{
			str_copy(GameClient()->m_FastActions.m_vBinds[s_SelectedBindIndex].m_aName, s_aBindName);
			str_copy(GameClient()->m_FastActions.m_vBinds[s_SelectedBindIndex].m_aCommand, s_aBindCommand);
		}

		static CButtonContainer s_FastActionsClearButton;
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		if(DoButton_Menu(&s_FastActionsClearButton, Localize("Clear command"), 0, &Button) &&
			s_SelectedBindIndex < static_cast<int>(GameClient()->m_FastActions.m_vBinds.size()))
		{
			GameClient()->m_FastActions.m_vBinds[s_SelectedBindIndex].m_aCommand[0] = '\0';
			s_aBindCommand[0] = '\0';
		}

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize * 0.8f, &Label, &MainView);
		Ui()->DoLabel(&Label, Localize("In game: hold bind key, press 1..6, release key to execute"), FontSize * 0.8f, TEXTALIGN_ML);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Label, &MainView);
		static CButtonContainer s_FastActionsReaderButton;
		static CButtonContainer s_FastActionsClearKeyButton;
		DoLine_KeyReader(Label, s_FastActionsReaderButton, s_FastActionsClearKeyButton, Localize("Fast Actions key"), "+fa");

		Ui()->ClipDisable();
	}

	// Speedrun timer (right column block)
	RightColumn.HSplitTop(MarginBetweenViews, nullptr, &RightColumn);

	const bool SpeedrunExpanded = g_Config.m_BcSpeedrunTimer != 0;
	static float s_SpeedrunRevealPhase = 0.0f;
	UpdateModuleRevealPhase(s_SpeedrunRevealPhase, SpeedrunExpanded, Client()->RenderFrameTime());
	const float SpeedrunHeaderHeight = LineSize + MarginSmall + LineSize;
	const float SpeedrunExpandedHeight = 5.0f * (MarginSmall + LineSize) * BCUiAnimations::EaseOutCubic(s_SpeedrunRevealPhase);
	const float SpeedrunBlockHeight = SpeedrunHeaderHeight + SpeedrunExpandedHeight;

	CUIRect SpeedrunBlock;
	RightColumn.HSplitTop(SpeedrunBlockHeight, &SpeedrunBlock, &RightColumn);

	CUIRect SpeedrunBlockBg = SpeedrunBlock;
	SpeedrunBlockBg.w += BlockPadding;
	SpeedrunBlockBg.h += BlockPadding;
	SpeedrunBlockBg.x -= BlockPadding * 0.5f;
	SpeedrunBlockBg.y -= BlockPadding * 0.5f;
	SpeedrunBlockBg.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);

	SpeedrunBlock.HSplitTop(LineSize, &Label, &SpeedrunBlock);
	Ui()->DoLabel(&Label, Localize("Speedrun timer"), HeadlineFontSize, TEXTALIGN_ML);
	SpeedrunBlock.HSplitTop(MarginSmall, nullptr, &SpeedrunBlock);

	SpeedrunBlock.HSplitTop(LineSize, &Content, &SpeedrunBlock);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcSpeedrunTimer, Localize("Enable speedrun timer"), &g_Config.m_BcSpeedrunTimer, &Content, LineSize);

	if(SpeedrunExpandedHeight > 0.5f)
	{
		CUIRect Visible = SpeedrunBlock;
		Visible.h = SpeedrunExpandedHeight;
		Ui()->ClipEnable(&Visible);

		SpeedrunBlock.HSplitTop(MarginSmall, nullptr, &SpeedrunBlock);
		SpeedrunBlock.HSplitTop(LineSize, &Button, &SpeedrunBlock);
		Ui()->DoScrollbarOption(&g_Config.m_BcSpeedrunTimerHours, &g_Config.m_BcSpeedrunTimerHours, &Button, Localize("Hours"), 0, 99);

		SpeedrunBlock.HSplitTop(MarginSmall, nullptr, &SpeedrunBlock);
		SpeedrunBlock.HSplitTop(LineSize, &Button, &SpeedrunBlock);
		Ui()->DoScrollbarOption(&g_Config.m_BcSpeedrunTimerMinutes, &g_Config.m_BcSpeedrunTimerMinutes, &Button, Localize("Minutes"), 0, 59);

		SpeedrunBlock.HSplitTop(MarginSmall, nullptr, &SpeedrunBlock);
		SpeedrunBlock.HSplitTop(LineSize, &Button, &SpeedrunBlock);
		Ui()->DoScrollbarOption(&g_Config.m_BcSpeedrunTimerSeconds, &g_Config.m_BcSpeedrunTimerSeconds, &Button, Localize("Seconds"), 0, 59);

		SpeedrunBlock.HSplitTop(MarginSmall, nullptr, &SpeedrunBlock);
		SpeedrunBlock.HSplitTop(LineSize, &Button, &SpeedrunBlock);
		Ui()->DoScrollbarOption(&g_Config.m_BcSpeedrunTimerMilliseconds, &g_Config.m_BcSpeedrunTimerMilliseconds, &Button, Localize("Milliseconds"), 0, 999, &CUi::ms_LinearScrollbarScale, 0, "ms");

		SpeedrunBlock.HSplitTop(MarginSmall, nullptr, &SpeedrunBlock);
		SpeedrunBlock.HSplitTop(LineSize, &Content, &SpeedrunBlock);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcSpeedrunTimerAutoDisable, Localize("Auto disable after time end"), &g_Config.m_BcSpeedrunTimerAutoDisable, &Content, LineSize);

		Ui()->ClipDisable();
	}

	// Finish prediction (right column block)
	RightColumn.HSplitTop(MarginBetweenViews, nullptr, &RightColumn);

	const bool FinishPredictionEnabled = g_Config.m_BcFinishPrediction != 0;
	const bool FinishPredictionShowTimeOptions = FinishPredictionEnabled && g_Config.m_BcFinishPredictionShowTime != 0;
	static float s_FinishPredictionRevealPhase = 0.0f;
	static float s_FinishPredictionTimeRevealPhase = 0.0f;
	UpdateModuleRevealPhase(s_FinishPredictionRevealPhase, FinishPredictionEnabled, Client()->RenderFrameTime());
	UpdateModuleRevealPhase(s_FinishPredictionTimeRevealPhase, FinishPredictionShowTimeOptions, Client()->RenderFrameTime());
	const float FinishPredictionTimeExpandedHeight = (MarginSmall + LineSize) * 2.0f * BCUiAnimations::EaseOutCubic(s_FinishPredictionTimeRevealPhase); // Time left/Finish time + Show milliseconds
	const float FinishPredictionExpandedTargetHeight = (MarginSmall + LineSize) // Show time checkbox
							    + FinishPredictionTimeExpandedHeight
							    + MarginSmall + LineSize // Show percentage checkbox
							    + MarginSmall + LineSize; // Show always checkbox
	const float FinishPredictionExpandedHeight = FinishPredictionExpandedTargetHeight * BCUiAnimations::EaseOutCubic(s_FinishPredictionRevealPhase);
	const float FinishPredictionBlockHeight = LineSize + MarginSmall + LineSize + FinishPredictionExpandedHeight;

	CUIRect FinishPredictionBlock;
	RightColumn.HSplitTop(FinishPredictionBlockHeight, &FinishPredictionBlock, &RightColumn);

	CUIRect FinishPredictionBlockBg = FinishPredictionBlock;
	FinishPredictionBlockBg.w += BlockPadding;
	FinishPredictionBlockBg.h += BlockPadding;
	FinishPredictionBlockBg.x -= BlockPadding * 0.5f;
	FinishPredictionBlockBg.y -= BlockPadding * 0.5f;
	FinishPredictionBlockBg.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);

	CUIRect FinishPredictionView = FinishPredictionBlock;

	FinishPredictionView.HSplitTop(LineSize, &Label, &FinishPredictionView);
	CUIRect FinishPredictionTitleLabel, FinishPredictionHudEditorButton;
	Label.VSplitRight(LineSize + 8.0f, &FinishPredictionTitleLabel, &FinishPredictionHudEditorButton);
	static CButtonContainer s_FinishPredictionHudEditorButton;
	const bool FinishPredictionCanOpenHudEditor = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
	const bool FinishPredictionHudEditorClicked = Ui()->DoButton_FontIcon(&s_FinishPredictionHudEditorButton, FontIcon::UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, FinishPredictionCanOpenHudEditor ? 0 : -1, &FinishPredictionHudEditorButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_FinishPredictionHudEditorButton, &FinishPredictionHudEditorButton, FinishPredictionCanOpenHudEditor ? Localize("Open in HUD editor") : Localize("Join a game first"));
	GameClient()->m_Tooltips.SetFadeTime(&s_FinishPredictionHudEditorButton, 0.0f);
	if(FinishPredictionHudEditorClicked && FinishPredictionCanOpenHudEditor)
	{
		SetActive(false);
		GameClient()->m_HudEditor.Activate();
	}
	Ui()->DoLabel(&FinishPredictionTitleLabel, Localize("Finish Prediction"), HeadlineFontSize, TEXTALIGN_ML);
	FinishPredictionView.HSplitTop(MarginSmall, nullptr, &FinishPredictionView);

	FinishPredictionView.HSplitTop(LineSize, &Content, &FinishPredictionView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcFinishPrediction, Localize("Enable finish prediction"), &g_Config.m_BcFinishPrediction, &Content, LineSize);
	if(g_Config.m_BcFinishPrediction && !HudLayout::IsEnabled(HudLayout::MODULE_FINISH_PREDICTION))
		HudLayout::SetEnabled(HudLayout::MODULE_FINISH_PREDICTION, true);

	if(FinishPredictionExpandedHeight > 0.5f)
	{
		CUIRect Visible = FinishPredictionView;
		Visible.h = FinishPredictionExpandedHeight;
		Ui()->ClipEnable(&Visible);

		FinishPredictionView.HSplitTop(MarginSmall, nullptr, &FinishPredictionView);
		FinishPredictionView.HSplitTop(LineSize, &Content, &FinishPredictionView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcFinishPredictionShowTime, Localize("Show time"), &g_Config.m_BcFinishPredictionShowTime, &Content, LineSize);

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
			Button.VSplitMid(&Left, &Right, 2.0f);
			Left.HMargin(2.0f, &Left);
			Right.HMargin(2.0f, &Right);
			if(DoButton_Menu(&s_FinishPredictionRemainingButton, Localize("Time left"), g_Config.m_BcFinishPredictionTimeMode == 0, &Left, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
				g_Config.m_BcFinishPredictionTimeMode = 0;
			if(DoButton_Menu(&s_FinishPredictionFinishTimeButton, Localize("Finish time"), g_Config.m_BcFinishPredictionTimeMode == 1, &Right, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
				g_Config.m_BcFinishPredictionTimeMode = 1;

			FinishPredictionView.HSplitTop(MarginSmall, nullptr, &FinishPredictionView);
			FinishPredictionView.HSplitTop(LineSize, &Content, &FinishPredictionView);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcFinishPredictionShowMillis, Localize("Show milliseconds"), &g_Config.m_BcFinishPredictionShowMillis, &Content, LineSize);

			Ui()->ClipDisable();
		}

		FinishPredictionView.HSplitTop(MarginSmall, nullptr, &FinishPredictionView);
		FinishPredictionView.HSplitTop(LineSize, &Content, &FinishPredictionView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcFinishPredictionShowPercentage, Localize("Show percentage"), &g_Config.m_BcFinishPredictionShowPercentage, &Content, LineSize);

		FinishPredictionView.HSplitTop(MarginSmall, nullptr, &FinishPredictionView);
		FinishPredictionView.HSplitTop(LineSize, &Content, &FinishPredictionView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcFinishPredictionShowAlways, Localize("Show always"), &g_Config.m_BcFinishPredictionShowAlways, &Content, LineSize);

		Ui()->ClipDisable();
	}

	// Focus Mode (right column block)
	RightColumn.HSplitTop(MarginBetweenViews, nullptr, &RightColumn);

	const bool FocusModeEnabled = g_Config.m_ClFocusMode != 0;
	static float s_FocusModeRevealPhase = 0.0f;
	UpdateModuleRevealPhase(s_FocusModeRevealPhase, FocusModeEnabled, Client()->RenderFrameTime());
	const float FocusModeExpandedHeight = (8.0f * (MarginSmall + LineSize)) * BCUiAnimations::EaseOutCubic(s_FocusModeRevealPhase);
	const float FocusModeHeaderHeight = LineSize + MarginSmall + LineSize;
	const float FocusModeBlockHeight = FocusModeHeaderHeight + FocusModeExpandedHeight;

	CUIRect FocusModeBlock;
	RightColumn.HSplitTop(FocusModeBlockHeight, &FocusModeBlock, &RightColumn);

	CUIRect FocusModeBlockBg = FocusModeBlock;
	FocusModeBlockBg.w += BlockPadding;
	FocusModeBlockBg.h += BlockPadding;
	FocusModeBlockBg.x -= BlockPadding * 0.5f;
	FocusModeBlockBg.y -= BlockPadding * 0.5f;
	FocusModeBlockBg.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);

	MainView = FocusModeBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	Ui()->DoLabel(&Label, Localize("Focus Mode"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusMode, Localize("Enable Focus Mode"), &g_Config.m_ClFocusMode, &Content, LineSize);

	if(FocusModeExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = FocusModeExpandedHeight;
		Ui()->ClipEnable(&Visible);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideNames, Localize("Hide Player Names"), &g_Config.m_ClFocusModeHideNames, &Content, LineSize);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideEffects, Localize("Hide Visual Effects"), &g_Config.m_ClFocusModeHideEffects, &Content, LineSize);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideHud, Localize("Hide HUD"), &g_Config.m_ClFocusModeHideHud, &Content, LineSize);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideSongPlayer, Localize("Hide Song Player"), &g_Config.m_ClFocusModeHideSongPlayer, &Content, LineSize);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideUI, Localize("Hide Unnecessary UI"), &g_Config.m_ClFocusModeHideUI, &Content, LineSize);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideChat, Localize("Hide Chat"), &g_Config.m_ClFocusModeHideChat, &Content, LineSize);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideScoreboard, Localize("Hide Scoreboard"), &g_Config.m_ClFocusModeHideScoreboard, &Content, LineSize);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Label, &MainView);
		static CButtonContainer s_FocusModeBindReader;
		static CButtonContainer s_FocusModeBindClear;
		DoLine_KeyReader(Label, s_FocusModeBindReader, s_FocusModeBindClear, Localize("Focus mode bind"), "toggle p_focus_mode 0 1");

		Ui()->ClipDisable();
	}

	const float RightColumnEndY = RightColumn.y;
	CUIRect GameplayScrollContentRect;
	GameplayScrollContentRect.x = MainView.x;
	GameplayScrollContentRect.y = maximum(LeftColumnEndY, RightColumnEndY) + MarginSmall * 2.0f;
	GameplayScrollContentRect.w = MainView.w;
	GameplayScrollContentRect.h = 0.0f;
	s_GameplayScrollRegion.AddRect(GameplayScrollContentRect);
	s_GameplayScrollRegion.End();
}

void CMenus::RenderSettingsBestClientOthers(CUIRect MainView)
{
	const float LineSize = 20.0f;
	const float MarginSmall = 5.0f;
	const float HeadlineFontSize = 20.0f;
	const float MarginBetweenViews = 30.0f;
	const float BlockPadding = MarginBetweenViews * 0.6666f;
	const ColorRGBA BlockColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f);

	static CScrollRegion s_OthersScrollRegion;
	vec2 OthersScrollOffset(0.0f, 0.0f);
	CScrollRegionParams OthersScrollParams;
	OthersScrollParams.m_ScrollUnit = 60.0f;
	OthersScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	OthersScrollParams.m_ScrollbarMargin = 5.0f;
	s_OthersScrollRegion.Begin(&MainView, &OthersScrollOffset, &OthersScrollParams);
	MainView.y += OthersScrollOffset.y;
	MainView.VSplitRight(5.0f, &MainView, nullptr);
	MainView.VSplitLeft(5.0f, nullptr, &MainView);

	CUIRect LeftView, RightView;
	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	CUIRect Column = LeftView;
	CUIRect RightColumn = RightView;
	Column.HSplitTop(10.0f, nullptr, &Column);
	RightColumn.HSplitTop(10.0f, nullptr, &RightColumn);

#if defined(CONF_AUTOUPDATE)
	const float AutoUpdateHeight = LineSize;
#else
	const float AutoUpdateHeight = 0.0f;
#endif
	const float RealHitboxColorLineSize = 25.0f;
	const float RealHitboxColorLineSpacing = 5.0f;
	const float RealHitboxColorHeight = g_Config.m_BcShowRealHitbox ? RealHitboxColorLineSize + RealHitboxColorLineSpacing : 0.0f;
	const float AutoLockDelayHeight = g_Config.m_BcAutoTeamLock ? LineSize : 0.0f;
	const float SpecMovedNotifyTextHeight = g_Config.m_BcSpecMovedNotify ? LineSize : 0.0f;
	const float MiscBlockHeight = LineSize + MarginSmall + AutoUpdateHeight + 17.0f * LineSize + AutoLockDelayHeight + SpecMovedNotifyTextHeight + RealHitboxColorHeight;
	CUIRect MiscBlock;
	Column.HSplitTop(MiscBlockHeight, &MiscBlock, &Column);

	CUIRect MiscBlockBg = MiscBlock;
	MiscBlockBg.w += BlockPadding;
	MiscBlockBg.h += BlockPadding;
	MiscBlockBg.x -= BlockPadding * 0.5f;
	MiscBlockBg.y -= BlockPadding * 0.5f;
	MiscBlockBg.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);

	CUIRect Content, Label, Button;
	MiscBlock.HSplitTop(LineSize, &Label, &MiscBlock);
	Ui()->DoLabel(&Label, Localize("Misc"), HeadlineFontSize, TEXTALIGN_ML);
	MiscBlock.HSplitTop(MarginSmall, nullptr, &MiscBlock);

#if defined(CONF_AUTOUPDATE)
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcAutoUpdate, Localize("Automatic update"), &g_Config.m_BcAutoUpdate, &MiscBlock, LineSize);
#endif
	static CButtonContainer s_SettingsLayoutButton;
	int UseNewMenuLayout = g_Config.m_BcSettingsLayout == 0 ? 1 : 0;
	DoButton_CheckBoxAutoVMarginAndSet(&s_SettingsLayoutButton, Localize("Use new menu layout"), &UseNewMenuLayout, &MiscBlock, LineSize);
	g_Config.m_BcSettingsLayout = UseNewMenuLayout ? 0 : 1;
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcHideHudInSettings, Localize("Hide hud in settings"), &g_Config.m_BcHideHudInSettings, &MiscBlock, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatSaveDraft, Localize("Save unsent messages"), &g_Config.m_BcChatSaveDraft, &MiscBlock, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcSilentTyping, Localize("Silent typing"), &g_Config.m_BcSilentTyping, &MiscBlock, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatAltCommandLayout, Localize("Commands in other layout"), &g_Config.m_BcChatAltCommandLayout, &MiscBlock, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcCinematicCamera, Localize("Cinematic camera"), &g_Config.m_BcCinematicCamera, &MiscBlock, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcSpecMovedNotify, Localize("Notify when moved in spec"), &g_Config.m_BcSpecMovedNotify, &MiscBlock, LineSize);
	if(g_Config.m_BcSpecMovedNotify)
	{
		static CLineInput s_SpecMovedNotifyTextInput;
		s_SpecMovedNotifyTextInput.SetBuffer(g_Config.m_BcSpecMovedNotifyText, sizeof(g_Config.m_BcSpecMovedNotifyText));
		MiscBlock.HSplitTop(LineSize, &Button, &MiscBlock);
		CUIRect TextLabel, TextField;
		Button.VSplitMid(&TextLabel, &TextField, minimum(10.0f, Button.w * 0.05f));
		Ui()->DoLabel(&TextLabel, Localize("Notification text"), 14.0f, TEXTALIGN_ML);
		Ui()->DoEditBox(&s_SpecMovedNotifyTextInput, &TextField, 14.0f);
	}
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcScoreboardTeamGradients, Localize("Gradient team colors"), &g_Config.m_BcScoreboardTeamGradients, &MiscBlock, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMastersrv, Localize("Use BestClient MasterServer"), &g_Config.m_BcMastersrv, &MiscBlock, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcShowhudDummyCoordIndicator, Localize("Show player below indicator"), &g_Config.m_BcShowhudDummyCoordIndicator, &MiscBlock, LineSize);
	CUIRect CheckpointButtonView;
	MiscBlock.HSplitTop(LineSize, &CheckpointButtonView, &MiscBlock);
	DrawBcMenuBadge(Graphics(), Ui(), TextRender(), &CheckpointButtonView, Localize("NEW"), 11.0f,
		ColorRGBA(0.25f, 0.85f, 0.40f, 1.0f), ColorRGBA(0.10f, 0.60f, 0.25f, 1.0f), MarginSmall);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcShowCorrectCheckpoint, Localize("Show correct checkpoint"), &g_Config.m_BcShowCorrectCheckpoint, &CheckpointButtonView, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcShowRealHitbox, Localize("Show real hitbox"), &g_Config.m_BcShowRealHitbox, &MiscBlock, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcAutoTeamLock, Localize("Lock team automatically after joining"), &g_Config.m_BcAutoTeamLock, &MiscBlock, LineSize);
	if(g_Config.m_BcAutoTeamLock)
	{
		MiscBlock.HSplitTop(LineSize, &Button, &MiscBlock);
		Ui()->DoScrollbarOption(&g_Config.m_BcAutoTeamLockDelay, &g_Config.m_BcAutoTeamLockDelay, &Button, Localize("Auto lock delay"), 0, 30, &CUi::ms_LinearScrollbarScale, 0, "s");
	}
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcExtendZoom, Localize("Extend zoom (0.5 steps)"), &g_Config.m_BcExtendZoom, &MiscBlock, LineSize);
	MiscBlock.HSplitTop(LineSize, &Button, &MiscBlock);
	Ui()->DoScrollbarOption(&g_Config.m_UiScale, &g_Config.m_UiScale, &Button, Localize("UI scale"), 50, 110, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, "%");
	MiscBlock.HSplitTop(LineSize, &Button, &MiscBlock);
	Ui()->DoScrollbarOption(&g_Config.m_BcWheelScale, &g_Config.m_BcWheelScale, &Button, Localize("Bind/Emote wheel scale"), 50, 200, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, "%");
	MiscBlock.HSplitTop(LineSize, &Button, &MiscBlock);
	Ui()->DoScrollbarOption(&g_Config.m_BcScoreboardScale, &g_Config.m_BcScoreboardScale, &Button, Localize("Scoreboard scale"), 50, 200, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, "%");
	if(g_Config.m_BcShowRealHitbox)
	{
		static CButtonContainer s_RealHitboxDotColorButton;
		DoLine_ColorPicker(&s_RealHitboxDotColorButton, RealHitboxColorLineSize, 13.0f, RealHitboxColorLineSpacing, &MiscBlock, Localize("Real hitbox dot color"), &g_Config.m_BcShowRealHitboxColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::BcShowRealHitboxColor, true)), false, nullptr, true);
	}

	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	const bool RollbackExpanded = g_Config.m_ClReplays != 0;
	static float s_RollbackRevealPhase = 0.0f;
	UpdateModuleRevealPhase(s_RollbackRevealPhase, RollbackExpanded, Client()->RenderFrameTime());
	const float HeaderHeight = LineSize + MarginSmall + LineSize;
	const float ExpandedHeight = 2.0f * (MarginSmall + LineSize) * BCUiAnimations::EaseOutCubic(s_RollbackRevealPhase);
	const float BlockHeight = HeaderHeight + ExpandedHeight;

	CUIRect Block;
	Column.HSplitTop(BlockHeight, &Block, &Column);

	CUIRect BlockBg = Block;
	BlockBg.w += BlockPadding;
	BlockBg.h += BlockPadding;
	BlockBg.x -= BlockPadding * 0.5f;
	BlockBg.y -= BlockPadding * 0.5f;
	BlockBg.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);

	Block.HSplitTop(LineSize, &Label, &Block);
	Ui()->DoLabel(&Label, Localize("Rollback Demo"), HeadlineFontSize, TEXTALIGN_ML);
	Block.HSplitTop(MarginSmall, nullptr, &Block);

	Block.HSplitTop(LineSize, &Content, &Block);
	if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClReplays, Localize("Enable rollback demo recording"), &g_Config.m_ClReplays, &Content, LineSize))
	{
		if(Client()->State() == IClient::STATE_ONLINE)
			Client()->DemoRecorder_UpdateReplayRecorder();
	}

	if(ExpandedHeight > 0.5f)
	{
		CUIRect Visible = Block;
		Visible.h = ExpandedHeight;
		Ui()->ClipEnable(&Visible);

		g_Config.m_ClReplayLength = std::clamp(g_Config.m_ClReplayLength, 10, 60);

		Block.HSplitTop(MarginSmall, nullptr, &Block);
		Block.HSplitTop(LineSize, &Button, &Block);
		Ui()->DoScrollbarOption(&g_Config.m_ClReplayLength, &g_Config.m_ClReplayLength, &Button, Localize("Rollback length"), 10, 60, &CUi::ms_LinearScrollbarScale, 0, " s");

		Block.HSplitTop(MarginSmall, nullptr, &Block);
		Block.HSplitTop(LineSize, &Button, &Block);
		static CButtonContainer s_RollbackBindReader;
		static CButtonContainer s_RollbackBindClear;
		DoLine_KeyReader(Button, s_RollbackBindReader, s_RollbackBindClear, Localize("Rollback bind"), "BC_save_rollback");

		Ui()->ClipDisable();
	}

	// Browser Utils (below Rollback Demo)
	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	const float BrowserUtilsBlockHeight = 4.0f * LineSize + 3.0f * MarginSmall;

	CUIRect BrowserUtilsBlock;
	Column.HSplitTop(BrowserUtilsBlockHeight, &BrowserUtilsBlock, &Column);

	CUIRect BrowserUtilsBlockBg = BrowserUtilsBlock;
	BrowserUtilsBlockBg.w += BlockPadding;
	BrowserUtilsBlockBg.h += BlockPadding;
	BrowserUtilsBlockBg.x -= BlockPadding * 0.5f;
	BrowserUtilsBlockBg.y -= BlockPadding * 0.5f;
	BrowserUtilsBlockBg.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);

	BrowserUtilsBlock.HSplitTop(LineSize, &Label, &BrowserUtilsBlock);
	Ui()->DoLabel(&Label, Localize("Browser Utils"), HeadlineFontSize, TEXTALIGN_ML);
	BrowserUtilsBlock.HSplitTop(MarginSmall, nullptr, &BrowserUtilsBlock);

	BrowserUtilsBlock.HSplitTop(LineSize, &Content, &BrowserUtilsBlock);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcAutoServerListRefresh, Localize("Auto server list refresh"), &g_Config.m_BcAutoServerListRefresh, &Content, LineSize);

	BrowserUtilsBlock.HSplitTop(MarginSmall, nullptr, &BrowserUtilsBlock);
	BrowserUtilsBlock.HSplitTop(LineSize, &Button, &BrowserUtilsBlock);
	Ui()->DoScrollbarOption(&g_Config.m_BcAutoServerListRefreshSeconds, &g_Config.m_BcAutoServerListRefreshSeconds, &Button, Localize("Seconds"), 1, 300, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, " s");

	BrowserUtilsBlock.HSplitTop(MarginSmall, nullptr, &BrowserUtilsBlock);
	BrowserUtilsBlock.HSplitTop(LineSize, &Content, &BrowserUtilsBlock);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcUseShortKogServerName, Localize("Use short KoG server name"), &g_Config.m_BcUseShortKogServerName, &Content, LineSize);

	// Chat Filter
	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	const bool ChatFilterEnabled = g_Config.m_BcEnableCensorList != 0;
	static float s_ChatFilterRevealPhase = 0.0f;
	UpdateModuleRevealPhase(s_ChatFilterRevealPhase, ChatFilterEnabled, Client()->RenderFrameTime());

	const float ChatFilterColorHeight = g_Config.m_BcShowBlockedWordInConsole ? LineSize + MarginSmall : 0.0f;
	const float ChatFilterPartialHeight = g_Config.m_BcFilterChangeWholeWord == 2 ? MarginSmall + LineSize : 0.0f;
	const float ChatFilterRadioHeight = 2.0f + LineSize;
	const float ChatFilterExpandedTargetHeight = MarginSmall + 3.0f * (LineSize + MarginSmall) + ChatFilterColorHeight + LineSize + MarginSmall + ChatFilterRadioHeight + ChatFilterPartialHeight;
	const float ChatFilterExpandedHeight = ChatFilterExpandedTargetHeight * BCUiAnimations::EaseOutCubic(s_ChatFilterRevealPhase);
	const float ChatFilterHeaderHeight = LineSize + MarginSmall + LineSize;
	const float ChatFilterBlockHeight = ChatFilterHeaderHeight + ChatFilterExpandedHeight;

	CUIRect ChatFilterBlock;
	Column.HSplitTop(ChatFilterBlockHeight, &ChatFilterBlock, &Column);

	CUIRect ChatFilterBlockBg = ChatFilterBlock;
	ChatFilterBlockBg.w += BlockPadding;
	ChatFilterBlockBg.h += BlockPadding;
	ChatFilterBlockBg.x -= BlockPadding * 0.5f;
	ChatFilterBlockBg.y -= BlockPadding * 0.5f;
	ChatFilterBlockBg.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);

	ChatFilterBlock.HSplitTop(LineSize, &Label, &ChatFilterBlock);
	DrawBcMenuBadge(Graphics(), Ui(), TextRender(), &Label, Localize("NEW"), 12.0f,
		ColorRGBA(0.25f, 0.85f, 0.40f, 1.0f), ColorRGBA(0.10f, 0.60f, 0.25f, 1.0f), MarginSmall);
	Ui()->DoLabel(&Label, Localize("Chat Filter"), HeadlineFontSize, TEXTALIGN_ML);
	ChatFilterBlock.HSplitTop(MarginSmall, nullptr, &ChatFilterBlock);

	ChatFilterBlock.HSplitTop(LineSize, &Content, &ChatFilterBlock);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcEnableCensorList, Localize("Enable chat filter"), &g_Config.m_BcEnableCensorList, &Content, LineSize);
	GameClient()->m_Tooltips.DoToolTip(&g_Config.m_BcEnableCensorList, &Content, Localize("Replacing blocked word with replacement char(badbad->******)"));

	if(ChatFilterExpandedHeight > 0.5f)
	{
		CUIRect Visible = ChatFilterBlock;
		Visible.h = ChatFilterExpandedHeight;
		Ui()->ClipEnable(&Visible);

		ChatFilterBlock.HSplitTop(MarginSmall, nullptr, &ChatFilterBlock);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcShowBlockedWordInConsole, Localize("Show blocked word in console"), &g_Config.m_BcShowBlockedWordInConsole, &ChatFilterBlock, LineSize);
		GameClient()->m_Tooltips.DoToolTip(&g_Config.m_BcShowBlockedWordInConsole, &ChatFilterBlock, Localize("In console will be like 'tee said badbad'"));
		ChatFilterBlock.HSplitTop(MarginSmall, nullptr, &ChatFilterBlock);
		if(g_Config.m_BcShowBlockedWordInConsole)
		{
			static CButtonContainer s_BlockedWordConsoleColorButton;
			DoLine_ColorPicker(&s_BlockedWordConsoleColorButton, LineSize, 13.0f, MarginSmall, &ChatFilterBlock, Localize("Blocked words console color"), &g_Config.m_BcBlockedWordConsoleColor, color_cast<ColorRGBA>(ColorHSLA(0x99ffff)), false, nullptr, false);
		}

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMultipleReplacementChar, Localize("Multiple replacement char on blocked word len"), &g_Config.m_BcMultipleReplacementChar, &ChatFilterBlock, LineSize);
		GameClient()->m_Tooltips.DoToolTip(&g_Config.m_BcMultipleReplacementChar, &ChatFilterBlock, Localize("if no will be 'badbad->*' if yes 'badbad->******'"));
		ChatFilterBlock.HSplitTop(MarginSmall, nullptr, &ChatFilterBlock);

		static CLineInput s_ReplacementChar;
		s_ReplacementChar.SetBuffer(g_Config.m_BcBlockedContentReplacementChar, sizeof(g_Config.m_BcBlockedContentReplacementChar));
		ChatFilterBlock.HSplitTop(LineSize, &Label, &ChatFilterBlock);
		DoEditBoxWithLabel(&s_ReplacementChar, &Label, Localize("Replacement char"), "*", g_Config.m_BcBlockedContentReplacementChar, sizeof(g_Config.m_BcBlockedContentReplacementChar));
		ChatFilterBlock.HSplitTop(MarginSmall, nullptr, &ChatFilterBlock);

		static std::vector<CButtonContainer> s_vChatFilterModeButtons = {{}, {}, {}};
		DoLine_RadioMenu(ChatFilterBlock, Localize("Replace word with:", "ChatFilter"),
			s_vChatFilterModeButtons,
			{Localize("Regex", "ChatFilter"), Localize("Full", "ChatFilter"), Localize("Both", "ChatFilter")},
			{0, 1, 2},
			g_Config.m_BcFilterChangeWholeWord);
		if(g_Config.m_BcFilterChangeWholeWord == 2)
		{
			ChatFilterBlock.HSplitTop(MarginSmall, nullptr, &ChatFilterBlock);
			static CLineInput s_PartialReplacementChar;
			s_PartialReplacementChar.SetBuffer(g_Config.m_BcBlockedContentPartialReplacementChar, sizeof(g_Config.m_BcBlockedContentPartialReplacementChar));
			ChatFilterBlock.HSplitTop(LineSize, &Label, &ChatFilterBlock);
			DoEditBoxWithLabel(&s_PartialReplacementChar, &Label, Localize("Partial Replacement char"), "*", g_Config.m_BcBlockedContentPartialReplacementChar, sizeof(g_Config.m_BcBlockedContentPartialReplacementChar));
		}

		Ui()->ClipDisable();
	}

	const bool VoiceExpanded = g_Config.m_BcVoiceChatEnable != 0;
	static float s_VoiceRevealPhase = 0.0f;
	UpdateModuleRevealPhase(s_VoiceRevealPhase, VoiceExpanded, Client()->RenderFrameTime());
	const float VoiceSettingsBlockHeight = GameClient()->m_VoiceChat.GetMenuSettingsBlockHeight(s_VoiceRevealPhase) + LineSize + MarginSmall;

	CUIRect VoiceSettingsBlock;
	RightColumn.HSplitTop(VoiceSettingsBlockHeight, &VoiceSettingsBlock, &RightColumn);

	CUIRect VoiceSettingsBlockBg = VoiceSettingsBlock;
	VoiceSettingsBlockBg.w += BlockPadding;
	VoiceSettingsBlockBg.h += BlockPadding;
	VoiceSettingsBlockBg.x -= BlockPadding * 0.5f;
	VoiceSettingsBlockBg.y -= BlockPadding * 0.5f;
	VoiceSettingsBlockBg.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);

	CUIRect VoiceSettingsContent = VoiceSettingsBlock;
	VoiceSettingsContent.HSplitBottom(LineSize, &VoiceSettingsContent, &Button);
	Button.VSplitRight(150.0f, nullptr, &Button);
	static CButtonContainer s_VoiceModerationButton;
	if(DoButton_Menu(&s_VoiceModerationButton, Localize("Voice Moderation"), 0, &Button))
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
	VoiceBindsBlockBg.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);

	VoiceBindsBlock.HSplitTop(LineSize, &Label, &VoiceBindsBlock);
	Ui()->DoLabel(&Label, Localize("Voice Binds"), HeadlineFontSize, TEXTALIGN_ML);
	VoiceBindsBlock.HSplitTop(MarginSmall, nullptr, &VoiceBindsBlock);

	static CButtonContainer s_PttBindReader;
	static CButtonContainer s_PttBindClear;
	static CButtonContainer s_MicMuteBindReader;
	static CButtonContainer s_MicMuteBindClear;
	static CButtonContainer s_HeadphonesMuteBindReader;
	static CButtonContainer s_HeadphonesMuteBindClear;
	VoiceBindsBlock.HSplitTop(LineSize, &Button, &VoiceBindsBlock);
	DoLine_KeyReader(Button, s_PttBindReader, s_PttBindClear, Localize("Push-to-talk"), "+voicechat");
	VoiceBindsBlock.HSplitTop(MarginSmall, nullptr, &VoiceBindsBlock);
	VoiceBindsBlock.HSplitTop(LineSize, &Button, &VoiceBindsBlock);
	DoLine_KeyReader(Button, s_MicMuteBindReader, s_MicMuteBindClear, Localize("Mute microphone"), "toggle_voice_mic_mute");
	VoiceBindsBlock.HSplitTop(MarginSmall, nullptr, &VoiceBindsBlock);
	VoiceBindsBlock.HSplitTop(LineSize, &Button, &VoiceBindsBlock);
	DoLine_KeyReader(Button, s_HeadphonesMuteBindReader, s_HeadphonesMuteBindClear, Localize("Mute headphones"), "toggle_voice_headphones_mute");

	// Client Indicator (right column block)
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
	ClientIndicatorBlockBg.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);

	ClientIndicatorBlock.HSplitTop(LineSize, &Label, &ClientIndicatorBlock);
	Ui()->DoLabel(&Label, Localize("Client Indicator"), HeadlineFontSize, TEXTALIGN_ML);
	ClientIndicatorBlock.HSplitTop(MarginSmall, nullptr, &ClientIndicatorBlock);

	ClientIndicatorBlock.HSplitTop(LineSize, &Content, &ClientIndicatorBlock);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcClientIndicatorInNamePlate, Localize("Show indicator in name plates"), &g_Config.m_BcClientIndicatorInNamePlate, &Content, LineSize);

	if(ShowNamePlateSettings)
	{
		ClientIndicatorBlock.HSplitTop(LineSize, &Content, &ClientIndicatorBlock);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcClientIndicatorInNamePlateAboveSelf, Localize("Show above yourself"), &g_Config.m_BcClientIndicatorInNamePlateAboveSelf, &Content, LineSize);

		ClientIndicatorBlock.HSplitTop(LineSize, &Button, &ClientIndicatorBlock);
		Ui()->DoScrollbarOption(&g_Config.m_BcClientIndicatorInNamePlateSize, &g_Config.m_BcClientIndicatorInNamePlateSize, &Button, Localize("Name plate indicator size"), -50, 100);
	}

	ClientIndicatorBlock.HSplitTop(LineSize, &Content, &ClientIndicatorBlock);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcClientIndicatorInScoreboard, Localize("Show indicator in scoreboard"), &g_Config.m_BcClientIndicatorInScoreboard, &Content, LineSize);

	if(ShowScoreboardSettings)
	{
		ClientIndicatorBlock.HSplitTop(LineSize, &Button, &ClientIndicatorBlock);
		Ui()->DoScrollbarOption(&g_Config.m_BcClientIndicatorInSoreboardSize, &g_Config.m_BcClientIndicatorInSoreboardSize, &Button, Localize("Scoreboard indicator size"), -50, 100);
	}

	CUIRect OthersScrollContentRect;
	OthersScrollContentRect.x = MainView.x;
	OthersScrollContentRect.y = maximum(Column.y, RightColumn.y) + MarginSmall * 2.0f;
	OthersScrollContentRect.w = MainView.w;
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
		pMenus->Ui()->DoLabel(&Row, Localize("Not connected to voice server"), 13.0f, TEXTALIGN_MC);
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
		pMenus->Ui()->DoLabel(&Row, Localize("Voice Moderator Login"), 14.0f, TEXTALIGN_MC);
		View.HSplitTop(Pad, nullptr, &View);

		CUIRect LabelRect, FieldRect;
		View.HSplitTop(RowH, &Row, &View);
		Row.VSplitLeft(80.0f, &LabelRect, &FieldRect);
		pMenus->Ui()->DoLabel(&LabelRect, Localize("Mod key:"), 12.0f, TEXTALIGN_ML);
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
			pMenus->Ui()->DoLabel(&Row, Localize("Authenticating..."), 12.0f, TEXTALIGN_MC);
		}
		else if(Voice.IsVoiceModAuthFailed())
		{
			CUIRect MsgRect, BtnRect;
			Row.VSplitRight(110.0f, &MsgRect, &BtnRect);
			pMenus->TextRender()->TextColor(ColorRGBA(1.0f, 0.3f, 0.3f, 1.0f));
			pMenus->Ui()->DoLabel(&MsgRect, Localize("Wrong key"), 12.0f, TEXTALIGN_ML);
			pMenus->TextRender()->TextColor(pMenus->TextRender()->DefaultTextColor());
			if(pMenus->DoButton_Menu(&s_VoiceModAuthButton, Localize("Try again"), 0, &BtnRect))
				DoLogin();
		}
		else
		{
			if(pMenus->DoButton_Menu(&s_VoiceModAuthButton, Localize("Login as Voice Mod"), 0, &Row))
				DoLogin();
		}
		return CUi::POPUP_KEEP_OPEN;
	}

	View.HSplitTop(RowH, &Row, &View);
	CUIRect TitleRect, RefreshBtn;
	Row.VSplitRight(90.0f, &TitleRect, &RefreshBtn);
	pMenus->Ui()->DoLabel(&TitleRect, Localize("Voice players on this server"), 13.0f, TEXTALIGN_ML);
	if(pMenus->DoButton_Menu(&s_VoiceModRefreshButton, Localize("Refresh"), 0, &RefreshBtn))
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
		pMenus->Ui()->DoLabel(&Row, Localize("No players in current voice room"), 12.0f, TEXTALIGN_MC);
		return CUi::POPUP_KEEP_OPEN;
	}

	if(s_vVoiceModMuteButtons.size() != Players.size())
		s_vVoiceModMuteButtons.resize(Players.size());

	View.HSplitTop(18.0f, &Row, &View);
	CUIRect NameHeader, ActionHeader;
	Row.VSplitRight(80.0f, &NameHeader, &ActionHeader);
	pMenus->TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f));
	pMenus->Ui()->DoLabel(&NameHeader, Localize("Player"), 11.0f, TEXTALIGN_ML);
	pMenus->Ui()->DoLabel(&ActionHeader, Localize("Action"), 11.0f, TEXTALIGN_MC);
	pMenus->TextRender()->TextColor(pMenus->TextRender()->DefaultTextColor());

	static CScrollRegion s_VoiceModScroll;
	static vec2 s_VoiceModScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = RowH + 4.0f;
	ScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	ScrollParams.m_ScrollbarMargin = 4.0f;
	s_VoiceModScroll.Begin(&View, &s_VoiceModScrollOffset, &ScrollParams);
	View.y += s_VoiceModScrollOffset.y;

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
			str_format(aName, sizeof(aName), Localize("slot %d"), (int)Player.m_GameClientId);
		else
			str_copy(aName, Player.m_Name.c_str(), sizeof(aName));

		if(Player.m_IsMuted)
			pMenus->TextRender()->TextColor(ColorRGBA(1.0f, 0.4f, 0.4f, 1.0f));
		pMenus->Ui()->DoLabel(&NameRect, aName, 11.5f, TEXTALIGN_ML);
		if(Player.m_IsMuted)
			pMenus->TextRender()->TextColor(pMenus->TextRender()->DefaultTextColor());

		const char *pBtnLabel = Player.m_IsMuted ? Localize("Unmute") : Localize("Mute");
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
	MainView.HSplitTop(20.0f, nullptr, &MainView);

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);
	LeftView.HSplitMid(&LeftView, &LowerLeftView, 0.0f);

	// ── BestClient Links ───────────────────────────────────────────────────
	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, Localize("BestClient Links"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	static CButtonContainer s_DiscordButton, s_WebsiteButton, s_TelegramButton, s_CheckUpdateButton;
	CUIRect ButtonLeft, ButtonRight;

	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&ButtonLeft, &ButtonRight, MarginSmall);
	if(DoButtonLineSize_Menu(&s_DiscordButton, Localize("Discord"), 0, &ButtonLeft, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://discord.gg/bestclient");
	if(DoButtonLineSize_Menu(&s_TelegramButton, Localize("Telegram"), 0, &ButtonRight, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://t.me/bestddnet");

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&ButtonLeft, &ButtonRight, MarginSmall);
	if(DoButtonLineSize_Menu(&s_WebsiteButton, Localize("Website"), 0, &ButtonLeft, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://bestclient.fun");
	// "Check update" rendered without action (not yet ported)
	DoButtonLineSize_Menu(&s_CheckUpdateButton, Localize("Check update"), 0, &ButtonRight, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f));

	// ── Editors ────────────────────────────────────────────────────────────
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, Localize("Editors"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	{
		const float LSize = 20.0f;
		CUIRect EditorLabel, EditorButton;

		LeftView.HSplitTop(LSize, &EditorLabel, &LeftView);
		Ui()->DoLabel(&EditorLabel, Localize("Create mixed assets or jump to the name plate editor."), 14.0f, TEXTALIGN_ML);
		LeftView.HSplitTop(5.0f, nullptr, &LeftView);
		static CButtonContainer s_AssetsEditorButton;
		LeftView.HSplitTop(LSize + 4.0f, &EditorButton, &LeftView);
		if(DoButton_Menu(&s_AssetsEditorButton, Localize("Assets editor"), 0, &EditorButton))
		{
			m_AssetsEditorState.m_VisualsEditorOpen = true;
			m_AssetsEditorState.m_FullscreenOpen = true;
			if(!m_AssetsEditorState.m_VisualsEditorInitialized)
			{
				AssetsEditorReloadAssets();
				AssetsEditorResetPartSlots();
				AssetsEditorEnsureDefaultExportNames();
				AssetsEditorSyncExportNameFromType();
				m_AssetsEditorState.m_VisualsEditorInitialized = true;
			}
		}
	}

	// ── Config Files (anchored to the bottom of the left column) ──────────
	LeftView = LowerLeftView;
	LeftView.HSplitBottom(LineSize * 2.0f + MarginSmall * 2.0f + HeadlineFontSize, nullptr, &LeftView);
	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, Localize("Config Files"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	char aBuf[128 + IO_MAX_PATH_LENGTH];
	CUIRect BestClientConfig;
	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	BestClientConfig = Button;

	static CButtonContainer s_Config;
	if(DoButtonLineSize_Menu(&s_Config, Localize("BestClient Settings"), 0, &BestClientConfig, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::BESTCLIENT].m_aConfigPath, aBuf, sizeof(aBuf));
		Client()->ViewFile(aBuf);
	}

	// ── BestClient Developers ─────────────────────────────────────────────
	RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	Ui()->DoLabel(&Label, Localize("BestClient Developers"), HeadlineFontSize, TEXTALIGN_ML);
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
	Ui()->DoLabel(&Label, Localize("Hide Settings Tabs"), HeadlineFontSize, TEXTALIGN_ML);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);

	const char *apTabNames[] = {
		Localize("Visuals"),
		Localize("Gameplay"),
		Localize("Others"),
		Localize("Fun"),
		Localize("Info"),
	};
	const int aTabOrder[NUM_BESTCLIENT_TABS] = {
		BESTCLIENT_TAB_VISUALS,
		BESTCLIENT_TAB_GAMEPLAY,
		BESTCLIENT_TAB_OTHERS,
		BESTCLIENT_TAB_FUN,
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
