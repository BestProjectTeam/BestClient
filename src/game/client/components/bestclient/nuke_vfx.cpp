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

bool CWeaponVfx::TriggerExplosion(vec2 Pos, float Alpha)
{
	const bool Glow = g_Config.m_BcRocketGlow && g_Config.m_BcRocketGlowPower > 0;
	if(!Glow && !g_Config.m_BcMemeNukeExplosion)
		return false;
	if(Alpha <= 0.001f || GameClient()->OptimizerDisableParticles() || !GameClient()->OptimizerAllowRenderPos(Pos) ||
		(g_Config.m_ClFocusMode && g_Config.m_ClFocusModeHideEffects))
		return true;
	const ColorRGBA Color(1.0f, 0.65f, 0.20f, Alpha);
	if(Glow)
		RenderExplosion(Pos, Color, g_Config.m_BcRocketGlowMode, (float)g_Config.m_BcRocketGlowPower);
	if(g_Config.m_BcMemeNukeExplosion)
	{
		const int64_t Now = 1 + (int64_t)(m_EffectTime * 1000.0);
		int NearbyCount = 0;
		for(const auto &Explosion : m_aRecentExplosions)
		{
			if(Explosion.m_TimeMs > 0 && Now >= Explosion.m_TimeMs && Now - Explosion.m_TimeMs < 550 && distance(Explosion.m_Pos, Pos) < 280.0f)
				++NearbyCount;
		}
		m_aRecentExplosions[m_RecentExplosionIndex] = {Pos, Now};
		m_RecentExplosionIndex = (m_RecentExplosionIndex + 1) % MAX_RECENT_EXPLOSIONS;
		m_NukeAlpha = Alpha;
		TriggerNukeExplosion(Pos, Color, g_Config.m_BcMemeNukeScale / 100.0f, std::clamp(1 + NearbyCount, 1, 4));
		m_NukeAlpha = 1.0f;
	}
	return true;
}

void CWeaponVfx::AddShake(float Intensity, float Duration, vec2 SourcePos)
{
	if(g_Config.m_BcMemeNukeShake <= 0 || !g_Config.m_BcMemeNukeExplosion)
		return;
	const float Dist = distance(GameClient()->m_Camera.m_Center, SourcePos);
	const float Shake = Intensity * m_NukeAlpha * std::clamp(1.0f - Dist / 3000.0f, 0.20f, 1.0f);
	if(Shake > m_ShakeIntensity * (m_ShakeTimer / std::max(0.001f, m_ShakeDuration)))
	{
		m_ShakeIntensity = Shake;
		m_ShakeDuration = Duration;
		m_ShakeTimer = Duration;
	}
}

void CWeaponVfx::ApplyCameraShake(vec2 &Center)
{
	if(!g_Config.m_BcMemeNukeExplosion || g_Config.m_BcMemeNukeShake <= 0 || GameClient()->OptimizerDisableParticles() ||
		(g_Config.m_ClFocusMode && g_Config.m_ClFocusModeHideEffects))
	{
		m_ShakeTimer = 0.0f;
		return;
	}
	if(m_ShakeTimer <= 0.0f || (Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK))
		return;
	const float Dt = Client()->RenderFrameTime() * GameClient()->GetAnimationPlaybackSpeed();
	m_ShakeTime += Dt;
	const float Progress = m_ShakeTimer / std::max(0.001f, m_ShakeDuration);
	const float Strength = m_ShakeIntensity * Progress * Progress * g_Config.m_BcMemeNukeShake / 100.0f;
	const float t = m_ShakeTime * 85.0f;
	Center += vec2(std::sin(t * 1.37f) * 0.75f + std::cos(t * 2.41f) * 0.45f,
			  std::cos(t * 1.73f) * 0.75f + std::sin(t * 3.11f) * 0.45f) *
		  Strength;
	m_ShakeTimer = std::max(0.0f, m_ShakeTimer - Dt);
}

