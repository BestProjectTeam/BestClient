/* Copyright © 2026 BestProject Team */
#include "settings_nav.h"
#include "widgets.h"

#include <base/dbg.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/menu_background.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>

namespace BestClientUiTheme
{

static constexpr int TC_TAB_SETTINGS = 0;
static constexpr int TC_TAB_BINDWHEEL = 1;
static constexpr int TC_TAB_WARLIST = 2;
static constexpr int TC_TAB_BINDCHAT = 3;
static constexpr int TC_TAB_STATUSBAR = 4;
static constexpr int TC_TAB_INFO = 5;

static constexpr int BC_TAB_VISUALS = 0;
static constexpr int BC_TAB_GAMEPLAY = 1;
static constexpr int BC_TAB_OTHERS = 2;
static constexpr int BC_TAB_INFO = 3;

static const int s_aDdnetLeaves[] = {
	SETTINGS_NAV_LEAF_GENERAL,
	SETTINGS_NAV_LEAF_TEE,
	SETTINGS_NAV_LEAF_APPEARANCE,
	SETTINGS_NAV_LEAF_CONTROLS,
	SETTINGS_NAV_LEAF_GRAPHICS,
	SETTINGS_NAV_LEAF_SOUND,
	SETTINGS_NAV_LEAF_DDNET,
	SETTINGS_NAV_LEAF_ASSETS,
	SETTINGS_NAV_LEAF_CREDITS,
};
static const int s_aTclientLeaves[] = {
	SETTINGS_NAV_LEAF_TC_SETTINGS,
	SETTINGS_NAV_LEAF_TC_BIND_WHEEL,
	SETTINGS_NAV_LEAF_TC_WAR_LIST,
	SETTINGS_NAV_LEAF_TC_CHAT_BINDS,
	SETTINGS_NAV_LEAF_TC_STATUS_BAR,
	SETTINGS_NAV_LEAF_TC_PROFILE,
	SETTINGS_NAV_LEAF_TC_CONFIGS,
	SETTINGS_NAV_LEAF_TC_INFO,
};
static const int s_aBestclientLeaves[] = {
	SETTINGS_NAV_LEAF_BC_VISUALS,
	SETTINGS_NAV_LEAF_BC_GAMEPLAY,
	SETTINGS_NAV_LEAF_BC_OTHERS,
	SETTINGS_NAV_LEAF_BC_INFO,
};

static int LeafVisualRank(int Leaf)
{
	const int *apLists[] = {s_aDdnetLeaves, s_aTclientLeaves, s_aBestclientLeaves};
	const int aCounts[] = {
		(int)(sizeof(s_aDdnetLeaves) / sizeof(s_aDdnetLeaves[0])),
		(int)(sizeof(s_aTclientLeaves) / sizeof(s_aTclientLeaves[0])),
		(int)(sizeof(s_aBestclientLeaves) / sizeof(s_aBestclientLeaves[0])),
	};
	int Rank = 0;
	for(int List = 0; List < 3; ++List)
	{
		for(int i = 0; i < aCounts[List]; ++i, ++Rank)
		{
			if(apLists[List][i] == Leaf)
				return Rank;
		}
	}
	return Leaf;
}

static bool IsFlagSet(int Flags, int Bit)
{
	return (Flags & (1 << Bit)) != 0;
}

SSettingsLeafTarget LeafTarget(int Leaf)
{
	SSettingsLeafTarget Target;
	switch(Leaf)
	{
	case SETTINGS_NAV_LEAF_GENERAL:
		Target.m_SettingsPage = CMenus::SETTINGS_GENERAL;
		break;
	case SETTINGS_NAV_LEAF_TEE:
		Target.m_SettingsPage = CMenus::SETTINGS_TEE;
		break;
	case SETTINGS_NAV_LEAF_APPEARANCE:
		Target.m_SettingsPage = CMenus::SETTINGS_APPEARANCE;
		break;
	case SETTINGS_NAV_LEAF_CONTROLS:
		Target.m_SettingsPage = CMenus::SETTINGS_CONTROLS;
		break;
	case SETTINGS_NAV_LEAF_GRAPHICS:
		Target.m_SettingsPage = CMenus::SETTINGS_GRAPHICS;
		break;
	case SETTINGS_NAV_LEAF_SOUND:
		Target.m_SettingsPage = CMenus::SETTINGS_SOUND;
		break;
	case SETTINGS_NAV_LEAF_DDNET:
		Target.m_SettingsPage = CMenus::SETTINGS_DDNET;
		break;
	case SETTINGS_NAV_LEAF_ASSETS:
		Target.m_SettingsPage = CMenus::SETTINGS_ASSETS;
		break;
	case SETTINGS_NAV_LEAF_CREDITS:
		Target.m_SettingsPage = CMenus::SETTINGS_CREDITS;
		break;
	case SETTINGS_NAV_LEAF_TC_SETTINGS:
		Target.m_SettingsPage = CMenus::SETTINGS_TCLIENT;
		Target.m_TClientTab = TC_TAB_SETTINGS;
		break;
	case SETTINGS_NAV_LEAF_TC_BIND_WHEEL:
		Target.m_SettingsPage = CMenus::SETTINGS_TCLIENT;
		Target.m_TClientTab = TC_TAB_BINDWHEEL;
		break;
	case SETTINGS_NAV_LEAF_TC_WAR_LIST:
		Target.m_SettingsPage = CMenus::SETTINGS_TCLIENT;
		Target.m_TClientTab = TC_TAB_WARLIST;
		break;
	case SETTINGS_NAV_LEAF_TC_CHAT_BINDS:
		Target.m_SettingsPage = CMenus::SETTINGS_TCLIENT;
		Target.m_TClientTab = TC_TAB_BINDCHAT;
		break;
	case SETTINGS_NAV_LEAF_TC_STATUS_BAR:
		Target.m_SettingsPage = CMenus::SETTINGS_TCLIENT;
		Target.m_TClientTab = TC_TAB_STATUSBAR;
		break;
	case SETTINGS_NAV_LEAF_TC_INFO:
		Target.m_SettingsPage = CMenus::SETTINGS_TCLIENT;
		Target.m_TClientTab = TC_TAB_INFO;
		break;
	case SETTINGS_NAV_LEAF_TC_PROFILE:
		Target.m_SettingsPage = CMenus::SETTINGS_PROFILES;
		break;
	case SETTINGS_NAV_LEAF_TC_CONFIGS:
		Target.m_SettingsPage = CMenus::SETTINGS_CONFIGS;
		break;
	case SETTINGS_NAV_LEAF_BC_VISUALS:
		Target.m_SettingsPage = CMenus::SETTINGS_BESTCLIENT;
		Target.m_BestClientTab = BC_TAB_VISUALS;
		break;
	case SETTINGS_NAV_LEAF_BC_GAMEPLAY:
		Target.m_SettingsPage = CMenus::SETTINGS_BESTCLIENT;
		Target.m_BestClientTab = BC_TAB_GAMEPLAY;
		break;
	case SETTINGS_NAV_LEAF_BC_OTHERS:
		Target.m_SettingsPage = CMenus::SETTINGS_BESTCLIENT;
		Target.m_BestClientTab = BC_TAB_OTHERS;
		break;
	case SETTINGS_NAV_LEAF_BC_INFO:
		Target.m_SettingsPage = CMenus::SETTINGS_BESTCLIENT;
		Target.m_BestClientTab = BC_TAB_INFO;
		break;
	default:
		Target.m_SettingsPage = CMenus::SETTINGS_GENERAL;
		break;
	}
	return Target;
}

int GroupForLeaf(int Leaf)
{
	if(Leaf >= SETTINGS_NAV_LEAF_TC_SETTINGS && Leaf <= SETTINGS_NAV_LEAF_TC_CONFIGS)
		return SETTINGS_NAV_GROUP_TCLIENT;
	if(Leaf >= SETTINGS_NAV_LEAF_BC_VISUALS && Leaf <= SETTINGS_NAV_LEAF_BC_INFO)
		return SETTINGS_NAV_GROUP_BESTCLIENT;
	return SETTINGS_NAV_GROUP_DDNET;
}

bool IsLeafVisible(int Leaf, int TcTabFlags, int BcTabFlags)
{
	if(Leaf >= SETTINGS_NAV_LEAF_TC_SETTINGS && Leaf <= SETTINGS_NAV_LEAF_TC_INFO)
	{
		const int Tab = Leaf - SETTINGS_NAV_LEAF_TC_SETTINGS;
		return !IsFlagSet(TcTabFlags, Tab);
	}
	if(Leaf >= SETTINGS_NAV_LEAF_BC_VISUALS && Leaf <= SETTINGS_NAV_LEAF_BC_OTHERS)
	{
		const int Tab = Leaf - SETTINGS_NAV_LEAF_BC_VISUALS;
		return !IsFlagSet(BcTabFlags, Tab);
	}
	return true;
}

int LeafFromState(int SettingsPage, int TClientTab, int BestClientTab)
{
	switch(SettingsPage)
	{
	case CMenus::SETTINGS_GENERAL: return SETTINGS_NAV_LEAF_GENERAL;
	case CMenus::SETTINGS_TEE: return SETTINGS_NAV_LEAF_TEE;
	case CMenus::SETTINGS_APPEARANCE: return SETTINGS_NAV_LEAF_APPEARANCE;
	case CMenus::SETTINGS_CONTROLS: return SETTINGS_NAV_LEAF_CONTROLS;
	case CMenus::SETTINGS_GRAPHICS: return SETTINGS_NAV_LEAF_GRAPHICS;
	case CMenus::SETTINGS_SOUND: return SETTINGS_NAV_LEAF_SOUND;
	case CMenus::SETTINGS_DDNET: return SETTINGS_NAV_LEAF_DDNET;
	case CMenus::SETTINGS_ASSETS: return SETTINGS_NAV_LEAF_ASSETS;
	case CMenus::SETTINGS_CREDITS: return SETTINGS_NAV_LEAF_CREDITS;
	case CMenus::SETTINGS_PROFILES: return SETTINGS_NAV_LEAF_TC_PROFILE;
	case CMenus::SETTINGS_CONFIGS: return SETTINGS_NAV_LEAF_TC_CONFIGS;
	case CMenus::SETTINGS_TCLIENT:
		switch(TClientTab)
		{
		case TC_TAB_BINDWHEEL: return SETTINGS_NAV_LEAF_TC_BIND_WHEEL;
		case TC_TAB_WARLIST: return SETTINGS_NAV_LEAF_TC_WAR_LIST;
		case TC_TAB_BINDCHAT: return SETTINGS_NAV_LEAF_TC_CHAT_BINDS;
		case TC_TAB_STATUSBAR: return SETTINGS_NAV_LEAF_TC_STATUS_BAR;
		case TC_TAB_INFO: return SETTINGS_NAV_LEAF_TC_INFO;
		default: return SETTINGS_NAV_LEAF_TC_SETTINGS;
		}
	case CMenus::SETTINGS_BESTCLIENT:
		switch(BestClientTab)
		{
		case BC_TAB_GAMEPLAY: return SETTINGS_NAV_LEAF_BC_GAMEPLAY;
		case BC_TAB_OTHERS: return SETTINGS_NAV_LEAF_BC_OTHERS;
		case BC_TAB_INFO: return SETTINGS_NAV_LEAF_BC_INFO;
		default: return SETTINGS_NAV_LEAF_BC_VISUALS;
		}
	default:
		return SETTINGS_NAV_LEAF_GENERAL;
	}
}

void ApplyLeaf(CMenus *pMenus, int Leaf)
{
	if(!pMenus)
		return;
	const SSettingsLeafTarget Target = LeafTarget(Leaf);
	g_Config.m_UiSettingsPage = Target.m_SettingsPage;
	if(Target.m_SettingsPage == CMenus::SETTINGS_TCLIENT)
		pMenus->SetTClientSettingsTab(Target.m_TClientTab);
	if(Target.m_SettingsPage == CMenus::SETTINGS_BESTCLIENT)
	{
		pMenus->SetBestClientSettingsTab(Target.m_BestClientTab);
		pMenus->CloseBestClientFun();
	}
}

BCUiAnimations::STabSwitch AnimateVerticalLeafSwitch(int SelectedLeaf, float DeltaTime)
{
	BCUiAnimations::STabSwitch Result;
	struct SState
	{
		bool m_Initialized = false;
		int m_FromLeaf = 0;
		int m_ToLeaf = 0;
		float m_Progress = 1.0f;
	};
	static SState s_State;

	SelectedLeaf = std::clamp(SelectedLeaf, 0, SETTINGS_NAV_LEAF_COUNT - 1);
	if(!s_State.m_Initialized)
	{
		s_State.m_Initialized = true;
		s_State.m_FromLeaf = SelectedLeaf;
		s_State.m_ToLeaf = SelectedLeaf;
		s_State.m_Progress = 1.0f;
	}
	if(SelectedLeaf != s_State.m_ToLeaf)
	{
		s_State.m_FromLeaf = s_State.m_ToLeaf;
		s_State.m_ToLeaf = SelectedLeaf;
		s_State.m_Progress = 0.0f;
	}
	s_State.m_FromLeaf = std::clamp(s_State.m_FromLeaf, 0, SETTINGS_NAV_LEAF_COUNT - 1);
	s_State.m_ToLeaf = std::clamp(s_State.m_ToLeaf, 0, SETTINGS_NAV_LEAF_COUNT - 1);

	if(!BCUiAnimations::SettingsUiAnimationEnabled())
	{
		s_State.m_FromLeaf = SelectedLeaf;
		s_State.m_ToLeaf = SelectedLeaf;
		s_State.m_Progress = 1.0f;
	}
	else if(s_State.m_Progress < 1.0f)
	{
		const float Step = std::clamp(DeltaTime * 12.0f, 0.0f, 1.0f);
		s_State.m_Progress += (1.0f - s_State.m_Progress) * Step;
		if(s_State.m_Progress > 0.995f)
			s_State.m_Progress = 1.0f;
	}

	Result.m_Progress = s_State.m_Progress;
	Result.m_FromIndex = s_State.m_FromLeaf;
	Result.m_ToIndex = s_State.m_ToLeaf;
	return Result;
}

void VerticalLeafContentOffsets(float Progress, int FromLeaf, int ToLeaf, float Height, float &FromOffset, float &ToOffset)
{
	const float Eased = BCUiAnimations::EaseOutCubic(Progress);
	const float SlideDistance = Height + 6.0f;
	const int Direction = ToLeaf >= FromLeaf ? 1 : -1;
	FromOffset = -Direction * Eased * SlideDistance;
	ToOffset = Direction * (1.0f - Eased) * SlideDistance;
}

} // namespace BestClientUiTheme

