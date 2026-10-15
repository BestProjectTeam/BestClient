#include "warlist.h"

#include <engine/engine.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/jobs.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>

class CWarListJob : public IJob
{
public:
	CWarListJob()
	{
		Abortable(true);
	}

	class CClientQuery
	{
	public:
		bool m_Active = false;
		char m_aName[MAX_NAME_LENGTH] = "";
		char m_aClan[MAX_CLAN_LENGTH] = "";
	};

	class CWarEntryData
	{
	public:
		char m_aName[MAX_NAME_LENGTH] = "";
		char m_aClan[MAX_CLAN_LENGTH] = "";
		char m_aReason[MAX_WARLIST_REASON_LENGTH] = "";
		ColorRGBA m_Color = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		int m_WarTypeIndex = 0;
	};

	uint64_t m_JobRevision = 0;
	CClientQuery m_aClients[MAX_CLIENTS];
	std::vector<CWarEntryData> m_vEntries;
	int m_NumWarTypes = 0;

	CWarDataCache m_aResult[MAX_CLIENTS];

	void Run() override
	{
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
		{
			if(State() == STATE_ABORTED)
				return;

			if(!m_aClients[ClientId].m_Active)
				continue;

			m_aResult[ClientId].m_WarName = false;
			m_aResult[ClientId].m_WarClan = false;
			memset(m_aResult[ClientId].m_aReason, 0, sizeof(m_aResult[ClientId].m_aReason));
			m_aResult[ClientId].m_NameColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
			m_aResult[ClientId].m_ClanColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
			m_aResult[ClientId].m_WarGroupMatches.clear();
			m_aResult[ClientId].m_WarGroupMatches.resize(m_NumWarTypes, false);

			for(const CWarEntryData &Entry : m_vEntries)
			{
				if(Entry.m_aName[0] != '\0' && str_comp(m_aClients[ClientId].m_aName, Entry.m_aName) == 0)
				{
					str_copy(m_aResult[ClientId].m_aReason, Entry.m_aReason);
					m_aResult[ClientId].m_WarName = true;
					m_aResult[ClientId].m_NameColor = Entry.m_Color;
					if(Entry.m_WarTypeIndex >= 0 && Entry.m_WarTypeIndex < m_NumWarTypes)
						m_aResult[ClientId].m_WarGroupMatches[Entry.m_WarTypeIndex] = true;
				}
				else if(Entry.m_aClan[0] != '\0' && str_comp(m_aClients[ClientId].m_aClan, Entry.m_aClan) == 0)
				{
					// Name war reason has priority over clan war reason
					if(!m_aResult[ClientId].m_WarName)
						str_copy(m_aResult[ClientId].m_aReason, Entry.m_aReason);

					m_aResult[ClientId].m_WarClan = true;
					m_aResult[ClientId].m_ClanColor = Entry.m_Color;
					if(Entry.m_WarTypeIndex >= 0 && Entry.m_WarTypeIndex < m_NumWarTypes)
						m_aResult[ClientId].m_WarGroupMatches[Entry.m_WarTypeIndex] = true;
				}
			}
		}
	}
};

void CWarList::CheckHarvestJob()
{
	if(m_pWarJob && m_pWarJob->Done())
	{
		if(m_pWarJob->State() == IJob::STATE_DONE)
		{
			if(g_Config.m_TcWarList)
			{
				for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
					m_WarPlayers[ClientId] = std::move(m_pWarJob->m_aResult[ClientId]);
				m_LastProcessedRevision = m_pWarJob->m_JobRevision;
				for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
				{
					m_aLastClients[ClientId].m_Active = m_pWarJob->m_aClients[ClientId].m_Active;
					str_copy(m_aLastClients[ClientId].m_aName, m_pWarJob->m_aClients[ClientId].m_aName);
					str_copy(m_aLastClients[ClientId].m_aClan, m_pWarJob->m_aClients[ClientId].m_aClan);
				}
			}
		}
		m_pWarJob = nullptr;
	}
}

void CWarList::OnRender()
{
	CheckHarvestJob();
}

void CWarList::OnShutdown()
{
	if(m_pWarJob)
	{
		m_pWarJob->Abort();
		m_pWarJob = nullptr;
	}
}

void CWarList::OnReset()
{
	if(m_pWarJob)
	{
		m_pWarJob->Abort();
		m_pWarJob = nullptr;
	}
	m_ClearedWhenDisabled = false;
	m_LastProcessedRevision = 0;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		m_aLastClients[ClientId].m_Active = false;
		m_aLastClients[ClientId].m_aName[0] = '\0';
		m_aLastClients[ClientId].m_aClan[0] = '\0';
		m_WarPlayers[ClientId] = CWarDataCache();
	}
}

