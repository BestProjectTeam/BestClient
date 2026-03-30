/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "countryflags.h"
#include "menus.h"
#include "skins.h"

#include <base/log.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/localization.h>
#include <engine/shared/protocol7.h>
#include <engine/storage.h>
#include <engine/textrender.h>
#include <engine/updater.h>

#include <generated/protocol.h>

#include <game/client/animstate.h>
#include <game/client/bc_ui_animations.h>
#include <game/client/components/chat.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/sounds.h>
#include <game/client/gameclient.h>
#include <game/client/skin.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <array>
#include <chrono>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

using namespace FontIcons;
using namespace std::chrono_literals;

namespace
{
enum
{
	SEARCH_BESTCLIENT_TAB_VISUALS = 0,
	SEARCH_BESTCLIENT_TAB_RACEFEATURES,
	SEARCH_BESTCLIENT_TAB_GORESFEATURES,
	SEARCH_BESTCLIENT_TAB_FUN,
	SEARCH_BESTCLIENT_TAB_SAVESYSTEM,
	SEARCH_BESTCLIENT_TAB_SHOP,
	SEARCH_BESTCLIENT_TAB_VOICE,
	SEARCH_BESTCLIENT_TAB_INFO,
};

constexpr int gs_BestClientSettingsBackgroundPosition = CMenuBackground::POS_SETTINGS_RESERVED0;

struct SSettingsSearchEntry
{
	const char *m_pNeedle;
	const char *m_pFocusKey;
	int m_Page;
	int m_GeneralSubTab;
	int m_AppearanceSubTab;
	int m_BestClientTab;
	int m_BestClientVisualsSubTab;
	int m_BestClientGoresCategory;
};

static const SSettingsSearchEntry gs_aSettingsSearchEntries[] = {
	{"general", "main_general_tab", CMenus::SETTINGS_GENERAL_TAB, -1, -1, -1, -1, -1},
	{"\u043E\u0441\u043D\u043E\u0432", "main_general_tab", CMenus::SETTINGS_GENERAL_TAB, -1, -1, -1, -1, -1},
	{"appearance", "main_appearance_tab", CMenus::SETTINGS_APPEARANCE_TAB, -1, -1, -1, -1, -1},
	{"\u043E\u0442\u043E\u0431\u0440\u0430\u0436", "main_appearance_tab", CMenus::SETTINGS_APPEARANCE_TAB, -1, -1, -1, -1, -1},
	{"tclient", "main_tclient_tab", CMenus::SETTINGS_TCLIENT, -1, -1, -1, -1, -1},
	{"\u0442\u043A\u043B\u0438", "main_tclient_tab", CMenus::SETTINGS_TCLIENT, -1, -1, -1, -1, -1},
	{"bestclient", "main_bestclient_tab", CMenus::SETTINGS_BESTCLIENT, -1, -1, -1, -1, -1},
	{"\u0431\u0435\u0441\u0442", "main_bestclient_tab", CMenus::SETTINGS_BESTCLIENT, -1, -1, -1, -1, -1},
	{"controls", "general_controls_tab", CMenus::SETTINGS_GENERAL_TAB, CMenus::GENERAL_SUBTAB_CONTROLS, -1, -1, -1, -1},
	{"\u0443\u043F\u0440\u0430\u0432\u043B", "general_controls_tab", CMenus::SETTINGS_GENERAL_TAB, CMenus::GENERAL_SUBTAB_CONTROLS, -1, -1, -1, -1},
	{"graphics", "appearance_graphics_tab", CMenus::SETTINGS_APPEARANCE_TAB, -1, CMenus::APPEARANCE_SUBTAB_GRAPHICS, -1, -1, -1},
	{"\u0433\u0440\u0430\u0444\u0438\u043A", "appearance_graphics_tab", CMenus::SETTINGS_APPEARANCE_TAB, -1, CMenus::APPEARANCE_SUBTAB_GRAPHICS, -1, -1, -1},
	{"sound", "appearance_sound_tab", CMenus::SETTINGS_APPEARANCE_TAB, -1, CMenus::APPEARANCE_SUBTAB_SOUND, -1, -1, -1},
	{"\u0437\u0432\u0443\u043A", "appearance_sound_tab", CMenus::SETTINGS_APPEARANCE_TAB, -1, CMenus::APPEARANCE_SUBTAB_SOUND, -1, -1, -1},
	{"ddnet", "general_ddnet_tab", CMenus::SETTINGS_GENERAL_TAB, CMenus::GENERAL_SUBTAB_DDNET, -1, -1, -1, -1},
	{"assets", "appearance_assets_tab", CMenus::SETTINGS_APPEARANCE_TAB, -1, CMenus::APPEARANCE_SUBTAB_ASSETS, -1, -1, -1},
	{"\u0440\u0435\u0441\u0443\u0440\u0441", "appearance_assets_tab", CMenus::SETTINGS_APPEARANCE_TAB, -1, CMenus::APPEARANCE_SUBTAB_ASSETS, -1, -1, -1},
	{"dynamic camera", "general_dynamic_camera", CMenus::SETTINGS_GENERAL_TAB, CMenus::GENERAL_SUBTAB_GENERAL, -1, -1, -1, -1},
	{"\u0434\u0438\u043D\u0430\u043C", "general_dynamic_camera", CMenus::SETTINGS_GENERAL_TAB, CMenus::GENERAL_SUBTAB_GENERAL, -1, -1, -1, -1},
	{"client", "general_client", CMenus::SETTINGS_GENERAL_TAB, CMenus::GENERAL_SUBTAB_GENERAL, -1, -1, -1, -1},
	{"\u043A\u043B\u0438\u0435\u043D\u0442", "general_client", CMenus::SETTINGS_GENERAL_TAB, CMenus::GENERAL_SUBTAB_GENERAL, -1, -1, -1, -1},
	{"language", "general_language", CMenus::SETTINGS_GENERAL_TAB, CMenus::GENERAL_SUBTAB_GENERAL, -1, -1, -1, -1},
	{"\u044F\u0437\u044B\u043A", "general_language", CMenus::SETTINGS_GENERAL_TAB, CMenus::GENERAL_SUBTAB_GENERAL, -1, -1, -1, -1},
	{"tee", "general_tee_subtab", CMenus::SETTINGS_GENERAL_TAB, CMenus::GENERAL_SUBTAB_TEE, -1, -1, -1, -1},
	{"\u0441\u043A\u0438\u043D", "general_tee_subtab", CMenus::SETTINGS_GENERAL_TAB, CMenus::GENERAL_SUBTAB_TEE, -1, -1, -1, -1},
	{"afterimage", "Afterimage", CMenus::SETTINGS_BESTCLIENT, -1, -1, SEARCH_BESTCLIENT_TAB_VISUALS, 0, -1},
	{"\u0430\u0444\u0442\u0435\u0440", "Afterimage", CMenus::SETTINGS_BESTCLIENT, -1, -1, SEARCH_BESTCLIENT_TAB_VISUALS, 0, -1},
	{"\u043F\u043E\u0441\u043B\u0435\u0441\u0432", "Afterimage", CMenus::SETTINGS_BESTCLIENT, -1, -1, SEARCH_BESTCLIENT_TAB_VISUALS, 0, -1},
	{"background", "Media Background", CMenus::SETTINGS_BESTCLIENT, -1, -1, SEARCH_BESTCLIENT_TAB_VISUALS, 0, -1},
	{"media background", "Media Background", CMenus::SETTINGS_BESTCLIENT, -1, -1, SEARCH_BESTCLIENT_TAB_VISUALS, 0, -1},
	{"video background", "Media Background", CMenus::SETTINGS_BESTCLIENT, -1, -1, SEARCH_BESTCLIENT_TAB_VISUALS, 0, -1},
	{"photo background", "Media Background", CMenus::SETTINGS_BESTCLIENT, -1, -1, SEARCH_BESTCLIENT_TAB_VISUALS, 0, -1},
	{"\u0444\u043E\u043D", "Media Background", CMenus::SETTINGS_BESTCLIENT, -1, -1, SEARCH_BESTCLIENT_TAB_VISUALS, 0, -1},
	{"\u0432\u0438\u0434\u0435\u043E", "Media Background", CMenus::SETTINGS_BESTCLIENT, -1, -1, SEARCH_BESTCLIENT_TAB_VISUALS, 0, -1},
	{"\u0444\u043E\u0442\u043E", "Media Background", CMenus::SETTINGS_BESTCLIENT, -1, -1, SEARCH_BESTCLIENT_TAB_VISUALS, 0, -1},
	{"fast input", "Input", CMenus::SETTINGS_BESTCLIENT, -1, -1, SEARCH_BESTCLIENT_TAB_GORESFEATURES, -1, 0},
	{"\u0431\u044B\u0441\u0442\u0440", "Input", CMenus::SETTINGS_BESTCLIENT, -1, -1, SEARCH_BESTCLIENT_TAB_GORESFEATURES, -1, 0},
	{"input", "Input", CMenus::SETTINGS_BESTCLIENT, -1, -1, SEARCH_BESTCLIENT_TAB_GORESFEATURES, -1, 0},
	{"hook combo", "Hook Combo", CMenus::SETTINGS_BESTCLIENT, -1, -1, SEARCH_BESTCLIENT_TAB_GORESFEATURES, -1, 0},
	{"hookcombo", "Hook Combo", CMenus::SETTINGS_BESTCLIENT, -1, -1, SEARCH_BESTCLIENT_TAB_GORESFEATURES, -1, 0},
	{"\u0445\u0443\u043A \u043A\u043E\u043C\u0431\u043E", "Hook Combo", CMenus::SETTINGS_BESTCLIENT, -1, -1, SEARCH_BESTCLIENT_TAB_GORESFEATURES, -1, 0},
	{"auto team lock", "Auto Team Lock", CMenus::SETTINGS_BESTCLIENT, -1, -1, SEARCH_BESTCLIENT_TAB_RACEFEATURES, -1, -1},
	{"autolock", "Auto Team Lock", CMenus::SETTINGS_BESTCLIENT, -1, -1, SEARCH_BESTCLIENT_TAB_RACEFEATURES, -1, -1},
	{"\u0430\u0432\u0442\u043E\u043B\u043E\u043A", "Auto Team Lock", CMenus::SETTINGS_BESTCLIENT, -1, -1, SEARCH_BESTCLIENT_TAB_RACEFEATURES, -1, -1},
	{"autoswitch", "general_autoswitch", CMenus::SETTINGS_GENERAL_TAB, CMenus::GENERAL_SUBTAB_GENERAL, -1, -1, -1, -1},
	{"switch weapon", "general_autoswitch", CMenus::SETTINGS_GENERAL_TAB, CMenus::GENERAL_SUBTAB_GENERAL, -1, -1, -1, -1},
	{"bindsystem", "BindSystem", CMenus::SETTINGS_BESTCLIENT, -1, -1, SEARCH_BESTCLIENT_TAB_RACEFEATURES, -1, -1},
	{"speedrun", "Speedrun Timer", CMenus::SETTINGS_BESTCLIENT, -1, -1, SEARCH_BESTCLIENT_TAB_RACEFEATURES, -1, -1},
	{"\u0442\u0430\u0439\u043C\u0435\u0440", "Speedrun Timer", CMenus::SETTINGS_BESTCLIENT, -1, -1, SEARCH_BESTCLIENT_TAB_RACEFEATURES, -1, -1},
	{"voice", "Voice chat", CMenus::SETTINGS_BESTCLIENT, -1, -1, SEARCH_BESTCLIENT_TAB_VOICE, -1, -1},
	{"voice chat", "Voice chat", CMenus::SETTINGS_BESTCLIENT, -1, -1, SEARCH_BESTCLIENT_TAB_VOICE, -1, -1},
	{"microphone", "Voice chat", CMenus::SETTINGS_BESTCLIENT, -1, -1, SEARCH_BESTCLIENT_TAB_VOICE, -1, -1},
	{"headphones", "Voice chat", CMenus::SETTINGS_BESTCLIENT, -1, -1, SEARCH_BESTCLIENT_TAB_VOICE, -1, -1},
	{"ptt", "Voice chat", CMenus::SETTINGS_BESTCLIENT, -1, -1, SEARCH_BESTCLIENT_TAB_VOICE, -1, -1},
	{"\u0433\u043E\u043B\u043E\u0441", "Voice chat", CMenus::SETTINGS_BESTCLIENT, -1, -1, SEARCH_BESTCLIENT_TAB_VOICE, -1, -1},
};

enum ESettingsAutoScrollPage
{
	SETTINGS_SCROLL_PAGE_GENERAL = 0,
	SETTINGS_SCROLL_PAGE_APPEARANCE,
	SETTINGS_SCROLL_PAGE_GRAPHICS,
	SETTINGS_SCROLL_PAGE_SOUND,
	SETTINGS_SCROLL_PAGE_DDNET,
	NUM_SETTINGS_SCROLL_PAGES,
};

static constexpr float gs_MenuPreviewTeeLarge = 64.0f;

static void SearchNormalizeText(const char *pInput, char *pOutput, int OutputSize)
{
	if(pOutput == nullptr || OutputSize <= 0)
		return;
	if(pInput == nullptr)
	{
		pOutput[0] = '\0';
		return;
	}

	char aLower[256];
	str_utf8_tolower(pInput, aLower, sizeof(aLower));

	int OutPos = 0;
	for(const char *pIt = aLower; *pIt != '\0' && OutPos < OutputSize - 1;)
	{
		const char *pNext = pIt;
		const int Code = str_utf8_decode(&pNext);
		if(Code <= 0)
			break;

		// Ignore separators so "status bar" matches "statusbar", etc.
		if(str_utf8_isspace(Code) || Code == '-' || Code == '_' || Code == '.' || Code == '/' || Code == '\\')
		{
			pIt = pNext;
			continue;
		}

		char aEncoded[8];
		const int EncodedSize = str_utf8_encode(aEncoded, Code);
		if(EncodedSize <= 0 || OutPos + EncodedSize >= OutputSize)
			break;
		mem_copy(pOutput + OutPos, aEncoded, EncodedSize);
		OutPos += EncodedSize;
		pIt = pNext;
	}

	pOutput[OutPos] = '\0';
}

static int SearchMatchScoreInternal(const char *pQuery, const char *pCandidate)
{
	if(pQuery == nullptr || pCandidate == nullptr || pQuery[0] == '\0' || pCandidate[0] == '\0')
		return -1;

	const int QueryLength = str_length(pQuery);
	const int CandidateLength = str_length(pCandidate);
	if(str_utf8_comp_nocase(pQuery, pCandidate) == 0)
		return 1600;

	if(const char *pAt = str_utf8_find_nocase(pCandidate, pQuery))
	{
		int Score = pAt == pCandidate ? 1450 : 1250;
		Score -= minimum(220, maximum(0, CandidateLength - QueryLength));
		Score -= minimum(180, (int)(pAt - pCandidate));
		return Score;
	}

	return -1;
}

static int SearchMatchScore(const char *pQuery, const char *pCandidate)
{
	const int DirectScore = SearchMatchScoreInternal(pQuery, pCandidate);

	char aQueryNormalized[256];
	char aCandidateNormalized[256];
	SearchNormalizeText(pQuery, aQueryNormalized, sizeof(aQueryNormalized));
	SearchNormalizeText(pCandidate, aCandidateNormalized, sizeof(aCandidateNormalized));

	int NormalizedScore = -1;
	if(aQueryNormalized[0] != '\0' && aCandidateNormalized[0] != '\0')
	{
		NormalizedScore = SearchMatchScoreInternal(aQueryNormalized, aCandidateNormalized);
		if(NormalizedScore > 0)
			NormalizedScore -= 80;
	}

	return maximum(DirectScore, NormalizedScore);
}

static const SSettingsSearchEntry *FindBestSettingsSearchEntry(const char *pQuery, int *pBestScore = nullptr, int *pSecondBestScore = nullptr)
{
	const SSettingsSearchEntry *pBest = nullptr;
	int BestScore = -1;
	int SecondBestScore = -1;
	for(const SSettingsSearchEntry &Entry : gs_aSettingsSearchEntries)
	{
		const int Score = SearchMatchScore(pQuery, Entry.m_pNeedle);
		if(Score > BestScore || (Score == BestScore && pBest != nullptr && str_length(Entry.m_pNeedle) < str_length(pBest->m_pNeedle)))
		{
			SecondBestScore = BestScore;
			BestScore = Score;
			pBest = &Entry;
		}
		else if(Score > SecondBestScore)
		{
			SecondBestScore = Score;
		}
	}
	if(pBestScore != nullptr)
		*pBestScore = BestScore;
	if(pSecondBestScore != nullptr)
		*pSecondBestScore = SecondBestScore;
	return pBest;
}

static bool SearchShouldNavigateToEntry(const char *pQuery, int BestScore, int SecondBestScore)
{
	if(pQuery == nullptr || pQuery[0] == '\0' || BestScore < 0)
		return false;

	const int QueryLength = str_length(pQuery);
	if(QueryLength <= 2)
		return BestScore >= 1600;

	if(BestScore >= 1600)
		return true;

	// Avoid jumping to random tabs for broad/ambiguous live search input.
	if(BestScore < 1400)
		return false;
	if(BestScore - maximum(SecondBestScore, 0) < 100)
		return false;
	if(QueryLength <= 3 && BestScore < 1450)
		return false;

	return true;
}
}

void CMenus::RenderAutoScrollSettingsPage(CUIRect MainView, int ScrollPage, const std::function<float(CUIRect)> &RenderFn)
{
	dbg_assert(ScrollPage >= 0 && ScrollPage < NUM_SETTINGS_SCROLL_PAGES, "invalid settings scroll page");

	static CScrollRegion s_aScrollRegions[NUM_SETTINGS_SCROLL_PAGES];

	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;

	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegion &ScrollRegion = s_aScrollRegions[ScrollPage];
	ScrollRegion.Begin(&MainView, &ScrollOffset, &ScrollParams);
	m_pSettingsSearchActiveScrollRegion = &ScrollRegion;

	CUIRect ContentView = MainView;
	ContentView.y += ScrollOffset.y;
	const float ContentStartY = ContentView.y;
	const float ContentBottom = maximum(RenderFn(ContentView), ContentStartY);

	CUIRect ContentRect = ContentView;
	ContentRect.y = ContentStartY;
	ContentRect.h = ContentBottom - ContentStartY;
	ScrollRegion.AddRect(ContentRect);
	ScrollRegion.End();

	m_pSettingsSearchActiveScrollRegion = nullptr;
}

bool CMenus::SettingsSearchHasText() const
{
	return m_SettingsSearchOpen && !m_SettingsSearchInput.IsEmpty();
}

void CMenus::OpenSettingsSearch()
{
	m_SettingsSearchOpen = true;
	Ui()->SetActiveItem(&m_SettingsSearchInput);
	m_SettingsSearchInput.Activate(EInputPriority::UI);
}

bool CMenus::SettingsSearchMatchesText(const char *pText) const
{
	if(!SettingsSearchHasText() || pText == nullptr || pText[0] == '\0')
		return false;
	return SearchMatchScore(m_SettingsSearchInput.GetString(), pText) >= 0;
}

bool CMenus::SettingsSearchLabelMatchCallback(void *pUser, const char *pText, const CUIRect *pRect)
{
	CMenus *pMenus = static_cast<CMenus *>(pUser);
	return pMenus != nullptr && pMenus->SettingsSearchHandleLabelMatch(pText, pRect);
}

bool CMenus::SettingsSearchHandleLabelMatch(const char *pText, const CUIRect *pRect)
{
	if(!SettingsSearchMatchesText(pText))
		return false;
	if(m_SettingsSearchRevealPending && m_pSettingsSearchActiveScrollRegion != nullptr && pRect != nullptr)
	{
		m_pSettingsSearchActiveScrollRegion->AddRect(*pRect);
		m_pSettingsSearchActiveScrollRegion->ScrollHere(CScrollRegion::SCROLLHERE_KEEP_IN_VIEW);
	}
	return true;
}



bool CMenus::SettingsSearchIsHighlight(const char *pFocusKey) const
{
	return SettingsSearchHasText() && pFocusKey != nullptr && pFocusKey[0] != '\0' &&
		str_comp_nocase(m_aSettingsSearchFocusKey, pFocusKey) == 0;
}

bool CMenus::SettingsSearchConsumeReveal(const char *pFocusKey)
{
	if(!m_SettingsSearchRevealPending || !SettingsSearchIsHighlight(pFocusKey))
		return false;

	// Keep reveal active for a few frames so scroll regions can reliably reach
	// deeply nested sections on slower/eased scrolling.
	if(m_SettingsSearchRevealFrames > 0)
		--m_SettingsSearchRevealFrames;
	if(m_SettingsSearchRevealFrames <= 0)
	{
		m_SettingsSearchRevealPending = false;
		m_SettingsSearchRevealFrames = 0;
	}
	return true;
}

void CMenus::ResetSettingsSearch()
{
	m_SettingsSearchOpen = false;
	m_SettingsSearchInput.Clear();
	m_aSettingsSearchLastApplied[0] = '\0';
	SettingsSearchSetFocusKey("");
	m_SettingsSearchRevealPending = false;
	m_SettingsSearchRevealFrames = 0;
}

void CMenus::SettingsSearchSetFocusKey(const char *pFocusKey)
{
	if(pFocusKey != nullptr)
		str_copy(m_aSettingsSearchFocusKey, pFocusKey, sizeof(m_aSettingsSearchFocusKey));
	else
		m_aSettingsSearchFocusKey[0] = '\0';
}

void CMenus::SettingsSearchDrawLabel(const CUIRect *pRect, const char *pText, float Size, int Align, const char *pFocusKey)
{
	if(SettingsSearchIsHighlight(pFocusKey) || SettingsSearchMatchesText(pText))
	{
		CUIRect Highlight = *pRect;
		Highlight.Draw(ColorRGBA(1.0f, 0.9f, 0.25f, 0.22f), IGraphics::CORNER_ALL, 4.0f);
		TextRender()->TextColor(ColorRGBA(1.0f, 0.95f, 0.65f, 1.0f));
		Ui()->DoLabel(pRect, pText, Size, Align);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
	else
	{
		Ui()->DoLabel(pRect, pText, Size, Align);
	}
}

CUi::EPopupMenuFunctionResult CMenus::PopupSettingsCountrySelection(void *pContext, CUIRect View, bool Active)
{
	SPopupSettingsCountrySelectionContext *pPopupContext = static_cast<SPopupSettingsCountrySelectionContext *>(pContext);
	CMenus *pMenus = pPopupContext->m_pMenus;

	static CListBox s_ListBox;
	s_ListBox.SetActive(Active);
	s_ListBox.DoStart(50.0f, pMenus->GameClient()->m_CountryFlags.Num(), 8, 1, -1, &View, false);

	if(pPopupContext->m_New)
	{
		pPopupContext->m_New = false;
		s_ListBox.ScrollToSelected();
	}

	for(size_t i = 0; i < pMenus->GameClient()->m_CountryFlags.Num(); ++i)
	{
		const CCountryFlags::CCountryFlag &Entry = pMenus->GameClient()->m_CountryFlags.GetByIndex(i);

		const CListboxItem Item = s_ListBox.DoNextItem(&Entry, Entry.m_CountryCode == pPopupContext->m_Selection);
		if(!Item.m_Visible)
			continue;

		CUIRect FlagRect, Label;
		Item.m_Rect.Margin(5.0f, &FlagRect);
		FlagRect.HSplitBottom(12.0f, &FlagRect, &Label);
		Label.HSplitTop(2.0f, nullptr, &Label);
		const float OldWidth = FlagRect.w;
		FlagRect.w = FlagRect.h * 2.0f;
		FlagRect.x += (OldWidth - FlagRect.w) / 2.0f;
		pMenus->GameClient()->m_CountryFlags.Render(Entry.m_CountryCode, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), FlagRect.x, FlagRect.y, FlagRect.w, FlagRect.h);

		pMenus->Ui()->DoLabel(&Label, Entry.m_aCountryCodeString, 10.0f, TEXTALIGN_MC);
	}

	const int NewSelected = s_ListBox.DoEnd();
	pPopupContext->m_Selection = NewSelected >= 0 ? pMenus->GameClient()->m_CountryFlags.GetByIndex(NewSelected).m_CountryCode : -1;
	if(s_ListBox.WasItemSelected() || s_ListBox.WasItemActivated())
	{
		if(pPopupContext->m_pCountry != nullptr)
		{
			*pPopupContext->m_pCountry = pPopupContext->m_Selection;
			pMenus->SetNeedSendInfo();
		}
		return CUi::POPUP_CLOSE_CURRENT;
	}

	return CUi::POPUP_KEEP_OPEN;
}

