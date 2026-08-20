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

#include <game/client/animstate.h>
#include <game/client/components/binds.h>
#include <game/client/components/camera.h>
#include <game/client/components/hud_layout.h>
#include <game/client/components/menus.h>
#include <game/client/components/nameplates.h>
#include <game/client/components/scoreboard.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/localization.h>

#include <algorithm>

static constexpr int SWAP_SERVER_MSG = -1;
static constexpr float DIMMED_ALPHA = 0.35f;
static constexpr const char *DEFAULT_MINIMAL_TEXT = "[%ds]";
static constexpr const char *ACCEPT_COMMAND = "/swap";
static constexpr const char *DECLINE_COMMAND = "/cancelswap";

static const ColorRGBA s_TextColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
static const ColorRGBA s_AcceptColor = ColorRGBA(0.35f, 0.85f, 0.35f, 1.0f);
static const ColorRGBA s_DeclineColor = ColorRGBA(0.90f, 0.32f, 0.28f, 1.0f);
static const ColorRGBA s_PeekColor = ColorRGBA(0.32f, 0.62f, 0.98f, 1.0f);

static float Smoothstep(float Phase)
{
	Phase = std::clamp(Phase, 0.0f, 1.0f);
	return Phase * Phase * (3.0f - 2.0f * Phase);
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

void CSwapTimer::ConSwapPeek(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CSwapTimer *>(pUserData)->SetPeekHeld(pResult->GetInteger(0) != 0);
}

void CSwapTimer::OnConsoleInit()
{
	Console()->Register("bc_swap_accept", "", CFGFLAG_CLIENT, ConSwapAccept, this, "Accept the pending swap request");
	Console()->Register("bc_swap_decline", "", CFGFLAG_CLIENT, ConSwapDecline, this, "Cancel an outgoing swap, or dismiss an incoming swap card");
	Console()->Register("+bc_swap_peek", "", CFGFLAG_CLIENT, ConSwapPeek, this, "Hold to spectate the swap partner");
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
		ClearEntry(Conn);
	StopPeek();
}

void CSwapTimer::ClearEntry(int Conn)
{
	m_aEntries[Conn] = SSwapEntry();
}

int CSwapTimer::VisibleConn() const
{
	return std::clamp(g_Config.m_ClDummy, 0, NUM_DUMMIES - 1);
}

CSwapTimer::SSwapEntry *CSwapTimer::ActiveEntry(int Conn)
{
	if(Conn < 0 || Conn >= NUM_DUMMIES || !m_aEntries[Conn].m_Active || m_aEntries[Conn].m_Closing)
		return nullptr;
	return &m_aEntries[Conn];
}

const CSwapTimer::SSwapEntry *CSwapTimer::ActiveEntry(int Conn) const
{
	if(Conn < 0 || Conn >= NUM_DUMMIES || !m_aEntries[Conn].m_Active || m_aEntries[Conn].m_Closing)
		return nullptr;
	return &m_aEntries[Conn];
}

bool CSwapTimer::HasActiveEntry() const
{
	for(const SSwapEntry &Entry : m_aEntries)
	{
		if(Entry.m_Active && !Entry.m_Closing)
			return true;
	}
	return false;
}

float CSwapTimer::HudCanvasWidth() const
{
	return HUD_CANVAS_HEIGHT * Graphics()->ScreenAspect();
}

float CSwapTimer::LineHeight(float Scale) const
{
	return 11.0f * Scale;
}

void CSwapTimer::CloseEntry(int Conn)
{
	if(Conn < 0 || Conn >= NUM_DUMMIES || !m_aEntries[Conn].m_Active || m_aEntries[Conn].m_Closing)
		return;

	const bool Paired = m_aEntries[Conn].m_FromDummy;
	const bool WasIncoming = m_aEntries[Conn].m_Incoming;

	m_aEntries[Conn].m_Closing = true;

	if(!Paired)
		return;

	const int OtherConn = Conn ^ 1;
	SSwapEntry &Mirror = m_aEntries[OtherConn];
	if(!Mirror.m_Active || Mirror.m_Closing || !Mirror.m_FromDummy || Mirror.m_Incoming == WasIncoming)
		return;
	Mirror.m_Closing = true;
}

void CSwapTimer::CloseOnConn(int Conn, bool OutgoingOnly)
{
	if(Conn < 0 || Conn >= NUM_DUMMIES || !m_aEntries[Conn].m_Active)
		return;
	if(OutgoingOnly && m_aEntries[Conn].m_Incoming)
		return;
	CloseEntry(Conn);
}

void CSwapTimer::SetEntry(int Conn, const char *pName, bool Incoming, float CooldownSeconds)
{
	if(!pName || !pName[0] || Conn < 0 || Conn >= NUM_DUMMIES)
		return;

	const float Now = Client()->LocalTime();
	const bool Paired = IsOwnOtherTee(Conn, pName);

	SSwapEntry Entry;
	Entry.m_Active = true;
	Entry.m_Incoming = Incoming;
	Entry.m_FromDummy = Paired;
	str_copy(Entry.m_aOtherName, pName, sizeof(Entry.m_aOtherName));
	Entry.m_CooldownEnd = Now + maximum(CooldownSeconds, 0.0f);
	Entry.m_ExpireTime = Entry.m_CooldownEnd + REQUEST_TIMEOUT_SECONDS;

	if(m_aEntries[Conn].m_Active && !m_aEntries[Conn].m_Closing &&
		str_comp(m_aEntries[Conn].m_aOtherName, pName) == 0 &&
		m_aEntries[Conn].m_Incoming == Incoming)
	{
		Entry.m_AnimPhase = m_aEntries[Conn].m_AnimPhase;
		Entry.m_AcceptPhase = m_aEntries[Conn].m_AcceptPhase;
		Entry.m_pSelfTee = m_aEntries[Conn].m_pSelfTee;
		Entry.m_pOtherTee = m_aEntries[Conn].m_pOtherTee;
	}

	m_aEntries[Conn] = Entry;

	if(!Paired)
		return;

	const int SelfId = GameClient()->m_aLocalIds[Conn];
	const int OtherConn = Conn ^ 1;
	if(SelfId < 0)
		return;

	SSwapEntry &Mirror = m_aEntries[OtherConn];
	if(Mirror.m_Active && !Mirror.m_Closing && Mirror.m_FromDummy && Mirror.m_Incoming != Incoming)
		return;

	SSwapEntry MirrorEntry = Entry;
	MirrorEntry.m_Incoming = !Incoming;
	str_copy(MirrorEntry.m_aOtherName, GameClient()->m_aClients[SelfId].m_aName, sizeof(MirrorEntry.m_aOtherName));
	MirrorEntry.m_pSelfTee = nullptr;
	MirrorEntry.m_pOtherTee = nullptr;
	m_aEntries[OtherConn] = MirrorEntry;
}

void CSwapTimer::ExpireEntries()
{
	const float Now = Client()->LocalTime();
	for(int Conn = 0; Conn < NUM_DUMMIES; Conn++)
	{
		if(m_aEntries[Conn].m_Active && !m_aEntries[Conn].m_Closing && Now >= m_aEntries[Conn].m_ExpireTime)
			CloseEntry(Conn);
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

		if(!pName[0] || !m_aEntries[Conn].m_Active)
			continue;
		if(str_comp(m_aEntries[Conn].m_aOtherName, pName) == 0)
			CloseEntry(Conn);
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
	Preview.m_Active = true;
	Preview.m_Incoming = true;
	str_copy(Preview.m_aOtherName, "Player", sizeof(Preview.m_aOtherName));
	Preview.m_CooldownEnd = Now;
	Preview.m_ExpireTime = Now + 163.0f;
	Preview.m_AnimPhase = 1.0f;
	Preview.m_AcceptPhase = 1.0f;
	return Preview;
}

void CSwapTimer::AcceptSwap()
{
	const int Conn = VisibleConn();
	SSwapEntry *pEntry = ActiveEntry(Conn);
	if(!pEntry || !IsAcceptEnabled(*pEntry))
		return;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "say %s %s", ACCEPT_COMMAND, pEntry->m_aOtherName);
	Console()->ExecuteLine(aBuf, IConsole::CLIENT_ID_UNSPECIFIED);
	CloseEntry(Conn);
}

void CSwapTimer::DeclineSwap()
{
	const int Conn = VisibleConn();
	SSwapEntry *pEntry = ActiveEntry(Conn);
	if(!pEntry)
		return;

	// DDNet only lets the initiator cancel. Incoming cards are dismissed locally.
	if(!pEntry->m_Incoming)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "say %s", DECLINE_COMMAND);
		Console()->ExecuteLine(aBuf, IConsole::CLIENT_ID_UNSPECIFIED);
	}

	CloseEntry(Conn);
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
	if(m_PeekHeld)
	{
		// Undo client-side SpecInfo override when the player is not actually spectating/paused.
		const int LocalId = GameClient()->m_Snap.m_LocalClientId;
		bool ReallySpectating = false;
		if(LocalId >= 0 && LocalId < MAX_CLIENTS)
		{
			const auto &Local = GameClient()->m_aClients[LocalId];
			ReallySpectating = Local.m_Team == TEAM_SPECTATORS || Local.m_Paused || Local.m_Spec;
		}
		if(!ReallySpectating)
		{
			GameClient()->m_Snap.m_SpecInfo.m_Active = false;
			GameClient()->m_Snap.m_SpecInfo.m_UsePosition = false;
			GameClient()->m_Snap.m_SpecInfo.m_SpectatorId = SPEC_FREEVIEW;
		}
	}

	m_PeekHeld = false;
	m_PeekClientId = -1;
}

