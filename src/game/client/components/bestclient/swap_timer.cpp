/* Copyright © 2026 BestProject Team */
#include "swap_timer.h"

#include <base/color.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/console.h>
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/protocol.h>

#include <game/client/components/binds.h>
#include <game/client/components/camera.h>
#include <game/client/components/chat.h>
#include <game/client/components/hud_layout.h>
#include <game/client/components/menus.h>
#include <game/client/components/nameplates.h>
#include <game/client/components/scoreboard.h>
#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/localization.h>

#include <algorithm>

static constexpr int SWAP_SERVER_MSG = -1;
static constexpr float DIMMED_ALPHA = 0.35f;
static constexpr const char *DEFAULT_MINIMAL_TEXT = "[%ds]";

static const ColorRGBA s_TextColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
static const ColorRGBA s_FadedTextColor = ColorRGBA(0.72f, 0.76f, 0.82f, 1.0f);
static const ColorRGBA s_AcceptColor = ColorRGBA(0.35f, 0.85f, 0.35f, 1.0f);
static const ColorRGBA s_DeclineColor = ColorRGBA(0.90f, 0.32f, 0.28f, 1.0f);
static const ColorRGBA s_PeekColor = ColorRGBA(0.32f, 0.62f, 0.98f, 1.0f);

static float Smoothstep(float Phase)
{
	Phase = std::clamp(Phase, 0.0f, 1.0f);
	return Phase * Phase * (3.0f - 2.0f * Phase);
}

// Quintic ease in/out - lingers at both ends and sprints through the middle.
static float PeekEase(float Phase)
{
	Phase = std::clamp(Phase, 0.0f, 1.0f);
	if(Phase < 0.5f)
		return 16.0f * Phase * Phase * Phase * Phase * Phase;

	const float Shifted = -2.0f * Phase + 2.0f;
	return 1.0f - Shifted * Shifted * Shifted * Shifted * Shifted * 0.5f;
}

void CSwapTimer::ConSwapAccept(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	static_cast<CSwapTimer *>(pUserData)->AcceptSwap();
}

void CSwapTimer::ConSwapDecline(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	static_cast<CSwapTimer *>(pUserData)->DeclineSwap();
}

void CSwapTimer::ConSwapNext(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	static_cast<CSwapTimer *>(pUserData)->SelectNext();
}

void CSwapTimer::ConSwapPrev(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	static_cast<CSwapTimer *>(pUserData)->SelectPrev();
}

void CSwapTimer::ConSwapPeek(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	static_cast<CSwapTimer *>(pUserData)->PeekPartner();
}

void CSwapTimer::OnConsoleInit()
{
	Console()->Register("bc_swap_accept", "", CFGFLAG_CLIENT, ConSwapAccept, this, "Accept the selected swap request");
	Console()->Register("bc_swap_decline", "", CFGFLAG_CLIENT, ConSwapDecline, this, "Decline or cancel the selected swap request");
	Console()->Register("bc_swap_next", "", CFGFLAG_CLIENT, ConSwapNext, this, "Select the next swap request");
	Console()->Register("bc_swap_prev", "", CFGFLAG_CLIENT, ConSwapPrev, this, "Select the previous swap request");
	Console()->Register("bc_swap_peek", "", CFGFLAG_CLIENT, ConSwapPeek, this, "Move the camera to the selected swap partner and back");
}

void CSwapTimer::OnReset()
{
	ResetState();
}

void CSwapTimer::OnStateChange(int NewState, int OldState)
{
	(void)OldState;
	if(NewState != IClient::STATE_ONLINE)
		ResetState();
}

void CSwapTimer::ResetState()
{
	for(int Conn = 0; Conn < NUM_DUMMIES; Conn++)
	{
		m_avEntries[Conn].clear();
		m_aSelected[Conn] = 0;
		m_aScrollAnim[Conn] = 0.0f;
	}
	StopPeek();
}

int CSwapTimer::EntryCount(int Conn) const
{
	return static_cast<int>(m_avEntries[Conn].size());
}

int CSwapTimer::VisibleConn() const
{
	return std::clamp(g_Config.m_ClDummy, 0, NUM_DUMMIES - 1);
}

float CSwapTimer::HudCanvasWidth() const
{
	return HUD_CANVAS_HEIGHT * Graphics()->ScreenAspect();
}

float CSwapTimer::LineHeight(float Scale) const
{
	return 11.0f * Scale;
}

int CSwapTimer::FindEntry(int Conn, const char *pName, bool Incoming) const
{
	if(!pName || !pName[0])
		return -1;

	const std::vector<SSwapEntry> &vEntries = m_avEntries[Conn];
	for(int i = 0, Count = static_cast<int>(vEntries.size()); i < Count; i++)
	{
		if(vEntries[i].m_Closing || vEntries[i].m_Incoming != Incoming)
			continue;
		if(str_comp(vEntries[i].m_aOtherName, pName) == 0)
			return i;
	}
	return -1;
}

int CSwapTimer::FindPairedEntry(int Conn, bool Incoming) const
{
	const std::vector<SSwapEntry> &vEntries = m_avEntries[Conn];
	for(int i = 0, Count = static_cast<int>(vEntries.size()); i < Count; i++)
	{
		if(!vEntries[i].m_Closing && vEntries[i].m_FromDummy && vEntries[i].m_Incoming == Incoming)
			return i;
	}
	return -1;
}

void CSwapTimer::ClampSelection(int Conn)
{
	const int Count = EntryCount(Conn);
	m_aSelected[Conn] = Count > 0 ? std::clamp(m_aSelected[Conn], 0, Count - 1) : 0;
}

void CSwapTimer::RemoveEntryAt(int Conn, int Index)
{
	m_avEntries[Conn].erase(m_avEntries[Conn].begin() + Index);
	if(m_aSelected[Conn] > Index)
		m_aSelected[Conn]--;
	ClampSelection(Conn);
}

void CSwapTimer::CloseEntryAt(int Conn, int Index)
{
	if(Index < 0 || Index >= EntryCount(Conn) || m_avEntries[Conn][Index].m_Closing)
		return;

	const bool Paired = m_avEntries[Conn][Index].m_FromDummy;
	const bool WasIncoming = m_avEntries[Conn][Index].m_Incoming;

	if(g_Config.m_BcSwapTimerAnimations)
		m_avEntries[Conn][Index].m_Closing = true;
	else
		RemoveEntryAt(Conn, Index);

	if(!Paired)
		return;

	// a swap with our own dummy shows up on both connections because idk how to say but it like you swapping with other player xD
	const int OtherConn = Conn ^ 1;
	const int MirrorIndex = FindPairedEntry(OtherConn, !WasIncoming);
	if(MirrorIndex < 0)
		return;

	if(g_Config.m_BcSwapTimerAnimations)
		m_avEntries[OtherConn][MirrorIndex].m_Closing = true;
	else
		RemoveEntryAt(OtherConn, MirrorIndex);
}

