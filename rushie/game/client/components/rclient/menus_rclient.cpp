#include "engine/font_icons.h"

#include <base/math.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/linereader.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <generated/protocol.h>

#include <game/client/animstate.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/menus.h>
#include <game/client/components/rclient/bindwheel.h>
#include <game/client/components/tclient/bindchat.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <SDL_audio.h>

#include <algorithm>
#include <cctype>
#include <vector>
enum
{
	RCLIENT_TAB_SETTINGS = 0,
	RCLIENT_TAB_BINDWHEEL,
	RCLIENT_TAB_NAMEPLATES_EDITOR,
	RCLIENT_TAB_RCON,
	RCLIENT_TAB_VOICE,
	RCLIENT_TAB_INFO,
	NUMBER_OF_RUSHIE_TABS
};

enum
{
	RCLIENT_SETTINGS_SECTION_AUTO_CHANGE_PLAYER_INFO = 0,
	RCLIENT_SETTINGS_SECTION_CHAT_FUNCTIONS,
	RCLIENT_SETTINGS_SECTION_BLOCK_LIST,
	RCLIENT_SETTINGS_SECTION_CHAT,
	RCLIENT_SETTINGS_SECTION_SCOREBOARD,
	RCLIENT_SETTINGS_SECTION_CHANGED_TATER,
	RCLIENT_SETTINGS_SECTION_NAMEPLATES,
	RCLIENT_SETTINGS_SECTION_DUMMY,
	RCLIENT_SETTINGS_SECTION_EFFECTS,
	RCLIENT_SETTINGS_SECTION_TRACKER_PLAYER,
	RCLIENT_SETTINGS_SECTION_HUD,
	RCLIENT_SETTINGS_SECTION_CONTROLS,
	RCLIENT_SETTINGS_SECTION_LASER,
	RCLIENT_SETTINGS_SECTION_SPECTATOR,
	RCLIENT_SETTINGS_SECTION_CHAT_BUBBLES,
	RCLIENT_SETTINGS_SECTION_RCLIENT_INDICATOR,
	RCLIENT_SETTINGS_SECTION_EDGE_INFO,
	RCLIENT_SETTINGS_SECTION_VOICE,
	RCLIENT_SETTINGS_SECTION_MENUS,
	NUM_RCLIENT_SETTINGS_SECTIONS
};

static float s_Time = 0.0f;
static bool s_StartedTime = false;

const float FontSize = 14.0f;
const float EditBoxFontSize = 12.0f;
const float LineSize = 20.0f;

static void VoiceNameVolumesRemove(char *pList, int ListSize, const char *pName)
{
	if(!pName || pName[0] == '\0')
		return;

	char aNew[512];
	aNew[0] = '\0';

	const char *p = pList;
	while(*p)
	{
		while(*p == ',' || std::isspace((unsigned char)*p))
			p++;
		if(*p == '\0')
			break;

		const char *pStart = p;
		while(*p && *p != ',')
			p++;
		const char *pEnd = p;
		while(pEnd > pStart && std::isspace((unsigned char)pEnd[-1]))
			pEnd--;

		const char *pTokenStart = pStart;
		while(pTokenStart < pEnd && std::isspace((unsigned char)*pTokenStart))
			pTokenStart++;
		if(pEnd <= pTokenStart)
			continue;

		const char *pSep = nullptr;
		for(const char *q = pTokenStart; q < pEnd; q++)
		{
			if(*q == '=' || *q == ':')
			{
				pSep = q;
				break;
			}
		}

		bool Match = false;
		if(pSep)
		{
			const char *pNameEnd = pSep;
			while(pNameEnd > pTokenStart && std::isspace((unsigned char)pNameEnd[-1]))
				pNameEnd--;
			const int NameLen = (int)(pNameEnd - pTokenStart);
			if(NameLen > 0)
			{
				char aToken[MAX_NAME_LENGTH];
				str_truncate(aToken, sizeof(aToken), pTokenStart, NameLen);
				if(str_comp_nocase(aToken, pName) == 0)
					Match = true;
			}
		}

		if(Match)
			continue;

		char aTokenFull[128];
		str_truncate(aTokenFull, sizeof(aTokenFull), pTokenStart, (int)(pEnd - pTokenStart));
		if(aTokenFull[0] == '\0')
			continue;
		if(aNew[0] != '\0')
			str_append(aNew, ",", sizeof(aNew));
		str_append(aNew, aTokenFull, sizeof(aNew));
	}

	str_copy(pList, aNew, ListSize);
}

static void VoiceNameVolumesSet(char *pList, int ListSize, const char *pName, int Percent)
{
	if(!pName || pName[0] == '\0')
		return;
	Percent = std::clamp(Percent, 0, 200);
	VoiceNameVolumesRemove(pList, ListSize, pName);
	char aItem[128];
	str_format(aItem, sizeof(aItem), "%s=%d", pName, Percent);
	if(pList[0] != '\0')
		str_append(pList, ",", ListSize);
	str_append(pList, aItem, ListSize);
}

static bool VoiceNameVolumesGet(const char *pList, const char *pName, int &OutPercent)
{
	if(!pList || pList[0] == '\0' || !pName || pName[0] == '\0')
		return false;

	const char *p = pList;
	while(*p)
	{
		while(*p == ',' || *p == ' ' || *p == '\t')
			p++;
		if(*p == '\0')
			break;

		const char *pStart = p;
		while(*p && *p != ',')
			p++;
		const char *pEnd = p;
		while(pEnd > pStart && std::isspace((unsigned char)pEnd[-1]))
			pEnd--;
		if(pEnd <= pStart)
			continue;

		const char *pSep = nullptr;
		for(const char *q = pStart; q < pEnd; q++)
		{
			if(*q == '=' || *q == ':')
			{
				pSep = q;
				break;
			}
		}
		if(!pSep)
			continue;

		const char *pNameEnd = pSep;
		while(pNameEnd > pStart && std::isspace((unsigned char)pNameEnd[-1]))
			pNameEnd--;
		const char *pValueStart = pSep + 1;
		while(pValueStart < pEnd && std::isspace((unsigned char)*pValueStart))
			pValueStart++;

		const int NameLen = (int)(pNameEnd - pStart);
		const int ValueLen = (int)(pEnd - pValueStart);
		if(NameLen <= 0 || ValueLen <= 0)
			continue;

		char aToken[MAX_NAME_LENGTH];
		str_truncate(aToken, sizeof(aToken), pStart, NameLen);
		if(str_comp_nocase(aToken, pName) != 0)
			continue;

		char aValue[16];
		str_truncate(aValue, sizeof(aValue), pValueStart, ValueLen);
		int Percent = str_toint(aValue);
		Percent = std::clamp(Percent, 0, 200);
		OutPercent = Percent;
		return true;
	}

	return false;
}

const float ColorPickerLineSize = 25.0f;
const float HeadlineFontSize = 20.0f;

const float HeadlineHeight = HeadlineFontSize + 0.0f;
const float Margin = 10.0f;
const float MarginSmall = 5.0f;
const float MarginExtraSmall = 2.5f;
const float MarginBetweenSections = 30.0f;
const float MarginBetweenViews = 30.0f;

const float ColorPickerLabelSize = 13.0f;
const float ColorPickerLineSpacing = 5.0f;

struct SDropDownSimple
{
	CUi::SDropDownState m_State;
	CScrollRegion m_ScrollRegion;
	std::vector<const char *> m_vNames;
};

// Example
// static SDropDownSimple s_MyDrop;
// g_Config.m_RiShowHammerHit = DoSimpleDropDown(
// 	Ui(),
// 	Column,
// 	RCLocalize("My setting:"),
// 	g_Config.m_RiShowHammerHit,
// 	{"Off", "On", "Auto"},
// 	"My setting",
// 	s_MyDrop);

static int DoSimpleDropDown(CUi *pUi, CUIRect &Column, const char *pLabel, int CurrentValue, const std::vector<const char *> &vKeys, const char *pLocalizeContext, SDropDownSimple &Helper)
{
	Helper.m_vNames.clear();
	Helper.m_vNames.reserve(vKeys.size());
	for(const char *pKey : vKeys)
		Helper.m_vNames.push_back(RCLocalize(pKey, pLocalizeContext));

	Helper.m_State.m_SelectionPopupContext.m_pScrollRegion = &Helper.m_ScrollRegion;

	CUIRect DropDownRect, Label;
	Column.HSplitTop(LineSize, &DropDownRect, &Column);
	DropDownRect.VSplitLeft(120.0f, &Label, &DropDownRect);
	pUi->DoLabel(&Label, pLabel, FontSize, TEXTALIGN_ML);
	CurrentValue = pUi->DoDropDown(&DropDownRect, CurrentValue, Helper.m_vNames.data(), Helper.m_vNames.size(), Helper.m_State);
	return CurrentValue;
}

static void SetFlag(int32_t &Flags, int n, bool Value)
{
	if(Value)
		Flags |= (1 << n);
	else
		Flags &= ~(1 << n);
}

static bool IsFlagSet(int32_t Flags, int n)
{
	return (Flags & (1 << n)) != 0;
}

