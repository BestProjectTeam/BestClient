/* Copyright © 2026 BestProject Team */
#include "fgf_ki.h"

#include <base/math.h>

#include <engine/shared/config.h>

#include "hookgrip.h"
#include <game/client/gameclient.h>

#include "own_tee.h"
#include <game/gamecore.h>

#include <algorithm>
#include <cmath>

static float KiAmount() { return g_Config.m_BcGatheredEnergyAmount / 100.0f; }

void CFgfKi::OnInit()
{
	m_GlowTexture = FgfGlow::CreateGlowTexture(Graphics());
	m_RingTexture = FgfGlow::CreateRingTexture(Graphics());
	m_BeamTexture = FgfGlow::CreateBeamTexture(Graphics());
}

void CFgfKi::OnReset()
{
	for(CCharge &Charge : m_aCharges)
		Charge = CCharge();
	m_Burst.Clear();
}

ColorRGBA CFgfKi::Color(const CCharge &Charge, float Offset) const
{
	if(g_Config.m_BcHookStyle == FGF_HOOK_STYLE_RAINBOW)
		return FgfRainbow(Charge.m_Spin * 0.1f + Offset);
	return FgfGlow::Whiten(FgfThemeColor(), 0.15f + 0.45f * Charge.m_Level);
}

void CFgfKi::Spawn(CCharge &Charge, float Scale)
{
	if(Charge.m_NumMotes >= MAX_MOTES)
		return;
	CMote &Mote = Charge.m_aMotes[Charge.m_NumMotes++];
	Mote.m_Angle = random_angle();
	Mote.m_Radius = random_float(110.0f, 190.0f) * Scale;
	Mote.m_TargetRadius = random_float(20.0f, 34.0f) * Scale;
	Mote.m_AngularSpeed = (random_float() < 0.5f ? -1.0f : 1.0f) * random_float(2.0f, 5.0f);
	Mote.m_Size = random_float(3.0f, 6.5f) * Scale;
	Mote.m_Life = 0.0f;
	Mote.m_LifeSpan = random_float(0.55f, 1.0f);
	Mote.m_Hue = random_float();
}

void CFgfKi::Release(int ClientId, vec2 Pos, float Scale)
{
	CCharge &Charge = m_aCharges[ClientId];
	const float Level = Charge.m_Level;
	Charge.m_NumMotes = 0;
	if(!g_Config.m_BcGatheredEnergyBurst || Level < 0.05f)
		return;

	const ColorRGBA Base = Color(Charge, 0.0f);
	m_Burst.AddRing(Pos, 0.35f, 24.0f * Scale, mix(70.0f, 190.0f, Level) * Scale, 0.8f * Level, Base);

	const int Count = std::max(0, round_to_int(mix(8.0f, 30.0f, Level) * KiAmount()));
	for(int i = 0; i < Count; i++)
	{
		CGlowParticles::CSpark *pSpark = m_Burst.NewSpark();
		if(!pSpark)
			break;
		*pSpark = CGlowParticles::CSpark{};
		const float Angle = i * 2.0f * pi / std::max(Count, 1) + random_float(-0.2f, 0.2f);
		pSpark->m_Pos = Pos + direction(Angle) * random_float(6.0f, 20.0f) * Scale;
		pSpark->m_Vel = direction(Angle) * random_float(120.0f, mix(220.0f, 480.0f, Level)) * Scale;
		pSpark->m_Drag = 0.06f;
		pSpark->m_Stretch = 0.04f;
		pSpark->m_LifeSpan = random_float(0.25f, 0.5f);
		pSpark->m_StartSize = random_float(4.0f, 7.0f) * Scale;
		pSpark->m_EndSize = 0.6f * Scale;
		pSpark->m_StartAlpha = 0.9f;
		pSpark->m_EndAlpha = 0.0f;
		pSpark->m_Color = g_Config.m_BcHookStyle == FGF_HOOK_STYLE_RAINBOW ? FgfRainbow(i / (float)std::max(Count, 1)) : FgfGlow::Whiten(Base, 0.4f);
	}
}

