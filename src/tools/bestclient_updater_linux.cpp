// Copyright © 2026 BestProject Team
// BestClient Updater — standalone Linux GUI application (SDL2).
// Mirrors src/tools/bestclient_updater.cpp (Windows) step-for-step. Receives
// four positional arguments:
//   argv[1]  PID of the client process to wait for
//   argv[2]  Absolute path to the downloaded archive (.tar.xz)
//   argv[3]  Install directory (directory containing the DDNet binary)
//   argv[4]  Absolute path to the client executable to relaunch
//
// Only compiled on Linux (see CMakeLists.txt).

#include "bestclient_updater_linux_font.h"
#include "bestclient_updater_linux_fs.h"

#include <SDL.h>

#include <dirent.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <string>

// ─── Theme (matches src/tools/bestclient_updater.cpp) ───────────────────────

static const int WND_W = 480;
static const int WND_H = 215;

static const SDL_Color C_BG = {22, 22, 22, 255};
static const SDL_Color C_GREEN = {105, 190, 70, 255};
static const SDL_Color C_ORANGE = {230, 80, 45, 255};
static const SDL_Color C_TITLE = {235, 245, 232, 255};
static const SDL_Color C_DIM = {140, 155, 135, 255};
static const SDL_Color C_BAR_BG = {40, 40, 40, 255};
static const SDL_Color C_ERROR = {230, 75, 45, 255};

// ─── Shared state ────────────────────────────────────────────────────────────

static SDL_mutex *g_pLock = nullptr;
static SDL_atomic_t g_Percent;
static SDL_atomic_t g_Failed;
static SDL_atomic_t g_Done;
static std::string g_Status = "Starting...";

static void SetStatus(const char *pText)
{
	SDL_LockMutex(g_pLock);
	g_Status = pText;
	SDL_UnlockMutex(g_pLock);
}

static std::string GetStatus()
{
	SDL_LockMutex(g_pLock);
	const std::string Copy = g_Status;
	SDL_UnlockMutex(g_pLock);
	return Copy;
}

static void SetPercent(int Pct)
{
	if(Pct < 0)
		Pct = 0;
	if(Pct > 100)
		Pct = 100;
	SDL_AtomicSet(&g_Percent, Pct);
}

static void Fail(const char *pMsg)
{
	SDL_AtomicSet(&g_Failed, 1);
	SetStatus(pMsg);
	// Leave window open so the user can read the error; Esc closes it.
}

// ─── Helpers ──────────────────────────────────────────────────────────────────

// Runs a process to completion without a shell (avoids shell-metacharacter
// injection from paths) and returns its exit code, or -1 on spawn failure.
static int RunProcessSync(const char *pFile, char *const apArgv[])
{
	const pid_t Child = fork();
	if(Child == -1)
		return -1;
	if(Child == 0)
	{
		execvp(pFile, apArgv);
		_exit(127);
	}
	int Status = 0;
	if(waitpid(Child, &Status, 0) == -1)
		return -1;
	if(WIFEXITED(Status))
		return WEXITSTATUS(Status);
	return -1;
}

// The PID we're given is our parent's parent (the client forked us and quit),
// not a child of this process, so waitpid() cannot be used to wait for it.
static bool ProcessGone(pid_t Pid)
{
	return kill(Pid, 0) == -1 && errno == ESRCH;
}

// ─── Worker ──────────────────────────────────────────────────────────────────

// Directories to preserve across updates (user-placed assets), matching
// src/tools/bestclient_updater.cpp's k_aUserDirs.
static const char *k_apUserDirs[] = {
	"data/assets/arrow",
	"data/assets/arrows",
	"data/assets/audio",
	"data/audio",
};

struct SWorkerArgs
{
	pid_t Pid;
	std::string Archive;
	std::string InstallDir;
	std::string ExePath;
};