void CMenus::RenderSettingsRushie(CUIRect MainView)
{
	s_Time += Client()->RenderFrameTime() * (1.0f / 100.0f);
	if(!s_StartedTime)
	{
		s_StartedTime = true;
		s_Time = (float)rand() / (float)RAND_MAX;
	}

	if(Client()->RconAuthed())
	{
		SetFlag(g_Config.m_RiRClientSettingsTabs, RCLIENT_TAB_RCON, 0);
	}
	else
	{
		SetFlag(g_Config.m_RiRClientSettingsTabs, RCLIENT_TAB_RCON, 1);
	}

	static int s_CurCustomTab = 0;

	CUIRect TabBar, Button;
	int TabCount = NUMBER_OF_RUSHIE_TABS;
	for(int Tab = 0; Tab < NUMBER_OF_RUSHIE_TABS; ++Tab)
	{
		if(IsFlagSet(g_Config.m_RiRClientSettingsTabs, Tab))
		{
			TabCount--;
			if(s_CurCustomTab == Tab)
				s_CurCustomTab++;
		}
	}

	MainView.HSplitTop(LineSize * 1.2f, &TabBar, &MainView);
	const float TabWidth = TabBar.w / TabCount;
	static CButtonContainer s_aPageTabs[NUMBER_OF_RUSHIE_TABS] = {};
	const char *apTabNames[] = {
		RCLocalize("Settings"),
		RCLocalize("Bindwheel in spec"),
		RCLocalize("Nameplate editor"),
		RCLocalize("RCON"),
		RCLocalize("Voice mix"),
		RCLocalize("Info")};

	for(int Tab = 0; Tab < NUMBER_OF_RUSHIE_TABS; ++Tab)
	{
		if(IsFlagSet(g_Config.m_RiRClientSettingsTabs, Tab))
			continue;

		TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
		const int Corners = Tab == 0 ? IGraphics::CORNER_L : Tab == NUMBER_OF_RUSHIE_TABS - 1 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE;
		if(DoButton_MenuTab(&s_aPageTabs[Tab], apTabNames[Tab], s_CurCustomTab == Tab, &Button, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
			s_CurCustomTab = Tab;
	}

	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	if(s_CurCustomTab == RCLIENT_TAB_SETTINGS)
	{
		RenderSettingsRushieSettings(MainView);
	}

	if(s_CurCustomTab == RCLIENT_TAB_BINDWHEEL)
	{
		RenderSettingsRushieBindWheelSpec(MainView);
	}

	if(s_CurCustomTab == RCLIENT_TAB_NAMEPLATES_EDITOR)
	{
		RenderSettingsRushieNameplatesEditor(MainView);
	}
	if(s_CurCustomTab == RCLIENT_TAB_RCON)
	{
		RenderSettingsRushieRCON(MainView);
	}
	if(s_CurCustomTab == RCLIENT_TAB_VOICE)
	{
		RenderSettingsRushieVoiceVolumes(MainView);
	}
	if(s_CurCustomTab == RCLIENT_TAB_INFO)
	{
		RenderSettingsRushieInfo(MainView);
	}
}
void CMenus::RenderSettingsRushieVoiceVolumes(CUIRect MainView)
{
	static CScrollRegion s_ScrollRegion;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 120.0f;
	ScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	s_ScrollRegion.Begin(&MainView, &ScrollOffset, &ScrollParams);

	MainView.y += ScrollOffset.y;

	CUIRect Header, Row, SkinRect, RightRect, TextRect, SliderRect, NameRect, SkinNameRect;
	MainView.HSplitTop(HeadlineHeight, &Header, &MainView);
	Ui()->DoLabel(&Header, RCLocalize("Voice mix"), HeadlineFontSize, TEXTALIGN_MC);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	CUIRect SearchRect;
	MainView.HSplitTop(25.0f, &SearchRect, &MainView);
	SearchRect.HSplitTop(MarginSmall, nullptr, &SearchRect);
	static CLineInputBuffered<64> s_VoiceSearchInput;
	s_VoiceSearchInput.SetEmptyText(RCLocalize("Search"));
	Ui()->DoEditBox_Search(&s_VoiceSearchInput, &SearchRect, 14.0f, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	const char *pSearch = s_VoiceSearchInput.GetString();
	const bool HasSearch = pSearch && pSearch[0] != '\0';
	int ActiveCount = 0;
	int MatchCount = 0;
	const float RowHeight = LineSize * 2.0f;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const auto &Client = GameClient()->m_aClients[i];
		if(!Client.m_Active || Client.m_aName[0] == '\0')
			continue;
		ActiveCount++;
		if(HasSearch && !str_find_nocase(Client.m_aName, pSearch) && !str_find_nocase(Client.m_aSkinName, pSearch))
			continue;
		MatchCount++;

		MainView.HSplitTop(RowHeight, &Row, &MainView);
		if(!s_ScrollRegion.AddRect(Row))
			continue;

		Row.VSplitLeft(RowHeight, &SkinRect, &RightRect);
		RightRect.HSplitMid(&TextRect, &SliderRect);
		TextRect.HSplitMid(&NameRect, &SkinNameRect);

		CTeeRenderInfo TeeRenderInfo = Client.m_RenderInfo;
		TeeRenderInfo.m_Size = RowHeight;
		RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeRenderInfo, EMOTE_NORMAL, vec2(1, 0), SkinRect.Center());

		Ui()->DoLabel(&NameRect, Client.m_aName, FontSize, TEXTALIGN_ML);
		Ui()->DoLabel(&SkinNameRect, Client.m_aSkinName, FontSize * 0.9f, TEXTALIGN_ML);

		int Volume = 100;
		VoiceNameVolumesGet(g_Config.m_RiVoiceNameVolumes, Client.m_aName, Volume);
		if(Ui()->DoScrollbarOption(&GameClient()->m_aClients[i], &Volume, &SliderRect, RCLocalize("Volume"), 0, 200))
		{
			if(Volume == 100)
				VoiceNameVolumesRemove(g_Config.m_RiVoiceNameVolumes, sizeof(g_Config.m_RiVoiceNameVolumes), Client.m_aName);
			else
				VoiceNameVolumesSet(g_Config.m_RiVoiceNameVolumes, sizeof(g_Config.m_RiVoiceNameVolumes), Client.m_aName, Volume);
		}

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	}

	if(ActiveCount == 0)
	{
		MainView.HSplitTop(LineSize, &Header, &MainView);
		Ui()->DoLabel(&Header, RCLocalize("No active players"), FontSize, TEXTALIGN_ML);
	}
	else if(MatchCount == 0)
	{
		MainView.HSplitTop(LineSize, &Header, &MainView);
		Ui()->DoLabel(&Header, RCLocalize("No matching players"), FontSize, TEXTALIGN_ML);
	}

	CUIRect ScrollRegion;
	ScrollRegion.x = MainView.x;
	ScrollRegion.y = MainView.y + MarginSmall;
	ScrollRegion.w = MainView.w;
	ScrollRegion.h = 0.0f;
	s_ScrollRegion.AddRect(ScrollRegion);
	s_ScrollRegion.End();
}

void CMenus::RenderSettingsRushieInfo(CUIRect MainView)
{
	CUIRect LeftView, RightView, Button, Label, LowerLeftView;
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);
	LeftView.HSplitMid(&LeftView, &LowerLeftView, 0.0f);

	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, RCLocalize("Rushie Client Links"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	static CButtonContainer s_DiscordButton, s_WebsiteButton;
	CUIRect ButtonLeft, ButtonRight;

	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&ButtonLeft, &ButtonRight, MarginSmall);
	if(DoButtonLineSize_Menu(&s_DiscordButton, RCLocalize("Discord"), 0, &ButtonLeft, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://discord.gg/wUFTVAGVGa");
	if(DoButtonLineSize_Menu(&s_WebsiteButton, RCLocalize("Website"), 0, &ButtonRight, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink(CRClient::RCLIENT_URL);

	LeftView = LowerLeftView;
	LeftView.HSplitBottom(LineSize * 4.0f + MarginSmall * 2.0f + HeadlineFontSize, nullptr, &LeftView);
	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, RCLocalize("Config Files"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	char aBuf[128 + IO_MAX_PATH_LENGTH];
	CUIRect TClientConfig, ProfilesFile;

	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&TClientConfig, &ProfilesFile, MarginSmall);

	static CButtonContainer s_Config;
	if(DoButtonLineSize_Menu(&s_Config, RCLocalize("RClient Settings"), 0, &TClientConfig, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::RCLIENT].m_aConfigPath, aBuf, sizeof(aBuf));
		Client()->ViewFile(aBuf);
	}

	// =======RIGHT VIEW========

	RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	Ui()->DoLabel(&Label, RCLocalize("RClient Developer"), HeadlineFontSize, TEXTALIGN_ML);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);

	const float TeeSize = 50.0f;
	const float CardSize = TeeSize + MarginSmall;
	CUIRect TeeRect, DevCardRect;
	{
		RightView.HSplitTop(CardSize, &DevCardRect, &RightView);
		DevCardRect.VSplitLeft(CardSize, &TeeRect, &Label);
		Label.VSplitLeft(TextRender()->TextWidth(LineSize, "Voix"), &Label, &Button);
		Button.VSplitLeft(MarginSmall, nullptr, &Button);
		Button.w = LineSize, Button.h = LineSize, Button.y = Label.y + (Label.h / 2.0f - Button.h / 2.0f);
		Ui()->DoLabel(&Label, "Voix", LineSize, TEXTALIGN_ML);
		RenderDevSkin(TeeRect.Center(), 50.0f, "Bomb 2", "bomb", false, 0, 0, 0, false, true);
	}

	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	Ui()->DoLabel(&Label, RCLocalize("Hide Settings Tabs"), HeadlineFontSize, TEXTALIGN_ML);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	CUIRect LeftSettings, RightSettings;

	RightView.VSplitMid(&LeftSettings, &RightSettings, MarginSmall);
	RightView.HSplitTop(LineSize * 3.5f, nullptr, &RightView);

	static int s_ShowSettings = IsFlagSet(g_Config.m_RiRClientSettingsTabs, RCLIENT_TAB_SETTINGS);
	DoButton_CheckBoxAutoVMarginAndSet(&s_ShowSettings, RCLocalize("Settings"), &s_ShowSettings, &LeftSettings, LineSize);
	SetFlag(g_Config.m_RiRClientSettingsTabs, RCLIENT_TAB_SETTINGS, s_ShowSettings);
	static int s_ShowBindWheel = IsFlagSet(g_Config.m_RiRClientSettingsTabs, RCLIENT_TAB_BINDWHEEL);
	DoButton_CheckBoxAutoVMarginAndSet(&s_ShowBindWheel, RCLocalize("Bindwheel"), &s_ShowBindWheel, &RightSettings, LineSize);
	SetFlag(g_Config.m_RiRClientSettingsTabs, RCLIENT_TAB_BINDWHEEL, s_ShowBindWheel);
	static int s_ShowNameplatesEditor = IsFlagSet(g_Config.m_RiRClientSettingsTabs, RCLIENT_TAB_NAMEPLATES_EDITOR);
	DoButton_CheckBoxAutoVMarginAndSet(&s_ShowNameplatesEditor, RCLocalize("Nameplate editor"), &s_ShowNameplatesEditor, &LeftSettings, LineSize);
	SetFlag(g_Config.m_RiRClientSettingsTabs, RCLIENT_TAB_NAMEPLATES_EDITOR, s_ShowNameplatesEditor);
	static int s_ShowVoice = IsFlagSet(g_Config.m_RiRClientSettingsTabs, RCLIENT_TAB_VOICE);
	DoButton_CheckBoxAutoVMarginAndSet(&s_ShowVoice, RCLocalize("Voice mix"), &s_ShowVoice, &RightSettings, LineSize);
	SetFlag(g_Config.m_RiRClientSettingsTabs, RCLIENT_TAB_VOICE, s_ShowVoice);
}

void CMenus::RenderSettingsRushieSettings(CUIRect MainView)
{
	#define MACRO_CONFIG_CHECKBOX(Name, Desc) \
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_##Name, RCLocalize(Desc), &g_Config.m_##Name, &Column, LineSize); \
		Column.HSplitTop(MarginSmall, nullptr, &Column);

	static CScrollRegion s_ScrollRegion;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 120.0f;
	ScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	s_ScrollRegion.Begin(&MainView, &ScrollOffset, &ScrollParams);

	static std::vector<CUIRect> s_SectionBoxes;
	static vec2 s_PrevScrollOffset(0.0f, 0.0f);

	MainView.y += ScrollOffset.y;

	// Add padding for scrollbar
	MainView.VSplitRight(5.0f, &MainView, nullptr);
	MainView.VSplitLeft(5.0f, nullptr, &MainView);

	CUIRect LeftView, RightView, Button, Label;

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	for(CUIRect &Section : s_SectionBoxes)
	{
		float Padding = MarginBetweenViews * 0.6666f;
		Section.w += Padding;
		Section.h += Padding;
		Section.x -= Padding * 0.5f;
		Section.y -= Padding * 0.5f;
		Section.y -= s_PrevScrollOffset.y - ScrollOffset.y;
		Section.Draw(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_RiMenusSettingsColor, true)), IGraphics::CORNER_ALL, 10.0f);
	}
	s_PrevScrollOffset = ScrollOffset;
	s_SectionBoxes.clear();

	auto BeginSection = [&](CUIRect &Column, float TopMargin) {
		Column.HSplitTop(TopMargin, nullptr, &Column);
		s_SectionBoxes.push_back(Column);
	};

	auto EndSection = [&](CUIRect &Column) {
		s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
	};

	static bool s_aSectionExpanded[NUM_RCLIENT_SETTINGS_SECTIONS] = {
		true, true, true, true, true, true, true, true, true, true,
		true, true, true, true, true, true, true, true, true};
	static CButtonContainer s_aSectionExpandButtons[NUM_RCLIENT_SETTINGS_SECTIONS];

	auto BeginSectionHeader = [&](CUIRect &Column, float TopMargin, int SectionId, const char *pTitle) {
		BeginSection(Column, TopMargin);
		CUIRect Header;
		Column.HSplitTop(HeadlineHeight, &Header, &Column);

		CUIRect ButtonArea;
		Header.Margin(-MarginSmall, &ButtonArea);
		if(Ui()->DoButtonLogic(&s_aSectionExpandButtons[SectionId], 0, &ButtonArea, BUTTONFLAG_LEFT))
		{
			s_aSectionExpanded[SectionId] = !s_aSectionExpanded[SectionId];
		}

		CUIRect ExpandButton;
		Header.VSplitRight(20.0f, &Header, &ExpandButton);
		Header.VSplitRight(MarginSmall, &Header, nullptr);

		SLabelProperties Props;
		Props.SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.65f * Ui()->ButtonColorMul(&s_aSectionExpandButtons[SectionId])));
		Props.m_EnableWidthCheck = false;
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		Ui()->DoLabel(&ExpandButton, s_aSectionExpanded[SectionId] ? FontIcon::CHEVRON_UP : FontIcon::CHEVRON_DOWN, HeadlineFontSize, TEXTALIGN_MR, Props);
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

		Ui()->DoLabel(&Header, pTitle, HeadlineFontSize, TEXTALIGN_ML);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		return s_aSectionExpanded[SectionId];
	};

	auto DoBindchatDefault = [&](CUIRect &Column, CBindChat::CBindRclient &BindDefault) {
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		CBindChat::CBind *pOldBind = GameClient()->m_BindChat.GetBind(BindDefault.m_Bind.m_aCommand);
		static char s_aTempName[BINDCHAT_MAX_NAME] = "";
		char *pName;
		if(pOldBind == nullptr)
			pName = s_aTempName;
		else
			pName = pOldBind->m_aName;
		if(DoEditBoxWithLabel(&BindDefault.m_LineInput, &Button, RCLocalize(BindDefault.m_pTitle, "Chatbinds"), BindDefault.m_Bind.m_aName, pName, BINDCHAT_MAX_NAME) && BindDefault.m_LineInput.IsActive())
		{
			if(!pOldBind && pName[0] != '\0')
			{
				auto BindNew = BindDefault.m_Bind;
				str_copy(BindNew.m_aName, pName);
				GameClient()->m_BindChat.RemoveBind(pName); // Prevent duplicates
				GameClient()->m_BindChat.AddBind(BindNew);
				s_aTempName[0] = '\0';
			}
			if(pOldBind && pName[0] == '\0')
			{
				GameClient()->m_BindChat.RemoveBind(pName);
			}
		}
	};

	// Split view into two columns
	CUIRect Column = LeftView;

	// ***** Auto Change Player Info ***** //
	BeginSectionHeader(Column, Margin, RCLIENT_SETTINGS_SECTION_AUTO_CHANGE_PLAYER_INFO, RCLocalize("Auto Change Player Info"));
	if(s_aSectionExpanded[RCLIENT_SETTINGS_SECTION_AUTO_CHANGE_PLAYER_INFO])
	{
		for(CBindChat::CBindRclient &BindchatDefault : s_aDefaultBindChatRclientFindSkin)
			DoBindchatDefault(Column, BindchatDefault);

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		static CButtonContainer s_FindSkinChatButton;
		if(DoButtonLineSize_Menu(&s_FindSkinChatButton, RCLocalize("Reset Find/Copy Skin Chatbinds"), 0, &Button, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.5f, 0.0f, 0.0f, 0.25f)))
		{
			for(const CBindChat::CBindRclient &BindDefault : s_aDefaultBindChatRclientFindSkinHistory)
			{
				GameClient()->m_BindChat.RemoveBindCommand(BindDefault.m_Bind.m_aCommand);
			}
			for(const CBindChat::CBindRclient &BindDefault : s_aDefaultBindChatRclientFindSkin)
			{
				GameClient()->m_BindChat.RemoveBindCommand(BindDefault.m_Bind.m_aCommand);
				GameClient()->m_BindChat.AddBind(BindDefault.m_Bind);
			}
		}
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_PlayerClanAutoChange, RCLocalize("Auto change clan when dummy connect"), &g_Config.m_PlayerClanAutoChange, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		if(g_Config.m_PlayerClanAutoChange)
		{
			static CLineInput s_WithDummy;
			static CLineInput s_WithoutDummy;

			s_WithDummy.SetBuffer(g_Config.m_PlayerClanWithDummy, sizeof(g_Config.m_PlayerClanWithDummy));
			s_WithoutDummy.SetBuffer(g_Config.m_PlayerClanNoDummy, sizeof(g_Config.m_PlayerClanNoDummy));

			// player clan with dummy
			Column.HSplitTop(LineSize, &Label, &Column);
			DoEditBoxWithLabel(&s_WithDummy, &Label, RCLocalize("Clan with dummy:"), "", g_Config.m_PlayerClanWithDummy, sizeof(g_Config.m_PlayerClanWithDummy));
			Column.HSplitTop(MarginSmall, nullptr, &Column);

			// player clan without dummy
			Column.HSplitTop(LineSize, &Label, &Column);
			DoEditBoxWithLabel(&s_WithoutDummy, &Label, RCLocalize("Clan without dummy:"), "", g_Config.m_PlayerClanNoDummy, sizeof(g_Config.m_PlayerClanNoDummy));
			Column.HSplitTop(MarginSmall, nullptr, &Column);
		}
		else
		{
			Column.HSplitTop(LineSize, nullptr, &Column);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, nullptr, &Column);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
		}
		Column.HSplitTop(LineSize, nullptr, &Column);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
	}
	EndSection(Column);

	// ***** Chat Functions ***** //
	BeginSectionHeader(Column, MarginBetweenSections, RCLIENT_SETTINGS_SECTION_CHAT_FUNCTIONS, RCLocalize("Chat Functions"));
	if(s_aSectionExpanded[RCLIENT_SETTINGS_SECTION_CHAT_FUNCTIONS])
	{
		for(CBindChat::CBindRclient &BindchatDefault : s_aDefaultBindChatRclientChat)
			DoBindchatDefault(Column, BindchatDefault);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		static CButtonContainer s_ChatChatButton;
		if(DoButtonLineSize_Menu(&s_ChatChatButton, RCLocalize("Reset Chat Chatbinds"), 0, &Button, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.5f, 0.0f, 0.0f, 0.25f)))
		{
			for(const CBindChat::CBindRclient &BindDefault : s_aDefaultBindChatRclientChat)
			{
				GameClient()->m_BindChat.RemoveBindCommand(BindDefault.m_Bind.m_aCommand);
				GameClient()->m_BindChat.AddBind(BindDefault.m_Bind);
			}
		}
		Column.HSplitTop(MarginSmall, nullptr, &Column);
	}
	EndSection(Column);

	// ***** Block List ***** //
	BeginSectionHeader(Column, MarginBetweenSections, RCLIENT_SETTINGS_SECTION_BLOCK_LIST, RCLocalize("Block list"));
	if(s_aSectionExpanded[RCLIENT_SETTINGS_SECTION_BLOCK_LIST])
	{
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiShowBlockedWordInConsole, RCLocalize("Show blocked word in console"), &g_Config.m_RiShowBlockedWordInConsole, &Column, LineSize);
		GameClient()->m_Tooltips.DoToolTip(&g_Config.m_RiShowBlockedWordInConsole, &Column, RCLocalize("In console will be like 'tee said badbad'"));
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		if(g_Config.m_RiShowBlockedWordInConsole)
		{
			CUIRect ColorRect;
			Column.HSplitTop(LineSize, &ColorRect, &Column);
			ColorRect.VSplitLeft(160.0f, &Label, &ColorRect); // Use fixed width instead of undefined ColumnWidth
			Ui()->DoLabel(&Label, RCLocalize("Blocked words console color"), FontSize, TEXTALIGN_MC);
			static CButtonContainer s_ColorPickerButton;
			ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_RiBlockedWordConsoleColor));
			ColorRect.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f * Ui()->ButtonColorMul(&s_ColorPickerButton)), IGraphics::CORNER_ALL, 5.0f);
			CUIRect ColorRectInner = ColorRect;
			ColorRectInner.Margin(1.5f, &ColorRectInner);
			ColorRectInner.Draw(Color, IGraphics::CORNER_ALL, 3.0f);

			if(Ui()->DoButtonLogic(&s_ColorPickerButton, 0, &ColorRect, BUTTONFLAG_LEFT))
			{
				m_ColorPickerPopupContext.m_Alpha = false;
				m_ColorPickerPopupContext.m_pHslaColor = &g_Config.m_RiBlockedWordConsoleColor;
				m_ColorPickerPopupContext.m_HslaColor = ColorHSLA(g_Config.m_RiBlockedWordConsoleColor);
				m_ColorPickerPopupContext.m_HsvaColor = color_cast<ColorHSVA>(m_ColorPickerPopupContext.m_HslaColor);
				m_ColorPickerPopupContext.m_RgbaColor = color_cast<ColorRGBA>(m_ColorPickerPopupContext.m_HslaColor);
				Ui()->ShowPopupColorPicker(Ui()->MouseX(), Ui()->MouseY(), &m_ColorPickerPopupContext);
			}
			Column.HSplitTop(MarginSmall, nullptr, &Column);
		}
		else
		{
			Column.HSplitTop(LineSize, nullptr, &Column);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
		}
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiEnableCensorList, RCLocalize("Enable word block list"), &g_Config.m_RiEnableCensorList, &Column, LineSize);
		GameClient()->m_Tooltips.DoToolTip(&g_Config.m_RiEnableCensorList, &Column, RCLocalize("Replacing blocked word with replacement char(badbad->******)"));
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiMultipleReplacementChar, RCLocalize("Multiple replacement char on blocked word len"), &g_Config.m_RiMultipleReplacementChar, &Column, LineSize);
		GameClient()->m_Tooltips.DoToolTip(&g_Config.m_RiMultipleReplacementChar, &Column, RCLocalize("if no will be 'badbad->*' if yes 'badbad->******'"));
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		static CLineInput s_ReplacementChar;
		s_ReplacementChar.SetBuffer(g_Config.m_RiBlockedContentReplacementChar, sizeof(g_Config.m_RiBlockedContentReplacementChar));
		Column.HSplitTop(LineSize, &Label, &Column);
		DoEditBoxWithLabel(&s_ReplacementChar, &Label, RCLocalize("Replacement char"), "*", g_Config.m_RiBlockedContentReplacementChar, sizeof(g_Config.m_RiBlockedContentReplacementChar));
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		static std::vector<CButtonContainer> s_vButtonContainers = {{}, {}, {}};
		DoLine_RadioMenu(Column, RCLocalize("Replace word with:", "Rclientfilter"),
			s_vButtonContainers,
			{RCLocalize("Regex", "Rclientfilter"), RCLocalize("Full", "Rclientfilter"), RCLocalize("Both", "Rclientfilter")},
			{0, 1, 2},
			g_Config.m_RiFilterChangeWholeWord);
		if(g_Config.m_RiFilterChangeWholeWord == 2)
		{
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			static CLineInput s_PartialReplacementChar;
			s_PartialReplacementChar.SetBuffer(g_Config.m_RiBlockedContentPartialReplacementChar, sizeof(g_Config.m_RiBlockedContentPartialReplacementChar));
			Column.HSplitTop(LineSize, &Label, &Column);
			DoEditBoxWithLabel(&s_PartialReplacementChar, &Label, RCLocalize("Partial Replacement char"), "*", g_Config.m_RiBlockedContentPartialReplacementChar, sizeof(g_Config.m_RiBlockedContentPartialReplacementChar));
		}
		else
		{
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, nullptr, &Column);
		}
		Column.HSplitTop(MarginSmall, nullptr, &Column);
	}
	EndSection(Column);

	// ***** Chat ***** //
	BeginSectionHeader(Column, MarginBetweenSections, RCLIENT_SETTINGS_SECTION_CHAT, RCLocalize("Chat"));
	if(s_aSectionExpanded[RCLIENT_SETTINGS_SECTION_CHAT])
	{
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiChatAnim, RCLocalize("Animate chat"), &g_Config.m_RiChatAnim, &Column, LineSize);
		if(g_Config.m_RiChatAnim)
		{
			Column.HSplitTop(20.0f, &Label, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RiChatAnimMs, &g_Config.m_RiChatAnimMs, &Label, RCLocalize("Anim chat ms"), 100, 2000, &CUi::ms_LogarithmicScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE);
		}
		else
		{
			Column.HSplitTop(20.0f, nullptr, &Column);
		}
		Column.HSplitTop(MarginSmall, nullptr, &Column);
	}
	EndSection(Column);

	// ***** Scoreboard ***** //
	BeginSectionHeader(Column, MarginBetweenSections, RCLIENT_SETTINGS_SECTION_SCOREBOARD, RCLocalize("Scoreboard"));
	if(s_aSectionExpanded[RCLIENT_SETTINGS_SECTION_SCOREBOARD])
	{
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiScoreboardFriendMark, RCLocalize("Show friend icon in scoreboard"), &g_Config.m_RiScoreboardFriendMark, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiScoreboardAlwaysShowQuickActions, RCLocalize("Always show quick actions"), &g_Config.m_RiScoreboardAlwaysShowQuickActions, &Column, LineSize);
		static std::vector<CButtonContainer> s_vScoreboardSortButtonContainers = {{}, {}, {}};
		DoLine_RadioMenu(Column, RCLocalize("Sort by id:", "ScoreboardSorting"),
			s_vScoreboardSortButtonContainers,
			{RCLocalize("Default", "ScoreboardSorting"), RCLocalize("Teams", "ScoreboardSorting"), RCLocalize("All", "ScoreboardSorting")},
			{0, 1, 2},
			g_Config.m_RiScoreboardSortById);
	}
	EndSection(Column);

	// ***** Changed Tater ***** //
	BeginSectionHeader(Column, MarginBetweenSections, RCLIENT_SETTINGS_SECTION_CHANGED_TATER, RCLocalize("Changed Tater"));
	if(s_aSectionExpanded[RCLIENT_SETTINGS_SECTION_CHANGED_TATER])
	{
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_RiFrozenHudPosX, &g_Config.m_RiFrozenHudPosX, &Button, RCLocalize("Pos x of Frozen hud"), 0, 100);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiFrozenHudWarlistTeamOnly, RCLocalize("Show frozen HUD only for warlist teammates"), &g_Config.m_RiFrozenHudWarlistTeamOnly, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiIndicatorTransparentToggle, RCLocalize("Player indicator transparent toggle"), &g_Config.m_RiIndicatorTransparentToggle, &Column, LineSize);
		GameClient()->m_Tooltips.DoToolTip(&g_Config.m_RiIndicatorTransparentToggle, &Column, RCLocalize("if you move away from the tee, the indicator will become more transparent"));
		if(g_Config.m_RiIndicatorTransparentToggle)
		{
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RiIndicatorTransparentOffset, &g_Config.m_RiIndicatorTransparentOffset, &Button, RCLocalize("Indicator transparent start"), 16, 200);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RiIndicatorTransparentOffsetMax, &g_Config.m_RiIndicatorTransparentOffsetMax, &Button, RCLocalize("Indicator transparent end"), 16, 200);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RiIndicatorTransparentMin, &g_Config.m_RiIndicatorTransparentMin, &Button, RCLocalize("Indicator transparent minimum"), 0, 100);
		}
		else
		{
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, nullptr, &Column);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, nullptr, &Column);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, nullptr, &Column);
		}
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiColorFreezeNoYourself, RCLocalize("No colored frozen tee skins for yourself"), &g_Config.m_RiColorFreezeNoYourself, &Column, LineSize);
		static std::vector<CButtonContainer> s_vFastInputVerButtonContainers = {{}, {}};
		DoLine_RadioMenu(Column, RCLocalize("FastInput version:", ""),
			s_vFastInputVerButtonContainers,
			{RCLocalize("Tater's old", ""), RCLocalize("Tater's new", "")},
			{0, 1},
			g_Config.m_RiFastInputVersion);
	}
	EndSection(Column);

	// ***** Nameplates ***** //
	BeginSectionHeader(Column, MarginBetweenSections, RCLIENT_SETTINGS_SECTION_NAMEPLATES, RCLocalize("Nameplates", "RClient"));
	if(s_aSectionExpanded[RCLIENT_SETTINGS_SECTION_NAMEPLATES])
	{
		Column.HSplitTop(20.0f, &Label, &Column);
		Ui()->DoLabel(&Label, RCLocalize("Nameplate Scheme"), 14.0f, TEXTALIGN_ML);
		Column.HSplitTop(5.0f, nullptr, &Column);
		Column.HSplitTop(20.0f, &Button, &Column);
		static CLineInput s_NamePlateScheme(g_Config.m_RiNamePlateScheme, sizeof(g_Config.m_RiNamePlateScheme));
		if(Ui()->DoEditBox(&s_NamePlateScheme, &Button, FontSize))
		{
			GameClient()->m_NamePlates.RiResetNameplatesPos(*GameClient(), g_Config.m_RiNamePlateScheme);
		}
		Column.HSplitTop(5.0f, nullptr, &Column);
		Column.HSplitTop(20.0f, &Label, &Column);
		Ui()->DoLabel(&Label, RCLocalize("p=ping i=ignore m=ID n=name c=clan d=direction f=friend h=hook r=reason s=skin H=HookName F=FireName l=newline"), 10.0f, TEXTALIGN_ML);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoLine_RadioMenu(Column, RCLocalize("Show you' fire presses"),
			m_vButtonContainersNamePlateFirePresses,
			{Localize("None", "Show players' key presses"), Localize("Own", "Show players' key presses RC"), Localize("Dummy", "Show players' key presses"), Localize("Both", "Show players' key presses")},
			{0, 3, 1, 2},
			g_Config.m_RiShowFire);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		if(g_Config.m_RiShowFire > 0)
		{
			Column.HSplitTop(20.0f, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RiFireDetectionSize, &g_Config.m_RiFireDetectionSize, &Button, Localize("Size of fire press icons"), -50, 100);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiShowFireDynamic, RCLocalize("Fire will change pos when some nearby"), &g_Config.m_RiShowFireDynamic, &Column, LineSize);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
		}
		else
		{
			Column.HSplitTop(20.0f, nullptr, &Column);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, nullptr, &Column);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
		}
		DoLine_RadioMenu(Column, RCLocalize("Show players' hook presses"),
			m_vButtonContainersNamePlateHookPresses,
			{Localize("None", "Show players' key presses"), Localize("Own", "Show players' key presses RC"), Localize("Others", "Show players' key presses"), Localize("All", "Show players' key presses")},
			{0, 3, 1, 2},
			g_Config.m_RiShowHook);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		if(g_Config.m_RiShowHook > 0)
		{
			Column.HSplitTop(20.0f, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RiHookDetectionSize, &g_Config.m_RiHookDetectionSize, &Button, Localize("Size of hook press icons"), -50, 100);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiShowHookDynamic, RCLocalize("Hook will change pos when some nearby"), &g_Config.m_RiShowHookDynamic, &Column, LineSize);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
		}
		else
		{
			Column.HSplitTop(20.0f, nullptr, &Column);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, nullptr, &Column);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
		}
		Column.HSplitTop(MarginSmall, nullptr, &Column);
	}
	EndSection(Column);

	// ***** Dummy ***** //
	BeginSectionHeader(Column, MarginBetweenSections, RCLIENT_SETTINGS_SECTION_DUMMY, RCLocalize("Dummy", "RClient"));
	if(s_aSectionExpanded[RCLIENT_SETTINGS_SECTION_DUMMY])
	{
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiAdvancedShowhudDummyActions, RCLocalize("Show Advanced Dummy Actions"), &g_Config.m_RiAdvancedShowhudDummyActions, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiShowhudDummyPosition, TCLocalize("Show Dummy position"), &g_Config.m_RiShowhudDummyPosition, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		if(g_Config.m_RiShowhudDummyPosition)
		{
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiShowLastPosHudDummy, TCLocalize("Show last known pos instead no info"), &g_Config.m_RiShowLastPosHudDummy, &Column, LineSize);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiChangeDummyColorWhenXDummyEqualXPlayer, TCLocalize("Change dummy pos x color when x dummy = x player"), &g_Config.m_RiChangeDummyColorWhenXDummyEqualXPlayer, &Column, LineSize);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			if(g_Config.m_RiChangeDummyColorWhenXDummyEqualXPlayer)
			{
				CUIRect Rightoffset;
				Column.VSplitLeft(25.0f, &Label, &Rightoffset);
				Column.HSplitTop(LineSize, nullptr, &Column);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiChangePlayerColorWhenXDummyEqualXPlayer, TCLocalize("Change player pos x color when x dummy = x player"), &g_Config.m_RiChangePlayerColorWhenXDummyEqualXPlayer, &Rightoffset, LineSize);
				Column.HSplitTop(MarginSmall, nullptr, &Column);
			}
		}
		else
		{
			Column.HSplitTop(LineSize, nullptr, &Column);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, nullptr, &Column);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, nullptr, &Column);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
		}
		if(g_Config.m_RiShowhudDummyPosition && !g_Config.m_RiChangeDummyColorWhenXDummyEqualXPlayer)
		{
			Column.HSplitTop(LineSize, nullptr, &Column);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
		}
		Column.HSplitTop(MarginSmall, nullptr, &Column);
	}
	EndSection(Column);

	// ***** Effects ***** //
	BeginSectionHeader(Column, MarginBetweenSections, RCLIENT_SETTINGS_SECTION_EFFECTS, RCLocalize("Effects"));
	if(s_aSectionExpanded[RCLIENT_SETTINGS_SECTION_EFFECTS])
	{
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiShowSparkleTrail, RCLocalize("Show sparkle trail"), &g_Config.m_RiShowSparkleTrail, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiShowFrozenFlakes, RCLocalize("Show frozen flakes in freeze"), &g_Config.m_RiShowFrozenFlakes, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiShowAfkEmoteInMenu, RCLocalize("Show sleep emote in menu (ONLY CLIENT OTHER DON'T SEE THAT)"), &g_Config.m_RiShowAfkEmoteInMenu, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		if(g_Config.m_RiShowAfkEmoteInMenu)
		{
			CUIRect Rightoffset;
			Column.VSplitLeft(25.0f, &Label, &Rightoffset);
			Column.HSplitTop(LineSize, nullptr, &Column);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiShowAfkTextureInMenu, RCLocalize("Show afk texture instead emote"), &g_Config.m_RiShowAfkTextureInMenu, &Rightoffset, LineSize);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
		}
		else
		{
			Column.HSplitTop(LineSize, nullptr, &Column);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
		}
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiShowAfkEmoteInSpec, RCLocalize("Show sleep emote in spec (ONLY CLIENT OTHER DON'T SEE THAT)"), &g_Config.m_RiShowAfkEmoteInSpec, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		if(g_Config.m_RiShowAfkEmoteInSpec)
		{
			CUIRect Rightoffset;
			Column.VSplitLeft(25.0f, &Label, &Rightoffset);
			Column.HSplitTop(LineSize, nullptr, &Column);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiShowAfkTextureInSpec, RCLocalize("Show spec texture instead emote"), &g_Config.m_RiShowAfkTextureInSpec, &Rightoffset, LineSize);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
		}
		else
		{
			Column.HSplitTop(LineSize, nullptr, &Column);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
		}
		static std::vector<CButtonContainer> s_vHammerHitEffectsButtonContainers = {{}, {}, {}};
		DoLine_RadioMenu(Column, RCLocalize("Show Hammer Hit:", "HammerHit"),
			s_vHammerHitEffectsButtonContainers,
			{RCLocalize("No effect", "HammerHit"), RCLocalize("Normal", "HammerHit"), RCLocalize("No Sound", "HammerHit")},
			{0, 1, 2},
			g_Config.m_RiShowHammerHit);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiPlayOnMoveNonInactive, RCLocalize("Play sound on move when window inactive"), &g_Config.m_RiPlayOnMoveNonInactive, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		static SDropDownSimple s_DropSoundChoose;
		if(g_Config.m_RiPlayOnMoveNonInactive)
		{
			g_Config.m_RiSoundOnMoveNonInactive = DoSimpleDropDown(
				Ui(),
				Column,
				RCLocalize("Choose sound:"),
				g_Config.m_RiSoundOnMoveNonInactive,
				{"WakeUp", "Grenade", "Tag"},
				"My setting",
				s_DropSoundChoose);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
		}
		else
		{
			Column.HSplitTop(LineSize, nullptr, &Column);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
		}
	}
	EndSection(Column);

	BeginSectionHeader(Column, MarginBetweenSections, RCLIENT_SETTINGS_SECTION_MENUS, RCLocalize("Menu"));
	if(s_aSectionExpanded[RCLIENT_SETTINGS_SECTION_MENUS])
	{
		MACRO_CONFIG_CHECKBOX(RiUiNewMenu, "Show new RClient's menu");
		MACRO_CONFIG_CHECKBOX(RiUiCustomBg, "custom RClient's menu background");
		MACRO_CONFIG_CHECKBOX(RiUiShowTopBar, "show RClient's menu topbar");
		MACRO_CONFIG_CHECKBOX(RiUiShowBottomBar, "show RClient's menu bottombar");
		static CButtonContainer s_MenuColor;
		Column.HSplitTop(LineSize, &Button, &Column);
		DoLine_ColorPicker(&s_MenuColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Button, RCLocalize("Color of settings menu"), &g_Config.m_RiMenusSettingsColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::RiMenusSettingsColor, true)), false, nullptr, true);
	}
	EndSection(Column);

	// Right column - Tracker pos
	LeftView = Column;
	Column = RightView;

	// ***** Tracker Player ***** //
	BeginSectionHeader(Column, Margin, RCLIENT_SETTINGS_SECTION_TRACKER_PLAYER, RCLocalize("Tracker Player"));
	if(s_aSectionExpanded[RCLIENT_SETTINGS_SECTION_TRACKER_PLAYER])
	{
		for(CBindChat::CBindRclient &BindchatDefault : s_aDefaultBindChatRclientTracker)
			DoBindchatDefault(Column, BindchatDefault);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		static CButtonContainer s_TrackerChatButton;
		if(DoButtonLineSize_Menu(&s_TrackerChatButton, RCLocalize("Reset Tracker Chatbinds"), 0, &Button, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.5f, 0.0f, 0.0f, 0.25f)))
		{
			for(const CBindChat::CBindRclient &BindDefault : s_aDefaultBindChatRclientTrackerHistory)
			{
				GameClient()->m_BindChat.RemoveBindCommand(BindDefault.m_Bind.m_aCommand);
			}
			for(const CBindChat::CBindRclient &BindDefault : s_aDefaultBindChatRclientTracker)
			{
				GameClient()->m_BindChat.RemoveBindCommand(BindDefault.m_Bind.m_aCommand);
				GameClient()->m_BindChat.AddBind(BindDefault.m_Bind);
			}
		}
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiShowLastPosHud, RCLocalize("Show last known pos instead no info"), &g_Config.m_RiShowLastPosHud, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiChangeTargetColorWhenXTargetEqualXPlayer, RCLocalize("Change target pos x color when x target = x player"), &g_Config.m_RiChangeTargetColorWhenXTargetEqualXPlayer, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		if(g_Config.m_RiChangeTargetColorWhenXTargetEqualXPlayer)
		{
			CUIRect Rightoffset;
			Column.VSplitLeft(25.0f, &Label, &Rightoffset);
			Column.HSplitTop(LineSize, nullptr, &Column);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiChangePlayerColorWhenXTargetEqualXPlayer, RCLocalize("Change player pos x color when x target = x player"), &g_Config.m_RiChangePlayerColorWhenXTargetEqualXPlayer, &Rightoffset, LineSize);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
		}
		else
		{
			Column.HSplitTop(LineSize, nullptr, &Column);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
		}
		Column.HSplitTop(MarginSmall, nullptr, &Column);
	}
	EndSection(Column);

	// ***** Hud ***** //
	BeginSectionHeader(Column, MarginBetweenSections, RCLIENT_SETTINGS_SECTION_HUD, RCLocalize("Hud"));
	if(s_aSectionExpanded[RCLIENT_SETTINGS_SECTION_HUD])
	{
		Column.HSplitTop(20.0f, &Label, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_RiHeartSize, &g_Config.m_RiHeartSize, &Label, RCLocalize("Friend heart size"), 0, 500, &CUi::ms_LogarithmicScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiShowMilliSecondsTimer, RCLocalize("Show milliseconds in timer"), &g_Config.m_RiShowMilliSecondsTimer, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
	}
	EndSection(Column);

	// ***** Controls ***** //
	BeginSectionHeader(Column, MarginBetweenSections, RCLIENT_SETTINGS_SECTION_CONTROLS, RCLocalize("Controls"));
	if(s_aSectionExpanded[RCLIENT_SETTINGS_SECTION_CONTROLS])
	{
		static CButtonContainer s_ReaderButtonDummyPseudo, s_ClearButtonDummyPseudo,
			s_ReaderButtonDeepfly, s_ClearButtonDeepfly,
			s_ReaderButtonDeepflyToggle, s_ClearButtonDeepflyToggle,
			s_ReaderButton45Degrees, s_ClearButton45Degrees,
			s_ReaderButtonSmallSens, s_ClearButtonSmallSens,
			s_ReaderButtonLeftJump, s_ClearButtonLeftJump,
			s_ReaderButtonRightJump, s_ClearButtonRightJump;
		Column.HSplitTop(LineSize, &Label, &Column);
		DoLine_KeyReader(Label, s_ReaderButtonDummyPseudo, s_ClearButtonDummyPseudo, RCLocalize("Dummy pseudo"), "+toggle cl_dummy_hammer 1 0");
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Label, &Column);
		DoLine_KeyReader(Label, s_ReaderButtonDeepfly, s_ClearButtonDeepfly, RCLocalize("Deepfly"), "+fire;+toggle cl_dummy_hammer 1 0");
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Label, &Column);
		DoLine_KeyReader(Label, s_ReaderButtonDeepflyToggle, s_ClearButtonDeepflyToggle, RCLocalize("Deepfly toggle"), "ri_deepfly_toggle");
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Label, &Column);
		DoLine_KeyReader(Label, s_ReaderButton45Degrees, s_ClearButton45Degrees, RCLocalize("45° bind"), "+ri_45_degrees");
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		{
			CUIRect Rightoffset;
			Column.VSplitLeft(25.0f, &Label, &Rightoffset);
			Column.HSplitTop(LineSize, nullptr, &Column);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiToggle45degrees, RCLocalize("Toggle 45 degrees"), &g_Config.m_RiToggle45degrees, &Rightoffset, LineSize);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			Column.VSplitLeft(25.0f, &Label, &Rightoffset);
			Column.HSplitTop(LineSize, nullptr, &Column);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_Ri45degreesEcho, RCLocalize("Echo 45 degrees"), &g_Config.m_Ri45degreesEcho, &Rightoffset, LineSize);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
		}
		Column.HSplitTop(LineSize, &Label, &Column);
		DoLine_KeyReader(Label, s_ReaderButtonSmallSens, s_ClearButtonSmallSens, RCLocalize("Small sens bind"), "+ri_small_sens");
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		{
			CUIRect Rightoffset;
			Column.VSplitLeft(25.0f, &Label, &Rightoffset);
			Column.HSplitTop(LineSize, nullptr, &Column);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiToggleSmallSens, RCLocalize("Toggle small sens"), &g_Config.m_RiToggleSmallSens, &Rightoffset, LineSize);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			Column.VSplitLeft(25.0f, &Label, &Rightoffset);
			Column.HSplitTop(LineSize, nullptr, &Column);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiSmallSensEcho, RCLocalize("Echo small sens"), &g_Config.m_RiSmallSensEcho, &Rightoffset, LineSize);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
		}
		Column.HSplitTop(LineSize, &Label, &Column);
		DoLine_KeyReader(Label, s_ReaderButtonLeftJump, s_ClearButtonLeftJump, RCLocalize("Left jump"), "+jump; +left");
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Label, &Column);
		DoLine_KeyReader(Label, s_ReaderButtonRightJump, s_ClearButtonRightJump, RCLocalize("Right jump"), "+jump; +right");
		Column.HSplitTop(MarginSmall, nullptr, &Column);
	}
	EndSection(Column);

	// ***** Laser Settings ***** //
	BeginSectionHeader(Column, MarginBetweenSections, RCLIENT_SETTINGS_SECTION_LASER, RCLocalize("Laser Settings(Pulse)"));
	if(s_aSectionExpanded[RCLIENT_SETTINGS_SECTION_LASER])
	{
		Column.HSplitTop(10.0f, nullptr, &Column);

		Column.HSplitTop(20.0f, &Button, &Column);
		if(DoButton_CheckBox(&g_Config.m_RiBetterLasers, RCLocalize("Enhanced Laser Effects"), g_Config.m_RiBetterLasers, &Button))
			g_Config.m_RiBetterLasers ^= 1;

		if(g_Config.m_RiBetterLasers)
		{
			Column.HSplitTop(20.0f, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RiLaserGlowIntensity, &g_Config.m_RiLaserGlowIntensity, &Button, RCLocalize("Laser Glow Intensity"), 30, 100);

			Column.HSplitTop(20.0f, &Label, &Column);
			Ui()->DoLabel(&Label, RCLocalize("Laser Preview"), 16.0f, TEXTALIGN_ML);
			Column.HSplitTop(10.0f, nullptr, &Column);

			const float LaserPreviewHeight = 50.0f;
			CUIRect LaserPreview;
			Column.HSplitTop(LaserPreviewHeight, &LaserPreview, &Column);
			Column.HSplitTop(2 * MarginSmall, nullptr, &Column);
			DoLaserPreview(&LaserPreview, g_Config.m_ClLaserRifleInnerColor, g_Config.m_ClLaserRifleOutlineColor, LASERTYPE_RIFLE);

			Column.HSplitTop(LaserPreviewHeight, &LaserPreview, &Column);
			Column.HSplitTop(2 * MarginSmall, nullptr, &Column);
			DoLaserPreview(&LaserPreview, g_Config.m_ClLaserShotgunInnerColor, g_Config.m_ClLaserShotgunOutlineColor, LASERTYPE_SHOTGUN);
		}
		else
		{
			Column.HSplitTop(20.0f, nullptr, &Column);
			Column.HSplitTop(20.0f, nullptr, &Column);
			Column.HSplitTop(10.0f, nullptr, &Column);
			Column.HSplitTop(50.0f, nullptr, &Column);
			Column.HSplitTop(2 * MarginSmall, nullptr, &Column);
			Column.HSplitTop(50.0f, nullptr, &Column);
			Column.HSplitTop(2 * MarginSmall, nullptr, &Column);
		}
		Column.HSplitTop(MarginSmall, nullptr, &Column);
	}
	EndSection(Column);

	// ***** Spectator ***** //
	BeginSectionHeader(Column, MarginBetweenSections, RCLIENT_SETTINGS_SECTION_SPECTATOR, RCLocalize("Spectator"));
	if(s_aSectionExpanded[RCLIENT_SETTINGS_SECTION_SPECTATOR])
	{
		static CButtonContainer s_ReaderButtonSpecPlr, s_ClearButtonSpecPlr;
		Column.HSplitTop(LineSize, &Label, &Column);
		DoLine_KeyReader(Label, s_ReaderButtonSpecPlr, s_ClearButtonSpecPlr, RCLocalize("Tracker for spectating player"), "ri_tracker_spectator");
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiSpectatorMoveEnable, RCLocalize("Enable spectator camera move"), &g_Config.m_RiSpectatorMoveEnable, &Column, LineSize);
		if(g_Config.m_RiSpectatorMoveEnable)
		{
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RiSpectatorMoveSpeed, &g_Config.m_RiSpectatorMoveSpeed, &Button, RCLocalize("Speed of camera"), 10, 200);
			Column.HSplitTop(MarginSmall, &Button, &Column);
			static CButtonContainer s_ReaderButtonSpectatorLeft, s_ClearButtonSpectatorLeft,
				s_ReaderButtonSpectatorRight, s_ClearButtonSpectatorRight,
				s_ReaderButtonSpectatorUp, s_ClearButtonSpectatorUp,
				s_ReaderButtonSpectatorDown, s_ClearButtonSpectatorDown;
			Column.HSplitTop(LineSize, &Label, &Column);
			DoLine_KeyReader(Label, s_ReaderButtonSpectatorLeft, s_ClearButtonSpectatorLeft, RCLocalize("Move left"), "+ri_spec_left");
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, &Label, &Column);
			DoLine_KeyReader(Label, s_ReaderButtonSpectatorRight, s_ClearButtonSpectatorRight, RCLocalize("Move right"), "+ri_spec_right");
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, &Label, &Column);
			DoLine_KeyReader(Label, s_ReaderButtonSpectatorUp, s_ClearButtonSpectatorUp, RCLocalize("Move up"), "+ri_spec_up");
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, &Label, &Column);
			DoLine_KeyReader(Label, s_ReaderButtonSpectatorDown, s_ClearButtonSpectatorDown, RCLocalize("Move down"), "+ri_spec_down");
		}
		else
		{
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, &Button, &Column);
			Column.HSplitTop(MarginSmall, &Button, &Column);
			Column.HSplitTop(LineSize, &Button, &Column);
			Column.HSplitTop(MarginSmall, &Button, &Column);
			Column.HSplitTop(LineSize, &Button, &Column);
			Column.HSplitTop(MarginSmall, &Button, &Column);
			Column.HSplitTop(LineSize, &Button, &Column);
			Column.HSplitTop(MarginSmall, &Button, &Column);
			Column.HSplitTop(LineSize, &Button, &Column);
		}
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		static std::vector<CButtonContainer> s_vSpectatorSortButtonContainers = {{}, {}, {}};
		DoLine_RadioMenu(Column, RCLocalize("Sort by id:", "ScoreboardSorting"),
			s_vSpectatorSortButtonContainers,
			{RCLocalize("Default", "ScoreboardSorting"), RCLocalize("Teams", "ScoreboardSorting"), RCLocalize("All", "ScoreboardSorting")},
			{0, 1, 2},
			g_Config.m_RiSpectatorSortById);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Label, &Column);
		static CButtonContainer s_ReaderButtonFindTp, s_ClearButtonFindTp;
		DoLine_KeyReader(Label, s_ReaderButtonFindTp, s_ClearButtonFindTp, RCLocalize("Find teleport"), "ri_goto_tele_cursor");
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Label, &Column);
		static CButtonContainer s_ReaderButtonFindFinish, s_ClearButtonFindFinish;
		DoLine_KeyReader(Label, s_ReaderButtonFindFinish, s_ClearButtonFindFinish, RCLocalize("Find finish"), "ri_goto_finish_cursor");
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Label, &Column);
		static CButtonContainer s_ReaderButtonPlayerMenu, s_ClearButtonPlayerMenu;
		DoLine_KeyReader(Label, s_ReaderButtonPlayerMenu, s_ClearButtonPlayerMenu, RCLocalize("Player menu"), "toggle_playermenu");
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Label, &Column);
		static CButtonContainer s_ReaderButtonFindCheckpointId, s_ClearButtonFindCheckpointId;
		DoLine_KeyReader(Label, s_ReaderButtonFindCheckpointId, s_ClearButtonFindCheckpointId, RCLocalize("Find checkpoint"), "ri_get_checkpoint_id");
		Column.HSplitTop(MarginSmall, nullptr, &Column);
	}
	EndSection(Column);

	// ***** Chat Bubbles ***** //
	BeginSectionHeader(Column, MarginBetweenSections, RCLIENT_SETTINGS_SECTION_CHAT_BUBBLES, RCLocalize("Chat Bubbles(E-Client)"));
	if(s_aSectionExpanded[RCLIENT_SETTINGS_SECTION_CHAT_BUBBLES])
	{
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiChatBubbles, RCLocalize("Show Chatbubbles above players"), &g_Config.m_RiChatBubbles, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiChatBubblesDemo, RCLocalize("Show Chatbubbles in demo"), &g_Config.m_RiChatBubblesDemo, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiChatBubblesSelf, RCLocalize("Show Chatbubbles above you"), &g_Config.m_RiChatBubblesSelf, &Column, LineSize);
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_RiChatBubbleSize, &g_Config.m_RiChatBubbleSize, &Button, RCLocalize("Chat Bubble Size"), 20, 30);
		Column.HSplitTop(MarginSmall, &Button, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		DoFloatScrollBar(&g_Config.m_RiChatBubbleShowTime, &g_Config.m_RiChatBubbleShowTime, &Button, RCLocalize("Show the Bubbles for"), 200, 1000, 100, &CUi::ms_LinearScrollbarScale, 0, "s");
		Column.HSplitTop(LineSize, &Button, &Column);
		DoFloatScrollBar(&g_Config.m_RiChatBubbleFadeIn, &g_Config.m_RiChatBubbleFadeIn, &Button, RCLocalize("fade in for"), 15, 100, 100, &CUi::ms_LinearScrollbarScale, 0, "s");
		Column.HSplitTop(LineSize, &Button, &Column);
		DoFloatScrollBar(&g_Config.m_RiChatBubbleFadeOut, &g_Config.m_RiChatBubbleFadeOut, &Button, RCLocalize("fade out for"), 15, 100, 100, &CUi::ms_LinearScrollbarScale, 0, "s");
		Column.HSplitTop(MarginSmall, nullptr, &Column);
	}
	EndSection(Column);

	// ***** RClient User Indicator ***** //
	BeginSectionHeader(Column, MarginBetweenSections, RCLIENT_SETTINGS_SECTION_RCLIENT_INDICATOR, RCLocalize("RClient User Indicator"));
	if(s_aSectionExpanded[RCLIENT_SETTINGS_SECTION_RCLIENT_INDICATOR])
	{
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiShowRclientIndicator, RCLocalize("Show RClient User indicator"), &g_Config.m_RiShowRclientIndicator, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_RiRclientIndicatorSize, &g_Config.m_RiRclientIndicatorSize, &Button, Localize("Size of Rclient indicator icons"), -50, 100);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiShowIndicatorDynamic, RCLocalize("Indicator will change pos when some nearby"), &g_Config.m_RiShowIndicatorDynamic, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiRclientIndicatorAboveSelf, RCLocalize("Show indicator above you"), &g_Config.m_RiRclientIndicatorAboveSelf, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiScoreboardShowRclientIndicator, RCLocalize("Show indicator in scoreboard"), &g_Config.m_RiScoreboardShowRclientIndicator, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		if(g_Config.m_RiScoreboardShowRclientIndicator)
		{
			Column.HSplitTop(LineSize, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RiScoreboardRclientIndicatorSize, &g_Config.m_RiScoreboardRclientIndicatorSize, &Button, RCLocalize("Size of indicator in scoreboard"), -50, 100);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
		}
		else
		{
			Column.HSplitTop(LineSize, nullptr, &Column);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
		}
	}
	EndSection(Column);

	// ***** Edge Info ***** //
	BeginSectionHeader(Column, MarginBetweenSections, RCLIENT_SETTINGS_SECTION_EDGE_INFO, RCLocalize("Edge Info"));
	if(s_aSectionExpanded[RCLIENT_SETTINGS_SECTION_EDGE_INFO])
	{
		static CButtonContainer s_ReaderButtonEdgeInfo, s_ClearButtonEdgeInfo;
		Column.HSplitTop(LineSize, &Label, &Column);
		DoLine_KeyReader(Label, s_ReaderButtonEdgeInfo, s_ClearButtonEdgeInfo, RCLocalize("Show edge info"), "ri_toggle_edgeinfo");
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiEdgeInfoCords, RCLocalize("Show edge info about freeze"), &g_Config.m_RiEdgeInfoCords, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiEdgeInfoJump, RCLocalize("Show edge info about jumps"), &g_Config.m_RiEdgeInfoJump, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		auto DoOutlineType = [&](CButtonContainer &ButtonContainer, const char *pName, unsigned int &Color, ColorRGBA ColorDefault) {
			// Checkbox & Color
			DoLine_ColorPicker(&ButtonContainer, ColorPickerLineSize, ColorPickerLabelSize, 0, &Column, pName, &Color, ColorDefault);
			Column.HSplitTop(ColorPickerLineSpacing, nullptr, &Column);
		};
		static CButtonContainer s_aOutlineButtonContainers[3];
		DoOutlineType(s_aOutlineButtonContainers[0], TCLocalize("Color when over freeze"), g_Config.m_RiEdgeInfoColorFreeze, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::RiEdgeInfoColorFreeze)));
		DoOutlineType(s_aOutlineButtonContainers[1], TCLocalize("Color when over kill"), g_Config.m_RiEdgeInfoColorKill, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::RiEdgeInfoColorKill)));
		DoOutlineType(s_aOutlineButtonContainers[2], TCLocalize("Color when falling safely"), g_Config.m_RiEdgeInfoColorSafe, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::RiEdgeInfoColorSafe)));
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_RiEdgeInfoPosX, &g_Config.m_RiEdgeInfoPosX, &Button, RCLocalize("Edge info pos x"), 0, 100);
		Column.HSplitTop(MarginSmall, &Button, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_RiEdgeInfoPosY, &g_Config.m_RiEdgeInfoPosY, &Button, RCLocalize("Edge info pos y"), 0, 100);
		Column.HSplitTop(MarginSmall, &Button, &Column);
	}
	EndSection(Column);

	// ***** Voice ***** //
	BeginSectionHeader(Column, MarginBetweenSections, RCLIENT_SETTINGS_SECTION_VOICE, RCLocalize("Voice"));
	if(s_aSectionExpanded[RCLIENT_SETTINGS_SECTION_VOICE])
	{
		CUIRect Rightoffset;
		auto DoVoiceSubHeader = [&](const char *pTitle) {
			Column.HSplitTop(LineSize, &Label, &Column);
			Ui()->DoLabel(&Label, pTitle, FontSize, TEXTALIGN_ML);
			Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
		};

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiVoiceEnable, RCLocalize("Enable voice chat"), &g_Config.m_RiVoiceEnable, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		if(g_Config.m_RiVoiceEnable)
		{
			const bool InputMissing = GameClient()->m_RClient.IsVoiceInputUnavailable();
			const bool OutputMissing = GameClient()->m_RClient.IsVoiceOutputUnavailable();
			if(InputMissing || OutputMissing)
			{
				Column.HSplitTop(LineSize, &Label, &Column);
				if(InputMissing && OutputMissing)
					Ui()->DoLabel(&Label, RCLocalize("Voice devices not available (input/output)"), FontSize * 0.9f, TEXTALIGN_ML);
				else if(InputMissing)
					Ui()->DoLabel(&Label, RCLocalize("Voice input device not available"), FontSize * 0.9f, TEXTALIGN_ML);
				else
					Ui()->DoLabel(&Label, RCLocalize("Voice output device not available"), FontSize * 0.9f, TEXTALIGN_ML);
				Column.HSplitTop(MarginSmall, nullptr, &Column);
			}
		}
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiVoiceOffNonActive, RCLocalize("Off voice when window nonactive"), &g_Config.m_RiVoiceOffNonActive, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		Column.HSplitTop(LineSize, nullptr, &Column);

		DoVoiceSubHeader(RCLocalize("Devices"));
		static CUi::SDropDownState s_VoiceBackendDropDownState;
		static CUi::SDropDownState s_VoiceInputDropDownState;
		static CUi::SDropDownState s_VoiceOutputDropDownState;
		static CScrollRegion s_VoiceBackendDropDownScrollRegion;
		static CScrollRegion s_VoiceInputDropDownScrollRegion;
		static CScrollRegion s_VoiceOutputDropDownScrollRegion;
		s_VoiceBackendDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_VoiceBackendDropDownScrollRegion;
		s_VoiceInputDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_VoiceInputDropDownScrollRegion;
		s_VoiceOutputDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_VoiceOutputDropDownScrollRegion;

		auto DoVoiceBackendDropDown = [&](CUIRect &ColumnRect, const char *pLabel, char *pConfigValue, int ConfigSize, CUi::SDropDownState &DropDownState) {
			std::vector<std::string> vBackendNames;
			std::vector<std::string> vBackendValues;
			std::vector<const char *> vpBackendNames;

			vBackendNames.emplace_back(RCLocalize("Auto", "Voice audio backend"));
			vBackendValues.emplace_back("");

			const int NumDrivers = SDL_GetNumAudioDrivers();
			vBackendNames.reserve(NumDrivers + 2);
			vBackendValues.reserve(NumDrivers + 2);

			for(int i = 0; i < NumDrivers; i++)
			{
				const char *pDriver = SDL_GetAudioDriver(i);
				if(pDriver && pDriver[0] != '\0')
				{
					vBackendNames.emplace_back(pDriver);
					vBackendValues.emplace_back(pDriver);
				}
			}

			if(pConfigValue[0] != '\0')
			{
				bool Found = false;
				for(const std::string &Name : vBackendValues)
				{
					if(str_comp_nocase(Name.c_str(), pConfigValue) == 0)
					{
						Found = true;
						break;
					}
				}
				if(!Found)
				{
					vBackendNames.emplace_back(pConfigValue);
					vBackendValues.emplace_back(pConfigValue);
				}
			}

			vpBackendNames.reserve(vBackendNames.size());
			for(const std::string &Name : vBackendNames)
				vpBackendNames.push_back(Name.c_str());

			int Selected = 0;
			if(pConfigValue[0] != '\0')
			{
				for(size_t i = 1; i < vBackendValues.size(); i++)
				{
					if(str_comp_nocase(vBackendValues[i].c_str(), pConfigValue) == 0)
					{
						Selected = (int)i;
						break;
					}
				}
			}

			CUIRect DropDownRect;
			ColumnRect.HSplitTop(LineSize, &DropDownRect, &ColumnRect);
			DropDownRect.VSplitLeft(120.0f, &Label, &DropDownRect);
			Ui()->DoLabel(&Label, pLabel, FontSize, TEXTALIGN_ML);
			const int NewSelected = Ui()->DoDropDown(&DropDownRect, Selected, vpBackendNames.data(), vpBackendNames.size(), DropDownState);
			if(NewSelected != Selected)
			{
				if(NewSelected <= 0)
					pConfigValue[0] = '\0';
				else
					str_copy(pConfigValue, vBackendValues[NewSelected].c_str(), ConfigSize);
			}
		};

		auto DoVoiceDeviceDropDown = [&](CUIRect &ColumnRect, const char *pLabel, char *pConfigValue, int ConfigSize, bool Capture, CUi::SDropDownState &DropDownState) {
			std::vector<std::string> vDeviceNames;
			std::vector<std::string> vDeviceValues;
			std::vector<const char *> vpDeviceNames;

			const int NumDevices = SDL_GetNumAudioDevices(Capture ? 1 : 0);
			vDeviceNames.reserve(NumDevices + 2);
			vDeviceValues.reserve(NumDevices + 2);

			vDeviceNames.emplace_back(RCLocalize("Default", "Voice device"));
			vDeviceValues.emplace_back("");

			for(int i = 0; i < NumDevices; i++)
			{
				const char *pName = SDL_GetAudioDeviceName(i, Capture ? 1 : 0);
				if(pName && pName[0] != '\0')
				{
					vDeviceNames.emplace_back(pName);
					vDeviceValues.emplace_back(pName);
				}
			}

			if(pConfigValue[0] != '\0')
			{
				bool Found = false;
				for(const std::string &Name : vDeviceValues)
				{
					if(str_comp_nocase(Name.c_str(), pConfigValue) == 0)
					{
						Found = true;
						break;
					}
				}
				if(!Found)
				{
					vDeviceNames.emplace_back(pConfigValue);
					vDeviceValues.emplace_back(pConfigValue);
				}
			}

			vpDeviceNames.reserve(vDeviceNames.size());
			for(const std::string &Name : vDeviceNames)
				vpDeviceNames.push_back(Name.c_str());

			int Selected = 0;
			if(pConfigValue[0] != '\0')
			{
				for(size_t i = 1; i < vDeviceValues.size(); i++)
				{
					if(str_comp_nocase(vDeviceValues[i].c_str(), pConfigValue) == 0)
					{
						Selected = (int)i;
						break;
					}
				}
			}

			CUIRect DropDownRect;
			ColumnRect.HSplitTop(LineSize, &DropDownRect, &ColumnRect);
			DropDownRect.VSplitLeft(120.0f, &Label, &DropDownRect);
			Ui()->DoLabel(&Label, pLabel, FontSize, TEXTALIGN_ML);
			const int NewSelected = Ui()->DoDropDown(&DropDownRect, Selected, vpDeviceNames.data(), vpDeviceNames.size(), DropDownState);
			if(NewSelected != Selected)
			{
				if(NewSelected <= 0)
					pConfigValue[0] = '\0';
				else
					str_copy(pConfigValue, vDeviceValues[NewSelected].c_str(), ConfigSize);
			}
		};

		DoVoiceBackendDropDown(Column, RCLocalize("Audio backend"), g_Config.m_RiVoiceAudioBackend, sizeof(g_Config.m_RiVoiceAudioBackend), s_VoiceBackendDropDownState);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize - 4, &Label, &Column);
		Label.VSplitLeft(LineSize, nullptr, &Label);
		Ui()->DoLabel(&Label, RCLocalize("If change backend u need restart game"), FontSize - 4, TEXTALIGN_ML);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoVoiceDeviceDropDown(Column, RCLocalize("Input device"), g_Config.m_RiVoiceInputDevice, sizeof(g_Config.m_RiVoiceInputDevice), true, s_VoiceInputDropDownState);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoVoiceDeviceDropDown(Column, RCLocalize("Output device"), g_Config.m_RiVoiceOutputDevice, sizeof(g_Config.m_RiVoiceOutputDevice), false, s_VoiceOutputDropDownState);
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		Column.HSplitTop(LineSize, nullptr, &Column);

		DoVoiceSubHeader(RCLocalize("Input"));
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiVoiceMicMute, RCLocalize("Mute microphone"), &g_Config.m_RiVoiceMicMute, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		static std::vector<CButtonContainer> s_vVoiceActivationButtonContainers = {{}, {}};
		DoLine_RadioMenu(Column, RCLocalize("Voice activation:", "Voice activation"),
			s_vVoiceActivationButtonContainers,
			{RCLocalize("Push to talk", "Voice activation"), RCLocalize("Voice", "Voice activation")},
			{0, 1},
			g_Config.m_RiVoiceVadEnable);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		if(g_Config.m_RiVoiceVadEnable)
		{
			Column.HSplitTop(LineSize, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RiVoiceVadThreshold, &g_Config.m_RiVoiceVadThreshold, &Button, RCLocalize("VAD threshold (%)"), 0, 100);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RiVoiceVadReleaseDelayMs, &g_Config.m_RiVoiceVadReleaseDelayMs, &Button, RCLocalize("VAD release delay (ms)"), 0, 1000);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
		}
		static CButtonContainer s_ReaderButtonVoicePtt, s_ClearButtonVoicePtt, s_ReaderButtonVoiceMuteToggle, s_ClearButtonVoiceMuteToggle;
		if(!g_Config.m_RiVoiceVadEnable)
		{
			Column.HSplitTop(LineSize, &Label, &Column);
			DoLine_KeyReader(Label, s_ReaderButtonVoicePtt, s_ClearButtonVoicePtt, RCLocalize("Voice button"), "+ri_voice_ptt");
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RiVoicePttReleaseDelayMs, &g_Config.m_RiVoicePttReleaseDelayMs, &Button, RCLocalize("PTT release delay (ms)"), 0, 1000);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
		}
		Column.HSplitTop(LineSize, &Label, &Column);
		DoLine_KeyReader(Label, s_ReaderButtonVoiceMuteToggle, s_ClearButtonVoiceMuteToggle, RCLocalize("Mic Mute toggle"), "toggle ri_voice_mic_mute 1 0");
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_RiVoiceMicVolume, &g_Config.m_RiVoiceMicVolume, &Button, RCLocalize("Microphone volume"), 0, 300);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Label, &Column);
		Ui()->DoLabel(&Label, RCLocalize("VAD allow list"), FontSize, TEXTALIGN_ML);
		Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		static CLineInput s_VoiceVadAllow(g_Config.m_RiVoiceVadAllow, sizeof(g_Config.m_RiVoiceVadAllow));
		s_VoiceVadAllow.SetEmptyText(RCLocalize("Name1,Name2"));
		Ui()->DoEditBox(&s_VoiceVadAllow, &Button, FontSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		Column.HSplitTop(LineSize, nullptr, &Column);

		DoVoiceSubHeader(RCLocalize("Listening & Groups"));
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiVoiceIgnoreDistance, RCLocalize("Ignore distance"), &g_Config.m_RiVoiceIgnoreDistance, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_RiVoiceRadius, &g_Config.m_RiVoiceRadius, &Button, RCLocalize("Voice radius (tiles)"), 1, 400);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiVoiceGroupGlobal, RCLocalize("Hear group members everywhere"), &g_Config.m_RiVoiceGroupGlobal, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiVoiceHearOnSpecPos, RCLocalize("Hear from camera center while spectating"), &g_Config.m_RiVoiceHearOnSpecPos, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiVoiceHearPeoplesInSpectate, RCLocalize("Hear observers (inactive players, not /spec)"), &g_Config.m_RiVoiceHearPeoplesInSpectate, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiVoiceHearVad, RCLocalize("Hear players using voice activation"), &g_Config.m_RiVoiceHearVad, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		static std::vector<CButtonContainer> s_vVoiceTeamVisibilityButtonContainers = {{}, {}, {}};
		DoLine_RadioMenu(Column, RCLocalize("Hear people that:", "VoiceChat"),
			s_vVoiceTeamVisibilityButtonContainers,
			{RCLocalize("You see", "VoiceChat"), RCLocalize("In team", "VoiceChat"), RCLocalize("All", "VoiceChat")},
			{0, 1, 2},
			g_Config.m_RiVoiceVisibilityMode);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		static SDropDownSimple s_VoiceGroupModeDrop;
		g_Config.m_RiVoiceGroupMode = DoSimpleDropDown(
			Ui(),
			Column,
			RCLocalize("Group mode"),
			g_Config.m_RiVoiceGroupMode,
			{"Hear All / Send to All", "Hear Group / Send to Group", "Hear All / Send to Group", "Hear Group / Send to All"},
			"Voice group mode",
			s_VoiceGroupModeDrop);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Label, &Column);
		Ui()->DoLabel(&Label, RCLocalize("Group token"), FontSize, TEXTALIGN_ML);
		Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		static CLineInput s_VoiceGroupToken(g_Config.m_RiVoiceToken, sizeof(g_Config.m_RiVoiceToken));
		s_VoiceGroupToken.SetEmptyText(RCLocalize("Group token"));
		Ui()->DoEditBox(&s_VoiceGroupToken, &Button, FontSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		static std::vector<CButtonContainer> s_vVoiceWhiteListButtonContainers = {{}, {}, {}};
		DoLine_RadioMenu(Column, RCLocalize("Block people with:", "VoiceChat"),
			s_vVoiceWhiteListButtonContainers,
			{RCLocalize("None", "VoiceChat"), RCLocalize("Whitelist", "VoiceChat"), RCLocalize("Blacklist", "VoiceChat")},
			{0, 1, 2},
			g_Config.m_RiVoiceListMode);
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		Column.HSplitTop(LineSize, nullptr, &Column);

		DoVoiceSubHeader(RCLocalize("Mix & Processing"));
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_RiVoiceVolume, &g_Config.m_RiVoiceVolume, &Button, RCLocalize("Voice volume"), 0, 400);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiVoiceStereo, RCLocalize("Stereo output (pan left/right)"), &g_Config.m_RiVoiceStereo, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_RiVoiceStereoWidth, &g_Config.m_RiVoiceStereoWidth, &Button, RCLocalize("Stereo width (%)"), 0, 200);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiVoiceFilterEnable, RCLocalize("Voice filter (HPF+compressor+limiter)"), &g_Config.m_RiVoiceFilterEnable, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
#if defined(CONF_RNNOISE)
		const char *pNoiseSuppressLabel = RCLocalize("Noise suppressor (RNNoise)");
