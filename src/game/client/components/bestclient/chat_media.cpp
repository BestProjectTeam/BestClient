/* Copyright © 2026 BestProject Team */
#include "chat_media.h"

#include <base/color.h>
#include <base/log.h>
#include <base/math.h>
#include <base/mem.h>
#include <base/str.h>
#include <base/time.h>
#include <base/vmath.h>

#include <engine/engine.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/chat.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

static constexpr int CHAT_MEDIA_MAX_CONCURRENT_DOWNLOADS = 2;
static constexpr int CHAT_MEDIA_MAX_COMPLETED_DECODE_PER_FRAME = 1;
static constexpr int CHAT_MEDIA_MAX_TEXTURE_UPLOADS_PER_FRAME = 1;
static constexpr int64_t CHAT_MEDIA_TEXTURE_UPLOAD_BUDGET_US = 1000;
static constexpr int64_t CHAT_MEDIA_MAX_RESPONSE_SIZE = 64 * 1024 * 1024;
static constexpr int CHAT_MEDIA_MAX_GIF_FRAMES = 120;
static constexpr int CHAT_MEDIA_MAX_DIMENSION = 480;
static constexpr int CHAT_MEDIA_MAX_RESOLVE_DEPTH = 2;
static constexpr int CHAT_MEDIA_MAX_VIDEO_ANIMATION_MS = 10000;
static constexpr int CHAT_MEDIA_MAX_RETRIES = 3;
static constexpr float CHAT_MEDIA_MAX_PREVIEW_HEIGHT = 70.0f;
static constexpr float CHAT_MEDIA_MAX_PREVIEW_HEIGHT_SCOREBOARD = 56.0f;
static constexpr float CHAT_MEDIA_COMPACT_EXPANDED_HEIGHT = 150.0f;
static constexpr float CHAT_MEDIA_PREVIEW_SIZE_SCALE = 0.9f;
static constexpr int CHAT_MEDIA_MAX_URL_LENGTH = 240;
static constexpr int CHAT_MEDIA_MAX_HTML_CANDIDATES = 32;
static constexpr size_t CHAT_MEDIA_MAX_ANIMATED_MEMORY_BYTES = 24ull * 1024ull * 1024ull;
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

	const float ClampedRounding = std::min(Rounding, std::min(W, H) / 2.0f);
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

class CChatMediaDecodeJob : public IJob
{
	EChatMediaKind m_MediaKind;
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
		case EChatMediaKind::PHOTO:
			m_Success = MediaDecoder::DecodeStaticImageCpu(m_pGraphics, m_vData.data(), m_vData.size(), m_aContextName, m_DecodedFrames, CHAT_MEDIA_MAX_DIMENSION);
			if(!m_Success)
				m_Success = DecodeSingleFrameFallback();
			break;
		case EChatMediaKind::ANIMATED:
			Limits.m_DecodeAllFrames = true;
			m_Success = MediaDecoder::DecodeImageWithFfmpegCpu(m_pGraphics, m_vData.data(), m_vData.size(), m_aContextName, m_DecodedFrames, Limits);
			if(!m_Success)
			{
				Limits.m_DecodeAllFrames = false;
				m_Success = MediaDecoder::DecodeImageWithFfmpegCpu(m_pGraphics, m_vData.data(), m_vData.size(), m_aContextName, m_DecodedFrames, Limits);
			}
			if(!m_Success)
				m_Success = DecodeSingleFrameFallback();
			break;
		case EChatMediaKind::VIDEO:
			Limits.m_DecodeAllFrames = CHAT_MEDIA_ANIMATE_VIDEOS;
			m_Success = MediaDecoder::DecodeImageWithFfmpegCpu(m_pGraphics, m_vData.data(), m_vData.size(), m_aContextName, m_DecodedFrames, Limits);
			if(!m_Success)
			{
				Limits.m_DecodeAllFrames = false;
				m_Success = MediaDecoder::DecodeImageWithFfmpegCpu(m_pGraphics, m_vData.data(), m_vData.size(), m_aContextName, m_DecodedFrames, Limits);
			}
			if(!m_Success)
				m_Success = DecodeSingleFrameFallback();
			break;
		case EChatMediaKind::UNKNOWN:
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
	CChatMediaDecodeJob(IGraphics *pGraphics, EChatMediaKind MediaKind, const unsigned char *pData, size_t DataSize, const char *pContextName) :
		m_MediaKind(MediaKind),
		m_pGraphics(pGraphics)
	{
		Abortable(true);
		if(pData != nullptr && DataSize > 0)
			m_vData.assign(pData, pData + DataSize);
		str_copy(m_aContextName, pContextName ? pContextName : "", sizeof(m_aContextName));
	}

	~CChatMediaDecodeJob() override
	{
		m_DecodedFrames.Free();
	}

	bool Success() const { return m_Success; }
	SMediaDecodedFrames &DecodedFrames() { return m_DecodedFrames; }
};

SChatMediaLine::SChatMediaLine() = default;
SChatMediaLine::~SChatMediaLine() = default;
SChatMediaLine::SChatMediaLine(SChatMediaLine &&) noexcept = default;
SChatMediaLine &SChatMediaLine::operator=(SChatMediaLine &&) noexcept = default;


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

	const size_t AtPos = HostPort.rfind('@');
	if(AtPos != std::string::npos)
		HostPort = HostPort.substr(AtPos + 1);

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

