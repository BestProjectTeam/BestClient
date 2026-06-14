// BestClient Updater — standalone Win32 GUI application.
// Replaces the PowerShell update script. Receives four positional arguments:
//   argv[1]  PID of the client process to wait for
//   argv[2]  Absolute path to the downloaded .zip archive
//   argv[3]  Install directory (directory containing DDNet.exe)
//   argv[4]  Absolute path to the client executable to relaunch
//
// Only compiled on Windows (see CMakeLists.txt).

#include <windows.h>
#include <wingdi.h>
#include <winuser.h>
#include <shellapi.h>
#include <stdlib.h>

#include <atomic>
#include <functional>
#include <string>

#pragma comment(lib, "shell32.lib")

// ─── Window ──────────────────────────────────────────────────────────────────

static const int WND_W = 480;
static const int WND_H = 215;

// BestClient logo-inspired theme: dark bg, green-to-orange gradient
static const COLORREF C_BG         = RGB(22,  22,  22);
static const COLORREF C_GREEN      = RGB(105, 190, 70);
static const COLORREF C_ORANGE     = RGB(230, 80,  45);
static const COLORREF C_TITLE      = RGB(235, 245, 232);
static const COLORREF C_DIM        = RGB(140, 155, 135);
static const COLORREF C_BAR_BG     = RGB(40,  40,  40);
static const COLORREF C_BAR_SHINE  = RGB(155, 220, 105);
static const COLORREF C_ERROR      = RGB(230, 75,  45);

// ─── Shared state ─────────────────────────────────────────────────────────────

static HWND              g_hWnd     = NULL;
static std::atomic<int>  g_Percent  = 0;
static bool              g_Failed   = false;
static CRITICAL_SECTION  g_Lock;
static wchar_t           g_aStatus[256] = L"Starting...";

#define WM_WORKER_TICK (WM_APP + 0)   // repaint
#define WM_WORKER_DONE (WM_APP + 1)   // close window

static void SetStatus(const wchar_t *pText)
{
	EnterCriticalSection(&g_Lock);
	wcsncpy_s(g_aStatus, pText, _TRUNCATE);
	LeaveCriticalSection(&g_Lock);
	if(g_hWnd)
		PostMessage(g_hWnd, WM_WORKER_TICK, 0, 0);
}

static void SetPercent(int Pct)
{
	if(Pct < 0) Pct = 0;
	if(Pct > 100) Pct = 100;
	g_Percent.store(Pct);
	if(g_hWnd)
		PostMessage(g_hWnd, WM_WORKER_TICK, 0, 0);
}

// ─── Helpers ──────────────────────────────────────────────────────────────────

// Run a process (no console window). If pLineCb is set, captures stdout+stderr
// and calls it for each complete line. Returns the process exit code, or -1 on error.
static int RunProcess(const wchar_t *pCmd, std::function<void(const wchar_t *)> LineCb = nullptr)
{
	HANDLE hRead = NULL, hWrite = NULL;
	if(LineCb)
	{
		SECURITY_ATTRIBUTES Sa = {sizeof(Sa), NULL, TRUE};
		if(!CreatePipe(&hRead, &hWrite, &Sa, 0))
			return -1;
		SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);
	}

	STARTUPINFOW Si = {};
	Si.cb = sizeof(Si);
	if(LineCb)
	{
		Si.dwFlags    = STARTF_USESTDHANDLES;
		Si.hStdOutput = hWrite;
		Si.hStdError  = hWrite;
	}

	PROCESS_INFORMATION Pi = {};
	std::wstring Cmd(pCmd);
	BOOL Ok = CreateProcessW(NULL, Cmd.data(), NULL, NULL,
		LineCb ? TRUE : FALSE, CREATE_NO_WINDOW, NULL, NULL, &Si, &Pi);

	if(hWrite) CloseHandle(hWrite);
	if(!Ok)
	{
		if(hRead) CloseHandle(hRead);
		return -1;
	}

	if(LineCb && hRead)
	{
		std::wstring Line;
		char Buf[512];
		DWORD Read;
		while(ReadFile(hRead, Buf, sizeof(Buf) - 1, &Read, NULL) && Read > 0)
		{
			Buf[Read] = '\0';
			for(DWORD i = 0; i < Read; ++i)
			{
				char Ch = Buf[i];
				if(Ch == '\n')
				{
					LineCb(Line.c_str());
					Line.clear();
				}
				else if(Ch != '\r')
					Line.push_back((wchar_t)(unsigned char)Ch);
			}
		}
		if(!Line.empty())
			LineCb(Line.c_str());
		CloseHandle(hRead);
	}

	WaitForSingleObject(Pi.hProcess, INFINITE);
	DWORD ExitCode = (DWORD)-1;
	GetExitCodeProcess(Pi.hProcess, &ExitCode);
	CloseHandle(Pi.hProcess);
	CloseHandle(Pi.hThread);
	return (int)ExitCode;
}

