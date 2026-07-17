/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_HOOKCOMBO_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_HOOKCOMBO_H

#include <game/client/component.h>

#include <array>
#include <vector>

class CHookCombo : public CComponent
{
	class SPopup
	{
	public:
		int m_Sequence = 1;
		float m_Age = 0.0f;
	};

	std::array<int, 7> m_aSoundIds{};
	std::vector<SPopup> m_vPopups;
	int m_Counter = 0;
	float m_LastHookTime = -1.0f;
	int m_TrackedClientId = -1;
	int m_LastHookedPlayer = -1;
	int m_LastProcessedGameTick = -1;
	bool m_SoundErrorShown = false;

	void LoadSounds(bool LogErrors = true);
	void UnloadSounds();
	void ResetState();
	void TriggerStep();
	bool HasWork() const;

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;
	void OnReset() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnRender() override;

	void Render(bool ForcePreview = false);
};

#endif
