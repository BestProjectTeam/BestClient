/* Copyright © 2026 BestProject Team */
#include "quick_binds.h"

#include <base/math.h>
#include <base/str.h>
#include <base/vmath.h>

#include <engine/keys.h>
#include <engine/shared/config.h>

#include <game/client/components/binds.h>
#include <game/client/components/camera.h>
#include <game/client/gameclient.h>
#include <game/collision.h>
#include <game/localization.h>
#include <game/mapitems.h>

#include <algorithm>
#include <vector>

void CQuickBinds::OnConsoleInit()
{
	Console()->Register("+BC_45_degrees", "", CFGFLAG_CLIENT, ConToggle45Degrees, this, "45 degrees bind");
	Console()->Register("BC_45_degrees", "", CFGFLAG_CLIENT, ConToggle45Degrees, this, "45 degrees bind (toggle)");
	Console()->Register("+BC_small_sens", "", CFGFLAG_CLIENT, ConToggleSmallSens, this, "Small sens bind");
	Console()->Register("BC_small_sens", "", CFGFLAG_CLIENT, ConToggleSmallSens, this, "Small sens bind (toggle)");
	Console()->Register("BC_deepfly_toggle", "", CFGFLAG_CLIENT, ConToggleDeepfly, this, "Deep fly toggle");
	Console()->Register("bc_goto_tele_cursor", "", CFGFLAG_CLIENT, ConGotoTeleCursor, this, "View teleport destination/source near cursor");
	Console()->Register("bc_goto_finish_cursor", "", CFGFLAG_CLIENT, ConGotoFinishCursor, this, "View finish near cursor (or start if already near finish)");
}

void CQuickBinds::ConToggle45Degrees(IConsole::IResult *pResult, void *pUserData)
{
	CQuickBinds *pSelf = static_cast<CQuickBinds *>(pUserData);
	const bool HasStrokeArgument = pResult->NumArguments() > 0;
	pSelf->m_45DegreesStroke = HasStrokeArgument ? (pResult->GetInteger(0) != 0) : true;

	const auto Enable = [&]() {
		if(pSelf->m_45DegreesEnabled)
			return;
		pSelf->m_45DegreesEnabled = true;
		pSelf->GameClient()->Echo(Localize("45° on"));
		g_Config.m_BcPrevInpMousesens45Degrees = pSelf->m_SmallSensEnabled ? g_Config.m_BcPrevInpMousesensSmallSens : g_Config.m_InpMousesens;
		g_Config.m_BcPrevMouseMaxDistance45Degrees = g_Config.m_ClMouseMaxDistance;
		g_Config.m_ClMouseMaxDistance = 2;
		g_Config.m_InpMousesens = 4;
	};

	const auto Disable = [&]() {
		if(!pSelf->m_45DegreesEnabled)
			return;
		pSelf->m_45DegreesEnabled = false;
		pSelf->GameClient()->Echo(Localize("45° off"));
		g_Config.m_ClMouseMaxDistance = g_Config.m_BcPrevMouseMaxDistance45Degrees;
		g_Config.m_InpMousesens = g_Config.m_BcPrevInpMousesens45Degrees;
	};

	if(!g_Config.m_BcToggle45Degrees && HasStrokeArgument)
	{
		if(pSelf->m_45DegreesStroke && !pSelf->m_45DegreesLastStroke)
			Enable();
		else if(!pSelf->m_45DegreesStroke)
			Disable();

		pSelf->m_45DegreesLastStroke = pSelf->m_45DegreesStroke;
		return;
	}

	const bool TriggerToggle = HasStrokeArgument ? (pSelf->m_45DegreesStroke && !pSelf->m_45DegreesLastStroke) : true;
	if(TriggerToggle)
	{
		if(pSelf->m_45DegreesEnabled)
			Disable();
		else
			Enable();
	}

	pSelf->m_45DegreesLastStroke = pSelf->m_45DegreesStroke;
}

