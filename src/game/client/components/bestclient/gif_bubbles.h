/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_GIF_BUBBLES_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_GIF_BUBBLES_H

#include <game/client/component.h>

class CGifBubbles : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
};

#endif
