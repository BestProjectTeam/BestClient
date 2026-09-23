/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_HOOKRAINBOW_H
#define GAME_CLIENT_COMPONENTS_HOOKRAINBOW_H
#include <base/color.h>
#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>
#include "fgf_glow.h"
#include "hookgrip.h"

class CHookRainbow : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnReset() override;
	void OnRender() override;

private:
	static constexpr int NUM_PETALS = 7;
	static constexpr int NUM_RAYS = 10;
	static constexpr int MAX_FALLING = 160;

	struct CBloomState
	{
		float m_Spin = 1.0f;
		float m_Angle = 0.0f;
		float m_HueOffset = 0.0f;
		float m_GlitterAccumulator = 0.0f;
		float m_Flash = 0.0f;
	};

	struct CFalling
	{
		vec2 m_Pos;
		vec2 m_Vel;
		float m_Hue;
		float m_Life;
		float m_LifeSpan;
		float m_Length;
		float m_Width;
	};

	IGraphics::CTextureHandle m_GlowTexture;
	IGraphics::CTextureHandle m_RingTexture;
	IGraphics::CTextureHandle m_BeamTexture;

	CHookGripTracker m_Tracker;
	CBloomState m_aStates[MAX_CLIENTS];
	CGlowParticles m_Glitter;
	CFalling m_aFalling[MAX_FALLING];
	int m_NumFalling = 0;
	float m_LastTime = 0.0f;

	void Bloom(int Owner, const CHookGripTracker::CGrip &Grip, float Scale);
	void Charged(int Owner, const CHookGripTracker::CGrip &Grip, float Scale);
	void Scatter(int Owner, const CHookGripTracker::CGrip &Grip, float Scale);
	void EmitGlitter(int Owner, const CHookGripTracker::CGrip &Grip, float Passed, float Scale);
	void AddGlitter(vec2 Pos, vec2 Vel, float Hue, float Size, float LifeSpan, float Alpha);

	void Update(float Passed);
	void DrawArc(vec2 Center, float Radius, float Angle, float Arc, float Width, ColorRGBA Color);
	void Draw(float Scale, float Time);
};
#endif
