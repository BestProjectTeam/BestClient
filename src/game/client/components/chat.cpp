/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "chat.h"

#include <base/io.h>
#include <base/log.h>
#include <base/time.h>

#include <engine/engine.h>
#include <engine/editor.h>
#include <engine/external/regex.h>
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/shared/csv.h>
#include <engine/shared/http.h>
#include <engine/textrender.h>

#include <generated/protocol.h>
#include <generated/protocol7.h>

#include <game/client/animstate.h>
#include <game/client/components/censor.h>
#include <game/client/components/scoreboard.h>
#include <game/client/components/skins.h>
#include <game/client/components/sounds.h>
#include <game/client/components/tclient/colored_parts.h>
#include <game/client/gameclient.h>
#include <game/client/bc_ui_animations.h>
#include <game/localization.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cinttypes>
#include <limits>
#include <string>
#include <utility>
#include <vector>

char CChat::ms_aDisplayText[MAX_LINE_LENGTH] = "";
static constexpr float CHAT_SCROLLBAR_WIDTH = 5.0f;
static constexpr float CHAT_SCROLLBAR_MARGIN = 0.0f;
static constexpr int CHAT_TYPING_ANIM_MAX_TEXT_BYTES = 16;
static constexpr int CHAT_MEDIA_MAX_CONCURRENT_DOWNLOADS = 3;
static constexpr int CHAT_MEDIA_MAX_COMPLETED_DECODE_PER_FRAME = 1;
static constexpr int CHAT_MEDIA_MAX_TEXTURE_UPLOADS_PER_FRAME = 3;
static constexpr int64_t CHAT_MEDIA_TEXTURE_UPLOAD_BUDGET_US = 2500; // keep frame hitches low while filling short animations faster
static constexpr int64_t CHAT_MEDIA_MAX_RESPONSE_SIZE = 64 * 1024 * 1024;
static constexpr int CHAT_MEDIA_MAX_GIF_FRAMES = 360;
static constexpr int CHAT_MEDIA_MAX_DIMENSION = 960;
static constexpr int CHAT_MEDIA_DOUBLE_CLICK_MS = 300;
static constexpr int CHAT_MEDIA_MAX_RESOLVE_DEPTH = 2;
static constexpr int CHAT_MEDIA_MAX_VIDEO_ANIMATION_MS = 15000;
static constexpr int CHAT_MEDIA_MAX_RETRIES = 3;
static constexpr float CHAT_MEDIA_MAX_PREVIEW_HEIGHT = 70.0f;
static constexpr float CHAT_MEDIA_MAX_PREVIEW_HEIGHT_SCOREBOARD = 56.0f;
static constexpr int CHAT_MEDIA_MAX_URL_LENGTH = 240;
static constexpr int CHAT_MEDIA_MAX_HTML_CANDIDATES = 32;
static constexpr size_t CHAT_MEDIA_MAX_ANIMATED_MEMORY_BYTES = 48ull * 1024ull * 1024ull;
static constexpr bool CHAT_MEDIA_ANIMATE_VIDEOS = true;
static constexpr float CHAT_MEDIA_MIN_PREVIEW_SIDE = 28.0f;

static bool ChatTypingAnimSupportsText(const char *pText)
{
	for(const char *pScan = pText; *pScan;)
	{
		const char *pBefore = pScan;
		const int Codepoint = str_utf8_decode(&pScan);
		if(Codepoint < 0)
			return false;
		if(pScan <= pBefore)
			return false;
	}
	return true;
}

class CChat::CMediaDecodeJob : public IJob
{
	EMediaKind m_MediaKind;
	IGraphics *m_pGraphics;
	std::vector<unsigned char> m_vData;
	char m_aContextName[512];
	SMediaDecodedFrames m_DecodedFrames;
	bool m_Success = false;

protected:
	void Run() override
	{
		if(State() == IJob::STATE_ABORTED || m_vData.empty())
			return;

		SMediaDecodeLimits Limits;
		Limits.m_MaxDimension = CHAT_MEDIA_MAX_DIMENSION;
		Limits.m_MaxFrames = CHAT_MEDIA_MAX_GIF_FRAMES;
		Limits.m_MaxTotalBytes = CHAT_MEDIA_MAX_ANIMATED_MEMORY_BYTES;
		Limits.m_MaxAnimationDurationMs = CHAT_MEDIA_MAX_VIDEO_ANIMATION_MS;

		switch(m_MediaKind)
		{
		case EMediaKind::PHOTO:
			m_Success = MediaDecoder::DecodeStaticImageCpu(m_pGraphics, m_vData.data(), m_vData.size(), m_aContextName, m_DecodedFrames, CHAT_MEDIA_MAX_DIMENSION);
			break;
		case EMediaKind::ANIMATED:
			// Animate previews for short animations within limits (frames/dimension/memory).
			// Long animations fall back to a single-frame thumbnail via m_MaxAnimationDurationMs.
			Limits.m_DecodeAllFrames = true;
			m_Success = MediaDecoder::DecodeImageWithFfmpegCpu(m_pGraphics, m_vData.data(), m_vData.size(), m_aContextName, m_DecodedFrames, Limits);
			if(!m_Success)
			{
				// Fallback for problematic GIF/animated WEBP payloads: decode single preview frame.
				Limits.m_DecodeAllFrames = false;
				m_Success = MediaDecoder::DecodeImageWithFfmpegCpu(m_pGraphics, m_vData.data(), m_vData.size(), m_aContextName, m_DecodedFrames, Limits);
			}
			break;
		case EMediaKind::VIDEO:
			Limits.m_DecodeAllFrames = CHAT_MEDIA_ANIMATE_VIDEOS;
			m_Success = MediaDecoder::DecodeImageWithFfmpegCpu(m_pGraphics, m_vData.data(), m_vData.size(), m_aContextName, m_DecodedFrames, Limits);
			if(!m_Success)
			{
				// Fallback for videos where full animation decode fails: keep a static poster frame.
				Limits.m_DecodeAllFrames = false;
				m_Success = MediaDecoder::DecodeImageWithFfmpegCpu(m_pGraphics, m_vData.data(), m_vData.size(), m_aContextName, m_DecodedFrames, Limits);
			}
			break;
		case EMediaKind::UNKNOWN:
		default:
			m_Success = false;
			break;
		}

		if(State() == IJob::STATE_ABORTED)
		{
			m_Success = false;
			m_DecodedFrames.Free();
		}
	}

public:
	CMediaDecodeJob(IGraphics *pGraphics, EMediaKind MediaKind, const unsigned char *pData, size_t DataSize, const char *pContextName) :
		m_MediaKind(MediaKind),
		m_pGraphics(pGraphics)
	{
		Abortable(true);
		if(pData != nullptr && DataSize > 0)
			m_vData.assign(pData, pData + DataSize);
		str_copy(m_aContextName, pContextName ? pContextName : "", sizeof(m_aContextName));
	}

	~CMediaDecodeJob() override
	{
		m_DecodedFrames.Free();
	}

	bool Success() const { return m_Success; }
	SMediaDecodedFrames &DecodedFrames() { return m_DecodedFrames; }
};

CChat::CLine::CLine()
{
	m_TextContainerIndex.Reset();
	m_QuadContainerIndex = -1;
	m_MediaState = EMediaState::NONE;
	m_MediaKind = EMediaKind::UNKNOWN;
	m_aMediaUrl[0] = '\0';
	m_MediaCandidateIndex = -1;
	m_MediaRetryCount = 0;
	m_MediaUploadIndex = 0;
	m_MediaTotalDurationMs = 0;
	m_MediaAnimated = false;
	m_MediaRevealed = false;
	m_MediaWidth = 0;
	m_MediaHeight = 0;
	m_MediaResolveDepth = 0;
	m_MediaAnimationStart = 0;
	m_aTextHeight[0] = 0.0f;
	m_aTextHeight[1] = 0.0f;
	m_aMediaPreviewWidth[0] = 0.0f;
	m_aMediaPreviewWidth[1] = 0.0f;
	m_aMediaPreviewHeight[0] = 0.0f;
	m_aMediaPreviewHeight[1] = 0.0f;
	m_SelectionStart = -1;
	m_SelectionEnd = -1;
	m_NameRectValid = false;
	m_TranslateRectValid = false;
	m_TranslateLanguageRectValid = false;
	m_MediaPreviewRectValid = false;
	m_MediaRetryRectValid = false;
}

void CChat::CLine::Reset(CChat &This)
{
	This.TextRender()->DeleteTextContainer(m_TextContainerIndex);
	This.Graphics()->DeleteQuadContainer(m_QuadContainerIndex);
	if(This.m_MediaViewerOpen && This.ValidateMediaViewerLine() && &This.m_aLines[This.m_MediaViewerLineIndex] == this)
		This.CloseMediaViewer();
	This.ResetLineMedia(*this);
	m_Initialized = false;
	m_Time = 0;
	m_aText[0] = '\0';
	m_aName[0] = '\0';
	m_Friend = false;
	m_TimesRepeated = 0;
	m_pManagedTeeRenderInfo = nullptr;
	m_pTranslateResponse = nullptr;
	m_SelectionStart = -1;
	m_SelectionEnd = -1;
	m_NameRectValid = false;
	m_TranslateRectValid = false;
	m_TranslateLanguageRectValid = false;
	m_MediaPreviewRectValid = false;
	m_MediaRetryRectValid = false;
}

CChat::CChat()
{
	m_Mode = MODE_NONE;
	m_BacklogCurLine = 0;
	m_ScrollbarDragging = false;
	m_ScrollbarDragOffset = 0.0f;
	m_MouseIsPress = false;
	m_MousePress = vec2(0.0f, 0.0f);
	m_MouseRelease = vec2(0.0f, 0.0f);
	m_HasSelection = false;
	m_WantsSelectionCopy = false;
	m_PrevModeActive = false;
	m_PrevChatSelectionActive = false;
	m_TranslateButtonPressed = false;
	m_TranslateButtonRectValid = false;
	m_HideMediaByBind = false;
	m_MediaViewerOpen = false;
	m_MediaViewerLineIndex = -1;
	m_MediaViewerZoom = 1.0f;
	m_MediaViewerPan = vec2(0.0f, 0.0f);
	m_MediaViewerDragging = false;
	m_MediaViewerDragStartMouse = vec2(0.0f, 0.0f);
	m_MediaViewerPanStart = vec2(0.0f, 0.0f);
	m_MediaViewerLastClickTime = 0;
	m_aPreviousDisplayedInputText[0] = '\0';
	m_ChatOpenAnimationStart = 0;
	m_vTypingGlyphAnims.clear();

	m_Input.SetClipboardLineCallback([this](const char *pStr) { SendChatQueued(pStr); });
	m_Input.SetCalculateOffsetCallback([this]() { return m_IsInputCensored; });
	m_Input.SetDisplayTextCallback([this](char *pStr, size_t NumChars) {
		m_IsInputCensored = false;
		if(
			g_Config.m_ClStreamerMode &&
			(str_startswith(pStr, "/login ") ||
				str_startswith(pStr, "/register ") ||
				str_startswith(pStr, "/code ") ||
				str_startswith(pStr, "/timeout ") ||
				str_startswith(pStr, "/save ") ||
				str_startswith(pStr, "/load ")))
		{
			bool Censor = false;
			const size_t NumLetters = minimum(NumChars, sizeof(ms_aDisplayText) - 1);
			for(size_t i = 0; i < NumLetters; ++i)
			{
				if(Censor)
					ms_aDisplayText[i] = '*';
				else
					ms_aDisplayText[i] = pStr[i];
				if(pStr[i] == ' ')
				{
					Censor = true;
					m_IsInputCensored = true;
				}
			}
			ms_aDisplayText[NumLetters] = '\0';
			return ms_aDisplayText;
		}
		return pStr;
	});
}

void CChat::RegisterCommand(const char *pName, const char *pParams, const char *pHelpText)
{
	// Don't allow duplicate commands.
	for(const auto &Command : m_vServerCommands)
		if(str_comp(Command.m_aName, pName) == 0)
			return;

	m_vServerCommands.emplace_back(pName, pParams, pHelpText);
	m_ServerCommandsNeedSorting = true;
}

void CChat::UnregisterCommand(const char *pName)
{
	m_vServerCommands.erase(std::remove_if(m_vServerCommands.begin(), m_vServerCommands.end(), [pName](const CCommand &Command) { return str_comp(Command.m_aName, pName) == 0; }), m_vServerCommands.end());
}

void CChat::RebuildChat()
{
	for(auto &Line : m_aLines)
	{
		if(!Line.m_Initialized)
			continue;
		TextRender()->DeleteTextContainer(Line.m_TextContainerIndex);
		Graphics()->DeleteQuadContainer(Line.m_QuadContainerIndex);
		// recalculate sizes
		Line.m_aYOffset[0] = -1.0f;
		Line.m_aYOffset[1] = -1.0f;
		if(Line.m_MediaState == EMediaState::NONE && HasAllowedMediaCandidates(Line))
			QueueMediaDownload(Line);
	}
}

void CChat::ClearLines()
{
	for(auto &Line : m_aLines)
		Line.Reset(*this);
	m_PrevScoreBoardShowed = false;
	m_PrevShowChat = false;
	m_PrevModeActive = false;
	m_PrevChatSelectionActive = false;
}

void CChat::OnWindowResize()
{
	RebuildChat();
}

void CChat::Reset()
{
	ClearLines();

	m_Show = false;
	m_CompletionUsed = false;
	m_CompletionChosen = -1;
	m_aCompletionBuffer[0] = 0;
	m_PlaceholderOffset = 0;
	m_PlaceholderLength = 0;
	m_pHistoryEntry = nullptr;
	m_vPendingChatQueue.clear();
	m_LastChatSend = 0;
	m_CurrentLine = 0;
	m_IsInputCensored = false;
	m_EditingNewLine = true;
	m_aSavedInputText[0] = '\0';
	m_SavedInputPending = false;
	m_aPreviousDisplayedInputText[0] = '\0';
	m_ChatOpenAnimationStart = 0;
	m_vTypingGlyphAnims.clear();
	m_ServerSupportsCommandInfo = false;
	m_ServerCommandsNeedSorting = false;
	m_aCurrentInputText[0] = '\0';
	m_BacklogCurLine = 0;
	m_ScrollbarDragging = false;
	m_ScrollbarDragOffset = 0.0f;
	m_LastMousePos = std::nullopt;
	m_TranslateButtonPressed = false;
	m_TranslateButtonRectValid = false;
	m_HideMediaByBind = false;
	m_MediaViewerOpen = false;
	m_MediaViewerLineIndex = -1;
	m_MediaViewerZoom = 1.0f;
	m_MediaViewerPan = vec2(0.0f, 0.0f);
	m_MediaViewerDragging = false;
	m_MediaViewerDragStartMouse = vec2(0.0f, 0.0f);
	m_MediaViewerPanStart = vec2(0.0f, 0.0f);
	m_MediaViewerLastClickTime = 0;
	DisableMode();
	m_vServerCommands.clear();

	for(int64_t &LastSoundPlayed : m_aLastSoundPlayed)
		LastSoundPlayed = 0;
}

void CChat::ResetTypingAnimation()
{
	m_vTypingGlyphAnims.clear();
}

void CChat::SyncTypingAnimationBaseline()
{
	ResetTypingAnimation();
	str_copy(m_aPreviousDisplayedInputText, m_Input.GetDisplayedString(), sizeof(m_aPreviousDisplayedInputText));
}

void CChat::RefreshTypingAnimation()
{
	if(m_Mode == MODE_NONE || !BCUiAnimations::Enabled() || g_Config.m_BcChatAnimation == 0 || g_Config.m_BcChatTypingAnimation == 0 || m_Input.HasSelection() || Input()->HasComposition())
	{
		SyncTypingAnimationBaseline();
		return;
	}

	const char *pCurrent = m_Input.GetDisplayedString();
	const size_t CurrentLen = str_length(pCurrent);
	const size_t PreviousLen = str_length(m_aPreviousDisplayedInputText);

	// Fall back only for invalid UTF-8, otherwise keep per-glyph animation for
	// normal multi-byte text such as Cyrillic.
	if(!ChatTypingAnimSupportsText(pCurrent) || !ChatTypingAnimSupportsText(m_aPreviousDisplayedInputText))
	{
		SyncTypingAnimationBaseline();
		return;
	}

	if(str_comp(pCurrent, m_aPreviousDisplayedInputText) == 0)
		return;

	if(CurrentLen == 0)
	{
		SyncTypingAnimationBaseline();
		return;
	}

	// Find edit boundaries aligned to UTF-8 codepoints (byte-wise diff breaks for multi-byte chars like Cyrillic).
	size_t PrefixBytes = 0;
	{
		const char *pCurScan = pCurrent;
		const char *pPrevScan = m_aPreviousDisplayedInputText;
		while(*pCurScan && *pPrevScan)
		{
			const char *pCurBefore = pCurScan;
			const char *pPrevBefore = pPrevScan;
			const int CurCp = str_utf8_decode(&pCurScan);
			const int PrevCp = str_utf8_decode(&pPrevScan);
			if(CurCp != PrevCp)
			{
				pCurScan = pCurBefore;
				pPrevScan = pPrevBefore;
				break;
			}
			PrefixBytes = (size_t)(pCurScan - pCurrent);
		}
	}

	size_t SuffixBytesCur = 0;
	size_t SuffixBytesPrev = 0;
	{
		int CurCursor = (int)CurrentLen;
		int PrevCursor = (int)PreviousLen;
		while(CurCursor > (int)PrefixBytes && PrevCursor > (int)PrefixBytes)
		{
			const int CurBefore = CurCursor;
			const int PrevBefore = PrevCursor;
			CurCursor = str_utf8_rewind(pCurrent, CurCursor);
			PrevCursor = str_utf8_rewind(m_aPreviousDisplayedInputText, PrevCursor);

			const char *pCurCpPtr = pCurrent + CurCursor;
			const char *pPrevCpPtr = m_aPreviousDisplayedInputText + PrevCursor;
			const int CurCp = str_utf8_decode(&pCurCpPtr);
			const int PrevCp = str_utf8_decode(&pPrevCpPtr);
			if(CurCp != PrevCp)
			{
				CurCursor = CurBefore;
				PrevCursor = PrevBefore;
				break;
			}

			SuffixBytesCur = CurrentLen - (size_t)CurCursor;
			SuffixBytesPrev = PreviousLen - (size_t)PrevCursor;
		}
	}

	const size_t RemovedBytes = PreviousLen - PrefixBytes - SuffixBytesPrev;
	const size_t InsertedBytes = CurrentLen - PrefixBytes - SuffixBytesCur;
	const int EditOldEndByte = (int)(PrefixBytes + RemovedBytes);
	const int DeltaBytes = (int)InsertedBytes - (int)RemovedBytes;

	for(auto It = m_vTypingGlyphAnims.begin(); It != m_vTypingGlyphAnims.end();)
	{
		const int AnimEndByte = It->m_ByteIndex + It->m_ByteLength;
		if(It->m_ByteIndex >= (int)PrefixBytes && AnimEndByte <= EditOldEndByte)
		{
			It = m_vTypingGlyphAnims.erase(It);
			continue;
		}
		if(It->m_ByteIndex >= EditOldEndByte)
			It->m_ByteIndex += DeltaBytes;

		if(It->m_ByteIndex < 0 || It->m_ByteLength <= 0 || It->m_ByteIndex + It->m_ByteLength > (int)CurrentLen)
		{
			It = m_vTypingGlyphAnims.erase(It);
			continue;
		}

		if(str_length(It->m_aText) != It->m_ByteLength ||
			str_comp_num(It->m_aText, pCurrent + It->m_ByteIndex, It->m_ByteLength) != 0)
		{
			It = m_vTypingGlyphAnims.erase(It);
			continue;
		}
		++It;
	}

	if(InsertedBytes > 0)
	{
		for(int ByteIndex = (int)PrefixBytes; ByteIndex < (int)(PrefixBytes + InsertedBytes);)
		{
			const int NextByteIndex = str_utf8_forward(pCurrent, ByteIndex);
			const int GlyphBytes = minimum(NextByteIndex - ByteIndex, CHAT_TYPING_ANIM_MAX_TEXT_BYTES - 1);
			if(GlyphBytes > 0)
			{
				STypingGlyphAnim Anim;
				Anim.m_StartTime = time_get();
				Anim.m_ByteIndex = ByteIndex;
				Anim.m_ByteLength = GlyphBytes;
				str_truncate(Anim.m_aText, sizeof(Anim.m_aText), pCurrent + ByteIndex, GlyphBytes);
				m_vTypingGlyphAnims.push_back(Anim);
			}
			ByteIndex = NextByteIndex;
		}
	}

	str_copy(m_aPreviousDisplayedInputText, pCurrent, sizeof(m_aPreviousDisplayedInputText));
}

void CChat::OnRelease()
{
	m_Show = false;
}

void CChat::OnStateChange(int NewState, int OldState)
{
	if(OldState <= IClient::STATE_CONNECTING)
		Reset();
}

void CChat::ConSay(IConsole::IResult *pResult, void *pUserData)
{
	((CChat *)pUserData)->SendChat(0, pResult->GetString(0));
}

void CChat::ConSayTeam(IConsole::IResult *pResult, void *pUserData)
{
	((CChat *)pUserData)->SendChat(1, pResult->GetString(0));
}

void CChat::ConChat(IConsole::IResult *pResult, void *pUserData)
{
	const char *pMode = pResult->GetString(0);
	if(str_comp(pMode, "all") == 0)
		((CChat *)pUserData)->EnableMode(0);
	else if(str_comp(pMode, "team") == 0)
		((CChat *)pUserData)->EnableMode(1);
	else
		((CChat *)pUserData)->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "console", "expected all or team as mode");

	CChat *pChat = (CChat *)pUserData;
	if(pResult->GetString(1)[0])
	{
		pChat->m_Input.Set(pResult->GetString(1));
	}
	else if(g_Config.m_ClChatReset || !g_Config.m_BcChatSaveDraft)
	{
		if(g_Config.m_BcChatSaveDraft && pChat->m_SavedInputPending)
			pChat->m_Input.Set(pChat->m_aSavedInputText);
		else
			pChat->m_Input.Clear();

		if(!g_Config.m_BcChatSaveDraft)
		{
			pChat->m_SavedInputPending = false;
			pChat->m_aSavedInputText[0] = '\0';
		}
	}
}

void CChat::ConShowChat(IConsole::IResult *pResult, void *pUserData)
{
	((CChat *)pUserData)->m_Show = pResult->GetInteger(0) != 0;
}

void CChat::ConEcho(IConsole::IResult *pResult, void *pUserData)
{
	((CChat *)pUserData)->Echo(pResult->GetString(0));
}

void CChat::ConClearChat(IConsole::IResult *pResult, void *pUserData)
{
	((CChat *)pUserData)->ClearLines();
}

void CChat::ConchainChatOld(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	((CChat *)pUserData)->RebuildChat();
}

void CChat::ConchainChatFontSize(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	CChat *pChat = (CChat *)pUserData;
	pChat->EnsureCoherentWidth();
	pChat->RebuildChat();
}

void CChat::ConchainChatWidth(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	CChat *pChat = (CChat *)pUserData;
	pChat->EnsureCoherentFontSize();
	pChat->RebuildChat();
}

void CChat::Echo(const char *pString)
{
	AddLine(CLIENT_MSG, 0, pString);
}

void CChat::OnConsoleInit()
{
	Console()->Register("say", "r[message]", CFGFLAG_CLIENT, ConSay, this, "Say in chat");
	Console()->Register("say_team", "r[message]", CFGFLAG_CLIENT, ConSayTeam, this, "Say in team chat");
	Console()->Register("chat", "s['team'|'all'] ?r[message]", CFGFLAG_CLIENT, ConChat, this, "Enable chat with all/team mode");
	Console()->Register("+show_chat", "", CFGFLAG_CLIENT, ConShowChat, this, "Show chat");
	Console()->Register("echo", "r[message]", CFGFLAG_CLIENT | CFGFLAG_STORE, ConEcho, this, "Echo the text in chat window");
	Console()->Register("clear_chat", "", CFGFLAG_CLIENT | CFGFLAG_STORE, ConClearChat, this, "Clear chat messages");
	Console()->Register("toggle_chat_media_hidden", "", CFGFLAG_CLIENT, ConToggleHideChatMedia, this, "Toggle hidden media mode in chat");
}

void CChat::OnInit()
{
	Reset();
	Console()->Chain("cl_chat_old", ConchainChatOld, this);
	Console()->Chain("cl_chat_size", ConchainChatFontSize, this);
	Console()->Chain("cl_chat_width", ConchainChatWidth, this);
}

namespace
{
struct STranslateLanguageOption
{
	const char *m_pCode;
	const char *m_pLabel;
};

constexpr STranslateLanguageOption gs_aTranslateSourceOptions[] = {
	{"auto", "Auto"},
	{"ru", "Russian"},
	{"en", "English"},
	{"de", "German"},
	{"zh", "Chinese"},
	{"pt", "Brazilian"},
};

constexpr STranslateLanguageOption gs_aTranslateTargetOptions[] = {
	{"auto", "Auto"},
	{"ru", "Russian"},
	{"en", "English"},
	{"de", "German"},
	{"zh", "Chinese"},
	{"pt", "Brazilian"},
};

template<size_t N>
int TranslateLanguageIndex(const char *pCode, const STranslateLanguageOption (&aOptions)[N])
{
	for(size_t i = 0; i < N; ++i)
	{
		if(str_comp_nocase(pCode, aOptions[i].m_pCode) == 0)
			return (int)i;
	}
	return 0;
}

template<size_t N>
void ApplyTranslateLanguage(char *pConfig, size_t ConfigSize, int Index, const STranslateLanguageOption (&aOptions)[N])
{
	Index = std::clamp(Index, 0, (int)N - 1);
	str_copy(pConfig, aOptions[Index].m_pCode, ConfigSize);
}
}

void CChat::OpenTranslateSettingsPopup(const CUIRect &ButtonRect)
{
	Ui()->DoPopupMenu(&m_TranslateSettingsPopupId, ButtonRect.x, ButtonRect.y, 300.0f, 205.0f, this, PopupTranslateSettings);
}

