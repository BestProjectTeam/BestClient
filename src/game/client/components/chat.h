/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_CHAT_H
#define GAME_CLIENT_COMPONENTS_CHAT_H

#include <base/str.h>

#include <engine/console.h>
#include <engine/external/regex.h>
#include <engine/shared/config.h>
#include <engine/shared/jobs.h>
#include <engine/shared/protocol.h>
#include <engine/shared/ringbuffer.h>

#include <generated/protocol7.h>

#include <game/client/component.h>
#include <game/client/components/media_decoder.h>
#include <game/client/lineinput.h>
#include <game/client/render.h>
#include <game/client/ui.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class CTranslateResponse
{
public:
	bool m_Error = false;
	char m_Text[1024] = "";
	char m_Language[16] = "";
};

constexpr auto SAVES_FILE = "ddnet-saves.txt";

constexpr int MAX_LINE_LENGTH = 256; // Global constant for chat line length
class CHttpRequest;

// Shared with CGifBubbles so the above-head bubble matches the chat preview's rounded style.
void DrawRoundedMediaPreview(IGraphics *pGraphics, const IGraphics::CTextureHandle &Texture, float X, float Y, float W, float H, float Rounding, float Alpha);

class CChat : public CComponent
{
	static constexpr float CHAT_HEIGHT_FULL = 200.0f;
	static constexpr float CHAT_HEIGHT_MIN = 50.0f;
	static constexpr float CHAT_FONTSIZE_WIDTH_RATIO = 2.5f;

	enum
	{
		MAX_LINES = 64,
		MAX_LINE_LENGTH = ::MAX_LINE_LENGTH
	};

	enum class EMediaState
	{
		NONE = 0,
		QUEUED,
		LOADING,
		DECODING,
		READY,
		FAILED,
	};

	enum class EMediaKind
	{
		UNKNOWN = 0,
		PHOTO,
		ANIMATED,
		VIDEO,
	};

	struct SRenderRect
	{
		float m_X = 0.0f;
		float m_Y = 0.0f;
		float m_W = 0.0f;
		float m_H = 0.0f;
	};

	class CMediaDecodeJob;

	CLineInputBuffered<MAX_LINE_LENGTH> m_Input;
	class CLine
	{
	public:
		CLine();
		void Reset(CChat &This);

		bool m_Initialized;
		int64_t m_Time;
		float m_aYOffset[2];
		int m_ClientId;
		int m_TeamNumber;
		bool m_Team;
		bool m_Whisper;
		int m_NameColor;
		char m_aName[64];
		char m_aText[MAX_LINE_LENGTH];
		bool m_Friend;
		bool m_Highlighted;
		std::optional<ColorRGBA> m_CustomColor;

		STextContainerIndex m_TextContainerIndex;
		int m_QuadContainerIndex;

		std::shared_ptr<CManagedTeeRenderInfo> m_pManagedTeeRenderInfo;

		float m_TextYOffset;
		int m_SelectionStart;
		int m_SelectionEnd;

		int m_TimesRepeated;

		std::shared_ptr<CTranslateResponse> m_pTranslateResponse;

		EMediaState m_MediaState;
		EMediaKind m_MediaKind;
		char m_aMediaUrl[512];
		std::vector<std::string> m_vMediaCandidates;
		int m_MediaCandidateIndex;
		int m_MediaRetryCount;
		std::shared_ptr<CHttpRequest> m_pMediaRequest;
		std::shared_ptr<CMediaDecodeJob> m_pMediaDecodeJob;
		std::optional<SMediaDecodedFrames> m_OptMediaDecodedFrames;
		int m_MediaUploadIndex;
		std::vector<SMediaFrame> m_vMediaFrames;
		std::vector<int> m_vMediaFrameEndMs;
		int m_MediaTotalDurationMs;
		bool m_MediaAnimated;
		bool m_MediaRevealed;
		int m_MediaWidth;
		int m_MediaHeight;
		int m_MediaResolveDepth;
		int64_t m_MediaAnimationStart;
		float m_aTextHeight[2];
		float m_aMediaPreviewWidth[2];
		float m_aMediaPreviewHeight[2];

