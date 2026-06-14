/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus_start.h"

#include <engine/client/updater.h>
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

#include <game/client/gameclient.h>
#include <game/client/components/bestclient/version.h>
#include <game/client/ui.h>
#include <game/localization.h>
#include <game/version.h>

#if defined(CONF_PLATFORM_ANDROID)
#include <android/android_main.h>
#endif

void CMenusStart::RenderStartMenu(CUIRect MainView)
{
	GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_START);

	const float Rounding = 10.0f;
	const float VMargin = MainView.w / 2 - 190.0f;
	const float ExtMenuBottomOffset = 40.0f;

	CUIRect Button;
	int NewPage = -1;
	const auto SetIconMode = [&]() {
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	};
	const auto ResetIconMode = [&]() {
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	};

	CUIRect ExtMenu;
	MainView.VSplitLeft(30.0f, nullptr, &ExtMenu);
	ExtMenu.VSplitLeft(100.0f, &ExtMenu, nullptr);
	ExtMenu.HSplitBottom(ExtMenuBottomOffset, &ExtMenu, nullptr);

	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static CButtonContainer s_DiscordButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_DiscordButton, BCLocalize("Discord"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Client()->ViewLink("https://discord.gg/tmT4emAbrS");
	}

	ExtMenu.HSplitBottom(5.0f, &ExtMenu, nullptr);
	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static CButtonContainer s_TelegramButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_TelegramButton, BCLocalize("Telegram"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Client()->ViewLink("https://t.me/bestddnet");
	}

	ExtMenu.HSplitBottom(5.0f, &ExtMenu, nullptr);
	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static CButtonContainer s_CheckUpdateButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_CheckUpdateButton, BCLocalize("Check update"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		GameClient()->m_BestClient.FetchBestClientInfo();
	}

	// Calculate total height of the centered block (logo + buttons) and center it vertically
	const float LogoW = 360.0f;
	const float LogoH = 103.0f;
	const float LogoGap = 30.0f;
	const float ButtonH = 40.0f;
	const float ButtonGap = 5.0f;
	const int NumButtons = 5;
	const float ButtonsH = NumButtons * ButtonH + (NumButtons - 1) * ButtonGap;
	const float TotalBlockH = LogoH + LogoGap + ButtonsH;

	CUIRect Menu;
	MainView.VMargin(VMargin, &Menu);

	// Center the block vertically, offset upward to account for bottom UI elements
	const float BlockStartY = MainView.y + (MainView.h - TotalBlockH) / 2.0f - 50.0f;

	// Render logo
	{
		const float LogoX = MainView.w / 2 - LogoW / 2;
		const float LogoY = BlockStartY;
		const IGraphics::CTextureHandle &LogoTexture = GameClient()->m_Menus.MainMenuLogoTexture();
		Graphics()->TextureSet(LogoTexture.IsValid() && !LogoTexture.IsNullTexture() ? LogoTexture : g_pData->m_aImages[IMAGE_BANNER].m_Id);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1, 1, 1, 1);
		IGraphics::CQuadItem QuadItem(LogoX, LogoY, LogoW, LogoH);
		Graphics()->QuadsDrawTL(&QuadItem, 1);
		Graphics()->QuadsEnd();
	}

	const bool AutoUpdating = GameClient()->m_BestClient.IsAutoUpdating();

	// Position buttons starting below the logo
	float ButtonY = BlockStartY + LogoH + LogoGap;

	Button.x = Menu.x;
	Button.w = Menu.w;
	Button.h = ButtonH;

	// Play
	Button.y = ButtonY;
	static CButtonContainer s_PlayButton;
	if((GameClient()->m_Menus.DoButton_MenuEx(&s_PlayButton, Localize("Play", "Start menu"), 0, &Button, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "play_game" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), g_Config.m_ClShowStartMenuImages) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || CheckHotKey(KEY_P)) && !AutoUpdating)
	{
		NewPage = g_Config.m_UiPage >= CMenus::PAGE_INTERNET && g_Config.m_UiPage <= CMenus::PAGE_FAVORITE_COMMUNITY_5 ? g_Config.m_UiPage : CMenus::PAGE_INTERNET;
	}
	ButtonY += ButtonH + ButtonGap;

	// Demos
	Button.y = ButtonY;
	static CButtonContainer s_DemoButton;
	if((GameClient()->m_Menus.DoButton_MenuEx(&s_DemoButton, Localize("Demos"), 0, &Button, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "demos" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), g_Config.m_ClShowStartMenuImages) || CheckHotKey(KEY_D)) && !AutoUpdating)
	{
		NewPage = CMenus::PAGE_DEMOS;
	}
	ButtonY += ButtonH + ButtonGap;

	// Editor
	Button.y = ButtonY;
	static CButtonContainer s_MapEditorButton;
	const CUIRect EditorButton = Button;
	if((GameClient()->m_Menus.DoButton_MenuEx(&s_MapEditorButton, Localize("Editor"), 0, &Button, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "editor" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, GameClient()->Editor()->HasUnsavedData() ? ColorRGBA(0.0f, 1.0f, 0.0f, 0.25f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), g_Config.m_ClShowStartMenuImages) || CheckHotKey(KEY_E)) && !AutoUpdating)
	{
		g_Config.m_ClEditor = 1;
		Input()->MouseModeRelative();
	}

	// "DUO MAPPING" badge in the editor button's top-left corner
	{
		CUIRect Badge = EditorButton;
		Badge.VSplitLeft(90.0f, &Badge, nullptr);
		Badge.HSplitTop(16.0f, &Badge, nullptr);
		Badge.Margin(3.0f, &Badge);
		Graphics()->DrawRect4(Badge.x, Badge.y, Badge.w, Badge.h,
			ColorRGBA(0.62f, 0.28f, 0.95f, 1.0f), ColorRGBA(0.42f, 0.10f, 0.78f, 1.0f),
			ColorRGBA(0.62f, 0.28f, 0.95f, 1.0f), ColorRGBA(0.42f, 0.10f, 0.78f, 1.0f),
			IGraphics::CORNER_ALL, 4.0f);
		Ui()->DoLabel(&Badge, "DUO MAPPING", 8.0f, TEXTALIGN_MC);
	}
	ButtonY += ButtonH + ButtonGap;

	// Run server
	Button.y = ButtonY;
	static CButtonContainer s_LocalServerButton;
	const bool LocalServerRunning = GameClient()->m_LocalServer.IsServerRunning();
	if((GameClient()->m_Menus.DoButton_MenuEx(&s_LocalServerButton, LocalServerRunning ? Localize("Stop server") : Localize("Run server"), 0, &Button, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "local_server" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, LocalServerRunning ? ColorRGBA(0.0f, 1.0f, 0.0f, 0.25f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), g_Config.m_ClShowStartMenuImages) || (CheckHotKey(KEY_R) && Input()->KeyPress(KEY_R))) && !AutoUpdating)
	{
		if(LocalServerRunning)
		{
			GameClient()->m_LocalServer.KillServer();
		}
		else
		{
			GameClient()->m_LocalServer.RunServer({});
		}
	}
	ButtonY += ButtonH + ButtonGap;

	// Settings
	Button.y = ButtonY;
	const CUIRect SettingsButton = Button;
	static CButtonContainer s_SettingsButton;
	if(GameClient()->m_Menus.DoButton_MenuEx(&s_SettingsButton, Localize("Settings"), 0, &Button, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "settings" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), g_Config.m_ClShowStartMenuImages) || CheckHotKey(KEY_S))
		NewPage = CMenus::PAGE_SETTINGS;

	if(AutoUpdating)
	{
		// Cover only Play/Demos/Editor/Run server — Settings stays visible and accessible
		CUIRect OverlayRect;
		OverlayRect.x = Menu.x;
		OverlayRect.y = BlockStartY + LogoH + LogoGap;
		OverlayRect.w = Menu.w;
		OverlayRect.h = 4.0f * (ButtonH + ButtonGap);
		OverlayRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.7f), IGraphics::CORNER_ALL, Rounding);
		Ui()->DoLabel(&OverlayRect, BCLocalize("Updating..."), 18.0f, TEXTALIGN_MC);
	}

