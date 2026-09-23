/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_HUD_LAYOUT_H
#define GAME_CLIENT_COMPONENTS_HUD_LAYOUT_H

#include <engine/graphics.h>

class IConsole;
class IConfigManager;
class ITextRender;

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
	MODULE_FINISH_PREDICTION,
	MODULE_KEYSTROKES_KEYBOARD,
	MODULE_KEYSTROKES_MOUSE,
	MODULE_DUMMY_ACTIONS,
	MODULE_SWAP_TIMER,
	MODULE_EDGE_INFO,
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
	int m_Alpha;
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

enum EPositionMode
{
	POSITION_MODE_TOP_LEFT = 0,
	POSITION_MODE_BOTTOM_RIGHT,
};

bool IsEditorModule(EModule Module);
bool IsPersistedModule(EModule Module);
const char *Id(EModule Module);
const char *Name(EModule Module);
EModule ModuleFromId(const char *pId);
SModuleLayout Get(EModule Module, float HudWidth, float HudHeight);
SModuleLayout GetDefault(EModule Module, float HudWidth, float HudHeight);
bool HasRuntimeOverride(EModule Module);
bool HasPositionOverride(EModule Module);
void SetPosition(EModule Module, float X, float Y);
void SetPosition(EModule Module, float X, float Y, EPositionMode PositionMode);
void SetScale(EModule Module, int Scale);
void SetEnabled(EModule Module, bool Enabled);
void SetAlpha(EModule Module, int Alpha);
bool IsEnabled(EModule Module);
int Alpha(EModule Module);
float AlphaFactor(EModule Module);
void ResetPosition(EModule Module);
void ResetScale(EModule Module);
void ResetSettings(EModule Module);
void ResetEditableModules();
SModuleRect ClampRectToScreen(const SModuleRect &Rect, float HudWidth, float HudHeight);
float CanvasXToHud(float CanvasX, float HudWidth);
int BackgroundCorners(int DefaultCorners, float RectX, float RectY, float RectW, float RectH, float CanvasWidth, float CanvasHeight);
void TryApplyMusicPlayerLegacyPositionFix(float HudWidth, float HudHeight, ITextRender *pTextRender);
void SetMusicPlayerResolvedDefaultX(float HudX, float HudWidth);
void OnConsoleInit(IConsole *pConsole, IConfigManager *pConfigManager);

} // namespace HudLayout

#endif
