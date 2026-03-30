#include "hud_layout.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/config.h>
#include <engine/console.h>
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
	{198.0f, 2.0f, 100, 0, false, 0x1E59A36BU},
	{4.0f, 122.0f, 100, 0, false, 0x66000000U},
	{136.0f, 0.0f, 100, 0, false, 0x66000000U},
	{5.0f, 278.0f, 100, 0, false, 0x66000000U},
	{0.0f, 60.0f, 100, 0, true, 0x66000000U},
	{250.0f, 200.0f, 65, 0, true, 0x66000000U},
	{490.0f, 5.0f, 100, 0, false, 0x66000000U},
};

static SModuleLayout gs_aRuntimeModuleLayouts[MODULE_COUNT];
static bool gs_RuntimeLayoutsInitialized = false;
static bool gs_ConfigCallbackRegistered = false;
static bool gs_ConsoleCommandRegistered = false;

void EnsureRuntimeLayouts()
{
	if(gs_RuntimeLayoutsInitialized)
		return;
	for(int i = 0; i < MODULE_COUNT; ++i)
		gs_aRuntimeModuleLayouts[i] = gs_aModuleLayouts[i];
	gs_RuntimeLayoutsInitialized = true;
}

bool HasRuntimeOverride(EModule Module)
{
	EnsureRuntimeLayouts();
	const SModuleLayout &Runtime = gs_aRuntimeModuleLayouts[Module];
	const SModuleLayout &Default = gs_aModuleLayouts[Module];
	return Runtime.m_X != Default.m_X ||
		Runtime.m_Y != Default.m_Y ||
		Runtime.m_Scale != Default.m_Scale ||
		Runtime.m_Mode != Default.m_Mode ||
		Runtime.m_BackgroundEnabled != Default.m_BackgroundEnabled ||
		Runtime.m_BackgroundColor != Default.m_BackgroundColor;
}

static const char *gs_apModuleNames[MODULE_COUNT] = {
	"Mini Vote",
	"Frozen HUD",
	"Movement Info",
	"Notify Last",
	"FPS",
	"Ping",
	"Game Timer",
	"Hook Combo",
	"Local Time",
	"Spectator Count",
	"Score",
	"Music Player",
	"Voice HUD",
	"Voice Mute Icons",
	"Ingame Chat",
	"Votes",
	"Lock Cam",
	"Killfeed",
};

SModuleLayout ConfigLayout(EModule Module)
{
	EnsureRuntimeLayouts();
	switch(Module)
	{
	case MODULE_MUSIC_PLAYER:
		return gs_aModuleLayouts[Module];
	case MODULE_VOICE_TALKERS:
		return {(float)g_Config.m_BcHudVoiceHudX, (float)g_Config.m_BcHudVoiceHudY, g_Config.m_BcHudVoiceHudScale, 0, gs_aModuleLayouts[Module].m_BackgroundEnabled, gs_aModuleLayouts[Module].m_BackgroundColor};
	case MODULE_VOICE_STATUS:
		return {(float)g_Config.m_BcHudVoiceMuteIconsX, (float)g_Config.m_BcHudVoiceMuteIconsY, g_Config.m_BcHudVoiceMuteIconsScale, 0, gs_aModuleLayouts[Module].m_BackgroundEnabled, gs_aModuleLayouts[Module].m_BackgroundColor};
	case MODULE_CHAT:
		return {(float)g_Config.m_BcHudChatX, (float)g_Config.m_BcHudChatY, g_Config.m_BcHudChatScale, 0, gs_aModuleLayouts[Module].m_BackgroundEnabled, gs_aModuleLayouts[Module].m_BackgroundColor};
	case MODULE_VOTES:
		return {(float)g_Config.m_BcHudVotesX, (float)g_Config.m_BcHudVotesY, g_Config.m_BcHudVotesScale, 0, gs_aModuleLayouts[Module].m_BackgroundEnabled, gs_aModuleLayouts[Module].m_BackgroundColor};
	default:
		return gs_aRuntimeModuleLayouts[Module];
	}
}