void CSwapTimer::CloseOnConn(int Conn, bool OutgoingOnly)
{
	// Shit optimiz
	for(int i = EntryCount(Conn) - 1; i >= 0; i--)
	{
		if(!OutgoingOnly || !m_avEntries[Conn][i].m_Incoming)
			CloseEntryAt(Conn, i);
	}
}

void CSwapTimer::AddEntry(int Conn, const char *pName, bool Incoming, float CooldownSeconds)
{
	if(!pName || !pName[0])
		return;

	const float Now = Client()->LocalTime();
	const bool Paired = IsOwnOtherTee(Conn, pName);

	SSwapEntry Entry;
	Entry.m_Incoming = Incoming;
	Entry.m_FromDummy = Paired;
	str_copy(Entry.m_aOtherName, pName, sizeof(Entry.m_aOtherName));
	Entry.m_CooldownEnd = Now + maximum(CooldownSeconds, 0.0f);
	Entry.m_ExpireTime = Entry.m_CooldownEnd + REQUEST_TIMEOUT_SECONDS;

	const auto Insert = [this](int TargetConn, const SSwapEntry &NewEntry) {
		const int Existing = FindEntry(TargetConn, NewEntry.m_aOtherName, NewEntry.m_Incoming);
		if(Existing >= 0)
		{
			const float AnimPhase = m_avEntries[TargetConn][Existing].m_AnimPhase;
			const float AcceptPhase = m_avEntries[TargetConn][Existing].m_AcceptPhase;
			m_avEntries[TargetConn][Existing] = NewEntry;
			m_avEntries[TargetConn][Existing].m_AnimPhase = AnimPhase;
			m_avEntries[TargetConn][Existing].m_AcceptPhase = AcceptPhase;
			return;
		}

		if(EntryCount(TargetConn) >= MAX_ENTRIES_PER_CONN)
			RemoveEntryAt(TargetConn, 0);
		m_avEntries[TargetConn].push_back(NewEntry);
	};

	Insert(Conn, Entry);

	if(!Paired)
		return;

	const int SelfId = GameClient()->m_aLocalIds[Conn];
	const int OtherConn = Conn ^ 1;
	if(SelfId < 0 || FindPairedEntry(OtherConn, !Incoming) >= 0)
		return;

	SSwapEntry Mirror = Entry;
	Mirror.m_Incoming = !Incoming;
	str_copy(Mirror.m_aOtherName, GameClient()->m_aClients[SelfId].m_aName, sizeof(Mirror.m_aOtherName));
	Insert(OtherConn, Mirror);
}

void CSwapTimer::ExpireEntries()
{
	const float Now = Client()->LocalTime();
	for(int Conn = 0; Conn < NUM_DUMMIES; Conn++)
	{
		for(int i = EntryCount(Conn) - 1; i >= 0; i--)
		{
			if(!m_avEntries[Conn][i].m_Closing && Now >= m_avEntries[Conn][i].m_ExpireTime)
				CloseEntryAt(Conn, i);
		}
	}
}

void CSwapTimer::CancelForPlayer(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	const char *pName = GameClient()->m_aClients[ClientId].m_aName;
	for(int Conn = 0; Conn < NUM_DUMMIES; Conn++)
	{
		if(GameClient()->m_aLocalIds[Conn] == ClientId)
		{
			CloseOnConn(Conn, false);
			continue;
		}

		if(!pName[0])
			continue;

		for(int i = EntryCount(Conn) - 1; i >= 0; i--)
		{
			if(str_comp(m_avEntries[Conn][i].m_aOtherName, pName) == 0)
				CloseEntryAt(Conn, i);
		}
	}
}

void CSwapTimer::OnPlayerDeath(int ClientId)
{
	CancelForPlayer(ClientId);
}

void CSwapTimer::OnMessage(int MsgType, void *pRawMsg)
{
	if(!HasActiveEntry())
		return;

	if(MsgType == NETMSGTYPE_SV_KILLMSG)
	{
		const CNetMsg_Sv_KillMsg *pMsg = static_cast<CNetMsg_Sv_KillMsg *>(pRawMsg);
		CancelForPlayer(pMsg->m_Victim);
	}
	else if(MsgType == NETMSGTYPE_SV_KILLMSGTEAM)
	{
		const CNetMsg_Sv_KillMsgTeam *pMsg = static_cast<CNetMsg_Sv_KillMsgTeam *>(pRawMsg);
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(GameClient()->m_Teams.Team(i) == pMsg->m_Team)
				CancelForPlayer(i);
		}
	}
	else if(MsgType == NETMSGTYPE_SV_RACEFINISH)
	{
		const CNetMsg_Sv_RaceFinish *pMsg = static_cast<CNetMsg_Sv_RaceFinish *>(pRawMsg);
		CancelForPlayer(pMsg->m_ClientId);
	}
}

bool CSwapTimer::HasActiveEntry() const
{
	for(const auto &vEntries : m_avEntries)
	{
		for(const SSwapEntry &Entry : vEntries)
		{
			if(!Entry.m_Closing)
				return true;
		}
	}
	return false;
}

bool CSwapTimer::IsCooldownActive(const SSwapEntry &Entry) const
{
	return Client()->LocalTime() < Entry.m_CooldownEnd;
}

bool CSwapTimer::IsAcceptEnabled(const SSwapEntry &Entry) const
{
	return Entry.m_Incoming && !Entry.m_Closing && !IsCooldownActive(Entry);
}

bool CSwapTimer::IsOwnOtherTee(int Conn, const char *pName) const
{
	if(!pName || !pName[0] || !Client()->DummyConnected())
		return false;

	const int OtherId = GameClient()->m_aLocalIds[Conn ^ 1];
	if(OtherId < 0)
		return false;

	return str_comp(GameClient()->m_aClients[OtherId].m_aName, pName) == 0;
}

const char *CSwapTimer::DisplayName(const SSwapEntry &Entry) const
{
	return Entry.m_FromDummy ? Localize("Dummy") : Entry.m_aOtherName;
}