static bool IsYouTubeUrl(const std::string &Url)
{
	const std::string HostLower = ExtractUrlHostLower(Url);
	if(HostLower.empty())
		return false;

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

	const size_t ScanSize = std::min(DataSize, (size_t)8192);
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
	const size_t HtmlSize = std::min(DataSize, MaxHtmlParseSize);
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

bool CChatMedia::IsDirectMediaUrl(const char *pUrl)
{
	if(!pUrl || !IsUrlStart(pUrl))
		return false;

	const std::string Ext = ExtractUrlExtensionLower(pUrl);
	return !Ext.empty() && (IsLikelyImageExtension(Ext) || IsLikelyVideoExtension(Ext));
}

EChatMediaKind CChatMedia::MediaKindFromUrl(const char *pUrl)
{
	if(!pUrl)
		return EChatMediaKind::UNKNOWN;

	const std::string Ext = ExtractUrlExtensionLower(pUrl);
	if(IsLikelyVideoExtension(Ext))
		return EChatMediaKind::VIDEO;
	if(IsLikelyAnimatedImageExtension(Ext))
		return EChatMediaKind::ANIMATED;
	if(IsLikelyImageExtension(Ext))
		return EChatMediaKind::PHOTO;
	return EChatMediaKind::UNKNOWN;
}

void CChatMedia::ExtractMediaUrlsFromText(const char *pText, std::vector<std::string> &vOutUrls)
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

void CChatMedia::ResetLine(SChatMediaLine &Media, IGraphics *pGraphics)
{
	IGraphics *pGfx = pGraphics ? pGraphics : Graphics();
	if(Media.m_pMediaRequest)
	{
		Media.m_pMediaRequest->Abort();
		Media.m_pMediaRequest = nullptr;
	}
	if(Media.m_pMediaDecodeJob)
	{
		Media.m_pMediaDecodeJob->Abort();
		Media.m_pMediaDecodeJob = nullptr;
	}

	Media.m_OptMediaDecodedFrames.reset();
	Media.m_MediaUploadIndex = 0;
	Media.m_vMediaFrameEndMs.clear();
	Media.m_MediaTotalDurationMs = 0;
	MediaDecoder::UnloadFrames(pGfx, Media.m_vMediaFrames);
	Media.m_MediaState = EChatMediaState::NONE;
	Media.m_MediaKind = EChatMediaKind::UNKNOWN;
	Media.m_aMediaUrl[0] = '\0';
	Media.m_vMediaCandidates.clear();
	Media.m_MediaCandidateIndex = -1;
	Media.m_MediaRetryCount = 0;
	Media.m_MediaAnimated = false;
	Media.m_MediaRevealed = false;
	Media.m_MediaWidth = 0;
	Media.m_MediaHeight = 0;
	Media.m_MediaResolveDepth = 0;
	Media.m_MediaAnimationStart = 0;
	Media.m_PendingLayoutRefresh = false;
	Media.m_aMediaPreviewWidth[0] = 0.0f;
	Media.m_aMediaPreviewWidth[1] = 0.0f;
	Media.m_aMediaPreviewHeight[0] = 0.0f;
	Media.m_aMediaPreviewHeight[1] = 0.0f;
	Media.m_MediaPreviewRectValid = false;
	Media.m_MediaRetryRectValid = false;
}

void CChatMedia::SetMediaCandidates(SChatMediaLine &Media, const std::vector<std::string> &vCandidates)
{
	Media.m_vMediaCandidates.clear();
	for(const std::string &Candidate : vCandidates)
	{
		if(!IsUrlStart(Candidate.c_str()))
			continue;
		if((int)Candidate.size() > CHAT_MEDIA_MAX_URL_LENGTH)
			continue;

		bool Exists = false;
		for(const std::string &Existing : Media.m_vMediaCandidates)
		{
			if(str_comp(Existing.c_str(), Candidate.c_str()) == 0)
			{
				Exists = true;
				break;
			}
		}
		if(!Exists)
		{
			Media.m_vMediaCandidates.push_back(Candidate);
			if((int)Media.m_vMediaCandidates.size() >= CHAT_MEDIA_MAX_HTML_CANDIDATES)
				break;
		}
	}

	Media.m_MediaCandidateIndex = -1;
	Media.m_MediaKind = EChatMediaKind::UNKNOWN;
	Media.m_aMediaUrl[0] = '\0';
	if(!Media.m_vMediaCandidates.empty())
	{
		Media.m_MediaCandidateIndex = 0;
		str_copy(Media.m_aMediaUrl, Media.m_vMediaCandidates.front().c_str(), sizeof(Media.m_aMediaUrl));
		Media.m_MediaKind = MediaKindFromUrl(Media.m_aMediaUrl);
	}
}

void CChatMedia::InsertMediaCandidates(SChatMediaLine &Media, const std::vector<std::string> &vCandidates, int InsertIndex)
{
	if(vCandidates.empty())
		return;

	int InsertPos = std::clamp(InsertIndex, 0, (int)Media.m_vMediaCandidates.size());
	for(const std::string &Candidate : vCandidates)
	{
		if(!IsUrlStart(Candidate.c_str()))
			continue;
		if((int)Candidate.size() > CHAT_MEDIA_MAX_URL_LENGTH)
			continue;

		bool Exists = false;
		for(const std::string &Existing : Media.m_vMediaCandidates)
		{
			if(str_comp(Existing.c_str(), Candidate.c_str()) == 0)
			{
				Exists = true;
				break;
			}
		}
		if(Exists)
			continue;

		Media.m_vMediaCandidates.insert(Media.m_vMediaCandidates.begin() + InsertPos, Candidate);
		InsertPos++;
		if((int)Media.m_vMediaCandidates.size() >= CHAT_MEDIA_MAX_HTML_CANDIDATES)
			break;
	}
}

bool CChatMedia::QueueNextMediaCandidate(SChatMediaLine &Media, const char *pReason)
{
	Media.m_OptMediaDecodedFrames.reset();
	Media.m_MediaUploadIndex = 0;
	Media.m_vMediaFrameEndMs.clear();
	Media.m_MediaTotalDurationMs = 0;
	MediaDecoder::UnloadFrames(Graphics(), Media.m_vMediaFrames);
	Media.m_MediaAnimated = false;
	Media.m_MediaWidth = 0;
	Media.m_MediaHeight = 0;

	const int NextIndex = Media.m_MediaCandidateIndex + 1;
	for(int CandidateIndex = std::max(0, NextIndex); CandidateIndex < (int)Media.m_vMediaCandidates.size(); ++CandidateIndex)
	{
		if(!IsUrlStart(Media.m_vMediaCandidates[CandidateIndex].c_str()))
			continue;
		if(!IsMediaUrlAllowed(Media.m_vMediaCandidates[CandidateIndex].c_str()))
			continue;

		Media.m_MediaCandidateIndex = CandidateIndex;
		str_copy(Media.m_aMediaUrl, Media.m_vMediaCandidates[CandidateIndex].c_str(), sizeof(Media.m_aMediaUrl));
		Media.m_MediaState = EChatMediaState::QUEUED;
		Media.m_MediaKind = MediaKindFromUrl(Media.m_aMediaUrl);
		Media.m_pMediaRequest = nullptr;
		Media.m_pMediaDecodeJob = nullptr;
		Media.m_MediaRevealed = false;
		Media.m_MediaPreviewRectValid = false;
		Media.m_MediaRetryRectValid = false;
		if(g_Config.m_Debug)
			log_debug("chat/media", "Trying fallback candidate (%d/%d): %s (%s)", CandidateIndex + 1, (int)Media.m_vMediaCandidates.size(), Media.m_aMediaUrl, pReason ? pReason : "unknown");
		return true;
	}

	if(g_Config.m_Debug)
		log_debug("chat/media", "No fallback media candidates left (%s)", pReason ? pReason : "unknown");
	return false;
}

bool CChatMedia::RetryMediaLine(SChatMediaLine &Media)
{
	if(Media.m_MediaState != EChatMediaState::FAILED)
		return false;

	if(Media.m_MediaRetryCount >= CHAT_MEDIA_MAX_RETRIES)
	{
		if(g_Config.m_Debug)
			log_debug("chat/media", "Retry limit reached for message media");
		return false;
	}

	if(Media.m_vMediaCandidates.empty())
	{
		if(g_Config.m_Debug)
			log_debug("chat/media", "Cannot retry media without candidates");
		return false;
	}

	if(Media.m_pMediaRequest)
	{
		Media.m_pMediaRequest->Abort();
		Media.m_pMediaRequest = nullptr;
	}
	if(Media.m_pMediaDecodeJob)
	{
		Media.m_pMediaDecodeJob->Abort();
		Media.m_pMediaDecodeJob = nullptr;
	}

	Media.m_OptMediaDecodedFrames.reset();
	Media.m_MediaUploadIndex = 0;
	Media.m_vMediaFrameEndMs.clear();
	Media.m_MediaTotalDurationMs = 0;
	MediaDecoder::UnloadFrames(Graphics(), Media.m_vMediaFrames);

	Media.m_MediaRetryCount++;
	Media.m_MediaCandidateIndex = 0;
	str_copy(Media.m_aMediaUrl, Media.m_vMediaCandidates.front().c_str(), sizeof(Media.m_aMediaUrl));
	Media.m_MediaState = EChatMediaState::QUEUED;
	Media.m_MediaKind = MediaKindFromUrl(Media.m_aMediaUrl);
	Media.m_MediaAnimated = false;
	Media.m_MediaRevealed = false;
	Media.m_MediaWidth = 0;
	Media.m_MediaHeight = 0;
	Media.m_MediaResolveDepth = 0;
	Media.m_MediaAnimationStart = 0;
	Media.m_MediaPreviewRectValid = false;
	Media.m_MediaRetryRectValid = false;
	GameClient()->m_Chat.RebuildChat();
	if(g_Config.m_Debug)
		log_debug("chat/media", "Retrying media preview (%d/%d): %s", Media.m_MediaRetryCount, CHAT_MEDIA_MAX_RETRIES, Media.m_aMediaUrl);
	return true;
}

void CChatMedia::QueueMediaDownload(SChatMediaLine &Media)
{
	if(!g_Config.m_BcChatMediaPreview || !AnyMediaAllowed() || Media.m_vMediaCandidates.empty())
		return;
	if(Media.m_MediaCandidateIndex < 0 || Media.m_MediaCandidateIndex >= (int)Media.m_vMediaCandidates.size())
	{
		if(!QueueNextMediaCandidate(Media, "initial candidate"))
		{
			Media.m_MediaState = EChatMediaState::NONE;
			return;
		}
	}
	if(Media.m_aMediaUrl[0] == '\0')
		return;
	if(!IsMediaUrlAllowed(Media.m_aMediaUrl))
	{
		if(!QueueNextMediaCandidate(Media, "media type disabled"))
			Media.m_MediaState = EChatMediaState::NONE;
		return;
	}
	Media.m_MediaState = EChatMediaState::QUEUED;
}

void CChatMedia::StartMediaDownload(SChatMediaLine &Media)
{
	if(Media.m_MediaState != EChatMediaState::QUEUED || Media.m_aMediaUrl[0] == '\0')
		return;
	if(!IsMediaUrlAllowed(Media.m_aMediaUrl))
	{
		if(!QueueNextMediaCandidate(Media, "media type disabled"))
			Media.m_MediaState = EChatMediaState::NONE;
		return;
	}
	if((int)str_length(Media.m_aMediaUrl) >= 255)
	{
		if(g_Config.m_Debug)
			log_debug("chat/media", "Skipping overlong URL (>255): %s", Media.m_aMediaUrl);
		if(!QueueNextMediaCandidate(Media, "overlong URL"))
			Media.m_MediaState = EChatMediaState::FAILED;
		return;
	}
	for(const char *p = Media.m_aMediaUrl; *p; ++p)
	{
		if((unsigned char)*p < 32 || *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
		{
			if(g_Config.m_Debug)
				log_debug("chat/media", "Skipping invalid URL characters: %s", Media.m_aMediaUrl);
			if(!QueueNextMediaCandidate(Media, "invalid URL characters"))
				Media.m_MediaState = EChatMediaState::FAILED;
			return;
		}
	}

	std::shared_ptr<IHttpRequest> pGet = HttpGet(Media.m_aMediaUrl);
	pGet->Timeout(CTimeout{8000, 0, 4096, 8});
	pGet->MaxResponseSize(CHAT_MEDIA_MAX_RESPONSE_SIZE);
	pGet->FailOnErrorStatus(false);
	pGet->LogProgress(HTTPLOG::NONE);
	Media.m_pMediaRequest = pGet;
	Media.m_MediaState = EChatMediaState::LOADING;
	Http()->Run(pGet);
}

bool CChatMedia::StartMediaDecode(SChatMediaLine &Media, EChatMediaKind MediaKind, const unsigned char *pData, size_t DataSize)
{
	if(!pData || DataSize == 0 || DataSize > (size_t)CHAT_MEDIA_MAX_RESPONSE_SIZE)
		return false;
	if(Media.m_pMediaDecodeJob)
	{
		Media.m_pMediaDecodeJob->Abort();
		Media.m_pMediaDecodeJob = nullptr;
	}

	Media.m_OptMediaDecodedFrames.reset();
	Media.m_MediaUploadIndex = 0;
	Media.m_vMediaFrameEndMs.clear();
	Media.m_MediaTotalDurationMs = 0;
	MediaDecoder::UnloadFrames(Graphics(), Media.m_vMediaFrames);
	Media.m_MediaAnimated = false;
	Media.m_MediaWidth = 0;
	Media.m_MediaHeight = 0;
	Media.m_MediaAnimationStart = 0;

	Media.m_pMediaDecodeJob = std::make_shared<CChatMediaDecodeJob>(Graphics(), MediaKind, pData, DataSize, Media.m_aMediaUrl);
	Engine()->AddJob(Media.m_pMediaDecodeJob);
	Media.m_MediaState = EChatMediaState::DECODING;
	return true;
}


bool CChatMedia::AnyMediaAllowed() const
{
	return g_Config.m_BcChatMediaPhotos != 0 || g_Config.m_BcChatMediaGifs != 0;
}

bool CChatMedia::IsMediaKindAllowed(EChatMediaKind Kind) const
{
	switch(Kind)
	{
	case EChatMediaKind::PHOTO:
		return g_Config.m_BcChatMediaPhotos != 0;
	case EChatMediaKind::ANIMATED:
	case EChatMediaKind::VIDEO:
		return g_Config.m_BcChatMediaGifs != 0;
	case EChatMediaKind::UNKNOWN:
	default:
		return AnyMediaAllowed();
	}
}

bool CChatMedia::IsMediaUrlAllowed(const char *pUrl) const
{
	return IsMediaKindAllowed(MediaKindFromUrl(pUrl)) && IsAllowedChatMediaUrl(pUrl);
}

bool CChatMedia::HasAllowedMediaCandidates(const SChatMediaLine &Media) const
{
	for(const std::string &Candidate : Media.m_vMediaCandidates)
	{
		if(IsMediaUrlAllowed(Candidate.c_str()))
			return true;
	}
	return Media.m_aMediaUrl[0] != '\0' && IsMediaUrlAllowed(Media.m_aMediaUrl);
}

bool CChatMedia::ShouldDisplayMediaSlot(const SChatMediaLine &Media) const
{
	if(!g_Config.m_BcChatMediaPreview || !AnyMediaAllowed())
		return false;
	if((Media.m_MediaState == EChatMediaState::READY || Media.m_MediaState == EChatMediaState::LOADING || Media.m_MediaState == EChatMediaState::DECODING || Media.m_MediaState == EChatMediaState::QUEUED) && Media.m_MediaKind != EChatMediaKind::UNKNOWN)
		return IsMediaKindAllowed(Media.m_MediaKind);
	return HasAllowedMediaCandidates(Media);
}

bool CChatMedia::ShouldHideMediaPreview(const SChatMediaLine &Media) const
{
	return m_HideMediaByBind && !Media.m_MediaRevealed && ShouldDisplayMediaSlot(Media);
}

bool CChatMedia::ShouldExpandCompactAreaForMedia(CChat &Chat, bool IsScoreBoardOpen, bool ShowLargeArea) const
{
	if(IsScoreBoardOpen || ShowLargeArea || !g_Config.m_BcChatMediaPreview || !AnyMediaAllowed())
		return false;
	for(int i = 0; i < 3; ++i)
	{
		const CChat::CLine &RecentLine = Chat.m_aLines[((Chat.m_CurrentLine - i) + CChat::MAX_LINES) % CChat::MAX_LINES];
		if(!RecentLine.m_Initialized)
			break;
		if(ShouldDisplayMediaSlot(RecentLine.m_Media))
			return true;
	}
	return false;
}

float CChatMedia::CompactMediaHeightLimit(float BottomY) const
{
	return BottomY - CHAT_MEDIA_COMPACT_EXPANDED_HEIGHT;
}

void CChatMedia::ResetHiddenMediaReveals(CChat &Chat)
{
	for(int i = 0; i < CChat::MAX_LINES; ++i)
	{
		CChat::CLine &Line = Chat.m_aLines[i];
		Line.m_Media.m_MediaRevealed = false;
		Line.m_Media.m_MediaPreviewRectValid = false;
		Line.m_Media.m_MediaRetryRectValid = false;
		Line.m_aYOffset[0] = -1.0f;
		Line.m_aYOffset[1] = -1.0f;
	}
}

void CChatMedia::OpenFullscreenMedia(CChat &Chat, int LineIndex)
{
	if(LineIndex < 0 || LineIndex >= CChat::MAX_LINES)
		return;
	const CChat::CLine &Line = Chat.m_aLines[LineIndex];
	if(!Line.m_Initialized || Line.m_Media.m_MediaState != EChatMediaState::READY)
		return;

	m_FullscreenMediaLineIndex = LineIndex;
	str_copy(m_aFullscreenMediaUrl, Line.m_Media.m_aMediaUrl, sizeof(m_aFullscreenMediaUrl));
}

void CChatMedia::CloseFullscreenMedia()
{
	m_FullscreenMediaLineIndex = -1;
	m_aFullscreenMediaUrl[0] = '\0';
}

bool CChatMedia::HasValidFullscreenMedia(CChat &Chat) const
{
	if(m_FullscreenMediaLineIndex < 0 || m_FullscreenMediaLineIndex >= CChat::MAX_LINES)
		return false;
	const CChat::CLine &Line = Chat.m_aLines[m_FullscreenMediaLineIndex];
	return Line.m_Initialized && Line.m_Media.m_MediaState == EChatMediaState::READY && str_comp(Line.m_Media.m_aMediaUrl, m_aFullscreenMediaUrl) == 0;
}

void CChatMedia::RenderFullscreen(CChat &Chat, float Width, float Height)
{
	if(!HasValidFullscreenMedia(Chat))
		return;

	CChat::CLine &Line = Chat.m_aLines[m_FullscreenMediaLineIndex];
	IGraphics::CTextureHandle MediaTexture;
	if(!GetCurrentFrameTexture(Line.m_Media, MediaTexture) || Line.m_Media.m_MediaWidth <= 0 || Line.m_Media.m_MediaHeight <= 0)
		return;

	Graphics()->DrawRect(0.0f, 0.0f, Width, Height, ColorRGBA(0.0f, 0.0f, 0.0f, 0.85f), IGraphics::CORNER_NONE, 0.0f);

	const float MaxW = Width * 0.92f;
	const float MaxH = Height * 0.92f;
	const float Scale = std::min(MaxW / (float)Line.m_Media.m_MediaWidth, MaxH / (float)Line.m_Media.m_MediaHeight);
	const float DrawW = Line.m_Media.m_MediaWidth * Scale;
	const float DrawH = Line.m_Media.m_MediaHeight * Scale;
	const float DrawX = (Width - DrawW) / 2.0f;
	const float DrawY = (Height - DrawH) / 2.0f;

	DrawRoundedMediaPreview(Graphics(), MediaTexture, DrawX, DrawY, DrawW, DrawH, 0.0f, 1.0f);
}

void CChatMedia::Update(CChat &Chat)
{
	if(!g_Config.m_BcChatMediaPreview || !AnyMediaAllowed())
		return;

	for(int LineIndex = 0; LineIndex < CChat::MAX_LINES; ++LineIndex)
	{
		CChat::CLine &Line = Chat.m_aLines[LineIndex];
		if(!Line.m_Initialized || !Line.m_Media.m_PendingLayoutRefresh)
			continue;
		Line.m_Media.m_PendingLayoutRefresh = false;
		Line.m_aYOffset[0] = -1.0f;
		Line.m_aYOffset[1] = -1.0f;
	}

	int ActiveDownloads = 0;
	for(int LineIndex = 0; LineIndex < CChat::MAX_LINES; ++LineIndex)
	{
		CChat::CLine &Line = Chat.m_aLines[LineIndex];
		if(Line.m_Media.m_MediaState == EChatMediaState::LOADING && Line.m_Media.m_pMediaRequest && !Line.m_Media.m_pMediaRequest->Done())
			ActiveDownloads++;
	}

	const auto FailLine = [this](CChat::CLine &Line, bool SuppressedBySettings) {
		if(SuppressedBySettings)
		{
			Line.m_Media.m_OptMediaDecodedFrames.reset();
			Line.m_Media.m_MediaUploadIndex = 0;
			Line.m_Media.m_vMediaFrameEndMs.clear();
			Line.m_Media.m_MediaTotalDurationMs = 0;
			MediaDecoder::UnloadFrames(Graphics(), Line.m_Media.m_vMediaFrames);
			Line.m_Media.m_MediaState = EChatMediaState::NONE;
			Line.m_Media.m_MediaAnimated = false;
			Line.m_Media.m_MediaWidth = 0;
			Line.m_Media.m_MediaHeight = 0;
			Line.m_Media.m_MediaRetryRectValid = false;
			Line.m_Media.m_MediaPreviewRectValid = false;
			Line.m_aYOffset[0] = -1.0f;
			Line.m_aYOffset[1] = -1.0f;
			return;
		}

		Line.m_Media.m_OptMediaDecodedFrames.reset();
		Line.m_Media.m_MediaUploadIndex = 0;
		Line.m_Media.m_vMediaFrameEndMs.clear();
		Line.m_Media.m_MediaTotalDurationMs = 0;
		MediaDecoder::UnloadFrames(Graphics(), Line.m_Media.m_vMediaFrames);
		Line.m_Media.m_MediaState = EChatMediaState::FAILED;
		Line.m_Media.m_MediaAnimated = false;
		Line.m_Media.m_MediaWidth = 0;
		Line.m_Media.m_MediaHeight = 0;
		Line.m_Media.m_MediaRetryRectValid = false;
		Line.m_aYOffset[0] = -1.0f;
		Line.m_aYOffset[1] = -1.0f;
		if(g_Config.m_Debug)
		{
			if(Line.m_Media.m_vMediaCandidates.empty())
				log_debug("chat/media", "Media failed: no candidates");
			else
				log_debug("chat/media", "Media failed after exhausting %d candidates", (int)Line.m_Media.m_vMediaCandidates.size());
		}
	};

	int CompletedRequestsThisFrame = 0;
	for(int LineIndex = 0; LineIndex < CChat::MAX_LINES; ++LineIndex)
	{
		CChat::CLine &Line = Chat.m_aLines[LineIndex];
		if(CompletedRequestsThisFrame >= CHAT_MEDIA_MAX_COMPLETED_DECODE_PER_FRAME)
			break;
		if(Line.m_Media.m_MediaState != EChatMediaState::LOADING || !Line.m_Media.m_pMediaRequest || !Line.m_Media.m_pMediaRequest->Done())
			continue;

		bool StartedDecode = false;
		bool SuppressedBySettings = false;
		const char *pFailureReason = "download failed";
		const bool HttpDone = Line.m_Media.m_pMediaRequest->State() == EHttpState::DONE;
		const int StatusCode = HttpDone ? Line.m_Media.m_pMediaRequest->StatusCode() : -1;
		if(HttpDone && StatusCode >= 200 && StatusCode < 400)
		{
			unsigned char *pResult = nullptr;
			size_t ResultSize = 0;
			Line.m_Media.m_pMediaRequest->Result(&pResult, &ResultSize);
			if(pResult && ResultSize > 0)
			{
				MediaDecoder::UnloadFrames(Graphics(), Line.m_Media.m_vMediaFrames);

				if(Line.m_Media.m_MediaResolveDepth < CHAT_MEDIA_MAX_RESOLVE_DEPTH)
				{
					std::vector<std::string> vExtractedUrls;
					ExtractMediaUrlsFromHtmlDocument(pResult, ResultSize, Line.m_Media.m_aMediaUrl, vExtractedUrls);
					const int CandidateCountBefore = (int)Line.m_Media.m_vMediaCandidates.size();
					InsertMediaCandidates(Line.m_Media, vExtractedUrls, Line.m_Media.m_MediaCandidateIndex + 1);
					if((int)Line.m_Media.m_vMediaCandidates.size() > CandidateCountBefore)
					{
						Line.m_Media.m_MediaResolveDepth++;
						if(g_Config.m_Debug)
							log_debug("chat/media", "Extracted %d fallback candidates from HTML: %s", (int)Line.m_Media.m_vMediaCandidates.size() - CandidateCountBefore, Line.m_Media.m_aMediaUrl);
					}
				}

				const bool IsHtmlResponse = IsLikelyHtmlDocument(pResult, ResultSize);
				if(IsHtmlResponse)
				{
					pFailureReason = "html response";
				}
				else
				{
					const std::string Ext = ExtractUrlExtensionLower(Line.m_Media.m_aMediaUrl);
					const bool IsGif = IsGifSignature(pResult, ResultSize) || Ext == "gif";
					const bool IsVideoCandidate = IsLikelyVideoExtension(Ext) || IsVideoPayloadSignature(pResult, ResultSize);
					const bool IsImageCandidate = IsLikelyImageExtension(Ext) || IsImagePayloadSignature(pResult, ResultSize);
					const bool IsAnimatedImageCandidate = IsLikelyAnimatedImageExtension(Ext) && !IsVideoCandidate;
					EChatMediaKind MediaKind = EChatMediaKind::UNKNOWN;
					if(IsGif || IsAnimatedImageCandidate)
						MediaKind = EChatMediaKind::ANIMATED;
					else if(IsVideoCandidate || (!IsImageCandidate && Ext.empty()))
						MediaKind = EChatMediaKind::VIDEO;
					else if(IsImageCandidate)
						MediaKind = EChatMediaKind::PHOTO;

					if(MediaKind == EChatMediaKind::UNKNOWN)
						MediaKind = EChatMediaKind::VIDEO;
					Line.m_Media.m_MediaKind = MediaKind;

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
							StartedDecode = StartMediaDecode(Line.m_Media, MediaKind, pResult, ResultSize);
							if(!StartedDecode)
								pFailureReason = "decode job failed";
						}
					}
				}
			}
			else if(g_Config.m_Debug)
			{
				pFailureReason = "empty response";
				log_debug("chat/media", "Empty HTTP response for media URL: %s", Line.m_Media.m_aMediaUrl);
			}
		}
		else if(g_Config.m_Debug)
		{
			pFailureReason = "http failure";
			log_debug("chat/media", "HTTP request failed for media URL (state=%d, status=%d): %s", (int)Line.m_Media.m_pMediaRequest->State(), StatusCode, Line.m_Media.m_aMediaUrl);
		}

		Line.m_Media.m_pMediaRequest = nullptr;
		ActiveDownloads = std::max(0, ActiveDownloads - 1);
		CompletedRequestsThisFrame++;

		if(StartedDecode)
			continue;

		if(QueueNextMediaCandidate(Line.m_Media, pFailureReason))
			continue;

		FailLine(Line, SuppressedBySettings);
	}

	int CompletedUploadsThisFrame = 0;
	for(int LineIndex = 0; LineIndex < CChat::MAX_LINES; ++LineIndex)
	{
		CChat::CLine &Line = Chat.m_aLines[LineIndex];
		if(CompletedUploadsThisFrame >= CHAT_MEDIA_MAX_COMPLETED_DECODE_PER_FRAME)
			break;
		if(Line.m_Media.m_MediaState != EChatMediaState::DECODING || !Line.m_Media.m_pMediaDecodeJob || !Line.m_Media.m_pMediaDecodeJob->Done())
			continue;

		bool Success = false;
		const char *pFailureReason = "decode failed";
		if(Line.m_Media.m_pMediaDecodeJob->State() == IJob::STATE_DONE && Line.m_Media.m_pMediaDecodeJob->Success() && !Line.m_Media.m_pMediaDecodeJob->DecodedFrames().Empty())
		{
			const int Width = Line.m_Media.m_pMediaDecodeJob->DecodedFrames().m_Width;
			const int Height = Line.m_Media.m_pMediaDecodeJob->DecodedFrames().m_Height;
			Line.m_Media.m_OptMediaDecodedFrames.emplace(std::move(Line.m_Media.m_pMediaDecodeJob->DecodedFrames()));
			Line.m_Media.m_MediaUploadIndex = 0;
			Line.m_Media.m_MediaWidth = Width;
			Line.m_Media.m_MediaHeight = Height;
			Line.m_Media.m_MediaAnimated = false;
			Line.m_Media.m_MediaAnimationStart = 0;
			Success = true;
		}
		else if(g_Config.m_Debug)
		{
			log_debug("chat/media", "Media decode job failed: %s", Line.m_Media.m_aMediaUrl);
		}

		Line.m_Media.m_pMediaDecodeJob = nullptr;
		CompletedUploadsThisFrame++;

		if(Success)
			continue;

		Line.m_Media.m_OptMediaDecodedFrames.reset();
		Line.m_Media.m_MediaUploadIndex = 0;
		if(QueueNextMediaCandidate(Line.m_Media, pFailureReason))
			continue;

		FailLine(Line, false);
	}

	const auto ClampFrameDurationMs = [](int DurationMs) -> int {
		constexpr int MediaFpsCap = 120;
		constexpr int MediaMinFrameMs = (1000 + MediaFpsCap - 1) / MediaFpsCap;
		constexpr int MediaMaxFrameMs = 10000;
		return std::clamp(DurationMs, MediaMinFrameMs, MediaMaxFrameMs);
	};

	auto UploadDecodedFramesStep = [&](CChat::CLine &Line, int MaxFramesToUpload, int64_t TimeBudgetUs, int &UploadedFramesOut, bool &FinishedOut) -> bool {
		UploadedFramesOut = 0;
		FinishedOut = false;
		if(!Line.m_Media.m_OptMediaDecodedFrames.has_value())
			return true;
		SMediaDecodedFrames &DecodedFrames = *Line.m_Media.m_OptMediaDecodedFrames;
		if(DecodedFrames.m_vFrames.empty())
		{
			FinishedOut = true;
			return false;
		}

		const int64_t Start = time_get();
		while(Line.m_Media.m_MediaUploadIndex < (int)DecodedFrames.m_vFrames.size())
		{
			if(UploadedFramesOut >= MaxFramesToUpload)
				break;
			if(TimeBudgetUs > 0)
			{
				const int64_t ElapsedUs = ((time_get() - Start) * 1000000) / time_freq();
				if(ElapsedUs >= TimeBudgetUs)
					break;
			}

			SMediaRawFrame &RawFrame = DecodedFrames.m_vFrames[Line.m_Media.m_MediaUploadIndex];
			SMediaFrame Frame;
			Frame.m_DurationMs = RawFrame.m_DurationMs;
			Frame.m_Texture = Graphics()->LoadTextureRawMove(RawFrame.m_Image, 0, Line.m_Media.m_aMediaUrl);
			if(!Frame.m_Texture.IsValid())
				return false;
			Line.m_Media.m_vMediaFrames.push_back(Frame);
			Line.m_Media.m_MediaUploadIndex++;
			UploadedFramesOut++;
		}

		FinishedOut = Line.m_Media.m_MediaUploadIndex >= (int)DecodedFrames.m_vFrames.size();
		return true;
	};

	int UploadedTexturesThisFrame = 0;
	const int64_t UploadStart = time_get();
	for(int LineIndex = 0; LineIndex < CChat::MAX_LINES; ++LineIndex)
	{
		CChat::CLine &Line = Chat.m_aLines[LineIndex];
		if(!Line.m_Initialized || !Line.m_Media.m_OptMediaDecodedFrames.has_value())
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
			Line.m_Media.m_OptMediaDecodedFrames.reset();
			Line.m_Media.m_MediaUploadIndex = 0;
			Line.m_Media.m_vMediaFrameEndMs.clear();
			Line.m_Media.m_MediaTotalDurationMs = 0;
			MediaDecoder::UnloadFrames(Graphics(), Line.m_Media.m_vMediaFrames);
			if(QueueNextMediaCandidate(Line.m_Media, "upload failed"))
				continue;
			FailLine(Line, false);
			continue;
		}

		if(!Line.m_Media.m_vMediaFrames.empty() && Line.m_Media.m_MediaState != EChatMediaState::READY)
		{
			Line.m_Media.m_MediaState = EChatMediaState::READY;
			Line.m_Media.m_MediaRetryRectValid = false;
			Line.m_Media.m_MediaPreviewRectValid = false;
			Line.m_Time = time();
			Line.m_Media.m_PendingLayoutRefresh = true;
		}

		if(Finished)
		{
			Line.m_Media.m_OptMediaDecodedFrames.reset();
			Line.m_Media.m_MediaUploadIndex = 0;
			Line.m_Media.m_vMediaFrameEndMs.clear();
			Line.m_Media.m_MediaTotalDurationMs = 0;
			if(Line.m_Media.m_vMediaFrames.size() > 1)
			{
				Line.m_Media.m_vMediaFrameEndMs.reserve(Line.m_Media.m_vMediaFrames.size());
				int TotalDuration = 0;
				for(const auto &Frame : Line.m_Media.m_vMediaFrames)
				{
					TotalDuration += ClampFrameDurationMs(Frame.m_DurationMs);
					Line.m_Media.m_vMediaFrameEndMs.push_back(TotalDuration);
				}
				Line.m_Media.m_MediaTotalDurationMs = TotalDuration;
				Line.m_Media.m_MediaAnimated = TotalDuration > 0;
				if(Line.m_Media.m_MediaAnimated)
					Line.m_Media.m_MediaAnimationStart = time_get();
			}
			else
			{
				Line.m_Media.m_MediaAnimated = false;
				Line.m_Media.m_MediaAnimationStart = 0;
			}
		}
	}

	for(int LineIndex = 0; LineIndex < CChat::MAX_LINES; ++LineIndex)
	{
		CChat::CLine &Line = Chat.m_aLines[LineIndex];
		if(ActiveDownloads >= CHAT_MEDIA_MAX_CONCURRENT_DOWNLOADS)
			break;
		if(Line.m_Media.m_MediaState == EChatMediaState::QUEUED)
		{
			StartMediaDownload(Line.m_Media);
			if(Line.m_Media.m_MediaState == EChatMediaState::LOADING)
				ActiveDownloads++;
		}
	}
}

