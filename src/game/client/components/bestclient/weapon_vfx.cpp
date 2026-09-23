/* Copyright © 2026 BestProject Team */
#include "weapon_vfx.h"

#include <base/math.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <generated/client_data.h>

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

void CWeaponVfx::OnInit()
{
	OnReset();
}

void CWeaponVfx::OnReset()
{
	for(int i = 0; i < MAX_SHOCKWAVES; ++i)
		m_aShockwaves[i].m_Active = false;

	for(int i = 0; i < MAX_DEBRIS; ++i)
		m_aDebris[i].m_Active = false;

	for(int i = 0; i < MAX_FLASHES; ++i)
		m_aFlashes[i].m_Active = false;

	for(int i = 0; i < MAX_NUKE_PUFFS; ++i)
		m_aNukePuffs[i].m_Active = false;

	for(int i = 0; i < MAX_BLACK_HOLES; ++i)
		m_aBlackHoles[i].m_Active = false;

	for(int i = 0; i < MAX_QUASARS; ++i)
		m_aQuasars[i].m_Active = false;

	for(int i = 0; i < MAX_RECENT_EXPLOSIONS; ++i)
	{
		m_aRecentExplosions[i].m_Pos = vec2(0.0f, 0.0f);
		m_aRecentExplosions[i].m_TimeMs = 0;
	}
	m_RecentExplosionIndex = 0;
	m_ShakeIntensity = m_ShakeDuration = m_ShakeTimer = m_ShakeTime = 0.0f;
	m_EffectTime = 0.0;
	m_HadRocketVfx = false;
}

