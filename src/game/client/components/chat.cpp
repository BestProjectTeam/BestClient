/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "chat.h"
#include <base/color.h>

#include <base/io.h>
#include <base/log.h>
#include <base/log_color.h>
#include <base/time.h>

#include <engine/editor.h>
#include <engine/engine.h>
#include <engine/external/regex.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/shared/csv.h>
#include <engine/http.h>
#include <engine/textrender.h>

#include <generated/protocol.h>
#include <generated/protocol7.h>

#include <game/client/animstate.h>
#include <game/client/bc_ui_animations.h>
#include <game/client/components/censor.h>
#include <game/client/components/hud_layout.h>
#include <game/client/components/scoreboard.h>
#include <game/client/components/skins.h>
#include <game/client/components/sounds.h>
#include <game/client/components/tclient/colored_parts.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <engine/font_icons.h>

#include <algorithm>
#include <cctype>
#include <cstring>
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
static constexpr int CHAT_MEDIA_MAX_RESOLVE_DEPTH = 2;
static constexpr int CHAT_MEDIA_MAX_VIDEO_ANIMATION_MS = 15000;
static constexpr int CHAT_MEDIA_MAX_RETRIES = 3;
static constexpr float CHAT_MEDIA_MAX_PREVIEW_HEIGHT = 70.0f;
static constexpr float CHAT_MEDIA_MAX_PREVIEW_HEIGHT_SCOREBOARD = 56.0f;
static constexpr float CHAT_MEDIA_PREVIEW_SIZE_SCALE = 0.9f;
static constexpr float CHAT_MEDIA_COMPACT_EXPANDED_HEIGHT = 150.0f;
static constexpr int CHAT_MEDIA_MAX_URL_LENGTH = 240;
static constexpr int CHAT_MEDIA_MAX_HTML_CANDIDATES = 32;
static constexpr size_t CHAT_MEDIA_MAX_ANIMATED_MEMORY_BYTES = 48ull * 1024ull * 1024ull;
static constexpr bool CHAT_MEDIA_ANIMATE_VIDEOS = true;
static constexpr float CHAT_MEDIA_MIN_PREVIEW_SIDE = 28.0f;

static float NormalizeMediaPreviewCoord(float Value, float Start, float Length)
{
	if(Length <= 0.0f)
		return 0.0f;
	return std::clamp((Value - Start) / Length, 0.0f, 1.0f);
}

static void QuadsSetSubsetRelative(IGraphics *pGraphics, float X, float Y, float W, float H, float OriginX, float OriginY, float OriginW, float OriginH)
{
	pGraphics->QuadsSetSubset(
		NormalizeMediaPreviewCoord(X, OriginX, OriginW),
		NormalizeMediaPreviewCoord(Y, OriginY, OriginH),
		NormalizeMediaPreviewCoord(X + W, OriginX, OriginW),
		NormalizeMediaPreviewCoord(Y + H, OriginY, OriginH));
}

static void QuadsSetSubsetFreeRelative(IGraphics *pGraphics,
	float X0, float Y0, float X1, float Y1, float X2, float Y2, float X3, float Y3,
	float OriginX, float OriginY, float OriginW, float OriginH)
{
	pGraphics->QuadsSetSubsetFree(
		NormalizeMediaPreviewCoord(X0, OriginX, OriginW),
		NormalizeMediaPreviewCoord(Y0, OriginY, OriginH),
		NormalizeMediaPreviewCoord(X1, OriginX, OriginW),
		NormalizeMediaPreviewCoord(Y1, OriginY, OriginH),
		NormalizeMediaPreviewCoord(X2, OriginX, OriginW),
		NormalizeMediaPreviewCoord(Y2, OriginY, OriginH),
		NormalizeMediaPreviewCoord(X3, OriginX, OriginW),
		NormalizeMediaPreviewCoord(Y3, OriginY, OriginH));
}

void DrawRoundedMediaPreview(IGraphics *pGraphics, const IGraphics::CTextureHandle &Texture, float X, float Y, float W, float H, float Rounding, float Alpha)
{
	if(!Texture.IsValid() || W <= 0.0f || H <= 0.0f)
		return;

	const float ClampedRounding = minimum(Rounding, minimum(W, H) / 2.0f);
	pGraphics->WrapClamp();
	pGraphics->TextureSet(Texture);
	pGraphics->QuadsBegin();
	pGraphics->SetColor(1.0f, 1.0f, 1.0f, Alpha);

	auto DrawQuad = [&](float QuadX, float QuadY, float QuadW, float QuadH) {
		if(QuadW <= 0.0f || QuadH <= 0.0f)
			return;

		QuadsSetSubsetRelative(pGraphics, QuadX, QuadY, QuadW, QuadH, X, Y, W, H);
		const IGraphics::CQuadItem QuadItem(QuadX, QuadY, QuadW, QuadH);
		pGraphics->QuadsDrawTL(&QuadItem, 1);
	};

	if(ClampedRounding <= 0.0f)
	{
		DrawQuad(X, Y, W, H);
	}
	else
	{
		constexpr int NumSegments = 8;
		const float SegmentAngle = pi / 2.0f / NumSegments;
		for(int i = 0; i < NumSegments; i += 2)
		{
			const float A1 = i * SegmentAngle;
			const float A2 = (i + 1) * SegmentAngle;
			const float A3 = (i + 2) * SegmentAngle;
			const float CosA1 = std::cos(A1);
			const float CosA2 = std::cos(A2);
			const float CosA3 = std::cos(A3);
			const float SinA1 = std::sin(A1);
			const float SinA2 = std::sin(A2);
			const float SinA3 = std::sin(A3);

			const IGraphics::CFreeformItem TopLeft(
				X + ClampedRounding, Y + ClampedRounding,
				X + (1.0f - CosA1) * ClampedRounding, Y + (1.0f - SinA1) * ClampedRounding,
				X + (1.0f - CosA3) * ClampedRounding, Y + (1.0f - SinA3) * ClampedRounding,
				X + (1.0f - CosA2) * ClampedRounding, Y + (1.0f - SinA2) * ClampedRounding);
			QuadsSetSubsetFreeRelative(pGraphics,
				TopLeft.m_X0, TopLeft.m_Y0, TopLeft.m_X1, TopLeft.m_Y1, TopLeft.m_X2, TopLeft.m_Y2, TopLeft.m_X3, TopLeft.m_Y3,
				X, Y, W, H);
			pGraphics->QuadsDrawFreeform(&TopLeft, 1);

			const IGraphics::CFreeformItem TopRight(
				X + W - ClampedRounding, Y + ClampedRounding,
				X + W - ClampedRounding + CosA1 * ClampedRounding, Y + (1.0f - SinA1) * ClampedRounding,
				X + W - ClampedRounding + CosA3 * ClampedRounding, Y + (1.0f - SinA3) * ClampedRounding,
				X + W - ClampedRounding + CosA2 * ClampedRounding, Y + (1.0f - SinA2) * ClampedRounding);
			QuadsSetSubsetFreeRelative(pGraphics,
				TopRight.m_X0, TopRight.m_Y0, TopRight.m_X1, TopRight.m_Y1, TopRight.m_X2, TopRight.m_Y2, TopRight.m_X3, TopRight.m_Y3,
				X, Y, W, H);
			pGraphics->QuadsDrawFreeform(&TopRight, 1);

			const IGraphics::CFreeformItem BottomLeft(
				X + ClampedRounding, Y + H - ClampedRounding,
				X + (1.0f - CosA1) * ClampedRounding, Y + H - ClampedRounding + SinA1 * ClampedRounding,
				X + (1.0f - CosA3) * ClampedRounding, Y + H - ClampedRounding + SinA3 * ClampedRounding,
				X + (1.0f - CosA2) * ClampedRounding, Y + H - ClampedRounding + SinA2 * ClampedRounding);
			QuadsSetSubsetFreeRelative(pGraphics,
				BottomLeft.m_X0, BottomLeft.m_Y0, BottomLeft.m_X1, BottomLeft.m_Y1, BottomLeft.m_X2, BottomLeft.m_Y2, BottomLeft.m_X3, BottomLeft.m_Y3,
				X, Y, W, H);
			pGraphics->QuadsDrawFreeform(&BottomLeft, 1);

			const IGraphics::CFreeformItem BottomRight(
				X + W - ClampedRounding, Y + H - ClampedRounding,
				X + W - ClampedRounding + CosA1 * ClampedRounding, Y + H - ClampedRounding + SinA1 * ClampedRounding,
				X + W - ClampedRounding + CosA3 * ClampedRounding, Y + H - ClampedRounding + SinA3 * ClampedRounding,
				X + W - ClampedRounding + CosA2 * ClampedRounding, Y + H - ClampedRounding + SinA2 * ClampedRounding);
			QuadsSetSubsetFreeRelative(pGraphics,
				BottomRight.m_X0, BottomRight.m_Y0, BottomRight.m_X1, BottomRight.m_Y1, BottomRight.m_X2, BottomRight.m_Y2, BottomRight.m_X3, BottomRight.m_Y3,
				X, Y, W, H);
			pGraphics->QuadsDrawFreeform(&BottomRight, 1);
		}

		DrawQuad(X + ClampedRounding, Y + ClampedRounding, W - ClampedRounding * 2.0f, H - ClampedRounding * 2.0f);
		DrawQuad(X + ClampedRounding, Y, W - ClampedRounding * 2.0f, ClampedRounding);
		DrawQuad(X + ClampedRounding, Y + H - ClampedRounding, W - ClampedRounding * 2.0f, ClampedRounding);
		DrawQuad(X, Y + ClampedRounding, ClampedRounding, H - ClampedRounding * 2.0f);
		DrawQuad(X + W - ClampedRounding, Y + ClampedRounding, ClampedRounding, H - ClampedRounding * 2.0f);
	}

	pGraphics->QuadsEnd();
	pGraphics->WrapNormal();
	pGraphics->TextureClear();
}

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

static int WrongLayoutToLatinCodepoint(int Codepoint)
{
	switch(Codepoint)
	{
	case '.': return '/';
	case 0x0451: return '`';
	case 0x0401: return '~';
	case 0x0439: return 'q';
	case 0x0419: return 'Q';
	case 0x0446: return 'w';
	case 0x0426: return 'W';
	case 0x0443: return 'e';
	case 0x0423: return 'E';
	case 0x043a: return 'r';
	case 0x041a: return 'R';
	case 0x0435: return 't';
	case 0x0415: return 'T';
	case 0x043d: return 'y';
	case 0x041d: return 'Y';
	case 0x0433: return 'u';
	case 0x0413: return 'U';
	case 0x0448: return 'i';
	case 0x0428: return 'I';
	case 0x0449: return 'o';
	case 0x0429: return 'O';
	case 0x0437: return 'p';
	case 0x0417: return 'P';
	case 0x0445: return '[';
	case 0x0425: return '{';
	case 0x044a: return ']';
	case 0x042a: return '}';
	case 0x0444: return 'a';
	case 0x0424: return 'A';
	case 0x044b: return 's';
	case 0x042b: return 'S';
	case 0x0432: return 'd';
	case 0x0412: return 'D';
	case 0x0430: return 'f';
	case 0x0410: return 'F';
	case 0x043f: return 'g';
	case 0x041f: return 'G';
	case 0x0440: return 'h';
	case 0x0420: return 'H';
	case 0x043e: return 'j';
	case 0x041e: return 'J';
	case 0x043b: return 'k';
	case 0x041b: return 'K';
	case 0x0434: return 'l';
	case 0x0414: return 'L';
	case 0x0436: return ';';
	case 0x0416: return ':';
	case 0x044d: return '\'';
	case 0x042d: return '"';
	case 0x044f: return 'z';
	case 0x042f: return 'Z';
	case 0x0447: return 'x';
	case 0x0427: return 'X';
	case 0x0441: return 'c';
	case 0x0421: return 'C';
	case 0x043c: return 'v';
	case 0x041c: return 'V';
	case 0x0438: return 'b';
	case 0x0418: return 'B';
	case 0x0442: return 'n';
	case 0x0422: return 'N';
	case 0x044c: return 'm';
	case 0x042c: return 'M';
	case 0x0431: return ',';
	case 0x0411: return '<';
	case 0x044e: return '.';
	case 0x042e: return '>';
	default: return Codepoint;
	}
}

