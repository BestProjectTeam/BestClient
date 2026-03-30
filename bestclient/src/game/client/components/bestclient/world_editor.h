/* (c) BestClient */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_WORLD_EDITOR_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_WORLD_EDITOR_H

#include <engine/console.h>

#include <game/client/component.h>
#include <game/client/ui.h>

class CWorldEditor : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnReset() override;
	void OnWindowResize() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnMapLoad() override;
	void OnShutdown() override;

	void TogglePanel();
	void SetPanelActive(bool Active);
	bool IsPanelActive() const;
	bool HasAnyEffectEnabled() const;
	bool ShouldUsePostProcess() const;
	void InvalidateHistory();
	void BeginWorldRender();
	void EndWorldRender();
	void RenderSettingsPage(CUIRect MainView);

private:
	enum ESection
	{
		SECTION_MOTION_BLUR = 0,
		SECTION_BLOOM,
		SECTION_OUTLINE,
		NUM_SECTIONS,
	};

	struct SRuntimeTarget
	{
		IGraphics::CRenderTargetHandle m_Target;
		int m_Width = 0;
		int m_Height = 0;
	};

	static void ConToggleWorldEditor(IConsole::IResult *pResult, void *pUserData);

	bool EnsureTargets(int Width, int Height);
	void DestroyTargets();
	void ResetEffectSettings();
	bool IsOpenGLBackend() const;
	bool IsVulkanBackend() const;
	vec2 CurrentCameraCenter() const;

	bool m_aSectionOpen[NUM_SECTIONS] = {true, true, true};

	SRuntimeTarget m_WorldTarget;
	SRuntimeTarget m_HistoryTarget;
	bool m_HasValidHistory = false;
	vec2 m_LastCameraCenter = vec2(0.0f, 0.0f);

	CButtonContainer m_ResetSettingsButton;
	CButtonContainer m_GlobalToggleButton;
	CButtonContainer m_SectionButtons[NUM_SECTIONS];
	CButtonContainer m_MotionBlurToggleButton;
	CButtonContainer m_BloomToggleButton;
	CButtonContainer m_OutlineToggleButton;
};

#endif
