/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_HUD_EXTRAS_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_HUD_EXTRAS_H

#include <base/vmath.h>

#include <engine/shared/protocol.h>

class CGameClient;
class IClient;
class IGraphics;
class ITextRender;

namespace HudExtras
{
bool HasPlayerBelowOnSameX(CGameClient *pGameClient, int ClientId, const vec2 &PosBlocks);
int GetCheckpointId(CGameClient *pGameClient);
void RenderPlayerBelowIndicator(CGameClient *pGameClient, IGraphics *pGraphics, ITextRender *pTextRender, float Width, float Height, float Dt);

class SSpectatorCountState
{
public:
	int m_Count = 0;
	char m_aCountBuf[16] = {};
	char m_aaNameLines[6][MAX_NAME_LENGTH + 8] = {};
	int m_NumNameLines = 0;
};

bool GetSpectatorCountState(CGameClient *pGameClient, IClient *pClient, int &LastSpectatorCountTick, SSpectatorCountState &State, bool ForcePreview);
}

#endif
