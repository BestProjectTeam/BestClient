/* Copyright © 2026 BestProject Team */
#include "hud_editor.h"

#include <base/color.h>
#include <base/math.h>
#include <base/str.h>

#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/bc_ui_animations.h>
#include <game/client/components/chat.h>
#include <game/client/components/voting.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float SNAP_THRESHOLD = 6.0f;
	constexpr float SETTINGS_POPUP_WIDTH = 210.0f;
	constexpr float SETTINGS_POPUP_HEIGHT = 138.0f;

	// Mirrors CUi::SPopupMenu::POPUP_BORDER + POPUP_MARGIN, which we can't reach directly
	// since that struct is a private implementation detail of CUi.
	constexpr float POPUP_FRAME_MARGIN = 5.0f;
	constexpr float POPUP_FRAME_ROUNDING = 3.0f;

	// Corner indices used for both the resize handles and CHudEditor::m_ResizeCorner.
	constexpr int RESIZE_CORNER_NONE = -1;
	constexpr int RESIZE_CORNER_TL = 0;
	constexpr int RESIZE_CORNER_TR = 1;
	constexpr int RESIZE_CORNER_BL = 2;
	constexpr int RESIZE_CORNER_BR = 3;
	// Quarter-circle "bracket" handles: a fixed decorative arc accent at each corner
	// (independent of the module's own corner rounding), not a dot or a square.
	constexpr float RESIZE_HANDLE_RADIUS = 4.2f;
	constexpr float RESIZE_HANDLE_THICKNESS = 1.8f;
	constexpr float RESIZE_HANDLE_OUTLINE = 0.7f;
	constexpr int RESIZE_HANDLE_ARC_SEGMENTS = 6;
	constexpr float RESIZE_HANDLE_HIT_RADIUS = 5.5f;
	constexpr float MIN_RESIZE_DIAGONAL = 12.0f;

	vec2 CornerPoint(const CUIRect &Rect, int Corner)
	{
		switch(Corner)
		{
		case RESIZE_CORNER_TL: return vec2(Rect.x, Rect.y);
		case RESIZE_CORNER_TR: return vec2(Rect.x + Rect.w, Rect.y);
		case RESIZE_CORNER_BL: return vec2(Rect.x, Rect.y + Rect.h);
		case RESIZE_CORNER_BR:
		default: return vec2(Rect.x + Rect.w, Rect.y + Rect.h);
		}
	}

	// Arc center + sweep for a corner's resize-handle bracket, at the given radius.
	// Angle convention matches DrawRoundedRectOutline()'s per-corner arcs below.
	void CornerArcParams(const CUIRect &Rect, int Corner, float Radius, vec2 &OutCenter, float &OutStartAngle, float &OutEndAngle)
	{
		switch(Corner)
		{
		case RESIZE_CORNER_TL:
			OutCenter = vec2(Rect.x + Radius, Rect.y + Radius);
			OutStartAngle = pi;
			OutEndAngle = 3.0f * pi / 2.0f;
			break;
		case RESIZE_CORNER_TR:
			OutCenter = vec2(Rect.x + Rect.w - Radius, Rect.y + Radius);
			OutStartAngle = -pi / 2.0f;
			OutEndAngle = 0.0f;
			break;
		case RESIZE_CORNER_BL:
			OutCenter = vec2(Rect.x + Radius, Rect.y + Rect.h - Radius);
			OutStartAngle = pi / 2.0f;
			OutEndAngle = pi;
			break;
		case RESIZE_CORNER_BR:
		default:
			OutCenter = vec2(Rect.x + Rect.w - Radius, Rect.y + Rect.h - Radius);
			OutStartAngle = 0.0f;
			OutEndAngle = pi / 2.0f;
			break;
		}
	}

	// Point used for hit-testing a corner's handle: the middle of its arc sweep.
	vec2 HandleCenterPoint(const CUIRect &Rect, int Corner)
	{
		vec2 Center;
		float StartAngle, EndAngle;
		CornerArcParams(Rect, Corner, RESIZE_HANDLE_RADIUS, Center, StartAngle, EndAngle);
		const float Mid = (StartAngle + EndAngle) * 0.5f;
		return Center + vec2(std::cos(Mid), std::sin(Mid)) * RESIZE_HANDLE_RADIUS;
	}

	// Maps a RESIZE_CORNER_* index to the matching IGraphics::CORNER_* bit, so handle
	// visibility can be checked against a module's Visual.m_Corners mask (see
	// HudLayout::BackgroundCorners(), which clears bits for edges flush against a
	// screen edge - handles at those corners are hidden the same way).
	int CornerBit(int Corner)
	{
		switch(Corner)
		{
		case RESIZE_CORNER_TL: return IGraphics::CORNER_TL;
		case RESIZE_CORNER_TR: return IGraphics::CORNER_TR;
		case RESIZE_CORNER_BL: return IGraphics::CORNER_BL;
		case RESIZE_CORNER_BR:
		default: return IGraphics::CORNER_BR;
		}
	}

	// Draws a filled ring segment (quarter-circle band) between two radii, the same
	// technique CUi::RenderProgressSpinner() uses for its filled arcs. Must be called
	// between Graphics()->QuadsBegin()/QuadsEnd().
	void DrawArcBand(IGraphics *pGraphics, vec2 Center, float StartAngle, float EndAngle, float InnerRadius, float OuterRadius, ColorRGBA Color)
	{
		if(Color.a <= 0.0f || OuterRadius <= InnerRadius)
			return;
		pGraphics->SetColor(Color);
		for(int i = 0; i < RESIZE_HANDLE_ARC_SEGMENTS; ++i)
		{
			const float Angle0 = mix(StartAngle, EndAngle, i / (float)RESIZE_HANDLE_ARC_SEGMENTS);
			const float Angle1 = mix(StartAngle, EndAngle, (i + 1) / (float)RESIZE_HANDLE_ARC_SEGMENTS);
			IGraphics::CFreeformItem Item(
				Center.x + std::cos(Angle0) * InnerRadius, Center.y + std::sin(Angle0) * InnerRadius,
				Center.x + std::cos(Angle1) * InnerRadius, Center.y + std::sin(Angle1) * InnerRadius,
				Center.x + std::cos(Angle0) * OuterRadius, Center.y + std::sin(Angle0) * OuterRadius,
				Center.x + std::cos(Angle1) * OuterRadius, Center.y + std::sin(Angle1) * OuterRadius);
			pGraphics->QuadsDrawFreeform(&Item, 1);
		}
	}

	// Small filled disc, used to cap the two ends of an arc band so it reads as a
	// rounded bracket instead of a strip with sharp, "cut off" ends. Must be called
	// between Graphics()->QuadsBegin()/QuadsEnd().
	void DrawFilledCircle(IGraphics *pGraphics, vec2 Center, float Radius, ColorRGBA Color)
	{
		if(Color.a <= 0.0f || Radius <= 0.0f)
			return;
		pGraphics->SetColor(Color);
		constexpr int Segments = 6;
		for(int i = 0; i < Segments; ++i)
		{
			const float Angle0 = 2.0f * pi * i / (float)Segments;
			const float Angle1 = 2.0f * pi * (i + 1) / (float)Segments;
			IGraphics::CFreeformItem Item(
				Center.x, Center.y,
				Center.x, Center.y,
				Center.x + std::cos(Angle0) * Radius, Center.y + std::sin(Angle0) * Radius,
				Center.x + std::cos(Angle1) * Radius, Center.y + std::sin(Angle1) * Radius);
			pGraphics->QuadsDrawFreeform(&Item, 1);
		}
	}

	// The full resize-handle bracket: an arc band with its two ends capped by a
	// rounded disc, so it doesn't look like a strip that's been cut off (see
	// DrawArcBand()/DrawFilledCircle()). Must be called between QuadsBegin()/QuadsEnd().
	void DrawHandleBracket(IGraphics *pGraphics, vec2 Center, float StartAngle, float EndAngle, float InnerRadius, float OuterRadius, ColorRGBA Color)
	{
		DrawArcBand(pGraphics, Center, StartAngle, EndAngle, InnerRadius, OuterRadius, Color);
		const float MidRadius = (InnerRadius + OuterRadius) * 0.5f;
		const float CapRadius = (OuterRadius - InnerRadius) * 0.5f;
		DrawFilledCircle(pGraphics, Center + vec2(std::cos(StartAngle), std::sin(StartAngle)) * MidRadius, CapRadius, Color);
		DrawFilledCircle(pGraphics, Center + vec2(std::cos(EndAngle), std::sin(EndAngle)) * MidRadius, CapRadius, Color);
	}

	// Growth animation used by both the opening popup (PopupModuleSettings) and the
	// closing ghost frame (CHudEditor::RenderClosingPopupFrame), so they stay visually
	// identical when played forwards vs. backwards.
	CUIRect ComputeAnimRect(const CUIRect &OuterRect, bool GrowFromRight, float Phase)
	{
		CUIRect AnimRect;
		AnimRect.w = OuterRect.w * Phase;
		AnimRect.h = OuterRect.h * Phase;
		AnimRect.y = OuterRect.y;
		AnimRect.x = GrowFromRight ? (OuterRect.x + OuterRect.w - AnimRect.w) : OuterRect.x;
		return AnimRect;
	}

	void DrawPopupFrame(const CUIRect &AnimRect, float Phase)
	{
		AnimRect.Draw(ColorRGBA(0.5f, 0.5f, 0.5f, 0.75f * Phase), IGraphics::CORNER_ALL, POPUP_FRAME_ROUNDING);
		CUIRect InnerAnimRect;
		AnimRect.Margin(1.0f, &InnerAnimRect);
		InnerAnimRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.75f * Phase), IGraphics::CORNER_ALL, POPUP_FRAME_ROUNDING);
	}

	// Kinds accepted by CHudEditor::StartResetAnimation().
	constexpr int RESET_KIND_POSITION = 1;
	constexpr int RESET_KIND_SCALE = 2;
	constexpr int RESET_KIND_ALL = 3;
	constexpr int RESET_ANIM_DURATION_MS = 260;

	CUIRect LerpRect(const CUIRect &A, const CUIRect &B, float T)
	{
		CUIRect Result;
		Result.x = mix(A.x, B.x, T);
		Result.y = mix(A.y, B.y, T);
		Result.w = mix(A.w, B.w, T);
		Result.h = mix(A.h, B.h, T);
		return Result;
	}

	// Modules the editor currently knows how to draw a drag handle for and render
	// a live preview of. Add a case here plus a GetModuleVisual() branch and one
	// CollectModuleVisuals() line to wire up a new module.
	bool IsEditorModule(HudLayout::EModule Module)
	{
		return Module == HudLayout::MODULE_SCORE ||
		       Module == HudLayout::MODULE_KEYSTROKES_KEYBOARD ||
		       Module == HudLayout::MODULE_KEYSTROKES_MOUSE ||
		       Module == HudLayout::MODULE_SPECTATOR_COUNT ||
		       Module == HudLayout::MODULE_MOVEMENT_INFO ||
		       Module == HudLayout::MODULE_DUMMY_ACTIONS ||
		       Module == HudLayout::MODULE_CHAT ||
		       Module == HudLayout::MODULE_VOTES ||
		       Module == HudLayout::MODULE_LOCAL_TIME ||
		       Module == HudLayout::MODULE_FROZEN_HUD ||
		       Module == HudLayout::MODULE_NOTIFY_LAST ||
		       Module == HudLayout::MODULE_FINISH_PREDICTION ||
		       Module == HudLayout::MODULE_VOICE_TALKERS ||
		       Module == HudLayout::MODULE_VOICE_STATUS ||
		       Module == HudLayout::MODULE_MUSIC_PLAYER;
	}

	bool PointInRect(vec2 Point, const CUIRect &Rect)
	{
		return Point.x >= Rect.x && Point.x <= Rect.x + Rect.w &&
		       Point.y >= Rect.y && Point.y <= Rect.y + Rect.h;
	}

	void DrawRoundedRectOutline(IGraphics *pGraphics, const CUIRect &Rect, int Corners, float Rounding, ColorRGBA Color)
	{
		if(Rect.w <= 0.0f || Rect.h <= 0.0f || Color.a <= 0.0f)
			return;

		const float Radius = std::clamp(Rounding, 0.0f, minimum(Rect.w, Rect.h) * 0.5f);
		if(Radius <= 0.01f || Corners == IGraphics::CORNER_NONE)
		{
			Rect.DrawOutline(Color);
			return;
		}

		constexpr int SegmentsPerCorner = 8;
		IGraphics::CLineItem aLines[SegmentsPerCorner * 4 + 4];
		int NumLines = 0;

		auto AddLine = [&](vec2 From, vec2 To) {
			aLines[NumLines++] = IGraphics::CLineItem(From, To);
		};

		auto AddArc = [&](vec2 Center, float StartAngle, float EndAngle) {
			vec2 Prev = vec2(
				Center.x + std::cos(StartAngle) * Radius,
				Center.y + std::sin(StartAngle) * Radius);
			for(int i = 1; i <= SegmentsPerCorner; ++i)
			{
				const float T = i / (float)SegmentsPerCorner;
				const float Angle = mix(StartAngle, EndAngle, T);
				const vec2 Cur(
					Center.x + std::cos(Angle) * Radius,
					Center.y + std::sin(Angle) * Radius);
				AddLine(Prev, Cur);
				Prev = Cur;
			}
		};

		const bool TopLeftRounded = (Corners & IGraphics::CORNER_TL) != 0;
		const bool TopRightRounded = (Corners & IGraphics::CORNER_TR) != 0;
		const bool BottomLeftRounded = (Corners & IGraphics::CORNER_BL) != 0;
		const bool BottomRightRounded = (Corners & IGraphics::CORNER_BR) != 0;
		const float Left = Rect.x;
		const float Right = Rect.x + Rect.w;
		const float Top = Rect.y;
		const float Bottom = Rect.y + Rect.h;

		AddLine(
			vec2(Left + (TopLeftRounded ? Radius : 0.0f), Top),
			vec2(Right - (TopRightRounded ? Radius : 0.0f), Top));
		if(TopRightRounded)
			AddArc(vec2(Right - Radius, Top + Radius), -pi / 2.0f, 0.0f);

		AddLine(
			vec2(Right, Top + (TopRightRounded ? Radius : 0.0f)),
			vec2(Right, Bottom - (BottomRightRounded ? Radius : 0.0f)));
		if(BottomRightRounded)
			AddArc(vec2(Right - Radius, Bottom - Radius), 0.0f, pi / 2.0f);

		AddLine(
			vec2(Right - (BottomRightRounded ? Radius : 0.0f), Bottom),
			vec2(Left + (BottomLeftRounded ? Radius : 0.0f), Bottom));
		if(BottomLeftRounded)
			AddArc(vec2(Left + Radius, Bottom - Radius), pi / 2.0f, pi);

		AddLine(
			vec2(Left, Bottom - (BottomLeftRounded ? Radius : 0.0f)),
			vec2(Left, Top + (TopLeftRounded ? Radius : 0.0f)));
		if(TopLeftRounded)
			AddArc(vec2(Left + Radius, Top + Radius), pi, 3.0f * pi / 2.0f);

		pGraphics->TextureClear();
		pGraphics->LinesBegin();
		pGraphics->SetColor(Color);
		pGraphics->LinesDraw(aLines, NumLines);
		pGraphics->LinesEnd();
	}

	CUIRect ClampToBounds(CUIRect Rect, float Width, float Height)
	{
		Rect.x = std::clamp(Rect.x, 0.0f, maximum(0.0f, Width - Rect.w));
		Rect.y = std::clamp(Rect.y, 0.0f, maximum(0.0f, Height - Rect.h));
		return Rect;
	}

	float ChatInputBottomExtra(const CChat &Chat)
	{
		const float ScaledFontSize = Chat.FontSize() * (8.0f / 6.0f);
		return maximum(2.25f * ScaledFontSize, maximum(ScaledFontSize + 4.0f, 16.0f));
	}

	bool UsesDynamicBottomRightAnchor(HudLayout::EModule Module)
	{
		return Module == HudLayout::MODULE_MOVEMENT_INFO ||
		       Module == HudLayout::MODULE_SPECTATOR_COUNT ||
		       Module == HudLayout::MODULE_DUMMY_ACTIONS;
	}
} // namespace

