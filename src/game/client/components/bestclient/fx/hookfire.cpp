/* Copyright © 2026 BestProject Team */
#include "hookfire.h"

#include <base/math.h>

#include <engine/image.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include "own_tee.h"

#include <algorithm>
#include <cmath>

static constexpr float FADE_TIME = 0.35f;
static constexpr float APPEAR_TIME = 0.25f;
static constexpr float SCORCH_TIME = 2.5f;
static constexpr int CELL_WIDTH = 64;
static constexpr int CELL_HEIGHT = 128;

static float FireIntensity() { return g_Config.m_BcHookFireIntensity / 100.0f; }
static float FireEmbers() { return g_Config.m_BcHookFireEmbers / 100.0f; }

static float Near(const CHookGripTracker::CGrip &Grip)
{
	return std::clamp((Grip.m_TeeDistance - 16.0f) / 45.0f, 0.6f, 1.0f);
}

static float TongueDistance(float u, float y, float CenterX, float Bottom, float Top, float HalfWidth, float Phase, float Curl)
{
	const float Along = (y - Bottom) / (Top - Bottom);
	if(Along < 0.0f || Along > 1.0f)
		return 1.0f;
	float Half;
	if(Along < 0.3f)
	{
		const float Belly = (0.3f - Along) / 0.3f;
		Half = HalfWidth * std::sqrt(std::max(0.0f, 1.0f - Belly * Belly));
	}
	else
	{
		const float Taper = (Along - 0.3f) / 0.7f;
		Half = HalfWidth * std::pow(1.0f - Taper, 1.3f) * (1.0f + 0.07f * std::sin(Taper * 9.0f + Phase));
	}
	const float Middle = CenterX + Curl * std::sin(Along * pi * 1.4f + Phase) * std::pow(Along, 1.6f);
	return std::abs(u - Middle) - Half;
}

static IGraphics::CTextureHandle CreateFlameAtlas(IGraphics *pGraphics)
{
	CImageInfo Image;
	Image.m_Width = CELL_WIDTH * CHookFire::NUM_VARIANTS;
	Image.m_Height = CELL_HEIGHT;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	Image.Allocate();

	for(int Variant = 0; Variant < CHookFire::NUM_VARIANTS; Variant++)
	{
		for(int py = 0; py < CELL_HEIGHT; py++)
		{
			for(int px = 0; px < CELL_WIDTH; px++)
			{
				const float u = (px + 0.5f) / CELL_WIDTH * 2.0f - 1.0f;
				const float y = 1.0f - (py + 0.5f) / CELL_HEIGHT;

				float Distance;
				switch(Variant)
				{
				case 0:
					Distance = TongueDistance(u, y, 0.0f, 0.02f, 0.97f, 0.8f, 0.0f, 0.16f);
					break;
				case 1:
					Distance = TongueDistance(u, y, 0.0f, 0.02f, 0.9f, 0.82f, 2.1f, -0.24f);
					break;
				case 2:
					Distance = std::min(TongueDistance(u, y, 0.08f, 0.02f, 0.98f, 0.72f, 4.0f, 0.12f),
						TongueDistance(u, y, -0.4f, 0.1f, 0.62f, 0.4f, 1.0f, -0.12f));
					break;
				default:
					Distance = std::min(TongueDistance(u, y, -0.06f, 0.02f, 0.93f, 0.74f, 1.2f, -0.18f),
						TongueDistance(u, y, 0.42f, 0.12f, 0.7f, 0.38f, 3.0f, 0.14f));
					break;
				}

				const float Pixels = Distance * CELL_WIDTH * 0.5f;
				const float Alpha = std::clamp(0.5f - Pixels, 0.0f, 1.0f);
				float Shade = mix(0.58f, 1.0f, std::clamp((-Pixels - 2.4f) / 1.2f, 0.0f, 1.0f));
				Shade *= mix(1.0f, 0.9f, y);

				uint8_t *pPixel = &Image.m_pData[(py * Image.m_Width + Variant * CELL_WIDTH + px) * 4];
				pPixel[0] = pPixel[1] = pPixel[2] = (uint8_t)round_to_int(Shade * 255.0f);
				pPixel[3] = (uint8_t)round_to_int(Alpha * 255.0f);
			}
		}
	}
	return pGraphics->LoadTextureRawMove(Image, 0, "fgf_flames");
}

