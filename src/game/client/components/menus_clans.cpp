/* Copyright © 2026 BestProject Team */
#include <base/color.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/animstate.h>
#include <game/client/bc_ui_animations.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include "countryflags.h"
#include "menus.h"

#include <engine/font_icons.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

class CClansEditBoxColorFunction final : public IButtonColorFunction
{
public:
	ColorRGBA GetColor(bool Active, bool Hovered) const override
	{
		if(Active)
			return ColorRGBA(0.0f, 0.0f, 0.0f, 0.42f);
		if(Hovered)
			return ColorRGBA(0.0f, 0.0f, 0.0f, 0.32f);
		return ColorRGBA(0.0f, 0.0f, 0.0f, 0.22f);
	}
};

static constexpr int CLAN_DESC_MAX_LINES = 3;
static constexpr int CLAN_DESC_CHARS_PER_LINE = 36;

static bool DoClansEditBox(CUi *pUi, CLineInput *pLineInput, const CUIRect *pRect, float FontSize, int Corners = IGraphics::CORNER_ALL, float LineWidth = -1.0f, float LineSpacing = 0.0f, int Align = -1)
{
	static const CClansEditBoxColorFunction s_ColorFunction;
	return pUi->DoEditBox(pLineInput, pRect, FontSize, Corners, {}, LineWidth, LineSpacing, &s_ColorFunction, Align);
}

// Multiline description: each line centered independently (TEXTALIGN_TC only centers the whole block).
static bool DoClansDescEditBox(CUi *pUi, ITextRender *pTextRender, CLineInput *pLineInput, const CUIRect *pRect, float FontSize, float LineSpacing)
{
	static const CClansEditBoxColorFunction s_ColorFunction;

	const bool Inside = pUi->MouseHovered(pRect);
	const bool Active = pUi->LastActiveItem() == pLineInput;
	const bool Changed = pLineInput->WasChanged();
	const bool CursorChanged = pLineInput->WasCursorChanged();

	bool JustGotActive = false;
	if(pUi->CheckActiveItem(pLineInput))
	{
		if(!pUi->MouseButton(0))
			pUi->SetActiveItem(nullptr);
	}
	else if(pUi->HotItem() == pLineInput)
	{
		if(pUi->MouseButton(0))
		{
			if(!Active)
				JustGotActive = true;
			pUi->SetActiveItem(pLineInput);
		}
	}

	if(Inside && !pUi->MouseButton(0))
		pUi->SetHotItem(pLineInput);

	if(pUi->Enabled() && Active && !JustGotActive)
		pLineInput->Activate(EInputPriority::UI);
	else
		pLineInput->Deactivate();

	CLineInput::SMouseSelection *pMouseSelection = pLineInput->GetMouseSelection();
	if(Inside && !pMouseSelection->m_Selecting && pUi->MouseButtonClicked(0))
	{
		pMouseSelection->m_Selecting = true;
		pMouseSelection->m_PressMouse = pUi->MousePos();
	}
	if(pMouseSelection->m_Selecting)
	{
		pMouseSelection->m_ReleaseMouse = pUi->MousePos();
		if(!pUi->MouseButton(0))
			pMouseSelection->m_Selecting = false;
	}

	pRect->Draw(s_ColorFunction.GetColor(Active, pUi->HotItem() == pLineInput), IGraphics::CORNER_ALL, 3.0f);

	CUIRect Textbox;
	pRect->VMargin(2.0f, &Textbox);
	const float TopPad = 3.0f;
	if(Textbox.h > TopPad + FontSize)
	{
		Textbox.y += TopPad;
		Textbox.h -= TopPad;
	}

	const char *pStr = pLineInput->GetString();
	const bool Empty = pStr[0] == '\0';
	const char *pDraw = Empty ? (pLineInput->GetEmptyText() ? pLineInput->GetEmptyText() : "") : pStr;

	const float LineAdvance = FontSize + LineSpacing;
	size_t aLineStart[CLAN_DESC_MAX_LINES + 1] = {};
	size_t aLineEnd[CLAN_DESC_MAX_LINES + 1] = {};
	int NumLines = 0;
	{
		const char *p = pDraw;
		size_t Base = 0;
		for(;;)
		{
			if(NumLines >= CLAN_DESC_MAX_LINES)
				break;
			const char *pNl = strchr(p, '\n');
			aLineStart[NumLines] = Base;
			if(pNl)
			{
				aLineEnd[NumLines] = Base + (size_t)(pNl - p);
				NumLines++;
				Base = aLineEnd[NumLines - 1] + 1;
				p = pNl + 1;
				if(!*p)
				{
					if(NumLines < CLAN_DESC_MAX_LINES)
					{
						aLineStart[NumLines] = Base;
						aLineEnd[NumLines] = Base;
						NumLines++;
					}
					break;
				}
			}
			else
			{
				aLineEnd[NumLines] = Base + (size_t)str_length(p);
				NumLines++;
				break;
			}
		}
		if(NumLines <= 0)
		{
			aLineStart[0] = 0;
			aLineEnd[0] = 0;
			NumLines = 1;
		}
	}

	// Click → cursor on centered line (before Render so LineInput does not override from left-aligned layout).
	if(!Empty && Active && Inside && pUi->MouseButtonClicked(0))
	{
		const float RelY = pUi->MousePos().y - Textbox.y;
		int LineIdx = std::clamp((int)(RelY / LineAdvance), 0, NumLines - 1);
		const size_t Start = aLineStart[LineIdx];
		const size_t End = aLineEnd[LineIdx];
		char aLine[128];
		const size_t Len = minimum(sizeof(aLine) - 1, End - Start);
		mem_copy(aLine, pDraw + Start, Len);
		aLine[Len] = '\0';
		const float LineW = pTextRender->TextWidth(FontSize, aLine);
		const float LineX = Textbox.x + (Textbox.w - LineW) * 0.5f;
		const float LocalX = pUi->MousePos().x - LineX;
		size_t Best = End;
		float BestDist = 1e9f;
		for(size_t Off = 0;;)
		{
			char aPrefix[128];
			mem_copy(aPrefix, aLine, Off);
			aPrefix[Off] = '\0';
			const float Dist = absolute(pTextRender->TextWidth(FontSize, aPrefix) - LocalX);
			if(Dist < BestDist)
			{
				BestDist = Dist;
				Best = Start + Off;
			}
			if(Off >= Len)
				break;
			const int Next = str_utf8_forward(aLine, (int)Off);
			if(Next <= (int)Off)
				break;
			Off = (size_t)Next;
		}
		pLineInput->SetCursorOffset(Best);
		pLineInput->SelectNothing();
	}

	// Drive LineInput (WasRendered / IME) with an invisible left-aligned pass; suppress its cursor/selection.
	// Clear empty text so Render does not draw the placeholder (it forces alpha 0.75 and ignores our transparent color).
	pUi->ClipEnable(pRect);
	const bool PrevSelecting = pMouseSelection->m_Selecting;
	pMouseSelection->m_Selecting = false;
	const char *pEmptyText = pLineInput->GetEmptyText();
	pLineInput->SetEmptyText(nullptr);
	const ColorRGBA PrevText = pTextRender->GetTextColor();
	const ColorRGBA PrevOutline = pTextRender->GetTextOutlineColor();
	pTextRender->TextColor(1.0f, 1.0f, 1.0f, 0.0f);
	pTextRender->TextOutlineColor(0.0f, 0.0f, 0.0f, 0.0f);
	pLineInput->SetHideCursor(true);
	pLineInput->Render(&Textbox, FontSize, TEXTALIGN_TL, Changed || CursorChanged, -1.0f, LineSpacing);
	pLineInput->SetHideCursor(false);
	pLineInput->SetEmptyText(pEmptyText);
	pMouseSelection->m_Selecting = PrevSelecting;
	pTextRender->TextColor(PrevText);
	pTextRender->TextOutlineColor(PrevOutline);

	if(Empty)
		pTextRender->TextColor(1.0f, 1.0f, 1.0f, 0.75f);

	for(int i = 0; i < NumLines; i++)
	{
		char aLine[128];
		const size_t Len = minimum(sizeof(aLine) - 1, aLineEnd[i] - aLineStart[i]);
		mem_copy(aLine, pDraw + aLineStart[i], Len);
		aLine[Len] = '\0';
		CUIRect LineRect;
		LineRect.x = Textbox.x;
		LineRect.y = Textbox.y + (float)i * LineAdvance;
		LineRect.w = Textbox.w;
		LineRect.h = LineAdvance;
		pUi->DoLabel(&LineRect, aLine, FontSize, TEXTALIGN_MC);
	}

	if(!Empty && Active && pLineInput->IsActive())
	{
		const size_t Cursor = pLineInput->GetCursorOffset();
		int CaretLine = 0;
		for(int i = 0; i < NumLines; i++)
		{
			if(Cursor >= aLineStart[i] && Cursor <= aLineEnd[i])
			{
				CaretLine = i;
				break;
			}
			CaretLine = i;
		}

		char aLine[128];
		const size_t LineLen = minimum(sizeof(aLine) - 1, aLineEnd[CaretLine] - aLineStart[CaretLine]);
		mem_copy(aLine, pDraw + aLineStart[CaretLine], LineLen);
		aLine[LineLen] = '\0';
		const float LineW = pTextRender->TextWidth(FontSize, aLine);
		const float LineX = Textbox.x + (Textbox.w - LineW) * 0.5f;
		size_t PrefixLen = 0;
		if(Cursor > aLineStart[CaretLine])
			PrefixLen = minimum(LineLen, Cursor - aLineStart[CaretLine]);
		char aPrefix[128];
		mem_copy(aPrefix, aLine, PrefixLen);
		aPrefix[PrefixLen] = '\0';

		CUIRect CaretRect;
		CaretRect.x = LineX + pTextRender->TextWidth(FontSize, aPrefix);
		CaretRect.y = Textbox.y + (float)CaretLine * LineAdvance;
		CaretRect.w = 1.5f;
		CaretRect.h = FontSize;
		CaretRect.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.9f), IGraphics::CORNER_NONE, 0.0f);
	}

	pTextRender->TextColor(pTextRender->DefaultTextColor());
	pUi->ClipDisable();
	return Changed;
}

// Take up to MaxChars UTF-8 codepoints from p, within MaxBytes. Returns byte length.
static int ClanDescTakeChars(const char *p, int MaxChars, int MaxBytes)
{
	if(!p || MaxChars <= 0 || MaxBytes <= 0)
		return 0;
	int Cursor = 0;
	int Chars = 0;
	while(p[Cursor] && Chars < MaxChars && Cursor < MaxBytes)
	{
		const int Next = str_utf8_forward(p, Cursor);
		if(Next <= Cursor || Next > MaxBytes)
			break;
		Cursor = Next;
		Chars++;
	}
	return Cursor;
}

// Enforce max 3 lines × 36 characters. Soft-wraps long segments; prefers space breaks.
static void NormalizeClanDescription(char *pDesc, int DescSize)
{
	if(!pDesc || DescSize <= 1)
		return;

	char aOut[256];
	int OutLen = 0;
	int Lines = 0;
	const char *p = pDesc;

	auto AppendLine = [&](const char *pLine, int ByteLen) -> bool {
		if(Lines >= CLAN_DESC_MAX_LINES)
			return false;
		if(Lines > 0)
		{
			if(OutLen + 1 >= (int)sizeof(aOut))
				return false;
			aOut[OutLen++] = '\n';
		}
		ByteLen = minimum(ByteLen, (int)sizeof(aOut) - 1 - OutLen);
		if(ByteLen > 0)
		{
			mem_copy(aOut + OutLen, pLine, ByteLen);
			OutLen += ByteLen;
		}
		aOut[OutLen] = '\0';
		Lines++;
		return Lines < CLAN_DESC_MAX_LINES;
	};

	for(;;)
	{
		if(Lines >= CLAN_DESC_MAX_LINES)
			break;

		const char *pNl = strchr(p, '\n');
		const int SegBytes = pNl ? (int)(pNl - p) : str_length(p);
		int Off = 0;
		if(SegBytes <= 0)
		{
			if(!AppendLine("", 0))
				break;
		}
		else
		{
			while(Off < SegBytes && Lines < CLAN_DESC_MAX_LINES)
			{
				int Take = ClanDescTakeChars(p + Off, CLAN_DESC_CHARS_PER_LINE, SegBytes - Off);
				if(Take <= 0)
					break;
				if(Off + Take < SegBytes)
				{
					int SpaceBreak = -1;
					for(int i = Take - 1; i > 0; i--)
					{
						if(p[Off + i] == ' ' && (p[Off + i] & 0xC0) != 0x80)
						{
							SpaceBreak = i;
							break;
						}
					}
					if(SpaceBreak > 0)
						Take = SpaceBreak;
				}
				if(!AppendLine(p + Off, Take))
					break;
				Off += Take;
				if(Off < SegBytes && p[Off] == ' ')
					Off++;
			}
		}

		if(!pNl)
			break;
		p = pNl + 1;
		// Keep a trailing empty line after Shift+Enter ("text\n").
		if(!*p)
		{
			AppendLine("", 0);
			break;
		}
	}

	str_copy(pDesc, aOut, DescSize);
}

static int CountClanDescriptionLines(const char *pDesc)
{
	if(!pDesc || !pDesc[0])
		return 1;
	int Lines = 0;
	const char *p = pDesc;
	for(;;)
	{
		const char *pNl = strchr(p, '\n');
		const int SegBytes = pNl ? (int)(pNl - p) : str_length(p);
		int Off = 0;
		if(SegBytes <= 0)
			Lines++;
		else
		{
			while(Off < SegBytes)
			{
				int Take = ClanDescTakeChars(p + Off, CLAN_DESC_CHARS_PER_LINE, SegBytes - Off);
				if(Take <= 0)
					break;
				if(Off + Take < SegBytes)
				{
					int SpaceBreak = -1;
					for(int i = Take - 1; i > 0; i--)
					{
						if(p[Off + i] == ' ' && (p[Off + i] & 0xC0) != 0x80)
						{
							SpaceBreak = i;
							break;
						}
					}
					if(SpaceBreak > 0)
						Take = SpaceBreak;
				}
				Lines++;
				Off += Take;
				if(Off < SegBytes && p[Off] == ' ')
					Off++;
			}
		}
		if(!pNl)
			break;
		p = pNl + 1;
	}
	return std::clamp(maximum(1, Lines), 1, CLAN_DESC_MAX_LINES);
}

static void DoClanDescriptionCentered(CUi *pUi, ITextRender *pTextRender, CUIRect Rect, const char *pDesc, float FontSize, float LineSpacing)
{
	(void)pTextRender;
	if(!pDesc)
		pDesc = "";
	const float LineAdvance = FontSize + LineSpacing;
	int Drawn = 0;
	const char *p = pDesc;
	for(;;)
	{
		if(Drawn >= CLAN_DESC_MAX_LINES)
			break;
		const char *pNl = strchr(p, '\n');
		const int SegBytes = pNl ? (int)(pNl - p) : str_length(p);
		int Off = 0;
		if(SegBytes <= 0)
		{
			Rect.HSplitTop(LineAdvance, nullptr, &Rect);
			Drawn++;
		}
		else
		{
			while(Off < SegBytes && Drawn < CLAN_DESC_MAX_LINES)
			{
				int Take = ClanDescTakeChars(p + Off, CLAN_DESC_CHARS_PER_LINE, SegBytes - Off);
				if(Take <= 0)
					break;
				if(Off + Take < SegBytes)
				{
					int SpaceBreak = -1;
					for(int i = Take - 1; i > 0; i--)
					{
						if(p[Off + i] == ' ' && (p[Off + i] & 0xC0) != 0x80)
						{
							SpaceBreak = i;
							break;
						}
					}
					if(SpaceBreak > 0)
						Take = SpaceBreak;
				}

				char aLine[128];
				const size_t LineLen = minimum(sizeof(aLine) - 1, (size_t)Take);
				mem_copy(aLine, p + Off, LineLen);
				aLine[LineLen] = '\0';
				str_utf8_fix_truncation(aLine);

				CUIRect LineRect;
				Rect.HSplitTop(LineAdvance, &LineRect, &Rect);
				pUi->DoLabel(&LineRect, aLine, FontSize, TEXTALIGN_MC);
				Drawn++;

				Off += Take;
				if(Off < SegBytes && p[Off] == ' ')
					Off++;
			}
		}
		if(!pNl)
			break;
		p = pNl + 1;
	}
}

static CUIRect ClansAnimGrowRect(const CUIRect &OuterRect, bool GrowFromRight, float Phase)
{
	CUIRect AnimRect;
	AnimRect.w = OuterRect.w * Phase;
	AnimRect.h = OuterRect.h * Phase;
	AnimRect.y = OuterRect.y;
	AnimRect.x = GrowFromRight ? (OuterRect.x + OuterRect.w - AnimRect.w) : OuterRect.x;
	return AnimRect;
}