void CWarList::OnNewSnapshot()
{
	UpdateWarPlayers();
}

void CWarList::OnConsoleInit()
{
	IConfigManager *pConfigManager = Kernel()->RequestInterface<IConfigManager>();
	if(pConfigManager)
		pConfigManager->RegisterCallback(ConfigSaveCallback, this, ConfigDomain::TCLIENTWARLIST);

	Console()->Register("update_war_group", "i[group_index] s[name] i[color]", CFGFLAG_CLIENT, ConUpsertWarType, this, "Update or add a specific war group");
	Console()->Register("add_war_entry", "s[group] s[name] s[clan] r[reason]", CFGFLAG_CLIENT, ConAddWarEntry, this, "Adds a specific war entry");

	Console()->Register("war_name", "s[group] s[name] ?r[reason]", CFGFLAG_CLIENT, ConName, this, "Add a name war entry");
	Console()->Register("war_clan", "s[group] s[clan] ?r[reason]", CFGFLAG_CLIENT, ConClan, this, "Add a clan war entry");
	Console()->Register("remove_war_name", "s[group] s[name]", CFGFLAG_CLIENT, ConRemoveName, this, "Remove a name war entry");
	Console()->Register("remove_war_clan", "s[group] s[clan]", CFGFLAG_CLIENT, ConRemoveClan, this, "Remove a clan war entry");

	// In-game commands
	Console()->Register("war_name_index", "i[group_index] s[name] ?r[reason]", CFGFLAG_CLIENT, ConNameIndex, this, "Remove a clan war entry");
	Console()->Register("war_clan_index", "s[group_index] s[name] ?r[reason]", CFGFLAG_CLIENT, ConClanIndex, this, "Remove a clan war entry");
	Console()->Register("remove_war_name_index", "i[group_index] s[name]", CFGFLAG_CLIENT, ConRemoveNameIndex, this, "Remove a clan war entry");
	Console()->Register("remove_war_clan_index", "s[group_index] s[name]", CFGFLAG_CLIENT, ConRemoveClanIndex, this, "Remove a clan war entry");
}

// In-game war Commands
void CWarList::ConNameIndex(IConsole::IResult *pResult, void *pUserData)
{
	int Index = pResult->GetInteger(0);
	const char *pName = pResult->GetString(1);
	const char *pReason = pResult->GetString(2);
	CWarList *pThis = static_cast<CWarList *>(pUserData);
	pThis->AddWarEntryInGame(Index, pName, pReason, false);
}
void CWarList::ConClanIndex(IConsole::IResult *pResult, void *pUserData)
{
	int Index = pResult->GetInteger(0);
	const char *pName = pResult->GetString(1);
	const char *pReason = pResult->GetString(2);
	CWarList *pThis = static_cast<CWarList *>(pUserData);
	pThis->AddWarEntryInGame(Index, pName, pReason, true);
}
void CWarList::ConRemoveNameIndex(IConsole::IResult *pResult, void *pUserData)
{
	int Index = pResult->GetInteger(0);
	const char *pName = pResult->GetString(1);
	CWarList *pThis = static_cast<CWarList *>(pUserData);
	pThis->RemoveWarEntryInGame(Index, pName, false);
}
void CWarList::ConRemoveClanIndex(IConsole::IResult *pResult, void *pUserData)
{
	int Index = pResult->GetInteger(0);
	const char *pName = pResult->GetString(1);
	CWarList *pThis = static_cast<CWarList *>(pUserData);
	pThis->RemoveWarEntryInGame(Index, pName, true);
}

void CWarList::ConRemoveNameWar(IConsole::IResult *pResult, void *pUserData) {}
void CWarList::ConRemoveClanWar(IConsole::IResult *pResult, void *pUserData) {}
void CWarList::ConNameTeam(IConsole::IResult *pResult, void *pUserData) {}
void CWarList::ConClanTeam(IConsole::IResult *pResult, void *pUserData) {}
void CWarList::ConRemoveNameTeam(IConsole::IResult *pResult, void *pUserData) {}
void CWarList::ConRemoveClanTeam(IConsole::IResult *pResult, void *pUserData) {}

