/* Copyright © 2026 BestProject Team */
#include "hookblackhole.h"

#include <base/math.h>

#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include "own_tee.h"

#include <algorithm>
#include <cmath>

using FgfGlow::Blend;
using FgfGlow::Whiten;

static constexpr float FADE_TIME = 0.3f;
static constexpr float DISK_FLATNESS = 0.28f;

static float HoleSpin() { return g_Config.m_BcHookHoleSpin / 100.0f; }
static float HoleStreaks() { return g_Config.m_BcHookHoleStreaks / 100.0f; }

void CHookBlackHole::OnInit()
{
	m_GlowTexture = FgfGlow::CreateGlowTexture(Graphics());
	m_RingTexture = FgfGlow::CreateRingTexture(Graphics());
	m_DiscTexture = FgfGlow::CreateDiscTexture(Graphics());
	m_BeamTexture = FgfGlow::CreateBeamTexture(Graphics());
}

void CHookBlackHole::OnReset()
{
	m_Tracker.Reset();
	for(CHoleState &State : m_aStates)
		State = CHoleState();
	m_Matter.Clear();
	m_Light.Clear();
}

CHookBlackHole::CPalette CHookBlackHole::Palette()
{
	const ColorHSLA Hsl(g_Config.m_BcHookHoleColor);
	ColorHSLA DeepHsl = Hsl;
	DeepHsl.h = std::fmod(DeepHsl.h + 0.95f, 1.0f);
	DeepHsl.l *= 0.6f;

	CPalette Result;
	Result.m_Hot = color_cast<ColorRGBA>(Hsl);
	Result.m_Deep = color_cast<ColorRGBA>(DeepHsl);
	Result.m_Core = Whiten(Result.m_Hot, 0.75f);
	return Result;
}

float CHookBlackHole::HorizonRadius(const CHookGripTracker::CGrip &Grip, float Scale)
{
	return mix(7.0f, 20.0f, Grip.m_Charge) * Scale * Grip.m_Fade;
}

void CHookBlackHole::Form(const CHookGripTracker::CGrip &Grip, const CPalette &Palette, float Scale)
{
	const float Alpha = Grip.m_Alpha;

	m_Light.AddRing(Grip.m_Pos, 0.35f, 130.0f * Scale, 10.0f * Scale, 0.7f * Alpha, Palette.m_Hot);
	m_Light.AddRing(Grip.m_Pos, 0.45f, 90.0f * Scale, 6.0f * Scale, 0.5f * Alpha, Palette.m_Deep);

	for(int i = 0; i < 20; i++)
	{
		CGlowParticles::CSpark *pSpark = m_Matter.NewSpark();
		if(!pSpark)
			break;
		const float Radius = random_float(70.0f, 110.0f) * Scale;
		pSpark->m_Orbital = true;
		pSpark->m_Center = Grip.m_Pos;
		pSpark->m_LifeSpan = random_float(0.25f, 0.35f);
		pSpark->m_Radius = Radius;
		pSpark->m_RadialSpeed = -Radius / pSpark->m_LifeSpan;
		pSpark->m_Angle = random_angle();
		pSpark->m_AngularSpeed = random_float(1.0f, 2.0f);
		pSpark->m_Pos = Grip.m_Pos + direction(pSpark->m_Angle) * Radius;
		pSpark->m_Stretch = 0.03f;
		pSpark->m_StartSize = 3.0f * Scale;
		pSpark->m_EndSize = 6.0f * Scale;
		pSpark->m_StartAlpha = 0.3f * Alpha;
		pSpark->m_EndAlpha = Alpha;
		pSpark->m_Color = Blend(Palette.m_Hot, Palette.m_Core, random_float());
	}
}

void CHookBlackHole::Flare(const CHookGripTracker::CGrip &Grip, const CPalette &Palette, float Scale)
{
	m_Light.AddRing(Grip.m_Pos, 0.5f, 30.0f * Scale, 140.0f * Scale, 0.6f * Grip.m_Alpha, Palette.m_Core);
	m_Light.AddRing(Grip.m_Pos, 0.8f, 20.0f * Scale, 220.0f * Scale, 0.25f * Grip.m_Alpha, Palette.m_Deep);
}

