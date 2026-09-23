/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "ui.h"
#include "ui_scrollregion.h"

#include <base/color.h>
#include <base/dbg.h>
#include <base/math.h>
#include <base/str.h>

#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/input.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/bestclient/bestclient.h> // bestclient
#include <game/client/components/bestclient/ui_theme/style.h> // bestclient
#include <game/localization.h>

#include <algorithm>

void CUi::DoPopupMenu(const SPopupMenuId *pId, float X, float Y, float Width, float Height, void *pContext, FPopupMenuFunction pfnFunc, const SPopupMenuProperties &Props)
{
	constexpr float Margin = SPopupMenu::POPUP_BORDER + SPopupMenu::POPUP_MARGIN;
	if(X + Width > Screen()->w - Margin)
		X = std::max(X - Width, Margin);
	if(Y + Height > Screen()->h - Margin)
		Y = std::max(Y - Height, Margin);

	m_vPopupMenus.emplace_back();
	SPopupMenu *pNewMenu = &m_vPopupMenus.back();
	pNewMenu->m_pId = pId;
	pNewMenu->m_Props = Props;
	pNewMenu->m_Rect.x = X;
	pNewMenu->m_Rect.y = Y;
	pNewMenu->m_Rect.w = Width;
	pNewMenu->m_Rect.h = Height;
	pNewMenu->m_pContext = pContext;
	pNewMenu->m_pfnFunc = pfnFunc;
}

void CUi::RenderPopupMenus()
{
	for(size_t i = 0; i < m_vPopupMenus.size(); ++i)
	{
		SPopupMenu &PopupMenu = m_vPopupMenus[i];
		const SPopupMenuId *pId = PopupMenu.m_pId;
		const bool Inside = MouseInside(&PopupMenu.m_Rect);
		const bool Active = i == m_vPopupMenus.size() - 1;

		// bestclient
		bool SkipActiveLogic = false;
		if(PopupMenu.m_Props.m_Draggable)
		{
			CUIRect DragHandle = PopupMenu.m_Rect;
			DragHandle.h = SPopupMenu::POPUP_BORDER + SPopupMenu::POPUP_MARGIN + SPopupMenu::POPUP_DRAG_HANDLE_HEIGHT;
			if(PopupMenu.m_Dragging)
			{
				const vec2 GuiScale = vec2(Screen()->w, Screen()->h) / vec2((float)Graphics()->WindowWidth(), (float)Graphics()->WindowHeight());
				PopupMenu.m_Rect.x += MouseDeltaX() * GuiScale.x;
				PopupMenu.m_Rect.y += MouseDeltaY() * GuiScale.y;
				PopupMenu.m_Rect.x = std::clamp(PopupMenu.m_Rect.x, 0.0f, std::max(0.0f, Screen()->w - PopupMenu.m_Rect.w));
				PopupMenu.m_Rect.y = std::clamp(PopupMenu.m_Rect.y, 0.0f, std::max(0.0f, Screen()->h - PopupMenu.m_Rect.h));
				if(!MouseButton(0))
				{
					PopupMenu.m_Dragging = false;
					SetActiveItem(nullptr);
				}
				SkipActiveLogic = true;
			}
			else if(MouseButtonClicked(0) && MouseInside(&DragHandle))
			{
				PopupMenu.m_Dragging = true;
				SkipActiveLogic = true;
			}
		}
		// bestclient

		if(Active)
		{
			// Prevent UI elements below the popup menu from being activated.
			SetHotItem(pId);
		}

		if(!SkipActiveLogic)
		{
			if(CheckActiveItem(pId))
			{
				if(!MouseButton(0))
				{
					if(!Inside)
					{
						ClosePopupMenu(pId);
						--i;
						continue;
					}
					SetActiveItem(nullptr);
				}
			}
			else if(HotItem() == pId)
			{
				if(MouseButton(0))
					SetActiveItem(pId);
			}
		}

		if(Inside)
		{
			// Prevent scroll regions directly behind popup menus from using the mouse scroll events.
			SetHotScrollRegion(nullptr);
		}

		CUIRect PopupRect = PopupMenu.m_Rect;
		PopupRect.Draw(PopupMenu.m_Props.m_BorderColor, PopupMenu.m_Props.m_Corners, 3.0f);
		PopupRect.Margin(SPopupMenu::POPUP_BORDER, &PopupRect);
		PopupRect.Draw(PopupMenu.m_Props.m_BackgroundColor, PopupMenu.m_Props.m_Corners, 3.0f);
		PopupRect.Margin(SPopupMenu::POPUP_MARGIN, &PopupRect);
		// bestclient
		if(PopupMenu.m_Props.m_Draggable)
		{
			CUIRect DragHint = PopupRect;
			DragHint.HSplitTop(SPopupMenu::POPUP_DRAG_HANDLE_HEIGHT, &DragHint, &PopupRect);
			DragHint.Margin(2.0f, &DragHint);
			DragHint.h = 2.0f;
			DragHint.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), IGraphics::CORNER_ALL, 1.0f);
		}
		// bestclient

		// The popup render function can open/close popups, which may resize the vector and thus
		// invalidate the variable PopupMenu. We therefore store pId in a separate variable.
		EPopupMenuFunctionResult Result = PopupMenu.m_pfnFunc(PopupMenu.m_pContext, PopupRect, Active);
		if(Result != POPUP_KEEP_OPEN || (Active && ConsumeHotkey(HOTKEY_ESCAPE)))
			ClosePopupMenu(pId, Result == POPUP_CLOSE_CURRENT_AND_DESCENDANTS);
	}
}

// bestclient
bool CUi::GetPopupPosition(const SPopupMenuId *pId, float &X, float &Y) const
{
	for(const SPopupMenu &PopupMenu : m_vPopupMenus)
	{
		if(PopupMenu.m_pId != pId)
			continue;
		X = PopupMenu.m_Rect.x;
		Y = PopupMenu.m_Rect.y;
		return true;
	}
	return false;
}

void CUi::SetPopupPosition(const SPopupMenuId *pId, float X, float Y)
{
	for(SPopupMenu &PopupMenu : m_vPopupMenus)
	{
		if(PopupMenu.m_pId != pId)
			continue;
		PopupMenu.m_Rect.x = std::clamp(X, 0.0f, std::max(0.0f, Screen()->w - PopupMenu.m_Rect.w));
		PopupMenu.m_Rect.y = std::clamp(Y, 0.0f, std::max(0.0f, Screen()->h - PopupMenu.m_Rect.h));
		break;
	}
}

