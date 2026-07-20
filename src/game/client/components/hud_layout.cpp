/* Copyright © 2026 BestProject Team */
#include "hud_layout.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/config.h>
#include <engine/console.h>
#include <engine/shared/config.h>

#include <game/client/components/hud.h>

#include <algorithm>

namespace HudLayout
{

	namespace
	{

		// Default layout per module, indexed by EModule. Positions are on the
		// CANVAS_WIDTH x CANVAS_HEIGHT canvas unless HasDynamicDefault() applies.
		static const SModuleLayout gs_aModuleLayouts[MODULE_COUNT] = {
			{0.0f, 60.0f, 100, 0, true, true, 0x66000000U}, // MINI_VOTE
			{304.0f, 0.0f, 100, 0, true, true, 0x66000000U}, // FROZEN_HUD
			{470.0f, 205.0f, 100, 0, true, true, 0x66000000U}, // MOVEMENT_INFO
			{100.0f, 3.0f, 100, 0, true, false, 0x66000000U}, // NOTIFY_LAST
			{500.0f, 5.0f, 100, 0, true, false, 0x66000000U}, // FPS
			{500.0f, 20.0f, 100, 0, true, false, 0x66000000U}, // PING
			{233.0f, 64.0f, 100, 0, true, false, 0x66000000U}, // GAME_TIMER
			{220.0f, 240.0f, 100, 0, true, false, 0x66000000U}, // HOOK_COMBO
			{110.0f, 2.0f, 100, 0, true, true, 0x66000000U}, // LOCAL_TIME
			{500.0f, 141.0f, 100, 0, true, true, 0x66000000U}, // SPECTATOR_COUNT
			{460.0f, 229.0f, 100, 0, true, true, 0x40000000U}, // SCORE
			{198.0f, 0.0f, 100, 0, true, false, 0x1E59A36BU}, // MUSIC_PLAYER
			{0.0f, 100.0f, 100, 0, true, false, 0x66000000U}, // VOICE_TALKERS
			{136.0f, 0.0f, 100, 0, true, false, 0x66000000U}, // VOICE_STATUS
			{5.0f, 278.0f, 100, 0, true, false, 0x66000000U}, // CHAT
			{0.0f, 60.0f, 100, 0, true, true, 0x66000000U}, // VOTES
			{250.0f, 200.0f, 65, 0, true, true, 0x66000000U}, // LOCK_CAM
			{490.0f, 5.0f, 100, 0, true, false, 0x66000000U}, // KILLFEED
			{0.0f, 129.0f, 100, 0, true, true, 0x66000000U}, // FINISH_PREDICTION
			{0.0f, 152.0f, 100, 0, true, false, 0x66000000U}, // KEYSTROKES_KEYBOARD
			// KEYSTROKES_MOUSE: X/Y here are unused - see HasDynamicDefault()/DynamicDefaultLayout()
			// below, which computes its default position directly in HUD-pixel space (not
			// canvas-relative like most modules), offset past the keyboard's *current* preset
			// width so it doesn't drift into the keyboard at non-widescreen aspect ratios or
			// when the keyboard preset changes.
			{84.5f, 152.0f, 100, 0, true, false, 0x66000000U}, // KEYSTROKES_MOUSE
			{484.0f, 172.0f, 100, 0, true, true, 0x66000000U}, // DUMMY_ACTIONS
		};

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
			"Finish Prediction",
			"Keyboard",
			"Mouse",
			"Dummy Actions",
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

		bool HasRuntimeOverrideInternal(EModule Module)
		{
			EnsureRuntimeLayouts();
			const SModuleLayout &Runtime = gs_aRuntimeModuleLayouts[Module];
			const SModuleLayout &Default = gs_aModuleLayouts[Module];
			return Runtime.m_X != Default.m_X ||
			       Runtime.m_Y != Default.m_Y ||
			       Runtime.m_Scale != Default.m_Scale ||
			       Runtime.m_Mode != Default.m_Mode ||
			       Runtime.m_Enabled != Default.m_Enabled ||
			       Runtime.m_BackgroundEnabled != Default.m_BackgroundEnabled ||
			       Runtime.m_BackgroundColor != Default.m_BackgroundColor;
		}

		bool IsLegacyConfigModule(EModule Module)
		{
			return Module == MODULE_MUSIC_PLAYER ||
			       Module == MODULE_VOICE_TALKERS ||
			       Module == MODULE_VOICE_STATUS ||
			       Module == MODULE_CHAT ||
			       Module == MODULE_VOTES ||
			       Module == MODULE_NOTIFY_LAST;
		}

