/* Copyright © 2026 BestProject Team */
#include "hooktroll.h"

#include <base/math.h>

#include <engine/shared/config.h>
#include <engine/storage.h>

#include "fgf_glow.h"
#include <game/client/gameclient.h>

#include "own_tee.h"

#include <algorithm>
#include <cmath>

static constexpr float FADE_TIME = 0.25f;
static constexpr float GRAVITY = 900.0f;

static const ColorRGBA INK(0.03f, 0.03f, 0.03f, 1.0f);
static const ColorRGBA GOO(0.97f, 0.97f, 0.96f, 1.0f);
static const ColorRGBA GOO_SHADE(0.72f, 0.73f, 0.75f, 1.0f);

static float Near(const CHookGripTracker::CGrip &Grip)
{
	return std::clamp((Grip.m_TeeDistance - 16.0f) / 45.0f, 0.5f, 1.0f);
}

void CHookTroll::OnInit()
{
	m_FaceTexture = Graphics()->LoadTexture("BestClient/trollface.png", IStorage::TYPE_ALL);
	m_DiscTexture = FgfGlow::CreateDiscTexture(Graphics());
}

void CHookTroll::OnReset()
{
	m_Tracker.Reset();
	for(CTrollState &State : m_aStates)
		State = CTrollState();
	m_NumDrips = 0;
	m_NumFaces = 0;
}

void CHookTroll::DrawFace(vec2 Pos, float Size, float Angle, bool Flip, float Alpha)
{
	if(Size < 1.0f || Alpha <= 0.0f)
		return;
	Graphics()->BlendNormal();
	Graphics()->TextureSet(m_FaceTexture);
	Graphics()->QuadsBegin();
	if(Flip)
		Graphics()->QuadsSetSubset(1.0f, 0.0f, 0.0f, 1.0f);
	else
		Graphics()->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
	Graphics()->QuadsSetRotation(Angle);
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
	IGraphics::CQuadItem Quad(Pos.x, Pos.y, Size, Size * 540.0f / 666.0f);
	Graphics()->QuadsDraw(&Quad, 1);
	Graphics()->QuadsSetRotation(0.0f);
	Graphics()->QuadsEnd();
}

void CHookTroll::DrawGoo(const vec2 *pPoints, const float *pWidths, int NumPoints, float Alpha)
{
	NumPoints = std::min<int>(NumPoints, FgfGlow::MAX_STRIP_POINTS);
	if(NumPoints < 2)
		return;
	float aWidths[FgfGlow::MAX_STRIP_POINTS];
	vec2 aPoints[FgfGlow::MAX_STRIP_POINTS];

	Graphics()->BlendNormal();
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	for(int i = 0; i < NumPoints; i++)
		aWidths[i] = pWidths[i] + 1.1f;
	FgfGlow::DrawStrip(Graphics(), pPoints, aWidths, NumPoints, INK.WithAlpha(Alpha));
	FgfGlow::DrawStrip(Graphics(), pPoints, pWidths, NumPoints, GOO_SHADE.WithAlpha(Alpha));
	for(int i = 0; i < NumPoints; i++)
	{
		aPoints[i] = pPoints[i] - vec2(0.0f, pWidths[i] * 0.22f);
		aWidths[i] = pWidths[i] * 0.72f;
	}
	FgfGlow::DrawStrip(Graphics(), aPoints, aWidths, NumPoints, GOO.WithAlpha(Alpha));
	for(int i = 0; i < NumPoints; i++)
	{
		aPoints[i] = pPoints[i] - vec2(0.0f, pWidths[i] * 0.5f);
		aWidths[i] = std::max(0.0f, pWidths[i] * 0.18f - 0.1f);
	}
	FgfGlow::DrawStrip(Graphics(), aPoints, aWidths, NumPoints, ColorRGBA(1.0f, 1.0f, 1.0f, Alpha));
	Graphics()->QuadsEnd();
}

void CHookTroll::EmitDrip(vec2 Pos, vec2 Vel, float Radius, float Alpha)
{
	if(m_NumDrips >= MAX_DRIPS)
		return;
	m_aDrips[m_NumDrips++] = {Pos, Vel, Radius, 0.0f, Alpha};
}