static const char *RoleLabel(CClans::ERole Role)
{
	switch(Role)
	{
	case CClans::ERole::PRESIDENT: return Localize("President");
	case CClans::ERole::VICE_PRESIDENT: return Localize("Vice-President");
	case CClans::ERole::VETERAN: return Localize("Veteran");
	case CClans::ERole::MEMBER: return Localize("Member");
	default: return "";
	}
}

static constexpr int NUM_CLAN_ICONS = 24;

static const char *ClanIconGlyph(int IconId)
{
	static const char *s_apGlyphs[NUM_CLAN_ICONS] = {
		FontIcon::STAR,
		FontIcon::HEART,
		FontIcon::BOMB,
		FontIcon::FLAG_CHECKERED,
		FontIcon::HOUSE,
		FontIcon::KEY,
		FontIcon::SNAKE,
		FontIcon::CHESS_KING,
		FontIcon::CHESS_QUEEN,
		FontIcon::CHESS_ROOK,
		FontIcon::CHESS_KNIGHT,
		FontIcon::CHESS_BISHOP,
		FontIcon::CHESS_PAWN,
		FontIcon::USER,
		FontIcon::ICON_USERS,
		FontIcon::LOCK,
		FontIcon::BOOKMARK,
		FontIcon::EYE,
		FontIcon::EARTH_AMERICAS,
		FontIcon::MUSIC,
		FontIcon::MICROPHONE,
		FontIcon::CAMERA,
		FontIcon::NETWORK_WIRED,
		FontIcon::TABLE_TENNIS_PADDLE_BALL,
	};
	return s_apGlyphs[std::clamp(IconId, 0, NUM_CLAN_ICONS - 1)];
}

static ColorRGBA ClanColorRgb(unsigned Color)
{
	Color &= 0xFFFFFF;
	return ColorRGBA(((Color >> 16) & 0xFF) / 255.0f, ((Color >> 8) & 0xFF) / 255.0f, (Color & 0xFF) / 255.0f, 1.0f);
}

static void RenderClansErrorBanner(CGameClient *pGameClient, CUi *pUi, ITextRender *pTextRender, CUIRect MainView, CClans &Clans)
{
	if(!Clans.ErrorMessage()[0])
		return;

	const bool OfferDiscord = Clans.ErrorOfferDiscord();
	CUIRect Msg;
	MainView.HSplitBottom(OfferDiscord ? 46.0f : 28.0f, nullptr, &Msg);
	Msg.VSplitLeft(minimum(OfferDiscord ? 420.0f : 280.0f, Msg.w), &Msg, nullptr);
	Msg.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.35f), IGraphics::CORNER_ALL, 5.0f);
	Msg.Margin(7.0f, &Msg);

	CUIRect Line, Link;
	if(OfferDiscord)
		Msg.HSplitTop(16.0f, &Line, &Link);
	else
		Line = Msg;

	pTextRender->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
	pUi->DoLabel(&Line, Clans.ErrorMessage(), 12.0f, TEXTALIGN_ML);
	pTextRender->TextColor(pTextRender->DefaultTextColor());

	if(OfferDiscord)
	{
		Link.HSplitTop(4.0f, nullptr, &Link);
		static CButtonContainer s_DiscordLink;
		const bool Hot = pUi->HotItem() == &s_DiscordLink;
		pTextRender->TextColor(Hot ? ColorRGBA(0.45f, 0.75f, 1.0f, 1.0f) : ColorRGBA(0.35f, 0.65f, 1.0f, 1.0f));
		pUi->DoLabel(&Link, "discord.gg/bestclient", 12.0f, TEXTALIGN_ML);
		pTextRender->TextColor(pTextRender->DefaultTextColor());
		if(pUi->DoButtonLogic(&s_DiscordLink, 0, &Link, BUTTONFLAG_LEFT))
			pGameClient->Client()->ViewLink("https://discord.gg/bestclient");
	}
}

static void RenderClanIcon(CUi *pUi, ITextRender *pTextRender, CUIRect Box, int IconId, unsigned Color)
{
	pTextRender->SetFontPreset(EFontPreset::ICON_FONT);
	pTextRender->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	pTextRender->TextColor(ClanColorRgb(Color));
	pUi->DoLabel(&Box, ClanIconGlyph(IconId), minimum(Box.w, Box.h) * 0.7f, TEXTALIGN_MC);
	pTextRender->TextColor(pTextRender->DefaultTextColor());
	pTextRender->SetRenderFlags(0);
	pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);
}

static void RenderClanFlag(CGameClient *pGameClient, CUIRect Box, int Country)
{
	const float FlagH = Box.h;
	const float FlagW = FlagH * 2.0f;
	CUIRect Flag = Box;
	Flag.w = FlagW;
	Flag.x += (Box.w - FlagW) * 0.5f;
	pGameClient->m_CountryFlags.Render(Country, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), Flag.x, Flag.y, Flag.w, Flag.h);
}

static void RenderClanIconPickerRow(CUi *pUi, ITextRender *pTextRender, CUIRect Row, int StartId, int Count, int *pIconId, unsigned ClanRgb, float IconCell, CButtonContainer *pIcons)
{
	for(int i = 0; i < Count; i++)
	{
		const int Id = StartId + i;
		if(Id >= NUM_CLAN_ICONS)
			break;
		CUIRect IconBtn;
		Row.VSplitLeft(IconCell, &IconBtn, &Row);
		IconBtn.VSplitRight(4.0f, &IconBtn, nullptr);
		IconBtn.Draw(ColorRGBA(0, 0, 0, *pIconId == Id ? 0.34f : 0.22f), IGraphics::CORNER_ALL, 4.0f);
		RenderClanIcon(pUi, pTextRender, IconBtn, Id, ClanRgb);
		if(pUi->DoButtonLogic(&pIcons[Id], *pIconId == Id, &IconBtn, BUTTONFLAG_LEFT))
			*pIconId = Id;
	}
}

static void FormatClanPlayingStatus(char *pBuf, int BufSize, const char *pMap, int Players, int MaxPlayers)
{
	str_format(pBuf, BufSize, Localize("playing: \"%s\" | %d/%d"), pMap, Players, MaxPlayers);
}

static bool CatalogClanIsOpen(const CClans &Clans, const char *pClanId)
{
	for(const auto &Cat : Clans.Catalog())
	{
		if(!str_comp(Cat.m_aClanId, pClanId))
			return !str_comp(Cat.m_aJoinPolicy, "open");
	}
	return false;
}

static void RenderClanMemberTee(CGameClient *pGameClient, CRenderTools *pRenderTools, CUIRect TeeBox, const CClans::SSkin &Skin)
{
	const CSkin *pDefault = pGameClient->m_Skins.Find("default");
	const CSkins::CSkinContainer *pCont = pGameClient->m_Skins.FindContainerOrNullptr(Skin.m_aName[0] ? Skin.m_aName : "default");
	CTeeRenderInfo Info;
	Info.Apply(pCont == nullptr || pCont->Skin() == nullptr ? pDefault : pCont->Skin().get());
	Info.ApplyColors(Skin.m_UseCustomColor, Skin.m_ColorBody, Skin.m_ColorFeet);
	Info.m_Size = minimum(TeeBox.w, TeeBox.h) * 0.9f;
	vec2 OffsetToMid;
	CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &Info, OffsetToMid);
	const vec2 Pos = vec2(TeeBox.x + TeeBox.w / 2.0f, TeeBox.y + TeeBox.h / 2.0f + OffsetToMid.y);
	pRenderTools->RenderTee(CAnimState::GetIdle(), &Info, EMOTE_NORMAL, vec2(1.0f, 0.0f), Pos);
}

static void RenderOwnTee(CGameClient *pGameClient, CRenderTools *pRenderTools, CUIRect TeeBox)
{
	CClans::SSkin Skin;
	str_copy(Skin.m_aName, g_Config.m_ClPlayerSkin, sizeof(Skin.m_aName));
	Skin.m_ColorBody = g_Config.m_ClPlayerColorBody;
	Skin.m_ColorFeet = g_Config.m_ClPlayerColorFeet;
	Skin.m_UseCustomColor = g_Config.m_ClPlayerUseCustomColor != 0;
	RenderClanMemberTee(pGameClient, pRenderTools, TeeBox, Skin);
}

static bool DoClansIconBtn(CUi *pUi, ITextRender *pTextRender, CButtonContainer *pId, const char *pIcon, const CUIRect *pRect, bool Danger = false, bool DrawBg = true)
{
	const bool Hot = pUi->HotItem() == pId || pUi->CheckActiveItem(pId);
	if(DrawBg)
		pRect->Draw(ColorRGBA(0.0f, 0.0f, 0.0f, Hot ? 0.28f : 0.20f), IGraphics::CORNER_ALL, 5.0f);

	const ColorRGBA Col = Danger ? ColorRGBA(0.92f, 0.32f, 0.32f, 1.0f) : pTextRender->DefaultTextColor();
	pTextRender->SetFontPreset(EFontPreset::ICON_FONT);
	pTextRender->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	pTextRender->TextColor(Col);
	pUi->DoLabel(pRect, pIcon, minimum(pRect->w, pRect->h) * 0.55f, TEXTALIGN_MC);
	pTextRender->SetRenderFlags(0);
	pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);
	pTextRender->TextColor(pTextRender->DefaultTextColor());
	return pUi->DoButtonLogic(pId, 0, pRect, BUTTONFLAG_LEFT) != 0;
}

static bool DoClansTextBtn(CUi *pUi, ITextRender *pTextRender, CButtonContainer *pId, const char *pText, const CUIRect *pRect, int Checked = 0, bool Danger = false)
{
	const bool Hot = pUi->HotItem() == pId || pUi->CheckActiveItem(pId);
	float Alpha = Checked ? 0.32f : (Hot ? 0.28f : 0.20f);
	pRect->Draw(ColorRGBA(0.0f, 0.0f, 0.0f, Alpha), IGraphics::CORNER_ALL, 5.0f);

	ColorRGBA Col = Danger ? ColorRGBA(0.92f, 0.32f, 0.32f, 1.0f) : pTextRender->DefaultTextColor();
	if(!Checked && !Hot)
		Col.a = 0.90f;
	pTextRender->TextColor(Col);
	pUi->DoLabel(pRect, pText, minimum(14.0f, pRect->h * 0.55f), TEXTALIGN_MC);
	pTextRender->TextColor(pTextRender->DefaultTextColor());
	return pUi->DoButtonLogic(pId, Checked, pRect, BUTTONFLAG_LEFT) != 0;
}

static bool DoClansSidebarItem(CUi *pUi, ITextRender *pTextRender, CButtonContainer *pId, const char *pIcon, const char *pText, const CUIRect *pRect, int Badge = 0, bool Danger = false)
{
	const bool Hot = pUi->HotItem() == pId || pUi->CheckActiveItem(pId);
	pRect->Draw(ColorRGBA(0.0f, 0.0f, 0.0f, Hot ? 0.28f : 0.20f), IGraphics::CORNER_ALL, 5.0f);

	const float FontSize = 14.0f;
	const float IconSize = 16.0f;
	const float Gap = 10.0f;
	const float TextW = pTextRender->TextWidth(FontSize, pText);
	float ContentW = IconSize + Gap + TextW;
	if(Badge > 0)
		ContentW += 8.0f + 18.0f;
	ContentW = minimum(ContentW, pRect->w - 16.0f);

	CUIRect Content = *pRect;
	if(Content.w > ContentW)
		Content.VMargin((Content.w - ContentW) * 0.5f, &Content);

	CUIRect IconBox, TextBox;
	Content.VSplitLeft(IconSize, &IconBox, &Content);
	Content.VSplitLeft(Gap, nullptr, &Content);
	TextBox = Content;

	const ColorRGBA Col = Danger ? ColorRGBA(0.92f, 0.32f, 0.32f, 1.0f) : pTextRender->DefaultTextColor();
	pTextRender->SetFontPreset(EFontPreset::ICON_FONT);
	pTextRender->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	pTextRender->TextColor(Col);
	pUi->DoLabel(&IconBox, pIcon, IconSize, TEXTALIGN_MC);
	pTextRender->SetRenderFlags(0);
	pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);
	pTextRender->TextColor(Col);
	pUi->DoLabel(&TextBox, pText, FontSize, TEXTALIGN_ML);
	pTextRender->TextColor(pTextRender->DefaultTextColor());

	if(Badge > 0)
	{
		CUIRect BadgeBox;
		BadgeBox.w = 18.0f;
		BadgeBox.h = 15.0f;
		BadgeBox.x = TextBox.x + TextW + 8.0f;
		BadgeBox.y = pRect->y + (pRect->h - BadgeBox.h) * 0.5f;
		BadgeBox.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.90f), IGraphics::CORNER_ALL, BadgeBox.h * 0.5f);
		char aBadge[8];
		str_format(aBadge, sizeof(aBadge), "%d", Badge > 99 ? 99 : Badge);
		pTextRender->TextColor(ColorRGBA(0.1f, 0.1f, 0.1f, 1.0f));
		pUi->DoLabel(&BadgeBox, aBadge, 10.0f, TEXTALIGN_MC);
		pTextRender->TextColor(pTextRender->DefaultTextColor());
	}

	return pUi->DoButtonLogic(pId, 0, pRect, BUTTONFLAG_LEFT) != 0;
}

static void DrawClansSectionHeader(CUi *pUi, ITextRender *pTextRender, CUIRect *pArea, const char *pTitle)
{
	CUIRect Label;
	pArea->HSplitTop(18.0f, &Label, pArea);
	pTextRender->TextColor(ColorRGBA(0.55f, 0.55f, 0.55f, 1.0f));
	pUi->DoLabel(&Label, pTitle, 12.0f, TEXTALIGN_MC);
	pTextRender->TextColor(pTextRender->DefaultTextColor());
	pArea->HSplitTop(4.0f, nullptr, pArea);
}

static void DrawClansAccountBarRect(CGameClient *pGameClient, CRenderTools *pRenderTools, CUi *pUi, CClans &Clans, CUIRect Acc)
{
	Acc.Draw(ColorRGBA(0, 0, 0, 0.28f), IGraphics::CORNER_ALL, 4.0f);
	Acc.Margin(3.0f, &Acc);

	CUIRect Tee, Text, LogoutBtn;
	const float LogoutSize = Acc.h;
	Acc.VSplitRight(LogoutSize, &Acc, &LogoutBtn);
	Acc.VSplitRight(4.0f, &Acc, nullptr);
	Acc.VSplitLeft(Acc.h, &Tee, &Text);
	Text.VSplitLeft(4.0f, nullptr, &Text);

	RenderOwnTee(pGameClient, pRenderTools, Tee);

	CUIRect Nick, Sub;
	Text.HSplitMid(&Nick, &Sub);
	pUi->DoLabel(&Nick, Clans.Nickname(), 12.0f, TEXTALIGN_ML);
	{
		char aSub[48];
		if(Clans.InClan() && Clans.Clan().m_aTag[0])
			str_format(aSub, sizeof(aSub), "[%s]", Clans.Clan().m_aTag);
		else if(Clans.InClan() && Clans.ClanId()[0])
			str_copy(aSub, Localize("In a clan"), sizeof(aSub));
		else
			str_copy(aSub, Localize("no clan"), sizeof(aSub));
		pUi->DoLabel(&Sub, aSub, 10.0f, TEXTALIGN_ML);
	}

	static CButtonContainer s_Logout;
	if(DoClansIconBtn(pUi, pGameClient->TextRender(), &s_Logout, FontIcon::RIGHT_FROM_BRACKET, &LogoutBtn, false, false))
		Clans.Logout();
}

void CMenus::RenderClans(CUIRect MainView)
{
	CClans &Clans = GameClient()->m_Clans;

	{
		CUIRect ToastArea = MainView;
		float Y = 4.0f;
		for(const auto &Toast : Clans.Toasts())
		{
			CUIRect ToastRect;
			ToastArea.HSplitTop(Y, nullptr, &ToastRect);
			ToastRect.HSplitTop(22.0f, &ToastRect, nullptr);
			ToastRect.VMargin(MainView.w * 0.25f, &ToastRect);
			ToastRect.Draw(ColorRGBA(0.1f, 0.1f, 0.1f, 0.85f), IGraphics::CORNER_ALL, 4.0f);
			Ui()->DoLabel(&ToastRect, Toast.m_aText, 12.0f, TEXTALIGN_MC);
			Y += 26.0f;
		}
	}

	MainView.Draw(ms_ColorTabbarActive, IGraphics::CORNER_B, 10.0f);
	MainView.Margin(8.0f, &MainView);

	if(!Clans.IsLoggedIn())
	{
		RenderClansAuth(MainView);
		RenderClansErrorBanner(GameClient(), Ui(), TextRender(), MainView, Clans);
		return;
	}

	switch(Clans.View())
	{
	case CClans::EView::SETUP:
		RenderClansSetup(MainView);
		break;
	case CClans::EView::APPLICATIONS:
		RenderClansApplications(MainView);
		break;
	case CClans::EView::ANNOUNCEMENTS:
		RenderClansAnnouncements(MainView);
		break;
	case CClans::EView::SETTINGS:
		RenderClansSettings(MainView);
		break;
	case CClans::EView::RECENT:
		RenderClansRecent(MainView);
		break;
	case CClans::EView::PREVIEW:
		RenderClansPreview(MainView);
		break;
	case CClans::EView::CLAN:
		RenderClansPage(MainView);
		break;
	case CClans::EView::BROWSE:
	case CClans::EView::LANDING:
	default:
		RenderClansLanding(MainView);
		break;
	}

	RenderClansErrorBanner(GameClient(), Ui(), TextRender(), MainView, Clans);
}