CSwapTimer::SSwapEntry CSwapTimer::MakePreviewEntry(float Now) const
{
	SSwapEntry Preview;
	Preview.m_Incoming = true;
	str_copy(Preview.m_aOtherName, "RoflikBEST", sizeof(Preview.m_aOtherName));
	Preview.m_CooldownEnd = Now;
	Preview.m_ExpireTime = Now + 163.0f;
	Preview.m_AnimPhase = 1.0f;
	Preview.m_AcceptPhase = 1.0f;
	return Preview;
}

int CSwapTimer::SelectedEntry(int Conn) const
{
	if(Conn < 0)
		return -1;

	const int Index = m_aSelected[Conn];
	if(Index >= EntryCount(Conn) || m_avEntries[Conn][Index].m_Closing)
		return -1;
	return Index;
}

void CSwapTimer::CycleSelection(int Step)
{
	const int Conn = VisibleConn();
	const int Count = EntryCount(Conn);
	if(Count <= 1)
		return;

	// skips cards that are already fading out just a base of animations ( I hate writing in english or at? idk)
	for(int i = 0; i < Count; i++)
	{
		m_aSelected[Conn] = (m_aSelected[Conn] + Step + Count) % Count;
		if(!m_avEntries[Conn][m_aSelected[Conn]].m_Closing)
			return;
	}
}

void CSwapTimer::SelectNext()
{
	CycleSelection(1);
}

void CSwapTimer::SelectPrev()
{
	CycleSelection(-1);
}

void CSwapTimer::AcceptSwap()
{
	const int Conn = VisibleConn();
	const int Index = SelectedEntry(Conn);
	if(Index < 0 || !IsAcceptEnabled(m_avEntries[Conn][Index]))
		return;

	const char *pCommand = g_Config.m_BcSwapAcceptCommand[0] ? g_Config.m_BcSwapAcceptCommand : "/swap";
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "say %s %s", pCommand, m_avEntries[Conn][Index].m_aOtherName);
	Console()->ExecuteLine(aBuf, IConsole::CLIENT_ID_UNSPECIFIED);
	CloseEntryAt(Conn, Index);
}

void CSwapTimer::DeclineSwap()
{
	const int Conn = VisibleConn();
	const int Index = SelectedEntry(Conn);
	if(Index < 0)
		return;

	// only our own request can be cancelled because its ddnet ( Its strange, why who receives cant cancel request??? )
	if(!m_avEntries[Conn][Index].m_Incoming)
	{
		const char *pCommand = g_Config.m_BcSwapDeclineCommand[0] ? g_Config.m_BcSwapDeclineCommand : "/cancelswap";
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "say %s", pCommand);
		Console()->ExecuteLine(aBuf, IConsole::CLIENT_ID_UNSPECIFIED);
	}

	CloseEntryAt(Conn, Index);
}

int CSwapTimer::FindClientByName(const char *pName) const
{
	if(!pName || !pName[0])
		return -1;

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(GameClient()->m_aClients[i].m_Active && str_comp(GameClient()->m_aClients[i].m_aName, pName) == 0)
			return i;
	}
	return -1;
}

void CSwapTimer::StopPeek()
{
	m_PeekStage = EPeekStage::IDLE;
	m_PeekClientId = -1;
}