void CSwapTimer::SetPeekHeld(bool Held)
{
	if(!Held)
	{
		StopPeek();
		return;
	}

	const SSwapEntry *pEntry = ActiveEntry(VisibleConn());
	if(!pEntry)
		return;

	const int ClientId = FindClientByName(pEntry->m_aOtherName);
	if(ClientId < 0 || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
		return;

	m_PeekHeld = true;
	m_PeekClientId = ClientId;
}

bool CSwapTimer::IsInputFrozen() const
{
	return m_PeekHeld;
}

bool CSwapTimer::ApplyPeekSpectate()
{
	if(!m_PeekHeld)
		return false;

	if(m_PeekClientId < 0 || !GameClient()->m_Snap.m_aCharacters[m_PeekClientId].m_Active)
	{
		StopPeek();
		return false;
	}

	// Client-side spectate: reuse the same SpecInfo path the camera uses for native follow.
	CGameClient::CSnapState::CSpectateInfo &Spec = GameClient()->m_Snap.m_SpecInfo;
	const CGameClient::CSnapState::CCharacterInfo &Char = GameClient()->m_Snap.m_aCharacters[m_PeekClientId];
	Spec.m_Active = true;
	Spec.m_SpectatorId = m_PeekClientId;
	Spec.m_UsePosition = true;
	Spec.m_Position = mix(
		vec2(Char.m_Prev.m_X, Char.m_Prev.m_Y),
		vec2(Char.m_Cur.m_X, Char.m_Cur.m_Y),
		Client()->IntraGameTick(g_Config.m_ClDummy));
	return true;
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
	static const char *const s_apPrefixes[] = {
		"You have already requested to swap with ",
		"You have requested to swap with ",
	};

	const char *pStart = nullptr;
	for(const char *pPrefix : s_apPrefixes)
	{
		const char *pFound = str_find(pMessage, pPrefix);
		if(!pFound)
			continue;
		pStart = pFound + str_length(pPrefix);
		break;
	}
	if(!pStart)
		return false;

	const char *pEnd = str_find(pStart, ". Use /cancelswap");
	if(!pEnd)
		pEnd = str_find(pStart, ".");
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

	if(str_find(pMessage, "swap request timed out") || str_find(pMessage, "no swap request"))
		return ECloseEvent::CANCEL_OUTGOING;

	return ECloseEvent::NONE;
}

bool CSwapTimer::ParseAcceptWait(const char *pMessage, float *pSeconds)
{
	const char *pWait = str_find(pMessage, "You have to wait ");
	if(!pWait || !str_find(pMessage, "until you can swap"))
		return false;

	const int Seconds = str_toint(pWait + str_length("You have to wait "));
	if(Seconds <= 0)
		return false;

	*pSeconds = static_cast<float>(Seconds);
	return true;
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
		SetEntry(Conn, aName, true, Cooldown);
		return;
	}
	if(ParseOutgoing(pMessage, aName, sizeof(aName)))
	{
		SetEntry(Conn, aName, false, REQUEST_COOLDOWN_SECONDS);
		return;
	}

	float AcceptWaitSeconds = 0.0f;
	if(ParseAcceptWait(pMessage, &AcceptWaitSeconds))
	{
		SSwapEntry *pEntry = ActiveEntry(Conn);
		if(pEntry && pEntry->m_Incoming)
		{
			const float Now = Client()->LocalTime();
			pEntry->m_CooldownEnd = Now + AcceptWaitSeconds;
			pEntry->m_ExpireTime = maximum(pEntry->m_ExpireTime, pEntry->m_CooldownEnd + REQUEST_TIMEOUT_SECONDS);
		}
		return;
	}

	const ECloseEvent Event = ParseCloseEvent(pMessage, aName, aSecondName, sizeof(aName));
	if(Event == ECloseEvent::NONE)
		return;

	if(Event == ECloseEvent::CANCEL_INCOMING || Event == ECloseEvent::CANCEL_OUTGOING)
	{
		const bool Incoming = Event == ECloseEvent::CANCEL_INCOMING;
		SSwapEntry &Entry = m_aEntries[Conn];
		if(Entry.m_Active && !Entry.m_Closing && Entry.m_Incoming == Incoming &&
			(!aName[0] || str_comp(Entry.m_aOtherName, aName) == 0))
		{
			CloseEntry(Conn);
		}
		else if(!Incoming)
		{
			CloseOnConn(Conn, true);
		}
		return;
	}

	for(const char *pName : {aName, aSecondName})
	{
		if(!pName[0])
			continue;
		for(int C = 0; C < NUM_DUMMIES; C++)
		{
			if(m_aEntries[C].m_Active && !m_aEntries[C].m_Closing && str_comp(m_aEntries[C].m_aOtherName, pName) == 0)
			{
				CloseEntry(C);
				return;
			}
		}
	}
}

