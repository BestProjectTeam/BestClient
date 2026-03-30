#include "menu_media_background.h"

#include <base/system.h>

#include <engine/shared/config.h>

#include <algorithm>

namespace
{
constexpr int MENU_MEDIA_MAX_VIDEO_FRAME_MS = 250;
constexpr int MENU_MEDIA_DEFAULT_VIDEO_FRAME_MS = 33;
}

CMenuMediaBackground::~CMenuMediaBackground()
{
	Unload();
}

void CMenuMediaBackground::Init(IGraphics *pGraphics, IStorage *pStorage)
{
	m_pGraphics = pGraphics;
	m_pStorage = pStorage;
	SetStatus("Disabled.");
}

void CMenuMediaBackground::SetStatus(const char *pText)
{
	m_HasError = false;
	str_copy(m_aStatus, pText, sizeof(m_aStatus));
}

void CMenuMediaBackground::SetError(const char *pText)
{
	m_HasError = true;
	str_copy(m_aStatus, pText, sizeof(m_aStatus));
}

void CMenuMediaBackground::ClearVideoState()
{
	if(m_pGraphics != nullptr)
		m_pGraphics->UnloadTexture(&m_VideoTexture);
	if(m_pPacket)
		av_packet_free(&m_pPacket);
	if(m_pFrameRgba)
		av_frame_free(&m_pFrameRgba);
	if(m_pFrame)
		av_frame_free(&m_pFrame);
	if(m_pSwsCtx)
		sws_freeContext(m_pSwsCtx);
	if(m_pCodecCtx)
		avcodec_free_context(&m_pCodecCtx);
	if(m_pFormatCtx)
		avformat_close_input(&m_pFormatCtx);

	m_pFormatCtx = nullptr;
	m_pCodecCtx = nullptr;
	m_pFrame = nullptr;
	m_pFrameRgba = nullptr;
	m_pPacket = nullptr;
	m_pSwsCtx = nullptr;
	m_VideoStream = -1;
	m_LastVideoPts = AV_NOPTS_VALUE;
	m_NextFrameTime = std::chrono::nanoseconds::zero();
	m_vVideoUploadBuffer.clear();
}

void CMenuMediaBackground::Unload()
{
	MediaDecoder::UnloadFrames(m_pGraphics, m_vFrames);
	ClearVideoState();

	m_Animated = false;
	m_Width = 0;
	m_Height = 0;
	m_AnimationStart = 0;
	m_IsVideo = false;
	m_IsLoaded = false;
	m_HasError = false;
	m_RuntimeState = ESubsystemRuntimeState::DISABLED;
	m_aLoadedPath[0] = '\0';
	m_LastConfigEnabled = -1;
	m_aLastConfigPath[0] = '\0';
	m_LastDecodeStepTick = 0;
	m_LastPerfReportTick = 0;
	m_LastUpdateCostTick = 0;
	m_MaxUpdateCostTick = 0;
	m_TotalUpdateCostTick = 0;
	m_UpdateSamples = 0;
	str_copy(m_aStatus, "Disabled.", sizeof(m_aStatus));
}

void CMenuMediaBackground::Shutdown()
{
	Unload();
	m_pGraphics = nullptr;
	m_pStorage = nullptr;
}

