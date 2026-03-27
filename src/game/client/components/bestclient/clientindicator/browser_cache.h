#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_CLIENTINDICATOR_BROWSER_CACHE_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_CLIENTINDICATOR_BROWSER_CACHE_H

#include <engine/serverbrowser.h>

#include <vector>

typedef struct _json_value json_value;

class CBrowserCache
{
	std::vector<IServerBrowser::CBestClientPlayerEntry> m_vPlayers;

public:
	bool Load(const json_value &Json);
	void Clear() { m_vPlayers.clear(); }
	const std::vector<IServerBrowser::CBestClientPlayerEntry> &Players() const { return m_vPlayers; }
};

#endif
