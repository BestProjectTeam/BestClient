/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus.h"

#include <base/dbg.h>
#include <base/math.h>
#include <base/str.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/components/menu_background.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>

void CMenus::SetNeedSendInfo()
{
	if(m_Dummy)
		m_NeedSendDummyinfo = true;
	else
		m_NeedSendinfo = true;
}

void CMenus::RenderSettings(CUIRect MainView)
{
	if(g_Config.m_UiSettingsPage < 0 || g_Config.m_UiSettingsPage >= SETTINGS_LENGTH)
		g_Config.m_UiSettingsPage = SETTINGS_GENERAL;
	if(g_Config.m_UiSettingsPage == SETTINGS_LANGUAGE)
		g_Config.m_UiSettingsPage = SETTINGS_GENERAL;
	if(g_Config.m_UiSettingsPage == SETTINGS_PLAYER)
		g_Config.m_UiSettingsPage = SETTINGS_TEE;

	// Must short-circuit here, before any of the root/sub tab bar buttons below are given a
	// chance to run their click logic this frame - otherwise clicks on the fullscreen editor
	// can also land on whatever tab button used to be underneath it.
	if(m_GifWheelEditorOpen)
	{
		RenderSettingsBestClientGifWheelFullscreen(*Ui()->Screen());
		return;
	}
	if(m_AssetsEditorState.m_VisualsEditorOpen && m_AssetsEditorState.m_FullscreenOpen)
	{
		RenderAssetsEditorScreen(*Ui()->Screen());
		return;
	}

	g_Config.m_BcSettingsLayout = minimum(maximum(g_Config.m_BcSettingsLayout, 0), 1);

	if(g_Config.m_BcSettingsLayout == 0)
	{
		const bool NeedRestart = m_NeedRestartGraphics || m_NeedRestartSound || m_NeedRestartUpdate;

		auto RenderSettingsPage = [&](CUIRect PageView) {
			if(g_Config.m_UiSettingsPage == SETTINGS_GENERAL)
			{
				GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_GENERAL);
				RenderSettingsGeneral(PageView);
			}
			else if(g_Config.m_UiSettingsPage == SETTINGS_TEE)
			{
				GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_TEE);
				if(Client()->IsSixup())
					RenderSettingsTee7(PageView);
				else
					RenderSettingsTee(PageView);
			}
			else if(g_Config.m_UiSettingsPage == SETTINGS_APPEARANCE)
			{
				GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_APPEARANCE);
				RenderSettingsAppearance(PageView);
			}
			else if(g_Config.m_UiSettingsPage == SETTINGS_CONTROLS)
			{
				GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_CONTROLS);
				m_MenusSettingsControls.Render(PageView);
			}
			else if(g_Config.m_UiSettingsPage == SETTINGS_GRAPHICS)
			{
				GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_GRAPHICS);
				RenderSettingsGraphics(PageView);
			}
			else if(g_Config.m_UiSettingsPage == SETTINGS_SOUND)
			{
				GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_SOUND);
				RenderSettingsSound(PageView);
			}
			else if(g_Config.m_UiSettingsPage == SETTINGS_DDNET)
			{
				GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_DDNET);
				RenderSettingsDDNet(PageView);
			}
			else if(g_Config.m_UiSettingsPage == SETTINGS_ASSETS)
			{
				GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_ASSETS);
				RenderSettingsAssets(PageView);
			}
			else if(g_Config.m_UiSettingsPage == SETTINGS_TCLIENT)
			{
				GameClient()->m_MenuBackground.ChangePosition(13);
				RenderSettingsTClient(PageView);
			}
			else if(g_Config.m_UiSettingsPage == SETTINGS_PROFILES)
			{
				GameClient()->m_MenuBackground.ChangePosition(14);
				RenderSettingsTClientProfiles(PageView);
			}
			else if(g_Config.m_UiSettingsPage == SETTINGS_CONFIGS)
			{
				GameClient()->m_MenuBackground.ChangePosition(15);
				RenderSettingsTClientConfigs(PageView);
			}
			else if(g_Config.m_UiSettingsPage == SETTINGS_CREDITS)
			{
				GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_CREDITS);
				RenderSettingsCredits(PageView);
			}
			else if(g_Config.m_UiSettingsPage == SETTINGS_BESTCLIENT)
			{
				GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_RESERVED0);
				RenderSettingsBestClient(PageView);
			}
			else
			{
				dbg_assert_failed("ui_settings_page invalid");
			}
		};

		auto RenderRestartWarning = [&](CUIRect RestartBar) {
			CUIRect RestartWarning, RestartButton;
			RestartBar.VSplitRight(125.0f, &RestartWarning, &RestartButton);
			RestartWarning.VSplitRight(10.0f, &RestartWarning, nullptr);
			if(m_NeedRestartUpdate)
			{
				TextRender()->TextColor(0.7f, 1.0f, 0.7f, 1.0f);
				Ui()->DoLabel(&RestartWarning, Localize("BestClient update downloaded! Restart to apply."), 14.0f, TEXTALIGN_ML);
				TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
			}
			else
			{
				Ui()->DoLabel(&RestartWarning, Localize("You must restart the game for all settings to take effect."), 14.0f, TEXTALIGN_ML);
			}

			static CButtonContainer s_RestartButton;
			if(DoButton_Menu(&s_RestartButton, Localize("Restart"), 0, &RestartButton))
			{
#if defined(CONF_AUTOUPDATE)
				if(m_NeedRestartUpdate)
				{
					Updater()->ApplyUpdateAndRestart();
				}
				else
#endif
				if(Client()->State() == IClient::STATE_ONLINE || GameClient()->Editor()->HasUnsavedData())
				{
					m_Popup = POPUP_RESTART;
				}
				else
				{
					Client()->Restart();
				}
			}
		};

		auto RenderSettingsPageNewLayout = [&](CUIRect PageView) {
			const int Page = g_Config.m_UiSettingsPage;
			const bool NeedsAutoScroll = Page == SETTINGS_GENERAL || Page == SETTINGS_APPEARANCE;

			if(!NeedsAutoScroll)
			{
				RenderSettingsPage(PageView);
				return;
			}

			static CScrollRegion s_aNewLayoutScrollRegions[SETTINGS_LENGTH];
			CScrollRegionParams ScrollParams;
			ScrollParams.m_ScrollUnit = 60.0f;
			ScrollParams.m_ScrollbarMargin = 5.0f;

			vec2 ScrollOffset(0.0f, 0.0f);
			CScrollRegion &ScrollRegion = s_aNewLayoutScrollRegions[Page];
			ScrollRegion.Begin(&PageView, &ScrollParams);

			CUIRect ContentView = PageView;
			const float ContentStartY = ContentView.y;
			const float VirtualHeightBoost = Page == SETTINGS_GENERAL ? 120.0f : 96.0f;
			ContentView.h = PageView.h + VirtualHeightBoost;

			RenderSettingsPage(ContentView);

			CUIRect ContentRect = ContentView;
			ContentRect.y = ContentStartY;
			ContentRect.h = ContentView.h;
			ScrollRegion.AddRect(ContentRect);
			ScrollRegion.End();
		};

		CUIRect ContentView, RootTabBar, RestartBar;
		MainView.Draw(ms_ColorTabbarActive, IGraphics::CORNER_B, 10.0f);
		MainView.Margin(20.0f, &MainView);
		ContentView = MainView;
		if(NeedRestart)
		{
			ContentView.HSplitBottom(20.0f, &ContentView, &RestartBar);
			ContentView.HSplitBottom(10.0f, &ContentView, nullptr);
		}

		enum
		{
			ROOT_TAB_GENERAL = 0,
			ROOT_TAB_APPEARANCE,
			ROOT_TAB_TCLIENT,
			ROOT_TAB_BESTCLIENT,
			ROOT_TAB_LENGTH,
		};

		auto GetRootTabByPage = [&](int Page) {
			if(Page == SETTINGS_GENERAL || Page == SETTINGS_CONTROLS || Page == SETTINGS_TEE || Page == SETTINGS_DDNET)
				return ROOT_TAB_GENERAL;
			if(Page == SETTINGS_APPEARANCE || Page == SETTINGS_GRAPHICS || Page == SETTINGS_SOUND || Page == SETTINGS_ASSETS)
				return ROOT_TAB_APPEARANCE;
			if(Page == SETTINGS_TCLIENT || Page == SETTINGS_PROFILES || Page == SETTINGS_CONFIGS)
				return ROOT_TAB_TCLIENT;
			return ROOT_TAB_BESTCLIENT;
		};

		ContentView.HSplitTop(36.0f, &RootTabBar, &ContentView);
		RootTabBar.HMargin(6.0f, &RootTabBar);

		const char *apRootTabs[ROOT_TAB_LENGTH] = {
			Localize("General"),
			Localize("Appearance"),
			TCLocalize("TClient"),
			Localize("BestClient"),
		};
		static CButtonContainer s_aRootTabButtons[ROOT_TAB_LENGTH];
		const int CurRootTab = GetRootTabByPage(g_Config.m_UiSettingsPage);
		const float RootTabWidth = RootTabBar.w / (float)ROOT_TAB_LENGTH;
		CUIRect RootTabs = RootTabBar;
		for(int i = 0; i < ROOT_TAB_LENGTH; ++i)
		{
			CUIRect RootTabButton;
			RootTabs.VSplitLeft(RootTabWidth, &RootTabButton, &RootTabs);
			const int Corners = i == 0 ? (IGraphics::CORNER_TL | IGraphics::CORNER_BL) : (i == ROOT_TAB_LENGTH - 1 ? (IGraphics::CORNER_TR | IGraphics::CORNER_BR) : IGraphics::CORNER_NONE);
			if(DoButton_MenuTab(&s_aRootTabButtons[i], apRootTabs[i], CurRootTab == i, &RootTabButton, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
			{
				if(i == ROOT_TAB_GENERAL)
					g_Config.m_UiSettingsPage = SETTINGS_GENERAL;
				else if(i == ROOT_TAB_APPEARANCE)
					g_Config.m_UiSettingsPage = SETTINGS_APPEARANCE;
				else if(i == ROOT_TAB_TCLIENT)
					g_Config.m_UiSettingsPage = SETTINGS_TCLIENT;
				else
					g_Config.m_UiSettingsPage = SETTINGS_BESTCLIENT;
			}
		}

		const int ActiveRootTab = GetRootTabByPage(g_Config.m_UiSettingsPage);
		ContentView.HSplitTop(6.0f, nullptr, &ContentView);

		if(ActiveRootTab != ROOT_TAB_BESTCLIENT)
		{
			CUIRect SubTabBar;
			ContentView.HSplitTop(24.0f, &SubTabBar, &ContentView);

			const char *apSubTabs[4] = {};
			int aSubTabPages[4] = {};
			int NumSubTabs = 0;
			if(ActiveRootTab == ROOT_TAB_GENERAL)
			{
				apSubTabs[0] = Localize("General");
				apSubTabs[1] = Localize("Controls");
				apSubTabs[2] = Client()->IsSixup() ? "Tee (0.7)" : Localize("Tee");
				apSubTabs[3] = Localize("DDNet");
				aSubTabPages[0] = SETTINGS_GENERAL;
				aSubTabPages[1] = SETTINGS_CONTROLS;
				aSubTabPages[2] = SETTINGS_TEE;
				aSubTabPages[3] = SETTINGS_DDNET;
				NumSubTabs = 4;
			}
			else if(ActiveRootTab == ROOT_TAB_APPEARANCE)
			{
				apSubTabs[0] = Localize("Appearance");
				apSubTabs[1] = Localize("Graphics");
				apSubTabs[2] = Localize("Sound");
				apSubTabs[3] = Localize("Assets");
				aSubTabPages[0] = SETTINGS_APPEARANCE;
				aSubTabPages[1] = SETTINGS_GRAPHICS;
				aSubTabPages[2] = SETTINGS_SOUND;
				aSubTabPages[3] = SETTINGS_ASSETS;
				NumSubTabs = 4;
			}
			else if(ActiveRootTab == ROOT_TAB_TCLIENT)
			{
				apSubTabs[0] = TCLocalize("TClient");
				apSubTabs[1] = Localize("Profiles");
				apSubTabs[2] = Localize("Configs");
				aSubTabPages[0] = SETTINGS_TCLIENT;
				aSubTabPages[1] = SETTINGS_PROFILES;
				aSubTabPages[2] = SETTINGS_CONFIGS;
				NumSubTabs = 3;
			}

			static CButtonContainer s_aGeneralSubTabButtons[4];
			static CButtonContainer s_aAppearanceSubTabButtons[4];
			static CButtonContainer s_aTClientSubTabButtons[3];
			CButtonContainer *pSubButtons = ActiveRootTab == ROOT_TAB_GENERAL ? s_aGeneralSubTabButtons : (ActiveRootTab == ROOT_TAB_APPEARANCE ? s_aAppearanceSubTabButtons : s_aTClientSubTabButtons);

			CUIRect SubTabs = SubTabBar;
			const float SubTabWidth = SubTabBar.w / (float)NumSubTabs;
			for(int i = 0; i < NumSubTabs; ++i)
			{
				CUIRect SubButton;
				SubTabs.VSplitLeft(SubTabWidth, &SubButton, &SubTabs);
				const int Corners = i == 0 ? (IGraphics::CORNER_TL | IGraphics::CORNER_BL) : (i == NumSubTabs - 1 ? (IGraphics::CORNER_TR | IGraphics::CORNER_BR) : IGraphics::CORNER_NONE);
				if(DoButton_MenuTab(&pSubButtons[i], apSubTabs[i], g_Config.m_UiSettingsPage == aSubTabPages[i], &SubButton, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
					g_Config.m_UiSettingsPage = aSubTabPages[i];
			}

			ContentView.HSplitTop(10.0f, nullptr, &ContentView);
		}

		RenderSettingsPageNewLayout(ContentView);
		if(NeedRestart)
			RenderRestartWarning(RestartBar);
		return;
	}

	// render background
	CUIRect Button, TabBar, RestartBar;
	MainView.VSplitRight(120.0f, &MainView, &TabBar);
	MainView.Draw(ms_ColorTabbarActive, IGraphics::CORNER_B, 10.0f);
	MainView.Margin(20.0f, &MainView);

	const bool NeedRestart = m_NeedRestartGraphics || m_NeedRestartSound || m_NeedRestartUpdate;
	if(NeedRestart)
	{
		MainView.HSplitBottom(20.0f, &MainView, &RestartBar);
		MainView.HSplitBottom(10.0f, &MainView, nullptr);
	}

	TabBar.HSplitTop(50.0f, &Button, &TabBar);
	Button.Draw(ms_ColorTabbarActive, IGraphics::CORNER_BR, 10.0f);

	const char *apTabs[SETTINGS_LENGTH] = {
		Localize("Language"),
		Localize("General"),
		Localize("Player"),
		Client()->IsSixup() ? "Tee 0.7" : Localize("Tee"),
		Localize("Appearance"),
		Localize("Controls"),
		Localize("Graphics"),
		Localize("Sound"),
		Localize("DDNet"),
		Localize("Assets"),
		TCLocalize("TClient"),
		Localize("BestClient"),
		Localize("Profiles"),
		Localize("Configs"),
		Localize("Credits")};

	if(g_Config.m_UiSettingsPage == SETTINGS_LANGUAGE)
		g_Config.m_UiSettingsPage = SETTINGS_GENERAL;
	if(g_Config.m_UiSettingsPage == SETTINGS_PLAYER)
		g_Config.m_UiSettingsPage = SETTINGS_TEE;

	static CButtonContainer s_aTabButtons[SETTINGS_LENGTH];

	for(int i = 0; i < SETTINGS_LENGTH; i++)
	{
		if(i == SETTINGS_LANGUAGE || i == SETTINGS_PLAYER)
			continue;
		TabBar.HSplitTop(10.0f, nullptr, &TabBar);
		TabBar.HSplitTop(26.0f, &Button, &TabBar);
		if(DoButton_MenuTab(&s_aTabButtons[i], apTabs[i], g_Config.m_UiSettingsPage == i, &Button, IGraphics::CORNER_R, &m_aAnimatorsSettingsTab[i]))
			g_Config.m_UiSettingsPage = i;
	}

	if(g_Config.m_UiSettingsPage == SETTINGS_LANGUAGE)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_LANGUAGE);
		RenderLanguageSettings(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_GENERAL)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_GENERAL);
		RenderSettingsGeneral(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_PLAYER)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_PLAYER);
		RenderSettingsPlayer(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_TEE)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_TEE);
		if(Client()->IsSixup())
			RenderSettingsTee7(MainView);
		else
			RenderSettingsTee(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_APPEARANCE)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_APPEARANCE);
		RenderSettingsAppearance(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_CONTROLS)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_CONTROLS);
		m_MenusSettingsControls.Render(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_GRAPHICS)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_GRAPHICS);
		RenderSettingsGraphics(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_SOUND)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_SOUND);
		RenderSettingsSound(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_DDNET)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_DDNET);
		RenderSettingsDDNet(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_ASSETS)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_ASSETS);
		RenderSettingsAssets(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_TCLIENT)
	{
		GameClient()->m_MenuBackground.ChangePosition(13);
		RenderSettingsTClient(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_PROFILES)
	{
		GameClient()->m_MenuBackground.ChangePosition(14);
		RenderSettingsTClientProfiles(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_CONFIGS)
	{
		GameClient()->m_MenuBackground.ChangePosition(15);
		RenderSettingsTClientConfigs(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_CREDITS)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_CREDITS);
		RenderSettingsCredits(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_BESTCLIENT)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_RESERVED0);
		RenderSettingsBestClient(MainView);
	}
	else
	{
		dbg_assert_failed("ui_settings_page invalid");
	}

	if(NeedRestart)
	{
		CUIRect RestartWarning, RestartButton;
		RestartBar.VSplitRight(125.0f, &RestartWarning, &RestartButton);
		RestartWarning.VSplitRight(10.0f, &RestartWarning, nullptr);
		if(m_NeedRestartUpdate)
		{
			TextRender()->TextColor(0.7f, 1.0f, 0.7f, 1.0f);
			Ui()->DoLabel(&RestartWarning, Localize("BestClient update downloaded! Restart to apply."), 14.0f, TEXTALIGN_ML);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		}
		else
		{
			Ui()->DoLabel(&RestartWarning, Localize("You must restart the game for all settings to take effect."), 14.0f, TEXTALIGN_ML);
		}

		static CButtonContainer s_RestartButton;
		if(DoButton_Menu(&s_RestartButton, Localize("Restart"), 0, &RestartButton))
		{
#if defined(CONF_AUTOUPDATE)
			if(m_NeedRestartUpdate)
			{
				Updater()->ApplyUpdateAndRestart();
			}
			else
#endif
			if(Client()->State() == IClient::STATE_ONLINE || GameClient()->Editor()->HasUnsavedData())
			{
				m_Popup = POPUP_RESTART;
			}
			else
			{
				Client()->Restart();
			}
		}
	}
}

