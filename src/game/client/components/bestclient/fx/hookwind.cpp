/* Copyright © 2026 BestProject Team */
#include "hookwind.h"

#include <base/math.h>

#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include "own_tee.h"
#include <game/gamecore.h>

#include <algorithm>
#include <cmath>

static constexpr float FADE_TIME = 0.4f;
static constexpr int MAX_LEAVES_PER_GRIP = 12;
static constexpr float CAPTURED_LEAF_LIFE = 30.0f;

static const ColorRGBA DUST_COLOR(0.72f, 0.66f, 0.56f, 1.0f);

static float WindDensity() { return g_Config.m_BcHookWindDensity / 100.0f; }
static float WindSpin() { return g_Config.m_BcHookWindSpin / 100.0f; }
static float WindLeaves() { return g_Config.m_BcHookWindLeafAmount / 100.0f; }
static float WindDust() { return g_Config.m_BcHookWindDust / 100.0f; }

static int RandomRound(float Amount)
{
	const int Whole = (int)Amount;
	return Whole + (random_float() < Amount - Whole ? 1 : 0);
}

static ColorRGBA RandomLeafColor()
{
	static const ColorRGBA s_aLeafColors[] = {
		ColorRGBA(0.40f, 0.70f, 0.25f, 1.0f),
		ColorRGBA(0.55f, 0.80f, 0.28f, 1.0f),
		ColorRGBA(0.30f, 0.58f, 0.22f, 1.0f),
		ColorRGBA(0.76f, 0.78f, 0.26f, 1.0f),
		ColorRGBA(0.90f, 0.58f, 0.22f, 1.0f),
	};
	const int Index = std::min<int>(std::size(s_aLeafColors) - 1, (int)(std::pow(random_float(), 1.6f) * std::size(s_aLeafColors)));
	return s_aLeafColors[Index];
}

void CHookWind::OnInit()
{
	m_GlowTexture = FgfGlow::CreateGlowTexture(Graphics());
	m_BeamTexture = FgfGlow::CreateBeamTexture(Graphics());
	m_LeafTexture = FgfGlow::CreateLeafTexture(Graphics());
}

void CHookWind::OnReset()
{
	m_Tracker.Reset();
	for(CWindState &State : m_aStates)
		State = CWindState();
	m_NumGusts = 0;
	m_NumLeaves = 0;
	m_Dust.Clear();
}

void CHookWind::AddGust(vec2 Center, float Radius, float RadialSpeed, float AngularSpeed, float Arc, float Width, float LifeSpan, float Alpha, float Angle)
{
	if(m_NumGusts >= MAX_GUSTS)
		return;
	CGust &Gust = m_aGusts[m_NumGusts++];
	Gust.m_Center = Center;
	Gust.m_Radius = Radius;
	Gust.m_RadialSpeed = RadialSpeed;
	Gust.m_Angle = Angle;
	Gust.m_AngularSpeed = AngularSpeed * WindSpin();
	Gust.m_Arc = Arc;
	Gust.m_Width = Width;
	Gust.m_Life = 0.0f;
	Gust.m_LifeSpan = LifeSpan;
	Gust.m_Alpha = Alpha;
}

CHookWind::CLeaf *CHookWind::AddLeaf(int Owner, vec2 Pos, float Scale)
{
	if(m_NumLeaves >= MAX_LEAVES)
		return nullptr;
	CLeaf &Leaf = m_aLeaves[m_NumLeaves++];
	Leaf = CLeaf{};
	Leaf.m_Owner = Owner;
	Leaf.m_Pos = Pos;
	Leaf.m_Rotation = random_angle();
	Leaf.m_RotationSpeed = random_float(-6.0f, 6.0f);
	Leaf.m_FlutterPhase = random_angle();
	Leaf.m_Size = random_float(13.0f, 19.0f) * Scale;
	Leaf.m_LifeSpan = CAPTURED_LEAF_LIFE;
	Leaf.m_Color = RandomLeafColor();
	return &Leaf;
}

