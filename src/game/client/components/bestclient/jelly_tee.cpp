/* Copyright © 2026 BestProject Team */
#include "jelly_tee.h"

#include <base/math.h>

#include <engine/shared/config.h>
#include <engine/shared/video.h>

#include <generated/protocol.h>

#include <game/client/gameclient.h>
#include <game/collision.h>

#include <algorithm>
#include <cmath>

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

	void ClampDeform(SJellyTeeDeform &Deform)
	{
		Deform.m_BodyScale.x = std::clamp(Deform.m_BodyScale.x, 0.94f, 1.22f);
		Deform.m_BodyScale.y = std::clamp(Deform.m_BodyScale.y, 0.74f, 1.22f);
		Deform.m_FeetScale.x = std::clamp(Deform.m_FeetScale.x, 0.95f, 1.15f);
		Deform.m_FeetScale.y = std::clamp(Deform.m_FeetScale.y, 0.70f, 1.10f);
	}

	bool IsSolidAt(const CCollision *pCollision, vec2 Pos)
	{
		if(pCollision == nullptr)
			return false;
		return pCollision->CheckPoint(Pos.x, Pos.y);
	}
} // namespace

void CJellyTee::OnReset()
{
	for(auto &State : m_aStates)
		State = CState();
	m_FrameId = 0;
}

void CJellyTee::OnRender()
{
	++m_FrameId;
}

bool CJellyTee::IsEnabledFor(int ClientId) const
{
	if(!g_Config.m_BcJellyTee || ClientId < 0 || ClientId >= MAX_JELLY_CLIENTS)
		return false;

	for(const int LocalId : GameClient()->m_aLocalIds)
	{
		if(ClientId == LocalId)
			return true;
	}
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
	{
		if(!GameClient()->m_Snap.m_SpecInfo.m_Active && GameClient()->m_Snap.m_LocalClientId == ClientId)
			return true;
		if(GameClient()->m_Snap.m_SpecInfo.m_Active && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == ClientId)
			return true;
	}

	return g_Config.m_BcJellyTeeOthers != 0;
}

CJellyTee::CState *CJellyTee::StateFor(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_JELLY_CLIENTS)
		return nullptr;
	return &m_aStates[ClientId];
}

void CJellyTee::ApplySurfaceImpacts(vec2 Position, vec2 PrevVel, vec2 Vel, vec2 &OutExtraDeformImpulse, float &OutExtraCompression, float &OutWallSquash) const
{
	const CCollision *pCollision = Collision();

	const float PrevSpeedX = absolute(PrevVel.x);
	const float SpeedDropX = PrevSpeedX - absolute(Vel.x);
	if(PrevSpeedX >= 4.0f && SpeedDropX >= 1.2f)
	{
		const float Side = PrevVel.x >= 0.0f ? 1.0f : -1.0f;
		const float ProbeX = Position.x + Side * 16.0f;
		const bool TouchingWall =
			IsSolidAt(pCollision, vec2(ProbeX, Position.y - 10.0f)) ||
			IsSolidAt(pCollision, vec2(ProbeX, Position.y)) ||
			IsSolidAt(pCollision, vec2(ProbeX, Position.y + 10.0f));
		if(TouchingWall)
		{
			const float WallImpact = std::clamp(SpeedDropX / 7.0f, 0.0f, 1.8f);
			OutExtraDeformImpulse.x += -Side * WallImpact * 0.55f;
			OutWallSquash += WallImpact * 1.35f;
		}
	}

	if(PrevVel.y >= 4.0f)
	{
		const float SpeedDropY = PrevVel.y - Vel.y;
		if(SpeedDropY >= 1.2f)
		{
			const float ProbeY = Position.y + 18.0f;
			const bool TouchingFloor =
				IsSolidAt(pCollision, vec2(Position.x - 10.0f, ProbeY)) ||
				IsSolidAt(pCollision, vec2(Position.x, ProbeY)) ||
				IsSolidAt(pCollision, vec2(Position.x + 10.0f, ProbeY));
			if(TouchingFloor)
			{
				const float FloorImpact = std::clamp(SpeedDropY / 7.0f, 0.0f, 1.8f);
				OutExtraDeformImpulse.y += FloorImpact * 0.55f;
				OutExtraCompression += FloorImpact * 1.35f;
			}
		}
	}

	if(PrevVel.y <= -4.0f)
	{
		const float SpeedDropY = (-PrevVel.y) - absolute(std::min(Vel.y, 0.0f));
		if(SpeedDropY >= 1.2f || Vel.y > PrevVel.y + 1.2f)
		{
			const float ProbeY = Position.y - 18.0f;
			const bool TouchingCeiling =
				IsSolidAt(pCollision, vec2(Position.x - 10.0f, ProbeY)) ||
				IsSolidAt(pCollision, vec2(Position.x, ProbeY)) ||
				IsSolidAt(pCollision, vec2(Position.x + 10.0f, ProbeY));
			if(TouchingCeiling)
			{
				const float CeilingImpact = std::clamp(std::max(SpeedDropY, Vel.y - PrevVel.y) / 7.0f, 0.0f, 1.8f);
				OutExtraDeformImpulse.y -= CeilingImpact * 0.85f;
				OutExtraCompression += CeilingImpact * 0.90f;
			}
		}
	}
}

