/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_UI_THEME_WIDGETS_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_UI_THEME_WIDGETS_H

#include <algorithm>

#include <base/color.h>

#include <game/client/components/bestclient/ui_theme/style.h>
#include <game/client/ui_rect.h>

class CUi;

namespace BestClientUiTheme
{

inline ColorRGBA SliderValueColor()
{
	return UiText(ColorRGBA(1.0f, 1.0f, 1.0f, 0.95f));
}

inline float SliderRounding(float Height)
{
	return std::min(3.0f, Height * 0.35f);
}

inline float CheckBoxRounding(float Size)
{
	return std::min(3.0f, Size * 0.35f);
}

inline ColorRGBA SettingsNavFromUi(float RgbScale, float Alpha)
{
	if(IsCustomUiNavTab())
		return ColorRGBA(RgbScale, RgbScale, RgbScale, Alpha);
	const ColorRGBA Gui = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_UiColor, true));
	return ColorRGBA(Gui.r * RgbScale, Gui.g * RgbScale, Gui.b * RgbScale, Alpha);
}

inline ColorRGBA SettingsNavGroupInactiveColor()
{
	return SettingsNavFromUi(0.16f, 0.86f);
}

inline ColorRGBA SettingsNavGroupActiveColor()
{
	return SettingsNavFromUi(0.10f, 0.94f);
}

inline ColorRGBA SettingsNavGroupHoverColor()
{
	return SettingsNavFromUi(0.26f, 0.90f);
}

inline ColorRGBA SettingsNavLeafInactiveColor()
{
	return SettingsNavFromUi(0.28f, 0.90f);
}

inline ColorRGBA SettingsNavLeafActiveColor()
{
	return SettingsNavFromUi(0.16f, 0.96f);
}

inline ColorRGBA SettingsNavLeafHoverColor()
{
	return SettingsNavFromUi(0.38f, 0.92f);
}

void DrawCheckBoxChecked(const CUIRect *pBox, float ColorMul);

float DoScrollbarH(CUi *pUi, const void *pId, const CUIRect *pRect, float Current, const char *pValueText);

} // namespace BestClientUiTheme

#endif