bool CChatMedia::GetCurrentFrameTexture(SChatMediaLine &Media, IGraphics::CTextureHandle &Texture) const
{
	if(Media.m_vMediaFrames.empty())
		return false;
	if(!Media.m_MediaAnimated || Media.m_vMediaFrames.size() == 1)
	{
		Texture = Media.m_vMediaFrames.front().m_Texture;
		return Texture.IsValid();
	}

	if(Media.m_MediaTotalDurationMs <= 0 || (int)Media.m_vMediaFrameEndMs.size() != (int)Media.m_vMediaFrames.size())
		return MediaDecoder::GetCurrentFrameTexture(Media.m_vMediaFrames, Media.m_MediaAnimated, Media.m_MediaAnimationStart, Texture);

	const int64_t ElapsedMs = ((time_get() - Media.m_MediaAnimationStart) * 1000) / time_freq();
	const int Offset = (int)(ElapsedMs % (int64_t)Media.m_MediaTotalDurationMs);
	const auto It = std::upper_bound(Media.m_vMediaFrameEndMs.begin(), Media.m_vMediaFrameEndMs.end(), Offset);
	const int Index = It == Media.m_vMediaFrameEndMs.end() ? 0 : (int)(It - Media.m_vMediaFrameEndMs.begin());
	Texture = Media.m_vMediaFrames[Index].m_Texture;
	return Texture.IsValid();
}

