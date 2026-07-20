/* Copyright © 2026 BestProject Team */
#include "clans.h"

#include <base/log.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/graphics.h>
#include <engine/map.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/shared/protocol.h>
#include <engine/serverbrowser.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>
#include <game/localization.h>

#include <cstdio>
#include <cstring>

static const char *const SESSION_FILE = "clans_session.json";

static const char *JsonString(const json_value *pObj, const char *pKey, const char *pDefault = "")
{
	const json_value *pVal = json_object_get(pObj, pKey);
	if(pVal && pVal->type == json_string)
		return pVal->u.string.ptr;
	return pDefault;
}

static int JsonInt(const json_value *pObj, const char *pKey, int Default = 0)
{
	const json_value *pVal = json_object_get(pObj, pKey);
	if(pVal && pVal->type == json_integer)
		return (int)pVal->u.integer;
	if(pVal && pVal->type == json_boolean)
		return pVal->u.boolean ? 1 : 0;
	return Default;
}

static bool JsonBool(const json_value *pObj, const char *pKey, bool Default = false)
{
	const json_value *pVal = json_object_get(pObj, pKey);
	if(pVal && pVal->type == json_boolean)
		return pVal->u.boolean != 0;
	return Default;
}

static void JsonEscapeString(char *pOut, int OutSize, const char *pIn)
{
	if(!pOut || OutSize <= 0)
		return;
	int Out = 0;
	for(int i = 0; pIn && pIn[i] && Out + 1 < OutSize; i++)
	{
		const char C = pIn[i];
		if((C == '\\' || C == '"' || C == '\n' || C == '\r' || C == '\t') && Out + 2 >= OutSize)
			break;
		if(C == '\\' || C == '"')
		{
			pOut[Out++] = '\\';
			pOut[Out++] = C;
		}
		else if(C == '\n')
		{
			pOut[Out++] = '\\';
			pOut[Out++] = 'n';
		}
		else if(C == '\r')
		{
			pOut[Out++] = '\\';
			pOut[Out++] = 'r';
		}
		else if(C == '\t')
		{
			pOut[Out++] = '\\';
			pOut[Out++] = 't';
		}
		else
			pOut[Out++] = C;
	}
	pOut[Out] = '\0';
}

CClans::ERole CClans::ParseRole(const char *pRole)
{
	if(!pRole)
		return ERole::NONE;
	if(!str_comp(pRole, "president"))
		return ERole::PRESIDENT;
	if(!str_comp(pRole, "vice_president"))
		return ERole::VICE_PRESIDENT;
	if(!str_comp(pRole, "veteran"))
		return ERole::VETERAN;
	if(!str_comp(pRole, "member"))
		return ERole::MEMBER;
	return ERole::NONE;
}

void CClans::OnInit()
{
	LoadSession();
	if(m_LoggedIn)
	{
		m_View = InClan() ? EView::CLAN : EView::LANDING;
		// Defer API sync until the Clans tab is opened.
		m_NeedInitialSync = true;
	}
}

void CClans::OnReset()
{
}

void CClans::OnShutdown()
{
	if(m_pPending)
	{
		m_pPending->Abort();
		m_pPending = nullptr;
	}
	if(m_pBgPending)
	{
		m_pBgPending->Abort();
		m_pBgPending = nullptr;
	}
	SaveSession();
}

