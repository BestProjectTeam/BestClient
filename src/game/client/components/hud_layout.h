#ifndef GAME_CLIENT_COMPONENTS_HUD_LAYOUT_H
#define GAME_CLIENT_COMPONENTS_HUD_LAYOUT_H

#include <engine/graphics.h>

class IConsole;
class IConfigManager;

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
	MODULE_CHAT,
	MODULE_VOTES,
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
	bool m_Enabled;
	bool m_BackgroundEnabled;
	unsigned m_BackgroundColor;
};

struct SModuleRect
{
	float m_X;
	float m_Y;
	float m_W;
	float m_H;
	float m_Rounding;
};

constexpr float CANVAS_WIDTH = 500.0f;
constexpr float CANVAS_HEIGHT = 300.0f;

bool IsEditableModule(EModule Module);
const char *Name(EModule Module);
SModuleLayout Get(EModule Module, float HudWidth, float HudHeight);
bool HasRuntimeOverride(EModule Module);
void SetPosition(EModule Module, float X, float Y);
void SetScale(EModule Module, int Scale);
void SetEnabled(EModule Module, bool Enabled);
bool IsEnabled(EModule Module);
void ResetPosition(EModule Module);
void ResetSettings(EModule Module);
void Reset(EModule Module);
void ResetEditableModules();
SModuleRect ClampRectToScreen(const SModuleRect &Rect, float HudWidth, float HudHeight);
float CanvasXToHud(float CanvasX, float HudWidth);
int BackgroundCorners(int DefaultCorners, float RectX, float RectY, float RectW, float RectH, float CanvasWidth, float CanvasHeight);
void OnConsoleInit(IConsole *pConsole, IConfigManager *pConfigManager);

} // namespace HudLayout

#endif
