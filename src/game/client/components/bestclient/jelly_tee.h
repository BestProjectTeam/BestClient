/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_JELLY_TEE_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_JELLY_TEE_H

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/client/component.h>

#include <array>

struct SJellyTeeDeform
{
	vec2 m_BodyScale = vec2(1.0f, 1.0f);
	vec2 m_FeetScale = vec2(1.0f, 1.0f);
	float m_BodyAngle = 0.0f;
	float m_FeetAngle = 0.0f;
};

class CJellyTee : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }

	void OnReset() override;
	void OnRender() override;

	SJellyTeeDeform GetDeform(int ClientId, vec2 PrevVel, vec2 Vel, vec2 LookDir, bool InAir, bool WantOtherDir, vec2 Position);

private:
	static constexpr int MAX_JELLY_CLIENTS = MAX_CLIENTS;

	struct CState
	{
		vec2 m_Deform = vec2(0.0f, 0.0f);
		vec2 m_DeformVelocity = vec2(0.0f, 0.0f);
		vec2 m_PrevInputVel = vec2(0.0f, 0.0f);
		float m_Compression = 0.0f;
		float m_CompressionVelocity = 0.0f;
		float m_WallCompression = 0.0f;
		float m_WallCompressionVelocity = 0.0f;
		float m_LastDemoPlaybackTime = 0.0f;
		int m_LastHammerImpactTick = -1;
		int m_LastUpdateFrame = -1;
		SJellyTeeDeform m_CachedDeform{};
		bool m_Initialized = false;
		bool m_HasLastDemoPlaybackTime = false;
	};

	std::array<CState, MAX_JELLY_CLIENTS> m_aStates{};
	int m_FrameId = 0;

	bool IsEnabledFor(int ClientId) const;
	CState *StateFor(int ClientId);
	void BuildExtraImpulse(int ClientId, vec2 Position, vec2 PrevVel, vec2 Vel, vec2 LookDir, vec2 &OutExtraDeformImpulse, float &OutExtraCompression, float &OutWallSquash);
	bool HasLocalHammerImpact(CState *pState, int ClientId);
	void ApplySurfaceImpacts(vec2 Position, vec2 PrevVel, vec2 Vel, vec2 &OutExtraDeformImpulse, float &OutExtraCompression, float &OutWallSquash) const;
};

#endif
