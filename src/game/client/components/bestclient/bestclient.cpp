#include "bestclient.h"

#include <engine/client/enums.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

CBestClient::CBestClient()
{
	OnReset();
}

void CBestClient::ConToggle45Degrees(IConsole::IResult *pResult, void *pUserData)
{
	CBestClient *pSelf = static_cast<CBestClient *>(pUserData);
	const bool HasStrokeArgument = pResult->NumArguments() > 0;
	pSelf->m_45degreestoggle = HasStrokeArgument ? (pResult->GetInteger(0) != 0) : 1;

	const auto Enable45Degrees = [&]() {
		if(pSelf->m_45degreesEnabled)
			return;
		pSelf->m_45degreesEnabled = 1;
		pSelf->GameClient()->Echo("[[green]] 45° on");
		g_Config.m_BcPrevInpMousesens45Degrees = (pSelf->m_SmallsensEnabled == 1 ? g_Config.m_BcPrevInpMousesensSmallSens : g_Config.m_InpMousesens);
		g_Config.m_BcPrevMouseMaxDistance45Degrees = g_Config.m_ClMouseMaxDistance;
		g_Config.m_ClMouseMaxDistance = 2;
		g_Config.m_InpMousesens = 4;
	};

	const auto Disable45Degrees = [&]() {
		if(!pSelf->m_45degreesEnabled)
			return;
		pSelf->m_45degreesEnabled = 0;
		pSelf->GameClient()->Echo("[[red]] 45° off");
		g_Config.m_ClMouseMaxDistance = g_Config.m_BcPrevMouseMaxDistance45Degrees;
		g_Config.m_InpMousesens = g_Config.m_BcPrevInpMousesens45Degrees;
	};

	if(!g_Config.m_BcToggle45Degrees && HasStrokeArgument)
	{
		if(pSelf->m_45degreestoggle && !pSelf->m_45degreestogglelastinput)
			Enable45Degrees();
		else if(!pSelf->m_45degreestoggle)
			Disable45Degrees();

		pSelf->m_45degreestogglelastinput = pSelf->m_45degreestoggle;
		return;
	}

	const bool TriggerToggle = HasStrokeArgument ? (pSelf->m_45degreestoggle && !pSelf->m_45degreestogglelastinput) : true;
	if(TriggerToggle)
	{
		if(pSelf->m_45degreesEnabled)
			Disable45Degrees();
		else
			Enable45Degrees();
	}

	pSelf->m_45degreestogglelastinput = pSelf->m_45degreestoggle;
}

void CBestClient::ConToggleSmallSens(IConsole::IResult *pResult, void *pUserData)
{
	CBestClient *pSelf = static_cast<CBestClient *>(pUserData);
	const bool HasStrokeArgument = pResult->NumArguments() > 0;
	pSelf->m_Smallsenstoggle = HasStrokeArgument ? (pResult->GetInteger(0) != 0) : 1;

	const auto EnableSmallSens = [&]() {
		if(pSelf->m_SmallsensEnabled)
			return;
		pSelf->m_SmallsensEnabled = 1;
		pSelf->GameClient()->Echo("[[green]] small sens on");
		g_Config.m_BcPrevInpMousesensSmallSens = (pSelf->m_45degreesEnabled == 1 ? g_Config.m_BcPrevInpMousesens45Degrees : g_Config.m_InpMousesens);
		g_Config.m_InpMousesens = 1;
	};

	const auto DisableSmallSens = [&]() {
		if(!pSelf->m_SmallsensEnabled)
			return;
		pSelf->m_SmallsensEnabled = 0;
		pSelf->GameClient()->Echo("[[red]] small sens off");
		g_Config.m_InpMousesens = g_Config.m_BcPrevInpMousesensSmallSens;
	};

	if(!g_Config.m_BcToggleSmallSens && HasStrokeArgument)
	{
		if(pSelf->m_Smallsenstoggle && !pSelf->m_Smallsenstogglelastinput)
			EnableSmallSens();
		else if(!pSelf->m_Smallsenstoggle)
			DisableSmallSens();

		pSelf->m_Smallsenstogglelastinput = pSelf->m_Smallsenstoggle;
		return;
	}

	const bool TriggerToggle = HasStrokeArgument ? (pSelf->m_Smallsenstoggle && !pSelf->m_Smallsenstogglelastinput) : true;
	if(TriggerToggle)
	{
		if(pSelf->m_SmallsensEnabled)
			DisableSmallSens();
		else
			EnableSmallSens();
	}

	pSelf->m_Smallsenstogglelastinput = pSelf->m_Smallsenstoggle;
}

void CBestClient::ConToggleDeepfly(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	CBestClient *pSelf = static_cast<CBestClient *>(pUserData);

	char aCurBind[128];
	str_copy(aCurBind, pSelf->GameClient()->m_Binds.Get(KEY_MOUSE_1, KeyModifier::NONE), sizeof(aCurBind));

	if(str_find_nocase(aCurBind, "+toggle cl_dummy_hammer"))
	{
		pSelf->GameClient()->Echo("[[red]] Deepfly off");
		if(str_length(pSelf->m_Oldmouse1Bind) > 1)
		{
			pSelf->GameClient()->m_Binds.Bind(KEY_MOUSE_1, pSelf->m_Oldmouse1Bind, false, KeyModifier::NONE);
		}
		else
		{
			pSelf->GameClient()->Echo("[[red]] No old bind in memory. Binding +fire");
			pSelf->GameClient()->m_Binds.Bind(KEY_MOUSE_1, "+fire", false, KeyModifier::NONE);
		}
	}
	else
	{
		pSelf->GameClient()->Echo("[[green]] Deepfly on");
		str_copy(pSelf->m_Oldmouse1Bind, aCurBind, sizeof(pSelf->m_Oldmouse1Bind));
		pSelf->GameClient()->m_Binds.Bind(KEY_MOUSE_1, "+fire; +toggle cl_dummy_hammer 1 0", false, KeyModifier::NONE);
	}
}

void CBestClient::ConToggleCinematicCamera(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	CBestClient *pSelf = static_cast<CBestClient *>(pUserData);
	g_Config.m_BcCinematicCamera = !g_Config.m_BcCinematicCamera;
	pSelf->GameClient()->Echo(g_Config.m_BcCinematicCamera ? "[[green]] Cinematic camera on" : "[[red]] Cinematic camera off");
}

void CBestClient::OnConsoleInit()
{
	Console()->Register("+BC_45_degrees", "", CFGFLAG_CLIENT, ConToggle45Degrees, this, "45 degrees bind");
	Console()->Register("BC_45_degrees", "", CFGFLAG_CLIENT, ConToggle45Degrees, this, "45 degrees bind (toggle)");
	Console()->Register("+BC_small_sens", "", CFGFLAG_CLIENT, ConToggleSmallSens, this, "Small sens bind");
	Console()->Register("BC_small_sens", "", CFGFLAG_CLIENT, ConToggleSmallSens, this, "Small sens bind (toggle)");
	Console()->Register("BC_deepfly_toggle", "", CFGFLAG_CLIENT, ConToggleDeepfly, this, "Deep fly toggle");
	Console()->Register("BC_cinematic_camera_toggle", "", CFGFLAG_CLIENT, ConToggleCinematicCamera, this, "Toggle cinematic spectator camera");
}
