/* Copyright © 2026 BestProject Team */
#include "hookmagic.h"

#include <base/math.h>

#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include "own_tee.h"

#include <algorithm>
#include <cmath>

using FgfGlow::Blend;
using FgfGlow::Whiten;

static constexpr float FADE_TIME = 0.35f;
static constexpr float APPEAR_TIME = 0.45f;

static float MagicSpin() { return g_Config.m_BcHookMagicSpin / 100.0f; }
static float MagicSparkles() { return g_Config.m_BcHookMagicSparkles / 100.0f; }

static bool s_ShadowPass = false;
static ColorRGBA s_ShadowColor;

static float StrokeWidth(float Width)
{
	return s_ShadowPass ? Width * 2.4f + 1.2f : Width;
}

static ColorRGBA StrokeColor(ColorRGBA Color)
{
	return s_ShadowPass ? s_ShadowColor.WithAlpha(Color.a * 0.22f) : Color;
}

static void DrawLine(IGraphics *pGraphics, vec2 From, vec2 To, float Width, ColorRGBA Color)
{
	const vec2 aPoints[2] = {From, To};
	const float aWidths[2] = {StrokeWidth(Width), StrokeWidth(Width)};
	Color = StrokeColor(Color);
	FgfGlow::DrawStrip(pGraphics, aPoints, aWidths, 2, Color);
}

static void DrawArc(IGraphics *pGraphics, vec2 Center, float Radius, float StartAngle, float Sweep, float Width, ColorRGBA Color)
{
	if(Sweep <= 0.001f || Radius <= 0.0f)
		return;
	const int NumPoints = std::clamp((int)(Sweep / (2.0f * pi) * 60.0f) + 2, 2, (int)FgfGlow::MAX_STRIP_POINTS);
	vec2 aPoints[FgfGlow::MAX_STRIP_POINTS];
	float aWidths[FgfGlow::MAX_STRIP_POINTS];
	for(int i = 0; i < NumPoints; i++)
	{
		aPoints[i] = Center + direction(StartAngle + Sweep * i / (NumPoints - 1)) * Radius;
		aWidths[i] = StrokeWidth(Width);
	}
	FgfGlow::DrawStrip(pGraphics, aPoints, aWidths, NumPoints, StrokeColor(Color));
}

void CHookMagic::DrawRune(IGraphics *pGraphics, vec2 Center, float Size, float Angle, int Glyph, float Width, ColorRGBA Color)
{
	struct CStroke
	{
		float m_X0, m_Y0, m_X1, m_Y1;
	};
	static constexpr CStroke s_aBranches[] = {
		{0.0f, -0.5f, 0.35f, -0.2f},
		{0.0f, -0.5f, -0.35f, -0.2f},
		{0.0f, -0.1f, 0.35f, -0.4f},
		{0.0f, -0.1f, -0.35f, -0.4f},
		{0.0f, -0.1f, 0.35f, 0.2f},
		{0.0f, -0.1f, -0.35f, 0.2f},
		{0.0f, 0.2f, 0.35f, 0.5f},
		{0.0f, 0.2f, -0.35f, -0.1f},
		{-0.3f, 0.05f, 0.3f, 0.05f},
		{0.35f, -0.25f, 0.35f, 0.25f},
		{0.0f, 0.5f, 0.3f, 0.2f},
		{-0.35f, -0.5f, 0.35f, 0.5f},
	};
	static constexpr int NUM_BRANCHES = std::size(s_aBranches);

	unsigned Hash = (unsigned)Glyph * 2654435761u + 0x9e3779b9u;
	auto Next = [&]() {
		Hash ^= Hash << 13;
		Hash ^= Hash >> 17;
		Hash ^= Hash << 5;
		return Hash;
	};
	Next();

	const vec2 X = direction(Angle) * Size;
	const vec2 Y = vec2(-X.y, X.x);
	auto Point = [&](float PX, float PY) { return Center + X * PX + Y * PY; };

	if(Next() % 4 == 0)
	{
		DrawLine(pGraphics, Point(-0.25f, -0.5f), Point(-0.25f, 0.5f), Width, Color);
		DrawLine(pGraphics, Point(0.25f, -0.5f), Point(0.25f, 0.5f), Width, Color);
	}
	else
		DrawLine(pGraphics, Point(0.0f, -0.5f), Point(0.0f, 0.5f), Width, Color);

	const int NumBranches = 1 + Next() % 3;
	int Used = 0;
	for(int i = 0; i < NumBranches; i++)
	{
		int Branch = Next() % NUM_BRANCHES;
		while(Used & (1 << Branch))
			Branch = (Branch + 1) % NUM_BRANCHES;
		Used |= 1 << Branch;
		const CStroke &Stroke = s_aBranches[Branch];
		DrawLine(pGraphics, Point(Stroke.m_X0, Stroke.m_Y0), Point(Stroke.m_X1, Stroke.m_Y1), Width, Color);
	}
}

