/* Copyright © 2026 BestProject Team */
#include "hud_editor.h"

#include "music_player.h"
#include "voice/voice.h"

#include <base/color.h>
#include <base/math.h>
#include <base/str.h>

#include <cmath>

#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/storage.h>
#include <engine/shared/config.h>

#include <game/client/components/chat.h>
#include <game/client/components/voting.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

namespace
{
constexpr float SNAP_THRESHOLD = 6.0f;
constexpr float SETTINGS_POPUP_WIDTH = 210.0f;
constexpr float SETTINGS_POPUP_HEIGHT = 132.0f;

bool IsMusicPlayerEnabled(const CGameClient *pGameClient)
{
	return g_Config.m_BcMusicPlayer != 0 &&
	       !pGameClient->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_MUSIC_PLAYER);
}

bool IsEditorModule(HudLayout::EModule Module)
{
	return Module == HudLayout::MODULE_CHAT ||
	       Module == HudLayout::MODULE_VOICE_TALKERS ||
	       Module == HudLayout::MODULE_VOICE_STATUS ||
	       Module == HudLayout::MODULE_VOTES ||
	       Module == HudLayout::MODULE_LOCAL_TIME;
}

bool IsLivePreviewModule(HudLayout::EModule Module)
{
	return Module == HudLayout::MODULE_CHAT ||
		Module == HudLayout::MODULE_VOTES ||
		Module == HudLayout::MODULE_LOCAL_TIME ||
		Module == HudLayout::MODULE_VOICE_TALKERS ||
		Module == HudLayout::MODULE_VOICE_STATUS;
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
	m_PressedOnReset = false;
	Ui()->ClosePopupMenus();
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
	case HudLayout::MODULE_SCORE:
		Rect = {Layout.m_X, Layout.m_Y, 112.0f, 56.0f};
		break;
	case HudLayout::MODULE_MOVEMENT_INFO:
	{
		const bool ShowPos = g_Config.m_ClShowhudPlayerPosition != 0;
		const bool ShowSpeed = g_Config.m_ClShowhudPlayerSpeed != 0;
		const bool ShowAngle = g_Config.m_ClShowhudPlayerAngle != 0;
		float BoxHeight = 3.0f * 8.0f * ((ShowPos ? 1.0f : 0.0f) + (ShowSpeed ? 1.0f : 0.0f)) + 2.0f * 8.0f * (ShowAngle ? 1.0f : 0.0f);
		if(g_Config.m_BcShowhudDummyCoordIndicator)
			BoxHeight += 8.0f;
		if(ShowPos || ShowSpeed || ShowAngle)
			BoxHeight += 2.0f;
		BoxHeight = maximum(BoxHeight, 12.0f);
		Rect = {Layout.m_X, Layout.m_Y, 62.0f, BoxHeight};
		break;
	}
	case HudLayout::MODULE_GAME_TIMER:
		Rect = {Layout.m_X, Layout.m_Y, 64.0f, 12.0f};
		break;
	case HudLayout::MODULE_FPS:
		Rect = {Layout.m_X, Layout.m_Y, 26.0f, 9.0f};
		break;
	case HudLayout::MODULE_PING:
		Rect = {Layout.m_X, Layout.m_Y, 26.0f, 9.0f};
		break;
	case HudLayout::MODULE_LOCAL_TIME:
	{
		const bool Seconds = g_Config.m_TcShowLocalTimeSeconds != 0;
		const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
		const char *pPreviewText = Seconds ? "18:42.37" : "18:42";
		const float FontSize = 5.0f * Scale;
		const float Padding = 5.0f * Scale;
		Rect = {Layout.m_X, Layout.m_Y, TextRender()->TextWidth(FontSize, pPreviewText, -1, -1.0f) + Padding * 2.0f, 12.5f * Scale};
		break;
	}
	case HudLayout::MODULE_SPECTATOR_COUNT:
		Rect = {Layout.m_X, Layout.m_Y, 118.0f, 16.0f};
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
	case HudLayout::MODULE_FROZEN_HUD:
		Rect = {Layout.m_X, Layout.m_Y, 176.0f, 34.0f};
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
	case HudLayout::MODULE_MUSIC_PLAYER:
		if(IsMusicPlayerEnabled(GameClient()))
		{
			Visual.m_Rect = GameClient()->m_MusicPlayer.GetHudEditorRect(false);
			if(Visual.m_Rect.w <= 0.0f || Visual.m_Rect.h <= 0.0f)
				Visual.m_Rect = GameClient()->m_MusicPlayer.GetHudEditorRect(true);
		}
		else
		{
			const auto Layout = HudLayout::Get(HudLayout::MODULE_MUSIC_PLAYER, Width, Height);
			Visual.m_Rect = {Layout.m_X, Layout.m_Y, 92.0f, 12.0f};
		}
		Visual.m_Rounding = 8.0f;
		break;
	case HudLayout::MODULE_VOICE_TALKERS:
		Visual.m_Rect = GameClient()->m_VoiceChat.GetHudTalkingIndicatorRect(Width, Height, true);
		Visual.m_Rounding = 4.0f;
		break;
	case HudLayout::MODULE_VOICE_STATUS:
		Visual.m_Rect = GameClient()->m_VoiceChat.GetHudMuteStatusIndicatorRect(Width, Height, true);
		Visual.m_Rounding = 3.0f;
		break;
	case HudLayout::MODULE_CHAT:
		Visual.m_Rect = GameClient()->m_Chat.GetHudRect(Width, Height, true);
		Visual.m_Rounding = 6.0f;
		Visual.m_UsesBottomAnchor = true;
		break;
	case HudLayout::MODULE_VOTES:
		Visual.m_Rect = GameClient()->m_Voting.GetHudRect(Width, Height, true);
		Visual.m_Rounding = 3.0f;
		break;
	case HudLayout::MODULE_LOCAL_TIME:
		Visual.m_Rect = GameClient()->m_Hud.GetLocalTimeHudEditorRect();
		Visual.m_Rounding = 3.75f * std::clamp(HudLayout::Get(HudLayout::MODULE_LOCAL_TIME, Width, Height).m_Scale / 100.0f, 0.25f, 3.0f);
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

	AddModule(HudLayout::MODULE_CHAT);
	AddModule(HudLayout::MODULE_VOTES);
	AddModule(HudLayout::MODULE_LOCAL_TIME);
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

void CHudEditor::ApplyDraggedPosition(HudLayout::EModule Module, const CUIRect &Rect)
{
	if(!IsEditableModule(Module))
		return;

	const float CanvasX = Rect.x * (HudLayout::CANVAS_WIDTH / maximum(HudWidth(), 1.0f));
	if(Module == HudLayout::MODULE_CHAT)
	{
		const float BottomAnchor = Rect.y + Rect.h - ChatInputBottomExtra(GameClient()->m_Chat);
		HudLayout::SetPosition(Module, CanvasX, BottomAnchor);
	}
	else if(Module == HudLayout::MODULE_LOCAL_TIME)
	{
		const auto Layout = HudLayout::Get(HudLayout::MODULE_LOCAL_TIME, HudWidth(), HudHeight());
		const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
		const float Padding = 5.0f * Scale;
		const float AnchorX = (Rect.x + Rect.w + Padding) * (HudLayout::CANVAS_WIDTH / maximum(HudWidth(), 1.0f));
		HudLayout::SetPosition(Module, AnchorX, Rect.y);
	}
	else
		HudLayout::SetPosition(Module, CanvasX, Rect.y);
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
	NewRect = SnapRect(NewRect, m_PressedModule);
	ApplyDraggedPosition(m_PressedModule, NewRect);
}

CUi::EPopupMenuFunctionResult CHudEditor::PopupModuleSettings(void *pContext, CUIRect View, bool Active)
{
	(void)Active;
	CHudEditor *pThis = static_cast<CHudEditor *>(pContext);
	if(pThis->m_SelectedModule == HudLayout::MODULE_COUNT)
		return CUi::POPUP_CLOSE_CURRENT;

	const bool Enabled = HudLayout::IsEnabled(pThis->m_SelectedModule);
	CUIRect Title, ToggleButton, ScaleLabel, ScaleSlider, ResetPositionButton, ResetSettingsButton;
	View.HSplitTop(16.0f, &Title, &View);
	pThis->Ui()->DoLabel(&Title, HudLayout::Name(pThis->m_SelectedModule), 10.0f, TEXTALIGN_MC);
	View.HSplitTop(8.0f, nullptr, &View);
	View.HSplitTop(16.0f, &ToggleButton, &View);
	if(pThis->GameClient()->m_Menus.DoButton_CheckBox(&pThis->m_ToggleModuleButton, Localize("Enabled"), Enabled ? 1 : 0, &ToggleButton))
		HudLayout::SetEnabled(pThis->m_SelectedModule, !Enabled);

	View.HSplitTop(8.0f, nullptr, &View);
	View.HSplitTop(12.0f, &ScaleLabel, &View);

	if(pThis->IsEditableModule(pThis->m_SelectedModule))
	{
		const int Scale = HudLayout::Get(pThis->m_SelectedModule, pThis->HudWidth(), pThis->HudHeight()).m_Scale;
		char aScale[32];
		str_format(aScale, sizeof(aScale), "%s %d%%", Localize("Scale"), Scale);
		pThis->Ui()->DoLabel(&ScaleLabel, aScale, 8.0f, TEXTALIGN_ML);

		View.HSplitTop(14.0f, &ScaleSlider, &View);
		const float Relative = CUi::ms_LinearScrollbarScale.ToRelative(Scale, 25, 300);
		const float NewRelative = pThis->Ui()->DoScrollbarH(&pThis->m_SelectedModule, &ScaleSlider, Relative);
		HudLayout::SetScale(pThis->m_SelectedModule, CUi::ms_LinearScrollbarScale.ToAbsolute(NewRelative, 25, 300));
	}
	else
	{
		View.HSplitTop(14.0f, nullptr, &View);
	}

	View.HSplitTop(10.0f, nullptr, &View);
	View.HSplitTop(16.0f, &ResetPositionButton, &View);
	if(pThis->Ui()->DoButton_PopupMenu(&pThis->m_ResetPositionButton, Localize("Reset position"), &ResetPositionButton, 8.0f, TEXTALIGN_MC))
		HudLayout::ResetPosition(pThis->m_SelectedModule);

	View.HSplitTop(5.0f, nullptr, &View);
	View.HSplitTop(16.0f, &ResetSettingsButton, &View);
	if(pThis->Ui()->DoButton_PopupMenu(&pThis->m_ResetSettingsButton, Localize("Reset settings"), &ResetSettingsButton, 8.0f, TEXTALIGN_MC))
		HudLayout::ResetSettings(pThis->m_SelectedModule);
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
	const float PopupWidth = SETTINGS_POPUP_WIDTH * UiScaleX;
	const float PopupHeight = SETTINGS_POPUP_HEIGHT * UiScaleY;
	const CUIRect ModuleRectUi = {
		Visual.m_Rect.x * UiScaleX,
		Visual.m_Rect.y * UiScaleY,
		Visual.m_Rect.w * UiScaleX,
		Visual.m_Rect.h * UiScaleY};
	float PopupX = ModuleRectUi.x + ModuleRectUi.w + PopupGap;
	if(PopupX + PopupWidth > Ui()->Screen()->w - PopupMargin)
		PopupX = ModuleRectUi.x - PopupWidth - PopupGap;
	PopupX = std::clamp(PopupX, PopupMargin, maximum(PopupMargin, Ui()->Screen()->w - PopupWidth - PopupMargin));
	const float PopupY = std::clamp(ModuleRectUi.y, PopupMargin, maximum(PopupMargin, Ui()->Screen()->h - PopupHeight - PopupMargin));
	Ui()->ClosePopupMenus();
	Ui()->DoPopupMenu(&m_SettingsPopupId, PopupX, PopupY, PopupWidth, PopupHeight, this, PopupModuleSettings);
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

void CHudEditor::RenderModuleLabel(const SModuleVisual &Visual) const
{
	char aLabel[96];
	if(Visual.m_Editable && !Visual.m_Enabled)
		str_format(aLabel, sizeof(aLabel), "%s (%s)", HudLayout::Name(Visual.m_Module), Localize("disabled"));
	else if(Visual.m_Editable)
		str_format(aLabel, sizeof(aLabel), "%s", HudLayout::Name(Visual.m_Module));
	else
		str_format(aLabel, sizeof(aLabel), "%s (%s)", HudLayout::Name(Visual.m_Module), Localize("preview"));

	const float Width = HudWidth();
	const float Height = HudHeight();
	const float FontSize = 6.4f;
	const float TextWidth = TextRender()->TextWidth(FontSize, aLabel, -1, -1.0f);
	const float LabelW = TextWidth + 8.0f;
	const float LabelH = 12.0f;
	float X = std::clamp(Visual.m_Rect.x + (Visual.m_Rect.w - LabelW) * 0.5f, 2.0f, Width - LabelW - 2.0f);
	float Y = Visual.m_Rect.y - LabelH - 3.0f;
	if(Y < 2.0f)
		Y = minimum(Height - LabelH - 2.0f, Visual.m_Rect.y + Visual.m_Rect.h + 3.0f);

	CUIRect LabelRect = {X, Y, LabelW, LabelH};
	Graphics()->DrawRect(LabelRect.x, LabelRect.y, LabelRect.w, LabelRect.h, ColorRGBA(0.0f, 0.0f, 0.0f, 0.78f), IGraphics::CORNER_ALL, 5.0f);
	Ui()->DoLabel(&LabelRect, aLabel, FontSize, TEXTALIGN_MC);
}

void CHudEditor::RenderChatExtraPreview(const SModuleVisual &Visual) const
{
	CUIRect Inner = Visual.m_Rect;
	Inner.Margin(3.0f, &Inner);
	if(Inner.w < 24.0f || Inner.h < 22.0f)
		return;

	const float SliderW = 3.0f;
	CUIRect Content, Slider;
	Inner.VSplitRight(SliderW, &Content, &Slider);
	Graphics()->DrawRect(Slider.x, Slider.y, Slider.w, Slider.h, ColorRGBA(1.0f, 1.0f, 1.0f, 0.16f), IGraphics::CORNER_ALL, 2.0f);
	Graphics()->DrawRect(Slider.x, Slider.y + Slider.h * 0.35f, Slider.w, Slider.h * 0.25f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.36f), IGraphics::CORNER_ALL, 2.0f);

	CUIRect InputRow;
	Content.HSplitBottom(11.0f, &Content, &InputRow);
	const float TranslateButtonSize = 9.0f;
	CUIRect InputRect, TranslateButtonRect;
	InputRow.VSplitRight(TranslateButtonSize + 2.0f, &InputRect, &TranslateButtonRect);
	Graphics()->DrawRect(InputRect.x, InputRect.y, InputRect.w, InputRect.h, ColorRGBA(1.0f, 1.0f, 1.0f, 0.12f), IGraphics::CORNER_ALL, 2.0f);
	Graphics()->DrawRect(TranslateButtonRect.x, TranslateButtonRect.y, TranslateButtonRect.w, TranslateButtonRect.h, ColorRGBA(0.35f, 0.68f, 1.0f, 0.36f), IGraphics::CORNER_ALL, 2.0f);
	Ui()->DoLabel(&TranslateButtonRect, "T", 6.0f, TEXTALIGN_MC);

	for(int i = 0; i < 4; ++i)
	{
		const float LineY = Content.y + 2.0f + i * 5.0f;
		const float LineW = std::clamp(Content.w - i * 6.0f, 14.0f, Content.w);
		Graphics()->DrawRect(Content.x + 1.0f, LineY, LineW, 2.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.18f), IGraphics::CORNER_ALL, 1.0f);
	}
}

