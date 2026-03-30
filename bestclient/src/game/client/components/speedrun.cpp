/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "speedrun.h"

#include "hud.h"

#include <base/system.h>

#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/client/gameclient.h>
#include <game/localization.h>

void FormatSpeedrunTime(int64_t RemainingMilliseconds, char *pBuf, size_t BufSize)
{
	const int RemainingHours = (int)(RemainingMilliseconds / (60 * 60 * 1000));
	const int RemainingMinutes = (int)((RemainingMilliseconds / (60 * 1000)) % 60);
	const int RemainingSeconds = (int)((RemainingMilliseconds / 1000) % 60);
	const int Milliseconds = (int)(RemainingMilliseconds % 1000);
	if(RemainingHours > 0)
		str_format(pBuf, BufSize, "%02d:%02d:%02d.%03d", RemainingHours, RemainingMinutes, RemainingSeconds, Milliseconds);
	else
		str_format(pBuf, BufSize, "%02d:%02d.%03d", RemainingMinutes, RemainingSeconds, Milliseconds);
}

void CHud::RenderSpeedrunTimer()
{
	if(!GameClient()->m_Snap.m_pLocalCharacter)
		return;

	constexpr float SpeedrunTimerY = 20.0f;
	constexpr float SpeedrunTimerExpiredY = 25.0f;

	const int TotalConfiguredMilliseconds =
		g_Config.m_BcSpeedrunTimerHours * 60 * 60 * 1000 +
		g_Config.m_BcSpeedrunTimerMinutes * 60 * 1000 +
		g_Config.m_BcSpeedrunTimerSeconds * 1000 +
		g_Config.m_BcSpeedrunTimerMilliseconds;

	// Backward compatibility with legacy MMSS config.
	int TotalSpeedrunTimerMilliseconds = TotalConfiguredMilliseconds;
	if(TotalSpeedrunTimerMilliseconds <= 0 && g_Config.m_BcSpeedrunTimerTime > 0)
	{
		const int LegacyMinutes = g_Config.m_BcSpeedrunTimerTime / 100;
		const int LegacySeconds = g_Config.m_BcSpeedrunTimerTime % 100;
		TotalSpeedrunTimerMilliseconds = (LegacyMinutes * 60 + LegacySeconds) * 1000;
	}

	if(TotalSpeedrunTimerMilliseconds <= 0)
		return;

	const bool RaceStarted = (GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_RACETIME) &&
				 GameClient()->m_Snap.m_pGameInfoObj->m_WarmupTimer < 0;

	// Reset expired state when race restarts.
	if(RaceStarted && m_SpeedrunTimerExpiredTick > 0)
		m_SpeedrunTimerExpiredTick = 0;

	// Show "TIME EXPIRED!" for 5 seconds after timer end.
	if(m_SpeedrunTimerExpiredTick > 0)
	{
		const int CurrentTick = Client()->GameTick(g_Config.m_ClDummy);
		if(CurrentTick < m_SpeedrunTimerExpiredTick + Client()->GameTickSpeed() * 5)
		{
			char aBuf[64];
			str_copy(aBuf, Localize("TIME EXPIRED!"), sizeof(aBuf));
			const float Half = m_Width / 2.0f;
			const float FontSize = 12.0f;
			const float w = TextRender()->TextWidth(FontSize, aBuf, -1, -1.0f);
			TextRender()->TextColor(1.0f, 0.25f, 0.25f, 1.0f);
			TextRender()->Text(Half - w / 2, SpeedrunTimerExpiredY, FontSize, aBuf, -1.0f);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		}
		else
		{
			m_SpeedrunTimerExpiredTick = 0;
		}
		return;
	}

	if(!RaceStarted)
		return;

	const int CurrentTick = Client()->GameTick(g_Config.m_ClDummy);
	const int StartTick = -GameClient()->m_Snap.m_pGameInfoObj->m_WarmupTimer;
	const int ElapsedTicks = CurrentTick - StartTick;

	const int64_t DeadlineTicks = (int64_t)TotalSpeedrunTimerMilliseconds * Client()->GameTickSpeed() / 1000;
	const int64_t RemainingTicks = DeadlineTicks - ElapsedTicks;

	if(RemainingTicks <= 0)
	{
		m_SpeedrunTimerExpiredTick = CurrentTick;
		GameClient()->SendKill();
		if(g_Config.m_BcSpeedrunTimerAutoDisable)
			g_Config.m_BcSpeedrunTimer = 0;
		return;
	}

	const int64_t RemainingMilliseconds = RemainingTicks * 1000 / Client()->GameTickSpeed();
	char aBuf[32];
	FormatSpeedrunTime(RemainingMilliseconds, aBuf, sizeof(aBuf));

	const float Half = m_Width / 2.0f;
	const float FontSize = 8.0f;
	const float w = TextRender()->TextWidth(FontSize, aBuf, -1, -1.0f);

	if(RemainingMilliseconds <= 60 * 1000)
		TextRender()->TextColor(1.0f, 0.25f, 0.25f, 1.0f);

	TextRender()->Text(Half - w / 2, SpeedrunTimerY, FontSize, aBuf, -1.0f);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}