static bool IsLikelySlashCommandName(const char *pName)
{
	if(!pName || !pName[0] || !std::isalpha((unsigned char)pName[0]))
		return false;

	for(const char *pChar = pName; *pChar != '\0'; ++pChar)
	{
		if(!std::isalnum((unsigned char)*pChar) && *pChar != '_' && *pChar != '-')
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

		auto DecodeSingleFrameFallback = [&]() -> bool {
			CImageInfo Image;
			if(!MediaDecoder::DecodeImageToRgba(m_pGraphics, m_vData.data(), m_vData.size(), m_aContextName, Image))
				return false;

			m_DecodedFrames.Free();
			m_DecodedFrames.m_Width = (int)Image.m_Width;
			m_DecodedFrames.m_Height = (int)Image.m_Height;
			m_DecodedFrames.m_Animated = false;
			m_DecodedFrames.m_AnimationStart = time_get();

			SMediaRawFrame Frame;
			Frame.m_DurationMs = 100;
			Frame.m_Image = std::move(Image);
			m_DecodedFrames.m_vFrames.push_back(std::move(Frame));
			return !m_DecodedFrames.m_vFrames.empty();
		};

		SMediaDecodeLimits Limits;
		Limits.m_MaxDimension = CHAT_MEDIA_MAX_DIMENSION;
		Limits.m_MaxFrames = CHAT_MEDIA_MAX_GIF_FRAMES;
		Limits.m_MaxTotalBytes = CHAT_MEDIA_MAX_ANIMATED_MEMORY_BYTES;
		Limits.m_MaxAnimationDurationMs = CHAT_MEDIA_MAX_VIDEO_ANIMATION_MS;

		switch(m_MediaKind)
		{
		case EMediaKind::PHOTO:
			m_Success = MediaDecoder::DecodeStaticImageCpu(m_pGraphics, m_vData.data(), m_vData.size(), m_aContextName, m_DecodedFrames, CHAT_MEDIA_MAX_DIMENSION);
			if(!m_Success)
				m_Success = DecodeSingleFrameFallback();
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
			if(!m_Success)
				m_Success = DecodeSingleFrameFallback();
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
			if(!m_Success)
				m_Success = DecodeSingleFrameFallback();
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
	m_TranslateRectValid = false;
	m_TranslateLanguageRectValid = false;
	m_MediaPreviewRectValid = false;
	m_MediaRetryRectValid = false;
	m_ShowAboveHead = false;
}

void CChat::CLine::Reset(CChat &This)
{
	This.TextRender()->DeleteTextContainer(m_TextContainerIndex);
	This.Graphics()->DeleteQuadContainer(m_QuadContainerIndex);
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
	m_TranslateRectValid = false;
	m_TranslateLanguageRectValid = false;
	m_MediaPreviewRectValid = false;
	m_MediaRetryRectValid = false;
	m_ShowAboveHead = false;
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
	m_FullscreenMediaLineIndex = -1;
	m_aFullscreenMediaUrl[0] = '\0';

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
			const size_t NumLetters = std::min(NumChars, sizeof(ms_aDisplayText) - 1);
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

bool CChat::HasServerCommand(const char *pName) const
{
	for(const auto &Command : m_vServerCommands)
	{
		if(str_comp_nocase(Command.m_aName, pName) == 0)
			return true;
	}
	return false;
}

bool CChat::TryConvertWrongLayoutSlashCommand(const char *pLine, char *pOut, int OutSize) const
{
	if(!g_Config.m_BcChatAltCommandLayout || !pLine || !pOut || OutSize <= 0)
		return false;

	const char *pTokenStart = str_utf8_skip_whitespaces(pLine);
	if(*pTokenStart == '\0')
		return false;

	const char *pTokenEnd = pTokenStart;
	while(*pTokenEnd)
	{
		const char *pNext = pTokenEnd;
		const int Codepoint = str_utf8_decode(&pNext);
		if(Codepoint <= 0 || str_utf8_isspace(Codepoint))
			break;
		pTokenEnd = pNext;
	}

	char aConvertedToken[MAX_LINE_LENGTH];
	int ConvertedLen = 0;
	bool Changed = false;
	for(const char *pScan = pTokenStart; pScan < pTokenEnd;)
	{
		const char *pNext = pScan;
		const int Codepoint = str_utf8_decode(&pNext);
		const int ConvertedCodepoint = WrongLayoutToLatinCodepoint(Codepoint);
		Changed |= ConvertedCodepoint != Codepoint;

		char aEncoded[8];
		const int EncodedLen = str_utf8_encode(aEncoded, ConvertedCodepoint);
		if(ConvertedLen + EncodedLen >= (int)sizeof(aConvertedToken))
			return false;
		std::copy_n(aEncoded, EncodedLen, aConvertedToken + ConvertedLen);
		ConvertedLen += EncodedLen;
		pScan = pNext;
	}
	aConvertedToken[ConvertedLen] = '\0';

	if(!Changed || aConvertedToken[0] != '/')
		return false;

	const char *pCommandName = aConvertedToken + 1;
	if(!IsLikelySlashCommandName(pCommandName))
		return false;
	if(!m_vServerCommands.empty() && !HasServerCommand(pCommandName))
		return false;

	str_truncate(pOut, OutSize, pLine, pTokenStart - pLine);
	str_append(pOut, aConvertedToken, OutSize);
	str_append(pOut, pTokenEnd, OutSize);
	return str_comp(pOut, pLine) != 0;
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
	m_PrevHudLayoutX = -10000.0f;
	m_PrevHudLayoutY = -10000.0f;
	m_PrevHudLayoutScale = -1;
	m_PrevHudLayoutEnabled = true;
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
	m_PendingChatCounter = 0;
	m_LastChatSend = 0;
	m_CurrentLine = 0;
	m_BacklogCurLine = 0;
	m_ScrollbarDragging = false;
	m_LastMousePos = std::nullopt;
	m_MouseIsPress = false;
	m_HasSelection = false;
	m_WantsSelectionCopy = false;
	m_IsInputCensored = false;
	m_EditingNewLine = true;
	m_aSavedInputText[0] = '\0';
	m_SavedInputPending = false;
	m_ServerSupportsCommandInfo = false;
	m_TranslateButtonPressed = false;
	m_TranslateButtonRectValid = false;
	m_ServerCommandsNeedSorting = false;
	m_aCurrentInputText[0] = '\0';
	m_aPreviousDisplayedInputText[0] = '\0';
	m_ChatOpenAnimationStart = 0;
	m_vTypingGlyphAnims.clear();
	m_HideMediaByBind = false;
	CloseFullscreenMedia();
	DisableMode();
	m_vServerCommands.clear();

	for(int64_t &LastSoundPlayed : m_aLastSoundPlayed)
		LastSoundPlayed = 0;
}

void CChat::ResetTypingAnimation()
{
	m_vTypingGlyphAnims.clear();
	m_CaretAnimValid = false;
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
		log_error("chat", "expected all or team as mode");

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
	// Migration: the default chat media allowlist gained gifs.teeworlds.xyz. Only upgrade users
	// still on the untouched old default, so anyone who customized the list keeps their edits.
	if(str_comp(g_Config.m_BcChatMediaAllowedDomains, "tenor.com; imgur.com; giphy.com") == 0)
		str_copy(g_Config.m_BcChatMediaAllowedDomains, "tenor.com; imgur.com; giphy.com; gifs.teeworlds.xyz", sizeof(g_Config.m_BcChatMediaAllowedDomains));

	Console()->Register("say", "r[message]", CFGFLAG_CLIENT, ConSay, this, "Say in chat");
	Console()->Register("say_team", "r[message]", CFGFLAG_CLIENT, ConSayTeam, this, "Say in team chat");
	Console()->Register("chat", "s['team'|'all'] ?r[message]", CFGFLAG_CLIENT, ConChat, this, "Enable chat with all/team mode");
	Console()->Register("+show_chat", "", CFGFLAG_CLIENT, ConShowChat, this, "Show chat");
	Console()->Register("echo", "r[message]", CFGFLAG_CLIENT | CFGFLAG_STORE, ConEcho, this, "Echo the text in chat window");
	Console()->Register("clear_chat", "", CFGFLAG_CLIENT | CFGFLAG_STORE, ConClearChat, this, "Clear chat messages");
	Console()->Register("toggle_chat_media_hidden", "", CFGFLAG_CLIENT, ConToggleHideChatMedia, this, "Toggle hidden media mode in chat");
	Console()->Register("add_censor_list", "r[word]", CFGFLAG_CLIENT, ConAddCensorList, this, "Add a word to the chat filter regex");
	Console()->Register("add_white_list", "s[nickname]", CFGFLAG_CLIENT, ConAddWhiteList, this, "Add a player to the chat filter whitelist");
}

void CChat::OnInit()
{
	Reset();
	Console()->Chain("cl_chat_old", ConchainChatOld, this);
	Console()->Chain("cl_chat_size", ConchainChatFontSize, this);
	Console()->Chain("cl_chat_width", ConchainChatWidth, this);
	Console()->Chain("bc_regex_player_whitelist", ConchainRegexPlayerWhitelist, this);

	if(g_Config.m_BcRegexPlayerWhitelist[0])
	{
		auto Re = Regex(g_Config.m_BcRegexPlayerWhitelist);
		if(Re.error().empty())
			m_RegexPlayerWhitelist = std::move(Re);
	}
	if(g_Config.m_TcRegexChatIgnore[0])
	{
		auto Re = Regex(g_Config.m_TcRegexChatIgnore);
		if(Re.error().empty())
			GameClient()->m_TClient.m_RegexChatIgnore = std::move(Re);
	}
}

bool CChat::OnInput(const IInput::CEvent &Event)
{
	// BestClient: click a ready photo/gif preview to open it fullscreen; click anywhere (or Esc)
	// to close it back. Works even while chat is fully closed, same as the retry-click below.
	if((Event.m_Flags & IInput::FLAG_PRESS) && HasValidFullscreenMedia())
	{
		if(Event.m_Key == KEY_MOUSE_1 || Event.m_Key == KEY_MOUSE_2 || Event.m_Key == KEY_ESCAPE)
		{
			CloseFullscreenMedia();
			return true;
		}
	}
	else if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_MOUSE_1 && g_Config.m_BcChatMediaPreview &&
		(Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK))
	{
		const vec2 MousePos = ChatMousePos();
		for(int LineIndex = 0; LineIndex < MAX_LINES; ++LineIndex)
		{
			CLine &Line = m_aLines[LineIndex];
			if(!Line.m_Initialized || !Line.m_MediaPreviewRectValid || Line.m_MediaState != EMediaState::READY)
				continue;
			if(ShouldHideMediaPreview(Line) || ShouldHideNsfwMedia(Line))
				continue;

			const SRenderRect &Rect = Line.m_MediaPreviewRect;
			if(MousePos.x >= Rect.m_X && MousePos.x <= Rect.m_X + Rect.m_W &&
				MousePos.y >= Rect.m_Y && MousePos.y <= Rect.m_Y + Rect.m_H)
			{
				OpenFullscreenMedia(LineIndex);
				return true;
			}
		}
	}

	// BestClient: retrying failed media works even while chat is fully closed, as long as a
	// message with a failed preview is still fading out on screen.
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

	if(m_Mode == MODE_NONE && !m_Show)
		return false;

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

	if(m_Mode == MODE_NONE)
	{
		if(Event.m_Key == KEY_MOUSE_1 && (Event.m_Flags & (IInput::FLAG_PRESS | IInput::FLAG_RELEASE)))
			return true;
		return false;
	}

	if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_MOUSE_1 && m_HideMediaByBind)
	{
		const vec2 MousePos = ChatMousePos();
		for(int LineIndex = 0; LineIndex < MAX_LINES; ++LineIndex)
		{
			CLine &Line = m_aLines[LineIndex];
			if(!Line.m_Initialized || !Line.m_MediaPreviewRectValid || !ShouldHideMediaPreview(Line))
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

	// Translate settings popup input handling
	if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_ESCAPE && Ui()->IsPopupOpen())
	{
		Ui()->ClosePopupMenus();
		return true;
	}
	if(Ui()->IsPopupOpen(&m_TranslateSettingsPopupId) && Ui()->OnInput(Event))
		return true;

	// Translate settings button click
	if(Event.m_Key == KEY_MOUSE_1 && m_TranslateButtonRectValid)
	{
		const vec2 MousePos = ChatMousePos();
		const bool InsideButton =
			MousePos.x >= m_TranslateButtonRect.m_X && MousePos.x <= m_TranslateButtonRect.m_X + m_TranslateButtonRect.m_W &&
			MousePos.y >= m_TranslateButtonRect.m_Y && MousePos.y <= m_TranslateButtonRect.m_Y + m_TranslateButtonRect.m_H;
		if(Event.m_Flags & IInput::FLAG_PRESS)
		{
			m_TranslateButtonPressed = InsideButton;
			if(InsideButton)
			{
				m_MouseIsPress = false;
				m_HasSelection = false;
				return true;
			}
		}
		else if(Event.m_Flags & IInput::FLAG_RELEASE)
		{
			const bool Activate = m_TranslateButtonPressed && InsideButton;
			m_TranslateButtonPressed = false;
			if(Activate)
			{
				if(Ui()->IsPopupOpen(&m_TranslateSettingsPopupId))
					Ui()->ClosePopupMenu(&m_TranslateSettingsPopupId);
				else
					OpenTranslateSettingsPopup(m_TranslateButtonUiRect);
				return true;
			}
		}
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
				if(m_Input.GetString()[0] != '\0')
				{
					str_copy(m_aSavedInputText, m_Input.GetString(), sizeof(m_aSavedInputText));
					m_SavedInputPending = true;
				}
				else
				{
					m_SavedInputPending = false;
					m_aSavedInputText[0] = '\0';
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

		if(!m_CompletionUsed && m_aCompletionBuffer[0] != '/')
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
				if((m_Input.GetString()[0] == '/' || GameClient()->m_BindChat.CheckBindChat(m_Input.GetString())) && (str_find(pCompletionString, " ") || str_find(pCompletionString, "\"")))
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
		{
			m_pHistoryEntry = m_History.Last();
		}

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

void CChat::EnableMode(int Team)
{
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return;

	if(m_Mode == MODE_NONE)
	{
		if(Team)
			m_Mode = MODE_TEAM;
		else
			m_Mode = MODE_ALL;

		m_CompletionChosen = -1;
		m_CompletionUsed = false;
		m_ChatOpenAnimationStart = time_get();
		ResetTypingAnimation();
		m_Input.Activate(EInputPriority::CHAT);
		SyncTypingAnimationBaseline();

		const vec2 WindowSize(maximum(1.0f, (float)Graphics()->WindowWidth()), maximum(1.0f, (float)Graphics()->WindowHeight()));
		m_LastMousePos = Ui()->UpdatedMousePos() * vec2(Ui()->Screen()->w, Ui()->Screen()->h) / WindowSize;
		const vec2 CenterPos = Ui()->Screen()->Center() / vec2(Ui()->Screen()->w, Ui()->Screen()->h) * WindowSize;
		Ui()->OnCursorMove(CenterPos.x - Ui()->UpdatedMousePos().x, CenterPos.y - Ui()->UpdatedMousePos().y);
	}
}

void CChat::DisableMode()
{
	if(m_Mode != MODE_NONE)
	{
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
		m_Input.Deactivate();
		if(m_LastMousePos.has_value())
		{
			const vec2 WindowSize(maximum(1.0f, (float)Graphics()->WindowWidth()), maximum(1.0f, (float)Graphics()->WindowHeight()));
			const vec2 RestorePos = m_LastMousePos.value() / vec2(Ui()->Screen()->w, Ui()->Screen()->h) * WindowSize;
			Ui()->OnCursorMove(RestorePos.x - Ui()->UpdatedMousePos().x, RestorePos.y - Ui()->UpdatedMousePos().y);
		}
		m_LastMousePos = std::nullopt;
	}
}

bool CChat::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(m_Mode == MODE_NONE)
		return false;
	Ui()->ConvertMouseMove(&x, &y, CursorType);
	Ui()->OnCursorMove(x, y);
	return true;
}

void CChat::OnMessage(int MsgType, void *pRawMsg)
{
	if(GameClient()->m_SuppressEvents)
		return;

	if(MsgType == NETMSGTYPE_SV_CHAT)
	{
		CNetMsg_Sv_Chat *pMsg = (CNetMsg_Sv_Chat *)pRawMsg;

		if(g_Config.m_TcRegexChatIgnore[0] && g_Config.m_BcEnableCensorList)
		{
			auto &Re = GameClient()->m_TClient.m_RegexChatIgnore;
			if(Re.error().empty() && Re.test(pMsg->m_pMessage))
			{
				const char *pFilteredMSG = FilterText(pMsg->m_pMessage, pMsg->m_ClientId, true);
				AddLine(pMsg->m_ClientId, pMsg->m_Team, pFilteredMSG);
				return;
			}
		}
		else
		{
			auto &Re = GameClient()->m_TClient.m_RegexChatIgnore;
			if(Re.error().empty() && Re.test(pMsg->m_pMessage))
				return;
		}

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

vec2 CChat::ChatMousePos() const
{
	const float Height = 300.0f;
	const float Width = Height * Graphics()->ScreenAspect();
	const vec2 WindowSize(maximum(1.0f, (float)Graphics()->WindowWidth()), maximum(1.0f, (float)Graphics()->WindowHeight()));
	const vec2 UiMousePos = Ui()->UpdatedMousePos() * vec2(Ui()->Screen()->w, Ui()->Screen()->h) / WindowSize;
	const vec2 UiToChatScale(Width / Ui()->Screen()->w, Height / Ui()->Screen()->h);
	return UiMousePos * UiToChatScale;
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
	{"fr", "French"},
	{"es", "Spanish"},
	{"pl", "Polish"},
	{"zh", "Chinese"},
	{"pt", "Brazilian"},
	{"tr", "Turkish"},
};

constexpr STranslateLanguageOption gs_aTranslateTargetOptions[] = {
	{"ru", "Russian"},
	{"en", "English"},
	{"de", "German"},
	{"fr", "French"},
	{"es", "Spanish"},
	{"pl", "Polish"},
	{"zh", "Chinese"},
	{"pt", "Brazilian"},
	{"tr", "Turkish"},
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
	// Use UI-space coordinates so the popup is positioned correctly by CMenus
	const float PopupW = 300.0f;
	const float PopupX = m_TranslateButtonUiRect.x + m_TranslateButtonUiRect.w / 2.0f - PopupW / 2.0f;
	Ui()->DoPopupMenu(&m_TranslateSettingsPopupId, PopupX, m_TranslateButtonUiRect.y, PopupW, 156.0f, this, PopupTranslateSettings);
	(void)ButtonRect;
}

CUi::EPopupMenuFunctionResult CChat::PopupTranslateSettings(void *pContext, CUIRect View, bool Active)
{
	CChat *pChat = static_cast<CChat *>(pContext);
	(void)Active;

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

	static const char *s_apSourceLabels[] = {"Auto", "Russian", "English", "German", "French", "Spanish", "Polish", "Chinese", "Brazilian", "Turkish"};
	static const char *s_apTargetLabels[] = {"Russian", "English", "German", "French", "Spanish", "Polish", "Chinese", "Brazilian", "Turkish"};

	const float RowH = 20.0f;
	const float SmallGap = 3.0f;
	const float SectionGap = 6.0f;
	const float FontSize = 11.0f;
	const float ArrowW = 20.0f;

	const auto RenderLangPair = [&](char *pSrc, size_t SrcSz, char *pDst, size_t DstSz) {
		CUIRect PairRow, SrcRect, ArrowRect, DstRect;
		View.HSplitTop(RowH, &PairRow, &View);
		const float DropW = (PairRow.w - ArrowW) / 2.0f;
		PairRow.VSplitLeft(DropW, &SrcRect, &PairRow);
		PairRow.VSplitLeft(ArrowW, &ArrowRect, &DstRect);
		pChat->Ui()->DoLabel(&ArrowRect, "→", FontSize, TEXTALIGN_MC);
		const int SrcIdx = TranslateLanguageIndex(pSrc, gs_aTranslateSourceOptions);
		const int NewSrcIdx = pChat->Ui()->DoDropDown(&SrcRect, SrcIdx, s_apSourceLabels, std::size(s_apSourceLabels), s_IncomingSourceDropDown);
		if(NewSrcIdx != SrcIdx)
			ApplyTranslateLanguage(pSrc, SrcSz, NewSrcIdx, gs_aTranslateSourceOptions);
		const int DstIdx = TranslateLanguageIndex(pDst, gs_aTranslateTargetOptions);
		const int NewDstIdx = pChat->Ui()->DoDropDown(&DstRect, DstIdx, s_apTargetLabels, std::size(s_apTargetLabels), s_IncomingTargetDropDown);
		if(NewDstIdx != DstIdx)
			ApplyTranslateLanguage(pDst, DstSz, NewDstIdx, gs_aTranslateTargetOptions);
	};

	const auto RenderSep = [&]() {
		CUIRect Sep;
		View.HSplitTop(SectionGap, nullptr, &View);
		View.HSplitTop(1.0f, &Sep, &View);
		Sep.Draw(ColorRGBA(0.4f, 0.4f, 0.4f, 0.6f), IGraphics::CORNER_NONE, 0.0f);
		View.HSplitTop(SectionGap, nullptr, &View);
	};

	// ── Others ──
	{
		CUIRect Row;
		View.HSplitTop(18.0f, &Row, &View);
		if(pChat->GameClient()->m_Menus.DoButton_CheckBox(&pChat->m_TranslateSettingsEnableButton, "Others", g_Config.m_TcTranslateAutoIncoming, &Row))
			g_Config.m_TcTranslateAutoIncoming ^= 1;
	}
	View.HSplitTop(SmallGap, nullptr, &View);
	RenderLangPair(g_Config.m_BcTranslateIncomingSource, sizeof(g_Config.m_BcTranslateIncomingSource),
		g_Config.m_TcTranslateTarget, sizeof(g_Config.m_TcTranslateTarget));

	View.HSplitTop(SectionGap, nullptr, &View);

	// ── Yours ──
	{
		CUIRect Row;
		View.HSplitTop(18.0f, &Row, &View);
		if(pChat->GameClient()->m_Menus.DoButton_CheckBox(&pChat->m_TranslateSettingsEnableOutgoingButton, "Yours", g_Config.m_TcTranslateAutoOutgoing, &Row))
			g_Config.m_TcTranslateAutoOutgoing ^= 1;
	}
	View.HSplitTop(SmallGap, nullptr, &View);
	{
		CUIRect PairRow, SrcRect, ArrowRect, DstRect;
		View.HSplitTop(RowH, &PairRow, &View);
		const float DropW = (PairRow.w - ArrowW) / 2.0f;
		PairRow.VSplitLeft(DropW, &SrcRect, &PairRow);
		PairRow.VSplitLeft(ArrowW, &ArrowRect, &DstRect);
		pChat->Ui()->DoLabel(&ArrowRect, "→", FontSize, TEXTALIGN_MC);
		const int SrcIdx = TranslateLanguageIndex(g_Config.m_BcTranslateOutgoingSource, gs_aTranslateSourceOptions);
		const int NewSrcIdx = pChat->Ui()->DoDropDown(&SrcRect, SrcIdx, s_apSourceLabels, std::size(s_apSourceLabels), s_OutgoingSourceDropDown);
		if(NewSrcIdx != SrcIdx)
			ApplyTranslateLanguage(g_Config.m_BcTranslateOutgoingSource, sizeof(g_Config.m_BcTranslateOutgoingSource), NewSrcIdx, gs_aTranslateSourceOptions);
		const int DstIdx = TranslateLanguageIndex(g_Config.m_BcTranslateOutgoingTarget, gs_aTranslateTargetOptions);
		const int NewDstIdx = pChat->Ui()->DoDropDown(&DstRect, DstIdx, s_apTargetLabels, std::size(s_apTargetLabels), s_OutgoingTargetDropDown);
		if(NewDstIdx != DstIdx)
			ApplyTranslateLanguage(g_Config.m_BcTranslateOutgoingTarget, sizeof(g_Config.m_BcTranslateOutgoingTarget), NewDstIdx, gs_aTranslateTargetOptions);
	}
	View.HSplitTop(SmallGap, nullptr, &View);
	{
		CUIRect Row;
		View.HSplitTop(18.0f, &Row, &View);
		if(pChat->GameClient()->m_Menus.DoButton_CheckBox(&pChat->m_TranslateSettingsStripPunctuationButton, "No commas or periods", g_Config.m_BcTranslateOutgoingStripPunctuation, &Row))
			g_Config.m_BcTranslateOutgoingStripPunctuation ^= 1;
	}

	RenderSep();

	static CButtonContainer s_TranslateKeyReader;
	static CButtonContainer s_TranslateKeyClear;
	pChat->GameClient()->m_Menus.DoLine_KeyReader(View, s_TranslateKeyReader, s_TranslateKeyClear, Localize("Toggle translate"), "toggle_translate");

	return CUi::POPUP_KEEP_OPEN;
}

void CChat::RenderTranslateSettingsButton(const CUIRect &ButtonRect)
{
	const bool Hovered = Ui()->MouseHovered(&ButtonRect);
	const bool IsOpen = Ui()->IsPopupOpen(&m_TranslateSettingsPopupId);
	const bool IsTranslateActive = g_Config.m_TcTranslateAutoIncoming || g_Config.m_TcTranslateAutoOutgoing;

	// No background — colour the icon itself
	const ColorRGBA IconColor = IsOpen ? ColorRGBA(0.5f, 0.65f, 1.0f, 1.0f) :
		(IsTranslateActive ? (Hovered ? ColorRGBA(0.5f, 0.95f, 0.5f, 1.0f) : ColorRGBA(0.35f, 0.80f, 0.35f, 0.90f)) :
		(Hovered ? ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.80f)));

	const float IconSize = ButtonRect.h * 1.4f;
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	TextRender()->TextColor(IconColor);
	Ui()->DoLabel(&ButtonRect, FontIcon::EARTH_AMERICAS, IconSize, TEXTALIGN_MC);
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	if(Hovered)
		Ui()->SetHotItem(&m_TranslateSettingsButton);
	GameClient()->m_Tooltips.DoToolTip(&m_TranslateSettingsButton, &ButtonRect, Localize("Chat translate settings"));
}

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

static constexpr const char *s_pDefaultChatMediaAllowedDomains = "tenor.com; imgur.com; giphy.com; gifs.teeworlds.xyz";

static bool IsAllowedChatMediaHostByDomainList(const std::string &HostLower, const char *pList, bool &HasDomains)
{
	HasDomains = false;
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
			HasDomains = true;
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

static bool IsAllowedChatMediaHost(const std::string &HostLower)
{
	if(!g_Config.m_BcChatMediaContentFilter)
		return true;
	if(HostLower.empty())
		return false;

	bool HasConfiguredDomains = false;
	if(IsAllowedChatMediaHostByDomainList(HostLower, g_Config.m_BcChatMediaAllowedDomains, HasConfiguredDomains))
		return true;
	if(HasConfiguredDomains)
		return false;

	bool HasDefaultDomains = false;
	return IsAllowedChatMediaHostByDomainList(HostLower, s_pDefaultChatMediaAllowedDomains, HasDefaultDomains);
}

static bool IsAllowedChatMediaUrl(const char *pUrl)
{
	if(!g_Config.m_BcChatMediaContentFilter)
		return true;
	if(pUrl == nullptr || pUrl[0] == '\0')
		return false;
	return IsAllowedChatMediaHost(ExtractUrlHostLower(pUrl));
}

// Separate, narrower allowlist than IsAllowedChatMediaUrl: only links from these domains
// pop the above-head gif bubble, so a random tenor/imgur link someone pastes doesn't spam it.
static bool IsGifBubbleUrl(const char *pUrl)
{
	if(!g_Config.m_BcGifBubbleAboveHead || pUrl == nullptr || pUrl[0] == '\0')
		return false;
	bool HasDomains = false;
	return IsAllowedChatMediaHostByDomainList(ExtractUrlHostLower(pUrl), g_Config.m_BcGifBubbleDomains, HasDomains);
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

static bool ExtractImgurMediaId(const std::string &Url, std::string &OutMediaId)
{
	OutMediaId.clear();
	const std::string HostLower = ExtractUrlHostLower(Url);
	if(!HostIsOrEndsWith(HostLower, "imgur.com"))
		return false;

	const std::string Path = ExtractUrlPath(Url);
	if(Path.empty() || Path == "/")
		return false;

	// Album/gallery/topic share links use post IDs that frequently do not map to a
	// direct i.imgur.com media ID. Let HTML extraction resolve the real media URL.
	const char *apPrefixes[] = {"/a/", "/gallery/", "/t/"};
	for(const char *pPrefix : apPrefixes)
	{
		if(str_startswith(Path.c_str(), pPrefix))
			return false;
	}

	size_t SegmentStart = Path.find_last_of('/');
	if(SegmentStart == std::string::npos || SegmentStart + 1 >= Path.size())
		return false;

	std::string LastSegment = Path.substr(SegmentStart + 1);
	const size_t DotPos = LastSegment.find('.');
	if(DotPos != std::string::npos)
		LastSegment.resize(DotPos);

	if(LastSegment.size() < 4 || LastSegment.size() > 16)
		return false;

	for(char c : LastSegment)
	{
		if(!std::isalnum((unsigned char)c))
			return false;
	}

	OutMediaId = LastSegment;
	return true;
}

static void AddDirectImgurCandidates(const std::string &Url, std::vector<std::string> &vOutCandidates)
{
	std::string MediaId;
	if(!ExtractImgurMediaId(Url, MediaId))
		return;

	const char *apFormats[] = {"mp4", "gif", "webm", "jpg", "jpeg", "png", "webp"};
	for(const char *pFormat : apFormats)
	{
		std::string Candidate = "https://i.imgur.com/" + MediaId + "." + pFormat;
		if((int)Candidate.size() <= CHAT_MEDIA_MAX_URL_LENGTH)
			vOutCandidates.push_back(std::move(Candidate));
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

static bool IsAviSignature(const unsigned char *pData, size_t DataSize)
{
	return DataSize >= 12 && mem_comp(pData, "RIFF", 4) == 0 && mem_comp(pData + 8, "AVI ", 4) == 0;
}

static bool IsFlvSignature(const unsigned char *pData, size_t DataSize)
{
	return DataSize >= 3 && mem_comp(pData, "FLV", 3) == 0;
}

static bool IsMpegProgramStreamSignature(const unsigned char *pData, size_t DataSize)
{
	static const unsigned char s_aMpegPsSig[4] = {0x00, 0x00, 0x01, 0xba};
	return DataSize >= 4 && mem_comp(pData, s_aMpegPsSig, 4) == 0;
}

static bool IsMpegTransportStreamSignature(const unsigned char *pData, size_t DataSize)
{
	// MPEG-TS packets are 188 bytes and start with sync byte 0x47.
	return DataSize >= 376 && pData[0] == 0x47 && pData[188] == 0x47;
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
	return IsMp4LikeSignature(pData, DataSize) || IsWebmSignature(pData, DataSize) || IsOggSignature(pData, DataSize) ||
		IsAviSignature(pData, DataSize) || IsFlvSignature(pData, DataSize) || IsMpegProgramStreamSignature(pData, DataSize) ||
		IsMpegTransportStreamSignature(pData, DataSize);
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
		AddDirectImgurCandidates(Url, vExpandedUrls);
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

	std::shared_ptr<IHttpRequest> pGet = HttpGet(Line.m_aMediaUrl);
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
	if((Line.m_MediaState == EMediaState::READY || Line.m_MediaState == EMediaState::LOADING || Line.m_MediaState == EMediaState::DECODING || Line.m_MediaState == EMediaState::QUEUED) && Line.m_MediaKind != EMediaKind::UNKNOWN)
		return IsMediaKindAllowed(Line.m_MediaKind);
	return HasAllowedMediaCandidates(Line);
}

bool CChat::ShouldHideMediaPreview(const CLine &Line) const
{
	return m_HideMediaByBind && !Line.m_MediaRevealed && ShouldDisplayMediaSlot(Line);
}

// Unlike ShouldHideMediaPreview (a spoiler toggle the user can click through), this is a hard
// block: if we know a link is nsfw and the browser's "show NSFW" setting is off, there is no
// click-to-reveal - the only way to see it is to enable that setting.
bool CChat::ShouldHideNsfwMedia(const CLine &Line) const
{
	if(g_Config.m_BcCherryGifsShowNsfw || Line.m_aMediaUrl[0] == '\0')
		return false;
	bool Nsfw = false;
	return GameClient()->m_CherryGifs.TryGetNsfw(Line.m_aMediaUrl, Nsfw) && Nsfw;
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

void CChat::OpenFullscreenMedia(int LineIndex)
{
	if(LineIndex < 0 || LineIndex >= MAX_LINES)
		return;
	const CLine &Line = m_aLines[LineIndex];
	if(!Line.m_Initialized || Line.m_MediaState != EMediaState::READY)
		return;

	m_FullscreenMediaLineIndex = LineIndex;
	str_copy(m_aFullscreenMediaUrl, Line.m_aMediaUrl, sizeof(m_aFullscreenMediaUrl));
}

void CChat::CloseFullscreenMedia()
{
	m_FullscreenMediaLineIndex = -1;
	m_aFullscreenMediaUrl[0] = '\0';
}

bool CChat::HasValidFullscreenMedia() const
{
	if(m_FullscreenMediaLineIndex < 0 || m_FullscreenMediaLineIndex >= MAX_LINES)
		return false;
	const CLine &Line = m_aLines[m_FullscreenMediaLineIndex];
	// Guards against the ring buffer slot having been recycled for a different message while the
	// viewer was open.
	return Line.m_Initialized && Line.m_MediaState == EMediaState::READY && str_comp(Line.m_aMediaUrl, m_aFullscreenMediaUrl) == 0;
}

void CChat::RenderFullscreenMedia(float Width, float Height)
{
	if(!HasValidFullscreenMedia())
		return;

	CLine &Line = m_aLines[m_FullscreenMediaLineIndex];
	IGraphics::CTextureHandle MediaTexture;
	if(!GetCurrentFrameTexture(Line, MediaTexture) || Line.m_MediaWidth <= 0 || Line.m_MediaHeight <= 0)
		return;

	Graphics()->DrawRect(0.0f, 0.0f, Width, Height, ColorRGBA(0.0f, 0.0f, 0.0f, 0.85f), IGraphics::CORNER_NONE, 0.0f);

	const float MaxW = Width * 0.92f;
	const float MaxH = Height * 0.92f;
	const float Scale = minimum(MaxW / (float)Line.m_MediaWidth, MaxH / (float)Line.m_MediaHeight);
	const float DrawW = Line.m_MediaWidth * Scale;
	const float DrawH = Line.m_MediaHeight * Scale;
	const float DrawX = (Width - DrawW) / 2.0f;
	const float DrawY = (Height - DrawH) / 2.0f;

	DrawRoundedMediaPreview(Graphics(), MediaTexture, DrawX, DrawY, DrawW, DrawH, 0.0f, 1.0f);
}

void CChat::UpdateMediaDownloads()
{
	if(!g_Config.m_BcChatMediaPreview || !AnyMediaAllowed())
		return;

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
					EMediaKind MediaKind = EMediaKind::UNKNOWN;
					if(IsGif || IsAnimatedImageCandidate)
						MediaKind = EMediaKind::ANIMATED;
					else if(IsVideoCandidate || (!IsImageCandidate && Ext.empty()))
						MediaKind = EMediaKind::VIDEO;
					else if(IsImageCandidate)
						MediaKind = EMediaKind::PHOTO;

					// Allow unknown payload signatures to be decoded as a fallback.
					// Some CDNs return uncommon containers/codecs without filename extension.
					if(MediaKind == EMediaKind::UNKNOWN)
						MediaKind = EMediaKind::VIDEO;
					Line.m_MediaKind = MediaKind;

					if(!Ext.empty() && IsBlockedMediaExtension(Ext))
					{
						pFailureReason = "blocked extension";
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

std::string CChat::BuildPlainTextLine(const CLine &Line, int HoveredTranslateLineIndex) const
{
	char aClientId[16] = "";
	if(g_Config.m_ClShowIds && Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
		GameClient()->FormatClientId(Line.m_ClientId, aClientId, EClientIdFormat::INDENT_AUTO);

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

	const char *pDisplayedTranslatedText = pTranslatedText;
	const char *pDisplayedTranslatedLanguage = pTranslatedLanguage;
	const int LineIndex = (int)(&Line - m_aLines);
	if(pTranslatedText != nullptr && HoveredTranslateLineIndex == LineIndex)
	{
		pDisplayedTranslatedText = pText;
		pDisplayedTranslatedLanguage = nullptr;
	}

	std::string Result;
	if(Line.m_ClientId >= 0 && Line.m_aName[0] != '\0' && Line.m_Friend && g_Config.m_ClMessageFriend)
		Result += "♥ ";
	Result += aClientId;
	Result += Line.m_aName;
	if(Line.m_TimesRepeated > 0)
		Result += aCount;
	if(Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
		Result += ": ";
	if(pDisplayedTranslatedText)
	{
		Result += pDisplayedTranslatedText;
		if(pDisplayedTranslatedLanguage)
		{
			Result += " [";
			Result += pDisplayedTranslatedLanguage;
			Result += "]";
		}
		Result += "\n";
		Result += pText;
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

void CChat::ConToggleHideChatMedia(IConsole::IResult *pResult, void *pUserData)
{
	CChat *pThis = static_cast<CChat *>(pUserData);
	(void)pResult;
	pThis->m_HideMediaByBind = !pThis->m_HideMediaByBind;
	pThis->ResetHiddenMediaReveals();
	pThis->RebuildChat();
	pThis->Echo(pThis->m_HideMediaByBind ? "Chat media hidden" : "Chat media visible");
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

	// TClient
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
		{
			pEnd = pStrOld;
		}

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

	auto &&FChatMsgCheckAndPrint = [](const CLine &Line) {
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

		log_info_color(color_cast<LOG_COLOR>(ChatLogColor), pFrom, "%s%s%s", Line.m_aName, Line.m_ClientId >= 0 ? ": " : "", Line.m_aText);
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

	// The whole message is a single recognized link from a gif-bubble domain (e.g. sent via
	// the gif wheel, or pasted by hand) -> pop a bubble above the sender's head too.
	CurrentLine.m_ShowAboveHead = ClientId >= 0 &&
				       CurrentLine.m_vMediaCandidates.size() == 1 &&
				       str_comp(CurrentLine.m_aText, CurrentLine.m_vMediaCandidates.front().c_str()) == 0 &&
				       IsGifBubbleUrl(CurrentLine.m_aText);

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
		const char *pFilteredLineAuthor = FilterText(LineAuthor.m_aName);

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
				str_append(CurrentLine.m_aName, pFilteredLineAuthor);
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
				str_append(CurrentLine.m_aName, pFilteredLineAuthor);
			}
			CurrentLine.m_NameColor = TEAM_RED;
			CurrentLine.m_Highlighted = true;
			Highlighted = true;
		}
		else
		{
			str_copy(CurrentLine.m_aName, pFilteredLineAuthor);
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
			str_format(aBuf, sizeof(aBuf), "%s: %s", CurrentLine.m_aName, CurrentLine.m_aText);
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

	// TClient
	GameClient()->m_Translate.AutoTranslate(CurrentLine);
}

void CChat::OnPrepareLines(float x, float y, int StartLine, int HoveredTranslateLineIndex)
{
	const float Height = HudLayout::CANVAS_HEIGHT;
	const float Width = Height * Graphics()->ScreenAspect();
	const auto Layout = HudLayout::Get(HudLayout::MODULE_CHAT, Width, Height);
	const bool LayoutEnabled = HudLayout::IsEnabled(HudLayout::MODULE_CHAT);
	const float LayoutScale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	float FontSize = this->FontSize();

	const bool IsScoreBoardOpen = GameClient()->m_Scoreboard.IsShown() && (Graphics()->ScreenAspect() > 1.7f); // only assume scoreboard when screen ratio is widescreen(something around 16:9)
	const bool ShowLargeArea = m_Show || (m_Mode != MODE_NONE && g_Config.m_ClShowChat == 1) || g_Config.m_ClShowChat == 2;
	const bool ModeActive = m_Mode != MODE_NONE;
	const bool ChatSelectionActive = m_HasSelection && !m_MouseIsPress && !m_WantsSelectionCopy && !m_Input.HasSelection();
	const bool ForceSelectionRefresh = m_MouseIsPress || m_WantsSelectionCopy || ChatSelectionActive != m_PrevChatSelectionActive;
	const bool LayoutChanged = Layout.m_X != m_PrevHudLayoutX || Layout.m_Y != m_PrevHudLayoutY || Layout.m_Scale != m_PrevHudLayoutScale || LayoutEnabled != m_PrevHudLayoutEnabled;
	const bool ForceRecreate = IsScoreBoardOpen != m_PrevScoreBoardShowed || ShowLargeArea != m_PrevShowChat || ModeActive != m_PrevModeActive || ForceSelectionRefresh || HoveredTranslateLineIndex != m_HoveredTranslateLineIndex || LayoutChanged;
	m_PrevScoreBoardShowed = IsScoreBoardOpen;
	m_PrevShowChat = ShowLargeArea;
	m_PrevModeActive = ModeActive;
	m_PrevChatSelectionActive = ChatSelectionActive;
	m_PrevHudLayoutX = Layout.m_X;
	m_PrevHudLayoutY = Layout.m_Y;
	m_PrevHudLayoutScale = Layout.m_Scale;
	m_PrevHudLayoutEnabled = LayoutEnabled;
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
	float LineWidth = (IsScoreBoardOpen ? maximum(85.0f * LayoutScale, (FontSize * 85.0f / 6.0f)) : ChatWidth()) - (RealMsgPaddingX * 1.5f) - RealMsgPaddingTee;

	const auto ShouldExpandCompactAreaForMedia = [&]() {
		if(IsScoreBoardOpen || ShowLargeArea || !g_Config.m_BcChatMediaPreview || !AnyMediaAllowed())
			return false;
		for(int i = 0; i < 3; ++i)
		{
			const CLine &RecentLine = m_aLines[((m_CurrentLine - i) + MAX_LINES) % MAX_LINES];
			if(!RecentLine.m_Initialized)
				break;
			if(ShouldDisplayMediaSlot(RecentLine))
				return true;
		}
		return false;
	};
	const bool ExpandCompactAreaForMedia = ShouldExpandCompactAreaForMedia();
	const float VisibleHeight = IsScoreBoardOpen ? 93.0f * LayoutScale : (ShowLargeArea ? 223.0f * LayoutScale : (ExpandCompactAreaForMedia ? CHAT_MEDIA_COMPACT_EXPANDED_HEIGHT * LayoutScale : 73.0f * LayoutScale));
	float HeightLimit = y - VisibleHeight;
	float Begin = x;
	float TextBegin = Begin + RealMsgPaddingX / 2.0f;
	int OffsetType = IsScoreBoardOpen ? 1 : 0;
	const float MaxPreviewHeight = (IsScoreBoardOpen ? CHAT_MEDIA_MAX_PREVIEW_HEIGHT_SCOREBOARD : CHAT_MEDIA_MAX_PREVIEW_HEIGHT) * CHAT_MEDIA_PREVIEW_SIZE_SCALE;
	const bool ChatInteractionActive = m_Mode != MODE_NONE;

	for(int i = StartLine; i < MAX_LINES; i++)
	{
		const int LineIndex = ((m_CurrentLine - i) + MAX_LINES) % MAX_LINES;
		CLine &Line = m_aLines[LineIndex];
		if(!Line.m_Initialized)
			break;
		if(Now > Line.m_Time + 16 * time_freq() && !m_PrevShowChat)
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

		const char *pDisplayedTranslatedText = pTranslatedText;
		const char *pDisplayedTranslatedLanguage = pTranslatedLanguage;
		const bool ShowOriginalOnHover = pTranslatedText != nullptr && HoveredTranslateLineIndex == LineIndex;
		if(ShowOriginalOnHover)
		{
			pDisplayedTranslatedText = pText;
			pDisplayedTranslatedLanguage = nullptr;
		}

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

			CTextCursor AppendCursor = MeasureCursor;
			AppendCursor.m_LongestLineWidth = 0.0f;
			if(!IsScoreBoardOpen && !g_Config.m_ClChatOld)
			{
				AppendCursor.m_StartX = MeasureCursor.m_X;
				AppendCursor.m_LineWidth -= MeasureCursor.m_LongestLineWidth;
			}

			if(pDisplayedTranslatedText)
			{
				TextRender()->TextEx(&AppendCursor, pDisplayedTranslatedText);
				if(pDisplayedTranslatedLanguage)
				{
					TextRender()->TextEx(&AppendCursor, " [");
					TextRender()->TextEx(&AppendCursor, pDisplayedTranslatedLanguage);
					TextRender()->TextEx(&AppendCursor, "]");
				}
				TextRender()->TextEx(&AppendCursor, "\n");
				AppendCursor.m_FontSize *= 0.8f;
				TextRender()->TextEx(&AppendCursor, pText);
				AppendCursor.m_FontSize /= 0.8f;
			}
			else if(pTranslatedError)
			{
				TextRender()->TextEx(&AppendCursor, pText);
				TextRender()->TextEx(&AppendCursor, "\n");
				AppendCursor.m_FontSize *= 0.8f;
				TextRender()->TextEx(&AppendCursor, pTranslatedError);
				AppendCursor.m_FontSize /= 0.8f;
			}
			else
			{
				TextRender()->TextEx(&AppendCursor, pText);
			}

			Line.m_aTextHeight[OffsetType] = AppendCursor.Height();
			Line.m_aMediaPreviewWidth[OffsetType] = 0.0f;
			Line.m_aMediaPreviewHeight[OffsetType] = 0.0f;
			float TotalHeight = Line.m_aTextHeight[OffsetType] + RealMsgPaddingY;
			const bool ShowMediaSlot = ShouldDisplayMediaSlot(Line);
			const bool HideMediaPreview = ShouldHideMediaPreview(Line);
			if(ShowMediaSlot && (HideMediaPreview || (Line.m_MediaState == EMediaState::READY && Line.m_MediaWidth > 0 && Line.m_MediaHeight > 0 && !Line.m_vMediaFrames.empty())))
			{
				const float MaxPreviewWidth = minimum(LineWidth, (float)g_Config.m_BcChatMediaPreviewMaxWidth) * CHAT_MEDIA_PREVIEW_SIZE_SCALE;
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
						Line.m_aMediaPreviewHeight[OffsetType] = maximum(FontSize * 1.6f, 18.0f) * CHAT_MEDIA_PREVIEW_SIZE_SCALE;
					}
					TotalHeight += FontSize * 0.4f + Line.m_aMediaPreviewHeight[OffsetType];
				}
			}
			else if(ShowMediaSlot && (Line.m_MediaState == EMediaState::QUEUED || Line.m_MediaState == EMediaState::LOADING || Line.m_MediaState == EMediaState::DECODING))
			{
				Line.m_aMediaPreviewWidth[OffsetType] = minimum(LineWidth, (float)g_Config.m_BcChatMediaPreviewMaxWidth) * CHAT_MEDIA_PREVIEW_SIZE_SCALE;
				Line.m_aMediaPreviewHeight[OffsetType] = maximum(FontSize * 1.2f, 12.0f) * CHAT_MEDIA_PREVIEW_SIZE_SCALE;
				TotalHeight += FontSize * 0.4f + Line.m_aMediaPreviewHeight[OffsetType];
			}
			else if(ShowMediaSlot && Line.m_MediaState == EMediaState::FAILED)
			{
				Line.m_aMediaPreviewWidth[OffsetType] = minimum(LineWidth, (float)g_Config.m_BcChatMediaPreviewMaxWidth) * CHAT_MEDIA_PREVIEW_SIZE_SCALE;
				Line.m_aMediaPreviewHeight[OffsetType] = maximum(FontSize * 2.1f, 18.0f) * CHAT_MEDIA_PREVIEW_SIZE_SCALE;
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
		else if(Line.m_ClientId >= 0 && g_Config.m_TcWarList && g_Config.m_TcWarListChat && GameClient()->m_WarList.GetAnyWar(Line.m_ClientId)) // TClient
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

		CTextCursor AppendCursor = LineCursor;
		AppendCursor.m_LongestLineWidth = 0.0f;
		if(!IsScoreBoardOpen && !g_Config.m_ClChatOld)
		{
			AppendCursor.m_StartX = LineCursor.m_X;
			AppendCursor.m_LineWidth -= LineCursor.m_LongestLineWidth;
		}

		if(pDisplayedTranslatedText)
		{
			const float TranslateRectX = AppendCursor.m_X;
			const float TranslateRectY = AppendCursor.m_Y;
			const float TextLineWidth = maximum(1.0f, AppendCursor.m_LineWidth);
			const STextBoundingBox DisplayedBoundingBox = TextRender()->TextBoundingBox(FontSize, pDisplayedTranslatedText, -1, TextLineWidth);
			float HoverRectWidth = DisplayedBoundingBox.m_W;
			float HoverRectHeight = DisplayedBoundingBox.m_H;
			if(pTranslatedText != nullptr && pText != nullptr)
			{
				const STextBoundingBox TranslatedBoundingBox = TextRender()->TextBoundingBox(FontSize, pTranslatedText, -1, TextLineWidth);
				const STextBoundingBox OriginalBoundingBox = TextRender()->TextBoundingBox(FontSize, pText, -1, TextLineWidth);
				HoverRectWidth = maximum(TranslatedBoundingBox.m_W, OriginalBoundingBox.m_W);
				HoverRectHeight = maximum(TranslatedBoundingBox.m_H, OriginalBoundingBox.m_H);
			}
			Line.m_TranslateRect.m_X = TranslateRectX;
			Line.m_TranslateRect.m_Y = TranslateRectY;
			Line.m_TranslateRect.m_W = maximum(1.0f, HoverRectWidth);
			Line.m_TranslateRect.m_H = maximum(FontSize, HoverRectHeight);
			Line.m_TranslateRectValid = true;
			Line.m_TranslateLanguageRectValid = false;
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &AppendCursor, pDisplayedTranslatedText);
			if(pDisplayedTranslatedLanguage)
			{
				ColorRGBA ColorLang = Color;
				ColorLang.r *= 0.8f;
				ColorLang.g *= 0.8f;
				ColorLang.b *= 0.8f;
				TextRender()->TextColor(ColorLang);
				TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &AppendCursor, " [");
				const float RectX = AppendCursor.m_X;
				const float RectY = AppendCursor.m_Y;
				TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &AppendCursor, pDisplayedTranslatedLanguage);
				Line.m_TranslateLanguageRect.m_X = RectX;
				Line.m_TranslateLanguageRect.m_Y = RectY;
				Line.m_TranslateLanguageRect.m_W = maximum(1.0f, AppendCursor.m_X - RectX);
				Line.m_TranslateLanguageRect.m_H = FontSize;
				Line.m_TranslateLanguageRectValid = true;
				const float TranslateRight = maximum(Line.m_TranslateRect.m_X + Line.m_TranslateRect.m_W, Line.m_TranslateLanguageRect.m_X + Line.m_TranslateLanguageRect.m_W);
				const float TranslateBottom = maximum(Line.m_TranslateRect.m_Y + Line.m_TranslateRect.m_H, Line.m_TranslateLanguageRect.m_Y + Line.m_TranslateLanguageRect.m_H);
				Line.m_TranslateRect.m_W = maximum(1.0f, TranslateRight - Line.m_TranslateRect.m_X);
				Line.m_TranslateRect.m_H = maximum(FontSize, TranslateBottom - Line.m_TranslateRect.m_Y);
				TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &AppendCursor, "]");
			}
			ColorRGBA ColorSub = Color;
			ColorSub.r *= 0.7f;
			ColorSub.g *= 0.7f;
			ColorSub.b *= 0.7f;
			TextRender()->TextColor(ColorSub);
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &AppendCursor, "\n");
			AppendCursor.m_FontSize *= 0.8f;
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &AppendCursor, pText);
			AppendCursor.m_FontSize /= 0.8f;
			TextRender()->TextColor(Color);
		}
		else if(pTranslatedError)
		{
			Line.m_TranslateRectValid = false;
			Line.m_TranslateLanguageRectValid = false;
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &AppendCursor, pText);
			ColorRGBA ColorSub = Color;
			ColorSub.r = 0.7f;
			ColorSub.g = 0.6f;
			ColorSub.b = 0.6f;
			TextRender()->TextColor(ColorSub);
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &AppendCursor, "\n");
			AppendCursor.m_FontSize *= 0.8f;
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &AppendCursor, pTranslatedError);
			AppendCursor.m_FontSize /= 0.8f;
			TextRender()->TextColor(Color);
		}
		else
		{
			Line.m_TranslateRectValid = false;
			Line.m_TranslateLanguageRectValid = false;
			ColoredParts.AddSplitsToCursor(AppendCursor);
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &AppendCursor, pText);
			AppendCursor.m_vColorSplits.clear();
		}

		if(AppendCursor.m_SelectionStart >= 0 && AppendCursor.m_SelectionEnd >= 0)
		{
			Line.m_SelectionStart = AppendCursor.m_SelectionStart;
			Line.m_SelectionEnd = AppendCursor.m_SelectionEnd;
		}
		else
		{
			Line.m_SelectionStart = LineCursor.m_SelectionStart;
			Line.m_SelectionEnd = LineCursor.m_SelectionEnd;
		}

		if(!g_Config.m_ClChatOld && (Line.m_aText[0] != '\0' || Line.m_aName[0] != '\0'))
		{
			float FullWidth = RealMsgPaddingX * 1.5f;
			if(!IsScoreBoardOpen && !g_Config.m_ClChatOld)
			{
				FullWidth += LineCursor.m_LongestLineWidth + AppendCursor.m_LongestLineWidth;
			}
			else
			{
				FullWidth += std::max(LineCursor.m_LongestLineWidth, AppendCursor.m_LongestLineWidth);
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

CUIRect CChat::GetHudRect(float HudWidth, float HudHeight, bool ForcePreview) const
{
	if(!ForcePreview && !HudLayout::IsEnabled(HudLayout::MODULE_CHAT))
		return {0.0f, 0.0f, 0.0f, 0.0f};

	const auto Layout = HudLayout::Get(HudLayout::MODULE_CHAT, HudWidth, HudHeight);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	const bool IsScoreBoardOpen = GameClient()->m_Scoreboard.IsShown() && (Graphics()->ScreenAspect() > 1.7f);
	const bool ShowLargeArea = ForcePreview || m_Show || (m_Mode != MODE_NONE && g_Config.m_ClShowChat == 1) || g_Config.m_ClShowChat == 2;
	const float VisibleHeight = IsScoreBoardOpen ? 93.0f * Scale : (ShowLargeArea ? 223.0f * Scale : 73.0f * Scale);
	float ExtraTop = 0.0f;
	float ExtraBottom = 0.0f;
	float VisibleWidth = ChatWidth();

	if(ForcePreview || m_Mode != MODE_NONE)
	{
		const float ScaledFontSize = FontSize() * (8.0f / 6.0f);
		const float TranslateButtonSize = maximum(16.0f, ScaledFontSize * 1.35f);
		const float TranslateButtonGap = 4.0f;
		const float InputLineWidth = maximum(ChatWidth() - 190.0f * Scale, 190.0f * Scale);
		const float ModeSuffixWidth = TextRender()->TextWidth(ScaledFontSize, ": ");
		const float PrefixWidth = maximum(
			TextRender()->TextWidth(ScaledFontSize, Localize("All")) + ModeSuffixWidth,
			maximum(
				TextRender()->TextWidth(ScaledFontSize, Localize("Team")) + ModeSuffixWidth,
				TextRender()->TextWidth(ScaledFontSize, Localize("Chat")) + ModeSuffixWidth));
		const float InputAndTranslateWidth = maximum(InputLineWidth, PrefixWidth + 40.0f + TranslateButtonGap + TranslateButtonSize);

		VisibleWidth = maximum(VisibleWidth, InputAndTranslateWidth);
		ExtraTop = ScaledFontSize;
		ExtraBottom = maximum(2.25f * ScaledFontSize, maximum(ScaledFontSize + 4.0f, 16.0f));
	}

	CUIRect Rect = {Layout.m_X, Layout.m_Y - VisibleHeight - ExtraTop, VisibleWidth, VisibleHeight + ExtraTop + ExtraBottom};
	Rect.x = std::clamp(Rect.x, 0.0f, maximum(0.0f, HudWidth - Rect.w));
	Rect.y = std::clamp(Rect.y, 0.0f, maximum(0.0f, HudHeight - Rect.h));
	return Rect;
}

void CChat::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	// send pending chat messages
	if(m_PendingChatCounter > 0 && m_LastChatSend + time_freq() < time())
	{
		CHistoryEntry *pEntry = m_History.Last();
		for(int i = m_PendingChatCounter - 1; pEntry; --i, pEntry = m_History.Prev(pEntry))
		{
			if(i == 0)
			{
				SendChat(pEntry->m_Team, pEntry->m_aText);
				break;
			}
		}
		--m_PendingChatCounter;
	}

	UpdateMediaDownloads();

	const float Height = 300.0f;
	const float Width = Height * Graphics()->ScreenAspect();
	Graphics()->MapScreenToSize(Width, Height);

	// Determine which translated line the cursor hovers over (using last frame's screen-space rects)
	int HoveredTranslateLineIndex = -1;
	{
		const vec2 MousePos = ChatMousePos();
		for(int LineIdx = 0; LineIdx < MAX_LINES; ++LineIdx)
		{
			const CLine &Line = m_aLines[LineIdx];
			if(!Line.m_TranslateRectValid && !Line.m_TranslateLanguageRectValid)
				continue;
			const bool HoveredText = Line.m_TranslateRectValid &&
				MousePos.x >= Line.m_TranslateRect.m_X && MousePos.x <= Line.m_TranslateRect.m_X + Line.m_TranslateRect.m_W &&
				MousePos.y >= Line.m_TranslateRect.m_Y && MousePos.y <= Line.m_TranslateRect.m_Y + Line.m_TranslateRect.m_H;
			const bool HoveredLang = Line.m_TranslateLanguageRectValid &&
				MousePos.x >= Line.m_TranslateLanguageRect.m_X && MousePos.x <= Line.m_TranslateLanguageRect.m_X + Line.m_TranslateLanguageRect.m_W &&
				MousePos.y >= Line.m_TranslateLanguageRect.m_Y && MousePos.y <= Line.m_TranslateLanguageRect.m_Y + Line.m_TranslateLanguageRect.m_H;
			if(HoveredText || HoveredLang)
			{
				HoveredTranslateLineIndex = LineIdx;
				break;
			}
		}
	}
	for(auto &Line : m_aLines)
	{
		Line.m_MediaPreviewRectValid = false;
		Line.m_MediaRetryRectValid = false;
	}
	m_TranslateButtonRectValid = false;

	const auto ChatLayout = HudLayout::Get(HudLayout::MODULE_CHAT, Width, Height);
	const float LayoutScale = std::clamp(ChatLayout.m_Scale / 100.0f, 0.25f, 3.0f);
	float x = ChatLayout.m_X;

	// TClient
	float y = ChatLayout.m_Y;
	// float y = 300.0f - 20.0f * FontSize() / 6.0f;

	float ScaledFontSize = FontSize() * (8.0f / 6.0f);
	const bool BcChatMessageAnimEnabled = BCUiAnimations::Enabled() && g_Config.m_BcChatAnimation != 0;
	const bool BcChatOpenAnimEnabled = BcChatMessageAnimEnabled && g_Config.m_BcChatOpenAnimation != 0 && g_Config.m_BcChatOpenAnimationMs > 0;
	const bool BcChatTypingAnimEnabled = BcChatMessageAnimEnabled && g_Config.m_BcChatTypingAnimation != 0 && g_Config.m_BcChatTypingAnimationMs > 0;
	float ChatOpenOffsetY = 0.0f;
	if(m_Mode != MODE_NONE && BcChatOpenAnimEnabled && m_ChatOpenAnimationStart > 0)
	{
		const float Dur = BCUiAnimations::MsToSeconds(g_Config.m_BcChatOpenAnimationMs);
		const float Age = (time_get() - m_ChatOpenAnimationStart) / (float)time_freq();
		const float Progress = Dur > 0.0f ? std::clamp(Age / Dur, 0.0f, 1.0f) : 1.0f;
		const float ChatOpenEase = BCUiAnimations::EaseOutCubic(Progress);
		ChatOpenOffsetY = Height * (1.0f - ChatOpenEase);
	}
	if(m_Mode != MODE_NONE)
	{
		const vec2 MousePos = ChatMousePos();
		const bool MouseDown = Input()->KeyIsPressed(KEY_MOUSE_1);
		if(!m_ScrollbarDragging)
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

		// render chat input
		CTextCursor InputCursor;
		InputCursor.SetPosition(vec2(x, y + ChatOpenOffsetY));
		InputCursor.m_FontSize = ScaledFontSize;
		InputCursor.m_LineWidth = ChatWidth() - 190.0f * LayoutScale;
		InputCursor.m_LineWidth = std::max(InputCursor.m_LineWidth, 190.0f * LayoutScale);

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
		const bool CaretAnimEnabled = BcChatTypingAnimEnabled && TypingAnimDuration > 0.0f;
		m_Input.SetHideCursor(CaretAnimEnabled);
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
			const float Ease = BCUiAnimations::EaseOutCubic(Progress);
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

		if(CaretAnimEnabled)
		{
			// Letters snap to their final X immediately (only their fade-in animates), so the caret
			// only has to cover a small distance (usually one glyph) per keystroke. Chasing it over the
			// full letter-fade duration falls behind during fast typing, since a new keystroke retargets
			// the animation before it catches up. Cap the caret's own travel time much shorter so it keeps up.
			const float CaretAnimDuration = std::min(TypingAnimDuration, 0.09f);
			const vec2 CaretTargetPos = m_Input.GetCaretPosition();
			const int64_t NowTicks = time_get();
			if(!m_CaretAnimValid)
			{
				m_CaretAnimFromPos = CaretTargetPos;
				m_CaretAnimTargetPos = CaretTargetPos;
				m_CaretVisualPos = CaretTargetPos;
				m_CaretAnimStartTime = NowTicks;
				m_CaretAnimValid = true;
			}
			else if(length(CaretTargetPos - m_CaretAnimTargetPos) > 0.01f)
			{
				m_CaretAnimFromPos = m_CaretVisualPos;
				m_CaretAnimTargetPos = CaretTargetPos;
				m_CaretAnimStartTime = NowTicks;
			}

			const float CaretAnimAge = (NowTicks - m_CaretAnimStartTime) / (float)time_freq();
			const float CaretEase = BCUiAnimations::EaseOutCubic(CaretAnimAge / CaretAnimDuration);
			m_CaretVisualPos = m_CaretAnimFromPos + (m_CaretAnimTargetPos - m_CaretAnimFromPos) * CaretEase;

			// Mirror the engine's own caret blink timing (500ms on/off, forced visible while typing/moving).
			if(Changed)
				m_CaretBlinkAnchor = NowTicks - (int64_t)(0.501f * time_freq());
			else if(NowTicks - m_CaretBlinkAnchor > time_freq())
				m_CaretBlinkAnchor = NowTicks;
			const bool CaretVisible = Changed || (NowTicks - m_CaretBlinkAnchor) > time_freq() / 2;

			if(CaretVisible)
			{
				float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
				const CScreenRect ScreenRect = Graphics()->GetScreen();
				ScreenX0 = ScreenRect.m_TopLeft.x;
				ScreenY0 = ScreenRect.m_TopLeft.y;
				ScreenX1 = ScreenRect.m_BottomRight.x;
				ScreenY1 = ScreenRect.m_BottomRight.y;
				const float CursorInnerWidth = ((ScreenX1 - ScreenX0) / Graphics()->ScreenWidth()) * 2.0f;
				const float CursorOuterWidth = CursorInnerWidth * 2.0f;
				const float CursorOuterInnerDiff = (CursorOuterWidth - CursorInnerWidth) / 2.0f;
				const float CaretHeight = InputCursor.m_FontSize;

				Graphics()->TextureClear();
				Graphics()->QuadsBegin();

				Graphics()->SetColor(TextRender()->DefaultTextOutlineColor());
				const IGraphics::CQuadItem OuterQuad(m_CaretVisualPos.x - CursorOuterInnerDiff, m_CaretVisualPos.y, CursorOuterWidth, CaretHeight);
				Graphics()->QuadsDrawTL(&OuterQuad, 1);

				Graphics()->SetColor(TextRender()->DefaultTextColor());
				const IGraphics::CQuadItem InnerQuad(m_CaretVisualPos.x, m_CaretVisualPos.y + CursorOuterInnerDiff, CursorInnerWidth, CaretHeight - CursorOuterInnerDiff * 2.0f);
				Graphics()->QuadsDrawTL(&InnerQuad, 1);

				Graphics()->QuadsEnd();
				Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
			}
		}

		Graphics()->ClipDisable();

		// Scroll up or down to keep the caret inside the clipping rect
		const float CaretPositionY = m_Input.GetCaretPosition().y - ScrollOffsetChange;
		if(CaretPositionY < ClippingRect.y)
			ScrollOffsetChange -= ClippingRect.y - CaretPositionY;
		else if(CaretPositionY + InputCursor.m_FontSize > ClippingRect.y + ClippingRect.h)
			ScrollOffsetChange += CaretPositionY + InputCursor.m_FontSize - (ClippingRect.y + ClippingRect.h);

		Ui()->DoSmoothScrollLogic(&ScrollOffset, &ScrollOffsetChange, ClippingRect.h, BoundingBox.m_H);

		m_Input.SetScrollOffset(ScrollOffset);
		m_Input.SetScrollOffsetChange(ScrollOffsetChange);

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

		// Translate settings button — to the right of the text input, in UI coordinate space.
		{
			const float UiScale = Ui()->Screen()->h / Height;
			// Use y (top of the input row) and font height so the icon sits level with the typed text
			const float BtnH = InputCursor.m_FontSize;
			m_TranslateButtonUiRect = {
				(ClippingRect.x + ClippingRect.w + TranslateButtonGap) * UiScale,
				y * UiScale,
				TranslateButtonSize * UiScale,
				BtnH * UiScale};
			// Store chat-space rect for OnInput click detection via ChatMousePos()
			m_TranslateButtonRect.m_X = ClippingRect.x + ClippingRect.w + TranslateButtonGap;
			m_TranslateButtonRect.m_Y = y;
			m_TranslateButtonRect.m_W = TranslateButtonSize;
			m_TranslateButtonRect.m_H = BtnH;
			m_TranslateButtonRectValid = true;
			Ui()->MapScreen();
			RenderTranslateSettingsButton(m_TranslateButtonUiRect);
			Graphics()->MapScreenToSize(Width, Height);
			if(Ui()->HotItem() == &m_TranslateSettingsButton || m_TranslateButtonPressed)
			{
				m_MouseIsPress = false;
				m_HasSelection = false;
			}
		}

		if(m_Input.HasSelection())
			m_HasSelection = false;
	}

#if defined(CONF_VIDEORECORDER)
	if(!((g_Config.m_ClShowChat && !IVideo::Current()) || (g_Config.m_ClVideoShowChat && IVideo::Current())))
#else
	if(!g_Config.m_ClShowChat)
#endif
		return;

	if(g_Config.m_ClFocusMode && g_Config.m_ClFocusModeHideChat)
		return;

	y -= ScaledFontSize;

	bool IsScoreBoardOpen = GameClient()->m_Scoreboard.IsShown() && (Graphics()->ScreenAspect() > 1.7f); // only assume scoreboard when screen ratio is widescreen(something around 16:9)

	int64_t Now = time();
	const bool ShowLargeArea = m_Show || (m_Mode != MODE_NONE && g_Config.m_ClShowChat == 1) || g_Config.m_ClShowChat == 2;
	const auto ShouldExpandCompactAreaForMedia = [&]() {
		if(IsScoreBoardOpen || ShowLargeArea || !g_Config.m_BcChatMediaPreview || !AnyMediaAllowed())
			return false;
		for(int i = 0; i < 3; ++i)
		{
			const CLine &RecentLine = m_aLines[((m_CurrentLine - i) + MAX_LINES) % MAX_LINES];
			if(!RecentLine.m_Initialized)
				break;
			if(ShouldDisplayMediaSlot(RecentLine))
				return true;
		}
		return false;
	};
	const bool ExpandCompactAreaForMedia = ShouldExpandCompactAreaForMedia();
	const float VisibleHeight = IsScoreBoardOpen ? 93.0f * LayoutScale : (ShowLargeArea ? 223.0f * LayoutScale : (ExpandCompactAreaForMedia ? CHAT_MEDIA_COMPACT_EXPANDED_HEIGHT * LayoutScale : 73.0f * LayoutScale));
	float HeightLimit = y - VisibleHeight;
	int OffsetType = IsScoreBoardOpen ? 1 : 0;

	float RealMsgPaddingX = MessagePaddingX();
	float RealMsgPaddingY = MessagePaddingY();

	if(g_Config.m_ClChatOld)
	{
		RealMsgPaddingX = 0;
		RealMsgPaddingY = 0;
	}

	// Mouse selection belongs to typing mode only; expand-only (m_Show) still scrolls.
	const bool SelectionActive = m_Mode != MODE_NONE;
	const bool ChatInteractionActive = m_Mode != MODE_NONE || m_Show;
	if(!SelectionActive)
	{
		m_MouseIsPress = false;
		m_HasSelection = false;
		m_WantsSelectionCopy = false;
	}

	if(ChatInteractionActive)
	{
		// Count total and visible lines to determine MaxScroll
		int TotalLines = 0;
		for(int i = 0; i < MAX_LINES; i++)
		{
			const CLine &Line = m_aLines[((m_CurrentLine - i) + MAX_LINES) % MAX_LINES];
			if(!Line.m_Initialized)
				break;
			TotalLines++;
		}

		float TempY = y;
		int VisibleLines = 0;
		for(int i = m_BacklogCurLine; i < MAX_LINES; i++)
		{
			const CLine &Line = m_aLines[((m_CurrentLine - i) + MAX_LINES) % MAX_LINES];
			if(!Line.m_Initialized)
				break;
			if(TempY - Line.m_aYOffset[OffsetType] < HeightLimit)
				break;
			TempY -= Line.m_aYOffset[OffsetType];
			VisibleLines++;
		}
		const int MaxScroll = maximum(0, TotalLines - VisibleLines);
		m_BacklogCurLine = maximum(0, minimum(m_BacklogCurLine, MaxScroll));

		if(MaxScroll > 0)
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
				ScrollbarRect.x = x - CHAT_SCROLLBAR_WIDTH - CHAT_SCROLLBAR_MARGIN;
				ScrollbarRect.y = LogTop;
				ScrollbarRect.w = CHAT_SCROLLBAR_WIDTH;
				ScrollbarRect.h = LogHeight;

				CUIRect Rail;
				ScrollbarRect.Margin(RailMargin, &Rail);
				CUIRect Handle;
				const float HandleHeight = maximum(Rail.w, minimum(24.0f, Rail.h / 3.0f));
				Rail.HSplitTop(HandleHeight, &Handle, nullptr);
				Handle.y = Rail.y + (Rail.h - Handle.h) * Current;

				const vec2 NativePos = Input()->NativeMousePos();
				const vec2 ScreenPos = vec2(NativePos.x / Graphics()->WindowWidth() * Width, NativePos.y / Graphics()->WindowHeight() * Height);
				const bool MouseDown = Input()->NativeMousePressed(1);

				const auto InsideRect = [&](const CUIRect &Rect) {
					return ScreenPos.x >= Rect.x && ScreenPos.x <= Rect.x + Rect.w && ScreenPos.y >= Rect.y && ScreenPos.y <= Rect.y + Rect.h;
				};

				if(!MouseDown)
				{
					m_ScrollbarDragging = false;
				}
				else if(!m_ScrollbarDragging && InsideRect(Rail))
				{
					if(InsideRect(Handle))
						m_ScrollbarDragOffset = ScreenPos.y - Handle.y;
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
						const float Cur = ScreenPos.y - m_ScrollbarDragOffset;
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
	}

	OnPrepareLines(x, y, m_BacklogCurLine, HoveredTranslateLineIndex);
	std::string SelectionString;
	bool HasChatSelection = false;

	for(int i = m_BacklogCurLine; i < MAX_LINES; i++)
	{
		const int LineIndex = ((m_CurrentLine - i) + MAX_LINES) % MAX_LINES;
		CLine &Line = m_aLines[LineIndex];
		if(!Line.m_Initialized)
			break;
		if(Now > Line.m_Time + 16 * time_freq() && !m_PrevShowChat)
			break;

		y -= Line.m_aYOffset[OffsetType];

		// cut off if msgs waste too much space
		if(y < HeightLimit && i != m_BacklogCurLine)
			break;

		float Blend = Now > Line.m_Time + 14 * time_freq() && !m_PrevShowChat ? 1.0f - (Now - Line.m_Time - 14 * time_freq()) / (2.0f * time_freq()) : 1.0f;

		// BestClient: lift newly received messages from the bottom.
		float BcLineYOffset = 0.0f;
		if(BcChatMessageAnimEnabled && g_Config.m_BcChatAnimationMs > 0 && Line.m_Time > 0)
		{
			const float Dur = BCUiAnimations::MsToSeconds(g_Config.m_BcChatAnimationMs);
			const float Age = (Now - Line.m_Time) / (float)time_freq();
			const float Progress = Dur > 0.0f ? std::clamp(Age / Dur, 0.0f, 1.0f) : 1.0f;
			const float Ease = BCUiAnimations::EaseOutCubic(Progress);
			BcLineYOffset = 42.0f * (1.0f - Ease);
		}
		const float LineRenderY = y + BcLineYOffset;

		// Draw backgrounds for messages in one batch
		if(!g_Config.m_ClChatOld)
		{
			Graphics()->TextureClear();
			if(Line.m_QuadContainerIndex != -1)
			{
				Graphics()->SetColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClChatBackgroundColor, true)).WithMultipliedAlpha(Blend));
				Graphics()->RenderQuadContainerEx(Line.m_QuadContainerIndex, 0, -1, 0.0f, ((LineRenderY + RealMsgPaddingY / 2.0f) - Line.m_TextYOffset));
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
				vec2 TeeRenderPos(x + (RealMsgPaddingX + TeeSize) / 2.0f, LineRenderY + OffsetTeeY + FullHeightMinusTee / 2.0f + OffsetToMid.y);
				RenderTools()->RenderTee(pIdleState, &TeeRenderInfo, EMOTE_NORMAL, vec2(1, 0.1f), TeeRenderPos, Blend);
			}

			const ColorRGBA TextColor = TextRender()->DefaultTextColor().WithMultipliedAlpha(Blend);
			const ColorRGBA TextOutlineColor = TextRender()->DefaultTextOutlineColor().WithMultipliedAlpha(Blend);
			const float TextOffsetY = (LineRenderY + RealMsgPaddingY / 2.0f) - Line.m_TextYOffset;
			TextRender()->RenderTextContainer(Line.m_TextContainerIndex, TextColor, TextOutlineColor, 0.0f, TextOffsetY);

			// Adjust translate rects to screen space for next frame's hover detection
			if(Line.m_TranslateRectValid || Line.m_TranslateLanguageRectValid)
			{
				if(Line.m_TranslateRectValid)
				{
					Line.m_TranslateRect.m_Y += TextOffsetY;
					const vec2 MousePos = ChatMousePos();
					const bool Hovered = MousePos.x >= Line.m_TranslateRect.m_X && MousePos.x <= Line.m_TranslateRect.m_X + Line.m_TranslateRect.m_W &&
						MousePos.y >= Line.m_TranslateRect.m_Y && MousePos.y <= Line.m_TranslateRect.m_Y + Line.m_TranslateRect.m_H;
					if(Hovered)
						Ui()->SetHotItem(&Line.m_TranslateRect);
				}
				if(Line.m_TranslateLanguageRectValid)
				{
					Line.m_TranslateLanguageRect.m_Y += TextOffsetY;
				}
			}

			if(Line.m_SelectionStart >= 0 && Line.m_SelectionEnd >= 0 && Line.m_SelectionStart != Line.m_SelectionEnd)
			{
				HasChatSelection = true;
				if(m_WantsSelectionCopy)
				{
					const std::string PlainText = BuildPlainTextLine(Line, HoveredTranslateLineIndex);
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
			const bool HideNsfwMedia = ShouldHideNsfwMedia(Line);
			const bool HasMediaPreview = Line.m_aMediaPreviewWidth[OffsetType] > 0.0f && Line.m_aMediaPreviewHeight[OffsetType] > 0.0f;
			const float PreviewX = x + RealMsgPaddingX / 2.0f;
			const float PreviewY = Line.m_TextYOffset + TextOffsetY + Line.m_aTextHeight[OffsetType] + FontSize() * 0.4f;
			const float PreviewW = Line.m_aMediaPreviewWidth[OffsetType];
			const float PreviewH = Line.m_aMediaPreviewHeight[OffsetType];
			Line.m_MediaPreviewRectValid = false;
			if(ShowMediaSlot && HasMediaPreview)
			{
				auto DrawMediaPreviewFrame = [&](ColorRGBA FillColor, float &InnerPreviewX, float &InnerPreviewY, float &InnerPreviewW, float &InnerPreviewH, float &InnerPreviewRounding) {
					const float PreviewBorder = maximum(0.35f, FontSize() * 0.025f);
					const float PreviewRounding = minimum(minimum(PreviewW, PreviewH) / 2.0f, maximum(4.0f, FontSize() * 0.55f));
					Graphics()->DrawRect(PreviewX, PreviewY, PreviewW, PreviewH, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f * Blend), IGraphics::CORNER_ALL, PreviewRounding);
					InnerPreviewX = PreviewX + PreviewBorder;
					InnerPreviewY = PreviewY + PreviewBorder;
					InnerPreviewW = maximum(1.0f, PreviewW - PreviewBorder * 2.0f);
					InnerPreviewH = maximum(1.0f, PreviewH - PreviewBorder * 2.0f);
					InnerPreviewRounding = maximum(0.0f, PreviewRounding - PreviewBorder);
					Graphics()->DrawRect(InnerPreviewX, InnerPreviewY, InnerPreviewW, InnerPreviewH, FillColor, IGraphics::CORNER_ALL, InnerPreviewRounding);
				};

				float InnerPreviewX = PreviewX;
				float InnerPreviewY = PreviewY;
				float InnerPreviewW = PreviewW;
				float InnerPreviewH = PreviewH;
				float InnerPreviewRounding = 0.0f;

				if(HideMediaPreview || HideNsfwMedia)
				{
					const char *pHiddenLabel = HideNsfwMedia ? "NSFW content hidden" : "hidden media";
					DrawMediaPreviewFrame(ColorRGBA(0.10f, 0.10f, 0.10f, 0.82f * Blend), InnerPreviewX, InnerPreviewY, InnerPreviewW, InnerPreviewH, InnerPreviewRounding);

					CTextCursor HiddenCursor;
					const float HiddenFontSize = FontSize() * 0.72f;
					const float HiddenLabelWidth = TextRender()->TextWidth(HiddenFontSize, pHiddenLabel);
					HiddenCursor.SetPosition(vec2(InnerPreviewX + maximum(FontSize() * 0.35f, (InnerPreviewW - HiddenLabelWidth) / 2.0f), InnerPreviewY + maximum(FontSize() * 0.25f, (InnerPreviewH - HiddenFontSize) / 2.0f)));
					HiddenCursor.m_FontSize = HiddenFontSize;
					TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.9f * Blend);
					TextRender()->TextEx(&HiddenCursor, pHiddenLabel);
					TextRender()->TextColor(TextRender()->DefaultTextColor());

					Line.m_MediaRetryRectValid = false;
					Line.m_MediaPreviewRect.m_X = PreviewX;
					Line.m_MediaPreviewRect.m_Y = PreviewY;
					Line.m_MediaPreviewRect.m_W = PreviewW;
					Line.m_MediaPreviewRect.m_H = PreviewH;
					Line.m_MediaPreviewRectValid = true;
				}
				else if(Line.m_MediaState == EMediaState::READY)
				{
					IGraphics::CTextureHandle MediaTexture;
					if(GetCurrentFrameTexture(Line, MediaTexture))
					{
						DrawMediaPreviewFrame(ColorRGBA(0.05f, 0.05f, 0.05f, 0.18f * Blend), InnerPreviewX, InnerPreviewY, InnerPreviewW, InnerPreviewH, InnerPreviewRounding);
						DrawRoundedMediaPreview(Graphics(), MediaTexture, InnerPreviewX, InnerPreviewY, InnerPreviewW, InnerPreviewH, InnerPreviewRounding, Blend);

						Line.m_MediaRetryRectValid = false;
						Line.m_MediaPreviewRect.m_X = PreviewX;
						Line.m_MediaPreviewRect.m_Y = PreviewY;
						Line.m_MediaPreviewRect.m_W = PreviewW;
						Line.m_MediaPreviewRect.m_H = PreviewH;
						Line.m_MediaPreviewRectValid = true;
					}
				}
				else if(Line.m_MediaState == EMediaState::QUEUED || Line.m_MediaState == EMediaState::LOADING || Line.m_MediaState == EMediaState::DECODING)
				{
					DrawMediaPreviewFrame(ColorRGBA(0.12f, 0.12f, 0.12f, 0.75f * Blend), InnerPreviewX, InnerPreviewY, InnerPreviewW, InnerPreviewH, InnerPreviewRounding);

					CTextCursor LoadingCursor;
					LoadingCursor.SetPosition(vec2(InnerPreviewX + FontSize() * 0.35f, InnerPreviewY + InnerPreviewH * 0.15f));
					LoadingCursor.m_FontSize = FontSize() * 0.75f;
					TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.8f * Blend);
					TextRender()->TextEx(&LoadingCursor, "Loading media...");
					TextRender()->TextColor(TextRender()->DefaultTextColor());
					Line.m_MediaRetryRectValid = false;
				}
				else if(Line.m_MediaState == EMediaState::FAILED)
				{
					const bool CanRetry = Line.m_MediaRetryCount < CHAT_MEDIA_MAX_RETRIES && !Line.m_vMediaCandidates.empty();
					DrawMediaPreviewFrame(ColorRGBA(0.23f, 0.10f, 0.10f, 0.82f * Blend), InnerPreviewX, InnerPreviewY, InnerPreviewW, InnerPreviewH, InnerPreviewRounding);

					CTextCursor FailedCursor;
					FailedCursor.SetPosition(vec2(InnerPreviewX + FontSize() * 0.35f, InnerPreviewY + FontSize() * 0.25f));
					FailedCursor.m_FontSize = FontSize() * 0.70f;
					TextRender()->TextColor(1.0f, 0.85f, 0.85f, 0.95f * Blend);
					TextRender()->TextEx(&FailedCursor, CanRetry ? "Media preview unavailable" : "Media preview unavailable (retry limit reached)");

					const char *pRetryLabel = CanRetry ? "Retry" : "Retry limit reached";
					const float RetryFont = FontSize() * 0.66f;
					const float RetryLabelWidth = TextRender()->TextWidth(RetryFont, pRetryLabel);
					const float RetryW = maximum(FontSize() * 4.2f, RetryLabelWidth + FontSize() * 0.8f);
					const float RetryH = maximum(FontSize() * 0.95f, 12.0f);
					const float RetryX = InnerPreviewX + InnerPreviewW - RetryW - FontSize() * 0.25f;
					const float RetryY = InnerPreviewY + InnerPreviewH - RetryH - FontSize() * 0.25f;

					Graphics()->DrawRect(RetryX, RetryY, RetryW, RetryH, CanRetry ? ColorRGBA(0.86f, 0.28f, 0.28f, 0.95f * Blend) : ColorRGBA(0.35f, 0.35f, 0.35f, 0.75f * Blend), IGraphics::CORNER_ALL, maximum(2.0f, RetryH * 0.3f));

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

	// Render translate settings popup (switch to UI coordinate space first).
	// Ui()->Update() must be called here because CMenus skips it during gameplay.
	if(m_Mode != MODE_NONE && Ui()->IsPopupOpen(&m_TranslateSettingsPopupId))
	{
		Ui()->MapScreen();
		Ui()->Update(vec2(0.0f, 0.0f));
		Ui()->RenderPopupMenus();
		Graphics()->MapScreenToSize(Width, Height);
	}

	RenderFullscreenMedia(Width, Height);

	if(m_Mode != MODE_NONE)
	{
		const vec2 WindowSize(maximum(1.0f, (float)Graphics()->WindowWidth()), maximum(1.0f, (float)Graphics()->WindowHeight()));
		const vec2 UiToChatScale(Width / Ui()->Screen()->w, Height / Ui()->Screen()->h);
		const vec2 UiMousePos = Ui()->UpdatedMousePos() * vec2(Ui()->Screen()->w, Ui()->Screen()->h) / WindowSize;
		RenderTools()->RenderCursor(UiMousePos * UiToChatScale, 12.0f);
	}
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
	// BestClient: fast practice consumes practice chat commands locally
	if(GameClient()->m_FastPractice.ConsumePracticeChatCommand(Team, pLine))
		return;
	if(GameClient()->m_VoiceChat.TryHandleChatCommand(pLine))
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

void CChat::SendChatQueued(const char *pLine)
{
	if(!pLine || *str_utf8_skip_whitespaces(pLine) == '\0')
		return;

	char aConvertedLine[MAX_LINE_LENGTH];
	if(TryConvertWrongLayoutSlashCommand(pLine, aConvertedLine, sizeof(aConvertedLine)))
		pLine = aConvertedLine;

	const int Team = m_Mode == MODE_ALL ? 0 : 1;
	if(GameClient()->m_Translate.TryTranslateOutgoingChat(Team, pLine))
		return;

	bool AddEntry = false;

	if(m_LastChatSend + time_freq() < time())
	{
		SendChat(m_Mode == MODE_ALL ? 0 : 1, pLine);
		AddEntry = true;
	}
	else if(m_PendingChatCounter < 3)
	{
		++m_PendingChatCounter;
		AddEntry = true;
	}

	if(AddEntry)
	{
		const int Length = str_length(pLine);
		CHistoryEntry *pEntry = m_History.Allocate(sizeof(CHistoryEntry) + Length);
		pEntry->m_Team = m_Mode == MODE_ALL ? 0 : 1;
		str_copy(pEntry->m_aText, pLine, Length + 1);
	}
}

void CChat::SendTranslatedChatQueued(int Team, const char *pLine)
{
	if(!pLine || str_length(pLine) < 1)
		return;

	bool AddEntry = false;

	if(m_LastChatSend + time_freq() < time())
	{
		SendChat(Team, pLine);
		AddEntry = true;
	}
	else if(m_PendingChatCounter < 3)
	{
		++m_PendingChatCounter;
		AddEntry = true;
	}

	if(AddEntry)
	{
		const int Length = str_length(pLine);
		CHistoryEntry *pEntry = m_History.Allocate(sizeof(CHistoryEntry) + Length);
		pEntry->m_Team = Team;
		str_copy(pEntry->m_aText, pLine, Length + 1);
	}
}

std::vector<std::string> CChat::SplitWords(const char *pMessage)
{
	std::vector<std::string> Parts;
	if(!pMessage)
		return Parts;

	std::string Str(pMessage);
	size_t Start = 0;
	size_t End = 0;
	while((End = Str.find(' ', Start)) != std::string::npos)
	{
		Parts.push_back(Str.substr(Start, End - Start));
		Start = End + 1;
	}
	Parts.push_back(Str.substr(Start));
	return Parts;
}

void CChat::ConchainRegexPlayerWhitelist(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	if(pResult->NumArguments() == 1)
	{
		auto Re = Regex(pResult->GetString(0));
		if(!Re.error().empty())
		{
			log_error("chat", "Invalid whitelist regex: %s", Re.error().c_str());
			return;
		}
		static_cast<CChat *>(pUserData)->m_RegexPlayerWhitelist = std::move(Re);
	}
	pfnCallback(pResult, pCallbackUserData);
}

void CChat::ConAddWhiteList(IConsole::IResult *pResult, void *pUserData)
{
	CChat *pSelf = static_cast<CChat *>(pUserData);
	const char *pInput = pResult->GetString(0);
	char aInput[256];
	str_copy(aInput, pInput, sizeof(aInput));
	str_utf8_trim_right(aInput);
	char aBuf[256];
	if(!aInput[0])
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Regex", "No nickname given");
		return;
	}

	const bool HadExistingRegex = g_Config.m_BcRegexPlayerWhitelist[0] != '\0';
	char aOldRegex[sizeof(g_Config.m_BcRegexPlayerWhitelist)];
	str_copy(aOldRegex, g_Config.m_BcRegexPlayerWhitelist, sizeof(aOldRegex));
	const char *pNewRegex = aInput;
	char aNewRegex[sizeof(g_Config.m_BcRegexPlayerWhitelist)];
	if(HadExistingRegex)
	{
		str_format(aNewRegex, sizeof(aNewRegex), "%s|%s", g_Config.m_BcRegexPlayerWhitelist, aInput);
		pNewRegex = aNewRegex;
	}

	str_copy(g_Config.m_BcRegexPlayerWhitelist, pNewRegex, sizeof(g_Config.m_BcRegexPlayerWhitelist));

	auto Re = Regex(g_Config.m_BcRegexPlayerWhitelist);
	if(!Re.error().empty())
	{
		str_copy(g_Config.m_BcRegexPlayerWhitelist, aOldRegex, sizeof(g_Config.m_BcRegexPlayerWhitelist));
		str_format(aBuf, sizeof(aBuf), "Invalid regex, list not updated: %s", Re.error().c_str());
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Regex", aBuf);
		return;
	}

	pSelf->m_RegexPlayerWhitelist = std::move(Re);
	if(!HadExistingRegex)
		str_format(aBuf, sizeof(aBuf), "New regex added: %s", aInput);
	else
		str_format(aBuf, sizeof(aBuf), "Added to existing regex: %s", aInput);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Regex", aBuf);
}

void CChat::ConAddCensorList(IConsole::IResult *pResult, void *pUserData)
{
	CChat *pSelf = static_cast<CChat *>(pUserData);
	const char *pInput = pResult->GetString(0);
	char aInput[256];
	str_copy(aInput, pInput, sizeof(aInput));
	str_utf8_trim_right(aInput);
	char aBuf[256];
	if(!aInput[0])
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Regex", "No word given");
		return;
	}

	const bool HadExistingRegex = g_Config.m_TcRegexChatIgnore[0] != '\0';
	char aOldRegex[sizeof(g_Config.m_TcRegexChatIgnore)];
	str_copy(aOldRegex, g_Config.m_TcRegexChatIgnore, sizeof(aOldRegex));
	const char *pNewRegex = aInput;
	char aNewRegex[sizeof(g_Config.m_TcRegexChatIgnore)];
	if(HadExistingRegex)
	{
		str_format(aNewRegex, sizeof(aNewRegex), "%s|%s", g_Config.m_TcRegexChatIgnore, aInput);
		pNewRegex = aNewRegex;
	}

	str_copy(g_Config.m_TcRegexChatIgnore, pNewRegex, sizeof(g_Config.m_TcRegexChatIgnore));

	auto Re = Regex(g_Config.m_TcRegexChatIgnore);
	if(!Re.error().empty())
	{
		str_copy(g_Config.m_TcRegexChatIgnore, aOldRegex, sizeof(g_Config.m_TcRegexChatIgnore));
		str_format(aBuf, sizeof(aBuf), "Invalid regex, list not updated: %s", Re.error().c_str());
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Regex", aBuf);
		return;
	}

	pSelf->GameClient()->m_TClient.m_RegexChatIgnore = std::move(Re);
	if(!HadExistingRegex)
		str_format(aBuf, sizeof(aBuf), "New regex added: %s", aInput);
	else
		str_format(aBuf, sizeof(aBuf), "Added to existing regex: %s", aInput);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Regex", aBuf);
}

const char *CChat::FilterText(const char *pMessage, int ClientId, bool IsChat)
{
	if(!pMessage || !g_Config.m_TcRegexChatIgnore[0] || !g_Config.m_BcEnableCensorList)
		return pMessage;

	static char s_aFilteredMessage[1024];
	s_aFilteredMessage[0] = '\0';
	if(g_Config.m_BcRegexPlayerWhitelist[0] && ClientId >= 0)
	{
		auto &RePlr = m_RegexPlayerWhitelist;
		if(RePlr.error().empty() && RePlr.test(GameClient()->m_aClients[ClientId].m_aName))
			return pMessage;
	}
	auto &Re = GameClient()->m_TClient.m_RegexChatIgnore;
	if(!Re.error().empty())
		return pMessage;
	if(!Re.test(pMessage))
		return pMessage;

	std::vector<std::string> BlockedWords;
	std::vector<std::string> SplitMsg = SplitWords(pMessage);

	if(g_Config.m_BcShowBlockedWordInConsole && IsChat)
	{
		Re.match(pMessage, true, [&BlockedWords](const std::string &Match, int /*MatchIndex*/, int Group) {
			if(Group == 0)
			{
				bool AlreadyBlocked = false;
				for(const auto &BlockedWord : BlockedWords)
				{
					if(BlockedWord == Match)
					{
						AlreadyBlocked = true;
						break;
					}
				}
				if(!AlreadyBlocked)
					BlockedWords.push_back(Match);
			}
		});
	}

	std::string FilteredMessage;
	if(g_Config.m_BcFilterChangeWholeWord == 0)
	{
		FilteredMessage = Re.replace(pMessage, true, [](const std::string &Match, int /*MatchIndex*/, int Group) -> std::string {
			if(Group != 0)
				return "";

			if(g_Config.m_BcMultipleReplacementChar)
			{
				size_t Size = 0, Count = 0;
				str_utf8_stats(Match.c_str(), Match.length() * 4, Match.length(), &Size, &Count);
				std::string Replacement;
				for(size_t i = 0; i < Count; i++)
					Replacement += g_Config.m_BcBlockedContentReplacementChar;
				return Replacement;
			}
			return g_Config.m_BcBlockedContentReplacementChar;
		});
		str_copy(s_aFilteredMessage, FilteredMessage.c_str(), sizeof(s_aFilteredMessage));
	}
	else if(g_Config.m_BcFilterChangeWholeWord == 1)
	{
		for(size_t w = 0; w < SplitMsg.size(); w++)
		{
			if(Re.error().empty() && Re.test(SplitMsg[w]))
			{
				if(g_Config.m_BcMultipleReplacementChar)
				{
					if(w > 0)
						str_append(s_aFilteredMessage, " ", sizeof(s_aFilteredMessage));
					size_t Size = 0, Count = 0;
					str_utf8_stats(SplitMsg[w].c_str(), SplitMsg[w].length() * 4, SplitMsg[w].length(), &Size, &Count);
					for(size_t i = 0; i < Count; i++)
						str_append(s_aFilteredMessage, g_Config.m_BcBlockedContentReplacementChar, sizeof(s_aFilteredMessage));
					if(w < SplitMsg.size())
						str_append(s_aFilteredMessage, " ", sizeof(s_aFilteredMessage));
				}
				else
				{
					str_append(s_aFilteredMessage, g_Config.m_BcBlockedContentReplacementChar, sizeof(s_aFilteredMessage));
				}
			}
			else
			{
				str_append(s_aFilteredMessage, SplitMsg[w].c_str(), sizeof(s_aFilteredMessage));
			}
		}
	}
	else if(g_Config.m_BcFilterChangeWholeWord == 2)
	{
		for(size_t w = 0; w < SplitMsg.size(); w++)
		{
			if(w > 0)
				str_append(s_aFilteredMessage, " ", sizeof(s_aFilteredMessage));

			bool IsExactMatch = false;
			if(Re.error().empty())
			{
				std::string LowerWord;
				LowerWord.resize(SplitMsg[w].size() * 4 + 1);
				str_utf8_tolower(SplitMsg[w].c_str(), LowerWord.data(), LowerWord.size());
				LowerWord.resize(std::strlen(LowerWord.c_str()));

				std::string MatchedWord;
				Re.match(LowerWord, false, [&MatchedWord, &LowerWord](const std::string &Match, int /*MatchIndex*/, int Group) {
					if(Group == 0 && Match == LowerWord)
						MatchedWord = Match;
				});
				IsExactMatch = !MatchedWord.empty();
			}

			if(IsExactMatch)
				str_append(s_aFilteredMessage, g_Config.m_BcBlockedContentReplacementChar, sizeof(s_aFilteredMessage));
			else
				str_append(s_aFilteredMessage, SplitMsg[w].c_str(), sizeof(s_aFilteredMessage));
		}

		FilteredMessage = Re.replace(s_aFilteredMessage, true, [](const std::string &Match, int /*MatchIndex*/, int Group) -> std::string {
			if(Group != 0)
				return "";

			if(g_Config.m_BcMultipleReplacementChar)
			{
				size_t Size = 0, Count = 0;
				str_utf8_stats(Match.c_str(), Match.length() * 4, Match.length(), &Size, &Count);
				std::string Replacement;
				for(size_t i = 0; i < Count; i++)
					Replacement += g_Config.m_BcBlockedContentPartialReplacementChar;
				return Replacement;
			}
			return g_Config.m_BcBlockedContentPartialReplacementChar;
		});
		str_copy(s_aFilteredMessage, FilteredMessage.c_str(), sizeof(s_aFilteredMessage));
	}

	if(g_Config.m_BcShowBlockedWordInConsole && IsChat && !BlockedWords.empty())
	{
		char aBlockedWordsStr[512];
		aBlockedWordsStr[0] = '\0';
		if(ClientId >= 0)
			str_format(aBlockedWordsStr, sizeof(aBlockedWordsStr), "%s said: ", GameClient()->m_aClients[ClientId].m_aName);
		else if(ClientId == SERVER_MSG)
			str_copy(aBlockedWordsStr, "Server said: ", sizeof(aBlockedWordsStr));
		for(size_t i = 0; i < BlockedWords.size(); i++)
		{
			if(i > 0)
				str_append(aBlockedWordsStr, ", ", sizeof(aBlockedWordsStr));
			str_append(aBlockedWordsStr, BlockedWords[i].c_str(), sizeof(aBlockedWordsStr));
		}
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Regex filter", aBlockedWordsStr, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcBlockedWordConsoleColor)));
	}

	return s_aFilteredMessage;
}
