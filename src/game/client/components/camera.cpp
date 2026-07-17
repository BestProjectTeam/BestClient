/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "camera.h"

#include "controls.h"

#include <base/log.h>
#include <base/math.h>
#include <base/vmath.h>

#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/collision.h>
#include <game/localization.h>
#include <game/mapitems.h>

#include <limits>

CCamera::CCamera()
{
	m_CamType = CAMTYPE_UNDEFINED;
	m_ZoomSet = false;
	m_Zoom = 1.0f;
	m_Zooming = false;
	m_ForceFreeview = false;
	m_GotoSwitchOffset = 0;
	m_GotoTeleOffset = 0;
	m_GotoSwitchLastPos = ivec2(-1, -1);
	m_GotoTeleLastPos = ivec2(-1, -1);

	std::fill(std::begin(m_aLastPos), std::end(m_aLastPos), vec2(0.0f, 0.0f));
	m_PrevCenter = vec2(0, 0);
	m_Center = vec2(0, 0);

	m_PrevSpecId = -1;
	m_WasSpectating = false;

	m_CameraSmoothing = false;

	m_LastTargetPos = vec2(0, 0);
	m_DyncamTargetCameraOffset = vec2(0, 0);
	std::fill(std::begin(m_aDyncamCurrentCameraOffset), std::end(m_aDyncamCurrentCameraOffset), vec2(0.0f, 0.0f));
	m_DyncamSmoothingSpeedBias = 0.5f;
	m_DemoCameraDriftTargetOffset = vec2(0.0f, 0.0f);
	m_DemoCameraDriftCurrentOffset = vec2(0.0f, 0.0f);
	m_DemoDynamicFovTarget = 1.0f;
	m_DemoDynamicFovCurrent = 1.0f;
	m_DemoDynamicFovAppliedFactor = 1.0f;
	m_CinematicCameraSmoothing = false;
	m_CinematicCameraPosition = vec2(0, 0);

	m_AutoSpecCamera = true;
	m_AutoSpecCameraZooming = false;
	m_CanUseCameraInfo = false;
	m_UsingAutoSpecCamera = false;

	m_aAutoSpecCameraTooltip[0] = '\0';
}

float CCamera::CameraSmoothingProgress(float CurrentTime) const
{
	float Progress = (CurrentTime - m_CameraSmoothingStart) / (m_CameraSmoothingEnd - m_CameraSmoothingStart);
	return 1.0 - std::pow(2.0, -10.0 * Progress);
}

float CCamera::ZoomProgress(float CurrentTime) const
{
	return (CurrentTime - m_ZoomSmoothingStart) / (m_ZoomSmoothingEnd - m_ZoomSmoothingStart);
}

void CCamera::RemoveDemoDynamicFov()
{
	if(m_DemoDynamicFovAppliedFactor == 1.0f)
		return;

	m_Zoom /= m_DemoDynamicFovAppliedFactor;
	m_DemoDynamicFovAppliedFactor = 1.0f;
}

void CCamera::ScaleZoom(float Factor)
{
	RemoveDemoDynamicFov();

	float CurrentTarget = m_Zooming ? m_ZoomSmoothingTarget : m_Zoom;
	ChangeZoom(CurrentTarget * Factor, GameClient()->m_Snap.m_SpecInfo.m_Active && GameClient()->m_MultiViewActivated ? g_Config.m_ClMultiViewZoomSmoothness : g_Config.m_ClSmoothZoomTime, true);

	m_AutoSpecCamera = false;
}

float CCamera::MaxZoomLevel()
{
	return (g_Config.m_ClLimitMaxZoomLevel) ? ((Graphics()->IsTileBufferingEnabled() ? 240 : 30)) : std::numeric_limits<float>::max();
}

float CCamera::MinZoomLevel()
{
	return 0.01f;
}

void CCamera::ChangeZoom(float Target, int Smoothness, bool IsUser)
{
	RemoveDemoDynamicFov();

	if(Target > MaxZoomLevel() || Target < MinZoomLevel())
	{
		return;
	}

	float Now = Client()->LocalTime();
	float Current = m_Zoom;
	float Derivative = 0.0f;
	if(m_Zooming)
	{
		float Progress = ZoomProgress(Now);
		Current = m_ZoomSmoothing.Evaluate(Progress);
		Derivative = m_ZoomSmoothing.Derivative(Progress);
	}

	m_ZoomSmoothingTarget = Target;
	m_ZoomSmoothing = CCubicBezier::With(Current, Derivative, 0, m_ZoomSmoothingTarget);
	m_ZoomSmoothingStart = Now;
	m_ZoomSmoothingEnd = Now + (float)Smoothness / 1000;

	if(IsUser)
		m_UserZoomTarget = Target;

	m_Zooming = true;
}

