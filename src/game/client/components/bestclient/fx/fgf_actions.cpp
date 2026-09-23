/* Copyright © 2026 BestProject Team */
#include "fgf_actions.h"

#include <base/math.h>

#include <engine/shared/config.h>

#include "hookgrip.h"
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>

#include "own_tee.h"
#include <game/gamecore.h>

#include <algorithm>
#include <cmath>

using FgfGlow::Whiten;

static constexpr float JUMP_RING_TIME = 0.4f;
static constexpr float SLASH_TIME = 0.28f;
static constexpr float SLASH_SWING_TIME = 0.09f;
static constexpr float RELEASE_TIME = 0.3f;

static bool s_ShadowPass = false;

static void Stroke(IGraphics *pGraphics, const vec2 *pPoints, float *pWidths, int NumPoints, ColorRGBA Color)
{
	if(s_ShadowPass)
	{
		float aWide[FgfGlow::MAX_STRIP_POINTS];
		for(int i = 0; i < NumPoints && i < FgfGlow::MAX_STRIP_POINTS; i++)
			aWide[i] = pWidths[i] > 0.01f ? pWidths[i] * 2.2f + 1.2f : 0.0f;
		FgfGlow::DrawStrip(pGraphics, pPoints, aWide, NumPoints, ColorRGBA(Color.r * 0.25f, Color.g * 0.3f, Color.b * 0.4f, Color.a * 0.25f));
		return;
	}
	FgfGlow::DrawStrip(pGraphics, pPoints, pWidths, NumPoints, Color);
}

static float Fract(float x)
{
	return x - std::floor(x);
}

static float EaseOut(float t)
{
	t = std::clamp(t, 0.0f, 1.0f);
	return 1.0f - (1.0f - t) * (1.0f - t);
}

void CFgfActions::OnInit()
{
	m_GlowTexture = FgfGlow::CreateGlowTexture(Graphics());
	m_RingTexture = FgfGlow::CreateRingTexture(Graphics());
	m_BeamTexture = FgfGlow::CreateBeamTexture(Graphics());
}

void CFgfActions::OnReset()
{
	for(CSlot &Slot : m_aSlots)
		Slot = CSlot();
	m_NumJumpRings = 0;
	m_NumSlashes = 0;
	m_Sparks.Clear();
}

void CFgfActions::AirJump(vec2 Feet, ColorRGBA Main, ColorRGBA Core)
{
	if(m_NumJumpRings < MAX_JUMP_RINGS)
		m_aJumpRings[m_NumJumpRings++] = {Feet, 0.0f};

	for(int i = 0; i < 10; i++)
	{
		CGlowParticles::CSpark *pSpark = m_Sparks.NewSpark();
		if(!pSpark)
			break;
		const float Side = (i % 2 ? 1.0f : -1.0f) * random_float(0.3f, 1.0f);
		pSpark->m_Pos = Feet;
		pSpark->m_Vel = vec2(Side * random_float(90.0f, 180.0f), random_float(30.0f, 90.0f));
		pSpark->m_Drag = 0.02f;
		pSpark->m_Stretch = 0.04f;
		pSpark->m_LifeSpan = random_float(0.2f, 0.35f);
		pSpark->m_StartSize = random_float(2.5f, 3.5f);
		pSpark->m_EndSize = 0.5f;
		pSpark->m_StartAlpha = 0.8f;
		pSpark->m_EndAlpha = 0.0f;
		pSpark->m_Color = i % 3 ? Core : Main;
	}
}

void CFgfActions::Swing(int ClientId, float Angle, ColorRGBA Core)
{
	if(m_NumSlashes >= MAX_SLASHES)
		return;
	const float Sweep = std::cos(Angle) >= 0.0f ? 1.0f : -1.0f;
	m_aSlashes[m_NumSlashes++] = {ClientId, Angle, Sweep, 0.0f};
}

