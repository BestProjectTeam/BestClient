/* Copyright © 2026 BestProject Team */
#include "browser_cache.h"

#include <base/net.h>
#include <base/system.h>

#include <engine/external/json-parser/json.h>

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace
{
struct CParsedPlayerInfo
{
	bool m_Developer = false;
	std::string m_Version;
};

using TParsedPlayers = std::unordered_map<std::string, std::unordered_map<std::string, CParsedPlayerInfo>>;

static const char *GetStringField(const json_value &Json, const char *pField)
{
	const json_value &Field = Json[pField];
	return Field.type == json_string ? Field.u.string.ptr : nullptr;
}

static bool GetBoolField(const json_value &Json, const char *pField)
{
	const json_value &Field = Json[pField];
	return Field.type == json_boolean && Field.u.boolean != 0;
}

static const char *GetOptionalStringField(const json_value &Json, const char *pField)
{
	const json_value &Field = Json[pField];
	return Field.type == json_string ? Field.u.string.ptr : nullptr;
}

static bool NormalizeServerAddress(const char *pAddress, char *pBuffer, int BufferSize)
{
	if(!pAddress || pAddress[0] == '\0')
		return false;

	while(*pAddress != '\0' && str_isspace(*pAddress))
		++pAddress;

	char aToken[MAX_SERVER_ADDRESSES * NETADDR_MAXSTRSIZE];
	int Length = 0;
	while(pAddress[Length] != '\0' && pAddress[Length] != ',')
		++Length;
	while(Length > 0 && str_isspace(pAddress[Length - 1]))
		--Length;
	str_truncate(aToken, sizeof(aToken), pAddress, Length);
	if(aToken[0] == '\0')
		return false;

	NETADDR Addr;
	if(net_addr_from_url(&Addr, aToken, nullptr, 0) == 0 || net_addr_from_str(&Addr, aToken) == 0)
	{
		net_addr_str(&Addr, pBuffer, BufferSize, true);
		return true;
	}

	str_copy(pBuffer, aToken, BufferSize);
	return true;
}

static void AddPlayer(TParsedPlayers &Map, const char *pServerAddress, const char *pName, bool Developer = false, const char *pVersion = nullptr)
{
	if(!pServerAddress || !pName || pServerAddress[0] == '\0' || pName[0] == '\0')
		return;

	char aNormalizedAddress[MAX_SERVER_ADDRESSES * NETADDR_MAXSTRSIZE];
	if(!NormalizeServerAddress(pServerAddress, aNormalizedAddress, sizeof(aNormalizedAddress)))
		return;

	CParsedPlayerInfo &Info = Map[aNormalizedAddress][pName];
	Info.m_Developer = Info.m_Developer || Developer;
	if(pVersion && pVersion[0] != '\0' && Info.m_Version.empty())
		Info.m_Version = pVersion;
}

static void ParsePlayerObject(TParsedPlayers &Map, const char *pServerAddress, const json_value &Player)
{
	if(Player.type != json_object)
		return;
	const bool Developer = GetBoolField(Player, "developer");
	const char *pVersion = GetOptionalStringField(Player, "version");

	if(const char *pName = GetStringField(Player, "name"))
	{
		AddPlayer(Map, pServerAddress, pName, Developer, pVersion);
		return;
	}
	if(const char *pName = GetStringField(Player, "player_name"))
	{
		AddPlayer(Map, pServerAddress, pName, Developer, pVersion);
		return;
	}

	for(unsigned int i = 0; i < Player.u.object.length; ++i)
	{
		const auto &Entry = Player.u.object.values[i];
		if(Entry.value->type == json_object)
		{
			if(const char *pName = GetStringField(*Entry.value, "name"))
				AddPlayer(Map, pServerAddress, pName, GetBoolField(*Entry.value, "developer"), GetOptionalStringField(*Entry.value, "version"));
		}
		else if(Entry.value->type == json_string)
		{
			AddPlayer(Map, pServerAddress, Entry.value->u.string.ptr);
		}
		else
		{
			AddPlayer(Map, pServerAddress, Entry.name);
		}
	}
}

static void ParsePlayersValue(TParsedPlayers &Map, const char *pServerAddress, const json_value &Players)
{
	if(!pServerAddress || pServerAddress[0] == '\0')
		return;

	if(Players.type == json_array)
	{
		for(unsigned int i = 0; i < Players.u.array.length; ++i)
		{
			const json_value &Player = *Players.u.array.values[i];
			if(Player.type == json_string)
				AddPlayer(Map, pServerAddress, Player.u.string.ptr);
			else if(Player.type == json_object)
				ParsePlayerObject(Map, pServerAddress, Player);
		}
	}
	else if(Players.type == json_object)
	{
		ParsePlayerObject(Map, pServerAddress, Players);
	}
}
}