		bool HasLegacyConfigOverride(EModule Module)
		{
			switch(Module)
			{
			case MODULE_MUSIC_PLAYER:
				return g_Config.m_BcHudMusicPlayerX != (int)gs_aModuleLayouts[Module].m_X ||
				       g_Config.m_BcHudMusicPlayerY != (int)gs_aModuleLayouts[Module].m_Y ||
				       g_Config.m_BcHudMusicPlayerScale != gs_aModuleLayouts[Module].m_Scale;
			case MODULE_VOICE_TALKERS:
				return g_Config.m_BcHudVoiceHudX != (int)gs_aModuleLayouts[Module].m_X ||
				       g_Config.m_BcHudVoiceHudY != (int)gs_aModuleLayouts[Module].m_Y ||
				       g_Config.m_BcHudVoiceHudScale != gs_aModuleLayouts[Module].m_Scale;
			case MODULE_VOICE_STATUS:
				return g_Config.m_BcHudVoiceMuteIconsX != (int)gs_aModuleLayouts[Module].m_X ||
				       g_Config.m_BcHudVoiceMuteIconsY != (int)gs_aModuleLayouts[Module].m_Y ||
				       g_Config.m_BcHudVoiceMuteIconsScale != gs_aModuleLayouts[Module].m_Scale;
			case MODULE_CHAT:
				return g_Config.m_BcHudChatX != (int)gs_aModuleLayouts[Module].m_X ||
				       g_Config.m_BcHudChatY != (int)gs_aModuleLayouts[Module].m_Y ||
				       g_Config.m_BcHudChatScale != gs_aModuleLayouts[Module].m_Scale;
			case MODULE_VOTES:
				return g_Config.m_BcHudVotesX != (int)gs_aModuleLayouts[Module].m_X ||
				       g_Config.m_BcHudVotesY != (int)gs_aModuleLayouts[Module].m_Y ||
				       g_Config.m_BcHudVotesScale != gs_aModuleLayouts[Module].m_Scale;
			case MODULE_NOTIFY_LAST:
				// Percentage + font-size settings are always the source of truth so the
				// tclient scrollbars and the HUD editor stay in sync both ways.
				return true;
			default:
				return false;
			}
		}

		SModuleLayout LegacyConfigLayout(EModule Module)
		{
			EnsureRuntimeLayouts();
			SModuleLayout Layout = gs_aRuntimeModuleLayouts[Module];
			switch(Module)
			{
			case MODULE_MUSIC_PLAYER:
				Layout.m_X = (float)g_Config.m_BcHudMusicPlayerX;
				Layout.m_Y = (float)g_Config.m_BcHudMusicPlayerY;
				Layout.m_Scale = g_Config.m_BcHudMusicPlayerScale;
				break;
			case MODULE_VOICE_TALKERS:
				Layout.m_X = (float)g_Config.m_BcHudVoiceHudX;
				Layout.m_Y = (float)g_Config.m_BcHudVoiceHudY;
				Layout.m_Scale = g_Config.m_BcHudVoiceHudScale;
				break;
			case MODULE_VOICE_STATUS:
				Layout.m_X = (float)g_Config.m_BcHudVoiceMuteIconsX;
				Layout.m_Y = (float)g_Config.m_BcHudVoiceMuteIconsY;
				Layout.m_Scale = g_Config.m_BcHudVoiceMuteIconsScale;
				break;
			case MODULE_CHAT:
				Layout.m_X = (float)g_Config.m_BcHudChatX;
				Layout.m_Y = (float)g_Config.m_BcHudChatY;
				Layout.m_Scale = g_Config.m_BcHudChatScale;
				break;
			case MODULE_VOTES:
				Layout.m_X = (float)g_Config.m_BcHudVotesX;
				Layout.m_Y = (float)g_Config.m_BcHudVotesY;
				Layout.m_Scale = g_Config.m_BcHudVotesScale;
				break;
			case MODULE_NOTIFY_LAST:
				// tc_last_notify_x/y are % of the HUD canvas; map them into the same
				// canvas-space storage the other legacy modules use (X scaled via
				// CanvasXToHud in ResolveBaseLayout, Y already in HUD pixels at height 300).
				// Font size stays on tc_last_notify_size (absolute) so the 1..50 range
				// is preserved; Layout.m_Scale remains an editor-only multiplier.
				Layout.m_X = (g_Config.m_TcNotifyWhenLastX / 100.0f) * CANVAS_WIDTH;
				Layout.m_Y = (g_Config.m_TcNotifyWhenLastY / 100.0f) * CANVAS_HEIGHT;
				// Keep runtime in sync when the settings scrollbars change so
				// hud_layout_set saves the same position the player sees.
				gs_aRuntimeModuleLayouts[Module].m_X = Layout.m_X;
				gs_aRuntimeModuleLayouts[Module].m_Y = Layout.m_Y;
				break;
			default:
				break;
			}
			return Layout;
		}

