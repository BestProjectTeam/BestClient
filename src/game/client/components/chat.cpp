/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "chat.h"

#include <base/color.h>
#include <base/io.h>
#include <base/log.h>
#include <base/log_color.h>
#include <base/time.h>

#include <engine/editor.h>
#include <engine/external/regex.h>
#include <engine/font_icons.h> // bestclient
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/shared/csv.h>
#include <engine/textrender.h>

#include <generated/protocol.h>
#include <generated/protocol7.h>

#include <game/client/animstate.h>
#include <game/client/components/bestclient/chat_qol.h> // bestclient
#include <game/client/components/bestclient/chat_scroll.h> // bestclient
#include <game/client/components/censor.h>
#include <game/client/components/hud_layout.h> // bestclient
#include <game/client/components/scoreboard.h>
#include <game/client/components/skins.h>
#include <game/client/components/sounds.h>
#include <game/client/components/tclient/colored_parts.h>
#include <game/client/gameclient.h>
#include <game/client/ui_scrollregion.h> // bestclient
#include <game/localization.h>

#include <algorithm> // bestclient
#include <string> // bestclient

char CChat::ms_aDisplayText[MAX_CHAT_LENGTH] = "";

CChat::CLine::CLine()
{
	m_TextContainerIndex.Reset();
	m_QuadContainerIndex = -1;
}

void CChat::CLine::Reset(CChat &This)
{
	This.TextRender()->DeleteTextContainer(m_TextContainerIndex);
	This.Graphics()->DeleteQuadContainer(m_QuadContainerIndex);
	m_Initialized = false;
	m_Time = 0;
	m_aText[0] = '\0';
	m_aName[0] = '\0';
	m_Friend = false;
	m_TimesRepeated = 0;
	m_pManagedTeeRenderInfo = nullptr;
	m_pTranslateResponse = nullptr;
	// bestclient
	m_CustomColor = std::nullopt;
	m_BcCopyAsLoad = false;
	This.GameClient()->m_ChatMedia.ResetLine(m_Media, This.Graphics());
	// bestclient
}

CChat::CChat()
{
	m_Mode = MODE_NONE;

	m_Input.SetCalculateOffsetCallback([this]() { return m_IsInputCensored; });
	m_Input.SetDisplayTextCallback([this](char *pStr, size_t NumChars) {
		m_IsInputCensored = false;
		if(
			g_Config.m_ClStreamerMode &&
			(str_startswith(pStr, "/login ") ||
				str_startswith(pStr, "/register ") ||
				str_startswith(pStr, "/code ") ||
				str_startswith(pStr, "/timeout ") ||
				str_startswith(pStr, "/save ") ||
				str_startswith(pStr, "/load ")))
		{
			bool Censor = false;
			const size_t NumLetters = std::min(NumChars, sizeof(ms_aDisplayText) - 1);
			for(size_t i = 0; i < NumLetters; ++i)
			{
				if(Censor)
					ms_aDisplayText[i] = '*';
				else
					ms_aDisplayText[i] = pStr[i];
				if(pStr[i] == ' ')
				{
					Censor = true;
					m_IsInputCensored = true;
				}
			}
			ms_aDisplayText[NumLetters] = '\0';
			return ms_aDisplayText;
		}
		return pStr;
	});
}

void CChat::RegisterCommand(const char *pName, const char *pParams, const char *pHelpText)
{
	// Don't allow duplicate commands.
	for(const auto &Command : m_vServerCommands)
		if(str_comp(Command.m_aName, pName) == 0)
			return;

	m_vServerCommands.emplace_back(pName, pParams, pHelpText);
	m_ServerCommandsNeedSorting = true;
}

void CChat::UnregisterCommand(const char *pName)
{
	m_vServerCommands.erase(std::remove_if(m_vServerCommands.begin(), m_vServerCommands.end(), [pName](const CCommand &Command) { return str_comp(Command.m_aName, pName) == 0; }), m_vServerCommands.end());
}

void CChat::RebuildChat()
{
	for(auto &Line : m_aLines)
	{
		if(!Line.m_Initialized)
			continue;
		TextRender()->DeleteTextContainer(Line.m_TextContainerIndex);
		Graphics()->DeleteQuadContainer(Line.m_QuadContainerIndex);
		// recalculate sizes
		Line.m_aYOffset[0] = -1.0f;
		Line.m_aYOffset[1] = -1.0f;
	}
}

void CChat::ClearLines()
{
	for(auto &Line : m_aLines)
		Line.Reset(*this);
	m_PrevScoreBoardShowed = false;
	m_PrevShowChat = false;
	// bestclient
	m_PrevHudLayoutX = -10000.0f;
	m_PrevHudLayoutY = -10000.0f;
	m_PrevHudLayoutScale = -1;
	m_PrevHudLayoutEnabled = true;
	CChatScroll::Reset(*this);
	// bestclient
}

void CChat::OnWindowResize()
{
	RebuildChat();
}

void CChat::Reset()
{
	ClearLines();

	m_Show = false;
	m_CompletionUsed = false;
	m_CompletionChosen = -1;
	m_aCompletionBuffer[0] = 0;
	m_PlaceholderOffset = 0;
	m_PlaceholderLength = 0;
	m_pHistoryEntry = nullptr;
	m_PendingChatCounter = 0;
	m_LastChatSend = 0;
	m_CurrentLine = 0;
	m_IsInputCensored = false;
	m_EditingNewLine = true;
	m_ServerSupportsCommandInfo = false;
	m_ServerCommandsNeedSorting = false;
	m_aCurrentInputText[0] = '\0';
	// bestclient
	GameClient()->m_BcUiAnimations.OnChatReset();
	// bestclient
	DisableMode();
	m_vServerCommands.clear();

	for(int64_t &LastSoundPlayed : m_aLastSoundPlayed)
		LastSoundPlayed = 0;
}

void CChat::OnRelease()
{
	m_Show = false;
}

void CChat::OnStateChange(int NewState, int OldState)
{
	if(OldState <= IClient::STATE_CONNECTING)
		Reset();
}

void CChat::ConSay(IConsole::IResult *pResult, void *pUserData)
{
	((CChat *)pUserData)->SendChat(0, pResult->GetString(0));
}

void CChat::ConSayTeam(IConsole::IResult *pResult, void *pUserData)
{
	((CChat *)pUserData)->SendChat(1, pResult->GetString(0));
}

void CChat::ConChat(IConsole::IResult *pResult, void *pUserData)
{
	const char *pMode = pResult->GetString(0);
	if(str_comp(pMode, "all") == 0)
		((CChat *)pUserData)->EnableMode(0);
	else if(str_comp(pMode, "team") == 0)
		((CChat *)pUserData)->EnableMode(1);
	else
		log_error("chat", "expected all or team as mode");

	if(pResult->GetString(1)[0] || g_Config.m_ClChatReset)
		((CChat *)pUserData)->m_Input.Set(pResult->GetString(1));
}

void CChat::ConShowChat(IConsole::IResult *pResult, void *pUserData)
{
	((CChat *)pUserData)->m_Show = pResult->GetInteger(0) != 0;
}

void CChat::ConEcho(IConsole::IResult *pResult, void *pUserData)
{
	((CChat *)pUserData)->Echo(pResult->GetString(0));
}

void CChat::ConClearChat(IConsole::IResult *pResult, void *pUserData)
{
	((CChat *)pUserData)->ClearLines();
}

void CChat::ConchainChatOld(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	((CChat *)pUserData)->RebuildChat();
}

void CChat::ConchainChatFontSize(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	CChat *pChat = (CChat *)pUserData;
	pChat->EnsureCoherentWidth();
	pChat->RebuildChat();
}

void CChat::ConchainChatWidth(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	CChat *pChat = (CChat *)pUserData;
	pChat->EnsureCoherentFontSize();
	pChat->RebuildChat();
}

void CChat::Echo(const char *pString)
{
	AddLine(CLIENT_MSG, 0, pString);
}

// bestclient
void CChat::AddColoredLine(const char *pLine, ColorRGBA Color, bool PlaySystemSound, bool CopyAsLoad)
{
	if(!pLine || pLine[0] == '\0')
		return;

	const int PrevShowChatClient = g_Config.m_TcShowChatClient;
	g_Config.m_TcShowChatClient = 1;
	AddLine(CLIENT_MSG, 0, pLine);
	g_Config.m_TcShowChatClient = PrevShowChatClient;

	CLine &Line = m_aLines[m_CurrentLine];
	if(!Line.m_Initialized || Line.m_ClientId != CLIENT_MSG)
		return;

	Line.m_CustomColor = Color;
	Line.m_BcCopyAsLoad = CopyAsLoad;
	Line.m_aName[0] = '\0';
	TextRender()->DeleteTextContainer(Line.m_TextContainerIndex);
	Graphics()->DeleteQuadContainer(Line.m_QuadContainerIndex);
	Line.m_TextContainerIndex.Reset();
	Line.m_QuadContainerIndex = -1;
	Line.m_aYOffset[0] = -1.0f;
	Line.m_aYOffset[1] = -1.0f;

	if(PlaySystemSound)
	{
		const int64_t Now = time();
		if(Now - m_aLastSoundPlayed[CHAT_SERVER] >= time_freq() * 3 / 10 && g_Config.m_SndServerMessage)
		{
			GameClient()->m_Sounds.Play(CSounds::CHN_GUI, SOUND_CHAT_SERVER, 1.0f);
			m_aLastSoundPlayed[CHAT_SERVER] = Now;
		}
	}
}
// bestclient

void CChat::OnConsoleInit()
{
	Console()->Register("say", "r[message]", CFGFLAG_CLIENT, ConSay, this, "Say in chat");
	Console()->Register("say_team", "r[message]", CFGFLAG_CLIENT, ConSayTeam, this, "Say in team chat");
	Console()->Register("chat", "s['team'|'all'] ?r[message]", CFGFLAG_CLIENT, ConChat, this, "Enable chat with all/team mode");
	Console()->Register("+show_chat", "", CFGFLAG_CLIENT, ConShowChat, this, "Show chat");
	Console()->Register("echo", "r[message]", CFGFLAG_CLIENT | CFGFLAG_STORE, ConEcho, this, "Echo the text in chat window");
	Console()->Register("clear_chat", "", CFGFLAG_CLIENT | CFGFLAG_STORE, ConClearChat, this, "Clear chat messages");
}

void CChat::OnInit()
{
	Reset();
	Console()->Chain("cl_chat_old", ConchainChatOld, this);
	Console()->Chain("cl_chat_size", ConchainChatFontSize, this);
	Console()->Chain("cl_chat_width", ConchainChatWidth, this);
}

// bestclient
vec2 CChat::ChatMousePos() const
{
	const float Height = 300.0f;
	const float Width = Height * Graphics()->ScreenAspect();
	const vec2 WindowSize(std::max(1.0f, (float)Graphics()->WindowWidth()), std::max(1.0f, (float)Graphics()->WindowHeight()));
	const vec2 UiMousePos = Ui()->UpdatedMousePos() * vec2(Ui()->Screen()->w, Ui()->Screen()->h) / WindowSize;
	return UiMousePos * vec2(Width / Ui()->Screen()->w, Height / Ui()->Screen()->h);
}