void CHudEditor::OnConsoleInit()
{
	Storage()->CreateFolder("BestClient", IStorage::TYPE_SAVE);
	HudLayout::OnConsoleInit(Console(), ConfigManager());
}

void CHudEditor::Activate()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	m_Active = true;
	m_MouseDownLast = false;
	m_RightMouseDownLast = false;
	m_Dragging = false;
	m_PressedModule = HudLayout::MODULE_COUNT;
	m_HoveredModule = HudLayout::MODULE_COUNT;
	m_SelectedModule = HudLayout::MODULE_COUNT;
	m_ResizingModule = HudLayout::MODULE_COUNT;
	m_ResizeCorner = -1;
	m_PopupRevealPhase = 0.0f;
	m_SuppressCloseAnim = false;
	m_PopupClosing = false;
	m_PopupClosePhase = 0.0f;
	m_ResetAnimCount = 0;
	m_PressedOnReset = false;
	Ui()->ClosePopupMenus();

	const float CanvasScale = HudLayout::CANVAS_WIDTH / maximum(HudWidth(), 1.0f);
	const HudLayout::EModule aDynamicModules[] = {
		HudLayout::MODULE_MOVEMENT_INFO,
		HudLayout::MODULE_SPECTATOR_COUNT,
		HudLayout::MODULE_DUMMY_ACTIONS,
	};
	for(const HudLayout::EModule Module : aDynamicModules)
	{
		const auto Layout = HudLayout::Get(Module, HudWidth(), HudHeight());
		if(!HudLayout::HasPositionOverride(Module) || Layout.m_Mode != HudLayout::POSITION_MODE_TOP_LEFT)
			continue;

		const CUIRect Rect = GetModuleVisual(Module).m_Rect;
		if(Rect.w <= 0.0f || Rect.h <= 0.0f)
			continue;
		HudLayout::SetPosition(Module, (Rect.x + Rect.w) * CanvasScale, Rect.y + Rect.h, HudLayout::POSITION_MODE_BOTTOM_RIGHT);
	}
}