void CHookFire::OnInit()
{
	m_FlameTexture = CreateFlameAtlas(Graphics());
	m_GlowTexture = FgfGlow::CreateGlowTexture(Graphics());
	m_RingTexture = FgfGlow::CreateRingTexture(Graphics());
}

void CHookFire::OnReset()
{
	m_Tracker.Reset();
	for(CFireState &State : m_aStates)
		State = CFireState();
	m_NumFlames = 0;
	m_NumScorches = 0;
	m_Embers.Clear();
	m_Smoke.Clear();
}

CHookFire::CPalette CHookFire::Palette()
{
	const ColorHSLA Hsl(g_Config.m_BcHookFireColor);
	ColorHSLA OuterHsl = Hsl;
	OuterHsl.h = std::fmod(OuterHsl.h + 0.97f, 1.0f);
	OuterHsl.l *= 0.82f;
	ColorHSLA InnerHsl = Hsl;
	InnerHsl.h = std::fmod(InnerHsl.h + 0.075f, 1.0f);
	InnerHsl.l = std::min(0.93f, InnerHsl.l + 0.24f);

	CPalette Result;
	Result.m_Outer = color_cast<ColorRGBA>(OuterHsl);
	Result.m_Middle = color_cast<ColorRGBA>(Hsl);
	Result.m_Inner = color_cast<ColorRGBA>(InnerHsl);
	return Result;
}

void CHookFire::EmitFlame(vec2 Pos, vec2 Vel, float Size, float LifeSpan, float Alpha, float ColumnX, float Pull)
{
	if(m_NumFlames >= MAX_FLAMES)
		return;
	CFlameParticle &Flame = m_aFlames[m_NumFlames++];
	Flame.m_Pos = Pos;
	Flame.m_Vel = Vel;
	Flame.m_Life = 0.0f;
	Flame.m_LifeSpan = LifeSpan;
	Flame.m_Size = Size;
	Flame.m_Phase = random_float(0.0f, 2.0f * pi);
	Flame.m_Alpha = Alpha;
	Flame.m_ColumnX = ColumnX;
	Flame.m_Pull = Pull;
	Flame.m_Variant = (int)random_float(0.0f, NUM_VARIANTS - 0.01f);
}

void CHookFire::EmitEmber(vec2 Pos, vec2 Vel, float Alpha)
{
	const CPalette CurrentPalette = Palette();
	CGlowParticles::CSpark *pSpark = m_Embers.NewSpark();
	if(!pSpark)
		return;
	pSpark->m_Pos = Pos;
	pSpark->m_Vel = Vel;
	pSpark->m_Drag = 0.6f;
	pSpark->m_Gravity = 30.0f;
	pSpark->m_Stretch = 0.02f;
	pSpark->m_LifeSpan = random_float(0.7f, 1.4f);
	pSpark->m_StartSize = random_float(2.5f, 4.0f);
	pSpark->m_EndSize = 1.0f;
	pSpark->m_StartAlpha = Alpha;
	pSpark->m_EndAlpha = 0.0f;
	pSpark->m_Color = CurrentPalette.m_Inner;
	pSpark->m_ShiftColor = true;
	pSpark->m_EndColor = CurrentPalette.m_Outer;
}

void CHookFire::EmitSmoke(vec2 Pos, vec2 Vel, float Size, float Alpha)
{
	if(!g_Config.m_BcHookFireSmoke)
		return;
	CGlowParticles::CSpark *pSpark = m_Smoke.NewSpark();
	if(!pSpark)
		return;
	pSpark->m_Pos = Pos;
	pSpark->m_Vel = Vel;
	pSpark->m_Drag = 0.5f;
	pSpark->m_Gravity = -25.0f;
	pSpark->m_LifeSpan = random_float(1.1f, 1.8f);
	pSpark->m_StartSize = Size;
	pSpark->m_EndSize = Size * 3.0f;
	pSpark->m_StartAlpha = Alpha;
	pSpark->m_EndAlpha = 0.0f;
	const float Grey = random_float(0.22f, 0.32f);
	pSpark->m_Color = ColorRGBA(Grey, Grey * 0.96f, Grey * 0.92f, 1.0f);
}