		void WriteLegacyConfigLayout(EModule Module, const SModuleLayout &Layout)
		{
			if(!IsLegacyConfigModule(Module))
				return;

			gs_aRuntimeModuleLayouts[Module] = Layout;
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
			case MODULE_NOTIFY_LAST:
				g_Config.m_TcNotifyWhenLastX = std::clamp(round_to_int((Layout.m_X / CANVAS_WIDTH) * 100.0f), 0, 100);
				g_Config.m_TcNotifyWhenLastY = std::clamp(round_to_int((Layout.m_Y / CANVAS_HEIGHT) * 100.0f), 0, 100);
				break;
			default:
				break;
			}
		}

		// Modules whose default position is computed directly in HUD-pixel space (as
		// opposed to a fixed canvas coordinate that scales with aspect ratio via
		// CanvasXToHud()) until the user drags them for the first time. Most of these are
		// right/center anchored; KEYSTROKES_MOUSE instead needs an aspect-independent
		// offset from the keyboard so the two don't drift into each other - see
		// DynamicDefaultLayout() below.
		bool HasDynamicDefault(EModule Module)
		{
			switch(Module)
			{
			case MODULE_MINI_VOTE:
			case MODULE_FROZEN_HUD:
			case MODULE_MOVEMENT_INFO:
			case MODULE_NOTIFY_LAST:
			case MODULE_FPS:
			case MODULE_PING:
			case MODULE_GAME_TIMER:
			case MODULE_HOOK_COMBO:
			case MODULE_LOCAL_TIME:
			case MODULE_KEYSTROKES_MOUSE:
			case MODULE_DUMMY_ACTIONS:
				return true;
			default:
				return false;
			}
		}

		// The default layout for a module at a given HUD size, as if it had never been
		// overridden - used both by ResolveBaseLayout() below and by the public GetDefault()
		// (see hud_layout.h), which the HUD editor uses as the animation target for
		// "reset position"/"reset scale". Modules outside HasDynamicDefault() just keep
		// gs_aModuleLayouts[Module] unchanged (the switch's default case).
		SModuleLayout DynamicDefaultLayout(EModule Module, float HudWidth, float HudHeight)
		{
			SModuleLayout Layout = gs_aModuleLayouts[Module];
			switch(Module)
			{
			case MODULE_MINI_VOTE:
				Layout.m_X = 0.0f;
				Layout.m_Y = 60.0f;
				break;
			case MODULE_FROZEN_HUD:
				// Matches CHud's pre-editor default frozen-tee-strip anchor, nudged right a bit.
				Layout.m_X = HudWidth / 2.0f + 60.0f * (HudWidth / HudHeight) / 1.78f;
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
			case MODULE_LOCAL_TIME:
				Layout.m_X = HudWidth / 7.0f * 3.0f;
				Layout.m_Y = 0.0f;
				break;
			case MODULE_KEYSTROKES_MOUSE:
				// A fixed HUD-pixel gap past the keyboard's own default (100% scale) width for
				// whichever keyboard preset is currently selected, not canvas-relative -
				// CanvasXToHud() would shrink this offset at narrower aspect ratios while the
				// keyboard's own pixel width stays fixed, drifting the two into each other.
				// Recomputed from the live preset (rather than assuming the minimal preset) so
				// switching keyboard presets doesn't make the mouse module overlap or drift away.
				// Only chases the keyboard like this while the keyboard is also still at its own
				// default (X = 0) - once it's been dragged, this offset is no longer measured
				// from where the keyboard actually renders, so keep the flat fallback instead of
				// sending the mouse to an unrelated spot.
				if(!HasRuntimeOverrideInternal(MODULE_KEYSTROKES_KEYBOARD))
					Layout.m_X = GetKeystrokesKeyboardPresetWidthHudPx(g_Config.m_BcKeystrokesKeyboardPreset) + 5.0f;
				Layout.m_Y = 152.0f;
				break;
			case MODULE_DUMMY_ACTIONS:
				Layout.m_X = (float)round_to_int(HudWidth - 16.0f);
				Layout.m_Y = 172.0f;
				break;
			default:
				break;
			}
			return Layout;
		}

