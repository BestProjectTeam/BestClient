/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_UI_ANIMATIONS_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_UI_ANIMATIONS_H

#include <base/color.h>
#include <base/vmath.h>

#include <engine/shared/config.h>
#include <engine/shared/protocol.h>
#include <engine/textrender.h>

#include <game/client/component.h>
#include <game/client/lineinput.h>
#include <game/client/ui_rect.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace BCUiAnimations
{
inline bool Enabled()
{
	return g_Config.m_BcAnimations != 0;
}

inline bool SettingsUiAnimationEnabled()
{
	return Enabled() && g_Config.m_BcModuleUiRevealAnimation != 0;
}

inline float Clamp01(float v)
{
	return std::clamp(v, 0.0f, 1.0f);
}

inline float MsToSeconds(int Ms)
{
	return std::max(0, Ms) / 1000.0f;
}

inline float EaseInOutQuad(float t)
{
	t = Clamp01(t);
	if(t < 0.5f)
		return 2.0f * t * t;
	return 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
}

inline float EaseOutCubic(float t)
{
	t = Clamp01(t);
	const float Inv = 1.0f - t;
	return 1.0f - Inv * Inv * Inv;
}

inline float UpdatePhase(float &Phase, float Target, float Dt, float DurationSeconds)
{
	Target = Clamp01(Target);
	if(DurationSeconds <= 0.0f || Dt <= 0.0f)
	{
		Phase = Target;
		return Phase;
	}

	const float Speed = 1.0f / DurationSeconds;
	if(Phase < Target)
		Phase = std::min(Target, Phase + Dt * Speed);
	else if(Phase > Target)
		Phase = std::max(Target, Phase - Dt * Speed);
	return Phase;
}

inline void UpdateModuleRevealPhase(float &Phase, bool Expanded, float Dt)
{
	if(SettingsUiAnimationEnabled())
		UpdatePhase(Phase, Expanded ? 1.0f : 0.0f, Dt, MsToSeconds(g_Config.m_BcModuleUiRevealAnimationMs));
	else
		Phase = Expanded ? 1.0f : 0.0f;
}

inline float ModuleReveal(float &Phase, bool Expanded, float TargetHeight, float Dt)
{
	UpdateModuleRevealPhase(Phase, Expanded, Dt);
	return TargetHeight * EaseOutCubic(Phase);
}

inline float ModuleReveal(float &Phase, bool Expanded, float TargetHeight, float Dt, float DurationSeconds)
{
	if(SettingsUiAnimationEnabled())
		UpdatePhase(Phase, Expanded ? 1.0f : 0.0f, Dt, DurationSeconds);
	else
		Phase = Expanded ? 1.0f : 0.0f;
	return TargetHeight * EaseOutCubic(Phase);
}

struct STabSwitch
{
	float m_Progress = 1.0f;
	int m_FromIndex = 0;
	int m_ToIndex = 0;
};

inline STabSwitch AnimateTabSwitch(int SelectedIndex, int Count, float DeltaTime, int Channel = 0)
{
	STabSwitch Result;
	if(Count <= 0)
		return Result;

	struct STabSwitchAnimState
	{
		bool m_Initialized = false;
		int m_FromTab = 0;
		int m_ToTab = 0;
		float m_Progress = 1.0f;
	};
	static STabSwitchAnimState s_aAnimState[2];
	Channel = std::clamp(Channel, 0, 1);
	STabSwitchAnimState &AnimState = s_aAnimState[Channel];

	SelectedIndex = std::clamp(SelectedIndex, 0, Count - 1);
	if(!AnimState.m_Initialized)
	{
		AnimState.m_Initialized = true;
		AnimState.m_FromTab = SelectedIndex;
		AnimState.m_ToTab = SelectedIndex;
		AnimState.m_Progress = 1.0f;
	}
	if(SelectedIndex != AnimState.m_ToTab)
	{
		AnimState.m_FromTab = AnimState.m_ToTab;
		AnimState.m_ToTab = SelectedIndex;
		AnimState.m_Progress = 0.0f;
	}
	AnimState.m_FromTab = std::clamp(AnimState.m_FromTab, 0, Count - 1);
	AnimState.m_ToTab = std::clamp(AnimState.m_ToTab, 0, Count - 1);

	if(!SettingsUiAnimationEnabled())
	{
		AnimState.m_FromTab = SelectedIndex;
		AnimState.m_ToTab = SelectedIndex;
		AnimState.m_Progress = 1.0f;
	}
	else if(AnimState.m_Progress < 1.0f)
	{
		const float Step = std::clamp(DeltaTime * 12.0f, 0.0f, 1.0f);
		AnimState.m_Progress += (1.0f - AnimState.m_Progress) * Step;
		if(AnimState.m_Progress > 0.995f)
			AnimState.m_Progress = 1.0f;
	}

	Result.m_Progress = AnimState.m_Progress;
	Result.m_FromIndex = AnimState.m_FromTab;
	Result.m_ToIndex = AnimState.m_ToTab;
	return Result;
}

inline void TabSwitchContentOffsets(float Progress, int FromIndex, int ToIndex, float Width, float &FromOffset, float &ToOffset)
{
	const float Eased = EaseOutCubic(Progress);
	const float SlideDistance = Width + 6.0f;
	const int Direction = ToIndex >= FromIndex ? 1 : -1;
	FromOffset = -Direction * Eased * SlideDistance;
	ToOffset = Direction * (1.0f - Eased) * SlideDistance;
}

inline ColorRGBA MultiplyAlpha(ColorRGBA Color, float AlphaMul)
{
	Color.a *= AlphaMul;
	return Color;
}
} // namespace BCUiAnimations

