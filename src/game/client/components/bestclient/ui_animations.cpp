/* Copyright © 2026 BestProject Team */
#include "ui_animations.h"

#include <base/str.h>
#include <base/time.h>

#include <engine/graphics.h>
#include <engine/textrender.h>

#include <game/client/ui.h>

bool CBcUiAnimations::TypingAnimSupportsText(const char *pText)
{
	for(const char *pScan = pText; *pScan;)
	{
		const char *pBefore = pScan;
		const int Codepoint = str_utf8_decode(&pScan);
		if(Codepoint < 0)
			return false;
		if(pScan <= pBefore)
			return false;
	}
	return true;
}

void CBcUiAnimations::ResetTypingAnimation()
{
	m_vTypingGlyphAnims.clear();
	m_CaretAnimValid = false;
}

void CBcUiAnimations::OnChatReset()
{
	m_aPreviousDisplayedInputText[0] = '\0';
	m_ChatOpenAnimationStart = 0;
	m_vTypingGlyphAnims.clear();
	m_CaretAnimValid = false;
}

void CBcUiAnimations::OnChatEnable()
{
	m_ChatOpenAnimationStart = time_get();
	ResetTypingAnimation();
}

void CBcUiAnimations::OnChatDisable()
{
	m_ChatOpenAnimationStart = 0;
	ResetTypingAnimation();
	m_aPreviousDisplayedInputText[0] = '\0';
}

void CBcUiAnimations::SyncTypingBaseline(CLineInput &Input)
{
	ResetTypingAnimation();
	str_copy(m_aPreviousDisplayedInputText, Input.GetDisplayedString(), sizeof(m_aPreviousDisplayedInputText));
}

