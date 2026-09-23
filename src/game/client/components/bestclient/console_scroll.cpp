/* Copyright © 2026 BestProject Team */
#include "console_scroll.h"

#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/input.h>

#include <game/client/components/bestclient/scroll_bar.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>

#include <algorithm>
#include <cmath>

int CConsoleScroll::TotalBacklogLines(CGameConsole::CInstance &Console)
{
	int Lines = 0;
	for(CGameConsole::CInstance::CBacklogEntry *pEntry = Console.m_Backlog.First(); pEntry; pEntry = Console.m_Backlog.Next(pEntry))
	{
		if(pEntry->m_LineCount == -1)
			Console.UpdateEntryTextAttributes(pEntry);
		Lines += pEntry->m_LineCount;
	}
	return Lines;
}

float CConsoleScroll::LogLineWidth(const CGameConsole::CInstance &Console)
{
	float Width = Console.m_pGameConsole->BcGameClient()->Ui()->Screen()->w - 10.0f;
	Width -= (BestClientScrollBar::CONSOLE.m_Width + BestClientScrollBar::CONSOLE.m_OuterMargin);
	return std::max(0.0f, Width);
}

void CConsoleScroll::Render(CGameConsole &GameConsole, CGameConsole::CInstance *pConsole, const CUIRect &Screen, float LogTop, float LogBottom, float LineHeight)
{
	CGameClient *pClient = GameConsole.BcGameClient();
	const float LogHeight = std::max(0.0f, LogBottom - LogTop);
	const int VisibleLines = std::max(1, (int)std::floor(LogHeight / LineHeight));
	const int TotalLines = TotalBacklogLines(*pConsole);
	const int MaxScroll = std::max(0, TotalLines - VisibleLines);

	CUIRect ScrollbarRect;
	ScrollbarRect.x = Screen.w - BestClientScrollBar::CONSOLE.m_Width - BestClientScrollBar::CONSOLE.m_OuterMargin;
	ScrollbarRect.y = LogTop;
	ScrollbarRect.w = BestClientScrollBar::CONSOLE.m_Width;
	ScrollbarRect.h = LogHeight;

	const vec2 ScreenSize = vec2(Screen.w, Screen.h);
	const vec2 WindowSize = vec2(std::max(1.0f, (float)pClient->Graphics()->WindowWidth()), std::max(1.0f, (float)pClient->Graphics()->WindowHeight()));
	const vec2 MousePos = GameConsole.m_TouchState.m_PrimaryPressed ? (GameConsole.m_TouchState.m_PrimaryPosition * ScreenSize) : (pClient->Input()->NativeMousePos() / WindowSize * ScreenSize);
	const bool MouseDown = GameConsole.m_TouchState.m_PrimaryPressed || pClient->Input()->NativeMousePressed(1);

	BestClientScrollBar::SDragState ScrollState;
	ScrollState.m_Dragging = pConsole->m_ScrollbarDragging;
	ScrollState.m_DragOffset = pConsole->m_ScrollbarDragOffset;
	const int NewLine = BestClientScrollBar::DoVertical(ScrollState, ScrollbarRect, pConsole->m_BacklogCurLine, MaxScroll, MousePos, MouseDown, BestClientScrollBar::CONSOLE);
	pConsole->m_ScrollbarDragging = ScrollState.m_Dragging;
	pConsole->m_ScrollbarDragOffset = ScrollState.m_DragOffset;

	if(NewLine != pConsole->m_BacklogCurLine)
	{
		pConsole->m_BacklogCurLine = NewLine;
		pConsole->m_BacklogLastActiveLine = pConsole->m_BacklogCurLine;
		pConsole->m_HasSelection = false;
	}
	if(pConsole->m_ScrollbarDragging)
	{
		pConsole->m_MouseIsPress = false;
		pConsole->m_HasSelection = false;
	}
}
