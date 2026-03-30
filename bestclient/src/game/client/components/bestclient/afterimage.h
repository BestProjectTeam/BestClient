#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_AFTERIMAGE_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_AFTERIMAGE_H

#include <base/vmath.h>

#include <game/client/component.h>
#include <game/client/render.h>

#include <deque>

class CAfterimage : public CComponent
{
	struct SGhostSample
	{
		vec2 m_Pos;
		vec2 m_Dir;
		int m_Weapon;
		int m_AttackTick;
	};

	std::deque<SGhostSample> m_vGhostSamples;
	CTeeRenderInfo m_LastRenderInfo{};
	bool m_HasLastRenderInfo = false;

	void RenderAfterimageWeapon(const SGhostSample &Sample, float Alpha);
	void ResetState();
	bool CanSampleAfterimage(int &LocalClientId) const;

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnRender() override;
};

#endif