float CMenus::RenderSettingsGeneral(CUIRect MainView)
{
	const float BaseContentBottom = MainView.y + MainView.h;
	float ContentBottom = BaseContentBottom;
	char aBuf[128 + IO_MAX_PATH_LENGTH];
	CUIRect Label, Button, Left, Right, Game, ClientSettings;
	MainView.HSplitTop(190.0f, &Game, &ClientSettings);

	// game
	{
		// headline
		CUIRect GameLabel, LanguageLabel;
		Game.HSplitTop(30.0f, &Label, &Game);
		Label.VSplitMid(&GameLabel, &LanguageLabel, 20.0f);
		SettingsSearchDrawLabel(&GameLabel, Localize("Game"), 20.0f, TEXTALIGN_ML, "general_dynamic_camera");
		SettingsSearchDrawLabel(&LanguageLabel, Localize("Language"), 20.0f, TEXTALIGN_ML, "general_language");
		Game.HSplitTop(2.5f, nullptr, &Game);
		Game.VSplitMid(&Left, &Right, 20.0f);

		// dynamic camera
		Left.HSplitTop(20.0f, &Button, &Left);
		if(SettingsSearchIsHighlight("general_dynamic_camera"))
			Button.Draw(ColorRGBA(1.0f, 0.9f, 0.25f, 0.18f), IGraphics::CORNER_ALL, 3.0f);
		const bool IsDyncam = g_Config.m_ClDyncam || g_Config.m_ClMouseFollowfactor > 0;
		if(DoButton_CheckBox(&g_Config.m_ClDyncam, Localize("Dynamic Camera"), IsDyncam, &Button))
		{
			if(IsDyncam)
			{
				g_Config.m_ClDyncam = 0;
				g_Config.m_ClMouseFollowfactor = 0;
			}
			else
			{
				g_Config.m_ClDyncam = 1;
			}
		}

		// smooth dynamic camera
		Left.HSplitTop(5.0f, nullptr, &Left);
		Left.HSplitTop(20.0f, &Button, &Left);
		if(g_Config.m_ClDyncam)
		{
			if(DoButton_CheckBox(&g_Config.m_ClDyncamSmoothness, Localize("Smooth Dynamic Camera"), g_Config.m_ClDyncamSmoothness, &Button))
			{
				if(g_Config.m_ClDyncamSmoothness)
				{
					g_Config.m_ClDyncamSmoothness = 0;
				}
				else
				{
					g_Config.m_ClDyncamSmoothness = 50;
					g_Config.m_ClDyncamStabilizing = 50;
				}
			}
		}

		// weapon pickup
		Left.HSplitTop(5.0f, nullptr, &Left);
		Left.HSplitTop(20.0f, &Button, &Left);
		if(SettingsSearchIsHighlight("general_autoswitch"))
			Button.Draw(ColorRGBA(1.0f, 0.9f, 0.25f, 0.18f), IGraphics::CORNER_ALL, 3.0f);
		if(DoButton_CheckBox(&g_Config.m_ClAutoswitchWeapons, Localize("Switch weapon on pickup"), g_Config.m_ClAutoswitchWeapons, &Button))
			g_Config.m_ClAutoswitchWeapons ^= 1;

		// weapon out of ammo autoswitch
		Left.HSplitTop(5.0f, nullptr, &Left);
		Left.HSplitTop(20.0f, &Button, &Left);
		if(SettingsSearchIsHighlight("general_autoswitch"))
			Button.Draw(ColorRGBA(1.0f, 0.9f, 0.25f, 0.18f), IGraphics::CORNER_ALL, 3.0f);
		if(DoButton_CheckBox(&g_Config.m_ClAutoswitchWeaponsOutOfAmmo, Localize("Switch weapon when out of ammo"), g_Config.m_ClAutoswitchWeaponsOutOfAmmo, &Button))
			g_Config.m_ClAutoswitchWeaponsOutOfAmmo ^= 1;

		// language selection (placed in upper-right area)
		Right.HSplitTop(2.5f, nullptr, &Right);
		ContentBottom = maximum(ContentBottom, Right.y + Right.h);
		RenderLanguageSelection(Right);
	}

	// client
	{
		// headline
		ClientSettings.HSplitTop(30.0f, &Label, &ClientSettings);
		SettingsSearchDrawLabel(&Label, Localize("Client"), 20.0f, TEXTALIGN_ML, "general_client");
		ClientSettings.HSplitTop(5.0f, nullptr, &ClientSettings);
		ClientSettings.VSplitMid(&Left, &Right, 20.0f);

		// skip main menu
		Left.HSplitTop(20.0f, &Button, &Left);
		if(DoButton_CheckBox(&g_Config.m_ClSkipStartMenu, Localize("Skip the main menu"), g_Config.m_ClSkipStartMenu, &Button))
			g_Config.m_ClSkipStartMenu ^= 1;

		Left.HSplitTop(10.0f, nullptr, &Left);
		Left.HSplitTop(20.0f, &Button, &Left);
		str_copy(aBuf, " ");
		str_append(aBuf, Localize("Hz", "Hertz"));
		Ui()->DoScrollbarOption(&g_Config.m_ClRefreshRate, &g_Config.m_ClRefreshRate, &Button, Localize("Refresh Rate"), 10, 1000, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_INFINITE | CUi::SCROLLBAR_OPTION_NOCLAMPVALUE | CUi::SCROLLBAR_OPTION_DELAYUPDATE, aBuf);
		Left.HSplitTop(5.0f, nullptr, &Left);
		Left.HSplitTop(20.0f, &Button, &Left);
		static int s_LowerRefreshRate;
		if(DoButton_CheckBox(&s_LowerRefreshRate, Localize("Save power by lowering refresh rate (higher input latency)"), g_Config.m_ClRefreshRate <= 480 && g_Config.m_ClRefreshRate != 0, &Button))
			g_Config.m_ClRefreshRate = g_Config.m_ClRefreshRate > 480 || g_Config.m_ClRefreshRate == 0 ? 480 : 0;

		Left.HSplitTop(10.0f, nullptr, &Left);
		CUIRect ThemeSelection = Left;
		ThemeSelection.h = 210.0f;
		RenderThemeSelection(ThemeSelection);
		ContentBottom = maximum(ContentBottom, ThemeSelection.y + ThemeSelection.h);

		CUIRect SettingsButton = Left;
		SettingsButton.y = ThemeSelection.y + ThemeSelection.h + 10.0f;
		SettingsButton.h = 20.0f;
		static CButtonContainer s_SettingsButtonId;
		if(DoButton_Menu(&s_SettingsButtonId, Localize("Settings file"), 0, &SettingsButton))
		{
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::DDNET].m_aConfigPath, aBuf, sizeof(aBuf));
			Client()->ViewFile(aBuf);
		}
		GameClient()->m_Tooltips.DoToolTip(&s_SettingsButtonId, &SettingsButton, Localize("Open the settings file"));

		CUIRect SavesButton = SettingsButton;
		SavesButton.y += 25.0f;
		static CButtonContainer s_SavesButtonId;
		if(DoButton_Menu(&s_SavesButtonId, Localize("Saves file"), 0, &SavesButton))
		{
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, SAVES_FILE, aBuf, sizeof(aBuf));
			Client()->ViewFile(aBuf);
		}
		GameClient()->m_Tooltips.DoToolTip(&s_SavesButtonId, &SavesButton, Localize("Open the saves file"));

		CUIRect ConfigButton = SavesButton;
		ConfigButton.y += 25.0f;
		static CButtonContainer s_ConfigButtonId;
		if(DoButton_Menu(&s_ConfigButtonId, Localize("Config directory"), 0, &ConfigButton))
		{
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, "", aBuf, sizeof(aBuf));
			Client()->ViewFile(aBuf);
		}
		GameClient()->m_Tooltips.DoToolTip(&s_ConfigButtonId, &ConfigButton, Localize("Open the directory that contains the configuration and user files"));

		CUIRect DirectoryButton = ConfigButton;
		DirectoryButton.y += 25.0f;
		static CButtonContainer s_ThemesButtonId;
		if(DoButton_Menu(&s_ThemesButtonId, Localize("Themes directory"), 0, &DirectoryButton))
		{
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, "themes", aBuf, sizeof(aBuf));
			Storage()->CreateFolder("themes", IStorage::TYPE_SAVE);
			Client()->ViewFile(aBuf);
		}
		GameClient()->m_Tooltips.DoToolTip(&s_ThemesButtonId, &DirectoryButton, Localize("Open the directory to add custom themes"));
		ContentBottom = maximum(ContentBottom, DirectoryButton.y + DirectoryButton.h);

		// auto demo settings
		{
			Right.HSplitTop(40.0f, nullptr, &Right);
			Right.HSplitTop(20.0f, &Button, &Right);
			if(DoButton_CheckBox(&g_Config.m_ClAutoDemoRecord, Localize("Automatically record demos"), g_Config.m_ClAutoDemoRecord, &Button))
				g_Config.m_ClAutoDemoRecord ^= 1;

			if(g_Config.m_ClAutoDemoRecord)
			{
				Right.HSplitTop(2 * 20.0f, &Button, &Right);
				Ui()->DoScrollbarOption(&g_Config.m_ClAutoDemoMax, &g_Config.m_ClAutoDemoMax, &Button, Localize("Max demos"), 1, 1000, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_INFINITE | CUi::SCROLLBAR_OPTION_MULTILINE);
			}

			Right.HSplitTop(10.0f, nullptr, &Right);
			Right.HSplitTop(20.0f, &Button, &Right);
			if(DoButton_CheckBox(&g_Config.m_ClAutoScreenshot, Localize("Automatically take game over screenshot"), g_Config.m_ClAutoScreenshot, &Button))
				g_Config.m_ClAutoScreenshot ^= 1;

			if(g_Config.m_ClAutoScreenshot)
			{
				Right.HSplitTop(2 * 20.0f, &Button, &Right);
				Ui()->DoScrollbarOption(&g_Config.m_ClAutoScreenshotMax, &g_Config.m_ClAutoScreenshotMax, &Button, Localize("Max Screenshots"), 1, 1000, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_INFINITE | CUi::SCROLLBAR_OPTION_MULTILINE);
			}
		}

		// auto statboard screenshot
		{
			Right.HSplitTop(10.0f, nullptr, &Right);
			Right.HSplitTop(20.0f, &Button, &Right);
			if(DoButton_CheckBox(&g_Config.m_ClAutoStatboardScreenshot, Localize("Automatically take statboard screenshot"), g_Config.m_ClAutoStatboardScreenshot, &Button))
			{
				g_Config.m_ClAutoStatboardScreenshot ^= 1;
			}

			if(g_Config.m_ClAutoStatboardScreenshot)
			{
				Right.HSplitTop(2 * 20.0f, &Button, &Right);
				Ui()->DoScrollbarOption(&g_Config.m_ClAutoStatboardScreenshotMax, &g_Config.m_ClAutoStatboardScreenshotMax, &Button, Localize("Max Screenshots"), 1, 1000, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_INFINITE | CUi::SCROLLBAR_OPTION_MULTILINE);
			}
		}

		// auto statboard csv
		{
			Right.HSplitTop(10.0f, nullptr, &Right);
			Right.HSplitTop(20.0f, &Button, &Right);
			if(DoButton_CheckBox(&g_Config.m_ClAutoCSV, Localize("Automatically create statboard csv"), g_Config.m_ClAutoCSV, &Button))
			{
				g_Config.m_ClAutoCSV ^= 1;
			}

			if(g_Config.m_ClAutoCSV)
			{
				Right.HSplitTop(2 * 20.0f, &Button, &Right);
				Ui()->DoScrollbarOption(&g_Config.m_ClAutoCSVMax, &g_Config.m_ClAutoCSVMax, &Button, Localize("Max CSVs"), 1, 1000, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_INFINITE | CUi::SCROLLBAR_OPTION_MULTILINE);
			}
		}
	}

	ContentBottom = maximum(ContentBottom, maximum(Left.y + Left.h, Right.y));
	return ContentBottom;
}

void CMenus::SetNeedSendInfo()
{
	if(m_Dummy)
		m_NeedSendDummyinfo = true;
	else
		m_NeedSendinfo = true;
}

void CMenus::RenderSettingsPlayer(CUIRect MainView)
{
	CUIRect TabBar, PlayerTab, DummyTab, ChangeInfo, QuickSearch;
	MainView.HSplitTop(20.0f, &TabBar, &MainView);
	TabBar.VSplitMid(&TabBar, &ChangeInfo, 20.f);
	TabBar.VSplitMid(&PlayerTab, &DummyTab);
	MainView.HSplitTop(10.0f, nullptr, &MainView);

	static CButtonContainer s_PlayerTabButton;
	if(DoButton_MenuTab(&s_PlayerTabButton, Localize("Player"), !m_Dummy, &PlayerTab, IGraphics::CORNER_L, nullptr, nullptr, nullptr, nullptr, 4.0f))
	{
		m_Dummy = false;
	}

	static CButtonContainer s_DummyTabButton;
	if(DoButton_MenuTab(&s_DummyTabButton, Localize("Dummy"), m_Dummy, &DummyTab, IGraphics::CORNER_R, nullptr, nullptr, nullptr, nullptr, 4.0f))
	{
		m_Dummy = true;
	}

	if(Client()->State() == IClient::STATE_ONLINE &&
		GameClient()->m_aNextChangeInfo[m_Dummy] > Client()->GameTick(m_Dummy))
	{
		char aChangeInfo[128], aTimeLeft[32];
		str_format(aTimeLeft, sizeof(aTimeLeft), Localize("%ds left"), (GameClient()->m_aNextChangeInfo[m_Dummy] - Client()->GameTick(m_Dummy) + Client()->GameTickSpeed() - 1) / Client()->GameTickSpeed());
		str_format(aChangeInfo, sizeof(aChangeInfo), "%s: %s", Localize("Player info change cooldown"), aTimeLeft);
		Ui()->DoLabel(&ChangeInfo, aChangeInfo, 10.f, TEXTALIGN_ML);
	}

	static CLineInput s_NameInput;
	static CLineInput s_ClanInput;

	int *pCountry;
	if(!m_Dummy)
	{
		pCountry = &g_Config.m_PlayerCountry;
		s_NameInput.SetBuffer(g_Config.m_PlayerName, sizeof(g_Config.m_PlayerName));
		s_NameInput.SetEmptyText(Client()->PlayerName());
		s_ClanInput.SetBuffer(g_Config.m_PlayerClan, sizeof(g_Config.m_PlayerClan));
	}
	else
	{
		pCountry = &g_Config.m_ClDummyCountry;
		s_NameInput.SetBuffer(g_Config.m_ClDummyName, sizeof(g_Config.m_ClDummyName));
		s_NameInput.SetEmptyText(Client()->DummyName());
		s_ClanInput.SetBuffer(g_Config.m_ClDummyClan, sizeof(g_Config.m_ClDummyClan));
	}

	// player name
	CUIRect Button, Label;
	MainView.HSplitTop(20.0f, &Button, &MainView);
	Button.VSplitLeft(80.0f, &Label, &Button);
	Button.VSplitLeft(150.0f, &Button, nullptr);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "%s:", Localize("Name"));
	Ui()->DoLabel(&Label, aBuf, 14.0f, TEXTALIGN_ML);
	if(Ui()->DoEditBox(&s_NameInput, &Button, 14.0f))
	{
		SetNeedSendInfo();
	}

	// player clan
	MainView.HSplitTop(5.0f, nullptr, &MainView);
	MainView.HSplitTop(20.0f, &Button, &MainView);
	Button.VSplitLeft(80.0f, &Label, &Button);
	Button.VSplitLeft(150.0f, &Button, nullptr);
	str_format(aBuf, sizeof(aBuf), "%s:", Localize("Clan"));
	Ui()->DoLabel(&Label, aBuf, 14.0f, TEXTALIGN_ML);
	if(Ui()->DoEditBox(&s_ClanInput, &Button, 14.0f))
	{
		SetNeedSendInfo();
	}

	// country flag selector
	static CLineInputBuffered<25> s_FlagFilterInput;

	std::vector<const CCountryFlags::CCountryFlag *> vpFilteredFlags;
	for(size_t i = 0; i < GameClient()->m_CountryFlags.Num(); ++i)
	{
		const CCountryFlags::CCountryFlag &Entry = GameClient()->m_CountryFlags.GetByIndex(i);
		if(!str_find_nocase(Entry.m_aCountryCodeString, s_FlagFilterInput.GetString()))
			continue;
		vpFilteredFlags.push_back(&Entry);
	}

	MainView.HSplitTop(10.0f, nullptr, &MainView);
	MainView.HSplitBottom(20.0f, &MainView, &QuickSearch);
	MainView.HSplitBottom(5.0f, &MainView, nullptr);
	QuickSearch.VSplitLeft(220.0f, &QuickSearch, nullptr);

	int SelectedOld = -1;
	static CListBox s_ListBox;
	s_ListBox.DoStart(48.0f, vpFilteredFlags.size(), 10, 3, SelectedOld, &MainView);

	for(size_t i = 0; i < vpFilteredFlags.size(); i++)
	{
		const CCountryFlags::CCountryFlag *pEntry = vpFilteredFlags[i];

		if(pEntry->m_CountryCode == *pCountry)
			SelectedOld = i;

		const CListboxItem Item = s_ListBox.DoNextItem(&pEntry->m_CountryCode, SelectedOld >= 0 && (size_t)SelectedOld == i);
		if(!Item.m_Visible)
			continue;

		CUIRect FlagRect;
		Item.m_Rect.Margin(5.0f, &FlagRect);
		FlagRect.HSplitBottom(12.0f, &FlagRect, &Label);
		Label.HSplitTop(2.0f, nullptr, &Label);
		const float OldWidth = FlagRect.w;
		FlagRect.w = FlagRect.h * 2;
		FlagRect.x += (OldWidth - FlagRect.w) / 2.0f;
		GameClient()->m_CountryFlags.Render(pEntry->m_CountryCode, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), FlagRect.x, FlagRect.y, FlagRect.w, FlagRect.h);

		if(pEntry->m_Texture.IsValid())
		{
			Ui()->DoLabel(&Label, pEntry->m_aCountryCodeString, 10.0f, TEXTALIGN_MC);
		}
	}

	const int NewSelected = s_ListBox.DoEnd();
	if(SelectedOld != NewSelected)
	{
		*pCountry = vpFilteredFlags[NewSelected]->m_CountryCode;
		SetNeedSendInfo();
	}

	Ui()->DoEditBox_Search(&s_FlagFilterInput, &QuickSearch, 14.0f, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());
}

