#include "updater.h"

#include <base/system.h>

#include <engine/client.h>
#include <engine/external/json-parser/json.h>
#include <engine/shared/http.h>
#include <engine/shared/json.h>
#include <engine/storage.h>

#include <game/version.h>
#include <game/client/components/bestclient/version.h>

#include <algorithm>
#include <cctype>
#include <iterator>
#include <string>
#include <vector>

static constexpr const char *GITHUB_RELEASES_URL = "https://api.github.com/repos/RoflikBEST/bestdownload/releases?per_page=10";
static constexpr const char *UPDATE_ARCHIVE_PATH = "update/bestclient-release.zip";
static constexpr const char *UPDATE_SCRIPT_PATH = "update/apply_bestclient_update.ps1";

static void BuildGitHubReleasesUrl(char *pBuf, int BufSize)
{
	str_format(pBuf, BufSize, "%s&t=%lld", GITHUB_RELEASES_URL, (long long)time_timestamp());
}

static std::string ToLowerAscii(const char *pStr)
{
	std::string Lower;
	if(!pStr)
		return Lower;

	for(const unsigned char *p = reinterpret_cast<const unsigned char *>(pStr); *p != '\0'; ++p)
		Lower.push_back(static_cast<char>(std::tolower(*p)));
	return Lower;
}

static void NormalizeVersionString(const char *pVersion, char *pBuf, int BufSize)
{
	if(BufSize <= 0)
		return;

	if(!pVersion)
	{
		pBuf[0] = '\0';
		return;
	}

	while(*pVersion != '\0' && std::isspace(static_cast<unsigned char>(*pVersion)))
		++pVersion;

	if((pVersion[0] == 'v' || pVersion[0] == 'V') && std::isdigit(static_cast<unsigned char>(pVersion[1])))
		++pVersion;

	str_copy(pBuf, pVersion, BufSize);
}

static std::vector<int> ExtractVersionNumbers(const char *pVersion)
{
	std::vector<int> vNumbers;
	if(!pVersion)
		return vNumbers;

	int Current = -1;
	for(const unsigned char *p = reinterpret_cast<const unsigned char *>(pVersion); *p != '\0'; ++p)
	{
		if(std::isdigit(*p))
		{
			if(Current < 0)
				Current = 0;
			Current = Current * 10 + (*p - '0');
		}
		else if(Current >= 0)
		{
			vNumbers.push_back(Current);
			Current = -1;
		}
	}

	if(Current >= 0)
		vNumbers.push_back(Current);

	return vNumbers;
}

static int CompareVersionStrings(const char *pLeft, const char *pRight)
{
	char aLeftNormalized[64];
	char aRightNormalized[64];
	NormalizeVersionString(pLeft, aLeftNormalized, sizeof(aLeftNormalized));
	NormalizeVersionString(pRight, aRightNormalized, sizeof(aRightNormalized));

	const std::vector<int> vLeft = ExtractVersionNumbers(aLeftNormalized);
	const std::vector<int> vRight = ExtractVersionNumbers(aRightNormalized);
	const size_t Num = maximum(vLeft.size(), vRight.size());
	for(size_t i = 0; i < Num; ++i)
	{
		const int Left = i < vLeft.size() ? vLeft[i] : 0;
		const int Right = i < vRight.size() ? vRight[i] : 0;
		if(Left < Right)
			return -1;
		if(Left > Right)
			return 1;
	}

	return str_comp_nocase(aLeftNormalized, aRightNormalized);
}

static int ScoreArchiveAsset(const char *pAssetName)
{
	if(!pAssetName || !str_endswith_nocase(pAssetName, ".zip"))
		return -1;

	const std::string Lower = ToLowerAscii(pAssetName);
	int Score = 10;

	if(Lower.find("windows") != std::string::npos || Lower.find("win") != std::string::npos)
		Score += 20;
	if(Lower.find("bestclient") != std::string::npos)
		Score += 10;

#if defined(CONF_ARCH_AMD64)
	if(Lower.find("x64") != std::string::npos || Lower.find("64") != std::string::npos || Lower.find("amd64") != std::string::npos)
		Score += 10;
#elif defined(CONF_ARCH_IA32)
	if(Lower.find("x86") != std::string::npos || Lower.find("32") != std::string::npos)
		Score += 10;
#endif

	if(Lower.find("linux") != std::string::npos || Lower.find("mac") != std::string::npos || Lower.find("android") != std::string::npos)
		Score -= 10;

	return Score;
}

