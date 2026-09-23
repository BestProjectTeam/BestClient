/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_HOOKROPE_H
#define GAME_CLIENT_COMPONENTS_HOOKROPE_H
#include <base/color.h>
#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>
#include "fgf_glow.h"

class CHookRope : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnReset() override;
	void OnRender() override;

	bool RenderRope(int ClientId, vec2 TeePos, vec2 HookPos, int HookState, float Alpha);
	void RenderOverlay(int ClientId, vec2 TeePos, vec2 HookPos, int HookState, float Alpha);

private:
	struct CRopeState
	{
		float m_LastTime = -1.0f;
		float m_EmitAccumulator = 0.0f;
		float m_HeadAccumulator = 0.0f;
		float m_RingAccumulator = 0.0f;
	};

	IGraphics::CTextureHandle m_GlowTexture;
	IGraphics::CTextureHandle m_RingTexture;
	IGraphics::CTextureHandle m_DiscTexture;
	IGraphics::CTextureHandle m_BeamTexture;

	CRopeState m_aStates[MAX_CLIENTS];
	CGlowParticles m_Particles;
	float m_LastTime = 0.0f;

	void RenderAir(int ClientId, vec2 TeePos, vec2 HookPos, bool Pulling, float Alpha, float Passed);
	void RenderVoid(int ClientId, vec2 TeePos, vec2 HookPos, bool Flying, float Alpha, float Passed);
	void RenderMagic(int ClientId, vec2 TeePos, vec2 HookPos, bool Pulling, float Alpha, float Passed);
	void RenderTroll(int ClientId, vec2 TeePos, vec2 HookPos, bool Pulling, float Alpha, float Passed);
	void RenderFire(int ClientId, vec2 TeePos, vec2 HookPos, bool Pulling, float Alpha, float Passed);
	void RenderRainbow(int ClientId, vec2 TeePos, vec2 HookPos, bool Pulling, float Alpha, float Passed);
	void RenderBolt(int ClientId, vec2 TeePos, vec2 HookPos, bool Pulling, float Alpha, float Passed);
	void RenderTentacle(int ClientId, vec2 TeePos, vec2 HookPos, bool Pulling, float Alpha, float Passed);
	float Passed(int ClientId);
};
#endif
