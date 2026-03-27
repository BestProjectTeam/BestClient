#include "rclient_indicator.h"

#include <base/system.h>

#include "engine/client.h"
#include "engine/external/json-parser/json.h"
#include "engine/serverbrowser.h"
#include "game/client/gameclient.h"

static constexpr int POLL_TIMEOUT_SECONDS = 20;
static constexpr int POLL_RETRY_SECONDS = 1;
static constexpr int TOKEN_RETRY_SECONDS = 10;
static constexpr int HTTP_TIMEOUT_MS = 25000;
static constexpr int HTTP_CONNECT_TIMEOUT_MS = 10000;
static constexpr int LONGPOLL_TIMEOUT_MS = (POLL_TIMEOUT_SECONDS + 5) * 1000;

static const char *GetRclientUsersUrl()
{
	return g_Config.m_RiRclientIndicatorUsersUrl[0] != '\0' ? g_Config.m_RiRclientIndicatorUsersUrl : nullptr;
}

static const char *GetRclientTokenUrl()
{
	return g_Config.m_RiRclientIndicatorTokenUrl[0] != '\0' ? g_Config.m_RiRclientIndicatorTokenUrl : nullptr;
}

CRClientIndicator::CRClientIndicator()
{
};

void CRClientIndicator::OnInit()
{
	if(g_Config.m_RiShowRclientIndicator)
		FetchAuthToken();
}

void CRClientIndicator::OnRender()
{
	auto HandleTaskDone = [&](std::shared_ptr<CHttpRequest> &pTask, auto &&Finish, auto &&Reset) {
		if(!pTask || pTask->State() != EHttpState::DONE)
			return;
		Finish();
		Reset();
	};

	HandleTaskDone(m_pAuthTokenTask, [this]() { FinishAuthToken(); }, [this]() { ResetAuthToken(); });
	HandleTaskDone(m_pRClientUsersTask, [this]() { FinishRClientUsers(); }, [this]() { ResetRClientUsers(); });
	HandleTaskDone(m_pRClientUsersTaskSend, [this]() { FinishRClientUsersSend(); }, [this]() { ResetRClientUsersSend(); });

	if(!g_Config.m_RiShowRclientIndicator)
	{
		if(m_WasOnline && !m_LastServerAddress.empty() && m_LastLocalId >= 0)
			SendPlayerData(m_LastServerAddress.c_str(), m_LastLocalId, m_LastDummyId, false);

		ResetRClientUsers();
		ClearUsers();
		m_WasOnline = false;
		m_LastServerAddress.clear();
		m_LastLocalId = -1;
		m_LastDummyId = -1;
		m_LastPollAttempt = 0;
		m_ServerRev = 0;
		return;
	}

	const int64_t Now = time_get();
	if(m_aAuthToken[0] == '\0' && Now - m_LastTokenAttempt > time_freq() * TOKEN_RETRY_SECONDS)
	{
		m_LastTokenAttempt = Now;
		FetchAuthToken();
	}

	const bool Online = Client()->State() == IClient::STATE_ONLINE;
	if(!Online)
	{
		if(m_WasOnline && !m_LastServerAddress.empty() && m_LastLocalId >= 0)
			SendPlayerData(m_LastServerAddress.c_str(), m_LastLocalId, m_LastDummyId, false);

		ResetRClientUsers();
		m_WasOnline = false;
		ClearUsers();
		m_LastPollAttempt = 0;
		m_ServerRev = 0;
		return;
	}

	if(!m_WasOnline)
	{
		m_WasOnline = true;
		m_LastPollAttempt = 0;
	}

	CServerInfo CurrentServerInfo;
	Client()->GetServerInfo(&CurrentServerInfo);
	const char *pServerAddress = CurrentServerInfo.m_aAddress;

	int LocalClientId = GameClient()->m_aLocalIds[0];
	int DummyClientId = -1;
	if(Client()->DummyConnected())
		DummyClientId = GameClient()->m_aLocalIds[1];

	bool ForceSync = false;
	if(m_LastServerAddress != pServerAddress)
	{
		if(!m_LastServerAddress.empty() && m_LastLocalId >= 0)
			SendPlayerData(m_LastServerAddress.c_str(), m_LastLocalId, m_LastDummyId, false);

		ResetRClientUsers();
		ClearUsers();
		m_LastServerAddress = pServerAddress;
		m_ServerRev = 0;
		ForceSync = true;
	}

	if(m_LastLocalId != LocalClientId || m_LastDummyId != DummyClientId)
	{
		if(m_LastLocalId >= 0 && !m_LastServerAddress.empty())
			SendPlayerData(m_LastServerAddress.c_str(), m_LastLocalId, m_LastDummyId, false);

		m_LastLocalId = LocalClientId;
		m_LastDummyId = DummyClientId;
		m_ServerRev = 0;
		ForceSync = true;
	}

	if(m_aAuthToken[0] == '\0')
		return;

	const int64_t PollRetry = time_freq() * POLL_RETRY_SECONDS;
	if(!m_pRClientUsersTask && (ForceSync || Now - m_LastPollAttempt >= PollRetry))
	{
		if(LocalClientId >= 0)
			FetchRClientUsers(pServerAddress, LocalClientId, DummyClientId);
		m_LastPollAttempt = Now;
	}
}