CUi::EPopupMenuFunctionResult CChat::PopupTranslateSettings(void *pContext, CUIRect View, bool Active)
{
	CChat *pChat = static_cast<CChat *>(pContext);
	(void)Active;
	const float Spacing = 5.0f;
	const float RowHeight = 20.0f;
	const float FontSize = 11.0f;
	static CUi::SDropDownState s_IncomingSourceDropDown;
	static CUi::SDropDownState s_IncomingTargetDropDown;
	static CUi::SDropDownState s_OutgoingSourceDropDown;
	static CUi::SDropDownState s_OutgoingTargetDropDown;
	static CScrollRegion s_IncomingSourceScroll;
	static CScrollRegion s_IncomingTargetScroll;
	static CScrollRegion s_OutgoingSourceScroll;
	static CScrollRegion s_OutgoingTargetScroll;

	s_IncomingSourceDropDown.m_SelectionPopupContext.m_pScrollRegion = &s_IncomingSourceScroll;
	s_IncomingTargetDropDown.m_SelectionPopupContext.m_pScrollRegion = &s_IncomingTargetScroll;
	s_OutgoingSourceDropDown.m_SelectionPopupContext.m_pScrollRegion = &s_OutgoingSourceScroll;
	s_OutgoingTargetDropDown.m_SelectionPopupContext.m_pScrollRegion = &s_OutgoingTargetScroll;

	CUIRect Row;
	View.HSplitTop(14.0f, &Row, &View);
	pChat->Ui()->DoLabel(&Row, Localize("Chat translate"), 12.0f, TEXTALIGN_ML);

	View.HSplitTop(Spacing, nullptr, &View);
	View.HSplitTop(18.0f, &Row, &View);
	if(pChat->GameClient()->m_Menus.DoButton_CheckBox(&pChat->m_TranslateSettingsEnableButton, Localize("Auto translate others' messages"), g_Config.m_TcTranslateAutoIncoming, &Row))
		g_Config.m_TcTranslateAutoIncoming ^= 1;

	View.HSplitTop(Spacing, nullptr, &View);
	View.HSplitTop(18.0f, &Row, &View);
	if(pChat->GameClient()->m_Menus.DoButton_CheckBox(&pChat->m_TranslateSettingsEnableOutgoingButton, Localize("Auto translate your messages"), g_Config.m_TcTranslateAutoOutgoing, &Row))
		g_Config.m_TcTranslateAutoOutgoing ^= 1;

	const auto RenderLanguageField = [&](const char *pLabel, int CurrentIndex, const char **ppLabels, int LabelCount, CUi::SDropDownState &DropDownState) {
		View.HSplitTop(Spacing, nullptr, &View);
		View.HSplitTop(RowHeight, &Row, &View);
		CUIRect Label, DropDown;
		Row.VSplitLeft(145.0f, &Label, &DropDown);
		pChat->Ui()->DoLabel(&Label, pLabel, FontSize, TEXTALIGN_ML);
		return pChat->Ui()->DoDropDown(&DropDown, CurrentIndex, ppLabels, LabelCount, DropDownState);
	};

	static const char *s_apSourceLabels[] = {
		"Auto", "Russian", "English", "German", "Chinese", "Brazilian"};
	static const char *s_apTargetLabels[] = {
		"Auto", "Russian", "English", "German", "Chinese", "Brazilian"};

	const int IncomingSourceIndex = TranslateLanguageIndex(g_Config.m_BcTranslateIncomingSource, gs_aTranslateSourceOptions);
	const int NewIncomingSourceIndex = RenderLanguageField(Localize("Incoming from"), IncomingSourceIndex, s_apSourceLabels, std::size(s_apSourceLabels), s_IncomingSourceDropDown);
	if(NewIncomingSourceIndex != IncomingSourceIndex)
		ApplyTranslateLanguage(g_Config.m_BcTranslateIncomingSource, sizeof(g_Config.m_BcTranslateIncomingSource), NewIncomingSourceIndex, gs_aTranslateSourceOptions);

	const int IncomingTargetIndex = TranslateLanguageIndex(g_Config.m_TcTranslateTarget, gs_aTranslateTargetOptions);
	const int NewIncomingTargetIndex = RenderLanguageField(Localize("Incoming to"), IncomingTargetIndex, s_apTargetLabels, std::size(s_apTargetLabels), s_IncomingTargetDropDown);
	if(NewIncomingTargetIndex != IncomingTargetIndex)
		ApplyTranslateLanguage(g_Config.m_TcTranslateTarget, sizeof(g_Config.m_TcTranslateTarget), NewIncomingTargetIndex, gs_aTranslateTargetOptions);

	const int OutgoingSourceIndex = TranslateLanguageIndex(g_Config.m_BcTranslateOutgoingSource, gs_aTranslateSourceOptions);
	const int NewOutgoingSourceIndex = RenderLanguageField(Localize("Your messages from"), OutgoingSourceIndex, s_apSourceLabels, std::size(s_apSourceLabels), s_OutgoingSourceDropDown);
	if(NewOutgoingSourceIndex != OutgoingSourceIndex)
		ApplyTranslateLanguage(g_Config.m_BcTranslateOutgoingSource, sizeof(g_Config.m_BcTranslateOutgoingSource), NewOutgoingSourceIndex, gs_aTranslateSourceOptions);

	const int OutgoingTargetIndex = TranslateLanguageIndex(g_Config.m_BcTranslateOutgoingTarget, gs_aTranslateTargetOptions);
	const int NewOutgoingTargetIndex = RenderLanguageField(Localize("Your messages to"), OutgoingTargetIndex, s_apTargetLabels, std::size(s_apTargetLabels), s_OutgoingTargetDropDown);
	if(NewOutgoingTargetIndex != OutgoingTargetIndex)
		ApplyTranslateLanguage(g_Config.m_BcTranslateOutgoingTarget, sizeof(g_Config.m_BcTranslateOutgoingTarget), NewOutgoingTargetIndex, gs_aTranslateTargetOptions);

	return CUi::POPUP_KEEP_OPEN;
}

void CChat::RenderTranslateSettingsButton(const CUIRect &ButtonRect)
{
	m_TranslateButtonRect.m_X = ButtonRect.x;
	m_TranslateButtonRect.m_Y = ButtonRect.y;
	m_TranslateButtonRect.m_W = ButtonRect.w;
	m_TranslateButtonRect.m_H = ButtonRect.h;
	m_TranslateButtonRectValid = true;

	const vec2 MousePos = ChatMousePos();
	const bool Hovered = MousePos.x >= ButtonRect.x && MousePos.x <= ButtonRect.x + ButtonRect.w &&
		MousePos.y >= ButtonRect.y && MousePos.y <= ButtonRect.y + ButtonRect.h;
	const bool IsOpen = Ui()->IsPopupOpen(&m_TranslateSettingsPopupId);
	const ColorRGBA ButtonColor = IsOpen ? ColorRGBA(0.35f, 0.45f, 0.70f, 0.90f) :
		(Hovered ? ColorRGBA(0.28f, 0.28f, 0.28f, 0.90f) : ColorRGBA(0.16f, 0.16f, 0.16f, 0.82f));
	const float ButtonRounding = maximum(3.0f, ButtonRect.h * 0.28f);

	ButtonRect.Draw(ButtonColor, IGraphics::CORNER_ALL, ButtonRounding);

	CUIRect IconRect;
	ButtonRect.Margin(1.0f, &IconRect);
	const float IconSize = IconRect.h * CUi::ms_FontmodHeight;
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.95f);
	Ui()->DoLabel(&IconRect, FontIcon::COMMENT, IconSize, TEXTALIGN_MC);
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	if(Hovered)
		Ui()->SetHotItem(&m_TranslateSettingsButton);
	GameClient()->m_Tooltips.DoToolTip(&m_TranslateSettingsButton, &ButtonRect, Localize("Chat translate settings"));
}

