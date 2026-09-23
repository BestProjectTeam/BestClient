/* Copyright © 2026 BestProject Team */
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
constexpr float PROJ_SCALE_MIN = 0.5f;
constexpr float PROJ_SCALE_MAX = 1.6f;
constexpr int PARTICLE_MAX_CUBE = 12000;
constexpr int PARTICLE_MAX_HEART = 8000;
constexpr int MAX_COLLISION_PARTICLES = 140;
constexpr size_t MAX_COLLISION_PAIRS = 10000;
constexpr float GLOW_ALPHA = 0.35f;
constexpr float GLOW_OFFSET = 2.0f;
constexpr float DENSITY_CELL_SPARSE = 120.0f;
constexpr float DENSITY_CELL_DENSE = 14.0f;
constexpr float UPDATE_EXPAND = 1.35f;
constexpr float HEART_SIZE_SCALE = 0.09f;

const std::array<vec3, 8> g_aCubeVertices = {{
	vec3(-1.0f, -1.0f, -1.0f),
	vec3(1.0f, -1.0f, -1.0f),
	vec3(1.0f, 1.0f, -1.0f),
	vec3(-1.0f, 1.0f, -1.0f),
	vec3(-1.0f, -1.0f, 1.0f),
	vec3(1.0f, -1.0f, 1.0f),
	vec3(1.0f, 1.0f, 1.0f),
	vec3(-1.0f, 1.0f, 1.0f),
}};

const std::array<std::array<int, 2>, 12> g_aCubeEdges = {{
	{{0, 1}},
	{{1, 2}},
	{{2, 3}},
	{{3, 0}},
	{{4, 5}},
	{{5, 6}},
	{{6, 7}},
	{{7, 4}},
	{{0, 4}},
	{{1, 5}},
	{{2, 6}},
	{{3, 7}},
}};

constexpr int HEART_POINTS = 8;
constexpr int HEART_LAYERS = 2;
constexpr float HEART_THICKNESS = 0.28f;

struct SRotation
{
	float m_Cx, m_Sx, m_Cy, m_Sy, m_Cz, m_Sz;
};

SRotation MakeRotation(const vec3 &Rot)
{
	return SRotation{
		std::cos(Rot.x), std::sin(Rot.x),
		std::cos(Rot.y), std::sin(Rot.y),
		std::cos(Rot.z), std::sin(Rot.z)};
}

vec3 RotateVec3(const vec3 &V, const SRotation &Rot)
{
	vec3 R = vec3(V.x * Rot.m_Cz - V.y * Rot.m_Sz, V.x * Rot.m_Sz + V.y * Rot.m_Cz, V.z);
	R = vec3(R.x, R.y * Rot.m_Cx - R.z * Rot.m_Sx, R.y * Rot.m_Sx + R.z * Rot.m_Cx);
	R = vec3(R.x * Rot.m_Cy + R.z * Rot.m_Sy, R.y, -R.x * Rot.m_Sy + R.z * Rot.m_Cy);
	return R;
}

vec2 ProjectPoint(const vec3 &Pos, const vec2 &Center)
{
	const float Scale = std::clamp(PROJ_DIST / (PROJ_DIST + Pos.z), PROJ_SCALE_MIN, PROJ_SCALE_MAX);
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

int PickType(int ConfigType)
{
	if(ConfigType == SHAPE_MIXED)
		return random_float() > 0.5f ? SHAPE_CUBE : SHAPE_HEART;
	return ConfigType;
}

ColorRGBA MakeParticleColor()
{
	ColorRGBA BaseColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_Bc3dParticlesColor, true));
	if(g_Config.m_Bc3dParticlesColorMode == 2)
	{
		const ColorHSLA RandomColor(random_float(), 0.85f, 0.65f, BaseColor.a);
		BaseColor = color_cast<ColorRGBA>(RandomColor);
	}
	return BaseColor;
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
	m_HasLastLocalPos = false;
	m_LastLocalPos = vec2(0.0f, 0.0f);
}