static bool TranslateBackendNeedsEndpoint(int BackendIndex)
{
	if(BackendIndex < 0 || BackendIndex >= (int)g_aTranslateBackends.size())
		return false;
	return g_aTranslateBackends[BackendIndex].m_NeedsEndpointConfig;
}

static int TranslateBackendIndex()
{
	for(size_t i = 0; i < g_aTranslateBackends.size(); ++i)
		if(str_comp_nocase(g_Config.m_TcTranslateBackend, g_aTranslateBackends[i].m_pValue) == 0)
			return (int)i;
	return 0;
}

static float TranslateSettingsPopupHeight(bool NeedsEndpointConfig)
{
	constexpr float RowHeight = 20.0f;
	constexpr float Gap = 3.0f;
	constexpr float BottomPadding = 12.0f;
	constexpr float RowsWithGap = RowHeight + Gap;
	float Height = RowsWithGap * 7 + Gap + RowHeight;
	if(NeedsEndpointConfig)
		Height += RowHeight + Gap + RowHeight;
	return Height + BottomPadding;
}

void CChat::OpenTranslateSettingsPopup()
{
	const float PopupWidth = 330.0f;
	const float PopupHeight = TranslateSettingsPopupHeight(TranslateBackendNeedsEndpoint(TranslateBackendIndex()));
	const float PopupX = std::clamp(m_TranslateButtonUiRect.x + m_TranslateButtonUiRect.w / 2.0f - PopupWidth / 2.0f, 0.0f, Ui()->Screen()->w - PopupWidth);
	const float PopupY = std::max(0.0f, m_TranslateButtonUiRect.y - PopupHeight - 4.0f);
	Ui()->DoPopupMenu(&m_TranslateSettingsPopupId, PopupX, PopupY, PopupWidth, PopupHeight, this, PopupTranslateSettings);
}

CUi::EPopupMenuFunctionResult CChat::PopupTranslateSettings(void *pContext, CUIRect View, bool Active)
{
	CChat *pChat = static_cast<CChat *>(pContext);
	(void)Active;

	static const std::vector<STranslateLanguage> s_vSourceLanguages = {
		{"Auto", "auto"}, {"Russian", "ru"}, {"English", "en"}, {"German", "de"},
		{"French", "fr"}, {"Spanish", "es"}, {"Polish", "pl"}, {"Chinese", "zh"},
		{"Portuguese", "pt"}, {"Turkish", "tr"},
	};
	static CUi::SDropDownState s_IncomingSourceState;
	static CUi::SDropDownState s_IncomingTargetState;
	static CUi::SDropDownState s_OutgoingSourceState;
	static CUi::SDropDownState s_OutgoingTargetState;
	static CUi::SDropDownState s_BackendState;
	static CUi::SDropDownState s_ShiftClickTargetState;
	static CScrollRegion s_IncomingSourceScroll;
	static CScrollRegion s_IncomingTargetScroll;
	static CScrollRegion s_OutgoingSourceScroll;
	static CScrollRegion s_OutgoingTargetScroll;
	static CScrollRegion s_BackendScroll;
	static CScrollRegion s_ShiftClickTargetScroll;
	s_IncomingSourceState.m_SelectionPopupContext.m_pScrollRegion = &s_IncomingSourceScroll;
	s_IncomingTargetState.m_SelectionPopupContext.m_pScrollRegion = &s_IncomingTargetScroll;
	s_OutgoingSourceState.m_SelectionPopupContext.m_pScrollRegion = &s_OutgoingSourceScroll;
	s_OutgoingTargetState.m_SelectionPopupContext.m_pScrollRegion = &s_OutgoingTargetScroll;
	s_BackendState.m_SelectionPopupContext.m_pScrollRegion = &s_BackendScroll;
	s_ShiftClickTargetState.m_SelectionPopupContext.m_pScrollRegion = &s_ShiftClickTargetScroll;

	std::vector<const char *> BackendNames;
	for(const STranslateBackendInfo &Backend : g_aTranslateBackends)
		BackendNames.push_back(Backend.m_pName);
	int BackendIndex = TranslateBackendIndex();
	static std::vector<const char *> SourceNames;

	if(Active)
		pChat->Ui()->SetPopupMenuHeight(&pChat->m_TranslateSettingsPopupId, TranslateSettingsPopupHeight(TranslateBackendNeedsEndpoint(BackendIndex)));

	const STranslateBackendInfo &Backend = g_aTranslateBackends[BackendIndex];
	std::vector<const char *> TargetNames;
	for(const STranslateLanguage &Language : Backend.m_vLanguages)
		TargetNames.push_back(Language.m_pName);
	SourceNames.clear();
	for(const STranslateLanguage &Language : s_vSourceLanguages)
		SourceNames.push_back(Language.m_pName);

	const float RowHeight = 20.0f;
	const float Gap = 3.0f;
	const float FontSize = 11.0f;
	CUIRect Row;
	View.HSplitTop(RowHeight, &Row, &View);
	{
		CUIRect Label;
		Row.VSplitLeft(90.0f, &Label, &Row);
		pChat->Ui()->DoLabel(&Label, Localize("Shift+click"), FontSize, TEXTALIGN_ML);
		const int ShiftClickTarget = std::clamp(g_Config.m_BcTranslateShiftClickTarget, 0, 2);
		const char *apShiftClickTargets[] = {Localize("Others"), Localize("Yours"), Localize("Both")};
		const int NewShiftClickTarget = pChat->Ui()->DoDropDown(&Row, ShiftClickTarget, apShiftClickTargets, std::size(apShiftClickTargets), s_ShiftClickTargetState);
		if(NewShiftClickTarget != ShiftClickTarget)
			g_Config.m_BcTranslateShiftClickTarget = NewShiftClickTarget;
	}
	View.HSplitTop(Gap, nullptr, &View);

	View.HSplitTop(RowHeight, &Row, &View);
	if(pChat->GameClient()->m_Menus.DoButton_CheckBox(&pChat->m_TranslateSettingsIncomingButton, Localize("Others"), g_Config.m_TcTranslateAutoIncoming, &Row))
		g_Config.m_TcTranslateAutoIncoming ^= 1;
	View.HSplitTop(Gap, nullptr, &View);

	auto RenderLanguagePair = [&](char *pSource, size_t SourceSize, char *pTarget, size_t TargetSize, CUi::SDropDownState &SourceState, CUi::SDropDownState &TargetState) {
		CUIRect Pair, Source, Arrow, Target;
		View.HSplitTop(RowHeight, &Pair, &View);
		Pair.VSplitLeft((Pair.w - 20.0f) / 2.0f, &Source, &Pair);
		Pair.VSplitLeft(20.0f, &Arrow, &Target);
		pChat->Ui()->DoLabel(&Arrow, "→", FontSize, TEXTALIGN_MC);
		const int SourceIndex = TranslateLanguageIndex(s_vSourceLanguages, pSource);
		const int NewSourceIndex = pChat->Ui()->DoDropDown(&Source, SourceIndex, SourceNames.data(), SourceNames.size(), SourceState);
		if(NewSourceIndex != SourceIndex)
			str_copy(pSource, s_vSourceLanguages[NewSourceIndex].m_pCode, SourceSize);
		const int TargetIndex = TranslateLanguageIndex(Backend.m_vLanguages, pTarget);
		const int NewTargetIndex = pChat->Ui()->DoDropDown(&Target, TargetIndex, TargetNames.data(), TargetNames.size(), TargetState);
		if(NewTargetIndex != TargetIndex)
			str_copy(pTarget, Backend.m_vLanguages[NewTargetIndex].m_pCode, TargetSize);
	};

	RenderLanguagePair(g_Config.m_BcTranslateIncomingSource, sizeof(g_Config.m_BcTranslateIncomingSource),
		g_Config.m_TcTranslateTarget, sizeof(g_Config.m_TcTranslateTarget), s_IncomingSourceState, s_IncomingTargetState);
	View.HSplitTop(Gap, nullptr, &View);
	View.HSplitTop(RowHeight, &Row, &View);
	if(pChat->GameClient()->m_Menus.DoButton_CheckBox(&pChat->m_TranslateSettingsOutgoingButton, Localize("Yours"), g_Config.m_TcTranslateAutoOutgoing, &Row))
		g_Config.m_TcTranslateAutoOutgoing ^= 1;
	View.HSplitTop(Gap, nullptr, &View);
	RenderLanguagePair(g_Config.m_BcTranslateOutgoingSource, sizeof(g_Config.m_BcTranslateOutgoingSource),
		g_Config.m_BcTranslateOutgoingTarget, sizeof(g_Config.m_BcTranslateOutgoingTarget), s_OutgoingSourceState, s_OutgoingTargetState);
	View.HSplitTop(Gap, nullptr, &View);
	View.HSplitTop(RowHeight, &Row, &View);
	if(pChat->GameClient()->m_Menus.DoButton_CheckBox(&pChat->m_TranslateSettingsPunctuationButton, Localize("Remove apostrophes"), g_Config.m_BcTranslateOutgoingStripPunctuation, &Row))
		g_Config.m_BcTranslateOutgoingStripPunctuation ^= 1;
	View.HSplitTop(Gap, nullptr, &View);
	const STranslateBackendInfo &BackendConfig = g_aTranslateBackends[BackendIndex];
	if(BackendConfig.m_NeedsEndpointConfig)
	{
		static CLineInput s_TranslateEndpoint(g_Config.m_TcTranslateEndpoint, sizeof(g_Config.m_TcTranslateEndpoint));
		static CLineInput s_TranslateKey(g_Config.m_TcTranslateKey, sizeof(g_Config.m_TcTranslateKey));
		const bool IsDeepLBackend = str_comp_nocase(BackendConfig.m_pValue, "deepl") == 0;
		s_TranslateEndpoint.SetEmptyText(IsDeepLBackend ? "https://api-free.deepl.com/v2/translate" : "http://localhost:5000/translate");
		s_TranslateKey.SetEmptyText(IsDeepLBackend ? "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx:fx" : "optional API key");
		View.HSplitTop(RowHeight, &Row, &View);
		CUIRect Label;
		Row.VSplitLeft(90.0f, &Label, &Row);
		pChat->Ui()->DoLabel(&Label, Localize("Endpoint"), FontSize, TEXTALIGN_ML);
		pChat->Ui()->DoEditBox(&s_TranslateEndpoint, &Row, FontSize);
		View.HSplitTop(Gap, nullptr, &View);
		View.HSplitTop(RowHeight, &Row, &View);
		Row.VSplitLeft(90.0f, &Label, &Row);
		pChat->Ui()->DoLabel(&Label, Localize("API key"), FontSize, TEXTALIGN_ML);
		pChat->Ui()->DoEditBox(&s_TranslateKey, &Row, FontSize);
		View.HSplitTop(Gap, nullptr, &View);
	}
	View.HSplitTop(RowHeight, &Row, &View);
	Row.VSplitLeft(90.0f, nullptr, &Row);
	const int NewBackendIndex = pChat->Ui()->DoDropDown(&Row, BackendIndex, BackendNames.data(), BackendNames.size(), s_BackendState, true);
	if(NewBackendIndex != BackendIndex)
		str_copy(g_Config.m_TcTranslateBackend, g_aTranslateBackends[NewBackendIndex].m_pValue);
	const int DisplayBackendIndex = NewBackendIndex != BackendIndex ? NewBackendIndex : BackendIndex;
	if(Active)
		pChat->Ui()->SetPopupMenuHeight(&pChat->m_TranslateSettingsPopupId, TranslateSettingsPopupHeight(TranslateBackendNeedsEndpoint(DisplayBackendIndex)));

	return CUi::POPUP_KEEP_OPEN;
}

