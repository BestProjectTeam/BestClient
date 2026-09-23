/* Copyright © 2026 BestProject Team */
#include "hooktentacle.h"

#include <base/math.h>

#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include "own_tee.h"

#include <algorithm>
#include <cmath>

using FgfGlow::Blend;
using FgfGlow::Whiten;

static constexpr float FADE_TIME = 0.28f;
static constexpr int COIL_POINTS = 26;

static float TentacleWaves() { return g_Config.m_BcHookTentacleWaves / 100.0f; }
static float TentacleSlime() { return g_Config.m_BcHookTentacleSlime / 100.0f; }
static float TentacleThickness() { return g_Config.m_BcHookTentacleThickness / 100.0f; }
static ColorRGBA TentacleColor() { return color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcHookTentacleColor)); }

float CHookTentacle::Wobble(float Along01, float Phase, float Amount)
{
	return (std::sin(Along01 * 7.0f - Phase) * 0.7f + std::sin(Along01 * 13.0f - Phase * 1.7f) * 0.3f) * Amount;
}

void CHookTentacle::OnInit()
{
	m_GlowTexture = FgfGlow::CreateGlowTexture(Graphics());
	m_RingTexture = FgfGlow::CreateRingTexture(Graphics());
	m_DiscTexture = FgfGlow::CreateDiscTexture(Graphics());
	m_BeamTexture = FgfGlow::CreateBeamTexture(Graphics());
}

void CHookTentacle::OnReset()
{
	m_Tracker.Reset();
	for(CGripState &State : m_aStates)
		State = CGripState();
	m_Slime.Clear();
}

void CHookTentacle::AddDroplet(vec2 Pos, vec2 Vel, float Size, float LifeSpan, float Alpha, ColorRGBA Color)
{
	CGlowParticles::CSpark *pSpark = m_Slime.NewSpark();
	if(!pSpark)
		return;
	*pSpark = CGlowParticles::CSpark{};
	pSpark->m_Pos = Pos;
	pSpark->m_Vel = Vel;
	pSpark->m_Gravity = 420.0f;
	pSpark->m_Drag = 0.5f;
	pSpark->m_Stretch = 0.02f;
	pSpark->m_LifeSpan = LifeSpan;
	pSpark->m_StartSize = Size;
	pSpark->m_EndSize = Size * 0.4f;
	pSpark->m_StartAlpha = Alpha;
	pSpark->m_EndAlpha = 0.0f;
	pSpark->m_Color = Color;
}

void CHookTentacle::Grab(int Owner, const CHookGripTracker::CGrip &Grip, float Scale)
{
	CGripState &State = m_aStates[Owner];
	State.m_Squeeze = 1.0f;
	const ColorRGBA Color = TentacleColor();

	const int Count = std::max(0, round_to_int(8.0f * TentacleSlime()));
	for(int i = 0; i < Count; i++)
		AddDroplet(Grip.m_Pos, random_direction() * random_float(60.0f, 180.0f) * Scale,
			random_float(3.0f, 5.5f) * Scale, random_float(0.4f, 0.8f), 0.85f * Grip.m_Alpha, Color);
}

void CHookTentacle::Squeeze(int Owner, const CHookGripTracker::CGrip &Grip, float Scale)
{
	m_aStates[Owner].m_Squeeze = 1.0f;
}

void CHookTentacle::LetGo(int Owner, const CHookGripTracker::CGrip &Grip, float Scale)
{
	const ColorRGBA Color = TentacleColor();
	const int Count = std::max(0, round_to_int(10.0f * TentacleSlime()));
	for(int i = 0; i < Count; i++)
		AddDroplet(Grip.m_Pos + random_direction() * random_float(0.0f, 14.0f) * Scale,
			random_direction() * random_float(60.0f, 200.0f) * Scale,
			random_float(2.5f, 5.0f) * Scale, random_float(0.4f, 0.9f), 0.8f * Grip.m_Alpha, Color);
}

