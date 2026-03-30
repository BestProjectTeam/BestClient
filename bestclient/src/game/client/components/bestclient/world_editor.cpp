/* (c) BestClient */
#include "world_editor.h"

#include <base/math.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/localization.h>
#include <base/system.h>

#include <game/client/gameclient.h>
#include <game/localization.h>

namespace
{
constexpr float SECTION_GAP = 6.0f;
constexpr float SECTION_MARGIN = 10.0f;
constexpr float INFO_LINE_HEIGHT = 18.0f;
constexpr float ROW_HEIGHT = 24.0f;

float ConfigToFloat01(int Value)
{
	return std::clamp(Value / 100.0f, 0.0f, 1.0f);
}
} // namespace

void CWorldEditor::ConToggleWorldEditor(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	static_cast<CWorldEditor *>(pUserData)->TogglePanel();
}

void CWorldEditor::OnConsoleInit()
{
	Console()->Register("toggle_world_editor", "", CFGFLAG_CLIENT, ConToggleWorldEditor, this, "Open or close the World Editor settings page");
}

void CWorldEditor::OnReset()
{
	InvalidateHistory();
}

void CWorldEditor::OnWindowResize()
{
	InvalidateHistory();
}

void CWorldEditor::OnStateChange(int NewState, int OldState)
{
	(void)OldState;
	(void)NewState;
	InvalidateHistory();
}

void CWorldEditor::OnMapLoad()
{
	InvalidateHistory();
}

void CWorldEditor::OnShutdown()
{
	DestroyTargets();
}

void CWorldEditor::TogglePanel()
{
	if(GameClient()->m_Menus.IsWorldEditorSettingsOpen())
		GameClient()->m_Menus.CloseWorldEditorSettings();
	else
		GameClient()->m_Menus.OpenWorldEditorSettings();
}

void CWorldEditor::SetPanelActive(bool Active)
{
	if(Active)
		GameClient()->m_Menus.OpenWorldEditorSettings();
	else
		GameClient()->m_Menus.CloseWorldEditorSettings();
}

bool CWorldEditor::IsPanelActive() const
{
	return GameClient()->m_Menus.IsWorldEditorSettingsOpen();
}

bool CWorldEditor::HasAnyEffectEnabled() const
{
	return g_Config.m_BcWorldEditorMotionBlur || g_Config.m_BcWorldEditorBloom || g_Config.m_BcWorldEditorOutline;
}

bool CWorldEditor::ShouldUsePostProcess() const
{
	if(!g_Config.m_BcWorldEditor)
		return false;
	if(!HasAnyEffectEnabled())
		return false;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return false;
	if(!Graphics()->IsConfigModernAPI())
		return false;
	int Major = 0;
	int Minor = 0;
	int Patch = 0;
	const char *pBackendName = nullptr;
	if(!Graphics()->GetDriverVersion(GRAPHICS_DRIVER_AGE_TYPE_DEFAULT, Major, Minor, Patch, pBackendName, BACKEND_TYPE_AUTO))
		return false;
	if(pBackendName == nullptr)
		return false;
	if(str_comp_nocase(pBackendName, "OpenGL") != 0 && str_comp_nocase(pBackendName, "OpenGL ES") != 0 && str_comp_nocase(pBackendName, "Vulkan") != 0)
		return false;
	return true;
}

void CWorldEditor::InvalidateHistory()
{
	m_HasValidHistory = false;
}

