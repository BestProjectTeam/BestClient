/* Copyright © 2026 BestProject Team */
#include "twitch_chat.h"

#include <base/net.h>
#include <base/system.h>

#include <engine/shared/config.h>

#include <game/client/components/chat.h>
#include <game/client/gameclient.h>

#include <cctype>
#include <cinttypes>
#include <chrono>
#include <cstring>

namespace
{
constexpr ColorRGBA TWITCH_CHAT_COLOR = ColorRGBA(0.35f, 0.65f, 1.0f, 1.0f);
constexpr const char *TWITCH_IRC_HOST = "irc.chat.twitch.tv";
constexpr int TWITCH_IRC_PORT = 6667;
constexpr int TWITCH_ANONYMOUS_SESSION_SECONDS = 20;
constexpr int TWITCH_IDLE_PING_SECONDS = 240;
constexpr int TWITCH_RESPONSE_TIMEOUT_SECONDS = 30;

bool SendAll(NETSOCKET Socket, const char *pData, int Size)
{
	int SentTotal = 0;
	while(SentTotal < Size)
	{
		const int Sent = net_tcp_send(Socket, pData + SentTotal, Size - SentTotal);
		if(Sent <= 0)
			return false;
		SentTotal += Sent;
	}
	return true;
}

bool SendLine(NETSOCKET Socket, const char *pLine)
{
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "%s\r\n", pLine);
	return SendAll(Socket, aBuf, str_length(aBuf));
}
}

void CTwitchChat::OnUpdate()
{
	FlushChatMessages();
}

void CTwitchChat::OnShutdown()
{
	Stop();
}

bool CTwitchChat::IsActive() const
{
	return m_WorkerRunning.load(std::memory_order_relaxed);
}

CTwitchChat::EState CTwitchChat::State() const
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	return m_State;
}

void CTwitchChat::GetStatusText(char *pBuf, int BufSize) const
{
	if(!pBuf || BufSize <= 0)
		return;
	std::lock_guard<std::mutex> Lock(m_Mutex);
	if(m_aStatusText[0] == '\0')
		str_copy(pBuf, "Not connected", BufSize);
	else if(m_State == EState::Connected)
		str_format(pBuf, BufSize, "%s | %" PRIu64 " msgs", m_aStatusText, m_ReceivedMessages);
	else
		str_copy(pBuf, m_aStatusText, BufSize);
}

void CTwitchChat::SetStatus(const char *pMessage)
{
	if(!pMessage)
		return;

	std::lock_guard<std::mutex> Lock(m_Mutex);
	str_copy(m_aStatusText, pMessage, sizeof(m_aStatusText));
}

void CTwitchChat::SetState(EState State)
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	m_State = State;
}

void CTwitchChat::QueueChatMessage(const char *pName, const char *pText)
{
	if(!pName || !pText || pName[0] == '\0' || pText[0] == '\0')
		return;

	std::lock_guard<std::mutex> Lock(m_Mutex);
	if((int)m_MessageQueue.size() >= MAX_QUEUED_MESSAGES)
		m_MessageQueue.pop_front();
	m_MessageQueue.push_back({pName, pText});
	++m_ReceivedMessages;
}

void CTwitchChat::FlushChatMessages()
{
	std::deque<SQueuedMessage> Messages;
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		if(m_MessageQueue.empty())
			return;
		Messages.swap(m_MessageQueue);
	}

	for(const SQueuedMessage &Message : Messages)
	{
		char aLine[MAX_LINE_LENGTH];
		str_format(aLine, sizeof(aLine), "%s: %s", Message.m_Name.c_str(), Message.m_Text.c_str());
		GameClient()->m_Chat.AddColoredLine(aLine, TWITCH_CHAT_COLOR);
	}
}