void CHookMagic::OnInit()
{
	m_GlowTexture = FgfGlow::CreateGlowTexture(Graphics());
	m_RingTexture = FgfGlow::CreateRingTexture(Graphics());
	m_BeamTexture = FgfGlow::CreateBeamTexture(Graphics());
}

void CHookMagic::OnReset()
{
	m_Tracker.Reset();
	for(CCircleState &State : m_aStates)
		State = CCircleState();
	m_Sparks.Clear();
	m_NumFlyingRunes = 0;
}

CHookMagic::CPalette CHookMagic::Palette()
{
	const ColorHSLA Hsl(g_Config.m_BcHookMagicColor);
	ColorHSLA AccentHsl = Hsl;
	AccentHsl.h = std::fmod(AccentHsl.h + 0.1f, 1.0f);
	AccentHsl.l = std::min(1.0f, AccentHsl.l * 1.1f);

	CPalette Result;
	Result.m_Main = color_cast<ColorRGBA>(Hsl);
	Result.m_Accent = color_cast<ColorRGBA>(AccentHsl);
	Result.m_Core = Whiten(Result.m_Main, 0.7f);
	return Result;
}

float CHookMagic::CircleRadius(const CHookGripTracker::CGrip &Grip, float Scale)
{
	return mix(26.0f, 40.0f, Grip.m_Charge) * Scale * (1.0f + (1.0f - Grip.m_Fade) * 0.45f);
}

void CHookMagic::Unfold(const CHookGripTracker::CGrip &Grip, const CPalette &Palette, float Scale)
{
	const float Alpha = Grip.m_Alpha;
	m_Sparks.AddRing(Grip.m_Pos, 0.35f, 8.0f * Scale, 70.0f * Scale, 0.8f * Alpha, Palette.m_Core);
	m_Sparks.AddRing(Grip.m_Pos, 0.5f, 4.0f * Scale, 50.0f * Scale, 0.6f * Alpha, Palette.m_Accent);

	const int Count = (int)(18 * std::max(MagicSparkles(), 0.3f));
	for(int i = 0; i < Count; i++)
	{
		CGlowParticles::CSpark *pSpark = m_Sparks.NewSpark();
		if(!pSpark)
			break;
		const float Angle = i * 2.0f * pi / Count + random_float(-0.15f, 0.15f);
		pSpark->m_Pos = Grip.m_Pos;
		pSpark->m_Vel = direction(Angle) * random_float(120.0f, 260.0f) * Scale;
		pSpark->m_Drag = 0.02f;
		pSpark->m_Stretch = 0.05f;
		pSpark->m_LifeSpan = random_float(0.3f, 0.5f);
		pSpark->m_StartSize = random_float(3.5f, 5.5f) * Scale;
		pSpark->m_EndSize = 1.0f * Scale;
		pSpark->m_StartAlpha = 0.9f * Alpha;
		pSpark->m_EndAlpha = 0.0f;
		pSpark->m_Color = i % 2 ? Palette.m_Core : Palette.m_Accent;
	}
}

void CHookMagic::Empower(const CHookGripTracker::CGrip &Grip, const CPalette &Palette, float Scale)
{
	const float Alpha = Grip.m_Alpha;
	const float Radius = CircleRadius(Grip, Scale);
	m_Sparks.AddRing(Grip.m_Pos, 0.5f, Radius * 1.6f, Radius * 4.2f, 0.7f * Alpha, Palette.m_Core);
	m_Sparks.AddRing(Grip.m_Pos, 0.6f, Radius * 1.2f, Radius * 3.0f, 0.5f * Alpha, Palette.m_Main);

	for(int i = 0; i < NUM_RUNES * 2; i++)
	{
		CGlowParticles::CSpark *pSpark = m_Sparks.NewSpark();
		if(!pSpark)
			break;
		const vec2 Out = direction(i * pi / NUM_RUNES);
		pSpark->m_Pos = Grip.m_Pos + Out * Radius * 0.9f;
		pSpark->m_Vel = Out * random_float(60.0f, 140.0f) * Scale + vec2(0.0f, -30.0f);
		pSpark->m_Drag = 0.1f;
		pSpark->m_Stretch = 0.03f;
		pSpark->m_LifeSpan = random_float(0.5f, 0.8f);
		pSpark->m_StartSize = random_float(3.0f, 5.0f) * Scale;
		pSpark->m_EndSize = 1.0f * Scale;
		pSpark->m_StartAlpha = 1.0f * Alpha;
		pSpark->m_EndAlpha = 0.0f;
		pSpark->m_Color = i % 2 ? Palette.m_Core : Palette.m_Accent;
	}
}

