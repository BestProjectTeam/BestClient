#include "crash_reporter.h"

#include <base/log.h>
#include <base/math.h>
#include <base/str.h>
#include <base/system.h>

#include <cstdint>
#include <cstdlib>
#include <string>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>

namespace
{
constexpr const char *CRASH_REPORTER_MONITOR_ARG = "--crashreport-monitor";
constexpr int CRASH_REPORTER_MARGIN = 12;
constexpr int CRASH_REPORTER_BUTTON_WIDTH = 110;
constexpr int CRASH_REPORTER_BUTTON_HEIGHT = 30;

enum
{
	CRASH_REPORTER_CONTROL_PATH = 1001,
	CRASH_REPORTER_CONTROL_TEXT = 1002,
	CRASH_REPORTER_CONTROL_COPY = 1003,
	CRASH_REPORTER_CONTROL_CLOSE = 1004,
};

struct SCrashReporterWindow
{
	std::wstring m_PathLabel;
	std::wstring m_LogText;
	HWND m_hPath = nullptr;
	HWND m_hText = nullptr;
	HWND m_hCopy = nullptr;
	HWND m_hClose = nullptr;
	HFONT m_hFont = nullptr;
};

char gs_aCrashLogPath[IO_MAX_PATH_LENGTH];
LPTOP_LEVEL_EXCEPTION_FILTER gs_pPreviousExceptionFilter = nullptr;

const char *ExceptionCodeToString(DWORD ExceptionCode)
{
	switch(ExceptionCode)
	{
	case EXCEPTION_ACCESS_VIOLATION:
		return "EXCEPTION_ACCESS_VIOLATION";
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
		return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
	case EXCEPTION_BREAKPOINT:
		return "EXCEPTION_BREAKPOINT";
	case EXCEPTION_DATATYPE_MISALIGNMENT:
		return "EXCEPTION_DATATYPE_MISALIGNMENT";
	case EXCEPTION_FLT_DIVIDE_BY_ZERO:
		return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
	case EXCEPTION_ILLEGAL_INSTRUCTION:
		return "EXCEPTION_ILLEGAL_INSTRUCTION";
	case EXCEPTION_IN_PAGE_ERROR:
		return "EXCEPTION_IN_PAGE_ERROR";
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
		return "EXCEPTION_INT_DIVIDE_BY_ZERO";
	case EXCEPTION_PRIV_INSTRUCTION:
		return "EXCEPTION_PRIV_INSTRUCTION";
	case EXCEPTION_STACK_OVERFLOW:
		return "EXCEPTION_STACK_OVERFLOW";
	default:
		return "UNKNOWN_EXCEPTION";
	}
}

void WriteCrashLogLine(IOHANDLE File, const char *pLine)
{
	io_write(File, pLine, str_length(pLine));
	io_write_newline(File);
}

void WriteCrashLog(EXCEPTION_POINTERS *pExceptionPointers)
{
	if(gs_aCrashLogPath[0] == '\0')
	{
		return;
	}

	IOHANDLE File = io_open(gs_aCrashLogPath, IOFLAG_WRITE);
	if(File == nullptr)
	{
		return;
	}

	char aTimestamp[64];
	char aLine[512];
	str_timestamp(aTimestamp, sizeof(aTimestamp));

	WriteCrashLogLine(File, "BestClient crash report");
	WriteCrashLogLine(File, "======================");

	str_format(aLine, sizeof(aLine), "Timestamp: %s", aTimestamp);
	WriteCrashLogLine(File, aLine);
	str_format(aLine, sizeof(aLine), "Process ID: %d", pid());
	WriteCrashLogLine(File, aLine);
	str_format(aLine, sizeof(aLine), "Thread ID: %lu", (unsigned long)GetCurrentThreadId());
	WriteCrashLogLine(File, aLine);

	const DWORD ExceptionCode = pExceptionPointers != nullptr && pExceptionPointers->ExceptionRecord != nullptr ? pExceptionPointers->ExceptionRecord->ExceptionCode : 0;
	const void *pAddress = pExceptionPointers != nullptr && pExceptionPointers->ExceptionRecord != nullptr ? pExceptionPointers->ExceptionRecord->ExceptionAddress : nullptr;
	str_format(aLine, sizeof(aLine), "Exception: %s (0x%08lx)", ExceptionCodeToString(ExceptionCode), (unsigned long)ExceptionCode);
	WriteCrashLogLine(File, aLine);
	str_format(aLine, sizeof(aLine), "Address: %p", pAddress);
	WriteCrashLogLine(File, aLine);

	if(pExceptionPointers != nullptr && pExceptionPointers->ContextRecord != nullptr)
	{
#if defined(CONF_ARCH_AMD64)
		const CONTEXT &Context = *pExceptionPointers->ContextRecord;
		WriteCrashLogLine(File, "");
		WriteCrashLogLine(File, "Registers:");
		str_format(aLine, sizeof(aLine), "RIP=0x%016llx RSP=0x%016llx RBP=0x%016llx", (unsigned long long)Context.Rip, (unsigned long long)Context.Rsp, (unsigned long long)Context.Rbp);
		WriteCrashLogLine(File, aLine);
		str_format(aLine, sizeof(aLine), "RAX=0x%016llx RBX=0x%016llx RCX=0x%016llx", (unsigned long long)Context.Rax, (unsigned long long)Context.Rbx, (unsigned long long)Context.Rcx);
		WriteCrashLogLine(File, aLine);
		str_format(aLine, sizeof(aLine), "RDX=0x%016llx RSI=0x%016llx RDI=0x%016llx", (unsigned long long)Context.Rdx, (unsigned long long)Context.Rsi, (unsigned long long)Context.Rdi);
		WriteCrashLogLine(File, aLine);
#elif defined(CONF_ARCH_IA32)
		const CONTEXT &Context = *pExceptionPointers->ContextRecord;
		WriteCrashLogLine(File, "");
		WriteCrashLogLine(File, "Registers:");
		str_format(aLine, sizeof(aLine), "EIP=0x%08lx ESP=0x%08lx EBP=0x%08lx", (unsigned long)Context.Eip, (unsigned long)Context.Esp, (unsigned long)Context.Ebp);
		WriteCrashLogLine(File, aLine);
		str_format(aLine, sizeof(aLine), "EAX=0x%08lx EBX=0x%08lx ECX=0x%08lx", (unsigned long)Context.Eax, (unsigned long)Context.Ebx, (unsigned long)Context.Ecx);
		WriteCrashLogLine(File, aLine);
		str_format(aLine, sizeof(aLine), "EDX=0x%08lx ESI=0x%08lx EDI=0x%08lx", (unsigned long)Context.Edx, (unsigned long)Context.Esi, (unsigned long)Context.Edi);
		WriteCrashLogLine(File, aLine);
#endif
	}

	WriteCrashLogLine(File, "");
	WriteCrashLogLine(File, "Backtrace:");
	void *apFrames[32];
	const unsigned short NumFrames = CaptureStackBackTrace(0, 32, apFrames, nullptr);
	for(unsigned short i = 0; i < NumFrames; ++i)
	{
		str_format(aLine, sizeof(aLine), "#%u %p", i, apFrames[i]);
		WriteCrashLogLine(File, aLine);
	}

	io_close(File);
}

LONG WINAPI CrashReporterUnhandledExceptionFilter(EXCEPTION_POINTERS *pExceptionPointers)
{
	WriteCrashLog(pExceptionPointers);
	if(gs_pPreviousExceptionFilter != nullptr)
	{
		return gs_pPreviousExceptionFilter(pExceptionPointers);
	}
	return EXCEPTION_EXECUTE_HANDLER;
}

bool ReadCrashLog(const char *pPath, std::string &Log)
{
	IOHANDLE File = io_open(pPath, IOFLAG_READ);
	if(File == nullptr)
	{
		return false;
	}

	char *pContent = io_read_all_str(File);
	io_close(File);
	if(pContent == nullptr)
	{
		return false;
	}

	Log.assign(pContent);
	free(pContent);
	return !Log.empty();
}

void CopyCrashLogToClipboard(HWND hText)
{
	SendMessageW(hText, EM_SETSEL, 0, -1);
	SendMessageW(hText, WM_COPY, 0, 0);
	SendMessageW(hText, EM_SETSEL, -1, -1);
}

void ResizeCrashReporterControls(HWND hWnd, SCrashReporterWindow *pWindow)
{
	if(pWindow == nullptr)
	{
		return;
	}

	RECT ClientRect{};
	GetClientRect(hWnd, &ClientRect);

	const int Width = ClientRect.right - ClientRect.left;
	const int Height = ClientRect.bottom - ClientRect.top;
	const int PathHeight = 42;
	const int ButtonsTop = Height - CRASH_REPORTER_MARGIN - CRASH_REPORTER_BUTTON_HEIGHT;
	const int TextTop = CRASH_REPORTER_MARGIN + PathHeight + 8;
	const int TextHeight = maximum(80, ButtonsTop - TextTop - 8);

	MoveWindow(pWindow->m_hPath, CRASH_REPORTER_MARGIN, CRASH_REPORTER_MARGIN, Width - CRASH_REPORTER_MARGIN * 2, PathHeight, TRUE);
	MoveWindow(pWindow->m_hText, CRASH_REPORTER_MARGIN, TextTop, Width - CRASH_REPORTER_MARGIN * 2, TextHeight, TRUE);
	MoveWindow(pWindow->m_hClose, Width - CRASH_REPORTER_MARGIN - CRASH_REPORTER_BUTTON_WIDTH, ButtonsTop, CRASH_REPORTER_BUTTON_WIDTH, CRASH_REPORTER_BUTTON_HEIGHT, TRUE);
	MoveWindow(pWindow->m_hCopy, Width - CRASH_REPORTER_MARGIN * 2 - CRASH_REPORTER_BUTTON_WIDTH * 2, ButtonsTop, CRASH_REPORTER_BUTTON_WIDTH, CRASH_REPORTER_BUTTON_HEIGHT, TRUE);
}

LRESULT CALLBACK CrashReporterWindowProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	SCrashReporterWindow *pWindow = reinterpret_cast<SCrashReporterWindow *>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