void CWeaponVfx::RenderExplosion(vec2 Pos, const ColorRGBA &Color, int Mode, float Power)
{
	const float ScaledPower = std::clamp(Power / 50.0f, 0.4f, 2.0f);

	ColorRGBA UserExplosionCol = Color;
	if(g_Config.m_BcRocketGlowColor != 0)
	{
		ColorHSLA Hsl(g_Config.m_BcRocketGlowColor);
		if(Hsl.l < 0.08f)
			Hsl.l = 0.55f;
		if(Hsl.s < 0.08f)
			Hsl.s = 0.85f;
		UserExplosionCol = color_cast<ColorRGBA>(Hsl).WithAlpha(Color.a);
	}
	else if(Mode == MODE_PULSE)
	{
		UserExplosionCol = ColorRGBA(0.20f, 0.85f, 1.0f, Color.a);
	}

	for(int i = 0; i < MAX_FLASHES; ++i)
	{
		if(!m_aFlashes[i].m_Active)
		{
			m_aFlashes[i].m_Active = true;
			m_aFlashes[i].m_Pos = Pos;
			m_aFlashes[i].m_Life = 0.0f;
			m_aFlashes[i].m_MaxLife = 0.065f;
			m_aFlashes[i].m_Size = 65.0f * ScaledPower;
			m_aFlashes[i].m_Color = (Mode == MODE_PULSE) ? ColorRGBA(0.70f, 0.95f, 1.0f, Color.a) :
								       ((Mode == MODE_PRISM) ? ColorRGBA(1.0f, 0.60f, 0.95f, Color.a) : UserExplosionCol);
			break;
		}
	}

	for(int i = 0; i < MAX_SHOCKWAVES; ++i)
	{
		if(!m_aShockwaves[i].m_Active)
		{
			m_aShockwaves[i].m_Active = true;
			m_aShockwaves[i].m_Pos = Pos;
			m_aShockwaves[i].m_Life = 0.0f;
			m_aShockwaves[i].m_MaxLife = 0.36f;
			m_aShockwaves[i].m_MaxRadius = 120.0f * ScaledPower;
			m_aShockwaves[i].m_Thickness = 6.5f * ScaledPower;
			m_aShockwaves[i].m_Color = UserExplosionCol;
			m_aShockwaves[i].m_Mode = Mode;
			break;
		}
	}

	const int NumSparks = (Mode == MODE_CRYSTAL) ? 12 : 20;
	for(int i = 0; i < NumSparks; ++i)
	{
		int Slot = -1;
		for(int d = 0; d < MAX_DEBRIS; ++d)
		{
			if(!m_aDebris[d].m_Active)
			{
				Slot = d;
				break;
			}
		}

		if(Slot == -1)
			break;

		const float Angle = random_angle();
		const float Speed = random_float(320.0f, 980.0f) * ScaledPower;

		m_aDebris[Slot].m_Active = true;
		m_aDebris[Slot].m_Pos = Pos;
		m_aDebris[Slot].m_Vel = vec2(std::cos(Angle), std::sin(Angle)) * Speed;
		m_aDebris[Slot].m_Life = 0.0f;
		m_aDebris[Slot].m_MaxLife = random_float(0.30f, 0.65f);
		m_aDebris[Slot].m_Size = (Mode == MODE_CRYSTAL) ? random_float(5.5f, 8.5f) : random_float(2.8f, 4.8f);
		m_aDebris[Slot].m_ShapeType = (Mode == MODE_CRYSTAL) ? 1 : 0;
		m_aDebris[Slot].m_Rot = random_angle();
		m_aDebris[Slot].m_RotSpeed = random_float(-12.0f, 12.0f);

		if(Mode == MODE_CRYSTAL)
			m_aDebris[Slot].m_Color = (i % 2 == 0) ? UserExplosionCol : ColorRGBA(0.85f, 0.95f, 1.0f, Color.a);
		else if(Mode == MODE_PRISM)
			m_aDebris[Slot].m_Color = color_cast<ColorRGBA>(ColorHSLA((float)i / (float)NumSparks, 1.0f, 0.65f));
		else
			m_aDebris[Slot].m_Color = UserExplosionCol;
		m_aDebris[Slot].m_Color.a = Color.a;
	}
}

void CWeaponVfx::CancelLowerTierVfx(vec2 Pos, int NewStackLevel, float Radius)
{
	for(int i = 0; i < MAX_NUKE_PUFFS; ++i)
	{
		if(m_aNukePuffs[i].m_Active && m_aNukePuffs[i].m_StackLevel < NewStackLevel)
		{
			if(distance(m_aNukePuffs[i].m_Pos, Pos) < Radius || distance(m_aNukePuffs[i].m_OriginPos, Pos) < Radius)
			{
				m_aNukePuffs[i].m_Active = false;
			}
		}
	}

	if(NewStackLevel >= 3)
	{
		for(int b = 0; b < MAX_BLACK_HOLES; ++b)
		{
			if(m_aBlackHoles[b].m_Active && distance(m_aBlackHoles[b].m_Pos, Pos) < Radius)
			{
				m_aBlackHoles[b].m_Active = false;
			}
		}
	}
}