void CHookWind::Burst(int Owner, const CHookGripTracker::CGrip &Grip, float Scale)
{
	const float Alpha = Grip.m_Alpha;
	const float Spin = m_aStates[Owner].m_Spin;

	const int Lines = std::max(1, round_to_int(9.0f * WindDensity()));
	for(int i = 0; i < Lines; i++)
	{
		AddGust(Grip.m_Pos, random_float(8.0f, 16.0f) * Scale, random_float(220.0f, 320.0f) * Scale, Spin * random_float(2.0f, 3.5f),
			random_float(0.5f, 0.9f), random_float(2.5f, 3.8f) * Scale, random_float(0.35f, 0.5f), 0.75f * Alpha,
			i * 2.0f * pi / Lines + random_float(-0.2f, 0.2f));
	}

	const float BaseAngle = angle(Grip.m_Dir);
	const int Dust = RandomRound(14.0f * WindDust());
	for(int i = 0; i < Dust; i++)
	{
		CGlowParticles::CSpark *pDust = m_Dust.NewSpark();
		if(!pDust)
			break;
		pDust->m_Pos = Grip.m_Pos + random_direction() * random_float(0.0f, 6.0f) * Scale;
		pDust->m_Vel = direction(BaseAngle + random_float(-1.3f, 1.3f)) * random_float(60.0f, 240.0f) * Scale;
		pDust->m_Gravity = 60.0f;
		pDust->m_Drag = 0.05f;
		pDust->m_LifeSpan = random_float(0.5f, 0.9f);
		pDust->m_StartSize = random_float(10.0f, 20.0f) * Scale;
		pDust->m_EndSize = random_float(24.0f, 36.0f) * Scale;
		pDust->m_StartAlpha = 0.45f * Alpha;
		pDust->m_Color = DUST_COLOR;
	}

	const int Leaves = RandomRound(3.0f * WindLeaves());
	for(int i = 0; i < Leaves; i++)
	{
		CLeaf *pLeaf = AddLeaf(Owner, Grip.m_Pos + random_direction() * random_float(4.0f, 12.0f) * Scale, Scale);
		if(!pLeaf)
			break;
		pLeaf->m_Radius = random_float(14.0f, 30.0f) * Scale;
		pLeaf->m_Angle = random_angle();
		pLeaf->m_RadialSpeed = random_float(-10.0f, 10.0f) * Scale;
	}
}

void CHookWind::GustRing(int Owner, const CHookGripTracker::CGrip &Grip, float Scale)
{
	const float Spin = m_aStates[Owner].m_Spin;
	const int Lines = std::max(1, round_to_int(7.0f * WindDensity()));
	for(int i = 0; i < Lines; i++)
	{
		AddGust(Grip.m_Pos, 20.0f * Scale, random_float(150.0f, 210.0f) * Scale, Spin * random_float(4.0f, 6.0f),
			random_float(0.9f, 1.3f), 3.4f * Scale, random_float(0.4f, 0.55f), 0.7f * Grip.m_Alpha,
			i * 2.0f * pi / Lines);
	}
}

