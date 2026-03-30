#ifndef GAME_CLIENT_COMPONENTS_NAMEPLATES_H
#define GAME_CLIENT_COMPONENTS_NAMEPLATES_H

#include <base/color.h>
#include <base/vmath.h>

#include <game/client/component.h>
#include <game/client/ui_rect.h>

struct CNetObj_PlayerInfo;

class CNamePlates : public CComponent
{
private:
	class CNamePlatesData;
	CNamePlatesData *m_pData;

public:
	void RenderNamePlateGame(vec2 Position, const CNetObj_PlayerInfo *pPlayerInfo, float Alpha);
	void RenderNamePlatePreview(const CUIRect &PreviewRect, int Dummy);
	void ResetNamePlateElementOffsets();
	void ResetNamePlates();
	int Sizeof() const override { return sizeof(*this); }
	void OnWindowResize() override;
	void OnRender() override;
	CNamePlates();
	~CNamePlates() override;

	// BestClient
	float GetNamePlateOffset(int ClientId) const;
};

#endif