void CFgfActions::Catch(vec2 Target, ColorRGBA Main, ColorRGBA Core)
{
	m_Sparks.AddRing(Target, 0.3f, 20.0f, 80.0f, 0.7f, Core);
	for(int i = 0; i < 14; i++)
	{
		CGlowParticles::CSpark *pSpark = m_Sparks.NewSpark();
		if(!pSpark)
			break;
		const vec2 Out = direction(i * 2.0f * pi / 14.0f + random_float(-0.2f, 0.2f));
		pSpark->m_Pos = Target + Out * 16.0f;
		pSpark->m_Vel = Out * random_float(120.0f, 220.0f);
		pSpark->m_Drag = 0.02f;
		pSpark->m_Stretch = 0.05f;
		pSpark->m_LifeSpan = random_float(0.2f, 0.35f);
		pSpark->m_StartSize = random_float(3.0f, 4.0f);
		pSpark->m_EndSize = 0.5f;
		pSpark->m_StartAlpha = 0.9f;
		pSpark->m_EndAlpha = 0.0f;
		pSpark->m_Color = i % 2 ? Core : Main;
	}
}

void CFgfActions::LetGo(vec2 Target, ColorRGBA Main, ColorRGBA Core)
{
	for(int i = 0; i < 10; i++)
	{
		CGlowParticles::CSpark *pSpark = m_Sparks.NewSpark();
		if(!pSpark)
			break;
		const vec2 Out = direction(random_float(0.0f, 2.0f * pi));
		pSpark->m_Pos = Target + vec2(Out.x * 24.0f, Out.y * 8.0f);
		pSpark->m_Vel = Out * random_float(40.0f, 110.0f);
		pSpark->m_Drag = 0.1f;
		pSpark->m_Stretch = 0.03f;
		pSpark->m_LifeSpan = random_float(0.25f, 0.4f);
		pSpark->m_StartSize = random_float(2.5f, 3.5f);
		pSpark->m_EndSize = 0.5f;
		pSpark->m_StartAlpha = 0.8f;
		pSpark->m_EndAlpha = 0.0f;
		pSpark->m_Color = i % 2 ? Core : Main;
	}
}

