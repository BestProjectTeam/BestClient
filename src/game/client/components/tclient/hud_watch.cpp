#include "hud_watch.h"

#include <base/color.h>
#include <base/system.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/hud_layout.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>

#include <cstring>

CHudWatch::CHudWatch() :
	m_Dirty(false)
{
}

void CHudWatch::OnInit()
{
	LoadFromConfig();
}

void CHudWatch::LoadFromConfig()
{
	m_vWatches.clear();
	const char *pData = g_Config.m_TcHudWatchData;
	if(!pData || pData[0] == '\0')
		return;

	char aData[2048];
	str_copy(aData, pData);
	char *pEntry = aData;
	while(pEntry && pEntry[0])
	{
		const char *pSemi_const = str_find(pEntry, ";");
		char *pSemi = const_cast<char *>(pSemi_const);
		if(pSemi)
			*pSemi = '\0';

		char *pParts[3] = {};
		int NumParts = 0;
		char *pTok = pEntry;
		for(int i = 0; i < 3; i++)
		{
			const char *pPipe_const = str_find(pTok, "|");
			char *pPipe = const_cast<char *>(pPipe_const);
			if(pPipe)
			{
				*pPipe = '\0';
				pParts[i] = pTok;
				pTok = pPipe + 1;
				NumParts++;
			}
			else
			{
				pParts[i] = pTok;
				NumParts++;
				break;
			}
		}
		if(NumParts >= 1 && pParts[0] && pParts[0][0])
		{
			const char *pVarName = pParts[0];
			const char *pLabel = (NumParts >= 2 && pParts[1]) ? pParts[1] : pParts[0];
			ColorRGBA Color(1.0f, 1.0f, 1.0f, 1.0f);
			std::unordered_map<int, ColorRGBA> ValueColors;
			if(NumParts >= 3 && pParts[2] && pParts[2][0])
			{
				const char *pColorPart = pParts[2];
				const char *pComma = str_find(pColorPart, ",");
				if(pComma)
				{
					char aDefColor[16];
					str_copy(aDefColor, pColorPart, (int)(pComma - pColorPart) + 1);
					unsigned char aDefHex[4] = {};
					if(str_hex_decode(aDefHex, sizeof(aDefHex), aDefColor) == 0)
					{
						Color.r = aDefHex[0] / 255.0f;
						Color.g = aDefHex[1] / 255.0f;
						Color.b = aDefHex[2] / 255.0f;
						Color.a = aDefHex[3] / 255.0f;
					}
					const char *pCond = pComma + 1;
					while(pCond && pCond[0])
					{
						const char *pComma2 = str_find(pCond, ",");
						char aCond[64];
						if(pComma2)
						{
							str_copy(aCond, pCond, (int)(pComma2 - pCond) + 1);
							pCond = pComma2 + 1;
						}
						else
						{
							str_copy(aCond, pCond);
							pCond = nullptr;
						}
						const char *pColon = str_find(aCond, ":");
						if(pColon)
						{
							int Val = str_toint(aCond);
							unsigned char aHex[4] = {};
							if(str_hex_decode(aHex, sizeof(aHex), pColon + 1) == 0)
							{
								ValueColors[Val] = ColorRGBA(
									aHex[0] / 255.0f,
									aHex[1] / 255.0f,
									aHex[2] / 255.0f,
									aHex[3] / 255.0f);
							}
						}
					}
				}
				else
				{
					unsigned char aHex[4] = {};
					if(str_hex_decode(aHex, sizeof(aHex), pColorPart) == 0)
					{
						Color.r = aHex[0] / 255.0f;
						Color.g = aHex[1] / 255.0f;
						Color.b = aHex[2] / 255.0f;
						Color.a = aHex[3] / 255.0f;
					}
				}
			}

			struct SFindData
			{
				const char *m_pName;
				const SConfigVariable *m_pResult;
			};
			SFindData FindData = {pVarName, nullptr};
			auto Finder = [](const SConfigVariable *pVar, void *pUserData) {
				auto *pData = static_cast<SFindData *>(pUserData);
				if(str_comp(pVar->m_pScriptName, pData->m_pName) == 0)
					pData->m_pResult = pVar;
			};
			GameClient()->ConfigManager()->PossibleConfigVariables("", CFGFLAG_CLIENT, Finder, &FindData);
			if(FindData.m_pResult)
			{
				SHudWatchItem Item;
				Item.m_pVar = FindData.m_pResult;
				str_copy(Item.m_aLabel, pLabel);
				Item.m_Color = Color;
				Item.m_vValueColors = std::move(ValueColors);
				m_vWatches.push_back(Item);
			}
		}

		if(pSemi)
			pEntry = pSemi + 1;
		else
			break;
	}
}

void CHudWatch::SaveToConfig()
{
	char aData[2048] = "";
	int Offset = 0;
	for(size_t i = 0; i < m_vWatches.size(); i++)
	{
		if(Offset > 0 && Offset < (int)sizeof(aData) - 2)
		{
			aData[Offset] = ';';
			Offset++;
		}
		const SHudWatchItem &Item = m_vWatches[i];
		char aColorBuf[512];
		int ColorOff = str_format(aColorBuf, sizeof(aColorBuf), "%02X%02X%02X%02X",
			(int)(Item.m_Color.r * 255),
			(int)(Item.m_Color.g * 255),
			(int)(Item.m_Color.b * 255),
			(int)(Item.m_Color.a * 255));
		for(const auto &[Val, Col] : Item.m_vValueColors)
		{
			ColorOff += str_format(aColorBuf + ColorOff, (int)sizeof(aColorBuf) - ColorOff, ",%d:%02X%02X%02X%02X",
				Val,
				(int)(Col.r * 255),
				(int)(Col.g * 255),
				(int)(Col.b * 255),
				(int)(Col.a * 255));
		}
		int Written = str_format(aData + Offset, (int)sizeof(aData) - Offset, "%s|%s|%s",
			Item.m_pVar->m_pScriptName,
			Item.m_aLabel,
			aColorBuf);
		if(Written > 0)
			Offset += Written;
	}
	str_copy(g_Config.m_TcHudWatchData, aData);
	GameClient()->ConfigManager()->Save();
}