void CHudEditor::Deactivate()
{
	m_Active = false;
	m_MouseDownLast = false;
	m_RightMouseDownLast = false;
	m_Dragging = false;
	m_PressedModule = HudLayout::MODULE_COUNT;
	m_HoveredModule = HudLayout::MODULE_COUNT;
	m_SelectedModule = HudLayout::MODULE_COUNT;
	m_ResizingModule = HudLayout::MODULE_COUNT;
	m_ResizeCorner = -1;
	m_PopupRevealPhase = 0.0f;
	m_SuppressCloseAnim = false;
	m_PopupClosing = false;
	m_PopupClosePhase = 0.0f;
	m_ResetAnimCount = 0;
	m_PressedOnReset = false;
	Ui()->ClosePopupMenus();
}

void CHudEditor::OnStateChange(int NewState, int OldState)
{
	(void)OldState;
	if(NewState != IClient::STATE_ONLINE && NewState != IClient::STATE_DEMOPLAYBACK)
		Deactivate();
}

bool CHudEditor::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!m_Active)
		return false;

	Ui()->ConvertMouseMove(&x, &y, CursorType);
	Ui()->OnCursorMove(x, y);
	return true;
}

bool CHudEditor::OnInput(const IInput::CEvent &Event)
{
	if(!m_Active)
		return false;
	if((Event.m_Flags & IInput::FLAG_PRESS) != 0 && Event.m_Key == KEY_ESCAPE)
	{
		Deactivate();
		return true;
	}
	return true;
}

float CHudEditor::HudWidth() const
{
	return HudLayout::CANVAS_HEIGHT * Graphics()->ScreenAspect();
}

float CHudEditor::HudHeight() const
{
	return HudLayout::CANVAS_HEIGHT;
}

bool CHudEditor::IsEditableModule(HudLayout::EModule Module) const
{
	return IsEditorModule(Module) && HudLayout::IsEditableModule(Module);
}

bool CHudEditor::IsModuleEnabled(HudLayout::EModule Module) const
{
	return HudLayout::IsEnabled(Module);
}

CUIRect CHudEditor::GetFallbackModuleRect(HudLayout::EModule Module) const
{
	const float Width = HudWidth();
	const float Height = HudHeight();
	const auto Layout = HudLayout::Get(Module, Width, Height);
	CUIRect Rect{};

	switch(Module)
	{
	case HudLayout::MODULE_GAME_TIMER:
		Rect = {Layout.m_X, Layout.m_Y, 64.0f, 12.0f};
		break;
	case HudLayout::MODULE_FPS:
		Rect = {Layout.m_X, Layout.m_Y, 26.0f, 9.0f};
		break;
	case HudLayout::MODULE_PING:
		Rect = {Layout.m_X, Layout.m_Y, 26.0f, 9.0f};
		break;
	case HudLayout::MODULE_HOOK_COMBO:
	{
		const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
		const float FontSize = 13.0f * Scale;
		const float BoxWidth = TextRender()->TextWidth(FontSize, "fantastic (x7)", -1, -1.0f) + 8.0f * Scale;
		const float BoxHeight = FontSize + 4.0f * Scale;
		Rect = {Layout.m_X, Layout.m_Y, BoxWidth, BoxHeight};
		break;
	}
	case HudLayout::MODULE_MINI_VOTE:
		Rect = {Layout.m_X, Layout.m_Y, 70.0f, 35.0f};
		break;
	case HudLayout::MODULE_NOTIFY_LAST:
		Rect = {Layout.m_X, Layout.m_Y, 185.0f, 16.0f};
		break;
	case HudLayout::MODULE_LOCK_CAM:
		Rect = {Layout.m_X, Layout.m_Y, 16.0f, 16.0f};
		break;
	case HudLayout::MODULE_KILLFEED:
		Rect = {Layout.m_X, Layout.m_Y, 155.0f, 70.0f};
		break;
	default:
		Rect = {Layout.m_X, Layout.m_Y, 78.0f, 18.0f};
		break;
	}

	return ClampToBounds(Rect, Width, Height);
}

CHudEditor::SModuleVisual CHudEditor::GetModuleVisual(HudLayout::EModule Module) const
{
	SModuleVisual Visual;
	Visual.m_Module = Module;
	Visual.m_Editable = IsEditableModule(Module);
	Visual.m_Enabled = IsModuleEnabled(Module);
	Visual.m_IsFallbackPreview = false;

	const float Width = HudWidth();
	const float Height = HudHeight();

	switch(Module)
	{
	case HudLayout::MODULE_CHAT:
		Visual.m_Rect = GameClient()->m_Chat.GetHudRect(Width, Height, true);
		Visual.m_Rounding = 6.0f;
		Visual.m_UsesBottomAnchor = true;
		break;
	case HudLayout::MODULE_VOTES:
		Visual.m_Rect = GameClient()->m_Voting.GetHudRect(Width, Height, true);
		Visual.m_Rounding = 3.0f;
		break;
	case HudLayout::MODULE_SCORE:
		Visual.m_Rect = GameClient()->m_Hud.GetScoreHudEditorRect();
		Visual.m_Rounding = 5.0f;
		break;
	case HudLayout::MODULE_KEYSTROKES_KEYBOARD:
		Visual.m_Rect = GameClient()->m_Hud.GetKeystrokesKeyboardHudEditorRect();
		Visual.m_Rounding = 4.0f;
		break;
	case HudLayout::MODULE_KEYSTROKES_MOUSE:
		Visual.m_Rect = GameClient()->m_Hud.GetKeystrokesMouseHudEditorRect();
		Visual.m_Rounding = 4.0f;
		break;
	case HudLayout::MODULE_SPECTATOR_COUNT:
		Visual.m_Rect = GameClient()->m_Hud.GetSpectatorCountHudEditorRect();
		Visual.m_Rounding = 5.0f;
		break;
	case HudLayout::MODULE_DUMMY_ACTIONS:
		Visual.m_Rect = GameClient()->m_Hud.GetDummyActionsHudEditorRect();
		Visual.m_Rounding = 5.0f;
		break;
	case HudLayout::MODULE_MOVEMENT_INFO:
		Visual.m_Rect = GameClient()->m_Hud.GetMovementInformationHudEditorRect();
		Visual.m_Rounding = 5.0f;
		break;
	case HudLayout::MODULE_LOCAL_TIME:
		Visual.m_Rect = GameClient()->m_Hud.GetLocalTimeHudEditorRect();
		Visual.m_Rounding = 3.75f;
		break;
	case HudLayout::MODULE_FROZEN_HUD:
		Visual.m_Rect = GameClient()->m_Hud.GetFrozenHudEditorRect();
		Visual.m_Rounding = 5.0f;
		break;
	case HudLayout::MODULE_NOTIFY_LAST:
		Visual.m_Rect = GameClient()->m_Hud.GetNotifyLastHudEditorRect();
		Visual.m_Rounding = 2.0f;
		break;
	case HudLayout::MODULE_FINISH_PREDICTION:
		Visual.m_Rect = GameClient()->m_Hud.GetFinishPredictionHudEditorRect();
		Visual.m_Rounding = 5.0f;
		break;
	case HudLayout::MODULE_MUSIC_PLAYER:
	{
		Visual.m_Rect = GameClient()->m_MusicPlayer.GetHudEditorRect(false);
		if(Visual.m_Rect.w <= 0.0f || Visual.m_Rect.h <= 0.0f)
			Visual.m_Rect = GameClient()->m_MusicPlayer.GetHudEditorRect(true);
		const auto MusicPlayerLayout = HudLayout::Get(HudLayout::MODULE_MUSIC_PLAYER, Width, Height);
		const float MusicPlayerScale = std::clamp(MusicPlayerLayout.m_Scale / 100.0f, 0.25f, 3.0f);
		Visual.m_Rounding = minimum(5.0f * MusicPlayerScale, Visual.m_Rect.h * 0.24f);
		break;
	}
	case HudLayout::MODULE_VOICE_TALKERS:
		Visual.m_Rect = GameClient()->m_VoiceChat.GetHudTalkingIndicatorRect(Width, Height, true);
		Visual.m_Rounding = 3.1f;
		break;
	case HudLayout::MODULE_VOICE_STATUS:
		Visual.m_Rect = GameClient()->m_VoiceChat.GetHudMuteStatusIndicatorRect(Width, Height, true);
		Visual.m_Rounding = 2.3f;
		break;
	default:
		Visual.m_Rect = GetFallbackModuleRect(Module);
		Visual.m_Rounding = 4.0f;
		Visual.m_IsFallbackPreview = true;
		break;
	}

	if(Visual.m_Rect.w <= 0.0f || Visual.m_Rect.h <= 0.0f)
	{
		Visual.m_Rect = GetFallbackModuleRect(Module);
		Visual.m_Rounding = 4.0f;
		Visual.m_IsFallbackPreview = true;
	}

	Visual.m_Rounding = std::clamp(Visual.m_Rounding, 2.0f, 12.0f);
	Visual.m_Rect = ClampToBounds(Visual.m_Rect, Width, Height);
	Visual.m_Corners = HudLayout::BackgroundCorners(IGraphics::CORNER_ALL, Visual.m_Rect.x, Visual.m_Rect.y, Visual.m_Rect.w, Visual.m_Rect.h, Width, Height);
	return Visual;
}

