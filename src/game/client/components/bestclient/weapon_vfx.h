/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_WEAPON_VFX_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_WEAPON_VFX_H

#include <base/color.h>
#include <base/vmath.h>

#include <game/client/component.h>

class CProjectileData;

class CWeaponVfx : public CComponent
{
public:
	enum EGlowMode
	{
		MODE_CLASSIC = 0,
		MODE_PRISM = 1,
		MODE_PULSE = 2,
		MODE_CRYSTAL = 3,
		NUM_MODES
	};

	struct CShockwaveRing
	{
		bool m_Active;
		vec2 m_Pos;
		float m_Life;
		float m_MaxLife;
		float m_MaxRadius;
		float m_Thickness;
		ColorRGBA m_Color;
		int m_Mode;
	};

	struct CExplosionDebris
	{
		bool m_Active;
		vec2 m_Pos;
		vec2 m_Vel;
		float m_Life;
		float m_MaxLife;
		float m_Size;
		ColorRGBA m_Color;
		int m_ShapeType;
		float m_Rot;
		float m_RotSpeed;
	};

	struct CFlashCore
	{
		bool m_Active;
		vec2 m_Pos;
		float m_Life;
		float m_MaxLife;
		float m_Size;
		ColorRGBA m_Color;
	};

	struct CNukePuff
	{
		bool m_Active;
		vec2 m_Pos;
		vec2 m_Vel;
		float m_Life;
		float m_MaxLife;
		float m_StartSize;
		float m_EndSize;
		float m_Rot;
		float m_RotSpeed;
		ColorRGBA m_StartColor;
		ColorRGBA m_EndColor;
		int m_Layer;
		int m_StackLevel;
		vec2 m_OriginPos;
		float m_OrbitRadius;
		float m_OrbitAngle;
		float m_OrbitSpeed;
	};

	struct CBlackHoleSingularity
	{
		bool m_Active;
		vec2 m_Pos;
		float m_Life;
		float m_MaxLife;
		float m_Scale;
		float m_SpinAngle;
		float m_Alpha;
	};

	struct CQuasarHyperBlast
	{
		bool m_Active;
		vec2 m_Pos;
		float m_Life;
		float m_MaxLife;
		float m_Scale;
		float m_Alpha;
	};

	struct CRecentExplosion
	{
		vec2 m_Pos;
		int64_t m_TimeMs;
	};

private:
	static const int MAX_SHOCKWAVES = 32;
	CShockwaveRing m_aShockwaves[MAX_SHOCKWAVES]{};

	static const int MAX_DEBRIS = 256;
	CExplosionDebris m_aDebris[MAX_DEBRIS]{};

	static const int MAX_FLASHES = 16;
	CFlashCore m_aFlashes[MAX_FLASHES]{};

	static const int MAX_NUKE_PUFFS = 640;
	CNukePuff m_aNukePuffs[MAX_NUKE_PUFFS]{};

	static const int MAX_BLACK_HOLES = 8;
	CBlackHoleSingularity m_aBlackHoles[MAX_BLACK_HOLES]{};

	static const int MAX_QUASARS = 8;
	CQuasarHyperBlast m_aQuasars[MAX_QUASARS]{};

	static const int MAX_RECENT_EXPLOSIONS = 32;
	CRecentExplosion m_aRecentExplosions[MAX_RECENT_EXPLOSIONS]{};
	int m_RecentExplosionIndex = 0;
	float m_ShakeIntensity = 0.0f;
	float m_ShakeDuration = 0.0f;
	float m_ShakeTimer = 0.0f;
	float m_ShakeTime = 0.0f;
	double m_EffectTime = 0.0;
	float m_NukeAlpha = 1.0f;
	bool m_HadRocketVfx = false;

public:
	int Sizeof() const override { return sizeof(*this); }
	bool RenderWeaponLaser(vec2 From, vec2 Pos, ColorRGBA OuterColor, ColorRGBA InnerColor, float TicksBody, float TicksHead, int Type) const;
	bool RenderRocketTrail(const CProjectileData &Data, float Time, float Curvature, float Speed, float Alpha);
	bool TriggerExplosion(vec2 Pos, float Alpha);
	void AddShake(float Intensity, float Duration, vec2 SourcePos);
	void ApplyCameraShake(vec2 &Center);
	void OnInit() override;
	void OnRender() override;
	void OnReset() override;

	void UpdateSimulation(float Dt);
	void RenderActiveVfx() const;

	void RenderRocketTrajectory(const vec2 *pPoints, const float *pAlphas, int NumPoints, int Mode, float Power);
	void RenderExplosion(vec2 Pos, const ColorRGBA &Color, int Mode, float Power);
	void TriggerNukeExplosion(vec2 Pos, const ColorRGBA &Color, float Scale, int StackLevel = 1);
	void TriggerBombExplosion(vec2 Pos, const ColorRGBA &Color, float Scale);
	void TriggerNuclearMushroom(vec2 Pos, const ColorRGBA &Color, float Scale);
	void TriggerBlackHoleSingularity(vec2 Pos, const ColorRGBA &Color, float Scale);
	void TriggerQuasarHyperBlast(vec2 Pos, const ColorRGBA &Color, float Scale);
	void CancelLowerTierVfx(vec2 Pos, int NewStackLevel, float Radius = 450.0f);
	void RenderLaser(vec2 From, vec2 Pos, const ColorRGBA &OuterColor, const ColorRGBA &InnerColor, float WidthScale, float TicksHead, int Mode, float Power) const;
	void RenderImpactStar(vec2 Pos, const ColorRGBA &Color, float Scale, float TicksHead) const;

	void RenderLaserRings(vec2 Pos, const ColorRGBA &Color, float Power, float TicksHead, int Mode) const;
	void CreateShockwave(vec2 Pos, float MaxRadius = 110.0f, float Duration = 0.35f, const ColorRGBA &Color = ColorRGBA(1.0f, 0.70f, 0.25f, 0.90f));
};

#endif