bool CJellyTee::HasLocalHammerImpact(CState *pState, int ClientId)
{
	bool IsLocal = false;
	for(const int LocalId : GameClient()->m_aLocalIds)
	{
		if(LocalId == ClientId)
		{
			IsLocal = true;
			break;
		}
	}
	if(!IsLocal)
		return false;

	int LatestTick = -1;
	for(const auto &Event : GameClient()->m_PredictedWorld.m_PredictedEvents)
	{
		if(Event.m_EventId != NETEVENTTYPE_HAMMERHIT || Event.m_Id != ClientId)
			continue;
		LatestTick = std::max(LatestTick, Event.m_Tick);
	}
	if(LatestTick < 0 || LatestTick <= pState->m_LastHammerImpactTick)
		return false;

	pState->m_LastHammerImpactTick = LatestTick;
	return true;
}

void CJellyTee::BuildExtraImpulse(int ClientId, vec2 Position, vec2 PrevVel, vec2 Vel, vec2 LookDir, vec2 &OutExtraDeformImpulse, float &OutExtraCompression, float &OutWallSquash)
{
	OutExtraDeformImpulse = vec2(0.0f, 0.0f);
	OutExtraCompression = 0.0f;
	OutWallSquash = 0.0f;

	CState *pState = StateFor(ClientId);
	if(pState == nullptr)
		return;

	ApplySurfaceImpacts(Position, PrevVel, Vel, OutExtraDeformImpulse, OutExtraCompression, OutWallSquash);

	if(HasLocalHammerImpact(pState, ClientId))
	{
		const float HitImpact = 1.05f;
		const float HorizontalKick = absolute(Vel.x - PrevVel.x) > 0.05f ? std::clamp(Vel.x - PrevVel.x, -1.0f, 1.0f) : -LookDir.x;
		OutExtraDeformImpulse.x += HorizontalKick * 0.80f * HitImpact;
		OutExtraCompression += HitImpact;
	}
}