void CChat::RenderTranslateSettingsButton(const CUIRect &ButtonRect)
{
	const bool Hovered = Ui()->MouseHovered(&ButtonRect);
	const bool Active = g_Config.m_TcTranslateAutoIncoming || g_Config.m_TcTranslateAutoOutgoing;
	const bool Open = Ui()->IsPopupOpen(&m_TranslateSettingsPopupId);
	const ColorRGBA IconColor = Open ? ColorRGBA(0.5f, 0.65f, 1.0f, 1.0f) :
		(Active ? ColorRGBA(0.35f, 0.85f, 0.45f, 1.0f) : ColorRGBA(1.0f, 1.0f, 1.0f, Hovered ? 1.0f : 0.8f));
	const float IconSize = ButtonRect.h * 1.65f;
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH |
		ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING |
		ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING |
		ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT |
		ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	TextRender()->TextColor(IconColor);
	Ui()->DoLabel(&ButtonRect, FontIcon::EARTH_AMERICAS, IconSize, TEXTALIGN_MC);
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	if(Hovered)
		Ui()->SetHotItem(&m_TranslateSettingsButton);
	GameClient()->m_Tooltips.DoToolTip(&m_TranslateSettingsButton, &ButtonRect, Localize("Chat translate settings"));
}
// bestclient

bool CChat::OnInput(const IInput::CEvent &Event)
{
	// bestclient
	if(GameClient()->m_ChatMedia.OnInput(*this, Event))
		return true;
	if(CChatScroll::OnInput(*this, Event))
		return true;
	// bestclient

	if(m_Mode == MODE_NONE)
		return false;

	// bestclient
	if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_ESCAPE && Ui()->IsPopupOpen())
	{
		Ui()->ClosePopupMenus();
		return true;
	}
	if(Ui()->IsPopupOpen(&m_TranslateSettingsPopupId) && Ui()->OnInput(Event))
		return true;
	if(Event.m_Key == KEY_MOUSE_1 && m_TranslateButtonRectValid)
	{
		const vec2 MousePos = ChatMousePos();
		const bool InsideButton = MousePos.x >= m_TranslateButtonRect.x && MousePos.x <= m_TranslateButtonRect.x + m_TranslateButtonRect.w &&
			MousePos.y >= m_TranslateButtonRect.y && MousePos.y <= m_TranslateButtonRect.y + m_TranslateButtonRect.h;
		if(Event.m_Flags & IInput::FLAG_PRESS)
		{
			m_TranslateButtonPressed = InsideButton;
			if(InsideButton)
				return true;
		}
		else if(Event.m_Flags & IInput::FLAG_RELEASE)
		{
			const bool Activate = m_TranslateButtonPressed && InsideButton;
			m_TranslateButtonPressed = false;
			if(Activate)
			{
				if(Input()->ShiftIsPressed())
					GameClient()->m_Translate.ToggleShiftClick();
				else if(Ui()->IsPopupOpen(&m_TranslateSettingsPopupId))
					Ui()->ClosePopupMenu(&m_TranslateSettingsPopupId, true);
				else
					OpenTranslateSettingsPopup();
				return true;
			}
		}
	}
	// bestclient

	if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_ESCAPE)
	{
		DisableMode();
		GameClient()->OnRelease();
		if(g_Config.m_ClChatReset)
		{
			m_Input.Clear();
			m_pHistoryEntry = nullptr;
		}
	}
	else if(Event.m_Flags & IInput::FLAG_PRESS && (Event.m_Key == KEY_RETURN || Event.m_Key == KEY_KP_ENTER))
	{
		if(m_ServerCommandsNeedSorting)
		{
			std::sort(m_vServerCommands.begin(), m_vServerCommands.end());
			m_ServerCommandsNeedSorting = false;
		}

		if(GameClient()->m_BindChat.ChatDoBinds(m_Input.GetString()))
			; // Do nothing as bindchat was executed
		// bestclient
		else if(CChatQoL::TryHandleBangCommand(*this, m_Input.GetString()))
			;
		// bestclient
		else if(GameClient()->m_TClient.ChatDoSpecId(m_Input.GetString()))
			; // Do nothing as specid was executed
		else
		{
			// bestclient
			const char *pLine = m_Input.GetString();
			char aConvertedLine[MAX_CHAT_LENGTH];
			if(CChatQoL::TryConvertWrongLayoutSlashCommand(*this, pLine, aConvertedLine, sizeof(aConvertedLine)))
				pLine = aConvertedLine;
			// bestclient
			if(GameClient()->m_Translate.ChatDoTranslateOutgoing(m_Mode == MODE_TEAM ? 1 : 0, pLine))
				; // Do nothing as outgoing translate was queued
			else
				SendChatQueued(pLine);
		}
		m_pHistoryEntry = nullptr;
		DisableMode();
		GameClient()->OnRelease();
		m_Input.Clear();
	}
	if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_TAB)
	{
		const bool ShiftPressed = Input()->ShiftIsPressed();

		// fill the completion buffer
		if(!m_CompletionUsed)
		{
			const char *pCursor = m_Input.GetString() + m_Input.GetCursorOffset();
			for(size_t Count = 0; Count < m_Input.GetCursorOffset() && *(pCursor - 1) != ' '; --pCursor, ++Count)
				;
			m_PlaceholderOffset = pCursor - m_Input.GetString();

			for(m_PlaceholderLength = 0; *pCursor && *pCursor != ' '; ++pCursor)
				++m_PlaceholderLength;

			str_truncate(m_aCompletionBuffer, sizeof(m_aCompletionBuffer), m_Input.GetString() + m_PlaceholderOffset, m_PlaceholderLength);
		}

		if(!m_CompletionUsed && m_aCompletionBuffer[0] != '/')
		{
			// Create the completion list of player names through which the player can iterate
			const char *PlayerName, *FoundInput;
			m_PlayerCompletionListLength = 0;
			for(auto &PlayerInfo : GameClient()->m_Snap.m_apInfoByName)
			{
				if(PlayerInfo)
				{
					PlayerName = GameClient()->m_aClients[PlayerInfo->m_ClientId].m_aName;
					FoundInput = str_utf8_find_nocase(PlayerName, m_aCompletionBuffer);
					if(FoundInput != nullptr)
					{
						m_aPlayerCompletionList[m_PlayerCompletionListLength].m_ClientId = PlayerInfo->m_ClientId;
						// The score for suggesting a player name is determined by the distance of the search input to the beginning of the player name
						m_aPlayerCompletionList[m_PlayerCompletionListLength].m_Score = (int)(FoundInput - PlayerName);
						m_PlayerCompletionListLength++;
					}
				}
			}
			std::stable_sort(m_aPlayerCompletionList, m_aPlayerCompletionList + m_PlayerCompletionListLength,
				[](const CRateablePlayer &Player1, const CRateablePlayer &Player2) -> bool {
					return Player1.m_Score < Player2.m_Score;
				});
		}

		if(GameClient()->m_BindChat.ChatDoAutocomplete(ShiftPressed))
		{
		}
		// bestclient
		else if(CChatQoL::TryBangCommandAutocomplete(*this))
		{
		}
		// bestclient
		else if(m_aCompletionBuffer[0] == '/' && !m_vServerCommands.empty())
		{
			CCommand *pCompletionCommand = nullptr;

			const size_t NumCommands = m_vServerCommands.size();

			if(ShiftPressed && m_CompletionUsed)
				m_CompletionChosen--;
			else if(!ShiftPressed)
				m_CompletionChosen++;
			m_CompletionChosen = (m_CompletionChosen + 2 * NumCommands) % (2 * NumCommands);

			m_CompletionUsed = true;

			const char *pCommandStart = m_aCompletionBuffer + 1;
			for(size_t i = 0; i < 2 * NumCommands; ++i)
			{
				int SearchType;
				int Index;

				if(ShiftPressed)
				{
					SearchType = ((m_CompletionChosen - i + 2 * NumCommands) % (2 * NumCommands)) / NumCommands;
					Index = (m_CompletionChosen - i + NumCommands) % NumCommands;
				}
				else
				{
					SearchType = ((m_CompletionChosen + i) % (2 * NumCommands)) / NumCommands;
					Index = (m_CompletionChosen + i) % NumCommands;
				}

				auto &Command = m_vServerCommands[Index];

				if(str_startswith_nocase(Command.m_aName, pCommandStart))
				{
					pCompletionCommand = &Command;
					m_CompletionChosen = Index + SearchType * NumCommands;
					break;
				}
			}

			// insert the command
			if(pCompletionCommand)
			{
				char aBuf[MAX_CHAT_LENGTH];
				// add part before the name
				str_truncate(aBuf, sizeof(aBuf), m_Input.GetString(), m_PlaceholderOffset);

				// add the command
				str_append(aBuf, "/");
				str_append(aBuf, pCompletionCommand->m_aName);

				// add separator
				const char *pSeparator = pCompletionCommand->m_aParams[0] == '\0' ? "" : " ";
				str_append(aBuf, pSeparator);

				// add part after the name
				str_append(aBuf, m_Input.GetString() + m_PlaceholderOffset + m_PlaceholderLength);

				m_PlaceholderLength = str_length(pSeparator) + str_length(pCompletionCommand->m_aName) + 1;
				m_Input.Set(aBuf);
				m_Input.SetCursorOffset(m_PlaceholderOffset + m_PlaceholderLength);
			}
		}
		else
		{
			// find next possible name
			const char *pCompletionString = nullptr;
			if(m_PlayerCompletionListLength > 0)
			{
				// We do this in a loop, if a player left the game during the repeated pressing of Tab, they are skipped
				CGameClient::CClientData *pCompletionClientData;
				for(int i = 0; i < m_PlayerCompletionListLength; ++i)
				{
					if(ShiftPressed && m_CompletionUsed)
					{
						m_CompletionChosen--;
					}
					else if(!ShiftPressed)
					{
						m_CompletionChosen++;
					}
					if(m_CompletionChosen < 0)
					{
						m_CompletionChosen += m_PlayerCompletionListLength;
					}
					m_CompletionChosen %= m_PlayerCompletionListLength;
					m_CompletionUsed = true;

					pCompletionClientData = &GameClient()->m_aClients[m_aPlayerCompletionList[m_CompletionChosen].m_ClientId];
					if(!pCompletionClientData->m_Active)
					{
						continue;
					}

					pCompletionString = pCompletionClientData->m_aName;
					break;
				}
			}

			// insert the name
			if(pCompletionString)
			{
				char aBuf[MAX_CHAT_LENGTH];
				// add part before the name
				str_truncate(aBuf, sizeof(aBuf), m_Input.GetString(), m_PlaceholderOffset);

				// quote the name
				char aQuoted[128];
				if((m_Input.GetString()[0] == '/' || GameClient()->m_BindChat.CheckBindChat(m_Input.GetString())) && (str_find(pCompletionString, " ") || str_find(pCompletionString, "\"")))
				{
					// escape the name
					str_copy(aQuoted, "\"");
					char *pDst = aQuoted + str_length(aQuoted);
					str_escape(&pDst, pCompletionString, aQuoted + sizeof(aQuoted));
					str_append(aQuoted, "\"");

					pCompletionString = aQuoted;
				}

				// add the name
				str_append(aBuf, pCompletionString);

				// add separator
				const char *pSeparator = "";
				if(*(m_Input.GetString() + m_PlaceholderOffset + m_PlaceholderLength) != ' ')
					pSeparator = m_PlaceholderOffset == 0 ? ": " : " ";
				else if(m_PlaceholderOffset == 0)
					pSeparator = ":";
				if(*pSeparator)
					str_append(aBuf, pSeparator);

				// add part after the name
				str_append(aBuf, m_Input.GetString() + m_PlaceholderOffset + m_PlaceholderLength);

				m_PlaceholderLength = str_length(pSeparator) + str_length(pCompletionString);
				m_Input.Set(aBuf);
				m_Input.SetCursorOffset(m_PlaceholderOffset + m_PlaceholderLength);
			}
		}
	}
	else
	{
		// reset name completion process
		if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key != KEY_TAB && Event.m_Key != KEY_LSHIFT && Event.m_Key != KEY_RSHIFT)
		{
			m_CompletionChosen = -1;
			m_CompletionUsed = false;
		}

		m_Input.ProcessInput(Event);
	}

	if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_UP)
	{
		if(m_EditingNewLine)
		{
			str_copy(m_aCurrentInputText, m_Input.GetString());
			m_EditingNewLine = false;
		}

		if(m_pHistoryEntry)
		{
			CHistoryEntry *pTest = m_History.Prev(m_pHistoryEntry);

			if(pTest)
				m_pHistoryEntry = pTest;
		}
		else
		{
			m_pHistoryEntry = m_History.Last();
		}

		if(m_pHistoryEntry)
			m_Input.Set(m_pHistoryEntry->m_aText);
	}
	else if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_DOWN)
	{
		if(m_pHistoryEntry)
			m_pHistoryEntry = m_History.Next(m_pHistoryEntry);

		if(m_pHistoryEntry)
		{
			m_Input.Set(m_pHistoryEntry->m_aText);
		}
		else if(!m_EditingNewLine)
		{
			m_Input.Set(m_aCurrentInputText);
			m_EditingNewLine = true;
		}
	}

	// bestclient
	GameClient()->m_BcUiAnimations.RefreshTypingAnimation(m_Input, m_Mode != MODE_NONE, Input()->HasComposition());
	// bestclient
	return true;
}

