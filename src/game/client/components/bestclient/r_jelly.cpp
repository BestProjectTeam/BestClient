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
	for(auto &State : m_aStates)
		State = CState();
}

bool CRJelly::IsEnabledFor(int ClientId) const
{
	if(m_pClient == nullptr || !g_Config.m_BcJellyTee || ClientId < 0 || ClientId >= MAX_JELLY_CLIENTS)
		return false;

	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		if(ClientId == m_pClient->m_aLocalIds[Dummy])
			return true;
	}

	return g_Config.m_BcJellyTeeOthers != 0;
}

CRJelly::CState *CRJelly::StateFor(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_JELLY_CLIENTS)
		return nullptr;
	return &m_aStates[ClientId];
}

JellyTee CRJelly::GetDeform(int ClientId, vec2 PrevVel, vec2 Vel, vec2 LookDir, bool InAir, bool WantOtherDir, float DeltaTime, vec2 ExtraDeformImpulse, float ExtraCompression)
{
	JellyTee Deform;
	const bool Disabled = !g_Config.m_BcJellyTee || g_Config.m_BcJellyTeeStrength <= 0;
	CState *pState = StateFor(ClientId);

	if(Disabled)
	{
		Reset();
		return Deform;
	}

	if(pState == nullptr)
		return Deform;

	if(!IsEnabledFor(ClientId))
		return Deform;

	pState->m_ClientId = ClientId;
	DeltaTime = std::clamp(DeltaTime, MIN_DELTA_TIME, MAX_DELTA_TIME);

	if(!pState->m_Initialized)
	{
		pState->m_PrevInputVel = Vel;
		pState->m_Initialized = true;
	}

	const float Strength = g_Config.m_BcJellyTeeStrength / 100.0f;
	const float Duration = DurationScale();

	const vec2 PreviousVel = pState->m_PrevInputVel;
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
	TargetDeform += ExtraDeformImpulse * Strength;

	const float DeformSpring = 6.0f / Duration;
	const float DeformDamping = 1.9f / maximum(Duration, 0.35f);
	pState->m_DeformVelocity += (TargetDeform - pState->m_Deform) * DeformSpring * DeltaTime;
	pState->m_DeformVelocity *= 1.0f / (1.0f + DeformDamping * DeltaTime);
	pState->m_Deform += pState->m_DeformVelocity * DeltaTime;

	const float TargetCompression = LandingImpact * 1.25f + StopImpulse * 0.55f + TurnImpulse * 0.30f - std::clamp(-Vel.y / 24.0f, 0.0f, 0.25f) + std::clamp(ExtraCompression, -0.8f, 2.2f) * Strength;
	const float CompressionSpring = 4.2f / Duration;
	const float CompressionDamping = 1.7f / maximum(Duration, 0.35f);
	pState->m_CompressionVelocity += (TargetCompression - pState->m_Compression) * CompressionSpring * DeltaTime;
	pState->m_CompressionVelocity *= 1.0f / (1.0f + CompressionDamping * DeltaTime);
	pState->m_Compression += pState->m_CompressionVelocity * DeltaTime;

	const vec2 DeformDirection = NormalizeOr(pState->m_Deform, MotionBasis);
	const float DeformAmount = std::clamp(length(pState->m_Deform), 0.0f, 1.10f);
	const float HorizontalStretch = absolute(DeformDirection.x) * DeformAmount;
	const float VerticalStretch = absolute(DeformDirection.y) * DeformAmount;
	const float LandingSquash = std::clamp(pState->m_Compression, 0.0f, 1.2f);
	const float AirStretch = std::clamp(-pState->m_Compression, 0.0f, 0.5f);

	Deform.m_BodyScale.x += HorizontalStretch * 0.14f + LandingSquash * 0.24f + VerticalStretch * 0.03f;
	Deform.m_BodyScale.y += VerticalStretch * 0.15f + AirStretch * 0.22f - HorizontalStretch * 0.05f - LandingSquash * 0.34f;

	Deform.m_FeetScale.x += LandingSquash * 0.15f + HorizontalStretch * 0.03f;
	Deform.m_FeetScale.y += AirStretch * 0.05f - LandingSquash * 0.24f - HorizontalStretch * 0.01f;

	Deform.m_BodyAngle = std::clamp(-pState->m_Deform.x * 0.12f - pState->m_DeformVelocity.x * 0.008f + DeformDirection.x * VerticalStretch * 0.02f, -0.12f, 0.12f);
	Deform.m_FeetAngle = std::clamp(-Deform.m_BodyAngle * 0.30f + pState->m_Deform.x * 0.025f, -0.06f, 0.06f);
	ClampDeform(Deform);

	pState->m_PrevInputVel = Vel;
	return Deform;
}