void CWeaponVfx::TriggerBombExplosion(vec2 Pos, const ColorRGBA &, float Scale)
{
	Scale = std::clamp(Scale * 0.95f, 0.3f, 4.0f);

	AddShake(22.0f * Scale, 0.40f, Pos);

	CreateShockwave(Pos, 220.0f * Scale, 0.38f, ColorRGBA(1.0f, 0.75f, 0.20f, 0.90f));

	auto SpawnPuff = [this, Pos](vec2 P, vec2 V, float Life, float SSize, float ESize, ColorRGBA SCol, ColorRGBA ECol, int Layer) {
		for(int i = 0; i < MAX_NUKE_PUFFS; ++i)
		{
			if(!m_aNukePuffs[i].m_Active)
			{
				m_aNukePuffs[i].m_Active = true;
				m_aNukePuffs[i].m_Pos = P;
				m_aNukePuffs[i].m_OriginPos = Pos;
				m_aNukePuffs[i].m_Vel = V;
				m_aNukePuffs[i].m_Life = 0.0f;
				m_aNukePuffs[i].m_MaxLife = Life;
				m_aNukePuffs[i].m_StartSize = SSize;
				m_aNukePuffs[i].m_EndSize = ESize;
				m_aNukePuffs[i].m_Rot = random_float(0.0f, 2.0f * pi);
				m_aNukePuffs[i].m_RotSpeed = random_float(-2.2f, 2.2f);
				m_aNukePuffs[i].m_StartColor = SCol.WithMultipliedAlpha(m_NukeAlpha);
				m_aNukePuffs[i].m_EndColor = ECol.WithMultipliedAlpha(m_NukeAlpha);
				m_aNukePuffs[i].m_Layer = Layer;
				m_aNukePuffs[i].m_StackLevel = 1;
				m_aNukePuffs[i].m_OrbitRadius = 0.0f;
				break;
			}
		}
	};

	for(int i = 0; i < 14; ++i)
	{
		const float Angle = random_float(0.0f, 2.0f * pi);
		const float Spd = random_float(90.0f, 320.0f) * Scale;
		const vec2 Vel = vec2(std::cos(Angle), std::sin(Angle)) * Spd;
		const vec2 P = Pos + vec2(random_float(-10.0f, 10.0f), random_float(-10.0f, 10.0f)) * Scale;
		const float Life = random_float(0.6f, 1.2f);
		const float SSize = random_float(24.0f, 40.0f) * Scale;
		const float ESize = random_float(65.0f, 110.0f) * Scale;
		SpawnPuff(P, Vel, Life, SSize, ESize, ColorRGBA(1.0f, 0.95f, 0.65f, 1.0f), ColorRGBA(1.0f, 0.50f, 0.05f, 0.0f), 2);
	}

	for(int i = 0; i < 18; ++i)
	{
		const float Angle = random_float(0.0f, 2.0f * pi);
		const float Spd = random_float(60.0f, 240.0f) * Scale;
		const vec2 Vel = vec2(std::cos(Angle), std::sin(Angle) * 0.8f) * Spd;
		const vec2 P = Pos + vec2(random_float(-15.0f, 15.0f), random_float(-15.0f, 15.0f)) * Scale;
		const float Life = random_float(0.9f, 1.8f);
		const float SSize = random_float(30.0f, 52.0f) * Scale;
		const float ESize = random_float(80.0f, 140.0f) * Scale;
		SpawnPuff(P, Vel, Life, SSize, ESize, ColorRGBA(0.24f, 0.10f, 0.05f, 0.88f), ColorRGBA(0.08f, 0.03f, 0.02f, 0.0f), 0);
	}

	for(int i = 0; i < 16; ++i)
	{
		const float Angle = random_float(0.0f, 2.0f * pi);
		const float Spd = random_float(320.0f, 850.0f) * Scale;
		const vec2 Vel = vec2(std::cos(Angle), std::sin(Angle)) * Spd;
		const vec2 P = Pos + vec2(random_float(-6.0f, 6.0f), random_float(-6.0f, 6.0f)) * Scale;
		const float Life = random_float(0.7f, 1.4f);
		const float SSize = random_float(6.0f, 12.0f) * Scale;
		const float ESize = random_float(2.0f, 5.0f) * Scale;
		SpawnPuff(P, Vel, Life, SSize, ESize, ColorRGBA(1.0f, 0.90f, 0.45f, 1.0f), ColorRGBA(1.0f, 0.25f, 0.02f, 0.0f), 3);
	}
}

