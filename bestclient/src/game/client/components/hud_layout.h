#ifndef GAME_CLIENT_COMPONENTS_HUD_LAYOUT_H
#define GAME_CLIENT_COMPONENTS_HUD_LAYOUT_H

#include <engine/graphics.h>

namespace HudLayout
{

enum EModule
{
	MODULE_MINI_VOTE = 0,
	MODULE_FROZEN_HUD,
	MODULE_MOVEMENT_INFO,
	MODULE_NOTIFY_LAST,
	MODULE_FPS,
	MODULE_PING,
	MODULE_GAME_TIMER,
	MODULE_HOOK_COMBO,
	MODULE_LOCAL_TIME,
	MODULE_SPECTATOR_COUNT,
	MODULE_SCORE,
	MODULE_MUSIC_PLAYER,
	MODULE_VOICE_TALKERS,
	MODULE_VOICE_STATUS,
	MODULE_LOCK_CAM,
	MODULE_KILLFEED,
	MODULE_COUNT,
};

struct SModuleLayout
{
	float m_X;
	float m_Y;
	int m_Scale;
	int m_Mode;
	bool m_BackgroundEnabled;
	unsigned m_BackgroundColor;
};

constexpr float CANVAS_WIDTH = 500.0f;
constexpr float CANVAS_HEIGHT = 300.0f;

SModuleLayout Get(EModule Module, float HudWidth, float HudHeight);
float CanvasXToHud(float CanvasX, float HudWidth);
int BackgroundCorners(int DefaultCorners, float RectX, float RectY, float RectW, float RectH, float CanvasWidth, float CanvasHeight);

} // namespace HudLayout

#endif