void CFgfActions::UpdateSlot(int Dummy, float Passed, ColorRGBA Main, ColorRGBA Core)
{
	CSlot &Slot = m_aSlots[Dummy];
	const int ClientId = GameClient()->m_aLocalIds[Dummy];
	const bool Duplicate = Dummy == 1 && ClientId == GameClient()->m_aLocalIds[0];
	if(!in_range(ClientId, MAX_CLIENTS - 1) || !BcIsOwnTee(GameClient(), ClientId) || Duplicate || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
	{
		Slot = CSlot();
		return;
	}
	if(Slot.m_ClientId != ClientId)
	{
		Slot = CSlot();
		Slot.m_ClientId = ClientId;
		Slot.m_JumpedTotal = -1;
	}

	const CGameClient::CClientData &Client = GameClient()->m_aClients[ClientId];
	const CGameClient::CSnapState::CCharacterInfo &Snap = GameClient()->m_Snap.m_aCharacters[ClientId];
	const vec2 Pos = Client.m_RenderPos;

	CCharacter *pPredicted = GameClient()->Predict() ? GameClient()->m_PredictedWorld.GetCharacterById(ClientId) : nullptr;

	int JumpedTotal = Snap.m_HasExtendedData ? Snap.m_ExtendedData.m_JumpedTotal : -1;
	if(pPredicted)
		JumpedTotal = pPredicted->Core()->m_JumpedTotal;
	if(Slot.m_JumpedTotal >= 0 && JumpedTotal > Slot.m_JumpedTotal && g_Config.m_BcAirJump)
		AirJump(Pos + vec2(0.0f, 16.0f), Main, Core);
	Slot.m_JumpedTotal = JumpedTotal;

	const int AttackTick = pPredicted ? pPredicted->GetAttackTick() : Snap.m_Cur.m_AttackTick;
	const int Weapon = pPredicted ? pPredicted->GetActiveWeapon() : Snap.m_Cur.m_Weapon;
	if(Slot.m_AttackTick >= 0 && AttackTick != Slot.m_AttackTick)
	{
		const vec2 Aim = GameClient()->m_Controls.m_aMousePos[Dummy];
		const float Angle = length(Aim) > 0.001f ? angle(Aim) : Client.m_RenderCur.m_Angle / 256.0f;
		if(Weapon == WEAPON_HAMMER && g_Config.m_BcHammerSlash)
			Swing(ClientId, Angle, Core);
	}
	Slot.m_AttackTick = AttackTick;

	const CNetObj_Character &Cur = Client.m_RenderCur;
	const bool Holding = (g_Config.m_BcHookTether || g_Config.m_BcHookLassoBox) && Cur.m_HookState == HOOK_GRABBED &&
			     in_range(Cur.m_HookedPlayer, MAX_CLIENTS - 1) && GameClient()->m_Snap.m_aCharacters[Cur.m_HookedPlayer].m_Active;
	if(Holding)
	{
		const vec2 TargetPos = GameClient()->m_aClients[Cur.m_HookedPlayer].m_RenderPos;
		if(!Slot.m_Tethered || Slot.m_Target != Cur.m_HookedPlayer)
		{
			Slot.m_TetherTime = 0.0f;
			Catch(TargetPos, Main, Core);
		}
		Slot.m_Tethered = true;
		Slot.m_Target = Cur.m_HookedPlayer;
		Slot.m_TargetPos = TargetPos;
		Slot.m_TetherTime += Passed;
		Slot.m_ReleaseAge = RELEASE_TIME;
	}
	else
	{
		if(Slot.m_Tethered)
		{
			Slot.m_ReleaseAge = 0.0f;
			LetGo(Slot.m_TargetPos, Main, Core);
		}
		Slot.m_Tethered = false;
		Slot.m_ReleaseAge = std::min(RELEASE_TIME, Slot.m_ReleaseAge + Passed);
	}
}

static void DrawEllipseArc(IGraphics *pGraphics, vec2 Center, float RadiusX, float RadiusY, float Tilt, float From, float To, float Width, ColorRGBA Color)
{
	const int NumPoints = 24;
	vec2 aPoints[NumPoints];
	float aWidths[NumPoints];
	const vec2 X = direction(Tilt);
	const vec2 Y(-X.y, X.x);
	for(int i = 0; i < NumPoints; i++)
	{
		const float t = i / (float)(NumPoints - 1);
		const float a = mix(From, To, t);
		aPoints[i] = Center + X * std::cos(a) * RadiusX + Y * std::sin(a) * RadiusY;
		aWidths[i] = Width * std::min(1.0f, std::sin(pi * t) * 4.0f);
	}
	Stroke(pGraphics, aPoints, aWidths, NumPoints, Color);
}

void CFgfActions::DrawTether(const CSlot &Slot, ColorRGBA Main, ColorRGBA Core, float Time)
{
	vec2 aPoints[3];
	float aWidths[3];

	if(Slot.m_Tethered)
	{
		const vec2 Target = Slot.m_TargetPos;
		const float T = Slot.m_TetherTime;

		if(g_Config.m_BcHookLassoBox && T < 0.6f)
		{
			const float Snap = EaseOut(T / 0.18f);
			const float Dist = mix(48.0f, 25.0f, Snap);
			const float Rotation = mix(pi / 4.0f, 0.0f, Snap);
			const float Alpha = T < 0.35f ? 1.0f : 1.0f - (T - 0.35f) / 0.25f;
			const vec2 X = direction(Rotation);
			const vec2 Y(-X.y, X.x);
			for(int Corner = 0; Corner < 4; Corner++)
			{
				const float SX = Corner == 0 || Corner == 3 ? 1.0f : -1.0f;
				const float SY = Corner < 2 ? 1.0f : -1.0f;
				const vec2 Tip = Target + X * SX * Dist + Y * SY * Dist;
				aPoints[0] = Tip - X * SX * 11.0f;
				aPoints[1] = Tip;
				aPoints[2] = Tip - Y * SY * 11.0f;
				aWidths[0] = aWidths[1] = aWidths[2] = 1.8f;
				Stroke(Graphics(), aPoints, aWidths, 3, Core.WithAlpha(Alpha));
			}
		}

		if(g_Config.m_BcHookTether)
		{
			const float Tighten = EaseOut(T / 0.22f);
			const float RadiusX = mix(40.0f, 23.0f, Tighten);
			const float RadiusY = RadiusX * 0.3f;
			const float Tilt = 0.18f * std::sin(Time * 1.7f);
			const vec2 Waist = Target + vec2(0.0f, 4.0f);
			DrawEllipseArc(Graphics(), Waist, RadiusX, RadiusY, Tilt, pi, 2.0f * pi, 1.1f, Main.WithAlpha(0.6f));
			DrawEllipseArc(Graphics(), Waist, RadiusX, RadiusY, Tilt, 0.0f, pi, 2.0f, Core.WithAlpha(1.0f));
			DrawEllipseArc(Graphics(), Waist, RadiusX * 1.15f, RadiusY * 1.15f, -Tilt, 0.0f, pi, 0.7f, Main.WithAlpha(0.4f));
		}
	}
	else if(g_Config.m_BcHookTether && Slot.m_ReleaseAge < RELEASE_TIME)
	{
		const float Age = Slot.m_ReleaseAge / RELEASE_TIME;
		const float RadiusX = mix(23.0f, 48.0f, EaseOut(Age));
		const vec2 Waist = Slot.m_TargetPos + vec2(0.0f, 4.0f);
		DrawEllipseArc(Graphics(), Waist, RadiusX, RadiusX * 0.3f, 0.0f, 0.0f, 2.0f * pi, 1.2f * (1.0f - Age), Core.WithAlpha(1.0f - Age));
	}
}

void CFgfActions::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(BcFxSuppressed(GameClient()))
		return;

	const float Time = LocalTime();
	const float Passed = std::clamp(Time - m_LastTime, 0.0f, 0.1f) * GameClient()->GetAnimationPlaybackSpeed();
	m_LastTime = Time;

	const ColorRGBA Main = FgfThemeColor();
	const ColorRGBA Core = Whiten(Main, 0.7f);

	for(int Dummy = 0; Dummy < NUM_DUMMIES; Dummy++)
		UpdateSlot(Dummy, Passed, Main, Core);

	for(int i = 0; i < m_NumJumpRings;)
	{
		m_aJumpRings[i].m_Age += Passed;
		if(m_aJumpRings[i].m_Age >= JUMP_RING_TIME)
		{
			m_aJumpRings[i] = m_aJumpRings[--m_NumJumpRings];
			continue;
		}
		i++;
	}
	for(int i = 0; i < m_NumSlashes;)
	{
		m_aSlashes[i].m_Age += Passed;
		if(m_aSlashes[i].m_Age >= SLASH_TIME)
		{
			m_aSlashes[i] = m_aSlashes[--m_NumSlashes];
			continue;
		}
		i++;
	}
	m_Sparks.Update(Passed);

	bool AnyTether = false;
	for(const CSlot &Slot : m_aSlots)
		AnyTether |= Slot.m_Tethered || Slot.m_ReleaseAge < RELEASE_TIME;
	if(!AnyTether && m_NumJumpRings == 0 && m_NumSlashes == 0 && !m_Sparks.HasAny())
		return;

	Graphics()->BlendAdditive();

	Graphics()->TextureSet(m_GlowTexture);
	Graphics()->QuadsBegin();
	Graphics()->QuadsSetRotation(0.0f);
	for(const CSlot &Slot : m_aSlots)
	{
		if(!g_Config.m_BcHookTether || !Slot.m_Tethered)
			continue;
		const vec2 Target = Slot.m_TargetPos;
		Graphics()->SetColor(Main.WithAlpha(0.14f + 0.06f * std::sin(Time * 6.0f)));
		IGraphics::CQuadItem Halo(Target.x, Target.y, 80.0f, 80.0f);
		Graphics()->QuadsDraw(&Halo, 1);

		const vec2 Own = GameClient()->m_aClients[Slot.m_ClientId].m_RenderPos;
		const float Progress = Fract(Time * 2.2f);
		const vec2 Pulse = mix(Own, Target, Progress * Progress);
		const float PulseAlpha = std::sin(pi * Progress);
		Graphics()->SetColor(Main.WithAlpha(0.6f * PulseAlpha));
		IGraphics::CQuadItem PulseGlow(Pulse.x, Pulse.y, 16.0f, 16.0f);
		Graphics()->QuadsDraw(&PulseGlow, 1);
		Graphics()->SetColor(Core.WithAlpha(0.9f * PulseAlpha));
		IGraphics::CQuadItem PulseCore(Pulse.x, Pulse.y, 5.0f, 5.0f);
		Graphics()->QuadsDraw(&PulseCore, 1);

		const float Tighten = EaseOut(Slot.m_TetherTime / 0.22f);
		const float RadiusX = mix(40.0f, 23.0f, Tighten);
		const float BeadAngle = Time * 7.0f;
		const vec2 Bead = Target + vec2(0.0f, 4.0f) + vec2(std::cos(BeadAngle) * RadiusX, std::sin(BeadAngle) * RadiusX * 0.3f);
		const float Front = 0.5f + 0.5f * std::sin(BeadAngle);
		Graphics()->SetColor(Core.WithAlpha(mix(0.3f, 1.0f, Front)));
		IGraphics::CQuadItem BeadGlow(Bead.x, Bead.y, 9.0f, 9.0f);
		Graphics()->QuadsDraw(&BeadGlow, 1);
	}
	for(int i = 0; i < m_NumJumpRings; i++)
	{
		const CJumpRing &Ring = m_aJumpRings[i];
		const float Age = Ring.m_Age / JUMP_RING_TIME;
		Graphics()->SetColor(Main.WithAlpha(0.45f * (1.0f - Age) * (1.0f - Age)));
		IGraphics::CQuadItem Flash(Ring.m_Pos.x, Ring.m_Pos.y + 4.0f, 60.0f * mix(0.6f, 1.2f, Age), 20.0f);
		Graphics()->QuadsDraw(&Flash, 1);
	}
	Graphics()->QuadsEnd();

	for(int Pass = 0; Pass < 2; Pass++)
	{
		s_ShadowPass = Pass == 0;
		if(s_ShadowPass)
			Graphics()->BlendNormal();
		else
			Graphics()->BlendAdditive();
		FgfGlow::BeginStrips(Graphics(), m_BeamTexture);
		for(const CSlot &Slot : m_aSlots)
			DrawTether(Slot, Main, Core, Time);

		for(int i = 0; i < m_NumJumpRings; i++)
		{
			const CJumpRing &Ring = m_aJumpRings[i];
			const float Age = Ring.m_Age / JUMP_RING_TIME;
			for(int Layer = 0; Layer < 2; Layer++)
			{
				const float LayerAge = std::clamp(Age * 1.25f - Layer * 0.25f, 0.0f, 1.0f);
				if(LayerAge <= 0.0f || LayerAge >= 1.0f)
					continue;
				const float RadiusX = mix(8.0f, Layer == 0 ? 38.0f : 28.0f, EaseOut(LayerAge));
				const vec2 Center = Ring.m_Pos + vec2(0.0f, Layer * 5.0f + LayerAge * 6.0f);
				DrawEllipseArc(Graphics(), Center, RadiusX, RadiusX * 0.25f, 0.0f, 0.0f, 2.0f * pi, 1.9f * (1.0f - LayerAge) + 0.3f,
					(Layer == 0 ? Core : Main).WithAlpha((1.0f - LayerAge) * (Layer == 0 ? 0.95f : 0.7f)));
			}
		}

		for(int i = 0; i < m_NumSlashes; i++)
		{
			const CSlash &Slash = m_aSlashes[i];
			if(!GameClient()->m_Snap.m_aCharacters[Slash.m_ClientId].m_Active)
				continue;
			const vec2 Center = GameClient()->m_aClients[Slash.m_ClientId].m_RenderPos;
			const float Swing = EaseOut(Slash.m_Age / SLASH_SWING_TIME);
			const float Fade = Slash.m_Age < SLASH_SWING_TIME ? 1.0f : 1.0f - (Slash.m_Age - SLASH_SWING_TIME) / (SLASH_TIME - SLASH_SWING_TIME);
			const float Head = Slash.m_Angle + Slash.m_Sweep * mix(-1.4f, 1.3f, Swing);
			const float TrailLength = 2.0f * Swing;
			const int NumPoints = 20;
			vec2 aPoints[NumPoints];
			float aWidths[NumPoints];
			for(int Layer = 0; Layer < 2; Layer++)
			{
				const float Radius = Layer == 0 ? 34.0f : 28.0f;
				for(int p = 0; p < NumPoints; p++)
				{
					const float t = p / (float)(NumPoints - 1);
					const float a = Head - Slash.m_Sweep * TrailLength * (1.0f - t);
					aPoints[p] = Center + direction(a) * Radius;
					aWidths[p] = (Layer == 0 ? 4.0f : 1.8f) * t * std::min(1.0f, (1.0f - t) * 8.0f + 0.3f);
				}
				Stroke(Graphics(), aPoints, aWidths, NumPoints, (Layer == 0 ? Core : Main).WithAlpha(Fade * (Layer == 0 ? 0.9f : 0.6f)));
			}
		}
		Graphics()->QuadsEnd();
	}
	s_ShadowPass = false;

	m_Sparks.Draw(Graphics(), m_GlowTexture, m_RingTexture);
	Graphics()->BlendNormal();
}
