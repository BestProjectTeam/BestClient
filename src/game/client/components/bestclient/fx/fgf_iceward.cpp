/* Copyright © 2026 BestProject Team */
#include "fgf_iceward.h"

#include <base/math.h>

#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include "hookgrip.h"
#include "own_tee.h"
#include <game/collision.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>

namespace {
bool IsFreeze(int Tile)
{
	return Tile == TILE_FREEZE || Tile == TILE_DFREEZE || Tile == TILE_LFREEZE;
}

float Noise(float Seed)
{
	const float x = std::sin(Seed * 12.9898f + 78.233f) * 43758.5453f;
	return x - std::floor(x);
}
}

static float WardSparks() { return g_Config.m_BcFreezePullSparks / 100.0f; }

void CFgfIceWard::OnInit()
{
	m_GlowTexture = FgfGlow::CreateGlowTexture(Graphics());
	m_RingTexture = FgfGlow::CreateRingTexture(Graphics());
	m_BeamTexture = FgfGlow::CreateBeamTexture(Graphics());
}

void CFgfIceWard::OnReset()
{
	for(CWard &Ward : m_aWards)
		Ward = CWard();
	for(int &Num : m_aNumStrands)
		Num = 0;
	m_Frost.Clear();
}

bool CFgfIceWard::FreezeAt(vec2 Pos) const
{
	const CCollision *pCollision = GameClient()->Collision();
	const int Index = pCollision->GetPureMapIndex(Pos);
	return IsFreeze(pCollision->GetTileIndex(Index)) || IsFreeze(pCollision->GetFrontTileIndex(Index));
}

void CFgfIceWard::Gather(int ClientId, vec2 Pos, float Reach)
{
	CWard &Ward = m_aWards[ClientId];
	CStrand *pStrands = m_aStrands[ClientId];
	int &Num = m_aNumStrands[ClientId];
	Num = 0;
	Ward.m_Push = vec2(0.0f, 0.0f);

	const int MaxStrands = std::clamp(g_Config.m_BcFreezePullStrands, 1, MAX_STRANDS);
	const int TileX = (int)std::floor(Pos.x / 32.0f);
	const int TileY = (int)std::floor(Pos.y / 32.0f);
	const int Scan = std::max(SCAN, (int)std::ceil(Reach / 32.0f) + 1);

	for(int y = -Scan; y <= Scan; y++)
	{
		for(int x = -Scan; x <= Scan; x++)
		{
			const vec2 Center((TileX + x) * 32.0f + 16.0f, (TileY + y) * 32.0f + 16.0f);
			if(!FreezeAt(Center))
				continue;
			const vec2 Near(std::clamp(Pos.x, Center.x - 16.0f, Center.x + 16.0f),
				std::clamp(Pos.y, Center.y - 16.0f, Center.y + 16.0f));
			const vec2 Delta = Pos - Near;
			const float Distance = length(Delta);
			if(Distance > Reach)
				continue;
			const vec2 Out = Distance > 0.001f ? Delta / Distance : vec2(0.0f, -1.0f);
			if(FreezeAt(Near + Out * 20.0f) && Distance > 4.0f)
				continue;
			if(GameClient()->Collision()->IntersectLine(Near, Pos, nullptr, nullptr))
				continue;

			const float Strength = 1.0f - Distance / Reach;
			Ward.m_Push += Out * Strength;

			int Slot = Num;
			if(Num >= MaxStrands)
			{
				int Weakest = 0;
				for(int i = 1; i < Num; i++)
					if(pStrands[i].m_Strength < pStrands[Weakest].m_Strength)
						Weakest = i;
				if(pStrands[Weakest].m_Strength >= Strength)
					continue;
				Slot = Weakest;
			}
			else
			{
				Num++;
			}
			pStrands[Slot].m_Root = Near;
			pStrands[Slot].m_Away = Out;
			pStrands[Slot].m_Strength = Strength;
			pStrands[Slot].m_Seed = (TileX + x) * 37.0f + (TileY + y) * 91.0f;
		}
	}
}

