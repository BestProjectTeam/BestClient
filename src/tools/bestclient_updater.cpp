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
#include <stdint.h>
#include <stdlib.h>

#include <zlib.h>

#include <atomic>
#include <functional>
#include <string>
#include <vector>

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

// ─── ZIP extraction (in-process) ─────────────────────────────────────────────
//
// The archive is unpacked with zlib inside this process rather than by shelling out to
// tar.exe. Spawning a hidden LOLBIN to stage files that are then copied over the running
// application's own executables is the textbook dropper sequence, and antivirus behaviour
// monitors score it accordingly. Doing the inflate ourselves keeps the whole update inside
// one binary with no child processes at all.

namespace zip
{
// Little-endian scalar reads from the in-memory archive.
static uint16_t Read16(const unsigned char *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t Read32(const unsigned char *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static const uint32_t SIG_EOCD = 0x06054b50;
static const uint32_t SIG_CDIR = 0x02014b50;
static const uint32_t SIG_LOCAL = 0x04034b50;

// Hard caps: a release archive is a few hundred MiB at most and holds a few thousand
// files. Anything past these is treated as malformed rather than trusted.
static const size_t MAX_ARCHIVE_BYTES = 512u * 1024u * 1024u;
static const uint64_t MAX_ENTRY_BYTES = 512ull * 1024ull * 1024ull;
static const size_t MAX_ENTRIES = 100000;

struct SEntry
{
	std::string m_Name; // archive path, forward slashes
	uint64_t m_CompressedSize = 0;
	uint64_t m_UncompressedSize = 0;
	uint16_t m_Method = 0;
	uint64_t m_LocalOffset = 0;
};

// Reject anything that could write outside the extraction root: absolute paths, drive
// letters, and any ".." component. The archive comes from our own release pipeline, but it
// arrives over the network and is unpacked with the user's privileges, so it gets checked.
static bool IsSafeEntryName(const std::string &Name)
{
	if(Name.empty() || Name.size() > 512)
		return false;
	if(Name[0] == '/')
		return false;
	if(Name.size() >= 2 && Name[1] == ':')
		return false;

	size_t Begin = 0;
	while(Begin < Name.size())
	{
		size_t End = Name.find('/', Begin);
		if(End == std::string::npos)
			End = Name.size();
		if(Name.compare(Begin, End - Begin, "..") == 0)
			return false;
		Begin = End + 1;
	}
	return true;
}

static std::wstring Widen(const std::string &Utf8)
{
	if(Utf8.empty())
		return std::wstring();
	const int Need = MultiByteToWideChar(CP_UTF8, 0, Utf8.c_str(), (int)Utf8.size(), NULL, 0);
	if(Need <= 0)
		return std::wstring();
	std::wstring Out((size_t)Need, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, Utf8.c_str(), (int)Utf8.size(), Out.data(), Need);
	return Out;
}

// Read the whole archive into memory. Release archives are small enough that streaming
// would only add complexity.
static bool ReadFileBytes(const wchar_t *pPath, std::vector<unsigned char> &vOut)
{
	const HANDLE hFile = CreateFileW(pPath, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hFile == INVALID_HANDLE_VALUE)
		return false;

	LARGE_INTEGER Size = {};
	if(!GetFileSizeEx(hFile, &Size) || Size.QuadPart <= 0 || (uint64_t)Size.QuadPart > MAX_ARCHIVE_BYTES)
	{
		CloseHandle(hFile);
		return false;
	}

	vOut.resize((size_t)Size.QuadPart);
	size_t Done = 0;
	while(Done < vOut.size())
	{
		const DWORD Chunk = (DWORD)((vOut.size() - Done) > 0x100000 ? 0x100000 : (vOut.size() - Done));
		DWORD Read = 0;
		if(!ReadFile(hFile, vOut.data() + Done, Chunk, &Read, NULL) || Read == 0)
		{
			CloseHandle(hFile);
			return false;
		}
		Done += Read;
	}
	CloseHandle(hFile);
	return true;
}

// Parse the central directory. Only stored (0) and deflate (8) entries are accepted;
// zip64 archives are rejected rather than half-handled.
static bool ReadCentralDirectory(const std::vector<unsigned char> &vZip, std::vector<SEntry> &vOut)
{
	if(vZip.size() < 22)
		return false;

	// Locate the end-of-central-directory record, scanning back over the comment field.
	size_t Eocd = 0;
	bool Found = false;
	const size_t MaxComment = vZip.size() < 65557 ? vZip.size() : 65557;
	for(size_t Back = 22; Back <= MaxComment; ++Back)
	{
		const size_t Pos = vZip.size() - Back;
		if(Read32(&vZip[Pos]) == SIG_EOCD)
		{
			Eocd = Pos;
			Found = true;
			break;
		}
	}
	if(!Found)
		return false;

	const uint32_t Count = Read16(&vZip[Eocd + 10]);
	const uint32_t CdSize = Read32(&vZip[Eocd + 12]);
	const uint32_t CdOffset = Read32(&vZip[Eocd + 16]);
	if(Count == 0xffff || CdOffset == 0xffffffffu || CdSize == 0xffffffffu)
		return false; // zip64, not produced by our release pipeline
	if(Count > MAX_ENTRIES)
		return false;
	if((uint64_t)CdOffset + CdSize > vZip.size())
		return false;

	size_t Pos = CdOffset;
	vOut.clear();
	vOut.reserve(Count);
	for(uint32_t i = 0; i < Count; ++i)
	{
		if(Pos + 46 > vZip.size() || Read32(&vZip[Pos]) != SIG_CDIR)
			return false;

		SEntry Entry;
		Entry.m_Method = Read16(&vZip[Pos + 10]);
		Entry.m_CompressedSize = Read32(&vZip[Pos + 20]);
		Entry.m_UncompressedSize = Read32(&vZip[Pos + 24]);
		const uint16_t NameLen = Read16(&vZip[Pos + 28]);
		const uint16_t ExtraLen = Read16(&vZip[Pos + 30]);
		const uint16_t CommentLen = Read16(&vZip[Pos + 32]);
		Entry.m_LocalOffset = Read32(&vZip[Pos + 42]);

		if(Pos + 46 + NameLen > vZip.size())
			return false;
		if(Entry.m_UncompressedSize > MAX_ENTRY_BYTES || Entry.m_CompressedSize > MAX_ENTRY_BYTES)
			return false;
		if(Entry.m_Method != 0 && Entry.m_Method != 8)
			return false;

		Entry.m_Name.assign((const char *)&vZip[Pos + 46], NameLen);
		for(char &Ch : Entry.m_Name)
		{
			if(Ch == '\\')
				Ch = '/';
		}

		Pos += 46u + NameLen + ExtraLen + CommentLen;
		if(Entry.m_Name.empty() || !IsSafeEntryName(Entry.m_Name))
			return false;
		vOut.push_back(std::move(Entry));
	}
	return true;
}

// Create every missing directory along Path (a full filesystem path).
static void EnsureDirectories(const std::wstring &Path)
{
	for(size_t i = 0; i < Path.size(); ++i)
	{
		if(Path[i] != L'\\' && Path[i] != L'/')
			continue;
		const std::wstring Prefix = Path.substr(0, i);
		if(Prefix.empty() || (Prefix.size() == 2 && Prefix[1] == L':'))
			continue;
		CreateDirectoryW(Prefix.c_str(), NULL);
	}
}

static bool WriteAll(HANDLE hFile, const unsigned char *pData, size_t Size)
{
	size_t Done = 0;
	while(Done < Size)
	{
		const DWORD Chunk = (DWORD)((Size - Done) > 0x100000 ? 0x100000 : (Size - Done));
		DWORD Written = 0;
		if(!WriteFile(hFile, pData + Done, Chunk, &Written, NULL) || Written == 0)
			return false;
		Done += Written;
	}
	return true;
}

// Inflate (or copy, for stored entries) a single member to DstPath.
static bool ExtractEntry(const std::vector<unsigned char> &vZip, const SEntry &Entry, const std::wstring &DstPath)
{
	// The local header repeats the name/extra lengths; the payload starts after them.
	if(Entry.m_LocalOffset + 30 > vZip.size() || Read32(&vZip[(size_t)Entry.m_LocalOffset]) != SIG_LOCAL)
		return false;
	const uint16_t LocalNameLen = Read16(&vZip[(size_t)Entry.m_LocalOffset + 26]);
	const uint16_t LocalExtraLen = Read16(&vZip[(size_t)Entry.m_LocalOffset + 28]);
	const uint64_t DataOffset = Entry.m_LocalOffset + 30ull + LocalNameLen + LocalExtraLen;
	if(DataOffset >= vZip.size() || DataOffset + Entry.m_CompressedSize > vZip.size())
		return false;

	EnsureDirectories(DstPath);
	const HANDLE hFile = CreateFileW(DstPath.c_str(), GENERIC_WRITE, 0, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hFile == INVALID_HANDLE_VALUE)
		return false;

	const unsigned char *pSrc = &vZip[(size_t)DataOffset];
	bool Ok = true;

	if(Entry.m_Method == 0)
	{
		Ok = Entry.m_CompressedSize == Entry.m_UncompressedSize &&
			WriteAll(hFile, pSrc, (size_t)Entry.m_CompressedSize);
	}
	else
	{
		z_stream Stream = {};
		// Raw deflate: zip members carry no zlib header, hence the negative window bits.
		if(inflateInit2(&Stream, -MAX_WBITS) != Z_OK)
		{
			CloseHandle(hFile);
			return false;
		}

		Stream.next_in = (Bytef *)pSrc;
		Stream.avail_in = (uInt)Entry.m_CompressedSize;

		std::vector<unsigned char> vOut(0x40000);
		uint64_t Total = 0;
		int Ret = Z_OK;
		do
		{
			Stream.next_out = vOut.data();
			Stream.avail_out = (uInt)vOut.size();
			Ret = inflate(&Stream, Z_NO_FLUSH);
			if(Ret != Z_OK && Ret != Z_STREAM_END && Ret != Z_BUF_ERROR)
			{
				Ok = false;
				break;
			}
			const size_t Produced = vOut.size() - Stream.avail_out;
			if(Produced > 0)
			{
				Total += Produced;
				if(Total > Entry.m_UncompressedSize || !WriteAll(hFile, vOut.data(), Produced))
				{
					Ok = false;
					break;
				}
			}
			else if(Ret == Z_BUF_ERROR)
			{
				Ok = false; // no progress possible: truncated member
				break;
			}
		} while(Ret != Z_STREAM_END);

		inflateEnd(&Stream);
		if(Ok)
			Ok = Ret == Z_STREAM_END && Total == Entry.m_UncompressedSize;
	}

	CloseHandle(hFile);
	if(!Ok)
		DeleteFileW(DstPath.c_str());
	return Ok;
}

// Unpack pArchive into pDestDir. PerEntry is called after each member for progress.
static bool Extract(const wchar_t *pArchive, const wchar_t *pDestDir, const std::function<void(int, int)> &PerEntry)
{
	std::vector<unsigned char> vZip;
	if(!ReadFileBytes(pArchive, vZip))
		return false;

	std::vector<SEntry> vEntries;
	if(!ReadCentralDirectory(vZip, vEntries) || vEntries.empty())
		return false;

	const int Total = (int)vEntries.size();
	int Done = 0;
	for(const SEntry &Entry : vEntries)
	{
		const std::wstring Relative = Widen(Entry.m_Name);
		if(Relative.empty())
			return false;

		std::wstring Full(pDestDir);
		Full += L'\\';
		Full += Relative;
		for(wchar_t &Ch : Full)
		{
			if(Ch == L'/')
				Ch = L'\\';
		}

		// A trailing slash marks a directory member: create it and move on.
		if(Entry.m_Name.back() == '/')
		{
			EnsureDirectories(Full + L'\\');
			CreateDirectoryW(Full.c_str(), NULL);
		}
		else if(!ExtractEntry(vZip, Entry, Full))
		{
			return false;
		}

		++Done;
		if(PerEntry)
			PerEntry(Done, Total);
	}
	return true;
}
} // namespace zip

// ─── Helpers ──────────────────────────────────────────────────────────────────

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
	if(!zip::Extract(pA->aArchive, aExtract, [](int Done, int Total)
		{
			const int Pct = 10 + Done * 40 / (Total > 0 ? Total : 1);
			SetPercent(Pct < 50 ? Pct : 50);
		}))
	{
		Fail(L"Extraction failed. The archive may be corrupted.");
		delete pA; return 1;
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
