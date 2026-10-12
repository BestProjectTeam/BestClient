/* Copyright © 2026 BestProject Team */
#include "config_presets.h"

#include <base/io.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/config.h>
#include <engine/console.h>
#include <engine/shared/config.h>
#include <engine/shared/linereader.h>
#include <engine/storage.h>

#include <algorithm>
#include <utility>

static constexpr const char *CONFIGS_DIR = "BestClient/configs";

bool CBcConfigPresets::NameOk(const char *pName)
{
	if(!pName || !str_utf8_check(pName) || str_length(pName) >= MAX_NAME_LENGTH)
		return false;
	return str_valid_filename(pName);
}

static bool MakePath(char *pPath, int PathSize, const char *pName)
{
	if(!CBcConfigPresets::NameOk(pName))
		return false;
	str_format(pPath, PathSize, "%s/%s.cfg", CONFIGS_DIR, pName);
	return true;
}

static int ListCallback(const CFsFileInfo *pInfo, int IsDir, int, void *pUser)
{
	if(IsDir)
		return 0;
	const char *pExt = str_endswith(pInfo->m_pName, ".cfg");
	if(!pExt)
		return 0;
	const int NameLen = (int)(pExt - pInfo->m_pName);
	if(NameLen <= 0 || NameLen >= CBcConfigPresets::MAX_NAME_LENGTH)
		return 0;

	CBcConfigPreset Preset;
	str_copy(Preset.m_aName, pInfo->m_pName, NameLen + 1);
	if(!CBcConfigPresets::NameOk(Preset.m_aName))
		return 0;
	Preset.m_Time = pInfo->m_TimeModified;
	Preset.m_Changes = 0;
	auto *pList = static_cast<std::pair<CBcConfigPresets *, IStorage *> *>(pUser);
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "%s/%s", CONFIGS_DIR, pInfo->m_pName);
	IOHANDLE File = pList->second->OpenFile(aPath, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(File)
	{
		CLineReader Reader;
		if(Reader.OpenFile(File))
		{
			const char *pLine = Reader.Get();
			int Value = 0;
			if(pLine && sscanf(pLine, "# changes %d", &Value) == 1)
				Preset.m_Changes = Value;
		}
	}
	pList->first->m_vPresets.push_back(Preset);
	return 0;
}

static void CountChangedVariable(SConfigVariable *pVariable, void *pUserData)
{
	if(pVariable->m_ConfigDomain == ConfigDomain::BESTCLIENT && (pVariable->m_Flags & CFGFLAG_SAVE) != 0 && !pVariable->IsDefault())
		++*static_cast<int *>(pUserData);
}

static void ResetBestClientVariable(SConfigVariable *pVariable, void *)
{
	if(pVariable->m_ConfigDomain == ConfigDomain::BESTCLIENT && (pVariable->m_Flags & CFGFLAG_SAVE) != 0)
		pVariable->ResetToDefault();
}

static void WritePresetLine(IOHANDLE File, const char *pLine)
{
	io_write(File, pLine, str_length(pLine));
	io_write_newline(File);
}

static void WritePresetVariable(SConfigVariable *pVariable, void *pUserData)
{
	if(pVariable->m_ConfigDomain != ConfigDomain::BESTCLIENT)
		return;
	if((pVariable->m_Flags & CFGFLAG_SAVE) == 0 || pVariable->IsDefault())
		return;
	char aBuf[2048];
	pVariable->Serialize(aBuf, sizeof(aBuf));
	WritePresetLine(static_cast<IOHANDLE>(pUserData), aBuf);
}