void CUi::SetPopupMenuHeight(const SPopupMenuId *pId, float Height)
{
	constexpr float Margin = SPopupMenu::POPUP_BORDER + SPopupMenu::POPUP_MARGIN;
	for(SPopupMenu &PopupMenu : m_vPopupMenus)
	{
		if(PopupMenu.m_pId != pId)
			continue;
		const float Bottom = PopupMenu.m_Rect.y + PopupMenu.m_Rect.h;
		PopupMenu.m_Rect.h = Height;
		PopupMenu.m_Rect.y = Bottom - Height;
		PopupMenu.m_Rect.y = std::max(PopupMenu.m_Rect.y, Margin);
		break;
	}
}
// bestclient

void CUi::ClosePopupMenu(const SPopupMenuId *pId, bool IncludeDescendants)
{
	auto PopupMenuToClose = std::find_if(m_vPopupMenus.begin(), m_vPopupMenus.end(), [pId](const SPopupMenu &PopupMenu) { return PopupMenu.m_pId == pId; });
	if(PopupMenuToClose != m_vPopupMenus.end())
	{
		if(IncludeDescendants)
			m_vPopupMenus.erase(PopupMenuToClose, m_vPopupMenus.end());
		else
			m_vPopupMenus.erase(PopupMenuToClose);
		SetActiveItem(nullptr);
		if(m_pfnPopupMenuClosedCallback)
			m_pfnPopupMenuClosedCallback();
	}
}

void CUi::ClosePopupMenus()
{
	if(m_vPopupMenus.empty())
		return;

	m_vPopupMenus.clear();
	SetActiveItem(nullptr);
	if(m_pfnPopupMenuClosedCallback)
		m_pfnPopupMenuClosedCallback();
}

bool CUi::IsPopupOpen() const
{
	return !m_vPopupMenus.empty();
}

bool CUi::IsPopupOpen(const SPopupMenuId *pId) const
{
	return std::any_of(m_vPopupMenus.begin(), m_vPopupMenus.end(), [pId](const SPopupMenu PopupMenu) { return PopupMenu.m_pId == pId; });
}

bool CUi::IsPopupHovered() const
{
	return std::any_of(m_vPopupMenus.begin(), m_vPopupMenus.end(), [this](const SPopupMenu PopupMenu) { return MouseHovered(&PopupMenu.m_Rect); });
}

void CUi::SetPopupMenuClosedCallback(FPopupMenuClosedCallback pfnCallback)
{
	m_pfnPopupMenuClosedCallback = std::move(pfnCallback);
}

void CUi::SMessagePopupContext::DefaultColor(ITextRender *pTextRender)
{
	m_TextColor = pTextRender->DefaultTextColor();
}

void CUi::SMessagePopupContext::ErrorColor()
{
	m_TextColor = ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f);
}

CUi::EPopupMenuFunctionResult CUi::PopupMessage(void *pContext, CUIRect View, bool Active)
{
	SMessagePopupContext *pMessagePopup = static_cast<SMessagePopupContext *>(pContext);
	CUi *pUI = pMessagePopup->m_pUI;

	pUI->TextRender()->TextColor(pMessagePopup->m_TextColor);
	pUI->TextRender()->Text(View.x, View.y, SMessagePopupContext::POPUP_FONT_SIZE, pMessagePopup->m_aMessage, View.w);
	pUI->TextRender()->TextColor(pUI->TextRender()->DefaultTextColor());

	return (Active && pUI->ConsumeHotkey(HOTKEY_ENTER)) ? CUi::POPUP_CLOSE_CURRENT : CUi::POPUP_KEEP_OPEN;
}

void CUi::ShowPopupMessage(float X, float Y, SMessagePopupContext *pContext)
{
	const float TextWidth = std::min(std::ceil(TextRender()->TextWidth(SMessagePopupContext::POPUP_FONT_SIZE, pContext->m_aMessage, -1, -1.0f) + 0.5f), SMessagePopupContext::POPUP_MAX_WIDTH);
	float TextHeight = 0.0f;
	STextSizeProperties TextSizeProps{};
	TextSizeProps.m_pHeight = &TextHeight;
	TextRender()->TextWidth(SMessagePopupContext::POPUP_FONT_SIZE, pContext->m_aMessage, -1, TextWidth, 0, TextSizeProps);
	pContext->m_pUI = this;
	DoPopupMenu(pContext, X, Y, TextWidth + 10.0f, TextHeight + 10.0f, pContext, PopupMessage);
}

CUi::SConfirmPopupContext::SConfirmPopupContext()
{
	Reset();
}

void CUi::SConfirmPopupContext::Reset()
{
	m_Result = SConfirmPopupContext::UNSET;
}

void CUi::SConfirmPopupContext::YesNoButtons()
{
	str_copy(m_aPositiveButtonLabel, Localize("Yes"));
	str_copy(m_aNegativeButtonLabel, Localize("No"));
}

void CUi::ShowPopupConfirm(float X, float Y, SConfirmPopupContext *pContext)
{
	const float TextWidth = std::min(std::ceil(TextRender()->TextWidth(SConfirmPopupContext::POPUP_FONT_SIZE, pContext->m_aMessage, -1, -1.0f) + 0.5f), SConfirmPopupContext::POPUP_MAX_WIDTH);
	float TextHeight = 0.0f;
	STextSizeProperties TextSizeProps{};
	TextSizeProps.m_pHeight = &TextHeight;
	TextRender()->TextWidth(SConfirmPopupContext::POPUP_FONT_SIZE, pContext->m_aMessage, -1, TextWidth, 0, TextSizeProps);
	const float PopupHeight = TextHeight + SConfirmPopupContext::POPUP_BUTTON_HEIGHT + SConfirmPopupContext::POPUP_BUTTON_SPACING + 10.0f;
	pContext->m_pUI = this;
	pContext->m_Result = SConfirmPopupContext::UNSET;
	DoPopupMenu(pContext, X, Y, TextWidth + 10.0f, PopupHeight, pContext, PopupConfirm);
}

