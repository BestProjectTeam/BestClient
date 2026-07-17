/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_CLIENTINDICATOR_BROWSER_CACHE_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_CLIENTINDICATOR_BROWSER_CACHE_H

#include <engine/serverbrowser.h>

#include <string>
#include <unordered_map>
#include <vector>

typedef struct _json_value json_value;

class CBrowserCache
{
	std::vector<IServerBrowser::CBestClientPlayerEntry> m_vPlayers;
	std::unordered_map<std::string, std::unordered_map<std::string, std::string>> m_PlayerVersionsByServer;
	std::unordered_map<std::string, std::unordered_map<std::string, bool>> m_DeveloperByServer;

public:
	bool Load(const json_value &Json);
	void Clear()
	{
		m_vPlayers.clear();
		m_PlayerVersionsByServer.clear();
		m_DeveloperByServer.clear();
	}
	const std::vector<IServerBrowser::CBestClientPlayerEntry> &Players() const { return m_vPlayers; }
	bool HasPlayer(const char *pServerAddress, const char *pName, bool *pDeveloper = nullptr) const;
	bool GetPlayerVersion(const char *pServerAddress, const char *pName, char *pVersion, int VersionSize) const;
	const std::unordered_map<std::string, std::unordered_map<std::string, std::string>> &PlayerVersionsByServer() const { return m_PlayerVersionsByServer; }
};

#endif