void CHudEditor::RenderModulePreview(const SModuleVisual &Visual) const
{
	const CUIRect &Rect = Visual.m_Rect;
	if(Rect.w <= 0.0f || Rect.h <= 0.0f)
		return;

	const bool LivePreview = IsLivePreviewModule(Visual.m_Module);
	ColorRGBA Fill = Visual.m_Editable ? ColorRGBA(0.22f, 0.37f, 0.56f, 0.26f) : ColorRGBA(0.25f, 0.25f, 0.25f, 0.22f);
	if(LivePreview)
		Fill = Visual.m_Editable ? ColorRGBA(0.22f, 0.37f, 0.56f, 0.10f) : ColorRGBA(0.25f, 0.25f, 0.25f, 0.08f);
	if(Visual.m_IsFallbackPreview)
		Fill = ColorRGBA(0.30f, 0.26f, 0.20f, 0.20f);
	if(!Visual.m_Enabled)
		Fill = ColorRGBA(0.12f, 0.14f, 0.18f, LivePreview ? 0.22f : 0.30f);
	Graphics()->DrawRect(Rect.x, Rect.y, Rect.w, Rect.h, Fill, Visual.m_Corners, Visual.m_Rounding);
	if(LivePreview)
	{
		if(!Visual.m_Enabled)
			Graphics()->DrawRect(Rect.x, Rect.y, Rect.w, Rect.h, ColorRGBA(0.02f, 0.03f, 0.04f, 0.30f), Visual.m_Corners, Visual.m_Rounding);
		return;
	}

	auto DrawPreviewRows = [&](const CUIRect &Area, int RowCount, float RowHeight, float RowGap, float Alpha) {
		CUIRect Inner = Area;
		Inner.Margin(3.0f, &Inner);
		for(int i = 0; i < RowCount; ++i)
		{
			const float y = Inner.y + i * (RowHeight + RowGap);
			if(y + RowHeight > Inner.y + Inner.h)
				break;
			const float RowWidth = maximum(10.0f, Inner.w - i * 6.0f);
			Graphics()->DrawRect(Inner.x + 1.0f, y, RowWidth, RowHeight, ColorRGBA(1.0f, 1.0f, 1.0f, Alpha), IGraphics::CORNER_ALL, 1.5f);
		}
	};

	if(Visual.m_Module == HudLayout::MODULE_CHAT)
	{
		RenderChatExtraPreview(Visual);
		return;
	}
	if(Visual.m_Module == HudLayout::MODULE_VOTES)
	{
		CUIRect Inner = Rect;
		Inner.Margin(3.0f, &Inner);
		Graphics()->DrawRect(Inner.x, Inner.y, Inner.w, 6.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.16f), IGraphics::CORNER_ALL, 2.0f);
		Graphics()->DrawRect(Inner.x, Inner.y + 9.0f, Inner.w * 0.62f, 4.0f, ColorRGBA(0.4f, 0.9f, 0.5f, 0.4f), IGraphics::CORNER_ALL, 2.0f);
		Graphics()->DrawRect(Inner.x + Inner.w * 0.62f, Inner.y + 9.0f, Inner.w * 0.38f, 4.0f, ColorRGBA(0.9f, 0.45f, 0.45f, 0.4f), IGraphics::CORNER_ALL, 2.0f);
		return;
	}
	if(Visual.m_Module == HudLayout::MODULE_GAME_TIMER)
	{
		CUIRect TimerRect = Rect;
		Ui()->DoLabel(&TimerRect, "00:37", 8.0f, TEXTALIGN_MC);
		return;
	}
	if(Visual.m_Module == HudLayout::MODULE_SCORE)
	{
		CUIRect Inner = Rect;
		Inner.Margin(4.0f, &Inner);
		Graphics()->DrawRect(Inner.x, Inner.y, Inner.w, 18.0f, ColorRGBA(1.0f, 0.25f, 0.25f, 0.24f), IGraphics::CORNER_L, 3.0f);
		Graphics()->DrawRect(Inner.x, Inner.y + 22.0f, Inner.w, 18.0f, ColorRGBA(0.25f, 0.48f, 1.0f, 0.24f), IGraphics::CORNER_L, 3.0f);
		return;
	}
	if(Visual.m_Module == HudLayout::MODULE_MOVEMENT_INFO)
	{
		DrawPreviewRows(Rect, 6, 2.2f, 4.8f, 0.20f);
		return;
	}
	if(Visual.m_Module == HudLayout::MODULE_FPS)
	{
		CUIRect TextRect = Rect;
		TextRect.Margin(2.5f, &TextRect);
		Ui()->DoLabel(&TextRect, "144 FPS", 6.0f, TEXTALIGN_MC);
		return;
	}
	if(Visual.m_Module == HudLayout::MODULE_PING)
	{
		CUIRect TextRect = Rect;
		TextRect.Margin(2.5f, &TextRect);
		Ui()->DoLabel(&TextRect, "24 ms", 6.0f, TEXTALIGN_MC);
		return;
	}
	if(Visual.m_Module == HudLayout::MODULE_LOCAL_TIME)
	{
		CUIRect TextRect = Rect;
		TextRect.Margin(2.5f, &TextRect);
		Ui()->DoLabel(&TextRect, "18:42", 6.0f, TEXTALIGN_MC);
		return;
	}
	if(Visual.m_Module == HudLayout::MODULE_SPECTATOR_COUNT)
	{
		CUIRect Inner = Rect;
		Inner.Margin(3.0f, &Inner);
		Graphics()->DrawRect(Inner.x, Inner.y + 1.5f, 6.0f, 6.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.24f), IGraphics::CORNER_ALL, 3.0f);
		CUIRect TextRect = Inner;
		TextRect.x += 8.5f;
		Ui()->DoLabel(&TextRect, "5 spectators", 5.8f, TEXTALIGN_ML);
		return;
	}
	if(Visual.m_Module == HudLayout::MODULE_MINI_VOTE)
	{
		CUIRect Inner = Rect;
		Inner.Margin(3.0f, &Inner);
		Graphics()->DrawRect(Inner.x, Inner.y, Inner.w, 6.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.16f), IGraphics::CORNER_ALL, 2.0f);
		Graphics()->DrawRect(Inner.x, Inner.y + 10.0f, Inner.w * 0.52f, 4.0f, ColorRGBA(0.4f, 0.9f, 0.5f, 0.4f), IGraphics::CORNER_ALL, 2.0f);
		Graphics()->DrawRect(Inner.x + Inner.w * 0.52f, Inner.y + 10.0f, Inner.w * 0.48f, 4.0f, ColorRGBA(0.9f, 0.45f, 0.45f, 0.4f), IGraphics::CORNER_ALL, 2.0f);
		DrawPreviewRows(Inner, 3, 2.0f, 3.5f, 0.18f);
		return;
	}
	if(Visual.m_Module == HudLayout::MODULE_NOTIFY_LAST)
	{
		CUIRect TextRect = Rect;
		TextRect.Margin(3.0f, &TextRect);
		Ui()->DoLabel(&TextRect, "You are last alive!", 5.8f, TEXTALIGN_MC);
		return;
	}
	if(Visual.m_Module == HudLayout::MODULE_FROZEN_HUD)
	{
		CUIRect Inner = Rect;
		Inner.Margin(3.0f, &Inner);
		const float DotSize = minimum(Inner.h - 2.0f, 9.0f);
		for(int i = 0; i < 6; ++i)
		{
			const float x = Inner.x + i * (DotSize + 2.2f);
			if(x + DotSize > Inner.x + Inner.w)
				break;
			Graphics()->DrawRect(x, Inner.y + (Inner.h - DotSize) * 0.5f, DotSize, DotSize, ColorRGBA(1.0f, 1.0f, 1.0f, i % 2 == 0 ? 0.28f : 0.14f), IGraphics::CORNER_ALL, DotSize * 0.5f);
		}
		return;
	}
	if(Visual.m_Module == HudLayout::MODULE_LOCK_CAM)
	{
		CUIRect Inner = Rect;
		Inner.Margin(2.0f, &Inner);
		Graphics()->DrawRect(Inner.x + Inner.w * 0.15f, Inner.y + Inner.h * 0.35f, Inner.w * 0.70f, 1.8f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.24f), IGraphics::CORNER_ALL, 1.0f);
		Graphics()->DrawRect(Inner.x + Inner.w * 0.40f, Inner.y + Inner.h * 0.10f, Inner.w * 0.20f, Inner.h * 0.20f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.24f), IGraphics::CORNER_ALL, 1.0f);
		return;
	}
	if(Visual.m_Module == HudLayout::MODULE_KILLFEED)
	{
		CUIRect Inner = Rect;
		Inner.Margin(3.0f, &Inner);
		for(int i = 0; i < 3; ++i)
		{
			const float RowY = Inner.y + i * 7.0f;
			Graphics()->DrawRect(Inner.x, RowY, 16.0f, 2.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.22f), IGraphics::CORNER_ALL, 1.0f);
			Graphics()->DrawRect(Inner.x + 19.0f, RowY, 4.0f, 2.0f, ColorRGBA(0.95f, 0.55f, 0.55f, 0.36f), IGraphics::CORNER_ALL, 1.0f);
			Graphics()->DrawRect(Inner.x + 26.0f, RowY, minimum(Inner.w - 26.0f, 17.0f), 2.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.22f), IGraphics::CORNER_ALL, 1.0f);
		}
		return;
	}

	DrawPreviewRows(Rect, 4, 2.2f, 4.0f, 0.20f);
	if(!Visual.m_Enabled)
		Graphics()->DrawRect(Rect.x, Rect.y, Rect.w, Rect.h, ColorRGBA(0.02f, 0.03f, 0.04f, 0.26f), Visual.m_Corners, Visual.m_Rounding);
}

