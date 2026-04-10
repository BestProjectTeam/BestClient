/* Copyright В© 2026 BestProject Team */
#include "r_trail.h"

#include <engine/shared/config.h>

#include <game/client/components/effects.h>
#include <game/client/gameclient.h>

std::unique_ptr<CRTrail> rTrail;

namespace
{
	float clampf(float v, float min, float max)
	{
		if(v < min)
			return min;
		if(v > max)
			return max;
		return v;
	}

	int clampi(int v, int min, int max)
	{
		if(v < min)
			return min;
		if(v > max)
			return max;
		return v;
	}

	constexpr float MIN_DELTA_TIME = 1.0f / 240.0f;
	constexpr float MAX_DELTA_TIME = 1.0f / 20.0f;
	constexpr float MIN_TRAIL_MOVEMENT = 0.75f;
	constexpr float MAX_TRAIL_JUMP = 96.0f;
	constexpr float TRAIL_SAMPLE_DISTANCE = 8.0f;
	constexpr int MAX_TRAIL_SAMPLES = 24;

	enum ETrailMode
	{
		TRAIL_MODE_GRENADE = 0,
		TRAIL_MODE_GUN,
		TRAIL_MODE_NINJA,
	};

	float sample_dist(int mode)
	{
		if(mode == TRAIL_MODE_GUN)
			return 24.0f;
		if(mode == TRAIL_MODE_NINJA)
			return 14.0f;
		return TRAIL_SAMPLE_DISTANCE;
	}

	void emit(CGameClient *pClient, int mode, vec2 pos, vec2 bodyPos, vec2 vel, float alpha, float timePassed)
	{
		if(pClient == nullptr)
			return;

		if(mode == TRAIL_MODE_GUN)
		{
			if(timePassed <= 0.0001f)
				pClient->m_Effects.SparkleTrail(bodyPos, alpha);
		}
		else if(mode == TRAIL_MODE_NINJA)
		{
			if(timePassed <= 0.0001f)
				pClient->m_Effects.PowerupShine(bodyPos, vec2(20.0f, 20.0f), alpha);
		}
		else
		{
			pClient->m_Effects.SmokeTrail(pos, vel * -1.0f, alpha, timePassed);
		}
	}
} // namespace

CRTrail::CRTrail(CGameClient *pClient) :
	m_pClient(pClient)
{
}

void CRTrail::Reset()
{
	for(auto &State : m_aStates)
		State = CState();
}

bool CRTrail::IsEnabledFor(int id) const
{
	if(m_pClient == nullptr || !g_Config.m_BcTrail || id < 0 || id >= MAX_TRAIL_CLIENTS)
		return false;

	for(int i = 0; i < NUM_DUMMIES; ++i)
	{
		if(id == m_pClient->m_aLocalIds[i])
			return true;
	}

	return g_Config.m_BcTrailOthers != 0;
}

CRTrail::CState *CRTrail::StateFor(int id)
{
	if(id < 0 || id >= MAX_TRAIL_CLIENTS)
		return nullptr;
	return &m_aStates[id];
}

void CRTrail::RenderPlayerTrail(int id, vec2 pos, vec2 bodyPos, vec2 vel, float alpha, float dt)
{
	if(m_pClient == nullptr || alpha <= 0.0f || id < 0)
		return;

	if(!IsEnabledFor(id))
	{
		CState *p = StateFor(id);
		if(p)
			*p = CState();
		return;
	}

	CState *p = StateFor(id);
	if(p == nullptr)
		return;

	dt = clampf(dt, MIN_DELTA_TIME, MAX_DELTA_TIME);

	if(!p->m_Initialized)
	{
		p->m_LastPos = pos;
		p->m_LastVel = vel;
		p->m_Initialized = true;
		return;
	}

	const vec2 d = pos - p->m_LastPos;
	const float dist = length(d);
	if(dist > MAX_TRAIL_JUMP)
	{
		p->m_LastPos = pos;
		p->m_LastVel = vel;
		return;
	}

	if(dist < MIN_TRAIL_MOVEMENT && length(vel) < 0.05f)
	{
		p->m_LastPos = pos;
		p->m_LastVel = vel;
		return;
	}

	const int mode = g_Config.m_BcTrailMode;
	const float step = sample_dist(mode);
	const int num = clampi((int)(dist / step), 1, MAX_TRAIL_SAMPLES);
	const vec2 from = p->m_LastPos;
	const vec2 oldVel = p->m_LastVel;

	for(int i = 1; i <= num; ++i)
	{
		const float prevT = (float)(i - 1) / (float)num;
		const float t = (float)i / (float)num;
		const vec2 prevPos = mix(from, pos, prevT);
		const vec2 ppos = mix(from, pos, t);
		vec2 v = ppos - prevPos;
		if(length(v) < 0.001f)
			v = mix(oldVel, vel, t) * 0.12f;

		const float tp = (1.0f - t) * dt;
		emit(m_pClient, mode, ppos, bodyPos, v, alpha, tp);
	}

	p->m_LastPos = pos;
	p->m_LastVel = vel;
}