static void LoadSettingsNavExpanded(bool *pExpanded, int CurGroup)
{
	int Mask = g_Config.m_BcSettingsNavExpanded;
	bool AnyExpanded = false;
	for(int Group = 0; Group < BestClientUiTheme::SETTINGS_NAV_GROUP_COUNT; ++Group)
	{
		pExpanded[Group] = (Mask & (1 << Group)) != 0;
		if(pExpanded[Group])
			AnyExpanded = true;
	}
	if(!AnyExpanded)
		pExpanded[CurGroup] = true;
}

static void SaveSettingsNavExpanded(const bool *pExpanded)
{
	int Mask = 0;
	for(int Group = 0; Group < BestClientUiTheme::SETTINGS_NAV_GROUP_COUNT; ++Group)
	{
		if(pExpanded[Group])
			Mask |= (1 << Group);
	}
	g_Config.m_BcSettingsNavExpanded = Mask;
}

void CMenus::RenderSettingsNavNewStyle(CUIRect &TabBar)
{
	using namespace BestClientUiTheme;

	static bool s_aExpandedGroup[SETTINGS_NAV_GROUP_COUNT] = {};
	static float s_aGroupPhase[SETTINGS_NAV_GROUP_COUNT] = {};
	static bool s_PhasesInit = false;
	static CButtonContainer s_aGroupButtons[SETTINGS_NAV_GROUP_COUNT];
	static CButtonContainer s_aLeafButtons[SETTINGS_NAV_LEAF_COUNT];

	const int CurLeaf = LeafFromState(g_Config.m_UiSettingsPage, GetTClientSettingsTab(), GetBestClientSettingsTab());
	const int CurGroup = GroupForLeaf(CurLeaf);
	if(!s_PhasesInit)
	{
		LoadSettingsNavExpanded(s_aExpandedGroup, CurGroup);
		for(int Group = 0; Group < SETTINGS_NAV_GROUP_COUNT; ++Group)
			s_aGroupPhase[Group] = s_aExpandedGroup[Group] ? 1.0f : 0.0f;
		s_PhasesInit = true;
	}

	const bool ExploreLock = m_AssetsEditorState.m_Open && m_AssetsEditorState.m_ExploreSide >= 0;
	const float Dt = Client()->RenderFrameTime();
	const float GroupH = 26.0f;
	const float LeafH = 22.0f;
	const float GroupGap = 8.0f;
	const float LeafGap = 4.0f;
	const float LeafRightInset = 14.0f;
	const bool NavOnLeft = IsSettingsNavOnLeft();
	const int NavCorners = NavOnLeft ? IGraphics::CORNER_L : IGraphics::CORNER_R;

	constexpr int DdnetLeafCount = (int)(sizeof(s_aDdnetLeaves) / sizeof(s_aDdnetLeaves[0]));
	constexpr int TclientLeafCount = (int)(sizeof(s_aTclientLeaves) / sizeof(s_aTclientLeaves[0]));
	constexpr int BestclientLeafCount = (int)(sizeof(s_aBestclientLeaves) / sizeof(s_aBestclientLeaves[0]));

	const char *apGroupNames[SETTINGS_NAV_GROUP_COUNT] = {
		Localize("DDNet"),
		TCLocalize("TClient"),
		Localize("BestClient"),
	};

	auto LeafName = [&](int Leaf) -> const char * {
		switch(Leaf)
		{
		case SETTINGS_NAV_LEAF_GENERAL: return Localize("General");
		case SETTINGS_NAV_LEAF_TEE: return Client()->IsSixup() ? "Tee 0.7" : Localize("Tee");
		case SETTINGS_NAV_LEAF_APPEARANCE: return Localize("Appearance");
		case SETTINGS_NAV_LEAF_CONTROLS: return Localize("Controls");
		case SETTINGS_NAV_LEAF_GRAPHICS: return Localize("Graphics");
		case SETTINGS_NAV_LEAF_SOUND: return Localize("Sound");
		case SETTINGS_NAV_LEAF_DDNET: return Localize("DDNet");
		case SETTINGS_NAV_LEAF_ASSETS: return Localize("Assets");
		case SETTINGS_NAV_LEAF_CREDITS: return Localize("Credits");
		case SETTINGS_NAV_LEAF_TC_SETTINGS: return TCLocalize("Settings");
		case SETTINGS_NAV_LEAF_TC_BIND_WHEEL: return TCLocalize("Bind Wheel");
		case SETTINGS_NAV_LEAF_TC_WAR_LIST: return TCLocalize("War List");
		case SETTINGS_NAV_LEAF_TC_CHAT_BINDS: return TCLocalize("Chat Binds");
		case SETTINGS_NAV_LEAF_TC_STATUS_BAR: return TCLocalize("Status Bar");
		case SETTINGS_NAV_LEAF_TC_INFO: return TCLocalize("Info");
		case SETTINGS_NAV_LEAF_TC_PROFILE: return Localize("Profiles");
		case SETTINGS_NAV_LEAF_TC_CONFIGS: return Localize("Configs");
		case SETTINGS_NAV_LEAF_BC_VISUALS: return BcLocalize("Visuals");
		case SETTINGS_NAV_LEAF_BC_GAMEPLAY: return BcLocalize("Gameplay");
		case SETTINGS_NAV_LEAF_BC_OTHERS: return BcLocalize("Others");
		case SETTINGS_NAV_LEAF_BC_INFO: return BcLocalize("Info");
		default: return "";
		}
	};

	auto CollectVisibleLeaves = [&](const int *pLeaves, int Count, int *pOut, int &OutCount) {
		OutCount = 0;
		for(int i = 0; i < Count; ++i)
		{
			const int Leaf = pLeaves[i];
			if(!IsLeafVisible(Leaf, g_Config.m_TcTClientSettingsTabs, g_Config.m_BcBestClientSettingsTabs))
				continue;
			pOut[OutCount++] = Leaf;
		}
	};

	int aVisibleLeaves[SETTINGS_NAV_LEAF_COUNT];
	int VisibleCount = 0;

	for(int Group = 0; Group < SETTINGS_NAV_GROUP_COUNT; ++Group)
	{
		TabBar.HSplitTop(Group == 0 ? 4.0f : GroupGap, nullptr, &TabBar);
		CUIRect GroupButton;
		TabBar.HSplitTop(GroupH, &GroupButton, &TabBar);
		const bool Expanded = s_aExpandedGroup[Group];
		const ColorRGBA GroupInactive = SettingsNavGroupInactiveColor();
		const ColorRGBA GroupActive = SettingsNavGroupActiveColor();
		const ColorRGBA GroupHover = SettingsNavGroupHoverColor();
		if(DoButton_MenuTab(&s_aGroupButtons[Group], apGroupNames[Group], Expanded || CurGroup == Group, &GroupButton, NavCorners, nullptr, &GroupInactive, &GroupActive, &GroupHover, 10.0f, nullptr, true, IGraphics::CORNER_NONE, -1.0f, ExploreLock ? 0.35f : 1.0f) && !ExploreLock)
		{
			s_aExpandedGroup[Group] = !s_aExpandedGroup[Group];
			if(s_aExpandedGroup[Group])
			{
				if(Group == SETTINGS_NAV_GROUP_DDNET)
					s_aExpandedGroup[SETTINGS_NAV_GROUP_TCLIENT] = false;
				else if(Group == SETTINGS_NAV_GROUP_TCLIENT)
					s_aExpandedGroup[SETTINGS_NAV_GROUP_DDNET] = false;

				int aFirstLeaves[SETTINGS_NAV_LEAF_COUNT];
				int FirstCount = 0;
				if(Group == SETTINGS_NAV_GROUP_DDNET)
					CollectVisibleLeaves(s_aDdnetLeaves, DdnetLeafCount, aFirstLeaves, FirstCount);
				else if(Group == SETTINGS_NAV_GROUP_TCLIENT)
					CollectVisibleLeaves(s_aTclientLeaves, TclientLeafCount, aFirstLeaves, FirstCount);
				else
					CollectVisibleLeaves(s_aBestclientLeaves, BestclientLeafCount, aFirstLeaves, FirstCount);
				if(FirstCount > 0 && GroupForLeaf(CurLeaf) != Group)
					ApplyLeaf(this, aFirstLeaves[0]);
			}
			SaveSettingsNavExpanded(s_aExpandedGroup);
		}

		if(Group == SETTINGS_NAV_GROUP_DDNET)
			CollectVisibleLeaves(s_aDdnetLeaves, DdnetLeafCount, aVisibleLeaves, VisibleCount);
		else if(Group == SETTINGS_NAV_GROUP_TCLIENT)
			CollectVisibleLeaves(s_aTclientLeaves, TclientLeafCount, aVisibleLeaves, VisibleCount);
		else
			CollectVisibleLeaves(s_aBestclientLeaves, BestclientLeafCount, aVisibleLeaves, VisibleCount);

		const float TargetHeight = VisibleCount > 0 ? VisibleCount * (LeafH + LeafGap) : 0.0f;
		const float RevealH = BCUiAnimations::ModuleReveal(s_aGroupPhase[Group], Expanded, TargetHeight, Dt);
		if(RevealH <= 0.5f || VisibleCount <= 0)
			continue;

		CUIRect LeavesArea;
		TabBar.HSplitTop(RevealH, &LeavesArea, &TabBar);
		Ui()->ClipEnable(&LeavesArea);

		CUIRect LeafCursor = LeavesArea;
		for(int i = 0; i < VisibleCount; ++i)
		{
			const int Leaf = aVisibleLeaves[i];
			CUIRect LeafButton;
			LeafCursor.HSplitTop(LeafGap, nullptr, &LeafCursor);
			LeafCursor.HSplitTop(LeafH, &LeafButton, &LeafCursor);
			if(NavOnLeft)
				LeafButton.VSplitLeft(LeafRightInset, nullptr, &LeafButton);
			else
				LeafButton.VSplitRight(LeafRightInset, &LeafButton, nullptr);
			const bool AssetsLeaf = Leaf == SETTINGS_NAV_LEAF_ASSETS;
			const ColorRGBA LeafInactive = SettingsNavLeafInactiveColor();
			const ColorRGBA LeafActive = SettingsNavLeafActiveColor();
			const ColorRGBA LeafHover = SettingsNavLeafHoverColor();
			if(DoButton_MenuTab(&s_aLeafButtons[Leaf], LeafName(Leaf), CurLeaf == Leaf, &LeafButton, NavCorners, nullptr, &LeafInactive, &LeafActive, &LeafHover, 10.0f, nullptr, true, IGraphics::CORNER_NONE, -1.0f, ExploreLock && !AssetsLeaf ? 0.35f : 1.0f) && (!ExploreLock || AssetsLeaf))
			{
				s_aExpandedGroup[Group] = true;
				SaveSettingsNavExpanded(s_aExpandedGroup);
				ApplyLeaf(this, Leaf);
			}
		}

		Ui()->ClipDisable();
	}
}