	switch(Msg)
	{
	case WM_NCCREATE:
	{
		const CREATESTRUCTW *pCreate = reinterpret_cast<const CREATESTRUCTW *>(lParam);
		SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreate->lpCreateParams));
		return TRUE;
	}
	case WM_CREATE:
	{
		pWindow = reinterpret_cast<SCrashReporterWindow *>(reinterpret_cast<CREATESTRUCTW *>(lParam)->lpCreateParams);
		pWindow->m_hPath = CreateWindowExW(
			0, L"STATIC", pWindow->m_PathLabel.c_str(),
			WS_CHILD | WS_VISIBLE,
			0, 0, 0, 0,
			hWnd, reinterpret_cast<HMENU>(CRASH_REPORTER_CONTROL_PATH), GetModuleHandleW(nullptr), nullptr);
		pWindow->m_hText = CreateWindowExW(
			WS_EX_CLIENTEDGE, L"EDIT", L"",
			WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY,
			0, 0, 0, 0,
			hWnd, reinterpret_cast<HMENU>(CRASH_REPORTER_CONTROL_TEXT), GetModuleHandleW(nullptr), nullptr);
		pWindow->m_hCopy = CreateWindowExW(
			0, L"BUTTON", L"Copy",
			WS_CHILD | WS_VISIBLE | WS_TABSTOP,
			0, 0, 0, 0,
			hWnd, reinterpret_cast<HMENU>(CRASH_REPORTER_CONTROL_COPY), GetModuleHandleW(nullptr), nullptr);
		pWindow->m_hClose = CreateWindowExW(
			0, L"BUTTON", L"Close",
			WS_CHILD | WS_VISIBLE | WS_TABSTOP,
			0, 0, 0, 0,
			hWnd, reinterpret_cast<HMENU>(CRASH_REPORTER_CONTROL_CLOSE), GetModuleHandleW(nullptr), nullptr);

		pWindow->m_hFont = CreateFontW(
			-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
			FIXED_PITCH | FF_MODERN, L"Consolas");
		SendMessageW(pWindow->m_hText, EM_SETLIMITTEXT, 0, 0);
		SetWindowTextW(pWindow->m_hText, pWindow->m_LogText.c_str());
		if(pWindow->m_hFont != nullptr)
		{
			SendMessageW(pWindow->m_hPath, WM_SETFONT, reinterpret_cast<WPARAM>(pWindow->m_hFont), TRUE);
			SendMessageW(pWindow->m_hText, WM_SETFONT, reinterpret_cast<WPARAM>(pWindow->m_hFont), TRUE);
			SendMessageW(pWindow->m_hCopy, WM_SETFONT, reinterpret_cast<WPARAM>(pWindow->m_hFont), TRUE);
			SendMessageW(pWindow->m_hClose, WM_SETFONT, reinterpret_cast<WPARAM>(pWindow->m_hFont), TRUE);
		}

		ResizeCrashReporterControls(hWnd, pWindow);
		return 0;
	}
	case WM_SIZE:
		ResizeCrashReporterControls(hWnd, pWindow);
		return 0;
	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case CRASH_REPORTER_CONTROL_COPY:
			if(pWindow != nullptr && pWindow->m_hText != nullptr)
			{
				CopyCrashLogToClipboard(pWindow->m_hText);
			}
			return 0;
		case CRASH_REPORTER_CONTROL_CLOSE:
			DestroyWindow(hWnd);
			return 0;
		}
		break;
	case WM_CLOSE:
		DestroyWindow(hWnd);
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProcW(hWnd, Msg, wParam, lParam);
}