void CHookWind::Disperse(int Owner, const CHookGripTracker::CGrip &Grip, float Scale)
{
	const float Spin = m_aStates[Owner].m_Spin;
	const float Charge = Grip.m_Charge;
	const float Alpha = Grip.m_Alpha;

	const int Lines = round_to_int((4.0f + Charge * 9.0f) * WindDensity());
	for(int i = 0; i < Lines; i++)
	{
		AddGust(Grip.m_Pos, mix(10.0f, 30.0f, Charge) * random_float(0.6f, 1.2f) * Scale, random_float(70.0f, mix(110.0f, 240.0f, Charge)) * Scale,
			Spin * mix(3.0f, 9.0f, Charge) * random_float(0.7f, 1.0f), random_float(1.0f, 1.8f), mix(2.0f, 3.6f, Charge) * Scale,
			random_float(0.4f, 0.7f), 0.6f * Alpha, random_angle());
	}

	const int Dust = RandomRound((4.0f + Charge * 14.0f) * WindDust());
	for(int i = 0; i < Dust; i++)
	{
		CGlowParticles::CSpark *pDust = m_Dust.NewSpark();
		if(!pDust)
			break;
		pDust->m_Pos = Grip.m_Pos + random_direction() * random_float(0.0f, 20.0f) * Scale;
		pDust->m_Vel = random_direction() * random_float(40.0f, mix(90.0f, 220.0f, Charge)) * Scale;
		pDust->m_Gravity = 40.0f;
		pDust->m_Drag = 0.1f;
		pDust->m_LifeSpan = random_float(0.6f, 1.1f);
		pDust->m_StartSize = random_float(8.0f, 16.0f) * Scale;
		pDust->m_EndSize = random_float(20.0f, 34.0f) * Scale;
		pDust->m_StartAlpha = 0.35f * Alpha;
		pDust->m_Color = DUST_COLOR;
	}

	for(int i = 0; i < m_NumLeaves; i++)
	{
		CLeaf &Leaf = m_aLeaves[i];
		if(Leaf.m_Owner != Owner)
			continue;
		const vec2 Outward = direction(Leaf.m_Angle);
		const vec2 Tangent = vec2(-Outward.y, Outward.x) * Spin;
		Leaf.m_Owner = -1;
		Leaf.m_Vel = (Tangent * random_float(160.0f, 320.0f) + Outward * random_float(60.0f, 160.0f)) * mix(0.6f, 1.2f, Charge) * Scale;
		Leaf.m_RotationSpeed = random_float(-9.0f, 9.0f);
		Leaf.m_LifeSpan = Leaf.m_Life + random_float(1.2f, 2.0f);
	}
}

void CHookWind::EmitVortex(int Owner, const CHookGripTracker::CGrip &Grip, float Passed, float Scale)
{
	CWindState &State = m_aStates[Owner];
	const float Spin = State.m_Spin;
	const float Charge = Grip.m_Charge;
	const float Alpha = Grip.m_Alpha;

	State.m_GustAccumulator += Passed * mix(7.0f, 24.0f, Charge) * WindDensity();
	while(State.m_GustAccumulator >= 1.0f)
	{
		State.m_GustAccumulator -= 1.0f;
		AddGust(Grip.m_Pos, random_float(mix(22.0f, 34.0f, Charge), mix(45.0f, 85.0f, Charge)) * Scale, -mix(15.0f, 50.0f, Charge) * Scale,
			Spin * mix(4.0f, 11.0f, Charge) * random_float(0.8f, 1.1f), random_float(0.9f, 1.8f), mix(2.0f, 3.8f, Charge) * Scale,
			random_float(0.45f, 0.75f), mix(0.45f, 0.75f, Charge) * Alpha, random_angle());
	}

	State.m_DustAccumulator += Passed * mix(10.0f, 36.0f, Charge) * WindDust();
	while(State.m_DustAccumulator >= 1.0f)
	{
		State.m_DustAccumulator -= 1.0f;
		CGlowParticles::CSpark *pDust = m_Dust.NewSpark();
		if(!pDust)
			break;
		const float Radius = random_float(40.0f, mix(70.0f, 110.0f, Charge)) * Scale;
		pDust->m_Orbital = true;
		pDust->m_Center = Grip.m_Pos;
		pDust->m_LifeSpan = random_float(0.6f, 1.0f);
		pDust->m_Radius = Radius;
		pDust->m_RadialSpeed = -(Radius - 6.0f * Scale) / pDust->m_LifeSpan;
		pDust->m_Angle = random_angle();
		pDust->m_AngularSpeed = Spin * mix(2.5f, 6.0f, Charge) * WindSpin();
		pDust->m_Pos = Grip.m_Pos + direction(pDust->m_Angle) * Radius;
		pDust->m_StartSize = random_float(2.0f, 3.5f) * Scale;
		pDust->m_EndSize = random_float(3.5f, 5.5f) * Scale;
		pDust->m_StartAlpha = 0.25f * Alpha;
		pDust->m_EndAlpha = 0.6f * Alpha;
		pDust->m_Color = FgfGlow::Blend(DUST_COLOR, ColorRGBA(0.45f, 0.4f, 0.33f, 1.0f), random_float());
	}

	if(WindLeaves() <= 0.0f)
		return;

	State.m_LeafAccumulator += Passed * mix(1.2f, 5.0f, Charge) * WindLeaves();
	while(State.m_LeafAccumulator >= 1.0f)
	{
		State.m_LeafAccumulator -= 1.0f;
		int Carried = 0;
		for(int i = 0; i < m_NumLeaves; i++)
			Carried += m_aLeaves[i].m_Owner == Owner;
		if(Carried >= std::max(1, round_to_int(MAX_LEAVES_PER_GRIP * WindLeaves())))
			break;
		const float Angle = random_angle();
		const float Radius = random_float(70.0f, 110.0f) * Scale;
		CLeaf *pLeaf = AddLeaf(Owner, Grip.m_Pos + direction(Angle) * Radius, Scale);
		if(!pLeaf)
			break;
		pLeaf->m_Angle = Angle;
		pLeaf->m_Radius = Radius;
		pLeaf->m_RadialSpeed = -random_float(50.0f, 90.0f) * Scale;
	}
}