// Generic Commands
void CWarList::ConName(IConsole::IResult *pResult, void *pUserData)
{
	const char *pType = pResult->GetString(0);
	const char *pName = pResult->GetString(1);
	const char *pReason = pResult->GetString(2);
	CWarList *pThis = static_cast<CWarList *>(pUserData);
	pThis->AddWarEntry(pName, "", pReason, pType);
}
void CWarList::ConClan(IConsole::IResult *pResult, void *pUserData)
{
	const char *pType = pResult->GetString(0);
	const char *pClan = pResult->GetString(1);
	const char *pReason = pResult->GetString(2);
	CWarList *pThis = static_cast<CWarList *>(pUserData);
	pThis->AddWarEntry("", pClan, pReason, pType);
}
void CWarList::ConRemoveName(IConsole::IResult *pResult, void *pUserData)
{
	const char *pType = pResult->GetString(0);
	const char *pName = pResult->GetString(1);
	CWarList *pThis = static_cast<CWarList *>(pUserData);
	pThis->RemoveWarEntry(pName, "", pType);
}
void CWarList::ConRemoveClan(IConsole::IResult *pResult, void *pUserData)
{
	const char *pType = pResult->GetString(0);
	const char *pClan = pResult->GetString(1);
	CWarList *pThis = static_cast<CWarList *>(pUserData);
	pThis->RemoveWarEntry("", pClan, pType);
}

// Backend Commands for config file
void CWarList::ConAddWarEntry(IConsole::IResult *pResult, void *pUserData)
{
	const char *pType = pResult->GetString(0);
	const char *pName = pResult->GetString(1);
	const char *pClan = pResult->GetString(2);
	const char *pReason = pResult->GetString(3);
	CWarList *pThis = static_cast<CWarList *>(pUserData);
	pThis->AddWarEntry(pName, pClan, pReason, pType);
}
void CWarList::ConUpsertWarType(IConsole::IResult *pResult, void *pUserData)
{
	int Index = pResult->GetInteger(0);
	const char *pType = pResult->GetString(1);
	unsigned int ColorInt = pResult->GetInteger(2);
	ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(ColorInt));
	CWarList *pThis = static_cast<CWarList *>(pUserData);
	pThis->UpsertWarType(Index, pType, Color);
}

void CWarList::AddWarEntryInGame(int WarType, const char *pName, const char *pReason, bool IsClan)
{
	if(str_comp(pName, "") == 0)
		return;
	if(WarType >= (int)m_WarTypes.size())
		return;

	CWarType *pWarType = m_WarTypes[WarType];
	CWarEntry Entry(pWarType);
	str_copy(Entry.m_aReason, pReason);

	if(IsClan)
	{
		for(auto &Client : GameClient()->m_aClients)
		{
			if(!Client.m_Active)
				continue;
			// Found user
			if(str_comp(Client.m_aName, pName) == 0)
			{
				if(str_comp(Client.m_aClan, "") != 0)
					str_copy(Entry.m_aClan, Client.m_aClan);
				else
				{
					char aBuf[128];
					str_format(aBuf, sizeof(aBuf), "No clan found for user \"%s\"", pName);
					GameClient()->Echo(aBuf);
					break;
				}
			}
		}
	}
	else
	{
		str_copy(Entry.m_aName, pName);
	}
	if(!g_Config.m_TcWarListAllowDuplicates)
		RemoveWarEntryDuplicates(Entry.m_aName, Entry.m_aClan);

	AddWarEntry(Entry.m_aName, Entry.m_aClan, Entry.m_aReason, Entry.m_pWarType->m_aWarName);
}

void CWarList::RemoveWarEntryInGame(int WarType, const char *pName, bool IsClan)
{
	if(str_comp(pName, "") == 0)
		return;
	if(WarType >= (int)m_WarTypes.size())
		return;

	CWarType *pWarType = m_WarTypes[WarType];
	CWarEntry Entry(pWarType);

	if(IsClan)
	{
		for(auto &Client : GameClient()->m_aClients)
		{
			if(!Client.m_Active)
				continue;
			// Found user
			if(str_comp(Client.m_aName, pName) == 0)
			{
				if(str_comp(Client.m_aClan, "") != 0)
				{
					str_copy(Entry.m_aClan, Client.m_aClan);
					break;
				}
				else
				{
					char aBuf[128];
					str_format(aBuf, sizeof(aBuf), "No clan found for user \"%s\"", pName);
					GameClient()->Echo(aBuf);
					break;
				}
			}
		}
	}
	else
	{
		str_copy(Entry.m_aName, pName);
	}
	RemoveWarEntry(Entry.m_aName, Entry.m_aClan, Entry.m_pWarType->m_aWarName);
}

