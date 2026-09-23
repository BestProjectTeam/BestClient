/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_HOOKMAGIC_H
#define GAME_CLIENT_COMPONENTS_HOOKMAGIC_H
#include <base/color.h>
#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>
#include "fgf_glow.h"
#include "hookgrip.h"

class CHookMagic : public CComponent
{
public:
	struct CPalette
	{
		ColorRGBA m_Main;
		ColorRGBA m_Accent;
		ColorRGBA m_Core;
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnReset() override;
	void OnRender() override;

	static CPalette Palette();
	static void DrawRune(IGraphics *pGraphics, vec2 Center, float Size, float Angle, int Glyph, float Width, ColorRGBA Color);

private:
	static constexpr int NUM_RUNES = 12;
	static constexpr int MAX_FLYING_RUNES = 128;

	struct CCircleState
	{
		float m_Spin = 1.0f;
		float m_Angle = 0.0f;
		float m_Appear = 0.0f;
		int m_GlyphOffset = 0;
		float m_MoteAccumulator = 0.0f;
	};

	struct CFlyingRune
	{
		vec2 m_Pos;
		vec2 m_Vel;
		float m_Angle;
		float m_AngularSpeed;
		float m_Size;
		float m_Life;
		float m_LifeSpan;
		float m_Alpha;
		int m_Glyph;
	};

	IGraphics::CTextureHandle m_GlowTexture;
	IGraphics::CTextureHandle m_RingTexture;
	IGraphics::CTextureHandle m_BeamTexture;

	CHookGripTracker m_Tracker;
	CCircleState m_aStates[MAX_CLIENTS];
	CGlowParticles m_Sparks;
	CFlyingRune m_aFlyingRunes[MAX_FLYING_RUNES];
	int m_NumFlyingRunes = 0;
	float m_LastTime = 0.0f;

	static float CircleRadius(const CHookGripTracker::CGrip &Grip, float Scale);

	void Unfold(const CHookGripTracker::CGrip &Grip, const CPalette &Palette, float Scale);
	void Empower(const CHookGripTracker::CGrip &Grip, const CPalette &Palette, float Scale);
	void Shatter(int Owner, const CHookGripTracker::CGrip &Grip, const CPalette &Palette, float Scale);
	void EmitMotes(int Owner, const CHookGripTracker::CGrip &Grip, const CPalette &Palette, float Passed, float Scale);

	void DrawCircle(int Owner, const CHookGripTracker::CGrip &Grip, const CPalette &Palette, float Scale, float Time);
	void Draw(const CPalette &Palette, float Scale, float Time);
};
#endif
