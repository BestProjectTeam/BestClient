/* Copyright © 2026 BestProject Team */
#include "demo_tee_editor.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/demo.h>
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/protocol.h>

#include <game/client/animstate.h>
#include <game/client/components/menus.h>
#include <game/client/components/skins.h>
#include <game/client/demo_tee_selection.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>

#include <algorithm>
#include <vector>

const SDemoTeeOverride *CDemoTeeEditor::Override(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !m_aOverrides[ClientId].m_Active)
		return nullptr;
	return &m_aOverrides[ClientId];
}

void CDemoTeeEditor::Reset()
{
	m_aOverrides = {};
	m_SelectedClient = -1;
}

void CDemoTeeEditor::Close(CGameClient *pGameClient)
{
	pGameClient->Ui()->ClosePopupMenu(&m_EditorPopupId, true);
	m_SelectedClient = -1;
}

bool CDemoTeeEditor::EditorPopupOpen(CGameClient *pGameClient) const
{
	return pGameClient->Ui()->IsPopupOpen(&m_EditorPopupId);
}

bool CDemoTeeEditor::EditorPopupHovered(CGameClient *pGameClient) const
{
	return EditorPopupOpen(pGameClient) && pGameClient->Ui()->IsPopupHovered();
}

void CDemoTeeEditor::Open(CGameClient *pGameClient, int ClientId)
{
	if(pGameClient->Client()->State() != IClient::STATE_DEMOPLAYBACK || ClientId < 0 || ClientId >= MAX_CLIENTS ||
		!pGameClient->m_Snap.m_aCharacters[ClientId].m_Active || pGameClient->m_Snap.m_apPlayerInfos[ClientId] == nullptr)
		return;

	const CGameClient::CClientData &ClientData = pGameClient->m_aClients[ClientId];
	char aSnapName[MAX_NAME_LENGTH] = "";
	char aSnapClan[MAX_CLAN_LENGTH] = "";
	char aSnapSkin[MAX_SKIN_LENGTH] = "default";
	bool SnapUseCustomColors = ClientData.m_UseCustomColor != 0;
	unsigned SnapBodyColor = (unsigned)ClientData.m_ColorBody;
	unsigned SnapFeetColor = (unsigned)ClientData.m_ColorFeet;
	if(const void *pSnapData = pGameClient->Client()->SnapFindItem(IClient::SNAP_CURRENT, NETOBJTYPE_CLIENTINFO, ClientId))
	{
		const auto *pInfo = static_cast<const CNetObj_ClientInfo *>(pSnapData);
		if(!IntsToStr(pInfo->m_aName, std::size(pInfo->m_aName), aSnapName, sizeof(aSnapName)))
			str_copy(aSnapName, "nameless tee");
		IntsToStr(pInfo->m_aClan, std::size(pInfo->m_aClan), aSnapClan, sizeof(aSnapClan));
		IntsToStr(pInfo->m_aSkin, std::size(pInfo->m_aSkin), aSnapSkin, sizeof(aSnapSkin));
		if(aSnapSkin[0] == '\0')
			str_copy(aSnapSkin, "default");
		SnapUseCustomColors = pInfo->m_UseCustomColor != 0;
		SnapBodyColor = (unsigned)pInfo->m_ColorBody;
		SnapFeetColor = (unsigned)pInfo->m_ColorFeet;
	}
	else
	{
		str_copy(aSnapName, ClientData.m_aName);
		str_copy(aSnapClan, ClientData.m_aClan);
		str_copy(aSnapSkin, ClientData.m_aSkinName[0] == '\0' ? "default" : ClientData.m_aSkinName);
	}

	SDemoTeeOverride &TeeOverride = m_aOverrides[ClientId];
	if(!TeeOverride.m_Active || str_comp(TeeOverride.m_aOriginalName, aSnapName) != 0)
	{
		TeeOverride = {};
		TeeOverride.m_Active = true;
		str_copy(TeeOverride.m_aOriginalName, aSnapName);
		str_copy(TeeOverride.m_aOriginalClan, aSnapClan);
		str_copy(TeeOverride.m_aOriginalSkinName, aSnapSkin);
		str_copy(TeeOverride.m_aName, aSnapName);
		str_copy(TeeOverride.m_aClan, aSnapClan);
		str_copy(TeeOverride.m_aSkinName, aSnapSkin);
		TeeOverride.m_UseCustomColors = SnapUseCustomColors;
		TeeOverride.m_OriginalUseCustomColors = SnapUseCustomColors;
		TeeOverride.m_BodyColor = TeeOverride.m_OriginalBodyColor = SnapBodyColor;
		TeeOverride.m_FeetColor = TeeOverride.m_OriginalFeetColor = SnapFeetColor;
	}

	CUi *pUi = pGameClient->Ui();
	pUi->ClosePopupMenu(&m_EditorPopupId, true);
	pUi->SetActiveItem(nullptr);
	m_SelectedClient = ClientId;
	m_NameInput.SetBuffer(TeeOverride.m_aName, sizeof(TeeOverride.m_aName), sizeof(g_Config.m_PlayerName));
	m_ClanInput.SetBuffer(TeeOverride.m_aClan, sizeof(TeeOverride.m_aClan), sizeof(g_Config.m_PlayerClan));

	const CUIRect *pScreen = pUi->Screen();
	const float PopupWidth = std::min(620.0f, pScreen->w - 16.0f);
	const float PopupHeight = std::min(246.0f, pScreen->h - 16.0f);
	const float Margin = 8.0f;
	float PopupX = pUi->MouseX() + 12.0f;
	float PopupY = pUi->MouseY() + 12.0f;
	if(PopupX + PopupWidth > pScreen->x + pScreen->w - Margin)
		PopupX = pUi->MouseX() - PopupWidth - 12.0f;
	if(PopupY + PopupHeight > pScreen->y + pScreen->h - Margin)
		PopupY = pUi->MouseY() - PopupHeight - 12.0f;
	PopupX = std::clamp(PopupX, pScreen->x + Margin, std::max(pScreen->x + Margin, pScreen->x + pScreen->w - PopupWidth - Margin));
	PopupY = std::clamp(PopupY, pScreen->y + Margin, std::max(pScreen->y + Margin, pScreen->y + pScreen->h - PopupHeight - Margin));
	pUi->DoPopupMenu(&m_EditorPopupId, PopupX, PopupY, PopupWidth, PopupHeight, pGameClient, PopupEditor);
}

