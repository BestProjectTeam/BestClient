/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_TWITCH_CHAT_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_TWITCH_CHAT_H

#include <game/client/component.h>
#include <game/client/ui_rect.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

class CTwitchChat : public CComponent
{
public:
	enum class EState
	{
		Disconnected,
		Connecting,
		Connected,
		Error,
	};

	int Sizeof() const override { return sizeof(*this); }

	void OnUpdate() override;
	void OnShutdown() override;

	void Start();
	void Stop();
	bool IsActive() const;
	EState State() const;
	void GetStatusText(char *pBuf, int BufSize) const;

private:
	enum
	{
		MAX_CHANNEL_LENGTH = 64,
		MAX_STATUS_LENGTH = 128,
		MAX_QUEUED_MESSAGES = 256,
		IRC_RECV_CHUNK = 2048,
	};

	struct SQueuedMessage
	{
		std::string m_Name;
		std::string m_Text;
	};

	mutable std::mutex m_Mutex;
	std::thread m_Worker;
	std::atomic<bool> m_StopRequested{false};
	std::atomic<bool> m_WorkerRunning{false};
	EState m_State = EState::Disconnected;
	char m_aChannel[MAX_CHANNEL_LENGTH] = "";
	char m_aStatusText[MAX_STATUS_LENGTH] = "";
	std::deque<SQueuedMessage> m_MessageQueue;

	void SetStatus(const char *pMessage);
	void SetState(EState State);
	void QueueChatMessage(const char *pName, const char *pText);
	void FlushChatMessages();
	void WorkerMain(std::string Channel);
	void HandleIrcLine(const char *pLine);

	static bool ParseChannel(const char *pInput, char *pOut, int OutSize);
	static bool ExtractTagValue(const char *pTags, const char *pKey, char *pOut, int OutSize);
};

#endif
