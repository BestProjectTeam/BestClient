/* Copyright © 2026 BestProject Team */
#include "camera_demos.h"

#include <base/math.h>

#include <engine/input.h>
#include <engine/keys.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <algorithm>

vec2 CCameraDemos::s_DriftTargetOffset = vec2(0.0f, 0.0f);
vec2 CCameraDemos::s_DriftCurrentOffset = vec2(0.0f, 0.0f);
float CCameraDemos::s_DynamicFovTarget = 1.0f;
float CCameraDemos::s_DynamicFovCurrent = 1.0f;
float CCameraDemos::s_DynamicFovAppliedFactor = 1.0f;
vec2 CCameraDemos::s_FreeviewVelocity = vec2(0.0f, 0.0f);

void CCameraDemos::Reset()
{
	s_DriftTargetOffset = vec2(0.0f, 0.0f);
	s_DriftCurrentOffset = vec2(0.0f, 0.0f);
	s_DynamicFovTarget = 1.0f;
	s_DynamicFovCurrent = 1.0f;
	s_DynamicFovAppliedFactor = 1.0f;
	s_FreeviewVelocity = vec2(0.0f, 0.0f);
}

void CCameraDemos::RemoveDynamicFov(float &Zoom)
{
	if(s_DynamicFovAppliedFactor == 1.0f)
		return;

	Zoom /= s_DynamicFovAppliedFactor;
	s_DynamicFovAppliedFactor = 1.0f;
}

void CCameraDemos::UpdateEffects(CGameClient *pGameClient, float DeltaTime, float &Zoom, float MinZoom, float MaxZoom)
{
	const CNetObj_Character *pTrackedCharacter = nullptr;
	const CNetObj_Character *pPrevTrackedCharacter = nullptr;

	if(pGameClient->Client()->State() == IClient::STATE_DEMOPLAYBACK && !pGameClient->m_MultiViewActivated)
	{
		int TrackedClientId = -1;
		if(pGameClient->m_Snap.m_SpecInfo.m_Active)
		{
			const int SpectatorId = pGameClient->m_Snap.m_SpecInfo.m_SpectatorId;
			if(in_range(SpectatorId, 0, MAX_CLIENTS - 1) && pGameClient->m_Snap.m_aCharacters[SpectatorId].m_Active)
				TrackedClientId = SpectatorId;
		}
		else if(in_range(pGameClient->m_Snap.m_LocalClientId, 0, MAX_CLIENTS - 1) && pGameClient->m_Snap.m_aCharacters[pGameClient->m_Snap.m_LocalClientId].m_Active)
		{
			TrackedClientId = pGameClient->m_Snap.m_LocalClientId;
		}

		if(TrackedClientId >= 0)
		{
			pTrackedCharacter = &pGameClient->m_Snap.m_aCharacters[TrackedClientId].m_Cur;
			pPrevTrackedCharacter = &pGameClient->m_Snap.m_aCharacters[TrackedClientId].m_Prev;
		}
	}

	if(pTrackedCharacter == nullptr)
	{
		s_DriftTargetOffset = vec2(0.0f, 0.0f);
		s_DriftCurrentOffset = vec2(0.0f, 0.0f);
		s_DynamicFovTarget = 1.0f;
		s_DynamicFovCurrent = 1.0f;
		return;
	}

	const float IntraTick = pGameClient->Client()->IntraGameTick(g_Config.m_ClDummy);

	if(g_Config.m_BcCameraDrift)
	{
		const float CurrentVelocity = pTrackedCharacter->m_VelX / 256.0f;
		const float PreviousVelocity = pPrevTrackedCharacter->m_VelX / 256.0f;
		const float HorizontalVelocity = mix(PreviousVelocity, CurrentVelocity, IntraTick);
		const float VelocityFactor = absolute(HorizontalVelocity);
		float DriftDirection = HorizontalVelocity < 0.0f ? -1.0f : HorizontalVelocity > 0.0f ? 1.0f : 0.0f;
		if(g_Config.m_BcCameraDriftReverse)
			DriftDirection *= -1.0f;

		const float DriftMultiplier = 1.0f + VelocityFactor / 10.0f;
		const float DriftAmount = VelocityFactor * (g_Config.m_BcCameraDriftAmount / 50.0f) * DriftMultiplier;
		s_DriftTargetOffset = vec2(DriftDirection * DriftAmount, 0.0f);

		const float SmoothFactor = (1.0f - g_Config.m_BcCameraDriftSmoothness / 100.0f) * 10.0f;
		s_DriftCurrentOffset += (s_DriftTargetOffset - s_DriftCurrentOffset) * std::min(DeltaTime * SmoothFactor, 1.0f);
	}
	else
	{
		s_DriftTargetOffset = vec2(0.0f, 0.0f);
		s_DriftCurrentOffset = vec2(0.0f, 0.0f);
	}

	s_DynamicFovTarget = 1.0f;
	if(g_Config.m_BcDynamicFov)
	{
		const vec2 CurrentVelocity = vec2(pTrackedCharacter->m_VelX, pTrackedCharacter->m_VelY) / 256.0f;
		const vec2 PreviousVelocity = vec2(pPrevTrackedCharacter->m_VelX, pPrevTrackedCharacter->m_VelY) / 256.0f;
		const float VelocityFactor = length(mix(PreviousVelocity, CurrentVelocity, IntraTick));
		const float DynamicFovMultiplier = 1.0f + VelocityFactor / 10.0f;
		const float DynamicFovAmount = VelocityFactor * (g_Config.m_BcDynamicFovAmount / 50.0f) * DynamicFovMultiplier;
		s_DynamicFovTarget = std::clamp(1.0f + DynamicFovAmount / 500.0f, 1.0f, 5.0f);
	}

	if(g_Config.m_BcDynamicFov && g_Config.m_BcDynamicFovSmoothness > 0)
	{
		const float SmoothFactor = (1.0f - g_Config.m_BcDynamicFovSmoothness / 100.0f) * 15.0f + 0.5f;
		s_DynamicFovCurrent += (s_DynamicFovTarget - s_DynamicFovCurrent) * std::min(DeltaTime * SmoothFactor, 1.0f);
	}
	else
	{
		s_DynamicFovCurrent = s_DynamicFovTarget;
	}

	s_DynamicFovCurrent = std::max(1.0f, s_DynamicFovCurrent);
	const float BaseZoom = Zoom;
	Zoom = std::clamp(BaseZoom * s_DynamicFovCurrent, MinZoom, MaxZoom);
	s_DynamicFovAppliedFactor = Zoom / BaseZoom;
}

