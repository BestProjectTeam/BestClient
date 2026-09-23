/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_BROWSER_UTILS_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_BROWSER_UTILS_H

#include <game/client/component.h>

#include <cstddef>

class CServerInfo;

class CBrowserUtils : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }

	void OnUpdate() override;

	template<size_t N>
	static const char *GetDisplayName(const CServerInfo *pInfo, char (&aBuffer)[N])
	{
		return GetDisplayName(pInfo, aBuffer, N);
	}

private:
	int64_t m_LastServerBrowserRefreshTick = 0;

	static const char *GetDisplayName(const CServerInfo *pInfo, char *pBuffer, size_t BufferSize);
};

#endif
