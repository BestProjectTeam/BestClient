/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_CLANS_CLANS_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_CLANS_CLANS_H

#include <engine/shared/http.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>
#include <game/client/lineinput.h>

#include <deque>
#include <memory>
#include <string>
#include <vector>

class CClans : public CComponent
{
public:
	enum class EView
	{
		AUTH,
		LANDING,
		SETUP,
		CLAN,
		APPLICATIONS,
		ANNOUNCEMENTS,
		SETTINGS,
		RECENT,
		BROWSE, // catalog while already in a clan
		PREVIEW, // catalog clan detail (not a member yet)
	};

	enum class ERole
	{
		NONE,
		MEMBER,
		VETERAN,
		VICE_PRESIDENT,
		PRESIDENT,
	};

	struct SSkin
	{
		char m_aName[24] = "default";
		int m_ColorBody = 0;
		int m_ColorFeet = 0;
		bool m_UseCustomColor = false;
	};

	struct SCatalogEntry
	{
		char m_aClanId[64] = "";
		char m_aName[64] = "";
		char m_aTag[16] = "";
		char m_aDescription[256] = "";
		int m_IconId = 0;
		unsigned m_Color = 0xFFFFFF;
		int m_Country = -1;
		char m_aJoinPolicy[16] = "open";
		int m_MaxMembers = 50;
		int m_MemberCount = 0;
		int m_OnlineCount = 0;
	};

	struct SMember
	{
		char m_aUserId[64] = "";
		char m_aNickname[32] = "";
		ERole m_Role = ERole::MEMBER;
		SSkin m_Skin;
		bool m_Online = false;
		char m_aServer[64] = "";
		char m_aMap[64] = "";
		int m_Players = 0;
		int m_MaxPlayers = 0;
	};

	struct SApplication
	{
		char m_aId[64] = "";
		char m_aUserId[64] = "";
		char m_aNickname[32] = "";
		char m_aText[288] = "";
	};

	struct SAnnouncement
	{
		char m_aId[64] = "";
		char m_aAuthorId[64] = "";
		char m_aAuthorNick[32] = "";
		ERole m_AuthorRole = ERole::MEMBER;
		SSkin m_AuthorSkin;
		char m_aText[512] = "";
		char m_aCreatedAt[40] = "";
	};

	struct SRecentClan
	{
		char m_aClanId[64] = "";
		char m_aName[64] = "";
		char m_aTag[16] = "";
		bool m_WasPresident = false;
	};

	struct SNotification
	{
		char m_aId[64] = "";
		char m_aType[40] = "";
		char m_aClanId[64] = "";
		char m_aClanName[64] = "";
		bool m_Read = false;
	};

	struct SToast
	{
		char m_aText[160] = "";
		float m_TimeLeft = 0.0f;
	};

	struct SUnleashedPlayer
	{
		char m_aName[MAX_NAME_LENGTH] = "";
		char m_aMap[64] = "";
		char m_aServer[64] = "";
		int m_Players = 0;
		int m_MaxPlayers = 0;
	};

	struct SClanSnapshot
	{
		char m_aClanId[64] = "";
		char m_aName[64] = "";
		char m_aTag[16] = "";
		char m_aDescription[256] = "";
		int m_IconId = 0;
		unsigned m_Color = 0xFFFFFF;
		int m_Country = -1;
		char m_aJoinPolicy[16] = "open";
		int m_MaxMembers = 50;
		char m_aInviteCode[32] = "";
		bool m_HasInviteCode = false;
		std::vector<SMember> m_vMembers;
		std::vector<SUnleashedPlayer> m_vUnleashed;
		int m_UnreadAnnouncements = 0;
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnReset() override;
	void OnUpdate() override;
	void OnShutdown() override;

