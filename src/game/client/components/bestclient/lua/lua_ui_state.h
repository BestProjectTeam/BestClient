/* Copyright © 2026 BestClient Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_LUA_LUA_UI_STATE_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_LUA_LUA_UI_STATE_H

struct SLuaUiState
{
	float m_X = 0, m_Y = 0, m_W = 0;
	float m_CurY = 0;
	float m_RowH = 22.0f;
	float m_Pad = 5.0f;
	bool m_Active = false;
	int m_WidgetIdx = 0;
	char m_aWidgetIds[256] = {};
};

#endif