void CBcUiAnimations::RefreshTypingAnimation(CLineInput &Input, bool ChatOpen, bool HasComposition)
{
	if(!ChatOpen || !BCUiAnimations::Enabled() || g_Config.m_BcChatAnimation == 0 || g_Config.m_BcChatTypingAnimation == 0 || Input.HasSelection() || HasComposition)
	{
		SyncTypingBaseline(Input);
		return;
	}

	const char *pCurrent = Input.GetDisplayedString();
	const size_t CurrentLen = str_length(pCurrent);
	const size_t PreviousLen = str_length(m_aPreviousDisplayedInputText);

	if(!TypingAnimSupportsText(pCurrent) || !TypingAnimSupportsText(m_aPreviousDisplayedInputText))
	{
		SyncTypingBaseline(Input);
		return;
	}

	if(str_comp(pCurrent, m_aPreviousDisplayedInputText) == 0)
		return;

	if(CurrentLen == 0)
	{
		SyncTypingBaseline(Input);
		return;
	}

	size_t PrefixBytes = 0;
	{
		const char *pCurScan = pCurrent;
		const char *pPrevScan = m_aPreviousDisplayedInputText;
		while(*pCurScan && *pPrevScan)
		{
			const char *pCurBefore = pCurScan;
			const char *pPrevBefore = pPrevScan;
			const int CurCp = str_utf8_decode(&pCurScan);
			const int PrevCp = str_utf8_decode(&pPrevScan);
			if(CurCp != PrevCp)
			{
				pCurScan = pCurBefore;
				pPrevScan = pPrevBefore;
				break;
			}
			PrefixBytes = (size_t)(pCurScan - pCurrent);
		}
	}

	size_t SuffixBytesCur = 0;
	size_t SuffixBytesPrev = 0;
	{
		int CurCursor = (int)CurrentLen;
		int PrevCursor = (int)PreviousLen;
		while(CurCursor > (int)PrefixBytes && PrevCursor > (int)PrefixBytes)
		{
			const int CurBefore = CurCursor;
			const int PrevBefore = PrevCursor;
			CurCursor = str_utf8_rewind(pCurrent, CurCursor);
			PrevCursor = str_utf8_rewind(m_aPreviousDisplayedInputText, PrevCursor);

			const char *pCurCpPtr = pCurrent + CurCursor;
			const char *pPrevCpPtr = m_aPreviousDisplayedInputText + PrevCursor;
			const int CurCp = str_utf8_decode(&pCurCpPtr);
			const int PrevCp = str_utf8_decode(&pPrevCpPtr);
			if(CurCp != PrevCp)
			{
				CurCursor = CurBefore;
				PrevCursor = PrevBefore;
				break;
			}

			SuffixBytesCur = CurrentLen - (size_t)CurCursor;
			SuffixBytesPrev = PreviousLen - (size_t)PrevCursor;
		}
	}

	const size_t RemovedBytes = PreviousLen - PrefixBytes - SuffixBytesPrev;
	const size_t InsertedBytes = CurrentLen - PrefixBytes - SuffixBytesCur;
	const int EditOldEndByte = (int)(PrefixBytes + RemovedBytes);
	const int DeltaBytes = (int)InsertedBytes - (int)RemovedBytes;

	for(auto It = m_vTypingGlyphAnims.begin(); It != m_vTypingGlyphAnims.end();)
	{
		const int AnimEndByte = It->m_ByteIndex + It->m_ByteLength;
		if(It->m_ByteIndex >= (int)PrefixBytes && AnimEndByte <= EditOldEndByte)
		{
			It = m_vTypingGlyphAnims.erase(It);
			continue;
		}
		if(It->m_ByteIndex >= EditOldEndByte)
			It->m_ByteIndex += DeltaBytes;

		if(It->m_ByteIndex < 0 || It->m_ByteLength <= 0 || It->m_ByteIndex + It->m_ByteLength > (int)CurrentLen)
		{
			It = m_vTypingGlyphAnims.erase(It);
			continue;
		}

		if(str_length(It->m_aText) != It->m_ByteLength ||
			str_comp_num(It->m_aText, pCurrent + It->m_ByteIndex, It->m_ByteLength) != 0)
		{
			It = m_vTypingGlyphAnims.erase(It);
			continue;
		}
		++It;
	}

	if(InsertedBytes > 0)
	{
		for(int ByteIndex = (int)PrefixBytes; ByteIndex < (int)(PrefixBytes + InsertedBytes);)
		{
			const int NextByteIndex = str_utf8_forward(pCurrent, ByteIndex);
			const int GlyphBytes = std::min(NextByteIndex - ByteIndex, CHAT_TYPING_ANIM_MAX_TEXT_BYTES - 1);
			if(GlyphBytes > 0)
			{
				STypingGlyphAnim Anim;
				Anim.m_StartTime = time_get();
				Anim.m_ByteIndex = ByteIndex;
				Anim.m_ByteLength = GlyphBytes;
				str_truncate(Anim.m_aText, sizeof(Anim.m_aText), pCurrent + ByteIndex, GlyphBytes);
				m_vTypingGlyphAnims.push_back(Anim);
			}
			ByteIndex = NextByteIndex;
		}
	}

	str_copy(m_aPreviousDisplayedInputText, pCurrent, sizeof(m_aPreviousDisplayedInputText));
}

float CBcUiAnimations::ChatOpenOffsetY(float ScreenHeight, bool ChatOpen) const
{
	const bool Enabled = BCUiAnimations::Enabled() && g_Config.m_BcChatAnimation != 0 && g_Config.m_BcChatOpenAnimation != 0 && g_Config.m_BcChatOpenAnimationMs > 0;
	if(!ChatOpen || !Enabled || m_ChatOpenAnimationStart <= 0)
		return 0.0f;

	const float Dur = BCUiAnimations::MsToSeconds(g_Config.m_BcChatOpenAnimationMs);
	const float Age = (time_get() - m_ChatOpenAnimationStart) / (float)time_freq();
	const float Progress = Dur > 0.0f ? std::clamp(Age / Dur, 0.0f, 1.0f) : 1.0f;
	return ScreenHeight * (1.0f - BCUiAnimations::EaseOutCubic(Progress));
}

