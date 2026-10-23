/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_KEYBIND_MAP_UI_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_KEYBIND_MAP_UI_H

#include <game/client/components/bestclient/keybind_map.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>

#include <string>
#include <vector>

class CGameClient;

struct CKeybindMapActionRef
{
	const char *m_pLabel;
	const char *m_pCommand;
};

struct CKeybindMapUi
{
	CButtonContainer m_aScaleButtons[3];
	CButtonContainer m_aKeyButtons[KEY_LAST];
	CButtonContainer m_ResetButton;
	CButtonContainer m_DeleteButton;
	CButtonContainer m_SaveButton;
	std::vector<CButtonContainer> m_vActionButtons;
	std::vector<CKeybindMapActionRef> m_vActions;
	CLineInputBuffered<512> m_Command;
	CScrollRegion m_ActionScroll;
	std::vector<std::string> m_vCompletions;
	char m_aCompletionBase[512] = {'\0'};
	int m_CompletionStart = 0;
	int m_CompletionEnd = 0;
	int m_CompletionIndex = -1;
	int m_Format = (int)EKeybindMapFormat::COMPACT;
	int m_SelectedKey = KEY_UNKNOWN;
	int m_HoveredKey = KEY_UNKNOWN;
};

void RenderBestClientKeybindMap(CGameClient *pClient, CUi *pUi, CUIRect View, CKeybindMapUi &Ui, const CKeybindMapActionRef *pActions, int NumActions, const char *pFilter);

#endif