void CWeaponVfx::TriggerNuclearMushroom(vec2 Pos, const ColorRGBA &, float Scale)
{
	Scale = std::clamp(Scale * 1.55f, 0.3f, 6.0f);

	AddShake(58.0f * Scale, 1.15f, Pos);

	CreateShockwave(Pos, 550.0f * Scale, 0.65f, ColorRGBA(1.0f, 0.95f, 0.70f, 0.95f));
	CreateShockwave(Pos, 950.0f * Scale, 1.15f, ColorRGBA(1.0f, 0.55f, 0.10f, 0.88f));
	CreateShockwave(Pos, 1500.0f * Scale, 1.65f, ColorRGBA(0.85f, 0.20f, 0.05f, 0.65f));

	auto SpawnPuff = [this, Pos](vec2 P, vec2 V, float Life, float SSize, float ESize, ColorRGBA SCol, ColorRGBA ECol, int Layer) {
		for(int i = 0; i < MAX_NUKE_PUFFS; ++i)
		{
			if(!m_aNukePuffs[i].m_Active)
			{
				m_aNukePuffs[i].m_Active = true;
				m_aNukePuffs[i].m_Pos = P;
				m_aNukePuffs[i].m_OriginPos = Pos;
				m_aNukePuffs[i].m_Vel = V;
				m_aNukePuffs[i].m_Life = 0.0f;
				m_aNukePuffs[i].m_MaxLife = Life;
				m_aNukePuffs[i].m_StartSize = SSize;
				m_aNukePuffs[i].m_EndSize = ESize;
				m_aNukePuffs[i].m_Rot = random_float(0.0f, 2.0f * pi);
				m_aNukePuffs[i].m_RotSpeed = random_float(-1.8f, 1.8f);
				m_aNukePuffs[i].m_StartColor = SCol.WithMultipliedAlpha(m_NukeAlpha);
				m_aNukePuffs[i].m_EndColor = ECol.WithMultipliedAlpha(m_NukeAlpha);
				m_aNukePuffs[i].m_Layer = Layer;
				m_aNukePuffs[i].m_StackLevel = 2;
				m_aNukePuffs[i].m_OrbitRadius = 0.0f;
				break;
			}
		}
	};

	const float StemH = 340.0f * Scale;

	for(int i = 0; i < 40; ++i)
	{
		const float ProgressY = (float)i / 40.0f;
		const float YOff = -ProgressY * StemH;
		const float XOff = random_float(-18.0f, 18.0f) * Scale;
		const vec2 P = Pos + vec2(XOff, YOff);
		const vec2 Vel = vec2(random_float(-15.0f, 15.0f), random_float(-180.0f, -60.0f)) * Scale;
		const float Life = random_float(2.4f, 4.0f);
		const float SSize = random_float(22.0f, 38.0f) * Scale;
		const float ESize = random_float(48.0f, 85.0f) * Scale;
		SpawnPuff(P, Vel, Life, SSize, ESize, ColorRGBA(0.18f, 0.08f, 0.05f, 0.95f), ColorRGBA(0.08f, 0.03f, 0.02f, 0.0f), 0);
	}

	for(int i = 0; i < 25; ++i)
	{
		const float ProgressY = (float)i / 25.0f;
		const float YOff = -ProgressY * StemH;
		const float XOff = random_float(-10.0f, 10.0f) * Scale;
		const vec2 P = Pos + vec2(XOff, YOff);
		const vec2 Vel = vec2(random_float(-8.0f, 8.0f), random_float(-220.0f, -90.0f)) * Scale;
		const float Life = random_float(1.8f, 3.2f);
		const float SSize = random_float(18.0f, 32.0f) * Scale;
		const float ESize = random_float(40.0f, 70.0f) * Scale;
		SpawnPuff(P, Vel, Life, SSize, ESize, ColorRGBA(1.0f, 0.70f, 0.15f, 0.95f), ColorRGBA(0.60f, 0.12f, 0.02f, 0.0f), 1);
	}

	for(int i = 0; i < 28; ++i)
	{
		const float XOff = random_float(-140.0f, 140.0f) * Scale;
		const float YOff = -StemH * 0.72f + random_float(-12.0f, 12.0f) * Scale;
		const vec2 P = Pos + vec2(XOff, YOff);
		const vec2 Vel = vec2((XOff > 0 ? 1.0f : -1.0f) * random_float(60.0f, 180.0f) * Scale, random_float(-25.0f, -5.0f) * Scale);
		const float Life = random_float(2.0f, 3.6f);
		const float SSize = random_float(30.0f, 48.0f) * Scale;
		const float ESize = random_float(75.0f, 130.0f) * Scale;
		SpawnPuff(P, Vel, Life, SSize, ESize, ColorRGBA(1.0f, 0.85f, 0.40f, 0.90f), ColorRGBA(0.40f, 0.10f, 0.05f, 0.0f), 1);
	}

	const vec2 CapCenter = Pos + vec2(0.0f, -StemH * 1.05f);

	for(int i = 0; i < 35; ++i)
	{
		const float Angle = random_float(-pi * 0.95f, -pi * 0.05f);
		const float Rad = random_float(10.0f, 90.0f) * Scale;
		const vec2 P = CapCenter + vec2(std::cos(Angle) * Rad, std::sin(Angle) * (Rad * 0.65f));
		const vec2 Vel = vec2(std::cos(Angle), std::sin(Angle) * 0.75f) * random_float(80.0f, 260.0f) * Scale;
		const float Life = random_float(1.8f, 3.2f);
		const float SSize = random_float(38.0f, 60.0f) * Scale;
		const float ESize = random_float(110.0f, 180.0f) * Scale;
		SpawnPuff(P, Vel, Life, SSize, ESize, ColorRGBA(1.0f, 0.98f, 0.75f, 1.0f), ColorRGBA(1.0f, 0.70f, 0.15f, 0.0f), 2);
	}

	for(int i = 0; i < 50; ++i)
	{
		const float Angle = random_float(-pi * 1.05f, 0.05f);
		const float Rad = random_float(30.0f, 150.0f) * Scale;
		const vec2 P = CapCenter + vec2(std::cos(Angle) * Rad, std::sin(Angle) * (Rad * 0.70f));
		const vec2 Vel = vec2(std::cos(Angle), std::sin(Angle) * 0.8f) * random_float(70.0f, 240.0f) * Scale;
		const float Life = random_float(2.2f, 4.0f);
		const float SSize = random_float(45.0f, 70.0f) * Scale;
		const float ESize = random_float(130.0f, 230.0f) * Scale;
		SpawnPuff(P, Vel, Life, SSize, ESize, ColorRGBA(1.0f, 0.60f, 0.08f, 0.95f), ColorRGBA(0.70f, 0.12f, 0.02f, 0.0f), 1);
	}

	for(int i = 0; i < 40; ++i)
	{
		const float Angle = random_float(-pi * 1.10f, 0.10f);
		const float Rad = random_float(60.0f, 180.0f) * Scale;
		const vec2 P = CapCenter + vec2(std::cos(Angle) * Rad, std::sin(Angle) * (Rad * 0.75f));
		const vec2 Vel = vec2(std::cos(Angle), std::sin(Angle) * 0.7f) * random_float(50.0f, 190.0f) * Scale;
		const float Life = random_float(2.6f, 4.5f);
		const float SSize = random_float(50.0f, 85.0f) * Scale;
		const float ESize = random_float(160.0f, 280.0f) * Scale;
		SpawnPuff(P, Vel, Life, SSize, ESize, ColorRGBA(0.28f, 0.08f, 0.04f, 0.90f), ColorRGBA(0.10f, 0.03f, 0.02f, 0.0f), 0);
	}

	for(int i = 0; i < 32; ++i)
	{
		const float Dir = (i % 2 == 0) ? 1.0f : -1.0f;
		const float Spd = random_float(450.0f, 1250.0f) * Scale;
		const vec2 Vel = vec2(Dir * Spd, random_float(-140.0f, -15.0f) * Scale);
		const vec2 P = Pos + vec2(random_float(-20.0f, 20.0f), random_float(-10.0f, 5.0f)) * Scale;
		const float Life = random_float(1.2f, 2.8f);
		const float SSize = random_float(36.0f, 65.0f) * Scale;
		const float ESize = random_float(130.0f, 230.0f) * Scale;
		SpawnPuff(P, Vel, Life, SSize, ESize, ColorRGBA(1.0f, 0.55f, 0.12f, 0.90f), ColorRGBA(0.28f, 0.08f, 0.04f, 0.0f), 0);
	}

	for(int i = 0; i < 35; ++i)
	{
		const float Angle = -pi * 0.5f + random_float(-1.30f, 1.30f);
		const float Spd = random_float(500.0f, 1400.0f) * Scale;
		const vec2 Vel = vec2(std::cos(Angle), std::sin(Angle)) * Spd;
		const vec2 P = Pos + vec2(random_float(-12.0f, 12.0f), random_float(-20.0f, 5.0f)) * Scale;
		const float Life = random_float(1.4f, 2.8f);
		const float SSize = random_float(8.0f, 16.0f) * Scale;
		const float ESize = random_float(2.5f, 6.0f) * Scale;
		SpawnPuff(P, Vel, Life, SSize, ESize, ColorRGBA(1.0f, 0.95f, 0.60f, 1.0f), ColorRGBA(1.0f, 0.35f, 0.05f, 0.0f), 3);
	}
}

