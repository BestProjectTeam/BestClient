/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus.h"

#include <base/str.h>

#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/animstate.h>
#include <game/client/components/console.h>
#include <game/client/components/emoticon.h>
#include <game/client/components/skins.h>
#include <game/client/components/tooltips.h>
#include <game/client/gameclient.h>
#include <game/client/skin.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>

#include <algorithm>
#include <vector>

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

	const float EyeButtonSize = 40.0f;
	const float NameClanSkinHeight = 3.0f * 20.0f + 2.0f * 5.0f;
	const float CustomColorsRowHeight = 20.0f;
	const float EyesGridHeight = 2.0f * EyeButtonSize + 5.0f;
	const float LeftStackHeight = NameClanSkinHeight + 5.0f + CustomColorsRowHeight;
	const float CheckboxesHeight = 4.0f * 20.0f;
	const float MidStackHeight = CheckboxesHeight + 5.0f + EyesGridHeight;
	const float SkinPrefixHeight = 20.0f + 20.0f + 2.0f + 20.0f + 20.0f + 2.0f + 20.0f + 20.0f + 2.0f + 40.0f;
	const float TopSectionHeight = maximum(maximum(LeftStackHeight, MidStackHeight), SkinPrefixHeight);

	CUIRect TopSection, Checkboxes, SkinPrefix, Eyes, Button, Label, CustomColorsRow;
	MainView.HSplitTop(TopSectionHeight, &TopSection, &MainView);

	CUIRect YourSkin;
	TopSection.VSplitLeft(TopSection.w * 0.38f, &YourSkin, &TopSection);
	YourSkin.VSplitRight(12.0f, &YourSkin, nullptr);
	TopSection.VSplitMid(&Checkboxes, &SkinPrefix, 20.0f);
	Checkboxes.VSplitRight(10.0f, &Checkboxes, nullptr);

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

	// Emote preview 2x3 — fills the empty middle under checkboxes
	Checkboxes.HSplitTop(5.0f, nullptr, &Checkboxes);
	Checkboxes.HSplitTop(EyesGridHeight, &Eyes, &Checkboxes);

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

		SkinPrefix.HSplitTop(2.0f, nullptr, &SkinPrefix);
		SkinPrefix.HSplitTop(20.0f, &Label, &SkinPrefix);
		Ui()->DoLabel(&Label, Localize("Frozen skin"), 14.0f, TEXTALIGN_ML);

		SkinPrefix.HSplitTop(20.0f, &Button, &SkinPrefix);
		static CLineInput s_FrozenSkinInput(g_Config.m_TcFrozenSkin, sizeof(g_Config.m_TcFrozenSkin));
		Ui()->DoClearableEditBox(&s_FrozenSkinInput, &Button, 14.0f);

		SkinPrefix.HSplitTop(2.0f, nullptr, &SkinPrefix);
		SkinPrefix.HSplitTop(40.0f, &Button, &SkinPrefix);
		{
			CUIRect ScrollBar, ValueLabel;
			Button.HSplitMid(&ScrollBar, &ValueLabel);
			if(g_Config.m_TcFrozenSkinDarken > 70)
				g_Config.m_TcFrozenSkinDarken = 70;
			g_Config.m_TcFrozenSkinDarken = CUi::ms_LinearScrollbarScale.ToAbsolute(
				Ui()->DoScrollbarH(&g_Config.m_TcFrozenSkinDarken, &ScrollBar, CUi::ms_LinearScrollbarScale.ToRelative(g_Config.m_TcFrozenSkinDarken, 0, 70)),
				0, 70);
			char aFrozenBuf[64];
			str_format(aFrozenBuf, sizeof(aFrozenBuf), "%s: %i%%", Localize("Darken"), g_Config.m_TcFrozenSkinDarken);
			Ui()->DoLabel(&ValueLabel, aFrozenBuf, ValueLabel.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_ML);
		}
	}

	CUIRect RandomColorsButton, CustomColorsButton, RandomSkinButton, TeePreview, Fields;
	YourSkin.HSplitTop(NameClanSkinHeight, &TeePreview, &YourSkin);
	YourSkin.HSplitTop(5.0f, nullptr, &YourSkin);
	YourSkin.HSplitTop(CustomColorsRowHeight, &CustomColorsRow, &YourSkin);

	TeePreview.VSplitLeft(65.0f, &TeePreview, &Fields);
	Fields.VSplitLeft(5.0f, nullptr, &Fields);

	CUIRect NameRow, ClanRow, SkinRow;
	Fields.HSplitTop(20.0f, &NameRow, &Fields);
	Fields.HSplitTop(5.0f, nullptr, &Fields);
	Fields.HSplitTop(20.0f, &ClanRow, &Fields);
	Fields.HSplitTop(5.0f, nullptr, &Fields);
	Fields.HSplitTop(20.0f, &SkinRow, nullptr);

	CUIRect NameLabel, NameInput, ClanLabel, ClanInput, SkinLabel, SkinInput, FlagButton;
	NameRow.VSplitLeft(45.0f, &NameLabel, &NameInput);
	ClanRow.VSplitLeft(45.0f, &ClanLabel, &ClanInput);
	SkinRow.VSplitLeft(45.0f, &SkinLabel, &SkinInput);
	SkinInput.VSplitRight(44.0f, &SkinInput, &FlagButton);
	SkinInput.VSplitRight(5.0f, &SkinInput, nullptr);

	Ui()->DoLabel(&NameLabel, Localize("Name"), 14.0f, TEXTALIGN_ML);
	Ui()->DoLabel(&ClanLabel, Localize("Clan"), 14.0f, TEXTALIGN_ML);
	Ui()->DoLabel(&SkinLabel, Localize("Skin"), 14.0f, TEXTALIGN_ML);

	if(Ui()->DoEditBox(&s_NameInput, &NameInput, 14.0f))
		SetNeedSendInfo();
	if(!m_Dummy && GameClient()->m_Clans.IsPlayerClanLocked())
		Ui()->DoLabel(&ClanInput, g_Config.m_PlayerClan, 14.0f, TEXTALIGN_ML);
	else if(Ui()->DoEditBox(&s_ClanInput, &ClanInput, 14.0f))
		SetNeedSendInfo();

	char aBuf[128 + IO_MAX_PATH_LENGTH];
	CSkins::CSkinList &SkinList = GameClient()->m_Skins.SkinList();
	const CSkin *pDefaultSkin = GameClient()->m_Skins.Find("default");
	const CSkins::CSkinContainer *pOwnSkinContainer = GameClient()->m_Skins.FindContainerOrNullptr(pSkinName[0] == '\0' ? "default" : pSkinName);
	if(pOwnSkinContainer != nullptr && pOwnSkinContainer->IsSpecial())
	{
		pOwnSkinContainer = nullptr; // Special skins cannot be selected, show as missing due to invalid name
	}

	CTeeRenderInfo OwnSkinInfo;
	OwnSkinInfo.Apply(pOwnSkinContainer == nullptr || pOwnSkinContainer->Skin() == nullptr ? pDefaultSkin : pOwnSkinContainer->Skin().get());
	OwnSkinInfo.ApplyColors(*pUseCustomColor, *pColorBody, *pColorFeet);
	OwnSkinInfo.m_Size = 50.0f;

	// Tee — top-aligned with Name/Clan/Skin
	{
		vec2 OffsetToMid;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &OwnSkinInfo, OffsetToMid);
		const vec2 TeeRenderPos = vec2(TeePreview.x + TeePreview.w / 2.0f, TeePreview.y + OwnSkinInfo.m_Size / 2.0f + OffsetToMid.y);
		const vec2 DeltaPosition = Ui()->MousePos() - TeeRenderPos;
		const float Distance = length(DeltaPosition);
		const float InteractionDistance = 20.0f;
		const vec2 TeeDirection = Distance < InteractionDistance ? normalize(vec2(DeltaPosition.x, std::max(DeltaPosition.y, 0.5f))) : normalize(DeltaPosition);
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
			Ui()->DoLabel(&StatusIcon, pSkinContainer == nullptr || pSkinContainer->State() == CSkins::CSkinContainer::EState::ERROR ? FontIcon::TRIANGLE_EXCLAMATION : FontIcon::QUESTION, 12.0f, TEXTALIGN_MC);
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
	RenderSkinStatus(TeePreview, pOwnSkinContainer, &s_StatusTooltipId);

	// Skin name
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
	const float OldFlagWidth = FlagIcon.w;
	FlagIcon.w = FlagIcon.h * 2.0f;
	FlagIcon.x += (OldFlagWidth - FlagIcon.w) / 2.0f;
	GameClient()->m_CountryFlags.Render(*pCountry, ColorRGBA(1.0f, 1.0f, 1.0f, Ui()->HotItem() == &s_FlagButton ? 1.0f : 0.85f), FlagIcon.x, FlagIcon.y, FlagIcon.w, FlagIcon.h);

	// Default eyes — 2x3 grid in the middle empty space under checkboxes
	{
		CTeeRenderInfo EyeSkinInfo = OwnSkinInfo;
		EyeSkinInfo.m_Size = EyeButtonSize;
		vec2 OffsetToMid;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &EyeSkinInfo, OffsetToMid);

		static CButtonContainer s_aEyeButtons[NUM_EMOTES];
		CUIRect EyesRow;
		Eyes.HSplitTop(EyeButtonSize, &EyesRow, &Eyes);
		for(int CurrentEyeEmote = 0; CurrentEyeEmote < NUM_EMOTES; CurrentEyeEmote++)
		{
			EyesRow.VSplitLeft(EyeButtonSize, &Button, &EyesRow);
			EyesRow.VSplitLeft(5.0f, nullptr, &EyesRow);
			if((CurrentEyeEmote + 1) % 3 == 0 && CurrentEyeEmote + 1 < NUM_EMOTES)
			{
				Eyes.HSplitTop(5.0f, nullptr, &Eyes);
				Eyes.HSplitTop(EyeButtonSize, &EyesRow, &Eyes);
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

	// Custom colors — under Name/Clan/Skin
	CustomColorsButton = CustomColorsRow;
	CustomColorsButton.VSplitRight(30.0f, &CustomColorsButton, &RandomSkinButton);
	CustomColorsButton.VSplitRight(3.0f, &CustomColorsButton, nullptr);
	if(*pUseCustomColor)
	{
		CustomColorsButton.VSplitRight(110.0f, &CustomColorsButton, &RandomColorsButton);
		CustomColorsButton.VSplitRight(5.0f, &CustomColorsButton, nullptr);
	}

	static CButtonContainer s_RandomSkinButton;
	static const char *s_apDice[] = {FontIcon::DICE_ONE, FontIcon::DICE_TWO, FontIcon::DICE_THREE, FontIcon::DICE_FOUR, FontIcon::DICE_FIVE, FontIcon::DICE_SIX};
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

	if(DoButton_CheckBox(pUseCustomColor, Localize("Custom colors"), *pUseCustomColor, &CustomColorsButton))
	{
		*pUseCustomColor = *pUseCustomColor ? 0 : 1;
		SetNeedSendInfo();
	}

	// Custom color pickers — immediately under the top section
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
	if(DoButton_Menu(&s_SkinRefreshButton, FontIcon::ARROW_ROTATE_RIGHT, 0, &RefreshButton) || Input()->KeyPress(KEY_F5) || (Input()->KeyPress(KEY_R) && Input()->ModifierIsPressed()))
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
