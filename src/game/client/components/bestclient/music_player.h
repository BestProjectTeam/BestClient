/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_MUSIC_PLAYER_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_MUSIC_PLAYER_H

#include <base/color.h>

#include <game/client/component.h>
#include <game/client/ui_rect.h>

#include <memory>

class CMusicPlayer : public CComponent
{
public:
	struct SHudReservation
	{
		CUIRect m_Rect{};
		bool m_Visible = false;
		bool m_Active = false;
		float m_PushAmount = 0.0f;
	};

	CMusicPlayer();
	~CMusicPlayer() override;

	int Sizeof() const override { return sizeof(*this); }

	void OnReset() override;
	void OnUpdate() override;
	void OnRender() override;
	void OnShutdown() override;
	void OnWindowResize() override;

	SHudReservation HudReservation() const;
	vec2 GetHudPushOffsetForRect(const CUIRect &Rect, float CanvasWidth, float CanvasHeight, float Padding = 0.0f) const;
	bool GetHudThemeColor(ColorRGBA &Out, bool ForcePreview = false) const;
	CUIRect GetHudEditorRect(bool ForcePreview = false) const;
	void RenderHudEditor(bool ForcePreview);

private:
	class CImpl;
	std::unique_ptr<CImpl> m_pImpl;

	void RenderMusicPlayer(bool ForcePreview);
	void EnsureImpl();
};

#endif
