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

inline float EaseInOutQuart(float t)
{
	t = Clamp01(t);
	if(t < 0.5f)
		return 8.0f * t * t * t * t;
	const float Inv = -2.0f * t + 2.0f;
	return 1.0f - (Inv * Inv * Inv * Inv) / 2.0f;
}

// Overshooting "pop" easing, suitable for alpha/intensity animation without affecting layout.
// Matches typical easeOutBack behavior: fast start, slight overshoot near the end.
inline float EaseOutBack(float t, float Overshoot = 1.70158f)
{
	t = Clamp01(t);
	const float t1 = t - 1.0f;
	return 1.0f + (Overshoot + 1.0f) * (t1 * t1 * t1) + Overshoot * (t1 * t1);
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
}

#endif
