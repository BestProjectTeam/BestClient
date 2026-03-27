#include "menus_start_rclient.h"

#include <algorithm>

#include <engine/client/updater.h>
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>
#include <game/version.h>

#if defined(CONF_PLATFORM_ANDROID)
#include <android/android_main.h>
#endif

void CMenusStartRClient::RenderStartMenu(CUIRect MainView)
{
	GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_START);

	if(g_Config.m_RiUiSkipOpenMenu)
	{
		m_LogoMenuExpanded = true;
		m_LogoMenuAnim = 1.0f;
	}

	const CUIRect View = MainView;
	const auto Lerp = [](float From, float To, float Amount) {
		return From + (To - From) * Amount;
	};

	m_LogoMenuAnim = std::clamp(m_LogoMenuAnim + Client()->RenderFrameTime() * (m_LogoMenuExpanded ? 6.0f : -6.0f), 0.0f, 1.0f);
	const float AnimProgress = m_LogoMenuAnim * m_LogoMenuAnim * (3.0f - 2.0f * m_LogoMenuAnim);

	// render logo
	const float LogoCircleSizeCollapsed = 170.0f;
	const float LogoCircleSizeExpanded = 118.0f;
	const float LogoSizeCollapsed = LogoCircleSizeCollapsed;
	const float LogoSizeExpanded = LogoCircleSizeExpanded;
	const float ViewCenterX = View.x + View.w / 2.0f;
	const float LogoCenterYCollapsed = View.y + View.h / 2.0f - 10.0f;
	const float LogoCenterYExpanded = LogoCenterYCollapsed;
	const float LogoCenterXExpanded = ViewCenterX + std::min(View.w * 0.18f, 170.0f);
	const float LogoCircleSize = Lerp(LogoCircleSizeCollapsed, LogoCircleSizeExpanded, AnimProgress);
	const float LogoSize = Lerp(LogoSizeCollapsed, LogoSizeExpanded, AnimProgress);
	const float LogoCenterX = Lerp(ViewCenterX, LogoCenterXExpanded, AnimProgress);
	const float LogoCenterY = Lerp(LogoCenterYCollapsed, LogoCenterYExpanded, AnimProgress);
	static CButtonContainer s_LogoButton;
	const auto Ease = [](float Progress) {
		return Progress * Progress * (3.0f - 2.0f * Progress);
	};
	const float PrevHoverScale = Lerp(1.0f, 1.08f, Ease(m_LogoHoverAnim));
	CUIRect LogoCircleHoverRect = {
		LogoCenterX - LogoCircleSize * PrevHoverScale / 2.0f,
		LogoCenterY - LogoCircleSize * PrevHoverScale / 2.0f,
		LogoCircleSize * PrevHoverScale,
		LogoCircleSize * PrevHoverScale};
	const bool LogoHovered = Ui()->MouseHovered(&LogoCircleHoverRect);
	const bool LogoActive = Ui()->CheckActiveItem(&s_LogoButton);
	m_LogoHoverAnim = std::clamp(m_LogoHoverAnim + Client()->RenderFrameTime() * ((LogoHovered || LogoActive) ? 10.0f : -10.0f), 0.0f, 1.0f);
	const float HoverProgress = Ease(m_LogoHoverAnim);
	const float LogoHoverScale = Lerp(1.0f, 1.08f, HoverProgress);
	const float LogoCircleSizeHovered = LogoCircleSize * LogoHoverScale;
	const float LogoSizeHovered = LogoSize * LogoHoverScale;
	CUIRect LogoCircle = {LogoCenterX - LogoCircleSizeHovered / 2.0f, LogoCenterY - LogoCircleSizeHovered / 2.0f, LogoCircleSizeHovered, LogoCircleSizeHovered};
	const auto RenderLogo = [&]() {
		CUIRect LogoRect;
		LogoCircle.Margin((LogoCircle.w - LogoSizeHovered) / 2.0f, &LogoRect);

		Graphics()->TextureSet(g_pData->m_aImages[IMAGE_RCLIENT_BIG_LOGO].m_Id);
		Graphics()->QuadsBegin();
		const float LogoShade = LogoActive ? 0.92f : (LogoHovered ? 1.0f : 0.97f);
		Graphics()->SetColor(LogoShade, LogoShade, LogoShade, 1.0f);
		IGraphics::CQuadItem QuadItem(LogoRect.x, LogoRect.y, LogoRect.w, LogoRect.h);
		Graphics()->QuadsDrawTL(&QuadItem, 1);
		Graphics()->QuadsEnd();
	};

	// render ribbon
	const CUIRect *pScreen = Ui()->Screen();
	const float RibbonHeight = 68.0f;
	const float RibbonY = LogoCircle.y + LogoCircle.h / 2.0f - RibbonHeight / 2.0f;
	const float RibbonReveal = AnimProgress * AnimProgress * (3.0f - 2.0f * AnimProgress);
	if(RibbonReveal > 0.0f)
	{
		const float LeftReveal = (LogoCenterX - pScreen->x) * RibbonReveal;
		const float RightReveal = (pScreen->x + pScreen->w - LogoCenterX) * RibbonReveal;
		CUIRect Ribbon = {
			LogoCenterX - LeftReveal,
			RibbonY,
			LeftReveal + RightReveal,
			RibbonHeight};
		Ribbon.Draw(ColorRGBA(0.12f, 0.12f, 0.18f, 1.0f), IGraphics::CORNER_NONE, 0.0f);
	}

	if(Ui()->DoButtonLogic(&s_LogoButton, 0, &LogoCircle, BUTTONFLAG_LEFT))
	{
		m_LogoMenuExpanded = !m_LogoMenuExpanded;
	}

	const float VMargin = std::max(30.0f, View.w / 2.0f - 140.0f);

	int NewPage = -1;

	CUIRect TopStatus, BottomStatus, VersionUpdate;
	BottomStatus = *pScreen;
	BottomStatus.y += BottomStatus.h - 60.0f;
	BottomStatus.h = 60.0f;
	TopStatus = *pScreen;
	TopStatus.h = 30.0f;
	BottomStatus.HSplitBottom(30.0f, &VersionUpdate, &BottomStatus);
	VersionUpdate.HSplitBottom(10.0f, &VersionUpdate, nullptr);

	const float ButtonWidthTarget = std::clamp(View.w * 0.115f, 108.0f, 150.0f);
	const float ButtonHeight = RibbonHeight;
	const float ButtonSpacing = 8.0f;
	const float ButtonFloatOffset = 10.0f;
	const float SideGap = 18.0f;
	const float ButtonsCollapsedX = LogoCenterX - ButtonWidthTarget / 2.0f;
	const float ButtonsCollapsedY = RibbonY;
	const float LeftBlockWidth = 4.0f * ButtonWidthTarget + 3.0f * ButtonSpacing;
	const float RightBlockWidth = ButtonWidthTarget;
	const float LeftStartX = std::max(View.x + 16.0f, LogoCircle.x - SideGap - LeftBlockWidth);
	const float RightStartX = std::min(View.x + View.w - RightBlockWidth - 16.0f, LogoCircle.x + LogoCircle.w + SideGap);
	const auto ButtonRevealProgress = [&](int Index, float Delay) {
		return std::clamp((AnimProgress - Delay - Index * 0.08f) / 0.5f, 0.0f, 1.0f);
	};
	const auto ButtonEase = [](float Progress) {
		return Progress * Progress * (3.0f - 2.0f * Progress);
	};
	const auto PrepareAnimatedButton = [&](bool LeftSide, int Index, CUIRect *pButtonRect) {
		const float Progress = ButtonEase(ButtonRevealProgress(Index, LeftSide ? 0.05f : 0.1f));
		if(Progress <= 0.0f)
			return 0.0f;

		const float Width = Lerp(ButtonWidthTarget * 0.55f, ButtonWidthTarget, Progress);
		const float XTarget = LeftSide ? LeftStartX + Index * (ButtonWidthTarget + ButtonSpacing) : RightStartX + Index * (ButtonWidthTarget + ButtonSpacing);
		const float X = Lerp(ButtonsCollapsedX, XTarget, Progress);
		const float Y = ButtonsCollapsedY;
		*pButtonRect = {X, Y, Width, ButtonHeight};
		return Progress;
	};
	enum ERibbonButtonSlot
	{
		RIBBON_BUTTON_EDITOR = 0,
		RIBBON_BUTTON_LOCAL_SERVER,
		RIBBON_BUTTON_DEMOS,
		RIBBON_BUTTON_PLAY,
		RIBBON_BUTTON_SETTINGS,
		NUM_RIBBON_BUTTONS
	};
	struct CRibbonButtonState
	{
		CButtonContainer *m_pButton = nullptr;
		CUIRect m_HitRect{};
		CUIRect m_VisualRect{};
		float m_RevealProgress = 0.0f;
		float m_HoverProgress = 0.0f;
		ColorRGBA m_BaseColor{};
		const char *m_pIcon = nullptr;
		const char *m_pLabel = nullptr;
		bool m_FillLeftGap = false;
		bool m_FillRightGap = false;
	};
	const auto GetVisualButtonRect = [&](const CUIRect &HitRect, float ButtonHoverProgress, bool FillLeftGap, bool FillRightGap) {
		CUIRect VisualRect = HitRect;
		const float GapFillLeft = FillLeftGap ? ButtonSpacing * ButtonHoverProgress : 0.0f;
		const float GapFillRight = FillRightGap ? ButtonSpacing * ButtonHoverProgress : 0.0f;
		VisualRect.x -= GapFillLeft;
		VisualRect.w += GapFillLeft + GapFillRight;
		VisualRect.y -= ButtonFloatOffset * ButtonHoverProgress;
		return VisualRect;
	};
	const auto UpdateButtonHover = [&](int Slot, CButtonContainer *pButton, const CUIRect &HitRect) {
		const bool WantsHover = Ui()->MouseHovered(&HitRect) || Ui()->CheckActiveItem(pButton);
		m_aRibbonButtonHoverAnim[Slot] = std::clamp(m_aRibbonButtonHoverAnim[Slot] + Client()->RenderFrameTime() * (WantsHover ? 10.0f : -10.0f), 0.0f, 1.0f);
		return ButtonEase(m_aRibbonButtonHoverAnim[Slot]);
	};
	const auto RenderRibbonButton = [&](const CRibbonButtonState &ButtonState) -> bool {
		if(ButtonState.m_RevealProgress <= 0.0f)
			return false;

		const bool Hovered = Ui()->MouseHovered(&ButtonState.m_HitRect);
		const bool Active = Ui()->CheckActiveItem(ButtonState.m_pButton);
		const float Highlight = Active ? 0.82f : Lerp(1.0f, Hovered ? 1.1f : 1.04f, ButtonState.m_HoverProgress);
		const float ShadowStrength = ButtonState.m_HoverProgress;
		ColorRGBA Fill = ButtonState.m_BaseColor;
		Fill.r = std::clamp(Fill.r * Highlight, 0.0f, 1.0f);
		Fill.g = std::clamp(Fill.g * Highlight, 0.0f, 1.0f);
		Fill.b = std::clamp(Fill.b * Highlight, 0.0f, 1.0f);
		Fill.a = 1.0f;

		CUIRect Panel = ButtonState.m_VisualRect;
		CUIRect Shadow = Panel;
		Shadow.x += 2.0f;
		Shadow.y += Lerp(2.0f, 7.0f, ShadowStrength);
		Shadow.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, Lerp(0.05f, 0.18f, ShadowStrength)), IGraphics::CORNER_NONE, 0.0f);
		Panel.Draw(Fill, IGraphics::CORNER_NONE, 0.0f);

		CUIRect LabelRect = Panel;
		LabelRect.Margin(8.0f, &LabelRect);
		CUIRect IconRect, TextRect;
		LabelRect.HSplitTop(LabelRect.h * 0.58f, &IconRect, &TextRect);

		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		Ui()->DoLabel(&IconRect, ButtonState.m_pIcon, IconRect.h * 0.55f, TEXTALIGN_MC);
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		Ui()->DoLabel(&TextRect, ButtonState.m_pLabel, TextRect.h * 0.72f, TEXTALIGN_MC);

		return Ui()->DoButtonLogic(ButtonState.m_pButton, 0, &ButtonState.m_HitRect, BUTTONFLAG_LEFT) != 0;
	};
	const auto CreateRibbonButton = [&](CRibbonButtonState &ButtonState, int Slot, CButtonContainer *pButton, bool LeftSide, int Index, ColorRGBA BaseColor, const char *pIcon, const char *pLabel, bool FillLeftGap, bool FillRightGap) {
		ButtonState.m_pButton = pButton;
		ButtonState.m_RevealProgress = PrepareAnimatedButton(LeftSide, Index, &ButtonState.m_HitRect);
		ButtonState.m_BaseColor = BaseColor;
		ButtonState.m_pIcon = pIcon;
		ButtonState.m_pLabel = pLabel;
		ButtonState.m_FillLeftGap = FillLeftGap;
		ButtonState.m_FillRightGap = FillRightGap;
		if(ButtonState.m_RevealProgress <= 0.0f)
		{
			m_aRibbonButtonHoverAnim[Slot] = 0.0f;
			ButtonState.m_HoverProgress = 0.0f;
			ButtonState.m_VisualRect = ButtonState.m_HitRect;
			return;
		}

		ButtonState.m_HoverProgress = UpdateButtonHover(Slot, pButton, ButtonState.m_HitRect);
		ButtonState.m_VisualRect = GetVisualButtonRect(ButtonState.m_HitRect, ButtonState.m_HoverProgress, FillLeftGap, FillRightGap);
	};

	static CButtonContainer s_QuitButton;
	static CButtonContainer s_LocalServerButton;
	static CButtonContainer s_DemoButton;
	static CButtonContainer s_PlayButton;
	static CButtonContainer s_SettingsButton;
	std::array<CRibbonButtonState, NUM_RIBBON_BUTTONS> aRibbonButtons;
	CreateRibbonButton(aRibbonButtons[RIBBON_BUTTON_EDITOR], RIBBON_BUTTON_EDITOR, &s_QuitButton, true, 0, ColorRGBA(0.88f, 0.18f, 0.56f, 1.0f), FontIcon::PEN_TO_SQUARE, Localize("Editor"), false, true);
	CreateRibbonButton(aRibbonButtons[RIBBON_BUTTON_LOCAL_SERVER], RIBBON_BUTTON_LOCAL_SERVER, &s_LocalServerButton, true, 1, GameClient()->m_LocalServer.IsServerRunning() ? ColorRGBA(0.15f, 0.73f, 0.34f, 1.0f) : ColorRGBA(0.92f, 0.66f, 0.08f, 1.0f), FontIcon::NETWORK_WIRED, GameClient()->m_LocalServer.IsServerRunning() ? Localize("Stop server") : Localize("Run server"), true, true);
	CreateRibbonButton(aRibbonButtons[RIBBON_BUTTON_DEMOS], RIBBON_BUTTON_DEMOS, &s_DemoButton, true, 2, ColorRGBA(0.63f, 0.81f, 0.02f, 1.0f), FontIcon::CLAPPERBOARD, Localize("Demos"), true, true);
	CreateRibbonButton(aRibbonButtons[RIBBON_BUTTON_PLAY], RIBBON_BUTTON_PLAY, &s_PlayButton, true, 3, ColorRGBA(0.43f, 0.29f, 0.88f, 1.0f), FontIcon::CIRCLE_PLAY, Localize("Play", "Start menu"), true, false);
	CreateRibbonButton(aRibbonButtons[RIBBON_BUTTON_SETTINGS], RIBBON_BUTTON_SETTINGS, &s_SettingsButton, false, 0, ColorRGBA(0.35f, 0.35f, 0.38f, 1.0f), FontIcon::GEAR, Localize("Settings"), false, false);
	std::array<int, NUM_RIBBON_BUTTONS> aRibbonButtonRenderOrder = {RIBBON_BUTTON_EDITOR, RIBBON_BUTTON_LOCAL_SERVER, RIBBON_BUTTON_DEMOS, RIBBON_BUTTON_PLAY, RIBBON_BUTTON_SETTINGS};
	std::sort(aRibbonButtonRenderOrder.begin(), aRibbonButtonRenderOrder.end(), [&](int Left, int Right) {
		return aRibbonButtons[Left].m_HoverProgress < aRibbonButtons[Right].m_HoverProgress;
	});
	std::array<bool, NUM_RIBBON_BUTTONS> aRibbonButtonClicked{};
	for(const int Slot : aRibbonButtonRenderOrder)
	{
		aRibbonButtonClicked[Slot] = RenderRibbonButton(aRibbonButtons[Slot]);
	}
	RenderLogo();

	if((aRibbonButtonClicked[RIBBON_BUTTON_EDITOR]) || CheckHotKey(KEY_E))
	{
		g_Config.m_ClEditor = 1;
		Input()->MouseModeRelative();
	}

	if((aRibbonButtonClicked[RIBBON_BUTTON_SETTINGS]) || CheckHotKey(KEY_S))
		NewPage = CMenus::PAGE_SETTINGS;

	const bool LocalServerRunning = GameClient()->m_LocalServer.IsServerRunning();
	if(aRibbonButtons[RIBBON_BUTTON_LOCAL_SERVER].m_RevealProgress > 0.0f)
	{
		if(aRibbonButtonClicked[RIBBON_BUTTON_LOCAL_SERVER] || (CheckHotKey(KEY_R) && Input()->KeyPress(KEY_R)))
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
	}

	if((aRibbonButtonClicked[RIBBON_BUTTON_DEMOS]) || CheckHotKey(KEY_D))
	{
		NewPage = CMenus::PAGE_DEMOS;
	}

	if((aRibbonButtonClicked[RIBBON_BUTTON_PLAY]) ||
		Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || CheckHotKey(KEY_P))
	{
		NewPage = g_Config.m_UiPage >= CMenus::PAGE_INTERNET && g_Config.m_UiPage <= CMenus::PAGE_FAVORITE_COMMUNITY_5 ? g_Config.m_UiPage : CMenus::PAGE_INTERNET;
	}

	if(g_Config.m_RiUiShowBottomBar)
	{
		CUIRect TopBar, AboutClient, ClientVersions, TClientVersion, RClientVersion, CurVersion;
		TopBar = BottomStatus;
		TopBar.Draw(ColorRGBA(0.12f, 0.12f, 0.18f, 1.0f), IGraphics::CORNER_NONE, 0);
		TopBar.VSplitRight(390.0f, &AboutClient, &ClientVersions);
		ClientVersions.VSplitRight(130.0f, &TClientVersion, &RClientVersion);
		TClientVersion.VSplitMid(&CurVersion, &TClientVersion);

		char aBuf[128] = "Based on Tater client. Thanks Pulse and Entity clients for some functions";
		Ui()->DoLabel(&AboutClient, aBuf, 14.0f, TEXTALIGN_MC);

		char aTBuf[64];
		char aRBuf[64];
		str_format(aTBuf, sizeof(aTBuf), "DDNet %s", GAME_RELEASE_VERSION);
		Ui()->DoLabel(&CurVersion, aTBuf, 14.0f, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		str_format(aTBuf, sizeof(aTBuf), "TClient %s", TCLIENT_VERSION);
		if(GameClient()->m_TClient.NeedUpdate())
			TextRender()->TextColor(1.0f, 0.2f, 0.2f, 1.0f);
		Ui()->DoLabel(&TClientVersion, aTBuf, 14.0f, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		str_format(aRBuf, sizeof(aRBuf), "RClient %s", RCLIENT_VERSION);
		if(GameClient()->m_RClient.NeedUpdate())
			TextRender()->TextColor(1.0f, 0.2f, 0.2f, 1.0f);
		Ui()->DoLabel(&RClientVersion, aRBuf, 14.0f, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}

	CUIRect Button, ExtBar;
	ExtBar = TopStatus;
	if(g_Config.m_RiUiShowTopBar)
		ExtBar.Draw(ColorRGBA(0.12f, 0.12f, 0.18f, 1.0f), IGraphics::CORNER_NONE, 0);
	ExtBar.HSplitBottom(5.0f, &ExtBar, nullptr);
	ExtBar.VSplitLeft(15.0f, nullptr, &ExtBar);
	ExtBar.HSplitTop(5.0f, nullptr, &ExtBar);

	// render console
	if(g_Config.m_RiUiShowTopBar)
	{
		ExtBar.VSplitLeft(80.0f, &Button, &ExtBar);
		Ui()->DoLabel(&Button, "Discords:", 14.0f, TEXTALIGN_MC);

		ExtBar.VSplitLeft(15.0f, nullptr, &ExtBar);
		ExtBar.VSplitLeft(60.0f, &Button, &ExtBar);
		static CButtonContainer s_DiscordDDButton;
		if(GameClient()->m_Menus.DoButton_Menu(&s_DiscordDDButton, "DDNet", 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f)))
		{
			Client()->ViewLink(Localize("https://ddnet.org/discord"));
		}

		ExtBar.VSplitLeft(15.0f, nullptr, &ExtBar);
		ExtBar.VSplitLeft(60.0f, &Button, &ExtBar);
		static CButtonContainer s_DiscordTCButton;
		if(GameClient()->m_Menus.DoButton_Menu(&s_DiscordTCButton, "TClient", 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f)))
		{
			Client()->ViewLink(Localize("https://discord.gg/BgPSapKRkZ"));
		}

		ExtBar.VSplitLeft(15.0f, nullptr, &ExtBar);
		ExtBar.VSplitLeft(60.0f, &Button, &ExtBar);
		static CButtonContainer s_DiscordRCButton;
		if(GameClient()->m_Menus.DoButton_Menu(&s_DiscordRCButton, "RClient", 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f)))
		{
			Client()->ViewLink(Localize("https://discord.gg/wUFTVAGVGa"));
		}

		ExtBar.VSplitLeft(15.0f, nullptr, &ExtBar);
		ExtBar.VSplitLeft(15.0f, &Button, &ExtBar);
		Ui()->DoLabel(&Button, "|", 14.0f, TEXTALIGN_MC);

		ExtBar.VSplitLeft(15.0f, nullptr, &ExtBar);
		ExtBar.VSplitLeft(60.0f, &Button, &ExtBar);
		static CButtonContainer s_LearnButton;
		if(GameClient()->m_Menus.DoButton_Menu(&s_LearnButton, Localize("Learn"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f)))
		{
			Client()->ViewLink(Localize("https://wiki.ddnet.org/"));
		}

		ExtBar.VSplitLeft(15.0f, nullptr, &ExtBar);
		ExtBar.VSplitLeft(60.0f, &Button, &ExtBar);
		static CButtonContainer s_TutorialButton;
		if(GameClient()->m_Menus.DoButton_Menu(&s_TutorialButton, Localize("Tutorial"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f)))
		{
			GameClient()->m_Menus.JoinTutorial();
		}

		ExtBar.VSplitLeft(15.0f, nullptr, &ExtBar);
		ExtBar.VSplitLeft(60.0f, &Button, &ExtBar);
		static CButtonContainer s_WebsiteButton;
		if(GameClient()->m_Menus.DoButton_Menu(&s_WebsiteButton, Localize("Website"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f)))
		{
			Client()->ViewLink("https://rushie-client.ru/");
		}

		ExtBar.VSplitLeft(15.0f, nullptr, &ExtBar);
		ExtBar.VSplitLeft(60.0f, &Button, &ExtBar);
		static CButtonContainer s_NewsButton;
		if(GameClient()->m_Menus.DoButton_Menu(&s_NewsButton, Localize("News"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, g_Config.m_UiUnreadNews ? ColorRGBA(0.0f, 1.0f, 0.0f, 0.25f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f)) || CheckHotKey(KEY_N))
			NewPage = CMenus::PAGE_NEWS;


		//Right
		bool UsedEscape = false;
		ExtBar.VSplitRight(15.0f, &ExtBar, nullptr);
		ExtBar.VSplitRight(40.0f, &ExtBar, &Button);
		static CButtonContainer s_ExitButton;
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		if(GameClient()->m_Menus.DoButton_Menu(&s_ExitButton, FontIcon::POWER_OFF, 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.5f, 0.0f, 0.0f, 0.5f)))
		{
			if(UsedEscape || GameClient()->Editor()->HasUnsavedData() || (GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmQuitTime && g_Config.m_ClConfirmQuitTime >= 0))
			{
				GameClient()->m_Menus.ShowQuitPopup();
			}
			else
			{
				Client()->Quit();
			}
		}
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

		ExtBar.VSplitRight(15.0f, &ExtBar, nullptr);
		ExtBar.VSplitRight(40.0f, &ExtBar, &Button);
		static CButtonContainer s_ConsoleButton;
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		if(GameClient()->m_Menus.DoButton_Menu(&s_ConsoleButton, FontIcon::TERMINAL, 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.5f, 0.0f, 0.0f, 0.5f)))
		{
			GameClient()->m_GameConsole.Toggle(CGameConsole::CONSOLETYPE_LOCAL);
		}
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	}
	else
	{
		bool UsedEscape = false;
		ExtBar.VSplitRight(15.0f, &ExtBar, nullptr);
		ExtBar.VSplitRight(40.0f, &ExtBar, &Button);
		static CButtonContainer s_ExitButton;
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		if(GameClient()->m_Menus.DoButton_Menu(&s_ExitButton, FontIcon::POWER_OFF, 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.5f, 0.0f, 0.0f, 0.5f)))
		{
			if(UsedEscape || GameClient()->Editor()->HasUnsavedData() || (GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmQuitTime && g_Config.m_ClConfirmQuitTime >= 0))
			{
				GameClient()->m_Menus.ShowQuitPopup();
			}
			else
			{
				Client()->Quit();
			}
		}
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	}

	if(GameClient()->m_RClient.NeedUpdate())
	{
		CUIRect UpdateButton;
		VersionUpdate.VMargin(VMargin, &VersionUpdate);
		VersionUpdate.VSplitRight(100.0f, &VersionUpdate, &UpdateButton);
		VersionUpdate.VSplitRight(10.0f, &VersionUpdate, nullptr);

		static CButtonContainer s_VersionUpdate;
		if(GameClient()->m_Menus.DoButton_Menu(&s_VersionUpdate, Localize("Download"), 0, &UpdateButton, BUTTONFLAG_LEFT, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(1.0f, 0.2f, 0.2f, 0.25f)))
		{
			Client()->ViewLink(CRClient::RCLIENT_URL);
		}

		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), Localize("Rushie client %s is out!"), GameClient()->m_RClient.m_aVersionStr);
		TextRender()->TextColor(1.0f, 0.2f, 0.2f, 1.0f);
		Ui()->DoLabel(&VersionUpdate, aBuf, 14.0f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}

	if(NewPage != -1)
	{
		GameClient()->m_Menus.SetShowStart(false);
		GameClient()->m_Menus.SetMenuPage(NewPage);
	}
}

bool CMenusStartRClient::CheckHotKey(int Key) const
{
	return !Input()->ShiftIsPressed() && !Input()->ModifierIsPressed() && !Input()->AltIsPressed() &&
	       Input()->KeyPress(Key) &&
	       !GameClient()->m_GameConsole.IsActive();
}