void CMenus::RenderClansAuth(CUIRect MainView)
{
	CClans &Clans = GameClient()->m_Clans;
	static CLineInput s_Nick;
	static CLineInput s_Pass;
	static char s_aNick[MAX_NAME_LENGTH];
	static char s_aPass[64];
	static bool s_Init = false;
	if(!s_Init)
	{
		str_copy(s_aNick, g_Config.m_PlayerName, sizeof(s_aNick));
		s_aPass[0] = '\0';
		s_Init = true;
	}
	s_Nick.SetBuffer(s_aNick, sizeof(s_aNick));
	s_Pass.SetBuffer(s_aPass, sizeof(s_aPass));
	s_Pass.SetHidden(true);

	CUIRect Box, Label, Button;
	MainView.VMargin(MainView.w * 0.28f, &Box);
	Box.HMargin(Box.h * 0.2f, &Box);
	Box.Draw(ColorRGBA(0, 0, 0, 0.35f), IGraphics::CORNER_ALL, 8.0f);
	Box.Margin(16.0f, &Box);

	Box.HSplitTop(28.0f, &Label, &Box);
	Ui()->DoLabel(&Label, Localize("Clans account"), 18.0f, TEXTALIGN_MC);

	Box.HSplitTop(12.0f, nullptr, &Box);
	Box.HSplitTop(20.0f, &Label, &Box);
	Ui()->DoLabel(&Label, Localize("Nickname"), 14.0f, TEXTALIGN_ML);
	Box.HSplitTop(4.0f, nullptr, &Box);
	Box.HSplitTop(24.0f, &Button, &Box);
	DoClansEditBox(Ui(), &s_Nick, &Button, 14.0f);

	Box.HSplitTop(10.0f, nullptr, &Box);
	Box.HSplitTop(20.0f, &Label, &Box);
	Ui()->DoLabel(&Label, Localize("Password"), 14.0f, TEXTALIGN_ML);
	Box.HSplitTop(4.0f, nullptr, &Box);
	Box.HSplitTop(24.0f, &Button, &Box);
	DoClansEditBox(Ui(), &s_Pass, &Button, 14.0f);

	Box.HSplitTop(16.0f, nullptr, &Box);
	CUIRect Left, Right;
	Box.HSplitTop(28.0f, &Button, &Box);
	Button.VSplitMid(&Left, &Right, 8.0f);
	static CButtonContainer s_Login, s_Register;
	if(DoClansTextBtn(Ui(), TextRender(), &s_Login, Localize("Login"), &Left) && !Clans.IsBusy())
		Clans.Login(s_aNick, s_aPass);
	if(DoClansTextBtn(Ui(), TextRender(), &s_Register, Localize("Register"), &Right) && !Clans.IsBusy())
		Clans.Register(s_aNick, s_aPass);

}

void CMenus::RenderClansLanding(CUIRect MainView)
{
	CClans &Clans = GameClient()->m_Clans;
	static CLineInput s_Search;
	static CLineInput s_Code;
	static char s_aSearch[64];
	static char s_aCode[32];
	s_Search.SetBuffer(s_aSearch, sizeof(s_aSearch));
	s_Code.SetBuffer(s_aCode, sizeof(s_aCode));
	s_Search.SetEmptyText(Localize("Search clans"));
	s_Code.SetEmptyText(Localize("Join with code"));

	CUIRect Left, Right, Row, Button, Label;
	MainView.VSplitLeft(MainView.w * 0.34f, &Left, &Right);
	Left.VSplitRight(8.0f, &Left, nullptr);

	// Left sidebar — mockup layout
	Left.Draw(ColorRGBA(0, 0, 0, 0.22f), IGraphics::CORNER_ALL, 6.0f);
	Left.Margin(10.0f, &Left);

	// Account (top)
	CUIRect Acc;
	Left.HSplitTop(36.0f, &Acc, &Left);
	DrawClansAccountBarRect(GameClient(), RenderTools(), Ui(), Clans, Acc);
	Left.HSplitTop(8.0f, nullptr, &Left);

	// Search
	CUIRect SearchBox;
	Left.HSplitTop(26.0f, &SearchBox, &Left);
	DoClansEditBox(Ui(), &s_Search, &SearchBox, 13.0f);
	Left.HSplitTop(8.0f, nullptr, &Left);

	// Join with code + Join (one row)
	CUIRect CodeInput, JoinBtn;
	Left.HSplitTop(26.0f, &Row, &Left);
	Row.VSplitRight(64.0f, &CodeInput, &JoinBtn);
	CodeInput.VSplitRight(6.0f, &CodeInput, nullptr);
	DoClansEditBox(Ui(), &s_Code, &CodeInput, 13.0f);
	static CButtonContainer s_JoinCode;
	if(DoClansTextBtn(Ui(), TextRender(), &s_JoinCode, Localize("Join"), &JoinBtn) && s_aCode[0] && !Clans.IsBusy())
		Clans.JoinCode(s_aCode);
	Left.HSplitTop(8.0f, nullptr, &Left);

	// Create clan
	CUIRect CreateBtn;
	Left.HSplitTop(28.0f, &CreateBtn, &Left);
	static CButtonContainer s_Create;
	if(DoClansTextBtn(Ui(), TextRender(), &s_Create, Localize("+ Create clan"), &CreateBtn))
		Clans.SetView(CClans::EView::SETUP);

	if(Clans.InClan())
	{
		Left.HSplitTop(6.0f, nullptr, &Left);
		Left.HSplitTop(26.0f, &Button, &Left);
		static CButtonContainer s_BackClan;
		if(DoClansTextBtn(Ui(), TextRender(), &s_BackClan, Localize("Back to clan"), &Button))
			Clans.SetView(CClans::EView::CLAN);
	}

	Left.HSplitTop(12.0f, nullptr, &Left);

	// Recent clans (fills remaining, Clear + Refresh at bottom)
	CUIRect BottomRow;
	Left.HSplitBottom(28.0f, &Left, &BottomRow);
	Left.HSplitBottom(8.0f, &Left, nullptr);

	Left.HSplitTop(16.0f, &Label, &Left);
	Ui()->DoLabel(&Label, Localize("Recent clans"), 12.0f, TEXTALIGN_ML);
	Left.HSplitTop(4.0f, nullptr, &Left);

	{
		static char s_aRecentForUser[64] = "";
		if(str_comp(s_aRecentForUser, Clans.UserId()) != 0)
		{
			str_copy(s_aRecentForUser, Clans.UserId(), sizeof(s_aRecentForUser));
			if(Clans.UserId()[0])
				Clans.RefreshRecentClans();
		}

		CUIRect ListBox = Left;
		ListBox.Draw(ColorRGBA(0, 0, 0, 0.20f), IGraphics::CORNER_ALL, 4.0f);
		ListBox.Margin(1.0f, &ListBox);

		if(Clans.RecentClans().empty())
		{
			Ui()->DoLabel(&ListBox, Localize("No recent clans yet"), 11.0f, TEXTALIGN_MC);
		}
		else
		{
			static CScrollRegion s_RecentScroll;
			vec2 RecentOffset(0.0f, 0.0f);
			CScrollRegionParams RecentParams;
			s_RecentScroll.Begin(&ListBox, &RecentOffset, &RecentParams);
			ListBox.y += RecentOffset.y;

			static std::vector<CButtonContainer> s_vLandingRejoin;
			if(s_vLandingRejoin.size() < Clans.RecentClans().size())
				s_vLandingRejoin.resize(Clans.RecentClans().size());

			for(size_t Ri = 0; Ri < Clans.RecentClans().size(); Ri++)
			{
				const auto &R = Clans.RecentClans()[Ri];
				CUIRect Item, IconBox, Name, Meta, Act;
				ListBox.HSplitTop(32.0f, &Item, &ListBox);
				if(s_RecentScroll.AddRect(Item))
				{
					Item.Margin(4.0f, &Item);
					Item.VSplitLeft(18.0f, &IconBox, &Item);
					Item.VSplitLeft(4.0f, nullptr, &Item);

					TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
					TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
					Ui()->DoLabel(&IconBox, FontIcon::ICON_USERS, 11.0f, TEXTALIGN_MC);
					TextRender()->SetRenderFlags(0);
					TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

					char aName[80];
					str_format(aName, sizeof(aName), "[%s] %s", R.m_aTag, R.m_aName);

					int Members = -1;
					int MaxMembers = -1;
					for(const auto &Cat : Clans.Catalog())
					{
						if(!str_comp(Cat.m_aClanId, R.m_aClanId))
						{
							Members = Cat.m_MemberCount;
							MaxMembers = Cat.m_MaxMembers;
							break;
						}
					}

					if(!Clans.InClan() && (R.m_WasPresident || CatalogClanIsOpen(Clans, R.m_aClanId)))
					{
						Item.VSplitRight(64.0f, &Item, &Act);
						Ui()->DoLabel(&Item, aName, 12.0f, TEXTALIGN_ML);
						if(DoClansTextBtn(Ui(), TextRender(), &s_vLandingRejoin[Ri], Localize("Return"), &Act) && !Clans.IsBusy())
						{
							if(R.m_WasPresident)
								Clans.RejoinAsPresident(R.m_aClanId);
							else
								Clans.Join(R.m_aClanId);
						}
					}
					else
					{
						if(Members >= 0)
						{
							Item.VSplitRight(48.0f, &Name, &Meta);
							Ui()->DoLabel(&Name, aName, 12.0f, TEXTALIGN_ML);
							char aMeta[24];
							str_format(aMeta, sizeof(aMeta), "%d/%d", Members, MaxMembers);
							Ui()->DoLabel(&Meta, aMeta, 11.0f, TEXTALIGN_MR);
						}
						else
						{
							Ui()->DoLabel(&Item, aName, 12.0f, TEXTALIGN_ML);
						}
						if(Ui()->DoButtonLogic(&Clans.RecentClans()[Ri], 0, &Item, BUTTONFLAG_LEFT))
						{
							if(Clans.InClan() && !str_comp(R.m_aClanId, Clans.ClanId()))
								Clans.SetView(CClans::EView::CLAN);
							else
								Clans.OpenPreview(R.m_aClanId);
						}
					}
				}
				// separator
				CUIRect Sep;
				ListBox.HSplitTop(1.0f, &Sep, &ListBox);
				if(Ri + 1 < Clans.RecentClans().size())
					Sep.Draw(ColorRGBA(1, 1, 1, 0.08f), IGraphics::CORNER_NONE, 0.0f);
			}
			s_RecentScroll.End();
		}
	}

	CUIRect ClearRow, RefreshRow;
	BottomRow.VSplitMid(&ClearRow, &RefreshRow, 6.0f);
	static CButtonContainer s_ClearRecent, s_RefreshList;
	if(DoClansTextBtn(Ui(), TextRender(), &s_ClearRecent, Localize("Clear"), &ClearRow) && !Clans.IsBusy() && !Clans.RecentClans().empty())
		Clans.ClearRecentClans();
	if(DoClansTextBtn(Ui(), TextRender(), &s_RefreshList, Localize("Refresh list"), &RefreshRow) && !Clans.IsBusy())
		Clans.RefreshCurrentView();

	static bool s_ShowApplyPopup = false;
	static char s_aApplyClanId[64] = "";
	static char s_aApplyText[288] = "";
	static CLineInput s_ApplyInput;
	s_ApplyInput.SetBuffer(s_aApplyText, sizeof(s_aApplyText));
	if(s_ShowApplyPopup)
	{
		CUIRect Overlay = *Ui()->Screen();
		Overlay.Draw(ColorRGBA(0, 0, 0, 0.55f), IGraphics::CORNER_ALL, 0.0f);
		CUIRect Popup;
		MainView.VMargin(MainView.w * 0.15f, &Popup);
		Popup.HMargin(Popup.h * 0.22f, &Popup);
		Popup.Draw(ColorRGBA(0.12f, 0.12f, 0.12f, 0.98f), IGraphics::CORNER_ALL, 8.0f);
		Popup.Margin(14.0f, &Popup);
		Popup.HSplitTop(24.0f, &Label, &Popup);
		Ui()->DoLabel(&Label, Localize("Application text"), 16.0f, TEXTALIGN_MC);
		Popup.HSplitTop(8.0f, nullptr, &Popup);
		Popup.HSplitTop(80.0f, &Button, &Popup);
		DoClansEditBox(Ui(), &s_ApplyInput, &Button, 14.0f);
		Popup.HSplitTop(12.0f, nullptr, &Popup);
		Popup.HSplitTop(28.0f, &Row, &Popup);
		CUIRect Send, Cancel;
		Row.VSplitMid(&Send, &Cancel, 8.0f);
		static CButtonContainer s_SendApply, s_CancelApply;
		if(DoClansTextBtn(Ui(), TextRender(), &s_SendApply, Localize("Send"), &Send) && !Clans.IsBusy())
		{
			Clans.Apply(s_aApplyClanId, s_aApplyText);
			s_ShowApplyPopup = false;
			s_aApplyText[0] = '\0';
		}
		if(DoClansTextBtn(Ui(), TextRender(), &s_CancelApply, Localize("Cancel"), &Cancel))
		{
			s_ShowApplyPopup = false;
			s_aApplyText[0] = '\0';
		}
		return;
	}

	// Catalog list — separate visual block
	Right.Draw(ColorRGBA(0, 0, 0, 0.22f), IGraphics::CORNER_ALL, 6.0f);
	Right.Margin(10.0f, &Right);

	Right.HSplitTop(22.0f, &Label, &Right);
	Ui()->DoLabel(&Label, Localize("Clan catalog"), 15.0f, TEXTALIGN_ML);
	Right.HSplitTop(6.0f, nullptr, &Right);

	static CScrollRegion s_Scroll;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams Params;
	s_Scroll.Begin(&Right, &ScrollOffset, &Params);
	Right.y += ScrollOffset.y;

	for(size_t Index = 0; Index < Clans.Catalog().size(); Index++)
	{
		const auto &Entry = Clans.Catalog()[Index];
		if(s_aSearch[0] &&
			!str_find_nocase(Entry.m_aName, s_aSearch) &&
			!str_find_nocase(Entry.m_aTag, s_aSearch) &&
			!str_find_nocase(Entry.m_aDescription, s_aSearch))
		{
			continue;
		}
		CUIRect Item;
		Right.HSplitTop(36.0f, &Item, &Right);
		const bool Visible = s_Scroll.AddRect(Item);
		if(Visible)
		{
			Item.Draw(ColorRGBA(1, 1, 1, 0.04f), IGraphics::CORNER_ALL, 3.0f);
			Item.Margin(4.0f, &Item);
			CUIRect IconBox, FlagBox, Name, Meta;
			Item.VSplitLeft(28.0f, &IconBox, &Item);
			Item.VSplitLeft(4.0f, nullptr, &Item);
			Item.VSplitLeft(36.0f, &FlagBox, &Item);
			Item.VSplitLeft(4.0f, nullptr, &Item);
			Item.VSplitRight(100.0f, &Name, &Meta);
			RenderClanIcon(Ui(), TextRender(), IconBox, Entry.m_IconId, Entry.m_Color);
			RenderClanFlag(GameClient(), FlagBox, Entry.m_Country);
			char aLine[96];
			str_format(aLine, sizeof(aLine), "[%s] %s", Entry.m_aTag, Entry.m_aName);
			Ui()->DoLabel(&Name, aLine, 13.0f, TEXTALIGN_ML);
			str_format(aLine, sizeof(aLine), "%d/%d · %s", Entry.m_OnlineCount, Entry.m_MemberCount, Entry.m_aJoinPolicy);
			Ui()->DoLabel(&Meta, aLine, 11.0f, TEXTALIGN_MR);
			if(Ui()->DoButtonLogic(&Clans.Catalog()[Index], 0, &Item, BUTTONFLAG_LEFT))
			{
				if(Clans.InClan() && !str_comp(Entry.m_aClanId, Clans.ClanId()))
					Clans.SetView(CClans::EView::CLAN);
				else
					Clans.OpenPreview(Entry.m_aClanId);
			}
		}
		Right.HSplitTop(3.0f, nullptr, &Right);
	}
	s_Scroll.End();
}

static unsigned ClanRgbFromHsla(unsigned HslaPacked)
{
	const ColorRGBA Rgba = color_cast<ColorRGBA>(ColorHSLA(HslaPacked, false));
	const unsigned R = (unsigned)(std::clamp(Rgba.r, 0.0f, 1.0f) * 255.0f + 0.5f);
	const unsigned G = (unsigned)(std::clamp(Rgba.g, 0.0f, 1.0f) * 255.0f + 0.5f);
	const unsigned B = (unsigned)(std::clamp(Rgba.b, 0.0f, 1.0f) * 255.0f + 0.5f);
	return (R << 16) | (G << 8) | B;
}

