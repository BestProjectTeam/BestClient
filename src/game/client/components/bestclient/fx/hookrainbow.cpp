/* Copyright © 2026 BestProject Team */
#include "hookrainbow.h"

#include <base/math.h>

#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include "own_tee.h"

#include <algorithm>
#include <cmath>

static constexpr float FADE_TIME = 0.45f;

static float RainbowSpeed() { return g_Config.m_BcHookRainbowSpeed / 100.0f; }
static float RainbowGlitter() { return g_Config.m_BcHookRainbowGlitter / 100.0f; }

void CHookRainbow::OnInit()
{
	m_GlowTexture = FgfGlow::CreateGlowTexture(Graphics());
	m_RingTexture = FgfGlow::CreateRingTexture(Graphics());
	m_BeamTexture = FgfGlow::CreateBeamTexture(Graphics());
}

void CHookRainbow::OnReset()
{
	m_Tracker.Reset();
	for(CBloomState &State : m_aStates)
		State = CBloomState();
	m_Glitter.Clear();
	m_NumFalling = 0;
}

void CHookRainbow::AddGlitter(vec2 Pos, vec2 Vel, float Hue, float Size, float LifeSpan, float Alpha)
{
	CGlowParticles::CSpark *pSpark = m_Glitter.NewSpark();
	if(!pSpark)
		return;
	*pSpark = CGlowParticles::CSpark{};
	pSpark->m_Pos = Pos;
	pSpark->m_Vel = Vel;
	pSpark->m_Gravity = 40.0f;
	pSpark->m_Drag = 0.12f;
	pSpark->m_Stretch = 0.02f;
	pSpark->m_LifeSpan = LifeSpan;
	pSpark->m_StartSize = Size;
	pSpark->m_EndSize = Size * 0.2f;
	pSpark->m_StartAlpha = Alpha;
	pSpark->m_EndAlpha = 0.0f;
	pSpark->m_Color = FgfGlow::Whiten(FgfRainbow(Hue), 0.25f);
	pSpark->m_ShiftColor = true;
	pSpark->m_EndColor = FgfRainbow(Hue + 0.15f);
}

void CHookRainbow::Bloom(int Owner, const CHookGripTracker::CGrip &Grip, float Scale)
{
	const CBloomState &State = m_aStates[Owner];
	for(int i = 0; i < NUM_PETALS; i++)
	{
		const float Hue = State.m_HueOffset + i / (float)NUM_PETALS;
		m_Glitter.AddRing(Grip.m_Pos, 0.35f + i * 0.05f, 8.0f * Scale, (60.0f + i * 14.0f) * Scale, 0.75f * Grip.m_Alpha, FgfRainbow(Hue));
	}
	const int Count = std::max(0, round_to_int(26.0f * RainbowGlitter()));
	for(int i = 0; i < Count; i++)
	{
		const float Angle = i * 2.0f * pi / std::max(Count, 1) + random_float(-0.15f, 0.15f);
		AddGlitter(Grip.m_Pos, direction(Angle) * random_float(120.0f, 300.0f) * Scale, State.m_HueOffset + i / (float)std::max(Count, 1),
			random_float(5.0f, 9.0f) * Scale, random_float(0.45f, 0.8f), 0.95f * Grip.m_Alpha);
	}
}