#else
		const char *pNoiseSuppressLabel = RCLocalize("Noise suppressor");
#endif
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiVoiceNoiseSuppressEnable, pNoiseSuppressLabel, &g_Config.m_RiVoiceNoiseSuppressEnable, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
#if !defined(CONF_RNNOISE)
		if(g_Config.m_RiVoiceNoiseSuppressEnable)
		{
			Column.HSplitTop(LineSize, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RiVoiceNoiseSuppressStrength, &g_Config.m_RiVoiceNoiseSuppressStrength, &Button, RCLocalize("Noise suppress strength (%)"), 0, 100);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
		}
#endif
		Column.HSplitTop(LineSize, &Label, &Column);
		Ui()->DoLabel(&Label, RCLocalize("Filter presets"), FontSize, TEXTALIGN_ML);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		CUIRect ButtonSoft, ButtonBalanced, ButtonStrong;
		Button.VSplitLeft(Button.w / 3.0f - MarginSmall, &ButtonSoft, &Button);
		Button.VSplitLeft(MarginSmall, nullptr, &Button);
		Button.VSplitLeft(Button.w / 2.0f - MarginSmall, &ButtonBalanced, &Button);
		Button.VSplitLeft(MarginSmall, nullptr, &ButtonStrong);

		static CButtonContainer s_VoiceFilterSoft, s_VoiceFilterBalanced, s_VoiceFilterStrong;
		if(DoButton_Menu(&s_VoiceFilterSoft, RCLocalize("Soft"), 0, &ButtonSoft))
		{
			g_Config.m_RiVoiceCompThreshold = 25;
			g_Config.m_RiVoiceCompRatio = 20;
			g_Config.m_RiVoiceCompAttackMs = 15;
			g_Config.m_RiVoiceCompReleaseMs = 150;
			g_Config.m_RiVoiceCompMakeup = 120;
			g_Config.m_RiVoiceLimiter = 70;
		}
		if(DoButton_Menu(&s_VoiceFilterBalanced, RCLocalize("Balanced"), 0, &ButtonBalanced))
		{
			g_Config.m_RiVoiceCompThreshold = 20;
			g_Config.m_RiVoiceCompRatio = 25;
			g_Config.m_RiVoiceCompAttackMs = 20;
			g_Config.m_RiVoiceCompReleaseMs = 200;
			g_Config.m_RiVoiceCompMakeup = 160;
			g_Config.m_RiVoiceLimiter = 50;
		}
		if(DoButton_Menu(&s_VoiceFilterStrong, RCLocalize("Strong"), 0, &ButtonStrong))
		{
			g_Config.m_RiVoiceCompThreshold = 15;
			g_Config.m_RiVoiceCompRatio = 40;
			g_Config.m_RiVoiceCompAttackMs = 10;
			g_Config.m_RiVoiceCompReleaseMs = 250;
			g_Config.m_RiVoiceCompMakeup = 200;
			g_Config.m_RiVoiceLimiter = 40;
		}
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		static int s_ShowVoiceFilterAdvanced = 0;
		DoButton_CheckBoxAutoVMarginAndSet(&s_ShowVoiceFilterAdvanced, RCLocalize("Show advanced filter settings"), &s_ShowVoiceFilterAdvanced, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		if(s_ShowVoiceFilterAdvanced)
		{
			Column.HSplitTop(LineSize, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RiVoiceCompThreshold, &g_Config.m_RiVoiceCompThreshold, &Button, RCLocalize("Comp threshold (%)"), 1, 100);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RiVoiceCompRatio, &g_Config.m_RiVoiceCompRatio, &Button, RCLocalize("Comp ratio (x0.1)"), 10, 80);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RiVoiceCompAttackMs, &g_Config.m_RiVoiceCompAttackMs, &Button, RCLocalize("Comp attack (ms)"), 1, 100);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RiVoiceCompReleaseMs, &g_Config.m_RiVoiceCompReleaseMs, &Button, RCLocalize("Comp release (ms)"), 10, 500);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RiVoiceCompMakeup, &g_Config.m_RiVoiceCompMakeup, &Button, RCLocalize("Comp makeup (%)"), 0, 300);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RiVoiceLimiter, &g_Config.m_RiVoiceLimiter, &Button, RCLocalize("Limiter (%)"), 10, 100);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
		}
		static std::vector<CButtonContainer> s_vVoiceTestModeButtons = {{}, {}, {}};
		DoLine_RadioMenu(Column, RCLocalize("Test mode", "VoiceChat"),
			s_vVoiceTestModeButtons,
			{RCLocalize("Off", "VoiceChat"), RCLocalize("Local", "VoiceChat"), RCLocalize("Server", "VoiceChat")},
			{0, 1, 2},
			g_Config.m_RiVoiceTestMode);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		Button.VSplitLeft(120.0f, &Label, &Button);
		Ui()->DoLabel(&Label, RCLocalize("Microphone level"), FontSize, TEXTALIGN_ML);
		{
			const float MicLevel = std::clamp(GameClient()->m_RClient.VoiceMicLevel(), 0.0f, 1.0f);
			const float Rounding = minimum(5.0f, Button.h / 2.0f);
			Button.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_ALL, Rounding);
			ColorRGBA FillColor(0.2f, 0.8f, 0.2f, 0.85f);
			if(MicLevel > 0.85f)
				FillColor = ColorRGBA(0.9f, 0.2f, 0.2f, 0.85f);
			else if(MicLevel > 0.65f)
				FillColor = ColorRGBA(0.9f, 0.75f, 0.2f, 0.85f);
			if(MicLevel > 0.001f)
			{
				CUIRect Fill = Button;
				Fill.w = maximum(2.0f * Rounding, Button.w * MicLevel);
				Fill.Draw(FillColor, IGraphics::CORNER_ALL, Rounding);
			}
			char aBuf[16];
			const int Percent = (int)(MicLevel * 100.0f + 0.5f);
			str_format(aBuf, sizeof(aBuf), "%d%%", Percent);
			Ui()->DoLabel(&Button, aBuf, FontSize * 0.9f, TEXTALIGN_MR);
		}
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		static char s_aVoiceNameVolumeName[32];
		static int s_VoiceNameVolumePercent = 100;
		static CLineInput s_VoiceNameVolumeName;
		static CButtonContainer s_VoiceNameVolumeSetButton, s_VoiceNameVolumeRemoveButton;
		Column.HSplitTop(LineSize, &Button, &Column);
		Button.VSplitLeft(120.0f, &Label, &Button);
		Ui()->DoLabel(&Label, RCLocalize("Name volume"), FontSize, TEXTALIGN_ML);
		s_VoiceNameVolumeName.SetBuffer(s_aVoiceNameVolumeName, sizeof(s_aVoiceNameVolumeName));
		s_VoiceNameVolumeName.SetEmptyText(RCLocalize("Nickname"));
		Ui()->DoEditBox(&s_VoiceNameVolumeName, &Button, EditBoxFontSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&s_VoiceNameVolumePercent, &s_VoiceNameVolumePercent, &Button, RCLocalize("Name volume (%)"), 0, 200);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		CUIRect ButtonLeft, ButtonRight;
		Button.VSplitMid(&ButtonLeft, &ButtonRight, MarginSmall);
		if(DoButton_Menu(&s_VoiceNameVolumeSetButton, RCLocalize("Set volume"), 0, &ButtonLeft))
		{
			VoiceNameVolumesSet(g_Config.m_RiVoiceNameVolumes, sizeof(g_Config.m_RiVoiceNameVolumes), s_aVoiceNameVolumeName, s_VoiceNameVolumePercent);
		}
		if(DoButton_Menu(&s_VoiceNameVolumeRemoveButton, RCLocalize("Remove volume"), 0, &ButtonRight))
		{
			VoiceNameVolumesRemove(g_Config.m_RiVoiceNameVolumes, sizeof(g_Config.m_RiVoiceNameVolumes), s_aVoiceNameVolumeName);
		}
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Label, &Column);
		Ui()->DoLabel(&Label, RCLocalize("Example: Name=80,Other=120"), FontSize * 0.9f, TEXTALIGN_ML);
		Column.HSplitTop(LineSize, &Label, &Column);
		Ui()->DoLabel(&Label, g_Config.m_RiVoiceNameVolumes[0] ? g_Config.m_RiVoiceNameVolumes : RCLocalize("Name volume list empty"), FontSize * 0.9f, TEXTALIGN_ML);
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		Column.HSplitTop(LineSize, nullptr, &Column);

		DoVoiceSubHeader(RCLocalize("Overlay"));
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiVoiceShowOverlay, RCLocalize("Show overlay"), &g_Config.m_RiVoiceShowOverlay, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiVoiceShowWhenActive, RCLocalize("Show when microphone active"), &g_Config.m_RiVoiceShowWhenActive, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		if(g_Config.m_RiVoiceShowWhenActive)
		{
			Column.VSplitLeft(25.0f, &Label, &Rightoffset);
			Column.HSplitTop(LineSize, nullptr, &Column);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiVoiceShowPing, RCLocalize("Show voice ping"), &g_Config.m_RiVoiceShowPing, &Rightoffset, LineSize);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
		}
		else
		{
			Column.HSplitTop(LineSize, nullptr, &Column);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
		}
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiVoiceShowIndicator, RCLocalize("Show voice indicator"), &g_Config.m_RiVoiceShowIndicator, &Column, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		if(g_Config.m_RiVoiceShowIndicator)
		{
			Column.VSplitLeft(25.0f, &Label, &Rightoffset);
			Column.HSplitTop(LineSize, nullptr, &Column);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiVoiceIndicatorAboveSelf, RCLocalize("Show indicator above you"), &g_Config.m_RiVoiceIndicatorAboveSelf, &Rightoffset, LineSize);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
		}
		else
		{
			Column.HSplitTop(LineSize, nullptr, &Column);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
		}

		for(CBindChat::CBindRclient &BindchatDefault : s_aDefaultBindChatRclientVoice)
			DoBindchatDefault(Column, BindchatDefault);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		static CButtonContainer s_VoiceChatButton;
		if(DoButtonLineSize_Menu(&s_VoiceChatButton, RCLocalize("Reset Voice Chatbinds"), 0, &Button, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.5f, 0.0f, 0.0f, 0.25f)))
		{
			for(const CBindChat::CBindRclient &BindDefault : s_aDefaultBindChatRclientVoice)
			{
				GameClient()->m_BindChat.RemoveBindCommand(BindDefault.m_Bind.m_aCommand);
				GameClient()->m_BindChat.AddBind(BindDefault.m_Bind);
			}
		}
	}
	EndSection(Column);

	RightView = Column;

	CUIRect ScrollRegion;
	ScrollRegion.x = MainView.x;
	ScrollRegion.y = maximum(LeftView.y, RightView.y) + MarginSmall * 2.0f;
	ScrollRegion.w = MainView.w;
	ScrollRegion.h = 0.0f;
	s_ScrollRegion.AddRect(ScrollRegion);
	s_ScrollRegion.End();
}

