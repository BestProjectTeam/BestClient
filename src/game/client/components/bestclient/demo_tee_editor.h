/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_DEMO_TEE_EDITOR_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_DEMO_TEE_EDITOR_H

#include <game/client/demo_tee_edit.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>

#include <array>

class CGameClient;
class CMenus;
class CUIRect;

class CDemoTeeEditor
{
	std::array<SDemoTeeOverride, MAX_CLIENTS> m_aOverrides{};
#if defined(CONF_VIDEORECORDER)
	std::array<SDemoTeeOverride, MAX_CLIENTS> m_aRenderOverrides{};
	char m_aRenderFilename[IO_MAX_PATH_LENGTH] = "";
#endif
	int m_SelectedClient = -1;
	CLineInput m_NameInput;
	CLineInput m_ClanInput;
	SPopupMenuId m_EditorPopupId;
	SPopupMenuId m_SkinPickerPopupId;
	CLineInputBuffered<MAX_SKIN_LENGTH> m_SkinSearchInput;
	bool m_SkinPickerResetScroll = true;

	static CUi::EPopupMenuFunctionResult PopupEditor(void *pContext, CUIRect View, bool Active);
	static CUi::EPopupMenuFunctionResult PopupSkinPicker(void *pContext, CUIRect View, bool Active);

public:
	const SDemoTeeOverride *Override(int ClientId) const;
	int SelectedClientId() const { return m_SelectedClient; }
	void ClearSelection() { m_SelectedClient = -1; }

	void Reset();
	void Close(CGameClient *pGameClient);
	bool EditorPopupOpen(CGameClient *pGameClient) const;
	bool EditorPopupHovered(CGameClient *pGameClient) const;

	void Open(CGameClient *pGameClient, int ClientId);
	void UpdateSelection(CGameClient *pGameClient, const CUIRect &DemoControls);
	void CloseIfMenuInactive(CGameClient *pGameClient, bool MenuActive);

	bool SliceHasUnsupportedEdits(bool Sixup) const;
	void ApplyToSlice(void *pData, int Size, bool Sixup) const;
	static void ApplyToSliceCallback(void *pData, int Size, bool Sixup, void *pUser);

	void PrepareCutRender(const char *pPath);
	void RestoreCutRender(const char *pPath);
	void ClearCutRender();
};

#endif
