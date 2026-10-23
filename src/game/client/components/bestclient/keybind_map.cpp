/* Copyright © 2026 BestProject Team */
#include "keybind_map_ui.h"

#include "ui_theme/style.h"

#include <base/color.h>
#include <base/str.h>

#include <engine/console.h>
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/binds.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>

static void ClearCompletion(CKeybindMapUi &Ui)
{
	Ui.m_vCompletions.clear();
	Ui.m_aCompletionBase[0] = '\0';
	Ui.m_CompletionStart = 0;
	Ui.m_CompletionEnd = 0;
	Ui.m_CompletionIndex = -1;
}

static bool CommitSelectedBind(CBinds &Binds, CKeybindMapUi &Ui, EKeybindMapCommit Commit, const char *pCommand)
{
	const int Key = Ui.m_SelectedKey;
	if(Key <= KEY_UNKNOWN || Key >= KEY_LAST)
		return false;

	CKeybindMapBinding Binding;
	Binding.m_Key = Key;
	str_copy(Binding.m_aCommand, Binds.Get(Key, KeyModifier::NONE), sizeof(Binding.m_aCommand));
	if(!KeybindMapCommit(&Binding, 1, Key, Commit, pCommand))
		return false;

	Binds.Bind(Key, Binding.m_aCommand, false, KeyModifier::NONE);
	Ui.m_Command.Set(Binding.m_aCommand);
	Ui.m_Command.WasChanged();
	Ui.m_Command.WasCursorChanged();
	ClearCompletion(Ui);
	return true;
}

static void WordAtCursor(const char *pLine, int Cursor, int *pStart, int *pEnd)
{
	const int Len = pLine == nullptr ? 0 : str_length(pLine);
	if(Cursor < 0)
		Cursor = 0;
	if(Cursor > Len)
		Cursor = Len;
	int Start = Cursor;
	while(Start > 0)
	{
		const char Prev = pLine[Start - 1];
		if(Prev == ' ' || Prev == '\t' || Prev == ';')
			break;
		--Start;
	}
	int End = Cursor;
	while(pLine != nullptr && pLine[End] != '\0' && pLine[End] != ' ' && pLine[End] != '\t' && pLine[End] != ';')
		++End;
	*pStart = Start;
	*pEnd = End;
}

static void CopyRange(char *pDest, int DestSize, const char *pSrc, int Start, int End)
{
	if(DestSize <= 0)
		return;
	if(pSrc == nullptr || Start < 0 || End < Start)
	{
		pDest[0] = '\0';
		return;
	}
	str_copy(pDest, pSrc + Start, std::min(DestSize, End - Start + 1));
}

static void CollectCompletion(int Index, const char *pStr, void *pUser)
{
	(void)Index;
	auto *pCompletions = static_cast<std::vector<std::string> *>(pUser);
	if(pStr != nullptr)
		pCompletions->emplace_back(pStr);
}

static void SortCompletions(std::vector<std::string> &vCompletions, const char *pSearch)
{
	if(pSearch == nullptr || pSearch[0] == '\0')
	{
		std::sort(vCompletions.begin(), vCompletions.end());
		return;
	}

	std::sort(vCompletions.begin(), vCompletions.end(), [pSearch](const std::string &A, const std::string &B) {
		const char *pMatchA = str_find_nocase(A.c_str(), pSearch);
		const char *pMatchB = str_find_nocase(B.c_str(), pSearch);
		const int MatchPosA = pMatchA != nullptr ? (int)(pMatchA - A.c_str()) : -1;
		const int MatchPosB = pMatchB != nullptr ? (int)(pMatchB - B.c_str()) : -1;
		if(MatchPosA != MatchPosB)
			return MatchPosA < MatchPosB;
		if(A.size() != B.size())
			return A.size() < B.size();
		return str_comp_nocase(A.c_str(), B.c_str()) < 0;
	});
}

