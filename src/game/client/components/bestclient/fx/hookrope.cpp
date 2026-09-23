/* Copyright © 2026 BestProject Team */
#include "hookrope.h"

#include <base/math.h>

#include <engine/shared/config.h>

#include "hookfire.h"
#include "hooklightning.h"
#include "hookgrip.h"
#include "hookmagic.h"
#include "hooktentacle.h"
#include "hooktroll.h"
#include <game/client/gameclient.h>

#include "own_tee.h"
#include <game/gamecore.h>

#include <algorithm>
#include <cmath>

using FgfGlow::Blend;
using FgfGlow::Whiten;

static constexpr float ROPE_STEP = 8.0f;

static float Envelope(float s, float Length, float HeadFade, float TeeFade)
{
	return std::clamp(s / HeadFade, 0.0f, 1.0f) * std::clamp((Length - s) / TeeFade, 0.0f, 1.0f);
}

static float Fract(float x)
{
	return x - std::floor(x);
}

void CHookRope::OnInit()
{
	m_GlowTexture = FgfGlow::CreateGlowTexture(Graphics());
	m_RingTexture = FgfGlow::CreateRingTexture(Graphics());
	m_DiscTexture = FgfGlow::CreateDiscTexture(Graphics());
	m_BeamTexture = FgfGlow::CreateBeamTexture(Graphics());
}

void CHookRope::OnReset()
{
	for(CRopeState &State : m_aStates)
		State = CRopeState();
	m_Particles.Clear();
}

void CHookRope::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(BcFxSuppressed(GameClient()))
		return;

	const float Time = LocalTime();
	const float Passed = std::clamp(Time - m_LastTime, 0.0f, 0.1f) * GameClient()->GetAnimationPlaybackSpeed();
	m_LastTime = Time;

	if(!m_Particles.HasAny())
		return;
	m_Particles.Update(Passed);
	Graphics()->BlendAdditive();
	m_Particles.Draw(Graphics(), m_GlowTexture, m_RingTexture);
	Graphics()->BlendNormal();
}

float CHookRope::Passed(int ClientId)
{
	CRopeState &State = m_aStates[ClientId];
	const float Time = LocalTime();
	float Passed = Time - State.m_LastTime;
	if(State.m_LastTime < 0.0f || Passed > 0.15f || Passed < 0.0f)
		Passed = 0.0f;
	State.m_LastTime = Time;
	return Passed * GameClient()->GetAnimationPlaybackSpeed();
}

bool CHookRope::RenderRope(int ClientId, vec2 TeePos, vec2 HookPos, int HookState, float Alpha)
{
	if(BcFxSuppressed(GameClient()))
		return false;
	if(!g_Config.m_BcHookRope || !in_range(ClientId, MAX_CLIENTS - 1))
		return false;
	const int Style = g_Config.m_BcHookStyle;
	if(Style != FGF_HOOK_STYLE_WHIRLWIND && Style != FGF_HOOK_STYLE_BLACK_HOLE && Style != FGF_HOOK_STYLE_MAGIC && Style != FGF_HOOK_STYLE_TROLL &&
		Style != FGF_HOOK_STYLE_RAINBOW && Style != FGF_HOOK_STYLE_LIGHTNING && Style != FGF_HOOK_STYLE_TENTACLE)
		return false;
	if(!BcIsOwnTee(GameClient(), ClientId))
		return false;

	const float TimePassed = Passed(ClientId);
	if(Style == FGF_HOOK_STYLE_WHIRLWIND)
		RenderAir(ClientId, TeePos, HookPos, HookState == HOOK_GRABBED, Alpha, TimePassed);
	else if(Style == FGF_HOOK_STYLE_BLACK_HOLE)
		RenderVoid(ClientId, TeePos, HookPos, HookState == HOOK_FLYING, Alpha, TimePassed);
	else if(Style == FGF_HOOK_STYLE_MAGIC)
		RenderMagic(ClientId, TeePos, HookPos, HookState == HOOK_GRABBED, Alpha, TimePassed);
	else if(Style == FGF_HOOK_STYLE_RAINBOW)
		RenderRainbow(ClientId, TeePos, HookPos, HookState == HOOK_GRABBED, Alpha, TimePassed);
	else if(Style == FGF_HOOK_STYLE_LIGHTNING)
		RenderBolt(ClientId, TeePos, HookPos, HookState == HOOK_GRABBED, Alpha, TimePassed);
	else if(Style == FGF_HOOK_STYLE_TENTACLE)
		RenderTentacle(ClientId, TeePos, HookPos, HookState == HOOK_GRABBED, Alpha, TimePassed);
	else
		RenderTroll(ClientId, TeePos, HookPos, HookState == HOOK_GRABBED, Alpha, TimePassed);

	Graphics()->BlendNormal();
	Graphics()->QuadsSetRotation(0.0f);
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	return true;
}

void CHookRope::RenderOverlay(int ClientId, vec2 TeePos, vec2 HookPos, int HookState, float Alpha)
{
	if(BcFxSuppressed(GameClient()))
		return;
	if(!g_Config.m_BcHookRope || !in_range(ClientId, MAX_CLIENTS - 1) || !BcIsOwnTee(GameClient(), ClientId))
		return;
	if(g_Config.m_BcHookStyle != FGF_HOOK_STYLE_FIRE)
		return;

	RenderFire(ClientId, TeePos, HookPos, HookState == HOOK_GRABBED, Alpha, Passed(ClientId));

	Graphics()->BlendNormal();
	Graphics()->QuadsSetRotation(0.0f);
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}

static float Hash(float Seed)
{
	return Fract(std::sin(Seed * 12.9898f + 78.233f) * 43758.5453f);
}