void CFgfIceWard::DrawStrands(int ClientId, vec2 TeePos, float Alpha, ColorRGBA Color, float Time)
{
	const CWard &Ward = m_aWards[ClientId];
	const CStrand *pStrands = m_aStrands[ClientId];
	const ColorRGBA Bright = FgfGlow::Whiten(Color, 0.6f);

	for(int i = 0; i < m_aNumStrands[ClientId]; i++)
	{
		const CStrand &Strand = pStrands[i];
		const float Strength = Strand.m_Strength;
		if(Strength <= 0.02f)
			continue;

		const vec2 ToTee = TeePos - Strand.m_Root;
		const float Gap = length(ToTee);
		if(Gap < 1.0f)
			continue;
		const vec2 Along = ToTee / Gap;
		const vec2 Side(-Along.y, Along.x);
		const float Stop = Gap - mix(26.0f, 14.0f, Strength);
		if(Stop < 4.0f)
			continue;

		const float Lash = std::sin(Ward.m_Phase * 2.4f + Strand.m_Seed) * (0.35f + 0.65f * Noise(Strand.m_Seed));
		const float Bow = mix(6.0f, 22.0f, Strength) * Lash;

		static constexpr int s_Points = 12;
		vec2 aPoints[s_Points];
		float aWidths[s_Points];
		for(int k = 0; k < s_Points; k++)
		{
			const float u = k / (float)(s_Points - 1);
			const float Curve = std::sin(u * pi) * Bow;
			const float Whip = std::sin(u * 5.0f - Ward.m_Phase * 3.0f + Strand.m_Seed) * 2.5f * u * Strength;
			aPoints[k] = Strand.m_Root + Along * (Stop * u) + Side * (Curve + Whip);
			aWidths[k] = mix(3.4f, 0.4f, u) * mix(0.5f, 1.3f, Strength);
		}
		int Num = s_Points;
		for(int k = 1; k < s_Points; k++)
		{
			if(GameClient()->Collision()->IntersectLine(aPoints[k - 1], aPoints[k], &aPoints[k], nullptr))
			{
				Num = k + 1;
				break;
			}
		}
		if(Num < 2)
			continue;
		FgfGlow::DrawStrip(Graphics(), aPoints, aWidths, Num, Color.WithAlpha(0.7f * Strength * Alpha));

		if(Strength > 0.45f)
		{
			for(int k = 0; k < Num; k++)
				aWidths[k] *= 0.4f;
			FgfGlow::DrawStrip(Graphics(), aPoints, aWidths, Num, Bright.WithAlpha(0.8f * (Strength - 0.45f) / 0.55f * Alpha));
		}
	}
}

void CFgfIceWard::DrawShield(int ClientId, vec2 TeePos, float Alpha, ColorRGBA Color, float Time)
{
	const CWard &Ward = m_aWards[ClientId];
	if(Ward.m_Level <= 0.02f || length(Ward.m_Push) < 0.01f)
		return;

	const vec2 Facing = normalize(Ward.m_Push);
	const float Center = std::atan2(-Facing.y, -Facing.x);
	const float Radius = mix(46.0f, 34.0f, Ward.m_Level);
	const float Arc = mix(0.7f, 1.5f, Ward.m_Level);

	static constexpr int s_Points = 18;
	vec2 aPoints[s_Points];
	float aWidths[s_Points];
	for(int k = 0; k < s_Points; k++)
	{
		const float u = k / (float)(s_Points - 1);
		const float Angle = Center + (u - 0.5f) * 2.0f * Arc;
		const float Ripple = 1.0f + 0.06f * std::sin(u * 9.0f - Ward.m_Phase * 4.0f);
		aPoints[k] = TeePos + direction(Angle) * Radius * Ripple;
		aWidths[k] = std::sin(u * pi) * mix(1.2f, 3.4f, Ward.m_Level);
	}
	FgfGlow::DrawStrip(Graphics(), aPoints, aWidths, s_Points, Color.WithAlpha(0.45f * Ward.m_Level * Alpha));
}