vec2 CHookFire::FlameDirection(const CHookGripTracker::CGrip &Grip)
{
	return normalize(vec2(Grip.m_Dir.x * 0.5f, -1.0f));
}

void CHookFire::Ignite(int Owner, const CHookGripTracker::CGrip &Grip, float Scale)
{
	m_aStates[Owner] = CFireState();
	const vec2 Up = FlameDirection(Grip);
	const float Alpha = Grip.m_Alpha * Near(Grip);
	for(int i = 0; i < 18; i++)
	{
		const float Across = random_float(-1.0f, 1.0f);
		EmitFlame(Grip.m_Pos + vec2(Across * 12.0f, 0.0f) * Scale,
			(Up * random_float(180.0f, 320.0f) + vec2(Across * 60.0f, 0.0f)) * Scale,
			random_float(16.0f, 24.0f) * Scale, random_float(0.35f, 0.55f), Alpha, Grip.m_Pos.x, 3.0f);
	}
	for(int i = 0; i < 10; i++)
	{
		const vec2 Out = direction(i * 2.0f * pi / 10.0f + random_float(-0.2f, 0.2f));
		EmitFlame(Grip.m_Pos + Out * 6.0f * Scale, Out * random_float(140.0f, 220.0f) * Scale,
			random_float(10.0f, 15.0f) * Scale, random_float(0.3f, 0.45f), Alpha, Grip.m_Pos.x, 0.0f);
	}
	for(int i = 0; i < (int)(16 * std::max(FireEmbers(), 0.3f)); i++)
		EmitEmber(Grip.m_Pos, direction(random_float(0.0f, 2.0f * pi)) * random_float(120.0f, 280.0f) + vec2(0.0f, -80.0f), Alpha);
}

void CHookFire::Roar(int Owner, const CHookGripTracker::CGrip &Grip, float Scale)
{
	m_aStates[Owner].m_Flare = 1.0f;
	const vec2 Up = FlameDirection(Grip);
	const float Alpha = Grip.m_Alpha * Near(Grip);
	for(int i = 0; i < 16; i++)
		EmitFlame(Grip.m_Pos + vec2(random_float(-16.0f, 16.0f), 0.0f) * Scale, Up * random_float(220.0f, 360.0f) * Scale,
			random_float(18.0f, 26.0f) * Scale, random_float(0.45f, 0.65f), Alpha, Grip.m_Pos.x, 4.0f);
	for(int i = 0; i < (int)(20 * std::max(FireEmbers(), 0.3f)); i++)
		EmitEmber(Grip.m_Pos, vec2(random_float(-120.0f, 120.0f), random_float(-320.0f, -120.0f)), Alpha);
}

void CHookFire::DieDown(const CHookGripTracker::CGrip &Grip, float Scale)
{
	const float Alpha = Grip.m_Alpha * Near(Grip);
	const vec2 Up = FlameDirection(Grip);
	for(int i = 0; i < 7; i++)
		EmitSmoke(Grip.m_Pos + Up * random_float(4.0f, 24.0f) * Scale + vec2(random_float(-14.0f, 14.0f), 0.0f) * Scale,
			vec2(random_float(-30.0f, 30.0f), random_float(-80.0f, -30.0f)) * Scale, random_float(10.0f, 15.0f) * Scale, 0.45f * Alpha);
	for(int i = 0; i < (int)(10 * FireEmbers()); i++)
		EmitEmber(Grip.m_Pos, vec2(random_float(-90.0f, 90.0f), random_float(-200.0f, -60.0f)), Alpha);
	for(int i = 0; i < 8; i++)
		EmitFlame(Grip.m_Pos + vec2(random_float(-14.0f, 14.0f), 0.0f) * Scale, (Up * random_float(60.0f, 140.0f) + vec2(random_float(-50.0f, 50.0f), 0.0f)) * Scale,
			random_float(8.0f, 13.0f) * Scale, random_float(0.25f, 0.4f), Alpha, Grip.m_Pos.x, 0.0f);

	if(m_NumScorches < MAX_SCORCHES)
		m_aScorches[m_NumScorches++] = {Grip.m_Pos, 0.0f, mix(40.0f, 64.0f, Grip.m_Charge) * Scale, Grip.m_Alpha};
}