void CRClientIndicator::SendPlayerData(const char *pServerAddress, int ClientId, int DummyClientId, bool Online)
{
	if(m_aAuthToken[0] == '\0')
	{
		if(!m_pAuthTokenTask)
			FetchAuthToken();
		return;
	}

	const char *pUsersUrl = GetRclientUsersUrl();
	if(!pUsersUrl)
		return;

	ResetRClientUsersSend();

	const char *pOnlineStr = Online ? "true" : "false";
	char aJsonData[512];

	if(DummyClientId >= 0)
		str_format(aJsonData, sizeof(aJsonData),
			"{"
			"\"server_address\":\"%s\","
			"\"player_id\":%d,"
			"\"dummy_id\":%d,"
			"\"online\":%s,"
			"\"auth_token\":\"%s\""
			"}",
			pServerAddress,
			ClientId,
			DummyClientId,
			pOnlineStr,
			m_aAuthToken);
	else
		str_format(aJsonData, sizeof(aJsonData),
			"{"
			"\"server_address\":\"%s\","
			"\"player_id\":%d,"
			"\"online\":%s,"
			"\"auth_token\":\"%s\""
			"}",
			pServerAddress,
			ClientId,
			pOnlineStr,
			m_aAuthToken);

	m_pRClientUsersTaskSend = std::make_shared<CHttpRequest>(pUsersUrl);
	m_pRClientUsersTaskSend->PostJson(aJsonData);
	m_pRClientUsersTaskSend->Timeout(CTimeout{HTTP_TIMEOUT_MS, 0, 500, 5});
	m_pRClientUsersTaskSend->IpResolve(IPRESOLVE::V4);
	m_pRClientUsersTaskSend->LogProgress(HTTPLOG::FAILURE);
	Http()->Run(m_pRClientUsersTaskSend);
}

void CRClientIndicator::FetchRClientUsers(const char *pServerAddress, int ClientId, int DummyClientId)
{
	if(m_pRClientUsersTask && !m_pRClientUsersTask->Done())
		return;

	const char *pUsersUrl = GetRclientUsersUrl();
	if(!pUsersUrl)
		return;

	m_pRClientUsersTask = HttpGet(pUsersUrl);
	ApplyPollHeaders(*m_pRClientUsersTask, pServerAddress, ClientId, DummyClientId);
	m_pRClientUsersTask->Timeout(CTimeout{HTTP_CONNECT_TIMEOUT_MS, LONGPOLL_TIMEOUT_MS, 0, 0});
	m_pRClientUsersTask->IpResolve(IPRESOLVE::V4);
	m_pRClientUsersTask->LogProgress(HTTPLOG::FAILURE);
	Http()->Run(m_pRClientUsersTask);
}

void CRClientIndicator::FetchAuthToken()
{
	if(m_pAuthTokenTask && !m_pAuthTokenTask->Done())
		return;

	const char *pTokenUrl = GetRclientTokenUrl();
	if(!pTokenUrl)
		return;

	m_pAuthTokenTask = HttpGet(pTokenUrl);
	m_pAuthTokenTask->Timeout(CTimeout{HTTP_TIMEOUT_MS, 0, 500, 5});
	m_pAuthTokenTask->IpResolve(IPRESOLVE::V4);
	Http()->Run(m_pAuthTokenTask);
}

void CRClientIndicator::FinishAuthToken()
{
	if(m_pAuthTokenTask->State() != EHttpState::DONE)
		return;

	json_value *pJson = m_pAuthTokenTask->ResultJson();
	if(!pJson)
	{
		GameClient()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "RClient", "Failed to fetch auth token: no JSON");
		return;
	}

	const json_value &Json = *pJson;
	const json_value &Token = Json["token"];

	if(Token.type == json_string)
	{
		str_copy(m_aAuthToken, Token.u.string.ptr, sizeof(m_aAuthToken));
		str_utf8_trim_right(m_aAuthToken);
		m_LastPollAttempt = 0;
		GameClient()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "RClient", "Fetched auth token");
	}
	else
	{
		GameClient()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "RClient", "Failed to fetch auth token: token not found in JSON");
	}
	json_value_free(pJson);
}

void CRClientIndicator::ResetAuthToken()
{
	if(m_pAuthTokenTask)
	{
		m_pAuthTokenTask->Abort();
		m_pAuthTokenTask = nullptr;
	}
}

void CRClientIndicator::FinishRClientUsersSend()
{
	if(!m_pRClientUsersTaskSend)
		return;

	const int Status = m_pRClientUsersTaskSend->StatusCode();
	if(Status == 401 || Status == 403)
	{
		m_aAuthToken[0] = '\0';
		FetchAuthToken();
	}
}