std::string CChatMedia::MediaPlaceholderText(const SChatMediaLine &Media) const
{
	const char *pUrl = Media.m_aMediaUrl;
	if(pUrl[0] == '\0' && !Media.m_vMediaCandidates.empty())
		pUrl = Media.m_vMediaCandidates.front().c_str();

	const std::string Ext = ExtractUrlExtensionLower(pUrl);
	if(IsLikelyVideoExtension(Ext))
		return "Video";
	if(Media.m_MediaAnimated || IsLikelyAnimatedImageExtension(Ext))
		return "GIF";
	if(IsLikelyImageExtension(Ext))
		return "Photo";
	return "Media";
}

std::string CChatMedia::BuildVisibleMessageText(const SChatMediaLine &Media, const char *pText, bool UseMediaLabelWhenEmpty) const
{
	if(!ShouldDisplayMediaSlot(Media))
		return pText ? pText : "";

	std::string Result;
	bool RemovedUrl = false;
	for(const char *pCur = pText ? pText : ""; *pCur;)
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
		return MediaPlaceholderText(Media);

	return Compacted;
}


void ComputeChatMediaPreviewSize(int MediaWidth, int MediaHeight, float MaxPreviewWidth, float MaxPreviewHeight, float &PreviewW, float &PreviewH)
{
	if(MediaWidth <= 0 || MediaHeight <= 0 || MaxPreviewWidth <= 0.0f || MaxPreviewHeight <= 0.0f)
	{
		PreviewW = 0.0f;
		PreviewH = 0.0f;
		return;
	}
	const float ScaleByWidth = MaxPreviewWidth / (float)MediaWidth;
	const float ScaleByHeight = MaxPreviewHeight / (float)MediaHeight;
	float Scale = std::min(1.0f, std::min(ScaleByWidth, ScaleByHeight));
	PreviewW = std::max(1.0f, (float)MediaWidth * Scale);
	PreviewH = std::max(1.0f, (float)MediaHeight * Scale);
	if(PreviewW < CHAT_MEDIA_MIN_PREVIEW_SIDE || PreviewH < CHAT_MEDIA_MIN_PREVIEW_SIDE)
	{
		const float UpscaleByW = CHAT_MEDIA_MIN_PREVIEW_SIDE / PreviewW;
		const float UpscaleByH = CHAT_MEDIA_MIN_PREVIEW_SIDE / PreviewH;
		const float Upscale = std::max(UpscaleByW, UpscaleByH);
		const float MaxUpscale = std::min(MaxPreviewWidth / PreviewW, MaxPreviewHeight / PreviewH);
		if(MaxUpscale > 1.0f)
		{
			const float UseUpscale = std::min(Upscale, MaxUpscale);
			PreviewW *= UseUpscale;
			PreviewH *= UseUpscale;
		}
	}
	PreviewW = std::max(1.0f, PreviewW);
	PreviewH = std::max(1.0f, PreviewH);
}