void ShowCrashReporterWindow(const char *pPath, const char *pLogText)
{
	static constexpr wchar_t WINDOW_CLASS_NAME[] = L"BestClientCrashReporterWindow";

	WNDCLASSEXW WindowClass{};
	WindowClass.cbSize = sizeof(WindowClass);
	WindowClass.lpfnWndProc = CrashReporterWindowProc;
	WindowClass.hInstance = GetModuleHandleW(nullptr);
	WindowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	WindowClass.hIcon = LoadIconW(nullptr, IDI_ERROR);
	WindowClass.hIconSm = LoadIconW(nullptr, IDI_ERROR);
	WindowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
	WindowClass.lpszClassName = WINDOW_CLASS_NAME;
	RegisterClassExW(&WindowClass);

	SCrashReporterWindow WindowData;
	WindowData.m_PathLabel = windows_utf8_to_wide("Crash log: ");
	WindowData.m_PathLabel += windows_utf8_to_wide(pPath);
	WindowData.m_LogText = windows_utf8_to_wide(pLogText);

	HWND hWnd = CreateWindowExW(
		WS_EX_APPWINDOW, WINDOW_CLASS_NAME, L"BestClient Crash Log",
		WS_OVERLAPPEDWINDOW | WS_VISIBLE,
		CW_USEDEFAULT, CW_USEDEFAULT, 900, 700,
		nullptr, nullptr, GetModuleHandleW(nullptr), &WindowData);
	if(hWnd == nullptr)
	{
		return;
	}

	ShowWindow(hWnd, SW_SHOW);
	UpdateWindow(hWnd);

	MSG Msg;
	while(GetMessageW(&Msg, nullptr, 0, 0) > 0)
	{
		TranslateMessage(&Msg);
		DispatchMessageW(&Msg);
	}

	if(WindowData.m_hFont != nullptr)
	{
		DeleteObject(WindowData.m_hFont);
	}
}