namespace
{
static bool IsUrlStart(const char *pStr)
{
	return str_startswith(pStr, "http://") || str_startswith(pStr, "https://");
}

static bool IsTokenEnd(char c)
{
	return c == '\0' || c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

static bool IsTrimmedUrlChar(char c)
{
	return c == '.' || c == ',' || c == '!' || c == '?' || c == ';' || c == ':' ||
		c == ')' || c == ']' || c == '}' || c == '"' || c == '\'' || c == '>';
}

static std::string ExtractUrlHostLower(const std::string &Url)
{
	const size_t SchemePos = Url.find("://");
	if(SchemePos == std::string::npos)
		return {};

	const size_t HostStart = SchemePos + 3;
	const size_t HostEnd = Url.find_first_of("/?#", HostStart);
	std::string HostPort = Url.substr(HostStart, HostEnd == std::string::npos ? std::string::npos : HostEnd - HostStart);

	// Strip userinfo (user:pass@host).
	const size_t AtPos = HostPort.rfind('@');
	if(AtPos != std::string::npos)
		HostPort = HostPort.substr(AtPos + 1);

	// Strip port (host:port) while supporting IPv6 literals in brackets.
	if(!HostPort.empty() && HostPort.front() == '[')
	{
		const size_t Close = HostPort.find(']');
		if(Close != std::string::npos)
			HostPort = HostPort.substr(1, Close - 1);
	}
	else
	{
		const size_t ColonPos = HostPort.find(':');
		if(ColonPos != std::string::npos)
			HostPort = HostPort.substr(0, ColonPos);
	}

	while(!HostPort.empty() && HostPort.back() == '.')
		HostPort.pop_back();

	std::transform(HostPort.begin(), HostPort.end(), HostPort.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return HostPort;
}

static bool HostIsOrEndsWith(const std::string &HostLower, const char *pDomainLower)
{
	const std::string Domain(pDomainLower);
	if(HostLower == Domain)
		return true;
	if(HostLower.size() <= Domain.size())
		return false;
	const size_t Start = HostLower.size() - Domain.size();
	return HostLower.compare(Start, Domain.size(), Domain) == 0 && HostLower[Start - 1] == '.';
}

static std::string TrimAsciiWhitespaceCopy(std::string Value)
{
	while(!Value.empty() && std::isspace((unsigned char)Value.front()))
		Value.erase(Value.begin());
	while(!Value.empty() && std::isspace((unsigned char)Value.back()))
		Value.pop_back();
	return Value;
}

static std::string NormalizeAllowedMediaDomain(std::string Domain)
{
	Domain = TrimAsciiWhitespaceCopy(std::move(Domain));
	std::transform(Domain.begin(), Domain.end(), Domain.begin(), [](unsigned char c) { return (char)std::tolower(c); });

	const size_t SchemePos = Domain.find("://");
	if(SchemePos != std::string::npos)
		Domain = Domain.substr(SchemePos + 3);

	const size_t AtPos = Domain.rfind('@');
	if(AtPos != std::string::npos)
		Domain = Domain.substr(AtPos + 1);

	if(!Domain.empty() && Domain.front() == '[')
	{
		const size_t ClosePos = Domain.find(']');
		if(ClosePos != std::string::npos)
			Domain = Domain.substr(1, ClosePos - 1);
	}
	else
	{
		const size_t SlashPos = Domain.find_first_of("/?#");
		if(SlashPos != std::string::npos)
			Domain.resize(SlashPos);
		const size_t ColonPos = Domain.find(':');
		if(ColonPos != std::string::npos)
			Domain.resize(ColonPos);
	}

	while(!Domain.empty() && (Domain.front() == '.' || std::isspace((unsigned char)Domain.front())))
		Domain.erase(Domain.begin());
	while(!Domain.empty() && (Domain.back() == '.' || std::isspace((unsigned char)Domain.back())))
		Domain.pop_back();

	return Domain;
}

static bool IsAllowedChatMediaHost(const std::string &HostLower)
{
	if(!g_Config.m_BcChatMediaContentFilter)
		return true;
	if(HostLower.empty())
		return false;

	const char *pList = g_Config.m_BcChatMediaAllowedDomains;
	if(pList == nullptr || pList[0] == '\0')
		return false;

	const char *pTokenStart = pList;
	while(true)
	{
		const char *pSep = str_find(pTokenStart, ";");
		const size_t TokenLen = pSep ? (size_t)(pSep - pTokenStart) : str_length(pTokenStart);
		std::string Domain = NormalizeAllowedMediaDomain(std::string(pTokenStart, TokenLen));
		if(!Domain.empty())
		{
			if(HostLower == Domain)
				return true;
			if(HostLower.size() > Domain.size())
			{
				const size_t Start = HostLower.size() - Domain.size();
				if(HostLower.compare(Start, Domain.size(), Domain) == 0 && HostLower[Start - 1] == '.')
					return true;
			}
		}

		if(!pSep)
			break;
		pTokenStart = pSep + 1;
	}

	return false;
}

static bool IsAllowedChatMediaUrl(const char *pUrl)
{
	if(!g_Config.m_BcChatMediaContentFilter)
		return true;
	if(pUrl == nullptr || pUrl[0] == '\0')
		return false;
	return IsAllowedChatMediaHost(ExtractUrlHostLower(pUrl));
}

static bool IsYouTubeUrl(const std::string &Url)
{
	const std::string HostLower = ExtractUrlHostLower(Url);
	if(HostLower.empty())
		return false;

	// Prevent media previews for YouTube links (the media preview fetcher may otherwise resolve
	// thumbnails/embeds from HTML/JSON-LD).
	return HostIsOrEndsWith(HostLower, "youtube.com") ||
		HostIsOrEndsWith(HostLower, "youtu.be") ||
		HostIsOrEndsWith(HostLower, "youtube-nocookie.com") ||
		HostIsOrEndsWith(HostLower, "ytimg.com") ||
		HostIsOrEndsWith(HostLower, "googlevideo.com");
}

static std::string ExtractUrlPath(const std::string &Url)
{
	const size_t SchemePos = Url.find("://");
	if(SchemePos == std::string::npos)
		return {};

	const size_t PathStart = Url.find('/', SchemePos + 3);
	if(PathStart == std::string::npos)
		return "/";

	const size_t PathEnd = Url.find_first_of("?#", PathStart);
	return Url.substr(PathStart, PathEnd == std::string::npos ? std::string::npos : PathEnd - PathStart);
}

static bool ExtractGiphyMediaId(const std::string &Url, std::string &OutMediaId)
{
	OutMediaId.clear();
	const std::string HostLower = ExtractUrlHostLower(Url);
	if(!HostIsOrEndsWith(HostLower, "giphy.com"))
		return false;

	const std::string Path = ExtractUrlPath(Url);
	if(Path.empty() || Path.find("/gifs/") == std::string::npos)
		return false;

	size_t SegmentStart = Path.find_last_of('/');
	if(SegmentStart == std::string::npos || SegmentStart + 1 >= Path.size())
		return false;

	std::string LastSegment = Path.substr(SegmentStart + 1);
	if(LastSegment.empty())
		return false;

	const size_t DashPos = LastSegment.find_last_of('-');
	if(DashPos != std::string::npos && DashPos + 1 < LastSegment.size())
		LastSegment = LastSegment.substr(DashPos + 1);

	if(LastSegment.size() < 6 || LastSegment.size() > 64)
		return false;

	for(char c : LastSegment)
	{
		if(!std::isalnum((unsigned char)c))
			return false;
	}

	OutMediaId = LastSegment;
	return true;
}

static void AddDirectGiphyCandidates(const std::string &Url, std::vector<std::string> &vOutCandidates)
{
	std::string MediaId;
	if(!ExtractGiphyMediaId(Url, MediaId))
		return;

	const char *apHosts[] = {"https://media.giphy.com/media/", "https://media1.giphy.com/media/"};
	const char *apFormats[] = {"giphy.mp4", "giphy.gif", "giphy.webp"};
	for(const char *pHost : apHosts)
	{
		for(const char *pFormat : apFormats)
		{
			std::string Candidate = std::string(pHost) + MediaId + "/" + pFormat;
			if((int)Candidate.size() <= CHAT_MEDIA_MAX_URL_LENGTH)
				vOutCandidates.push_back(std::move(Candidate));
		}
	}
}

static bool IsGifSignature(const unsigned char *pData, size_t DataSize)
{
	return DataSize >= 6 && (mem_comp(pData, "GIF87a", 6) == 0 || mem_comp(pData, "GIF89a", 6) == 0);
}

static std::string ExtractUrlExtensionLower(const std::string &Url)
{
	const size_t QueryPos = Url.find_first_of("?#");
	const std::string Path = Url.substr(0, QueryPos);
	const size_t SlashPos = Path.find_last_of('/');
	const size_t DotPos = Path.find_last_of('.');
	if(DotPos == std::string::npos || (SlashPos != std::string::npos && DotPos < SlashPos))
		return {};

	std::string Ext = Path.substr(DotPos + 1);
	std::transform(Ext.begin(), Ext.end(), Ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return Ext;
}

static bool IsLikelyImageExtension(const std::string &Ext)
{
	return Ext == "png" || Ext == "jpg" || Ext == "jpeg" || Ext == "gif" || Ext == "webp" || Ext == "bmp" || Ext == "avif" || Ext == "apng";
}

static bool IsLikelyAnimatedImageExtension(const std::string &Ext)
{
	return Ext == "gif" || Ext == "webp" || Ext == "apng" || Ext == "avif";
}

static bool IsLikelyVideoExtension(const std::string &Ext)
{
	return Ext == "mp4" || Ext == "webm" || Ext == "mov" || Ext == "m4v" || Ext == "mkv" || Ext == "avi" ||
		Ext == "gifv" || Ext == "mpg" || Ext == "mpeg" || Ext == "ogv" || Ext == "3gp" || Ext == "3g2" ||
		Ext == "flv" || Ext == "wmv" || Ext == "asf" || Ext == "ts" || Ext == "m2ts" || Ext == "mts" || Ext == "f4v";
}

static bool IsBlockedMediaExtension(const std::string &Ext)
{
	return Ext == "svg" || Ext == "svgz" || Ext == "ico" || Ext == "css" || Ext == "js" || Ext == "json" || Ext == "txt" || Ext == "xml" || Ext == "pdf" || Ext == "html" || Ext == "htm";
}

static bool IsLikelyMediaExtension(const std::string &Ext)
{
	return IsLikelyImageExtension(Ext) || IsLikelyVideoExtension(Ext);
}

static bool IsPngSignature(const unsigned char *pData, size_t DataSize)
{
	static const unsigned char s_aPngSig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
	return DataSize >= 8 && mem_comp(pData, s_aPngSig, 8) == 0;
}

static bool IsJpegSignature(const unsigned char *pData, size_t DataSize)
{
	return DataSize >= 3 && pData[0] == 0xff && pData[1] == 0xd8 && pData[2] == 0xff;
}

static bool IsWebpSignature(const unsigned char *pData, size_t DataSize)
{
	return DataSize >= 12 && mem_comp(pData, "RIFF", 4) == 0 && mem_comp(pData + 8, "WEBP", 4) == 0;
}

static bool IsBmpSignature(const unsigned char *pData, size_t DataSize)
{
	return DataSize >= 2 && pData[0] == 'B' && pData[1] == 'M';
}

static bool IsMp4LikeSignature(const unsigned char *pData, size_t DataSize)
{
	return DataSize >= 12 && mem_comp(pData + 4, "ftyp", 4) == 0;
}

static bool IsWebmSignature(const unsigned char *pData, size_t DataSize)
{
	static const unsigned char s_aWebmSig[4] = {0x1a, 0x45, 0xdf, 0xa3};
	return DataSize >= 4 && mem_comp(pData, s_aWebmSig, 4) == 0;
}

static bool IsOggSignature(const unsigned char *pData, size_t DataSize)
{
	return DataSize >= 4 && mem_comp(pData, "OggS", 4) == 0;
}

static bool IsImagePayloadSignature(const unsigned char *pData, size_t DataSize)
{
	return IsPngSignature(pData, DataSize) || IsJpegSignature(pData, DataSize) || IsGifSignature(pData, DataSize) || IsWebpSignature(pData, DataSize) || IsBmpSignature(pData, DataSize);
}

static bool IsVideoPayloadSignature(const unsigned char *pData, size_t DataSize)
{
	return IsMp4LikeSignature(pData, DataSize) || IsWebmSignature(pData, DataSize) || IsOggSignature(pData, DataSize);
}

static std::string ToLowerAscii(std::string Value)
{
	std::transform(Value.begin(), Value.end(), Value.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return Value;
}

static void ReplaceAll(std::string &Value, const char *pFrom, const char *pTo)
{
	const std::string From(pFrom);
	const std::string To(pTo);
	size_t Pos = 0;
	while((Pos = Value.find(From, Pos)) != std::string::npos)
	{
		Value.replace(Pos, From.size(), To);
		Pos += To.size();
	}
}

static std::string DecodeHtmlUrl(std::string Value)
{
	ReplaceAll(Value, "&amp;", "&");
	ReplaceAll(Value, "&quot;", "\"");
	ReplaceAll(Value, "&#39;", "'");
	ReplaceAll(Value, "&lt;", "<");
	ReplaceAll(Value, "&gt;", ">");
	ReplaceAll(Value, "\\/", "/");
	return Value;
}

static void TrimAsciiWhitespace(std::string &Value)
{
	while(!Value.empty() && std::isspace((unsigned char)Value.front()))
		Value.erase(Value.begin());
	while(!Value.empty() && std::isspace((unsigned char)Value.back()))
		Value.pop_back();
}

static bool ExtractHtmlAttribute(const std::string &Tag, const std::string &TagLower, const char *pAttrName, std::string &OutValue)
{
	const std::string AttrName = ToLowerAscii(pAttrName);
	size_t Pos = 0;
	while((Pos = TagLower.find(AttrName, Pos)) != std::string::npos)
	{
		const bool LeftBoundary = Pos == 0 || std::isspace((unsigned char)TagLower[Pos - 1]) || TagLower[Pos - 1] == '<' || TagLower[Pos - 1] == '/';
		if(!LeftBoundary)
		{
			Pos += AttrName.size();
			continue;
		}

		size_t EqPos = Pos + AttrName.size();
		while(EqPos < TagLower.size() && std::isspace((unsigned char)TagLower[EqPos]))
			EqPos++;
		if(EqPos >= TagLower.size() || TagLower[EqPos] != '=')
		{
			Pos += AttrName.size();
			continue;
		}
		EqPos++;
		while(EqPos < Tag.size() && std::isspace((unsigned char)Tag[EqPos]))
			EqPos++;
		if(EqPos >= Tag.size())
			return false;

		size_t ValueBegin = EqPos;
		size_t ValueEnd = EqPos;
		if(Tag[EqPos] == '"' || Tag[EqPos] == '\'')
		{
			const char Quote = Tag[EqPos];
			ValueBegin = EqPos + 1;
			ValueEnd = Tag.find(Quote, ValueBegin);
			if(ValueEnd == std::string::npos)
				return false;
		}
		else
		{
			while(ValueEnd < Tag.size() && !std::isspace((unsigned char)Tag[ValueEnd]) && Tag[ValueEnd] != '>')
				ValueEnd++;
		}

		OutValue = DecodeHtmlUrl(Tag.substr(ValueBegin, ValueEnd - ValueBegin));
		TrimAsciiWhitespace(OutValue);
		return !OutValue.empty();
	}
	return false;
}

static bool ResolveRelativeUrl(const std::string &BaseUrl, const std::string &CandidateUrl, std::string &OutResolvedUrl)
{
	if(CandidateUrl.empty())
		return false;
	if(str_startswith(CandidateUrl.c_str(), "http://") || str_startswith(CandidateUrl.c_str(), "https://"))
	{
		OutResolvedUrl = CandidateUrl;
		return true;
	}
	if(str_startswith(CandidateUrl.c_str(), "//"))
	{
		const size_t SchemePos = BaseUrl.find("://");
		if(SchemePos == std::string::npos)
			return false;
		OutResolvedUrl = BaseUrl.substr(0, SchemePos) + ":" + CandidateUrl;
		return true;
	}
	if(CandidateUrl[0] == '#')
		return false;

	const size_t SchemePos = BaseUrl.find("://");
	if(SchemePos == std::string::npos)
		return false;
	const size_t HostStart = SchemePos + 3;
	const size_t PathStart = BaseUrl.find('/', HostStart);
	const std::string Origin = PathStart == std::string::npos ? BaseUrl : BaseUrl.substr(0, PathStart);

	if(CandidateUrl[0] == '/')
	{
		OutResolvedUrl = Origin + CandidateUrl;
		return true;
	}

	std::string BasePath = PathStart == std::string::npos ? "/" : BaseUrl.substr(PathStart);
	const size_t QueryPos = BasePath.find_first_of("?#");
	if(QueryPos != std::string::npos)
		BasePath.resize(QueryPos);
	size_t LastSlash = BasePath.find_last_of('/');
	if(LastSlash == std::string::npos)
		BasePath = "/";
	else
		BasePath.resize(LastSlash + 1);

	OutResolvedUrl = Origin + BasePath + CandidateUrl;
	return true;
}

static bool ResolveAndFilterCandidateUrl(const char *pBaseUrl, const std::string &RawCandidate, std::string &OutResolvedUrl, bool AllowUnknownExtensions)
{
	std::string Candidate = DecodeHtmlUrl(RawCandidate);
	TrimAsciiWhitespace(Candidate);
	if(Candidate.empty())
		return false;

	const std::string CandidateLower = ToLowerAscii(Candidate);
	if(str_startswith(CandidateLower.c_str(), "data:") || str_startswith(CandidateLower.c_str(), "blob:") ||
		str_startswith(CandidateLower.c_str(), "javascript:") || str_startswith(CandidateLower.c_str(), "mailto:") ||
		str_startswith(CandidateLower.c_str(), "about:"))
	{
		return false;
	}

	std::string Resolved;
	if(IsUrlStart(Candidate.c_str()))
		Resolved = Candidate;
	else
	{
		if(!pBaseUrl || !IsUrlStart(pBaseUrl) || !ResolveRelativeUrl(pBaseUrl, Candidate, Resolved))
			return false;
	}

	if(!IsUrlStart(Resolved.c_str()))
		return false;
	if((int)Resolved.size() > CHAT_MEDIA_MAX_URL_LENGTH)
		return false;
	for(char c : Resolved)
	{
		if((unsigned char)c < 32 || c == ' ' || c == '\t' || c == '\n' || c == '\r')
			return false;
	}

	const std::string LowerResolved = ToLowerAscii(Resolved);
	const std::string Ext = ExtractUrlExtensionLower(LowerResolved);
	if(!Ext.empty() && IsBlockedMediaExtension(Ext))
		return false;
	if(!AllowUnknownExtensions && !Ext.empty() && !IsLikelyMediaExtension(Ext))
		return false;

	OutResolvedUrl = Resolved;
	return true;
}

static bool IsLikelyHtmlDocument(const unsigned char *pData, size_t DataSize)
{
	if(!pData || DataSize == 0)
		return false;

	const size_t ScanSize = minimum(DataSize, (size_t)8192);
	std::string Prefix((const char *)pData, ScanSize);
	const std::string PrefixLower = ToLowerAscii(Prefix);
	return PrefixLower.find("<!doctype html") != std::string::npos ||
		PrefixLower.find("<html") != std::string::npos ||
		PrefixLower.find("<head") != std::string::npos ||
		PrefixLower.find("<meta") != std::string::npos;
}

static void FindMetaContentsByKey(const std::string &Html, const std::string &HtmlLower, const char *pKey, std::vector<std::string> &vOutValues)
{
	const std::string KeyLower = ToLowerAscii(pKey);
	size_t Pos = 0;
	while((Pos = HtmlLower.find("<meta", Pos)) != std::string::npos)
	{
		const size_t EndPos = HtmlLower.find('>', Pos);
		if(EndPos == std::string::npos)
			break;
		if(EndPos - Pos > 3072)
		{
			Pos = EndPos + 1;
			continue;
		}

		const std::string Tag = Html.substr(Pos, EndPos - Pos + 1);
		const std::string TagLower = HtmlLower.substr(Pos, EndPos - Pos + 1);
		std::string NameOrProperty;
		const bool MatchesProperty = ExtractHtmlAttribute(Tag, TagLower, "property", NameOrProperty) && ToLowerAscii(NameOrProperty) == KeyLower;
		const bool MatchesName = ExtractHtmlAttribute(Tag, TagLower, "name", NameOrProperty) && ToLowerAscii(NameOrProperty) == KeyLower;
		if(MatchesProperty || MatchesName)
		{
			std::string Value;
			if(ExtractHtmlAttribute(Tag, TagLower, "content", Value) ||
				ExtractHtmlAttribute(Tag, TagLower, "src", Value) ||
				ExtractHtmlAttribute(Tag, TagLower, "href", Value))
			{
				vOutValues.push_back(Value);
			}
		}
		Pos = EndPos + 1;
	}
}

static void CollectLinkMediaHrefs(const std::string &Html, const std::string &HtmlLower, std::vector<std::string> &vOutValues)
{
	size_t Pos = 0;
	while((Pos = HtmlLower.find("<link", Pos)) != std::string::npos)
	{
		const size_t EndPos = HtmlLower.find('>', Pos);
		if(EndPos == std::string::npos)
			break;

		const std::string Tag = Html.substr(Pos, EndPos - Pos + 1);
		const std::string TagLower = HtmlLower.substr(Pos, EndPos - Pos + 1);
		std::string Rel;
		if(ExtractHtmlAttribute(Tag, TagLower, "rel", Rel))
		{
			const std::string RelLower = ToLowerAscii(Rel);
			if(RelLower.find("image_src") != std::string::npos || RelLower.find("thumbnail") != std::string::npos ||
				RelLower.find("image") != std::string::npos || RelLower.find("video") != std::string::npos)
			{
				std::string Value;
				if(ExtractHtmlAttribute(Tag, TagLower, "href", Value))
					vOutValues.push_back(Value);
			}
		}

		Pos = EndPos + 1;
	}
}

static void CollectImageTagSources(const std::string &Html, const std::string &HtmlLower, std::vector<std::string> &vOutValues)
{
	size_t Pos = 0;
	while((Pos = HtmlLower.find("<img", Pos)) != std::string::npos)
	{
		const size_t EndPos = HtmlLower.find('>', Pos);
		if(EndPos == std::string::npos)
			break;
		if(EndPos - Pos > 4096)
		{
			Pos = EndPos + 1;
			continue;
		}

		const std::string Tag = Html.substr(Pos, EndPos - Pos + 1);
		const std::string TagLower = HtmlLower.substr(Pos, EndPos - Pos + 1);
		std::string Value;
		if(ExtractHtmlAttribute(Tag, TagLower, "src", Value) ||
			ExtractHtmlAttribute(Tag, TagLower, "data-src", Value) ||
			ExtractHtmlAttribute(Tag, TagLower, "data-original", Value))
		{
			vOutValues.push_back(Value);
		}

		Pos = EndPos + 1;
	}
}

static void CollectVideoTagSources(const std::string &Html, const std::string &HtmlLower, std::vector<std::string> &vOutValues)
{
	size_t Pos = 0;
	while((Pos = HtmlLower.find("<video", Pos)) != std::string::npos)
	{
		const size_t EndPos = HtmlLower.find('>', Pos);
		if(EndPos == std::string::npos)
			break;
		if(EndPos - Pos > 4096)
		{
			Pos = EndPos + 1;
			continue;
		}

		const std::string Tag = Html.substr(Pos, EndPos - Pos + 1);
		const std::string TagLower = HtmlLower.substr(Pos, EndPos - Pos + 1);
		std::string Value;
		if(ExtractHtmlAttribute(Tag, TagLower, "src", Value) ||
			ExtractHtmlAttribute(Tag, TagLower, "poster", Value) ||
			ExtractHtmlAttribute(Tag, TagLower, "data-src", Value))
		{
			vOutValues.push_back(Value);
		}

		Pos = EndPos + 1;
	}
}

static void CollectSourceTagSources(const std::string &Html, const std::string &HtmlLower, std::vector<std::string> &vOutValues)
{
	size_t Pos = 0;
	while((Pos = HtmlLower.find("<source", Pos)) != std::string::npos)
	{
		const size_t EndPos = HtmlLower.find('>', Pos);
		if(EndPos == std::string::npos)
			break;
		if(EndPos - Pos > 4096)
		{
			Pos = EndPos + 1;
			continue;
		}

		const std::string Tag = Html.substr(Pos, EndPos - Pos + 1);
		const std::string TagLower = HtmlLower.substr(Pos, EndPos - Pos + 1);
		std::string Value;
		if(ExtractHtmlAttribute(Tag, TagLower, "src", Value))
			vOutValues.push_back(Value);

		Pos = EndPos + 1;
	}
}

static bool TryParseJsonQuotedValue(const std::string &Json, size_t QuotePos, std::string &OutValue, size_t &OutEndPos)
{
	if(QuotePos >= Json.size() || (Json[QuotePos] != '"' && Json[QuotePos] != '\''))
		return false;
	const char Quote = Json[QuotePos];
	std::string Value;
	size_t Pos = QuotePos + 1;
	while(Pos < Json.size())
	{
		const char c = Json[Pos++];
		if(c == '\\')
		{
			if(Pos >= Json.size())
				break;
			Value.push_back(Json[Pos++]);
			continue;
		}
		if(c == Quote)
		{
			OutValue = Value;
			OutEndPos = Pos;
			return true;
		}
		Value.push_back(c);
	}
	return false;
}

static void FindJsonValuesByKey(const std::string &Json, const std::string &JsonLower, const char *pKey, std::vector<std::string> &vOutValues)
{
	const std::string KeyPattern = "\"" + ToLowerAscii(pKey) + "\"";
	size_t Pos = 0;
	while((Pos = JsonLower.find(KeyPattern, Pos)) != std::string::npos)
	{
		const size_t ColonPos = JsonLower.find(':', Pos + KeyPattern.size());
		if(ColonPos == std::string::npos)
			break;

		size_t ValuePos = ColonPos + 1;
		while(ValuePos < Json.size() && std::isspace((unsigned char)Json[ValuePos]))
			ValuePos++;
		if(ValuePos >= Json.size())
			break;

		if(Json[ValuePos] == '"' || Json[ValuePos] == '\'')
		{
			std::string Value;
			size_t EndPos = ValuePos;
			if(TryParseJsonQuotedValue(Json, ValuePos, Value, EndPos))
			{
				vOutValues.push_back(Value);
				Pos = EndPos;
				continue;
			}
		}
		else
		{
			size_t EndPos = ValuePos;
			while(EndPos < Json.size() && Json[EndPos] != ',' && Json[EndPos] != '}' && Json[EndPos] != ']' && !std::isspace((unsigned char)Json[EndPos]))
				EndPos++;
			if(EndPos > ValuePos)
			{
				vOutValues.emplace_back(Json.substr(ValuePos, EndPos - ValuePos));
				Pos = EndPos;
				continue;
			}
		}

		Pos += KeyPattern.size();
	}
}

static void CollectJsonLdMediaCandidates(const std::string &Html, const std::string &HtmlLower, std::vector<std::string> &vOutValues)
{
	size_t Pos = 0;
	while((Pos = HtmlLower.find("<script", Pos)) != std::string::npos)
	{
		const size_t TagEnd = HtmlLower.find('>', Pos);
		if(TagEnd == std::string::npos)
			break;
		const size_t ClosePos = HtmlLower.find("</script>", TagEnd + 1);
		if(ClosePos == std::string::npos)
			break;

		const std::string Tag = Html.substr(Pos, TagEnd - Pos + 1);
		const std::string TagLower = HtmlLower.substr(Pos, TagEnd - Pos + 1);
		std::string TypeValue;
		if(!ExtractHtmlAttribute(Tag, TagLower, "type", TypeValue) || ToLowerAscii(TypeValue).find("ld+json") == std::string::npos)
		{
			Pos = ClosePos + 9;
			continue;
		}

		const std::string ScriptBody = Html.substr(TagEnd + 1, ClosePos - (TagEnd + 1));
		const std::string ScriptBodyLower = ToLowerAscii(ScriptBody);
		const char *apJsonKeys[] = {"contentUrl", "thumbnailUrl", "video", "embedUrl", "url", "mp4", "srcUrl"};
		for(const char *pKey : apJsonKeys)
			FindJsonValuesByKey(ScriptBody, ScriptBodyLower, pKey, vOutValues);

		Pos = ClosePos + 9;
	}
}

static void ExtractMediaUrlsFromHtmlDocument(const unsigned char *pData, size_t DataSize, const char *pBaseUrl, std::vector<std::string> &vOutUrls)
{
	vOutUrls.clear();
	if(!pData || DataSize == 0 || !pBaseUrl || !IsLikelyHtmlDocument(pData, DataSize))
		return;

	const size_t MaxHtmlParseSize = 256 * 1024;
	const size_t HtmlSize = minimum(DataSize, MaxHtmlParseSize);
	const std::string Html((const char *)pData, HtmlSize);
	const std::string HtmlLower = ToLowerAscii(Html);

	struct SPrioritizedCandidate
	{
		int m_Priority = 0;
		std::string m_Value;
	};

	std::vector<SPrioritizedCandidate> vRawCandidates;
	const auto AddCandidates = [&](int Priority, const std::vector<std::string> &vValues) {
		for(const std::string &Value : vValues)
		{
			vRawCandidates.push_back({Priority, Value});
			if((int)vRawCandidates.size() >= CHAT_MEDIA_MAX_HTML_CANDIDATES * 8)
				return;
		}
	};

	const char *apMetaVideoKeys[] = {"og:video", "og:video:url", "og:video:secure_url", "twitter:video", "twitter:video:src", "twitter:player:stream"};
	for(const char *pKey : apMetaVideoKeys)
	{
		std::vector<std::string> vValues;
		FindMetaContentsByKey(Html, HtmlLower, pKey, vValues);
		AddCandidates(0, vValues);
	}

	const char *apMetaImageKeys[] = {"og:image", "og:image:url", "og:image:secure_url", "twitter:image", "twitter:image:src"};
	for(const char *pKey : apMetaImageKeys)
	{
		std::vector<std::string> vValues;
		FindMetaContentsByKey(Html, HtmlLower, pKey, vValues);
		AddCandidates(1, vValues);
	}

	{
		std::vector<std::string> vValues;
		CollectVideoTagSources(Html, HtmlLower, vValues);
		AddCandidates(1, vValues);
	}
	{
		std::vector<std::string> vValues;
		CollectSourceTagSources(Html, HtmlLower, vValues);
		AddCandidates(1, vValues);
	}
	{
		std::vector<std::string> vValues;
		CollectLinkMediaHrefs(Html, HtmlLower, vValues);
		AddCandidates(2, vValues);
	}
	{
		std::vector<std::string> vValues;
		CollectJsonLdMediaCandidates(Html, HtmlLower, vValues);
		AddCandidates(2, vValues);
	}
	{
		std::vector<std::string> vValues;
		CollectImageTagSources(Html, HtmlLower, vValues);
		AddCandidates(3, vValues);
	}

	std::vector<std::pair<int, std::string>> vResolvedCandidates;
	for(const auto &Candidate : vRawCandidates)
	{
		if((int)vResolvedCandidates.size() >= CHAT_MEDIA_MAX_HTML_CANDIDATES)
			break;
		std::string Resolved;
		if(!ResolveAndFilterCandidateUrl(pBaseUrl, Candidate.m_Value, Resolved, true))
			continue;
		if(str_comp(Resolved.c_str(), pBaseUrl) == 0)
			continue;

		bool Exists = false;
		for(const auto &Entry : vResolvedCandidates)
		{
			if(str_comp(Entry.second.c_str(), Resolved.c_str()) == 0)
			{
				Exists = true;
				break;
			}
		}
		if(!Exists)
			vResolvedCandidates.emplace_back(Candidate.m_Priority, std::move(Resolved));
	}

	std::stable_sort(vResolvedCandidates.begin(), vResolvedCandidates.end(),
		[](const auto &A, const auto &B) { return A.first < B.first; });

	for(const auto &Entry : vResolvedCandidates)
	{
		if((int)vOutUrls.size() >= CHAT_MEDIA_MAX_HTML_CANDIDATES)
			break;
		vOutUrls.push_back(Entry.second);
	}
}
} // namespace

bool CChat::IsDirectMediaUrl(const char *pUrl)
{
	if(!pUrl || !IsUrlStart(pUrl))
		return false;

	const std::string Ext = ExtractUrlExtensionLower(pUrl);
	return !Ext.empty() && (IsLikelyImageExtension(Ext) || IsLikelyVideoExtension(Ext));
}

CChat::EMediaKind CChat::MediaKindFromUrl(const char *pUrl)
{
	if(!pUrl)
		return EMediaKind::UNKNOWN;

	const std::string Ext = ExtractUrlExtensionLower(pUrl);
	if(IsLikelyVideoExtension(Ext))
		return EMediaKind::VIDEO;
	if(IsLikelyAnimatedImageExtension(Ext))
		return EMediaKind::ANIMATED;
	if(IsLikelyImageExtension(Ext))
		return EMediaKind::PHOTO;
	return EMediaKind::UNKNOWN;
}

void CChat::ExtractMediaUrlsFromText(const char *pText, std::vector<std::string> &vOutUrls)
{
	vOutUrls.clear();
	if(!pText)
		return;

	const char *pCur = pText;
	while(*pCur)
	{
		if(!IsUrlStart(pCur))
		{
			++pCur;
			continue;
		}

		const char *pEnd = pCur;
		while(!IsTokenEnd(*pEnd))
			++pEnd;

		std::string Url(pCur, pEnd - pCur);
		while(!Url.empty() && IsTrimmedUrlChar(Url.back()))
			Url.pop_back();

		if(IsYouTubeUrl(Url))
		{
			pCur = pEnd;
			continue;
		}

		std::vector<std::string> vExpandedUrls;
		AddDirectGiphyCandidates(Url, vExpandedUrls);
		vExpandedUrls.push_back(Url);

		for(const std::string &ExpandedUrl : vExpandedUrls)
		{
			if(!IsUrlStart(ExpandedUrl.c_str()) || (int)ExpandedUrl.size() > CHAT_MEDIA_MAX_URL_LENGTH)
				continue;

			bool Exists = false;
			for(const auto &ExistingUrl : vOutUrls)
			{
				if(str_comp(ExistingUrl.c_str(), ExpandedUrl.c_str()) == 0)
				{
					Exists = true;
					break;
				}
			}
			if(!Exists)
			{
				vOutUrls.push_back(ExpandedUrl);
				if((int)vOutUrls.size() >= CHAT_MEDIA_MAX_HTML_CANDIDATES)
					return;
			}
		}

		pCur = pEnd;
	}
}

void CChat::ResetLineMedia(CLine &Line)
{
	if(Line.m_pMediaRequest)
	{
		Line.m_pMediaRequest->Abort();
		Line.m_pMediaRequest = nullptr;
	}
	if(Line.m_pMediaDecodeJob)
	{
		Line.m_pMediaDecodeJob->Abort();
		Line.m_pMediaDecodeJob = nullptr;
	}

	Line.m_OptMediaDecodedFrames.reset();
	Line.m_MediaUploadIndex = 0;
	Line.m_vMediaFrameEndMs.clear();
	Line.m_MediaTotalDurationMs = 0;
	MediaDecoder::UnloadFrames(Graphics(), Line.m_vMediaFrames);
	Line.m_MediaState = EMediaState::NONE;
	Line.m_MediaKind = EMediaKind::UNKNOWN;
	Line.m_aMediaUrl[0] = '\0';
	Line.m_vMediaCandidates.clear();
	Line.m_MediaCandidateIndex = -1;
	Line.m_MediaRetryCount = 0;
	Line.m_MediaAnimated = false;
	Line.m_MediaRevealed = false;
	Line.m_MediaWidth = 0;
	Line.m_MediaHeight = 0;
	Line.m_MediaResolveDepth = 0;
	Line.m_MediaAnimationStart = 0;
	Line.m_aMediaPreviewWidth[0] = 0.0f;
	Line.m_aMediaPreviewWidth[1] = 0.0f;
	Line.m_aMediaPreviewHeight[0] = 0.0f;
	Line.m_aMediaPreviewHeight[1] = 0.0f;
	Line.m_MediaPreviewRectValid = false;
	Line.m_MediaRetryRectValid = false;
}

void CChat::SetMediaCandidates(CLine &Line, const std::vector<std::string> &vCandidates)
{
	Line.m_vMediaCandidates.clear();
	for(const std::string &Candidate : vCandidates)
	{
		if(!IsUrlStart(Candidate.c_str()))
			continue;
		if((int)Candidate.size() > CHAT_MEDIA_MAX_URL_LENGTH)
			continue;

		bool Exists = false;
		for(const std::string &Existing : Line.m_vMediaCandidates)
		{
			if(str_comp(Existing.c_str(), Candidate.c_str()) == 0)
			{
				Exists = true;
				break;
			}
		}
		if(!Exists)
		{
			Line.m_vMediaCandidates.push_back(Candidate);
			if((int)Line.m_vMediaCandidates.size() >= CHAT_MEDIA_MAX_HTML_CANDIDATES)
				break;
		}
	}

	Line.m_MediaCandidateIndex = -1;
	Line.m_MediaKind = EMediaKind::UNKNOWN;
	Line.m_aMediaUrl[0] = '\0';
	if(!Line.m_vMediaCandidates.empty())
	{
		Line.m_MediaCandidateIndex = 0;
		str_copy(Line.m_aMediaUrl, Line.m_vMediaCandidates.front().c_str(), sizeof(Line.m_aMediaUrl));
		Line.m_MediaKind = MediaKindFromUrl(Line.m_aMediaUrl);
	}
}

void CChat::InsertMediaCandidates(CLine &Line, const std::vector<std::string> &vCandidates, int InsertIndex)
{
	if(vCandidates.empty())
		return;

	int InsertPos = std::clamp(InsertIndex, 0, (int)Line.m_vMediaCandidates.size());
	for(const std::string &Candidate : vCandidates)
	{
		if(!IsUrlStart(Candidate.c_str()))
			continue;
		if((int)Candidate.size() > CHAT_MEDIA_MAX_URL_LENGTH)
			continue;

		bool Exists = false;
		for(const std::string &Existing : Line.m_vMediaCandidates)
		{
			if(str_comp(Existing.c_str(), Candidate.c_str()) == 0)
			{
				Exists = true;
				break;
			}
		}
		if(Exists)
			continue;

		Line.m_vMediaCandidates.insert(Line.m_vMediaCandidates.begin() + InsertPos, Candidate);
		InsertPos++;
		if((int)Line.m_vMediaCandidates.size() >= CHAT_MEDIA_MAX_HTML_CANDIDATES)
			break;
	}
}

bool CChat::QueueNextMediaCandidate(CLine &Line, const char *pReason)
{
	Line.m_OptMediaDecodedFrames.reset();
	Line.m_MediaUploadIndex = 0;
	Line.m_vMediaFrameEndMs.clear();
	Line.m_MediaTotalDurationMs = 0;
	MediaDecoder::UnloadFrames(Graphics(), Line.m_vMediaFrames);
	Line.m_MediaAnimated = false;
	Line.m_MediaWidth = 0;
	Line.m_MediaHeight = 0;

	const int NextIndex = Line.m_MediaCandidateIndex + 1;
	for(int CandidateIndex = maximum(0, NextIndex); CandidateIndex < (int)Line.m_vMediaCandidates.size(); ++CandidateIndex)
	{
		if(!IsUrlStart(Line.m_vMediaCandidates[CandidateIndex].c_str()))
			continue;
		if(!IsMediaUrlAllowed(Line.m_vMediaCandidates[CandidateIndex].c_str()))
			continue;

		Line.m_MediaCandidateIndex = CandidateIndex;
		str_copy(Line.m_aMediaUrl, Line.m_vMediaCandidates[CandidateIndex].c_str(), sizeof(Line.m_aMediaUrl));
		Line.m_MediaState = EMediaState::QUEUED;
		Line.m_MediaKind = MediaKindFromUrl(Line.m_aMediaUrl);
		Line.m_pMediaRequest = nullptr;
		Line.m_pMediaDecodeJob = nullptr;
		Line.m_MediaRevealed = false;
		Line.m_MediaPreviewRectValid = false;
		Line.m_MediaRetryRectValid = false;
		Line.m_aYOffset[0] = -1.0f;
		Line.m_aYOffset[1] = -1.0f;
		if(g_Config.m_Debug)
			log_debug("chat/media", "Trying fallback candidate (%d/%d): %s (%s)", CandidateIndex + 1, (int)Line.m_vMediaCandidates.size(), Line.m_aMediaUrl, pReason ? pReason : "unknown");
		return true;
	}

	if(g_Config.m_Debug)
		log_debug("chat/media", "No fallback media candidates left (%s)", pReason ? pReason : "unknown");
	return false;
}

bool CChat::RetryMediaLine(CLine &Line)
{
	if(Line.m_MediaState != EMediaState::FAILED)
		return false;

	if(Line.m_MediaRetryCount >= CHAT_MEDIA_MAX_RETRIES)
	{
		if(g_Config.m_Debug)
			log_debug("chat/media", "Retry limit reached for message media");
		return false;
	}

	if(Line.m_vMediaCandidates.empty())
	{
		if(g_Config.m_Debug)
			log_debug("chat/media", "Cannot retry media without candidates");
		return false;
	}

	if(Line.m_pMediaRequest)
	{
		Line.m_pMediaRequest->Abort();
		Line.m_pMediaRequest = nullptr;
	}
	if(Line.m_pMediaDecodeJob)
	{
		Line.m_pMediaDecodeJob->Abort();
		Line.m_pMediaDecodeJob = nullptr;
	}

	Line.m_OptMediaDecodedFrames.reset();
	Line.m_MediaUploadIndex = 0;
	Line.m_vMediaFrameEndMs.clear();
	Line.m_MediaTotalDurationMs = 0;
	MediaDecoder::UnloadFrames(Graphics(), Line.m_vMediaFrames);

	Line.m_MediaRetryCount++;
	Line.m_MediaCandidateIndex = 0;
	str_copy(Line.m_aMediaUrl, Line.m_vMediaCandidates.front().c_str(), sizeof(Line.m_aMediaUrl));
	Line.m_MediaState = EMediaState::QUEUED;
	Line.m_MediaKind = MediaKindFromUrl(Line.m_aMediaUrl);
	Line.m_MediaAnimated = false;
	Line.m_MediaRevealed = false;
	Line.m_MediaWidth = 0;
	Line.m_MediaHeight = 0;
	Line.m_MediaResolveDepth = 0;
	Line.m_MediaAnimationStart = 0;
	Line.m_MediaPreviewRectValid = false;
	Line.m_MediaRetryRectValid = false;
	Line.m_aYOffset[0] = -1.0f;
	Line.m_aYOffset[1] = -1.0f;
	if(g_Config.m_Debug)
		log_debug("chat/media", "Retrying media preview (%d/%d): %s", Line.m_MediaRetryCount, CHAT_MEDIA_MAX_RETRIES, Line.m_aMediaUrl);
	return true;
}

void CChat::QueueMediaDownload(CLine &Line)
{
	if(!g_Config.m_BcChatMediaPreview || !AnyMediaAllowed() || Line.m_vMediaCandidates.empty())
		return;
	if(Line.m_MediaCandidateIndex < 0 || Line.m_MediaCandidateIndex >= (int)Line.m_vMediaCandidates.size())
	{
		if(!QueueNextMediaCandidate(Line, "initial candidate"))
		{
			Line.m_MediaState = EMediaState::NONE;
			return;
		}
	}
	if(Line.m_aMediaUrl[0] == '\0')
		return;
	if(!IsMediaUrlAllowed(Line.m_aMediaUrl))
	{
		if(!QueueNextMediaCandidate(Line, "media type disabled"))
			Line.m_MediaState = EMediaState::NONE;
		return;
	}
	Line.m_MediaState = EMediaState::QUEUED;
}

void CChat::StartMediaDownload(CLine &Line)
{
	if(Line.m_MediaState != EMediaState::QUEUED || Line.m_aMediaUrl[0] == '\0')
		return;
	if(!IsMediaUrlAllowed(Line.m_aMediaUrl))
	{
		if(!QueueNextMediaCandidate(Line, "media type disabled"))
			Line.m_MediaState = EMediaState::NONE;
		return;
	}
	if((int)str_length(Line.m_aMediaUrl) >= 255)
	{
		if(g_Config.m_Debug)
			log_debug("chat/media", "Skipping overlong URL (>255): %s", Line.m_aMediaUrl);
		if(!QueueNextMediaCandidate(Line, "overlong URL"))
			Line.m_MediaState = EMediaState::FAILED;
		return;
	}
	for(const char *p = Line.m_aMediaUrl; *p; ++p)
	{
		if((unsigned char)*p < 32 || *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
		{
			if(g_Config.m_Debug)
				log_debug("chat/media", "Skipping invalid URL characters: %s", Line.m_aMediaUrl);
			if(!QueueNextMediaCandidate(Line, "invalid URL characters"))
				Line.m_MediaState = EMediaState::FAILED;
			return;
		}
	}

	std::shared_ptr<CHttpRequest> pGet = HttpGet(Line.m_aMediaUrl);
	pGet->Timeout(CTimeout{8000, 0, 4096, 8});
	pGet->MaxResponseSize(CHAT_MEDIA_MAX_RESPONSE_SIZE);
	pGet->FailOnErrorStatus(false);
	pGet->LogProgress(HTTPLOG::NONE);
	Line.m_pMediaRequest = pGet;
	Line.m_MediaState = EMediaState::LOADING;
	Http()->Run(pGet);
}

bool CChat::StartMediaDecode(CLine &Line, EMediaKind MediaKind, const unsigned char *pData, size_t DataSize)
{
	if(!pData || DataSize == 0 || DataSize > (size_t)CHAT_MEDIA_MAX_RESPONSE_SIZE)
		return false;
	if(Line.m_pMediaDecodeJob)
	{
		Line.m_pMediaDecodeJob->Abort();
		Line.m_pMediaDecodeJob = nullptr;
	}

	Line.m_OptMediaDecodedFrames.reset();
	Line.m_MediaUploadIndex = 0;
	Line.m_vMediaFrameEndMs.clear();
	Line.m_MediaTotalDurationMs = 0;
	MediaDecoder::UnloadFrames(Graphics(), Line.m_vMediaFrames);
	Line.m_MediaAnimated = false;
	Line.m_MediaWidth = 0;
	Line.m_MediaHeight = 0;
	Line.m_MediaAnimationStart = 0;

	Line.m_pMediaDecodeJob = std::make_shared<CMediaDecodeJob>(Graphics(), MediaKind, pData, DataSize, Line.m_aMediaUrl);
	Engine()->AddJob(Line.m_pMediaDecodeJob);
	Line.m_MediaState = EMediaState::DECODING;
	return true;
}

bool CChat::DecodeImageWithFfmpeg(const unsigned char *pData, size_t DataSize, const char *pContextName, CLine &Line, bool DecodeAllFrames, int MaxAnimationDurationMs)
{
	if(!pData || DataSize == 0 || DataSize > (size_t)CHAT_MEDIA_MAX_RESPONSE_SIZE)
		return false;

	return MediaDecoder::DecodeImageWithFfmpeg(Graphics(), pData, DataSize, pContextName, Line.m_vMediaFrames, Line.m_MediaAnimated, Line.m_MediaWidth, Line.m_MediaHeight, Line.m_MediaAnimationStart, DecodeAllFrames, MaxAnimationDurationMs);
}

bool CChat::DecodeStaticImage(const unsigned char *pData, size_t DataSize, const char *pContextName, CLine &Line)
{
	if(!pData || DataSize == 0 || DataSize > (size_t)CHAT_MEDIA_MAX_RESPONSE_SIZE)
		return false;
	return MediaDecoder::DecodeStaticImage(Graphics(), pData, DataSize, pContextName, Line.m_vMediaFrames, Line.m_MediaAnimated, Line.m_MediaWidth, Line.m_MediaHeight, Line.m_MediaAnimationStart);
}

bool CChat::DecodeAnimatedGif(const unsigned char *pData, size_t DataSize, const char *pContextName, CLine &Line)
{
	if(!pData || DataSize == 0 || DataSize > (size_t)CHAT_MEDIA_MAX_RESPONSE_SIZE)
		return false;
	if(!MediaDecoder::DecodeAnimatedImage(Graphics(), pData, DataSize, pContextName, Line.m_vMediaFrames, Line.m_MediaAnimated, Line.m_MediaWidth, Line.m_MediaHeight, Line.m_MediaAnimationStart, CHAT_MEDIA_MAX_VIDEO_ANIMATION_MS))
		return false;
	if(Line.m_vMediaFrames.empty())
		return false;
	Line.m_MediaAnimated = Line.m_vMediaFrames.size() > 1;
	return true;
}

bool CChat::AnyMediaAllowed() const
{
	return g_Config.m_BcChatMediaPhotos || g_Config.m_BcChatMediaGifs;
}

bool CChat::IsMediaKindAllowed(EMediaKind Kind) const
{
	switch(Kind)
	{
	case EMediaKind::PHOTO:
		return g_Config.m_BcChatMediaPhotos;
	case EMediaKind::ANIMATED:
	case EMediaKind::VIDEO:
		return g_Config.m_BcChatMediaGifs;
	case EMediaKind::UNKNOWN:
	default:
		return AnyMediaAllowed();
	}
}

bool CChat::IsMediaUrlAllowed(const char *pUrl) const
{
	return IsMediaKindAllowed(MediaKindFromUrl(pUrl)) && IsAllowedChatMediaUrl(pUrl);
}

bool CChat::HasAllowedMediaCandidates(const CLine &Line) const
{
	for(const std::string &Candidate : Line.m_vMediaCandidates)
	{
		if(IsMediaUrlAllowed(Candidate.c_str()))
			return true;
	}
	return Line.m_aMediaUrl[0] != '\0' && IsMediaUrlAllowed(Line.m_aMediaUrl);
}

bool CChat::ShouldDisplayMediaSlot(const CLine &Line) const
{
	if(!g_Config.m_BcChatMediaPreview || !AnyMediaAllowed())
		return false;
	if(Line.m_MediaState == EMediaState::FAILED)
		return false;
	if((Line.m_MediaState == EMediaState::READY || Line.m_MediaState == EMediaState::LOADING || Line.m_MediaState == EMediaState::DECODING || Line.m_MediaState == EMediaState::QUEUED) && Line.m_MediaKind != EMediaKind::UNKNOWN)
		return IsMediaKindAllowed(Line.m_MediaKind);
	return HasAllowedMediaCandidates(Line);
}

bool CChat::ShouldHideMediaPreview(const CLine &Line) const
{
	return m_HideMediaByBind && !Line.m_MediaRevealed && ShouldDisplayMediaSlot(Line);
}

void CChat::ResetHiddenMediaReveals()
{
	for(auto &Line : m_aLines)
	{
		Line.m_MediaRevealed = false;
		Line.m_MediaPreviewRectValid = false;
		Line.m_MediaRetryRectValid = false;
		Line.m_aYOffset[0] = -1.0f;
		Line.m_aYOffset[1] = -1.0f;
	}
}

void CChat::UpdateMediaDownloads()
{
	if(!g_Config.m_BcChatMediaPreview || !AnyMediaAllowed())
	{
		if(m_MediaViewerOpen)
			CloseMediaViewer();
		return;
	}

	int ActiveDownloads = 0;
	for(auto &Line : m_aLines)
	{
		if(Line.m_MediaState == EMediaState::LOADING && Line.m_pMediaRequest && !Line.m_pMediaRequest->Done())
			ActiveDownloads++;
	}

	const auto FailLine = [this](CLine &Line, bool SuppressedBySettings) {
		if(SuppressedBySettings)
		{
			Line.m_OptMediaDecodedFrames.reset();
			Line.m_MediaUploadIndex = 0;
			Line.m_vMediaFrameEndMs.clear();
			Line.m_MediaTotalDurationMs = 0;
			MediaDecoder::UnloadFrames(Graphics(), Line.m_vMediaFrames);
			Line.m_MediaState = EMediaState::NONE;
			Line.m_MediaAnimated = false;
			Line.m_MediaWidth = 0;
			Line.m_MediaHeight = 0;
			Line.m_MediaRetryRectValid = false;
			Line.m_MediaPreviewRectValid = false;
			Line.m_aYOffset[0] = -1.0f;
			Line.m_aYOffset[1] = -1.0f;
			return;
		}

		Line.m_OptMediaDecodedFrames.reset();
		Line.m_MediaUploadIndex = 0;
		Line.m_vMediaFrameEndMs.clear();
		Line.m_MediaTotalDurationMs = 0;
		MediaDecoder::UnloadFrames(Graphics(), Line.m_vMediaFrames);
		Line.m_MediaState = EMediaState::FAILED;
		Line.m_MediaAnimated = false;
		Line.m_MediaWidth = 0;
		Line.m_MediaHeight = 0;
		Line.m_MediaRetryRectValid = false;
		Line.m_aYOffset[0] = -1.0f;
		Line.m_aYOffset[1] = -1.0f;
		if(g_Config.m_Debug)
		{
			if(Line.m_vMediaCandidates.empty())
				log_debug("chat/media", "Media failed: no candidates");
			else
				log_debug("chat/media", "Media failed after exhausting %d candidates", (int)Line.m_vMediaCandidates.size());
		}
	};

	int CompletedRequestsThisFrame = 0;
	for(auto &Line : m_aLines)
	{
		if(CompletedRequestsThisFrame >= CHAT_MEDIA_MAX_COMPLETED_DECODE_PER_FRAME)
			break;
		if(Line.m_MediaState != EMediaState::LOADING || !Line.m_pMediaRequest || !Line.m_pMediaRequest->Done())
			continue;

		bool StartedDecode = false;
		bool SuppressedBySettings = false;
		const char *pFailureReason = "download failed";
		const bool HttpDone = Line.m_pMediaRequest->State() == EHttpState::DONE;
		const int StatusCode = HttpDone ? Line.m_pMediaRequest->StatusCode() : -1;
		if(HttpDone && StatusCode >= 200 && StatusCode < 400)
		{
			unsigned char *pResult = nullptr;
			size_t ResultSize = 0;
			Line.m_pMediaRequest->Result(&pResult, &ResultSize);
			if(pResult && ResultSize > 0)
			{
				MediaDecoder::UnloadFrames(Graphics(), Line.m_vMediaFrames);

				if(Line.m_MediaResolveDepth < CHAT_MEDIA_MAX_RESOLVE_DEPTH)
				{
					std::vector<std::string> vExtractedUrls;
					ExtractMediaUrlsFromHtmlDocument(pResult, ResultSize, Line.m_aMediaUrl, vExtractedUrls);
					const int CandidateCountBefore = (int)Line.m_vMediaCandidates.size();
					InsertMediaCandidates(Line, vExtractedUrls, Line.m_MediaCandidateIndex + 1);
					if((int)Line.m_vMediaCandidates.size() > CandidateCountBefore)
					{
						Line.m_MediaResolveDepth++;
						if(g_Config.m_Debug)
							log_debug("chat/media", "Extracted %d fallback candidates from HTML: %s", (int)Line.m_vMediaCandidates.size() - CandidateCountBefore, Line.m_aMediaUrl);
					}
				}

				const bool IsHtmlResponse = IsLikelyHtmlDocument(pResult, ResultSize);
				if(IsHtmlResponse)
				{
					pFailureReason = "html response";
				}
				else
				{
					const std::string Ext = ExtractUrlExtensionLower(Line.m_aMediaUrl);
					const bool IsGif = IsGifSignature(pResult, ResultSize) || Ext == "gif";
					const bool IsVideoCandidate = IsLikelyVideoExtension(Ext) || IsVideoPayloadSignature(pResult, ResultSize);
					const bool IsImageCandidate = IsLikelyImageExtension(Ext) || IsImagePayloadSignature(pResult, ResultSize);
					const bool IsAnimatedImageCandidate = IsLikelyAnimatedImageExtension(Ext) && !IsVideoCandidate;
					const bool LooksLikeMediaPayload = IsGif || IsVideoCandidate || IsImageCandidate;
					EMediaKind MediaKind = EMediaKind::UNKNOWN;
					if(IsGif || IsAnimatedImageCandidate)
						MediaKind = EMediaKind::ANIMATED;
					else if(IsVideoCandidate || (!IsImageCandidate && Ext.empty()))
						MediaKind = EMediaKind::VIDEO;
					else if(IsImageCandidate)
						MediaKind = EMediaKind::PHOTO;
					Line.m_MediaKind = MediaKind;
					if(!LooksLikeMediaPayload)
					{
						pFailureReason = "unsupported payload";
					}
					else if(ResultSize < 16)
					{
						pFailureReason = "payload too small";
					}
					else
					{
						if(!IsMediaKindAllowed(MediaKind))
						{
							SuppressedBySettings = true;
							pFailureReason = "media type disabled";
						}
						else
						{
							StartedDecode = StartMediaDecode(Line, MediaKind, pResult, ResultSize);
							if(!StartedDecode)
								pFailureReason = "decode job failed";
						}
					}
				}
			}
			else if(g_Config.m_Debug)
			{
				pFailureReason = "empty response";
				log_debug("chat/media", "Empty HTTP response for media URL: %s", Line.m_aMediaUrl);
			}
		}
		else if(g_Config.m_Debug)
		{
			pFailureReason = "http failure";
			log_debug("chat/media", "HTTP request failed for media URL (state=%d, status=%d): %s", (int)Line.m_pMediaRequest->State(), StatusCode, Line.m_aMediaUrl);
		}

		Line.m_pMediaRequest = nullptr;
		ActiveDownloads = maximum(0, ActiveDownloads - 1);
		CompletedRequestsThisFrame++;

		if(StartedDecode)
			continue;

		if(QueueNextMediaCandidate(Line, pFailureReason))
			continue;

		FailLine(Line, SuppressedBySettings);
	}

	int CompletedUploadsThisFrame = 0;
	for(auto &Line : m_aLines)
	{
		if(CompletedUploadsThisFrame >= CHAT_MEDIA_MAX_COMPLETED_DECODE_PER_FRAME)
			break;
		if(Line.m_MediaState != EMediaState::DECODING || !Line.m_pMediaDecodeJob || !Line.m_pMediaDecodeJob->Done())
			continue;

		bool Success = false;
		const char *pFailureReason = "decode failed";
		if(Line.m_pMediaDecodeJob->State() == IJob::STATE_DONE && Line.m_pMediaDecodeJob->Success() && !Line.m_pMediaDecodeJob->DecodedFrames().Empty())
		{
			const int Width = Line.m_pMediaDecodeJob->DecodedFrames().m_Width;
			const int Height = Line.m_pMediaDecodeJob->DecodedFrames().m_Height;
			Line.m_OptMediaDecodedFrames.emplace(std::move(Line.m_pMediaDecodeJob->DecodedFrames()));
			Line.m_MediaUploadIndex = 0;
			Line.m_MediaWidth = Width;
			Line.m_MediaHeight = Height;
			Line.m_MediaAnimated = false;
			Line.m_MediaAnimationStart = 0;
			Success = true;
		}
		else if(g_Config.m_Debug)
		{
			log_debug("chat/media", "Media decode job failed: %s", Line.m_aMediaUrl);
		}

		Line.m_pMediaDecodeJob = nullptr;
		CompletedUploadsThisFrame++;

		if(Success)
			continue;

		Line.m_OptMediaDecodedFrames.reset();
		Line.m_MediaUploadIndex = 0;
		if(QueueNextMediaCandidate(Line, pFailureReason))
			continue;

		FailLine(Line, false);
	}

	const auto ClampFrameDurationMs = [](int DurationMs) -> int {
		constexpr int MediaFpsCap = 120;
		constexpr int MediaMinFrameMs = (1000 + MediaFpsCap - 1) / MediaFpsCap; // ceil(1000/fps)
		constexpr int MediaMaxFrameMs = 10000;
		return std::clamp(DurationMs, MediaMinFrameMs, MediaMaxFrameMs);
	};

	auto UploadDecodedFramesStep = [&](CLine &Line, int MaxFramesToUpload, int64_t TimeBudgetUs, int &UploadedFramesOut, bool &FinishedOut) -> bool {
		UploadedFramesOut = 0;
		FinishedOut = false;
		if(!Line.m_OptMediaDecodedFrames.has_value())
			return true;
		SMediaDecodedFrames &DecodedFrames = *Line.m_OptMediaDecodedFrames;
		if(DecodedFrames.m_vFrames.empty())
		{
			FinishedOut = true;
			return false;
		}

		const int64_t Start = time_get();
		while(Line.m_MediaUploadIndex < (int)DecodedFrames.m_vFrames.size())
		{
			if(UploadedFramesOut >= MaxFramesToUpload)
				break;
			if(TimeBudgetUs > 0)
			{
				const int64_t ElapsedUs = ((time_get() - Start) * 1000000) / time_freq();
				if(ElapsedUs >= TimeBudgetUs)
					break;
			}

			SMediaRawFrame &RawFrame = DecodedFrames.m_vFrames[Line.m_MediaUploadIndex];
			SMediaFrame Frame;
			Frame.m_DurationMs = RawFrame.m_DurationMs;
			Frame.m_Texture = Graphics()->LoadTextureRawMove(RawFrame.m_Image, 0, Line.m_aMediaUrl);
			if(!Frame.m_Texture.IsValid())
				return false;
			Line.m_vMediaFrames.push_back(Frame);
			Line.m_MediaUploadIndex++;
			UploadedFramesOut++;
		}

		FinishedOut = Line.m_MediaUploadIndex >= (int)DecodedFrames.m_vFrames.size();
		return true;
	};

	int UploadedTexturesThisFrame = 0;
	const int64_t UploadStart = time_get();
	for(auto &Line : m_aLines)
	{
		if(!Line.m_Initialized || !Line.m_OptMediaDecodedFrames.has_value())
			continue;
		if(UploadedTexturesThisFrame >= CHAT_MEDIA_MAX_TEXTURE_UPLOADS_PER_FRAME)
			break;

		const int64_t ElapsedUs = ((time_get() - UploadStart) * 1000000) / time_freq();
		const int64_t RemainingUs = CHAT_MEDIA_TEXTURE_UPLOAD_BUDGET_US - ElapsedUs;
		if(RemainingUs <= 0)
			break;

		const int FramesBudget = CHAT_MEDIA_MAX_TEXTURE_UPLOADS_PER_FRAME - UploadedTexturesThisFrame;
		int UploadedNow = 0;
		bool Finished = false;
		const bool Success = UploadDecodedFramesStep(Line, FramesBudget, RemainingUs, UploadedNow, Finished);
		UploadedTexturesThisFrame += UploadedNow;

		if(!Success)
		{
			Line.m_OptMediaDecodedFrames.reset();
			Line.m_MediaUploadIndex = 0;
			Line.m_vMediaFrameEndMs.clear();
			Line.m_MediaTotalDurationMs = 0;
			MediaDecoder::UnloadFrames(Graphics(), Line.m_vMediaFrames);
			if(QueueNextMediaCandidate(Line, "upload failed"))
				continue;
			FailLine(Line, false);
			continue;
		}

		if(!Line.m_vMediaFrames.empty() && Line.m_MediaState != EMediaState::READY)
		{
			Line.m_MediaState = EMediaState::READY;
			Line.m_MediaRetryRectValid = false;
			Line.m_MediaPreviewRectValid = false;
			Line.m_aYOffset[0] = -1.0f;
			Line.m_aYOffset[1] = -1.0f;
		}

		if(Finished)
		{
			Line.m_OptMediaDecodedFrames.reset();
			Line.m_MediaUploadIndex = 0;
			Line.m_vMediaFrameEndMs.clear();
			Line.m_MediaTotalDurationMs = 0;
			if(Line.m_vMediaFrames.size() > 1)
			{
				Line.m_vMediaFrameEndMs.reserve(Line.m_vMediaFrames.size());
				int TotalDuration = 0;
				for(const auto &Frame : Line.m_vMediaFrames)
				{
					TotalDuration += ClampFrameDurationMs(Frame.m_DurationMs);
					Line.m_vMediaFrameEndMs.push_back(TotalDuration);
				}
				Line.m_MediaTotalDurationMs = TotalDuration;
				Line.m_MediaAnimated = TotalDuration > 0;
				if(Line.m_MediaAnimated)
					Line.m_MediaAnimationStart = time_get();
			}
			else
			{
				Line.m_MediaAnimated = false;
				Line.m_MediaAnimationStart = 0;
			}
		}
	}

	for(auto &Line : m_aLines)
	{
		if(ActiveDownloads >= CHAT_MEDIA_MAX_CONCURRENT_DOWNLOADS)
			break;
		if(Line.m_MediaState == EMediaState::QUEUED)
		{
			StartMediaDownload(Line);
			if(Line.m_MediaState == EMediaState::LOADING)
				ActiveDownloads++;
		}
	}
}

bool CChat::ValidateMediaViewerLine() const
{
	if(!m_MediaViewerOpen)
		return false;
	if(m_MediaViewerLineIndex < 0 || m_MediaViewerLineIndex >= MAX_LINES)
		return false;
	const CLine &Line = m_aLines[m_MediaViewerLineIndex];
	return Line.m_Initialized && Line.m_MediaState == EMediaState::READY && !Line.m_vMediaFrames.empty();
}

void CChat::CloseMediaViewer()
{
	m_MediaViewerOpen = false;
	m_MediaViewerLineIndex = -1;
	m_MediaViewerZoom = 1.0f;
	m_MediaViewerPan = vec2(0.0f, 0.0f);
	m_MediaViewerDragging = false;
}

void CChat::OpenMediaViewer(int LineIndex)
{
	if(!g_Config.m_BcChatMediaViewer)
		return;

	if(LineIndex < 0 || LineIndex >= MAX_LINES)
		return;
	CLine &Line = m_aLines[LineIndex];
	if(!Line.m_Initialized || Line.m_MediaState != EMediaState::READY || Line.m_vMediaFrames.empty())
		return;
	m_MediaViewerOpen = true;
	m_MediaViewerLineIndex = LineIndex;
	m_MediaViewerZoom = 1.0f;
	m_MediaViewerPan = vec2(0.0f, 0.0f);
	m_MediaViewerDragging = false;
	m_MediaViewerLastClickTime = 0;
}

bool CChat::GetCurrentFrameTexture(CLine &Line, IGraphics::CTextureHandle &Texture) const
{
	if(Line.m_vMediaFrames.empty())
		return false;
	if(!Line.m_MediaAnimated || Line.m_vMediaFrames.size() == 1)
	{
		Texture = Line.m_vMediaFrames.front().m_Texture;
		return Texture.IsValid();
	}

	if(Line.m_MediaTotalDurationMs <= 0 || (int)Line.m_vMediaFrameEndMs.size() != (int)Line.m_vMediaFrames.size())
	{
		// Fallback (should be rare, e.g. old state after config toggle).
		return MediaDecoder::GetCurrentFrameTexture(Line.m_vMediaFrames, Line.m_MediaAnimated, Line.m_MediaAnimationStart, Texture);
	}

	const int64_t ElapsedMs = ((time_get() - Line.m_MediaAnimationStart) * 1000) / time_freq();
	const int Offset = (int)(ElapsedMs % (int64_t)Line.m_MediaTotalDurationMs);
	const auto It = std::upper_bound(Line.m_vMediaFrameEndMs.begin(), Line.m_vMediaFrameEndMs.end(), Offset);
	const int Index = It == Line.m_vMediaFrameEndMs.end() ? 0 : (int)(It - Line.m_vMediaFrameEndMs.begin());
	Texture = Line.m_vMediaFrames[Index].m_Texture;
	return Texture.IsValid();
}

vec2 CChat::ChatMousePos() const
{
	const float Height = 300.0f;
	const float Width = Height * Graphics()->ScreenAspect();
	const vec2 WindowSize(maximum(1.0f, (float)Graphics()->WindowWidth()), maximum(1.0f, (float)Graphics()->WindowHeight()));
	const vec2 UiMousePos = Ui()->UpdatedMousePos() * vec2(Ui()->Screen()->w, Ui()->Screen()->h) / WindowSize;
	const vec2 UiToChatScale(Width / Ui()->Screen()->w, Height / Ui()->Screen()->h);
	return UiMousePos * UiToChatScale;
}

std::string CChat::MediaPlaceholderText(const CLine &Line) const
{
	const char *pUrl = Line.m_aMediaUrl;
	if(pUrl[0] == '\0' && !Line.m_vMediaCandidates.empty())
		pUrl = Line.m_vMediaCandidates.front().c_str();

	const std::string Ext = ExtractUrlExtensionLower(pUrl);
	if(IsLikelyVideoExtension(Ext))
		return "Video";
	if(Line.m_MediaAnimated || IsLikelyAnimatedImageExtension(Ext))
		return "GIF";
	if(IsLikelyImageExtension(Ext))
		return "Photo";
	return "Media";
}

std::string CChat::BuildVisibleMessageText(const CLine &Line, bool UseMediaLabelWhenEmpty) const
{
	if(!ShouldDisplayMediaSlot(Line))
		return Line.m_aText;

	std::string Result;
	bool RemovedUrl = false;
	for(const char *pCur = Line.m_aText; *pCur;)
	{
		if(IsUrlStart(pCur))
		{
			RemovedUrl = true;
			while(*pCur && !IsTokenEnd(*pCur))
				++pCur;
			continue;
		}

		Result.push_back(*pCur);
		++pCur;
	}

	std::string Compacted;
	Compacted.reserve(Result.size());
	bool PrevWhitespace = false;
	for(char c : Result)
	{
		if(std::isspace((unsigned char)c))
		{
			if(!PrevWhitespace)
				Compacted.push_back(' ');
			PrevWhitespace = true;
		}
		else
		{
			Compacted.push_back(c);
			PrevWhitespace = false;
		}
	}
	TrimAsciiWhitespace(Compacted);

	if(Compacted.empty() && RemovedUrl && UseMediaLabelWhenEmpty)
		return MediaPlaceholderText(Line);

	return Compacted;
}

void CChat::ConToggleHideChatMedia(IConsole::IResult *pResult, void *pUserData)
{
	CChat *pThis = static_cast<CChat *>(pUserData);
	(void)pResult;
	pThis->m_HideMediaByBind = !pThis->m_HideMediaByBind;
	pThis->CloseMediaViewer();
	pThis->ResetHiddenMediaReveals();
	pThis->RebuildChat();
	pThis->Echo(pThis->m_HideMediaByBind ? "Chat media hidden" : "Chat media visible");
}

std::string CChat::BuildPlainTextLine(const CLine &Line) const
{
	char aClientId[16] = "";
	if(g_Config.m_ClShowIds && Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
	{
		GameClient()->FormatClientId(Line.m_ClientId, aClientId, EClientIdFormat::INDENT_AUTO);
	}

	char aCount[12] = "";
	if(Line.m_TimesRepeated > 0)
	{
		if(Line.m_ClientId < 0)
			str_format(aCount, sizeof(aCount), "[%d] ", Line.m_TimesRepeated + 1);
		else
			str_format(aCount, sizeof(aCount), " [%d]", Line.m_TimesRepeated + 1);
	}

	bool TextHiddenByStreamer = false;
	std::string VisibleTextStorage;
	const char *pText = Line.m_aText;
	if(Config()->m_ClStreamerMode && Line.m_ClientId == SERVER_MSG)
	{
		if(str_startswith(Line.m_aText, "Team save in progress. You'll be able to load with '/load ") && str_endswith(Line.m_aText, "'"))
		{
			TextHiddenByStreamer = true;
			pText = "Team save in progress. You'll be able to load with '/load *** *** ***'";
		}
		else if(str_startswith(Line.m_aText, "Team save in progress. You'll be able to load with '/load") && str_endswith(Line.m_aText, "if it fails"))
		{
			TextHiddenByStreamer = true;
			pText = "Team save in progress. You'll be able to load with '/load *** *** ***' if save is successful or with '/load *** *** ***' if it fails";
		}
		else if(str_startswith(Line.m_aText, "Team successfully saved by ") && str_endswith(Line.m_aText, " to continue"))
		{
			TextHiddenByStreamer = true;
			pText = "Team successfully saved by ***. Use '/load *** *** ***' to continue";
		}
	}
	else
	{
		VisibleTextStorage = BuildVisibleMessageText(Line, true);
		pText = VisibleTextStorage.c_str();
	}

	const CColoredParts ColoredParts(pText, Line.m_ClientId == CLIENT_MSG);
	pText = ColoredParts.Text();

	const char *pTranslatedError = nullptr;
	const char *pTranslatedText = nullptr;
	const char *pTranslatedLanguage = nullptr;
	if(Line.m_pTranslateResponse != nullptr && Line.m_pTranslateResponse->m_Text[0])
	{
		if(TextHiddenByStreamer)
			pTranslatedError = TCLocalize("Translated text hidden due to streamer mode");
		else if(Line.m_pTranslateResponse->m_Error)
			pTranslatedError = Line.m_pTranslateResponse->m_Text;
		else
		{
			pTranslatedText = Line.m_pTranslateResponse->m_Text;
			if(Line.m_pTranslateResponse->m_Language[0] != '\0')
				pTranslatedLanguage = Line.m_pTranslateResponse->m_Language;
		}
	}

	std::string Result;
	if(Line.m_ClientId >= 0 && Line.m_aName[0] != '\0' && Line.m_Friend && g_Config.m_ClMessageFriend)
		Result += "♥ ";
	Result += aClientId;
	Result += Line.m_aName;
	Result += aCount;
	if(Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
		Result += ": ";
	if(pTranslatedText)
	{
		Result += pTranslatedText;
		if(pTranslatedLanguage)
		{
			Result += " [";
			Result += pTranslatedLanguage;
			Result += "]";
		}
	}
	else if(pTranslatedError)
	{
		Result += pText;
		Result += "\n";
		Result += pTranslatedError;
	}
	else
	{
		Result += pText;
	}
	return Result;
}

void CChat::RenderTextLine(CLine &Line, float y, float FontSize, float LineWidth, float TextBegin, float RealMsgPaddingTee, float RealMsgPaddingY, bool IsScoreBoardOpen, float Blend, std::string *pSelectionString)
{
	char aClientId[16] = "";
	if(g_Config.m_ClShowIds && Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
	{
		GameClient()->FormatClientId(Line.m_ClientId, aClientId, EClientIdFormat::INDENT_AUTO);
	}

	char aCount[12] = "";
	if(Line.m_TimesRepeated > 0)
	{
		if(Line.m_ClientId < 0)
			str_format(aCount, sizeof(aCount), "[%d] ", Line.m_TimesRepeated + 1);
		else
			str_format(aCount, sizeof(aCount), " [%d]", Line.m_TimesRepeated + 1);
	}

	bool TextHiddenByStreamer = false;
	std::string VisibleTextStorage;
	const char *pText = Line.m_aText;
	if(Config()->m_ClStreamerMode && Line.m_ClientId == SERVER_MSG)
	{
		if(str_startswith(Line.m_aText, "Team save in progress. You'll be able to load with '/load ") && str_endswith(Line.m_aText, "'"))
		{
			TextHiddenByStreamer = true;
			pText = "Team save in progress. You'll be able to load with '/load *** *** ***'";
		}
		else if(str_startswith(Line.m_aText, "Team save in progress. You'll be able to load with '/load") && str_endswith(Line.m_aText, "if it fails"))
		{
			TextHiddenByStreamer = true;
			pText = "Team save in progress. You'll be able to load with '/load *** *** ***' if save is successful or with '/load *** *** ***' if it fails";
		}
		else if(str_startswith(Line.m_aText, "Team successfully saved by ") && str_endswith(Line.m_aText, " to continue"))
		{
			TextHiddenByStreamer = true;
			pText = "Team successfully saved by ***. Use '/load *** *** ***' to continue";
		}
	}
	else
	{
		VisibleTextStorage = BuildVisibleMessageText(Line, false);
		pText = VisibleTextStorage.c_str();
	}

	const CColoredParts ColoredParts(pText, Line.m_ClientId == CLIENT_MSG);
	pText = ColoredParts.Text();

	std::optional<ColorRGBA> CustomColor;
	if(!ColoredParts.Colors().empty() && ColoredParts.Colors()[0].m_Index == 0)
		CustomColor = ColoredParts.Colors()[0].m_Color;

	const char *pTranslatedError = nullptr;
	const char *pTranslatedText = nullptr;
	const char *pTranslatedLanguage = nullptr;
	if(Line.m_pTranslateResponse != nullptr && Line.m_pTranslateResponse->m_Text[0])
	{
		if(TextHiddenByStreamer)
			pTranslatedError = TCLocalize("Translated text hidden due to streamer mode");
		else if(Line.m_pTranslateResponse->m_Error)
			pTranslatedError = Line.m_pTranslateResponse->m_Text;
		else
		{
			pTranslatedText = Line.m_pTranslateResponse->m_Text;
			if(Line.m_pTranslateResponse->m_Language[0] != '\0')
				pTranslatedLanguage = Line.m_pTranslateResponse->m_Language;
		}
	}

	CTextCursor LineCursor;
	LineCursor.SetPosition(vec2(TextBegin, y + RealMsgPaddingY / 2.0f));
	LineCursor.m_FontSize = FontSize;
	LineCursor.m_LineWidth = LineWidth;
	if(m_MouseIsPress || m_HasSelection || m_WantsSelectionCopy)
	{
		LineCursor.m_CalculateSelectionMode = TEXT_CURSOR_SELECTION_MODE_CALCULATE;
		LineCursor.m_PressMouse = m_MousePress;
		LineCursor.m_ReleaseMouse = m_MouseRelease;
	}

	if(Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
	{
		LineCursor.m_X += RealMsgPaddingTee;
		if(Line.m_Friend && g_Config.m_ClMessageFriend)
		{
			TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageFriendColor)).WithAlpha(Blend));
			TextRender()->TextEx(&LineCursor, "♥ ");
		}
	}

	ColorRGBA NameColor;
	if(CustomColor)
		NameColor = *CustomColor;
	else if(Line.m_ClientId == SERVER_MSG)
		NameColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageSystemColor));
	else if(Line.m_ClientId == CLIENT_MSG)
		NameColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageClientColor));
	else if(Line.m_ClientId >= 0 && g_Config.m_TcWarList && g_Config.m_TcWarListChat && GameClient()->m_WarList.GetAnyWar(Line.m_ClientId))
		NameColor = GameClient()->m_WarList.GetPriorityColor(Line.m_ClientId);
	else if(Line.m_Team)
		NameColor = CalculateNameColor(ColorHSLA(g_Config.m_ClMessageTeamColor));
	else if(Line.m_NameColor == TEAM_RED)
		NameColor = ColorRGBA(1.0f, 0.5f, 0.5f, Blend);
	else if(Line.m_NameColor == TEAM_BLUE)
		NameColor = ColorRGBA(0.7f, 0.7f, 1.0f, Blend);
	else if(Line.m_NameColor == TEAM_SPECTATORS)
		NameColor = ColorRGBA(0.75f, 0.5f, 0.75f, Blend);
	else if(Line.m_ClientId >= 0 && g_Config.m_ClChatTeamColors && GameClient()->m_Teams.Team(Line.m_ClientId))
		NameColor = GameClient()->GetDDTeamColor(GameClient()->m_Teams.Team(Line.m_ClientId), 0.75f);
	else
		NameColor = ColorRGBA(0.8f, 0.8f, 0.8f, 1.0f);
	NameColor.a *= Blend;

	TextRender()->TextColor(NameColor);
	TextRender()->TextEx(&LineCursor, aClientId);
	TextRender()->TextEx(&LineCursor, Line.m_aName);
	if(Line.m_TimesRepeated > 0)
	{
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.3f * Blend);
		TextRender()->TextEx(&LineCursor, aCount);
	}
	if(Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
	{
		TextRender()->TextColor(NameColor);
		TextRender()->TextEx(&LineCursor, ": ");
	}

	ColorRGBA Color;
	if(CustomColor)
		Color = *CustomColor;
	else if(Line.m_ClientId == SERVER_MSG)
		Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageSystemColor));
	else if(Line.m_ClientId == CLIENT_MSG)
		Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageClientColor));
	else if(Line.m_Highlighted)
		Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageHighlightColor));
	else if(Line.m_Team)
		Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageTeamColor));
	else
		Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageColor));
	Color.a *= Blend;
	TextRender()->TextColor(Color);

	const float PrefixWidth = LineCursor.m_LongestLineWidth;
	LineCursor.m_LongestLineWidth = 0.0f;
	if(!IsScoreBoardOpen && !g_Config.m_ClChatOld)
	{
		LineCursor.m_StartX = LineCursor.m_X;
		LineCursor.m_LineWidth -= PrefixWidth;
	}

	if(pTranslatedText)
	{
		TextRender()->TextEx(&LineCursor, pTranslatedText);
		if(pTranslatedLanguage)
		{
			ColorRGBA ColorLang = Color;
			ColorLang.r *= 0.8f;
			ColorLang.g *= 0.8f;
			ColorLang.b *= 0.8f;
			TextRender()->TextColor(ColorLang);
			TextRender()->TextEx(&LineCursor, " [");
			TextRender()->TextEx(&LineCursor, pTranslatedLanguage);
			TextRender()->TextEx(&LineCursor, "]");
		}
	}
	else if(pTranslatedError)
	{
		TextRender()->TextColor(Color);
		TextRender()->TextEx(&LineCursor, pText);
		ColorRGBA ColorSub = Color;
		ColorSub.r = 0.7f;
		ColorSub.g = 0.6f;
		ColorSub.b = 0.6f;
		TextRender()->TextColor(ColorSub);
		TextRender()->TextEx(&LineCursor, "\n");
		LineCursor.m_FontSize *= 0.8f;
		TextRender()->TextEx(&LineCursor, pTranslatedError);
		LineCursor.m_FontSize /= 0.8f;
	}
	else
	{
		LineCursor.m_vColorSplits = {};
		ColoredParts.AddSplitsToCursor(LineCursor);
		TextRender()->TextEx(&LineCursor, pText);
		LineCursor.m_vColorSplits.clear();
	}

	if((m_MouseIsPress || m_HasSelection || m_WantsSelectionCopy) && LineCursor.m_SelectionStart >= 0 && LineCursor.m_SelectionEnd >= 0 && LineCursor.m_SelectionStart != LineCursor.m_SelectionEnd)
	{
		m_HasSelection = true;
		if(pSelectionString != nullptr)
		{
			const std::string PlainText = BuildPlainTextLine(Line);
			const int SelectionMin = minimum(LineCursor.m_SelectionStart, LineCursor.m_SelectionEnd);
			const int SelectionMax = maximum(LineCursor.m_SelectionStart, LineCursor.m_SelectionEnd);
			const size_t OffUTF8Start = str_utf8_offset_chars_to_bytes(PlainText.c_str(), SelectionMin);
			const size_t OffUTF8End = str_utf8_offset_chars_to_bytes(PlainText.c_str(), SelectionMax);
			const bool HasNewLine = !pSelectionString->empty();
			pSelectionString->insert(0, PlainText.substr(OffUTF8Start, OffUTF8End - OffUTF8Start) + (HasNewLine ? "\n" : ""));
		}
	}

	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