void CMenus::RenderClansSetup(CUIRect MainView)
{
	CClans &Clans = GameClient()->m_Clans;
	static CLineInput s_Name, s_Tag, s_Desc;
	static char s_aName[64], s_aTag[16], s_aDesc[256];
	static int s_Policy = 0;
	static int s_MaxMembers = 50;
	static int s_IconId = 7; // crown-ish default
	static int s_Country = -1;
	static unsigned s_ColorHsla = 0;
	static bool s_Init = false;
	if(!s_Init)
	{
		s_aName[0] = s_aTag[0] = s_aDesc[0] = '\0';
		str_copy(s_aTag, g_Config.m_PlayerClan, sizeof(s_aTag));
		str_copy(s_aName, g_Config.m_PlayerClan, sizeof(s_aName));
		s_IconId = 7;
		s_Country = g_Config.m_PlayerCountry;
		s_ColorHsla = color_cast<ColorHSLA>(ColorRGBA(0.33f, 0.55f, 1.0f, 1.0f)).Pack(false);
		s_Init = true;
	}
	s_Name.SetBuffer(s_aName, sizeof(s_aName));
	s_Tag.SetBuffer(s_aTag, sizeof(s_aTag));
	s_Desc.SetBuffer(s_aDesc, sizeof(s_aDesc));
	s_Name.SetEmptyText(Localize("Clan name"));
	s_Tag.SetEmptyText(Localize("Tag"));
	s_Desc.SetEmptyText(Localize("Description"));
	s_Desc.SetAllowNewline(true);

	const unsigned ClanRgb = ClanRgbFromHsla(s_ColorHsla);
	static const char *apPolicy[] = {"open", "request", "closed"};
	const char *pPolicyLabel = Localize("Open");
	if(s_Policy == 1)
		pPolicyLabel = Localize("Request");
	else if(s_Policy == 2)
		pPolicyLabel = Localize("Closed");

	CUIRect Left, Right, Label, Button, Row;
	MainView.VSplitLeft(MainView.w * 0.58f, &Left, &Right);
	Left.VSplitRight(10.0f, &Left, nullptr);

	const float DescFont = 11.0f;
	const float DescSpacing = 1.0f;
	const float PreviewDescW = maximum(1.0f, Right.w - 32.0f);

	Left.Draw(ColorRGBA(0, 0, 0, 0.28f), IGraphics::CORNER_ALL, 6.0f);
	Left.Margin(12.0f, &Left);

	// Bottom actions first so form uses remaining space
	CUIRect Actions;
	Left.HSplitBottom(40.0f, &Left, &Actions);
	Left.HSplitBottom(10.0f, &Left, nullptr);
	CUIRect CreateBtn, BackBtn;
	Actions.VSplitMid(&CreateBtn, &BackBtn, 8.0f);
	static CButtonContainer s_Create, s_Back;
	if(DoClansTextBtn(Ui(), TextRender(), &s_Create, Localize("Create clan"), &CreateBtn) && s_aName[0] && s_aTag[0] && !Clans.IsBusy())
	{
		NormalizeClanDescription(s_aDesc, sizeof(s_aDesc));
		Clans.CreateClan(s_aName, s_aTag, s_aDesc, s_IconId, ClanRgb, s_Country, apPolicy[s_Policy], s_MaxMembers);
	}
	if(DoClansTextBtn(Ui(), TextRender(), &s_Back, Localize("Back"), &BackBtn))
		Clans.SetView(Clans.InClan() ? CClans::EView::CLAN : CClans::EView::LANDING);

	// Name + Tag row
	CUIRect NameCol, TagCol;
	Left.HSplitTop(16.0f, &Label, &Left);
	Label.VSplitMid(&NameCol, &TagCol, 8.0f);
	Ui()->DoLabel(&NameCol, Localize("Name"), 12.0f, TEXTALIGN_ML);
	Ui()->DoLabel(&TagCol, Localize("Tag"), 12.0f, TEXTALIGN_ML);
	Left.HSplitTop(4.0f, nullptr, &Left);
	Left.HSplitTop(26.0f, &Row, &Left);
	Row.VSplitMid(&NameCol, &TagCol, 8.0f);
	DoClansEditBox(Ui(), &s_Name, &NameCol, 13.0f);
	DoClansEditBox(Ui(), &s_Tag, &TagCol, 13.0f);

	Left.HSplitTop(14.0f, nullptr, &Left);
	Left.HSplitTop(14.0f, &Label, &Left);
	Ui()->DoLabel(&Label, Localize("Description"), 12.0f, TEXTALIGN_ML);
	Left.HSplitTop(4.0f, nullptr, &Left);
	Left.HSplitTop(56.0f, &Button, &Left);
	{
		if(DoClansDescEditBox(Ui(), TextRender(), &s_Desc, &Button, DescFont, DescSpacing))
			NormalizeClanDescription(s_aDesc, sizeof(s_aDesc));
	}

	Left.HSplitTop(8.0f, nullptr, &Left);
	Left.HSplitTop(24.0f, &Button, &Left);
	static CButtonContainer s_UseCurrent;
	if(DoClansTextBtn(Ui(), TextRender(), &s_UseCurrent, Localize("Use current tee clan"), &Button))
	{
		str_copy(s_aTag, g_Config.m_PlayerClan, sizeof(s_aTag));
		str_copy(s_aName, g_Config.m_PlayerClan, sizeof(s_aName));
	}

	Left.HSplitTop(10.0f, nullptr, &Left);
	Left.HSplitTop(16.0f, &Label, &Left);
	Ui()->DoLabel(&Label, Localize("Icon and color"), 12.0f, TEXTALIGN_ML);
	Left.HSplitTop(4.0f, nullptr, &Left);
	Left.HSplitTop(32.0f, &Row, &Left);
	CUIRect IconsRow, ColorCol;
	Row.VSplitRight(100.0f, &IconsRow, &ColorCol);
	ColorCol.VSplitLeft(8.0f, nullptr, &ColorCol);
	static CButtonContainer s_aIcons[NUM_CLAN_ICONS];
	const float IconCell = IconsRow.h + 4.0f;
	RenderClanIconPickerRow(Ui(), TextRender(), IconsRow, 0, 8, &s_IconId, ClanRgb, IconCell, s_aIcons);
	{
		CUIRect ColorBtn, ResetBtn;
		ColorCol.VSplitRight(56.0f, &ColorCol, &ResetBtn);
		ColorCol.VSplitRight(4.0f, &ColorCol, nullptr);
		const float Sq = minimum(ColorCol.w, ColorCol.h);
		ColorCol.VMargin(maximum(0.0f, (ColorCol.w - Sq) * 0.5f), &ColorBtn);
		ColorBtn.HMargin(maximum(0.0f, (ColorBtn.h - Sq) * 0.5f), &ColorBtn);
		DoButton_ColorPicker(&ColorBtn, &s_ColorHsla, false);
		static CButtonContainer s_ColorReset;
		ResetBtn.HMargin(2.0f, &ResetBtn);
		if(DoClansTextBtn(Ui(), TextRender(), &s_ColorReset, Localize("Reset"), &ResetBtn))
			s_ColorHsla = color_cast<ColorHSLA>(ColorRGBA(0.33f, 0.55f, 1.0f, 1.0f)).Pack(false);
	}
	Left.HSplitTop(4.0f, nullptr, &Left);
	Left.HSplitTop(32.0f, &Row, &Left);
	Row.VSplitRight(100.0f, &IconsRow, &ColorCol);
	ColorCol.VSplitLeft(8.0f, nullptr, &ColorCol);
	RenderClanIconPickerRow(Ui(), TextRender(), IconsRow, 8, 8, &s_IconId, ClanRgb, IconCell, s_aIcons);
	{
		// Flag sits under the color picker in the spare column.
		CUIRect FlagButton = ColorCol;
		const float FlagW = minimum(FlagButton.w, FlagButton.h * 2.0f);
		FlagButton.x += (FlagButton.w - FlagW) * 0.5f;
		FlagButton.w = FlagW;
		static CButtonContainer s_FlagButton;
		if(DoButton_Menu(&s_FlagButton, "", 0, &FlagButton))
		{
			static SPopupMenuId s_PopupCountryId;
			static SPopupSettingsCountrySelectionContext s_PopupCountryContext;
			s_PopupCountryContext.m_pMenus = this;
			s_PopupCountryContext.m_pCountry = &s_Country;
			s_PopupCountryContext.m_Selection = s_Country;
			s_PopupCountryContext.m_New = true;
			Ui()->DoPopupMenu(&s_PopupCountryId, FlagButton.x, FlagButton.y + FlagButton.h, 490.0f, 210.0f, &s_PopupCountryContext, PopupSettingsCountrySelection);
		}
		GameClient()->m_Tooltips.DoToolTip(&s_FlagButton, &FlagButton, Localize("Choose country flag"));
		GameClient()->m_CountryFlags.Render(s_Country, ColorRGBA(1.0f, 1.0f, 1.0f, Ui()->HotItem() == &s_FlagButton ? 1.0f : 0.85f), FlagButton.x, FlagButton.y, FlagButton.w, FlagButton.h);
	}
	Left.HSplitTop(4.0f, nullptr, &Left);
	Left.HSplitTop(32.0f, &Row, &Left);
	RenderClanIconPickerRow(Ui(), TextRender(), Row, 16, 8, &s_IconId, ClanRgb, IconCell, s_aIcons);

	Left.HSplitTop(10.0f, nullptr, &Left);
	Left.HSplitTop(16.0f, &Label, &Left);
	Ui()->DoLabel(&Label, Localize("Join policy"), 12.0f, TEXTALIGN_ML);
	Left.HSplitTop(4.0f, nullptr, &Left);

	const float PolicyBtnH = 26.0f;
	static CButtonContainer s_Open, s_Req, s_Closed;
	CUIRect PolicyBtn;
	Left.HSplitTop(PolicyBtnH, &PolicyBtn, &Left);
	if(DoClansTextBtn(Ui(), TextRender(), &s_Open, Localize("Open"), &PolicyBtn, s_Policy == 0))
		s_Policy = 0;
	Left.HSplitTop(4.0f, nullptr, &Left);
	Left.HSplitTop(PolicyBtnH, &PolicyBtn, &Left);
	if(DoClansTextBtn(Ui(), TextRender(), &s_Req, Localize("Request"), &PolicyBtn, s_Policy == 1))
		s_Policy = 1;
	Left.HSplitTop(4.0f, nullptr, &Left);
	Left.HSplitTop(PolicyBtnH, &PolicyBtn, &Left);
	if(DoClansTextBtn(Ui(), TextRender(), &s_Closed, Localize("Closed"), &PolicyBtn, s_Policy == 2))
		s_Policy = 2;

	Left.HSplitTop(10.0f, nullptr, &Left);
	Left.HSplitTop(16.0f, &Label, &Left);
	{
		CUIRect MaxTitle, MaxValue;
		Label.VSplitRight(40.0f, &MaxTitle, &MaxValue);
		Ui()->DoLabel(&MaxTitle, Localize("Max members"), 12.0f, TEXTALIGN_ML);
		char aMaxBuf[16];
		str_format(aMaxBuf, sizeof(aMaxBuf), "%d", s_MaxMembers);
		Ui()->DoLabel(&MaxValue, aMaxBuf, 12.0f, TEXTALIGN_MR);
	}
	Left.HSplitTop(4.0f, nullptr, &Left);
	Left.HSplitTop(22.0f, &Button, &Left);
	{
		const float Rel = (s_MaxMembers - 2) / 198.0f;
		const float NewRel = Ui()->DoScrollbarH(&s_MaxMembers, &Button, Rel);
		s_MaxMembers = 2 + (int)(NewRel * 198.0f + 0.5f);
	}

	// ---- Preview panel ----
	Right.Draw(ColorRGBA(0, 0, 0, 0.28f), IGraphics::CORNER_ALL, 6.0f);
	Right.Margin(12.0f, &Right);
	Right.HSplitTop(16.0f, &Label, &Right);
	Ui()->DoLabel(&Label, Localize("PREVIEW"), 12.0f, TEXTALIGN_ML);
	Right.HSplitTop(8.0f, nullptr, &Right);

	CUIRect Card = Right;
	Card.Margin(4.0f, &Card);

	CUIRect Header, IconBox, FlagBox, TextCol;
	Card.HSplitTop(40.0f, &Header, &Card);
	Header.VSplitLeft(36.0f, &IconBox, &TextCol);
	TextCol.VSplitLeft(8.0f, nullptr, &TextCol);
	TextCol.VSplitRight(44.0f, &TextCol, &FlagBox);
	IconBox.Draw(ColorRGBA(0, 0, 0, 0.20f), IGraphics::CORNER_ALL, 4.0f);
	RenderClanIcon(Ui(), TextRender(), IconBox, s_IconId, ClanRgb);
	RenderClanFlag(GameClient(), FlagBox, s_Country);

	CUIRect TitleR, MetaR;
	TextCol.HSplitMid(&TitleR, &MetaR);
	char aTitle[96];
	const char *pName = s_aName[0] ? s_aName : Localize("Clan name");
	const char *pTag = s_aTag[0] ? s_aTag : "???";
	str_format(aTitle, sizeof(aTitle), "[%s] %s", pTag, pName);
	Ui()->DoLabel(&TitleR, aTitle, 14.0f, TEXTALIGN_ML);
	str_format(aTitle, sizeof(aTitle), "%s · 0/%d", pPolicyLabel, s_MaxMembers);
	Ui()->DoLabel(&MetaR, aTitle, 11.0f, TEXTALIGN_ML);

	Card.HSplitTop(8.0f, nullptr, &Card);
	CUIRect Sep;
	Card.HSplitTop(1.0f, &Sep, &Card);
	Sep.Draw(ColorRGBA(1, 1, 1, 0.10f), IGraphics::CORNER_NONE, 0.0f);
	Card.HSplitTop(10.0f, nullptr, &Card);

	{
		const char *pDesc = s_aDesc[0] ? s_aDesc : Localize("This is how your clan will appear in the catalog list to other players.");
		const int LineCount = CountClanDescriptionLines(pDesc);
		const float DescH = LineCount * (DescFont + DescSpacing) + 4.0f;
		CUIRect DescBox;
		Card.HSplitTop(DescH, &DescBox, &Card);
		if(DescBox.w > PreviewDescW)
			DescBox.w = PreviewDescW;
		DoClanDescriptionCentered(Ui(), TextRender(), DescBox, pDesc, DescFont, DescSpacing);
	}

	Card.HSplitTop(6.0f, nullptr, &Card);
	CUIRect FootIcon, FootText;
	Card.HSplitTop(18.0f, &Row, &Card);
	Row.VSplitLeft(16.0f, &FootIcon, &FootText);
	FootText.VSplitLeft(4.0f, nullptr, &FootText);
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	Ui()->DoLabel(&FootIcon, FontIcon::ICON_USERS, 11.0f, TEXTALIGN_MC);
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	str_format(aTitle, sizeof(aTitle), Localize("Up to %d members"), s_MaxMembers);
	Ui()->DoLabel(&FootText, aTitle, 11.0f, TEXTALIGN_ML);

	Card.HSplitTop(10.0f, nullptr, &Card);
	Card.HSplitTop(1.0f, &Sep, &Card);
	Sep.Draw(ColorRGBA(1, 1, 1, 0.10f), IGraphics::CORNER_NONE, 0.0f);
	Card.HSplitTop(8.0f, nullptr, &Card);

	// You as president (preview member row)
	CUIRect MemberRow, TeeBox, MemberText, OnlineBox;
	Card.HSplitTop(36.0f, &MemberRow, &Card);
	MemberRow.Draw(ColorRGBA(0, 0, 0, 0.18f), IGraphics::CORNER_ALL, 4.0f);
	MemberRow.Margin(4.0f, &MemberRow);
	MemberRow.VSplitLeft(28.0f, &TeeBox, &MemberText);
	MemberText.VSplitLeft(6.0f, nullptr, &MemberText);
	MemberText.VSplitRight(52.0f, &MemberText, &OnlineBox);
	RenderOwnTee(GameClient(), RenderTools(), TeeBox);

	CUIRect NickR, RoleR;
	MemberText.HSplitMid(&NickR, &RoleR);
	Ui()->DoLabel(&NickR, Clans.Nickname()[0] ? Clans.Nickname() : g_Config.m_PlayerName, 12.0f, TEXTALIGN_ML);
	Ui()->DoLabel(&RoleR, Localize("President"), 10.0f, TEXTALIGN_ML);

	CUIRect OnlineDot, OnlineLbl;
	OnlineBox.VSplitLeft(10.0f, &OnlineDot, &OnlineLbl);
	OnlineDot.Margin(2.0f, &OnlineDot);
	OnlineDot.HMargin(maximum(0.0f, (OnlineDot.h - OnlineDot.w) * 0.5f), &OnlineDot);
	OnlineDot.Draw(ColorRGBA(0.35f, 0.9f, 0.4f, 1.0f), IGraphics::CORNER_ALL, OnlineDot.w * 0.5f);
	Ui()->DoLabel(&OnlineLbl, Localize("Online"), 10.0f, TEXTALIGN_MR);
}