CUi::EPopupMenuFunctionResult CMenus::PopupSettingsCountrySelection(void *pContext, CUIRect View, bool Active)
{
	SPopupSettingsCountrySelectionContext *pPopupContext = static_cast<SPopupSettingsCountrySelectionContext *>(pContext);
	CMenus *pMenus = pPopupContext->m_pMenus;

	static CListBox s_ListBox;
	s_ListBox.SetActive(Active);
	s_ListBox.DoStart(50.0f, pMenus->GameClient()->m_CountryFlags.Num(), 8, 1, -1, &View, false);

	if(pPopupContext->m_New)
	{
		pPopupContext->m_New = false;
		s_ListBox.ScrollToSelected();
	}

	for(size_t i = 0; i < pMenus->GameClient()->m_CountryFlags.Num(); ++i)
	{
		const CCountryFlags::CCountryFlag &Entry = pMenus->GameClient()->m_CountryFlags.GetByIndex(i);

		const CListboxItem Item = s_ListBox.DoNextItem(&Entry, Entry.m_CountryCode == pPopupContext->m_Selection);
		if(!Item.m_Visible)
			continue;

		CUIRect FlagRect, Label;
		Item.m_Rect.Margin(5.0f, &FlagRect);
		FlagRect.HSplitBottom(12.0f, &FlagRect, &Label);
		Label.HSplitTop(2.0f, nullptr, &Label);
		const float OldWidth = FlagRect.w;
		FlagRect.w = FlagRect.h * 2.0f;
		FlagRect.x += (OldWidth - FlagRect.w) / 2.0f;
		pMenus->GameClient()->m_CountryFlags.Render(Entry.m_CountryCode, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), FlagRect.x, FlagRect.y, FlagRect.w, FlagRect.h);

		pMenus->Ui()->DoLabel(&Label, Entry.m_aCountryCodeString, 10.0f, TEXTALIGN_MC);
	}

	const int NewSelected = s_ListBox.DoEnd();
	pPopupContext->m_Selection = NewSelected >= 0 ? pMenus->GameClient()->m_CountryFlags.GetByIndex(NewSelected).m_CountryCode : -1;
	if(s_ListBox.WasItemSelected() || s_ListBox.WasItemActivated())
	{
		if(pPopupContext->m_pCountry != nullptr)
		{
			*pPopupContext->m_pCountry = pPopupContext->m_Selection;
			pMenus->SetNeedSendInfo();
		}
		return CUi::POPUP_CLOSE_CURRENT;
	}

	return CUi::POPUP_KEEP_OPEN;
}

