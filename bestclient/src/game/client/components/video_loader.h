#ifndef GAME_CLIENT_COMPONENTS_VIDEO_LOADER_H
#define GAME_CLIENT_COMPONENTS_VIDEO_LOADER_H

#include <engine/graphics.h>
#include <engine/storage.h>

#include <chrono>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

class CVideoLoader
{
private:
    AVFormatContext *m_pFormatCtx = nullptr;
    AVCodecContext *m_pCodecCtx = nullptr;
    AVFrame *m_pFrame = nullptr;
    AVFrame *m_pFrameRGB = nullptr;
    struct SwsContext *m_pSwsCtx = nullptr;
    std::vector<uint8_t> m_vBuffer;
    
    int m_VideoStream = -1;
    IGraphics::CTextureHandle m_Texture;
    
    IGraphics *m_pGraphics;
    IStorage *m_pStorage;
    
    int m_Width = 0;
    int m_Height = 0;
    bool m_IsPlaying = false;
    int64_t m_LastVideoPts = AV_NOPTS_VALUE;
    std::chrono::nanoseconds m_NextFrameTime{0};

    bool UpdateFrame(bool Force);
    bool UploadFrame(int DurationMs);
    
public:
    void Init(IGraphics *pGraphics, IStorage *pStorage);
    bool LoadVideo(const char *pFilename);
    void Update();
    void Render(float x, float y, float w, float h);
    void Close();
    
    bool IsPlaying() const { return m_IsPlaying; }
};

#endif