		SRenderRect m_TranslateRect;
		bool m_TranslateRectValid;
		SRenderRect m_TranslateLanguageRect;
		bool m_TranslateLanguageRectValid;
		SRenderRect m_MediaPreviewRect;
		bool m_MediaPreviewRectValid;
		SRenderRect m_MediaRetryRect;
		bool m_MediaRetryRectValid;

		// Set when the whole line is a single allowed gif-bubble-domain media link; drives the
		// floating gif bubble rendered above the sender's head (see CGifBubbles).
		bool m_ShowAboveHead;
	};

	bool m_PrevScoreBoardShowed;
	bool m_PrevShowChat;
	bool m_PrevModeActive;
	bool m_PrevChatSelectionActive;
	float m_PrevHudLayoutX;
	float m_PrevHudLayoutY;
	int m_PrevHudLayoutScale;
	bool m_PrevHudLayoutEnabled;

	CLine m_aLines[MAX_LINES];
	int m_CurrentLine;
	bool m_MouseIsPress;
	vec2 m_MousePress;
	vec2 m_MouseRelease;
	bool m_HasSelection;
	bool m_WantsSelectionCopy;

	enum
	{
		// client IDs for special messages
		CLIENT_MSG = -2,
		SERVER_MSG = -1,
	};

	enum
	{
		MODE_NONE = 0,
		MODE_ALL,
		MODE_TEAM,
	};

	enum
	{
		CHAT_SERVER = 0,
		CHAT_HIGHLIGHT,
		CHAT_CLIENT,
		CHAT_NUM,
	};

	int m_Mode;
	bool m_Show;
	bool m_CompletionUsed;
	int m_CompletionChosen;
	char m_aCompletionBuffer[MAX_LINE_LENGTH];
	int m_PlaceholderOffset;
	int m_PlaceholderLength;
	static char ms_aDisplayText[MAX_LINE_LENGTH];
	class CRateablePlayer
	{
	public:
		int m_ClientId;
		int m_Score;
	};
	CRateablePlayer m_aPlayerCompletionList[MAX_CLIENTS];
	int m_PlayerCompletionListLength;

	struct CCommand
	{
		char m_aName[IConsole::TEMPCMD_NAME_LENGTH];
		char m_aParams[IConsole::TEMPCMD_PARAMS_LENGTH];
		char m_aHelpText[IConsole::TEMPCMD_HELP_LENGTH];

		CCommand() = default;
		CCommand(const char *pName, const char *pParams, const char *pHelpText)
		{
			str_copy(m_aName, pName);
			str_copy(m_aParams, pParams);
			str_copy(m_aHelpText, pHelpText);
		}

		bool operator<(const CCommand &Other) const { return str_comp(m_aName, Other.m_aName) < 0; }
		bool operator<=(const CCommand &Other) const { return str_comp(m_aName, Other.m_aName) <= 0; }
		bool operator==(const CCommand &Other) const { return str_comp(m_aName, Other.m_aName) == 0; }
	};

	std::vector<CCommand> m_vServerCommands;
	bool m_ServerCommandsNeedSorting;

	struct CHistoryEntry
	{
		int m_Team;
		char m_aText[1];
	};
	CHistoryEntry *m_pHistoryEntry;
	CStaticRingBuffer<CHistoryEntry, 64 * 1024, CRingBufferBase::FLAG_RECYCLE> m_History;
	int m_PendingChatCounter;
	int64_t m_LastChatSend;
	int64_t m_aLastSoundPlayed[CHAT_NUM];
	bool m_IsInputCensored;
	char m_aCurrentInputText[MAX_LINE_LENGTH];
	bool m_EditingNewLine;
	char m_aSavedInputText[MAX_LINE_LENGTH];
	bool m_SavedInputPending;
	char m_aPreviousDisplayedInputText[MAX_LINE_LENGTH];
	int64_t m_ChatOpenAnimationStart;
	struct STypingGlyphAnim
	{
		int64_t m_StartTime = 0;
		int m_ByteIndex = 0;
		int m_ByteLength = 0;
		char m_aText[16] = "";
	};
	std::vector<STypingGlyphAnim> m_vTypingGlyphAnims;

