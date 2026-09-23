/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_BC_MENU_BADGES_H
#define GAME_CLIENT_BC_MENU_BADGES_H

#include <base/color.h>

#include <engine/graphics.h>
#include <engine/textrender.h>

#include <game/client/components/bestclient/ui_theme/style.h>
#include <game/client/ui.h>
#include <game/client/ui_rect.h>
#include <game/localization.h>

namespace BcMenuBadges
{

struct SBadge
{
	const char *m_pText;
	float m_FontSize;
	ColorRGBA m_Left;
	ColorRGBA m_Right;
	float m_Rounding;
};

inline constexpr SBadge New{
	"NEW", 11.0f,
	ColorRGBA(0.35f, 0.85f, 0.45f, 1.0f), ColorRGBA(0.15f, 0.55f, 0.25f, 1.0f),
	5.0f};
inline constexpr SBadge Beta{
	"BETA", 12.0f,
	ColorRGBA(0.95f, 0.25f, 0.25f, 1.0f), ColorRGBA(0.75f, 0.08f, 0.08f, 1.0f),
	5.0f};
inline constexpr SBadge EClient{
	"E-Client", 12.0f,
	ColorRGBA(0.95f, 0.80f, 0.20f, 1.0f), ColorRGBA(0.75f, 0.55f, 0.05f, 1.0f),
	5.0f};
inline constexpr SBadge RClient{
	"R-Client", 12.0f,
	ColorRGBA(0.30f, 0.55f, 0.95f, 1.0f), ColorRGBA(0.15f, 0.35f, 0.75f, 1.0f),
	5.0f};
inline constexpr SBadge Author{
	"IDEA", 12.0f,
	ColorRGBA(0.58f, 0.42f, 0.95f, 1.0f), ColorRGBA(0.38f, 0.20f, 0.78f, 1.0f),
	5.0f};

inline void DrawAt(IGraphics *pGraphics, CUi *pUi, ITextRender *pTextRender, const CUIRect &Badge, const SBadge &Style, const char *pText = nullptr)
{
	const char *pLabel = pText != nullptr ? pText : Style.m_pText;
	if(BestClientUiTheme::IsCustomUiBadge())
	{
		BestClientUiTheme::DrawUiBadge(&Badge, Style.m_Left.a, Style.m_Right.a, Style.m_Rounding, false);
	}
	else
	{
		pGraphics->DrawRect4(
			Badge.x, Badge.y, Badge.w, Badge.h,
			Style.m_Left, Style.m_Right, Style.m_Left, Style.m_Right,
			IGraphics::CORNER_ALL, Style.m_Rounding);
	}
	pUi->DoLabel(&Badge, pLabel, Style.m_FontSize, TEXTALIGN_MC);
}

inline void Draw(IGraphics *pGraphics, CUi *pUi, ITextRender *pTextRender, CUIRect *pRow, const SBadge &Style, float Gap, const char *pText = nullptr)
{
	const char *pLabel = pText != nullptr ? pText : Style.m_pText;
	const float BadgeWidth = pTextRender->TextWidth(Style.m_FontSize, pLabel) + 10.0f;
	CUIRect Badge;
	pRow->VSplitRight(BadgeWidth + Gap, pRow, &Badge);
	Badge.VSplitLeft(Gap, nullptr, &Badge);
	Badge.HMargin(2.0f, &Badge);
	DrawAt(pGraphics, pUi, pTextRender, Badge, Style, pLabel);
}

inline void DrawBeta(IGraphics *pGraphics, CUi *pUi, ITextRender *pTextRender, CUIRect *pRow, float Gap)
{
	Draw(pGraphics, pUi, pTextRender, pRow, Beta, Gap, Localize("BETA"));
}

inline void DrawNew(IGraphics *pGraphics, CUi *pUi, ITextRender *pTextRender, CUIRect *pRow, float Gap)
{
	Draw(pGraphics, pUi, pTextRender, pRow, New, Gap);
}

inline void DrawEClient(IGraphics *pGraphics, CUi *pUi, ITextRender *pTextRender, CUIRect *pRow, float Gap)
{
	Draw(pGraphics, pUi, pTextRender, pRow, EClient, Gap);
}

inline void DrawRClient(IGraphics *pGraphics, CUi *pUi, ITextRender *pTextRender, CUIRect *pRow, float Gap)
{
	Draw(pGraphics, pUi, pTextRender, pRow, RClient, Gap);
}

inline void DrawAuthor(IGraphics *pGraphics, CUi *pUi, ITextRender *pTextRender, CUIRect *pRow, float Gap, CUIRect *pBadgeOut = nullptr)
{
	const char *pLabel = Author.m_pText;
	const float BadgeWidth = pTextRender->TextWidth(Author.m_FontSize, pLabel) + 10.0f;
	CUIRect Badge;
	pRow->VSplitRight(BadgeWidth + Gap, pRow, &Badge);
	Badge.VSplitLeft(Gap, nullptr, &Badge);
	Badge.HMargin(2.0f, &Badge);
	DrawAt(pGraphics, pUi, pTextRender, Badge, Author, pLabel);
	if(pBadgeOut != nullptr)
		*pBadgeOut = Badge;
}

} // namespace BcMenuBadges

#endif