void CHudEditor::CollectModuleVisuals(SModuleVisual *pOut, int &Count) const
{
	Count = 0;

	auto AddModule = [&](HudLayout::EModule Module) {
		if(Count >= MAX_MODULE_VISUALS)
			return;
		pOut[Count++] = GetModuleVisual(Module);
	};

	// Keep chat at the very bottom so overlapping HUD modules remain easy to select and drag.
	AddModule(HudLayout::MODULE_CHAT);
	AddModule(HudLayout::MODULE_SCORE);
	AddModule(HudLayout::MODULE_KEYSTROKES_KEYBOARD);
	if(g_Config.m_BcKeystrokesStyle != 1)
		AddModule(HudLayout::MODULE_KEYSTROKES_MOUSE);
	AddModule(HudLayout::MODULE_SPECTATOR_COUNT);
	AddModule(HudLayout::MODULE_DUMMY_ACTIONS);
	AddModule(HudLayout::MODULE_MOVEMENT_INFO);
	AddModule(HudLayout::MODULE_VOTES);
	AddModule(HudLayout::MODULE_LOCAL_TIME);
	AddModule(HudLayout::MODULE_FROZEN_HUD);
	AddModule(HudLayout::MODULE_NOTIFY_LAST);
	AddModule(HudLayout::MODULE_FINISH_PREDICTION);
	AddModule(HudLayout::MODULE_MUSIC_PLAYER);
	AddModule(HudLayout::MODULE_VOICE_TALKERS);
	AddModule(HudLayout::MODULE_VOICE_STATUS);
}

HudLayout::EModule CHudEditor::HitTestModule(vec2 MousePos) const
{
	SModuleVisual aVisuals[MAX_MODULE_VISUALS];
	int Count = 0;
	CollectModuleVisuals(aVisuals, Count);

	// Editable modules should always win hit-tests over locked preview modules.
	for(int i = Count - 1; i >= 0; --i)
	{
		if(!aVisuals[i].m_Editable)
			continue;
		const CUIRect &Rect = aVisuals[i].m_Rect;
		if(PointInRect(MousePos, Rect))
			return aVisuals[i].m_Module;
	}

	for(int i = Count - 1; i >= 0; --i)
	{
		const CUIRect &Rect = aVisuals[i].m_Rect;
		if(PointInRect(MousePos, Rect))
			return aVisuals[i].m_Module;
	}
	return HudLayout::MODULE_COUNT;
}

HudLayout::EModule CHudEditor::HitTestResizeHandle(vec2 MousePos, int &OutCorner) const
{
	OutCorner = RESIZE_CORNER_NONE;

	// Resize handles are only hot on the module that currently shows them, i.e. the
	// hovered and/or selected module (see the RenderModuleOutline/RenderResizeHandles
	// call site in RenderOverlay), so we never fight another module's handles for input.
	auto TryModule = [&](HudLayout::EModule Module) {
		if(Module == HudLayout::MODULE_COUNT || !IsEditableModule(Module))
			return false;
		const SModuleVisual Visual = GetModuleVisual(Module);
		static constexpr int aCorners[4] = {RESIZE_CORNER_TL, RESIZE_CORNER_TR, RESIZE_CORNER_BL, RESIZE_CORNER_BR};
		for(int Corner : aCorners)
		{
			// Flush against a screen edge - see RenderResizeHandles(), no bracket is
			// drawn there so it shouldn't be grabbable either.
			if((Visual.m_Corners & CornerBit(Corner)) == 0)
				continue;
			if(distance(MousePos, HandleCenterPoint(Visual.m_Rect, Corner)) <= RESIZE_HANDLE_HIT_RADIUS)
			{
				OutCorner = Corner;
				return true;
			}
		}
		return false;
	};

	if(TryModule(m_SelectedModule))
		return m_SelectedModule;
	if(m_HoveredModule != m_SelectedModule && TryModule(m_HoveredModule))
		return m_HoveredModule;
	return HudLayout::MODULE_COUNT;
}

void CHudEditor::ApplyDraggedPosition(HudLayout::EModule Module, const CUIRect &Rect)
{
	if(!IsEditableModule(Module))
		return;

	const float CanvasScale = HudLayout::CANVAS_WIDTH / maximum(HudWidth(), 1.0f);
	if(UsesDynamicBottomRightAnchor(Module))
	{
		HudLayout::SetPosition(Module, (Rect.x + Rect.w) * CanvasScale, Rect.y + Rect.h, HudLayout::POSITION_MODE_BOTTOM_RIGHT);
	}
	else if(Module == HudLayout::MODULE_CHAT)
	{
		const float BottomAnchor = Rect.y + Rect.h - ChatInputBottomExtra(GameClient()->m_Chat);
		HudLayout::SetPosition(Module, Rect.x * CanvasScale, BottomAnchor);
	}
	else if(Module == HudLayout::MODULE_LOCAL_TIME)
	{
		const auto Layout = HudLayout::Get(HudLayout::MODULE_LOCAL_TIME, HudWidth(), HudHeight());
		const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
		const float Padding = 5.0f * Scale;
		const float AnchorX = (Rect.x + Rect.w + Padding) * CanvasScale;
		HudLayout::SetPosition(Module, AnchorX, Rect.y);
	}
	else if(Module == HudLayout::MODULE_FROZEN_HUD)
	{
		const auto Layout = HudLayout::Get(Module, HudWidth(), HudHeight());
		const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
		const float TeeSize = g_Config.m_TcFrozenHudTeeSize * Scale;
		float AnchorX = Rect.x + TeeSize * 0.5f;
		if(g_Config.m_TcFrozenHudExpandDir == 1)
			AnchorX = Rect.x + Rect.w - TeeSize * 0.5f;
		else if(g_Config.m_TcFrozenHudExpandDir == 2)
			AnchorX = Rect.x + Rect.w * 0.5f;
		HudLayout::SetPosition(Module, AnchorX * CanvasScale, Rect.y);
	}
	else if(Module == HudLayout::MODULE_MUSIC_PLAYER)
	{
		const auto Layout = HudLayout::Get(HudLayout::MODULE_MUSIC_PLAYER, HudWidth(), HudHeight());
		const CUIRect CurrentRect = GameClient()->m_MusicPlayer.GetHudEditorRect(true);
		const float AnchorOffsetX = CurrentRect.w > 0.0f ? (CurrentRect.x - Layout.m_X) : 0.0f;
		const float BaseX = Rect.x - AnchorOffsetX;
		HudLayout::SetPosition(Module, BaseX * CanvasScale, Rect.y);
	}
	else
		HudLayout::SetPosition(Module, Rect.x * CanvasScale, Rect.y);
}

CUIRect CHudEditor::SnapRect(const CUIRect &Rect, HudLayout::EModule DraggedModule) const
{
	CUIRect Result = Rect;
	SModuleVisual aVisuals[MAX_MODULE_VISUALS];
	int Count = 0;
	CollectModuleVisuals(aVisuals, Count);

	auto TrySnap = [](float Candidate, float Target, float &BestDelta) {
		const float Delta = Target - Candidate;
		if(absolute(Delta) <= SNAP_THRESHOLD && absolute(Delta) < absolute(BestDelta))
			BestDelta = Delta;
	};

	float BestDeltaX = SNAP_THRESHOLD + 1.0f;
	float BestDeltaY = SNAP_THRESHOLD + 1.0f;
	const float Width = HudWidth();
	const float Height = HudHeight();

	TrySnap(Result.x, 0.0f, BestDeltaX);
	TrySnap(Result.x + Result.w, Width, BestDeltaX);
	TrySnap(Result.x + Result.w * 0.5f, Width * 0.5f, BestDeltaX);
	TrySnap(Result.y, 0.0f, BestDeltaY);
	TrySnap(Result.y + Result.h, Height, BestDeltaY);
	TrySnap(Result.y + Result.h * 0.5f, Height * 0.5f, BestDeltaY);

	for(int i = 0; i < Count; ++i)
	{
		if(aVisuals[i].m_Module == DraggedModule)
			continue;
		const CUIRect &Other = aVisuals[i].m_Rect;
		TrySnap(Result.x, Other.x, BestDeltaX);
		TrySnap(Result.x + Result.w, Other.x + Other.w, BestDeltaX);
		TrySnap(Result.x, Other.x + Other.w, BestDeltaX);
		TrySnap(Result.x + Result.w, Other.x, BestDeltaX);
		TrySnap(Result.x + Result.w * 0.5f, Other.x + Other.w * 0.5f, BestDeltaX);
		TrySnap(Result.y, Other.y, BestDeltaY);
		TrySnap(Result.y + Result.h, Other.y + Other.h, BestDeltaY);
		TrySnap(Result.y, Other.y + Other.h, BestDeltaY);
		TrySnap(Result.y + Result.h, Other.y, BestDeltaY);
		TrySnap(Result.y + Result.h * 0.5f, Other.y + Other.h * 0.5f, BestDeltaY);
	}

	if(absolute(BestDeltaX) <= SNAP_THRESHOLD)
		Result.x += BestDeltaX;
	if(absolute(BestDeltaY) <= SNAP_THRESHOLD)
		Result.y += BestDeltaY;

	return ClampToBounds(Result, Width, Height);
}