void CWeaponVfx::TriggerBlackHoleSingularity(vec2 Pos, const ColorRGBA &, float Scale)
{
	Scale = std::clamp(Scale * 1.85f, 0.4f, 6.0f);

	int Slot = -1;
	for(int i = 0; i < MAX_BLACK_HOLES; ++i)
	{
		if(!m_aBlackHoles[i].m_Active)
		{
			Slot = i;
			break;
		}
	}
	if(Slot != -1)
	{
		m_aBlackHoles[Slot].m_Active = true;
		m_aBlackHoles[Slot].m_Pos = Pos;
		m_aBlackHoles[Slot].m_Life = 0.0f;
		m_aBlackHoles[Slot].m_MaxLife = 10.0f;
		m_aBlackHoles[Slot].m_Scale = Scale;
		m_aBlackHoles[Slot].m_SpinAngle = 0.0f;
		m_aBlackHoles[Slot].m_Alpha = m_NukeAlpha;
	}

	AddShake(75.0f * Scale, 1.4f, Pos);
	CreateShockwave(Pos, 450.0f * Scale, 0.65f, ColorRGBA(0.95f, 0.40f, 1.0f, 0.95f));
	CreateShockwave(Pos, 850.0f * Scale, 1.15f, ColorRGBA(0.40f, 0.15f, 0.95f, 0.90f));
	CreateShockwave(Pos, 1400.0f * Scale, 1.75f, ColorRGBA(0.15f, 0.85f, 1.0f, 0.70f));

	auto SpawnOrbitPuff = [this](vec2 Origin, float Rad, float Ang, float Spd, float Life, float SSize, float ESize, ColorRGBA SCol, ColorRGBA ECol, int Layer) {
		for(int i = 0; i < MAX_NUKE_PUFFS; ++i)
		{
			if(!m_aNukePuffs[i].m_Active)
			{
				m_aNukePuffs[i].m_Active = true;
				m_aNukePuffs[i].m_OriginPos = Origin;
				m_aNukePuffs[i].m_OrbitRadius = Rad;
				m_aNukePuffs[i].m_OrbitAngle = Ang;
				m_aNukePuffs[i].m_OrbitSpeed = Spd;
				m_aNukePuffs[i].m_Pos = Origin + vec2(std::cos(Ang), std::sin(Ang)) * Rad;
				m_aNukePuffs[i].m_Vel = vec2(0, 0);
				m_aNukePuffs[i].m_Life = 0.0f;
				m_aNukePuffs[i].m_MaxLife = Life;
				m_aNukePuffs[i].m_StartSize = SSize;
				m_aNukePuffs[i].m_EndSize = ESize;
				m_aNukePuffs[i].m_Rot = random_float(0.0f, 2.0f * pi);
				m_aNukePuffs[i].m_RotSpeed = random_float(-1.5f, 1.5f);
				m_aNukePuffs[i].m_StartColor = SCol.WithMultipliedAlpha(m_NukeAlpha);
				m_aNukePuffs[i].m_EndColor = ECol.WithMultipliedAlpha(m_NukeAlpha);
				m_aNukePuffs[i].m_Layer = Layer;
				m_aNukePuffs[i].m_StackLevel = 3;
				break;
			}
		}
	};

	for(int i = 0; i < 70; ++i)
	{
		const float Rad = random_float(38.0f, 220.0f) * Scale;
		const float Ang = random_float(0.0f, 2.0f * pi);
		const float Spd = random_float(1.5f, 4.5f);
		const float Life = random_float(8.5f, 10.0f);
		const float SSize = random_float(25.0f, 50.0f) * Scale;
		const float ESize = random_float(45.0f, 90.0f) * Scale;

		ColorRGBA AccretionCol = (i % 3 == 0) ? ColorRGBA(0.95f, 0.25f, 1.0f, 0.90f) :
							((i % 3 == 1) ? ColorRGBA(0.65f, 0.15f, 0.95f, 0.90f) : ColorRGBA(0.20f, 0.90f, 1.0f, 0.85f));

		SpawnOrbitPuff(Pos, Rad, Ang, Spd, Life, SSize, ESize, AccretionCol, ColorRGBA(AccretionCol.r, AccretionCol.g, AccretionCol.b, 0.0f), 1);
	}

	for(int i = 0; i < 35; ++i)
	{
		const float Rad = random_float(60.0f, 260.0f) * Scale;
		const float Ang = random_float(0.0f, 2.0f * pi);
		const float Spd = random_float(0.8f, 2.2f);
		const float Life = random_float(9.0f, 10.0f);
		const float SSize = random_float(45.0f, 85.0f) * Scale;
		const float ESize = random_float(80.0f, 150.0f) * Scale;
		SpawnOrbitPuff(Pos, Rad, Ang, Spd, Life, SSize, ESize, ColorRGBA(0.25f, 0.05f, 0.35f, 0.80f), ColorRGBA(0.08f, 0.02f, 0.12f, 0.0f), 0);
	}

	for(int i = 0; i < 40; ++i)
	{
		const float Rad = random_float(45.0f, 280.0f) * Scale;
		const float Ang = random_float(0.0f, 2.0f * pi);
		const float Spd = random_float(2.0f, 5.5f);
		const float Life = random_float(8.0f, 10.0f);
		const float SSize = random_float(6.0f, 14.0f) * Scale;
		const float ESize = random_float(2.0f, 8.0f) * Scale;

		ColorRGBA StarCol = (i % 2 == 0) ? ColorRGBA(1.0f, 0.85f, 1.0f, 1.0f) : ColorRGBA(0.60f, 0.95f, 1.0f, 1.0f);
		SpawnOrbitPuff(Pos, Rad, Ang, Spd, Life, SSize, ESize, StarCol, ColorRGBA(StarCol.r, StarCol.g, StarCol.b, 0.0f), 4);
	}
}