bool CMenus::RenderHslaScrollbars(CUIRect *pRect, unsigned int *pColor, bool Alpha, float DarkestLight)
{
	const unsigned PrevPackedColor = *pColor;
	ColorHSLA Color(*pColor, Alpha);
	const ColorHSLA OriginalColor = Color;
	const char *apLabels[] = {Localize("Hue"), Localize("Sat."), Localize("Lht."), Localize("Alpha")};
	const float SizePerEntry = 20.0f;
	const float MarginPerEntry = 5.0f;
	const float PreviewMargin = 2.5f;
	const float PreviewHeight = 40.0f + 2 * PreviewMargin;
	const float OffY = (SizePerEntry + MarginPerEntry) * (3 + (Alpha ? 1 : 0)) - PreviewHeight;

	CUIRect Preview;
	pRect->VSplitLeft(PreviewHeight, &Preview, pRect);
	Preview.HSplitTop(OffY / 2.0f, nullptr, &Preview);
	Preview.HSplitTop(PreviewHeight, &Preview, nullptr);

	Preview.Draw(ColorRGBA(0.15f, 0.15f, 0.15f, 1.0f), IGraphics::CORNER_ALL, 4.0f + PreviewMargin);
	Preview.Margin(PreviewMargin, &Preview);
	Preview.Draw(color_cast<ColorRGBA>(Color.UnclampLighting(DarkestLight)), IGraphics::CORNER_ALL, 4.0f + PreviewMargin);

	auto &&RenderHueRect = [&](CUIRect *pColorRect) {
		float CurXOff = pColorRect->x;
		const float SizeColor = pColorRect->w / 6;

		// red to yellow
		{
			IGraphics::CColorVertex aColorVertices[] = {
				IGraphics::CColorVertex(0, 1, 0, 0, 1),
				IGraphics::CColorVertex(1, 1, 1, 0, 1),
				IGraphics::CColorVertex(2, 1, 0, 0, 1),
				IGraphics::CColorVertex(3, 1, 1, 0, 1)};
			Graphics()->SetColorVertex(aColorVertices, std::size(aColorVertices));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		// yellow to green
		CurXOff += SizeColor;
		{
			IGraphics::CColorVertex aColorVertices[] = {
				IGraphics::CColorVertex(0, 1, 1, 0, 1),
				IGraphics::CColorVertex(1, 0, 1, 0, 1),
				IGraphics::CColorVertex(2, 1, 1, 0, 1),
				IGraphics::CColorVertex(3, 0, 1, 0, 1)};
			Graphics()->SetColorVertex(aColorVertices, std::size(aColorVertices));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		CurXOff += SizeColor;
		// green to turquoise
		{
			IGraphics::CColorVertex aColorVertices[] = {
				IGraphics::CColorVertex(0, 0, 1, 0, 1),
				IGraphics::CColorVertex(1, 0, 1, 1, 1),
				IGraphics::CColorVertex(2, 0, 1, 0, 1),
				IGraphics::CColorVertex(3, 0, 1, 1, 1)};
			Graphics()->SetColorVertex(aColorVertices, std::size(aColorVertices));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		CurXOff += SizeColor;
		// turquoise to blue
		{
			IGraphics::CColorVertex aColorVertices[] = {
				IGraphics::CColorVertex(0, 0, 1, 1, 1),
				IGraphics::CColorVertex(1, 0, 0, 1, 1),
				IGraphics::CColorVertex(2, 0, 1, 1, 1),
				IGraphics::CColorVertex(3, 0, 0, 1, 1)};
			Graphics()->SetColorVertex(aColorVertices, std::size(aColorVertices));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		CurXOff += SizeColor;
		// blue to purple
		{
			IGraphics::CColorVertex aColorVertices[] = {
				IGraphics::CColorVertex(0, 0, 0, 1, 1),
				IGraphics::CColorVertex(1, 1, 0, 1, 1),
				IGraphics::CColorVertex(2, 0, 0, 1, 1),
				IGraphics::CColorVertex(3, 1, 0, 1, 1)};
			Graphics()->SetColorVertex(aColorVertices, std::size(aColorVertices));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		CurXOff += SizeColor;
		// purple to red
		{
			IGraphics::CColorVertex aColorVertices[] = {
				IGraphics::CColorVertex(0, 1, 0, 1, 1),
				IGraphics::CColorVertex(1, 1, 0, 0, 1),
				IGraphics::CColorVertex(2, 1, 0, 1, 1),
				IGraphics::CColorVertex(3, 1, 0, 0, 1)};
			Graphics()->SetColorVertex(aColorVertices, std::size(aColorVertices));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}
	};

	auto &&RenderSaturationRect = [&](CUIRect *pColorRect, const ColorRGBA &CurColor) {
		ColorHSLA LeftColor = color_cast<ColorHSLA>(CurColor);
		ColorHSLA RightColor = color_cast<ColorHSLA>(CurColor);

		LeftColor.s = 0.0f;
		RightColor.s = 1.0f;

		const ColorRGBA LeftColorRGBA = color_cast<ColorRGBA>(LeftColor);
		const ColorRGBA RightColorRGBA = color_cast<ColorRGBA>(RightColor);

		Graphics()->SetColor4(LeftColorRGBA, RightColorRGBA, RightColorRGBA, LeftColorRGBA);

		IGraphics::CFreeformItem Freeform(
			pColorRect->x, pColorRect->y,
			pColorRect->x + pColorRect->w, pColorRect->y,
			pColorRect->x, pColorRect->y + pColorRect->h,
			pColorRect->x + pColorRect->w, pColorRect->y + pColorRect->h);
		Graphics()->QuadsDrawFreeform(&Freeform, 1);
	};

	auto &&RenderLightingRect = [&](CUIRect *pColorRect, const ColorRGBA &CurColor) {
		ColorHSLA LeftColor = color_cast<ColorHSLA>(CurColor);
		ColorHSLA RightColor = color_cast<ColorHSLA>(CurColor);

		LeftColor.l = DarkestLight;
		RightColor.l = 1.0f;

		const ColorRGBA LeftColorRGBA = color_cast<ColorRGBA>(LeftColor);
		const ColorRGBA RightColorRGBA = color_cast<ColorRGBA>(RightColor);

		Graphics()->SetColor4(LeftColorRGBA, RightColorRGBA, RightColorRGBA, LeftColorRGBA);

		IGraphics::CFreeformItem Freeform(
			pColorRect->x, pColorRect->y,
			pColorRect->x + pColorRect->w, pColorRect->y,
			pColorRect->x, pColorRect->y + pColorRect->h,
			pColorRect->x + pColorRect->w, pColorRect->y + pColorRect->h);
		Graphics()->QuadsDrawFreeform(&Freeform, 1);
	};

	auto &&RenderAlphaRect = [&](CUIRect *pColorRect, const ColorRGBA &CurColorFull) {
		const ColorRGBA LeftColorRGBA = color_cast<ColorRGBA>(color_cast<ColorHSLA>(CurColorFull).WithAlpha(0.0f));
		const ColorRGBA RightColorRGBA = color_cast<ColorRGBA>(color_cast<ColorHSLA>(CurColorFull).WithAlpha(1.0f));

		Graphics()->SetColor4(LeftColorRGBA, RightColorRGBA, RightColorRGBA, LeftColorRGBA);

		IGraphics::CFreeformItem Freeform(
			pColorRect->x, pColorRect->y,
			pColorRect->x + pColorRect->w, pColorRect->y,
			pColorRect->x, pColorRect->y + pColorRect->h,
			pColorRect->x + pColorRect->w, pColorRect->y + pColorRect->h);
		Graphics()->QuadsDrawFreeform(&Freeform, 1);
	};

	for(int i = 0; i < 3 + Alpha; i++)
	{
		CUIRect Button, Label;
		pRect->HSplitTop(SizePerEntry, &Button, pRect);
		pRect->HSplitTop(MarginPerEntry, nullptr, pRect);
		Button.VSplitLeft(140.0f, &Label, &Button);
		Label.VMargin(10.0f, &Label);

		Button.Draw(ColorRGBA(0.15f, 0.15f, 0.15f, 1.0f), IGraphics::CORNER_ALL, 1.0f);

		CUIRect Rail;
		Button.Margin(2.0f, &Rail);

		char aBuf[32];

		// Hue
		if(i == 0)
			str_format(aBuf, sizeof(aBuf), "%s: %.1f° (%03d)", apLabels[i], Color[i] * 360.0f, round_to_int(Color[i] * 255.0f));
		// Lht
		else if(i == 2)
		{
			// handle internal light clamping, see `UnclampLighting`
			float Lht = DarkestLight + Color[i] * (1.0f - DarkestLight);
			str_format(aBuf, sizeof(aBuf), "%s: %.1f%% (%03d)", apLabels[i], Lht * 100.0f, round_to_int(Color[i] * 255.0f));
		}
		// Sat and Alpha
		else
			str_format(aBuf, sizeof(aBuf), "%s: %.1f%% (%03d)", apLabels[i], Color[i] * 100.0f, round_to_int(Color[i] * 255.0f));
		Ui()->DoLabel(&Label, aBuf, 12.0f, TEXTALIGN_ML);

		ColorRGBA HandleColor;
		Graphics()->TextureClear();
		Graphics()->TrianglesBegin();
		if(i == 0)
		{
			RenderHueRect(&Rail);
			HandleColor = color_cast<ColorRGBA>(ColorHSLA(Color.h, 1.0f, 0.5f, 1.0f));
		}
		else if(i == 1)
		{
			RenderSaturationRect(&Rail, color_cast<ColorRGBA>(ColorHSLA(Color.h, 1.0f, 0.5f, 1.0f)));
			HandleColor = color_cast<ColorRGBA>(ColorHSLA(Color.h, Color.s, 0.5f, 1.0f));
		}
		else if(i == 2)
		{
			RenderLightingRect(&Rail, color_cast<ColorRGBA>(ColorHSLA(Color.h, Color.s, 0.5f, 1.0f)));
			HandleColor = color_cast<ColorRGBA>(ColorHSLA(Color.h, Color.s, Color.l, 1.0f).UnclampLighting(DarkestLight));
		}
		else if(i == 3)
		{
			RenderAlphaRect(&Rail, color_cast<ColorRGBA>(ColorHSLA(Color.h, Color.s, Color.l, 1.0f).UnclampLighting(DarkestLight)));
			HandleColor = color_cast<ColorRGBA>(Color.UnclampLighting(DarkestLight));
		}
		Graphics()->TrianglesEnd();

		Color[i] = Ui()->DoScrollbarH(&((char *)pColor)[i], &Button, Color[i], &HandleColor);
	}

	if(OriginalColor != Color)
	{
		*pColor = Color.Pack(Alpha);
	}
	return PrevPackedColor != *pColor;
}
