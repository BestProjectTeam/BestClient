#include "r_jelly.h"

#include <base/math.h>

#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>

std::unique_ptr<CRJelly> rJelly;

namespace
{
constexpr float MIN_DELTA_TIME = 1.0f / 240.0f;
constexpr float MAX_DELTA_TIME = 1.0f / 20.0f;

vec2 NormalizeOr(vec2 Value, vec2 Fallback)
{
	const float ValueLength = length(Value);
	if(ValueLength > 0.0001f)
		return Value / ValueLength;

	const float FallbackLength = length(Fallback);
	if(FallbackLength > 0.0001f)
		return Fallback / FallbackLength;

	return vec2(0.0f, -1.0f);
}

float DurationScale()
{
	const float Duration = g_Config.m_BcJellyTeeDuration / 100.0f;
	return std::pow(std::clamp(Duration, 0.2f, 5.0f), 1.65f);
}

void ClampDeform(JellyTee &Deform)
{
	Deform.m_BodyScale.x = std::clamp(Deform.m_BodyScale.x, 0.94f, 1.22f);
	Deform.m_BodyScale.y = std::clamp(Deform.m_BodyScale.y, 0.74f, 1.22f);
	Deform.m_FeetScale.x = std::clamp(Deform.m_FeetScale.x, 0.95f, 1.15f);
	Deform.m_FeetScale.y = std::clamp(Deform.m_FeetScale.y, 0.70f, 1.10f);
}
} // namespace

CRJelly::CRJelly(CGameClient *pClient) :
	m_pClient(pClient)
{
}

void CRJelly::Reset()
{
	m_State = CState();
}

bool CRJelly::IsEnabledFor(int ClientId) const
{
	return m_pClient != nullptr &&
		g_Config.m_BcJellyTee &&
		ClientId >= 0 &&
		ClientId == m_pClient->m_aLocalIds[g_Config.m_ClDummy];
}

JellyTee CRJelly::GetDeform(int ClientId, vec2 PrevVel, vec2 Vel, vec2 LookDir, bool InAir, bool WantOtherDir, float DeltaTime)
{
	JellyTee Deform;
	const bool Disabled = !g_Config.m_BcJellyTee || g_Config.m_BcJellyTeeStrength <= 0;

	if(Disabled && m_State.m_Initialized)
	{
		Reset();
		return Deform;
	}

	if(!IsEnabledFor(ClientId) || Disabled)
		return Deform;

	if(m_State.m_ClientId != ClientId)
		Reset();

	m_State.m_ClientId = ClientId;
	DeltaTime = std::clamp(DeltaTime, MIN_DELTA_TIME, MAX_DELTA_TIME);

	if(!m_State.m_Initialized)
	{
		m_State.m_PrevInputVel = Vel;
		m_State.m_Initialized = true;
	}

	const float Strength = g_Config.m_BcJellyTeeStrength / 100.0f;
	const float Duration = DurationScale();

	const vec2 PreviousVel = m_State.m_PrevInputVel;
	const vec2 DeltaVel = Vel - PreviousVel;
	const bool DirectionFlipX = (Vel.x > 0.0f && PreviousVel.x < 0.0f) || (Vel.x < 0.0f && PreviousVel.x > 0.0f);
	const float LandingImpact = (InAir ? 0.0f : std::clamp((PrevVel.y - Vel.y) / 13.0f, 0.0f, 1.8f)) * Strength;
	const float StopImpulse = std::clamp((length(PreviousVel) - length(Vel)) / 4.8f, 0.0f, 1.3f) * Strength;
	const float TurnImpulse = std::clamp(absolute(DeltaVel.x) / 5.8f, 0.0f, DirectionFlipX ? 1.2f : 0.8f) * Strength;
	const float SideImpulse = std::clamp(absolute(DeltaVel.x) / 5.2f, 0.0f, DirectionFlipX ? 1.15f : 0.75f) * Strength;

	vec2 MotionBasis = NormalizeOr(Vel, vec2(LookDir.x, -0.25f));
	vec2 TargetDeform(
		std::clamp(-DeltaVel.x / 6.0f, -0.9f, 0.9f) * Strength,
		std::clamp(-DeltaVel.y / 10.0f, -1.0f, 1.0f) * Strength);
	TargetDeform.x += std::clamp(-DeltaVel.x / 5.0f, -0.5f, 0.5f) * SideImpulse;
	TargetDeform += vec2(std::clamp(-Vel.x / 22.0f, -0.28f, 0.28f), 0.0f) * StopImpulse;
	if(DirectionFlipX || WantOtherDir)
		TargetDeform.x += std::clamp(-Vel.x / 17.0f, -0.28f, 0.28f) * Strength;

	const float DeformSpring = 6.0f / Duration;
	const float DeformDamping = 1.9f / maximum(Duration, 0.35f);
	m_State.m_DeformVelocity += (TargetDeform - m_State.m_Deform) * DeformSpring * DeltaTime;
	m_State.m_DeformVelocity *= 1.0f / (1.0f + DeformDamping * DeltaTime);
	m_State.m_Deform += m_State.m_DeformVelocity * DeltaTime;

	const float TargetCompression = LandingImpact * 1.25f + StopImpulse * 0.55f + TurnImpulse * 0.30f - std::clamp(-Vel.y / 24.0f, 0.0f, 0.25f);
	const float CompressionSpring = 4.2f / Duration;
	const float CompressionDamping = 1.7f / maximum(Duration, 0.35f);
	m_State.m_CompressionVelocity += (TargetCompression - m_State.m_Compression) * CompressionSpring * DeltaTime;
	m_State.m_CompressionVelocity *= 1.0f / (1.0f + CompressionDamping * DeltaTime);
	m_State.m_Compression += m_State.m_CompressionVelocity * DeltaTime;

	const vec2 DeformDirection = NormalizeOr(m_State.m_Deform, MotionBasis);
	const float DeformAmount = std::clamp(length(m_State.m_Deform), 0.0f, 1.10f);
	const float HorizontalStretch = absolute(DeformDirection.x) * DeformAmount;
	const float VerticalStretch = absolute(DeformDirection.y) * DeformAmount;
	const float LandingSquash = std::clamp(m_State.m_Compression, 0.0f, 1.2f);
	const float AirStretch = std::clamp(-m_State.m_Compression, 0.0f, 0.5f);

	Deform.m_BodyScale.x += HorizontalStretch * 0.14f + LandingSquash * 0.24f + VerticalStretch * 0.03f;
	Deform.m_BodyScale.y += VerticalStretch * 0.15f + AirStretch * 0.22f - HorizontalStretch * 0.05f - LandingSquash * 0.34f;

	Deform.m_FeetScale.x += LandingSquash * 0.15f + HorizontalStretch * 0.03f;
	Deform.m_FeetScale.y += AirStretch * 0.05f - LandingSquash * 0.24f - HorizontalStretch * 0.01f;

	Deform.m_BodyAngle = std::clamp(-m_State.m_Deform.x * 0.12f - m_State.m_DeformVelocity.x * 0.008f + DeformDirection.x * VerticalStretch * 0.02f, -0.12f, 0.12f);
	Deform.m_FeetAngle = std::clamp(-Deform.m_BodyAngle * 0.30f + m_State.m_Deform.x * 0.025f, -0.06f, 0.06f);
	ClampDeform(Deform);

	m_State.m_PrevInputVel = Vel;
	return Deform;
}