float CSwapTimer::EntryAlpha(const SSwapEntry &Entry) const
{
	return Smoothstep(Entry.m_AnimPhase);
}

float CSwapTimer::AcceptDimFactor(const SSwapEntry &Entry) const
{
	return DIMMED_ALPHA + (1.0f - DIMMED_ALPHA) * Smoothstep(Entry.m_AcceptPhase);
}

void CSwapTimer::UpdateAnimations()
{
	const float FrameTime = Client()->RenderFrameTime();
	const float Step = FrameTime / maximum(ANIMATION_TIME_SECONDS, 0.001f);

	for(int Conn = 0; Conn < NUM_DUMMIES; Conn++)
	{
		SSwapEntry &Entry = m_aEntries[Conn];
		if(!Entry.m_Active)
			continue;

		UpdateTeeInfos(Entry, Conn);
		const float AcceptTarget = IsAcceptEnabled(Entry) ? 1.0f : 0.0f;

		if(Entry.m_AcceptPhase < AcceptTarget)
			Entry.m_AcceptPhase = minimum(Entry.m_AcceptPhase + Step, AcceptTarget);
		else if(Entry.m_AcceptPhase > AcceptTarget)
			Entry.m_AcceptPhase = maximum(Entry.m_AcceptPhase - Step, AcceptTarget);

		if(Entry.m_Closing)
		{
			Entry.m_AnimPhase -= Step;
			if(Entry.m_AnimPhase <= 0.0f)
				ClearEntry(Conn);
		}
		else if(Entry.m_AnimPhase < 1.0f)
		{
			Entry.m_AnimPhase = minimum(Entry.m_AnimPhase + Step, 1.0f);
		}
	}
}