void CChat::EnableMode(int Team)
{
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return;

	if(m_Mode == MODE_NONE)
	{
		if(Team)
			m_Mode = MODE_TEAM;
		else
			m_Mode = MODE_ALL;

		m_CompletionChosen = -1;
		m_CompletionUsed = false;
		// bestclient
		GameClient()->m_BcUiAnimations.OnChatEnable();
		// bestclient
		m_Input.Activate(EInputPriority::CHAT);
		// bestclient
		GameClient()->m_BcUiAnimations.SyncTypingBaseline(m_Input);
		const vec2 WindowSize(std::max(1.0f, (float)Graphics()->WindowWidth()), std::max(1.0f, (float)Graphics()->WindowHeight()));
		m_LastMousePos = Ui()->UpdatedMousePos() * vec2(Ui()->Screen()->w, Ui()->Screen()->h) / WindowSize;
		const vec2 CenterPos = Ui()->Screen()->Center() / vec2(Ui()->Screen()->w, Ui()->Screen()->h) * WindowSize;
		Ui()->OnCursorMove(CenterPos.x - Ui()->UpdatedMousePos().x, CenterPos.y - Ui()->UpdatedMousePos().y);
		// bestclient
	}
}

void CChat::DisableMode()
{
	if(m_Mode != MODE_NONE)
	{
		// bestclient
		Ui()->ClosePopupMenu(&m_TranslateSettingsPopupId, true);
		m_TranslateButtonPressed = false;
		// bestclient
		m_Mode = MODE_NONE;
		// bestclient
		GameClient()->m_BcUiAnimations.OnChatDisable();
		CChatScroll::OnDisableMode(*this);
		// bestclient
		m_Input.Deactivate();
		// bestclient
		if(m_LastMousePos.has_value())
		{
			const vec2 WindowSize(std::max(1.0f, (float)Graphics()->WindowWidth()), std::max(1.0f, (float)Graphics()->WindowHeight()));
			const vec2 RestorePos = m_LastMousePos.value() / vec2(Ui()->Screen()->w, Ui()->Screen()->h) * WindowSize;
			Ui()->OnCursorMove(RestorePos.x - Ui()->UpdatedMousePos().x, RestorePos.y - Ui()->UpdatedMousePos().y);
		}
		m_LastMousePos = std::nullopt;
		// bestclient
	}
}

// bestclient
bool CChat::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(m_Mode == MODE_NONE)
		return false;
	Ui()->ConvertMouseMove(&x, &y, CursorType);
	Ui()->OnCursorMove(x, y);
	return true;
}
// bestclient

void CChat::OnMessage(int MsgType, void *pRawMsg)
{
	if(GameClient()->m_SuppressEvents)
		return;

	if(MsgType == NETMSGTYPE_SV_CHAT)
	{
		CNetMsg_Sv_Chat *pMsg = (CNetMsg_Sv_Chat *)pRawMsg;

		/*
		if(g_Config.m_ClCensorChat)
		{
			char aMessage[MAX_CHAT_LENGTH];
			str_copy(aMessage, pMsg->m_pMessage);
			GameClient()->m_Censor.CensorMessage(aMessage);
			AddLine(pMsg->m_ClientId, pMsg->m_Team, aMessage);
		}
		else
			AddLine(pMsg->m_ClientId, pMsg->m_Team, pMsg->m_pMessage);
		*/

		AddLine(pMsg->m_ClientId, pMsg->m_Team, pMsg->m_pMessage);

		if(Client()->State() != IClient::STATE_DEMOPLAYBACK &&
			pMsg->m_ClientId == SERVER_MSG)
		{
			StoreSave(pMsg->m_pMessage);
		}
	}
	else if(MsgType == NETMSGTYPE_SV_COMMANDINFO)
	{
		CNetMsg_Sv_CommandInfo *pMsg = (CNetMsg_Sv_CommandInfo *)pRawMsg;
		if(!m_ServerSupportsCommandInfo)
		{
			m_vServerCommands.clear();
			m_ServerSupportsCommandInfo = true;
		}
		RegisterCommand(pMsg->m_pName, pMsg->m_pArgsFormat, pMsg->m_pHelpText);
	}
	else if(MsgType == NETMSGTYPE_SV_COMMANDINFOREMOVE)
	{
		CNetMsg_Sv_CommandInfoRemove *pMsg = (CNetMsg_Sv_CommandInfoRemove *)pRawMsg;
		UnregisterCommand(pMsg->m_pName);
	}
}

bool CChat::LineShouldHighlight(const char *pLine, const char *pName)
{
	const char *pHit = str_utf8_find_nocase(pLine, pName);

	while(pHit)
	{
		int Length = str_length(pName);

		if(Length > 0 && (pLine == pHit || pHit[-1] == ' ') && (pHit[Length] == 0 || pHit[Length] == ' ' || pHit[Length] == '.' || pHit[Length] == '!' || pHit[Length] == ',' || pHit[Length] == '?' || pHit[Length] == ':'))
			return true;

		pHit = str_utf8_find_nocase(pHit + 1, pName);
	}

	return false;
}

static constexpr const char *SAVES_HEADER[] = {
	"Time",
	"Player",
	"Map",
	"Code",
};

// TODO: remove this in a few releases (in 2027 or later)
//       it got deprecated by CGameClient::StoreSave
void CChat::StoreSave(const char *pText)
{
	const char *pStart = str_find(pText, "Team successfully saved by ");
	const char *pMid = str_find(pText, ". Use '/load ");
	const char *pOn = str_find(pText, "' on ");
	const char *pEnd = str_find(pText, pOn ? " to continue" : "' to continue");

	if(!pStart || !pMid || !pEnd || pMid < pStart || pEnd < pMid || (pOn && (pOn < pMid || pEnd < pOn)))
		return;

	char aName[16];
	str_truncate(aName, sizeof(aName), pStart + 27, pMid - pStart - 27);

	char aSaveCode[64];

	str_truncate(aSaveCode, sizeof(aSaveCode), pMid + 13, (pOn ? pOn : pEnd) - pMid - 13);

	char aTimestamp[20];
	str_timestamp_format(aTimestamp, sizeof(aTimestamp), TimestampFormat::SPACE);

	const bool SavesFileExists = Storage()->FileExists(SAVES_FILE, IStorage::TYPE_SAVE);
	IOHANDLE File = Storage()->OpenFile(SAVES_FILE, IOFLAG_APPEND, IStorage::TYPE_SAVE);
	if(!File)
		return;

	const char *apColumns[4] = {
		aTimestamp,
		aName,
		GameClient()->Map()->BaseName(),
		aSaveCode,
	};

	if(!SavesFileExists)
	{
		CsvWrite(File, 4, SAVES_HEADER);
	}
	CsvWrite(File, 4, apColumns);
	io_close(File);
}