void CHudEditor::UpdateDragging(vec2 MousePos)
{
	if(!m_Dragging || m_PressedModule == HudLayout::MODULE_COUNT || !IsEditableModule(m_PressedModule))
		return;
	SModuleVisual Visual = GetModuleVisual(m_PressedModule);
	CUIRect NewRect = Visual.m_Rect;
	NewRect.x = MousePos.x - m_DragMouseOffset.x;
	NewRect.y = MousePos.y - m_DragMouseOffset.y;
	if(!Input()->ShiftIsPressed())
		NewRect = SnapRect(NewRect, m_PressedModule);
	else
		NewRect = ClampToBounds(NewRect, HudWidth(), HudHeight());
	ApplyDraggedPosition(m_PressedModule, NewRect);
}

void CHudEditor::UpdateResizing(vec2 MousePos)
{
	if(m_ResizingModule == HudLayout::MODULE_COUNT || m_ResizeCorner == RESIZE_CORNER_NONE || !IsEditableModule(m_ResizingModule))
		return;

	// The corner opposite the one being dragged stays fixed in HUD space; the dragged
	// corner follows the mouse. Since modules only expose a single uniform Scale (no
	// independent width/height), we derive the new scale from how far the dragged
	// corner's distance to that fixed anchor changed, keeping the module's aspect ratio.
	const CUIRect &Anchor = m_ResizeAnchorRect;
	vec2 FixedPoint;
	switch(m_ResizeCorner)
	{
	case RESIZE_CORNER_TL: FixedPoint = vec2(Anchor.x + Anchor.w, Anchor.y + Anchor.h); break;
	case RESIZE_CORNER_TR: FixedPoint = vec2(Anchor.x, Anchor.y + Anchor.h); break;
	case RESIZE_CORNER_BL: FixedPoint = vec2(Anchor.x + Anchor.w, Anchor.y); break;
	case RESIZE_CORNER_BR:
	default: FixedPoint = vec2(Anchor.x, Anchor.y); break;
	}

	const float StartDiagonal = distance(FixedPoint, CornerPoint(Anchor, m_ResizeCorner));
	if(StartDiagonal < MIN_RESIZE_DIAGONAL)
		return;

	const vec2 ClampedMouse(
		std::clamp(MousePos.x, 0.0f, HudWidth()),
		std::clamp(MousePos.y, 0.0f, HudHeight()));
	const float NewDiagonal = distance(FixedPoint, ClampedMouse);
	const int NewScale = std::clamp((int)std::lround(m_ResizeStartScale * (NewDiagonal / StartDiagonal)), 25, 300);
	const float Factor = (float)NewScale / (float)m_ResizeStartScale;

	CUIRect NewRect;
	NewRect.w = Anchor.w * Factor;
	NewRect.h = Anchor.h * Factor;
	switch(m_ResizeCorner)
	{
	case RESIZE_CORNER_TL:
		NewRect.x = FixedPoint.x - NewRect.w;
		NewRect.y = FixedPoint.y - NewRect.h;
		break;
	case RESIZE_CORNER_TR:
		NewRect.x = FixedPoint.x;
		NewRect.y = FixedPoint.y - NewRect.h;
		break;
	case RESIZE_CORNER_BL:
		NewRect.x = FixedPoint.x - NewRect.w;
		NewRect.y = FixedPoint.y;
		break;
	case RESIZE_CORNER_BR:
	default:
		NewRect.x = FixedPoint.x;
		NewRect.y = FixedPoint.y;
		break;
	}
	NewRect = ClampToBounds(NewRect, HudWidth(), HudHeight());

	HudLayout::SetScale(m_ResizingModule, NewScale);
	ApplyDraggedPosition(m_ResizingModule, NewRect);
}

CHudEditor::SResetAnim *CHudEditor::FindOrAddResetAnim(HudLayout::EModule Module)
{
	for(int i = 0; i < m_ResetAnimCount; ++i)
	{
		if(m_aResetAnims[i].m_Module == Module)
			return &m_aResetAnims[i];
	}
	if(m_ResetAnimCount >= MAX_MODULE_VISUALS)
		return nullptr;
	SResetAnim &NewAnim = m_aResetAnims[m_ResetAnimCount++];
	NewAnim = SResetAnim{};
	NewAnim.m_Module = Module;
	return &NewAnim;
}

void CHudEditor::CancelResetAnimation(HudLayout::EModule Module)
{
	for(int i = 0; i < m_ResetAnimCount; ++i)
	{
		if(m_aResetAnims[i].m_Module == Module)
		{
			m_aResetAnims[i] = m_aResetAnims[m_ResetAnimCount - 1];
			--m_ResetAnimCount;
			return;
		}
	}
}

void CHudEditor::StartResetAnimation(HudLayout::EModule Module, int Kind)
{
	if(!IsEditableModule(Module))
		return;

	// Reuses the module's existing entry (restarting it from its current, possibly
	// mid-flight, state) if one was already running - each module animates
	// independently, so this never touches any other module's entry (see the top-left
	// "Reset All" button in OnRender(), which starts one of these per editable module).
	SResetAnim *pAnim = FindOrAddResetAnim(Module);
	if(!pAnim)
		return;

	pAnim->m_StartRect = GetModuleVisual(Module).m_Rect;
	pAnim->m_StartScale = HudLayout::Get(Module, HudWidth(), HudHeight()).m_Scale;
	pAnim->m_Kind = Kind;

	// Apply the reset immediately so we can read the resulting target rect/scale, then
	// roll position+scale back to where they were - UpdateResetAnimation() drives them
	// back to this (already-applied) target smoothly every frame instead of snapping.
	// Enabled/background (only touched by RESET_KIND_ALL) are left applied as-is, since
	// toggling those isn't something that can be meaningfully interpolated.
	if(Kind == RESET_KIND_SCALE)
		HudLayout::ResetScale(Module);
	else if(Kind == RESET_KIND_POSITION)
		HudLayout::ResetPosition(Module);
	else
		HudLayout::ResetSettings(Module);

	pAnim->m_TargetRect = Module == HudLayout::MODULE_MUSIC_PLAYER ?
		ClampToBounds(GameClient()->m_MusicPlayer.GetHudEditorRect(true), HudWidth(), HudHeight()) :
		GetModuleVisual(Module).m_Rect;
	pAnim->m_TargetScale = HudLayout::Get(Module, HudWidth(), HudHeight()).m_Scale;

	HudLayout::SetScale(Module, pAnim->m_StartScale);
	ApplyDraggedPosition(Module, pAnim->m_StartRect);

	pAnim->m_Phase = 0.0f;
}

void CHudEditor::UpdateResetAnimation()
{
	if(m_ResetAnimCount == 0)
		return;

	const bool AnimEnabled = BCUiAnimations::Enabled() && g_Config.m_BcModuleUiRevealAnimation != 0;
	const float Dt = Client()->RenderFrameTime();
	for(int i = 0; i < m_ResetAnimCount; ++i)
	{
		SResetAnim &Anim = m_aResetAnims[i];
		if(AnimEnabled)
			BCUiAnimations::UpdatePhase(Anim.m_Phase, 1.0f, Dt, BCUiAnimations::MsToSeconds(RESET_ANIM_DURATION_MS));
		else
			Anim.m_Phase = 1.0f;

		const float Eased = BCUiAnimations::EaseOutCubic(Anim.m_Phase);
		const CUIRect CurRect = LerpRect(Anim.m_StartRect, Anim.m_TargetRect, Eased);
		const int CurScale = (int)std::lround(mix((float)Anim.m_StartScale, (float)Anim.m_TargetScale, Eased));

		HudLayout::SetScale(Anim.m_Module, CurScale);
		if(Anim.m_Phase >= 1.0f && Anim.m_Kind != RESET_KIND_SCALE)
		{
			// Leave position at the true default (no runtime override) so dynamic
			// right-edge stacking - Dummy Actions appearing/disappearing, etc. -
			// keeps working instead of baking a temporary stack gap into the save.
			HudLayout::ResetPosition(Anim.m_Module);
			HudLayout::SetScale(Anim.m_Module, Anim.m_TargetScale);
		}
		else
		{
			ApplyDraggedPosition(Anim.m_Module, CurRect);
		}
	}

	// Compact away finished entries (swap-remove, order doesn't matter here).
	int Write = 0;
	for(int Read = 0; Read < m_ResetAnimCount; ++Read)
	{
		if(m_aResetAnims[Read].m_Phase < 1.0f)
		{
			if(Write != Read)
				m_aResetAnims[Write] = m_aResetAnims[Read];
			++Write;
		}
	}
	m_ResetAnimCount = Write;
}

