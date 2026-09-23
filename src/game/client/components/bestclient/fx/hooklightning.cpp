/* Copyright © 2026 BestProject Team */
#include "hooklightning.h"

#include <base/math.h>

#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include "own_tee.h"

#include <algorithm>
#include <cmath>

using FgfGlow::Whiten;

static constexpr float FADE_TIME = 0.3f;

static float BoltJitter() { return g_Config.m_BcHookBoltJitter / 100.0f; }
static float BoltSparks() { return g_Config.m_BcHookBoltSparks / 100.0f; }
static ColorRGBA BoltColor() { return color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcHookBoltColor)); }

static float Noise(float Seed)
{
	const float x = std::sin(Seed * 12.9898f + 78.233f) * 43758.5453f;
	return x - std::floor(x);
}

void CHookLightning::Bolt(IGraphics *pGraphics, vec2 Start, vec2 End, float Seed, float Spread, float Width, ColorRGBA Color, int Segments)
{
	Segments = std::clamp(Segments, 2, (int)FgfGlow::MAX_STRIP_POINTS - 1);
	const vec2 Delta = End - Start;
	const float Length = length(Delta);
	if(Length < 0.5f)
		return;
	const vec2 Along = Delta / Length;
	const vec2 Normal(-Along.y, Along.x);

	vec2 aPoints[FgfGlow::MAX_STRIP_POINTS];
	float aWidths[FgfGlow::MAX_STRIP_POINTS];
	const int Count = Segments + 1;
	for(int i = 0; i < Count; i++)
	{
		const float Along01 = i / (float)Segments;
		const float Taper = std::sin(Along01 * pi);
		const float Offset = (Noise(Seed + i * 3.77f) - 0.5f) * 2.0f * Spread * Taper;
		aPoints[i] = Start + Along * (Length * Along01) + Normal * Offset;
		aWidths[i] = Width * mix(0.55f, 1.0f, Taper);
	}
	FgfGlow::DrawStrip(pGraphics, aPoints, aWidths, Count, Color);
}

void CHookLightning::OnInit()
{
	m_GlowTexture = FgfGlow::CreateGlowTexture(Graphics());
	m_RingTexture = FgfGlow::CreateRingTexture(Graphics());
	m_BeamTexture = FgfGlow::CreateBeamTexture(Graphics());
}

void CHookLightning::OnReset()
{
	m_Tracker.Reset();
	for(CBoltState &State : m_aStates)
		State = CBoltState();
	m_Sparks.Clear();
}

void CHookLightning::AddSpark(vec2 Pos, vec2 Vel, float Size, float LifeSpan, float Alpha, ColorRGBA Color)
{
	CGlowParticles::CSpark *pSpark = m_Sparks.NewSpark();
	if(!pSpark)
		return;
	*pSpark = CGlowParticles::CSpark{};
	pSpark->m_Pos = Pos;
	pSpark->m_Vel = Vel;
	pSpark->m_Gravity = 180.0f;
	pSpark->m_Drag = 0.08f;
	pSpark->m_Stretch = 0.05f;
	pSpark->m_LifeSpan = LifeSpan;
	pSpark->m_StartSize = Size;
	pSpark->m_EndSize = Size * 0.15f;
	pSpark->m_StartAlpha = Alpha;
	pSpark->m_EndAlpha = 0.0f;
	pSpark->m_Color = Whiten(Color, 0.6f);
	pSpark->m_ShiftColor = true;
	pSpark->m_EndColor = Color;
}

