/* Copyright © 2026 BestProject Team */
#include "weapon_vfx.h"

#include <base/math.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <generated/client_data.h>
#include <generated/protocol.h>

#include <game/client/gameclient.h>
#include <game/collision.h>

#include <algorithm>
#include <cmath>

namespace
{
	ColorRGBA BlendCol(const ColorRGBA &A, const ColorRGBA &B, float T)
	{
		return ColorRGBA(mix(A.r, B.r, T), mix(A.g, B.g, T), mix(A.b, B.b, T), mix(A.a, B.a, T));
	}
}

bool CWeaponVfx::RenderWeaponLaser(vec2 From, vec2 Pos, ColorRGBA OuterColor, ColorRGBA InnerColor, float TicksBody, float TicksHead, int Type) const
{
	if(Type != LASERTYPE_RIFLE && Type != LASERTYPE_SHOTGUN && Type >= 0)
		return false;
	const bool Shotgun = Type == LASERTYPE_SHOTGUN;
	const int Enabled = Shotgun ? g_Config.m_BcShotgunGlow : g_Config.m_BcLaserGlow;
	const int Power = Shotgun ? g_Config.m_BcShotgunGlowPower : g_Config.m_BcLaserGlowPower;
	if(!Enabled || Power <= 0 || (g_Config.m_ClFocusMode && g_Config.m_ClFocusModeHideEffects))
		return false;
	const int Mode = std::clamp(Shotgun ? g_Config.m_BcShotgunGlowMode : g_Config.m_BcLaserGlowMode, 0, NUM_MODES - 1);
	const unsigned Color = Shotgun ? g_Config.m_BcShotgunGlowColor : g_Config.m_BcLaserGlowColor;
	ColorRGBA DrawOuter = OuterColor;
	ColorRGBA DrawInner = InnerColor;
	if(Color != 0)
	{
		DrawOuter = color_cast<ColorRGBA>(ColorHSLA(Color)).WithAlpha(OuterColor.a);
		DrawInner = BlendCol(DrawOuter, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), 0.65f).WithAlpha(InnerColor.a);
	}
	if(Mode == MODE_CRYSTAL)
	{
		DrawOuter = BlendCol(OuterColor, Shotgun ? ColorRGBA(0.96f, 0.82f, 0.45f, 1.0f) : ColorRGBA(0.65f, 0.92f, 1.0f, 1.0f), 0.75f).WithAlpha(OuterColor.a);
		DrawInner = BlendCol(InnerColor, Shotgun ? ColorRGBA(1.0f, 0.95f, 0.70f, 1.0f) : ColorRGBA(0.90f, 0.98f, 1.0f, 1.0f), 0.75f).WithAlpha(InnerColor.a);
	}
	const int TuneZone = Client()->State() == IClient::STATE_ONLINE && GameClient()->m_GameWorld.m_WorldConfig.m_UseTuneZones ? Collision()->IsTune(Collision()->GetMapIndex(From)) : 0;
	const float Delay = GameClient()->GetTuning(TuneZone)->m_LaserBounceDelay;
	const float Ms = TicksBody * 1000.0f / Client()->GameTickSpeed();
	const float Fade = 1.0f - std::clamp(Ms / std::max(Delay, 0.001f), 0.0f, 1.0f);
	RenderLaser(From, Pos, DrawOuter, DrawInner, Fade, TicksHead, Mode, (float)Power);
	const int Particle = ((int)TicksHead % 3 + 3) % 3;
	Graphics()->TextureSet(GameClient()->m_ParticlesSkin.m_aSpriteParticleSplat[Particle]);
	Graphics()->QuadsBegin();
	Graphics()->QuadsSetRotation((int)TicksHead);
	Graphics()->SetColor(DrawOuter.WithMultipliedAlpha(Fade));
	IGraphics::CQuadItem Outer(Pos.x, Pos.y, 24.0f, 24.0f);
	Graphics()->QuadsDraw(&Outer, 1);
	Graphics()->SetColor(DrawInner.WithMultipliedAlpha(Fade));
	IGraphics::CQuadItem Inner(Pos.x, Pos.y, 20.0f, 20.0f);
	Graphics()->QuadsDraw(&Inner, 1);
	Graphics()->QuadsEnd();
	RenderLaserRings(Pos, DrawOuter, Power * Fade, TicksHead, Mode);
	RenderImpactStar(Pos, DrawInner, Fade, TicksHead);
	Graphics()->QuadsSetRotation(0.0f);
	return true;
}

