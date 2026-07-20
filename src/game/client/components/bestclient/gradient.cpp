/* Copyright © 2026 BestProject Team */
#include "gradient.h"

#include <base/math.h>

#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/client/ui.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

void (*BCGradient_ApplyEverythingHook)(CTextCursor *pCursor, const char *pText, int Length) = nullptr;

namespace
{
static CBcGradient *s_pActiveGradient = nullptr;
static CGameClient *s_pGameClient = nullptr;

static ColorRGBA ConfigColor(unsigned ConfigValue)
{
	return color_cast<ColorRGBA>(ColorHSLA(ConfigValue, true));
}

static void CollectCustomColors(std::vector<ColorRGBA> &vColors)
{
	vColors.clear();
	const int Count = std::clamp(g_Config.m_BcNameplateGradientColorCount, 2, 4);
	vColors.push_back(ConfigColor(g_Config.m_BcNameplateGradientColor1));
	vColors.push_back(ConfigColor(g_Config.m_BcNameplateGradientColor2));
	if(Count >= 3)
		vColors.push_back(ConfigColor(g_Config.m_BcNameplateGradientColor3));
	if(Count >= 4)
		vColors.push_back(ConfigColor(g_Config.m_BcNameplateGradientColor4));
}

static ColorRGBA SampleColorStops(const std::vector<ColorRGBA> &vColors, float Position)
{
	if(vColors.empty())
		return ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	if(vColors.size() == 1)
		return vColors[0];

	// Loop seamlessly: last stop blends back into the first (1→2→…→N→1).
	float Wrapped = std::fmod(Position, 1.0f);
	if(Wrapped < 0.0f)
		Wrapped += 1.0f;

	const int Count = (int)vColors.size();
	const float Scaled = Wrapped * Count;
	const int Index = (int)Scaled % Count;
	const float LocalT = Scaled - std::floor(Scaled);
	const ColorRGBA &A = vColors[Index];
	const ColorRGBA &B = vColors[(Index + 1) % Count];
	return ColorRGBA(
		A.r + LocalT * (B.r - A.r),
		A.g + LocalT * (B.g - A.g),
		A.b + LocalT * (B.b - A.b),
		1.0f);
}

static std::vector<STextColorSplit> BuildMultiColorSplits(const char *pText, const std::function<ColorRGBA(float)> &SampleColor)
{
	std::vector<STextColorSplit> vSplits;
	size_t Size, Count;
	str_utf8_stats(pText, str_length(pText) + 1, SIZE_MAX, &Size, &Count);
	if(Count == 0)
		return vSplits;

	vSplits.reserve(Count);
	const char *pStr = pText;
	for(size_t i = 0; i < Count; i++)
	{
		int ByteOffset = (int)(pStr - pText);
		const char *pPrev = pStr;
		str_utf8_decode(&pStr);
		int ByteLen = (int)(pStr - pPrev);
		const float t = Count == 1 ? 0.5f : (float)i / (float)(Count - 1);
		vSplits.emplace_back(ByteOffset, ByteLen, SampleColor(t));
	}
	return vSplits;
}
} // namespace

float CBcGradient::AnimatePhase(double GlobalTime)
{
	return (float)std::fmod(GlobalTime * (g_Config.m_BcNameplateGradientAnimateSpeed / 100.0), 1.0);
}

ColorRGBA CBcGradient::SampleRainbow(float Hue)
{
	return color_cast<ColorRGBA>(ColorHSLA(std::fmod(Hue, 1.0f), 1.0f, 0.5f));
}

ColorRGBA CBcGradient::SampleCustomGradient(float Position)
{
	std::vector<ColorRGBA> vColors;
	CollectCustomColors(vColors);
	return SampleColorStops(vColors, Position);
}

void CBcGradient::GetSkinToneColors(int ClientId, const CGameClient *pGameClient, ColorRGBA &Body, ColorRGBA &Feet)
{
	Body = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	Feet = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	const CBcGradient *pGradient = &pGameClient->m_BcGradient;
	if(g_Config.m_BcNameplateGradientSkin && pGradient->m_aHasOriginal[ClientId])
	{
		Body = pGradient->m_aOriginalBody[ClientId];
		Feet = pGradient->m_aOriginalFeet[ClientId];
		return;
	}

	const auto &RenderInfo = pGameClient->m_aClients[ClientId].m_RenderInfo;
	if(RenderInfo.m_CustomColoredSkin)
	{
		Body = RenderInfo.m_ColorBody;
		Feet = RenderInfo.m_ColorFeet;
	}
	else
	{
		Body = RenderInfo.m_BloodColor;
		Feet = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	}
}