void CMenus::RenderSettingsTee(CUIRect MainView)
{
	CUIRect TabBar, PlayerTab, DummyTab, ChangeInfo;
	MainView.HSplitTop(20.0f, &TabBar, &MainView);
	TabBar.VSplitMid(&TabBar, &ChangeInfo, 20.f);
	TabBar.VSplitMid(&PlayerTab, &DummyTab);
	MainView.HSplitTop(10.0f, nullptr, &MainView);

	static CButtonContainer s_PlayerTabButton;
	if(DoButton_MenuTab(&s_PlayerTabButton, Localize("Player"), !m_Dummy, &PlayerTab, IGraphics::CORNER_L, nullptr, nullptr, nullptr, nullptr, 4.0f))
	{
		m_Dummy = false;
		m_SkinListScrollToSelected = true;
	}

	static CButtonContainer s_DummyTabButton;
	if(DoButton_MenuTab(&s_DummyTabButton, Localize("Dummy"), m_Dummy, &DummyTab, IGraphics::CORNER_R, nullptr, nullptr, nullptr, nullptr, 4.0f))
	{
		m_Dummy = true;
		m_SkinListScrollToSelected = true;
	}

	if(Client()->State() == IClient::STATE_ONLINE &&
		GameClient()->m_aNextChangeInfo[m_Dummy] > Client()->GameTick(m_Dummy))
	{
		char aChangeInfo[128], aTimeLeft[32];
		str_format(aTimeLeft, sizeof(aTimeLeft), Localize("%ds left"), (GameClient()->m_aNextChangeInfo[m_Dummy] - Client()->GameTick(m_Dummy) + Client()->GameTickSpeed() - 1) / Client()->GameTickSpeed());
		str_format(aChangeInfo, sizeof(aChangeInfo), "%s: %s", Localize("Player info change cooldown"), aTimeLeft);
		Ui()->DoLabel(&ChangeInfo, aChangeInfo, 10.f, TEXTALIGN_ML);
	}

	if(g_Config.m_Debug)
	{
		const CSkins::CSkinLoadingStats Stats = GameClient()->m_Skins.LoadingStats();
		char aStats[256];
		str_format(aStats, sizeof(aStats), "unloaded: %" PRIzu ", pending: %" PRIzu ", loading: %" PRIzu ",\nloaded: %" PRIzu ", error: %" PRIzu ", notfound: %" PRIzu,
			Stats.m_NumUnloaded, Stats.m_NumPending, Stats.m_NumLoading, Stats.m_NumLoaded, Stats.m_NumError, Stats.m_NumNotFound);
		Ui()->DoLabel(&ChangeInfo, aStats, 9.0f, TEXTALIGN_MR);
	}

	char *pSkinName;
	size_t SkinNameSize;
	int *pUseCustomColor;
	unsigned *pColorBody;
	unsigned *pColorFeet;
	int *pEmote;
	int *pCountry;
	static CLineInput s_NameInput;
	static CLineInput s_ClanInput;
	if(!m_Dummy)
	{
		pSkinName = g_Config.m_ClPlayerSkin;
		SkinNameSize = sizeof(g_Config.m_ClPlayerSkin);
		pUseCustomColor = &g_Config.m_ClPlayerUseCustomColor;
		pColorBody = &g_Config.m_ClPlayerColorBody;
		pColorFeet = &g_Config.m_ClPlayerColorFeet;
		pEmote = &g_Config.m_ClPlayerDefaultEyes;
		pCountry = &g_Config.m_PlayerCountry;
		s_NameInput.SetBuffer(g_Config.m_PlayerName, sizeof(g_Config.m_PlayerName));
		s_NameInput.SetEmptyText(Client()->PlayerName());
		s_ClanInput.SetBuffer(g_Config.m_PlayerClan, sizeof(g_Config.m_PlayerClan));
	}
	else
	{
		pSkinName = g_Config.m_ClDummySkin;
		SkinNameSize = sizeof(g_Config.m_ClDummySkin);
		pUseCustomColor = &g_Config.m_ClDummyUseCustomColor;
		pColorBody = &g_Config.m_ClDummyColorBody;
		pColorFeet = &g_Config.m_ClDummyColorFeet;
		pEmote = &g_Config.m_ClDummyDefaultEyes;
		pCountry = &g_Config.m_ClDummyCountry;
		s_NameInput.SetBuffer(g_Config.m_ClDummyName, sizeof(g_Config.m_ClDummyName));
		s_NameInput.SetEmptyText(Client()->DummyName());
		s_ClanInput.SetBuffer(g_Config.m_ClDummyClan, sizeof(g_Config.m_ClDummyClan));
	}

	const float EyeLineSize = 40.0f;
	const bool RenderEyesBelow = MainView.w < 750.0f;
	CUIRect YourSkin, Checkboxes, SkinPrefix, Eyes, Button, Label;
	MainView.HSplitTop(130.0f, &YourSkin, &MainView);
	if(RenderEyesBelow)
	{
		YourSkin.VSplitLeft(MainView.w * 0.45f, &YourSkin, &Checkboxes);
		Checkboxes.VSplitLeft(MainView.w * 0.35f, &Checkboxes, &SkinPrefix);
		MainView.HSplitTop(5.0f, nullptr, &MainView);
		MainView.HSplitTop(EyeLineSize, &Eyes, &MainView);
		Eyes.VSplitRight(EyeLineSize * NUM_EMOTES + 5.0f * (NUM_EMOTES - 1), nullptr, &Eyes);
	}
	else
	{
		YourSkin.VSplitRight(3 * EyeLineSize + 2 * 5.0f, &YourSkin, &Eyes);
		const float RemainderWidth = YourSkin.w;
		YourSkin.VSplitLeft(RemainderWidth * 0.4f, &YourSkin, &Checkboxes);
		Checkboxes.VSplitLeft(RemainderWidth * 0.35f, &Checkboxes, &SkinPrefix);
		SkinPrefix.VSplitRight(20.0f, &SkinPrefix, nullptr);
	}
	YourSkin.VSplitRight(20.0f, &YourSkin, nullptr);
	Checkboxes.VSplitRight(20.0f, &Checkboxes, nullptr);

	// Checkboxes
	bool ShouldRefresh = false;
	Checkboxes.HSplitTop(20.0f, &Button, &Checkboxes);
	if(DoButton_CheckBox(&g_Config.m_ClDownloadSkins, Localize("Download skins"), g_Config.m_ClDownloadSkins, &Button))
	{
		g_Config.m_ClDownloadSkins ^= 1;
		ShouldRefresh = true;
	}

	Checkboxes.HSplitTop(20.0f, &Button, &Checkboxes);
	if(DoButton_CheckBox(&g_Config.m_ClDownloadCommunitySkins, Localize("Download community skins"), g_Config.m_ClDownloadCommunitySkins, &Button))
	{
		g_Config.m_ClDownloadCommunitySkins ^= 1;
		ShouldRefresh = true;
	}

	Checkboxes.HSplitTop(20.0f, &Button, &Checkboxes);
	if(DoButton_CheckBox(&g_Config.m_ClVanillaSkinsOnly, Localize("Vanilla skins only"), g_Config.m_ClVanillaSkinsOnly, &Button))
	{
		g_Config.m_ClVanillaSkinsOnly ^= 1;
		ShouldRefresh = true;
	}

	Checkboxes.HSplitTop(20.0f, &Button, &Checkboxes);
	if(DoButton_CheckBox(&g_Config.m_ClFatSkins, Localize("Fat skins (DDFat)"), g_Config.m_ClFatSkins, &Button))
	{
		g_Config.m_ClFatSkins ^= 1;
	}

	// Skin prefix
	{
		SkinPrefix.HSplitTop(20.0f, &Label, &SkinPrefix);
		Ui()->DoLabel(&Label, Localize("Skin prefix"), 14.0f, TEXTALIGN_ML);

		SkinPrefix.HSplitTop(20.0f, &Button, &SkinPrefix);
		static CLineInput s_SkinPrefixInput(g_Config.m_ClSkinPrefix, sizeof(g_Config.m_ClSkinPrefix));
		if(Ui()->DoClearableEditBox(&s_SkinPrefixInput, &Button, 14.0f))
		{
			ShouldRefresh = true;
		}

		SkinPrefix.HSplitTop(2.0f, nullptr, &SkinPrefix);

		static const char *s_apSkinPrefixes[] = {"kitty", "santa"};
		static CButtonContainer s_aPrefixButtons[std::size(s_apSkinPrefixes)];
		for(size_t i = 0; i < std::size(s_apSkinPrefixes); i++)
		{
			SkinPrefix.HSplitTop(20.0f, &Button, &SkinPrefix);
			Button.HMargin(2.0f, &Button);
			if(DoButton_Menu(&s_aPrefixButtons[i], s_apSkinPrefixes[i], 0, &Button))
			{
				str_copy(g_Config.m_ClSkinPrefix, s_apSkinPrefixes[i]);
				ShouldRefresh = true;
			}
		}
	}
	CUIRect RandomColorsButton;

	// Player skin area
	CUIRect CustomColorsButton, RandomSkinButton;
	YourSkin.HSplitTop(20.0f, &Label, &YourSkin);
	YourSkin.HSplitBottom(20.0f, &YourSkin, &CustomColorsButton);

	CustomColorsButton.VSplitRight(30.0f, &CustomColorsButton, &RandomSkinButton);
	CustomColorsButton.VSplitRight(3.0f, &CustomColorsButton, 0);

	CustomColorsButton.VSplitRight(110.0f, &CustomColorsButton, &RandomColorsButton);

	CustomColorsButton.VSplitRight(5.0f, &CustomColorsButton, nullptr);
	CSkins::CSkinList &SkinList = GameClient()->m_Skins.SkinList();
	YourSkin.VSplitLeft(65.0f, &YourSkin, &Button);
	Button.VSplitLeft(5.0f, nullptr, &Button);

	const float NameClanSkinHeight = 3.0f * 20.0f + 2.0f * 5.0f;
	if(Button.h > NameClanSkinHeight)
	{
		Button.HMargin((Button.h - NameClanSkinHeight) / 2.0f, &Button);
	}

	CUIRect NameRow, ClanRow, SkinRow;
	Button.HSplitTop(20.0f, &NameRow, &Button);
	Button.HSplitTop(5.0f, nullptr, &Button);
	Button.HSplitTop(20.0f, &ClanRow, &Button);
	Button.HSplitTop(5.0f, nullptr, &Button);
	Button.HSplitTop(20.0f, &SkinRow, nullptr);

	CUIRect NameLabel, NameInput, ClanLabel, ClanInput, SkinLabel, SkinInput, FlagButton;
	NameRow.VSplitLeft(45.0f, &NameLabel, &NameInput);
	ClanRow.VSplitLeft(45.0f, &ClanLabel, &ClanInput);
	SkinRow.VSplitLeft(45.0f, &SkinLabel, &SkinInput);
	SkinInput.VSplitRight(44.0f, &SkinInput, &FlagButton);
	SkinInput.VSplitRight(5.0f, &SkinInput, nullptr);

	Ui()->DoLabel(&NameLabel, Localize("Name"), 14.0f, TEXTALIGN_ML);
	Ui()->DoLabel(&ClanLabel, Localize("Clan"), 14.0f, TEXTALIGN_ML);
	Ui()->DoLabel(&SkinLabel, "Скин", 14.0f, TEXTALIGN_ML);

	if(Ui()->DoEditBox(&s_NameInput, &NameInput, 14.0f))
	{
		SetNeedSendInfo();
	}

	if(Ui()->DoEditBox(&s_ClanInput, &ClanInput, 14.0f))
	{
		SetNeedSendInfo();
	}

	static CLineInput s_SkinInput;
	s_SkinInput.SetBuffer(pSkinName, SkinNameSize);
	s_SkinInput.SetEmptyText("default");
	if(Ui()->DoClearableEditBox(&s_SkinInput, &SkinInput, 14.0f))
	{
		SetNeedSendInfo();
		m_SkinListScrollToSelected = true;
		SkinList.ForceRefresh();
	}

	static CButtonContainer s_FlagButton;
	if(DoButton_Menu(&s_FlagButton, "", 0, &FlagButton))
	{
		static SPopupMenuId s_PopupCountryId;
		static SPopupSettingsCountrySelectionContext s_PopupCountryContext;
		s_PopupCountryContext.m_pMenus = this;
		s_PopupCountryContext.m_pCountry = pCountry;
		s_PopupCountryContext.m_Selection = *pCountry;
		s_PopupCountryContext.m_New = true;
		Ui()->DoPopupMenu(&s_PopupCountryId, FlagButton.x, FlagButton.y + FlagButton.h, 490.0f, 210.0f, &s_PopupCountryContext, PopupSettingsCountrySelection);
	}
	GameClient()->m_Tooltips.DoToolTip(&s_FlagButton, &FlagButton, Localize("Choose country flag"));

	CUIRect FlagIcon = FlagButton;
	const float OldWidth = FlagIcon.w;
	FlagIcon.w = FlagIcon.h * 2.0f;
	FlagIcon.x += (OldWidth - FlagIcon.w) / 2.0f;
	GameClient()->m_CountryFlags.Render(*pCountry, ColorRGBA(1.0f, 1.0f, 1.0f, Ui()->HotItem() == &s_FlagButton ? 1.0f : 0.85f), FlagIcon.x, FlagIcon.y, FlagIcon.w, FlagIcon.h);

	char aBuf[128 + IO_MAX_PATH_LENGTH];
	str_format(aBuf, sizeof(aBuf), "%s:", Localize("Your skin"));
	Ui()->DoLabel(&Label, aBuf, 14.0f, TEXTALIGN_ML);

	const CSkin *pDefaultSkin = GameClient()->m_Skins.Find("default");
	const CSkins::CSkinContainer *pOwnSkinContainer = GameClient()->m_Skins.FindContainerOrNullptr(pSkinName[0] == '\0' ? "default" : pSkinName);
	if(pOwnSkinContainer != nullptr && pOwnSkinContainer->IsSpecial())
	{
		pOwnSkinContainer = nullptr; // Special skins cannot be selected, show as missing due to invalid name
	}

	CTeeRenderInfo OwnSkinInfo;
	OwnSkinInfo.Apply(pOwnSkinContainer == nullptr || pOwnSkinContainer->Skin() == nullptr ? pDefaultSkin : pOwnSkinContainer->Skin().get());
	OwnSkinInfo.ApplyColors(*pUseCustomColor, *pColorBody, *pColorFeet);
	OwnSkinInfo.m_Size = gs_MenuPreviewTeeLarge;

	// Tee
	{
		vec2 OffsetToMid;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &OwnSkinInfo, OffsetToMid);
		const vec2 TeeRenderPos = vec2(YourSkin.x + YourSkin.w / 2.0f, YourSkin.y + YourSkin.h / 2.0f + OffsetToMid.y);
		// tee looking towards cursor, and it is happy when you touch it
		const vec2 DeltaPosition = Ui()->MousePos() - TeeRenderPos;
		const float Distance = length(DeltaPosition);
		const float InteractionDistance = 20.0f;
		const vec2 TeeDirection = Distance < InteractionDistance ? normalize(vec2(DeltaPosition.x, maximum(DeltaPosition.y, 0.5f))) : normalize(DeltaPosition);
		const int TeeEmote = Distance < InteractionDistance ? EMOTE_HAPPY : *pEmote;
		RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, TeeEmote, TeeDirection, TeeRenderPos);
	}

	// Skin loading status
	const auto &&RenderSkinStatus = [&](CUIRect Parent, const CSkins::CSkinContainer *pSkinContainer, const void *pStatusTooltipId) {
		if(pSkinContainer != nullptr && pSkinContainer->State() == CSkins::CSkinContainer::EState::LOADED)
		{
			return;
		}

		CUIRect StatusIcon;
		Parent.HSplitTop(20.0f, &StatusIcon, nullptr);
		StatusIcon.VSplitLeft(20.0f, &StatusIcon, nullptr);

		if(pSkinContainer != nullptr &&
			(pSkinContainer->State() == CSkins::CSkinContainer::EState::UNLOADED ||
				pSkinContainer->State() == CSkins::CSkinContainer::EState::PENDING ||
				pSkinContainer->State() == CSkins::CSkinContainer::EState::LOADING))
		{
			Ui()->RenderProgressSpinner(StatusIcon.Center(), 5.0f);
		}
		else
		{
			TextRender()->TextColor(ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f));
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
			Ui()->DoLabel(&StatusIcon, pSkinContainer == nullptr || pSkinContainer->State() == CSkins::CSkinContainer::EState::ERROR ? FONT_ICON_TRIANGLE_EXCLAMATION : FONT_ICON_QUESTION, 12.0f, TEXTALIGN_MC);
			TextRender()->SetRenderFlags(0);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			Ui()->DoButtonLogic(pStatusTooltipId, 0, &StatusIcon, BUTTONFLAG_NONE);
			const char *pErrorTooltip;
			if(pSkinContainer == nullptr)
			{
				pErrorTooltip = Localize("This skin name cannot be used.");
			}
			else if(pSkinContainer->State() == CSkins::CSkinContainer::EState::ERROR)
			{
				pErrorTooltip = Localize("Skin could not be loaded due to an error. Check the local console for details.");
			}
			else
			{
				pErrorTooltip = Localize("Skin could not be found.");
			}
			GameClient()->m_Tooltips.DoToolTip(pStatusTooltipId, &StatusIcon, pErrorTooltip);
		}
	};
	static char s_StatusTooltipId;
	RenderSkinStatus(YourSkin, pOwnSkinContainer, &s_StatusTooltipId);

	// Random skin button
	static CButtonContainer s_RandomSkinButton;
	static const char *s_apDice[] = {FONT_ICON_DICE_ONE, FONT_ICON_DICE_TWO, FONT_ICON_DICE_THREE, FONT_ICON_DICE_FOUR, FONT_ICON_DICE_FIVE, FONT_ICON_DICE_SIX};
	static int s_CurrentDie = rand() % std::size(s_apDice);
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	if(DoButton_Menu(&s_RandomSkinButton, s_apDice[s_CurrentDie], 0, &RandomSkinButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, -0.2f))
	{
		GameClient()->m_Skins.RandomizeSkin(m_Dummy);
		SetNeedSendInfo();
		m_SkinListScrollToSelected = true;
		s_CurrentDie = rand() % std::size(s_apDice);
	}
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	GameClient()->m_Tooltips.DoToolTip(&s_RandomSkinButton, &RandomSkinButton, Localize("Create a random skin"));

	static CButtonContainer s_RandomizeColors;
	if(*pUseCustomColor)
	{
		// RandomColorsButton.VSplitLeft(120.0f, &RandomColorsButton, 0);
		if(DoButton_Menu(&s_RandomizeColors, "Random Colors", 0, &RandomColorsButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f)))
		{
			if(m_Dummy)
			{
				g_Config.m_ClDummyColorBody = ColorHSLA((std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, 1).Pack(false);
				g_Config.m_ClDummyColorFeet = ColorHSLA((std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, 1).Pack(false);
			}
			else
			{
				g_Config.m_ClPlayerColorBody = ColorHSLA((std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, 1).Pack(false);
				g_Config.m_ClPlayerColorFeet = ColorHSLA((std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, 1).Pack(false);
			}
			SetNeedSendInfo();
		}
	}
	MainView.HSplitTop(5.0f, 0, &MainView);

	// Custom colors button
	if(DoButton_CheckBox(pUseCustomColor, Localize("Custom colors"), *pUseCustomColor, &CustomColorsButton))
	{
		*pUseCustomColor = *pUseCustomColor ? 0 : 1;
		SetNeedSendInfo();
	}

	// Default eyes
	{
		CTeeRenderInfo EyeSkinInfo = OwnSkinInfo;
		EyeSkinInfo.m_Size = EyeLineSize;
		vec2 OffsetToMid;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &EyeSkinInfo, OffsetToMid);

		CUIRect EyesRow;
		Eyes.HSplitTop(EyeLineSize, &EyesRow, &Eyes);
		static CButtonContainer s_aEyeButtons[NUM_EMOTES];
		for(int CurrentEyeEmote = 0; CurrentEyeEmote < NUM_EMOTES; CurrentEyeEmote++)
		{
			EyesRow.VSplitLeft(EyeLineSize, &Button, &EyesRow);
			EyesRow.VSplitLeft(5.0f, nullptr, &EyesRow);
			if(!RenderEyesBelow && (CurrentEyeEmote + 1) % 3 == 0)
			{
				Eyes.HSplitTop(5.0f, nullptr, &Eyes);
				Eyes.HSplitTop(EyeLineSize, &EyesRow, &Eyes);
			}

			const ColorRGBA EyeButtonColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f + (*pEmote == CurrentEyeEmote ? 0.25f : 0.0f));
			if(DoButton_Menu(&s_aEyeButtons[CurrentEyeEmote], "", 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, EyeButtonColor))
			{
				*pEmote = CurrentEyeEmote;
				if((int)m_Dummy == g_Config.m_ClDummy)
					GameClient()->m_Emoticon.EyeEmote(CurrentEyeEmote);
			}
			GameClient()->m_Tooltips.DoToolTip(&s_aEyeButtons[CurrentEyeEmote], &Button, Localize("Choose default eyes when joining a server"));
			RenderTools()->RenderTee(CAnimState::GetIdle(), &EyeSkinInfo, CurrentEyeEmote, vec2(1.0f, 0.0f), vec2(Button.x + Button.w / 2.0f, Button.y + Button.h / 2.0f + OffsetToMid.y));
		}
	}

	// Custom color pickers
	MainView.HSplitTop(5.0f, nullptr, &MainView);
	if(*pUseCustomColor)
	{
		CUIRect CustomColors;
		MainView.HSplitTop(95.0f, &CustomColors, &MainView);
		CUIRect aRects[2];
		CustomColors.VSplitMid(&aRects[0], &aRects[1], 20.0f);

		unsigned *apColors[] = {pColorBody, pColorFeet};
		const char *apParts[] = {Localize("Body"), Localize("Feet")};

		for(int i = 0; i < 2; i++)
		{
			aRects[i].HSplitTop(20.0f, &Label, &aRects[i]);
			Ui()->DoLabel(&Label, apParts[i], 14.0f, TEXTALIGN_ML);
			if(RenderHslaScrollbars(&aRects[i], apColors[i], false, ColorHSLA::DARKEST_LGT))
			{
				SetNeedSendInfo();
			}
		}
	}
	MainView.HSplitTop(5.0f, nullptr, &MainView);

	// Layout bottom controls and use remainder for skin selector
	CUIRect QuickSearch, DatabaseButton, DirectoryButton, RefreshButton;
	MainView.HSplitBottom(20.0f, &MainView, &QuickSearch);
	MainView.HSplitBottom(5.0f, &MainView, nullptr);
	QuickSearch.VSplitLeft(220.0f, &QuickSearch, &DatabaseButton);
	DatabaseButton.VSplitLeft(10.0f, nullptr, &DatabaseButton);
	DatabaseButton.VSplitLeft(150.0f, &DatabaseButton, &DirectoryButton);
	DirectoryButton.VSplitRight(175.0f, nullptr, &DirectoryButton);
	DirectoryButton.VSplitRight(25.0f, &DirectoryButton, &RefreshButton);
	DirectoryButton.VSplitRight(10.0f, &DirectoryButton, nullptr);

	// Skin selector
	static CListBox s_ListBox;
	std::vector<CSkins::CSkinListEntry> &vSkinList = SkinList.Skins();
	int OldSelected = -1;
	s_ListBox.DoStart(50.0f, vSkinList.size(), 4, 2, OldSelected, &MainView);
	for(size_t i = 0; i < vSkinList.size(); ++i)
	{
		CSkins::CSkinListEntry &SkinListEntry = vSkinList[i];
		const CSkins::CSkinContainer *pSkinContainer = vSkinList[i].SkinContainer();

		if(!m_Dummy ? SkinListEntry.IsSelectedMain() : SkinListEntry.IsSelectedDummy())
		{
			OldSelected = i;
			if(m_SkinListScrollToSelected)
			{
				s_ListBox.ScrollToSelected();
				m_SkinListScrollToSelected = false;
			}
		}

		const CListboxItem Item = s_ListBox.DoNextItem(SkinListEntry.ListItemId(), OldSelected >= 0 && (size_t)OldSelected == i);
		if(!Item.m_Visible)
		{
			continue;
		}

		SkinListEntry.RequestLoad();
		const CSkin *pSkin = pSkinContainer->State() == CSkins::CSkinContainer::EState::LOADED ? pSkinContainer->Skin().get() : pDefaultSkin;

		Item.m_Rect.VSplitLeft(60.0f, &Button, &Label);

		{
			CTeeRenderInfo Info = OwnSkinInfo;
			Info.Apply(pSkin);
			vec2 OffsetToMid;
			CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &Info, OffsetToMid);
			const vec2 TeeRenderPos = vec2(Button.x + Button.w / 2.0f, Button.y + Button.h / 2 + OffsetToMid.y);
			RenderTools()->RenderTee(CAnimState::GetIdle(), &Info, *pEmote, vec2(1.0f, 0.0f), TeeRenderPos);
		}

		{
			SLabelProperties Props;
			Props.m_MaxWidth = Label.w - 5.0f;
			const auto &NameMatch = SkinListEntry.NameMatch();
			if(NameMatch.has_value())
			{
				const auto [MatchStart, MatchLength] = NameMatch.value();
				Props.m_vColorSplits.emplace_back(MatchStart, MatchLength, ColorRGBA(0.4f, 0.4f, 1.0f, 1.0f));
			}
			Ui()->DoLabel(&Label, pSkinContainer->Name(), 12.0f, TEXTALIGN_ML, Props);
		}

		if(g_Config.m_Debug)
		{
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(*pUseCustomColor ? color_cast<ColorRGBA>(ColorHSLA(*pColorBody).UnclampLighting(ColorHSLA::DARKEST_LGT)) : pSkin->m_BloodColor);
			IGraphics::CQuadItem QuadItem(Label.x, Label.y, 12.0f, 12.0f);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
		}

		// render skin favorite icon
		{
			CUIRect FavIcon;
			Item.m_Rect.HSplitTop(20.0f, &FavIcon, nullptr);
			FavIcon.VSplitRight(20.0f, nullptr, &FavIcon);
			if(DoButton_Favorite(SkinListEntry.FavoriteButtonId(), SkinListEntry.ListItemId(), SkinListEntry.IsFavorite(), &FavIcon))
			{
				if(SkinListEntry.IsFavorite())
				{
					GameClient()->m_Skins.RemoveFavorite(pSkinContainer->Name());
				}
				else
				{
					GameClient()->m_Skins.AddFavorite(pSkinContainer->Name());
				}
			}
		}

		RenderSkinStatus(Item.m_Rect, pSkinContainer, SkinListEntry.ErrorTooltipId());
	}

	const int NewSelected = s_ListBox.DoEnd();
	if(OldSelected != NewSelected)
	{
		str_copy(pSkinName, vSkinList[NewSelected].SkinContainer()->Name(), SkinNameSize);
		SkinList.ForceRefresh();
		SetNeedSendInfo();
	}

	static CLineInput s_SkinFilterInput(g_Config.m_ClSkinFilterString, sizeof(g_Config.m_ClSkinFilterString));
	if(SkinList.UnfilteredCount() > 0 && vSkinList.empty())
	{
		CUIRect FilterLabel, ResetButton;
		MainView.HMargin((MainView.h - (16.0f + 18.0f + 8.0f)) / 2.0f, &FilterLabel);
		FilterLabel.HSplitTop(16.0f, &FilterLabel, &ResetButton);
		ResetButton.HSplitTop(8.0f, nullptr, &ResetButton);
		ResetButton.VMargin((ResetButton.w - 200.0f) / 2.0f, &ResetButton);
		Ui()->DoLabel(&FilterLabel, Localize("No skins match your filter criteria"), 16.0f, TEXTALIGN_MC);
		static CButtonContainer s_ResetButton;
		if(DoButton_Menu(&s_ResetButton, Localize("Reset filter"), 0, &ResetButton))
		{
			s_SkinFilterInput.Clear();
			SkinList.ForceRefresh();
		}
	}

	if(Ui()->DoEditBox_Search(&s_SkinFilterInput, &QuickSearch, 14.0f, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive()))
	{
		SkinList.ForceRefresh();
	}

	static CButtonContainer s_SkinDatabaseButton;
	if(DoButton_Menu(&s_SkinDatabaseButton, Localize("Skin Database"), 0, &DatabaseButton))
	{
		Client()->ViewLink("https://ddnet.org/skins/");
	}

	static CButtonContainer s_DirectoryButton;
	if(DoButton_Menu(&s_DirectoryButton, Localize("Skins directory"), 0, &DirectoryButton))
	{
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, "skins", aBuf, sizeof(aBuf));
		Storage()->CreateFolder("skins", IStorage::TYPE_SAVE);
		Client()->ViewFile(aBuf);
	}
	GameClient()->m_Tooltips.DoToolTip(&s_DirectoryButton, &DirectoryButton, Localize("Open the directory to add custom skins"));

	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	static CButtonContainer s_SkinRefreshButton;
	if(DoButton_Menu(&s_SkinRefreshButton, FONT_ICON_ARROW_ROTATE_RIGHT, 0, &RefreshButton) || Input()->KeyPress(KEY_F5) || (Input()->KeyPress(KEY_R) && Input()->ModifierIsPressed()))
	{
		ShouldRefresh = true;
	}
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

	if(ShouldRefresh)
	{
		GameClient()->RefreshSkins(CSkinDescriptor::FLAG_SIX);
	}
}