CUi::EPopupMenuFunctionResult CDemoTeeEditor::PopupEditor(void *pContext, CUIRect View, bool)
{
	auto *pGameClient = static_cast<CGameClient *>(pContext);
	CDemoTeeEditor &Editor = pGameClient->m_DemoTeeEditor;
	CMenus *pMenus = &pGameClient->m_Menus;
	const int ClientId = Editor.m_SelectedClient;
	if(pGameClient->Client()->State() != IClient::STATE_DEMOPLAYBACK ||
		ClientId < 0 || ClientId >= MAX_CLIENTS || pMenus->m_Popup != CMenus::POPUP_NONE)
		return CUi::POPUP_CLOSE_CURRENT_AND_DESCENDANTS;
	SDemoTeeOverride &TeeOverride = Editor.m_aOverrides[ClientId];
	const CGameClient::CClientData &ClientData = pGameClient->m_aClients[ClientId];
	char aCurrentName[MAX_NAME_LENGTH];
	if(const void *pSnapData = pGameClient->Client()->SnapFindItem(IClient::SNAP_CURRENT, NETOBJTYPE_CLIENTINFO, ClientId))
	{
		const auto *pInfo = static_cast<const CNetObj_ClientInfo *>(pSnapData);
		if(!IntsToStr(pInfo->m_aName, std::size(pInfo->m_aName), aCurrentName, sizeof(aCurrentName)))
			str_copy(aCurrentName, "nameless tee");
	}
	else
		str_copy(aCurrentName, ClientData.m_aName);
	if(!TeeOverride.m_Active || str_comp(TeeOverride.m_aOriginalName, aCurrentName) != 0)
		return CUi::POPUP_CLOSE_CURRENT_AND_DESCENDANTS;

	const ColorRGBA PanelColor(0.16f, 0.16f, 0.16f, 0.95f);
	const ColorRGBA PreviewColor(0.22f, 0.22f, 0.22f, 1.0f);

	constexpr float Gap = 6.0f;
	View.Margin(8.0f, &View);
	CUIRect Header, CloseButton, ResetButton;
	View.HSplitTop(24.0f, &Header, &View);
	Header.VSplitRight(24.0f, &Header, &CloseButton);
	Header.VSplitRight(4.0f, &Header, nullptr);
	Header.VSplitRight(24.0f, &Header, &ResetButton);
	char aTitle[MAX_NAME_LENGTH + 64];
	str_format(aTitle, sizeof(aTitle), "%s · %s", Localize("Demo tee editor"), TeeOverride.m_aOriginalName);
	CUi *pUi = pGameClient->Ui();
	pUi->DoLabel(&Header, aTitle, 15.0f, TEXTALIGN_ML);
	static CButtonContainer s_ResetButton;
	static CButtonContainer s_CloseButton;
	const int ResetPressed = pMenus->DoButton_Menu(&s_ResetButton, "", 0, &ResetButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f);
	CUIRect ResetIcon;
	ResetButton.HMargin(2.0f, &ResetIcon);
	pGameClient->TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	pGameClient->TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
	pUi->DoLabel(&ResetIcon, FontIcon::ARROW_ROTATE_LEFT, ResetIcon.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
	pGameClient->TextRender()->SetRenderFlags(0);
	pGameClient->TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	if(ResetPressed)
	{
		Editor.m_NameInput.Set(TeeOverride.m_aOriginalName);
		Editor.m_NameInput.SetCursorOffset(str_length(TeeOverride.m_aOriginalName));
		Editor.m_ClanInput.Set(TeeOverride.m_aOriginalClan);
		str_copy(TeeOverride.m_aSkinName, TeeOverride.m_aOriginalSkinName);
		TeeOverride.m_BodyColor = TeeOverride.m_OriginalBodyColor;
		TeeOverride.m_FeetColor = TeeOverride.m_OriginalFeetColor;
		TeeOverride.m_UseCustomColors = TeeOverride.m_OriginalUseCustomColors;
		TeeOverride.m_OverrideAppearance = false;
	}
	const int ClosePressed = pMenus->DoButton_Menu(&s_CloseButton, "", 0, &CloseButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f);
	CUIRect CloseIcon;
	CloseButton.HMargin(2.0f, &CloseIcon);
	pGameClient->TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	pGameClient->TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
	pUi->DoLabel(&CloseIcon, FontIcon::XMARK, CloseIcon.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
	pGameClient->TextRender()->SetRenderFlags(0);
	pGameClient->TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	if(ClosePressed)
	{
		Editor.m_SelectedClient = -1;
		return CUi::POPUP_CLOSE_CURRENT_AND_DESCENDANTS;
	}

	View.HSplitTop(Gap, nullptr, &View);
	CUIRect Identity;
	View.HSplitTop(66.0f, &Identity, &View);
	Identity.Draw(PanelColor, IGraphics::CORNER_ALL, 5.0f);
	Identity.Margin(5.0f, &Identity);
	CUIRect Preview, Details;
	Identity.VSplitLeft(60.0f, &Preview, &Details);
	Preview.Draw(PreviewColor, IGraphics::CORNER_ALL, 4.0f);
	Preview.Margin(2.0f, &Preview);
	CTeeRenderInfo PreviewInfo = ClientData.m_RenderInfo;
	if(const CSkin *pSkin = pGameClient->m_Skins.FindLocalSkin(TeeOverride.m_aSkinName))
		PreviewInfo.Apply(pSkin);
	if(TeeOverride.m_OverrideAppearance)
	{
		PreviewInfo.m_aSixup[g_Config.m_ClDummy].Reset();
		PreviewInfo.ApplyColors(TeeOverride.m_UseCustomColors, TeeOverride.m_BodyColor, TeeOverride.m_FeetColor);
	}
	PreviewInfo.m_Size = 46.0f;
	vec2 OffsetToMid;
	CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &PreviewInfo, OffsetToMid);
	pGameClient->RenderTools()->RenderTee(CAnimState::GetIdle(), &PreviewInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), vec2(Preview.x + Preview.w / 2.0f, Preview.y + Preview.h / 2.0f + OffsetToMid.y));

	Details.VSplitLeft(8.0f, nullptr, &Details);
	CUIRect NameRow, ClanRow, Label, Input;
	Details.HSplitTop(25.0f, &NameRow, &Details);
	NameRow.VSplitLeft(66.0f, &Label, &Input);
	pUi->DoLabel(&Label, Localize("Name"), 13.0f, TEXTALIGN_ML);
	pUi->DoEditBox(&Editor.m_NameInput, &Input, 13.0f);

	Details.HSplitTop(5.0f, nullptr, &Details);
	Details.HSplitTop(25.0f, &ClanRow, nullptr);
	ClanRow.VSplitLeft(66.0f, &Label, &Input);
	pUi->DoLabel(&Label, Localize("Clan"), 13.0f, TEXTALIGN_ML);
	pUi->DoEditBox(&Editor.m_ClanInput, &Input, 13.0f);

	static CButtonContainer s_PreviewButton;
	pGameClient->m_Tooltips.DoToolTip(&s_PreviewButton, &Preview, Localize("Choose a skin"));
	if(pUi->DoButtonLogic(&s_PreviewButton, 0, &Preview, BUTTONFLAG_LEFT))
	{
		Editor.m_SkinSearchInput.Clear();
		Editor.m_SkinPickerResetScroll = true;
		Editor.m_SkinSearchInput.SetEmptyText(Localize("Search"));
		const CUIRect *pScreen = pUi->Screen();
		const float PickerWidth = std::min(340.0f, pScreen->w - 16.0f);
		const float PickerHeight = std::min(330.0f, pScreen->h - 16.0f);
		float PickerX = Preview.x + Preview.w + 4.0f;
		float PickerY = Preview.y;
		if(PickerX + PickerWidth > pScreen->x + pScreen->w - 8.0f)
			PickerX = Preview.x - PickerWidth - 4.0f;
		PickerX = std::clamp(PickerX, pScreen->x + 8.0f, std::max(pScreen->x + 8.0f, pScreen->x + pScreen->w - PickerWidth - 8.0f));
		PickerY = std::clamp(PickerY, pScreen->y + 8.0f, std::max(pScreen->y + 8.0f, pScreen->y + pScreen->h - PickerHeight - 8.0f));
		pUi->DoPopupMenu(&Editor.m_SkinPickerPopupId, PickerX, PickerY, PickerWidth, PickerHeight, pGameClient, PopupSkinPicker);
	}

	View.HSplitTop(Gap, nullptr, &View);
	CUIRect ColorsTitle;
	View.HSplitTop(20.0f, &ColorsTitle, &View);
	if(pMenus->DoButton_CheckBox(&TeeOverride.m_UseCustomColors, Localize("Custom colors"), TeeOverride.m_UseCustomColors, &ColorsTitle))
	{
		TeeOverride.m_UseCustomColors = !TeeOverride.m_UseCustomColors;
		TeeOverride.m_OverrideAppearance = true;
	}
	CUIRect Colors;
	View.HSplitTop(96.0f, &Colors, &View);
	Colors.Draw(PanelColor, IGraphics::CORNER_ALL, 5.0f);
	Colors.Margin(4.0f, &Colors);
	CUIRect Body, Feet;
	Colors.VSplitMid(&Body, &Feet, 8.0f);
	Body.HSplitTop(18.0f, &Label, &Body);
	pUi->DoLabel(&Label, Localize("Body"), 12.0f, TEXTALIGN_ML);
	if(pMenus->RenderHslaScrollbars(&Body, &TeeOverride.m_BodyColor, false, ColorHSLA::DARKEST_LGT))
	{
		TeeOverride.m_UseCustomColors = true;
		TeeOverride.m_OverrideAppearance = true;
	}
	Feet.HSplitTop(18.0f, &Label, &Feet);
	pUi->DoLabel(&Label, Localize("Feet"), 12.0f, TEXTALIGN_ML);
	if(pMenus->RenderHslaScrollbars(&Feet, &TeeOverride.m_FeetColor, false, ColorHSLA::DARKEST_LGT))
	{
		TeeOverride.m_UseCustomColors = true;
		TeeOverride.m_OverrideAppearance = true;
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CDemoTeeEditor::PopupSkinPicker(void *pContext, CUIRect View, bool Active)
{
	auto *pGameClient = static_cast<CGameClient *>(pContext);
	CDemoTeeEditor &Editor = pGameClient->m_DemoTeeEditor;
	CMenus *pMenus = &pGameClient->m_Menus;
	const int ClientId = Editor.m_SelectedClient;
	if(pGameClient->Client()->State() != IClient::STATE_DEMOPLAYBACK || !pMenus->IsActive() ||
		ClientId < 0 || ClientId >= MAX_CLIENTS || pMenus->m_Popup != CMenus::POPUP_NONE)
		return CUi::POPUP_CLOSE_CURRENT;
	if(!Active)
		return CUi::POPUP_KEEP_OPEN;

	const SDemoTeeOverride &TeeOverride = Editor.m_aOverrides[ClientId];
	const CGameClient::CClientData &ClientData = pGameClient->m_aClients[ClientId];
	if(!TeeOverride.m_Active || str_comp(TeeOverride.m_aOriginalName, ClientData.m_aName) != 0)
		return CUi::POPUP_CLOSE_CURRENT;

	View.Margin(6.0f, &View);
	CUIRect Title, Search, Rows;
	View.HSplitTop(18.0f, &Title, &View);
	CUi *pUi = pGameClient->Ui();
	pUi->DoLabel(&Title, Localize("Choose a skin"), 13.0f, TEXTALIGN_ML);
	View.HSplitTop(4.0f, nullptr, &View);
	View.HSplitTop(22.0f, &Search, &Rows);
	Search.Draw(ColorRGBA(0.10f, 0.10f, 0.10f, 0.95f), IGraphics::CORNER_ALL, 4.0f);
	Search.Margin(2.0f, &Search);
	const bool SearchChanged = pUi->DoEditBox_Search(&Editor.m_SkinSearchInput, &Search, 12.0f, false);
	Rows.HSplitTop(5.0f, nullptr, &Rows);

	const auto &vpLocalSkins = pGameClient->m_Skins.LocalSkinList();
	std::vector<CSkins::CSkinContainer *> vpFilteredEntries;
	int SelectedIndex = -1;
	for(CSkins::CSkinContainer *pSkinContainer : vpLocalSkins)
	{
		if(pSkinContainer == nullptr || pSkinContainer->IsSpecial() ||
			pSkinContainer->State() == CSkins::CSkinContainer::EState::ERROR ||
			pSkinContainer->State() == CSkins::CSkinContainer::EState::NOT_FOUND)
			continue;
		if(Editor.m_SkinSearchInput.GetString()[0] != '\0' &&
			str_utf8_find_nocase(pSkinContainer->Name(), Editor.m_SkinSearchInput.GetString(), nullptr) == nullptr)
			continue;
		if(str_comp(pSkinContainer->Name(), TeeOverride.m_aSkinName) == 0)
			SelectedIndex = vpFilteredEntries.size();
		vpFilteredEntries.push_back(pSkinContainer);
	}

	static CListBox s_SkinList;
	if(SearchChanged || Editor.m_SkinPickerResetScroll)
		s_SkinList.Reset();
	if(Editor.m_SkinPickerResetScroll)
	{
		s_SkinList.ScrollToSelected();
		Editor.m_SkinPickerResetScroll = false;
	}
	s_SkinList.DoStart(42.0f, vpFilteredEntries.size(), 1, 2, SelectedIndex, &Rows);
	const CSkin *pDefaultSkin = pGameClient->m_Skins.Find("default");
	for(size_t i = 0; i < vpFilteredEntries.size(); ++i)
	{
		CSkins::CSkinContainer *pSkinContainer = vpFilteredEntries[i];
		const CListboxItem Item = s_SkinList.DoNextItem(pSkinContainer, (int)i == SelectedIndex);
		if(!Item.m_Visible)
			continue;

		pSkinContainer->RequestLoad();
		const CSkin *pSkin = pSkinContainer->State() == CSkins::CSkinContainer::EState::LOADED ? pSkinContainer->Skin().get() : pDefaultSkin;
		CUIRect Preview, Label;
		Item.m_Rect.VSplitLeft(42.0f, &Preview, &Label);
		CTeeRenderInfo PreviewInfo = ClientData.m_RenderInfo;
		PreviewInfo.Apply(pSkin);
		PreviewInfo.m_aSixup[g_Config.m_ClDummy].Reset();
		PreviewInfo.ApplyColors(TeeOverride.m_UseCustomColors, TeeOverride.m_BodyColor, TeeOverride.m_FeetColor);
		PreviewInfo.m_Size = 34.0f;
		vec2 OffsetToMid;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &PreviewInfo, OffsetToMid);
		pGameClient->RenderTools()->RenderTee(CAnimState::GetIdle(), &PreviewInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), vec2(Preview.x + Preview.w / 2.0f, Preview.y + Preview.h / 2.0f + OffsetToMid.y));
		Label.VSplitLeft(8.0f, nullptr, &Label);
		pUi->DoLabel(&Label, pSkinContainer->Name(), 12.0f, TEXTALIGN_ML);
	}

	const int NewSelected = s_SkinList.DoEnd();
	if((s_SkinList.WasItemSelected() || s_SkinList.WasItemActivated()) &&
		NewSelected >= 0 && (size_t)NewSelected < vpFilteredEntries.size())
	{
		CSkins::CSkinContainer *pSkinContainer = vpFilteredEntries[NewSelected];
		str_copy(Editor.m_aOverrides[ClientId].m_aSkinName, pSkinContainer->Name());
		Editor.m_aOverrides[ClientId].m_OverrideAppearance = true;
		pSkinContainer->RequestLoad();
		return CUi::POPUP_CLOSE_CURRENT;
	}

	if(vpFilteredEntries.empty())
		pUi->DoLabel(&Rows, Localize("No skins match your filter criteria"), 12.0f, TEXTALIGN_MC);
	return CUi::POPUP_KEEP_OPEN;
}