void CWarList::UpdateWarEntry(int Index, const char *pName, const char *pClan, const char *pReason, CWarType *pType)
{
	if(Index >= 0 && Index < static_cast<int>(m_vWarEntries.size()))
	{
		str_copy(m_vWarEntries[Index].m_aName, pName);
		str_copy(m_vWarEntries[Index].m_aClan, pClan);
		str_copy(m_vWarEntries[Index].m_aReason, pReason);
		m_vWarEntries[Index].m_pWarType = pType;
		m_WarListRevision++;
		UpdateWarPlayers();
	}
}

void CWarList::UpsertWarType(int Index, const char *pType, ColorRGBA Color)
{
	if(str_comp(pType, "none") == 0)
		return;

	if(Index >= 0 && Index < static_cast<int>(m_WarTypes.size()))
	{
		str_copy(m_WarTypes[Index]->m_aWarName, pType);
		m_WarTypes[Index]->m_Color = Color;
		m_WarListRevision++;
		UpdateWarPlayers();
	}
	else
	{
		AddWarType(pType, Color);
	}
}

void CWarList::AddWarEntry(const char *pName, const char *pClan, const char *pReason, const char *pType)
{
	if(str_comp(pName, "") == 0 && str_comp(pClan, "") == 0)
		return;

	CWarType *WarType = FindWarType(pType);
	if(WarType == m_pWarTypeNone)
	{
		AddWarType(pType, ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f));
		WarType = FindWarType(pType);
	}

	CWarEntry Entry(WarType);
	str_copy(Entry.m_aReason, pReason);

	if(str_comp(pClan, "") != 0)
		str_copy(Entry.m_aClan, pClan);
	else if(str_comp(pName, "") != 0)
		str_copy(Entry.m_aName, pName);

	if(!g_Config.m_TcWarListAllowDuplicates)
		RemoveWarEntryDuplicates(pName, pClan);
	m_vWarEntries.push_back(Entry);
	m_WarListRevision++;
	UpdateWarPlayers();
}

void CWarList::RemoveWarEntryDuplicates(const char *pName, const char *pClan)
{
	if(str_comp(pName, "") == 0 && str_comp(pClan, "") == 0)
		return;

	bool Removed = false;
	for(auto It = m_vWarEntries.begin(); It != m_vWarEntries.end();)
	{
		bool IsDuplicate =
			(str_comp(It->m_aName, pName) == 0) &&
			(str_comp(It->m_aClan, pClan) == 0);

		if(IsDuplicate)
		{
			It = m_vWarEntries.erase(It);
			Removed = true;
		}
		else
			++It;
	}
	if(Removed)
	{
		m_WarListRevision++;
		UpdateWarPlayers();
	}
}

void CWarList::AddWarType(const char *pType, ColorRGBA Color)
{
	if(str_comp(pType, "none") == 0)
		return;

	CWarType *Type = FindWarType(pType);
	if(Type == m_pWarTypeNone)
	{
		CWarType *NewType = new CWarType(pType, Color);
		m_WarTypes.push_back(NewType);
	}
	else
	{
		Type->m_Color = Color;
	}
	m_WarListRevision++;
	UpdateWarPlayers();
}

void CWarList::RemoveWarEntry(const char *pName, const char *pClan, const char *pType)
{
	CWarType *pWarType = FindWarType(pType);
	CWarEntry Entry(pWarType, pName, pClan, "");
	auto It = std::find(m_vWarEntries.begin(), m_vWarEntries.end(), Entry);
	if(It != m_vWarEntries.end())
	{
		m_vWarEntries.erase(It);
		m_WarListRevision++;
		UpdateWarPlayers();
	}
}

void CWarList::RemoveWarEntry(CWarEntry *Entry)
{
	auto It = std::find_if(m_vWarEntries.begin(), m_vWarEntries.end(),
		[Entry](const CWarEntry &WarEntry) { return &WarEntry == Entry; });
	if(It != m_vWarEntries.end())
	{
		m_vWarEntries.erase(It);
		m_WarListRevision++;
		UpdateWarPlayers();
	}
}

void CWarList::RemoveWarEntry(int Index)
{
	if(Index >= 0 && Index < static_cast<int>(m_vWarEntries.size()))
	{
		m_vWarEntries.erase(m_vWarEntries.begin() + Index);
		m_WarListRevision++;
		UpdateWarPlayers();
	}
}

