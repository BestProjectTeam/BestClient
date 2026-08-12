#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_SPEC_PAUSE_RADIO_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_SPEC_PAUSE_RADIO_H

#include <base/vmath.h>

#include <engine/console.h>
#include <engine/input.h>

#include <game/client/component.h>
#include <game/client/ui.h>

#include <cstdint>

enum ESpecPauseType
{
	SPEC_PAUSE_NONE = -1,
	SPEC_PAUSE_PAUSE,
	SPEC_PAUSE_SPEC,
};

class CSpecPauseRadio : public CComponent
{
	bool m_InitializedSelectorMouse = false;
	vec2 m_SelectorMouse = vec2(0.0f, 0.0f);
	int m_SelectedType = SPEC_PAUSE_NONE;
	bool m_Active = false;
	bool m_WasActive = false;
	CUi::CTouchState m_TouchState;
	bool m_TouchPressedOutside = false;

	int64_t m_ActiveSince = 0;

	void SendSpecPause(int Type);
	static void ConOpenRadio(IConsole::IResult *pResult, void *pUserData);
	static void ConchainBetterSpectate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

public:
	CSpecPauseRadio();
	int Sizeof() const override { return sizeof(*this); }

	void OnReset() override;
	void OnRender() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnConsoleInit() override;
	void OnRelease() override;
	bool OnInput(const IInput::CEvent &Event) override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;

	void SyncSpectateBinds(bool EnableBetterSpectate);
	bool IsActive() const { return m_Active; }
};

#endif // GAME_CLIENT_COMPONENTS_BESTCLIENT_SPEC_PAUSE_RADIO_H