void CChatMedia::OnConsoleInit()
{
	if(str_comp(g_Config.m_BcChatMediaAllowedDomains, "tenor.com; imgur.com; giphy.com") == 0)
		str_copy(g_Config.m_BcChatMediaAllowedDomains, "tenor.com; imgur.com; giphy.com; gifs.teeworlds.xyz", sizeof(g_Config.m_BcChatMediaAllowedDomains));
	Console()->Register("toggle_chat_media_hidden", "", CFGFLAG_CLIENT, ConToggleHideChatMedia, this, "Toggle hidden media mode in chat");
}

void CChatMedia::ConToggleHideChatMedia(IConsole::IResult *pResult, void *pUserData)
{
	CChatMedia *pThis = static_cast<CChatMedia *>(pUserData);
	(void)pResult;
	pThis->m_HideMediaByBind = !pThis->m_HideMediaByBind;
	CChat &Chat = pThis->GameClient()->m_Chat;
	pThis->ResetHiddenMediaReveals(Chat);
	Chat.RebuildChat();
	Chat.Echo(pThis->m_HideMediaByBind ? "Chat media hidden" : "Chat media visible");
}

void CChatMedia::OnChatLineAdded(CChat &Chat, SChatMediaLine &Media, const char *pText)
{
	(void)Chat;
	if(!g_Config.m_BcChatMediaPreview || !AnyMediaAllowed() || !pText)
		return;
	std::vector<std::string> vMediaUrls;
	ExtractMediaUrlsFromText(pText, vMediaUrls);
	SetMediaCandidates(Media, vMediaUrls);
	if(!Media.m_vMediaCandidates.empty())
		QueueMediaDownload(Media);
	else if(g_Config.m_Debug && str_find(pText, "http"))
		log_debug("chat/media", "No usable media candidates in message: %s", pText);
}

