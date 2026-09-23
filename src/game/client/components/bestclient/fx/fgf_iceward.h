/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_FGF_ICEWARD_H
#define GAME_CLIENT_COMPONENTS_FGF_ICEWARD_H
#include <base/color.h>
#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>
#include "fgf_glow.h"

class CFgfIceWard : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnReset() override;
	void OnRender() override;

private:
	static constexpr int MAX_STRANDS = 8;
	static constexpr int SCAN = 5;

	struct CStrand
	{
		vec2 m_Root;
		vec2 m_Away;
		float m_Strength;
		float m_Seed;
	};

	struct CWard
	{
		float m_Level = 0.0f;
		float m_Phase = 0.0f;
		vec2 m_Push = vec2(0.0f, 0.0f);
		float m_SparkAccumulator = 0.0f;
	};

	IGraphics::CTextureHandle m_GlowTexture;
	IGraphics::CTextureHandle m_RingTexture;
	IGraphics::CTextureHandle m_BeamTexture;

	CWard m_aWards[MAX_CLIENTS];
	CStrand m_aStrands[MAX_CLIENTS][MAX_STRANDS];
	int m_aNumStrands[MAX_CLIENTS];
	CGlowParticles m_Frost;
	float m_LastTime = 0.0f;

	bool FreezeAt(vec2 Pos) const;
	void Gather(int ClientId, vec2 Pos, float Reach);
	void DrawStrands(int ClientId, vec2 TeePos, float Alpha, ColorRGBA Color, float Time);
	void DrawShield(int ClientId, vec2 TeePos, float Alpha, ColorRGBA Color, float Time);
};
#endif
