#include "hud_layout.h"

#include <base/math.h>

#include <engine/shared/config.h>

#include <algorithm>

namespace HudLayout
{

namespace
{

static const SModuleLayout gs_aModuleLayouts[MODULE_COUNT] = {
	{0.0f, 60.0f, 100, 0, true, 0x66000000U},
	{304.0f, 0.0f, 100, 0, true, 0x66000000U},
	{470.0f, 205.0f, 100, 0, true, 0x66000000U},
	{100.0f, 3.0f, 100, 0, false, 0x66000000U},
	{500.0f, 5.0f, 100, 0, false, 0x66000000U},
	{500.0f, 20.0f, 100, 0, false, 0x66000000U},
	{233.0f, 64.0f, 100, 0, false, 0x66000000U},
	{220.0f, 240.0f, 100, 0, false, 0x66000000U},
	{110.0f, 2.0f, 100, 0, true, 0x66000000U},
	{500.0f, 141.0f, 100, 0, true, 0x66000000U},
	{460.0f, 229.0f, 100, 0, true, 0x40000000U},
	{184.0f, 2.0f, 100, 0, false, 0x1E59A36BU},
	{4.0f, 122.0f, 100, 0, false, 0x66000000U},
	{326.0f, 2.0f, 100, 0, false, 0x66000000U},
	{250.0f, 200.0f, 65, 0, true, 0x66000000U},
	{490.0f, 5.0f, 100, 0, false, 0x66000000U},
};

bool IsLegacyModule(EModule Module)
{
	return Module >= MODULE_MINI_VOTE && Module <= MODULE_GAME_TIMER;
}

SModuleLayout ResolveBaseLayout(EModule Module, float HudWidth, float HudHeight)
{
	SModuleLayout Layout = gs_aModuleLayouts[Module];
	if(IsLegacyModule(Module))
	{
		switch(Module)
		{
		case MODULE_MINI_VOTE:
			Layout.m_X = 0.0f;
			Layout.m_Y = 60.0f;
			break;
		case MODULE_FROZEN_HUD:
			Layout.m_X = (float)round_to_int(HudWidth * 0.595f);
			Layout.m_Y = 0.0f;
			break;
		case MODULE_MOVEMENT_INFO:
			Layout.m_X = (float)round_to_int(HudWidth - 62.0f);
			Layout.m_Y = 205.0f;
			break;
		case MODULE_NOTIFY_LAST:
			Layout.m_X = (float)round_to_int(HudWidth * 0.2f);
			Layout.m_Y = (float)round_to_int(HudHeight * 0.01f);
			break;
		case MODULE_FPS:
			Layout.m_X = (float)round_to_int(HudWidth - 26.0f);
			Layout.m_Y = 5.0f;
			break;
		case MODULE_PING:
			Layout.m_X = (float)round_to_int(HudWidth - 26.0f);
			Layout.m_Y = 20.0f;
			break;
		case MODULE_GAME_TIMER:
			Layout.m_X = (float)round_to_int(HudWidth * 0.5f - 22.0f);
			Layout.m_Y = -2.0f;
			break;
		default:
			break;
		}
	}
	else
	{
		Layout.m_X = CanvasXToHud(Layout.m_X, HudWidth);
		if(Module == MODULE_MUSIC_PLAYER)
		{
			const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
			const float WidthScale = HudWidth / maximum(CANVAS_WIDTH, 0.001f);
			const float ExpandedW = 104.0f * Scale * WidthScale;
			Layout.m_X = std::clamp((HudWidth - ExpandedW) * 0.5f, 0.0f, maximum(0.0f, HudWidth - ExpandedW));
		}
	}
	return Layout;
}

} // namespace

SModuleLayout Get(EModule Module, float HudWidth, float HudHeight)
{
	SModuleLayout Layout = ResolveBaseLayout(Module, HudWidth, HudHeight);
	if(Module == MODULE_HOOK_COMBO)
	{
		const float UserScale = std::clamp(g_Config.m_BcHookComboSize / 100.0f, 0.5f, 2.0f);
		Layout.m_Scale = round_to_int(Layout.m_Scale * UserScale);
	}
	return Layout;
}

float CanvasXToHud(float CanvasX, float HudWidth)
{
	return CanvasX * (HudWidth / CANVAS_WIDTH);
}

int BackgroundCorners(int DefaultCorners, float RectX, float RectY, float RectW, float RectH, float CanvasWidth, float CanvasHeight)
{
	(void)RectX;
	(void)RectY;
	(void)RectW;
	(void)RectH;
	(void)CanvasWidth;
	(void)CanvasHeight;
	return DefaultCorners;
}

} // namespace HudLayout