void CHookMagic::Shatter(int Owner, const CHookGripTracker::CGrip &Grip, const CPalette &Palette, float Scale)
{
	const CCircleState &State = m_aStates[Owner];
	const float Alpha = Grip.m_Alpha * std::max(State.m_Appear, 0.3f);
	const float Radius = CircleRadius(Grip, Scale);
	const float RingAngle = State.m_Angle * 0.35f;

	m_Sparks.AddRing(Grip.m_Pos, 0.4f, Radius * 1.8f, Radius * 3.2f, 0.5f * Alpha, Palette.m_Main);

	if(g_Config.m_BcHookMagicRunes)
	{
		for(int i = 0; i < NUM_RUNES && m_NumFlyingRunes < MAX_FLYING_RUNES; i++)
		{
			const float Angle = RingAngle + i * 2.0f * pi / NUM_RUNES;
			const vec2 Out = direction(Angle);
			CFlyingRune &Rune = m_aFlyingRunes[m_NumFlyingRunes++];
			Rune.m_Pos = Grip.m_Pos + Out * Radius * 0.89f;
			Rune.m_Vel = Out * random_float(50.0f, 110.0f) * Scale + vec2(0.0f, random_float(-40.0f, -10.0f));
			Rune.m_Angle = Angle + pi / 2.0f;
			Rune.m_AngularSpeed = random_float(-4.0f, 4.0f);
			Rune.m_Size = Radius * 0.17f;
			Rune.m_Life = 0.0f;
			Rune.m_LifeSpan = random_float(0.5f, 0.8f);
			Rune.m_Alpha = (Grip.m_Charge * NUM_RUNES > i ? 1.0f : 0.55f) * Alpha;
			Rune.m_Glyph = State.m_GlyphOffset + i;
		}
	}

	const int Count = (int)(16 * MagicSparkles());
	for(int i = 0; i < Count; i++)
	{
		CGlowParticles::CSpark *pSpark = m_Sparks.NewSpark();
		if(!pSpark)
			break;
		const vec2 Out = direction(random_float(0.0f, 2.0f * pi));
		pSpark->m_Pos = Grip.m_Pos + Out * Radius * random_float(0.3f, 1.0f);
		pSpark->m_Vel = Out * random_float(40.0f, 120.0f) * Scale;
		pSpark->m_Drag = 0.2f;
		pSpark->m_Stretch = 0.02f;
		pSpark->m_LifeSpan = random_float(0.4f, 0.7f);
		pSpark->m_StartSize = random_float(3.0f, 4.5f) * Scale;
		pSpark->m_EndSize = 0.5f * Scale;
		pSpark->m_StartAlpha = 0.8f * Alpha;
		pSpark->m_EndAlpha = 0.0f;
		pSpark->m_Color = random_float() < 0.5f ? Palette.m_Core : Palette.m_Accent;
	}
}

void CHookMagic::EmitMotes(int Owner, const CHookGripTracker::CGrip &Grip, const CPalette &Palette, float Passed, float Scale)
{
	CCircleState &State = m_aStates[Owner];
	const float Radius = CircleRadius(Grip, Scale);
	State.m_MoteAccumulator += Passed * mix(10.0f, 26.0f, Grip.m_Charge) * MagicSparkles();
	while(State.m_MoteAccumulator >= 1.0f)
	{
		State.m_MoteAccumulator -= 1.0f;
		CGlowParticles::CSpark *pSpark = m_Sparks.NewSpark();
		if(!pSpark)
			break;
		const vec2 Out = direction(random_float(0.0f, 2.0f * pi));
		pSpark->m_Pos = Grip.m_Pos + Out * Radius * std::sqrt(random_float()) * 1.05f;
		pSpark->m_Vel = vec2(random_float(-12.0f, 12.0f), random_float(-55.0f, -20.0f)) * Scale;
		pSpark->m_Drag = 0.6f;
		pSpark->m_LifeSpan = random_float(0.7f, 1.3f);
		pSpark->m_StartSize = random_float(2.5f, 4.5f) * Scale;
		pSpark->m_EndSize = 0.5f * Scale;
		pSpark->m_StartAlpha = 0.85f * Grip.m_Alpha;
		pSpark->m_EndAlpha = 0.0f;
		pSpark->m_Color = random_float() < 0.6f ? Palette.m_Core : Palette.m_Accent;
	}
}

