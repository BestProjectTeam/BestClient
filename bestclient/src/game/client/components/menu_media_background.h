#ifndef GAME_CLIENT_COMPONENTS_MENU_MEDIA_BACKGROUND_H
#define GAME_CLIENT_COMPONENTS_MENU_MEDIA_BACKGROUND_H

#include "media_decoder.h"
#include "bestclient/subsystem_runtime.h"

#include <base/system.h>

#include <engine/graphics.h>
#include <engine/storage.h>

#include <chrono>
#include <cstdint>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

class CMenuMediaBackground
{
	IGraphics *m_pGraphics = nullptr;
	IStorage *m_pStorage = nullptr;

	std::vector<SMediaFrame> m_vFrames;
	bool m_Animated = false;
	int m_Width = 0;
	int m_Height = 0;
	int64_t m_AnimationStart = 0;

	bool m_IsVideo = false;
	bool m_IsLoaded = false;
	bool m_HasError = false;
	ESubsystemRuntimeState m_RuntimeState = ESubsystemRuntimeState::DISABLED;
	char m_aStatus[128] = "";
	char m_aLoadedPath[IO_MAX_PATH_LENGTH] = "";
	int m_LastConfigEnabled = -1;
	char m_aLastConfigPath[IO_MAX_PATH_LENGTH] = "";

	AVFormatContext *m_pFormatCtx = nullptr;
	AVCodecContext *m_pCodecCtx = nullptr;
	AVFrame *m_pFrame = nullptr;
	AVFrame *m_pFrameRgba = nullptr;
	AVPacket *m_pPacket = nullptr;
	SwsContext *m_pSwsCtx = nullptr;
	int m_VideoStream = -1;
	int64_t m_LastVideoPts = AV_NOPTS_VALUE;
	IGraphics::CTextureHandle m_VideoTexture;
	std::chrono::nanoseconds m_NextFrameTime{0};
	std::vector<uint8_t> m_vVideoUploadBuffer;
	int64_t m_LastDecodeStepTick = 0;
	int64_t m_LastPerfReportTick = 0;
	int64_t m_LastUpdateCostTick = 0;
	int64_t m_MaxUpdateCostTick = 0;
	int64_t m_TotalUpdateCostTick = 0;
	int64_t m_UpdateSamples = 0;

public:
	struct SRenderContext
	{
		float m_CameraCenterX = 0.0f;
		float m_CameraCenterY = 0.0f;
		float m_ViewWidth = 0.0f;
		float m_ViewHeight = 0.0f;
		float m_MapWidth = 0.0f;
		float m_MapHeight = 0.0f;
		float m_WorldOffset = 0.0f;
	};

private:

	void SetStatus(const char *pText);
	void SetError(const char *pText);
	void ClearVideoState();
	bool LoadStaticMedia(const char *pPath, int StorageType);
	bool LoadVideo(const char *pPath, int StorageType);
	bool DecodeNextVideoFrame(bool LoopOnEof);
	bool UploadCurrentVideoFrame(const char *pContextName, int DurationMs);
	void RenderTextureCover(IGraphics::CTextureHandle Texture, int Width, int Height, float TargetCenterX, float TargetCenterY, float TargetWidth, float TargetHeight);

public:
	~CMenuMediaBackground();

	void Init(IGraphics *pGraphics, IStorage *pStorage);
	void SyncFromConfig(int Enabled, const char *pPath);
	void ReloadFromConfig(int Enabled, const char *pPath);
	void SyncFromConfig();
	void ReloadFromConfig();
	void Shutdown();
	void Unload();
	void Update();
	bool Render(float ScreenWidth, float ScreenHeight, const SRenderContext *pRenderContext = nullptr);

	bool HasError() const { return m_HasError; }
	bool IsLoaded() const { return m_IsLoaded; }
	const char *StatusText() const { return m_aStatus; }
};

#endif
