/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_AUTOMARGIN_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_AUTOMARGIN_H

#include <game/client/component.h>

class CBcAutoMargin : public CComponent
{
	float m_CheckTimer = 0.0f;
	float m_LatestPing = -1.0f;
	float m_IntervalPingSum = 0.0f;
	int m_IntervalPingSamples = 0;
	float m_IntervalMinPing = -1.0f;
	float m_IntervalPeakPing = -1.0f;
	float m_SmoothedPing = -1.0f;
	float m_SmoothedJitter = 0.0f;
	bool m_HighPing = false;

	void ResetState();

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnNewSnapshot() override;
	void OnUpdate() override;
};

#endif // GAME_CLIENT_COMPONENTS_BESTCLIENT_AUTOMARGIN_H
