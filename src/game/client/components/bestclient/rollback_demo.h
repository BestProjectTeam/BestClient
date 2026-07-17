/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_ROLLBACK_DEMO_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_ROLLBACK_DEMO_H

#include <engine/console.h>

#include <game/client/component.h>

class CRollbackDemo : public CComponent
{
	static void ConSaveRollback(IConsole::IResult *pResult, void *pUserData);

	void SaveRollback();

public:
	int Sizeof() const override { return sizeof(*this); }

	void OnConsoleInit() override;
};

#endif // GAME_CLIENT_COMPONENTS_BESTCLIENT_ROLLBACK_DEMO_H