void CHookFire::Burn(int Owner, const CHookGripTracker::CGrip &Grip, float Passed, float Scale)
{
	CFireState &State = m_aStates[Owner];
	const float Alpha = Grip.m_Alpha * Near(Grip);
	const float Charge = Grip.m_Charge;
	const float Grow = State.m_Appear * mix(0.7f, 1.0f, Charge) * (1.0f + 0.45f * State.m_Flare);
	const vec2 Up = FlameDirection(Grip);
	const float BaseWidth = mix(16.0f, 28.0f, Charge) * Scale;

	State.m_FlameAccumulator += Passed * mix(55.0f, 95.0f, Charge) * FireIntensity();
	while(State.m_FlameAccumulator >= 1.0f)
	{
		State.m_FlameAccumulator -= 1.0f;
		const float Across = random_float(-1.0f, 1.0f) * std::abs(random_float(-1.0f, 1.0f) + random_float(-1.0f, 1.0f)) * 0.5f + random_float(-0.35f, 0.35f);
		const float Middle = 1.0f - std::min(1.0f, std::abs(Across));
		const vec2 Pos = Grip.m_Pos + vec2(Across * BaseWidth, random_float(-2.0f, 4.0f) * Scale);
		const vec2 Vel = Up * random_float(60.0f, 120.0f) * mix(0.8f, 1.2f, Middle) * Scale * Grow;
		EmitFlame(Pos, Vel, mix(13.0f, 25.0f, Middle) * Scale * Grow, random_float(0.4f, 0.65f) * mix(0.8f, 1.15f, Grow), Alpha, Grip.m_Pos.x + Up.x * 20.0f, 5.0f);
	}

	State.m_EmberAccumulator += Passed * mix(6.0f, 14.0f, Charge) * FireEmbers();
	while(State.m_EmberAccumulator >= 1.0f)
	{
		State.m_EmberAccumulator -= 1.0f;
		EmitEmber(Grip.m_Pos + vec2(random_float(-1.0f, 1.0f) * BaseWidth, random_float(-30.0f, -10.0f) * Scale),
			vec2(random_float(-40.0f, 40.0f), random_float(-160.0f, -70.0f)), Alpha);
	}

	State.m_SmokeAccumulator += Passed * mix(2.5f, 6.0f, Charge) * FireIntensity();
	while(State.m_SmokeAccumulator >= 1.0f)
	{
		State.m_SmokeAccumulator -= 1.0f;
		EmitSmoke(Grip.m_Pos + Up * 55.0f * Scale * Grow + vec2(random_float(-10.0f, 10.0f), 0.0f) * Scale,
			vec2(random_float(-12.0f, 12.0f), random_float(-60.0f, -35.0f)) * Scale, random_float(9.0f, 13.0f) * Scale, 0.18f * Alpha);
	}
}

void CHookFire::UpdateFlames(float Passed, float Time)
{
	for(int i = 0; i < m_NumFlames;)
	{
		CFlameParticle &Flame = m_aFlames[i];
		Flame.m_Life += Passed;
		if(Flame.m_Life >= Flame.m_LifeSpan)
		{
			Flame = m_aFlames[--m_NumFlames];
			continue;
		}
		Flame.m_Vel.y -= 170.0f * Passed;
		Flame.m_Vel.x += (Flame.m_ColumnX - Flame.m_Pos.x) * Flame.m_Pull * Passed;
		Flame.m_Vel.x += std::sin(Time * 6.3f + Flame.m_Phase + Flame.m_Pos.y * 0.045f) * 110.0f * Passed;
		Flame.m_Vel *= std::pow(0.35f, Passed);
		Flame.m_Pos += Flame.m_Vel * Passed;
		i++;
	}
}