void CWeaponVfx::RenderLaser(vec2 From, vec2 Pos, const ColorRGBA &OuterColor, const ColorRGBA &InnerColor, float WidthScale, float, int Mode, float Power) const
{
	const float Len = distance(Pos, From);
	if(Len <= 0.0f || WidthScale <= 0.001f || Power <= 0.0f)
		return;

	const vec2 Dir = normalize(Pos - From);
	const vec2 Normal = vec2(Dir.y, -Dir.x);
	const float NormPower = std::clamp(Power / 50.0f, 0.05f, 2.0f);
	const float GlobalTime = Client()->GlobalTime();

	ColorRGBA GlowCol = InnerColor;
	ColorRGBA BloomCol = OuterColor;

	Graphics()->TextureClear();
	Graphics()->BlendAdditive();
	Graphics()->QuadsBegin();

	float BloomAlpha = 0.18f;
	float GlowAlpha = 0.72f;
	if(Mode == MODE_PULSE)
	{
		const float Wave = 0.70f + 0.30f * std::sin(GlobalTime * 12.0f);
		GlowCol = InnerColor.Multiply(Wave);
		BloomCol = BlendCol(OuterColor, ColorRGBA(0.20f, 0.90f, 1.0f, 1.0f), 0.70f).Multiply(Wave);
		BloomAlpha = 0.22f;
		GlowAlpha = 0.75f;
	}
	else if(Mode == MODE_CRYSTAL)
	{
		GlowCol = BlendCol(InnerColor, ColorRGBA(0.70f, 0.95f, 1.0f, 1.0f), 0.65f);
		BloomCol = BlendCol(OuterColor, ColorRGBA(0.35f, 0.85f, 1.0f, 1.0f), 0.55f);
		BloomAlpha = 0.20f;
		GlowAlpha = 0.75f;
	}
	else if(Mode == MODE_PRISM)
	{
		const float Hue = fmod(GlobalTime * 0.5f, 1.0f);
		GlowCol = color_cast<ColorRGBA>(ColorHSLA(Hue, 1.0f, 0.60f));
		BloomCol = color_cast<ColorRGBA>(ColorHSLA(fmod(Hue + 0.25f, 1.0f), 1.0f, 0.60f));
	}

	const float OuterBloomWidth = (8.0f + NormPower * 6.0f) * WidthScale;
	Graphics()->SetColor(BloomCol.WithAlpha(BloomAlpha * OuterColor.a * WidthScale));
	IGraphics::CFreeformItem FreeformOuter(
		From - Normal * OuterBloomWidth, From + Normal * OuterBloomWidth,
		Pos - Normal * OuterBloomWidth, Pos + Normal * OuterBloomWidth);
	Graphics()->QuadsDrawFreeform(&FreeformOuter, 1);

	const float MainGlowWidth = (4.0f + NormPower * 3.0f) * WidthScale;
	Graphics()->SetColor(GlowCol.WithAlpha(GlowAlpha * InnerColor.a * WidthScale));
	IGraphics::CFreeformItem FreeformMain(
		From - Normal * MainGlowWidth, From + Normal * MainGlowWidth,
		Pos - Normal * MainGlowWidth, Pos + Normal * MainGlowWidth);
	Graphics()->QuadsDrawFreeform(&FreeformMain, 1);

	const float CoreWidth = 1.8f * WidthScale;
	Graphics()->SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.95f * InnerColor.a * WidthScale));
	IGraphics::CFreeformItem FreeformCore(
		From - Normal * CoreWidth, From + Normal * CoreWidth,
		Pos - Normal * CoreWidth, Pos + Normal * CoreWidth);
	Graphics()->QuadsDrawFreeform(&FreeformCore, 1);

	if(Mode == MODE_PULSE)
	{
		const int WaveSegments = std::clamp((int)(Len / 6.0f), 20, 80);
		const float WaveLength = 40.0f;
		const float WaveSpeed = 14.0f;
		const float WaveAmp = (3.8f + NormPower * 2.5f) * WidthScale;

		for(int wave = 0; wave < 2; ++wave)
		{
			const float WaveSign = (wave == 0) ? 1.0f : -1.0f;
			const float PhaseOffset = wave * pi;

			for(int i = 0; i < WaveSegments; ++i)
			{
				const float t0 = (float)i / (float)WaveSegments;
				const float t1 = (float)(i + 1) / (float)WaveSegments;

				const float d0 = t0 * Len;
				const float d1 = t1 * Len;

				const float Offset0 = std::sin((d0 / WaveLength) * 2.0f * pi - GlobalTime * WaveSpeed + PhaseOffset) * WaveAmp * WaveSign;
				const float Offset1 = std::sin((d1 / WaveLength) * 2.0f * pi - GlobalTime * WaveSpeed + PhaseOffset) * WaveAmp * WaveSign;

				const vec2 p0 = mix(From, Pos, t0) + Normal * Offset0;
				const vec2 p1 = mix(From, Pos, t1) + Normal * Offset1;
				const float CoilW = 1.3f * WidthScale;

				Graphics()->SetColor(ColorRGBA(0.85f, 0.98f, 1.0f, 0.85f * WidthScale * InnerColor.a));
				IGraphics::CFreeformItem CoilSeg(
					p0 - Normal * CoilW, p0 + Normal * CoilW,
					p1 - Normal * CoilW, p1 + Normal * CoilW);
				Graphics()->QuadsDrawFreeform(&CoilSeg, 1);
			}
		}
	}
	else if(Mode == MODE_CRYSTAL)
	{
		const int NumNodes = std::clamp((int)(Len / 45.0f), 3, 10);
		for(int i = 1; i < NumNodes; ++i)
		{
			const float t = (float)i / (float)NumNodes;
			const vec2 NodePos = mix(From, Pos, t);
			const float Shimmer = 0.65f + 0.35f * std::sin(GlobalTime * 9.0f + i * 1.85f);
			const float NodeR = (3.5f + 2.0f * Shimmer) * WidthScale * NormPower;

			Graphics()->SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.90f * WidthScale * Shimmer * InnerColor.a));
			IGraphics::CFreeformItem Diamond(
				NodePos - Dir * (NodeR * 1.6f),
				NodePos + Normal * NodeR,
				NodePos - Normal * NodeR,
				NodePos + Dir * (NodeR * 1.6f));
			Graphics()->QuadsDrawFreeform(&Diamond, 1);
		}
	}

	Graphics()->QuadsEnd();
	Graphics()->BlendNormal();
}

