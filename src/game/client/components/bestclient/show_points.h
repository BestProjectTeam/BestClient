/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_SHOW_POINTS_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_SHOW_POINTS_H

#include <game/client/component.h>

#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class IHttpRequest;

class CShowPoints : public CComponent
{
public:
	enum class EProvider
	{
		None = 0,
		Ddnet,
		Ego,
	};

	int Sizeof() const override { return sizeof(*this); }

	void OnReset() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnUpdate() override;

	bool Enabled() const;
	bool ActiveOnCurrentServer() const;
	EProvider CurrentProvider() const;

	bool TryGetPoints(const char *pName, int *pPoints) const;
	void RequestPoints(const char *pName);

private:
	enum
	{
		PROVIDER_COUNT = 2, // Ddnet, Ego
		MAX_CONCURRENT = 3,
		MAX_QUEUE = 64,
		SUCCESS_TTL_MS = 10 * 60 * 1000,
		FAILURE_TTL_MS = 30 * 1000,
		COMMUNITY_ID_LENGTH = 32,
	};

	struct SCacheEntry
	{
		int m_Points = 0;
		bool m_HasPoints = false;
		bool m_Failed = false;
		int64_t m_FetchedAt = 0;
	};

	struct SInFlight
	{
		std::string m_Name;
		EProvider m_Provider = EProvider::None;
		std::shared_ptr<IHttpRequest> m_pTask;
	};

	std::unordered_map<std::string, SCacheEntry> m_aCache[PROVIDER_COUNT];
	std::deque<std::string> m_Queue;
	std::vector<SInFlight> m_vInFlight;
	EProvider m_LastProvider = EProvider::None;
	mutable char m_aCommunityIdBuf[COMMUNITY_ID_LENGTH] = "";

	static int ProviderIndex(EProvider Provider);
	static void MakeLowerAscii(char *pBuf, int Size, const char *pSrc);

	const char *CurrentCommunityId() const;
	bool IsCacheFresh(const SCacheEntry &Entry) const;
	bool IsQueuedOrInFlight(const std::string &Name) const;
	void ClearPending();
	void ProcessInFlight();
	void PumpQueue();
	void StartRequest(const std::string &Name, EProvider Provider);
};

#endif