static int WorkerThreadFn(void *pData)
{
	auto *pArgs = static_cast<SWorkerArgs *>(pData);

	// ── 1. Wait for the client to exit ────────────────────────────────────
	SetStatus("Waiting for client to close...");
	SetPercent(2);
	while(!ProcessGone(pArgs->Pid))
		usleep(200 * 1000);
	SetPercent(8);

	// ── 2. Prepare extraction directory ───────────────────────────────────
	const std::string Extract = pArgs->InstallDir + "/update/extract";
	BcFs::DeleteTree(Extract.c_str());
	if(!BcFs::MakeDirRecursive(Extract))
	{
		Fail("Failed to create extraction directory.");
		delete pArgs;
		return 1;
	}

	// ── 3. Extract archive ─────────────────────────────────────────────────
	SetStatus("Extracting update...");
	{
		char *apArgv[] = {(char *)"tar", (char *)"-xf", (char *)pArgs->Archive.c_str(), (char *)"-C", (char *)Extract.c_str(), nullptr};
		const int ExitCode = RunProcessSync("tar", apArgv);
		if(ExitCode != 0)
		{
			Fail("Extraction failed. The archive may be corrupted.");
			delete pArgs;
			return 1;
		}
	}
	SetPercent(50);

	// ── 4. Resolve copy root (archive may wrap files in a single subfolder) ──
	std::string CopyRoot = Extract;
	{
		DIR *pDir = opendir(Extract.c_str());
		if(pDir)
		{
			int Count = 0;
			std::string First;
			struct dirent *pEntry;
			while((pEntry = readdir(pDir)) != nullptr)
			{
				if(strcmp(pEntry->d_name, ".") == 0 || strcmp(pEntry->d_name, "..") == 0)
					continue;
				++Count;
				if(Count == 1)
					First = pEntry->d_name;
			}
			closedir(pDir);

			if(Count == 1)
			{
				const std::string Sub = Extract + "/" + First;
				if(BcFs::IsDirectory(Sub.c_str()))
					CopyRoot = Sub;
			}
		}
	}

	// ── 5. Backup user asset directories ──────────────────────────────────
	SetStatus("Backing up settings...");
	const std::string Backup = pArgs->InstallDir + "/update/backup_" + std::to_string((long long)pArgs->Pid);
	BcFs::DeleteTree(Backup.c_str());
	for(const char *pRel : k_apUserDirs)
	{
		const std::string Src = pArgs->InstallDir + "/" + pRel;
		const std::string Dst = Backup + "/" + pRel;
		if(BcFs::PathExists(Src.c_str()))
			BcFs::CopyTree(Src.c_str(), Dst.c_str());
	}
	SetPercent(55);

	// ── 6. Copy new files into install directory ─────────────────────────────
	SetStatus("Installing files...");
	{
		const int Total = BcFs::CountFiles(CopyRoot.c_str());
		int Done = 0;
		BcFs::CopyTree(CopyRoot.c_str(), pArgs->InstallDir.c_str(), [&]() {
			++Done;
			const int Pct = 55 + Done * 35 / Total;
			SetPercent(Pct < 90 ? Pct : 90);
		});
	}
	SetPercent(90);

	// ── 7. Restore user assets ─────────────────────────────────────────────
	SetStatus("Restoring settings...");
	for(const char *pRel : k_apUserDirs)
	{
		const std::string Src = Backup + "/" + pRel;
		const std::string Dst = pArgs->InstallDir + "/" + pRel;
		if(BcFs::PathExists(Src.c_str()))
			BcFs::CopyTree(Src.c_str(), Dst.c_str());
	}
	SetPercent(95);

	// ── 8. Clean up temp files ─────────────────────────────────────────────
	SetStatus("Cleaning up...");
	unlink(pArgs->Archive.c_str());
	BcFs::DeleteTree(Extract.c_str());
	BcFs::DeleteTree(Backup.c_str());
	SetPercent(100);

	// ── 9. Launch client ───────────────────────────────────────────────────
	SetStatus("Launching BestClient...");
	usleep(500 * 1000);

	const pid_t Child = fork();
	if(Child == 0)
	{
		chdir(pArgs->InstallDir.c_str());
		char *apArgv[] = {(char *)pArgs->ExePath.c_str(), nullptr};
		execvp(pArgs->ExePath.c_str(), apArgv);
		_exit(1);
	}

	usleep(300 * 1000);
	delete pArgs;

	SDL_AtomicSet(&g_Done, 1);
	return 0;
}

// ─── Rendering ────────────────────────────────────────────────────────────────

static SDL_Color LerpColor(SDL_Color A, SDL_Color B, float T)
{
	SDL_Color Out;
	Out.r = (Uint8)(A.r + T * (B.r - A.r));
	Out.g = (Uint8)(A.g + T * (B.g - A.g));
	Out.b = (Uint8)(A.b + T * (B.b - A.b));
	Out.a = 255;
	return Out;
}

static void DrawGradientH(SDL_Renderer *pRenderer, SDL_Rect Rect, SDL_Color C1, SDL_Color C2)
{
	if(Rect.w <= 0)
		return;
	for(int X = 0; X < Rect.w; ++X)
	{
		const float T = Rect.w > 1 ? (float)X / (float)(Rect.w - 1) : 0.0f;
		const SDL_Color C = LerpColor(C1, C2, T);
		SDL_SetRenderDrawColor(pRenderer, C.r, C.g, C.b, 255);
		SDL_Rect Col = {Rect.x + X, Rect.y, 1, Rect.h};
		SDL_RenderFillRect(pRenderer, &Col);
	}
}