float CBcUiAnimations::ChatMessageYOffset(int64_t LineTime, int64_t Now) const
{
	if(!BCUiAnimations::Enabled() || g_Config.m_BcChatAnimation == 0 || g_Config.m_BcChatAnimationMs <= 0 || LineTime <= 0)
		return 0.0f;

	const float Dur = BCUiAnimations::MsToSeconds(g_Config.m_BcChatAnimationMs);
	const float Age = (Now - LineTime) / (float)time_freq();
	const float Progress = Dur > 0.0f ? std::clamp(Age / Dur, 0.0f, 1.0f) : 1.0f;
	return 42.0f * (1.0f - BCUiAnimations::EaseOutCubic(Progress));
}

CUIRect CBcUiAnimations::ChatInputClipRect(const CUIRect &ClippingRect, float ScreenWidth) const
{
	const float TypingTravel = 30.0f;
	return {0.0f, ClippingRect.y - TypingTravel, ScreenWidth, ClippingRect.h + TypingTravel};
}

STextBoundingBox CBcUiAnimations::RenderChatTypingInput(CLineInput &Input, const CUIRect &InputCursorRect, float FontSize, float MessageMaxWidth, bool Changed)
{
	const bool TypingEnabled = BCUiAnimations::Enabled() && g_Config.m_BcChatAnimation != 0 && g_Config.m_BcChatTypingAnimation != 0 && g_Config.m_BcChatTypingAnimationMs > 0;
	char aDisplayedInputText[MAX_CHAT_LENGTH];
	str_copy(aDisplayedInputText, Input.GetDisplayedString(), sizeof(aDisplayedInputText));
	const float TypingAnimDuration = BCUiAnimations::MsToSeconds(g_Config.m_BcChatTypingAnimationMs);
	const bool CaretAnimEnabled = TypingEnabled && TypingAnimDuration > 0.0f;
	Input.SetHideCursor(CaretAnimEnabled);

	std::vector<STextColorSplit> vTypingColorSplits;
	std::vector<STypingGlyphAnim> vActiveTypingGlyphAnims;
	if(TypingEnabled && TypingAnimDuration > 0.0f && aDisplayedInputText[0] != '\0' && TypingAnimSupportsText(aDisplayedInputText))
	{
		for(auto It = m_vTypingGlyphAnims.begin(); It != m_vTypingGlyphAnims.end();)
		{
			const float TypingAnimAge = (time_get() - It->m_StartTime) / (float)time_freq();
			const int StartByte = It->m_ByteIndex;
			const int GlyphBytes = It->m_ByteLength;
			const int StoredGlyphBytes = str_length(It->m_aText);
			const bool Valid =
				TypingAnimAge < TypingAnimDuration &&
				It->m_ByteIndex >= 0 &&
				GlyphBytes > 0 &&
				StartByte + GlyphBytes <= str_length(aDisplayedInputText) &&
				StoredGlyphBytes == GlyphBytes &&
				str_comp_num(It->m_aText, aDisplayedInputText + StartByte, GlyphBytes) == 0;
			if(!Valid)
			{
				It = m_vTypingGlyphAnims.erase(It);
				continue;
			}

			vActiveTypingGlyphAnims.push_back(*It);
			vTypingColorSplits.emplace_back(It->m_ByteIndex, It->m_ByteLength, ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f));
			++It;
		}
	}

	const bool DisableBaseOutline = !vTypingColorSplits.empty();
	if(DisableBaseOutline)
		TextRender()->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f));
	const STextBoundingBox BoundingBox = Input.Render(&InputCursorRect, FontSize, TEXTALIGN_TL, Changed, MessageMaxWidth, 0.0f, vTypingColorSplits);
	if(DisableBaseOutline)
		TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());

	for(const auto &TypingGlyphAnim : vActiveTypingGlyphAnims)
	{
		const float TypingAnimAge = (time_get() - TypingGlyphAnim.m_StartTime) / (float)time_freq();
		const float Progress = std::clamp(TypingAnimAge / TypingAnimDuration, 0.0f, 1.0f);
		const float Ease = BCUiAnimations::EaseOutCubic(Progress);
		const float OverlayYOffset = -4.5f * (1.0f - Ease);
		const int PrefixBytes = TypingGlyphAnim.m_ByteIndex;
		char aPrefixText[MAX_CHAT_LENGTH] = "";
		if(PrefixBytes < 0 || PrefixBytes > str_length(aDisplayedInputText))
			continue;
		str_truncate(aPrefixText, sizeof(aPrefixText), aDisplayedInputText, PrefixBytes);

		CTextCursor MeasureCursor;
		MeasureCursor.SetPosition(vec2(InputCursorRect.x, InputCursorRect.y));
		MeasureCursor.m_FontSize = FontSize;
		MeasureCursor.m_LineWidth = MessageMaxWidth;
		TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f));
		TextRender()->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f));
		TextRender()->TextEx(&MeasureCursor, aPrefixText);

		CTextCursor OverlayCursor;
		OverlayCursor.SetPosition(vec2(MeasureCursor.m_X, MeasureCursor.m_Y + OverlayYOffset));
		OverlayCursor.m_FontSize = FontSize;
		OverlayCursor.m_LineWidth = MessageMaxWidth;
		TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.75f + 0.25f * Ease));
		TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor().WithMultipliedAlpha(0.75f + 0.25f * Ease));
		TextRender()->TextEx(&OverlayCursor, TypingGlyphAnim.m_aText);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
	TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());

	if(CaretAnimEnabled)
	{
		const float CaretAnimDuration = std::min(TypingAnimDuration, 0.09f);
		const vec2 CaretTargetPos = Input.GetCaretPosition();
		const int64_t NowTicks = time_get();
		if(!m_CaretAnimValid)
		{
			m_CaretAnimFromPos = CaretTargetPos;
			m_CaretAnimTargetPos = CaretTargetPos;
			m_CaretVisualPos = CaretTargetPos;
			m_CaretAnimStartTime = NowTicks;
			m_CaretAnimValid = true;
		}
		else if(length(CaretTargetPos - m_CaretAnimTargetPos) > 0.01f)
		{
			m_CaretAnimFromPos = m_CaretVisualPos;
			m_CaretAnimTargetPos = CaretTargetPos;
			m_CaretAnimStartTime = NowTicks;
		}

		const float CaretAnimAge = (NowTicks - m_CaretAnimStartTime) / (float)time_freq();
		const float CaretEase = BCUiAnimations::EaseOutCubic(CaretAnimAge / CaretAnimDuration);
		m_CaretVisualPos = m_CaretAnimFromPos + (m_CaretAnimTargetPos - m_CaretAnimFromPos) * CaretEase;

		if(Changed)
			m_CaretBlinkAnchor = NowTicks - (int64_t)(0.501f * time_freq());
		else if(NowTicks - m_CaretBlinkAnchor > time_freq())
			m_CaretBlinkAnchor = NowTicks;
		const bool CaretVisible = Changed || (NowTicks - m_CaretBlinkAnchor) > time_freq() / 2;

		if(CaretVisible)
		{
			const CScreenRect ScreenRect = Graphics()->GetScreen();
			const float CursorInnerWidth = (ScreenRect.Width() / Graphics()->ScreenWidth()) * 2.0f;
			const float CursorOuterWidth = CursorInnerWidth * 2.0f;
			const float CursorOuterInnerDiff = (CursorOuterWidth - CursorInnerWidth) / 2.0f;
			const float CaretHeight = FontSize;

			Graphics()->TextureClear();
			Graphics()->QuadsBegin();

			Graphics()->SetColor(TextRender()->DefaultTextOutlineColor());
			const IGraphics::CQuadItem OuterQuad(m_CaretVisualPos.x - CursorOuterInnerDiff, m_CaretVisualPos.y, CursorOuterWidth, CaretHeight);
			Graphics()->QuadsDrawTL(&OuterQuad, 1);

			Graphics()->SetColor(TextRender()->DefaultTextColor());
			const IGraphics::CQuadItem InnerQuad(m_CaretVisualPos.x, m_CaretVisualPos.y + CursorOuterInnerDiff, CursorInnerWidth, CaretHeight - CursorOuterInnerDiff * 2.0f);
			Graphics()->QuadsDrawTL(&InnerQuad, 1);

			Graphics()->QuadsEnd();
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
		}
	}

	return BoundingBox;
}

