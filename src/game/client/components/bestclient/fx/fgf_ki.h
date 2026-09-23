/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_FGF_KI_H
#define GAME_CLIENT_COMPONENTS_FGF_KI_H
#include <base/color.h>
#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>
#include "fgf_glow.h"

class CFgfKi : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnReset() override;
	void OnRender() override;

private:
	static constexpr int MAX_MOTES = 28;

	struct CMote
	{
		float m_Angle;
		float m_Radius;
		float m_TargetRadius;
		float m_AngularSpeed;
		float m_Size;
		float m_Life;
		float m_LifeSpan;
		float m_Hue;
	};

	struct CCharge
	{
		bool m_Holding = false;
		float m_HoldTime = 0.0f;
		float m_Charge = 0.0f;
		float m_Level = 0.0f;
		float m_Flash = 0.0f;
		float m_Spin = 0.0f;
		float m_Accumulator = 0.0f;
		bool m_Peaked = false;
		CMote m_aMotes[MAX_MOTES];
		int m_NumMotes = 0;
	};

	IGraphics::CTextureHandle m_GlowTexture;
	IGraphics::CTextureHandle m_RingTexture;
	IGraphics::CTextureHandle m_BeamTexture;

	CCharge m_aCharges[MAX_CLIENTS];
	CGlowParticles m_Burst;
	float m_LastTime = 0.0f;

	ColorRGBA Color(const CCharge &Charge, float Offset) const;
	void Spawn(CCharge &Charge, float Scale);
	void Release(int ClientId, vec2 Pos, float Scale);
	void Draw(float Scale, float Time);
};
#endif
