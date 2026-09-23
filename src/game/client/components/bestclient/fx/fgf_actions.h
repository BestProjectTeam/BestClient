/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_FGF_ACTIONS_H
#define GAME_CLIENT_COMPONENTS_FGF_ACTIONS_H
#include <base/color.h>
#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/client/enums.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>
#include "fgf_glow.h"

class CFgfActions : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnReset() override;
	void OnRender() override;

private:
	static constexpr int MAX_JUMP_RINGS = 16;
	static constexpr int MAX_SLASHES = 8;

	struct CSlot
	{
		int m_ClientId = -1;
		int m_JumpedTotal = 0;
		int m_AttackTick = -1;
		bool m_Tethered = false;
		int m_Target = -1;
		float m_TetherTime = 0.0f;
		vec2 m_TargetPos = vec2(0.0f, 0.0f);
		float m_ReleaseAge = 1.0f;
	};

	struct CJumpRing
	{
		vec2 m_Pos;
		float m_Age;
	};

	struct CSlash
	{
		int m_ClientId;
		float m_Angle;
		float m_Sweep;
		float m_Age;
	};

	IGraphics::CTextureHandle m_GlowTexture;
	IGraphics::CTextureHandle m_RingTexture;
	IGraphics::CTextureHandle m_BeamTexture;

	CSlot m_aSlots[NUM_DUMMIES];
	CJumpRing m_aJumpRings[MAX_JUMP_RINGS];
	int m_NumJumpRings = 0;
	CSlash m_aSlashes[MAX_SLASHES];
	int m_NumSlashes = 0;
	CGlowParticles m_Sparks;
	float m_LastTime = 0.0f;

	void UpdateSlot(int Dummy, float Passed, ColorRGBA Main, ColorRGBA Core);
	void AirJump(vec2 Feet, ColorRGBA Main, ColorRGBA Core);
	void Swing(int ClientId, float Angle, ColorRGBA Core);
	void Catch(vec2 Target, ColorRGBA Main, ColorRGBA Core);
	void LetGo(vec2 Target, ColorRGBA Main, ColorRGBA Core);

	void DrawTether(const CSlot &Slot, ColorRGBA Main, ColorRGBA Core, float Time);
};
#endif
