/* Copyright © 2026 BestProject Team */
#include "weapon_vfx.h"

#include <base/math.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <generated/client_data.h>

#include <game/client/gameclient.h>
#include <game/client/projectile_data.h>
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

bool CWeaponVfx::RenderRocketTrail(const CProjectileData &Data, float Time, float Curvature, float Speed, float Alpha)
{
	if(!g_Config.m_BcRocketGlow || g_Config.m_BcRocketGlowPower <= 0)
		return false;
	if(GameClient()->OptimizerDisableParticles() || (g_Config.m_ClFocusMode && g_Config.m_ClFocusModeHideEffects))
		return true;
	constexpr int NumSamples = 16;
	vec2 aPoints[NumSamples];
	float aAlphas[NumSamples];
	const float SampleTime = std::min(Time, 0.32f);
	for(int i = 0; i < NumSamples; ++i)
	{
		const float Sample = std::max(0.0f, Time - i / (float)(NumSamples - 1) * SampleTime);
		aPoints[i] = CalcPos(Data.m_StartPos, Data.m_StartVel, Curvature, Speed, Sample);
		aAlphas[i] = Alpha;
	}
	RenderRocketTrajectory(aPoints, aAlphas, NumSamples, g_Config.m_BcRocketGlowMode, (float)g_Config.m_BcRocketGlowPower);
	return true;
}