void CWeaponVfx::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(GameClient()->OptimizerDisableParticles() || (g_Config.m_ClFocusMode && g_Config.m_ClFocusModeHideEffects))
	{
		OnReset();
		return;
	}
	const bool RocketVfx = g_Config.m_BcRocketGlow || g_Config.m_BcMemeNukeExplosion;
	if(!RocketVfx)
	{
		if(m_HadRocketVfx)
			OnReset();
		m_HadRocketVfx = false;
		return;
	}
	m_HadRocketVfx = true;
	if(!g_Config.m_BcMemeNukeExplosion)
	{
		for(auto &Puff : m_aNukePuffs)
			Puff.m_Active = false;
		for(auto &Hole : m_aBlackHoles)
			Hole.m_Active = false;
		for(auto &Quasar : m_aQuasars)
			Quasar.m_Active = false;
		for(auto &Explosion : m_aRecentExplosions)
			Explosion.m_TimeMs = 0;
		m_ShakeTimer = 0.0f;
	}
	const float Dt = Client()->RenderFrameTime() * GameClient()->GetAnimationPlaybackSpeed();
	m_EffectTime += Dt;
	UpdateSimulation(Dt);
	RenderActiveVfx();
	Graphics()->QuadsSetRotation(0.0f);
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CWeaponVfx::UpdateSimulation(float Dt)
{
	if(Dt <= 0.0f)
		return;

	for(int i = 0; i < MAX_SHOCKWAVES; ++i)
	{
		if(!m_aShockwaves[i].m_Active)
			continue;

		m_aShockwaves[i].m_Life += Dt;
		if(m_aShockwaves[i].m_Life >= m_aShockwaves[i].m_MaxLife)
		{
			m_aShockwaves[i].m_Active = false;
		}
	}

	for(int i = 0; i < MAX_FLASHES; ++i)
	{
		if(!m_aFlashes[i].m_Active)
			continue;

		m_aFlashes[i].m_Life += Dt;
		if(m_aFlashes[i].m_Life >= m_aFlashes[i].m_MaxLife)
		{
			m_aFlashes[i].m_Active = false;
		}
	}

	for(int i = 0; i < MAX_DEBRIS; ++i)
	{
		if(!m_aDebris[i].m_Active)
			continue;

		m_aDebris[i].m_Life += Dt;
		if(m_aDebris[i].m_Life >= m_aDebris[i].m_MaxLife)
		{
			m_aDebris[i].m_Active = false;
			continue;
		}

		m_aDebris[i].m_Vel.y += 820.0f * Dt;
		m_aDebris[i].m_Vel *= std::pow(0.92f, Dt * 60.0f);
		m_aDebris[i].m_Rot += m_aDebris[i].m_RotSpeed * Dt;

		vec2 Pos = m_aDebris[i].m_Pos;
		vec2 NewPos = Pos + m_aDebris[i].m_Vel * Dt;

		if(Collision()->CheckPoint(NewPos))
		{
			const bool HitX = Collision()->CheckPoint(vec2(NewPos.x, Pos.y));
			const bool HitY = Collision()->CheckPoint(vec2(Pos.x, NewPos.y));

			if(HitX)
				m_aDebris[i].m_Vel.x = -m_aDebris[i].m_Vel.x * 0.55f;
			if(HitY)
				m_aDebris[i].m_Vel.y = -m_aDebris[i].m_Vel.y * 0.55f;
			if(!HitX && !HitY)
				m_aDebris[i].m_Vel = -m_aDebris[i].m_Vel * 0.55f;

			NewPos = Pos + m_aDebris[i].m_Vel * Dt;
		}

		m_aDebris[i].m_Pos = NewPos;
	}

	for(int i = 0; i < MAX_NUKE_PUFFS; ++i)
	{
		if(!m_aNukePuffs[i].m_Active)
			continue;

		m_aNukePuffs[i].m_Life += Dt;
		if(m_aNukePuffs[i].m_Life >= m_aNukePuffs[i].m_MaxLife)
		{
			m_aNukePuffs[i].m_Active = false;
			continue;
		}

		if(m_aNukePuffs[i].m_OrbitRadius > 0.0f)
		{
			m_aNukePuffs[i].m_OrbitAngle += m_aNukePuffs[i].m_OrbitSpeed * Dt;
			if(m_aNukePuffs[i].m_StackLevel == 3)
			{
				m_aNukePuffs[i].m_OrbitRadius = std::max(20.0f, m_aNukePuffs[i].m_OrbitRadius - 10.0f * Dt);
			}
			m_aNukePuffs[i].m_Pos = m_aNukePuffs[i].m_OriginPos + vec2(std::cos(m_aNukePuffs[i].m_OrbitAngle), std::sin(m_aNukePuffs[i].m_OrbitAngle)) * m_aNukePuffs[i].m_OrbitRadius;
		}
		else if(m_aNukePuffs[i].m_Layer == 3)
		{
			m_aNukePuffs[i].m_Vel.y += 620.0f * Dt;
			m_aNukePuffs[i].m_Vel *= std::pow(0.96f, Dt * 60.0f);
			m_aNukePuffs[i].m_Pos += m_aNukePuffs[i].m_Vel * Dt;
		}
		else
		{
			m_aNukePuffs[i].m_Vel.y -= 45.0f * Dt;
			m_aNukePuffs[i].m_Vel *= std::pow(0.92f, Dt * 60.0f);
			m_aNukePuffs[i].m_Pos += m_aNukePuffs[i].m_Vel * Dt;
		}

		m_aNukePuffs[i].m_Rot += m_aNukePuffs[i].m_RotSpeed * Dt;
	}

	for(int i = 0; i < MAX_BLACK_HOLES; ++i)
	{
		if(!m_aBlackHoles[i].m_Active)
			continue;

		m_aBlackHoles[i].m_Life += Dt;
		m_aBlackHoles[i].m_SpinAngle += 2.2f * Dt;

		if(m_aBlackHoles[i].m_Life < m_aBlackHoles[i].m_MaxLife)
		{
			AddShake(3.0f * m_aBlackHoles[i].m_Scale * m_aBlackHoles[i].m_Alpha, 0.2f, m_aBlackHoles[i].m_Pos);
		}
		else
		{
			m_aBlackHoles[i].m_Active = false;

			CreateShockwave(m_aBlackHoles[i].m_Pos, 650.0f * m_aBlackHoles[i].m_Scale, 0.55f, ColorRGBA(0.95f, 0.35f, 1.0f, 0.90f * m_aBlackHoles[i].m_Alpha));
		}
	}

	for(int i = 0; i < MAX_QUASARS; ++i)
	{
		if(!m_aQuasars[i].m_Active)
			continue;

		m_aQuasars[i].m_Life += Dt;

		if(m_aQuasars[i].m_Life < m_aQuasars[i].m_MaxLife)
		{
			AddShake(5.5f * m_aQuasars[i].m_Scale * m_aQuasars[i].m_Alpha, 0.25f, m_aQuasars[i].m_Pos);
		}
		else
		{
			m_aQuasars[i].m_Active = false;
		}
	}
}