void CWarList::RemoveWarType(const char *pType)
{
	CWarType Type(pType);

	auto It = std::find_if(m_WarTypes.begin(), m_WarTypes.end(),
		[&Type](CWarType *pWarTypePtr) { return *pWarTypePtr == Type; });
	if(It != m_WarTypes.end())
	{
		// Don't remove default war types
		if(!(*It)->m_Removable)
			return;

		// Find all war entries and set them to None if they are using this type
		for(CWarEntry &Entry : m_vWarEntries)
		{
			if(*Entry.m_pWarType == **It)
			{
				Entry.m_pWarType = m_pWarTypeNone;
			}
		}
		m_WarTypes.erase(It);
		m_WarListRevision++;
		UpdateWarPlayers();
	}
}

void CWarList::RemoveWarType(int Index)
{
	if(Index >= 0 && Index < static_cast<int>(m_WarTypes.size()))
	{
		if(!m_WarTypes[Index]->m_Removable)
			return;
		RemoveWarType(m_WarTypes[Index]->m_aWarName);
	}
}

CWarType *CWarList::FindWarType(const char *pType)
{
	CWarType Type(pType);
	auto It = std::find_if(m_WarTypes.begin(), m_WarTypes.end(),
		[&Type](CWarType *pWarTypePtr) { return *pWarTypePtr == Type; });
	if(It != m_WarTypes.end())
		return *It;
	else
		return m_pWarTypeNone;
}

CWarEntry *CWarList::FindWarEntry(const char *pName, const char *pClan, const char *pType)
{
	CWarType *pWarType = FindWarType(pType);
	CWarEntry Entry(pWarType, pName, pClan, "");
	auto It = std::find(m_vWarEntries.begin(), m_vWarEntries.end(), Entry);

	if(It != m_vWarEntries.end())
		return &(*It);
	else
		return nullptr;
}

ColorRGBA CWarList::GetPriorityColor(int ClientId)
{
	if(m_WarPlayers[ClientId].m_WarClan && !m_WarPlayers[ClientId].m_WarName)
		return m_WarPlayers[ClientId].m_ClanColor;
	else
		return m_WarPlayers[ClientId].m_NameColor;
}

ColorRGBA CWarList::GetNameplateColor(int ClientId)
{
	return m_WarPlayers[ClientId].m_NameColor;
}

ColorRGBA CWarList::GetClanColor(int ClientId)
{
	return m_WarPlayers[ClientId].m_ClanColor;
}

bool CWarList::GetAnyWar(int ClientId)
{
	if(ClientId < 0)
		return false;
	return m_WarPlayers[ClientId].m_WarClan || m_WarPlayers[ClientId].m_WarName;
}

bool CWarList::GetNameWar(int ClientId)
{
	if(ClientId < 0)
		return false;
	return m_WarPlayers[ClientId].m_WarName;
}
bool CWarList::GetClanWar(int ClientId)
{
	if(ClientId < 0)
		return false;
	return m_WarPlayers[ClientId].m_WarClan;
}

void CWarList::GetReason(char *pReason, int ClientId)
{
	str_copy(pReason, m_WarPlayers[ClientId].m_aReason, sizeof(m_WarPlayers[ClientId].m_aReason));
}

CWarDataCache &CWarList::GetWarData(int ClientId)
{
	return m_WarPlayers[ClientId];
}

void CWarList::SortWarEntries()
{
	// TODO
}