void CHookRope::RenderAir(int ClientId, vec2 TeePos, vec2 HookPos, bool Pulling, float Alpha, float Passed)
{
	const float Distance = distance(TeePos, HookPos);
	if(Distance < 1.0f)
		return;

	CRopeState &State = m_aStates[ClientId];
	const ColorRGBA Wind = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcHookWindColor));
	const ColorRGBA Bright = Whiten(Wind, 0.75f);
	const float Opacity = g_Config.m_BcHookWindOpacity / 100.0f * Alpha;
	const float Thickness = g_Config.m_BcHookWindThickness / 100.0f;
	const float Spin = g_Config.m_BcHookWindSpin / 100.0f;
	const float Density = g_Config.m_BcHookWindDensity / 100.0f;
	const float Scale = g_Config.m_BcHookSize / 100.0f;
	const float Time = LocalTime() + ClientId * 1.37f;

	const vec2 Along = (TeePos - HookPos) / Distance;
	const vec2 Normal(-Along.y, Along.x);
	const float Flow = Pulling ? 1.0f : -1.0f;
	const vec2 Forward = Along * Flow;

	const int NumPoints = std::clamp((int)(Distance / ROPE_STEP) + 2, 2, (int)FgfGlow::MAX_STRIP_POINTS);
	vec2 aPoints[FgfGlow::MAX_STRIP_POINTS];
	float aWidths[FgfGlow::MAX_STRIP_POINTS];

	Graphics()->BlendAdditive();
	FgfGlow::BeginStrips(Graphics(), m_BeamTexture);

	for(int Thread = 0; Thread < 2; Thread++)
	{
		for(int i = 0; i < NumPoints; i++)
		{
			const float s = Distance * i / (NumPoints - 1);
			const float Env = Envelope(s, Distance, 18.0f, 16.0f);
			const float Phase = s * 2.0f * pi / 95.0f - Time * 4.0f * Spin * Flow + Thread * pi;
			aPoints[i] = HookPos + Along * s + Normal * (std::sin(Phase) * 3.2f * Scale * Env);
			const float Sheen = 0.55f + 0.45f * std::sin(s * 0.045f - Time * 9.0f * Spin * Flow + Thread * 1.3f);
			aWidths[i] = 0.75f * Thickness * Scale * mix(0.35f, 1.0f, Sheen) * std::max(Env, 0.25f);
		}
		FgfGlow::DrawStrip(Graphics(), aPoints, aWidths, NumPoints, (Thread == 0 ? Bright : Wind).WithAlpha(0.8f * Opacity));
	}

	enum
	{
		LINE_POINTS = 12,
		CURL_POINTS = 10,
	};
	const int NumStreaks = std::clamp((int)(Distance / 60.0f * Density) + 1, 1, 10);
	for(int Streak = 0; Streak < NumStreaks; Streak++)
	{
		const float Seed = Streak * 7.31f + ClientId * 3.17f;
		const float Speed = mix(260.0f, 420.0f, Hash(Seed + 1.0f)) * Spin;
		const float Length = mix(28.0f, 60.0f, Hash(Seed + 2.0f)) * Scale;
		const float Side = (Hash(Seed + 3.0f) < 0.5f ? -1.0f : 1.0f) * mix(4.5f, 8.0f, Hash(Seed + 4.0f)) * Scale;
		const bool Curl = Hash(Seed + 5.0f) < 0.5f;
		const float Cycle = Distance + Length + 60.0f;
		const float Front = Fract(Time * Speed / Cycle + Hash(Seed + 6.0f)) * Cycle - 30.0f;

		int Count = 0;
		float FrontWidth = 0.0f;
		for(int i = 0; i < LINE_POINTS; i++)
		{
			const float u = i / (float)(LINE_POINTS - 1);
			const float Travelled = Front - Length * (1.0f - u);
			const float s = Flow > 0.0f ? Travelled : Distance - Travelled;
			const float Env = Envelope(s, Distance, 24.0f, 24.0f);
			aPoints[Count] = HookPos + Along * std::clamp(s, 0.0f, Distance) + Normal * Side * mix(0.4f, 1.0f, Env);
			aWidths[Count] = 0.6f * Thickness * Scale * std::sin(pi * 0.5f * u) * Env;
			FrontWidth = aWidths[Count];
			Count++;
		}
		if(Curl && FrontWidth > 0.01f)
		{
			const vec2 Start = aPoints[Count - 1];
			const vec2 Away = Normal * (Side > 0.0f ? 1.0f : -1.0f);
			const float Radius = 3.0f * Scale;
			const vec2 Center = Start + Away * Radius;
			for(int i = 1; i <= CURL_POINTS; i++)
			{
				const float c = i / (float)CURL_POINTS;
				const float a = c * 1.6f * pi;
				aPoints[Count] = Center + (-Away * std::cos(a) + Forward * std::sin(a)) * Radius * (1.0f - 0.55f * c);
				aWidths[Count] = FrontWidth * (1.0f - c);
				Count++;
			}
		}
		else
		{
			for(int i = 0; i < Count; i++)
				aWidths[i] *= std::min(1.0f, (Count - 1 - i) / 3.0f);
		}
		FgfGlow::DrawStrip(Graphics(), aPoints, aWidths, Count, Bright.WithAlpha(0.65f * Opacity));
	}

	const float HeadSpin = (Pulling ? 6.0f : 14.0f) * Spin * (ClientId % 2 ? 1.0f : -1.0f);
	for(int Arc = 0; Arc < 2; Arc++)
	{
		const int ArcPoints = 14;
		for(int i = 0; i < ArcPoints; i++)
		{
			const float t = i / (float)(ArcPoints - 1);
			const float Angle = Time * HeadSpin + Arc * pi + t * 2.2f;
			aPoints[i] = HookPos + direction(Angle) * mix(6.5f, 4.0f, t) * Scale;
			aWidths[i] = 0.7f * Thickness * Scale * std::sin(pi * t);
		}
		FgfGlow::DrawStrip(Graphics(), aPoints, aWidths, ArcPoints, Bright.WithAlpha(0.9f * Opacity));
	}

	if(!Pulling)
	{
		enum
		{
			NUM_CRESCENTS = 2,
			ARC_POINTS = 14,
		};
		const float Facing = angle(-Along);
		for(int Crescent = 0; Crescent < NUM_CRESCENTS; Crescent++)
		{
			const float Age = Fract(Time * 3.0f * Spin + Crescent / (float)NUM_CRESCENTS);
			const vec2 Center = HookPos + Along * (Age * 30.0f + 2.0f) * Scale;
			const float Radius = mix(6.0f, 15.0f, Age) * Scale;
			for(int i = 0; i < ARC_POINTS; i++)
			{
				const float t = i / (float)(ARC_POINTS - 1);
				aPoints[i] = Center + direction(Facing + (t * 2.0f - 1.0f) * 1.1f) * Radius;
				aWidths[i] = 0.8f * Thickness * Scale * std::sin(pi * t);
			}
			FgfGlow::DrawStrip(Graphics(), aPoints, aWidths, ARC_POINTS, Bright.WithAlpha(0.7f * (1.0f - Age) * std::min(1.0f, Age * 6.0f) * Opacity));
		}
	}
	Graphics()->QuadsEnd();

	Graphics()->TextureSet(m_GlowTexture);
	Graphics()->QuadsBegin();
	Graphics()->QuadsSetRotation(angle(Along));
	{
		const float Spacing = 70.0f;
		const float Travel = Time * 380.0f * Spin * Flow / Spacing;
		const int Count = (int)(Distance / Spacing) + 2;
		for(int j = -1; j < Count; j++)
		{
			const float s = (Fract(Travel) + j) * Spacing;
			if(s < 0.0f || s > Distance)
				continue;
			const float Fade = Envelope(s, Distance, 30.0f, 30.0f);
			const int Id = j - (int)std::floor(Travel);
			const float Phase = s * 2.0f * pi / 95.0f - Time * 4.0f * Spin * Flow + (Id % 2) * pi;
			const vec2 Pos = HookPos + Along * s + Normal * (std::sin(Phase) * 3.2f * Scale * Envelope(s, Distance, 18.0f, 16.0f));
			Graphics()->SetColor(Bright.WithAlpha(0.6f * Fade * Opacity));
			IGraphics::CQuadItem Quad(Pos.x, Pos.y, 16.0f * Scale, 2.5f * Scale);
			Graphics()->QuadsDraw(&Quad, 1);
		}
	}
	Graphics()->QuadsSetRotation(0.0f);
	Graphics()->SetColor(Wind.WithAlpha(0.18f * Opacity));
	IGraphics::CQuadItem Puff(HookPos.x, HookPos.y, 22.0f * Scale, 22.0f * Scale);
	Graphics()->QuadsDraw(&Puff, 1);
	Graphics()->QuadsEnd();

	if(Passed <= 0.0f)
		return;

	const float Amount = g_Config.m_BcHookWindDust / 100.0f;
	State.m_EmitAccumulator += Passed * Distance * 0.025f * Amount;
	while(State.m_EmitAccumulator >= 1.0f)
	{
		State.m_EmitAccumulator -= 1.0f;
		CGlowParticles::CSpark *pSpark = m_Particles.NewSpark();
		if(!pSpark)
			break;
		const float Side = random_float(-1.0f, 1.0f);
		pSpark->m_Pos = HookPos + Along * random_float(0.1f, 0.9f) * Distance + Normal * Side * 6.0f * Scale;
		pSpark->m_Vel = Forward * random_float(140.0f, 260.0f) + Normal * Side * random_float(10.0f, 30.0f);
		pSpark->m_Drag = 0.1f;
		pSpark->m_Stretch = 0.04f;
		pSpark->m_LifeSpan = random_float(0.3f, 0.5f);
		pSpark->m_StartSize = random_float(1.5f, 2.5f) * Scale;
		pSpark->m_EndSize = 0.5f * Scale;
		pSpark->m_StartAlpha = 0.5f * Opacity;
		pSpark->m_EndAlpha = 0.0f;
		pSpark->m_Color = Bright;
	}
}

