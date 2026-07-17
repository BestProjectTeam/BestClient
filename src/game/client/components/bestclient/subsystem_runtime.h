/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_SUBSYSTEM_RUNTIME_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_SUBSYSTEM_RUNTIME_H

#include <base/system.h>

enum class ESubsystemRuntimeState
{
	DISABLED = 0,
	ARMED,
	ACTIVE,
	COOLDOWN,
};

class CSubsystemTicker
{
public:
	static bool ShouldRunPeriodic(int64_t Now, int64_t &LastTick, int64_t Interval, bool Force = false)
	{
		if(Force || LastTick == 0 || Now - LastTick >= Interval)
		{
			LastTick = Now;
			return true;
		}
		return false;
	}
};

#endif
