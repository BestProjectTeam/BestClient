/* Copyright © 2026 BestProject Team */
#include "profile_assets.h"

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/components/menus.h>
#include <game/client/components/sounds.h>
#include <game/client/components/tclient/skinprofiles.h>
#include <game/client/gameclient.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <string>
#include <vector>

enum
{
	BC_PROFILE_ASSET_ENTITIES = 1 << 0,
	BC_PROFILE_ASSET_GAME = 1 << 1,
	BC_PROFILE_ASSET_EMOTICONS = 1 << 2,
	BC_PROFILE_ASSET_PARTICLES = 1 << 3,
	BC_PROFILE_ASSET_HUD = 1 << 4,
	BC_PROFILE_ASSET_EXTRAS = 1 << 5,
	BC_PROFILE_ASSET_CURSOR = 1 << 6,
	BC_PROFILE_ASSET_ARROW = 1 << 7,
	BC_PROFILE_ASSET_AUDIO = 1 << 8,
	BC_PROFILE_ASSET_COUNT = 9,
};

struct SBestClientMultiSelectDropDownState
{
	CUIElement m_UiElement;
	CButtonContainer m_ButtonContainer;
	SPopupMenuId m_PopupId;
	CScrollRegion m_ScrollRegion;
	bool m_Init = false;
	bool m_PopupWasOpen = false;
	int m_Flags = 0;
	char m_aLabel[128] = "";
	std::vector<std::string> m_vLabels;
	std::vector<const char *> m_vpLabelPtrs;
	std::vector<int> m_vBits;
};

void BestClientApplyProfileAssets(CGameClient *pGameClient, const CProfile &Profile)
{
	const int Flags = g_Config.m_BcProfileAssets;
	if((Flags & BC_PROFILE_ASSET_ENTITIES) && Profile.m_AssetEntities[0] != '\0')
	{
		str_copy(g_Config.m_ClAssetsEntities, Profile.m_AssetEntities);
		pGameClient->m_MapImages.ChangeEntitiesPath(Profile.m_AssetEntities);
	}
	if((Flags & BC_PROFILE_ASSET_GAME) && Profile.m_AssetGame[0] != '\0')
	{
		str_copy(g_Config.m_ClAssetGame, Profile.m_AssetGame);
		pGameClient->LoadGameSkin(Profile.m_AssetGame);
	}
	if((Flags & BC_PROFILE_ASSET_EMOTICONS) && Profile.m_AssetEmoticons[0] != '\0')
	{
		str_copy(g_Config.m_ClAssetEmoticons, Profile.m_AssetEmoticons);
		pGameClient->LoadEmoticonsSkin(Profile.m_AssetEmoticons);
	}
	if((Flags & BC_PROFILE_ASSET_PARTICLES) && Profile.m_AssetParticles[0] != '\0')
	{
		str_copy(g_Config.m_ClAssetParticles, Profile.m_AssetParticles);
		pGameClient->LoadParticlesSkin(Profile.m_AssetParticles);
	}
	if((Flags & BC_PROFILE_ASSET_HUD) && Profile.m_AssetHud[0] != '\0')
	{
		str_copy(g_Config.m_ClAssetHud, Profile.m_AssetHud);
		pGameClient->LoadHudSkin(Profile.m_AssetHud);
	}
	if((Flags & BC_PROFILE_ASSET_EXTRAS) && Profile.m_AssetExtras[0] != '\0')
	{
		str_copy(g_Config.m_ClAssetExtras, Profile.m_AssetExtras);
		pGameClient->LoadExtrasSkin(Profile.m_AssetExtras);
	}
	if((Flags & BC_PROFILE_ASSET_CURSOR) && Profile.m_AssetCursor[0] != '\0')
	{
		str_copy(g_Config.m_ClAssetCursor, Profile.m_AssetCursor);
		pGameClient->LoadCursorAsset(Profile.m_AssetCursor);
	}
	if((Flags & BC_PROFILE_ASSET_ARROW) && Profile.m_AssetArrow[0] != '\0')
	{
		str_copy(g_Config.m_ClAssetArrow, Profile.m_AssetArrow);
		pGameClient->LoadArrowAsset(Profile.m_AssetArrow);
	}
	if((Flags & BC_PROFILE_ASSET_AUDIO) && Profile.m_AssetAudio[0] != '\0')
	{
		if(str_comp(g_Config.m_SndPack, Profile.m_AssetAudio) != 0)
		{
			str_copy(g_Config.m_SndPack, Profile.m_AssetAudio);
			pGameClient->m_Sounds.Clear();
		}
	}
}