void CHookBlackHole::Evaporate(int Owner, const CHookGripTracker::CGrip &Grip, const CPalette &Palette, float Scale)
{
	const float Charge = Grip.m_Charge;
	const float Alpha = Grip.m_Alpha;

	if(CGlowParticles::CSpark *pFlash = m_Light.NewSpark())
	{
		pFlash->m_Pos = Grip.m_Pos;
		pFlash->m_LifeSpan = 0.25f;
		pFlash->m_StartSize = mix(50.0f, 100.0f, Charge) * Scale;
		pFlash->m_EndSize = 10.0f * Scale;
		pFlash->m_StartAlpha = Alpha;
		pFlash->m_Color = Palette.m_Core;
	}
	m_Light.AddRing(Grip.m_Pos, 0.45f, 10.0f * Scale, (140.0f + 100.0f * Charge) * Scale, 0.8f * Alpha, Palette.m_Hot);

	const int Sparks = round_to_int(12.0f + 30.0f * Charge);
	for(int i = 0; i < Sparks; i++)
	{
		CGlowParticles::CSpark *pSpark = m_Light.NewSpark();
		if(!pSpark)
			break;
		pSpark->m_Pos = Grip.m_Pos;
		pSpark->m_Vel = random_direction() * random_float(150.0f, mix(300.0f, 600.0f, Charge)) * Scale;
		pSpark->m_Drag = 0.1f;
		pSpark->m_Stretch = 0.03f;
		pSpark->m_LifeSpan = random_float(0.3f, 0.7f);
		pSpark->m_StartSize = random_float(3.0f, 6.0f) * Scale;
		pSpark->m_StartAlpha = Alpha;
		pSpark->m_Color = Blend(Palette.m_Hot, Palette.m_Core, random_float());
	}

	const int Embers = round_to_int(6.0f + 14.0f * Charge);
	for(int i = 0; i < Embers; i++)
	{
		CGlowParticles::CSpark *pSpark = m_Light.NewSpark();
		if(!pSpark)
			break;
		pSpark->m_Orbital = true;
		pSpark->m_Center = Grip.m_Pos;
		pSpark->m_Radius = random_float(4.0f, 14.0f) * Scale;
		pSpark->m_RadialSpeed = random_float(80.0f, mix(140.0f, 260.0f, Charge)) * Scale;
		pSpark->m_Angle = random_angle();
		pSpark->m_AngularSpeed = m_aStates[Owner].m_Spin * random_float(3.0f, 6.0f) * HoleSpin();
		pSpark->m_Pos = Grip.m_Pos + direction(pSpark->m_Angle) * pSpark->m_Radius;
		pSpark->m_LifeSpan = random_float(0.5f, 0.9f);
		pSpark->m_StartSize = random_float(3.0f, 6.0f) * Scale;
		pSpark->m_StartAlpha = 0.9f * Alpha;
		pSpark->m_Color = Blend(Palette.m_Deep, Palette.m_Hot, random_float());
	}
}

