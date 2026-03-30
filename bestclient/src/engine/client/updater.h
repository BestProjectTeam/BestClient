#ifndef ENGINE_CLIENT_UPDATER_H
#define ENGINE_CLIENT_UPDATER_H

#include <base/detect.h>
#include <base/lock.h>
#include <base/system.h>

#include <engine/updater.h>

#include <memory>

#define CLIENT_EXEC "DDNet"
#define SERVER_EXEC "DDNet-Server"

#if defined(CONF_FAMILY_WINDOWS)
#define PLAT_EXT ".exe"
#define PLAT_NAME CONF_PLATFORM_STRING
#elif defined(CONF_FAMILY_UNIX)
#define PLAT_EXT ""
#if defined(CONF_ARCH_IA32)
#define PLAT_NAME CONF_PLATFORM_STRING "-x86"
#elif defined(CONF_ARCH_AMD64)
#define PLAT_NAME CONF_PLATFORM_STRING "-x86_64"
#else
#define PLAT_NAME CONF_PLATFORM_STRING "-unsupported"
#endif
#else
#if defined(AUTOUPDATE)
#error Compiling with autoupdater on an unsupported platform
#endif
#define PLAT_EXT ""
#define PLAT_NAME "unsupported-unsupported"
#endif

#define PLAT_CLIENT_DOWN CLIENT_EXEC "-" PLAT_NAME PLAT_EXT
#define PLAT_SERVER_DOWN SERVER_EXEC "-" PLAT_NAME PLAT_EXT

#define PLAT_CLIENT_EXEC CLIENT_EXEC PLAT_EXT
#define PLAT_SERVER_EXEC SERVER_EXEC PLAT_EXT

class CHttpRequest;

class CUpdater : public IUpdater
{
	enum class ETaskKind
	{
		NONE,
		FETCH_RELEASE,
		DOWNLOAD_ARCHIVE,
	};

	class IClient *m_pClient;
	class IStorage *m_pStorage;
	class CHttp *m_pHttp;

	CLock m_Lock;

	EUpdaterState m_State GUARDED_BY(m_Lock);
	char m_aStatus[256] GUARDED_BY(m_Lock);
	int m_Percent GUARDED_BY(m_Lock);

	std::shared_ptr<CHttpRequest> m_pCurrentTask;
	ETaskKind m_TaskKind = ETaskKind::NONE;

	char m_aLatestVersion[64];
	char m_aArchiveName[128];
	char m_aArchiveUrl[2048];
	char m_aArchivePath[IO_MAX_PATH_LENGTH];

	void ResetTask() REQUIRES(!m_Lock);
	void StartReleaseFetch() REQUIRES(!m_Lock);
	bool ParseReleaseTask() REQUIRES(!m_Lock);
	void StartArchiveDownload() REQUIRES(!m_Lock);
	bool WriteApplyScript(char *pScriptPath, int ScriptPathSize, char *pInstallDir, int InstallDirSize, char *pExePath, int ExePathSize) REQUIRES(!m_Lock);
	bool LaunchApplyScriptAndQuit() REQUIRES(!m_Lock);

	void SetCurrentState(EUpdaterState NewState) REQUIRES(!m_Lock);
	void SetStatus(const char *pStatus) REQUIRES(!m_Lock);
	void SetPercent(int Percent) REQUIRES(!m_Lock);

public:
	CUpdater();

	EUpdaterState GetCurrentState() override REQUIRES(!m_Lock);
	void GetCurrentFile(char *pBuf, int BufSize) override REQUIRES(!m_Lock);
	int GetCurrentPercent() override REQUIRES(!m_Lock);

	void InitiateUpdate() REQUIRES(!m_Lock) override;
	void ApplyUpdateAndRestart() REQUIRES(!m_Lock) override;
	void Init(CHttp *pHttp);
	void Update() REQUIRES(!m_Lock) override;
};

#endif
