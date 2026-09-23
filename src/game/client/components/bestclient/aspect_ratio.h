/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_ASPECT_RATIO_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_ASPECT_RATIO_H

#include <base/types.h>

#include <game/client/component.h>

class CAspectRatio : public CComponent
{
	struct SConfig
	{
		int m_Mode = 0;
		int m_Ratio = 0;
		int m_ApplyMode = 1;
		int m_Num = 0;
		int m_Den = 0;

		bool operator==(const SConfig &Other) const
		{
			return m_Mode == Other.m_Mode && m_Ratio == Other.m_Ratio && m_ApplyMode == Other.m_ApplyMode &&
				m_Num == Other.m_Num && m_Den == Other.m_Den;
		}
		bool operator!=(const SConfig &Other) const { return !(*this == Other); }
	};

	bool m_ConfirmSeeded = false;
	bool m_ConfirmActive = false;
	int64_t m_ConfirmDeadline = 0;
	SConfig m_Prev;
	SConfig m_Last;
	bool m_HudAspectDisabled = false;
	bool m_MenuRestoreGraphicsAspect = true;

	static SConfig ReadConfig();
	static bool ConfirmNeeded(const SConfig &Cfg);
	void ResetToPrevious();

public:
	int Sizeof() const override { return sizeof(*this); }

	bool IsBlockedByFng() const;
	bool UseGameNoHudAspect() const;
	bool ShouldApplyCustomAspect(bool IsActiveGameplay) const;

	void BeginFrame();
	void PrepareComponent(const CComponent *pComponent);
	void BeforeFogRect();
	void AfterFogRect();

	void BeginCursorGameAspect();
	void EndCursorGameAspect();

	void UpdateConfirm();
	bool ConfirmWantsInput() const;
	bool ShowConfirmOverlay() const;
	void ApplyMenuUiAspect(bool MenuActive);
	void RenderConfirmOverlay(bool MenuActive);
	void RestoreMenuUiAspect();
};

#endif