void CCamera::ResetAutoSpecCamera()
{
	m_AutoSpecCamera = true;
}

void CCamera::UpdateCamera()
{
	RemoveDemoDynamicFov();

	// use hardcoded smooth camera for spectating unless player explicitly turn it off
	bool CanUseCameraInfo = !GameClient()->m_MultiViewActivated;
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
	{
		// only follow mode have the correct camera info
		CanUseCameraInfo = CanUseCameraInfo && GameClient()->m_DemoSpecId == SPEC_FOLLOW;
	}
	else
	{
		CanUseCameraInfo = CanUseCameraInfo && GameClient()->m_Snap.m_SpecInfo.m_Active &&
				   GameClient()->m_Snap.m_SpecInfo.m_SpectatorId >= 0 &&
				   GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != GameClient()->m_aLocalIds[0] &&
				   (!GameClient()->Client()->DummyConnected() || GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != GameClient()->m_aLocalIds[1]);
	}

	bool UsingAutoSpecCamera = m_AutoSpecCamera && CanUseAutoSpecCamera();
	float CurrentZoom = m_Zooming ? m_ZoomSmoothingTarget : m_Zoom;
	bool ZoomChanged = false;
	if(CanUseCameraInfo && UsingAutoSpecCamera && CurrentZoom != GameClient()->m_Snap.m_SpecInfo.m_Zoom)
	{
		// start spectating player / turn on auto spec camera
		bool ChangeTarget = m_PrevSpecId != GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;
		float SmoothTime = ChangeTarget ? g_Config.m_ClSmoothSpectatingTime : 250;
		ChangeZoom(GameClient()->m_Snap.m_SpecInfo.m_Zoom, SmoothTime, false);

		// it is auto spec camera zooming if only the zoom is changed during activation, not at the start of the activation
		m_AutoSpecCameraZooming = !ChangeTarget && CanUseCameraInfo && m_UsingAutoSpecCamera;

		ZoomChanged = true;
	}
	else if((CanUseCameraInfo && !UsingAutoSpecCamera) && CurrentZoom != m_UserZoomTarget)
	{
		// turning off auto spec camera
		ChangeZoom(m_UserZoomTarget, g_Config.m_ClSmoothZoomTime, false);
		m_AutoSpecCameraZooming = false;

		ZoomChanged = true;
	}
	else if(!CanUseCameraInfo && CurrentZoom != m_UserZoomTarget)
	{
		// stop spectating player
		if(!GameClient()->m_MultiViewActivated)
			ChangeZoom(m_UserZoomTarget, g_Config.m_ClSmoothZoomTime, false);
		m_AutoSpecCameraZooming = false;

		ZoomChanged = true;
	}

	// snap zoom when going in and out of spectating
	if(ZoomChanged && m_WasSpectating != GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		m_Zoom = m_ZoomSmoothingTarget;
		m_Zooming = false;
	}

	if(m_Zooming)
	{
		float Time = Client()->LocalTime();
		if(Time >= m_ZoomSmoothingEnd)
		{
			m_Zoom = m_ZoomSmoothingTarget;
			m_Zooming = false;
			m_AutoSpecCameraZooming = false;
		}
		else
		{
			const float OldLevel = m_Zoom;
			m_Zoom = m_ZoomSmoothing.Evaluate(ZoomProgress(Time));
			if((OldLevel < m_ZoomSmoothingTarget && m_Zoom > m_ZoomSmoothingTarget) || (OldLevel > m_ZoomSmoothingTarget && m_Zoom < m_ZoomSmoothingTarget))
			{
				m_Zoom = m_ZoomSmoothingTarget;
				m_Zooming = false;
				m_AutoSpecCameraZooming = false;
			}
		}
		m_Zoom = std::clamp(m_Zoom, MinZoomLevel(), MaxZoomLevel());
	}

	if(!ZoomAllowed())
	{
		m_ZoomSet = false;
		m_Zoom = 1.0f;
		m_Zooming = false;
		m_AutoSpecCameraZooming = false;
	}
	else if(!m_ZoomSet && g_Config.m_ClDefaultZoom != 10)
	{
		m_ZoomSet = true;
		OnReset();
	}

	const float DeltaTime = Client()->RenderFrameTime();

	if(GameClient()->m_Snap.m_SpecInfo.m_Active && !GameClient()->m_Snap.m_SpecInfo.m_UsePosition)
	{
		m_aDyncamCurrentCameraOffset[g_Config.m_ClDummy] = vec2(0, 0);
		m_CanUseCameraInfo = CanUseCameraInfo;
		m_UsingAutoSpecCamera = UsingAutoSpecCamera;
		UpdateDemoCameraEffects(DeltaTime);
		return;
	}

	vec2 TargetPos = CanUseCameraInfo ? GameClient()->m_CursorInfo.Target() : GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy];
	int Smoothness = CanUseCameraInfo ? 50 : g_Config.m_ClDyncamSmoothness;
	int Stabilizing = CanUseCameraInfo ? 50 : g_Config.m_ClDyncamStabilizing;
	bool IsDyncam = CanUseCameraInfo ? true : g_Config.m_ClDyncam;

	if(Smoothness > 0)
	{
		float CameraSpeed = (1.0f - (Smoothness / 100.0f)) * 9.5f + 0.5f;
		float CameraStabilizingFactor = 1 + Stabilizing / 100.0f;

		m_DyncamSmoothingSpeedBias += CameraSpeed * DeltaTime;
		if(IsDyncam)
		{
			m_DyncamSmoothingSpeedBias -= length(TargetPos - m_LastTargetPos) * std::log10(CameraStabilizingFactor) * 0.02f;
			m_DyncamSmoothingSpeedBias = std::clamp(m_DyncamSmoothingSpeedBias, 0.5f, CameraSpeed);
		}
		else
		{
			m_DyncamSmoothingSpeedBias = maximum(5.0f, CameraSpeed); // make sure toggle back is fast
		}
	}

	m_DyncamTargetCameraOffset = vec2(0, 0);
	float l = length(TargetPos);
	if(l > 0.0001f) // make sure that this isn't 0
	{
		float CurrentDeadzone = Deadzone();
		float CurrentFollowFactor = FollowFactor();

		// use provided camera setting from server
		if(CanUseCameraInfo)
		{
			CurrentDeadzone = GameClient()->m_Snap.m_SpecInfo.m_Deadzone;
			CurrentFollowFactor = GameClient()->m_Snap.m_SpecInfo.m_FollowFactor;

			if(!UsingAutoSpecCamera)
			{
				// turn off dyncam if user zooms when spectating
				CurrentDeadzone = 0;
				CurrentFollowFactor = 0;
			}
		}

		float OffsetAmount = maximum(l - CurrentDeadzone, 0.0f) * (CurrentFollowFactor / 100.0f);

		if(CanUseCameraInfo)
		{
			OffsetAmount = minimum(OffsetAmount, 350.0f * m_Zoom);
		}

		m_DyncamTargetCameraOffset = normalize_pre_length(TargetPos, l) * OffsetAmount;
	}

	m_LastTargetPos = TargetPos;
	vec2 CurrentCameraOffset = m_aDyncamCurrentCameraOffset[g_Config.m_ClDummy];
	float SpeedBias = m_CameraSmoothing ? 50.0f : m_DyncamSmoothingSpeedBias;
	if(Smoothness > 0)
		CurrentCameraOffset += (m_DyncamTargetCameraOffset - CurrentCameraOffset) * minimum(DeltaTime * SpeedBias, 1.0f);
	else
		CurrentCameraOffset = m_DyncamTargetCameraOffset;

	// directly put the camera in place when switching in and out of freeview or spectate mode
	if(m_CanUseCameraInfo != CanUseCameraInfo)
	{
		CurrentCameraOffset = m_DyncamTargetCameraOffset;
	}

	m_aDyncamCurrentCameraOffset[g_Config.m_ClDummy] = CurrentCameraOffset;
	m_CanUseCameraInfo = CanUseCameraInfo;
	m_UsingAutoSpecCamera = UsingAutoSpecCamera;

	UpdateDemoCameraEffects(DeltaTime);
}