void CRClientIndicator::ResetRClientUsersSend()
{
	if(m_pRClientUsersTaskSend)
	{
		m_pRClientUsersTaskSend->Abort();
		m_pRClientUsersTaskSend = nullptr;
	}
}

void CRClientIndicator::FinishRClientUsers()
{
	if(!m_pRClientUsersTask)
		return;

	const int Status = m_pRClientUsersTask->StatusCode();
	if(Status == 401 || Status == 403)
	{
		m_aAuthToken[0] = '\0';
		FetchAuthToken();
		return;
	}

	json_value *pJson = m_pRClientUsersTask->ResultJson();
	if(!pJson)
		return;

	const json_value &Json = *pJson;
	const json_value &Error = Json["error"];
	if(Error.type == json_string)
	{
		json_value_free(pJson);
		return;
	}

	m_vRClientUsers.clear();

	if(Json.type == json_object)
	{
		const json_value &Rev = Json["_rev"];
		if(Rev.type == json_integer)
			m_ServerRev = (int)Rev.u.integer;

		for(unsigned int i = 0; i < Json.u.object.length; i++)
		{
			const char *pServerAddr = Json.u.object.values[i].name;
			const json_value &PlayersObj = *Json.u.object.values[i].value;

			if(pServerAddr[0] == '_')
				continue;

			if(PlayersObj.type != json_object)
				continue;

			for(unsigned int j = 0; j < PlayersObj.u.object.length; j++)
			{
				const char *pPlayerIdStr = PlayersObj.u.object.values[j].name;
				int PlayerId = atoi(pPlayerIdStr);
				const json_value &PlayerData = *PlayersObj.u.object.values[j].value;

				bool VoiceEnabled = false;
				int DummyId = -1;
				bool HasDummy = false;

				if(PlayerData.type == json_object)
				{
					for(unsigned int k = 0; k < PlayerData.u.object.length; k++)
					{
						const char *pKey = PlayerData.u.object.values[k].name;
						const json_value &Value = *PlayerData.u.object.values[k].value;
						if(str_comp(pKey, "dummy_id") == 0 && Value.type == json_integer)
						{
							DummyId = Value.u.integer;
							HasDummy = true;
						}
						else if(str_comp(pKey, "voice_enabled") == 0)
						{
							if(Value.type == json_boolean)
								VoiceEnabled = Value.u.boolean != 0;
							else if(Value.type == json_integer)
								VoiceEnabled = Value.u.integer != 0;
						}
					}
				}

				m_vRClientUsers.push_back({std::string(pServerAddr), PlayerId, VoiceEnabled});

				if(HasDummy)
					m_vRClientUsers.push_back({std::string(pServerAddr), DummyId, VoiceEnabled});
			}
		}
	}

	json_value_free(pJson);
}

void CRClientIndicator::ResetRClientUsers()
{
	if(m_pRClientUsersTask)
	{
		m_pRClientUsersTask->Abort();
		m_pRClientUsersTask = nullptr;
	}
}

void CRClientIndicator::ApplyPollHeaders(CHttpRequest &Request, const char *pServerAddress, int ClientId, int DummyClientId)
{
	Request.HeaderString("X-RClient-Token", m_aAuthToken);
	Request.HeaderString("X-RClient-Server", pServerAddress);
	Request.HeaderInt("X-RClient-Since", m_ServerRev);
	Request.HeaderInt("X-RClient-Timeout", POLL_TIMEOUT_SECONDS);
	Request.HeaderInt("X-RClient-Voice", g_Config.m_RiVoiceEnable);
	if(ClientId >= 0)
		Request.HeaderInt("X-RClient-Player", ClientId);
	if(DummyClientId >= 0)
		Request.HeaderInt("X-RClient-Dummy", DummyClientId);
}

void CRClientIndicator::ClearUsers()
{
	m_vRClientUsers.clear();
}

bool CRClientIndicator::IsPlayerRClient(int ClientId)
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return false;

	CServerInfo CurrentServerInfo;
	Client()->GetServerInfo(&CurrentServerInfo);

	for(const auto &User : m_vRClientUsers)
	{
		if(str_comp(User.m_ServerAddress.c_str(), CurrentServerInfo.m_aAddress) == 0 && User.m_PlayerId == ClientId)
			return true;
	}

	return false;
}

bool CRClientIndicator::IsPlayerRClientVoiceEnabled(int ClientId)
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return false;

	CServerInfo CurrentServerInfo;
	Client()->GetServerInfo(&CurrentServerInfo);

	for(const auto &User : m_vRClientUsers)
	{
		if(str_comp(User.m_ServerAddress.c_str(), CurrentServerInfo.m_aAddress) == 0 && User.m_PlayerId == ClientId)
			return User.m_VoiceEnabled;
	}

	return false;
}
