/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "countryflags.h"
#include "menus.h"
#include "skins.h"

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

void CMenus::RenderSettingsGeneral(CUIRect MainView)
{
	char aBuf[128 + IO_MAX_PATH_LENGTH];
	CUIRect Label, Button, Left, Right, Game, ClientSettings;
	MainView.HSplitTop(190.0f, &Game, &ClientSettings);

	// game
	{
		// headline
		CUIRect GameLabel, LanguageLabel;
		Game.HSplitTop(30.0f, &Label, &Game);
		Label.VSplitMid(&GameLabel, &LanguageLabel, 20.0f);
		Ui()->DoLabel(&GameLabel, Localize("Game"), 20.0f, TEXTALIGN_ML);
		Ui()->DoLabel(&LanguageLabel, Localize("Language"), 20.0f, TEXTALIGN_ML);
		Game.HSplitTop(5.0f, nullptr, &Game);
		Game.VSplitMid(&Left, &Right, 20.0f);

		// dynamic camera
		Left.HSplitTop(20.0f, &Button, &Left);
		const bool IsDyncam = g_Config.m_ClDyncam || g_Config.m_ClMouseFollowfactor > 0;
		if(DoButton_CheckBox(&g_Config.m_ClDyncam, Localize("Dynamic Camera"), IsDyncam, &Button))
		{
			if(IsDyncam)
			{
				g_Config.m_ClDyncam = 0;
				g_Config.m_ClMouseFollowfactor = 0;
			}
			else
			{
				g_Config.m_ClDyncam = 1;
			}
		}

		// smooth dynamic camera
		Left.HSplitTop(5.0f, nullptr, &Left);
		Left.HSplitTop(20.0f, &Button, &Left);
		if(g_Config.m_ClDyncam)
		{
			if(DoButton_CheckBox(&g_Config.m_ClDyncamSmoothness, Localize("Smooth Dynamic Camera"), g_Config.m_ClDyncamSmoothness, &Button))
			{
				if(g_Config.m_ClDyncamSmoothness)
				{
					g_Config.m_ClDyncamSmoothness = 0;
				}
				else
				{
					g_Config.m_ClDyncamSmoothness = 50;
					g_Config.m_ClDyncamStabilizing = 50;
				}
			}
		}

		// weapon pickup
		Left.HSplitTop(5.0f, nullptr, &Left);
		Left.HSplitTop(20.0f, &Button, &Left);
		if(DoButton_CheckBox(&g_Config.m_ClAutoswitchWeapons, Localize("Switch weapon on pickup"), g_Config.m_ClAutoswitchWeapons, &Button))
			g_Config.m_ClAutoswitchWeapons ^= 1;

		// weapon out of ammo autoswitch
		Left.HSplitTop(5.0f, nullptr, &Left);
		Left.HSplitTop(20.0f, &Button, &Left);
		if(DoButton_CheckBox(&g_Config.m_ClAutoswitchWeaponsOutOfAmmo, Localize("Switch weapon when out of ammo"), g_Config.m_ClAutoswitchWeaponsOutOfAmmo, &Button))
			g_Config.m_ClAutoswitchWeaponsOutOfAmmo ^= 1;

		Right.HSplitTop(5.0f, nullptr, &Right);
		RenderLanguageSelection(Right);
	}

	// client
	{
		// headline
		ClientSettings.HSplitTop(30.0f, &Label, &ClientSettings);
		Ui()->DoLabel(&Label, Localize("Client"), 20.0f, TEXTALIGN_ML);
		ClientSettings.HSplitTop(5.0f, nullptr, &ClientSettings);
		ClientSettings.VSplitMid(&Left, &Right, 20.0f);

		// skip main menu
		Left.HSplitTop(20.0f, &Button, &Left);
		if(DoButton_CheckBox(&g_Config.m_ClSkipStartMenu, Localize("Skip the main menu"), g_Config.m_ClSkipStartMenu, &Button))
			g_Config.m_ClSkipStartMenu ^= 1;

		Left.HSplitTop(10.0f, nullptr, &Left);
		Left.HSplitTop(20.0f, &Button, &Left);
		str_copy(aBuf, " ");
		str_append(aBuf, Localize("Hz", "Hertz"));
		Ui()->DoScrollbarOption(&g_Config.m_ClRefreshRate, &g_Config.m_ClRefreshRate, &Button, Localize("Update Rate"), 10, 1000, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_INFINITE | CUi::SCROLLBAR_OPTION_NOCLAMPVALUE | CUi::SCROLLBAR_OPTION_DELAYUPDATE, aBuf);
		Left.HSplitTop(5.0f, nullptr, &Left);
		Left.HSplitTop(20.0f, &Button, &Left);
		static int s_LowerRefreshRate;
		if(DoButton_CheckBox(&s_LowerRefreshRate, Localize("Save power by lowering update rate (higher input latency)"), g_Config.m_ClRefreshRate <= 480 && g_Config.m_ClRefreshRate != 0, &Button))
			g_Config.m_ClRefreshRate = g_Config.m_ClRefreshRate > 480 || g_Config.m_ClRefreshRate == 0 ? 480 : 0;

		CUIRect SettingsButton;
		Left.HSplitBottom(20.0f, &Left, &SettingsButton);
		Left.HSplitBottom(5.0f, &Left, nullptr);
		static CButtonContainer s_SettingsButtonId;
		if(DoButton_Menu(&s_SettingsButtonId, Localize("Settings file"), 0, &SettingsButton))
		{
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::DDNET].m_aConfigPath, aBuf, sizeof(aBuf));
			Client()->ViewFile(aBuf);
		}
		GameClient()->m_Tooltips.DoToolTip(&s_SettingsButtonId, &SettingsButton, Localize("Open the settings file"));

		CUIRect SavesButton;
		Left.HSplitBottom(20.0f, &Left, &SavesButton);
		Left.HSplitBottom(5.0f, &Left, nullptr);
		static CButtonContainer s_SavesButtonId;
		if(DoButton_Menu(&s_SavesButtonId, Localize("Saves file"), 0, &SavesButton))
		{
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, SAVES_FILE, aBuf, sizeof(aBuf));
			Client()->ViewFile(aBuf);
		}
		GameClient()->m_Tooltips.DoToolTip(&s_SavesButtonId, &SavesButton, Localize("Open the saves file"));

		CUIRect ConfigButton;
		Left.HSplitBottom(20.0f, &Left, &ConfigButton);
		Left.HSplitBottom(5.0f, &Left, nullptr);
		static CButtonContainer s_ConfigButtonId;
		if(DoButton_Menu(&s_ConfigButtonId, Localize("Config directory"), 0, &ConfigButton))
		{
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, "", aBuf, sizeof(aBuf));
			Client()->ViewFile(aBuf);
		}
		GameClient()->m_Tooltips.DoToolTip(&s_ConfigButtonId, &ConfigButton, Localize("Open the directory that contains the configuration and user files"));

		CUIRect DirectoryButton;
		Left.HSplitBottom(20.0f, &Left, &DirectoryButton);
		Left.HSplitBottom(5.0f, &Left, nullptr);
		static CButtonContainer s_ThemesButtonId;
		if(DoButton_Menu(&s_ThemesButtonId, Localize("Themes directory"), 0, &DirectoryButton))
		{
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, "themes", aBuf, sizeof(aBuf));
			Storage()->CreateFolder("themes", IStorage::TYPE_SAVE);
			Client()->ViewFile(aBuf);
		}
		GameClient()->m_Tooltips.DoToolTip(&s_ThemesButtonId, &DirectoryButton, Localize("Open the directory to add custom themes"));

		Left.HSplitTop(20.0f, nullptr, &Left);
		RenderThemeSelection(Left);

		// auto demo settings
		{
			Right.HSplitTop(40.0f, nullptr, &Right);
			Right.HSplitTop(20.0f, &Button, &Right);
			if(DoButton_CheckBox(&g_Config.m_ClAutoDemoRecord, Localize("Automatically record demos"), g_Config.m_ClAutoDemoRecord, &Button))
				g_Config.m_ClAutoDemoRecord ^= 1;

			Right.HSplitTop(2 * 20.0f, &Button, &Right);
			if(g_Config.m_ClAutoDemoRecord)
				Ui()->DoScrollbarOption(&g_Config.m_ClAutoDemoMax, &g_Config.m_ClAutoDemoMax, &Button, Localize("Max demos"), 1, 1000, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_INFINITE | CUi::SCROLLBAR_OPTION_MULTILINE);

			Right.HSplitTop(10.0f, nullptr, &Right);
			Right.HSplitTop(20.0f, &Button, &Right);
			if(DoButton_CheckBox(&g_Config.m_ClAutoScreenshot, Localize("Automatically take game over screenshot"), g_Config.m_ClAutoScreenshot, &Button))
				g_Config.m_ClAutoScreenshot ^= 1;

			Right.HSplitTop(2 * 20.0f, &Button, &Right);
			if(g_Config.m_ClAutoScreenshot)
				Ui()->DoScrollbarOption(&g_Config.m_ClAutoScreenshotMax, &g_Config.m_ClAutoScreenshotMax, &Button, Localize("Max Screenshots"), 1, 1000, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_INFINITE | CUi::SCROLLBAR_OPTION_MULTILINE);
		}

		// auto statboard screenshot
		{
			Right.HSplitTop(10.0f, nullptr, &Right);
			Right.HSplitTop(20.0f, &Button, &Right);
			if(DoButton_CheckBox(&g_Config.m_ClAutoStatboardScreenshot, Localize("Automatically take statboard screenshot"), g_Config.m_ClAutoStatboardScreenshot, &Button))
			{
				g_Config.m_ClAutoStatboardScreenshot ^= 1;
			}

			Right.HSplitTop(2 * 20.0f, &Button, &Right);
			if(g_Config.m_ClAutoStatboardScreenshot)
				Ui()->DoScrollbarOption(&g_Config.m_ClAutoStatboardScreenshotMax, &g_Config.m_ClAutoStatboardScreenshotMax, &Button, Localize("Max Screenshots"), 1, 1000, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_INFINITE | CUi::SCROLLBAR_OPTION_MULTILINE);
		}

		// auto statboard csv
		{
			Right.HSplitTop(10.0f, nullptr, &Right);
			Right.HSplitTop(20.0f, &Button, &Right);
			if(DoButton_CheckBox(&g_Config.m_ClAutoCSV, Localize("Automatically create statboard csv"), g_Config.m_ClAutoCSV, &Button))
			{
				g_Config.m_ClAutoCSV ^= 1;
			}

			Right.HSplitTop(2 * 20.0f, &Button, &Right);
			if(g_Config.m_ClAutoCSV)
				Ui()->DoScrollbarOption(&g_Config.m_ClAutoCSVMax, &g_Config.m_ClAutoCSVMax, &Button, Localize("Max CSVs"), 1, 1000, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_INFINITE | CUi::SCROLLBAR_OPTION_MULTILINE);
		}
	}
}

void CMenus::SetNeedSendInfo()
{
	if(m_Dummy)
		m_NeedSendDummyinfo = true;
	else
		m_NeedSendinfo = true;
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

void CMenus::RenderSettingsPlayer(CUIRect MainView)
{
	CUIRect TabBar, PlayerTab, DummyTab, ChangeInfo, QuickSearch;
	MainView.HSplitTop(20.0f, &TabBar, &MainView);
	TabBar.VSplitMid(&TabBar, &ChangeInfo, 20.f);
	TabBar.VSplitMid(&PlayerTab, &DummyTab);
	MainView.HSplitTop(10.0f, nullptr, &MainView);

	static CButtonContainer s_PlayerTabButton;
	if(DoButton_MenuTab(&s_PlayerTabButton, Localize("Player"), !m_Dummy, &PlayerTab, IGraphics::CORNER_L, nullptr, nullptr, nullptr, nullptr, 4.0f))
	{
		m_Dummy = false;
	}

	static CButtonContainer s_DummyTabButton;
	if(DoButton_MenuTab(&s_DummyTabButton, Localize("Dummy"), m_Dummy, &DummyTab, IGraphics::CORNER_R, nullptr, nullptr, nullptr, nullptr, 4.0f))
	{
		m_Dummy = true;
	}

	if(Client()->State() == IClient::STATE_ONLINE &&
		GameClient()->m_aNextChangeInfo[m_Dummy] > Client()->GameTick(m_Dummy))
	{
		char aChangeInfo[128], aTimeLeft[32];
		str_format(aTimeLeft, sizeof(aTimeLeft), Localize("%ds left"), (GameClient()->m_aNextChangeInfo[m_Dummy] - Client()->GameTick(m_Dummy) + Client()->GameTickSpeed() - 1) / Client()->GameTickSpeed());
		str_format(aChangeInfo, sizeof(aChangeInfo), "%s: %s", Localize("Player info change cooldown"), aTimeLeft);
		Ui()->DoLabel(&ChangeInfo, aChangeInfo, 10.f, TEXTALIGN_ML);
	}

	static CLineInput s_NameInput;
	static CLineInput s_ClanInput;

	int *pCountry;
	if(!m_Dummy)
	{
		pCountry = &g_Config.m_PlayerCountry;
		s_NameInput.SetBuffer(g_Config.m_PlayerName, sizeof(g_Config.m_PlayerName));
		s_NameInput.SetEmptyText(Client()->PlayerName());
		s_ClanInput.SetBuffer(g_Config.m_PlayerClan, sizeof(g_Config.m_PlayerClan));
	}
	else
	{
		pCountry = &g_Config.m_ClDummyCountry;
		s_NameInput.SetBuffer(g_Config.m_ClDummyName, sizeof(g_Config.m_ClDummyName));
		s_NameInput.SetEmptyText(Client()->DummyName());
		s_ClanInput.SetBuffer(g_Config.m_ClDummyClan, sizeof(g_Config.m_ClDummyClan));
	}

	// player name
	CUIRect Button, Label;
	MainView.HSplitTop(20.0f, &Button, &MainView);
	Button.VSplitLeft(80.0f, &Label, &Button);
	Button.VSplitLeft(150.0f, &Button, nullptr);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "%s:", Localize("Name"));
	Ui()->DoLabel(&Label, aBuf, 14.0f, TEXTALIGN_ML);
	if(Ui()->DoEditBox(&s_NameInput, &Button, 14.0f))
	{
		SetNeedSendInfo();
	}

	// player clan
	MainView.HSplitTop(5.0f, nullptr, &MainView);
	MainView.HSplitTop(20.0f, &Button, &MainView);
	Button.VSplitLeft(80.0f, &Label, &Button);
	Button.VSplitLeft(150.0f, &Button, nullptr);
	str_format(aBuf, sizeof(aBuf), "%s:", Localize("Clan"));
	Ui()->DoLabel(&Label, aBuf, 14.0f, TEXTALIGN_ML);
	if(Ui()->DoEditBox(&s_ClanInput, &Button, 14.0f))
	{
		SetNeedSendInfo();
	}

	// country flag selector
	static CLineInputBuffered<25> s_FlagFilterInput;

	std::vector<const CCountryFlags::CCountryFlag *> vpFilteredFlags;
	for(size_t i = 0; i < GameClient()->m_CountryFlags.Num(); ++i)
	{
		const CCountryFlags::CCountryFlag &Entry = GameClient()->m_CountryFlags.GetByIndex(i);
		if(!str_find_nocase(Entry.m_aCountryCodeString, s_FlagFilterInput.GetString()))
			continue;
		vpFilteredFlags.push_back(&Entry);
	}

	MainView.HSplitTop(10.0f, nullptr, &MainView);
	MainView.HSplitBottom(20.0f, &MainView, &QuickSearch);
	MainView.HSplitBottom(5.0f, &MainView, nullptr);
	QuickSearch.VSplitLeft(220.0f, &QuickSearch, nullptr);

	int SelectedOld = -1;
	static CListBox s_ListBox;
	s_ListBox.DoStart(48.0f, vpFilteredFlags.size(), 10, 3, SelectedOld, &MainView);

	for(size_t i = 0; i < vpFilteredFlags.size(); i++)
	{
		const CCountryFlags::CCountryFlag *pEntry = vpFilteredFlags[i];

		if(pEntry->m_CountryCode == *pCountry)
			SelectedOld = i;

		const CListboxItem Item = s_ListBox.DoNextItem(&pEntry->m_CountryCode, SelectedOld >= 0 && (size_t)SelectedOld == i);
		if(!Item.m_Visible)
			continue;

		CUIRect FlagRect;
		Item.m_Rect.Margin(5.0f, &FlagRect);
		FlagRect.HSplitBottom(12.0f, &FlagRect, &Label);
		Label.HSplitTop(2.0f, nullptr, &Label);
		const float OldWidth = FlagRect.w;
		FlagRect.w = FlagRect.h * 2;
		FlagRect.x += (OldWidth - FlagRect.w) / 2.0f;
		GameClient()->m_CountryFlags.Render(pEntry->m_CountryCode, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), FlagRect.x, FlagRect.y, FlagRect.w, FlagRect.h);

		if(pEntry->m_Texture.IsValid())
		{
			Ui()->DoLabel(&Label, pEntry->m_aCountryCodeString, 10.0f, TEXTALIGN_MC);
		}
	}

	const int NewSelected = s_ListBox.DoEnd();
	if(SelectedOld != NewSelected)
	{
		*pCountry = vpFilteredFlags[NewSelected]->m_CountryCode;
		SetNeedSendInfo();
	}

	Ui()->DoEditBox_Search(&s_FlagFilterInput, &QuickSearch, 14.0f, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());
}