void CChat::AddLine(int ClientId, int Team, const char *pLine)
{
	if(*pLine == 0 ||
		(ClientId == SERVER_MSG && !g_Config.m_ClShowChatSystem) ||
		(ClientId >= 0 && (GameClient()->m_aClients[ClientId].m_aName[0] == '\0' || // unknown client
					  GameClient()->m_aClients[ClientId].m_ChatIgnore ||
					  (GameClient()->m_Snap.m_LocalClientId != ClientId && g_Config.m_ClShowChatFriends && !GameClient()->m_aClients[ClientId].m_Friend) ||
					  (GameClient()->m_Snap.m_LocalClientId != ClientId && g_Config.m_ClShowChatTeamMembersOnly && GameClient()->IsOtherTeam(ClientId) && GameClient()->m_Teams.Team(GameClient()->m_Snap.m_LocalClientId) != TEAM_FLOCK) ||
					  (GameClient()->m_Snap.m_LocalClientId != ClientId && GameClient()->m_aClients[ClientId].m_Foe))))
		return;

	// TClient
	if(ClientId == CLIENT_MSG && !g_Config.m_TcShowChatClient)
		return;

	// TClient
	if(([&]() {
		   if(ClientId == CLIENT_MSG || ClientId == SERVER_MSG)
			   return false;
		   for(const auto LocalClientId : GameClient()->m_aLocalIds)
			   if(LocalClientId == ClientId)
				   return false;
		   auto &Re = GameClient()->m_TClient.m_RegexChatIgnore;
		   return Re.error().empty() && Re.test(pLine);
	   })())
		return;

	// trim right and set maximum length to 256 utf8-characters
	int Length = 0;
	const char *pStr = pLine;
	const char *pEnd = nullptr;
	while(*pStr)
	{
		const char *pStrOld = pStr;
		int Code = str_utf8_decode(&pStr);

		// check if unicode is not empty
		if(!str_utf8_isspace(Code))
		{
			pEnd = nullptr;
		}
		else if(pEnd == nullptr)
		{
			pEnd = pStrOld;
		}

		if(++Length >= MAX_CHAT_LENGTH)
		{
			*(const_cast<char *>(pStr)) = '\0';
			break;
		}
	}
	if(pEnd != nullptr)
		*(const_cast<char *>(pEnd)) = '\0';

	if(*pLine == 0)
		return;

	bool Highlighted = false;

	auto &&FChatMsgCheckAndPrint = [](const CLine &Line) {
		ColorRGBA ChatLogColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		if(Line.m_Highlighted)
		{
			ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageHighlightColor));
		}
		else
		{
			if(Line.m_Friend && g_Config.m_ClMessageFriend)
				ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageFriendColor));
			else if(Line.m_Team)
				ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageTeamColor));
			else if(Line.m_ClientId == SERVER_MSG)
				ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageSystemColor));
			else if(Line.m_ClientId == CLIENT_MSG)
				ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageClientColor));
			else // regular message
				ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageColor));
		}

		const char *pFrom;
		if(Line.m_Whisper)
			pFrom = "chat/whisper";
		else if(Line.m_Team)
			pFrom = "chat/team";
		else if(Line.m_ClientId == SERVER_MSG)
			pFrom = "chat/server";
		else if(Line.m_ClientId == CLIENT_MSG)
			pFrom = "chat/client";
		else
			pFrom = "chat/all";

		log_info_color(color_cast<LOG_COLOR>(ChatLogColor), pFrom, "%s%s%s", Line.m_aName, Line.m_ClientId >= 0 ? ": " : "", Line.m_aText);
	};

	// Custom color for new line
	std::optional<ColorRGBA> CustomColor = std::nullopt;
	if(ClientId == CLIENT_MSG)
		CustomColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageClientColor));

	// bestclient
	if(Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		if(ClientId >= 0 && ClientId != GameClient()->m_aLocalIds[0] && ClientId != GameClient()->m_aLocalIds[1])
		{
			for(int LocalId : GameClient()->m_aLocalIds)
			{
				Highlighted |= LocalId >= 0 && LineShouldHighlight(pLine, GameClient()->m_aClients[LocalId].m_aName);
			}
		}
	}
	else
	{
		// on demo playback use local id from snap directly,
		// since m_aLocalIds isn't valid there
		Highlighted |= GameClient()->m_Snap.m_LocalClientId >= 0 && LineShouldHighlight(pLine, GameClient()->m_aClients[GameClient()->m_Snap.m_LocalClientId].m_aName);
	}

	if(g_Config.m_BcChatOnlyTagsAndWhispers && !Highlighted && Team < 2)
		return;
	// bestclient

	CLine &PreviousLine = m_aLines[m_CurrentLine];

	// Team Number:
	// 0 = global; 1 = team; 2 = sending whisper; 3 = receiving whisper

	// If it's a client message, m_aText will have ": " prepended so we have to work around it.
	if(PreviousLine.m_Initialized &&
		PreviousLine.m_TeamNumber == Team &&
		PreviousLine.m_ClientId == ClientId &&
		str_comp(PreviousLine.m_aText, pLine) == 0 &&
		PreviousLine.m_CustomColor == CustomColor)
	{
		PreviousLine.m_TimesRepeated++;
		TextRender()->DeleteTextContainer(PreviousLine.m_TextContainerIndex);
		Graphics()->DeleteQuadContainer(PreviousLine.m_QuadContainerIndex);
		PreviousLine.m_Time = time();
		PreviousLine.m_aYOffset[0] = -1.0f;
		PreviousLine.m_aYOffset[1] = -1.0f;

		FChatMsgCheckAndPrint(PreviousLine);
		return;
	}

	m_CurrentLine = (m_CurrentLine + 1) % MAX_LINES;

	// bestclient
	CChatScroll::OnLineAdded(*this);
	// bestclient

	CLine &CurrentLine = m_aLines[m_CurrentLine];
	CurrentLine.Reset(*this);
	CurrentLine.m_Initialized = true;
	CurrentLine.m_Time = time();
	CurrentLine.m_aYOffset[0] = -1.0f;
	CurrentLine.m_aYOffset[1] = -1.0f;
	CurrentLine.m_ClientId = ClientId;
	CurrentLine.m_TeamNumber = Team;
	CurrentLine.m_Team = Team == 1;
	CurrentLine.m_Whisper = Team >= 2;
	CurrentLine.m_NameColor = -2;
	CurrentLine.m_CustomColor = CustomColor;
	CurrentLine.m_Highlighted = Highlighted;

	str_copy(CurrentLine.m_aText, pLine);
	// bestclient
	GameClient()->m_ChatMedia.OnChatLineAdded(*this, CurrentLine.m_Media, CurrentLine.m_aText);
	// bestclient

	if(CurrentLine.m_ClientId == SERVER_MSG)
	{
		str_copy(CurrentLine.m_aName, "*** ");
	}
	else if(CurrentLine.m_ClientId == CLIENT_MSG)
	{
		str_copy(CurrentLine.m_aName, "— ");
	}
	else
	{
		const auto &LineAuthor = GameClient()->m_aClients[CurrentLine.m_ClientId];

		if(LineAuthor.m_Active)
		{
			if(LineAuthor.m_Team == TEAM_SPECTATORS)
				CurrentLine.m_NameColor = TEAM_SPECTATORS;

			if(GameClient()->IsTeamPlay())
			{
				if(LineAuthor.m_Team == TEAM_RED)
					CurrentLine.m_NameColor = TEAM_RED;
				else if(LineAuthor.m_Team == TEAM_BLUE)
					CurrentLine.m_NameColor = TEAM_BLUE;
			}
		}

		if(Team == TEAM_WHISPER_SEND)
		{
			str_copy(CurrentLine.m_aName, "→");
			if(LineAuthor.m_Active)
			{
				str_append(CurrentLine.m_aName, " ");
				str_append(CurrentLine.m_aName, LineAuthor.m_aName);
			}
			CurrentLine.m_NameColor = TEAM_BLUE;
			CurrentLine.m_Highlighted = false;
			Highlighted = false;
		}
		else if(Team == TEAM_WHISPER_RECV)
		{
			str_copy(CurrentLine.m_aName, "←");
			if(LineAuthor.m_Active)
			{
				str_append(CurrentLine.m_aName, " ");
				str_append(CurrentLine.m_aName, LineAuthor.m_aName);
			}
			CurrentLine.m_NameColor = TEAM_RED;
			CurrentLine.m_Highlighted = true;
			Highlighted = true;
		}
		else
		{
			str_copy(CurrentLine.m_aName, LineAuthor.m_aName);
		}

		if(LineAuthor.m_Active)
		{
			CurrentLine.m_Friend = LineAuthor.m_Friend;
			CurrentLine.m_pManagedTeeRenderInfo = GameClient()->CreateManagedTeeRenderInfo(LineAuthor);
		}
	}

	FChatMsgCheckAndPrint(CurrentLine);

	// play sound
	int64_t Now = time();
	if(ClientId == SERVER_MSG)
	{
		if(Now - m_aLastSoundPlayed[CHAT_SERVER] >= time_freq() * 3 / 10)
		{
			if(g_Config.m_SndServerMessage)
			{
				GameClient()->m_Sounds.Play(CSounds::CHN_GUI, SOUND_CHAT_SERVER, 1.0f);
				m_aLastSoundPlayed[CHAT_SERVER] = Now;
			}
		}
	}
	else if(ClientId == CLIENT_MSG)
	{
		// No sound yet
	}
	else if(Highlighted && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		if(Now - m_aLastSoundPlayed[CHAT_HIGHLIGHT] >= time_freq() * 3 / 10)
		{
			char aBuf[1024];
			str_format(aBuf, sizeof(aBuf), "%s: %s", CurrentLine.m_aName, CurrentLine.m_aText);
			Client()->Notify("DDNet Chat", aBuf);
			if(g_Config.m_SndHighlight)
			{
				GameClient()->m_Sounds.Play(CSounds::CHN_GUI, SOUND_CHAT_HIGHLIGHT, 1.0f);
				m_aLastSoundPlayed[CHAT_HIGHLIGHT] = Now;
			}

			if(g_Config.m_ClEditor)
			{
				GameClient()->Editor()->UpdateMentions();
			}
		}
	}
	else if(Team != TEAM_WHISPER_SEND)
	{
		if(Now - m_aLastSoundPlayed[CHAT_CLIENT] >= time_freq() * 3 / 10)
		{
			bool PlaySound = CurrentLine.m_Team ? g_Config.m_SndTeamChat : g_Config.m_SndChat;
#if defined(CONF_VIDEORECORDER)
			if(IVideo::Current())
			{
				PlaySound &= (bool)g_Config.m_ClVideoShowChat;
			}
#endif
			if(PlaySound)
			{
				GameClient()->m_Sounds.Play(CSounds::CHN_GUI, SOUND_CHAT_CLIENT, 1.0f);
				m_aLastSoundPlayed[CHAT_CLIENT] = Now;
			}
		}
	}

	// TClient
	GameClient()->m_Translate.AutoTranslate(CurrentLine);
}

