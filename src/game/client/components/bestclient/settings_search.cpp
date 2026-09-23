/* Copyright © 2026 BestProject Team */
#include "settings_search.h"

#include <base/str.h>
#include <base/vmath.h>

#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/bestclient/ui_theme/settings_nav.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <vector>

namespace BestClientSettingsSearch
{

using namespace BestClientUiTheme;

static constexpr float HIGHLIGHT_DURATION = 1.6f;
static constexpr ColorRGBA HIGHLIGHT_COLOR = ColorRGBA(1.0f, 0.28f, 0.28f, 1.0f);

static const SEntry gs_aCatalog[] = {
#include "settings_search_catalog.inl"
};

struct SPopupContext : public SPopupMenuId
{
	CMenus *m_pMenus = nullptr;
	CUi *m_pUi = nullptr;
	CGameClient *m_pGameClient = nullptr;
};

static SPopupContext gs_PopupContext;
static CLineInputBuffered<128> gs_SearchInput;
static CScrollRegion gs_ResultsScroll;
static const void *gs_pHighlightId = nullptr;
static float gs_HighlightTime = 0.0f;
static bool gs_PendingScroll = false;
static CScrollRegion *gs_pActiveScroll = nullptr;
static std::vector<CScrollRegion *> gs_vScrollStack;
static const void *gs_pBlockHighlightDrawn = nullptr;
static std::vector<int> gs_vFiltered;

static const char *CatalogText(const SEntry &Entry, const char *pText)
{
	if(pText && GroupForLeaf(Entry.m_Leaf) == SETTINGS_NAV_GROUP_BESTCLIENT)
		return BcLocalize(pText);
	return Localize(pText);
}

static const char *LeafLabel(int Leaf)
{
	switch(Leaf)
	{
	case SETTINGS_NAV_LEAF_TC_SETTINGS: return TCLocalize("Settings");
	case SETTINGS_NAV_LEAF_TC_BIND_WHEEL: return TCLocalize("Bind Wheel");
	case SETTINGS_NAV_LEAF_TC_WAR_LIST: return TCLocalize("War List");
	case SETTINGS_NAV_LEAF_TC_CHAT_BINDS: return TCLocalize("Chat Binds");
	case SETTINGS_NAV_LEAF_TC_STATUS_BAR: return TCLocalize("Status Bar");
	case SETTINGS_NAV_LEAF_TC_INFO: return TCLocalize("Info");
	case SETTINGS_NAV_LEAF_TC_PROFILE: return TCLocalize("Profiles");
	case SETTINGS_NAV_LEAF_TC_CONFIGS: return TCLocalize("Configs");
	case SETTINGS_NAV_LEAF_BC_VISUALS: return BcLocalize("Visuals");
	case SETTINGS_NAV_LEAF_BC_GAMEPLAY: return BcLocalize("Gameplay");
	case SETTINGS_NAV_LEAF_BC_OTHERS: return BcLocalize("Others");
	case SETTINGS_NAV_LEAF_BC_INFO: return BcLocalize("Info");
	default: return Localize("Settings");
	}
}

static const char *GroupLabel(int Leaf)
{
	if(GroupForLeaf(Leaf) == SETTINGS_NAV_GROUP_TCLIENT)
		return TCLocalize("TClient");
	return Localize("BestClient");
}

static void FormatPath(const SEntry &Entry, char *pBuf, size_t BufSize)
{
	const char *pGroup = GroupLabel(Entry.m_Leaf);
	const char *pLeaf = LeafLabel(Entry.m_Leaf);
	const char *pSection = (Entry.m_pSection && Entry.m_pSection[0]) ? CatalogText(Entry, Entry.m_pSection) : nullptr;
	if(pSection && str_comp(pSection, pLeaf) != 0)
		str_format(pBuf, BufSize, "%s > %s > %s", pGroup, pLeaf, pSection);
	else
		str_format(pBuf, BufSize, "%s > %s", pGroup, pLeaf);
}

static bool EntryMatches(const SEntry &Entry, const char *pQuery)
{
	if(!pQuery || !pQuery[0])
		return false;
	return str_utf8_find_nocase(CatalogText(Entry, Entry.m_pName), pQuery) || str_utf8_find_nocase(Entry.m_pName, pQuery);
}

static void RebuildFilter()
{
	gs_vFiltered.clear();
	const char *pQuery = gs_SearchInput.GetString();
	if(!pQuery[0])
		return;
	for(int i = 0; i < (int)std::size(gs_aCatalog); ++i)
	{
		if(EntryMatches(gs_aCatalog[i], pQuery))
			gs_vFiltered.push_back(i);
	}
}

static void NavigateTo(CMenus *pMenus, const SEntry &Entry)
{
	ApplyLeaf(pMenus, Entry.m_Leaf);
	gs_pHighlightId = Entry.m_pId;
	if(Entry.m_pRequireEnabled && *Entry.m_pRequireEnabled == 0)
		gs_pHighlightId = Entry.m_pRequireEnabled;
	gs_HighlightTime = HIGHLIGHT_DURATION;
	gs_PendingScroll = true;
}

static bool gs_ForceSearchFocus = false;

static bool DoDarkSearchEdit(CUi *pUi, CLineInput *pLineInput, const CUIRect *pRect, float FontSize, bool HotkeyEnabled)
{
	static bool s_Focused = false;
	if(gs_ForceSearchFocus)
	{
		s_Focused = true;
		gs_ForceSearchFocus = false;
	}
	const bool Inside = pUi->MouseHovered(pRect);
	const bool Changed = pLineInput->WasChanged();
	const bool CursorChanged = pLineInput->WasCursorChanged();

	if(HotkeyEnabled && pUi->Input()->ModifierIsPressed() && pUi->Input()->KeyPress(KEY_F))
	{
		pUi->SetActiveItem(pLineInput);
		pLineInput->SelectAll();
		s_Focused = true;
	}

	if(pUi->MouseButtonClicked(0))
		s_Focused = Inside;

	if(Inside && !pUi->MouseButton(0))
		pUi->SetHotItem(pLineInput);

	if(Inside && pUi->MouseButton(0))
		pUi->SetActiveItem(pLineInput);
	else if(pUi->CheckActiveItem(pLineInput) && !pUi->MouseButton(0))
		pUi->SetActiveItem(nullptr);

	if(pUi->Enabled() && s_Focused)
		pLineInput->Activate(EInputPriority::UI);
	else
		pLineInput->Deactivate();

	pRect->Draw(ColorRGBA(1.0f, 1.0f, 1.0f, s_Focused ? 0.14f : (Inside ? 0.11f : 0.08f)), IGraphics::CORNER_ALL, 3.0f);

	CUIRect Textbox;
	pRect->VMargin(4.0f, &Textbox);
	pLineInput->SetEmptyText(Localize("Search"));
	float ScrollOffset = pLineInput->GetScrollOffset();
	Textbox.x -= ScrollOffset;
	pUi->ClipEnable(pRect);
	pLineInput->Render(&Textbox, FontSize, TEXTALIGN_ML, Changed || CursorChanged, -1.0f, 0.0f, {});
	pUi->ClipDisable();

	return Changed;
}

static CUi::EPopupMenuFunctionResult PopupRender(void *pContext, CUIRect View, bool Active)
{
	SPopupContext *pPopup = static_cast<SPopupContext *>(pContext);
	CMenus *pMenus = pPopup->m_pMenus;
	CUi *pUi = pPopup->m_pUi;
	CGameClient *pGameClient = pPopup->m_pGameClient;

	CUIRect SearchBar, List;
	View.HSplitTop(26.0f, &SearchBar, &List);
	List.HSplitTop(6.0f, nullptr, &List);

	DoDarkSearchEdit(pUi, &gs_SearchInput, &SearchBar, 13.0f, Active && !pGameClient->m_GameConsole.IsActive());
	RebuildFilter();

	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollbarThickness = 10.0f;
	ScrollParams.m_ScrollbarMargin = 4.0f;
	ScrollParams.m_ScrollbarNoOuterMargin = true;
	ScrollParams.m_ScrollUnit = 42.0f;
	ScrollParams.m_ForceShowScrollbar = gs_vFiltered.size() > 5;
	gs_ResultsScroll.Begin(&List, &ScrollParams);

	if(gs_vFiltered.empty())
	{
		CUIRect Empty;
		List.HSplitTop(24.0f, &Empty, &List);
		if(gs_ResultsScroll.AddRect(Empty))
		{
			const char *pEmpty = gs_SearchInput.IsEmpty() ? BcLocalize("Type to search settings") : Localize("No results");
			pUi->DoLabel(&Empty, pEmpty, 12.0f, TEXTALIGN_MC);
		}
	}
	else
	{
		for(size_t i = 0; i < gs_vFiltered.size(); ++i)
		{
			const int Index = gs_vFiltered[i];
			const SEntry &Entry = gs_aCatalog[Index];
			CUIRect Row, Title, Path;
			if(i > 0)
				List.HSplitTop(2.0f, nullptr, &List);
			List.HSplitTop(40.0f, &Row, &List);
			if(!gs_ResultsScroll.AddRect(Row))
				continue;

			if(pUi->HotItem() == &gs_aCatalog[Index])
				Row.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f), IGraphics::CORNER_ALL, 4.0f);

			Row.HSplitTop(18.0f, &Title, &Path);
			pUi->DoLabel(&Title, CatalogText(Entry, Entry.m_pName), 13.0f, TEXTALIGN_ML);

			char aPath[256];
			FormatPath(Entry, aPath, sizeof(aPath));
			SLabelProperties Props;
			Props.SetColor(ColorRGBA(0.7f, 0.7f, 0.7f, 1.0f));
			pUi->DoLabel(&Path, aPath, 11.0f, TEXTALIGN_ML, Props);

			if(Active && pUi->DoButtonLogic(&gs_aCatalog[Index], 0, &Row, BUTTONFLAG_LEFT))
			{
				NavigateTo(pMenus, Entry);
				gs_ResultsScroll.End();
				return CUi::POPUP_CLOSE_CURRENT;
			}
		}
	}

	gs_ResultsScroll.End();
	return CUi::POPUP_KEEP_OPEN;
}

