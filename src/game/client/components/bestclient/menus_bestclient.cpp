#include <base/log.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/localization.h>
#include <engine/shared/protocol7.h>
#include <engine/storage.h>
#include <engine/textrender.h>
#include <engine/updater.h>

#include <generated/protocol.h>

#include <game/client/animstate.h>
#include <game/client/bc_ui_animations.h>
#include <game/client/components/chat.h>
#include <game/client/components/media_decoder.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/menus.h>
#include <game/client/components/sounds.h>
#include <game/client/gameclient.h>
#include <game/client/skin.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <array>
#include <chrono>
#include <cmath>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

using namespace std::chrono_literals;

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

enum
{
	COMPONENTS_GROUP_VISUALS = 0,
	COMPONENTS_GROUP_GAMEPLAY,
	COMPONENTS_GROUP_OTHERS,
	COMPONENTS_GROUP_TCLIENT,
	NUM_COMPONENTS_GROUPS,
};

struct SBestClientComponentEntry
{
	CBestClient::EBestClientComponent m_Component;
	const char *m_pName;
	int m_Group;
};

static const SBestClientComponentEntry gs_aBestClientComponentEntries[] = {
	{CBestClient::COMPONENT_VISUALS_CAMERA_DRIFT, "Camera Drift", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_JELLY_TEE, "Jelly Tee", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_MAGIC_PARTICLES, "Magic Particles", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_ORBIT_AURA, "Orbit Aura", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_3D_PARTICLES, "3D Particles", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_DYNAMIC_FOV, "Dynamic FOV", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_AFTERIMAGE, "Afterimage", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_CRYSTAL_LASER, "Crystal Laser", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_MUSIC_PLAYER, "Music Player", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_MEDIA_BACKGROUND, "Media Background", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_ANIMATIONS, "Animations", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_ASPECT_RATIO, "Aspect Ratio", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_GAMEPLAY_HOOK_COMBO, "Hook Combo", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_GAMEPLAY_INPUT, "Input", COMPONENTS_GROUP_GAMEPLAY},
	{CBestClient::COMPONENT_GAMEPLAY_FAST_ACTIONS, "Fast Actions", COMPONENTS_GROUP_GAMEPLAY},
	{CBestClient::COMPONENT_GAMEPLAY_SPEEDRUN_TIMER, "Speedrun Timer", COMPONENTS_GROUP_GAMEPLAY},
	{CBestClient::COMPONENT_GAMEPLAY_AUTO_TEAM_LOCK, "Auto Team Lock", COMPONENTS_GROUP_GAMEPLAY},
	{CBestClient::COMPONENT_GAMEPLAY_GORES_MODE, "Gores Mode", COMPONENTS_GROUP_GAMEPLAY},
	{CBestClient::COMPONENT_VISUALS_OPTIMIZER, "Optimizer", COMPONENTS_GROUP_GAMEPLAY},
	{CBestClient::COMPONENT_VISUALS_FOCUS_MODE, "Focus Mode", COMPONENTS_GROUP_GAMEPLAY},
	{CBestClient::COMPONENT_OTHERS_MISC, "Misc", COMPONENTS_GROUP_OTHERS},
	{CBestClient::COMPONENT_OTHERS_CHAT_MEDIA, "Chat Media", COMPONENTS_GROUP_OTHERS},
	{CBestClient::COMPONENT_VISUALS_CHAT_BUBBLES, "Chat Bubbles", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_OTHERS_VOICE_SETTINGS, "Voice Chat", COMPONENTS_GROUP_OTHERS},
	{CBestClient::COMPONENT_OTHERS_VOICE_BINDS, "Voice Binds", COMPONENTS_GROUP_OTHERS},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_TAB, "Settings tab", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_BIND_WHEEL_TAB, "Bind wheel tab", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_WAR_LIST_TAB, "War list tab", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_CHAT_BINDS_TAB, "Chat binds tab", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_STATUS_BAR_TAB, "Status bar tab", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_INFO_TAB, "Info tab", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_PROFILES_PAGE, "Profiles page", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_CONFIGS_PAGE, "Configs page", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_VISUAL, "Settings: Visual", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_ANTI_LATENCY, "Settings: Anti Latency Tools", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_ANTI_PING_SMOOTHING, "Settings: Anti Ping Smoothing", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_AUTO_EXECUTE, "Settings: Auto execute", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_VOTING, "Settings: Voting", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_AUTO_REPLY, "Settings: Auto Reply", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_PLAYER_INDICATOR, "Settings: Player Indicator", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_PET, "Settings: Pet", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_HUD, "Settings: HUD", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_FROZEN_TEE_DISPLAY, "Settings: Frozen Tee Display", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_TILE_OUTLINES, "Settings: Tile Outlines", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_GHOST_TOOLS, "Settings: Ghost Tools", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_RAINBOW, "Settings: Rainbow", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_TEE_TRAILS, "Settings: Tee Trails", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_BACKGROUND_DRAW, "Settings: Background Draw", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_FINISH_NAME, "Settings: Finish Name", COMPONENTS_GROUP_TCLIENT},
};

static bool ComponentsEditorIsDisabled(int Component, int MaskLo, int MaskHi)
{
	return CBestClient::IsComponentDisabledByMask(Component, MaskLo, MaskHi);
}

static void ComponentsEditorSetDisabled(int Component, int &MaskLo, int &MaskHi, bool Disabled)
{
	if(Component < 0 || Component >= CBestClient::NUM_COMPONENTS_EDITOR_COMPONENTS)
		return;

	int *pMask = &MaskLo;
	int Bit = Component;
	if(Component >= 31)
	{
		pMask = &MaskHi;
		Bit = Component - 31;
	}

	if(Disabled)
		*pMask |= (1 << Bit);
	else
		*pMask &= ~(1 << Bit);
}