void CChat::OnPrepareLines(float y)
{
	// bestclient
	static bool s_QuadScrollAnchorFixed = false;
	if(!s_QuadScrollAnchorFixed)
	{
		for(CLine &Line : m_aLines)
		{
			Graphics()->DeleteQuadContainer(Line.m_QuadContainerIndex);
			Line.m_QuadContainerIndex = -1;
		}
		s_QuadScrollAnchorFixed = true;
	}
	// bestclient
	const float Height = HudLayout::CANVAS_HEIGHT;
	const float Width = Height * Graphics()->ScreenAspect();
	const auto Layout = HudLayout::Get(HudLayout::MODULE_CHAT, Width, Height);
	const bool LayoutEnabled = HudLayout::IsEnabled(HudLayout::MODULE_CHAT);
	const float LayoutScale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	float x = Layout.m_X;
	float FontSize = this->FontSize();
	const bool IsScoreBoardOpen = GameClient()->m_Scoreboard.IsShown() && (Graphics()->ScreenAspect() > 1.7f); // only assume scoreboard when screen ratio is widescreen(something around 16:9)
	const bool EditorPreview = GameClient()->m_HudEditor.IsActive();
	const bool ShowLargeArea = EditorPreview || m_Show || (m_Mode != MODE_NONE && g_Config.m_ClShowChat == 1) || g_Config.m_ClShowChat == 2;
	const bool LayoutChanged = Layout.m_X != m_PrevHudLayoutX || Layout.m_Y != m_PrevHudLayoutY || Layout.m_Scale != m_PrevHudLayoutScale || LayoutEnabled != m_PrevHudLayoutEnabled;
	const bool ModeActive = m_Mode != MODE_NONE;
	const bool ForceRecreate = IsScoreBoardOpen != m_PrevScoreBoardShowed || ShowLargeArea != m_PrevShowChat || ModeActive != m_PrevModeActive || LayoutChanged;
	m_PrevScoreBoardShowed = IsScoreBoardOpen;
	m_PrevShowChat = ShowLargeArea;
	m_PrevHudLayoutX = Layout.m_X;
	m_PrevHudLayoutY = Layout.m_Y;
	m_PrevHudLayoutScale = Layout.m_Scale;
	m_PrevHudLayoutEnabled = LayoutEnabled;
	m_PrevModeActive = ModeActive;
	// bestclient

	const int TeeSize = MessageTeeSize();
	float RealMsgPaddingX = MessagePaddingX();
	float RealMsgPaddingY = MessagePaddingY();
	float RealMsgPaddingTee = TeeSize + MESSAGE_TEE_PADDING_RIGHT;

	if(g_Config.m_ClChatOld)
	{
		RealMsgPaddingX = 0;
		RealMsgPaddingY = 0;
		RealMsgPaddingTee = 0;
	}

	int64_t Now = time();
	// bestclient
	float LineWidth = (IsScoreBoardOpen ? std::max(85.0f * LayoutScale, FontSize * 85.0f / 6.0f) : ChatWidth()) - (RealMsgPaddingX * 1.5f) - RealMsgPaddingTee;
	// bestclient

	// bestclient
	float HeightLimit;
	if(IsScoreBoardOpen)
		HeightLimit = y - 93.0f * LayoutScale;
	else if(ShowLargeArea)
		HeightLimit = y - 223.0f * LayoutScale;
	else if(GameClient()->m_ChatMedia.ShouldExpandCompactAreaForMedia(*this, IsScoreBoardOpen, ShowLargeArea))
		HeightLimit = GameClient()->m_ChatMedia.CompactMediaHeightLimit(y);
	else
		HeightLimit = y - 73.0f * LayoutScale;
	// bestclient
	float Begin = x;
	float TextBegin = Begin + RealMsgPaddingX / 2.0f;
	int OffsetType = IsScoreBoardOpen ? 1 : 0;

	float LineY = y + m_BacklogScrollOffset;
	bool AllowTopClip = true;
	for(int i = 0; i < MAX_LINES; i++) // bestclient
	{
		CLine &Line = m_aLines[((m_CurrentLine - i) + MAX_LINES) % MAX_LINES];
		if(!Line.m_Initialized)
			break;
		// bestclient
		if(Now > Line.m_Time + 16 * time_freq() && !m_PrevShowChat && !GameClient()->m_HudEditor.IsActive())
			break;
		// bestclient

		const float LineH = Line.m_aYOffset[OffsetType] >= 0.0f ? Line.m_aYOffset[OffsetType] : FontSize + RealMsgPaddingY;
		LineY -= LineH;
		if(LineY < HeightLimit && !AllowTopClip)
			break;
		AllowTopClip = false;

		if(Line.m_TextContainerIndex.Valid() && Line.m_aYOffset[OffsetType] >= 0.0f && !ForceRecreate) // bestclient
			continue;

		TextRender()->DeleteTextContainer(Line.m_TextContainerIndex);
		Graphics()->DeleteQuadContainer(Line.m_QuadContainerIndex);

		char aClientId[16] = "";
		if(g_Config.m_ClShowIds && Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
		{
			GameClient()->FormatClientId(Line.m_ClientId, aClientId, EClientIdFormat::INDENT_AUTO);
		}

		char aCount[12];
		if(Line.m_ClientId < 0)
			str_format(aCount, sizeof(aCount), "[%d] ", Line.m_TimesRepeated + 1);
		else
			str_format(aCount, sizeof(aCount), " [%d]", Line.m_TimesRepeated + 1);

		const char *pText = Line.m_aText;
		// bestclient
		bool TextHiddenByStreamer = false;
		if(Config()->m_ClStreamerMode && Line.m_ClientId == SERVER_MSG)
		{
			if(str_startswith(Line.m_aText, "Team save in progress. You'll be able to load with '/load ") && str_endswith(Line.m_aText, "'"))
			{
				TextHiddenByStreamer = true;
				pText = "Team save in progress. You'll be able to load with '/load *** *** ***'";
			}
			else if(str_startswith(Line.m_aText, "Team save in progress. You'll be able to load with '/load") && str_endswith(Line.m_aText, "if it fails"))
			{
				TextHiddenByStreamer = true;
				pText = "Team save in progress. You'll be able to load with '/load *** *** ***' if save is successful or with '/load *** *** ***' if it fails";
			}
			else if(str_startswith(Line.m_aText, "Team successfully saved by ") && str_endswith(Line.m_aText, " to continue"))
			{
				TextHiddenByStreamer = true;
				pText = "Team successfully saved by ***. Use '/load *** *** ***' to continue";
			}
		}
		// bestclient

		// bestclient
		std::string VisibleTextStorage = GameClient()->m_ChatMedia.BuildVisibleMessageText(Line.m_Media, pText, false);
		pText = VisibleTextStorage.c_str();
		// bestclient

		const CColoredParts ColoredParts(pText, Line.m_ClientId == CLIENT_MSG);
		if(!ColoredParts.Colors().empty() && ColoredParts.Colors()[0].m_Index == 0)
			Line.m_CustomColor = ColoredParts.Colors()[0].m_Color;
		pText = ColoredParts.Text();

		const char *pTranslatedError = nullptr;
		const char *pTranslatedText = nullptr;
		const char *pTranslatedLanguage = nullptr;
		if(Line.m_pTranslateResponse != nullptr && Line.m_pTranslateResponse->m_Text[0])
		{
			// bestclient
			if(TextHiddenByStreamer)
			{
				pTranslatedError = TCLocalize("Translated text hidden due to streamer mode");
			}
			else if(Line.m_pTranslateResponse->m_Error)
			{
				pTranslatedError = Line.m_pTranslateResponse->m_Text;
			}
			else
			{
				pTranslatedText = Line.m_pTranslateResponse->m_Text;
				if(Line.m_pTranslateResponse->m_Language[0] != '\0')
					pTranslatedLanguage = Line.m_pTranslateResponse->m_Language;
			}
		}
		// bestclient

		// the position the text was created
		Line.m_TextYOffset = LineY + RealMsgPaddingY / 2.0f;

		int CurRenderFlags = TextRender()->GetRenderFlags();
		TextRender()->SetRenderFlags(CurRenderFlags | ETextRenderFlags::TEXT_RENDER_FLAG_NO_AUTOMATIC_QUAD_UPLOAD);

		// reset the cursor
		CTextCursor LineCursor;
		LineCursor.SetPosition(vec2(TextBegin, Line.m_TextYOffset));
		LineCursor.m_FontSize = FontSize;
		LineCursor.m_LineWidth = LineWidth;

		// Message is from valid player
		if(Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
		{
			LineCursor.m_X += RealMsgPaddingTee;

			if(Line.m_Friend && g_Config.m_ClMessageFriend)
			{
				TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageFriendColor)).WithAlpha(1.0f));
				TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, "♥ ");
			}
		}

		// render name
		ColorRGBA NameColor;
		if(Line.m_CustomColor)
			NameColor = *Line.m_CustomColor;
		else if(Line.m_ClientId == SERVER_MSG)
			NameColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageSystemColor));
		else if(Line.m_ClientId == CLIENT_MSG)
			NameColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageClientColor));
		else if(Line.m_ClientId >= 0 && g_Config.m_TcWarList && g_Config.m_TcWarListChat && GameClient()->m_WarList.GetAnyWar(Line.m_ClientId)) // TClient
			NameColor = GameClient()->m_WarList.GetPriorityColor(Line.m_ClientId);
		else if(Line.m_Team)
			NameColor = CalculateNameColor(ColorHSLA(g_Config.m_ClMessageTeamColor));
		else if(Line.m_NameColor == TEAM_RED)
			NameColor = ColorRGBA(1.0f, 0.5f, 0.5f, 1.0f);
		else if(Line.m_NameColor == TEAM_BLUE)
			NameColor = ColorRGBA(0.7f, 0.7f, 1.0f, 1.0f);
		else if(Line.m_NameColor == TEAM_SPECTATORS)
			NameColor = ColorRGBA(0.75f, 0.5f, 0.75f, 1.0f);
		else if(Line.m_ClientId >= 0 && g_Config.m_ClChatTeamColors && GameClient()->m_Teams.Team(Line.m_ClientId))
			NameColor = GameClient()->GetDDTeamColor(GameClient()->m_Teams.Team(Line.m_ClientId), 0.75f);
		else
			NameColor = ColorRGBA(0.8f, 0.8f, 0.8f, 1.0f);

		TextRender()->TextColor(NameColor);
		TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, aClientId);
		TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, Line.m_aName);

		if(Line.m_TimesRepeated > 0)
		{
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.3f);
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, aCount);
		}

		if(Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
		{
			TextRender()->TextColor(NameColor);
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, ": ");
		}

		ColorRGBA Color;
		if(Line.m_CustomColor)
			Color = *Line.m_CustomColor;
		else if(Line.m_ClientId == SERVER_MSG)
			Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageSystemColor));
		else if(Line.m_ClientId == CLIENT_MSG)
			Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageClientColor));
		else if(Line.m_Highlighted)
			Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageHighlightColor));
		else if(Line.m_Team)
			Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageTeamColor));
		else // regular message
			Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageColor));
		TextRender()->TextColor(Color);

		CTextCursor AppendCursor = LineCursor;
		AppendCursor.m_LongestLineWidth = 0.0f;
		if(!IsScoreBoardOpen && !g_Config.m_ClChatOld)
		{
			AppendCursor.m_StartX = LineCursor.m_X;
			AppendCursor.m_LineWidth -= LineCursor.m_LongestLineWidth;
		}

		if(pTranslatedText)
		{
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &AppendCursor, pTranslatedText);
			if(pTranslatedLanguage)
			{
				ColorRGBA ColorLang = Color;
				ColorLang.r *= 0.8f;
				ColorLang.g *= 0.8f;
				ColorLang.b *= 0.8f;
				TextRender()->TextColor(ColorLang);
				TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &AppendCursor, " [");
				TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &AppendCursor, pTranslatedLanguage);
				TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &AppendCursor, "]");
			}
			ColorRGBA ColorSub = Color;
			ColorSub.r *= 0.7f;
			ColorSub.g *= 0.7f;
			ColorSub.b *= 0.7f;
			TextRender()->TextColor(ColorSub);
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &AppendCursor, "\n");
			AppendCursor.m_FontSize *= 0.8f;
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &AppendCursor, pText);
			AppendCursor.m_FontSize /= 0.8f;
			TextRender()->TextColor(Color);
		}
		else if(pTranslatedError)
		{
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &AppendCursor, pText);
			ColorRGBA ColorSub = Color;
			ColorSub.r = 0.7f;
			ColorSub.g = 0.6f;
			ColorSub.b = 0.6f;
			TextRender()->TextColor(ColorSub);
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &AppendCursor, "\n");
			AppendCursor.m_FontSize *= 0.8f;
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &AppendCursor, pTranslatedError);
			AppendCursor.m_FontSize /= 0.8f;
			TextRender()->TextColor(Color);
		}
		else
		{
			ColoredParts.AddSplitsToCursor(AppendCursor);
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &AppendCursor, pText);
			AppendCursor.m_vColorSplits.clear();
		}

		if(!g_Config.m_ClChatOld && (Line.m_aText[0] != '\0' || Line.m_aName[0] != '\0'))
		{
			float FullWidth = RealMsgPaddingX * 1.5f;
			if(!IsScoreBoardOpen && !g_Config.m_ClChatOld)
			{
				FullWidth += LineCursor.m_LongestLineWidth + AppendCursor.m_LongestLineWidth;
			}
			else
			{
				FullWidth += std::max(LineCursor.m_LongestLineWidth, AppendCursor.m_LongestLineWidth);
			}
			Graphics()->SetColor(1, 1, 1, 1);
			Line.m_QuadContainerIndex = Graphics()->CreateRectQuadContainer(Begin, LineY, FullWidth, Line.m_aYOffset[OffsetType], MessageRounding(), IGraphics::CORNER_ALL);
		}

		TextRender()->SetRenderFlags(CurRenderFlags);
		if(Line.m_TextContainerIndex.Valid())
			TextRender()->UploadTextContainer(Line.m_TextContainerIndex);
	}

	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

