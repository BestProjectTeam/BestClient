/* Copyright © 2026 BestProject Team */
#include "camera_extras.h"

#include <algorithm>

float CCameraExtras::DefaultZoomStep(bool ExtendZoom)
{
	return ExtendZoom ? 0.5f : 1.0f;
}

void CCameraExtras::ApplyCinematicFreeview(vec2 &Center, const vec2 &TargetCenter, bool &Smoothing, vec2 &SmoothPos, int Strength, float FrameTime)
{
	if(!Smoothing)
	{
		SmoothPos = Center;
		Smoothing = true;
	}
	const float Strength01 = Strength / 100.0f;
	const float FollowSpeed = std::max(0.5f, 16.0f * (1.0f - Strength01));
	SmoothPos += (TargetCenter - SmoothPos) * std::min(FrameTime * FollowSpeed, 1.0f);
	Center = SmoothPos;
}

void CCameraExtras::ResetCinematic(bool &Smoothing)
{
	Smoothing = false;
}