static bool ParseReleaseObject(const json_value *pJson, char *pVersion, int VersionSize, char *pArchiveName, int ArchiveNameSize, char *pArchiveUrl, int ArchiveUrlSize)
{
	if(!pJson || pJson->type != json_object)
		return false;

	const char *pReleaseVersion = json_string_get(json_object_get(pJson, "tag_name"));
	if(!pReleaseVersion)
		pReleaseVersion = json_string_get(json_object_get(pJson, "name"));
	if(!pReleaseVersion)
		return false;

	const json_value *pAssets = json_object_get(pJson, "assets");
	if(!pAssets || pAssets->type != json_array)
		return false;

	int BestScore = -1;
	char aBestName[128] = "";
	char aBestUrl[2048] = "";

	for(int i = 0; i < json_array_length(pAssets); ++i)
	{
		const json_value *pAsset = json_array_get(pAssets, i);
		if(!pAsset || pAsset->type != json_object)
			continue;

		const char *pName = json_string_get(json_object_get(pAsset, "name"));
		const char *pUrl = json_string_get(json_object_get(pAsset, "browser_download_url"));
		const int Score = ScoreArchiveAsset(pName);
		if(!pName || !pUrl || Score < BestScore)
			continue;

		BestScore = Score;
		str_copy(aBestName, pName, sizeof(aBestName));
		str_copy(aBestUrl, pUrl, sizeof(aBestUrl));
	}

	if(BestScore < 0)
		return false;

	str_copy(pVersion, pReleaseVersion, VersionSize);
	str_copy(pArchiveName, aBestName, ArchiveNameSize);
	str_copy(pArchiveUrl, aBestUrl, ArchiveUrlSize);
	return true;
}

static bool ParseLatestRelease(json_value *pJson, char *pVersion, int VersionSize, char *pArchiveName, int ArchiveNameSize, char *pArchiveUrl, int ArchiveUrlSize)
{
	if(!pJson)
		return false;

	if(pJson->type == json_object)
		return ParseReleaseObject(pJson, pVersion, VersionSize, pArchiveName, ArchiveNameSize, pArchiveUrl, ArchiveUrlSize);

	if(pJson->type == json_array)
	{
		for(int i = 0; i < json_array_length(pJson); ++i)
		{
			const json_value *pRelease = json_array_get(pJson, i);
			if(ParseReleaseObject(pRelease, pVersion, VersionSize, pArchiveName, ArchiveNameSize, pArchiveUrl, ArchiveUrlSize))
				return true;
		}
	}

	return false;
}

static void StripFilename(char *pPath)
{
	if(!pPath)
		return;

	for(int i = str_length(pPath) - 1; i >= 0; --i)
	{
		if(pPath[i] == '/' || pPath[i] == '\\')
		{
			pPath[i] = '\0';
			return;
		}
	}
	pPath[0] = '\0';
}

CUpdater::CUpdater()
{
	m_pClient = nullptr;
	m_pStorage = nullptr;
	m_pHttp = nullptr;

	m_State = CLEAN;
	m_aStatus[0] = '\0';
	m_Percent = 0;
	m_aLatestVersion[0] = '\0';
	m_aArchiveName[0] = '\0';
	m_aArchiveUrl[0] = '\0';
	str_copy(m_aArchivePath, UPDATE_ARCHIVE_PATH, sizeof(m_aArchivePath));
}

void CUpdater::Init(CHttp *pHttp)
{
	m_pClient = Kernel()->RequestInterface<IClient>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();
	m_pHttp = pHttp;
}

void CUpdater::SetCurrentState(EUpdaterState NewState)
{
	const CLockScope LockScope(m_Lock);
	m_State = NewState;
}

void CUpdater::SetStatus(const char *pStatus)
{
	const CLockScope LockScope(m_Lock);
	str_copy(m_aStatus, pStatus ? pStatus : "", sizeof(m_aStatus));
}

void CUpdater::SetPercent(int Percent)
{
	const CLockScope LockScope(m_Lock);
	m_Percent = std::clamp(Percent, 0, 100);
}

IUpdater::EUpdaterState CUpdater::GetCurrentState()
{
	const CLockScope LockScope(m_Lock);
	return m_State;
}

void CUpdater::GetCurrentFile(char *pBuf, int BufSize)
{
	const CLockScope LockScope(m_Lock);
	str_copy(pBuf, m_aStatus, BufSize);
}

int CUpdater::GetCurrentPercent()
{
	const CLockScope LockScope(m_Lock);
	return m_Percent;
}

void CUpdater::ResetTask()
{
	if(m_pCurrentTask)
	{
		m_pCurrentTask->Abort();
		m_pCurrentTask = nullptr;
	}
	m_TaskKind = ETaskKind::NONE;
}