void CMenus::RenderSettingsBestClient(CUIRect MainView)
{
	enum
	{
		BESTCLIENT_TAB_VISUALS = 0,
		BESTCLIENT_TAB_GAMEPLAY,
		BESTCLIENT_TAB_OTHERS,
		BESTCLIENT_TAB_FUN,
		BESTCLIENT_TAB_SHOP,
		BESTCLIENT_TAB_EDITORS,
		BESTCLIENT_TAB_INFO,
		NUM_BESTCLIENT_TABS,
	};

	static int s_CurTab = BESTCLIENT_TAB_VISUALS;
	static CButtonContainer s_aPageTabs[NUM_BESTCLIENT_TABS] = {};

	if(m_AssetsEditorState.m_VisualsEditorOpen && m_AssetsEditorState.m_FullscreenOpen)
	{
		s_CurTab = BESTCLIENT_TAB_EDITORS;
		SetBestClientShopVisible(false);
		RenderAssetsEditorScreen(*Ui()->Screen());
		return;
	}
	if(m_ComponentsEditorState.m_Open && m_ComponentsEditorState.m_FullscreenOpen)
	{
		s_CurTab = BESTCLIENT_TAB_EDITORS;
		SetBestClientShopVisible(false);
		RenderComponentsEditorScreen(*Ui()->Screen());
		return;
	}

	CUIRect TabBar, Button;
	MainView.HSplitTop(24.0f, &TabBar, &MainView);
	const char *apTabNames[NUM_BESTCLIENT_TABS] = {
		BCLocalize("Visuals"),
		BCLocalize("Gameplay"),
		BCLocalize("Others"),
		BCLocalize("Fun"),
		BCLocalize("Shop"),
		BCLocalize("Editors"),
		BCLocalize("Info"),
	};
	const int aTabOrder[NUM_BESTCLIENT_TABS] = {
		BESTCLIENT_TAB_VISUALS,
		BESTCLIENT_TAB_GAMEPLAY,
		BESTCLIENT_TAB_OTHERS,
		BESTCLIENT_TAB_EDITORS,
		BESTCLIENT_TAB_FUN,
		BESTCLIENT_TAB_SHOP,
		BESTCLIENT_TAB_INFO,
	};

	auto IsTabHidden = [&](int Tab) {
		// Keep Info always visible.
		return Tab != BESTCLIENT_TAB_INFO && IsBestClientTabFlagSet(g_Config.m_BcBestClientSettingsTabs, Tab);
	};

	int TabCount = 0;
	int FirstVisibleTab = -1;
	for(int TabIndex = 0; TabIndex < NUM_BESTCLIENT_TABS; ++TabIndex)
	{
		const int Tab = aTabOrder[TabIndex];
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
	for(int TabIndex = 0; TabIndex < NUM_BESTCLIENT_TABS; ++TabIndex)
	{
		const int Tab = aTabOrder[TabIndex];
		if(IsTabHidden(Tab))
			continue;

		TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
		const int Corners = VisibleIndex == 0 ? IGraphics::CORNER_L : (VisibleIndex == TabCount - 1 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
		if(DoButton_MenuTab(&s_aPageTabs[Tab], apTabNames[Tab], s_CurTab == Tab, &Button, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
		{
			s_CurTab = Tab;
		}
		VisibleIndex++;
	}

	MainView.HSplitTop(10.0f, nullptr, &MainView);
	SetBestClientShopVisible(s_CurTab == BESTCLIENT_TAB_SHOP);

	if(s_CurTab == BESTCLIENT_TAB_VISUALS)
	{
		const float LineSize = 20.0f;
		const float HeadlineFontSize = 20.0f;
		const float MarginSmall = 5.0f;
		const float MarginBetweenSections = 30.0f;
		const float MarginBetweenViews = 30.0f;
		const ColorRGBA BlockColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f);
		const auto ModuleUiRevealAnimationsEnabled = [&]() {
			return BCUiAnimations::Enabled() && g_Config.m_BcModuleUiRevealAnimation != 0;
		};
		const auto ModuleUiRevealAnimationDuration = [&]() {
			return BCUiAnimations::MsToSeconds(g_Config.m_BcModuleUiRevealAnimationMs);
		};
		const auto UpdateRevealPhase = [&](float &Phase, bool Expanded) {
			if(ModuleUiRevealAnimationsEnabled())
				BCUiAnimations::UpdatePhase(Phase, Expanded ? 1.0f : 0.0f, Client()->RenderFrameTime(), ModuleUiRevealAnimationDuration());
			else
				Phase = Expanded ? 1.0f : 0.0f;
		};

		static CScrollRegion s_BestClientVisualsScrollRegion;
		vec2 VisualsScrollOffset(0.0f, 0.0f);
		CScrollRegionParams VisualsScrollParams;
		VisualsScrollParams.m_ScrollUnit = 60.0f;
		VisualsScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
		VisualsScrollParams.m_ScrollbarMargin = 5.0f;
		s_BestClientVisualsScrollRegion.Begin(&MainView, &VisualsScrollOffset, &VisualsScrollParams);

		MainView.y += VisualsScrollOffset.y;
		MainView.VSplitRight(5.0f, &MainView, nullptr);
		MainView.VSplitLeft(5.0f, nullptr, &MainView);

		const bool IsOnline = Client()->State() == IClient::STATE_ONLINE;
		const bool IsFngServer = IsOnline && GameClient()->m_GameInfo.m_PredictFNG;
		const bool Is0xFServer = IsOnline && str_comp_nocase(GameClient()->m_GameInfo.m_aGameType, "0xf") == 0;
		const bool IsBlockedCameraServer = IsFngServer || Is0xFServer;

		CUIRect LeftView, RightView;
		MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
		LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
		RightView.VSplitRight(MarginSmall, &RightView, nullptr);

		static std::vector<CUIRect> s_SectionBoxes;
		static vec2 s_PrevScrollOffset(0.0f, 0.0f);
		for(CUIRect &Section : s_SectionBoxes)
		{
			float Padding = MarginBetweenViews * 0.6666f;
			Section.w += Padding;
			Section.h += Padding;
			Section.x -= Padding * 0.5f;
			Section.y -= Padding * 0.5f;
			Section.y -= s_PrevScrollOffset.y - VisualsScrollOffset.y;
			Section.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);
		}
		s_PrevScrollOffset = VisualsScrollOffset;
		s_SectionBoxes.clear();

		auto BeginBlock = [&](CUIRect &ColumnRef, float ContentHeight, CUIRect &Content) {
			CUIRect Block;
			ColumnRef.HSplitTop(ContentHeight, &Block, &ColumnRef);
			s_SectionBoxes.push_back(Block);
			Content = Block;
		};

		CUIRect Column = LeftView;
		Column.HSplitTop(10.0f, nullptr, &Column);

		// Hook combo (left column block)
		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_GAMEPLAY_HOOK_COMBO))
		{
			static float s_HookComboPhase = 0.0f;
			static CButtonContainer s_HookComboResetButton;
			const bool HookComboExpanded = g_Config.m_BcHookCombo != 0;
			UpdateRevealPhase(s_HookComboPhase, HookComboExpanded);
			const float ExpandedTargetHeight = MarginSmall + LineSize * 5.0f;
			const float ExpandedHeight = ExpandedTargetHeight * s_HookComboPhase;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExpandedHeight;
			CUIRect Content, Label, Button, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			CUIRect TitleLabel, ResetButton, ResetHitbox;
			Label.VSplitRight(LineSize + 8.0f, &TitleLabel, &ResetButton);
			ResetHitbox = ResetButton;
			const bool HookComboResetClicked = Ui()->DoButton_FontIcon(&s_HookComboResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &ResetHitbox, BUTTONFLAG_LEFT);
			GameClient()->m_Tooltips.DoToolTip(&s_HookComboResetButton, &ResetHitbox, BCLocalize("Reset to defaults"));
			if(HookComboResetClicked)
			{
				g_Config.m_BcHookComboMode = DefaultConfig::BcHookComboMode;
				g_Config.m_BcHookComboResetTime = DefaultConfig::BcHookComboResetTime;
				g_Config.m_BcHookComboSoundVolume = DefaultConfig::BcHookComboSoundVolume;
				g_Config.m_BcHookComboSize = DefaultConfig::BcHookComboSize;
			}
			Ui()->DoLabel(&TitleLabel, BCLocalize("Hook combo"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcHookCombo, BCLocalize("Hook combo"), &g_Config.m_BcHookCombo, &Content, LineSize);
			if(!HookComboResetClicked && ExpandedHeight > 0.0f)
			{
				Content.HSplitTop(ExpandedHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExpandedTargetHeight};
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);

				CUIRect ModeLabel, ModeRow;
				Expand.HSplitTop(LineSize, &ModeLabel, &Expand);
				Ui()->DoLabel(&ModeLabel, BCLocalize("Mode"), 14.0f, TEXTALIGN_ML);

				Expand.HSplitTop(LineSize, &ModeRow, &Expand);
				CUIRect HookButton, HammerButton, HookHammerButton;
				ModeRow.VSplitLeft(ModeRow.w / 3.0f, &HookButton, &ModeRow);
				ModeRow.VSplitLeft(ModeRow.w / 2.0f, &HammerButton, &HookHammerButton);

				static CButtonContainer s_HookComboModeHook;
				static CButtonContainer s_HookComboModeHammer;
				static CButtonContainer s_HookComboModeHookHammer;
				if(DoButton_Menu(&s_HookComboModeHook, BCLocalize("hook"), g_Config.m_BcHookComboMode == 0, &HookButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
					g_Config.m_BcHookComboMode = 0;
				if(DoButton_Menu(&s_HookComboModeHammer, BCLocalize("hammer"), g_Config.m_BcHookComboMode == 1, &HammerButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
					g_Config.m_BcHookComboMode = 1;
				if(DoButton_Menu(&s_HookComboModeHookHammer, BCLocalize("hook&hammer"), g_Config.m_BcHookComboMode == 2, &HookHammerButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
					g_Config.m_BcHookComboMode = 2;

				Expand.HSplitTop(LineSize, &Button, &Expand);
				DoSliderWithScaledValue(&g_Config.m_BcHookComboResetTime, &g_Config.m_BcHookComboResetTime, &Button, BCLocalize("Max time between hooks"), 100, 5000, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");

				Expand.HSplitTop(LineSize, &Button, &Expand);
				DoSliderWithScaledValue(&g_Config.m_BcHookComboSoundVolume, &g_Config.m_BcHookComboSoundVolume, &Button, BCLocalize("Hook combo sound volume"), 0, 100, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "%");

				Expand.HSplitTop(LineSize, &Button, &Expand);
				DoSliderWithScaledValue(&g_Config.m_BcHookComboSize, &g_Config.m_BcHookComboSize, &Button, BCLocalize("Hook combo size"), 50, 200, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "%");
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		// Jelly tee (left column block)
		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_JELLY_TEE))
		{
			static float s_JellyTeePhase = 0.0f;
			static CButtonContainer s_JellyTeeResetButton;
			const bool JellyTeeEnabled = g_Config.m_BcJellyTee != 0;
			UpdateRevealPhase(s_JellyTeePhase, JellyTeeEnabled);
			const float ExtraTargetHeight = 3.0f * LineSize;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExtraTargetHeight * s_JellyTeePhase;
			CUIRect Content, Label, Row, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			CUIRect TitleLabel, ResetButton, ResetHitbox;
			Label.VSplitRight(LineSize + 8.0f, &TitleLabel, &ResetButton);
			ResetHitbox = ResetButton;
			const bool JellyTeeResetClicked = Ui()->DoButton_FontIcon(&s_JellyTeeResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &ResetHitbox, BUTTONFLAG_LEFT);
			GameClient()->m_Tooltips.DoToolTip(&s_JellyTeeResetButton, &ResetHitbox, BCLocalize("Reset to defaults"));
			if(JellyTeeResetClicked)
			{
				g_Config.m_BcJellyTeeOthers = DefaultConfig::BcJellyTeeOthers;
				g_Config.m_BcJellyTeeStrength = DefaultConfig::BcJellyTeeStrength;
				g_Config.m_BcJellyTeeDuration = DefaultConfig::BcJellyTeeDuration;
			}
			Ui()->DoLabel(&TitleLabel, BCLocalize("Jelly Tee"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcJellyTee, BCLocalize("Enable Jelly Tee"), &g_Config.m_BcJellyTee, &Content, LineSize);

			const float ExtraHeight = ExtraTargetHeight * s_JellyTeePhase;
			if(!JellyTeeResetClicked && ExtraHeight > 0.0f)
			{
				Content.HSplitTop(ExtraHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExtraTargetHeight};

				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcJellyTeeOthers, BCLocalize("Jelly Others"), &g_Config.m_BcJellyTeeOthers, &Expand, LineSize);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcJellyTeeStrength, &g_Config.m_BcJellyTeeStrength, &Row, BCLocalize("Jelly strength"), 0, 1000);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcJellyTeeDuration, &g_Config.m_BcJellyTeeDuration, &Row, BCLocalize("Jelly duration"), 1, 500);
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		// 3D particles (left column block)
		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_3D_PARTICLES))
		{
			const float ColorPickerLineSize = 25.0f;
			const float ColorPickerLabelSize = 13.0f;
			const float ColorPickerSpacing = 5.0f;
			static float s_Bc3dParticlesPhase = 0.0f;
			static float s_Bc3dParticlesGlowPhase = 0.0f;
			static CButtonContainer s_3DParticlesResetButton;
			const bool ParticlesEnabled = g_Config.m_Bc3dParticles != 0;
			UpdateRevealPhase(s_Bc3dParticlesPhase, ParticlesEnabled);
			const bool ShowCustomColor = ParticlesEnabled && g_Config.m_Bc3dParticlesColorMode == 1;
			const bool ShowGlowOptions = ParticlesEnabled && g_Config.m_Bc3dParticlesGlow != 0;
			if(BCUiAnimations::Enabled())
				BCUiAnimations::UpdatePhase(s_Bc3dParticlesGlowPhase, ShowGlowOptions ? 1.0f : 0.0f, Client()->RenderFrameTime(), 0.16f);
			else
				s_Bc3dParticlesGlowPhase = ShowGlowOptions ? 1.0f : 0.0f;
			const float GlowTargetHeight = 2.0f * LineSize;
			const float BaseTargetHeight = 7.0f * LineSize + (ShowCustomColor ? ColorPickerLineSize + ColorPickerSpacing : 0.0f);
			const float ExtraTargetHeight = BaseTargetHeight + GlowTargetHeight * s_Bc3dParticlesGlowPhase;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExtraTargetHeight * s_Bc3dParticlesPhase;
			CUIRect Content, Label, Row, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			CUIRect TitleLabel, ResetButton, ResetHitbox;
			Label.VSplitRight(LineSize + 8.0f, &TitleLabel, &ResetButton);
			ResetHitbox = ResetButton;
			const bool Particles3DResetClicked = Ui()->DoButton_FontIcon(&s_3DParticlesResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &ResetHitbox, BUTTONFLAG_LEFT);
			GameClient()->m_Tooltips.DoToolTip(&s_3DParticlesResetButton, &ResetHitbox, BCLocalize("Reset to defaults"));
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
			Ui()->DoLabel(&TitleLabel, BCLocalize("3D Particles"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_Bc3dParticles, BCLocalize("3D Particles"), &g_Config.m_Bc3dParticles, &Content, LineSize);

			const float ExpandedHeight = ExtraTargetHeight * s_Bc3dParticlesPhase;
			if(!Particles3DResetClicked && ExpandedHeight > 0.0f)
			{
				Content.HSplitTop(ExpandedHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExtraTargetHeight};

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesCount, &g_Config.m_Bc3dParticlesCount, &Row, BCLocalize("Particles count"), 1, 200);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				CUIRect TypeLabel, TypeSelect;
				Row.VSplitLeft(150.0f, &TypeLabel, &TypeSelect);
				Ui()->DoLabel(&TypeLabel, BCLocalize("Particle type"), 14.0f, TEXTALIGN_ML);

				static CUi::SDropDownState s_3DParticlesTypeState;
				static CScrollRegion s_3DParticlesTypeScrollRegion;
				s_3DParticlesTypeState.m_SelectionPopupContext.m_pScrollRegion = &s_3DParticlesTypeScrollRegion;
				const char *ap3DParticleTypes[3] = {
					BCLocalize("Cube"),
					BCLocalize("Heart"),
					BCLocalize("Mixed"),
				};
				g_Config.m_Bc3dParticlesType = Ui()->DoDropDown(&TypeSelect, g_Config.m_Bc3dParticlesType - 1, ap3DParticleTypes, (int)std::size(ap3DParticleTypes), s_3DParticlesTypeState) + 1;

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesSizeMax, &g_Config.m_Bc3dParticlesSizeMax, &Row, BCLocalize("Size"), 2, 200);
				g_Config.m_Bc3dParticlesSizeMin = std::max(2, g_Config.m_Bc3dParticlesSizeMax - 3);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesSpeed, &g_Config.m_Bc3dParticlesSpeed, &Row, BCLocalize("Speed"), 1, 500);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesAlpha, &g_Config.m_Bc3dParticlesAlpha, &Row, BCLocalize("Alpha"), 1, 100);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				CUIRect ColorModeLabel, ColorModeSelect;
				Row.VSplitLeft(150.0f, &ColorModeLabel, &ColorModeSelect);
				Ui()->DoLabel(&ColorModeLabel, BCLocalize("Color mode"), 14.0f, TEXTALIGN_ML);

				static CUi::SDropDownState s_3DParticlesColorModeState;
				static CScrollRegion s_3DParticlesColorModeScrollRegion;
				s_3DParticlesColorModeState.m_SelectionPopupContext.m_pScrollRegion = &s_3DParticlesColorModeScrollRegion;
				const char *ap3DParticleColorModes[2] = {
					BCLocalize("Custom"),
					BCLocalize("Random"),
				};
				g_Config.m_Bc3dParticlesColorMode = Ui()->DoDropDown(&ColorModeSelect, g_Config.m_Bc3dParticlesColorMode - 1, ap3DParticleColorModes, (int)std::size(ap3DParticleColorModes), s_3DParticlesColorModeState) + 1;

				if(g_Config.m_Bc3dParticlesColorMode == 1)
				{
					static CButtonContainer s_3DParticlesColorButton;
					DoLine_ColorPicker(&s_3DParticlesColorButton, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerSpacing, &Expand, BCLocalize("Color"), &g_Config.m_Bc3dParticlesColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
				}

				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_Bc3dParticlesGlow, BCLocalize("Glow"), &g_Config.m_Bc3dParticlesGlow, &Expand, LineSize);

				const float GlowHeight = GlowTargetHeight * s_Bc3dParticlesGlowPhase;
				if(GlowHeight > 0.0f)
				{
					CUIRect GlowVisible;
					Expand.HSplitTop(GlowHeight, &GlowVisible, &Expand);
					Ui()->ClipEnable(&GlowVisible);
					SScopedClip GlowClipGuard{Ui()};

					CUIRect GlowExpand = {GlowVisible.x, GlowVisible.y, GlowVisible.w, GlowTargetHeight};
					GlowExpand.HSplitTop(LineSize, &Row, &GlowExpand);
					Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesGlowAlpha, &g_Config.m_Bc3dParticlesGlowAlpha, &Row, BCLocalize("Glow alpha"), 1, 100);
					GlowExpand.HSplitTop(LineSize, &Row, &GlowExpand);
					Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesGlowOffset, &g_Config.m_Bc3dParticlesGlowOffset, &Row, BCLocalize("Glow offset"), 1, 20);
				}
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		// Orbit aura (left column block)
		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_ORBIT_AURA))
		{
			static float s_OrbitAuraPhase = 0.0f;
			static float s_OrbitAuraIdlePhase = 0.0f;
			static CButtonContainer s_OrbitAuraResetButton;
			const bool OrbitEnabled = g_Config.m_BcOrbitAura != 0;
			const bool OrbitIdleEnabled = OrbitEnabled && g_Config.m_BcOrbitAuraIdle != 0;
			const float Dt = Client()->RenderFrameTime();
			if(ModuleUiRevealAnimationsEnabled())
				BCUiAnimations::UpdatePhase(s_OrbitAuraPhase, OrbitEnabled ? 1.0f : 0.0f, Dt, ModuleUiRevealAnimationDuration());
			else
				s_OrbitAuraPhase = OrbitEnabled ? 1.0f : 0.0f;
			if(BCUiAnimations::Enabled())
				BCUiAnimations::UpdatePhase(s_OrbitAuraIdlePhase, OrbitIdleEnabled ? 1.0f : 0.0f, Dt, 0.16f);
			else
				s_OrbitAuraIdlePhase = OrbitIdleEnabled ? 1.0f : 0.0f;

			const float OrbitIdleTargetHeight = 1.0f * LineSize;
			const float OrbitBaseTargetHeight = 5.0f * LineSize;
			const float OrbitExtraTargetHeight = OrbitBaseTargetHeight + OrbitIdleTargetHeight * s_OrbitAuraIdlePhase;
			const float ContentHeight = LineSize + MarginSmall + LineSize + OrbitExtraTargetHeight * s_OrbitAuraPhase;
			CUIRect Content, Label, Row, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			CUIRect TitleLabel, ResetButton, ResetHitbox;
			Label.VSplitRight(LineSize + 8.0f, &TitleLabel, &ResetButton);
			ResetHitbox = ResetButton;
			const bool OrbitAuraResetClicked = Ui()->DoButton_FontIcon(&s_OrbitAuraResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &ResetHitbox, BUTTONFLAG_LEFT);
			GameClient()->m_Tooltips.DoToolTip(&s_OrbitAuraResetButton, &ResetHitbox, BCLocalize("Reset to defaults"));
			if(OrbitAuraResetClicked)
			{
				g_Config.m_BcOrbitAuraRadius = DefaultConfig::BcOrbitAuraRadius;
				g_Config.m_BcOrbitAuraParticles = DefaultConfig::BcOrbitAuraParticles;
				g_Config.m_BcOrbitAuraAlpha = DefaultConfig::BcOrbitAuraAlpha;
				g_Config.m_BcOrbitAuraSpeed = DefaultConfig::BcOrbitAuraSpeed;
				g_Config.m_BcOrbitAuraIdle = DefaultConfig::BcOrbitAuraIdle;
				g_Config.m_BcOrbitAuraIdleTimer = DefaultConfig::BcOrbitAuraIdleTimer;
			}
			Ui()->DoLabel(&TitleLabel, BCLocalize("Orbit Aura"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOrbitAura, BCLocalize("Orbit Aura"), &g_Config.m_BcOrbitAura, &Content, LineSize);

			const float OrbitExtraHeight = OrbitExtraTargetHeight * s_OrbitAuraPhase;
			if(!OrbitAuraResetClicked && OrbitExtraHeight > 0.0f)
			{
				Content.HSplitTop(OrbitExtraHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, OrbitExtraTargetHeight};

				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOrbitAuraIdle, BCLocalize("Enable in idle mode"), &g_Config.m_BcOrbitAuraIdle, &Expand, LineSize);

				const float OrbitIdleHeight = OrbitIdleTargetHeight * s_OrbitAuraIdlePhase;
				if(OrbitIdleHeight > 0.0f)
				{
					CUIRect IdleVisible;
					Expand.HSplitTop(OrbitIdleHeight, &IdleVisible, &Expand);
					Ui()->ClipEnable(&IdleVisible);
					SScopedClip IdleClipGuard{Ui()};

					CUIRect IdleExpand = {IdleVisible.x, IdleVisible.y, IdleVisible.w, OrbitIdleTargetHeight};
					IdleExpand.HSplitTop(LineSize, &Row, &IdleExpand);
					Ui()->DoScrollbarOption(&g_Config.m_BcOrbitAuraIdleTimer, &g_Config.m_BcOrbitAuraIdleTimer, &Row, BCLocalize("Idle delay"), 1, 30);
				}

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcOrbitAuraRadius, &g_Config.m_BcOrbitAuraRadius, &Row, BCLocalize("Aura radius"), 8, 200);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcOrbitAuraParticles, &g_Config.m_BcOrbitAuraParticles, &Row, BCLocalize("Particles"), 2, 120);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcOrbitAuraAlpha, &g_Config.m_BcOrbitAuraAlpha, &Row, BCLocalize("Aura alpha"), 0, 100);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcOrbitAuraSpeed, &g_Config.m_BcOrbitAuraSpeed, &Row, BCLocalize("Aura speed"), 10, 200);
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		// Media background (left column block)
		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_MEDIA_BACKGROUND))
		{
			const float ContentHeight = LineSize + MarginSmall + 5.0f * LineSize + MarginSmall;
			CUIRect Content, Label, Row;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("Media Background"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			const bool MenuMediaChanged = DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMenuMediaBackground, BCLocalize("Enable to main menu"), &g_Config.m_BcMenuMediaBackground, &Content, LineSize);
			const bool GameMediaChanged = DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcGameMediaBackground, BCLocalize("Enable to game background"), &g_Config.m_BcGameMediaBackground, &Content, LineSize);
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

			CUIRect MediaPathRow, MediaFileDropDown, MediaReloadButton, MediaFolderButton;
			Content.HSplitTop(MarginSmall, nullptr, &Content);
			Content.HSplitTop(LineSize, &MediaPathRow, &Content);
			MediaPathRow.VSplitRight(20.0f, &MediaPathRow, &MediaFolderButton);
			MediaPathRow.VSplitRight(MarginSmall, &MediaPathRow, nullptr);
			MediaPathRow.VSplitRight(20.0f, &MediaPathRow, &MediaReloadButton);
			MediaPathRow.VSplitRight(MarginSmall, &MediaPathRow, nullptr);
			MediaFileDropDown = MediaPathRow;

			if(s_vMenuMediaDropDownLabelPtrs.empty())
			{
				static CButtonContainer s_MenuMediaEmptyButton;
				DoButton_Menu(&s_MenuMediaEmptyButton, BCLocalize("No media files in backgrounds folder"), -1, &MediaFileDropDown);
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

			Content.HSplitTop(LineSize, &Row, &Content);
			Ui()->DoScrollbarOption(&g_Config.m_BcGameMediaBackgroundOffset, &g_Config.m_BcGameMediaBackgroundOffset, &Row, BCLocalize("Map offset"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");
			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_BcGameMediaBackgroundOffset, &Row, BCLocalize("0 keeps the image fixed to the screen. 100 fixes it to the map for a full parallax effect."));

			Content.HSplitTop(LineSize, &Row, &Content);
			if(m_MenuMediaBackground.HasError())
				TextRender()->TextColor(ColorRGBA(1.0f, 0.45f, 0.45f, 1.0f));
			else if(m_MenuMediaBackground.IsLoaded())
				TextRender()->TextColor(ColorRGBA(0.55f, 1.0f, 0.55f, 1.0f));
			Ui()->DoLabel(&Row, m_MenuMediaBackground.StatusText(), 11.0f, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		// Sweat Weapon (left column block)
		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_CRYSTAL_LASER))
		{
			const float ContentHeight = LineSize + MarginSmall + LineSize + MarginSmall + LineSize + 58.0f + MarginSmall + LineSize + 58.0f;
			CUIRect Content, Label, PreviewLabel, PreviewRect;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("Sweat Weapon"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcCrystalLaser, BCLocalize("Enable"), &g_Config.m_BcCrystalLaser, &Content, LineSize);

			Content.HSplitTop(MarginSmall, nullptr, &Content);
			Content.HSplitTop(LineSize, &PreviewLabel, &Content);
			Ui()->DoLabel(&PreviewLabel, BCLocalize("Crystal Laser"), 14.0f, TEXTALIGN_ML);
			Content.HSplitTop(58.0f, &PreviewRect, &Content);
			DoLaserPreview(&PreviewRect, ColorHSLA(g_Config.m_ClLaserRifleOutlineColor), ColorHSLA(g_Config.m_ClLaserRifleInnerColor), LASERTYPE_RIFLE);

			Content.HSplitTop(MarginSmall, nullptr, &Content);
			Content.HSplitTop(LineSize, &PreviewLabel, &Content);
			Ui()->DoLabel(&PreviewLabel, BCLocalize("Sand Shotgun"), 14.0f, TEXTALIGN_ML);
			Content.HSplitTop(58.0f, &PreviewRect, &Content);
			DoLaserPreview(&PreviewRect, ColorHSLA(g_Config.m_ClLaserShotgunOutlineColor), ColorHSLA(g_Config.m_ClLaserShotgunInnerColor), LASERTYPE_SHOTGUN);
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_CHAT_BUBBLES))
		{
			static float s_BcChatBubblesPhase = 0.0f;
			static CButtonContainer s_ChatBubblesResetButton;
			const bool ChatBubblesEnabled = g_Config.m_BcChatBubbles != 0;
			UpdateRevealPhase(s_BcChatBubblesPhase, ChatBubblesEnabled);
			const float ExtraTargetHeight = MarginSmall + 6.0f * LineSize;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExtraTargetHeight * s_BcChatBubblesPhase;
			CUIRect Content, Label, Row, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			CUIRect TitleLabel, ResetButton, ResetHitbox;
			Label.VSplitRight(LineSize + 8.0f, &TitleLabel, &ResetButton);
			ResetHitbox = ResetButton;
			const bool ChatBubblesResetClicked = Ui()->DoButton_FontIcon(&s_ChatBubblesResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &ResetHitbox, BUTTONFLAG_LEFT);
			GameClient()->m_Tooltips.DoToolTip(&s_ChatBubblesResetButton, &ResetHitbox, BCLocalize("Reset to defaults"));
			if(ChatBubblesResetClicked)
			{
				g_Config.m_BcChatBubblesDemo = DefaultConfig::BcChatBubblesDemo;
				g_Config.m_BcChatBubblesSelf = DefaultConfig::BcChatBubblesSelf;
				g_Config.m_BcChatBubbleSize = DefaultConfig::BcChatBubbleSize;
				g_Config.m_BcChatBubbleShowTime = DefaultConfig::BcChatBubbleShowTime;
				g_Config.m_BcChatBubbleFadeIn = DefaultConfig::BcChatBubbleFadeIn;
				g_Config.m_BcChatBubbleFadeOut = DefaultConfig::BcChatBubbleFadeOut;
			}
			Ui()->DoLabel(&TitleLabel, BCLocalize("Chat Bubbles"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatBubbles, BCLocalize("Chat Bubbles"), &g_Config.m_BcChatBubbles, &Content, LineSize);
			const float ExtraHeight = ExtraTargetHeight * s_BcChatBubblesPhase;
			if(!ChatBubblesResetClicked && ExtraHeight > 0.0f)
			{
				Content.HSplitTop(ExtraHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExtraTargetHeight};

				Expand.HSplitTop(MarginSmall, nullptr, &Expand);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatBubblesDemo, BCLocalize("Show Chatbubbles in demo"), &g_Config.m_BcChatBubblesDemo, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatBubblesSelf, BCLocalize("Show Chatbubbles above you"), &g_Config.m_BcChatBubblesSelf, &Expand, LineSize);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcChatBubbleSize, &g_Config.m_BcChatBubbleSize, &Row, BCLocalize("Chat Bubble Size"), 20, 30);
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcChatBubbleShowTime, &g_Config.m_BcChatBubbleShowTime, &Row, BCLocalize("Show the Bubbles for"), 200, 1000);
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcChatBubbleFadeIn, &g_Config.m_BcChatBubbleFadeIn, &Row, BCLocalize("fade in for"), 15, 100);
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcChatBubbleFadeOut, &g_Config.m_BcChatBubbleFadeOut, &Row, BCLocalize("fade out for"), 15, 100);
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		const float LeftColumnEndY = Column.y;
		Column = RightView;
		Column.HSplitTop(10.0f, nullptr, &Column);

		// Animations (right column block)
		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_ANIMATIONS))
		{
			static float s_AnimationsBlockPhase = 0.0f;
			static CButtonContainer s_AnimationsResetButton;
			const bool AnimationsEnabled = g_Config.m_BcAnimations != 0;
			const float Dt = Client()->RenderFrameTime();
			const bool AnimateBlock = g_Config.m_BcModuleUiRevealAnimation != 0;
			if(AnimateBlock)
				BCUiAnimations::UpdatePhase(s_AnimationsBlockPhase, AnimationsEnabled ? 1.0f : 0.0f, Dt, ModuleUiRevealAnimationDuration());
			else
				s_AnimationsBlockPhase = AnimationsEnabled ? 1.0f : 0.0f;

			const float ExpandedTargetHeight = 12.0f * LineSize;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExpandedTargetHeight * s_AnimationsBlockPhase;
			CUIRect Content, Label, Row, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			CUIRect TitleLabel, ResetButton, ResetHitbox;
			Label.VSplitRight(LineSize + 8.0f, &TitleLabel, &ResetButton);
			ResetHitbox = ResetButton;
			const bool AnimationsResetClicked = Ui()->DoButton_FontIcon(&s_AnimationsResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &ResetHitbox, BUTTONFLAG_LEFT);
			GameClient()->m_Tooltips.DoToolTip(&s_AnimationsResetButton, &ResetHitbox, BCLocalize("Reset to defaults"));
			if(AnimationsResetClicked)
			{
				g_Config.m_BcAnimations = DefaultConfig::BcAnimations;
				g_Config.m_BcModuleUiRevealAnimation = DefaultConfig::BcModuleUiRevealAnimation;
				g_Config.m_BcModuleUiRevealAnimationMs = DefaultConfig::BcModuleUiRevealAnimationMs;
				g_Config.m_BcIngameMenuAnimation = DefaultConfig::BcIngameMenuAnimation;
				g_Config.m_BcIngameMenuAnimationMs = DefaultConfig::BcIngameMenuAnimationMs;
				g_Config.m_BcChatAnimation = DefaultConfig::BcChatAnimation;
				g_Config.m_BcChatAnimationMs = DefaultConfig::BcChatAnimationMs;
				g_Config.m_BcChatOpenAnimation = DefaultConfig::BcChatOpenAnimation;
				g_Config.m_BcChatOpenAnimationMs = DefaultConfig::BcChatOpenAnimationMs;
				g_Config.m_BcChatTypingAnimation = DefaultConfig::BcChatTypingAnimation;
				g_Config.m_BcChatTypingAnimationMs = DefaultConfig::BcChatTypingAnimationMs;
				g_Config.m_BcKillfeedAnimation = DefaultConfig::BcKillfeedAnimation;
				g_Config.m_BcKillfeedAnimationMs = DefaultConfig::BcKillfeedAnimationMs;
				g_Config.m_BcChatAnimationType = DefaultConfig::BcChatAnimationType;
			}
			Ui()->DoLabel(&TitleLabel, BCLocalize("Animations"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcAnimations, BCLocalize("Enable animations"), &g_Config.m_BcAnimations, &Content, LineSize);

			const float ExpandedHeight = ExpandedTargetHeight * s_AnimationsBlockPhase;
			if(!AnimationsResetClicked && ExpandedHeight > 0.0f)
			{
				Content.HSplitTop(ExpandedHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExpandedTargetHeight};
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcModuleUiRevealAnimation, BCLocalize("Module settings reveals"), &g_Config.m_BcModuleUiRevealAnimation, &Expand, LineSize);
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcModuleUiRevealAnimationMs, &g_Config.m_BcModuleUiRevealAnimationMs, &Row, BCLocalize("Module reveal time (ms)"), 1, 500);

				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcIngameMenuAnimation, BCLocalize("ESC menu animation"), &g_Config.m_BcIngameMenuAnimation, &Expand, LineSize);
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcIngameMenuAnimationMs, &g_Config.m_BcIngameMenuAnimationMs, &Row, BCLocalize("ESC menu animation time (ms)"), 1, 500);

				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatAnimation, BCLocalize("Chat message animations"), &g_Config.m_BcChatAnimation, &Expand, LineSize);
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcChatAnimationMs, &g_Config.m_BcChatAnimationMs, &Row, BCLocalize("Chat message animation time (ms)"), 1, 500);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatOpenAnimation, BCLocalize("Chat open animation"), &g_Config.m_BcChatOpenAnimation, &Expand, LineSize);
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcChatOpenAnimationMs, &g_Config.m_BcChatOpenAnimationMs, &Row, BCLocalize("Chat open animation time (ms)"), 1, 500);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatTypingAnimation, BCLocalize("Chat typing animation"), &g_Config.m_BcChatTypingAnimation, &Expand, LineSize);
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcChatTypingAnimationMs, &g_Config.m_BcChatTypingAnimationMs, &Row, BCLocalize("Chat typing animation time (ms)"), 1, 500);

				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcKillfeedAnimation, BCLocalize("Killfeed animation"), &g_Config.m_BcKillfeedAnimation, &Expand, LineSize);
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcKillfeedAnimationMs, &g_Config.m_BcKillfeedAnimationMs, &Row, BCLocalize("Killfeed animation time (ms)"), 1, 500);
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		// Music player (right column block)
		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_MUSIC_PLAYER))
		{
			const float ColorPickerLineSize = 25.0f;
			const float ColorPickerLabelSize = 13.0f;
			const float ColorPickerSpacing = 5.0f;
			static float s_MusicPlayerPhase = 0.0f;
			static float s_MusicPlayerStaticColorPhase = 0.0f;
			static float s_MusicPlayerVisualizerPhase = 0.0f;
			const bool MusicPlayerEnabled = g_Config.m_BcMusicPlayer != 0;
			const bool StaticColorOn = MusicPlayerEnabled && g_Config.m_BcMusicPlayerColorMode == 0;
			const bool VisualizerOn = MusicPlayerEnabled && g_Config.m_BcMusicPlayerVisualizer != 0;
			UpdateRevealPhase(s_MusicPlayerPhase, MusicPlayerEnabled);
			if(BCUiAnimations::Enabled())
			{
				BCUiAnimations::UpdatePhase(s_MusicPlayerStaticColorPhase, StaticColorOn ? 1.0f : 0.0f, Client()->RenderFrameTime(), 0.16f);
				BCUiAnimations::UpdatePhase(s_MusicPlayerVisualizerPhase, VisualizerOn ? 1.0f : 0.0f, Client()->RenderFrameTime(), 0.16f);
			}
			else
			{
				s_MusicPlayerStaticColorPhase = StaticColorOn ? 1.0f : 0.0f;
				s_MusicPlayerVisualizerPhase = VisualizerOn ? 1.0f : 0.0f;
			}

			const float StaticColorTargetHeight = ColorPickerLineSize + ColorPickerSpacing;
			const float VisualizerSliderHeight = LineSize + 4.0f;
			const float VisualizerTargetHeight = LineSize + VisualizerSliderHeight * 2.0f;
			const float ExtraTargetHeight = LineSize * 2.0f + VisualizerTargetHeight * s_MusicPlayerVisualizerPhase + StaticColorTargetHeight * s_MusicPlayerStaticColorPhase;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExtraTargetHeight * s_MusicPlayerPhase;
			CUIRect Content, Label, Row, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("Music Player"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMusicPlayer, BCLocalize("Enable music player"), &g_Config.m_BcMusicPlayer, &Content, LineSize);

			const float ExpandedHeight = ExtraTargetHeight * s_MusicPlayerPhase;
			if(ExpandedHeight > 0.0f)
			{
				Content.HSplitTop(ExpandedHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExtraTargetHeight};

				CUIRect ModeLabel, ModeDropDown;
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Row.VSplitLeft(120.0f, &ModeLabel, &ModeDropDown);
				Ui()->DoLabel(&ModeLabel, BCLocalize("Color mode"), 14.0f, TEXTALIGN_ML);

				static CUi::SDropDownState s_MusicPlayerColorModeState;
				static CScrollRegion s_MusicPlayerColorModeScrollRegion;
				s_MusicPlayerColorModeState.m_SelectionPopupContext.m_pScrollRegion = &s_MusicPlayerColorModeScrollRegion;
				const char *apMusicPlayerColorModes[4] = {
					BCLocalize("Static color"),
					BCLocalize("Cover accent color"),
					BCLocalize("Dominant cover color"),
					BCLocalize("Translucent"),
				};
				g_Config.m_BcMusicPlayerColorMode = std::clamp(g_Config.m_BcMusicPlayerColorMode, 0, 3);
				g_Config.m_BcMusicPlayerColorMode = Ui()->DoDropDown(&ModeDropDown, g_Config.m_BcMusicPlayerColorMode, apMusicPlayerColorModes, (int)std::size(apMusicPlayerColorModes), s_MusicPlayerColorModeState);

				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMusicPlayerVisualizer, BCLocalize("Enable visualizer"), &g_Config.m_BcMusicPlayerVisualizer, &Expand, LineSize);

				const float VisualizerHeight = VisualizerTargetHeight * s_MusicPlayerVisualizerPhase;
				if(VisualizerHeight > 0.0f)
				{
					CUIRect VisualizerVisible;
					Expand.HSplitTop(VisualizerHeight, &VisualizerVisible, &Expand);
					Ui()->ClipEnable(&VisualizerVisible);
					SScopedClip VisualizerClipGuard{Ui()};

					CUIRect VisualizerExpand = {VisualizerVisible.x, VisualizerVisible.y, VisualizerVisible.w, VisualizerTargetHeight};
					VisualizerExpand.HSplitTop(LineSize, &Row, &VisualizerExpand);
					Row.VSplitLeft(120.0f, &ModeLabel, &ModeDropDown);
					Ui()->DoLabel(&ModeLabel, BCLocalize("Visualizer mode"), 14.0f, TEXTALIGN_ML);

					static CUi::SDropDownState s_MusicPlayerVisualizerModeState;
					static CScrollRegion s_MusicPlayerVisualizerModeScrollRegion;
					s_MusicPlayerVisualizerModeState.m_SelectionPopupContext.m_pScrollRegion = &s_MusicPlayerVisualizerModeScrollRegion;
					const char *apMusicPlayerVisualizerModes[2] = {
						BCLocalize("Bottom"),
						BCLocalize("Center"),
					};
					g_Config.m_BcMusicPlayerVisualizerMode = std::clamp(g_Config.m_BcMusicPlayerVisualizerMode, 0, 1);
					g_Config.m_BcMusicPlayerVisualizerMode = Ui()->DoDropDown(&ModeDropDown, g_Config.m_BcMusicPlayerVisualizerMode, apMusicPlayerVisualizerModes, (int)std::size(apMusicPlayerVisualizerModes), s_MusicPlayerVisualizerModeState);

					CUIRect SliderRow, SliderLabel, SliderButton;
					VisualizerExpand.HSplitTop(VisualizerSliderHeight, &SliderRow, &VisualizerExpand);
					SliderRow.VSplitLeft(120.0f, &SliderLabel, &SliderButton);
					Ui()->DoLabel(&SliderLabel, BCLocalize("Visualizer sensitivity"), 14.0f, TEXTALIGN_ML);
					Ui()->DoScrollbarOption(&g_Config.m_BcMusicPlayerVisualizerSensitivity, &g_Config.m_BcMusicPlayerVisualizerSensitivity, &SliderButton, "", 50, 300, &CUi::ms_LinearScrollbarScale, 0u, "%");

					VisualizerExpand.HSplitTop(VisualizerSliderHeight, &SliderRow, &VisualizerExpand);
					SliderRow.VSplitLeft(120.0f, &SliderLabel, &SliderButton);
					Ui()->DoLabel(&SliderLabel, BCLocalize("Visualizer smoothing"), 14.0f, TEXTALIGN_ML);
					Ui()->DoScrollbarOption(&g_Config.m_BcMusicPlayerVisualizerSmoothing, &g_Config.m_BcMusicPlayerVisualizerSmoothing, &SliderButton, "", 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");
				}

				const float StaticColorHeight = StaticColorTargetHeight * s_MusicPlayerStaticColorPhase;
				if(StaticColorHeight > 0.0f)
				{
					CUIRect StaticVisible;
					Expand.HSplitTop(StaticColorHeight, &StaticVisible, &Expand);
					Ui()->ClipEnable(&StaticVisible);
					SScopedClip StaticClipGuard{Ui()};

					CUIRect StaticExpand = {StaticVisible.x, StaticVisible.y, StaticVisible.w, StaticColorTargetHeight};
					static CButtonContainer s_MusicPlayerStaticColorButton;
					DoLine_ColorPicker(&s_MusicPlayerStaticColorButton, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerSpacing, &StaticExpand, BCLocalize("Static color"), &g_Config.m_BcMusicPlayerStaticColor, ColorRGBA(0.34f, 0.53f, 0.79f, 1.0f), false);
				}
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		// Camera Drift (right column block)
		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_CAMERA_DRIFT))
		{
			static float s_CameraDriftPhase = 0.0f;
			static CButtonContainer s_CameraDriftResetButton;
			const bool CameraDriftEnabled = g_Config.m_BcCameraDrift != 0;
			UpdateRevealPhase(s_CameraDriftPhase, CameraDriftEnabled);
			const float ExtraTargetHeight = 3.0f * LineSize;
			const float BlockedHintHeight = IsBlockedCameraServer ? (MarginSmall + LineSize) : 0.0f;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExtraTargetHeight * s_CameraDriftPhase + BlockedHintHeight;
			CUIRect Content, Label, Row, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			CUIRect TitleLabel, ResetButton, ResetHitbox;
			Label.VSplitRight(LineSize + 8.0f, &TitleLabel, &ResetButton);
			ResetHitbox = ResetButton;
			const bool CameraDriftResetClicked = Ui()->DoButton_FontIcon(&s_CameraDriftResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &ResetHitbox, BUTTONFLAG_LEFT);
			GameClient()->m_Tooltips.DoToolTip(&s_CameraDriftResetButton, &ResetHitbox, BCLocalize("Reset to defaults"));
			if(CameraDriftResetClicked)
			{
				g_Config.m_BcCameraDriftAmount = DefaultConfig::BcCameraDriftAmount;
				g_Config.m_BcCameraDriftSmoothness = DefaultConfig::BcCameraDriftSmoothness;
				g_Config.m_BcCameraDriftReverse = DefaultConfig::BcCameraDriftReverse;
			}
			Ui()->DoLabel(&TitleLabel, BCLocalize("Camera Drift"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcCameraDrift, BCLocalize("Camera Drift"), &g_Config.m_BcCameraDrift, &Content, LineSize);

			const float ExtraHeight = ExtraTargetHeight * s_CameraDriftPhase;
			if(!CameraDriftResetClicked && ExtraHeight > 0.0f)
			{
				Content.HSplitTop(ExtraHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExtraTargetHeight};

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcCameraDriftAmount, &g_Config.m_BcCameraDriftAmount, &Row, BCLocalize("Camera drift amount"), 1, 200);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcCameraDriftSmoothness, &g_Config.m_BcCameraDriftSmoothness, &Row, BCLocalize("Camera drift smoothness"), 1, 20);

				CUIRect DirectionLabel, DirectionButtons, DirectionForward, DirectionBackward;
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Row.VSplitLeft(150.0f, &DirectionLabel, &DirectionButtons);
				Ui()->DoLabel(&DirectionLabel, BCLocalize("Drift direction"), 14.0f, TEXTALIGN_ML);
				DirectionButtons.VSplitMid(&DirectionForward, &DirectionBackward, MarginSmall);

				static int s_CameraDriftForwardButton = 0;
				static int s_CameraDriftBackwardButton = 0;
				if(DoButton_CheckBox(&s_CameraDriftForwardButton, BCLocalize("Forward"), !g_Config.m_BcCameraDriftReverse, &DirectionForward))
					g_Config.m_BcCameraDriftReverse = 0;
				if(DoButton_CheckBox(&s_CameraDriftBackwardButton, BCLocalize("Backward"), g_Config.m_BcCameraDriftReverse, &DirectionBackward))
					g_Config.m_BcCameraDriftReverse = 1;
			}
			if(IsBlockedCameraServer)
			{
				Content.HSplitTop(MarginSmall, nullptr, &Content);
				Content.HSplitTop(LineSize, &Label, &Content);
				TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
				Ui()->DoLabel(&Label, BCLocalize("Looks like you're on a server where this feature is forbidden"), 14.0f, TEXTALIGN_ML);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		// Dynamic FOV (right column block)
		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_DYNAMIC_FOV))
		{
			static float s_DynamicFovPhase = 0.0f;
			static CButtonContainer s_DynamicFovResetButton;
			const bool DynamicFovEnabled = g_Config.m_BcDynamicFov != 0;
			UpdateRevealPhase(s_DynamicFovPhase, DynamicFovEnabled);
			const float ExtraTargetHeight = 2.0f * LineSize;
			const float BlockedHintHeight = IsBlockedCameraServer ? (MarginSmall + LineSize) : 0.0f;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExtraTargetHeight * s_DynamicFovPhase + BlockedHintHeight;
			CUIRect Content, Label, Row, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			CUIRect TitleLabel, ResetButton, ResetHitbox;
			Label.VSplitRight(LineSize + 8.0f, &TitleLabel, &ResetButton);
			ResetHitbox = ResetButton;
			const bool DynamicFovResetClicked = Ui()->DoButton_FontIcon(&s_DynamicFovResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &ResetHitbox, BUTTONFLAG_LEFT);
			GameClient()->m_Tooltips.DoToolTip(&s_DynamicFovResetButton, &ResetHitbox, BCLocalize("Reset to defaults"));
			if(DynamicFovResetClicked)
			{
				g_Config.m_BcDynamicFovAmount = DefaultConfig::BcDynamicFovAmount;
				g_Config.m_BcDynamicFovSmoothness = DefaultConfig::BcDynamicFovSmoothness;
			}
			Ui()->DoLabel(&TitleLabel, BCLocalize("Dynamic FOV"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcDynamicFov, BCLocalize("Dynamic FOV"), &g_Config.m_BcDynamicFov, &Content, LineSize);

			const float ExtraHeight = ExtraTargetHeight * s_DynamicFovPhase;
			if(!DynamicFovResetClicked && ExtraHeight > 0.0f)
			{
				Content.HSplitTop(ExtraHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExtraTargetHeight};

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcDynamicFovAmount, &g_Config.m_BcDynamicFovAmount, &Row, BCLocalize("Dynamic FOV amount"), 1, 200);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcDynamicFovSmoothness, &g_Config.m_BcDynamicFovSmoothness, &Row, BCLocalize("Dynamic FOV smoothness"), 1, 20);
			}
			if(IsBlockedCameraServer)
			{
				Content.HSplitTop(MarginSmall, nullptr, &Content);
				Content.HSplitTop(LineSize, &Label, &Content);
				TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
				Ui()->DoLabel(&Label, BCLocalize("Looks like you're on a server where this feature is forbidden"), 14.0f, TEXTALIGN_ML);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		// Afterimage (right column block)
		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_AFTERIMAGE))
		{
			static float s_AfterimagePhase = 0.0f;
			static CButtonContainer s_AfterimageResetButton;
			const bool AfterimageEnabled = g_Config.m_BcAfterimage != 0;
			UpdateRevealPhase(s_AfterimagePhase, AfterimageEnabled);
			const float ExtraTargetHeight = 3.0f * LineSize;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExtraTargetHeight * s_AfterimagePhase;
			CUIRect Content, Label, Row, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			CUIRect TitleLabel, ResetButton, ResetHitbox;
			Label.VSplitRight(LineSize + 8.0f, &TitleLabel, &ResetButton);
			ResetHitbox = ResetButton;
			const bool AfterimageResetClicked = Ui()->DoButton_FontIcon(&s_AfterimageResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &ResetHitbox, BUTTONFLAG_LEFT);
			GameClient()->m_Tooltips.DoToolTip(&s_AfterimageResetButton, &ResetHitbox, BCLocalize("Reset to defaults"));
			if(AfterimageResetClicked)
			{
				g_Config.m_BcAfterimageFrames = DefaultConfig::BcAfterimageFrames;
				g_Config.m_BcAfterimageAlpha = DefaultConfig::BcAfterimageAlpha;
				g_Config.m_BcAfterimageSpacing = DefaultConfig::BcAfterimageSpacing;
			}
			Ui()->DoLabel(&TitleLabel, BCLocalize("Afterimage"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcAfterimage, BCLocalize("Enable Afterimage"), &g_Config.m_BcAfterimage, &Content, LineSize);

			const float ExtraHeight = ExtraTargetHeight * s_AfterimagePhase;
			if(!AfterimageResetClicked && ExtraHeight > 0.0f)
			{
				Content.HSplitTop(ExtraHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExtraTargetHeight};

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcAfterimageFrames, &g_Config.m_BcAfterimageFrames, &Row, BCLocalize("Afterimage frames"), 2, 20);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcAfterimageAlpha, &g_Config.m_BcAfterimageAlpha, &Row, BCLocalize("Afterimage alpha"), 1, 100);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcAfterimageSpacing, &g_Config.m_BcAfterimageSpacing, &Row, BCLocalize("Afterimage spacing"), 1, 64);
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		// Magic particles (right column block)
		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_MAGIC_PARTICLES))
		{
			static float s_MagicParticlesPhase = 0.0f;
			static CButtonContainer s_MagicParticlesResetButton;
			const bool MagicParticlesEnabled = g_Config.m_BcMagicParticles != 0;
			UpdateRevealPhase(s_MagicParticlesPhase, MagicParticlesEnabled);
			const float ExpandedTargetHeight = 5.0f * LineSize;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExpandedTargetHeight * s_MagicParticlesPhase;
			CUIRect Content, Label, Row, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			CUIRect TitleLabel, ResetButton, ResetHitbox;
			Label.VSplitRight(LineSize + 8.0f, &TitleLabel, &ResetButton);
			ResetHitbox = ResetButton;
			const bool MagicParticlesResetClicked = Ui()->DoButton_FontIcon(&s_MagicParticlesResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &ResetHitbox, BUTTONFLAG_LEFT);
			GameClient()->m_Tooltips.DoToolTip(&s_MagicParticlesResetButton, &ResetHitbox, BCLocalize("Reset to defaults"));
			if(MagicParticlesResetClicked)
			{
				g_Config.m_BcMagicParticlesCount = DefaultConfig::BcMagicParticlesCount;
				g_Config.m_BcMagicParticlesRadius = DefaultConfig::BcMagicParticlesRadius;
				g_Config.m_BcMagicParticlesSize = DefaultConfig::BcMagicParticlesSize;
				g_Config.m_BcMagicParticlesAlphaDelay = DefaultConfig::BcMagicParticlesAlphaDelay;
				g_Config.m_BcMagicParticlesType = DefaultConfig::BcMagicParticlesType;
			}
			Ui()->DoLabel(&TitleLabel, BCLocalize("Magic Particles"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMagicParticles, BCLocalize("Magic Particles"), &g_Config.m_BcMagicParticles, &Content, LineSize);

			const float ExpandedHeight = ExpandedTargetHeight * s_MagicParticlesPhase;
			if(!MagicParticlesResetClicked && ExpandedHeight > 0.0f)
			{
				Content.HSplitTop(ExpandedHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExpandedTargetHeight};

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcMagicParticlesCount, &g_Config.m_BcMagicParticlesCount, &Row, BCLocalize("Particles count"), 1, 100);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcMagicParticlesRadius, &g_Config.m_BcMagicParticlesRadius, &Row, BCLocalize("Radius"), 1, 1000);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcMagicParticlesSize, &g_Config.m_BcMagicParticlesSize, &Row, BCLocalize("Size"), 1, 50);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcMagicParticlesAlphaDelay, &g_Config.m_BcMagicParticlesAlphaDelay, &Row, BCLocalize("Alpha delay"), 1, 100);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				CUIRect TypeLabel, TypeSelect;
				Row.VSplitLeft(150.0f, &TypeLabel, &TypeSelect);
				Ui()->DoLabel(&TypeLabel, BCLocalize("Particle type"), 14.0f, TEXTALIGN_ML);

				static CUi::SDropDownState s_MagicParticlesTypeState;
				static CScrollRegion s_MagicParticlesTypeScrollRegion;
				s_MagicParticlesTypeState.m_SelectionPopupContext.m_pScrollRegion = &s_MagicParticlesTypeScrollRegion;
				const char *apMagicParticleTypes[4] = {
					BCLocalize("Slice"),
					BCLocalize("Ball"),
					BCLocalize("Smoke"),
					BCLocalize("Shell"),
				};
				g_Config.m_BcMagicParticlesType = Ui()->DoDropDown(&TypeSelect, g_Config.m_BcMagicParticlesType - 1, apMagicParticleTypes, (int)std::size(apMagicParticleTypes), s_MagicParticlesTypeState) + 1;
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		// Aspect ratio (right column block)
		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_ASPECT_RATIO))
		{
			const int AspectMode = g_Config.m_BcCustomAspectRatioMode >= 0 ? g_Config.m_BcCustomAspectRatioMode : (g_Config.m_BcCustomAspectRatio > 0 ? 1 : 0);
			const bool IsCustomMode = AspectMode == 2;
			const float ContentHeight = LineSize + MarginSmall + LineSize + MarginSmall + LineSize + (IsCustomMode ? (MarginSmall + LineSize + MarginSmall + LineSize) : 0.0f);
			CUIRect Content, Label, Row;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("Aspect Ratio"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			const auto SplitRowLabelControl = [&](CUIRect &InRow, CUIRect &OutLabel, CUIRect &OutControl) {
				const float LabelWidth = std::clamp(InRow.w * 0.40f, 90.0f, 170.0f);
				InRow.VSplitLeft(LabelWidth, &OutLabel, &OutControl);
			};

			const char *apAspectPresetNames[5] = {
				BCLocalize("Off (default)"),
				"5:4",
				"4:3",
				"3:2",
				BCLocalize("Custom"),
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
			Content.HSplitTop(LineSize, &Row, &Content);
			SplitRowLabelControl(Row, PresetLabel, PresetDropDown);
			Ui()->DoLabel(&PresetLabel, BCLocalize("Preset"), 14.0f, TEXTALIGN_ML);
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
				}
				else
				{
					g_Config.m_BcCustomAspectRatioMode = 1;
					g_Config.m_BcCustomAspectRatio = s_aAspectPresetValues[NewPreset];
				}
				GameClient()->m_TClient.SetForcedAspect();
			}

			Content.HSplitTop(MarginSmall, nullptr, &Content);
			Content.HSplitTop(LineSize, &Row, &Content);
			CUIRect ApplyLabel, ApplyDropDown;
			SplitRowLabelControl(Row, ApplyLabel, ApplyDropDown);
			Ui()->DoLabel(&ApplyLabel, BCLocalize("Apply"), 14.0f, TEXTALIGN_ML);
			const char *apAspectApplyNames[3] = {
				BCLocalize("Game only"),
				BCLocalize("Full"),
				BCLocalize("Game no HUD"),
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
			static int s_CustomPendingAspectValue = -1;
			static bool s_CustomPendingDirty = false;
			static int s_LastAppliedAspectValue = -1;
			if(EffectiveAspectMode == 2)
			{
				const int AppliedAspectValue = maximum(g_Config.m_BcCustomAspectRatio, 100);
				if(!s_CustomPendingDirty && s_LastAppliedAspectValue != AppliedAspectValue)
				{
					s_CustomPendingAspectValue = AppliedAspectValue;
					s_LastAppliedAspectValue = AppliedAspectValue;
				}

				const int DisplayAspectValue = maximum(s_CustomPendingAspectValue, 100);
				if(!s_CustomAspectNumeratorInput.IsActive() && !s_CustomAspectDenominatorInput.IsActive() && DisplayAspectValue != s_LastAppliedAspectValue)
				{
					s_CustomPendingAspectValue = DisplayAspectValue;
					s_LastAppliedAspectValue = DisplayAspectValue;
				}

				if(!s_CustomAspectNumeratorInput.IsActive() && !s_CustomAspectDenominatorInput.IsActive())
				{
					const int Denominator = 1080;
					const int Numerator = maximum(1, (int)std::lround((double)maximum(s_CustomPendingAspectValue, 100) * (double)Denominator / 100.0));
					if(s_CustomAspectNumeratorInput.GetInteger() != Numerator || s_CustomAspectDenominatorInput.GetInteger() != Denominator)
					{
						s_CustomAspectNumeratorInput.SetInteger(Numerator);
						s_CustomAspectDenominatorInput.SetInteger(Denominator);
					}
				}

				Content.HSplitTop(MarginSmall, nullptr, &Content);
				Content.HSplitTop(LineSize, &Row, &Content);
				CUIRect RatioLabel, RatioControls;
				SplitRowLabelControl(Row, RatioLabel, RatioControls);
				Ui()->DoLabel(&RatioLabel, BCLocalize("Custom size"), 14.0f, TEXTALIGN_ML);

				CUIRect NumeratorRect, SeparatorRect, DenominatorRect;
				const float Gap = minimum(6.0f, RatioControls.w * 0.08f);
				const float SeparatorWidth = minimum(12.0f, RatioControls.w * 0.18f);
				const float FieldWidth = maximum(1.0f, (RatioControls.w - SeparatorWidth - 2.0f * Gap) / 2.0f);
				RatioControls.VSplitLeft(FieldWidth, &NumeratorRect, &RatioControls);
				RatioControls.VSplitLeft(Gap, nullptr, &RatioControls);
				RatioControls.VSplitLeft(SeparatorWidth, &SeparatorRect, &RatioControls);
				RatioControls.VSplitLeft(Gap, nullptr, &RatioControls);
				RatioControls.VSplitLeft(FieldWidth, &DenominatorRect, nullptr);

				const bool NumeratorChanged = Ui()->DoEditBox(&s_CustomAspectNumeratorInput, &NumeratorRect, 14.0f);
				Ui()->DoLabel(&SeparatorRect, ":", 14.0f, TEXTALIGN_MC);
				const bool DenominatorChanged = Ui()->DoEditBox(&s_CustomAspectDenominatorInput, &DenominatorRect, 14.0f);

				if(NumeratorChanged || DenominatorChanged)
				{
					const int Numerator = maximum(1, s_CustomAspectNumeratorInput.GetInteger());
					const int Denominator = maximum(1, s_CustomAspectDenominatorInput.GetInteger());
					const int NewAspectValue = std::clamp((int)std::lround((double)Numerator * 100.0 / (double)Denominator), 100, 300);
					if(NewAspectValue != s_CustomPendingAspectValue)
					{
						s_CustomPendingAspectValue = NewAspectValue;
						s_CustomPendingDirty = s_CustomPendingAspectValue != g_Config.m_BcCustomAspectRatio;
					}
				}

				const bool HasPendingCustomChange = s_CustomPendingAspectValue >= 100 && s_CustomPendingAspectValue != g_Config.m_BcCustomAspectRatio;
				Content.HSplitTop(MarginSmall, nullptr, &Content);
				Content.HSplitTop(LineSize, &Row, &Content);
				CUIRect ButtonSpace, ApplyButton;
				SplitRowLabelControl(Row, ButtonSpace, ApplyButton);
				(void)ButtonSpace;
				static CButtonContainer s_AspectApplyButton;
				if(DoButton_Menu(&s_AspectApplyButton, BCLocalize("Apply"), HasPendingCustomChange ? 0 : -1, &ApplyButton) && HasPendingCustomChange)
				{
					g_Config.m_BcCustomAspectRatio = s_CustomPendingAspectValue;
					s_CustomPendingDirty = false;
					s_LastAppliedAspectValue = g_Config.m_BcCustomAspectRatio;
					GameClient()->m_TClient.SetForcedAspect();
				}
			}
			else
			{
				s_CustomPendingDirty = false;
				s_CustomPendingAspectValue = -1;
				s_LastAppliedAspectValue = -1;
			}
		}

		const float RightColumnEndY = Column.y;
		CUIRect ScrollRegion;
		ScrollRegion.x = MainView.x;
		ScrollRegion.y = maximum(LeftColumnEndY, RightColumnEndY) + MarginSmall * 2.0f;
		ScrollRegion.w = MainView.w;
		ScrollRegion.h = 0.0f;
		s_BestClientVisualsScrollRegion.AddRect(ScrollRegion);
		s_BestClientVisualsScrollRegion.End();
	}
	else if(s_CurTab == BESTCLIENT_TAB_EDITORS)
	{
		const float LineSize = 20.0f;

		if(m_AssetsEditorState.m_VisualsEditorOpen)
		{
			RenderAssetsEditorScreen(MainView);
			return;
		}

		CUIRect Label, Button;
		MainView.HSplitTop(24.0f, &Label, &MainView);
		Ui()->DoLabel(&Label, BCLocalize("Editors"), 20.0f, TEXTALIGN_ML);
		MainView.HSplitTop(5.0f, nullptr, &MainView);

		MainView.HSplitTop(LineSize, &Label, &MainView);
		Ui()->DoLabel(&Label, BCLocalize("Create mixed assets or jump to the name plate editor."), 14.0f, TEXTALIGN_ML);
		MainView.HSplitTop(5.0f, nullptr, &MainView);

		static CButtonContainer s_OpenAssetsEditorButton;
		MainView.HSplitTop(LineSize + 4.0f, &Button, &MainView);
		if(DoButton_Menu(&s_OpenAssetsEditorButton, BCLocalize("Assets editor"), 0, &Button))
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

		MainView.HSplitTop(30.0f, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Label, &MainView);
		Ui()->DoLabel(&Label, BCLocalize("Open a dedicated component toggles page."), 14.0f, TEXTALIGN_ML);
		MainView.HSplitTop(5.0f, nullptr, &MainView);

		static CButtonContainer s_OpenComponentsEditorButton;
		MainView.HSplitTop(LineSize + 4.0f, &Button, &MainView);
		if(DoButton_Menu(&s_OpenComponentsEditorButton, BCLocalize("Components editor"), 0, &Button))
			ComponentsEditorOpen();

		MainView.HSplitTop(30.0f, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Label, &MainView);
		Ui()->DoLabel(&Label, BCLocalize("Edit HUD positions directly above the live game."), 14.0f, TEXTALIGN_ML);
		MainView.HSplitTop(5.0f, nullptr, &MainView);

		static CButtonContainer s_OpenHudEditorButton;
		MainView.HSplitTop(LineSize + 4.0f, &Button, &MainView);
		const bool CanOpenHudEditor = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
		if(DoButton_Menu(&s_OpenHudEditorButton, BCLocalize("HUD editor"), CanOpenHudEditor ? 0 : -1, &Button) && CanOpenHudEditor)
		{
			SetActive(false);
			GameClient()->m_HudEditor.Activate();
		}
	}
	else if(s_CurTab == BESTCLIENT_TAB_GAMEPLAY)
	{
		const float LineSize = 20.0f;
		const float HeadlineFontSize = 20.0f;
		const float FontSize = 14.0f;
		const float EditBoxFontSize = 14.0f;
		const float MarginSmall = 5.0f;
		const float MarginBetweenSections = 30.0f;
		const float MarginBetweenViews = 30.0f;
		const ColorRGBA BlockColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f);
		const auto ModuleUiRevealAnimationsEnabled = [&]() {
			return BCUiAnimations::Enabled() && g_Config.m_BcModuleUiRevealAnimation != 0;
		};
		const auto ModuleUiRevealAnimationDuration = [&]() {
			return BCUiAnimations::MsToSeconds(g_Config.m_BcModuleUiRevealAnimationMs);
		};
		const auto UpdateRevealPhase = [&](float &Phase, bool Expanded) {
			if(ModuleUiRevealAnimationsEnabled())
				BCUiAnimations::UpdatePhase(Phase, Expanded ? 1.0f : 0.0f, Client()->RenderFrameTime(), ModuleUiRevealAnimationDuration());
			else
				Phase = Expanded ? 1.0f : 0.0f;
		};

		static CScrollRegion s_BestClientGameplayScrollRegion;
		vec2 GameplayScrollOffset(0.0f, 0.0f);
		CScrollRegionParams GameplayScrollParams;
		GameplayScrollParams.m_ScrollUnit = 60.0f;
		GameplayScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
		GameplayScrollParams.m_ScrollbarMargin = 5.0f;
		s_BestClientGameplayScrollRegion.Begin(&MainView, &GameplayScrollOffset, &GameplayScrollParams);

		MainView.y += GameplayScrollOffset.y;
		MainView.VSplitRight(5.0f, &MainView, nullptr);
		MainView.VSplitLeft(5.0f, nullptr, &MainView);

		CUIRect LeftView, RightView;
		MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
		LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
		RightView.VSplitRight(MarginSmall, &RightView, nullptr);

		static std::vector<CUIRect> s_SectionBoxes;
		static vec2 s_PrevScrollOffset(0.0f, 0.0f);
		for(CUIRect &Section : s_SectionBoxes)
		{
			float Padding = MarginBetweenViews * 0.6666f;
			Section.w += Padding;
			Section.h += Padding;
			Section.x -= Padding * 0.5f;
			Section.y -= Padding * 0.5f;
			Section.y -= s_PrevScrollOffset.y - GameplayScrollOffset.y;
			Section.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);
		}
		s_PrevScrollOffset = GameplayScrollOffset;
		s_SectionBoxes.clear();

		auto BeginBlock = [&](CUIRect &ColumnRef, float ContentHeight, CUIRect &Content) {
			CUIRect Block;
			ColumnRef.HSplitTop(ContentHeight, &Block, &ColumnRef);
			s_SectionBoxes.push_back(Block);
			Content = Block;
		};

		CUIRect Column = LeftView;
		Column.HSplitTop(10.0f, nullptr, &Column);
		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_GAMEPLAY_INPUT))
		{
			const bool IsSnapTapBlocked = GameClient()->IsSnapTapBlockedByCommunity();
			static float s_FastInputPhase = 0.0f;
			const bool FastInputExpanded = g_Config.m_TcFastInput != 0;
			UpdateRevealPhase(s_FastInputPhase, FastInputExpanded);

			const float FastInputExtraTargetHeight = MarginSmall * 3.0f + LineSize * 3.0f;
			const float ContentHeight = LineSize + MarginSmall + LineSize * 3.0f +
				FastInputExtraTargetHeight * s_FastInputPhase;

			CUIRect Content, Label, Button, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("Input"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFastInput, BCLocalize("Fast Input"), &g_Config.m_TcFastInput, &Content, LineSize);

			const float FastInputExtraHeight = FastInputExtraTargetHeight * s_FastInputPhase;
			if(FastInputExtraHeight > 0.0f)
			{
				Content.HSplitTop(FastInputExtraHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, FastInputExtraTargetHeight};
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);

				static CButtonContainer s_FastInputModeFast;
				static CButtonContainer s_FastInputModeDeltaInput;
				static CButtonContainer s_FastInputModeGammaInput;
				const int OldMode = g_Config.m_BcFastInputMode;

				Expand.HSplitTop(LineSize, &Button, &Expand);
				{
					CUIRect Left, RightRest, Middle, Right;
					Button.VSplitLeft((Button.w - 4.0f) / 3.0f, &Left, &RightRest);
					RightRest.VSplitLeft(2.0f, nullptr, &RightRest);
					RightRest.VSplitLeft((RightRest.w - 2.0f) / 2.0f, &Middle, &Right);
					Right.VSplitLeft(2.0f, nullptr, &Right);
					Left.HMargin(2.0f, &Left);
					Middle.HMargin(2.0f, &Middle);
					Right.HMargin(2.0f, &Right);

					if(DoButton_Menu(&s_FastInputModeFast, BCLocalize("Fast input"), g_Config.m_BcFastInputMode == 0, &Left, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
						g_Config.m_BcFastInputMode = 0;
					if(DoButton_Menu(&s_FastInputModeDeltaInput, BCLocalize("Delta input"), g_Config.m_BcFastInputMode == 1, &Middle, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
						g_Config.m_BcFastInputMode = 1;
					if(DoButton_Menu(&s_FastInputModeGammaInput, BCLocalize("Gamma input"), g_Config.m_BcFastInputMode == 2, &Right, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
						g_Config.m_BcFastInputMode = 2;
				}

				if(g_Config.m_BcFastInputMode != OldMode)
				{
					if(g_Config.m_BcFastInputMode == 1 && g_Config.m_BcFastInputDeltaInput <= 0)
					{
						if(OldMode == 2 && g_Config.m_BcFastInputGammaInput > 0)
							g_Config.m_BcFastInputDeltaInput = BcFastInputGammaUiToEffectiveAmount(g_Config.m_BcFastInputGammaInput);
						else if(g_Config.m_TcFastInputAmount > 0)
							g_Config.m_BcFastInputDeltaInput = std::clamp(g_Config.m_TcFastInputAmount * 5, 0, 500);
					}
					else if(g_Config.m_BcFastInputMode == 2 && g_Config.m_BcFastInputGammaInput <= 0)
					{
						if(OldMode == 1 && g_Config.m_BcFastInputDeltaInput > 0)
							g_Config.m_BcFastInputGammaInput = BcFastInputGammaEffectiveToUiAmount(g_Config.m_BcFastInputDeltaInput);
						else if(g_Config.m_TcFastInputAmount > 0)
							g_Config.m_BcFastInputGammaInput = BcFastInputGammaEffectiveToUiAmount(g_Config.m_TcFastInputAmount * 5);
					}
					else if(g_Config.m_BcFastInputMode == 0 && g_Config.m_TcFastInputAmount <= 0)
					{
						const int SourceAmount = OldMode == 2 ? BcFastInputGammaUiToEffectiveAmount(g_Config.m_BcFastInputGammaInput) : g_Config.m_BcFastInputDeltaInput;
						if(SourceAmount > 0)
							g_Config.m_TcFastInputAmount = std::clamp((SourceAmount + 2) / 5, 0, 40);
					}
				}

				Expand.HSplitTop(MarginSmall, nullptr, &Expand);
				Expand.HSplitTop(LineSize, &Button, &Expand);
				if(g_Config.m_BcFastInputMode == 0)
				{
					DoSliderWithScaledValue(&g_Config.m_TcFastInputAmount, &g_Config.m_TcFastInputAmount, &Button, BCLocalize("Amount"), 0, 40, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
				}
				else
				{
					const int Min = 0;
					const bool GammaMode = g_Config.m_BcFastInputMode == 2;
					if(GammaMode)
					{
						const int Max = BC_FAST_INPUT_GAMMA_UI_MAX;
						int *pAmountValue = &g_Config.m_BcFastInputGammaInput;
						int Value = std::clamp(*pAmountValue, Min, Max);

						const int Increment = std::max(1, (Max - Min) / 35);
						if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_UP) && Ui()->MouseInside(&Button))
							Value = std::clamp(Value + Increment, Min, Max);
						if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN) && Ui()->MouseInside(&Button))
							Value = std::clamp(Value - Increment, Min, Max);

						char aBuf[256];
						str_format(aBuf, sizeof(aBuf), "%s: %.2fM", BCLocalize("Gamma amount"), Value / 100.0f);

						CUIRect AmountLabel, ScrollBar;
						Button.VSplitMid(&AmountLabel, &ScrollBar, minimum(10.0f, Button.w * 0.05f));
						const float LabelFontSize = AmountLabel.h * CUi::ms_FontmodHeight * 0.8f;
						Ui()->DoLabel(&AmountLabel, aBuf, LabelFontSize, TEXTALIGN_ML);

						const float Rel = (Value - Min) / (float)(Max - Min);
						const float NewRel = Ui()->DoScrollbarH(pAmountValue, &ScrollBar, Rel);
						Value = (int)(Min + NewRel * (Max - Min) + 0.5f);
						*pAmountValue = std::clamp(Value, Min, Max);
					}
					else
					{
						const int Max = 500;
						int *pAmountValue = &g_Config.m_BcFastInputDeltaInput;
						int Value = std::clamp(*pAmountValue, Min, Max);

						const int Increment = std::max(1, (Max - Min) / 35);
						if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_UP) && Ui()->MouseInside(&Button))
							Value = std::clamp(Value + Increment, Min, Max);
						if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN) && Ui()->MouseInside(&Button))
							Value = std::clamp(Value - Increment, Min, Max);

						char aBuf[256];
						str_format(aBuf, sizeof(aBuf), "%s: %.2fA", BCLocalize("Amount"), Value / 100.0f);

						CUIRect AmountLabel, ScrollBar;
						Button.VSplitMid(&AmountLabel, &ScrollBar, minimum(10.0f, Button.w * 0.05f));
						const float LabelFontSize = AmountLabel.h * CUi::ms_FontmodHeight * 0.8f;
						Ui()->DoLabel(&AmountLabel, aBuf, LabelFontSize, TEXTALIGN_ML);

						const float Rel = (Value - Min) / (float)(Max - Min);
						const float NewRel = Ui()->DoScrollbarH(pAmountValue, &ScrollBar, Rel);
						Value = (int)(Min + NewRel * (Max - Min) + 0.5f);
						*pAmountValue = std::clamp(Value, Min, Max);
					}
				}

				Expand.HSplitTop(MarginSmall, nullptr, &Expand);
				if(g_Config.m_BcFastInputMode == 0)
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFastInputOthers, BCLocalize("Fast Input others"), &g_Config.m_TcFastInputOthers, &Expand, LineSize);
				else if(g_Config.m_BcFastInputMode == 1)
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcDeltaInputOthers, BCLocalize("Delta input others"), &g_Config.m_BcDeltaInputOthers, &Expand, LineSize);
				else
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcGammaInputOthers, BCLocalize("Gamma input others"), &g_Config.m_BcGammaInputOthers, &Expand, LineSize);
			}

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClSubTickAiming, BCLocalize("Sub-Tick aiming"), &g_Config.m_ClSubTickAiming, &Content, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcFastInputAutoMargin, BCLocalize("Auto margin"), &g_Config.m_BcFastInputAutoMargin, &Content, LineSize);
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);

			static float s_SnapTapPhase = 0.0f;
			const bool SnapTapExpanded = g_Config.m_BcSnapTap != 0;
			UpdateRevealPhase(s_SnapTapPhase, SnapTapExpanded);
			const float SnapTapExtraTargetHeight = MarginSmall + LineSize;
			const float SnapTapBlockedHintHeight = IsSnapTapBlocked ? (MarginSmall + LineSize) : 0.0f;
			const float SnapTapContentHeight = LineSize + MarginSmall + LineSize +
				SnapTapBlockedHintHeight +
				SnapTapExtraTargetHeight * s_SnapTapPhase;

			BeginBlock(Column, SnapTapContentHeight, Content);
			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("Snap Tap"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcSnapTap, BCLocalize("Enable"), &g_Config.m_BcSnapTap, &Content, LineSize);

			const float SnapTapExtraHeight = SnapTapExtraTargetHeight * s_SnapTapPhase;
			if(SnapTapExtraHeight > 0.0f)
			{
				Content.HSplitTop(SnapTapExtraHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, SnapTapExtraTargetHeight};
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);
				Expand.HSplitTop(LineSize, &Button, &Expand);

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
					str_format(aBuf, sizeof(aBuf), "%s: Off", BCLocalize("Delay"));
				else
					str_format(aBuf, sizeof(aBuf), "%s: %dms", BCLocalize("Delay"), Value);

				CUIRect DelayLabel, ScrollBar;
				Button.VSplitMid(&DelayLabel, &ScrollBar, minimum(10.0f, Button.w * 0.05f));
				const float LabelFontSize = DelayLabel.h * CUi::ms_FontmodHeight * 0.8f;
				Ui()->DoLabel(&DelayLabel, aBuf, LabelFontSize, TEXTALIGN_ML);

				const float Rel = (Value - Min) / (float)(Max - Min);
				const float NewRel = Ui()->DoScrollbarH(&g_Config.m_BcSnapTapDelay, &ScrollBar, Rel);
				Value = (int)(Min + NewRel * (Max - Min) + 0.5f);
				g_Config.m_BcSnapTapDelay = std::clamp(Value, Min, Max);
			}
			if(IsSnapTapBlocked)
			{
				Content.HSplitTop(MarginSmall, nullptr, &Content);
				Content.HSplitTop(LineSize, &Label, &Content);
				TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
				Ui()->DoLabel(&Label, BCLocalize("Looks like you're on a server where this feature is forbidden"), FontSize, TEXTALIGN_ML);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}

			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);

			const bool HookPredictorEnabled = g_Config.m_BcHookTrajectoryPredictor != 0;
			float HookPredictorContentHeight = LineSize + MarginSmall + LineSize;
			if(HookPredictorEnabled)
				HookPredictorContentHeight += MarginSmall + LineSize + MarginSmall + LineSize;
			BeginBlock(Column, HookPredictorContentHeight, Content);
			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("Hook Predictor"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcHookTrajectoryPredictor, BCLocalize("Enable"), &g_Config.m_BcHookTrajectoryPredictor, &Content, LineSize);
			if(g_Config.m_BcHookTrajectoryPredictor)
			{
				Content.HSplitTop(MarginSmall, nullptr, &Content);
				Content.HSplitTop(LineSize, &Button, &Content);
				DoSliderWithScaledValue(&g_Config.m_BcHookTrajectoryPredictorReleaseOffset, &g_Config.m_BcHookTrajectoryPredictorReleaseOffset, &Button, BCLocalize("Release offset"), 0, 250, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
				Content.HSplitTop(MarginSmall, nullptr, &Content);
				Content.HSplitTop(LineSize, &Button, &Content);
				DoSliderWithScaledValue(&g_Config.m_BcHookTrajectoryPredictorReleaseWindow, &g_Config.m_BcHookTrajectoryPredictorReleaseWindow, &Button, BCLocalize("Release window"), 5, 250, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
			}

			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}
		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_OPTIMIZER))
		{
			static float s_OptimizerPhase = 0.0f;
			static float s_OptimizerFogPhase = 0.0f;
			const float Dt = Client()->RenderFrameTime();
			const bool Enabled = g_Config.m_BcOptimizer != 0;
			const bool FogOn = Enabled && g_Config.m_BcOptimizerFpsFog != 0;
			if(ModuleUiRevealAnimationsEnabled())
			{
				BCUiAnimations::UpdatePhase(s_OptimizerPhase, Enabled ? 1.0f : 0.0f, Dt, ModuleUiRevealAnimationDuration());
				BCUiAnimations::UpdatePhase(s_OptimizerFogPhase, FogOn ? 1.0f : 0.0f, Dt, 0.16f);
			}
			else
			{
				s_OptimizerPhase = Enabled ? 1.0f : 0.0f;
				s_OptimizerFogPhase = FogOn ? 1.0f : 0.0f;
			}

			const float RadioTargetHeight = 22.0f;
			const float FogTargetHeight = 3.0f * LineSize + RadioTargetHeight;
			const float BaseTargetHeight = 5.0f * LineSize;
			const float ExtraTargetHeight = BaseTargetHeight + FogTargetHeight * s_OptimizerFogPhase;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExtraTargetHeight * s_OptimizerPhase;
			CUIRect Content, Label, Row, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("Optimizer"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizer, BCLocalize("Enable optimizer"), &g_Config.m_BcOptimizer, &Content, LineSize);

			const float ExpandedHeight = ExtraTargetHeight * s_OptimizerPhase;
			if(ExpandedHeight > 0.0f)
			{
				Content.HSplitTop(ExpandedHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExtraTargetHeight};
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerDisableParticles, BCLocalize("Disable all particles render"), &g_Config.m_BcOptimizerDisableParticles, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_GfxHighDetail, BCLocalize("High Detail"), &g_Config.m_GfxHighDetail, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerFpsFog, BCLocalize("FPS fog (cull outside limit)"), &g_Config.m_BcOptimizerFpsFog, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerDdnetPriorityHigh, BCLocalize("DDNet priority: High"), &g_Config.m_BcOptimizerDdnetPriorityHigh, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerDiscordPriorityBelowNormal, BCLocalize("Discord priority: Below Normal"), &g_Config.m_BcOptimizerDiscordPriorityBelowNormal, &Expand, LineSize);

				const float FogHeight = FogTargetHeight * s_OptimizerFogPhase;
				if(FogHeight > 0.0f)
				{
					CUIRect FogVisible;
					Expand.HSplitTop(FogHeight, &FogVisible, &Expand);
					Ui()->ClipEnable(&FogVisible);
					SScopedClip FogClipGuard{Ui()};

					CUIRect FogExpand = {FogVisible.x, FogVisible.y, FogVisible.w, FogTargetHeight};
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerFpsFogRenderRect, BCLocalize("Render FPS fog rectangle"), &g_Config.m_BcOptimizerFpsFogRenderRect, &FogExpand, LineSize);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerFpsFogCullMapTiles, BCLocalize("Cull map tiles outside FPS fog"), &g_Config.m_BcOptimizerFpsFogCullMapTiles, &FogExpand, LineSize);

					static std::vector<CButtonContainer> s_OptimizerFogModeButtons = {{}, {}};
					int FogMode = g_Config.m_BcOptimizerFpsFogMode;
					if(DoLine_RadioMenu(FogExpand, BCLocalize("FPS fog mode"),
						   s_OptimizerFogModeButtons,
						   {BCLocalize("Manual radius"), BCLocalize("By zoom")},
						   {0, 1},
						   FogMode))
					{
						g_Config.m_BcOptimizerFpsFogMode = FogMode;
					}

					FogExpand.HSplitTop(LineSize, &Row, &FogExpand);
					if(g_Config.m_BcOptimizerFpsFogMode == 0)
						Ui()->DoScrollbarOption(&g_Config.m_BcOptimizerFpsFogRadiusTiles, &g_Config.m_BcOptimizerFpsFogRadiusTiles, &Row, BCLocalize("Radius (tiles)"), 5, 300);
					else
						Ui()->DoScrollbarOption(&g_Config.m_BcOptimizerFpsFogZoomPercent, &g_Config.m_BcOptimizerFpsFogZoomPercent, &Row, BCLocalize("Visible area (%)"), 10, 120, &CUi::ms_LinearScrollbarScale, 0, "%");
				}
			}
		}

		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_GAMEPLAY_GORES_MODE))
		{
			static float s_GoresModePhase = 0.0f;
			const bool GoresModeExpanded = g_Config.m_BcGoresMode != 0;
			UpdateRevealPhase(s_GoresModePhase, GoresModeExpanded);
			const float ExpandedTargetHeight = MarginSmall + LineSize;
			const float ExpandedHeight = ExpandedTargetHeight * s_GoresModePhase;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExpandedHeight;
			CUIRect Content, Label, Button, Visible;
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("Gores mode"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcGoresMode, BCLocalize("Enable gores mode"), &g_Config.m_BcGoresMode, &Content, LineSize);
			if(ExpandedHeight > 0.0f)
			{
				Content.HSplitTop(ExpandedHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExpandedTargetHeight};
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcGoresModeDisableIfWeapons, BCLocalize("Disable if you have shotgun, grenade or laser"), &g_Config.m_BcGoresModeDisableIfWeapons, &Expand, LineSize);
			}
		}

		const float LeftColumnEndY = Column.y;
		Column = RightView;
		Column.HSplitTop(10.0f, nullptr, &Column);

		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_GAMEPLAY_FAST_ACTIONS))
		{
			static char s_aBindName[FAST_ACTIONS_MAX_NAME] = "";
			static char s_aBindCommand[FAST_ACTIONS_MAX_CMD] = "";
			static int s_SelectedBindIndex = 0;
			static int s_LastSelectedBindIndex = -1;

			const float WheelPreviewHeight = 96.0f;
			const float ContentHeight = LineSize + MarginSmall +
				WheelPreviewHeight + MarginSmall +
				LineSize + MarginSmall +
				LineSize + MarginSmall +
				LineSize + MarginSmall +
				LineSize + MarginSmall +
				LineSize * 0.8f + MarginSmall +
				LineSize;

			CUIRect Content, Label, Button;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("Fast Actions"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			int HoveringIndex = -1;
			CUIRect WheelPreview;
			Content.HSplitTop(WheelPreviewHeight, &WheelPreview, &Content);
			const vec2 Center = WheelPreview.Center();
			const float LineInset = 18.0f;
			const float LineHalfWidth = maximum(40.0f, WheelPreview.w / 2.0f - LineInset);
			const float LineHeight = minimum(WheelPreview.h * 0.78f, 44.0f);
			const float SelectBandHalfHeight = LineHeight * 1.2f;
			const float LabelW = 52.0f;
			const float LabelH = 52.0f;
			const float TextHalfRange = maximum(0.0f, LineHalfWidth - LabelW / 2.0f - 2.0f);

			Graphics()->DrawRect(Center.x - LineHalfWidth, Center.y - LineHeight / 2.0f, LineHalfWidth * 2.0f, LineHeight, ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f), IGraphics::CORNER_ALL, 8.0f);

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

			Content.HSplitTop(MarginSmall, nullptr, &Content);
			Content.HSplitTop(LineSize, &Button, &Content);
			char aSlotLabel[64];
			str_format(aSlotLabel, sizeof(aSlotLabel), "%s %d", BCLocalize("Selected slot"), s_SelectedBindIndex + 1);
			Ui()->DoLabel(&Button, aSlotLabel, FontSize, TEXTALIGN_ML);

			Content.HSplitTop(MarginSmall, nullptr, &Content);
			Content.HSplitTop(LineSize, &Button, &Content);
			Button.VSplitLeft(150.0f, &Label, &Button);
			Ui()->DoLabel(&Label, BCLocalize("Name:"), FontSize, TEXTALIGN_ML);
			static CLineInput s_BindNameInput;
			s_BindNameInput.SetBuffer(s_aBindName, sizeof(s_aBindName));
			s_BindNameInput.SetEmptyText(BCLocalize("Name (optional)"));
			Ui()->DoEditBox(&s_BindNameInput, &Button, EditBoxFontSize);

			Content.HSplitTop(MarginSmall, nullptr, &Content);
			Content.HSplitTop(LineSize, &Button, &Content);
			Button.VSplitLeft(150.0f, &Label, &Button);
			Ui()->DoLabel(&Label, BCLocalize("Command:"), FontSize, TEXTALIGN_ML);
			static CLineInput s_BindInput;
			s_BindInput.SetBuffer(s_aBindCommand, sizeof(s_aBindCommand));
			s_BindInput.SetEmptyText(BCLocalize("Command"));
			Ui()->DoEditBox(&s_BindInput, &Button, EditBoxFontSize);

			if(s_SelectedBindIndex < static_cast<int>(GameClient()->m_FastActions.m_vBinds.size()))
			{
				str_copy(GameClient()->m_FastActions.m_vBinds[s_SelectedBindIndex].m_aName, s_aBindName);
				str_copy(GameClient()->m_FastActions.m_vBinds[s_SelectedBindIndex].m_aCommand, s_aBindCommand);
			}

			static CButtonContainer s_ClearButton;
			Content.HSplitTop(MarginSmall, nullptr, &Content);
			Content.HSplitTop(LineSize, &Button, &Content);
			if(DoButton_Menu(&s_ClearButton, BCLocalize("Clear command"), 0, &Button) &&
				s_SelectedBindIndex < static_cast<int>(GameClient()->m_FastActions.m_vBinds.size()))
			{
				GameClient()->m_FastActions.m_vBinds[s_SelectedBindIndex].m_aCommand[0] = '\0';
				s_aBindCommand[0] = '\0';
			}

			Content.HSplitTop(MarginSmall, nullptr, &Content);
			Content.HSplitTop(LineSize * 0.8f, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("In game: hold bind key, press 1..6, release key to execute"), FontSize * 0.8f, TEXTALIGN_ML);

			Content.HSplitTop(MarginSmall, nullptr, &Content);
			Content.HSplitTop(LineSize, &Label, &Content);
			static CButtonContainer s_ReaderButtonWheel;
			static CButtonContainer s_ClearButtonWheel;
			DoLine_KeyReader(Label, s_ReaderButtonWheel, s_ClearButtonWheel, BCLocalize("Fast Actions key"), "+fa");
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_GAMEPLAY_SPEEDRUN_TIMER))
		{
			static float s_SpeedrunPhase = 0.0f;
			const bool SpeedrunExpanded = g_Config.m_BcSpeedrunTimer != 0;
			UpdateRevealPhase(s_SpeedrunPhase, SpeedrunExpanded);
			const float ExpandedTargetHeight = LineSize * 5.0f + MarginSmall * 6.0f;
			const float ExpandedHeight = ExpandedTargetHeight * s_SpeedrunPhase;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExpandedHeight;
			CUIRect Content, Label, Button, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("Speedrun timer"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcSpeedrunTimer, BCLocalize("Enable speedrun timer"), &g_Config.m_BcSpeedrunTimer, &Content, LineSize);
			if(ExpandedHeight > 0.0f)
			{
				if(g_Config.m_BcSpeedrunTimerHours == 0 &&
					g_Config.m_BcSpeedrunTimerMinutes == 0 &&
					g_Config.m_BcSpeedrunTimerSeconds == 0 &&
					g_Config.m_BcSpeedrunTimerMilliseconds == 0 &&
					g_Config.m_BcSpeedrunTimerTime > 0)
				{
					const int LegacyMinutes = g_Config.m_BcSpeedrunTimerTime / 100;
					const int LegacySeconds = g_Config.m_BcSpeedrunTimerTime % 100;
					const int TotalLegacySeconds = LegacyMinutes * 60 + LegacySeconds;
					g_Config.m_BcSpeedrunTimerHours = TotalLegacySeconds / 3600;
					g_Config.m_BcSpeedrunTimerMinutes = (TotalLegacySeconds % 3600) / 60;
					g_Config.m_BcSpeedrunTimerSeconds = TotalLegacySeconds % 60;
				}

				Content.HSplitTop(ExpandedHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExpandedTargetHeight};
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);
				Expand.HSplitTop(LineSize, &Button, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcSpeedrunTimerHours, &g_Config.m_BcSpeedrunTimerHours, &Button, BCLocalize("Hours"), 0, 99);
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);

				Expand.HSplitTop(LineSize, &Button, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcSpeedrunTimerMinutes, &g_Config.m_BcSpeedrunTimerMinutes, &Button, BCLocalize("Minutes"), 0, 59);
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);

				Expand.HSplitTop(LineSize, &Button, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcSpeedrunTimerSeconds, &g_Config.m_BcSpeedrunTimerSeconds, &Button, BCLocalize("Seconds"), 0, 59);
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);

				Expand.HSplitTop(LineSize, &Button, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcSpeedrunTimerMilliseconds, &g_Config.m_BcSpeedrunTimerMilliseconds, &Button, BCLocalize("Milliseconds"), 0, 999, &CUi::ms_LinearScrollbarScale, 0, "ms");
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);

				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcSpeedrunTimerAutoDisable, BCLocalize("Auto disable after time end"), &g_Config.m_BcSpeedrunTimerAutoDisable, &Expand, LineSize);
			}

			// Keep legacy MMSS setting synchronized for backward compatibility.
			g_Config.m_BcSpeedrunTimerTime = g_Config.m_BcSpeedrunTimerMinutes * 100 + g_Config.m_BcSpeedrunTimerSeconds;
		}

		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_GAMEPLAY_AUTO_TEAM_LOCK))
		{
			static float s_AutoTeamLockPhase = 0.0f;
			const bool AutoTeamLockExpanded = g_Config.m_BcAutoTeamLock != 0;
			UpdateRevealPhase(s_AutoTeamLockPhase, AutoTeamLockExpanded);
			const float ExpandedTargetHeight = MarginSmall + LineSize;
			const float ExpandedHeight = ExpandedTargetHeight * s_AutoTeamLockPhase;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExpandedHeight;
			CUIRect Content, Label, Button, Visible;
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("Auto team lock"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcAutoTeamLock, BCLocalize("Lock team automatically after joining"), &g_Config.m_BcAutoTeamLock, &Content, LineSize);
			if(ExpandedHeight > 0.0f)
			{
				Content.HSplitTop(ExpandedHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExpandedTargetHeight};
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);
				Expand.HSplitTop(LineSize, &Button, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcAutoTeamLockDelay, &g_Config.m_BcAutoTeamLockDelay, &Button, BCLocalize("Delay"), 0, 30, &CUi::ms_LinearScrollbarScale, 0, "s");
			}
		}

		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_FOCUS_MODE))
		{
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
			static float s_FocusModePhase = 0.0f;
			const bool Enabled = g_Config.m_ClFocusMode != 0;
			UpdateRevealPhase(s_FocusModePhase, Enabled);

			const float KeyReaderHeight = LineSize + MarginSmall;
			const float ExpandedTargetHeight = KeyReaderHeight + 7.0f * LineSize;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExpandedTargetHeight * s_FocusModePhase;
			CUIRect Content, Label, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("Focus Mode"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusMode, BCLocalize("Enable Focus Mode"), &g_Config.m_ClFocusMode, &Content, LineSize);
			static CButtonContainer s_FocusModeBindReader;
			static CButtonContainer s_FocusModeBindClear;

			const float CurHeight = ExpandedTargetHeight * s_FocusModePhase;
			if(CurHeight > 0.0f)
			{
				Content.HSplitTop(CurHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExpandedTargetHeight};
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideNames, BCLocalize("Hide Player Names"), &g_Config.m_ClFocusModeHideNames, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideEffects, BCLocalize("Hide Visual Effects"), &g_Config.m_ClFocusModeHideEffects, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideHud, BCLocalize("Hide HUD"), &g_Config.m_ClFocusModeHideHud, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideSongPlayer, BCLocalize("Hide Song Player"), &g_Config.m_ClFocusModeHideSongPlayer, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideUI, BCLocalize("Hide Unnecessary UI"), &g_Config.m_ClFocusModeHideUI, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideChat, BCLocalize("Hide Chat"), &g_Config.m_ClFocusModeHideChat, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideScoreboard, BCLocalize("Hide Scoreboard"), &g_Config.m_ClFocusModeHideScoreboard, &Expand, LineSize);
				DoLine_KeyReader(Expand, s_FocusModeBindReader, s_FocusModeBindClear, BCLocalize("Focus mode bind"), "toggle p_focus_mode 0 1");
			}
		}

		const float RightColumnEndY = Column.y;
		CUIRect ScrollRegion;
		ScrollRegion.x = MainView.x;
		ScrollRegion.y = maximum(LeftColumnEndY, RightColumnEndY) + MarginSmall * 2.0f;
		ScrollRegion.w = MainView.w;
		ScrollRegion.h = 0.0f;
		s_BestClientGameplayScrollRegion.AddRect(ScrollRegion);
		s_BestClientGameplayScrollRegion.End();
	}
	else if(s_CurTab == BESTCLIENT_TAB_FUN)
	{
		RenderSettingsBestClientFun(MainView);
	}
	else if(s_CurTab == BESTCLIENT_TAB_OTHERS)
	{
		const float LineSize = 20.0f;
		const float HeadlineFontSize = 20.0f;
		const float MarginSmall = 5.0f;
		const float MarginExtraSmall = 2.5f;
		const float MarginBetweenViews = 30.0f;
		const float MarginBetweenSections = 30.0f;
		const ColorRGBA BlockColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f);
		const auto ModuleUiRevealAnimationsEnabled = [&]() {
			return BCUiAnimations::Enabled() && g_Config.m_BcModuleUiRevealAnimation != 0;
		};
		const auto ModuleUiRevealAnimationDuration = [&]() {
			return BCUiAnimations::MsToSeconds(g_Config.m_BcModuleUiRevealAnimationMs);
		};
		const auto UpdateRevealPhase = [&](float &Phase, bool Expanded) {
			if(ModuleUiRevealAnimationsEnabled())
				BCUiAnimations::UpdatePhase(Phase, Expanded ? 1.0f : 0.0f, Client()->RenderFrameTime(), ModuleUiRevealAnimationDuration());
			else
				Phase = Expanded ? 1.0f : 0.0f;
		};

		static CScrollRegion s_BestClientOthersScrollRegion;
		vec2 OthersScrollOffset(0.0f, 0.0f);
		CScrollRegionParams OthersScrollParams;
		OthersScrollParams.m_ScrollUnit = 60.0f;
		OthersScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
		OthersScrollParams.m_ScrollbarMargin = 5.0f;
		s_BestClientOthersScrollRegion.Begin(&MainView, &OthersScrollOffset, &OthersScrollParams);

		MainView.y += OthersScrollOffset.y;
		MainView.VSplitRight(5.0f, &MainView, nullptr);
		MainView.VSplitLeft(5.0f, nullptr, &MainView);

		static std::vector<CUIRect> s_SectionBoxes;
		static vec2 s_PrevScrollOffset(0.0f, 0.0f);
		for(CUIRect &Section : s_SectionBoxes)
		{
			float Padding = MarginBetweenViews * 0.6666f;
			Section.w += Padding;
			Section.h += Padding;
			Section.x -= Padding * 0.5f;
			Section.y -= Padding * 0.5f;
			Section.y -= s_PrevScrollOffset.y - OthersScrollOffset.y;
			Section.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);
		}
		s_PrevScrollOffset = OthersScrollOffset;
		s_SectionBoxes.clear();

		auto BeginBlock = [&](CUIRect &ColumnRef, float ContentHeight, CUIRect &Content) {
			CUIRect Block;
			ColumnRef.HSplitTop(ContentHeight, &Block, &ColumnRef);
			s_SectionBoxes.push_back(Block);
			Content = Block;
		};

		CUIRect LeftView, RightView;
		MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
		LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
		RightView.VSplitRight(MarginSmall, &RightView, nullptr);

		CUIRect Column = LeftView;
		Column.HSplitTop(10.0f, nullptr, &Column);

		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_OTHERS_MISC))
		{
			const float ColorPickerLineSize = 25.0f;
			const float ColorPickerLabelSize = 13.0f;
			const float ColorPickerLineSpacing = 5.0f;
			const bool ShowRealHitboxEnabled = g_Config.m_BcShowRealHitbox != 0;
			const float ColorPickerHeight = ShowRealHitboxEnabled ? (ColorPickerLineSize + ColorPickerLineSpacing) : 0.0f;
			const float ContentHeight = LineSize + MarginSmall + 8.0f * LineSize + ColorPickerHeight;
			CUIRect Content, Label, Row;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("Misc"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			static CButtonContainer s_SettingsLayoutButton;
			int UseNewMenuLayout = g_Config.m_BcSettingsLayout == 0 ? 1 : 0;
			DoButton_CheckBoxAutoVMarginAndSet(&s_SettingsLayoutButton, BCLocalize("Use new menu layout"), &UseNewMenuLayout, &Content, LineSize);
			g_Config.m_BcSettingsLayout = UseNewMenuLayout ? 0 : 1;
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcHideHudInSettings, BCLocalize("Hide hud in settings"), &g_Config.m_BcHideHudInSettings, &Content, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcEscPlayerList, BCLocalize("Show ESC players list"), &g_Config.m_BcEscPlayerList, &Content, LineSize);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcEmoticonShadow, BCLocalize("Shadow of Emotions"), &g_Config.m_BcEmoticonShadow, &Content, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatSaveDraft, BCLocalize("Save unsent messages"), &g_Config.m_BcChatSaveDraft, &Content, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcShowhudDummyCoordIndicator, BCLocalize("Show player below indicator"), &g_Config.m_BcShowhudDummyCoordIndicator, &Content, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcShowRealHitbox, BCLocalize("Show real hitbox"), &g_Config.m_BcShowRealHitbox, &Content, LineSize);
			Content.HSplitTop(LineSize, &Row, &Content);
			Ui()->DoScrollbarOption(&g_Config.m_UiScale, &g_Config.m_UiScale, &Row, BCLocalize("UI scale"), 50, 200, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, "%");
			if(g_Config.m_BcShowRealHitbox)
			{
				static CButtonContainer s_RealHitboxDotColorButton;
				DoLine_ColorPicker(&s_RealHitboxDotColorButton, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Content, BCLocalize("Real hitbox dot color"), &g_Config.m_BcShowRealHitboxColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::BcShowRealHitboxColor, true)), false, nullptr, true);
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		{
			const float ContentHeight = LineSize + MarginSmall + 3.0f * LineSize;
			CUIRect Content, Label, Row;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("Browser Utils"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcAutoServerListRefresh, BCLocalize("Auto server list refresh"), &g_Config.m_BcAutoServerListRefresh, &Content, LineSize);
			Content.HSplitTop(LineSize, &Row, &Content);
			Ui()->DoScrollbarOption(&g_Config.m_BcAutoServerListRefreshSeconds, &g_Config.m_BcAutoServerListRefreshSeconds, &Row, BCLocalize("Seconds"), 1, 300, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, " s");
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcUseShortKogServerName, BCLocalize("Use short KoG server name"), &g_Config.m_BcUseShortKogServerName, &Content, LineSize);

			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_OTHERS_CHAT_MEDIA))
		{
			static float s_ChatMediaPhase = 0.0f;
			const bool ChatMediaEnabled = g_Config.m_BcChatMediaPreview != 0;
			const bool ContentFilterEnabled = g_Config.m_BcChatMediaContentFilter != 0;
			UpdateRevealPhase(s_ChatMediaPhase, ChatMediaEnabled);
			const float KeyReaderHeight = LineSize + 2.5f;
			const float BaseExpandedHeight = 6.0f * LineSize + KeyReaderHeight;
			const float FilterSettingsHeight = 2.0f * LineSize;
			const float ExpandedTargetHeight = BaseExpandedHeight + (ContentFilterEnabled ? FilterSettingsHeight : 0.0f);
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExpandedTargetHeight * s_ChatMediaPhase;
			CUIRect Content, Label, Row, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("Chat Media"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			CChat &Chat = GameClient()->m_Chat;
			if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatMediaPreview, BCLocalize("Render media previews from chat links"), &g_Config.m_BcChatMediaPreview, &Content, LineSize))
				Chat.RebuildChat();

			const float ExpandedHeight = ExpandedTargetHeight * s_ChatMediaPhase;
			if(ExpandedHeight > 0.0f)
			{
				Content.HSplitTop(ExpandedHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExpandedTargetHeight};

				if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatMediaPhotos, BCLocalize("Show photos in chat media"), &g_Config.m_BcChatMediaPhotos, &Expand, LineSize))
					Chat.RebuildChat();

				if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatMediaGifs, BCLocalize("Show GIFs in chat media"), &g_Config.m_BcChatMediaGifs, &Expand, LineSize))
					Chat.RebuildChat();

				if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatMediaContentFilter, BCLocalize("Content filtering"), &g_Config.m_BcChatMediaContentFilter, &Expand, LineSize))
					Chat.RebuildChat();

				if(g_Config.m_BcChatMediaContentFilter)
				{
					Expand.HSplitTop(LineSize, &Row, &Expand);
					Ui()->DoLabel(&Row, BCLocalize("Allowed media domains"), 12.0f, TEXTALIGN_ML);

					Expand.HSplitTop(LineSize, &Row, &Expand);
					static CLineInput s_ChatMediaAllowedDomains(g_Config.m_BcChatMediaAllowedDomains, sizeof(g_Config.m_BcChatMediaAllowedDomains));
					s_ChatMediaAllowedDomains.SetEmptyText("tenor.com; imgur.com; giphy.com");
					if(Ui()->DoClearableEditBox(&s_ChatMediaAllowedDomains, &Row, 14.0f))
						Chat.RebuildChat();
					GameClient()->m_Tooltips.DoToolTip(&s_ChatMediaAllowedDomains, &Row, BCLocalize("Semicolon-separated allowlist, for example: tenor.com; imgur.com; giphy.com; cdn.discordapp.com"));
				}

				Expand.HSplitTop(LineSize, &Row, &Expand);
				if(Ui()->DoScrollbarOption(&g_Config.m_BcChatMediaPreviewMaxWidth, &g_Config.m_BcChatMediaPreviewMaxWidth, &Row, BCLocalize("Media preview width"), 120, 400))
					Chat.RebuildChat();

				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatMediaViewer, BCLocalize("Enable fullscreen media viewer in chat"), &g_Config.m_BcChatMediaViewer, &Expand, LineSize);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcChatMediaViewerMaxZoom, &g_Config.m_BcChatMediaViewerMaxZoom, &Row, BCLocalize("Viewer max zoom"), 100, 2000, &CUi::ms_LinearScrollbarScale, 0u, "%");

				static CButtonContainer s_HideMediaBindReader;
				static CButtonContainer s_HideMediaBindClear;
				DoLine_KeyReader(Expand, s_HideMediaBindReader, s_HideMediaBindClear, BCLocalize("Hide media bind"), "toggle_chat_media_hidden");
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		const float LeftColumnEndY = Column.y;
		Column = RightView;
		Column.HSplitTop(10.0f, nullptr, &Column);

		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_OTHERS_VOICE_SETTINGS))
		{
			static float s_VoiceSettingsPhase = 0.0f;
			const bool VoiceSettingsExpanded = g_Config.m_BcVoiceChatEnable != 0;
			UpdateRevealPhase(s_VoiceSettingsPhase, VoiceSettingsExpanded);

			const float VoiceSettingsHeight = GameClient()->m_VoiceChat.GetMenuSettingsBlockHeight(s_VoiceSettingsPhase);
			CUIRect VoiceSettingsView;
			BeginBlock(Column, VoiceSettingsHeight, VoiceSettingsView);
			GameClient()->m_VoiceChat.RenderMenuSettingsBlock(VoiceSettingsView, s_VoiceSettingsPhase);
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_OTHERS_VOICE_BINDS))
		{
			static CButtonContainer s_VoicePanelBindReader;
			static CButtonContainer s_VoicePanelBindClear;
			static CButtonContainer s_PushToTalkBindReader;
			static CButtonContainer s_PushToTalkBindClear;
			static CButtonContainer s_MicMuteBindReader;
			static CButtonContainer s_MicMuteBindClear;
			static CButtonContainer s_HeadphonesMuteBindReader;
			static CButtonContainer s_HeadphonesMuteBindClear;

			const float ContentHeight = 4.0f * (LineSize + MarginExtraSmall);
			CUIRect BindsView;
			BeginBlock(Column, ContentHeight, BindsView);
			DoLine_KeyReader(BindsView, s_VoicePanelBindReader, s_VoicePanelBindClear, BCLocalize("Voice panel"), "toggle_voice_panel");
			DoLine_KeyReader(BindsView, s_PushToTalkBindReader, s_PushToTalkBindClear, BCLocalize("Push-to-talk"), "+voicechat");
			DoLine_KeyReader(BindsView, s_MicMuteBindReader, s_MicMuteBindClear, BCLocalize("Mute microphone"), "toggle_voice_mic_mute");
			DoLine_KeyReader(BindsView, s_HeadphonesMuteBindReader, s_HeadphonesMuteBindClear, BCLocalize("Mute headphones"), "toggle_voice_headphones_mute");
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_OTHERS_CLIENT_INDICATOR))
		{
			const bool ShowNamePlateSettings = g_Config.m_BcClientIndicatorInNamePlate != 0;
			const bool ShowScoreboardSettings = g_Config.m_BcClientIndicatorInScoreboard != 0;
			const float NamePlateSettingsHeight = ShowNamePlateSettings ? 2.0f * LineSize : 0.0f;
			const float ScoreboardSettingsHeight = ShowScoreboardSettings ? LineSize : 0.0f;
			const float ContentHeight = LineSize + MarginSmall + 2.0f * LineSize + NamePlateSettingsHeight + ScoreboardSettingsHeight;

			CUIRect Content, Label, Row;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("Client Indicator"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcClientIndicatorInNamePlate, BCLocalize("Show indicator in name plates"), &g_Config.m_BcClientIndicatorInNamePlate, &Content, LineSize);

			if(g_Config.m_BcClientIndicatorInNamePlate)
			{
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcClientIndicatorInNamePlateAboveSelf, BCLocalize("Show above yourself"), &g_Config.m_BcClientIndicatorInNamePlateAboveSelf, &Content, LineSize);

				Content.HSplitTop(LineSize, &Row, &Content);
				Ui()->DoScrollbarOption(&g_Config.m_BcClientIndicatorInNamePlateSize, &g_Config.m_BcClientIndicatorInNamePlateSize, &Row, BCLocalize("Name plate indicator size"), -50, 100);
			}

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcClientIndicatorInScoreboard, BCLocalize("Show indicator in scoreboard"), &g_Config.m_BcClientIndicatorInScoreboard, &Content, LineSize);
			if(g_Config.m_BcClientIndicatorInScoreboard)
			{
				Content.HSplitTop(LineSize, &Row, &Content);
				Ui()->DoScrollbarOption(&g_Config.m_BcClientIndicatorInSoreboardSize, &g_Config.m_BcClientIndicatorInSoreboardSize, &Row, BCLocalize("Scoreboard indicator size"), -50, 100);
			}
		}
		const float RightColumnEndY = Column.y;
		CUIRect ScrollRegion;
		ScrollRegion.x = MainView.x;
		ScrollRegion.y = maximum(LeftColumnEndY, RightColumnEndY) + MarginSmall * 2.0f;
		ScrollRegion.w = MainView.w;
		ScrollRegion.h = 0.0f;
		s_BestClientOthersScrollRegion.AddRect(ScrollRegion);
		s_BestClientOthersScrollRegion.End();
	}
	else if(s_CurTab == BESTCLIENT_TAB_SHOP)
	{
		RenderSettingsBestClientShop(MainView);
	}
	else if(s_CurTab == BESTCLIENT_TAB_INFO)
	{
		RenderSettingsBestClientInfo(MainView);
	}

}