bool CClans::IsClansUiOpen() const
{
	const CMenus &Menus = GameClient()->m_Menus;
	if(!Menus.IsActive())
		return false;
	if(Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return Menus.GamePage() == CMenus::PAGE_CLANS;
	return Menus.MenuPage() == CMenus::PAGE_CLANS;
}

void CClans::OnUpdate()
{
	HandlePending();
	HandleBackground();

	for(auto It = m_Toasts.begin(); It != m_Toasts.end();)
	{
		It->m_TimeLeft -= Client()->RenderFrameTime();
		if(It->m_TimeLeft <= 0.0f)
			It = m_Toasts.erase(It);
		else
			++It;
	}

	if(!m_LoggedIn)
		return;

	const bool UiOpen = IsClansUiOpen();
	if(UiOpen)
	{
		if(m_NeedInitialSync)
		{
			m_NeedInitialSync = false;
			RefreshRecentClans();
			RefreshMe();
		}
		MaybeAutoRefresh();
		MaybePollNotifications();
	}

	// Presence heartbeats only while Clans tab is open.
	// Exception: report joining a server even with the tab closed (must be in a clan).
	if(InClan())
		MaybePushPresence(UiOpen);
}

bool CClans::ResolveBaseUrl(char *pOut, int OutSize) const
{
	const char *pUrl = g_Config.m_BcClansApiUrl;
	const bool IsProd = !str_comp(pUrl, "https://clans.bestclient.fun");
	const bool IsLocal = !str_comp(pUrl, "http://127.0.0.1:8787") || !str_comp(pUrl, "http://localhost:8787");
	if(IsProd || (g_Config.m_BcClansAllowLocalDev && IsLocal))
	{
		str_copy(pOut, pUrl, OutSize);
		// strip trailing slash
		int Len = str_length(pOut);
		if(Len > 0 && pOut[Len - 1] == '/')
			pOut[Len - 1] = '\0';
		return true;
	}
	str_copy(pOut, "https://clans.bestclient.fun", OutSize);
	return true;
}

void CClans::SetError(const char *pErrorCode, const char *pFallback)
{
	m_ErrorOfferDiscord = false;
	if(pErrorCode)
	{
		if(!str_comp(pErrorCode, "clan.tag_taken"))
			str_copy(m_aError, Localize("This clan tag is already taken"), sizeof(m_aError));
		else if(!str_comp(pErrorCode, "clan.tag_reserved") || !str_comp(pErrorCode, "clan.tag_reserved_denied"))
		{
			str_copy(m_aError, Localize("This clan tag is reserved. Contact us on Discord to request it."), sizeof(m_aError));
			m_ErrorOfferDiscord = true;
		}
		else if(!str_comp(pErrorCode, "auth.bad_credentials"))
			str_copy(m_aError, Localize("Invalid nickname or password"), sizeof(m_aError));
		else if(!str_comp(pErrorCode, "auth.taken"))
			str_copy(m_aError, Localize("Nickname already taken"), sizeof(m_aError));
		else if(!str_comp(pErrorCode, "invite.invalid"))
			str_copy(m_aError, Localize("Invalid invite code"), sizeof(m_aError));
		else if(!str_comp(pErrorCode, "rate_limited"))
			str_copy(m_aError, Localize("Too many requests, try later"), sizeof(m_aError));
		else if(!str_comp(pErrorCode, "clan.in_clan"))
			str_copy(m_aError, Localize("Already in a clan"), sizeof(m_aError));
		else if(!str_comp(pErrorCode, "clan.full"))
			str_copy(m_aError, Localize("Clan is full"), sizeof(m_aError));
		else if(!str_comp(pErrorCode, "clan.banned"))
			str_copy(m_aError, Localize("You are banned from this clan"), sizeof(m_aError));
		else if(!str_comp(pErrorCode, "clan.not_found"))
			str_copy(m_aError, Localize("Clan no longer exists"), sizeof(m_aError));
		else if(!str_comp(pErrorCode, "clan.membership_cooldown"))
		{
			if(!IsMembershipCooldownActive())
				ArmMembershipCooldown();
			const int Secs = maximum(1, MembershipCooldownSecondsLeft());
			str_format(m_aError, sizeof(m_aError), Localize("Wait %d seconds before joining or leaving again"), Secs);
		}
		else if(!str_comp(pErrorCode, "clan.chat_cooldown"))
		{
			if(!IsAnnounceCooldownActive())
				ArmAnnounceCooldown();
			const int Secs = maximum(1, AnnounceCooldownSecondsLeft());
			str_format(m_aError, sizeof(m_aError), Localize("Wait %d seconds before sending another message"), Secs);
		}
		else if(!str_comp(pErrorCode, "clan.cooldown"))
			str_copy(m_aError, Localize("Clan create cooldown is active"), sizeof(m_aError));
		else if(pFallback && pFallback[0])
			str_copy(m_aError, pFallback, sizeof(m_aError));
		else
			str_copy(m_aError, pErrorCode, sizeof(m_aError));
	}
	else if(pFallback)
		str_copy(m_aError, pFallback, sizeof(m_aError));
}

void CClans::SetStatus(const char *pMsg)
{
	str_copy(m_aStatus, pMsg ? pMsg : "", sizeof(m_aStatus));
}

void CClans::AuthHeader(CHttpRequest *pReq) const
{
	if(m_aAccessToken[0])
	{
		char aAuth[160];
		str_format(aAuth, sizeof(aAuth), "Bearer %s", m_aAccessToken);
		pReq->HeaderString("Authorization", aAuth);
	}
}

void CClans::BeginRequest(std::shared_ptr<CHttpRequest> pReq, int Kind)
{
	if(m_pPending)
	{
		m_pPending->Abort();
		m_pPending = nullptr;
	}
	pReq->LogProgress(HTTPLOG::NONE);
	m_PendingKind = Kind;
	m_pPending = std::move(pReq);
	m_aError[0] = '\0';
	Http()->Run(m_pPending);
}

void CClans::BeginBackground(std::shared_ptr<CHttpRequest> pReq, int Kind)
{
	if(m_pBgPending && !m_pBgPending->Done())
	{
		// High-priority: recent/ann-read always preempt. Presence preempts soft polls
		// so Main-list online status stays fresh (client-reported, not master).
		const bool SoftCurrent = m_BgKind == REQ_NOTIFS || m_BgKind == REQ_NOTIF_READ || m_BgKind == REQ_PRESENCE;
		if(Kind == REQ_RECENT || Kind == REQ_ANN_READ || (Kind == REQ_PRESENCE && SoftCurrent))
		{
			m_pBgPending->Abort();
			m_pBgPending = nullptr;
		}
		else
			return;
	}
	pReq->LogProgress(HTTPLOG::NONE);
	m_BgKind = Kind;
	m_pBgPending = std::move(pReq);
	Http()->Run(m_pBgPending);
}

void CClans::BuildSkinJson(char *pBuf, int BufSize) const
{
	str_format(pBuf, BufSize,
		"{\"name\":\"%s\",\"color_body\":%d,\"color_feet\":%d,\"use_custom_color\":%s}",
		g_Config.m_ClPlayerSkin[0] ? g_Config.m_ClPlayerSkin : "default",
		g_Config.m_ClPlayerColorBody,
		g_Config.m_ClPlayerColorFeet,
		g_Config.m_ClPlayerUseCustomColor ? "true" : "false");
}

void CClans::LeaveClanLocal()
{
	m_aClanId[0] = '\0';
	m_aClanTag[0] = '\0';
	m_Role = ERole::NONE;
	m_Clan = SClanSnapshot{};
	ClearClanTagLock();
	SaveSession();
	if(m_View == EView::CLAN || m_View == EView::APPLICATIONS || m_View == EView::ANNOUNCEMENTS || m_View == EView::SETTINGS)
		m_View = EView::LANDING;
	RefreshRecentClans();
}

bool CClans::IsMembershipCooldownActive() const
{
	return m_MembershipCooldownUntil > 0 && time_get() < m_MembershipCooldownUntil;
}

int CClans::MembershipCooldownSecondsLeft() const
{
	if(!IsMembershipCooldownActive())
		return 0;
	const int64_t Left = m_MembershipCooldownUntil - time_get();
	return (int)((Left + time_freq() - 1) / time_freq());
}

void CClans::ArmMembershipCooldown()
{
	m_MembershipCooldownUntil = time_get() + time_freq() * 60;
}

bool CClans::GuardMembershipCooldown()
{
	if(!IsMembershipCooldownActive())
		return true;
	SetError("clan.membership_cooldown", Localize("Wait before joining or leaving again"));
	return false;
}

void CClans::ArmAnnounceCooldown()
{
	m_AnnounceCooldownUntil = time_get() + time_freq() * 5;
}

bool CClans::IsAnnounceCooldownActive() const
{
	return m_AnnounceCooldownUntil > 0 && time_get() < m_AnnounceCooldownUntil;
}

int CClans::AnnounceCooldownSecondsLeft() const
{
	if(!IsAnnounceCooldownActive())
		return 0;
	const int64_t Left = m_AnnounceCooldownUntil - time_get();
	return (int)((Left + time_freq() - 1) / time_freq());
}

bool CClans::GuardAnnounceCooldown()
{
	if(!IsAnnounceCooldownActive())
		return true;
	SetError("clan.chat_cooldown", Localize("Wait before sending another message"));
	return false;
}

void CClans::ApplyClanTagLock(const char *pTag)
{
	if(!pTag || !pTag[0])
		return;
	// Avoid ChangeInfo spam on every clan poll / snapshot parse.
	if(m_PlayerClanLocked && !str_comp(m_aLockedTag, pTag) && !str_comp(g_Config.m_PlayerClan, pTag))
		return;
	if(!m_PlayerClanLocked)
		str_copy(m_aPlayerClanBackup, g_Config.m_PlayerClan, sizeof(m_aPlayerClanBackup));
	str_copy(m_aLockedTag, pTag, sizeof(m_aLockedTag));
	str_copy(g_Config.m_PlayerClan, pTag, sizeof(g_Config.m_PlayerClan));
	m_PlayerClanLocked = true;
	GameClient()->SendInfo(false);
}

void CClans::ClearClanTagLock()
{
	if(!m_PlayerClanLocked)
		return;
	str_copy(g_Config.m_PlayerClan, m_aPlayerClanBackup, sizeof(g_Config.m_PlayerClan));
	m_aLockedTag[0] = '\0';
	m_aPlayerClanBackup[0] = '\0';
	m_PlayerClanLocked = false;
	GameClient()->SendInfo(false);
}

int CClans::GetUnreadCount() const
{
	int Count = 0;
	for(const auto &N : m_vNotifications)
	{
		if(!N.m_Read)
			Count++;
	}
	return Count;
}

void CClans::CollectUnleashed(std::vector<SUnleashedPlayer> *pOut) const
{
	// Server provides unleashed via clan snapshot; keep API for callers.
	*pOut = m_Clan.m_vUnleashed;
}

bool CClans::TryRefreshSession()
{
	if(!m_aRefreshToken[0] || m_pPending)
		return false;
	char aBase[128];
	ResolveBaseUrl(aBase, sizeof(aBase));
	char aUrl[160];
	str_format(aUrl, sizeof(aUrl), "%s/api/auth/refresh", aBase);
	char aJson[200];
	str_format(aJson, sizeof(aJson), "{\"refresh_token\":\"%s\"}", m_aRefreshToken);
	auto pReq = HttpPostJson(aUrl, aJson);
	pReq->FailOnErrorStatus(false);
	BeginRequest(std::move(pReq), REQ_REFRESH);
	SetStatus(Localize("Refreshing session..."));
	return true;
}

void CClans::SaveSession() const
{
	if(!m_LoggedIn || !m_aAccessToken[0])
	{
		Storage()->RemoveFile(SESSION_FILE, IStorage::TYPE_SAVE);
		return;
	}
	char aJson[512];
	str_format(aJson, sizeof(aJson),
		"{\"access_token\":\"%s\",\"refresh_token\":\"%s\",\"nickname\":\"%s\",\"user_id\":\"%s\",\"clan_id\":\"%s\",\"clan_tag\":\"%s\"}",
		m_aAccessToken, m_aRefreshToken, m_aNickname, m_aUserId, m_aClanId, m_aClanTag);
	IOHANDLE File = Storage()->OpenFile(SESSION_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(File)
	{
		io_write(File, aJson, str_length(aJson));
		io_close(File);
	}
}

void CClans::LoadSession()
{
	IOHANDLE File = Storage()->OpenFile(SESSION_FILE, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(!File)
		return;
	char aBuf[1024];
	unsigned Len = io_read(File, aBuf, sizeof(aBuf) - 1);
	io_close(File);
	aBuf[Len] = '\0';
	json_value *pRoot = json_parse(aBuf, Len);
	if(!pRoot || pRoot->type != json_object)
	{
		if(pRoot)
			json_value_free(pRoot);
		return;
	}
	str_copy(m_aAccessToken, JsonString(pRoot, "access_token"), sizeof(m_aAccessToken));
	str_copy(m_aRefreshToken, JsonString(pRoot, "refresh_token"), sizeof(m_aRefreshToken));
	str_copy(m_aNickname, JsonString(pRoot, "nickname"), sizeof(m_aNickname));
	str_copy(m_aUserId, JsonString(pRoot, "user_id"), sizeof(m_aUserId));
	str_copy(m_aClanId, JsonString(pRoot, "clan_id"), sizeof(m_aClanId));
	str_copy(m_aClanTag, JsonString(pRoot, "clan_tag"), sizeof(m_aClanTag));
	json_value_free(pRoot);
	if(m_aAccessToken[0])
	{
		m_LoggedIn = true;
		if(m_aClanTag[0])
			ApplyClanTagLock(m_aClanTag);
	}
}

void CClans::ClearSession()
{
	if(m_pPending)
	{
		m_pPending->Abort();
		m_pPending = nullptr;
		m_PendingKind = REQ_NONE;
	}
	if(m_pBgPending)
	{
		m_pBgPending->Abort();
		m_pBgPending = nullptr;
		m_BgKind = REQ_NONE;
	}
	m_LoggedIn = false;
	m_aAccessToken[0] = '\0';
	m_aRefreshToken[0] = '\0';
	m_aUserId[0] = '\0';
	m_aNickname[0] = '\0';
	m_aClanId[0] = '\0';
	m_aClanTag[0] = '\0';
	m_aRecentPendingUserId[0] = '\0';
	m_Role = ERole::NONE;
	m_Clan = SClanSnapshot{};
	m_vRecentClans.clear();
	m_vNotifications.clear();
	m_vAnnouncements.clear();
	m_vApplications.clear();
	ClearClanTagLock();
	Storage()->RemoveFile(SESSION_FILE, IStorage::TYPE_SAVE);
	m_View = EView::AUTH;
}

void CClans::SetView(EView View)
{
	m_View = View;
	if(View == EView::ANNOUNCEMENTS)
		MarkAnnouncementsRead();
}

void CClans::MarkAnnouncementsRead()
{
	if(!m_aClanId[0] || m_Clan.m_UnreadAnnouncements <= 0)
	{
		m_Clan.m_UnreadAnnouncements = 0;
		return;
	}
	m_Clan.m_UnreadAnnouncements = 0;
	char aBase[128];
	ResolveBaseUrl(aBase, sizeof(aBase));
	char aUrl[192];
	str_format(aUrl, sizeof(aUrl), "%s/api/clans/%s/announcements/read", aBase, m_aClanId);
	auto pReq = HttpPostJson(aUrl, "{}");
	AuthHeader(pReq.get());
	pReq->FailOnErrorStatus(false);
	BeginBackground(std::move(pReq), REQ_ANN_READ);
}

void CClans::Register(const char *pNickname, const char *pPassword)
{
	char aBase[128];
	ResolveBaseUrl(aBase, sizeof(aBase));
	char aUrl[160];
	str_format(aUrl, sizeof(aUrl), "%s/api/auth/register", aBase);
	char aSkin[192];
	BuildSkinJson(aSkin, sizeof(aSkin));
	char aEscNick[64];
	char aEscPass[128];
	JsonEscapeString(aEscNick, sizeof(aEscNick), pNickname ? pNickname : "");
	JsonEscapeString(aEscPass, sizeof(aEscPass), pPassword ? pPassword : "");
	char aJson[640];
	str_format(aJson, sizeof(aJson),
		"{\"nickname\":\"%s\",\"password\":\"%s\",\"skin\":%s}",
		aEscNick, aEscPass, aSkin);
	auto pReq = HttpPostJson(aUrl, aJson);
	pReq->FailOnErrorStatus(false);
	pReq->Timeout(CTimeout{4000, 15000, 500, 5});
	BeginRequest(std::move(pReq), REQ_REGISTER);
	SetStatus(Localize("Registering..."));
}

void CClans::Login(const char *pNickname, const char *pPassword)
{
	char aBase[128];
	ResolveBaseUrl(aBase, sizeof(aBase));
	char aUrl[160];
	str_format(aUrl, sizeof(aUrl), "%s/api/auth/login", aBase);
	char aSkin[192];
	BuildSkinJson(aSkin, sizeof(aSkin));
	char aEscNick[64];
	char aEscPass[128];
	JsonEscapeString(aEscNick, sizeof(aEscNick), pNickname ? pNickname : "");
	JsonEscapeString(aEscPass, sizeof(aEscPass), pPassword ? pPassword : "");
	char aJson[640];
	str_format(aJson, sizeof(aJson),
		"{\"nickname\":\"%s\",\"password\":\"%s\",\"skin\":%s}",
		aEscNick, aEscPass, aSkin);
	auto pReq = HttpPostJson(aUrl, aJson);
	pReq->FailOnErrorStatus(false);
	BeginRequest(std::move(pReq), REQ_LOGIN);
	SetStatus(Localize("Logging in..."));
}

void CClans::Logout()
{
	if(m_aAccessToken[0])
	{
		char aBase[128];
		ResolveBaseUrl(aBase, sizeof(aBase));
		char aUrl[160];
		str_format(aUrl, sizeof(aUrl), "%s/api/auth/logout", aBase);
		auto pReq = HttpPostJson(aUrl, "{}");
		AuthHeader(pReq.get());
		pReq->FailOnErrorStatus(false);
		BeginRequest(std::move(pReq), REQ_LOGOUT);
	}
	ClearSession();
}

void CClans::RefreshMe()
{
	char aBase[128];
	ResolveBaseUrl(aBase, sizeof(aBase));
	char aUrl[160];
	str_format(aUrl, sizeof(aUrl), "%s/api/me", aBase);
	auto pReq = HttpGet(aUrl);
	AuthHeader(pReq.get());
	pReq->FailOnErrorStatus(false);
	BeginRequest(std::move(pReq), REQ_ME);
}

void CClans::RefreshCatalog()
{
	char aBase[128];
	ResolveBaseUrl(aBase, sizeof(aBase));
	char aUrl[160];
	str_format(aUrl, sizeof(aUrl), "%s/list.json", aBase);
	auto pReq = HttpGet(aUrl);
	pReq->FailOnErrorStatus(false);
	BeginRequest(std::move(pReq), REQ_CATALOG);
}

void CClans::CreateClan(const char *pName, const char *pTag, const char *pDescription, int IconId, unsigned Color, int Country, const char *pJoinPolicy, int MaxMembers)
{
	if(!GuardMembershipCooldown())
		return;
	char aBase[128];
	ResolveBaseUrl(aBase, sizeof(aBase));
	char aUrl[160];
	str_format(aUrl, sizeof(aUrl), "%s/api/clans", aBase);
	char aEscName[128], aEscTag[48], aEscDesc[512];
	JsonEscapeString(aEscName, sizeof(aEscName), pName ? pName : "");
	JsonEscapeString(aEscTag, sizeof(aEscTag), pTag ? pTag : "");
	JsonEscapeString(aEscDesc, sizeof(aEscDesc), pDescription ? pDescription : "");
	char aJson[960];
	str_format(aJson, sizeof(aJson),
		"{\"name\":\"%s\",\"tag\":\"%s\",\"description\":\"%s\",\"icon_id\":%d,\"color\":%u,\"country\":%d,\"join_policy\":\"%s\",\"max_members\":%d}",
		aEscName, aEscTag, aEscDesc, IconId, Color, Country, pJoinPolicy, MaxMembers);
	auto pReq = HttpPostJson(aUrl, aJson);
	AuthHeader(pReq.get());
	pReq->FailOnErrorStatus(false);
	BeginRequest(std::move(pReq), REQ_CREATE);
	SetStatus(Localize("Creating clan..."));
}

void CClans::UpdateClanSettings(const char *pName, const char *pDescription, int IconId, unsigned Color, int Country)
{
	if(!m_aClanId[0])
		return;
	char aBase[128];
	ResolveBaseUrl(aBase, sizeof(aBase));
	char aUrl[192];
	str_format(aUrl, sizeof(aUrl), "%s/api/clans/%s/settings", aBase, m_aClanId);
	char aEscName[128], aEscDesc[512];
	JsonEscapeString(aEscName, sizeof(aEscName), pName ? pName : "");
	JsonEscapeString(aEscDesc, sizeof(aEscDesc), pDescription ? pDescription : "");
	char aJson[960];
	str_format(aJson, sizeof(aJson),
		"{\"name\":\"%s\",\"description\":\"%s\",\"icon_id\":%d,\"color\":%u,\"country\":%d}",
		aEscName, aEscDesc, IconId, Color, Country);
	auto pReq = HttpPostJson(aUrl, aJson);
	AuthHeader(pReq.get());
	pReq->FailOnErrorStatus(false);
	BeginRequest(std::move(pReq), REQ_UPDATE);

	// Keep the settings form and clan page in sync while the request is in flight.
	// Clearing the form cache and reloading from the old snapshot caused a one-step lag on save.
	str_copy(m_Clan.m_aName, pName ? pName : "", sizeof(m_Clan.m_aName));
	str_copy(m_Clan.m_aDescription, pDescription ? pDescription : "", sizeof(m_Clan.m_aDescription));
	m_Clan.m_IconId = IconId;
	m_Clan.m_Color = Color;
	m_Clan.m_Country = Country;
	SetStatus(Localize("Saving..."));
}

void CClans::Join(const char *pClanId)
{
	if(!GuardMembershipCooldown())
		return;
	char aBase[128];
	ResolveBaseUrl(aBase, sizeof(aBase));
	char aUrl[192];
	str_format(aUrl, sizeof(aUrl), "%s/api/clans/%s/join", aBase, pClanId);
	auto pReq = HttpPostJson(aUrl, "{}");
	AuthHeader(pReq.get());
	pReq->FailOnErrorStatus(false);
	BeginRequest(std::move(pReq), REQ_JOIN);
}

void CClans::Apply(const char *pClanId, const char *pText)
{
	char aBase[128];
	ResolveBaseUrl(aBase, sizeof(aBase));
	char aUrl[192];
	str_format(aUrl, sizeof(aUrl), "%s/api/clans/%s/apply", aBase, pClanId);
	char aJson[400];
	str_format(aJson, sizeof(aJson), "{\"text\":\"%s\"}", pText ? pText : "");
	auto pReq = HttpPostJson(aUrl, aJson);
	AuthHeader(pReq.get());
	pReq->FailOnErrorStatus(false);
	BeginRequest(std::move(pReq), REQ_APPLY);
}

void CClans::JoinCode(const char *pCode)
{
	if(!GuardMembershipCooldown())
		return;
	char aBase[128];
	ResolveBaseUrl(aBase, sizeof(aBase));
	char aUrl[160];
	str_format(aUrl, sizeof(aUrl), "%s/api/clans/join-code", aBase);
	char aJson[64];
	str_format(aJson, sizeof(aJson), "{\"code\":\"%s\"}", pCode);
	auto pReq = HttpPostJson(aUrl, aJson);
	AuthHeader(pReq.get());
	pReq->FailOnErrorStatus(false);
	BeginRequest(std::move(pReq), REQ_JOIN_CODE);
}

void CClans::Leave()
{
	if(!m_aClanId[0])
		return;
	if(!GuardMembershipCooldown())
		return;
	char aBase[128];
	ResolveBaseUrl(aBase, sizeof(aBase));
	char aUrl[192];
	str_format(aUrl, sizeof(aUrl), "%s/api/clans/%s/leave", aBase, m_aClanId);
	auto pReq = HttpPostJson(aUrl, "{}");
	AuthHeader(pReq.get());
	pReq->FailOnErrorStatus(false);
	BeginRequest(std::move(pReq), REQ_LEAVE);
}

void CClans::Disband()
{
	if(!m_aClanId[0])
		return;
	if(!GuardMembershipCooldown())
		return;
	char aBase[128];
	ResolveBaseUrl(aBase, sizeof(aBase));
	char aUrl[192];
	str_format(aUrl, sizeof(aUrl), "%s/api/clans/%s/disband", aBase, m_aClanId);
	auto pReq = HttpPostJson(aUrl, "{}");
	AuthHeader(pReq.get());
	pReq->FailOnErrorStatus(false);
	BeginRequest(std::move(pReq), REQ_DISBAND);
}

void CClans::RejoinAsPresident(const char *pClanId)
{
	if(!GuardMembershipCooldown())
		return;
	char aBase[128];
	ResolveBaseUrl(aBase, sizeof(aBase));
	char aUrl[220];
	str_format(aUrl, sizeof(aUrl), "%s/api/clans/%s/rejoin-as-president", aBase, pClanId);
	auto pReq = HttpPostJson(aUrl, "{}");
	AuthHeader(pReq.get());
	pReq->FailOnErrorStatus(false);
	BeginRequest(std::move(pReq), REQ_REJOIN);
}

void CClans::RefreshClan()
{
	if(!m_aClanId[0])
		return;
	char aBase[128];
	ResolveBaseUrl(aBase, sizeof(aBase));
	char aUrl[192];
	str_format(aUrl, sizeof(aUrl), "%s/api/clans/%s", aBase, m_aClanId);
	auto pReq = HttpGet(aUrl);
	AuthHeader(pReq.get());
	pReq->FailOnErrorStatus(false);
	BeginRequest(std::move(pReq), REQ_CLAN);
}

void CClans::OpenPreview(const char *pClanId)
{
	if(!pClanId || !pClanId[0])
		return;
	str_copy(m_aPreviewClanId, pClanId, sizeof(m_aPreviewClanId));
	m_View = EView::PREVIEW;
	char aBase[128];
	ResolveBaseUrl(aBase, sizeof(aBase));
	char aUrl[192];
	str_format(aUrl, sizeof(aUrl), "%s/api/clans/%s", aBase, pClanId);
	auto pReq = HttpGet(aUrl);
	AuthHeader(pReq.get());
	pReq->FailOnErrorStatus(false);
	BeginRequest(std::move(pReq), REQ_PREVIEW);
	SetStatus(Localize("Loading..."));
}

void CClans::ClearPreview()
{
	m_aPreviewClanId[0] = '\0';
	m_Preview = SClanSnapshot{};
}

void CClans::RotateInvite()
{
	if(!m_aClanId[0])
		return;
	char aBase[128];
	ResolveBaseUrl(aBase, sizeof(aBase));
	char aUrl[220];
	str_format(aUrl, sizeof(aUrl), "%s/api/clans/%s/invite/rotate", aBase, m_aClanId);
	auto pReq = HttpPostJson(aUrl, "{}");
	AuthHeader(pReq.get());
	pReq->FailOnErrorStatus(false);
	BeginRequest(std::move(pReq), REQ_ROTATE);
}

void CClans::Promote(const char *pUserId)
{
	char aBase[128];
	ResolveBaseUrl(aBase, sizeof(aBase));
	char aUrl[256];
	str_format(aUrl, sizeof(aUrl), "%s/api/clans/%s/members/%s/promote", aBase, m_aClanId, pUserId);
	auto pReq = HttpPostJson(aUrl, "{}");
	AuthHeader(pReq.get());
	pReq->FailOnErrorStatus(false);
	BeginRequest(std::move(pReq), REQ_PROMOTE);
}

void CClans::Demote(const char *pUserId)
{
	char aBase[128];
	ResolveBaseUrl(aBase, sizeof(aBase));
	char aUrl[256];
	str_format(aUrl, sizeof(aUrl), "%s/api/clans/%s/members/%s/demote", aBase, m_aClanId, pUserId);
	auto pReq = HttpPostJson(aUrl, "{}");
	AuthHeader(pReq.get());
	pReq->FailOnErrorStatus(false);
	BeginRequest(std::move(pReq), REQ_DEMOTE);
}

void CClans::Kick(const char *pUserId, int BanHours)
{
	char aBase[128];
	ResolveBaseUrl(aBase, sizeof(aBase));
	char aUrl[256];
	str_format(aUrl, sizeof(aUrl), "%s/api/clans/%s/members/%s/kick", aBase, m_aClanId, pUserId);
	char aJson[48];
	str_format(aJson, sizeof(aJson), "{\"ban_hours\":%d}", BanHours);
	auto pReq = HttpPostJson(aUrl, aJson);
	AuthHeader(pReq.get());
	pReq->FailOnErrorStatus(false);
	BeginRequest(std::move(pReq), REQ_KICK);
}

void CClans::RefreshApplications()
{
	if(!m_aClanId[0])
		return;
	char aBase[128];
	ResolveBaseUrl(aBase, sizeof(aBase));
	char aUrl[220];
	str_format(aUrl, sizeof(aUrl), "%s/api/clans/%s/applications", aBase, m_aClanId);
	auto pReq = HttpGet(aUrl);
	AuthHeader(pReq.get());
	pReq->FailOnErrorStatus(false);
	BeginRequest(std::move(pReq), REQ_APPS);
}

void CClans::ApproveApplication(const char *pAppId)
{
	char aBase[128];
	ResolveBaseUrl(aBase, sizeof(aBase));
	char aUrl[256];
	str_format(aUrl, sizeof(aUrl), "%s/api/clans/%s/applications/%s/approve", aBase, m_aClanId, pAppId);
	auto pReq = HttpPostJson(aUrl, "{}");
	AuthHeader(pReq.get());
	pReq->FailOnErrorStatus(false);
	BeginRequest(std::move(pReq), REQ_APPROVE);
}

void CClans::RejectApplication(const char *pAppId)
{
	char aBase[128];
	ResolveBaseUrl(aBase, sizeof(aBase));
	char aUrl[256];
	str_format(aUrl, sizeof(aUrl), "%s/api/clans/%s/applications/%s/reject", aBase, m_aClanId, pAppId);
	auto pReq = HttpPostJson(aUrl, "{}");
	AuthHeader(pReq.get());
	pReq->FailOnErrorStatus(false);
	BeginRequest(std::move(pReq), REQ_REJECT);
}

void CClans::RefreshAnnouncements()
{
	if(!m_aClanId[0])
		return;
	char aBase[128];
	ResolveBaseUrl(aBase, sizeof(aBase));
	char aUrl[220];
	str_format(aUrl, sizeof(aUrl), "%s/api/clans/%s/announcements", aBase, m_aClanId);
	auto pReq = HttpGet(aUrl);
	AuthHeader(pReq.get());
	pReq->FailOnErrorStatus(false);
	BeginRequest(std::move(pReq), REQ_ANNS);
}

void CClans::PostAnnouncement(const char *pText)
{
	if(!GuardAnnounceCooldown())
		return;
	char aBase[128];
	ResolveBaseUrl(aBase, sizeof(aBase));
	char aUrl[220];
	str_format(aUrl, sizeof(aUrl), "%s/api/clans/%s/announcements", aBase, m_aClanId);
	char aEsc[1024];
	JsonEscapeString(aEsc, sizeof(aEsc), pText ? pText : "");
	char aJson[1100];
	str_format(aJson, sizeof(aJson), "{\"text\":\"%s\"}", aEsc);
	auto pReq = HttpPostJson(aUrl, aJson);
	AuthHeader(pReq.get());
	pReq->FailOnErrorStatus(false);
	BeginRequest(std::move(pReq), REQ_POST_ANN);
}

void CClans::RefreshNotifications()
{
	char aBase[128];
	ResolveBaseUrl(aBase, sizeof(aBase));
	char aUrl[160];
	str_format(aUrl, sizeof(aUrl), "%s/api/me/notifications", aBase);
	auto pReq = HttpGet(aUrl);
	AuthHeader(pReq.get());
	pReq->FailOnErrorStatus(false);
	BeginRequest(std::move(pReq), REQ_NOTIFS);
}

void CClans::MarkNotificationRead(const char *pId)
{
	char aBase[128];
	ResolveBaseUrl(aBase, sizeof(aBase));
	char aUrl[200];
	str_format(aUrl, sizeof(aUrl), "%s/api/notifications/%s/read", aBase, pId);
	auto pReq = HttpPostJson(aUrl, "{}");
	AuthHeader(pReq.get());
	pReq->FailOnErrorStatus(false);
	BeginBackground(std::move(pReq), REQ_NOTIF_READ);
}

void CClans::RefreshRecentClans()
{
	if(!m_LoggedIn || !m_aUserId[0])
	{
		m_vRecentClans.clear();
		return;
	}
	char aBase[128];
	ResolveBaseUrl(aBase, sizeof(aBase));
	char aUrl[160];
	str_format(aUrl, sizeof(aUrl), "%s/api/me/recent-clans", aBase);
	auto pReq = HttpGet(aUrl);
	AuthHeader(pReq.get());
	pReq->FailOnErrorStatus(false);
	str_copy(m_aRecentPendingUserId, m_aUserId, sizeof(m_aRecentPendingUserId));
	BeginBackground(std::move(pReq), REQ_RECENT);
}

void CClans::ClearRecentClans()
{
	if(!m_LoggedIn || !m_aUserId[0])
	{
		m_vRecentClans.clear();
		return;
	}
	m_vRecentClans.clear();
	char aBase[128];
	ResolveBaseUrl(aBase, sizeof(aBase));
	char aUrl[192];
	str_format(aUrl, sizeof(aUrl), "%s/api/me/recent-clans/clear", aBase);
	auto pReq = HttpPostJson(aUrl, "{}");
	AuthHeader(pReq.get());
	pReq->FailOnErrorStatus(false);
	str_copy(m_aRecentPendingUserId, m_aUserId, sizeof(m_aRecentPendingUserId));
	BeginRequest(std::move(pReq), REQ_RECENT_CLEAR);
	SetStatus(Localize("Clearing..."));
}

void CClans::RefreshCurrentView()
{
	SetStatus(Localize("Refreshing..."));
	switch(m_View)
	{
	case EView::CLAN:
		RefreshClan();
		break;
	case EView::PREVIEW:
		if(m_aPreviewClanId[0])
			OpenPreview(m_aPreviewClanId);
		break;
	case EView::APPLICATIONS:
		RefreshApplications();
		break;
	case EView::ANNOUNCEMENTS:
		RefreshAnnouncements();
		break;
	case EView::SETTINGS:
		RefreshClan();
		break;
	case EView::RECENT:
		RefreshRecentClans();
		break;
	case EView::SETUP:
		break;
	case EView::BROWSE:
		RefreshCatalog();
		break;
	case EView::LANDING:
	default:
		RefreshCatalog();
		break;
	}
}

void CClans::MaybeAutoRefresh()
{
	if(m_pPending)
		return;

	const int64_t Now = time_get();
	const int64_t Freq = time_freq();

	// Keep polls light: ~500 clients must not hammer the API / master.
	// Manual Refresh still updates immediately; auto is intentionally slow.
	if(InClan() && (m_View == EView::CLAN || m_View == EView::APPLICATIONS || m_View == EView::ANNOUNCEMENTS || m_View == EView::SETTINGS || m_View == EView::BROWSE))
	{
		if(Now - m_LastClanPollTick >= Freq * 60)
		{
			m_LastClanPollTick = Now;
			if(m_View == EView::APPLICATIONS)
				RefreshApplications();
			else if(m_View == EView::ANNOUNCEMENTS)
				RefreshAnnouncements();
			else
				RefreshClan();
			return;
		}
		if(Now - m_LastMePollTick >= Freq * 90)
		{
			m_LastMePollTick = Now;
			RefreshMe();
			return;
		}
	}
	else if(m_View == EView::PREVIEW && m_aPreviewClanId[0] && Now - m_LastClanPollTick >= Freq * 60)
	{
		m_LastClanPollTick = Now;
		OpenPreview(m_aPreviewClanId);
		return;
	}

	if(m_View == EView::LANDING || m_View == EView::BROWSE)
	{
		if(Now - m_LastCatalogPollTick >= Freq * 60)
		{
			m_LastCatalogPollTick = Now;
			RefreshCatalog();
		}
	}
}

void CClans::MaybePushPresence(bool UiOpen)
{
	// Main-list online/playing status is reported by each BestClient to the clans API.
	// Unleashed players are filled server-side from the DDNet master only.
	if(!m_LoggedIn || !InClan())
		return;

	const int ClientState = Client()->State();
	const bool StateChanged = ClientState != m_LastPresenceClientState;
	const bool JoinedServer = StateChanged && ClientState == IClient::STATE_ONLINE;
	m_LastPresenceClientState = ClientState;

	// With Clans tab closed: only notify API when joining a server.
	if(!UiOpen && !JoinedServer)
		return;

	const int64_t Now = time_get();
	if(UiOpen && !StateChanged && Now - m_LastPresenceTick < time_freq() * 30)
		return;

	// Don't burn the tick if the bg slot is busy with a non-soft request.
	if(m_pBgPending && !m_pBgPending->Done())
	{
		const bool SoftCurrent = m_BgKind == REQ_NOTIFS || m_BgKind == REQ_NOTIF_READ || m_BgKind == REQ_PRESENCE;
		if(!SoftCurrent)
			return;
	}

	char aAddr[NETADDR_MAXSTRSIZE] = "";
	char aMap[64] = "";
	int Players = 0;
	int MaxPlayers = 0;
	const bool InGame = ClientState == IClient::STATE_ONLINE;
	if(InGame)
	{
		net_addr_str(&Client()->ServerAddress(), aAddr, sizeof(aAddr), true);
		if(GameClient()->Map())
			str_copy(aMap, GameClient()->Map()->BaseName(), sizeof(aMap));
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(GameClient()->m_Snap.m_apPlayerInfos[i])
				Players++;
		}
		MaxPlayers = maximum(Players, 64);
	}

	char aBase[128];
	ResolveBaseUrl(aBase, sizeof(aBase));
	char aUrl[160];
	str_format(aUrl, sizeof(aUrl), "%s/api/presence", aBase);
	char aSkin[192];
	BuildSkinJson(aSkin, sizeof(aSkin));
	char aEscServer[NETADDR_MAXSTRSIZE * 2];
	char aEscMap[128];
	JsonEscapeString(aEscServer, sizeof(aEscServer), aAddr);
	JsonEscapeString(aEscMap, sizeof(aEscMap), aMap);
	char aJson[640];
	str_format(aJson, sizeof(aJson),
		"{\"online\":true,\"in_game\":%s,\"server\":\"%s\",\"map\":\"%s\",\"players\":%d,\"max_players\":%d,\"skin\":%s}",
		InGame ? "true" : "false", aEscServer, aEscMap, Players, MaxPlayers, aSkin);

	auto pReq = std::make_shared<CHttpRequest>(aUrl);
	pReq->PostJson(aJson);
	AuthHeader(pReq.get());
	pReq->FailOnErrorStatus(false);
	BeginBackground(std::move(pReq), REQ_PRESENCE);
	if(m_BgKind == REQ_PRESENCE && m_pBgPending)
		m_LastPresenceTick = Now;
}

void CClans::MaybePollNotifications()
{
	const int64_t Now = time_get();
	if(Now - m_LastNotifTick < time_freq() * 60)
		return;
	m_LastNotifTick = Now;
	if(m_pBgPending && !m_pBgPending->Done())
		return;

	char aBase[128];
	ResolveBaseUrl(aBase, sizeof(aBase));
	char aUrl[160];
	str_format(aUrl, sizeof(aUrl), "%s/api/me/notifications", aBase);
	auto pReq = HttpGet(aUrl);
	AuthHeader(pReq.get());
	pReq->FailOnErrorStatus(false);
	BeginBackground(std::move(pReq), REQ_NOTIFS);
}

void CClans::ParseAuthResponse(const json_value *pRoot)
{
	str_copy(m_aAccessToken, JsonString(pRoot, "access_token"), sizeof(m_aAccessToken));
	str_copy(m_aRefreshToken, JsonString(pRoot, "refresh_token"), sizeof(m_aRefreshToken));
	const json_value *pUser = json_object_get(pRoot, "user");
	if(pUser && pUser->type == json_object)
	{
		str_copy(m_aUserId, JsonString(pUser, "user_id"), sizeof(m_aUserId));
		str_copy(m_aNickname, JsonString(pUser, "nickname"), sizeof(m_aNickname));
		str_copy(m_aClanId, JsonString(pUser, "clan_id"), sizeof(m_aClanId));
		m_Role = ParseRole(JsonString(pUser, "role", nullptr));
		ParseRecentClans(pUser);
	}
	m_LoggedIn = m_aAccessToken[0] != '\0';
	SaveSession();
	m_View = InClan() ? EView::CLAN : EView::LANDING;
	m_LastPresenceTick = 0;
	m_LastPresenceClientState = -1;
	m_NeedInitialSync = false;
	if(InClan())
		RefreshClan();
	else
		RefreshCatalog();
	// recent_clans already parsed from user; optional bg refresh keeps list in sync
	RefreshRecentClans();
}

void CClans::ParseCatalog(const json_value *pRoot)
{
	m_vCatalog.clear();
	const json_value *pClans = json_object_get(pRoot, "clans");
	if(!pClans || pClans->type != json_array)
		return;
	for(unsigned i = 0; i < pClans->u.array.length; i++)
	{
		const json_value *pE = pClans->u.array.values[i];
		if(!pE || pE->type != json_object)
			continue;
		SCatalogEntry Entry;
		str_copy(Entry.m_aClanId, JsonString(pE, "clan_id"), sizeof(Entry.m_aClanId));
		str_copy(Entry.m_aName, JsonString(pE, "name"), sizeof(Entry.m_aName));
		str_copy(Entry.m_aTag, JsonString(pE, "tag"), sizeof(Entry.m_aTag));
		str_copy(Entry.m_aDescription, JsonString(pE, "description"), sizeof(Entry.m_aDescription));
		Entry.m_IconId = JsonInt(pE, "icon_id");
		Entry.m_Color = (unsigned)JsonInt(pE, "color", 0xFFFFFF);
		Entry.m_Country = JsonInt(pE, "country", -1);
		str_copy(Entry.m_aJoinPolicy, JsonString(pE, "join_policy", "open"), sizeof(Entry.m_aJoinPolicy));
		Entry.m_MaxMembers = JsonInt(pE, "max_members", 50);
		Entry.m_MemberCount = JsonInt(pE, "member_count");
		Entry.m_OnlineCount = JsonInt(pE, "online_count");
		m_vCatalog.push_back(Entry);
	}
}

void CClans::ParseMemberSkin(const json_value *pMember, SSkin *pSkin)
{
	const json_value *pSkinObj = json_object_get(pMember, "skin");
	if(!pSkinObj || pSkinObj->type != json_object)
		return;
	str_copy(pSkin->m_aName, JsonString(pSkinObj, "name", "default"), sizeof(pSkin->m_aName));
	pSkin->m_ColorBody = JsonInt(pSkinObj, "color_body");
	pSkin->m_ColorFeet = JsonInt(pSkinObj, "color_feet");
	pSkin->m_UseCustomColor = JsonBool(pSkinObj, "use_custom_color");
}

void CClans::ParseClanSnapshot(const json_value *pRoot)
{
	SClanSnapshot Parsed{};
	str_copy(Parsed.m_aClanId, JsonString(pRoot, "clan_id"), sizeof(Parsed.m_aClanId));
	str_copy(Parsed.m_aName, JsonString(pRoot, "name"), sizeof(Parsed.m_aName));
	str_copy(Parsed.m_aTag, JsonString(pRoot, "tag"), sizeof(Parsed.m_aTag));
	str_copy(Parsed.m_aDescription, JsonString(pRoot, "description"), sizeof(Parsed.m_aDescription));
	Parsed.m_IconId = JsonInt(pRoot, "icon_id");
	Parsed.m_Color = (unsigned)JsonInt(pRoot, "color", 0xFFFFFF);
	Parsed.m_Country = JsonInt(pRoot, "country", -1);
	str_copy(Parsed.m_aJoinPolicy, JsonString(pRoot, "join_policy", "open"), sizeof(Parsed.m_aJoinPolicy));
	Parsed.m_MaxMembers = JsonInt(pRoot, "max_members", 50);
	const json_value *pInvite = json_object_get(pRoot, "invite_code");
	if(pInvite && pInvite->type == json_string)
	{
		str_copy(Parsed.m_aInviteCode, pInvite->u.string.ptr, sizeof(Parsed.m_aInviteCode));
		Parsed.m_HasInviteCode = true;
	}

	bool FoundSelf = false;
	ERole SelfRole = ERole::NONE;
	const json_value *pMembers = json_object_get(pRoot, "members");
	if(pMembers && pMembers->type == json_array)
	{
		for(unsigned i = 0; i < pMembers->u.array.length; i++)
		{
			const json_value *pM = pMembers->u.array.values[i];
			if(!pM || pM->type != json_object)
				continue;
			SMember Member;
			str_copy(Member.m_aUserId, JsonString(pM, "user_id"), sizeof(Member.m_aUserId));
			str_copy(Member.m_aNickname, JsonString(pM, "nickname"), sizeof(Member.m_aNickname));
			Member.m_Role = ParseRole(JsonString(pM, "role"));
			ParseMemberSkin(pM, &Member.m_Skin);
			const json_value *pPres = json_object_get(pM, "presence");
			if(pPres && pPres->type == json_object)
			{
				Member.m_Online = JsonBool(pPres, "online");
				str_copy(Member.m_aServer, JsonString(pPres, "server"), sizeof(Member.m_aServer));
				str_copy(Member.m_aMap, JsonString(pPres, "map"), sizeof(Member.m_aMap));
				Member.m_Players = JsonInt(pPres, "players");
				Member.m_MaxPlayers = JsonInt(pPres, "max_players");
			}
			if(m_aUserId[0] && !str_comp(Member.m_aUserId, m_aUserId))
			{
				SelfRole = Member.m_Role;
				FoundSelf = true;
			}
			Parsed.m_vMembers.push_back(Member);
		}
	}

	// Only wipe membership when this snapshot is for OUR clan and we are missing from it.
	// A foreign clan payload must never kick us out (that used to cause "not found" after preview).
	if(m_aUserId[0] && !FoundSelf)
	{
		if(m_aClanId[0] && !str_comp(m_aClanId, Parsed.m_aClanId))
		{
			LeaveClanLocal();
			RefreshCatalog();
		}
		return;
	}

	m_Clan = std::move(Parsed);
	m_Role = SelfRole;
	str_copy(m_aClanId, m_Clan.m_aClanId, sizeof(m_aClanId));
	str_copy(m_aClanTag, m_Clan.m_aTag, sizeof(m_aClanTag));
	ApplyClanTagLock(m_Clan.m_aTag);

	m_Clan.m_UnreadAnnouncements = JsonInt(pRoot, "unread_announcements");
	const json_value *pUnl = json_object_get(pRoot, "unleashed");
	if(pUnl && pUnl->type == json_array)
	{
		for(unsigned i = 0; i < pUnl->u.array.length; i++)
		{
			const json_value *pU = pUnl->u.array.values[i];
			if(!pU || pU->type != json_object)
				continue;
			SUnleashedPlayer U;
			str_copy(U.m_aName, JsonString(pU, "name"), sizeof(U.m_aName));
			str_copy(U.m_aMap, JsonString(pU, "map"), sizeof(U.m_aMap));
			str_copy(U.m_aServer, JsonString(pU, "server"), sizeof(U.m_aServer));
			U.m_Players = JsonInt(pU, "players");
			U.m_MaxPlayers = JsonInt(pU, "max_players");
			if(U.m_aName[0])
				m_Clan.m_vUnleashed.push_back(U);
		}
	}

	SaveSession();
	// Only jump into the own-clan view when this snapshot is actually our membership.
	// Never hijack an open catalog preview of another clan.
	if(FoundSelf && (m_View == EView::LANDING || m_View == EView::SETUP || m_View == EView::AUTH || m_View == EView::PREVIEW))
		m_View = EView::CLAN;
}

void CClans::ParsePreviewSnapshot(const json_value *pRoot)
{
	m_Preview = SClanSnapshot{};
	str_copy(m_Preview.m_aClanId, JsonString(pRoot, "clan_id"), sizeof(m_Preview.m_aClanId));
	str_copy(m_aPreviewClanId, m_Preview.m_aClanId, sizeof(m_aPreviewClanId));
	str_copy(m_Preview.m_aName, JsonString(pRoot, "name"), sizeof(m_Preview.m_aName));
	str_copy(m_Preview.m_aTag, JsonString(pRoot, "tag"), sizeof(m_Preview.m_aTag));
	str_copy(m_Preview.m_aDescription, JsonString(pRoot, "description"), sizeof(m_Preview.m_aDescription));
	m_Preview.m_IconId = JsonInt(pRoot, "icon_id");
	m_Preview.m_Color = (unsigned)JsonInt(pRoot, "color", 0xFFFFFF);
	m_Preview.m_Country = JsonInt(pRoot, "country", -1);
	str_copy(m_Preview.m_aJoinPolicy, JsonString(pRoot, "join_policy", "open"), sizeof(m_Preview.m_aJoinPolicy));
	m_Preview.m_MaxMembers = JsonInt(pRoot, "max_members", 50);

	const json_value *pMembers = json_object_get(pRoot, "members");
	if(pMembers && pMembers->type == json_array)
	{
		for(unsigned i = 0; i < pMembers->u.array.length; i++)
		{
			const json_value *pM = pMembers->u.array.values[i];
			if(!pM || pM->type != json_object)
				continue;
			SMember Member;
			str_copy(Member.m_aUserId, JsonString(pM, "user_id"), sizeof(Member.m_aUserId));
			str_copy(Member.m_aNickname, JsonString(pM, "nickname"), sizeof(Member.m_aNickname));
			Member.m_Role = ParseRole(JsonString(pM, "role"));
			ParseMemberSkin(pM, &Member.m_Skin);
			const json_value *pPres = json_object_get(pM, "presence");
			if(pPres && pPres->type == json_object)
			{
				Member.m_Online = JsonBool(pPres, "online");
				str_copy(Member.m_aServer, JsonString(pPres, "server"), sizeof(Member.m_aServer));
				str_copy(Member.m_aMap, JsonString(pPres, "map"), sizeof(Member.m_aMap));
				Member.m_Players = JsonInt(pPres, "players");
				Member.m_MaxPlayers = JsonInt(pPres, "max_players");
			}
			m_Preview.m_vMembers.push_back(Member);
		}
	}

	const json_value *pUnl = json_object_get(pRoot, "unleashed");
	if(pUnl && pUnl->type == json_array)
	{
		for(unsigned i = 0; i < pUnl->u.array.length; i++)
		{
			const json_value *pU = pUnl->u.array.values[i];
			if(!pU || pU->type != json_object)
				continue;
			SUnleashedPlayer U;
			str_copy(U.m_aName, JsonString(pU, "name"), sizeof(U.m_aName));
			str_copy(U.m_aMap, JsonString(pU, "map"), sizeof(U.m_aMap));
			str_copy(U.m_aServer, JsonString(pU, "server"), sizeof(U.m_aServer));
			U.m_Players = JsonInt(pU, "players");
			U.m_MaxPlayers = JsonInt(pU, "max_players");
			if(U.m_aName[0])
				m_Preview.m_vUnleashed.push_back(U);
		}
	}

	m_View = EView::PREVIEW;
}

void CClans::ParseApplications(const json_value *pRoot)
{
	m_vApplications.clear();
	const json_value *pApps = json_object_get(pRoot, "applications");
	if(!pApps || pApps->type != json_array)
		return;
	for(unsigned i = 0; i < pApps->u.array.length; i++)
	{
		const json_value *pA = pApps->u.array.values[i];
		if(!pA || pA->type != json_object)
			continue;
		SApplication App;
		str_copy(App.m_aId, JsonString(pA, "id"), sizeof(App.m_aId));
		str_copy(App.m_aUserId, JsonString(pA, "user_id"), sizeof(App.m_aUserId));
		str_copy(App.m_aNickname, JsonString(pA, "nickname"), sizeof(App.m_aNickname));
		str_copy(App.m_aText, JsonString(pA, "text"), sizeof(App.m_aText));
		m_vApplications.push_back(App);
	}
}

void CClans::ParseAnnouncements(const json_value *pRoot)
{
	m_vAnnouncements.clear();
	const json_value *pAnns = json_object_get(pRoot, "announcements");
	if(!pAnns || pAnns->type != json_array)
		return;
	for(unsigned i = 0; i < pAnns->u.array.length; i++)
	{
		const json_value *pA = pAnns->u.array.values[i];
		if(!pA || pA->type != json_object)
			continue;
		SAnnouncement Ann;
		str_copy(Ann.m_aId, JsonString(pA, "id"), sizeof(Ann.m_aId));
		str_copy(Ann.m_aAuthorId, JsonString(pA, "author_id"), sizeof(Ann.m_aAuthorId));
		str_copy(Ann.m_aAuthorNick, JsonString(pA, "author_nick"), sizeof(Ann.m_aAuthorNick));
		Ann.m_AuthorRole = ParseRole(JsonString(pA, "author_role", "member"));
		const json_value *pSkinObj = json_object_get(pA, "author_skin");
		if(pSkinObj && pSkinObj->type == json_object)
		{
			str_copy(Ann.m_AuthorSkin.m_aName, JsonString(pSkinObj, "name", "default"), sizeof(Ann.m_AuthorSkin.m_aName));
			Ann.m_AuthorSkin.m_ColorBody = JsonInt(pSkinObj, "color_body");
			Ann.m_AuthorSkin.m_ColorFeet = JsonInt(pSkinObj, "color_feet");
			Ann.m_AuthorSkin.m_UseCustomColor = JsonBool(pSkinObj, "use_custom_color");
		}
		str_copy(Ann.m_aText, JsonString(pA, "text"), sizeof(Ann.m_aText));
		str_copy(Ann.m_aCreatedAt, JsonString(pA, "created_at"), sizeof(Ann.m_aCreatedAt));
		m_vAnnouncements.push_back(Ann);
	}
}

void CClans::ParseNotifications(const json_value *pRoot)
{
	m_vNotifications.clear();
	const json_value *pList = json_object_get(pRoot, "notifications");
	if(!pList || pList->type != json_array)
		return;
	for(unsigned i = 0; i < pList->u.array.length; i++)
	{
		const json_value *pN = pList->u.array.values[i];
		if(!pN || pN->type != json_object)
			continue;
		SNotification N;
		str_copy(N.m_aId, JsonString(pN, "id"), sizeof(N.m_aId));
		str_copy(N.m_aType, JsonString(pN, "type"), sizeof(N.m_aType));
		str_copy(N.m_aClanId, JsonString(pN, "clan_id"), sizeof(N.m_aClanId));
		str_copy(N.m_aClanName, JsonString(pN, "clan_name"), sizeof(N.m_aClanName));
		N.m_Read = JsonBool(pN, "read");
		m_vNotifications.push_back(N);
	}
	PushToastFromNotifications();
}

void CClans::ParseRecentClans(const json_value *pRoot)
{
	m_vRecentClans.clear();
	const json_value *pList = json_object_get(pRoot, "recent_clans");
	if(!pList || pList->type != json_array)
		return;
	for(unsigned i = 0; i < pList->u.array.length; i++)
	{
		const json_value *pR = pList->u.array.values[i];
		if(!pR || pR->type != json_object)
			continue;
		SRecentClan R;
		str_copy(R.m_aClanId, JsonString(pR, "clan_id"), sizeof(R.m_aClanId));
		str_copy(R.m_aName, JsonString(pR, "name"), sizeof(R.m_aName));
		str_copy(R.m_aTag, JsonString(pR, "tag"), sizeof(R.m_aTag));
		R.m_WasPresident = JsonBool(pR, "was_president");
		m_vRecentClans.push_back(R);
	}
}

void CClans::PushToastFromNotifications()
{
	for(const auto &N : m_vNotifications)
	{
		if(N.m_Read)
			continue;
		SToast Toast;
		if(!str_comp(N.m_aType, "application_accepted"))
			str_format(Toast.m_aText, sizeof(Toast.m_aText), Localize("You were accepted into clan %s"), N.m_aClanName);
		else if(!str_comp(N.m_aType, "application_rejected"))
			str_format(Toast.m_aText, sizeof(Toast.m_aText), Localize("Your application to clan %s was rejected"), N.m_aClanName);
		else
			continue;
		Toast.m_TimeLeft = 6.0f;
		m_Toasts.push_back(Toast);
		MarkNotificationRead(N.m_aId);
	}
}

void CClans::HandlePending()
{
	if(!m_pPending || !m_pPending->Done())
		return;

	const int Kind = m_PendingKind;
	auto pReq = m_pPending;
	m_pPending = nullptr;
	m_PendingKind = REQ_NONE;
	m_aStatus[0] = '\0';

	if(pReq->State() != EHttpState::DONE)
	{
		SetError(nullptr, Localize("Network error"));
		return;
	}

	json_value *pRoot = pReq->ResultJson();
	const int Code = pReq->StatusCode();

	if(Code >= 400)
	{
		// Background polls and failed preview refreshes with valid cached data must not spam the UI error banner.
		const bool HasCachedPreview = Kind == REQ_PREVIEW && m_Preview.m_aClanId[0] && !str_comp(m_Preview.m_aClanId, m_aPreviewClanId);
		const bool Silent = Kind == REQ_PRESENCE || Kind == REQ_NOTIFS || Kind == REQ_NOTIF_READ || HasCachedPreview;
		const char *pErr = nullptr;
		const char *pMsg = nullptr;
		if(pRoot && pRoot->type == json_object)
		{
			pErr = JsonString(pRoot, "error", nullptr);
			pMsg = JsonString(pRoot, "message", nullptr);
		}

		// Clan wiped on server / disbanded — drop local stuck membership.
		const bool ClanGone = Code == 404 || (pErr && !str_comp(pErr, "clan.not_found"));
		if(ClanGone && (Kind == REQ_CLAN || Kind == REQ_LEAVE || Kind == REQ_DISBAND || Kind == REQ_APPS || Kind == REQ_ANNS || Kind == REQ_ROTATE || Kind == REQ_PROMOTE || Kind == REQ_DEMOTE || Kind == REQ_KICK || Kind == REQ_APPROVE || Kind == REQ_REJECT || Kind == REQ_POST_ANN || Kind == REQ_UPDATE))
		{
			if(pRoot)
				json_value_free(pRoot);
			LeaveClanLocal();
			RefreshCatalog();
			SetStatus(Localize("Clan no longer exists"));
			return;
		}

		if(!Silent)
		{
			if(pMsg && pMsg[0])
				SetError(pErr, pMsg);
			else if(Code == 404)
				SetError(pErr, Localize("Not found"));
			else
				SetError(pErr, Localize("Request failed"));
		}
		if(pRoot)
			json_value_free(pRoot);
		if(Code == 401 && Kind != REQ_LOGIN && Kind != REQ_REGISTER && Kind != REQ_REFRESH)
		{
			if(m_aRefreshToken[0])
				TryRefreshSession();
			else
				ClearSession();
		}
		else if(Code == 401 && Kind == REQ_REFRESH)
			ClearSession();
		return;
	}

	if(Kind == REQ_LOGOUT || Kind == REQ_LEAVE || Kind == REQ_DISBAND)
	{
		if(Kind == REQ_LEAVE || Kind == REQ_DISBAND)
		{
			m_aClanId[0] = '\0';
			m_aClanTag[0] = '\0';
			m_Role = ERole::NONE;
			m_Clan = SClanSnapshot{};
			ClearClanTagLock();
			m_View = EView::LANDING;
			ArmMembershipCooldown();
			SaveSession();
			RefreshCatalog();
			RefreshRecentClans();
		}
		if(pRoot)
			json_value_free(pRoot);
		return;
	}

	if(Kind == REQ_PRESENCE || Kind == REQ_PROMOTE || Kind == REQ_DEMOTE || Kind == REQ_KICK || Kind == REQ_APPROVE || Kind == REQ_REJECT || Kind == REQ_NOTIF_READ)
	{
		if(pRoot)
			json_value_free(pRoot);
		if(Kind == REQ_APPROVE || Kind == REQ_REJECT)
			RefreshApplications();
		if(Kind == REQ_PROMOTE || Kind == REQ_DEMOTE || Kind == REQ_KICK)
			RefreshClan();
		return;
	}

	if(!pRoot || pRoot->type != json_object)
	{
		if(pRoot)
			json_value_free(pRoot);
		SetError(nullptr, Localize("Invalid response from server"));
		if(Kind == REQ_ROTATE)
			RefreshClan();
		return;
	}

	switch(Kind)
	{
	case REQ_REGISTER:
	case REQ_LOGIN:
	case REQ_REFRESH:
		ParseAuthResponse(pRoot);
		break;
	case REQ_ME:
	{
		const bool WasInClan = m_aClanId[0] != '\0';
		str_copy(m_aUserId, JsonString(pRoot, "user_id"), sizeof(m_aUserId));
		str_copy(m_aNickname, JsonString(pRoot, "nickname"), sizeof(m_aNickname));
		const json_value *pClanId = json_object_get(pRoot, "clan_id");
		if(pClanId && pClanId->type == json_string)
			str_copy(m_aClanId, pClanId->u.string.ptr, sizeof(m_aClanId));
		else
			m_aClanId[0] = '\0';
		m_Role = ParseRole(JsonString(pRoot, "role", nullptr));
		ParseRecentClans(pRoot);
		const json_value *pClan = json_object_get(pRoot, "clan");
		if(pClan && pClan->type == json_object)
			str_copy(m_aClanTag, JsonString(pClan, "tag"), sizeof(m_aClanTag));
		else if(!InClan())
			m_aClanTag[0] = '\0';
		if(InClan())
		{
			ApplyClanTagLock(m_aClanTag);
			if(!WasInClan || m_View == EView::LANDING || m_View == EView::SETUP || m_View == EView::AUTH || !m_Clan.m_aClanId[0])
			{
				m_View = EView::CLAN;
				RefreshClan();
			}
		}
		else
		{
			ClearClanTagLock();
			m_Clan = SClanSnapshot{};
			m_Role = ERole::NONE;
			if(WasInClan || m_View == EView::CLAN || m_View == EView::APPLICATIONS || m_View == EView::ANNOUNCEMENTS || m_View == EView::SETTINGS)
				m_View = EView::LANDING;
			if((m_View == EView::LANDING || m_View == EView::BROWSE) && m_vCatalog.empty())
				RefreshCatalog();
		}
		SaveSession();
		break;
	}
	case REQ_CATALOG:
		ParseCatalog(pRoot);
		break;
	case REQ_CREATE:
	case REQ_UPDATE:
	case REQ_JOIN:
	case REQ_JOIN_CODE:
	case REQ_REJOIN:
	case REQ_CLAN:
		ParseClanSnapshot(pRoot);
		ClearPreview();
		if(Kind == REQ_CREATE || Kind == REQ_JOIN || Kind == REQ_JOIN_CODE || Kind == REQ_REJOIN)
			ArmMembershipCooldown();
		if(Kind == REQ_UPDATE)
			SetStatus(Localize("Settings saved"));
		break;
	case REQ_PREVIEW:
		ParsePreviewSnapshot(pRoot);
		break;
	case REQ_ROTATE:
		str_copy(m_Clan.m_aInviteCode, JsonString(pRoot, "invite_code"), sizeof(m_Clan.m_aInviteCode));
		m_Clan.m_HasInviteCode = m_Clan.m_aInviteCode[0] != '\0';
		break;
	case REQ_APPLY:
		SetStatus(Localize("Application sent"));
		break;
	case REQ_APPS:
		ParseApplications(pRoot);
		break;
	case REQ_ANNS:
	case REQ_POST_ANN:
		if(Kind == REQ_POST_ANN)
		{
			ArmAnnounceCooldown();
			RefreshAnnouncements();
		}
		else
			ParseAnnouncements(pRoot);
		break;
	case REQ_NOTIFS:
		ParseNotifications(pRoot);
		break;
	case REQ_RECENT:
	case REQ_RECENT_CLEAR:
		if(!m_aRecentPendingUserId[0] || !str_comp(m_aRecentPendingUserId, m_aUserId))
			ParseRecentClans(pRoot);
		if(Kind == REQ_RECENT_CLEAR)
			SetStatus(Localize("Recent clans cleared"));
		break;
	default:
		break;
	}

	json_value_free(pRoot);
	if(Kind == REQ_CLAN || Kind == REQ_CATALOG || Kind == REQ_APPS || Kind == REQ_ANNS || Kind == REQ_ME || Kind == REQ_PREVIEW)
		m_aStatus[0] = '\0';
}

void CClans::HandleBackground()
{
	if(!m_pBgPending || !m_pBgPending->Done())
		return;

	const int Kind = m_BgKind;
	auto pReq = m_pBgPending;
	m_pBgPending = nullptr;
	m_BgKind = REQ_NONE;

	if(pReq->State() != EHttpState::DONE)
		return;

	const int Code = pReq->StatusCode();
	json_value *pRoot = pReq->ResultJson();

	if(Kind == REQ_PRESENCE || Kind == REQ_NOTIF_READ || Kind == REQ_ANN_READ)
	{
		if(pRoot)
			json_value_free(pRoot);
		return;
	}

	if(Kind == REQ_NOTIFS)
	{
		if(Code >= 200 && Code < 300 && pRoot && pRoot->type == json_object)
			ParseNotifications(pRoot);
		if(pRoot)
			json_value_free(pRoot);
		return;
	}

	if(Kind == REQ_RECENT)
	{
		if(Code >= 200 && Code < 300 && pRoot && pRoot->type == json_object)
		{
			if(!m_aRecentPendingUserId[0] || !str_comp(m_aRecentPendingUserId, m_aUserId))
				ParseRecentClans(pRoot);
		}
		if(pRoot)
			json_value_free(pRoot);
		return;
	}

	if(pRoot)
		json_value_free(pRoot);
}