vec2 CCameraDemos::DriftOffset(bool DemoPlayback)
{
	return DemoPlayback ? s_DriftCurrentOffset : vec2(0.0f, 0.0f);
}

void CCameraDemos::ApplyFreeviewNumpad(IInput *pInput, float FrameTime, bool &ForceFreeview, vec2 &ForceFreeviewPos, vec2 &Center)
{
	if(!g_Config.m_BcFreeviewNumpad)
	{
		s_FreeviewVelocity = vec2(0.0f, 0.0f);
		return;
	}

	const float SmoothFreeviewSpeed = (float)g_Config.m_BcFreeviewSpeed;
	const float SmoothFreeviewAcceleration = g_Config.m_BcFreeviewSmoothness / 10.0f;
	vec2 FreeviewMove = vec2(0.0f, 0.0f);
	if(pInput->KeyIsPressed(KEY_KP_4))
		FreeviewMove.x -= 1.0f;
	if(pInput->KeyIsPressed(KEY_KP_6))
		FreeviewMove.x += 1.0f;
	if(pInput->KeyIsPressed(KEY_KP_8))
		FreeviewMove.y -= 1.0f;
	if(pInput->KeyIsPressed(KEY_KP_2))
		FreeviewMove.y += 1.0f;

	const vec2 TargetVelocity = FreeviewMove * SmoothFreeviewSpeed;
	s_FreeviewVelocity += (TargetVelocity - s_FreeviewVelocity) * std::min(FrameTime * SmoothFreeviewAcceleration, 1.0f);
	if(absolute(s_FreeviewVelocity.x) < 0.1f && absolute(s_FreeviewVelocity.y) < 0.1f)
		s_FreeviewVelocity = vec2(0.0f, 0.0f);

	if(s_FreeviewVelocity.x != 0.0f || s_FreeviewVelocity.y != 0.0f)
	{
		ForceFreeviewPos = Center + s_FreeviewVelocity * FrameTime;
		ForceFreeview = true;
	}
}