void CMenus::ComponentsEditorSyncFromConfig()
{
	m_ComponentsEditorState.m_AppliedMaskLo = g_Config.m_BcDisabledComponentsMaskLo;
	m_ComponentsEditorState.m_AppliedMaskHi = g_Config.m_BcDisabledComponentsMaskHi;
	m_ComponentsEditorState.m_StagedMaskLo = m_ComponentsEditorState.m_AppliedMaskLo;
	m_ComponentsEditorState.m_StagedMaskHi = m_ComponentsEditorState.m_AppliedMaskHi;
	m_ComponentsEditorState.m_HasUnsavedChanges = false;
}

void CMenus::ComponentsEditorOpen()
{
	ComponentsEditorSyncFromConfig();
	m_ComponentsEditorState.m_Open = true;
	m_ComponentsEditorState.m_FullscreenOpen = true;
	m_ComponentsEditorState.m_ShowExitConfirm = false;
	m_ComponentsEditorState.m_ShowRestartConfirm = false;
}

void CMenus::ComponentsEditorRequestClose()
{
	if(m_ComponentsEditorState.m_HasUnsavedChanges)
	{
		m_ComponentsEditorState.m_ShowExitConfirm = true;
		return;
	}
	ComponentsEditorCloseNow();
}

void CMenus::ComponentsEditorCloseNow()
{
	m_ComponentsEditorState.m_Open = false;
	m_ComponentsEditorState.m_ShowExitConfirm = false;
	m_ComponentsEditorState.m_ShowRestartConfirm = false;
	m_ComponentsEditorState.m_HasUnsavedChanges = false;
	ComponentsEditorSyncFromConfig();
}