void CQuickBinds::ConToggleSmallSens(IConsole::IResult *pResult, void *pUserData)
{
	CQuickBinds *pSelf = static_cast<CQuickBinds *>(pUserData);
	const bool HasStrokeArgument = pResult->NumArguments() > 0;
	pSelf->m_SmallSensStroke = HasStrokeArgument ? (pResult->GetInteger(0) != 0) : true;

	const auto Enable = [&]() {
		if(pSelf->m_SmallSensEnabled)
			return;
		pSelf->m_SmallSensEnabled = true;
		pSelf->GameClient()->Echo(Localize("small sens on"));
		g_Config.m_BcPrevInpMousesensSmallSens = pSelf->m_45DegreesEnabled ? g_Config.m_BcPrevInpMousesens45Degrees : g_Config.m_InpMousesens;
		g_Config.m_InpMousesens = 1;
	};

	const auto Disable = [&]() {
		if(!pSelf->m_SmallSensEnabled)
			return;
		pSelf->m_SmallSensEnabled = false;
		pSelf->GameClient()->Echo(Localize("small sens off"));
		g_Config.m_InpMousesens = g_Config.m_BcPrevInpMousesensSmallSens;
	};

	if(!g_Config.m_BcToggleSmallSens && HasStrokeArgument)
	{
		if(pSelf->m_SmallSensStroke && !pSelf->m_SmallSensLastStroke)
			Enable();
		else if(!pSelf->m_SmallSensStroke)
			Disable();

		pSelf->m_SmallSensLastStroke = pSelf->m_SmallSensStroke;
		return;
	}

	const bool TriggerToggle = HasStrokeArgument ? (pSelf->m_SmallSensStroke && !pSelf->m_SmallSensLastStroke) : true;
	if(TriggerToggle)
	{
		if(pSelf->m_SmallSensEnabled)
			Disable();
		else
			Enable();
	}

	pSelf->m_SmallSensLastStroke = pSelf->m_SmallSensStroke;
}

void CQuickBinds::ConToggleDeepfly(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	CQuickBinds *pSelf = static_cast<CQuickBinds *>(pUserData);

	char aCurBind[128];
	str_copy(aCurBind, pSelf->GameClient()->m_Binds.Get(KEY_MOUSE_1, KeyModifier::NONE), sizeof(aCurBind));

	if(str_find_nocase(aCurBind, "+toggle cl_dummy_hammer"))
	{
		pSelf->GameClient()->Echo(Localize("Deepfly off"));
		if(str_length(pSelf->m_aOldMouse1Bind) > 1)
			pSelf->GameClient()->m_Binds.Bind(KEY_MOUSE_1, pSelf->m_aOldMouse1Bind, false, KeyModifier::NONE);
		else
		{
			pSelf->GameClient()->Echo(Localize("No old bind in memory. Binding +fire"));
			pSelf->GameClient()->m_Binds.Bind(KEY_MOUSE_1, "+fire", false, KeyModifier::NONE);
		}
	}
	else
	{
		pSelf->GameClient()->Echo(Localize("Deepfly on"));
		str_copy(pSelf->m_aOldMouse1Bind, aCurBind, sizeof(pSelf->m_aOldMouse1Bind));
		pSelf->GameClient()->m_Binds.Bind(KEY_MOUSE_1, "+fire; +toggle cl_dummy_hammer 1 0", false, KeyModifier::NONE);
	}
}

void CQuickBinds::ConGotoTeleCursor(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	CQuickBinds *pSelf = static_cast<CQuickBinds *>(pUserData);
	pSelf->GotoTeleCursor();
}

void CQuickBinds::ConGotoFinishCursor(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	CQuickBinds *pSelf = static_cast<CQuickBinds *>(pUserData);
	pSelf->GotoFinishCursor();
}