vec2 CChatMedia::ChatMousePos() const
{
	const float Height = 300.0f;
	const float Width = Height * Graphics()->ScreenAspect();
	const vec2 WindowSize(std::max(1.0f, (float)Graphics()->WindowWidth()), std::max(1.0f, (float)Graphics()->WindowHeight()));
	const vec2 UiMousePos = Ui()->UpdatedMousePos() * vec2(Ui()->Screen()->w, Ui()->Screen()->h) / WindowSize;
	const vec2 UiToChatScale(Width / Ui()->Screen()->w, Height / Ui()->Screen()->h);
	return UiMousePos * UiToChatScale;
}

bool CChatMedia::OnInput(CChat &Chat, const IInput::CEvent &Event)
{
	if((Event.m_Flags & IInput::FLAG_PRESS) && HasValidFullscreenMedia(Chat))
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
		for(int LineIndex = 0; LineIndex < CChat::MAX_LINES; ++LineIndex)
		{
			CChat::CLine &Line = Chat.m_aLines[LineIndex];
			SChatMediaLine &Media = Line.m_Media;
			if(!Line.m_Initialized || !Media.m_MediaPreviewRectValid || Media.m_MediaState != EChatMediaState::READY)
				continue;
			if(ShouldHideMediaPreview(Media))
				continue;
			const SChatMediaRenderRect &Rect = Media.m_MediaPreviewRect;
			if(MousePos.x >= Rect.m_X && MousePos.x <= Rect.m_X + Rect.m_W &&
				MousePos.y >= Rect.m_Y && MousePos.y <= Rect.m_Y + Rect.m_H)
			{
				OpenFullscreenMedia(Chat, LineIndex);
				return true;
			}
		}
	}

	if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_MOUSE_1 && g_Config.m_BcChatMediaPreview &&
		(Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK))
	{
		bool HasRetryTargets = false;
		for(int i = 0; i < CChat::MAX_LINES; ++i)
		{
			const SChatMediaLine &Media = Chat.m_aLines[i].m_Media;
			if(Media.m_MediaRetryRectValid && Media.m_MediaState == EChatMediaState::FAILED)
			{
				HasRetryTargets = true;
				break;
			}
		}
		if(HasRetryTargets)
		{
			const vec2 MousePos = ChatMousePos();
			for(int i = 0; i < CChat::MAX_LINES; ++i)
			{
				SChatMediaLine &Media = Chat.m_aLines[i].m_Media;
				if(!Media.m_MediaRetryRectValid || Media.m_MediaState != EChatMediaState::FAILED)
					continue;
				const SChatMediaRenderRect &Rect = Media.m_MediaRetryRect;
				if(MousePos.x >= Rect.m_X && MousePos.x <= Rect.m_X + Rect.m_W &&
					MousePos.y >= Rect.m_Y && MousePos.y <= Rect.m_Y + Rect.m_H)
				{
					if(RetryMediaLine(Media))
						return true;
				}
			}
		}
	}

	if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_MOUSE_1 && m_HideMediaByBind)
	{
		const vec2 MousePos = ChatMousePos();
		for(int LineIndex = 0; LineIndex < CChat::MAX_LINES; ++LineIndex)
		{
			CChat::CLine &Line = Chat.m_aLines[LineIndex];
			SChatMediaLine &Media = Line.m_Media;
			if(!Line.m_Initialized || !Media.m_MediaPreviewRectValid || !ShouldHideMediaPreview(Media))
				continue;
			const SChatMediaRenderRect &Rect = Media.m_MediaPreviewRect;
			if(MousePos.x >= Rect.m_X && MousePos.x <= Rect.m_X + Rect.m_W &&
				MousePos.y >= Rect.m_Y && MousePos.y <= Rect.m_Y + Rect.m_H)
			{
				Media.m_MediaRevealed = true;
				Line.m_aYOffset[0] = -1.0f;
				Line.m_aYOffset[1] = -1.0f;
				Chat.RebuildChat();
				return true;
			}
		}
	}
	return false;
}