void CSwapTimer::PeekPartner()
{
	if(m_PeekStage != EPeekStage::IDLE)
	{
		// Cancelling mid-flight rewinds through the same animation instead of snapping back.
		if(m_PeekStage == EPeekStage::TRAVEL_BACK || !g_Config.m_BcSwapPeekAnimation)
		{
			StopPeek();
			return;
		}

		const float Now = Client()->LocalTime();
		float Progress = 1.0f;
		if(m_PeekStage == EPeekStage::TRAVEL_TO)
		{
			const float TravelTime = maximum(g_Config.m_BcSwapPeekTravelTime / 1000.0f, 0.001f);
			Progress = std::clamp((Now - m_PeekStageStart) / TravelTime, 0.0f, 1.0f);
		}

		const float ReturnTime = maximum(g_Config.m_BcSwapPeekReturnTime / 1000.0f, 0.001f);
		m_PeekStage = EPeekStage::TRAVEL_BACK;
		m_PeekStageStart = Now - (1.0f - Progress) * ReturnTime;
		return;
	}

	const int Conn = VisibleConn();
	const int Index = SelectedEntry(Conn);
	if(Index < 0)
		return;

	const int ClientId = FindClientByName(m_avEntries[Conn][Index].m_aOtherName);
	if(ClientId < 0 || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
		return;

	m_PeekClientId = ClientId;
	m_PeekOrigin = GameClient()->m_Camera.m_Center;
	m_PeekStageStart = Client()->LocalTime();
	m_PeekStage = g_Config.m_BcSwapPeekAnimation ? EPeekStage::TRAVEL_TO : EPeekStage::HOLD;
}

bool CSwapTimer::IsInputFrozen() const
{
	return m_PeekStage != EPeekStage::IDLE && g_Config.m_BcSwapPeekFreezeInput;
}

void CSwapTimer::ApplyPeekCamera()
{
	if(m_PeekStage == EPeekStage::IDLE)
		return;

	if(m_PeekClientId < 0 || !GameClient()->m_Snap.m_aCharacters[m_PeekClientId].m_Active)
	{
		StopPeek();
		return;
	}

	const float Now = Client()->LocalTime();
	const float Elapsed = Now - m_PeekStageStart;
	const vec2 Target = GameClient()->m_aClients[m_PeekClientId].m_RenderPos;
	const float TravelTime = maximum(g_Config.m_BcSwapPeekTravelTime / 1000.0f, 0.001f);
	const float ReturnTime = maximum(g_Config.m_BcSwapPeekReturnTime / 1000.0f, 0.001f);

	// The origin follows our own tee, so returning lands on it even if we kept moving.
	const int SelfId = GameClient()->m_aLocalIds[VisibleConn()];
	if(SelfId >= 0 && GameClient()->m_Snap.m_aCharacters[SelfId].m_Active)
		m_PeekOrigin = GameClient()->m_aClients[SelfId].m_RenderPos;

	switch(m_PeekStage)
	{
	case EPeekStage::TRAVEL_TO:
		if(Elapsed >= TravelTime)
		{
			m_PeekStage = EPeekStage::HOLD;
			m_PeekStageStart = Now;
			GameClient()->m_Camera.m_Center = Target;
			break;
		}
		GameClient()->m_Camera.m_Center = mix(m_PeekOrigin, Target, PeekEase(Elapsed / TravelTime));
		break;

	case EPeekStage::HOLD:
		GameClient()->m_Camera.m_Center = Target;
		if(Elapsed >= g_Config.m_BcSwapPeekHoldTime / 1000.0f)
		{
			m_PeekStage = g_Config.m_BcSwapPeekAnimation ? EPeekStage::TRAVEL_BACK : EPeekStage::IDLE;
			m_PeekStageStart = Now;
		}
		break;

	case EPeekStage::TRAVEL_BACK:
		if(Elapsed >= ReturnTime)
		{
			StopPeek();
			break;
		}
		GameClient()->m_Camera.m_Center = mix(Target, m_PeekOrigin, PeekEase(Elapsed / ReturnTime));
		break;

	case EPeekStage::IDLE:
		break;
	}
}

void CSwapTimer::CopyName(const char *pStart, const char *pEnd, char *pBuf, int BufSize)
{
	while(pEnd > pStart && (pEnd[-1] == '.' || pEnd[-1] == ' '))
		pEnd--;

	const int Length = std::clamp(static_cast<int>(pEnd - pStart), 0, BufSize - 1);
	mem_copy(pBuf, pStart, Length);
	pBuf[Length] = '\0';
}

const char *CSwapTimer::SkipMessagePrefix(const char *pMessage)
{
	// just prefiiixxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
	while(*pMessage == '*' || *pMessage == ' ')
		pMessage++;
	return pMessage;
}

bool CSwapTimer::ParseIncoming(const char *pMessage, char *pName, int NameSize, float *pCooldown)
{
	const char *pRequested = str_find(pMessage, " has requested to swap with you");
	if(!pRequested)
		return false;

	const char *pStart = SkipMessagePrefix(pMessage);
	if(pRequested <= pStart)
		return false;

	CopyName(pStart, pRequested, pName, NameSize);

	*pCooldown = REQUEST_COOLDOWN_SECONDS;
	const char *pWait = str_find(pRequested, "please wait ");
	if(pWait)
	{
		const int Seconds = str_toint(pWait + str_length("please wait "));
		if(Seconds > 0)
			*pCooldown = static_cast<float>(Seconds);
	}
	return pName[0] != '\0';
}

bool CSwapTimer::ParseOutgoing(const char *pMessage, char *pName, int NameSize)
{
	const char *pPrefix = str_find(pMessage, "You have requested to swap with ");
	if(!pPrefix)
		return false;

	const char *pStart = pPrefix + str_length("You have requested to swap with ");
	const char *pEnd = str_find(pStart, ". Use /cancelswap");
	CopyName(pStart, pEnd ? pEnd : pStart + str_length(pStart), pName, NameSize);
	return pName[0] != '\0';
}

CSwapTimer::ECloseEvent CSwapTimer::ParseCloseEvent(const char *pMessage, char *pFirst, char *pSecond, int NameSize)
{
	pFirst[0] = '\0';
	pSecond[0] = '\0';

	const char *pCancelled = str_find(pMessage, "You have canceled swap with ");
	if(pCancelled)
	{
		const char *pStart = pCancelled + str_length("You have canceled swap with ");
		CopyName(pStart, pStart + str_length(pStart), pFirst, NameSize);
		return ECloseEvent::CANCEL_OUTGOING;
	}

	const char *pStart = SkipMessagePrefix(pMessage);

	const char *pDeclined = str_find(pMessage, " has canceled swap with you");
	if(pDeclined && pDeclined > pStart)
	{
		CopyName(pStart, pDeclined, pFirst, NameSize);
		return ECloseEvent::CANCEL_INCOMING;
	}

	const char *pSwapped = str_find(pMessage, " has swapped with ");
	if(pSwapped && pSwapped > pStart)
	{
		CopyName(pStart, pSwapped, pFirst, NameSize);
		const char *pOther = pSwapped + str_length(" has swapped with ");
		CopyName(pOther, pOther + str_length(pOther), pSecond, NameSize);
		return ECloseEvent::COMPLETE;
	}

	if(str_find(pMessage, "no swap request"))
		return ECloseEvent::CANCEL_OUTGOING;

	return ECloseEvent::NONE;
}

void CSwapTimer::OnChatMessage(int ClientId, const char *pMessage, int Conn)
{
	if(ClientId != SWAP_SERVER_MSG || !pMessage || !pMessage[0])
		return;
	if(Conn < 0 || Conn >= NUM_DUMMIES)
		return;

	char aName[MAX_NAME_LENGTH];
	char aSecondName[MAX_NAME_LENGTH];
	float Cooldown = REQUEST_COOLDOWN_SECONDS;

	if(ParseIncoming(pMessage, aName, sizeof(aName), &Cooldown))
	{
		AddEntry(Conn, aName, true, Cooldown);
		return;
	}
	if(ParseOutgoing(pMessage, aName, sizeof(aName)))
	{
		AddEntry(Conn, aName, false, REQUEST_COOLDOWN_SECONDS);
		return;
	}

	const ECloseEvent Event = ParseCloseEvent(pMessage, aName, aSecondName, sizeof(aName));
	if(Event == ECloseEvent::NONE)
		return;

	if(Event == ECloseEvent::CANCEL_INCOMING || Event == ECloseEvent::CANCEL_OUTGOING)
	{
		const bool Incoming = Event == ECloseEvent::CANCEL_INCOMING;
		const int Index = FindEntry(Conn, aName, Incoming);
		if(Index >= 0)
			CloseEntryAt(Conn, Index);
		else if(!Incoming)
			CloseOnConn(Conn, true);
		return;
	}

	for(const char *pName : {aName, aSecondName})
	{
		for(const bool Incoming : {true, false})
		{
			const int Index = FindEntry(Conn, pName, Incoming);
			if(Index >= 0)
			{
				CloseEntryAt(Conn, Index);
				return;
			}
		}
	}
}

float CSwapTimer::EntryAlpha(const SSwapEntry &Entry) const
{
	if(!g_Config.m_BcSwapTimerAnimations)
		return Entry.m_Closing ? 0.0f : 1.0f;
	return Smoothstep(Entry.m_AnimPhase);
}

float CSwapTimer::AcceptDimFactor(const SSwapEntry &Entry) const
{
	if(!g_Config.m_BcSwapTimerAnimations)
		return IsAcceptEnabled(Entry) ? 1.0f : DIMMED_ALPHA;
	return DIMMED_ALPHA + (1.0f - DIMMED_ALPHA) * Smoothstep(Entry.m_AcceptPhase);
}

void CSwapTimer::UpdateAnimations()
{
	const float FrameTime = Client()->RenderFrameTime();
	const float Duration = maximum(g_Config.m_BcSwapTimerAnimationTime / 1000.0f, 0.001f);
	const float Step = FrameTime / Duration;
	const bool Animate = g_Config.m_BcSwapTimerAnimations != 0;

	for(int Conn = 0; Conn < NUM_DUMMIES; Conn++)
	{
		for(int i = EntryCount(Conn) - 1; i >= 0; i--)
		{
			SSwapEntry &Entry = m_avEntries[Conn][i];
			UpdateTeeInfos(Entry, Conn);
			const float AcceptTarget = IsAcceptEnabled(Entry) ? 1.0f : 0.0f;

			if(!Animate)
			{
				Entry.m_AcceptPhase = AcceptTarget;
				Entry.m_AnimPhase = 1.0f;
				if(Entry.m_Closing)
					RemoveEntryAt(Conn, i);
				continue;
			}

			if(Entry.m_AcceptPhase < AcceptTarget)
				Entry.m_AcceptPhase = minimum(Entry.m_AcceptPhase + Step, AcceptTarget);
			else if(Entry.m_AcceptPhase > AcceptTarget)
				Entry.m_AcceptPhase = maximum(Entry.m_AcceptPhase - Step, AcceptTarget);

			if(Entry.m_Closing)
			{
				Entry.m_AnimPhase -= Step;
				if(Entry.m_AnimPhase <= 0.0f)
					RemoveEntryAt(Conn, i);
			}
			else if(Entry.m_AnimPhase < 1.0f)
			{
				Entry.m_AnimPhase = minimum(Entry.m_AnimPhase + Step, 1.0f);
			}
		}

		ClampSelection(Conn);

		const float ScrollTarget = static_cast<float>(m_aSelected[Conn]);
		const float Delta = ScrollTarget - m_aScrollAnim[Conn];
		if(!Animate || absolute(Delta) < 0.001f)
			m_aScrollAnim[Conn] = ScrollTarget;
		else
			m_aScrollAnim[Conn] += Delta * std::clamp(FrameTime / (Duration * 0.75f), 0.0f, 1.0f);
	}
}

void CSwapTimer::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE || !g_Config.m_BcSwapTimer)
	{
		ResetState();
		return;
	}

	ExpireEntries();
	UpdateAnimations();

	if(g_Config.m_BcSwapTimerStyle == 1)
		RenderNameplateMode();
}