void CHookFire::DrawFlames(const CPalette &Palette, float Time)
{
	int Count = 0;
	for(int i = 0; i < m_NumFlames; i++)
	{
		const CFlameParticle &Flame = m_aFlames[i];
		const float Progress = Flame.m_Life / Flame.m_LifeSpan;
		const float Swell = std::min(1.0f, Progress / 0.12f);
		const float Burn = 1.0f - 0.85f * std::pow(Progress, 1.4f);
		const float Size = Flame.m_Size * Swell * Burn;
		if(Size < 1.0f)
			continue;
		CFlameQuad &Quad = m_aQuads[Count++];
		const float Speed = length(Flame.m_Vel);
		Quad.m_Dir = normalize(vec2(Flame.m_Vel.x * 0.006f, -1.0f));
		Quad.m_Height = Size * (1.5f + std::min(0.9f, Speed * 0.004f));
		Quad.m_Width = Size;
		Quad.m_Base = Flame.m_Pos - Quad.m_Dir * Quad.m_Height * 0.3f;
		Quad.m_Heat = 1.0f - Progress;
		Quad.m_Variant = Flame.m_Variant;
		Quad.m_Flip = ((int)(Time * 9.0f + Flame.m_Phase)) % 2 == 0;
		Quad.m_Alpha = Flame.m_Alpha * std::min(1.0f, (1.0f - Progress) * 5.0f);
	}
	if(Count == 0)
		return;

	Graphics()->BlendNormal();
	Graphics()->TextureSet(m_FlameTexture);
	Graphics()->QuadsBegin();
	for(int Layer = 0; Layer < 3; Layer++)
	{
		for(int i = 0; i < Count; i++)
		{
			const CFlameQuad &Quad = m_aQuads[i];
			float LayerScale;
			ColorRGBA Color;
			if(Layer == 0)
			{
				LayerScale = 1.0f;
				Color = FgfGlow::Blend(Palette.m_Outer, ColorRGBA(Palette.m_Outer.r * 0.7f, Palette.m_Outer.g * 0.6f, Palette.m_Outer.b * 0.6f, 1.0f), 1.0f - Quad.m_Heat);
			}
			else if(Layer == 1)
			{
				LayerScale = 0.72f * std::clamp((Quad.m_Heat - 0.15f) / 0.3f, 0.0f, 1.0f);
				Color = Palette.m_Middle;
			}
			else
			{
				LayerScale = 0.45f * std::clamp((Quad.m_Heat - 0.45f) / 0.25f, 0.0f, 1.0f);
				Color = Palette.m_Inner;
			}
			const float Height = Quad.m_Height * LayerScale;
			const float Width = Quad.m_Width * LayerScale;
			if(Height < 1.5f || Width < 1.0f)
				continue;
			const vec2 Center = Quad.m_Base + Quad.m_Dir * (Quad.m_Height * (1.0f - LayerScale) * 0.12f + Height * 0.5f);
			const float U0 = (Quad.m_Variant + 0.02f) / NUM_VARIANTS;
			const float U1 = (Quad.m_Variant + 0.98f) / NUM_VARIANTS;
			if(Quad.m_Flip)
				Graphics()->QuadsSetSubset(U1, 0.0f, U0, 1.0f);
			else
				Graphics()->QuadsSetSubset(U0, 0.0f, U1, 1.0f);
			Graphics()->QuadsSetRotation(angle(Quad.m_Dir) + pi / 2.0f);
			Graphics()->SetColor(Color.WithAlpha(Quad.m_Alpha));
			IGraphics::CQuadItem Item(Center.x, Center.y, Width, Height);
			Graphics()->QuadsDraw(&Item, 1);
		}
	}
	Graphics()->QuadsEnd();
	Graphics()->QuadsSetRotation(0.0f);
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CHookFire::Draw(const CPalette &Palette, float Scale, float Time)
{
	bool AnyFire = false;
	for(int i = 0; i < MAX_CLIENTS; i++)
		AnyFire |= m_Tracker.Grip(i).m_Fade > 0.0f;
	if(!AnyFire && m_NumFlames == 0 && m_NumScorches == 0 && !m_Embers.HasAny() && !m_Smoke.HasAny())
		return;

	Graphics()->QuadsSetRotation(0.0f);
	Graphics()->BlendNormal();

	if(m_NumScorches > 0)
	{
		Graphics()->TextureSet(m_GlowTexture);
		Graphics()->QuadsBegin();
		for(int i = 0; i < m_NumScorches; i++)
		{
			const CScorch &Scorch = m_aScorches[i];
			const float Fade = 1.0f - Scorch.m_Life / SCORCH_TIME;
			Graphics()->SetColor(0.08f, 0.05f, 0.03f, 0.55f * Fade * Fade * Scorch.m_Alpha);
			IGraphics::CQuadItem Quad(Scorch.m_Pos.x, Scorch.m_Pos.y, Scorch.m_Size, Scorch.m_Size * 0.8f);
			Graphics()->QuadsDraw(&Quad, 1);
		}
		Graphics()->QuadsEnd();
	}
	m_Smoke.Draw(Graphics(), m_GlowTexture, m_RingTexture);

	if(AnyFire)
	{
		Graphics()->BlendAdditive();
		Graphics()->TextureSet(m_GlowTexture);
		Graphics()->QuadsBegin();
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			const CHookGripTracker::CGrip &Grip = m_Tracker.Grip(i);
			if(Grip.m_Fade <= 0.0f)
				continue;
			const float Flicker = 0.85f + 0.1f * std::sin(Time * 11.0f + i) + 0.05f * std::sin(Time * 23.0f + i);
			const float Light = Grip.m_Fade * Grip.m_Alpha * Near(Grip) * Flicker * m_aStates[i].m_Appear;
			const float Size = mix(100.0f, 150.0f, Grip.m_Charge) * Scale;
			const vec2 Center = Grip.m_Pos + FlameDirection(Grip) * 20.0f * Scale;
			Graphics()->SetColor(Palette.m_Middle.WithAlpha(0.3f * Light));
			IGraphics::CQuadItem Quad(Center.x, Center.y, Size, Size);
			Graphics()->QuadsDraw(&Quad, 1);
		}
		Graphics()->QuadsEnd();
	}

	DrawFlames(Palette, Time);

	Graphics()->BlendAdditive();
	m_Embers.Draw(Graphics(), m_GlowTexture, m_RingTexture);
	Graphics()->BlendNormal();
}