CUi::EPopupMenuFunctionResult CHudEditor::PopupModuleSettings(void *pContext, CUIRect View, bool Active)
{
	(void)Active;
	CHudEditor *pThis = static_cast<CHudEditor *>(pContext);
	if(pThis->m_SelectedModule == HudLayout::MODULE_COUNT)
		return CUi::POPUP_CLOSE_CURRENT;

	// Smoothly grow the popup out of the module it belongs to instead of popping in at
	// full size: the outer frame (border+background) is drawn here manually - growing
	// from the corner nearest the module - since DoPopupMenu() already drew a transparent
	// one for us (see OpenModuleSettings), and the real content below is clipped to match.
	if(BCUiAnimations::Enabled() && g_Config.m_BcModuleUiRevealAnimation != 0)
		BCUiAnimations::UpdatePhase(pThis->m_PopupRevealPhase, 1.0f, pThis->Client()->RenderFrameTime(), BCUiAnimations::MsToSeconds(g_Config.m_BcModuleUiRevealAnimationMs));
	else
		pThis->m_PopupRevealPhase = 1.0f;
	const float Phase = BCUiAnimations::EaseOutCubic(pThis->m_PopupRevealPhase);

	CUIRect OuterRect = View;
	OuterRect.x -= POPUP_FRAME_MARGIN;
	OuterRect.y -= POPUP_FRAME_MARGIN;
	OuterRect.w += POPUP_FRAME_MARGIN * 2.0f;
	OuterRect.h += POPUP_FRAME_MARGIN * 2.0f;
	// Cached so RenderClosingPopupFrame() can replay the same box, shrinking, after this
	// popup is gone from CUi's popup stack (see the close-detection in OnRender()).
	pThis->m_LastPopupOuterRect = OuterRect;

	const CUIRect AnimRect = ComputeAnimRect(OuterRect, pThis->m_PopupGrowFromRight, Phase);
	DrawPopupFrame(AnimRect, Phase);
	pThis->Ui()->ClipEnable(&AnimRect);

	const bool Enabled = HudLayout::IsEnabled(pThis->m_SelectedModule);
	CUIRect Title, ToggleButton, ScaleLabel, ScaleSlider, ResetScaleButton, ResetPositionButton, ResetAllButton;
	View.HSplitTop(16.0f, &Title, &View);
	pThis->Ui()->DoLabel(&Title, HudLayout::Name(pThis->m_SelectedModule), 10.0f, TEXTALIGN_MC);
	View.HSplitTop(4.0f, nullptr, &View);
	View.HSplitTop(16.0f, &ToggleButton, &View);
	if(pThis->GameClient()->m_Menus.DoButton_CheckBox(&pThis->m_ToggleModuleButton, Localize("Enabled"), Enabled ? 1 : 0, &ToggleButton))
		HudLayout::SetEnabled(pThis->m_SelectedModule, !Enabled);

	View.HSplitTop(4.0f, nullptr, &View);
	View.HSplitTop(12.0f, &ScaleLabel, &View);
	const int Scale = HudLayout::Get(pThis->m_SelectedModule, pThis->HudWidth(), pThis->HudHeight()).m_Scale;
	char aScale[32];
	str_format(aScale, sizeof(aScale), "%s %d%%", Localize("Scale"), Scale);
	pThis->Ui()->DoLabel(&ScaleLabel, aScale, 8.0f, TEXTALIGN_ML);

	View.HSplitTop(14.0f, &ScaleSlider, &View);
	const float Relative = CUi::ms_LinearScrollbarScale.ToRelative(Scale, 25, 300);
	const float NewRelative = pThis->Ui()->DoScrollbarH(&pThis->m_SelectedModule, &ScaleSlider, Relative);
	const int NewScale = CUi::ms_LinearScrollbarScale.ToAbsolute(NewRelative, 25, 300);
	if(NewScale != Scale)
	{
		if(pThis->m_SelectedModule != HudLayout::MODULE_CHAT)
		{
			const SModuleVisual Visual = pThis->GetModuleVisual(pThis->m_SelectedModule);
			pThis->ApplyDraggedPosition(pThis->m_SelectedModule, Visual.m_Rect);
		}
		HudLayout::SetScale(pThis->m_SelectedModule, NewScale);
	}

	View.HSplitTop(6.0f, nullptr, &View);
	View.HSplitTop(16.0f, &ResetScaleButton, &View);
	if(pThis->Ui()->DoButton_PopupMenu(&pThis->m_ResetScaleButton, Localize("Reset scale"), &ResetScaleButton, 8.0f, TEXTALIGN_MC))
		pThis->StartResetAnimation(pThis->m_SelectedModule, RESET_KIND_SCALE);

	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(16.0f, &ResetPositionButton, &View);
	if(pThis->Ui()->DoButton_PopupMenu(&pThis->m_ResetPositionButton, Localize("Reset position"), &ResetPositionButton, 8.0f, TEXTALIGN_MC))
		pThis->StartResetAnimation(pThis->m_SelectedModule, RESET_KIND_POSITION);

	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(16.0f, &ResetAllButton, &View);
	const ColorRGBA ResetAllColor(1.0f, 0.32f, 0.32f, 0.85f * pThis->Ui()->ButtonColorMul(&pThis->m_ResetSettingsButton));
	if(pThis->Ui()->DoButton_PopupMenu(&pThis->m_ResetSettingsButton, Localize("Reset all"), &ResetAllButton, 8.0f, TEXTALIGN_MC, 0.0f, false, true, ResetAllColor))
		pThis->StartResetAnimation(pThis->m_SelectedModule, RESET_KIND_ALL);

	pThis->Ui()->ClipDisable();
	return CUi::POPUP_KEEP_OPEN;
}

void CHudEditor::OpenModuleSettings(const SModuleVisual &Visual)
{
	if(!IsEditableModule(Visual.m_Module))
		return;

	m_SelectedModule = Visual.m_Module;
	const float Width = HudWidth();
	const float Height = HudHeight();
	const float UiScaleX = Ui()->Screen()->w / maximum(Width, 1.0f);
	const float UiScaleY = Ui()->Screen()->h / maximum(Height, 1.0f);
	constexpr float PopupMargin = 5.0f;
	constexpr float PopupGap = 6.0f;
	const float PopupWidth = SETTINGS_POPUP_WIDTH;
	const float PopupHeight = SETTINGS_POPUP_HEIGHT;
	const CUIRect ModuleRectUi = {
		Visual.m_Rect.x * UiScaleX,
		Visual.m_Rect.y * UiScaleY,
		Visual.m_Rect.w * UiScaleX,
		Visual.m_Rect.h * UiScaleY};
	float PopupX = ModuleRectUi.x + ModuleRectUi.w + PopupGap;
	m_PopupGrowFromRight = false;
	if(PopupX + PopupWidth > Ui()->Screen()->w - PopupMargin)
	{
		PopupX = ModuleRectUi.x - PopupWidth - PopupGap;
		m_PopupGrowFromRight = true;
	}
	PopupX = std::clamp(PopupX, PopupMargin, maximum(PopupMargin, Ui()->Screen()->w - PopupWidth - PopupMargin));
	const float PopupY = std::clamp(ModuleRectUi.y, PopupMargin, maximum(PopupMargin, Ui()->Screen()->h - PopupHeight - PopupMargin));
	m_PopupRevealPhase = 0.0f;
	Ui()->ClosePopupMenus();
	// The popup's own border/background are drawn manually inside PopupModuleSettings so
	// they can grow in sync with the reveal animation, so make DoPopupMenu's copy invisible.
	SPopupMenuProperties Props;
	Props.m_BorderColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
	Props.m_BackgroundColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
	Ui()->DoPopupMenu(&m_SettingsPopupId, PopupX, PopupY, PopupWidth, PopupHeight, this, PopupModuleSettings, Props);
}

void CHudEditor::RenderModuleOutline(const SModuleVisual &Visual, bool Hovered, bool Selected) const
{
	const CUIRect &Rect = Visual.m_Rect;
	ColorRGBA Color = Selected ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.92f) : (Hovered ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.78f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.46f));
	if(!Visual.m_Editable)
		Color.a *= 0.72f;
	if(!Visual.m_Enabled)
		Color.a *= 0.82f;

	DrawRoundedRectOutline(Graphics(), Rect, Visual.m_Corners, Visual.m_Rounding, Color);
}

