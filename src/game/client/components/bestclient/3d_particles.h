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
		int m_Type;
	};

	std::vector<SParticle> m_vParticles;
	vec2 m_LastLocalPos = vec2(0.0f, 0.0f);
	bool m_HasLastLocalPos = false;
	bool m_HasConfigSnapshot = false;

	int m_LastType = 0;
	int m_LastSizeMax = 0;
	int m_LastDensity = 0;
	int m_LastColorMode = 0;
	unsigned m_LastColor = 0;
	int m_LastMapW = 0;
	int m_LastMapH = 0;
	int m_LastAutoCount = 0;

	void ResetParticles();
	void RenderParticles(float CullMinX, float CullMaxX, float CullMinY, float CullMaxY, float BaseAlpha);
};

#endif
