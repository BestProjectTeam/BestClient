/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_QUICK_BINDS_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_QUICK_BINDS_H

#include <engine/console.h>

#include <game/client/component.h>

class CQuickBinds : public CComponent
{
	static void ConToggle45Degrees(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleSmallSens(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleDeepfly(IConsole::IResult *pResult, void *pUserData);

	bool m_45DegreesStroke = false;
	bool m_45DegreesLastStroke = false;
	bool m_45DegreesEnabled = false;

	bool m_SmallSensStroke = false;
	bool m_SmallSensLastStroke = false;
	bool m_SmallSensEnabled = false;

	char m_aOldMouse1Bind[128] = {};

public:
	int Sizeof() const override { return sizeof(*this); }

	void OnConsoleInit() override;
};

#endif // GAME_CLIENT_COMPONENTS_BESTCLIENT_QUICK_BINDS_H