void CSwapTimer::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE || !g_Config.m_BcSwapTimer)
	{
		StopPeek();
		return;
	}

	ExpireEntries();
	UpdateAnimations();

	if(g_Config.m_BcSwapTimerStyle == 1)
		RenderNameplateMode();
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
	GameClient()->m_Binds.GetKey("+bc_swap_peek", Out.m_aPeekKey, sizeof(Out.m_aPeekKey));

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

void CSwapTimer::FormatMinimalText(const SSwapEntry &Entry, float Now, char *pBuf, int BufSize) const
{
	const bool OnCooldown = Now < Entry.m_CooldownEnd;
	const float Target = OnCooldown ? Entry.m_CooldownEnd : Entry.m_ExpireTime;
	str_format(pBuf, BufSize, DEFAULT_MINIMAL_TEXT, maximum(0, round_to_int(Target - Now)));
}

void CSwapTimer::FormatStatusText(const SSwapEntry &Entry, float Now, char *pBuf, int BufSize) const
{
	if(Now < Entry.m_CooldownEnd)
		str_format(pBuf, BufSize, Localize("Wait %ds"), maximum(0, round_to_int(Entry.m_CooldownEnd - Now)));
	else
		str_format(pBuf, BufSize, Localize("%ds left"), maximum(0, round_to_int(Entry.m_ExpireTime - Now)));
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

	char aTimeBuf[32];
	FormatStatusText(Entry, Now, aTimeBuf, sizeof(aTimeBuf));

	const char *pFrom = Entry.m_Incoming ? DisplayName(Entry) : Localize("You");
	const char *pTo = Entry.m_Incoming ? Localize("You") : DisplayName(Entry);

	char aBuf[ENTRY_TEXT_SIZE];
	str_format(aBuf, sizeof(aBuf), "%s → %s  (%s)", pFrom, pTo, aTimeBuf);

	SHotkeyLayout Hotkeys;
	BuildHotkeyLayout(true, FontSize, 6.0f * Scale, 2.0f * Scale, 0.0f, Hotkeys);

	const float Icons = TeeIconSize(FontSize) * 2.0f;
	return TextRender()->TextWidth(FontSize, aBuf, -1, -1.0f) + Hotkeys.m_Width + Icons;
}