void CHookRope::RenderVoid(int ClientId, vec2 TeePos, vec2 HookPos, bool Flying, float Alpha, float Passed)
{
	const float Distance = distance(TeePos, HookPos);
	if(Distance < 1.0f)
		return;

	CRopeState &State = m_aStates[ClientId];

	const ColorHSLA Hsl(g_Config.m_BcHookHoleColor);
	ColorHSLA DeepHsl = Hsl;
	DeepHsl.h = std::fmod(DeepHsl.h + 0.95f, 1.0f);
	DeepHsl.l *= 0.6f;
	const ColorRGBA Hot = color_cast<ColorRGBA>(Hsl);
	const ColorRGBA Deep = color_cast<ColorRGBA>(DeepHsl);
	const ColorRGBA Core = Whiten(Hot, 0.75f);

	const float Scale = g_Config.m_BcHookSize / 100.0f;
	const float Spin = g_Config.m_BcHookHoleSpin / 100.0f;
	const float Time = LocalTime() + ClientId * 1.37f;

	const vec2 Along = (TeePos - HookPos) / Distance;
	const vec2 Normal(-Along.y, Along.x);

	const int NumPoints = std::clamp((int)(Distance / ROPE_STEP) + 2, 2, (int)FgfGlow::MAX_STRIP_POINTS);
	vec2 aCenter[FgfGlow::MAX_STRIP_POINTS];
	float aBead[FgfGlow::MAX_STRIP_POINTS];
	float aEnv[FgfGlow::MAX_STRIP_POINTS];
	vec2 aPoints[FgfGlow::MAX_STRIP_POINTS];
	float aWidths[FgfGlow::MAX_STRIP_POINTS];

	for(int i = 0; i < NumPoints; i++)
	{
		const float s = Distance * i / (NumPoints - 1);
		aEnv[i] = std::max(Envelope(s, Distance, 16.0f, 24.0f), 0.0f);
		const float Warp = std::sin(s * 0.07f + Time * 5.0f) * 1.3f * Scale * aEnv[i];
		aCenter[i] = HookPos + Along * s + Normal * Warp;
		const float q = Fract((s + Time * 240.0f) / 70.0f) - 0.5f;
		aBead[i] = std::exp(-q * q * 50.0f) * aEnv[i];
	}

	auto Layer = [&](float BaseWidth, float BeadWidth, float MinEnv, float Side, ColorRGBA Color) {
		for(int i = 0; i < NumPoints; i++)
		{
			const float Env = std::max(aEnv[i], MinEnv);
			aWidths[i] = (BaseWidth + BeadWidth * aBead[i]) * Env * Scale;
			aPoints[i] = aCenter[i] + Normal * (Side * (2.4f + 1.8f * aBead[i]) * Env * Scale);
		}
		FgfGlow::DrawStrip(Graphics(), aPoints, aWidths, NumPoints, Color);
	};

	Graphics()->BlendAdditive();
	FgfGlow::BeginStrips(Graphics(), m_BeamTexture);
	Layer(9.0f, 6.0f, 0.4f, 0.0f, Deep.WithAlpha(0.45f * Alpha));
	Layer(4.2f, 3.0f, 0.5f, 0.0f, Hot.WithAlpha(0.75f * Alpha));
	Graphics()->QuadsEnd();

	Graphics()->BlendNormal();
	FgfGlow::BeginStrips(Graphics(), m_BeamTexture);
	Layer(2.0f, 1.8f, 0.6f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.92f * Alpha));
	Graphics()->QuadsEnd();

	Graphics()->BlendAdditive();
	FgfGlow::BeginStrips(Graphics(), m_BeamTexture);
	Layer(0.7f, 0.5f, 0.3f, 1.0f, Core.WithAlpha(0.55f * Alpha));
	Layer(0.7f, 0.5f, 0.3f, -1.0f, Core.WithAlpha(0.55f * Alpha));

	const float Horizon = (5.5f + 0.6f * std::sin(Time * 9.0f)) * Scale;
	const float DiskAngle = Time * 6.0f * Spin * (ClientId % 2 ? 1.0f : -1.0f);
	const vec2 Major = Normal * 13.0f * Scale;
	const vec2 Minor = Along * 13.0f * 0.3f * Scale;
	auto DiskHalf = [&](bool Front, ColorRGBA Color, float Width) {
		const int HalfPoints = 16;
		for(int i = 0; i < HalfPoints; i++)
		{
			const float a = pi * i / (HalfPoints - 1) + (Front ? 0.0f : pi);
			aPoints[i] = HookPos + Major * std::cos(a) + Minor * std::sin(a);
			const float Clump = 0.5f + 0.5f * std::cos(a - DiskAngle);
			aWidths[i] = Width * (0.6f + 0.8f * Clump * Clump) * Scale * std::sin(pi * i / (HalfPoints - 1)) + 0.3f;
		}
		FgfGlow::DrawStrip(Graphics(), aPoints, aWidths, HalfPoints, Color);
	};
	DiskHalf(false, Hot.WithAlpha(0.7f * Alpha), 2.6f);
	Graphics()->QuadsEnd();

	Graphics()->TextureSet(m_GlowTexture);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(Deep.WithAlpha(0.55f * Alpha));
	IGraphics::CQuadItem Halo(HookPos.x, HookPos.y, 50.0f * Scale, 50.0f * Scale);
	Graphics()->QuadsDraw(&Halo, 1);
	Graphics()->SetColor(Hot.WithAlpha(0.5f * Alpha));
	IGraphics::CQuadItem Inner(HookPos.x, HookPos.y, 24.0f * Scale, 24.0f * Scale);
	Graphics()->QuadsDraw(&Inner, 1);
	Graphics()->QuadsEnd();

	Graphics()->BlendNormal();
	Graphics()->TextureSet(m_DiscTexture);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.97f * Alpha);
	IGraphics::CQuadItem Hole(HookPos.x, HookPos.y, Horizon * 2.0f, Horizon * 2.0f);
	Graphics()->QuadsDraw(&Hole, 1);
	Graphics()->QuadsEnd();

	Graphics()->BlendAdditive();
	Graphics()->TextureSet(m_RingTexture);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(Core.WithAlpha(0.8f * Alpha));
	IGraphics::CQuadItem PhotonRing(HookPos.x, HookPos.y, Horizon * 2.7f, Horizon * 2.7f);
	Graphics()->QuadsDraw(&PhotonRing, 1);
	Graphics()->QuadsEnd();

	FgfGlow::BeginStrips(Graphics(), m_BeamTexture);
	DiskHalf(true, Core.WithAlpha(0.85f * Alpha), 2.2f);
	Graphics()->QuadsEnd();

	if(Passed <= 0.0f)
		return;

	const float Amount = g_Config.m_BcHookHoleStreaks / 100.0f;

	State.m_EmitAccumulator += Passed * Distance * 0.13f * Amount;
	while(State.m_EmitAccumulator >= 1.0f)
	{
		State.m_EmitAccumulator -= 1.0f;
		CGlowParticles::CSpark *pSpark = m_Particles.NewSpark();
		if(!pSpark)
			break;
		const float s = random_float(Distance * 0.25f, Distance);
		const float Side = random_float(-16.0f, 16.0f) * Scale;
		const float Speed = random_float(170.0f, 300.0f);
		pSpark->m_Pos = HookPos + Along * s + Normal * Side;
		pSpark->m_Vel = -Along * Speed - Normal * Side * 3.0f;
		pSpark->m_Drag = 2.2f;
		pSpark->m_Stretch = 0.04f;
		pSpark->m_LifeSpan = std::clamp(s / (Speed * 1.35f), 0.08f, 0.8f);
		pSpark->m_StartSize = random_float(2.5f, 4.5f) * Scale;
		pSpark->m_EndSize = 1.2f * Scale;
		pSpark->m_StartAlpha = 0.3f * Alpha;
		pSpark->m_EndAlpha = 0.9f * Alpha;
		pSpark->m_Color = Blend(Hot, Core, random_float());
	}

	State.m_HeadAccumulator += Passed * 30.0f * std::max(Amount, 0.3f);
	while(State.m_HeadAccumulator >= 1.0f)
	{
		State.m_HeadAccumulator -= 1.0f;
		CGlowParticles::CSpark *pSpark = m_Particles.NewSpark();
		if(!pSpark)
			break;
		const float Radius = random_float(14.0f, 26.0f) * Scale;
		pSpark->m_Orbital = true;
		pSpark->m_Center = HookPos;
		pSpark->m_LifeSpan = random_float(0.25f, 0.4f);
		pSpark->m_Radius = Radius;
		pSpark->m_RadialSpeed = -(Radius - Horizon) / pSpark->m_LifeSpan;
		pSpark->m_Angle = random_float(0.0f, 2.0f * pi);
		pSpark->m_AngularSpeed = 5.0f * Spin * (ClientId % 2 ? 1.0f : -1.0f);
		pSpark->m_Stretch = 0.03f;
		pSpark->m_StartSize = 2.5f * Scale;
		pSpark->m_EndSize = 1.5f * Scale;
		pSpark->m_StartAlpha = 0.2f * Alpha;
		pSpark->m_EndAlpha = 0.9f * Alpha;
		pSpark->m_Color = Core;
	}

	if(Flying)
	{
		State.m_RingAccumulator += Passed * 7.0f;
		while(State.m_RingAccumulator >= 1.0f)
		{
			State.m_RingAccumulator -= 1.0f;
			m_Particles.AddRing(HookPos, 0.28f, 44.0f * Scale, 6.0f * Scale, 0.35f * Alpha, Hot);
		}
	}
}