bool CMenuMediaBackground::LoadStaticMedia(const char *pPath, int StorageType)
{
	m_IsVideo = false;

	const std::string Ext = MediaDecoder::ExtractExtensionLower(pPath);
	if(Ext == "png")
	{
		CImageInfo Image;
		if(!m_pGraphics->LoadPng(Image, pPath, StorageType))
		{
			SetError("Failed to load PNG.");
			return false;
		}

		MediaDecoder::UnloadFrames(m_pGraphics, m_vFrames);
		const int ImageWidth = (int)Image.m_Width;
		const int ImageHeight = (int)Image.m_Height;
		SMediaFrame Frame;
		Frame.m_Texture = m_pGraphics->LoadTextureRawMove(Image, 0, pPath);
		if(!Frame.m_Texture.IsValid())
		{
			Image.Free();
			SetError("Failed to upload PNG.");
			return false;
		}

		m_vFrames.push_back(Frame);
		m_Animated = false;
		m_Width = ImageWidth;
		m_Height = ImageHeight;
		m_AnimationStart = time_get();
		m_IsLoaded = true;
		SetStatus("Loaded image.");
		return true;
	}

	void *pData = nullptr;
	unsigned DataSize = 0;
	if(!m_pStorage->ReadFile(pPath, StorageType, &pData, &DataSize))
	{
		SetError("Failed to read file.");
		return false;
	}

	const bool AnimatedImage = MediaDecoder::IsLikelyAnimatedImageExtension(Ext);
	const bool Success = AnimatedImage ?
		MediaDecoder::DecodeAnimatedImage(m_pGraphics, static_cast<unsigned char *>(pData), DataSize, pPath, m_vFrames, m_Animated, m_Width, m_Height, m_AnimationStart, 15000) :
		MediaDecoder::DecodeStaticImage(m_pGraphics, static_cast<unsigned char *>(pData), DataSize, pPath, m_vFrames, m_Animated, m_Width, m_Height, m_AnimationStart);
	free(pData);

	if(!Success)
	{
		SetError("Failed to decode image.");
		return false;
	}

	m_IsLoaded = true;
	SetStatus(m_Animated ? "Loaded animated image." : "Loaded image.");
	return true;
}

bool CMenuMediaBackground::UploadCurrentVideoFrame(const char *pContextName, int DurationMs)
{
	if(m_pGraphics == nullptr || m_pFrame == nullptr || m_pFrameRgba == nullptr || m_pSwsCtx == nullptr || m_Width <= 0 || m_Height <= 0)
		return false;

	const size_t FrameBytes = (size_t)m_Width * (size_t)m_Height * 4ull;
	if(m_vVideoUploadBuffer.size() != FrameBytes)
		m_vVideoUploadBuffer.resize(FrameBytes);
	CImageInfo Image;
	Image.m_Width = m_Width;
	Image.m_Height = m_Height;
	Image.m_Format = CImageInfo::FORMAT_RGBA;

	if(av_frame_make_writable(m_pFrameRgba) < 0)
		return false;

	sws_scale(m_pSwsCtx, m_pFrame->data, m_pFrame->linesize, 0, m_Height, m_pFrameRgba->data, m_pFrameRgba->linesize);
	for(int y = 0; y < m_Height; ++y)
		mem_copy(m_vVideoUploadBuffer.data() + (size_t)y * (size_t)m_Width * 4ull, m_pFrameRgba->data[0] + (size_t)y * (size_t)m_pFrameRgba->linesize[0], (size_t)m_Width * 4ull);
	Image.m_pData = m_vVideoUploadBuffer.data();
	if(!m_VideoTexture.IsValid())
	{
		m_VideoTexture = m_pGraphics->LoadTextureRaw(Image, 0, pContextName);
		if(!m_VideoTexture.IsValid())
			return false;
	}
	else if(!m_pGraphics->UpdateTextureRaw(m_VideoTexture, 0, 0, m_Width, m_Height, Image, false))
	{
		return false;
	}

	m_LastVideoPts = m_pFrame->best_effort_timestamp;
	m_NextFrameTime = time_get_nanoseconds() + std::chrono::milliseconds(std::clamp(DurationMs, 1, MENU_MEDIA_MAX_VIDEO_FRAME_MS));
	return true;
}

bool CMenuMediaBackground::DecodeNextVideoFrame(bool LoopOnEof)
{
	if(!m_pFormatCtx || !m_pCodecCtx || !m_pPacket || !m_pFrame)
		return false;

	while(true)
	{
		while(avcodec_receive_frame(m_pCodecCtx, m_pFrame) == 0)
		{
			int DurationMs = MENU_MEDIA_DEFAULT_VIDEO_FRAME_MS;
			int64_t DurationTs = m_pFrame->duration;
			if(DurationTs <= 0 && m_LastVideoPts != AV_NOPTS_VALUE && m_pFrame->best_effort_timestamp != AV_NOPTS_VALUE)
				DurationTs = m_pFrame->best_effort_timestamp - m_LastVideoPts;
			if(DurationTs > 0)
			{
				const int64_t Rescaled = av_rescale_q(DurationTs, m_pFormatCtx->streams[m_VideoStream]->time_base, AVRational{1, 1000});
				if(Rescaled > 0)
					DurationMs = (int)Rescaled;
			}
			return UploadCurrentVideoFrame(m_aLoadedPath, DurationMs);
		}

		const int ReadResult = av_read_frame(m_pFormatCtx, m_pPacket);
		if(ReadResult < 0)
		{
			if(!LoopOnEof)
				return false;

			av_seek_frame(m_pFormatCtx, m_VideoStream, 0, AVSEEK_FLAG_BACKWARD);
			avcodec_flush_buffers(m_pCodecCtx);
			m_LastVideoPts = AV_NOPTS_VALUE;
			continue;
		}

		if(m_pPacket->stream_index != m_VideoStream)
		{
			av_packet_unref(m_pPacket);
			continue;
		}

		const int SendResult = avcodec_send_packet(m_pCodecCtx, m_pPacket);
		av_packet_unref(m_pPacket);
		if(SendResult < 0)
			return false;
	}
}