void CUpdater::StartReleaseFetch()
{
	ResetTask();
	SetStatus("Checking latest release");
	SetPercent(0);
	SetCurrentState(IUpdater::GETTING_MANIFEST);

	char aUrl[2304];
	BuildGitHubReleasesUrl(aUrl, sizeof(aUrl));
	m_TaskKind = ETaskKind::FETCH_RELEASE;
	m_pCurrentTask = HttpGet(aUrl);
	m_pCurrentTask->HeaderString("Accept", "application/vnd.github+json");
	m_pCurrentTask->HeaderString("User-Agent", CLIENT_NAME);
	m_pCurrentTask->HeaderString("X-GitHub-Api-Version", "2022-11-28");
	m_pCurrentTask->HeaderString("Cache-Control", "no-cache");
	m_pCurrentTask->HeaderString("Pragma", "no-cache");
	m_pCurrentTask->Timeout(CTimeout{10000, 0, 500, 10});
	m_pCurrentTask->IpResolve(IPRESOLVE::V4);
	m_pHttp->Run(m_pCurrentTask);
}

bool CUpdater::ParseReleaseTask()
{
	json_value *pJson = m_pCurrentTask ? m_pCurrentTask->ResultJson() : nullptr;
	if(!pJson)
	{
		SetStatus("Failed to parse release info");
		SetCurrentState(IUpdater::FAIL);
		return false;
	}

	const bool Parsed = ParseLatestRelease(pJson, m_aLatestVersion, sizeof(m_aLatestVersion), m_aArchiveName, sizeof(m_aArchiveName), m_aArchiveUrl, sizeof(m_aArchiveUrl));
	json_value_free(pJson);

	if(!Parsed)
	{
		SetStatus("Release archive not found");
		SetCurrentState(IUpdater::FAIL);
		return false;
	}

	if(CompareVersionStrings(m_aLatestVersion, BESTCLIENT_VERSION) <= 0)
	{
		SetStatus("No new release found");
		SetCurrentState(IUpdater::CLEAN);
		return false;
	}

	return true;
}

void CUpdater::StartArchiveDownload()
{
	ResetTask();
	str_copy(m_aArchivePath, UPDATE_ARCHIVE_PATH, sizeof(m_aArchivePath));
	m_pStorage->RemoveBinaryFile(m_aArchivePath);

	SetStatus(m_aArchiveName);
	SetPercent(0);
	SetCurrentState(IUpdater::DOWNLOADING);

	m_TaskKind = ETaskKind::DOWNLOAD_ARCHIVE;
	m_pCurrentTask = HttpGetFile(m_aArchiveUrl, m_pStorage, m_aArchivePath, IStorage::TYPE_ABSOLUTE);
	m_pCurrentTask->HeaderString("User-Agent", CLIENT_NAME);
	m_pCurrentTask->Timeout(CTimeout{10000, 0, 500, 10});
	m_pCurrentTask->IpResolve(IPRESOLVE::V4);
	m_pHttp->Run(m_pCurrentTask);
}

bool CUpdater::WriteApplyScript(char *pScriptPath, int ScriptPathSize, char *pInstallDir, int InstallDirSize, char *pExePath, int ExePathSize)
{
	m_pStorage->GetBinaryPath(UPDATE_SCRIPT_PATH, pScriptPath, ScriptPathSize);
	m_pStorage->GetBinaryPathAbsolute(PLAT_CLIENT_EXEC, pExePath, ExePathSize);
	str_copy(pInstallDir, pExePath, InstallDirSize);
	StripFilename(pInstallDir);

	if(fs_makedir_rec_for(pScriptPath) < 0)
		return false;

	static constexpr const char *pScript =
		"param(\n"
		"    [int]$PidToWait,\n"
		"    [string]$ArchivePath,\n"
		"    [string]$InstallDir,\n"
		"    [string]$ExePath\n"
		")\n"
		"$ErrorActionPreference = 'Stop'\n"
		"try {\n"
		"    while(Get-Process -Id $PidToWait -ErrorAction SilentlyContinue) {\n"
		"        Start-Sleep -Milliseconds 200\n"
		"    }\n"
		"    $extractDir = Join-Path $InstallDir 'update\\extract'\n"
		"    if(Test-Path -LiteralPath $extractDir) {\n"
		"        Remove-Item -LiteralPath $extractDir -Recurse -Force\n"
		"    }\n"
		"    New-Item -ItemType Directory -Path $extractDir -Force | Out-Null\n"
		"    Expand-Archive -LiteralPath $ArchivePath -DestinationPath $extractDir -Force\n"
		"    $copyRoot = $extractDir\n"
		"    $items = @(Get-ChildItem -LiteralPath $extractDir -Force)\n"
		"    if($items.Count -eq 1 -and $items[0].PSIsContainer) {\n"
		"        $copyRoot = $items[0].FullName\n"
		"    }\n"
		"    foreach($item in Get-ChildItem -LiteralPath $copyRoot -Force) {\n"
		"        Copy-Item -Path $item.FullName -Destination $InstallDir -Recurse -Force\n"
		"    }\n"
		"    Remove-Item -LiteralPath $ArchivePath -Force -ErrorAction SilentlyContinue\n"
		"    Remove-Item -LiteralPath $extractDir -Recurse -Force -ErrorAction SilentlyContinue\n"
		"    Start-Process -FilePath $ExePath\n"
		"} catch {\n"
		"    $logPath = Join-Path $InstallDir 'update\\apply_update_error.txt'\n"
		"    $_ | Out-String | Set-Content -LiteralPath $logPath -Encoding UTF8\n"
		"}\n";

	IOHANDLE File = io_open(pScriptPath, IOFLAG_WRITE);
	if(!File)
		return false;

	io_write(File, pScript, str_length(pScript));
	io_close(File);
	return true;
}