bool CChat::GetMediaViewerRect(const CLine &Line, float ScreenWidth, float ScreenHeight, float &x, float &y, float &w, float &h) const
{
	if(Line.m_MediaWidth <= 0 || Line.m_MediaHeight <= 0)
		return false;

	const float Margin = FontSize() * 2.0f;
	const float MaxW = maximum(16.0f, ScreenWidth - Margin * 2.0f);
	const float MaxH = maximum(16.0f, ScreenHeight - Margin * 2.0f);
	const float FitScale = minimum(MaxW / (float)Line.m_MediaWidth, MaxH / (float)Line.m_MediaHeight);
	const float BaseW = (float)Line.m_MediaWidth * FitScale;
	const float BaseH = (float)Line.m_MediaHeight * FitScale;

	w = BaseW * m_MediaViewerZoom;
	h = BaseH * m_MediaViewerZoom;
	x = (ScreenWidth - w) / 2.0f + m_MediaViewerPan.x;
	y = (ScreenHeight - h) / 2.0f + m_MediaViewerPan.y;
	return true;
}

void CChat::ClampMediaViewerPan(const CLine &Line, float ScreenWidth, float ScreenHeight)
{
	float x = 0.0f;
	float y = 0.0f;
	float w = 0.0f;
	float h = 0.0f;
	if(!GetMediaViewerRect(Line, ScreenWidth, ScreenHeight, x, y, w, h))
		return;

	float ClampedX = x;
	float ClampedY = y;
	if(w <= ScreenWidth)
		ClampedX = (ScreenWidth - w) / 2.0f;
	else
		ClampedX = maximum(ScreenWidth - w, minimum(0.0f, ClampedX));
	if(h <= ScreenHeight)
		ClampedY = (ScreenHeight - h) / 2.0f;
	else
		ClampedY = maximum(ScreenHeight - h, minimum(0.0f, ClampedY));

	m_MediaViewerPan.x += ClampedX - x;
	m_MediaViewerPan.y += ClampedY - y;
}