void CSwapTimer::RenderLine(const SSwapEntry &Entry, float X, float Y, float Scale, float Now, float Alpha, bool ShowHotkeys)
{
	const float FontSize = 8.0f * Scale;
	const ColorRGBA Color = s_TextColor.WithMultipliedAlpha(Alpha);

	char aTimeBuf[32];
	FormatStatusText(Entry, Now, aTimeBuf, sizeof(aTimeBuf));

	const bool Incoming = Entry.m_Incoming;
	const char *pFromName = Incoming ? DisplayName(Entry) : Localize("You");
	const char *pToName = Incoming ? Localize("You") : DisplayName(Entry);
	const auto &pFromTee = Incoming ? Entry.m_pOtherTee : Entry.m_pSelfTee;
	const auto &pToTee = Incoming ? Entry.m_pSelfTee : Entry.m_pOtherTee;

	char aTail[64];
	str_format(aTail, sizeof(aTail), "  (%s)", aTimeBuf);

	float CursorX = X;
	const auto DrawText = [&](const char *pText) {
		TextRender()->TextColor(Color);
		TextRender()->Text(CursorX, Y, FontSize, pText, -1.0f);
		CursorX += TextRender()->TextWidth(FontSize, pText, -1, -1.0f);
	};

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
	if(ForcePreview)
	{
		MaxWidth = MeasureLineWidth(MakePreviewEntry(Now), Scale, Now);
	}
	else
	{
		const SSwapEntry &Entry = m_aEntries[VisibleConn()];
		if(!Entry.m_Active)
			return {0.0f, 0.0f, 0.0f, 0.0f};
		MaxWidth = MeasureLineWidth(Entry, Scale, Now);
	}

	if(MaxWidth <= 0.0f)
		return {0.0f, 0.0f, 0.0f, 0.0f};

	const float PaddingX = 3.0f * Scale;
	const float PaddingY = 2.0f * Scale;

	CUIRect Rect;
	Rect.w = MaxWidth + PaddingX * 2.0f;
	Rect.h = LineHeight(Scale) + PaddingY * 2.0f;
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

	float BackgroundAlpha = 1.0f;
	if(!ForcePreview)
	{
		if(!m_aEntries[Conn].m_Active)
			return;
		BackgroundAlpha = EntryAlpha(m_aEntries[Conn]);
		if(BackgroundAlpha <= 0.01f)
			return;
	}

	if(Layout.m_BackgroundEnabled)
	{
		const int Corners = HudLayout::BackgroundCorners(IGraphics::CORNER_ALL, Rect.x, Rect.y, Rect.w, Rect.h, Width, HUD_CANVAS_HEIGHT);
		const ColorRGBA Background = color_cast<ColorRGBA>(ColorHSLA(Layout.m_BackgroundColor, true)).WithMultipliedAlpha(BackgroundAlpha);
		Graphics()->DrawRect(Rect.x, Rect.y, Rect.w, Rect.h, Background, Corners, 4.0f * Scale);
	}

	if(ForcePreview)
	{
		RenderLine(MakePreviewEntry(Now), X, TopY, Scale, Now, 1.0f, true);
		return;
	}

	RenderLine(m_aEntries[Conn], X, TopY, Scale, Now, BackgroundAlpha, true);
}