void BestClientFillProfileAssets(CProfile &Profile)
{
	const int Flags = g_Config.m_BcProfileAssets;
	str_copy(Profile.m_AssetEntities, (Flags & BC_PROFILE_ASSET_ENTITIES) ? g_Config.m_ClAssetsEntities : "");
	str_copy(Profile.m_AssetGame, (Flags & BC_PROFILE_ASSET_GAME) ? g_Config.m_ClAssetGame : "");
	str_copy(Profile.m_AssetEmoticons, (Flags & BC_PROFILE_ASSET_EMOTICONS) ? g_Config.m_ClAssetEmoticons : "");
	str_copy(Profile.m_AssetParticles, (Flags & BC_PROFILE_ASSET_PARTICLES) ? g_Config.m_ClAssetParticles : "");
	str_copy(Profile.m_AssetHud, (Flags & BC_PROFILE_ASSET_HUD) ? g_Config.m_ClAssetHud : "");
	str_copy(Profile.m_AssetExtras, (Flags & BC_PROFILE_ASSET_EXTRAS) ? g_Config.m_ClAssetExtras : "");
	str_copy(Profile.m_AssetCursor, (Flags & BC_PROFILE_ASSET_CURSOR) ? g_Config.m_ClAssetCursor : "");
	str_copy(Profile.m_AssetArrow, (Flags & BC_PROFILE_ASSET_ARROW) ? g_Config.m_ClAssetArrow : "");
	str_copy(Profile.m_AssetAudio, (Flags & BC_PROFILE_ASSET_AUDIO) ? g_Config.m_SndPack : "");
}

struct SMultiSelectPopupContext
{
	CMenus *m_pMenus = nullptr;
	int *m_pFlags = nullptr;
	const char **m_ppLabels = nullptr;
	const int *m_pBits = nullptr;
	int m_Num = 0;
	CScrollRegion *m_pScrollRegion = nullptr;
	std::vector<int> m_vButtonIds;
	float m_EntryHeight = 0.0f;
	float m_EntrySpacing = 0.0f;
	SPopupMenuProperties m_Props;
};

static CUi::EPopupMenuFunctionResult PopupMultiSelect(void *pContext, CUIRect View, bool Active)
{
	(void)Active;
	auto *pPopup = static_cast<SMultiSelectPopupContext *>(pContext);

	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollbarThickness = 10.0f;
	ScrollParams.m_ScrollbarMargin = 4.0f;
	ScrollParams.m_ScrollbarNoOuterMargin = true;
	ScrollParams.m_ScrollUnit = 3 * (pPopup->m_EntryHeight + pPopup->m_EntrySpacing);
	pPopup->m_pScrollRegion->Begin(&View, &ScrollParams);

	pPopup->m_vButtonIds.resize(pPopup->m_Num);
	for(int i = 0; i < pPopup->m_Num; ++i)
	{
		if(i != 0)
			View.HSplitTop(pPopup->m_EntrySpacing, nullptr, &View);
		CUIRect Slot;
		View.HSplitTop(pPopup->m_EntryHeight, &Slot, &View);
		if(!pPopup->m_pScrollRegion->AddRect(Slot))
			continue;

		const int Bit = pPopup->m_pBits[i];
		const int Checked = (*(pPopup->m_pFlags) & Bit) != 0 ? 1 : 0;
		if(pPopup->m_pMenus->DoButton_CheckBox(&pPopup->m_vButtonIds[i], pPopup->m_ppLabels[i], Checked, &Slot))
			*(pPopup->m_pFlags) ^= Bit;
	}

	pPopup->m_pScrollRegion->End();
	return CUi::POPUP_KEEP_OPEN;
}

