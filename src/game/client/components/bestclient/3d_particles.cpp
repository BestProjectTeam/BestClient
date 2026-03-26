#include "3d_particles.h"

#include <base/math.h>

#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
enum
{
	SHAPE_CUBE = 1,
	SHAPE_HEART = 2,
	SHAPE_MIXED = 3,
};

constexpr float MAX_DELTA = 0.1f;
constexpr float PROJ_DIST = 600.0f;
constexpr int PARTICLE_MAX_RENDERED = 200;

const std::array<vec3, 8> g_aCubeVertices = { {
	vec3(-1.0f, -1.0f, -1.0f),
	vec3(1.0f, -1.0f, -1.0f),
	vec3(1.0f, 1.0f, -1.0f),
	vec3(-1.0f, 1.0f, -1.0f),
	vec3(-1.0f, -1.0f, 1.0f),
	vec3(1.0f, -1.0f, 1.0f),
	vec3(1.0f, 1.0f, 1.0f),
	vec3(-1.0f, 1.0f, 1.0f),
} };

const std::array<std::array<int, 2>, 12> g_aCubeEdges = { {
	{ {0, 1} },
	{ {1, 2} },
	{ {2, 3} },
	{ {3, 0} },
	{ {4, 5} },
	{ {5, 6} },
	{ {6, 7} },
	{ {7, 4} },
	{ {0, 4} },
	{ {1, 5} },
	{ {2, 6} },
	{ {3, 7} },
} };

constexpr int HEART_POINTS = 96;
constexpr int HEART_LOW_POINTS = 24;
constexpr int HEART_LAYERS = 5;
constexpr float HEART_THICKNESS = 0.35f;

vec3 RotateVec3(const vec3 &V, const vec3 &Rot)
{
	vec3 R = V;

	const float Cz = std::cos(Rot.z);
	const float Sz = std::sin(Rot.z);
	R = vec3(R.x * Cz - R.y * Sz, R.x * Sz + R.y * Cz, R.z);

	const float Cx = std::cos(Rot.x);
	const float Sx = std::sin(Rot.x);
	R = vec3(R.x, R.y * Cx - R.z * Sx, R.y * Sx + R.z * Cx);

	const float Cy = std::cos(Rot.y);
	const float Sy = std::sin(Rot.y);
	R = vec3(R.x * Cy + R.z * Sy, R.y, -R.x * Sy + R.z * Cy);

	return R;
}

vec2 ProjectPoint(const vec3 &Pos, const vec2 &Center)
{
	const float Scale = std::clamp(PROJ_DIST / (PROJ_DIST + Pos.z), 0.5f, 1.6f);
	const vec2 Rel = vec2(Pos.x - Center.x, Pos.y - Center.y);
	return Center + Rel * Scale;
}

const std::array<vec3, HEART_POINTS> &HeartVertices()
{
	static std::array<vec3, HEART_POINTS> s_aVerts;
	static bool s_Initialized = false;
	if(!s_Initialized)
	{
		for(int i = 0; i < HEART_POINTS; i++)
		{
			const float T = 2.0f * pi * (float)i / (float)HEART_POINTS;
			const float X = 16.0f * std::pow(std::sin(T), 3.0f);
			const float Y = 13.0f * std::cos(T) - 5.0f * std::cos(2.0f * T) - 2.0f * std::cos(3.0f * T) - std::cos(4.0f * T);
			s_aVerts[i] = vec3(X, -Y, 0.0f);
		}
		s_Initialized = true;
	}
	return s_aVerts;
}

const std::array<vec3, HEART_LOW_POINTS> &HeartLowVertices()
{
	static std::array<vec3, HEART_LOW_POINTS> s_aVerts;
	static bool s_Initialized = false;
	if(!s_Initialized)
	{
		const auto &HighRes = HeartVertices();
		for(int i = 0; i < HEART_LOW_POINTS; i++)
		{
			const int Src = std::clamp((i * HEART_POINTS) / HEART_LOW_POINTS, 0, HEART_POINTS - 1);
			s_aVerts[i] = HighRes[Src];
		}
		s_Initialized = true;
	}
	return s_aVerts;
}

