/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_KEYSTROKES_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_KEYSTROKES_H

#include <engine/graphics.h>

#include <game/client/component.h>
#include <game/client/ui_rect.h>

#include <generated/protocol.h>

float GetKeystrokesKeyboardPresetWidthHudPx(int Preset);

class CKeystrokes : public CComponent
{
	IGraphics::CTextureHandle m_KeyboardTexture;
	IGraphics::CTextureHandle m_MouseTexture;
	int64_t m_Mouse1EndTime = 0;
	int64_t m_WheelUpEndTime = 0;
	int64_t m_WheelDownEndTime = 0;

	CUIRect GetKeyboardRectInternal(bool IgnoreModuleEnabled) const;
	CUIRect GetMouseRectInternal(bool IgnoreModuleEnabled) const;
	void RenderKeyboardInternal(bool ForcePreview, bool IgnoreModuleEnabled);
	void RenderMouseInternal(bool ForcePreview, bool IgnoreModuleEnabled);
	int GetTrackedClientId() const;
	const CNetObj_PlayerInput *GetTrackedInput() const;

public:
	int Sizeof() const override { return sizeof(*this); }

	CUIRect GetKeyboardHudEditorRect() const;
	CUIRect GetMouseHudEditorRect() const { return GetMouseRectInternal(true); }
	void RenderKeyboardPreview() { RenderKeyboardInternal(true, true); }
	void RenderMousePreview() { RenderMouseInternal(true, true); }

	void OnInit() override;
	void OnReset() override;
	void OnRender() override;
};

#endif