void CHookRope::RenderMagic(int ClientId, vec2 TeePos, vec2 HookPos, bool Pulling, float Alpha, float Passed)
{
	const float Distance = distance(TeePos, HookPos);
	if(Distance < 1.0f)
		return;

	CRopeState &State = m_aStates[ClientId];
	const CHookMagic::CPalette Palette = CHookMagic::Palette();
	const float Scale = g_Config.m_BcHookSize / 100.0f;
	const float Spin = g_Config.m_BcHookMagicSpin / 100.0f;
	const float Time = LocalTime() + ClientId * 1.37f;
	const vec2 Along = (TeePos - HookPos) / Distance;
	const vec2 Normal(-Along.y, Along.x);
	const float Flow = Pulling ? 1.0f : -1.0f;

	const int NumPoints = std::clamp((int)(Distance / ROPE_STEP) + 2, 2, (int)FgfGlow::MAX_STRIP_POINTS);
	vec2 aPoints[FgfGlow::MAX_STRIP_POINTS];
	float aWidths[FgfGlow::MAX_STRIP_POINTS];

	Graphics()->BlendAdditive();
	FgfGlow::BeginStrips(Graphics(), m_BeamTexture);

	for(int i = 0; i < NumPoints; i++)
	{
		const float s = Distance * i / (NumPoints - 1);
		aPoints[i] = HookPos + Along * s;
		aWidths[i] = 6.0f * Scale * std::max(Envelope(s, Distance, 10.0f, 24.0f), 0.3f);
	}
	FgfGlow::DrawStrip(Graphics(), aPoints, aWidths, NumPoints, Palette.m_Main.WithAlpha(0.35f * Alpha));
	for(int i = 0; i < NumPoints; i++)
		aWidths[i] *= 0.28f;
	FgfGlow::DrawStrip(Graphics(), aPoints, aWidths, NumPoints, Palette.m_Core.WithAlpha(0.9f * Alpha));

	for(int Thread = 0; Thread < 2; Thread++)
	{
		for(int i = 0; i < NumPoints; i++)
		{
			const float s = Distance * i / (NumPoints - 1);
			const float Env = Envelope(s, Distance, 18.0f, 26.0f);
			const float Phase = s * 2.0f * pi / 44.0f - Time * 7.0f * Spin * Flow + Thread * pi;
			aPoints[i] = HookPos + Along * s + Normal * std::sin(Phase) * 6.0f * Scale * Env;
			aWidths[i] = (0.5f + 0.5f * (0.5f + 0.5f * std::cos(Phase))) * 1.2f * Scale * std::max(Env, 0.2f);
		}
		FgfGlow::DrawStrip(Graphics(), aPoints, aWidths, NumPoints, Palette.m_Accent.WithAlpha(0.75f * Alpha));
	}

	if(g_Config.m_BcHookMagicRunes)
	{
		const float Spacing = 24.0f * Scale;
		const float Travel = Time * 70.0f * Spin * Flow / Spacing;
		const int Count = (int)(Distance / Spacing) + 2;
		for(int j = -1; j < Count; j++)
		{
			const float s = (Fract(Travel) + j) * Spacing;
			if(s < 0.0f || s > Distance)
				continue;
			const int Id = j - (int)std::floor(Travel);
			const float Fade = Envelope(s, Distance, 20.0f, 30.0f);
			const float Shimmer = 0.7f + 0.3f * std::sin(Time * 9.0f + Id * 2.3f);
			CHookMagic::DrawRune(Graphics(), HookPos + Along * s, 7.5f * Scale, angle(Along) - pi / 2.0f, Id * 7 + ClientId * 131, 1.0f * Scale, Palette.m_Core.WithAlpha(Fade * Shimmer * Alpha));
		}
	}

	const float HeadRadius = 9.0f * Scale;
	const float HeadAngle = Time * (Pulling ? 3.0f : 9.0f) * Spin * (ClientId % 2 ? 1.0f : -1.0f);
	for(int i = 0; i < 25; i++)
	{
		aPoints[i] = HookPos + direction(HeadAngle + i * 2.0f * pi / 24.0f) * HeadRadius;
		aWidths[i] = 1.1f * Scale;
	}
	FgfGlow::DrawStrip(Graphics(), aPoints, aWidths, 25, Palette.m_Core.WithAlpha(0.95f * Alpha));
	for(int Triangle = 0; Triangle < 2; Triangle++)
	{
		for(int i = 0; i < 4; i++)
		{
			aPoints[i] = HookPos + direction(-HeadAngle * 1.5f + Triangle * pi / 3.0f + i * 2.0f * pi / 3.0f) * HeadRadius * 0.85f;
			aWidths[i] = 0.8f * Scale;
		}
		FgfGlow::DrawStrip(Graphics(), aPoints, aWidths, 4, Palette.m_Main.WithAlpha(0.9f * Alpha));
	}
	for(int i = 0; i < 4; i++)
	{
		const vec2 Out = direction(HeadAngle + i * pi / 2.0f);
		aPoints[0] = HookPos + Out * HeadRadius;
		aPoints[1] = HookPos + Out * HeadRadius * 1.45f;
		aWidths[0] = 1.0f * Scale;
		aWidths[1] = 0.2f * Scale;
		FgfGlow::DrawStrip(Graphics(), aPoints, aWidths, 2, Palette.m_Accent.WithAlpha(0.9f * Alpha));
	}
	Graphics()->QuadsEnd();

	Graphics()->TextureSet(m_GlowTexture);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(Palette.m_Main.WithAlpha(0.5f * Alpha));
	IGraphics::CQuadItem Halo(HookPos.x, HookPos.y, 44.0f * Scale, 44.0f * Scale);
	Graphics()->QuadsDraw(&Halo, 1);
	Graphics()->SetColor(Palette.m_Core.WithAlpha(0.8f * Alpha));
	IGraphics::CQuadItem Core(HookPos.x, HookPos.y, 9.0f * Scale, 9.0f * Scale);
	Graphics()->QuadsDraw(&Core, 1);
	Graphics()->QuadsEnd();

	if(Passed <= 0.0f)
		return;

	const float Amount = g_Config.m_BcHookMagicSparkles / 100.0f;
	State.m_EmitAccumulator += Passed * Distance * 0.08f * Amount;
	while(State.m_EmitAccumulator >= 1.0f)
	{
		State.m_EmitAccumulator -= 1.0f;
		CGlowParticles::CSpark *pSpark = m_Particles.NewSpark();
		if(!pSpark)
			break;
		pSpark->m_Pos = HookPos + Along * random_float(0.0f, Distance) + Normal * random_float(-5.0f, 5.0f) * Scale;
		pSpark->m_Vel = vec2(random_float(-15.0f, 15.0f), random_float(-45.0f, -15.0f)) * Scale;
		pSpark->m_Drag = 0.5f;
		pSpark->m_LifeSpan = random_float(0.5f, 0.9f);
		pSpark->m_StartSize = random_float(2.5f, 4.0f) * Scale;
		pSpark->m_EndSize = 0.5f * Scale;
		pSpark->m_StartAlpha = 0.8f * Alpha;
		pSpark->m_EndAlpha = 0.0f;
		pSpark->m_Color = random_float() < 0.5f ? Palette.m_Core : Palette.m_Accent;
	}

	if(!Pulling)
	{
		State.m_HeadAccumulator += Passed * 50.0f * std::max(Amount, 0.3f);
		while(State.m_HeadAccumulator >= 1.0f)
		{
			State.m_HeadAccumulator -= 1.0f;
			CGlowParticles::CSpark *pSpark = m_Particles.NewSpark();
			if(!pSpark)
				break;
			pSpark->m_Pos = HookPos + direction(random_float(0.0f, 2.0f * pi)) * HeadRadius;
			pSpark->m_Vel = Along * random_float(30.0f, 90.0f) + Normal * random_float(-60.0f, 60.0f);
			pSpark->m_Drag = 0.1f;
			pSpark->m_Stretch = 0.03f;
			pSpark->m_LifeSpan = random_float(0.25f, 0.45f);
			pSpark->m_StartSize = random_float(3.0f, 4.5f) * Scale;
			pSpark->m_EndSize = 0.8f * Scale;
			pSpark->m_StartAlpha = 0.9f * Alpha;
			pSpark->m_EndAlpha = 0.0f;
			pSpark->m_Color = Palette.m_Accent;
		}
	}
}