float CMenus::RenderSettingsGraphics(CUIRect MainView)
{
	const float BaseContentBottom = MainView.y + MainView.h;
	CUIRect Button;
	char aBuf[128];
	bool CheckSettings = false;

	static const int MAX_RESOLUTIONS = 256;
	static CVideoMode s_aModes[MAX_RESOLUTIONS];
	static int s_NumNodes = Graphics()->GetVideoModes(s_aModes, MAX_RESOLUTIONS, g_Config.m_GfxScreen);
	static int s_GfxFsaaSamples = g_Config.m_GfxFsaaSamples;
	static bool s_GfxBackendChanged = false;
	static bool s_GfxGpuChanged = false;

	static int s_InitDisplayAllVideoModes = g_Config.m_GfxDisplayAllVideoModes;

	static bool s_WasInit = false;
	static bool s_ModesReload = false;
	if(!s_WasInit)
	{
		s_WasInit = true;

		Graphics()->AddWindowPropChangeListener([]() {
			s_ModesReload = true;
		});
	}

	if(s_ModesReload || g_Config.m_GfxDisplayAllVideoModes != s_InitDisplayAllVideoModes)
	{
		s_NumNodes = Graphics()->GetVideoModes(s_aModes, MAX_RESOLUTIONS, g_Config.m_GfxScreen);
		s_ModesReload = false;
		s_InitDisplayAllVideoModes = g_Config.m_GfxDisplayAllVideoModes;
	}

	CUIRect ModeList, ModeLabel;
	MainView.VSplitLeft(350.0f, &MainView, &ModeList);
	ModeList.HSplitTop(24.0f, &ModeLabel, &ModeList);
	MainView.VSplitLeft(340.0f, &MainView, nullptr);

	// display mode list
	static CListBox s_ListBox;
	static const float sc_RowHeightResList = 22.0f;
	static const float sc_FontSizeResListHeader = 12.0f;
	static const float sc_FontSizeResList = 10.0f;

	{
		int G = std::gcd(g_Config.m_GfxScreenWidth, g_Config.m_GfxScreenHeight);
		str_format(aBuf, sizeof(aBuf), "%s: %dx%d @%dhz %d bit (%d:%d)", Localize("Current"), (int)(g_Config.m_GfxScreenWidth * Graphics()->ScreenHiDPIScale()), (int)(g_Config.m_GfxScreenHeight * Graphics()->ScreenHiDPIScale()), g_Config.m_GfxScreenRefreshRate, g_Config.m_GfxColorDepth, g_Config.m_GfxScreenWidth / G, g_Config.m_GfxScreenHeight / G);
		Ui()->DoLabel(&ModeLabel, aBuf, sc_FontSizeResListHeader, TEXTALIGN_MC);
	}

	int SelectedOld = -1;
	s_ListBox.SetActive(!Ui()->IsPopupOpen());
	s_ListBox.DoStart(sc_RowHeightResList, s_NumNodes, 1, 3, SelectedOld, &ModeList);

	for(int i = 0; i < s_NumNodes; ++i)
	{
		const int Depth = s_aModes[i].m_Red + s_aModes[i].m_Green + s_aModes[i].m_Blue > 16 ? 24 : 16;
		if(g_Config.m_GfxColorDepth == Depth &&
			g_Config.m_GfxScreenWidth == s_aModes[i].m_WindowWidth &&
			g_Config.m_GfxScreenHeight == s_aModes[i].m_WindowHeight &&
			g_Config.m_GfxScreenRefreshRate == s_aModes[i].m_RefreshRate)
		{
			SelectedOld = i;
		}

		const CListboxItem Item = s_ListBox.DoNextItem(&s_aModes[i], SelectedOld == i);
		if(!Item.m_Visible)
			continue;

		int G = std::gcd(s_aModes[i].m_WindowWidth, s_aModes[i].m_WindowHeight);
		str_format(aBuf, sizeof(aBuf), " %dx%d @%dhz %d bit (%d:%d)", s_aModes[i].m_CanvasWidth, s_aModes[i].m_CanvasHeight, s_aModes[i].m_RefreshRate, Depth, s_aModes[i].m_WindowWidth / G, s_aModes[i].m_WindowHeight / G);
		Ui()->DoLabel(&Item.m_Rect, aBuf, sc_FontSizeResList, TEXTALIGN_ML);
	}

	const int NewSelected = s_ListBox.DoEnd();
	if(SelectedOld != NewSelected)
	{
		const int Depth = s_aModes[NewSelected].m_Red + s_aModes[NewSelected].m_Green + s_aModes[NewSelected].m_Blue > 16 ? 24 : 16;
		g_Config.m_GfxColorDepth = Depth;
		g_Config.m_GfxScreenWidth = s_aModes[NewSelected].m_WindowWidth;
		g_Config.m_GfxScreenHeight = s_aModes[NewSelected].m_WindowHeight;
		g_Config.m_GfxScreenRefreshRate = s_aModes[NewSelected].m_RefreshRate;
		Graphics()->ResizeToScreen();
	}

	// switches
	CUIRect WindowModeDropDown;
	MainView.HSplitTop(20.0f, &WindowModeDropDown, &MainView);

	const char *apWindowModes[] = {Localize("Windowed"), Localize("Windowed borderless"), Localize("Windowed fullscreen"), Localize("Desktop fullscreen"), Localize("Fullscreen")};
	static const int s_NumWindowMode = std::size(apWindowModes);

	const int OldWindowMode = (g_Config.m_GfxFullscreen ? (g_Config.m_GfxFullscreen == 1 ? 4 : (g_Config.m_GfxFullscreen == 2 ? 3 : 2)) : (g_Config.m_GfxBorderless ? 1 : 0));

	static CUi::SDropDownState s_WindowModeDropDownState;
	static CScrollRegion s_WindowModeDropDownScrollRegion;
	s_WindowModeDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_WindowModeDropDownScrollRegion;
	const int NewWindowMode = Ui()->DoDropDown(&WindowModeDropDown, OldWindowMode, apWindowModes, s_NumWindowMode, s_WindowModeDropDownState);
	if(OldWindowMode != NewWindowMode)
	{
		if(NewWindowMode == 0)
			Graphics()->SetWindowParams(0, false);
		else if(NewWindowMode == 1)
			Graphics()->SetWindowParams(0, true);
		else if(NewWindowMode == 2)
			Graphics()->SetWindowParams(3, false);
		else if(NewWindowMode == 3)
			Graphics()->SetWindowParams(2, false);
		else if(NewWindowMode == 4)
			Graphics()->SetWindowParams(1, false);
	}

	if(Graphics()->GetNumScreens() > 1)
	{
		CUIRect ScreenDropDown;
		MainView.HSplitTop(2.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &ScreenDropDown, &MainView);

		const int NumScreens = Graphics()->GetNumScreens();
		static std::vector<std::string> s_vScreenNames;
		static std::vector<const char *> s_vpScreenNames;
		s_vScreenNames.resize(NumScreens);
		s_vpScreenNames.resize(NumScreens);

		for(int i = 0; i < NumScreens; ++i)
		{
			str_format(aBuf, sizeof(aBuf), "%s %d: %s", Localize("Screen"), i, Graphics()->GetScreenName(i));
			s_vScreenNames[i] = aBuf;
			s_vpScreenNames[i] = s_vScreenNames[i].c_str();
		}

		static CUi::SDropDownState s_ScreenDropDownState;
		static CScrollRegion s_ScreenDropDownScrollRegion;
		s_ScreenDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_ScreenDropDownScrollRegion;
		const int NewScreen = Ui()->DoDropDown(&ScreenDropDown, g_Config.m_GfxScreen, s_vpScreenNames.data(), s_vpScreenNames.size(), s_ScreenDropDownState);
		if(NewScreen != g_Config.m_GfxScreen)
			Graphics()->SwitchWindowScreen(NewScreen, true);
	}

	MainView.HSplitTop(2.0f, nullptr, &MainView);
	MainView.HSplitTop(20.0f, &Button, &MainView);
	str_format(aBuf, sizeof(aBuf), "%s (%s)", Localize("V-Sync"), Localize("may cause delay"));
	if(DoButton_CheckBox(&g_Config.m_GfxVsync, aBuf, g_Config.m_GfxVsync, &Button))
	{
		Graphics()->SetVSync(!g_Config.m_GfxVsync);
	}

	bool MultiSamplingChanged = false;
	MainView.HSplitTop(20.0f, &Button, &MainView);
	str_format(aBuf, sizeof(aBuf), "%s (%s)", Localize("FSAA samples"), Localize("may cause delay"));
	int GfxFsaaSamplesMouseButton = DoButton_CheckBox_Number(&g_Config.m_GfxFsaaSamples, aBuf, g_Config.m_GfxFsaaSamples, &Button);
	int CurFSAA = g_Config.m_GfxFsaaSamples == 0 ? 1 : g_Config.m_GfxFsaaSamples;
	if(GfxFsaaSamplesMouseButton == 1) // inc
	{
		g_Config.m_GfxFsaaSamples = std::pow(2, (int)std::log2(CurFSAA) + 1);
		if(g_Config.m_GfxFsaaSamples > 64)
			g_Config.m_GfxFsaaSamples = 0;
		MultiSamplingChanged = true;
	}
	else if(GfxFsaaSamplesMouseButton == 2) // dec
	{
		if(CurFSAA == 1)
			g_Config.m_GfxFsaaSamples = 64;
		else if(CurFSAA == 2)
			g_Config.m_GfxFsaaSamples = 0;
		else
			g_Config.m_GfxFsaaSamples = std::pow(2, (int)std::log2(CurFSAA) - 1);
		MultiSamplingChanged = true;
	}

	uint32_t MultiSamplingCountBackend = 0;
	if(MultiSamplingChanged)
	{
		if(Graphics()->SetMultiSampling(g_Config.m_GfxFsaaSamples, MultiSamplingCountBackend))
		{
			// try again with 0 if mouse click was increasing multi sampling
			// else just accept the current value as is
			if((uint32_t)g_Config.m_GfxFsaaSamples > MultiSamplingCountBackend && GfxFsaaSamplesMouseButton == 1)
				Graphics()->SetMultiSampling(0, MultiSamplingCountBackend);
			g_Config.m_GfxFsaaSamples = (int)MultiSamplingCountBackend;
		}
		else
		{
			CheckSettings = true;
		}
	}

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_GfxHighDetail, Localize("High Detail"), g_Config.m_GfxHighDetail, &Button))
		g_Config.m_GfxHighDetail ^= 1;
	GameClient()->m_Tooltips.DoToolTip(&g_Config.m_GfxHighDetail, &Button, Localize("Allows maps to render with more detail"));

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_ClShowfps, Localize("Show FPS"), g_Config.m_ClShowfps, &Button))
		g_Config.m_ClShowfps ^= 1;
	GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ClShowfps, &Button, Localize("Renders your frame rate in the top right"));

	MainView.HSplitTop(20.0f, &Button, &MainView);
	str_copy(aBuf, " ");
	str_append(aBuf, Localize("Hz", "Hertz"));
	Ui()->DoScrollbarOption(&g_Config.m_GfxRefreshRate, &g_Config.m_GfxRefreshRate, &Button, Localize("Refresh Rate"), 10, 1000, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_INFINITE | CUi::SCROLLBAR_OPTION_NOCLAMPVALUE | CUi::SCROLLBAR_OPTION_DELAYUPDATE, aBuf);

	MainView.HSplitTop(20.0f, &Button, &MainView);
	Ui()->DoScrollbarOption(&g_Config.m_UiScale, &g_Config.m_UiScale, &Button, Localize("UI scale"), 50, 300, &CUi::ms_LinearScrollbarScale, 0u, "%");

	MainView.HSplitTop(2.0f, nullptr, &MainView);
	static CButtonContainer s_UiColorResetId;
	DoLine_ColorPicker(&s_UiColorResetId, 25.0f, 13.0f, 2.0f, &MainView, Localize("UI Color"), &g_Config.m_UiColor, color_cast<ColorRGBA>(ColorHSLA(0xE4A046AFU, true)), false, nullptr, true);

	// Backend list
	struct SMenuBackendInfo
	{
		int m_Major = 0;
		int m_Minor = 0;
		int m_Patch = 0;
		const char *m_pBackendName = "";
		bool m_Found = false;
	};
	std::array<std::array<SMenuBackendInfo, EGraphicsDriverAgeType::GRAPHICS_DRIVER_AGE_TYPE_COUNT>, EBackendType::BACKEND_TYPE_COUNT> aaSupportedBackends{};
	uint32_t FoundBackendCount = 0;
	for(uint32_t i = 0; i < BACKEND_TYPE_COUNT; ++i)
	{
		if(EBackendType(i) == BACKEND_TYPE_AUTO)
			continue;
		for(uint32_t n = 0; n < GRAPHICS_DRIVER_AGE_TYPE_COUNT; ++n)
		{
			auto &Info = aaSupportedBackends[i][n];
			if(Graphics()->GetDriverVersion(EGraphicsDriverAgeType(n), Info.m_Major, Info.m_Minor, Info.m_Patch, Info.m_pBackendName, EBackendType(i)))
			{
				// don't count blocked opengl drivers
				if(EBackendType(i) != BACKEND_TYPE_OPENGL || EGraphicsDriverAgeType(n) == GRAPHICS_DRIVER_AGE_TYPE_LEGACY || g_Config.m_GfxDriverIsBlocked == 0)
				{
					Info.m_Found = true;
					++FoundBackendCount;
				}
			}
		}
	}

	if(FoundBackendCount > 1)
	{
		CUIRect Text, BackendDropDown;
		MainView.HSplitTop(10.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &Text, &MainView);
		MainView.HSplitTop(2.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &BackendDropDown, &MainView);
		Ui()->DoLabel(&Text, Localize("Renderer"), 16.0f, TEXTALIGN_MC);

		static std::vector<std::string> s_vBackendIdNames;
		static std::vector<const char *> s_vpBackendIdNamesCStr;
		static std::vector<SMenuBackendInfo> s_vBackendInfos;

		size_t BackendCount = FoundBackendCount + 1;
		s_vBackendIdNames.resize(BackendCount);
		s_vpBackendIdNamesCStr.resize(BackendCount);
		s_vBackendInfos.resize(BackendCount);

		char aTmpBackendName[256];

		auto IsInfoDefault = [](const SMenuBackendInfo &CheckInfo) {
			return str_comp_nocase(CheckInfo.m_pBackendName, DefaultConfig::GfxBackend) == 0 && CheckInfo.m_Major == DefaultConfig::GfxGLMajor && CheckInfo.m_Minor == DefaultConfig::GfxGLMinor && CheckInfo.m_Patch == DefaultConfig::GfxGLPatch;
		};

		int SelectedOldBackend = -1;
		uint32_t CurCounter = 0;
		for(uint32_t i = 0; i < BACKEND_TYPE_COUNT; ++i)
		{
			for(uint32_t n = 0; n < GRAPHICS_DRIVER_AGE_TYPE_COUNT; ++n)
			{
				auto &Info = aaSupportedBackends[i][n];
				if(Info.m_Found)
				{
					bool IsDefault = IsInfoDefault(Info);
					str_format(aTmpBackendName, sizeof(aTmpBackendName), "%s (%d.%d.%d)%s%s", Info.m_pBackendName, Info.m_Major, Info.m_Minor, Info.m_Patch, IsDefault ? " - " : "", IsDefault ? Localize("default") : "");
					s_vBackendIdNames[CurCounter] = aTmpBackendName;
					s_vpBackendIdNamesCStr[CurCounter] = s_vBackendIdNames[CurCounter].c_str();
					if(str_comp_nocase(Info.m_pBackendName, g_Config.m_GfxBackend) == 0 && g_Config.m_GfxGLMajor == Info.m_Major && g_Config.m_GfxGLMinor == Info.m_Minor && g_Config.m_GfxGLPatch == Info.m_Patch)
					{
						SelectedOldBackend = CurCounter;
					}

					s_vBackendInfos[CurCounter] = Info;
					++CurCounter;
				}
			}
		}

		if(SelectedOldBackend != -1)
		{
			// no custom selected
			BackendCount -= 1;
		}
		else
		{
			// custom selected one
			str_format(aTmpBackendName, sizeof(aTmpBackendName), "%s (%s %d.%d.%d)", Localize("custom"), g_Config.m_GfxBackend, g_Config.m_GfxGLMajor, g_Config.m_GfxGLMinor, g_Config.m_GfxGLPatch);
			s_vBackendIdNames[CurCounter] = aTmpBackendName;
			s_vpBackendIdNamesCStr[CurCounter] = s_vBackendIdNames[CurCounter].c_str();
			SelectedOldBackend = CurCounter;

			s_vBackendInfos[CurCounter].m_pBackendName = "custom";
			s_vBackendInfos[CurCounter].m_Major = g_Config.m_GfxGLMajor;
			s_vBackendInfos[CurCounter].m_Minor = g_Config.m_GfxGLMinor;
			s_vBackendInfos[CurCounter].m_Patch = g_Config.m_GfxGLPatch;
		}

		static int s_SelectedOldBackend = -1;
		if(s_SelectedOldBackend == -1)
			s_SelectedOldBackend = SelectedOldBackend;

		static CUi::SDropDownState s_BackendDropDownState;
		static CScrollRegion s_BackendDropDownScrollRegion;
		s_BackendDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_BackendDropDownScrollRegion;
		const int NewBackend = Ui()->DoDropDown(&BackendDropDown, SelectedOldBackend, s_vpBackendIdNamesCStr.data(), BackendCount, s_BackendDropDownState);
		if(SelectedOldBackend != NewBackend)
		{
			str_copy(g_Config.m_GfxBackend, s_vBackendInfos[NewBackend].m_pBackendName);
			g_Config.m_GfxGLMajor = s_vBackendInfos[NewBackend].m_Major;
			g_Config.m_GfxGLMinor = s_vBackendInfos[NewBackend].m_Minor;
			g_Config.m_GfxGLPatch = s_vBackendInfos[NewBackend].m_Patch;

			CheckSettings = true;
			s_GfxBackendChanged = s_SelectedOldBackend != NewBackend;
		}
	}

	// GPU list
	const auto &GpuList = Graphics()->GetGpus();
	if(GpuList.m_vGpus.size() > 1)
	{
		CUIRect Text, GpuDropDown;
		MainView.HSplitTop(10.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &Text, &MainView);
		MainView.HSplitTop(2.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &GpuDropDown, &MainView);
		Ui()->DoLabel(&Text, Localize("Graphics card"), 16.0f, TEXTALIGN_MC);

		static std::vector<const char *> s_vpGpuIdNames;

		size_t GpuCount = GpuList.m_vGpus.size() + 1;
		s_vpGpuIdNames.resize(GpuCount);

		char aCurDeviceName[256 + 4];

		int OldSelectedGpu = -1;
		for(size_t i = 0; i < GpuCount; ++i)
		{
			if(i == 0)
			{
				str_format(aCurDeviceName, sizeof(aCurDeviceName), "%s (%s)", Localize("auto"), GpuList.m_AutoGpu.m_aName);
				s_vpGpuIdNames[i] = aCurDeviceName;
				if(str_comp("auto", g_Config.m_GfxGpuName) == 0)
				{
					OldSelectedGpu = 0;
				}
			}
			else
			{
				s_vpGpuIdNames[i] = GpuList.m_vGpus[i - 1].m_aName;
				if(str_comp(GpuList.m_vGpus[i - 1].m_aName, g_Config.m_GfxGpuName) == 0)
				{
					OldSelectedGpu = i;
				}
			}
		}

		static int s_OldSelectedGpu = -1;
		if(s_OldSelectedGpu == -1)
			s_OldSelectedGpu = OldSelectedGpu;

		static CUi::SDropDownState s_GpuDropDownState;
		static CScrollRegion s_GpuDropDownScrollRegion;
		s_GpuDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_GpuDropDownScrollRegion;
		const int NewGpu = Ui()->DoDropDown(&GpuDropDown, OldSelectedGpu, s_vpGpuIdNames.data(), GpuCount, s_GpuDropDownState);
		if(OldSelectedGpu != NewGpu)
		{
			if(NewGpu == 0)
				str_copy(g_Config.m_GfxGpuName, "auto");
			else
				str_copy(g_Config.m_GfxGpuName, GpuList.m_vGpus[NewGpu - 1].m_aName);
			CheckSettings = true;
			s_GfxGpuChanged = NewGpu != s_OldSelectedGpu;
		}
	}

	// check if the new settings require a restart
	if(CheckSettings)
	{
		m_NeedRestartGraphics = !(s_GfxFsaaSamples == g_Config.m_GfxFsaaSamples &&
					  !s_GfxBackendChanged &&
					  !s_GfxGpuChanged);
	}

	return maximum(BaseContentBottom, MainView.y);
}

float CMenus::RenderSettingsSound(CUIRect MainView)
{
	const float BaseContentBottom = MainView.y + MainView.h;
	static int s_SndEnable = g_Config.m_SndEnable;

	CUIRect Button;
	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndEnable, Localize("Use sounds"), g_Config.m_SndEnable, &Button))
	{
		g_Config.m_SndEnable ^= 1;
		UpdateMusicState();
		m_NeedRestartSound = g_Config.m_SndEnable && !s_SndEnable;
	}

	if(!g_Config.m_SndEnable)
		return BaseContentBottom;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndMusic, Localize("Play background music"), g_Config.m_SndMusic, &Button))
	{
		g_Config.m_SndMusic ^= 1;
		UpdateMusicState();
	}

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndNonactiveMute, Localize("Mute when not active"), g_Config.m_SndNonactiveMute, &Button))
		g_Config.m_SndNonactiveMute ^= 1;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndGame, Localize("Enable game sounds"), g_Config.m_SndGame, &Button))
		g_Config.m_SndGame ^= 1;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndGun, Localize("Enable gun sound"), g_Config.m_SndGun, &Button))
		g_Config.m_SndGun ^= 1;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndLongPain, Localize("Enable long pain sound (used when shooting in freeze)"), g_Config.m_SndLongPain, &Button))
		g_Config.m_SndLongPain ^= 1;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndServerMessage, Localize("Enable server message sound"), g_Config.m_SndServerMessage, &Button))
		g_Config.m_SndServerMessage ^= 1;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndChat, Localize("Enable regular chat sound"), g_Config.m_SndChat, &Button))
		g_Config.m_SndChat ^= 1;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndTeamChat, Localize("Enable team chat sound"), g_Config.m_SndTeamChat, &Button))
		g_Config.m_SndTeamChat ^= 1;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndHighlight, Localize("Enable highlighted chat sound"), g_Config.m_SndHighlight, &Button))
		g_Config.m_SndHighlight ^= 1;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_BcMenuSfx, Localize("Enable menu UI sounds (osu!lazer)"), g_Config.m_BcMenuSfx, &Button))
		g_Config.m_BcMenuSfx ^= 1;

	{
		MainView.HSplitTop(5.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_BcMenuSfxVolume, &g_Config.m_BcMenuSfxVolume, &Button, Localize("Menu UI sound volume"), 0, 100, &CUi::ms_LogarithmicScrollbarScale, 0u, "%");
	}

	// volume slider
	{
		MainView.HSplitTop(5.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_SndVolume, &g_Config.m_SndVolume, &Button, Localize("Sound volume"), 0, 100, &CUi::ms_LogarithmicScrollbarScale, 0u, "%");
	}

	// volume slider game sounds
	{
		MainView.HSplitTop(5.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_SndGameVolume, &g_Config.m_SndGameVolume, &Button, Localize("Game sound volume"), 0, 100, &CUi::ms_LogarithmicScrollbarScale, 0u, "%");
	}

	// volume slider gui sounds
	{
		MainView.HSplitTop(5.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_SndChatVolume, &g_Config.m_SndChatVolume, &Button, Localize("Chat sound volume"), 0, 100, &CUi::ms_LogarithmicScrollbarScale, 0u, "%");
	}

	// volume slider map sounds
	{
		MainView.HSplitTop(5.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_SndMapVolume, &g_Config.m_SndMapVolume, &Button, Localize("Map sound volume"), 0, 100, &CUi::ms_LogarithmicScrollbarScale, 0u, "%");
	}

	// volume slider background music
	{
		MainView.HSplitTop(5.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_SndBackgroundMusicVolume, &g_Config.m_SndBackgroundMusicVolume, &Button, Localize("Background music volume"), 0, 100, &CUi::ms_LogarithmicScrollbarScale, 0u, "%");
	}

	return maximum(BaseContentBottom, MainView.y);
}

void CMenus::RenderLanguageSettings(CUIRect MainView)
{
	const float CreditsFontSize = 14.0f;
	const float CreditsMargin = 10.0f;

	CUIRect List, CreditsScroll;
	MainView.HSplitBottom(4.0f * CreditsFontSize + 2.0f * CreditsMargin + CScrollRegion::HEIGHT_MAGIC_FIX, &List, &CreditsScroll);
	List.HSplitBottom(5.0f, &List, nullptr);

	RenderLanguageSelection(List);

	CreditsScroll.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 5.0f);

	static CScrollRegion s_CreditsScrollRegion;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = CreditsFontSize;
	s_CreditsScrollRegion.Begin(&CreditsScroll, &ScrollOffset, &ScrollParams);
	CreditsScroll.y += ScrollOffset.y;

	CTextCursor Cursor;
	Cursor.m_FontSize = CreditsFontSize;
	Cursor.m_LineWidth = CreditsScroll.w - 2.0f * CreditsMargin;

	const unsigned OldRenderFlags = TextRender()->GetRenderFlags();
	TextRender()->SetRenderFlags(OldRenderFlags | TEXT_RENDER_FLAG_ONE_TIME_USE);
	STextContainerIndex CreditsTextContainer;
	TextRender()->CreateTextContainer(CreditsTextContainer, &Cursor, Localize("English translation by the DDNet Team", "Translation credits: Add your own name here when you update translations"));
	TextRender()->SetRenderFlags(OldRenderFlags);
	if(CreditsTextContainer.Valid())
	{
		CUIRect CreditsLabel;
		CreditsScroll.HSplitTop(TextRender()->GetBoundingBoxTextContainer(CreditsTextContainer).m_H + 2.0f * CreditsMargin, &CreditsLabel, &CreditsScroll);
		s_CreditsScrollRegion.AddRect(CreditsLabel);
		CreditsLabel.Margin(CreditsMargin, &CreditsLabel);
		TextRender()->RenderTextContainer(CreditsTextContainer, TextRender()->DefaultTextColor(), TextRender()->DefaultTextOutlineColor(), CreditsLabel.x, CreditsLabel.y);
		TextRender()->DeleteTextContainer(CreditsTextContainer);
	}

	s_CreditsScrollRegion.End();
}

bool CMenus::RenderLanguageSelection(CUIRect MainView)
{
	static int s_SelectedLanguage = -2; // -2 = unloaded, -1 = unset
	static CListBox s_ListBox;

	if(s_SelectedLanguage == -2)
	{
		s_SelectedLanguage = -1;
		for(size_t i = 0; i < g_Localization.Languages().size(); i++)
		{
			if(str_comp(g_Localization.Languages()[i].m_Filename.c_str(), g_Config.m_ClLanguagefile) == 0)
			{
				s_SelectedLanguage = i;
				s_ListBox.ScrollToSelected();
				break;
			}
		}
	}

	const int SelectedOld = s_SelectedLanguage;

	s_ListBox.DoStart(24.0f, g_Localization.Languages().size(), 1, 3, s_SelectedLanguage, &MainView);

	for(const auto &Language : g_Localization.Languages())
	{
		const CListboxItem Item = s_ListBox.DoNextItem(&Language.m_Name, s_SelectedLanguage != -1 && !str_comp(g_Localization.Languages()[s_SelectedLanguage].m_Name.c_str(), Language.m_Name.c_str()));
		if(!Item.m_Visible)
			continue;

		CUIRect FlagRect, Label;
		Item.m_Rect.VSplitLeft(Item.m_Rect.h * 2.0f, &FlagRect, &Label);
		FlagRect.VMargin(6.0f, &FlagRect);
		FlagRect.HMargin(3.0f, &FlagRect);
		GameClient()->m_CountryFlags.Render(Language.m_CountryCode, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), FlagRect.x, FlagRect.y, FlagRect.w, FlagRect.h);

		Ui()->DoLabel(&Label, Language.m_Name.c_str(), 16.0f, TEXTALIGN_ML);
	}

	s_SelectedLanguage = s_ListBox.DoEnd();

	if(SelectedOld != s_SelectedLanguage)
	{
		str_copy(g_Config.m_ClLanguagefile, g_Localization.Languages()[s_SelectedLanguage].m_Filename.c_str());
		GameClient()->OnLanguageChange();
	}

	return s_ListBox.WasItemActivated();
}

void CMenus::SyncOldSettingsPageFromCurrent()
{
	g_Config.m_UiSettingsPage = minimum(maximum(g_Config.m_UiSettingsPage, 0), SETTINGS_LENGTH - 1);

	if(g_Config.m_UiSettingsPage == SETTINGS_GENERAL_TAB)
	{
		m_GeneralSubTab = minimum(maximum(m_GeneralSubTab, 0), GENERAL_SUBTAB_LENGTH - 1);
		if(m_GeneralSubTab == GENERAL_SUBTAB_CONTROLS)
			m_OldSettingsPage = OLD_SETTINGS_CONTROLS;
		else if(m_GeneralSubTab == GENERAL_SUBTAB_TEE)
			m_OldSettingsPage = OLD_SETTINGS_TEE;
		else if(m_GeneralSubTab == GENERAL_SUBTAB_DDNET)
			m_OldSettingsPage = OLD_SETTINGS_DDNET;
		else
			m_OldSettingsPage = OLD_SETTINGS_GENERAL;
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_APPEARANCE_TAB)
	{
		m_AppearanceSubTab = minimum(maximum(m_AppearanceSubTab, 0), APPEARANCE_SUBTAB_LENGTH - 1);
		if(m_AppearanceSubTab == APPEARANCE_SUBTAB_GRAPHICS)
			m_OldSettingsPage = OLD_SETTINGS_GRAPHICS;
		else if(m_AppearanceSubTab == APPEARANCE_SUBTAB_SOUND)
			m_OldSettingsPage = OLD_SETTINGS_SOUND;
		else if(m_AppearanceSubTab == APPEARANCE_SUBTAB_ASSETS)
			m_OldSettingsPage = OLD_SETTINGS_ASSETS;
		else
			m_OldSettingsPage = OLD_SETTINGS_APPEARANCE;
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_TCLIENT)
	{
		m_OldSettingsPage = OLD_SETTINGS_TCLIENT;
	}
	else
	{
		m_OldSettingsPage = OLD_SETTINGS_BESTCLIENT;
	}
}

void CMenus::SyncCurrentSettingsPageFromOld()
{
	m_OldSettingsPage = minimum(maximum(m_OldSettingsPage, 0), OLD_SETTINGS_LENGTH - 1);

	switch(m_OldSettingsPage)
	{
	case OLD_SETTINGS_GRAPHICS:
		g_Config.m_UiSettingsPage = SETTINGS_APPEARANCE_TAB;
		m_AppearanceSubTab = APPEARANCE_SUBTAB_GRAPHICS;
		break;
	case OLD_SETTINGS_SOUND:
		g_Config.m_UiSettingsPage = SETTINGS_APPEARANCE_TAB;
		m_AppearanceSubTab = APPEARANCE_SUBTAB_SOUND;
		break;
	case OLD_SETTINGS_ASSETS:
		g_Config.m_UiSettingsPage = SETTINGS_APPEARANCE_TAB;
		m_AppearanceSubTab = APPEARANCE_SUBTAB_ASSETS;
		break;
	case OLD_SETTINGS_APPEARANCE:
		g_Config.m_UiSettingsPage = SETTINGS_APPEARANCE_TAB;
		m_AppearanceSubTab = APPEARANCE_SUBTAB_APPEARANCE;
		break;
	case OLD_SETTINGS_CONTROLS:
		g_Config.m_UiSettingsPage = SETTINGS_GENERAL_TAB;
		m_GeneralSubTab = GENERAL_SUBTAB_CONTROLS;
		break;
	case OLD_SETTINGS_TEE:
		g_Config.m_UiSettingsPage = SETTINGS_GENERAL_TAB;
		m_GeneralSubTab = GENERAL_SUBTAB_TEE;
		break;
	case OLD_SETTINGS_DDNET:
		g_Config.m_UiSettingsPage = SETTINGS_GENERAL_TAB;
		m_GeneralSubTab = GENERAL_SUBTAB_DDNET;
		break;
	case OLD_SETTINGS_BESTCLIENT:
		g_Config.m_UiSettingsPage = SETTINGS_BESTCLIENT;
		break;
	case OLD_SETTINGS_TCLIENT:
		g_Config.m_UiSettingsPage = SETTINGS_TCLIENT;
		break;
	case OLD_SETTINGS_GENERAL:
	default:
		g_Config.m_UiSettingsPage = SETTINGS_GENERAL_TAB;
		m_GeneralSubTab = GENERAL_SUBTAB_GENERAL;
		break;
	}
}

void CMenus::RenderSettingsSearchRow(CUIRect &ContentView)
{
	static CButtonContainer s_SearchCloseButton;
	if(!m_SettingsSearchOpen)
		return;

	CUIRect SearchRow;
	ContentView.HSplitTop(24.0f, &SearchRow, &ContentView);
	CUIRect SearchInput, SearchClose;
	SearchRow.VSplitRight(24.0f, &SearchInput, &SearchClose);
	SearchInput.VSplitRight(4.0f, &SearchInput, nullptr);
	Ui()->DoEditBox_Search(&m_SettingsSearchInput, &SearchInput, 12.0f, !Ui()->IsPopupOpen());
	if(Ui()->DoButton_FontIcon(&s_SearchCloseButton, FONT_ICON_XMARK, 0, &SearchClose, IGraphics::CORNER_ALL) || (!Ui()->IsPopupOpen() && Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE)))
		ResetSettingsSearch();
	ContentView.HSplitTop(8.0f, nullptr, &ContentView);
}

void CMenus::UpdateSettingsSearchNavigation()
{
	const char *pSearch = m_SettingsSearchOpen ? m_SettingsSearchInput.GetString() : "";
	if(m_SettingsSearchOpen && pSearch[0] != '\0')
	{
		const bool ForceApply = m_SettingsSearchInput.IsActive() && Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER);
		if(str_comp(m_aSettingsSearchLastApplied, pSearch) != 0 || ForceApply)
		{
			int BestScore = -1;
			int SecondBestScore = -1;
			const SSettingsSearchEntry *pSelectedEntry = FindBestSettingsSearchEntry(pSearch, &BestScore, &SecondBestScore);
			const bool CanNavigate = pSelectedEntry != nullptr && (ForceApply || SearchShouldNavigateToEntry(pSearch, BestScore, SecondBestScore));
			if(CanNavigate)
			{
				g_Config.m_UiSettingsPage = pSelectedEntry->m_Page;
				if(pSelectedEntry->m_GeneralSubTab >= 0)
					m_GeneralSubTab = pSelectedEntry->m_GeneralSubTab;
				if(pSelectedEntry->m_AppearanceSubTab >= 0)
					m_AppearanceSubTab = pSelectedEntry->m_AppearanceSubTab;
				if(pSelectedEntry->m_BestClientTab >= 0 || pSelectedEntry->m_BestClientVisualsSubTab >= 0 || pSelectedEntry->m_BestClientGoresCategory >= 0)
					SettingsSearchJumpToBestClient(pSelectedEntry->m_BestClientTab, pSelectedEntry->m_BestClientVisualsSubTab, pSelectedEntry->m_BestClientGoresCategory);
				SettingsSearchSetFocusKey(pSelectedEntry->m_pFocusKey);
				m_SettingsSearchRevealPending = true;
				m_SettingsSearchRevealFrames = 90;
				if(g_Config.m_BcSettingsLayout == 1)
					SyncOldSettingsPageFromCurrent();
			}
			else
			{
				SettingsSearchSetFocusKey("");
				m_SettingsSearchRevealPending = true;
				m_SettingsSearchRevealFrames = 90;
			}
			str_copy(m_aSettingsSearchLastApplied, pSearch, sizeof(m_aSettingsSearchLastApplied));
		}
	}
	else
	{
		m_aSettingsSearchLastApplied[0] = '\0';
		SettingsSearchSetFocusKey("");
		m_SettingsSearchRevealPending = false;
		m_SettingsSearchRevealFrames = 0;
	}

	// For free-text matches (without an explicit focus key), keep reveal active
	// for a bounded amount of frames so scrolling can settle on deep matches.
	if(m_SettingsSearchRevealPending && m_aSettingsSearchFocusKey[0] == '\0' && m_SettingsSearchRevealFrames > 0)
	{
		--m_SettingsSearchRevealFrames;
		if(m_SettingsSearchRevealFrames <= 0)
			m_SettingsSearchRevealPending = false;
	}
}