static void OpenSearchPopup(CMenus *pMenus, CUi *pUi, CGameClient *pGameClient, const CUIRect &Anchor)
{
	gs_PopupContext.m_pMenus = pMenus;
	gs_PopupContext.m_pUi = pUi;
	gs_PopupContext.m_pGameClient = pGameClient;
	const float PopupW = 320.0f;
	const float PopupH = 320.0f;
	const float X = BestClientUiTheme::IsSettingsNavOnLeft() ? Anchor.x : (Anchor.x + Anchor.w - PopupW);
	const float Y = Anchor.y + Anchor.h + 4.0f;
	pUi->DoPopupMenu(&gs_PopupContext, X, Y, PopupW, PopupH, &gs_PopupContext, PopupRender);
	gs_ForceSearchFocus = true;
	pUi->SetActiveItem(&gs_SearchInput);
	gs_SearchInput.SelectAll();
}

void RenderCornerButton(CMenus *pMenus, CUi *pUi, CGameClient *pGameClient, IInput *pInput, CUIRect Corner)
{
	CUIRect Button = Corner;
	if(BestClientUiTheme::IsSettingsNavOnLeft())
	{
		Button.VSplitLeft(12.0f, nullptr, &Button);
		Button.VSplitLeft(28.0f, &Button, nullptr);
	}
	else
	{
		Button.VSplitRight(12.0f, &Button, nullptr);
		Button.VSplitRight(28.0f, nullptr, &Button);
	}
	Button.HMargin(11.0f, &Button);

	static char s_IconId;
	const bool Open = pUi->IsPopupOpen(&gs_PopupContext);
	const bool Hot = pUi->HotItem() == &s_IconId;

	pUi->TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	pUi->TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	{
		SLabelProperties Props;
		Props.SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, Open || Hot ? 1.0f : 0.65f));
		pUi->DoLabel(&Button, FontIcon::MAGNIFYING_GLASS, Button.h * 0.85f, TEXTALIGN_MC, Props);
	}
	pUi->TextRender()->SetRenderFlags(0);
	pUi->TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

	if(pUi->DoButtonLogic(&s_IconId, 0, &Button, BUTTONFLAG_LEFT))
	{
		if(Open)
			pUi->ClosePopupMenu(&gs_PopupContext);
		else
			OpenSearchPopup(pMenus, pUi, pGameClient, Button);
	}

	if(!pUi->IsPopupOpen() && !pGameClient->m_GameConsole.IsActive() && pInput->ModifierIsPressed() && pInput->KeyPress(KEY_F))
		OpenSearchPopup(pMenus, pUi, pGameClient, Button);
}

