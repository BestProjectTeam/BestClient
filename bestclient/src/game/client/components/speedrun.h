/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_SPEEDRUN_H
#define GAME_CLIENT_COMPONENTS_SPEEDRUN_H

#include <cstddef>
#include <cstdint>

void FormatSpeedrunTime(int64_t RemainingMilliseconds, char *pBuf, size_t BufSize);

#endif
