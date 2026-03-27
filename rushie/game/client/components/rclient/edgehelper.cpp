#include "edgehelper.h"

#include "engine/font_icons.h"
#include "game/localization.h"

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/client/components/camera.h>
#include <game/client/components/chat.h>
#include <game/client/components/console.h>
#include <game/client/components/controls.h>
#include <game/client/components/emoticon.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/ui.h>
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

	static constexpr float ms_HeadlineFontSize = 8.0f;

	static ColorRGBA WindowColor() { return ColorRGBA(0.451f, 0.451f, 0.451f, 0.9f); };
	static ColorRGBA WindowColorDark() { return ColorRGBA(0.2f, 0.2f, 0.2f, 0.9f); };
	static ColorRGBA WindowColorMedium() { return ColorRGBA(0.35f, 0.35f, 0.35f, 0.9f); };

	static ColorRGBA ActionActiveButtonColor() { return ColorRGBA(0.53f, 0.78f, 0.53f, 0.8f); };
	static ColorRGBA ActionAltActiveButtonColor() { return ColorRGBA(1.0f, 0.42f, 0.42f, 0.8f); };
	static ColorRGBA BlueSteelButtonColor() { return ColorRGBA(0.2f, 0.4f, 0.65f, 0.8f); };
	static ColorRGBA ActionWhiteButtonColor() { return ColorRGBA(1.0f, 1.0f, 1.0f, 0.8f); };
};

CEdgeHelper::CEdgeHelper()
{
	OnReset();
}

void CEdgeHelper::OnConsoleInit()
{
	Console()->Register("ri_toggle_edgeinfo", "", CFGFLAG_CLIENT, ConToggleEdgeHelper, this, "Toggle edge info");
}

void CEdgeHelper::ConToggleEdgeHelper(IConsole::IResult *pResult, void *pUserData)
{
	CEdgeHelper *pSelf = (CEdgeHelper *)pUserData;
	pSelf->SetActive(!pSelf->IsActive());

}

void CEdgeHelper::SetActive(bool Active)
{
	if(m_Active == Active)
		return;

	m_Active = Active;
	if(m_Active)
	{
		if(!g_Config.m_RiEdgeInfoJump && !g_Config.m_RiEdgeInfoCords)
		{
			GameClient()->Echo("Enable any edgeinfo function");
			OnReset();
		}
	}
	else
	{
		OnReset();
	}
}

void CEdgeHelper::OnReset()
{
	RIReset();
	SetActive(false);
}

void CEdgeHelper::OnRelease()
{
	RIReset();
	SetActive(false);
}

void CEdgeHelper::OnRender()
{
	if(!IsActive())
		return;

	RenderEdgeHelper();
}

void CEdgeHelper::RenderEdgeHelper()
{
	CUIRect Base, EdgeInfo, JumpInfo;

	Base.h = 100.0f * 3.0f / (g_Config.m_RiEdgeInfoJump && g_Config.m_RiEdgeInfoCords ? 6 : 12);
	Base.w = 100.0f * 3.0f * Graphics()->ScreenAspect() / 5;
	Base.x = (100.0f * 3.0f * Graphics()->ScreenAspect() / 2 - Base.w / 2) * (g_Config.m_RiEdgeInfoPosX / 50.0f);
	Base.y = ((100.0f * 3.0f) / 2) * (g_Config.m_RiEdgeInfoPosY / 50.0f);

	vec2 ScreenTL, ScreenBR;
	Graphics()->GetScreen(&ScreenTL.x, &ScreenTL.y, &ScreenBR.x, &ScreenBR.y);

	if(Base.y + Base.h > ScreenBR.y)
	{
		Base.y -= Base.y + Base.h - ScreenBR.y;
	}
	if(Base.x + Base.w > ScreenBR.x)
	{
		Base.x -= Base.x + Base.w - ScreenBR.x;
	}

	m_Rect = Base;

	Base.Draw(SEdgeHelperProperties::WindowColorDark(), IGraphics::CORNER_ALL, SEdgeHelperProperties::ms_Rounding);
	Base.Margin(SEdgeHelperProperties::ms_Padding, &Base);
	const int ClientId = GameClient()->m_Snap.m_SpecInfo.m_Active ? GameClient()->m_Snap.m_SpecInfo.m_SpectatorId : GameClient()->m_Snap.m_LocalClientId;
	m_Pos_x = GetPositionEdgeHelper(ClientId, g_Config.m_ClDummy);

	if(g_Config.m_RiEdgeInfoCords && g_Config.m_RiEdgeInfoJump)
		Base.HSplitMid(&EdgeInfo, &JumpInfo);
	if(g_Config.m_RiEdgeInfoCords)
		RenderEdgeHelperEdgeInfo(g_Config.m_RiEdgeInfoJump ? &EdgeInfo : &Base);
	if(g_Config.m_RiEdgeInfoJump)
		RenderEdgeHelperJumpInfo(g_Config.m_RiEdgeInfoCords ? &JumpInfo : &Base);
}