void CHookMagic::DrawCircle(int Owner, const CHookGripTracker::CGrip &Grip, const CPalette &Palette, float Scale, float Time)
{
	const CCircleState &State = m_aStates[Owner];
	const vec2 Center = Grip.m_Pos;
	const float Radius = CircleRadius(Grip, Scale);
	const float Near = std::clamp((Grip.m_TeeDistance - 20.0f) / 45.0f, 0.35f, 1.0f);
	const float Intensity = Grip.m_Fade * Grip.m_Alpha * Near;
	const float Charge = Grip.m_Charge;
	const float Line = mix(1.1f, 1.6f, Charge) * Scale;
	const float Pulse = 0.85f + 0.15f * std::sin(Time * 6.0f);

	auto Stage = [&](float Start, float Length) {
		return std::clamp((State.m_Appear - Start) / Length, 0.0f, 1.0f);
	};
	const float OuterRing = Stage(0.0f, 0.55f);
	const float InnerRing = Stage(0.15f, 0.55f);
	const float Star = Stage(0.3f, 0.5f);
	const float Heart = Stage(0.45f, 0.55f);

	const float Angle = State.m_Angle;
	const float Spin = State.m_Spin;
	const ColorRGBA Main = Palette.m_Main;
	const ColorRGBA Core = Palette.m_Core;

	DrawArc(Graphics(), Center, Radius, Angle * 0.35f * Spin, OuterRing * 2.0f * pi, Line * 1.2f, Core.WithAlpha(0.9f * Intensity * Pulse));
	DrawArc(Graphics(), Center, Radius * 0.78f, -Angle * 0.35f * Spin, InnerRing * 2.0f * pi, Line, Main.WithAlpha(0.85f * Intensity));

	if(g_Config.m_BcHookMagicRunes)
	{
		for(int i = 0; i < NUM_RUNES; i++)
		{
			const float Reveal = std::clamp((InnerRing * NUM_RUNES - i), 0.0f, 1.0f);
			if(Reveal <= 0.0f)
				continue;
			const float Lit = std::clamp(Charge * NUM_RUNES - i, 0.0f, 1.0f);
			const float RuneAngle = Angle * 0.35f * Spin + i * 2.0f * pi / NUM_RUNES;
			const vec2 Pos = Center + direction(RuneAngle) * Radius * 0.89f;
			const ColorRGBA Color = Blend(Main, Core, Lit).WithAlpha(mix(0.45f, 1.0f, Lit) * Reveal * Intensity);
			DrawRune(Graphics(), Pos, Radius * 0.16f, RuneAngle + pi / 2.0f, State.m_GlyphOffset + i, Line * mix(0.7f, 0.95f, Lit), Color);
		}
	}
	else
	{
		for(int i = 0; i < NUM_RUNES * 2; i++)
		{
			const float TickAngle = Angle * 0.35f * Spin + i * pi / NUM_RUNES;
			const vec2 Out = direction(TickAngle);
			DrawLine(Graphics(), Center + Out * Radius * 0.82f, Center + Out * Radius * (i % 2 ? 0.88f : 0.95f), Line * 0.8f, Main.WithAlpha(0.8f * InnerRing * Intensity));
		}
	}

	const float StarRadius = Radius * 0.74f;
	for(int Triangle = 0; Triangle < 2; Triangle++)
	{
		const float Base = -Angle * 0.6f * Spin + Triangle * pi / 3.0f - pi / 2.0f;
		for(int Edge = 0; Edge < 3; Edge++)
		{
			const float EdgeProgress = std::clamp(Star * 3.0f - Edge, 0.0f, 1.0f);
			if(EdgeProgress <= 0.0f)
				continue;
			const vec2 From = Center + direction(Base + Edge * 2.0f * pi / 3.0f) * StarRadius;
			const vec2 To = Center + direction(Base + (Edge + 1) * 2.0f * pi / 3.0f) * StarRadius;
			DrawLine(Graphics(), From, mix(From, To, EdgeProgress), Line * 0.9f, Main.WithAlpha(0.75f * Intensity));
		}
	}

	const float InnerRadius = Radius * 0.37f;
	DrawArc(Graphics(), Center, InnerRadius, Angle * Spin, Heart * 2.0f * pi, Line, Core.WithAlpha(0.85f * Intensity));
	for(int i = 0; i < 8; i++)
	{
		const float Reveal = std::clamp(Heart * 8.0f - i, 0.0f, 1.0f);
		const vec2 Out = direction(Angle * Spin + i * pi / 4.0f);
		DrawLine(Graphics(), Center + Out * InnerRadius, Center + Out * InnerRadius * (i % 2 ? 1.2f : 1.35f), Line * 0.8f, Core.WithAlpha(0.8f * Reveal * Intensity));
	}
	for(int Edge = 0; Edge < 4; Edge++)
	{
		const float Base = Angle * 1.4f * Spin;
		const vec2 From = Center + direction(Base + Edge * pi / 2.0f) * InnerRadius * 0.7f;
		const vec2 To = Center + direction(Base + (Edge + 1) * pi / 2.0f) * InnerRadius * 0.7f;
		DrawLine(Graphics(), From, To, Line * 0.8f, Palette.m_Accent.WithAlpha(0.8f * Heart * Intensity));
	}

	const float Dashes = std::clamp((Charge - 0.4f) / 0.4f, 0.0f, 1.0f) * State.m_Appear;
	if(Dashes > 0.0f)
	{
		const int NumDashes = 20;
		for(int i = 0; i < NumDashes; i++)
		{
			const float DashAngle = -Angle * 0.2f * Spin + i * 2.0f * pi / NumDashes;
			DrawArc(Graphics(), Center, Radius * 1.16f, DashAngle, 2.0f * pi / NumDashes * 0.45f, Line * 0.9f, Palette.m_Accent.WithAlpha(0.75f * Dashes * Intensity));
		}
	}
}