void CSwapTimer::RenderNameplateCard(const SSwapEntry &Entry, int ClientId, float Now)
{
	const float UserScale = std::clamp(g_Config.m_BcSwapTimerSize / 100.0f, 0.5f, 2.0f);
	const float FontSize = 20.0f * UserScale;
	const float HotkeyFontSize = FontSize * 0.8f;
	const float RowSpacing = FontSize * 0.12f;

	char aBuf[ENTRY_TEXT_SIZE];
	FormatMinimalText(Entry, Now, aBuf, MINIMAL_TEXT_SIZE);

	SHotkeyLayout Hotkeys;
	BuildHotkeyLayout(true, HotkeyFontSize, 0.0f, HotkeyFontSize * 0.25f, HotkeyFontSize * 0.7f, Hotkeys);

	const float TextWidth = TextRender()->TextWidth(FontSize, aBuf, -1, -1.0f);
	const float BoxHeight = FontSize + (Hotkeys.m_HasAny ? RowSpacing + HotkeyFontSize : 0.0f);

	const vec2 Position = GameClient()->m_aClients[ClientId].m_RenderPos;
	const float PlateHeight = maximum(GameClient()->m_NamePlates.GetNamePlateOffset(ClientId), 38.0f);
	const float BoxY = Position.y - PlateHeight - FontSize * 0.2f - BoxHeight;

	const float Alpha = EntryAlpha(Entry);
	TextRender()->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f * Alpha));
	TextRender()->TextColor(s_TextColor.WithMultipliedAlpha(Alpha));
	TextRender()->Text(Position.x - TextWidth * 0.5f, BoxY, FontSize, aBuf, -1.0f);

	RenderHotkeyLayout(Hotkeys, Position.x - Hotkeys.m_Width * 0.5f, BoxY + FontSize + RowSpacing, Alpha, AcceptDimFactor(Entry));

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
	if(!m_aEntries[Conn].m_Active)
		return;

	const int ClientId = GameClient()->m_aLocalIds[Conn];
	if(ClientId < 0 || !GameClient()->m_aClients[ClientId].m_Active || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
		return;
	if(!GameClient()->OptimizerAllowRenderPos(GameClient()->m_aClients[ClientId].m_RenderPos))
		return;

	const float Now = Client()->LocalTime();

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	const CScreenRect ScreenRect = Graphics()->GetScreen();
	ScreenX0 = ScreenRect.m_TopLeft.x;
	ScreenY0 = ScreenRect.m_TopLeft.y;
	ScreenX1 = ScreenRect.m_BottomRight.x;
	ScreenY1 = ScreenRect.m_BottomRight.y;

	CScreenRect WorldScreen = Graphics()->MapScreenToWorld(
		GameClient()->m_Camera.m_Center.x, GameClient()->m_Camera.m_Center.y,
		100.0f, 100.0f, 100.0f, 0, 0,
		Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom);
	Graphics()->MapScreen(WorldScreen);

	RenderNameplateCard(m_aEntries[Conn], ClientId, Now);

	Graphics()->MapScreen(CScreenRect(vec2(ScreenX0, ScreenY0), vec2(ScreenX1, ScreenY1)));
}