bool CWorldEditor::EnsureTargets(int Width, int Height)
{
	if(Width <= 0 || Height <= 0)
		return false;

	const bool MainReady = m_WorldTarget.m_Target.IsValid() && m_WorldTarget.m_Width == Width && m_WorldTarget.m_Height == Height;
	const bool HistoryReady = m_HistoryTarget.m_Target.IsValid() && m_HistoryTarget.m_Width == Width && m_HistoryTarget.m_Height == Height;
	if(MainReady && HistoryReady)
		return true;

	DestroyTargets();

	m_WorldTarget.m_Target = Graphics()->CreateRenderTarget(Width, Height);
	m_WorldTarget.m_Width = Width;
	m_WorldTarget.m_Height = Height;
	m_HistoryTarget.m_Target = Graphics()->CreateRenderTarget(Width, Height);
	m_HistoryTarget.m_Width = Width;
	m_HistoryTarget.m_Height = Height;

	const bool Ready = m_WorldTarget.m_Target.IsValid() && m_HistoryTarget.m_Target.IsValid();
	if(!Ready)
		DestroyTargets();
	m_HasValidHistory = false;
	return Ready;
}

void CWorldEditor::DestroyTargets()
{
	if(m_WorldTarget.m_Target.IsValid())
		Graphics()->DeleteRenderTarget(&m_WorldTarget.m_Target);
	if(m_HistoryTarget.m_Target.IsValid())
		Graphics()->DeleteRenderTarget(&m_HistoryTarget.m_Target);

	m_WorldTarget = {};
	m_HistoryTarget = {};
	m_HasValidHistory = false;
}

bool CWorldEditor::IsOpenGLBackend() const
{
	int Major = 0;
	int Minor = 0;
	int Patch = 0;
	const char *pBackendName = nullptr;
	return Graphics()->GetDriverVersion(GRAPHICS_DRIVER_AGE_TYPE_DEFAULT, Major, Minor, Patch, pBackendName, BACKEND_TYPE_AUTO) &&
		pBackendName != nullptr &&
		(str_comp_nocase(pBackendName, "OpenGL") == 0 || str_comp_nocase(pBackendName, "OpenGL ES") == 0);
}

bool CWorldEditor::IsVulkanBackend() const
{
	int Major = 0;
	int Minor = 0;
	int Patch = 0;
	const char *pBackendName = nullptr;
	return Graphics()->GetDriverVersion(GRAPHICS_DRIVER_AGE_TYPE_DEFAULT, Major, Minor, Patch, pBackendName, BACKEND_TYPE_AUTO) &&
		pBackendName != nullptr &&
		str_comp_nocase(pBackendName, "Vulkan") == 0;
}

vec2 CWorldEditor::CurrentCameraCenter() const
{
	return GameClient()->m_Camera.m_Center;
}

void CWorldEditor::BeginWorldRender()
{
	if(!ShouldUsePostProcess())
		return;
	if(!EnsureTargets(Graphics()->ScreenWidth(), Graphics()->ScreenHeight()))
		return;
	if(IsOpenGLBackend())
		Graphics()->BeginRenderTarget(m_WorldTarget.m_Target);
}

void CWorldEditor::EndWorldRender()
{
	if(!ShouldUsePostProcess() || !m_WorldTarget.m_Target.IsValid())
		return;

	if(IsOpenGLBackend())
	{
		Graphics()->EndRenderTarget();
	}
	else
	{
		Graphics()->CaptureScreenToRenderTarget(m_WorldTarget.m_Target);
	}

	const vec2 CameraCenter = CurrentCameraCenter();
	if(m_HasValidHistory && distance(CameraCenter, m_LastCameraCenter) > 600.0f)
		m_HasValidHistory = false;
	m_LastCameraCenter = CameraCenter;

	IGraphics::CPostProcessParams Params;
	auto ResetParams = [&]() {
		for(float &Value : Params.m_aValues)
			Value = 0.0f;
	};

	ResetParams();
	Params.m_aValues[0] = g_Config.m_BcWorldEditorMotionBlur ? ConfigToFloat01(g_Config.m_BcWorldEditorMotionBlurStrength) : 0.0f;
	Params.m_aValues[1] = g_Config.m_BcWorldEditorMotionBlur ? ConfigToFloat01(g_Config.m_BcWorldEditorMotionBlurResponse) : 0.0f;
	Params.m_aValues[2] = (g_Config.m_BcWorldEditorMotionBlur && m_HasValidHistory) ? 1.0f : 0.0f;
	Params.m_aValues[3] = g_Config.m_BcWorldEditorBloom ? ConfigToFloat01(g_Config.m_BcWorldEditorBloomIntensity) : 0.0f;
	Params.m_aValues[4] = ConfigToFloat01(g_Config.m_BcWorldEditorBloomThreshold);
	Params.m_aValues[5] = g_Config.m_BcWorldEditorOutline ? ConfigToFloat01(g_Config.m_BcWorldEditorOutlineIntensity) : 0.0f;
	Params.m_aValues[6] = ConfigToFloat01(g_Config.m_BcWorldEditorOutlineThreshold);
	Graphics()->RenderPostProcess(IGraphics::CRenderTargetHandle(), IGraphics::EPostProcessShader::WORLD_EDITOR_COMBINED,
		Graphics()->GetRenderTargetTexture(m_WorldTarget.m_Target),
		Graphics()->GetRenderTargetTexture(m_HistoryTarget.m_Target),
		Params);

	Graphics()->CaptureScreenToRenderTarget(m_HistoryTarget.m_Target);
	m_HasValidHistory = true;
}

