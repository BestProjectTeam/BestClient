#include "video_loader.h"

#include <base/system.h>

#include <algorithm>

extern "C" {
#include <libavutil/imgutils.h>
}

namespace
{
constexpr int VIDEO_LOADER_DEFAULT_FRAME_MS = 33;
constexpr int VIDEO_LOADER_MAX_FRAME_MS = 250;
}

void CVideoLoader::Init(IGraphics *pGraphics, IStorage *pStorage)
{
	m_pGraphics = pGraphics;
	m_pStorage = pStorage;
}

bool CVideoLoader::LoadVideo(const char *pFilename)
{
	Close();
	if(m_pGraphics == nullptr || pFilename == nullptr || pFilename[0] == '\0')
		return false;

	if(avformat_open_input(&m_pFormatCtx, pFilename, nullptr, nullptr) != 0)
		return false;
	if(avformat_find_stream_info(m_pFormatCtx, nullptr) < 0)
	{
		Close();
		return false;
	}

	for(unsigned i = 0; i < m_pFormatCtx->nb_streams; i++)
	{
		if(m_pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			m_VideoStream = (int)i;
			break;
		}
	}
	if(m_VideoStream < 0)
	{
		Close();
		return false;
	}

	AVCodecParameters *pCodecPar = m_pFormatCtx->streams[m_VideoStream]->codecpar;
	const AVCodec *pCodec = avcodec_find_decoder(pCodecPar->codec_id);
	if(!pCodec)
	{
		Close();
		return false;
	}

	m_pCodecCtx = avcodec_alloc_context3(pCodec);
	if(m_pCodecCtx == nullptr || avcodec_parameters_to_context(m_pCodecCtx, pCodecPar) < 0 || avcodec_open2(m_pCodecCtx, pCodec, nullptr) < 0)
	{
		Close();
		return false;
	}

	m_Width = m_pCodecCtx->width;
	m_Height = m_pCodecCtx->height;
	if(m_Width <= 0 || m_Height <= 0)
	{
		Close();
		return false;
	}

	m_pFrame = av_frame_alloc();
	m_pFrameRGB = av_frame_alloc();
	if(m_pFrame == nullptr || m_pFrameRGB == nullptr)
	{
		Close();
		return false;
	}

	const int BufferSize = av_image_get_buffer_size(AV_PIX_FMT_RGBA, m_Width, m_Height, 1);
	if(BufferSize <= 0)
	{
		Close();
		return false;
	}

	m_vBuffer.resize((size_t)BufferSize);
	if(av_image_fill_arrays(m_pFrameRGB->data, m_pFrameRGB->linesize, m_vBuffer.data(), AV_PIX_FMT_RGBA, m_Width, m_Height, 1) < 0)
	{
		Close();
		return false;
	}

	m_pSwsCtx = sws_getContext(m_Width, m_Height, m_pCodecCtx->pix_fmt,
		m_Width, m_Height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
	if(m_pSwsCtx == nullptr)
	{
		Close();
		return false;
	}

	CImageInfo ImgInfo;
	ImgInfo.m_Width = m_Width;
	ImgInfo.m_Height = m_Height;
	ImgInfo.m_Format = CImageInfo::FORMAT_RGBA;
	ImgInfo.m_pData = m_vBuffer.data();
	m_Texture = m_pGraphics->LoadTextureRaw(ImgInfo, 0, "loading_video");
	if(!m_Texture.IsValid())
	{
		Close();
		return false;
	}

	m_LastVideoPts = AV_NOPTS_VALUE;
	m_NextFrameTime = std::chrono::nanoseconds::zero();
	m_IsPlaying = true;
	return UpdateFrame(true);
}

bool CVideoLoader::UploadFrame(int DurationMs)
{
	if(m_pGraphics == nullptr || !m_Texture.IsValid())
		return false;

	CImageInfo ImgInfo;
	ImgInfo.m_Width = m_Width;
	ImgInfo.m_Height = m_Height;
	ImgInfo.m_Format = CImageInfo::FORMAT_RGBA;
	ImgInfo.m_pData = m_vBuffer.data();
	if(!m_pGraphics->UpdateTextureRaw(m_Texture, 0, 0, m_Width, m_Height, ImgInfo, false))
		return false;

	m_LastVideoPts = m_pFrame->best_effort_timestamp;
	m_NextFrameTime = time_get_nanoseconds() + std::chrono::milliseconds(std::clamp(DurationMs, 1, VIDEO_LOADER_MAX_FRAME_MS));
	return true;
}

bool CVideoLoader::UpdateFrame(bool Force)
{
	if(!m_IsPlaying || m_pFormatCtx == nullptr || m_pCodecCtx == nullptr || m_pFrame == nullptr || m_pFrameRGB == nullptr || m_pSwsCtx == nullptr)
		return false;

	if(!Force && m_NextFrameTime != std::chrono::nanoseconds::zero() && time_get_nanoseconds() < m_NextFrameTime)
		return true;

	while(true)
	{
		while(avcodec_receive_frame(m_pCodecCtx, m_pFrame) == 0)
		{
			sws_scale(m_pSwsCtx, m_pFrame->data, m_pFrame->linesize, 0, m_Height, m_pFrameRGB->data, m_pFrameRGB->linesize);
			int DurationMs = VIDEO_LOADER_DEFAULT_FRAME_MS;
			int64_t DurationTs = m_pFrame->duration;
			if(DurationTs <= 0 && m_LastVideoPts != AV_NOPTS_VALUE && m_pFrame->best_effort_timestamp != AV_NOPTS_VALUE)
				DurationTs = m_pFrame->best_effort_timestamp - m_LastVideoPts;
			if(DurationTs > 0)
			{
				const int64_t Rescaled = av_rescale_q(DurationTs, m_pFormatCtx->streams[m_VideoStream]->time_base, AVRational{1, 1000});
				if(Rescaled > 0)
					DurationMs = (int)Rescaled;
			}
			return UploadFrame(DurationMs);
		}

		AVPacket Packet;
		av_init_packet(&Packet);
		const int ReadResult = av_read_frame(m_pFormatCtx, &Packet);
		if(ReadResult < 0)
		{
			av_seek_frame(m_pFormatCtx, m_VideoStream, 0, AVSEEK_FLAG_BACKWARD);
			avcodec_flush_buffers(m_pCodecCtx);
			m_LastVideoPts = AV_NOPTS_VALUE;
			return true;
		}

		if(Packet.stream_index == m_VideoStream)
			avcodec_send_packet(m_pCodecCtx, &Packet);
		av_packet_unref(&Packet);
	}
}

void CVideoLoader::Update()
{
	if(!m_IsPlaying)
		return;
	UpdateFrame(false);
}

void CVideoLoader::Render(float x, float y, float w, float h)
{
	if(!m_Texture.IsValid())
		return;
	m_pGraphics->TextureSet(m_Texture);
	m_pGraphics->QuadsBegin();
	m_pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	IGraphics::CQuadItem QuadItem(x, y, w, h);
	m_pGraphics->QuadsDrawTL(&QuadItem, 1);
	m_pGraphics->QuadsEnd();
}

void CVideoLoader::Close()
{
	if(m_Texture.IsValid() && m_pGraphics != nullptr)
		m_pGraphics->UnloadTexture(&m_Texture);
	if(m_pFrameRGB)
		av_frame_free(&m_pFrameRGB);
	if(m_pFrame)
		av_frame_free(&m_pFrame);
	if(m_pSwsCtx)
		sws_freeContext(m_pSwsCtx);
	if(m_pCodecCtx)
		avcodec_free_context(&m_pCodecCtx);
	if(m_pFormatCtx)
		avformat_close_input(&m_pFormatCtx);

	m_vBuffer.clear();
	m_VideoStream = -1;
	m_Width = 0;
	m_Height = 0;
	m_LastVideoPts = AV_NOPTS_VALUE;
	m_NextFrameTime = std::chrono::nanoseconds::zero();
	m_IsPlaying = false;
}