bool CChat::OnInput(const IInput::CEvent &Event)
{
	const bool ChatInputActive = m_Mode != MODE_NONE;
	const bool ChatInteractionActive = ChatInputActive || m_Show;

	if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_MOUSE_1 && g_Config.m_BcChatMediaPreview &&
		(Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK))
	{
		bool HasRetryTargets = false;
		for(const auto &Line : m_aLines)
		{
			if(Line.m_MediaRetryRectValid && Line.m_MediaState == EMediaState::FAILED)
			{
				HasRetryTargets = true;
				break;
			}
		}
		if(HasRetryTargets)
		{
			const vec2 MousePos = ChatMousePos();
			for(auto &Line : m_aLines)
			{
				if(!Line.m_MediaRetryRectValid || Line.m_MediaState != EMediaState::FAILED)
					continue;

				const SRenderRect &Rect = Line.m_MediaRetryRect;
				if(MousePos.x >= Rect.m_X && MousePos.x <= Rect.m_X + Rect.m_W &&
					MousePos.y >= Rect.m_Y && MousePos.y <= Rect.m_Y + Rect.m_H)
				{
					if(RetryMediaLine(Line))
						return true;
				}
			}
		}
	}

	if(!ChatInteractionActive)
		return false;

	if(ChatInputActive && (Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_ESCAPE && Ui()->IsPopupOpen())
	{
		Ui()->ClosePopupMenus();
		return true;
	}

	if(ChatInputActive && Event.m_Key == KEY_MOUSE_1 && m_TranslateButtonRectValid)
	{
		const vec2 MousePos = ChatMousePos();
		const bool InsideTranslateButton =
			MousePos.x >= m_TranslateButtonRect.m_X && MousePos.x <= m_TranslateButtonRect.m_X + m_TranslateButtonRect.m_W &&
			MousePos.y >= m_TranslateButtonRect.m_Y && MousePos.y <= m_TranslateButtonRect.m_Y + m_TranslateButtonRect.m_H;

		if(Event.m_Flags & IInput::FLAG_PRESS)
		{
			m_TranslateButtonPressed = InsideTranslateButton;
			if(InsideTranslateButton)
			{
				m_MouseIsPress = false;
				m_HasSelection = false;
				return true;
			}
		}
		else if(Event.m_Flags & IInput::FLAG_RELEASE)
		{
			const bool ActivateButton = m_TranslateButtonPressed && InsideTranslateButton;
			m_TranslateButtonPressed = false;
			if(ActivateButton)
			{
				CUIRect ButtonRect = {m_TranslateButtonRect.m_X, m_TranslateButtonRect.m_Y, m_TranslateButtonRect.m_W, m_TranslateButtonRect.m_H};
				if(Ui()->IsPopupOpen(&m_TranslateSettingsPopupId))
					Ui()->ClosePopupMenu(&m_TranslateSettingsPopupId);
				else
					OpenTranslateSettingsPopup(ButtonRect);
				return true;
			}
		}
	}

	if(m_MediaViewerOpen && (!g_Config.m_BcChatMediaPreview || !g_Config.m_BcChatMediaViewer))
		CloseMediaViewer();

	if(m_MediaViewerOpen && !ValidateMediaViewerLine())
		CloseMediaViewer();

	if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_MOUSE_1 && m_HideMediaByBind && !m_MediaViewerOpen)
	{
		const vec2 MousePos = ChatMousePos();
		for(int i = m_BacklogCurLine; i < MAX_LINES; i++)
		{
			const int LineIndex = ((m_CurrentLine - i) + MAX_LINES) % MAX_LINES;
			CLine &Line = m_aLines[LineIndex];
			if(!Line.m_Initialized)
				break;
			if(!Line.m_MediaPreviewRectValid || !ShouldHideMediaPreview(Line))
				continue;
			const SRenderRect &Rect = Line.m_MediaPreviewRect;
			if(MousePos.x >= Rect.m_X && MousePos.x <= Rect.m_X + Rect.m_W &&
				MousePos.y >= Rect.m_Y && MousePos.y <= Rect.m_Y + Rect.m_H)
			{
				Line.m_MediaRevealed = true;
				Line.m_aYOffset[0] = -1.0f;
				Line.m_aYOffset[1] = -1.0f;
				RebuildChat();
				return true;
			}
		}
	}

	if(m_MediaViewerOpen && ValidateMediaViewerLine())
	{
		CLine &ViewerLine = m_aLines[m_MediaViewerLineIndex];
		const float ScreenHeight = 300.0f;
		const float ScreenWidth = ScreenHeight * Graphics()->ScreenAspect();
		const vec2 MousePos = ChatMousePos();
		float ViewerX = 0.0f;
		float ViewerY = 0.0f;
		float ViewerW = 0.0f;
		float ViewerH = 0.0f;
		GetMediaViewerRect(ViewerLine, ScreenWidth, ScreenHeight, ViewerX, ViewerY, ViewerW, ViewerH);

		if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_ESCAPE)
		{
			CloseMediaViewer();
			return true;
		}

		if(Event.m_Flags & IInput::FLAG_PRESS)
		{
			const float MaxZoom = maximum(1.0f, g_Config.m_BcChatMediaViewerMaxZoom / 100.0f);
			const float ZoomStep = 1.12f;
			if(Event.m_Key == KEY_MOUSE_WHEEL_UP || Event.m_Key == KEY_MOUSE_WHEEL_DOWN)
			{
				const float OldZoom = m_MediaViewerZoom;
				float NewZoom = OldZoom;
				if(Event.m_Key == KEY_MOUSE_WHEEL_UP)
					NewZoom = minimum(MaxZoom, OldZoom * ZoomStep);
				else
					NewZoom = maximum(1.0f, OldZoom / ZoomStep);

				if(NewZoom != OldZoom && ViewerW > 0.0f && ViewerH > 0.0f)
				{
					const float RelX = (MousePos.x - ViewerX) / ViewerW;
					const float RelY = (MousePos.y - ViewerY) / ViewerH;
					m_MediaViewerZoom = NewZoom;
					float NewX = 0.0f;
					float NewY = 0.0f;
					float NewW = 0.0f;
					float NewH = 0.0f;
					GetMediaViewerRect(ViewerLine, ScreenWidth, ScreenHeight, NewX, NewY, NewW, NewH);
					const float TargetX = MousePos.x - RelX * NewW;
					const float TargetY = MousePos.y - RelY * NewH;
					m_MediaViewerPan.x += TargetX - NewX;
					m_MediaViewerPan.y += TargetY - NewY;
					ClampMediaViewerPan(ViewerLine, ScreenWidth, ScreenHeight);
				}
				return true;
			}

			if(Event.m_Key == KEY_MOUSE_1)
			{
				const bool InsideMedia = MousePos.x >= ViewerX && MousePos.x <= ViewerX + ViewerW &&
					MousePos.y >= ViewerY && MousePos.y <= ViewerY + ViewerH;
				if(!InsideMedia)
				{
					CloseMediaViewer();
					return true;
				}

				const int64_t Now = time_get();
				if(m_MediaViewerLastClickTime > 0 &&
					(Now - m_MediaViewerLastClickTime) * 1000 / time_freq() <= CHAT_MEDIA_DOUBLE_CLICK_MS)
				{
					m_MediaViewerZoom = 1.0f;
					m_MediaViewerPan = vec2(0.0f, 0.0f);
					m_MediaViewerDragging = false;
					m_MediaViewerLastClickTime = 0;
					return true;
				}

				m_MediaViewerLastClickTime = Now;
				m_MediaViewerDragging = true;
				m_MediaViewerDragStartMouse = MousePos;
				m_MediaViewerPanStart = m_MediaViewerPan;
				return true;
			}
		}

		if((Event.m_Flags & IInput::FLAG_RELEASE) && Event.m_Key == KEY_MOUSE_1)
		{
			m_MediaViewerDragging = false;
			return true;
		}
	}

	if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_MOUSE_1 && g_Config.m_BcChatMediaViewer && !m_MediaViewerOpen)
	{
		const vec2 MousePos = ChatMousePos();
		for(int i = m_BacklogCurLine; i < MAX_LINES; i++)
		{
			const int LineIndex = ((m_CurrentLine - i) + MAX_LINES) % MAX_LINES;
			CLine &Line = m_aLines[LineIndex];
			if(!Line.m_Initialized)
				break;
			if(!Line.m_MediaPreviewRectValid)
				continue;
			const SRenderRect &Rect = Line.m_MediaPreviewRect;
			if(MousePos.x >= Rect.m_X && MousePos.x <= Rect.m_X + Rect.m_W &&
				MousePos.y >= Rect.m_Y && MousePos.y <= Rect.m_Y + Rect.m_H)
			{
				OpenMediaViewer(LineIndex);
				return m_MediaViewerOpen;
			}
		}
	}

	if(Event.m_Flags & IInput::FLAG_PRESS)
	{
		if(Input()->ModifierIsPressed() && Event.m_Key == KEY_C && !m_Input.HasSelection() && m_HasSelection)
		{
			m_WantsSelectionCopy = true;
			return true;
		}

		if(Event.m_Key == KEY_MOUSE_WHEEL_UP)
		{
			m_BacklogCurLine = minimum(m_BacklogCurLine + 1, MAX_LINES - 1);
			m_HasSelection = false;
			return true;
		}
		if(Event.m_Key == KEY_MOUSE_WHEEL_DOWN)
		{
			m_BacklogCurLine = maximum(m_BacklogCurLine - 1, 0);
			m_HasSelection = false;
			return true;
		}
	}

	if(!ChatInputActive)
	{
		if(Event.m_Key == KEY_MOUSE_1 && (Event.m_Flags & (IInput::FLAG_PRESS | IInput::FLAG_RELEASE)))
			return true;
		return false;
	}

	if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_ESCAPE)
	{
		const bool SaveDraft = g_Config.m_BcChatSaveDraft != 0;
		DisableMode();
		GameClient()->OnRelease();
		if(g_Config.m_ClChatReset)
		{
			if(SaveDraft)
			{
				if(m_SavedInputPending)
				{
					m_SavedInputPending = false;
					m_aSavedInputText[0] = '\0';
				}
				else if(m_Input.GetString()[0] != '\0')
				{
					str_copy(m_aSavedInputText, m_Input.GetString(), sizeof(m_aSavedInputText));
					m_SavedInputPending = true;
				}
			}
			else
			{
				m_SavedInputPending = false;
				m_aSavedInputText[0] = '\0';
			}
			m_Input.Clear();
			m_pHistoryEntry = nullptr;
		}
		else if(!SaveDraft)
		{
			m_Input.Clear();
			m_SavedInputPending = false;
			m_aSavedInputText[0] = '\0';
			m_pHistoryEntry = nullptr;
		}
		m_HasSelection = false;
		m_WantsSelectionCopy = false;
	}
	else if(Event.m_Flags & IInput::FLAG_PRESS && (Event.m_Key == KEY_RETURN || Event.m_Key == KEY_KP_ENTER))
	{
		if(m_ServerCommandsNeedSorting)
		{
			std::sort(m_vServerCommands.begin(), m_vServerCommands.end());
			m_ServerCommandsNeedSorting = false;
		}

		if(GameClient()->m_BindChat.ChatDoBinds(m_Input.GetString()))
			; // Do nothing as bindchat was executed
		else if(GameClient()->m_TClient.ChatDoSpecId(m_Input.GetString()))
			; // Do nothing as specid was executed
		else
			SendChatQueued(m_Input.GetString());
		m_SavedInputPending = false;
		m_aSavedInputText[0] = '\0';
		m_pHistoryEntry = nullptr;
		DisableMode();
		GameClient()->OnRelease();
		m_Input.Clear();
		m_HasSelection = false;
		m_WantsSelectionCopy = false;
	}
	if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_TAB)
	{
		const bool ShiftPressed = Input()->ShiftIsPressed();

		// fill the completion buffer
		if(!m_CompletionUsed)
		{
			const char *pCursor = m_Input.GetString() + m_Input.GetCursorOffset();
			for(size_t Count = 0; Count < m_Input.GetCursorOffset() && *(pCursor - 1) != ' '; --pCursor, ++Count)
				;
			m_PlaceholderOffset = pCursor - m_Input.GetString();

			for(m_PlaceholderLength = 0; *pCursor && *pCursor != ' '; ++pCursor)
				++m_PlaceholderLength;

			str_truncate(m_aCompletionBuffer, sizeof(m_aCompletionBuffer), m_Input.GetString() + m_PlaceholderOffset, m_PlaceholderLength);
		}

		if(!m_CompletionUsed && m_aCompletionBuffer[0] != '/' && m_aCompletionBuffer[0] != '!')
		{
			// Create the completion list of player names through which the player can iterate
			const char *PlayerName, *FoundInput;
			m_PlayerCompletionListLength = 0;
			for(auto &PlayerInfo : GameClient()->m_Snap.m_apInfoByName)
			{
				if(PlayerInfo)
				{
					PlayerName = GameClient()->m_aClients[PlayerInfo->m_ClientId].m_aName;
					FoundInput = str_utf8_find_nocase(PlayerName, m_aCompletionBuffer);
					if(FoundInput != nullptr)
					{
						m_aPlayerCompletionList[m_PlayerCompletionListLength].m_ClientId = PlayerInfo->m_ClientId;
						// The score for suggesting a player name is determined by the distance of the search input to the beginning of the player name
						m_aPlayerCompletionList[m_PlayerCompletionListLength].m_Score = (int)(FoundInput - PlayerName);
						m_PlayerCompletionListLength++;
					}
				}
			}
			std::stable_sort(m_aPlayerCompletionList, m_aPlayerCompletionList + m_PlayerCompletionListLength,
				[](const CRateablePlayer &Player1, const CRateablePlayer &Player2) -> bool {
					return Player1.m_Score < Player2.m_Score;
				});
		}

		auto DoVoiceAutocomplete = [&]() -> bool {
			const char *pInput = m_Input.GetString();
			if(!pInput || pInput[0] != '!')
				return false;

			const int InputLen = str_length(pInput);
			int aTokenStarts[8];
			int aTokenEnds[8];
			int NumTokens = 0;
			bool InToken = false;
			int TokenStart = 0;
			for(int i = 0; i <= InputLen && NumTokens < 8; ++i)
			{
				const char c = pInput[i];
				const bool IsSpace = c == '\0' || std::isspace((unsigned char)c);
				if(!InToken && !IsSpace)
				{
					InToken = true;
					TokenStart = i;
				}
				else if(InToken && IsSpace)
				{
					InToken = false;
					aTokenStarts[NumTokens] = TokenStart;
					aTokenEnds[NumTokens] = i;
					NumTokens++;
				}
			}
			if(NumTokens <= 0)
				return false;

			int PlaceholderToken = -1;
			for(int t = 0; t < NumTokens; ++t)
			{
				if(aTokenStarts[t] == m_PlaceholderOffset)
				{
					PlaceholderToken = t;
					break;
				}
				if(m_PlaceholderOffset >= aTokenStarts[t] && m_PlaceholderOffset < aTokenEnds[t])
					PlaceholderToken = t;
			}
			if(PlaceholderToken < 0 && m_PlaceholderLength == 0)
			{
				// Cursor is on whitespace (e.g. "!voice "): treat as completing the next token.
				if(m_PlaceholderOffset >= 0 && (m_PlaceholderOffset == InputLen || std::isspace((unsigned char)pInput[m_PlaceholderOffset])))
					PlaceholderToken = NumTokens;
			}
			if(PlaceholderToken < 0)
				return false;

			char aToken0[64];
			str_truncate(aToken0, sizeof(aToken0), pInput + aTokenStarts[0], aTokenEnds[0] - aTokenStarts[0]);

			const char *apSuggestions[24];
			int NumSuggestions = 0;

			if(PlaceholderToken == 0)
			{
				apSuggestions[NumSuggestions++] = "!voice";
				apSuggestions[NumSuggestions++] = "!voicegroup";
			}
			else
			{
				if(str_comp_nocase(aToken0, "!voice") == 0)
				{
					if(PlaceholderToken == 1)
					{
						apSuggestions[NumSuggestions++] = "on";
						apSuggestions[NumSuggestions++] = "off";
						apSuggestions[NumSuggestions++] = "status";
						apSuggestions[NumSuggestions++] = "mode";
						apSuggestions[NumSuggestions++] = "server";
						apSuggestions[NumSuggestions++] = "mute";
						apSuggestions[NumSuggestions++] = "unmute";
						apSuggestions[NumSuggestions++] = "volume";
					}
					else if(PlaceholderToken == 2 && NumTokens >= 2)
					{
						char aSub[32];
						str_truncate(aSub, sizeof(aSub), pInput + aTokenStarts[1], aTokenEnds[1] - aTokenStarts[1]);
						if(str_comp_nocase(aSub, "mode") == 0)
						{
							apSuggestions[NumSuggestions++] = "automatic";
							apSuggestions[NumSuggestions++] = "ppt";
						}
						else if(str_comp_nocase(aSub, "server") == 0)
						{
							apSuggestions[NumSuggestions++] = "reload";
						}
						else
						{
							return false;
						}
					}
					else
					{
						return false;
					}
				}
				else if(str_comp_nocase(aToken0, "!voicegroup") == 0)
				{
					if(PlaceholderToken == 1)
					{
						apSuggestions[NumSuggestions++] = "create";
						apSuggestions[NumSuggestions++] = "leave";
						apSuggestions[NumSuggestions++] = "private";
						apSuggestions[NumSuggestions++] = "public";
						apSuggestions[NumSuggestions++] = "join";
						apSuggestions[NumSuggestions++] = "invite";
						apSuggestions[NumSuggestions++] = "list";
					}
					else
					{
						return false;
					}
				}
				else
				{
					return false;
				}
			}

			if(NumSuggestions <= 0)
				return false;

			const char *pCompletion = nullptr;
			if(ShiftPressed && m_CompletionUsed)
				m_CompletionChosen--;
			else if(!ShiftPressed)
				m_CompletionChosen++;
			m_CompletionChosen = (m_CompletionChosen % NumSuggestions + NumSuggestions) % NumSuggestions;
			m_CompletionUsed = true;

			for(int i = 0; i < NumSuggestions; ++i)
			{
				const int Index = (m_CompletionChosen + (ShiftPressed ? -i : i) + NumSuggestions) % NumSuggestions;
				const char *pCandidate = apSuggestions[Index];
				if(str_startswith_nocase(pCandidate, m_aCompletionBuffer))
				{
					pCompletion = pCandidate;
					m_CompletionChosen = Index;
					break;
				}
			}
			if(!pCompletion)
				return false;

			char aBuf[MAX_LINE_LENGTH];
			str_truncate(aBuf, sizeof(aBuf), m_Input.GetString(), m_PlaceholderOffset);
			str_append(aBuf, pCompletion);

			const char *pAfter = m_Input.GetString() + m_PlaceholderOffset + m_PlaceholderLength;
			const char *pSeparator = *pAfter == '\0' ? " " : (*pAfter != ' ' ? " " : "");
			if(*pSeparator)
				str_append(aBuf, pSeparator);

			str_append(aBuf, pAfter);

			m_PlaceholderLength = str_length(pCompletion) + str_length(pSeparator);
			m_Input.Set(aBuf);
			m_Input.SetCursorOffset(m_PlaceholderOffset + m_PlaceholderLength);
			return true;
		};

		if(GameClient()->m_BindChat.ChatDoAutocomplete(ShiftPressed))
		{
		}
		else if(m_aCompletionBuffer[0] == '/' && !m_vServerCommands.empty())
		{
			CCommand *pCompletionCommand = nullptr;

			const size_t NumCommands = m_vServerCommands.size();

			if(ShiftPressed && m_CompletionUsed)
				m_CompletionChosen--;
			else if(!ShiftPressed)
				m_CompletionChosen++;
			m_CompletionChosen = (m_CompletionChosen + 2 * NumCommands) % (2 * NumCommands);

			m_CompletionUsed = true;

			const char *pCommandStart = m_aCompletionBuffer + 1;
			for(size_t i = 0; i < 2 * NumCommands; ++i)
			{
				int SearchType;
				int Index;

				if(ShiftPressed)
				{
					SearchType = ((m_CompletionChosen - i + 2 * NumCommands) % (2 * NumCommands)) / NumCommands;
					Index = (m_CompletionChosen - i + NumCommands) % NumCommands;
				}
				else
				{
					SearchType = ((m_CompletionChosen + i) % (2 * NumCommands)) / NumCommands;
					Index = (m_CompletionChosen + i) % NumCommands;
				}

				auto &Command = m_vServerCommands[Index];

				if(str_startswith_nocase(Command.m_aName, pCommandStart))
				{
					pCompletionCommand = &Command;
					m_CompletionChosen = Index + SearchType * NumCommands;
					break;
				}
			}

			// insert the command
			if(pCompletionCommand)
			{
				char aBuf[MAX_LINE_LENGTH];
				// add part before the name
				str_truncate(aBuf, sizeof(aBuf), m_Input.GetString(), m_PlaceholderOffset);

				// add the command
				str_append(aBuf, "/");
				str_append(aBuf, pCompletionCommand->m_aName);

				// add separator
				const char *pSeparator = pCompletionCommand->m_aParams[0] == '\0' ? "" : " ";
				str_append(aBuf, pSeparator);

				// add part after the name
				str_append(aBuf, m_Input.GetString() + m_PlaceholderOffset + m_PlaceholderLength);

				m_PlaceholderLength = str_length(pSeparator) + str_length(pCompletionCommand->m_aName) + 1;
				m_Input.Set(aBuf);
				m_Input.SetCursorOffset(m_PlaceholderOffset + m_PlaceholderLength);
			}
		}
		else if(DoVoiceAutocomplete())
		{
		}
		else
		{
			// find next possible name
			const char *pCompletionString = nullptr;
			if(m_PlayerCompletionListLength > 0)
			{
				// We do this in a loop, if a player left the game during the repeated pressing of Tab, they are skipped
				CGameClient::CClientData *pCompletionClientData;
				for(int i = 0; i < m_PlayerCompletionListLength; ++i)
				{
					if(ShiftPressed && m_CompletionUsed)
					{
						m_CompletionChosen--;
					}
					else if(!ShiftPressed)
					{
						m_CompletionChosen++;
					}
					if(m_CompletionChosen < 0)
					{
						m_CompletionChosen += m_PlayerCompletionListLength;
					}
					m_CompletionChosen %= m_PlayerCompletionListLength;
					m_CompletionUsed = true;

					pCompletionClientData = &GameClient()->m_aClients[m_aPlayerCompletionList[m_CompletionChosen].m_ClientId];
					if(!pCompletionClientData->m_Active)
					{
						continue;
					}

					pCompletionString = pCompletionClientData->m_aName;
					break;
				}
			}

			// insert the name
			if(pCompletionString)
			{
				char aBuf[MAX_LINE_LENGTH];
				// add part before the name
				str_truncate(aBuf, sizeof(aBuf), m_Input.GetString(), m_PlaceholderOffset);

				// quote the name
				char aQuoted[128];
				if((m_Input.GetString()[0] == '/' || m_Input.GetString()[0] == '!' || GameClient()->m_BindChat.CheckBindChat(m_Input.GetString())) && (str_find(pCompletionString, " ") || str_find(pCompletionString, "\"")))
				{
					// escape the name
					str_copy(aQuoted, "\"");
					char *pDst = aQuoted + str_length(aQuoted);
					str_escape(&pDst, pCompletionString, aQuoted + sizeof(aQuoted));
					str_append(aQuoted, "\"");

					pCompletionString = aQuoted;
				}

				// add the name
				str_append(aBuf, pCompletionString);

				// add separator
				const char *pSeparator = "";
				if(*(m_Input.GetString() + m_PlaceholderOffset + m_PlaceholderLength) != ' ')
					pSeparator = m_PlaceholderOffset == 0 ? ": " : " ";
				else if(m_PlaceholderOffset == 0)
					pSeparator = ":";
				if(*pSeparator)
					str_append(aBuf, pSeparator);

				// add part after the name
				str_append(aBuf, m_Input.GetString() + m_PlaceholderOffset + m_PlaceholderLength);

				m_PlaceholderLength = str_length(pSeparator) + str_length(pCompletionString);
				m_Input.Set(aBuf);
				m_Input.SetCursorOffset(m_PlaceholderOffset + m_PlaceholderLength);
			}
		}
	}
	else
	{
		// reset name completion process
		if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key != KEY_TAB && Event.m_Key != KEY_LSHIFT && Event.m_Key != KEY_RSHIFT)
		{
			m_CompletionChosen = -1;
			m_CompletionUsed = false;
		}

		m_Input.ProcessInput(Event);
	}

	if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_UP)
	{
		if(m_EditingNewLine)
		{
			str_copy(m_aCurrentInputText, m_Input.GetString());
			m_EditingNewLine = false;
		}

		if(m_pHistoryEntry)
		{
			CHistoryEntry *pTest = m_History.Prev(m_pHistoryEntry);

			if(pTest)
				m_pHistoryEntry = pTest;
		}
		else
			m_pHistoryEntry = m_History.Last();

		if(m_pHistoryEntry)
			m_Input.Set(m_pHistoryEntry->m_aText);
	}
	else if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_DOWN)
	{
		if(m_pHistoryEntry)
			m_pHistoryEntry = m_History.Next(m_pHistoryEntry);

		if(m_pHistoryEntry)
		{
			m_Input.Set(m_pHistoryEntry->m_aText);
		}
		else if(!m_EditingNewLine)
		{
			m_Input.Set(m_aCurrentInputText);
			m_EditingNewLine = true;
		}
	}

	RefreshTypingAnimation();
	return true;
}