void CCamera::UpdateDemoCameraEffects(float DeltaTime)
{
	const CNetObj_Character *pTrackedCharacter = nullptr;
	const CNetObj_Character *pPrevTrackedCharacter = nullptr;

	if(Client()->State() == IClient::STATE_DEMOPLAYBACK && !GameClient()->m_MultiViewActivated)
	{
		int TrackedClientId = -1;
		if(GameClient()->m_Snap.m_SpecInfo.m_Active)
		{
			const int SpectatorId = GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;
			if(in_range(SpectatorId, 0, MAX_CLIENTS - 1) && GameClient()->m_Snap.m_aCharacters[SpectatorId].m_Active)
				TrackedClientId = SpectatorId;
		}
		else if(in_range(GameClient()->m_Snap.m_LocalClientId, 0, MAX_CLIENTS - 1) && GameClient()->m_Snap.m_aCharacters[GameClient()->m_Snap.m_LocalClientId].m_Active)
		{
			TrackedClientId = GameClient()->m_Snap.m_LocalClientId;
		}

		if(TrackedClientId >= 0)
		{
			pTrackedCharacter = &GameClient()->m_Snap.m_aCharacters[TrackedClientId].m_Cur;
			pPrevTrackedCharacter = &GameClient()->m_Snap.m_aCharacters[TrackedClientId].m_Prev;
		}
	}

	if(pTrackedCharacter == nullptr)
	{
		m_DemoCameraDriftTargetOffset = vec2(0.0f, 0.0f);
		m_DemoCameraDriftCurrentOffset = vec2(0.0f, 0.0f);
		m_DemoDynamicFovTarget = 1.0f;
		m_DemoDynamicFovCurrent = 1.0f;
		return;
	}

	const float IntraTick = Client()->IntraGameTick(g_Config.m_ClDummy);

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
		m_DemoCameraDriftTargetOffset = vec2(DriftDirection * DriftAmount, 0.0f);

		const float SmoothFactor = (1.0f - g_Config.m_BcCameraDriftSmoothness / 100.0f) * 10.0f;
		m_DemoCameraDriftCurrentOffset += (m_DemoCameraDriftTargetOffset - m_DemoCameraDriftCurrentOffset) * minimum(DeltaTime * SmoothFactor, 1.0f);
	}
	else
	{
		m_DemoCameraDriftTargetOffset = vec2(0.0f, 0.0f);
		m_DemoCameraDriftCurrentOffset = vec2(0.0f, 0.0f);
	}

	m_DemoDynamicFovTarget = 1.0f;
	if(g_Config.m_BcDynamicFov)
	{
		const vec2 CurrentVelocity = vec2(pTrackedCharacter->m_VelX, pTrackedCharacter->m_VelY) / 256.0f;
		const vec2 PreviousVelocity = vec2(pPrevTrackedCharacter->m_VelX, pPrevTrackedCharacter->m_VelY) / 256.0f;
		const float VelocityFactor = length(mix(PreviousVelocity, CurrentVelocity, IntraTick));
		const float DynamicFovMultiplier = 1.0f + VelocityFactor / 10.0f;
		const float DynamicFovAmount = VelocityFactor * (g_Config.m_BcDynamicFovAmount / 50.0f) * DynamicFovMultiplier;
		m_DemoDynamicFovTarget = std::clamp(1.0f + DynamicFovAmount / 500.0f, 1.0f, 5.0f);
	}

	if(g_Config.m_BcDynamicFov && g_Config.m_BcDynamicFovSmoothness > 0)
	{
		const float SmoothFactor = (1.0f - g_Config.m_BcDynamicFovSmoothness / 100.0f) * 15.0f + 0.5f;
		m_DemoDynamicFovCurrent += (m_DemoDynamicFovTarget - m_DemoDynamicFovCurrent) * minimum(DeltaTime * SmoothFactor, 1.0f);
	}
	else
	{
		m_DemoDynamicFovCurrent = m_DemoDynamicFovTarget;
	}

	m_DemoDynamicFovCurrent = maximum(1.0f, m_DemoDynamicFovCurrent);
	const float BaseZoom = m_Zoom;
	m_Zoom = std::clamp(BaseZoom * m_DemoDynamicFovCurrent, MinZoomLevel(), MaxZoomLevel());
	m_DemoDynamicFovAppliedFactor = m_Zoom / BaseZoom;
}

