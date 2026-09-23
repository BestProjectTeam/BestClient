/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_FX_OWN_TEE_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_FX_OWN_TEE_H

#include <engine/shared/config.h>

#include <game/client/gameclient.h>

inline bool BcIsOwnTee(const CGameClient *pGameClient, int ClientId)
{
	if(pGameClient == nullptr || ClientId < 0)
		return false;
	if(ClientId == pGameClient->m_Snap.m_LocalClientId)
		return true;
	for(int Dummy = 0; Dummy < NUM_DUMMIES; Dummy++)
	{
		if(ClientId == pGameClient->m_aLocalIds[Dummy])
			return true;
	}
	return false;
}

inline bool BcFxSuppressed(const CGameClient *pGameClient)
{
	if(g_Config.m_ClFocusMode && g_Config.m_ClFocusModeHideEffects)
		return true;
	return pGameClient != nullptr && pGameClient->OptimizerDisableParticles();
}

#endif
