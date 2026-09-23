/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_CAMERA_EXTRAS_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_CAMERA_EXTRAS_H

#include <base/vmath.h>

class CCameraExtras
{
public:
	static float DefaultZoomStep(bool ExtendZoom);
	static void ApplyCinematicFreeview(vec2 &Center, const vec2 &TargetCenter, bool &Smoothing, vec2 &SmoothPos, int Strength, float FrameTime);
	static void ResetCinematic(bool &Smoothing);
};

#endif