int CSwapTimer::VisibleSlotCount(int Conn) const
{
	return minimum(EntryCount(Conn), (int)MAX_VISIBLE_CARDS);
}

float CSwapTimer::WindowStart(int Conn) const
{
	// The box keeps its size and position, only the entries scroll inside of it.
	const int Slots = VisibleSlotCount(Conn);
	const int Last = maximum(EntryCount(Conn) - Slots, 0);
	return std::clamp(m_aScrollAnim[Conn] - (Slots - 1) * 0.5f, 0.0f, static_cast<float>(Last));
}

int CSwapTimer::CollectVisibleCards(int Conn, SVisibleCard *pOut) const
{
	const float Start = WindowStart(Conn);
	const float Slots = static_cast<float>(VisibleSlotCount(Conn));

	int Num = 0;
	for(int i = 0, Count = EntryCount(Conn); i < Count && Num < MAX_VISIBLE_CARDS; i++)
	{
		const float Slot = i - Start;
		if(Slot < -0.5f || Slot > Slots - 0.5f)
			continue;

		const float Distance = absolute(i - m_aScrollAnim[Conn]);
		const float Alpha = EntryAlpha(m_avEntries[Conn][i]) * std::clamp(1.0f - Distance * 0.22f, 0.0f, 1.0f);
		if(Alpha <= 0.01f)
			continue;

		SVisibleCard &Card = pOut[Num++];
		Card.m_Index = i;
		Card.m_Slot = Slot;
		Card.m_Distance = Distance;
		Card.m_Alpha = Alpha;
		Card.m_Squeeze = std::clamp(1.0f - Distance * 0.06f, 0.7f, 1.0f);
		Card.m_Selected = Distance < 0.5f;
	}

	std::sort(pOut, pOut + Num, [](const SVisibleCard &Left, const SVisibleCard &Right) {
		return Left.m_Distance > Right.m_Distance;
	});
	return Num;
}

void CSwapTimer::BuildHotkeyLayout(bool Show, float FontSize, float LeadGap, float IconGap, float PairGap, SHotkeyLayout &Out) const
{
	Out = SHotkeyLayout();
	Out.m_FontSize = FontSize;
	Out.m_LeadGap = LeadGap;
	Out.m_IconGap = IconGap;
	Out.m_PairGap = PairGap;

	if(!Show || !g_Config.m_BcSwapTimerShowHotkeys)
		return;

	GameClient()->m_Binds.GetKey("bc_swap_accept", Out.m_aAcceptKey, sizeof(Out.m_aAcceptKey));
	GameClient()->m_Binds.GetKey("bc_swap_decline", Out.m_aDeclineKey, sizeof(Out.m_aDeclineKey));
	GameClient()->m_Binds.GetKey("bc_swap_peek", Out.m_aPeekKey, sizeof(Out.m_aPeekKey));

	const auto KeyWidth = [&](const char *pKey) {
		return pKey[0] ? LeadGap + FontSize + IconGap + TextRender()->TextWidth(FontSize, pKey, -1, -1.0f) : 0.0f;
	};

	int Used = 0;
	for(const char *pKey : {Out.m_aAcceptKey, Out.m_aDeclineKey, Out.m_aPeekKey})
	{
		const float Width = KeyWidth(pKey);
		if(Width <= 0.0f)
			continue;
		Out.m_Width += Width + (Used > 0 ? PairGap : 0.0f);
		Used++;
	}
	Out.m_HasAny = Out.m_Width > 0.0f;
}

void CSwapTimer::RenderHotkeyLayout(const SHotkeyLayout &Hotkeys, float X, float Y, float Alpha, float AcceptDim)
{
	if(!Hotkeys.m_HasAny)
		return;

	float CursorX = X;
	bool First = true;

	const auto RenderKey = [&](const char *pIcon, const ColorRGBA &IconColor, const char *pKeyName, float Dim) {
		if(!pKeyName[0])
			return;

		if(!First)
			CursorX += Hotkeys.m_PairGap;
		First = false;
		CursorX += Hotkeys.m_LeadGap;

		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->TextColor(IconColor.WithMultipliedAlpha(Alpha * Dim));
		TextRender()->Text(CursorX, Y, Hotkeys.m_FontSize, pIcon, -1.0f);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		CursorX += Hotkeys.m_FontSize + Hotkeys.m_IconGap;

		TextRender()->TextColor(s_TextColor.WithMultipliedAlpha(Alpha * Dim));
		TextRender()->Text(CursorX, Y, Hotkeys.m_FontSize, pKeyName, -1.0f);
		CursorX += TextRender()->TextWidth(Hotkeys.m_FontSize, pKeyName, -1, -1.0f);
	};

	RenderKey(FontIcon::CHECK, s_AcceptColor, Hotkeys.m_aAcceptKey, AcceptDim);
	RenderKey(FontIcon::XMARK, s_DeclineColor, Hotkeys.m_aDeclineKey, 1.0f);
	RenderKey(FontIcon::EYE, s_PeekColor, Hotkeys.m_aPeekKey, 1.0f);
}