float CBcUiAnimations::KillfeedSlideOffsetX(ITextRender *pTextRender, int64_t AppearTime, int64_t Now, const STextContainerIndex &VictimText, const STextContainerIndex &KillerText, int TeamSize, int FirstVictimId, int KillerId) const
{
	if(!BCUiAnimations::Enabled() || g_Config.m_BcKillfeedAnimation == 0 || g_Config.m_BcKillfeedAnimationMs <= 0 || AppearTime <= 0)
		return 0.0f;

	const float Duration = BCUiAnimations::MsToSeconds(g_Config.m_BcKillfeedAnimationMs);
	const float AgeSeconds = (Now - AppearTime) / (float)time_freq();
	const float AppearProgress = Duration > 0.0f ? std::clamp(AgeSeconds / Duration, 0.0f, 1.0f) : 1.0f;
	const float AppearEase = BCUiAnimations::EaseInOutQuad(AppearProgress);

	float KillMsgWidth = 0.0f;
	if(VictimText.Valid())
		KillMsgWidth += pTextRender->GetBoundingBoxTextContainer(VictimText).m_W;
	KillMsgWidth += 24.0f;
	KillMsgWidth += 44.0f * TeamSize;
	KillMsgWidth += 32.0f;
	KillMsgWidth += 52.0f;
	if(FirstVictimId != KillerId)
	{
		KillMsgWidth += 24.0f;
		KillMsgWidth += 32.0f;
		if(KillerText.Valid())
			KillMsgWidth += pTextRender->GetBoundingBoxTextContainer(KillerText).m_W;
	}

	return (1.0f - AppearEase) * (KillMsgWidth + 40.0f);
}

