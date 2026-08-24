#include "edgehelper.h"

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/client/components/camera.h>
#include <game/client/components/hud_layout.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>

struct SEdgeHelperProperties
{
	static constexpr float ms_Padding = 3.0f;
	static constexpr float ms_Rounding = 3.0f;
	static constexpr float ms_ItemSpacing = 2.0f;
	static constexpr float ms_CubeSize = 24.0f;
	static constexpr float ms_ArrowsSize = 18.0f;
	static constexpr float ms_WallWidth = 3.0f;
	static constexpr float ms_CircleRadius = 8.0f;
	static constexpr float ms_CircleThickness = 2.0f;

	static ColorRGBA WindowColorMedium() { return ColorRGBA(0.35f, 0.35f, 0.35f, 0.9f); }
	static ColorRGBA ActionActiveButtonColor() { return ColorRGBA(0.53f, 0.78f, 0.53f, 0.8f); }
	static ColorRGBA ActionWhiteButtonColor() { return ColorRGBA(1.0f, 1.0f, 1.0f, 0.8f); }
};

static constexpr const char *s_pEdgeInfoAngleUp = "\uF106";
static constexpr const char *s_pEdgeInfoAnglesUp = "\uF102";
static constexpr std::array<int, 14> s_aEdgeInfoJumpPositions = {13, 16, 25, 28, 31, 41, 56, 62, 63, 66, 69, 72, 81, 84};

void CEdgeHelper::OnConsoleInit()
{
	Console()->Register("ri_toggle_edgeinfo", "", CFGFLAG_CLIENT, ConToggleEdgeHelper, this, "Toggle edge info");
}

void CEdgeHelper::ConToggleEdgeHelper(IConsole::IResult *, void *pUserData)
{
	auto *pSelf = static_cast<CEdgeHelper *>(pUserData);
	pSelf->SetActive(!pSelf->IsActive());
}

void CEdgeHelper::SetActive(bool Active)
{
	if(m_Active == Active)
		return;

	m_Active = Active;
	if(m_Active && !g_Config.m_RiEdgeInfoJump && !g_Config.m_RiEdgeInfoCords)
	{
		GameClient()->Echo("Enable any edgeinfo function");
		m_Active = false;
	}
}

void CEdgeHelper::OnReset()
{
	m_Active = false;
}

void CEdgeHelper::OnRelease()
{
	m_Active = false;
}

void CEdgeHelper::OnRender()
{
	if(IsActive())
		RenderEdgeHelper(false);
}

CUIRect CEdgeHelper::GetRect(bool ForcePreview) const
{
	if(!ForcePreview && !HudLayout::IsEnabled(HudLayout::MODULE_EDGE_INFO))
		return {};

	int PanelCount = (g_Config.m_RiEdgeInfoCords != 0) + (g_Config.m_RiEdgeInfoJump != 0);
	if(PanelCount == 0)
	{
		if(!ForcePreview)
			return {};
		PanelCount = 2;
	}

	const float HudHeight = HudLayout::CANVAS_HEIGHT;
	const float HudWidth = HudHeight * Graphics()->ScreenAspect();
	const auto Layout = HudLayout::Get(HudLayout::MODULE_EDGE_INFO, HudWidth, HudHeight);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);

	CUIRect Rect;
	Rect.w = HudWidth / 5.0f * Scale;
	Rect.h = (PanelCount == 2 ? 50.0f : 25.0f) * Scale;
	Rect.x = std::clamp(Layout.m_X, 0.0f, maximum(0.0f, HudWidth - Rect.w));
	Rect.y = std::clamp(Layout.m_Y, 0.0f, maximum(0.0f, HudHeight - Rect.h));
	return Rect;
}

