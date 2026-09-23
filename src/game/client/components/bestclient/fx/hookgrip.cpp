/* Copyright © 2026 BestProject Team */
#include "hookgrip.h"

#include <base/time.h>

#include <cmath>

#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include "own_tee.h"
#include <game/gamecore.h>

#include <algorithm>

ColorRGBA FgfRainbow(float Hue, float Lightness)
{
	Hue -= std::floor(Hue);
	return color_cast<ColorRGBA>(ColorHSLA(Hue, 1.0f, Lightness, 1.0f));
}

ColorRGBA FgfThemeColor()
{
	switch(g_Config.m_BcHookStyle)
	{
	case FGF_HOOK_STYLE_WHIRLWIND: return color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcHookWindColor));
	case FGF_HOOK_STYLE_BLACK_HOLE: return color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcHookHoleColor));
	case FGF_HOOK_STYLE_MAGIC: return color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcHookMagicColor));
	case FGF_HOOK_STYLE_FIRE: return color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcHookFireColor));
	case FGF_HOOK_STYLE_TROLL: return ColorRGBA(0.95f, 0.95f, 0.95f, 1.0f);
	case FGF_HOOK_STYLE_RAINBOW: return FgfRainbow((float)std::fmod(time_get() / (double)time_freq() * 0.25, 1.0));
	case FGF_HOOK_STYLE_LIGHTNING: return color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcHookBoltColor));
	case FGF_HOOK_STYLE_TENTACLE: return color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcHookTentacleColor));
	default: return color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcFreezeLightningColor));
	}
}

static constexpr float RELEASE_DEBOUNCE = 0.06f;

void CHookGripTracker::Reset()
{
	for(CGrip &Grip : m_aGrips)
		Grip = CGrip();
}

void CHookGripTracker::Update(CGameClient *pGameClient, float Passed, float ChargeTime, float FadeTime, const FEvent &OnEvent)
{
	IClient *pClient = pGameClient->Client();

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CGrip &Grip = m_aGrips[i];
		const CGameClient::CClientData &ClientData = pGameClient->m_aClients[i];
		const CNetObj_Character &Cur = ClientData.m_RenderCur;
		const CNetObj_Character &Prev = ClientData.m_RenderPrev;

		const bool Shown = BcIsOwnTee(pGameClient, i);
		const bool Gripping = Shown && pGameClient->m_Snap.m_aCharacters[i].m_Active &&
				      Cur.m_HookState == HOOK_GRABBED && Cur.m_HookedPlayer == -1;

		if(Gripping)
		{
			const float Intra = ClientData.m_IsPredicted ? pClient->PredIntraGameTick(g_Config.m_ClDummy) : pClient->IntraGameTick(g_Config.m_ClDummy);
			const vec2 HookPos = mix(vec2(Prev.m_HookX, Prev.m_HookY), vec2(Cur.m_HookX, Cur.m_HookY), Intra);
			const vec2 ToTee = ClientData.m_RenderPos - HookPos;

			if(!Grip.m_Holding)
			{
				const bool Flicker = !Grip.m_Released && distance(HookPos, Grip.m_Pos) < 12.0f;
				if(!Flicker)
				{
					if(!Grip.m_Released)
						OnEvent(i, EEvent::RELEASE, Grip);
					const float Fade = Grip.m_Fade;
					Grip = CGrip();
					Grip.m_Fade = Fade;
					Grip.m_Pos = HookPos;
					Grip.m_TeePos = ClientData.m_RenderPos;
					Grip.m_Dir = length(ToTee) > 0.001f ? normalize(ToTee) : vec2(0.0f, 1.0f);
					Grip.m_TeeDistance = length(ToTee);
					Grip.m_Alpha = pGameClient->IsOtherTeam(i) ? g_Config.m_ClShowOthersAlpha / 100.0f : 1.0f;
					OnEvent(i, EEvent::GRAB, Grip);
				}
				Grip.m_Holding = true;
				Grip.m_Released = false;
			}

			Grip.m_Pos = HookPos;
			Grip.m_TeePos = ClientData.m_RenderPos;
			Grip.m_TeeDistance = length(ToTee);
			if(Grip.m_TeeDistance > 0.001f)
				Grip.m_Dir = ToTee / Grip.m_TeeDistance;
			Grip.m_ReleaseTime = 0.0f;
			Grip.m_Fade = 1.0f;
			Grip.m_HoldTime += Passed;
			Grip.m_Charge = std::min(1.0f, Grip.m_HoldTime / ChargeTime);
			if(Grip.m_Charge >= 1.0f && !Grip.m_FullyCharged)
			{
				Grip.m_FullyCharged = true;
				OnEvent(i, EEvent::CHARGED, Grip);
			}
		}
		else if(!Grip.m_Released)
		{
			Grip.m_Holding = false;
			Grip.m_ReleaseTime += Passed;
			if(Grip.m_ReleaseTime >= RELEASE_DEBOUNCE)
			{
				Grip.m_Released = true;
				OnEvent(i, EEvent::RELEASE, Grip);
			}
		}

		if(Grip.m_Released && Grip.m_Fade > 0.0f)
			Grip.m_Fade = std::max(0.0f, Grip.m_Fade - Passed / FadeTime);
	}
}
