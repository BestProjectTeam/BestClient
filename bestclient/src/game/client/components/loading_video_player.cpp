#include "loading_video_player.h"

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/storage.h>
#include <engine/shared/config.h>
#include <base/system.h>
#include <cmath>
#include <algorithm>

// format cubic-bezier to ease values using Newton-Raphson method
class CCubicBezier
{
private:
	float m_X1, m_Y1, m_X2, m_Y2;
	
	float SampleCurve(float t, float p1, float p2) const
	{
		float t2 = t * t;
		float t3 = t2 * t;
		float mt = 1.0f - t;
		float mt2 = mt * mt;
		return 3.0f * mt2 * t * p1 + 3.0f * mt * t2 * p2 + t3;
	}

	float SolveForT(float x) const
	{
		if(x <= 0.0f) return 0.0f;
		if(x >= 1.0f) return 1.0f;
		
		float t = x;
		for(int i = 0; i < 8; i++)
		{
			float currentX = SampleCurve(t, m_X1, m_X2);
			float currentSlope = (SampleCurve(t + 0.001f, m_X1, m_X2) - currentX) / 0.001f;
			if(std::abs(currentSlope) < 0.000001f) break;
			t -= (currentX - x) / currentSlope;
		}
		return std::clamp(t, 0.0f, 1.0f);
	}
	
public:
	CCubicBezier(float x1, float y1, float x2, float y2) : m_X1(x1), m_Y1(y1), m_X2(x2), m_Y2(y2) {}
	
	float Ease(float t) const
	{
		if(t <= 0.0f) return 0.0f;
		if(t >= 1.0f) return 1.0f;
		float solvedT = SolveForT(t);
		return SampleCurve(solvedT, m_Y1, m_Y2);
	}
};

// Presets for easings
static const CCubicBezier EASE_OUT_CUBIC(0.22f, 1.0f, 0.36f, 1.0f);     // cubic-bezier(0.22, 1, 0.36, 1)
static const CCubicBezier EASE_IN_OUT_CUBIC(0.65f, 0.0f, 0.35f, 1.0f);  // cubic-bezier(0.65, 0, 0.35, 1)
static const CCubicBezier EASE_OUT_EXPO(0.16f, 1.0f, 0.3f, 1.0f);       // cubic-bezier(0.16, 1, 0.3, 1)
static const CCubicBezier EASE_IN_OUT_EXPO(0.87f, 0.0f, 0.13f, 1.0f);   // cubic-bezier(0.87, 0, 0.13, 1)
static const CCubicBezier EASE_OUT_BACK(0.34f, 1.56f, 0.64f, 1.0f);     // cubic-bezier(0.34, 1.56, 0.64, 1)

CLoadingVideoPlayer::CLoadingVideoPlayer() :
	m_pGraphics(nullptr),
	m_pStorage(nullptr),
	m_LogoPos(vec2(0, 0)),
	m_LogoWidth(200.0f),
	m_LogoHeight(200.0f),
	m_TitleMainWidth(450.0f),
	m_TitleMainHeight(126.0f),
	m_TitleSubWidth(450.0f),
	m_TitleSubHeight(87.0f),
	m_TargetRotationPhase1(pi), // 180 degrees
	m_TargetRotationPhase2(2.0f * pi),
	m_TargetRotationPhase4(2.0f * pi + 2.0f * pi * 365.0f / 360.0f),
    m_FinalLogoRotationOffset(0.0f),
    m_FinalLogoPos(vec2(0, 0)),
	m_FinalLogoSize(200.0f),
	m_FinalTitlePos(vec2(0, 0)),
	m_HasFinalTargets(false),
	m_Alpha(1.0f),
	m_StartTime(std::chrono::nanoseconds(0)),
	m_IsPlaying(false),
	m_IsLoaded(false)
{
}

CLoadingVideoPlayer::~CLoadingVideoPlayer()
{
	m_pGraphics = nullptr;
	m_pStorage = nullptr;
}

