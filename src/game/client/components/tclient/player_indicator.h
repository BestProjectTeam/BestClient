#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_PLAYER_INDICATOR_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_PLAYER_INDICATOR_H

#include <game/client/component.h>

class CPlayerIndicator : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
};

#endif