void CMenus::ComponentsEditorApply()
{
	g_Config.m_BcDisabledComponentsMaskLo = m_ComponentsEditorState.m_StagedMaskLo;
	g_Config.m_BcDisabledComponentsMaskHi = m_ComponentsEditorState.m_StagedMaskHi;
	m_ComponentsEditorState.m_AppliedMaskLo = m_ComponentsEditorState.m_StagedMaskLo;
	m_ComponentsEditorState.m_AppliedMaskHi = m_ComponentsEditorState.m_StagedMaskHi;
	m_ComponentsEditorState.m_HasUnsavedChanges = false;
	m_ComponentsEditorState.m_ShowExitConfirm = false;
	m_ComponentsEditorState.m_ShowRestartConfirm = true;
}

void CMenus::ComponentsEditorRenderExitConfirm(const CUIRect &Rect)
{
	const float FontSize = 14.0f;
	const float LineSize = 20.0f;
	const float MarginSmall = 5.0f;

	CUIRect Overlay = Rect;
	Overlay.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.6f), IGraphics::CORNER_ALL, 0.0f);

	CUIRect Box;
	Box.w = minimum(520.0f, Rect.w - 30.0f);
	Box.h = 130.0f;
	Box.x = Rect.x + (Rect.w - Box.w) * 0.5f;
	Box.y = Rect.y + (Rect.h - Box.h) * 0.5f;
	Box.Draw(ColorRGBA(0.1f, 0.1f, 0.1f, 0.95f), IGraphics::CORNER_ALL, 8.0f);

	CUIRect Title, Message, Buttons;
	Box.Margin(10.0f, &Box);
	Box.HSplitTop(LineSize + 4.0f, &Title, &Box);
	Box.HSplitTop(LineSize, &Message, &Box);
	Box.HSplitBottom(LineSize + 4.0f, &Box, &Buttons);

	Ui()->DoLabel(&Title, BCLocalize("Cancel all changes?"), FontSize * 1.1f, TEXTALIGN_ML);
	Ui()->DoLabel(&Message, BCLocalize("All staged component changes will be lost."), FontSize, TEXTALIGN_ML);

	CUIRect YesButton, NoButton;
	Buttons.VSplitMid(&YesButton, &NoButton, MarginSmall);
	static CButtonContainer s_YesButton;
	static CButtonContainer s_NoButton;
	if(DoButton_Menu(&s_YesButton, BCLocalize("Yes"), 0, &YesButton))
		ComponentsEditorCloseNow();
	if(DoButton_Menu(&s_NoButton, BCLocalize("No"), 0, &NoButton) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
		m_ComponentsEditorState.m_ShowExitConfirm = false;
}