void CLoadingVideoPlayer::SetFinalTargets(vec2 LogoCenterPos, float LogoSize, vec2 TitleTopLeftPos, float FinalRotationOffset)
{
	m_FinalLogoPos = LogoCenterPos;
	m_FinalLogoSize = LogoSize;
	m_FinalTitlePos = TitleTopLeftPos;
	m_FinalLogoRotationOffset = FinalRotationOffset;
	m_HasFinalTargets = true;

	dbg_msg("loading", "Final targets: Logo(%.1f,%.1f) size %.1f, Title(%.1f,%.1f), Rotation offset %.1f degrees",
		LogoCenterPos.x, LogoCenterPos.y, LogoSize, TitleTopLeftPos.x, TitleTopLeftPos.y, FinalRotationOffset);
}

void CLoadingVideoPlayer::Init(IGraphics *pGraphics, IStorage *pStorage)
{
	m_pGraphics = pGraphics;
	m_pStorage = pStorage;
	
	// Load logo
	dbg_msg("loading", "Loading gui_logo.png...");
	m_LogoTexture = m_pGraphics->LoadTexture("gui_logo.png", IStorage::TYPE_ALL);
	if(!m_LogoTexture.IsValid())
	{
		dbg_msg("loading", "ERROR: gui_logo.png not found!");
		return;
	}
	
	// Load main title "BEST"
	dbg_msg("loading", "Loading best_titlemain.png...");
	m_TitleMainTexture = m_pGraphics->LoadTexture("best_titlemain.png", IStorage::TYPE_ALL);
	if(!m_TitleMainTexture.IsValid())
	{
		dbg_msg("loading", "ERROR: best_titlemain.png not found!");
		return;
	}
	
	// Load subtitle "client"
	dbg_msg("loading", "Loading best_titlesub.png...");
	m_TitleSubTexture = m_pGraphics->LoadTexture("best_titlesub.png", IStorage::TYPE_ALL);
	if(!m_TitleSubTexture.IsValid())
	{
		dbg_msg("loading", "ERROR: best_titlesub.png not found!");
		return;
	}
	
	m_IsLoaded = true;
	dbg_msg("loading", "Animation assets loaded successfully!");
}

void CLoadingVideoPlayer::Update(std::chrono::nanoseconds CurrentTime)
{
	if(!m_IsLoaded || !m_IsPlaying)
		return;
	
	if(m_StartTime.count() == 0)
	{
		m_StartTime = CurrentTime;
		dbg_msg("loading", "Animation started!");
	}

	float Elapsed = std::chrono::duration_cast<std::chrono::duration<float>>(CurrentTime - m_StartTime).count();
	
	// some notes on the animation phases
	// Phase 1: Logo movement
	// Phase 2: Logo move + title reveal
	// Phase 3: Hold
	// Phase 4: Transition to final positions
	// Phase 5: Fade out
	
	float HoldEndTime = PHASE1_DURATION + PHASE2_DURATION + HOLD_DURATION;
	float TransitionEndTime = HoldEndTime + TRANSITION_DURATION;
	
	if(Elapsed >= TransitionEndTime)
	{
		// Phase 5
		float FadeProgress = (Elapsed - TransitionEndTime) / FADE_OUT_DURATION;
		FadeProgress = std::clamp(FadeProgress, 0.0f, 1.0f);
		FadeProgress = FadeProgress == 0.0f ? 0.0f : std::pow(2.0f, 10.0f * FadeProgress - 10.0f);
		m_Alpha = 1.0f - FadeProgress;
		
		if(m_Alpha <= 0.0f)
		{
			m_Alpha = 0.0f;
			m_IsPlaying = false;
			dbg_msg("loading", "Animation finished!");
		}
	}
	else if(Elapsed >= HoldEndTime)
	{
		// Phase 4
		m_Alpha = 1.0f;
	}
	else if(Elapsed >= PHASE1_DURATION + PHASE2_DURATION)
	{
		// Phase 3
		m_Alpha = 1.0f;
	}
}