void CHudEditor::RenderOverlay(vec2 MousePos)
{
	const float Width = HudWidth();
	const float Height = HudHeight();
	Graphics()->MapScreen(0.0f, 0.0f, Width, Height);
	Graphics()->TextureClear();
	Graphics()->DrawRect(0.0f, 0.0f, Width, Height, ColorRGBA(0.0f, 0.0f, 0.0f, 0.38f), IGraphics::CORNER_ALL, 0.0f);

	// Draw true HUD previews first, then add interactive editor overlays on top.
	GameClient()->m_Chat.RenderHud(true);
	GameClient()->m_Voting.RenderHud(true);
	GameClient()->m_Hud.RenderLocalTimePreview();
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
				HudLayout::ResetSettings(aVisuals[i].m_Module);
		}
		m_Dragging = false;
		m_PressedModule = HudLayout::MODULE_COUNT;
		m_SelectedModule = HudLayout::MODULE_COUNT;
		m_PressedOnReset = true;
		Ui()->ClosePopupMenus();
	}
	else if(LeftClicked)
	{
		m_PressedOnReset = false;
		m_PressMousePos = MousePos;
		m_SelectedModule = m_HoveredModule;
		m_PressedModule = (m_HoveredModule != HudLayout::MODULE_COUNT && IsEditableModule(m_HoveredModule)) ? m_HoveredModule : HudLayout::MODULE_COUNT;
		if(m_PressedModule != HudLayout::MODULE_COUNT)
		{
			const SModuleVisual Visual = GetModuleVisual(m_PressedModule);
			m_DragMouseOffset = MousePos - vec2(Visual.m_Rect.x, Visual.m_Rect.y);
		}
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
		m_PressedOnReset = false;
	}

	m_MouseDownLast = LeftDown;
	m_RightMouseDownLast = RightDown;

	RenderOverlay(MousePos);
	Ui()->FinishCheck();
	Ui()->ClearHotkeys();
}
