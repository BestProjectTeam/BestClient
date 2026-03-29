#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_R_JELLY_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_R_JELLY_H

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <array>
#include <memory>

class CGameClient;

struct JellyTee
{
	vec2 m_BodyScale = vec2(1.0f, 1.0f);
	vec2 m_FeetScale = vec2(1.0f, 1.0f);
	float m_BodyAngle = 0.0f;
	float m_FeetAngle = 0.0f;
};

class CRJelly
{
public:
	explicit CRJelly(CGameClient *pClient);

	void Reset();
	JellyTee GetDeform(int ClientId, vec2 PrevVel, vec2 Vel, vec2 LookDir, bool InAir, bool WantOtherDir, float DeltaTime, vec2 ExtraDeformImpulse = vec2(0.0f, 0.0f), float ExtraCompression = 0.0f);

private:
	static constexpr int MAX_JELLY_CLIENTS = MAX_CLIENTS;

	struct CState
	{
		vec2 m_Deform = vec2(0.0f, 0.0f);
		vec2 m_DeformVelocity = vec2(0.0f, 0.0f);
		vec2 m_PrevInputVel = vec2(0.0f, 0.0f);
		float m_Compression = 0.0f;
		float m_CompressionVelocity = 0.0f;
		int m_ClientId = -1;
		bool m_Initialized = false;
	};

	CGameClient *m_pClient = nullptr;
	std::array<CState, MAX_JELLY_CLIENTS> m_aStates{};

	bool IsEnabledFor(int ClientId) const;
	CState *StateFor(int ClientId);
};

extern std::unique_ptr<CRJelly> rJelly;

#endif