void CMenus::RenderSettingsRestartWarning(CUIRect RestartBar)
{
	CUIRect RestartWarning, RestartButton;
	RestartBar.VSplitRight(125.0f, &RestartWarning, &RestartButton);
	RestartWarning.VSplitRight(10.0f, &RestartWarning, nullptr);

	if(m_NeedRestartUpdate)
	{
		TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
		Ui()->DoLabel(&RestartWarning, Localize("DDNet Client needs to be restarted to complete update!"), 14.0f, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	}
	else
	{
		Ui()->DoLabel(&RestartWarning, Localize("You must restart the game for all settings to take effect."), 14.0f, TEXTALIGN_ML);
	}

	static CButtonContainer sRestartButton;
	if(DoButton_Menu(&sRestartButton, Localize("Restart"), 0, &RestartButton))
	{
		if(Client()->State() == IClient::STATE_ONLINE || GameClient()->Editor()->HasUnsavedData())
			m_Popup = POPUP_RESTART;
		else
			Client()->Restart();
	}
}

void CMenus::RenderSettingsNew(CUIRect MainView, bool NeedRestart)
{
	CUIRect Button, TabBar, RestartBar, ContentView;
	MainView.Draw(ms_ColorTabbarActive, IGraphics::CORNER_B, 10.0f);
	MainView.Margin(20.0f, &MainView);
	ContentView = MainView;

	if(NeedRestart)
	{
		ContentView.HSplitBottom(20.0f, &ContentView, &RestartBar);
		ContentView.HSplitBottom(10.0f, &ContentView, nullptr);
	}

	RenderSettingsSearchRow(ContentView);
	UpdateSettingsSearchNavigation();

	// Top tab bar (slightly taller than regular buttons).
	ContentView.HSplitTop(40.0f, &TabBar, &ContentView);
	TabBar.HMargin(6.0f, &TabBar);

	g_Config.m_UiSettingsPage = minimum(maximum(g_Config.m_UiSettingsPage, 0), SETTINGS_LENGTH - 1);

	// Main tabs at top
	const char *apTabs[] = {Localize("General"), Localize("Appearance"), TCLocalize("TClient"), TCLocalize("BestClient")};
	static CButtonContainer saTabButtons[SETTINGS_LENGTH];

	CUIRect TabsBar = TabBar;
	CUIRect SearchButtonRect;
	TabsBar.VSplitRight(30.0f, &TabsBar, &SearchButtonRect);
	TabsBar.VSplitRight(6.0f, &TabsBar, nullptr);
	static CButtonContainer s_SettingsSearchButton;
	const char *pSearchIcon = m_SettingsSearchOpen ? FONT_ICON_XMARK : FONT_ICON_MAGNIFYING_GLASS;
	const bool SettingsSearchToggleEnabled = !IsBestClientFunTabActive() || m_SettingsSearchOpen;
	const int SearchClicked = Ui()->DoButton_FontIcon(&s_SettingsSearchButton, pSearchIcon, m_SettingsSearchOpen ? 1 : 0, &SearchButtonRect, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, SettingsSearchToggleEnabled);
	if(SettingsSearchToggleEnabled && SearchClicked)
	{
		if(m_SettingsSearchOpen)
			ResetSettingsSearch();
		else
			OpenSettingsSearch();
	}

	const float TabWidth = TabsBar.w / SETTINGS_LENGTH;

	for(int i = 0; i < SETTINGS_LENGTH; i++)
	{
		TabsBar.VSplitLeft(TabWidth, &Button, &TabsBar);

		int Corners = IGraphics::CORNER_NONE;
		if(i == 0)
			Corners = IGraphics::CORNER_TL | IGraphics::CORNER_BL;
		else if(i == SETTINGS_LENGTH - 1)
			Corners = IGraphics::CORNER_TR | IGraphics::CORNER_BR;

		if((i == SETTINGS_GENERAL_TAB && SettingsSearchIsHighlight("main_general_tab")) ||
			(i == SETTINGS_APPEARANCE_TAB && SettingsSearchIsHighlight("main_appearance_tab")) ||
			(i == SETTINGS_TCLIENT && SettingsSearchIsHighlight("main_tclient_tab")) ||
			(i == SETTINGS_BESTCLIENT && SettingsSearchIsHighlight("main_bestclient_tab")))
		{
			Button.Draw(ColorRGBA(1.0f, 0.9f, 0.25f, 0.2f), Corners, 4.0f);
		}

		if(DoButton_MenuTab(&saTabButtons[i], apTabs[i], g_Config.m_UiSettingsPage == i, &Button, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
			g_Config.m_UiSettingsPage = i;
	}

	if(g_Config.m_UiSettingsPage == SETTINGS_GENERAL_TAB)
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_LANGUAGE);
	else if(g_Config.m_UiSettingsPage == SETTINGS_APPEARANCE_TAB)
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_CONTROLS);
	else if(g_Config.m_UiSettingsPage == SETTINGS_TCLIENT)
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_RESERVED1);
	else if(g_Config.m_UiSettingsPage == SETTINGS_BESTCLIENT)
		GameClient()->m_MenuBackground.ChangePosition(gs_BestClientSettingsBackgroundPosition);

	const bool PageAnimationsEnabled = false;
	const float PageAnimationDuration = 0.0f;
	struct SScopedClip
	{
		CUi *m_pUi;
		~SScopedClip() { m_pUi->ClipDisable(); }
	};

	auto UpdateTransition = [&](SBestClientTabTransition &Transition, int TargetTab) {
		if(Transition.m_Current < 0)
		{
			Transition.m_Current = TargetTab;
			Transition.m_Previous = TargetTab;
			Transition.m_Phase = 1.0f;
		}
		if(Transition.m_Current != TargetTab)
		{
			Transition.m_Previous = Transition.m_Current;
			Transition.m_Current = TargetTab;
			Transition.m_Phase = PageAnimationsEnabled ? 0.0f : 1.0f;
		}
		if(PageAnimationsEnabled)
			BCUiAnimations::UpdatePhase(Transition.m_Phase, 1.0f, Client()->RenderFrameTime(), PageAnimationDuration);
		else
			Transition.m_Phase = 1.0f;
		if(Transition.m_Phase >= 1.0f)
			Transition.m_Previous = Transition.m_Current;
	};

	auto IsAnimating = [&](const SBestClientTabTransition &Transition) {
		return PageAnimationsEnabled && Transition.m_Previous != Transition.m_Current && Transition.m_Phase < 1.0f;
	};

	auto RenderSlidingContent = [&](SBestClientTabTransition &Transition, const CUIRect &View, auto &&RenderFn) {
		if(IsAnimating(Transition))
		{
			const float SlideMax = minimum(View.w * 0.16f, 52.0f);
			const float Ease = BCUiAnimations::EaseInOutQuad(Transition.m_Phase);
			const float Offset = (1.0f - Ease) * SlideMax;
			const int Direction = Transition.m_Current > Transition.m_Previous ? 1 : -1;
			const bool WasEnabled = Ui()->Enabled();

			CUIRect PrevView = View;
			CUIRect CurrentView = View;
			PrevView.x -= Direction * Offset;
			CurrentView.x += Direction * (SlideMax - Offset);

			Ui()->ClipEnable(&View);
			SScopedClip ClipGuard{Ui()};
			Ui()->SetEnabled(false);
			RenderFn(Transition.m_Previous, PrevView);
			Ui()->SetEnabled(WasEnabled);
			RenderFn(Transition.m_Current, CurrentView);
			Ui()->SetEnabled(WasEnabled);
		}
		else
			RenderFn(Transition.m_Current, View);
	};

	UpdateTransition(m_SettingsPageTransition, g_Config.m_UiSettingsPage);

	const bool NeedsBestClientVisible = g_Config.m_UiSettingsPage == SETTINGS_BESTCLIENT || (IsAnimating(m_SettingsPageTransition) && m_SettingsPageTransition.m_Previous == SETTINGS_BESTCLIENT);
	if(!NeedsBestClientVisible && (m_AssetsEditorState.m_VisualsEditorOpen || m_AssetsEditorState.m_VisualsEditorInitialized))
	{
		m_AssetsEditorState.m_VisualsEditorOpen = false;
		AssetsEditorClearAssets();
	}
	if(!NeedsBestClientVisible && m_ComponentsEditorState.m_Open)
	{
		ComponentsEditorCloseNow();
	}

	auto RenderGeneralSubPage = [&](int SubTab, CUIRect View) {
		if(SubTab == GENERAL_SUBTAB_GENERAL)
			RenderAutoScrollSettingsPage(View, SETTINGS_SCROLL_PAGE_GENERAL, [this](CUIRect Page) { return RenderSettingsGeneral(Page); });
		else if(SubTab == GENERAL_SUBTAB_CONTROLS)
			m_MenusSettingsControls.Render(View);
		else if(SubTab == GENERAL_SUBTAB_TEE)
		{
			if(Client()->IsSixup())
				RenderSettingsTee7(View);
			else
				RenderSettingsTee(View);
		}
		else if(SubTab == GENERAL_SUBTAB_DDNET)
			RenderAutoScrollSettingsPage(View, SETTINGS_SCROLL_PAGE_DDNET, [this](CUIRect Page) { return RenderSettingsDDNet(Page); });
	};

	auto RenderAppearanceSubPage = [&](int SubTab, CUIRect View) {
		if(SubTab == APPEARANCE_SUBTAB_APPEARANCE)
			RenderAutoScrollSettingsPage(View, SETTINGS_SCROLL_PAGE_APPEARANCE, [this](CUIRect Page) { return RenderSettingsAppearance(Page); });
		else if(SubTab == APPEARANCE_SUBTAB_GRAPHICS)
			RenderAutoScrollSettingsPage(View, SETTINGS_SCROLL_PAGE_GRAPHICS, [this](CUIRect Page) { return RenderSettingsGraphics(Page); });
		else if(SubTab == APPEARANCE_SUBTAB_SOUND)
			RenderAutoScrollSettingsPage(View, SETTINGS_SCROLL_PAGE_SOUND, [this](CUIRect Page) { return RenderSettingsSound(Page); });
		else if(SubTab == APPEARANCE_SUBTAB_ASSETS)
			RenderSettingsCustom(View);
	};

	auto RenderGeneralPage = [&](CUIRect View) {
		View.HSplitTop(10.0f, nullptr, &View);

		CUIRect SubTabBar, PageView;
		View.HSplitTop(24.0f, &SubTabBar, &PageView);
		PageView.HSplitTop(10.0f, nullptr, &PageView);

		m_GeneralSubTab = minimum(maximum(m_GeneralSubTab, 0), GENERAL_SUBTAB_LENGTH - 1);
		const char *apSubTabs[] = {Localize("General"), Localize("Controls"), Client()->IsSixup() ? "Tee (0.7)" : Localize("Tee"), Localize("DDNet")};
		const float SubTabWidth = SubTabBar.w / GENERAL_SUBTAB_LENGTH;

		static CButtonContainer saGeneralSubTabButtons[GENERAL_SUBTAB_LENGTH];
		for(int i = 0; i < GENERAL_SUBTAB_LENGTH; i++)
		{
			CUIRect SubTab;
			SubTabBar.VSplitLeft(SubTabWidth, &SubTab, &SubTabBar);

			int Corners = IGraphics::CORNER_NONE;
			if(i == 0)
				Corners = IGraphics::CORNER_TL | IGraphics::CORNER_BL;
			else if(i == GENERAL_SUBTAB_LENGTH - 1)
				Corners = IGraphics::CORNER_TR | IGraphics::CORNER_BR;

			if((i == GENERAL_SUBTAB_GENERAL && (SettingsSearchIsHighlight("general_dynamic_camera") || SettingsSearchIsHighlight("general_client") || SettingsSearchIsHighlight("general_language") || SettingsSearchIsHighlight("general_autoswitch"))) ||
				(i == GENERAL_SUBTAB_CONTROLS && SettingsSearchIsHighlight("general_controls_tab")) ||
				(i == GENERAL_SUBTAB_TEE && SettingsSearchIsHighlight("general_tee_subtab")) ||
				(i == GENERAL_SUBTAB_DDNET && SettingsSearchIsHighlight("general_ddnet_tab")))
			{
				SubTab.Draw(ColorRGBA(1.0f, 0.9f, 0.25f, 0.2f), Corners, 4.0f);
			}

			if(DoButton_MenuTab(&saGeneralSubTabButtons[i], apSubTabs[i], m_GeneralSubTab == i, &SubTab, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
				m_GeneralSubTab = i;
		}

		UpdateTransition(m_SettingsGeneralSubTabTransition, m_GeneralSubTab);
		RenderSlidingContent(m_SettingsGeneralSubTabTransition, PageView, [&](int SubTab, CUIRect SubView) { RenderGeneralSubPage(SubTab, SubView); });
	};

	auto RenderAppearancePage = [&](CUIRect View) {
		View.HSplitTop(10.0f, nullptr, &View);

		CUIRect SubTabBar, PageView;
		View.HSplitTop(24.0f, &SubTabBar, &PageView);
		PageView.HSplitTop(10.0f, nullptr, &PageView);

		m_AppearanceSubTab = minimum(maximum(m_AppearanceSubTab, 0), APPEARANCE_SUBTAB_LENGTH - 1);
		const char *apSubTabs[] = {Localize("Appearance"), Localize("Graphics"), Localize("Sound"), Localize("Assets")};
		const float SubTabWidth = SubTabBar.w / APPEARANCE_SUBTAB_LENGTH;

		static CButtonContainer saAppearanceSubTabButtons[APPEARANCE_SUBTAB_LENGTH];
		for(int i = 0; i < APPEARANCE_SUBTAB_LENGTH; i++)
		{
			CUIRect SubTab;
			SubTabBar.VSplitLeft(SubTabWidth, &SubTab, &SubTabBar);

			int Corners = IGraphics::CORNER_NONE;
			if(i == 0)
				Corners = IGraphics::CORNER_TL | IGraphics::CORNER_BL;
			else if(i == APPEARANCE_SUBTAB_LENGTH - 1)
				Corners = IGraphics::CORNER_TR | IGraphics::CORNER_BR;

			if((i == APPEARANCE_SUBTAB_APPEARANCE && SettingsSearchIsHighlight("appearance_appearance_tab")) ||
				(i == APPEARANCE_SUBTAB_GRAPHICS && SettingsSearchIsHighlight("appearance_graphics_tab")) ||
				(i == APPEARANCE_SUBTAB_SOUND && SettingsSearchIsHighlight("appearance_sound_tab")) ||
				(i == APPEARANCE_SUBTAB_ASSETS && SettingsSearchIsHighlight("appearance_assets_tab")))
			{
				SubTab.Draw(ColorRGBA(1.0f, 0.9f, 0.25f, 0.2f), Corners, 4.0f);
			}

			if(DoButton_MenuTab(&saAppearanceSubTabButtons[i], apSubTabs[i], m_AppearanceSubTab == i, &SubTab, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
				m_AppearanceSubTab = i;
		}

		UpdateTransition(m_SettingsAppearanceSubTabTransition, m_AppearanceSubTab);
		RenderSlidingContent(m_SettingsAppearanceSubTabTransition, PageView, [&](int SubTab, CUIRect SubView) { RenderAppearanceSubPage(SubTab, SubView); });
	};

	auto RenderSettingsRootPage = [&](int Page, CUIRect View) {
		if(Page == SETTINGS_GENERAL_TAB)
			RenderGeneralPage(View);
		else if(Page == SETTINGS_APPEARANCE_TAB)
			RenderAppearancePage(View);
		else if(Page == SETTINGS_TCLIENT)
		{
			View.HSplitTop(10.0f, nullptr, &View);
			RenderSettingsTClient(View);
		}
		else if(Page == SETTINGS_BESTCLIENT)
		{
			View.HSplitTop(10.0f, nullptr, &View);
			RenderSettingsBestClient(View);
		}
	};

	RenderSlidingContent(m_SettingsPageTransition, ContentView, [&](int Page, CUIRect View) { RenderSettingsRootPage(Page, View); });

	if(NeedRestart)
		RenderSettingsRestartWarning(RestartBar);
}

void CMenus::RenderSettingsOld(CUIRect MainView, bool NeedRestart)
{
	const bool FullscreenBestClientEditors = m_OldSettingsPage == OLD_SETTINGS_BESTCLIENT &&
		((m_AssetsEditorState.m_VisualsEditorOpen && m_AssetsEditorState.m_FullscreenOpen) ||
			(m_ComponentsEditorState.m_Open && m_ComponentsEditorState.m_FullscreenOpen));
	if(FullscreenBestClientEditors)
	{
		Ui()->SetLabelMatchCallback(nullptr, nullptr);
		GameClient()->m_MenuBackground.ChangePosition(gs_BestClientSettingsBackgroundPosition);
		RenderSettingsBestClient(MainView);
		return;
	}

	CUIRect Button, ContentPanel, TabBar;
	MainView.VSplitRight(120.0f, &ContentPanel, &TabBar);
	ContentPanel.Draw(ms_ColorTabbarActive, IGraphics::CORNER_B, 10.0f);
	ContentPanel.Margin(20.0f, &ContentPanel);

	CUIRect RestartBar, PageView;
	PageView = ContentPanel;
	if(NeedRestart)
	{
		PageView.HSplitBottom(20.0f, &PageView, &RestartBar);
		PageView.HSplitBottom(10.0f, &PageView, nullptr);
	}

	RenderSettingsSearchRow(PageView);
	UpdateSettingsSearchNavigation();

	TabBar.HSplitTop(50.0f, &Button, &TabBar);
	Button.Draw(ms_ColorTabbarActive, IGraphics::CORNER_BR, 10.0f);
	{
		CUIRect SearchButtonRect = Button;
		SearchButtonRect.VSplitRight(40.0f, nullptr, &SearchButtonRect);
		SearchButtonRect.HSplitTop(11.0f, nullptr, &SearchButtonRect);
		SearchButtonRect.HSplitBottom(11.0f, &SearchButtonRect, nullptr);
		SearchButtonRect.VSplitLeft(6.0f, nullptr, &SearchButtonRect);
		SearchButtonRect.VSplitRight(6.0f, &SearchButtonRect, nullptr);

		static CButtonContainer s_SettingsSearchButtonOld;
		const char *pSearchIcon = m_SettingsSearchOpen ? FONT_ICON_XMARK : FONT_ICON_MAGNIFYING_GLASS;
		const bool SettingsSearchToggleEnabled = !IsBestClientFunTabActive() || m_SettingsSearchOpen;
		const int SearchClicked = Ui()->DoButton_FontIcon(&s_SettingsSearchButtonOld, pSearchIcon, m_SettingsSearchOpen ? 1 : 0, &SearchButtonRect, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, SettingsSearchToggleEnabled);
		if(SettingsSearchToggleEnabled && SearchClicked)
		{
			if(m_SettingsSearchOpen)
				ResetSettingsSearch();
			else
				OpenSettingsSearch();
		}
	}

	m_OldSettingsPage = minimum(maximum(m_OldSettingsPage, 0), OLD_SETTINGS_LENGTH - 1);

	const char *apTabs[OLD_SETTINGS_LENGTH] = {
		Localize("General"),
		Client()->IsSixup() ? "Tee 0.7" : Localize("Tee"),
		Localize("Appearance"),
		Localize("Controls"),
		Localize("Graphics"),
		Localize("Sound"),
		Localize("DDNet"),
		Localize("Assets"),
		TCLocalize("TClient"),
		TCLocalize("BestClient")};

	static CButtonContainer s_aTabButtons[OLD_SETTINGS_LENGTH];
	for(int i = 0; i < OLD_SETTINGS_LENGTH; i++)
	{
		TabBar.HSplitTop(10.0f, nullptr, &TabBar);
		TabBar.HSplitTop(26.0f, &Button, &TabBar);
		if(DoButton_MenuTab(&s_aTabButtons[i], apTabs[i], m_OldSettingsPage == i, &Button, IGraphics::CORNER_R, &m_aAnimatorsOldSettingsTab[i]))
		{
			m_OldSettingsPage = i;
			SyncCurrentSettingsPageFromOld();
		}
	}

	if(m_OldSettingsPage != OLD_SETTINGS_BESTCLIENT)
	{
		if(m_AssetsEditorState.m_VisualsEditorOpen || m_AssetsEditorState.m_VisualsEditorInitialized)
		{
			m_AssetsEditorState.m_VisualsEditorOpen = false;
			AssetsEditorClearAssets();
		}
		if(m_ComponentsEditorState.m_Open)
		{
			ComponentsEditorCloseNow();
		}
	}

	switch(m_OldSettingsPage)
	{
	case OLD_SETTINGS_GENERAL:
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_GENERAL);
		RenderAutoScrollSettingsPage(PageView, SETTINGS_SCROLL_PAGE_GENERAL, [this](CUIRect View) { return RenderSettingsGeneral(View); });
		break;
	case OLD_SETTINGS_TEE:
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_TEE);
		if(Client()->IsSixup())
			RenderSettingsTee7(PageView);
		else
			RenderSettingsTee(PageView);
		break;
	case OLD_SETTINGS_APPEARANCE:
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_APPEARANCE);
		RenderAutoScrollSettingsPage(PageView, SETTINGS_SCROLL_PAGE_APPEARANCE, [this](CUIRect View) { return RenderSettingsAppearance(View); });
		break;
	case OLD_SETTINGS_CONTROLS:
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_CONTROLS);
		m_MenusSettingsControls.Render(PageView);
		break;
	case OLD_SETTINGS_GRAPHICS:
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_GRAPHICS);
		RenderAutoScrollSettingsPage(PageView, SETTINGS_SCROLL_PAGE_GRAPHICS, [this](CUIRect View) { return RenderSettingsGraphics(View); });
		break;
	case OLD_SETTINGS_SOUND:
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_SOUND);
		RenderAutoScrollSettingsPage(PageView, SETTINGS_SCROLL_PAGE_SOUND, [this](CUIRect View) { return RenderSettingsSound(View); });
		break;
	case OLD_SETTINGS_DDNET:
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_DDNET);
		RenderAutoScrollSettingsPage(PageView, SETTINGS_SCROLL_PAGE_DDNET, [this](CUIRect View) { return RenderSettingsDDNet(View); });
		break;
	case OLD_SETTINGS_ASSETS:
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_ASSETS);
		RenderSettingsCustom(PageView);
		break;
	case OLD_SETTINGS_TCLIENT:
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_RESERVED1);
		RenderSettingsTClient(PageView);
		break;
	case OLD_SETTINGS_BESTCLIENT:
	default:
		GameClient()->m_MenuBackground.ChangePosition(gs_BestClientSettingsBackgroundPosition);
		RenderSettingsBestClient(PageView);
		break;
	}

	if(NeedRestart)
		RenderSettingsRestartWarning(RestartBar);
}

void CMenus::RenderSettings(CUIRect MainView)
{
	const bool FullscreenBestClientEditors = g_Config.m_UiSettingsPage == SETTINGS_BESTCLIENT &&
		((m_AssetsEditorState.m_VisualsEditorOpen && m_AssetsEditorState.m_FullscreenOpen) ||
			(m_ComponentsEditorState.m_Open && m_ComponentsEditorState.m_FullscreenOpen));
	if(FullscreenBestClientEditors)
	{
		Ui()->SetLabelMatchCallback(nullptr, nullptr);
		RenderSettingsBestClient(MainView);
		return;
	}

	m_pSettingsSearchActiveScrollRegion = nullptr;

	if(IsBestClientFunTabActive() && m_SettingsSearchOpen)
		ResetSettingsSearch();

	Ui()->SetLabelMatchCallback(m_SettingsSearchOpen ? &CMenus::SettingsSearchLabelMatchCallback : nullptr, this);

	const bool NeedRestart = m_NeedRestartGraphics || m_NeedRestartSound || m_NeedRestartUpdate;

	g_Config.m_BcSettingsLayout = minimum(maximum(g_Config.m_BcSettingsLayout, 0), 1);
	if(m_LastSettingsLayout != g_Config.m_BcSettingsLayout)
	{
		if(g_Config.m_BcSettingsLayout == 1)
			SyncOldSettingsPageFromCurrent();
		else
			SyncCurrentSettingsPageFromOld();
		m_LastSettingsLayout = g_Config.m_BcSettingsLayout;
	}

	if(g_Config.m_BcSettingsLayout == 1)
		RenderSettingsOld(MainView, NeedRestart);
	else
		RenderSettingsNew(MainView, NeedRestart);

	Ui()->SetLabelMatchCallback(nullptr, nullptr);
	m_pSettingsSearchActiveScrollRegion = nullptr;
}

