/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_SETTINGS_SEARCH_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_SETTINGS_SEARCH_H

#include <game/client/ui_rect.h>

class CMenus;
class CScrollRegion;
class CGameClient;
class CUi;
class IInput;

namespace BestClientSettingsSearch
{

struct SEntry
{
	const char *m_pName;
	int m_Leaf;
	const char *m_pSection;
	const void *m_pId;
	const int *m_pRequireEnabled;
};

void RenderCornerButton(CMenus *pMenus, CUi *pUi, CGameClient *pGameClient, IInput *pInput, CUIRect Corner);
void Update(float DeltaTime);
void DrawItemHighlight(const void *pId, const CUIRect *pRect);
void DrawBlockHighlight(const void *pId, const CUIRect *pRect);
void SetActiveScrollRegion(CScrollRegion *pRegion);
void ClearActiveScrollRegion(CScrollRegion *pRegion);

} // namespace BestClientSettingsSearch

#endif
