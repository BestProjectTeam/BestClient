/* Copyright © 2026 BestProject Team */
#include "cloud_input.h"

#include <engine/shared/config.h>

#include <game/client/components/controls.h>
#include <game/client/gameclient.h>

#include <algorithm>
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

bool CCloudInput::CheckNewInput(CControls &Controls)
{
	bool NewInput[2] = {};
	for(int Dummy = 0; Dummy < NUM_DUMMIES; Dummy++)
	{
		CNetObj_PlayerInput NextInput = Controls.m_aInputData[Dummy];
		if(Dummy == g_Config.m_ClDummy)
		{
			const bool LeftPressed = Controls.m_aInputDirectionLeft[Dummy] != 0;
			const bool RightPressed = Controls.m_aInputDirectionRight[Dummy] != 0;
			NextInput.m_Direction = Controls.ResolveMovementDirection(Dummy, LeftPressed, RightPressed, /*UpdateState=*/false);
		}

		if(Dummy == g_Config.m_ClDummy && g_Config.m_ClSubTickAiming)
		{
			NextInput.m_TargetX = (int)Controls.m_aMousePos[Dummy].x;
			NextInput.m_TargetY = (int)Controls.m_aMousePos[Dummy].y;
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
		if(m_aInput[Dummy].m_TargetX != NextInput.m_TargetX || m_aInput[Dummy].m_TargetY != NextInput.m_TargetY)
			NewInput[Dummy] = true;

		m_aInput[Dummy] = NextInput;
	}

	return NewInput[0] || NewInput[1];
}

void CCloudInput::ApplyOffset(const CGameClient &GameClient, int ClientId, int &Tick, float &Intra) const
{
	if(!IsActive())
		return;
	if(!GameClient.IsFastInputLocalClient(ClientId) && (!g_Config.m_BcCloudInputOthers || !GameClient.m_ReceivedPreInput))
		return;

	// Others are only predicted one extra tick (see OthersTickOffset); never apply the full self amount.
	const float Offset = GameClient.IsFastInputLocalClient(ClientId) ? Amount() : (float)OthersTickOffset();
	if(Offset <= 0.0f)
		return;

	const float TotalSmoothTick = (Tick - 1) + Intra + Offset;
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

	const int TickOffset = GameClient.IsFastInputLocalClient(ClientId) ? SelfTickOffset() : OthersTickOffset();
	const int MaxTick = GameClient.Client()->PredGameTick(g_Config.m_ClDummy) + TickOffset;
	int SampleTick = std::min(Tick, MaxTick);

	// Keep the cloud-input feel stable when the prediction ring has a temporary gap.
	// Use the closest contiguous pair instead of falling back to the regular prediction
	// position. The requested tick is still capped by the correct local/others horizon.
	while(SampleTick > 1)
	{
		if(GameClient.m_aClients[ClientId].m_aPredTick[(SampleTick - 1) % 200] == SampleTick - 1 &&
			GameClient.m_aClients[ClientId].m_aPredTick[SampleTick % 200] == SampleTick)
		{
			const float SampleIntra = SampleTick == Tick ? Intra : 1.0f;
			OutPos = mix(
				GameClient.m_aClients[ClientId].m_aPredPos[(SampleTick - 1) % 200],
				GameClient.m_aClients[ClientId].m_aPredPos[SampleTick % 200],
				SampleIntra);
			return true;
		}
		SampleTick--;
	}
	return false;
}
