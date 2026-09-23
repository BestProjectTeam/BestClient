/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_HOOKTENTACLE_H
#define GAME_CLIENT_COMPONENTS_HOOKTENTACLE_H
#include <base/color.h>
#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>
#include "fgf_glow.h"
#include "hookgrip.h"

class CHookTentacle : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnReset() override;
	void OnRender() override;

	static float Wobble(float Along01, float Phase, float Amount);

private:
	struct CGripState
	{
		float m_Phase = 0.0f;
		float m_Seed = 0.0f;
		float m_Coil = 0.0f;
		float m_Squeeze = 0.0f;
		float m_SlimeAccumulator = 0.0f;
	};

	IGraphics::CTextureHandle m_GlowTexture;
	IGraphics::CTextureHandle m_RingTexture;
	IGraphics::CTextureHandle m_DiscTexture;
	IGraphics::CTextureHandle m_BeamTexture;

	CHookGripTracker m_Tracker;
	CGripState m_aStates[MAX_CLIENTS];
	CGlowParticles m_Slime;
	float m_LastTime = 0.0f;

	void Grab(int Owner, const CHookGripTracker::CGrip &Grip, float Scale);
	void Squeeze(int Owner, const CHookGripTracker::CGrip &Grip, float Scale);
	void LetGo(int Owner, const CHookGripTracker::CGrip &Grip, float Scale);
	void Drip(int Owner, const CHookGripTracker::CGrip &Grip, float Passed, float Scale);
	void AddDroplet(vec2 Pos, vec2 Vel, float Size, float LifeSpan, float Alpha, ColorRGBA Color);
	void Draw(float Scale, float Time);
};
#endif
