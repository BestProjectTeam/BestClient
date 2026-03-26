#include "orbit_aura.h"

#include <base/color.h>
#include <base/math.h>
#include <base/vmath.h>

#include <engine/shared/config.h>

#include <generated/client_data.h>

#include <game/client/components/particles.h>
#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>

void COrbitAura::ResetState()
{
	m_SpawnAccumulator = 0.0f;
	m_LastMovementTime = 0.0f;
	m_IdleVisibility = 0.0f;
	m_LastPosition = vec2(0.0f, 0.0f);
	m_HasLastPosition = false;
}

float COrbitAura::GetIdleVisibility(const vec2 &BasePos, float Delta, float CurrentTime)
{
	if(!g_Config.m_BcOrbitAuraIdle)
	{
		m_LastMovementTime = CurrentTime;
		m_LastPosition = BasePos;
		m_HasLastPosition = true;
		m_IdleVisibility = 1.0f;
		return m_IdleVisibility;
	}

	const bool IsMoving = !m_HasLastPosition || length(BasePos - m_LastPosition) > 1.0f;
	m_LastPosition = BasePos;
	m_HasLastPosition = true;

	if(IsMoving)
		m_LastMovementTime = CurrentTime;

	const bool ShouldShow = !IsMoving && CurrentTime - m_LastMovementTime >= g_Config.m_BcOrbitAuraIdleTimer;
	const float FadeStep = ShouldShow ? Delta / 0.45f : Delta / 0.2f;
	m_IdleVisibility = std::clamp(m_IdleVisibility + (ShouldShow ? FadeStep : -FadeStep), 0.0f, 1.0f);
	return m_IdleVisibility;
}

bool COrbitAura::CanRenderAura(int &ClientId, vec2 &BasePos, ColorRGBA &BaseColor, float &Alpha)
{
	if(!g_Config.m_BcOrbitAura)
		return false;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return false;

	ClientId = GameClient()->m_Snap.m_LocalClientId;
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;

	const auto &Character = GameClient()->m_Snap.m_aCharacters[ClientId];
	if(!Character.m_Active)
		return false;

	const auto &ClientData = GameClient()->m_aClients[ClientId];
	if(!ClientData.m_Active || ClientData.m_Team == TEAM_SPECTATORS || GameClient()->m_Snap.m_SpecInfo.m_Active)
		return false;

	const float BaseAlpha = std::clamp(g_Config.m_BcOrbitAuraAlpha / 100.0f, 0.0f, 1.0f);
	const float Delta = std::clamp(Client()->RenderFrameTime(), 0.0f, 0.1f);
	const float CurrentTime = Client()->LocalTime();
	BasePos = ClientData.m_RenderPos;
	if(!GameClient()->OptimizerAllowRenderPos(BasePos))
		return false;
	Alpha = BaseAlpha * GetIdleVisibility(BasePos, Delta, CurrentTime);
	if(Alpha <= 0.001f)
		return false;

	const auto &RenderInfo = ClientData.m_RenderInfo;
	BaseColor = RenderInfo.m_CustomColoredSkin ? RenderInfo.m_ColorBody : RenderInfo.m_BloodColor;
	return true;
}

void COrbitAura::OnReset()
{
	ResetState();
}

static ColorRGBA ShiftedAuraColor(const ColorRGBA &Base, float Time, float Phase)
{
	ColorHSLA Hsl = color_cast<ColorHSLA>(Base);
	Hsl.h = std::fmod(Hsl.h + 1.0f + 0.04f * std::sin(Time * 0.7f + Phase), 1.0f);
	Hsl.s = std::clamp(Hsl.s * 0.9f + 0.15f * std::sin(Time * 0.4f + Phase), 0.0f, 1.0f);
	Hsl.l = std::clamp(Hsl.l * 1.05f, 0.0f, 1.0f);
	return color_cast<ColorRGBA>(Hsl);
}