DWORD WaitForMonitoredProcess(const int ProcessId)
{
	if(ProcessId <= 0)
	{
		return 1;
	}

	HANDLE hProcess = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, ProcessId);
	if(hProcess == nullptr)
	{
		return 1;
	}

	WaitForSingleObject(hProcess, INFINITE);

	DWORD ExitCode = 1;
	if(!GetExitCodeProcess(hProcess, &ExitCode))
	{
		ExitCode = 1;
	}
	CloseHandle(hProcess);
	return ExitCode;
}
}
#endif

void InitializeCrashReporter(const char *pCrashLogPath)
{
#if defined(CONF_FAMILY_WINDOWS)
	str_copy(gs_aCrashLogPath, pCrashLogPath, sizeof(gs_aCrashLogPath));
	gs_pPreviousExceptionFilter = SetUnhandledExceptionFilter(CrashReporterUnhandledExceptionFilter);
#else
	(void)pCrashLogPath;
#endif
}

bool HandleCrashReporterCommandLine(int argc, const char **argv)
{
#if defined(CONF_FAMILY_WINDOWS)
	for(int i = 1; i < argc; ++i)
	{
		if(str_comp(argv[i], CRASH_REPORTER_MONITOR_ARG) != 0)
		{
			continue;
		}

		if(i + 2 >= argc)
		{
			return true;
		}

		const int ProcessId = str_toint(argv[i + 1]);
		const char *pCrashLogPath = argv[i + 2];
		const DWORD ExitCode = WaitForMonitoredProcess(ProcessId);
		if(ExitCode == 0)
		{
			return true;
		}

		std::string CrashLog;
		if(!ReadCrashLog(pCrashLogPath, CrashLog))
		{
			char aFallback[1024];
			str_format(aFallback, sizeof(aFallback),
				"The client terminated unexpectedly, but the crash log file was not created.\n\n"
				"Exit code: 0x%08lx\n"
				"Expected crash log: %s",
				(unsigned long)ExitCode, pCrashLogPath);
			ShowCrashReporterWindow(pCrashLogPath, aFallback);
			return true;
		}

		ShowCrashReporterWindow(pCrashLogPath, CrashLog.c_str());
		return true;
	}
#else
	(void)argc;
	(void)argv;
#endif

	return false;
}

void LaunchCrashReporterMonitor(const char *pExecutablePath, const char *pCrashLogPath)
{
#if defined(CONF_FAMILY_WINDOWS)
	char aPid[32];
	str_format(aPid, sizeof(aPid), "%d", pid());
	const char *apArguments[] = {CRASH_REPORTER_MONITOR_ARG, aPid, pCrashLogPath};
	if(shell_execute(pExecutablePath, EShellExecuteWindowState::HIDDEN, apArguments, 3) == INVALID_PROCESS)
	{
		log_warn("crashreport", "failed to launch crash reporter monitor");
	}
#else
	(void)pExecutablePath;
	(void)pCrashLogPath;
#endif
}

[[noreturn]] void TriggerTestCrash()
{
	volatile int *pCrash = nullptr;
	*pCrash = 0;
	std::abort();
}
