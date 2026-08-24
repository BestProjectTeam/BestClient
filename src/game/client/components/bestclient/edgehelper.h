#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_EDGEHELPER_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_EDGEHELPER_H

#include <engine/console.h>

#include <game/client/component.h>
#include <game/client/ui_rect.h>

class CEdgeHelper : public CComponent
{
	bool m_Active = false;
	int m_PosX = 0;

	static void ConToggleEdgeHelper(IConsole::IResult *, void *pUserData);

	void DoIconButton(CUIRect *pRect, const char *pIcon, float TextSize, ColorRGBA IconColor) const;
	CUIRect GetRect(bool ForcePreview) const;
	int GetPositionEdgeHelper(int ClientId, int Conn) const;
	void RenderEdgeHelper(bool ForcePreview);
	void RenderEdgeHelperEdgeInfo(CUIRect *pBase, float Scale);
	void RenderEdgeHelperJumpInfo(CUIRect *pBase, float Scale);

public:
	int Sizeof() const override { return sizeof(*this); }

	void SetActive(bool Active);
	bool IsActive() const { return m_Active; }
	CUIRect GetHudEditorRect() const { return GetRect(true); }
	void RenderPreview() { RenderEdgeHelper(true); }

	void OnConsoleInit() override;
	void OnRelease() override;
	void OnRender() override;
	void OnReset() override;
};

#endif