void CMenus::RenderSettingsRushieNameplatesEditor(CUIRect MainView)
{
	// --- Делим MainView на три колонки ---
	CUIRect ListCol, SchemeCol, PreviewCol, Spacer;
	MainView.VSplitLeft(MainView.w * 0.2f, &ListCol, &SchemeCol); // 20% список
	SchemeCol.VSplitLeft(SchemeCol.w * 0.5f, &SchemeCol, &PreviewCol); // 40% схема, 40% превью
	ListCol.VSplitRight(MarginSmall, &ListCol, &Spacer); // отступ справа от списка
	SchemeCol.VSplitRight(MarginSmall, &SchemeCol, &Spacer); // отступ справа от схемы
	PreviewCol.VSplitLeft(MarginSmall, &Spacer, &PreviewCol); // отступ слева от превью

	// --- 1. Левая колонка: вертикальный список для добавления ---
	ListCol.Margin(MarginSmall, &ListCol);
	ListCol.Draw(ColorRGBA(0.13f, 0.13f, 0.13f, 0.7f), IGraphics::CORNER_ALL, 8.0f);
	static const struct
	{
		char Code;
		const char *pDesc;
		const char *pIcon;
	} s_aElems[] = {
		{'n', "Nick", "N"},
		{'C', "Country", "FL"},
		{'c', "Clan", "C"},
		{'m', "ID", "#"},
		{'M', "ID Line", "#2"},
		{'f', "Friend", "♥"},
		{'d', "Dir", "⇄"},
		{'p', "Ping", "P"},
		{'r', "Reason", "R"},
		{'s', "Skin", "S"},
		{'i', "Ignore", "I"},
		{'h', "Hook", "H"},
		{'l', "↵", "↵"},
		{'H', "HookName", "HN"},
		{'F', "FireName", "FN"},
		{'I', "RC_User", "RI"},
		{'V', "Voice", "🎙"},
	};

	// Улучшенные расчеты размеров кнопок с минимальными ограничениями
	const float MinBtnHeight = 24.0f; // минимальная высота кнопки
	const float BtnSpacing = MarginExtraSmall; // отступ между кнопками
	const float ContentPadding = MarginExtraSmall; // внутренние отступы

	float AvailableHeight = ListCol.h - 2.0f * ContentPadding;
	float TotalSpacing = BtnSpacing * (std::size(s_aElems) - 1);
	float BtnH = (AvailableHeight - TotalSpacing) / (float)std::size(s_aElems);
	BtnH = std::max(BtnH, MinBtnHeight); // применяем минимальную высоту

	float BtnW = ListCol.w - 2.0f * ContentPadding;
	float bx = ListCol.x + ContentPadding;
	float by = ListCol.y + ContentPadding;
	// Проверяем, помещаются ли все кнопки в доступное пространство
	float TotalNeededHeight = std::size(s_aElems) * BtnH + TotalSpacing + 2.0f * ContentPadding;
	bool UseScrolling = TotalNeededHeight > ListCol.h;

	for(unsigned i = 0; i < std::size(s_aElems); ++i)
	{
		float CurrentY = by + i * (BtnH + BtnSpacing);

		// Пропускаем кнопки, которые не помещаются в видимую область
		if(UseScrolling && (CurrentY + BtnH > ListCol.y + ListCol.h - ContentPadding))
			break;

		CUIRect Btn = {bx, CurrentY, BtnW, BtnH};
		static CButtonContainer s_aAddElemButtons[32];
		char aLabel[32];
		str_format(aLabel, sizeof(aLabel), "%s  %s", s_aElems[i].pIcon, RCLocalize(s_aElems[i].pDesc, "Nameplate_Editor"));
		if(DoButton_Menu(&s_aAddElemButtons[i], aLabel, 0, &Btn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 16.0f, 0.0f, ColorRGBA(0.22f, 0.22f, 0.22f, 0.8f)))
		{
			std::vector<char> vScheme2;
			for(const char *p = g_Config.m_RiNamePlateScheme; *p; ++p)
				vScheme2.push_back(*p);
			vScheme2.push_back(s_aElems[i].Code);
			char aBuf[64] = {0};
			for(size_t j = 0; j < vScheme2.size() && j < sizeof(aBuf) - 1; ++j)
				aBuf[j] = vScheme2[j];
			str_copy(g_Config.m_RiNamePlateScheme, aBuf, sizeof(g_Config.m_RiNamePlateScheme));
			GameClient()->m_NamePlates.RiResetNameplatesPos(*GameClient(), g_Config.m_RiNamePlateScheme);
		}
	}

	// --- 2. Средняя колонка: вертикальный drag&drop редактор (схема) ---
	SchemeCol.Margin(MarginSmall, &SchemeCol);
	SchemeCol.Draw(ColorRGBA(0.1f, 0.1f, 0.1f, 0.7f), IGraphics::CORNER_ALL, 8.0f);
	std::vector<char> vScheme;
	for(const char *p = g_Config.m_RiNamePlateScheme; *p; ++p)
		vScheme.push_back(*p);

	// Улучшенные расчеты размеров карточек - адаптивное сжатие
	const float CardSpacing = MarginExtraSmall; // отступ между карточками
	const float SchemeContentPadding = MarginExtraSmall; // внутренние отступы

	int SchemeSize = std::max(1, (int)vScheme.size());
	float AvailableSchemeHeight = SchemeCol.h - 2.0f * SchemeContentPadding;
	float TotalCardSpacing = CardSpacing * (SchemeSize - 1);
	float CardH = (AvailableSchemeHeight - TotalCardSpacing) / (float)SchemeSize;

	// Адаптивное сжатие: если карточки слишком маленькие, уменьшаем отступы
	if(CardH < 20.0f && SchemeSize > 1)
	{
		float ReducedSpacing = std::max(1.0f, CardSpacing * 0.5f);
		TotalCardSpacing = ReducedSpacing * (SchemeSize - 1);
		CardH = (AvailableSchemeHeight - TotalCardSpacing) / (float)SchemeSize;
	}

	float CardW = SchemeCol.w - 2.0f * SchemeContentPadding;
	float cx = SchemeCol.x + SchemeContentPadding;
	float cy = SchemeCol.y + SchemeContentPadding;
	static int s_DragIdx = -1;
	static int s_HoverIdx = -1;

	// Используем фактический отступ (может быть уменьшен для адаптивного сжатия)
	float ActualCardSpacing = (SchemeSize > 1) ? (AvailableSchemeHeight - SchemeSize * CardH) / (SchemeSize - 1) : 0.0f;

	for(size_t i = 0; i < vScheme.size(); ++i)
	{
		float CurrentCardY = cy + i * (CardH + ActualCardSpacing);

		CUIRect Card = {cx, CurrentCardY, CardW, CardH};
		bool Hovered = Ui()->MouseInside(&Card);
		if(Hovered)
			s_HoverIdx = i;
		const char *pDesc = "?";
		const char *pIcon = "?";
		for(const auto &e : s_aElems)
			if(e.Code == vScheme[i])
			{
				pDesc = e.pDesc;
				pIcon = e.pIcon;
			}
		Card.Draw((s_DragIdx == int(i)) ? ColorRGBA(0.3f, 0.3f, 0.6f, 0.9f) : (Hovered ? ColorRGBA(0.2f, 0.2f, 0.2f, 0.9f) : ColorRGBA(0.18f, 0.18f, 0.18f, 0.8f)), IGraphics::CORNER_ALL, 6.0f);

		// Кнопка удаления - размещаем сначала, чтобы зарезервировать место
		CUIRect DelRect = Card;
		float DelSize = std::min(14.0f, CardH * 0.4f);
		DelRect.x += Card.w - DelSize - 2.0f;
		DelRect.y += 2.0f;
		DelRect.w = DelSize;
		DelRect.h = DelSize;

		// Уменьшаем область для контента, чтобы не накладываться на кнопку удаления
		CUIRect ContentArea = Card;
		ContentArea.w -= DelSize + 4.0f; // оставляем место для кнопки удаления

		// Адаптивное размещение иконки и текста в зависимости от высоты карточки
		CUIRect IconRect = ContentArea, TextRect = ContentArea;
		if(CardH >= 30.0f)
		{
			// Нормальное размещение для больших карточек
			IconRect.HSplitTop(CardH * 0.15f, nullptr, &IconRect);
			IconRect.HSplitTop(CardH * 0.35f, &IconRect, &TextRect);
			TextRect.HSplitTop(CardH * 0.35f, &TextRect, nullptr);

			Ui()->DoLabel(&IconRect, pIcon, std::min(18.0f, CardH * 0.4f), TEXTALIGN_MC);
			Ui()->DoLabel(&TextRect, RCLocalize(pDesc, "Nameplate_Editor"), std::min(10.0f, CardH * 0.25f), TEXTALIGN_MC);
		}
		else
		{
			// Компактное размещение для маленьких карточек
			char aBuf[32];
			str_format(aBuf, sizeof(aBuf), "%s  %s", pIcon, RCLocalize(pDesc, "Nameplate_Editor"));
			IconRect.HSplitTop(CardH * 0.1f, nullptr, &IconRect);
			IconRect.HSplitTop(CardH * 0.8f, &IconRect, nullptr);

			// Только иконка для очень маленьких карточек
			Ui()->DoLabel(&IconRect, aBuf, std::min(CardH * 0.6f, 16.0f), TEXTALIGN_MC);
		}
		static CButtonContainer s_aDelButtons[64];
		if(DoButton_Menu(&s_aDelButtons[i], "✖", 0, &DelRect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 8.0f, 0.0f, ColorRGBA(0.5f, 0.1f, 0.1f, 0.8f)))
		{
			vScheme.erase(vScheme.begin() + i);
			char aBuf[64] = {0};
			for(size_t j = 0; j < vScheme.size() && j < sizeof(aBuf) - 1; ++j)
				aBuf[j] = vScheme[j];
			str_copy(g_Config.m_RiNamePlateScheme, aBuf, sizeof(g_Config.m_RiNamePlateScheme));
			GameClient()->m_NamePlates.RiResetNameplatesPos(*GameClient(), g_Config.m_RiNamePlateScheme);
			return;
		}
		if(Ui()->MouseButton(0) && Hovered && s_DragIdx == -1)
			s_DragIdx = i;
	}
	if(s_DragIdx >= 0 && Ui()->MouseButton(0))
	{
		if(s_HoverIdx >= 0 && s_HoverIdx != s_DragIdx)
		{
			std::swap(vScheme[s_DragIdx], vScheme[s_HoverIdx]);
			char aBuf[64] = {0};
			for(size_t j = 0; j < vScheme.size() && j < sizeof(aBuf) - 1; ++j)
				aBuf[j] = vScheme[j];
			str_copy(g_Config.m_RiNamePlateScheme, aBuf, sizeof(g_Config.m_RiNamePlateScheme));
			GameClient()->m_NamePlates.RiResetNameplatesPos(*GameClient(), g_Config.m_RiNamePlateScheme);
			s_DragIdx = s_HoverIdx;
		}
	}
	else if(!Ui()->MouseButton(0))
	{
		s_DragIdx = -1;
		s_HoverIdx = -1;
	}

	// --- 3. Правая колонка: предпросмотр и чекбокс dummy ---
	CUIRect PreviewLabel, PreviewRect, DummyCheckRect;
	PreviewCol.HSplitTop(24.0f, &PreviewLabel, &PreviewCol);
	Ui()->DoLabel(&PreviewLabel, RCLocalize("Preview:"), 14.0f, TEXTALIGN_ML);
	PreviewCol.HSplitTop(PreviewCol.h - 84.0f, &PreviewRect, &PreviewCol); // 60+24=84, чтобы хватило места под строку схемы
	PreviewRect.Margin(6.0f, &PreviewRect);
	PreviewRect.Draw(ColorRGBA(0.12f, 0.12f, 0.12f, 0.7f), IGraphics::CORNER_ALL, 8.0f);
	static int showDummyPreview = false;
	PreviewCol.HSplitTop(24.0f, &DummyCheckRect, &PreviewCol);
	DoButton_CheckBoxAutoVMarginAndSet(&showDummyPreview, RCLocalize("Show dummy"), &showDummyPreview, &DummyCheckRect, 20.0f);
	vec2 PreviewPos = vec2(PreviewRect.x + PreviewRect.w / 2, PreviewRect.y + PreviewRect.h * 0.45f);
	GameClient()->m_NamePlates.RenderNamePlatePreview(PreviewPos, showDummyPreview ? 1 : 0);
	// --- Вывод схемы текстом ---
	static CLineInput s_CurrentScheme;
	s_CurrentScheme.SetBuffer(g_Config.m_RiNamePlateScheme, sizeof(g_Config.m_RiNamePlateScheme));
	CUIRect Label;
	PreviewCol.HSplitTop(LineSize, &Label, &PreviewCol);
	DoEditBoxWithLabel(&s_CurrentScheme, &Label, RCLocalize("Current scheme:"), "", g_Config.m_RiNamePlateScheme, sizeof(g_Config.m_RiNamePlateScheme));
	PreviewCol.HSplitTop(MarginSmall, nullptr, &PreviewCol);

	// --- Кнопки сброса (возвращаем к оригинальному виду) ---
	PreviewCol.HSplitTop(MarginSmall, nullptr, &PreviewCol);
	CUIRect ResetCol, ClearCol;
	static CButtonContainer s_ResetButton, s_ClearButton;
	PreviewCol.VSplitLeft(PreviewCol.w * 0.5f, &ResetCol, &ClearCol);
	ResetCol.VSplitRight(4.0f, &ResetCol, &Spacer);
	ClearCol.VSplitLeft(4.0f, &Spacer, &ClearCol);
	if(DoButton_Menu(&s_ResetButton, RCLocalize("Reset to default"), 0, &ResetCol, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 14.0f, 0.0f, ColorRGBA(0.3f, 0.1f, 0.1f, 0.8f)))
	{
		str_copy(g_Config.m_RiNamePlateScheme, "ICpifmnlclrlHFlslMlhldlV", sizeof(g_Config.m_RiNamePlateScheme));
		GameClient()->m_NamePlates.RiResetNameplatesPos(*GameClient(), g_Config.m_RiNamePlateScheme);
	}
	if(DoButton_Menu(&s_ClearButton, RCLocalize("Clear all"), 0, &ClearCol, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 14.0f, 0.0f, ColorRGBA(0.3f, 0.1f, 0.1f, 0.8f)))
	{
		g_Config.m_RiNamePlateScheme[0] = 0;
		GameClient()->m_NamePlates.RiResetNameplatesPos(*GameClient(), g_Config.m_RiNamePlateScheme);
	}
}