void CHookTroll::DrawSplat(vec2 Center, vec2 Normal, float Radius, float Seed, float Alpha)
{
	if(Radius < 1.0f)
		return;
	const float Time = LocalTime();
	vec2 aPoints[SPLAT_POINTS + 1];
	for(int i = 0; i <= SPLAT_POINTS; i++)
	{
		const float a = i * 2.0f * pi / SPLAT_POINTS;
		float r = 1.0f + 0.22f * std::sin(a * 3.0f + Seed) + 0.12f * std::sin(a * 7.0f + Seed * 2.3f) + 0.05f * std::sin(a * 4.0f + Time * 3.0f + Seed);
		aPoints[i] = Center + direction(a) * Radius * r;
	}

	Graphics()->BlendNormal();
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	for(int Layer = 0; Layer < 2; Layer++)
	{
		const float Grow = Layer == 0 ? 1.0f + 1.4f / Radius : 1.0f;
		const ColorRGBA Color = Layer == 0 ? INK.WithAlpha(Alpha) : GOO.WithAlpha(Alpha);
		for(int i = 0; i < SPLAT_POINTS; i++)
		{
			Graphics()->SetColor(Color);
			const IGraphics::CFreeformItem Item(Center, Center, Center + (aPoints[i] - Center) * Grow, Center + (aPoints[i + 1] - Center) * Grow);
			Graphics()->QuadsDrawFreeform(&Item, 1);
		}
	}
	Graphics()->QuadsEnd();

	for(int t = 0; t < 7; t++)
	{
		const float a = t * 2.0f * pi / 7.0f + Seed;
		const float Length = Radius * (0.7f + 0.5f * std::sin(Seed * 3.0f + t * 1.7f));
		const float Reach = Length * (0.9f + 0.1f * std::sin(Time * 2.0f + t));
		vec2 aTendril[6];
		float aWidths[6];
		for(int i = 0; i < 6; i++)
		{
			const float u = i / 5.0f;
			aTendril[i] = Center + direction(a + std::sin(u * 3.0f + t) * 0.25f) * (Radius * 0.8f + Reach * u);
			aWidths[i] = Radius * 0.16f * (1.0f - u * 0.85f);
		}
		DrawGoo(aTendril, aWidths, 6, Alpha);
	}
	(void)Normal;
}

