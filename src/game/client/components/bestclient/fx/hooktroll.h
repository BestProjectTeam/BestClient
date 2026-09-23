/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_HOOKTROLL_H
#define GAME_CLIENT_COMPONENTS_HOOKTROLL_H
#include <base/color.h>
#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>
#include "hookgrip.h"

class CHookTroll : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnReset() override;
	void OnRender() override;

	void DrawFace(vec2 Pos, float Size, float Angle, bool Flip, float Alpha);
	void DrawGoo(const vec2 *pPoints, const float *pWidths, int NumPoints, float Alpha);
	void EmitDrip(vec2 Pos, vec2 Vel, float Radius, float Alpha);

private:
	static constexpr int MAX_DRIPS = 256;
	static constexpr int MAX_FACES = 8;
	static constexpr int SPLAT_POINTS = 28;

	struct CDrip
	{
		vec2 m_Pos;
		vec2 m_Vel;
		float m_Radius;
		float m_Life;
		float m_Alpha;
	};

	struct CLooseFace
	{
		vec2 m_Pos;
		vec2 m_Vel;
		float m_Size;
		float m_Angle;
		float m_Spin;
		float m_Life;
		float m_Alpha;
		bool m_Flip;
	};

	struct CTrollState
	{
		float m_Seed = 0.0f;
		float m_FaceScale = 0.0f;
		float m_FaceScaleVel = 0.0f;
		float m_Splat = 0.0f;
		float m_DripAccumulator = 0.0f;
		float m_Laugh = 0.0f;
	};

	IGraphics::CTextureHandle m_FaceTexture;
	IGraphics::CTextureHandle m_DiscTexture;

	CHookGripTracker m_Tracker;
	CTrollState m_aStates[MAX_CLIENTS];
	CDrip m_aDrips[MAX_DRIPS];
	int m_NumDrips = 0;
	CLooseFace m_aFaces[MAX_FACES];
	int m_NumFaces = 0;
	float m_LastTime = 0.0f;

	void DrawSplat(vec2 Center, vec2 Normal, float Radius, float Seed, float Alpha);
};
#endif