void CHookMagic::Draw(const CPalette &Palette, float Scale, float Time)
{
	bool AnyCircle = false;
	for(int i = 0; i < MAX_CLIENTS; i++)
		AnyCircle |= m_Tracker.Grip(i).m_Fade > 0.0f;
	if(!AnyCircle && !m_Sparks.HasAny() && m_NumFlyingRunes == 0)
		return;

	Graphics()->BlendAdditive();
	Graphics()->QuadsSetRotation(0.0f);

	if(AnyCircle)
	{
		Graphics()->TextureSet(m_GlowTexture);
		Graphics()->QuadsBegin();
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			const CHookGripTracker::CGrip &Grip = m_Tracker.Grip(i);
			if(Grip.m_Fade <= 0.0f)
				continue;
			const CCircleState &State = m_aStates[i];
			const float Near = std::clamp((Grip.m_TeeDistance - 20.0f) / 45.0f, 0.35f, 1.0f);
			const float Intensity = Grip.m_Fade * Grip.m_Alpha * State.m_Appear * Near;
			const float Radius = CircleRadius(Grip, Scale);

			Graphics()->SetColor(Palette.m_Main.WithAlpha(mix(0.18f, 0.35f, Grip.m_Charge) * Intensity));
			IGraphics::CQuadItem Halo(Grip.m_Pos.x, Grip.m_Pos.y, Radius * 3.4f, Radius * 3.4f);
			Graphics()->QuadsDraw(&Halo, 1);
			const float Heart = (0.8f + 0.2f * std::sin(Time * 7.0f)) * Radius * 0.8f;
			Graphics()->SetColor(Palette.m_Core.WithAlpha(0.45f * Intensity));
			IGraphics::CQuadItem Core(Grip.m_Pos.x, Grip.m_Pos.y, Heart, Heart);
			Graphics()->QuadsDraw(&Core, 1);

			if(Grip.m_FullyCharged)
			{
				for(int Orb = 0; Orb < 3; Orb++)
				{
					const vec2 Pos = Grip.m_Pos + direction(State.m_Angle * 1.2f * State.m_Spin + Orb * 2.0f * pi / 3.0f) * Radius * 1.32f;
					Graphics()->SetColor(Palette.m_Accent.WithAlpha(0.7f * Intensity));
					IGraphics::CQuadItem Glow(Pos.x, Pos.y, 16.0f * Scale, 16.0f * Scale);
					Graphics()->QuadsDraw(&Glow, 1);
					Graphics()->SetColor(Palette.m_Core.WithAlpha(0.95f * Intensity));
					IGraphics::CQuadItem Spot(Pos.x, Pos.y, 5.0f * Scale, 5.0f * Scale);
					Graphics()->QuadsDraw(&Spot, 1);
				}
			}
		}
		Graphics()->QuadsEnd();
	}

	if(AnyCircle)
	{
		Graphics()->BlendNormal();
		FgfGlow::BeginStrips(Graphics(), m_BeamTexture);
		s_ShadowPass = true;
		s_ShadowColor = ColorRGBA(Palette.m_Main.r * 0.25f, Palette.m_Main.g * 0.2f, Palette.m_Main.b * 0.35f, 1.0f);
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			const CHookGripTracker::CGrip &Grip = m_Tracker.Grip(i);
			if(Grip.m_Fade > 0.0f)
				DrawCircle(i, Grip, Palette, Scale, Time);
		}
		s_ShadowPass = false;
		Graphics()->QuadsEnd();
		Graphics()->BlendAdditive();
	}

	FgfGlow::BeginStrips(Graphics(), m_BeamTexture);
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const CHookGripTracker::CGrip &Grip = m_Tracker.Grip(i);
		if(Grip.m_Fade > 0.0f)
			DrawCircle(i, Grip, Palette, Scale, Time);
	}
	for(int i = 0; i < m_NumFlyingRunes; i++)
	{
		const CFlyingRune &Rune = m_aFlyingRunes[i];
		const float Progress = Rune.m_Life / Rune.m_LifeSpan;
		const float Alpha = Rune.m_Alpha * (1.0f - Progress * Progress);
		DrawRune(Graphics(), Rune.m_Pos, Rune.m_Size * mix(1.0f, 1.4f, Progress), Rune.m_Angle, Rune.m_Glyph, 1.2f * Scale, Palette.m_Core.WithAlpha(Alpha));
	}
	Graphics()->QuadsEnd();

	m_Sparks.Draw(Graphics(), m_GlowTexture, m_RingTexture);
	Graphics()->BlendNormal();
}