void CMenus::RenderSettingsPageAt(CUIRect MainView, int SettingsPage, int TClientTab, int BestClientTab)
{
	const int OldPage = g_Config.m_UiSettingsPage;
	const int OldTc = GetTClientSettingsTab();
	const int OldBc = GetBestClientSettingsTab();
	g_Config.m_UiSettingsPage = SettingsPage;
	SetTClientSettingsTab(TClientTab);
	SetBestClientSettingsTab(BestClientTab);
	RenderSettingsPage(MainView);
	g_Config.m_UiSettingsPage = OldPage;
	SetTClientSettingsTab(OldTc);
	SetBestClientSettingsTab(OldBc);
}

void CMenus::RenderSettingsPage(CUIRect MainView)
{
	if(g_Config.m_UiSettingsPage == SETTINGS_GENERAL)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_GENERAL);
		RenderSettingsGeneral(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_TEE)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_TEE);
		if(Client()->IsSixup())
			RenderSettingsTee7(MainView);
		else
			RenderSettingsTee(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_APPEARANCE)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_APPEARANCE);
		RenderSettingsAppearance(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_CONTROLS)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_CONTROLS);
		m_MenusSettingsControls.Render(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_GRAPHICS)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_GRAPHICS);
		RenderSettingsGraphics(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_SOUND)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_SOUND);
		RenderSettingsSound(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_DDNET)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_DDNET);
		RenderSettingsDDNet(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_ASSETS)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_ASSETS);
		RenderSettingsAssets(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_TCLIENT)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_LANGUAGE);
		RenderSettingsTClient(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_BESTCLIENT)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_RESERVED0);
		RenderSettingsBestClient(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_PROFILES)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_GENERAL);
		RenderSettingsTClientProfiles(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_CONFIGS)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_PLAYER);
		RenderSettingsTClientConfigs(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_CREDITS)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_CREDITS);
		RenderSettingsCredits(MainView);
	}
	else
	{
		dbg_assert_failed("ui_settings_page invalid");
	}
}

