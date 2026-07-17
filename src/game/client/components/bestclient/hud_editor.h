/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_HUD_EDITOR_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_HUD_EDITOR_H

#include <game/client/component.h>
#include <game/client/components/hud_layout.h>
#include <game/client/ui.h>

class CHudEditor : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnRender() override;
	bool OnInput(const IInput::CEvent &Event) override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	void OnStateChange(int NewState, int OldState) override;

	void Activate();
	void Deactivate();
	bool IsActive() const { return m_Active; }

private:
	// Headroom above the modules currently wired (Score, Movement Info, Dummy Actions,
	// Local Time, Spectator Count, Frozen HUD, Chat, Votes, Finish Prediction, Music
	// Player, Keystrokes, Voice) so new modules can be added to CollectModuleVisuals
	// without bumping this constant.
	static constexpr int MAX_MODULE_VISUALS = 16;

	struct SModuleVisual
	{
		HudLayout::EModule m_Module = HudLayout::MODULE_SCORE;
		CUIRect m_Rect{};
		int m_Corners = IGraphics::CORNER_ALL;
		float m_Rounding = 6.0f;
		bool m_UsesBottomAnchor = false;
		bool m_Editable = false;
		bool m_Enabled = true;
		bool m_IsFallbackPreview = false;
	};

	// One module's in-flight "reset position"/"reset scale"/"reset all" animation - see
	// RESET_KIND_* in hud_editor.cpp. Multiple can run at once (e.g. the top-left
	// "Reset All" button animates every editable module simultaneously).
	struct SResetAnim
	{
		HudLayout::EModule m_Module = HudLayout::MODULE_COUNT;
		CUIRect m_StartRect{};
		CUIRect m_TargetRect{};
		int m_StartScale = 100;
		int m_TargetScale = 100;
		int m_Kind = 0;
		float m_Phase = 0.0f;
	};

	static CUi::EPopupMenuFunctionResult PopupModuleSettings(void *pContext, CUIRect View, bool Active);

	bool m_Active = false;
	bool m_MouseDownLast = false;
	bool m_RightMouseDownLast = false;
	bool m_Dragging = false;
	bool m_PressedOnReset = false;
	HudLayout::EModule m_PressedModule = HudLayout::MODULE_COUNT;
	HudLayout::EModule m_HoveredModule = HudLayout::MODULE_COUNT;
	HudLayout::EModule m_SelectedModule = HudLayout::MODULE_COUNT;
	HudLayout::EModule m_ResizingModule = HudLayout::MODULE_COUNT;
	// -1 = none, else 0=TL, 1=TR, 2=BL, 3=BR; see the RESIZE_CORNER_* constants in hud_editor.cpp.
	int m_ResizeCorner = -1;
	CUIRect m_ResizeAnchorRect{};
	int m_ResizeStartScale = 100;
	vec2 m_DragMouseOffset = vec2(0.0f, 0.0f);
	vec2 m_PressMousePos = vec2(0.0f, 0.0f);
	SPopupMenuId m_SettingsPopupId;
	bool m_PopupGrowFromRight = false;
	float m_PopupRevealPhase = 0.0f;
	bool m_SuppressCloseAnim = false;
	bool m_PopupClosing = false;
	float m_PopupClosePhase = 0.0f;
	CUIRect m_LastPopupOuterRect{};
	// Grabbing a module manually cancels its own entry (see CancelResetAnimation() in
	// OnRender()); an empty array just means nothing is mid-reset right now.
	SResetAnim m_aResetAnims[MAX_MODULE_VISUALS];
	int m_ResetAnimCount = 0;
	CButtonContainer m_ResetAllButton;
	CButtonContainer m_ToggleModuleButton;
	CButtonContainer m_ResetPositionButton;
	CButtonContainer m_ResetScaleButton;
	CButtonContainer m_ResetSettingsButton;

	SModuleVisual GetModuleVisual(HudLayout::EModule Module) const;
	CUIRect GetFallbackModuleRect(HudLayout::EModule Module) const;
	float HudWidth() const;
	float HudHeight() const;
	bool IsEditableModule(HudLayout::EModule Module) const;
	bool IsModuleEnabled(HudLayout::EModule Module) const;
	void RenderModulePreview(const SModuleVisual &Visual) const;
	void CollectModuleVisuals(SModuleVisual *pOut, int &Count) const;
	HudLayout::EModule HitTestModule(vec2 MousePos) const;
	HudLayout::EModule HitTestResizeHandle(vec2 MousePos, int &OutCorner) const;
	void UpdateDragging(vec2 MousePos);
	void UpdateResizing(vec2 MousePos);
	void StartResetAnimation(HudLayout::EModule Module, int Kind);
	void CancelResetAnimation(HudLayout::EModule Module);
	SResetAnim *FindOrAddResetAnim(HudLayout::EModule Module);
	void UpdateResetAnimation();
	void RenderOverlay(vec2 MousePos);
	void RenderModuleOutline(const SModuleVisual &Visual, bool Hovered, bool Selected) const;
	void RenderResizeHandles(const SModuleVisual &Visual) const;
	void RenderClosingPopupFrame() const;
	void RenderModuleLabel(const SModuleVisual &Visual) const;
	void OpenModuleSettings(const SModuleVisual &Visual);
	void ApplyDraggedPosition(HudLayout::EModule Module, const CUIRect &Rect);
	CUIRect SnapRect(const CUIRect &Rect, HudLayout::EModule DraggedModule) const;
};

#endif
