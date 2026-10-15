/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_HOOKLIGHTNING_H
#define GAME_CLIENT_COMPONENTS_HOOKLIGHTNING_H
#include "fgf_glow.h"
#include "hookgrip.h"

#include <base/color.h>
#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>

class CHookLightning : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnReset() override;
	void OnRender() override;

	static int BuildBolt(vec2 Start, vec2 End, float Seed, float Spread, int Segments, vec2 *pPoints, float *pScales);
	static void Bolt(IGraphics *pGraphics, vec2 Start, vec2 End, float Seed, float Spread, float Width, ColorRGBA Color, int Segments = 10);

private:
	static void DrawBolt(IGraphics *pGraphics, const vec2 *pPoints, const float *pScales, int Count, float Width, ColorRGBA Color);

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
	bool m_WasActive = false;

	void Strike(int Owner, const CHookGripTracker::CGrip &Grip, float Scale);
	void Charged(int Owner, const CHookGripTracker::CGrip &Grip, float Scale);
	void Snap(int Owner, const CHookGripTracker::CGrip &Grip, float Scale);
	void EmitSparks(int Owner, const CHookGripTracker::CGrip &Grip, float Passed, float Scale);
	void AddSpark(vec2 Pos, vec2 Vel, float Size, float LifeSpan, float Alpha, ColorRGBA Color);
	void Draw(float Scale, float Time);
};
#endif