	// smoothly animated position of the chat input's blinking caret, replacing the input's own
	// instantly-teleporting one while typing animation is enabled
	vec2 m_CaretVisualPos = vec2(0.0f, 0.0f);
	vec2 m_CaretAnimFromPos = vec2(0.0f, 0.0f);
	vec2 m_CaretAnimTargetPos = vec2(0.0f, 0.0f);
	int64_t m_CaretAnimStartTime = 0;
	bool m_CaretAnimValid = false;
	int64_t m_CaretBlinkAnchor = 0;

	int m_BacklogCurLine;
	bool m_ScrollbarDragging;
	float m_ScrollbarDragOffset;
	std::optional<vec2> m_LastMousePos;

	bool m_ServerSupportsCommandInfo;

	CButtonContainer m_TranslateSettingsButton;
	CButtonContainer m_TranslateSettingsEnableButton;
	CButtonContainer m_TranslateSettingsEnableOutgoingButton;
	CButtonContainer m_TranslateSettingsStripPunctuationButton;
	SPopupMenuId m_TranslateSettingsPopupId;
	bool m_TranslateButtonPressed;
	bool m_TranslateButtonRectValid;
	SRenderRect m_TranslateButtonRect;    // chat-space, for ChatMousePos() click detection
	CUIRect m_TranslateButtonUiRect = {0, 0, 0, 0}; // UI-space, for DoPopupMenu
	int m_HoveredTranslateLineIndex = -1;

	bool m_HideMediaByBind;

	int m_FullscreenMediaLineIndex;
	char m_aFullscreenMediaUrl[512];

	static bool IsDirectMediaUrl(const char *pUrl);
	static void ExtractMediaUrlsFromText(const char *pText, std::vector<std::string> &vOutUrls);
	static EMediaKind MediaKindFromUrl(const char *pUrl);
	void SetMediaCandidates(CLine &Line, const std::vector<std::string> &vCandidates);
	void InsertMediaCandidates(CLine &Line, const std::vector<std::string> &vCandidates, int InsertIndex);
	bool QueueNextMediaCandidate(CLine &Line, const char *pReason);
	bool RetryMediaLine(CLine &Line);
	void ResetLineMedia(CLine &Line);
	void ResetHiddenMediaReveals();
	void QueueMediaDownload(CLine &Line);
	void StartMediaDownload(CLine &Line);
	bool StartMediaDecode(CLine &Line, EMediaKind MediaKind, const unsigned char *pData, size_t DataSize);
	void UpdateMediaDownloads();
	bool DecodeStaticImage(const unsigned char *pData, size_t DataSize, const char *pContextName, CLine &Line);
	bool DecodeAnimatedGif(const unsigned char *pData, size_t DataSize, const char *pContextName, CLine &Line);
	bool DecodeImageWithFfmpeg(const unsigned char *pData, size_t DataSize, const char *pContextName, CLine &Line, bool DecodeAllFrames, int MaxAnimationDurationMs);
	bool GetCurrentFrameTexture(CLine &Line, IGraphics::CTextureHandle &Texture) const;
	bool AnyMediaAllowed() const;
	bool IsMediaKindAllowed(EMediaKind Kind) const;
	bool IsMediaUrlAllowed(const char *pUrl) const;
	bool HasAllowedMediaCandidates(const CLine &Line) const;
	bool ShouldDisplayMediaSlot(const CLine &Line) const;
	bool ShouldHideMediaPreview(const CLine &Line) const;
	bool ShouldHideNsfwMedia(const CLine &Line) const;
	std::string MediaPlaceholderText(const CLine &Line) const;
	std::string BuildVisibleMessageText(const CLine &Line, bool UseMediaLabelWhenEmpty) const;
	std::string BuildPlainTextLine(const CLine &Line, int HoveredTranslateLineIndex = -1) const;

	void OpenFullscreenMedia(int LineIndex);
	void CloseFullscreenMedia();
	bool HasValidFullscreenMedia() const;
	void RenderFullscreenMedia(float Width, float Height);

	vec2 ChatMousePos() const;
	void OpenTranslateSettingsPopup(const CUIRect &ButtonRect);
	void RenderTranslateSettingsButton(const CUIRect &ButtonRect);
	static CUi::EPopupMenuFunctionResult PopupTranslateSettings(void *pContext, CUIRect View, bool Active);