bool CChat::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(m_Mode == MODE_NONE && !m_Show && !m_MediaViewerOpen)
		return false;

	Ui()->ConvertMouseMove(&x, &y, CursorType);
	if(m_MediaViewerOpen && m_MediaViewerDragging && ValidateMediaViewerLine())
	{
		const float Height = 300.0f;
		const float Width = Height * Graphics()->ScreenAspect();
		const vec2 UiToChatScale(Width / Ui()->Screen()->w, Height / Ui()->Screen()->h);
		m_MediaViewerPan += vec2(x * UiToChatScale.x, y * UiToChatScale.y);
		ClampMediaViewerPan(m_aLines[m_MediaViewerLineIndex], Width, Height);
	}
	Ui()->OnCursorMove(x, y);
	return true;
}

void CChat::SetUiMousePos(vec2 Pos)
{
	const vec2 WindowSize = vec2(Graphics()->WindowWidth(), Graphics()->WindowHeight());
	const CUIRect *pScreen = Ui()->Screen();
	const vec2 UpdatedMousePos = Ui()->UpdatedMousePos();
	Pos = Pos / vec2(pScreen->w, pScreen->h) * WindowSize;
	Ui()->OnCursorMove(Pos.x - UpdatedMousePos.x, Pos.y - UpdatedMousePos.y);
}

bool CChat::WasChatAutoHidden() const
{
	if(g_Config.m_ClShowChat == 0 || g_Config.m_ClShowChat == 2 || m_Mode != MODE_NONE)
		return false;

	const int64_t Now = time();
	bool HadAnyLines = false;
	for(int i = 0; i < MAX_LINES; i++)
	{
		const CLine &Line = m_aLines[((m_CurrentLine - i) + MAX_LINES) % MAX_LINES];
		if(!Line.m_Initialized)
			break;

		HadAnyLines = true;
		if(Now <= Line.m_Time + 16 * time_freq())
			return false;
	}

	return HadAnyLines;
}

void CChat::EnableMode(int Team)
{
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return;

	if(m_Mode == MODE_NONE)
	{
		const bool AnimateWholeChatOpen = WasChatAutoHidden();
		if(Team)
			m_Mode = MODE_TEAM;
		else
			m_Mode = MODE_ALL;

		Input()->Clear();
		m_CompletionChosen = -1;
		m_CompletionUsed = false;
		m_BacklogCurLine = 0;
		m_ScrollbarDragging = false;
		m_ScrollbarDragOffset = 0.0f;
		m_MouseIsPress = false;
		m_HasSelection = false;
		m_WantsSelectionCopy = false;
		m_ChatOpenAnimationStart = AnimateWholeChatOpen ? time_get() : 0;
		ResetTypingAnimation();
		const vec2 WindowSize(maximum(1.0f, (float)Graphics()->WindowWidth()), maximum(1.0f, (float)Graphics()->WindowHeight()));
		m_LastMousePos = Ui()->UpdatedMousePos() * vec2(Ui()->Screen()->w, Ui()->Screen()->h) / WindowSize;
		SetUiMousePos(Ui()->Screen()->Center());
		m_Input.Activate(EInputPriority::CHAT);
		SyncTypingAnimationBaseline();

		}
	}

void CChat::DisableMode()
{
	if(m_Mode != MODE_NONE)
	{
		CloseMediaViewer();
		Ui()->ClosePopupMenus();
		m_Mode = MODE_NONE;
		m_BacklogCurLine = 0;
		m_ScrollbarDragging = false;
		m_MouseIsPress = false;
		m_HasSelection = false;
		m_WantsSelectionCopy = false;
		m_ChatOpenAnimationStart = 0;
		ResetTypingAnimation();
		m_aPreviousDisplayedInputText[0] = '\0';
		ResetHiddenMediaReveals();
		if(m_LastMousePos.has_value())
			SetUiMousePos(m_LastMousePos.value());
		const vec2 WindowSize(maximum(1.0f, (float)Graphics()->WindowWidth()), maximum(1.0f, (float)Graphics()->WindowHeight()));
		m_LastMousePos = Ui()->UpdatedMousePos() * vec2(Ui()->Screen()->w, Ui()->Screen()->h) / WindowSize;
		m_Input.Deactivate();

		}
	}

void CChat::OnMessage(int MsgType, void *pRawMsg)
{
	if(GameClient()->m_SuppressEvents)
		return;

	if(MsgType == NETMSGTYPE_SV_CHAT)
	{
		CNetMsg_Sv_Chat *pMsg = (CNetMsg_Sv_Chat *)pRawMsg;

		/*
		if(g_Config.m_ClCensorChat)
		{
			char aMessage[MAX_LINE_LENGTH];
			str_copy(aMessage, pMsg->m_pMessage);
			GameClient()->m_Censor.CensorMessage(aMessage);
			AddLine(pMsg->m_ClientId, pMsg->m_Team, aMessage);
		}
		else
			AddLine(pMsg->m_ClientId, pMsg->m_Team, pMsg->m_pMessage);
		*/

		AddLine(pMsg->m_ClientId, pMsg->m_Team, pMsg->m_pMessage);

		if(Client()->State() != IClient::STATE_DEMOPLAYBACK &&
			pMsg->m_ClientId == SERVER_MSG)
		{
			StoreSave(pMsg->m_pMessage);
		}
	}
	else if(MsgType == NETMSGTYPE_SV_COMMANDINFO)
	{
		CNetMsg_Sv_CommandInfo *pMsg = (CNetMsg_Sv_CommandInfo *)pRawMsg;
		if(!m_ServerSupportsCommandInfo)
		{
			m_vServerCommands.clear();
			m_ServerSupportsCommandInfo = true;
		}
		RegisterCommand(pMsg->m_pName, pMsg->m_pArgsFormat, pMsg->m_pHelpText);
	}
	else if(MsgType == NETMSGTYPE_SV_COMMANDINFOREMOVE)
	{
		CNetMsg_Sv_CommandInfoRemove *pMsg = (CNetMsg_Sv_CommandInfoRemove *)pRawMsg;
		UnregisterCommand(pMsg->m_pName);
	}
}

bool CChat::LineShouldHighlight(const char *pLine, const char *pName)
{
	const char *pHit = str_utf8_find_nocase(pLine, pName);

	while(pHit)
	{
		int Length = str_length(pName);

		if(Length > 0 && (pLine == pHit || pHit[-1] == ' ') && (pHit[Length] == 0 || pHit[Length] == ' ' || pHit[Length] == '.' || pHit[Length] == '!' || pHit[Length] == ',' || pHit[Length] == '?' || pHit[Length] == ':'))
			return true;

		pHit = str_utf8_find_nocase(pHit + 1, pName);
	}

	return false;
}

static constexpr const char *SAVES_HEADER[] = {
	"Time",
	"Player",
	"Map",
	"Code",
};

// TODO: remove this in a few releases (in 2027 or later)
//       it got deprecated by CGameClient::StoreSave
void CChat::StoreSave(const char *pText)
{
	const char *pStart = str_find(pText, "Team successfully saved by ");
	const char *pMid = str_find(pText, ". Use '/load ");
	const char *pOn = str_find(pText, "' on ");
	const char *pEnd = str_find(pText, pOn ? " to continue" : "' to continue");

	if(!pStart || !pMid || !pEnd || pMid < pStart || pEnd < pMid || (pOn && (pOn < pMid || pEnd < pOn)))
		return;

	char aName[16];
	str_truncate(aName, sizeof(aName), pStart + 27, pMid - pStart - 27);

	char aSaveCode[64];

	str_truncate(aSaveCode, sizeof(aSaveCode), pMid + 13, (pOn ? pOn : pEnd) - pMid - 13);

	char aTimestamp[20];
	str_timestamp_format(aTimestamp, sizeof(aTimestamp), TimestampFormat::SPACE);

	const bool SavesFileExists = Storage()->FileExists(SAVES_FILE, IStorage::TYPE_SAVE);
	IOHANDLE File = Storage()->OpenFile(SAVES_FILE, IOFLAG_APPEND, IStorage::TYPE_SAVE);
	if(!File)
		return;

	const char *apColumns[4] = {
		aTimestamp,
		aName,
		GameClient()->Map()->BaseName(),
		aSaveCode,
	};

	if(!SavesFileExists)
	{
		CsvWrite(File, 4, SAVES_HEADER);
	}
	CsvWrite(File, 4, apColumns);
	io_close(File);
}

void CChat::AddLine(int ClientId, int Team, const char *pLine)
{
	if(*pLine == 0 ||
		(ClientId == SERVER_MSG && !g_Config.m_ClShowChatSystem) ||
		(ClientId >= 0 && (GameClient()->m_aClients[ClientId].m_aName[0] == '\0' || // unknown client
					  GameClient()->m_aClients[ClientId].m_ChatIgnore ||
					  (GameClient()->m_Snap.m_LocalClientId != ClientId && g_Config.m_ClShowChatFriends && !GameClient()->m_aClients[ClientId].m_Friend) ||
					  (GameClient()->m_Snap.m_LocalClientId != ClientId && g_Config.m_ClShowChatTeamMembersOnly && GameClient()->IsOtherTeam(ClientId) && GameClient()->m_Teams.Team(GameClient()->m_Snap.m_LocalClientId) != TEAM_FLOCK) ||
					  (GameClient()->m_Snap.m_LocalClientId != ClientId && GameClient()->m_aClients[ClientId].m_Foe))))
		return;

	// BestClient
	if(ClientId == CLIENT_MSG && !g_Config.m_TcShowChatClient)
		return;

	// trim right and set maximum length to 256 utf8-characters
	int Length = 0;
	const char *pStr = pLine;
	const char *pEnd = nullptr;
	while(*pStr)
	{
		const char *pStrOld = pStr;
		int Code = str_utf8_decode(&pStr);

		// check if unicode is not empty
		if(!str_utf8_isspace(Code))
		{
			pEnd = nullptr;
		}
		else if(pEnd == nullptr)
			pEnd = pStrOld;

		if(++Length >= MAX_LINE_LENGTH)
		{
			*(const_cast<char *>(pStr)) = '\0';
			break;
		}
	}
	if(pEnd != nullptr)
		*(const_cast<char *>(pEnd)) = '\0';

	if(*pLine == 0)
		return;

	bool Highlighted = false;

	auto &&FChatMsgCheckAndPrint = [this](const CLine &Line) {
		char aBuf[1024];
		str_format(aBuf, sizeof(aBuf), "%s%s%s", Line.m_aName, Line.m_ClientId >= 0 ? ": " : "", Line.m_aText);

		ColorRGBA ChatLogColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		if(Line.m_Highlighted)
		{
			ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageHighlightColor));
		}
		else
		{
			if(Line.m_Friend && g_Config.m_ClMessageFriend)
				ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageFriendColor));
			else if(Line.m_Team)
				ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageTeamColor));
			else if(Line.m_ClientId == SERVER_MSG)
				ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageSystemColor));
			else if(Line.m_ClientId == CLIENT_MSG)
				ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageClientColor));
			else // regular message
				ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageColor));
		}

		const char *pFrom;
		if(Line.m_Whisper)
			pFrom = "chat/whisper";
		else if(Line.m_Team)
			pFrom = "chat/team";
		else if(Line.m_ClientId == SERVER_MSG)
			pFrom = "chat/server";
		else if(Line.m_ClientId == CLIENT_MSG)
			pFrom = "chat/client";
		else
			pFrom = "chat/all";

		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, pFrom, aBuf, ChatLogColor);
	};

	// Custom color for new line
	std::optional<ColorRGBA> CustomColor = std::nullopt;
	if(ClientId == CLIENT_MSG)
		CustomColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageClientColor));

	CLine &PreviousLine = m_aLines[m_CurrentLine];

	// Team Number:
	// 0 = global; 1 = team; 2 = sending whisper; 3 = receiving whisper

	// If it's a client message, m_aText will have ": " prepended so we have to work around it.
	if(PreviousLine.m_Initialized &&
		PreviousLine.m_TeamNumber == Team &&
		PreviousLine.m_ClientId == ClientId &&
		str_comp(PreviousLine.m_aText, pLine) == 0 &&
		PreviousLine.m_CustomColor == CustomColor)
	{
		PreviousLine.m_TimesRepeated++;
		TextRender()->DeleteTextContainer(PreviousLine.m_TextContainerIndex);
		Graphics()->DeleteQuadContainer(PreviousLine.m_QuadContainerIndex);
		PreviousLine.m_Time = time();
		PreviousLine.m_aYOffset[0] = -1.0f;
		PreviousLine.m_aYOffset[1] = -1.0f;

		FChatMsgCheckAndPrint(PreviousLine);
		return;
	}

	m_CurrentLine = (m_CurrentLine + 1) % MAX_LINES;
	if(m_BacklogCurLine > 0)
		m_BacklogCurLine = minimum(m_BacklogCurLine + 1, MAX_LINES - 1);

	CLine &CurrentLine = m_aLines[m_CurrentLine];
	CurrentLine.Reset(*this);
	CurrentLine.m_Initialized = true;
	CurrentLine.m_Time = time();
	CurrentLine.m_aYOffset[0] = -1.0f;
	CurrentLine.m_aYOffset[1] = -1.0f;
	CurrentLine.m_ClientId = ClientId;
	CurrentLine.m_TeamNumber = Team;
	CurrentLine.m_Team = Team == 1;
	CurrentLine.m_Whisper = Team >= 2;
	CurrentLine.m_NameColor = -2;
	CurrentLine.m_CustomColor = CustomColor;

	// check for highlighted name
	if(Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		if(ClientId >= 0 && ClientId != GameClient()->m_aLocalIds[0] && ClientId != GameClient()->m_aLocalIds[1])
		{
			for(int LocalId : GameClient()->m_aLocalIds)
			{
				Highlighted |= LocalId >= 0 && LineShouldHighlight(pLine, GameClient()->m_aClients[LocalId].m_aName);
			}
		}
	}
	else
	{
		// on demo playback use local id from snap directly,
		// since m_aLocalIds isn't valid there
		Highlighted |= GameClient()->m_Snap.m_LocalClientId >= 0 && LineShouldHighlight(pLine, GameClient()->m_aClients[GameClient()->m_Snap.m_LocalClientId].m_aName);
	}
	CurrentLine.m_Highlighted = Highlighted;

	str_copy(CurrentLine.m_aText, pLine);
	if(g_Config.m_BcChatMediaPreview && AnyMediaAllowed())
	{
		std::vector<std::string> vMediaUrls;
		ExtractMediaUrlsFromText(CurrentLine.m_aText, vMediaUrls);
		SetMediaCandidates(CurrentLine, vMediaUrls);
		if(!CurrentLine.m_vMediaCandidates.empty())
		{
			QueueMediaDownload(CurrentLine);
		}
		else if(g_Config.m_Debug && str_find(CurrentLine.m_aText, "http"))
		{
			log_debug("chat/media", "No usable media candidates in message: %s", CurrentLine.m_aText);
		}
	}

	if(CurrentLine.m_ClientId == SERVER_MSG)
	{
		str_copy(CurrentLine.m_aName, "*** ");
	}
	else if(CurrentLine.m_ClientId == CLIENT_MSG)
	{
		str_copy(CurrentLine.m_aName, "— ");
	}
	else
	{
		const auto &LineAuthor = GameClient()->m_aClients[CurrentLine.m_ClientId];

		if(LineAuthor.m_Active)
		{
			if(LineAuthor.m_Team == TEAM_SPECTATORS)
				CurrentLine.m_NameColor = TEAM_SPECTATORS;

			if(GameClient()->IsTeamPlay())
			{
				if(LineAuthor.m_Team == TEAM_RED)
					CurrentLine.m_NameColor = TEAM_RED;
				else if(LineAuthor.m_Team == TEAM_BLUE)
					CurrentLine.m_NameColor = TEAM_BLUE;
			}
		}

		if(Team == TEAM_WHISPER_SEND)
		{
			str_copy(CurrentLine.m_aName, "→");
			if(LineAuthor.m_Active)
			{
				str_append(CurrentLine.m_aName, " ");
				str_append(CurrentLine.m_aName, LineAuthor.m_aName);
			}
			CurrentLine.m_NameColor = TEAM_BLUE;
			CurrentLine.m_Highlighted = false;
			Highlighted = false;
		}
		else if(Team == TEAM_WHISPER_RECV)
		{
			str_copy(CurrentLine.m_aName, "←");
			if(LineAuthor.m_Active)
			{
				str_append(CurrentLine.m_aName, " ");
				str_append(CurrentLine.m_aName, LineAuthor.m_aName);
			}
			CurrentLine.m_NameColor = TEAM_RED;
			CurrentLine.m_Highlighted = true;
			Highlighted = true;
		}
		else
		{
			str_copy(CurrentLine.m_aName, LineAuthor.m_aName);
		}

		if(LineAuthor.m_Active)
		{
			CurrentLine.m_Friend = LineAuthor.m_Friend;
			CurrentLine.m_pManagedTeeRenderInfo = GameClient()->CreateManagedTeeRenderInfo(LineAuthor);
		}
	}

	FChatMsgCheckAndPrint(CurrentLine);

	// play sound
	int64_t Now = time();
	if(ClientId == SERVER_MSG)
	{
		if(Now - m_aLastSoundPlayed[CHAT_SERVER] >= time_freq() * 3 / 10)
		{
			if(g_Config.m_SndServerMessage)
			{
				GameClient()->m_Sounds.Play(CSounds::CHN_GUI, SOUND_CHAT_SERVER, 1.0f);
				m_aLastSoundPlayed[CHAT_SERVER] = Now;
			}
		}
	}
	else if(ClientId == CLIENT_MSG)
	{
		// No sound yet
	}
	else if(Highlighted && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		if(Now - m_aLastSoundPlayed[CHAT_HIGHLIGHT] >= time_freq() * 3 / 10)
		{
			char aBuf[1024];
			const std::string VisibleText = BuildVisibleMessageText(CurrentLine, true);
			str_format(aBuf, sizeof(aBuf), "%s: %s", CurrentLine.m_aName, VisibleText.c_str());
			Client()->Notify("DDNet Chat", aBuf);
			if(g_Config.m_SndHighlight)
			{
				GameClient()->m_Sounds.Play(CSounds::CHN_GUI, SOUND_CHAT_HIGHLIGHT, 1.0f);
				m_aLastSoundPlayed[CHAT_HIGHLIGHT] = Now;
			}

			if(g_Config.m_ClEditor)
			{
				GameClient()->Editor()->UpdateMentions();
			}
		}
	}
	else if(Team != TEAM_WHISPER_SEND)
	{
		if(Now - m_aLastSoundPlayed[CHAT_CLIENT] >= time_freq() * 3 / 10)
		{
			bool PlaySound = CurrentLine.m_Team ? g_Config.m_SndTeamChat : g_Config.m_SndChat;
#if defined(CONF_VIDEORECORDER)
			if(IVideo::Current())
			{
				PlaySound &= (bool)g_Config.m_ClVideoShowChat;
			}
#endif
			if(PlaySound)
			{
				GameClient()->m_Sounds.Play(CSounds::CHN_GUI, SOUND_CHAT_CLIENT, 1.0f);
				m_aLastSoundPlayed[CHAT_CLIENT] = Now;
			}
		}
	}

	// BestClient
	GameClient()->m_Translate.AutoTranslate(CurrentLine);
}