void CChatMedia::PrepareLineLayout(SChatMediaLine &Media, int OffsetType, float LineWidth, float MaxPreviewHeight, float FontSize, float TextHeight, float &TotalHeight)
{
	Media.m_aTextHeight[OffsetType] = TextHeight;
	Media.m_aMediaPreviewWidth[OffsetType] = 0.0f;
	Media.m_aMediaPreviewHeight[OffsetType] = 0.0f;
	const bool ShowMediaSlot = ShouldDisplayMediaSlot(Media);
	const bool HideMediaPreview = ShouldHideMediaPreview(Media);
	if(ShowMediaSlot && (HideMediaPreview || (Media.m_MediaState == EChatMediaState::READY && Media.m_MediaWidth > 0 && Media.m_MediaHeight > 0 && !Media.m_vMediaFrames.empty())))
	{
		const float MaxPreviewWidth = std::min(LineWidth, (float)g_Config.m_BcChatMediaPreviewMaxWidth) * CHAT_MEDIA_PREVIEW_SIZE_SCALE;
		if(MaxPreviewWidth > 0.0f && MaxPreviewHeight > 0.0f)
		{
			if(Media.m_MediaState == EChatMediaState::READY && Media.m_MediaWidth > 0 && Media.m_MediaHeight > 0 && !Media.m_vMediaFrames.empty())
			{
				float PreviewW = 0.0f;
				float PreviewH = 0.0f;
				ComputeChatMediaPreviewSize(Media.m_MediaWidth, Media.m_MediaHeight, MaxPreviewWidth, MaxPreviewHeight, PreviewW, PreviewH);
				Media.m_aMediaPreviewWidth[OffsetType] = PreviewW;
				Media.m_aMediaPreviewHeight[OffsetType] = PreviewH;
			}
			else
			{
				Media.m_aMediaPreviewWidth[OffsetType] = MaxPreviewWidth;
				Media.m_aMediaPreviewHeight[OffsetType] = std::max(FontSize * 1.6f, 18.0f) * CHAT_MEDIA_PREVIEW_SIZE_SCALE;
			}
			TotalHeight += FontSize * 0.4f + Media.m_aMediaPreviewHeight[OffsetType];
		}
	}
	else if(ShowMediaSlot && (Media.m_MediaState == EChatMediaState::QUEUED || Media.m_MediaState == EChatMediaState::LOADING || Media.m_MediaState == EChatMediaState::DECODING))
	{
		Media.m_aMediaPreviewWidth[OffsetType] = std::min(LineWidth, (float)g_Config.m_BcChatMediaPreviewMaxWidth) * CHAT_MEDIA_PREVIEW_SIZE_SCALE;
		Media.m_aMediaPreviewHeight[OffsetType] = std::max(FontSize * 1.2f, 12.0f) * CHAT_MEDIA_PREVIEW_SIZE_SCALE;
		TotalHeight += FontSize * 0.4f + Media.m_aMediaPreviewHeight[OffsetType];
	}
	else if(ShowMediaSlot && Media.m_MediaState == EChatMediaState::FAILED)
	{
		Media.m_aMediaPreviewWidth[OffsetType] = std::min(LineWidth, (float)g_Config.m_BcChatMediaPreviewMaxWidth) * CHAT_MEDIA_PREVIEW_SIZE_SCALE;
		Media.m_aMediaPreviewHeight[OffsetType] = std::max(FontSize * 2.1f, 18.0f) * CHAT_MEDIA_PREVIEW_SIZE_SCALE;
		TotalHeight += FontSize * 0.4f + Media.m_aMediaPreviewHeight[OffsetType];
	}
}

