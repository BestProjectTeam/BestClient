#include "media_decoder.h"

#include <base/system.h>

#include <engine/gfx/image_manipulation.h>

#include <algorithm>
#include <cctype>
#include <limits>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

namespace
{
constexpr int MEDIA_FPS_CAP = 120;
constexpr int MEDIA_MIN_FRAME_MS = (1000 + MEDIA_FPS_CAP - 1) / MEDIA_FPS_CAP; // ceil(1000 / fps)
constexpr int MEDIA_DEFAULT_FRAME_MS = 50;
constexpr int MEDIA_MAX_FRAME_MS = 10000;
constexpr int MEDIA_MAX_FRAMES = 120;
constexpr int MEDIA_MAX_DIMENSION = 4096;
constexpr size_t MEDIA_MAX_ANIMATED_MEMORY_BYTES = 64ull * 1024ull * 1024ull;

struct CFfmpegMemoryReader
{
	const unsigned char *m_pData = nullptr;
	size_t m_Size = 0;
	size_t m_Pos = 0;
};

int FfmpegReadPacket(void *pOpaque, uint8_t *pBuffer, int BufferSize)
{
	auto *pReader = static_cast<CFfmpegMemoryReader *>(pOpaque);
	if(!pReader || !pBuffer || BufferSize <= 0)
		return AVERROR(EINVAL);
	if(pReader->m_Pos >= pReader->m_Size)
		return AVERROR_EOF;
	const size_t Remaining = pReader->m_Size - pReader->m_Pos;
	const size_t CopySize = std::min((size_t)BufferSize, Remaining);
	mem_copy(pBuffer, pReader->m_pData + pReader->m_Pos, CopySize);
	pReader->m_Pos += CopySize;
	return (int)CopySize;
}

int64_t FfmpegSeek(void *pOpaque, int64_t Offset, int Whence)
{
	auto *pReader = static_cast<CFfmpegMemoryReader *>(pOpaque);
	if(!pReader)
		return AVERROR(EINVAL);
	if(Whence == AVSEEK_SIZE)
		return (int64_t)pReader->m_Size;

	size_t NewPos = pReader->m_Pos;
	const int BaseWhence = Whence & 0x3;
	if(BaseWhence == SEEK_SET)
		NewPos = (size_t)maximum<int64_t>(0, Offset);
	else if(BaseWhence == SEEK_CUR)
		NewPos = (size_t)maximum<int64_t>(0, (int64_t)NewPos + Offset);
	else if(BaseWhence == SEEK_END)
		NewPos = (size_t)maximum<int64_t>(0, (int64_t)pReader->m_Size + Offset);
	else
		return AVERROR(EINVAL);

	NewPos = std::min(NewPos, pReader->m_Size);
	pReader->m_Pos = NewPos;
	return (int64_t)NewPos;
}

int ClampFrameDurationMs(int DurationMs)
{
	return std::clamp(DurationMs, MEDIA_MIN_FRAME_MS, MEDIA_MAX_FRAME_MS);
}

void ClampImageSize(int Width, int Height, int MaxDimension, int &ScaledWidth, int &ScaledHeight)
{
	ScaledWidth = Width;
	ScaledHeight = Height;
	if(MaxDimension <= 0 || Width <= 0 || Height <= 0 || (Width <= MaxDimension && Height <= MaxDimension))
		return;

	const double Scale = minimum((double)MaxDimension / (double)Width, (double)MaxDimension / (double)Height);
	ScaledWidth = maximum(1, round_to_int((double)Width * Scale));
	ScaledHeight = maximum(1, round_to_int((double)Height * Scale));
}

void ClampImageSize(CImageInfo &Image, int MaxDimension)
{
	int ScaledWidth = 0;
	int ScaledHeight = 0;
	ClampImageSize((int)Image.m_Width, (int)Image.m_Height, MaxDimension, ScaledWidth, ScaledHeight);
	if(ScaledWidth > 0 && ScaledHeight > 0 && ((size_t)ScaledWidth != Image.m_Width || (size_t)ScaledHeight != Image.m_Height))
		ResizeImage(Image, ScaledWidth, ScaledHeight);
}
}

void SMediaDecodedFrames::Free()
{
	m_vFrames.clear();
	m_Animated = false;
	m_Width = 0;
	m_Height = 0;
	m_AnimationStart = 0;
}

size_t SMediaDecodedFrames::MemoryUsage() const
{
	size_t Total = 0;
	for(const auto &Frame : m_vFrames)
		Total += Frame.m_Image.DataSize();
	return Total;
}