void CDemoTeeEditor::UpdateSelection(CGameClient *pGameClient, const CUIRect &DemoControls)
{
	if(m_SelectedClient >= 0 && !EditorPopupOpen(pGameClient))
		m_SelectedClient = -1;

	CUi *pUi = pGameClient->Ui();
	const bool CanSelect = pGameClient->m_Menus.IsActive() && pGameClient->m_Menus.m_Popup == CMenus::POPUP_NONE &&
		!pGameClient->m_GameConsole.IsActive() && !pGameClient->m_AspectRatio.ConfirmWantsInput() &&
		(!pUi->IsPopupOpen() || (EditorPopupOpen(pGameClient) && !EditorPopupHovered(pGameClient))) &&
		pUi->MouseButton(0) == 0 && pUi->LastMouseButton(0) &&
		!DemoControls.Inside(pUi->MousePos());
	if(!CanSelect)
		return;

	vec2 WorldSize;
	pGameClient->m_AspectRatio.BeginCursorGameAspect();
	pGameClient->Graphics()->CalcScreenParams(pGameClient->Graphics()->ScreenAspect(), pGameClient->m_Camera.m_Zoom, &WorldSize.x, &WorldSize.y);
	pGameClient->m_AspectRatio.EndCursorGameAspect();
	const CUIRect *pScreen = pUi->Screen();
	const vec2 MouseWorldPos = DemoTeeSelection::MouseToWorld(pUi->MousePos() - vec2(pScreen->x, pScreen->y),
		vec2(pScreen->w, pScreen->h), pGameClient->m_Camera.m_Center, WorldSize);
	const float Radius = std::max(32.0f, 8.0f * std::max(WorldSize.x / std::max(1, pGameClient->Graphics()->WindowWidth()), WorldSize.y / std::max(1, pGameClient->Graphics()->WindowHeight())));
	std::vector<DemoTeeSelection::SCandidate> vCandidates;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(!pGameClient->m_Snap.m_aCharacters[ClientId].m_Active ||
			pGameClient->m_Snap.m_apPlayerInfos[ClientId] == nullptr ||
			pGameClient->m_Snap.m_apPrevPlayerInfos[ClientId] == nullptr)
			continue;
		vCandidates.push_back({ClientId, pGameClient->m_aClients[ClientId].m_RenderPos});
	}
	const int NearestClientId = DemoTeeSelection::Pick(vCandidates, MouseWorldPos, Radius, m_SelectedClient);
	if(NearestClientId >= 0)
		Open(pGameClient, NearestClientId);
}