void CChat::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	// bestclient
	GameClient()->m_ChatMedia.Update(*this);
	// bestclient

	// send pending chat messages
	if(m_PendingChatCounter > 0 && m_LastChatSend + time_freq() < time())
	{
		CHistoryEntry *pEntry = m_History.Last();
		for(int i = m_PendingChatCounter - 1; pEntry; --i, pEntry = m_History.Prev(pEntry))
		{
			if(i == 0)
			{
				SendChat(pEntry->m_Team, pEntry->m_aText);
				break;
			}
		}
		--m_PendingChatCounter;
	}

	const float Height = 300.0f;
	const float Width = Height * Graphics()->ScreenAspect();
	Graphics()->MapScreenToSize(Width, Height);
	m_TranslateButtonRectValid = false; // bestclient

	// bestclient
	if(!HudLayout::IsEnabled(HudLayout::MODULE_CHAT) && m_Mode == MODE_NONE)
		return;
	const auto ChatLayout = HudLayout::Get(HudLayout::MODULE_CHAT, Width, Height);
	const float LayoutScale = std::clamp(ChatLayout.m_Scale / 100.0f, 0.25f, 3.0f);
	float x = ChatLayout.m_X;
	float y = ChatLayout.m_Y;
	const float ModuleAlpha = HudLayout::AlphaFactor(HudLayout::MODULE_CHAT);
	const bool EditorPreview = GameClient()->m_HudEditor.IsActive();
	// bestclient

	float ScaledFontSize = FontSize() * (8.0f / 6.0f);
	// bestclient
	const float ChatOpenOffsetY = GameClient()->m_BcUiAnimations.ChatOpenOffsetY(Height, m_Mode != MODE_NONE);
	// bestclient
	if(m_Mode != MODE_NONE)
	{
		// render chat input
		CTextCursor InputCursor;
		// bestclient
		InputCursor.SetPosition(vec2(x, y + ChatOpenOffsetY));
		// bestclient
		InputCursor.m_FontSize = ScaledFontSize;
		// bestclient
		InputCursor.m_LineWidth = ChatWidth() - 190.0f * LayoutScale;
		InputCursor.m_LineWidth = std::max(InputCursor.m_LineWidth, 190.0f * LayoutScale);
		// bestclient

		if(m_Mode == MODE_ALL)
			TextRender()->TextEx(&InputCursor, Localize("All"));
		else if(m_Mode == MODE_TEAM)
			TextRender()->TextEx(&InputCursor, Localize("Team"));
		else
			TextRender()->TextEx(&InputCursor, Localize("Chat"));

		TextRender()->TextEx(&InputCursor, ": ");

		const float MessageMaxWidth = InputCursor.m_LineWidth - (InputCursor.m_X - InputCursor.m_StartX);
		const CUIRect ClippingRect = {InputCursor.m_X, InputCursor.m_Y, MessageMaxWidth, 2.25f * InputCursor.m_FontSize};
		const float XScale = Graphics()->ScreenWidth() / Width;
		const float YScale = Graphics()->ScreenHeight() / Height;
		// bestclient
		const CUIRect ChatInputClipRect = GameClient()->m_BcUiAnimations.ChatInputClipRect(ClippingRect, Width);
		Graphics()->ClipEnable((int)(ChatInputClipRect.x * XScale), (int)(ChatInputClipRect.y * YScale), (int)(ChatInputClipRect.w * XScale), (int)(ChatInputClipRect.h * YScale));
		// bestclient

		float ScrollOffset = m_Input.GetScrollOffset();
		float ScrollOffsetChange = m_Input.GetScrollOffsetChange();

		m_Input.Activate(EInputPriority::CHAT); // Ensure that the input is active
		const CUIRect InputCursorRect = {InputCursor.m_X, InputCursor.m_Y - ScrollOffset, 0.0f, 0.0f};
		const bool WasChanged = m_Input.WasChanged();
		const bool WasCursorChanged = m_Input.WasCursorChanged();
		const bool Changed = WasChanged || WasCursorChanged;
		// bestclient
		const STextBoundingBox BoundingBox = GameClient()->m_BcUiAnimations.RenderChatTypingInput(m_Input, InputCursorRect, InputCursor.m_FontSize, MessageMaxWidth, Changed);
		// bestclient

		Graphics()->ClipDisable();

		// Scroll up or down to keep the caret inside the clipping rect
		const float CaretPositionY = m_Input.GetCaretPosition().y - ScrollOffsetChange;
		if(CaretPositionY < ClippingRect.y)
			ScrollOffsetChange -= ClippingRect.y - CaretPositionY;
		else if(CaretPositionY + InputCursor.m_FontSize > ClippingRect.y + ClippingRect.h)
			ScrollOffsetChange += CaretPositionY + InputCursor.m_FontSize - (ClippingRect.y + ClippingRect.h);

		Ui()->DoSmoothScrollLogic(&ScrollOffset, &ScrollOffsetChange, ClippingRect.h, BoundingBox.m_H);

		m_Input.SetScrollOffset(ScrollOffset);
		m_Input.SetScrollOffsetChange(ScrollOffsetChange);

		// Autocompletion hint
		if(m_Input.GetString()[0] == '/' && m_Input.GetString()[1] != '\0' && !m_vServerCommands.empty())
		{
			for(const auto &Command : m_vServerCommands)
			{
				if(str_startswith_nocase(Command.m_aName, m_Input.GetString() + 1))
				{
					InputCursor.m_X = InputCursor.m_X + TextRender()->TextWidth(InputCursor.m_FontSize, m_Input.GetString(), -1, InputCursor.m_LineWidth);
					InputCursor.m_Y = m_Input.GetCaretPosition().y;
					TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.5f);
					TextRender()->TextEx(&InputCursor, Command.m_aName + str_length(m_Input.GetString() + 1));
					TextRender()->TextColor(TextRender()->DefaultTextColor());
					break;
				}
			}
		}
		// bestclient
		else if(m_Input.GetString()[0] == '!' && m_Input.GetString()[1] != '\0')
		{
			InputCursor.m_X = InputCursor.m_X + TextRender()->TextWidth(InputCursor.m_FontSize, m_Input.GetString(), -1, InputCursor.m_LineWidth);
			InputCursor.m_Y = m_Input.GetCaretPosition().y;
			CChatQoL::TryRenderBangCommandHint(GameClient(), m_Input.GetString(), &InputCursor, TextRender());
		}
		// bestclient

		// bestclient
		const float TranslateButtonSize = std::max(24.0f, InputCursor.m_FontSize * 1.75f);
		const float TranslateButtonGap = 4.0f;
		const float UiScale = Ui()->Screen()->h / Height;
		m_TranslateButtonRect = {
			ClippingRect.x + ClippingRect.w + TranslateButtonGap,
			y,
			TranslateButtonSize,
			InputCursor.m_FontSize};
		m_TranslateButtonUiRect = {
			m_TranslateButtonRect.x * UiScale,
			m_TranslateButtonRect.y * UiScale,
			m_TranslateButtonRect.w * UiScale,
			m_TranslateButtonRect.h * UiScale};
		m_TranslateButtonRectValid = true;
		Ui()->MapScreen();
		RenderTranslateSettingsButton(m_TranslateButtonUiRect);
		Graphics()->MapScreenToSize(Width, Height);
		// bestclient
	}

	// bestclient
#if defined(CONF_VIDEORECORDER)
	if(!EditorPreview && !((g_Config.m_ClShowChat && !IVideo::Current()) || (g_Config.m_ClVideoShowChat && IVideo::Current())))
#else
	if(!EditorPreview && !g_Config.m_ClShowChat)