void CWeaponVfx::TriggerQuasarHyperBlast(vec2 Pos, const ColorRGBA &, float Scale)
{
	Scale = std::clamp(Scale * 2.30f, 0.5f, 8.0f);

	int Slot = -1;
	for(int i = 0; i < MAX_QUASARS; ++i)
	{
		if(!m_aQuasars[i].m_Active)
		{
			Slot = i;
			break;
		}
	}
	if(Slot != -1)
	{
		m_aQuasars[Slot].m_Active = true;
		m_aQuasars[Slot].m_Pos = Pos;
		m_aQuasars[Slot].m_Life = 0.0f;
		m_aQuasars[Slot].m_MaxLife = 6.0f;
		m_aQuasars[Slot].m_Scale = Scale;
		m_aQuasars[Slot].m_Alpha = m_NukeAlpha;
	}

	AddShake(95.0f * Scale, 2.2f, Pos);

	CreateShockwave(Pos, 700.0f * Scale, 0.65f, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	CreateShockwave(Pos, 1400.0f * Scale, 1.20f, ColorRGBA(0.30f, 0.95f, 1.0f, 0.95f));
	CreateShockwave(Pos, 2400.0f * Scale, 1.85f, ColorRGBA(1.0f, 0.45f, 0.15f, 0.85f));
	CreateShockwave(Pos, 3800.0f * Scale, 2.60f, ColorRGBA(0.85f, 0.15f, 0.90f, 0.70f));

	auto SpawnOrbitPuff = [this](vec2 Origin, float Rad, float Ang, float Spd, float Life, float SSize, float ESize, ColorRGBA SCol, ColorRGBA ECol, int Layer) {
		for(int i = 0; i < MAX_NUKE_PUFFS; ++i)
		{
			if(!m_aNukePuffs[i].m_Active)
			{
				m_aNukePuffs[i].m_Active = true;
				m_aNukePuffs[i].m_OriginPos = Origin;
				m_aNukePuffs[i].m_OrbitRadius = Rad;
				m_aNukePuffs[i].m_OrbitAngle = Ang;
				m_aNukePuffs[i].m_OrbitSpeed = Spd;
				m_aNukePuffs[i].m_Pos = Origin + vec2(std::cos(Ang), std::sin(Ang)) * Rad;
				m_aNukePuffs[i].m_Vel = vec2(0, 0);
				m_aNukePuffs[i].m_Life = 0.0f;
				m_aNukePuffs[i].m_MaxLife = Life;
				m_aNukePuffs[i].m_StartSize = SSize;
				m_aNukePuffs[i].m_EndSize = ESize;
				m_aNukePuffs[i].m_Rot = random_float(0.0f, 2.0f * pi);
				m_aNukePuffs[i].m_RotSpeed = random_float(-2.5f, 2.5f);
				m_aNukePuffs[i].m_StartColor = SCol.WithMultipliedAlpha(m_NukeAlpha);
				m_aNukePuffs[i].m_EndColor = ECol.WithMultipliedAlpha(m_NukeAlpha);
				m_aNukePuffs[i].m_Layer = Layer;
				m_aNukePuffs[i].m_StackLevel = 4;
				break;
			}
		}
	};

	for(int i = 0; i < 90; ++i)
	{
		const float ArmOffset = (i % 3) * (2.0f * pi / 3.0f);
		const float t = (float)i / 90.0f;
		const float Rad = (30.0f + t * 320.0f) * Scale;
		const float Ang = ArmOffset + t * 4.0f * pi + random_float(-0.25f, 0.25f);
		const float Spd = (3.2f + (1.0f - t) * 3.5f);
		const float Life = random_float(4.5f, 6.0f);
		const float SSize = random_float(28.0f, 55.0f) * Scale;
		const float ESize = random_float(60.0f, 130.0f) * Scale;

		ColorRGBA SpiralCol = (i % 3 == 0) ? ColorRGBA(1.0f, 0.65f, 0.30f, 0.95f) :
						     ((i % 3 == 1) ? ColorRGBA(0.95f, 0.40f, 0.55f, 0.95f) : ColorRGBA(0.35f, 0.90f, 1.0f, 0.95f));

		SpawnOrbitPuff(Pos, Rad, Ang, Spd, Life, SSize, ESize, SpiralCol, ColorRGBA(SpiralCol.r, SpiralCol.g, SpiralCol.b, 0.0f), 1);
	}

	for(int i = 0; i < 40; ++i)
	{
		const float Rad = random_float(25.0f, 360.0f) * Scale;
		const float Ang = random_float(0.0f, 2.0f * pi);
		const float Spd = random_float(2.0f, 6.0f);
		const float Life = random_float(4.0f, 6.0f);
		const float SSize = random_float(8.0f, 18.0f) * Scale;
		const float ESize = random_float(3.0f, 8.0f) * Scale;
		SpawnOrbitPuff(Pos, Rad, Ang, Spd, Life, SSize, ESize, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), ColorRGBA(0.4f, 0.85f, 1.0f, 0.0f), 4);
	}

	for(int i = 0; i < 55; ++i)
	{
		for(int k = 0; k < MAX_NUKE_PUFFS; ++k)
		{
			if(!m_aNukePuffs[k].m_Active)
			{
				const float Angle = -pi * 0.5f + random_float(-1.40f, 1.40f);
				const float Spd = random_float(550.0f, 1500.0f) * Scale;
				m_aNukePuffs[k].m_Active = true;
				m_aNukePuffs[k].m_OriginPos = Pos;
				m_aNukePuffs[k].m_Pos = Pos + vec2(random_float(-15.0f, 15.0f), random_float(-20.0f, 5.0f)) * Scale;
				m_aNukePuffs[k].m_Vel = vec2(std::cos(Angle), std::sin(Angle)) * Spd;
				m_aNukePuffs[k].m_Life = 0.0f;
				m_aNukePuffs[k].m_MaxLife = random_float(1.6f, 3.2f);
				m_aNukePuffs[k].m_StartSize = random_float(9.0f, 18.0f) * Scale;
				m_aNukePuffs[k].m_EndSize = random_float(3.0f, 7.0f) * Scale;
				m_aNukePuffs[k].m_Rot = random_float(0.0f, 2.0f * pi);
				m_aNukePuffs[k].m_RotSpeed = random_float(-3.0f, 3.0f);
				m_aNukePuffs[k].m_StartColor = (i % 2 == 0) ? ColorRGBA(0.35f, 0.95f, 1.0f, 1.0f) : ColorRGBA(1.0f, 0.85f, 0.40f, 1.0f);
				m_aNukePuffs[k].m_StartColor.a *= m_NukeAlpha;
				m_aNukePuffs[k].m_EndColor = ColorRGBA(1.0f, 0.30f, 0.05f, 0.0f);
				m_aNukePuffs[k].m_Layer = 3;
				m_aNukePuffs[k].m_StackLevel = 4;
				m_aNukePuffs[k].m_OrbitRadius = 0.0f;
				break;
			}
		}
	}
}