void CHookRope::RenderFire(int ClientId, vec2 TeePos, vec2 HookPos, bool Pulling, float Alpha, float Passed)
{
	const float Distance = distance(TeePos, HookPos);
	if(Distance < 1.0f)
		return;

	CHookFire &Fire = GameClient()->m_HookFire;
	CRopeState &State = m_aStates[ClientId];
	const CHookFire::CPalette Palette = CHookFire::Palette();
	const float Scale = g_Config.m_BcHookSize / 100.0f;
	const float Intensity = g_Config.m_BcHookFireIntensity / 100.0f;
	const float Time = LocalTime() + ClientId * 1.37f;
	const vec2 Along = (TeePos - HookPos) / Distance;

	const int NumPoints = std::clamp((int)(Distance / ROPE_STEP) + 2, 2, (int)FgfGlow::MAX_STRIP_POINTS);
	vec2 aPoints[FgfGlow::MAX_STRIP_POINTS];
	float aWidths[FgfGlow::MAX_STRIP_POINTS];
	for(int i = 0; i < NumPoints; i++)
	{
		const float s = Distance * i / (NumPoints - 1);
		aPoints[i] = HookPos + Along * s;
		const float Heat = 0.55f + 0.45f * std::exp(-s / 120.0f);
		aWidths[i] = 6.0f * Scale * Heat * (0.85f + 0.15f * std::sin(s * 0.12f - Time * 9.0f)) * std::clamp((Distance - s) / 24.0f, 0.0f, 1.0f);
	}
	Graphics()->BlendAdditive();
	FgfGlow::BeginStrips(Graphics(), m_BeamTexture);
	FgfGlow::DrawStrip(Graphics(), aPoints, aWidths, NumPoints, Palette.m_Middle.WithAlpha(0.55f * Alpha));
	Graphics()->QuadsEnd();
	Graphics()->BlendNormal();

	if(Passed <= 0.0f)
		return;

	State.m_EmitAccumulator += Passed * Distance * 0.22f * Intensity;
	while(State.m_EmitAccumulator >= 1.0f)
	{
		State.m_EmitAccumulator -= 1.0f;
		const float s = random_float(0.05f, 0.92f) * Distance;
		const vec2 Pos = HookPos + Along * s;
		const float Heat = 0.6f + 0.4f * std::exp(-s / 120.0f);
		Fire.EmitFlame(Pos, vec2(random_float(-15.0f, 15.0f), random_float(-80.0f, -40.0f)) * Scale,
			random_float(7.0f, 11.0f) * Scale * Heat, random_float(0.25f, 0.4f), Alpha, Pos.x, 0.0f);
		if(random_float() < 0.08f * g_Config.m_BcHookFireEmbers / 100.0f)
			Fire.EmitEmber(Pos, vec2(random_float(-30.0f, 30.0f), random_float(-120.0f, -50.0f)), Alpha);
	}

	State.m_HeadAccumulator += Passed * (Pulling ? 25.0f : 70.0f) * Intensity;
	while(State.m_HeadAccumulator >= 1.0f)
	{
		State.m_HeadAccumulator -= 1.0f;
		const vec2 Vel = Pulling ? vec2(random_float(-20.0f, 20.0f), random_float(-100.0f, -50.0f)) : Along * random_float(80.0f, 200.0f) + vec2(random_float(-25.0f, 25.0f), random_float(-40.0f, 0.0f));
		Fire.EmitFlame(HookPos + vec2(random_float(-4.0f, 4.0f), random_float(-4.0f, 4.0f)) * Scale, Vel * Scale,
			random_float(11.0f, 17.0f) * Scale, random_float(0.25f, 0.4f), Alpha, HookPos.x, 0.0f);
	}
}