void CChatMedia::RenderLinePreview(SChatMediaLine &Media, float PreviewX, float PreviewY, float Blend, float FontSize, int OffsetType)
{
	const bool ShowMediaSlot = ShouldDisplayMediaSlot(Media);
	const bool HideMediaPreview = ShouldHideMediaPreview(Media);
	const bool HasMediaPreview = Media.m_aMediaPreviewWidth[OffsetType] > 0.0f && Media.m_aMediaPreviewHeight[OffsetType] > 0.0f;
	const float PreviewW = Media.m_aMediaPreviewWidth[OffsetType];
	const float PreviewH = Media.m_aMediaPreviewHeight[OffsetType];
	Media.m_MediaPreviewRectValid = false;
	if(!ShowMediaSlot || !HasMediaPreview)
		return;

	auto DrawMediaPreviewFrame = [&](ColorRGBA FillColor, float &InnerPreviewX, float &InnerPreviewY, float &InnerPreviewW, float &InnerPreviewH, float &InnerPreviewRounding) {
		const float PreviewBorder = std::max(0.35f, FontSize * 0.025f);
		const float PreviewRounding = std::min(std::min(PreviewW, PreviewH) / 2.0f, std::max(4.0f, FontSize * 0.55f));
		Graphics()->DrawRect(PreviewX, PreviewY, PreviewW, PreviewH, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f * Blend), IGraphics::CORNER_ALL, PreviewRounding);
		InnerPreviewX = PreviewX + PreviewBorder;
		InnerPreviewY = PreviewY + PreviewBorder;
		InnerPreviewW = std::max(1.0f, PreviewW - PreviewBorder * 2.0f);
		InnerPreviewH = std::max(1.0f, PreviewH - PreviewBorder * 2.0f);
		InnerPreviewRounding = std::max(0.0f, PreviewRounding - PreviewBorder);
		Graphics()->DrawRect(InnerPreviewX, InnerPreviewY, InnerPreviewW, InnerPreviewH, FillColor, IGraphics::CORNER_ALL, InnerPreviewRounding);
	};

	float InnerPreviewX = PreviewX;
	float InnerPreviewY = PreviewY;
	float InnerPreviewW = PreviewW;
	float InnerPreviewH = PreviewH;
	float InnerPreviewRounding = 0.0f;

	if(HideMediaPreview)
	{
		const char *pHiddenLabel = "hidden media";
		DrawMediaPreviewFrame(ColorRGBA(0.10f, 0.10f, 0.10f, 0.82f * Blend), InnerPreviewX, InnerPreviewY, InnerPreviewW, InnerPreviewH, InnerPreviewRounding);
		CTextCursor HiddenCursor;
		const float HiddenFontSize = FontSize * 0.72f;
		const float HiddenLabelWidth = TextRender()->TextWidth(HiddenFontSize, pHiddenLabel);
		HiddenCursor.SetPosition(vec2(InnerPreviewX + std::max(FontSize * 0.35f, (InnerPreviewW - HiddenLabelWidth) / 2.0f), InnerPreviewY + std::max(FontSize * 0.25f, (InnerPreviewH - HiddenFontSize) / 2.0f)));
		HiddenCursor.m_FontSize = HiddenFontSize;
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.9f * Blend);
		TextRender()->TextEx(&HiddenCursor, pHiddenLabel);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		Media.m_MediaRetryRectValid = false;
		Media.m_MediaPreviewRect = {PreviewX, PreviewY, PreviewW, PreviewH};
		Media.m_MediaPreviewRectValid = true;
	}
	else if(Media.m_MediaState == EChatMediaState::READY)
	{
		IGraphics::CTextureHandle MediaTexture;
		if(GetCurrentFrameTexture(Media, MediaTexture))
		{
			DrawMediaPreviewFrame(ColorRGBA(0.05f, 0.05f, 0.05f, 0.18f * Blend), InnerPreviewX, InnerPreviewY, InnerPreviewW, InnerPreviewH, InnerPreviewRounding);
			DrawRoundedMediaPreview(Graphics(), MediaTexture, InnerPreviewX, InnerPreviewY, InnerPreviewW, InnerPreviewH, InnerPreviewRounding, Blend);
			Media.m_MediaRetryRectValid = false;
			Media.m_MediaPreviewRect = {PreviewX, PreviewY, PreviewW, PreviewH};
			Media.m_MediaPreviewRectValid = true;
		}
	}
	else if(Media.m_MediaState == EChatMediaState::QUEUED || Media.m_MediaState == EChatMediaState::LOADING || Media.m_MediaState == EChatMediaState::DECODING)
	{
		DrawMediaPreviewFrame(ColorRGBA(0.12f, 0.12f, 0.12f, 0.75f * Blend), InnerPreviewX, InnerPreviewY, InnerPreviewW, InnerPreviewH, InnerPreviewRounding);
		CTextCursor LoadingCursor;
		LoadingCursor.SetPosition(vec2(InnerPreviewX + FontSize * 0.35f, InnerPreviewY + InnerPreviewH * 0.15f));
		LoadingCursor.m_FontSize = FontSize * 0.75f;
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.8f * Blend);
		TextRender()->TextEx(&LoadingCursor, "Loading media...");
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		Media.m_MediaRetryRectValid = false;
	}
	else if(Media.m_MediaState == EChatMediaState::FAILED)
	{
		const bool CanRetry = Media.m_MediaRetryCount < CHAT_MEDIA_MAX_RETRIES && !Media.m_vMediaCandidates.empty();
		DrawMediaPreviewFrame(ColorRGBA(0.23f, 0.10f, 0.10f, 0.82f * Blend), InnerPreviewX, InnerPreviewY, InnerPreviewW, InnerPreviewH, InnerPreviewRounding);
		CTextCursor FailedCursor;
		FailedCursor.SetPosition(vec2(InnerPreviewX + FontSize * 0.35f, InnerPreviewY + FontSize * 0.25f));
		FailedCursor.m_FontSize = FontSize * 0.70f;
		TextRender()->TextColor(1.0f, 0.85f, 0.85f, 0.95f * Blend);
		TextRender()->TextEx(&FailedCursor, CanRetry ? "Media preview unavailable" : "Media preview unavailable (retry limit reached)");
		const char *pRetryLabel = CanRetry ? "Retry" : "Retry limit reached";
		const float RetryFont = FontSize * 0.66f;
		const float RetryLabelWidth = TextRender()->TextWidth(RetryFont, pRetryLabel);
		const float RetryW = std::max(FontSize * 4.2f, RetryLabelWidth + FontSize * 0.8f);
		const float RetryH = std::max(FontSize * 0.95f, 12.0f);
		const float RetryX = InnerPreviewX + InnerPreviewW - RetryW - FontSize * 0.25f;
		const float RetryY = InnerPreviewY + InnerPreviewH - RetryH - FontSize * 0.25f;
		Graphics()->DrawRect(RetryX, RetryY, RetryW, RetryH, CanRetry ? ColorRGBA(0.86f, 0.28f, 0.28f, 0.95f * Blend) : ColorRGBA(0.35f, 0.35f, 0.35f, 0.75f * Blend), IGraphics::CORNER_ALL, std::max(2.0f, RetryH * 0.3f));
		CTextCursor RetryCursor;
		RetryCursor.SetPosition(vec2(RetryX + (RetryW - RetryLabelWidth) / 2.0f, RetryY + (RetryH - RetryFont) / 2.0f));
		RetryCursor.m_FontSize = RetryFont;
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.95f * Blend);
		TextRender()->TextEx(&RetryCursor, pRetryLabel);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		Media.m_MediaRetryRectValid = CanRetry;
		if(CanRetry)
			Media.m_MediaRetryRect = {RetryX, RetryY, RetryW, RetryH};
	}
}

SChatMediaLine *CChatMedia::FindReadyMediaForMessage(CChat &Chat, int ClientId, const char *pText)
{
	if(!pText || !g_Config.m_BcChatMediaPreview || !AnyMediaAllowed())
		return nullptr;
	for(int i = 0; i < CChat::MAX_LINES; ++i)
	{
		const int Idx = ((Chat.m_CurrentLine - i) + CChat::MAX_LINES) % CChat::MAX_LINES;
		CChat::CLine &Line = Chat.m_aLines[Idx];
		if(!Line.m_Initialized)
			break;
		if(Line.m_ClientId != ClientId)
			continue;
		if(str_comp(Line.m_aText, pText) != 0)
			continue;
		if(Line.m_Media.m_MediaState == EChatMediaState::READY && !Line.m_Media.m_vMediaFrames.empty())
			return &Line.m_Media;
	}
	return nullptr;
}