void CHudWatch::OnRender()
{
	if(!g_Config.m_TcHudWatchEnable || m_vWatches.empty())
		return;

	float Width = 300.0f * Graphics()->ScreenAspect();
	float Height = 300.0f;
	Graphics()->MapScreen(0.0f, 0.0f, Width, Height);

	const auto Layout = HudLayout::Get(HudLayout::MODULE_HUD_WATCH, Width, Height);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	const float FontSize = 6.0f * Scale;

	float X = Layout.m_X;
	float Y = Layout.m_Y;
	float LineHeight = FontSize + 2.0f * Scale;

	for(const SHudWatchItem &Item : m_vWatches)
	{
		ITextRender *pTextRender = TextRender();

		int CurrentValue = 0;
		char aValue[256] = "";
		if(Item.m_pVar->m_Type == SConfigVariable::VAR_INT)
		{
			const SIntConfigVariable *pInt = static_cast<const SIntConfigVariable *>(Item.m_pVar);
			CurrentValue = *pInt->m_pVariable;
			str_format(aValue, sizeof(aValue), "%d", CurrentValue);
		}
		else if(Item.m_pVar->m_Type == SConfigVariable::VAR_STRING)
		{
			const SStringConfigVariable *pStr = static_cast<const SStringConfigVariable *>(Item.m_pVar);
			str_copy(aValue, pStr->m_pStr);
		}
		else if(Item.m_pVar->m_Type == SConfigVariable::VAR_COLOR)
		{
			const SColorConfigVariable *pCol = static_cast<const SColorConfigVariable *>(Item.m_pVar);
			unsigned ColVal = *pCol->m_pVariable;
			if(pCol->m_Alpha)
				str_format(aValue, sizeof(aValue), "#%08X", ColVal);
			else
				str_format(aValue, sizeof(aValue), "#%06X", ColVal & 0xFFFFFF);
		}

		auto it = Item.m_vValueColors.find(CurrentValue);
		if(!Item.m_vValueColors.empty() && it == Item.m_vValueColors.end())
		{
			Y += LineHeight;
			continue;
		}
		ColorRGBA UseColor = (it != Item.m_vValueColors.end()) ? it->second : Item.m_Color;

		char aBuf[512];
		str_format(aBuf, sizeof(aBuf), "%s: %s", Item.m_aLabel, aValue);

		pTextRender->TextColor(UseColor);
		pTextRender->Text(X, Y, FontSize, aBuf, -1.0f);

		Y += LineHeight;
	}

	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}

CUIRect CHudWatch::GetHudEditorRect(bool ForcePreview) const
{
	if(m_vWatches.empty() && !ForcePreview)
		return {0.0f, 0.0f, 0.0f, 0.0f};

	float Width = 300.0f * Graphics()->ScreenAspect();
	float Height = 300.0f;

	const auto Layout = HudLayout::Get(HudLayout::MODULE_HUD_WATCH, Width, Height);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	const float FontSize = 6.0f * Scale;
	const float LineHeight = FontSize + 2.0f * Scale;

	size_t NumItems = m_vWatches.empty() ? 1 : m_vWatches.size();
	float MaxWidth = 80.0f * Scale;
	for(size_t i = 0; i < m_vWatches.size(); i++)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "%s: 000", m_vWatches[i].m_aLabel);
		float TextWidth = TextRender()->TextWidth(FontSize, aBuf, -1, -1.0f);
		if(TextWidth > MaxWidth)
			MaxWidth = TextWidth;
	}

	CUIRect Rect;
	Rect.x = Layout.m_X;
	Rect.y = Layout.m_Y;
	Rect.w = MaxWidth + 4.0f;
	Rect.h = NumItems * LineHeight + 2.0f;

	Rect.x = std::clamp(Rect.x, 0.0f, std::max(0.0f, Width - Rect.w));
	Rect.y = std::clamp(Rect.y, 0.0f, std::max(0.0f, Height - Rect.h));
	return Rect;
}

bool CHudWatch::IsWatched(const SConfigVariable *pVar) const
{
	for(const auto &Item : m_vWatches)
		if(Item.m_pVar == pVar)
			return true;
	return false;
}

void CHudWatch::SetWatch(const SConfigVariable *pVar, const char *pLabel, ColorRGBA Color, std::unordered_map<int, ColorRGBA> ValueColors)
{
	for(auto &Item : m_vWatches)
	{
		if(Item.m_pVar == pVar)
		{
			str_copy(Item.m_aLabel, pLabel);
			Item.m_Color = Color;
			Item.m_vValueColors = std::move(ValueColors);
			SaveToConfig();
			return;
		}
	}
	SHudWatchItem Item;
	Item.m_pVar = pVar;
	str_copy(Item.m_aLabel, pLabel);
	Item.m_Color = Color;
	Item.m_vValueColors = std::move(ValueColors);
	m_vWatches.push_back(Item);
	SaveToConfig();
}

void CHudWatch::RemoveWatch(const SConfigVariable *pVar)
{
	for(auto it = m_vWatches.begin(); it != m_vWatches.end(); ++it)
	{
		if(it->m_pVar == pVar)
		{
			m_vWatches.erase(it);
			SaveToConfig();
			return;
		}
	}
}
