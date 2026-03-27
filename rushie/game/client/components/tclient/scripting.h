#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_SCRIPTING_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_SCRIPTING_H

#include <engine/console.h>

#include <game/client/component.h>

class CScripting : public CComponent
{
private:
	static void ConExecScript(IConsole::IResult *pResult, void *pUserData);

public:
	void ExecScript(const char *pFilename, const char *pArgs);
	void OnConsoleInit() override;
	int Sizeof() const override { return sizeof(*this); }
};

#endif
