#include "magic_particles.h"

#include <base/math.h>

#include <engine/client.h>
#include <engine/shared/config.h>

#include <game/client/components/effects.h>
#include <game/client/gameclient.h>

void CMagicParticles::ResetState()
{
	m_SpawnAccumulator = 0.0f;
}

bool CMagicParticles::CanSpawnParticles(int &Count, float &Radius, float &Size, float &AlphaDelay, int &Type) const
{
	if(GameClient()->m_BestClient.OptimizerDisableParticles())
		return false;
	if(!g_Config.m_BcMagicParticles)
		return false;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return false;

	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId < 0 || LocalId >= MAX_CLIENTS)
		return false;

	const auto &LocalPlayer = GameClient()->m_aClients[LocalId];
	if(!LocalPlayer.m_Active || LocalPlayer.m_Team == TEAM_SPECTATORS || !GameClient()->m_Snap.m_aCharacters[LocalId].m_Active || GameClient()->m_Snap.m_SpecInfo.m_Active)
		return false;

	Count = std::clamp(g_Config.m_BcMagicParticlesCount, 0, 100);
	Radius = (float)g_Config.m_BcMagicParticlesRadius;
	Size = (float)g_Config.m_BcMagicParticlesSize;
	AlphaDelay = (float)g_Config.m_BcMagicParticlesAlphaDelay;
	Type = g_Config.m_BcMagicParticlesType;
	if(Count <= 0 || Radius <= 0.0f || Size <= 0.0f || AlphaDelay <= 0.0f)
		return false;
	return true;
}

void CMagicParticles::OnReset()
{
	ResetState();
}

void CMagicParticles::OnRender()
{
	if(GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_MAGIC_PARTICLES))
		return;

	int Count = 0;
	int Type = 0;
	float Radius = 0.0f;
	float Size = 0.0f;
	float AlphaDelay = 0.0f;
	if(!CanSpawnParticles(Count, Radius, Size, AlphaDelay, Type))
	{
		if(m_SpawnAccumulator != 0.0f)
			ResetState();
		return;
	}

	const float Delta = std::clamp(Client()->RenderFrameTime(), 0.0f, 0.1f);
	if(Delta <= 0.0f)
		return;

	const float SpawnRate = Count * 60.0f;
	m_SpawnAccumulator += Delta * SpawnRate;

	const int MaxBurst = std::clamp(Count * 2, 8, 128);
	const int SpawnCount = minimum((int)m_SpawnAccumulator, MaxBurst);
	if(SpawnCount <= 0)
		return;

	m_SpawnAccumulator -= SpawnCount;
	GameClient()->m_Effects.MagicParticles(Radius, Size, Type, AlphaDelay, (float)SpawnCount);
}