CUi::EPopupMenuFunctionResult CUi::PopupConfirm(void *pContext, CUIRect View, bool Active)
{
	SConfirmPopupContext *pConfirmPopup = static_cast<SConfirmPopupContext *>(pContext);
	CUi *pUI = pConfirmPopup->m_pUI;

	CUIRect Label, ButtonBar, CancelButton, ConfirmButton;
	View.HSplitBottom(SConfirmPopupContext::POPUP_BUTTON_HEIGHT, &Label, &ButtonBar);
	ButtonBar.VSplitMid(&CancelButton, &ConfirmButton, SConfirmPopupContext::POPUP_BUTTON_SPACING);

	pUI->TextRender()->Text(Label.x, Label.y, SConfirmPopupContext::POPUP_FONT_SIZE, pConfirmPopup->m_aMessage, Label.w);

	if(pUI->DoButton_PopupMenu(&pConfirmPopup->m_CancelButton, pConfirmPopup->m_aNegativeButtonLabel, &CancelButton, SConfirmPopupContext::POPUP_FONT_SIZE, TEXTALIGN_MC))
	{
		pConfirmPopup->m_Result = SConfirmPopupContext::CANCELED;
		return CUi::POPUP_CLOSE_CURRENT;
	}

	if(pUI->DoButton_PopupMenu(&pConfirmPopup->m_ConfirmButton, pConfirmPopup->m_aPositiveButtonLabel, &ConfirmButton, SConfirmPopupContext::POPUP_FONT_SIZE, TEXTALIGN_MC) || (Active && pUI->ConsumeHotkey(HOTKEY_ENTER)))
	{
		pConfirmPopup->m_Result = SConfirmPopupContext::CONFIRMED;
		return CUi::POPUP_CLOSE_CURRENT;
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::SSelectionPopupContext::SSelectionPopupContext()
{
	Reset();
}

void CUi::SSelectionPopupContext::Reset()
{
	m_Props = SPopupMenuProperties();
	m_aMessage[0] = '\0';
	m_pSelection = nullptr;
	m_SelectionIndex = -1;
	m_vEntries.clear();
	m_vButtonContainers.clear();
	m_EntryHeight = 12.0f;
	m_EntryPadding = 0.0f;
	m_EntrySpacing = 5.0f;
	m_FontSize = 10.0f;
	m_Width = 300.0f + (SPopupMenu::POPUP_BORDER + SPopupMenu::POPUP_MARGIN) * 2;
	m_AlignmentHeight = -1.0f;
	m_TransparentButtons = false;
	// bestclient
	m_OpenUpward = false;
	// bestclient
}

CUi::EPopupMenuFunctionResult CUi::PopupSelection(void *pContext, CUIRect View, bool Active)
{
	SSelectionPopupContext *pSelectionPopup = static_cast<SSelectionPopupContext *>(pContext);
	CUi *pUI = pSelectionPopup->m_pUI;
	CScrollRegion *pScrollRegion = pSelectionPopup->m_pScrollRegion;

	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollbarThickness = 10.0f;
	ScrollParams.m_ScrollbarMargin = SPopupMenu::POPUP_MARGIN;
	ScrollParams.m_ScrollbarNoOuterMargin = true;
	ScrollParams.m_ScrollUnit = 3 * (pSelectionPopup->m_EntryHeight + pSelectionPopup->m_EntrySpacing);
	pScrollRegion->Begin(&View, &ScrollParams);

	CUIRect Slot;
	if(pSelectionPopup->m_aMessage[0] != '\0')
	{
		const STextBoundingBox TextBoundingBox = pUI->TextRender()->TextBoundingBox(pSelectionPopup->m_FontSize, pSelectionPopup->m_aMessage, -1, pSelectionPopup->m_Width);
		View.HSplitTop(TextBoundingBox.m_H, &Slot, &View);
		if(pScrollRegion->AddRect(Slot))
		{
			pUI->TextRender()->Text(Slot.x, Slot.y, pSelectionPopup->m_FontSize, pSelectionPopup->m_aMessage, Slot.w);
		}
	}

	pSelectionPopup->m_vButtonContainers.resize(pSelectionPopup->m_vEntries.size());

	size_t Index = 0;
	for(const auto &Entry : pSelectionPopup->m_vEntries)
	{
		// TClient: preview every font name using that font.
		if(pSelectionPopup->m_SpecialFontRenderMode)
			pUI->TextRender()->SetCustomFace(Entry.c_str());

		if(pSelectionPopup->m_aMessage[0] != '\0' || Index != 0)
			View.HSplitTop(pSelectionPopup->m_EntrySpacing, nullptr, &View);
		View.HSplitTop(pSelectionPopup->m_EntryHeight, &Slot, &View);
		if(pScrollRegion->AddRect(Slot))
		{
			if(pUI->DoButton_PopupMenu(&pSelectionPopup->m_vButtonContainers[Index], Entry.c_str(), &Slot, pSelectionPopup->m_FontSize, TEXTALIGN_ML, pSelectionPopup->m_EntryPadding, pSelectionPopup->m_TransparentButtons))
			{
				pSelectionPopup->m_pSelection = &Entry;
				pSelectionPopup->m_SelectionIndex = Index;
			}
		}
		++Index;
	}
	if(pSelectionPopup->m_SpecialFontRenderMode)
		BestClientApplyMenuFont(pUI->TextRender()); // bestclient

	pScrollRegion->End();

	return pSelectionPopup->m_pSelection == nullptr ? CUi::POPUP_KEEP_OPEN : CUi::POPUP_CLOSE_CURRENT;
}

void CUi::ShowPopupSelection(float X, float Y, SSelectionPopupContext *pContext)
{
	const STextBoundingBox TextBoundingBox = TextRender()->TextBoundingBox(pContext->m_FontSize, pContext->m_aMessage, -1, pContext->m_Width);
	const float PopupHeight = std::min((pContext->m_aMessage[0] == '\0' ? -pContext->m_EntrySpacing : TextBoundingBox.m_H) + pContext->m_vEntries.size() * (pContext->m_EntryHeight + pContext->m_EntrySpacing) + (SPopupMenu::POPUP_BORDER + SPopupMenu::POPUP_MARGIN) * 2, Screen()->h * 0.4f);
	pContext->m_pUI = this;
	pContext->m_pSelection = nullptr;
	pContext->m_SelectionIndex = -1;
	pContext->m_Props.m_Corners = IGraphics::CORNER_ALL;
	if(pContext->m_AlignmentHeight >= 0.0f)
	{
		constexpr float Margin = SPopupMenu::POPUP_BORDER + SPopupMenu::POPUP_MARGIN;
		if(X + pContext->m_Width > Screen()->w - Margin)
		{
			X = std::max(X - pContext->m_Width, Margin);
		}
		// bestclient
		if(pContext->m_OpenUpward || Y + pContext->m_AlignmentHeight + PopupHeight > Screen()->h - Margin)
		{
			Y -= PopupHeight;
			pContext->m_Props.m_Corners = IGraphics::CORNER_T;
		}
		else
		{
			Y += pContext->m_AlignmentHeight;
			pContext->m_Props.m_Corners = IGraphics::CORNER_B;
		}
		// bestclient
	}
	DoPopupMenu(pContext, X, Y, pContext->m_Width, PopupHeight, pContext, PopupSelection, pContext->m_Props);
}

// bestclient
int CUi::DoDropDown(CUIRect *pRect, int CurSelection, const char **pStrs, int Num, SDropDownState &State)
{
	return DoDropDown(pRect, CurSelection, pStrs, Num, State, false);
}

int CUi::DoDropDown(CUIRect *pRect, int CurSelection, const char **pStrs, int Num, SDropDownState &State, bool OpenUpward)
{
	if(!State.m_Init)
	{
		State.m_UiElement.Init(this, -1);
		State.m_Init = true;
	}

	const auto LabelFunc = [CurSelection, pStrs]() {
		return CurSelection > -1 ? pStrs[CurSelection] : "";
	};

	SMenuButtonProperties Props;
	Props.m_HintRequiresStringCheck = true;
	Props.m_HintCanChangePositionOrSize = true;
	Props.m_ShowDropDownIcon = true;
	if(IsPopupOpen(&State.m_SelectionPopupContext))
		Props.m_Corners = IGraphics::CORNER_ALL & (~State.m_SelectionPopupContext.m_Props.m_Corners);
	if(DoButton_Menu(State.m_UiElement, &State.m_ButtonContainer, LabelFunc, pRect, Props))
	{
		State.m_SelectionPopupContext.Reset();
		State.m_SelectionPopupContext.m_Props.m_BorderColor = BestClientUiTheme::UiDropdown(ColorRGBA(0.7f, 0.7f, 0.7f, 0.9f));
		State.m_SelectionPopupContext.m_Props.m_BackgroundColor = BestClientUiTheme::IsCustomUiDropdown() ? BestClientUiTheme::UiDropdown(0.25f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f);
		for(int i = 0; i < Num; ++i)
			State.m_SelectionPopupContext.m_vEntries.emplace_back(pStrs[i]);
		State.m_SelectionPopupContext.m_EntryHeight = pRect->h;
		State.m_SelectionPopupContext.m_EntryPadding = pRect->h >= 20.0f ? 2.0f : 1.0f;
		State.m_SelectionPopupContext.m_FontSize = (State.m_SelectionPopupContext.m_EntryHeight - 2 * State.m_SelectionPopupContext.m_EntryPadding) * CUi::ms_FontmodHeight;
		State.m_SelectionPopupContext.m_Width = pRect->w;
		State.m_SelectionPopupContext.m_AlignmentHeight = pRect->h;
		State.m_SelectionPopupContext.m_TransparentButtons = true;
		State.m_SelectionPopupContext.m_OpenUpward = OpenUpward;
		ShowPopupSelection(pRect->x, pRect->y, &State.m_SelectionPopupContext);
	}

	if(State.m_SelectionPopupContext.m_SelectionIndex >= 0)
	{
		const int NewSelection = State.m_SelectionPopupContext.m_SelectionIndex;
		State.m_SelectionPopupContext.Reset();
		return NewSelection;
	}

	return CurSelection;
}
// bestclient

// bestclient
static CUi::EPopupMenuFunctionResult PopupColorPickerNewStyle(CUi::SColorPickerPopupContext *pColorPicker, CUIRect View, bool Active)
{
	CUi *pUI = pColorPicker->m_pUI;
	pColorPicker->m_State = EEditState::NONE;

	CUIRect ColorsArea, HueArea, AlphaArea, BottomArea, HexRect, CopyRect, ResetRect;

	const bool ShowAlpha = pColorPicker->m_Alpha;
	View.HSplitBottom(24.0f, &View, &BottomArea);
	View.HSplitBottom(8.0f, &View, nullptr);
	if(ShowAlpha)
	{
		View.HSplitBottom(18.0f, &ColorsArea, &AlphaArea);
		View.HSplitBottom(22.0f, &ColorsArea, nullptr);
	}
	else
	{
		ColorsArea = View;
		AlphaArea = {};
	}
	ColorsArea.VSplitRight(18.0f, &ColorsArea, &HueArea);
	HueArea.VSplitLeft(4.0f, nullptr, &HueArea);

	const ColorRGBA BlackColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f);
	HueArea.Draw(BlackColor, IGraphics::CORNER_ALL, 3.0f);
	HueArea.Margin(1.0f, &HueArea);
	ColorsArea.Draw(BlackColor, IGraphics::CORNER_ALL, 3.0f);
	ColorsArea.Margin(1.0f, &ColorsArea);

	ColorHSVA PickerColorHSV = pColorPicker->m_HsvaColor;
	ColorRGBA PickerColorRGB = pColorPicker->m_RgbaColor;
	ColorHSLA PickerColorHSL = pColorPicker->m_HslaColor;

	ColorRGBA TL, TR, BL, BR;
	TL = BL = color_cast<ColorRGBA>(ColorHSVA(PickerColorHSV.x, 0.0f, 1.0f));
	TR = BR = color_cast<ColorRGBA>(ColorHSVA(PickerColorHSV.x, 1.0f, 1.0f));
	ColorsArea.Draw4(TL, TR, BL, BR, IGraphics::CORNER_NONE, 0.0f);
	TL = TR = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
	BL = BR = ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f);
	ColorsArea.Draw4(TL, TR, BL, BR, IGraphics::CORNER_NONE, 0.0f);

	static const float s_aaColorIndices[7][3] = {
		{1.0f, 0.0f, 0.0f},
		{1.0f, 0.0f, 1.0f},
		{0.0f, 0.0f, 1.0f},
		{0.0f, 1.0f, 1.0f},
		{0.0f, 1.0f, 0.0f},
		{1.0f, 1.0f, 0.0f},
		{1.0f, 0.0f, 0.0f},
	};
	const float HuePickerOffset = HueArea.h / 6.0f;
	CUIRect HuePartialArea = HueArea;
	HuePartialArea.h = HuePickerOffset;
	for(size_t j = 0; j < std::size(s_aaColorIndices) - 1; j++)
	{
		TL = ColorRGBA(s_aaColorIndices[j][0], s_aaColorIndices[j][1], s_aaColorIndices[j][2], 1.0f);
		BL = ColorRGBA(s_aaColorIndices[j + 1][0], s_aaColorIndices[j + 1][1], s_aaColorIndices[j + 1][2], 1.0f);
		HuePartialArea.y = HueArea.y + HuePickerOffset * j;
		HuePartialArea.Draw4(TL, TL, BL, BL, IGraphics::CORNER_NONE, 0.0f);
	}

	float PickerX, PickerY;
	EEditState ColorPickerRes = pUI->DoPickerLogic(&pColorPicker->m_ColorPickerId, &ColorsArea, &PickerX, &PickerY);
	if(ColorPickerRes != EEditState::NONE)
	{
		PickerColorHSV.y = PickerX / ColorsArea.w;
		PickerColorHSV.z = 1.0f - PickerY / ColorsArea.h;
		PickerColorHSL = color_cast<ColorHSLA>(PickerColorHSV);
		PickerColorRGB = color_cast<ColorRGBA>(PickerColorHSL);
		pColorPicker->m_State = ColorPickerRes;
	}

	EEditState HuePickerRes = pUI->DoPickerLogic(&pColorPicker->m_HuePickerId, &HueArea, &PickerX, &PickerY);
	if(HuePickerRes != EEditState::NONE)
	{
		PickerColorHSV.x = 1.0f - PickerY / HueArea.h;
		PickerColorHSL = color_cast<ColorHSLA>(PickerColorHSV);
		PickerColorRGB = color_cast<ColorRGBA>(PickerColorHSL);
		pColorPicker->m_State = HuePickerRes;
	}

	const float MarkerX = ColorsArea.x + ColorsArea.w * PickerColorHSV.y;
	const float MarkerY = ColorsArea.y + ColorsArea.h * (1.0f - PickerColorHSV.z);
	const float MarkerOutlineInd = PickerColorHSV.z > 0.5f ? 0.0f : 1.0f;
	const ColorRGBA MarkerOutline = ColorRGBA(MarkerOutlineInd, MarkerOutlineInd, MarkerOutlineInd, 1.0f);
	pUI->Graphics()->TextureClear();
	pUI->Graphics()->QuadsBegin();
	pUI->Graphics()->SetColor(MarkerOutline);
	pUI->Graphics()->DrawCircle(MarkerX, MarkerY, 4.5f, 32);
	pUI->Graphics()->SetColor(PickerColorRGB.WithAlpha(1.0f));
	pUI->Graphics()->DrawCircle(MarkerX, MarkerY, 3.5f, 32);
	pUI->Graphics()->QuadsEnd();

	CUIRect HueMarker;
	HueArea.Margin(-2.5f, &HueMarker);
	HueMarker.h = 6.5f;
	HueMarker.y = (HueArea.y + HueArea.h * (1.0f - PickerColorHSV.x)) - HueMarker.h / 2.0f;
	const ColorRGBA HueMarkerColor = color_cast<ColorRGBA>(ColorHSVA(PickerColorHSV.x, 1.0f, 1.0f, 1.0f));
	const float HueMarkerOutlineColor = PickerColorHSV.x > 0.75f ? 1.0f : 0.0f;
	HueMarker.Draw(ColorRGBA(HueMarkerOutlineColor, HueMarkerOutlineColor, HueMarkerOutlineColor, 1.0f), IGraphics::CORNER_ALL, 1.2f);
	HueMarker.Margin(1.2f, &HueMarker);
	HueMarker.Draw(HueMarkerColor, IGraphics::CORNER_ALL, 1.2f);

	{
		if(ShowAlpha)
		{
			CUIRect AlphaRail = AlphaArea;
			AlphaRail.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), IGraphics::CORNER_ALL, 3.0f);
			AlphaRail.Margin(1.0f, &AlphaRail);
			const ColorRGBA Opaque = PickerColorRGB.WithAlpha(1.0f);
			const ColorRGBA Faded = ColorRGBA(Opaque.r * 0.12f, Opaque.g * 0.12f, Opaque.b * 0.12f, 1.0f);
			AlphaRail.Draw4(Faded, Opaque, Faded, Opaque, IGraphics::CORNER_ALL, 2.0f);

			EEditState AlphaRes = pUI->DoPickerLogic(&pColorPicker->m_AlphaPickerId, &AlphaRail, &PickerX, &PickerY);
			if(AlphaRes != EEditState::NONE)
			{
				PickerColorHSV.a = std::clamp(PickerX / AlphaRail.w, 0.0f, 1.0f);
				PickerColorRGB.a = PickerColorHSV.a;
				PickerColorHSL.a = PickerColorHSV.a;
				pColorPicker->m_State = AlphaRes;
			}

			const float AlphaPos = PickerColorHSV.a;
			const ColorRGBA UnderColor = ColorRGBA(
				Faded.r + (Opaque.r - Faded.r) * AlphaPos,
				Faded.g + (Opaque.g - Faded.g) * AlphaPos,
				Faded.b + (Opaque.b - Faded.b) * AlphaPos,
				1.0f);
			const float UnderLum = 0.299f * UnderColor.r + 0.587f * UnderColor.g + 0.114f * UnderColor.b;
			const float AlphaOutlineInd = UnderLum > 0.45f ? 0.0f : 1.0f;
			const ColorRGBA AlphaOutline = ColorRGBA(AlphaOutlineInd, AlphaOutlineInd, AlphaOutlineInd, 1.0f);

			CUIRect AlphaMarker = AlphaRail;
			AlphaMarker.w = 6.5f;
			AlphaMarker.x = AlphaRail.x + AlphaRail.w * AlphaPos - AlphaMarker.w / 2.0f;
			AlphaMarker.Draw(AlphaOutline, IGraphics::CORNER_ALL, 1.2f);
			AlphaMarker.Margin(1.2f, &AlphaMarker);
			AlphaMarker.Draw(Opaque, IGraphics::CORNER_ALL, 1.2f);
		}
		else
		{
			PickerColorHSV.a = 1.0f;
			PickerColorRGB.a = 1.0f;
			PickerColorHSL.a = 1.0f;
		}
	}

	BottomArea.VSplitLeft(24.0f, &CopyRect, &BottomArea);
	BottomArea.VSplitLeft(4.0f, nullptr, &BottomArea);
	BottomArea.VSplitLeft(24.0f, &ResetRect, &BottomArea);
	BottomArea.VSplitLeft(6.0f, nullptr, &BottomArea);
	HexRect = BottomArea;

	const bool UseAlphaHex = pColorPicker->m_Alpha;
	const unsigned PackedHex = PickerColorRGB.PackAlphaLast(UseAlphaHex);
	char aHex[16];
	if(UseAlphaHex)
		str_format(aHex, sizeof(aHex), "#%08X", PackedHex);
	else
		str_format(aHex, sizeof(aHex), "#%06X", PackedHex);

	if(pUI->DoButton_FontIcon(&pColorPicker->m_CopyButton, FontIcon::COPY, 0, &CopyRect, BUTTONFLAG_LEFT))
		pUI->Input()->SetClipboardText(aHex);

	if(pColorPicker->m_HasDefault && pUI->DoButton_FontIcon(&pColorPicker->m_ResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &ResetRect, BUTTONFLAG_LEFT))
	{
		PickerColorHSL = color_cast<ColorHSLA>(pColorPicker->m_DefaultColor);
		PickerColorHSV = color_cast<ColorHSVA>(PickerColorHSL);
		PickerColorRGB = color_cast<ColorRGBA>(PickerColorHSL);
		pColorPicker->m_State = EEditState::END;
		if(UseAlphaHex)
			str_format(aHex, sizeof(aHex), "#%08X", PickerColorRGB.PackAlphaLast(true));
		else
			str_format(aHex, sizeof(aHex), "#%06X", PickerColorRGB.PackAlphaLast(false));
		pColorPicker->m_HexInput.Set(aHex);
	}

	const bool HexFocused = CLineInput::GetActiveInput() == &pColorPicker->m_HexInput;
	if(!HexFocused && str_comp(pColorPicker->m_HexInput.GetString(), aHex) != 0)
		pColorPicker->m_HexInput.Set(aHex);

	const ColorRGBA OldOutline = pUI->TextRender()->GetTextOutlineColor();
	pUI->TextRender()->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.65f));
	if(pUI->DoEditBox(&pColorPicker->m_HexInput, &HexRect, 12.0f))
	{
		const char *pHexStr = pColorPicker->m_HexInput.GetString();
		if(pHexStr[0] == '#')
			++pHexStr;
		if(const auto Parsed = color_parse<ColorRGBA>(pHexStr))
		{
			PickerColorRGB = *Parsed;
			if(!UseAlphaHex)
				PickerColorRGB.a = 1.0f;
			PickerColorHSL = color_cast<ColorHSLA>(PickerColorRGB);
			PickerColorHSV = color_cast<ColorHSVA>(PickerColorHSL);
			pColorPicker->m_State = EEditState::EDITING;
		}
	}
	pUI->TextRender()->TextOutlineColor(OldOutline);

	pColorPicker->m_HsvaColor = PickerColorHSV;
	pColorPicker->m_RgbaColor = PickerColorRGB;
	pColorPicker->m_HslaColor = PickerColorHSL;
	if(pColorPicker->m_pHslaColor != nullptr)
		*pColorPicker->m_pHslaColor = PickerColorHSL.Pack(pColorPicker->m_Alpha);

	return CUi::POPUP_KEEP_OPEN;
}
// bestclient

