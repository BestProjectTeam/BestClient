/* (c) BestClient */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_CAVA_HUD_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_CAVA_HUD_H

#include <game/client/component.h>

#include <cstdint>
#include <vector>

class CCavaHud : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }

	void OnReset() override;
	void OnShutdown() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnUpdate() override;
	void OnRender() override;

private:
	std::vector<float> m_vLevels;
	std::vector<float> m_vScratch;

	int m_LastBars = 0;

	void EnsureBars();
	void UpdateLevelsFromAudio(float Delta);
};

#endif