void CMenus::RenderSettingsTee(CUIRect MainView)
{
	CUIRect TabBar, PlayerTab, DummyTab, ChangeInfo;
	MainView.HSplitTop(20.0f, &TabBar, &MainView);
	TabBar.VSplitMid(&TabBar, &ChangeInfo, 20.f);
	TabBar.VSplitMid(&PlayerTab, &DummyTab);
	MainView.HSplitTop(10.0f, nullptr, &MainView);

	static CButtonContainer s_PlayerTabButton;
	if(DoButton_MenuTab(&s_PlayerTabButton, Localize("Player"), !m_Dummy, &PlayerTab, IGraphics::CORNER_L, nullptr, nullptr, nullptr, nullptr, 4.0f))
	{
		m_Dummy = false;
		m_SkinListScrollToSelected = true;
	}

	static CButtonContainer s_DummyTabButton;
	if(DoButton_MenuTab(&s_DummyTabButton, Localize("Dummy"), m_Dummy, &DummyTab, IGraphics::CORNER_R, nullptr, nullptr, nullptr, nullptr, 4.0f))
	{
		m_Dummy = true;
		m_SkinListScrollToSelected = true;
	}

	if(Client()->State() == IClient::STATE_ONLINE &&
		GameClient()->m_aNextChangeInfo[m_Dummy] > Client()->GameTick(m_Dummy))
	{
		char aChangeInfo[128], aTimeLeft[32];
		str_format(aTimeLeft, sizeof(aTimeLeft), Localize("%ds left"), (GameClient()->m_aNextChangeInfo[m_Dummy] - Client()->GameTick(m_Dummy) + Client()->GameTickSpeed() - 1) / Client()->GameTickSpeed());
		str_format(aChangeInfo, sizeof(aChangeInfo), "%s: %s", Localize("Player info change cooldown"), aTimeLeft);
		Ui()->DoLabel(&ChangeInfo, aChangeInfo, 10.f, TEXTALIGN_ML);
	}

	if(g_Config.m_Debug)
	{
		const CSkins::CSkinLoadingStats Stats = GameClient()->m_Skins.LoadingStats();
		char aStats[256];
		str_format(aStats, sizeof(aStats), "unloaded: %" PRIzu ", pending: %" PRIzu ", loading: %" PRIzu ",\nloaded: %" PRIzu ", error: %" PRIzu ", notfound: %" PRIzu,
			Stats.m_NumUnloaded, Stats.m_NumPending, Stats.m_NumLoading, Stats.m_NumLoaded, Stats.m_NumError, Stats.m_NumNotFound);
		Ui()->DoLabel(&ChangeInfo, aStats, 9.0f, TEXTALIGN_MR);
	}

	char *pSkinName;
	size_t SkinNameSize;
	int *pUseCustomColor;
	unsigned *pColorBody;
	unsigned *pColorFeet;
	int *pEmote;
	int *pCountry;
	static CLineInput s_NameInput;
	static CLineInput s_ClanInput;
	if(!m_Dummy)
	{
		pSkinName = g_Config.m_ClPlayerSkin;
		SkinNameSize = sizeof(g_Config.m_ClPlayerSkin);
		pUseCustomColor = &g_Config.m_ClPlayerUseCustomColor;
		pColorBody = &g_Config.m_ClPlayerColorBody;
		pColorFeet = &g_Config.m_ClPlayerColorFeet;
		pEmote = &g_Config.m_ClPlayerDefaultEyes;
		pCountry = &g_Config.m_PlayerCountry;
		s_NameInput.SetBuffer(g_Config.m_PlayerName, sizeof(g_Config.m_PlayerName));
		s_NameInput.SetEmptyText(Client()->PlayerName());
		s_ClanInput.SetBuffer(g_Config.m_PlayerClan, sizeof(g_Config.m_PlayerClan));
	}
	else
	{
		pSkinName = g_Config.m_ClDummySkin;
		SkinNameSize = sizeof(g_Config.m_ClDummySkin);
		pUseCustomColor = &g_Config.m_ClDummyUseCustomColor;
		pColorBody = &g_Config.m_ClDummyColorBody;
		pColorFeet = &g_Config.m_ClDummyColorFeet;
		pEmote = &g_Config.m_ClDummyDefaultEyes;
		pCountry = &g_Config.m_ClDummyCountry;
		s_NameInput.SetBuffer(g_Config.m_ClDummyName, sizeof(g_Config.m_ClDummyName));
		s_NameInput.SetEmptyText(Client()->DummyName());
		s_ClanInput.SetBuffer(g_Config.m_ClDummyClan, sizeof(g_Config.m_ClDummyClan));
	}

	const float EyeButtonSize = 40.0f;
	const bool RenderEyesBelow = MainView.w < 750.0f;
	CUIRect YourSkin, Checkboxes, SkinPrefix, Eyes, Button, Label;
	MainView.HSplitTop(130.0f, &YourSkin, &MainView);
	if(RenderEyesBelow)
	{
		YourSkin.VSplitLeft(MainView.w * 0.45f, &YourSkin, &Checkboxes);
		Checkboxes.VSplitLeft(MainView.w * 0.35f, &Checkboxes, &SkinPrefix);
		MainView.HSplitTop(5.0f, nullptr, &MainView);
		MainView.HSplitTop(EyeButtonSize, &Eyes, &MainView);
		Eyes.VSplitRight(EyeButtonSize * (float)NUM_EMOTES + 5.0f * (float)(NUM_EMOTES - 1), nullptr, &Eyes);
	}
	else
	{
		YourSkin.VSplitRight(3 * EyeButtonSize + 2 * 5.0f, &YourSkin, &Eyes);
		const float RemainderWidth = YourSkin.w;
		YourSkin.VSplitLeft(RemainderWidth * 0.4f, &YourSkin, &Checkboxes);
		Checkboxes.VSplitLeft(RemainderWidth * 0.35f, &Checkboxes, &SkinPrefix);
		SkinPrefix.VSplitRight(20.0f, &SkinPrefix, nullptr);
	}
	YourSkin.VSplitRight(20.0f, &YourSkin, nullptr);
	Checkboxes.VSplitRight(20.0f, &Checkboxes, nullptr);

	// Checkboxes
	bool ShouldRefresh = false;
	Checkboxes.HSplitTop(20.0f, &Button, &Checkboxes);
	if(DoButton_CheckBox(&g_Config.m_ClDownloadSkins, Localize("Download skins"), g_Config.m_ClDownloadSkins, &Button))
	{
		g_Config.m_ClDownloadSkins ^= 1;
		ShouldRefresh = true;
	}

	Checkboxes.HSplitTop(20.0f, &Button, &Checkboxes);
	if(DoButton_CheckBox(&g_Config.m_ClDownloadCommunitySkins, Localize("Download community skins"), g_Config.m_ClDownloadCommunitySkins, &Button))
	{
		g_Config.m_ClDownloadCommunitySkins ^= 1;
		ShouldRefresh = true;
	}

	Checkboxes.HSplitTop(20.0f, &Button, &Checkboxes);
	if(DoButton_CheckBox(&g_Config.m_ClVanillaSkinsOnly, Localize("Vanilla skins only"), g_Config.m_ClVanillaSkinsOnly, &Button))
	{
		g_Config.m_ClVanillaSkinsOnly ^= 1;
		ShouldRefresh = true;
	}

	Checkboxes.HSplitTop(20.0f, &Button, &Checkboxes);
	if(DoButton_CheckBox(&g_Config.m_ClFatSkins, Localize("Fat skins (DDFat)"), g_Config.m_ClFatSkins, &Button))
	{
		g_Config.m_ClFatSkins ^= 1;
	}

	// Skin prefix
	{
		SkinPrefix.HSplitTop(20.0f, &Label, &SkinPrefix);
		Ui()->DoLabel(&Label, Localize("Skin prefix"), 14.0f, TEXTALIGN_ML);

		SkinPrefix.HSplitTop(20.0f, &Button, &SkinPrefix);
		static CLineInput s_SkinPrefixInput(g_Config.m_ClSkinPrefix, sizeof(g_Config.m_ClSkinPrefix));
		if(Ui()->DoClearableEditBox(&s_SkinPrefixInput, &Button, 14.0f))
		{
			ShouldRefresh = true;
		}

		SkinPrefix.HSplitTop(2.0f, nullptr, &SkinPrefix);

		static const char *s_apSkinPrefixes[] = {"kitty", "santa"};
		static CButtonContainer s_aPrefixButtons[std::size(s_apSkinPrefixes)];
		for(size_t i = 0; i < std::size(s_apSkinPrefixes); i++)
		{
			SkinPrefix.HSplitTop(20.0f, &Button, &SkinPrefix);
			Button.HMargin(2.0f, &Button);
			if(DoButton_Menu(&s_aPrefixButtons[i], s_apSkinPrefixes[i], 0, &Button))
			{
				str_copy(g_Config.m_ClSkinPrefix, s_apSkinPrefixes[i]);
				ShouldRefresh = true;
			}
		}
	}
	CUIRect RandomColorsButton;

	// Player skin area
	CUIRect CustomColorsButton, RandomSkinButton;
	YourSkin.HSplitTop(20.0f, &Label, &YourSkin);
	YourSkin.HSplitBottom(20.0f, &YourSkin, &CustomColorsButton);

	CustomColorsButton.VSplitRight(30.0f, &CustomColorsButton, &RandomSkinButton);
	CustomColorsButton.VSplitRight(3.0f, &CustomColorsButton, 0);

	CustomColorsButton.VSplitRight(110.0f, &CustomColorsButton, &RandomColorsButton);

	CustomColorsButton.VSplitRight(5.0f, &CustomColorsButton, nullptr);
	CSkins::CSkinList &SkinList = GameClient()->m_Skins.SkinList();
	YourSkin.VSplitLeft(65.0f, &YourSkin, &Button);
	Button.VSplitLeft(5.0f, nullptr, &Button);

	const float NameClanSkinHeight = 3.0f * 20.0f + 2.0f * 5.0f;
	if(Button.h > NameClanSkinHeight)
	{
		Button.HMargin((Button.h - NameClanSkinHeight) / 2.0f, &Button);
	}

	CUIRect NameRow, ClanRow, SkinRow;
	Button.HSplitTop(20.0f, &NameRow, &Button);
	Button.HSplitTop(5.0f, nullptr, &Button);
	Button.HSplitTop(20.0f, &ClanRow, &Button);
	Button.HSplitTop(5.0f, nullptr, &Button);
	Button.HSplitTop(20.0f, &SkinRow, nullptr);

	CUIRect NameLabel, NameInput, ClanLabel, ClanInput, SkinLabel, SkinInput, FlagButton;
	NameRow.VSplitLeft(45.0f, &NameLabel, &NameInput);
	ClanRow.VSplitLeft(45.0f, &ClanLabel, &ClanInput);
	SkinRow.VSplitLeft(45.0f, &SkinLabel, &SkinInput);
	SkinInput.VSplitRight(44.0f, &SkinInput, &FlagButton);
	SkinInput.VSplitRight(5.0f, &SkinInput, nullptr);

	Ui()->DoLabel(&NameLabel, Localize("Name"), 14.0f, TEXTALIGN_ML);
	Ui()->DoLabel(&ClanLabel, Localize("Clan"), 14.0f, TEXTALIGN_ML);
	Ui()->DoLabel(&SkinLabel, Localize("Skin"), 14.0f, TEXTALIGN_ML);

	if(Ui()->DoEditBox(&s_NameInput, &NameInput, 14.0f))
	{
		SetNeedSendInfo();
	}

	if(Ui()->DoEditBox(&s_ClanInput, &ClanInput, 14.0f))
	{
		SetNeedSendInfo();
	}

	static CLineInput s_SkinInput;
	s_SkinInput.SetBuffer(pSkinName, SkinNameSize);
	s_SkinInput.SetEmptyText("default");
	if(Ui()->DoClearableEditBox(&s_SkinInput, &SkinInput, 14.0f))
	{
		SetNeedSendInfo();
		m_SkinListScrollToSelected = true;
		SkinList.ForceRefresh();
	}

	static CButtonContainer s_FlagButton;
	if(DoButton_Menu(&s_FlagButton, "", 0, &FlagButton))
	{
		static SPopupMenuId s_PopupCountryId;
		static SPopupSettingsCountrySelectionContext s_PopupCountryContext;
		s_PopupCountryContext.m_pMenus = this;
		s_PopupCountryContext.m_pCountry = pCountry;
		s_PopupCountryContext.m_Selection = *pCountry;
		s_PopupCountryContext.m_New = true;
		Ui()->DoPopupMenu(&s_PopupCountryId, FlagButton.x, FlagButton.y + FlagButton.h, 490.0f, 210.0f, &s_PopupCountryContext, PopupSettingsCountrySelection);
	}
	GameClient()->m_Tooltips.DoToolTip(&s_FlagButton, &FlagButton, Localize("Choose country flag"));

	CUIRect FlagIcon = FlagButton;
	const float OldWidth = FlagIcon.w;
	FlagIcon.w = FlagIcon.h * 2.0f;
	FlagIcon.x += (OldWidth - FlagIcon.w) / 2.0f;
	GameClient()->m_CountryFlags.Render(*pCountry, ColorRGBA(1.0f, 1.0f, 1.0f, Ui()->HotItem() == &s_FlagButton ? 1.0f : 0.85f), FlagIcon.x, FlagIcon.y, FlagIcon.w, FlagIcon.h);

	char aBuf[128 + IO_MAX_PATH_LENGTH];
	str_format(aBuf, sizeof(aBuf), "%s:", Localize("Your skin"));
	Ui()->DoLabel(&Label, aBuf, 14.0f, TEXTALIGN_ML);

	const CSkin *pDefaultSkin = GameClient()->m_Skins.Find("default");
	const CSkins::CSkinContainer *pOwnSkinContainer = GameClient()->m_Skins.FindContainerOrNullptr(pSkinName[0] == '\0' ? "default" : pSkinName);
	if(pOwnSkinContainer != nullptr && pOwnSkinContainer->IsSpecial())
	{
		pOwnSkinContainer = nullptr; // Special skins cannot be selected, show as missing due to invalid name
	}

	CTeeRenderInfo OwnSkinInfo;
	OwnSkinInfo.Apply(pOwnSkinContainer == nullptr || pOwnSkinContainer->Skin() == nullptr ? pDefaultSkin : pOwnSkinContainer->Skin().get());
	OwnSkinInfo.ApplyColors(*pUseCustomColor, *pColorBody, *pColorFeet);
	OwnSkinInfo.m_Size = 64.0f;

	// Tee
	{
		vec2 OffsetToMid;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &OwnSkinInfo, OffsetToMid);
		const vec2 TeeRenderPos = vec2(YourSkin.x + YourSkin.w / 2.0f, YourSkin.y + YourSkin.h / 2.0f + OffsetToMid.y);
		// tee looking towards cursor, and it is happy when you touch it
		const vec2 DeltaPosition = Ui()->MousePos() - TeeRenderPos;
		const float Distance = length(DeltaPosition);
		const float InteractionDistance = 20.0f;
		const vec2 TeeDirection = Distance < InteractionDistance ? normalize(vec2(DeltaPosition.x, maximum(DeltaPosition.y, 0.5f))) : normalize(DeltaPosition);
		const int TeeEmote = Distance < InteractionDistance ? EMOTE_HAPPY : *pEmote;
		RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, TeeEmote, TeeDirection, TeeRenderPos);
	}

	// Skin loading status
	const auto &&RenderSkinStatus = [&](CUIRect Parent, const CSkins::CSkinContainer *pSkinContainer, const void *pStatusTooltipId) {
		if(pSkinContainer != nullptr && pSkinContainer->State() == CSkins::CSkinContainer::EState::LOADED)
		{
			return;
		}

		CUIRect StatusIcon;
		Parent.HSplitTop(20.0f, &StatusIcon, nullptr);
		StatusIcon.VSplitLeft(20.0f, &StatusIcon, nullptr);

		if(pSkinContainer != nullptr &&
			(pSkinContainer->State() == CSkins::CSkinContainer::EState::UNLOADED ||
				pSkinContainer->State() == CSkins::CSkinContainer::EState::PENDING ||
				pSkinContainer->State() == CSkins::CSkinContainer::EState::LOADING))
		{
			Ui()->RenderProgressSpinner(StatusIcon.Center(), 5.0f);
		}
		else
		{
			TextRender()->TextColor(ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f));
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
			Ui()->DoLabel(&StatusIcon, pSkinContainer == nullptr || pSkinContainer->State() == CSkins::CSkinContainer::EState::ERROR ? FontIcon::TRIANGLE_EXCLAMATION : FontIcon::QUESTION, 12.0f, TEXTALIGN_MC);
			TextRender()->SetRenderFlags(0);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			Ui()->DoButtonLogic(pStatusTooltipId, 0, &StatusIcon, BUTTONFLAG_NONE);
			const char *pErrorTooltip;
			if(pSkinContainer == nullptr)
			{
				pErrorTooltip = Localize("This skin name cannot be used.");
			}
			else if(pSkinContainer->State() == CSkins::CSkinContainer::EState::ERROR)
			{
				pErrorTooltip = Localize("Skin could not be loaded due to an error. Check the local console for details.");
			}
			else
			{
				pErrorTooltip = Localize("Skin could not be found.");
			}
			GameClient()->m_Tooltips.DoToolTip(pStatusTooltipId, &StatusIcon, pErrorTooltip);
		}
	};
	static char s_StatusTooltipId;
	RenderSkinStatus(YourSkin, pOwnSkinContainer, &s_StatusTooltipId);

	// Random skin button
	static CButtonContainer s_RandomSkinButton;
	static const char *s_apDice[] = {FontIcon::DICE_ONE, FontIcon::DICE_TWO, FontIcon::DICE_THREE, FontIcon::DICE_FOUR, FontIcon::DICE_FIVE, FontIcon::DICE_SIX};
	static int s_CurrentDie = rand() % std::size(s_apDice);
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	if(DoButton_Menu(&s_RandomSkinButton, s_apDice[s_CurrentDie], 0, &RandomSkinButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, -0.2f))
	{
		GameClient()->m_Skins.RandomizeSkin(m_Dummy);
		SetNeedSendInfo();
		m_SkinListScrollToSelected = true;
		s_CurrentDie = rand() % std::size(s_apDice);
	}
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	GameClient()->m_Tooltips.DoToolTip(&s_RandomSkinButton, &RandomSkinButton, Localize("Create a random skin"));

	static CButtonContainer s_RandomizeColors;
	if(*pUseCustomColor)
	{
		// RandomColorsButton.VSplitLeft(120.0f, &RandomColorsButton, 0);
		if(DoButton_Menu(&s_RandomizeColors, "Random Colors", 0, &RandomColorsButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f)))
		{
			if(m_Dummy)
			{
				g_Config.m_ClDummyColorBody = ColorHSLA((std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, 1).Pack(false);
				g_Config.m_ClDummyColorFeet = ColorHSLA((std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, 1).Pack(false);
			}
			else
			{
				g_Config.m_ClPlayerColorBody = ColorHSLA((std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, 1).Pack(false);
				g_Config.m_ClPlayerColorFeet = ColorHSLA((std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, 1).Pack(false);
			}
			SetNeedSendInfo();
		}
	}
	MainView.HSplitTop(5.0f, 0, &MainView);

	// Custom colors button
	if(DoButton_CheckBox(pUseCustomColor, Localize("Custom colors"), *pUseCustomColor, &CustomColorsButton))
	{
		*pUseCustomColor = *pUseCustomColor ? 0 : 1;
		SetNeedSendInfo();
	}

	// Default eyes
	{
		CTeeRenderInfo EyeSkinInfo = OwnSkinInfo;
		EyeSkinInfo.m_Size = EyeButtonSize;
		vec2 OffsetToMid;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &EyeSkinInfo, OffsetToMid);

		CUIRect EyesRow;
		Eyes.HSplitTop(EyeButtonSize, &EyesRow, &Eyes);
		static CButtonContainer s_aEyeButtons[NUM_EMOTES];
		for(int CurrentEyeEmote = 0; CurrentEyeEmote < NUM_EMOTES; CurrentEyeEmote++)
		{
			EyesRow.VSplitLeft(EyeButtonSize, &Button, &EyesRow);
			EyesRow.VSplitLeft(5.0f, nullptr, &EyesRow);
			if(!RenderEyesBelow && (CurrentEyeEmote + 1) % 3 == 0)
			{
				Eyes.HSplitTop(5.0f, nullptr, &Eyes);
				Eyes.HSplitTop(EyeButtonSize, &EyesRow, &Eyes);
			}

			const ColorRGBA EyeButtonColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f + (*pEmote == CurrentEyeEmote ? 0.25f : 0.0f));
			if(DoButton_Menu(&s_aEyeButtons[CurrentEyeEmote], "", 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, EyeButtonColor))
			{
				*pEmote = CurrentEyeEmote;
				if((int)m_Dummy == g_Config.m_ClDummy)
					GameClient()->m_Emoticon.EyeEmote(CurrentEyeEmote);
			}
			GameClient()->m_Tooltips.DoToolTip(&s_aEyeButtons[CurrentEyeEmote], &Button, Localize("Choose default eyes when joining a server"));
			RenderTools()->RenderTee(CAnimState::GetIdle(), &EyeSkinInfo, CurrentEyeEmote, vec2(1.0f, 0.0f), vec2(Button.x + Button.w / 2.0f, Button.y + Button.h / 2.0f + OffsetToMid.y));
		}
	}

	// Custom color pickers
	MainView.HSplitTop(5.0f, nullptr, &MainView);
	if(*pUseCustomColor)
	{
		CUIRect CustomColors;
		MainView.HSplitTop(95.0f, &CustomColors, &MainView);
		CUIRect aRects[2];
		CustomColors.VSplitMid(&aRects[0], &aRects[1], 20.0f);

		unsigned *apColors[] = {pColorBody, pColorFeet};
		const char *apParts[] = {Localize("Body"), Localize("Feet")};

		for(int i = 0; i < 2; i++)
		{
			aRects[i].HSplitTop(20.0f, &Label, &aRects[i]);
			Ui()->DoLabel(&Label, apParts[i], 14.0f, TEXTALIGN_ML);
			if(RenderHslaScrollbars(&aRects[i], apColors[i], false, ColorHSLA::DARKEST_LGT))
			{
				SetNeedSendInfo();
			}
		}
	}
	MainView.HSplitTop(5.0f, nullptr, &MainView);

	// Layout bottom controls and use remainder for skin selector
	CUIRect QuickSearch, DatabaseButton, DirectoryButton, RefreshButton;
	MainView.HSplitBottom(20.0f, &MainView, &QuickSearch);
	MainView.HSplitBottom(5.0f, &MainView, nullptr);
	QuickSearch.VSplitLeft(220.0f, &QuickSearch, &DatabaseButton);
	DatabaseButton.VSplitLeft(10.0f, nullptr, &DatabaseButton);
	DatabaseButton.VSplitLeft(150.0f, &DatabaseButton, &DirectoryButton);
	DirectoryButton.VSplitRight(175.0f, nullptr, &DirectoryButton);
	DirectoryButton.VSplitRight(25.0f, &DirectoryButton, &RefreshButton);
	DirectoryButton.VSplitRight(10.0f, &DirectoryButton, nullptr);

	// Skin selector
	static CListBox s_ListBox;
	std::vector<CSkins::CSkinListEntry> &vSkinList = SkinList.Skins();
	int OldSelected = -1;
	s_ListBox.DoStart(50.0f, vSkinList.size(), 4, 2, OldSelected, &MainView);
	for(size_t i = 0; i < vSkinList.size(); ++i)
	{
		CSkins::CSkinListEntry &SkinListEntry = vSkinList[i];
		const CSkins::CSkinContainer *pSkinContainer = vSkinList[i].SkinContainer();

		if(!m_Dummy ? SkinListEntry.IsSelectedMain() : SkinListEntry.IsSelectedDummy())
		{
			OldSelected = i;
			if(m_SkinListScrollToSelected)
			{
				s_ListBox.ScrollToSelected();
				m_SkinListScrollToSelected = false;
			}
		}

		const CListboxItem Item = s_ListBox.DoNextItem(SkinListEntry.ListItemId(), OldSelected >= 0 && (size_t)OldSelected == i);
		if(!Item.m_Visible)
		{
			continue;
		}

		SkinListEntry.RequestLoad();
		const CSkin *pSkin = pSkinContainer->State() == CSkins::CSkinContainer::EState::LOADED ? pSkinContainer->Skin().get() : pDefaultSkin;

		Item.m_Rect.VSplitLeft(60.0f, &Button, &Label);

		{
			CTeeRenderInfo Info = OwnSkinInfo;
			Info.Apply(pSkin);
			vec2 OffsetToMid;
			CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &Info, OffsetToMid);
			const vec2 TeeRenderPos = vec2(Button.x + Button.w / 2.0f, Button.y + Button.h / 2 + OffsetToMid.y);
			RenderTools()->RenderTee(CAnimState::GetIdle(), &Info, *pEmote, vec2(1.0f, 0.0f), TeeRenderPos);
		}

		{
			SLabelProperties Props;
			Props.m_MaxWidth = Label.w - 5.0f;
			const auto &NameMatch = SkinListEntry.NameMatch();
			if(NameMatch.has_value())
			{
				const auto [MatchStart, MatchLength] = NameMatch.value();
				Props.m_vColorSplits.emplace_back(MatchStart, MatchLength, ColorRGBA(0.4f, 0.4f, 1.0f, 1.0f));
			}
			Ui()->DoLabel(&Label, pSkinContainer->Name(), 12.0f, TEXTALIGN_ML, Props);
		}

		if(g_Config.m_Debug)
		{
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(*pUseCustomColor ? color_cast<ColorRGBA>(ColorHSLA(*pColorBody).UnclampLighting(ColorHSLA::DARKEST_LGT)) : pSkin->m_BloodColor);
			IGraphics::CQuadItem QuadItem(Label.x, Label.y, 12.0f, 12.0f);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
		}

		// render skin favorite icon
		{
			CUIRect FavIcon;
			Item.m_Rect.HSplitTop(20.0f, &FavIcon, nullptr);
			FavIcon.VSplitRight(20.0f, nullptr, &FavIcon);
			if(DoButton_Favorite(SkinListEntry.FavoriteButtonId(), SkinListEntry.ListItemId(), SkinListEntry.IsFavorite(), &FavIcon))
			{
				if(SkinListEntry.IsFavorite())
				{
					GameClient()->m_Skins.RemoveFavorite(pSkinContainer->Name());
				}
				else
				{
					GameClient()->m_Skins.AddFavorite(pSkinContainer->Name());
				}
			}
		}

		RenderSkinStatus(Item.m_Rect, pSkinContainer, SkinListEntry.ErrorTooltipId());
	}

	const int NewSelected = s_ListBox.DoEnd();
	if(OldSelected != NewSelected)
	{
		str_copy(pSkinName, vSkinList[NewSelected].SkinContainer()->Name(), SkinNameSize);
		SkinList.ForceRefresh();
		SetNeedSendInfo();
	}

	static CLineInput s_SkinFilterInput(g_Config.m_ClSkinFilterString, sizeof(g_Config.m_ClSkinFilterString));
	if(SkinList.UnfilteredCount() > 0 && vSkinList.empty())
	{
		CUIRect FilterLabel, ResetButton;
		MainView.HMargin((MainView.h - (16.0f + 18.0f + 8.0f)) / 2.0f, &FilterLabel);
		FilterLabel.HSplitTop(16.0f, &FilterLabel, &ResetButton);
		ResetButton.HSplitTop(8.0f, nullptr, &ResetButton);
		ResetButton.VMargin((ResetButton.w - 200.0f) / 2.0f, &ResetButton);
		Ui()->DoLabel(&FilterLabel, Localize("No skins match your filter criteria"), 16.0f, TEXTALIGN_MC);
		static CButtonContainer s_ResetButton;
		if(DoButton_Menu(&s_ResetButton, Localize("Reset filter"), 0, &ResetButton))
		{
			s_SkinFilterInput.Clear();
			SkinList.ForceRefresh();
		}
	}

	if(Ui()->DoEditBox_Search(&s_SkinFilterInput, &QuickSearch, 14.0f, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive()))
	{
		SkinList.ForceRefresh();
	}

	static CButtonContainer s_SkinDatabaseButton;
	if(DoButton_Menu(&s_SkinDatabaseButton, Localize("Skin Database"), 0, &DatabaseButton))
	{
		Client()->ViewLink("https://ddnet.org/skins/");
	}

	static CButtonContainer s_DirectoryButton;
	if(DoButton_Menu(&s_DirectoryButton, Localize("Skins directory"), 0, &DirectoryButton))
	{
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, "skins", aBuf, sizeof(aBuf));
		Storage()->CreateFolder("skins", IStorage::TYPE_SAVE);
		Client()->ViewFile(aBuf);
	}
	GameClient()->m_Tooltips.DoToolTip(&s_DirectoryButton, &DirectoryButton, Localize("Open the directory to add custom skins"));

	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	static CButtonContainer s_SkinRefreshButton;
	if(DoButton_Menu(&s_SkinRefreshButton, FontIcon::ARROW_ROTATE_RIGHT, 0, &RefreshButton) || Input()->KeyPress(KEY_F5) || (Input()->KeyPress(KEY_R) && Input()->ModifierIsPressed()))
	{
		ShouldRefresh = true;
	}
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

	if(ShouldRefresh)
	{
		GameClient()->RefreshSkins(CSkinDescriptor::FLAG_SIX);
	}
}

