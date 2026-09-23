/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_CLIENTINDICATOR_CLIENT_INDICATOR_SYNC_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_CLIENTINDICATOR_CLIENT_INDICATOR_SYNC_H

#include <engine/client/enums.h>
#include <engine/shared/protocol.h>

#include <array>
#include <vector>

namespace BestClientIndicatorClient
{
struct CLocalClientIdSnapshot
{
	std::array<bool, MAX_CLIENTS> m_aActive{};
	std::vector<int> m_vClientIds;

	bool Contains(int ClientId) const
	{
		return ClientId >= 0 && ClientId < MAX_CLIENTS && m_aActive[ClientId];
	}
};

inline CLocalClientIdSnapshot CollectActiveLocalClientIds(const int (&aLocalIds)[NUM_DUMMIES], const std::array<bool, MAX_CLIENTS> &aClientActive)
{
	CLocalClientIdSnapshot Snapshot;
	Snapshot.m_vClientIds.reserve(NUM_DUMMIES);
	for(const int ClientId : aLocalIds)
	{
		if(ClientId < 0 || ClientId >= MAX_CLIENTS || !aClientActive[ClientId] || Snapshot.m_aActive[ClientId])
			continue;
		Snapshot.m_aActive[ClientId] = true;
		Snapshot.m_vClientIds.push_back(ClientId);
	}
	return Snapshot;
}
}

#endif
