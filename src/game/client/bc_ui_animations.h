/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_BC_UI_ANIMATIONS_H
#define GAME_CLIENT_BC_UI_ANIMATIONS_H

#include <base/color.h>

#include <engine/shared/config.h>

#include <algorithm>
#include <cmath>

namespace BCUiAnimations
{
inline bool Enabled()
{
	return g_Config.m_BcAnimations != 0;
}

inline float Clamp01(float v)
{
	return std::clamp(v, 0.0f, 1.0f);
}

inline float MsToSeconds(int Ms)
{
	return std::max(0, Ms) / 1000.0f;
}

inline float EaseInOutQuad(float t)
{
	t = Clamp01(t);
	if(t < 0.5f)
		return 2.0f * t * t;
	return 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
}

inline float EaseOutCubic(float t)
{
	t = Clamp01(t);
	const float Inv = 1.0f - t;
	return 1.0f - Inv * Inv * Inv;
}

inline float UpdatePhase(float &Phase, float Target, float Dt, float DurationSeconds)
{
	Target = Clamp01(Target);
	if(DurationSeconds <= 0.0f || Dt <= 0.0f)
	{
		Phase = Target;
		return Phase;
	}

	const float Speed = 1.0f / DurationSeconds;
	if(Phase < Target)
		Phase = std::min(Target, Phase + Dt * Speed);
	else if(Phase > Target)
		Phase = std::max(Target, Phase - Dt * Speed);
	return Phase;
}

inline ColorRGBA MultiplyAlpha(ColorRGBA Color, float AlphaMul)
{
	Color.a *= AlphaMul;
	return Color;
}
} // namespace BCUiAnimations

#endif