void CSwapTimer::FormatPlaceholders(const char *pFormat, int Seconds, const char *pSelf, const char *pOther, char *pBuf, int BufSize)
{
	char aNumber[16];
	str_format(aNumber, sizeof(aNumber), "%d", Seconds);

	int Out = 0;
	bool Replaced = false;
	for(const char *pCur = pFormat; *pCur && Out < BufSize - 1; pCur++)
	{
		const char *pValue = nullptr;
		if(pCur[0] == '%')
		{
			if(!Replaced && pCur[1] == 'd')
				pValue = aNumber;
			else if(pCur[1] == 'y')
				pValue = pSelf;
			else if(pCur[1] == 'n')
				pValue = pOther;
		}

		if(!pValue)
		{
			pBuf[Out++] = *pCur;
			continue;
		}

		for(; *pValue && Out < BufSize - 1; pValue++)
			pBuf[Out++] = *pValue;
		Replaced |= pCur[1] == 'd';
		pCur++;
	}
	pBuf[Out] = '\0';

	if(!Replaced)
		str_format(pBuf, BufSize, DEFAULT_MINIMAL_TEXT, Seconds);
}

void CSwapTimer::FormatMinimalText(const SSwapEntry &Entry, float Now, char *pBuf, int BufSize) const
{
	const bool OnCooldown = Now < Entry.m_CooldownEnd;
	const float Target = OnCooldown ? Entry.m_CooldownEnd : Entry.m_ExpireTime;
	const char *pConfigured = OnCooldown ? g_Config.m_BcSwapTimerWaitText : g_Config.m_BcSwapTimerLeftText;

	FormatPlaceholders(pConfigured[0] ? pConfigured : DEFAULT_MINIMAL_TEXT, maximum(0, round_to_int(Target - Now)),
		Localize("You"), DisplayName(Entry), pBuf, BufSize);
}

void CSwapTimer::FormatStatusText(const SSwapEntry &Entry, float Now, char *pBuf, int BufSize) const
{
	if(Now < Entry.m_CooldownEnd)
		str_format(pBuf, BufSize, Localize("Wait %ds"), maximum(0, round_to_int(Entry.m_CooldownEnd - Now)));
	else
		str_format(pBuf, BufSize, Localize("%ds left"), maximum(0, round_to_int(Entry.m_ExpireTime - Now)));
}

void CSwapTimer::FormatEntryText(const SSwapEntry &Entry, float Now, char *pBuf, int BufSize) const
{
	char aTimeBuf[32];
	FormatStatusText(Entry, Now, aTimeBuf, sizeof(aTimeBuf));

	const char *pTitle = Entry.m_Incoming ? Localize("Incoming swap") : Localize("Outcoming swap");
	const char *pFrom = Entry.m_Incoming ? DisplayName(Entry) : Localize("You");
	const char *pTo = Entry.m_Incoming ? Localize("You") : DisplayName(Entry);

	str_format(pBuf, BufSize, "%s: %s → %s  (%s)", pTitle, pFrom, pTo, aTimeBuf);
}

void CSwapTimer::UpdateTeeInfos(SSwapEntry &Entry, int Conn)
{
	if(!g_Config.m_BcSwapTimerShowTees)
	{
		Entry.m_pSelfTee = nullptr;
		Entry.m_pOtherTee = nullptr;
		return;
	}

	const int SelfId = GameClient()->m_aLocalIds[Conn];
	if(!Entry.m_pSelfTee && SelfId >= 0 && GameClient()->m_aClients[SelfId].m_Active)
		Entry.m_pSelfTee = GameClient()->CreateManagedTeeRenderInfo(GameClient()->m_aClients[SelfId]);

	if(Entry.m_pOtherTee)
		return;

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!GameClient()->m_aClients[i].m_Active)
			continue;
		if(str_comp(GameClient()->m_aClients[i].m_aName, Entry.m_aOtherName) != 0)
			continue;

		Entry.m_pOtherTee = GameClient()->CreateManagedTeeRenderInfo(GameClient()->m_aClients[i]);
		break;
	}
}

float CSwapTimer::TeeIconSize(float FontSize) const
{
	return g_Config.m_BcSwapTimerShowTees ? FontSize * 1.6f : 0.0f;
}

float CSwapTimer::RenderTeeIcon(const std::shared_ptr<CManagedTeeRenderInfo> &pTee, float X, float Y, float FontSize, float Alpha) const
{
	const float Size = TeeIconSize(FontSize);
	if(Size <= 0.0f)
		return 0.0f;
	if(!pTee)
		return Size;

	CTeeRenderInfo Info = pTee->TeeRenderInfo();
	Info.m_Size = Size;

	vec2 OffsetToMid;
	CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &Info, OffsetToMid);
	RenderTools()->RenderTee(CAnimState::GetIdle(), &Info, EMOTE_NORMAL, vec2(1.0f, 0.0f),
		vec2(X + Size * 0.5f, Y + FontSize * 0.5f + OffsetToMid.y), Alpha);
	return Size;
}

float CSwapTimer::MeasureLineWidth(const SSwapEntry &Entry, float Scale, float Now) const
{
	const float FontSize = 8.0f * Scale;

	char aBuf[ENTRY_TEXT_SIZE];
	FormatEntryText(Entry, Now, aBuf, sizeof(aBuf));

	// when scrolling box dont need to resize
	SHotkeyLayout Hotkeys;
	BuildHotkeyLayout(true, FontSize, 6.0f * Scale, 2.0f * Scale, 0.0f, Hotkeys);

	const float Icons = TeeIconSize(FontSize) * 2.0f;
	return TextRender()->TextWidth(FontSize, aBuf, -1, -1.0f) + Hotkeys.m_Width + Icons;
}