void COrbitAura::OnRender()
{
	int ClientId = -1;
	vec2 BasePos(0.0f, 0.0f);
	ColorRGBA BaseColor(1.0f, 1.0f, 1.0f, 1.0f);
	float Alpha = 0.0f;
	if(!CanRenderAura(ClientId, BasePos, BaseColor, Alpha))
	{
		if(!g_Config.m_BcOrbitAura || Client()->State() == IClient::STATE_OFFLINE || GameClient()->m_Snap.m_LocalClientId < 0)
			ResetState();
		else
			m_SpawnAccumulator = 0.0f;
		return;
	}
	(void)ClientId;

	const float Delta = std::clamp(Client()->RenderFrameTime(), 0.0f, 0.1f);
	if(Delta <= 0.0f)
		return;

	m_SpawnAccumulator += Delta;

	const float SpawnInterval = 1.0f / 30.0f;
	const float Time = Client()->LocalTime();
	const int SparkleCount = std::clamp(g_Config.m_BcOrbitAuraParticles, 2, 120);
	const float Radius = std::clamp((float)g_Config.m_BcOrbitAuraRadius, 8.0f, 200.0f);
	const float Speed = std::clamp(g_Config.m_BcOrbitAuraSpeed / 100.0f, 0.1f, 2.0f);

	int Steps = 0;
	while(m_SpawnAccumulator >= SpawnInterval && Steps < 3)
	{
		m_SpawnAccumulator -= SpawnInterval;
		Steps++;

		for(int i = 0; i < SparkleCount; i++)
		{
			const float T = (float)i / (float)SparkleCount;
			const float BaseAngle = T * 2.0f * pi;
			const float Angle = BaseAngle + Time * (0.9f * Speed) + std::sin(Time * 1.1f + i) * 0.15f;
			const float RadiusPulse = Radius * (0.88f + 0.12f * std::sin(Time * 2.0f + i * 0.6f));
			const float VerticalWave = std::sin(Time * 1.6f + i * 0.4f) * 6.0f;
			const vec2 Offset = vec2(std::cos(Angle) * RadiusPulse, std::sin(Angle) * RadiusPulse + VerticalWave);

			float SparkleAlpha = (0.35f + 0.35f * std::sin(Time * 3.0f + i * 0.7f)) * Alpha;
			SparkleAlpha = std::clamp(SparkleAlpha, 0.0f, 1.0f);

			const ColorRGBA AuraColor = ShiftedAuraColor(BaseColor, Time, T * 2.0f * pi);

			CParticle p;
			p.SetDefault();
			p.m_Spr = SPRITE_PART_SPARKLE;
			p.m_Pos = BasePos + Offset;
			p.m_Vel = vec2(0.0f, 0.0f);
			p.m_LifeSpan = 0.35f;
			p.m_StartSize = 6.0f + 2.0f * std::sin(Time * 4.0f + i);
			p.m_EndSize = p.m_StartSize * 0.5f;
			p.m_UseAlphaFading = true;
			p.m_StartAlpha = SparkleAlpha;
			p.m_EndAlpha = 0.0f;
			p.m_Rot = Angle;
			p.m_Rotspeed = 0.0f;
			p.m_Gravity = 0.0f;
			p.m_Friction = 1.0f;
			p.m_FlowAffected = 0.0f;
			p.m_Collides = false;
			p.m_Color = AuraColor;
			GameClient()->m_Particles.Add(CParticles::GROUP_EXTRA, &p);
		}

		CParticle Glow;
		Glow.SetDefault();
		Glow.m_Spr = SPRITE_PART_BALL;
		Glow.m_Pos = BasePos + vec2(0.0f, -2.0f);
		Glow.m_Vel = vec2(0.0f, 0.0f);
		Glow.m_LifeSpan = 0.35f;
		Glow.m_StartSize = Radius * 0.9f;
		Glow.m_EndSize = Radius * 0.2f;
		Glow.m_UseAlphaFading = true;
		Glow.m_StartAlpha = 0.18f * Alpha;
		Glow.m_EndAlpha = 0.0f;
		Glow.m_Gravity = 0.0f;
		Glow.m_Friction = 1.0f;
		Glow.m_FlowAffected = 0.0f;
		Glow.m_Collides = false;
		Glow.m_Color = ShiftedAuraColor(BaseColor, Time, 0.0f).Multiply(0.75f);
		GameClient()->m_Particles.Add(CParticles::GROUP_GENERAL, &Glow);
	}
}