		SModuleLayout ResolveBaseLayout(EModule Module, float HudWidth, float HudHeight)
		{
			if(IsLegacyConfigModule(Module) && HasLegacyConfigOverride(Module))
			{
				SModuleLayout Layout = LegacyConfigLayout(Module);
				Layout.m_X = CanvasXToHud(Layout.m_X, HudWidth);
				return Layout;
			}

			SModuleLayout Layout;
			if(HasDynamicDefault(Module) && !HasRuntimeOverrideInternal(Module))
			{
				Layout = DynamicDefaultLayout(Module, HudWidth, HudHeight);
			}
			else
			{
				EnsureRuntimeLayouts();
				Layout = gs_aRuntimeModuleLayouts[Module];
				Layout.m_X = CanvasXToHud(Layout.m_X, HudWidth);
			}
			return Layout;
		}

		void ConHudLayoutSet(IConsole::IResult *pResult, void *pUserData)
		{
			(void)pUserData;

			const int ModuleIndex = pResult->GetInteger(0);
			if(ModuleIndex < 0 || ModuleIndex >= MODULE_COUNT)
				return;
			if(ModuleIndex == MODULE_GAME_TIMER)
				return;

			EnsureRuntimeLayouts();
			const EModule Module = (EModule)ModuleIndex;
			SModuleLayout Layout = gs_aRuntimeModuleLayouts[Module];
			Layout.m_X = pResult->GetFloat(1);
			Layout.m_Y = pResult->GetFloat(2);
			Layout.m_Scale = std::clamp(pResult->GetInteger(3), 25, 300);
			Layout.m_Mode = pResult->GetInteger(4);
			Layout.m_BackgroundEnabled = pResult->GetInteger(5) != 0;
			Layout.m_BackgroundColor = (unsigned)pResult->GetInteger(6);
			if(pResult->NumArguments() > 7)
				Layout.m_Enabled = pResult->GetInteger(7) != 0;

			if(IsLegacyConfigModule(Module))
				WriteLegacyConfigLayout(Module, Layout);
			else
				gs_aRuntimeModuleLayouts[Module] = Layout;
		}

