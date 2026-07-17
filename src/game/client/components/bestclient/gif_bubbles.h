/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_GIF_BUBBLES_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_GIF_BUBBLES_H

#include <game/client/component.h>

// Renders a small floating gif bubble above a player's head when they post a chat line that
// is a single link from a gif-bubble domain (see CChat::CLine::m_ShowAboveHead). Reuses the
// already-decoded frames of that chat line, it does not download anything itself.
class CGifBubbles : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
};

#endif // GAME_CLIENT_COMPONENTS_BESTCLIENT_GIF_BUBBLES_H