namespace MediaDecoder
{
std::string ExtractExtensionLower(const char *pPath)
{
	if(pPath == nullptr)
		return {};

	const char *pDot = str_rchr(pPath, '.');
	if(pDot == nullptr || pDot[1] == '\0')
		return {};

	std::string Ext = pDot + 1;
	std::transform(Ext.begin(), Ext.end(), Ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return Ext;
}

bool IsLikelyAnimatedImageExtension(const std::string &Ext)
{
	return Ext == "gif" || Ext == "webp" || Ext == "apng" || Ext == "avif";
}

bool IsLikelyVideoExtension(const std::string &Ext)
{
	return Ext == "mp4" || Ext == "webm" || Ext == "mov" || Ext == "m4v" || Ext == "mkv" || Ext == "avi" ||
			Ext == "gifv" || Ext == "mpg" || Ext == "mpeg" || Ext == "ogv" || Ext == "3gp" || Ext == "3g2";
}

void UnloadFrames(IGraphics *pGraphics, std::vector<SMediaFrame> &vFrames)
{
	if(pGraphics == nullptr)
	{
		vFrames.clear();
		return;
	}

	for(auto &Frame : vFrames)
		pGraphics->UnloadTexture(&Frame.m_Texture);
	vFrames.clear();
}

bool UploadFrames(IGraphics *pGraphics, SMediaDecodedFrames &DecodedFrames, std::vector<SMediaFrame> &vFrames, const char *pContextName)
{
	UnloadFrames(pGraphics, vFrames);
	if(pGraphics == nullptr)
	{
		DecodedFrames.Free();
		return false;
	}

	for(auto &RawFrame : DecodedFrames.m_vFrames)
	{
		SMediaFrame Frame;
		Frame.m_DurationMs = RawFrame.m_DurationMs;
		Frame.m_Texture = pGraphics->LoadTextureRawMove(RawFrame.m_Image, 0, pContextName);
		if(!Frame.m_Texture.IsValid())
		{
			UnloadFrames(pGraphics, vFrames);
			DecodedFrames.Free();
			return false;
		}
		vFrames.push_back(Frame);
	}

	const bool Success = !vFrames.empty();
	DecodedFrames.Free();
	return Success;
}

bool GetCurrentFrameTexture(const std::vector<SMediaFrame> &vFrames, bool Animated, int64_t AnimationStart, IGraphics::CTextureHandle &Texture)
{
	if(vFrames.empty())
		return false;
	if(!Animated || vFrames.size() == 1)
	{
		Texture = vFrames.front().m_Texture;
		return Texture.IsValid();
	}

	int64_t TotalDuration = 0;
	for(const auto &Frame : vFrames)
		TotalDuration += ClampFrameDurationMs(Frame.m_DurationMs);

	if(TotalDuration <= 0)
	{
		Texture = vFrames.front().m_Texture;
		return Texture.IsValid();
	}

	const int64_t ElapsedMs = ((time_get() - AnimationStart) * 1000) / time_freq();
	int64_t Offset = ElapsedMs % TotalDuration;
	for(const auto &Frame : vFrames)
	{
		const int DurationMs = ClampFrameDurationMs(Frame.m_DurationMs);
		if(Offset < DurationMs)
		{
			Texture = Frame.m_Texture;
			return Texture.IsValid();
		}
		Offset -= DurationMs;
	}

	Texture = vFrames.front().m_Texture;
	return Texture.IsValid();
}

bool DecodeImageToRgba(IGraphics *pGraphics, const unsigned char *pData, size_t DataSize, const char *pContextName, CImageInfo &Image)
{
	Image.Free();

	if(pGraphics != nullptr && pData != nullptr && DataSize > 0 && pGraphics->LoadPng(Image, pData, DataSize, pContextName))
		return true;

	if(!pData || DataSize == 0)
		return false;

	CFfmpegMemoryReader Reader{pData, DataSize, 0};
	uint8_t *pIoBuffer = (uint8_t *)av_malloc(4096);
	if(!pIoBuffer)
		return false;

	AVIOContext *pIoCtx = avio_alloc_context(pIoBuffer, 4096, 0, &Reader, FfmpegReadPacket, nullptr, FfmpegSeek);
	if(!pIoCtx)
	{
		av_free(pIoBuffer);
		return false;
	}

	AVFormatContext *pFormatCtx = avformat_alloc_context();
	if(!pFormatCtx)
	{
		avio_context_free(&pIoCtx);
		return false;
	}

	bool Success = false;
	AVCodecContext *pCodecCtx = nullptr;
	SwsContext *pSwsCtx = nullptr;
	AVPacket *pPacket = nullptr;
	AVFrame *pFrame = nullptr;
	AVFrame *pFrameRgba = nullptr;
	int VideoStream = -1;
	int SrcW = 0;
	int SrcH = 0;
	size_t FrameBytes = 0;

	auto CopyCurrentFrame = [&]() -> bool {
		if(!pFrame || !pFrameRgba || !pSwsCtx || SrcW <= 0 || SrcH <= 0)
			return false;
		if(av_frame_make_writable(pFrameRgba) < 0)
			return false;
		const int ScaledLines = sws_scale(pSwsCtx, pFrame->data, pFrame->linesize, 0, SrcH, pFrameRgba->data, pFrameRgba->linesize);
		if(ScaledLines <= 0 || pFrameRgba->linesize[0] <= 0 || (size_t)pFrameRgba->linesize[0] < (size_t)SrcW * 4ull)
			return false;

		Image.Free();
		Image.m_Width = SrcW;
		Image.m_Height = SrcH;
		Image.m_Format = CImageInfo::FORMAT_RGBA;
		Image.m_pData = (uint8_t *)malloc(FrameBytes);
		if(!Image.m_pData)
			return false;

		for(int y = 0; y < SrcH; ++y)
			mem_copy(Image.m_pData + (size_t)y * (size_t)SrcW * 4ull, pFrameRgba->data[0] + (size_t)y * (size_t)pFrameRgba->linesize[0], (size_t)SrcW * 4ull);

		return true;
	};

	pFormatCtx->pb = pIoCtx;
	pFormatCtx->flags |= AVFMT_FLAG_CUSTOM_IO;

	do
	{
		if(avformat_open_input(&pFormatCtx, nullptr, nullptr, nullptr) < 0 || avformat_find_stream_info(pFormatCtx, nullptr) < 0)
			break;

		VideoStream = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
		if(VideoStream < 0)
			break;

		AVStream *pStream = pFormatCtx->streams[VideoStream];
		const AVCodec *pCodec = avcodec_find_decoder(pStream->codecpar->codec_id);
		if(!pCodec)
			break;

		pCodecCtx = avcodec_alloc_context3(pCodec);
		if(!pCodecCtx || avcodec_parameters_to_context(pCodecCtx, pStream->codecpar) < 0 || avcodec_open2(pCodecCtx, pCodec, nullptr) < 0)
			break;

		SrcW = pCodecCtx->width;
		SrcH = pCodecCtx->height;
		if(SrcW <= 0 || SrcH <= 0 || SrcW > MEDIA_MAX_DIMENSION || SrcH > MEDIA_MAX_DIMENSION)
			break;
		if((size_t)SrcW > std::numeric_limits<size_t>::max() / ((size_t)SrcH * 4ull))
			break;
		FrameBytes = (size_t)SrcW * (size_t)SrcH * 4ull;
		if(FrameBytes == 0 || FrameBytes > MEDIA_MAX_ANIMATED_MEMORY_BYTES)
			break;

		pSwsCtx = sws_getContext(SrcW, SrcH, pCodecCtx->pix_fmt, SrcW, SrcH, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
		if(!pSwsCtx)
			break;

		pPacket = av_packet_alloc();
		pFrame = av_frame_alloc();
		pFrameRgba = av_frame_alloc();
		if(!pPacket || !pFrame || !pFrameRgba)
			break;

		pFrameRgba->format = AV_PIX_FMT_RGBA;
		pFrameRgba->width = SrcW;
		pFrameRgba->height = SrcH;
		if(av_frame_get_buffer(pFrameRgba, 1) < 0)
			break;

		while(av_read_frame(pFormatCtx, pPacket) >= 0)
		{
			if(pPacket->stream_index == VideoStream)
			{
				if(avcodec_send_packet(pCodecCtx, pPacket) < 0)
				{
					av_packet_unref(pPacket);
					break;
				}
				while(avcodec_receive_frame(pCodecCtx, pFrame) == 0)
				{
					if(CopyCurrentFrame())
					{
						Success = true;
						break;
					}
				}
			}
			av_packet_unref(pPacket);
			if(Success)
				break;
		}

		if(!Success && avcodec_send_packet(pCodecCtx, nullptr) >= 0)
		{
			while(avcodec_receive_frame(pCodecCtx, pFrame) == 0)
			{
				if(CopyCurrentFrame())
				{
					Success = true;
					break;
				}
			}
		}
	} while(false);

	if(!Success)
		Image.Free();

	if(pFrameRgba)
		av_frame_free(&pFrameRgba);
	if(pFrame)
		av_frame_free(&pFrame);
	if(pPacket)
		av_packet_free(&pPacket);
	if(pSwsCtx)
		sws_freeContext(pSwsCtx);
	if(pCodecCtx)
		avcodec_free_context(&pCodecCtx);
	if(pFormatCtx)
	{
		if(pFormatCtx->pb == pIoCtx)
			pFormatCtx->pb = nullptr;
		avformat_close_input(&pFormatCtx);
	}
	if(pIoCtx)
		avio_context_free(&pIoCtx);

	return Success;
}

bool DecodeImageWithFfmpegCpu(IGraphics *pGraphics, const unsigned char *pData, size_t DataSize, const char *pContextName, SMediaDecodedFrames &DecodedFrames, const SMediaDecodeLimits &Limits)
{
	DecodedFrames.Free();
	(void)pGraphics;
	(void)pContextName;

	if(!pData || DataSize == 0)
		return false;

	CFfmpegMemoryReader Reader{pData, DataSize, 0};
	uint8_t *pIoBuffer = (uint8_t *)av_malloc(4096);
	if(!pIoBuffer)
		return false;

	AVIOContext *pIoCtx = avio_alloc_context(pIoBuffer, 4096, 0, &Reader, FfmpegReadPacket, nullptr, FfmpegSeek);
	if(!pIoCtx)
	{
		av_free(pIoBuffer);
		return false;
	}

	AVFormatContext *pFormatCtx = avformat_alloc_context();
	if(!pFormatCtx)
	{
		avio_context_free(&pIoCtx);
		return false;
	}

	bool Success = false;
	AVCodecContext *pCodecCtx = nullptr;
	SwsContext *pSwsCtx = nullptr;
	AVPacket *pPacket = nullptr;
	AVFrame *pFrame = nullptr;
	AVFrame *pFrameRgba = nullptr;
	int VideoStream = -1;
	AVStream *pStream = nullptr;
	int SrcW = 0;
	int SrcH = 0;
	int DstW = 0;
	int DstH = 0;
	int64_t LastValidPts = AV_NOPTS_VALUE;
	bool StopDecode = false;
	bool DecodeFailed = false;
	bool DecodeAllFrames = Limits.m_DecodeAllFrames;
	int FallbackFrameDurationMs = MEDIA_DEFAULT_FRAME_MS;
	int PreferredFrameDurationMsFromRate = 0;
	int64_t PacketDurationTsForNextFrame = 0;
	size_t FrameBytes = 0;
	size_t TotalDecodedBytes = 0;
	int64_t DecodedTimelineMs = 0;

	auto MergeFramePairs = [&]() {
		if(DecodedFrames.m_vFrames.size() <= 1)
			return;

		size_t Out = 0;
		const size_t Count = DecodedFrames.m_vFrames.size();
		for(size_t i = 0; i < Count; i += 2)
		{
			if(Out != i)
				DecodedFrames.m_vFrames[Out] = std::move(DecodedFrames.m_vFrames[i]);
			if(i + 1 < Count)
			{
				const int CombinedDuration = DecodedFrames.m_vFrames[Out].m_DurationMs + DecodedFrames.m_vFrames[i + 1].m_DurationMs;
				DecodedFrames.m_vFrames[Out].m_DurationMs = ClampFrameDurationMs(CombinedDuration);
			}
			Out++;
		}
		DecodedFrames.m_vFrames.resize(Out);
	};

	auto RecomputeTotalDecodedBytes = [&]() {
		TotalDecodedBytes = FrameBytes * DecodedFrames.m_vFrames.size();
	};

	auto EnforceAnimationLimits = [&]() {
		if(!DecodeAllFrames)
			return;

		RecomputeTotalDecodedBytes();
		while(true)
		{
			const bool OverFrames = Limits.m_MaxFrames > 0 && (int)DecodedFrames.m_vFrames.size() > Limits.m_MaxFrames;
			const bool OverBytes = Limits.m_MaxTotalBytes > 0 && TotalDecodedBytes > Limits.m_MaxTotalBytes;
			if(!(OverFrames || OverBytes) || DecodedFrames.m_vFrames.size() <= 1)
				break;
			MergeFramePairs();
			RecomputeTotalDecodedBytes();
		}

		DecodedTimelineMs = 0;
		for(const auto &Frame : DecodedFrames.m_vFrames)
			DecodedTimelineMs += ClampFrameDurationMs(Frame.m_DurationMs);
	};

	auto AddFrame = [&](int DurationMs) {
		if(FrameBytes == 0 || DstW <= 0 || DstH <= 0 || !pFrame || !pFrame->data[0] || !pFrameRgba || !pSwsCtx || !pFrameRgba->data[0])
			return false;

		SMediaRawFrame Frame;
		Frame.m_DurationMs = ClampFrameDurationMs(DurationMs);
		Frame.m_Image.m_Width = DstW;
		Frame.m_Image.m_Height = DstH;
		Frame.m_Image.m_Format = CImageInfo::FORMAT_RGBA;
		Frame.m_Image.m_pData = (uint8_t *)malloc(FrameBytes);
		if(!Frame.m_Image.m_pData)
			return false;

		if(av_frame_make_writable(pFrameRgba) < 0)
		{
			return false;
		}

		const int ScaledLines = sws_scale(pSwsCtx, pFrame->data, pFrame->linesize, 0, SrcH, pFrameRgba->data, pFrameRgba->linesize);
		if(ScaledLines <= 0)
			return false;
		if(pFrameRgba->linesize[0] <= 0 || (size_t)pFrameRgba->linesize[0] < (size_t)DstW * 4ull)
			return false;

		for(int y = 0; y < DstH; ++y)
			mem_copy(Frame.m_Image.m_pData + (size_t)y * (size_t)DstW * 4ull, pFrameRgba->data[0] + (size_t)y * (size_t)pFrameRgba->linesize[0], (size_t)DstW * 4ull);

		DecodedFrames.m_vFrames.push_back(std::move(Frame));
		TotalDecodedBytes += FrameBytes;
		return true;
	};

	auto ConsumeFrame = [&]() {
		int DurationMs = FallbackFrameDurationMs;
		if(DecodeAllFrames)
		{
			const int64_t PacketDurationTs = PacketDurationTsForNextFrame;
			PacketDurationTsForNextFrame = 0;

			const int64_t CurPts = pFrame->best_effort_timestamp;
			int PtsDiffMs = 0;
			if(LastValidPts != AV_NOPTS_VALUE && CurPts != AV_NOPTS_VALUE)
			{
				const int64_t PtsDiffTs = CurPts - LastValidPts;
				if(PtsDiffTs > 0)
				{
					const int64_t Rescaled = av_rescale_q(PtsDiffTs, pStream->time_base, AVRational{1, 1000});
					if(Rescaled > 0)
						PtsDiffMs = (int)minimum<int64_t>(Rescaled, MEDIA_MAX_FRAME_MS);
				}
			}

			int FrameDurationMs = 0;
			if(pFrame->duration > 0)
			{
				const int64_t Rescaled = av_rescale_q(pFrame->duration, pStream->time_base, AVRational{1, 1000});
				if(Rescaled > 0)
					FrameDurationMs = (int)minimum<int64_t>(Rescaled, MEDIA_MAX_FRAME_MS);
			}

			int PacketDurationMs = 0;
			if(PacketDurationTs > 0)
			{
				const int64_t Rescaled = av_rescale_q(PacketDurationTs, pStream->time_base, AVRational{1, 1000});
				if(Rescaled > 0)
					PacketDurationMs = (int)minimum<int64_t>(Rescaled, MEDIA_MAX_FRAME_MS);
			}

			bool ImplicitTiming = false;
			if(FrameDurationMs > 0)
			{
				DurationMs = FrameDurationMs;

				// Some demuxers/decoders fill AVFrame::duration inconsistently (wrong time base),
				// especially at the start. If PTS diff suggests a sane duration, prefer that.
				constexpr int MAX_REASONABLE_MS = 200; // >= 5 FPS
				if(PtsDiffMs > 0 && PtsDiffMs <= MAX_REASONABLE_MS && DurationMs > MAX_REASONABLE_MS && DurationMs >= PtsDiffMs * 4)
				{
					DurationMs = PtsDiffMs;
					ImplicitTiming = true;
				}
				else if(PreferredFrameDurationMsFromRate > 0 && DurationMs > MAX_REASONABLE_MS && DurationMs >= PreferredFrameDurationMsFromRate * 4)
				{
					DurationMs = PreferredFrameDurationMsFromRate;
					ImplicitTiming = true;
				}
			}
			else if(PacketDurationMs > 0)
			{
				DurationMs = PacketDurationMs;
				ImplicitTiming = true;
			}
			else if(PtsDiffMs > 0)
			{
				DurationMs = PtsDiffMs;
				ImplicitTiming = true;
			}

			if(ImplicitTiming)
			{
				constexpr int MAX_REASONABLE_IMPLICIT_MS = 200; // >= 5 FPS
				if(DurationMs > MAX_REASONABLE_IMPLICIT_MS)
					DurationMs = FallbackFrameDurationMs;
			}
		}

		// Adaptive fallback: if we ever see a sane faster duration, use it for subsequent missing-timing frames.
		{
			constexpr int MAX_REASONABLE_FALLBACK_UPDATE_MS = 200; // >= 5 FPS
			if(DurationMs > 0 && DurationMs <= MAX_REASONABLE_FALLBACK_UPDATE_MS)
				FallbackFrameDurationMs = minimum(FallbackFrameDurationMs, ClampFrameDurationMs(DurationMs));
		}

		// Startup smoothing: some formats/codecs produce bogus long durations for the first few frames.
		// Cap initial per-frame durations relative to the fallback to avoid a "slideshow" start.
		{
			constexpr size_t STARTUP_FRAME_CLAMP_COUNT = 60;
			const size_t FrameIndex = DecodedFrames.m_vFrames.size();
			if(FrameIndex < STARTUP_FRAME_CLAMP_COUNT && FallbackFrameDurationMs > 0)
			{
				constexpr int MAX_STARTUP_FRAME_MS_ABS = 200; // >= 5 FPS
				const int StartupMaxFrameMs = minimum(MAX_STARTUP_FRAME_MS_ABS, ClampFrameDurationMs(FallbackFrameDurationMs * 2));
				if(DurationMs > StartupMaxFrameMs)
					DurationMs = StartupMaxFrameMs;
			}
		}

		if(pFrame->best_effort_timestamp != AV_NOPTS_VALUE)
			LastValidPts = pFrame->best_effort_timestamp;
		if(!AddFrame(DurationMs))
			return false;

		if(DecodeAllFrames)
		{
			DecodedTimelineMs += DecodedFrames.m_vFrames.back().m_DurationMs;
			if(Limits.m_MaxAnimationDurationMs > 0 && DecodedTimelineMs > Limits.m_MaxAnimationDurationMs)
			{
				// Animation is too long, fall back to a single-frame thumbnail.
				DecodeAllFrames = false;
				if(!DecodedFrames.m_vFrames.empty())
					DecodedFrames.m_vFrames.resize(1);
				RecomputeTotalDecodedBytes();
				StopDecode = true;
				return !DecodedFrames.m_vFrames.empty();
			}

			EnforceAnimationLimits();
		}

		if(!DecodeAllFrames)
			StopDecode = true;
		return true;
	};

	pFormatCtx->pb = pIoCtx;
	pFormatCtx->flags |= AVFMT_FLAG_CUSTOM_IO;

	do
	{
		if(avformat_open_input(&pFormatCtx, nullptr, nullptr, nullptr) < 0 || avformat_find_stream_info(pFormatCtx, nullptr) < 0)
			break;

		VideoStream = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
		if(VideoStream < 0)
			break;

		pStream = pFormatCtx->streams[VideoStream];
		const AVCodec *pCodec = avcodec_find_decoder(pStream->codecpar->codec_id);
		if(!pCodec)
			break;

		pCodecCtx = avcodec_alloc_context3(pCodec);
		if(!pCodecCtx || avcodec_parameters_to_context(pCodecCtx, pStream->codecpar) < 0 || avcodec_open2(pCodecCtx, pCodec, nullptr) < 0)
			break;

		// If per-frame timing is missing/invalid, fall back to the stream's frame rate.
		{
			AVRational Rate = pStream->avg_frame_rate;
			if(Rate.num <= 0 || Rate.den <= 0)
				Rate = av_guess_frame_rate(pFormatCtx, pStream, nullptr);
			if(Rate.num <= 0 || Rate.den <= 0)
				Rate = pStream->r_frame_rate;
			if(Rate.num <= 0 || Rate.den <= 0)
				Rate = pCodecCtx->framerate;
			if(Rate.num > 0 && Rate.den > 0)
			{
				const int64_t Ms = (1000ll * (int64_t)Rate.den) / (int64_t)Rate.num;
				// Never allow the FPS fallback to be slower than the default (prevents "startup slideshow").
				constexpr int64_t MAX_REASONABLE_FALLBACK_MS = 200; // >= 5 FPS
				if(Ms > 0 && Ms <= MAX_REASONABLE_FALLBACK_MS)
				{
					const int CandidateMs = ClampFrameDurationMs((int)Ms);
					PreferredFrameDurationMsFromRate = CandidateMs;
					FallbackFrameDurationMs = minimum(FallbackFrameDurationMs, CandidateMs);
				}
			}
		}

		if(DecodeAllFrames && Limits.m_MaxAnimationDurationMs > 0)
		{
			int64_t DurationMs = 0;
			if(pStream->duration > 0)
				DurationMs = av_rescale_q(pStream->duration, pStream->time_base, AVRational{1, 1000});
			else if(pFormatCtx->duration > 0)
				DurationMs = pFormatCtx->duration / (AV_TIME_BASE / 1000);

			if(DurationMs > Limits.m_MaxAnimationDurationMs)
				DecodeAllFrames = false;
		}

		SrcW = pCodecCtx->width;
		SrcH = pCodecCtx->height;
		if(SrcW <= 0 || SrcH <= 0 || SrcW > MEDIA_MAX_DIMENSION || SrcH > MEDIA_MAX_DIMENSION)
			break;

		ClampImageSize(SrcW, SrcH, Limits.m_MaxDimension, DstW, DstH);
		if((size_t)DstW > std::numeric_limits<size_t>::max() / ((size_t)DstH * 4ull))
			break;
		FrameBytes = (size_t)DstW * (size_t)DstH * 4ull;
		if(FrameBytes == 0 || FrameBytes > Limits.m_MaxTotalBytes)
			break;

		pSwsCtx = sws_getContext(SrcW, SrcH, pCodecCtx->pix_fmt, DstW, DstH, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
		if(!pSwsCtx)
			break;

		pPacket = av_packet_alloc();
		pFrame = av_frame_alloc();
		pFrameRgba = av_frame_alloc();
		if(!pPacket || !pFrame || !pFrameRgba)
			break;

		pFrameRgba->format = AV_PIX_FMT_RGBA;
		pFrameRgba->width = DstW;
		pFrameRgba->height = DstH;
		if(av_frame_get_buffer(pFrameRgba, 1) < 0)
			break;

		if(av_frame_make_writable(pFrameRgba) < 0)
			break;

		while(!StopDecode && av_read_frame(pFormatCtx, pPacket) >= 0)
		{
			if(pPacket->stream_index == VideoStream)
			{
				PacketDurationTsForNextFrame = pPacket->duration > 0 ? pPacket->duration : 0;
				if(avcodec_send_packet(pCodecCtx, pPacket) < 0)
				{
					av_packet_unref(pPacket);
					DecodeFailed = true;
					break;
				}
				while(!StopDecode && avcodec_receive_frame(pCodecCtx, pFrame) == 0)
				{
					if(!ConsumeFrame())
					{
						av_packet_unref(pPacket);
						DecodeFailed = true;
						StopDecode = true;
						break;
					}
				}
			}
			av_packet_unref(pPacket);
		}

		if(!DecodeFailed && !DecodedFrames.m_vFrames.empty())
		{
			DecodedFrames.m_Width = DstW;
			DecodedFrames.m_Height = DstH;
			DecodedFrames.m_Animated = DecodeAllFrames && DecodedFrames.m_vFrames.size() > 1;
			DecodedFrames.m_AnimationStart = time_get();
			Success = true;
			break;
		}

		if(!DecodeFailed && !StopDecode && avcodec_send_packet(pCodecCtx, nullptr) >= 0)
		{
			while(!StopDecode && avcodec_receive_frame(pCodecCtx, pFrame) == 0)
			{
				if(!ConsumeFrame())
				{
					DecodeFailed = true;
					break;
				}
			}
		}

		if(DecodeFailed || DecodedFrames.m_vFrames.empty())
			break;

		DecodedFrames.m_Width = DstW;
		DecodedFrames.m_Height = DstH;
		DecodedFrames.m_Animated = DecodeAllFrames && DecodedFrames.m_vFrames.size() > 1;
		DecodedFrames.m_AnimationStart = time_get();
		Success = true;
	} while(false);

	if(!Success)
		DecodedFrames.Free();

	if(pFrameRgba)
		av_frame_free(&pFrameRgba);
	if(pFrame)
		av_frame_free(&pFrame);
	if(pPacket)
		av_packet_free(&pPacket);
	if(pSwsCtx)
		sws_freeContext(pSwsCtx);
	if(pCodecCtx)
		avcodec_free_context(&pCodecCtx);
	if(pFormatCtx)
	{
		if(pFormatCtx->pb == pIoCtx)
			pFormatCtx->pb = nullptr;
		avformat_close_input(&pFormatCtx);
	}
	if(pIoCtx)
		avio_context_free(&pIoCtx);

	return Success;
}

bool DecodeStaticImageCpu(IGraphics *pGraphics, const unsigned char *pData, size_t DataSize, const char *pContextName, SMediaDecodedFrames &DecodedFrames, int MaxDimension)
{
	DecodedFrames.Free();

	if(pGraphics == nullptr || !pData || DataSize == 0)
		return false;

	CImageInfo Image;
	if(pGraphics->LoadPng(Image, pData, DataSize, pContextName))
	{
		ClampImageSize(Image, MaxDimension);
		DecodedFrames.m_Width = (int)Image.m_Width;
		DecodedFrames.m_Height = (int)Image.m_Height;
		DecodedFrames.m_Animated = false;
		DecodedFrames.m_AnimationStart = time_get();
		SMediaRawFrame Frame;
		Frame.m_Image = std::move(Image);
		DecodedFrames.m_vFrames.push_back(std::move(Frame));
		return true;
	}

	SMediaDecodeLimits Limits;
	Limits.m_MaxDimension = MaxDimension;
	Limits.m_DecodeAllFrames = false;
	return DecodeImageWithFfmpegCpu(pGraphics, pData, DataSize, pContextName, DecodedFrames, Limits);
}

bool DecodeAnimatedImageCpu(IGraphics *pGraphics, const unsigned char *pData, size_t DataSize, const char *pContextName, SMediaDecodedFrames &DecodedFrames, const SMediaDecodeLimits &Limits)
{
	SMediaDecodeLimits AnimatedLimits = Limits;
	AnimatedLimits.m_DecodeAllFrames = true;
	if(!DecodeImageWithFfmpegCpu(pGraphics, pData, DataSize, pContextName, DecodedFrames, AnimatedLimits))
		return false;
	DecodedFrames.m_Animated = DecodedFrames.m_vFrames.size() > 1;
	return !DecodedFrames.m_vFrames.empty();
}

bool DecodeImageWithFfmpeg(IGraphics *pGraphics, const unsigned char *pData, size_t DataSize, const char *pContextName, std::vector<SMediaFrame> &vFrames, bool &Animated, int &Width, int &Height, int64_t &AnimationStart, bool DecodeAllFrames, int MaxAnimationDurationMs)
{
	UnloadFrames(pGraphics, vFrames);
	Animated = false;
	Width = 0;
	Height = 0;
	AnimationStart = 0;

	SMediaDecodedFrames DecodedFrames;
	SMediaDecodeLimits Limits;
	Limits.m_DecodeAllFrames = DecodeAllFrames;
	Limits.m_MaxAnimationDurationMs = MaxAnimationDurationMs;
	if(!DecodeImageWithFfmpegCpu(pGraphics, pData, DataSize, pContextName, DecodedFrames, Limits))
		return false;
	const int DecodedWidth = DecodedFrames.m_Width;
	const int DecodedHeight = DecodedFrames.m_Height;
	if(!UploadFrames(pGraphics, DecodedFrames, vFrames, pContextName))
		return false;

	Animated = DecodeAllFrames && vFrames.size() > 1;
	Width = DecodedWidth;
	Height = DecodedHeight;
	AnimationStart = time_get();
	return true;
}

bool DecodeStaticImage(IGraphics *pGraphics, const unsigned char *pData, size_t DataSize, const char *pContextName, std::vector<SMediaFrame> &vFrames, bool &Animated, int &Width, int &Height, int64_t &AnimationStart)
{
	UnloadFrames(pGraphics, vFrames);
	Animated = false;
	Width = 0;
	Height = 0;
	AnimationStart = 0;

	SMediaDecodedFrames DecodedFrames;
	if(!DecodeStaticImageCpu(pGraphics, pData, DataSize, pContextName, DecodedFrames, 0))
		return false;
	const int DecodedWidth = DecodedFrames.m_Width;
	const int DecodedHeight = DecodedFrames.m_Height;
	if(!UploadFrames(pGraphics, DecodedFrames, vFrames, pContextName))
		return false;

	Width = DecodedWidth;
	Height = DecodedHeight;
	AnimationStart = time_get();
	return true;
}

bool DecodeAnimatedImage(IGraphics *pGraphics, const unsigned char *pData, size_t DataSize, const char *pContextName, std::vector<SMediaFrame> &vFrames, bool &Animated, int &Width, int &Height, int64_t &AnimationStart, int MaxAnimationDurationMs)
{
	SMediaDecodedFrames DecodedFrames;
	SMediaDecodeLimits Limits;
	Limits.m_DecodeAllFrames = true;
	Limits.m_MaxAnimationDurationMs = MaxAnimationDurationMs;
	if(!DecodeAnimatedImageCpu(pGraphics, pData, DataSize, pContextName, DecodedFrames, Limits))
		return false;
	const int DecodedWidth = DecodedFrames.m_Width;
	const int DecodedHeight = DecodedFrames.m_Height;
	if(!UploadFrames(pGraphics, DecodedFrames, vFrames, pContextName))
		return false;

	Animated = vFrames.size() > 1;
	Width = DecodedWidth;
	Height = DecodedHeight;
	AnimationStart = time_get();
	return !vFrames.empty();
}
}