float CEdgeHelper::GetPositionEdgeHelper(int ClientId, int Conn)
{
	float ValuePos;
	if(ClientId == SPEC_FREEVIEW)
	{
		ValuePos = GameClient()->m_Camera.m_Center.x / 32.0f;
	}
	else if(GameClient()->m_aClients[ClientId].m_SpecCharPresent)
	{
		ValuePos = GameClient()->m_aClients[ClientId].m_SpecChar.x / 32.0f;
	}
	else
	{
		const CNetObj_Character *pPrevChar = &GameClient()->m_Snap.m_aCharacters[ClientId].m_Prev;
		const CNetObj_Character *pCurChar = &GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur;
		const float IntraTick = Client()->IntraGameTick(Conn);
		ValuePos = mix(vec2(pPrevChar->m_X, pPrevChar->m_Y), vec2(pCurChar->m_X, pCurChar->m_Y), IntraTick).x / 32.0f;
	}

	ValuePos = std::round(ValuePos * 100.0f) / 100.0f;
	float temp = std::round(ValuePos * 100.0f); // temp = 3263.0f
	int result = static_cast<int>(temp) % 100; // result = 3263 % 100 = 63
	return result;
}

void CEdgeHelper::RenderEdgeHelperEdgeInfo(CUIRect *pBase)
{
	CUIRect LeftZone, RightZone, CenterZone;
	pBase->HSplitBottom(SEdgeHelperProperties::ms_ItemSpacing, pBase, nullptr);
	float ActionSpacing = (pBase->w - (2 * (SEdgeHelperProperties::ms_WallWidth + SEdgeHelperProperties::ms_CircleRadius + SEdgeHelperProperties::ms_CubeSize))) / 4;
	pBase->VSplitLeft(SEdgeHelperProperties::ms_CubeSize + ActionSpacing, &LeftZone, &CenterZone);
	CenterZone.VSplitRight(SEdgeHelperProperties::ms_CubeSize + ActionSpacing, &CenterZone, &RightZone);
	LeftZone.VSplitRight(ActionSpacing + 2, &LeftZone, nullptr);
	RightZone.VSplitLeft(ActionSpacing + 2, nullptr, &RightZone);
	LeftZone.Margin(SEdgeHelperProperties::ms_ItemSpacing, &LeftZone);
	RightZone.Margin(SEdgeHelperProperties::ms_ItemSpacing, &RightZone);
	LeftZone.Draw(m_Pos_x >= 44 ? color_cast<ColorRGBA>(ColorHSLA(g_Config.m_RiEdgeInfoColorFreeze)) : m_Pos_x >= 28 ? color_cast<ColorRGBA>(ColorHSLA(g_Config.m_RiEdgeInfoColorSafe)) : color_cast<ColorRGBA>(ColorHSLA(g_Config.m_RiEdgeInfoColorKill)), IGraphics::CORNER_ALL, SEdgeHelperProperties::ms_Rounding);
	RightZone.Draw(m_Pos_x <= 53 ? color_cast<ColorRGBA>(ColorHSLA(g_Config.m_RiEdgeInfoColorFreeze)) : m_Pos_x <= 69 ? color_cast<ColorRGBA>(ColorHSLA(g_Config.m_RiEdgeInfoColorSafe)) : color_cast<ColorRGBA>(ColorHSLA(g_Config.m_RiEdgeInfoColorKill)), IGraphics::CORNER_ALL, SEdgeHelperProperties::ms_Rounding);
	CenterZone.VSplitLeft(SEdgeHelperProperties::ms_WallWidth + ActionSpacing, &LeftZone, &CenterZone);
	CenterZone.VSplitRight(SEdgeHelperProperties::ms_WallWidth + ActionSpacing, &CenterZone, &RightZone);
	LeftZone.VSplitRight(ActionSpacing - 3, &LeftZone, nullptr);
	LeftZone.VSplitLeft(3, nullptr, &LeftZone);
	RightZone.VSplitLeft(ActionSpacing - 3, nullptr, &RightZone);
	RightZone.VSplitRight(3, &RightZone, nullptr);
	LeftZone.Draw(m_Pos_x >= 44 && m_Pos_x < 53 ? SEdgeHelperProperties::ActionWhiteButtonColor() : SEdgeHelperProperties::WindowColorMedium(), IGraphics::CORNER_NONE, 0);
	RightZone.Draw(m_Pos_x <= 53 && m_Pos_x > 44 ? SEdgeHelperProperties::ActionWhiteButtonColor() : SEdgeHelperProperties::WindowColorMedium(), IGraphics::CORNER_NONE, 0);
	CenterZone.Margin(SEdgeHelperProperties::ms_ItemSpacing, &CenterZone);
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(m_Pos_x > 44 && m_Pos_x < 53 ? SEdgeHelperProperties::ActionWhiteButtonColor() : SEdgeHelperProperties::WindowColorMedium());
	Graphics()->DrawCircle(CenterZone.x + CenterZone.w / 2, CenterZone.y + CenterZone.h / 2, SEdgeHelperProperties::ms_CircleRadius, 16);
	Graphics()->QuadsEnd();
	//
	if(m_Pos_x == 44 || m_Pos_x == 53)
	{
		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(SEdgeHelperProperties::ActionWhiteButtonColor());

		const int Segments = 16;
		const float CenterX = CenterZone.x + CenterZone.w / 2.0f;
		const float CenterY = CenterZone.y + CenterZone.h / 2.0f;
		const float Radius = SEdgeHelperProperties::ms_CircleRadius;
		const float Thickness = SEdgeHelperProperties::ms_CircleThickness;

		for(int i = 0; i < Segments; ++i)
		{
			float a1 = 2.0f * pi * i / Segments;
			float a2 = 2.0f * pi * (i + 1) / Segments;
			IGraphics::CFreeformItem Quad(
				CenterX + std::cos(a1) * (Radius - Thickness / 2.0f), CenterY + std::sin(a1) * (Radius - Thickness / 2.0f),
				CenterX + std::cos(a2) * (Radius - Thickness / 2.0f), CenterY + std::sin(a2) * (Radius - Thickness / 2.0f),
				CenterX + std::cos(a1) * (Radius + Thickness / 2.0f), CenterY + std::sin(a1) * (Radius + Thickness / 2.0f),
				CenterX + std::cos(a2) * (Radius + Thickness / 2.0f), CenterY + std::sin(a2) * (Radius + Thickness / 2.0f));
			Graphics()->QuadsDrawFreeform(&Quad, 1);
		}
		Graphics()->QuadsEnd();
	}
}