void CHookRainbow::Charged(int Owner, const CHookGripTracker::CGrip &Grip, float Scale)
{
	CBloomState &State = m_aStates[Owner];
	State.m_Flash = 1.0f;
	m_Glitter.AddRing(Grip.m_Pos, 0.5f, 20.0f * Scale, 140.0f * Scale, 0.9f * Grip.m_Alpha, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
}

void CHookRainbow::Scatter(int Owner, const CHookGripTracker::CGrip &Grip, float Scale)
{
	const CBloomState &State = m_aStates[Owner];
	const int Streaks = 10 + round_to_int(18.0f * Grip.m_Charge);
	for(int i = 0; i < Streaks && m_NumFalling < MAX_FALLING; i++)
	{
		CFalling &Falling = m_aFalling[m_NumFalling++];
		const float Angle = State.m_Angle + i * 2.0f * pi / Streaks;
		Falling.m_Pos = Grip.m_Pos + direction(Angle) * random_float(8.0f, 26.0f) * Scale;
		Falling.m_Vel = direction(Angle) * random_float(90.0f, mix(160.0f, 320.0f, Grip.m_Charge)) * Scale + vec2(0.0f, -60.0f);
		Falling.m_Hue = State.m_HueOffset + i / (float)Streaks;
		Falling.m_Life = 0.0f;
		Falling.m_LifeSpan = random_float(0.7f, 1.2f);
		Falling.m_Length = random_float(10.0f, 18.0f) * Scale;
		Falling.m_Width = random_float(2.0f, 3.2f) * Scale;
	}
	const int Count = round_to_int((8.0f + 16.0f * Grip.m_Charge) * RainbowGlitter());
	for(int i = 0; i < Count; i++)
		AddGlitter(Grip.m_Pos + random_direction() * random_float(0.0f, 20.0f) * Scale, random_direction() * random_float(40.0f, 180.0f) * Scale,
			random_float(0.0f, 1.0f), random_float(3.0f, 6.0f) * Scale, random_float(0.5f, 1.0f), 0.8f * Grip.m_Alpha);
}

void CHookRainbow::EmitGlitter(int Owner, const CHookGripTracker::CGrip &Grip, float Passed, float Scale)
{
	CBloomState &State = m_aStates[Owner];
	State.m_GlitterAccumulator += Passed * mix(8.0f, 26.0f, Grip.m_Charge) * RainbowGlitter();
	while(State.m_GlitterAccumulator >= 1.0f)
	{
		State.m_GlitterAccumulator -= 1.0f;
		const float Angle = random_angle();
		const float Radius = random_float(10.0f, mix(30.0f, 50.0f, Grip.m_Charge)) * Scale;
		AddGlitter(Grip.m_Pos + direction(Angle) * Radius, direction(Angle) * random_float(10.0f, 40.0f) * Scale + vec2(0.0f, -30.0f * Scale),
			State.m_HueOffset + Angle / (2.0f * pi), random_float(2.5f, 4.5f) * Scale, random_float(0.5f, 0.9f), 0.9f * Grip.m_Alpha);
	}
}

void CHookRainbow::Update(float Passed)
{
	for(int i = 0; i < m_NumFalling;)
	{
		CFalling &Falling = m_aFalling[i];
		Falling.m_Life += Passed;
		if(Falling.m_Life >= Falling.m_LifeSpan)
		{
			Falling = m_aFalling[--m_NumFalling];
			continue;
		}
		Falling.m_Vel.y += 520.0f * Passed;
		Falling.m_Vel *= std::pow(0.4f, Passed);
		Falling.m_Pos += Falling.m_Vel * Passed;
		i++;
	}
	m_Glitter.Update(Passed);
}

void CHookRainbow::DrawArc(vec2 Center, float Radius, float Angle, float Arc, float Width, ColorRGBA Color)
{
	static constexpr int s_Points = 14;
	vec2 aPoints[s_Points];
	float aWidths[s_Points];
	for(int i = 0; i < s_Points; i++)
	{
		const float Along = i / (float)(s_Points - 1);
		aPoints[i] = Center + direction(Angle + Arc * Along) * Radius;
		aWidths[i] = Width * std::sin(Along * pi);
	}
	FgfGlow::DrawStrip(Graphics(), aPoints, aWidths, s_Points, Color);
}

void CHookRainbow::Draw(float Scale, float Time)
{
	bool AnyGrip = false;
	for(int i = 0; i < MAX_CLIENTS; i++)
		AnyGrip |= m_Tracker.Grip(i).m_Fade > 0.0f;
	if(!AnyGrip && m_NumFalling == 0 && !m_Glitter.HasAny())
		return;

	Graphics()->BlendAdditive();

	Graphics()->TextureSet(m_GlowTexture);
	Graphics()->QuadsBegin();
	Graphics()->QuadsSetRotation(0.0f);
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const CHookGripTracker::CGrip &Grip = m_Tracker.Grip(i);
		if(Grip.m_Fade <= 0.0f)
			continue;
		const CBloomState &State = m_aStates[i];
		const float Alpha = Grip.m_Fade * Grip.m_Alpha;
		for(int Blob = 0; Blob < 3; Blob++)
		{
			const float BlobAngle = State.m_Angle * 0.5f + Blob * 2.0f * pi / 3.0f;
			const vec2 P = Grip.m_Pos + direction(BlobAngle) * 10.0f * Scale;
			const float Size = mix(50.0f, 110.0f, Grip.m_Charge) * Scale;
			Graphics()->SetColor(FgfRainbow(State.m_HueOffset + Blob / 3.0f).WithAlpha(0.22f * Alpha));
			IGraphics::CQuadItem Quad(P.x, P.y, Size, Size);
			Graphics()->QuadsDraw(&Quad, 1);
		}
		if(State.m_Flash > 0.0f)
		{
			const float Size = mix(40.0f, 160.0f, 1.0f - State.m_Flash) * Scale;
			Graphics()->SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.8f * State.m_Flash * Alpha));
			IGraphics::CQuadItem Quad(Grip.m_Pos.x, Grip.m_Pos.y, Size, Size);
			Graphics()->QuadsDraw(&Quad, 1);
		}
		Graphics()->SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.9f * Alpha));
		IGraphics::CQuadItem Heart(Grip.m_Pos.x, Grip.m_Pos.y, mix(10.0f, 18.0f, Grip.m_Charge) * Scale, mix(10.0f, 18.0f, Grip.m_Charge) * Scale);
		Graphics()->QuadsDraw(&Heart, 1);
	}
	Graphics()->QuadsEnd();

	m_Glitter.Draw(Graphics(), m_GlowTexture, m_RingTexture);

	FgfGlow::BeginStrips(Graphics(), m_BeamTexture);
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const CHookGripTracker::CGrip &Grip = m_Tracker.Grip(i);
		if(Grip.m_Fade <= 0.0f)
			continue;
		const CBloomState &State = m_aStates[i];
		const float Alpha = Grip.m_Fade * Grip.m_Alpha;
		const float Open = std::min(1.0f, Grip.m_HoldTime / 0.18f);

		if(g_Config.m_BcHookRainbowRays)
		{
			for(int Ray = 0; Ray < NUM_RAYS; Ray++)
			{
				const float RayAngle = -State.m_Angle * 0.6f + Ray * 2.0f * pi / NUM_RAYS;
				const float Pulse = 0.5f + 0.5f * std::sin(Time * 5.0f + Ray * 1.7f);
				const float Length = mix(26.0f, 70.0f, Grip.m_Charge) * (0.7f + 0.3f * Pulse) * Open * Scale;
				const vec2 aPoints[2] = {Grip.m_Pos + direction(RayAngle) * 8.0f * Scale, Grip.m_Pos + direction(RayAngle) * (8.0f * Scale + Length)};
				const float aWidths[2] = {2.2f * Scale, 0.2f * Scale};
				FgfGlow::DrawStrip(Graphics(), aPoints, aWidths, 2, FgfRainbow(State.m_HueOffset + Ray / (float)NUM_RAYS).WithAlpha(0.55f * Alpha * (0.5f + 0.5f * Pulse)));
			}
		}

		const float Outer = mix(18.0f, 30.0f, Grip.m_Charge) * Open * Scale;
		for(int Petal = 0; Petal < NUM_PETALS; Petal++)
		{
			const float Hue = State.m_HueOffset + Petal / (float)NUM_PETALS;
			const float PetalAngle = State.m_Angle + Petal * 2.0f * pi / NUM_PETALS;
			DrawArc(Grip.m_Pos, Outer, PetalAngle, 2.0f * pi / NUM_PETALS * 0.8f, mix(3.0f, 5.0f, Grip.m_Charge) * Scale, FgfRainbow(Hue).WithAlpha(0.9f * Alpha));
			DrawArc(Grip.m_Pos, Outer * 0.55f, -State.m_Angle * 1.6f + PetalAngle, 2.0f * pi / NUM_PETALS * 0.6f, 2.0f * Scale, FgfGlow::Whiten(FgfRainbow(Hue + 0.5f), 0.3f).WithAlpha(0.8f * Alpha));
		}
	}

	for(int i = 0; i < m_NumFalling; i++)
	{
		const CFalling &Falling = m_aFalling[i];
		const float Progress = Falling.m_Life / Falling.m_LifeSpan;
		const float Speed = length(Falling.m_Vel);
		const vec2 Dir = Speed > 0.001f ? Falling.m_Vel / Speed : vec2(0.0f, 1.0f);
		const vec2 aPoints[2] = {Falling.m_Pos - Dir * Falling.m_Length, Falling.m_Pos};
		const float aWidths[2] = {0.3f, Falling.m_Width};
		FgfGlow::DrawStrip(Graphics(), aPoints, aWidths, 2, FgfRainbow(Falling.m_Hue + Progress * 0.3f).WithAlpha(0.9f * (1.0f - Progress)));
	}
	Graphics()->QuadsEnd();

	Graphics()->BlendNormal();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CHookRainbow::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(BcFxSuppressed(GameClient()))
		return;

	const float Time = LocalTime();
	const float Passed = std::clamp(Time - m_LastTime, 0.0f, 0.1f) * GameClient()->GetAnimationPlaybackSpeed();
	m_LastTime = Time;

	if(g_Config.m_BcHookStyle != FGF_HOOK_STYLE_RAINBOW)
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
				m_aStates[ClientId] = CBloomState();
				m_aStates[ClientId].m_Spin = random_float() < 0.5f ? -1.0f : 1.0f;
				m_aStates[ClientId].m_Angle = random_angle();
				m_aStates[ClientId].m_HueOffset = random_float();
				Bloom(ClientId, Grip, Scale);
				break;
			case CHookGripTracker::EEvent::CHARGED:
				Charged(ClientId, Grip, Scale);
				break;
			case CHookGripTracker::EEvent::RELEASE:
				Scatter(ClientId, Grip, Scale);
				break;
			}
		});

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const CHookGripTracker::CGrip &Grip = m_Tracker.Grip(i);
		CBloomState &State = m_aStates[i];
		if(Grip.m_Holding)
			EmitGlitter(i, Grip, Passed, Scale);
		State.m_Angle += Passed * State.m_Spin * mix(1.5f, 5.0f, Grip.m_Charge) * RainbowSpeed();
		State.m_HueOffset += Passed * 0.35f * RainbowSpeed();
		State.m_Flash = std::max(0.0f, State.m_Flash - Passed * 3.0f);
	}

	Update(Passed);
	Draw(Scale, Time);
}