void CEdgeHelper::RenderEdgeHelper(bool ForcePreview)
{
	const float HudHeight = HudLayout::CANVAS_HEIGHT;
	const float HudWidth = HudHeight * Graphics()->ScreenAspect();
	Graphics()->MapScreen(0.0f, 0.0f, HudWidth, HudHeight);

	CUIRect Base = GetRect(ForcePreview);
	if(Base.w <= 0.0f || Base.h <= 0.0f)
		return;

	const auto Layout = HudLayout::Get(HudLayout::MODULE_EDGE_INFO, HudWidth, HudHeight);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	const bool ShowEdgeInfo = g_Config.m_RiEdgeInfoCords != 0 || (ForcePreview && !g_Config.m_RiEdgeInfoJump);
	const bool ShowJumpInfo = g_Config.m_RiEdgeInfoJump != 0 || (ForcePreview && !g_Config.m_RiEdgeInfoCords);
	CUIRect EdgeInfo, JumpInfo;

	if(Layout.m_BackgroundEnabled)
	{
		const int Corners = HudLayout::BackgroundCorners(IGraphics::CORNER_ALL, Base.x, Base.y, Base.w, Base.h, HudWidth, HudHeight);
		Base.Draw(color_cast<ColorRGBA>(ColorHSLA(Layout.m_BackgroundColor, true)), Corners, SEdgeHelperProperties::ms_Rounding * Scale);
	}
	Base.Margin(SEdgeHelperProperties::ms_Padding * Scale, &Base);

	if(ForcePreview)
		m_PosX = 53;
	else
	{
		const int ClientId = GameClient()->m_Snap.m_SpecInfo.m_Active ? GameClient()->m_Snap.m_SpecInfo.m_SpectatorId : GameClient()->m_Snap.m_LocalClientId;
		m_PosX = GetPositionEdgeHelper(ClientId, g_Config.m_ClDummy);
	}

	if(ShowEdgeInfo && ShowJumpInfo)
		Base.HSplitMid(&EdgeInfo, &JumpInfo);
	if(ShowEdgeInfo)
		RenderEdgeHelperEdgeInfo(ShowJumpInfo ? &EdgeInfo : &Base, Scale);
	if(ShowJumpInfo)
		RenderEdgeHelperJumpInfo(ShowEdgeInfo ? &JumpInfo : &Base, Scale);
}

