#ifndef ENGINE_CLIENT_SERVERBROWSER_HTTP_H
#define ENGINE_CLIENT_SERVERBROWSER_HTTP_H
#include <base/types.h>

class CServerInfo;
class IEngine;
class IStorage;
class IHttp;

class IServerBrowserHttp
{
public:
	virtual ~IServerBrowserHttp() = default;

	virtual void Update() = 0;

	virtual bool IsRefreshing() const = 0;
	virtual bool IsError() const = 0;
	virtual void Refresh() = 0;

	virtual bool GetBestUrl(const char **pBestUrl) const = 0;

	// True when the latest completed refresh produced a new server list payload
	// that should be applied. Unchanged responses (same SHA-256) stay false so
	// the browser can skip expensive rebuilds on auto-refresh.
	virtual bool ServersDataChanged() const = 0;

	virtual int NumServers() const = 0;
	virtual const CServerInfo &Server(int Index) const = 0;
};

IServerBrowserHttp *CreateServerBrowserHttp(IEngine *pEngine, IStorage *pStorage, IHttp *pHttp, const char *pPreviousBestUrl);
#endif // ENGINE_CLIENT_SERVERBROWSER_HTTP_H