void CMenus::RenderClansPage(CUIRect MainView)
{
	CClans &Clans = GameClient()->m_Clans;
	const auto &Clan = Clans.Clan();

	static bool s_ShowDisband = false;
	static int s_aDisbandOrder[4] = {0, 1, 2, 3};
	static int s_MemberMenu = -1;
	static vec2 s_MemberMenuPos;
	static float s_MemberMenuPhase = 0.0f;
	static bool s_MemberMenuClosing = false;
	static bool s_MemberMenuJustOpened = false;
	static int s_KickUser = -1;
	static int s_BanHours = 24;
	static char s_aKickUserId[64] = "";
	static std::vector<CButtonContainer> s_vMemberSettings;

	CUIRect Left, Right, Label, Button, Row;
	MainView.VSplitLeft(MainView.w * 0.36f, &Left, &Right);
	Left.VSplitRight(8.0f, &Left, nullptr);
	Left.Margin(6.0f, &Left);

	int OnlineMain = 0;
	for(const auto &M : Clan.m_vMembers)
		if(M.m_Online)
			OnlineMain++;
	std::vector<CClans::SUnleashedPlayer> vUnleashed;
	Clans.CollectUnleashed(&vUnleashed);
	const int OnlineTotal = OnlineMain + (int)vUnleashed.size();
	const int MemberCount = (int)Clan.m_vMembers.size();
	const int MaxMembers = Clan.m_MaxMembers > 0 ? Clan.m_MaxMembers : MemberCount;

	const bool CanApps = !str_comp(Clan.m_aJoinPolicy, "request") &&
			     (Clans.Role() == CClans::ERole::PRESIDENT || Clans.Role() == CClans::ERole::VICE_PRESIDENT || Clans.Role() == CClans::ERole::VETERAN);
	const bool IsPresident = Clans.Role() == CClans::ERole::PRESIDENT;
	const bool CanSettings = Clans.Role() == CClans::ERole::PRESIDENT || Clans.Role() == CClans::ERole::VICE_PRESIDENT;
	const bool CanInvite = Clan.m_HasInviteCode && (Clans.Role() == CClans::ERole::PRESIDENT || Clans.Role() == CClans::ERole::VICE_PRESIDENT);
	const int ManageCount = 3 + (CanApps ? 1 : 0) + (CanSettings ? 1 : 0);
	const int DangerCount = 1 + (IsPresident ? 1 : 0);

	CUIRect InfoBlock, IconBox;
	const float DescFont = 11.0f;
	const float DescSpacing = 1.0f;
	const int DescLines = CountClanDescriptionLines(Clan.m_aDescription);
	const float DescBlockH = DescLines * (DescFont + DescSpacing) + 4.0f;
	const float InfoBlockH = 34.0f + 18.0f + 25.0f + 18.0f + 4.0f + DescBlockH + 20.0f + 16.0f;
	Left.HSplitTop(InfoBlockH, &InfoBlock, &Left);
	InfoBlock.Draw(ColorRGBA(0, 0, 0, 0.20f), IGraphics::CORNER_ALL, 6.0f);
	InfoBlock.Margin(8.0f, &InfoBlock);

	CUIRect CenterIcon, TitleRow, FlagRow;
	InfoBlock.HSplitTop(34.0f, &CenterIcon, &InfoBlock);
	CenterIcon.VMargin(maximum(0.0f, (CenterIcon.w - 34.0f) * 0.5f), &IconBox);
	RenderClanIcon(Ui(), TextRender(), IconBox, Clan.m_IconId, Clan.m_Color);
	InfoBlock.HSplitTop(18.0f, &FlagRow, &InfoBlock);
	FlagRow.VMargin(maximum(0.0f, (FlagRow.w - 36.0f) * 0.5f), &FlagRow);
	RenderClanFlag(GameClient(), FlagRow, Clan.m_Country);
	InfoBlock.HSplitTop(25.0f, &TitleRow, &InfoBlock);
	Ui()->DoLabel(&TitleRow, Clan.m_aName[0] ? Clan.m_aName : Localize("Loading..."), 18.0f, TEXTALIGN_MC);

	InfoBlock.HSplitTop(18.0f, &Label, &InfoBlock);
	char aMeta[96];
	str_format(aMeta, sizeof(aMeta), "[%s] · %s", Clan.m_aTag, Clan.m_aJoinPolicy);
	TextRender()->TextColor(ColorRGBA(0.65f, 0.65f, 0.65f, 1.0f));
	Ui()->DoLabel(&Label, aMeta, 13.0f, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	InfoBlock.HSplitTop(4.0f, nullptr, &InfoBlock);
	InfoBlock.HSplitTop(DescBlockH, &Label, &InfoBlock);
	DoClanDescriptionCentered(Ui(), TextRender(), Label, Clan.m_aDescription, DescFont, DescSpacing);

	CUIRect MembersInfo, OnlineInfo;
	InfoBlock.HSplitTop(20.0f, &Label, &InfoBlock);
	Label.VSplitMid(&MembersInfo, &OnlineInfo, 8.0f);
	str_format(aMeta, sizeof(aMeta), Localize("Members %d/%d"), MemberCount, MaxMembers);
	Ui()->DoLabel(&MembersInfo, aMeta, 12.0f, TEXTALIGN_MC);
	str_format(aMeta, sizeof(aMeta), Localize("Online %d/%d"), OnlineTotal, MemberCount);
	Ui()->DoLabel(&OnlineInfo, aMeta, 12.0f, TEXTALIGN_MC);
	Left.HSplitTop(10.0f, nullptr, &Left);

	if(CanInvite)
	{
		Left.HSplitTop(10.0f, nullptr, &Left);
		Left.HSplitTop(18.0f, &Label, &Left);
		str_format(aMeta, sizeof(aMeta), "%s: %s", Localize("Invite"), Clan.m_aInviteCode);
		Ui()->DoLabel(&Label, aMeta, 12.0f, TEXTALIGN_ML);
		Left.HSplitTop(4.0f, nullptr, &Left);
		Left.HSplitTop(28.0f, &Row, &Left);
		CUIRect Copy, Rotate;
		Row.VSplitMid(&Copy, &Rotate, 6.0f);
		static CButtonContainer s_Copy, s_Rotate;
		if(DoClansTextBtn(Ui(), TextRender(), &s_Copy, Localize("Copy"), &Copy))
			Input()->SetClipboardText(Clan.m_aInviteCode);
		if(DoClansTextBtn(Ui(), TextRender(), &s_Rotate, Localize("Rotate"), &Rotate) && !Clans.IsBusy())
			Clans.RotateInvite();
	}

	Left.HSplitTop(12.0f, nullptr, &Left);

	// MANAGE + DANGER — same size as main menu (40px + 5px gap)
	constexpr float ButtonH = 40.0f;
	constexpr float ButtonGap = 5.0f;
	const float HeaderH = 18.0f + 4.0f;
	const float GapManageDanger = 12.0f;
	const float ManageBlockH = HeaderH + (float)ManageCount * ButtonH + (float)maximum(0, ManageCount - 1) * ButtonGap;
	const float DangerBlockH = HeaderH + (float)DangerCount * ButtonH + (float)maximum(0, DangerCount - 1) * ButtonGap;
	const float BottomH = ManageBlockH + GapManageDanger + DangerBlockH;

	CUIRect Bottom;
	Left.HSplitBottom(BottomH, &Left, &Bottom);

	DrawClansSectionHeader(Ui(), TextRender(), &Bottom, Localize("MANAGE"));
	static CButtonContainer s_Refresh, s_Catalog, s_Ann, s_Apps, s_Settings;
	Bottom.HSplitTop(ButtonH, &Button, &Bottom);
	if(DoClansSidebarItem(Ui(), TextRender(), &s_Refresh, FontIcon::ARROW_ROTATE_RIGHT, Localize("Refresh"), &Button) && !Clans.IsBusy())
		Clans.RefreshCurrentView();
	Bottom.HSplitTop(ButtonGap, nullptr, &Bottom);
	Bottom.HSplitTop(ButtonH, &Button, &Bottom);
	if(DoClansSidebarItem(Ui(), TextRender(), &s_Catalog, FontIcon::LIST_UL, Localize("Clan list"), &Button))
	{
		Clans.RefreshCatalog();
		Clans.SetView(CClans::EView::BROWSE);
	}
	if(CanApps)
	{
		Bottom.HSplitTop(ButtonGap, nullptr, &Bottom);
		Bottom.HSplitTop(ButtonH, &Button, &Bottom);
		const int AppBadge = (int)Clans.Applications().size();
		if(DoClansSidebarItem(Ui(), TextRender(), &s_Apps, FontIcon::COMMENT, Localize("Applications"), &Button, AppBadge))
		{
			Clans.RefreshApplications();
			Clans.SetView(CClans::EView::APPLICATIONS);
		}
	}
	Bottom.HSplitTop(ButtonGap, nullptr, &Bottom);
	Bottom.HSplitTop(ButtonH, &Button, &Bottom);
	{
		const int Unread = Clans.UnreadAnnouncements();
		if(DoClansSidebarItem(Ui(), TextRender(), &s_Ann, FontIcon::COMMENT, Localize("Chat"), &Button, Unread))
		{
			Clans.RefreshAnnouncements();
			Clans.SetView(CClans::EView::ANNOUNCEMENTS);
		}
	}
	if(CanSettings)
	{
		Bottom.HSplitTop(ButtonGap, nullptr, &Bottom);
		Bottom.HSplitTop(ButtonH, &Button, &Bottom);
		if(DoClansSidebarItem(Ui(), TextRender(), &s_Settings, FontIcon::GEAR, Localize("Settings"), &Button))
			Clans.SetView(CClans::EView::SETTINGS);
	}

	Bottom.HSplitTop(GapManageDanger, nullptr, &Bottom);
	DrawClansSectionHeader(Ui(), TextRender(), &Bottom, Localize("DANGER ZONE"));
	Bottom.HSplitTop(ButtonH, &Button, &Bottom);
	static CButtonContainer s_Leave;
	if(DoClansSidebarItem(Ui(), TextRender(), &s_Leave, FontIcon::RIGHT_FROM_BRACKET, Localize("Leave"), &Button) && !Clans.IsBusy())
		Clans.Leave();
	if(IsPresident)
	{
		Bottom.HSplitTop(ButtonGap, nullptr, &Bottom);
		Bottom.HSplitTop(ButtonH, &Button, &Bottom);
		static CButtonContainer s_Disband;
		if(DoClansSidebarItem(Ui(), TextRender(), &s_Disband, FontIcon::TRASH, Localize("Disband"), &Button, 0, true) && !Clans.IsBusy())
		{
			s_ShowDisband = true;
			for(int i = 0; i < 4; i++)
				s_aDisbandOrder[i] = i;
			for(int i = 3; i > 0; i--)
			{
				const int j = rand() % (i + 1);
				std::swap(s_aDisbandOrder[i], s_aDisbandOrder[j]);
			}
		}
	}

	CUIRect MainSection, UnleashedSection;
	Right.HSplitMid(&MainSection, &UnleashedSection, 8.0f);

	MainSection.Draw(ColorRGBA(0, 0, 0, 0.20f), IGraphics::CORNER_ALL, 6.0f);
	MainSection.Margin(6.0f, &MainSection);

	MainSection.HSplitTop(22.0f, &Label, &MainSection);
	char aMainHead[64];
	str_format(aMainHead, sizeof(aMainHead), "%s (%d)", Localize("Main"), MemberCount);
	Ui()->DoLabel(&Label, aMainHead, 15.0f, TEXTALIGN_ML);
	MainSection.HSplitTop(4.0f, nullptr, &MainSection);

	static CScrollRegion s_MainScroll;
	vec2 MainScrollOffset(0.0f, 0.0f);
	CScrollRegionParams MainParams;
	MainParams.m_ClipBgColor = ColorRGBA(0, 0, 0, 0.0f);
	s_MainScroll.Begin(&MainSection, &MainScrollOffset, &MainParams);
	MainSection.y += MainScrollOffset.y;

	size_t MemberIndex = 0;
	static int s_aMemberIds[128];
	static int s_aMemberJoinIds[128];
	if(s_vMemberSettings.size() < Clan.m_vMembers.size())
		s_vMemberSettings.resize(Clan.m_vMembers.size());
	for(const auto &M : Clan.m_vMembers)
	{
		CUIRect Item;
		MainSection.HSplitTop(42.0f, &Item, &MainSection);
		if(s_MainScroll.AddRect(Item))
		{
			const bool IsSelf = !str_comp(M.m_aUserId, Clans.UserId());
			const bool CanJoin = !IsSelf && M.m_aServer[0] != '\0';
			const bool JoinHot = CanJoin && MemberIndex < std::size(s_aMemberJoinIds) && Ui()->HotItem() == &s_aMemberJoinIds[MemberIndex];
			Item.Draw(ColorRGBA(1, 1, 1, JoinHot ? 0.10f : 0.04f), IGraphics::CORNER_ALL, 3.0f);
			Item.Margin(4.0f, &Item);
			CUIRect Tee, Name, Status, SettingsBtn;
			Item.VSplitLeft(36.0f, &Tee, &Item);
			Item.VSplitLeft(4.0f, nullptr, &Item);

			const bool CanModerate = !IsSelf && (Clans.Role() == CClans::ERole::PRESIDENT || Clans.Role() == CClans::ERole::VICE_PRESIDENT || Clans.Role() == CClans::ERole::VETERAN);
			if(CanModerate)
			{
				Item.VSplitRight(28.0f, &Item, &SettingsBtn);
				Item.VSplitRight(4.0f, &Item, nullptr);
			}
			Item.VSplitRight(200.0f, &Name, &Status);
			RenderClanMemberTee(GameClient(), RenderTools(), Tee, M.m_Skin);
			CUIRect NickRow, RoleRow;
			Name.HSplitMid(&NickRow, &RoleRow);
			Ui()->DoLabel(&NickRow, M.m_aNickname, 12.0f, TEXTALIGN_ML);
			TextRender()->TextColor(ColorRGBA(0.55f, 0.55f, 0.55f, 1.0f));
			Ui()->DoLabel(&RoleRow, RoleLabel(M.m_Role), 9.0f, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			if(M.m_Online)
			{
				char aSt[128];
				if(M.m_aMap[0])
					FormatClanPlayingStatus(aSt, sizeof(aSt), M.m_aMap, M.m_Players, M.m_MaxPlayers);
				else
					str_copy(aSt, Localize("online"), sizeof(aSt));
				Ui()->DoLabel(&Status, aSt, 11.0f, TEXTALIGN_MR);
			}
			else
				Ui()->DoLabel(&Status, Localize("offline"), 11.0f, TEXTALIGN_MR);

			if(CanModerate && MemberIndex < s_vMemberSettings.size())
			{
				if(DoClansIconBtn(Ui(), TextRender(), &s_vMemberSettings[MemberIndex], FontIcon::GEAR, &SettingsBtn))
				{
					s_MemberMenu = (int)MemberIndex;
					s_MemberMenuPos = vec2(SettingsBtn.x + SettingsBtn.w, SettingsBtn.y);
					s_MemberMenuPhase = 0.0f;
					s_MemberMenuClosing = false;
					s_MemberMenuJustOpened = true;
				}
			}
			if(CanModerate && MemberIndex < std::size(s_aMemberIds) && Ui()->DoButtonLogic(&s_aMemberIds[MemberIndex], 0, &Item, BUTTONFLAG_RIGHT))
			{
				s_MemberMenu = (int)MemberIndex;
				s_MemberMenuPos = Ui()->MousePos();
				s_MemberMenuPhase = 0.0f;
				s_MemberMenuClosing = false;
				s_MemberMenuJustOpened = true;
			}
			if(CanJoin && MemberIndex < std::size(s_aMemberJoinIds) && Ui()->DoButtonLogic(&s_aMemberJoinIds[MemberIndex], 0, &Item, BUTTONFLAG_LEFT))
				Connect(M.m_aServer);
			if(CanJoin && MemberIndex < std::size(s_aMemberJoinIds))
				GameClient()->m_Tooltips.DoToolTip(&s_aMemberJoinIds[MemberIndex], &Item, Localize("Click to join this player"));
		}
		MainSection.HSplitTop(3.0f, nullptr, &MainSection);
		MemberIndex++;
	}
	s_MainScroll.End();

	UnleashedSection.Draw(ColorRGBA(0, 0, 0, 0.20f), IGraphics::CORNER_ALL, 6.0f);
	UnleashedSection.Margin(6.0f, &UnleashedSection);

	UnleashedSection.HSplitTop(22.0f, &Label, &UnleashedSection);
	char aUnlHead[64];
	str_format(aUnlHead, sizeof(aUnlHead), "%s (%d)", Localize("Unleashed"), (int)vUnleashed.size());
	Ui()->DoLabel(&Label, aUnlHead, 15.0f, TEXTALIGN_ML);
	UnleashedSection.HSplitTop(4.0f, nullptr, &UnleashedSection);

	static CScrollRegion s_UnleashedScroll;
	vec2 UnleashedScrollOffset(0.0f, 0.0f);
	CScrollRegionParams UnleashedParams;
	UnleashedParams.m_ClipBgColor = ColorRGBA(0, 0, 0, 0.0f);
	s_UnleashedScroll.Begin(&UnleashedSection, &UnleashedScrollOffset, &UnleashedParams);
	UnleashedSection.y += UnleashedScrollOffset.y;
	static int s_aUnleashedJoinIds[128];
	size_t UnleashedIndex = 0;
	for(const auto &U : vUnleashed)
	{
		CUIRect Item;
		UnleashedSection.HSplitTop(26.0f, &Item, &UnleashedSection);
		if(s_UnleashedScroll.AddRect(Item))
		{
			const bool CanJoin = U.m_aServer[0] != '\0';
			const bool JoinHot = CanJoin && UnleashedIndex < std::size(s_aUnleashedJoinIds) && Ui()->HotItem() == &s_aUnleashedJoinIds[UnleashedIndex];
			Item.Draw(ColorRGBA(1, 1, 1, JoinHot ? 0.10f : 0.03f), IGraphics::CORNER_ALL, 3.0f);
			Item.Margin(4.0f, &Item);
			CUIRect Name, Meta;
			Item.VSplitRight(220.0f, &Name, &Meta);
			Ui()->DoLabel(&Name, U.m_aName, 12.0f, TEXTALIGN_ML);
			char aUMeta[160];
			FormatClanPlayingStatus(aUMeta, sizeof(aUMeta), U.m_aMap, U.m_Players, U.m_MaxPlayers);
			Ui()->DoLabel(&Meta, aUMeta, 11.0f, TEXTALIGN_MR);
			if(CanJoin && UnleashedIndex < std::size(s_aUnleashedJoinIds) && Ui()->DoButtonLogic(&s_aUnleashedJoinIds[UnleashedIndex], 0, &Item, BUTTONFLAG_LEFT))
				Connect(U.m_aServer);
			if(CanJoin && UnleashedIndex < std::size(s_aUnleashedJoinIds))
				GameClient()->m_Tooltips.DoToolTip(&s_aUnleashedJoinIds[UnleashedIndex], &Item, Localize("Click to join this player"));
		}
		UnleashedSection.HSplitTop(2.0f, nullptr, &UnleashedSection);
		UnleashedIndex++;
	}
	s_UnleashedScroll.End();

	// Member settings popup (gear / RMB) — HUD-editor style grow animation
	if(s_MemberMenu >= 0 && s_MemberMenu < (int)Clan.m_vMembers.size())
	{
		const auto &M = Clan.m_vMembers[s_MemberMenu];
		const bool CanPromote = Clans.Role() == CClans::ERole::PRESIDENT || Clans.Role() == CClans::ERole::VICE_PRESIDENT;
		const bool CanDemote = Clans.Role() == CClans::ERole::PRESIDENT;
		const int ActionCount = 1 + (CanPromote ? 1 : 0) + (CanDemote ? 1 : 0);

		CUIRect Outer;
		Outer.w = 148.0f;
		Outer.h = 12.0f + ActionCount * 30.0f + (ActionCount - 1) * 4.0f;
		Outer.x = s_MemberMenuPos.x - Outer.w;
		Outer.y = s_MemberMenuPos.y;
		if(Outer.x < 4.0f)
			Outer.x = 4.0f;
		if(Outer.x + Outer.w > Ui()->Screen()->w - 4.0f)
			Outer.x = Ui()->Screen()->w - Outer.w - 4.0f;
		if(Outer.y + Outer.h > Ui()->Screen()->h - 4.0f)
			Outer.y = Ui()->Screen()->h - Outer.h - 4.0f;

		const float AnimTarget = s_MemberMenuClosing ? 0.0f : 1.0f;
		if(BCUiAnimations::Enabled() && g_Config.m_BcModuleUiRevealAnimation != 0)
			BCUiAnimations::UpdatePhase(s_MemberMenuPhase, AnimTarget, Client()->RenderFrameTime(), BCUiAnimations::MsToSeconds(g_Config.m_BcModuleUiRevealAnimationMs));
		else
			s_MemberMenuPhase = AnimTarget;
		const float Phase = BCUiAnimations::EaseOutCubic(s_MemberMenuPhase);

		if(s_MemberMenuClosing && s_MemberMenuPhase <= 0.001f)
		{
			s_MemberMenu = -1;
			s_MemberMenuClosing = false;
		}
		else
		{
			const CUIRect AnimRect = ClansAnimGrowRect(Outer, true, maximum(0.001f, Phase));
			AnimRect.Draw(ColorRGBA(0.5f, 0.5f, 0.5f, 0.55f * Phase), IGraphics::CORNER_ALL, 5.0f);
			CUIRect Inner = AnimRect;
			Inner.Margin(1.0f, &Inner);
			Inner.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.72f * Phase), IGraphics::CORNER_ALL, 5.0f);

			Ui()->ClipEnable(&AnimRect);
			CUIRect Menu = Outer;
			Menu.Margin(6.0f, &Menu);
			CUIRect Btn;
			static CButtonContainer s_Promote, s_Demote, s_Kick;
			bool CloseAfterAction = false;
			if(CanPromote)
			{
				Menu.HSplitTop(26.0f, &Btn, &Menu);
				if(DoClansTextBtn(Ui(), TextRender(), &s_Promote, Localize("Promote"), &Btn) && !Clans.IsBusy())
				{
					Clans.Promote(M.m_aUserId);
					CloseAfterAction = true;
				}
				Menu.HSplitTop(4.0f, nullptr, &Menu);
			}
			if(CanDemote)
			{
				Menu.HSplitTop(26.0f, &Btn, &Menu);
				if(DoClansTextBtn(Ui(), TextRender(), &s_Demote, Localize("Demote"), &Btn) && !Clans.IsBusy())
				{
					Clans.Demote(M.m_aUserId);
					CloseAfterAction = true;
				}
				Menu.HSplitTop(4.0f, nullptr, &Menu);
			}
			Menu.HSplitTop(26.0f, &Btn, &Menu);
			if(DoClansTextBtn(Ui(), TextRender(), &s_Kick, Localize("Kick"), &Btn, 0, true))
			{
				str_copy(s_aKickUserId, M.m_aUserId, sizeof(s_aKickUserId));
				s_KickUser = s_MemberMenu;
				s_BanHours = 24;
				CloseAfterAction = true;
			}
			Ui()->ClipDisable();

			if(CloseAfterAction)
			{
				s_MemberMenuClosing = true;
				if(!BCUiAnimations::Enabled() || g_Config.m_BcModuleUiRevealAnimation == 0)
				{
					s_MemberMenu = -1;
					s_MemberMenuClosing = false;
					s_MemberMenuPhase = 0.0f;
				}
			}
			else if(s_MemberMenuJustOpened)
			{
				s_MemberMenuJustOpened = false;
			}
			else if(Ui()->MouseButtonClicked(0) && !Ui()->MouseHovered(&Outer))
			{
				s_MemberMenuClosing = true;
				if(!BCUiAnimations::Enabled() || g_Config.m_BcModuleUiRevealAnimation == 0)
				{
					s_MemberMenu = -1;
					s_MemberMenuClosing = false;
					s_MemberMenuPhase = 0.0f;
				}
			}
		}
	}

	// Kick confirm popup
	if(s_KickUser >= 0 && s_aKickUserId[0])
	{
		CUIRect Overlay = *Ui()->Screen();
		Overlay.Draw(ColorRGBA(0, 0, 0, 0.45f), IGraphics::CORNER_ALL, 0.0f);
		CUIRect Popup;
		MainView.VMargin(MainView.w * 0.22f, &Popup);
		Popup.HMargin(Popup.h * 0.28f, &Popup);
		Popup.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.55f), IGraphics::CORNER_ALL, 8.0f);
		Popup.Margin(14.0f, &Popup);
		Popup.HSplitTop(24.0f, &Label, &Popup);
		Ui()->DoLabel(&Label, Localize("Kick member"), 16.0f, TEXTALIGN_MC);
		Popup.HSplitTop(10.0f, nullptr, &Popup);

		if(Clans.Role() == CClans::ERole::PRESIDENT)
		{
			Popup.HSplitTop(18.0f, &Label, &Popup);
			Ui()->DoLabel(&Label, Localize("Ban duration"), 13.0f, TEXTALIGN_MC);
			Popup.HSplitTop(6.0f, nullptr, &Popup);
			Popup.HSplitTop(28.0f, &Row, &Popup);
			static const int s_aBanOpts[] = {0, 24, 48, 72, -1};
			static const char *s_apBanLabels[] = {"0h", "24h", "48h", "72h", "perm"};
			static CButtonContainer s_aBanBtns[5];
			const float BanBtnW = 52.0f;
			const float BanGap = 4.0f;
			const float BanRowW = 5.0f * BanBtnW + 4.0f * BanGap;
			if(Row.w > BanRowW)
				Row.VMargin((Row.w - BanRowW) * 0.5f, &Row);
			for(int i = 0; i < 5; i++)
			{
				CUIRect B;
				Row.VSplitLeft(BanBtnW, &B, &Row);
				if(i < 4)
					Row.VSplitLeft(BanGap, nullptr, &Row);
				if(DoClansTextBtn(Ui(), TextRender(), &s_aBanBtns[i], s_apBanLabels[i], &B, s_BanHours == s_aBanOpts[i]))
					s_BanHours = s_aBanOpts[i];
			}
			Popup.HSplitTop(12.0f, nullptr, &Popup);
		}

		Popup.HSplitTop(30.0f, &Row, &Popup);
		CUIRect Confirm, Cancel;
		Row.VSplitMid(&Confirm, &Cancel, 8.0f);
		static CButtonContainer s_ConfirmKick, s_CancelKick;
		if(DoClansTextBtn(Ui(), TextRender(), &s_ConfirmKick, Localize("Kick"), &Confirm, 0, true) && !Clans.IsBusy())
		{
			Clans.Kick(s_aKickUserId, Clans.Role() == CClans::ERole::PRESIDENT ? s_BanHours : 0);
			s_KickUser = -1;
			s_aKickUserId[0] = '\0';
		}
		if(DoClansTextBtn(Ui(), TextRender(), &s_CancelKick, Localize("Cancel"), &Cancel))
		{
			s_KickUser = -1;
			s_aKickUserId[0] = '\0';
		}
	}

	// Disband confirm (4 shuffled answers)
	if(s_ShowDisband)
	{
		CUIRect Overlay = *Ui()->Screen();
		Overlay.Draw(ColorRGBA(0, 0, 0, 0.6f), IGraphics::CORNER_ALL, 0.0f);
		CUIRect Popup;
		MainView.VMargin(MainView.w * 0.12f, &Popup);
		Popup.HMargin(Popup.h * 0.18f, &Popup);
		Popup.Draw(ColorRGBA(0.12f, 0.12f, 0.12f, 0.98f), IGraphics::CORNER_ALL, 8.0f);
		Popup.Margin(16.0f, &Popup);
		Popup.HSplitTop(22.0f, &Label, &Popup);
		Ui()->DoLabel(&Label, Localize("Disband clan?"), 18.0f, TEXTALIGN_MC);
		Popup.HSplitTop(8.0f, nullptr, &Popup);
		Popup.HSplitTop(18.0f, &Label, &Popup);
		Ui()->DoLabel(&Label, Localize("Click the exact phrase:"), 13.0f, TEXTALIGN_MC);
		Popup.HSplitTop(4.0f, nullptr, &Popup);
		Popup.HSplitTop(28.0f, &Label, &Popup);
		Label.Draw(ColorRGBA(0.8f, 0.25f, 0.25f, 0.35f), IGraphics::CORNER_ALL, 4.0f);
		Ui()->DoLabel(&Label, Localize("Yes I really want to disband the clan!"), 13.0f, TEXTALIGN_MC);
		Popup.HSplitTop(12.0f, nullptr, &Popup);

		const char *apAnswers[4] = {
			Localize("Yes I really want to disband the clan!"),
			Localize("Yes, disband the clan"),
			Localize("I want to disband the clan"),
			Localize("Disband this clan now"),
		};
		static CButtonContainer s_aAns[4];
		for(int i = 0; i < 4; i++)
		{
			const int Idx = s_aDisbandOrder[i];
			Popup.HSplitTop(30.0f, &Button, &Popup);
			Popup.HSplitTop(6.0f, nullptr, &Popup);
			if(DoClansTextBtn(Ui(), TextRender(), &s_aAns[i], apAnswers[Idx], &Button) && !Clans.IsBusy())
			{
				if(Idx == 0)
					Clans.Disband();
				s_ShowDisband = false;
			}
		}
		Popup.HSplitTop(8.0f, nullptr, &Popup);
		Popup.HSplitTop(28.0f, &Button, &Popup);
		static CButtonContainer s_CancelDisband;
		if(DoClansTextBtn(Ui(), TextRender(), &s_CancelDisband, Localize("Cancel"), &Button))
			s_ShowDisband = false;
	}
}

