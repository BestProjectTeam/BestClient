/* Copyright © 2026 BestClient Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_LUA_LUA_API_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_LUA_LUA_API_H

#include "lua_ui_state.h"

#include <sol/sol.hpp>

#include <string>
#include <unordered_map>

class CGameClient;
class IConsole;
class IClient;
class IGraphics;
class ITextRender;
class CUi;

struct SLuaApiCtx
{
	CGameClient *m_pGameClient;
	IConsole *m_pConsole;
	IClient *m_pClient;
	IGraphics *m_pGraphics;
	ITextRender *m_pTextRender;
	CUi *m_pUi;
	SLuaUiState *m_pUiState;
	std::unordered_map<std::string, std::string> *m_pStorage;
	const std::string *m_pStoragePath;
};

void RegisterLuaApi(sol::state &Lua, const SLuaApiCtx &Ctx);

#endif