void CWeaponVfx::RenderActiveVfx() const
{
	const float GlobalTime = (float)m_EffectTime;

	for(int s = 0; s < MAX_SHOCKWAVES; ++s)
	{
		const auto &Ring = m_aShockwaves[s];
		if(!Ring.m_Active)
			continue;

		const float t = std::clamp(Ring.m_Life / Ring.m_MaxLife, 0.0f, 1.0f);
		if(t >= 1.0f)
			continue;

		const float InvT = 1.0f - t;
		const float BaseRadius = Ring.m_MaxRadius * (1.0f - InvT * InvT * InvT);
		const float Thickness = Ring.m_Thickness * (1.0f - t * 0.72f);
		const float Alpha = Ring.m_Color.a * (1.0f - t);

		if(Alpha <= 0.001f || BaseRadius <= 0.5f || !GameClient()->OptimizerAllowRenderPos(Ring.m_Pos))
			continue;

		Graphics()->TextureClear();
		Graphics()->BlendAdditive();
		Graphics()->QuadsBegin();

		const int Segments = 48;
		const float Step = 2.0f * pi / (float)Segments;

		switch(Ring.m_Mode)
		{
		case MODE_PRISM:
		{
			const float aRadiiMultipliers[3] = {1.05f, 0.98f, 0.91f};
			const ColorRGBA aChromaticCols[3] = {
				ColorRGBA(1.0f, 0.15f, 0.15f, 1.0f),
				ColorRGBA(0.15f, 1.0f, 0.20f, 1.0f),
				ColorRGBA(0.15f, 0.40f, 1.0f, 1.0f)};

			for(int c = 0; c < 3; ++c)
			{
				const float CurR = BaseRadius * aRadiiMultipliers[c];
				const float R_in = std::max(0.1f, CurR - Thickness * 0.45f);
				const float R_out = CurR + Thickness * 0.45f;
				Graphics()->SetColor(aChromaticCols[c].WithAlpha(Alpha * 0.75f));

				for(int i = 0; i < Segments; ++i)
				{
					const float a0 = i * Step;
					const float a1 = (i + 1) * Step;
					const vec2 p0_in = Ring.m_Pos + vec2(std::cos(a0), std::sin(a0)) * R_in;
					const vec2 p0_out = Ring.m_Pos + vec2(std::cos(a0), std::sin(a0)) * R_out;
					const vec2 p1_in = Ring.m_Pos + vec2(std::cos(a1), std::sin(a1)) * R_in;
					const vec2 p1_out = Ring.m_Pos + vec2(std::cos(a1), std::sin(a1)) * R_out;

					IGraphics::CFreeformItem Freeform(p0_in, p0_out, p1_in, p1_out);
					Graphics()->QuadsDrawFreeform(&Freeform, 1);
				}
			}
			break;
		}
		case MODE_PULSE:
		{
			const float R_in = std::max(0.1f, BaseRadius - Thickness * 0.5f);
			const float R_out = BaseRadius + Thickness * 0.5f;
			Graphics()->SetColor(Ring.m_Color.WithAlpha(Alpha));

			for(int i = 0; i < Segments; ++i)
			{
				const float a0 = i * Step;
				const float a1 = (i + 1) * Step;
				const vec2 p0_in = Ring.m_Pos + vec2(std::cos(a0), std::sin(a0)) * R_in;
				const vec2 p0_out = Ring.m_Pos + vec2(std::cos(a0), std::sin(a0)) * R_out;
				const vec2 p1_in = Ring.m_Pos + vec2(std::cos(a1), std::sin(a1)) * R_in;
				const vec2 p1_out = Ring.m_Pos + vec2(std::cos(a1), std::sin(a1)) * R_out;

				IGraphics::CFreeformItem Freeform(p0_in, p0_out, p1_in, p1_out);
				Graphics()->QuadsDrawFreeform(&Freeform, 1);
			}

			const int NumSpikes = 8;
			const float SpikeAlpha = Alpha * (1.0f - t);
			if(SpikeAlpha > 0.01f)
			{
				Graphics()->SetColor(ColorRGBA(0.85f, 1.0f, 1.0f, SpikeAlpha));
				for(int k = 0; k < NumSpikes; ++k)
				{
					const float BaseAngle = (float)k * (2.0f * pi / (float)NumSpikes) + GlobalTime * 2.5f;
					const vec2 Origin = Ring.m_Pos + vec2(std::cos(BaseAngle), std::sin(BaseAngle)) * BaseRadius;
					const float RayLen = Thickness * 2.8f * (1.0f - t);
					const vec2 RayDir = vec2(std::cos(BaseAngle), std::sin(BaseAngle));
					const vec2 RayNorm = vec2(-RayDir.y, RayDir.x) * 1.5f;

					IGraphics::CFreeformItem Spike(
						Origin - RayNorm, Origin + RayNorm,
						Origin + RayDir * RayLen - RayNorm * 0.2f,
						Origin + RayDir * RayLen + RayNorm * 0.2f);
					Graphics()->QuadsDrawFreeform(&Spike, 1);
				}
			}
			break;
		}
		case MODE_CRYSTAL:
		{
			const int PolySides = 10;
			const float PolyStep = 2.0f * pi / (float)PolySides;
			const float R_in = std::max(0.1f, BaseRadius - Thickness * 0.6f);
			const float R_out = BaseRadius + Thickness * 0.6f;
			Graphics()->SetColor(Ring.m_Color.WithAlpha(Alpha * 0.90f));

			for(int i = 0; i < PolySides; ++i)
			{
				const float a0 = i * PolyStep;
				const float a1 = (i + 1) * PolyStep;
				const vec2 p0_in = Ring.m_Pos + vec2(std::cos(a0), std::sin(a0)) * R_in;
				const vec2 p0_out = Ring.m_Pos + vec2(std::cos(a0), std::sin(a0)) * R_out;
				const vec2 p1_in = Ring.m_Pos + vec2(std::cos(a1), std::sin(a1)) * R_in;
				const vec2 p1_out = Ring.m_Pos + vec2(std::cos(a1), std::sin(a1)) * R_out;

				IGraphics::CFreeformItem Freeform(p0_in, p0_out, p1_in, p1_out);
				Graphics()->QuadsDrawFreeform(&Freeform, 1);
			}
			break;
		}
		default:
		{
			const float R_in = std::max(0.1f, BaseRadius - Thickness * 0.5f);
			const float R_out = BaseRadius + Thickness * 0.5f;
			Graphics()->SetColor(Ring.m_Color.WithAlpha(Alpha));

			for(int i = 0; i < Segments; ++i)
			{
				const float a0 = i * Step;
				const float a1 = (i + 1) * Step;
				const vec2 p0_in = Ring.m_Pos + vec2(std::cos(a0), std::sin(a0)) * R_in;
				const vec2 p0_out = Ring.m_Pos + vec2(std::cos(a0), std::sin(a0)) * R_out;
				const vec2 p1_in = Ring.m_Pos + vec2(std::cos(a1), std::sin(a1)) * R_in;
				const vec2 p1_out = Ring.m_Pos + vec2(std::cos(a1), std::sin(a1)) * R_out;

				IGraphics::CFreeformItem Freeform(p0_in, p0_out, p1_in, p1_out);
				Graphics()->QuadsDrawFreeform(&Freeform, 1);
			}

			const float R_out2 = BaseRadius + Thickness * 1.35f;
			Graphics()->SetColor(Ring.m_Color.WithAlpha(Alpha * 0.35f));
			for(int i = 0; i < Segments; ++i)
			{
				const float a0 = i * Step;
				const float a1 = (i + 1) * Step;
				const vec2 p0_in = Ring.m_Pos + vec2(std::cos(a0), std::sin(a0)) * R_out;
				const vec2 p0_out = Ring.m_Pos + vec2(std::cos(a0), std::sin(a0)) * R_out2;
				const vec2 p1_in = Ring.m_Pos + vec2(std::cos(a1), std::sin(a1)) * R_out;
				const vec2 p1_out = Ring.m_Pos + vec2(std::cos(a1), std::sin(a1)) * R_out2;

				IGraphics::CFreeformItem Freeform(p0_in, p0_out, p1_in, p1_out);
				Graphics()->QuadsDrawFreeform(&Freeform, 1);
			}
			break;
		}
		}

		Graphics()->QuadsEnd();
		Graphics()->BlendNormal();
	}

	for(int f = 0; f < MAX_FLASHES; ++f)
	{
		const auto &Flash = m_aFlashes[f];
		if(!Flash.m_Active || !GameClient()->OptimizerAllowRenderPos(Flash.m_Pos))
			continue;

		const float t = std::clamp(Flash.m_Life / Flash.m_MaxLife, 0.0f, 1.0f);
		const float Alpha = Flash.m_Color.a * (1.0f - t);
		const float Size = Flash.m_Size * (1.0f + t * 0.35f);

		if(Alpha <= 0.001f)
			continue;

		if(GameClient()->m_ParticlesSkinLoaded)
			Graphics()->TextureSet(GameClient()->m_ParticlesSkin.m_SpriteParticleBall);
		else
			Graphics()->TextureClear();
		Graphics()->BlendAdditive();
		Graphics()->QuadsBegin();

		Graphics()->SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, Alpha));
		IGraphics::CQuadItem CoreQuad(Flash.m_Pos.x - Size * 0.5f, Flash.m_Pos.y - Size * 0.5f, Size, Size);
		Graphics()->QuadsDrawTL(&CoreQuad, 1);

		Graphics()->SetColor(Flash.m_Color.WithAlpha(Alpha * 0.65f));
		IGraphics::CQuadItem FlareQuad(Flash.m_Pos.x - Size, Flash.m_Pos.y - Size, Size * 2.0f, Size * 2.0f);
		Graphics()->QuadsDrawTL(&FlareQuad, 1);

		Graphics()->QuadsEnd();
		Graphics()->BlendNormal();
	}

	const bool HasActiveDebris = std::any_of(std::begin(m_aDebris), std::end(m_aDebris), [](const auto &Effect) { return Effect.m_Active; });

	if(HasActiveDebris)
	{
		Graphics()->TextureClear();
		Graphics()->BlendAdditive();
		Graphics()->QuadsBegin();

		for(int i = 0; i < MAX_DEBRIS; ++i)
		{
			if(!m_aDebris[i].m_Active || !GameClient()->OptimizerAllowRenderPos(m_aDebris[i].m_Pos))
				continue;

			const float Progress = m_aDebris[i].m_Life / m_aDebris[i].m_MaxLife;
			const float Fade = 1.0f - Progress;
			const float Alpha = m_aDebris[i].m_Color.a * Fade;
			if(Alpha <= 0.001f)
				continue;

			const vec2 Center = m_aDebris[i].m_Pos;
			Graphics()->SetColor(m_aDebris[i].m_Color.WithAlpha(Alpha));

			if(m_aDebris[i].m_ShapeType == 1)
			{
				const float Size = m_aDebris[i].m_Size * Fade;
				const float Angle = m_aDebris[i].m_Rot;
				const vec2 Dir = vec2(std::cos(Angle), std::sin(Angle));
				const vec2 Norm = vec2(-Dir.y, Dir.x);

				IGraphics::CFreeformItem Rhombus(
					Center - Dir * (Size * 1.5f),
					Center + Norm * (Size * 0.85f),
					Center - Norm * (Size * 0.85f),
					Center + Dir * (Size * 1.5f));
				Graphics()->QuadsDrawFreeform(&Rhombus, 1);
			}
			else
			{
				const float Speed = length(m_aDebris[i].m_Vel);
				if(Speed <= 0.5f)
					continue;

				const vec2 Dir = m_aDebris[i].m_Vel / Speed;
				const vec2 Norm = vec2(-Dir.y, Dir.x);
				const float StretchLen = std::clamp(Speed * 0.035f, m_aDebris[i].m_Size * 0.8f, m_aDebris[i].m_Size * 3.0f);
				const float HalfThick = m_aDebris[i].m_Size * 0.40f * Fade;

				IGraphics::CFreeformItem StretchQuad(
					Center - Dir * StretchLen - Norm * HalfThick,
					Center - Dir * StretchLen + Norm * HalfThick,
					Center + Dir * StretchLen - Norm * HalfThick,
					Center + Dir * StretchLen + Norm * HalfThick);
				Graphics()->QuadsDrawFreeform(&StretchQuad, 1);
			}
		}

		Graphics()->QuadsEnd();
		Graphics()->BlendNormal();
	}

	const bool HasNukePuffs = std::any_of(std::begin(m_aNukePuffs), std::end(m_aNukePuffs), [](const auto &Effect) { return Effect.m_Active; });

	if(HasNukePuffs || std::any_of(std::begin(m_aBlackHoles), std::end(m_aBlackHoles), [](const auto &Effect) { return Effect.m_Active; }) ||
		std::any_of(std::begin(m_aQuasars), std::end(m_aQuasars), [](const auto &Effect) { return Effect.m_Active; }))
	{
		const bool HasPartSkin = GameClient()->m_ParticlesSkinLoaded;
		const bool HasExtraSkin = GameClient()->m_ExtrasSkinLoaded;

		Graphics()->BlendNormal();

		const auto RenderPuffLayer = [this](int Layer, IGraphics::CTextureHandle Texture, float SizeScale) {
			Graphics()->TextureSet(Texture);
			Graphics()->QuadsBegin();
			for(const auto &Puff : m_aNukePuffs)
			{
				if(!Puff.m_Active || Puff.m_Layer != Layer)
					continue;
				const float Progress = Puff.m_Life / Puff.m_MaxLife;
				const float Size = mix(Puff.m_StartSize, Puff.m_EndSize, Layer < 2 ? std::sqrt(Progress) : Progress) * SizeScale;
				const ColorRGBA Col = BlendCol(Puff.m_StartColor, Puff.m_EndColor, Progress);
				const float Alpha = Col.a * (1.0f - (Layer < 2 ? Progress * Progress : Progress));
				if(Alpha <= 0.001f)
					continue;
				Graphics()->SetColor(Col.WithAlpha(Alpha));
				Graphics()->QuadsSetRotation(Puff.m_Rot);
				IGraphics::CQuadItem Quad(Puff.m_Pos.x, Puff.m_Pos.y, Size, Size);
				Graphics()->QuadsDraw(&Quad, 1);
			}
			Graphics()->QuadsEnd();
		};
		if(HasPartSkin)
		{
			RenderPuffLayer(0, GameClient()->m_ParticlesSkin.m_SpriteParticleSmoke, 2.0f);
			RenderPuffLayer(1, GameClient()->m_ParticlesSkin.m_SpriteParticleSmoke, 2.0f);
		}

		if(HasPartSkin)
		{
			Graphics()->TextureSet(GameClient()->m_ParticlesSkin.m_SpriteParticleBall);
			Graphics()->QuadsBegin();

			for(int b = 0; b < MAX_BLACK_HOLES; ++b)
			{
				if(!m_aBlackHoles[b].m_Active)
					continue;

				const float Progress = m_aBlackHoles[b].m_Life / m_aBlackHoles[b].m_MaxLife;
				const float Fade = ((Progress < 0.06f) ? (Progress / 0.06f) : ((Progress > 0.94f) ? ((1.0f - Progress) / 0.06f) : 1.0f));
				const float VoidDiameter = 80.0f * m_aBlackHoles[b].m_Scale * Fade;
				const vec2 Center = m_aBlackHoles[b].m_Pos;

				Graphics()->SetColor(ColorRGBA(0.0f, 0.0f, 0.0f, m_aBlackHoles[b].m_Alpha));
				Graphics()->QuadsSetRotation(0.0f);
				IGraphics::CQuadItem VoidQuad(Center.x, Center.y, VoidDiameter, VoidDiameter);
				Graphics()->QuadsDraw(&VoidQuad, 1);
			}
			Graphics()->QuadsEnd();
		}

		Graphics()->BlendAdditive();

		if(HasPartSkin)
		{
			RenderPuffLayer(2, GameClient()->m_ParticlesSkin.m_SpriteParticleExpl, 2.2f);
			RenderPuffLayer(3, GameClient()->m_ParticlesSkin.m_SpriteParticleExpl, 2.5f);
		}
		if(HasExtraSkin)
			RenderPuffLayer(4, GameClient()->m_ExtrasSkin.m_SpriteParticleSparkle, 2.6f);

		if(HasPartSkin)
		{
			Graphics()->TextureSet(GameClient()->m_ParticlesSkin.m_SpriteParticleAirJump);
			Graphics()->QuadsBegin();
			for(int b = 0; b < MAX_BLACK_HOLES; ++b)
			{
				if(!m_aBlackHoles[b].m_Active)
					continue;

				const float Progress = m_aBlackHoles[b].m_Life / m_aBlackHoles[b].m_MaxLife;
				const float Fade = ((Progress < 0.06f) ? (Progress / 0.06f) : ((Progress > 0.94f) ? ((1.0f - Progress) / 0.06f) : 1.0f));
				const vec2 Center = m_aBlackHoles[b].m_Pos;
				const float Scale = m_aBlackHoles[b].m_Scale;
				const float Pulse = 1.0f + 0.08f * std::sin(GlobalTime * 14.0f);

				Graphics()->SetColor(ColorRGBA(0.95f, 0.30f, 1.0f, 0.90f * Fade * m_aBlackHoles[b].m_Alpha));
				Graphics()->QuadsSetRotation(m_aBlackHoles[b].m_SpinAngle);
				IGraphics::CQuadItem RingQuad(Center.x, Center.y, 110.0f * Scale * Pulse * Fade, 110.0f * Scale * Pulse * Fade);
				Graphics()->QuadsDraw(&RingQuad, 1);
			}
			Graphics()->QuadsEnd();

			Graphics()->TextureSet(GameClient()->m_ParticlesSkin.m_SpriteParticleBall);
			Graphics()->QuadsBegin();
			for(int b = 0; b < MAX_BLACK_HOLES; ++b)
			{
				if(!m_aBlackHoles[b].m_Active)
					continue;

				const float Progress = m_aBlackHoles[b].m_Life / m_aBlackHoles[b].m_MaxLife;
				const float Fade = ((Progress < 0.06f) ? (Progress / 0.06f) : ((Progress > 0.94f) ? ((1.0f - Progress) / 0.06f) : 1.0f));
				const vec2 Center = m_aBlackHoles[b].m_Pos;
				const float Scale = m_aBlackHoles[b].m_Scale;

				Graphics()->SetColor(ColorRGBA(0.70f, 0.15f, 1.0f, 0.65f * Fade * m_aBlackHoles[b].m_Alpha));
				Graphics()->QuadsSetRotation(0.0f);
				IGraphics::CQuadItem HaloQuad(Center.x, Center.y, 160.0f * Scale * Fade, 160.0f * Scale * Fade);
				Graphics()->QuadsDraw(&HaloQuad, 1);
			}
			Graphics()->QuadsEnd();
		}

		if(HasPartSkin)
		{
			Graphics()->TextureSet(GameClient()->m_ParticlesSkin.m_SpriteParticleBall);
			Graphics()->QuadsBegin();

			for(int q = 0; q < MAX_QUASARS; ++q)
			{
				if(!m_aQuasars[q].m_Active)
					continue;

				const float Progress = m_aQuasars[q].m_Life / m_aQuasars[q].m_MaxLife;
				const float Fade = ((Progress < 0.08f) ? (Progress / 0.08f) : ((Progress > 0.85f) ? ((1.0f - Progress) / 0.15f) : 1.0f));
				const vec2 Center = m_aQuasars[q].m_Pos;
				const float Scale = m_aQuasars[q].m_Scale;
				const vec2 JetDir = normalize(vec2(0.18f, -0.98f));

				const int JetNodes = 28;
				const float TotalJetLen = 1500.0f * Scale * Fade;
				for(int n = 0; n < JetNodes; ++n)
				{
					const float Dist = ((float)n / (float)JetNodes) * TotalJetLen;
					const vec2 NodePos = Center + JetDir * Dist;
					const float NodeDiam = (45.0f + 25.0f * (float)n / (float)JetNodes) * Scale * Fade;

					Graphics()->SetColor(ColorRGBA(0.15f, 0.85f, 1.0f, 0.45f * (1.0f - (float)n / (float)JetNodes * 0.5f) * Fade * m_aQuasars[q].m_Alpha));
					Graphics()->QuadsSetRotation(0.0f);
					IGraphics::CQuadItem SheathQuad(NodePos.x, NodePos.y, NodeDiam * 1.8f, NodeDiam * 1.8f);
					Graphics()->QuadsDraw(&SheathQuad, 1);

					Graphics()->SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.85f * (1.0f - (float)n / (float)JetNodes * 0.4f) * Fade * m_aQuasars[q].m_Alpha));
					IGraphics::CQuadItem CoreQuad(NodePos.x, NodePos.y, NodeDiam * 0.8f, NodeDiam * 0.8f);
					Graphics()->QuadsDraw(&CoreQuad, 1);
				}

				const int OppNodes = 14;
				const float OppJetLen = TotalJetLen * 0.55f;
				for(int n = 0; n < OppNodes; ++n)
				{
					const float Dist = ((float)n / (float)OppNodes) * OppJetLen;
					const vec2 NodePos = Center - JetDir * Dist;
					const float NodeDiam = (40.0f + 20.0f * (float)n / (float)OppNodes) * Scale * Fade;

					Graphics()->SetColor(ColorRGBA(0.20f, 0.80f, 1.0f, 0.35f * Fade * m_aQuasars[q].m_Alpha));
					IGraphics::CQuadItem OppQuad(NodePos.x, NodePos.y, NodeDiam * 1.5f, NodeDiam * 1.5f);
					Graphics()->QuadsDraw(&OppQuad, 1);
				}

				Graphics()->SetColor(ColorRGBA(0.85f, 0.95f, 1.0f, 0.95f * Fade * m_aQuasars[q].m_Alpha));
				IGraphics::CQuadItem CentralCore(Center.x, Center.y, 220.0f * Scale * Fade, 220.0f * Scale * Fade);
				Graphics()->QuadsDraw(&CentralCore, 1);
			}
			Graphics()->QuadsEnd();

			if(HasExtraSkin)
			{
				Graphics()->TextureSet(GameClient()->m_ExtrasSkin.m_SpriteParticleSparkle);
				Graphics()->QuadsBegin();
				for(int q = 0; q < MAX_QUASARS; ++q)
				{
					if(!m_aQuasars[q].m_Active)
						continue;

					const float Progress = m_aQuasars[q].m_Life / m_aQuasars[q].m_MaxLife;
					const float Fade = ((Progress < 0.08f) ? (Progress / 0.08f) : ((Progress > 0.85f) ? ((1.0f - Progress) / 0.15f) : 1.0f));
					const vec2 Center = m_aQuasars[q].m_Pos;
					const float Scale = m_aQuasars[q].m_Scale;

					Graphics()->SetColor(ColorRGBA(0.50f, 0.88f, 1.0f, 0.95f * Fade * m_aQuasars[q].m_Alpha));
					Graphics()->QuadsSetRotation(0.0f);
					IGraphics::CQuadItem StarCore(Center.x, Center.y, 350.0f * Scale * Fade, 80.0f * Scale * Fade);
					Graphics()->QuadsDraw(&StarCore, 1);
				}
				Graphics()->QuadsEnd();
			}
		}

		Graphics()->BlendNormal();
	}
}
