/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_UPDATER_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_UPDATER_H

#include <base/lock.h>
#include <base/types.h>

#include <engine/updater.h>

#include <memory>

class IHttpRequest;

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
	class IHttp *m_pHttp;

	CLock m_Lock;

	EUpdaterState m_State GUARDED_BY(m_Lock);
	char m_aStatus[256] GUARDED_BY(m_Lock);
	int m_Percent GUARDED_BY(m_Lock);

	std::shared_ptr<IHttpRequest> m_pCurrentTask;
	ETaskKind m_TaskKind = ETaskKind::NONE;
	bool m_AutoCheckPending = false;

	char m_aLatestVersion[64];
	char m_aArchiveName[128];
	char m_aArchiveUrl[2048];
	char m_aArchivePath[IO_MAX_PATH_LENGTH];

	void ResetTask() REQUIRES(!m_Lock);
	void StartReleaseFetch() REQUIRES(!m_Lock);
	void ParseReleaseTask() REQUIRES(!m_Lock);
	void StartArchiveDownload() REQUIRES(!m_Lock);
	bool LaunchApplyScriptAndQuit() REQUIRES(!m_Lock);

	void SetCurrentState(EUpdaterState NewState) REQUIRES(!m_Lock);
	void SetStatus(const char *pStatus) REQUIRES(!m_Lock);
	void SetPercent(int Percent) REQUIRES(!m_Lock);

public:
	CUpdater();

	EUpdaterState GetCurrentState() override REQUIRES(!m_Lock);
	void GetCurrentFile(char *pBuf, int BufSize) override REQUIRES(!m_Lock);
	int GetCurrentPercent() override REQUIRES(!m_Lock);

	void CheckForUpdate() REQUIRES(!m_Lock) override;
	void InitiateUpdate() REQUIRES(!m_Lock) override;
	void ApplyUpdateAndRestart() REQUIRES(!m_Lock) override;
	const char *GetLatestVersionString() override REQUIRES(!m_Lock);
	void Init();
	void Update() REQUIRES(!m_Lock) override;
};

#endif
