/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_FGF_WEATHER_H
#define GAME_CLIENT_COMPONENTS_FGF_WEATHER_H
#include <base/color.h>
#include <base/vmath.h>

#include <engine/graphics.h>

#include <game/client/component.h>
#include "fgf_glow.h"

enum
{
	FGF_WEATHER_SNOW = 0,
	FGF_WEATHER_RAIN,
	FGF_WEATHER_FIREFLIES,
	FGF_WEATHER_DUST,
};

class CFgfWeather : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnReset() override;
	void OnRender() override;

private:
	static constexpr int MAX_DROPS = 1024;
	static constexpr int MAX_SPLASHES = 256;

	struct CDrop
	{
		vec2 m_Pos;
		vec2 m_Vel;
		float m_Life;
		float m_LifeSpan;
		float m_Size;
		float m_Phase;
		bool m_Settled;
	};

	struct CSplash
	{
		vec2 m_Pos;
		vec2 m_Vel;
		float m_Life;
		float m_LifeSpan;
		float m_Size;
	};

	IGraphics::CTextureHandle m_GlowTexture;

	int m_Kind = FGF_WEATHER_SNOW;
	CDrop m_aDrops[MAX_DROPS];
	int m_NumDrops = 0;
	CSplash m_aSplashes[MAX_SPLASHES];
	int m_NumSplashes = 0;
	float m_SpawnAccumulator = 0.0f;
	float m_LastTime = 0.0f;

	CDrop *NewDrop();
	void Splash(vec2 Pos);
	void Spawn(const CScreenRect &View, float Passed, float Time);
	void Update(const CScreenRect &View, float Passed, float Time);
	void Draw(float Time);
	void AmbientDust();
};
#endif
