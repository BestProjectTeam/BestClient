/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_FAST_ACTIONS_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_FAST_ACTIONS_H

#include <engine/console.h>

#include <game/client/component.h>

#include <vector>

class IConfigManager;

enum
{
	FAST_ACTIONS_MAX_NAME = 64,
	FAST_ACTIONS_MAX_CMD = 1024,
	FAST_ACTIONS_FIXED_SLOTS = 6
};

class CFastActions : public CComponent
{
	float m_AnimationTime = 0.0f;

	bool m_Active = false;

	int m_SelectedBind = -1;
	int m_DisplayBind = -1;

	static void ConOpenFa(IConsole::IResult *pResult, void *pUserData);
	static void ConAddFaLegacy(IConsole::IResult *pResult, void *pUserData);
	static void ConAddFa(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveFa(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveAllFaBinds(IConsole::IResult *pResult, void *pUserData);
	static void ConFaExecuteHover(IConsole::IResult *pResult, void *pUserData);

	static void ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData);

public:
	class CBind
	{
	public:
		char m_aName[FAST_ACTIONS_MAX_NAME] = "";
		char m_aCommand[FAST_ACTIONS_MAX_CMD] = "";
	};

	std::vector<CBind> m_vBinds;

	CFastActions();
	int Sizeof() const override { return sizeof(*this); }

	void OnReset() override;
	void OnRender() override;
	void OnConsoleInit() override;
	void OnRelease() override;
	bool OnInput(const IInput::CEvent &Event) override;

	void AddBind(const char *pName, const char *pCommand);
	void RemoveBind(const char *pName, const char *pCommand);
	void RemoveBind(int Index);
	void RemoveAllBinds();

	void ExecuteHoveredBind();
	void ExecuteBind(int Bind);

	bool IsActive() const { return m_Active; }
};

#endif