void CFgfKi::Draw(float Scale, float Time)
{
	bool Any = false;
	for(const CCharge &Charge : m_aCharges)
		Any |= Charge.m_Level > 0.001f || Charge.m_NumMotes > 0;
	if(!Any && !m_Burst.HasAny())
		return;

	Graphics()->BlendAdditive();

	Graphics()->TextureSet(m_GlowTexture);
	Graphics()->QuadsBegin();
	Graphics()->QuadsSetRotation(0.0f);
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const CCharge &Charge = m_aCharges[i];
		if(Charge.m_Level <= 0.001f && Charge.m_NumMotes == 0)
			continue;
		const vec2 Pos = GameClient()->m_aClients[i].m_RenderPos;
		const float Alpha = GameClient()->IsOtherTeam(i) ? g_Config.m_ClShowOthersAlpha / 100.0f : 1.0f;

		if(g_Config.m_BcGatheredEnergyAura && Charge.m_Level > 0.001f)
		{
			const float Breathe = 1.0f + 0.06f * std::sin(Time * 7.0f + i);
			const float Size = mix(52.0f, 128.0f, Charge.m_Level) * Breathe * Scale;
			Graphics()->SetColor(Color(Charge, 0.0f).WithAlpha(0.2f * Charge.m_Level * Alpha));
			IGraphics::CQuadItem Aura(Pos.x, Pos.y, Size, Size);
			Graphics()->QuadsDraw(&Aura, 1);
		}

		if(Charge.m_Flash > 0.0f)
		{
			const float Size = mix(50.0f, 180.0f, 1.0f - Charge.m_Flash) * Scale;
			Graphics()->SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.55f * Charge.m_Flash * Alpha));
			IGraphics::CQuadItem Quad(Pos.x, Pos.y, Size, Size);
			Graphics()->QuadsDraw(&Quad, 1);
		}

		for(int m = 0; m < Charge.m_NumMotes; m++)
		{
			const CMote &Mote = Charge.m_aMotes[m];
			const float Left = 1.0f - Mote.m_Life / Mote.m_LifeSpan;
			const float Fade = std::min(1.0f, Left * 4.0f);
			const vec2 At = Pos + direction(Mote.m_Angle) * Mote.m_Radius;
			const ColorRGBA MoteColor = g_Config.m_BcHookStyle == FGF_HOOK_STYLE_RAINBOW ? FgfRainbow(Mote.m_Hue) : Color(Charge, 0.0f);
			Graphics()->SetColor(MoteColor.WithAlpha(0.85f * Fade * Alpha));
			const float Size = Mote.m_Size * mix(0.5f, 1.3f, 1.0f - Left);
			IGraphics::CQuadItem Quad(At.x, At.y, Size, Size);
			Graphics()->QuadsDraw(&Quad, 1);
		}
	}
	Graphics()->QuadsEnd();

	if(g_Config.m_BcGatheredEnergyAura)
	{
		Graphics()->TextureSet(m_RingTexture);
		Graphics()->QuadsBegin();
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			const CCharge &Charge = m_aCharges[i];
			if(Charge.m_Level <= 0.01f)
				continue;
			const vec2 Pos = GameClient()->m_aClients[i].m_RenderPos;
			const float Alpha = GameClient()->IsOtherTeam(i) ? g_Config.m_ClShowOthersAlpha / 100.0f : 1.0f;
			const float Size = mix(150.0f, 62.0f, Charge.m_Level) * Scale;
			Graphics()->QuadsSetRotation(Charge.m_Spin);
			Graphics()->SetColor(Color(Charge, 0.25f).WithAlpha(mix(0.1f, 0.55f, Charge.m_Level) * Alpha));
			IGraphics::CQuadItem Ring(Pos.x, Pos.y, Size, Size);
			Graphics()->QuadsDraw(&Ring, 1);
		}
		Graphics()->QuadsSetRotation(0.0f);
		Graphics()->QuadsEnd();
	}

	FgfGlow::BeginStrips(Graphics(), m_BeamTexture);
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const CCharge &Charge = m_aCharges[i];
		if(Charge.m_Level < 0.45f)
			continue;
		const vec2 Pos = GameClient()->m_aClients[i].m_RenderPos;
		const float Alpha = GameClient()->IsOtherTeam(i) ? g_Config.m_ClShowOthersAlpha / 100.0f : 1.0f;
		const float Power = (Charge.m_Level - 0.45f) / 0.55f;
		for(int Streak = 0; Streak < 5; Streak++)
		{
			const float Phase = Time * 3.5f + Streak * 1.27f + i;
			const float Along = Phase - std::floor(Phase);
			const float Side = std::sin(Streak * 2.4f + i) * 22.0f * Scale;
			const float Foot = 26.0f * Scale - Along * 70.0f * Scale;
			const vec2 Bottom = Pos + vec2(Side, Foot);
			const vec2 Top = Bottom + vec2(0.0f, -26.0f * Scale);
			const float aWidths[2] = {0.3f * Scale, 2.6f * Scale * Power};
			const vec2 aPoints[2] = {Bottom, Top};
			FgfGlow::DrawStrip(Graphics(), aPoints, aWidths, 2, Color(Charge, Streak * 0.1f).WithAlpha(0.6f * Power * (1.0f - Along) * Alpha));
		}
	}
	Graphics()->QuadsEnd();

	m_Burst.Draw(Graphics(), m_GlowTexture, m_RingTexture);

	Graphics()->BlendNormal();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CFgfKi::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(BcFxSuppressed(GameClient()))
		return;

	const float Time = LocalTime();
	const float Passed = std::clamp(Time - m_LastTime, 0.0f, 0.1f) * GameClient()->GetAnimationPlaybackSpeed();
	m_LastTime = Time;

	if(!g_Config.m_BcGatheredEnergy)
	{
		OnReset();
		return;
	}

	const float Scale = g_Config.m_BcHookSize / 100.0f;
	const float HookLimit = (float)(SERVER_TICK_SPEED + SERVER_TICK_SPEED / 5);

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CCharge &Charge = m_aCharges[i];
		const CGameClient::CSnapState::CCharacterInfo &Character = GameClient()->m_Snap.m_aCharacters[i];
		const bool Holding = BcIsOwnTee(GameClient(), i) && Character.m_Active &&
				     Character.m_Cur.m_HookState == HOOK_GRABBED && Character.m_Cur.m_HookedPlayer >= 0;

		if(Holding)
		{
			Charge.m_Holding = true;
			Charge.m_HoldTime += Passed;
			const float Ticks = mix((float)Character.m_Prev.m_HookTick, (float)Character.m_Cur.m_HookTick,
				Client()->IntraGameTick(g_Config.m_ClDummy));
			Charge.m_Charge = std::clamp(Ticks / HookLimit, 0.0f, 1.0f);
			if(Charge.m_Charge >= 0.97f && !Charge.m_Peaked)
			{
				Charge.m_Peaked = true;
				Charge.m_Flash = 1.0f;
			}
			Charge.m_Accumulator += Passed * mix(10.0f, 28.0f, Charge.m_Charge) * KiAmount();
			while(Charge.m_Accumulator >= 1.0f)
			{
				Charge.m_Accumulator -= 1.0f;
				Spawn(Charge, Scale);
			}
		}
		else if(Charge.m_Holding)
		{
			Charge.m_Holding = false;
			Release(i, GameClient()->m_aClients[i].m_RenderPos, Scale);
			Charge.m_HoldTime = 0.0f;
			Charge.m_Charge = 0.0f;
			Charge.m_Peaked = false;
		}

		Charge.m_Level += (Charge.m_Charge - Charge.m_Level) * std::min(1.0f, Passed * (Charge.m_Holding ? 12.0f : 9.0f));
		if(Charge.m_Level < 0.001f)
			Charge.m_Level = 0.0f;
		Charge.m_Spin += Passed * mix(1.0f, 3.5f, Charge.m_Level);
		Charge.m_Flash = std::max(0.0f, Charge.m_Flash - Passed * 2.5f);

		for(int m = 0; m < Charge.m_NumMotes;)
		{
			CMote &Mote = Charge.m_aMotes[m];
			Mote.m_Life += Passed;
			if(Mote.m_Life >= Mote.m_LifeSpan)
			{
				Charge.m_aMotes[m] = Charge.m_aMotes[--Charge.m_NumMotes];
				continue;
			}
			const float Progress = Mote.m_Life / Mote.m_LifeSpan;
			Mote.m_Radius = mix(Mote.m_Radius, Mote.m_TargetRadius, std::min(1.0f, Passed * 3.0f));
			Mote.m_Angle += Passed * Mote.m_AngularSpeed * mix(1.0f, 3.0f, Progress);
			m++;
		}
	}

	m_Burst.Update(Passed);
	Draw(Scale, Time);
}