void CHookBlackHole::EmitInfall(int Owner, const CHookGripTracker::CGrip &Grip, const CPalette &Palette, float Passed, float Scale)
{
	CHoleState &State = m_aStates[Owner];
	const float Charge = Grip.m_Charge;
	const float Alpha = Grip.m_Alpha;
	const float Horizon = HorizonRadius(Grip, Scale);

	State.m_InfallAccumulator += Passed * mix(10.0f, 45.0f, Charge) * HoleStreaks();
	while(State.m_InfallAccumulator >= 1.0f)
	{
		State.m_InfallAccumulator -= 1.0f;
		CGlowParticles::CSpark *pSpark = m_Matter.NewSpark();
		if(!pSpark)
			break;
		const float Radius = Horizon * random_float(4.0f, 7.0f) + 20.0f * Scale;
		pSpark->m_Orbital = true;
		pSpark->m_Center = Grip.m_Pos;
		pSpark->m_LifeSpan = random_float(0.5f, 0.9f);
		pSpark->m_Radius = Radius;
		pSpark->m_RadialSpeed = -(Radius - Horizon * 0.5f) / pSpark->m_LifeSpan;
		pSpark->m_Angle = random_angle();
		pSpark->m_AngularSpeed = State.m_Spin * random_float(1.0f, 3.0f) * HoleSpin();
		pSpark->m_Pos = Grip.m_Pos + direction(pSpark->m_Angle) * Radius;
		pSpark->m_Stretch = 0.05f;
		pSpark->m_StartSize = 2.0f * Scale;
		pSpark->m_EndSize = 4.5f * Scale;
		pSpark->m_StartAlpha = 0.2f * Alpha;
		pSpark->m_EndAlpha = Alpha;
		pSpark->m_Color = Blend(Palette.m_Deep, Palette.m_Hot, random_float());
	}

	if(Grip.m_TeeDistance < 50.0f)
		return;
	State.m_PullAccumulator += Passed * mix(4.0f, 20.0f, Charge) * HoleStreaks();
	while(State.m_PullAccumulator >= 1.0f)
	{
		State.m_PullAccumulator -= 1.0f;
		CGlowParticles::CSpark *pSpark = m_Matter.NewSpark();
		if(!pSpark)
			break;
		const vec2 From = Grip.m_TeePos + random_direction() * random_float(0.0f, 22.0f);
		pSpark->m_Pos = From;
		pSpark->m_LifeSpan = random_float(0.35f, 0.6f);
		pSpark->m_Vel = (Grip.m_Pos - From) / pSpark->m_LifeSpan;
		pSpark->m_Stretch = 0.04f;
		pSpark->m_StartSize = 2.0f * Scale;
		pSpark->m_EndSize = 3.5f * Scale;
		pSpark->m_StartAlpha = 0.15f * Alpha;
		pSpark->m_EndAlpha = 0.6f * Alpha;
		pSpark->m_Color = Palette.m_Core;
	}
}

void CHookBlackHole::DrawDisk(const CHookGripTracker::CGrip &Grip, const CHoleState &State, const CPalette &Palette, float Scale, float Time, bool FrontHalf)
{
	const float Horizon = HorizonRadius(Grip, Scale);
	const float Near = FrontHalf ? std::clamp((Grip.m_TeeDistance - 16.0f) / 40.0f, 0.3f, 1.0f) : 1.0f;
	const float Alpha = 0.8f * Grip.m_Alpha * mix(0.5f, 1.0f, Grip.m_Charge) * Grip.m_Fade * Near;
	static constexpr int NUM_BANDS = 3;

	for(int Band = 0; Band < NUM_BANDS; Band++)
	{
		const float Depth = Band / (float)(NUM_BANDS - 1);
		const float Radius = Horizon * mix(1.35f, 2.7f, Depth);
		const ColorRGBA Color = Depth < 0.5f ? Blend(Palette.m_Core, Palette.m_Hot, Depth * 2.0f) : Blend(Palette.m_Hot, Palette.m_Deep, Depth * 2.0f - 1.0f);
		const float Size = Horizon * mix(0.9f, 1.3f, Depth) + 3.0f * Scale;

		for(int i = 0; i < DISK_SAMPLES; i++)
		{
			const float Angle = i * 2.0f * pi / DISK_SAMPLES + State.m_DiskAngle * mix(0.6f, 0.35f, Depth);
			const float Depthwise = std::sin(Angle);
			if((Depthwise > 0.0f) != FrontHalf)
				continue;
			const float Beaming = 0.6f + 0.4f * State.m_Spin * std::cos(Angle);
			const float Streams = 0.75f + 0.25f * std::sin(3.0f * Angle + Time * 1.3f + Depth * 2.0f);
			const vec2 Pos = Grip.m_Pos + vec2(std::cos(Angle) * Radius, Depthwise * Radius * DISK_FLATNESS);

			Graphics()->SetColor(Color.WithAlpha(Alpha * Beaming * Streams * mix(0.85f, 0.45f, Depth)));
			IGraphics::CQuadItem Quad(Pos.x, Pos.y, Size * 1.6f, Size);
			Graphics()->QuadsDraw(&Quad, 1);
		}
	}
}

