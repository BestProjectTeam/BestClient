#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_PET_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_PET_H

#include <game/client/component.h>

class CPet : public CComponent
{
private:
	vec2 m_Target;
	vec2 m_Position;
	vec2 m_Velocity;
	vec2 m_Dir;
	float m_Alpha = 0.0f;

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnMapLoad() override;
};

#endif