void CMenus::RenderClansPreview(CUIRect MainView)
{
	CClans &Clans = GameClient()->m_Clans;
	const auto &Clan = Clans.Preview();

	static bool s_ShowApplyPopup = false;
	static char s_aApplyText[288] = "";
	static CLineInput s_ApplyInput;
	s_ApplyInput.SetBuffer(s_aApplyText, sizeof(s_aApplyText));

	CUIRect TopBar, BackBtn, RefreshBtn;
	MainView.HSplitTop(28.0f, &TopBar, &MainView);
	TopBar.VSplitRight(28.0f, &TopBar, &RefreshBtn);
	TopBar.VSplitRight(4.0f, &TopBar, nullptr);
	TopBar.VSplitRight(28.0f, &TopBar, &BackBtn);
	static CButtonContainer s_Back, s_Refresh;
	if(DoClansIconBtn(Ui(), TextRender(), &s_Back, FontIcon::CHEVRON_LEFT, &BackBtn))
	{
		Clans.ClearPreview();
		Clans.SetView(Clans.InClan() ? CClans::EView::BROWSE : CClans::EView::LANDING);
	}
	if(DoClansIconBtn(Ui(), TextRender(), &s_Refresh, FontIcon::ARROW_ROTATE_RIGHT, &RefreshBtn) && !Clans.IsBusy())
		Clans.RefreshCurrentView();

	if(s_ShowApplyPopup)
	{
		CUIRect Overlay = *Ui()->Screen();
		Overlay.Draw(ColorRGBA(0, 0, 0, 0.55f), IGraphics::CORNER_ALL, 0.0f);
		CUIRect Popup, Label, Button, Row;
		MainView.VMargin(MainView.w * 0.15f, &Popup);
		Popup.HMargin(Popup.h * 0.22f, &Popup);
		Popup.Draw(ColorRGBA(0.12f, 0.12f, 0.12f, 0.98f), IGraphics::CORNER_ALL, 8.0f);
		Popup.Margin(14.0f, &Popup);
		Popup.HSplitTop(24.0f, &Label, &Popup);
		Ui()->DoLabel(&Label, Localize("Application text"), 16.0f, TEXTALIGN_MC);
		Popup.HSplitTop(8.0f, nullptr, &Popup);
		Popup.HSplitTop(80.0f, &Button, &Popup);
		DoClansEditBox(Ui(), &s_ApplyInput, &Button, 14.0f);
		Popup.HSplitTop(12.0f, nullptr, &Popup);
		Popup.HSplitTop(28.0f, &Row, &Popup);
		CUIRect Send, Cancel;
		Row.VSplitMid(&Send, &Cancel, 8.0f);
		static CButtonContainer s_SendApply, s_CancelApply;
		if(DoClansTextBtn(Ui(), TextRender(), &s_SendApply, Localize("Send"), &Send) && !Clans.IsBusy())
		{
			Clans.Apply(Clans.PreviewClanId(), s_aApplyText);
			s_ShowApplyPopup = false;
			s_aApplyText[0] = '\0';
		}
		if(DoClansTextBtn(Ui(), TextRender(), &s_CancelApply, Localize("Cancel"), &Cancel))
		{
			s_ShowApplyPopup = false;
			s_aApplyText[0] = '\0';
		}
		return;
	}

	CUIRect Left, Right, Label, Button;
	MainView.VSplitLeft(MainView.w * 0.36f, &Left, &Right);
	Left.VSplitRight(8.0f, &Left, nullptr);
	Left.Draw(ColorRGBA(0, 0, 0, 0.3f), IGraphics::CORNER_ALL, 6.0f);
	Left.Margin(10.0f, &Left);

	CUIRect InfoBlock, IconBox;
	const float DescFont = 10.0f;
	const float DescSpacing = 1.0f;
	const int DescLines = CountClanDescriptionLines(Clan.m_aDescription);
	const float DescBlockH = DescLines * (DescFont + DescSpacing) + 4.0f;
	const float InfoBlockH = 32.0f + 16.0f + 24.0f + 18.0f + 6.0f + DescBlockH + 20.0f + 8.0f;
	Left.HSplitTop(InfoBlockH, &InfoBlock, &Left);
	InfoBlock.Draw(ColorRGBA(0, 0, 0, 0.20f), IGraphics::CORNER_ALL, 6.0f);
	InfoBlock.Margin(8.0f, &InfoBlock);

	CUIRect CenterIcon, TitleRow, FlagRow;
	InfoBlock.HSplitTop(32.0f, &CenterIcon, &InfoBlock);
	CenterIcon.VMargin(maximum(0.0f, (CenterIcon.w - 32.0f) * 0.5f), &IconBox);
	RenderClanIcon(Ui(), TextRender(), IconBox, Clan.m_IconId, Clan.m_Color);
	InfoBlock.HSplitTop(16.0f, &FlagRow, &InfoBlock);
	FlagRow.VMargin(maximum(0.0f, (FlagRow.w - 32.0f) * 0.5f), &FlagRow);
	RenderClanFlag(GameClient(), FlagRow, Clan.m_Country);
	InfoBlock.HSplitTop(24.0f, &TitleRow, &InfoBlock);
	Ui()->DoLabel(&TitleRow, Clan.m_aName[0] ? Clan.m_aName : Localize("Loading..."), 17.0f, TEXTALIGN_MC);

	InfoBlock.HSplitTop(18.0f, &Label, &InfoBlock);
	char aMeta[80];
	str_format(aMeta, sizeof(aMeta), "[%s] · %s", Clan.m_aTag, Clan.m_aJoinPolicy);
	Ui()->DoLabel(&Label, aMeta, 13.0f, TEXTALIGN_MC);
	InfoBlock.HSplitTop(6.0f, nullptr, &InfoBlock);
	InfoBlock.HSplitTop(DescBlockH, &Label, &InfoBlock);
	DoClanDescriptionCentered(Ui(), TextRender(), Label, Clan.m_aDescription, DescFont, DescSpacing);

	int OnlineMain = 0;
	for(const auto &M : Clan.m_vMembers)
		if(M.m_Online)
			OnlineMain++;
	const int OnlineTotal = OnlineMain + (int)Clan.m_vUnleashed.size();
	const int MemberCount = (int)Clan.m_vMembers.size();
	InfoBlock.HSplitTop(20.0f, &Label, &InfoBlock);
	str_format(aMeta, sizeof(aMeta), Localize("Online %d/%d"), OnlineTotal, MemberCount);
	Ui()->DoLabel(&Label, aMeta, 12.0f, TEXTALIGN_MC);
	Left.HSplitTop(10.0f, nullptr, &Left);

	if(!Clans.InClan() && Clan.m_aClanId[0])
	{
		Left.HSplitBottom(36.0f, &Left, &Button);
		static CButtonContainer s_JoinOpen, s_Apply;
		if(!str_comp(Clan.m_aJoinPolicy, "open"))
		{
			if(DoClansTextBtn(Ui(), TextRender(), &s_JoinOpen, Localize("Join"), &Button) && !Clans.IsBusy())
				Clans.Join(Clan.m_aClanId);
		}
		else if(!str_comp(Clan.m_aJoinPolicy, "request"))
		{
			if(DoClansTextBtn(Ui(), TextRender(), &s_Apply, Localize("Apply"), &Button) && !Clans.IsBusy())
			{
				s_aApplyText[0] = '\0';
				s_ShowApplyPopup = true;
			}
		}
		else
			Ui()->DoLabel(&Button, Localize("Invite only"), 13.0f, TEXTALIGN_MC);
	}

	CUIRect MainSection, UnleashedSection;
	Right.HSplitMid(&MainSection, &UnleashedSection, 8.0f);

	MainSection.Draw(ColorRGBA(0, 0, 0, 0.20f), IGraphics::CORNER_ALL, 6.0f);
	MainSection.Margin(6.0f, &MainSection);
	MainSection.HSplitTop(22.0f, &Label, &MainSection);
	char aMainHead[64];
	str_format(aMainHead, sizeof(aMainHead), "%s (%d)", Localize("Main"), MemberCount);
	Ui()->DoLabel(&Label, aMainHead, 15.0f, TEXTALIGN_ML);
	MainSection.HSplitTop(4.0f, nullptr, &MainSection);

	static CScrollRegion s_MainScroll;
	vec2 MainScrollOffset(0.0f, 0.0f);
	CScrollRegionParams MainParams;
	MainParams.m_ClipBgColor = ColorRGBA(0, 0, 0, 0.0f);
	s_MainScroll.Begin(&MainSection, &MainScrollOffset, &MainParams);
	MainSection.y += MainScrollOffset.y;
	static int s_aPreviewJoinIds[128];
	size_t MemberIndex = 0;
	for(const auto &M : Clan.m_vMembers)
	{
		CUIRect Item;
		MainSection.HSplitTop(42.0f, &Item, &MainSection);
		if(s_MainScroll.AddRect(Item))
		{
			const bool IsSelf = !str_comp(M.m_aUserId, Clans.UserId());
			const bool CanJoin = !IsSelf && M.m_aServer[0] != '\0';
			const bool JoinHot = CanJoin && MemberIndex < std::size(s_aPreviewJoinIds) && Ui()->HotItem() == &s_aPreviewJoinIds[MemberIndex];
			Item.Draw(ColorRGBA(1, 1, 1, JoinHot ? 0.10f : 0.04f), IGraphics::CORNER_ALL, 3.0f);
			Item.Margin(4.0f, &Item);
			CUIRect Tee, Name, Status;
			Item.VSplitLeft(36.0f, &Tee, &Item);
			Item.VSplitLeft(4.0f, nullptr, &Item);
			Item.VSplitRight(200.0f, &Name, &Status);
			RenderClanMemberTee(GameClient(), RenderTools(), Tee, M.m_Skin);
			CUIRect NickRow, RoleRow;
			Name.HSplitMid(&NickRow, &RoleRow);
			Ui()->DoLabel(&NickRow, M.m_aNickname, 12.0f, TEXTALIGN_ML);
			TextRender()->TextColor(ColorRGBA(0.55f, 0.55f, 0.55f, 1.0f));
			Ui()->DoLabel(&RoleRow, RoleLabel(M.m_Role), 9.0f, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			if(M.m_Online)
			{
				char aSt[128];
				if(M.m_aMap[0])
					FormatClanPlayingStatus(aSt, sizeof(aSt), M.m_aMap, M.m_Players, M.m_MaxPlayers);
				else
					str_copy(aSt, Localize("online"), sizeof(aSt));
				Ui()->DoLabel(&Status, aSt, 11.0f, TEXTALIGN_MR);
			}
			else
				Ui()->DoLabel(&Status, Localize("offline"), 11.0f, TEXTALIGN_MR);
			if(CanJoin && MemberIndex < std::size(s_aPreviewJoinIds) && Ui()->DoButtonLogic(&s_aPreviewJoinIds[MemberIndex], 0, &Item, BUTTONFLAG_LEFT))
				Connect(M.m_aServer);
			if(CanJoin && MemberIndex < std::size(s_aPreviewJoinIds))
				GameClient()->m_Tooltips.DoToolTip(&s_aPreviewJoinIds[MemberIndex], &Item, Localize("Click to join this player"));
		}
		MainSection.HSplitTop(2.0f, nullptr, &MainSection);
		MemberIndex++;
	}
	s_MainScroll.End();

	UnleashedSection.Draw(ColorRGBA(0, 0, 0, 0.20f), IGraphics::CORNER_ALL, 6.0f);
	UnleashedSection.Margin(6.0f, &UnleashedSection);
	UnleashedSection.HSplitTop(22.0f, &Label, &UnleashedSection);
	char aUnlHead[64];
	str_format(aUnlHead, sizeof(aUnlHead), "%s (%d)", Localize("Unleashed"), (int)Clan.m_vUnleashed.size());
	Ui()->DoLabel(&Label, aUnlHead, 15.0f, TEXTALIGN_ML);
	UnleashedSection.HSplitTop(4.0f, nullptr, &UnleashedSection);

	static CScrollRegion s_UnleashedScroll;
	vec2 UnleashedScrollOffset(0.0f, 0.0f);
	CScrollRegionParams UnleashedParams;
	UnleashedParams.m_ClipBgColor = ColorRGBA(0, 0, 0, 0.0f);
	s_UnleashedScroll.Begin(&UnleashedSection, &UnleashedScrollOffset, &UnleashedParams);
	UnleashedSection.y += UnleashedScrollOffset.y;
	static int s_aPreviewUnlJoinIds[128];
	size_t UnleashedIndex = 0;
	for(const auto &U : Clan.m_vUnleashed)
	{
		CUIRect Item;
		UnleashedSection.HSplitTop(26.0f, &Item, &UnleashedSection);
		if(s_UnleashedScroll.AddRect(Item))
		{
			const bool CanJoin = U.m_aServer[0] != '\0';
			const bool JoinHot = CanJoin && UnleashedIndex < std::size(s_aPreviewUnlJoinIds) && Ui()->HotItem() == &s_aPreviewUnlJoinIds[UnleashedIndex];
			Item.Draw(ColorRGBA(1, 1, 1, JoinHot ? 0.10f : 0.03f), IGraphics::CORNER_ALL, 3.0f);
			Item.Margin(4.0f, &Item);
			CUIRect Name, Meta;
			Item.VSplitRight(220.0f, &Name, &Meta);
			Ui()->DoLabel(&Name, U.m_aName, 12.0f, TEXTALIGN_ML);
			char aUMeta[160];
			FormatClanPlayingStatus(aUMeta, sizeof(aUMeta), U.m_aMap, U.m_Players, U.m_MaxPlayers);
			Ui()->DoLabel(&Meta, aUMeta, 11.0f, TEXTALIGN_MR);
			if(CanJoin && UnleashedIndex < std::size(s_aPreviewUnlJoinIds) && Ui()->DoButtonLogic(&s_aPreviewUnlJoinIds[UnleashedIndex], 0, &Item, BUTTONFLAG_LEFT))
				Connect(U.m_aServer);
			if(CanJoin && UnleashedIndex < std::size(s_aPreviewUnlJoinIds))
				GameClient()->m_Tooltips.DoToolTip(&s_aPreviewUnlJoinIds[UnleashedIndex], &Item, Localize("Click to join this player"));
		}
		UnleashedSection.HSplitTop(2.0f, nullptr, &UnleashedSection);
		UnleashedIndex++;
	}
	s_UnleashedScroll.End();
}

void CMenus::RenderClansApplications(CUIRect MainView)
{
	CClans &Clans = GameClient()->m_Clans;
	CUIRect Top, Label, Button, BackBtn, RefreshBtn;
	MainView.HSplitTop(28.0f, &Top, &MainView);
	Top.VSplitRight(28.0f, &Top, &RefreshBtn);
	Top.VSplitRight(4.0f, &Top, nullptr);
	Top.VSplitRight(28.0f, &Label, &BackBtn);
	Ui()->DoLabel(&Label, Localize("Applications"), 16.0f, TEXTALIGN_ML);
	static CButtonContainer s_Back, s_Refresh;
	if(DoClansIconBtn(Ui(), TextRender(), &s_Back, FontIcon::CHEVRON_LEFT, &BackBtn))
		Clans.SetView(CClans::EView::CLAN);
	if(DoClansIconBtn(Ui(), TextRender(), &s_Refresh, FontIcon::ARROW_ROTATE_RIGHT, &RefreshBtn) && !Clans.IsBusy())
		Clans.RefreshCurrentView();

	static CScrollRegion s_Scroll;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams Params;
	s_Scroll.Begin(&MainView, &ScrollOffset, &Params);
	MainView.y += ScrollOffset.y;
	static std::vector<CButtonContainer> s_vAccept;
	static std::vector<CButtonContainer> s_vReject;
	if(s_vAccept.size() < Clans.Applications().size())
	{
		s_vAccept.resize(Clans.Applications().size());
		s_vReject.resize(Clans.Applications().size());
	}
	size_t AppIndex = 0;
	for(const auto &App : Clans.Applications())
	{
		CUIRect Item, Text, Actions, Accept, Reject;
		MainView.HSplitTop(70.0f, &Item, &MainView);
		if(s_Scroll.AddRect(Item))
		{
			Item.Draw(ColorRGBA(1, 1, 1, 0.04f), IGraphics::CORNER_ALL, 4.0f);
			Item.Margin(8.0f, &Item);
			Item.HSplitTop(18.0f, &Label, &Item);
			Ui()->DoLabel(&Label, App.m_aNickname, 14.0f, TEXTALIGN_ML);
			Item.HSplitBottom(28.0f, &Text, &Actions);
			Ui()->DoLabel(&Text, App.m_aText, 12.0f, TEXTALIGN_TL);
			Actions.VSplitRight(90.0f, &Actions, &Reject);
			Actions.VSplitRight(8.0f, &Actions, nullptr);
			Actions.VSplitRight(90.0f, nullptr, &Accept);
			if(DoClansTextBtn(Ui(), TextRender(), &s_vAccept[AppIndex], Localize("Accept"), &Accept) && !Clans.IsBusy())
				Clans.ApproveApplication(App.m_aId);
			if(DoClansTextBtn(Ui(), TextRender(), &s_vReject[AppIndex], Localize("Reject"), &Reject, 0, true) && !Clans.IsBusy())
				Clans.RejectApplication(App.m_aId);
		}
		MainView.HSplitTop(6.0f, nullptr, &MainView);
		AppIndex++;
	}
	s_Scroll.End();
}

void CMenus::RenderClansAnnouncements(CUIRect MainView)
{
	CClans &Clans = GameClient()->m_Clans;
	CUIRect Top, Label, Composer, BackBtn, RefreshBtn;
	MainView.HSplitTop(28.0f, &Top, &MainView);
	Top.VSplitRight(28.0f, &Top, &RefreshBtn);
	Top.VSplitRight(4.0f, &Top, nullptr);
	Top.VSplitRight(28.0f, &Label, &BackBtn);
	{
		char aTitle[64];
		str_format(aTitle, sizeof(aTitle), "%s (%d)", Localize("Chat"), (int)Clans.Announcements().size());
		Ui()->DoLabel(&Label, aTitle, 16.0f, TEXTALIGN_ML);
	}
	static CButtonContainer s_Back, s_Refresh;
	if(DoClansIconBtn(Ui(), TextRender(), &s_Back, FontIcon::CHEVRON_LEFT, &BackBtn))
		Clans.SetView(CClans::EView::CLAN);
	if(DoClansIconBtn(Ui(), TextRender(), &s_Refresh, FontIcon::ARROW_ROTATE_RIGHT, &RefreshBtn) && !Clans.IsBusy())
		Clans.RefreshCurrentView();

	MainView.HSplitTop(8.0f, nullptr, &MainView);

	static CLineInput s_Text;
	static char s_aText[500];
	s_Text.SetBuffer(s_aText, sizeof(s_aText));
	s_Text.SetEmptyText(Localize("Write a message..."));

	MainView.HSplitBottom(78.0f, &MainView, &Composer);
	MainView.HSplitBottom(8.0f, &MainView, nullptr);
	Composer.Margin(2.0f, &Composer);

	CUIRect Edit, Footer, Send, Counter;
	Composer.HSplitBottom(28.0f, &Edit, &Footer);
	Composer.HSplitBottom(6.0f, &Edit, nullptr);
	DoClansEditBox(Ui(), &s_Text, &Edit, 13.0f);

	Footer.VSplitRight(100.0f, &Counter, &Send);
	Counter.VSplitRight(8.0f, &Counter, nullptr);
	if(Clans.IsAnnounceCooldownActive())
	{
		char aCd[32];
		str_format(aCd, sizeof(aCd), "%ds", Clans.AnnounceCooldownSecondsLeft());
		TextRender()->TextColor(ColorRGBA(0.85f, 0.55f, 0.25f, 1.0f));
		Ui()->DoLabel(&Counter, aCd, 11.0f, TEXTALIGN_MR);
	}
	else
	{
		char aCount[24];
		str_format(aCount, sizeof(aCount), "%d/500", str_length(s_aText));
		TextRender()->TextColor(ColorRGBA(0.55f, 0.55f, 0.55f, 1.0f));
		Ui()->DoLabel(&Counter, aCount, 11.0f, TEXTALIGN_MR);
	}
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	static CButtonContainer s_Send;
	const bool CanSend = s_aText[0] && !Clans.IsBusy() && !Clans.IsAnnounceCooldownActive();
	if(DoClansTextBtn(Ui(), TextRender(), &s_Send, Localize("Send"), &Send) && CanSend)
	{
		Clans.PostAnnouncement(s_aText);
		s_aText[0] = '\0';
	}

	if(Clans.Announcements().empty())
	{
		CUIRect Empty = MainView;
		Empty.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.22f), IGraphics::CORNER_ALL, 6.0f);
		CUIRect IconBox, Hint;
		Empty.HSplitMid(&IconBox, &Hint);
		IconBox.HSplitBottom(8.0f, &IconBox, nullptr);
		Hint.HSplitTop(4.0f, nullptr, &Hint);
		Hint.HSplitTop(20.0f, &Hint, nullptr);
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		TextRender()->TextColor(ColorRGBA(0.45f, 0.45f, 0.45f, 1.0f));
		Ui()->DoLabel(&IconBox, FontIcon::COMMENT, 28.0f, TEXTALIGN_BC);
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		Ui()->DoLabel(&Hint, Localize("No messages yet"), 13.0f, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		return;
	}

	static CScrollRegion s_Scroll;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams Params;
	s_Scroll.Begin(&MainView, &ScrollOffset, &Params);
	MainView.y += ScrollOffset.y;

	const auto &vAnns = Clans.Announcements();
	// API stores newest-first; chat shows oldest → newest (composer at bottom).
	for(int i = (int)vAnns.size() - 1; i >= 0; i--)
	{
		const auto &Ann = vAnns[i];
		const bool IsPresident = Ann.m_AuthorRole == CClans::ERole::PRESIDENT;
		const float BodyFont = 12.0f;
		const float TeeSize = 28.0f;
		const float BodyWidth = maximum(1.0f, MainView.w - 28.0f - TeeSize - 10.0f - (IsPresident ? 6.0f : 0.0f));
		STextSizeProperties SizeProps;
		int LineCount = 1;
		SizeProps.m_pLineCount = &LineCount;
		TextRender()->TextWidth(BodyFont, Ann.m_aText, -1, BodyWidth, 0, SizeProps);
		LineCount = std::clamp(LineCount, 1, 8);
		const float CardH = maximum(TeeSize + 12.0f, 10.0f + 16.0f + 4.0f + LineCount * (BodyFont + 3.0f) + 8.0f);

		CUIRect Item;
		MainView.HSplitTop(CardH, &Item, &MainView);
		if(s_Scroll.AddRect(Item))
		{
			Item.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.28f), IGraphics::CORNER_ALL, 6.0f);

			if(IsPresident)
			{
				CUIRect Accent = Item;
				Accent.w = 3.0f;
				ColorRGBA AccentColor = ClanColorRgb(Clans.Clan().m_Color);
				AccentColor.a = 0.95f;
				Accent.Draw(AccentColor, IGraphics::CORNER_L, 6.0f);
				Item.x += 5.0f;
				Item.w -= 5.0f;
			}

			Item.Margin(6.0f, &Item);
			CUIRect Tee, Content;
			Item.VSplitLeft(TeeSize, &Tee, &Content);
			Content.VSplitLeft(8.0f, nullptr, &Content);
			RenderClanMemberTee(GameClient(), RenderTools(), Tee, Ann.m_AuthorSkin);

			CUIRect Head, Body;
			Content.HSplitTop(16.0f, &Head, &Body);
			Body.HSplitTop(3.0f, nullptr, &Body);

			CUIRect Author, Date;
			Head.VSplitRight(120.0f, &Author, &Date);
			Ui()->DoLabel(&Author, Ann.m_aAuthorNick, 12.0f, TEXTALIGN_ML);

			char aDate[40];
			str_copy(aDate, Ann.m_aCreatedAt, sizeof(aDate));
			for(int c = 0; aDate[c]; c++)
			{
				if(aDate[c] == 'T')
					aDate[c] = ' ';
				if(aDate[c] == '.' || aDate[c] == 'Z')
				{
					aDate[c] = '\0';
					break;
				}
			}
			if(str_length(aDate) > 16)
				aDate[16] = '\0';
			TextRender()->TextColor(ColorRGBA(0.55f, 0.55f, 0.55f, 1.0f));
			Ui()->DoLabel(&Date, aDate, 11.0f, TEXTALIGN_MR);
			TextRender()->TextColor(TextRender()->DefaultTextColor());

			SLabelProperties BodyProps;
			BodyProps.m_MaxWidth = Body.w;
			Ui()->DoLabel(&Body, Ann.m_aText, BodyFont, TEXTALIGN_TL, BodyProps);
		}
		MainView.HSplitTop(6.0f, nullptr, &MainView);
	}
	s_Scroll.End();
}