void CSwapTimer::RenderLine(const SSwapEntry &Entry, float X, float Y, float Scale, float Now, float Alpha, bool Selected, bool ShowHotkeys)
{
	const float FontSize = 8.0f * Scale;
	const ColorRGBA Color = (Selected ? s_TextColor : s_FadedTextColor).WithMultipliedAlpha(Alpha);

	char aTimeBuf[32];
	FormatStatusText(Entry, Now, aTimeBuf, sizeof(aTimeBuf));

	const bool Incoming = Entry.m_Incoming;
	const char *pFromName = Incoming ? DisplayName(Entry) : Localize("You");
	const char *pToName = Incoming ? Localize("You") : DisplayName(Entry);
	const auto &pFromTee = Incoming ? Entry.m_pOtherTee : Entry.m_pSelfTee;
	const auto &pToTee = Incoming ? Entry.m_pSelfTee : Entry.m_pOtherTee;

	char aTitle[128];
	str_format(aTitle, sizeof(aTitle), "%s: ", Incoming ? Localize("Incoming swap") : Localize("Outcoming swap"));

	char aTail[64];
	str_format(aTail, sizeof(aTail), "  (%s)", aTimeBuf);

	float CursorX = X;
	const auto DrawText = [&](const char *pText) {
		TextRender()->TextColor(Color);
		TextRender()->Text(CursorX, Y, FontSize, pText, -1.0f);
		CursorX += TextRender()->TextWidth(FontSize, pText, -1, -1.0f);
	};

	DrawText(aTitle);
	CursorX += RenderTeeIcon(pFromTee, CursorX, Y, FontSize, Alpha);
	DrawText(pFromName);
	DrawText(" → ");
	CursorX += RenderTeeIcon(pToTee, CursorX, Y, FontSize, Alpha);
	DrawText(pToName);
	DrawText(aTail);

	SHotkeyLayout Hotkeys;
	BuildHotkeyLayout(ShowHotkeys, FontSize, 6.0f * Scale, 2.0f * Scale, 0.0f, Hotkeys);
	RenderHotkeyLayout(Hotkeys, CursorX, Y, Alpha, AcceptDimFactor(Entry));

	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

CUIRect CSwapTimer::GetRect(bool ForcePreview) const
{
	if(!HudLayout::IsEnabled(HudLayout::MODULE_SWAP_TIMER) || !g_Config.m_BcSwapTimer)
		return {0.0f, 0.0f, 0.0f, 0.0f};
	if(!ForcePreview && g_Config.m_BcSwapTimerStyle == 1)
		return {0.0f, 0.0f, 0.0f, 0.0f};

	const float Width = HudCanvasWidth();
	const auto Layout = HudLayout::Get(HudLayout::MODULE_SWAP_TIMER, Width, HUD_CANVAS_HEIGHT);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	const float Now = Client()->LocalTime();

	float MaxWidth = 0.0f;
	float Slots = 1.0f;

	if(ForcePreview)
	{
		MaxWidth = MeasureLineWidth(MakePreviewEntry(Now), Scale, Now);
	}
	else
	{
		const int Conn = VisibleConn();
		if(EntryCount(Conn) <= 0)
			return {0.0f, 0.0f, 0.0f, 0.0f};

		for(const SSwapEntry &Entry : m_avEntries[Conn])
			MaxWidth = maximum(MaxWidth, MeasureLineWidth(Entry, Scale, Now));
		Slots = static_cast<float>(VisibleSlotCount(Conn));
	}

	if(MaxWidth <= 0.0f)
		return {0.0f, 0.0f, 0.0f, 0.0f};

	const float PaddingX = 3.0f * Scale;
	const float PaddingY = 2.0f * Scale;

	CUIRect Rect;
	Rect.w = MaxWidth + PaddingX * 2.0f;
	Rect.h = LineHeight(Scale) * Slots + PaddingY * 2.0f;
	Rect.x = std::clamp(Layout.m_X - Rect.w * 0.5f, 0.0f, maximum(0.0f, Width - Rect.w));
	Rect.y = std::clamp(Layout.m_Y, 0.0f, maximum(0.0f, HUD_CANVAS_HEIGHT - Rect.h));
	return Rect;
}

void CSwapTimer::Render(bool ForcePreview)
{
	if(!ForcePreview)
	{
		if(Client()->State() != IClient::STATE_ONLINE || g_Config.m_BcSwapTimerStyle == 1)
			return;
		if(GameClient()->m_Scoreboard.IsActive() || GameClient()->m_Menus.IsActive())
			return;
	}

	const CUIRect Rect = GetRect(ForcePreview);
	if(Rect.w <= 0.0f || Rect.h <= 0.0f)
		return;

	const float Width = HudCanvasWidth();
	const auto Layout = HudLayout::Get(HudLayout::MODULE_SWAP_TIMER, Width, HUD_CANVAS_HEIGHT);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	const float Now = Client()->LocalTime();
	const float X = Rect.x + 3.0f * Scale;
	const float TopY = Rect.y + 2.0f * Scale;
	const int Conn = VisibleConn();

	SVisibleCard aCards[MAX_VISIBLE_CARDS];
	int Num = 0;
	float BackgroundAlpha = 1.0f;

	if(!ForcePreview)
	{
		Num = CollectVisibleCards(Conn, aCards);
		if(Num <= 0)
			return;

		BackgroundAlpha = 0.0f;
		for(int i = 0; i < Num; i++)
			BackgroundAlpha = maximum(BackgroundAlpha, aCards[i].m_Alpha);
	}

	if(Layout.m_BackgroundEnabled)
	{
		const int Corners = HudLayout::BackgroundCorners(IGraphics::CORNER_ALL, Rect.x, Rect.y, Rect.w, Rect.h, Width, HUD_CANVAS_HEIGHT);
		const ColorRGBA Background = color_cast<ColorRGBA>(ColorHSLA(Layout.m_BackgroundColor, true)).WithMultipliedAlpha(BackgroundAlpha);
		Graphics()->DrawRect(Rect.x, Rect.y, Rect.w, Rect.h, Background, Corners, 4.0f * Scale);
	}

	if(ForcePreview)
	{
		RenderLine(MakePreviewEntry(Now), X, TopY, Scale, Now, 1.0f, true, true);
		return;
	}

	for(int i = 0; i < Num; i++)
	{
		const SVisibleCard &Card = aCards[i];
		const float Y = TopY + Card.m_Slot * LineHeight(Scale);
		RenderLine(m_avEntries[Conn][Card.m_Index], X + Card.m_Distance * 3.0f * Scale, Y, Scale, Now, Card.m_Alpha, Card.m_Selected, Card.m_Selected);
	}
}

void CSwapTimer::RenderNameplateCard(const SSwapEntry &Entry, int ClientId, float Now, const SVisibleCard &Card)
{
	const bool Minimal = g_Config.m_BcSwapTimerNameplateMinimal != 0;
	const float UserScale = std::clamp(g_Config.m_BcSwapTimerSize / 100.0f, 0.5f, 2.0f) * Card.m_Squeeze;
	const float FontSize = (Minimal ? 20.0f : 24.0f) * UserScale;
	const float HotkeyFontSize = FontSize * (Minimal ? 0.8f : 0.85f);
	const float PaddingX = Minimal ? 0.0f : FontSize * 0.35f;
	const float PaddingY = Minimal ? 0.0f : FontSize * 0.22f;
	const float RowSpacing = FontSize * (Minimal ? 0.12f : 0.15f);

	char aBuf[ENTRY_TEXT_SIZE];
	if(Minimal)
		FormatMinimalText(Entry, Now, aBuf, MINIMAL_TEXT_SIZE);
	else
		FormatEntryText(Entry, Now, aBuf, sizeof(aBuf));

	SHotkeyLayout Hotkeys;
	BuildHotkeyLayout(Card.m_Selected, HotkeyFontSize, 0.0f, HotkeyFontSize * 0.25f, HotkeyFontSize * 0.7f, Hotkeys);

	const float IconSize = Minimal ? 0.0f : TeeIconSize(FontSize);
	const float TextWidth = TextRender()->TextWidth(FontSize, aBuf, -1, -1.0f) + IconSize * 2.0f;
	const float BoxWidth = maximum(TextWidth, Hotkeys.m_Width) + PaddingX * 2.0f;
	const float BoxHeight = FontSize + (Hotkeys.m_HasAny ? RowSpacing + HotkeyFontSize : 0.0f) + PaddingY * 2.0f;

	// just because no nameplate ( found by mistake because i playing with own name on )
	const vec2 Position = GameClient()->m_aClients[ClientId].m_RenderPos;
	const float PlateHeight = maximum(GameClient()->m_NamePlates.GetNamePlateOffset(ClientId), 38.0f);
	const float BoxY = Position.y - PlateHeight - FontSize * (Minimal ? 0.2f : 0.4f) - BoxHeight - (BoxHeight + FontSize * 0.18f) * Card.m_Slot;

	if(!Minimal)
	{
		const auto Layout = HudLayout::Get(HudLayout::MODULE_SWAP_TIMER, HudCanvasWidth(), HUD_CANVAS_HEIGHT);
		if(Layout.m_BackgroundEnabled)
		{
			const ColorRGBA Background = color_cast<ColorRGBA>(ColorHSLA(Layout.m_BackgroundColor, true)).WithMultipliedAlpha(Card.m_Alpha);
			Graphics()->DrawRect(Position.x - BoxWidth * 0.5f, BoxY, BoxWidth, BoxHeight, Background, IGraphics::CORNER_ALL, FontSize * 0.25f);
		}
	}

	const ColorRGBA Color = (Card.m_Selected ? s_TextColor : s_FadedTextColor).WithMultipliedAlpha(Card.m_Alpha);
	TextRender()->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, (Minimal ? 0.4f : 0.3f) * Card.m_Alpha));
	TextRender()->TextColor(Color);

	if(IconSize <= 0.0f)
	{
		TextRender()->Text(Position.x - TextWidth * 0.5f, BoxY + PaddingY, FontSize, aBuf, -1.0f);
	}
	else
	{
		const bool Incoming = Entry.m_Incoming;
		const char *pFromName = Incoming ? DisplayName(Entry) : Localize("You");
		const char *pToName = Incoming ? Localize("You") : DisplayName(Entry);
		const auto &pFromTee = Incoming ? Entry.m_pOtherTee : Entry.m_pSelfTee;
		const auto &pToTee = Incoming ? Entry.m_pSelfTee : Entry.m_pOtherTee;

		char aTimeBuf[32];
		FormatStatusText(Entry, Now, aTimeBuf, sizeof(aTimeBuf));

		char aTitle[128];
		str_format(aTitle, sizeof(aTitle), "%s: ", Incoming ? Localize("Incoming swap") : Localize("Outcoming swap"));

		char aTail[64];
		str_format(aTail, sizeof(aTail), "  (%s)", aTimeBuf);

		float CursorX = Position.x - TextWidth * 0.5f;
		const float TextY = BoxY + PaddingY;
		const auto DrawText = [&](const char *pText) {
			TextRender()->TextColor(Color);
			TextRender()->Text(CursorX, TextY, FontSize, pText, -1.0f);
			CursorX += TextRender()->TextWidth(FontSize, pText, -1, -1.0f);
		};

		DrawText(aTitle);
		CursorX += RenderTeeIcon(pFromTee, CursorX, TextY, FontSize, Card.m_Alpha);
		DrawText(pFromName);
		DrawText(" → ");
		CursorX += RenderTeeIcon(pToTee, CursorX, TextY, FontSize, Card.m_Alpha);
		DrawText(pToName);
		DrawText(aTail);
	}

	RenderHotkeyLayout(Hotkeys, Position.x - Hotkeys.m_Width * 0.5f, BoxY + PaddingY + FontSize + RowSpacing, Card.m_Alpha, AcceptDimFactor(Entry));

	TextRender()->TextColor(TextRender()->DefaultTextColor());
	TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
}