void CHookRope::RenderTroll(int ClientId, vec2 TeePos, vec2 HookPos, bool Pulling, float Alpha, float Passed)
{
	const float Distance = distance(TeePos, HookPos);
	if(Distance < 1.0f)
		return;

	CHookTroll &Troll = GameClient()->m_HookTroll;
	CRopeState &State = m_aStates[ClientId];
	const float Scale = g_Config.m_BcHookSize / 100.0f;
	const float Time = LocalTime() + ClientId * 1.37f;
	const vec2 Along = (TeePos - HookPos) / Distance;
	const vec2 Normal(-Along.y, Along.x);

	const int NumPoints = std::clamp((int)(Distance / ROPE_STEP) + 2, 2, (int)FgfGlow::MAX_STRIP_POINTS);
	vec2 aPoints[FgfGlow::MAX_STRIP_POINTS];
	float aWidths[FgfGlow::MAX_STRIP_POINTS];
	for(int Strand = 0; Strand < 3; Strand++)
	{
		for(int i = 0; i < NumPoints; i++)
		{
			const float s = Distance * i / (NumPoints - 1);
			const float Env = std::max(Envelope(s, Distance, 10.0f, 18.0f), 0.25f);
			const float Phase = s * 2.0f * pi / 70.0f - Time * 3.0f * (Pulling ? 1.0f : -1.0f) + Strand * 2.1f;
			const float Sag = std::sin(pi * s / Distance) * std::min(10.0f, Distance * 0.03f) * Scale;
			const float Lump = std::pow(0.5f + 0.5f * std::sin(s * 0.09f - Time * 5.0f + Strand * 1.3f), 4.0f);
			aPoints[i] = HookPos + Along * s + Normal * std::sin(Phase) * 3.2f * Scale * Env + vec2(0.0f, Sag);
			aWidths[i] = (Strand == 0 ? 2.6f : 1.8f) * Scale * (0.8f + 0.9f * Lump) * Env;
		}
		Troll.DrawGoo(aPoints, aWidths, NumPoints, Alpha);
	}

	if(!Pulling)
		Troll.DrawFace(HookPos, 22.0f * Scale, std::sin(Time * 12.0f) * 0.15f, Along.x > 0.0f, Alpha);

	if(Passed <= 0.0f)
		return;
	State.m_EmitAccumulator += Passed * Distance * 0.012f;
	while(State.m_EmitAccumulator >= 1.0f)
	{
		State.m_EmitAccumulator -= 1.0f;
		Troll.EmitDrip(HookPos + Along * random_float(0.2f, 0.8f) * Distance, vec2(random_float(-10.0f, 10.0f), 20.0f), random_float(1.4f, 2.4f) * Scale, Alpha);
	}
}

void CHookRope::RenderRainbow(int ClientId, vec2 TeePos, vec2 HookPos, bool Pulling, float Alpha, float Passed)
{
	const float Distance = distance(TeePos, HookPos);
	if(Distance < 1.0f)
		return;

	CRopeState &State = m_aStates[ClientId];
	const float Scale = g_Config.m_BcHookSize / 100.0f;
	const float Speed = g_Config.m_BcHookRainbowSpeed / 100.0f;
	const float Width = g_Config.m_BcHookRainbowWidth / 100.0f;
	const float Time = LocalTime() + ClientId * 1.37f;
	const vec2 Along = (TeePos - HookPos) / Distance;
	const vec2 Normal(-Along.y, Along.x);
	const float Flow = Pulling ? 1.0f : -1.0f;

	static constexpr int s_Bands = 6;
	const float BandGap = 1.6f * Scale * Width;
	const int NumPoints = std::clamp((int)(Distance / ROPE_STEP) + 2, 2, (int)FgfGlow::MAX_STRIP_POINTS);
	vec2 aCenter[FgfGlow::MAX_STRIP_POINTS];
	vec2 aSide[FgfGlow::MAX_STRIP_POINTS];
	float aEnv[FgfGlow::MAX_STRIP_POINTS];
	for(int i = 0; i < NumPoints; i++)
	{
		const float s = Distance * i / (NumPoints - 1);
		aEnv[i] = Envelope(s, Distance, 10.0f, 18.0f);
		const float Wave = std::sin(s * 2.0f * pi / 90.0f - Time * 6.0f * Speed * Flow);
		aCenter[i] = HookPos + Along * s + Normal * Wave * 3.5f * Scale * aEnv[i];
		const float Twist = 0.75f + 0.25f * std::cos(s * 2.0f * pi / 90.0f - Time * 6.0f * Speed * Flow);
		aSide[i] = Normal * Twist;
	}

	Graphics()->BlendAdditive();
	FgfGlow::BeginStrips(Graphics(), m_BeamTexture);

	{
		float aWidths[FgfGlow::MAX_STRIP_POINTS];
		for(int i = 0; i < NumPoints; i++)
			aWidths[i] = (s_Bands * BandGap + 5.0f * Scale) * std::max(aEnv[i], 0.35f);
		FgfGlow::DrawStrip(Graphics(), aCenter, aWidths, NumPoints, ColorRGBA(1.0f, 1.0f, 1.0f, 0.12f * Alpha));
	}

	static constexpr int s_Segment = 3;
	for(int Band = 0; Band < s_Bands; Band++)
	{
		const float Offset = (Band - (s_Bands - 1) / 2.0f) * BandGap;
		for(int Start = 0; Start < NumPoints - 1; Start += s_Segment)
		{
			const int End = std::min(Start + s_Segment, NumPoints - 1);
			const int Count = End - Start + 1;
			vec2 aPoints[s_Segment + 1];
			float aWidths[s_Segment + 1];
			for(int k = 0; k < Count; k++)
			{
				const int i = Start + k;
				aPoints[k] = aCenter[i] + aSide[i] * Offset * std::max(aEnv[i], 0.25f);
				aWidths[k] = BandGap * 0.62f * std::max(aEnv[i], 0.3f);
			}
			const float s = Distance * (Start + End) * 0.5f / (NumPoints - 1);
			const float Hue = Band / (float)s_Bands * 0.85f + s / 260.0f - Time * 0.6f * Speed * Flow;
			FgfGlow::DrawStrip(Graphics(), aPoints, aWidths, Count, FgfRainbow(Hue, 0.58f).WithAlpha(0.95f * Alpha));
		}
	}

	const float HeadRadius = 9.0f * Scale;
	const float HeadAngle = Time * (Pulling ? 2.5f : 8.0f) * Speed;
	for(int Arm = 0; Arm < 4; Arm++)
	{
		const vec2 Dir = direction(HeadAngle + Arm * pi / 2.0f);
		const vec2 aPoints[2] = {HookPos, HookPos + Dir * HeadRadius * 1.6f};
		const float aWidths[2] = {3.0f * Scale, 0.2f * Scale};
		FgfGlow::DrawStrip(Graphics(), aPoints, aWidths, 2, FgfRainbow(HeadAngle * 0.1f + Arm * 0.25f).WithAlpha(0.95f * Alpha));
	}
	Graphics()->QuadsEnd();

	Graphics()->TextureSet(m_GlowTexture);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(FgfRainbow(Time * 0.4f * Speed).WithAlpha(0.45f * Alpha));
	IGraphics::CQuadItem Halo(HookPos.x, HookPos.y, 40.0f * Scale, 40.0f * Scale);
	Graphics()->QuadsDraw(&Halo, 1);
	Graphics()->SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.95f * Alpha));
	IGraphics::CQuadItem Core(HookPos.x, HookPos.y, 10.0f * Scale, 10.0f * Scale);
	Graphics()->QuadsDraw(&Core, 1);
	Graphics()->QuadsEnd();

	if(Passed <= 0.0f)
		return;

	const float Amount = g_Config.m_BcHookRainbowGlitter / 100.0f;
	State.m_EmitAccumulator += Passed * Distance * 0.07f * Amount;
	while(State.m_EmitAccumulator >= 1.0f)
	{
		State.m_EmitAccumulator -= 1.0f;
		CGlowParticles::CSpark *pSpark = m_Particles.NewSpark();
		if(!pSpark)
			break;
		*pSpark = CGlowParticles::CSpark{};
		const float s = random_float(0.0f, Distance);
		pSpark->m_Pos = HookPos + Along * s + Normal * random_float(-6.0f, 6.0f) * Scale;
		pSpark->m_Vel = Normal * random_float(-40.0f, 40.0f) * Scale + vec2(0.0f, random_float(-30.0f, 10.0f));
		pSpark->m_Gravity = 30.0f;
		pSpark->m_Drag = 0.4f;
		pSpark->m_LifeSpan = random_float(0.4f, 0.8f);
		pSpark->m_StartSize = random_float(2.5f, 4.5f) * Scale;
		pSpark->m_EndSize = 0.5f * Scale;
		pSpark->m_StartAlpha = 0.9f * Alpha;
		pSpark->m_EndAlpha = 0.0f;
		const float Hue = s / 260.0f - Time * 0.6f * Speed * Flow + random_float(0.0f, 0.2f);
		pSpark->m_Color = FgfGlow::Whiten(FgfRainbow(Hue), 0.3f);
		pSpark->m_ShiftColor = true;
		pSpark->m_EndColor = FgfRainbow(Hue + 0.2f);
	}

	if(!Pulling)
	{
		State.m_HeadAccumulator += Passed * 60.0f * std::max(Amount, 0.3f);
		while(State.m_HeadAccumulator >= 1.0f)
		{
			State.m_HeadAccumulator -= 1.0f;
			CGlowParticles::CSpark *pSpark = m_Particles.NewSpark();
			if(!pSpark)
				break;
			*pSpark = CGlowParticles::CSpark{};
			pSpark->m_Pos = HookPos + random_direction() * random_float(0.0f, HeadRadius);
			pSpark->m_Vel = Along * random_float(40.0f, 110.0f) + Normal * random_float(-50.0f, 50.0f);
			pSpark->m_Drag = 0.1f;
			pSpark->m_Stretch = 0.03f;
			pSpark->m_LifeSpan = random_float(0.25f, 0.45f);
			pSpark->m_StartSize = random_float(3.5f, 5.5f) * Scale;
			pSpark->m_EndSize = 0.8f * Scale;
			pSpark->m_StartAlpha = 0.95f * Alpha;
			pSpark->m_EndAlpha = 0.0f;
			pSpark->m_Color = FgfRainbow(Time * 1.5f * Speed + random_float(0.0f, 0.3f));
		}
	}
}