void CMenus::ComponentsEditorRenderRestartConfirm(const CUIRect &Rect)
{
	const float FontSize = 14.0f;
	const float LineSize = 20.0f;
	const float MarginSmall = 5.0f;

	CUIRect Overlay = Rect;
	Overlay.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.6f), IGraphics::CORNER_ALL, 0.0f);

	CUIRect Box;
	Box.w = minimum(560.0f, Rect.w - 30.0f);
	Box.h = 140.0f;
	Box.x = Rect.x + (Rect.w - Box.w) * 0.5f;
	Box.y = Rect.y + (Rect.h - Box.h) * 0.5f;
	Box.Draw(ColorRGBA(0.1f, 0.1f, 0.1f, 0.95f), IGraphics::CORNER_ALL, 8.0f);

	CUIRect Title, Message, Buttons;
	Box.Margin(10.0f, &Box);
	Box.HSplitTop(LineSize + 4.0f, &Title, &Box);
	Box.HSplitTop(LineSize * 2.0f, &Message, &Box);
	Box.HSplitBottom(LineSize + 4.0f, &Box, &Buttons);

	Ui()->DoLabel(&Title, BCLocalize("Restart"), FontSize * 1.1f, TEXTALIGN_ML);
	Ui()->DoLabel(&Message, BCLocalize("Restart client now so component changes fully apply?"), FontSize, TEXTALIGN_ML);

	CUIRect YesButton, NoButton;
	Buttons.VSplitMid(&YesButton, &NoButton, MarginSmall);
	static CButtonContainer s_RestartYesButton;
	static CButtonContainer s_RestartNoButton;
	if(DoButton_Menu(&s_RestartYesButton, BCLocalize("Yes"), 0, &YesButton))
	{
		m_ComponentsEditorState.m_ShowRestartConfirm = false;
		m_ComponentsEditorState.m_Open = false;
		if(Client()->State() == IClient::STATE_ONLINE || GameClient()->Editor()->HasUnsavedData())
			m_Popup = POPUP_RESTART;
		else
			Client()->Restart();
	}
	if(DoButton_Menu(&s_RestartNoButton, BCLocalize("No"), 0, &NoButton) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
	{
		m_ComponentsEditorState.m_ShowRestartConfirm = false;
		m_ComponentsEditorState.m_Open = false;
	}
}