bool CMenus::RenderHslaScrollbars(CUIRect *pRect, unsigned int *pColor, bool Alpha, float DarkestLight)
{
	const unsigned PrevPackedColor = *pColor;
	ColorHSLA Color(*pColor, Alpha);
	const ColorHSLA OriginalColor = Color;
	const char *apLabels[] = {Localize("Hue"), Localize("Sat."), Localize("Lht."), Localize("Alpha")};
	const float SizePerEntry = 20.0f;
	const float MarginPerEntry = 5.0f;
	const float PreviewMargin = 2.5f;
	const float PreviewHeight = 40.0f + 2 * PreviewMargin;
	const float OffY = (SizePerEntry + MarginPerEntry) * (3 + (Alpha ? 1 : 0)) - PreviewHeight;

	CUIRect Preview;
	pRect->VSplitLeft(PreviewHeight, &Preview, pRect);
	Preview.HSplitTop(OffY / 2.0f, nullptr, &Preview);
	Preview.HSplitTop(PreviewHeight, &Preview, nullptr);

	Preview.Draw(ColorRGBA(0.15f, 0.15f, 0.15f, 1.0f), IGraphics::CORNER_ALL, 4.0f + PreviewMargin);
	Preview.Margin(PreviewMargin, &Preview);
	Preview.Draw(color_cast<ColorRGBA>(Color.UnclampLighting(DarkestLight)), IGraphics::CORNER_ALL, 4.0f + PreviewMargin);

	auto &&RenderHueRect = [&](CUIRect *pColorRect) {
		float CurXOff = pColorRect->x;
		const float SizeColor = pColorRect->w / 6;

		// red to yellow
		{
			IGraphics::CColorVertex aColorVertices[] = {
				IGraphics::CColorVertex(0, 1, 0, 0, 1),
				IGraphics::CColorVertex(1, 1, 1, 0, 1),
				IGraphics::CColorVertex(2, 1, 0, 0, 1),
				IGraphics::CColorVertex(3, 1, 1, 0, 1)};
			Graphics()->SetColorVertex(aColorVertices, std::size(aColorVertices));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		// yellow to green
		CurXOff += SizeColor;
		{
			IGraphics::CColorVertex aColorVertices[] = {
				IGraphics::CColorVertex(0, 1, 1, 0, 1),
				IGraphics::CColorVertex(1, 0, 1, 0, 1),
				IGraphics::CColorVertex(2, 1, 1, 0, 1),
				IGraphics::CColorVertex(3, 0, 1, 0, 1)};
			Graphics()->SetColorVertex(aColorVertices, std::size(aColorVertices));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		CurXOff += SizeColor;
		// green to turquoise
		{
			IGraphics::CColorVertex aColorVertices[] = {
				IGraphics::CColorVertex(0, 0, 1, 0, 1),
				IGraphics::CColorVertex(1, 0, 1, 1, 1),
				IGraphics::CColorVertex(2, 0, 1, 0, 1),
				IGraphics::CColorVertex(3, 0, 1, 1, 1)};
			Graphics()->SetColorVertex(aColorVertices, std::size(aColorVertices));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		CurXOff += SizeColor;
		// turquoise to blue
		{
			IGraphics::CColorVertex aColorVertices[] = {
				IGraphics::CColorVertex(0, 0, 1, 1, 1),
				IGraphics::CColorVertex(1, 0, 0, 1, 1),
				IGraphics::CColorVertex(2, 0, 1, 1, 1),
				IGraphics::CColorVertex(3, 0, 0, 1, 1)};
			Graphics()->SetColorVertex(aColorVertices, std::size(aColorVertices));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		CurXOff += SizeColor;
		// blue to purple
		{
			IGraphics::CColorVertex aColorVertices[] = {
				IGraphics::CColorVertex(0, 0, 0, 1, 1),
				IGraphics::CColorVertex(1, 1, 0, 1, 1),
				IGraphics::CColorVertex(2, 0, 0, 1, 1),
				IGraphics::CColorVertex(3, 1, 0, 1, 1)};
			Graphics()->SetColorVertex(aColorVertices, std::size(aColorVertices));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		CurXOff += SizeColor;
		// purple to red
		{
			IGraphics::CColorVertex aColorVertices[] = {
				IGraphics::CColorVertex(0, 1, 0, 1, 1),
				IGraphics::CColorVertex(1, 1, 0, 0, 1),
				IGraphics::CColorVertex(2, 1, 0, 1, 1),
				IGraphics::CColorVertex(3, 1, 0, 0, 1)};
			Graphics()->SetColorVertex(aColorVertices, std::size(aColorVertices));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}
	};

	auto &&RenderSaturationRect = [&](CUIRect *pColorRect, const ColorRGBA &CurColor) {
		ColorHSLA LeftColor = color_cast<ColorHSLA>(CurColor);
		ColorHSLA RightColor = color_cast<ColorHSLA>(CurColor);

		LeftColor.s = 0.0f;
		RightColor.s = 1.0f;

		const ColorRGBA LeftColorRGBA = color_cast<ColorRGBA>(LeftColor);
		const ColorRGBA RightColorRGBA = color_cast<ColorRGBA>(RightColor);

		Graphics()->SetColor4(LeftColorRGBA, RightColorRGBA, RightColorRGBA, LeftColorRGBA);

		IGraphics::CFreeformItem Freeform(
			pColorRect->x, pColorRect->y,
			pColorRect->x + pColorRect->w, pColorRect->y,
			pColorRect->x, pColorRect->y + pColorRect->h,
			pColorRect->x + pColorRect->w, pColorRect->y + pColorRect->h);
		Graphics()->QuadsDrawFreeform(&Freeform, 1);
	};

	auto &&RenderLightingRect = [&](CUIRect *pColorRect, const ColorRGBA &CurColor) {
		ColorHSLA LeftColor = color_cast<ColorHSLA>(CurColor);
		ColorHSLA RightColor = color_cast<ColorHSLA>(CurColor);

		LeftColor.l = DarkestLight;
		RightColor.l = 1.0f;

		const ColorRGBA LeftColorRGBA = color_cast<ColorRGBA>(LeftColor);
		const ColorRGBA RightColorRGBA = color_cast<ColorRGBA>(RightColor);

		Graphics()->SetColor4(LeftColorRGBA, RightColorRGBA, RightColorRGBA, LeftColorRGBA);

		IGraphics::CFreeformItem Freeform(
			pColorRect->x, pColorRect->y,
			pColorRect->x + pColorRect->w, pColorRect->y,
			pColorRect->x, pColorRect->y + pColorRect->h,
			pColorRect->x + pColorRect->w, pColorRect->y + pColorRect->h);
		Graphics()->QuadsDrawFreeform(&Freeform, 1);
	};

	auto &&RenderAlphaRect = [&](CUIRect *pColorRect, const ColorRGBA &CurColorFull) {
		const ColorRGBA LeftColorRGBA = color_cast<ColorRGBA>(color_cast<ColorHSLA>(CurColorFull).WithAlpha(0.0f));
		const ColorRGBA RightColorRGBA = color_cast<ColorRGBA>(color_cast<ColorHSLA>(CurColorFull).WithAlpha(1.0f));

		Graphics()->SetColor4(LeftColorRGBA, RightColorRGBA, RightColorRGBA, LeftColorRGBA);

		IGraphics::CFreeformItem Freeform(
			pColorRect->x, pColorRect->y,
			pColorRect->x + pColorRect->w, pColorRect->y,
			pColorRect->x, pColorRect->y + pColorRect->h,
			pColorRect->x + pColorRect->w, pColorRect->y + pColorRect->h);
		Graphics()->QuadsDrawFreeform(&Freeform, 1);
	};

	for(int i = 0; i < 3 + Alpha; i++)
	{
		CUIRect Button, Label;
		pRect->HSplitTop(SizePerEntry, &Button, pRect);
		pRect->HSplitTop(MarginPerEntry, nullptr, pRect);
		Button.VSplitLeft(10.0f, nullptr, &Button);
		Button.VSplitLeft(100.0f, &Label, &Button);

		Button.Draw(ColorRGBA(0.15f, 0.15f, 0.15f, 1.0f), IGraphics::CORNER_ALL, 1.0f);

		CUIRect Rail;
		Button.Margin(2.0f, &Rail);

		char aBuf[32];
		str_format(aBuf, sizeof(aBuf), "%s: %03d", apLabels[i], round_to_int(Color[i] * 255.0f));
		Ui()->DoLabel(&Label, aBuf, 14.0f, TEXTALIGN_ML);

		ColorRGBA HandleColor;
		Graphics()->TextureClear();
		Graphics()->TrianglesBegin();
		if(i == 0)
		{
			RenderHueRect(&Rail);
			HandleColor = color_cast<ColorRGBA>(ColorHSLA(Color.h, 1.0f, 0.5f, 1.0f));
		}
		else if(i == 1)
		{
			RenderSaturationRect(&Rail, color_cast<ColorRGBA>(ColorHSLA(Color.h, 1.0f, 0.5f, 1.0f)));
			HandleColor = color_cast<ColorRGBA>(ColorHSLA(Color.h, Color.s, 0.5f, 1.0f));
		}
		else if(i == 2)
		{
			RenderLightingRect(&Rail, color_cast<ColorRGBA>(ColorHSLA(Color.h, Color.s, 0.5f, 1.0f)));
			HandleColor = color_cast<ColorRGBA>(ColorHSLA(Color.h, Color.s, Color.l, 1.0f).UnclampLighting(DarkestLight));
		}
		else if(i == 3)
		{
			RenderAlphaRect(&Rail, color_cast<ColorRGBA>(ColorHSLA(Color.h, Color.s, Color.l, 1.0f).UnclampLighting(DarkestLight)));
			HandleColor = color_cast<ColorRGBA>(Color.UnclampLighting(DarkestLight));
		}
		Graphics()->TrianglesEnd();

		Color[i] = Ui()->DoScrollbarH(&((char *)pColor)[i], &Button, Color[i], &HandleColor);
	}

	if(OriginalColor != Color)
	{
		*pColor = Color.Pack(Alpha);
	}
	return PrevPackedColor != *pColor;
}

float CMenus::RenderSettingsAppearance(CUIRect MainView)
{
	const float BaseContentBottom = MainView.y + MainView.h;
	float ContentBottom = BaseContentBottom;
	char aBuf[128];

	CUIRect TabBar, LeftView, RightView, Button;

	MainView.HSplitTop(20.0f, &TabBar, &MainView);
	m_AppearanceTab = minimum(maximum(m_AppearanceTab, 0), NUMBER_OF_APPEARANCE_TABS - 1);
	const float TabWidth = TabBar.w / NUMBER_OF_APPEARANCE_TABS;
	static CButtonContainer s_aPageTabs[NUMBER_OF_APPEARANCE_TABS] = {};
	const char *apTabNames[NUMBER_OF_APPEARANCE_TABS] = {
		Localize("HUD"),
		Localize("Chat"),
		Localize("Name Plate"),
		Localize("Hook Collisions"),
		Localize("Info Messages"),
		Localize("Laser")};

	for(int Tab = APPEARANCE_TAB_HUD; Tab < NUMBER_OF_APPEARANCE_TABS; ++Tab)
	{
		TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
		const int Corners = Tab == APPEARANCE_TAB_HUD ? IGraphics::CORNER_L : (Tab == NUMBER_OF_APPEARANCE_TABS - 1 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
		if(DoButton_MenuTab(&s_aPageTabs[Tab], apTabNames[Tab], m_AppearanceTab == Tab, &Button, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
		{
			m_AppearanceTab = Tab;
		}
	}

	MainView.HSplitTop(10.0f, nullptr, &MainView);

	const float LineSize = 20.0f;
	const float ColorPickerLineSize = 25.0f;
	const float HeadlineFontSize = 20.0f;
	const float HeadlineHeight = 30.0f;
	const float MarginSmall = 5.0f;
	const float MarginBetweenViews = 20.0f;

	const float ColorPickerLabelSize = 13.0f;
	const float ColorPickerLineSpacing = 5.0f;

	if(m_AppearanceTab == APPEARANCE_TAB_HUD)
	{
		MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);

		// ***** HUD ***** //
		Ui()->DoLabel_AutoLineSize(Localize("HUD"), HeadlineFontSize,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

		// Switch of the entire HUD
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowhud, Localize("Show ingame HUD"), &g_Config.m_ClShowhud, &LeftView, LineSize);

		// Switches of the various normal HUD elements
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowhudHealthAmmo, Localize("Show health, shields and ammo"), &g_Config.m_ClShowhudHealthAmmo, &LeftView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowhudScore, Localize("Show score"), &g_Config.m_ClShowhudScore, &LeftView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowLocalTimeAlways, Localize("Show local time always"), &g_Config.m_ClShowLocalTimeAlways, &LeftView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClSpecCursor, Localize("Show spectator cursor"), &g_Config.m_ClSpecCursor, &LeftView, LineSize);

		// Settings of the HUD element for votes
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowVotesAfterVoting, Localize("Show votes window after voting"), &g_Config.m_ClShowVotesAfterVoting, &LeftView, LineSize);

		// ***** Scoreboard ***** //
		LeftView.HSplitTop(MarginBetweenViews, nullptr, &LeftView);
		Ui()->DoLabel_AutoLineSize(Localize("Scoreboard"), HeadlineFontSize,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

		ColorRGBA GreenDefault(0.78f, 1.0f, 0.8f, 1.0f);
		static CButtonContainer s_AuthedColor, s_SameClanColor;
		DoLine_ColorPicker(&s_AuthedColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Authed name color in scoreboard"), &g_Config.m_ClAuthedPlayerColor, GreenDefault, false);
		DoLine_ColorPicker(&s_SameClanColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Same clan color in scoreboard"), &g_Config.m_ClSameClanColor, GreenDefault, false);

		// ***** DDRace HUD ***** //
		Ui()->DoLabel_AutoLineSize(Localize("DDRace HUD"), HeadlineFontSize,
			TEXTALIGN_ML, &RightView, HeadlineHeight);
		RightView.HSplitTop(MarginSmall, nullptr, &RightView);

		// Switches of various DDRace HUD elements
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowIds, Localize("Show client IDs (scoreboard, chat, spectator)"), &g_Config.m_ClShowIds, &RightView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowhudDDRace, Localize("Show DDRace HUD"), &g_Config.m_ClShowhudDDRace, &RightView, LineSize);
		if(g_Config.m_ClShowhudDDRace)
		{
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowhudJumpsIndicator, Localize("Show jumps indicator"), &g_Config.m_ClShowhudJumpsIndicator, &RightView, LineSize);
		}
		else
		{
			RightView.HSplitTop(LineSize, nullptr, &RightView); // Create empty space for hidden option
		}

		// Eye with spectators watching you
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowhudSpectatorCount, Localize("Показывать спектаторов"), &g_Config.m_ClShowhudSpectatorCount, &RightView, LineSize);

		// Switch for dummy actions display
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowhudDummyActions, Localize("Show dummy actions"), &g_Config.m_ClShowhudDummyActions, &RightView, LineSize);

		// Player movement information display settings
		RightView.HSplitTop(MarginSmall, nullptr, &RightView); // BestClient
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowhudPlayerPosition, Localize("Show player position"), &g_Config.m_ClShowhudPlayerPosition, &RightView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowhudPlayerSpeed, Localize("Show player speed"), &g_Config.m_ClShowhudPlayerSpeed, &RightView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowhudPlayerAngle, Localize("Show player target angle"), &g_Config.m_ClShowhudPlayerAngle, &RightView, LineSize);

		// Dummy movement information display settings
		RightView.HSplitTop(MarginSmall, nullptr, &RightView); // BestClient
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowhudDummyPosition, TCLocalize("Show dummy position"), &g_Config.m_TcShowhudDummyPosition, &RightView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowhudDummySpeed, TCLocalize("Show dummy speed"), &g_Config.m_TcShowhudDummySpeed, &RightView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowhudDummyAngle, TCLocalize("Show dummy target angle"), &g_Config.m_TcShowhudDummyAngle, &RightView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcShowhudDummyCoordIndicator, TCLocalize("Show player below indicator"), &g_Config.m_BcShowhudDummyCoordIndicator, &RightView, LineSize);
		if(g_Config.m_BcShowhudDummyCoordIndicator)
		{
			static CButtonContainer s_DummyCoordIndicatorColor;
			static CButtonContainer s_DummyCoordIndicatorSameHeightColor;
			DoLine_ColorPicker(&s_DummyCoordIndicatorColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &RightView, TCLocalize("Indicator false color"), &g_Config.m_BcShowhudDummyCoordIndicatorColor, ColorRGBA(1.0f, 0.5f, 0.0f, 1.0f), false);
			DoLine_ColorPicker(&s_DummyCoordIndicatorSameHeightColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &RightView, TCLocalize("Indicator true color"), &g_Config.m_BcShowhudDummyCoordIndicatorSameHeightColor, ColorRGBA(0.2f, 1.0f, 0.2f, 1.0f), false);
		}

		// Freeze bar settings
		RightView.HSplitTop(MarginSmall, nullptr, &RightView); // BestClient
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowFreezeBars, Localize("Show freeze bars"), &g_Config.m_ClShowFreezeBars, &RightView, LineSize);
		RightView.HSplitTop(LineSize * 2.0f, &Button, &RightView);
		if(g_Config.m_ClShowFreezeBars)
		{
			Ui()->DoScrollbarOption(&g_Config.m_ClFreezeBarsAlphaInsideFreeze, &g_Config.m_ClFreezeBarsAlphaInsideFreeze, &Button, Localize("Opacity of freeze bars inside freeze"), 0, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE, "%");
		}

		ContentBottom = maximum(ContentBottom, maximum(LeftView.y, RightView.y));
	}
	else if(m_AppearanceTab == APPEARANCE_TAB_CHAT)
	{
		CChat &Chat = GameClient()->m_Chat;
		CUIRect TopView, PreviewView;
		const float ColorPickerHeight = ColorPickerLineSize + ColorPickerLineSpacing;
		const float KeyReaderHeight = LineSize + MarginSmall;
		float RequiredLeftHeight = HeadlineHeight + MarginSmall + 9.0f * LineSize + ColorPickerHeight;
		if(g_Config.m_BcChatMediaPreview)
			RequiredLeftHeight += 5.0f * LineSize + KeyReaderHeight;
		const float RequiredRightHeight = HeadlineHeight + MarginSmall + 6.0f * ColorPickerHeight;
		const float TopContentHeight = maximum(RequiredLeftHeight, RequiredRightHeight);
		const float MinPreviewHeight = HeadlineHeight + MarginSmall + 60.0f;
		const float PreferredPreviewHeight = 140.0f;
		const float MaxPreviewHeight = 220.0f;
		const float PreviewHeight = std::clamp(PreferredPreviewHeight, MinPreviewHeight, MaxPreviewHeight);
		const bool ShowPreview = PreviewHeight >= MinPreviewHeight;

		TopView = MainView;
		if(ShowPreview)
		{
			TopView.h = TopContentHeight;
			PreviewView = MainView;
			PreviewView.y = TopView.y + TopView.h + MarginBetweenViews;
			PreviewView.h = PreviewHeight;
		}
		TopView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);

		// ***** Chat ***** //
		Ui()->DoLabel_AutoLineSize(Localize("Chat"), HeadlineFontSize,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

		// General chat settings
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(DoButton_CheckBox(&g_Config.m_ClShowChat, Localize("Show chat"), g_Config.m_ClShowChat, &Button))
		{
			g_Config.m_ClShowChat = g_Config.m_ClShowChat ? 0 : 1;
		}
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(g_Config.m_ClShowChat)
		{
			static int s_ShowChat = 0;
			if(DoButton_CheckBox(&s_ShowChat, Localize("Always show chat"), g_Config.m_ClShowChat == 2, &Button))
				g_Config.m_ClShowChat = g_Config.m_ClShowChat != 2 ? 2 : 1;
		}

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClChatTeamColors, Localize("Show names in chat in team colors"), &g_Config.m_ClChatTeamColors, &LeftView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowChatFriends, Localize("Show only chat messages from friends"), &g_Config.m_ClShowChatFriends, &LeftView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowChatTeamMembersOnly, Localize("Show only chat messages from team members"), &g_Config.m_ClShowChatTeamMembersOnly, &LeftView, LineSize);

		if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClChatOld, Localize("Use old chat style"), &g_Config.m_ClChatOld, &LeftView, LineSize))
			GameClient()->m_Chat.RebuildChat();

		// DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClCensorChat, Localize("Censor profanity"), &g_Config.m_ClCensorChat, &LeftView, LineSize);

		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(Ui()->DoScrollbarOption(&g_Config.m_ClChatFontSize, &g_Config.m_ClChatFontSize, &Button, Localize("Chat font size"), 10, 100))
		{
			Chat.EnsureCoherentWidth();
			Chat.RebuildChat();
		}

		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(Ui()->DoScrollbarOption(&g_Config.m_ClChatWidth, &g_Config.m_ClChatWidth, &Button, Localize("Chat width"), 120, 400))
		{
			Chat.EnsureCoherentFontSize();
			Chat.RebuildChat();
		}

		if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatMediaPreview, Localize("Render media previews from chat links"), &g_Config.m_BcChatMediaPreview, &LeftView, LineSize))
			Chat.RebuildChat();

		if(g_Config.m_BcChatMediaPreview)
		{
			if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatMediaPhotos, Localize("Show photos in chat media"), &g_Config.m_BcChatMediaPhotos, &LeftView, LineSize))
				Chat.RebuildChat();

			if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatMediaGifs, Localize("Show GIFs in chat media"), &g_Config.m_BcChatMediaGifs, &LeftView, LineSize))
				Chat.RebuildChat();

			LeftView.HSplitTop(LineSize, &Button, &LeftView);
			if(Ui()->DoScrollbarOption(&g_Config.m_BcChatMediaPreviewMaxWidth, &g_Config.m_BcChatMediaPreviewMaxWidth, &Button, Localize("Media preview width"), 120, 400))
				Chat.RebuildChat();

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatMediaViewer, Localize("Enable fullscreen media viewer in chat"), &g_Config.m_BcChatMediaViewer, &LeftView, LineSize);

			LeftView.HSplitTop(LineSize, &Button, &LeftView);
			Ui()->DoScrollbarOption(&g_Config.m_BcChatMediaViewerMaxZoom, &g_Config.m_BcChatMediaViewerMaxZoom, &Button, Localize("Viewer max zoom"), 100, 2000, &CUi::ms_LinearScrollbarScale, 0u, "%");

			static CButtonContainer s_HideMediaBindReader;
			static CButtonContainer s_HideMediaBindClear;
			DoLine_KeyReader(LeftView, s_HideMediaBindReader, s_HideMediaBindClear, Localize("Hide media bind"), "toggle_chat_media_hidden");
		}

		static CButtonContainer s_BackgroundColor;
		DoLine_ColorPicker(&s_BackgroundColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Chat background color"), &g_Config.m_ClChatBackgroundColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::ClChatBackgroundColor, true)), false, nullptr, true);

		// ***** Messages ***** //
		Ui()->DoLabel_AutoLineSize(Localize("Messages"), HeadlineFontSize,
			TEXTALIGN_ML, &RightView, HeadlineHeight);
		RightView.HSplitTop(MarginSmall, nullptr, &RightView);

		// Message Colors and extra settings
		static CButtonContainer s_SystemMessageColor;
		DoLine_ColorPicker(&s_SystemMessageColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &RightView, Localize("System message"), &g_Config.m_ClMessageSystemColor, ColorRGBA(1.0f, 1.0f, 0.5f), true, &g_Config.m_ClShowChatSystem);
		static CButtonContainer s_HighlightedMessageColor;
		DoLine_ColorPicker(&s_HighlightedMessageColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &RightView, Localize("Highlighted message"), &g_Config.m_ClMessageHighlightColor, ColorRGBA(1.0f, 0.5f, 0.5f));
		static CButtonContainer s_TeamMessageColor;
		DoLine_ColorPicker(&s_TeamMessageColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &RightView, Localize("Team message"), &g_Config.m_ClMessageTeamColor, ColorRGBA(0.65f, 1.0f, 0.65f));
		static CButtonContainer s_FriendMessageColor;
		DoLine_ColorPicker(&s_FriendMessageColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &RightView, Localize("Friend message"), &g_Config.m_ClMessageFriendColor, ColorRGBA(1.0f, 0.137f, 0.137f), true, &g_Config.m_ClMessageFriend);
		static CButtonContainer s_NormalMessageColor;
		DoLine_ColorPicker(&s_NormalMessageColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &RightView, Localize("Normal message"), &g_Config.m_ClMessageColor, ColorRGBA(1.0f, 1.0f, 1.0f));

		str_format(aBuf, sizeof(aBuf), "%s (echo)", Localize("Client message"));
		static CButtonContainer s_ClientMessageColor;
		// BestClient
		DoLine_ColorPicker(&s_ClientMessageColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &RightView, aBuf, &g_Config.m_ClMessageClientColor, ColorRGBA(0.5f, 0.78f, 1.0f), true, &g_Config.m_TcShowChatClient);

		if(ShowPreview)
		{
			// ***** Chat Preview ***** //
			Ui()->DoLabel_AutoLineSize(Localize("Preview"), HeadlineFontSize,
				TEXTALIGN_ML, &PreviewView, HeadlineHeight);
			PreviewView.HSplitTop(MarginSmall, nullptr, &PreviewView);

			// Use the rest of the view for preview
			PreviewView.Draw(ColorRGBA(1, 1, 1, 0.1f), IGraphics::CORNER_ALL, 5.0f);
			PreviewView.Margin(MarginSmall, &PreviewView);
			Ui()->ClipEnable(&PreviewView);
			struct SScopedClip
			{
				CUi *m_pUi;
				~SScopedClip() { m_pUi->ClipDisable(); }
			} ClipGuard{Ui()};

			ColorRGBA SystemColor = color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageSystemColor));
			ColorRGBA HighlightedColor = color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageHighlightColor));
			ColorRGBA TeamColor = color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageTeamColor));
			ColorRGBA FriendColor = color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageFriendColor));
			ColorRGBA NormalColor = color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageColor));
			ColorRGBA ClientColor = color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageClientColor));
			ColorRGBA DefaultNameColor(0.8f, 0.8f, 0.8f, 1.0f);

		const float RealFontSize = Chat.FontSize() * 2;
		const float RealMsgPaddingX = (!g_Config.m_ClChatOld ? Chat.MessagePaddingX() : 0) * 2;
		const float RealMsgPaddingY = (!g_Config.m_ClChatOld ? Chat.MessagePaddingY() : 0) * 2;
		const float RealMsgPaddingTee = (!g_Config.m_ClChatOld ? Chat.MessageTeeSize() + CChat::MESSAGE_TEE_PADDING_RIGHT : 0) * 2;
		const float RealOffsetY = RealFontSize + RealMsgPaddingY;

			const float X = RealMsgPaddingX / 2.0f + PreviewView.x;
			float Y = PreviewView.y;
			float LineWidth = g_Config.m_ClChatWidth * 2 - (RealMsgPaddingX * 1.5f) - RealMsgPaddingTee;
			LineWidth = minimum(LineWidth, maximum(0.0f, PreviewView.w - (RealMsgPaddingX * 1.5f) - RealMsgPaddingTee));

		str_copy(aBuf, Client()->PlayerName());

		const CAnimState *pIdleState = CAnimState::GetIdle();
		const float RealTeeSize = Chat.MessageTeeSize() * 2;
		const float RealTeeSizeHalved = Chat.MessageTeeSize();
		constexpr float TWSkinUnreliableOffset = -0.25f;
		const float OffsetTeeY = RealTeeSizeHalved;
		const float FullHeightMinusTee = RealOffsetY - RealTeeSize;

		struct SPreviewLine
		{
			int m_ClientId;
			bool m_Team;
			char m_aName[64];
			char m_aText[256];
			bool m_Friend;
			bool m_Player;
			bool m_Client;
			bool m_Highlighted;
			int m_TimesRepeated;

			CTeeRenderInfo m_RenderInfo;
		};

		static std::vector<SPreviewLine> s_vLines;

		enum ELineFlag
		{
			FLAG_TEAM = 1 << 0,
			FLAG_FRIEND = 1 << 1,
			FLAG_HIGHLIGHT = 1 << 2,
			FLAG_CLIENT = 1 << 3
		};
		enum
		{
			PREVIEW_SYS,
			PREVIEW_HIGHLIGHT,
			PREVIEW_TEAM,
			PREVIEW_FRIEND,
			PREVIEW_SPAMMER,
			PREVIEW_CLIENT
		};
		auto &&SetPreviewLine = [](int Index, int ClientId, const char *pName, const char *pText, int Flag, int Repeats) {
			SPreviewLine *pLine;
			if((int)s_vLines.size() <= Index)
			{
				s_vLines.emplace_back();
				pLine = &s_vLines.back();
			}
			else
			{
				pLine = &s_vLines[Index];
			}
			pLine->m_ClientId = ClientId;
			pLine->m_Team = Flag & FLAG_TEAM;
			pLine->m_Friend = Flag & FLAG_FRIEND;
			pLine->m_Player = ClientId >= 0;
			pLine->m_Highlighted = Flag & FLAG_HIGHLIGHT;
			pLine->m_Client = Flag & FLAG_CLIENT;
			pLine->m_TimesRepeated = Repeats;
			str_copy(pLine->m_aName, pName);
			str_copy(pLine->m_aText, pText);
		};
		auto &&SetLineSkin = [RealTeeSize](int Index, const CSkin *pSkin) {
			if(Index >= (int)s_vLines.size())
				return;
			s_vLines[Index].m_RenderInfo.m_Size = RealTeeSize;
			s_vLines[Index].m_RenderInfo.Apply(pSkin);
		};

		auto &&RenderPreview = [&](int LineIndex, int x, int y, bool Render = true) {
			if(LineIndex >= (int)s_vLines.size() || LineWidth <= 0.0f)
				return vec2(0, 0);
			CTextCursor LocalCursor;
			LocalCursor.SetPosition(vec2(x, y));
			LocalCursor.m_FontSize = RealFontSize;
			LocalCursor.m_Flags = Render ? TEXTFLAG_RENDER : 0;
			LocalCursor.m_LineWidth = LineWidth;
			const auto &Line = s_vLines[LineIndex];

			char aClientId[16] = "";
			if(g_Config.m_ClShowIds && Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
			{
				GameClient()->FormaBestClientId(Line.m_ClientId, aClientId, EClientIdFormat::INDENT_FORCE);
			}

			char aCount[12];
			if(Line.m_ClientId < 0)
				str_format(aCount, sizeof(aCount), "[%d] ", Line.m_TimesRepeated + 1);
			else
				str_format(aCount, sizeof(aCount), " [%d]", Line.m_TimesRepeated + 1);

			if(Line.m_Player)
			{
				LocalCursor.m_X += RealMsgPaddingTee;

				if(Line.m_Friend && g_Config.m_ClMessageFriend)
				{
					if(Render)
						TextRender()->TextColor(FriendColor);
					TextRender()->TextEx(&LocalCursor, "♥ ", -1);
				}
			}

			ColorRGBA NameColor;
			if(Line.m_Team)
				NameColor = CalculateNameColor(color_cast<ColorHSLA>(TeamColor));
			else if(Line.m_Player)
				NameColor = DefaultNameColor;
			else if(Line.m_Client)
				NameColor = ClientColor;
			else
				NameColor = SystemColor;

			if(Render)
				TextRender()->TextColor(NameColor);

			TextRender()->TextEx(&LocalCursor, aClientId);
			TextRender()->TextEx(&LocalCursor, Line.m_aName);

			if(Line.m_TimesRepeated > 0)
			{
				if(Render)
					TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.3f);
				TextRender()->TextEx(&LocalCursor, aCount, -1);
			}

			if(Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
			{
				if(Render)
					TextRender()->TextColor(NameColor);
				TextRender()->TextEx(&LocalCursor, ": ", -1);
			}

			CTextCursor AppendCursor = LocalCursor;
			AppendCursor.m_LongestLineWidth = 0.0f;
			if(!g_Config.m_ClChatOld)
			{
				AppendCursor.m_StartX = LocalCursor.m_X;
				AppendCursor.m_LineWidth -= LocalCursor.m_LongestLineWidth;
			}

			if(Render)
			{
				if(Line.m_Highlighted)
					TextRender()->TextColor(HighlightedColor);
				else if(Line.m_Team)
					TextRender()->TextColor(TeamColor);
				else if(Line.m_Player)
					TextRender()->TextColor(NormalColor);
			}

			TextRender()->TextEx(&AppendCursor, Line.m_aText, -1);
			if(Render)
				TextRender()->TextColor(TextRender()->DefaultTextColor());

			return vec2{LocalCursor.m_LongestLineWidth + AppendCursor.m_LongestLineWidth, AppendCursor.Height() + RealMsgPaddingY};
		};

		// Set preview lines
		{
			char aLineBuilder[128];

			str_format(aLineBuilder, sizeof(aLineBuilder), Localize("'%s' entered and joined the game"), aBuf);
			SetPreviewLine(PREVIEW_SYS, -1, "*** ", aLineBuilder, 0, 0);

			str_format(aLineBuilder, sizeof(aLineBuilder), Localize("Hey, how are you %s?"), aBuf);
			SetPreviewLine(PREVIEW_HIGHLIGHT, 7, Localize("Random Tee"), aLineBuilder, FLAG_HIGHLIGHT, 0);

			SetPreviewLine(PREVIEW_TEAM, 11, Localize("Your Teammate"), Localize("Let's speedrun this!"), FLAG_TEAM, 0);
			SetPreviewLine(PREVIEW_FRIEND, 8, Localize("Friend"), Localize("Hello there"), FLAG_FRIEND, 0);
			SetPreviewLine(PREVIEW_SPAMMER, 9, Localize("Spammer"), Localize("Hey fools, I'm spamming here!"), 0, 5);
			SetPreviewLine(PREVIEW_CLIENT, -1, "* ", Localize("Echo command executed"), FLAG_CLIENT, 0);
		}
		SetLineSkin(1, GameClient()->m_Skins.Find("pinky"));
		SetLineSkin(2, GameClient()->m_Skins.Find("default"));
		SetLineSkin(3, GameClient()->m_Skins.Find("cammostripes"));
		SetLineSkin(4, GameClient()->m_Skins.Find("beast"));

		// Backgrounds first
		if(!g_Config.m_ClChatOld)
		{
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClChatBackgroundColor, true)));

			float TempY = Y;
			const float RealBackgroundRounding = Chat.MessageRounding() * 2.0f;

			auto &&RenderMessageBackground = [&](int LineIndex) {
				auto Size = RenderPreview(LineIndex, 0, 0, false);
				Graphics()->DrawRectExt(X - RealMsgPaddingX / 2.0f, TempY - RealMsgPaddingY / 2.0f, Size.x + RealMsgPaddingX * 1.5f, Size.y, RealBackgroundRounding, IGraphics::CORNER_ALL);
				return Size.y;
			};

			if(g_Config.m_ClShowChatSystem)
			{
				TempY += RenderMessageBackground(PREVIEW_SYS);
			}

			if(!g_Config.m_ClShowChatFriends)
			{
				if(!g_Config.m_ClShowChatTeamMembersOnly)
					TempY += RenderMessageBackground(PREVIEW_HIGHLIGHT);
				TempY += RenderMessageBackground(PREVIEW_TEAM);
			}

			if(!g_Config.m_ClShowChatTeamMembersOnly)
				TempY += RenderMessageBackground(PREVIEW_FRIEND);

			if(!g_Config.m_ClShowChatFriends && !g_Config.m_ClShowChatTeamMembersOnly)
			{
				TempY += RenderMessageBackground(PREVIEW_SPAMMER);
			}

			if(g_Config.m_TcShowChatClient)
			{
				TempY += RenderMessageBackground(PREVIEW_CLIENT);
			}

			Graphics()->QuadsEnd();
		}

		// System
		if(g_Config.m_ClShowChatSystem)
		{
			Y += RenderPreview(PREVIEW_SYS, X, Y).y;
		}

		if(!g_Config.m_ClShowChatFriends)
		{
			// Highlighted
			if(!g_Config.m_ClChatOld && !g_Config.m_ClShowChatTeamMembersOnly)
				RenderTools()->RenderTee(pIdleState, &s_vLines[PREVIEW_HIGHLIGHT].m_RenderInfo, EMOTE_NORMAL, vec2(1, 0.1f), vec2(X + RealTeeSizeHalved, Y + OffsetTeeY + FullHeightMinusTee / 2.0f + TWSkinUnreliableOffset));
			if(!g_Config.m_ClShowChatTeamMembersOnly)
				Y += RenderPreview(PREVIEW_HIGHLIGHT, X, Y).y;

			// Team
			if(!g_Config.m_ClChatOld)
				RenderTools()->RenderTee(pIdleState, &s_vLines[PREVIEW_TEAM].m_RenderInfo, EMOTE_NORMAL, vec2(1, 0.1f), vec2(X + RealTeeSizeHalved, Y + OffsetTeeY + FullHeightMinusTee / 2.0f + TWSkinUnreliableOffset));
			Y += RenderPreview(PREVIEW_TEAM, X, Y).y;
		}

		// Friend
		if(!g_Config.m_ClChatOld && !g_Config.m_ClShowChatTeamMembersOnly)
			RenderTools()->RenderTee(pIdleState, &s_vLines[PREVIEW_FRIEND].m_RenderInfo, EMOTE_NORMAL, vec2(1, 0.1f), vec2(X + RealTeeSizeHalved, Y + OffsetTeeY + FullHeightMinusTee / 2.0f + TWSkinUnreliableOffset));
		if(!g_Config.m_ClShowChatTeamMembersOnly)
			Y += RenderPreview(PREVIEW_FRIEND, X, Y).y;

		// Normal
		if(!g_Config.m_ClShowChatFriends && !g_Config.m_ClShowChatTeamMembersOnly)
		{
			if(!g_Config.m_ClChatOld)
				RenderTools()->RenderTee(pIdleState, &s_vLines[PREVIEW_SPAMMER].m_RenderInfo, EMOTE_NORMAL, vec2(1, 0.1f), vec2(X + RealTeeSizeHalved, Y + OffsetTeeY + FullHeightMinusTee / 2.0f + TWSkinUnreliableOffset));
			Y += RenderPreview(PREVIEW_SPAMMER, X, Y).y;
		}
		// Client
		if(g_Config.m_TcShowChatClient)
		{
			RenderPreview(PREVIEW_CLIENT, X, Y);
		}

			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}

		float LayoutBottom = maximum(LeftView.y, RightView.y);
		if(ShowPreview)
			LayoutBottom = maximum(LayoutBottom, PreviewView.y + PreviewView.h);
		ContentBottom = maximum(ContentBottom, LayoutBottom);
	}
	else if(m_AppearanceTab == APPEARANCE_TAB_NAME_PLATE)
	{
		MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);

		// ***** Name Plate ***** //
		Ui()->DoLabel_AutoLineSize(Localize("Name Plate"), HeadlineFontSize,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

		// General name plate settings
		{
			int Pressed = (g_Config.m_ClNamePlates ? 2 : 0) + (g_Config.m_ClNamePlatesOwn ? 1 : 0);
			if(DoLine_RadioMenu(LeftView, Localize("Show name plates"),
				   m_vButtonContainersNamePlateShow,
				   {Localize("None", "Show name plates"), Localize("Own", "Show name plates"), Localize("Others", "Show name plates"), Localize("All", "Show name plates")},
				   {0, 1, 2, 3},
				   Pressed))
			{
				g_Config.m_ClNamePlates = Pressed & 2 ? 1 : 0;
				g_Config.m_ClNamePlatesOwn = Pressed & 1 ? 1 : 0;
			}
		}
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_ClNamePlatesSize, &g_Config.m_ClNamePlatesSize, &Button, Localize("Name plates size"), -50, 100);
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_ClNamePlatesOffset, &g_Config.m_ClNamePlatesOffset, &Button, Localize("Name plates offset"), 10, 50);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClNamePlatesSmartFade, Localize("Smart fade nameplates"), &g_Config.m_ClNamePlatesSmartFade, &LeftView, LineSize);
		if(g_Config.m_ClNamePlatesSmartFade)
		{
			LeftView.HSplitTop(LineSize, &Button, &LeftView);
			Ui()->DoScrollbarOption(&g_Config.m_ClNamePlatesSmartFadeStrength, &g_Config.m_ClNamePlatesSmartFadeStrength, &Button, Localize("Smart fade strength"), 0, 100);
			LeftView.HSplitTop(LineSize, &Button, &LeftView);
			Ui()->DoScrollbarOption(&g_Config.m_ClNamePlatesSmartFadeSize, &g_Config.m_ClNamePlatesSmartFadeSize, &Button, Localize("Smart fade start size"), 0, 30);
		}

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClNamePlatesClan, Localize("Show clan above name plates"), &g_Config.m_ClNamePlatesClan, &LeftView, LineSize);
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(g_Config.m_ClNamePlatesClan)
			Ui()->DoScrollbarOption(&g_Config.m_ClNamePlatesClanSize, &g_Config.m_ClNamePlatesClanSize, &Button, Localize("Clan plates size"), -50, 100);

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClNamePlatesTeamcolors, Localize("Use team colors for name plates"), &g_Config.m_ClNamePlatesTeamcolors, &LeftView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClNamePlatesFriendMark, Localize("Show friend icon in name plates"), &g_Config.m_ClNamePlatesFriendMark, &LeftView, LineSize);

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClNamePlatesIds, Localize("Show client IDs in name plates"), &g_Config.m_ClNamePlatesIds, &LeftView, LineSize);
		if(g_Config.m_ClNamePlatesIds > 0)
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClNamePlatesIdsSeparateLine, Localize("Show client IDs on a separate line"), &g_Config.m_ClNamePlatesIdsSeparateLine, &LeftView, LineSize);
		else
			LeftView.HSplitTop(LineSize, nullptr, &LeftView);
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(g_Config.m_ClNamePlatesIds > 0 && g_Config.m_ClNamePlatesIdsSeparateLine > 0)
			Ui()->DoScrollbarOption(&g_Config.m_ClNamePlatesIdsSize, &g_Config.m_ClNamePlatesIdsSize, &Button, Localize("Client IDs size"), -50, 100);

		// ***** Hook Strength ***** //
		LeftView.HSplitTop(MarginBetweenViews, nullptr, &LeftView);
		Ui()->DoLabel_AutoLineSize(Localize("Hook Strength"), HeadlineFontSize,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(DoButton_CheckBox(&g_Config.m_ClNamePlatesStrong, Localize("Show hook strength icon indicator"), g_Config.m_ClNamePlatesStrong, &Button))
		{
			g_Config.m_ClNamePlatesStrong = g_Config.m_ClNamePlatesStrong ? 0 : 1;
		}
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(g_Config.m_ClNamePlatesStrong)
		{
			static int s_NamePlatesStrong = 0;
			if(DoButton_CheckBox(&s_NamePlatesStrong, Localize("Show hook strength number indicator"), g_Config.m_ClNamePlatesStrong == 2, &Button))
				g_Config.m_ClNamePlatesStrong = g_Config.m_ClNamePlatesStrong != 2 ? 2 : 1;
		}

		LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
		if(g_Config.m_ClNamePlatesStrong)
		{
			Ui()->DoScrollbarOption(&g_Config.m_ClNamePlatesStrongSize, &g_Config.m_ClNamePlatesStrongSize, &Button, Localize("Size of hook strength icon and number indicator"), -50, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE);
		}

		// ***** Key Presses ***** //
		LeftView.HSplitTop(MarginBetweenViews, nullptr, &LeftView);
		Ui()->DoLabel_AutoLineSize(Localize("Key Presses"), HeadlineFontSize,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

		DoLine_RadioMenu(LeftView, Localize("Show players' key presses"),
			m_vButtonContainersNamePlateKeyPresses,
			{Localize("None", "Show players' key presses"), Localize("Own", "Show players' key presses"), Localize("Others", "Show players' key presses"), Localize("All", "Show players' key presses")},
			{0, 3, 1, 2},
			g_Config.m_ClShowDirection);

		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(g_Config.m_ClShowDirection > 0)
			Ui()->DoScrollbarOption(&g_Config.m_ClDirectionSize, &g_Config.m_ClDirectionSize, &Button, Localize("Size of key press icons"), -50, 100);

		// ***** Name Plate Preview ***** //
		Ui()->DoLabel_AutoLineSize(Localize("Preview"), HeadlineFontSize,
			TEXTALIGN_ML, &RightView, HeadlineHeight);
		RightView.HSplitTop(2.0f * MarginSmall, nullptr, &RightView);

		// ***** Name Plate Dummy Preview ***** //
		RightView.HSplitBottom(LineSize, &RightView, &Button);
		if(DoButton_CheckBox(&m_DummyNamePlatePreview, g_Config.m_ClDummy ? Localize("Preview player's name plate") : Localize("Preview dummy's name plate"), m_DummyNamePlatePreview, &Button))
			m_DummyNamePlatePreview = !m_DummyNamePlatePreview;

		static CButtonContainer s_ResetNamePlateLayoutButton;
		RightView.HSplitBottom(LineSize, &RightView, &Button);
		if(DoButton_Menu(&s_ResetNamePlateLayoutButton, Localize("Reset layout"), 0, &Button))
			GameClient()->m_NamePlates.ResetNamePlateElementOffsets();

		RightView.HSplitBottom(LineSize, &RightView, &Button);
		Ui()->DoLabel(&Button, Localize("Drag items in the preview. Right click resets one item."), 10.0f, TEXTALIGN_ML);

		int Dummy = g_Config.m_ClDummy != (m_DummyNamePlatePreview ? 1 : 0);
		GameClient()->m_NamePlates.RenderNamePlatePreview(RightView, Dummy);
		ContentBottom = maximum(ContentBottom, maximum(LeftView.y, RightView.y));
	}
	else if(m_AppearanceTab == APPEARANCE_TAB_HOOK_COLLISION)
	{
		MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);

		// ***** Hookline ***** //
		Ui()->DoLabel_AutoLineSize(Localize("Hook collision line"), HeadlineFontSize,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

		// General hookline settings
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(DoButton_CheckBox(&g_Config.m_ClShowHookCollOwn, Localize("Show own player's hook collision line"), g_Config.m_ClShowHookCollOwn, &Button))
		{
			g_Config.m_ClShowHookCollOwn = g_Config.m_ClShowHookCollOwn ? 0 : 1;
		}
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(g_Config.m_ClShowHookCollOwn)
		{
			static int s_ShowHookCollOwn = 0;
			if(DoButton_CheckBox(&s_ShowHookCollOwn, Localize("Always show own player's hook collision line"), g_Config.m_ClShowHookCollOwn == 2, &Button))
				g_Config.m_ClShowHookCollOwn = g_Config.m_ClShowHookCollOwn != 2 ? 2 : 1;
		}

		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(DoButton_CheckBox(&g_Config.m_ClShowHookCollOther, Localize("Show other players' hook collision lines"), g_Config.m_ClShowHookCollOther, &Button))
		{
			g_Config.m_ClShowHookCollOther = g_Config.m_ClShowHookCollOther >= 1 ? 0 : 1;
		}
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(g_Config.m_ClShowHookCollOther)
		{
			static int s_ShowHookCollOther = 0;
			if(DoButton_CheckBox(&s_ShowHookCollOther, Localize("Always show other players' hook collision lines"), g_Config.m_ClShowHookCollOther == 2, &Button))
				g_Config.m_ClShowHookCollOther = g_Config.m_ClShowHookCollOther != 2 ? 2 : 1;
		}

		LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_ClHookCollSize, &g_Config.m_ClHookCollSize, &Button, Localize("Width of your own hook collision line"), 0, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE);

		LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_ClHookCollSizeOther, &g_Config.m_ClHookCollSizeOther, &Button, Localize("Width of others' hook collision line"), 0, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE);

		LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_ClHookCollAlpha, &g_Config.m_ClHookCollAlpha, &Button, Localize("Hook collision line opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE, "%");

		static CButtonContainer s_HookCollNoCollResetId, s_HookCollHookableCollResetId, s_HookCollTeeCollResetId;
		static int s_HookCollToolTip;

		Ui()->DoLabel_AutoLineSize(Localize("Colors of the hook collision line, in case of a possible collision with:"), 13.0f,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);

		Ui()->DoButtonLogic(&s_HookCollToolTip, 0, &LeftView, BUTTONFLAG_NONE); // Just for the tooltip, result ignored
		GameClient()->m_Tooltips.DoToolTip(&s_HookCollToolTip, &LeftView, Localize("Your movements are not taken into account when calculating the line colors"));
		DoLine_ColorPicker(&s_HookCollNoCollResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Nothing hookable"), &g_Config.m_ClHookCollColorNoColl, ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f), false);
		DoLine_ColorPicker(&s_HookCollHookableCollResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Something hookable"), &g_Config.m_ClHookCollColorHookableColl, ColorRGBA(130.0f / 255.0f, 232.0f / 255.0f, 160.0f / 255.0f, 1.0f), false);
		DoLine_ColorPicker(&s_HookCollTeeCollResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("A Tee"), &g_Config.m_ClHookCollColorTeeColl, ColorRGBA(1.0f, 1.0f, 0.0f, 1.0f), false);

		// ***** Hook collisions preview ***** //
		Ui()->DoLabel_AutoLineSize(Localize("Preview"), HeadlineFontSize,
			TEXTALIGN_ML, &RightView, HeadlineHeight);
		RightView.HSplitTop(2 * MarginSmall, nullptr, &RightView);

		auto DoHookCollision = [this](const vec2 &Pos, const float &Length, const int &Size, const ColorRGBA &Color, const bool &Invert) {
			ColorRGBA ColorModified = Color;
			if(Invert)
				ColorModified = color_invert(ColorModified);
			ColorModified = ColorModified.WithAlpha((float)g_Config.m_ClHookCollAlpha / 100);
			Graphics()->TextureClear();
			if(Size > 0)
			{
				Graphics()->QuadsBegin();
				Graphics()->SetColor(ColorModified);
				float LineWidth = 0.5f + (float)(Size - 1) * 0.25f;
				IGraphics::CQuadItem QuadItem(Pos.x, Pos.y - LineWidth, Length, LineWidth * 2.f);
				Graphics()->QuadsDrawTL(&QuadItem, 1);
				Graphics()->QuadsEnd();
			}
			else
			{
				Graphics()->LinesBegin();
				Graphics()->SetColor(ColorModified);
				IGraphics::CLineItem LineItem(Pos.x, Pos.y, Pos.x + Length, Pos.y);
				Graphics()->LinesDraw(&LineItem, 1);
				Graphics()->LinesEnd();
			}
		};

		CTeeRenderInfo OwnSkinInfo;
		OwnSkinInfo.Apply(GameClient()->m_Skins.Find(g_Config.m_ClPlayerSkin));
		OwnSkinInfo.ApplyColors(g_Config.m_ClPlayerUseCustomColor, g_Config.m_ClPlayerColorBody, g_Config.m_ClPlayerColorFeet);
		OwnSkinInfo.m_Size = gs_MenuPreviewTeeLarge;

		CTeeRenderInfo DummySkinInfo;
		DummySkinInfo.Apply(GameClient()->m_Skins.Find(g_Config.m_ClDummySkin));
		DummySkinInfo.ApplyColors(g_Config.m_ClDummyUseCustomColor, g_Config.m_ClDummyColorBody, g_Config.m_ClDummyColorFeet);
		DummySkinInfo.m_Size = gs_MenuPreviewTeeLarge;

		vec2 TeeRenderPos, DummyRenderPos;

		const float LineLength = 150.f;
		const float LeftMargin = 30.f;

		const int TileScale = 32.0f;

		const float PreviewTeeRowHeight = gs_MenuPreviewTeeLarge + 8.0f;

		// Toggled via checkbox later, inverts some previews
		static bool s_HookCollPressed = false;

		CUIRect PreviewColl;

		// ***** Unhookable Tile Preview *****
		CUIRect PreviewNoColl;
		RightView.HSplitTop(PreviewTeeRowHeight, &PreviewNoColl, &RightView);
		RightView.HSplitTop(4 * MarginSmall, nullptr, &RightView);
		TeeRenderPos = vec2(PreviewNoColl.x + LeftMargin, PreviewNoColl.y + PreviewNoColl.h / 2.0f);
		DoHookCollision(TeeRenderPos, PreviewNoColl.w - LineLength, g_Config.m_ClHookCollSize, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollColorNoColl)), s_HookCollPressed);
		RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, 0, vec2(1.0f, 0.0f), TeeRenderPos);

		CUIRect NoHookTileRect;
		PreviewNoColl.VSplitRight(LineLength, &PreviewNoColl, &NoHookTileRect);
		NoHookTileRect.VSplitLeft(50.0f, &NoHookTileRect, nullptr);
		NoHookTileRect.Margin(10.0f, &NoHookTileRect);

		// Render unhookable tile
		Graphics()->TextureClear();
		Graphics()->TextureSet(GameClient()->m_MapImages.GetEntities(MAP_IMAGE_ENTITY_LAYER_TYPE_ALL_EXCEPT_SWITCH));
		Graphics()->BlendNormal();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
		RenderMap()->RenderTile(NoHookTileRect.x, NoHookTileRect.y, TILE_NOHOOK, TileScale, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));

		// ***** Hookable Tile Preview *****
		RightView.HSplitTop(PreviewTeeRowHeight, &PreviewColl, &RightView);
		RightView.HSplitTop(4 * MarginSmall, nullptr, &RightView);
		TeeRenderPos = vec2(PreviewColl.x + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
		DoHookCollision(TeeRenderPos, PreviewColl.w - LineLength, g_Config.m_ClHookCollSize, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollColorHookableColl)), s_HookCollPressed);
		RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, 0, vec2(1.0f, 0.0f), TeeRenderPos);

		CUIRect HookTileRect;
		PreviewColl.VSplitRight(LineLength, &PreviewColl, &HookTileRect);
		HookTileRect.VSplitLeft(50.0f, &HookTileRect, nullptr);
		HookTileRect.Margin(10.0f, &HookTileRect);

		// Render hookable tile
		Graphics()->TextureClear();
		Graphics()->TextureSet(GameClient()->m_MapImages.GetEntities(MAP_IMAGE_ENTITY_LAYER_TYPE_ALL_EXCEPT_SWITCH));
		Graphics()->BlendNormal();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
		RenderMap()->RenderTile(HookTileRect.x, HookTileRect.y, TILE_SOLID, TileScale, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));

		// ***** Hook Dummy Preview *****
		RightView.HSplitTop(PreviewTeeRowHeight, &PreviewColl, &RightView);
		RightView.HSplitTop(4 * MarginSmall, nullptr, &RightView);
		TeeRenderPos = vec2(PreviewColl.x + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
		DummyRenderPos = vec2(PreviewColl.x + PreviewColl.w - LineLength - 5.f + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
		DoHookCollision(TeeRenderPos, PreviewColl.w - LineLength - 15.f, g_Config.m_ClHookCollSize, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollColorTeeColl)), s_HookCollPressed);
		RenderTools()->RenderTee(CAnimState::GetIdle(), &DummySkinInfo, 0, vec2(1.0f, 0.0f), DummyRenderPos);
		RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, 0, vec2(1.0f, 0.0f), TeeRenderPos);

		// ***** Hook Dummy Reverse Preview *****
		RightView.HSplitTop(PreviewTeeRowHeight, &PreviewColl, &RightView);
		RightView.HSplitTop(4 * MarginSmall, nullptr, &RightView);
		TeeRenderPos = vec2(PreviewColl.x + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
		DummyRenderPos = vec2(PreviewColl.x + PreviewColl.w - LineLength - 5.f + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
		DoHookCollision(TeeRenderPos, PreviewColl.w - LineLength - 15.f, g_Config.m_ClHookCollSizeOther, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollColorTeeColl)), false);
		RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, 0, vec2(1.0f, 0.0f), DummyRenderPos);
		RenderTools()->RenderTee(CAnimState::GetIdle(), &DummySkinInfo, 0, vec2(1.0f, 0.0f), TeeRenderPos);

		// ***** Preview +hookcoll pressed toggle *****
		RightView.HSplitTop(LineSize, &Button, &RightView);
		if(DoButton_CheckBox(&s_HookCollPressed, Localize("Preview 'Hook collisions' being pressed"), s_HookCollPressed, &Button))
			s_HookCollPressed = !s_HookCollPressed;
		ContentBottom = maximum(ContentBottom, maximum(LeftView.y, RightView.y));
	}
	else if(m_AppearanceTab == APPEARANCE_TAB_INFO_MESSAGES)
	{
		MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);

		// ***** Info Messages ***** //
		Ui()->DoLabel_AutoLineSize(Localize("Info Messages"), HeadlineFontSize,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

		// General info messages settings
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(DoButton_CheckBox(&g_Config.m_ClShowKillMessages, Localize("Show kill messages"), g_Config.m_ClShowKillMessages, &Button))
		{
			g_Config.m_ClShowKillMessages ^= 1;
		}

		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(DoButton_CheckBox(&g_Config.m_ClShowFinishMessages, Localize("Show finish messages"), g_Config.m_ClShowFinishMessages, &Button))
		{
			g_Config.m_ClShowFinishMessages ^= 1;
		}

		static CButtonContainer s_KillMessageNormalColorId, s_KillMessageHighlightColorId;
		DoLine_ColorPicker(&s_KillMessageNormalColorId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Normal Color"), &g_Config.m_ClKillMessageNormalColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
		DoLine_ColorPicker(&s_KillMessageHighlightColorId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Highlight Color"), &g_Config.m_ClKillMessageHighlightColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
		ContentBottom = maximum(ContentBottom, maximum(LeftView.y, RightView.y));
	}
	else if(m_AppearanceTab == APPEARANCE_TAB_LASER)
	{
		MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);

		// ***** Weapons ***** //
		Ui()->DoLabel_AutoLineSize(Localize("Weapons"), HeadlineFontSize,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

		// General weapon laser settings
		static CButtonContainer s_LaserRifleOutResetId, s_LaserRifleInResetId, s_LaserShotgunOutResetId, s_LaserShotgunInResetId;

		ColorHSLA LaserRifleOutlineColor = DoLine_ColorPicker(&s_LaserRifleOutResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Rifle Laser Outline Color"), &g_Config.m_ClLaserRifleOutlineColor, ColorRGBA(0.074402f, 0.074402f, 0.247166f, 1.0f), false);
		ColorHSLA LaserRifleInnerColor = DoLine_ColorPicker(&s_LaserRifleInResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Rifle Laser Inner Color"), &g_Config.m_ClLaserRifleInnerColor, ColorRGBA(0.498039f, 0.498039f, 1.0f, 1.0f), false);
		ColorHSLA LaserShotgunOutlineColor = DoLine_ColorPicker(&s_LaserShotgunOutResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Shotgun Laser Outline Color"), &g_Config.m_ClLaserShotgunOutlineColor, ColorRGBA(0.125490f, 0.098039f, 0.043137f, 1.0f), false);
		ColorHSLA LaserShotgunInnerColor = DoLine_ColorPicker(&s_LaserShotgunInResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Shotgun Laser Inner Color"), &g_Config.m_ClLaserShotgunInnerColor, ColorRGBA(0.570588f, 0.417647f, 0.252941f, 1.0f), false);

		// ***** Entities ***** //
		LeftView.HSplitTop(10.0f, nullptr, &LeftView);
		Ui()->DoLabel_AutoLineSize(Localize("Entities"), HeadlineFontSize,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

		// General entity laser settings
		static CButtonContainer s_LaserDoorOutResetId, s_LaserDoorInResetId, s_LaserFreezeOutResetId, s_LaserFreezeInResetId, s_LaserDraggerOutResetId, s_LaserDraggerInResetId;

		ColorHSLA LaserDoorOutlineColor = DoLine_ColorPicker(&s_LaserDoorOutResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Door Laser Outline Color"), &g_Config.m_ClLaserDoorOutlineColor, ColorRGBA(0.0f, 0.131372f, 0.096078f, 1.0f), false);
		ColorHSLA LaserDoorInnerColor = DoLine_ColorPicker(&s_LaserDoorInResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Door Laser Inner Color"), &g_Config.m_ClLaserDoorInnerColor, ColorRGBA(0.262745f, 0.760784f, 0.639215f, 1.0f), false);
		ColorHSLA LaserFreezeOutlineColor = DoLine_ColorPicker(&s_LaserFreezeOutResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Freeze Laser Outline Color"), &g_Config.m_ClLaserFreezeOutlineColor, ColorRGBA(0.131372f, 0.123529f, 0.182352f, 1.0f), false);
		ColorHSLA LaserFreezeInnerColor = DoLine_ColorPicker(&s_LaserFreezeInResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Freeze Laser Inner Color"), &g_Config.m_ClLaserFreezeInnerColor, ColorRGBA(0.482352f, 0.443137f, 0.564705f, 1.0f), false);
		ColorHSLA LaserDraggerOutlineColor = DoLine_ColorPicker(&s_LaserDraggerOutResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Dragger Outline Color"), &g_Config.m_ClLaserDraggerOutlineColor, ColorRGBA(0.1640625f, 0.015625f, 0.015625f, 1.0f), false);
		ColorHSLA LaserDraggerInnerColor = DoLine_ColorPicker(&s_LaserDraggerInResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Dragger Inner Color"), &g_Config.m_ClLaserDraggerInnerColor, ColorRGBA(.8666666f, .3725490f, .3725490f, 1.0f), false);

		static CButtonContainer s_AllToRifleResetId, s_AllToDefaultResetId;

		LeftView.HSplitTop(4 * MarginSmall, nullptr, &LeftView);
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(DoButton_Menu(&s_AllToRifleResetId, Localize("Set all to Rifle"), 0, &Button))
		{
			g_Config.m_ClLaserShotgunOutlineColor = g_Config.m_ClLaserRifleOutlineColor;
			g_Config.m_ClLaserShotgunInnerColor = g_Config.m_ClLaserRifleInnerColor;
			g_Config.m_ClLaserDoorOutlineColor = g_Config.m_ClLaserRifleOutlineColor;
			g_Config.m_ClLaserDoorInnerColor = g_Config.m_ClLaserRifleInnerColor;
			g_Config.m_ClLaserFreezeOutlineColor = g_Config.m_ClLaserRifleOutlineColor;
			g_Config.m_ClLaserFreezeInnerColor = g_Config.m_ClLaserRifleInnerColor;
			g_Config.m_ClLaserDraggerOutlineColor = g_Config.m_ClLaserRifleOutlineColor;
			g_Config.m_ClLaserDraggerInnerColor = g_Config.m_ClLaserRifleInnerColor;
		}

		// values taken from the CL commands
		LeftView.HSplitTop(2 * MarginSmall, nullptr, &LeftView);
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(DoButton_Menu(&s_AllToDefaultResetId, Localize("Reset to defaults"), 0, &Button))
		{
			g_Config.m_ClLaserRifleOutlineColor = 11176233;
			g_Config.m_ClLaserRifleInnerColor = 11206591;
			g_Config.m_ClLaserShotgunOutlineColor = 1866773;
			g_Config.m_ClLaserShotgunInnerColor = 1467241;
			g_Config.m_ClLaserDoorOutlineColor = 7667473;
			g_Config.m_ClLaserDoorInnerColor = 7701379;
			g_Config.m_ClLaserFreezeOutlineColor = 11613223;
			g_Config.m_ClLaserFreezeInnerColor = 12001153;
			g_Config.m_ClLaserDraggerOutlineColor = 57618;
			g_Config.m_ClLaserDraggerInnerColor = 42398;
		}

		// ***** Laser Preview ***** //
		Ui()->DoLabel_AutoLineSize(Localize("Preview"), HeadlineFontSize,
			TEXTALIGN_ML, &RightView, HeadlineHeight);
		RightView.HSplitTop(MarginSmall, nullptr, &RightView);

		const float LaserPreviewHeight = 60.0f;
		CUIRect LaserPreview;
		RightView.HSplitTop(LaserPreviewHeight, &LaserPreview, &RightView);
		RightView.HSplitTop(2 * MarginSmall, nullptr, &RightView);
		DoLaserPreview(&LaserPreview, LaserRifleOutlineColor, LaserRifleInnerColor, LASERTYPE_RIFLE);

		RightView.HSplitTop(LaserPreviewHeight, &LaserPreview, &RightView);
		RightView.HSplitTop(2 * MarginSmall, nullptr, &RightView);
		DoLaserPreview(&LaserPreview, LaserShotgunOutlineColor, LaserShotgunInnerColor, LASERTYPE_SHOTGUN);

		RightView.HSplitTop(LaserPreviewHeight, &LaserPreview, &RightView);
		RightView.HSplitTop(2 * MarginSmall, nullptr, &RightView);
		DoLaserPreview(&LaserPreview, LaserDoorOutlineColor, LaserDoorInnerColor, LASERTYPE_DOOR);

		RightView.HSplitTop(LaserPreviewHeight, &LaserPreview, &RightView);
		RightView.HSplitTop(2 * MarginSmall, nullptr, &RightView);
		DoLaserPreview(&LaserPreview, LaserFreezeOutlineColor, LaserFreezeInnerColor, LASERTYPE_FREEZE);

		RightView.HSplitTop(LaserPreviewHeight, &LaserPreview, &RightView);
		RightView.HSplitTop(2 * MarginSmall, nullptr, &RightView);
		DoLaserPreview(&LaserPreview, LaserDraggerOutlineColor, LaserDraggerInnerColor, LASERTYPE_DRAGGER);
		ContentBottom = maximum(ContentBottom, maximum(LeftView.y, RightView.y));
	}

	return ContentBottom;
}

float CMenus::RenderSettingsDDNet(CUIRect MainView)
{
	const float BaseContentBottom = MainView.y + MainView.h;
	float ContentBottom = BaseContentBottom;
	CUIRect Button, Left, Right, LeftLeft, Label;

#if defined(CONF_AUTOUPDATE)
	CUIRect UpdaterRect;
	MainView.HSplitBottom(20.0f, &MainView, &UpdaterRect);
	MainView.HSplitBottom(5.0f, &MainView, nullptr);
#endif

	// demo
	CUIRect Demo;
	MainView.HSplitTop(110.0f, &Demo, &MainView);
	Demo.HSplitTop(30.0f, &Label, &Demo);
	Ui()->DoLabel(&Label, Localize("Demo"), 20.0f, TEXTALIGN_ML);
	Label.VSplitMid(nullptr, &Label, 20.0f);
	Ui()->DoLabel(&Label, Localize("Ghost"), 20.0f, TEXTALIGN_ML);

	Demo.HSplitTop(5.0f, nullptr, &Demo);
	Demo.VSplitMid(&Left, &Right, 20.0f);

	Left.HSplitTop(20.0f, &Button, &Left);
	if(DoButton_CheckBox(&g_Config.m_ClAutoRaceRecord, Localize("Save the best demo of each race"), g_Config.m_ClAutoRaceRecord, &Button))
	{
		g_Config.m_ClAutoRaceRecord ^= 1;
	}

	Left.HSplitTop(20.0f, &Button, &Left);
	if(DoButton_CheckBox(&g_Config.m_ClReplays, Localize("Enable replays"), g_Config.m_ClReplays, &Button))
	{
		g_Config.m_ClReplays ^= 1;
		Client()->DemoRecorder_UpdateReplayRecorder();
	}

	Left.HSplitTop(20.0f, &Button, &Left);
	if(g_Config.m_ClReplays)
		Ui()->DoScrollbarOption(&g_Config.m_ClReplayLength, &g_Config.m_ClReplayLength, &Button, Localize("Default length"), 10, 600, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE);

	Right.HSplitTop(20.0f, &Button, &Right);
	if(DoButton_CheckBox(&g_Config.m_ClRaceGhost, Localize("Enable ghost"), g_Config.m_ClRaceGhost, &Button))
	{
		g_Config.m_ClRaceGhost ^= 1;
	}
	GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ClRaceGhost, &Button, Localize("When you cross the start line, show a ghost tee replicating the movements of your best time"));

	if(g_Config.m_ClRaceGhost)
	{
		Right.HSplitTop(20.0f, &Button, &Right);
		Button.VSplitMid(&LeftLeft, &Button);
		if(DoButton_CheckBox(&g_Config.m_ClRaceShowGhost, Localize("Show ghost"), g_Config.m_ClRaceShowGhost, &LeftLeft))
		{
			g_Config.m_ClRaceShowGhost ^= 1;
		}
		Ui()->DoScrollbarOption(&g_Config.m_ClRaceGhostAlpha, &g_Config.m_ClRaceGhostAlpha, &Button, Localize("Opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");

		Right.HSplitTop(20.0f, &Button, &Right);
		if(DoButton_CheckBox(&g_Config.m_ClRaceSaveGhost, Localize("Save ghost"), g_Config.m_ClRaceSaveGhost, &Button))
		{
			g_Config.m_ClRaceSaveGhost ^= 1;
		}

		if(g_Config.m_ClRaceSaveGhost)
		{
			Right.HSplitTop(20.0f, &Button, &Right);
			if(DoButton_CheckBox(&g_Config.m_ClRaceGhostSaveBest, Localize("Only save improvements"), g_Config.m_ClRaceGhostSaveBest, &Button))
			{
				g_Config.m_ClRaceGhostSaveBest ^= 1;
			}
		}
	}
	ContentBottom = maximum(ContentBottom, maximum(Left.y, Right.y));

	// gameplay
	CUIRect Gameplay;
	MainView.HSplitTop(150.0f, &Gameplay, &MainView);
	Gameplay.HSplitTop(30.0f, &Label, &Gameplay);
	Ui()->DoLabel(&Label, Localize("Gameplay"), 20.0f, TEXTALIGN_ML);
	Gameplay.HSplitTop(5.0f, nullptr, &Gameplay);
	Gameplay.VSplitMid(&Left, &Right, 20.0f);

	Left.HSplitTop(20.0f, &Button, &Left);
	Ui()->DoScrollbarOption(&g_Config.m_ClOverlayEntities, &g_Config.m_ClOverlayEntities, &Button, Localize("Overlay entities"), 0, 100);

	Left.HSplitTop(20.0f, &Button, &Left);
	Button.VSplitMid(&LeftLeft, &Button);

	if(DoButton_CheckBox(&g_Config.m_ClTextEntities, Localize("Show text entities"), g_Config.m_ClTextEntities, &LeftLeft))
		g_Config.m_ClTextEntities ^= 1;

	if(g_Config.m_ClTextEntities)
	{
		if(Ui()->DoScrollbarOption(&g_Config.m_ClTextEntitiesSize, &g_Config.m_ClTextEntitiesSize, &Button, Localize("Size"), 20, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE))
			GameClient()->m_MapImages.SetTextureScale(g_Config.m_ClTextEntitiesSize);
	}

	Left.HSplitTop(20.0f, &Button, &Left);
	Button.VSplitMid(&LeftLeft, &Button);

	if(DoButton_CheckBox(&g_Config.m_ClShowOthers, Localize("Show others"), g_Config.m_ClShowOthers == SHOW_OTHERS_ON, &LeftLeft))
		g_Config.m_ClShowOthers = g_Config.m_ClShowOthers != SHOW_OTHERS_ON ? SHOW_OTHERS_ON : SHOW_OTHERS_OFF;

	Ui()->DoScrollbarOption(&g_Config.m_ClShowOthersAlpha, &g_Config.m_ClShowOthersAlpha, &Button, Localize("Opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");

	GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ClShowOthersAlpha, &Button, Localize("Adjust the opacity of entities belonging to other teams, such as tees and name plates"));

	Left.HSplitTop(20.0f, &Button, &Left);
	static int s_ShowOwnTeamId = 0;
	if(DoButton_CheckBox(&s_ShowOwnTeamId, Localize("Show others (own team only)"), g_Config.m_ClShowOthers == SHOW_OTHERS_ONLY_TEAM, &Button))
	{
		g_Config.m_ClShowOthers = g_Config.m_ClShowOthers != SHOW_OTHERS_ONLY_TEAM ? SHOW_OTHERS_ONLY_TEAM : SHOW_OTHERS_OFF;
	}

	Left.HSplitTop(20.0f, &Button, &Left);
	if(DoButton_CheckBox(&g_Config.m_ClShowQuads, Localize("Show background quads"), g_Config.m_ClShowQuads, &Button))
	{
		g_Config.m_ClShowQuads ^= 1;
	}
	GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ClShowQuads, &Button, Localize("Quads are used for background decoration"));

	Right.HSplitTop(20.0f, &Button, &Right);
	if(Ui()->DoScrollbarOption(&g_Config.m_ClDefaultZoom, &g_Config.m_ClDefaultZoom, &Button, Localize("Default zoom"), 0, 20))
		GameClient()->m_Camera.SetZoom(CCamera::ZoomStepsToValue(g_Config.m_ClDefaultZoom - 10), g_Config.m_ClSmoothZoomTime, true);

	Right.HSplitTop(20.0f, &Button, &Right);
	if(DoButton_CheckBox(&g_Config.m_ClAntiPing, Localize("AntiPing"), g_Config.m_ClAntiPing, &Button))
	{
		g_Config.m_ClAntiPing ^= 1;
	}
	GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ClAntiPing, &Button, Localize("Tries to predict other entities to give a feel of low latency"));

	if(g_Config.m_ClAntiPing)
	{
		Right.HSplitTop(20.0f, &Button, &Right);
		if(DoButton_CheckBox(&g_Config.m_ClAntiPingPlayers, Localize("AntiPing: predict other players"), g_Config.m_ClAntiPingPlayers, &Button))
		{
			g_Config.m_ClAntiPingPlayers ^= 1;
		}

		Right.HSplitTop(20.0f, &Button, &Right);
		if(DoButton_CheckBox(&g_Config.m_ClAntiPingWeapons, Localize("AntiPing: predict weapons"), g_Config.m_ClAntiPingWeapons, &Button))
		{
			g_Config.m_ClAntiPingWeapons ^= 1;
		}

		Right.HSplitTop(20.0f, &Button, &Right);
		if(DoButton_CheckBox(&g_Config.m_ClAntiPingGrenade, Localize("AntiPing: predict grenade paths"), g_Config.m_ClAntiPingGrenade, &Button))
		{
			g_Config.m_ClAntiPingGrenade ^= 1;
		}
		Right.HSplitTop(20.0f, &Button, &Right);
		Ui()->DoScrollbarOption(&g_Config.m_ClPredictionMargin, &g_Config.m_ClPredictionMargin, &Button, Localize("AntiPing: prediction margin"), 1, 300);
	}
	ContentBottom = maximum(ContentBottom, maximum(Left.y, Right.y));

	CUIRect Background, Miscellaneous;
	MainView.VSplitMid(&Background, &Miscellaneous, 20.0f);

	// background
	Background.HSplitTop(30.0f, &Label, &Background);
	Background.HSplitTop(5.0f, nullptr, &Background);
	Ui()->DoLabel(&Label, Localize("Background"), 20.0f, TEXTALIGN_ML);

	ColorRGBA GreyDefault(0.5f, 0.5f, 0.5f, 1);

	static CButtonContainer s_ResetId1;
	DoLine_ColorPicker(&s_ResetId1, 25.0f, 13.0f, 5.0f, &Background, Localize("Regular background color"), &g_Config.m_ClBackgroundColor, GreyDefault, false);

	static CButtonContainer s_ResetId2;
	DoLine_ColorPicker(&s_ResetId2, 25.0f, 13.0f, 5.0f, &Background, Localize("Entities background color"), &g_Config.m_ClBackgroundEntitiesColor, GreyDefault, false);

	CUIRect EditBox, ReloadButton;
	Background.HSplitTop(20.0f, &Label, &Background);
	Background.HSplitTop(2.0f, nullptr, &Background);
	Label.VSplitLeft(100.0f, &Label, &EditBox);
	EditBox.VSplitRight(60.0f, &EditBox, &Button);
	Button.VSplitMid(&ReloadButton, &Button, 5.0f);
	EditBox.VSplitRight(5.0f, &EditBox, nullptr);

	Ui()->DoLabel(&Label, Localize("Map"), 14.0f, TEXTALIGN_ML);

	static CLineInput s_BackgroundEntitiesInput(g_Config.m_ClBackgroundEntities, sizeof(g_Config.m_ClBackgroundEntities));
	Ui()->DoEditBox(&s_BackgroundEntitiesInput, &EditBox, 14.0f);

	static CButtonContainer s_BackgroundEntitiesMapPicker, s_BackgroundEntitiesReload;

	if(Ui()->DoButton_FontIcon(&s_BackgroundEntitiesReload, FONT_ICON_ARROW_ROTATE_RIGHT, 0, &ReloadButton, BUTTONFLAG_LEFT))
	{
		GameClient()->m_Background.LoadBackground();
	}

	if(Ui()->DoButton_FontIcon(&s_BackgroundEntitiesMapPicker, FONT_ICON_FOLDER, 0, &Button, BUTTONFLAG_LEFT))
	{
		static SPopupMenuId s_PopupMapPickerId;
		static CPopupMapPickerContext s_PopupMapPickerContext;
		s_PopupMapPickerContext.m_pMenus = this;
		s_PopupMapPickerContext.MapListPopulate();
		Ui()->DoPopupMenu(&s_PopupMapPickerId, Ui()->MouseX(), Ui()->MouseY(), 300.0f, 250.0f, &s_PopupMapPickerContext, PopupMapPicker);
	}

	Background.HSplitTop(20.0f, &Button, &Background);
	const bool UseCurrentMap = str_comp(g_Config.m_ClBackgroundEntities, CURRENT_MAP) == 0;
	static int s_UseCurrentMapId = 0;
	if(DoButton_CheckBox(&s_UseCurrentMapId, Localize("Use current map as background"), UseCurrentMap, &Button))
	{
		if(UseCurrentMap)
			g_Config.m_ClBackgroundEntities[0] = '\0';
		else
			str_copy(g_Config.m_ClBackgroundEntities, CURRENT_MAP);
		GameClient()->m_Background.LoadBackground();
	}

	Background.HSplitTop(20.0f, &Button, &Background);
	if(DoButton_CheckBox(&g_Config.m_ClBackgroundShowTilesLayers, Localize("Show tiles layers from BG map"), g_Config.m_ClBackgroundShowTilesLayers, &Button))
		g_Config.m_ClBackgroundShowTilesLayers ^= 1;

	// miscellaneous
	Miscellaneous.HSplitTop(30.0f, &Label, &Miscellaneous);
	Miscellaneous.HSplitTop(5.0f, nullptr, &Miscellaneous);

	Ui()->DoLabel(&Label, Localize("Miscellaneous"), 20.0f, TEXTALIGN_ML);

	static CButtonContainer s_ButtonTimeout;
	Miscellaneous.HSplitTop(20.0f, &Button, &Miscellaneous);
	if(DoButton_Menu(&s_ButtonTimeout, Localize("New random timeout code"), 0, &Button))
	{
		Client()->GenerateTimeoutSeed();
	}

	Miscellaneous.HSplitTop(5.0f, nullptr, &Miscellaneous);
	Miscellaneous.HSplitTop(20.0f, &Label, &Miscellaneous);
	Miscellaneous.HSplitTop(2.0f, nullptr, &Miscellaneous);
	Ui()->DoLabel(&Label, Localize("Run on join"), 14.0f, TEXTALIGN_ML);
	Miscellaneous.HSplitTop(20.0f, &Button, &Miscellaneous);
	static CLineInput s_RunOnJoinInput(g_Config.m_ClRunOnJoin, sizeof(g_Config.m_ClRunOnJoin));
	s_RunOnJoinInput.SetEmptyText(Localize("Chat command (e.g. showall 1)"));
	Ui()->DoEditBox(&s_RunOnJoinInput, &Button, 14.0f);

#if defined(CONF_FAMILY_WINDOWS)
	static CButtonContainer s_ButtonUnregisterShell;
	Miscellaneous.HSplitTop(10.0f, nullptr, &Miscellaneous);
	Miscellaneous.HSplitTop(20.0f, &Button, &Miscellaneous);
	if(DoButton_Menu(&s_ButtonUnregisterShell, Localize("Unregister protocol and file extensions"), 0, &Button))
	{
		Client()->ShellUnregister();
	}
#endif

	// Updater
#if defined(CONF_AUTOUPDATE)
	{
		const bool NeedUpdate = GameClient()->m_BestClient.NeedUpdate();
		IUpdater::EUpdaterState State = Updater()->GetCurrentState();

		// Update Button
		char aBuf[256];
		if(NeedUpdate && State <= IUpdater::CLEAN)
		{
			str_format(aBuf, sizeof(aBuf), "BestClient %s Is release", GameClient()->m_BestClient.m_aVersionStr);
			UpdaterRect.VSplitLeft(TextRender()->TextWidth(14.0f, aBuf, -1, -1.0f) + 10.0f, &UpdaterRect, &Button);
			Button.VSplitLeft(100.0f, &Button, nullptr);
			static CButtonContainer s_ButtonUpdate;
			if(DoButton_Menu(&s_ButtonUpdate, Localize("Update now"), 0, &Button))
			{
				Updater()->InitiateUpdate();
			}
			TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
		}
		else if(State >= IUpdater::GETTING_MANIFEST && State < IUpdater::NEED_RESTART)
			str_copy(aBuf, Localize("Updating…"));
		else if(State == IUpdater::NEED_RESTART)
		{
			str_copy(aBuf, Localize("BestClient Client updated!"));
			m_NeedRestartUpdate = true;
		}
		else
		{
			str_copy(aBuf, Localize("No updates available"));
			UpdaterRect.VSplitLeft(TextRender()->TextWidth(14.0f, aBuf, -1, -1.0f) + 10.0f, &UpdaterRect, &Button);
			Button.VSplitLeft(100.0f, &Button, nullptr);
			static CButtonContainer s_ButtonUpdate;
			if(DoButton_Menu(&s_ButtonUpdate, Localize("Check now"), 0, &Button))
			{
				GameClient()->m_BestClient.FetchBestClientInfo();
			}
		}
		Ui()->DoLabel(&UpdaterRect, aBuf, 14.0f, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	}
#endif

	ContentBottom = maximum(ContentBottom, maximum(Background.y, Miscellaneous.y));
	return ContentBottom;
}

CUi::EPopupMenuFunctionResult CMenus::PopupMapPicker(void *pContext, CUIRect View, bool Active)
{
	CPopupMapPickerContext *pPopupContext = static_cast<CPopupMapPickerContext *>(pContext);
	CMenus *pMenus = pPopupContext->m_pMenus;

	static CListBox s_ListBox;
	s_ListBox.SetActive(Active);
	s_ListBox.DoStart(20.0f, pPopupContext->m_vMaps.size(), 1, 3, -1, &View, false);

	int MapIndex = 0;
	for(auto &Map : pPopupContext->m_vMaps)
	{
		MapIndex++;
		const CListboxItem Item = s_ListBox.DoNextItem(&Map, MapIndex == pPopupContext->m_Selection);
		if(!Item.m_Visible)
			continue;

		CUIRect Label, Icon;
		Item.m_Rect.VSplitLeft(20.0f, &Icon, &Label);

		char aLabelText[IO_MAX_PATH_LENGTH];
		str_copy(aLabelText, Map.m_aFilename);
		if(Map.m_IsDirectory)
			str_append(aLabelText, "/", sizeof(aLabelText));

		const char *pIconType;
		if(!Map.m_IsDirectory)
		{
			pIconType = FONT_ICON_MAP;
		}
		else
		{
			if(!str_comp(Map.m_aFilename, ".."))
				pIconType = FONT_ICON_FOLDER_TREE;
			else
				pIconType = FONT_ICON_FOLDER;
		}

		pMenus->TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		pMenus->TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
		pMenus->Ui()->DoLabel(&Icon, pIconType, 12.0f, TEXTALIGN_ML);
		pMenus->TextRender()->SetRenderFlags(0);
		pMenus->TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

		pMenus->Ui()->DoLabel(&Label, aLabelText, 10.0f, TEXTALIGN_ML);
	}

	const int NewSelected = s_ListBox.DoEnd();
	pPopupContext->m_Selection = NewSelected >= 0 ? NewSelected : -1;
	if(s_ListBox.WasItemSelected() || s_ListBox.WasItemActivated())
	{
		const CMapListItem &SelectedItem = pPopupContext->m_vMaps[pPopupContext->m_Selection];

		if(SelectedItem.m_IsDirectory)
		{
			if(!str_comp(SelectedItem.m_aFilename, ".."))
			{
				fs_parent_dir(pPopupContext->m_aCurrentMapFolder);
			}
			else
			{
				str_append(pPopupContext->m_aCurrentMapFolder, "/", sizeof(pPopupContext->m_aCurrentMapFolder));
				str_append(pPopupContext->m_aCurrentMapFolder, SelectedItem.m_aFilename, sizeof(pPopupContext->m_aCurrentMapFolder));
			}
			pPopupContext->MapListPopulate();
		}
		else
		{
			str_format(g_Config.m_ClBackgroundEntities, sizeof(g_Config.m_ClBackgroundEntities), "%s/%s", pPopupContext->m_aCurrentMapFolder, SelectedItem.m_aFilename);
			pMenus->GameClient()->m_Background.LoadBackground();
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	return CUi::POPUP_KEEP_OPEN;
}

void CMenus::CPopupMapPickerContext::MapListPopulate()
{
	m_vMaps.clear();
	char aTemp[IO_MAX_PATH_LENGTH];
	str_format(aTemp, sizeof(aTemp), "maps/%s", m_aCurrentMapFolder);
	m_pMenus->Storage()->ListDirectoryInfo(IStorage::TYPE_ALL, aTemp, MapListFetchCallback, this);
	std::stable_sort(m_vMaps.begin(), m_vMaps.end(), CompareFilenameAscending);
	m_Selection = -1;
}

int CMenus::CPopupMapPickerContext::MapListFetchCallback(const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser)
{
	CPopupMapPickerContext *pRealUser = (CPopupMapPickerContext *)pUser;
	if((!IsDir && !str_endswith(pInfo->m_pName, ".map")) || !str_comp(pInfo->m_pName, ".") || (!str_comp(pInfo->m_pName, "..") && (!str_comp(pRealUser->m_aCurrentMapFolder, ""))))
		return 0;

	CMapListItem Item;
	str_copy(Item.m_aFilename, pInfo->m_pName);
	Item.m_IsDirectory = IsDir;

	pRealUser->m_vMaps.emplace_back(Item);

	return 0;
}
