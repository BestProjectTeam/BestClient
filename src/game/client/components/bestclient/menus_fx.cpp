/* Copyright © 2026 BestProject Team */
#include <algorithm>

#include <engine/font_icons.h>
#include <engine/shared/config.h>

#include <game/client/bc_menu_badges.h>
#include <game/client/bc_ui_animations.h>
#include <game/client/components/bestclient/fx/fgf_weather.h>
#include <game/client/components/bestclient/fx/hookgrip.h>
#include <game/client/components/bestclient/settings_search.h>
#include <game/client/components/bestclient/ui_theme/style.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

void CMenus::RenderSettingsBestClientFx(CUIRect &Column, bool HookColumn)
{
	const float LineSize = 20.0f;
	const float MarginSmall = 5.0f;
	const float HeadlineFontSize = 20.0f;
	const float MarginBetweenViews = 30.0f;
	const float BlockPadding = MarginBetweenViews * 0.6666f;
	const float Row = MarginSmall + LineSize;

	const auto RowCheck = [&](CUIRect &View, int *pValue, const char *pText) {
		View.HSplitTop(MarginSmall, nullptr, &View);
		CUIRect Line;
		View.HSplitTop(LineSize, &Line, &View);
		DoButton_CheckBoxAutoVMarginAndSet(pValue, pText, pValue, &Line, LineSize);
	};
	const auto RowSlider = [&](CUIRect &View, int *pValue, const char *pText, int Min, int Max, const char *pSuffix) {
		View.HSplitTop(MarginSmall, nullptr, &View);
		CUIRect Line;
		View.HSplitTop(LineSize, &Line, &View);
		Ui()->DoScrollbarOption(pValue, pValue, &Line, pText, Min, Max, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, pSuffix);
	};
	const auto RowColor = [&](CUIRect &View, CButtonContainer *pResetId, const char *pText, unsigned *pColor, unsigned DefaultColor) {
		View.HSplitTop(MarginSmall, nullptr, &View);
		DoLine_ColorPicker(pResetId, LineSize, 13.0f, 0.0f, &View, pText, pColor, color_cast<ColorRGBA>(ColorHSLA(DefaultColor)), false);
	};
	const auto RowDropDown = [&](CUIRect &View, const char *pLabel, int &Value, const char **pItems, int Num, CUi::SDropDownState &State, CScrollRegion &Scroll) {
		View.HSplitTop(MarginSmall, nullptr, &View);
		CUIRect Line, Label, Select;
		View.HSplitTop(LineSize, &Line, &View);
		Line.VSplitLeft(110.0f, &Label, &Select);
		Ui()->DoLabel(&Label, pLabel, 14.0f, TEXTALIGN_ML);
		State.m_SelectionPopupContext.m_pScrollRegion = &Scroll;
		Value = Ui()->DoDropDown(&Select, Value, pItems, Num, State);
	};
	const auto DrawFxTitle = [&](CUIRect Title, const char *pText, CButtonContainer *pAuthor) {
		CUIRect AuthorBadge;
		Title.VSplitRight(6.0f, &Title, nullptr);
		BcMenuBadges::DrawAuthor(Graphics(), Ui(), TextRender(), &Title, 4.0f, &AuthorBadge);
		Ui()->DoButtonLogic(pAuthor, 0, &AuthorBadge, BUTTONFLAG_NONE);
		GameClient()->m_Tooltips.DoToolTip(pAuthor, &AuthorBadge, "recidivismnational");
		GameClient()->m_Tooltips.SetFadeTime(pAuthor, 0.0f);
		BcMenuBadges::DrawNew(Graphics(), Ui(), TextRender(), &Title, 4.0f);
		Ui()->DoLabel(&Title, pText, HeadlineFontSize, TEXTALIGN_ML);
	};

	if(HookColumn)
	{
		int StyleSettingRows = 0;
		switch(g_Config.m_BcHookStyle)
		{
		case FGF_HOOK_STYLE_WHIRLWIND: StyleSettingRows = 9; break;
		case FGF_HOOK_STYLE_BLACK_HOLE:
		case FGF_HOOK_STYLE_MAGIC:
		case FGF_HOOK_STYLE_FIRE:
		case FGF_HOOK_STYLE_RAINBOW: StyleSettingRows = 4; break;
		case FGF_HOOK_STYLE_LIGHTNING:
		case FGF_HOOK_STYLE_TENTACLE: StyleSettingRows = 5; break;
		default: break;
		}
		const bool StyleOn = g_Config.m_BcHookStyle != FGF_HOOK_STYLE_OFF;
		int DetailRows = 0;
		if(StyleOn)
		{
			DetailRows = 3;
			if(StyleSettingRows > 0)
				DetailRows += StyleSettingRows;
		}
		const float ExpandedTarget = (1.0f + DetailRows) * Row;
		const float BlockHeight = LineSize + ExpandedTarget;
		CUIRect Block;
		Column.HSplitTop(BlockHeight, &Block, &Column);
		CUIRect BlockBg = Block;
		BlockBg.w += BlockPadding;
		BlockBg.h += BlockPadding;
		BlockBg.x -= BlockPadding * 0.5f;
		BlockBg.y -= BlockPadding * 0.5f;
		BestClientUiTheme::DrawUiBlock(&BlockBg, IGraphics::CORNER_ALL, 10.0f);
		BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcHookStyle, &BlockBg);

		CUIRect Label;
		Block.HSplitTop(LineSize, &Label, &Block);
		CUIRect Title, ResetButton;
		Label.VSplitRight(LineSize + 8.0f, &Title, &ResetButton);
		static CButtonContainer s_Reset;
		if(Ui()->DoButton_FontIcon(&s_Reset, FontIcon::ARROW_ROTATE_LEFT, 0, &ResetButton, BUTTONFLAG_LEFT))
		{
			g_Config.m_BcHookStyle = DefaultConfig::BcHookStyle;
			g_Config.m_BcHookRope = DefaultConfig::BcHookRope;
			g_Config.m_BcHookChargeTime = DefaultConfig::BcHookChargeTime;
			g_Config.m_BcHookSize = DefaultConfig::BcHookSize;
			g_Config.m_BcHookWindColor = DefaultConfig::BcHookWindColor;
			g_Config.m_BcHookWindBurst = DefaultConfig::BcHookWindBurst;
			g_Config.m_BcHookWindEye = DefaultConfig::BcHookWindEye;
			g_Config.m_BcHookWindDensity = DefaultConfig::BcHookWindDensity;
			g_Config.m_BcHookWindSpin = DefaultConfig::BcHookWindSpin;
			g_Config.m_BcHookWindThickness = DefaultConfig::BcHookWindThickness;
			g_Config.m_BcHookWindOpacity = DefaultConfig::BcHookWindOpacity;
			g_Config.m_BcHookWindLeafAmount = DefaultConfig::BcHookWindLeafAmount;
			g_Config.m_BcHookWindDust = DefaultConfig::BcHookWindDust;
			g_Config.m_BcHookHoleColor = DefaultConfig::BcHookHoleColor;
			g_Config.m_BcHookHoleDisk = DefaultConfig::BcHookHoleDisk;
			g_Config.m_BcHookHoleStreaks = DefaultConfig::BcHookHoleStreaks;
			g_Config.m_BcHookHoleSpin = DefaultConfig::BcHookHoleSpin;
			g_Config.m_BcHookMagicColor = DefaultConfig::BcHookMagicColor;
			g_Config.m_BcHookMagicRunes = DefaultConfig::BcHookMagicRunes;
			g_Config.m_BcHookMagicSpin = DefaultConfig::BcHookMagicSpin;
			g_Config.m_BcHookMagicSparkles = DefaultConfig::BcHookMagicSparkles;
			g_Config.m_BcHookRainbowSpeed = DefaultConfig::BcHookRainbowSpeed;
			g_Config.m_BcHookRainbowGlitter = DefaultConfig::BcHookRainbowGlitter;
			g_Config.m_BcHookRainbowRays = DefaultConfig::BcHookRainbowRays;
			g_Config.m_BcHookRainbowWidth = DefaultConfig::BcHookRainbowWidth;
			g_Config.m_BcHookFireColor = DefaultConfig::BcHookFireColor;
			g_Config.m_BcHookFireSmoke = DefaultConfig::BcHookFireSmoke;
			g_Config.m_BcHookFireIntensity = DefaultConfig::BcHookFireIntensity;
			g_Config.m_BcHookFireEmbers = DefaultConfig::BcHookFireEmbers;
			g_Config.m_BcHookBoltColor = DefaultConfig::BcHookBoltColor;
			g_Config.m_BcHookBoltJitter = DefaultConfig::BcHookBoltJitter;
			g_Config.m_BcHookBoltBranches = DefaultConfig::BcHookBoltBranches;
			g_Config.m_BcHookBoltSparks = DefaultConfig::BcHookBoltSparks;
			g_Config.m_BcHookBoltArc = DefaultConfig::BcHookBoltArc;
			g_Config.m_BcHookTentacleColor = DefaultConfig::BcHookTentacleColor;
			g_Config.m_BcHookTentacleWaves = DefaultConfig::BcHookTentacleWaves;
			g_Config.m_BcHookTentacleThickness = DefaultConfig::BcHookTentacleThickness;
			g_Config.m_BcHookTentacleSuckers = DefaultConfig::BcHookTentacleSuckers;
			g_Config.m_BcHookTentacleSlime = DefaultConfig::BcHookTentacleSlime;
		}
		GameClient()->m_Tooltips.DoToolTip(&s_Reset, &ResetButton, BcLocalize("Reset to defaults"));
		static CButtonContainer s_HooksAuthor;
		DrawFxTitle(Title, BcLocalize("Custom hooks"), &s_HooksAuthor);

		CUIRect Visible = Block;
		Visible.h = ExpandedTarget;
		Ui()->ClipEnable(&Visible);
		static CUi::SDropDownState s_StyleState;
		static CScrollRegion s_StyleScroll;
		const char *apStyles[] = {
			BcLocalize("None"),
			BcLocalize("Whirlwind"),
			BcLocalize("Black hole"),
			BcLocalize("Magic"),
			BcLocalize("Fire"),
			BcLocalize("Troll"),
			BcLocalize("Rainbow"),
			BcLocalize("Lightning"),
			BcLocalize("Tentacle"),
		};
		RowDropDown(Block, BcLocalize("Hook"), g_Config.m_BcHookStyle, apStyles, (int)std::size(apStyles), s_StyleState, s_StyleScroll);

		if(StyleOn)
		{
			RowCheck(Block, &g_Config.m_BcHookRope, BcLocalize("Style the hook"));
			RowSlider(Block, &g_Config.m_BcHookChargeTime, BcLocalize("Grow time"), 200, 4000, "ms");
			RowSlider(Block, &g_Config.m_BcHookSize, BcLocalize("Size"), 50, 200, "%");

			if(g_Config.m_BcHookStyle == FGF_HOOK_STYLE_WHIRLWIND)
			{
				RowCheck(Block, &g_Config.m_BcHookWindBurst, BcLocalize("Grab gust"));
				RowCheck(Block, &g_Config.m_BcHookWindEye, BcLocalize("Eye"));
				RowSlider(Block, &g_Config.m_BcHookWindDensity, BcLocalize("Wind lines"), 25, 300, "%");
				RowSlider(Block, &g_Config.m_BcHookWindSpin, BcLocalize("Spin speed"), 25, 300, "%");
				RowSlider(Block, &g_Config.m_BcHookWindThickness, BcLocalize("Line thickness"), 25, 300, "%");
				RowSlider(Block, &g_Config.m_BcHookWindOpacity, BcLocalize("Wind opacity"), 10, 100, "%");
				RowSlider(Block, &g_Config.m_BcHookWindLeafAmount, BcLocalize("Leaves"), 0, 300, "%");
				RowSlider(Block, &g_Config.m_BcHookWindDust, BcLocalize("Dust"), 0, 300, "%");
				static CButtonContainer s_Color;
				RowColor(Block, &s_Color, BcLocalize("Wind color"), &g_Config.m_BcHookWindColor, DefaultConfig::BcHookWindColor);
			}
			else if(g_Config.m_BcHookStyle == FGF_HOOK_STYLE_BLACK_HOLE)
			{
				RowCheck(Block, &g_Config.m_BcHookHoleDisk, BcLocalize("Disk"));
				RowSlider(Block, &g_Config.m_BcHookHoleStreaks, BcLocalize("Matter"), 0, 300, "%");
				RowSlider(Block, &g_Config.m_BcHookHoleSpin, BcLocalize("Spin"), 25, 300, "%");
				static CButtonContainer s_Color;
				RowColor(Block, &s_Color, BcLocalize("Disk color"), &g_Config.m_BcHookHoleColor, DefaultConfig::BcHookHoleColor);
			}
			else if(g_Config.m_BcHookStyle == FGF_HOOK_STYLE_MAGIC)
			{
				RowCheck(Block, &g_Config.m_BcHookMagicRunes, BcLocalize("Runes"));
				RowSlider(Block, &g_Config.m_BcHookMagicSpin, BcLocalize("Spin"), 25, 300, "%");
				RowSlider(Block, &g_Config.m_BcHookMagicSparkles, BcLocalize("Sparkles"), 0, 300, "%");
				static CButtonContainer s_Color;
				RowColor(Block, &s_Color, BcLocalize("Spell color"), &g_Config.m_BcHookMagicColor, DefaultConfig::BcHookMagicColor);
			}
			else if(g_Config.m_BcHookStyle == FGF_HOOK_STYLE_FIRE)
			{
				RowCheck(Block, &g_Config.m_BcHookFireSmoke, BcLocalize("Smoke"));
				RowSlider(Block, &g_Config.m_BcHookFireIntensity, BcLocalize("Flames"), 25, 300, "%");
				RowSlider(Block, &g_Config.m_BcHookFireEmbers, BcLocalize("Embers"), 0, 300, "%");
				static CButtonContainer s_Color;
				RowColor(Block, &s_Color, BcLocalize("Fire color"), &g_Config.m_BcHookFireColor, DefaultConfig::BcHookFireColor);
			}
			else if(g_Config.m_BcHookStyle == FGF_HOOK_STYLE_RAINBOW)
			{
				RowCheck(Block, &g_Config.m_BcHookRainbowRays, BcLocalize("Rays"));
				RowSlider(Block, &g_Config.m_BcHookRainbowSpeed, BcLocalize("Flow"), 25, 300, "%");
				RowSlider(Block, &g_Config.m_BcHookRainbowGlitter, BcLocalize("Glitter"), 0, 300, "%");
				RowSlider(Block, &g_Config.m_BcHookRainbowWidth, BcLocalize("Ribbon width"), 50, 250, "%");
			}
			else if(g_Config.m_BcHookStyle == FGF_HOOK_STYLE_LIGHTNING)
			{
				RowCheck(Block, &g_Config.m_BcHookBoltArc, BcLocalize("Wall arcs"));
				RowSlider(Block, &g_Config.m_BcHookBoltJitter, BcLocalize("Jitter"), 25, 300, "%");
				RowSlider(Block, &g_Config.m_BcHookBoltBranches, BcLocalize("Forks"), 0, 6, "");
				RowSlider(Block, &g_Config.m_BcHookBoltSparks, BcLocalize("Sparks"), 0, 300, "%");
				static CButtonContainer s_Color;
				RowColor(Block, &s_Color, BcLocalize("Lightning color"), &g_Config.m_BcHookBoltColor, DefaultConfig::BcHookBoltColor);
			}
			else if(g_Config.m_BcHookStyle == FGF_HOOK_STYLE_TENTACLE)
			{
				RowCheck(Block, &g_Config.m_BcHookTentacleSuckers, BcLocalize("Suckers"));
				RowSlider(Block, &g_Config.m_BcHookTentacleWaves, BcLocalize("Writhing"), 25, 300, "%");
				RowSlider(Block, &g_Config.m_BcHookTentacleThickness, BcLocalize("Thickness"), 50, 250, "%");
				RowSlider(Block, &g_Config.m_BcHookTentacleSlime, BcLocalize("Slime"), 0, 300, "%");
				static CButtonContainer s_Color;
				RowColor(Block, &s_Color, BcLocalize("Flesh color"), &g_Config.m_BcHookTentacleColor, DefaultConfig::BcHookTentacleColor);
			}
		}
		Ui()->ClipDisable();

		Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

		const float HoldTarget = 3.0f * Row;
		const float HoldHeight = BC_MODULE_REVEAL(g_Config.m_BcGatheredEnergy != 0, HoldTarget, Client()->RenderFrameTime());
		const float EffectRows = 5.0f + (g_Config.m_BcAirJump != 0 ? 1.0f : 0.0f) + (g_Config.m_BcHammerSlash != 0 ? 1.0f : 0.0f);
		const float EffectsBlockHeight = LineSize + EffectRows * Row + HoldHeight;
		CUIRect EffectsBlock;
		Column.HSplitTop(EffectsBlockHeight, &EffectsBlock, &Column);
		CUIRect EffectsBg = EffectsBlock;
		EffectsBg.w += BlockPadding;
		EffectsBg.h += BlockPadding;
		EffectsBg.x -= BlockPadding * 0.5f;
		EffectsBg.y -= BlockPadding * 0.5f;
		BestClientUiTheme::DrawUiBlock(&EffectsBg, IGraphics::CORNER_ALL, 10.0f);
		BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcHookTether, &EffectsBg);

		CUIRect EffectsLabel;
		EffectsBlock.HSplitTop(LineSize, &EffectsLabel, &EffectsBlock);
		CUIRect EffectsTitle, EffectsResetButton;
		EffectsLabel.VSplitRight(LineSize + 8.0f, &EffectsTitle, &EffectsResetButton);
		static CButtonContainer s_EffectsReset;
		if(Ui()->DoButton_FontIcon(&s_EffectsReset, FontIcon::ARROW_ROTATE_LEFT, 0, &EffectsResetButton, BUTTONFLAG_LEFT))
		{
			g_Config.m_BcHookTether = DefaultConfig::BcHookTether;
			g_Config.m_BcHookLassoBox = DefaultConfig::BcHookLassoBox;
			g_Config.m_BcAirJump = DefaultConfig::BcAirJump;
			g_Config.m_BcAirJumpHideParticles = DefaultConfig::BcAirJumpHideParticles;
			g_Config.m_BcHammerSlash = DefaultConfig::BcHammerSlash;
			g_Config.m_BcHammerHideParticles = DefaultConfig::BcHammerHideParticles;
			g_Config.m_BcGatheredEnergyAura = DefaultConfig::BcGatheredEnergyAura;
			g_Config.m_BcGatheredEnergyBurst = DefaultConfig::BcGatheredEnergyBurst;
			g_Config.m_BcGatheredEnergyAmount = DefaultConfig::BcGatheredEnergyAmount;
		}
		GameClient()->m_Tooltips.DoToolTip(&s_EffectsReset, &EffectsResetButton, BcLocalize("Reset to defaults"));
		static CButtonContainer s_EffectsAuthor;
		DrawFxTitle(EffectsTitle, BcLocalize("Hook effects"), &s_EffectsAuthor);
		RowCheck(EffectsBlock, &g_Config.m_BcAirJump, BcLocalize("Air jump ring"));
		if(g_Config.m_BcAirJump)
			RowCheck(EffectsBlock, &g_Config.m_BcAirJumpHideParticles, BcLocalize("Disable jump particles"));
		RowCheck(EffectsBlock, &g_Config.m_BcHammerSlash, BcLocalize("Hammer slash"));
		if(g_Config.m_BcHammerSlash)
			RowCheck(EffectsBlock, &g_Config.m_BcHammerHideParticles, BcLocalize("Disable hammer particles"));
		RowCheck(EffectsBlock, &g_Config.m_BcHookTether, BcLocalize("Hook lasso"));
		RowCheck(EffectsBlock, &g_Config.m_BcHookLassoBox, BcLocalize("Hook box"));
		RowCheck(EffectsBlock, &g_Config.m_BcGatheredEnergy, BcLocalize("Hook hold"));
		if(HoldHeight > 0.5f)
		{
			CUIRect Visible = EffectsBlock;
			Visible.h = HoldHeight;
			Ui()->ClipEnable(&Visible);
			RowCheck(EffectsBlock, &g_Config.m_BcGatheredEnergyAura, BcLocalize("Hold aura"));
			RowCheck(EffectsBlock, &g_Config.m_BcGatheredEnergyBurst, BcLocalize("Hold burst"));
			RowSlider(EffectsBlock, &g_Config.m_BcGatheredEnergyAmount, BcLocalize("Energy motes"), 25, 300, "%");
			Ui()->ClipDisable();
		}

		Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

		{
			const bool Expanded = g_Config.m_BcAtmosphere != 0;
			const float ExpandedTarget = (g_Config.m_BcWeather == FGF_WEATHER_SNOW ? 4.0f : 3.0f) * Row;
			const float ExpandedHeight = BC_MODULE_REVEAL(Expanded, ExpandedTarget, Client()->RenderFrameTime());
			const float BlockHeight = LineSize + MarginSmall + LineSize + ExpandedHeight;
			CUIRect Block;
			Column.HSplitTop(BlockHeight, &Block, &Column);
			CUIRect BlockBg = Block;
			BlockBg.w += BlockPadding;
			BlockBg.h += BlockPadding;
			BlockBg.x -= BlockPadding * 0.5f;
			BlockBg.y -= BlockPadding * 0.5f;
			BestClientUiTheme::DrawUiBlock(&BlockBg, IGraphics::CORNER_ALL, 10.0f);
			BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcAtmosphere, &BlockBg);

			CUIRect Label;
			Block.HSplitTop(LineSize, &Label, &Block);
			CUIRect Title, ResetButton;
			Label.VSplitRight(LineSize + 8.0f, &Title, &ResetButton);
			static CButtonContainer s_Reset;
			if(Ui()->DoButton_FontIcon(&s_Reset, FontIcon::ARROW_ROTATE_LEFT, 0, &ResetButton, BUTTONFLAG_LEFT))
			{
				g_Config.m_BcWeather = DefaultConfig::BcWeather;
				g_Config.m_BcWeatherAmount = DefaultConfig::BcWeatherAmount;
				g_Config.m_BcWeatherSnowColor = DefaultConfig::BcWeatherSnowColor;
				g_Config.m_BcWeatherSnowflakes = DefaultConfig::BcWeatherSnowflakes;
				g_Config.m_BcWeatherRainColor = DefaultConfig::BcWeatherRainColor;
				g_Config.m_BcWeatherFireflyColor = DefaultConfig::BcWeatherFireflyColor;
				g_Config.m_BcAmbientDust = DefaultConfig::BcAmbientDust;
				g_Config.m_BcAmbientDustAmount = DefaultConfig::BcAmbientDustAmount;
				g_Config.m_BcAmbientDustColor = DefaultConfig::BcAmbientDustColor;
			}
			GameClient()->m_Tooltips.DoToolTip(&s_Reset, &ResetButton, BcLocalize("Reset to defaults"));
			static CButtonContainer s_WeatherAuthor;
			DrawFxTitle(Title, BcLocalize("Weather"), &s_WeatherAuthor);
			Block.HSplitTop(MarginSmall, nullptr, &Block);
			CUIRect Enable;
			Block.HSplitTop(LineSize, &Enable, &Block);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcAtmosphere, BcLocalize("Enable"), &g_Config.m_BcAtmosphere, &Enable, LineSize);
			if(ExpandedHeight > 0.5f)
			{
				CUIRect Visible = Block;
				Visible.h = ExpandedHeight;
				Ui()->ClipEnable(&Visible);
				static CUi::SDropDownState s_WeatherState;
				static CScrollRegion s_WeatherScroll;
				const char *apWeather[] = {
					BcLocalize("Snow"),
					BcLocalize("Rain"),
					BcLocalize("Fireflies"),
					BcLocalize("Dust"),
				};
				g_Config.m_BcWeather = std::clamp(g_Config.m_BcWeather, 0, (int)FGF_WEATHER_DUST);
				RowDropDown(Block, BcLocalize("Weather"), g_Config.m_BcWeather, apWeather, (int)std::size(apWeather), s_WeatherState, s_WeatherScroll);
				if(g_Config.m_BcWeather == FGF_WEATHER_DUST)
				{
					RowSlider(Block, &g_Config.m_BcAmbientDustAmount, BcLocalize("Amount"), 25, 200, "%");
					static CButtonContainer s_DustColor;
					RowColor(Block, &s_DustColor, BcLocalize("Dust color"), &g_Config.m_BcAmbientDustColor, DefaultConfig::BcAmbientDustColor);
				}
				else
				{
					RowSlider(Block, &g_Config.m_BcWeatherAmount, BcLocalize("Amount"), 25, 300, "%");
					if(g_Config.m_BcWeather == FGF_WEATHER_RAIN)
					{
						static CButtonContainer s_RainColor;
						RowColor(Block, &s_RainColor, BcLocalize("Rain color"), &g_Config.m_BcWeatherRainColor, DefaultConfig::BcWeatherRainColor);
					}
					else if(g_Config.m_BcWeather == FGF_WEATHER_FIREFLIES)
					{
						static CButtonContainer s_FireflyColor;
						RowColor(Block, &s_FireflyColor, BcLocalize("Firefly color"), &g_Config.m_BcWeatherFireflyColor, DefaultConfig::BcWeatherFireflyColor);
					}
					else
					{
						static CUi::SDropDownState s_SnowState;
						static CScrollRegion s_SnowScroll;
						const char *apSnow[] = {
							BcLocalize("Dots"),
							BcLocalize("Snowflakes"),
						};
						g_Config.m_BcWeatherSnowflakes = std::clamp(g_Config.m_BcWeatherSnowflakes, 0, 1);
						RowDropDown(Block, BcLocalize("Snow"), g_Config.m_BcWeatherSnowflakes, apSnow, (int)std::size(apSnow), s_SnowState, s_SnowScroll);
						static CButtonContainer s_SnowColor;
						RowColor(Block, &s_SnowColor, BcLocalize("Snow color"), &g_Config.m_BcWeatherSnowColor, DefaultConfig::BcWeatherSnowColor);
					}
				}
				Ui()->ClipDisable();
			}
		}

		Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	{
		const bool Expanded = g_Config.m_BcFreezePull != 0;
		const bool ShowColdColor = g_Config.m_BcFreezePullHookColor == 0;
		const float ExpandedTarget = (5.0f + (ShowColdColor ? 1.0f : 0.0f)) * Row;
		const float ExpandedHeight = BC_MODULE_REVEAL(Expanded, ExpandedTarget, Client()->RenderFrameTime());
		const float BlockHeight = LineSize + MarginSmall + LineSize + ExpandedHeight;
		CUIRect Block;
		Column.HSplitTop(BlockHeight, &Block, &Column);
		CUIRect BlockBg = Block;
		BlockBg.w += BlockPadding;
		BlockBg.h += BlockPadding;
		BlockBg.x -= BlockPadding * 0.5f;
		BlockBg.y -= BlockPadding * 0.5f;
		BestClientUiTheme::DrawUiBlock(&BlockBg, IGraphics::CORNER_ALL, 10.0f);
		BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcFreezePull, &BlockBg);

		CUIRect Label;
		Block.HSplitTop(LineSize, &Label, &Block);
		CUIRect Title, ResetButton;
		Label.VSplitRight(LineSize + 8.0f, &Title, &ResetButton);
		static CButtonContainer s_Reset;
		if(Ui()->DoButton_FontIcon(&s_Reset, FontIcon::ARROW_ROTATE_LEFT, 0, &ResetButton, BUTTONFLAG_LEFT))
		{
			g_Config.m_BcFreezePullShield = DefaultConfig::BcFreezePullShield;
			g_Config.m_BcFreezePullRange = DefaultConfig::BcFreezePullRange;
			g_Config.m_BcFreezePullStrands = DefaultConfig::BcFreezePullStrands;
			g_Config.m_BcFreezePullSparks = DefaultConfig::BcFreezePullSparks;
			g_Config.m_BcFreezePullColor = DefaultConfig::BcFreezePullColor;
			g_Config.m_BcFreezePullHookColor = DefaultConfig::BcFreezePullHookColor;
		}
		GameClient()->m_Tooltips.DoToolTip(&s_Reset, &ResetButton, BcLocalize("Reset to defaults"));
		static CButtonContainer s_PullAuthor;
		DrawFxTitle(Title, BcLocalize("Freeze lifesteal"), &s_PullAuthor);
		Block.HSplitTop(MarginSmall, nullptr, &Block);
		CUIRect Enable;
		Block.HSplitTop(LineSize, &Enable, &Block);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcFreezePull, BcLocalize("Enable"), &g_Config.m_BcFreezePull, &Enable, LineSize);
		if(ExpandedHeight > 0.5f)
		{
			CUIRect Visible = Block;
			Visible.h = ExpandedHeight;
			Ui()->ClipEnable(&Visible);
			RowCheck(Block, &g_Config.m_BcFreezePullShield, BcLocalize("Shield"));
			RowSlider(Block, &g_Config.m_BcFreezePullRange, BcLocalize("Reach"), 60, 260, "");
			RowSlider(Block, &g_Config.m_BcFreezePullStrands, BcLocalize("Amount"), 1, 8, "");
			RowSlider(Block, &g_Config.m_BcFreezePullSparks, BcLocalize("Frost"), 0, 300, "%");
			RowCheck(Block, &g_Config.m_BcFreezePullHookColor, BcLocalize("Custom hook color"));
			if(ShowColdColor)
			{
				static CButtonContainer s_Color;
				RowColor(Block, &s_Color, BcLocalize("Cold color"), &g_Config.m_BcFreezePullColor, DefaultConfig::BcFreezePullColor);
			}
			Ui()->ClipDisable();
		}
	}

	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	{
		const bool Expanded = g_Config.m_BcFreezeLightning != 0;
		const float ExpandedTarget = 7.0f * Row;
		const float ExpandedHeight = BC_MODULE_REVEAL(Expanded, ExpandedTarget, Client()->RenderFrameTime());
		const float BlockHeight = LineSize + MarginSmall + LineSize + ExpandedHeight;
		CUIRect Block;
		Column.HSplitTop(BlockHeight, &Block, &Column);
		CUIRect BlockBg = Block;
		BlockBg.w += BlockPadding;
		BlockBg.h += BlockPadding;
		BlockBg.x -= BlockPadding * 0.5f;
		BlockBg.y -= BlockPadding * 0.5f;
		BestClientUiTheme::DrawUiBlock(&BlockBg, IGraphics::CORNER_ALL, 10.0f);
		BestClientSettingsSearch::DrawBlockHighlight(&g_Config.m_BcFreezeLightning, &BlockBg);

		CUIRect Label;
		Block.HSplitTop(LineSize, &Label, &Block);
		CUIRect Title, ResetButton;
		Label.VSplitRight(LineSize + 8.0f, &Title, &ResetButton);
		static CButtonContainer s_Reset;
		if(Ui()->DoButton_FontIcon(&s_Reset, FontIcon::ARROW_ROTATE_LEFT, 0, &ResetButton, BUTTONFLAG_LEFT))
		{
			g_Config.m_BcFreezeLightningImpact = DefaultConfig::BcFreezeLightningImpact;
			g_Config.m_BcFreezeLightningHeight = DefaultConfig::BcFreezeLightningHeight;
			g_Config.m_BcFreezeLightningThickness = DefaultConfig::BcFreezeLightningThickness;
			g_Config.m_BcFreezeLightningBranches = DefaultConfig::BcFreezeLightningBranches;
			g_Config.m_BcFreezeLightningShake = DefaultConfig::BcFreezeLightningShake;
			g_Config.m_BcFreezeLightningColor = DefaultConfig::BcFreezeLightningColor;
		}
		GameClient()->m_Tooltips.DoToolTip(&s_Reset, &ResetButton, BcLocalize("Reset to defaults"));
		static CButtonContainer s_LightningAuthor;
		DrawFxTitle(Title, BcLocalize("Freeze lightning"), &s_LightningAuthor);
		Block.HSplitTop(MarginSmall, nullptr, &Block);
		CUIRect Enable;
		Block.HSplitTop(LineSize, &Enable, &Block);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcFreezeLightning, BcLocalize("Enable"), &g_Config.m_BcFreezeLightning, &Enable, LineSize);

		if(ExpandedHeight > 0.5f)
		{
			CUIRect Visible = Block;
			Visible.h = ExpandedHeight;
			Ui()->ClipEnable(&Visible);
			RowCheck(Block, &g_Config.m_BcFreezeLightningImpact, BcLocalize("Impact"));
			RowSlider(Block, &g_Config.m_BcFreezeLightningHeight, BcLocalize("Height"), 100, 900, "");
			RowSlider(Block, &g_Config.m_BcFreezeLightningThickness, BcLocalize("Thickness"), 50, 200, "%");
			RowSlider(Block, &g_Config.m_BcFreezeLightningBranches, BcLocalize("Branches"), 0, 8, "");
			RowSlider(Block, &g_Config.m_BcFreezeLightningShake, BcLocalize("Shake"), 0, 100, "%");
			static CButtonContainer s_Color;
			RowColor(Block, &s_Color, BcLocalize("Color"), &g_Config.m_BcFreezeLightningColor, DefaultConfig::BcFreezeLightningColor);
			Block.HSplitTop(MarginSmall, nullptr, &Block);
			CUIRect Preview;
			Block.HSplitTop(LineSize, &Preview, &Block);
			if(GameClient()->m_Snap.m_pLocalCharacter != nullptr)
			{
				static CButtonContainer s_Strike;
				if(DoButton_Menu(&s_Strike, BcLocalize("Strike me"), 0, &Preview))
				{
					GameClient()->m_Lightning.Strike(GameClient()->m_LocalCharacterPos, 1.0f, true);
					if(g_Config.m_BcFreezeLightningShake > 0)
						GameClient()->m_Lightning.AddShake(g_Config.m_BcFreezeLightningShake / 100.0f * 14.0f);
				}
			}
			else
				Ui()->DoLabel(&Preview, BcLocalize("Join a server to preview the lightning"), 13.0f, TEXTALIGN_ML);
			Ui()->ClipDisable();
		}
	}

		Column.HSplitTop(MarginBetweenViews, nullptr, &Column);
	}
}