bool CUpdater::LaunchApplyScriptAndQuit()
{
#if defined(CONF_FAMILY_WINDOWS)
	char aArchivePath[IO_MAX_PATH_LENGTH];
	char aScriptPath[IO_MAX_PATH_LENGTH];
	char aInstallDir[IO_MAX_PATH_LENGTH];
	char aExePath[IO_MAX_PATH_LENGTH];
	char aPid[32];

	m_pStorage->GetBinaryPath(m_aArchivePath, aArchivePath, sizeof(aArchivePath));
	if(!m_pStorage->FileExists(aArchivePath, IStorage::TYPE_ABSOLUTE))
	{
		SetStatus("Downloaded archive missing");
		return false;
	}

	if(!WriteApplyScript(aScriptPath, sizeof(aScriptPath), aInstallDir, sizeof(aInstallDir), aExePath, sizeof(aExePath)))
	{
		SetStatus("Failed to prepare updater script");
		return false;
	}

	str_format(aPid, sizeof(aPid), "%d", pid());
	const char *apArguments[] = {
		"-NoProfile",
		"-File",
		aScriptPath,
		"-PidToWait",
		aPid,
		"-ArchivePath",
		aArchivePath,
		"-InstallDir",
		aInstallDir,
		"-ExePath",
		aExePath,
	};

	if(!shell_execute("powershell.exe", EShellExecuteWindowState::BACKGROUND, apArguments, std::size(apArguments)))
	{
		SetStatus("Failed to launch updater");
		return false;
	}

	m_pClient->Quit();
	return true;
#else
	SetStatus("Archive updater is only available on Windows");
	return false;
#endif
}

void CUpdater::InitiateUpdate()
{
	const EUpdaterState State = GetCurrentState();
	if(State == IUpdater::GETTING_MANIFEST || State == IUpdater::DOWNLOADING)
		return;

	StartReleaseFetch();
}

void CUpdater::ApplyUpdateAndRestart()
{
	if(GetCurrentState() != IUpdater::NEED_RESTART)
		return;

	if(!LaunchApplyScriptAndQuit())
		SetCurrentState(IUpdater::FAIL);
}

void CUpdater::Update()
{
	if(!m_pCurrentTask)
		return;

	if(!m_pCurrentTask->Done())
	{
		if(GetCurrentState() == IUpdater::DOWNLOADING)
			SetPercent(m_pCurrentTask->Progress());
		return;
	}

	if(m_pCurrentTask->State() != EHttpState::DONE || m_pCurrentTask->StatusCode() >= 400)
	{
		ResetTask();
		SetStatus("Update download failed");
		SetCurrentState(IUpdater::FAIL);
		return;
	}

	if(m_TaskKind == ETaskKind::FETCH_RELEASE)
	{
		if(ParseReleaseTask())
			StartArchiveDownload();
		else
			ResetTask();
		return;
	}

	if(m_TaskKind == ETaskKind::DOWNLOAD_ARCHIVE)
	{
		ResetTask();
		SetPercent(100);
		SetStatus(m_aArchiveName[0] != '\0' ? m_aArchiveName : "update");
		SetCurrentState(IUpdater::NEED_RESTART);
	}
}