void CDemoTeeEditor::CloseIfMenuInactive(CGameClient *pGameClient, bool MenuActive)
{
	if(MenuActive)
		return;
	if(EditorPopupOpen(pGameClient))
		pGameClient->Ui()->ClosePopupMenu(&m_EditorPopupId, true);
	m_SelectedClient = -1;
}

bool CDemoTeeEditor::SliceHasUnsupportedEdits(bool Sixup) const
{
	if(!Sixup)
		return false;
	return std::any_of(m_aOverrides.begin(), m_aOverrides.end(), [](const SDemoTeeOverride &TeeOverride) {
		return TeeOverride.m_Active && (TeeOverride.m_OverrideAppearance || str_comp(TeeOverride.m_aName, TeeOverride.m_aOriginalName) != 0 || str_comp(TeeOverride.m_aClan, TeeOverride.m_aOriginalClan) != 0);
	});
}

void CDemoTeeEditor::ApplyToSlice(void *pData, int Size, bool Sixup) const
{
	ApplyDemoTeeOverrides(pData, Size, Sixup, m_aOverrides);
}

void CDemoTeeEditor::ApplyToSliceCallback(void *pData, int Size, bool Sixup, void *pUser)
{
	static_cast<const CDemoTeeEditor *>(pUser)->ApplyToSlice(pData, Size, Sixup);
}