int PickType(int ConfigType)
{
	if(ConfigType == SHAPE_MIXED)
		return random_float() > 0.5f ? SHAPE_CUBE : SHAPE_HEART;
	return ConfigType;
}
} // namespace

void C3DParticles::OnInit()
{
	m_HasConfigSnapshot = false;
	ResetParticles();
}

void C3DParticles::OnReset()
{
	m_HasConfigSnapshot = false;
	ResetParticles();
}

void C3DParticles::OnStateChange(int NewState, int OldState)
{
	(void)NewState;
	(void)OldState;
	m_HasConfigSnapshot = false;
	ResetParticles();
}

void C3DParticles::ResetParticles()
{
	m_vParticles.clear();
	m_Time = 0.0f;
	m_HasLastLocalPos = false;
	m_LastLocalPos = vec2(0.0f, 0.0f);
}

bool C3DParticles::ShouldRender() const
{
	return !(g_Config.m_ClFocusMode && g_Config.m_ClFocusModeHideEffects);
}

void C3DParticles::OnRender()
{
	if(!ShouldRender())
	{
		if(!m_vParticles.empty())
			ResetParticles();
		return;
	}

	if(GameClient()->OptimizerDisableParticles())
	{
		if(!m_vParticles.empty())
			ResetParticles();
		return;
	}

	if(!g_Config.m_Bc3dParticles)
	{
		if(!m_vParticles.empty())
			ResetParticles();
		return;
	}

	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		ResetParticles();
		return;
	}

	const int CurType = g_Config.m_Bc3dParticlesType;
	const int CurCount = g_Config.m_Bc3dParticlesCount;
	const int CurSizeMax = g_Config.m_Bc3dParticlesSizeMax;
	const int CurSpeed = g_Config.m_Bc3dParticlesSpeed;
	const int CurAlpha = g_Config.m_Bc3dParticlesAlpha;
	const int CurColorMode = g_Config.m_Bc3dParticlesColorMode;
	const unsigned CurColor = g_Config.m_Bc3dParticlesColor;
	const int CurGlow = g_Config.m_Bc3dParticlesGlow;
	const int CurGlowAlpha = g_Config.m_Bc3dParticlesGlowAlpha;
	const int CurGlowOffset = g_Config.m_Bc3dParticlesGlowOffset;
	const int CurDepth = g_Config.m_Bc3dParticlesDepth;
	const int CurFadeInMs = g_Config.m_Bc3dParticlesFadeInMs;
	const int CurFadeOutMs = g_Config.m_Bc3dParticlesFadeOutMs;
	const int CurPushRadius = g_Config.m_Bc3dParticlesPushRadius;
	const int CurPushStrength = g_Config.m_Bc3dParticlesPushStrength;
	const int CurCollide = g_Config.m_Bc3dParticlesCollide;
	const int CurViewMargin = g_Config.m_Bc3dParticlesViewMargin;

	const bool Changed = !m_HasConfigSnapshot ||
		CurType != m_LastType ||
		CurCount != m_LastCount ||
		CurSizeMax != m_LastSizeMax ||
		CurSpeed != m_LastSpeed ||
		CurAlpha != m_LastAlpha ||
		CurColorMode != m_LastColorMode ||
		CurColor != m_LastColor ||
		CurGlow != m_LastGlow ||
		CurGlowAlpha != m_LastGlowAlpha ||
		CurGlowOffset != m_LastGlowOffset ||
		CurDepth != m_LastDepth ||
		CurFadeInMs != m_LastFadeInMs ||
		CurFadeOutMs != m_LastFadeOutMs ||
		CurPushRadius != m_LastPushRadius ||
		CurPushStrength != m_LastPushStrength ||
		CurCollide != m_LastCollide ||
		CurViewMargin != m_LastViewMargin;

	if(Changed)
	{
		m_LastType = CurType;
		m_LastCount = CurCount;
		m_LastSizeMax = CurSizeMax;
		m_LastSpeed = CurSpeed;
		m_LastAlpha = CurAlpha;
		m_LastColorMode = CurColorMode;
		m_LastColor = CurColor;
		m_LastGlow = CurGlow;
		m_LastGlowAlpha = CurGlowAlpha;
		m_LastGlowOffset = CurGlowOffset;
		m_LastDepth = CurDepth;
		m_LastFadeInMs = CurFadeInMs;
		m_LastFadeOutMs = CurFadeOutMs;
		m_LastPushRadius = CurPushRadius;
		m_LastPushStrength = CurPushStrength;
		m_LastCollide = CurCollide;
		m_LastViewMargin = CurViewMargin;
		m_HasConfigSnapshot = true;
		ResetParticles();
	}

	const float Delta = std::clamp(Client()->RenderFrameTime(), 0.0f, MAX_DELTA);
	if(Delta <= 0.0f)
		return;
	m_Time += Delta;

	vec2 LocalPos = GameClient()->m_Camera.m_Center;
	if(GameClient()->m_Snap.m_pLocalCharacter != nullptr)
		LocalPos = GameClient()->m_LocalCharacterPos;

	vec2 LocalVel(0.0f, 0.0f);
	if(m_HasLastLocalPos)
		LocalVel = (LocalPos - m_LastLocalPos) / Delta;
	m_LastLocalPos = LocalPos;
	m_HasLastLocalPos = true;

	const float Depth = std::clamp((float)g_Config.m_Bc3dParticlesDepth, 10.0f, 1000.0f);
	const float FadeIn = std::clamp(g_Config.m_Bc3dParticlesFadeInMs / 1000.0f, 0.001f, 5.0f);
	const float FadeOut = std::clamp(g_Config.m_Bc3dParticlesFadeOutMs / 1000.0f, 0.001f, 5.0f);
	const float BaseAlpha = std::clamp(g_Config.m_Bc3dParticlesAlpha / 100.0f, 0.0f, 1.0f);

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	const float MapMinX = 0.0f;
	const float MapMinY = 0.0f;
	const float MapMaxX = Collision()->GetWidth() * 32.0f;
	const float MapMaxY = Collision()->GetHeight() * 32.0f;
	if(MapMaxX <= MapMinX || MapMaxY <= MapMinY)
		return;

	const float ViewMargin = std::clamp((float)g_Config.m_Bc3dParticlesViewMargin, 0.0f, 1000.0f);
	const float ViewMinX = std::max(ScreenX0 - ViewMargin, MapMinX);
	const float ViewMaxX = std::min(ScreenX1 + ViewMargin, MapMaxX);
	const float ViewMinY = std::max(ScreenY0 - ViewMargin, MapMinY);
	const float ViewMaxY = std::min(ScreenY1 + ViewMargin, MapMaxY);

	float SpawnMinX = std::clamp(ScreenX0, MapMinX, MapMaxX);
	float SpawnMaxX = std::clamp(ScreenX1, MapMinX, MapMaxX);
	float SpawnMinY = std::clamp(ScreenY0, MapMinY, MapMaxY);
	float SpawnMaxY = std::clamp(ScreenY1, MapMinY, MapMaxY);
	if(SpawnMinX > SpawnMaxX)
		std::swap(SpawnMinX, SpawnMaxX);
	if(SpawnMinY > SpawnMaxY)
		std::swap(SpawnMinY, SpawnMaxY);

	int TargetCount = std::clamp(g_Config.m_Bc3dParticlesCount, 0, PARTICLE_MAX_RENDERED);
	if(GameClient()->OptimizerDisableParticles())
		TargetCount = minimum(TargetCount, 32);
	if((int)m_vParticles.size() > TargetCount)
		m_vParticles.resize(TargetCount);

	const float PushRadius = std::clamp((float)g_Config.m_Bc3dParticlesPushRadius, 0.0f, 1000.0f);
	const float PushStrength = std::clamp((float)g_Config.m_Bc3dParticlesPushStrength, 0.0f, 2000.0f);
	const float MaxSpeed = maximum(40.0f, (float)g_Config.m_Bc3dParticlesSpeed * 4.0f);
	const float PushRadiusSq = PushRadius * PushRadius;
	const bool EnableParticleCollisions = g_Config.m_Bc3dParticlesCollide != 0 && TargetCount <= 96;

	for(auto &Part : m_vParticles)
	{
		Part.m_Pos += Part.m_Vel * Delta;
		Part.m_Rot += Part.m_RotVel * Delta;

		if(PushStrength > 0.0f && PushRadius > 0.0f)
		{
			const vec3 Diff = Part.m_Pos - vec3(LocalPos.x, LocalPos.y, 0.0f);
			const float DistSq = dot(Diff, Diff);
			if(DistSq > 0.0001f && DistSq < PushRadiusSq)
			{
				const float Dist = sqrtf(DistSq);
				const float Factor = 1.0f - Dist / PushRadius;
				const vec3 Dir = Diff / Dist;
				Part.m_Vel += Dir * (PushStrength * Factor) * Delta;
				Part.m_Vel += vec3(LocalVel.x, LocalVel.y, 0.0f) * (0.002f * Factor);
			}
		}

		const float Speed = length(Part.m_Vel);
		if(Speed > MaxSpeed)
			Part.m_Vel = Part.m_Vel / Speed * MaxSpeed;
		Part.m_Vel *= 0.995f;

		Part.m_Pos.z = std::clamp(Part.m_Pos.z, -Depth, Depth);
		if((Part.m_Pos.x < ViewMinX || Part.m_Pos.x > ViewMaxX || Part.m_Pos.y < ViewMinY || Part.m_Pos.y > ViewMaxY) && !Part.m_FadingOut)
		{
			Part.m_FadingOut = true;
			Part.m_FadeOutStart = m_Time;
		}
	}

	if(EnableParticleCollisions)
	{
		for(size_t i = 0; i < m_vParticles.size(); i++)
		{
			for(size_t j = i + 1; j < m_vParticles.size(); j++)
			{
				auto &A = m_vParticles[i];
				auto &B = m_vParticles[j];
				const vec3 Diff = A.m_Pos - B.m_Pos;
				const float Radius = (A.m_Size + B.m_Size) * 0.6f;
				const float RadiusSq = Radius * Radius;
				const float DistSq = dot(Diff, Diff);
				if(DistSq > 0.0001f && DistSq < RadiusSq)
				{
					const float Dist = sqrtf(DistSq);
					const vec3 Dir = Diff / Dist;
					const float Pen = Radius - Dist;
					const float MassA = maximum(1.0f, A.m_Size);
					const float MassB = maximum(1.0f, B.m_Size);
					A.m_Pos += Dir * (Pen * (MassB / (MassA + MassB)));
					B.m_Pos -= Dir * (Pen * (MassA / (MassA + MassB)));

					const vec3 RelVel = A.m_Vel - B.m_Vel;
					const float RelAlong = dot(RelVel, Dir);
					if(RelAlong < 0.0f)
					{
						const float Restitution = 0.6f;
						const float Impulse = (-(1.0f + Restitution) * RelAlong) / (1.0f / MassA + 1.0f / MassB);
						A.m_Vel += Dir * (Impulse / MassA);
						B.m_Vel -= Dir * (Impulse / MassB);
					}
				}
			}
		}
	}

	for(size_t i = 0; i < m_vParticles.size();)
	{
		auto &Part = m_vParticles[i];
		if(Part.m_FadingOut)
		{
			const float T = FadeOut > 0.0f ? (m_Time - Part.m_FadeOutStart) / FadeOut : 1.0f;
			if(T >= 1.0f)
			{
				m_vParticles[i] = m_vParticles.back();
				m_vParticles.pop_back();
				continue;
			}
		}
		i++;
	}

	const int Missing = TargetCount - (int)m_vParticles.size();
	const int SpawnNow = std::min(Missing, 10);
	const float SpawnWidth = maximum(1.0f, SpawnMaxX - SpawnMinX);
	const float SpawnHeight = maximum(1.0f, SpawnMaxY - SpawnMinY);
	const float SpawnArea = SpawnWidth * SpawnHeight;
	const float IdealSpacing = std::sqrt(SpawnArea / maximum(1, TargetCount));
	const float MinSpacing = std::clamp(IdealSpacing * 0.6f, 8.0f, 160.0f);
	const float MinSpacingSq = MinSpacing * MinSpacing;

	auto IsPositionFree = [&](const vec2 &Pos, float MinDistSq) {
		for(const auto &Part : m_vParticles)
		{
			const float Dx = Part.m_Pos.x - Pos.x;
			const float Dy = Part.m_Pos.y - Pos.y;
			if(Dx * Dx + Dy * Dy < MinDistSq)
				return false;
		}
		return true;
	};

	const int ConfigTypeValue = (int)g_Config.m_Bc3dParticlesType;
	int ConfigType = ConfigTypeValue;
	if(ConfigType < SHAPE_CUBE)
		ConfigType = SHAPE_CUBE;
	else if(ConfigType > SHAPE_MIXED)
		ConfigType = SHAPE_MIXED;
	const int SizeMinValue = std::min((int)g_Config.m_Bc3dParticlesSizeMin, (int)g_Config.m_Bc3dParticlesSizeMax);
	const int SizeMaxValue = std::max((int)g_Config.m_Bc3dParticlesSizeMin, (int)g_Config.m_Bc3dParticlesSizeMax);
	const float SizeMin = (float)SizeMinValue;
	const float SizeMax = (float)SizeMaxValue;
	const float Speed = (float)g_Config.m_Bc3dParticlesSpeed;

	for(int i = 0; i < SpawnNow; i++)
	{
		SParticle P;
		const int Type = PickType(ConfigType);
		P.m_Type = Type;
		P.m_Size = random_float(SizeMin, SizeMax);

		ColorRGBA BaseColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_Bc3dParticlesColor));
		if(g_Config.m_Bc3dParticlesColorMode == 2)
		{
			const ColorHSLA RandomColor(random_float(), 0.85f, 0.65f, BaseColor.a);
			BaseColor = color_cast<ColorRGBA>(RandomColor);
		}
		P.m_Color = BaseColor;

		bool Found = false;
		for(int Try = 0; Try < 12; Try++)
		{
			const float Relax = Try >= 8 ? 0.5f : (Try >= 4 ? 0.75f : 1.0f);
			const float MinDistSq = MinSpacingSq * Relax * Relax;
			const vec2 Candidate(random_float(SpawnMinX, SpawnMaxX), random_float(SpawnMinY, SpawnMaxY));
			if(IsPositionFree(Candidate, MinDistSq))
			{
				P.m_Pos = vec3(Candidate.x, Candidate.y, random_float(-Depth, Depth));
				Found = true;
				break;
			}
		}
		if(!Found)
		{
			P.m_Pos = vec3(
				random_float(SpawnMinX, SpawnMaxX),
				random_float(SpawnMinY, SpawnMaxY),
				random_float(-Depth, Depth));
		}

		vec3 Dir(random_float(-1.0f, 1.0f), random_float(-1.0f, 1.0f), 0.0f);
		const float DirLen = length(Dir);
		if(DirLen < 0.001f)
			Dir = vec3(1.0f, 0.0f, 0.0f);
		else
			Dir /= DirLen;
		P.m_Vel = Dir * Speed;

		P.m_Rot = vec3(random_float(-0.35f, 0.35f), random_float(-0.35f, 0.35f), random_float(0.0f, 2.0f * pi));
		P.m_RotVel = vec3(random_float(-0.08f, 0.08f), random_float(-0.08f, 0.08f), random_float(-0.2f, 0.2f));

		const vec3 SpawnDir = normalize(vec3(random_float(-1.0f, 1.0f), random_float(-1.0f, 1.0f), random_float(-0.5f, 0.5f)) + vec3(0.001f, 0.0f, 0.0f));
		const vec3 FadeDir = normalize(vec3(random_float(-1.0f, 1.0f), random_float(-1.0f, 1.0f), random_float(-0.5f, 0.5f)) + vec3(0.001f, 0.0f, 0.0f));
		P.m_SpawnOffset = SpawnDir * (P.m_Size * random_float(0.35f, 0.85f));
		P.m_FadeOutOffset = FadeDir * (P.m_Size * random_float(0.55f, 1.1f));
		P.m_SpawnTime = m_Time;
		P.m_FadeOutStart = 0.0f;
		P.m_FadingOut = false;

		m_vParticles.push_back(P);
	}

	RenderParticles(ViewMinX, ViewMaxX, ViewMinY, ViewMaxY, BaseAlpha, FadeIn, FadeOut);
}

