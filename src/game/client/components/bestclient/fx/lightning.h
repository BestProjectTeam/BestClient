/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_LIGHTNING_H
#define GAME_CLIENT_COMPONENTS_LIGHTNING_H
#include <base/color.h>
#include <base/vmath.h>

#include <engine/graphics.h>

#include <game/client/component.h>
#include "fgf_glow.h"

class CLightning : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnReset() override;
	void OnNewSnapshot() override;
	void OnRender() override;

	void Strike(vec2 Target, float Alpha, bool Local);
	void AddShake(float Strength);
	void ApplyCameraShake(vec2 &Center);

private:
	enum
	{
		MAX_BOLTS = 16,
		MAX_POINTS = 33,
		BRANCH_POINTS = 9,
		MAX_BRANCHES = 8,
	};

	struct CPath
	{
		vec2 m_aPoints[MAX_POINTS];
		int m_NumPoints = 0;
	};

	struct CPalette
	{
		ColorRGBA m_Primary;
		ColorRGBA m_Secondary;
		ColorRGBA m_Core;
	};

	struct CBolt
	{
		vec2 m_Origin;
		vec2 m_Target;
		float m_Age;
		float m_Alpha;
		bool m_Local;
		int m_Strokes;
		CPath m_Channel;
		CPath m_aBranches[MAX_BRANCHES];
		float m_aBranchWidths[MAX_BRANCHES];
		int m_NumBranches;
	};

	IGraphics::CTextureHandle m_GlowTexture;
	IGraphics::CTextureHandle m_RingTexture;
	IGraphics::CTextureHandle m_BeamTexture;

	CBolt m_aBolts[MAX_BOLTS];
	int m_NumBolts = 0;
	CGlowParticles m_Glow;
	float m_LastTime = 0.0f;
	float m_ShakeStrength = 0.0f;
	float m_LastShakeTime = 0.0f;

	static CPalette Palette();
	static void BuildPath(CPath *pPath, vec2 From, vec2 To, int NumPoints, float Wildness);
	static float Intensity(const CBolt &Bolt);

	void Stroke(CBolt &Bolt, const CPalette &Palette);
	void Impact(const CBolt &Bolt, const CPalette &Palette, float Power);

	void DrawPath(const CPath &Path, float Width, bool TaperToTip, ColorRGBA Color);
	void DrawBolts(const CPalette &Palette);
};
#endif