void CWeaponVfx::RenderImpactStar(vec2 Pos, const ColorRGBA &Color, float Scale, float) const
{
	if(Scale <= 0.05f)
		return;

	Graphics()->TextureClear();
	Graphics()->BlendAdditive();
	Graphics()->QuadsBegin();

	const float FlareR = 7.0f * Scale;
	Graphics()->SetColor(Color.WithAlpha(0.40f * Scale * Color.a));
	const int FlareSegs = 16;
	const float FlareStep = 2.0f * pi / (float)FlareSegs;
	for(int i = 0; i < FlareSegs; ++i)
	{
		const float a0 = i * FlareStep;
		const float a1 = (i + 1) * FlareStep;
		const vec2 p0 = Pos + vec2(std::cos(a0), std::sin(a0)) * FlareR;
		const vec2 p1 = Pos + vec2(std::cos(a1), std::sin(a1)) * FlareR;
		IGraphics::CFreeformItem Tri(Pos, Pos, p0, p1);
		Graphics()->QuadsDrawFreeform(&Tri, 1);
	}

	const float RayLen = 13.0f * Scale;
	const float RayThick = 1.6f * Scale;
	Graphics()->SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.90f * Scale * Color.a));

	IGraphics::CFreeformItem RayH(
		Pos.x - RayLen, Pos.y - RayThick * 0.5f,
		Pos.x + RayLen, Pos.y - RayThick * 0.5f,
		Pos.x - RayLen, Pos.y + RayThick * 0.5f,
		Pos.x + RayLen, Pos.y + RayThick * 0.5f);
	Graphics()->QuadsDrawFreeform(&RayH, 1);

	IGraphics::CFreeformItem RayV(
		Pos.x - RayThick * 0.5f, Pos.y - RayLen,
		Pos.x + RayThick * 0.5f, Pos.y - RayLen,
		Pos.x - RayThick * 0.5f, Pos.y + RayLen,
		Pos.x + RayThick * 0.5f, Pos.y + RayLen);
	Graphics()->QuadsDrawFreeform(&RayV, 1);

	const float DiagLen = 8.0f * Scale;
	const float DiagThick = 1.2f * Scale;
	const vec2 Diag1 = normalize(vec2(1.0f, 1.0f));
	const vec2 Diag1Norm = vec2(Diag1.y, -Diag1.x) * DiagThick;
	const vec2 Diag2 = normalize(vec2(-1.0f, 1.0f));
	const vec2 Diag2Norm = vec2(Diag2.y, -Diag2.x) * DiagThick;

	Graphics()->SetColor(Color.WithAlpha(0.65f * Scale * Color.a));
	IGraphics::CFreeformItem RayD1(
		Pos - Diag1 * DiagLen - Diag1Norm, Pos - Diag1 * DiagLen + Diag1Norm,
		Pos + Diag1 * DiagLen - Diag1Norm, Pos + Diag1 * DiagLen + Diag1Norm);
	Graphics()->QuadsDrawFreeform(&RayD1, 1);

	IGraphics::CFreeformItem RayD2(
		Pos - Diag2 * DiagLen - Diag2Norm, Pos - Diag2 * DiagLen + Diag2Norm,
		Pos + Diag2 * DiagLen - Diag2Norm, Pos + Diag2 * DiagLen + Diag2Norm);
	Graphics()->QuadsDrawFreeform(&RayD2, 1);

	Graphics()->QuadsEnd();
	Graphics()->BlendNormal();
}