#if defined(CONF_AUTOUPDATE)
	char aUpdateBuf[128] = "";
	const bool NeedUpdate = GameClient()->m_BestClient.NeedUpdate();
	const IUpdater::EUpdaterState State = Updater()->GetCurrentState();
	const bool ShowDownloadButton = NeedUpdate && State == IUpdater::CLEAN && !AutoUpdating;
	const bool ShowRetryButton = NeedUpdate && State == IUpdater::FAIL && !AutoUpdating;
	const bool ShowRestartButton = State == IUpdater::NEED_RESTART && !AutoUpdating;
	const bool ShowUpdateProgress = State >= IUpdater::GETTING_MANIFEST && State < IUpdater::NEED_RESTART;

	if(ShowDownloadButton || ShowRetryButton || ShowRestartButton || ShowUpdateProgress)
	{
		CUIRect UpdateRow = SettingsButton;
		UpdateRow.y += SettingsButton.h + 5.0f;
		UpdateRow.h = 22.0f;

		CUIRect UpdateLabel, UpdateButton;
		UpdateRow.VSplitRight(120.0f, &UpdateLabel, &UpdateButton);
		UpdateLabel.VSplitRight(10.0f, &UpdateLabel, nullptr);

		if(ShowDownloadButton)
		{
			str_format(aUpdateBuf, sizeof(aUpdateBuf), BCLocalize("BestClient %s is out!"), GameClient()->m_BestClient.m_aVersionStr);
			TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
		}
		else if(ShowUpdateProgress)
		{
			if(State == IUpdater::GETTING_MANIFEST)
				str_copy(aUpdateBuf, BCLocalize("Preparing update..."));
			else
				str_format(aUpdateBuf, sizeof(aUpdateBuf), BCLocalize("Downloading %d%%"), Updater()->GetCurrentPercent());
		}
		else if(State == IUpdater::FAIL)
		{
			str_copy(aUpdateBuf, BCLocalize("Update failed"));
			TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
		}
		else if(State == IUpdater::NEED_RESTART)
		{
			str_copy(aUpdateBuf, BCLocalize("Update downloaded"));
			TextRender()->TextColor(0.7f, 1.0f, 0.7f, 1.0f);
		}

		Ui()->DoLabel(&UpdateLabel, aUpdateBuf, 14.0f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		if(ShowDownloadButton || ShowRetryButton)
		{
			static CButtonContainer s_MenuUpdateDownload;
			if(GameClient()->m_Menus.DoButton_Menu(&s_MenuUpdateDownload, BCLocalize("Download"), 0, &UpdateButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
			{
				Updater()->InitiateUpdate();
			}
		}
		else if(ShowRestartButton)
		{
			static CButtonContainer s_MenuUpdateRestart;
			if(GameClient()->m_Menus.DoButton_Menu(&s_MenuUpdateRestart, BCLocalize("Restart"), 0, &UpdateButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
			{
				Updater()->ApplyUpdateAndRestart();
			}
		}
		else
		{
			Ui()->RenderProgressBar(UpdateButton, Updater()->GetCurrentPercent() / 100.0f);
		}
	}
#endif

	// Quit button at the bottom
	CUIRect QuitArea;
	MainView.VMargin(VMargin, &QuitArea);
	QuitArea.HSplitBottom(25.0f, &QuitArea, nullptr);
	QuitArea.HSplitBottom(40.0f, &QuitArea, &Button);
	CUIRect QuitButton = Button;
	QuitButton.w = QuitButton.h;
	QuitButton.x += (Button.w - QuitButton.w) / 2.0f;
	static CButtonContainer s_QuitButton;
	bool UsedEscape = false;
	SetIconMode();
	if(GameClient()->m_Menus.DoButton_Menu(&s_QuitButton, FontIcon::POWER_OFF, 0, &QuitButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, Rounding, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || (UsedEscape = Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE)) || CheckHotKey(KEY_Q))
	{
		ResetIconMode();
		if(UsedEscape || GameClient()->Editor()->HasUnsavedData() || (GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmQuitTime && g_Config.m_ClConfirmQuitTime >= 0))
		{
			GameClient()->m_Menus.ShowQuitPopup();
		}
		else
		{
			GameClient()->m_Menus.QuitWithMenuSfx();
		}
	}
	ResetIconMode();

	CUIRect CurVersion, ConsoleButton;
	MainView.HSplitBottom(74.0f, nullptr, &CurVersion);
	CurVersion.VSplitRight(40.0f, &CurVersion, nullptr);
	CurVersion.HSplitTop(20.0f, &ConsoleButton, &CurVersion);
	CurVersion.HSplitTop(5.0f, nullptr, &CurVersion);
	ConsoleButton.VSplitRight(40.0f, nullptr, &ConsoleButton);

	CUIRect VersionLine1, VersionLine2, VersionLine3;
	CurVersion.HSplitTop(16.0f, &VersionLine1, &CurVersion);
	CurVersion.HSplitTop(2.0f, nullptr, &CurVersion);
	CurVersion.HSplitTop(16.0f, &VersionLine2, &CurVersion);
	CurVersion.HSplitTop(2.0f, nullptr, &CurVersion);
	CurVersion.HSplitTop(16.0f, &VersionLine3, &CurVersion);

	char aDDNetBuf[64];
	char aTClientBuf[64];
	char aBestClientBuf[64];
	str_format(aDDNetBuf, sizeof(aDDNetBuf), "DDNet %s", GAME_RELEASE_VERSION);
	str_format(aTClientBuf, sizeof(aTClientBuf), "TClient %s", TCLIENT_VERSION);
	str_format(aBestClientBuf, sizeof(aBestClientBuf), "BestClient %s", BESTCLIENT_VERSION);
	Ui()->DoLabel(&VersionLine1, aDDNetBuf, 14.0f, TEXTALIGN_MR);
	Ui()->DoLabel(&VersionLine2, aTClientBuf, 14.0f, TEXTALIGN_MR);
	Ui()->DoLabel(&VersionLine3, aBestClientBuf, 14.0f, TEXTALIGN_MR);

	static CButtonContainer s_ConsoleButton;
	SetIconMode();
	if(GameClient()->m_Menus.DoButton_Menu(&s_ConsoleButton, FontIcon::TERMINAL, 0, &ConsoleButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.1f)))
	{
		GameClient()->m_GameConsole.Toggle(CGameConsole::CONSOLETYPE_LOCAL);
	}
	ResetIconMode();

	if(NewPage != -1)
	{
		GameClient()->m_Menus.SetShowStart(false);
		GameClient()->m_Menus.SetMenuPage(NewPage);
	}
}

bool CMenusStart::CheckHotKey(int Key) const
{
	return !Input()->ShiftIsPressed() && !Input()->ModifierIsPressed() && !Input()->AltIsPressed() &&
	       Input()->KeyPress(Key) &&
	       !GameClient()->m_GameConsole.IsActive();
}