void CHookTentacle::Drip(int Owner, const CHookGripTracker::CGrip &Grip, float Passed, float Scale)
{
	CGripState &State = m_aStates[Owner];
	State.m_SlimeAccumulator += Passed * 3.5f * TentacleSlime();
	const ColorRGBA Color = TentacleColor();
	while(State.m_SlimeAccumulator >= 1.0f)
	{
		State.m_SlimeAccumulator -= 1.0f;
		AddDroplet(Grip.m_Pos + random_direction() * random_float(0.0f, 18.0f) * Scale,
			vec2(random_float(-15.0f, 15.0f), random_float(-5.0f, 25.0f)) * Scale,
			random_float(2.0f, 4.0f) * Scale, random_float(0.5f, 1.0f), 0.7f * Grip.m_Alpha, Color);
	}
}

void CHookTentacle::Draw(float Scale, float Time)
{
	bool AnyGrip = false;
	for(int i = 0; i < MAX_CLIENTS; i++)
		AnyGrip |= m_Tracker.Grip(i).m_Fade > 0.0f;
	if(!AnyGrip && !m_Slime.HasAny())
		return;

	const ColorRGBA Color = TentacleColor();
	const ColorRGBA Dark = Blend(Color, ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f), 0.5f);
	const ColorRGBA Bright = Whiten(Color, 0.4f);
	const float Thickness = TentacleThickness();

	Graphics()->BlendNormal();

	for(int Pass = 0; Pass < 2; Pass++)
	{
		FgfGlow::BeginStrips(Graphics(), m_BeamTexture);
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			const CHookGripTracker::CGrip &Grip = m_Tracker.Grip(i);
			if(Grip.m_Fade <= 0.0f)
				continue;
			const CGripState &State = m_aStates[i];
			const float Alpha = Grip.m_Fade * Grip.m_Alpha;
			const float Entry = std::atan2(Grip.m_Dir.y, Grip.m_Dir.x);
			const float Coil = State.m_Coil;
			const float Turns = mix(0.55f, 1.85f, Coil);
			const float Outer = mix(26.0f, 19.0f, Coil) * (1.0f - 0.12f * State.m_Squeeze) * Scale;
			const float Inner = Outer * 0.32f;
			const float Sway = std::sin(State.m_Phase * 0.9f) * 0.12f;

			vec2 aPoints[COIL_POINTS];
			float aWidths[COIL_POINTS];
			for(int k = 0; k < COIL_POINTS; k++)
			{
				const float u = k / (float)(COIL_POINTS - 1);
				const float Angle = Entry + Sway + u * Turns * 2.0f * pi;
				const float Radius = mix(Outer, Inner, u * u);
				aPoints[k] = Grip.m_Pos + direction(Angle) * Radius;
				aWidths[k] = mix(4.6f, 0.7f, u) * Thickness * Scale;
			}
			for(int k = 0; k < COIL_POINTS; k++)
				aWidths[k] += Pass == 0 ? 1.7f * Scale : 0.0f;
			FgfGlow::DrawStrip(Graphics(), aPoints, aWidths, COIL_POINTS, (Pass == 0 ? Dark : Color).WithAlpha(0.95f * Alpha));
		}
		Graphics()->QuadsEnd();
	}

	if(g_Config.m_BcHookTentacleSuckers)
	{
		Graphics()->TextureSet(m_DiscTexture);
		Graphics()->QuadsBegin();
		Graphics()->QuadsSetRotation(0.0f);
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			const CHookGripTracker::CGrip &Grip = m_Tracker.Grip(i);
			if(Grip.m_Fade <= 0.0f)
				continue;
			const CGripState &State = m_aStates[i];
			const float Alpha = Grip.m_Fade * Grip.m_Alpha;
			const float Entry = std::atan2(Grip.m_Dir.y, Grip.m_Dir.x);
			const float Coil = State.m_Coil;
			const float Turns = mix(0.55f, 1.85f, Coil);
			const float Outer = mix(26.0f, 19.0f, Coil) * (1.0f - 0.12f * State.m_Squeeze) * Scale;
			const float Inner = Outer * 0.32f;
			const float Sway = std::sin(State.m_Phase * 0.9f) * 0.12f;

			for(int k = 0; k < 11; k++)
			{
				const float u = (k + 0.5f) / 11.0f;
				const float Angle = Entry + Sway + u * Turns * 2.0f * pi;
				const float Radius = mix(Outer, Inner, u * u);
				const float Width = mix(4.6f, 0.7f, u) * Thickness * Scale;
				const vec2 At = Grip.m_Pos + direction(Angle) * (Radius - Width * 0.5f);
				const float Size = Width * 1.05f;
				Graphics()->SetColor(Dark.WithAlpha(0.9f * Alpha));
				IGraphics::CQuadItem Outerq(At.x, At.y, Size, Size);
				Graphics()->QuadsDraw(&Outerq, 1);
				Graphics()->SetColor(Blend(Dark, ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f), 0.45f).WithAlpha(0.9f * Alpha));
				IGraphics::CQuadItem Innerq(At.x, At.y, Size * 0.5f, Size * 0.5f);
				Graphics()->QuadsDraw(&Innerq, 1);
			}
		}
		Graphics()->QuadsEnd();
	}

	Graphics()->BlendAdditive();
	Graphics()->TextureSet(m_GlowTexture);
	Graphics()->QuadsBegin();
	Graphics()->QuadsSetRotation(0.0f);
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const CHookGripTracker::CGrip &Grip = m_Tracker.Grip(i);
		if(Grip.m_Fade <= 0.0f)
			continue;
		const float Alpha = Grip.m_Fade * Grip.m_Alpha;
		const vec2 Sheen = Grip.m_Pos - Grip.m_Dir * 7.0f * Scale;
		Graphics()->SetColor(Bright.WithAlpha(0.28f * Alpha));
		IGraphics::CQuadItem Wet(Sheen.x, Sheen.y, 12.0f * Scale, 12.0f * Scale);
		Graphics()->QuadsDraw(&Wet, 1);
	}
	Graphics()->QuadsEnd();

	m_Slime.Draw(Graphics(), m_GlowTexture, m_RingTexture);

	Graphics()->BlendNormal();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CHookTentacle::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(BcFxSuppressed(GameClient()))
		return;

	const float Time = LocalTime();
	const float Passed = std::clamp(Time - m_LastTime, 0.0f, 0.1f) * GameClient()->GetAnimationPlaybackSpeed();
	m_LastTime = Time;

	if(g_Config.m_BcHookStyle != FGF_HOOK_STYLE_TENTACLE)
	{
		OnReset();
		return;
	}

	const float Scale = g_Config.m_BcHookSize / 100.0f;
	m_Tracker.Update(GameClient(), Passed, g_Config.m_BcHookChargeTime / 1000.0f, FADE_TIME,
		[&](int ClientId, CHookGripTracker::EEvent Event, const CHookGripTracker::CGrip &Grip) {
			switch(Event)
			{
			case CHookGripTracker::EEvent::GRAB:
				m_aStates[ClientId] = CGripState();
				m_aStates[ClientId].m_Seed = random_float(0.0f, 500.0f);
				Grab(ClientId, Grip, Scale);
				break;
			case CHookGripTracker::EEvent::CHARGED:
				Squeeze(ClientId, Grip, Scale);
				break;
			case CHookGripTracker::EEvent::RELEASE:
				LetGo(ClientId, Grip, Scale);
				break;
			}
		});

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const CHookGripTracker::CGrip &Grip = m_Tracker.Grip(i);
		CGripState &State = m_aStates[i];
		if(Grip.m_Holding)
			Drip(i, Grip, Passed, Scale);
		const float Target = Grip.m_Holding ? 1.0f : 0.0f;
		State.m_Coil += (Target - State.m_Coil) * std::min(1.0f, Passed * (Grip.m_Holding ? 12.0f : 16.0f));
		State.m_Phase += Passed * 3.0f * TentacleWaves();
		State.m_Squeeze = std::max(0.0f, State.m_Squeeze - Passed * 4.0f);
	}

	m_Slime.Update(Passed);
	Draw(Scale, Time);
}