void CBcGradient::GetAnimatedEndpointColors(int ClientId, const CGameClient *pGameClient, float Phase, ColorRGBA &Color1, ColorRGBA &Color2)
{
	switch(g_Config.m_BcNameplateGradientMode)
	{
	case BC_GRADIENT_MODE_CUSTOM:
	{
		std::vector<ColorRGBA> vColors;
		CollectCustomColors(vColors);
		Color1 = SampleColorStops(vColors, Phase);
		Color2 = SampleColorStops(vColors, std::fmod(Phase + 0.5f, 1.0f));
		break;
	}
	case BC_GRADIENT_MODE_RAINBOW:
		Color1 = SampleRainbow(Phase);
		Color2 = SampleRainbow(Phase + 0.33f);
		break;
	default:
	{
		ColorRGBA Body, Feet;
		GetSkinToneColors(ClientId, pGameClient, Body, Feet);
		const float Wave = 0.5f + 0.5f * std::cos(2.0f * pi * Phase);
		Color1 = ColorRGBA(
			Body.r + Wave * (Feet.r - Body.r),
			Body.g + Wave * (Feet.g - Body.g),
			Body.b + Wave * (Feet.b - Body.b),
			1.0f);
		Color2 = Feet;
		break;
	}
	}
}

std::vector<STextColorSplit> CBcGradient::BuildStaticColorSplits(const char *pText, const ColorRGBA &Color1, const ColorRGBA &Color2)
{
	return BuildMultiColorSplits(pText, [&](float t) {
		return ColorRGBA(
			Color1.r + t * (Color2.r - Color1.r),
			Color1.g + t * (Color2.g - Color1.g),
			Color1.b + t * (Color2.b - Color1.b),
			1.0f);
	});
}

std::vector<STextColorSplit> CBcGradient::BuildAnimatedColorSplits(const char *pText, const ColorRGBA &Color1, const ColorRGBA &Color2, float Phase)
{
	return BuildMultiColorSplits(pText, [&](float t) {
		const float Wave = 0.5f + 0.5f * std::cos(2.0f * pi * (t - Phase));
		return ColorRGBA(
			Color1.r + Wave * (Color2.r - Color1.r),
			Color1.g + Wave * (Color2.g - Color1.g),
			Color1.b + Wave * (Color2.b - Color1.b),
			1.0f);
	});
}

std::vector<STextColorSplit> CBcGradient::BuildAnimatedTextSplits(const char *pText, int ClientId, CGameClient *pGameClient, float Phase)
{
	switch(g_Config.m_BcNameplateGradientMode)
	{
	case BC_GRADIENT_MODE_CUSTOM:
	{
		std::vector<ColorRGBA> vColors;
		CollectCustomColors(vColors);
		return BuildMultiColorSplits(pText, [&](float t) {
			const float Shifted = std::fmod(t - Phase + 1.0f, 1.0f);
			return SampleColorStops(vColors, Shifted);
		});
	}
	case BC_GRADIENT_MODE_RAINBOW:
		return BuildMultiColorSplits(pText, [&](float t) {
			return SampleRainbow(t * 0.75f + Phase);
		});
	default:
	{
		ColorRGBA Body, Feet;
		GetSkinToneColors(ClientId, pGameClient, Body, Feet);
		return BuildAnimatedColorSplits(pText, Body, Feet, Phase);
	}
	}
}

void CBcGradient::ApplyEverythingGradient(CTextCursor *pCursor, const char *pText, int Length, CGameClient *pGameClient)
{
	if(!g_Config.m_BcNameplateGradientEverything || pCursor == nullptr || pText == nullptr || !pCursor->m_vColorSplits.empty())
		return;

	char aText[512];
	if(Length < 0)
		Length = str_length(pText);
	Length = minimum(Length, (int)sizeof(aText) - 1);
	str_copy(aText, pText, Length + 1);

	const int LocalId = pGameClient->m_Snap.m_LocalClientId;
	const float Phase = AnimatePhase(pGameClient->Client()->GlobalTime());
	// Color splits are matched against absolute m_CharCount, so offset by the
	// characters already written to this cursor (chat builds lines in pieces).
	const int CharOffset = pCursor->m_CharCount;
	const std::vector<STextColorSplit> vSplits = BuildAnimatedTextSplits(aText, LocalId, pGameClient, Phase);
	pCursor->m_vColorSplits.clear();
	pCursor->m_vColorSplits.reserve(vSplits.size());
	for(const STextColorSplit &Split : vSplits)
		pCursor->m_vColorSplits.emplace_back(CharOffset + Split.m_CharIndex, Split.m_Length, Split.m_Color);
}