void CMenus::RenderClansSettings(CUIRect MainView)
{
	CClans &Clans = GameClient()->m_Clans;
	const auto &Clan = Clans.Clan();

	static CLineInput s_Name, s_Desc;
	static char s_aName[64], s_aDesc[256];
	static int s_IconId = 0;
	static int s_Country = -1;
	static unsigned s_ColorHsla = 0;
	static char s_aLoadedClanId[64] = "";

	if(str_comp(s_aLoadedClanId, Clan.m_aClanId) != 0 || !s_aLoadedClanId[0])
	{
		str_copy(s_aLoadedClanId, Clan.m_aClanId, sizeof(s_aLoadedClanId));
		str_copy(s_aName, Clan.m_aName, sizeof(s_aName));
		str_copy(s_aDesc, Clan.m_aDescription, sizeof(s_aDesc));
		NormalizeClanDescription(s_aDesc, sizeof(s_aDesc));
		s_IconId = std::clamp(Clan.m_IconId, 0, NUM_CLAN_ICONS - 1);
		s_Country = Clan.m_Country;
		const unsigned R = (Clan.m_Color >> 16) & 0xFF;
		const unsigned G = (Clan.m_Color >> 8) & 0xFF;
		const unsigned B = Clan.m_Color & 0xFF;
		s_ColorHsla = color_cast<ColorHSLA>(ColorRGBA(R / 255.0f, G / 255.0f, B / 255.0f, 1.0f)).Pack(false);
	}

	s_Name.SetBuffer(s_aName, sizeof(s_aName));
	s_Desc.SetBuffer(s_aDesc, sizeof(s_aDesc));
	s_Name.SetEmptyText(Localize("Clan name"));
	s_Desc.SetEmptyText(Localize("Description"));
	s_Desc.SetAllowNewline(true);

	CUIRect Top, Label, BackBtn, RefreshBtn, Button, Row;
	MainView.HSplitTop(28.0f, &Top, &MainView);
	Top.VSplitRight(28.0f, &Top, &RefreshBtn);
	Top.VSplitRight(4.0f, &Top, nullptr);
	Top.VSplitRight(28.0f, &Label, &BackBtn);
	Ui()->DoLabel(&Label, Localize("Clan settings"), 16.0f, TEXTALIGN_ML);
	static CButtonContainer s_Back, s_Refresh;
	if(DoClansIconBtn(Ui(), TextRender(), &s_Back, FontIcon::CHEVRON_LEFT, &BackBtn))
	{
		s_aLoadedClanId[0] = '\0';
		Clans.SetView(CClans::EView::CLAN);
	}
	if(DoClansIconBtn(Ui(), TextRender(), &s_Refresh, FontIcon::ARROW_ROTATE_RIGHT, &RefreshBtn) && !Clans.IsBusy())
	{
		s_aLoadedClanId[0] = '\0';
		Clans.RefreshCurrentView();
	}

	MainView.HSplitTop(8.0f, nullptr, &MainView);
	MainView.VMargin(MainView.w * 0.12f, &MainView);

	MainView.HSplitTop(16.0f, &Label, &MainView);
	Ui()->DoLabel(&Label, Localize("Name"), 12.0f, TEXTALIGN_ML);
	MainView.HSplitTop(4.0f, nullptr, &MainView);
	MainView.HSplitTop(28.0f, &Button, &MainView);
	DoClansEditBox(Ui(), &s_Name, &Button, 13.0f);

	MainView.HSplitTop(10.0f, nullptr, &MainView);
	MainView.HSplitTop(16.0f, &Label, &MainView);
	char aTagLocked[48];
	str_format(aTagLocked, sizeof(aTagLocked), "%s: [%s]", Localize("Tag"), Clan.m_aTag);
	TextRender()->TextColor(ColorRGBA(0.65f, 0.65f, 0.65f, 1.0f));
	Ui()->DoLabel(&Label, aTagLocked, 12.0f, TEXTALIGN_ML);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	MainView.HSplitTop(10.0f, nullptr, &MainView);
	MainView.HSplitTop(16.0f, &Label, &MainView);
	Ui()->DoLabel(&Label, Localize("Description"), 12.0f, TEXTALIGN_ML);
	MainView.HSplitTop(4.0f, nullptr, &MainView);
	MainView.HSplitTop(56.0f, &Button, &MainView);
	{
		const float DescFont = 11.0f;
		const float DescSpacing = 1.0f;
		if(DoClansDescEditBox(Ui(), TextRender(), &s_Desc, &Button, DescFont, DescSpacing))
			NormalizeClanDescription(s_aDesc, sizeof(s_aDesc));
	}

	const unsigned ClanRgb = ClanRgbFromHsla(s_ColorHsla);
	MainView.HSplitTop(12.0f, nullptr, &MainView);
	MainView.HSplitTop(16.0f, &Label, &MainView);
	Ui()->DoLabel(&Label, Localize("Icon and color"), 12.0f, TEXTALIGN_ML);
	MainView.HSplitTop(4.0f, nullptr, &MainView);
	MainView.HSplitTop(36.0f, &Row, &MainView);
	CUIRect IconsRow, ColorCol;
	Row.VSplitRight(100.0f, &IconsRow, &ColorCol);
	ColorCol.VSplitLeft(8.0f, nullptr, &ColorCol);
	static CButtonContainer s_aIcons[NUM_CLAN_ICONS];
	const float IconCell = IconsRow.h + 4.0f;
	RenderClanIconPickerRow(Ui(), TextRender(), IconsRow, 0, 8, &s_IconId, ClanRgb, IconCell, s_aIcons);
	{
		CUIRect ColorBtn, ResetBtn;
		ColorCol.VSplitRight(56.0f, &ColorCol, &ResetBtn);
		ColorCol.VSplitRight(4.0f, &ColorCol, nullptr);
		const float Sq = minimum(ColorCol.w, ColorCol.h);
		ColorCol.VMargin(maximum(0.0f, (ColorCol.w - Sq) * 0.5f), &ColorBtn);
		ColorBtn.HMargin(maximum(0.0f, (ColorBtn.h - Sq) * 0.5f), &ColorBtn);
		DoButton_ColorPicker(&ColorBtn, &s_ColorHsla, false);
		static CButtonContainer s_ColorReset;
		ResetBtn.HMargin(2.0f, &ResetBtn);
		if(DoClansTextBtn(Ui(), TextRender(), &s_ColorReset, Localize("Reset"), &ResetBtn))
			s_ColorHsla = color_cast<ColorHSLA>(ColorRGBA(0.33f, 0.55f, 1.0f, 1.0f)).Pack(false);
	}
	MainView.HSplitTop(4.0f, nullptr, &MainView);
	MainView.HSplitTop(36.0f, &Row, &MainView);
	Row.VSplitRight(100.0f, &IconsRow, &ColorCol);
	ColorCol.VSplitLeft(8.0f, nullptr, &ColorCol);
	RenderClanIconPickerRow(Ui(), TextRender(), IconsRow, 8, 8, &s_IconId, ClanRgb, IconCell, s_aIcons);
	{
		CUIRect FlagButton = ColorCol;
		const float FlagW = minimum(FlagButton.w, FlagButton.h * 2.0f);
		FlagButton.x += (FlagButton.w - FlagW) * 0.5f;
		FlagButton.w = FlagW;
		static CButtonContainer s_FlagButton;
		if(DoButton_Menu(&s_FlagButton, "", 0, &FlagButton))
		{
			static SPopupMenuId s_PopupCountryId;
			static SPopupSettingsCountrySelectionContext s_PopupCountryContext;
			s_PopupCountryContext.m_pMenus = this;
			s_PopupCountryContext.m_pCountry = &s_Country;
			s_PopupCountryContext.m_Selection = s_Country;
			s_PopupCountryContext.m_New = true;
			Ui()->DoPopupMenu(&s_PopupCountryId, FlagButton.x, FlagButton.y + FlagButton.h, 490.0f, 210.0f, &s_PopupCountryContext, PopupSettingsCountrySelection);
		}
		GameClient()->m_Tooltips.DoToolTip(&s_FlagButton, &FlagButton, Localize("Choose country flag"));
		GameClient()->m_CountryFlags.Render(s_Country, ColorRGBA(1.0f, 1.0f, 1.0f, Ui()->HotItem() == &s_FlagButton ? 1.0f : 0.85f), FlagButton.x, FlagButton.y, FlagButton.w, FlagButton.h);
	}
	MainView.HSplitTop(4.0f, nullptr, &MainView);
	MainView.HSplitTop(36.0f, &Row, &MainView);
	RenderClanIconPickerRow(Ui(), TextRender(), Row, 16, 8, &s_IconId, ClanRgb, IconCell, s_aIcons);

	MainView.HSplitTop(20.0f, nullptr, &MainView);
	MainView.HSplitTop(40.0f, &Row, &MainView);
	CUIRect SaveBtn, CancelBtn;
	Row.VSplitMid(&SaveBtn, &CancelBtn, 10.0f);
	static CButtonContainer s_Save, s_Cancel;
	if(DoClansTextBtn(Ui(), TextRender(), &s_Save, Localize("Save"), &SaveBtn) && s_aName[0] && !Clans.IsBusy())
	{
		NormalizeClanDescription(s_aDesc, sizeof(s_aDesc));
		Clans.UpdateClanSettings(s_aName, s_aDesc, s_IconId, ClanRgb, s_Country);
	}
	if(DoClansTextBtn(Ui(), TextRender(), &s_Cancel, Localize("Back"), &CancelBtn))
	{
		s_aLoadedClanId[0] = '\0';
		Clans.SetView(CClans::EView::CLAN);
	}

	if(Clans.StatusMessage()[0])
	{
		MainView.HSplitTop(12.0f, nullptr, &MainView);
		MainView.HSplitTop(18.0f, &Label, &MainView);
		Ui()->DoLabel(&Label, Clans.StatusMessage(), 12.0f, TEXTALIGN_MC);
	}
}