void CHookBlackHole::DrawLensedArc(vec2 Center, float Radius, float StartAngle, float Width, ColorRGBA Color)
{
	vec2 aPoints[ARC_POINTS];
	float aWidths[ARC_POINTS];
	for(int i = 0; i < ARC_POINTS; i++)
	{
		const float Along = i / (float)(ARC_POINTS - 1);
		aPoints[i] = Center + direction(StartAngle + Along * pi) * Radius;
		aWidths[i] = Width * std::sin(Along * pi);
	}
	FgfGlow::DrawStrip(Graphics(), aPoints, aWidths, ARC_POINTS, Color);
}

void CHookBlackHole::Draw(const CPalette &Palette, float Scale, float Time)
{
	bool AnyHole = false;
	for(int i = 0; i < MAX_CLIENTS; i++)
		AnyHole |= m_Tracker.Grip(i).m_Fade > 0.0f;
	if(!AnyHole && !m_Matter.HasAny() && !m_Light.HasAny())
		return;

	const bool Disk = g_Config.m_BcHookHoleDisk;
	Graphics()->QuadsSetRotation(0.0f);

	Graphics()->BlendNormal();
	Graphics()->TextureSet(m_GlowTexture);
	Graphics()->QuadsBegin();
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const CHookGripTracker::CGrip &Grip = m_Tracker.Grip(i);
		if(Grip.m_Fade <= 0.0f)
			continue;
		const float Near = std::clamp((Grip.m_TeeDistance - 16.0f) / 40.0f, 0.3f, 1.0f);
		const float Size = HorizonRadius(Grip, Scale) * 5.0f;
		Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.5f * Near * Grip.m_Alpha);
		IGraphics::CQuadItem Quad(Grip.m_Pos.x, Grip.m_Pos.y, Size, Size);
		Graphics()->QuadsDraw(&Quad, 1);
	}
	Graphics()->QuadsEnd();

	Graphics()->BlendAdditive();
	if(Disk)
	{
		Graphics()->TextureSet(m_GlowTexture);
		Graphics()->QuadsBegin();
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			const CHookGripTracker::CGrip &Grip = m_Tracker.Grip(i);
			if(Grip.m_Fade > 0.0f)
				DrawDisk(Grip, m_aStates[i], Palette, Scale, Time, false);
		}
		Graphics()->QuadsEnd();
	}
	m_Matter.Draw(Graphics(), m_GlowTexture, m_RingTexture);

	Graphics()->BlendNormal();
	Graphics()->TextureSet(m_DiscTexture);
	Graphics()->QuadsBegin();
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const CHookGripTracker::CGrip &Grip = m_Tracker.Grip(i);
		if(Grip.m_Fade <= 0.0f)
			continue;
		const float Near = std::clamp((Grip.m_TeeDistance - 16.0f) / 40.0f, 0.3f, 1.0f);
		const float Size = HorizonRadius(Grip, Scale) * 2.0f;
		Graphics()->SetColor(0.0f, 0.0f, 0.0f, Near * Grip.m_Alpha);
		IGraphics::CQuadItem Quad(Grip.m_Pos.x, Grip.m_Pos.y, Size, Size);
		Graphics()->QuadsDraw(&Quad, 1);
	}
	Graphics()->QuadsEnd();

	Graphics()->BlendAdditive();
	if(Disk)
	{
		Graphics()->TextureSet(m_GlowTexture);
		Graphics()->QuadsBegin();
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			const CHookGripTracker::CGrip &Grip = m_Tracker.Grip(i);
			if(Grip.m_Fade > 0.0f)
				DrawDisk(Grip, m_aStates[i], Palette, Scale, Time, true);
		}
		Graphics()->QuadsEnd();

		FgfGlow::BeginStrips(Graphics(), m_BeamTexture);
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			const CHookGripTracker::CGrip &Grip = m_Tracker.Grip(i);
			if(Grip.m_Fade <= 0.0f)
				continue;
			const float Horizon = HorizonRadius(Grip, Scale);
			const float Alpha = Grip.m_Alpha * mix(0.5f, 1.0f, Grip.m_Charge) * Grip.m_Fade;
			const float Near = std::clamp((Grip.m_TeeDistance - 16.0f) / 40.0f, 0.3f, 1.0f);
			DrawLensedArc(Grip.m_Pos, Horizon * 1.3f, pi, Horizon * 0.55f + 2.0f * Scale, Palette.m_Hot.WithAlpha(0.8f * Alpha));
			DrawLensedArc(Grip.m_Pos, Horizon * 1.18f, 0.0f, Horizon * 0.3f + 1.5f * Scale, Palette.m_Hot.WithAlpha(0.35f * Alpha * Near));
		}
		Graphics()->QuadsEnd();
	}

	Graphics()->TextureSet(m_RingTexture);
	Graphics()->QuadsBegin();
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const CHookGripTracker::CGrip &Grip = m_Tracker.Grip(i);
		if(Grip.m_Fade <= 0.0f)
			continue;
		const float Flicker = 0.85f + 0.15f * std::sin(Time * 11.0f + i);
		const float Size = HorizonRadius(Grip, Scale) * 1.08f * 2.0f / 0.78f;
		Graphics()->SetColor(Palette.m_Core.WithAlpha(0.9f * Flicker * Grip.m_Alpha * Grip.m_Fade));
		IGraphics::CQuadItem Quad(Grip.m_Pos.x, Grip.m_Pos.y, Size, Size);
		Graphics()->QuadsDraw(&Quad, 1);
	}
	Graphics()->QuadsEnd();

	m_Light.Draw(Graphics(), m_GlowTexture, m_RingTexture);

	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	Graphics()->BlendNormal();
}