void CChat::OnPrepareLines(float y, int StartLine, int HoveredTranslateLineIndex)
{
	float x = 5.0f;
	float FontSize = this->FontSize();

	const bool IsScoreBoardOpen = GameClient()->m_Scoreboard.IsActive() && (Graphics()->ScreenAspect() > 1.7f); // only assume scoreboard when screen ratio is widescreen(something around 16:9)
	const bool ShowLargeArea = m_Show || (m_Mode != MODE_NONE && g_Config.m_ClShowChat == 1) || g_Config.m_ClShowChat == 2;
	const bool ChatInteractionActive = m_Mode != MODE_NONE || m_Show;
	const bool ModeActive = m_Mode != MODE_NONE;
	const bool ChatSelectionActive = m_HasSelection && !m_MouseIsPress && !m_WantsSelectionCopy && !m_Input.HasSelection();
	const bool ForceSelectionRefresh = m_MouseIsPress || m_WantsSelectionCopy || ChatSelectionActive != m_PrevChatSelectionActive;
	const bool ForceRecreate = IsScoreBoardOpen != m_PrevScoreBoardShowed || ShowLargeArea != m_PrevShowChat || ModeActive != m_PrevModeActive || ForceSelectionRefresh || HoveredTranslateLineIndex != m_HoveredTranslateLineIndex;
	const bool KeepLinesAlive = m_MediaViewerOpen && ValidateMediaViewerLine();
	m_PrevScoreBoardShowed = IsScoreBoardOpen;
	m_PrevShowChat = ShowLargeArea;
	m_PrevModeActive = ModeActive;
	m_PrevChatSelectionActive = ChatSelectionActive;
	m_HoveredTranslateLineIndex = HoveredTranslateLineIndex;

	const int TeeSize = MessageTeeSize();
	float RealMsgPaddingX = MessagePaddingX();
	float RealMsgPaddingY = MessagePaddingY();
	float RealMsgPaddingTee = TeeSize + MESSAGE_TEE_PADDING_RIGHT;

	if(g_Config.m_ClChatOld)
	{
		RealMsgPaddingX = 0;
		RealMsgPaddingY = 0;
		RealMsgPaddingTee = 0;
	}

	int64_t Now = time();
	float LineWidth = (IsScoreBoardOpen ? maximum(85.0f, (FontSize * 85.0f / 6.0f)) : g_Config.m_ClChatWidth) - (RealMsgPaddingX * 1.5f) - RealMsgPaddingTee;

	float HeightLimit = IsScoreBoardOpen ? 180.0f : (m_PrevShowChat ? 50.0f : 200.0f);
	float Begin = x;
	float TextBegin = Begin + RealMsgPaddingX / 2.0f;
	int OffsetType = IsScoreBoardOpen ? 1 : 0;
	const float MaxPreviewHeight = IsScoreBoardOpen ? CHAT_MEDIA_MAX_PREVIEW_HEIGHT_SCOREBOARD : CHAT_MEDIA_MAX_PREVIEW_HEIGHT;

	for(int i = StartLine; i < MAX_LINES; i++)
	{
		const int LineIndex = ((m_CurrentLine - i) + MAX_LINES) % MAX_LINES;
		CLine &Line = m_aLines[LineIndex];
		if(!Line.m_Initialized)
			break;
		if(Now > Line.m_Time + 16 * time_freq() && !m_PrevShowChat && !KeepLinesAlive)
			break;

		if(Line.m_TextContainerIndex.Valid() && Line.m_aYOffset[OffsetType] >= 0.0f && !ForceRecreate)
			continue;

		TextRender()->DeleteTextContainer(Line.m_TextContainerIndex);
		Graphics()->DeleteQuadContainer(Line.m_QuadContainerIndex);

		char aClientId[16] = "";
		if(g_Config.m_ClShowIds && Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
		{
			GameClient()->FormatClientId(Line.m_ClientId, aClientId, EClientIdFormat::INDENT_AUTO);
		}

		char aCount[12];
		if(Line.m_ClientId < 0)
			str_format(aCount, sizeof(aCount), "[%d] ", Line.m_TimesRepeated + 1);
		else
			str_format(aCount, sizeof(aCount), " [%d]", Line.m_TimesRepeated + 1);

		bool TextHiddenByStreamer = false;
		std::string VisibleTextStorage;
		const char *pText = Line.m_aText;
		if(Config()->m_ClStreamerMode && Line.m_ClientId == SERVER_MSG)
		{
			if(str_startswith(Line.m_aText, "Team save in progress. You'll be able to load with '/load ") && str_endswith(Line.m_aText, "'"))
			{
				TextHiddenByStreamer = true;
				pText = "Team save in progress. You'll be able to load with '/load *** *** ***'";
			}
			else if(str_startswith(Line.m_aText, "Team save in progress. You'll be able to load with '/load") && str_endswith(Line.m_aText, "if it fails"))
			{
				TextHiddenByStreamer = true;
				pText = "Team save in progress. You'll be able to load with '/load *** *** ***' if save is successful or with '/load *** *** ***' if it fails";
			}
			else if(str_startswith(Line.m_aText, "Team successfully saved by ") && str_endswith(Line.m_aText, " to continue"))
			{
				TextHiddenByStreamer = true;
				pText = "Team successfully saved by ***. Use '/load *** *** ***' to continue";
			}
		}
		else
		{
			VisibleTextStorage = BuildVisibleMessageText(Line, false);
			pText = VisibleTextStorage.c_str();
		}

		const CColoredParts ColoredParts(pText, Line.m_ClientId == CLIENT_MSG);
		if(!ColoredParts.Colors().empty() && ColoredParts.Colors()[0].m_Index == 0)
			Line.m_CustomColor = ColoredParts.Colors()[0].m_Color;
		pText = ColoredParts.Text();

		const char *pTranslatedError = nullptr;
		const char *pTranslatedText = nullptr;
		const char *pTranslatedLanguage = nullptr;
		if(Line.m_pTranslateResponse != nullptr && Line.m_pTranslateResponse->m_Text[0])
		{
			// If hidden and there is translated text
			if(TextHiddenByStreamer)
			{
				pTranslatedError = TCLocalize("Translated text hidden due to streamer mode");
			}
			else if(Line.m_pTranslateResponse->m_Error)
			{
				pTranslatedError = Line.m_pTranslateResponse->m_Text;
			}
			else
			{
				pTranslatedText = Line.m_pTranslateResponse->m_Text;
				if(Line.m_pTranslateResponse->m_Language[0] != '\0')
					pTranslatedLanguage = Line.m_pTranslateResponse->m_Language;
			}
		}
		const bool ShowOriginalOnHover = pTranslatedText != nullptr && HoveredTranslateLineIndex == LineIndex;
		const char *pDisplayedTranslatedText = ShowOriginalOnHover ? pText : pTranslatedText;
		const char *pDisplayedTranslatedLanguage = ShowOriginalOnHover ? nullptr : pTranslatedLanguage;

		Line.m_SelectionStart = -1;
		Line.m_SelectionEnd = -1;

		// get the y offset (calculate it if we haven't done that yet)
		if(Line.m_aYOffset[OffsetType] < 0.0f)
		{
			CTextCursor MeasureCursor;
			MeasureCursor.SetPosition(vec2(TextBegin, 0.0f));
			MeasureCursor.m_FontSize = FontSize;
			MeasureCursor.m_Flags = 0;
			MeasureCursor.m_LineWidth = LineWidth;

			if(Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
			{
				MeasureCursor.m_X += RealMsgPaddingTee;

				if(Line.m_Friend && g_Config.m_ClMessageFriend)
				{
					TextRender()->TextEx(&MeasureCursor, "♥ ");
				}
			}

			TextRender()->TextEx(&MeasureCursor, aClientId);
			TextRender()->TextEx(&MeasureCursor, Line.m_aName);
			if(Line.m_TimesRepeated > 0)
				TextRender()->TextEx(&MeasureCursor, aCount);

			if(Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
			{
				TextRender()->TextEx(&MeasureCursor, ": ");
			}

			const float PrefixWidth = MeasureCursor.m_LongestLineWidth;
			MeasureCursor.m_LongestLineWidth = 0.0f;
			if(!IsScoreBoardOpen && !g_Config.m_ClChatOld)
			{
				MeasureCursor.m_StartX = MeasureCursor.m_X;
				MeasureCursor.m_LineWidth -= PrefixWidth;
			}

			if(pDisplayedTranslatedText)
			{
				TextRender()->TextEx(&MeasureCursor, pDisplayedTranslatedText);
				if(pDisplayedTranslatedLanguage)
				{
					TextRender()->TextEx(&MeasureCursor, " [");
					TextRender()->TextEx(&MeasureCursor, pDisplayedTranslatedLanguage);
					TextRender()->TextEx(&MeasureCursor, "]");
				}
			}
			else if(pTranslatedError)
			{
				TextRender()->TextEx(&MeasureCursor, pText);
				TextRender()->TextEx(&MeasureCursor, "\n");
				MeasureCursor.m_FontSize *= 0.8f;
				TextRender()->TextEx(&MeasureCursor, pTranslatedError);
				MeasureCursor.m_FontSize /= 0.8f;
			}
			else
			{
				TextRender()->TextEx(&MeasureCursor, pText);
			}

			Line.m_aTextHeight[OffsetType] = MeasureCursor.Height();
			Line.m_aMediaPreviewWidth[OffsetType] = 0.0f;
			Line.m_aMediaPreviewHeight[OffsetType] = 0.0f;
			float TotalHeight = Line.m_aTextHeight[OffsetType] + RealMsgPaddingY;
			const bool ShowMediaSlot = ShouldDisplayMediaSlot(Line);
			const bool HideMediaPreview = ShouldHideMediaPreview(Line);
			if(ShowMediaSlot && (HideMediaPreview || (Line.m_MediaState == EMediaState::READY && Line.m_MediaWidth > 0 && Line.m_MediaHeight > 0 && !Line.m_vMediaFrames.empty())))
			{
				const float MaxPreviewWidth = minimum(LineWidth, (float)g_Config.m_BcChatMediaPreviewMaxWidth);
				if(MaxPreviewWidth > 0.0f && MaxPreviewHeight > 0.0f)
				{
					if(Line.m_MediaState == EMediaState::READY && Line.m_MediaWidth > 0 && Line.m_MediaHeight > 0 && !Line.m_vMediaFrames.empty())
					{
						const float ScaleByWidth = MaxPreviewWidth / (float)Line.m_MediaWidth;
						const float ScaleByHeight = MaxPreviewHeight / (float)Line.m_MediaHeight;
						float Scale = minimum(1.0f, minimum(ScaleByWidth, ScaleByHeight));
						float PreviewW = maximum(1.0f, (float)Line.m_MediaWidth * Scale);
						float PreviewH = maximum(1.0f, (float)Line.m_MediaHeight * Scale);
						if(PreviewW < CHAT_MEDIA_MIN_PREVIEW_SIDE || PreviewH < CHAT_MEDIA_MIN_PREVIEW_SIDE)
						{
							const float UpscaleByW = CHAT_MEDIA_MIN_PREVIEW_SIDE / PreviewW;
							const float UpscaleByH = CHAT_MEDIA_MIN_PREVIEW_SIDE / PreviewH;
							const float Upscale = maximum(UpscaleByW, UpscaleByH);
							const float MaxUpscale = minimum(MaxPreviewWidth / PreviewW, MaxPreviewHeight / PreviewH);
							if(MaxUpscale > 1.0f)
							{
								const float UseUpscale = minimum(Upscale, MaxUpscale);
								PreviewW *= UseUpscale;
								PreviewH *= UseUpscale;
							}
						}
						Line.m_aMediaPreviewWidth[OffsetType] = maximum(1.0f, PreviewW);
						Line.m_aMediaPreviewHeight[OffsetType] = maximum(1.0f, PreviewH);
					}
					else
					{
						Line.m_aMediaPreviewWidth[OffsetType] = MaxPreviewWidth;
						Line.m_aMediaPreviewHeight[OffsetType] = maximum(FontSize * 1.6f, 18.0f);
					}
					TotalHeight += FontSize * 0.4f + Line.m_aMediaPreviewHeight[OffsetType];
				}
			}
			else if(ShowMediaSlot && (Line.m_MediaState == EMediaState::QUEUED || Line.m_MediaState == EMediaState::LOADING || Line.m_MediaState == EMediaState::DECODING))
			{
				Line.m_aMediaPreviewWidth[OffsetType] = minimum(LineWidth, (float)g_Config.m_BcChatMediaPreviewMaxWidth);
				Line.m_aMediaPreviewHeight[OffsetType] = maximum(FontSize * 1.2f, 12.0f);
				TotalHeight += FontSize * 0.4f + Line.m_aMediaPreviewHeight[OffsetType];
			}
			else if(ShowMediaSlot && Line.m_MediaState == EMediaState::FAILED)
			{
				Line.m_aMediaPreviewWidth[OffsetType] = minimum(LineWidth, (float)g_Config.m_BcChatMediaPreviewMaxWidth);
				Line.m_aMediaPreviewHeight[OffsetType] = maximum(FontSize * 2.1f, 18.0f);
				TotalHeight += FontSize * 0.4f + Line.m_aMediaPreviewHeight[OffsetType];
			}

			Line.m_aYOffset[OffsetType] = TotalHeight;
		}

		y -= Line.m_aYOffset[OffsetType];

		// cut off if msgs waste too much space
		if(y < HeightLimit && i != StartLine)
			break;

		// the position the text was created
		Line.m_TextYOffset = y + RealMsgPaddingY / 2.0f;

		int CurRenderFlags = TextRender()->GetRenderFlags();
		TextRender()->SetRenderFlags(CurRenderFlags | ETextRenderFlags::TEXT_RENDER_FLAG_NO_AUTOMATIC_QUAD_UPLOAD);

		// reset the cursor
		CTextCursor LineCursor;
		LineCursor.SetPosition(vec2(TextBegin, Line.m_TextYOffset));
		LineCursor.m_FontSize = FontSize;
		LineCursor.m_LineWidth = LineWidth;
		if(ChatInteractionActive && !m_Input.HasSelection() && (m_MouseIsPress || m_HasSelection || m_WantsSelectionCopy))
		{
			LineCursor.m_CalculateSelectionMode = TEXT_CURSOR_SELECTION_MODE_CALCULATE;
			LineCursor.m_PressMouse = m_MousePress;
			LineCursor.m_ReleaseMouse = m_MouseRelease;
		}

		// Message is from valid player
		if(Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
		{
			LineCursor.m_X += RealMsgPaddingTee;

			if(Line.m_Friend && g_Config.m_ClMessageFriend)
			{
				TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageFriendColor)).WithAlpha(1.0f));
				TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, "♥ ");
			}
		}

		// render name
		ColorRGBA NameColor;
		if(Line.m_CustomColor)
			NameColor = *Line.m_CustomColor;
		else if(Line.m_ClientId == SERVER_MSG)
			NameColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageSystemColor));
		else if(Line.m_ClientId == CLIENT_MSG)
			NameColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageClientColor));
		else if(Line.m_ClientId >= 0 && g_Config.m_TcWarList && g_Config.m_TcWarListChat && GameClient()->m_WarList.GetAnyWar(Line.m_ClientId)) // BestClient
			NameColor = GameClient()->m_WarList.GetPriorityColor(Line.m_ClientId);
		else if(Line.m_Team)
			NameColor = CalculateNameColor(ColorHSLA(g_Config.m_ClMessageTeamColor));
		else if(Line.m_NameColor == TEAM_RED)
			NameColor = ColorRGBA(1.0f, 0.5f, 0.5f, 1.0f);
		else if(Line.m_NameColor == TEAM_BLUE)
			NameColor = ColorRGBA(0.7f, 0.7f, 1.0f, 1.0f);
		else if(Line.m_NameColor == TEAM_SPECTATORS)
			NameColor = ColorRGBA(0.75f, 0.5f, 0.75f, 1.0f);
		else if(Line.m_ClientId >= 0 && g_Config.m_ClChatTeamColors && GameClient()->m_Teams.Team(Line.m_ClientId))
			NameColor = GameClient()->GetDDTeamColor(GameClient()->m_Teams.Team(Line.m_ClientId), 0.75f);
		else
			NameColor = ColorRGBA(0.8f, 0.8f, 0.8f, 1.0f);

		TextRender()->TextColor(NameColor);
		TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, aClientId);
		TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, Line.m_aName);

		if(Line.m_TimesRepeated > 0)
		{
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.3f);
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, aCount);
		}

		if(Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
		{
			TextRender()->TextColor(NameColor);
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, ": ");
		}

		ColorRGBA Color;
		if(Line.m_CustomColor)
			Color = *Line.m_CustomColor;
		else if(Line.m_ClientId == SERVER_MSG)
			Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageSystemColor));
		else if(Line.m_ClientId == CLIENT_MSG)
			Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageClientColor));
		else if(Line.m_Highlighted)
			Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageHighlightColor));
		else if(Line.m_Team)
			Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageTeamColor));
		else // regular message
			Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageColor));
		TextRender()->TextColor(Color);

		const float PrefixWidth = LineCursor.m_LongestLineWidth;
		LineCursor.m_LongestLineWidth = 0.0f;
		if(!IsScoreBoardOpen && !g_Config.m_ClChatOld)
		{
			LineCursor.m_StartX = LineCursor.m_X;
			LineCursor.m_LineWidth -= PrefixWidth;
		}

		if(pDisplayedTranslatedText)
		{
			const float TranslateRectX = LineCursor.m_X;
			const float TranslateRectY = LineCursor.m_Y;
			const STextBoundingBox TranslatedBoundingBox = TextRender()->TextBoundingBox(FontSize, pDisplayedTranslatedText, -1, maximum(1.0f, LineCursor.m_LineWidth));
			Line.m_TranslateRect.m_X = TranslateRectX;
			Line.m_TranslateRect.m_Y = TranslateRectY;
			Line.m_TranslateRect.m_W = maximum(1.0f, TranslatedBoundingBox.m_W);
			Line.m_TranslateRect.m_H = maximum(FontSize, TranslatedBoundingBox.m_H);
			Line.m_TranslateRectValid = true;
			Line.m_TranslateLanguageRectValid = false;
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, pDisplayedTranslatedText);
			if(pDisplayedTranslatedLanguage)
			{
				ColorRGBA ColorLang = Color;
				ColorLang.r *= 0.8f;
				ColorLang.g *= 0.8f;
				ColorLang.b *= 0.8f;
				TextRender()->TextColor(ColorLang);
				TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, " [");
				const float RectX = LineCursor.m_X;
				const float RectY = LineCursor.m_Y;
				TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, pDisplayedTranslatedLanguage);
				Line.m_TranslateLanguageRect.m_X = RectX;
				Line.m_TranslateLanguageRect.m_Y = RectY;
				Line.m_TranslateLanguageRect.m_W = maximum(1.0f, LineCursor.m_X - RectX);
				Line.m_TranslateLanguageRect.m_H = FontSize;
				Line.m_TranslateLanguageRectValid = true;
				TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, "]");
			}
			TextRender()->TextColor(Color);
		}
		else if(pTranslatedError)
		{
			Line.m_TranslateRectValid = false;
			Line.m_TranslateLanguageRectValid = false;
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, pText);
			ColorRGBA ColorSub = Color;
			ColorSub.r = 0.7f;
			ColorSub.g = 0.6f;
			ColorSub.b = 0.6f;
			TextRender()->TextColor(ColorSub);
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, "\n");
			LineCursor.m_FontSize *= 0.8f;
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, pTranslatedError);
			LineCursor.m_FontSize /= 0.8f;
			TextRender()->TextColor(Color);
		}
		else
		{
			Line.m_TranslateRectValid = false;
			Line.m_TranslateLanguageRectValid = false;
			ColoredParts.AddSplitsToCursor(LineCursor);
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, pText);
			LineCursor.m_vColorSplits.clear();
		}

		Line.m_SelectionStart = LineCursor.m_SelectionStart;
		Line.m_SelectionEnd = LineCursor.m_SelectionEnd;

		if(!g_Config.m_ClChatOld && (Line.m_aText[0] != '\0' || Line.m_aName[0] != '\0'))
		{
			float FullWidth = RealMsgPaddingX * 1.5f;
			if(!IsScoreBoardOpen && !g_Config.m_ClChatOld)
			{
				FullWidth += PrefixWidth + LineCursor.m_LongestLineWidth;
			}
			else
			{
				FullWidth += maximum(PrefixWidth, LineCursor.m_LongestLineWidth);
			}
			if(Line.m_aMediaPreviewWidth[OffsetType] > 0.0f)
			{
				const float PreviewWidth = Line.m_aMediaPreviewWidth[OffsetType] + (TextBegin - Begin) + RealMsgPaddingX;
				FullWidth = maximum(FullWidth, PreviewWidth);
			}
			Graphics()->SetColor(1, 1, 1, 1);
			Line.m_QuadContainerIndex = Graphics()->CreateRectQuadContainer(Begin, y, FullWidth, Line.m_aYOffset[OffsetType], MessageRounding(), IGraphics::CORNER_ALL);
		}

		TextRender()->SetRenderFlags(CurRenderFlags);
		if(Line.m_TextContainerIndex.Valid())
			TextRender()->UploadTextContainer(Line.m_TextContainerIndex);
	}

	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

void CChat::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	// send pending chat messages
	if(!m_vPendingChatQueue.empty() && m_LastChatSend + time_freq() < time())
	{
		const CPendingChatEntry Entry = m_vPendingChatQueue.front();
		m_vPendingChatQueue.erase(m_vPendingChatQueue.begin());
		SendChat(Entry.m_Team, Entry.m_aText);
	}

	UpdateMediaDownloads();
	if(m_MediaViewerOpen && (!g_Config.m_BcChatMediaPreview || !g_Config.m_BcChatMediaViewer))
		CloseMediaViewer();

	const float Height = 300.0f;
	const float Width = Height * Graphics()->ScreenAspect();
	Graphics()->MapScreen(0.0f, 0.0f, Width, Height);

	const bool BcChatMessageAnimEnabled = BCUiAnimations::Enabled() && g_Config.m_BcChatAnimation != 0;
	float x = 5.0f;
	const vec2 WindowSize(maximum(1.0f, (float)Graphics()->WindowWidth()), maximum(1.0f, (float)Graphics()->WindowHeight()));
	const vec2 UiMousePos = Ui()->UpdatedMousePos() * vec2(Ui()->Screen()->w, Ui()->Screen()->h) / WindowSize;
	const vec2 UiToChatScale(Width / Ui()->Screen()->w, Height / Ui()->Screen()->h);
	const vec2 MousePos = UiMousePos * UiToChatScale;
	const bool MouseDown = Input()->KeyIsPressed(KEY_MOUSE_1);
	int HoveredTranslateLineIndex = -1;
	for(int LineIndex = 0; LineIndex < MAX_LINES; ++LineIndex)
	{
		const CLine &Line = m_aLines[LineIndex];
		if(!Line.m_TranslateRectValid && !Line.m_TranslateLanguageRectValid)
			continue;
		const bool HoveredTranslatedText = Line.m_TranslateRectValid &&
			MousePos.x >= Line.m_TranslateRect.m_X && MousePos.x <= Line.m_TranslateRect.m_X + Line.m_TranslateRect.m_W &&
			MousePos.y >= Line.m_TranslateRect.m_Y && MousePos.y <= Line.m_TranslateRect.m_Y + Line.m_TranslateRect.m_H;
		const bool HoveredLanguage = Line.m_TranslateLanguageRectValid &&
			MousePos.x >= Line.m_TranslateLanguageRect.m_X && MousePos.x <= Line.m_TranslateLanguageRect.m_X + Line.m_TranslateLanguageRect.m_W &&
			MousePos.y >= Line.m_TranslateLanguageRect.m_Y && MousePos.y <= Line.m_TranslateLanguageRect.m_Y + Line.m_TranslateLanguageRect.m_H;
		if(HoveredTranslatedText || HoveredLanguage)
		{
			HoveredTranslateLineIndex = LineIndex;
			break;
		}
	}
	for(auto &Line : m_aLines)
	{
		Line.m_NameRectValid = false;
		Line.m_TranslateRectValid = false;
		Line.m_TranslateLanguageRectValid = false;
		Line.m_MediaPreviewRectValid = false;
		Line.m_MediaRetryRectValid = false;
	}
	m_TranslateButtonRectValid = false;
	// BestClient
	float y = 300.0f - (20.0f * FontSize() / 6.0f + (g_Config.m_TcStatusBar ? g_Config.m_TcStatusBarHeight : 0.0f));
	// float y = 300.0f - 20.0f * FontSize() / 6.0f;
	float ScaledFontSize = FontSize() * (8.0f / 6.0f);
	const bool BcChatOpenAnimEnabled = BcChatMessageAnimEnabled && g_Config.m_BcChatOpenAnimation != 0 && g_Config.m_BcChatOpenAnimationMs > 0;
	const bool BcChatTypingAnimEnabled = BcChatMessageAnimEnabled && g_Config.m_BcChatTypingAnimation != 0 && g_Config.m_BcChatTypingAnimationMs > 0;
	float ChatOpenOffsetX = 0.0f;
	if(m_Mode != MODE_NONE && BcChatOpenAnimEnabled && m_ChatOpenAnimationStart > 0)
	{
		const float Dur = BCUiAnimations::MsToSeconds(g_Config.m_BcChatOpenAnimationMs);
		const float Age = (time_get() - m_ChatOpenAnimationStart) / (float)time_freq();
		const float Progress = Dur > 0.0f ? std::clamp(Age / Dur, 0.0f, 1.0f) : 1.0f;
		const float ChatOpenEase = BCUiAnimations::EaseInOutQuart(Progress);
		ChatOpenOffsetX = -(x + maximum(Width - 190.0f, 190.0f) + 24.0f) * (1.0f - ChatOpenEase);
	}
	const bool ChatInteractionActive = m_Mode != MODE_NONE || m_Show;
	if(m_MediaViewerOpen && !ChatInteractionActive)
		CloseMediaViewer();
	if(!ChatInteractionActive)
	{
		m_MouseIsPress = false;
		m_HasSelection = false;
		m_WantsSelectionCopy = false;
	}
	if(ChatInteractionActive)
	{
		if(!m_MediaViewerOpen && !m_ScrollbarDragging)
		{
			if(!m_MouseIsPress && MouseDown)
			{
				m_MouseIsPress = true;
				m_MousePress = MousePos;
				m_MouseRelease = MousePos;
				m_HasSelection = false;
			}
			else if(m_MouseIsPress && !MouseDown)
			{
				m_MouseIsPress = false;
			}
			if(m_MouseIsPress)
				m_MouseRelease = MousePos;
		}
		else
		{
			m_MouseIsPress = false;
		}

		if(m_Mode != MODE_NONE)
		{
			// render chat input
			CTextCursor InputCursor;
			InputCursor.SetPosition(vec2(x + ChatOpenOffsetX, y));
			InputCursor.m_FontSize = ScaledFontSize;
			InputCursor.m_LineWidth = Width - 190.0f;

		// BestClient
		InputCursor.m_LineWidth = std::max(Width - 190.0f, 190.0f);

		if(m_Mode == MODE_ALL)
			TextRender()->TextEx(&InputCursor, Localize("All"));
		else if(m_Mode == MODE_TEAM)
			TextRender()->TextEx(&InputCursor, Localize("Team"));
		else
			TextRender()->TextEx(&InputCursor, Localize("Chat"));

		TextRender()->TextEx(&InputCursor, ": ");

		const float TranslateButtonSize = maximum(16.0f, ScaledFontSize * 1.35f);
		const float TranslateButtonGap = 4.0f;
		const float MessageMaxWidth = maximum(40.0f, InputCursor.m_LineWidth - (InputCursor.m_X - InputCursor.m_StartX) - TranslateButtonSize - TranslateButtonGap);
		const CUIRect ClippingRect = {InputCursor.m_X, InputCursor.m_Y, MessageMaxWidth, 2.25f * InputCursor.m_FontSize};
		const float TypingTravel = 30.0f;
		const CUIRect ChatInputClipRect = {0.0f, ClippingRect.y - TypingTravel, Width, ClippingRect.h + TypingTravel};
		const float XScale = Graphics()->ScreenWidth() / Width;
		const float YScale = Graphics()->ScreenHeight() / Height;
		Graphics()->ClipEnable((int)(ChatInputClipRect.x * XScale), (int)(ChatInputClipRect.y * YScale), (int)(ChatInputClipRect.w * XScale), (int)(ChatInputClipRect.h * YScale));

		float ScrollOffset = m_Input.GetScrollOffset();
		float ScrollOffsetChange = m_Input.GetScrollOffsetChange();
		CLineInput::SMouseSelection *pMouseSelection = m_Input.GetMouseSelection();
		const bool InputInside = MousePos.x >= ClippingRect.x && MousePos.x <= ClippingRect.x + ClippingRect.w &&
			MousePos.y >= ClippingRect.y && MousePos.y <= ClippingRect.y + ClippingRect.h;
		if(InputInside && m_MouseIsPress)
		{
			pMouseSelection->m_Selecting = true;
			pMouseSelection->m_PressMouse = m_MousePress;
			pMouseSelection->m_ReleaseMouse = m_MouseRelease;
			pMouseSelection->m_Offset.y = ScrollOffset;
			m_HasSelection = false;
		}
		else if(!m_MouseIsPress)
		{
			pMouseSelection->m_Selecting = false;
		}
		if(ScrollOffset != pMouseSelection->m_Offset.y)
		{
			pMouseSelection->m_PressMouse.y -= ScrollOffset - pMouseSelection->m_Offset.y;
			pMouseSelection->m_Offset.y = ScrollOffset;
		}

			m_Input.Activate(EInputPriority::CHAT); // Ensure that the input is active
			const CUIRect InputCursorRect = {InputCursor.m_X, InputCursor.m_Y - ScrollOffset, 0.0f, 0.0f};
			const bool WasChanged = m_Input.WasChanged();
			const bool WasCursorChanged = m_Input.WasCursorChanged();
			const bool Changed = WasChanged || WasCursorChanged;

			char aDisplayedInputText[MAX_LINE_LENGTH];
			str_copy(aDisplayedInputText, m_Input.GetDisplayedString(), sizeof(aDisplayedInputText));
			const float TypingAnimDuration = BCUiAnimations::MsToSeconds(g_Config.m_BcChatTypingAnimationMs);
			std::vector<STextColorSplit> vTypingColorSplits;
			std::vector<CChat::STypingGlyphAnim> vActiveTypingGlyphAnims;
			if(BcChatTypingAnimEnabled && TypingAnimDuration > 0.0f && aDisplayedInputText[0] != '\0' && ChatTypingAnimSupportsText(aDisplayedInputText))
			{
				for(auto It = m_vTypingGlyphAnims.begin(); It != m_vTypingGlyphAnims.end();)
				{
					const float TypingAnimAge = (time_get() - It->m_StartTime) / (float)time_freq();
					const int StartByte = It->m_ByteIndex;
					const int GlyphBytes = It->m_ByteLength;
					const int StoredGlyphBytes = str_length(It->m_aText);
					const bool Valid =
						TypingAnimAge < TypingAnimDuration &&
						It->m_ByteIndex >= 0 &&
						GlyphBytes > 0 &&
						StartByte + GlyphBytes <= str_length(aDisplayedInputText) &&
						StoredGlyphBytes == GlyphBytes &&
						str_comp_num(It->m_aText, aDisplayedInputText + StartByte, GlyphBytes) == 0;
					if(!Valid)
					{
						It = m_vTypingGlyphAnims.erase(It);
						continue;
					}

					vActiveTypingGlyphAnims.push_back(*It);
					vTypingColorSplits.emplace_back(It->m_ByteIndex, It->m_ByteLength, ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f));
					++It;
				}
			}

			// Color splits can hide the fill color, but the outline would still be drawn for hidden glyphs.
			// Temporarily disable outline for the base pass so the animated overlay is the only visible glyph.
			const bool DisableBaseOutline = !vTypingColorSplits.empty();
			if(DisableBaseOutline)
				TextRender()->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f));
			const STextBoundingBox BoundingBox = m_Input.Render(&InputCursorRect, InputCursor.m_FontSize, TEXTALIGN_TL, Changed, MessageMaxWidth, 0.0f, vTypingColorSplits);
			if(DisableBaseOutline)
				TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());

			for(const auto &TypingGlyphAnim : vActiveTypingGlyphAnims)
			{
				const float TypingAnimAge = (time_get() - TypingGlyphAnim.m_StartTime) / (float)time_freq();
				const float Progress = std::clamp(TypingAnimAge / TypingAnimDuration, 0.0f, 1.0f);
				const float Ease = BCUiAnimations::EaseInOutQuart(Progress);
				const float OverlayYOffset = -4.5f * (1.0f - Ease);
				const int PrefixBytes = TypingGlyphAnim.m_ByteIndex;
				char aPrefixText[MAX_LINE_LENGTH] = "";
				if(PrefixBytes < 0 || PrefixBytes > str_length(aDisplayedInputText))
					continue;
				str_truncate(aPrefixText, sizeof(aPrefixText), aDisplayedInputText, PrefixBytes);

				CTextCursor MeasureCursor;
				MeasureCursor.SetPosition(vec2(InputCursorRect.x, InputCursorRect.y));
				MeasureCursor.m_FontSize = InputCursor.m_FontSize;
				MeasureCursor.m_LineWidth = MessageMaxWidth;
				TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f));
				TextRender()->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f));
				TextRender()->TextEx(&MeasureCursor, aPrefixText);

				CTextCursor OverlayCursor;
				OverlayCursor.SetPosition(vec2(MeasureCursor.m_X, MeasureCursor.m_Y + OverlayYOffset));
				OverlayCursor.m_FontSize = InputCursor.m_FontSize;
				OverlayCursor.m_LineWidth = MessageMaxWidth;
				TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.75f + 0.25f * Ease));
				TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor().WithMultipliedAlpha(0.75f + 0.25f * Ease));
				TextRender()->TextEx(&OverlayCursor, TypingGlyphAnim.m_aText);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
			TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());

		Graphics()->ClipDisable();

		CUIRect TranslateButtonRect = {ClippingRect.x + ClippingRect.w + TranslateButtonGap, ClippingRect.y, TranslateButtonSize, maximum(InputCursor.m_FontSize + 4.0f, 16.0f)};
		RenderTranslateSettingsButton(TranslateButtonRect);
		if(Ui()->HotItem() == &m_TranslateSettingsButton || m_TranslateButtonPressed)
		{
			m_MouseIsPress = false;
			m_HasSelection = false;
		}

		// Scroll up or down to keep the caret inside the clipping rect
		const float CaretPositionY = m_Input.GetCaretPosition().y - ScrollOffsetChange;
		if(CaretPositionY < ClippingRect.y)
			ScrollOffsetChange -= ClippingRect.y - CaretPositionY;
		else if(CaretPositionY + InputCursor.m_FontSize > ClippingRect.y + ClippingRect.h)
			ScrollOffsetChange += CaretPositionY + InputCursor.m_FontSize - (ClippingRect.y + ClippingRect.h);

		Ui()->DoSmoothScrollLogic(&ScrollOffset, &ScrollOffsetChange, ClippingRect.h, BoundingBox.m_H);

		m_Input.SetScrollOffset(ScrollOffset);
		m_Input.SetScrollOffsetChange(ScrollOffsetChange);
		if(m_Input.HasSelection())
			m_HasSelection = false;

		// Autocompletion hint
		if(m_Input.GetString()[0] == '/' && m_Input.GetString()[1] != '\0' && !m_vServerCommands.empty())
		{
			for(const auto &Command : m_vServerCommands)
			{
				if(str_startswith_nocase(Command.m_aName, m_Input.GetString() + 1))
				{
					InputCursor.m_X = InputCursor.m_X + TextRender()->TextWidth(InputCursor.m_FontSize, m_Input.GetString(), -1, InputCursor.m_LineWidth);
					InputCursor.m_Y = m_Input.GetCaretPosition().y;
					TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.5f);
					TextRender()->TextEx(&InputCursor, Command.m_aName + str_length(m_Input.GetString() + 1));
					TextRender()->TextColor(TextRender()->DefaultTextColor());
					break;
				}
			}
		}
		else if(m_Input.GetString()[0] == '!' && m_Input.GetString()[1] != '\0')
		{
			const char *pIn = m_Input.GetString();
			bool HasSpace = false;
			for(const char *pScan = pIn; *pScan; ++pScan)
			{
				if(std::isspace((unsigned char)*pScan))
				{
					HasSpace = true;
					break;
				}
			}
			if(!HasSpace)
			{
				const char *apCmds[] = {"!voice", "!voicegroup"};
				const char *pCandidate = nullptr;
				for(const char *pCmd : apCmds)
				{
					if(str_startswith_nocase(pCmd, pIn))
					{
						pCandidate = pCmd;
						break;
					}
				}
				if(pCandidate && str_length(pCandidate) > str_length(pIn))
				{
					InputCursor.m_X = InputCursor.m_X + TextRender()->TextWidth(InputCursor.m_FontSize, pIn, -1, InputCursor.m_LineWidth);
					InputCursor.m_Y = m_Input.GetCaretPosition().y;
					TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.5f);
					TextRender()->TextEx(&InputCursor, pCandidate + str_length(pIn));
					TextRender()->TextColor(TextRender()->DefaultTextColor());
				}
			}
		}
	}
	}

#if defined(CONF_VIDEORECORDER)
	if(!((g_Config.m_ClShowChat && !IVideo::Current()) || (g_Config.m_ClVideoShowChat && IVideo::Current())))
#else
	if(!g_Config.m_ClShowChat)
