#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_BESTCLIENT_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_BESTCLIENT_H

#include <engine/shared/console.h>

#include <game/client/component.h>

class CBestClient : public CComponent
{
	static void ConToggle45Degrees(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleSmallSens(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleDeepfly(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleCinematicCamera(IConsole::IResult *pResult, void *pUserData);

	int m_45degreestoggle = 0;
	int m_45degreestogglelastinput = 0;
	int m_45degreesEnabled = 0;
	int m_Smallsenstoggle = 0;
	int m_Smallsenstogglelastinput = 0;
	int m_SmallsensEnabled = 0;
	char m_Oldmouse1Bind[128] = {};

public:
	CBestClient();
	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
};

#endif