void CMenus::RenderSettingsGraphics(CUIRect MainView)
{
	CUIRect Button;
	char aBuf[128];
	bool CheckSettings = false;

	static const int MAX_RESOLUTIONS = 256;
	static CVideoMode s_aModes[MAX_RESOLUTIONS];
	static int s_NumNodes = Graphics()->GetVideoModes(s_aModes, MAX_RESOLUTIONS, g_Config.m_GfxScreen);
	static int s_GfxFsaaSamples = g_Config.m_GfxFsaaSamples;
	static bool s_GfxBackendChanged = false;
	static bool s_GfxGpuChanged = false;

	static int s_InitDisplayAllVideoModes = g_Config.m_GfxDisplayAllVideoModes;

	static bool s_WasInit = false;
	static bool s_ModesReload = false;
	if(!s_WasInit)
	{
		s_WasInit = true;

		Graphics()->AddWindowPropChangeListener([]() {
			s_ModesReload = true;
		});
	}

	if(s_ModesReload || g_Config.m_GfxDisplayAllVideoModes != s_InitDisplayAllVideoModes)
	{
		s_NumNodes = Graphics()->GetVideoModes(s_aModes, MAX_RESOLUTIONS, g_Config.m_GfxScreen);
		s_ModesReload = false;
		s_InitDisplayAllVideoModes = g_Config.m_GfxDisplayAllVideoModes;
	}

	CUIRect ModeList, ModeLabel;
	MainView.VSplitLeft(350.0f, &MainView, &ModeList);
	ModeList.HSplitTop(24.0f, &ModeLabel, &ModeList);
	MainView.VSplitLeft(340.0f, &MainView, nullptr);

	// display mode list
	static CListBox s_ListBox;
	static const float sc_RowHeightResList = 22.0f;
	static const float sc_FontSizeResListHeader = 12.0f;
	static const float sc_FontSizeResList = 10.0f;

	{
		int G = std::gcd(g_Config.m_GfxScreenWidth, g_Config.m_GfxScreenHeight);
		str_format(aBuf, sizeof(aBuf), "%s: %dx%d @%dhz %d bit (%d:%d)", Localize("Current"), (int)(g_Config.m_GfxScreenWidth * Graphics()->ScreenHiDPIScale()), (int)(g_Config.m_GfxScreenHeight * Graphics()->ScreenHiDPIScale()), g_Config.m_GfxScreenRefreshRate, g_Config.m_GfxColorDepth, g_Config.m_GfxScreenWidth / G, g_Config.m_GfxScreenHeight / G);
		Ui()->DoLabel(&ModeLabel, aBuf, sc_FontSizeResListHeader, TEXTALIGN_MC);
	}

	int SelectedOld = -1;
	s_ListBox.SetActive(!Ui()->IsPopupOpen());
	s_ListBox.DoStart(sc_RowHeightResList, s_NumNodes, 1, 3, SelectedOld, &ModeList);

	for(int i = 0; i < s_NumNodes; ++i)
	{
		const int Depth = s_aModes[i].m_Red + s_aModes[i].m_Green + s_aModes[i].m_Blue > 16 ? 24 : 16;
		if(g_Config.m_GfxColorDepth == Depth &&
			g_Config.m_GfxScreenWidth == s_aModes[i].m_WindowWidth &&
			g_Config.m_GfxScreenHeight == s_aModes[i].m_WindowHeight &&
			g_Config.m_GfxScreenRefreshRate == s_aModes[i].m_RefreshRate)
		{
			SelectedOld = i;
		}

		const CListboxItem Item = s_ListBox.DoNextItem(&s_aModes[i], SelectedOld == i);
		if(!Item.m_Visible)
			continue;

		int G = std::gcd(s_aModes[i].m_WindowWidth, s_aModes[i].m_WindowHeight);
		str_format(aBuf, sizeof(aBuf), " %dx%d @%dhz %d bit (%d:%d)", s_aModes[i].m_CanvasWidth, s_aModes[i].m_CanvasHeight, s_aModes[i].m_RefreshRate, Depth, s_aModes[i].m_WindowWidth / G, s_aModes[i].m_WindowHeight / G);
		Ui()->DoLabel(&Item.m_Rect, aBuf, sc_FontSizeResList, TEXTALIGN_ML);
	}

	const int NewSelected = s_ListBox.DoEnd();
	if(SelectedOld != NewSelected)
	{
		const int Depth = s_aModes[NewSelected].m_Red + s_aModes[NewSelected].m_Green + s_aModes[NewSelected].m_Blue > 16 ? 24 : 16;
		g_Config.m_GfxColorDepth = Depth;
		g_Config.m_GfxScreenWidth = s_aModes[NewSelected].m_WindowWidth;
		g_Config.m_GfxScreenHeight = s_aModes[NewSelected].m_WindowHeight;
		g_Config.m_GfxScreenRefreshRate = s_aModes[NewSelected].m_RefreshRate;
		Graphics()->ResizeToScreen();
	}

	// switches
	CUIRect WindowModeDropDown;
	MainView.HSplitTop(20.0f, &WindowModeDropDown, &MainView);

	const char *apWindowModes[] = {Localize("Windowed"), Localize("Windowed borderless"), Localize("Windowed fullscreen"), Localize("Desktop fullscreen"), Localize("Fullscreen")};
	static const int s_NumWindowMode = std::size(apWindowModes);

	const int OldWindowMode = (g_Config.m_GfxFullscreen ? (g_Config.m_GfxFullscreen == 1 ? 4 : (g_Config.m_GfxFullscreen == 2 ? 3 : 2)) : (g_Config.m_GfxBorderless ? 1 : 0));

	static CUi::SDropDownState s_WindowModeDropDownState;
	static CScrollRegion s_WindowModeDropDownScrollRegion;
	s_WindowModeDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_WindowModeDropDownScrollRegion;
	const int NewWindowMode = Ui()->DoDropDown(&WindowModeDropDown, OldWindowMode, apWindowModes, s_NumWindowMode, s_WindowModeDropDownState);
	if(OldWindowMode != NewWindowMode)
	{
		if(NewWindowMode == 0)
			Graphics()->SetWindowParams(0, false);
		else if(NewWindowMode == 1)
			Graphics()->SetWindowParams(0, true);
		else if(NewWindowMode == 2)
			Graphics()->SetWindowParams(3, false);
		else if(NewWindowMode == 3)
			Graphics()->SetWindowParams(2, false);
		else if(NewWindowMode == 4)
			Graphics()->SetWindowParams(1, false);
	}

	if(Graphics()->GetNumScreens() > 1)
	{
		CUIRect ScreenDropDown;
		MainView.HSplitTop(2.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &ScreenDropDown, &MainView);

		const int NumScreens = Graphics()->GetNumScreens();
		static std::vector<std::string> s_vScreenNames;
		static std::vector<const char *> s_vpScreenNames;
		s_vScreenNames.resize(NumScreens);
		s_vpScreenNames.resize(NumScreens);

		for(int i = 0; i < NumScreens; ++i)
		{
			str_format(aBuf, sizeof(aBuf), "%s %d: %s", Localize("Screen"), i, Graphics()->GetScreenName(i));
			s_vScreenNames[i] = aBuf;
			s_vpScreenNames[i] = s_vScreenNames[i].c_str();
		}

		static CUi::SDropDownState s_ScreenDropDownState;
		static CScrollRegion s_ScreenDropDownScrollRegion;
		s_ScreenDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_ScreenDropDownScrollRegion;
		const int NewScreen = Ui()->DoDropDown(&ScreenDropDown, g_Config.m_GfxScreen, s_vpScreenNames.data(), s_vpScreenNames.size(), s_ScreenDropDownState);
		if(NewScreen != g_Config.m_GfxScreen)
			Graphics()->SwitchWindowScreen(NewScreen, true);
	}

	MainView.HSplitTop(2.0f, nullptr, &MainView);
	MainView.HSplitTop(20.0f, &Button, &MainView);
	str_format(aBuf, sizeof(aBuf), "%s (%s)", Localize("V-Sync"), Localize("may cause delay"));
	if(DoButton_CheckBox(&g_Config.m_GfxVsync, aBuf, g_Config.m_GfxVsync, &Button))
	{
		Graphics()->SetVSync(!g_Config.m_GfxVsync);
	}

	bool MultiSamplingChanged = false;
	MainView.HSplitTop(20.0f, &Button, &MainView);
	str_format(aBuf, sizeof(aBuf), "%s (%s)", Localize("FSAA samples"), Localize("may cause delay"));
	int GfxFsaaSamplesMouseButton = DoButton_CheckBox_Number(&g_Config.m_GfxFsaaSamples, aBuf, g_Config.m_GfxFsaaSamples, &Button);
	int CurFSAA = g_Config.m_GfxFsaaSamples == 0 ? 1 : g_Config.m_GfxFsaaSamples;
	if(GfxFsaaSamplesMouseButton == 1) // inc
	{
		g_Config.m_GfxFsaaSamples = std::pow(2, (int)std::log2(CurFSAA) + 1);
		if(g_Config.m_GfxFsaaSamples > 64)
			g_Config.m_GfxFsaaSamples = 0;
		MultiSamplingChanged = true;
	}
	else if(GfxFsaaSamplesMouseButton == 2) // dec
	{
		if(CurFSAA == 1)
			g_Config.m_GfxFsaaSamples = 64;
		else if(CurFSAA == 2)
			g_Config.m_GfxFsaaSamples = 0;
		else
			g_Config.m_GfxFsaaSamples = std::pow(2, (int)std::log2(CurFSAA) - 1);
		MultiSamplingChanged = true;
	}

	uint32_t MultiSamplingCountBackend = 0;
	if(MultiSamplingChanged)
	{
		if(Graphics()->SetMultiSampling(g_Config.m_GfxFsaaSamples, MultiSamplingCountBackend))
		{
			// try again with 0 if mouse click was increasing multi sampling
			// else just accept the current value as is
			if((uint32_t)g_Config.m_GfxFsaaSamples > MultiSamplingCountBackend && GfxFsaaSamplesMouseButton == 1)
				Graphics()->SetMultiSampling(0, MultiSamplingCountBackend);
			g_Config.m_GfxFsaaSamples = (int)MultiSamplingCountBackend;
		}
		else
		{
			CheckSettings = true;
		}
	}

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_GfxHighDetail, Localize("High Detail"), g_Config.m_GfxHighDetail, &Button))
		g_Config.m_GfxHighDetail ^= 1;
	GameClient()->m_Tooltips.DoToolTip(&g_Config.m_GfxHighDetail, &Button, Localize("Allows maps to render with more detail"));

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_ClShowfps, Localize("Show FPS"), g_Config.m_ClShowfps, &Button))
		g_Config.m_ClShowfps ^= 1;
	GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ClShowfps, &Button, Localize("Renders your frame rate in the top right"));

	MainView.HSplitTop(20.0f, &Button, &MainView);
	str_copy(aBuf, " ");
	str_append(aBuf, Localize("Hz", "Hertz"));
	Ui()->DoScrollbarOption(&g_Config.m_GfxRefreshRate, &g_Config.m_GfxRefreshRate, &Button, Localize("Refresh Rate"), 10, 1000, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_INFINITE | CUi::SCROLLBAR_OPTION_NOCLAMPVALUE | CUi::SCROLLBAR_OPTION_DELAYUPDATE, aBuf);

	MainView.HSplitTop(2.0f, nullptr, &MainView);
	static CButtonContainer s_UiColorResetId;
	DoLine_ColorPicker(&s_UiColorResetId, 25.0f, 13.0f, 2.0f, &MainView, Localize("UI Color"), &g_Config.m_UiColor, color_cast<ColorRGBA>(ColorHSLA(0xE4A046AFU, true)), false, nullptr, true);

	// Backend list
	struct SMenuBackendInfo
	{
		int m_Major = 0;
		int m_Minor = 0;
		int m_Patch = 0;
		const char *m_pBackendName = "";
		bool m_Found = false;
	};
	std::array<std::array<SMenuBackendInfo, EGraphicsDriverAgeType::GRAPHICS_DRIVER_AGE_TYPE_COUNT>, EBackendType::BACKEND_TYPE_COUNT> aaSupportedBackends{};
	uint32_t FoundBackendCount = 0;
	for(uint32_t i = 0; i < BACKEND_TYPE_COUNT; ++i)
	{
		if(EBackendType(i) == BACKEND_TYPE_AUTO)
			continue;
		for(uint32_t n = 0; n < GRAPHICS_DRIVER_AGE_TYPE_COUNT; ++n)
		{
			auto &Info = aaSupportedBackends[i][n];
			if(Graphics()->GetDriverVersion(EGraphicsDriverAgeType(n), Info.m_Major, Info.m_Minor, Info.m_Patch, Info.m_pBackendName, EBackendType(i)))
			{
				// don't count blocked opengl drivers
				if(EBackendType(i) != BACKEND_TYPE_OPENGL || EGraphicsDriverAgeType(n) == GRAPHICS_DRIVER_AGE_TYPE_LEGACY || g_Config.m_GfxDriverIsBlocked == 0)
				{
					Info.m_Found = true;
					++FoundBackendCount;
				}
			}
		}
	}

	if(FoundBackendCount > 1)
	{
		CUIRect Text, BackendDropDown;
		MainView.HSplitTop(10.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &Text, &MainView);
		MainView.HSplitTop(2.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &BackendDropDown, &MainView);
		Ui()->DoLabel(&Text, Localize("Renderer"), 16.0f, TEXTALIGN_MC);

		static std::vector<std::string> s_vBackendIdNames;
		static std::vector<const char *> s_vpBackendIdNamesCStr;
		static std::vector<SMenuBackendInfo> s_vBackendInfos;

		size_t BackendCount = FoundBackendCount + 1;
		s_vBackendIdNames.resize(BackendCount);
		s_vpBackendIdNamesCStr.resize(BackendCount);
		s_vBackendInfos.resize(BackendCount);

		char aTmpBackendName[256];

		auto IsInfoDefault = [](const SMenuBackendInfo &CheckInfo) {
			return str_comp_nocase(CheckInfo.m_pBackendName, DefaultConfig::GfxBackend) == 0 && CheckInfo.m_Major == DefaultConfig::GfxGLMajor && CheckInfo.m_Minor == DefaultConfig::GfxGLMinor && CheckInfo.m_Patch == DefaultConfig::GfxGLPatch;
		};

		int SelectedOldBackend = -1;
		uint32_t CurCounter = 0;
		for(uint32_t i = 0; i < BACKEND_TYPE_COUNT; ++i)
		{
			for(uint32_t n = 0; n < GRAPHICS_DRIVER_AGE_TYPE_COUNT; ++n)
			{
				auto &Info = aaSupportedBackends[i][n];
				if(Info.m_Found)
				{
					bool IsDefault = IsInfoDefault(Info);
					str_format(aTmpBackendName, sizeof(aTmpBackendName), "%s (%d.%d.%d)%s%s", Info.m_pBackendName, Info.m_Major, Info.m_Minor, Info.m_Patch, IsDefault ? " - " : "", IsDefault ? Localize("default") : "");
					s_vBackendIdNames[CurCounter] = aTmpBackendName;
					s_vpBackendIdNamesCStr[CurCounter] = s_vBackendIdNames[CurCounter].c_str();
					if(str_comp_nocase(Info.m_pBackendName, g_Config.m_GfxBackend) == 0 && g_Config.m_GfxGLMajor == Info.m_Major && g_Config.m_GfxGLMinor == Info.m_Minor && g_Config.m_GfxGLPatch == Info.m_Patch)
					{
						SelectedOldBackend = CurCounter;
					}

					s_vBackendInfos[CurCounter] = Info;
					++CurCounter;
				}
			}
		}

		if(SelectedOldBackend != -1)
		{
			// no custom selected
			BackendCount -= 1;
		}
		else
		{
			// custom selected one
			str_format(aTmpBackendName, sizeof(aTmpBackendName), "%s (%s %d.%d.%d)", Localize("custom"), g_Config.m_GfxBackend, g_Config.m_GfxGLMajor, g_Config.m_GfxGLMinor, g_Config.m_GfxGLPatch);
			s_vBackendIdNames[CurCounter] = aTmpBackendName;
			s_vpBackendIdNamesCStr[CurCounter] = s_vBackendIdNames[CurCounter].c_str();
			SelectedOldBackend = CurCounter;

			s_vBackendInfos[CurCounter].m_pBackendName = "custom";
			s_vBackendInfos[CurCounter].m_Major = g_Config.m_GfxGLMajor;
			s_vBackendInfos[CurCounter].m_Minor = g_Config.m_GfxGLMinor;
			s_vBackendInfos[CurCounter].m_Patch = g_Config.m_GfxGLPatch;
		}

		static int s_SelectedOldBackend = -1;
		if(s_SelectedOldBackend == -1)
			s_SelectedOldBackend = SelectedOldBackend;

		static CUi::SDropDownState s_BackendDropDownState;
		static CScrollRegion s_BackendDropDownScrollRegion;
		s_BackendDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_BackendDropDownScrollRegion;
		const int NewBackend = Ui()->DoDropDown(&BackendDropDown, SelectedOldBackend, s_vpBackendIdNamesCStr.data(), BackendCount, s_BackendDropDownState);
		if(SelectedOldBackend != NewBackend)
		{
			str_copy(g_Config.m_GfxBackend, s_vBackendInfos[NewBackend].m_pBackendName);
			g_Config.m_GfxGLMajor = s_vBackendInfos[NewBackend].m_Major;
			g_Config.m_GfxGLMinor = s_vBackendInfos[NewBackend].m_Minor;
			g_Config.m_GfxGLPatch = s_vBackendInfos[NewBackend].m_Patch;

			CheckSettings = true;
			s_GfxBackendChanged = s_SelectedOldBackend != NewBackend;
		}
	}

	// GPU list
	const auto &GpuList = Graphics()->GetGpus();
	if(GpuList.m_vGpus.size() > 1)
	{
		CUIRect Text, GpuDropDown;
		MainView.HSplitTop(10.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &Text, &MainView);
		MainView.HSplitTop(2.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &GpuDropDown, &MainView);
		Ui()->DoLabel(&Text, Localize("Graphics card"), 16.0f, TEXTALIGN_MC);

		static std::vector<const char *> s_vpGpuIdNames;

		size_t GpuCount = GpuList.m_vGpus.size() + 1;
		s_vpGpuIdNames.resize(GpuCount);

		char aCurDeviceName[256 + 4];

		int OldSelectedGpu = -1;
		for(size_t i = 0; i < GpuCount; ++i)
		{
			if(i == 0)
			{
				str_format(aCurDeviceName, sizeof(aCurDeviceName), "%s (%s)", Localize("auto"), GpuList.m_AutoGpu.m_aName);
				s_vpGpuIdNames[i] = aCurDeviceName;
				if(str_comp("auto", g_Config.m_GfxGpuName) == 0)
				{
					OldSelectedGpu = 0;
				}
			}
			else
			{
				s_vpGpuIdNames[i] = GpuList.m_vGpus[i - 1].m_aName;
				if(str_comp(GpuList.m_vGpus[i - 1].m_aName, g_Config.m_GfxGpuName) == 0)
				{
					OldSelectedGpu = i;
				}
			}
		}

		static int s_OldSelectedGpu = -1;
		if(s_OldSelectedGpu == -1)
			s_OldSelectedGpu = OldSelectedGpu;

		static CUi::SDropDownState s_GpuDropDownState;
		static CScrollRegion s_GpuDropDownScrollRegion;
		s_GpuDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_GpuDropDownScrollRegion;
		const int NewGpu = Ui()->DoDropDown(&GpuDropDown, OldSelectedGpu, s_vpGpuIdNames.data(), GpuCount, s_GpuDropDownState);
		if(OldSelectedGpu != NewGpu)
		{
			if(NewGpu == 0)
				str_copy(g_Config.m_GfxGpuName, "auto");
			else
				str_copy(g_Config.m_GfxGpuName, GpuList.m_vGpus[NewGpu - 1].m_aName);
			CheckSettings = true;
			s_GfxGpuChanged = NewGpu != s_OldSelectedGpu;
		}
	}

	// check if the new settings require a restart
	if(CheckSettings)
	{
		m_NeedRestartGraphics = !(s_GfxFsaaSamples == g_Config.m_GfxFsaaSamples &&
					  !s_GfxBackendChanged &&
					  !s_GfxGpuChanged);
	}
}

void CMenus::RenderSettingsSound(CUIRect MainView)
{
	static int s_SndEnable = g_Config.m_SndEnable;

	CUIRect Button;
	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndEnable, Localize("Use sounds"), g_Config.m_SndEnable, &Button))
	{
		g_Config.m_SndEnable ^= 1;
		UpdateMusicState();
		m_NeedRestartSound = g_Config.m_SndEnable && !s_SndEnable;
	}

	if(!g_Config.m_SndEnable)
		return;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndMusic, Localize("Play background music"), g_Config.m_SndMusic, &Button))
	{
		g_Config.m_SndMusic ^= 1;
		UpdateMusicState();
	}

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndNonactiveMute, Localize("Mute when not active"), g_Config.m_SndNonactiveMute, &Button))
		g_Config.m_SndNonactiveMute ^= 1;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndGame, Localize("Enable game sounds"), g_Config.m_SndGame, &Button))
		g_Config.m_SndGame ^= 1;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndGun, Localize("Enable gun sound"), g_Config.m_SndGun, &Button))
		g_Config.m_SndGun ^= 1;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndLongPain, Localize("Enable long pain sound (used when shooting in freeze)"), g_Config.m_SndLongPain, &Button))
		g_Config.m_SndLongPain ^= 1;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndServerMessage, Localize("Enable server message sound"), g_Config.m_SndServerMessage, &Button))
		g_Config.m_SndServerMessage ^= 1;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndChat, Localize("Enable regular chat sound"), g_Config.m_SndChat, &Button))
		g_Config.m_SndChat ^= 1;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndTeamChat, Localize("Enable team chat sound"), g_Config.m_SndTeamChat, &Button))
		g_Config.m_SndTeamChat ^= 1;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndHighlight, Localize("Enable highlighted chat sound"), g_Config.m_SndHighlight, &Button))
		g_Config.m_SndHighlight ^= 1;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_BcMenuSfx, Localize("Enable menu UI sounds"), g_Config.m_BcMenuSfx, &Button))
		g_Config.m_BcMenuSfx ^= 1;

	{
		MainView.HSplitTop(5.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcMenuSfxVolume, &g_Config.m_BcMenuSfxVolume, &Button, Localize("SFX sound volume"), 0, 100, &CUi::ms_LogarithmicScrollbarScale, 0u, "%");
	}

	// volume slider
	{
		MainView.HSplitTop(5.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_SndVolume, &g_Config.m_SndVolume, &Button, Localize("Sound volume"), 0, 100, &CUi::ms_LogarithmicScrollbarScale, 0u, "%");
	}

	// volume slider game sounds
	{
		MainView.HSplitTop(5.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_SndGameVolume, &g_Config.m_SndGameVolume, &Button, Localize("Game sound volume"), 0, 100, &CUi::ms_LogarithmicScrollbarScale, 0u, "%");
	}

	// volume slider gui sounds
	{
		MainView.HSplitTop(5.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_SndChatVolume, &g_Config.m_SndChatVolume, &Button, Localize("Chat sound volume"), 0, 100, &CUi::ms_LogarithmicScrollbarScale, 0u, "%");
	}

	// volume slider map sounds
	{
		MainView.HSplitTop(5.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_SndMapVolume, &g_Config.m_SndMapVolume, &Button, Localize("Map sound volume"), 0, 100, &CUi::ms_LogarithmicScrollbarScale, 0u, "%");
	}

	// volume slider background music
	{
		MainView.HSplitTop(5.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_SndBackgroundMusicVolume, &g_Config.m_SndBackgroundMusicVolume, &Button, Localize("Background music volume"), 0, 100, &CUi::ms_LogarithmicScrollbarScale, 0u, "%");
	}
}

void CMenus::RenderLanguageSettings(CUIRect MainView)
{
	const float CreditsFontSize = 14.0f;
	const float CreditsMargin = 10.0f;

	CUIRect List, CreditsScroll;
	MainView.HSplitBottom(4.0f * CreditsFontSize + 2.0f * CreditsMargin + CScrollRegion::HEIGHT_MAGIC_FIX, &List, &CreditsScroll);
	List.HSplitBottom(5.0f, &List, nullptr);

	RenderLanguageSelection(List);

	CreditsScroll.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 5.0f);

	static CScrollRegion s_CreditsScrollRegion;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = CreditsFontSize;
	s_CreditsScrollRegion.Begin(&CreditsScroll, &ScrollOffset, &ScrollParams);
	CreditsScroll.y += ScrollOffset.y;

	CTextCursor Cursor;
	Cursor.m_FontSize = CreditsFontSize;
	Cursor.m_LineWidth = CreditsScroll.w - 2.0f * CreditsMargin;

	const unsigned OldRenderFlags = TextRender()->GetRenderFlags();
	TextRender()->SetRenderFlags(OldRenderFlags | TEXT_RENDER_FLAG_ONE_TIME_USE);
	STextContainerIndex CreditsTextContainer;
	TextRender()->CreateTextContainer(CreditsTextContainer, &Cursor, Localize("English translation by the DDNet Team", "Translation credits: Add your own name here when you update translations"));
	TextRender()->SetRenderFlags(OldRenderFlags);
	if(CreditsTextContainer.Valid())
	{
		CUIRect CreditsLabel;
		CreditsScroll.HSplitTop(TextRender()->GetBoundingBoxTextContainer(CreditsTextContainer).m_H + 2.0f * CreditsMargin, &CreditsLabel, &CreditsScroll);
		s_CreditsScrollRegion.AddRect(CreditsLabel);
		CreditsLabel.Margin(CreditsMargin, &CreditsLabel);
		TextRender()->RenderTextContainer(CreditsTextContainer, TextRender()->DefaultTextColor(), TextRender()->DefaultTextOutlineColor(), CreditsLabel.x, CreditsLabel.y);
		TextRender()->DeleteTextContainer(CreditsTextContainer);
	}

	s_CreditsScrollRegion.End();
}

bool CMenus::RenderLanguageSelection(CUIRect MainView)
{
	static int s_SelectedLanguage = -2; // -2 = unloaded, -1 = unset
	static CListBox s_ListBox;

	if(s_SelectedLanguage == -2)
	{
		s_SelectedLanguage = -1;
		for(size_t i = 0; i < g_Localization.Languages().size(); i++)
		{
			if(str_comp(g_Localization.Languages()[i].m_Filename.c_str(), g_Config.m_ClLanguagefile) == 0)
			{
				s_SelectedLanguage = i;
				s_ListBox.ScrollToSelected();
				break;
			}
		}
	}

	const int SelectedOld = s_SelectedLanguage;

	s_ListBox.DoStart(24.0f, g_Localization.Languages().size(), 1, 3, s_SelectedLanguage, &MainView);

	for(const auto &Language : g_Localization.Languages())
	{
		const CListboxItem Item = s_ListBox.DoNextItem(&Language.m_Name, s_SelectedLanguage != -1 && !str_comp(g_Localization.Languages()[s_SelectedLanguage].m_Name.c_str(), Language.m_Name.c_str()));
		if(!Item.m_Visible)
			continue;

		CUIRect FlagRect, Label;
		Item.m_Rect.VSplitLeft(Item.m_Rect.h * 2.0f, &FlagRect, &Label);
		FlagRect.VMargin(6.0f, &FlagRect);
		FlagRect.HMargin(3.0f, &FlagRect);
		GameClient()->m_CountryFlags.Render(Language.m_CountryCode, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), FlagRect.x, FlagRect.y, FlagRect.w, FlagRect.h);

		Ui()->DoLabel(&Label, Language.m_Name.c_str(), 16.0f, TEXTALIGN_ML);
	}

	s_SelectedLanguage = s_ListBox.DoEnd();

	if(SelectedOld != s_SelectedLanguage)
	{
		str_copy(g_Config.m_ClLanguagefile, g_Localization.Languages()[s_SelectedLanguage].m_Filename.c_str());
		GameClient()->OnLanguageChange();
	}

	return s_ListBox.WasItemActivated();
}