void CMenus::RenderSettingsRushieRCON(CUIRect MainView)
{
	// Add scroll region using the same pattern as other menus
	static CScrollRegion s_ScrollRegion;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 120.0f;
	ScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	s_ScrollRegion.Begin(&MainView, &ScrollOffset, &ScrollParams);

	MainView.y += ScrollOffset.y;

	// Add padding for scrollbar
	MainView.VSplitRight(5.0f, &MainView, nullptr);
	MainView.VSplitLeft(5.0f, nullptr, &MainView);

	CUIRect LeftView, RightView, Label;

	// auto DoBindchatDefault = [&](CUIRect &Column, CBindChat::CBindRclient &BindDefault) {
	// 	Column.HSplitTop(MarginSmall, nullptr, &Column);
	// 	Column.HSplitTop(LineSize, &Button, &Column);
	// 	CBindChat::CBind *pOldBind = GameClient()->m_BindChat.GetBind(BindDefault.m_Bind.m_aCommand);
	// 	static char s_aTempName[BINDCHAT_MAX_NAME] = "";
	// 	char *pName;
	// 	if(pOldBind == nullptr)
	// 		pName = s_aTempName;
	// 	else
	// 		pName = pOldBind->m_aName;
	// 	if(DoEditBoxWithLabel(&BindDefault.m_LineInput, &Button, RCLocalize(BindDefault.m_pTitle, "Chatbinds"), BindDefault.m_Bind.m_aName, pName, BINDCHAT_MAX_NAME) && BindDefault.m_LineInput.IsActive())
	// 	{
	// 		if(!pOldBind && pName[0] != '\0')
	// 		{
	// 			auto BindNew = BindDefault.m_Bind;
	// 			str_copy(BindNew.m_aName, pName);
	// 			GameClient()->m_BindChat.RemoveBind(pName); // Prevent duplicates
	// 			GameClient()->m_BindChat.AddBind(BindNew);
	// 			s_aTempName[0] = '\0';
	// 		}
	// 		if(pOldBind && pName[0] == '\0')
	// 		{
	// 			GameClient()->m_BindChat.RemoveBind(pName);
	// 		}
	// 	}
	// };

	// auto DoBindchatDefaults = [&](CUIRect &Column, const char *pTitle, std::vector<CBindChat::CBindRclient> &vBindchatDefaults) {
	// 	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	// 	Ui()->DoLabel(&Label, pTitle, HeadlineFontSize, TEXTALIGN_ML);
	// 	Column.HSplitTop(MarginSmall, nullptr, &Column);
	// 	for(CBindChat::CBindRclient &BindchatDefault : vBindchatDefaults)
	// 		DoBindchatDefault(Column, BindchatDefault);
	// 	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	// };

	// Split view into two columns
	CUIRect Column;
	MainView.VSplitMid(&LeftView, &RightView, 10.0f);

	// Left column - Find/Copy Skin/Player
	Column = LeftView;

	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Controls"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(MarginSmall, nullptr, &Column);
	static CButtonContainer s_ReaderButtonRCON, s_ClearButtonRCON;
	DoLine_KeyReader(Label, s_ReaderButtonRCON, s_ClearButtonRCON, RCLocalize("Admin Panel"), "toggle_adminpanel");

	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Adminpanel"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(MarginSmall, nullptr, &Column);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiPlaySounds, RCLocalize("Plays sound at exec command"), &g_Config.m_RiPlaySounds, &Column, LineSize);

	LeftView = Column;
	Column = RightView;

	RightView = Column;

	CUIRect ScrollRegion;
	ScrollRegion.x = MainView.x;
	ScrollRegion.y = maximum(LeftView.y, RightView.y) + MarginSmall * 2.0f;
	ScrollRegion.w = MainView.w;
	ScrollRegion.h = 0.0f;
	s_ScrollRegion.AddRect(ScrollRegion);
	s_ScrollRegion.End();
}