static void Render(SDL_Renderer *pRenderer)
{
	SDL_SetRenderDrawColor(pRenderer, C_BG.r, C_BG.g, C_BG.b, 255);
	SDL_RenderClear(pRenderer);

	SDL_Rect TopBar = {0, 0, WND_W, 4};
	DrawGradientH(pRenderer, TopBar, C_GREEN, C_ORANGE);

	{
		const int Scale = 4;
		const int W1 = BcFont::TextWidth("BestClient", Scale);
		BcFont::DrawText(pRenderer, (WND_W - W1) / 2, 14, Scale, "BestClient", C_TITLE);
		const int W2 = BcFont::TextWidth("Updater", Scale);
		BcFont::DrawText(pRenderer, (WND_W - W2) / 2, 48, Scale, "Updater", C_TITLE);
	}

	const int BarL = 40, BarR = WND_W - 80, BarT = 110, BarB = 134;

	SDL_SetRenderDrawColor(pRenderer, C_BAR_BG.r, C_BAR_BG.g, C_BAR_BG.b, 255);
	{
		SDL_Rect Bg = {BarL, BarT, BarR - BarL, BarB - BarT};
		SDL_RenderFillRect(pRenderer, &Bg);
	}

	const int Pct = SDL_AtomicGet(&g_Percent);
	const bool Failed = SDL_AtomicGet(&g_Failed) != 0;
	const int FillW = (BarR - BarL) * Pct / 100;
	if(FillW > 0)
	{
		if(Failed)
		{
			SDL_SetRenderDrawColor(pRenderer, C_ERROR.r, C_ERROR.g, C_ERROR.b, 255);
			SDL_Rect Fill = {BarL, BarT, FillW, BarB - BarT};
			SDL_RenderFillRect(pRenderer, &Fill);
		}
		else
		{
			const SDL_Color FillEnd = LerpColor(C_GREEN, C_ORANGE, Pct / 100.0f);
			SDL_Rect Fill = {BarL, BarT, FillW, BarB - BarT};
			DrawGradientH(pRenderer, Fill, C_GREEN, FillEnd);
		}
	}

	{
		char aBuf[8];
		snprintf(aBuf, sizeof(aBuf), "%d%%", Pct);
		BcFont::DrawText(pRenderer, BarR + 6, BarT + 5, 2, aBuf, Failed ? C_ERROR : C_TITLE);
	}

	{
		const std::string Status = GetStatus();
		BcFont::DrawText(pRenderer, BarL, BarB + 12, 2, Status.c_str(), Failed ? C_ERROR : C_DIM);
	}

	if(Failed)
		BcFont::DrawText(pRenderer, BarL, WND_H - 24, 2, "Press Esc to close.", C_DIM);

	SDL_RenderPresent(pRenderer);
}

// ─── Entry point ──────────────────────────────────────────────────────────────

int main(int argc, char **argv)
{
	if(argc < 5)
	{
		fprintf(stderr, "Usage: bestclient-updater <pid> <archive> <install_dir> <exe>\n");
		return 1;
	}

	auto *pArgs = new SWorkerArgs();
	pArgs->Pid = (pid_t)atol(argv[1]);
	pArgs->Archive = argv[2];
	pArgs->InstallDir = argv[3];
	pArgs->ExePath = argv[4];

	if(SDL_Init(SDL_INIT_VIDEO) != 0)
	{
		fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		return 1;
	}

	g_pLock = SDL_CreateMutex();

	SDL_Window *pWindow = SDL_CreateWindow("BestClient Updater",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		WND_W, WND_H, SDL_WINDOW_BORDERLESS);
	if(!pWindow)
	{
		fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
		SDL_Quit();
		return 1;
	}

	SDL_Renderer *pRenderer = SDL_CreateRenderer(pWindow, -1, SDL_RENDERER_ACCELERATED);
	if(!pRenderer)
		pRenderer = SDL_CreateRenderer(pWindow, -1, SDL_RENDERER_SOFTWARE);

	SDL_Thread *pThread = SDL_CreateThread(WorkerThreadFn, "bc-updater-worker", pArgs);
	if(pThread)
		SDL_DetachThread(pThread);

	bool Running = true;
	while(Running)
	{
		SDL_Event Event;
		while(SDL_PollEvent(&Event))
		{
			if(Event.type == SDL_QUIT)
				Running = false;
			else if(Event.type == SDL_KEYDOWN && Event.key.keysym.sym == SDLK_ESCAPE)
				Running = false;
		}

		if(SDL_AtomicGet(&g_Done) != 0)
			Running = false;

		Render(pRenderer);
		SDL_Delay(16);
	}

	SDL_DestroyRenderer(pRenderer);
	SDL_DestroyWindow(pWindow);
	SDL_DestroyMutex(g_pLock);
	SDL_Quit();
	return 0;
}