void CHookWind::Update(float Passed)
{
	for(int i = 0; i < m_NumGusts;)
	{
		CGust &Gust = m_aGusts[i];
		Gust.m_Life += Passed;
		if(Gust.m_Life >= Gust.m_LifeSpan)
		{
			Gust = m_aGusts[--m_NumGusts];
			continue;
		}
		const float Spin = Gust.m_AngularSpeed * (1.0f + 20.0f / (Gust.m_Radius + 10.0f));
		Gust.m_Radius = std::max(2.0f, Gust.m_Radius + Gust.m_RadialSpeed * Passed);
		Gust.m_Angle += Spin * Passed;
		i++;
	}

	for(int i = 0; i < m_NumLeaves;)
	{
		CLeaf &Leaf = m_aLeaves[i];
		Leaf.m_Life += Passed;
		if(Leaf.m_Life >= Leaf.m_LifeSpan)
		{
			Leaf = m_aLeaves[--m_NumLeaves];
			continue;
		}

		Leaf.m_FlutterPhase += Passed * 7.0f;
		if(Leaf.m_Owner >= 0)
		{
			const CHookGripTracker::CGrip &Grip = m_Tracker.Grip(Leaf.m_Owner);
			const float Spin = m_aStates[Leaf.m_Owner].m_Spin;
			const float Scale = g_Config.m_BcHookSize / 100.0f;
			const float Orbit = mix(26.0f, 16.0f, Grip.m_Charge) * Scale;
			Leaf.m_Radius = std::max(Orbit, Leaf.m_Radius + Leaf.m_RadialSpeed * Passed);
			const float OrbitSpin = Spin * mix(3.0f, 9.0f, Grip.m_Charge) * WindSpin() * (1.0f + 15.0f / (Leaf.m_Radius + 10.0f));
			Leaf.m_Angle += OrbitSpin * Passed;
			Leaf.m_Pos = Grip.m_Pos + direction(Leaf.m_Angle) * Leaf.m_Radius + vec2(0.0f, std::sin(Leaf.m_FlutterPhase) * 2.0f * Scale);
			Leaf.m_Rotation += Leaf.m_RotationSpeed * Passed;
		}
		else
		{
			Leaf.m_Vel.y += 260.0f * Passed;
			Leaf.m_Vel *= std::pow(0.3f, Passed);
			Leaf.m_Vel.x += std::sin(Leaf.m_FlutterPhase * 0.6f) * 90.0f * Passed;
			Leaf.m_Pos += Leaf.m_Vel * Passed;
			Leaf.m_Rotation += Leaf.m_RotationSpeed * Passed;
			Leaf.m_RotationSpeed *= std::pow(0.5f, Passed);
		}
		i++;
	}

	m_Dust.Update(Passed);
}

