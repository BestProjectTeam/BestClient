/* Copyright © 2026 BestProject Team */
#include "quick_binds.h"

#include <base/system.h>

#include <engine/keys.h>
#include <engine/shared/config.h>

#include <game/client/components/binds.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

void CQuickBinds::OnConsoleInit()
{
	Console()->Register("+BC_45_degrees", "", CFGFLAG_CLIENT, ConToggle45Degrees, this, "45 degrees bind");
	Console()->Register("BC_45_degrees", "", CFGFLAG_CLIENT, ConToggle45Degrees, this, "45 degrees bind (toggle)");
	Console()->Register("+BC_small_sens", "", CFGFLAG_CLIENT, ConToggleSmallSens, this, "Small sens bind");
	Console()->Register("BC_small_sens", "", CFGFLAG_CLIENT, ConToggleSmallSens, this, "Small sens bind (toggle)");
	Console()->Register("BC_deepfly_toggle", "", CFGFLAG_CLIENT, ConToggleDeepfly, this, "Deep fly toggle");
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