void CMenus::RenderSettingsContentNewStyle(CUIRect MainView)
{
	using namespace BestClientUiTheme;

	const int CurLeaf = LeafFromState(g_Config.m_UiSettingsPage, GetTClientSettingsTab(), GetBestClientSettingsTab());
	const BCUiAnimations::STabSwitch Anim = AnimateVerticalLeafSwitch(CurLeaf, Client()->RenderFrameTime());
	const SSettingsLeafTarget From = LeafTarget(Anim.m_FromIndex);
	const SSettingsLeafTarget To = LeafTarget(Anim.m_ToIndex);
	const bool InvolvesAssets = From.m_SettingsPage == SETTINGS_ASSETS || To.m_SettingsPage == SETTINGS_ASSETS;
	if(Anim.m_Progress >= 1.0f || Anim.m_FromIndex == Anim.m_ToIndex || InvolvesAssets)
	{
		RenderSettingsPage(MainView);
		return;
	}

	float FromOffset = 0.0f;
	float ToOffset = 0.0f;
	VerticalLeafContentOffsets(Anim.m_Progress, LeafVisualRank(Anim.m_FromIndex), LeafVisualRank(Anim.m_ToIndex), MainView.h, FromOffset, ToOffset);
	(void)FromOffset;

	CUIRect ToView = MainView;
	ToView.y += ToOffset;

	CUIRect ClipRect = MainView;
	ClipRect.y -= 20.0f;
	ClipRect.h += 20.0f;
	Ui()->ClipEnable(&ClipRect);
	RenderSettingsPageAt(ToView, To.m_SettingsPage, To.m_TClientTab, To.m_BestClientTab);
	Ui()->ClipDisable();
}

