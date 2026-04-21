/* Copyright 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_IRC_IRC_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_IRC_IRC_H

#include <game/client/component.h>
#include <game/client/components/media_decoder.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class CHttpRequest;

class CIrcChat : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;
	void OnRender() override;
	bool OnInput(const IInput::CEvent &Event) override;

	void RenderPage(CUIRect MainView);
	bool IsOpenInMenus() const;

private:
	enum class EConnectionState
	{
		DISCONNECTED,
		CONNECTING,
		CONNECTED,
		AUTHENTICATED,
		ERROR,
	};

	enum class EView
	{
		CHANNEL,
		FRIENDS,
		SEARCH,
		SETTINGS,
	};

	struct SUser
	{
		int64_t m_Id = 0;
		std::string m_Login;
		std::string m_Display;
		std::string m_Status;
		std::string m_Description;
		std::string m_BannerColor = "#5865f2";
		std::string m_Skin;
		bool m_Online = false;
	};

	struct SMessage
	{
		int64_t m_Id = 0;
		std::string m_RoomType;
		std::string m_RoomKey;
		int64_t m_SenderId = 0;
		std::string m_SenderName;
		std::string m_Text;
		int64_t m_CreatedAt = 0;
	};

	enum class EMediaPreviewState
	{
		NONE,
		LOADING,
		READY,
		FAILED,
	};

	struct SMediaPreview
	{
		std::string m_Url;
		std::string m_ResolvedUrl;
		EMediaPreviewState m_State = EMediaPreviewState::NONE;
		std::shared_ptr<CHttpRequest> m_pRequest;
		std::vector<SMediaFrame> m_vFrames;
		bool m_Animated = false;
		int m_Width = 0;
		int m_Height = 0;
		int64_t m_AnimationStart = 0;
	};

	struct SNetEvent
	{
		std::string m_Line;
	};

	EConnectionState m_State = EConnectionState::DISCONNECTED;
	EView m_View = EView::CHANNEL;
	bool m_RegisterMode = false;
	bool m_StatusMenuOpen = false;
	bool m_LoggedIn = false;
	bool m_AutoLoginTried = false;
	int64_t m_LocalUserId = 0;
	std::string m_CurrentChannel = "international";
	std::string m_CurrentDmKey;
	int64_t m_SelectedDmUserId = 0;
	std::string m_StatusText = "Disconnected";
	std::string m_ErrorText;
	CUIRect m_LastChatListRect;
	int m_ChatScrollOffset = 0;
	bool m_LoadingOlderHistory = false;
	bool m_HistoryExhausted = false;

	std::unordered_map<int64_t, SUser> m_Users;
	std::vector<int64_t> m_PresenceOrder;
	std::vector<int64_t> m_Friends;
	std::vector<int64_t> m_PendingIn;
	std::vector<int64_t> m_PendingOut;
	std::vector<int64_t> m_SearchResults;
	std::vector<SMessage> m_Messages;
	std::unordered_map<int64_t, SMediaPreview> m_MediaPreviews;

	CLineInputBuffered<64> m_LoginInput;
	CLineInputBuffered<128> m_PasswordInput;
	CLineInputBuffered<128> m_PasswordRepeatInput;
	CLineInputBuffered<512> m_MessageInput;
	CLineInputBuffered<64> m_SearchInput;
	CLineInputBuffered<256> m_DescriptionInput;
	CLineInputBuffered<16> m_BannerColorInput;
	CLineInputBuffered<64> m_NewPasswordInput;

	CButtonContainer m_ConnectButton;
	CButtonContainer m_AuthModeButton;
	CButtonContainer m_AuthSubmitButton;
	CButtonContainer m_SendButton;
	CButtonContainer m_ProfileButton;
	CButtonContainer m_SettingsButton;
	CButtonContainer m_SupportButton;
	CButtonContainer m_SearchButton;
	CButtonContainer m_LogoutButton;
	CButtonContainer m_SaveProfileButton;
	CButtonContainer m_ChangeLoginButton;
	CButtonContainer m_ChangePasswordButton;
	CButtonContainer m_ResetTlsButton;
	CButtonContainer m_StatusButtons[4];
	CButtonContainer m_ChannelButtons[5];
	CButtonContainer m_FriendsButton;
	CButtonContainer m_LinkButtons[24];
	CButtonContainer m_SettingsStatusButtons[4];
	CButtonContainer m_SettingsPanelSupportButton;
	CButtonContainer m_SettingsPanelResetTlsButton;
	CButtonContainer m_SettingsPanelLogoutButton;
	CButtonContainer m_LoadHistoryButton;

	std::atomic<bool> m_StopThread{false};
	std::thread m_NetworkThread;
	std::mutex m_NetMutex;
	std::condition_variable m_NetCv;
	std::vector<std::string> m_vOutgoing;
	std::vector<SNetEvent> m_vEvents;

	void StartConnection();
	void StopConnection();
	void NetworkMain();
	void QueueJson(const std::string &Json);
	void QueueEvent(const std::string &Line);
	void DrainEvents();
	void HandleServerLine(const char *pLine);

	void SendLogin();
	void SendRegister();
	void SendResume();
	void SendJoinChannel(const char *pChannel);
	void SendMessage();
	void SendStatus(const char *pStatus);
	void SendSearch();
	void SendFriendRequest(int64_t UserId);
	void SendFriendAccept(int64_t UserId);
	void SendFriendRemove(int64_t UserId);
	void SendDmOpen(int64_t UserId);
	void SendProfileUpdate();
	void SendLoginChange();
	void SendPasswordChange();
	void SendFriendList();
	void SendHistoryRequest(bool OlderPage);

	void RenderAuth(CUIRect View);
	void RenderShell(CUIRect View);
	void RenderLeftRail(CUIRect View);
	void RenderChat(CUIRect View);
	void RenderRightPanel(CUIRect View);
	void RenderSettings(CUIRect View);
	void RenderSettingsPanel(CUIRect View);
	void RenderUserRow(CUIRect Row, const SUser &User, bool FriendActions);
	void RenderAvatar(const CUIRect &Rect, const SUser *pUser);
	int DoIrcButton(CButtonContainer *pButton, const char *pLabel, CUIRect Rect, bool Active, const char *pIcon = nullptr, float FontSize = 12.0f);
	void RenderStatusIcon(CUIRect Rect, const char *pStatus, float Size = 10.0f);
	void RenderWrappedText(CUIRect Rect, const char *pText, float FontSize, ColorRGBA Color);
	float WrappedTextHeight(const char *pText, float FontSize, float Width) const;
	SMediaPreview *EnsureMediaPreview(int64_t MessageId, const std::string &Url);
	void StartMediaPreview(SMediaPreview &Preview, const std::string &Url);
	void UpdateMediaPreviews();
	void ResetMediaPreviews();
	bool DecodeMediaPreview(SMediaPreview &Preview, const unsigned char *pData, size_t DataSize);
	float MediaPreviewHeight(const SMediaPreview *pPreview, float MaxWidth) const;
	void RenderMediaPreview(CUIRect Rect, SMediaPreview *pPreview, const std::string &Url, int ButtonIndex);

	const char *ConnectionStatusText() const;
	const SUser *FindUser(int64_t UserId) const;
	SUser &EnsureUser(int64_t UserId);
	std::vector<SMessage> CurrentRoomMessages() const;
	int64_t CurrentRoomOldestMessageId() const;
	std::string CurrentRoomType() const;
	std::string CurrentRoomKey() const;
	std::string BuildLocalSkinJson() const;
	static std::string JsonEscape(const char *pString);
	static std::string Lower(std::string Value);
	static std::string DisplayChannelName(const std::string &Channel);
	static std::string ExtractFirstUrl(const std::string &Text);
	static std::string ExtractMediaUrlFromHtml(const unsigned char *pData, size_t DataSize);
};

#endif