void CHookLightning::Strike(int Owner, const CHookGripTracker::CGrip &Grip, float Scale)
{
	CBoltState &State = m_aStates[Owner];
	State.m_Flash = 1.0f;
	const ColorRGBA Color = BoltColor();
	m_Sparks.AddRing(Grip.m_Pos, 0.28f, 6.0f * Scale, 90.0f * Scale, 0.85f * Grip.m_Alpha, Whiten(Color, 0.5f));

	const int Count = std::max(0, round_to_int(22.0f * BoltSparks()));
	for(int i = 0; i < Count; i++)
	{
		const float Angle = random_angle();
		const vec2 Dir = normalize(direction(Angle) + Grip.m_Dir * 0.8f);
		AddSpark(Grip.m_Pos, Dir * random_float(140.0f, 420.0f) * Scale, random_float(3.5f, 6.0f) * Scale,
			random_float(0.25f, 0.55f), 0.95f * Grip.m_Alpha, Color);
	}
}

void CHookLightning::Charged(int Owner, const CHookGripTracker::CGrip &Grip, float Scale)
{
	CBoltState &State = m_aStates[Owner];
	State.m_Flash = 1.0f;
	m_Sparks.AddRing(Grip.m_Pos, 0.45f, 16.0f * Scale, 150.0f * Scale, 0.9f * Grip.m_Alpha, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
}

void CHookLightning::Snap(int Owner, const CHookGripTracker::CGrip &Grip, float Scale)
{
	const ColorRGBA Color = BoltColor();
	const int Count = std::max(0, round_to_int((10.0f + 22.0f * Grip.m_Charge) * BoltSparks()));
	for(int i = 0; i < Count; i++)
		AddSpark(Grip.m_Pos + random_direction() * random_float(0.0f, 14.0f) * Scale,
			random_direction() * random_float(90.0f, mix(220.0f, 480.0f, Grip.m_Charge)) * Scale,
			random_float(3.0f, 5.5f) * Scale, random_float(0.25f, 0.5f), 0.9f * Grip.m_Alpha, Color);
	m_Sparks.AddRing(Grip.m_Pos, 0.3f, 10.0f * Scale, mix(60.0f, 130.0f, Grip.m_Charge) * Scale, 0.6f * Grip.m_Alpha, Color);
}

void CHookLightning::EmitSparks(int Owner, const CHookGripTracker::CGrip &Grip, float Passed, float Scale)
{
	CBoltState &State = m_aStates[Owner];
	State.m_SparkAccumulator += Passed * mix(6.0f, 22.0f, Grip.m_Charge) * BoltSparks();
	const ColorRGBA Color = BoltColor();
	while(State.m_SparkAccumulator >= 1.0f)
	{
		State.m_SparkAccumulator -= 1.0f;
		const vec2 Dir = normalize(random_direction() * 0.7f + Grip.m_Dir);
		AddSpark(Grip.m_Pos + random_direction() * random_float(0.0f, 10.0f) * Scale,
			Dir * random_float(40.0f, 160.0f) * Scale, random_float(2.0f, 4.0f) * Scale,
			random_float(0.2f, 0.45f), 0.85f * Grip.m_Alpha, Color);
	}
}

void CHookLightning::Draw(float Scale, float Time)
{
	bool AnyGrip = false;
	for(int i = 0; i < MAX_CLIENTS; i++)
		AnyGrip |= m_Tracker.Grip(i).m_Fade > 0.0f;
	if(!AnyGrip && !m_Sparks.HasAny())
		return;

	const ColorRGBA Color = BoltColor();
	const ColorRGBA Hot = Whiten(Color, 0.75f);

	Graphics()->BlendAdditive();

	Graphics()->TextureSet(m_GlowTexture);
	Graphics()->QuadsBegin();
	Graphics()->QuadsSetRotation(0.0f);
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const CHookGripTracker::CGrip &Grip = m_Tracker.Grip(i);
		if(Grip.m_Fade <= 0.0f)
			continue;
		const CBoltState &State = m_aStates[i];
		const float Alpha = Grip.m_Fade * Grip.m_Alpha;
		const float Corona = mix(46.0f, 104.0f, Grip.m_Charge) * Scale * State.m_Flicker;
		Graphics()->SetColor(Color.WithAlpha(0.3f * Alpha));
		IGraphics::CQuadItem Halo(Grip.m_Pos.x, Grip.m_Pos.y, Corona, Corona);
		Graphics()->QuadsDraw(&Halo, 1);

		if(State.m_Flash > 0.0f)
		{
			const float Size = mix(30.0f, 170.0f, 1.0f - State.m_Flash) * Scale;
			Graphics()->SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.85f * State.m_Flash * Alpha));
			IGraphics::CQuadItem Quad(Grip.m_Pos.x, Grip.m_Pos.y, Size, Size);
			Graphics()->QuadsDraw(&Quad, 1);
		}

		const float Core = mix(8.0f, 16.0f, Grip.m_Charge) * Scale * State.m_Flicker;
		Graphics()->SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.95f * Alpha));
		IGraphics::CQuadItem Quad(Grip.m_Pos.x, Grip.m_Pos.y, Core, Core);
		Graphics()->QuadsDraw(&Quad, 1);
	}
	Graphics()->QuadsEnd();

	m_Sparks.Draw(Graphics(), m_GlowTexture, m_RingTexture);

	FgfGlow::BeginStrips(Graphics(), m_BeamTexture);
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const CHookGripTracker::CGrip &Grip = m_Tracker.Grip(i);
		if(Grip.m_Fade <= 0.0f || !g_Config.m_BcHookBoltArc)
			continue;
		const CBoltState &State = m_aStates[i];
		const float Alpha = Grip.m_Fade * Grip.m_Alpha;
		const vec2 Wall(-Grip.m_Dir.y, Grip.m_Dir.x);
		const float Step = std::floor(Time * 20.0f);
		for(int Arc = 0; Arc < NUM_ARCS; Arc++)
		{
			const float Seed = State.m_Seed + Arc * 41.7f + Step * 7.13f;
			if(Noise(Seed) < 0.35f)
				continue;
			const float Side = Noise(Seed + 1.0f) < 0.5f ? -1.0f : 1.0f;
			const float Reach = mix(18.0f, 54.0f, Grip.m_Charge) * mix(0.5f, 1.0f, Noise(Seed + 2.0f)) * Scale;
			const vec2 End = Grip.m_Pos + Wall * Side * Reach + Grip.m_Dir * (Noise(Seed + 3.0f) - 0.35f) * 16.0f * Scale;
			Bolt(Graphics(), Grip.m_Pos, End, Seed, 7.0f * Scale * BoltJitter(), 1.4f * Scale,
				Hot.WithAlpha(0.75f * Alpha * mix(0.5f, 1.0f, Noise(Seed + 4.0f))), 6);
		}
	}
	Graphics()->QuadsEnd();

	Graphics()->BlendNormal();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CHookLightning::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(BcFxSuppressed(GameClient()))
		return;

	const float Time = LocalTime();
	const float Passed = std::clamp(Time - m_LastTime, 0.0f, 0.1f) * GameClient()->GetAnimationPlaybackSpeed();
	m_LastTime = Time;

	if(g_Config.m_BcHookStyle != FGF_HOOK_STYLE_LIGHTNING)
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
				m_aStates[ClientId] = CBoltState();
				m_aStates[ClientId].m_Seed = random_float(0.0f, 500.0f);
				Strike(ClientId, Grip, Scale);
				break;
			case CHookGripTracker::EEvent::CHARGED:
				Charged(ClientId, Grip, Scale);
				break;
			case CHookGripTracker::EEvent::RELEASE:
				Snap(ClientId, Grip, Scale);
				break;
			}
		});

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const CHookGripTracker::CGrip &Grip = m_Tracker.Grip(i);
		CBoltState &State = m_aStates[i];
		if(Grip.m_Holding)
			EmitSparks(i, Grip, Passed, Scale);
		State.m_Flicker = mix(0.8f, 1.2f, Noise(State.m_Seed + std::floor(Time * 28.0f)));
		State.m_Flash = std::max(0.0f, State.m_Flash - Passed * 4.0f);
	}

	m_Sparks.Update(Passed);
	Draw(Scale, Time);
}