void CCamera::OnRender()
{
	if(m_CameraSmoothing)
	{
		if(!GameClient()->m_Snap.m_SpecInfo.m_Active)
		{
			m_Center = m_CameraSmoothingTarget;
			m_CameraSmoothing = false;
		}
		else
		{
			float Time = Client()->LocalTime();
			if(Time >= m_CameraSmoothingEnd)
			{
				m_Center = m_CameraSmoothingTarget;
				m_CameraSmoothing = false;
			}
			else
			{
				m_CameraSmoothingCenter = vec2(m_CameraSmoothingBezierX.Evaluate(CameraSmoothingProgress(Time)), m_CameraSmoothingBezierY.Evaluate(CameraSmoothingProgress(Time)));
				if(distance(m_CameraSmoothingCenter, m_CameraSmoothingTarget) <= 0.1f)
				{
					m_Center = m_CameraSmoothingTarget;
					m_CameraSmoothing = false;
				}
			}
		}
	}

	const vec2 DemoCameraDriftOffset = Client()->State() == IClient::STATE_DEMOPLAYBACK ? m_DemoCameraDriftCurrentOffset : vec2(0.0f, 0.0f);

	// update camera center
	if(GameClient()->m_Snap.m_SpecInfo.m_Active && !GameClient()->m_Snap.m_SpecInfo.m_UsePosition)
	{
		if(m_CamType != CAMTYPE_SPEC)
		{
			m_aLastPos[g_Config.m_ClDummy] = GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy];
			GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy] = m_PrevCenter;
			GameClient()->m_Controls.m_aMouseInputType[g_Config.m_ClDummy] = CControls::EMouseInputType::AUTOMATED;
			GameClient()->m_Controls.ClampMousePos();
			m_CamType = CAMTYPE_SPEC;
		}
		const vec2 TargetCenter = GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy];
		if(g_Config.m_BcCinematicCamera)
		{
			if(!m_CinematicCameraSmoothing)
			{
				m_CinematicCameraPosition = m_Center;
				m_CinematicCameraSmoothing = true;
			}
			const float FollowSpeed = 8.0f;
			m_CinematicCameraPosition += (TargetCenter - m_CinematicCameraPosition) * minimum(Client()->RenderFrameTime() * FollowSpeed, 1.0f);
			m_Center = m_CinematicCameraPosition;
		}
		else
		{
			m_Center = TargetCenter;
			m_CinematicCameraSmoothing = false;
		}
	}
	else
	{
		m_CinematicCameraSmoothing = false;
		if(m_CamType != CAMTYPE_PLAYER)
		{
			GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy] = m_aLastPos[g_Config.m_ClDummy];
			GameClient()->m_Controls.m_aMouseInputType[g_Config.m_ClDummy] = CControls::EMouseInputType::AUTOMATED;
			GameClient()->m_Controls.ClampMousePos();
			m_CamType = CAMTYPE_PLAYER;
		}

		if(GameClient()->m_Snap.m_SpecInfo.m_Active)
			m_Center = GameClient()->m_Snap.m_SpecInfo.m_Position + m_aDyncamCurrentCameraOffset[g_Config.m_ClDummy] + DemoCameraDriftOffset;
		else
			m_Center = GameClient()->m_LocalCharacterPos + m_aDyncamCurrentCameraOffset[g_Config.m_ClDummy] + DemoCameraDriftOffset;
	}

	if(m_ForceFreeview && m_CamType == CAMTYPE_SPEC)
	{
		GameClient()->m_Controls.m_aMouseInputType[g_Config.m_ClDummy] = CControls::EMouseInputType::AUTOMATED;
		m_Center = GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy] = m_ForceFreeviewPos;
		m_ForceFreeview = false;
	}
	else
		m_ForceFreeviewPos = m_Center;

	const int SpecId = GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;

	// start smoothing from the current position when the target changes
	if(m_CameraSmoothing && SpecId != m_PrevSpecId)
		m_CameraSmoothing = false;

	if(GameClient()->m_Snap.m_SpecInfo.m_Active &&
		(SpecId != m_PrevSpecId ||
			(m_CameraSmoothing && m_CameraSmoothingTarget != m_Center)) && // the target is moving during camera smoothing
		!(!m_WasSpectating && m_Center != m_PrevCenter) && // dont smooth when starting to spectate
		m_CamType != CAMTYPE_SPEC &&
		!GameClient()->m_MultiViewActivated)
	{
		float Now = Client()->LocalTime();
		if(!m_CameraSmoothing)
			m_CenterBeforeSmoothing = m_PrevCenter;

		vec2 Derivative = {0.f, 0.f};
		if(m_CameraSmoothing)
		{
			float Progress = CameraSmoothingProgress(Now);
			Derivative.x = m_CameraSmoothingBezierX.Derivative(Progress);
			Derivative.y = m_CameraSmoothingBezierY.Derivative(Progress);
		}

		m_CameraSmoothingTarget = m_Center;
		m_CameraSmoothingBezierX = CCubicBezier::With(m_CenterBeforeSmoothing.x, Derivative.x, 0, m_CameraSmoothingTarget.x);
		m_CameraSmoothingBezierY = CCubicBezier::With(m_CenterBeforeSmoothing.y, Derivative.y, 0, m_CameraSmoothingTarget.y);

		if(!m_CameraSmoothing)
		{
			m_CameraSmoothingStart = Now;
			m_CameraSmoothingEnd = Now + (float)g_Config.m_ClSmoothSpectatingTime / 1000.0f;
		}

		if(!m_CameraSmoothing)
			m_CameraSmoothingCenter = m_PrevCenter;

		m_CameraSmoothing = true;
	}

	if(m_CameraSmoothing)
		m_Center = m_CameraSmoothingCenter;

	m_PrevCenter = m_Center;
	m_PrevSpecId = SpecId;

	// demo always count as spectating
	m_WasSpectating = GameClient()->m_Snap.m_SpecInfo.m_Active;
}