CUi::EPopupMenuFunctionResult CUi::PopupColorPicker(void *pContext, CUIRect View, bool Active)
{
	SColorPickerPopupContext *pColorPicker = static_cast<SColorPickerPopupContext *>(pContext);
	// bestclient
	if(BestClientUiTheme::IsNewColorPicker())
		return PopupColorPickerNewStyle(pColorPicker, View, Active);
	// bestclient
	CUi *pUI = pColorPicker->m_pUI;
	pColorPicker->m_State = EEditState::NONE;

	CUIRect ColorsArea, HueArea, BottomArea, ModeButtonArea, HueRect, SatRect, ValueRect, HexRect, AlphaRect;

	View.HSplitTop(140.0f, &ColorsArea, &BottomArea);
	ColorsArea.VSplitRight(20.0f, &ColorsArea, &HueArea);

	BottomArea.HSplitTop(3.0f, nullptr, &BottomArea);
	HueArea.VSplitLeft(3.0f, nullptr, &HueArea);

	BottomArea.HSplitTop(20.0f, &HueRect, &BottomArea);
	BottomArea.HSplitTop(3.0f, nullptr, &BottomArea);

	constexpr float ValuePadding = 5.0f;
	const float HsvValueWidth = (HueRect.w - ValuePadding * 2) / 3.0f;
	const float HexValueWidth = HsvValueWidth * 2 + ValuePadding;

	HueRect.VSplitLeft(HsvValueWidth, &HueRect, &SatRect);
	SatRect.VSplitLeft(ValuePadding, nullptr, &SatRect);
	SatRect.VSplitLeft(HsvValueWidth, &SatRect, &ValueRect);
	ValueRect.VSplitLeft(ValuePadding, nullptr, &ValueRect);

	BottomArea.HSplitTop(20.0f, &HexRect, &BottomArea);
	BottomArea.HSplitTop(3.0f, nullptr, &BottomArea);
	HexRect.VSplitLeft(HexValueWidth, &HexRect, &AlphaRect);
	AlphaRect.VSplitLeft(ValuePadding, nullptr, &AlphaRect);
	BottomArea.HSplitTop(20.0f, &ModeButtonArea, &BottomArea);

	const ColorRGBA BlackColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f);

	HueArea.Draw(BlackColor, IGraphics::CORNER_NONE, 0.0f);
	HueArea.Margin(1.0f, &HueArea);

	ColorsArea.Draw(BlackColor, IGraphics::CORNER_NONE, 0.0f);
	ColorsArea.Margin(1.0f, &ColorsArea);

	ColorHSVA PickerColorHSV = pColorPicker->m_HsvaColor;
	ColorRGBA PickerColorRGB = pColorPicker->m_RgbaColor;
	ColorHSLA PickerColorHSL = pColorPicker->m_HslaColor;

	// Color Area
	ColorRGBA TL, TR, BL, BR;
	TL = BL = color_cast<ColorRGBA>(ColorHSVA(PickerColorHSV.x, 0.0f, 1.0f));
	TR = BR = color_cast<ColorRGBA>(ColorHSVA(PickerColorHSV.x, 1.0f, 1.0f));
	ColorsArea.Draw4(TL, TR, BL, BR, IGraphics::CORNER_NONE, 0.0f);

	TL = TR = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
	BL = BR = ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f);
	ColorsArea.Draw4(TL, TR, BL, BR, IGraphics::CORNER_NONE, 0.0f);

	// Hue Area
	static const float s_aaColorIndices[7][3] = {
		{1.0f, 0.0f, 0.0f}, // red
		{1.0f, 0.0f, 1.0f}, // magenta
		{0.0f, 0.0f, 1.0f}, // blue
		{0.0f, 1.0f, 1.0f}, // cyan
		{0.0f, 1.0f, 0.0f}, // green
		{1.0f, 1.0f, 0.0f}, // yellow
		{1.0f, 0.0f, 0.0f}, // red
	};

	const float HuePickerOffset = HueArea.h / 6.0f;
	CUIRect HuePartialArea = HueArea;
	HuePartialArea.h = HuePickerOffset;

	for(size_t j = 0; j < std::size(s_aaColorIndices) - 1; j++)
	{
		TL = ColorRGBA(s_aaColorIndices[j][0], s_aaColorIndices[j][1], s_aaColorIndices[j][2], 1.0f);
		BL = ColorRGBA(s_aaColorIndices[j + 1][0], s_aaColorIndices[j + 1][1], s_aaColorIndices[j + 1][2], 1.0f);

		HuePartialArea.y = HueArea.y + HuePickerOffset * j;
		HuePartialArea.Draw4(TL, TL, BL, BL, IGraphics::CORNER_NONE, 0.0f);
	}

	const auto &&RenderAlphaSelector = [&](unsigned OldA) -> SEditResult<int64_t> {
		if(pColorPicker->m_Alpha)
		{
			return pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[3], &AlphaRect, "A:", OldA, 0, 255);
		}
		else
		{
			char aBuf[8];
			str_format(aBuf, sizeof(aBuf), "A: %d", OldA);
			pUI->DoLabel(&AlphaRect, aBuf, 10.0f, TEXTALIGN_MC);
			AlphaRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.65f), IGraphics::CORNER_ALL, 3.0f);
			return {EEditState::NONE, OldA};
		}
	};

	// Editboxes Area
	if(pColorPicker->m_ColorMode == SColorPickerPopupContext::MODE_HSVA)
	{
		const unsigned OldH = round_to_int(PickerColorHSV.h * 255.0f);
		const unsigned OldS = round_to_int(PickerColorHSV.s * 255.0f);
		const unsigned OldV = round_to_int(PickerColorHSV.v * 255.0f);
		const unsigned OldA = round_to_int(PickerColorHSV.a * 255.0f);

		const auto [StateH, H] = pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[0], &HueRect, "H:", OldH, 0, 255);
		const auto [StateS, S] = pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[1], &SatRect, "S:", OldS, 0, 255);
		const auto [StateV, V] = pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[2], &ValueRect, "V:", OldV, 0, 255);
		const auto [StateA, A] = RenderAlphaSelector(OldA);

		if(OldH != H || OldS != S || OldV != V || OldA != A)
		{
			PickerColorHSV = ColorHSVA(H / 255.0f, S / 255.0f, V / 255.0f, A / 255.0f);
			PickerColorHSL = color_cast<ColorHSLA>(PickerColorHSV);
			PickerColorRGB = color_cast<ColorRGBA>(PickerColorHSL);
		}

		for(auto State : {StateH, StateS, StateV, StateA})
		{
			if(State != EEditState::NONE)
			{
				pColorPicker->m_State = State;
				break;
			}
		}
	}
	else if(pColorPicker->m_ColorMode == SColorPickerPopupContext::MODE_RGBA)
	{
		const unsigned OldR = round_to_int(PickerColorRGB.r * 255.0f);
		const unsigned OldG = round_to_int(PickerColorRGB.g * 255.0f);
		const unsigned OldB = round_to_int(PickerColorRGB.b * 255.0f);
		const unsigned OldA = round_to_int(PickerColorRGB.a * 255.0f);

		const auto [StateR, R] = pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[0], &HueRect, "R:", OldR, 0, 255);
		const auto [StateG, G] = pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[1], &SatRect, "G:", OldG, 0, 255);
		const auto [StateB, B] = pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[2], &ValueRect, "B:", OldB, 0, 255);
		const auto [StateA, A] = RenderAlphaSelector(OldA);

		if(OldR != R || OldG != G || OldB != B || OldA != A)
		{
			PickerColorRGB = ColorRGBA(R / 255.0f, G / 255.0f, B / 255.0f, A / 255.0f);
			PickerColorHSL = color_cast<ColorHSLA>(PickerColorRGB);
			PickerColorHSV = color_cast<ColorHSVA>(PickerColorHSL);
		}

		for(auto State : {StateR, StateG, StateB, StateA})
		{
			if(State != EEditState::NONE)
			{
				pColorPicker->m_State = State;
				break;
			}
		}
	}
	else if(pColorPicker->m_ColorMode == SColorPickerPopupContext::MODE_HSLA)
	{
		const unsigned OldH = round_to_int(PickerColorHSL.h * 255.0f);
		const unsigned OldS = round_to_int(PickerColorHSL.s * 255.0f);
		const unsigned OldL = round_to_int(PickerColorHSL.l * 255.0f);
		const unsigned OldA = round_to_int(PickerColorHSL.a * 255.0f);

		const auto [StateH, H] = pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[0], &HueRect, "H:", OldH, 0, 255);
		const auto [StateS, S] = pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[1], &SatRect, "S:", OldS, 0, 255);
		const auto [StateL, L] = pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[2], &ValueRect, "L:", OldL, 0, 255);
		const auto [StateA, A] = RenderAlphaSelector(OldA);

		if(OldH != H || OldS != S || OldL != L || OldA != A)
		{
			PickerColorHSL = ColorHSLA(H / 255.0f, S / 255.0f, L / 255.0f, A / 255.0f);
			PickerColorHSV = color_cast<ColorHSVA>(PickerColorHSL);
			PickerColorRGB = color_cast<ColorRGBA>(PickerColorHSL);
		}

		for(auto State : {StateH, StateS, StateL, StateA})
		{
			if(State != EEditState::NONE)
			{
				pColorPicker->m_State = State;
				break;
			}
		}
	}
	else
	{
		dbg_assert_failed("Color picker mode invalid: %d", (int)pColorPicker->m_ColorMode);
	}

	SValueSelectorProperties Props;
	Props.m_UseScroll = false;
	Props.m_IsHex = true;
	Props.m_HexPrefix = pColorPicker->m_Alpha ? 8 : 6;
	const unsigned OldHex = PickerColorRGB.PackAlphaLast(pColorPicker->m_Alpha);
	auto [HexState, Hex] = pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[4], &HexRect, "Hex:", OldHex, 0, pColorPicker->m_Alpha ? 0xFFFFFFFFll : 0xFFFFFFll, Props);
	if(OldHex != Hex)
	{
		const float OldAlpha = PickerColorRGB.a;
		PickerColorRGB = ColorRGBA::UnpackAlphaLast<ColorRGBA>(Hex, pColorPicker->m_Alpha);
		if(!pColorPicker->m_Alpha)
			PickerColorRGB.a = OldAlpha;
		PickerColorHSL = color_cast<ColorHSLA>(PickerColorRGB);
		PickerColorHSV = color_cast<ColorHSVA>(PickerColorHSL);
	}

	if(HexState != EEditState::NONE)
		pColorPicker->m_State = HexState;

	// Logic
	float PickerX, PickerY;
	EEditState ColorPickerRes = pUI->DoPickerLogic(&pColorPicker->m_ColorPickerId, &ColorsArea, &PickerX, &PickerY);
	if(ColorPickerRes != EEditState::NONE)
	{
		PickerColorHSV.y = PickerX / ColorsArea.w;
		PickerColorHSV.z = 1.0f - PickerY / ColorsArea.h;
		PickerColorHSL = color_cast<ColorHSLA>(PickerColorHSV);
		PickerColorRGB = color_cast<ColorRGBA>(PickerColorHSL);
		pColorPicker->m_State = ColorPickerRes;
	}

	EEditState HuePickerRes = pUI->DoPickerLogic(&pColorPicker->m_HuePickerId, &HueArea, &PickerX, &PickerY);
	if(HuePickerRes != EEditState::NONE)
	{
		PickerColorHSV.x = 1.0f - PickerY / HueArea.h;
		PickerColorHSL = color_cast<ColorHSLA>(PickerColorHSV);
		PickerColorRGB = color_cast<ColorRGBA>(PickerColorHSL);
		pColorPicker->m_State = HuePickerRes;
	}

	// Marker Color Area
	const float MarkerX = ColorsArea.x + ColorsArea.w * PickerColorHSV.y;
	const float MarkerY = ColorsArea.y + ColorsArea.h * (1.0f - PickerColorHSV.z);

	const float MarkerOutlineInd = PickerColorHSV.z > 0.5f ? 0.0f : 1.0f;
	const ColorRGBA MarkerOutline = ColorRGBA(MarkerOutlineInd, MarkerOutlineInd, MarkerOutlineInd, 1.0f);

	pUI->Graphics()->TextureClear();
	pUI->Graphics()->QuadsBegin();
	pUI->Graphics()->SetColor(MarkerOutline);
	pUI->Graphics()->DrawCircle(MarkerX, MarkerY, 4.5f, 32);
	pUI->Graphics()->SetColor(PickerColorRGB);
	pUI->Graphics()->DrawCircle(MarkerX, MarkerY, 3.5f, 32);
	pUI->Graphics()->QuadsEnd();

	// Marker Hue Area
	CUIRect HueMarker;
	HueArea.Margin(-2.5f, &HueMarker);
	HueMarker.h = 6.5f;
	HueMarker.y = (HueArea.y + HueArea.h * (1.0f - PickerColorHSV.x)) - HueMarker.h / 2.0f;

	const ColorRGBA HueMarkerColor = color_cast<ColorRGBA>(ColorHSVA(PickerColorHSV.x, 1.0f, 1.0f, 1.0f));
	const float HueMarkerOutlineColor = PickerColorHSV.x > 0.75f ? 1.0f : 0.0f;
	const ColorRGBA HueMarkerOutline = ColorRGBA(HueMarkerOutlineColor, HueMarkerOutlineColor, HueMarkerOutlineColor, 1.0f);

	HueMarker.Draw(HueMarkerOutline, IGraphics::CORNER_ALL, 1.2f);
	HueMarker.Margin(1.2f, &HueMarker);
	HueMarker.Draw(HueMarkerColor, IGraphics::CORNER_ALL, 1.2f);

	pColorPicker->m_HsvaColor = PickerColorHSV;
	pColorPicker->m_RgbaColor = PickerColorRGB;
	pColorPicker->m_HslaColor = PickerColorHSL;
	if(pColorPicker->m_pHslaColor != nullptr)
		*pColorPicker->m_pHslaColor = PickerColorHSL.Pack(pColorPicker->m_Alpha);

	static constexpr SColorPickerPopupContext::EColorPickerMode PICKER_MODES[] = {SColorPickerPopupContext::MODE_HSVA, SColorPickerPopupContext::MODE_RGBA, SColorPickerPopupContext::MODE_HSLA};
	static constexpr const char *PICKER_MODE_LABELS[] = {"HSVA", "RGBA", "HSLA"};
	static_assert(std::size(PICKER_MODES) == std::size(PICKER_MODE_LABELS));
	for(SColorPickerPopupContext::EColorPickerMode Mode : PICKER_MODES)
	{
		CUIRect ModeButton;
		ModeButtonArea.VSplitLeft(HsvValueWidth, &ModeButton, &ModeButtonArea);
		ModeButtonArea.VSplitLeft(ValuePadding, nullptr, &ModeButtonArea);
		if(pUI->DoButton_PopupMenu(&pColorPicker->m_aModeButtons[(int)Mode], PICKER_MODE_LABELS[Mode], &ModeButton, 10.0f, TEXTALIGN_MC, 2.0f, false, pColorPicker->m_ColorMode != Mode))
		{
			pColorPicker->m_ColorMode = Mode;
		}
	}

	return CUi::POPUP_KEEP_OPEN;
}

void CUi::ShowPopupColorPicker(float X, float Y, SColorPickerPopupContext *pContext)
{
	pContext->m_pUI = this;
	if(pContext->m_ColorMode == SColorPickerPopupContext::MODE_UNSET)
		pContext->m_ColorMode = SColorPickerPopupContext::MODE_HSVA;
	// bestclient
	if(BestClientUiTheme::IsNewColorPicker())
	{
		char aHex[16];
		const unsigned PackedHex = pContext->m_RgbaColor.PackAlphaLast(pContext->m_Alpha);
		if(pContext->m_Alpha)
			str_format(aHex, sizeof(aHex), "#%08X", PackedHex);
		else
			str_format(aHex, sizeof(aHex), "#%06X", PackedHex);
		pContext->m_HexInput.Set(aHex);
		const float PopupH = pContext->m_Alpha ? 236.0f : 200.0f;
		DoPopupMenu(pContext, X, Y, 170.0f + 10.0f, PopupH + 10.0f, pContext, PopupColorPicker);
	}
	else
		// bestclient
		DoPopupMenu(pContext, X, Y, 160.0f + 10.0f, 209.0f + 10.0f, pContext, PopupColorPicker);
}
