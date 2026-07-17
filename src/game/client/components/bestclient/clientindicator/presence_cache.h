/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_CLIENTINDICATOR_PRESENCE_CACHE_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_CLIENTINDICATOR_PRESENCE_CACHE_H

#include <string>
#include <unordered_set>
#include <vector>

class CPresenceCache
{
	std::string m_ServerAddress;
	std::unordered_set<int> m_PresentClientIds;

public:
	void Clear();
	bool SetServerAddress(const std::string &ServerAddress);
	const std::string &ServerAddress() const { return m_ServerAddress; }
	void Replace(const std::vector<int> &vClientIds);
	void SetPresent(int ClientId, bool Present);
	bool IsPresent(int ClientId) const;
};

#endif