void CCamera::OnConsoleInit()
{
	Console()->Register("zoom+", "?f[amount]", CFGFLAG_CLIENT, ConZoomPlus, this, "Zoom increase");
	Console()->Register("zoom-", "?f[amount]", CFGFLAG_CLIENT, ConZoomMinus, this, "Zoom decrease");
	Console()->Register("zoom", "?f", CFGFLAG_CLIENT, ConZoom, this, "Change zoom");
	Console()->Register("set_view", "i[x]i[y]", CFGFLAG_CLIENT, ConSetView, this, "Set camera position to x and y in the map");
	Console()->Register("set_view_relative", "i[x]i[y]", CFGFLAG_CLIENT, ConSetViewRelative, this, "Set camera position relative to current view in the map");
	Console()->Register("goto_switch", "i[number]?i[offset]", CFGFLAG_CLIENT, ConGotoSwitch, this, "View switch found (at offset) with given number");
	Console()->Register("goto_tele", "i[number]?i[offset]", CFGFLAG_CLIENT, ConGotoTele, this, "View tele found (at offset) with given number");
	Console()->Register("BC_cinematic_camera_toggle", "", CFGFLAG_CLIENT, ConToggleCinematicCamera, this, "Toggle cinematic spectator camera");
}

void CCamera::OnReset()
{
	m_CameraSmoothing = false;

	m_Zoom = CCamera::ZoomStepsToValue(g_Config.m_ClDefaultZoom - 10);
	m_Zooming = false;
	m_AutoSpecCameraZooming = false;
	m_UserZoomTarget = CCamera::ZoomStepsToValue(g_Config.m_ClDefaultZoom - 10);
	m_DemoCameraDriftTargetOffset = vec2(0.0f, 0.0f);
	m_DemoCameraDriftCurrentOffset = vec2(0.0f, 0.0f);
	m_DemoDynamicFovTarget = 1.0f;
	m_DemoDynamicFovCurrent = 1.0f;
	m_DemoDynamicFovAppliedFactor = 1.0f;
}