	bool IsLoggedIn() const { return m_LoggedIn; }
	const char *Nickname() const { return m_aNickname; }
	const char *UserId() const { return m_aUserId; }
	const char *ClanId() const { return m_aClanId; }
	bool InClan() const { return m_aClanId[0] != '\0'; }
	ERole Role() const { return m_Role; }
	EView View() const { return m_View; }
	void SetView(EView View);
	int UnreadAnnouncements() const { return m_Clan.m_UnreadAnnouncements; }
	void MarkAnnouncementsRead();

	const char *StatusMessage() const { return m_aStatus; }
	const char *ErrorMessage() const { return m_aError; }
	bool ErrorOfferDiscord() const { return m_ErrorOfferDiscord; }
	void ClearError()
	{
		m_aError[0] = '\0';
		m_ErrorOfferDiscord = false;
	}

	bool IsBusy() const { return m_pPending != nullptr; }
	int GetUnreadCount() const;

	const std::vector<SCatalogEntry> &Catalog() const { return m_vCatalog; }
	const SClanSnapshot &Clan() const { return m_Clan; }
	const SClanSnapshot &Preview() const { return m_Preview; }
	bool HasPreview() const { return m_aPreviewClanId[0] != '\0'; }
	const char *PreviewClanId() const { return m_aPreviewClanId; }
	void OpenPreview(const char *pClanId);
	void ClearPreview();
	const std::vector<SApplication> &Applications() const { return m_vApplications; }
	const std::vector<SAnnouncement> &Announcements() const { return m_vAnnouncements; }
	const std::vector<SRecentClan> &RecentClans() const { return m_vRecentClans; }
	const std::deque<SToast> &Toasts() const { return m_Toasts; }

	bool IsPlayerClanLocked() const { return m_PlayerClanLocked; }
	const char *LockedClanTag() const { return m_aLockedTag; }
	void ApplyClanTagLock(const char *pTag);
	void ClearClanTagLock();

	void CollectUnleashed(std::vector<SUnleashedPlayer> *pOut) const;
	const std::vector<SUnleashedPlayer> &Unleashed() const { return m_Clan.m_vUnleashed; }
	bool TryRefreshSession();
	bool IsMembershipCooldownActive() const;
	int MembershipCooldownSecondsLeft() const;
	bool IsAnnounceCooldownActive() const;
	int AnnounceCooldownSecondsLeft() const;

	void Register(const char *pNickname, const char *pPassword);
	void Login(const char *pNickname, const char *pPassword);
	void Logout();
	void RefreshMe();
	void RefreshCatalog();
	void CreateClan(const char *pName, const char *pTag, const char *pDescription, int IconId, unsigned Color, int Country, const char *pJoinPolicy, int MaxMembers);
	void UpdateClanSettings(const char *pName, const char *pDescription, int IconId, unsigned Color, int Country);
	void Join(const char *pClanId);
	void Apply(const char *pClanId, const char *pText);
	void JoinCode(const char *pCode);
	void Leave();
	void Disband();
	void RejoinAsPresident(const char *pClanId);
	void RefreshClan();
	void RotateInvite();
	void Promote(const char *pUserId);
	void Demote(const char *pUserId);
	void Kick(const char *pUserId, int BanHours);
	void RefreshApplications();
	void ApproveApplication(const char *pAppId);
	void RejectApplication(const char *pAppId);
	void RefreshAnnouncements();
	void PostAnnouncement(const char *pText);
	void RefreshNotifications();
	void MarkNotificationRead(const char *pId);
	void RefreshRecentClans();
	void ClearRecentClans();
	void RefreshCurrentView();