void CHudEditor::RenderResizeHandles(const SModuleVisual &Visual) const
{
	const CUIRect &Rect = Visual.m_Rect;
	if(Rect.w <= 0.0f || Rect.h <= 0.0f)
		return;

	const bool Active = m_ResizingModule == Visual.m_Module;
	const ColorRGBA Color = Active ? ColorRGBA(1.0f, 0.82f, 0.35f, 1.0f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.95f);
	constexpr ColorRGBA Outline(0.0f, 0.0f, 0.0f, 0.5f);
	constexpr float InnerRadius = RESIZE_HANDLE_RADIUS - RESIZE_HANDLE_THICKNESS * 0.5f;
	constexpr float OuterRadius = RESIZE_HANDLE_RADIUS + RESIZE_HANDLE_THICKNESS * 0.5f;
	static constexpr int aCorners[4] = {RESIZE_CORNER_TL, RESIZE_CORNER_TR, RESIZE_CORNER_BL, RESIZE_CORNER_BR};

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	for(int Corner : aCorners)
	{
		// Flush against a screen edge - see HudLayout::BackgroundCorners() - so there's
		// nothing meaningful to grab there; hide the bracket instead of clipping it
		// against the wall.
		if((Visual.m_Corners & CornerBit(Corner)) == 0)
			continue;

		// A quarter-circle bracket accenting the corner, independent of the module's own
		// rounding, instead of a dot sitting on top of it. Rounded end caps (via
		// DrawHandleBracket) keep it from looking like a strip that's been cut off.
		vec2 Center;
		float StartAngle, EndAngle;
		CornerArcParams(Rect, Corner, RESIZE_HANDLE_RADIUS, Center, StartAngle, EndAngle);
		DrawHandleBracket(Graphics(), Center, StartAngle, EndAngle, InnerRadius - RESIZE_HANDLE_OUTLINE, OuterRadius + RESIZE_HANDLE_OUTLINE, Outline);
		DrawHandleBracket(Graphics(), Center, StartAngle, EndAngle, InnerRadius, OuterRadius, Color);
	}
	Graphics()->QuadsEnd();
}

void CHudEditor::RenderClosingPopupFrame() const
{
	// The real popup is already gone from CUi's stack by the time this runs (see
	// OnRender()'s close-detection), so this just replays its frame shrinking back
	// into the corner it grew from - the mirror image of PopupModuleSettings's opening draw.
	const float Phase = BCUiAnimations::EaseOutCubic(m_PopupClosePhase);
	if(Phase <= 0.0f)
		return;
	const CUIRect AnimRect = ComputeAnimRect(m_LastPopupOuterRect, m_PopupGrowFromRight, Phase);
	DrawPopupFrame(AnimRect, Phase);
}

void CHudEditor::RenderModuleLabel(const SModuleVisual &Visual) const
{
	char aName[64];
	str_format(aName, sizeof(aName), "%s", HudLayout::Name(Visual.m_Module));
	const char *pStatus = nullptr;
	if(Visual.m_Editable && !Visual.m_Enabled)
		pStatus = Localize("disabled");
	else if(!Visual.m_Editable)
		pStatus = Localize("preview");

	const float Width = HudWidth();
	const float Height = HudHeight();
	const float FontSize = 6.6f;
	const float StatusFontSize = 5.6f;
	const float NameWidth = TextRender()->TextWidth(FontSize, aName, -1, -1.0f);
	const float StatusWidth = pStatus ? TextRender()->TextWidth(StatusFontSize, pStatus, -1, -1.0f) + 6.0f : 0.0f;
	const float LabelW = 10.0f + NameWidth + StatusWidth;
	const float LabelH = 13.0f;
	constexpr float TailSize = 2.6f;
	float X = std::clamp(Visual.m_Rect.x + (Visual.m_Rect.w - LabelW) * 0.5f, 2.0f, Width - LabelW - 2.0f);
	float Y = Visual.m_Rect.y - LabelH - TailSize - 2.0f;
	const bool PointingDown = Y >= 2.0f;
	if(!PointingDown)
		Y = minimum(Height - LabelH - TailSize - 2.0f, Visual.m_Rect.y + Visual.m_Rect.h + TailSize + 2.0f);

	// Soft border first, then an inset fill, so the tooltip reads as a distinct floating
	// chip instead of a flat black box that blends into a dark HUD background.
	CUIRect LabelRect = {X, Y, LabelW, LabelH};
	Graphics()->DrawRect(LabelRect.x - 0.6f, LabelRect.y - 0.6f, LabelRect.w + 1.2f, LabelRect.h + 1.2f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.14f), IGraphics::CORNER_ALL, 6.0f);
	Graphics()->DrawRect(LabelRect.x, LabelRect.y, LabelRect.w, LabelRect.h, ColorRGBA(0.04f, 0.04f, 0.05f, 0.88f), IGraphics::CORNER_ALL, 5.5f);

	// Small tail connecting the chip to the element it describes, like a speech bubble.
	const float TailCenterX = std::clamp(Visual.m_Rect.x + Visual.m_Rect.w * 0.5f, LabelRect.x + TailSize * 2.0f, LabelRect.x + LabelRect.w - TailSize * 2.0f);
	const float TailBaseY = PointingDown ? LabelRect.y + LabelRect.h : LabelRect.y;
	const float TailDir = PointingDown ? 1.0f : -1.0f;
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(0.04f, 0.04f, 0.05f, 0.88f);
	IGraphics::CFreeformItem Tail(
		TailCenterX - TailSize, TailBaseY,
		TailCenterX + TailSize, TailBaseY,
		TailCenterX, TailBaseY + TailDir * TailSize * 1.5f,
		TailCenterX, TailBaseY + TailDir * TailSize * 1.5f);
	Graphics()->QuadsDrawFreeform(&Tail, 1);
	Graphics()->QuadsEnd();

	CUIRect NameRect, StatusRect;
	if(pStatus)
		LabelRect.VSplitRight(StatusWidth, &NameRect, &StatusRect);
	else
		NameRect = LabelRect;
	Ui()->DoLabel(&NameRect, aName, FontSize, TEXTALIGN_MC);
	if(pStatus)
	{
		TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.55f));
		Ui()->DoLabel(&StatusRect, pStatus, StatusFontSize, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
}

void CHudEditor::RenderModulePreview(const SModuleVisual &Visual) const
{
	const CUIRect &Rect = Visual.m_Rect;
	if(Rect.w <= 0.0f || Rect.h <= 0.0f)
		return;

	// All modules currently wired into the editor render themselves live (see the
	// RenderXPreview() calls in RenderOverlay), so this is just a translucent
	// highlight over the real content marking the draggable area.
	ColorRGBA Fill = Visual.m_Editable ? ColorRGBA(0.22f, 0.37f, 0.56f, 0.10f) : ColorRGBA(0.25f, 0.25f, 0.25f, 0.08f);
	if(Visual.m_IsFallbackPreview)
		Fill = ColorRGBA(0.30f, 0.26f, 0.20f, 0.20f);
	if(!Visual.m_Enabled)
		Fill = ColorRGBA(0.12f, 0.14f, 0.18f, 0.22f);
	Graphics()->DrawRect(Rect.x, Rect.y, Rect.w, Rect.h, Fill, Visual.m_Corners, Visual.m_Rounding);
	if(!Visual.m_Enabled)
		Graphics()->DrawRect(Rect.x, Rect.y, Rect.w, Rect.h, ColorRGBA(0.02f, 0.03f, 0.04f, 0.30f), Visual.m_Corners, Visual.m_Rounding);
}

