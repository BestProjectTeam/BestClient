/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_PROFILE_ASSETS_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_PROFILE_ASSETS_H

#include <game/client/ui.h>

class CMenus;
class CProfile;

void BestClientApplyProfileAssets(class CGameClient *pGameClient, const CProfile &Profile);
void BestClientFillProfileAssets(CProfile &Profile);
void BestClientDoProfileAssetsDropDown(CUi *pUi, CMenus *pMenus, CUIRect *pRect);

#endif