void CMenus::RenderSettingsContentLegacyStyle(CUIRect MainView)
{
	using namespace BestClientUiTheme;

	struct SPageAnimState
	{
		bool m_Initialized = false;
		int m_FromPage = 0;
		int m_ToPage = 0;
		float m_Progress = 1.0f;
	};
	static SPageAnimState s_PageAnim;

	const int CurPage = g_Config.m_UiSettingsPage;
	if(!s_PageAnim.m_Initialized)
	{
		s_PageAnim.m_Initialized = true;
		s_PageAnim.m_FromPage = CurPage;
		s_PageAnim.m_ToPage = CurPage;
		s_PageAnim.m_Progress = 1.0f;
	}
	if(CurPage != s_PageAnim.m_ToPage)
	{
		s_PageAnim.m_FromPage = s_PageAnim.m_ToPage;
		s_PageAnim.m_ToPage = CurPage;
		s_PageAnim.m_Progress = 0.0f;
	}

	if(!BCUiAnimations::SettingsUiAnimationEnabled())
	{
		s_PageAnim.m_FromPage = CurPage;
		s_PageAnim.m_ToPage = CurPage;
		s_PageAnim.m_Progress = 1.0f;
	}
	else if(s_PageAnim.m_Progress < 1.0f)
	{
		const float Step = std::clamp(Client()->RenderFrameTime() * 12.0f, 0.0f, 1.0f);
		s_PageAnim.m_Progress += (1.0f - s_PageAnim.m_Progress) * Step;
		if(s_PageAnim.m_Progress > 0.995f)
			s_PageAnim.m_Progress = 1.0f;
	}

	const bool InvolvesAssets = s_PageAnim.m_FromPage == SETTINGS_ASSETS || s_PageAnim.m_ToPage == SETTINGS_ASSETS;
	if(s_PageAnim.m_Progress >= 1.0f || s_PageAnim.m_FromPage == s_PageAnim.m_ToPage || InvolvesAssets)
	{
		RenderSettingsPage(MainView);
		return;
	}

	float FromOffset = 0.0f;
	float ToOffset = 0.0f;
	VerticalLeafContentOffsets(s_PageAnim.m_Progress, s_PageAnim.m_FromPage, s_PageAnim.m_ToPage, MainView.h, FromOffset, ToOffset);
	(void)FromOffset;

	CUIRect ToView = MainView;
	ToView.y += ToOffset;

	CUIRect ClipRect = MainView;
	ClipRect.y -= 20.0f;
	ClipRect.h += 20.0f;
	Ui()->ClipEnable(&ClipRect);
	RenderSettingsPageAt(ToView, s_PageAnim.m_ToPage, GetTClientSettingsTab(), GetBestClientSettingsTab());
	Ui()->ClipDisable();
}