// Count entries in a zip archive using tar -tf.
static int CountArchiveEntries(const wchar_t *pArchive)
{
	wchar_t Cmd[1024];
	_snwprintf_s(Cmd, _TRUNCATE, L"tar.exe -tf \"%ls\"", pArchive);
	int N = 0;
	RunProcess(Cmd, [&](const wchar_t *) { ++N; });
	return N > 0 ? N : 1;
}

// Recursively count files (not dirs) under pDir.
static int CountFiles(const wchar_t *pDir)
{
	std::wstring Search(pDir);
	Search += L"\\*";
	WIN32_FIND_DATAW Fd;
	HANDLE h = FindFirstFileW(Search.c_str(), &Fd);
	if(h == INVALID_HANDLE_VALUE) return 0;
	int N = 0;
	do
	{
		if(!wcscmp(Fd.cFileName, L".") || !wcscmp(Fd.cFileName, L"..")) continue;
		if(Fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			std::wstring Sub(pDir); Sub += L"\\"; Sub += Fd.cFileName;
			N += CountFiles(Sub.c_str());
		}
		else ++N;
	} while(FindNextFileW(h, &Fd));
	FindClose(h);
	return N > 0 ? N : 1;
}

// Recursively copy pSrc into pDst (pDst is created if missing).
// pPerFile is called after each file copy (for progress tracking).
static void CopyTree(const wchar_t *pSrc, const wchar_t *pDst, std::function<void()> PerFile = nullptr)
{
	CreateDirectoryW(pDst, NULL);
	std::wstring Search(pSrc);
	Search += L"\\*";
	WIN32_FIND_DATAW Fd;
	HANDLE h = FindFirstFileW(Search.c_str(), &Fd);
	if(h == INVALID_HANDLE_VALUE) return;
	do
	{
		if(!wcscmp(Fd.cFileName, L".") || !wcscmp(Fd.cFileName, L"..")) continue;
		std::wstring Src(pSrc); Src += L"\\"; Src += Fd.cFileName;
		std::wstring Dst(pDst); Dst += L"\\"; Dst += Fd.cFileName;
		if(Fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			CopyTree(Src.c_str(), Dst.c_str(), PerFile);
		else
		{
			CopyFileW(Src.c_str(), Dst.c_str(), FALSE);
			if(PerFile) PerFile();
		}
	} while(FindNextFileW(h, &Fd));
	FindClose(h);
}

// Recursively delete a directory and all its contents.
static void DeleteTree(const wchar_t *pPath)
{
	std::wstring Search(pPath);
	Search += L"\\*";
	WIN32_FIND_DATAW Fd;
	HANDLE h = FindFirstFileW(Search.c_str(), &Fd);
	if(h != INVALID_HANDLE_VALUE)
	{
		do
		{
			if(!wcscmp(Fd.cFileName, L".") || !wcscmp(Fd.cFileName, L"..")) continue;
			std::wstring Full(pPath); Full += L"\\"; Full += Fd.cFileName;
			if(Fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				DeleteTree(Full.c_str());
			else
				DeleteFileW(Full.c_str());
		} while(FindNextFileW(h, &Fd));
		FindClose(h);
	}
	RemoveDirectoryW(pPath);
}

// ─── Worker thread ────────────────────────────────────────────────────────────

// Directories to preserve across updates (user-placed assets).
static const wchar_t *k_aUserDirs[] = {
	L"data\\assets\\arrow",
	L"data\\assets\\arrows",
	L"data\\assets\\audio",
	L"data\\audio",
};

struct WorkerArgs
{
	DWORD    Pid;
	wchar_t  aArchive[MAX_PATH];
	wchar_t  aInstallDir[MAX_PATH];
	wchar_t  aExePath[MAX_PATH];
};

static void Fail(const wchar_t *pMsg)
{
	g_Failed = true;
	SetStatus(pMsg);
	// Leave window open so user can read the error. ESC closes it.
}

static DWORD WINAPI WorkerThread(LPVOID pParam)
{
	auto *pA = (WorkerArgs *)pParam;

	// ── 1. Wait for the client to exit ────────────────────────────────────────
	SetStatus(L"Waiting for client to close...");
	SetPercent(2);
	{
		HANDLE hProc = OpenProcess(SYNCHRONIZE, FALSE, pA->Pid);
		if(hProc)
		{
			WaitForSingleObject(hProc, INFINITE);
			CloseHandle(hProc);
		}
		else
			Sleep(500); // PID already gone
	}
	SetPercent(8);

	// ── 2. Prepare extraction directory ──────────────────────────────────────
	wchar_t aExtract[MAX_PATH];
	_snwprintf_s(aExtract, _TRUNCATE, L"%ls\\update\\extract", pA->aInstallDir);
	DeleteTree(aExtract);
	if(!CreateDirectoryW(aExtract, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
	{
		Fail(L"Failed to create extraction directory.");
		delete pA; return 1;
	}

	// ── 3. Extract archive ───────────────────────────────────────────────────
	SetStatus(L"Extracting update...");
	{
		int Total = CountArchiveEntries(pA->aArchive);
		int Done  = 0;

		wchar_t Cmd[1024];
		_snwprintf_s(Cmd, _TRUNCATE, L"tar.exe -xvf \"%ls\" -C \"%ls\"", pA->aArchive, aExtract);
		int ExitCode = RunProcess(Cmd, [&](const wchar_t *)
		{
			++Done;
			int Pct = 10 + Done * 40 / Total;
			SetPercent(Pct < 50 ? Pct : 50);
		});

		if(ExitCode != 0)
		{
			Fail(L"Extraction failed. The archive may be corrupted.");
			delete pA; return 1;
		}
	}
	SetPercent(50);

	// ── 4. Resolve copy root (archive may wrap files in a single subfolder) ──
	wchar_t aCopyRoot[MAX_PATH];
	wcscpy_s(aCopyRoot, aExtract);
	{
		std::wstring Search(aExtract); Search += L"\\*";
		WIN32_FIND_DATAW Fd;
		HANDLE h = FindFirstFileW(Search.c_str(), &Fd);
		if(h != INVALID_HANDLE_VALUE)
		{
			int N = 0; wchar_t aFirst[MAX_PATH] = L"";
			do
			{
				if(!wcscmp(Fd.cFileName, L".") || !wcscmp(Fd.cFileName, L"..")) continue;
				++N;
				if(N == 1) wcscpy_s(aFirst, Fd.cFileName);
			} while(FindNextFileW(h, &Fd));
			FindClose(h);

			if(N == 1)
			{
				std::wstring Sub(aExtract); Sub += L"\\"; Sub += aFirst;
				if(GetFileAttributesW(Sub.c_str()) & FILE_ATTRIBUTE_DIRECTORY)
					wcscpy_s(aCopyRoot, Sub.c_str());
			}
		}
	}

	// ── 5. Backup user asset directories ─────────────────────────────────────
	SetStatus(L"Backing up settings...");
	wchar_t aBackup[MAX_PATH];
	_snwprintf_s(aBackup, _TRUNCATE, L"%ls\\update\\backup_%lu", pA->aInstallDir, pA->Pid);
	DeleteTree(aBackup);
	for(const wchar_t *pRel : k_aUserDirs)
	{
		wchar_t aSrc[MAX_PATH], aDst[MAX_PATH];
		_snwprintf_s(aSrc, _TRUNCATE, L"%ls\\%ls", pA->aInstallDir, pRel);
		_snwprintf_s(aDst, _TRUNCATE, L"%ls\\%ls", aBackup, pRel);
		if(GetFileAttributesW(aSrc) != INVALID_FILE_ATTRIBUTES)
			CopyTree(aSrc, aDst);
	}
	SetPercent(55);

	// ── 6. Copy new files into install directory ──────────────────────────────
	SetStatus(L"Installing files...");
	{
		int Total = CountFiles(aCopyRoot);
		int Done  = 0;
		CopyTree(aCopyRoot, pA->aInstallDir, [&]()
		{
			++Done;
			int Pct = 55 + Done * 35 / Total;
			SetPercent(Pct < 90 ? Pct : 90);
		});
	}
	SetPercent(90);

	// ── 7. Restore user assets ────────────────────────────────────────────────
	SetStatus(L"Restoring settings...");
	for(const wchar_t *pRel : k_aUserDirs)
	{
		wchar_t aSrc[MAX_PATH], aDst[MAX_PATH];
		_snwprintf_s(aSrc, _TRUNCATE, L"%ls\\%ls", aBackup, pRel);
		_snwprintf_s(aDst, _TRUNCATE, L"%ls\\%ls", pA->aInstallDir, pRel);
		if(GetFileAttributesW(aSrc) != INVALID_FILE_ATTRIBUTES)
			CopyTree(aSrc, aDst);
	}
	SetPercent(95);

	// ── 8. Clean up temp files ────────────────────────────────────────────────
	SetStatus(L"Cleaning up...");
	DeleteFileW(pA->aArchive);
	DeleteTree(aExtract);
	DeleteTree(aBackup);
	SetPercent(100);

	// ── 9. Launch client ──────────────────────────────────────────────────────
	SetStatus(L"Launching BestClient...");
	Sleep(500);

	SHELLEXECUTEINFOW Sei = {};
	Sei.cbSize    = sizeof(Sei);
	Sei.lpVerb    = L"open";
	Sei.lpFile    = pA->aExePath;
	Sei.lpDirectory = pA->aInstallDir;
	Sei.nShow     = SW_SHOWNORMAL;
	ShellExecuteExW(&Sei);

	Sleep(300);
	delete pA;

	if(g_hWnd)
		PostMessage(g_hWnd, WM_WORKER_DONE, 0, 0);
	return 0;
}

// ─── Painting ─────────────────────────────────────────────────────────────────

// Linear interpolation between two COLORREF values (t = 0.0 .. 1.0).
static COLORREF LerpColor(COLORREF A, COLORREF B, float T)
{
	int R = (int)(GetRValue(A) + T * (GetRValue(B) - GetRValue(A)));
	int G = (int)(GetGValue(A) + T * (GetGValue(B) - GetGValue(A)));
	int Bl = (int)(GetBValue(A) + T * (GetBValue(B) - GetBValue(A)));
	return RGB(R, G, Bl);
}

// Draw a left-to-right horizontal gradient from C1 to C2 inside Rc.
static void DrawGradientH(HDC Dc, RECT Rc, COLORREF C1, COLORREF C2)
{
	int W = Rc.right - Rc.left;
	if(W <= 0) return;
	for(int X = 0; X < W; ++X)
	{
		float T = (float)X / (W > 1 ? W - 1 : 1);
		HBRUSH Br = CreateSolidBrush(LerpColor(C1, C2, T));
		RECT Col = {Rc.left + X, Rc.top, Rc.left + X + 1, Rc.bottom};
		FillRect(Dc, &Col, Br);
		DeleteObject(Br);
	}
}

static HFONT MakeFont(int Size, bool Bold, const wchar_t *pFace = L"Segoe UI")
{
	return CreateFontW(Size, 0, 0, 0, Bold ? FW_BOLD : FW_NORMAL,
		FALSE, FALSE, FALSE, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
		DEFAULT_PITCH | FF_DONTCARE, pFace);
}

static void Paint(HWND hWnd)
{
	PAINTSTRUCT Ps;
	HDC Dc = BeginPaint(hWnd, &Ps);

	RECT Rc; GetClientRect(hWnd, &Rc);

	// Off-screen buffer
	HDC Mem = CreateCompatibleDC(Dc);
	HBITMAP Bmp = CreateCompatibleBitmap(Dc, Rc.right, Rc.bottom);
	SelectObject(Mem, Bmp);

	// Background
	{
		HBRUSH Br = CreateSolidBrush(C_BG);
		FillRect(Mem, &Rc, Br);
		DeleteObject(Br);
	}

	// Top accent bar — green-to-orange gradient (4 px)
	{
		RECT Bar = {0, 0, Rc.right, 4};
		DrawGradientH(Mem, Bar, C_GREEN, C_ORANGE);
	}

	SetBkMode(Mem, TRANSPARENT);

	// Title "BestClient"
	{
		HFONT F = MakeFont(32, true);
		HFONT Old = (HFONT)SelectObject(Mem, F);
		SetTextColor(Mem, C_TITLE);
		RECT R = {0, 10, Rc.right, 52};
		DrawTextW(Mem, L"BestClient", -1, &R, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
		SelectObject(Mem, Old);
		DeleteObject(F);
	}

	// "Updater" — same style as "BestClient", pulled up close
	{
		HFONT F = MakeFont(32, true);
		HFONT Old = (HFONT)SelectObject(Mem, F);
		SetTextColor(Mem, C_TITLE);
		RECT R = {0, 44, Rc.right, 86};
		DrawTextW(Mem, L"Updater", -1, &R, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
		SelectObject(Mem, Old);
		DeleteObject(F);
	}

	// Progress bar
	const int BarL  = 40;
	const int BarR  = Rc.right - 80;
	const int BarT  = 110;
	const int BarB  = 134;
	const int BarH  = BarB - BarT;

	// Bar background
	{
		RECT R = {BarL, BarT, BarR, BarB};
		HBRUSH Br = CreateSolidBrush(C_BAR_BG);
		FillRect(Mem, &R, Br);
		DeleteObject(Br);
	}

	// Bar fill — green-to-orange gradient matching the logo
	int Pct      = g_Percent.load();
	int FillW    = (BarR - BarL) * Pct / 100;
	if(FillW > 0)
	{
		// Gradient spans the visible fill but colour positions are relative to
		// the full bar width, so the hue advances as the bar grows.
		COLORREF FillEnd = LerpColor(C_GREEN, C_ORANGE, (float)Pct / 100.0f);
		RECT R = {BarL, BarT, BarL + FillW, BarB};
		if(g_Failed)
		{
			HBRUSH Br = CreateSolidBrush(C_ERROR);
			FillRect(Mem, &R, Br);
			DeleteObject(Br);
		}
		else
		{
			DrawGradientH(Mem, R, C_GREEN, FillEnd);
		}
	}

	// Percent label
	{
		HFONT F = MakeFont(13, true);
		HFONT Old = (HFONT)SelectObject(Mem, F);
		SetTextColor(Mem, g_Failed ? C_ERROR : C_TITLE);
		wchar_t aBuf[8];
		_snwprintf_s(aBuf, _TRUNCATE, L"%d%%", Pct);
		RECT R = {BarR + 6, BarT, Rc.right - 4, BarB};
		DrawTextW(Mem, aBuf, -1, &R, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
		SelectObject(Mem, Old);
		DeleteObject(F);
	}

	// Status text
	{
		HFONT F = MakeFont(13, false);
		HFONT Old = (HFONT)SelectObject(Mem, F);
		SetTextColor(Mem, g_Failed ? C_ERROR : C_DIM);
		EnterCriticalSection(&g_Lock);
		wchar_t aStatus[256];
		wcscpy_s(aStatus, g_aStatus);
		LeaveCriticalSection(&g_Lock);
		RECT R = {BarL, BarB + 10, Rc.right - BarL, BarB + 36};
		DrawTextW(Mem, aStatus, -1, &R, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
		SelectObject(Mem, Old);
		DeleteObject(F);
	}

	// Hint when failed
	if(g_Failed)
	{
		HFONT F = MakeFont(11, false);
		HFONT Old = (HFONT)SelectObject(Mem, F);
		SetTextColor(Mem, C_DIM);
		RECT R = {BarL, Rc.bottom - 22, Rc.right - BarL, Rc.bottom - 4};
		DrawTextW(Mem, L"Press Esc to close.", -1, &R, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
		SelectObject(Mem, Old);
		DeleteObject(F);
	}

	BitBlt(Dc, 0, 0, Rc.right, Rc.bottom, Mem, 0, 0, SRCCOPY);
	DeleteObject(Bmp);
	DeleteDC(Mem);
	EndPaint(hWnd, &Ps);
}

// ─── Window procedure ─────────────────────────────────────────────────────────

static LRESULT CALLBACK WndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch(Msg)
	{
	case WM_PAINT:        Paint(hWnd); return 0;
	case WM_ERASEBKGND:  return 1;
	case WM_WORKER_TICK: InvalidateRect(hWnd, NULL, FALSE); return 0;
	case WM_WORKER_DONE: DestroyWindow(hWnd); return 0;
	case WM_DESTROY:     PostQuitMessage(0); return 0;
	// Allow dragging the borderless window from anywhere
	case WM_NCHITTEST:
		if(DefWindowProcW(hWnd, Msg, wParam, lParam) == HTCLIENT)
			return HTCAPTION;
		return DefWindowProcW(hWnd, Msg, wParam, lParam);
	case WM_KEYDOWN:
		if(wParam == VK_ESCAPE) DestroyWindow(hWnd);
		return 0;
	}
	return DefWindowProcW(hWnd, Msg, wParam, lParam);
}

// ─── Entry point ──────────────────────────────────────────────────────────────

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
	InitializeCriticalSection(&g_Lock);

	// Parse: argv[0]=exe, [1]=pid, [2]=archive, [3]=install_dir, [4]=exe_path
	int Argc = 0;
	LPWSTR *ppArgv = CommandLineToArgvW(GetCommandLineW(), &Argc);
	if(Argc < 5 || !ppArgv)
	{
		MessageBoxW(NULL,
			L"Usage: bestclient-updater.exe <pid> <archive> <install_dir> <exe>",
			L"BestClient Updater", MB_ICONERROR);
		return 1;
	}

	auto *pArgs      = new WorkerArgs();
	pArgs->Pid       = (DWORD)_wtol(ppArgv[1]);
	wcscpy_s(pArgs->aArchive,    ppArgv[2]);
	wcscpy_s(pArgs->aInstallDir, ppArgv[3]);
	wcscpy_s(pArgs->aExePath,    ppArgv[4]);
	LocalFree(ppArgv);

	// Register window class
	WNDCLASSEXW Wc      = {};
	Wc.cbSize           = sizeof(Wc);
	Wc.lpfnWndProc      = WndProc;
	Wc.hInstance        = hInst;
	Wc.hCursor          = LoadCursor(NULL, IDC_ARROW);
	Wc.hbrBackground    = NULL;
	Wc.lpszClassName    = L"BCUpdater";
	Wc.hIcon            = LoadIcon(NULL, IDI_APPLICATION);
	RegisterClassExW(&Wc);

	int X = (GetSystemMetrics(SM_CXSCREEN) - WND_W) / 2;
	int Y = (GetSystemMetrics(SM_CYSCREEN) - WND_H) / 2;

	g_hWnd = CreateWindowExW(
		WS_EX_APPWINDOW,
		L"BCUpdater",
		L"BestClient Updater",
		WS_POPUP | WS_VISIBLE,
		X, Y, WND_W, WND_H,
		NULL, NULL, hInst, NULL);

	if(!g_hWnd) return 1;

	// Start worker
	HANDLE hThread = CreateThread(NULL, 0, WorkerThread, pArgs, 0, NULL);
	if(hThread) CloseHandle(hThread);

	MSG Msg;
	while(GetMessageW(&Msg, NULL, 0, 0))
	{
		TranslateMessage(&Msg);
		DispatchMessageW(&Msg);
	}

	DeleteCriticalSection(&g_Lock);
	return 0;
}