SJellyTeeDeform CJellyTee::GetDeform(int ClientId, vec2 PrevVel, vec2 Vel, vec2 LookDir, bool InAir, bool WantOtherDir, vec2 Position)
{
	SJellyTeeDeform Deform;
	CState *pState = StateFor(ClientId);
	if(pState == nullptr)
		return Deform;

	const bool Disabled = !g_Config.m_BcJellyTee || g_Config.m_BcJellyTeeStrength <= 0;
	if(Disabled || !IsEnabledFor(ClientId))
	{
		if(pState->m_Initialized)
			*pState = CState();
		return Deform;
	}

	if(pState->m_LastUpdateFrame == m_FrameId)
		return pState->m_CachedDeform;

	pState->m_LastUpdateFrame = m_FrameId;

	float DeltaTime = Client()->RenderFrameTime();
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK
#if defined(CONF_VIDEORECORDER)
		&& IVideo::Current()
#endif
	)
	{
		const float DemoPlaybackTime = (Client()->PrevGameTick(0) + Client()->IntraGameTickSincePrev(0)) / (float)Client()->GameTickSpeed();
		if(pState->m_HasLastDemoPlaybackTime)
			DeltaTime = DemoPlaybackTime - pState->m_LastDemoPlaybackTime;
		pState->m_LastDemoPlaybackTime = DemoPlaybackTime;
		pState->m_HasLastDemoPlaybackTime = true;
	}
	else
	{
		pState->m_HasLastDemoPlaybackTime = false;
	}
	DeltaTime = std::clamp(DeltaTime, MIN_DELTA_TIME, MAX_DELTA_TIME);

	if(!pState->m_Initialized)
	{
		pState->m_PrevInputVel = Vel;
		pState->m_Initialized = true;
	}

	const float Strength = g_Config.m_BcJellyTeeStrength / 100.0f;
	const float Duration = DurationScale();

	vec2 ExtraDeformImpulse = vec2(0.0f, 0.0f);
	float ExtraCompression = 0.0f;
	float ExtraWallSquash = 0.0f;
	BuildExtraImpulse(ClientId, Position, PrevVel, Vel, LookDir, ExtraDeformImpulse, ExtraCompression, ExtraWallSquash);

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
	const float DeformDamping = 1.9f / std::max(Duration, 0.35f);
	pState->m_DeformVelocity += (TargetDeform - pState->m_Deform) * DeformSpring * DeltaTime;
	pState->m_DeformVelocity *= 1.0f / (1.0f + DeformDamping * DeltaTime);
	pState->m_Deform += pState->m_DeformVelocity * DeltaTime;

	const float TargetCompression = LandingImpact * 1.25f + StopImpulse * 0.55f + TurnImpulse * 0.30f - std::clamp(-Vel.y / 24.0f, 0.0f, 0.25f) + std::clamp(ExtraCompression, -0.8f, 2.2f) * Strength;
	const float CompressionSpring = 4.2f / Duration;
	const float CompressionDamping = 1.7f / std::max(Duration, 0.35f);
	pState->m_CompressionVelocity += (TargetCompression - pState->m_Compression) * CompressionSpring * DeltaTime;
	pState->m_CompressionVelocity *= 1.0f / (1.0f + CompressionDamping * DeltaTime);
	pState->m_Compression += pState->m_CompressionVelocity * DeltaTime;

	const float TargetWallCompression = std::clamp(ExtraWallSquash, 0.0f, 2.2f) * Strength;
	pState->m_WallCompressionVelocity += (TargetWallCompression - pState->m_WallCompression) * CompressionSpring * DeltaTime;
	pState->m_WallCompressionVelocity *= 1.0f / (1.0f + CompressionDamping * DeltaTime);
	pState->m_WallCompression += pState->m_WallCompressionVelocity * DeltaTime;

	const vec2 DeformDirection = NormalizeOr(pState->m_Deform, MotionBasis);
	const float DeformAmount = std::clamp(length(pState->m_Deform), 0.0f, 1.10f);
	const float HorizontalStretch = absolute(DeformDirection.x) * DeformAmount;
	const float VerticalStretch = absolute(DeformDirection.y) * DeformAmount;
	const float LandingSquash = std::clamp(pState->m_Compression, 0.0f, 1.2f);
	const float WallSquash = std::clamp(pState->m_WallCompression, 0.0f, 1.2f);
	const float AirStretch = std::clamp(-pState->m_Compression, 0.0f, 0.5f);

	Deform.m_BodyScale.x += HorizontalStretch * 0.14f + LandingSquash * 0.24f + VerticalStretch * 0.03f - WallSquash * 0.34f;
	Deform.m_BodyScale.y += VerticalStretch * 0.15f + AirStretch * 0.22f - HorizontalStretch * 0.05f - LandingSquash * 0.34f + WallSquash * 0.24f;

	Deform.m_FeetScale.x += LandingSquash * 0.15f + HorizontalStretch * 0.03f - WallSquash * 0.24f;
	Deform.m_FeetScale.y += AirStretch * 0.05f - LandingSquash * 0.24f - HorizontalStretch * 0.01f + WallSquash * 0.15f;

	const float MoveLean = std::clamp(Vel.x / 11.0f, -1.0f, 1.0f) * 0.055f * (InAir ? 0.70f : 1.0f);
	Deform.m_BodyAngle = std::clamp(-pState->m_Deform.x * 0.12f - pState->m_DeformVelocity.x * 0.008f + DeformDirection.x * VerticalStretch * 0.02f + MoveLean, -0.17f, 0.17f);
	Deform.m_FeetAngle = std::clamp(-Deform.m_BodyAngle * 0.30f + pState->m_Deform.x * 0.025f + MoveLean * 0.25f, -0.08f, 0.08f);
	ClampDeform(Deform);

	pState->m_PrevInputVel = Vel;
	pState->m_CachedDeform = Deform;
	return Deform;
}
