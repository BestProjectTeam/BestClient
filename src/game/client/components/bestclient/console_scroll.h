/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_CONSOLE_SCROLL_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_CONSOLE_SCROLL_H

#include <game/client/components/console.h>
#include <game/client/ui_rect.h>

class CConsoleScroll
{
public:
	static int TotalBacklogLines(CGameConsole::CInstance &Console);
	static float LogLineWidth(const CGameConsole::CInstance &Console);
	static void Render(CGameConsole &GameConsole, CGameConsole::CInstance *pConsole, const CUIRect &Screen, float LogTop, float LogBottom, float LineHeight);
};

#endif