void CMenus::RenderComponentsEditorScreen(CUIRect MainView)
{
	const float FontSize = 14.0f;
	const float LineSize = 20.0f;
	const float HeadlineFontSize = 20.0f;
	const float MarginSmall = 5.0f;

	if(m_ComponentsEditorState.m_FullscreenOpen)
		MainView = *Ui()->Screen();

	if(!m_ComponentsEditorState.m_ShowExitConfirm && !m_ComponentsEditorState.m_ShowRestartConfirm && Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
	{
		ComponentsEditorRequestClose();
		if(!m_ComponentsEditorState.m_Open)
			return;
	}
	if(m_ComponentsEditorState.m_ShowExitConfirm)
	{
		ComponentsEditorRenderExitConfirm(*Ui()->Screen());
		return;
	}
	if(m_ComponentsEditorState.m_ShowRestartConfirm)
	{
		ComponentsEditorRenderRestartConfirm(*Ui()->Screen());
		return;
	}

	CUIRect EditorRect = MainView;
	EditorRect.Margin(8.0f, &EditorRect);
	EditorRect.Draw(ColorRGBA(0.10f, 0.11f, 0.15f, 0.96f), IGraphics::CORNER_ALL, 8.0f);

	CUIRect WorkRect;
	EditorRect.Margin(8.0f, &WorkRect);
	CUIRect Header, Content, Footer;
	WorkRect.HSplitTop(24.0f, &Header, &WorkRect);
	WorkRect.HSplitBottom(34.0f, &Content, &Footer);

	CUIRect HeaderText = Header;
	CUIRect CloseButtonArea, HeaderSpacer;
	HeaderText.VSplitLeft(14.0f, &CloseButtonArea, &HeaderText);
	HeaderText.VSplitLeft(6.0f, &HeaderSpacer, &HeaderText);
	(void)HeaderSpacer;

	CUIRect CloseButton;
	CloseButtonArea.HMargin(5.0f, &CloseButton);

	static CButtonContainer s_CloseButton;
	if(Ui()->DoButton_FontIcon(&s_CloseButton, FontIcon::XMARK, 0, &CloseButton, IGraphics::CORNER_ALL))
	{
		ComponentsEditorRequestClose();
		if(!m_ComponentsEditorState.m_Open)
			return;
	}

	Ui()->DoLabel(&HeaderText, BCLocalize("Components editor"), HeadlineFontSize, TEXTALIGN_ML);

	static CScrollRegion s_ScrollRegion;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	s_ScrollRegion.Begin(&Content, &ScrollOffset, &ScrollParams);

	CUIRect View = Content;
	View.y += ScrollOffset.y;
	View.VSplitLeft(5.0f, nullptr, &View);
	View.VSplitRight(5.0f, &View, nullptr);

	const char *apGroupNames[NUM_COMPONENTS_GROUPS] = {
		BCLocalize("Visuals"),
		BCLocalize("Gameplay"),
		BCLocalize("Others"),
		"TClient",
	};

	for(int Group = 0; Group < NUM_COMPONENTS_GROUPS; ++Group)
	{
		int Count = 0;
		for(const auto &Entry : gs_aBestClientComponentEntries)
		{
			if(Entry.m_Group == Group)
				++Count;
		}
		if(Count == 0)
			continue;

		CUIRect GroupBox;
		const float GroupHeight = 20.0f + HeadlineFontSize + MarginSmall + Count * LineSize;
		View.HSplitTop(GroupHeight, &GroupBox, &View);
		GroupBox.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.22f), IGraphics::CORNER_ALL, 8.0f);
		GroupBox.Margin(10.0f, &GroupBox);

		CUIRect Label;
		GroupBox.HSplitTop(HeadlineFontSize, &Label, &GroupBox);
		Ui()->DoLabel(&Label, apGroupNames[Group], HeadlineFontSize, TEXTALIGN_ML);
		GroupBox.HSplitTop(MarginSmall, nullptr, &GroupBox);

		for(const auto &Entry : gs_aBestClientComponentEntries)
		{
			if(Entry.m_Group != Group)
				continue;

			CUIRect Row;
			GroupBox.HSplitTop(LineSize, &Row, &GroupBox);
			int Disabled = ComponentsEditorIsDisabled((int)Entry.m_Component, m_ComponentsEditorState.m_StagedMaskLo, m_ComponentsEditorState.m_StagedMaskHi);
			if(DoButton_CheckBox(&Entry, BCLocalize(Entry.m_pName), Disabled, &Row))
			{
				Disabled ^= 1;
				ComponentsEditorSetDisabled((int)Entry.m_Component, m_ComponentsEditorState.m_StagedMaskLo, m_ComponentsEditorState.m_StagedMaskHi, Disabled != 0);
				m_ComponentsEditorState.m_HasUnsavedChanges =
					m_ComponentsEditorState.m_StagedMaskLo != m_ComponentsEditorState.m_AppliedMaskLo ||
					m_ComponentsEditorState.m_StagedMaskHi != m_ComponentsEditorState.m_AppliedMaskHi;
			}
		}

		View.HSplitTop(16.0f, nullptr, &View);
	}

	CUIRect ScrollRegion;
	ScrollRegion.x = Content.x;
	ScrollRegion.y = View.y;
	ScrollRegion.w = Content.w;
	ScrollRegion.h = 0.0f;
	s_ScrollRegion.AddRect(ScrollRegion);
	s_ScrollRegion.End();

	CUIRect Counter, ApplyButton;
	Footer.VSplitLeft(300.0f, &Counter, &Footer);
	Footer.VSplitRight(88.0f, &Footer, &ApplyButton);

	int DisabledCount = 0;
	for(const auto &Entry : gs_aBestClientComponentEntries)
	{
		if(ComponentsEditorIsDisabled((int)Entry.m_Component, m_ComponentsEditorState.m_StagedMaskLo, m_ComponentsEditorState.m_StagedMaskHi))
			++DisabledCount;
	}

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), BCLocalize("Disabled components: %d"), DisabledCount);
	Ui()->DoLabel(&Counter, aBuf, FontSize, TEXTALIGN_ML);

	static CButtonContainer s_ApplyButton;
	const int DisabledStyle = m_ComponentsEditorState.m_HasUnsavedChanges ? 0 : -1;
	if(DoButton_Menu(&s_ApplyButton, BCLocalize("Apply"), DisabledStyle, &ApplyButton) && m_ComponentsEditorState.m_HasUnsavedChanges)
		ComponentsEditorApply();
}