void CHookWind::DrawArc(vec2 Center, float Radius, float Angle, float Arc, float Width, float Flare, float Spin, ColorRGBA Color)
{
	vec2 aPoints[GUST_POINTS];
	float aWidths[GUST_POINTS];
	for(int i = 0; i < GUST_POINTS; i++)
	{
		const float Along = i / (float)(GUST_POINTS - 1);
		const float PointRadius = Radius * (1.0f + Flare * (1.0f - Along));
		aPoints[i] = Center + direction(Angle - Spin * Arc * (1.0f - Along)) * PointRadius;
		aWidths[i] = Width * std::pow(Along, 0.7f) * std::pow(1.0f - Along, 0.25f) * 1.6f;
	}
	FgfGlow::DrawStrip(Graphics(), aPoints, aWidths, GUST_POINTS, Color);
}

void CHookWind::Draw(ColorRGBA Tint, float Scale)
{
	if(m_NumGusts == 0 && m_NumLeaves == 0 && !m_Dust.HasAny())
	{
		bool AnyGrip = false;
		for(int i = 0; i < MAX_CLIENTS; i++)
			AnyGrip |= m_Tracker.Grip(i).m_Fade > 0.0f;
		if(!AnyGrip)
			return;
	}

	const float Opacity = g_Config.m_BcHookWindOpacity / 100.0f;
	const float Thickness = g_Config.m_BcHookWindThickness / 100.0f;

	Graphics()->BlendNormal();

	Graphics()->TextureSet(m_GlowTexture);
	Graphics()->QuadsBegin();
	Graphics()->QuadsSetRotation(0.0f);
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const CHookGripTracker::CGrip &Grip = m_Tracker.Grip(i);
		if(Grip.m_Fade <= 0.0f)
			continue;
		const float Size = mix(40.0f, 120.0f, Grip.m_Charge) * Scale * (0.5f + 0.5f * Grip.m_Fade);
		Graphics()->SetColor(Tint.WithAlpha(0.14f * Grip.m_Charge * Grip.m_Fade * Grip.m_Alpha * Opacity));
		IGraphics::CQuadItem Quad(Grip.m_Pos.x, Grip.m_Pos.y, Size, Size);
		Graphics()->QuadsDraw(&Quad, 1);
	}
	Graphics()->QuadsEnd();

	m_Dust.Draw(Graphics(), m_GlowTexture, m_GlowTexture);

	FgfGlow::BeginStrips(Graphics(), m_BeamTexture);
	for(int i = 0; i < m_NumGusts; i++)
	{
		const CGust &Gust = m_aGusts[i];
		const float Progress = Gust.m_Life / Gust.m_LifeSpan;
		const float Alpha = Gust.m_Alpha * std::sin(Progress * pi);
		const float Spin = Gust.m_AngularSpeed < 0.0f ? -1.0f : 1.0f;
		const float Flare = Gust.m_RadialSpeed < 0.0f ? 0.35f : -0.2f;
		DrawArc(Gust.m_Center, Gust.m_Radius, Gust.m_Angle, Gust.m_Arc, Gust.m_Width * Thickness, Flare, Spin, Tint.WithAlpha(Alpha * Opacity));
	}
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const CHookGripTracker::CGrip &Grip = m_Tracker.Grip(i);
		const CWindState &State = m_aStates[i];
		if(Grip.m_Fade <= 0.0f || !g_Config.m_BcHookWindEye)
			continue;
		const float Radius = mix(6.0f, 16.0f, Grip.m_Charge) * Scale * (0.3f + 0.7f * Grip.m_Fade);
		for(int Arc = 0; Arc < NUM_EYE_ARCS; Arc++)
		{
			DrawArc(Grip.m_Pos, Radius, State.m_EyeAngle + Arc * 2.0f * pi / NUM_EYE_ARCS, 1.5f, mix(1.6f, 2.8f, Grip.m_Charge) * Scale * Thickness, 0.5f, State.m_Spin,
				Tint.WithAlpha(mix(0.4f, 0.8f, Grip.m_Charge) * Grip.m_Fade * Grip.m_Alpha * Opacity));
		}
	}
	Graphics()->QuadsEnd();

	Graphics()->TextureSet(m_LeafTexture);
	Graphics()->QuadsBegin();
	Graphics()->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
	for(int i = 0; i < m_NumLeaves; i++)
	{
		const CLeaf &Leaf = m_aLeaves[i];
		const float FadeIn = std::min(1.0f, Leaf.m_Life / 0.2f);
		const float FadeOut = std::min(1.0f, (Leaf.m_LifeSpan - Leaf.m_Life) / 0.3f);
		const float Flip = 0.35f + 0.65f * std::abs(std::cos(Leaf.m_FlutterPhase));
		Graphics()->SetColor(Leaf.m_Color.WithAlpha(FadeIn * FadeOut));
		Graphics()->QuadsSetRotation(Leaf.m_Rotation);
		IGraphics::CQuadItem Quad(Leaf.m_Pos.x, Leaf.m_Pos.y, Leaf.m_Size, Leaf.m_Size * Flip);
		Graphics()->QuadsDraw(&Quad, 1);
	}
	Graphics()->QuadsEnd();

	Graphics()->QuadsSetRotation(0.0f);
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CHookWind::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(BcFxSuppressed(GameClient()))
		return;

	const float Time = LocalTime();
	const float Passed = std::clamp(Time - m_LastTime, 0.0f, 0.1f) * GameClient()->GetAnimationPlaybackSpeed();
	m_LastTime = Time;

	if(g_Config.m_BcHookStyle != FGF_HOOK_STYLE_WHIRLWIND)
	{
		OnReset();
		return;
	}

	const ColorRGBA Tint = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcHookWindColor));
	const float Scale = g_Config.m_BcHookSize / 100.0f;

	m_Tracker.Update(GameClient(), Passed, g_Config.m_BcHookChargeTime / 1000.0f, FADE_TIME,
		[&](int ClientId, CHookGripTracker::EEvent Event, const CHookGripTracker::CGrip &Grip) {
			switch(Event)
			{
			case CHookGripTracker::EEvent::GRAB:
				m_aStates[ClientId] = CWindState();
				m_aStates[ClientId].m_Spin = random_float() < 0.5f ? -1.0f : 1.0f;
				m_aStates[ClientId].m_EyeAngle = random_angle();
				if(g_Config.m_BcHookWindBurst)
					Burst(ClientId, Grip, Scale);
				break;
			case CHookGripTracker::EEvent::CHARGED:
				GustRing(ClientId, Grip, Scale);
				break;
			case CHookGripTracker::EEvent::RELEASE:
				Disperse(ClientId, Grip, Scale);
				break;
			}
		});

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const CHookGripTracker::CGrip &Grip = m_Tracker.Grip(i);
		if(Grip.m_Holding)
			EmitVortex(i, Grip, Passed, Scale);
		m_aStates[i].m_EyeAngle += Passed * m_aStates[i].m_Spin * mix(4.0f, 14.0f, Grip.m_Charge) * WindSpin();
	}

	Update(Passed);
	Draw(Tint, Scale);
}