#endif
		return;

	// Check focus mode settings
	if(g_Config.m_ClFocusMode && g_Config.m_ClFocusModeHideChat)
		return;

	y -= ScaledFontSize;
	bool IsScoreBoardOpen = GameClient()->m_Scoreboard.IsActive() && (Graphics()->ScreenAspect() > 1.7f); // only assume scoreboard when screen ratio is widescreen(something around 16:9)
	const bool ShowLargeArea = m_Show || (m_Mode != MODE_NONE && g_Config.m_ClShowChat == 1) || g_Config.m_ClShowChat == 2;
	const bool KeepLinesAlive = m_MediaViewerOpen && ValidateMediaViewerLine();

	int64_t Now = time();
	float HeightLimit = IsScoreBoardOpen ? 180.0f : (ShowLargeArea ? 50.0f : 200.0f);
	int OffsetType = IsScoreBoardOpen ? 1 : 0;

	float RealMsgPaddingX = MessagePaddingX();
	float RealMsgPaddingY = MessagePaddingY();
	float RealMsgPaddingTee = MessageTeeSize() + MESSAGE_TEE_PADDING_RIGHT;

	if(g_Config.m_ClChatOld)
	{
		RealMsgPaddingX = 0;
		RealMsgPaddingY = 0;
		RealMsgPaddingTee = 0;
	}

	int TotalLines = 0;
	for(int i = 0; i < MAX_LINES; i++)
	{
		CLine &Line = m_aLines[((m_CurrentLine - i) + MAX_LINES) % MAX_LINES];
		if(!Line.m_Initialized)
			break;
		if(Now > Line.m_Time + 16 * time_freq() && !ShowLargeArea && !KeepLinesAlive)
			break;
		++TotalLines;
	}

	const auto CountVisibleLines = [&](int StartLine) {
		int VisibleLines = 0;
		float TmpY = y;
		for(int i = StartLine; i < TotalLines; i++)
		{
			CLine &Line = m_aLines[((m_CurrentLine - i) + MAX_LINES) % MAX_LINES];
			const float LineHeight = Line.m_aYOffset[OffsetType] > 0.0f ? Line.m_aYOffset[OffsetType] : (FontSize() + RealMsgPaddingY);
			TmpY -= LineHeight;
			if(TmpY < HeightLimit)
			{
				if(VisibleLines == 0)
					++VisibleLines;
				break;
			}
			++VisibleLines;
		}
		return maximum(1, VisibleLines);
	};

	m_BacklogCurLine = maximum(0, minimum(m_BacklogCurLine, maximum(0, TotalLines - 1)));
	const int VisibleLines = CountVisibleLines(m_BacklogCurLine);
	const int MaxScroll = maximum(0, TotalLines - VisibleLines);
	m_BacklogCurLine = maximum(0, minimum(m_BacklogCurLine, MaxScroll));

	if(ChatInteractionActive && MaxScroll > 0)
	{
		const float LogTop = HeightLimit;
		const float LogBottom = y;
		const float LogHeight = maximum(0.0f, LogBottom - LogTop);
		const float RailMargin = 1.0f;
		const float RailWidth = maximum(0.0f, CHAT_SCROLLBAR_WIDTH - 2.0f * RailMargin);
		const float MinRailHeight = RailWidth * 3.0f;
		const float MinScrollbarHeight = MinRailHeight + 2.0f * RailMargin;
		if(LogHeight >= MinScrollbarHeight && RailWidth > 0.0f)
		{
			const float Current = 1.0f - (float)m_BacklogCurLine / (float)MaxScroll;
			CUIRect ScrollbarRect;
			ScrollbarRect.x = x + ChatOpenOffsetX - CHAT_SCROLLBAR_WIDTH - CHAT_SCROLLBAR_MARGIN;
			ScrollbarRect.y = LogTop;
			ScrollbarRect.w = CHAT_SCROLLBAR_WIDTH;
			ScrollbarRect.h = LogHeight;

			CUIRect Rail;
			ScrollbarRect.Margin(RailMargin, &Rail);
			CUIRect Handle;
			const float HandleHeight = maximum(Rail.w, minimum(24.0f, Rail.h / 3.0f));
			Rail.HSplitTop(HandleHeight, &Handle, nullptr);
			Handle.y = Rail.y + (Rail.h - Handle.h) * Current;

			const auto InsideRect = [&](const CUIRect &Rect) {
				return MousePos.x >= Rect.x && MousePos.x <= Rect.x + Rect.w && MousePos.y >= Rect.y && MousePos.y <= Rect.y + Rect.h;
			};

			if(!MouseDown)
			{
				m_ScrollbarDragging = false;
			}
			else if(!m_ScrollbarDragging && InsideRect(Rail))
			{
				if(InsideRect(Handle))
					m_ScrollbarDragOffset = MousePos.y - Handle.y;
				else
					m_ScrollbarDragOffset = Handle.h / 2.0f;
				m_ScrollbarDragging = true;
				m_MouseIsPress = false;
				m_HasSelection = false;
			}

			float NewValue = Current;
			if(m_ScrollbarDragging)
			{
				const float ScrollableHeight = Rail.h - Handle.h;
				if(ScrollableHeight > 0.0f)
				{
					const float Cur = MousePos.y - m_ScrollbarDragOffset;
					NewValue = maximum(0.0f, minimum((Cur - Rail.y) / ScrollableHeight, 1.0f));
				}
			}

			const int NewLine = maximum(0, minimum((int)((1.0f - NewValue) * MaxScroll + 0.5f), MaxScroll));
			m_BacklogCurLine = NewLine;

			Rail.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), IGraphics::CORNER_ALL, Rail.w / 2.0f);
			const ColorRGBA HandleColor = m_ScrollbarDragging ? ColorRGBA(0.8f, 0.8f, 0.8f, 1.0f) : ColorRGBA(0.6f, 0.6f, 0.6f, 1.0f);
			Handle.Draw(HandleColor, IGraphics::CORNER_ALL, Handle.w / 2.0f);
		}
	}
	else
	{
		m_ScrollbarDragging = false;
	}

	OnPrepareLines(y, m_BacklogCurLine, HoveredTranslateLineIndex);
	std::string SelectionString;
	bool HasChatSelection = false;

	for(int i = m_BacklogCurLine; i < MAX_LINES; i++)
	{
		const int LineIndex = ((m_CurrentLine - i) + MAX_LINES) % MAX_LINES;
		CLine &Line = m_aLines[LineIndex];
		if(!Line.m_Initialized)
			break;
		if(Now > Line.m_Time + 16 * time_freq() && !m_PrevShowChat && !KeepLinesAlive)
			break;

		y -= Line.m_aYOffset[OffsetType];

		// cut off if msgs waste too much space
		if(y < HeightLimit && i != m_BacklogCurLine)
			break;

		float Blend = Now > Line.m_Time + 14 * time_freq() && !m_PrevShowChat ? 1.0f - (Now - Line.m_Time - 14 * time_freq()) / (2.0f * time_freq()) : 1.0f;
		if(KeepLinesAlive && LineIndex == m_MediaViewerLineIndex)
			Blend = 1.0f;

		// BestClient: lift newly received messages from the bottom.
		float BcLineXOffset = 0.0f;
		float BcLineYOffset = 0.0f;
		if(BcChatMessageAnimEnabled && g_Config.m_BcChatAnimationMs > 0 && Line.m_Time > 0)
		{
			const float Dur = BCUiAnimations::MsToSeconds(g_Config.m_BcChatAnimationMs);
			const float Age = (Now - Line.m_Time) / (float)time_freq();
			const float Progress = Dur > 0.0f ? std::clamp(Age / Dur, 0.0f, 1.0f) : 1.0f;
			const float Ease = BCUiAnimations::EaseInOutQuad(Progress);
			BcLineYOffset = 42.0f * (1.0f - Ease);
		}

		const float LineRenderX = x + ChatOpenOffsetX + BcLineXOffset;
		const float LineRenderY = y + BcLineYOffset;

		// Draw backgrounds for messages in one batch
		if(!g_Config.m_ClChatOld)
		{
			Graphics()->TextureClear();
			if(Line.m_QuadContainerIndex != -1)
			{
				Graphics()->SetColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClChatBackgroundColor, true)).WithMultipliedAlpha(Blend));
				Graphics()->RenderQuadContainerEx(Line.m_QuadContainerIndex, 0, -1, ChatOpenOffsetX + BcLineXOffset, ((LineRenderY + RealMsgPaddingY / 2.0f) - Line.m_TextYOffset));
			}
		}

		if(Line.m_TextContainerIndex.Valid())
		{
			if(!g_Config.m_ClChatOld && Line.m_pManagedTeeRenderInfo != nullptr)
			{
				CTeeRenderInfo &TeeRenderInfo = Line.m_pManagedTeeRenderInfo->TeeRenderInfo();
				const int TeeSize = MessageTeeSize();
				TeeRenderInfo.m_Size = TeeSize;

				float RowHeight = FontSize() + RealMsgPaddingY;
				float OffsetTeeY = TeeSize / 2.0f;
				float FullHeightMinusTee = RowHeight - TeeSize;

				const CAnimState *pIdleState = CAnimState::GetIdle();
				vec2 OffsetToMid;
				CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeRenderInfo, OffsetToMid);
				vec2 TeeRenderPos(LineRenderX + (RealMsgPaddingX + TeeSize) / 2.0f, LineRenderY + OffsetTeeY + FullHeightMinusTee / 2.0f + OffsetToMid.y);
				RenderTools()->RenderTee(pIdleState, &TeeRenderInfo, EMOTE_NORMAL, vec2(1, 0.1f), TeeRenderPos, Blend);
			}

			const ColorRGBA TextColor = TextRender()->DefaultTextColor().WithMultipliedAlpha(Blend);
			const ColorRGBA TextOutlineColor = TextRender()->DefaultTextOutlineColor().WithMultipliedAlpha(Blend);
			const float TextOffsetY = (LineRenderY + RealMsgPaddingY / 2.0f) - Line.m_TextYOffset;
			if(Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
			{
				char aClientId[16] = "";
				if(g_Config.m_ClShowIds)
					GameClient()->FormatClientId(Line.m_ClientId, aClientId, EClientIdFormat::INDENT_AUTO);

				float NameRectX = LineRenderX + RealMsgPaddingX / 2.0f + RealMsgPaddingTee;
				if(Line.m_Friend && g_Config.m_ClMessageFriend)
					NameRectX += TextRender()->TextWidth(FontSize(), "♥ ");
				NameRectX += TextRender()->TextWidth(FontSize(), aClientId);
				Line.m_NameRect.m_X = NameRectX;
				Line.m_NameRect.m_Y = Line.m_TextYOffset + TextOffsetY;
				Line.m_NameRect.m_W = maximum(1.0f, TextRender()->TextWidth(FontSize(), Line.m_aName));
				Line.m_NameRect.m_H = FontSize();
				Line.m_NameRectValid = true;
			}
			TextRender()->RenderTextContainer(Line.m_TextContainerIndex, TextColor, TextOutlineColor, ChatOpenOffsetX + BcLineXOffset, TextOffsetY);

			if(Line.m_TranslateRectValid || Line.m_TranslateLanguageRectValid)
			{
				const SRenderRect ActualTranslateRect = {
					Line.m_TranslateRect.m_X + ChatOpenOffsetX + BcLineXOffset,
					Line.m_TranslateRect.m_Y + TextOffsetY,
					Line.m_TranslateRect.m_W,
					Line.m_TranslateRect.m_H};
				Line.m_TranslateRect = ActualTranslateRect;
				CUIRect TranslateRectUi = {
					ActualTranslateRect.m_X / UiToChatScale.x,
					ActualTranslateRect.m_Y / UiToChatScale.y,
					ActualTranslateRect.m_W / UiToChatScale.x,
					ActualTranslateRect.m_H / UiToChatScale.y};
				CUIRect LanguageRectUi = {0.0f, 0.0f, 0.0f, 0.0f};
				const bool HoveredTranslatedMessage = Line.m_TranslateRectValid &&
					(Ui()->MouseHovered(&TranslateRectUi) ||
						(MousePos.x >= ActualTranslateRect.m_X && MousePos.x <= ActualTranslateRect.m_X + ActualTranslateRect.m_W &&
							MousePos.y >= ActualTranslateRect.m_Y && MousePos.y <= ActualTranslateRect.m_Y + ActualTranslateRect.m_H));

				bool HoveredLanguageTag = false;
				if(Line.m_TranslateLanguageRectValid)
				{
					const SRenderRect ActualLanguageRect = {
						Line.m_TranslateLanguageRect.m_X + ChatOpenOffsetX + BcLineXOffset,
						Line.m_TranslateLanguageRect.m_Y + TextOffsetY,
						Line.m_TranslateLanguageRect.m_W,
						Line.m_TranslateLanguageRect.m_H};
					Line.m_TranslateLanguageRect = ActualLanguageRect;
					LanguageRectUi = {
						ActualLanguageRect.m_X / UiToChatScale.x,
						ActualLanguageRect.m_Y / UiToChatScale.y,
						ActualLanguageRect.m_W / UiToChatScale.x,
						ActualLanguageRect.m_H / UiToChatScale.y};
					HoveredLanguageTag = Ui()->MouseHovered(&LanguageRectUi) ||
						(MousePos.x >= ActualLanguageRect.m_X && MousePos.x <= ActualLanguageRect.m_X + ActualLanguageRect.m_W &&
							MousePos.y >= ActualLanguageRect.m_Y && MousePos.y <= ActualLanguageRect.m_Y + ActualLanguageRect.m_H);
				}

				if(HoveredTranslatedMessage || HoveredLanguageTag)
					Ui()->SetHotItem(Line.m_TranslateLanguageRectValid ? (const void *)&Line.m_TranslateLanguageRect : (const void *)&Line.m_TranslateRect);

				if(HoveredTranslatedMessage || HoveredLanguageTag)
					HoveredTranslateLineIndex = LineIndex;
			}

			if(Line.m_SelectionStart >= 0 && Line.m_SelectionEnd >= 0 && Line.m_SelectionStart != Line.m_SelectionEnd)
			{
				HasChatSelection = true;
				if(m_WantsSelectionCopy)
				{
					const std::string PlainText = BuildPlainTextLine(Line);
					const int SelectionMin = minimum(Line.m_SelectionStart, Line.m_SelectionEnd);
					const int SelectionMax = maximum(Line.m_SelectionStart, Line.m_SelectionEnd);
					const size_t OffUTF8Start = str_utf8_offset_chars_to_bytes(PlainText.c_str(), SelectionMin);
					const size_t OffUTF8End = str_utf8_offset_chars_to_bytes(PlainText.c_str(), SelectionMax);
					const bool HasNewLine = !SelectionString.empty();
					SelectionString.insert(0, PlainText.substr(OffUTF8Start, OffUTF8End - OffUTF8Start) + (HasNewLine ? "\n" : ""));
				}
			}

				const bool ShowMediaSlot = ShouldDisplayMediaSlot(Line);
				const bool HideMediaPreview = ShouldHideMediaPreview(Line);
				Line.m_MediaPreviewRectValid = false;
				if(ShowMediaSlot && HideMediaPreview &&
					Line.m_aMediaPreviewWidth[OffsetType] > 0.0f && Line.m_aMediaPreviewHeight[OffsetType] > 0.0f)
				{
					const float PreviewX = LineRenderX + RealMsgPaddingX / 2.0f;
					const float PreviewY = Line.m_TextYOffset + TextOffsetY + Line.m_aTextHeight[OffsetType] + FontSize() * 0.4f;
					const float PreviewW = Line.m_aMediaPreviewWidth[OffsetType];
					const float PreviewH = Line.m_aMediaPreviewHeight[OffsetType];

					Graphics()->TextureClear();
					Graphics()->QuadsBegin();
					Graphics()->SetColor(0.10f, 0.10f, 0.10f, 0.82f * Blend);
					const IGraphics::CQuadItem HiddenQuad(PreviewX, PreviewY, PreviewW, PreviewH);
					Graphics()->QuadsDrawTL(&HiddenQuad, 1);
					Graphics()->QuadsEnd();

					CTextCursor HiddenCursor;
					const float HiddenFontSize = FontSize() * 0.72f;
					const float HiddenLabelWidth = TextRender()->TextWidth(HiddenFontSize, "hidden media");
					HiddenCursor.SetPosition(vec2(PreviewX + maximum(FontSize() * 0.35f, (PreviewW - HiddenLabelWidth) / 2.0f), PreviewY + maximum(FontSize() * 0.25f, (PreviewH - HiddenFontSize) / 2.0f)));
					HiddenCursor.m_FontSize = HiddenFontSize;
					TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.9f * Blend);
					TextRender()->TextEx(&HiddenCursor, "hidden media");
					TextRender()->TextColor(TextRender()->DefaultTextColor());

					Line.m_MediaRetryRectValid = false;
					if(ChatInteractionActive)
					{
						Line.m_MediaPreviewRect.m_X = PreviewX;
						Line.m_MediaPreviewRect.m_Y = PreviewY;
						Line.m_MediaPreviewRect.m_W = PreviewW;
						Line.m_MediaPreviewRect.m_H = PreviewH;
						Line.m_MediaPreviewRectValid = true;
					}
				}
				else if(ShowMediaSlot && Line.m_MediaState == EMediaState::READY &&
					Line.m_aMediaPreviewWidth[OffsetType] > 0.0f && Line.m_aMediaPreviewHeight[OffsetType] > 0.0f)
				{
					IGraphics::CTextureHandle MediaTexture;
					if(GetCurrentFrameTexture(Line, MediaTexture))
					{
						const float PreviewX = LineRenderX + RealMsgPaddingX / 2.0f;
						const float PreviewY = Line.m_TextYOffset + TextOffsetY + Line.m_aTextHeight[OffsetType] + FontSize() * 0.4f;
						const float PreviewW = Line.m_aMediaPreviewWidth[OffsetType];
						const float PreviewH = Line.m_aMediaPreviewHeight[OffsetType];

						Graphics()->WrapClamp();
						Graphics()->TextureSet(MediaTexture);
						Graphics()->QuadsBegin();
						Graphics()->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
						Graphics()->SetColor(1.0f, 1.0f, 1.0f, Blend);
						const IGraphics::CQuadItem QuadItem(PreviewX, PreviewY, PreviewW, PreviewH);
						Graphics()->QuadsDrawTL(&QuadItem, 1);
						Graphics()->QuadsEnd();
						Graphics()->WrapNormal();
						Graphics()->TextureClear();

						Line.m_MediaRetryRectValid = false;
						if(ChatInteractionActive && g_Config.m_BcChatMediaViewer)
						{
							Line.m_MediaPreviewRect.m_X = PreviewX;
							Line.m_MediaPreviewRect.m_Y = PreviewY;
							Line.m_MediaPreviewRect.m_W = PreviewW;
							Line.m_MediaPreviewRect.m_H = PreviewH;
							Line.m_MediaPreviewRectValid = true;
						}
					}
				}
				else if(ShowMediaSlot &&
					(Line.m_MediaState == EMediaState::QUEUED || Line.m_MediaState == EMediaState::LOADING || Line.m_MediaState == EMediaState::DECODING) &&
					Line.m_aMediaPreviewWidth[OffsetType] > 0.0f && Line.m_aMediaPreviewHeight[OffsetType] > 0.0f)
				{
					const float PreviewX = LineRenderX + RealMsgPaddingX / 2.0f;
					const float PreviewY = Line.m_TextYOffset + TextOffsetY + Line.m_aTextHeight[OffsetType] + FontSize() * 0.4f;
					const float PreviewW = Line.m_aMediaPreviewWidth[OffsetType];
					const float PreviewH = Line.m_aMediaPreviewHeight[OffsetType];

					Graphics()->TextureClear();
					Graphics()->QuadsBegin();
					Graphics()->SetColor(0.12f, 0.12f, 0.12f, 0.75f * Blend);
					const IGraphics::CQuadItem LoadingQuad(PreviewX, PreviewY, PreviewW, PreviewH);
					Graphics()->QuadsDrawTL(&LoadingQuad, 1);
					Graphics()->QuadsEnd();

					CTextCursor LoadingCursor;
					LoadingCursor.SetPosition(vec2(PreviewX + FontSize() * 0.35f, PreviewY + PreviewH * 0.15f));
					LoadingCursor.m_FontSize = FontSize() * 0.75f;
					TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.8f * Blend);
					TextRender()->TextEx(&LoadingCursor, "Loading media...");
					TextRender()->TextColor(TextRender()->DefaultTextColor());
					Line.m_MediaRetryRectValid = false;
				}
				else if(ShowMediaSlot &&
					Line.m_MediaState == EMediaState::FAILED &&
					Line.m_aMediaPreviewWidth[OffsetType] > 0.0f && Line.m_aMediaPreviewHeight[OffsetType] > 0.0f)
				{
					const float PreviewX = LineRenderX + RealMsgPaddingX / 2.0f;
					const float PreviewY = Line.m_TextYOffset + TextOffsetY + Line.m_aTextHeight[OffsetType] + FontSize() * 0.4f;
					const float PreviewW = Line.m_aMediaPreviewWidth[OffsetType];
					const float PreviewH = Line.m_aMediaPreviewHeight[OffsetType];
					const bool CanRetry = Line.m_MediaRetryCount < CHAT_MEDIA_MAX_RETRIES && !Line.m_vMediaCandidates.empty();

					Graphics()->TextureClear();
					Graphics()->QuadsBegin();
					Graphics()->SetColor(0.23f, 0.10f, 0.10f, 0.82f * Blend);
					const IGraphics::CQuadItem FailedQuad(PreviewX, PreviewY, PreviewW, PreviewH);
					Graphics()->QuadsDrawTL(&FailedQuad, 1);
					Graphics()->QuadsEnd();

					CTextCursor FailedCursor;
					FailedCursor.SetPosition(vec2(PreviewX + FontSize() * 0.35f, PreviewY + FontSize() * 0.25f));
					FailedCursor.m_FontSize = FontSize() * 0.70f;
					TextRender()->TextColor(1.0f, 0.85f, 0.85f, 0.95f * Blend);
					TextRender()->TextEx(&FailedCursor, CanRetry ? "Media preview unavailable" : "Media preview unavailable (retry limit reached)");

					const char *pRetryLabel = CanRetry ? "Retry" : "Retry limit reached";
					const float RetryFont = FontSize() * 0.66f;
					const float RetryLabelWidth = TextRender()->TextWidth(RetryFont, pRetryLabel);
					const float RetryW = maximum(FontSize() * 4.2f, RetryLabelWidth + FontSize() * 0.8f);
					const float RetryH = maximum(FontSize() * 0.95f, 12.0f);
					const float RetryX = PreviewX + PreviewW - RetryW - FontSize() * 0.25f;
					const float RetryY = PreviewY + PreviewH - RetryH - FontSize() * 0.25f;

					Graphics()->TextureClear();
					Graphics()->QuadsBegin();
					if(CanRetry)
						Graphics()->SetColor(0.86f, 0.28f, 0.28f, 0.95f * Blend);
					else
						Graphics()->SetColor(0.35f, 0.35f, 0.35f, 0.75f * Blend);
					const IGraphics::CQuadItem RetryQuad(RetryX, RetryY, RetryW, RetryH);
					Graphics()->QuadsDrawTL(&RetryQuad, 1);
					Graphics()->QuadsEnd();

					CTextCursor RetryCursor;
					RetryCursor.SetPosition(vec2(RetryX + (RetryW - RetryLabelWidth) / 2.0f, RetryY + (RetryH - RetryFont) / 2.0f));
					RetryCursor.m_FontSize = RetryFont;
					TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.95f * Blend);
					TextRender()->TextEx(&RetryCursor, pRetryLabel);
					TextRender()->TextColor(TextRender()->DefaultTextColor());

					Line.m_MediaRetryRectValid = CanRetry;
					if(CanRetry)
					{
						Line.m_MediaRetryRect.m_X = RetryX;
						Line.m_MediaRetryRect.m_Y = RetryY;
						Line.m_MediaRetryRect.m_W = RetryW;
						Line.m_MediaRetryRect.m_H = RetryH;
					}
				}
		}
	}

	m_HasSelection = HasChatSelection;
	if(m_Input.HasSelection())
		m_HasSelection = false;

	if(m_WantsSelectionCopy)
	{
		if(!SelectionString.empty())
			Input()->SetClipboardText(SelectionString.c_str());
		m_WantsSelectionCopy = false;
	}

	if(m_MediaViewerOpen && ValidateMediaViewerLine() && g_Config.m_BcChatMediaViewer)
	{
		CLine &ViewerLine = m_aLines[m_MediaViewerLineIndex];
		IGraphics::CTextureHandle MediaTexture;
		float ViewerX = 0.0f;
		float ViewerY = 0.0f;
		float ViewerW = 0.0f;
		float ViewerH = 0.0f;
		if(GetCurrentFrameTexture(ViewerLine, MediaTexture) && GetMediaViewerRect(ViewerLine, Width, Height, ViewerX, ViewerY, ViewerW, ViewerH))
		{
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.82f);
			const IGraphics::CQuadItem Backdrop(0.0f, 0.0f, Width, Height);
			Graphics()->QuadsDrawTL(&Backdrop, 1);
			Graphics()->QuadsEnd();

			Graphics()->WrapClamp();
			Graphics()->TextureSet(MediaTexture);
			Graphics()->QuadsBegin();
			Graphics()->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
			const IGraphics::CQuadItem ViewerQuad(ViewerX, ViewerY, ViewerW, ViewerH);
			Graphics()->QuadsDrawTL(&ViewerQuad, 1);
			Graphics()->QuadsEnd();
			Graphics()->WrapNormal();
			Graphics()->TextureClear();

			CTextCursor HintCursor;
			HintCursor.SetPosition(vec2(10.0f, Height - FontSize() * 1.8f));
			HintCursor.m_FontSize = FontSize() * 0.75f;
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.9f);
			TextRender()->TextEx(&HintCursor, "Esc - close, Wheel - zoom, Drag - move, Double click - reset");
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
	}

	if(m_Mode != MODE_NONE && Ui()->IsPopupOpen())
	{
		Ui()->StartCheck();
		Ui()->Update();
		Ui()->MapScreen();
		Ui()->RenderPopupMenus();
		Ui()->FinishCheck();
		Ui()->ClearHotkeys();
		Graphics()->MapScreen(0.0f, 0.0f, Width, Height);
	}

	if(ChatInteractionActive)
		RenderTools()->RenderCursor(UiMousePos * UiToChatScale, 12.0f);
}

void CChat::EnsureCoherentFontSize() const
{
	// Adjust font size based on width
	if(g_Config.m_ClChatWidth / (float)g_Config.m_ClChatFontSize >= CHAT_FONTSIZE_WIDTH_RATIO)
		return;

	// We want to keep a ration between font size and font width so that we don't have a weird rendering
	g_Config.m_ClChatFontSize = g_Config.m_ClChatWidth / CHAT_FONTSIZE_WIDTH_RATIO;
}

void CChat::EnsureCoherentWidth() const
{
	// Adjust width based on font size
	if(g_Config.m_ClChatWidth / (float)g_Config.m_ClChatFontSize >= CHAT_FONTSIZE_WIDTH_RATIO)
		return;

	// We want to keep a ration between font size and font width so that we don't have a weird rendering
	g_Config.m_ClChatWidth = CHAT_FONTSIZE_WIDTH_RATIO * g_Config.m_ClChatFontSize;
}

// ----- send functions -----

void CChat::SendChat(int Team, const char *pLine)
{
	// don't send empty messages
	if(*str_utf8_skip_whitespaces(pLine) == '\0')
		return;
	if(GameClient()->m_FastPractice.ConsumePracticeChatCommand(Team, pLine))
		return;

	m_LastChatSend = time();

	if(GameClient()->Client()->IsSixup())
	{
		protocol7::CNetMsg_Cl_Say Msg7;
		Msg7.m_Mode = Team == 1 ? protocol7::CHAT_TEAM : protocol7::CHAT_ALL;
		Msg7.m_Target = -1;
		Msg7.m_pMessage = pLine;
		Client()->SendPackMsgActive(&Msg7, MSGFLAG_VITAL, true);
		return;
	}

	// send chat message
	CNetMsg_Cl_Say Msg;
	Msg.m_Team = Team;
	Msg.m_pMessage = pLine;
	Client()->SendPackMsgActive(&Msg, MSGFLAG_VITAL);
}

void CChat::AddHistoryEntry(int Team, const char *pLine)
{
	if(!pLine || str_length(pLine) < 1)
		return;

	const int Length = str_length(pLine);
	CHistoryEntry *pEntry = m_History.Allocate(sizeof(CHistoryEntry) + Length);
	pEntry->m_Team = Team;
	str_copy(pEntry->m_aText, pLine, Length + 1);
}

void CChat::SendChatPayloadQueued(int Team, const char *pLine)
{
	if(!pLine || str_length(pLine) < 1)
		return;

	if(m_LastChatSend + time_freq() < time())
	{
		SendChat(Team, pLine);
	}
	else if(m_vPendingChatQueue.size() < 3)
	{
		CPendingChatEntry Entry;
		Entry.m_Team = Team;
		str_copy(Entry.m_aText, pLine, sizeof(Entry.m_aText));
		m_vPendingChatQueue.emplace_back(Entry);
	}
}

void CChat::SendChatQueued(const char *pLine)
{
	if(!pLine || *str_utf8_skip_whitespaces(pLine) == '\0')
		return;

	const int Team = m_Mode == MODE_ALL ? 0 : 1;
	AddHistoryEntry(Team, pLine);
	if(GameClient()->m_Translate.TryTranslateOutgoingChat(Team, pLine))
		return;
	SendChatPayloadQueued(Team, pLine);
}

void CChat::SendTranslatedChatQueued(int Team, const char *pLine)
{
	SendChatPayloadQueued(Team, pLine);
}

bool CChat::LineHighlighted(int ClientId, const char *pLine)
{
	bool Highlighted = false;

	if(Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		if(ClientId >= 0 && ClientId != GameClient()->m_aLocalIds[0] && ClientId != GameClient()->m_aLocalIds[1])
		{
			for(int LocalId : GameClient()->m_aLocalIds)
			{
				Highlighted |= LocalId >= 0 && LineShouldHighlight(pLine, GameClient()->m_aClients[LocalId].m_aName);
			}
		}
	}
	else
	{
		// on demo playback use local id from snap directly,
		// since m_aLocalIds isn't valid there
		Highlighted |= GameClient()->m_Snap.m_LocalClientId >= 0 && LineShouldHighlight(pLine, GameClient()->m_aClients[GameClient()->m_Snap.m_LocalClientId].m_aName);
	}

	return Highlighted;
}