static void FillCompletions(CGameClient *pClient, CKeybindMapUi &Ui, const char *pPrefix)
{
	Ui.m_vCompletions.clear();
	if(pClient == nullptr || pClient->Console() == nullptr)
		return;
	pClient->Console()->PossibleCommands(pPrefix != nullptr ? pPrefix : "", CFGFLAG_CLIENT, false, CollectCompletion, &Ui.m_vCompletions);
	SortCompletions(Ui.m_vCompletions, pPrefix);
}

static void SnapshotCompletion(CKeybindMapUi &Ui)
{
	str_copy(Ui.m_aCompletionBase, Ui.m_Command.GetString(), sizeof(Ui.m_aCompletionBase));
	WordAtCursor(Ui.m_aCompletionBase, (int)Ui.m_Command.GetCursorOffset(), &Ui.m_CompletionStart, &Ui.m_CompletionEnd);
}

static void WriteCompletion(CKeybindMapUi &Ui)
{
	if(Ui.m_CompletionIndex < 0 || Ui.m_CompletionIndex >= (int)Ui.m_vCompletions.size())
		return;

	const char *pSuggestion = Ui.m_vCompletions[Ui.m_CompletionIndex].c_str();
	char aBefore[512];
	str_truncate(aBefore, sizeof(aBefore), Ui.m_aCompletionBase, Ui.m_CompletionStart);
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "%s%s%s", aBefore, pSuggestion, Ui.m_aCompletionBase + Ui.m_CompletionEnd);
	Ui.m_Command.Set(aBuf);
	Ui.m_Command.SetCursorOffset((size_t)Ui.m_CompletionStart + (size_t)str_length(pSuggestion));
	Ui.m_Command.WasChanged();
	Ui.m_Command.WasCursorChanged();
}

static void CycleCompletion(CGameClient *pClient, CKeybindMapUi &Ui, int Direction)
{
	if(Ui.m_CompletionIndex < 0)
	{
		SnapshotCompletion(Ui);
		char aPrefix[128];
		CopyRange(aPrefix, sizeof(aPrefix), Ui.m_aCompletionBase, Ui.m_CompletionStart, Ui.m_CompletionEnd);
		FillCompletions(pClient, Ui, aPrefix);
		if(Ui.m_vCompletions.empty())
			return;
		Ui.m_CompletionIndex = Direction < 0 ? (int)Ui.m_vCompletions.size() - 1 : 0;
	}
	else
	{
		const int Count = (int)Ui.m_vCompletions.size();
		if(Count <= 0)
		{
			Ui.m_CompletionIndex = -1;
			return;
		}
		Ui.m_CompletionIndex = (Ui.m_CompletionIndex + Direction + Count) % Count;
	}
	WriteCompletion(Ui);
}

static void RefreshLiveCompletions(CGameClient *pClient, CKeybindMapUi &Ui)
{
	int Start, End;
	WordAtCursor(Ui.m_Command.GetString(), (int)Ui.m_Command.GetCursorOffset(), &Start, &End);
	char aPrefix[128];
	CopyRange(aPrefix, sizeof(aPrefix), Ui.m_Command.GetString(), Start, End);
	if(aPrefix[0] == '\0')
	{
		Ui.m_vCompletions.clear();
		Ui.m_aCompletionBase[0] = '\0';
		return;
	}
	if(str_comp(Ui.m_aCompletionBase, aPrefix) == 0 && !Ui.m_vCompletions.empty())
		return;
	str_copy(Ui.m_aCompletionBase, aPrefix, sizeof(Ui.m_aCompletionBase));
	FillCompletions(pClient, Ui, aPrefix);
}

static CButtonContainer s_aCompletionRows[6];

static bool CompletionRowHeld(CUi *pUi)
{
	const void *pActive = pUi->ActiveItem();
	const void *pHot = pUi->HotItem();
	for(CButtonContainer &Row : s_aCompletionRows)
	{
		if(pActive == &Row || pHot == &Row)
			return true;
	}
	return false;
}