void WriteConfigLayout(EModule Module, const SModuleLayout &Layout)
{
	switch(Module)
	{
	case MODULE_MUSIC_PLAYER:
		g_Config.m_BcHudMusicPlayerX = round_to_int(Layout.m_X);
		g_Config.m_BcHudMusicPlayerY = round_to_int(Layout.m_Y);
		g_Config.m_BcHudMusicPlayerScale = Layout.m_Scale;
		break;
	case MODULE_VOICE_TALKERS:
		g_Config.m_BcHudVoiceHudX = round_to_int(Layout.m_X);
		g_Config.m_BcHudVoiceHudY = round_to_int(Layout.m_Y);
		g_Config.m_BcHudVoiceHudScale = Layout.m_Scale;
		break;
	case MODULE_VOICE_STATUS:
		g_Config.m_BcHudVoiceMuteIconsX = round_to_int(Layout.m_X);
		g_Config.m_BcHudVoiceMuteIconsY = round_to_int(Layout.m_Y);
		g_Config.m_BcHudVoiceMuteIconsScale = Layout.m_Scale;
		break;
	case MODULE_CHAT:
		g_Config.m_BcHudChatX = round_to_int(Layout.m_X);
		g_Config.m_BcHudChatY = round_to_int(Layout.m_Y);
		g_Config.m_BcHudChatScale = Layout.m_Scale;
		break;
	case MODULE_VOTES:
		g_Config.m_BcHudVotesX = round_to_int(Layout.m_X);
		g_Config.m_BcHudVotesY = round_to_int(Layout.m_Y);
		g_Config.m_BcHudVotesScale = Layout.m_Scale;
		break;
	default:
		EnsureRuntimeLayouts();
		gs_aRuntimeModuleLayouts[Module] = Layout;
		break;
	}
}

bool IsLegacyModule(EModule Module)
{
	return Module >= MODULE_MINI_VOTE && Module <= MODULE_GAME_TIMER;
}

SModuleLayout ResolveBaseLayout(EModule Module, float HudWidth, float HudHeight)
{
	SModuleLayout Layout = gs_aModuleLayouts[Module];
	if(IsLegacyModule(Module) && !HasRuntimeOverride(Module))
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
		Layout = ConfigLayout(Module);
		Layout.m_X = CanvasXToHud(Layout.m_X, HudWidth);
		if(Module == MODULE_MUSIC_PLAYER)
		{
			Layout.m_X = std::clamp(Layout.m_X, 0.0f, HudWidth);
		}
	}
	return Layout;
}

void ConHudLayoutSet(IConsole::IResult *pResult, void *pUserData)
{
	(void)pUserData;

	const int ModuleIndex = pResult->GetInteger(0);
	if(ModuleIndex < 0 || ModuleIndex >= MODULE_COUNT)
		return;
	if(ModuleIndex == MODULE_MUSIC_PLAYER || ModuleIndex == MODULE_GAME_TIMER)
		return;

	const EModule Module = (EModule)ModuleIndex;
	SModuleLayout Layout = ConfigLayout(Module);
	Layout.m_X = pResult->GetFloat(1);
	Layout.m_Y = pResult->GetFloat(2);
	Layout.m_Scale = std::clamp(pResult->GetInteger(3), 25, 300);
	Layout.m_Mode = pResult->GetInteger(4);
	Layout.m_BackgroundEnabled = pResult->GetInteger(5) != 0;
	Layout.m_BackgroundColor = (unsigned)pResult->GetInteger(6);
	WriteConfigLayout(Module, Layout);
}

void ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData)
{
	(void)pUserData;
	EnsureRuntimeLayouts();

	char aLine[256];
	for(int Module = 0; Module < MODULE_COUNT; ++Module)
	{
		if(Module == MODULE_MUSIC_PLAYER || Module == MODULE_GAME_TIMER)
			continue;
		const EModule ModuleId = (EModule)Module;
		const SModuleLayout Layout = ConfigLayout(ModuleId);
		str_format(
			aLine,
			sizeof(aLine),
			"hud_layout_set %d %.3f %.3f %d %d %d %u",
			Module,
			Layout.m_X,
			Layout.m_Y,
			Layout.m_Scale,
			Layout.m_Mode,
			Layout.m_BackgroundEnabled ? 1 : 0,
			Layout.m_BackgroundColor);
		pConfigManager->WriteLine(aLine, ConfigDomain::HUDLAYOUT);
	}
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

bool IsEditableModule(EModule Module)
{
	return Module >= 0 && Module < MODULE_COUNT;
}

const char *Name(EModule Module)
{
	return Module >= 0 && Module < MODULE_COUNT ? gs_apModuleNames[Module] : "HUD Module";
}

void SetPosition(EModule Module, float X, float Y)
{
	SModuleLayout Layout = ConfigLayout(Module);
	Layout.m_X = X;
	Layout.m_Y = Y;
	WriteConfigLayout(Module, Layout);
}

void SetScale(EModule Module, int Scale)
{
	SModuleLayout Layout = ConfigLayout(Module);
	Layout.m_Scale = std::clamp(Scale, 25, 300);
	WriteConfigLayout(Module, Layout);
}

void Reset(EModule Module)
{
	WriteConfigLayout(Module, gs_aModuleLayouts[Module]);
}

void ResetEditableModules()
{
	for(int Module = 0; Module < MODULE_COUNT; ++Module)
	{
		if(IsEditableModule((EModule)Module))
			Reset((EModule)Module);
	}
}

SModuleRect ClampRectToScreen(const SModuleRect &Rect, float HudWidth, float HudHeight)
{
	SModuleRect Result = Rect;
	Result.m_X = std::clamp(Result.m_X, 0.0f, maximum(0.0f, HudWidth - Result.m_W));
	Result.m_Y = std::clamp(Result.m_Y, 0.0f, maximum(0.0f, HudHeight - Result.m_H));
	return Result;
}

float CanvasXToHud(float CanvasX, float HudWidth)
{
	return CanvasX * (HudWidth / CANVAS_WIDTH);
}

int BackgroundCorners(int DefaultCorners, float RectX, float RectY, float RectW, float RectH, float CanvasWidth, float CanvasHeight)
{
	int Corners = DefaultCorners;
	const float Eps = 0.01f;
	if(RectW <= 0.0f || RectH <= 0.0f)
		return Corners;

	if(RectX <= Eps)
		Corners &= ~IGraphics::CORNER_L;
	if(RectX + RectW >= CanvasWidth - Eps)
		Corners &= ~IGraphics::CORNER_R;
	if(RectY <= Eps)
		Corners &= ~IGraphics::CORNER_T;
	if(RectY + RectH >= CanvasHeight - Eps)
		Corners &= ~IGraphics::CORNER_B;
	return Corners;
}

void OnConsoleInit(IConsole *pConsole, IConfigManager *pConfigManager)
{
	if(!gs_ConsoleCommandRegistered && pConsole)
	{
		pConsole->Register(
			"hud_layout_set",
			"i[module] f[x] f[y] i[scale] i[mode] i[background_enabled] i[background_color]",
			CFGFLAG_CLIENT,
			ConHudLayoutSet,
			nullptr,
			"Set HUD module layout entry");
		gs_ConsoleCommandRegistered = true;
	}

	if(!gs_ConfigCallbackRegistered && pConfigManager)
	{
		pConfigManager->RegisterCallback(ConfigSaveCallback, nullptr, ConfigDomain::HUDLAYOUT);
		gs_ConfigCallbackRegistered = true;
	}
}

} // namespace HudLayout
