/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_CAMERA_DEMOS_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_CAMERA_DEMOS_H

#include <base/vmath.h>

class CGameClient;
class IInput;

class CCameraDemos
{
	static vec2 s_DriftTargetOffset;
	static vec2 s_DriftCurrentOffset;
	static float s_DynamicFovTarget;
	static float s_DynamicFovCurrent;
	static float s_DynamicFovAppliedFactor;
	static vec2 s_FreeviewVelocity;

public:
	static void Reset();
	static void RemoveDynamicFov(float &Zoom);
	static void UpdateEffects(CGameClient *pGameClient, float DeltaTime, float &Zoom, float MinZoom, float MaxZoom);
	static vec2 DriftOffset(bool DemoPlayback);
	static void ApplyFreeviewNumpad(IInput *pInput, float FrameTime, bool &ForceFreeview, vec2 &ForceFreeviewPos, vec2 &Center);
};

#endif