void CHudEditor::RenderOverlay(vec2 MousePos)
{
	const float Width = HudWidth();
	const float Height = HudHeight();
	Graphics()->MapScreen(0.0f, 0.0f, Width, Height);
	Graphics()->TextureClear();
	Graphics()->DrawRect(0.0f, 0.0f, Width, Height, ColorRGBA(0.0f, 0.0f, 0.0f, 0.38f), IGraphics::CORNER_ALL, 0.0f);

	// Draw true HUD previews first, then add interactive editor overlays on top.
	// Chat renders itself every frame via its own OnRender regardless of g_Config.m_ClShowhud,
	// so it doesn't need an explicit preview call here like the CHud-owned modules below.
	GameClient()->m_Hud.RenderScoreHudPreview();
	GameClient()->m_Hud.RenderKeystrokesKeyboardPreview();
	GameClient()->m_Hud.RenderKeystrokesMousePreview();
	GameClient()->m_Hud.RenderSpectatorCountPreview();
	GameClient()->m_Hud.RenderDummyActionsPreview();
	GameClient()->m_Hud.RenderMovementInformationPreview();
	GameClient()->m_Voting.Render(true);
	GameClient()->m_Hud.RenderLocalTimePreview();
	GameClient()->m_Hud.RenderFrozenHudPreview();
	GameClient()->m_Hud.RenderNotifyLastPreview();
	GameClient()->m_Hud.RenderFinishPredictionPreview();
	const bool MusicPlayerHasLiveRect = g_Config.m_BcMusicPlayer != 0 && GameClient()->m_MusicPlayer.HudReservation().m_Visible;
	GameClient()->m_MusicPlayer.RenderHudEditor(!MusicPlayerHasLiveRect);
	GameClient()->m_VoiceChat.RenderHudTalkingIndicator(Width, Height, true);
	GameClient()->m_VoiceChat.RenderHudMuteStatusIndicator(Width, Height, true);

	SModuleVisual aVisuals[MAX_MODULE_VISUALS];
	int Count = 0;
	CollectModuleVisuals(aVisuals, Count);
	for(int Pass = 0; Pass < 2; ++Pass)
	{
		const bool RenderEditable = Pass == 1;
		for(int i = 0; i < Count; ++i)
		{
			if(aVisuals[i].m_Editable != RenderEditable)
				continue;
			RenderModulePreview(aVisuals[i]);
		}
	}

	for(int Pass = 0; Pass < 2; ++Pass)
	{
		const bool RenderEditable = Pass == 1;
		for(int i = 0; i < Count; ++i)
		{
			if(aVisuals[i].m_Editable != RenderEditable)
				continue;
			const bool Hovered = aVisuals[i].m_Module == m_HoveredModule;
			const bool Selected = aVisuals[i].m_Module == m_SelectedModule || aVisuals[i].m_Module == m_PressedModule;
			RenderModuleOutline(aVisuals[i], Hovered, Selected);
			if(Hovered)
				RenderModuleLabel(aVisuals[i]);
			if(aVisuals[i].m_Editable && (Hovered || Selected))
				RenderResizeHandles(aVisuals[i]);
		}
	}

	CUIRect ResetRect = {8.0f, 8.0f, 66.0f, 16.0f};
	const bool ResetHovered = PointInRect(MousePos, ResetRect);
	const ColorRGBA ResetColor = m_PressedOnReset ? ColorRGBA(0.95f, 0.48f, 0.48f, 0.90f) :
							(ResetHovered ? ColorRGBA(0.95f, 0.48f, 0.48f, 0.55f) : ColorRGBA(0.95f, 0.48f, 0.48f, 0.36f));
	Graphics()->DrawRect(ResetRect.x, ResetRect.y, ResetRect.w, ResetRect.h, ResetColor, IGraphics::CORNER_ALL, 4.0f);
	Ui()->DoLabel(&ResetRect, Localize("Reset All"), 6.5f, TEXTALIGN_MC);

	Ui()->MapScreen();
	Ui()->RenderPopupMenus();
	if(m_PopupClosing)
	{
		if(BCUiAnimations::Enabled() && g_Config.m_BcModuleUiRevealAnimation != 0)
			BCUiAnimations::UpdatePhase(m_PopupClosePhase, 0.0f, Client()->RenderFrameTime(), BCUiAnimations::MsToSeconds(g_Config.m_BcModuleUiRevealAnimationMs));
		else
			m_PopupClosePhase = 0.0f;
		RenderClosingPopupFrame();
		if(m_PopupClosePhase <= 0.001f)
			m_PopupClosing = false;
	}
	Graphics()->MapScreen(0.0f, 0.0f, Width, Height);
	RenderTools()->RenderCursor(MousePos, 12.0f);
}

void CHudEditor::OnRender()
{
	if(!m_Active)
		return;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		Deactivate();
		return;
	}

	Ui()->StartCheck();
	Ui()->Update();
	Ui()->MapScreen();

	UpdateResetAnimation();

	const vec2 WindowSize(maximum(1.0f, (float)Graphics()->WindowWidth()), maximum(1.0f, (float)Graphics()->WindowHeight()));
	const vec2 UiMousePos = Ui()->UpdatedMousePos() * vec2(Ui()->Screen()->w, Ui()->Screen()->h) / WindowSize;
	const vec2 UiToHudScale(HudWidth() / maximum(Ui()->Screen()->w, 1.0f), HudHeight() / maximum(Ui()->Screen()->h, 1.0f));
	const vec2 MousePos = UiMousePos * UiToHudScale;
	const bool LeftDown = Input()->KeyIsPressed(KEY_MOUSE_1);
	const bool RightDown = Input()->KeyIsPressed(KEY_MOUSE_2);
	const bool LeftClicked = LeftDown && !m_MouseDownLast;
	const bool RightClicked = RightDown && !m_RightMouseDownLast;
	const bool PopupOpen = Ui()->IsPopupOpen(&m_SettingsPopupId);

	m_HoveredModule = HitTestModule(MousePos);
	int ResizeHitCorner = RESIZE_CORNER_NONE;
	const HudLayout::EModule ResizeHitModule = HitTestResizeHandle(MousePos, ResizeHitCorner);

	CUIRect ResetRect = {8.0f, 8.0f, 66.0f, 16.0f};
	const bool ResetHovered = PointInRect(MousePos, ResetRect);

	if(RightClicked && m_HoveredModule != HudLayout::MODULE_COUNT && IsEditableModule(m_HoveredModule))
	{
		m_SelectedModule = m_HoveredModule;
		OpenModuleSettings(GetModuleVisual(m_HoveredModule));
	}

	if(PopupOpen)
	{
		m_Dragging = false;
		m_PressedModule = HudLayout::MODULE_COUNT;
		m_ResizingModule = HudLayout::MODULE_COUNT;
		m_ResizeCorner = RESIZE_CORNER_NONE;
		m_PressedOnReset = false;
	}
	else if(LeftClicked && ResetHovered)
	{
		SModuleVisual aVisuals[MAX_MODULE_VISUALS];
		int Count = 0;
		CollectModuleVisuals(aVisuals, Count);
		for(int i = 0; i < Count; ++i)
		{
			if(IsEditableModule(aVisuals[i].m_Module))
				StartResetAnimation(aVisuals[i].m_Module, RESET_KIND_ALL);
		}
		m_Dragging = false;
		m_PressedModule = HudLayout::MODULE_COUNT;
		m_ResizingModule = HudLayout::MODULE_COUNT;
		m_ResizeCorner = RESIZE_CORNER_NONE;
		m_SelectedModule = HudLayout::MODULE_COUNT;
		m_PressedOnReset = true;
		m_SuppressCloseAnim = true;
		Ui()->ClosePopupMenus();
	}
	else if(LeftClicked && ResizeHitModule != HudLayout::MODULE_COUNT)
	{
		m_PressedOnReset = false;
		m_PressMousePos = MousePos;
		m_SelectedModule = ResizeHitModule;
		m_ResizingModule = ResizeHitModule;
		m_ResizeCorner = ResizeHitCorner;
		CancelResetAnimation(ResizeHitModule);
		m_ResizeAnchorRect = GetModuleVisual(ResizeHitModule).m_Rect;
		m_ResizeStartScale = HudLayout::Get(ResizeHitModule, HudWidth(), HudHeight()).m_Scale;
	}
	else if(LeftClicked)
	{
		m_PressedOnReset = false;
		m_PressMousePos = MousePos;
		m_SelectedModule = m_HoveredModule;
		m_PressedModule = (m_HoveredModule != HudLayout::MODULE_COUNT && IsEditableModule(m_HoveredModule)) ? m_HoveredModule : HudLayout::MODULE_COUNT;
		if(m_PressedModule != HudLayout::MODULE_COUNT)
		{
			CancelResetAnimation(m_PressedModule);
			const SModuleVisual Visual = GetModuleVisual(m_PressedModule);
			m_DragMouseOffset = MousePos - vec2(Visual.m_Rect.x, Visual.m_Rect.y);
		}
	}
	else if(LeftDown && m_MouseDownLast && m_ResizingModule != HudLayout::MODULE_COUNT)
	{
		UpdateResizing(MousePos);
	}
	else if(LeftDown && m_MouseDownLast && m_PressedModule != HudLayout::MODULE_COUNT)
	{
		if(!m_Dragging && distance(m_PressMousePos, MousePos) > 2.0f)
			m_Dragging = true;
		UpdateDragging(MousePos);
	}
	else if(!LeftDown && m_MouseDownLast)
	{
		m_Dragging = false;
		m_PressedModule = HudLayout::MODULE_COUNT;
		m_ResizingModule = HudLayout::MODULE_COUNT;
		m_ResizeCorner = RESIZE_CORNER_NONE;
		m_PressedOnReset = false;
	}

	m_MouseDownLast = LeftDown;
	m_RightMouseDownLast = RightDown;

	RenderOverlay(MousePos);

	// CUi closes the settings popup on its own (click elsewhere, Escape) during the
	// RenderPopupMenus() call inside RenderOverlay(), without going through any of our own
	// ClosePopupMenus() calls above, so catch that transition here (comparing against
	// PopupOpen from the top of this same frame) and play a closing animation instead of
	// just having it vanish.
	if(PopupOpen && !Ui()->IsPopupOpen(&m_SettingsPopupId) && !m_SuppressCloseAnim)
	{
		m_PopupClosing = true;
		m_PopupClosePhase = 1.0f;
	}
	m_SuppressCloseAnim = false;

	Ui()->FinishCheck();
	Ui()->ClearHotkeys();
}
