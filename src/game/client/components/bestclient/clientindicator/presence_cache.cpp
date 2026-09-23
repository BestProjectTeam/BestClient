/* Copyright © 2026 BestProject Team */
#include "presence_cache.h"

void CPresenceCache::Clear()
{
	m_ServerAddress.clear();
	m_PresentClientIds.clear();
}

bool CPresenceCache::SetServerAddress(const std::string &ServerAddress)
{
	if(m_ServerAddress == ServerAddress)
		return false;
	m_ServerAddress = ServerAddress;
	m_PresentClientIds.clear();
	return true;
}

void CPresenceCache::Replace(const std::vector<int> &vClientIds)
{
	m_PresentClientIds.clear();
	for(const int ClientId : vClientIds)
		m_PresentClientIds.insert(ClientId);
}

void CPresenceCache::SetPresent(int ClientId, bool Present)
{
	if(Present)
		m_PresentClientIds.insert(ClientId);
	else
		m_PresentClientIds.erase(ClientId);
}

bool CPresenceCache::IsPresent(int ClientId) const
{
	return m_PresentClientIds.find(ClientId) != m_PresentClientIds.end();
}