static int DrawCompletionPopup(CUi *pUi, CKeybindMapUi &Ui, const CUIRect &Edit, const CUIRect &View)
{
	const int Count = (int)Ui.m_vCompletions.size();
	if(Count <= 0)
		return -1;

	const int Window = std::min(6, Count);
	int First = 0;
	if(Ui.m_CompletionIndex >= 0)
	{
		First = Ui.m_CompletionIndex - 2;
		if(First > Count - Window)
			First = Count - Window;
		if(First < 0)
			First = 0;
	}

	const float RowH = 16.0f;
	CUIRect Popup;
	Popup.x = Edit.x;
	Popup.w = std::max(160.0f, Edit.w);
	Popup.h = RowH * (float)Window + 6.0f;
	Popup.y = Edit.y + Edit.h + 2.0f;
	if(Popup.y + Popup.h > View.y + View.h)
		Popup.y = Edit.y - Popup.h - 2.0f;
	if(Popup.x + Popup.w > View.x + View.w)
		Popup.x = View.x + View.w - Popup.w;
	if(Popup.x < View.x)
		Popup.x = View.x;

	Popup.Draw(ColorRGBA(0.08f, 0.09f, 0.08f, 0.94f), IGraphics::CORNER_ALL, 5.0f);
	CUIRect List;
	Popup.Margin(3.0f, &List);

	int Picked = -1;
	for(int Row = 0; Row < Window; Row++)
	{
		CUIRect RowRect;
		List.HSplitTop(RowH, &RowRect, &List);
		const int Index = First + Row;
		const bool Hot = pUi->HotItem() == &s_aCompletionRows[Row];
		if(Index == Ui.m_CompletionIndex || Hot)
			RowRect.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, Hot ? 0.22f : 0.12f), IGraphics::CORNER_ALL, 3.0f);
		SLabelProperties Props;
		Props.m_MaxWidth = std::max(1.0f, RowRect.w - 6.0f);
		Props.m_EllipsisAtEnd = true;
		Props.m_MinimumFontSize = 8.0f;
		CUIRect Label = RowRect;
		Label.VMargin(4.0f, &Label);
		pUi->DoLabel(&Label, Ui.m_vCompletions[Index].c_str(), 11.0f, TEXTALIGN_ML, Props);
		if(pUi->DoButtonLogic(&s_aCompletionRows[Row], 0, &RowRect, BUTTONFLAG_LEFT))
			Picked = Index;
	}
	return Picked;
}

