/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_FGF_ICEWARD_H
#define GAME_CLIENT_COMPONENTS_FGF_ICEWARD_H
#include "fgf_glow.h"

#include <base/color.h>
#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>

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
	static constexpr int MAX_CACHED_TILES = 256;

	struct CStrand
	{
		vec2 m_Root;
		vec2 m_Away;
		float m_Strength;
		float m_Seed;
	};

	struct CTile
	{
		short m_X;
		short m_Y;
	};

	struct CScan
	{
		vec2 m_Pos = vec2(0.0f, 0.0f);
		float m_Reach = -1.0f;
		bool m_Valid = false;
		bool m_Overflow = false;
		int m_Num = 0;
		CTile m_aTiles[MAX_CACHED_TILES];
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
	CScan m_aScans[MAX_CLIENTS];
	CGlowParticles m_Frost;
	float m_LastTime = 0.0f;
	bool m_WasActive = false;

	bool FreezeAt(vec2 Pos) const;
	bool UseTileCache(int ClientId, vec2 Pos, float Reach);
	void ConsiderTile(int ClientId, int TileX, int TileY, vec2 Pos, float Reach, int MaxStrands);
	void Gather(int ClientId, vec2 Pos, float Reach);
	void DrawStrands(int ClientId, vec2 TeePos, float Alpha, ColorRGBA Color, float Time);
	void DrawShield(int ClientId, vec2 TeePos, float Alpha, ColorRGBA Color, float Time);
};
#endif