void CWarList::UpdateWarPlayers()
{
	CheckHarvestJob();

	if(!g_Config.m_TcWarList)
	{
		if(!m_ClearedWhenDisabled)
		{
			if(m_pWarJob)
			{
				m_pWarJob->Abort();
				m_pWarJob = nullptr;
			}
			for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
			{
				m_WarPlayers[ClientId] = CWarDataCache();
				m_aLastClients[ClientId].m_Active = false;
				m_aLastClients[ClientId].m_aName[0] = '\0';
				m_aLastClients[ClientId].m_aClan[0] = '\0';
			}
			m_LastProcessedRevision = 0;
			m_ClearedWhenDisabled = true;
		}
		return;
	}
	m_ClearedWhenDisabled = false;

	if(m_pWarJob)
		return;

	bool Changed = (m_WarListRevision != m_LastProcessedRevision);
	if(!Changed)
	{
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
		{
			const auto &Client = GameClient()->m_aClients[ClientId];
			if(Client.m_Active != m_aLastClients[ClientId].m_Active ||
				(Client.m_Active && (str_comp(Client.m_aName, m_aLastClients[ClientId].m_aName) != 0 ||
							    str_comp(Client.m_aClan, m_aLastClients[ClientId].m_aClan) != 0)))
			{
				Changed = true;
				break;
			}
		}
	}

	if(!Changed)
		return;

	for(size_t Index = 0; Index < m_WarTypes.size(); ++Index)
		m_WarTypes[Index]->m_Index = Index;

	auto pJob = std::make_shared<CWarListJob>();
	pJob->m_JobRevision = m_WarListRevision;
	pJob->m_NumWarTypes = (int)m_WarTypes.size();

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		const auto &Client = GameClient()->m_aClients[ClientId];
		pJob->m_aClients[ClientId].m_Active = Client.m_Active;
		if(Client.m_Active)
		{
			str_copy(pJob->m_aClients[ClientId].m_aName, Client.m_aName);
			str_copy(pJob->m_aClients[ClientId].m_aClan, Client.m_aClan);
		}
		else
		{
			pJob->m_aClients[ClientId].m_aName[0] = '\0';
			pJob->m_aClients[ClientId].m_aClan[0] = '\0';
		}
	}

	pJob->m_vEntries.reserve(m_vWarEntries.size());
	for(const auto &Entry : m_vWarEntries)
	{
		CWarListJob::CWarEntryData Data;
		str_copy(Data.m_aName, Entry.m_aName);
		str_copy(Data.m_aClan, Entry.m_aClan);
		str_copy(Data.m_aReason, Entry.m_aReason);
		if(Entry.m_pWarType)
		{
			Data.m_Color = Entry.m_pWarType->m_Color;
			Data.m_WarTypeIndex = Entry.m_pWarType->m_Index;
		}
		pJob->m_vEntries.push_back(Data);
	}

	m_pWarJob = pJob;
	if(Engine())
		Engine()->AddJob(pJob);
	else
	{
		pJob->Run();
		CheckHarvestJob();
	}
}

CWarList::~CWarList()
{
	if(m_pWarJob)
	{
		m_pWarJob->Abort();
		m_pWarJob = nullptr;
	}
	for(CWarType *WarType : m_WarTypes)
		delete WarType;
	m_WarTypes.clear();
}

CWarList::CWarList()
{
	str_copy(m_WarTypes[0]->m_aWarName, "none");
	m_WarTypes[0]->m_Color = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
}

static inline void EscapeParam(char *pDst, const char *pSrc, int Size)
{
	str_escape(&pDst, pSrc, pDst + Size);
}

void CWarList::ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData)
{
	CWarList *pThis = (CWarList *)pUserData;

	char aBuf[1024];
	for(int i = 0; i < static_cast<int>(pThis->m_WarTypes.size()); i++)
	{
		CWarType &WarType = *pThis->m_WarTypes[i];

		// Imported wartypes don't get saved
		if(WarType.m_Imported)
			continue;

		char aEscapeType[MAX_WARLIST_TYPE_LENGTH * 2];
		EscapeParam(aEscapeType, WarType.m_aWarName, sizeof(aEscapeType));
		ColorHSLA Color = color_cast<ColorHSLA>(WarType.m_Color);

		str_format(aBuf, sizeof(aBuf), "update_war_group %d \"%s\" %d", i, aEscapeType, Color.Pack(false));
		pConfigManager->WriteLine(aBuf, ConfigDomain::TCLIENTWARLIST);
	}
	for(CWarEntry &Entry : pThis->m_vWarEntries)
	{
		// Imported entries don't get saved
		if(Entry.m_Imported)
			continue;

		char aEscapeType[MAX_WARLIST_TYPE_LENGTH * 2];
		char aEscapeName[MAX_NAME_LENGTH * 2];
		char aEscapeClan[MAX_CLAN_LENGTH * 2];
		char aEscapeReason[MAX_WARLIST_REASON_LENGTH * 2];
		EscapeParam(aEscapeType, Entry.m_pWarType->m_aWarName, sizeof(aEscapeType));
		EscapeParam(aEscapeName, Entry.m_aName, sizeof(aEscapeName));
		EscapeParam(aEscapeClan, Entry.m_aClan, sizeof(aEscapeClan));
		EscapeParam(aEscapeReason, Entry.m_aReason, sizeof(aEscapeReason));

		str_format(aBuf, sizeof(aBuf), "add_war_entry \"%s\" \"%s\" \"%s\" \"%s\"", aEscapeType, aEscapeName, aEscapeClan, aEscapeReason);
		pConfigManager->WriteLine(aBuf, ConfigDomain::TCLIENTWARLIST);
	}
}