	static void ConSay(IConsole::IResult *pResult, void *pUserData);
	static void ConSayTeam(IConsole::IResult *pResult, void *pUserData);
	static void ConChat(IConsole::IResult *pResult, void *pUserData);
	static void ConShowChat(IConsole::IResult *pResult, void *pUserData);
	static void ConEcho(IConsole::IResult *pResult, void *pUserData);
	static void ConClearChat(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleHideChatMedia(IConsole::IResult *pResult, void *pUserData);
	static void ConAddCensorList(IConsole::IResult *pResult, void *pUserData);
	static void ConAddWhiteList(IConsole::IResult *pResult, void *pUserData);

	static void ConchainChatOld(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainChatFontSize(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainChatWidth(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainRegexPlayerWhitelist(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

	static std::vector<std::string> SplitWords(const char *pMessage);
	Regex m_RegexPlayerWhitelist;

	bool LineShouldHighlight(const char *pLine, const char *pName);
	void StoreSave(const char *pText);
	bool HasServerCommand(const char *pName) const;
	bool TryConvertWrongLayoutSlashCommand(const char *pLine, char *pOut, int OutSize) const;
	void ResetTypingAnimation();
	void SyncTypingAnimationBaseline();
	void RefreshTypingAnimation();

	friend class CBindChat;
	friend class CTranslate;
	friend class CTClient;
	friend class CGifBubbles;
	friend class CChatBubbles;

public:
	CChat();
	int Sizeof() const override { return sizeof(*this); }

	static constexpr float MESSAGE_TEE_PADDING_RIGHT = 0.5f;

	bool IsActive() const { return m_Mode != MODE_NONE; }
	void AddLine(int ClientId, int Team, const char *pLine);
	// Client-side colored chat line without the "— " prefix (e.g. Twitch relay).
	void AddColoredLine(const char *pLine, ColorRGBA Color);
	const char *FilterText(const char *pMessage, int ClientId = -2, bool IsChat = false);
	void EnableMode(int Team);
	void DisableMode();
	void RegisterCommand(const char *pName, const char *pParams, const char *pHelpText);
	void UnregisterCommand(const char *pName);
	void Echo(const char *pString);

	void OnWindowResize() override;
	void OnConsoleInit() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnRender() override;
	void OnPrepareLines(float x, float y, int StartLine, int HoveredTranslateLineIndex = -1);
	CUIRect GetHudRect(float HudWidth, float HudHeight, bool ForcePreview = false) const;
	void Reset();
	void OnRelease() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	bool OnInput(const IInput::CEvent &Event) override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	void OnInit() override;

	void RebuildChat();
	void ClearLines();

	void EnsureCoherentFontSize() const;
	void EnsureCoherentWidth() const;

	float FontSize() const { return (g_Config.m_ClChatFontSize / 10.0f) * std::clamp(g_Config.m_BcHudChatScale / 100.0f, 0.25f, 3.0f); }
	float ChatWidth() const { return g_Config.m_ClChatWidth * std::clamp(g_Config.m_BcHudChatScale / 100.0f, 0.25f, 3.0f); }
	float MessagePaddingX() const { return FontSize() * (5 / 6.f); }
	float MessagePaddingY() const { return FontSize() * (1 / 6.f); }
	float MessageTeeSize() const { return FontSize() * (7 / 6.f); }
	float MessageRounding() const { return FontSize() * 0.38f; }

	// ----- send functions -----

	// Sends a chat message to the server.
	//
	// @param Team MODE_ALL=0 MODE_TEAM=1
	// @param pLine the chat message
	void SendChat(int Team, const char *pLine);

	// Sends a chat message to the server.
	//
	// It uses a queue with a maximum of 3 entries
	// that ensures there is a minimum delay of one second
	// between sent messages.
	//
	// It uses team or public chat depending on m_Mode.
	void SendChatQueued(const char *pLine);

	// Like SendChatQueued, but sends to an explicit team (used by the
	// translator to deliver an already-translated outgoing message
	// without re-triggering translation).
	void SendTranslatedChatQueued(int Team, const char *pLine);
};
#endif