void CEdgeHelper::RenderEdgeHelperJumpInfo(CUIRect *pBase)
{
	CUIRect LeftZone, RightZone, CenterZone;
	pBase->HSplitTop(SEdgeHelperProperties::ms_ItemSpacing, nullptr, pBase);
	float ActionSpacing = (pBase->w - (2 * SEdgeHelperProperties::ms_ArrowsSize + 3 * SEdgeHelperProperties::ms_ArrowsSize)) / 4;
	pBase->VSplitLeft(SEdgeHelperProperties::ms_ArrowsSize + ActionSpacing, &LeftZone, &CenterZone);
	CenterZone.VSplitRight(SEdgeHelperProperties::ms_ArrowsSize + ActionSpacing, &CenterZone, &RightZone);
	LeftZone.VSplitRight(ActionSpacing, &LeftZone, nullptr);
	RightZone.VSplitLeft(ActionSpacing, nullptr, &RightZone);
	LeftZone.Margin(SEdgeHelperProperties::ms_ItemSpacing, &LeftZone);
	RightZone.Margin(SEdgeHelperProperties::ms_ItemSpacing, &RightZone);
	DoIconButton(&RightZone, FontIcon::RC_ANGLES_UP, SEdgeHelperProperties::ms_ArrowsSize, (m_Pos_x == 56 || m_Pos_x == 69 || m_Pos_x == 72 || m_Pos_x == 84) ? SEdgeHelperProperties::ActionWhiteButtonColor() : SEdgeHelperProperties::WindowColorMedium());
	if(m_Pos_x == 62 || m_Pos_x == 63 || m_Pos_x == 66 || m_Pos_x == 81)
	{
		RightZone.HSplitTop(5, nullptr, &RightZone);
		DoIconButton(&RightZone, FontIcon::RC_ANGLE_UP, SEdgeHelperProperties::ms_ArrowsSize, SEdgeHelperProperties::ActionWhiteButtonColor());
	}
	DoIconButton(&LeftZone, FontIcon::RC_ANGLES_UP, SEdgeHelperProperties::ms_ArrowsSize, (m_Pos_x == 13 || m_Pos_x == 25 || m_Pos_x == 28 || m_Pos_x == 41) ? SEdgeHelperProperties::ActionWhiteButtonColor() : SEdgeHelperProperties::WindowColorMedium());
	if(m_Pos_x == 16 || m_Pos_x == 31)
	{
		LeftZone.HSplitTop(5, nullptr, &LeftZone);
		DoIconButton(&LeftZone, FontIcon::RC_ANGLE_UP, SEdgeHelperProperties::ms_ArrowsSize, SEdgeHelperProperties::ActionWhiteButtonColor());
	}
	CenterZone.VSplitLeft(SEdgeHelperProperties::ms_ArrowsSize + ActionSpacing, &LeftZone, &CenterZone);
	CenterZone.VSplitRight(SEdgeHelperProperties::ms_ArrowsSize + ActionSpacing, &CenterZone, &RightZone);
	LeftZone.VSplitRight(ActionSpacing - 3, &LeftZone, nullptr);
	LeftZone.VSplitLeft(3, nullptr, &LeftZone);
	RightZone.VSplitLeft(ActionSpacing - 3, nullptr, &RightZone);
	RightZone.VSplitRight(3, &RightZone, nullptr);
	std::sort(values.begin(), values.end());

	int lower = std::numeric_limits<int>::min();
	int upper = std::numeric_limits<int>::max();

	for (int v : values)
	{
		if (v <= m_Pos_x) lower = v;
		if (v >= m_Pos_x)
		{
			upper = v;
			break;
		}
	}

	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), "%02i", m_Pos_x);
	if(m_Pos_x == lower)
		TextRender()->TextColor(SEdgeHelperProperties::ActionActiveButtonColor());
	Ui()->DoLabel(&CenterZone, aBuf, 12, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	str_format(aBuf, sizeof(aBuf), "%s |", (lower == std::numeric_limits<int>::min()) ? "-" : std::to_string(lower).c_str());
	if(m_Pos_x == lower || m_Pos_x == upper)
		TextRender()->TextColor(SEdgeHelperProperties::ActionActiveButtonColor());
	Ui()->DoLabel(&LeftZone, aBuf, 12, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	str_format(aBuf, sizeof(aBuf), "| %s", (upper == std::numeric_limits<int>::max()) ? "-" : std::to_string(upper).c_str());
	if(m_Pos_x == upper)
		TextRender()->TextColor(SEdgeHelperProperties::ActionActiveButtonColor());
	Ui()->DoLabel(&RightZone, aBuf, 12, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

bool CEdgeHelper::IsActive() const
{
	if(m_Active)
		return true;

	return false;
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
