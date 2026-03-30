#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_ORBIT_AURA_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_ORBIT_AURA_H

#include <base/color.h>
#include <base/vmath.h>

#include <game/client/component.h>

class COrbitAura : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	void OnRender() override;

private:
	float m_SpawnAccumulator = 0.0f;
	float m_LastMovementTime = 0.0f;
	float m_IdleVisibility = 0.0f;
	vec2 m_LastPosition = vec2(0.0f, 0.0f);
	bool m_HasLastPosition = false;

	void ResetState();
	float GetIdleVisibility(const vec2 &BasePos, float Delta, float CurrentTime);
	bool CanRenderAura(int &ClientId, vec2 &BasePos, ColorRGBA &BaseColor, float &Alpha);
};

#endif