bool CBcConfigPresets::WriteFile(const char *pName)
{
	char aPath[IO_MAX_PATH_LENGTH];
	if(!MakePath(aPath, sizeof(aPath), pName))
		return false;
	Storage()->CreateFolder("BestClient", IStorage::TYPE_SAVE);
	Storage()->CreateFolder(CONFIGS_DIR, IStorage::TYPE_SAVE);

	char aTmpPath[IO_MAX_PATH_LENGTH];
	IOHANDLE File = Storage()->OpenFile(IStorage::FormatTmpPath(aTmpPath, sizeof(aTmpPath), aPath), IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return false;

	char aCount[64];
	str_format(aCount, sizeof(aCount), "# changes %d", CountChanged());
	WritePresetLine(File, aCount);
	ConfigManager()->VisitVariables(WritePresetVariable, File);
	if(io_sync(File) != 0 || io_close(File) != 0)
	{
		Storage()->RemoveFile(aTmpPath, IStorage::TYPE_SAVE);
		return false;
	}
	if(!Storage()->RenameFile(aTmpPath, aPath, IStorage::TYPE_SAVE))
	{
		Storage()->RemoveFile(aTmpPath, IStorage::TYPE_SAVE);
		return false;
	}
	return true;
}

void CBcConfigPresets::OnConsoleInit()
{
	Storage()->CreateFolder("BestClient", IStorage::TYPE_SAVE);
	Storage()->CreateFolder(CONFIGS_DIR, IStorage::TYPE_SAVE);
	Refresh();
}

void CBcConfigPresets::Refresh()
{
	m_vPresets.clear();
	std::pair<CBcConfigPresets *, IStorage *> List{this, Storage()};
	Storage()->ListDirectoryInfo(IStorage::TYPE_SAVE, CONFIGS_DIR, ListCallback, &List);
	std::sort(m_vPresets.begin(), m_vPresets.end(), [](const CBcConfigPreset &A, const CBcConfigPreset &B) {
		return str_comp_nocase(A.m_aName, B.m_aName) < 0;
	});
}

bool CBcConfigPresets::Exists(const char *pName) const
{
	for(const CBcConfigPreset &Preset : m_vPresets)
	{
		if(str_comp(Preset.m_aName, pName) == 0)
			return true;
	}
	return false;
}

bool CBcConfigPresets::SaveNew(const char *pName)
{
	if(!NameOk(pName) || Exists(pName))
		return false;
	if(!WriteFile(pName))
		return false;
	Refresh();
	return Exists(pName);
}

bool CBcConfigPresets::Override(const char *pName)
{
	if(!NameOk(pName) || !Exists(pName))
		return false;
	if(!WriteFile(pName))
		return false;
	Refresh();
	return true;
}

bool CBcConfigPresets::Load(const char *pName)
{
	char aPath[IO_MAX_PATH_LENGTH];
	if(!MakePath(aPath, sizeof(aPath), pName))
		return false;

	ConfigManager()->VisitVariables(ResetBestClientVariable, nullptr);
	if(!Console()->ExecuteFile(aPath, IConsole::CLIENT_ID_UNSPECIFIED, true, IStorage::TYPE_SAVE))
		return false;
	ConfigManager()->Save();
	return true;
}

int CBcConfigPresets::CountChanged() const
{
	int Count = 0;
	const_cast<CBcConfigPresets *>(this)->ConfigManager()->VisitVariables(CountChangedVariable, &Count);
	return Count;
}

void CBcConfigPresets::OpenFolder() const
{
	CBcConfigPresets *pSelf = const_cast<CBcConfigPresets *>(this);
	pSelf->Storage()->CreateFolder("BestClient", IStorage::TYPE_SAVE);
	pSelf->Storage()->CreateFolder(CONFIGS_DIR, IStorage::TYPE_SAVE);
	char aPath[IO_MAX_PATH_LENGTH];
	pSelf->Storage()->GetCompletePath(IStorage::TYPE_SAVE, CONFIGS_DIR, aPath, sizeof(aPath));
	pSelf->Client()->ViewFile(aPath);
}

bool CBcConfigPresets::Delete(const char *pName)
{
	char aPath[IO_MAX_PATH_LENGTH];
	if(!MakePath(aPath, sizeof(aPath), pName))
		return false;
	if(!Storage()->RemoveFile(aPath, IStorage::TYPE_SAVE))
		return false;
	Refresh();
	return true;
}