void CMenus::RenderClansRecent(CUIRect MainView)
{
	CClans &Clans = GameClient()->m_Clans;
	CUIRect Top, Label, BackBtn, RefreshBtn, ClearBtn;
	MainView.HSplitTop(28.0f, &Top, &MainView);
	Top.VSplitRight(28.0f, &Top, &RefreshBtn);
	Top.VSplitRight(4.0f, &Top, nullptr);
	Top.VSplitRight(70.0f, &Top, &ClearBtn);
	Top.VSplitRight(4.0f, &Top, nullptr);
	Top.VSplitRight(28.0f, &Label, &BackBtn);
	Ui()->DoLabel(&Label, Localize("Recent clans"), 16.0f, TEXTALIGN_ML);
	static CButtonContainer s_Back, s_Refresh, s_Clear;
	if(DoClansIconBtn(Ui(), TextRender(), &s_Back, FontIcon::CHEVRON_LEFT, &BackBtn))
		Clans.SetView(Clans.InClan() ? CClans::EView::CLAN : CClans::EView::LANDING);
	if(DoClansTextBtn(Ui(), TextRender(), &s_Clear, Localize("Clear"), &ClearBtn) && !Clans.IsBusy() && !Clans.RecentClans().empty())
		Clans.ClearRecentClans();
	if(DoClansIconBtn(Ui(), TextRender(), &s_Refresh, FontIcon::ARROW_ROTATE_RIGHT, &RefreshBtn) && !Clans.IsBusy())
		Clans.RefreshCurrentView();

	static std::vector<CButtonContainer> s_vRejoin;
	if(s_vRejoin.size() < Clans.RecentClans().size())
		s_vRejoin.resize(Clans.RecentClans().size());
	size_t RecentIndex = 0;
	for(const auto &R : Clans.RecentClans())
	{
		CUIRect Item, Act;
		MainView.HSplitTop(36.0f, &Item, &MainView);
		Item.Draw(ColorRGBA(1, 1, 1, 0.04f), IGraphics::CORNER_ALL, 4.0f);
		Item.Margin(6.0f, &Item);
		Item.VSplitRight(120.0f, &Label, &Act);
		char aLine[96];
		str_format(aLine, sizeof(aLine), "[%s] %s", R.m_aTag, R.m_aName);
		Ui()->DoLabel(&Label, aLine, 13.0f, TEXTALIGN_ML);
		if(!Clans.InClan() && (R.m_WasPresident || CatalogClanIsOpen(Clans, R.m_aClanId)))
		{
			const char *pBtn = R.m_WasPresident ? Localize("Return as President") : Localize("Return");
			if(DoClansTextBtn(Ui(), TextRender(), &s_vRejoin[RecentIndex], pBtn, &Act) && !Clans.IsBusy())
			{
				if(R.m_WasPresident)
					Clans.RejoinAsPresident(R.m_aClanId);
				else
					Clans.Join(R.m_aClanId);
			}
		}
		MainView.HSplitTop(4.0f, nullptr, &MainView);
		RecentIndex++;
	}
}
