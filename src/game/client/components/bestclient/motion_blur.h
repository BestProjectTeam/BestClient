/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_MOTION_BLUR_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_MOTION_BLUR_H

#include <base/time.h>

#include <engine/shared/config.h>

#include <algorithm>
#include <cmath>

inline bool BcMotionBlurEnabled()
{
	return g_Config.m_BcMotionBlur != 0 && g_Config.m_BcMotionBlurStrength > 0;
}

inline float BcMotionBlurAdvanceFrameTime(int64_t &LastTime)
{
	const int64_t Now = time_get();
	float FrameTime = 1.0f / 60.0f;
	if(LastTime != 0)
		FrameTime = (Now - LastTime) / (float)time_freq();
	LastTime = Now;
	return std::clamp(FrameTime, 0.001f, 0.1f);
}

inline float BcMotionBlurPersistence(float RefAlpha60, float FrameTime)
{
	RefAlpha60 = std::clamp(RefAlpha60, 0.0f, 0.999f);
	if(RefAlpha60 <= 0.0f)
		return 0.0f;
	return std::pow(RefAlpha60, FrameTime * 60.0f);
}

#endif
