/* Copyright © 2026 BestProject Team */
#include "cloud_input.h"

#include <engine/shared/config.h>

#include <game/client/components/controls.h>
#include <game/client/gameclient.h>

#include <cmath>

bool CCloudInput::IsActive() const
{
	return g_Config.m_BcInputs == BC_INPUTS_CLOUD && Amount() > 0.0f;
}

float CCloudInput::Amount() const
{
	return g_Config.m_BcCloudInputAmount / 100.0f;
}

int CCloudInput::SelfTickOffset() const
{
	return (int)std::ceil(Amount());
}

int CCloudInput::OthersTickOffset() const
{
	return g_Config.m_BcCloudInputOthers && Amount() > 0.0f ? 1 : 0;
}

CNetObj_PlayerInput &CCloudInput::Input(int Dummy)
{
	return m_aInput[Dummy];
}

const CNetObj_PlayerInput &CCloudInput::Input(int Dummy) const
{
	return m_aInput[Dummy];
}

bool CCloudInput::CheckNewInput(const CControls &Controls)
{
	bool NewInput[2] = {};
	for(int Dummy = 0; Dummy < NUM_DUMMIES; Dummy++)
	{
		CNetObj_PlayerInput NextInput = Controls.m_aInputData[Dummy];
		if(Dummy == g_Config.m_ClDummy)
		{
			NextInput.m_Direction = 0;
			if(Controls.m_aInputDirectionLeft[Dummy] && !Controls.m_aInputDirectionRight[Dummy])
				NextInput.m_Direction = -1;
			if(!Controls.m_aInputDirectionLeft[Dummy] && Controls.m_aInputDirectionRight[Dummy])
				NextInput.m_Direction = 1;
		}

		if(m_aInput[Dummy].m_Direction != NextInput.m_Direction)
			NewInput[Dummy] = true;
		if(m_aInput[Dummy].m_Hook != NextInput.m_Hook)
			NewInput[Dummy] = true;
		if(m_aInput[Dummy].m_Fire != NextInput.m_Fire)
			NewInput[Dummy] = true;
		if(m_aInput[Dummy].m_Jump != NextInput.m_Jump)
			NewInput[Dummy] = true;
		if(m_aInput[Dummy].m_NextWeapon != NextInput.m_NextWeapon)
			NewInput[Dummy] = true;
		if(m_aInput[Dummy].m_PrevWeapon != NextInput.m_PrevWeapon)
			NewInput[Dummy] = true;
		if(m_aInput[Dummy].m_WantedWeapon != NextInput.m_WantedWeapon)
			NewInput[Dummy] = true;

		if(Dummy == g_Config.m_ClDummy && g_Config.m_ClSubTickAiming)
		{
			NextInput.m_TargetX = (int)Controls.m_aMousePos[Dummy].x;
			NextInput.m_TargetY = (int)Controls.m_aMousePos[Dummy].y;
		}

		m_aInput[Dummy] = NextInput;
	}

	return NewInput[0] || NewInput[1];
}

void CCloudInput::ApplyOffset(const CGameClient &GameClient, int ClientId, int &Tick, float &Intra) const
{
	if(!IsActive())
		return;
	if(!GameClient.IsFastInputLocalClient(ClientId) && !g_Config.m_BcCloudInputOthers)
		return;

	const float TotalSmoothTick = (Tick - 1) + Intra + Amount();
	Tick = (int)TotalSmoothTick + 1;
	Intra = TotalSmoothTick - (int)TotalSmoothTick;
	if(Intra < 0.0f && Tick > 0)
	{
		Tick -= 1;
		Intra += 1.0f;
	}
}

bool CCloudInput::TryGetPredPos(const CGameClient &GameClient, int ClientId, int Tick, float Intra, vec2 &OutPos) const
{
	if(!IsActive() || Tick <= 0)
		return false;

	const int MaxTick = GameClient.Client()->PredGameTick(g_Config.m_ClDummy) + SelfTickOffset();
	if(GameClient.m_aClients[ClientId].m_aPredTick[(Tick - 1) % 200] != Tick - 1 ||
		GameClient.m_aClients[ClientId].m_aPredTick[Tick % 200] != Tick ||
		GameClient.m_aClients[ClientId].m_aPredTick[Tick % 200] > MaxTick)
		return false;

	OutPos = mix(GameClient.m_aClients[ClientId].m_aPredPos[(Tick - 1) % 200], GameClient.m_aClients[ClientId].m_aPredPos[Tick % 200], Intra);
	return true;
}