void RenderBestClientKeybindMap(CGameClient *pClient, CUi *pUi, CUIRect View, CKeybindMapUi &Ui, const CKeybindMapActionRef *pActions, int NumActions, const char *pFilter)
{
	if(pClient == nullptr || pUi == nullptr)
		return;

	CMenus &Menus = pClient->m_Menus;
	CBinds &Binds = pClient->m_Binds;
	if(g_Config.m_BcKeybindMapFormat < (int)EKeybindMapFormat::FULL || g_Config.m_BcKeybindMapFormat > (int)EKeybindMapFormat::COMPACT)
		g_Config.m_BcKeybindMapFormat = (int)EKeybindMapFormat::COMPACT;
	Ui.m_Format = g_Config.m_BcKeybindMapFormat;
	const EKeybindMapFormat Format = (EKeybindMapFormat)Ui.m_Format;
	if(Ui.m_SelectedKey != KEY_UNKNOWN && !KeybindMapKeyVisible(Format, Ui.m_SelectedKey))
	{
		Ui.m_SelectedKey = KEY_UNKNOWN;
		Ui.m_Command.Clear();
		Ui.m_Command.WasChanged();
		Ui.m_Command.WasCursorChanged();
		ClearCompletion(Ui);
	}

	float DetailHeight = View.h * 0.46f;
	if(DetailHeight < 168.0f)
		DetailHeight = 168.0f;
	if(DetailHeight > View.h * 0.62f)
		DetailHeight = View.h * 0.62f;
	if(DetailHeight > View.h - 64.0f)
		DetailHeight = std::max(64.0f, View.h - 64.0f);

	CUIRect KeyboardArea, Detail;
	View.HSplitBottom(DetailHeight, &KeyboardArea, &Detail);
	KeyboardArea.HSplitBottom(6.0f, &KeyboardArea, nullptr);
	CUIRect ScaleRow, KeyboardWell;
	KeyboardArea.HSplitTop(22.0f, &ScaleRow, &KeyboardWell);
	KeyboardWell.HSplitBottom(10.0f, &KeyboardWell, nullptr);
	BestClientUiTheme::DrawUiBlock(&KeyboardWell);
	BestClientUiTheme::DrawUiBlock(&Detail);

	CUIRect Board;
	KeyboardWell.Margin(8.0f, &Board);

	float MinX = 0.0f;
	float MinY = 0.0f;
	float MaxX = 1.0f;
	float MaxY = 1.0f;
	bool AnyKey = false;
	for(const CKeybindMapKey &KeyDef : KEYBIND_MAP_KEYS)
	{
		if(!KeybindMapZoneVisible(Format, KeyDef.m_Zone))
			continue;
		if(!AnyKey)
		{
			MinX = KeyDef.m_X;
			MinY = KeyDef.m_Y;
			MaxX = KeyDef.m_X + KeyDef.m_W;
			MaxY = KeyDef.m_Y + KeyDef.m_H;
			AnyKey = true;
			continue;
		}
		if(KeyDef.m_X < MinX)
			MinX = KeyDef.m_X;
		if(KeyDef.m_Y < MinY)
			MinY = KeyDef.m_Y;
		if(KeyDef.m_X + KeyDef.m_W > MaxX)
			MaxX = KeyDef.m_X + KeyDef.m_W;
		if(KeyDef.m_Y + KeyDef.m_H > MaxY)
			MaxY = KeyDef.m_Y + KeyDef.m_H;
	}

	const float BoardW = MaxX - MinX;
	const float BoardH = MaxY - MinY;
	const float Unit = BoardW > 0.0f && BoardH > 0.0f ? std::min(Board.w / BoardW, Board.h / BoardH) : 0.0f;
	const float OriginX = Board.x + (Board.w - BoardW * Unit) * 0.5f;
	const float OriginY = Board.y + (Board.h - BoardH * Unit) * 0.5f;
	const float Inset = Unit * 0.07f;

	int HoverKey = KEY_UNKNOWN;
	CUIRect HoverRect;
	for(const CKeybindMapKey &KeyDef : KEYBIND_MAP_KEYS)
	{
		if(!KeybindMapZoneVisible(Format, KeyDef.m_Zone))
			continue;

		CUIRect KeyRect;
		KeyRect.x = OriginX + (KeyDef.m_X - MinX) * Unit + Inset;
		KeyRect.y = OriginY + (KeyDef.m_Y - MinY) * Unit + Inset;
		KeyRect.w = KeyDef.m_W * Unit - Inset * 2.0f;
		KeyRect.h = KeyDef.m_H * Unit - Inset * 2.0f;
		if(KeyRect.w < 1.0f || KeyRect.h < 1.0f)
			continue;

		CButtonContainer *pButton = &Ui.m_aKeyButtons[KeyDef.m_Key];
		const int Click = pUi->DoButtonLogic(pButton, 0, &KeyRect, BUTTONFLAG_LEFT);
		const bool Bound = KeybindMapIsBound(Binds.Get(KeyDef.m_Key, KeyModifier::NONE));
		const bool Selected = Ui.m_SelectedKey == KeyDef.m_Key;
		float Alpha = Bound ? 0.78f : 0.28f;
		Alpha *= pUi->ButtonColorMul(pButton);
		KeyRect.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, Alpha), IGraphics::CORNER_ALL, 2.0f);
		if(Selected)
			KeyRect.DrawOutline(ColorRGBA(0.90f, 0.12f, 0.12f, 1.0f));

		const float FontSize = std::min(KeyRect.h * 0.42f, 13.0f);
		SLabelProperties LabelProps;
		LabelProps.m_MaxWidth = std::max(1.0f, KeyRect.w - 2.0f);
		LabelProps.m_MinimumFontSize = 6.0f;
		LabelProps.m_EllipsisAtEnd = true;
		pUi->DoLabel(&KeyRect, KeyDef.m_pLabel, FontSize, TEXTALIGN_MC, LabelProps);

		if(pUi->NextHotItem() == pButton)
		{
			HoverKey = KeyDef.m_Key;
			HoverRect = KeyRect;
		}
		if(Click)
		{
			Ui.m_SelectedKey = KeyDef.m_Key;
			Ui.m_Command.Set(Binds.Get(KeyDef.m_Key, KeyModifier::NONE));
			Ui.m_Command.WasChanged();
			Ui.m_Command.WasCursorChanged();
			ClearCompletion(Ui);
		}
	}

	const float ScaleH = 15.0f;
	const float ScaleW = 36.0f;
	CUIRect ScaleButtons;
	ScaleButtons.w = ScaleW * 3.0f + 6.0f;
	ScaleButtons.h = ScaleH;
	ScaleButtons.x = ScaleRow.x + ScaleRow.w - ScaleButtons.w;
	ScaleButtons.y = ScaleRow.y + (ScaleRow.h - ScaleH) * 0.5f;
	static const char *s_apScaleLabels[(int)EKeybindMapFormat::COMPACT + 1] = {"100%", "86%", "60%"};
	for(int Scale = 0; Scale < 3; Scale++)
	{
		CUIRect Button;
		ScaleButtons.VSplitLeft(ScaleW, &Button, &ScaleButtons);
		if(Scale < 2)
			ScaleButtons.VSplitLeft(3.0f, nullptr, &ScaleButtons);
		if(Menus.DoButton_Menu(&Ui.m_aScaleButtons[Scale], s_apScaleLabels[Scale], Ui.m_Format == Scale ? 1 : 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f))
		{
			Ui.m_Format = Scale;
			g_Config.m_BcKeybindMapFormat = Scale;
		}
	}

	CUIRect DetailInner;
	Detail.Margin(8.0f, &DetailInner);

	const bool HasSelection = Ui.m_SelectedKey > KEY_UNKNOWN && Ui.m_SelectedKey < KEY_LAST;
	char aCurrentCommand[sizeof(CKeybindMapBinding::m_aCommand)];
	aCurrentCommand[0] = '\0';
	if(HasSelection)
		str_copy(aCurrentCommand, Binds.Get(Ui.m_SelectedKey, KeyModifier::NONE), sizeof(aCurrentCommand));

	CUIRect BindRow;
	DetailInner.HSplitTop(16.0f, &BindRow, &DetailInner);
	DetailInner.HSplitTop(4.0f, nullptr, &DetailInner);
	char aBindLine[512];
	if(!HasSelection)
		str_copy(aBindLine, BcLocalize("Select a key"), sizeof(aBindLine));
	else
		str_format(aBindLine, sizeof(aBindLine), "%s: %s", KeybindMapKeyLabel(Ui.m_SelectedKey), KeybindMapIsBound(aCurrentCommand) ? aCurrentCommand : BcLocalize("Not bound"));
	SLabelProperties BindProps;
	BindProps.m_MaxWidth = std::max(1.0f, BindRow.w);
	BindProps.m_EllipsisAtEnd = true;
	BindProps.m_MinimumFontSize = 9.0f;
	pUi->DoLabel(&BindRow, aBindLine, 13.0f, TEXTALIGN_ML, BindProps);

	CUIRect CommandRow, ResetButton, DeleteButton, SaveButton;
	DetailInner.HSplitTop(20.0f, &CommandRow, &DetailInner);
	DetailInner.HSplitTop(4.0f, nullptr, &DetailInner);
	CommandRow.VSplitRight(20.0f, &CommandRow, &DeleteButton);
	CommandRow.VSplitRight(4.0f, &CommandRow, nullptr);
	CommandRow.VSplitRight(20.0f, &CommandRow, &ResetButton);
	CommandRow.VSplitRight(4.0f, &CommandRow, nullptr);
	CommandRow.VSplitRight(20.0f, &CommandRow, &SaveButton);
	CommandRow.VSplitRight(4.0f, &CommandRow, nullptr);

	const bool Edited = pUi->DoEditBox(&Ui.m_Command, &CommandRow, 12.0f);
	const bool CompletionHeld = CompletionRowHeld(pUi);
	if(Ui.m_Command.IsActive() && pUi->ConsumeHotkey(CUi::HOTKEY_TAB))
	{
		if(Edited)
		{
			Ui.m_CompletionIndex = -1;
			Ui.m_aCompletionBase[0] = '\0';
		}
		const int Direction = pClient->Input()->ShiftIsPressed() ? -1 : 1;
		CycleCompletion(pClient, Ui, Direction);
	}
	else if(Edited || (!Ui.m_Command.IsActive() && !CompletionHeld))
	{
		Ui.m_CompletionIndex = -1;
		Ui.m_aCompletionBase[0] = '\0';
		if(!Ui.m_Command.IsActive())
			Ui.m_vCompletions.clear();
	}
	if(Ui.m_Command.IsActive() && Ui.m_CompletionIndex < 0)
		RefreshLiveCompletions(pClient, Ui);

	const bool CanEditBind = HasSelection;
	if(pUi->DoButton_FontIcon(&Ui.m_ResetButton, FontIcon::UNDO, 0, &ResetButton, BUTTONFLAG_LEFT) && CanEditBind && CommitSelectedBind(Binds, Ui, EKeybindMapCommit::RESET, nullptr))
		str_copy(aCurrentCommand, Ui.m_Command.GetString(), sizeof(aCurrentCommand));
	if(pUi->DoButton_FontIcon(&Ui.m_DeleteButton, FontIcon::TRASH, 0, &DeleteButton, BUTTONFLAG_LEFT) && CanEditBind && CommitSelectedBind(Binds, Ui, EKeybindMapCommit::CLEAR, nullptr))
		str_copy(aCurrentCommand, Ui.m_Command.GetString(), sizeof(aCurrentCommand));
	if(pUi->DoButton_FontIcon(&Ui.m_SaveButton, FontIcon::FLOPPY_DISK, 0, &SaveButton, BUTTONFLAG_LEFT) && CanEditBind && CommitSelectedBind(Binds, Ui, EKeybindMapCommit::APPLY, Ui.m_Command.GetString()))
		str_copy(aCurrentCommand, Ui.m_Command.GetString(), sizeof(aCurrentCommand));
	pClient->m_Tooltips.DoToolTip(&Ui.m_ResetButton, &ResetButton, Localize("Reset"));
	pClient->m_Tooltips.DoToolTip(&Ui.m_DeleteButton, &DeleteButton, Localize("Delete"));
	pClient->m_Tooltips.DoToolTip(&Ui.m_SaveButton, &SaveButton, Localize("Save"));

	const bool CommandActive = Ui.m_Command.IsActive() || CompletionHeld;

	if((int)Ui.m_vActionButtons.size() < NumActions)
		Ui.m_vActionButtons.resize(NumActions);

	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 26.0f;
	Ui.m_ActionScroll.Begin(&DetailInner, &ScrollParams);

	const float Gap = 4.0f;
	const float BlockH = 22.0f;
	int Visible = 0;
	for(int i = 0; i < NumActions; i++)
	{
		if(pActions == nullptr || !KeybindMapActionVisible(pFilter, pActions[i].m_pLabel, pActions[i].m_pCommand))
			continue;
		Visible++;
	}

	if(Visible == 0)
	{
		CUIRect Empty;
		DetailInner.HSplitTop(BlockH, &Empty, &DetailInner);
		if(Ui.m_ActionScroll.AddRect(Empty))
			pUi->DoLabel(&Empty, Localize("No results"), 13.0f, TEXTALIGN_ML);
	}
	else
	{
		int Cols = (int)((DetailInner.w + Gap) / (148.0f + Gap));
		if(Cols < 1)
			Cols = 1;
		const float BlockW = (DetailInner.w - Gap * (float)(Cols - 1)) / (float)Cols;
		int Column = 0;
		CUIRect RowRect;
		bool RowOpen = false;
		for(int i = 0; i < NumActions; i++)
		{
			if(pActions == nullptr || !KeybindMapActionVisible(pFilter, pActions[i].m_pLabel, pActions[i].m_pCommand))
				continue;
			if(!RowOpen)
			{
				DetailInner.HSplitTop(BlockH, &RowRect, &DetailInner);
				DetailInner.HSplitTop(Gap, nullptr, &DetailInner);
				RowOpen = true;
				Column = 0;
			}

			CUIRect Block;
			RowRect.VSplitLeft(BlockW, &Block, &RowRect);
			if(Column + 1 < Cols)
				RowRect.VSplitLeft(Gap, nullptr, &RowRect);

			const bool Active = HasSelection && pActions[i].m_pCommand != nullptr && str_comp(aCurrentCommand, pActions[i].m_pCommand) == 0;
			if(Ui.m_ActionScroll.AddRect(Block))
			{
				if(Menus.DoButton_Menu(&Ui.m_vActionButtons[i], pActions[i].m_pLabel != nullptr ? pActions[i].m_pLabel : "", Active ? 1 : 0, &Block) &&
					CanEditBind && CommitSelectedBind(Binds, Ui, EKeybindMapCommit::APPLY, pActions[i].m_pCommand))
					str_copy(aCurrentCommand, Ui.m_Command.GetString(), sizeof(aCurrentCommand));
			}

			Column++;
			if(Column >= Cols)
				RowOpen = false;
		}
	}
	Ui.m_ActionScroll.End();

	if(CommandActive)
	{
		const int Picked = DrawCompletionPopup(pUi, Ui, CommandRow, View);
		if(Picked >= 0)
		{
			if(Ui.m_CompletionIndex < 0)
				SnapshotCompletion(Ui);
			Ui.m_CompletionIndex = Picked;
			WriteCompletion(Ui);
			Ui.m_Command.Activate(EInputPriority::UI);
			pUi->SetActiveItem(&Ui.m_Command);
		}
	}

	if(HoverKey != KEY_UNKNOWN && pUi->NextHotItem() == &Ui.m_aKeyButtons[HoverKey])
	{
		const char *pCommand = Binds.Get(HoverKey, KeyModifier::NONE);
		const char *pTitle = BcLocalize("Not bound");
		const char *pSubtitle = "";
		if(KeybindMapIsBound(pCommand))
		{
			pTitle = pCommand;
			for(int i = 0; i < NumActions; i++)
			{
				if(pActions == nullptr || pActions[i].m_pCommand == nullptr || str_comp(pCommand, pActions[i].m_pCommand) != 0)
					continue;
				if(pActions[i].m_pLabel != nullptr && pActions[i].m_pLabel[0] != '\0' && str_comp(pActions[i].m_pLabel, pActions[i].m_pCommand) != 0)
				{
					pTitle = pActions[i].m_pLabel;
					pSubtitle = pCommand;
				}
				break;
			}
		}
		Ui.m_HoveredKey = HoverKey;
		static char s_aHoverTip[512];
		if(!KeybindMapIsBound(pCommand))
			str_copy(s_aHoverTip, pTitle, sizeof(s_aHoverTip));
		else if(pSubtitle[0] != '\0')
			str_format(s_aHoverTip, sizeof(s_aHoverTip), "%s\n%s", pTitle, pSubtitle);
		else
			str_copy(s_aHoverTip, pTitle, sizeof(s_aHoverTip));
		const void *pTipId = &Ui.m_aKeyButtons[HoverKey];
		if(pUi->HotItem() == pTipId)
		{
			pClient->m_Tooltips.DoToolTip(pTipId, &HoverRect, s_aHoverTip, 260.0f);
			pClient->m_Tooltips.SetFadeTime(pTipId, 0.0f);
		}
	}
	else
		Ui.m_HoveredKey = KEY_UNKNOWN;
}