void CMenus::RenderSettings(CUIRect MainView)
{
	const bool BestClientPageVisible = g_Config.m_UiSettingsPage == SETTINGS_BESTCLIENT;
	if(!BestClientPageVisible)
		SetBestClientShopVisible(false);
	if(!BestClientPageVisible && (m_AssetsEditorState.m_VisualsEditorOpen || m_AssetsEditorState.m_VisualsEditorInitialized))
		m_AssetsEditorState.m_VisualsEditorOpen = false;
	if(!BestClientPageVisible && m_ComponentsEditorState.m_Open)
		ComponentsEditorCloseNow();

	if(g_Config.m_UiSettingsPage == SETTINGS_BESTCLIENT && m_AssetsEditorState.m_VisualsEditorOpen && m_AssetsEditorState.m_FullscreenOpen)
	{
		SetBestClientShopVisible(false);
		RenderSettingsBestClient(MainView);
		return;
	}
	if(g_Config.m_UiSettingsPage == SETTINGS_BESTCLIENT && m_ComponentsEditorState.m_Open && m_ComponentsEditorState.m_FullscreenOpen)
	{
		SetBestClientShopVisible(false);
		RenderSettingsBestClient(MainView);
		return;
	}

	const bool NeedRestart = m_NeedRestartGraphics || m_NeedRestartSound || m_NeedRestartUpdate;
	auto RenderSettingsPage = [&](CUIRect PageView) {
		if(g_Config.m_UiSettingsPage == SETTINGS_LANGUAGE)
		{
			GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_LANGUAGE);
			RenderLanguageSettings(PageView);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_GENERAL)
		{
			GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_GENERAL);
			RenderSettingsGeneral(PageView);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_PLAYER)
		{
			GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_PLAYER);
			RenderSettingsPlayer(PageView);
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
			RenderSettingsCustom(PageView);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_TCLIENT)
		{
			GameClient()->m_MenuBackground.ChangePosition(13);
			RenderSettingsTClient(PageView);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_BESTCLIENT)
		{
			GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_RESERVED0);
			RenderSettingsBestClient(PageView);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_PROFILES)
		{
			GameClient()->m_MenuBackground.ChangePosition(14);
			if(GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_TCLIENT_PROFILES_PAGE))
				g_Config.m_UiSettingsPage = SETTINGS_TCLIENT;
			else
				RenderSettingsTClientProfiles(PageView);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_CONFIGS)
		{
			GameClient()->m_MenuBackground.ChangePosition(15);
			if(GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_TCLIENT_CONFIGS_PAGE))
				g_Config.m_UiSettingsPage = SETTINGS_TCLIENT;
			else
				RenderSettingsTClientConfigs(PageView);
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
			TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
			Ui()->DoLabel(&RestartWarning, Localize("DDNet Client needs to be restarted to complete update!"), 14.0f, TEXTALIGN_ML);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		}
		else
		{
			Ui()->DoLabel(&RestartWarning, Localize("You must restart the game for all settings to take effect."), 14.0f, TEXTALIGN_ML);
		}

		static CButtonContainer s_RestartButton;
		if(DoButton_Menu(&s_RestartButton, Localize("Restart"), 0, &RestartButton))
		{
			if(Client()->State() == IClient::STATE_ONLINE || GameClient()->Editor()->HasUnsavedData())
				m_Popup = POPUP_RESTART;
			else
				Client()->Restart();
		}
	};

	auto RenderSettingsPageNewLayout = [&](CUIRect PageView) {
		const int Page = g_Config.m_UiSettingsPage;
		const bool NeedsAutoScroll =
			Page == SETTINGS_GENERAL ||
			Page == SETTINGS_APPEARANCE;

		if(!NeedsAutoScroll)
		{
			RenderSettingsPage(PageView);
			return;
		}

		static CScrollRegion s_aNewLayoutScrollRegions[SETTINGS_LENGTH];
		CScrollRegionParams ScrollParams;
		ScrollParams.m_ScrollUnit = 60.0f;
		ScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
		ScrollParams.m_ScrollbarMargin = 5.0f;

		vec2 ScrollOffset(0.0f, 0.0f);
		CScrollRegion &ScrollRegion = s_aNewLayoutScrollRegions[Page];
		ScrollRegion.Begin(&PageView, &ScrollOffset, &ScrollParams);

		CUIRect ContentView = PageView;
		ContentView.y += ScrollOffset.y;
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

	if(g_Config.m_UiSettingsPage == SETTINGS_LANGUAGE)
		g_Config.m_UiSettingsPage = SETTINGS_GENERAL;
	if(g_Config.m_UiSettingsPage == SETTINGS_PLAYER)
		g_Config.m_UiSettingsPage = SETTINGS_TEE;

	g_Config.m_BcSettingsLayout = minimum(maximum(g_Config.m_BcSettingsLayout, 0), 1);

	if(g_Config.m_BcSettingsLayout == 0)
	{
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
			"BestClient",
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

	// old settings layout
	CUIRect Button, TabBar, RestartBar;
	MainView.VSplitRight(120.0f, &MainView, &TabBar);
	MainView.Draw(ms_ColorTabbarActive, IGraphics::CORNER_B, 10.0f);
	MainView.Margin(20.0f, &MainView);

	if(NeedRestart)
	{
		MainView.HSplitBottom(20.0f, &MainView, &RestartBar);
		MainView.HSplitBottom(10.0f, &MainView, nullptr);
	}

	TabBar.HSplitTop(50.0f, &Button, &TabBar);
	Button.Draw(ms_ColorTabbarActive, IGraphics::CORNER_BR, 10.0f);

	static const int s_aVisibleSettingsPages[] = {
		SETTINGS_GENERAL,
		SETTINGS_TEE,
		SETTINGS_APPEARANCE,
		SETTINGS_CONTROLS,
		SETTINGS_GRAPHICS,
		SETTINGS_SOUND,
		SETTINGS_DDNET,
		SETTINGS_ASSETS,
		SETTINGS_TCLIENT,
		SETTINGS_BESTCLIENT,
		SETTINGS_PROFILES,
		SETTINGS_CONFIGS,
	};

	const char *apTabs[] = {
		Localize("General"),
		Client()->IsSixup() ? "Tee 0.7" : Localize("Tee"),
		Localize("Appearance"),
		Localize("Controls"),
		Localize("Graphics"),
		Localize("Sound"),
		Localize("DDNet"),
		Localize("Assets"),
		TCLocalize("TClient"),
		"BestClient",
		Localize("Profiles"),
		Localize("Configs")};

	static_assert(std::size(s_aVisibleSettingsPages) == std::size(apTabs));
	static CButtonContainer s_aTabButtons[std::size(s_aVisibleSettingsPages)];

	for(size_t i = 0; i < std::size(s_aVisibleSettingsPages); i++)
	{
		const int Page = s_aVisibleSettingsPages[i];
		TabBar.HSplitTop(10.0f, nullptr, &TabBar);
		TabBar.HSplitTop(26.0f, &Button, &TabBar);
		if(DoButton_MenuTab(&s_aTabButtons[i], apTabs[i], g_Config.m_UiSettingsPage == Page, &Button, IGraphics::CORNER_R, &m_aAnimatorsSettingsTab[Page]))
			g_Config.m_UiSettingsPage = Page;
	}

	RenderSettingsPage(MainView);
	if(NeedRestart)
		RenderRestartWarning(RestartBar);
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

enum
{
	APPEARANCE_TAB_HUD = 0,
	APPEARANCE_TAB_CHAT = 1,
	APPEARANCE_TAB_NAME_PLATE = 2,
	APPEARANCE_TAB_HOOK_COLLISION = 3,
	APPEARANCE_TAB_INFO_MESSAGES = 4,
	APPEARANCE_TAB_LASER = 5,
	NUMBER_OF_APPEARANCE_TABS = 6,
};

void CMenus::RenderSettingsAppearance(CUIRect MainView)
{
	char aBuf[128];
	static int s_CurTab = 0;

	CUIRect TabBar, LeftView, RightView, Button;

	MainView.HSplitTop(20.0f, &TabBar, &MainView);
	const float TabWidth = TabBar.w / (float)NUMBER_OF_APPEARANCE_TABS;
	static CButtonContainer s_aPageTabs[NUMBER_OF_APPEARANCE_TABS] = {};
	const char *apTabNames[NUMBER_OF_APPEARANCE_TABS] = {
		Localize("HUD"),
		Localize("Chat"),
		Localize("Name Plate"),
		Localize("Hook Collisions"),
		Localize("Info Messages"),
		Localize("Laser")};

	for(int Tab = APPEARANCE_TAB_HUD; Tab < NUMBER_OF_APPEARANCE_TABS; ++Tab)
	{
		TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
		const int Corners = Tab == APPEARANCE_TAB_HUD ? IGraphics::CORNER_L : (Tab == NUMBER_OF_APPEARANCE_TABS - 1 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
		if(DoButton_MenuTab(&s_aPageTabs[Tab], apTabNames[Tab], s_CurTab == Tab, &Button, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
		{
			s_CurTab = Tab;
		}
	}

	MainView.HSplitTop(10.0f, nullptr, &MainView);

	const float LineSize = 20.0f;
	const float ColorPickerLineSize = 25.0f;
	const float HeadlineFontSize = 20.0f;
	const float HeadlineHeight = 30.0f;
	const float MarginSmall = 5.0f;
	const float MarginBetweenViews = 20.0f;

	const float ColorPickerLabelSize = 13.0f;
	const float ColorPickerLineSpacing = 5.0f;

	if(s_CurTab == APPEARANCE_TAB_HUD)
	{
		MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);

		// ***** HUD ***** //
		Ui()->DoLabel_AutoLineSize(Localize("HUD"), HeadlineFontSize,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

		// Switch of the entire HUD
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowhud, Localize("Show ingame HUD"), &g_Config.m_ClShowhud, &LeftView, LineSize);

		// Switches of the various normal HUD elements
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowhudHealthAmmo, Localize("Show health, shields and ammo"), &g_Config.m_ClShowhudHealthAmmo, &LeftView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowhudScore, Localize("Show score"), &g_Config.m_ClShowhudScore, &LeftView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowLocalTimeAlways, Localize("Show local time always"), &g_Config.m_ClShowLocalTimeAlways, &LeftView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClSpecCursor, Localize("Show spectator cursor"), &g_Config.m_ClSpecCursor, &LeftView, LineSize);

		// Settings of the HUD element for votes
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowVotesAfterVoting, Localize("Show votes window after voting"), &g_Config.m_ClShowVotesAfterVoting, &LeftView, LineSize);

		// ***** Scoreboard ***** //
		LeftView.HSplitTop(MarginBetweenViews, nullptr, &LeftView);
		Ui()->DoLabel_AutoLineSize(Localize("Scoreboard"), HeadlineFontSize,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

		ColorRGBA GreenDefault(0.78f, 1.0f, 0.8f, 1.0f);
		static CButtonContainer s_AuthedColor, s_SameClanColor;
		DoLine_ColorPicker(&s_AuthedColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Authed name color in scoreboard"), &g_Config.m_ClAuthedPlayerColor, GreenDefault, false);
		DoLine_ColorPicker(&s_SameClanColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Same clan color in scoreboard"), &g_Config.m_ClSameClanColor, GreenDefault, false);

		// ***** DDRace HUD ***** //
		Ui()->DoLabel_AutoLineSize(Localize("DDRace HUD"), HeadlineFontSize,
			TEXTALIGN_ML, &RightView, HeadlineHeight);
		RightView.HSplitTop(MarginSmall, nullptr, &RightView);

		// Switches of various DDRace HUD elements
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowIds, Localize("Show client IDs (scoreboard, chat, spectator)"), &g_Config.m_ClShowIds, &RightView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowhudDDRace, Localize("Show DDRace HUD"), &g_Config.m_ClShowhudDDRace, &RightView, LineSize);
		if(g_Config.m_ClShowhudDDRace)
		{
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowhudJumpsIndicator, Localize("Show jumps indicator"), &g_Config.m_ClShowhudJumpsIndicator, &RightView, LineSize);
		}
		else
		{
			RightView.HSplitTop(LineSize, nullptr, &RightView); // Create empty space for hidden option
		}

		// Eye with a number of spectators
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowhudSpectatorCount, Localize("Show number of spectators"), &g_Config.m_ClShowhudSpectatorCount, &RightView, LineSize);

		// Switch for dummy actions display
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowhudDummyActions, Localize("Show dummy actions"), &g_Config.m_ClShowhudDummyActions, &RightView, LineSize);

		// Player movement information display settings
		RightView.HSplitTop(MarginSmall, nullptr, &RightView); // TClient
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowhudPlayerPosition, Localize("Show player position"), &g_Config.m_ClShowhudPlayerPosition, &RightView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowhudPlayerSpeed, Localize("Show player speed"), &g_Config.m_ClShowhudPlayerSpeed, &RightView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowhudPlayerAngle, Localize("Show player target angle"), &g_Config.m_ClShowhudPlayerAngle, &RightView, LineSize);

		// Dummy movement information display settings
		RightView.HSplitTop(MarginSmall, nullptr, &RightView); // TClient
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowhudDummyPosition, TCLocalize("Show dummy position"), &g_Config.m_TcShowhudDummyPosition, &RightView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowhudDummySpeed, TCLocalize("Show dummy speed"), &g_Config.m_TcShowhudDummySpeed, &RightView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowhudDummyAngle, TCLocalize("Show dummy target angle"), &g_Config.m_TcShowhudDummyAngle, &RightView, LineSize);

		// Freeze bar settings
		RightView.HSplitTop(MarginSmall, nullptr, &RightView); // TClient
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowFreezeBars, Localize("Show freeze bars"), &g_Config.m_ClShowFreezeBars, &RightView, LineSize);
		RightView.HSplitTop(LineSize * 2.0f, &Button, &RightView);
		if(g_Config.m_ClShowFreezeBars)
		{
			Ui()->DoScrollbarOption(&g_Config.m_ClFreezeBarsAlphaInsideFreeze, &g_Config.m_ClFreezeBarsAlphaInsideFreeze, &Button, Localize("Opacity of freeze bars inside freeze"), 0, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE, "%");
		}
	}
	else if(s_CurTab == APPEARANCE_TAB_CHAT)
	{
		CChat &Chat = GameClient()->m_Chat;
		CUIRect TopView, PreviewView;
		MainView.HSplitBottom(220.0f, &TopView, &PreviewView);
		TopView.HSplitBottom(MarginBetweenViews, &TopView, nullptr);
		TopView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);

		// ***** Chat ***** //
		Ui()->DoLabel_AutoLineSize(Localize("Chat"), HeadlineFontSize,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

		// General chat settings
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(DoButton_CheckBox(&g_Config.m_ClShowChat, Localize("Show chat"), g_Config.m_ClShowChat, &Button))
		{
			g_Config.m_ClShowChat = g_Config.m_ClShowChat ? 0 : 1;
		}
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(g_Config.m_ClShowChat)
		{
			static int s_ShowChat = 0;
			if(DoButton_CheckBox(&s_ShowChat, Localize("Always show chat"), g_Config.m_ClShowChat == 2, &Button))
				g_Config.m_ClShowChat = g_Config.m_ClShowChat != 2 ? 2 : 1;
		}

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClChatTeamColors, Localize("Show names in chat in team colors"), &g_Config.m_ClChatTeamColors, &LeftView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowChatFriends, Localize("Show only chat messages from friends"), &g_Config.m_ClShowChatFriends, &LeftView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowChatTeamMembersOnly, Localize("Show only chat messages from team members"), &g_Config.m_ClShowChatTeamMembersOnly, &LeftView, LineSize);

		if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClChatOld, Localize("Use old chat style"), &g_Config.m_ClChatOld, &LeftView, LineSize))
			GameClient()->m_Chat.RebuildChat();

		// DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClCensorChat, Localize("Censor profanity"), &g_Config.m_ClCensorChat, &LeftView, LineSize);

		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(Ui()->DoScrollbarOption(&g_Config.m_ClChatFontSize, &g_Config.m_ClChatFontSize, &Button, Localize("Chat font size"), 10, 100))
		{
			Chat.EnsureCoherentWidth();
			Chat.RebuildChat();
		}

		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(Ui()->DoScrollbarOption(&g_Config.m_ClChatWidth, &g_Config.m_ClChatWidth, &Button, Localize("Chat width"), 120, 400))
		{
			Chat.EnsureCoherentFontSize();
			Chat.RebuildChat();
		}

		static CButtonContainer s_BackgroundColor;
		DoLine_ColorPicker(&s_BackgroundColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Chat background color"), &g_Config.m_ClChatBackgroundColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::ClChatBackgroundColor, true)), false, nullptr, true);

		// ***** Messages ***** //
		Ui()->DoLabel_AutoLineSize(Localize("Messages"), HeadlineFontSize,
			TEXTALIGN_ML, &RightView, HeadlineHeight);
		RightView.HSplitTop(MarginSmall, nullptr, &RightView);

		// Message Colors and extra settings
		static CButtonContainer s_SystemMessageColor;
		DoLine_ColorPicker(&s_SystemMessageColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &RightView, Localize("System message"), &g_Config.m_ClMessageSystemColor, ColorRGBA(1.0f, 1.0f, 0.5f), true, &g_Config.m_ClShowChatSystem);
		static CButtonContainer s_HighlightedMessageColor;
		DoLine_ColorPicker(&s_HighlightedMessageColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &RightView, Localize("Highlighted message"), &g_Config.m_ClMessageHighlightColor, ColorRGBA(1.0f, 0.5f, 0.5f));
		static CButtonContainer s_TeamMessageColor;
		DoLine_ColorPicker(&s_TeamMessageColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &RightView, Localize("Team message"), &g_Config.m_ClMessageTeamColor, ColorRGBA(0.65f, 1.0f, 0.65f));
		static CButtonContainer s_FriendMessageColor;
		DoLine_ColorPicker(&s_FriendMessageColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &RightView, Localize("Friend message"), &g_Config.m_ClMessageFriendColor, ColorRGBA(1.0f, 0.137f, 0.137f), true, &g_Config.m_ClMessageFriend);
		static CButtonContainer s_NormalMessageColor;
		DoLine_ColorPicker(&s_NormalMessageColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &RightView, Localize("Normal message"), &g_Config.m_ClMessageColor, ColorRGBA(1.0f, 1.0f, 1.0f));

		str_format(aBuf, sizeof(aBuf), "%s (echo)", Localize("Client message"));
		static CButtonContainer s_ClientMessageColor;
		// TClient
		DoLine_ColorPicker(&s_ClientMessageColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &RightView, aBuf, &g_Config.m_ClMessageClientColor, ColorRGBA(0.5f, 0.78f, 1.0f), true, &g_Config.m_TcShowChatClient);

		// ***** Chat Preview ***** //
		Ui()->DoLabel_AutoLineSize(Localize("Preview"), HeadlineFontSize,
			TEXTALIGN_ML, &PreviewView, HeadlineHeight);
		PreviewView.HSplitTop(MarginSmall, nullptr, &PreviewView);

		// Use the rest of the view for preview
		PreviewView.Draw(ColorRGBA(1, 1, 1, 0.1f), IGraphics::CORNER_ALL, 5.0f);
		PreviewView.Margin(MarginSmall, &PreviewView);

		ColorRGBA SystemColor = color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageSystemColor));
		ColorRGBA HighlightedColor = color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageHighlightColor));
		ColorRGBA TeamColor = color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageTeamColor));
		ColorRGBA FriendColor = color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageFriendColor));
		ColorRGBA NormalColor = color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageColor));
		ColorRGBA ClientColor = color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageClientColor));
		ColorRGBA DefaultNameColor(0.8f, 0.8f, 0.8f, 1.0f);

		const float RealFontSize = Chat.FontSize() * 2;
		const float RealMsgPaddingX = (!g_Config.m_ClChatOld ? Chat.MessagePaddingX() : 0) * 2;
		const float RealMsgPaddingY = (!g_Config.m_ClChatOld ? Chat.MessagePaddingY() : 0) * 2;
		const float RealMsgPaddingTee = (!g_Config.m_ClChatOld ? Chat.MessageTeeSize() + CChat::MESSAGE_TEE_PADDING_RIGHT : 0) * 2;
		const float RealOffsetY = RealFontSize + RealMsgPaddingY;

		const float X = RealMsgPaddingX / 2.0f + PreviewView.x;
		float Y = PreviewView.y;
		float LineWidth = g_Config.m_ClChatWidth * 2 - (RealMsgPaddingX * 1.5f) - RealMsgPaddingTee;

		str_copy(aBuf, Client()->PlayerName());

		const CAnimState *pIdleState = CAnimState::GetIdle();
		const float RealTeeSize = Chat.MessageTeeSize() * 2;
		const float RealTeeSizeHalved = Chat.MessageTeeSize();
		constexpr float TWSkinUnreliableOffset = -0.25f;
		const float OffsetTeeY = RealTeeSizeHalved;
		const float FullHeightMinusTee = RealOffsetY - RealTeeSize;

		struct SPreviewLine
		{
			int m_ClientId;
			bool m_Team;
			char m_aName[64];
			char m_aText[256];
			bool m_Friend;
			bool m_Player;
			bool m_Client;
			bool m_Highlighted;
			int m_TimesRepeated;

			CTeeRenderInfo m_RenderInfo;
		};

		static std::vector<SPreviewLine> s_vLines;

		enum ELineFlag
		{
			FLAG_TEAM = 1 << 0,
			FLAG_FRIEND = 1 << 1,
			FLAG_HIGHLIGHT = 1 << 2,
			FLAG_CLIENT = 1 << 3
		};
		enum
		{
			PREVIEW_SYS,
			PREVIEW_HIGHLIGHT,
			PREVIEW_TEAM,
			PREVIEW_FRIEND,
			PREVIEW_SPAMMER,
			PREVIEW_CLIENT
		};
		auto &&SetPreviewLine = [](int Index, int ClientId, const char *pName, const char *pText, int Flag, int Repeats) {
			SPreviewLine *pLine;
			if((int)s_vLines.size() <= Index)
			{
				s_vLines.emplace_back();
				pLine = &s_vLines.back();
			}
			else
			{
				pLine = &s_vLines[Index];
			}
			pLine->m_ClientId = ClientId;
			pLine->m_Team = Flag & FLAG_TEAM;
			pLine->m_Friend = Flag & FLAG_FRIEND;
			pLine->m_Player = ClientId >= 0;
			pLine->m_Highlighted = Flag & FLAG_HIGHLIGHT;
			pLine->m_Client = Flag & FLAG_CLIENT;
			pLine->m_TimesRepeated = Repeats;
			str_copy(pLine->m_aName, pName);
			str_copy(pLine->m_aText, pText);
		};
		auto &&SetLineSkin = [RealTeeSize](int Index, const CSkin *pSkin) {
			if(Index >= (int)s_vLines.size())
				return;
			s_vLines[Index].m_RenderInfo.m_Size = RealTeeSize;
			s_vLines[Index].m_RenderInfo.Apply(pSkin);
		};

		auto &&RenderPreview = [&](int LineIndex, int x, int y, bool Render = true) {
			if(LineIndex >= (int)s_vLines.size())
				return vec2(0, 0);
			CTextCursor LocalCursor;
			LocalCursor.SetPosition(vec2(x, y));
			LocalCursor.m_FontSize = RealFontSize;
			LocalCursor.m_Flags = Render ? TEXTFLAG_RENDER : 0;
			LocalCursor.m_LineWidth = LineWidth;
			const auto &Line = s_vLines[LineIndex];

			char aClientId[16] = "";
			if(g_Config.m_ClShowIds && Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
			{
				GameClient()->FormatClientId(Line.m_ClientId, aClientId, EClientIdFormat::INDENT_FORCE);
			}

			char aCount[12];
			if(Line.m_ClientId < 0)
				str_format(aCount, sizeof(aCount), "[%d] ", Line.m_TimesRepeated + 1);
			else
				str_format(aCount, sizeof(aCount), " [%d]", Line.m_TimesRepeated + 1);

			if(Line.m_Player)
			{
				LocalCursor.m_X += RealMsgPaddingTee;

				if(Line.m_Friend && g_Config.m_ClMessageFriend)
				{
					if(Render)
						TextRender()->TextColor(FriendColor);
					TextRender()->TextEx(&LocalCursor, "♥ ", -1);
				}
			}

			ColorRGBA NameColor;
			if(Line.m_Team)
				NameColor = CalculateNameColor(color_cast<ColorHSLA>(TeamColor));
			else if(Line.m_Player)
				NameColor = DefaultNameColor;
			else if(Line.m_Client)
				NameColor = ClientColor;
			else
				NameColor = SystemColor;

			if(Render)
				TextRender()->TextColor(NameColor);

			TextRender()->TextEx(&LocalCursor, aClientId);
			TextRender()->TextEx(&LocalCursor, Line.m_aName);

			if(Line.m_TimesRepeated > 0)
			{
				if(Render)
					TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.3f);
				TextRender()->TextEx(&LocalCursor, aCount, -1);
			}

			if(Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
			{
				if(Render)
					TextRender()->TextColor(NameColor);
				TextRender()->TextEx(&LocalCursor, ": ", -1);
			}

			CTextCursor AppendCursor = LocalCursor;
			AppendCursor.m_LongestLineWidth = 0.0f;
			if(!g_Config.m_ClChatOld)
			{
				AppendCursor.m_StartX = LocalCursor.m_X;
				AppendCursor.m_LineWidth -= LocalCursor.m_LongestLineWidth;
			}

			if(Render)
			{
				if(Line.m_Highlighted)
					TextRender()->TextColor(HighlightedColor);
				else if(Line.m_Team)
					TextRender()->TextColor(TeamColor);
				else if(Line.m_Player)
					TextRender()->TextColor(NormalColor);
			}

			TextRender()->TextEx(&AppendCursor, Line.m_aText, -1);
			if(Render)
				TextRender()->TextColor(TextRender()->DefaultTextColor());

			return vec2{LocalCursor.m_LongestLineWidth + AppendCursor.m_LongestLineWidth, AppendCursor.Height() + RealMsgPaddingY};
		};

		// Set preview lines
		{
			char aLineBuilder[128];

			str_format(aLineBuilder, sizeof(aLineBuilder), "'%s' entered and joined the game", aBuf);
			SetPreviewLine(PREVIEW_SYS, -1, "*** ", aLineBuilder, 0, 0);

			str_format(aLineBuilder, sizeof(aLineBuilder), "Hey, how are you %s?", aBuf);
			SetPreviewLine(PREVIEW_HIGHLIGHT, 7, "Random Tee", aLineBuilder, FLAG_HIGHLIGHT, 0);

			SetPreviewLine(PREVIEW_TEAM, 11, "Your Teammate", "Let's speedrun this!", FLAG_TEAM, 0);
			SetPreviewLine(PREVIEW_FRIEND, 8, "Friend", "Hello there", FLAG_FRIEND, 0);
			SetPreviewLine(PREVIEW_SPAMMER, 9, "Spammer", "Hey fools, I'm spamming here!", 0, 5);
			SetPreviewLine(PREVIEW_CLIENT, -1, "— ", "Echo command executed", FLAG_CLIENT, 0);
		}

		SetLineSkin(1, GameClient()->m_Skins.Find("pinky"));
		SetLineSkin(2, GameClient()->m_Skins.Find("default"));
		SetLineSkin(3, GameClient()->m_Skins.Find("cammostripes"));
		SetLineSkin(4, GameClient()->m_Skins.Find("beast"));

		// Backgrounds first
		if(!g_Config.m_ClChatOld)
		{
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClChatBackgroundColor, true)));

			float TempY = Y;
			const float RealBackgroundRounding = Chat.MessageRounding() * 2.0f;

			auto &&RenderMessageBackground = [&](int LineIndex) {
				auto Size = RenderPreview(LineIndex, 0, 0, false);
				Graphics()->DrawRectExt(X - RealMsgPaddingX / 2.0f, TempY - RealMsgPaddingY / 2.0f, Size.x + RealMsgPaddingX * 1.5f, Size.y, RealBackgroundRounding, IGraphics::CORNER_ALL);
				return Size.y;
			};

			if(g_Config.m_ClShowChatSystem)
			{
				TempY += RenderMessageBackground(PREVIEW_SYS);
			}

			if(!g_Config.m_ClShowChatFriends)
			{
				if(!g_Config.m_ClShowChatTeamMembersOnly)
					TempY += RenderMessageBackground(PREVIEW_HIGHLIGHT);
				TempY += RenderMessageBackground(PREVIEW_TEAM);
			}

			if(!g_Config.m_ClShowChatTeamMembersOnly)
				TempY += RenderMessageBackground(PREVIEW_FRIEND);

			if(!g_Config.m_ClShowChatFriends && !g_Config.m_ClShowChatTeamMembersOnly)
			{
				TempY += RenderMessageBackground(PREVIEW_SPAMMER);
			}

			if(g_Config.m_TcShowChatClient)
			{
				TempY += RenderMessageBackground(PREVIEW_CLIENT);
			}

			Graphics()->QuadsEnd();
		}

		// System
		if(g_Config.m_ClShowChatSystem)
		{
			Y += RenderPreview(PREVIEW_SYS, X, Y).y;
		}

		if(!g_Config.m_ClShowChatFriends)
		{
			// Highlighted
			if(!g_Config.m_ClChatOld && !g_Config.m_ClShowChatTeamMembersOnly)
				RenderTools()->RenderTee(pIdleState, &s_vLines[PREVIEW_HIGHLIGHT].m_RenderInfo, EMOTE_NORMAL, vec2(1, 0.1f), vec2(X + RealTeeSizeHalved, Y + OffsetTeeY + FullHeightMinusTee / 2.0f + TWSkinUnreliableOffset));
			if(!g_Config.m_ClShowChatTeamMembersOnly)
				Y += RenderPreview(PREVIEW_HIGHLIGHT, X, Y).y;

			// Team
			if(!g_Config.m_ClChatOld)
				RenderTools()->RenderTee(pIdleState, &s_vLines[PREVIEW_TEAM].m_RenderInfo, EMOTE_NORMAL, vec2(1, 0.1f), vec2(X + RealTeeSizeHalved, Y + OffsetTeeY + FullHeightMinusTee / 2.0f + TWSkinUnreliableOffset));
			Y += RenderPreview(PREVIEW_TEAM, X, Y).y;
		}

		// Friend
		if(!g_Config.m_ClChatOld && !g_Config.m_ClShowChatTeamMembersOnly)
			RenderTools()->RenderTee(pIdleState, &s_vLines[PREVIEW_FRIEND].m_RenderInfo, EMOTE_NORMAL, vec2(1, 0.1f), vec2(X + RealTeeSizeHalved, Y + OffsetTeeY + FullHeightMinusTee / 2.0f + TWSkinUnreliableOffset));
		if(!g_Config.m_ClShowChatTeamMembersOnly)
			Y += RenderPreview(PREVIEW_FRIEND, X, Y).y;

		// Normal
		if(!g_Config.m_ClShowChatFriends && !g_Config.m_ClShowChatTeamMembersOnly)
		{
			if(!g_Config.m_ClChatOld)
				RenderTools()->RenderTee(pIdleState, &s_vLines[PREVIEW_SPAMMER].m_RenderInfo, EMOTE_NORMAL, vec2(1, 0.1f), vec2(X + RealTeeSizeHalved, Y + OffsetTeeY + FullHeightMinusTee / 2.0f + TWSkinUnreliableOffset));
			Y += RenderPreview(PREVIEW_SPAMMER, X, Y).y;
		}
		// Client
		if(g_Config.m_TcShowChatClient)
		{
			RenderPreview(PREVIEW_CLIENT, X, Y);
		}

		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
	else if(s_CurTab == APPEARANCE_TAB_NAME_PLATE)
	{
		MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);

		// ***** Name Plate ***** //
		Ui()->DoLabel_AutoLineSize(Localize("Name Plate"), HeadlineFontSize,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

		// General name plate settings
		{
			int Pressed = (g_Config.m_ClNamePlates ? 2 : 0) + (g_Config.m_ClNamePlatesOwn ? 1 : 0);
			if(DoLine_RadioMenu(LeftView, Localize("Show name plates"),
				   m_vButtonContainersNamePlateShow,
				   {Localize("None", "Show name plates"), Localize("Own", "Show name plates"), Localize("Others", "Show name plates"), Localize("All", "Show name plates")},
				   {0, 1, 2, 3},
				   Pressed))
			{
				g_Config.m_ClNamePlates = Pressed & 2 ? 1 : 0;
				g_Config.m_ClNamePlatesOwn = Pressed & 1 ? 1 : 0;
			}
		}
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_ClNamePlatesSize, &g_Config.m_ClNamePlatesSize, &Button, Localize("Name plates size"), -50, 100);
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_ClNamePlatesOffset, &g_Config.m_ClNamePlatesOffset, &Button, Localize("Name plates offset"), 10, 50);

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClNamePlatesClan, Localize("Show clan above name plates"), &g_Config.m_ClNamePlatesClan, &LeftView, LineSize);
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(g_Config.m_ClNamePlatesClan)
			Ui()->DoScrollbarOption(&g_Config.m_ClNamePlatesClanSize, &g_Config.m_ClNamePlatesClanSize, &Button, Localize("Clan plates size"), -50, 100);

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClNamePlatesTeamcolors, Localize("Use team colors for name plates"), &g_Config.m_ClNamePlatesTeamcolors, &LeftView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClNamePlatesFriendMark, Localize("Show friend icon in name plates"), &g_Config.m_ClNamePlatesFriendMark, &LeftView, LineSize);

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClNamePlatesIds, Localize("Show client IDs in name plates"), &g_Config.m_ClNamePlatesIds, &LeftView, LineSize);
		if(g_Config.m_ClNamePlatesIds > 0)
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClNamePlatesIdsSeparateLine, Localize("Show client IDs on a separate line"), &g_Config.m_ClNamePlatesIdsSeparateLine, &LeftView, LineSize);
		else
			LeftView.HSplitTop(LineSize, nullptr, &LeftView);
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(g_Config.m_ClNamePlatesIds > 0 && g_Config.m_ClNamePlatesIdsSeparateLine > 0)
			Ui()->DoScrollbarOption(&g_Config.m_ClNamePlatesIdsSize, &g_Config.m_ClNamePlatesIdsSize, &Button, Localize("Client IDs size"), -50, 100);

		// ***** Hook Strength ***** //
		LeftView.HSplitTop(MarginBetweenViews, nullptr, &LeftView);
		Ui()->DoLabel_AutoLineSize(Localize("Hook Strength"), HeadlineFontSize,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(DoButton_CheckBox(&g_Config.m_ClNamePlatesStrong, Localize("Show hook strength icon indicator"), g_Config.m_ClNamePlatesStrong, &Button))
		{
			g_Config.m_ClNamePlatesStrong = g_Config.m_ClNamePlatesStrong ? 0 : 1;
		}
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(g_Config.m_ClNamePlatesStrong)
		{
			static int s_NamePlatesStrong = 0;
			if(DoButton_CheckBox(&s_NamePlatesStrong, Localize("Show hook strength number indicator"), g_Config.m_ClNamePlatesStrong == 2, &Button))
				g_Config.m_ClNamePlatesStrong = g_Config.m_ClNamePlatesStrong != 2 ? 2 : 1;
		}

		LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
		if(g_Config.m_ClNamePlatesStrong)
		{
			Ui()->DoScrollbarOption(&g_Config.m_ClNamePlatesStrongSize, &g_Config.m_ClNamePlatesStrongSize, &Button, Localize("Size of hook strength icon and number indicator"), -50, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE);
		}

		// ***** Key Presses ***** //
		LeftView.HSplitTop(MarginBetweenViews, nullptr, &LeftView);
		Ui()->DoLabel_AutoLineSize(Localize("Key Presses"), HeadlineFontSize,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

		DoLine_RadioMenu(LeftView, Localize("Show players' key presses"),
			m_vButtonContainersNamePlateKeyPresses,
			{Localize("None", "Show players' key presses"), Localize("Own", "Show players' key presses"), Localize("Others", "Show players' key presses"), Localize("All", "Show players' key presses")},
			{0, 3, 1, 2},
			g_Config.m_ClShowDirection);

		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(g_Config.m_ClShowDirection > 0)
			Ui()->DoScrollbarOption(&g_Config.m_ClDirectionSize, &g_Config.m_ClDirectionSize, &Button, Localize("Size of key press icons"), -50, 100);

		// ***** Name Plate Preview ***** //
		Ui()->DoLabel_AutoLineSize(Localize("Preview"), HeadlineFontSize,
			TEXTALIGN_ML, &RightView, HeadlineHeight);
		RightView.HSplitTop(2.0f * MarginSmall, nullptr, &RightView);

		// ***** Name Plate Dummy Preview ***** //
		RightView.HSplitBottom(LineSize, &RightView, &Button);
		if(DoButton_CheckBox(&m_DummyNamePlatePreview, g_Config.m_ClDummy ? Localize("Preview player's name plate") : Localize("Preview dummy's name plate"), m_DummyNamePlatePreview, &Button))
			m_DummyNamePlatePreview = !m_DummyNamePlatePreview;

		int Dummy = g_Config.m_ClDummy != (m_DummyNamePlatePreview ? 1 : 0);

		const vec2 Position = RightView.Center();

		GameClient()->m_NamePlates.RenderNamePlatePreview(Position, Dummy);
	}
	else if(s_CurTab == APPEARANCE_TAB_HOOK_COLLISION)
	{
		MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);

		// ***** Hookline ***** //
		Ui()->DoLabel_AutoLineSize(Localize("Hook collision line"), HeadlineFontSize,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

		// General hookline settings
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(DoButton_CheckBox(&g_Config.m_ClShowHookCollOwn, Localize("Show own player's hook collision line"), g_Config.m_ClShowHookCollOwn, &Button))
		{
			g_Config.m_ClShowHookCollOwn = g_Config.m_ClShowHookCollOwn ? 0 : 1;
		}
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(g_Config.m_ClShowHookCollOwn)
		{
			static int s_ShowHookCollOwn = 0;
			if(DoButton_CheckBox(&s_ShowHookCollOwn, Localize("Always show own player's hook collision line"), g_Config.m_ClShowHookCollOwn == 2, &Button))
				g_Config.m_ClShowHookCollOwn = g_Config.m_ClShowHookCollOwn != 2 ? 2 : 1;
		}

		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(DoButton_CheckBox(&g_Config.m_ClShowHookCollOther, Localize("Show other players' hook collision lines"), g_Config.m_ClShowHookCollOther, &Button))
		{
			g_Config.m_ClShowHookCollOther = g_Config.m_ClShowHookCollOther >= 1 ? 0 : 1;
		}
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(g_Config.m_ClShowHookCollOther)
		{
			static int s_ShowHookCollOther = 0;
			if(DoButton_CheckBox(&s_ShowHookCollOther, Localize("Always show other players' hook collision lines"), g_Config.m_ClShowHookCollOther == 2, &Button))
				g_Config.m_ClShowHookCollOther = g_Config.m_ClShowHookCollOther != 2 ? 2 : 1;
		}

		LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_ClHookCollSize, &g_Config.m_ClHookCollSize, &Button, Localize("Width of your own hook collision line"), 0, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE);

		LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_ClHookCollSizeOther, &g_Config.m_ClHookCollSizeOther, &Button, Localize("Width of others' hook collision line"), 0, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE);

		LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_ClHookCollAlpha, &g_Config.m_ClHookCollAlpha, &Button, Localize("Hook collision line opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE, "%");

		static CButtonContainer s_HookCollNoCollResetId, s_HookCollHookableCollResetId, s_HookCollTeeCollResetId, s_HookCollTipColorResetId;
		static int s_HookCollToolTip;

		Ui()->DoLabel_AutoLineSize(Localize("Colors of the hook collision line:"), 13.0f,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);

		Ui()->DoButtonLogic(&s_HookCollToolTip, 0, &LeftView, BUTTONFLAG_NONE); // Just for the tooltip, result ignored
		GameClient()->m_Tooltips.DoToolTip(&s_HookCollToolTip, &LeftView, Localize("Your movements are not taken into account when calculating the line colors"));
		DoLine_ColorPicker(&s_HookCollNoCollResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("When nothing is hookable", "Hook collision line color"), &g_Config.m_ClHookCollColorNoColl, ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f), false);
		DoLine_ColorPicker(&s_HookCollHookableCollResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("When something is hookable", "Hook collision line color"), &g_Config.m_ClHookCollColorHookableColl, ColorRGBA(130.0f / 255.0f, 232.0f / 255.0f, 160.0f / 255.0f, 1.0f), false);
		DoLine_ColorPicker(&s_HookCollTeeCollResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("When a Tee is hookable", "Hook collision line color"), &g_Config.m_ClHookCollColorTeeColl, ColorRGBA(1.0f, 1.0f, 0.0f, 1.0f), false);
		DoLine_ColorPicker(&s_HookCollTipColorResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Hook collision line tip", "Hook collision line color"), &g_Config.m_ClHookCollTipColor, ColorRGBA(1.0f, 1.0f, 0.0f, 0.5f), false, nullptr, true);

		// ***** Hook collisions preview ***** //
		Ui()->DoLabel_AutoLineSize(Localize("Preview"), HeadlineFontSize,
			TEXTALIGN_ML, &RightView, HeadlineHeight);
		RightView.HSplitTop(2 * MarginSmall, nullptr, &RightView);

		auto DoHookCollision = [this](const vec2 &Pos, const float &Length, const int &Size, const ColorRGBA &Color, const ColorRGBA &TipColor, const bool &Invert) {
			ColorRGBA ColorModified = Color;
			ColorRGBA TipColorModified = TipColor;
			if(Invert)
				ColorModified = color_invert(ColorModified);
			ColorModified = ColorModified.WithAlpha((float)g_Config.m_ClHookCollAlpha / 100);
			TipColorModified = TipColor.WithMultipliedAlpha((float)g_Config.m_ClHookCollAlpha / 100);
			Graphics()->TextureClear();
			if(Size > 0)
			{
				Graphics()->QuadsBegin();
				Graphics()->SetColor(ColorModified);
				float LineWidth = 0.5f + (float)(Size - 1) * 0.25f;
				IGraphics::CQuadItem QuadItem(Pos.x, Pos.y - LineWidth, Length, LineWidth * 2.f);
				Graphics()->QuadsDrawTL(&QuadItem, 1);
				if(TipColor.a > 0.0f)
				{
					Graphics()->SetColor(TipColorModified);
					IGraphics::CQuadItem TipQuadItem(Pos.x + Length, Pos.y - LineWidth, 15.f, LineWidth * 2.f);
					Graphics()->QuadsDrawTL(&TipQuadItem, 1);
				}
				Graphics()->QuadsEnd();
			}
			else
			{
				Graphics()->LinesBegin();
				Graphics()->SetColor(ColorModified);
				IGraphics::CLineItem LineItem(Pos.x, Pos.y, Pos.x + Length, Pos.y);
				Graphics()->LinesDraw(&LineItem, 1);
				if(TipColor.a > 0.0f)
				{
					Graphics()->SetColor(TipColorModified);
					IGraphics::CLineItem TipLineItem(Pos.x + Length, Pos.y, Pos.x + Length + 15.f, Pos.y);
					Graphics()->LinesDraw(&TipLineItem, 1);
				}
				Graphics()->LinesEnd();
			}
		};

		CTeeRenderInfo OwnSkinInfo;
		OwnSkinInfo.Apply(GameClient()->m_Skins.Find(g_Config.m_ClPlayerSkin));
		OwnSkinInfo.ApplyColors(g_Config.m_ClPlayerUseCustomColor, g_Config.m_ClPlayerColorBody, g_Config.m_ClPlayerColorFeet);
		OwnSkinInfo.m_Size = 50.0f;

		CTeeRenderInfo DummySkinInfo;
		DummySkinInfo.Apply(GameClient()->m_Skins.Find(g_Config.m_ClDummySkin));
		DummySkinInfo.ApplyColors(g_Config.m_ClDummyUseCustomColor, g_Config.m_ClDummyColorBody, g_Config.m_ClDummyColorFeet);
		DummySkinInfo.m_Size = 50.0f;

		vec2 TeeRenderPos, DummyRenderPos;

		const float LineLength = 150.f;
		const float LeftMargin = 30.f;

		const int TileScale = 32.0f;

		// Toggled via checkbox later, inverts some previews
		static bool s_HookCollPressed = false;

		CUIRect PreviewColl;

		// ***** Unhookable Tile Preview *****
		CUIRect PreviewNoColl;
		RightView.HSplitTop(50.0f, &PreviewNoColl, &RightView);
		RightView.HSplitTop(4 * MarginSmall, nullptr, &RightView);
		TeeRenderPos = vec2(PreviewNoColl.x + LeftMargin, PreviewNoColl.y + PreviewNoColl.h / 2.0f);
		DoHookCollision(TeeRenderPos, PreviewNoColl.w - LineLength, g_Config.m_ClHookCollSize, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollColorNoColl)), ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f), s_HookCollPressed);
		RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, 0, vec2(1.0f, 0.0f), TeeRenderPos);

		CUIRect NoHookTileRect;
		PreviewNoColl.VSplitRight(LineLength, &PreviewNoColl, &NoHookTileRect);
		NoHookTileRect.VSplitLeft(50.0f, &NoHookTileRect, nullptr);
		NoHookTileRect.Margin(10.0f, &NoHookTileRect);

		// Render unhookable tile
		Graphics()->TextureClear();
		Graphics()->TextureSet(GameClient()->m_MapImages.GetEntities(MAP_IMAGE_ENTITY_LAYER_TYPE_ALL_EXCEPT_SWITCH));
		Graphics()->BlendNormal();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
		RenderMap()->RenderTile(NoHookTileRect.x, NoHookTileRect.y, TILE_NOHOOK, TileScale, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));

		// ***** Hookable Tile Preview *****
		RightView.HSplitTop(50.0f, &PreviewColl, &RightView);
		RightView.HSplitTop(4 * MarginSmall, nullptr, &RightView);
		TeeRenderPos = vec2(PreviewColl.x + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
		DoHookCollision(TeeRenderPos, PreviewColl.w - LineLength, g_Config.m_ClHookCollSize, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollColorHookableColl)), ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f), s_HookCollPressed);
		RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, 0, vec2(1.0f, 0.0f), TeeRenderPos);

		CUIRect HookTileRect;
		PreviewColl.VSplitRight(LineLength, &PreviewColl, &HookTileRect);
		HookTileRect.VSplitLeft(50.0f, &HookTileRect, nullptr);
		HookTileRect.Margin(10.0f, &HookTileRect);

		// Render hookable tile
		Graphics()->TextureClear();
		Graphics()->TextureSet(GameClient()->m_MapImages.GetEntities(MAP_IMAGE_ENTITY_LAYER_TYPE_ALL_EXCEPT_SWITCH));
		Graphics()->BlendNormal();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
		RenderMap()->RenderTile(HookTileRect.x, HookTileRect.y, TILE_SOLID, TileScale, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));

		// ***** Hook Dummy Preview *****
		RightView.HSplitTop(50.0f, &PreviewColl, &RightView);
		RightView.HSplitTop(4 * MarginSmall, nullptr, &RightView);
		TeeRenderPos = vec2(PreviewColl.x + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
		DummyRenderPos = vec2(PreviewColl.x + PreviewColl.w - LineLength - 5.f + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
		DoHookCollision(TeeRenderPos, PreviewColl.w - LineLength - 15.f, g_Config.m_ClHookCollSize, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollColorTeeColl)), ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f), s_HookCollPressed);
		RenderTools()->RenderTee(CAnimState::GetIdle(), &DummySkinInfo, 0, vec2(1.0f, 0.0f), DummyRenderPos);
		RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, 0, vec2(1.0f, 0.0f), TeeRenderPos);

		// ***** Hook Dummy Reverse Preview *****
		RightView.HSplitTop(50.0f, &PreviewColl, &RightView);
		RightView.HSplitTop(4 * MarginSmall, nullptr, &RightView);
		TeeRenderPos = vec2(PreviewColl.x + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
		DummyRenderPos = vec2(PreviewColl.x + PreviewColl.w - LineLength - 5.f + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
		DoHookCollision(TeeRenderPos, PreviewColl.w - LineLength - 15.f, g_Config.m_ClHookCollSizeOther, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollColorTeeColl)), ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f), false);
		RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, 0, vec2(1.0f, 0.0f), DummyRenderPos);
		RenderTools()->RenderTee(CAnimState::GetIdle(), &DummySkinInfo, 0, vec2(1.0f, 0.0f), TeeRenderPos);

		// ***** Hook Line Tip Preview *****
		RightView.HSplitTop(50.0f, &PreviewColl, &RightView);
		RightView.HSplitTop(4 * MarginSmall, nullptr, &RightView);
		TeeRenderPos = vec2(PreviewColl.x + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
		DoHookCollision(TeeRenderPos, PreviewColl.w - LineLength - 15.f, g_Config.m_ClHookCollSize, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollColorNoColl)), color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollTipColor, true)), s_HookCollPressed);
		RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, 0, vec2(1.0f, 0.0f), TeeRenderPos);

		// ***** Preview +hookcoll pressed toggle *****
		RightView.HSplitTop(LineSize, &Button, &RightView);
		if(DoButton_CheckBox(&s_HookCollPressed, Localize("Preview 'Hook collisions' being pressed"), s_HookCollPressed, &Button))
			s_HookCollPressed = !s_HookCollPressed;
	}
	else if(s_CurTab == APPEARANCE_TAB_INFO_MESSAGES)
	{
		MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);

		// ***** Info Messages ***** //
		Ui()->DoLabel_AutoLineSize(Localize("Info Messages"), HeadlineFontSize,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

		// General info messages settings
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(DoButton_CheckBox(&g_Config.m_ClShowKillMessages, Localize("Show kill messages"), g_Config.m_ClShowKillMessages, &Button))
		{
			g_Config.m_ClShowKillMessages ^= 1;
		}

		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(DoButton_CheckBox(&g_Config.m_ClShowFinishMessages, Localize("Show finish messages"), g_Config.m_ClShowFinishMessages, &Button))
		{
			g_Config.m_ClShowFinishMessages ^= 1;
		}

		static CButtonContainer s_KillMessageNormalColorId, s_KillMessageHighlightColorId;
		DoLine_ColorPicker(&s_KillMessageNormalColorId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Normal Color"), &g_Config.m_ClKillMessageNormalColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
		DoLine_ColorPicker(&s_KillMessageHighlightColorId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Highlight Color"), &g_Config.m_ClKillMessageHighlightColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
	}
	else if(s_CurTab == APPEARANCE_TAB_LASER)
	{
		MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);

		// ***** Weapons ***** //
		Ui()->DoLabel_AutoLineSize(Localize("Weapons"), HeadlineFontSize,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

		// General weapon laser settings
		static CButtonContainer s_LaserRifleOutResetId, s_LaserRifleInResetId, s_LaserShotgunOutResetId, s_LaserShotgunInResetId;

		ColorHSLA LaserRifleOutlineColor = DoLine_ColorPicker(&s_LaserRifleOutResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Rifle Laser Outline Color"), &g_Config.m_ClLaserRifleOutlineColor, ColorRGBA(0.074402f, 0.074402f, 0.247166f, 1.0f), false);
		ColorHSLA LaserRifleInnerColor = DoLine_ColorPicker(&s_LaserRifleInResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Rifle Laser Inner Color"), &g_Config.m_ClLaserRifleInnerColor, ColorRGBA(0.498039f, 0.498039f, 1.0f, 1.0f), false);
		ColorHSLA LaserShotgunOutlineColor = DoLine_ColorPicker(&s_LaserShotgunOutResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Shotgun Laser Outline Color"), &g_Config.m_ClLaserShotgunOutlineColor, ColorRGBA(0.125490f, 0.098039f, 0.043137f, 1.0f), false);
		ColorHSLA LaserShotgunInnerColor = DoLine_ColorPicker(&s_LaserShotgunInResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Shotgun Laser Inner Color"), &g_Config.m_ClLaserShotgunInnerColor, ColorRGBA(0.570588f, 0.417647f, 0.252941f, 1.0f), false);

		// ***** Entities ***** //
		LeftView.HSplitTop(10.0f, nullptr, &LeftView);
		Ui()->DoLabel_AutoLineSize(Localize("Entities"), HeadlineFontSize,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

		// General entity laser settings
		static CButtonContainer s_LaserDoorOutResetId, s_LaserDoorInResetId, s_LaserFreezeOutResetId, s_LaserFreezeInResetId, s_LaserDraggerOutResetId, s_LaserDraggerInResetId;

		ColorHSLA LaserDoorOutlineColor = DoLine_ColorPicker(&s_LaserDoorOutResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Door Laser Outline Color"), &g_Config.m_ClLaserDoorOutlineColor, ColorRGBA(0.0f, 0.131372f, 0.096078f, 1.0f), false);
		ColorHSLA LaserDoorInnerColor = DoLine_ColorPicker(&s_LaserDoorInResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Door Laser Inner Color"), &g_Config.m_ClLaserDoorInnerColor, ColorRGBA(0.262745f, 0.760784f, 0.639215f, 1.0f), false);
		ColorHSLA LaserFreezeOutlineColor = DoLine_ColorPicker(&s_LaserFreezeOutResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Freeze Laser Outline Color"), &g_Config.m_ClLaserFreezeOutlineColor, ColorRGBA(0.131372f, 0.123529f, 0.182352f, 1.0f), false);
		ColorHSLA LaserFreezeInnerColor = DoLine_ColorPicker(&s_LaserFreezeInResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Freeze Laser Inner Color"), &g_Config.m_ClLaserFreezeInnerColor, ColorRGBA(0.482352f, 0.443137f, 0.564705f, 1.0f), false);
		ColorHSLA LaserDraggerOutlineColor = DoLine_ColorPicker(&s_LaserDraggerOutResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Dragger Outline Color"), &g_Config.m_ClLaserDraggerOutlineColor, ColorRGBA(0.1640625f, 0.015625f, 0.015625f, 1.0f), false);
		ColorHSLA LaserDraggerInnerColor = DoLine_ColorPicker(&s_LaserDraggerInResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Dragger Inner Color"), &g_Config.m_ClLaserDraggerInnerColor, ColorRGBA(.8666666f, .3725490f, .3725490f, 1.0f), false);

		static CButtonContainer s_AllToRifleResetId, s_AllToDefaultResetId;

		LeftView.HSplitTop(4 * MarginSmall, nullptr, &LeftView);
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(DoButton_Menu(&s_AllToRifleResetId, Localize("Set all to Rifle"), 0, &Button))
		{
			g_Config.m_ClLaserShotgunOutlineColor = g_Config.m_ClLaserRifleOutlineColor;
			g_Config.m_ClLaserShotgunInnerColor = g_Config.m_ClLaserRifleInnerColor;
			g_Config.m_ClLaserDoorOutlineColor = g_Config.m_ClLaserRifleOutlineColor;
			g_Config.m_ClLaserDoorInnerColor = g_Config.m_ClLaserRifleInnerColor;
			g_Config.m_ClLaserFreezeOutlineColor = g_Config.m_ClLaserRifleOutlineColor;
			g_Config.m_ClLaserFreezeInnerColor = g_Config.m_ClLaserRifleInnerColor;
			g_Config.m_ClLaserDraggerOutlineColor = g_Config.m_ClLaserRifleOutlineColor;
			g_Config.m_ClLaserDraggerInnerColor = g_Config.m_ClLaserRifleInnerColor;
		}

		// values taken from the CL commands
		LeftView.HSplitTop(2 * MarginSmall, nullptr, &LeftView);
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(DoButton_Menu(&s_AllToDefaultResetId, Localize("Reset to defaults"), 0, &Button))
		{
			g_Config.m_ClLaserRifleOutlineColor = 11176233;
			g_Config.m_ClLaserRifleInnerColor = 11206591;
			g_Config.m_ClLaserShotgunOutlineColor = 1866773;
			g_Config.m_ClLaserShotgunInnerColor = 1467241;
			g_Config.m_ClLaserDoorOutlineColor = 7667473;
			g_Config.m_ClLaserDoorInnerColor = 7701379;
			g_Config.m_ClLaserFreezeOutlineColor = 11613223;
			g_Config.m_ClLaserFreezeInnerColor = 12001153;
			g_Config.m_ClLaserDraggerOutlineColor = 57618;
			g_Config.m_ClLaserDraggerInnerColor = 42398;
		}

		// ***** Laser Preview ***** //
		Ui()->DoLabel_AutoLineSize(Localize("Preview"), HeadlineFontSize,
			TEXTALIGN_ML, &RightView, HeadlineHeight);
		RightView.HSplitTop(MarginSmall, nullptr, &RightView);

		const float LaserPreviewHeight = 60.0f;
		CUIRect LaserPreview;
		RightView.HSplitTop(LaserPreviewHeight, &LaserPreview, &RightView);
		RightView.HSplitTop(2 * MarginSmall, nullptr, &RightView);
		DoLaserPreview(&LaserPreview, LaserRifleOutlineColor, LaserRifleInnerColor, LASERTYPE_RIFLE);

		RightView.HSplitTop(LaserPreviewHeight, &LaserPreview, &RightView);
		RightView.HSplitTop(2 * MarginSmall, nullptr, &RightView);
		DoLaserPreview(&LaserPreview, LaserShotgunOutlineColor, LaserShotgunInnerColor, LASERTYPE_SHOTGUN);

		RightView.HSplitTop(LaserPreviewHeight, &LaserPreview, &RightView);
		RightView.HSplitTop(2 * MarginSmall, nullptr, &RightView);
		DoLaserPreview(&LaserPreview, LaserDoorOutlineColor, LaserDoorInnerColor, LASERTYPE_DOOR);

		RightView.HSplitTop(LaserPreviewHeight, &LaserPreview, &RightView);
		RightView.HSplitTop(2 * MarginSmall, nullptr, &RightView);
		DoLaserPreview(&LaserPreview, LaserFreezeOutlineColor, LaserFreezeInnerColor, LASERTYPE_FREEZE);

		RightView.HSplitTop(LaserPreviewHeight, &LaserPreview, &RightView);
		RightView.HSplitTop(2 * MarginSmall, nullptr, &RightView);
		DoLaserPreview(&LaserPreview, LaserDraggerOutlineColor, LaserDraggerInnerColor, LASERTYPE_DRAGGER);
	}
}

void CMenus::RenderSettingsDDNet(CUIRect MainView)
{
	CUIRect Button, Left, Right, LeftLeft, Label;

#if defined(CONF_AUTOUPDATE)
	CUIRect UpdaterRect;
	MainView.HSplitBottom(20.0f, &MainView, &UpdaterRect);
	MainView.HSplitBottom(5.0f, &MainView, nullptr);
#endif

	// demo
	CUIRect Demo;
	MainView.HSplitTop(110.0f, &Demo, &MainView);
	Demo.HSplitTop(30.0f, &Label, &Demo);
	Ui()->DoLabel(&Label, Localize("Demo"), 20.0f, TEXTALIGN_ML);
	Label.VSplitMid(nullptr, &Label, 20.0f);
	Ui()->DoLabel(&Label, Localize("Ghost"), 20.0f, TEXTALIGN_ML);

	Demo.HSplitTop(5.0f, nullptr, &Demo);
	Demo.VSplitMid(&Left, &Right, 20.0f);

	Left.HSplitTop(20.0f, &Button, &Left);
	if(DoButton_CheckBox(&g_Config.m_ClAutoRaceRecord, Localize("Save the best demo of each race"), g_Config.m_ClAutoRaceRecord, &Button))
	{
		g_Config.m_ClAutoRaceRecord ^= 1;
	}

	Left.HSplitTop(20.0f, &Button, &Left);
	if(DoButton_CheckBox(&g_Config.m_ClReplays, Localize("Enable replays"), g_Config.m_ClReplays, &Button))
	{
		g_Config.m_ClReplays ^= 1;
		if(Client()->State() == IClient::STATE_ONLINE)
		{
			Client()->DemoRecorder_UpdateReplayRecorder();
		}
	}

	Left.HSplitTop(20.0f, &Button, &Left);
	if(g_Config.m_ClReplays)
		Ui()->DoScrollbarOption(&g_Config.m_ClReplayLength, &g_Config.m_ClReplayLength, &Button, Localize("Default length"), 10, 600, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE);

	Right.HSplitTop(20.0f, &Button, &Right);
	if(DoButton_CheckBox(&g_Config.m_ClRaceGhost, Localize("Enable ghost"), g_Config.m_ClRaceGhost, &Button))
	{
		g_Config.m_ClRaceGhost ^= 1;
	}
	GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ClRaceGhost, &Button, Localize("When you cross the start line, show a ghost tee replicating the movements of your best time"));

	if(g_Config.m_ClRaceGhost)
	{
		Right.HSplitTop(20.0f, &Button, &Right);
		Button.VSplitMid(&LeftLeft, &Button);
		if(DoButton_CheckBox(&g_Config.m_ClRaceShowGhost, Localize("Show ghost"), g_Config.m_ClRaceShowGhost, &LeftLeft))
		{
			g_Config.m_ClRaceShowGhost ^= 1;
		}
		Ui()->DoScrollbarOption(&g_Config.m_ClRaceGhostAlpha, &g_Config.m_ClRaceGhostAlpha, &Button, Localize("Opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");

		Right.HSplitTop(20.0f, &Button, &Right);
		if(DoButton_CheckBox(&g_Config.m_ClRaceSaveGhost, Localize("Save ghost"), g_Config.m_ClRaceSaveGhost, &Button))
		{
			g_Config.m_ClRaceSaveGhost ^= 1;
		}

		if(g_Config.m_ClRaceSaveGhost)
		{
			Right.HSplitTop(20.0f, &Button, &Right);
			if(DoButton_CheckBox(&g_Config.m_ClRaceGhostSaveBest, Localize("Only save improvements"), g_Config.m_ClRaceGhostSaveBest, &Button))
			{
				g_Config.m_ClRaceGhostSaveBest ^= 1;
			}
		}
	}

	// gameplay
	CUIRect Gameplay;
	MainView.HSplitTop(170.0f, &Gameplay, &MainView);
	Gameplay.HSplitTop(30.0f, &Label, &Gameplay);
	Ui()->DoLabel(&Label, Localize("Gameplay"), 20.0f, TEXTALIGN_ML);
	Gameplay.HSplitTop(5.0f, nullptr, &Gameplay);
	Gameplay.VSplitMid(&Left, &Right, 20.0f);

	Left.HSplitTop(20.0f, &Button, &Left);
	Ui()->DoScrollbarOption(&g_Config.m_ClOverlayEntities, &g_Config.m_ClOverlayEntities, &Button, Localize("Overlay entities"), 0, 100);

	Left.HSplitTop(20.0f, &Button, &Left);
	Button.VSplitMid(&LeftLeft, &Button);

	if(DoButton_CheckBox(&g_Config.m_ClTextEntities, Localize("Show text entities"), g_Config.m_ClTextEntities, &LeftLeft))
		g_Config.m_ClTextEntities ^= 1;

	if(g_Config.m_ClTextEntities)
	{
		if(Ui()->DoScrollbarOption(&g_Config.m_ClTextEntitiesSize, &g_Config.m_ClTextEntitiesSize, &Button, Localize("Size"), 20, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE))
			GameClient()->m_MapImages.SetTextureScale(g_Config.m_ClTextEntitiesSize);
	}

	Left.HSplitTop(20.0f, &Button, &Left);
	Button.VSplitMid(&LeftLeft, &Button);

	if(DoButton_CheckBox(&g_Config.m_ClShowOthers, Localize("Show others"), g_Config.m_ClShowOthers == SHOW_OTHERS_ON, &LeftLeft))
		g_Config.m_ClShowOthers = g_Config.m_ClShowOthers != SHOW_OTHERS_ON ? SHOW_OTHERS_ON : SHOW_OTHERS_OFF;

	Ui()->DoScrollbarOption(&g_Config.m_ClShowOthersAlpha, &g_Config.m_ClShowOthersAlpha, &Button, Localize("Opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");

	GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ClShowOthersAlpha, &Button, Localize("Adjust the opacity of entities belonging to other teams, such as tees and name plates"));

	Left.HSplitTop(20.0f, &Button, &Left);
	static int s_ShowOwnTeamId = 0;
	if(DoButton_CheckBox(&s_ShowOwnTeamId, Localize("Show others (own team only)"), g_Config.m_ClShowOthers == SHOW_OTHERS_ONLY_TEAM, &Button))
	{
		g_Config.m_ClShowOthers = g_Config.m_ClShowOthers != SHOW_OTHERS_ONLY_TEAM ? SHOW_OTHERS_ONLY_TEAM : SHOW_OTHERS_OFF;
	}

	Left.HSplitTop(20.0f, &Button, &Left);
	if(DoButton_CheckBox(&g_Config.m_ClShowQuads, Localize("Show background quads"), g_Config.m_ClShowQuads, &Button))
	{
		g_Config.m_ClShowQuads ^= 1;
	}
	GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ClShowQuads, &Button, Localize("Quads are used for background decoration"));

	Left.HSplitTop(20.0f, &Button, &Left);
	if(Ui()->DoScrollbarOption(&g_Config.m_ClDefaultZoom, &g_Config.m_ClDefaultZoom, &Button, Localize("Default zoom"), 0, 20))
		GameClient()->m_Camera.SetZoom(CCamera::ZoomStepsToValue(g_Config.m_ClDefaultZoom - 10), g_Config.m_ClSmoothZoomTime, true);

	Right.HSplitTop(20.0f, &Button, &Right);
	Ui()->DoScrollbarOption(&g_Config.m_ClPredictionMargin, &g_Config.m_ClPredictionMargin, &Button, Localize("Prediction margin"), 1, 300);

	Right.HSplitTop(20.0f, &Button, &Right);
	if(DoButton_CheckBox(&g_Config.m_ClPredictEvents, Localize("Predict events (experimental)"), g_Config.m_ClPredictEvents, &Button))
	{
		g_Config.m_ClPredictEvents ^= 1;
	}

	Right.HSplitTop(20.0f, &Button, &Right);
	if(DoButton_CheckBox(&g_Config.m_ClAntiPing, Localize("AntiPing"), g_Config.m_ClAntiPing, &Button))
	{
		g_Config.m_ClAntiPing ^= 1;
	}
	GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ClAntiPing, &Button, Localize("Tries to predict other entities to give a feel of low latency"));

	if(g_Config.m_ClAntiPing)
	{
		Right.HSplitTop(20.0f, &Button, &Right);
		if(DoButton_CheckBox(&g_Config.m_ClAntiPingPlayers, Localize("AntiPing: predict other players"), g_Config.m_ClAntiPingPlayers, &Button))
		{
			g_Config.m_ClAntiPingPlayers ^= 1;
		}

		Right.HSplitTop(20.0f, &Button, &Right);
		if(DoButton_CheckBox(&g_Config.m_ClAntiPingWeapons, Localize("AntiPing: predict weapons"), g_Config.m_ClAntiPingWeapons, &Button))
		{
			g_Config.m_ClAntiPingWeapons ^= 1;
		}

		Right.HSplitTop(20.0f, &Button, &Right);
		if(DoButton_CheckBox(&g_Config.m_ClAntiPingGrenade, Localize("AntiPing: predict grenade paths"), g_Config.m_ClAntiPingGrenade, &Button))
		{
			g_Config.m_ClAntiPingGrenade ^= 1;
		}
	}

	CUIRect Background, Miscellaneous;
	MainView.VSplitMid(&Background, &Miscellaneous, 20.0f);

	// background
	Background.HSplitTop(30.0f, &Label, &Background);
	Background.HSplitTop(5.0f, nullptr, &Background);
	Ui()->DoLabel(&Label, Localize("Background"), 20.0f, TEXTALIGN_ML);

	ColorRGBA GreyDefault(0.5f, 0.5f, 0.5f, 1);

	static CButtonContainer s_ResetId1;
	DoLine_ColorPicker(&s_ResetId1, 25.0f, 13.0f, 5.0f, &Background, Localize("Regular background color"), &g_Config.m_ClBackgroundColor, GreyDefault, false);

	static CButtonContainer s_ResetId2;
	DoLine_ColorPicker(&s_ResetId2, 25.0f, 13.0f, 5.0f, &Background, Localize("Entities background color"), &g_Config.m_ClBackgroundEntitiesColor, GreyDefault, false);

	CUIRect EditBox, ReloadButton;
	Background.HSplitTop(20.0f, &Label, &Background);
	Background.HSplitTop(2.0f, nullptr, &Background);
	Label.VSplitLeft(100.0f, &Label, &EditBox);
	EditBox.VSplitRight(60.0f, &EditBox, &Button);
	Button.VSplitMid(&ReloadButton, &Button, 5.0f);
	EditBox.VSplitRight(5.0f, &EditBox, nullptr);

	Ui()->DoLabel(&Label, Localize("Map"), 14.0f, TEXTALIGN_ML);

	static CLineInput s_BackgroundEntitiesInput(g_Config.m_ClBackgroundEntities, sizeof(g_Config.m_ClBackgroundEntities));
	Ui()->DoEditBox(&s_BackgroundEntitiesInput, &EditBox, 14.0f);

	static CButtonContainer s_BackgroundEntitiesMapPicker, s_BackgroundEntitiesReload;

	if(Ui()->DoButton_FontIcon(&s_BackgroundEntitiesReload, FontIcon::ARROW_ROTATE_RIGHT, 0, &ReloadButton, BUTTONFLAG_LEFT))
	{
		GameClient()->m_Background.LoadBackground();
	}

	if(Ui()->DoButton_FontIcon(&s_BackgroundEntitiesMapPicker, FontIcon::FOLDER, 0, &Button, BUTTONFLAG_LEFT))
	{
		static SPopupMenuId s_PopupMapPickerId;
		static CPopupMapPickerContext s_PopupMapPickerContext;
		s_PopupMapPickerContext.m_pMenus = this;
		s_PopupMapPickerContext.MapListPopulate();
		Ui()->DoPopupMenu(&s_PopupMapPickerId, Ui()->MouseX(), Ui()->MouseY(), 300.0f, 250.0f, &s_PopupMapPickerContext, PopupMapPicker);
	}

	Background.HSplitTop(20.0f, &Button, &Background);
	const bool UseCurrentMap = str_comp(g_Config.m_ClBackgroundEntities, CURRENT_MAP) == 0;
	static int s_UseCurrentMapId = 0;
	if(DoButton_CheckBox(&s_UseCurrentMapId, Localize("Use current map as background"), UseCurrentMap, &Button))
	{
		if(UseCurrentMap)
			g_Config.m_ClBackgroundEntities[0] = '\0';
		else
			str_copy(g_Config.m_ClBackgroundEntities, CURRENT_MAP);
		GameClient()->m_Background.LoadBackground();
	}

	Background.HSplitTop(20.0f, &Button, &Background);
	if(DoButton_CheckBox(&g_Config.m_ClBackgroundShowTilesLayers, Localize("Show tiles layers from BG map"), g_Config.m_ClBackgroundShowTilesLayers, &Button))
		g_Config.m_ClBackgroundShowTilesLayers ^= 1;

	// miscellaneous
	Miscellaneous.HSplitTop(30.0f, &Label, &Miscellaneous);
	Miscellaneous.HSplitTop(5.0f, nullptr, &Miscellaneous);

	Ui()->DoLabel(&Label, Localize("Miscellaneous"), 20.0f, TEXTALIGN_ML);

	static CButtonContainer s_ButtonTimeout;
	Miscellaneous.HSplitTop(20.0f, &Button, &Miscellaneous);
	if(DoButton_Menu(&s_ButtonTimeout, Localize("New random timeout code"), 0, &Button))
	{
		Client()->GenerateTimeoutSeed();
	}

	Miscellaneous.HSplitTop(5.0f, nullptr, &Miscellaneous);
	Miscellaneous.HSplitTop(20.0f, &Label, &Miscellaneous);
	Miscellaneous.HSplitTop(2.0f, nullptr, &Miscellaneous);
	Ui()->DoLabel(&Label, Localize("Run on join"), 14.0f, TEXTALIGN_ML);
	Miscellaneous.HSplitTop(20.0f, &Button, &Miscellaneous);
	static CLineInput s_RunOnJoinInput(g_Config.m_ClRunOnJoin, sizeof(g_Config.m_ClRunOnJoin));
	s_RunOnJoinInput.SetEmptyText(Localize("Chat command (e.g. showall 1)"));
	Ui()->DoEditBox(&s_RunOnJoinInput, &Button, 14.0f);

#if defined(CONF_FAMILY_WINDOWS)
	static CButtonContainer s_ButtonUnregisterShell;
	Miscellaneous.HSplitTop(10.0f, nullptr, &Miscellaneous);
	Miscellaneous.HSplitTop(20.0f, &Button, &Miscellaneous);
	if(DoButton_Menu(&s_ButtonUnregisterShell, Localize("Unregister protocol and file extensions"), 0, &Button))
	{
		Client()->ShellUnregister();
	}
#endif

	// Updater
#if defined(CONF_AUTOUPDATE)
	{
		const bool NeedUpdate = GameClient()->m_BestClient.NeedUpdate();
		IUpdater::EUpdaterState State = Updater()->GetCurrentState();

		// Update Button
		char aBuf[256];
		if(NeedUpdate && State <= IUpdater::CLEAN)
		{
			str_format(aBuf, sizeof(aBuf), "BestClient %s Is release", GameClient()->m_BestClient.m_aVersionStr);
			UpdaterRect.VSplitLeft(TextRender()->TextWidth(14.0f, aBuf, -1, -1.0f) + 10.0f, &UpdaterRect, &Button);
			Button.VSplitLeft(100.0f, &Button, nullptr);
			static CButtonContainer s_ButtonUpdate;
			if(DoButton_Menu(&s_ButtonUpdate, Localize("Update now"), 0, &Button))
			{
				Updater()->InitiateUpdate();
			}
		}
		else if(State >= IUpdater::GETTING_MANIFEST && State < IUpdater::NEED_RESTART)
			str_copy(aBuf, Localize("Updating…"));
		else if(State == IUpdater::NEED_RESTART)
		{
			str_copy(aBuf, Localize("BestClient Client updated!"));
			m_NeedRestartUpdate = true;
		}
		else
		{
			str_copy(aBuf, Localize("No updates available"));
			UpdaterRect.VSplitLeft(TextRender()->TextWidth(14.0f, aBuf, -1, -1.0f) + 10.0f, &UpdaterRect, &Button);
			Button.VSplitLeft(100.0f, &Button, nullptr);
			static CButtonContainer s_ButtonUpdate;
			if(DoButton_Menu(&s_ButtonUpdate, Localize("Check now"), 0, &Button))
			{
				GameClient()->m_BestClient.FetchBestClientInfo();
			}
		}
		Ui()->DoLabel(&UpdaterRect, aBuf, 14.0f, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	}
#endif
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
		Localize("Visuals"),
		"Gameplay",
		Localize("Others"),
		Localize("Fun"),
		"Shop",
		Localize("Editors"),
		"info",
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
			GameClient()->m_Tooltips.DoToolTip(&s_HookComboResetButton, &ResetHitbox, Localize("Reset to defaults"));
			if(HookComboResetClicked)
			{
				g_Config.m_BcHookComboMode = DefaultConfig::BcHookComboMode;
				g_Config.m_BcHookComboResetTime = DefaultConfig::BcHookComboResetTime;
				g_Config.m_BcHookComboSoundVolume = DefaultConfig::BcHookComboSoundVolume;
				g_Config.m_BcHookComboSize = DefaultConfig::BcHookComboSize;
			}
			Ui()->DoLabel(&TitleLabel, Localize("Hook combo"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcHookCombo, Localize("Hook combo"), &g_Config.m_BcHookCombo, &Content, LineSize);
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
				Ui()->DoLabel(&ModeLabel, Localize("Mode"), 14.0f, TEXTALIGN_ML);

				Expand.HSplitTop(LineSize, &ModeRow, &Expand);
				CUIRect HookButton, HammerButton, HookHammerButton;
				ModeRow.VSplitLeft(ModeRow.w / 3.0f, &HookButton, &ModeRow);
				ModeRow.VSplitLeft(ModeRow.w / 2.0f, &HammerButton, &HookHammerButton);

				static CButtonContainer s_HookComboModeHook;
				static CButtonContainer s_HookComboModeHammer;
				static CButtonContainer s_HookComboModeHookHammer;
				if(DoButton_Menu(&s_HookComboModeHook, "hook", g_Config.m_BcHookComboMode == 0, &HookButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
					g_Config.m_BcHookComboMode = 0;
				if(DoButton_Menu(&s_HookComboModeHammer, "hammer", g_Config.m_BcHookComboMode == 1, &HammerButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
					g_Config.m_BcHookComboMode = 1;
				if(DoButton_Menu(&s_HookComboModeHookHammer, "hook&hammer", g_Config.m_BcHookComboMode == 2, &HookHammerButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
					g_Config.m_BcHookComboMode = 2;

				Expand.HSplitTop(LineSize, &Button, &Expand);
				DoSliderWithScaledValue(&g_Config.m_BcHookComboResetTime, &g_Config.m_BcHookComboResetTime, &Button, Localize("Max time between hooks"), 100, 5000, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");

				Expand.HSplitTop(LineSize, &Button, &Expand);
				DoSliderWithScaledValue(&g_Config.m_BcHookComboSoundVolume, &g_Config.m_BcHookComboSoundVolume, &Button, Localize("Hook combo sound volume"), 0, 100, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "%");

				Expand.HSplitTop(LineSize, &Button, &Expand);
				DoSliderWithScaledValue(&g_Config.m_BcHookComboSize, &g_Config.m_BcHookComboSize, &Button, Localize("Hook combo size"), 50, 200, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "%");
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
			GameClient()->m_Tooltips.DoToolTip(&s_JellyTeeResetButton, &ResetHitbox, Localize("Reset to defaults"));
			if(JellyTeeResetClicked)
			{
				g_Config.m_BcJellyTeeOthers = DefaultConfig::BcJellyTeeOthers;
				g_Config.m_BcJellyTeeStrength = DefaultConfig::BcJellyTeeStrength;
				g_Config.m_BcJellyTeeDuration = DefaultConfig::BcJellyTeeDuration;
			}
			Ui()->DoLabel(&TitleLabel, Localize("Jelly Tee"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcJellyTee, Localize("Enable Jelly Tee"), &g_Config.m_BcJellyTee, &Content, LineSize);

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

				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcJellyTeeOthers, Localize("Jelly Others"), &g_Config.m_BcJellyTeeOthers, &Expand, LineSize);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcJellyTeeStrength, &g_Config.m_BcJellyTeeStrength, &Row, Localize("Jelly strength"), 0, 1000);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcJellyTeeDuration, &g_Config.m_BcJellyTeeDuration, &Row, Localize("Jelly duration"), 1, 500);
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
			GameClient()->m_Tooltips.DoToolTip(&s_3DParticlesResetButton, &ResetHitbox, Localize("Reset to defaults"));
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
			Ui()->DoLabel(&TitleLabel, Localize("3D Particles"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_Bc3dParticles, Localize("3D Particles"), &g_Config.m_Bc3dParticles, &Content, LineSize);

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
				Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesCount, &g_Config.m_Bc3dParticlesCount, &Row, Localize("Particles count"), 1, 200);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				CUIRect TypeLabel, TypeSelect;
				Row.VSplitLeft(150.0f, &TypeLabel, &TypeSelect);
				Ui()->DoLabel(&TypeLabel, Localize("Particle type"), 14.0f, TEXTALIGN_ML);

				static CUi::SDropDownState s_3DParticlesTypeState;
				static CScrollRegion s_3DParticlesTypeScrollRegion;
				s_3DParticlesTypeState.m_SelectionPopupContext.m_pScrollRegion = &s_3DParticlesTypeScrollRegion;
				const char *ap3DParticleTypes[3] = {
					Localize("Cube"),
					Localize("Heart"),
					Localize("Mixed"),
				};
				g_Config.m_Bc3dParticlesType = Ui()->DoDropDown(&TypeSelect, g_Config.m_Bc3dParticlesType - 1, ap3DParticleTypes, (int)std::size(ap3DParticleTypes), s_3DParticlesTypeState) + 1;

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesSizeMax, &g_Config.m_Bc3dParticlesSizeMax, &Row, Localize("Size"), 2, 200);
				g_Config.m_Bc3dParticlesSizeMin = std::max(2, g_Config.m_Bc3dParticlesSizeMax - 3);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesSpeed, &g_Config.m_Bc3dParticlesSpeed, &Row, Localize("Speed"), 1, 500);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesAlpha, &g_Config.m_Bc3dParticlesAlpha, &Row, Localize("Alpha"), 1, 100);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				CUIRect ColorModeLabel, ColorModeSelect;
				Row.VSplitLeft(150.0f, &ColorModeLabel, &ColorModeSelect);
				Ui()->DoLabel(&ColorModeLabel, Localize("Color mode"), 14.0f, TEXTALIGN_ML);

				static CUi::SDropDownState s_3DParticlesColorModeState;
				static CScrollRegion s_3DParticlesColorModeScrollRegion;
				s_3DParticlesColorModeState.m_SelectionPopupContext.m_pScrollRegion = &s_3DParticlesColorModeScrollRegion;
				const char *ap3DParticleColorModes[2] = {
					Localize("Custom"),
					Localize("Random"),
				};
				g_Config.m_Bc3dParticlesColorMode = Ui()->DoDropDown(&ColorModeSelect, g_Config.m_Bc3dParticlesColorMode - 1, ap3DParticleColorModes, (int)std::size(ap3DParticleColorModes), s_3DParticlesColorModeState) + 1;

				if(g_Config.m_Bc3dParticlesColorMode == 1)
				{
					static CButtonContainer s_3DParticlesColorButton;
					DoLine_ColorPicker(&s_3DParticlesColorButton, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerSpacing, &Expand, Localize("Color"), &g_Config.m_Bc3dParticlesColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
				}

				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_Bc3dParticlesGlow, Localize("Glow"), &g_Config.m_Bc3dParticlesGlow, &Expand, LineSize);

				const float GlowHeight = GlowTargetHeight * s_Bc3dParticlesGlowPhase;
				if(GlowHeight > 0.0f)
				{
					CUIRect GlowVisible;
					Expand.HSplitTop(GlowHeight, &GlowVisible, &Expand);
					Ui()->ClipEnable(&GlowVisible);
					SScopedClip GlowClipGuard{Ui()};

					CUIRect GlowExpand = {GlowVisible.x, GlowVisible.y, GlowVisible.w, GlowTargetHeight};
					GlowExpand.HSplitTop(LineSize, &Row, &GlowExpand);
					Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesGlowAlpha, &g_Config.m_Bc3dParticlesGlowAlpha, &Row, Localize("Glow alpha"), 1, 100);
					GlowExpand.HSplitTop(LineSize, &Row, &GlowExpand);
					Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesGlowOffset, &g_Config.m_Bc3dParticlesGlowOffset, &Row, Localize("Glow offset"), 1, 20);
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
			GameClient()->m_Tooltips.DoToolTip(&s_OrbitAuraResetButton, &ResetHitbox, Localize("Reset to defaults"));
			if(OrbitAuraResetClicked)
			{
				g_Config.m_BcOrbitAuraRadius = DefaultConfig::BcOrbitAuraRadius;
				g_Config.m_BcOrbitAuraParticles = DefaultConfig::BcOrbitAuraParticles;
				g_Config.m_BcOrbitAuraAlpha = DefaultConfig::BcOrbitAuraAlpha;
				g_Config.m_BcOrbitAuraSpeed = DefaultConfig::BcOrbitAuraSpeed;
				g_Config.m_BcOrbitAuraIdle = DefaultConfig::BcOrbitAuraIdle;
				g_Config.m_BcOrbitAuraIdleTimer = DefaultConfig::BcOrbitAuraIdleTimer;
			}
			Ui()->DoLabel(&TitleLabel, Localize("Orbit Aura"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOrbitAura, Localize("Orbit Aura"), &g_Config.m_BcOrbitAura, &Content, LineSize);

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

				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOrbitAuraIdle, Localize("Enable in idle mode"), &g_Config.m_BcOrbitAuraIdle, &Expand, LineSize);

				const float OrbitIdleHeight = OrbitIdleTargetHeight * s_OrbitAuraIdlePhase;
				if(OrbitIdleHeight > 0.0f)
				{
					CUIRect IdleVisible;
					Expand.HSplitTop(OrbitIdleHeight, &IdleVisible, &Expand);
					Ui()->ClipEnable(&IdleVisible);
					SScopedClip IdleClipGuard{Ui()};

					CUIRect IdleExpand = {IdleVisible.x, IdleVisible.y, IdleVisible.w, OrbitIdleTargetHeight};
					IdleExpand.HSplitTop(LineSize, &Row, &IdleExpand);
					Ui()->DoScrollbarOption(&g_Config.m_BcOrbitAuraIdleTimer, &g_Config.m_BcOrbitAuraIdleTimer, &Row, Localize("Idle delay"), 1, 30);
				}

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcOrbitAuraRadius, &g_Config.m_BcOrbitAuraRadius, &Row, Localize("Aura radius"), 8, 200);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcOrbitAuraParticles, &g_Config.m_BcOrbitAuraParticles, &Row, Localize("Particles"), 2, 120);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcOrbitAuraAlpha, &g_Config.m_BcOrbitAuraAlpha, &Row, Localize("Aura alpha"), 0, 100);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcOrbitAuraSpeed, &g_Config.m_BcOrbitAuraSpeed, &Row, Localize("Aura speed"), 10, 200);
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
			Ui()->DoLabel(&Label, Localize("Media Background"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			const bool MenuMediaChanged = DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMenuMediaBackground, Localize("Enable to main menu"), &g_Config.m_BcMenuMediaBackground, &Content, LineSize);
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

			Content.HSplitTop(LineSize, &Row, &Content);
			Ui()->DoScrollbarOption(&g_Config.m_BcGameMediaBackgroundOffset, &g_Config.m_BcGameMediaBackgroundOffset, &Row, Localize("Map offset"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");
			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_BcGameMediaBackgroundOffset, &Row, Localize("0 keeps the image fixed to the screen. 100 fixes it to the map for a full parallax effect."));

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
			Ui()->DoLabel(&Label, Localize("Sweat Weapon"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcCrystalLaser, Localize("Enable"), &g_Config.m_BcCrystalLaser, &Content, LineSize);

			Content.HSplitTop(MarginSmall, nullptr, &Content);
			Content.HSplitTop(LineSize, &PreviewLabel, &Content);
			Ui()->DoLabel(&PreviewLabel, Localize("Crystal Laser"), 14.0f, TEXTALIGN_ML);
			Content.HSplitTop(58.0f, &PreviewRect, &Content);
			DoLaserPreview(&PreviewRect, ColorHSLA(g_Config.m_ClLaserRifleOutlineColor), ColorHSLA(g_Config.m_ClLaserRifleInnerColor), LASERTYPE_RIFLE);

			Content.HSplitTop(MarginSmall, nullptr, &Content);
			Content.HSplitTop(LineSize, &PreviewLabel, &Content);
			Ui()->DoLabel(&PreviewLabel, Localize("Sand Shotgun"), 14.0f, TEXTALIGN_ML);
			Content.HSplitTop(58.0f, &PreviewRect, &Content);
			DoLaserPreview(&PreviewRect, ColorHSLA(g_Config.m_ClLaserShotgunOutlineColor), ColorHSLA(g_Config.m_ClLaserShotgunInnerColor), LASERTYPE_SHOTGUN);
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		// Chat bubbles (left column block)
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
			GameClient()->m_Tooltips.DoToolTip(&s_ChatBubblesResetButton, &ResetHitbox, Localize("Reset to defaults"));
			if(ChatBubblesResetClicked)
			{
				g_Config.m_BcChatBubblesDemo = DefaultConfig::BcChatBubblesDemo;
				g_Config.m_BcChatBubblesSelf = DefaultConfig::BcChatBubblesSelf;
				g_Config.m_BcChatBubbleSize = DefaultConfig::BcChatBubbleSize;
				g_Config.m_BcChatBubbleShowTime = DefaultConfig::BcChatBubbleShowTime;
				g_Config.m_BcChatBubbleFadeIn = DefaultConfig::BcChatBubbleFadeIn;
				g_Config.m_BcChatBubbleFadeOut = DefaultConfig::BcChatBubbleFadeOut;
			}
			Ui()->DoLabel(&TitleLabel, Localize("Chat Bubbles"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatBubbles, Localize("Chat Bubbles"), &g_Config.m_BcChatBubbles, &Content, LineSize);
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
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatBubblesDemo, Localize("Show Chatbubbles in demo"), &g_Config.m_BcChatBubblesDemo, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatBubblesSelf, Localize("Show Chatbubbles above you"), &g_Config.m_BcChatBubblesSelf, &Expand, LineSize);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcChatBubbleSize, &g_Config.m_BcChatBubbleSize, &Row, Localize("Chat Bubble Size"), 20, 30);
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcChatBubbleShowTime, &g_Config.m_BcChatBubbleShowTime, &Row, Localize("Show the Bubbles for"), 200, 1000);
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcChatBubbleFadeIn, &g_Config.m_BcChatBubbleFadeIn, &Row, Localize("fade in for"), 15, 100);
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcChatBubbleFadeOut, &g_Config.m_BcChatBubbleFadeOut, &Row, Localize("fade out for"), 15, 100);
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
			GameClient()->m_Tooltips.DoToolTip(&s_AnimationsResetButton, &ResetHitbox, Localize("Reset to defaults"));
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
			Ui()->DoLabel(&TitleLabel, Localize("Animations"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcAnimations, Localize("Enable animations"), &g_Config.m_BcAnimations, &Content, LineSize);

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
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcModuleUiRevealAnimation, Localize("Module settings reveals"), &g_Config.m_BcModuleUiRevealAnimation, &Expand, LineSize);
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcModuleUiRevealAnimationMs, &g_Config.m_BcModuleUiRevealAnimationMs, &Row, Localize("Module reveal time (ms)"), 1, 500);

				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcIngameMenuAnimation, Localize("ESC menu animation"), &g_Config.m_BcIngameMenuAnimation, &Expand, LineSize);
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcIngameMenuAnimationMs, &g_Config.m_BcIngameMenuAnimationMs, &Row, Localize("ESC menu animation time (ms)"), 1, 500);

				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatAnimation, Localize("Chat message animations"), &g_Config.m_BcChatAnimation, &Expand, LineSize);
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcChatAnimationMs, &g_Config.m_BcChatAnimationMs, &Row, Localize("Chat message animation time (ms)"), 1, 500);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatOpenAnimation, Localize("Chat open animation"), &g_Config.m_BcChatOpenAnimation, &Expand, LineSize);
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcChatOpenAnimationMs, &g_Config.m_BcChatOpenAnimationMs, &Row, Localize("Chat open animation time (ms)"), 1, 500);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatTypingAnimation, Localize("Chat typing animation"), &g_Config.m_BcChatTypingAnimation, &Expand, LineSize);
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcChatTypingAnimationMs, &g_Config.m_BcChatTypingAnimationMs, &Row, Localize("Chat typing animation time (ms)"), 1, 500);

				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcKillfeedAnimation, Localize("Killfeed animation"), &g_Config.m_BcKillfeedAnimation, &Expand, LineSize);
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcKillfeedAnimationMs, &g_Config.m_BcKillfeedAnimationMs, &Row, Localize("Killfeed animation time (ms)"), 1, 500);
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
			Ui()->DoLabel(&Label, Localize("Music Player"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMusicPlayer, Localize("Enable music player"), &g_Config.m_BcMusicPlayer, &Content, LineSize);

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
				Ui()->DoLabel(&ModeLabel, Localize("Color mode"), 14.0f, TEXTALIGN_ML);

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
				g_Config.m_BcMusicPlayerColorMode = Ui()->DoDropDown(&ModeDropDown, g_Config.m_BcMusicPlayerColorMode, apMusicPlayerColorModes, (int)std::size(apMusicPlayerColorModes), s_MusicPlayerColorModeState);

				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMusicPlayerVisualizer, Localize("Enable visualizer"), &g_Config.m_BcMusicPlayerVisualizer, &Expand, LineSize);

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
					Ui()->DoLabel(&ModeLabel, Localize("Visualizer mode"), 14.0f, TEXTALIGN_ML);

					static CUi::SDropDownState s_MusicPlayerVisualizerModeState;
					static CScrollRegion s_MusicPlayerVisualizerModeScrollRegion;
					s_MusicPlayerVisualizerModeState.m_SelectionPopupContext.m_pScrollRegion = &s_MusicPlayerVisualizerModeScrollRegion;
					const char *apMusicPlayerVisualizerModes[2] = {
						Localize("Bottom"),
						Localize("Center"),
					};
					g_Config.m_BcMusicPlayerVisualizerMode = std::clamp(g_Config.m_BcMusicPlayerVisualizerMode, 0, 1);
					g_Config.m_BcMusicPlayerVisualizerMode = Ui()->DoDropDown(&ModeDropDown, g_Config.m_BcMusicPlayerVisualizerMode, apMusicPlayerVisualizerModes, (int)std::size(apMusicPlayerVisualizerModes), s_MusicPlayerVisualizerModeState);

					CUIRect SliderRow, SliderLabel, SliderButton;
					VisualizerExpand.HSplitTop(VisualizerSliderHeight, &SliderRow, &VisualizerExpand);
					SliderRow.VSplitLeft(120.0f, &SliderLabel, &SliderButton);
					Ui()->DoLabel(&SliderLabel, Localize("Visualizer sensitivity"), 14.0f, TEXTALIGN_ML);
					Ui()->DoScrollbarOption(&g_Config.m_BcMusicPlayerVisualizerSensitivity, &g_Config.m_BcMusicPlayerVisualizerSensitivity, &SliderButton, "", 50, 300, &CUi::ms_LinearScrollbarScale, 0u, "%");

					VisualizerExpand.HSplitTop(VisualizerSliderHeight, &SliderRow, &VisualizerExpand);
					SliderRow.VSplitLeft(120.0f, &SliderLabel, &SliderButton);
					Ui()->DoLabel(&SliderLabel, Localize("Visualizer smoothing"), 14.0f, TEXTALIGN_ML);
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
					DoLine_ColorPicker(&s_MusicPlayerStaticColorButton, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerSpacing, &StaticExpand, Localize("Static color"), &g_Config.m_BcMusicPlayerStaticColor, ColorRGBA(0.34f, 0.53f, 0.79f, 1.0f), false);
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
			GameClient()->m_Tooltips.DoToolTip(&s_CameraDriftResetButton, &ResetHitbox, Localize("Reset to defaults"));
			if(CameraDriftResetClicked)
			{
				g_Config.m_BcCameraDriftAmount = DefaultConfig::BcCameraDriftAmount;
				g_Config.m_BcCameraDriftSmoothness = DefaultConfig::BcCameraDriftSmoothness;
				g_Config.m_BcCameraDriftReverse = DefaultConfig::BcCameraDriftReverse;
			}
			Ui()->DoLabel(&TitleLabel, Localize("Camera Drift"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcCameraDrift, Localize("Camera Drift"), &g_Config.m_BcCameraDrift, &Content, LineSize);

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
				Ui()->DoScrollbarOption(&g_Config.m_BcCameraDriftAmount, &g_Config.m_BcCameraDriftAmount, &Row, Localize("Camera drift amount"), 1, 200);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcCameraDriftSmoothness, &g_Config.m_BcCameraDriftSmoothness, &Row, Localize("Camera drift smoothness"), 1, 20);

				CUIRect DirectionLabel, DirectionButtons, DirectionForward, DirectionBackward;
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Row.VSplitLeft(150.0f, &DirectionLabel, &DirectionButtons);
				Ui()->DoLabel(&DirectionLabel, Localize("Drift direction"), 14.0f, TEXTALIGN_ML);
				DirectionButtons.VSplitMid(&DirectionForward, &DirectionBackward, MarginSmall);

				static int s_CameraDriftForwardButton = 0;
				static int s_CameraDriftBackwardButton = 0;
				if(DoButton_CheckBox(&s_CameraDriftForwardButton, Localize("Forward"), !g_Config.m_BcCameraDriftReverse, &DirectionForward))
					g_Config.m_BcCameraDriftReverse = 0;
				if(DoButton_CheckBox(&s_CameraDriftBackwardButton, Localize("Backward"), g_Config.m_BcCameraDriftReverse, &DirectionBackward))
					g_Config.m_BcCameraDriftReverse = 1;
			}
			if(IsBlockedCameraServer)
			{
				Content.HSplitTop(MarginSmall, nullptr, &Content);
				Content.HSplitTop(LineSize, &Label, &Content);
				TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
				Ui()->DoLabel(&Label, Localize("Looks like you're on a server where this feature is forbidden"), 14.0f, TEXTALIGN_ML);
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
			GameClient()->m_Tooltips.DoToolTip(&s_DynamicFovResetButton, &ResetHitbox, Localize("Reset to defaults"));
			if(DynamicFovResetClicked)
			{
				g_Config.m_BcDynamicFovAmount = DefaultConfig::BcDynamicFovAmount;
				g_Config.m_BcDynamicFovSmoothness = DefaultConfig::BcDynamicFovSmoothness;
			}
			Ui()->DoLabel(&TitleLabel, Localize("Dynamic FOV"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcDynamicFov, Localize("Dynamic FOV"), &g_Config.m_BcDynamicFov, &Content, LineSize);

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
				Ui()->DoScrollbarOption(&g_Config.m_BcDynamicFovAmount, &g_Config.m_BcDynamicFovAmount, &Row, Localize("Dynamic FOV amount"), 1, 200);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcDynamicFovSmoothness, &g_Config.m_BcDynamicFovSmoothness, &Row, Localize("Dynamic FOV smoothness"), 1, 20);
			}
			if(IsBlockedCameraServer)
			{
				Content.HSplitTop(MarginSmall, nullptr, &Content);
				Content.HSplitTop(LineSize, &Label, &Content);
				TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
				Ui()->DoLabel(&Label, Localize("Looks like you're on a server where this feature is forbidden"), 14.0f, TEXTALIGN_ML);
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
			GameClient()->m_Tooltips.DoToolTip(&s_AfterimageResetButton, &ResetHitbox, Localize("Reset to defaults"));
			if(AfterimageResetClicked)
			{
				g_Config.m_BcAfterimageFrames = DefaultConfig::BcAfterimageFrames;
				g_Config.m_BcAfterimageAlpha = DefaultConfig::BcAfterimageAlpha;
				g_Config.m_BcAfterimageSpacing = DefaultConfig::BcAfterimageSpacing;
			}
			Ui()->DoLabel(&TitleLabel, Localize("Afterimage"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcAfterimage, Localize("Enable Afterimage"), &g_Config.m_BcAfterimage, &Content, LineSize);

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
				Ui()->DoScrollbarOption(&g_Config.m_BcAfterimageFrames, &g_Config.m_BcAfterimageFrames, &Row, Localize("Afterimage frames"), 2, 20);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcAfterimageAlpha, &g_Config.m_BcAfterimageAlpha, &Row, Localize("Afterimage alpha"), 1, 100);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcAfterimageSpacing, &g_Config.m_BcAfterimageSpacing, &Row, Localize("Afterimage spacing"), 1, 64);
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
			GameClient()->m_Tooltips.DoToolTip(&s_MagicParticlesResetButton, &ResetHitbox, Localize("Reset to defaults"));
			if(MagicParticlesResetClicked)
			{
				g_Config.m_BcMagicParticlesCount = DefaultConfig::BcMagicParticlesCount;
				g_Config.m_BcMagicParticlesRadius = DefaultConfig::BcMagicParticlesRadius;
				g_Config.m_BcMagicParticlesSize = DefaultConfig::BcMagicParticlesSize;
				g_Config.m_BcMagicParticlesAlphaDelay = DefaultConfig::BcMagicParticlesAlphaDelay;
				g_Config.m_BcMagicParticlesType = DefaultConfig::BcMagicParticlesType;
			}
			Ui()->DoLabel(&TitleLabel, Localize("Magic Particles"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMagicParticles, Localize("Magic Particles"), &g_Config.m_BcMagicParticles, &Content, LineSize);

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
				Ui()->DoScrollbarOption(&g_Config.m_BcMagicParticlesCount, &g_Config.m_BcMagicParticlesCount, &Row, Localize("Particles count"), 1, 100);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcMagicParticlesRadius, &g_Config.m_BcMagicParticlesRadius, &Row, Localize("Radius"), 1, 1000);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcMagicParticlesSize, &g_Config.m_BcMagicParticlesSize, &Row, Localize("Size"), 1, 50);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcMagicParticlesAlphaDelay, &g_Config.m_BcMagicParticlesAlphaDelay, &Row, Localize("Alpha delay"), 1, 100);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				CUIRect TypeLabel, TypeSelect;
				Row.VSplitLeft(150.0f, &TypeLabel, &TypeSelect);
				Ui()->DoLabel(&TypeLabel, Localize("Particle type"), 14.0f, TEXTALIGN_ML);

				static CUi::SDropDownState s_MagicParticlesTypeState;
				static CScrollRegion s_MagicParticlesTypeScrollRegion;
				s_MagicParticlesTypeState.m_SelectionPopupContext.m_pScrollRegion = &s_MagicParticlesTypeScrollRegion;
				const char *apMagicParticleTypes[4] = {
					Localize("Slice"),
					Localize("Ball"),
					Localize("Smoke"),
					Localize("Shell"),
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
			Ui()->DoLabel(&Label, Localize("Aspect Ratio"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

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
			Content.HSplitTop(LineSize, &Row, &Content);
			SplitRowLabelControl(Row, PresetLabel, PresetDropDown);
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
				if(DoButton_Menu(&s_AspectApplyButton, Localize("Apply"), HasPendingCustomChange ? 0 : -1, &ApplyButton) && HasPendingCustomChange)
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
		Ui()->DoLabel(&Label, Localize("Editors"), 20.0f, TEXTALIGN_ML);
		MainView.HSplitTop(5.0f, nullptr, &MainView);

		MainView.HSplitTop(LineSize, &Label, &MainView);
		Ui()->DoLabel(&Label, Localize("Create mixed assets or jump to the name plate editor."), 14.0f, TEXTALIGN_ML);
		MainView.HSplitTop(5.0f, nullptr, &MainView);

		static CButtonContainer s_OpenAssetsEditorButton;
		MainView.HSplitTop(LineSize + 4.0f, &Button, &MainView);
		if(DoButton_Menu(&s_OpenAssetsEditorButton, Localize("Assets editor"), 0, &Button))
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
		Ui()->DoLabel(&Label, Localize("Open a dedicated component toggles page."), 14.0f, TEXTALIGN_ML);
		MainView.HSplitTop(5.0f, nullptr, &MainView);

		static CButtonContainer s_OpenComponentsEditorButton;
		MainView.HSplitTop(LineSize + 4.0f, &Button, &MainView);
		if(DoButton_Menu(&s_OpenComponentsEditorButton, Localize("Components editor"), 0, &Button))
			ComponentsEditorOpen();

		MainView.HSplitTop(30.0f, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Label, &MainView);
		Ui()->DoLabel(&Label, Localize("Edit HUD positions directly above the live game."), 14.0f, TEXTALIGN_ML);
		MainView.HSplitTop(5.0f, nullptr, &MainView);

		static CButtonContainer s_OpenHudEditorButton;
		MainView.HSplitTop(LineSize + 4.0f, &Button, &MainView);
		const bool CanOpenHudEditor = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
		if(DoButton_Menu(&s_OpenHudEditorButton, Localize("HUD editor"), CanOpenHudEditor ? 0 : -1, &Button) && CanOpenHudEditor)
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
			Ui()->DoLabel(&Label, Localize("Input"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFastInput, Localize("Fast Input"), &g_Config.m_TcFastInput, &Content, LineSize);

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

					if(DoButton_Menu(&s_FastInputModeFast, Localize("Fast input"), g_Config.m_BcFastInputMode == 0, &Left, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
						g_Config.m_BcFastInputMode = 0;
					if(DoButton_Menu(&s_FastInputModeDeltaInput, Localize("Delta input"), g_Config.m_BcFastInputMode == 1, &Middle, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
						g_Config.m_BcFastInputMode = 1;
					if(DoButton_Menu(&s_FastInputModeGammaInput, Localize("Gamma input"), g_Config.m_BcFastInputMode == 2, &Right, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
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
					DoSliderWithScaledValue(&g_Config.m_TcFastInputAmount, &g_Config.m_TcFastInputAmount, &Button, Localize("Amount"), 0, 40, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
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
						str_format(aBuf, sizeof(aBuf), "%s: %.2fM", Localize("Gamma amount"), Value / 100.0f);

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
						str_format(aBuf, sizeof(aBuf), "%s: %.2fA", Localize("Amount"), Value / 100.0f);

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
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFastInputOthers, Localize("Fast Input others"), &g_Config.m_TcFastInputOthers, &Expand, LineSize);
				else if(g_Config.m_BcFastInputMode == 1)
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcDeltaInputOthers, Localize("Delta input others"), &g_Config.m_BcDeltaInputOthers, &Expand, LineSize);
				else
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcGammaInputOthers, Localize("Gamma input others"), &g_Config.m_BcGammaInputOthers, &Expand, LineSize);
			}

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClSubTickAiming, Localize("Sub-Tick aiming"), &g_Config.m_ClSubTickAiming, &Content, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcFastInputAutoMargin, Localize("Auto margin"), &g_Config.m_BcFastInputAutoMargin, &Content, LineSize);
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
			Ui()->DoLabel(&Label, Localize("Snap Tap"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcSnapTap, Localize("Enable"), &g_Config.m_BcSnapTap, &Content, LineSize);

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
					str_format(aBuf, sizeof(aBuf), "%s: Off", Localize("Delay"));
				else
					str_format(aBuf, sizeof(aBuf), "%s: %dms", Localize("Delay"), Value);

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
				Ui()->DoLabel(&Label, Localize("Looks like you're on a server where this feature is forbidden"), FontSize, TEXTALIGN_ML);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}

			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);

			const bool HookPredictorEnabled = g_Config.m_BcHookTrajectoryPredictor != 0;
			float HookPredictorContentHeight = LineSize + MarginSmall + LineSize;
			if(HookPredictorEnabled)
				HookPredictorContentHeight += MarginSmall + LineSize + MarginSmall + LineSize;
			BeginBlock(Column, HookPredictorContentHeight, Content);
			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, Localize("Hook Predictor"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcHookTrajectoryPredictor, Localize("Enable"), &g_Config.m_BcHookTrajectoryPredictor, &Content, LineSize);
			if(g_Config.m_BcHookTrajectoryPredictor)
			{
				Content.HSplitTop(MarginSmall, nullptr, &Content);
				Content.HSplitTop(LineSize, &Button, &Content);
				DoSliderWithScaledValue(&g_Config.m_BcHookTrajectoryPredictorReleaseOffset, &g_Config.m_BcHookTrajectoryPredictorReleaseOffset, &Button, Localize("Release offset"), 0, 250, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
				Content.HSplitTop(MarginSmall, nullptr, &Content);
				Content.HSplitTop(LineSize, &Button, &Content);
				DoSliderWithScaledValue(&g_Config.m_BcHookTrajectoryPredictorReleaseWindow, &g_Config.m_BcHookTrajectoryPredictorReleaseWindow, &Button, Localize("Release window"), 5, 250, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
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
			Ui()->DoLabel(&Label, Localize("Optimizer"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizer, Localize("Enable optimizer"), &g_Config.m_BcOptimizer, &Content, LineSize);

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
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerDisableParticles, Localize("Disable all particles render"), &g_Config.m_BcOptimizerDisableParticles, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_GfxHighDetail, Localize("High Detail"), &g_Config.m_GfxHighDetail, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerFpsFog, Localize("FPS fog (cull outside limit)"), &g_Config.m_BcOptimizerFpsFog, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerDdnetPriorityHigh, Localize("DDNet priority: High"), &g_Config.m_BcOptimizerDdnetPriorityHigh, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerDiscordPriorityBelowNormal, Localize("Discord priority: Below Normal"), &g_Config.m_BcOptimizerDiscordPriorityBelowNormal, &Expand, LineSize);

				const float FogHeight = FogTargetHeight * s_OptimizerFogPhase;
				if(FogHeight > 0.0f)
				{
					CUIRect FogVisible;
					Expand.HSplitTop(FogHeight, &FogVisible, &Expand);
					Ui()->ClipEnable(&FogVisible);
					SScopedClip FogClipGuard{Ui()};

					CUIRect FogExpand = {FogVisible.x, FogVisible.y, FogVisible.w, FogTargetHeight};
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerFpsFogRenderRect, Localize("Render FPS fog rectangle"), &g_Config.m_BcOptimizerFpsFogRenderRect, &FogExpand, LineSize);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerFpsFogCullMapTiles, Localize("Cull map tiles outside FPS fog"), &g_Config.m_BcOptimizerFpsFogCullMapTiles, &FogExpand, LineSize);

					static std::vector<CButtonContainer> s_OptimizerFogModeButtons = {{}, {}};
					int FogMode = g_Config.m_BcOptimizerFpsFogMode;
					if(DoLine_RadioMenu(FogExpand, Localize("FPS fog mode"),
						   s_OptimizerFogModeButtons,
						   {Localize("Manual radius"), Localize("By zoom")},
						   {0, 1},
						   FogMode))
					{
						g_Config.m_BcOptimizerFpsFogMode = FogMode;
					}

					FogExpand.HSplitTop(LineSize, &Row, &FogExpand);
					if(g_Config.m_BcOptimizerFpsFogMode == 0)
						Ui()->DoScrollbarOption(&g_Config.m_BcOptimizerFpsFogRadiusTiles, &g_Config.m_BcOptimizerFpsFogRadiusTiles, &Row, Localize("Radius (tiles)"), 5, 300);
					else
						Ui()->DoScrollbarOption(&g_Config.m_BcOptimizerFpsFogZoomPercent, &g_Config.m_BcOptimizerFpsFogZoomPercent, &Row, Localize("Visible area (%)"), 10, 120, &CUi::ms_LinearScrollbarScale, 0, "%");
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
			Ui()->DoLabel(&Label, Localize("Gores mode"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcGoresMode, Localize("Enable gores mode"), &g_Config.m_BcGoresMode, &Content, LineSize);
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
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcGoresModeDisableIfWeapons, Localize("Disable if you have shotgun, grenade or laser"), &g_Config.m_BcGoresModeDisableIfWeapons, &Expand, LineSize);
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
			Ui()->DoLabel(&Label, Localize("Fast Actions"), HeadlineFontSize, TEXTALIGN_ML);
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
			str_format(aSlotLabel, sizeof(aSlotLabel), "%s %d", Localize("Selected slot"), s_SelectedBindIndex + 1);
			Ui()->DoLabel(&Button, aSlotLabel, FontSize, TEXTALIGN_ML);

			Content.HSplitTop(MarginSmall, nullptr, &Content);
			Content.HSplitTop(LineSize, &Button, &Content);
			Button.VSplitLeft(150.0f, &Label, &Button);
			Ui()->DoLabel(&Label, Localize("Name:"), FontSize, TEXTALIGN_ML);
			static CLineInput s_BindNameInput;
			s_BindNameInput.SetBuffer(s_aBindName, sizeof(s_aBindName));
			s_BindNameInput.SetEmptyText(Localize("Name (optional)"));
			Ui()->DoEditBox(&s_BindNameInput, &Button, EditBoxFontSize);

			Content.HSplitTop(MarginSmall, nullptr, &Content);
			Content.HSplitTop(LineSize, &Button, &Content);
			Button.VSplitLeft(150.0f, &Label, &Button);
			Ui()->DoLabel(&Label, Localize("Command:"), FontSize, TEXTALIGN_ML);
			static CLineInput s_BindInput;
			s_BindInput.SetBuffer(s_aBindCommand, sizeof(s_aBindCommand));
			s_BindInput.SetEmptyText(Localize("Command"));
			Ui()->DoEditBox(&s_BindInput, &Button, EditBoxFontSize);

			if(s_SelectedBindIndex < static_cast<int>(GameClient()->m_FastActions.m_vBinds.size()))
			{
				str_copy(GameClient()->m_FastActions.m_vBinds[s_SelectedBindIndex].m_aName, s_aBindName);
				str_copy(GameClient()->m_FastActions.m_vBinds[s_SelectedBindIndex].m_aCommand, s_aBindCommand);
			}

			static CButtonContainer s_ClearButton;
			Content.HSplitTop(MarginSmall, nullptr, &Content);
			Content.HSplitTop(LineSize, &Button, &Content);
			if(DoButton_Menu(&s_ClearButton, Localize("Clear command"), 0, &Button) &&
				s_SelectedBindIndex < static_cast<int>(GameClient()->m_FastActions.m_vBinds.size()))
			{
				GameClient()->m_FastActions.m_vBinds[s_SelectedBindIndex].m_aCommand[0] = '\0';
				s_aBindCommand[0] = '\0';
			}

			Content.HSplitTop(MarginSmall, nullptr, &Content);
			Content.HSplitTop(LineSize * 0.8f, &Label, &Content);
			Ui()->DoLabel(&Label, Localize("In game: hold bind key, press 1..6, release key to execute"), FontSize * 0.8f, TEXTALIGN_ML);

			Content.HSplitTop(MarginSmall, nullptr, &Content);
			Content.HSplitTop(LineSize, &Label, &Content);
			static CButtonContainer s_ReaderButtonWheel;
			static CButtonContainer s_ClearButtonWheel;
			DoLine_KeyReader(Label, s_ReaderButtonWheel, s_ClearButtonWheel, Localize("Fast Actions key"), "+fa");
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
			Ui()->DoLabel(&Label, Localize("Speedrun timer"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcSpeedrunTimer, Localize("Enable speedrun timer"), &g_Config.m_BcSpeedrunTimer, &Content, LineSize);
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
				Ui()->DoScrollbarOption(&g_Config.m_BcSpeedrunTimerHours, &g_Config.m_BcSpeedrunTimerHours, &Button, Localize("Hours"), 0, 99);
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);

				Expand.HSplitTop(LineSize, &Button, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcSpeedrunTimerMinutes, &g_Config.m_BcSpeedrunTimerMinutes, &Button, Localize("Minutes"), 0, 59);
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);

				Expand.HSplitTop(LineSize, &Button, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcSpeedrunTimerSeconds, &g_Config.m_BcSpeedrunTimerSeconds, &Button, Localize("Seconds"), 0, 59);
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);

				Expand.HSplitTop(LineSize, &Button, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcSpeedrunTimerMilliseconds, &g_Config.m_BcSpeedrunTimerMilliseconds, &Button, Localize("Milliseconds"), 0, 999, &CUi::ms_LinearScrollbarScale, 0, "ms");
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);

				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcSpeedrunTimerAutoDisable, Localize("Auto disable after time end"), &g_Config.m_BcSpeedrunTimerAutoDisable, &Expand, LineSize);
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
			Ui()->DoLabel(&Label, Localize("Auto team lock"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcAutoTeamLock, Localize("Lock team automatically after joining"), &g_Config.m_BcAutoTeamLock, &Content, LineSize);
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
				Ui()->DoScrollbarOption(&g_Config.m_BcAutoTeamLockDelay, &g_Config.m_BcAutoTeamLockDelay, &Button, Localize("Delay"), 0, 30, &CUi::ms_LinearScrollbarScale, 0, "s");
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
			Ui()->DoLabel(&Label, Localize("Focus Mode"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusMode, Localize("Enable Focus Mode"), &g_Config.m_ClFocusMode, &Content, LineSize);
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
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideNames, Localize("Hide Player Names"), &g_Config.m_ClFocusModeHideNames, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideEffects, Localize("Hide Visual Effects"), &g_Config.m_ClFocusModeHideEffects, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideHud, Localize("Hide HUD"), &g_Config.m_ClFocusModeHideHud, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideSongPlayer, Localize("Hide Song Player"), &g_Config.m_ClFocusModeHideSongPlayer, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideUI, Localize("Hide Unnecessary UI"), &g_Config.m_ClFocusModeHideUI, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideChat, Localize("Hide Chat"), &g_Config.m_ClFocusModeHideChat, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideScoreboard, Localize("Hide Scoreboard"), &g_Config.m_ClFocusModeHideScoreboard, &Expand, LineSize);
				DoLine_KeyReader(Expand, s_FocusModeBindReader, s_FocusModeBindClear, Localize("Focus mode bind"), "toggle p_focus_mode 0 1");
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
			Ui()->DoLabel(&Label, Localize("Misc"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			static CButtonContainer s_SettingsLayoutButton;
			int UseNewMenuLayout = g_Config.m_BcSettingsLayout == 0 ? 1 : 0;
			DoButton_CheckBoxAutoVMarginAndSet(&s_SettingsLayoutButton, Localize("Use new menu layout"), &UseNewMenuLayout, &Content, LineSize);
			g_Config.m_BcSettingsLayout = UseNewMenuLayout ? 0 : 1;
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcHideHudInSettings, Localize("Hide hud in settings"), &g_Config.m_BcHideHudInSettings, &Content, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcEscPlayerList, Localize("Show ESC players list"), &g_Config.m_BcEscPlayerList, &Content, LineSize);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcEmoticonShadow, Localize("Shadow of Emotions"), &g_Config.m_BcEmoticonShadow, &Content, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatSaveDraft, Localize("Save unsent messages"), &g_Config.m_BcChatSaveDraft, &Content, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcShowhudDummyCoordIndicator, TCLocalize("Show player below indicator"), &g_Config.m_BcShowhudDummyCoordIndicator, &Content, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcShowRealHitbox, Localize("Show real hitbox"), &g_Config.m_BcShowRealHitbox, &Content, LineSize);
			Content.HSplitTop(LineSize, &Row, &Content);
			Ui()->DoScrollbarOption(&g_Config.m_UiScale, &g_Config.m_UiScale, &Row, Localize("UI scale"), 50, 200, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, "%");
			if(g_Config.m_BcShowRealHitbox)
			{
				static CButtonContainer s_RealHitboxDotColorButton;
				DoLine_ColorPicker(&s_RealHitboxDotColorButton, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Content, Localize("Real hitbox dot color"), &g_Config.m_BcShowRealHitboxColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::BcShowRealHitboxColor, true)), false, nullptr, true);
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		{
			const float ContentHeight = LineSize + MarginSmall + 3.0f * LineSize;
			CUIRect Content, Label, Row;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, Localize("Browser Utils"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcAutoServerListRefresh, Localize("Auto server list refresh"), &g_Config.m_BcAutoServerListRefresh, &Content, LineSize);
			Content.HSplitTop(LineSize, &Row, &Content);
			Ui()->DoScrollbarOption(&g_Config.m_BcAutoServerListRefreshSeconds, &g_Config.m_BcAutoServerListRefreshSeconds, &Row, Localize("Seconds"), 1, 300, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, " s");
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcUseShortKogServerName, Localize("Use short KoG server name"), &g_Config.m_BcUseShortKogServerName, &Content, LineSize);

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
			Ui()->DoLabel(&Label, Localize("Chat Media"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			CChat &Chat = GameClient()->m_Chat;
			if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatMediaPreview, Localize("Render media previews from chat links"), &g_Config.m_BcChatMediaPreview, &Content, LineSize))
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

				if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatMediaPhotos, Localize("Show photos in chat media"), &g_Config.m_BcChatMediaPhotos, &Expand, LineSize))
					Chat.RebuildChat();

				if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatMediaGifs, Localize("Show GIFs in chat media"), &g_Config.m_BcChatMediaGifs, &Expand, LineSize))
					Chat.RebuildChat();

				if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatMediaContentFilter, Localize("Content filtering"), &g_Config.m_BcChatMediaContentFilter, &Expand, LineSize))
					Chat.RebuildChat();

				if(g_Config.m_BcChatMediaContentFilter)
				{
					Expand.HSplitTop(LineSize, &Row, &Expand);
					Ui()->DoLabel(&Row, Localize("Allowed media domains"), 12.0f, TEXTALIGN_ML);

					Expand.HSplitTop(LineSize, &Row, &Expand);
					static CLineInput s_ChatMediaAllowedDomains(g_Config.m_BcChatMediaAllowedDomains, sizeof(g_Config.m_BcChatMediaAllowedDomains));
					s_ChatMediaAllowedDomains.SetEmptyText("tenor.com; imgur.com; giphy.com");
					if(Ui()->DoClearableEditBox(&s_ChatMediaAllowedDomains, &Row, 14.0f))
						Chat.RebuildChat();
					GameClient()->m_Tooltips.DoToolTip(&s_ChatMediaAllowedDomains, &Row, Localize("Semicolon-separated allowlist, for example: tenor.com; imgur.com; giphy.com; cdn.discordapp.com"));
				}

				Expand.HSplitTop(LineSize, &Row, &Expand);
				if(Ui()->DoScrollbarOption(&g_Config.m_BcChatMediaPreviewMaxWidth, &g_Config.m_BcChatMediaPreviewMaxWidth, &Row, Localize("Media preview width"), 120, 400))
					Chat.RebuildChat();

				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatMediaViewer, Localize("Enable fullscreen media viewer in chat"), &g_Config.m_BcChatMediaViewer, &Expand, LineSize);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcChatMediaViewerMaxZoom, &g_Config.m_BcChatMediaViewerMaxZoom, &Row, Localize("Viewer max zoom"), 100, 2000, &CUi::ms_LinearScrollbarScale, 0u, "%");

				static CButtonContainer s_HideMediaBindReader;
				static CButtonContainer s_HideMediaBindClear;
				DoLine_KeyReader(Expand, s_HideMediaBindReader, s_HideMediaBindClear, Localize("Hide media bind"), "toggle_chat_media_hidden");
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
			DoLine_KeyReader(BindsView, s_VoicePanelBindReader, s_VoicePanelBindClear, Localize("Voice panel"), "toggle_voice_panel");
			DoLine_KeyReader(BindsView, s_PushToTalkBindReader, s_PushToTalkBindClear, Localize("Push-to-talk"), "+voicechat");
			DoLine_KeyReader(BindsView, s_MicMuteBindReader, s_MicMuteBindClear, Localize("Mute microphone"), "toggle_voice_mic_mute");
			DoLine_KeyReader(BindsView, s_HeadphonesMuteBindReader, s_HeadphonesMuteBindClear, Localize("Mute headphones"), "toggle_voice_headphones_mute");
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
			Ui()->DoLabel(&Label, Localize("Client Indicator"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcClientIndicatorInNamePlate, Localize("Show indicator in name plates"), &g_Config.m_BcClientIndicatorInNamePlate, &Content, LineSize);

			if(g_Config.m_BcClientIndicatorInNamePlate)
			{
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcClientIndicatorInNamePlateAboveSelf, Localize("Show above yourself"), &g_Config.m_BcClientIndicatorInNamePlateAboveSelf, &Content, LineSize);

				Content.HSplitTop(LineSize, &Row, &Content);
				Ui()->DoScrollbarOption(&g_Config.m_BcClientIndicatorInNamePlateSize, &g_Config.m_BcClientIndicatorInNamePlateSize, &Row, Localize("Name plate indicator size"), -50, 100);
			}

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcClientIndicatorInScoreboard, Localize("Show indicator in scoreboard"), &g_Config.m_BcClientIndicatorInScoreboard, &Content, LineSize);
			if(g_Config.m_BcClientIndicatorInScoreboard)
			{
				Content.HSplitTop(LineSize, &Row, &Content);
				Ui()->DoScrollbarOption(&g_Config.m_BcClientIndicatorInSoreboardSize, &g_Config.m_BcClientIndicatorInSoreboardSize, &Row, Localize("Scoreboard indicator size"), -50, 100);
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

	Ui()->DoLabel(&Title, Localize("Cancel all changes?"), FontSize * 1.1f, TEXTALIGN_ML);
	Ui()->DoLabel(&Message, Localize("All staged component changes will be lost."), FontSize, TEXTALIGN_ML);

	CUIRect YesButton, NoButton;
	Buttons.VSplitMid(&YesButton, &NoButton, MarginSmall);
	static CButtonContainer s_YesButton;
	static CButtonContainer s_NoButton;
	if(DoButton_Menu(&s_YesButton, Localize("Yes"), 0, &YesButton))
		ComponentsEditorCloseNow();
	if(DoButton_Menu(&s_NoButton, Localize("No"), 0, &NoButton) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
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

	Ui()->DoLabel(&Title, Localize("Restart"), FontSize * 1.1f, TEXTALIGN_ML);
	Ui()->DoLabel(&Message, Localize("Restart client now so component changes fully apply?"), FontSize, TEXTALIGN_ML);

	CUIRect YesButton, NoButton;
	Buttons.VSplitMid(&YesButton, &NoButton, MarginSmall);
	static CButtonContainer s_RestartYesButton;
	static CButtonContainer s_RestartNoButton;
	if(DoButton_Menu(&s_RestartYesButton, Localize("Yes"), 0, &YesButton))
	{
		m_ComponentsEditorState.m_ShowRestartConfirm = false;
		m_ComponentsEditorState.m_Open = false;
		if(Client()->State() == IClient::STATE_ONLINE || GameClient()->Editor()->HasUnsavedData())
			m_Popup = POPUP_RESTART;
		else
			Client()->Restart();
	}
	if(DoButton_Menu(&s_RestartNoButton, Localize("No"), 0, &NoButton) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
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

	Ui()->DoLabel(&HeaderText, Localize("Components editor"), HeadlineFontSize, TEXTALIGN_ML);

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
		Localize("Visuals"),
		Localize("Gameplay"),
		Localize("Others"),
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
			if(DoButton_CheckBox(&Entry, Localize(Entry.m_pName), Disabled, &Row))
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
	str_format(aBuf, sizeof(aBuf), Localize("Disabled components: %d"), DisabledCount);
	Ui()->DoLabel(&Counter, aBuf, FontSize, TEXTALIGN_ML);

	static CButtonContainer s_ApplyButton;
	const int DisabledStyle = m_ComponentsEditorState.m_HasUnsavedChanges ? 0 : -1;
	if(DoButton_Menu(&s_ApplyButton, Localize("Apply"), DisabledStyle, &ApplyButton) && m_ComponentsEditorState.m_HasUnsavedChanges)
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
	Ui()->DoLabel(&Label, TCLocalize("BestClient Links"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	static CButtonContainer s_DiscordButton, s_WebsiteButton, s_TelegramButton, s_CheckUpdateButton;
	CUIRect ButtonLeft, ButtonRight;

	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&ButtonLeft, &ButtonRight, MarginSmall);
	if(DoButtonLineSize_Menu(&s_DiscordButton, TCLocalize("Discord"), 0, &ButtonLeft, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://discord.gg/bestclient");
	if(DoButtonLineSize_Menu(&s_TelegramButton, TCLocalize("Telegram"), 0, &ButtonRight, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://t.me/bestddnet");

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&ButtonLeft, &ButtonRight, MarginSmall);

	if(DoButtonLineSize_Menu(&s_WebsiteButton, TCLocalize("Website"), 0, &ButtonLeft, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://bestclient.fun");
	if(DoButtonLineSize_Menu(&s_CheckUpdateButton, TCLocalize("Check update"), 0, &ButtonRight, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
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
			str_format(aUpdateLabel, sizeof(aUpdateLabel), "BestClient %s Is release", GameClient()->m_BestClient.m_aVersionStr);
		else if(ShowUpdateProgress)
		{
			if(UpdateState == IUpdater::GETTING_MANIFEST)
				str_copy(aUpdateLabel, TCLocalize("Preparing update..."), sizeof(aUpdateLabel));
			else
				str_format(aUpdateLabel, sizeof(aUpdateLabel), "%s %d%%", TCLocalize("Downloading"), Updater()->GetCurrentPercent());
		}
		else if(ShowRestartButton)
			str_copy(aUpdateLabel, TCLocalize("Update downloaded"), sizeof(aUpdateLabel));
		else
			str_copy(aUpdateLabel, TCLocalize("Update failed"), sizeof(aUpdateLabel));

		if(ShowDownloadButton)
			TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
		Ui()->DoLabel(&ButtonLeft, aUpdateLabel, HeadlineFontSize / 1.6f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		if(ShowDownloadButton || ShowRetryButton)
		{
			static CButtonContainer s_DownloadUpdateButton;
			if(DoButtonLineSize_Menu(&s_DownloadUpdateButton, TCLocalize("Download"), 0, &ButtonRight, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
				Updater()->InitiateUpdate();
		}
		else if(ShowRestartButton)
		{
			static CButtonContainer s_RestartUpdateButton;
			if(DoButtonLineSize_Menu(&s_RestartUpdateButton, TCLocalize("Restart"), 0, &ButtonRight, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
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
	Ui()->DoLabel(&Label, TCLocalize("Config Files"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	char aBuf[128 + IO_MAX_PATH_LENGTH];
	CUIRect BestClientConfig;
	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	BestClientConfig = Button;

	static CButtonContainer s_Config;
	if(DoButtonLineSize_Menu(&s_Config, TCLocalize("BestClient Settings"), 0, &BestClientConfig, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::BESTCLIENT].m_aConfigPath, aBuf, sizeof(aBuf));
		Client()->ViewFile(aBuf);
	}

	RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("BestClient Developers"), HeadlineFontSize, TEXTALIGN_ML);
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
	Ui()->DoLabel(&Label, TCLocalize("Hide Settings Tabs"), HeadlineFontSize, TEXTALIGN_ML);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	CUIRect LeftSettings, RightSettings;
	RightView.VSplitMid(&LeftSettings, &RightSettings, MarginSmall);

	const char *apTabNames[] = {
		Localize("Visuals"),
		"Gameplay",
		Localize("Others"),
		Localize("Fun"),
		"Shop",
		Localize("Editors"),
		"info",
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

CUi::EPopupMenuFunctionResult CMenus::PopupMapPicker(void *pContext, CUIRect View, bool Active)
{
	CPopupMapPickerContext *pPopupContext = static_cast<CPopupMapPickerContext *>(pContext);
	CMenus *pMenus = pPopupContext->m_pMenus;

	static CListBox s_ListBox;
	s_ListBox.SetActive(Active);
	s_ListBox.DoStart(20.0f, pPopupContext->m_vMaps.size(), 1, 3, -1, &View, false);

	int MapIndex = 0;
	for(auto &Map : pPopupContext->m_vMaps)
	{
		MapIndex++;
		const CListboxItem Item = s_ListBox.DoNextItem(&Map, MapIndex == pPopupContext->m_Selection);
		if(!Item.m_Visible)
			continue;

		CUIRect Label, Icon;
		Item.m_Rect.VSplitLeft(20.0f, &Icon, &Label);

		char aLabelText[IO_MAX_PATH_LENGTH];
		str_copy(aLabelText, Map.m_aFilename);
		if(Map.m_IsDirectory)
			str_append(aLabelText, "/", sizeof(aLabelText));

		const char *pIconType;
		if(!Map.m_IsDirectory)
		{
			pIconType = FontIcon::MAP;
		}
		else
		{
			if(!str_comp(Map.m_aFilename, ".."))
				pIconType = FontIcon::FOLDER_TREE;
			else
				pIconType = FontIcon::FOLDER;
		}

		pMenus->TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		pMenus->TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
		pMenus->Ui()->DoLabel(&Icon, pIconType, 12.0f, TEXTALIGN_ML);
		pMenus->TextRender()->SetRenderFlags(0);
		pMenus->TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

		pMenus->Ui()->DoLabel(&Label, aLabelText, 10.0f, TEXTALIGN_ML);
	}

	const int NewSelected = s_ListBox.DoEnd();
	pPopupContext->m_Selection = NewSelected >= 0 ? NewSelected : -1;
	if(s_ListBox.WasItemSelected() || s_ListBox.WasItemActivated())
	{
		const CMapListItem &SelectedItem = pPopupContext->m_vMaps[pPopupContext->m_Selection];

		if(SelectedItem.m_IsDirectory)
		{
			if(!str_comp(SelectedItem.m_aFilename, ".."))
			{
				fs_parent_dir(pPopupContext->m_aCurrentMapFolder);
			}
			else
			{
				str_append(pPopupContext->m_aCurrentMapFolder, "/", sizeof(pPopupContext->m_aCurrentMapFolder));
				str_append(pPopupContext->m_aCurrentMapFolder, SelectedItem.m_aFilename, sizeof(pPopupContext->m_aCurrentMapFolder));
			}
			pPopupContext->MapListPopulate();
		}
		else
		{
			str_format(g_Config.m_ClBackgroundEntities, sizeof(g_Config.m_ClBackgroundEntities), "%s/%s", pPopupContext->m_aCurrentMapFolder, SelectedItem.m_aFilename);
			pMenus->GameClient()->m_Background.LoadBackground();
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	return CUi::POPUP_KEEP_OPEN;
}

void CMenus::CPopupMapPickerContext::MapListPopulate()
{
	m_vMaps.clear();
	char aTemp[IO_MAX_PATH_LENGTH];
	str_format(aTemp, sizeof(aTemp), "maps/%s", m_aCurrentMapFolder);
	m_pMenus->Storage()->ListDirectoryInfo(IStorage::TYPE_ALL, aTemp, MapListFetchCallback, this);
	std::stable_sort(m_vMaps.begin(), m_vMaps.end(), CompareFilenameAscending);
	m_Selection = -1;
}

int CMenus::CPopupMapPickerContext::MapListFetchCallback(const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser)
{
	CPopupMapPickerContext *pRealUser = (CPopupMapPickerContext *)pUser;
	if((!IsDir && !str_endswith(pInfo->m_pName, ".map")) || !str_comp(pInfo->m_pName, ".") || (!str_comp(pInfo->m_pName, "..") && (!str_comp(pRealUser->m_aCurrentMapFolder, ""))))
		return 0;

	CMapListItem Item;
	str_copy(Item.m_aFilename, pInfo->m_pName);
	Item.m_IsDirectory = IsDir;

	pRealUser->m_vMaps.emplace_back(Item);

	return 0;
}