void CMenus::RenderSettingsBestClientInfo(CUIRect MainView)
{
	enum
	{
		BESTCLIENT_TAB_VISUALS = 0,
		BESTCLIENT_TAB_GAMEPLAY,
		BESTCLIENT_TAB_OTHERS,
		BESTCLIENT_TAB_FUN,
		BESTCLIENT_TAB_SHOP,
		BESTCLIENT_TAB_EDITORS,
		BESTCLIENT_TAB_INFO,
		NUM_BESTCLIENT_TABS,
	};

	const float LineSize = 20.0f;
	const float MarginSmall = 5.0f;
	const float MarginBetweenViews = 30.0f;
	const float HeadlineFontSize = 20.0f;
	const float HeadlineHeight = HeadlineFontSize;

	CUIRect LeftView, RightView, Button, Label, LowerLeftView;
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);
	LeftView.HSplitMid(&LeftView, &LowerLeftView, 0.0f);

	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, BCLocalize("BestClient Links"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	static CButtonContainer s_DiscordButton, s_WebsiteButton, s_TelegramButton, s_CheckUpdateButton;
	CUIRect ButtonLeft, ButtonRight;

	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&ButtonLeft, &ButtonRight, MarginSmall);
	if(DoButtonLineSize_Menu(&s_DiscordButton, BCLocalize("Discord"), 0, &ButtonLeft, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://discord.gg/bestclient");
	if(DoButtonLineSize_Menu(&s_TelegramButton, BCLocalize("Telegram"), 0, &ButtonRight, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://t.me/bestddnet");

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&ButtonLeft, &ButtonRight, MarginSmall);

	if(DoButtonLineSize_Menu(&s_WebsiteButton, BCLocalize("Website"), 0, &ButtonLeft, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://bestclient.fun");
	if(DoButtonLineSize_Menu(&s_CheckUpdateButton, BCLocalize("Check update"), 0, &ButtonRight, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		GameClient()->m_BestClient.FetchBestClientInfo();

#if defined(CONF_AUTOUPDATE)
	const bool NeedUpdate = GameClient()->m_BestClient.NeedUpdate();
	const IUpdater::EUpdaterState UpdateState = Updater()->GetCurrentState();
	const bool ShowDownloadButton = NeedUpdate && UpdateState == IUpdater::CLEAN;
	const bool ShowRetryButton = NeedUpdate && UpdateState == IUpdater::FAIL;
	const bool ShowRestartButton = UpdateState == IUpdater::NEED_RESTART;
	const bool ShowUpdateProgress = UpdateState >= IUpdater::GETTING_MANIFEST && UpdateState < IUpdater::NEED_RESTART;
	if(ShowDownloadButton || ShowRetryButton || ShowRestartButton || ShowUpdateProgress || UpdateState == IUpdater::FAIL)
	{
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
		LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
		Button.VSplitMid(&ButtonLeft, &ButtonRight, MarginSmall);

		char aUpdateLabel[128] = "";
		if(ShowDownloadButton)
			str_format(aUpdateLabel, sizeof(aUpdateLabel), BCLocalize("BestClient %s is available"), GameClient()->m_BestClient.m_aVersionStr);
		else if(ShowUpdateProgress)
		{
			if(UpdateState == IUpdater::GETTING_MANIFEST)
				str_copy(aUpdateLabel, BCLocalize("Preparing update..."), sizeof(aUpdateLabel));
			else
				str_format(aUpdateLabel, sizeof(aUpdateLabel), "%s %d%%", BCLocalize("Downloading"), Updater()->GetCurrentPercent());
		}
		else if(ShowRestartButton)
			str_copy(aUpdateLabel, BCLocalize("Update downloaded"), sizeof(aUpdateLabel));
		else
			str_copy(aUpdateLabel, BCLocalize("Update failed"), sizeof(aUpdateLabel));

		if(ShowDownloadButton)
			TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
		Ui()->DoLabel(&ButtonLeft, aUpdateLabel, HeadlineFontSize / 1.6f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		if(ShowDownloadButton || ShowRetryButton)
		{
			static CButtonContainer s_DownloadUpdateButton;
			if(DoButtonLineSize_Menu(&s_DownloadUpdateButton, BCLocalize("Download"), 0, &ButtonRight, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
				Updater()->InitiateUpdate();
		}
		else if(ShowRestartButton)
		{
			static CButtonContainer s_RestartUpdateButton;
			if(DoButtonLineSize_Menu(&s_RestartUpdateButton, BCLocalize("Restart"), 0, &ButtonRight, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
				Updater()->ApplyUpdateAndRestart();
		}
		else if(ShowUpdateProgress)
		{
			Ui()->RenderProgressBar(ButtonRight, Updater()->GetCurrentPercent() / 100.0f);
		}
	}
#endif

	LeftView = LowerLeftView;
	LeftView.HSplitBottom(LineSize * 2.0f + MarginSmall * 2.0f + HeadlineFontSize, nullptr, &LeftView);
	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, BCLocalize("Config Files"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	char aBuf[128 + IO_MAX_PATH_LENGTH];
	CUIRect BestClientConfig;
	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	BestClientConfig = Button;

	static CButtonContainer s_Config;
	if(DoButtonLineSize_Menu(&s_Config, BCLocalize("BestClient Settings"), 0, &BestClientConfig, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::BESTCLIENT].m_aConfigPath, aBuf, sizeof(aBuf));
		Client()->ViewFile(aBuf);
	}

	RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	Ui()->DoLabel(&Label, BCLocalize("BestClient Developers"), HeadlineFontSize, TEXTALIGN_ML);
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
		RenderDevSkin(TeeRect.Center(), TeeSize, "mushkitt", "mushkitt", false, 0, 0, 0, false, true);
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

	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	Ui()->DoLabel(&Label, BCLocalize("Hide Settings Tabs"), HeadlineFontSize, TEXTALIGN_ML);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	CUIRect LeftSettings, RightSettings;
	RightView.VSplitMid(&LeftSettings, &RightSettings, MarginSmall);

	const char *apTabNames[] = {
		BCLocalize("Visuals"),
		BCLocalize("Gameplay"),
		BCLocalize("Others"),
		BCLocalize("Fun"),
		BCLocalize("Shop"),
		BCLocalize("Editors"),
		BCLocalize("Info"),
	};
	const int aTabOrder[NUM_BESTCLIENT_TABS] = {
		BESTCLIENT_TAB_VISUALS,
		BESTCLIENT_TAB_GAMEPLAY,
		BESTCLIENT_TAB_OTHERS,
		BESTCLIENT_TAB_EDITORS,
		BESTCLIENT_TAB_FUN,
		BESTCLIENT_TAB_SHOP,
		BESTCLIENT_TAB_INFO,
	};

	static CButtonContainer s_aShowTabButtons[NUM_BESTCLIENT_TABS] = {};
	int HideableTabCount = 0;
	int HideableVisibleIndex = 0;
	for(int TabIndex = 0; TabIndex < NUM_BESTCLIENT_TABS; ++TabIndex)
	{
		const int Tab = aTabOrder[TabIndex];
		// Keep Info visible the same way as in legacy BestClient.
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