void Update(float DeltaTime)
{
	gs_pBlockHighlightDrawn = nullptr;
	if(gs_HighlightTime <= 0.0f)
		return;
	gs_HighlightTime -= DeltaTime;
	if(gs_HighlightTime <= 0.0f)
	{
		gs_HighlightTime = 0.0f;
		gs_pHighlightId = nullptr;
		gs_PendingScroll = false;
	}
}

static float HighlightAlpha()
{
	if(gs_HighlightTime <= 0.0f || !gs_pHighlightId)
		return 0.0f;
	const float T = gs_HighlightTime / HIGHLIGHT_DURATION;
	if(T > 0.7f)
		return 1.0f;
	return std::clamp(T / 0.7f, 0.0f, 1.0f);
}

static void ApplyHighlight(const CUIRect *pRect)
{
	const float Alpha = HighlightAlpha();
	if(Alpha <= 0.0f)
		return;

	CUIRect Glow = *pRect;
	Glow.Draw(ColorRGBA(HIGHLIGHT_COLOR.r, HIGHLIGHT_COLOR.g, HIGHLIGHT_COLOR.b, 0.18f * Alpha), IGraphics::CORNER_ALL, 4.0f);
	Glow.DrawOutline(ColorRGBA(HIGHLIGHT_COLOR.r, HIGHLIGHT_COLOR.g, HIGHLIGHT_COLOR.b, Alpha));

	if(gs_PendingScroll && gs_pActiveScroll)
	{
		gs_pActiveScroll->AddRect(*pRect, false);
		gs_pActiveScroll->ScrollHere(CScrollRegion::SCROLLHERE_CENTER, true);
		gs_PendingScroll = false;
	}
}

void DrawBlockHighlight(const void *pId, const CUIRect *pRect)
{
	if(!pId || !pRect || pId != gs_pHighlightId)
		return;
	gs_pBlockHighlightDrawn = pId;
	ApplyHighlight(pRect);
}

void DrawItemHighlight(const void *pId, const CUIRect *pRect)
{
	if(!pId || !pRect || pId != gs_pHighlightId)
		return;
	if(pId == gs_pBlockHighlightDrawn)
		return;
	ApplyHighlight(pRect);
}

void SetActiveScrollRegion(CScrollRegion *pRegion)
{
	if(!pRegion)
		return;
	gs_vScrollStack.push_back(pRegion);
	gs_pActiveScroll = pRegion;
}

void ClearActiveScrollRegion(CScrollRegion *pRegion)
{
	if(!pRegion)
		return;
	for(int i = (int)gs_vScrollStack.size() - 1; i >= 0; --i)
	{
		if(gs_vScrollStack[i] != pRegion)
			continue;
		gs_vScrollStack.erase(gs_vScrollStack.begin() + i);
		break;
	}
	gs_pActiveScroll = gs_vScrollStack.empty() ? nullptr : gs_vScrollStack.back();
}

} // namespace BestClientSettingsSearch