void CDemoTeeEditor::PrepareCutRender(const char *pPath)
{
#if defined(CONF_VIDEORECORDER)
	m_aRenderOverrides = m_aOverrides;
	str_copy(m_aRenderFilename, pPath);
	for(auto &TeeOverride : m_aRenderOverrides)
	{
		str_copy(TeeOverride.m_aOriginalName, TeeOverride.m_aName);
		if(TeeOverride.m_OverrideAppearance)
		{
			str_copy(TeeOverride.m_aOriginalSkinName, TeeOverride.m_aSkinName);
			TeeOverride.m_OriginalUseCustomColors = TeeOverride.m_UseCustomColors;
			TeeOverride.m_OriginalBodyColor = TeeOverride.m_BodyColor;
			TeeOverride.m_OriginalFeetColor = TeeOverride.m_FeetColor;
		}
	}
#else
	(void)pPath;
#endif
}

void CDemoTeeEditor::RestoreCutRender(const char *pPath)
{
#if defined(CONF_VIDEORECORDER)
	if(str_comp(m_aRenderFilename, pPath) != 0)
		return;
	m_aOverrides = m_aRenderOverrides;
	m_aRenderFilename[0] = '\0';
#else
	(void)pPath;
#endif
}

void CDemoTeeEditor::ClearCutRender()
{
#if defined(CONF_VIDEORECORDER)
	m_aRenderFilename[0] = '\0';
#endif
}