	static ERole ParseRole(const char *pRole);

private:
	bool ResolveBaseUrl(char *pOut, int OutSize) const;
	void SetError(const char *pErrorCode, const char *pFallback);
	void SetStatus(const char *pMsg);
	void BeginRequest(std::shared_ptr<CHttpRequest> pReq, int Kind);
	void BeginBackground(std::shared_ptr<CHttpRequest> pReq, int Kind);
	void AuthHeader(CHttpRequest *pReq) const;
	void BuildSkinJson(char *pBuf, int BufSize) const;
	void SaveSession() const;
	void LoadSession();
	void ClearSession();
	void HandlePending();
	void HandleBackground();
	void ParseAuthResponse(const json_value *pRoot);
	void ParseCatalog(const json_value *pRoot);
	void ParseClanSnapshot(const json_value *pRoot);
	void ParsePreviewSnapshot(const json_value *pRoot);
	void ParseMemberSkin(const json_value *pMember, SSkin *pSkin);
	void ParseApplications(const json_value *pRoot);
	void ParseAnnouncements(const json_value *pRoot);
	void ParseNotifications(const json_value *pRoot);
	void ParseRecentClans(const json_value *pRoot);
	void PushToastFromNotifications();
	bool IsClansUiOpen() const;
	void MaybePushPresence(bool UiOpen);
	void MaybePollNotifications();
	void MaybeAutoRefresh();
	void LeaveClanLocal();
	void ArmMembershipCooldown();
	bool GuardMembershipCooldown();
	void ArmAnnounceCooldown();
	bool GuardAnnounceCooldown();

	enum
	{
		REQ_NONE = 0,
		REQ_REGISTER,
		REQ_LOGIN,
		REQ_LOGOUT,
		REQ_REFRESH,
		REQ_ME,
		REQ_CATALOG,
		REQ_CREATE,
		REQ_UPDATE,
		REQ_JOIN,
		REQ_APPLY,
		REQ_JOIN_CODE,
		REQ_LEAVE,
		REQ_DISBAND,
		REQ_REJOIN,
		REQ_CLAN,
		REQ_ROTATE,
		REQ_PROMOTE,
		REQ_DEMOTE,
		REQ_KICK,
		REQ_APPS,
		REQ_APPROVE,
		REQ_REJECT,
		REQ_ANNS,
		REQ_POST_ANN,
		REQ_NOTIFS,
		REQ_NOTIF_READ,
		REQ_RECENT,
		REQ_RECENT_CLEAR,
		REQ_PRESENCE,
		REQ_PREVIEW,
		REQ_ANN_READ,
	};

	bool m_LoggedIn = false;
	EView m_View = EView::AUTH;
	ERole m_Role = ERole::NONE;
	char m_aUserId[64] = "";
	char m_aNickname[32] = "";
	char m_aAccessToken[128] = "";
	char m_aRefreshToken[128] = "";
	char m_aClanId[64] = "";
	char m_aClanTag[16] = "";
	char m_aStatus[128] = "";
	char m_aError[256] = "";
	bool m_ErrorOfferDiscord = false;
	bool m_PlayerClanLocked = false;
	char m_aLockedTag[16] = "";
	char m_aPlayerClanBackup[16] = "";

	std::vector<SCatalogEntry> m_vCatalog;
	SClanSnapshot m_Clan;
	SClanSnapshot m_Preview;
	char m_aPreviewClanId[64] = "";
	std::vector<SApplication> m_vApplications;
	std::vector<SAnnouncement> m_vAnnouncements;
	std::vector<SRecentClan> m_vRecentClans;
	char m_aRecentPendingUserId[64] = "";
	std::vector<SNotification> m_vNotifications;
	std::deque<SToast> m_Toasts;

	std::shared_ptr<CHttpRequest> m_pPending;
	int m_PendingKind = REQ_NONE;
	std::shared_ptr<CHttpRequest> m_pBgPending;
	int m_BgKind = REQ_NONE;
	int64_t m_LastPresenceTick = 0;
	int m_LastPresenceClientState = -1;
	bool m_NeedInitialSync = false;
	int64_t m_LastNotifTick = 0;
	int64_t m_LastClanPollTick = 0;
	int64_t m_LastMePollTick = 0;
	int64_t m_LastCatalogPollTick = 0;
	int64_t m_MembershipCooldownUntil = 0;
	int64_t m_AnnounceCooldownUntil = 0;
};

#endif