static void GradientEverythingHook(CTextCursor *pCursor, const char *pText, int Length)
{
	if(s_pGameClient == nullptr)
		return;
	CBcGradient::ApplyEverythingGradient(pCursor, pText, Length, s_pGameClient);
}

void CBcGradient::OnInit()
{
	s_pActiveGradient = this;
	s_pGameClient = GameClient();
	BCGradient_ApplyEverythingHook = GradientEverythingHook;
}

void CBcGradient::OnShutdown()
{
	if(s_pActiveGradient == this)
	{
		s_pActiveGradient = nullptr;
		s_pGameClient = nullptr;
		BCGradient_ApplyEverythingHook = nullptr;
	}
}

void CBcGradient::RefreshCachedText()
{
	// Streamed UI labels (server browser, etc.) bake gradient colors into text
	// containers; reset them so mode/color changes and animation stay in sync.
	Ui()->OnElementsReset();
	GameClient()->m_Chat.RebuildChat();
	GameClient()->m_NamePlates.ResetNamePlates();
	GameClient()->m_Hud.ResetHudContainers();
	GameClient()->m_InfoMessages.OnWindowResize();
	GameClient()->m_Broadcast.OnWindowResize();
}

void CBcGradient::OnRender()
{
	const int Everything = g_Config.m_BcNameplateGradientEverything;
	const int Nick = g_Config.m_BcNameplateGradient;
	const int Clan = g_Config.m_BcNameplateGradientClan;
	const int Mode = g_Config.m_BcNameplateGradientMode;
	const int ColorCount = g_Config.m_BcNameplateGradientColorCount;
	const int AnimateSpeed = g_Config.m_BcNameplateGradientAnimateSpeed;
	const unsigned CfgColor1 = g_Config.m_BcNameplateGradientColor1;
	const unsigned CfgColor2 = g_Config.m_BcNameplateGradientColor2;
	const unsigned CfgColor3 = g_Config.m_BcNameplateGradientColor3;
	const unsigned CfgColor4 = g_Config.m_BcNameplateGradientColor4;

	bool NeedRefresh = false;
	if(m_LastEverything < 0)
	{
		// First frame: just seed tracked values, no rebuild.
	}
	else if(Everything != m_LastEverything || Nick != m_LastNick || Clan != m_LastClan)
	{
		NeedRefresh = true;
	}
	else if(Everything && (Mode != m_LastMode || ColorCount != m_LastColorCount || AnimateSpeed != m_LastAnimateSpeed || CfgColor1 != m_LastColor1 || CfgColor2 != m_LastColor2 || CfgColor3 != m_LastColor3 || CfgColor4 != m_LastColor4))
	{
		NeedRefresh = true;
	}

	m_LastEverything = Everything;
	m_LastNick = Nick;
	m_LastClan = Clan;
	m_LastMode = Mode;
	m_LastColorCount = ColorCount;
	m_LastAnimateSpeed = AnimateSpeed;
	m_LastColor1 = CfgColor1;
	m_LastColor2 = CfgColor2;
	m_LastColor3 = CfgColor3;
	m_LastColor4 = CfgColor4;

	if(NeedRefresh)
		RefreshCachedText();

	std::fill(std::begin(m_aHasOriginal), std::end(m_aHasOriginal), false);

	if(!g_Config.m_BcNameplateGradientSkin)
		return;

	const float Phase = AnimatePhase(Client()->GlobalTime());
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!GameClient()->m_Snap.m_aCharacters[i].m_Active)
			continue;

		GameClient()->m_aClients[i].UpdateRenderInfo();
		const auto &BaseInfo = GameClient()->m_aClients[i].m_RenderInfo;
		if(BaseInfo.m_CustomColoredSkin)
		{
			m_aOriginalBody[i] = BaseInfo.m_ColorBody;
			m_aOriginalFeet[i] = BaseInfo.m_ColorFeet;
		}
		else
		{
			m_aOriginalBody[i] = BaseInfo.m_BloodColor;
			m_aOriginalFeet[i] = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		}
		m_aHasOriginal[i] = true;

		CTeeRenderInfo *pRenderInfo = &GameClient()->m_aClients[i].m_RenderInfo;
		ColorRGBA Endpoint1, Endpoint2;
		GetAnimatedEndpointColors(i, GameClient(), Phase, Endpoint1, Endpoint2);
		pRenderInfo->m_ColorBody = Endpoint1;
		pRenderInfo->m_ColorFeet = Endpoint2;
		pRenderInfo->m_CustomColoredSkin = true;
	}
}
