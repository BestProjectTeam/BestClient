#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_MUSIC_PLAYER_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_MUSIC_PLAYER_H

#include <game/client/component.h>
#include <game/client/ui_rect.h>

#include <cstdint>
#include <memory>

class CMusicPlayer : public CComponent
{
public:
	struct SNowPlayingInfo
	{
		bool m_Valid = false;
		bool m_Playing = false;
		int64_t m_DurationMs = 0;
		int64_t m_PositionMs = 0;
		uint32_t m_Seed = 0;
	};

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
	float GetHudPushOffsetForRect(const CUIRect &Rect, float CanvasWidth, float Padding = 0.0f) const;
	float GetHudPushDownOffsetForRect(const CUIRect &Rect, float CanvasHeight, float Padding = 0.0f) const;
	bool GetNowPlayingInfo(SNowPlayingInfo &Out) const;

private:
	class CImpl;
	std::unique_ptr<CImpl> m_pImpl;

	void RenderMusicPlayer(bool ForcePreview);
	void EnsureImpl();
};

#endif
