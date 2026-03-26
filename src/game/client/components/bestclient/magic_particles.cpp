#include "magic_particles.h"

#include <base/math.h>

#include <engine/client.h>
#include <engine/shared/config.h>

#include <generated/client_data.h>

#include <game/client/components/particles.h>
#include <game/client/gameclient.h>

void CMagicParticles::ResetState()
{
	m_SpawnAccumulator = 0.0f;
}

bool CMagicParticles::CanSpawnParticles(int &Count, float &Radius, float &Size, float &AlphaDelay, int &Type, vec2 &BasePos) const
{
	if(GameClient()->OptimizerDisableParticles())
		return false;
	if(!g_Config.m_BcMagicParticles)
		return false;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return false;

	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(!in_range(LocalId, 0, MAX_CLIENTS - 1))
		return false;

	const auto &LocalPlayer = GameClient()->m_aClients[LocalId];
	if(!LocalPlayer.m_Active || LocalPlayer.m_Team == TEAM_SPECTATORS || !GameClient()->m_Snap.m_aCharacters[LocalId].m_Active || GameClient()->m_Snap.m_SpecInfo.m_Active)
		return false;

	BasePos = LocalPlayer.m_RenderPos;
	Count = std::clamp(g_Config.m_BcMagicParticlesCount, 0, 100);
	Radius = (float)g_Config.m_BcMagicParticlesRadius;
	Size = (float)g_Config.m_BcMagicParticlesSize;
	AlphaDelay = (float)g_Config.m_BcMagicParticlesAlphaDelay;
	Type = g_Config.m_BcMagicParticlesType;
	return Count > 0 && Radius > 0.0f && Size > 0.0f && AlphaDelay > 0.0f;
}

void CMagicParticles::OnReset()
{
	ResetState();
}

void CMagicParticles::OnRender()
{
	int Count = 0;
	int Type = 0;
	float Radius = 0.0f;
	float Size = 0.0f;
	float AlphaDelay = 0.0f;
	vec2 BasePos(0.0f, 0.0f);
	if(!CanSpawnParticles(Count, Radius, Size, AlphaDelay, Type, BasePos))
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

	int Sprite = SPRITE_PART_BALL;
	switch(Type)
	{
	case 1:
		Sprite = SPRITE_PART_SLICE;
		break;
	case 2:
		Sprite = SPRITE_PART_BALL;
		break;
	case 3:
		Sprite = SPRITE_PART_SMOKE;
		break;
	case 4:
		Sprite = SPRITE_PART_SHELL;
		break;
	default:
		break;
	}

	const float LifeSpan = AlphaDelay / 10.0f;
	for(int i = 0; i < SpawnCount; i++)
	{
		CParticle p;
		p.SetDefault();
		p.m_Spr = Sprite;

		const float Angle = random_float(0.0f, 2.0f * pi);
		const float Dist = random_float(0.0f, Radius);
		const vec2 Offset(cosf(Angle) * Dist, sinf(Angle) * Dist);

		p.m_Pos = BasePos + Offset;
		p.m_LifeSpan = LifeSpan;
		p.m_StartSize = Size;
		p.m_EndSize = 0.0f;
		p.m_Friction = 0.7f;
		p.m_Color = ColorRGBA(random_float(), random_float(), random_float(), random_float(0.5f, 1.0f));
		p.m_StartAlpha = p.m_Color.a;
		GameClient()->m_Particles.Add(CParticles::GROUP_PROJECTILE_TRAIL, &p, Delta);
	}
}