bool CTwitchChat::ParseChannel(const char *pInput, char *pOut, int OutSize)
{
	if(!pInput || !pOut || OutSize <= 1)
		return false;

	char aBuf[256];
	str_copy(aBuf, pInput, sizeof(aBuf));

	char *pTrim = aBuf;
	while(*pTrim && str_isspace(*pTrim))
		++pTrim;
	str_utf8_trim_right(pTrim);
	if(pTrim[0] == '\0')
		return false;

	const char *p = pTrim;
	if(const char *pHttps = str_startswith_nocase(p, "https://"))
		p = pHttps;
	else if(const char *pHttp = str_startswith_nocase(p, "http://"))
		p = pHttp;

	if(const char *pWww = str_startswith_nocase(p, "www."))
		p = pWww;

	if(const char *pTwitch = str_startswith_nocase(p, "twitch.tv/"))
		p = pTwitch;

	while(*p == '/')
		++p;

	char aChannel[MAX_CHANNEL_LENGTH];
	int Len = 0;
	while(p[Len] && p[Len] != '/' && p[Len] != '?' && p[Len] != '#' && !str_isspace(p[Len]) && Len < MAX_CHANNEL_LENGTH - 1)
	{
		aChannel[Len] = (char)std::tolower((unsigned char)p[Len]);
		++Len;
	}
	aChannel[Len] = '\0';

	if(Len < 1 || Len > 25)
		return false;

	for(int i = 0; i < Len; ++i)
	{
		const unsigned char c = (unsigned char)aChannel[i];
		if(!(std::isalnum(c) || c == '_'))
			return false;
	}

	str_copy(pOut, aChannel, OutSize);
	return true;
}

bool CTwitchChat::ExtractTagValue(const char *pTags, const char *pKey, char *pOut, int OutSize)
{
	if(!pTags || !pKey || !pOut || OutSize <= 0)
		return false;

	pOut[0] = '\0';
	const int KeyLen = str_length(pKey);
	const char *p = pTags;
	while(*p)
	{
		const char *pEq = str_find(p, "=");
		if(!pEq)
			break;

		const int NameLen = (int)(pEq - p);
		const char *pValue = pEq + 1;
		const char *pNext = str_find(pValue, ";");
		const int ValueLen = pNext ? (int)(pNext - pValue) : str_length(pValue);

		if(NameLen == KeyLen && str_comp_nocase_num(p, pKey, KeyLen) == 0)
		{
			const int CopyLen = minimum(ValueLen, OutSize - 1);
			mem_copy(pOut, pValue, CopyLen);
			pOut[CopyLen] = '\0';
			return pOut[0] != '\0';
		}

		if(!pNext)
			break;
		p = pNext + 1;
	}
	return false;
}

void CTwitchChat::Start()
{
	char aChannel[MAX_CHANNEL_LENGTH];
	if(!ParseChannel(g_Config.m_BcTwitchChatNick, aChannel, sizeof(aChannel)))
	{
		SetState(EState::Error);
		SetStatus("Invalid Twitch nick");
		return;
	}

	Stop();

	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		str_copy(m_aChannel, aChannel, sizeof(m_aChannel));
		m_State = EState::Connecting;
		m_aStatusText[0] = '\0';
		m_ReceivedMessages = 0;
		m_MessageQueue.clear();
	}

	m_StopRequested.store(false, std::memory_order_relaxed);
	m_WorkerRunning.store(true, std::memory_order_relaxed);
	SetStatus("Connecting...");
	m_Worker = std::thread([this, Channel = std::string(aChannel)]() {
		WorkerMain(std::move(Channel));
	});
}

void CTwitchChat::Stop()
{
	m_StopRequested.store(true, std::memory_order_relaxed);
	if(m_Worker.joinable())
		m_Worker.join();
	m_WorkerRunning.store(false, std::memory_order_relaxed);

	std::lock_guard<std::mutex> Lock(m_Mutex);
	if(m_State == EState::Connected || m_State == EState::Connecting)
		m_State = EState::Disconnected;
}