void CLoadingVideoPlayer::Render(float ScreenWidth, float ScreenHeight)
{
	if(!m_pGraphics || !m_IsLoaded || !m_IsPlaying)
		return;
	
	if(m_Alpha <= 0.0f)
		return;

	float Elapsed = std::chrono::duration_cast<std::chrono::duration<float>>(
		time_get_nanoseconds() - m_StartTime).count();

    auto mix = [](float a, float b, float t) -> float {
        return a * (1.0f - t) + b * t;
    };

	// Define positions
	vec2 BottomLeft = vec2(-m_LogoWidth, ScreenHeight + m_LogoHeight);
	vec2 CenterRight = vec2(ScreenWidth * 0.76f, ScreenHeight * 0.5f);
	vec2 CenterLeft = vec2(ScreenWidth * 0.35f, ScreenHeight * 0.5f);

	float TitleStartTime = PHASE1_DURATION + 0.05f;
	float TitleDuration = 1.6f;
	float TitleEndTime = TitleStartTime + TitleDuration;

	const float HoldEnd = PHASE1_DURATION + PHASE2_DURATION + HOLD_DURATION;
	const float TransitionEnd = HoldEnd + TRANSITION_DURATION;

    float TitleMainX, TitleMainY;
    float TitleSubX, TitleSubY;
    float TitleSpacing = 10.0f;

    float CurrentTime = (float)(time_get() / (double)time_freq());
    float GhostRotation = fmod((CurrentTime - m_GhostRotationStartTime) * 0.15f + m_GhostRotationOffset, 2.0f * pi);

	// Phase 1
	if(Elapsed < PHASE1_DURATION)
	{
		float t = Elapsed / PHASE1_DURATION;
		t = EASE_OUT_EXPO.Ease(t);
        
        TitleMainX = ScreenWidth / 2.0f - m_TitleMainWidth / 2.0f + 85.0f;
        TitleMainY = (ScreenHeight / 2.0f - m_TitleMainHeight / 2.0f) - 30.0f;
	}
	// Phase 2
	else if(Elapsed < PHASE1_DURATION + PHASE2_DURATION)
	{
		float t = (Elapsed - PHASE1_DURATION) / PHASE2_DURATION;
		t = EASE_OUT_EXPO.Ease(t);

        TitleMainX = ScreenWidth / 2.0f - m_TitleMainWidth / 2.0f + 85.0f;
        TitleMainY = (ScreenHeight / 2.0f - m_TitleMainHeight / 2.0f) - 30.0f;
		
	}
	// Phase 3
	else if(Elapsed < HoldEnd)
	{
        TitleMainX = ScreenWidth / 2.0f - m_TitleMainWidth / 2.0f + 85.0f;
        TitleMainY = (ScreenHeight / 2.0f - m_TitleMainHeight / 2.0f) - 30.0f;
	}
	// Phase 4
	else if(Elapsed < TransitionEnd && m_HasFinalTargets)
	{
		float t = (Elapsed - HoldEnd) / TRANSITION_DURATION;
		t = EASE_IN_OUT_EXPO.Ease(t);

        float startMainX = ScreenWidth / 2.0f - m_TitleMainWidth / 2.0f + 85.0f;
        float startMainY = (ScreenHeight / 2.0f - m_TitleMainHeight / 2.0f) - 30.0f;

        TitleMainX = startMainX + (m_FinalTitlePos.x - startMainX) * t;
        TitleMainY = startMainY + (m_FinalTitlePos.y - startMainY) * t;
	}
	else
	{
		// Phase 5
        TitleMainX = m_FinalTitlePos.x;
        TitleMainY = m_FinalTitlePos.y;
	}

	if(Elapsed < HoldEnd)
	{
		TitleMainX = ScreenWidth / 2.0f - m_TitleMainWidth / 2.0f + 85.0f;
		TitleMainY = (ScreenHeight / 2.0f - m_TitleMainHeight / 2.0f) - 30.0f;
	}
	else if(Elapsed < TransitionEnd && m_HasFinalTargets)
	{
		float t = (Elapsed - HoldEnd) / TRANSITION_DURATION;
		t = EASE_IN_OUT_CUBIC.Ease(t);

		float startTitleMainX = ScreenWidth / 2.0f - m_TitleMainWidth / 2.0f + 85.0f;
		float startTitleMainY = (ScreenHeight / 2.0f - m_TitleMainHeight / 2.0f) - 30.0f;

		TitleMainX = startTitleMainX + (m_FinalTitlePos.x - startTitleMainX) * t;
		TitleMainY = startTitleMainY + (m_FinalTitlePos.y - startTitleMainY) * t;
	}
	else
	{
		TitleMainX = m_FinalTitlePos.x;
		TitleMainY = m_FinalTitlePos.y;
    }

    TitleSubX = TitleMainX + (m_TitleMainWidth - m_TitleSubWidth) / 2.0f;
    TitleSubY = TitleMainY + m_TitleMainHeight + TitleSpacing - 40.0f;

	m_pGraphics->TextureClear();
	m_pGraphics->QuadsBegin();
	m_pGraphics->SetColor(0x15/255.0f, 0x15/255.0f, 0x15/255.0f, m_Alpha);
	IGraphics::CQuadItem Background(0, 0, ScreenWidth, ScreenHeight);
	m_pGraphics->QuadsDrawTL(&Background, 1);
	m_pGraphics->QuadsEnd();
	
	float AdjustedRevealProgress = 0.0f;
	
    if(Elapsed < PHASE1_DURATION)
    {
        // Phase 1: 0° → 180° (FAST)
        float t = Elapsed / PHASE1_DURATION;
        t = EASE_OUT_EXPO.Ease(t);
        m_LogoPos = BottomLeft + (CenterRight - BottomLeft) * t;
        m_LogoRotation = mix(0.0f, m_TargetRotationPhase1, t);
        m_LogoWidth = m_LogoHeight = 200.0f;
    }
    else if(Elapsed < PHASE1_DURATION + PHASE2_DURATION)
    {
        float t = (Elapsed - PHASE1_DURATION) / PHASE2_DURATION;
        t = EASE_OUT_EXPO.Ease(t);
        
        m_LogoPos = CenterRight + (CenterLeft - CenterRight) * t;
        m_LogoRotation = m_TargetRotationPhase1 + (m_TargetRotationPhase2 - m_TargetRotationPhase1) * t;
        
        m_LogoWidth = m_LogoHeight = 200.0f;
    }
    else if(Elapsed < HoldEnd)
    {
        m_LogoPos = CenterLeft;
        m_LogoRotation = m_TargetRotationPhase1 + pi / 2.0f;
        m_LogoWidth = m_LogoHeight = 200.0f;
    }
    else if(Elapsed < TransitionEnd && m_HasFinalTargets)
    {
        float t = (Elapsed - HoldEnd) / TRANSITION_DURATION;
        t = EASE_IN_OUT_EXPO.Ease(t);
        
        m_LogoPos = CenterLeft + (m_FinalLogoPos - CenterLeft) * t;
        m_LogoWidth = 200.0f + (m_FinalLogoSize - 200.0f) * t;
        m_LogoHeight = m_LogoWidth;
        
        m_LogoRotation = mix(m_TargetRotationPhase1 + pi / 2.5f, GhostRotation, t);
    }
    else
    {
        // Phase 5: EXACT star position 
        if(m_HasFinalTargets)
        {
            m_LogoPos = m_FinalLogoPos;
            m_LogoWidth = m_FinalLogoSize;
            m_LogoHeight = m_FinalLogoSize;
            m_LogoRotation = GhostRotation;
        }
    }

	if(Elapsed >= TitleStartTime && Elapsed < TitleEndTime)
	{
		float t = (Elapsed - TitleStartTime) / TitleDuration;
		t = std::clamp(t, 0.0f, 1.0f);
		AdjustedRevealProgress = EASE_OUT_EXPO.Ease(t);
	}
	else if(Elapsed >= TitleEndTime)
	{
		AdjustedRevealProgress = 1.0f;
	}

	if(m_TitleMainTexture.IsValid() && AdjustedRevealProgress > 0.0f)
	{
		float RevealWidth = m_TitleMainWidth * AdjustedRevealProgress;
		float StartX = TitleMainX + (m_TitleMainWidth - RevealWidth);
		float TexCoordStart = 1.0f - AdjustedRevealProgress;
		
		m_pGraphics->TextureSet(m_TitleMainTexture);
		m_pGraphics->QuadsBegin();
		m_pGraphics->QuadsSetSubset(TexCoordStart, 0.0f, 1.0f, 1.0f);
		m_pGraphics->SetColor(1.0f, 1.0f, 1.0f, m_Alpha);
		IGraphics::CQuadItem TitleMain(StartX, TitleMainY, RevealWidth, m_TitleMainHeight);
		m_pGraphics->QuadsDrawTL(&TitleMain, 1);
		m_pGraphics->QuadsEnd();
	}

	if(m_TitleSubTexture.IsValid() && AdjustedRevealProgress > 0.0f)
	{
		float RevealWidth = m_TitleSubWidth * AdjustedRevealProgress;
		float StartX = TitleSubX + (m_TitleSubWidth - RevealWidth);
		
		float TexCoordStart = 1.0f - AdjustedRevealProgress;
		
		m_pGraphics->TextureSet(m_TitleSubTexture);
		m_pGraphics->QuadsBegin();
		m_pGraphics->QuadsSetSubset(TexCoordStart, 0.0f, 1.0f, 1.0f);
		m_pGraphics->SetColor(1.0f, 1.0f, 1.0f, m_Alpha);
		IGraphics::CQuadItem TitleSub(StartX, TitleSubY, RevealWidth, m_TitleSubHeight);
		m_pGraphics->QuadsDrawTL(&TitleSub, 1);
		m_pGraphics->QuadsEnd();
	}

    if(m_LogoTexture.IsValid())
    {
        float LogoCenterX = m_LogoPos.x;
        float LogoCenterY = m_LogoPos.y;
        
        m_pGraphics->TextureSet(m_LogoTexture);
        m_pGraphics->QuadsBegin();
        m_pGraphics->SetColor(1.0f, 1.0f, 1.0f, m_Alpha);
        
        m_pGraphics->QuadsSetRotation(m_LogoRotation);
        
        IGraphics::CQuadItem Logo(
            LogoCenterX - m_LogoWidth / 2.0f,
            LogoCenterY - m_LogoHeight / 2.0f,
            m_LogoWidth,
            m_LogoHeight
        );
        m_pGraphics->QuadsDrawTL(&Logo, 1);

        m_pGraphics->QuadsSetRotation(0.0f);
        m_pGraphics->QuadsEnd();
    }
}

void CLoadingVideoPlayer::Reset()
{
	m_Alpha = 1.0f;
	m_IsPlaying = false;
	m_StartTime = std::chrono::nanoseconds(0);
	m_LogoPos = vec2(0, 0);
}

void CLoadingVideoPlayer::StartPlayback()
{
	if(!g_Config.m_BcLoadscreenToggle || !m_IsLoaded)
		return;
	
	m_StartTime = std::chrono::nanoseconds(0);
	m_IsPlaying = true;
	m_Alpha = 1.0f;

    m_GhostRotationActive = true;
    m_GhostRotationStartTime = (float)(time_get() / (double)time_freq());
    m_GhostRotationOffset = 0.0f;  // Will sync automatically
	
	dbg_msg("loading", "Starting loading animation!");
}