void CHookRope::RenderBolt(int ClientId, vec2 TeePos, vec2 HookPos, bool Pulling, float Alpha, float Passed)
{
	const float Distance = distance(TeePos, HookPos);
	if(Distance < 1.0f)
		return;

	CRopeState &State = m_aStates[ClientId];
	const ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcHookBoltColor));
	const ColorRGBA Hot = Whiten(Color, 0.8f);
	const float Scale = g_Config.m_BcHookSize / 100.0f;
	const float Jitter = g_Config.m_BcHookBoltJitter / 100.0f;
	const float Time = LocalTime() + ClientId * 1.37f;
	const vec2 Along = (TeePos - HookPos) / Distance;
	const vec2 Normal(-Along.y, Along.x);

	const float Step = std::floor(Time * 26.0f);
	const float Seed = ClientId * 91.7f + Step * 13.1f;
	const float Spread = std::min(Distance * 0.06f, 16.0f) * Jitter * (Pulling ? 0.7f : 1.0f) * Scale;
	const int Segments = std::clamp((int)(Distance / 24.0f) + 3, 4, 20);

	Graphics()->BlendAdditive();
	FgfGlow::BeginStrips(Graphics(), m_BeamTexture);

	CHookLightning::Bolt(Graphics(), HookPos, TeePos, Seed, Spread, 6.5f * Scale, Color.WithAlpha(0.22f * Alpha), Segments);
	CHookLightning::Bolt(Graphics(), HookPos, TeePos, Seed, Spread, 2.6f * Scale, Color.WithAlpha(0.85f * Alpha), Segments);
	CHookLightning::Bolt(Graphics(), HookPos, TeePos, Seed, Spread, 1.0f * Scale, Hot.WithAlpha(0.95f * Alpha), Segments);

	const int Branches = g_Config.m_BcHookBoltBranches;
	for(int i = 0; i < Branches; i++)
	{
		const float BranchSeed = Seed + 311.0f + i * 57.3f;
		if(Hash(BranchSeed) < 0.45f)
			continue;
		const float At = mix(0.15f, 0.85f, Hash(BranchSeed + 1.0f));
		const float Side = Hash(BranchSeed + 2.0f) < 0.5f ? -1.0f : 1.0f;
		const float Length = mix(14.0f, 42.0f, Hash(BranchSeed + 3.0f)) * Scale * Jitter;
		const vec2 Root = HookPos + Along * (Distance * At) + Normal * (Hash(BranchSeed + 4.0f) - 0.5f) * Spread;
		const vec2 Tip = Root + normalize(Normal * Side + Along * (Hash(BranchSeed + 5.0f) - 0.5f)) * Length;
		CHookLightning::Bolt(Graphics(), Root, Tip, BranchSeed, Spread * 0.5f, 1.2f * Scale, Hot.WithAlpha(0.6f * Alpha), 5);
	}
	Graphics()->QuadsEnd();

	Graphics()->TextureSet(m_GlowTexture);
	Graphics()->QuadsBegin();
	const float Flicker = mix(0.85f, 1.15f, Hash(Seed + 7.0f));
	Graphics()->SetColor(Color.WithAlpha(0.5f * Alpha));
	IGraphics::CQuadItem Halo(HookPos.x, HookPos.y, 34.0f * Scale * Flicker, 34.0f * Scale * Flicker);
	Graphics()->QuadsDraw(&Halo, 1);
	Graphics()->SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.95f * Alpha));
	IGraphics::CQuadItem Core(HookPos.x, HookPos.y, 11.0f * Scale * Flicker, 11.0f * Scale * Flicker);
	Graphics()->QuadsDraw(&Core, 1);
	Graphics()->QuadsEnd();

	if(Passed <= 0.0f)
		return;

	const float Amount = g_Config.m_BcHookBoltSparks / 100.0f;
	State.m_EmitAccumulator += Passed * Distance * 0.05f * Amount;
	while(State.m_EmitAccumulator >= 1.0f)
	{
		State.m_EmitAccumulator -= 1.0f;
		CGlowParticles::CSpark *pSpark = m_Particles.NewSpark();
		if(!pSpark)
			break;
		*pSpark = CGlowParticles::CSpark{};
		const float s = random_float(0.0f, Distance);
		pSpark->m_Pos = HookPos + Along * s + Normal * random_float(-Spread, Spread);
		pSpark->m_Vel = Normal * random_float(-120.0f, 120.0f) * Scale + Along * random_float(-40.0f, 40.0f);
		pSpark->m_Gravity = 200.0f;
		pSpark->m_Drag = 0.1f;
		pSpark->m_Stretch = 0.05f;
		pSpark->m_LifeSpan = random_float(0.15f, 0.35f);
		pSpark->m_StartSize = random_float(2.5f, 4.5f) * Scale;
		pSpark->m_EndSize = 0.4f * Scale;
		pSpark->m_StartAlpha = 0.9f * Alpha;
		pSpark->m_EndAlpha = 0.0f;
		pSpark->m_Color = Hot;
		pSpark->m_ShiftColor = true;
		pSpark->m_EndColor = Color;
	}
}

