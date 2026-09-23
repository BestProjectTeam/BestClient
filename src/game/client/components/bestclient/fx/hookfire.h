/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_HOOKFIRE_H
#define GAME_CLIENT_COMPONENTS_HOOKFIRE_H
#include <base/color.h>
#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>
#include "fgf_glow.h"
#include "hookgrip.h"

class CHookFire : public CComponent
{
public:
	static constexpr int NUM_VARIANTS = 4;

	struct CPalette
	{
		ColorRGBA m_Outer;
		ColorRGBA m_Middle;
		ColorRGBA m_Inner;
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnReset() override;
	void OnRender() override;

	static CPalette Palette();

	void EmitFlame(vec2 Pos, vec2 Vel, float Size, float LifeSpan, float Alpha, float ColumnX, float Pull);
	void EmitEmber(vec2 Pos, vec2 Vel, float Alpha);

private:
	static constexpr int MAX_FLAMES = 1024;
	static constexpr int MAX_SCORCHES = 16;

	struct CFlameParticle
	{
		vec2 m_Pos;
		vec2 m_Vel;
		float m_Life;
		float m_LifeSpan;
		float m_Size;
		float m_Phase;
		float m_Alpha;
		float m_ColumnX;
		float m_Pull;
		int m_Variant;
	};

	struct CFlameQuad
	{
		vec2 m_Base;
		vec2 m_Dir;
		float m_Height;
		float m_Width;
		float m_Heat;
		int m_Variant;
		bool m_Flip;
		float m_Alpha;
	};

	struct CFireState
	{
		float m_Appear = 0.0f;
		float m_Flare = 0.0f;
		float m_FlameAccumulator = 0.0f;
		float m_EmberAccumulator = 0.0f;
		float m_SmokeAccumulator = 0.0f;
	};

	struct CScorch
	{
		vec2 m_Pos;
		float m_Life;
		float m_Size;
		float m_Alpha;
	};

	IGraphics::CTextureHandle m_FlameTexture;
	IGraphics::CTextureHandle m_GlowTexture;
	IGraphics::CTextureHandle m_RingTexture;

	CHookGripTracker m_Tracker;
	CFireState m_aStates[MAX_CLIENTS];
	CFlameParticle m_aFlames[MAX_FLAMES];
	int m_NumFlames = 0;
	CScorch m_aScorches[MAX_SCORCHES];
	int m_NumScorches = 0;
	CGlowParticles m_Embers;
	CGlowParticles m_Smoke;
	CFlameQuad m_aQuads[MAX_FLAMES];
	float m_LastTime = 0.0f;

	static vec2 FlameDirection(const CHookGripTracker::CGrip &Grip);

	void Ignite(int Owner, const CHookGripTracker::CGrip &Grip, float Scale);
	void Roar(int Owner, const CHookGripTracker::CGrip &Grip, float Scale);
	void DieDown(const CHookGripTracker::CGrip &Grip, float Scale);
	void Burn(int Owner, const CHookGripTracker::CGrip &Grip, float Passed, float Scale);
	void EmitSmoke(vec2 Pos, vec2 Vel, float Size, float Alpha);

	void UpdateFlames(float Passed, float Time);
	void DrawFlames(const CPalette &Palette, float Time);
	void Draw(const CPalette &Palette, float Scale, float Time);
};
#endif
