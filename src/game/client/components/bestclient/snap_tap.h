/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_SNAP_TAP_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_SNAP_TAP_H

#include <engine/client.h>

#include <game/client/component.h>

class CSnapTap : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }

	void OnReset() override;

	void Reset(int Dummy);
	int ResolveMovementDirection(int Dummy, bool LeftPressed, bool RightPressed, bool ShouldUpdateState);
	bool IsBlockedByCommunity() const;

private:
	int m_aAppliedDirection[NUM_DUMMIES] = {};
	int m_aLastPressedDirection[NUM_DUMMIES] = {};
	int64_t m_aLastPressedTime[NUM_DUMMIES] = {};
	int m_aPrevLeft[NUM_DUMMIES] = {};
	int m_aPrevRight[NUM_DUMMIES] = {};

	bool IsActive() const;
	void UpdateState(int Dummy, bool LeftPressed, bool RightPressed);
	int ResolveDirection(int Dummy, bool LeftPressed, bool RightPressed);
};

#endif