void C3DParticles::RenderParticles(float ViewMinX, float ViewMaxX, float ViewMinY, float ViewMaxY, float BaseAlpha, float FadeIn, float FadeOut)
{
	if(m_vParticles.empty())
		return;

	Graphics()->TextureClear();

	const bool GlowEnabled = g_Config.m_Bc3dParticlesGlow != 0;
	const float GlowAlpha = std::clamp(g_Config.m_Bc3dParticlesGlowAlpha / 100.0f, 0.0f, 1.0f);
	const float GlowOffset = (float)g_Config.m_Bc3dParticlesGlowOffset;
	const vec3 GlowOffsetVec(-GlowOffset, -GlowOffset, 0.0f);

	auto GetRenderParams = [&](const SParticle &Part, float AlphaMul, const vec3 &ExtraOffset, vec3 &OutPos, float &OutSize, float &OutAlpha) {
		if(Part.m_Pos.x < ViewMinX || Part.m_Pos.x > ViewMaxX || Part.m_Pos.y < ViewMinY || Part.m_Pos.y > ViewMaxY)
			return false;

		const float InT = FadeIn > 0.0f ? std::clamp((m_Time - Part.m_SpawnTime) / FadeIn, 0.0f, 1.0f) : 1.0f;
		const float OutT = Part.m_FadingOut ? (FadeOut > 0.0f ? std::clamp((m_Time - Part.m_FadeOutStart) / FadeOut, 0.0f, 1.0f) : 1.0f) : 0.0f;
		const float Out = Part.m_FadingOut ? (1.0f - OutT) : 1.0f;
		const float Alpha = BaseAlpha * InT * Out * AlphaMul;
		if(Alpha <= 0.0f)
			return false;

		const float InEase = InT * InT * (3.0f - 2.0f * InT);
		const float OutEase = OutT * OutT * (3.0f - 2.0f * OutT);

		float Scale = 1.0f;
		vec3 Offset(0.0f, 0.0f, 0.0f);

		const float Pop = 1.0f + 0.2f * std::sin(InEase * pi);
		Scale *= mix(0.55f, 1.0f, InEase) * Pop;
		Offset += Part.m_SpawnOffset * (1.0f - InEase);

		if(Part.m_FadingOut)
		{
			Offset += Part.m_FadeOutOffset * OutEase;
			Scale *= std::pow(Out, 1.25f);
		}

		OutPos = Part.m_Pos + Offset + ExtraOffset;
		OutSize = Part.m_Size * Scale;
		OutAlpha = Alpha;
		return OutAlpha > 0.0f && OutSize > 0.01f;
	};

	auto DrawCube = [&](const SParticle &Part, const vec3 &RenderPos, float RenderSize, float FinalAlpha) {
		Graphics()->SetColor(ColorRGBA(Part.m_Color.r, Part.m_Color.g, Part.m_Color.b, Part.m_Color.a * FinalAlpha));

		std::array<vec2, g_aCubeVertices.size()> aProjected;
		for(size_t i = 0; i < g_aCubeVertices.size(); i++)
		{
			const vec3 Local = g_aCubeVertices[i] * RenderSize;
			const vec3 V = RotateVec3(Local, Part.m_Rot) + RenderPos;
			aProjected[i] = ProjectPoint(V, GameClient()->m_Camera.m_Center);
		}

		std::array<IGraphics::CLineItem, g_aCubeEdges.size()> aLines;
		for(size_t i = 0; i < g_aCubeEdges.size(); i++)
		{
			const auto &Edge = g_aCubeEdges[i];
			aLines[i] = IGraphics::CLineItem(aProjected[Edge[0]], aProjected[Edge[1]]);
		}
		Graphics()->LinesDraw(aLines.data(), aLines.size());
	};

	auto DrawHeart = [&](const SParticle &Part, const vec3 &RenderPos, float RenderSize, float FinalAlpha) {
		Graphics()->SetColor(ColorRGBA(Part.m_Color.r, Part.m_Color.g, Part.m_Color.b, Part.m_Color.a * FinalAlpha));

		const auto &Verts = HeartLowVertices();
		const float Scale = RenderSize * 0.055f;
		const float LayerStep = HEART_LAYERS > 1 ? 2.0f / (float)(HEART_LAYERS - 1) : 0.0f;
		std::array<std::array<vec2, HEART_LOW_POINTS>, HEART_LAYERS> aProjected;
		std::array<float, HEART_LAYERS> aLayerZ;
		for(int L = 0; L < HEART_LAYERS; L++)
		{
			const float LayerT = -1.0f + LayerStep * (float)L;
			const float Z = LayerT * (RenderSize * HEART_THICKNESS);
			aLayerZ[L] = Z;
			const float LayerScale = 1.0f - std::abs(LayerT) * 0.08f;
			for(int i = 0; i < HEART_LOW_POINTS; i++)
			{
				const vec3 Local = vec3(Verts[i].x * Scale * LayerScale, Verts[i].y * Scale * LayerScale, Z);
				const vec3 V = RotateVec3(Local, Part.m_Rot) + RenderPos;
				aProjected[L][i] = ProjectPoint(V, GameClient()->m_Camera.m_Center);
			}
		}

		for(int L = 0; L < HEART_LAYERS; L++)
		{
			std::array<IGraphics::CLineItem, HEART_LOW_POINTS> aRingLines;
			for(int i = 0; i < HEART_LOW_POINTS; i++)
			{
				const int Next = (i + 1) % HEART_LOW_POINTS;
				aRingLines[i] = IGraphics::CLineItem(aProjected[L][i], aProjected[L][Next]);
			}
			Graphics()->LinesDraw(aRingLines.data(), aRingLines.size());
		}

		for(int L = 0; L < HEART_LAYERS - 1; L++)
		{
			std::array<IGraphics::CLineItem, HEART_LOW_POINTS> aVertical;
			std::array<IGraphics::CLineItem, HEART_LOW_POINTS> aDiagonal;
			for(int i = 0; i < HEART_LOW_POINTS; i++)
			{
				const int Next = (i + 1) % HEART_LOW_POINTS;
				aVertical[i] = IGraphics::CLineItem(aProjected[L][i], aProjected[L + 1][i]);
				aDiagonal[i] = IGraphics::CLineItem(aProjected[L][i], aProjected[L + 1][Next]);
			}
			Graphics()->LinesDraw(aVertical.data(), aVertical.size());
			Graphics()->LinesDraw(aDiagonal.data(), aDiagonal.size());
		}

		if(HEART_LAYERS >= 2)
		{
			const int Front = 0;
			const int Back = HEART_LAYERS - 1;
			const vec2 CenterFront = ProjectPoint(RotateVec3(vec3(0.0f, 0.0f, aLayerZ[Front]), Part.m_Rot) + RenderPos, GameClient()->m_Camera.m_Center);
			const vec2 CenterBack = ProjectPoint(RotateVec3(vec3(0.0f, 0.0f, aLayerZ[Back]), Part.m_Rot) + RenderPos, GameClient()->m_Camera.m_Center);
			std::array<IGraphics::CLineItem, HEART_LOW_POINTS> aFront;
			std::array<IGraphics::CLineItem, HEART_LOW_POINTS> aBack;
			for(int i = 0; i < HEART_LOW_POINTS; i++)
			{
				aFront[i] = IGraphics::CLineItem(CenterFront, aProjected[Front][i]);
				aBack[i] = IGraphics::CLineItem(CenterBack, aProjected[Back][i]);
			}
			Graphics()->LinesDraw(aFront.data(), aFront.size());
			Graphics()->LinesDraw(aBack.data(), aBack.size());
		}
	};

	Graphics()->LinesBegin();
	for(const auto &Part : m_vParticles)
	{
		vec3 RenderPos;
		float RenderSize;
		float FinalAlpha;

		if(GlowEnabled && GlowAlpha > 0.0f && GlowOffset > 0.0f)
		{
			if(GetRenderParams(Part, GlowAlpha, GlowOffsetVec, RenderPos, RenderSize, FinalAlpha))
			{
				if(Part.m_Type == SHAPE_CUBE)
					DrawCube(Part, RenderPos, RenderSize, FinalAlpha);
				else
					DrawHeart(Part, RenderPos, RenderSize, FinalAlpha);
			}
		}

		if(GetRenderParams(Part, 1.0f, vec3(0.0f, 0.0f, 0.0f), RenderPos, RenderSize, FinalAlpha))
		{
			if(Part.m_Type == SHAPE_CUBE)
				DrawCube(Part, RenderPos, RenderSize, FinalAlpha);
			else
				DrawHeart(Part, RenderPos, RenderSize, FinalAlpha);
		}
	}
	Graphics()->LinesEnd();
}
