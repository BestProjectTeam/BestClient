/* Copyright © 2026 BestClient Team */
#include "lua_api.h"

#include <base/system.h>
#include <engine/client.h>
#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/textrender.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>

#include <engine/font_icons.h>
#include <engine/shared/jsonwriter.h>

#include <algorithm>
#include <optional>
#include <unordered_map>

static ColorRGBA ExtractColor(const sol::table &C)
{
	return ColorRGBA(
		C.get_or("r", 1.0f),
		C.get_or("g", 1.0f),
		C.get_or("b", 1.0f),
		C.get_or("a", 1.0f));
}

static void SaveLuaStorage(const std::string &Path, const std::unordered_map<std::string, std::string> &Storage)
{
	IOHANDLE F = io_open(Path.c_str(), IOFLAG_WRITE);
	if(!F)
		return;
	CJsonFileWriter Writer(F); // closes F in destructor
	Writer.BeginObject();
	for(const auto &[Key, Val] : Storage)
	{
		Writer.WriteAttribute(Key.c_str());
		Writer.WriteStrValue(Val.c_str());
	}
	Writer.EndObject();
}

void RegisterLuaApi(sol::state &Lua, const SLuaApiCtx &Ctx)
{
	CGameClient *pGC = Ctx.m_pGameClient;
	IConsole *pConsole = Ctx.m_pConsole;
	IClient *pClient = Ctx.m_pClient;
	IGraphics *pGraphics = Ctx.m_pGraphics;
	ITextRender *pTextRender = Ctx.m_pTextRender;
	CUi *pUi = Ctx.m_pUi;
	SLuaUiState *pUiState = Ctx.m_pUiState;

	Lua.set_function("exec", [pConsole](const std::string &Cmd) {
		pConsole->ExecuteLine(Cmd.c_str(), -1);
	});

	Lua.set_function("echo", [pGC](const std::string &Msg) {
		pGC->m_Chat.Echo(Msg.c_str());
	});

	Lua.set_function("say", [pGC](const std::string &Msg) {
		pGC->m_Chat.SendChat(0, Msg.c_str());
	});

	auto Game = Lua.create_named_table("game");

	Game.set_function("state", [pClient]() -> std::string {
		switch(pClient->State())
		{
		case IClient::STATE_OFFLINE: return "offline";
		case IClient::STATE_CONNECTING: return "connecting";
		case IClient::STATE_LOADING: return "loading";
		case IClient::STATE_ONLINE: return "online";
		case IClient::STATE_DEMOPLAYBACK: return "demo";
		case IClient::STATE_QUITTING: return "quitting";
		case IClient::STATE_RESTARTING: return "restarting";
		default: return "offline";
		}
	});

	Game.set_function("map", [pGC, pClient]() -> std::optional<std::string> {
		auto State = pClient->State();
		if(State == IClient::STATE_ONLINE || State == IClient::STATE_DEMOPLAYBACK)
			return std::string(pGC->Map()->BaseName());
		if(pGC->m_ConnectServerInfo)
			return std::string(pGC->m_ConnectServerInfo->m_aMap);
		return std::nullopt;
	});

	Game.set_function("server_name", [pGC, pClient]() -> std::optional<std::string> {
		if(pClient->State() != IClient::STATE_ONLINE && pClient->State() != IClient::STATE_DEMOPLAYBACK)
			return std::nullopt;
		CServerInfo Info;
		pClient->GetServerInfo(&Info);
		return std::string(Info.m_aName);
	});

	Game.set_function("players_count", [pGC, pClient]() -> int {
		if(pClient->State() != IClient::STATE_ONLINE && pClient->State() != IClient::STATE_DEMOPLAYBACK)
			return 0;
		return pGC->m_Snap.m_NumPlayers;
	});

	Game.set_function("is_race", [pGC, pClient]() -> bool {
		if(pClient->State() != IClient::STATE_ONLINE && pClient->State() != IClient::STATE_DEMOPLAYBACK)
			return false;
		return pGC->m_GameInfo.m_Race;
	});

	Game.set_function("is_pvp", [pGC, pClient]() -> bool {
		if(pClient->State() != IClient::STATE_ONLINE && pClient->State() != IClient::STATE_DEMOPLAYBACK)
			return false;
		return pGC->m_GameInfo.m_Pvp;
	});

	// local player table — named "player" ("local" is a Lua keyword)
	lua_State *L = Lua.lua_state();
	auto Player = Lua.create_named_table("player");

	Player.set_function("pos", [pGC, pClient, L]() -> std::optional<sol::table> {
		if(pClient->State() != IClient::STATE_ONLINE || !pGC->m_Snap.m_pLocalCharacter)
			return std::nullopt;
		const auto *T = pGC->m_Snap.m_pLocalCharacter;
		sol::table Pos(L, sol::create);
		Pos["x"] = (float)T->m_X;
		Pos["y"] = (float)T->m_Y;
		return Pos;
	});

	Player.set_function("vel", [pGC, pClient, L]() -> std::optional<sol::table> {
		if(pClient->State() != IClient::STATE_ONLINE || !pGC->m_Snap.m_pLocalCharacter)
			return std::nullopt;
		const auto *T = pGC->m_Snap.m_pLocalCharacter;
		sol::table Vel(L, sol::create);
		Vel["x"] = T->m_VelX / 256.0f;
		Vel["y"] = T->m_VelY / 256.0f;
		return Vel;
	});

	Player.set_function("name", [pGC, pClient]() -> std::optional<std::string> {
		if(pClient->State() != IClient::STATE_ONLINE || pGC->m_Snap.m_LocalClientId < 0)
			return std::nullopt;
		return std::string(pGC->m_aClients[pGC->m_Snap.m_LocalClientId].m_aName);
	});

	Player.set_function("clan", [pGC, pClient]() -> std::optional<std::string> {
		if(pClient->State() != IClient::STATE_ONLINE || pGC->m_Snap.m_LocalClientId < 0)
			return std::nullopt;
		return std::string(pGC->m_aClients[pGC->m_Snap.m_LocalClientId].m_aClan);
	});

	Player.set_function("team", [pGC, pClient]() -> int {
		if(pClient->State() != IClient::STATE_ONLINE || pGC->m_Snap.m_LocalClientId < 0)
			return -1;
		return pGC->m_aClients[pGC->m_Snap.m_LocalClientId].m_Team;
	});

	Player.set_function("is_frozen", [pGC, pClient]() -> bool {
		if(pClient->State() != IClient::STATE_ONLINE || pGC->m_Snap.m_LocalClientId < 0)
			return false;
		const auto &C = pGC->m_aClients[pGC->m_Snap.m_LocalClientId];
		return C.m_FreezeEnd != 0 || C.m_DeepFrozen || C.m_LiveFrozen;
	});

	Player.set_function("is_dead", [pGC, pClient]() -> bool {
		if(pClient->State() != IClient::STATE_ONLINE)
			return true;
		return pGC->m_Snap.m_pLocalCharacter == nullptr;
	});

	// players table — info about all players in the current snapshot
	auto Players = Lua.create_named_table("players");

	Players.set_function("count", [pGC, pClient]() -> int {
		if(pClient->State() != IClient::STATE_ONLINE && pClient->State() != IClient::STATE_DEMOPLAYBACK)
			return 0;
		return pGC->m_Snap.m_NumPlayers;
	});

	Players.set_function("get", [pGC, pClient, L](int Id) -> std::optional<sol::table> {
		if(pClient->State() != IClient::STATE_ONLINE && pClient->State() != IClient::STATE_DEMOPLAYBACK)
			return std::nullopt;
		if(Id < 0 || Id >= MAX_CLIENTS)
			return std::nullopt;
		if(!pGC->m_Snap.m_apPlayerInfos[Id])
			return std::nullopt;

		const auto &Client = pGC->m_aClients[Id];
		const auto &CharInfo = pGC->m_Snap.m_aCharacters[Id];

		sol::table T(L, sol::create);
		T["name"] = std::string(Client.m_aName);
		T["clan"] = std::string(Client.m_aClan);
		T["team"] = (int)Client.m_Team;
		T["is_frozen"] = Client.m_FreezeEnd != 0 || Client.m_DeepFrozen || Client.m_LiveFrozen;
		T["is_dead"] = !CharInfo.m_Active;

		if(CharInfo.m_Active)
		{
			sol::table Pos(L, sol::create);
			Pos["x"] = (float)CharInfo.m_Cur.m_X;
			Pos["y"] = (float)CharInfo.m_Cur.m_Y;
			T["pos"] = Pos;

			sol::table Vel(L, sol::create);
			Vel["x"] = CharInfo.m_Cur.m_VelX / 256.0f;
			Vel["y"] = CharInfo.m_Cur.m_VelY / 256.0f;
			T["vel"] = Vel;
		}
		else
		{
			T["pos"] = sol::nil;
			T["vel"] = sol::nil;
		}

		return T;
	});

	Players.set_function("find_by_name", [pGC, pClient](const std::string &Name) -> std::optional<int> {
		if(pClient->State() != IClient::STATE_ONLINE && pClient->State() != IClient::STATE_DEMOPLAYBACK)
			return std::nullopt;
		// exact match first
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(!pGC->m_Snap.m_apPlayerInfos[i])
				continue;
			if(str_comp(pGC->m_aClients[i].m_aName, Name.c_str()) == 0)
				return i;
		}
		// case-insensitive fallback
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(!pGC->m_Snap.m_apPlayerInfos[i])
				continue;
			if(str_comp_nocase(pGC->m_aClients[i].m_aName, Name.c_str()) == 0)
				return i;
		}
		return std::nullopt;
	});

	// render table — HUD drawing (only valid inside on_render)
	auto Render = Lua.create_named_table("render");

	Render.set_function("text", [pTextRender](float X, float Y, float Size, const std::string &Text, sol::table Color) {
		ColorRGBA Col = ExtractColor(Color);
		pTextRender->TextColor(Col);
		pTextRender->Text(X, Y, Size, Text.c_str(), -1.0f);
		pTextRender->TextColor(pTextRender->DefaultTextColor());
	});

	Render.set_function("rect", [pGraphics](float X, float Y, float W, float H, sol::table Color, float Rounding) {
		pGraphics->DrawRect(X, Y, W, H, ExtractColor(Color), IGraphics::CORNER_ALL, Rounding);
	});

	Render.set_function("line", [pGraphics](float X0, float Y0, float X1, float Y1, sol::table Color, float /*Thickness*/) {
		pGraphics->TextureClear();
		pGraphics->LinesBegin();
		pGraphics->SetColor(ExtractColor(Color));
		IGraphics::CLineItem Line(X0, Y0, X1, Y1);
		pGraphics->LinesDraw(&Line, 1);
		pGraphics->LinesEnd();
	});

	Render.set_function("circle", [pGraphics](float CX, float CY, float Radius, sol::table Color) {
		pGraphics->TextureClear();
		pGraphics->QuadsBegin();
		pGraphics->SetColor(ExtractColor(Color));
		pGraphics->DrawCircle(CX, CY, Radius, 32);
		pGraphics->QuadsEnd();
	});

	Render.set_function("world_to_screen", [pGC, pGraphics, pUi, L](float WX, float WY) -> sol::table {
		float WorldW = 0.0f, WorldH = 0.0f;
		pGraphics->CalcScreenParams(pGraphics->ScreenAspect(), pGC->m_Camera.m_Zoom, &WorldW, &WorldH);
		const vec2 &Center = pGC->m_Camera.m_Center;
		const CUIRect *pScreen = pUi->Screen();
		float SX = (WX - (Center.x - WorldW * 0.5f)) / WorldW * pScreen->w;
		float SY = (WY - (Center.y - WorldH * 0.5f)) / WorldH * pScreen->h;
		sol::table Pos(L, sol::create);
		Pos["x"] = SX;
		Pos["y"] = SY;
		return Pos;
	});

	// storage table — persistent key-value store (saved to .json next to the script)
	auto Storage = Lua.create_named_table("storage");
	auto *pStorage = Ctx.m_pStorage;
	const auto *pStoragePath = Ctx.m_pStoragePath;

	Storage.set_function("get", [pStorage, pStoragePath, L](const std::string &Key, sol::object Default) -> sol::object {
		if(!pStorage || !pStoragePath)
			return Default;
		auto It = pStorage->find(Key);
		if(It == pStorage->end())
			return Default;
		const std::string &Val = It->second;
		if(Default.is<bool>())
			return sol::make_object(L, Val == "true");
		else if(Default.is<double>())
		{
			try { return sol::make_object(L, std::stod(Val)); }
			catch(...) { return Default; }
		}
		else
			return sol::make_object(L, Val);
	});

	Storage.set_function("set", [pStorage, pStoragePath](const std::string &Key, sol::object Value) {
		if(!pStorage || !pStoragePath)
			return;
		if(Value.is<bool>())
			(*pStorage)[Key] = Value.as<bool>() ? "true" : "false";
		else if(Value.is<double>())
		{
			char aBuf[64];
			str_format(aBuf, sizeof(aBuf), "%g", Value.as<double>());
			(*pStorage)[Key] = aBuf;
		}
		else if(Value.is<std::string>())
			(*pStorage)[Key] = Value.as<std::string>();
		SaveLuaStorage(*pStoragePath, *pStorage);
	});

	// ui table — immediate-mode widgets for on_settings
	auto Ui = Lua.create_named_table("ui");

	Ui.set_function("checkbox", [pUiState, pTextRender, pUi](const std::string &Label, bool Value) -> bool {
		if(!pUiState->m_Active)
			return Value;
		const void *pId = &pUiState->m_aWidgetIds[pUiState->m_WidgetIdx % 256];
		pUiState->m_WidgetIdx++;

		CUIRect FullRect{pUiState->m_X + pUiState->m_Pad, pUiState->m_CurY, pUiState->m_W - pUiState->m_Pad * 2.0f, pUiState->m_RowH};
		CUIRect BoxRect, LabelRect;
		FullRect.VSplitLeft(FullRect.h, &BoxRect, &LabelRect);
		LabelRect.VSplitLeft(5.0f, nullptr, &LabelRect);

		BoxRect.Margin(2.0f, &BoxRect);
		BoxRect.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f * pUi->ButtonColorMul(pId)), IGraphics::CORNER_ALL, 3.0f);

		if(Value)
		{
			pTextRender->SetRenderFlags(TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | TEXT_RENDER_FLAG_NO_X_BEARING | TEXT_RENDER_FLAG_NO_Y_BEARING | TEXT_RENDER_FLAG_NO_OVERSIZE | TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
			pTextRender->SetFontPreset(EFontPreset::ICON_FONT);
			pUi->DoLabel(&BoxRect, FontIcon::XMARK, BoxRect.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
			pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);
			pTextRender->SetRenderFlags(0);
		}

		pUi->DoLabel(&LabelRect, Label.c_str(), FullRect.h * CUi::ms_FontmodHeight, TEXTALIGN_ML);

		if(pUi->DoButtonLogic(pId, Value ? 1 : 0, &FullRect, BUTTONFLAG_LEFT, CUi::EButtonSoundType::CHECKBOX))
			Value = !Value;

		pUiState->m_CurY += pUiState->m_RowH;
		return Value;
	});

	Ui.set_function("slider", [pUiState, pUi](const std::string &Label, float Value, float Min, float Max) -> float {
		if(!pUiState->m_Active)
			return Value;
		const void *pId = &pUiState->m_aWidgetIds[pUiState->m_WidgetIdx % 256];
		pUiState->m_WidgetIdx++;

		CUIRect FullRect{pUiState->m_X + pUiState->m_Pad, pUiState->m_CurY, pUiState->m_W - pUiState->m_Pad * 2.0f, pUiState->m_RowH};
		CUIRect LabelRect, SliderRect;
		FullRect.VSplitMid(&LabelRect, &SliderRect, std::min(10.0f, FullRect.w * 0.05f));

		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "%s: %.1f", Label.c_str(), Value);
		const float FontSize = LabelRect.h * CUi::ms_FontmodHeight * 0.8f;
		pUi->DoLabel(&LabelRect, aBuf, FontSize, TEXTALIGN_ML);

		const float t = (Max > Min) ? std::clamp((Value - Min) / (Max - Min), 0.0f, 1.0f) : 0.0f;
		const float NewT = pUi->DoScrollbarH(pId, &SliderRect, t);
		Value = std::clamp(Min + NewT * (Max - Min), Min, Max);

		pUiState->m_CurY += pUiState->m_RowH;
		return Value;
	});

	Ui.set_function("button", [pUiState, pUi](const std::string &Label) -> bool {
		if(!pUiState->m_Active)
			return false;
		const void *pId = &pUiState->m_aWidgetIds[pUiState->m_WidgetIdx % 256];
		pUiState->m_WidgetIdx++;

		CUIRect BtnRect{pUiState->m_X + pUiState->m_Pad, pUiState->m_CurY, pUiState->m_W - pUiState->m_Pad * 2.0f, pUiState->m_RowH};
		ColorRGBA Color(1.0f, 1.0f, 1.0f, 0.5f * pUi->ButtonColorMul(pId));
		BtnRect.Draw(Color, IGraphics::CORNER_ALL, 5.0f);

		const float FontSize = BtnRect.h * CUi::ms_FontmodHeight;
		pUi->DoLabel(&BtnRect, Label.c_str(), FontSize, TEXTALIGN_MC);

		const bool Clicked = pUi->DoButtonLogic(pId, 0, &BtnRect, BUTTONFLAG_LEFT);
		pUiState->m_CurY += pUiState->m_RowH;
		return Clicked;
	});

	Ui.set_function("text", [pUiState, pTextRender](const std::string &Text) {
		if(!pUiState->m_Active)
			return;
		float X = pUiState->m_X + pUiState->m_Pad;
		float Y = pUiState->m_CurY + (pUiState->m_RowH - 12.0f) * 0.5f;
		pTextRender->Text(X, Y, 12.0f, Text.c_str(), -1.0f);
		pUiState->m_CurY += pUiState->m_RowH;
	});

	Ui.set_function("separator", [pUiState, pGraphics]() {
		if(!pUiState->m_Active)
			return;
		float X = pUiState->m_X + pUiState->m_Pad;
		float Y = pUiState->m_CurY + pUiState->m_RowH * 0.5f;
		float W = pUiState->m_W - pUiState->m_Pad * 2.0f;
		pGraphics->DrawRect(X, Y - 1.0f, W, 2.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.15f), IGraphics::CORNER_NONE, 0.0f);
		pUiState->m_CurY += pUiState->m_RowH * 0.5f;
	});
}
