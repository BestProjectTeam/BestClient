#include "afterimage.h"

#include <base/math.h>

#include <engine/shared/config.h>

#include <generated/client_data.h>
#include <generated/protocol.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>

#include <algorithm>

void CAfterimage::ResetState()
{
	m_vGhostSamples.clear();
	m_HasLastRenderInfo = false;
	m_LastRenderInfo.Reset();
}

bool CAfterimage::CanSampleAfterimage(int &LocalClientId) const
{
	if(!g_Config.m_BcAfterimage)
		return false;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return false;

	LocalClientId = GameClient()->m_Snap.m_LocalClientId;
	if(!in_range(LocalClientId, 0, MAX_CLIENTS - 1))
		return false;

	const auto &LocalPlayer = GameClient()->m_aClients[LocalClientId];
	return LocalPlayer.m_Active &&
		GameClient()->m_Snap.m_aCharacters[LocalClientId].m_Active &&
		LocalPlayer.m_Team != TEAM_SPECTATORS &&
		!GameClient()->m_Snap.m_SpecInfo.m_Active;
}

void CAfterimage::RenderAfterimageWeapon(const SGhostSample &Sample, float Alpha)
{
	if(Alpha <= 0.0f || Sample.m_Weapon < 0 || Sample.m_Weapon >= NUM_WEAPONS)
		return;

	const int CurrentWeapon = std::clamp(Sample.m_Weapon, 0, NUM_WEAPONS - 1);
	const CDataWeaponspec &WeaponSpec = g_pData->m_Weapons.m_aId[CurrentWeapon];
	const IGraphics::CTextureHandle &WeaponTexture = GameClient()->m_GameSkin.m_aSpriteWeapons[CurrentWeapon];
	if(!WeaponTexture.IsValid())
		return;

	vec2 Dir = Sample.m_Dir;
	if(length(Dir) < 0.0001f)
		Dir = vec2(1.0f, 0.0f);
	else
		Dir = normalize(Dir);

	vec2 WeaponPosition = Sample.m_Pos + Dir * WeaponSpec.m_Offsetx;
	WeaponPosition.y += WeaponSpec.m_Offsety;
	if(CurrentWeapon == WEAPON_HAMMER || CurrentWeapon == WEAPON_NINJA)
	{
		WeaponPosition = Sample.m_Pos;
		WeaponPosition.y += WeaponSpec.m_Offsety;
		if(Dir.x < 0.0f)
			WeaponPosition.x -= WeaponSpec.m_Offsetx;
	}
	else if(CurrentWeapon == WEAPON_GUN && g_Config.m_ClOldGunPosition)
	{
		WeaponPosition.y -= 8.0f;
	}

	float ScaleX = 1.0f, ScaleY = 1.0f;
	Graphics()->GetSpriteScale(WeaponSpec.m_pSpriteBody, ScaleX, ScaleY);

	Graphics()->TextureSet(WeaponTexture);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
	Graphics()->QuadsSetRotation(angle(Dir));
	Graphics()->DrawSprite(
		WeaponPosition.x,
		WeaponPosition.y,
		WeaponSpec.m_VisualSize * ScaleX,
		WeaponSpec.m_VisualSize * ScaleY);
	Graphics()->QuadsSetRotation(0.0f);
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	Graphics()->QuadsEnd();
}

void CAfterimage::OnReset()
{
	ResetState();
}

void CAfterimage::OnStateChange(int NewState, int OldState)
{
	(void)NewState;
	(void)OldState;
	ResetState();
}

void CAfterimage::OnRender()
{
	if(GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_AFTERIMAGE))
	{
		if(!m_vGhostSamples.empty() || m_HasLastRenderInfo)
			ResetState();
		return;
	}

	int LocalClientId = -1;
	if(!CanSampleAfterimage(LocalClientId))
	{
		if(!m_vGhostSamples.empty() || m_HasLastRenderInfo)
			ResetState();
		return;
	}

	const auto &LocalPlayer = GameClient()->m_aClients[LocalClientId];

	const int MaxFrames = std::clamp(g_Config.m_BcAfterimageFrames, 2, 20);
	const int MaxSamples = MaxFrames + 1;
	m_LastRenderInfo = LocalPlayer.m_RenderInfo;
	m_HasLastRenderInfo = m_LastRenderInfo.Valid();

	const float Angle = LocalPlayer.m_RenderCur.m_Angle / 256.0f;
	vec2 Dir = direction(Angle);
	if(length(Dir) < 0.0001f)
		Dir = vec2(1.0f, 0.0f);

	// Keep visible spacing between afterimages so the trail doesn't blend into a blur.
	const float MinAfterimageSpacing = std::clamp((float)g_Config.m_BcAfterimageSpacing, 1.0f, 64.0f);
	const float MinAfterimageSpacingSq = MinAfterimageSpacing * MinAfterimageSpacing;
	const bool NeedFirstSample = m_vGhostSamples.empty();
	bool FarEnoughFromLast = false;
	if(!NeedFirstSample)
	{
		const vec2 Diff = LocalPlayer.m_RenderPos - m_vGhostSamples.front().m_Pos;
		FarEnoughFromLast = dot(Diff, Diff) >= MinAfterimageSpacingSq;
	}
	if(NeedFirstSample || FarEnoughFromLast)
		m_vGhostSamples.push_front({LocalPlayer.m_RenderPos, Dir, LocalPlayer.m_RenderCur.m_Weapon, LocalPlayer.m_RenderCur.m_AttackTick});

	while((int)m_vGhostSamples.size() > MaxSamples)
		m_vGhostSamples.pop_back();

	const int GhostCount = minimum(MaxFrames, (int)m_vGhostSamples.size() - 1);
	if(GhostCount <= 0)
		return;

	const float BaseAlpha = std::clamp(g_Config.m_BcAfterimageAlpha / 100.0f, 0.0f, 1.0f);
	if(BaseAlpha <= 0.0f)
		return;

	if(!m_HasLastRenderInfo)
		return;

	CTeeRenderInfo GhostInfo = m_LastRenderInfo;
	const CAnimState *pIdle = CAnimState::GetIdle();
	for(int i = GhostCount; i >= 1; --i)
	{
		const auto &Sample = m_vGhostSamples[i];
		const float Life = 1.0f - i / (float)(GhostCount + 1);
		const float Alpha = BaseAlpha * Life * Life;
		if(Alpha <= 0.0f)
			continue;

		// Primary afterimage frame.
		RenderTools()->RenderTee(pIdle, &GhostInfo, g_Config.m_ClPlayerDefaultEyes, Sample.m_Dir, Sample.m_Pos, Alpha);
		RenderAfterimageWeapon(Sample, Alpha * 0.95f);
	}
}
