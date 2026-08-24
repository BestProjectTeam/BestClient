#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_PROCESS_PRIORITY_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_PROCESS_PRIORITY_H

#include <engine/console.h>

#include <game/client/component.h>

#include <atomic>

class CProcessPriority : public CComponent
{
	int Sizeof() const override { return sizeof(*this); }

	static void ConchainDDNetProcessPriority(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainDiscordProcessPriority(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void DiscordPriorityThread(void *pUserData);

	std::atomic<int64_t> m_DiscordPriorityDelay{0};
	std::atomic_bool m_DiscordPriorityThreadRunning{false};
	void *m_pDiscordPriorityThread = nullptr;

	void SetDiscordProcessesNormalPriority();

	void OnInit() override;
	void OnRender() override;
	void OnFocusChange(bool IsFocused) override;
	void OnConsoleInit() override;

public:
	void SetDDNetProcessPriority(bool Set);
	void StartDiscordPriorityThread();
};

#endif // GAME_CLIENT_COMPONENTS_BESTCLIENT_PROCESS_PRIORITY_H