void CWorldEditor::ResetEffectSettings()
{
	g_Config.m_BcWorldEditor = 1;
	g_Config.m_BcWorldEditorMotionBlur = 0;
	g_Config.m_BcWorldEditorMotionBlurStrength = 35;
	g_Config.m_BcWorldEditorMotionBlurResponse = 65;
	g_Config.m_BcWorldEditorBloom = 0;
	g_Config.m_BcWorldEditorBloomIntensity = 40;
	g_Config.m_BcWorldEditorBloomThreshold = 55;
	g_Config.m_BcWorldEditorOutline = 0;
	g_Config.m_BcWorldEditorOutlineIntensity = 45;
	g_Config.m_BcWorldEditorOutlineThreshold = 30;
	InvalidateHistory();
}

void CWorldEditor::RenderSettingsPage(CUIRect MainView)
{
	CUIRect Label;
	MainView.HSplitTop(28.0f, &Label, &MainView);
	Ui()->DoLabel(&Label, Localize("World Editor"), 20.0f, TEXTALIGN_ML);
	MainView.HSplitTop(SECTION_MARGIN, nullptr, &MainView);

	MainView.HSplitTop(INFO_LINE_HEIGHT, &Label, &MainView);
	Ui()->DoLabel(&Label, Localize("Adjust post-processing effects for gameplay and demo playback."), 10.0f, TEXTALIGN_ML);
	MainView.HSplitTop(4.0f, nullptr, &MainView);
	MainView.HSplitTop(INFO_LINE_HEIGHT, &Label, &MainView);
	Ui()->DoLabel(&Label, Localize("This page replaces the old floating World Editor window."), 10.0f, TEXTALIGN_ML);
	MainView.HSplitTop(SECTION_MARGIN, nullptr, &MainView);

	const bool BackendSupported = Graphics()->IsConfigModernAPI() && (IsOpenGLBackend() || IsVulkanBackend());
	const bool CanPreviewNow = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
	const char *pStatusText = !BackendSupported ? Localize("Status: unsupported backend. Use modern OpenGL or Vulkan.") :
		!CanPreviewNow ? Localize("Status: effects are configured here and become visible in-game or in demos.") :
		Localize("Status: effects are active immediately while you are online.");
	const ColorRGBA StatusColor = !BackendSupported ? ColorRGBA(1.0f, 0.45f, 0.35f, 1.0f) :
		!CanPreviewNow ? ColorRGBA(0.8f, 0.85f, 0.95f, 1.0f) :
		ColorRGBA(0.55f, 0.95f, 0.65f, 1.0f);
	MainView.HSplitTop(INFO_LINE_HEIGHT, &Label, &MainView);
	TextRender()->TextColor(StatusColor);
	Ui()->DoLabel(&Label, pStatusText, 10.0f, TEXTALIGN_ML);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	MainView.HSplitTop(SECTION_MARGIN, nullptr, &MainView);

	CUIRect Content = MainView;

	auto AddGap = [&](float Height) { Content.HSplitTop(Height, nullptr, &Content); };
	auto AddRow = [&](float Height, CUIRect &Out) -> bool {
		if(Content.h < Height)
			return false;
		Content.HSplitTop(Height, &Out, &Content);
		return true;
	};

	CUIRect Row;
	if(AddRow(ROW_HEIGHT, Row) && GameClient()->m_Menus.DoButton_CheckBox(&m_GlobalToggleButton, Localize("World Editor enabled"), g_Config.m_BcWorldEditor, &Row))
		g_Config.m_BcWorldEditor ^= 1;
	AddGap(SECTION_GAP);

	auto RenderSlider = [&](const char *pLabel, int *pConfig, CUIRect Rect) {
		CUIRect LabelRect, SliderRect;
		Rect.VSplitLeft(140.0f, &LabelRect, &SliderRect);
		Ui()->DoLabel(&LabelRect, pLabel, 11.0f, TEXTALIGN_ML);
		const float Value = Ui()->DoScrollbarH(pConfig, &SliderRect, ConfigToFloat01(*pConfig));
		*pConfig = std::clamp(round_to_int(Value * 100.0f), 0, 100);
	};

	auto RenderSection = [&](ESection Section, const char *pTitle, CButtonContainer &ToggleButton, int *pEnabled, int *pValue0, const char *pLabel0, int *pValue1, const char *pLabel1) {
		if(AddRow(ROW_HEIGHT, Row))
		{
			CUIRect ExpandButton, ToggleRect;
			Row.VSplitLeft(24.0f, &ExpandButton, &ToggleRect);
			if(GameClient()->m_Menus.DoButton_Menu(&m_SectionButtons[Section], m_aSectionOpen[Section] ? "-" : "+", 0, &ExpandButton))
				m_aSectionOpen[Section] = !m_aSectionOpen[Section];
			if(GameClient()->m_Menus.DoButton_CheckBox(&ToggleButton, pTitle, *pEnabled, &ToggleRect))
				*pEnabled ^= 1;
		}
		if(!m_aSectionOpen[Section])
		{
			AddGap(SECTION_GAP);
			return;
		}
		if(AddRow(ROW_HEIGHT, Row))
			RenderSlider(pLabel0, pValue0, Row);
		if(AddRow(ROW_HEIGHT, Row))
			RenderSlider(pLabel1, pValue1, Row);
		AddGap(SECTION_GAP);
	};

	RenderSection(SECTION_MOTION_BLUR, Localize("Motion Blur"), m_MotionBlurToggleButton, &g_Config.m_BcWorldEditorMotionBlur,
		&g_Config.m_BcWorldEditorMotionBlurStrength, Localize("Strength"),
		&g_Config.m_BcWorldEditorMotionBlurResponse, Localize("Response"));
	RenderSection(SECTION_BLOOM, Localize("Bloom"), m_BloomToggleButton, &g_Config.m_BcWorldEditorBloom,
		&g_Config.m_BcWorldEditorBloomIntensity, Localize("Intensity"),
		&g_Config.m_BcWorldEditorBloomThreshold, Localize("Threshold"));
	RenderSection(SECTION_OUTLINE, Localize("Outline"), m_OutlineToggleButton, &g_Config.m_BcWorldEditorOutline,
		&g_Config.m_BcWorldEditorOutlineIntensity, Localize("Intensity"),
		&g_Config.m_BcWorldEditorOutlineThreshold, Localize("Threshold"));

	if(AddRow(ROW_HEIGHT, Row))
	{
		if(GameClient()->m_Menus.DoButton_Menu(&m_ResetSettingsButton, Localize("Reset effects"), 0, &Row))
			ResetEffectSettings();
	}
}