int CEdgeHelper::GetPositionEdgeHelper(int ClientId, int Conn) const
{
	float Position;
	if(ClientId == SPEC_FREEVIEW)
	{
		Position = GameClient()->m_Camera.m_Center.x / 32.0f;
	}
	else if(ClientId >= 0 && ClientId < MAX_CLIENTS && GameClient()->m_aClients[ClientId].m_SpecCharPresent)
	{
		Position = GameClient()->m_aClients[ClientId].m_SpecChar.x / 32.0f;
	}
	else if(ClientId >= 0 && ClientId < MAX_CLIENTS && GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
	{
		const CNetObj_Character *pPrevChar = &GameClient()->m_Snap.m_aCharacters[ClientId].m_Prev;
		const CNetObj_Character *pCurChar = &GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur;
		const float IntraTick = Client()->IntraGameTick(Conn);
		Position = mix(vec2(pPrevChar->m_X, pPrevChar->m_Y), vec2(pCurChar->m_X, pCurChar->m_Y), IntraTick).x / 32.0f;
	}
	else
	{
		return 0;
	}

	const int PositionHundredths = static_cast<int>(std::round(Position * 100.0f));
	return ((PositionHundredths % 100) + 100) % 100;
}

void CEdgeHelper::RenderEdgeHelperEdgeInfo(CUIRect *pBase, float Scale)
{
	CUIRect LeftZone, RightZone, CenterZone;
	pBase->HSplitBottom(SEdgeHelperProperties::ms_ItemSpacing * Scale, pBase, nullptr);
	const float ActionSpacing = (pBase->w - 2.0f * (SEdgeHelperProperties::ms_WallWidth + SEdgeHelperProperties::ms_CircleRadius + SEdgeHelperProperties::ms_CubeSize) * Scale) / 4.0f;
	pBase->VSplitLeft(SEdgeHelperProperties::ms_CubeSize * Scale + ActionSpacing, &LeftZone, &CenterZone);
	CenterZone.VSplitRight(SEdgeHelperProperties::ms_CubeSize * Scale + ActionSpacing, &CenterZone, &RightZone);
	LeftZone.VSplitRight(ActionSpacing + 2.0f * Scale, &LeftZone, nullptr);
	RightZone.VSplitLeft(ActionSpacing + 2.0f * Scale, nullptr, &RightZone);
	LeftZone.Margin(SEdgeHelperProperties::ms_ItemSpacing * Scale, &LeftZone);
	RightZone.Margin(SEdgeHelperProperties::ms_ItemSpacing * Scale, &RightZone);
	LeftZone.Draw(m_PosX >= 44 ? color_cast<ColorRGBA>(ColorHSLA(g_Config.m_RiEdgeInfoColorFreeze)) : m_PosX >= 28 ? color_cast<ColorRGBA>(ColorHSLA(g_Config.m_RiEdgeInfoColorSafe)) : color_cast<ColorRGBA>(ColorHSLA(g_Config.m_RiEdgeInfoColorKill)), IGraphics::CORNER_ALL, SEdgeHelperProperties::ms_Rounding * Scale);
	RightZone.Draw(m_PosX <= 53 ? color_cast<ColorRGBA>(ColorHSLA(g_Config.m_RiEdgeInfoColorFreeze)) : m_PosX <= 69 ? color_cast<ColorRGBA>(ColorHSLA(g_Config.m_RiEdgeInfoColorSafe)) : color_cast<ColorRGBA>(ColorHSLA(g_Config.m_RiEdgeInfoColorKill)), IGraphics::CORNER_ALL, SEdgeHelperProperties::ms_Rounding * Scale);
	CenterZone.VSplitLeft(SEdgeHelperProperties::ms_WallWidth * Scale + ActionSpacing, &LeftZone, &CenterZone);
	CenterZone.VSplitRight(SEdgeHelperProperties::ms_WallWidth * Scale + ActionSpacing, &CenterZone, &RightZone);
	LeftZone.VSplitRight(ActionSpacing - 3.0f * Scale, &LeftZone, nullptr);
	LeftZone.VSplitLeft(3.0f * Scale, nullptr, &LeftZone);
	RightZone.VSplitLeft(ActionSpacing - 3.0f * Scale, nullptr, &RightZone);
	RightZone.VSplitRight(3.0f * Scale, &RightZone, nullptr);
	LeftZone.Draw(m_PosX >= 44 && m_PosX < 53 ? SEdgeHelperProperties::ActionWhiteButtonColor() : SEdgeHelperProperties::WindowColorMedium(), IGraphics::CORNER_NONE, 0.0f);
	RightZone.Draw(m_PosX <= 53 && m_PosX > 44 ? SEdgeHelperProperties::ActionWhiteButtonColor() : SEdgeHelperProperties::WindowColorMedium(), IGraphics::CORNER_NONE, 0.0f);
	CenterZone.Margin(SEdgeHelperProperties::ms_ItemSpacing * Scale, &CenterZone);

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(m_PosX > 44 && m_PosX < 53 ? SEdgeHelperProperties::ActionWhiteButtonColor() : SEdgeHelperProperties::WindowColorMedium());
	Graphics()->DrawCircle(CenterZone.x + CenterZone.w / 2.0f, CenterZone.y + CenterZone.h / 2.0f, SEdgeHelperProperties::ms_CircleRadius * Scale, 16);
	Graphics()->QuadsEnd();

	if(m_PosX != 44 && m_PosX != 53)
		return;

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(SEdgeHelperProperties::ActionWhiteButtonColor());
	constexpr int Segments = 16;
	const float CenterX = CenterZone.x + CenterZone.w / 2.0f;
	const float CenterY = CenterZone.y + CenterZone.h / 2.0f;
	for(int i = 0; i < Segments; ++i)
	{
		const float Angle1 = 2.0f * pi * i / Segments;
		const float Angle2 = 2.0f * pi * (i + 1) / Segments;
		IGraphics::CFreeformItem Quad(
			CenterX + std::cos(Angle1) * (SEdgeHelperProperties::ms_CircleRadius - SEdgeHelperProperties::ms_CircleThickness / 2.0f) * Scale, CenterY + std::sin(Angle1) * (SEdgeHelperProperties::ms_CircleRadius - SEdgeHelperProperties::ms_CircleThickness / 2.0f) * Scale,
			CenterX + std::cos(Angle2) * (SEdgeHelperProperties::ms_CircleRadius - SEdgeHelperProperties::ms_CircleThickness / 2.0f) * Scale, CenterY + std::sin(Angle2) * (SEdgeHelperProperties::ms_CircleRadius - SEdgeHelperProperties::ms_CircleThickness / 2.0f) * Scale,
			CenterX + std::cos(Angle1) * (SEdgeHelperProperties::ms_CircleRadius + SEdgeHelperProperties::ms_CircleThickness / 2.0f) * Scale, CenterY + std::sin(Angle1) * (SEdgeHelperProperties::ms_CircleRadius + SEdgeHelperProperties::ms_CircleThickness / 2.0f) * Scale,
			CenterX + std::cos(Angle2) * (SEdgeHelperProperties::ms_CircleRadius + SEdgeHelperProperties::ms_CircleThickness / 2.0f) * Scale, CenterY + std::sin(Angle2) * (SEdgeHelperProperties::ms_CircleRadius + SEdgeHelperProperties::ms_CircleThickness / 2.0f) * Scale);
		Graphics()->QuadsDrawFreeform(&Quad, 1);
	}
	Graphics()->QuadsEnd();
}

void CEdgeHelper::RenderEdgeHelperJumpInfo(CUIRect *pBase, float Scale)
{
	CUIRect LeftZone, RightZone, CenterZone;
	pBase->HSplitTop(SEdgeHelperProperties::ms_ItemSpacing * Scale, nullptr, pBase);
	const float JumpScale = minimum(Scale, pBase->w / (5.0f * SEdgeHelperProperties::ms_ArrowsSize));
	const float ActionSpacing = maximum(0.0f, (pBase->w - 5.0f * SEdgeHelperProperties::ms_ArrowsSize * JumpScale) / 4.0f);
	pBase->VSplitLeft(SEdgeHelperProperties::ms_ArrowsSize * JumpScale + ActionSpacing, &LeftZone, &CenterZone);
	CenterZone.VSplitRight(SEdgeHelperProperties::ms_ArrowsSize * JumpScale + ActionSpacing, &CenterZone, &RightZone);
	LeftZone.VSplitRight(ActionSpacing, &LeftZone, nullptr);
	RightZone.VSplitLeft(ActionSpacing, nullptr, &RightZone);
	LeftZone.Margin(SEdgeHelperProperties::ms_ItemSpacing * JumpScale, &LeftZone);
	RightZone.Margin(SEdgeHelperProperties::ms_ItemSpacing * JumpScale, &RightZone);
	const float ArrowFontSize = minimum(SEdgeHelperProperties::ms_ArrowsSize * JumpScale, minimum(LeftZone.h, RightZone.h));
	const float SingleArrowFontSize = ArrowFontSize * 0.75f;
	DoIconButton(&RightZone, s_pEdgeInfoAnglesUp, ArrowFontSize, (m_PosX == 56 || m_PosX == 69 || m_PosX == 72 || m_PosX == 84) ? SEdgeHelperProperties::ActionWhiteButtonColor() : SEdgeHelperProperties::WindowColorMedium());
	if(m_PosX == 62 || m_PosX == 63 || m_PosX == 66 || m_PosX == 81)
	{
		DoIconButton(&RightZone, s_pEdgeInfoAngleUp, SingleArrowFontSize, SEdgeHelperProperties::ActionWhiteButtonColor());
	}
	DoIconButton(&LeftZone, s_pEdgeInfoAnglesUp, ArrowFontSize, (m_PosX == 13 || m_PosX == 25 || m_PosX == 28 || m_PosX == 41) ? SEdgeHelperProperties::ActionWhiteButtonColor() : SEdgeHelperProperties::WindowColorMedium());
	if(m_PosX == 16 || m_PosX == 31)
	{
		DoIconButton(&LeftZone, s_pEdgeInfoAngleUp, SingleArrowFontSize, SEdgeHelperProperties::ActionWhiteButtonColor());
	}

	const float SeparatorWidth = minimum(4.0f * JumpScale, CenterZone.w * 0.1f);
	const float CenterValueWidth = minimum(24.0f * JumpScale, maximum(0.0f, CenterZone.w - SeparatorWidth * 2.0f));
	const float SideValueWidth = maximum(0.0f, (CenterZone.w - CenterValueWidth - SeparatorWidth * 2.0f) * 0.5f);
	CUIRect LeftSeparator, RightSeparator;
	CenterZone.VSplitLeft(SideValueWidth, &LeftZone, &CenterZone);
	CenterZone.VSplitLeft(SeparatorWidth, &LeftSeparator, &CenterZone);
	CenterZone.VSplitLeft(CenterValueWidth, &CenterZone, &RightZone);
	RightZone.VSplitLeft(SeparatorWidth, &RightSeparator, &RightZone);

	const auto LowerIt = std::upper_bound(s_aEdgeInfoJumpPositions.begin(), s_aEdgeInfoJumpPositions.end(), m_PosX);
	const auto UpperIt = std::lower_bound(s_aEdgeInfoJumpPositions.begin(), s_aEdgeInfoJumpPositions.end(), m_PosX);
	const int Lower = LowerIt == s_aEdgeInfoJumpPositions.begin() ? std::numeric_limits<int>::min() : *std::prev(LowerIt);
	const int Upper = UpperIt == s_aEdgeInfoJumpPositions.end() ? std::numeric_limits<int>::max() : *UpperIt;
	const float ValueFontSize = 12.0f * JumpScale;

	if(m_PosX == Lower || m_PosX == Upper)
		TextRender()->TextColor(SEdgeHelperProperties::ActionActiveButtonColor());
	Ui()->DoLabel(&LeftSeparator, "|", ValueFontSize, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	if(m_PosX == Upper)
		TextRender()->TextColor(SEdgeHelperProperties::ActionActiveButtonColor());
	Ui()->DoLabel(&RightSeparator, "|", ValueFontSize, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), "%02i", m_PosX);
	if(m_PosX == Lower)
		TextRender()->TextColor(SEdgeHelperProperties::ActionActiveButtonColor());
	Ui()->DoLabel(&CenterZone, aBuf, ValueFontSize, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	if(Lower == std::numeric_limits<int>::min())
		str_copy(aBuf, "-");
	else
		str_format(aBuf, sizeof(aBuf), "%d", Lower);
	if(m_PosX == Lower || m_PosX == Upper)
		TextRender()->TextColor(SEdgeHelperProperties::ActionActiveButtonColor());
	Ui()->DoLabel(&LeftZone, aBuf, ValueFontSize, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	if(Upper == std::numeric_limits<int>::max())
		str_copy(aBuf, "-");
	else
		str_format(aBuf, sizeof(aBuf), "%d", Upper);
	if(m_PosX == Upper)
		TextRender()->TextColor(SEdgeHelperProperties::ActionActiveButtonColor());
	Ui()->DoLabel(&RightZone, aBuf, ValueFontSize, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

void CEdgeHelper::DoIconButton(CUIRect *pRect, const char *pIcon, float TextSize, ColorRGBA IconColor) const
{
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	TextRender()->TextColor(IconColor);
	Ui()->DoLabel(pRect, pIcon, TextSize, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
}
