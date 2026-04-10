/* Copyright В© 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_R_TRAIL_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_R_TRAIL_H

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <array>
#include <memory>

class CGameClient;

class CRTrail
{
public:
	explicit CRTrail(CGameClient *pClient);

	void Reset();
	void RenderPlayerTrail(int id, vec2 pos, vec2 bodyPos, vec2 vel, float alpha, float dt);

private:
	static constexpr int MAX_TRAIL_CLIENTS = MAX_CLIENTS;

	struct CState
	{
		vec2 m_LastPos = vec2(0.0f, 0.0f);
		vec2 m_LastVel = vec2(0.0f, 0.0f);
		bool m_Initialized = false;
	};

	CGameClient *m_pClient = nullptr;
	std::array<CState, MAX_TRAIL_CLIENTS> m_aStates{};

	bool IsEnabledFor(int id) const;
	CState *StateFor(int id);
};

extern std::unique_ptr<CRTrail> rTrail;

#endif