void C3DParticles::OnRender()
{
	if(g_Config.m_ClFocusMode && g_Config.m_ClFocusModeHideEffects)
	{
		if(!m_vParticles.empty())
			ResetParticles();
		return;
	}

	if(GameClient()->OptimizerDisableParticles())
		return;

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
	const int CurSizeMax = g_Config.m_Bc3dParticlesSizeMax;
	const int CurDensity = g_Config.m_Bc3dParticlesDensity;
	const int CurColorMode = g_Config.m_Bc3dParticlesColorMode;
	const unsigned CurColor = g_Config.m_Bc3dParticlesColor;

	const bool StructuralChanged = !m_HasConfigSnapshot ||
		CurType != m_LastType ||
		CurSizeMax != m_LastSizeMax ||
		CurDensity != m_LastDensity;

	if(StructuralChanged)
	{
		m_LastType = CurType;
		m_LastSizeMax = CurSizeMax;
		m_LastDensity = CurDensity;
		m_LastColorMode = CurColorMode;
		m_LastColor = CurColor;
		m_HasConfigSnapshot = true;
		ResetParticles();
		m_LastAutoCount = 0;
		m_LastMapW = 0;
		m_LastMapH = 0;
	}
	else if(CurColorMode != m_LastColorMode || CurColor != m_LastColor)
	{
		for(auto &Part : m_vParticles)
		{
			if(CurColorMode == 2)
				Part.m_Color = MakeParticleColor();
			else
				Part.m_Color = color_cast<ColorRGBA>(ColorHSLA(CurColor, true));
		}
		m_LastColorMode = CurColorMode;
		m_LastColor = CurColor;
	}

	const float Delta = std::clamp(Client()->RenderFrameTime(), 0.0f, MAX_DELTA);
	if(Delta <= 0.0f)
		return;

	vec2 LocalPos = GameClient()->m_Camera.m_Center;
	if(GameClient()->m_Snap.m_pLocalCharacter != nullptr)
		LocalPos = GameClient()->m_LocalCharacterPos;

	vec2 LocalVel(0.0f, 0.0f);
	if(m_HasLastLocalPos)
		LocalVel = (LocalPos - m_LastLocalPos) / Delta;
	m_LastLocalPos = LocalPos;
	m_HasLastLocalPos = true;

	const float Depth = std::clamp((float)g_Config.m_Bc3dParticlesDepth, 10.0f, 1000.0f);
	const float BaseAlpha = std::clamp(g_Config.m_Bc3dParticlesAlpha / 100.0f, 0.0f, 1.0f);

	const CScreenRect ScreenRect = Graphics()->GetScreen();
	const float ScreenMinX = std::min(ScreenRect.m_TopLeft.x, ScreenRect.m_BottomRight.x);
	const float ScreenMaxX = std::max(ScreenRect.m_TopLeft.x, ScreenRect.m_BottomRight.x);
	const float ScreenMinY = std::min(ScreenRect.m_TopLeft.y, ScreenRect.m_BottomRight.y);
	const float ScreenMaxY = std::max(ScreenRect.m_TopLeft.y, ScreenRect.m_BottomRight.y);
	const float ScreenCenterX = GameClient()->m_Camera.m_Center.x;
	const float ScreenCenterY = GameClient()->m_Camera.m_Center.y;
	const float ScreenHalfW = (ScreenMaxX - ScreenMinX) * 0.5f;
	const float ScreenHalfH = (ScreenMaxY - ScreenMinY) * 0.5f;

	const float MapMinX = 0.0f;
	const float MapMinY = 0.0f;
	const float MapMaxX = Collision()->GetWidth() * 32.0f;
	const float MapMaxY = Collision()->GetHeight() * 32.0f;
	const float MapW = MapMaxX - MapMinX;
	const float MapH = MapMaxY - MapMinY;
	if(MapW <= 0.0f || MapH <= 0.0f)
		return;

	const int MapWInt = Collision()->GetWidth();
	const int MapHInt = Collision()->GetHeight();

	int ConfigType = (int)g_Config.m_Bc3dParticlesType;
	if(ConfigType < SHAPE_CUBE)
		ConfigType = SHAPE_CUBE;
	else if(ConfigType > SHAPE_MIXED)
		ConfigType = SHAPE_MIXED;
	const int SizeMaxValue = std::clamp((int)g_Config.m_Bc3dParticlesSizeMax, 2, 200);
	const float SizeMin = (float)std::max(2, SizeMaxValue - 3);
	const float SizeMax = (float)SizeMaxValue;
	const float ViewMargin = std::clamp((float)g_Config.m_Bc3dParticlesViewMargin, 0.0f, 1000.0f);

	const float CullPad = SizeMax * PROJ_SCALE_MAX + 8.0f;
	const float CullHalfW = ScreenHalfW / PROJ_SCALE_MIN + CullPad;
	const float CullHalfH = ScreenHalfH / PROJ_SCALE_MIN + CullPad;
	const float CullMinX = ScreenCenterX - CullHalfW;
	const float CullMaxX = ScreenCenterX + CullHalfW;
	const float CullMinY = ScreenCenterY - CullHalfH;
	const float CullMaxY = ScreenCenterY + CullHalfH;
	const float RenderMinX = CullMinX - ViewMargin;
	const float RenderMaxX = CullMaxX + ViewMargin;
	const float RenderMinY = CullMinY - ViewMargin;
	const float RenderMaxY = CullMaxY + ViewMargin;
	const float Speed = (float)g_Config.m_Bc3dParticlesSpeed;
	const int MaxCount = (ConfigType == SHAPE_HEART) ? PARTICLE_MAX_HEART : PARTICLE_MAX_CUBE;

	const float DensityT = std::clamp((CurDensity - 1) / 599.0f, 0.0f, 1.0f);
	const float DensityCell = mix(DENSITY_CELL_SPARSE, DENSITY_CELL_DENSE, DensityT);
	const int DensityCols = std::max(1, (int)std::round(MapW / DensityCell));
	const int DensityRows = std::max(1, (int)std::round(MapH / DensityCell));
	int TargetCount = std::clamp(DensityCols * DensityRows, 48, MaxCount);

	auto InitParticleMotion = [&](SParticle &P) {
		vec3 Dir(random_float(-1.0f, 1.0f), random_float(-1.0f, 1.0f), 0.0f);
		const float DirLen = length(Dir);
		if(DirLen < 0.001f)
			Dir = vec3(1.0f, 0.0f, 0.0f);
		else
			Dir /= DirLen;
		P.m_Vel = Dir * Speed;
		P.m_Rot = vec3(random_float(-0.35f, 0.35f), random_float(-0.35f, 0.35f), random_float(0.0f, 2.0f * pi));
		P.m_RotVel = vec3(random_float(-0.08f, 0.08f), random_float(-0.08f, 0.08f), random_float(-0.2f, 0.2f));
	};

	const bool NeedRefill = (int)m_vParticles.size() != TargetCount ||
		MapWInt != m_LastMapW || MapHInt != m_LastMapH || TargetCount != m_LastAutoCount;
	if(NeedRefill)
	{
		m_vParticles.clear();
		m_vParticles.reserve(TargetCount);
		const int FillCols = std::max(1, (int)std::ceil(std::sqrt((float)TargetCount * MapW / MapH)));
		const int FillRows = std::max(1, (int)std::ceil((float)TargetCount / (float)FillCols));
		const float CellW = MapW / (float)FillCols;
		const float CellH = MapH / (float)FillRows;

		for(int i = 0; i < TargetCount; i++)
		{
			const int Col = i % FillCols;
			const int Row = i / FillCols;
			SParticle P;
			P.m_Type = PickType(ConfigType);
			P.m_Size = random_float(SizeMin, SizeMax);
			P.m_Color = MakeParticleColor();
			const float X = std::clamp(MapMinX + (Col + random_float(0.15f, 0.85f)) * CellW, MapMinX, MapMaxX);
			const float Y = std::clamp(MapMinY + (Row + random_float(0.15f, 0.85f)) * CellH, MapMinY, MapMaxY);
			P.m_Pos = vec3(X, Y, random_float(-Depth, Depth));
			InitParticleMotion(P);
			m_vParticles.push_back(P);
		}

		m_LastMapW = MapWInt;
		m_LastMapH = MapHInt;
		m_LastAutoCount = TargetCount;
	}

	const float PushRadius = std::clamp((float)g_Config.m_Bc3dParticlesPushRadius, 0.0f, 1000.0f);
	const float PushStrength = std::clamp((float)g_Config.m_Bc3dParticlesPushStrength, 0.0f, 2000.0f);
	const float MaxSpeed = std::max(40.0f, Speed * 4.0f);
	const float PushRadiusSq = PushRadius * PushRadius;
	const bool EnableParticleCollisions = g_Config.m_Bc3dParticlesCollide != 0;

	const float UpdateHalfW = std::max((CullHalfW + ViewMargin) * UPDATE_EXPAND, 500.0f);
	const float UpdateHalfH = std::max((CullHalfH + ViewMargin) * UPDATE_EXPAND, 500.0f);
	const float UpdateMinX = ScreenCenterX - UpdateHalfW;
	const float UpdateMaxX = ScreenCenterX + UpdateHalfW;
	const float UpdateMinY = ScreenCenterY - UpdateHalfH;
	const float UpdateMaxY = ScreenCenterY + UpdateHalfH;

	std::vector<size_t> vActive;
	vActive.reserve(256);

	for(size_t i = 0; i < m_vParticles.size(); i++)
	{
		auto &Part = m_vParticles[i];
		if(Part.m_Pos.x < UpdateMinX || Part.m_Pos.x > UpdateMaxX || Part.m_Pos.y < UpdateMinY || Part.m_Pos.y > UpdateMaxY)
			continue;

		vActive.push_back(i);
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

		const float PartSpeed = length(Part.m_Vel);
		if(PartSpeed > MaxSpeed)
			Part.m_Vel = Part.m_Vel / PartSpeed * MaxSpeed;
		Part.m_Vel *= 0.995f;

		Part.m_Pos.z = std::clamp(Part.m_Pos.z, -Depth, Depth);

		if(Part.m_Pos.x < MapMinX)
		{
			Part.m_Pos.x = MapMinX;
			Part.m_Vel.x = std::abs(Part.m_Vel.x);
		}
		else if(Part.m_Pos.x > MapMaxX)
		{
			Part.m_Pos.x = MapMaxX;
			Part.m_Vel.x = -std::abs(Part.m_Vel.x);
		}
		if(Part.m_Pos.y < MapMinY)
		{
			Part.m_Pos.y = MapMinY;
			Part.m_Vel.y = std::abs(Part.m_Vel.y);
		}
		else if(Part.m_Pos.y > MapMaxY)
		{
			Part.m_Pos.y = MapMaxY;
			Part.m_Vel.y = -std::abs(Part.m_Vel.y);
		}
	}

	if(EnableParticleCollisions && vActive.size() > 1 && vActive.size() <= (size_t)MAX_COLLISION_PARTICLES && vActive.size() * (vActive.size() - 1) / 2 <= MAX_COLLISION_PAIRS)
	{
		for(size_t ai = 0; ai < vActive.size(); ai++)
		{
			for(size_t aj = ai + 1; aj < vActive.size(); aj++)
			{
				auto &A = m_vParticles[vActive[ai]];
				auto &B = m_vParticles[vActive[aj]];
				const vec3 Diff = A.m_Pos - B.m_Pos;
				const float Radius = (A.m_Size + B.m_Size) * 0.6f;
				const float RadiusSq = Radius * Radius;
				const float DistSq = dot(Diff, Diff);
				if(DistSq > 0.0001f && DistSq < RadiusSq)
				{
					const float Dist = sqrtf(DistSq);
					const vec3 Dir = Diff / Dist;
					const float Pen = Radius - Dist;
					const float MassA = std::max(1.0f, A.m_Size);
					const float MassB = std::max(1.0f, B.m_Size);
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

	RenderParticles(RenderMinX, RenderMaxX, RenderMinY, RenderMaxY, BaseAlpha);
}

void C3DParticles::RenderParticles(float CullMinX, float CullMaxX, float CullMinY, float CullMaxY, float BaseAlpha)
{
	if(m_vParticles.empty())
		return;

	Graphics()->TextureClear();

	const bool GlowEnabled = g_Config.m_Bc3dParticlesGlow != 0;
	const vec3 GlowOffsetVec(-GLOW_OFFSET, -GLOW_OFFSET, 0.0f);
	const vec2 CameraCenter = GameClient()->m_Camera.m_Center;

	auto DrawCube = [&](const SParticle &Part, const vec3 &RenderPos, float RenderSize, float FinalAlpha) {
		Graphics()->SetColor(ColorRGBA(Part.m_Color.r, Part.m_Color.g, Part.m_Color.b, Part.m_Color.a * FinalAlpha));

		const SRotation Rot = MakeRotation(Part.m_Rot);
		std::array<vec2, g_aCubeVertices.size()> aProjected;
		for(size_t i = 0; i < g_aCubeVertices.size(); i++)
		{
			const vec3 Local = g_aCubeVertices[i] * RenderSize;
			aProjected[i] = ProjectPoint(RotateVec3(Local, Rot) + RenderPos, CameraCenter);
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

		const SRotation Rot = MakeRotation(Part.m_Rot);
		const auto &Verts = HeartVertices();
		const float Scale = RenderSize * HEART_SIZE_SCALE;
		std::array<std::array<vec2, HEART_POINTS>, HEART_LAYERS> aProjected;
		for(int L = 0; L < HEART_LAYERS; L++)
		{
			const float LayerT = L == 0 ? -1.0f : 1.0f;
			const float Z = LayerT * (RenderSize * HEART_THICKNESS);
			const float LayerScale = 1.0f - std::abs(LayerT) * 0.06f;
			for(int i = 0; i < HEART_POINTS; i++)
			{
				const vec3 Local = vec3(Verts[i].x * Scale * LayerScale, Verts[i].y * Scale * LayerScale, Z);
				aProjected[L][i] = ProjectPoint(RotateVec3(Local, Rot) + RenderPos, CameraCenter);
			}
		}

		std::array<IGraphics::CLineItem, HEART_POINTS * 3> aLines;
		size_t LineCount = 0;
		for(int L = 0; L < HEART_LAYERS; L++)
		{
			for(int i = 0; i < HEART_POINTS; i++)
			{
				const int Next = (i + 1) % HEART_POINTS;
				aLines[LineCount++] = IGraphics::CLineItem(aProjected[L][i], aProjected[L][Next]);
			}
		}
		for(int i = 0; i < HEART_POINTS; i++)
			aLines[LineCount++] = IGraphics::CLineItem(aProjected[0][i], aProjected[1][i]);
		Graphics()->LinesDraw(aLines.data(), LineCount);
	};

	auto DrawParticle = [&](const SParticle &Part, const vec3 &ExtraOffset, float AlphaMul) {
		if(Part.m_Pos.x < CullMinX || Part.m_Pos.x > CullMaxX || Part.m_Pos.y < CullMinY || Part.m_Pos.y > CullMaxY)
			return;

		const float FinalAlpha = BaseAlpha * AlphaMul;
		if(FinalAlpha <= 0.0f)
			return;

		const vec3 RenderPos = Part.m_Pos + ExtraOffset;
		if(Part.m_Type == SHAPE_CUBE)
			DrawCube(Part, RenderPos, Part.m_Size, FinalAlpha);
		else
			DrawHeart(Part, RenderPos, Part.m_Size, FinalAlpha);
	};

	Graphics()->LinesBegin();
	for(const auto &Part : m_vParticles)
	{
		if(GlowEnabled)
			DrawParticle(Part, GlowOffsetVec, GLOW_ALPHA);
		DrawParticle(Part, vec3(0.0f, 0.0f, 0.0f), 1.0f);
	}
	Graphics()->LinesEnd();
}