void CHookTroll::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(BcFxSuppressed(GameClient()))
		return;

	const float Time = LocalTime();
	const float Passed = std::clamp(Time - m_LastTime, 0.0f, 0.05f) * GameClient()->GetAnimationPlaybackSpeed();
	m_LastTime = Time;

	if(g_Config.m_BcHookStyle != FGF_HOOK_STYLE_TROLL)
	{
		OnReset();
		return;
	}
	const float Scale = g_Config.m_BcHookSize / 100.0f;

	m_Tracker.Update(GameClient(), Passed, g_Config.m_BcHookChargeTime / 1000.0f, FADE_TIME,
		[&](int ClientId, CHookGripTracker::EEvent Event, const CHookGripTracker::CGrip &Grip) {
			CTrollState &State = m_aStates[ClientId];
			const float Alpha = Grip.m_Alpha * Near(Grip);
			switch(Event)
			{
			case CHookGripTracker::EEvent::GRAB:
				State = CTrollState();
				State.m_Seed = random_float(0.0f, 100.0f);
				State.m_FaceScaleVel = 9.0f;
				for(int i = 0; i < 14; i++)
				{
					const vec2 Dir = direction(angle(Grip.m_Dir) + random_float(-1.3f, 1.3f));
					EmitDrip(Grip.m_Pos, Dir * random_float(120.0f, 300.0f) * Scale, random_float(1.5f, 3.2f) * Scale, Alpha);
				}
				break;
			case CHookGripTracker::EEvent::CHARGED:
				State.m_Laugh = 1.0f;
				State.m_FaceScaleVel += 6.0f;
				break;
			case CHookGripTracker::EEvent::RELEASE:
				if(State.m_FaceScale > 0.1f && m_NumFaces < MAX_FACES)
				{
					const float Size = mix(34.0f, 52.0f, Grip.m_Charge) * State.m_FaceScale * Scale;
					m_aFaces[m_NumFaces++] = {Grip.m_Pos + Grip.m_Dir * 6.0f, (Grip.m_Dir * 140.0f + vec2(random_float(-60.0f, 60.0f), -200.0f)) * Scale,
						Size, 0.0f, random_float(-7.0f, 7.0f), 0.0f, Alpha, Grip.m_Dir.x < 0.0f};
				}
				for(int i = 0; i < 12; i++)
					EmitDrip(Grip.m_Pos + vec2(random_float(-10.0f, 10.0f), random_float(-6.0f, 6.0f)) * Scale, vec2(random_float(-50.0f, 50.0f), random_float(-80.0f, 20.0f)) * Scale, random_float(1.8f, 3.6f) * Scale, Alpha);
				break;
			}
		});

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const CHookGripTracker::CGrip &Grip = m_Tracker.Grip(i);
		CTrollState &State = m_aStates[i];
		const float Target = Grip.m_Holding ? 1.0f : 0.0f;
		State.m_FaceScaleVel += ((Target - State.m_FaceScale) * 120.0f - State.m_FaceScaleVel * 10.0f) * Passed;
		State.m_FaceScale = std::max(0.0f, State.m_FaceScale + State.m_FaceScaleVel * Passed);
		State.m_Splat = Grip.m_Holding ? std::min(1.0f, State.m_Splat + Passed * 8.0f) : std::max(0.0f, State.m_Splat - Passed * 5.0f);
		State.m_Laugh = std::max(0.0f, State.m_Laugh - Passed * 1.5f);
		if(Grip.m_Holding)
		{
			State.m_DripAccumulator += Passed * mix(1.5f, 4.0f, Grip.m_Charge);
			while(State.m_DripAccumulator >= 1.0f)
			{
				State.m_DripAccumulator -= 1.0f;
				EmitDrip(Grip.m_Pos + vec2(random_float(-8.0f, 8.0f), 10.0f) * Scale, vec2(0.0f, 10.0f), random_float(1.6f, 2.6f) * Scale, Grip.m_Alpha * Near(Grip));
			}
		}
	}

	for(int i = 0; i < m_NumDrips;)
	{
		CDrip &Drip = m_aDrips[i];
		Drip.m_Life += Passed;
		Drip.m_Vel.y += GRAVITY * Passed;
		const vec2 Next = Drip.m_Pos + Drip.m_Vel * Passed;
		if(Drip.m_Life > 2.5f || Collision()->CheckPoint(Next))
		{
			Drip = m_aDrips[--m_NumDrips];
			continue;
		}
		Drip.m_Pos = Next;
		i++;
	}
	for(int i = 0; i < m_NumFaces;)
	{
		CLooseFace &Face = m_aFaces[i];
		Face.m_Life += Passed;
		if(Face.m_Life > 0.9f)
		{
			Face = m_aFaces[--m_NumFaces];
			continue;
		}
		Face.m_Vel.y += GRAVITY * Passed;
		Face.m_Pos += Face.m_Vel * Passed;
		Face.m_Angle += Face.m_Spin * Passed;
		i++;
	}

	if(m_NumDrips > 0)
	{
		Graphics()->BlendNormal();
		Graphics()->TextureSet(m_DiscTexture);
		Graphics()->QuadsBegin();
		for(int Layer = 0; Layer < 3; Layer++)
		{
			for(int i = 0; i < m_NumDrips; i++)
			{
				const CDrip &Drip = m_aDrips[i];
				const float Speed = length(Drip.m_Vel);
				const float Fade = Drip.m_Alpha * std::min(1.0f, (2.5f - Drip.m_Life) * 3.0f);
				const float Long = Drip.m_Radius * 2.0f + std::min(Speed * 0.012f, Drip.m_Radius * 2.5f);
				Graphics()->QuadsSetRotation(Speed > 1.0f ? angle(Drip.m_Vel) : 0.0f);
				if(Layer == 0)
				{
					Graphics()->SetColor(INK.WithAlpha(Fade));
					IGraphics::CQuadItem Item(Drip.m_Pos.x, Drip.m_Pos.y, Long + 1.2f, Drip.m_Radius * 2.0f + 1.2f);
					Graphics()->QuadsDraw(&Item, 1);
				}
				else if(Layer == 1)
				{
					Graphics()->SetColor(GOO.WithAlpha(Fade));
					IGraphics::CQuadItem Item(Drip.m_Pos.x, Drip.m_Pos.y, Long, Drip.m_Radius * 2.0f);
					Graphics()->QuadsDraw(&Item, 1);
				}
				else
				{
					Graphics()->QuadsSetRotation(0.0f);
					Graphics()->SetColor(GOO_SHADE.WithAlpha(Fade * 0.8f));
					IGraphics::CQuadItem Item(Drip.m_Pos.x + Drip.m_Radius * 0.25f, Drip.m_Pos.y + Drip.m_Radius * 0.35f, Drip.m_Radius * 0.9f, Drip.m_Radius * 0.6f);
					Graphics()->QuadsDraw(&Item, 1);
				}
			}
		}
		Graphics()->QuadsSetRotation(0.0f);
		Graphics()->QuadsEnd();
	}

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const CHookGripTracker::CGrip &Grip = m_Tracker.Grip(i);
		const CTrollState &State = m_aStates[i];
		if(State.m_Splat <= 0.0f && State.m_FaceScale <= 0.01f)
			continue;
		const float Alpha = Grip.m_Alpha * Near(Grip);
		const float Radius = mix(14.0f, 20.0f, Grip.m_Charge) * Scale * State.m_Splat;
		DrawSplat(Grip.m_Pos, Grip.m_Dir, Radius, State.m_Seed, Alpha);
		const float Shake = State.m_Laugh * std::sin(Time * 40.0f) * 0.12f + std::sin(Time * 2.5f + State.m_Seed) * 0.06f;
		const float Size = mix(34.0f, 52.0f, Grip.m_Charge) * State.m_FaceScale * Scale;
		const vec2 Pos = Grip.m_Pos + Grip.m_Dir * 4.0f * Scale + vec2(0.0f, -State.m_Laugh * std::abs(std::sin(Time * 20.0f)) * 3.0f);
		DrawFace(Pos, Size, Shake, Grip.m_Dir.x < 0.0f, Alpha);
	}
	for(int i = 0; i < m_NumFaces; i++)
	{
		const CLooseFace &Face = m_aFaces[i];
		const float Fade = Face.m_Alpha * std::min(1.0f, (0.9f - Face.m_Life) * 4.0f);
		DrawFace(Face.m_Pos, Face.m_Size * (1.0f - Face.m_Life * 0.4f), Face.m_Angle, Face.m_Flip, Fade);
	}
}