void CWeaponVfx::RenderRocketTrajectory(const vec2 *pPoints, const float *pAlphas, int NumPoints, int Mode, float Power)
{
	if(NumPoints < 2 || Power <= 0.01f)
		return;

	ColorRGBA UserRocketCol(1.0f, 0.55f, 0.15f, 1.0f);
	if(g_Config.m_BcRocketGlowColor != 0)
	{
		ColorHSLA Hsl(g_Config.m_BcRocketGlowColor);
		if(Hsl.l < 0.08f)
			Hsl.l = 0.55f;
		if(Hsl.s < 0.08f)
			Hsl.s = 0.85f;
		UserRocketCol = color_cast<ColorRGBA>(Hsl);
	}

	const float GlobalTime = Client()->GlobalTime();
	const float BaseWidth = (9.0f + (Power / 50.0f) * 5.0f);

	Graphics()->TextureClear();
	Graphics()->BlendAdditive();
	Graphics()->QuadsBegin();

	switch(Mode)
	{
	case MODE_CLASSIC:
	{
		for(int i = 0; i < NumPoints - 1; ++i)
		{
			const float t0 = (float)i / (float)(NumPoints - 1);
			const float t1 = (float)(i + 1) / (float)(NumPoints - 1);

			const vec2 P0 = pPoints[i];
			const vec2 P1 = pPoints[i + 1];
			const float SegLen = distance(P0, P1);
			if(SegLen <= 0.001f)
				continue;

			const vec2 SegDir = (P1 - P0) / SegLen;
			const vec2 SegNorm = vec2(-SegDir.y, SegDir.x);

			const float W0 = BaseWidth * (1.0f - t0 * 0.85f);
			const float W1 = BaseWidth * (1.0f - t1 * 0.85f);
			const float CoreW0 = W0 * 0.38f;
			const float CoreW1 = W1 * 0.38f;

			ColorRGBA Col0 = (t0 < 0.35f) ? BlendCol(ColorRGBA(1.0f, 0.95f, 0.80f, 1.0f), UserRocketCol, t0 / 0.35f) :
							BlendCol(UserRocketCol, ColorRGBA(0.85f, 0.25f, 0.05f, 0.6f), (t0 - 0.35f) / 0.65f);

			const float Fade0 = (1.0f - t0) * pAlphas[i];

			Graphics()->SetColor(Col0.WithAlpha(Col0.a * Fade0 * 0.90f));
			IGraphics::CFreeformItem OuterQuad(
				P0 - SegNorm * W0, P0 + SegNorm * W0,
				P1 - SegNorm * W1, P1 + SegNorm * W1);
			Graphics()->QuadsDrawFreeform(&OuterQuad, 1);

			if(t0 < 0.6f)
			{
				Graphics()->SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, Fade0 * 0.95f));
				IGraphics::CFreeformItem CoreQuad(
					P0 - SegNorm * CoreW0, P0 + SegNorm * CoreW0,
					P1 - SegNorm * CoreW1, P1 + SegNorm * CoreW1);
				Graphics()->QuadsDrawFreeform(&CoreQuad, 1);
			}
		}
		break;
	}
	case MODE_PULSE:
	{
		const ColorRGBA PulseCyan = BlendCol(UserRocketCol, ColorRGBA(0.15f, 0.90f, 1.0f, 1.0f), 0.75f);
		for(int i = 0; i < NumPoints - 1; ++i)
		{
			const float t0 = (float)i / (float)(NumPoints - 1);
			const float t1 = (float)(i + 1) / (float)(NumPoints - 1);

			const vec2 P0 = pPoints[i];
			const vec2 P1 = pPoints[i + 1];
			const float SegLen = distance(P0, P1);
			if(SegLen <= 0.001f)
				continue;

			const vec2 SegDir = (P1 - P0) / SegLen;
			const vec2 SegNorm = vec2(-SegDir.y, SegDir.x);

			const float Pulse0 = 0.5f + 0.5f * std::sin(GlobalTime * 20.0f + t0 * 12.0f);
			const float Pulse1 = 0.5f + 0.5f * std::sin(GlobalTime * 20.0f + t1 * 12.0f);

			const float W0 = (BaseWidth * 0.65f + BaseWidth * 0.35f * Pulse0) * (1.0f - t0 * 0.80f);
			const float W1 = (BaseWidth * 0.65f + BaseWidth * 0.35f * Pulse1) * (1.0f - t1 * 0.80f);

			const float Fade0 = (1.0f - t0) * pAlphas[i];

			Graphics()->SetColor(PulseCyan.WithAlpha(Fade0 * 0.88f));
			IGraphics::CFreeformItem BloomQuad(
				P0 - SegNorm * W0, P0 + SegNorm * W0,
				P1 - SegNorm * W1, P1 + SegNorm * W1);
			Graphics()->QuadsDrawFreeform(&BloomQuad, 1);

			Graphics()->SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, Fade0 * 0.95f));
			IGraphics::CFreeformItem CoreQuad(
				P0 - SegNorm * (W0 * 0.35f), P0 + SegNorm * (W0 * 0.35f),
				P1 - SegNorm * (W1 * 0.35f), P1 + SegNorm * (W1 * 0.35f));
			Graphics()->QuadsDrawFreeform(&CoreQuad, 1);

			if(i % 2 == 0)
			{
				const float Jitter = std::sin(GlobalTime * 70.0f + i * 9.2f) * (6.5f * (Power / 50.0f));
				const vec2 SparkPos = mix(P0, P1, 0.5f) + SegNorm * Jitter;
				const float SparkW = 1.3f;

				Graphics()->SetColor(ColorRGBA(0.85f, 0.98f, 1.0f, Fade0 * 0.90f));
				IGraphics::CFreeformItem SparkSeg(
					P0 - SegNorm * SparkW, P0 + SegNorm * SparkW,
					SparkPos - SegNorm * SparkW, SparkPos + SegNorm * SparkW);
				Graphics()->QuadsDrawFreeform(&SparkSeg, 1);
			}
		}
		break;
	}
	case MODE_PRISM:
	{
		for(int i = 0; i < NumPoints - 1; ++i)
		{
			const float t0 = (float)i / (float)(NumPoints - 1);
			const float t1 = (float)(i + 1) / (float)(NumPoints - 1);

			const vec2 P0 = pPoints[i];
			const vec2 P1 = pPoints[i + 1];
			const float SegLen = distance(P0, P1);
			if(SegLen <= 0.001f)
				continue;

			const vec2 SegDir = (P1 - P0) / SegLen;
			const vec2 SegNorm = vec2(-SegDir.y, SegDir.x);

			const float W0 = BaseWidth * (1.0f - t0 * 0.80f);
			const float W1 = BaseWidth * (1.0f - t1 * 0.80f);

			const float Hue0 = fmod(GlobalTime * 0.8f + t0 * 0.8f, 1.0f);
			const ColorRGBA Col0 = color_cast<ColorRGBA>(ColorHSLA(Hue0, 1.0f, 0.60f));

			const float Fade0 = (1.0f - t0) * pAlphas[i];

			Graphics()->SetColor(Col0.WithAlpha(Fade0 * 0.85f));
			IGraphics::CFreeformItem RainbowQuad(
				P0 - SegNorm * W0, P0 + SegNorm * W0,
				P1 - SegNorm * W1, P1 + SegNorm * W1);
			Graphics()->QuadsDrawFreeform(&RainbowQuad, 1);

			Graphics()->SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, Fade0 * 0.90f));
			IGraphics::CFreeformItem CoreQuad(
				P0 - SegNorm * (W0 * 0.28f), P0 + SegNorm * (W0 * 0.28f),
				P1 - SegNorm * (W1 * 0.28f), P1 + SegNorm * (W1 * 0.28f));
			Graphics()->QuadsDrawFreeform(&CoreQuad, 1);
		}
		break;
	}
	case MODE_CRYSTAL:
	{
		for(int i = 0; i < NumPoints; ++i)
		{
			const float t = (float)i / (float)(NumPoints - 1);
			const vec2 Center = pPoints[i];
			const float ShardSize = (BaseWidth * 0.75f) * (1.0f - t * 0.75f);
			const float OrbAngle = GlobalTime * 8.0f + i * 0.85f;
			const vec2 Dir = vec2(std::cos(OrbAngle), std::sin(OrbAngle));
			const vec2 Norm = vec2(-Dir.y, Dir.x);

			const float Fade = (1.0f - t) * pAlphas[i];
			const ColorRGBA Shimmer = (i % 2 == 0) ? UserRocketCol : ColorRGBA(0.85f, 0.95f, 1.0f, 1.0f);

			Graphics()->SetColor(Shimmer.WithAlpha(Fade * 0.90f));
			IGraphics::CFreeformItem ShardRhombus(
				Center - Dir * (ShardSize * 1.5f),
				Center + Norm * (ShardSize * 0.85f),
				Center - Norm * (ShardSize * 0.85f),
				Center + Dir * (ShardSize * 1.5f));
			Graphics()->QuadsDrawFreeform(&ShardRhombus, 1);
		}
		break;
	}
	}

	Graphics()->QuadsEnd();
	Graphics()->BlendNormal();
}
