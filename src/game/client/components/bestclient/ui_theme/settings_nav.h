/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_UI_THEME_SETTINGS_NAV_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_UI_THEME_SETTINGS_NAV_H

#include <game/client/components/bestclient/ui_animations.h>
#include <game/client/components/bestclient/ui_theme/style.h>

class CMenus;
class CUIRect;

namespace BestClientUiTheme
{

enum ESettingsNavGroup
{
	SETTINGS_NAV_GROUP_DDNET = 0,
	SETTINGS_NAV_GROUP_TCLIENT,
	SETTINGS_NAV_GROUP_BESTCLIENT,
	SETTINGS_NAV_GROUP_COUNT,
};

enum ESettingsNavLeaf
{
	SETTINGS_NAV_LEAF_GENERAL = 0,
	SETTINGS_NAV_LEAF_TEE,
	SETTINGS_NAV_LEAF_APPEARANCE,
	SETTINGS_NAV_LEAF_CONTROLS,
	SETTINGS_NAV_LEAF_GRAPHICS,
	SETTINGS_NAV_LEAF_SOUND,
	SETTINGS_NAV_LEAF_DDNET,
	SETTINGS_NAV_LEAF_ASSETS,
	SETTINGS_NAV_LEAF_CREDITS,
	SETTINGS_NAV_LEAF_TC_SETTINGS,
	SETTINGS_NAV_LEAF_TC_BIND_WHEEL,
	SETTINGS_NAV_LEAF_TC_WAR_LIST,
	SETTINGS_NAV_LEAF_TC_CHAT_BINDS,
	SETTINGS_NAV_LEAF_TC_STATUS_BAR,
	SETTINGS_NAV_LEAF_TC_INFO,
	SETTINGS_NAV_LEAF_TC_PROFILE,
	SETTINGS_NAV_LEAF_TC_CONFIGS,
	SETTINGS_NAV_LEAF_BC_VISUALS,
	SETTINGS_NAV_LEAF_BC_GAMEPLAY,
	SETTINGS_NAV_LEAF_BC_OTHERS,
	SETTINGS_NAV_LEAF_BC_INFO,
	SETTINGS_NAV_LEAF_COUNT,
};

struct SSettingsLeafTarget
{
	int m_SettingsPage = 0;
	int m_TClientTab = 0;
	int m_BestClientTab = 0;
};

SSettingsLeafTarget LeafTarget(int Leaf);
int LeafFromState(int SettingsPage, int TClientTab, int BestClientTab);
int GroupForLeaf(int Leaf);
bool IsLeafVisible(int Leaf, int TcTabFlags, int BcTabFlags);

void ApplyLeaf(CMenus *pMenus, int Leaf);

BCUiAnimations::STabSwitch AnimateVerticalLeafSwitch(int SelectedLeaf, float DeltaTime);
void VerticalLeafContentOffsets(float Progress, int FromLeaf, int ToLeaf, float Height, float &FromOffset, float &ToOffset);

} // namespace BestClientUiTheme

#endif