void CCamera::ConZoomPlus(IConsole::IResult *pResult, void *pUserData)
{
	CCamera *pSelf = (CCamera *)pUserData;
	if(!pSelf->ZoomAllowed())
		return;

	const float DefaultStep = g_Config.m_BcExtendZoom ? 0.5f : 1.0f;
	float ZoomAmount = pResult->NumArguments() ? pResult->GetFloat(0) : DefaultStep;

	pSelf->ScaleZoom(CCamera::ZoomStepsToValue(ZoomAmount));

	if(pSelf->GameClient()->m_MultiViewActivated)
		pSelf->GameClient()->m_MultiViewPersonalZoom += ZoomAmount;
}

void CCamera::ConToggleCinematicCamera(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	CCamera *pSelf = static_cast<CCamera *>(pUserData);
	g_Config.m_BcCinematicCamera = !g_Config.m_BcCinematicCamera;
	pSelf->GameClient()->Echo(g_Config.m_BcCinematicCamera ? "[[green]] Cinematic camera on" : "[[red]] Cinematic camera off");
}

void CCamera::ConZoomMinus(IConsole::IResult *pResult, void *pUserData)
{
	CCamera *pSelf = (CCamera *)pUserData;
	if(!pSelf->ZoomAllowed())
		return;

	const float DefaultStep = g_Config.m_BcExtendZoom ? 0.5f : 1.0f;
	float ZoomAmount = pResult->NumArguments() ? pResult->GetFloat(0) : DefaultStep;
	ZoomAmount *= -1.0f;

	pSelf->ScaleZoom(CCamera::ZoomStepsToValue(ZoomAmount));

	if(pSelf->GameClient()->m_MultiViewActivated)
		pSelf->GameClient()->m_MultiViewPersonalZoom += ZoomAmount;
}
void CCamera::ConZoom(IConsole::IResult *pResult, void *pUserData)
{
	CCamera *pSelf = (CCamera *)pUserData;
	if(!pSelf->ZoomAllowed())
		return;

	bool IsReset = !pResult->NumArguments();

	float TargetLevel = !IsReset ? pResult->GetFloat(0) : g_Config.m_ClDefaultZoom;

	if(!pSelf->CanUseAutoSpecCamera() || !pSelf->m_CanUseCameraInfo)
		pSelf->ChangeZoom(CCamera::ZoomStepsToValue(TargetLevel - 10.0f), pSelf->GameClient()->m_Snap.m_SpecInfo.m_Active && pSelf->GameClient()->m_MultiViewActivated ? g_Config.m_ClMultiViewZoomSmoothness : g_Config.m_ClSmoothZoomTime, true);
	else
		pSelf->m_UserZoomTarget = CCamera::ZoomStepsToValue(TargetLevel - 10.0f);

	pSelf->m_AutoSpecCamera = IsReset;

	if(pSelf->GameClient()->m_MultiViewActivated && pSelf->GameClient()->m_Snap.m_SpecInfo.m_Active)
		pSelf->GameClient()->m_MultiViewPersonalZoom = TargetLevel - 10.0f;
}
void CCamera::ConSetView(IConsole::IResult *pResult, void *pUserData)
{
	CCamera *pSelf = (CCamera *)pUserData;
	// wait until free view camera type to update the position
	pSelf->SetView(ivec2(pResult->GetInteger(0), pResult->GetInteger(1)));
}
void CCamera::ConSetViewRelative(IConsole::IResult *pResult, void *pUserData)
{
	CCamera *pSelf = (CCamera *)pUserData;
	// wait until free view camera type to update the position
	pSelf->SetView(ivec2(pResult->GetInteger(0), pResult->GetInteger(1)), true);
}
void CCamera::ConGotoSwitch(IConsole::IResult *pResult, void *pUserData)
{
	CCamera *pSelf = (CCamera *)pUserData;
	pSelf->GotoSwitch(pResult->GetInteger(0), pResult->NumArguments() > 1 ? pResult->GetInteger(1) : -1);
}
void CCamera::ConGotoTele(IConsole::IResult *pResult, void *pUserData)
{
	CCamera *pSelf = (CCamera *)pUserData;
	pSelf->GotoTele(pResult->GetInteger(0), pResult->NumArguments() > 1 ? pResult->GetInteger(1) : -1);
}

