#ifndef GAME_CLIENT_COMPONENTS_RCLIENT_EDGEHELPER_H
#define GAME_CLIENT_COMPONENTS_RCLIENT_EDGEHELPER_H

#include "game/client/ui_rect.h"

#include <engine/console.h>

#include <game/client/component.h>
class CEdgeHelper : public CComponent
{
	bool m_Active = false;

	int m_Pos_x;
	void DoIconButton(CUIRect *pRect, const char *pIcon, float TextSize, ColorRGBA IconColor) const;
	static void ConToggleEdgeHelper(IConsole::IResult *pResult, void *pUserData);

	std::vector<int> values = {41, 31, 28, 25, 16, 13, 56, 62, 63, 66, 69, 72, 81, 84};

	CUIRect m_Rect;

	void RIReset()
	{
	}

	float GetPositionEdgeHelper(int ClientId, int Conn);

	void RenderEdgeHelper();
	void RenderEdgeHelperEdgeInfo(CUIRect *pBase);
	void RenderEdgeHelperJumpInfo(CUIRect *pBase);

public:
	CEdgeHelper();
	int Sizeof() const override { return sizeof(*this); }

	void SetActive(bool Active);

	void OnReset() override;
	void OnRender() override;
	void OnConsoleInit() override;
	void OnRelease() override;

	bool IsActive() const;
};

#endif