void CQuickBinds::GotoTeleCursor()
{
	if(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW || !GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		GameClient()->Echo("You're not in freeview spectating");
		return;
	}

	CCollision *pCollision = Collision();
	if(!pCollision || pCollision->TeleLayer() == nullptr)
		return;

	const int Width = pCollision->GetWidth();
	const int Height = pCollision->GetHeight();
	const vec2 Center = GameClient()->m_Camera.m_Center;
	const ivec2 CenterTile = ivec2(std::clamp(round_to_int(Center.x / 32.0f), 0, Width - 1), std::clamp(round_to_int(Center.y / 32.0f), 0, Height - 1));

	const CTeleTile *pTele = pCollision->TeleLayer();
	bool FoundTele = false;
	CTeleTile TeleTile{};
	float BestTeleDist = -1.0f;
	for(int y = CenterTile.y - 1; y <= CenterTile.y + 1; y++)
	{
		if(y < 0 || y >= Height)
			continue;
		for(int x = CenterTile.x - 1; x <= CenterTile.x + 1; x++)
		{
			if(x < 0 || x >= Width)
				continue;
			const int TileIndex = y * Width + x;
			const CTeleTile &Tile = pTele[TileIndex];
			if(Tile.m_Number <= 0 || Tile.m_Type <= 0)
				continue;
			const vec2 Pos = vec2(x * 32.0f + 16.0f, y * 32.0f + 16.0f);
			const float Dist = distance(Pos, Center);
			if(BestTeleDist < 0.0f || Dist < BestTeleDist)
			{
				BestTeleDist = Dist;
				TeleTile = Tile;
				FoundTele = true;
			}
		}
	}

	if(!FoundTele)
	{
		GameClient()->Echo("No teleporter near cursor");
		return;
	}

	const int Number = TeleTile.m_Number - 1;
	const int Type = TeleTile.m_Type;

	std::vector<ivec2> Targets;

	auto IsTypeAny = [](int Value, std::initializer_list<int> Types) {
		for(int T : Types)
		{
			if(Value == T)
				return true;
		}
		return false;
	};

	auto CollectTargets = [&](std::initializer_list<int> Types) {
		Targets.clear();
		for(int y = 0; y < Height; y++)
		{
			for(int x = 0; x < Width; x++)
			{
				const int TileIndex = y * Width + x;
				const CTeleTile &Tile = pTele[TileIndex];
				if(Tile.m_Number == Number + 1 && IsTypeAny(Tile.m_Type, Types))
					Targets.emplace_back(x, y);
			}
		}
	};

	const bool IsTeleOut = IsTypeAny(Type, {TILE_TELEOUT});
	const bool IsTeleCheckOut = IsTypeAny(Type, {TILE_TELECHECKOUT});
	const bool IsTeleIn = IsTypeAny(Type, {TILE_TELEIN, TILE_TELEINEVIL, TILE_TELEINWEAPON, TILE_TELEINHOOK});
	const bool IsTeleCheckIn = IsTypeAny(Type, {TILE_TELECHECK, TILE_TELECHECKIN, TILE_TELECHECKINEVIL});

	if(IsTeleOut)
	{
		CollectTargets({TILE_TELEIN, TILE_TELEINEVIL, TILE_TELEINWEAPON, TILE_TELEINHOOK});
	}
	else if(IsTeleCheckOut)
	{
		CollectTargets({TILE_TELECHECK, TILE_TELECHECKIN, TILE_TELECHECKINEVIL});
		if(Targets.empty())
			CollectTargets({TILE_TELEIN, TILE_TELEINEVIL, TILE_TELEINWEAPON, TILE_TELEINHOOK});
	}
	else if(IsTeleCheckIn)
	{
		CollectTargets({TILE_TELECHECKOUT});
	}
	else if(IsTeleIn)
	{
		CollectTargets({TILE_TELEOUT});
		if(Targets.empty())
			CollectTargets({TILE_TELECHECKOUT});
	}

	if(Targets.empty())
	{
		GameClient()->Echo("No teleporter destination found");
		return;
	}

	int BestIndex = 0;
	float BestDist = -1.0f;
	for(int i = 0; i < (int)Targets.size(); i++)
	{
		const vec2 Pos = vec2(Targets[i].x * 32.0f + 16.0f, Targets[i].y * 32.0f + 16.0f);
		const float Dist = distance(Pos, Center);
		if(BestDist < 0.0f || Dist < BestDist)
		{
			BestDist = Dist;
			BestIndex = i;
		}
	}

	GameClient()->m_Camera.SetView(Targets[BestIndex]);
}

void CQuickBinds::GotoFinishCursor()
{
	if(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW || !GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		GameClient()->Echo("You're not in freeview spectating");
		return;
	}

	CCollision *pCollision = Collision();
	if(!pCollision)
		return;

	const int Width = pCollision->GetWidth();
	const int Height = pCollision->GetHeight();
	const vec2 Center = GameClient()->m_Camera.m_Center;
	const ivec2 CenterTile = ivec2(std::clamp(round_to_int(Center.x / 32.0f), 0, Width - 1), std::clamp(round_to_int(Center.y / 32.0f), 0, Height - 1));

	auto HasTile = [&](int Index, int Tile) -> bool {
		return pCollision->GetTileIndex(Index) == Tile || pCollision->GetFrontTileIndex(Index) == Tile;
	};

	bool NearFinish = false;
	for(int y = CenterTile.y - 1; y <= CenterTile.y + 1 && !NearFinish; y++)
	{
		if(y < 0 || y >= Height)
			continue;
		for(int x = CenterTile.x - 1; x <= CenterTile.x + 1; x++)
		{
			if(x < 0 || x >= Width)
				continue;
			const int TileIndex = y * Width + x;
			if(HasTile(TileIndex, TILE_FINISH))
			{
				NearFinish = true;
				break;
			}
		}
	}

	const int TargetTile = NearFinish ? TILE_START : TILE_FINISH;
	const char *pMissingMsg = NearFinish ? "No start found" : "No finish found";

	std::vector<ivec2> Targets;
	for(int y = 0; y < Height; y++)
	{
		for(int x = 0; x < Width; x++)
		{
			const int TileIndex = y * Width + x;
			if(HasTile(TileIndex, TargetTile))
				Targets.emplace_back(x, y);
		}
	}

	if(Targets.empty())
	{
		GameClient()->Echo(pMissingMsg);
		return;
	}

	int BestIndex = 0;
	float BestDist = -1.0f;
	for(int i = 0; i < (int)Targets.size(); i++)
	{
		const vec2 Pos = vec2(Targets[i].x * 32.0f + 16.0f, Targets[i].y * 32.0f + 16.0f);
		const float Dist = distance(Pos, Center);
		if(BestDist < 0.0f || Dist < BestDist)
		{
			BestDist = Dist;
			BestIndex = i;
		}
	}

	GameClient()->m_Camera.SetView(Targets[BestIndex]);
}