void CMenus::RenderSettingsRushieBindWheelSpec(CUIRect MainView)
{
	CUIRect LeftView, RightView, Label, Button;
	MainView.VSplitLeft(MainView.w / 2.1f, &LeftView, &RightView);

	const float Radius = minimum(RightView.w, RightView.h) / 2.0f;
	vec2 Center = RightView.Center();
	// Draw Circle
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.3f);
	Graphics()->DrawCircle(Center.x, Center.y, Radius, 64);
	Graphics()->QuadsEnd();

	static char s_aBindName[BINDWHEEL_MAX_NAME_RCLIENT];
	static char s_aBindCommand[BINDWHEEL_MAX_CMD_RCLIENT];

	static int s_SelectedBindIndex = -1;
	int HoveringIndex = -1;

	float MouseDist = distance(Center, Ui()->MousePos());
	const int SegmentCount = GameClient()->m_BindWheelSpec.m_vBinds.size();
	if (MouseDist < Radius && MouseDist > Radius * 0.25f && SegmentCount > 0)
	{
		float SegmentAngle = 2.0f * pi / SegmentCount;

		float HoveringAngle = angle(Ui()->MousePos() - Center) + SegmentAngle / 2.0f;
		if (HoveringAngle < 0.0f)
			HoveringAngle += 2.0f * pi;

		HoveringIndex = (int)(HoveringAngle / (2.0f * pi) * SegmentCount);
		HoveringIndex = std::clamp(HoveringIndex, 0, SegmentCount - 1);
		if (Ui()->MouseButtonClicked(0))
		{
			s_SelectedBindIndex = HoveringIndex;
			str_copy(s_aBindName, GameClient()->m_BindWheelSpec.m_vBinds[HoveringIndex].m_aName);
			str_copy(s_aBindCommand, GameClient()->m_BindWheelSpec.m_vBinds[HoveringIndex].m_aCommand);
		}
		else if (Ui()->MouseButtonClicked(1) && s_SelectedBindIndex >= 0 && HoveringIndex >= 0 && HoveringIndex != s_SelectedBindIndex)
		{
			CBindWheelSpec::CBind BindA = GameClient()->m_BindWheelSpec.m_vBinds[s_SelectedBindIndex];
			CBindWheelSpec::CBind BindB = GameClient()->m_BindWheelSpec.m_vBinds[HoveringIndex];
			str_copy(GameClient()->m_BindWheelSpec.m_vBinds[s_SelectedBindIndex].m_aName, BindB.m_aName);
			str_copy(GameClient()->m_BindWheelSpec.m_vBinds[s_SelectedBindIndex].m_aCommand, BindB.m_aCommand);
			str_copy(GameClient()->m_BindWheelSpec.m_vBinds[HoveringIndex].m_aName, BindA.m_aName);
			str_copy(GameClient()->m_BindWheelSpec.m_vBinds[HoveringIndex].m_aCommand, BindA.m_aCommand);
		}
		else if (Ui()->MouseButtonClicked(2))
		{
			s_SelectedBindIndex = HoveringIndex;
		}
	}
	else if (MouseDist < Radius && Ui()->MouseButtonClicked(0))
	{
		s_SelectedBindIndex = -1;
		str_copy(s_aBindName, "");
		str_copy(s_aBindCommand, "");
	}

	const float Theta = pi * 2.0f / std::max<float>(1.0f, GameClient()->m_BindWheelSpec.m_vBinds.size());
	for (int i = 0; i < static_cast<int>(GameClient()->m_BindWheelSpec.m_vBinds.size()); i++)
	{
		TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));

		float SegmentFontSize = FontSize * 1.1f;
		if (i == s_SelectedBindIndex)
		{
			SegmentFontSize = FontSize * 1.7f;
			TextRender()->TextColor(ColorRGBA(0.5f, 1.0f, 0.75f, 1.0f));
		}
		else if (i == HoveringIndex)
		{
			SegmentFontSize = FontSize * 1.35f;
		}

		const CBindWheelSpec::CBind Bind = GameClient()->m_BindWheelSpec.m_vBinds[i];
		const float Angle = Theta * i;

		const vec2 Pos = direction(Angle) * (Radius * 0.75f) + Center;
		const CUIRect Rect = CUIRect{ Pos.x - 50.0f, Pos.y - 50.0f, 100.0f, 100.0f };
		Ui()->DoLabel(&Rect, Bind.m_aName, SegmentFontSize, TEXTALIGN_MC);
	}

	TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));

	LeftView.HSplitTop(LineSize, &Button, &LeftView);
	Button.VSplitLeft(100.0f, &Label, &Button);
	Ui()->DoLabel(&Label, RCLocalize("Name:"), FontSize, TEXTALIGN_ML);
	static CLineInput s_NameInput;
	s_NameInput.SetBuffer(s_aBindName, sizeof(s_aBindName));
	s_NameInput.SetEmptyText(RCLocalize("Name"));
	Ui()->DoEditBox(&s_NameInput, &Button, EditBoxFontSize);

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize, &Button, &LeftView);
	Button.VSplitLeft(100.0f, &Label, &Button);
	Ui()->DoLabel(&Label, RCLocalize("Command:"), FontSize, TEXTALIGN_ML);
	static CLineInput s_BindInput;
	s_BindInput.SetBuffer(s_aBindCommand, sizeof(s_aBindCommand));
	s_BindInput.SetEmptyText(RCLocalize("Command"));
	Ui()->DoEditBox(&s_BindInput, &Button, EditBoxFontSize);

	static CButtonContainer s_AddButton, s_RemoveButton, s_OverrideButton;

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize, &Button, &LeftView);
	if (DoButton_Menu(&s_OverrideButton, RCLocalize("Override Selected"), 0, &Button) && s_SelectedBindIndex >= 0 && s_SelectedBindIndex < static_cast<int>(GameClient()->m_BindWheel.m_vBinds.size()))
	{
		CBindWheel::CBind TempBind;
		if (str_length(s_aBindName) == 0)
			str_copy(TempBind.m_aName, "*");
		else
			str_copy(TempBind.m_aName, s_aBindName);

		str_copy(GameClient()->m_BindWheelSpec.m_vBinds[s_SelectedBindIndex].m_aName, TempBind.m_aName);
		str_copy(GameClient()->m_BindWheelSpec.m_vBinds[s_SelectedBindIndex].m_aCommand, s_aBindCommand);
	}
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize, &Button, &LeftView);
	CUIRect ButtonAdd, ButtonRemove;
	Button.VSplitMid(&ButtonRemove, &ButtonAdd, MarginSmall);
	if(DoButton_Menu(&s_AddButton, RCLocalize("Add Bind"), 0, &ButtonAdd))
	{
		CBindWheel::CBind TempBind;
		if (str_length(s_aBindName) == 0)
			str_copy(TempBind.m_aName, "*");
		else
			str_copy(TempBind.m_aName, s_aBindName);

		GameClient()->m_BindWheelSpec.AddBind(TempBind.m_aName, s_aBindCommand);
		s_SelectedBindIndex = static_cast<int>(GameClient()->m_BindWheelSpec.m_vBinds.size()) - 1;
	}
	if(DoButton_Menu(&s_RemoveButton, RCLocalize("Remove Bind"), 0, &ButtonRemove) && s_SelectedBindIndex >= 0)
	{
		GameClient()->m_BindWheelSpec.RemoveBind(s_SelectedBindIndex);
		s_SelectedBindIndex = -1;
	}

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize, &Label, &LeftView);
	Ui()->DoLabel(&Label, RCLocalize("The command is ran in console not chat"), FontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(LineSize * 0.8f, &Label, &LeftView);
	Ui()->DoLabel(&Label, RCLocalize("Use left mouse to select"), FontSize * 0.8f, TEXTALIGN_ML);
	LeftView.HSplitTop(LineSize * 0.8f, &Label, &LeftView);
	Ui()->DoLabel(&Label, RCLocalize("Use right mouse to swap with selected"), FontSize * 0.8f, TEXTALIGN_ML);
	LeftView.HSplitTop(LineSize * 0.8f, &Label, &LeftView);
	Ui()->DoLabel(&Label, RCLocalize("Use middle mouse select without copy"), FontSize * 0.8f, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginBetweenSections, &Label, &LeftView);
	LeftView.HSplitTop(LineSize, &Label, &LeftView);
	TextRender()->TextColor(ColorRGBA(0.53f, 1.00f, 0.53f, 1.0f));
	Ui()->DoLabel(&Label, RCLocalize("Rclient bindwheel settings"), FontSize, TEXTALIGN_ML);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	LeftView.HSplitTop(LineSize, &Label, &LeftView);
	Ui()->DoLabel(&Label, RCLocalize("playernickname to enter nickname"), FontSize, TEXTALIGN_ML);
	{
		CUIRect Rightoffset;
		LeftView.VSplitLeft(25.0f, &Label, &Rightoffset);
		Rightoffset.HSplitTop(LineSize, &Label, &Rightoffset);
		TextRender()->TextColor(ColorRGBA(1.00f, 0.53f, 0.53f, 1.0f));
		Ui()->DoLabel(&Label, RCLocalize("Do \"playernickname\" yourself in u need"), FontSize, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
	LeftView.HSplitTop(LineSize, &Label, &LeftView);
	LeftView.HSplitTop(LineSize, &Label, &LeftView);
	Ui()->DoLabel(&Label, RCLocalize("playerid to enter id"), FontSize, TEXTALIGN_ML);


	LeftView.HSplitBottom(LineSize, &LeftView, &Label);
	static CButtonContainer s_ReaderButtonSpecWheel, s_ClearButtonSpecWheel;
	DoLine_KeyReader(Label, s_ReaderButtonSpecWheel, s_ClearButtonSpecWheel, RCLocalize("Bind Wheel In Spec Key"), "+bindwheel_spec");

	LeftView.HSplitBottom(LineSize, &LeftView, &Label);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcResetBindWheelMouse, RCLocalize("Reset position of mouse when opening bindwheel"), &g_Config.m_TcResetBindWheelMouse, &Label, LineSize);
}

bool CMenus::DoFloatScrollBar(const void *pId, int *pOption, const CUIRect *pRect, const char *pStr, int Min, int Max, int DivideBy, const IScrollbarScale *pScale, unsigned Flags, const char *pSuffix)
{
	const bool Infinite = Flags & CUi::SCROLLBAR_OPTION_INFINITE;
	const bool NoClampValue = Flags & CUi::SCROLLBAR_OPTION_NOCLAMPVALUE;
	const bool MultiLine = Flags & CUi::SCROLLBAR_OPTION_MULTILINE;

	int Value = *pOption;
	if(Infinite)
	{
		Max += 1;
		if(Value == 0)
			Value = Max;
	}

	// Allow adjustment of slider options when ctrl is pressed (to avoid scrolling, or accidentally adjusting the value)
	int Increment = std::max(1, (Max - Min) / 35);
	if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_UP) && Ui()->MouseInside(pRect))
	{
		Value += Increment;
		Value = std::clamp(Value, Min, Max);
	}
	if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN) && Ui()->MouseInside(pRect))
	{
		Value -= Increment;
		Value = std::clamp(Value, Min, Max);
	}
	if(Input()->KeyPress(KEY_A) && Ui()->MouseInside(pRect))
	{
		Value -= Input()->ModifierIsPressed() ? 5 : 1;
		Value = std::clamp(Value, Min, Max);
	}
	if(Input()->KeyPress(KEY_D) && Ui()->MouseInside(pRect))
	{
		Value += Input()->ModifierIsPressed() ? 5 : 1;
		Value = std::clamp(Value, Min, Max);
	}

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "%s: %.1f%s", pStr, (float)Value / DivideBy, pSuffix);

	Value = std::clamp(Value, Min, Max);

	CUIRect Label, ScrollBar;
	if(MultiLine)
		pRect->HSplitMid(&Label, &ScrollBar);
	else
		pRect->VSplitMid(&Label, &ScrollBar, minimum(10.0f, pRect->w * 0.05f));

	const float aFontSize = Label.h * CUi::ms_FontmodHeight * 0.8f;
	Ui()->DoLabel(&Label, aBuf, aFontSize, TEXTALIGN_ML);

	Value = pScale->ToAbsolute(Ui()->DoScrollbarH(pId, &ScrollBar, pScale->ToRelative(Value, Min, Max)), Min, Max);
	if(NoClampValue && ((Value == Min && *pOption < Min) || (Value == Max && *pOption > Max)))
	{
		Value = *pOption; // use previous out of range value instead if the scrollbar is at the edge
	}
	else if(Infinite)
	{
		if(Value == Max)
			Value = 0;
	}

	if(*pOption != Value)
	{
		*pOption = Value;
		return true;
	}
	return false;
}
