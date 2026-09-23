/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_FLYING_NAME_PLATES_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_FLYING_NAME_PLATES_H

#include <base/color.h>
#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/client/component.h>

#include <array>

struct CNetObj_PlayerInfo;

class CFlyingNamePlates : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }

	void OnReset() override;

	void Update(int ClientId, vec2 Position);
	void RenderRopeGame(vec2 Position, const CNetObj_PlayerInfo *pPlayerInfo, float Alpha);
	vec2 GetRenderPos(int ClientId, vec2 TeePos) const;
	vec2 PreviewPos(vec2 NamePlateBasePos, vec2 TeeDirection) const;
	void RenderPreviewRope(vec2 TeePos, vec2 FlyingPos, ColorRGBA Color) const;

private:
	struct CState
	{
		bool m_Initialized = false;
		vec2 m_CurrentPos = vec2(0.0f, 0.0f);
		vec2 m_PrevPlayerPos = vec2(0.0f, 0.0f);
		float m_LastUpdateTime = -1.0f;
	};

	std::array<CState, MAX_CLIENTS> m_aStates{};

	ColorRGBA ColorForPlayer(vec2 Position, const CNetObj_PlayerInfo *pPlayerInfo, float Alpha) const;
	static vec2 AnchorPos(vec2 TeePos);
	static void RenderLine(class CGameClient &This, vec2 Anchor, vec2 NamePlatePos, ColorRGBA Color);
};

#endif