void CCamera::SetView(ivec2 Pos, bool Relative)
{
	vec2 RealPos = vec2(Pos.x * 32.0, Pos.y * 32.0);
	vec2 UntestedViewPos = Relative ? m_ForceFreeviewPos + RealPos : RealPos;

	m_ForceFreeview = true;

	m_ForceFreeviewPos = vec2(
		std::clamp(UntestedViewPos.x, 200.0f, Collision()->GetWidth() * 32 - 200.0f),
		std::clamp(UntestedViewPos.y, 200.0f, Collision()->GetHeight() * 32 - 200.0f));
}

void CCamera::GotoSwitch(int Number, int Offset)
{
	if(Collision()->SwitchLayer() == nullptr)
		return;

	int Match = -1;
	ivec2 MatchPos = ivec2(-1, -1);

	auto FindTile = [this, &Match, &MatchPos, &Number, &Offset]() {
		for(int x = 0; x < Collision()->GetWidth(); x++)
		{
			for(int y = 0; y < Collision()->GetHeight(); y++)
			{
				int i = y * Collision()->GetWidth() + x;
				if(Number == Collision()->GetSwitchNumber(i))
				{
					Match++;
					if(Offset != -1)
					{
						if(Match == Offset)
						{
							MatchPos = ivec2(x, y);
							m_GotoSwitchOffset = Match;
							return;
						}
						continue;
					}
					MatchPos = ivec2(x, y);
					if(Match == m_GotoSwitchOffset)
						return;
				}
			}
		}
	};
	FindTile();

	if(MatchPos == ivec2(-1, -1))
		return;
	if(Match < m_GotoSwitchOffset)
		m_GotoSwitchOffset = -1;
	SetView(MatchPos);
	m_GotoSwitchOffset++;
}

