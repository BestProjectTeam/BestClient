#include "process_priority.h"

#include <base/log.h>
#include <base/system.h>
#include <base/thread.h>

#include <engine/shared/config.h>

#if defined(CONF_FAMILY_WINDOWS)
#include <Windows.h>
#include <TlHelp32.h>
#include <processthreadsapi.h>

static bool IsDiscordProcessName(const wchar_t *pProcessName)
{
	return _wcsicmp(pProcessName, L"Discord.exe") == 0 ||
	       _wcsicmp(pProcessName, L"DiscordCanary.exe") == 0 ||
	       _wcsicmp(pProcessName, L"DiscordPTB.exe") == 0 ||
	       _wcsicmp(pProcessName, L"DiscordSystemHelper.exe") == 0;
}
#endif

void CProcessPriority::SetDDNetProcessPriority(bool Set)
{
#if defined(CONF_FAMILY_WINDOWS)
	if(!SetPriorityClass(GetCurrentProcess(), Set ? HIGH_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS))
	{
		log_info("bestclient", Set ? "Failed to set process priority" : "Failed to reset process priority");
		return;
	}
	if(!SetThreadPriority(GetCurrentThread(), Set ? THREAD_PRIORITY_HIGHEST : THREAD_PRIORITY_NORMAL))
	{
		log_info("bestclient", Set ? "Failed to set thread priority" : "Failed to reset thread priority");
		return;
	}
#endif
}

void CProcessPriority::DiscordPriorityThread(void *pUserData)
{
	CProcessPriority *pSelf = (CProcessPriority *)pUserData;
	pSelf->SetDiscordProcessesNormalPriority();
	pSelf->m_DiscordPriorityDelay.store(time_get() + time_freq() * 30);
	pSelf->m_DiscordPriorityThreadRunning.store(false);
}

void CProcessPriority::StartDiscordPriorityThread()
{
	// Don't start the thread if it's already running
	if(m_DiscordPriorityThreadRunning.load())
		return;

	if(m_pDiscordPriorityThread)
	{
		thread_wait(m_pDiscordPriorityThread);
		m_pDiscordPriorityThread = nullptr;
	}

	m_DiscordPriorityDelay.store(0);
	m_DiscordPriorityThreadRunning.store(true);
	m_pDiscordPriorityThread = thread_init(DiscordPriorityThread, this, "discord-priority");
}

void CProcessPriority::SetDiscordProcessesNormalPriority()
{
#if defined(CONF_FAMILY_WINDOWS)
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if(hSnapshot == INVALID_HANDLE_VALUE)
	{
		log_info("bestclient", "Failed to create process snapshot");
		return;
	}

	PROCESSENTRY32 Entry;
	mem_zero(&Entry, sizeof(Entry));
	Entry.dwSize = sizeof(Entry);

	int Changed = 0;
	int Failed = 0;

	if(Process32First(hSnapshot, &Entry))
	{
		do
		{
			if(!IsDiscordProcessName(Entry.szExeFile))
				continue;

			HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, Entry.th32ProcessID);
			if(!hProcess)
			{
				Failed++;
				continue;
			}

			if(SetPriorityClass(hProcess, NORMAL_PRIORITY_CLASS))
				Changed++;
			else
				Failed++;

			CloseHandle(hProcess);
		} while(Process32Next(hSnapshot, &Entry));
	}

	CloseHandle(hSnapshot);
	(void)Changed;
	(void)Failed;
#endif
}

void CProcessPriority::OnInit()
{
	SetDDNetProcessPriority(g_Config.m_BcHighProcessPriority);
	if(g_Config.m_BcDiscordNormalProcessPriority)
		StartDiscordPriorityThread();
}

void CProcessPriority::OnRender()
{
	const int64_t DiscordPriorityDelay = m_DiscordPriorityDelay.load();
	if(g_Config.m_BcDiscordNormalProcessPriority && !m_DiscordPriorityThreadRunning.load() && DiscordPriorityDelay < time_get())
	{
		StartDiscordPriorityThread();
	}
}

void CProcessPriority::OnFocusChange(bool IsFocused)
{
	(void)IsFocused;
	SetDDNetProcessPriority(g_Config.m_BcHighProcessPriority);
}

void CProcessPriority::OnConsoleInit()
{
	Console()->Chain("bc_high_process_priority", ConchainDDNetProcessPriority, this);
	Console()->Chain("bc_discord_normal_process_priority", ConchainDiscordProcessPriority, this);
}

void CProcessPriority::ConchainDDNetProcessPriority(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	CProcessPriority *pSelf = (CProcessPriority *)pUserData;
	if(pResult->NumArguments())
	{
		bool Value = pResult->GetInteger(0) != 0;
		pSelf->SetDDNetProcessPriority(Value);
	}
}

void CProcessPriority::ConchainDiscordProcessPriority(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	CProcessPriority *pSelf = (CProcessPriority *)pUserData;
	if(pResult->NumArguments())
	{
		if(pResult->GetInteger(0) != 0)
			pSelf->StartDiscordPriorityThread();
	}
}