void CFgfIceWard::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(BcFxSuppressed(GameClient()))
		return;

	const float Time = LocalTime();
	const float Passed = std::clamp(Time - m_LastTime, 0.0f, 0.1f) * GameClient()->GetAnimationPlaybackSpeed();
	m_LastTime = Time;

	if(!g_Config.m_BcFreezePull)
	{
		OnReset();
		return;
	}

	const float Reach = (float)g_Config.m_BcFreezePullRange;
	const ColorRGBA Color = g_Config.m_BcFreezePullHookColor ? FgfThemeColor() : color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcFreezePullColor));

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CWard &Ward = m_aWards[i];
		const CGameClient::CSnapState::CCharacterInfo &Character = GameClient()->m_Snap.m_aCharacters[i];
		if(!BcIsOwnTee(GameClient(), i) || !Character.m_Active)
		{
			m_aNumStrands[i] = 0;
			Ward.m_Level = 0.0f;
			continue;
		}

		const bool Frozen = (Character.m_HasExtendedData && Character.m_ExtendedData.m_FreezeEnd != 0) ||
				    (GameClient()->m_aClients[i].m_RenderInfo.m_TeeRenderFlags & TEE_EFFECT_FROZEN) != 0;

		const vec2 Pos = GameClient()->m_aClients[i].m_RenderPos;
		float Strongest = 0.0f;
		if(Frozen)
		{
			m_aNumStrands[i] = 0;
			Ward.m_Push = vec2(0.0f, 0.0f);
		}
		else
		{
			Gather(i, Pos, Reach);
			for(int k = 0; k < m_aNumStrands[i]; k++)
				Strongest = std::max(Strongest, m_aStrands[i][k].m_Strength);
		}
		Ward.m_Level += (Strongest - Ward.m_Level) * std::min(1.0f, Passed * (Frozen ? 22.0f : 10.0f));
		if(Ward.m_Level < 0.001f)
			Ward.m_Level = 0.0f;
		Ward.m_Phase += Passed * mix(2.0f, 5.5f, Ward.m_Level);

		Ward.m_SparkAccumulator += Passed * Ward.m_Level * 26.0f * WardSparks();
		while(Ward.m_SparkAccumulator >= 1.0f)
		{
			Ward.m_SparkAccumulator -= 1.0f;
			if(m_aNumStrands[i] == 0)
				break;
			const CStrand &Strand = m_aStrands[i][std::clamp((int)(random_float() * m_aNumStrands[i]), 0, m_aNumStrands[i] - 1)];
			CGlowParticles::CSpark *pSpark = m_Frost.NewSpark();
			if(!pSpark)
				break;
			*pSpark = CGlowParticles::CSpark{};
			pSpark->m_Pos = mix(Strand.m_Root, Pos, random_float(0.3f, 0.8f));
			pSpark->m_Vel = Strand.m_Away * random_float(20.0f, 90.0f) + random_direction() * random_float(10.0f, 50.0f);
			pSpark->m_Gravity = 60.0f;
			pSpark->m_Drag = 0.25f;
			pSpark->m_LifeSpan = random_float(0.3f, 0.7f);
			pSpark->m_StartSize = random_float(2.0f, 4.0f);
			pSpark->m_EndSize = 0.4f;
			pSpark->m_StartAlpha = 0.7f * Strand.m_Strength;
			pSpark->m_EndAlpha = 0.0f;
			pSpark->m_Color = FgfGlow::Whiten(Color, 0.35f);
		}
	}

	m_Frost.Update(Passed);

	bool Any = m_Frost.HasAny();
	for(int i = 0; i < MAX_CLIENTS && !Any; i++)
		Any = m_aNumStrands[i] > 0;
	if(!Any)
		return;

	Graphics()->BlendAdditive();

	Graphics()->TextureSet(m_GlowTexture);
	Graphics()->QuadsBegin();
	Graphics()->QuadsSetRotation(0.0f);
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const float Alpha = GameClient()->IsOtherTeam(i) ? g_Config.m_ClShowOthersAlpha / 100.0f : 1.0f;
		for(int k = 0; k < m_aNumStrands[i]; k++)
		{
			const CStrand &Strand = m_aStrands[i][k];
			const float Size = mix(10.0f, 26.0f, Strand.m_Strength);
			Graphics()->SetColor(Color.WithAlpha(0.35f * Strand.m_Strength * Alpha));
			IGraphics::CQuadItem Quad(Strand.m_Root.x, Strand.m_Root.y, Size, Size);
			Graphics()->QuadsDraw(&Quad, 1);
		}
	}
	Graphics()->QuadsEnd();

	FgfGlow::BeginStrips(Graphics(), m_BeamTexture);
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_aNumStrands[i] == 0 && m_aWards[i].m_Level <= 0.02f)
			continue;
		const vec2 Pos = GameClient()->m_aClients[i].m_RenderPos;
		const float Alpha = GameClient()->IsOtherTeam(i) ? g_Config.m_ClShowOthersAlpha / 100.0f : 1.0f;
		DrawStrands(i, Pos, Alpha, Color, Time);
		if(g_Config.m_BcFreezePullShield)
			DrawShield(i, Pos, Alpha, Color, Time);
	}
	Graphics()->QuadsEnd();

	m_Frost.Draw(Graphics(), m_GlowTexture, m_RingTexture);

	Graphics()->BlendNormal();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}