bool CBrowserCache::Load(const json_value &Json)
{
	TParsedPlayers Parsed;

	if(Json.type == json_array)
	{
		for(unsigned int i = 0; i < Json.u.array.length; ++i)
		{
			const json_value &Entry = *Json.u.array.values[i];
			if(Entry.type != json_object)
				continue;
			const char *pServerAddress = GetStringField(Entry, "server_address");
			if(!pServerAddress)
				pServerAddress = GetStringField(Entry, "address");
			if(!pServerAddress)
				pServerAddress = GetStringField(Entry, "server");
			if(!pServerAddress)
				continue;

			const json_value &Players = Entry["players"];
			if(Players.type != json_none)
				ParsePlayersValue(Parsed, pServerAddress, Players);

			if(const char *pName = GetStringField(Entry, "name"))
				AddPlayer(Parsed, pServerAddress, pName);
			if(const char *pName = GetStringField(Entry, "player_name"))
				AddPlayer(Parsed, pServerAddress, pName);
		}
	}
	else if(Json.type == json_object)
	{
		for(unsigned int i = 0; i < Json.u.object.length; ++i)
		{
			const auto &Entry = Json.u.object.values[i];
			if(Entry.value->type == json_object || Entry.value->type == json_array)
				ParsePlayersValue(Parsed, Entry.name, *Entry.value);
		}
	}

	m_vPlayers.clear();
	m_PlayerVersionsByServer.clear();
	m_DeveloperByServer.clear();
	for(const auto &ServerEntry : Parsed)
	{
		for(const auto &Name : ServerEntry.second)
		{
			IServerBrowser::CBestClientPlayerEntry Entry;
			mem_zero(&Entry, sizeof(Entry));
			str_copy(Entry.m_aServerAddress, ServerEntry.first.c_str(), sizeof(Entry.m_aServerAddress));
			str_copy(Entry.m_aName, Name.first.c_str(), sizeof(Entry.m_aName));
			Entry.m_Developer = Name.second.m_Developer;
			m_PlayerVersionsByServer[ServerEntry.first][Name.first] = Name.second.m_Version.empty() ? "under" : Name.second.m_Version;
			m_DeveloperByServer[ServerEntry.first][Name.first] = Name.second.m_Developer;
			m_vPlayers.push_back(Entry);
		}
	}
	return true;
}

bool CBrowserCache::HasPlayer(const char *pServerAddress, const char *pName, bool *pDeveloper) const
{
	if(pDeveloper)
		*pDeveloper = false;
	if(!pServerAddress || !pName || pServerAddress[0] == '\0' || pName[0] == '\0')
		return false;

	char aNormalizedAddress[MAX_SERVER_ADDRESSES * NETADDR_MAXSTRSIZE];
	if(!NormalizeServerAddress(pServerAddress, aNormalizedAddress, sizeof(aNormalizedAddress)))
		return false;

	const auto ServerIt = m_DeveloperByServer.find(aNormalizedAddress);
	if(ServerIt == m_DeveloperByServer.end())
		return false;

	const auto PlayerIt = ServerIt->second.find(pName);
	if(PlayerIt == ServerIt->second.end())
		return false;

	if(pDeveloper)
		*pDeveloper = PlayerIt->second;
	return true;
}

bool CBrowserCache::GetPlayerVersion(const char *pServerAddress, const char *pName, char *pVersion, int VersionSize) const
{
	if(!pVersion || VersionSize <= 0)
		return false;
	pVersion[0] = '\0';
	if(!pServerAddress || !pName || pServerAddress[0] == '\0' || pName[0] == '\0')
		return false;

	char aNormalizedAddress[MAX_SERVER_ADDRESSES * NETADDR_MAXSTRSIZE];
	if(!NormalizeServerAddress(pServerAddress, aNormalizedAddress, sizeof(aNormalizedAddress)))
		return false;

	const auto ServerIt = m_PlayerVersionsByServer.find(aNormalizedAddress);
	if(ServerIt == m_PlayerVersionsByServer.end())
		return false;

	const auto PlayerIt = ServerIt->second.find(pName);
	if(PlayerIt == ServerIt->second.end() || PlayerIt->second.empty())
		return false;

	str_copy(pVersion, PlayerIt->second.c_str(), VersionSize);
	return true;
}