void CHookRope::RenderTentacle(int ClientId, vec2 TeePos, vec2 HookPos, bool Pulling, float Alpha, float Passed)
{
	const float Distance = distance(TeePos, HookPos);
	if(Distance < 1.0f)
		return;

	CRopeState &State = m_aStates[ClientId];
	const ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcHookTentacleColor));
	const ColorRGBA Dark = Blend(Color, ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f), 0.5f);
	const ColorRGBA Bright = Whiten(Color, 0.5f);
	const float Scale = g_Config.m_BcHookSize / 100.0f;
	const float Waves = g_Config.m_BcHookTentacleWaves / 100.0f;
	const float Thickness = g_Config.m_BcHookTentacleThickness / 100.0f;
	const float Time = LocalTime() + ClientId * 1.37f;
	const vec2 Along = (TeePos - HookPos) / Distance;
	const vec2 Normal(-Along.y, Along.x);
	const float Amount = (Pulling ? 5.0f : 9.0f) * Scale * Waves;
	const float Phase = Time * (Pulling ? 5.0f : 9.0f) * Waves;

	const int NumPoints = std::clamp((int)(Distance / ROPE_STEP) + 2, 2, (int)FgfGlow::MAX_STRIP_POINTS);
	vec2 aPoints[FgfGlow::MAX_STRIP_POINTS];
	float aWidths[FgfGlow::MAX_STRIP_POINTS];
	float aEnv[FgfGlow::MAX_STRIP_POINTS];
	for(int i = 0; i < NumPoints; i++)
	{
		const float u = i / (float)(NumPoints - 1);
		const float s = Distance * u;
		aEnv[i] = Envelope(s, Distance, 14.0f, 20.0f);
		aPoints[i] = HookPos + Along * s + Normal * CHookTentacle::Wobble(u, Phase, Amount) * aEnv[i];
		aWidths[i] = mix(2.2f, 6.5f, u) * Thickness * Scale * std::max(aEnv[i], 0.45f);
	}

	Graphics()->BlendNormal();
	FgfGlow::BeginStrips(Graphics(), m_BeamTexture);

	{
		float aOutline[FgfGlow::MAX_STRIP_POINTS];
		for(int i = 0; i < NumPoints; i++)
			aOutline[i] = aWidths[i] + 1.6f * Scale;
		FgfGlow::DrawStrip(Graphics(), aPoints, aOutline, NumPoints, Dark.WithAlpha(0.95f * Alpha));
	}
	FgfGlow::DrawStrip(Graphics(), aPoints, aWidths, NumPoints, Color.WithAlpha(0.95f * Alpha));

	{
		vec2 aBelly[FgfGlow::MAX_STRIP_POINTS];
		float aBellyWidth[FgfGlow::MAX_STRIP_POINTS];
		for(int i = 0; i < NumPoints; i++)
		{
			aBelly[i] = aPoints[i] + Normal * aWidths[i] * 0.35f;
			aBellyWidth[i] = aWidths[i] * 0.3f;
		}
		FgfGlow::DrawStrip(Graphics(), aBelly, aBellyWidth, NumPoints, Bright.WithAlpha(0.5f * Alpha));
	}
	Graphics()->QuadsEnd();

	if(g_Config.m_BcHookTentacleSuckers)
	{
		Graphics()->TextureSet(m_DiscTexture);
		Graphics()->QuadsBegin();
		Graphics()->QuadsSetRotation(0.0f);
		const float Spacing = 13.0f * Scale;
		const int Count = std::clamp((int)(Distance / Spacing), 0, 40);
		for(int i = 0; i < Count; i++)
		{
			const float u = (i + 0.5f) / (float)std::max(Count, 1);
			const int Index = std::clamp((int)(u * (NumPoints - 1)), 0, NumPoints - 1);
			const vec2 Pos = aPoints[Index] - Normal * aWidths[Index] * 0.45f;
			const float Size = aWidths[Index] * 0.85f;
			Graphics()->SetColor(Dark.WithAlpha(0.9f * Alpha));
			IGraphics::CQuadItem Outer(Pos.x, Pos.y, Size, Size);
			Graphics()->QuadsDraw(&Outer, 1);
			Graphics()->SetColor(Blend(Dark, ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f), 0.4f).WithAlpha(0.9f * Alpha));
			IGraphics::CQuadItem Inner(Pos.x, Pos.y, Size * 0.5f, Size * 0.5f);
			Graphics()->QuadsDraw(&Inner, 1);
		}
		Graphics()->QuadsEnd();
	}

	Graphics()->BlendAdditive();
	Graphics()->TextureSet(m_GlowTexture);
	Graphics()->QuadsBegin();
	Graphics()->QuadsSetRotation(0.0f);
	for(int i = 1; i < NumPoints - 1; i += 3)
	{
		Graphics()->SetColor(Bright.WithAlpha(0.14f * Alpha * aEnv[i]));
		const vec2 At = aPoints[i] + Normal * aWidths[i] * 0.3f;
		IGraphics::CQuadItem Wet(At.x, At.y, aWidths[i] * 2.2f, aWidths[i] * 2.2f);
		Graphics()->QuadsDraw(&Wet, 1);
	}
	Graphics()->QuadsEnd();
	Graphics()->BlendNormal();

	if(Passed <= 0.0f)
		return;

	const float Slime = g_Config.m_BcHookTentacleSlime / 100.0f;
	State.m_EmitAccumulator += Passed * Distance * 0.015f * Slime;
	while(State.m_EmitAccumulator >= 1.0f)
	{
		State.m_EmitAccumulator -= 1.0f;
		CGlowParticles::CSpark *pSpark = m_Particles.NewSpark();
		if(!pSpark)
			break;
		*pSpark = CGlowParticles::CSpark{};
		const int Index = std::clamp((int)(random_float() * NumPoints), 0, NumPoints - 1);
		pSpark->m_Pos = aPoints[Index];
		pSpark->m_Vel = vec2(random_float(-30.0f, 30.0f), random_float(-20.0f, 40.0f)) * Scale;
		pSpark->m_Gravity = 420.0f;
		pSpark->m_Drag = 0.5f;
		pSpark->m_Stretch = 0.02f;
		pSpark->m_LifeSpan = random_float(0.4f, 0.9f);
		pSpark->m_StartSize = random_float(2.5f, 4.5f) * Scale;
		pSpark->m_EndSize = 1.0f * Scale;
		pSpark->m_StartAlpha = 0.75f * Alpha;
		pSpark->m_EndAlpha = 0.0f;
		pSpark->m_Color = Color;
	}
}
