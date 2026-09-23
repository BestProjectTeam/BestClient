/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_HOOKLIGHTNING_H
#define GAME_CLIENT_COMPONENTS_HOOKLIGHTNING_H
#include <base/color.h>
#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>
#include "fgf_glow.h"
#include "hookgrip.h"

class CHookLightning : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnReset() override;
	void OnRender() override;

	static void Bolt(IGraphics *pGraphics, vec2 Start, vec2 End, float Seed, float Spread, float Width, ColorRGBA Color, int Segments = 10);

private:
	static constexpr int NUM_ARCS = 5;

	struct CBoltState
	{
		float m_Flash = 0.0f;
		float m_SparkAccumulator = 0.0f;
		float m_Flicker = 1.0f;
		float m_Seed = 0.0f;
	};

	IGraphics::CTextureHandle m_GlowTexture;
	IGraphics::CTextureHandle m_RingTexture;
	IGraphics::CTextureHandle m_BeamTexture;

	CHookGripTracker m_Tracker;
	CBoltState m_aStates[MAX_CLIENTS];
	CGlowParticles m_Sparks;
	float m_LastTime = 0.0f;

	void Strike(int Owner, const CHookGripTracker::CGrip &Grip, float Scale);
	void Charged(int Owner, const CHookGripTracker::CGrip &Grip, float Scale);
	void Snap(int Owner, const CHookGripTracker::CGrip &Grip, float Scale);
	void EmitSparks(int Owner, const CHookGripTracker::CGrip &Grip, float Passed, float Scale);
	void AddSpark(vec2 Pos, vec2 Vel, float Size, float LifeSpan, float Alpha, ColorRGBA Color);
	void Draw(float Scale, float Time);
};
#endif