void CSwapTimer::RenderNameplateMode()
{
	if(GameClient()->m_Scoreboard.IsActive() || GameClient()->m_Menus.IsActive())
		return;
	if(!HudLayout::IsEnabled(HudLayout::MODULE_SWAP_TIMER))
		return;

	const int Conn = VisibleConn();
	const int ClientId = GameClient()->m_aLocalIds[Conn];
	if(ClientId < 0 || !GameClient()->m_aClients[ClientId].m_Active || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
		return;
	if(!GameClient()->OptimizerAllowRenderPos(GameClient()->m_aClients[ClientId].m_RenderPos))
		return;

	SVisibleCard aCards[MAX_VISIBLE_CARDS];
	const int Num = CollectVisibleCards(Conn, aCards);
	if(Num <= 0)
		return;

	const float Now = Client()->LocalTime();

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	float aPoints[4];
	Graphics()->MapScreenToWorld(
		GameClient()->m_Camera.m_Center.x, GameClient()->m_Camera.m_Center.y,
		100.0f, 100.0f, 100.0f, 0, 0,
		Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom, aPoints);
	Graphics()->MapScreen(aPoints[0], aPoints[1], aPoints[2], aPoints[3]);

	for(int i = 0; i < Num; i++)
		RenderNameplateCard(m_avEntries[Conn][aCards[i].m_Index], ClientId, Now, aCards[i]);

	Graphics()->MapScreen(ScreenX0, ScreenY0, ScreenX1, ScreenY1);
}