void CWeaponVfx::TriggerNukeExplosion(vec2 Pos, const ColorRGBA &Color, float BaseScale, int StackLevel)
{
	CancelLowerTierVfx(Pos, StackLevel);

	if(StackLevel == 1)
		TriggerBombExplosion(Pos, Color, BaseScale);
	else if(StackLevel == 2)
		TriggerNuclearMushroom(Pos, Color, BaseScale);
	else if(StackLevel == 3)
		TriggerBlackHoleSingularity(Pos, Color, BaseScale);
	else if(StackLevel >= 4)
		TriggerQuasarHyperBlast(Pos, Color, BaseScale);
}

void CWeaponVfx::CreateShockwave(vec2 Pos, float MaxRadius, float Duration, const ColorRGBA &Color)
{
	const int Mode = std::clamp(g_Config.m_BcRocketGlowMode, 0, 3);

	for(int i = 0; i < MAX_SHOCKWAVES; ++i)
	{
		if(!m_aShockwaves[i].m_Active)
		{
			m_aShockwaves[i].m_Active = true;
			m_aShockwaves[i].m_Pos = Pos;
			m_aShockwaves[i].m_Life = 0.0f;
			m_aShockwaves[i].m_MaxLife = Duration;
			m_aShockwaves[i].m_MaxRadius = MaxRadius;
			m_aShockwaves[i].m_Thickness = 6.5f;
			m_aShockwaves[i].m_Color = Color.WithMultipliedAlpha(m_NukeAlpha);
			m_aShockwaves[i].m_Mode = Mode;
			break;
		}
	}
}