#endif
		return;
	// bestclient

	// bestclient
	if(!EditorPreview && g_Config.m_ClFocusMode && g_Config.m_ClFocusModeHideChat)
		return;
	// bestclient

	y -= ScaledFontSize;

	// bestclient
	bool IsScoreBoardOpen = GameClient()->m_Scoreboard.IsShown() && (Graphics()->ScreenAspect() > 1.7f);
	const bool ShowLargeArea = EditorPreview || m_Show || (m_Mode != MODE_NONE && g_Config.m_ClShowChat == 1) || g_Config.m_ClShowChat == 2;
	int64_t Now = time();
	float HeightLimit;
	if(IsScoreBoardOpen)
		HeightLimit = y - 93.0f * LayoutScale;
	else if(ShowLargeArea)
		HeightLimit = y - 223.0f * LayoutScale;
	else if(GameClient()->m_ChatMedia.ShouldExpandCompactAreaForMedia(*this, IsScoreBoardOpen, ShowLargeArea))
		HeightLimit = GameClient()->m_ChatMedia.CompactMediaHeightLimit(y);
	else
		HeightLimit = y - 73.0f * LayoutScale;

	// bestclient
	const bool ChatInteractionActive = (m_Mode != MODE_NONE || m_Show) && !Ui()->IsPopupOpen();
	// bestclient
	int OffsetType = IsScoreBoardOpen ? 1 : 0;
	float RealMsgPaddingX = MessagePaddingX();
	float RealMsgPaddingY = MessagePaddingY();
	float RealMsgPaddingTee = MessageTeeSize() + MESSAGE_TEE_PADDING_RIGHT;
	if(g_Config.m_ClChatOld)
	{
		RealMsgPaddingX = 0;
		RealMsgPaddingY = 0;
		RealMsgPaddingTee = 0;
	}
	const float MeasureFontSize = FontSize();
	const float LineWidth = (IsScoreBoardOpen ? std::max(85.0f * LayoutScale, MeasureFontSize * 85.0f / 6.0f) : ChatWidth()) - (RealMsgPaddingX * 1.5f) - RealMsgPaddingTee;
	const float TextBegin = x + RealMsgPaddingX / 2.0f;
	const bool ModeActive = m_Mode != MODE_NONE;
	const bool LayoutEnabled = HudLayout::IsEnabled(HudLayout::MODULE_CHAT);
	const bool LayoutChanged = ChatLayout.m_X != m_PrevHudLayoutX || ChatLayout.m_Y != m_PrevHudLayoutY || ChatLayout.m_Scale != m_PrevHudLayoutScale || LayoutEnabled != m_PrevHudLayoutEnabled;
	const bool ForceRecreate = IsScoreBoardOpen != m_PrevScoreBoardShowed || ShowLargeArea != m_PrevShowChat || ModeActive != m_PrevModeActive || LayoutChanged;
	CChatScroll::MeasureHeights(*this, TextBegin, MeasureFontSize, LineWidth, RealMsgPaddingY, RealMsgPaddingTee, OffsetType, IsScoreBoardOpen, ForceRecreate);
	CChatScroll::UpdateScrollbar(*this, x, y, Width, Height, HeightLimit, OffsetType, MeasureFontSize, RealMsgPaddingY, ChatInteractionActive);
	// bestclient

	OnPrepareLines(y);

	// bestclient
	CChatScroll::SActionFrame ActionFrame = CChatScroll::BeginActions(*this);
	float RenderY = y + m_BacklogScrollOffset;
	bool AllowTopClip = true;
	// bestclient
	for(int i = 0; i < MAX_LINES; i++) // bestclient
	{
		CLine &Line = m_aLines[((m_CurrentLine - i) + MAX_LINES) % MAX_LINES];
		if(!Line.m_Initialized)
			break;
		if(Now > Line.m_Time + 16 * time_freq() && !m_PrevShowChat)
			break;

		// bestclient
		const float LineH = Line.m_aYOffset[OffsetType] >= 0.0f ? Line.m_aYOffset[OffsetType] : MeasureFontSize + RealMsgPaddingY;
		RenderY -= LineH;

		if(RenderY < HeightLimit && !AllowTopClip)
			break;
		AllowTopClip = false;

		float Blend = Now > Line.m_Time + 14 * time_freq() && !m_PrevShowChat ? 1.0f - (Now - Line.m_Time - 14 * time_freq()) / (2.0f * time_freq()) : 1.0f;
		const float LineRenderY = RenderY + GameClient()->m_BcUiAnimations.ChatMessageYOffset(Line.m_Time, Now);
		const float LineW = ChatWidth();
		const int ArrayIndex = ((m_CurrentLine - i) + MAX_LINES) % MAX_LINES;
		CChatScroll::NoteLineHover(ActionFrame, *this, ArrayIndex, x, LineW, LineRenderY, LineH, ChatInteractionActive);
		// bestclient

		if(!g_Config.m_ClChatOld)
		{
			Graphics()->TextureClear();
			if(Line.m_QuadContainerIndex != -1)
			{
				Graphics()->SetColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClChatBackgroundColor, true)).WithMultipliedAlpha(Blend * ModuleAlpha));
				Graphics()->RenderQuadContainerEx(Line.m_QuadContainerIndex, 0, -1, 0, ((LineRenderY + RealMsgPaddingY / 2.0f) - Line.m_TextYOffset));
			}
		}

		// bestclient
		if(CChatScroll::ShouldHighlight(ActionFrame, *this, ArrayIndex))
			CChatScroll::DrawHighlight(*this, x, LineRenderY, LineW, LineH, Blend * ModuleAlpha);
		// bestclient

		if(Line.m_TextContainerIndex.Valid())
		{
			if(!g_Config.m_ClChatOld && Line.m_pManagedTeeRenderInfo != nullptr)
			{
				CTeeRenderInfo &TeeRenderInfo = Line.m_pManagedTeeRenderInfo->TeeRenderInfo();
				const int TeeSize = MessageTeeSize();
				TeeRenderInfo.m_Size = TeeSize;

				float RowHeight = FontSize() + RealMsgPaddingY;
				float OffsetTeeY = TeeSize / 2.0f;
				float FullHeightMinusTee = RowHeight - TeeSize;

				const CAnimState *pIdleState = CAnimState::GetIdle();
				vec2 OffsetToMid;
				CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeRenderInfo, OffsetToMid);
				vec2 TeeRenderPos(x + (RealMsgPaddingX + TeeSize) / 2.0f, LineRenderY + OffsetTeeY + FullHeightMinusTee / 2.0f + OffsetToMid.y);
				RenderTools()->RenderTee(pIdleState, &TeeRenderInfo, EMOTE_NORMAL, vec2(1, 0.1f), TeeRenderPos, Blend * ModuleAlpha);
			}

			const ColorRGBA TextColor = TextRender()->DefaultTextColor().WithMultipliedAlpha(Blend * ModuleAlpha);
			const ColorRGBA TextOutlineColor = TextRender()->DefaultTextOutlineColor().WithMultipliedAlpha(Blend * ModuleAlpha);
			TextRender()->RenderTextContainer(Line.m_TextContainerIndex, TextColor, TextOutlineColor, 0, (LineRenderY + RealMsgPaddingY / 2.0f) - Line.m_TextYOffset);
		}

		// bestclient
		{
			const float PreviewX = x + RealMsgPaddingX / 2.0f;
			const float PreviewY = LineRenderY + RealMsgPaddingY / 2.0f + Line.m_Media.m_aTextHeight[OffsetType] + FontSize() * 0.4f;
			GameClient()->m_ChatMedia.RenderLinePreview(Line.m_Media, PreviewX, PreviewY, Blend, FontSize(), OffsetType);
		}
		// bestclient
	}

	// bestclient
	CChatScroll::EndActions(*this, ActionFrame, ModuleAlpha);
	GameClient()->m_ChatMedia.RenderFullscreen(*this, Width, Height);

	// bestclient
	if(m_Mode != MODE_NONE && Ui()->IsPopupOpen(&m_TranslateSettingsPopupId))
	{
		Ui()->MapScreen();
		Ui()->RenderPopupMenus();
		Graphics()->MapScreenToSize(Width, Height);
	}
	// bestclient

	if(m_Mode != MODE_NONE)
	{
		const vec2 WindowSize(std::max(1.0f, (float)Graphics()->WindowWidth()), std::max(1.0f, (float)Graphics()->WindowHeight()));
		const vec2 UiToChatScale(Width / Ui()->Screen()->w, Height / Ui()->Screen()->h);
		const vec2 UiMousePos = Ui()->UpdatedMousePos() * vec2(Ui()->Screen()->w, Ui()->Screen()->h) / WindowSize;
		RenderTools()->RenderCursor(UiMousePos * UiToChatScale, 12.0f);
	}
	// bestclient
}

// bestclient
CUIRect CChat::GetHudRect(float HudWidth, float HudHeight, bool ForcePreview) const
{
	if(!ForcePreview && !HudLayout::IsEnabled(HudLayout::MODULE_CHAT))
		return {0.0f, 0.0f, 0.0f, 0.0f};

	const auto Layout = HudLayout::Get(HudLayout::MODULE_CHAT, HudWidth, HudHeight);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	const bool IsScoreBoardOpen = GameClient()->m_Scoreboard.IsShown() && (Graphics()->ScreenAspect() > 1.7f);
	const bool ShowLargeArea = ForcePreview || m_Show || (m_Mode != MODE_NONE && g_Config.m_ClShowChat == 1) || g_Config.m_ClShowChat == 2;
	const float VisibleHeight = IsScoreBoardOpen ? 93.0f * Scale : (ShowLargeArea ? 223.0f * Scale : 73.0f * Scale);
	float ExtraTop = 0.0f;
	float ExtraBottom = 0.0f;
	float VisibleWidth = ChatWidth();

	if(ForcePreview || m_Mode != MODE_NONE)
	{
		const float ScaledFontSize = FontSize() * (8.0f / 6.0f);
		// bestclient
		const float TranslateButtonWidth = std::max(24.0f, ScaledFontSize * 1.75f) + 4.0f;
		VisibleWidth = std::max(VisibleWidth, std::max(ChatWidth() - 190.0f * Scale, 190.0f * Scale) + TranslateButtonWidth);
		ExtraTop = ScaledFontSize;
		ExtraBottom = std::max(2.25f * ScaledFontSize, std::max(ScaledFontSize + 4.0f, 16.0f));
		// bestclient
	}

	CUIRect Rect = {Layout.m_X, Layout.m_Y - VisibleHeight - ExtraTop, VisibleWidth, VisibleHeight + ExtraTop + ExtraBottom};
	Rect.x = std::clamp(Rect.x, 0.0f, std::max(0.0f, HudWidth - Rect.w));
	Rect.y = std::clamp(Rect.y, 0.0f, std::max(0.0f, HudHeight - Rect.h));
	return Rect;
}
// bestclient

void CChat::EnsureCoherentFontSize() const
{
	// Adjust font size based on width
	if(g_Config.m_ClChatWidth / (float)g_Config.m_ClChatFontSize >= CHAT_FONTSIZE_WIDTH_RATIO)
		return;

	// We want to keep a ration between font size and font width so that we don't have a weird rendering
	g_Config.m_ClChatFontSize = g_Config.m_ClChatWidth / CHAT_FONTSIZE_WIDTH_RATIO;
}

void CChat::EnsureCoherentWidth() const
{
	// Adjust width based on font size
	if(g_Config.m_ClChatWidth / (float)g_Config.m_ClChatFontSize >= CHAT_FONTSIZE_WIDTH_RATIO)
		return;

	// We want to keep a ration between font size and font width so that we don't have a weird rendering
	g_Config.m_ClChatWidth = CHAT_FONTSIZE_WIDTH_RATIO * g_Config.m_ClChatFontSize;
}

// ----- send functions -----

void CChat::SendChat(int Team, const char *pLine)
{
	// don't send empty messages
	if(*str_utf8_skip_whitespaces(pLine) == '\0')
		return;

	// bestclient
	if(GameClient()->m_FastPractice.ConsumePracticeChatCommand(Team, pLine))
		return;
	if(GameClient()->m_VoiceChat.TryHandleChatCommand(pLine))
		return;
	// bestclient

	m_LastChatSend = time();

	if(GameClient()->Client()->IsSixup())
	{
		protocol7::CNetMsg_Cl_Say Msg7;
		Msg7.m_Mode = Team == 1 ? protocol7::CHAT_TEAM : protocol7::CHAT_ALL;
		Msg7.m_Target = -1;
		Msg7.m_pMessage = pLine;
		Client()->SendPackMsgActive(&Msg7, MSGFLAG_VITAL, true);
		return;
	}

	// send chat message
	CNetMsg_Cl_Say Msg;
	Msg.m_Team = Team;
	Msg.m_pMessage = pLine;
	Client()->SendPackMsgActive(&Msg, MSGFLAG_VITAL);
}

void CChat::SendChatQueued(const char *pLine)
{
	if(!pLine || str_length(pLine) < 1)
		return;

	// bestclient
	char aConvertedLine[MAX_CHAT_LENGTH];
	if(CChatQoL::TryConvertWrongLayoutSlashCommand(*this, pLine, aConvertedLine, sizeof(aConvertedLine)))
		pLine = aConvertedLine;
	// bestclient

	bool AddEntry = false;

	if(m_LastChatSend + time_freq() < time())
	{
		SendChat(m_Mode == MODE_ALL ? 0 : 1, pLine);
		AddEntry = true;
	}
	else if(m_PendingChatCounter < 3)
	{
		++m_PendingChatCounter;
		AddEntry = true;
	}

	if(AddEntry)
	{
		const int Length = str_length(pLine);
		CHistoryEntry *pEntry = m_History.Allocate(sizeof(CHistoryEntry) + Length);
		pEntry->m_Team = m_Mode == MODE_ALL ? 0 : 1;
		str_copy(pEntry->m_aText, pLine, Length + 1);
	}
}

// bestclient
void CChat::SendTranslatedChatQueued(int Team, const char *pLine)
{
	if(!pLine || *str_utf8_skip_whitespaces(pLine) == '\0')
		return;

	bool AddEntry = false;
	if(m_LastChatSend + time_freq() < time())
	{
		SendChat(Team, pLine);
		AddEntry = true;
	}
	else if(m_PendingChatCounter < 3)
	{
		++m_PendingChatCounter;
		AddEntry = true;
	}

	if(AddEntry)
	{
		const int Length = str_length(pLine);
		CHistoryEntry *pEntry = m_History.Allocate(sizeof(CHistoryEntry) + Length);
		pEntry->m_Team = Team;
		str_copy(pEntry->m_aText, pLine, Length + 1);
	}
}
// bestclient
