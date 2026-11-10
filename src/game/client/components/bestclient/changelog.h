/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_CHANGELOG_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_CHANGELOG_H

#include <memory>
#include <string>
#include <vector>

class IHttp;
class IHttpRequest;

class CChangelog
{
public:
	static constexpr int MAX_RELEASES = 20;

	struct CRelease
	{
		std::string m_Tag;
		std::string m_Title;
		std::string m_Date;
		std::string m_Body;
		std::string m_Url;
		bool m_Prerelease = false;
		bool m_Latest = false;
	};

	void Update();
	void Shutdown();
	void NotifyPageOpen(IHttp *pHttp);
	void RequestRefresh(IHttp *pHttp);

	bool Loading() const { return m_Loading; }
	bool HasError() const { return !m_Error.empty(); }
	const char *Error() const { return m_Error.c_str(); }
	const std::vector<CRelease> &Releases() const { return m_vReleases; }

private:
	void Start(IHttp *pHttp);

	std::shared_ptr<IHttpRequest> m_pTask;
	std::vector<CRelease> m_vReleases;
	std::string m_Error;
	int64_t m_LastFetchTime = 0;
	bool m_OpenLastFrame = false;
	bool m_OpenThisFrame = false;
	bool m_Loading = false;
};

#endif