bool CMenuMediaBackground::LoadVideo(const char *pPath, int StorageType)
{
	m_IsVideo = true;

	char aPath[IO_MAX_PATH_LENGTH];
	m_pStorage->GetCompletePath(StorageType, pPath, aPath, sizeof(aPath));

	if(avformat_open_input(&m_pFormatCtx, aPath, nullptr, nullptr) != 0)
	{
		SetError("Failed to open video.");
		return false;
	}
	if(avformat_find_stream_info(m_pFormatCtx, nullptr) < 0)
	{
		SetError("Failed to read video info.");
		return false;
	}

	m_VideoStream = av_find_best_stream(m_pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
	if(m_VideoStream < 0)
	{
		SetError("No video stream found.");
		return false;
	}

	const AVStream *pStream = m_pFormatCtx->streams[m_VideoStream];
	const AVCodec *pCodec = avcodec_find_decoder(pStream->codecpar->codec_id);
	if(!pCodec)
	{
		SetError("Unsupported video codec.");
		ClearVideoState();
		return false;
	}

	m_pCodecCtx = avcodec_alloc_context3(pCodec);
	if(!m_pCodecCtx || avcodec_parameters_to_context(m_pCodecCtx, pStream->codecpar) < 0 || avcodec_open2(m_pCodecCtx, pCodec, nullptr) < 0)
	{
		SetError("Failed to initialize video decoder.");
		ClearVideoState();
		return false;
	}

	m_Width = m_pCodecCtx->width;
	m_Height = m_pCodecCtx->height;
	if(m_Width <= 0 || m_Height <= 0)
	{
		SetError("Invalid video dimensions.");
		ClearVideoState();
		return false;
	}

	m_pFrame = av_frame_alloc();
	m_pFrameRgba = av_frame_alloc();
	m_pPacket = av_packet_alloc();
	if(!m_pFrame || !m_pFrameRgba || !m_pPacket)
	{
		SetError("Failed to allocate video frames.");
		ClearVideoState();
		return false;
	}

	m_pFrameRgba->format = AV_PIX_FMT_RGBA;
	m_pFrameRgba->width = m_Width;
	m_pFrameRgba->height = m_Height;
	if(av_frame_get_buffer(m_pFrameRgba, 1) < 0)
	{
		SetError("Failed to allocate RGBA frame.");
		ClearVideoState();
		return false;
	}

	m_pSwsCtx = sws_getContext(m_Width, m_Height, m_pCodecCtx->pix_fmt, m_Width, m_Height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
	if(!m_pSwsCtx)
	{
		SetError("Failed to initialize video scaler.");
		ClearVideoState();
		return false;
	}

	if(!DecodeNextVideoFrame(true))
	{
		SetError("Failed to decode first video frame.");
		ClearVideoState();
		return false;
	}

	m_IsLoaded = true;
	SetStatus("Loaded video.");
	return true;
}

void CMenuMediaBackground::ReloadFromConfig(int Enabled, const char *pPath)
{
	char aPath[IO_MAX_PATH_LENGTH];
	str_copy(aPath, pPath, sizeof(aPath));

	Unload();

	m_LastConfigEnabled = Enabled;
	str_copy(m_aLastConfigPath, aPath, sizeof(m_aLastConfigPath));

	if(!Enabled)
	{
		SetStatus("Disabled.");
		return;
	}
	if(aPath[0] == '\0')
	{
		SetError("No media selected.");
		return;
	}

	const int StorageType = fs_is_relative_path(aPath) ? IStorage::TYPE_SAVE : IStorage::TYPE_ABSOLUTE;
	const std::string Ext = MediaDecoder::ExtractExtensionLower(aPath);

	bool Success = false;
	if(MediaDecoder::IsLikelyVideoExtension(Ext))
		Success = LoadVideo(aPath, StorageType);
	else
		Success = LoadStaticMedia(aPath, StorageType);

	if(Success)
		str_copy(m_aLoadedPath, aPath, sizeof(m_aLoadedPath));
}

void CMenuMediaBackground::SyncFromConfig(int Enabled, const char *pPath)
{
	if(m_LastConfigEnabled != Enabled || str_comp(m_aLastConfigPath, pPath) != 0)
		ReloadFromConfig(Enabled, pPath);
}

void CMenuMediaBackground::ReloadFromConfig()
{
	ReloadFromConfig(g_Config.m_BcMenuMediaBackground, g_Config.m_BcMenuMediaBackgroundPath);
}

void CMenuMediaBackground::SyncFromConfig()
{
	SyncFromConfig(g_Config.m_BcMenuMediaBackground, g_Config.m_BcMenuMediaBackgroundPath);
}

void CMenuMediaBackground::Update()
{
	if(!m_IsLoaded || !m_IsVideo)
	{
		m_RuntimeState = ESubsystemRuntimeState::DISABLED;
		return;
	}

	const int64_t PerfStart = time_get();
	const auto Now = time_get_nanoseconds();
	m_RuntimeState = ESubsystemRuntimeState::ARMED;
	if(!CSubsystemTicker::ShouldRunPeriodic(time_get(), m_LastDecodeStepTick, time_freq() / 120))
		return;

	int Guard = 0;
	while(Now >= m_NextFrameTime && Guard < 2)
	{
		if(!DecodeNextVideoFrame(true))
		{
			SetError("Failed while decoding video.");
			m_IsLoaded = false;
			m_RuntimeState = ESubsystemRuntimeState::DISABLED;
			ClearVideoState();
			break;
		}
		m_RuntimeState = ESubsystemRuntimeState::ACTIVE;
		++Guard;
	}
	if(Guard == 2 && Now >= m_NextFrameTime)
	{
		m_RuntimeState = ESubsystemRuntimeState::COOLDOWN;
		m_NextFrameTime = Now + std::chrono::milliseconds(MENU_MEDIA_DEFAULT_VIDEO_FRAME_MS);
	}

	m_LastUpdateCostTick = time_get() - PerfStart;
	m_MaxUpdateCostTick = maximum(m_MaxUpdateCostTick, m_LastUpdateCostTick);
	m_TotalUpdateCostTick += m_LastUpdateCostTick;
	++m_UpdateSamples;
	if(g_Config.m_Debug)
	{
		const int64_t PerfNow = time_get();
		if(m_LastPerfReportTick == 0 || PerfNow - m_LastPerfReportTick >= time_freq())
		{
			dbg_msg("menumedia/perf", "update last=%.3fms avg=%.3fms max=%.3fms samples=%lld loaded=%d video=%d",
				m_LastUpdateCostTick * 1000.0 / (double)time_freq(),
				m_UpdateSamples > 0 ? (m_TotalUpdateCostTick * 1000.0 / (double)time_freq()) / (double)m_UpdateSamples : 0.0,
				m_MaxUpdateCostTick * 1000.0 / (double)time_freq(),
				(long long)m_UpdateSamples,
				m_IsLoaded ? 1 : 0,
				m_IsVideo ? 1 : 0);
			m_LastPerfReportTick = PerfNow;
			m_TotalUpdateCostTick = 0;
			m_UpdateSamples = 0;
			m_MaxUpdateCostTick = 0;
		}
	}
}

void CMenuMediaBackground::RenderTextureCover(IGraphics::CTextureHandle Texture, int Width, int Height, float TargetCenterX, float TargetCenterY, float TargetWidth, float TargetHeight)
{
	const float TextureAspect = Width > 0 && Height > 0 ? (float)Width / (float)Height : 1.0f;
	const float TargetAspect = TargetHeight > 0.0f ? TargetWidth / TargetHeight : 1.0f;

	float DrawW = TargetWidth;
	float DrawH = TargetHeight;

	if(TextureAspect > TargetAspect)
	{
		DrawH = TargetHeight;
		DrawW = DrawH * TextureAspect;
	}
	else
	{
		DrawW = TargetWidth;
		DrawH = DrawW / std::max(TextureAspect, 0.001f);
	}
	const float DrawX = TargetCenterX - DrawW * 0.5f;
	const float DrawY = TargetCenterY - DrawH * 0.5f;

	m_pGraphics->TextureSet(Texture);
	m_pGraphics->WrapClamp();
	m_pGraphics->QuadsBegin();
	m_pGraphics->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
	m_pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	const IGraphics::CQuadItem Quad(DrawX, DrawY, DrawW, DrawH);
	m_pGraphics->QuadsDrawTL(&Quad, 1);
	m_pGraphics->QuadsEnd();
	m_pGraphics->WrapNormal();
}

bool CMenuMediaBackground::Render(float ScreenWidth, float ScreenHeight, const SRenderContext *pRenderContext)
{
	if(!m_IsLoaded || m_pGraphics == nullptr)
		return false;

	IGraphics::CTextureHandle Texture;
	if(m_IsVideo)
		Texture = m_VideoTexture;
	else if(!MediaDecoder::GetCurrentFrameTexture(m_vFrames, m_Animated, m_AnimationStart, Texture))
		return false;

	if(!Texture.IsValid())
		return false;

	float PrevScreenX0, PrevScreenY0, PrevScreenX1, PrevScreenY1;
	m_pGraphics->GetScreen(&PrevScreenX0, &PrevScreenY0, &PrevScreenX1, &PrevScreenY1);
	m_pGraphics->BlendNormal();

	const bool UseWorldOffset = pRenderContext != nullptr &&
		pRenderContext->m_WorldOffset > 0.0f &&
		pRenderContext->m_ViewWidth > 0.0f &&
		pRenderContext->m_ViewHeight > 0.0f;
	if(UseWorldOffset)
	{
		const float WorldOffset = std::clamp(pRenderContext->m_WorldOffset, 0.0f, 1.0f);
		const float ViewWidth = pRenderContext->m_ViewWidth;
		const float ViewHeight = pRenderContext->m_ViewHeight;
		const float MapWidth = std::max(pRenderContext->m_MapWidth, ViewWidth);
		const float MapHeight = std::max(pRenderContext->m_MapHeight, ViewHeight);
		const float MapCenterX = MapWidth * 0.5f;
		const float MapCenterY = MapHeight * 0.5f;
		const float TargetCenterX = pRenderContext->m_CameraCenterX + (MapCenterX - pRenderContext->m_CameraCenterX) * WorldOffset;
		const float TargetCenterY = pRenderContext->m_CameraCenterY + (MapCenterY - pRenderContext->m_CameraCenterY) * WorldOffset;
		const float TargetWidth = ViewWidth + (MapWidth - ViewWidth) * WorldOffset;
		const float TargetHeight = ViewHeight + (MapHeight - ViewHeight) * WorldOffset;

		m_pGraphics->MapScreen(
			pRenderContext->m_CameraCenterX - ViewWidth * 0.5f,
			pRenderContext->m_CameraCenterY - ViewHeight * 0.5f,
			pRenderContext->m_CameraCenterX + ViewWidth * 0.5f,
			pRenderContext->m_CameraCenterY + ViewHeight * 0.5f);
		RenderTextureCover(Texture, m_Width, m_Height, TargetCenterX, TargetCenterY, TargetWidth, TargetHeight);
	}
	else
	{
		m_pGraphics->MapScreen(0.0f, 0.0f, ScreenWidth, ScreenHeight);
		RenderTextureCover(Texture, m_Width, m_Height, ScreenWidth * 0.5f, ScreenHeight * 0.5f, ScreenWidth, ScreenHeight);
	}

	m_pGraphics->TextureClear();
	m_pGraphics->MapScreen(PrevScreenX0, PrevScreenY0, PrevScreenX1, PrevScreenY1);
	return true;
}