		void ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData)
		{
			(void)pUserData;
			EnsureRuntimeLayouts();

			char aLine[256];
			for(int Module = 0; Module < MODULE_COUNT; ++Module)
			{
				if(Module == MODULE_GAME_TIMER)
					continue;
				const SModuleLayout &Layout = gs_aRuntimeModuleLayouts[Module];
				str_format(
					aLine,
					sizeof(aLine),
					"hud_layout_set %d %.3f %.3f %d %d %d %u %d",
					Module,
					Layout.m_X,
					Layout.m_Y,
					Layout.m_Scale,
					Layout.m_Mode,
					Layout.m_BackgroundEnabled ? 1 : 0,
					Layout.m_BackgroundColor,
					Layout.m_Enabled ? 1 : 0);
				pConfigManager->WriteLine(aLine, ConfigDomain::HUDLAYOUT);
			}
		}

		void MigrateLegacyConfigLayouts()
		{
			EnsureRuntimeLayouts();
			for(int Module = 0; Module < MODULE_COUNT; ++Module)
			{
				const EModule ModuleId = (EModule)Module;
				if(!IsLegacyConfigModule(ModuleId) || !HasLegacyConfigOverride(ModuleId) || HasRuntimeOverrideInternal(ModuleId))
					continue;
				gs_aRuntimeModuleLayouts[ModuleId] = LegacyConfigLayout(ModuleId);
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

	SModuleLayout GetDefault(EModule Module, float HudWidth, float HudHeight)
	{
		SModuleLayout Layout = DynamicDefaultLayout(Module, HudWidth, HudHeight);
		if(!HasDynamicDefault(Module))
			Layout.m_X = CanvasXToHud(Layout.m_X, HudWidth);
		return Layout;
	}

	bool HasRuntimeOverride(EModule Module)
	{
		return Module >= 0 && Module < MODULE_COUNT && HasRuntimeOverrideInternal(Module);
	}

	bool HasPositionOverride(EModule Module)
	{
		if(Module < 0 || Module >= MODULE_COUNT)
			return false;

		EnsureRuntimeLayouts();
		const SModuleLayout Layout = IsLegacyConfigModule(Module) && HasLegacyConfigOverride(Module) ?
						     LegacyConfigLayout(Module) :
						     gs_aRuntimeModuleLayouts[Module];
		const SModuleLayout &Default = gs_aModuleLayouts[Module];
		return Layout.m_X != Default.m_X ||
		       Layout.m_Y != Default.m_Y ||
		       Layout.m_Mode != Default.m_Mode;
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
		EnsureRuntimeLayouts();
		if(IsLegacyConfigModule(Module))
		{
			SModuleLayout Layout = LegacyConfigLayout(Module);
			Layout.m_X = X;
			Layout.m_Y = Y;
			WriteLegacyConfigLayout(Module, Layout);
			return;
		}
		gs_aRuntimeModuleLayouts[Module].m_X = X;
		gs_aRuntimeModuleLayouts[Module].m_Y = Y;
	}

	void SetPosition(EModule Module, float X, float Y, EPositionMode PositionMode)
	{
		EnsureRuntimeLayouts();
		if(IsLegacyConfigModule(Module))
		{
			SModuleLayout Layout = LegacyConfigLayout(Module);
			Layout.m_X = X;
			Layout.m_Y = Y;
			Layout.m_Mode = PositionMode;
			WriteLegacyConfigLayout(Module, Layout);
			return;
		}
		gs_aRuntimeModuleLayouts[Module].m_X = X;
		gs_aRuntimeModuleLayouts[Module].m_Y = Y;
		gs_aRuntimeModuleLayouts[Module].m_Mode = PositionMode;
	}

	void SetScale(EModule Module, int Scale)
	{
		EnsureRuntimeLayouts();
		if(IsLegacyConfigModule(Module))
		{
			SModuleLayout Layout = LegacyConfigLayout(Module);
			Layout.m_Scale = std::clamp(Scale, 25, 300);
			WriteLegacyConfigLayout(Module, Layout);
			return;
		}
		gs_aRuntimeModuleLayouts[Module].m_Scale = std::clamp(Scale, 25, 300);
	}

	void SetEnabled(EModule Module, bool Enabled)
	{
		EnsureRuntimeLayouts();
		if(IsLegacyConfigModule(Module))
		{
			SModuleLayout Layout = LegacyConfigLayout(Module);
			Layout.m_Enabled = Enabled;
			WriteLegacyConfigLayout(Module, Layout);
			return;
		}
		gs_aRuntimeModuleLayouts[Module].m_Enabled = Enabled;
	}

	bool IsEnabled(EModule Module)
	{
		EnsureRuntimeLayouts();
		return gs_aRuntimeModuleLayouts[Module].m_Enabled;
	}

	void ResetPosition(EModule Module)
	{
		EnsureRuntimeLayouts();
		if(IsLegacyConfigModule(Module))
		{
			SModuleLayout Layout = LegacyConfigLayout(Module);
			Layout.m_X = gs_aModuleLayouts[Module].m_X;
			Layout.m_Y = gs_aModuleLayouts[Module].m_Y;
			Layout.m_Mode = gs_aModuleLayouts[Module].m_Mode;
			WriteLegacyConfigLayout(Module, Layout);
			return;
		}
		gs_aRuntimeModuleLayouts[Module].m_X = gs_aModuleLayouts[Module].m_X;
		gs_aRuntimeModuleLayouts[Module].m_Y = gs_aModuleLayouts[Module].m_Y;
		gs_aRuntimeModuleLayouts[Module].m_Mode = gs_aModuleLayouts[Module].m_Mode;
	}

	void ResetScale(EModule Module)
	{
		EnsureRuntimeLayouts();
		if(IsLegacyConfigModule(Module))
		{
			SModuleLayout Layout = LegacyConfigLayout(Module);
			Layout.m_Scale = gs_aModuleLayouts[Module].m_Scale;
			WriteLegacyConfigLayout(Module, Layout);
			return;
		}
		gs_aRuntimeModuleLayouts[Module].m_Scale = gs_aModuleLayouts[Module].m_Scale;
	}

	void ResetSettings(EModule Module)
	{
		EnsureRuntimeLayouts();
		if(IsLegacyConfigModule(Module))
		{
			WriteLegacyConfigLayout(Module, gs_aModuleLayouts[Module]);
			return;
		}
		gs_aRuntimeModuleLayouts[Module] = gs_aModuleLayouts[Module];
	}

	void ResetEditableModules()
	{
		for(int Module = 0; Module < MODULE_COUNT; ++Module)
		{
			if(IsEditableModule((EModule)Module))
				ResetSettings((EModule)Module);
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
		MigrateLegacyConfigLayouts();

		if(!gs_ConsoleCommandRegistered && pConsole)
		{
			pConsole->Register(
				"hud_layout_set",
				"i[module] f[x] f[y] i[scale] i[mode] i[background_enabled] i[background_color] ?i[enabled]",
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