void CCamera::GotoTele(int Number, int Offset)
{
	if(Collision()->TeleLayer() == nullptr)
		return;
	Number--;

	if(m_GotoTeleLastNumber != Number)
		m_GotoTeleLastPos = ivec2(-1, -1);

	ivec2 MatchPos = ivec2(-1, -1);
	const size_t NumTeles = Collision()->TeleAllSize(Number);
	if(!NumTeles)
	{
		log_error("camera", "No teleporter with number %d found.", Number + 1);
		return;
	}

	if(Offset != -1 || m_GotoTeleLastPos == ivec2(-1, -1))
	{
		if((size_t)Offset >= NumTeles || Offset < 0)
			Offset = 0;
		vec2 Tele = Collision()->TeleAllGet(Number, Offset);
		MatchPos = ivec2(Tele.x / 32, Tele.y / 32);
		m_GotoTeleOffset = Offset;
	}
	else
	{
		bool FullRound = false;
		do
		{
			vec2 Tele = Collision()->TeleAllGet(Number, m_GotoTeleOffset);
			MatchPos = ivec2(Tele.x / 32, Tele.y / 32);
			m_GotoTeleOffset++;
			if((size_t)m_GotoTeleOffset >= NumTeles)
			{
				m_GotoTeleOffset = 0;
				if(FullRound)
				{
					MatchPos = m_GotoTeleLastPos;
					break;
				}
				FullRound = true;
			}
		} while(distance(m_GotoTeleLastPos, MatchPos) < 10.0f);
	}

	if(MatchPos == ivec2(-1, -1))
		return;
	m_GotoTeleLastPos = MatchPos;
	m_GotoTeleLastNumber = Number;
	SetView(MatchPos);
}

void CCamera::SetZoom(float Target, int Smoothness, bool IsUser)
{
	ChangeZoom(Target, Smoothness, IsUser);
}

bool CCamera::ZoomAllowed() const
{
	return GameClient()->m_Snap.m_SpecInfo.m_Active ||
	       GameClient()->m_GameInfo.m_AllowZoom ||
	       Client()->State() == IClient::STATE_DEMOPLAYBACK;
}

int CCamera::Deadzone() const
{
	return g_Config.m_ClDyncam ? g_Config.m_ClDyncamDeadzone : g_Config.m_ClMouseDeadzone;
}

int CCamera::FollowFactor() const
{
	return g_Config.m_ClDyncam ? g_Config.m_ClDyncamFollowFactor : g_Config.m_ClMouseFollowfactor;
}

bool CCamera::CanUseAutoSpecCamera() const
{
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
	{
		// only follow mode has the correct camera info
		return GameClient()->m_Snap.m_SpecInfo.m_HasCameraInfo && GameClient()->m_DemoSpecId == SPEC_FOLLOW;
	}

	return g_Config.m_ClSpecAutoSync && GameClient()->m_Snap.m_SpecInfo.m_HasCameraInfo &&
	       GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != GameClient()->m_aLocalIds[0] &&
	       (!GameClient()->Client()->DummyConnected() || GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != GameClient()->m_aLocalIds[1]);
}

void CCamera::ToggleAutoSpecCamera()
{
	if(!g_Config.m_ClSpecAutoSync)
	{
		g_Config.m_ClSpecAutoSync = 1;
		GameClient()->m_Camera.m_AutoSpecCamera = true;
	}
	else if(GameClient()->m_Camera.m_AutoSpecCamera && GameClient()->m_Camera.SpectatingPlayer() && GameClient()->m_Camera.CanUseAutoSpecCamera())
	{
		GameClient()->m_Camera.m_AutoSpecCamera = false;
	}
	else
	{
		g_Config.m_ClSpecAutoSync = 0;
	}
}

void CCamera::UpdateAutoSpecCameraTooltip()
{
	const char *pFeatureText = Localize("Auto-sync player camera");

	if(!g_Config.m_ClSpecAutoSync)
		str_format(m_aAutoSpecCameraTooltip, sizeof(m_aAutoSpecCameraTooltip), "%s: %s", pFeatureText, Localize("Disabled", "Auto camera"));
	else if(!SpectatingPlayer())
		str_format(m_aAutoSpecCameraTooltip, sizeof(m_aAutoSpecCameraTooltip), "%s: %s", pFeatureText, Localize("Enabled", "Auto camera"));
	else if(!CanUseAutoSpecCamera())
		str_format(m_aAutoSpecCameraTooltip, sizeof(m_aAutoSpecCameraTooltip), "%s: %s", pFeatureText, Localize("Unavailable for this player", "Auto camera"));
	else if(!m_AutoSpecCamera)
		str_format(m_aAutoSpecCameraTooltip, sizeof(m_aAutoSpecCameraTooltip), "%s: %s", pFeatureText, Localize("Inactive", "Auto camera"));
	else
		str_format(m_aAutoSpecCameraTooltip, sizeof(m_aAutoSpecCameraTooltip), "%s: %s", pFeatureText, Localize("Active", "Auto camera"));
}