void CTwitchChat::WorkerMain(std::string Channel)
{
	while(!m_StopRequested.load(std::memory_order_relaxed))
	{
		bool ImmediateReconnect = false;
		const bool Reconnect = RunConnection(Channel, ImmediateReconnect);
		if(!Reconnect || m_StopRequested.load(std::memory_order_relaxed))
			break;

		SetState(EState::Connecting);
		SetStatus(ImmediateReconnect ? "Refreshing connection..." : "Disconnected, reconnecting...");
		if(!ImmediateReconnect)
		{
			for(int i = 0; i < 30 && !m_StopRequested.load(std::memory_order_relaxed); ++i)
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}

	if(m_StopRequested.load(std::memory_order_relaxed))
	{
		SetState(EState::Disconnected);
		SetStatus("Stopped");
	}
	m_WorkerRunning.store(false, std::memory_order_relaxed);
}

bool CTwitchChat::RunConnection(const std::string &Channel, bool &ImmediateReconnect)
{
	ImmediateReconnect = false;
	NETADDR Addr;
	mem_zero(&Addr, sizeof(Addr));
	if(net_host_lookup(TWITCH_IRC_HOST, &Addr, NETTYPE_IPV4 | NETTYPE_IPV6) != 0)
	{
		SetState(EState::Error);
		SetStatus("Failed to resolve irc.chat.twitch.tv");
		return true;
	}
	Addr.port = TWITCH_IRC_PORT;

	NETADDR BindAddr;
	mem_zero(&BindAddr, sizeof(BindAddr));
	BindAddr.type = Addr.type & (NETTYPE_IPV4 | NETTYPE_IPV6);

	NETSOCKET Socket = net_tcp_create(BindAddr);
	if(!Socket)
	{
		SetState(EState::Error);
		SetStatus("Failed to create socket");
		return true;
	}

	if(net_tcp_connect_timeout(Socket, &Addr, 8000) != 0)
	{
		net_tcp_close(Socket);
		SetState(EState::Error);
		SetStatus("Connection failed");
		return true;
	}

	net_set_non_blocking(Socket);

	const int FanId = 10000 + secure_rand_below(80000);
	char aNick[64];
	str_format(aNick, sizeof(aNick), "justinfan%d", FanId);

	char aLine[256];
	bool Ok = true;
	Ok = Ok && SendLine(Socket, "CAP REQ :twitch.tv/tags twitch.tv/commands");
	Ok = Ok && SendLine(Socket, "PASS SCHMOOPIIE");
	str_format(aLine, sizeof(aLine), "NICK %s", aNick);
	Ok = Ok && SendLine(Socket, aLine);
	str_format(aLine, sizeof(aLine), "JOIN #%s", Channel.c_str());
	Ok = Ok && SendLine(Socket, aLine);

	if(!Ok)
	{
		net_tcp_close(Socket);
		SetState(EState::Error);
		SetStatus("Failed to send handshake");
		return true;
	}

	char aRecvBuf[8192];
	int RecvLen = 0;
	bool Joined = false;
	bool ConnectionLost = false;
	bool Reconnect = true;
	bool WaitingForResponse = false;
	int64_t LastReceive = time_get();
	int64_t PingSentAt = 0;
	int64_t ConnectedAt = 0;

	while(!m_StopRequested.load(std::memory_order_relaxed))
	{
		const int Ready = net_socket_read_wait(Socket, std::chrono::milliseconds(200));
		const int Bytes = Ready > 0 ? net_tcp_recv(Socket, aRecvBuf + RecvLen, (int)sizeof(aRecvBuf) - RecvLen - 1) : -1;
		if(Bytes > 0)
		{
			LastReceive = time_get();
			WaitingForResponse = false;
			RecvLen += Bytes;
			aRecvBuf[RecvLen] = '\0';

			char *pStart = aRecvBuf;
			while(true)
			{
				char *pEnd = const_cast<char *>(str_find(pStart, "\n"));
				if(!pEnd)
					break;

				*pEnd = '\0';
				if(pEnd > pStart && pEnd[-1] == '\r')
					pEnd[-1] = '\0';

				if(str_startswith(pStart, ":tmi.twitch.tv RECONNECT") || str_startswith(pStart, "RECONNECT"))
				{
					ConnectionLost = true;
					break;
				}
				else if(str_startswith(pStart, "PING "))
				{
					char aPong[256];
					str_format(aPong, sizeof(aPong), "PONG %s", pStart + 5);
					if(!SendLine(Socket, aPong))
					{
						ConnectionLost = true;
						break;
					}
				}
				else
				{
					if(!Joined && (str_find(pStart, " 001 ") || str_find(pStart, "JOIN #")))
					{
						Joined = true;
						ConnectedAt = time_get();
						SetState(EState::Connected);
						char aMsg[128];
						str_format(aMsg, sizeof(aMsg), "Connected to #%s", Channel.c_str());
						SetStatus(aMsg);
					}
					if(str_find(pStart, "Login authentication failed") || str_find(pStart, "NOTICE * :Login unsuccessful"))
					{
						SetState(EState::Error);
						SetStatus("Authentication failed");
						Reconnect = false;
						ConnectionLost = true;
						break;
					}
					HandleIrcLine(pStart);
				}

				pStart = pEnd + 1;
			}

			const int Remaining = RecvLen - (int)(pStart - aRecvBuf);
			if(Remaining > 0)
				mem_move(aRecvBuf, pStart, Remaining);
			RecvLen = maximum(0, Remaining);
			if(RecvLen >= (int)sizeof(aRecvBuf) - 1)
				RecvLen = 0;
			if(ConnectionLost)
			{
				if(Reconnect)
				{
					SetState(EState::Error);
					SetStatus("Connection lost");
				}
				break;
			}
		}
		else if(Bytes == 0)
		{
			SetState(EState::Error);
			SetStatus("Disconnected");
			break;
		}
		else if(Ready > 0 && !net_would_block())
		{
			SetState(EState::Error);
			SetStatus("Connection lost");
			break;
		}

		const int64_t Now = time_get();
		if(Joined && Now - ConnectedAt > time_freq() * TWITCH_ANONYMOUS_SESSION_SECONDS)
		{
			ImmediateReconnect = true;
			break;
		}
		if(WaitingForResponse && Now - PingSentAt > time_freq() * TWITCH_RESPONSE_TIMEOUT_SECONDS)
		{
			SetState(EState::Error);
			SetStatus("Connection timed out");
			break;
		}
		if(!WaitingForResponse && Now - LastReceive > time_freq() * TWITCH_IDLE_PING_SECONDS)
		{
			if(!SendLine(Socket, "PING :tmi.twitch.tv"))
			{
				SetState(EState::Error);
				SetStatus("Connection lost");
				break;
			}
			WaitingForResponse = true;
			PingSentAt = Now;
		}
	}

	net_tcp_close(Socket);

	if(!m_StopRequested.load(std::memory_order_relaxed) && State() != EState::Error)
	{
		SetState(EState::Disconnected);
		SetStatus("Disconnected");
	}
	return Reconnect;
}

void CTwitchChat::HandleIrcLine(const char *pLine)
{
	if(!pLine || pLine[0] == '\0')
		return;

	const char *p = pLine;
	char aTags[1024] = "";
	if(*p == '@')
	{
		++p;
		const char *pSpace = str_find(p, " ");
		if(!pSpace)
			return;
		const int TagLen = minimum((int)(pSpace - p), (int)sizeof(aTags) - 1);
		mem_copy(aTags, p, TagLen);
		aTags[TagLen] = '\0';
		p = pSpace + 1;
	}

	if(*p != ':')
		return;
	++p;

	const char *pPrefixEnd = str_find(p, " ");
	if(!pPrefixEnd)
		return;

	char aPrefix[128];
	const int PrefixLen = minimum((int)(pPrefixEnd - p), (int)sizeof(aPrefix) - 1);
	mem_copy(aPrefix, p, PrefixLen);
	aPrefix[PrefixLen] = '\0';
	p = pPrefixEnd + 1;

	if(!str_startswith(p, "PRIVMSG "))
		return;
	p += 8;

	const char *pMsgStart = str_find(p, " :");
	if(!pMsgStart)
		return;
	pMsgStart += 2;

	char aName[64] = "";
	if(!ExtractTagValue(aTags, "display-name", aName, sizeof(aName)))
	{
		const char *pBang = str_find(aPrefix, "!");
		const int NameLen = pBang ? (int)(pBang - aPrefix) : str_length(aPrefix);
		str_copy(aName, aPrefix, minimum((int)sizeof(aName), NameLen + 1));
	}

	if(aName[0] == '\0')
		return;

	char aText[MAX_LINE_LENGTH];
	str_copy(aText, pMsgStart, sizeof(aText));
	str_utf8_trim_right(aText);
	char *pText = aText;
	while(*pText && str_isspace(*pText))
		++pText;
	if(pText[0] == '\0')
		return;

	QueueChatMessage(aName, pText);
}