void CHookMagic::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(BcFxSuppressed(GameClient()))
		return;

	const float Time = LocalTime();
	const float Passed = std::clamp(Time - m_LastTime, 0.0f, 0.1f) * GameClient()->GetAnimationPlaybackSpeed();
	m_LastTime = Time;

	if(g_Config.m_BcHookStyle != FGF_HOOK_STYLE_MAGIC)
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
				m_aStates[ClientId] = CCircleState();
				m_aStates[ClientId].m_Spin = random_float() < 0.5f ? -1.0f : 1.0f;
				m_aStates[ClientId].m_Angle = random_float(0.0f, 2.0f * pi);
				m_aStates[ClientId].m_GlyphOffset = (int)random_float(0.0f, 1000.0f);
				Unfold(Grip, CurrentPalette, Scale);
				break;
			case CHookGripTracker::EEvent::CHARGED:
				Empower(Grip, CurrentPalette, Scale);
				break;
			case CHookGripTracker::EEvent::RELEASE:
				Shatter(ClientId, Grip, CurrentPalette, Scale);
				break;
			}
		});

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const CHookGripTracker::CGrip &Grip = m_Tracker.Grip(i);
		CCircleState &State = m_aStates[i];
		if(Grip.m_Holding)
		{
			State.m_Appear = std::min(1.0f, State.m_Appear + Passed / APPEAR_TIME);
			EmitMotes(i, Grip, CurrentPalette, Passed, Scale);
		}
		State.m_Angle += Passed * mix(0.8f, 2.2f, Grip.m_Charge) * MagicSpin();
	}

	for(int i = 0; i < m_NumFlyingRunes;)
	{
		CFlyingRune &Rune = m_aFlyingRunes[i];
		Rune.m_Life += Passed;
		if(Rune.m_Life >= Rune.m_LifeSpan)
		{
			Rune = m_aFlyingRunes[--m_NumFlyingRunes];
			continue;
		}
		Rune.m_Vel *= std::pow(0.15f, Passed);
		Rune.m_Pos += Rune.m_Vel * Passed;
		Rune.m_Angle += Rune.m_AngularSpeed * Passed;
		i++;
	}

	m_Sparks.Update(Passed);
	Draw(CurrentPalette, Scale, Time);
}
