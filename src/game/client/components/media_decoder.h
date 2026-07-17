#ifndef GAME_CLIENT_COMPONENTS_MEDIA_DECODER_H
#define GAME_CLIENT_COMPONENTS_MEDIA_DECODER_H

#include <engine/graphics.h>
#include <engine/storage.h>

#include <string>
#include <vector>

struct SMediaFrame
{
	IGraphics::CTextureHandle m_Texture;
	int m_DurationMs = 0;
};

struct SMediaRawFrame
{
	CImageInfo m_Image;
	int m_DurationMs = 0;

	SMediaRawFrame() = default;
	SMediaRawFrame(SMediaRawFrame &&Other) = default;
	SMediaRawFrame &operator=(SMediaRawFrame &&Other) = default;
	SMediaRawFrame(const SMediaRawFrame &Other) = delete;
	SMediaRawFrame &operator=(const SMediaRawFrame &Other) = delete;
	~SMediaRawFrame() { m_Image.Free(); }
};

struct SMediaDecodedFrames
{
	std::vector<SMediaRawFrame> m_vFrames;
	bool m_Animated = false;
	int m_Width = 0;
	int m_Height = 0;
	int64_t m_AnimationStart = 0;

	void Free();
	size_t MemoryUsage() const;
	bool Empty() const { return m_vFrames.empty(); }
};

struct SMediaDecodeLimits
{
	int m_MaxDimension = 0;
	int m_MaxFrames = 120;
	size_t m_MaxTotalBytes = 64ull * 1024ull * 1024ull;
	int m_MaxAnimationDurationMs = 0;
	bool m_DecodeAllFrames = false;
};

namespace MediaDecoder
{
std::string ExtractExtensionLower(const char *pPath);
bool IsLikelyAnimatedImageExtension(const std::string &Ext);
bool IsLikelyVideoExtension(const std::string &Ext);

void UnloadFrames(IGraphics *pGraphics, std::vector<SMediaFrame> &vFrames);
bool UploadFrames(IGraphics *pGraphics, SMediaDecodedFrames &DecodedFrames, std::vector<SMediaFrame> &vFrames, const char *pContextName);
bool GetCurrentFrameTexture(const std::vector<SMediaFrame> &vFrames, bool Animated, int64_t AnimationStart, IGraphics::CTextureHandle &Texture);
bool DecodeImageToRgba(IGraphics *pGraphics, const unsigned char *pData, size_t DataSize, const char *pContextName, CImageInfo &Image);
bool DecodeStaticImageCpu(IGraphics *pGraphics, const unsigned char *pData, size_t DataSize, const char *pContextName, SMediaDecodedFrames &DecodedFrames, int MaxDimension);
bool DecodeAnimatedImageCpu(IGraphics *pGraphics, const unsigned char *pData, size_t DataSize, const char *pContextName, SMediaDecodedFrames &DecodedFrames, const SMediaDecodeLimits &Limits);
bool DecodeImageWithFfmpegCpu(IGraphics *pGraphics, const unsigned char *pData, size_t DataSize, const char *pContextName, SMediaDecodedFrames &DecodedFrames, const SMediaDecodeLimits &Limits);

bool DecodeStaticImage(IGraphics *pGraphics, const unsigned char *pData, size_t DataSize, const char *pContextName, std::vector<SMediaFrame> &vFrames, bool &Animated, int &Width, int &Height, int64_t &AnimationStart);
bool DecodeAnimatedImage(IGraphics *pGraphics, const unsigned char *pData, size_t DataSize, const char *pContextName, std::vector<SMediaFrame> &vFrames, bool &Animated, int &Width, int &Height, int64_t &AnimationStart, int MaxAnimationDurationMs);
bool DecodeImageWithFfmpeg(IGraphics *pGraphics, const unsigned char *pData, size_t DataSize, const char *pContextName, std::vector<SMediaFrame> &vFrames, bool &Animated, int &Width, int &Height, int64_t &AnimationStart, bool DecodeAllFrames, int MaxAnimationDurationMs);
}

#endif