void CHookBlackHole::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(BcFxSuppressed(GameClient()))
		return;

	const float Time = LocalTime();
	const float Passed = std::clamp(Time - m_LastTime, 0.0f, 0.1f) * GameClient()->GetAnimationPlaybackSpeed();
	m_LastTime = Time;

	if(g_Config.m_BcHookStyle != FGF_HOOK_STYLE_BLACK_HOLE)
	{
		OnReset();
		return;
	}

	const CPalette CurrentPalette = Palette();
	const float Scale = g_Config.m_BcHookSize / 100.0f;

	m_Tracker.Update(GameClient(), Passed, g_Config.m_BcHookChargeTime / 1000.0f, FADE_TIME,
		[&](int ClientId, CHookGripTracker::EEvent Event, const CHookGripTracker::CGrip &Grip) {
			switch(Event)
			{
			case CHookGripTracker::EEvent::GRAB:
				m_aStates[ClientId] = CHoleState();
				m_aStates[ClientId].m_Spin = random_float() < 0.5f ? -1.0f : 1.0f;
				Form(Grip, CurrentPalette, Scale);
				break;
			case CHookGripTracker::EEvent::CHARGED:
				Flare(Grip, CurrentPalette, Scale);
				break;
			case CHookGripTracker::EEvent::RELEASE:
				Evaporate(ClientId, Grip, CurrentPalette, Scale);
				break;
			}
		});

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const CHookGripTracker::CGrip &Grip = m_Tracker.Grip(i);
		if(Grip.m_Holding)
			EmitInfall(i, Grip, CurrentPalette, Passed, Scale);
		m_aStates[i].m_DiskAngle += Passed * m_aStates[i].m_Spin * mix(2.0f, 6.0f, Grip.m_Charge) * HoleSpin();
	}

	m_Matter.Update(Passed);
	m_Light.Update(Passed);
	Draw(CurrentPalette, Scale, Time);
}
