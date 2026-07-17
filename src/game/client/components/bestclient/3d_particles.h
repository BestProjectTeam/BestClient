/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_3D_PARTICLES_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_3D_PARTICLES_H

#include <base/color.h>
#include <base/vmath.h>

#include <game/client/component.h>

#include <vector>

class C3DParticles : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnReset() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnRender() override;

private:
	struct SParticle
	{
		vec3 m_Pos;
		vec3 m_Vel;
		vec3 m_Rot;
		vec3 m_RotVel;
		ColorRGBA m_Color;
		float m_Size;
		vec3 m_SpawnOffset;
		vec3 m_FadeOutOffset;
		float m_SpawnTime;
		float m_FadeOutStart;
		int m_Type;
		bool m_FadingOut;
	};

	std::vector<SParticle> m_vParticles;
	float m_Time = 0.0f;
	vec2 m_LastLocalPos = vec2(0.0f, 0.0f);
	bool m_HasLastLocalPos = false;
	bool m_HasConfigSnapshot = false;

	int m_LastType = 0;
	int m_LastCount = 0;
	int m_LastSizeMax = 0;
	int m_LastSpeed = 0;
	int m_LastAlpha = 0;
	int m_LastColorMode = 0;
	unsigned m_LastColor = 0;
	int m_LastGlow = 0;
	int m_LastGlowAlpha = 0;
	int m_LastGlowOffset = 0;
	int m_LastDepth = 0;
	int m_LastFadeInMs = 0;
	int m_LastFadeOutMs = 0;
	int m_LastPushRadius = 0;
	int m_LastPushStrength = 0;
	int m_LastCollide = 0;
	int m_LastViewMargin = 0;

	void ResetParticles();
	void RenderParticles(float ViewMinX, float ViewMaxX, float ViewMinY, float ViewMaxY, float BaseAlpha, float FadeIn, float FadeOut);
};

#endif
