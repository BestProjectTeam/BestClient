/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_FGF_GLOW_H
#define GAME_CLIENT_COMPONENTS_FGF_GLOW_H
#include <base/color.h>
#include <base/vmath.h>

#include <engine/graphics.h>

namespace FgfGlow
{
IGraphics::CTextureHandle CreateGlowTexture(IGraphics *pGraphics);
IGraphics::CTextureHandle CreateRingTexture(IGraphics *pGraphics);
IGraphics::CTextureHandle CreateDiscTexture(IGraphics *pGraphics);
IGraphics::CTextureHandle CreateBeamTexture(IGraphics *pGraphics);
IGraphics::CTextureHandle CreateLeafTexture(IGraphics *pGraphics);

ColorRGBA Whiten(ColorRGBA Color, float Amount);
ColorRGBA Blend(ColorRGBA A, ColorRGBA B, float Amount);

enum
{
	MAX_STRIP_POINTS = 64,
};

void DrawStrip(IGraphics *pGraphics, const vec2 *pPoints, const float *pWidths, int NumPoints, ColorRGBA Color);
void BeginStrips(IGraphics *pGraphics, IGraphics::CTextureHandle BeamTexture);
}

class CGlowParticles
{
public:
	struct CSpark
	{
		bool m_Orbital;
		vec2 m_Pos;
		vec2 m_Vel;
		vec2 m_Center;
		float m_Radius;
		float m_RadialSpeed;
		float m_Angle;
		float m_AngularSpeed;
		float m_Gravity;
		float m_Drag;
		float m_Stretch;
		float m_Life;
		float m_LifeSpan;
		float m_StartSize;
		float m_EndSize;
		float m_StartAlpha;
		float m_EndAlpha;
		ColorRGBA m_Color;
		bool m_ShiftColor;
		ColorRGBA m_EndColor;
	};

	void Clear();
	bool HasAny() const { return m_NumSparks > 0 || m_NumRings > 0; }
	CSpark *NewSpark();
	void AddRing(vec2 Pos, float LifeSpan, float StartSize, float EndSize, float Alpha, ColorRGBA Color);
	void Update(float Passed);
	void Draw(IGraphics *pGraphics, IGraphics::CTextureHandle GlowTexture, IGraphics::CTextureHandle RingTexture, float AlphaScale = 1.0f) const;

private:
	enum
	{
		MAX_SPARKS = 1024,
		MAX_RINGS = 32,
	};

	struct CRing
	{
		vec2 m_Pos;
		float m_Life;
		float m_LifeSpan;
		float m_StartSize;
		float m_EndSize;
		float m_Alpha;
		ColorRGBA m_Color;
	};

	CSpark m_aSparks[MAX_SPARKS];
	int m_NumSparks = 0;
	CRing m_aRings[MAX_RINGS];
	int m_NumRings = 0;
};
#endif