void CWeaponVfx::RenderLaserRings(vec2 Pos, const ColorRGBA &Color, float PowerScale, float TicksHead, int Mode) const
{
	const float NormPower = std::clamp(PowerScale / 50.0f, 0.0f, 2.0f);
	const float BaseAlpha = std::clamp(Color.a * std::min(NormPower, 1.0f), 0.0f, 1.0f);
	if(BaseAlpha <= 0.001f)
		return;

	Graphics()->TextureClear();
	Graphics()->BlendAdditive();
	Graphics()->QuadsBegin();

	const auto DrawRing = [this, Pos](float Radius, float Thickness, int Segments) {
		const float Step = 2.0f * pi / Segments;
		for(int i = 0; i < Segments; ++i)
		{
			const vec2 Dir0 = direction(i * Step);
			const vec2 Dir1 = direction((i + 1) * Step);
			IGraphics::CFreeformItem Quad(
				Pos + Dir0 * (Radius - Thickness * 0.5f), Pos + Dir0 * (Radius + Thickness * 0.5f),
				Pos + Dir1 * (Radius - Thickness * 0.5f), Pos + Dir1 * (Radius + Thickness * 0.5f));
			Graphics()->QuadsDrawFreeform(&Quad, 1);
		}
	};

	if(Mode == MODE_CLASSIC)
	{
		const float aRadii[4] = {5.0f, 10.0f, 16.0f, 22.0f};
		const float aAlphas[4] = {0.50f, 0.32f, 0.18f, 0.08f};
		for(int ring = 0; ring < 4; ++ring)
		{
			const float R = aRadii[ring] * NormPower;
			const float Thickness = (1.2f + ring * 0.25f) * NormPower;
			const float Alpha = BaseAlpha * aAlphas[ring];
			Graphics()->SetColor(Color.WithAlpha(Alpha));

			DrawRing(R, Thickness, 32);
		}
	}
	else if(Mode == MODE_PULSE)
	{
		const float Phase = fmod(TicksHead * 0.08f, 1.0f);
		for(int ring = 0; ring < 3; ++ring)
		{
			const float RingPhase = fmod(Phase + ring * 0.33f, 1.0f);
			const float R = (3.0f + RingPhase * 20.0f) * NormPower;
			const float Thickness = 1.3f * NormPower;
			const float Alpha = BaseAlpha * (1.0f - RingPhase) * 0.45f;
			Graphics()->SetColor(Color.WithAlpha(Alpha));

			DrawRing(R, Thickness, 32);
		}
	}
	else if(Mode == MODE_CRYSTAL)
	{
		const float Shimmer = 0.88f + 0.12f * std::sin(TicksHead * 0.35f);
		const ColorRGBA CyanGold = BlendCol(Color, ColorRGBA(0.75f, 0.96f, 1.0f, 1.0f), 0.65f);
		const float aRadii[3] = {6.0f, 13.0f, 21.0f};
		for(int ring = 0; ring < 3; ++ring)
		{
			const float R = aRadii[ring] * NormPower * Shimmer;
			const float Thickness = (1.2f + ring * 0.3f) * NormPower;
			const float Alpha = BaseAlpha * (0.38f / (float)(ring + 1));
			Graphics()->SetColor(CyanGold.WithAlpha(Alpha));

			DrawRing(R, Thickness, 8 + ring * 4);
		}
	}
	else if(Mode == MODE_PRISM)
	{
		const float Phase = TicksHead * 0.04f;
		const ColorRGBA aPrismCols[4] = {
			ColorRGBA(0.70f, 0.15f, 0.95f, 1.0f),
			ColorRGBA(0.15f, 0.90f, 0.40f, 1.0f),
			ColorRGBA(0.10f, 0.85f, 0.95f, 1.0f),
			ColorRGBA(0.95f, 0.20f, 0.50f, 1.0f)};
		const float aRadii[4] = {6.0f, 12.0f, 18.0f, 24.0f};
		for(int ring = 0; ring < 4; ++ring)
		{
			const float HueShift = fmod(Phase + ring * 0.25f, 1.0f);
			const ColorRGBA PrismColor = BlendCol(aPrismCols[ring], color_cast<ColorRGBA>(ColorHSLA(HueShift, 1.0f, 0.65f)), 0.4f);
			const float R = aRadii[ring] * NormPower;
			const float Thickness = (1.2f + ring * 0.2f) * NormPower;
			const float Alpha = BaseAlpha * (0.35f - ring * 0.06f);
			Graphics()->SetColor(PrismColor.WithAlpha(Alpha));

			DrawRing(R, Thickness, 32);
		}
	}

	Graphics()->QuadsEnd();
	Graphics()->BlendNormal();
}