static int DoMultiSelectDropDown(CUi *pUi, CMenus *pMenus, CUIRect *pRect, int Flags, const char **ppLabels, const int *pBits, int Num, SBestClientMultiSelectDropDownState &State)
{
	if(!State.m_Init)
	{
		State.m_UiElement.Init(pUi, -1);
		State.m_Init = true;
	}

	static SMultiSelectPopupContext s_PopupContext;

	const bool PopupOpen = pUi->IsPopupOpen(&State.m_PopupId);
	if(!PopupOpen && !State.m_PopupWasOpen)
		State.m_Flags = Flags;

	int SelectedCount = 0;
	for(int i = 0; i < Num; ++i)
	{
		if(State.m_Flags & pBits[i])
			++SelectedCount;
	}
	if(SelectedCount <= 0)
		str_copy(State.m_aLabel, Localize("Save/Load Assets"));
	else if(SelectedCount == 1)
	{
		for(int i = 0; i < Num; ++i)
		{
			if(State.m_Flags & pBits[i])
			{
				str_copy(State.m_aLabel, ppLabels[i]);
				break;
			}
		}
	}
	else
		str_format(State.m_aLabel, sizeof(State.m_aLabel), Localize("Assets (%d)"), SelectedCount);

	const auto LabelFunc = [&State]() {
		return State.m_aLabel;
	};

	SMenuButtonProperties Props;
	Props.m_HintRequiresStringCheck = true;
	Props.m_HintCanChangePositionOrSize = true;
	Props.m_ShowDropDownIcon = true;
	if(PopupOpen)
		Props.m_Corners = IGraphics::CORNER_ALL & (~s_PopupContext.m_Props.m_Corners);

	if(pUi->DoButton_Menu(State.m_UiElement, &State.m_ButtonContainer, LabelFunc, pRect, Props))
	{
		State.m_Flags = Flags;
		State.m_vLabels.clear();
		State.m_vpLabelPtrs.clear();
		State.m_vBits.clear();
		State.m_vLabels.reserve(Num);
		State.m_vpLabelPtrs.reserve(Num);
		State.m_vBits.reserve(Num);
		for(int i = 0; i < Num; ++i)
		{
			State.m_vLabels.emplace_back(ppLabels[i]);
			State.m_vBits.push_back(pBits[i]);
		}
		for(const std::string &Label : State.m_vLabels)
			State.m_vpLabelPtrs.push_back(Label.c_str());

		s_PopupContext = {};
		s_PopupContext.m_pMenus = pMenus;
		s_PopupContext.m_pFlags = &State.m_Flags;
		s_PopupContext.m_ppLabels = State.m_vpLabelPtrs.data();
		s_PopupContext.m_pBits = State.m_vBits.data();
		s_PopupContext.m_Num = Num;
		s_PopupContext.m_pScrollRegion = &State.m_ScrollRegion;
		s_PopupContext.m_EntryHeight = pRect->h;
		s_PopupContext.m_EntrySpacing = 0.0f;
		s_PopupContext.m_Props.m_BorderColor = ColorRGBA(0.7f, 0.7f, 0.7f, 0.9f);
		s_PopupContext.m_Props.m_BackgroundColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f);

		const float PopupHeight = std::min(
			Num * s_PopupContext.m_EntryHeight + 10.0f,
			pUi->Screen()->h * 0.4f);
		float X = pRect->x;
		float Y = pRect->y;
		s_PopupContext.m_Props.m_Corners = IGraphics::CORNER_ALL;
		constexpr float Margin = 5.0f;
		if(X + pRect->w > pUi->Screen()->w - Margin)
			X = std::max(X - pRect->w, Margin);
		if(Y + pRect->h + PopupHeight > pUi->Screen()->h - Margin)
		{
			Y -= PopupHeight;
			s_PopupContext.m_Props.m_Corners = IGraphics::CORNER_T;
		}
		else
		{
			Y += pRect->h;
			s_PopupContext.m_Props.m_Corners = IGraphics::CORNER_B;
		}
		pUi->DoPopupMenu(&State.m_PopupId, X, Y, pRect->w, PopupHeight, &s_PopupContext, PopupMultiSelect, s_PopupContext.m_Props);
		State.m_PopupWasOpen = true;
	}

	const bool StillOpen = pUi->IsPopupOpen(&State.m_PopupId);
	if(StillOpen)
		State.m_PopupWasOpen = true;
	else if(State.m_PopupWasOpen)
		State.m_PopupWasOpen = false;

	return State.m_Flags;
}

void BestClientDoProfileAssetsDropDown(CUi *pUi, CMenus *pMenus, CUIRect *pRect)
{
	static SBestClientMultiSelectDropDownState s_State;
	const char *apLabels[] = {
		Localize("Entities"),
		Localize("Game"),
		Localize("Emoticons"),
		Localize("Particles"),
		Localize("HUD"),
		Localize("Extras"),
		Localize("Cursor"),
		Localize("Arrow"),
		Localize("Audio"),
	};
	static const int s_aBits[] = {
		BC_PROFILE_ASSET_ENTITIES,
		BC_PROFILE_ASSET_GAME,
		BC_PROFILE_ASSET_EMOTICONS,
		BC_PROFILE_ASSET_PARTICLES,
		BC_PROFILE_ASSET_HUD,
		BC_PROFILE_ASSET_EXTRAS,
		BC_PROFILE_ASSET_CURSOR,
		BC_PROFILE_ASSET_ARROW,
		BC_PROFILE_ASSET_AUDIO,
	};
	g_Config.m_BcProfileAssets = DoMultiSelectDropDown(pUi, pMenus, pRect, g_Config.m_BcProfileAssets, apLabels, s_aBits, BC_PROFILE_ASSET_COUNT, s_State);
}