#define BC_MODULE_REVEAL(Expanded, TargetHeight, Dt) \
	BCUiAnimations::ModuleReveal([]() -> float & { static float Phase = 0.0f; return Phase; }(), (Expanded), (TargetHeight), (Dt))

#define BC_MODULE_REVEAL_DUR(Expanded, TargetHeight, Dt, DurationSeconds) \
	BCUiAnimations::ModuleReveal([]() -> float & { static float Phase = 0.0f; return Phase; }(), (Expanded), (TargetHeight), (Dt), (DurationSeconds))

class CUi;

class CBcUiAnimations : public CComponent
{
	static constexpr int CHAT_TYPING_ANIM_MAX_TEXT_BYTES = 16;
	static constexpr int MAIN_MENU_BUTTON_COUNT = 6;

	struct STypingGlyphAnim
	{
		int64_t m_StartTime = 0;
		int m_ByteIndex = 0;
		int m_ByteLength = 0;
		char m_aText[16] = "";
	};

	char m_aPreviousDisplayedInputText[MAX_CHAT_LENGTH] = "";
	int64_t m_ChatOpenAnimationStart = 0;
	std::vector<STypingGlyphAnim> m_vTypingGlyphAnims;
	vec2 m_CaretVisualPos = vec2(0.0f, 0.0f);
	vec2 m_CaretAnimFromPos = vec2(0.0f, 0.0f);
	vec2 m_CaretAnimTargetPos = vec2(0.0f, 0.0f);
	int64_t m_CaretAnimStartTime = 0;
	bool m_CaretAnimValid = false;
	int64_t m_CaretBlinkAnchor = 0;

	float m_aMainMenuButtonScale[MAIN_MENU_BUTTON_COUNT] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

	static bool TypingAnimSupportsText(const char *pText);
	void ResetTypingAnimation();

public:
	int Sizeof() const override { return sizeof(*this); }

	void OnChatReset();
	void OnChatEnable();
	void OnChatDisable();
	void SyncTypingBaseline(CLineInput &Input);
	void RefreshTypingAnimation(CLineInput &Input, bool ChatOpen, bool HasComposition);

	float ChatOpenOffsetY(float ScreenHeight, bool ChatOpen) const;
	float ChatMessageYOffset(int64_t LineTime, int64_t Now) const;
	CUIRect ChatInputClipRect(const CUIRect &ClippingRect, float ScreenWidth) const;
	STextBoundingBox RenderChatTypingInput(CLineInput &Input, const CUIRect &InputCursorRect, float FontSize, float MessageMaxWidth, bool Changed);

	float KillfeedSlideOffsetX(ITextRender *pTextRender, int64_t AppearTime, int64_t Now, const STextContainerIndex &VictimText, const STextContainerIndex &KillerText, int TeamSize, int FirstVictimId, int KillerId) const;

	static CUIRect ScaleMenuButtonRect(const CUIRect &Base, float Scale);
	void UpdateMainMenuButtonScales(const CUIRect *pButtons, int Count, CUi *pUi, float FrameTime);
	float MainMenuButtonScale(int Index) const;
};

#endif
