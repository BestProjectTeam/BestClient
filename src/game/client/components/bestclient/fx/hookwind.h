/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_HOOKWIND_H
#define GAME_CLIENT_COMPONENTS_HOOKWIND_H
#include <base/color.h>
#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>
#include "fgf_glow.h"
#include "hookgrip.h"

class CHookWind : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnReset() override;
	void OnRender() override;

private:
	enum
	{
		MAX_GUSTS = 256,
		MAX_LEAVES = 128,
		GUST_POINTS = 12,
		NUM_EYE_ARCS = 3,
	};

	struct CWindState
	{
		float m_Spin = 1.0f;
		float m_EyeAngle = 0.0f;
		float m_GustAccumulator = 0.0f;
		float m_LeafAccumulator = 0.0f;
		float m_DustAccumulator = 0.0f;
	};

	struct CGust
	{
		vec2 m_Center;
		float m_Radius;
		float m_RadialSpeed;
		float m_Angle;
		float m_AngularSpeed;
		float m_Arc;
		float m_Width;
		float m_Life;
		float m_LifeSpan;
		float m_Alpha;
	};

	struct CLeaf
	{
		int m_Owner;
		vec2 m_Pos;
		vec2 m_Vel;
		float m_Radius;
		float m_RadialSpeed;
		float m_Angle;
		float m_Rotation;
		float m_RotationSpeed;
		float m_FlutterPhase;
		float m_Size;
		float m_Life;
		float m_LifeSpan;
		ColorRGBA m_Color;
	};

	IGraphics::CTextureHandle m_GlowTexture;
	IGraphics::CTextureHandle m_BeamTexture;
	IGraphics::CTextureHandle m_LeafTexture;

	CHookGripTracker m_Tracker;
	CWindState m_aStates[MAX_CLIENTS];
	CGust m_aGusts[MAX_GUSTS];
	int m_NumGusts = 0;
	CLeaf m_aLeaves[MAX_LEAVES];
	int m_NumLeaves = 0;
	CGlowParticles m_Dust;
	float m_LastTime = 0.0f;

	void AddGust(vec2 Center, float Radius, float RadialSpeed, float AngularSpeed, float Arc, float Width, float LifeSpan, float Alpha, float Angle);
	CLeaf *AddLeaf(int Owner, vec2 Pos, float Scale);

	void Burst(int Owner, const CHookGripTracker::CGrip &Grip, float Scale);
	void GustRing(int Owner, const CHookGripTracker::CGrip &Grip, float Scale);
	void Disperse(int Owner, const CHookGripTracker::CGrip &Grip, float Scale);
	void EmitVortex(int Owner, const CHookGripTracker::CGrip &Grip, float Passed, float Scale);

	void Update(float Passed);
	void DrawArc(vec2 Center, float Radius, float Angle, float Arc, float Width, float Flare, float Spin, ColorRGBA Color);
	void Draw(ColorRGBA Tint, float Scale);
};
#endif