CUIRect CBcUiAnimations::ScaleMenuButtonRect(const CUIRect &Base, float Scale)
{
	CUIRect Out = Base;
	Out.w *= Scale;
	Out.h *= Scale;
	Out.x = Base.x + (Base.w - Out.w) * 0.5f;
	Out.y = Base.y + (Base.h - Out.h) * 0.5f;
	return Out;
}

void CBcUiAnimations::UpdateMainMenuButtonScales(const CUIRect *pButtons, int Count, CUi *pUi, float FrameTime)
{
	const int UseCount = std::min(Count, MAIN_MENU_BUTTON_COUNT);
	const bool AnimEnabled = BCUiAnimations::Enabled() && g_Config.m_BcMainMenuAnimation != 0;
	int HoveredIndex = -1;
	if(AnimEnabled)
	{
		for(int i = 0; i < UseCount; ++i)
		{
			const CUIRect Scaled = ScaleMenuButtonRect(pButtons[i], m_aMainMenuButtonScale[i]);
			if(pUi->MouseHovered(&Scaled))
			{
				HoveredIndex = i;
				break;
			}
		}
	}
	const bool AnyHovered = HoveredIndex != -1;
	const float HoverScale = 1.08f;
	const float OtherScale = 0.94f;
	const float Speed = (float)g_Config.m_BcMainMenuAnimationSpeed;
	const float Blend = AnimEnabled ? std::clamp(FrameTime * Speed, 0.0f, 1.0f) : 1.0f;
	for(int i = 0; i < UseCount; ++i)
	{
		const float Target = (AnimEnabled && AnyHovered) ? (i == HoveredIndex ? HoverScale : OtherScale) : 1.0f;
		m_aMainMenuButtonScale[i] += (Target - m_aMainMenuButtonScale[i]) * Blend;
	}
}

float CBcUiAnimations::MainMenuButtonScale(int Index) const
{
	if(Index < 0 || Index >= MAIN_MENU_BUTTON_COUNT)
		return 1.0f;
	return m_aMainMenuButtonScale[Index];
}
