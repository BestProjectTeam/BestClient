/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_CURSOR_TRAIL_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_CURSOR_TRAIL_H

#include <base/types.h>
#include <base/vmath.h>

#include <engine/graphics.h>

#include <game/client/component.h>

#include <vector>

class CCursorTrail : public CComponent
{
	struct SCursorTrailPoint
	{
		vec2 m_Pos;
		float m_Age;
	};

	IGraphics::CTextureHandle m_Texture;
	char m_aPath[IO_MAX_PATH_LENGTH] = {};
	std::vector<SCursorTrailPoint> m_vPoints;
	int m_Mode = -1;
	int m_Frames = -1;
	int m_DisableMovement = -1;
	float m_SampleTime = 0.0f;
	vec2 m_PreviousPlayerPos;
	bool m_AnchorValid = false;
	int m_AnchorSource = -1;

public:
	int Sizeof() const override { return sizeof(*this); }

	void OnInit() override;
	void OnReset() override;

	void Reload();
	void Render(vec2 TrailTargetPos, float Scale, float Alpha, int CurWeapon, int HudQuadContainerIndex, const int *pCursorOffsets);
};

#endif