void CHookFire::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(BcFxSuppressed(GameClient()))
		return;

	const float Time = LocalTime();
	const float Passed = std::clamp(Time - m_LastTime, 0.0f, 0.1f) * GameClient()->GetAnimationPlaybackSpeed();
	m_LastTime = Time;

	if(g_Config.m_BcHookStyle != FGF_HOOK_STYLE_FIRE)
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
				Ignite(ClientId, Grip, Scale);
				break;
			case CHookGripTracker::EEvent::CHARGED:
				Roar(ClientId, Grip, Scale);
				break;
			case CHookGripTracker::EEvent::RELEASE:
				DieDown(Grip, Scale);
				break;
			}
		});

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const CHookGripTracker::CGrip &Grip = m_Tracker.Grip(i);
		CFireState &State = m_aStates[i];
		if(Grip.m_Holding)
		{
			State.m_Appear = std::min(1.0f, State.m_Appear + Passed / APPEAR_TIME);
			Burn(i, Grip, Passed, Scale);
		}
		State.m_Flare = std::max(0.0f, State.m_Flare - Passed / 0.6f);
	}

	for(int i = 0; i < m_NumScorches;)
	{
		m_aScorches[i].m_Life += Passed;
		if(m_aScorches[i].m_Life >= SCORCH_TIME)
		{
			m_aScorches[i] = m_aScorches[--m_NumScorches];
			continue;
		}
		i++;
	}

	UpdateFlames(Passed, Time);
	m_Embers.Update(Passed);
	m_Smoke.Update(Passed);
	Draw(CurrentPalette, Scale, Time);
}
