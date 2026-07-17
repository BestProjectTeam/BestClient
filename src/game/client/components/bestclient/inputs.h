/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_INPUTS_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_INPUTS_H

#include <base/vmath.h>

// bc_inputs prediction modes (fast / best / saiko / delta / f), see engine/shared/config.h for the BC_INPUTS_* values.
namespace BcInputs
{
	// Fractional prediction ticks the currently active mode wants to look ahead of the regular prediction tick.
	float EffectiveOffsetTicks();

	int PredictionTicks(float OffsetTicks);
	int PredictionTicksOthers(float OffsetTicks);

	void ApplyOffset(float OffsetTicks, int &Tick, float &Intra);

	float BestInterpolationAmount(float Fraction, float DeltaLength, bool Enable);
	vec2 BestInterpolate(vec2 PrevPos, vec2 CurPos, float Fraction, bool Enable);
	float BestInterpolate(float PrevPos, float CurPos, float Fraction, bool Enable);

	bool FastOthers();
	bool BestOthers();
	bool SaikoOthers();
	bool DeltaOthers();
	bool FOthers();
	bool AnyOthers();
	bool ImmediateOthers();
} // namespace BcInputs

#endif
