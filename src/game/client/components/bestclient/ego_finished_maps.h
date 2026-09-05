/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_EGO_FINISHED_MAPS_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_EGO_FINISHED_MAPS_H

#include <game/client/component.h>

#include <memory>
#include <string>
#include <unordered_set>

class CHttpRequest;
class CServerInfo;

class CEgoFinishedMaps : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }

	void OnReset() override;
	void OnUpdate() override;

	bool IsFinishedMap(const CServerInfo *pServer) const;

private:
	enum
	{
		PLAYER_NAME_LENGTH = 64,
		RETRY_DELAY_SECONDS = 30,
	};

	std::unordered_set<std::string> m_FinishedMaps;
	std::shared_ptr<CHttpRequest> m_pTask;
	char m_aPlayerName[PLAYER_NAME_LENGTH] = "";
	char m_aLoadedPlayerName[PLAYER_NAME_LENGTH] = "";
	int64_t m_NextRetry = 0;

	bool Enabled() const;
	void ResetRequest();
	void StartRequest();
	void ProcessRequest();
};

#endif
