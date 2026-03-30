#ifndef GAME_CLIENT_COMPONENTS_LOADING_VIDEO_PLAYER_H
#define GAME_CLIENT_COMPONENTS_LOADING_VIDEO_PLAYER_H

#include <base/vmath.h>
#include <engine/graphics.h>
#include <engine/storage.h>
#include <chrono>

class CLoadingVideoPlayer
{
private:
	IGraphics *m_pGraphics;
	IStorage *m_pStorage;
	
	IGraphics::CTextureHandle m_LogoTexture;
	IGraphics::CTextureHandle m_TitleMainTexture;
	IGraphics::CTextureHandle m_TitleSubTexture;
	
	vec2 m_LogoPos;
	float m_LogoWidth;
	float m_LogoHeight;
	float m_TitleMainWidth;
	float m_TitleMainHeight;
	float m_TitleSubWidth;
	float m_TitleSubHeight;
    float m_LogoRotation;
	float m_TargetRotationPhase1;
	float m_TargetRotationPhase2; 
	float m_TargetRotationPhase4;
    float m_FinalLogoRotationOffset;
    bool m_GhostRotationActive;
    float m_GhostRotationStartTime;
    float m_GhostRotationOffset;

	// Final target positions (set from menus_start)
    vec2 m_FinalLogoPos;
    float m_FinalLogoSize;
    vec2 m_FinalTitlePos;
    bool m_HasFinalTargets;
	
	float m_Alpha;
	std::chrono::nanoseconds m_StartTime;
	bool m_IsPlaying;
	bool m_IsLoaded;
	bool m_HasFinalPositions;
	
	static constexpr float PHASE1_DURATION = 1.0f;      // Logo moves to center-right
	static constexpr float PHASE2_DURATION = 2.0f;      // Logo sweeps left + titles reveal
	static constexpr float HOLD_DURATION = 1.0f;        // Hold at 100% for 5 seconds
    static constexpr float TRANSITION_DURATION = 1.2f;
	static constexpr float FADE_OUT_DURATION = 1.0f;    // Fade to 0%

public:
	CLoadingVideoPlayer();
	~CLoadingVideoPlayer();
	
	void Init(IGraphics *pGraphics, IStorage *pStorage);
	void Update(std::chrono::nanoseconds CurrentTime);
	void Render(float ScreenWidth, float ScreenHeight);
	void Reset();
	void StartPlayback();

    void SetFinalTargets(vec2 LogoCenterPos, float LogoSize, vec2 TitleTopLeftPos, float FinalRotationOffset = 0.0f);
	
	bool IsVisible() const { return m_IsPlaying && m_Alpha > 0.0f; }
	bool IsLoaded() const { return m_IsLoaded; }
    bool IsFinished() const { return !m_IsPlaying; }
};

#endif
