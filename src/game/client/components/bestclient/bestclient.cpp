/* Copyright © 2026 BestProject Team */
#include "bestclient.h"
#include "version.h"

#include <base/color.h>
#include <base/log.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/client/enums.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/storage.h>

#include <game/client/components/hud_layout.h>
#include <game/client/components/sounds.h>
#include <game/client/gameclient.h>
#include <game/localization.h>
#include <game/version.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

static constexpr const char *BestClient_INFO_URL = "https://api.github.com/repos/RoflikBEST/bestdownload/releases?per_page=10";
static constexpr const char *BestClient_STREAMER_WORDS_FILE = "nwords.txt";
static const char *const gs_apDefaultStreamerWords[] = {
	"пидор",
	"чурка",
	"петух",
	"петушок",
	"петушня",
	"пидорасы",
	"пидрилы",
	"негры",
	"нигеры",
	"негор",
	"негар",
	"nigger",
	"niggers",
	"nigga",
	"niga",
	"sniggers",
	"niggerz",
	"пидорасня",
	"пидорасина",
	"kys",
	"kill your self",
	"suicide",
	"суицид",
	"суициднись",
	"убейся",
	"вскройся",
	"нiгер",
	"HuGGER",
};

static bool StreamerWordExists(const std::vector<std::string> &vWords, const char *pWord)
{
	return std::any_of(vWords.begin(), vWords.end(), [pWord](const std::string &Word) {
		return str_utf8_comp_nocase(Word.c_str(), pWord) == 0;
	});
}

static void AppendSanitizedChunk(char **ppDst, char *pDstEnd, const char *pChunkStart, const char *pChunkEnd)
{
	while(pChunkStart < pChunkEnd && *ppDst < pDstEnd)
	{
		*(*ppDst)++ = *pChunkStart++;
	}
}

static void NormalizeBestClientVersion(const char *pVersion, char *pBuf, int BufSize)
{
	if(BufSize <= 0)
		return;

	if(!pVersion)
	{
		pBuf[0] = '\0';
		return;
	}

	while(*pVersion != '\0' && std::isspace(static_cast<unsigned char>(*pVersion)))
		++pVersion;

	if((pVersion[0] == 'v' || pVersion[0] == 'V') && std::isdigit(static_cast<unsigned char>(pVersion[1])))
		++pVersion;

	str_copy(pBuf, pVersion, BufSize);
}

static std::vector<int> ExtractBestClientVersionNumbers(const char *pVersion)
{
	std::vector<int> vNumbers;
	if(!pVersion)
		return vNumbers;

	int Current = -1;
	for(const unsigned char *p = reinterpret_cast<const unsigned char *>(pVersion); *p != '\0'; ++p)
	{
		if(std::isdigit(*p))
		{
			if(Current < 0)
				Current = 0;
			Current = Current * 10 + (*p - '0');
		}
		else if(Current >= 0)
		{
			vNumbers.push_back(Current);
			Current = -1;
		}
	}

	if(Current >= 0)
		vNumbers.push_back(Current);

	return vNumbers;
}

static int CompareBestClientVersions(const char *pLeft, const char *pRight)
{
	char aLeft[64];
	char aRight[64];
	NormalizeBestClientVersion(pLeft, aLeft, sizeof(aLeft));
	NormalizeBestClientVersion(pRight, aRight, sizeof(aRight));

	const std::vector<int> vLeft = ExtractBestClientVersionNumbers(aLeft);
	const std::vector<int> vRight = ExtractBestClientVersionNumbers(aRight);
	const size_t Num = maximum(vLeft.size(), vRight.size());
	for(size_t i = 0; i < Num; ++i)
	{
		const int Left = i < vLeft.size() ? vLeft[i] : 0;
		const int Right = i < vRight.size() ? vRight[i] : 0;
		if(Left < Right)
			return -1;
		if(Left > Right)
			return 1;
	}

	return str_comp_nocase(aLeft, aRight);
}

static void BuildBestClientInfoUrl(char *pBuf, int BufSize)
{
	str_format(pBuf, BufSize, "%s&t=%lld", BestClient_INFO_URL, (long long)time_timestamp());
}

bool CBestClient::IsStreamerModeEnabled() const
{
	return g_Config.m_ClStreamerMode != 0;
}

bool CBestClient::HasStreamerFlag(int Flag) const
{
	return IsStreamerModeEnabled() && (g_Config.m_BcStreamerFlags & Flag) != 0;
}

bool CBestClient::IsLocalClientId(int ClientId) const
{
	for(int LocalId : GameClient()->m_aLocalIds)
	{
		if(LocalId == ClientId)
			return true;
	}

	return Client()->State() == IClient::STATE_DEMOPLAYBACK && GameClient()->m_Snap.m_LocalClientId == ClientId;
}

bool CBestClient::ShouldHidePlayerName(int ClientId, bool InScoreboard) const
{
	if(!IsStreamerModeEnabled() || ClientId < 0)
		return false;
	if(InScoreboard && HasStreamerFlag(STREAMER_HIDE_TAB_NAMES))
		return true;
	if(IsLocalClientId(ClientId))
		return HasStreamerFlag(STREAMER_HIDE_OWN_NAME);
	return HasStreamerFlag(STREAMER_HIDE_OTHER_NAMES);
}

const char *CBestClient::MaskServerAddress(const char *pAddress, char *pOutput, size_t OutputSize) const
{
	if(HasStreamerFlag(STREAMER_HIDE_SERVER_IP))
	{
		str_copy(pOutput, Localize("Hidden"), OutputSize);
		return pOutput;
	}

	str_copy(pOutput, pAddress != nullptr ? pAddress : "", OutputSize);
	return pOutput;
}

void CBestClient::EnsureStreamerWordsLoaded()
{
	if(m_StreamerWordsLoaded)
		return;

	m_StreamerWordsLoaded = true;
	m_vStreamerBlockedWords.clear();

	char *pFileData = Storage()->ReadFileStr(BestClient_STREAMER_WORDS_FILE, IStorage::TYPE_SAVE);
	if(pFileData != nullptr)
	{
		const char *pCursor = pFileData;
		while(*pCursor != '\0')
		{
			const char *pLineEnd = pCursor;
			while(*pLineEnd != '\0' && *pLineEnd != '\n' && *pLineEnd != '\r')
				++pLineEnd;

			char aWord[128];
			str_truncate(aWord, sizeof(aWord), pCursor, pLineEnd - pCursor);
			str_utf8_trim_right(aWord);
			const char *pTrimmedWord = str_utf8_skip_whitespaces(aWord);
			if(*pTrimmedWord != '\0' && !StreamerWordExists(m_vStreamerBlockedWords, pTrimmedWord))
				m_vStreamerBlockedWords.emplace_back(pTrimmedWord);

			pCursor = pLineEnd;
			while(*pCursor == '\n' || *pCursor == '\r')
				++pCursor;
		}

		free(pFileData);
	}

	if(m_vStreamerBlockedWords.empty())
	{
		for(const char *pWord : gs_apDefaultStreamerWords)
			m_vStreamerBlockedWords.emplace_back(pWord);
		SaveStreamerWords();
	}
}

void CBestClient::SaveStreamerWords() const
{
	IOHANDLE File = Storage()->OpenFile(BestClient_STREAMER_WORDS_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return;

	for(const std::string &Word : m_vStreamerBlockedWords)
	{
		io_write(File, Word.c_str(), str_length(Word.c_str()));
		io_write_newline(File);
	}

	io_close(File);
}

void CBestClient::AddStreamerBlockedWord(const char *pWord)
{
	EnsureStreamerWordsLoaded();

	char aWord[128];
	str_copy(aWord, pWord != nullptr ? pWord : "", sizeof(aWord));
	str_utf8_trim_right(aWord);
	const char *pTrimmedWord = str_utf8_skip_whitespaces(aWord);
	if(*pTrimmedWord == '\0' || StreamerWordExists(m_vStreamerBlockedWords, pTrimmedWord))
		return;

	m_vStreamerBlockedWords.emplace_back(pTrimmedWord);
	SaveStreamerWords();
}

void CBestClient::RemoveStreamerBlockedWord(int Index)
{
	EnsureStreamerWordsLoaded();

	if(Index < 0 || Index >= (int)m_vStreamerBlockedWords.size())
		return;

	m_vStreamerBlockedWords.erase(m_vStreamerBlockedWords.begin() + Index);
	SaveStreamerWords();
}

const std::vector<std::string> &CBestClient::StreamerBlockedWords()
{
	EnsureStreamerWordsLoaded();
	return m_vStreamerBlockedWords;
}

int CBestClient::StreamerBlockedWordCount()
{
	EnsureStreamerWordsLoaded();
	return (int)m_vStreamerBlockedWords.size();
}

void CBestClient::SanitizeText(const char *pInput, char *pOutput, size_t OutputSize)
{
	EnsureStreamerWordsLoaded();

	if(!IsStreamerModeEnabled() || pInput == nullptr || OutputSize == 0)
	{
		if(OutputSize > 0)
			str_copy(pOutput, pInput != nullptr ? pInput : "", OutputSize);
		return;
	}

	char *pDst = pOutput;
	char *pDstEnd = pOutput + OutputSize - 1;
	const char *pCursor = pInput;

	while(*pCursor != '\0' && pDst < pDstEnd)
	{
		const char *pBestStart = nullptr;
		const char *pBestEnd = nullptr;
		for(const std::string &Word : m_vStreamerBlockedWords)
		{
			if(Word.empty())
				continue;

			const char *pMatchEnd = nullptr;
			const char *pFound = str_utf8_find_nocase(pCursor, Word.c_str(), &pMatchEnd);
			if(pFound != nullptr && (pBestStart == nullptr || pFound < pBestStart || (pFound == pBestStart && pMatchEnd > pBestEnd)))
			{
				pBestStart = pFound;
				pBestEnd = pMatchEnd;
			}
		}

		if(pBestStart == nullptr)
		{
			AppendSanitizedChunk(&pDst, pDstEnd, pCursor, pCursor + str_length(pCursor));
			break;
		}

		AppendSanitizedChunk(&pDst, pDstEnd, pCursor, pBestStart);

		const char *pWalk = pBestStart;
		while(pWalk < pBestEnd && pDst < pDstEnd)
		{
			str_utf8_decode(&pWalk);
			*pDst++ = '*';
		}

		pCursor = pBestEnd;
	}

	*pDst = '\0';
}

void CBestClient::SanitizePlayerName(const char *pInput, char *pOutput, size_t OutputSize, int ClientId, bool InScoreboard)
{
	if(OutputSize == 0)
		return;

	if(ShouldHidePlayerName(ClientId, InScoreboard))
	{
		str_copy(pOutput, Localize("Hidden"), OutputSize);
		return;
	}

	SanitizeText(pInput, pOutput, OutputSize);
}

static const char *FindBestClientReleaseVersion(const json_value *pJson)
{
	if(!pJson)
		return nullptr;

	if(pJson->type == json_object)
	{
		const char *pVersion = json_string_get(json_object_get(pJson, "tag_name"));
		if(!pVersion)
			pVersion = json_string_get(json_object_get(pJson, "name"));
		return pVersion;
	}

	if(pJson->type == json_array)
	{
		const char *pBestVersion = nullptr;
		for(int i = 0; i < json_array_length(pJson); ++i)
		{
			const json_value *pRelease = json_array_get(pJson, i);
			if(!pRelease || pRelease->type != json_object)
				continue;
			const char *pVersion = json_string_get(json_object_get(pRelease, "tag_name"));
			if(!pVersion)
				pVersion = json_string_get(json_object_get(pRelease, "name"));
			if(!pVersion)
				continue;
			if(!pBestVersion || CompareBestClientVersions(pVersion, pBestVersion) > 0)
				pBestVersion = pVersion;
		}
		return pBestVersion;
	}

	return nullptr;
}

static constexpr int s_HookComboBaseTextCount = 15;
static constexpr int s_HookComboVariantLimit = 100;
static constexpr int s_HookComboSoundCount = 7;
static constexpr int s_HookComboBrilliantSoundIndex = 5; // 0-based => sound #6
static constexpr int s_HookComboModeHook = 0;
static constexpr int s_HookComboModeHammer = 1;
static constexpr int s_HookComboModeHookAndHammer = 2;

static constexpr const char *const s_apHookComboTexts[s_HookComboBaseTextCount] = {
	"cool",
	"nice",
	"great",
	"awesome",
	"excellent",
	"amazing",
	"fantastic",
	"incredible",
	"spectacular",
	"legendary",
	"mythic",
	"unstoppable",
	"dominant",
	"masterful",
	"BRILLIANT"};

static const ColorRGBA s_aHookComboColors[s_HookComboBaseTextCount] = {
	ColorRGBA(0.36f, 1.0f, 0.50f, 1.0f),
	ColorRGBA(0.28f, 0.78f, 1.0f, 1.0f),
	ColorRGBA(0.40f, 1.0f, 0.92f, 1.0f),
	ColorRGBA(1.0f, 0.75f, 0.26f, 1.0f),
	ColorRGBA(1.0f, 0.52f, 0.23f, 1.0f),
	ColorRGBA(1.0f, 0.40f, 0.70f, 1.0f),
	ColorRGBA(0.96f, 0.96f, 0.34f, 1.0f),
	ColorRGBA(0.65f, 0.90f, 1.0f, 1.0f),
	ColorRGBA(0.75f, 1.0f, 0.82f, 1.0f),
	ColorRGBA(1.0f, 0.66f, 0.38f, 1.0f),
	ColorRGBA(0.92f, 0.74f, 1.0f, 1.0f),
	ColorRGBA(1.0f, 0.58f, 0.58f, 1.0f),
	ColorRGBA(1.0f, 0.85f, 0.48f, 1.0f),
	ColorRGBA(0.80f, 0.95f, 0.50f, 1.0f),
	ColorRGBA(1.0f, 0.97f, 0.35f, 1.0f)};

static void FormatHookComboText(int Sequence, char *pBuf, int BufSize)
{
	if(Sequence <= s_HookComboBaseTextCount)
	{
		str_copy(pBuf, s_apHookComboTexts[Sequence - 1], BufSize);
		return;
	}

	if(Sequence <= s_HookComboVariantLimit)
	{
		static constexpr const char *const s_apAdvancedTitles[] = {
			"brilliant",
			"godlike",
			"unreal",
			"mythic",
			"supreme",
			"transcendent",
			"unstoppable",
			"devastating",
			"apex",
			"ascendant"};
		static constexpr int s_AdvancedTitleCount = 10;
		const int Step = Sequence - s_HookComboBaseTextCount - 1;
		const int GroupSize = 9;
		const int Group = std::min(Step / GroupSize, s_AdvancedTitleCount - 1);
		str_format(pBuf, BufSize, "%s %d", s_apAdvancedTitles[Group], Sequence);
		return;
	}

	str_copy(pBuf, "BRILLIANT", BufSize);
}

CBestClient::CBestClient()
{
	OnReset();
}

void CBestClient::OnInit()
{
	LoadHookComboSounds();
	ResetHookComboState();
	FetchBestClientInfo();
}

void CBestClient::OnShutdown()
{
	ResetBestClientInfoTask();
	ResetHookComboState();
	UnloadHookComboSounds();
}

void CBestClient::OnReset()
{
	ResetHookComboState();
}

void CBestClient::OnStateChange(int NewState, int OldState)
{
	(void)NewState;
	(void)OldState;
	ResetHookComboState();
}

void CBestClient::OnRender()
{
	if(m_pBestClientInfoTask)
	{
		if(m_pBestClientInfoTask->State() == EHttpState::DONE)
		{
			FinishBestClientInfo();
			ResetBestClientInfoTask();
		}
		else if(m_pBestClientInfoTask->State() == EHttpState::ERROR || m_pBestClientInfoTask->State() == EHttpState::ABORTED)
		{
			ResetBestClientInfoTask();
		}
	}

	if(HasHookComboWork())
		UpdateHookCombo();
}

void CBestClient::LoadHookComboSounds(bool LogErrors)
{
	for(int &SoundId : m_aHookComboSoundIds)
		SoundId = -1;

	for(int i = 0; i < (int)m_aHookComboSoundIds.size(); ++i)
	{
		auto TryLoad = [this, i](const char *pPath, int StorageType) {
			if(m_aHookComboSoundIds[i] != -1)
				return;
			if(!Storage()->FileExists(pPath, StorageType))
				return;
			m_aHookComboSoundIds[i] = Sound()->LoadWV(pPath, StorageType);
		};

		char aPathWv[96];
		char aDataPathWv[128];
		char aParentRelativeDataPathWv[144];
		char aBinaryDataPathWv[IO_MAX_PATH_LENGTH];
		char aParentDataPathWv[IO_MAX_PATH_LENGTH];
		str_format(aPathWv, sizeof(aPathWv), "BestClient/combo/combo%d.wv", i + 1);
		str_format(aDataPathWv, sizeof(aDataPathWv), "data/BestClient/combo/combo%d.wv", i + 1);
		str_format(aParentRelativeDataPathWv, sizeof(aParentRelativeDataPathWv), "../%s", aDataPathWv);
		Storage()->GetBinaryPathAbsolute(aDataPathWv, aBinaryDataPathWv, sizeof(aBinaryDataPathWv));
		Storage()->GetBinaryPathAbsolute(aParentRelativeDataPathWv, aParentDataPathWv, sizeof(aParentDataPathWv));

		TryLoad(aPathWv, IStorage::TYPE_ALL);
		TryLoad(aDataPathWv, IStorage::TYPE_ALL);
		TryLoad(aBinaryDataPathWv, IStorage::TYPE_ABSOLUTE);
		TryLoad(aParentDataPathWv, IStorage::TYPE_ABSOLUTE);

		if(LogErrors && m_aHookComboSoundIds[i] == -1)
			log_warn("hook_combo", "Failed to load combo sound #%d (expected data/BestClient/combo/combo%d.wv)", i + 1, i + 1);
	}
}

void CBestClient::UnloadHookComboSounds()
{
	for(int &SoundId : m_aHookComboSoundIds)
	{
		if(SoundId != -1)
		{
			Sound()->UnloadSample(SoundId);
			SoundId = -1;
		}
	}
}

void CBestClient::ResetHookComboState()
{
	m_HookComboCounter = 0;
	m_HookComboLastHookTime = -1.0f;
	m_HookComboTrackedClientId = -1;
	m_HookComboLastHookedPlayer = -1;
	m_HookComboSoundErrorShown = false;
	m_vHookComboPopups.clear();
}

void CBestClient::TriggerHookComboStep()
{
	m_HookComboCounter++;

	SHookComboPopup Popup;
	Popup.m_Sequence = m_HookComboCounter;
	Popup.m_Age = 0.0f;
	m_vHookComboPopups.push_back(Popup);
	if(m_vHookComboPopups.size() > 16)
		m_vHookComboPopups.erase(m_vHookComboPopups.begin());

	if(!GameClient()->m_SuppressEvents && g_Config.m_SndEnable)
	{
		int SoundIndex = 0;
		if(m_HookComboCounter > s_HookComboVariantLimit)
			SoundIndex = s_HookComboBrilliantSoundIndex;
		else if(m_HookComboCounter <= s_HookComboSoundCount)
			SoundIndex = m_HookComboCounter - 1;
		else
			SoundIndex = (m_HookComboCounter - 1) % s_HookComboSoundCount;

		int SoundId = m_aHookComboSoundIds[SoundIndex];
		if(SoundId == -1)
		{
			// Retry at runtime, because startup path/audio init may differ from gameplay runtime.
			LoadHookComboSounds(false);
			SoundId = m_aHookComboSoundIds[SoundIndex];
		}
		if(SoundId != -1)
		{
			const float GameVol = (g_Config.m_SndGame && g_Config.m_SndGameVolume > 0) ? (float)g_Config.m_SndGameVolume : 0.0f;
			const float ChatVol = (g_Config.m_SndChat && g_Config.m_SndChatVolume > 0) ? (float)g_Config.m_SndChatVolume : 0.0f;
			const int Channel = GameVol >= ChatVol ? CSounds::CHN_GLOBAL : CSounds::CHN_GUI;
			const float ComboVol = std::clamp(g_Config.m_BcHookComboSoundVolume / 100.0f, 0.0f, 1.0f);
			if(ComboVol > 0.0f)
				Sound()->Play(Channel, SoundId, 0, ComboVol);
		}
		else if(!m_HookComboSoundErrorShown)
		{
			m_HookComboSoundErrorShown = true;
			GameClient()->Echo("[[red]] Hook combo sounds not found. Put files as data/BestClient/combo/combo1.wv ... combo7.wv");
		}
	}
}

void CBestClient::UpdateHookCombo()
{
	constexpr float PopupLifetime = 1.1f;
	const float FrameTime = Client()->RenderFrameTime();
	for(auto &Popup : m_vHookComboPopups)
		Popup.m_Age += FrameTime;
	m_vHookComboPopups.erase(std::remove_if(m_vHookComboPopups.begin(), m_vHookComboPopups.end(), [](const SHookComboPopup &Popup) {
		return Popup.m_Age >= PopupLifetime;
	}),
		m_vHookComboPopups.end());

	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		ResetHookComboState();
		return;
	}

	if(!g_Config.m_BcHookCombo)
	{
		ResetHookComboState();
		return;
	}

	if(GameClient()->m_Snap.m_SpecInfo.m_Active)
		return;

	const int ComboMode = std::clamp(g_Config.m_BcHookComboMode, s_HookComboModeHook, s_HookComboModeHookAndHammer);
	const bool HammerEventFrame = GameClient()->m_aPredictedHammerHitEvent[g_Config.m_ClDummy];
	if(!GameClient()->m_NewPredictedTick && !(HammerEventFrame && ComboMode != s_HookComboModeHook))
		return;

	int LocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	if(LocalId < 0 || LocalId >= MAX_CLIENTS)
		LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId < 0 || LocalId >= MAX_CLIENTS || !GameClient()->m_aClients[LocalId].m_Active)
		return;

	if(LocalId != m_HookComboTrackedClientId)
	{
		m_HookComboTrackedClientId = LocalId;
		m_HookComboLastHookedPlayer = -1;
	}

	const int HookedPlayer = GameClient()->m_aClients[LocalId].m_Predicted.HookedPlayer();
	const bool NewPlayerHook = HookedPlayer >= 0 && (m_HookComboLastHookedPlayer < 0 || HookedPlayer != m_HookComboLastHookedPlayer);
	m_HookComboLastHookedPlayer = HookedPlayer;

	const bool NewHammerAttack = GameClient()->m_aPredictedHammerHitEvent[g_Config.m_ClDummy];

	bool TriggerCombo = false;
	if(ComboMode == s_HookComboModeHook)
		TriggerCombo = NewPlayerHook;
	else if(ComboMode == s_HookComboModeHammer)
		TriggerCombo = NewHammerAttack;
	else
		TriggerCombo = NewPlayerHook || NewHammerAttack;

	if(TriggerCombo)
	{
		const float ResetTime = g_Config.m_BcHookComboResetTime / 1000.0f;
		const float Now = Client()->LocalTime();
		if(m_HookComboLastHookTime >= 0.0f && (Now - m_HookComboLastHookTime) > ResetTime)
			m_HookComboCounter = 0;
		m_HookComboLastHookTime = Now;
		TriggerHookComboStep();
	}
}

bool CBestClient::HasHookComboWork() const
{
	if(IsComponentDisabled(COMPONENT_GAMEPLAY_HOOK_COMBO))
		return false;
	return g_Config.m_BcHookCombo != 0 || !m_vHookComboPopups.empty();
}

void CBestClient::RenderHookCombo(bool ForcePreview)
{
	if(!ForcePreview && IsComponentDisabled(COMPONENT_GAMEPLAY_HOOK_COMBO))
		return;

	if(!ForcePreview && (!g_Config.m_BcHookCombo || m_vHookComboPopups.empty()))
		return;
	if(GameClient()->m_Scoreboard.IsActive() || (GameClient()->m_Menus.IsActive() && !ForcePreview))
		return;

	constexpr float PopupLifetime = 1.1f;
	constexpr float FadeIn = 0.15f;
	constexpr float FadeOut = 0.25f;

	const float Width = 300.0f * Graphics()->ScreenAspect();
	const float Height = HudLayout::CANVAS_HEIGHT;
	const auto Layout = HudLayout::Get(HudLayout::MODULE_HOOK_COMBO, Width, Height);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	const bool BackgroundEnabled = Layout.m_BackgroundEnabled;
	const ColorRGBA BackgroundColor = color_cast<ColorRGBA>(ColorHSLA(Layout.m_BackgroundColor, true));
	const float AnchorCenterX = Width * 0.5f;
	const float BaseY = Height * 0.84f;
	const float StackStep = 14.0f * Scale;

	int Stack = 0;
	SHookComboPopup PreviewPopup;
	PreviewPopup.m_Sequence = 7;
	PreviewPopup.m_Age = PopupLifetime * 0.35f;

	auto RenderPopup = [&](const SHookComboPopup &Popup) {
		const float Age = std::clamp(Popup.m_Age, 0.0f, PopupLifetime);
		const float In = std::clamp(Age / FadeIn, 0.0f, 1.0f);
		const float Out = Age > PopupLifetime - FadeOut ? std::clamp((PopupLifetime - Age) / FadeOut, 0.0f, 1.0f) : 1.0f;
		const float Alpha = ForcePreview ? 1.0f : In * Out;
		if(Alpha <= 0.0f)
			return;

		const int Sequence = std::max(Popup.m_Sequence, 1);
		const int ColorIndex = (Sequence - 1) % s_HookComboBaseTextCount;

		char aText[64];
		FormatHookComboText(Sequence, aText, sizeof(aText));
		char aBuf[96];
		str_format(aBuf, sizeof(aBuf), "%s (x%d)", aText, Sequence);

		ColorRGBA TextColor = s_aHookComboColors[ColorIndex];
		TextColor.a *= Alpha;
		TextRender()->TextColor(TextColor);

		const float FontSize = (ForcePreview ? 13.0f : (11.0f + In * 2.0f)) * Scale;
		const float TextWidth = TextRender()->TextWidth(FontSize, aBuf, -1, -1.0f);
		const float BoxWidth = TextWidth + 8.0f * Scale;
		const float BoxHeight = FontSize + 4.0f * Scale;
		const float Intro = 1.0f - (1.0f - In) * (1.0f - In);
		const float Rise = ForcePreview ? 0.0f : (20.0f * Intro + Age * 10.0f) * Scale;
		const float RectX = std::clamp(AnchorCenterX - BoxWidth * 0.5f, 0.0f, std::max(0.0f, Width - BoxWidth));
		const float RectY = std::clamp(ForcePreview ? BaseY : (BaseY + (1.0f - In) * 12.0f * Scale - Stack * StackStep - Rise), 0.0f, std::max(0.0f, Height - BoxHeight));
		if(BackgroundEnabled)
		{
			ColorRGBA BgColor = BackgroundColor;
			BgColor.a *= Alpha;
			const int Corners = HudLayout::BackgroundCorners(IGraphics::CORNER_ALL, RectX, RectY, BoxWidth, BoxHeight, Width, Height);
			Graphics()->DrawRect(RectX, RectY, BoxWidth, BoxHeight, BgColor, Corners, 4.0f * Scale);
		}
		TextRender()->Text(RectX + 4.0f * Scale, RectY + 2.0f * Scale, FontSize, aBuf, -1.0f);
	};

	if(ForcePreview)
	{
		RenderPopup(PreviewPopup);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		return;
	}

	for(auto It = m_vHookComboPopups.rbegin(); It != m_vHookComboPopups.rend(); ++It, ++Stack)
		RenderPopup(*It);

	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

bool CBestClient::IsComponentDisabledByMask(int Component, int MaskLo, int MaskHi)
{
	if(Component < 0 || Component >= NUM_COMPONENTS_EDITOR_COMPONENTS)
		return false;

	if(Component < 31)
		return (MaskLo & (1 << Component)) != 0;

	const int HiBit = Component - 31;
	return HiBit >= 0 && HiBit < 31 && (MaskHi & (1 << HiBit)) != 0;
}

bool CBestClient::IsComponentDisabled(EBestClientComponent Component) const
{
	return IsComponentDisabledByMask((int)Component, g_Config.m_BcDisabledComponentsMaskLo, g_Config.m_BcDisabledComponentsMaskHi);
}

void CBestClient::ConToggle45Degrees(IConsole::IResult *pResult, void *pUserData)
{
	CBestClient *pSelf = static_cast<CBestClient *>(pUserData);
	const bool HasStrokeArgument = pResult->NumArguments() > 0;
	pSelf->m_45degreestoggle = HasStrokeArgument ? (pResult->GetInteger(0) != 0) : 1;

	const auto Enable45Degrees = [&]() {
		if(pSelf->m_45degreesEnabled)
			return;
		pSelf->m_45degreesEnabled = 1;
		pSelf->GameClient()->Echo("[[green]] 45° on");
		g_Config.m_BcPrevInpMousesens45Degrees = (pSelf->m_SmallsensEnabled == 1 ? g_Config.m_BcPrevInpMousesensSmallSens : g_Config.m_InpMousesens);
		g_Config.m_BcPrevMouseMaxDistance45Degrees = g_Config.m_ClMouseMaxDistance;
		g_Config.m_ClMouseMaxDistance = 2;
		g_Config.m_InpMousesens = 4;
	};

	const auto Disable45Degrees = [&]() {
		if(!pSelf->m_45degreesEnabled)
			return;
		pSelf->m_45degreesEnabled = 0;
		pSelf->GameClient()->Echo("[[red]] 45° off");
		g_Config.m_ClMouseMaxDistance = g_Config.m_BcPrevMouseMaxDistance45Degrees;
		g_Config.m_InpMousesens = g_Config.m_BcPrevInpMousesens45Degrees;
	};

	if(!g_Config.m_BcToggle45Degrees && HasStrokeArgument)
	{
		if(pSelf->m_45degreestoggle && !pSelf->m_45degreestogglelastinput)
			Enable45Degrees();
		else if(!pSelf->m_45degreestoggle)
			Disable45Degrees();

		pSelf->m_45degreestogglelastinput = pSelf->m_45degreestoggle;
		return;
	}

	const bool TriggerToggle = HasStrokeArgument ? (pSelf->m_45degreestoggle && !pSelf->m_45degreestogglelastinput) : true;
	if(TriggerToggle)
	{
		if(pSelf->m_45degreesEnabled)
			Disable45Degrees();
		else
			Enable45Degrees();
	}

	pSelf->m_45degreestogglelastinput = pSelf->m_45degreestoggle;
}

void CBestClient::ConToggleSmallSens(IConsole::IResult *pResult, void *pUserData)
{
	CBestClient *pSelf = static_cast<CBestClient *>(pUserData);
	const bool HasStrokeArgument = pResult->NumArguments() > 0;
	pSelf->m_Smallsenstoggle = HasStrokeArgument ? (pResult->GetInteger(0) != 0) : 1;

	const auto EnableSmallSens = [&]() {
		if(pSelf->m_SmallsensEnabled)
			return;
		pSelf->m_SmallsensEnabled = 1;
		pSelf->GameClient()->Echo("[[green]] small sens on");
		g_Config.m_BcPrevInpMousesensSmallSens = (pSelf->m_45degreesEnabled == 1 ? g_Config.m_BcPrevInpMousesens45Degrees : g_Config.m_InpMousesens);
		g_Config.m_InpMousesens = 1;
	};

	const auto DisableSmallSens = [&]() {
		if(!pSelf->m_SmallsensEnabled)
			return;
		pSelf->m_SmallsensEnabled = 0;
		pSelf->GameClient()->Echo("[[red]] small sens off");
		g_Config.m_InpMousesens = g_Config.m_BcPrevInpMousesensSmallSens;
	};

	if(!g_Config.m_BcToggleSmallSens && HasStrokeArgument)
	{
		if(pSelf->m_Smallsenstoggle && !pSelf->m_Smallsenstogglelastinput)
			EnableSmallSens();
		else if(!pSelf->m_Smallsenstoggle)
			DisableSmallSens();

		pSelf->m_Smallsenstogglelastinput = pSelf->m_Smallsenstoggle;
		return;
	}

	const bool TriggerToggle = HasStrokeArgument ? (pSelf->m_Smallsenstoggle && !pSelf->m_Smallsenstogglelastinput) : true;
	if(TriggerToggle)
	{
		if(pSelf->m_SmallsensEnabled)
			DisableSmallSens();
		else
			EnableSmallSens();
	}

	pSelf->m_Smallsenstogglelastinput = pSelf->m_Smallsenstoggle;
}

void CBestClient::ConToggleDeepfly(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	CBestClient *pSelf = static_cast<CBestClient *>(pUserData);

	char aCurBind[128];
	str_copy(aCurBind, pSelf->GameClient()->m_Binds.Get(KEY_MOUSE_1, KeyModifier::NONE), sizeof(aCurBind));

	if(str_find_nocase(aCurBind, "+toggle cl_dummy_hammer"))
	{
		pSelf->GameClient()->Echo("[[red]] Deepfly off");
		if(str_length(pSelf->m_Oldmouse1Bind) > 1)
		{
			pSelf->GameClient()->m_Binds.Bind(KEY_MOUSE_1, pSelf->m_Oldmouse1Bind, false, KeyModifier::NONE);
		}
		else
		{
			pSelf->GameClient()->Echo("[[red]] No old bind in memory. Binding +fire");
			pSelf->GameClient()->m_Binds.Bind(KEY_MOUSE_1, "+fire", false, KeyModifier::NONE);
		}
	}
	else
	{
		pSelf->GameClient()->Echo("[[green]] Deepfly on");
		str_copy(pSelf->m_Oldmouse1Bind, aCurBind, sizeof(pSelf->m_Oldmouse1Bind));
		pSelf->GameClient()->m_Binds.Bind(KEY_MOUSE_1, "+fire; +toggle cl_dummy_hammer 1 0", false, KeyModifier::NONE);
	}
}

void CBestClient::ConToggleCinematicCamera(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	CBestClient *pSelf = static_cast<CBestClient *>(pUserData);
	g_Config.m_BcCinematicCamera = !g_Config.m_BcCinematicCamera;
	pSelf->GameClient()->Echo(g_Config.m_BcCinematicCamera ? "[[green]] Cinematic camera on" : "[[red]] Cinematic camera off");
}

bool CBestClient::NeedUpdate()
{
	return str_comp(m_aVersionStr, "0") != 0;
}

void CBestClient::ResetBestClientInfoTask()
{
	if(m_pBestClientInfoTask)
	{
		m_pBestClientInfoTask->Abort();
		m_pBestClientInfoTask = nullptr;
	}
}

void CBestClient::FetchBestClientInfo()
{
	if(m_pBestClientInfoTask && !m_pBestClientInfoTask->Done())
		return;

	char aUrl[512];
	BuildBestClientInfoUrl(aUrl, sizeof(aUrl));
	m_pBestClientInfoTask = HttpGet(aUrl);
	m_pBestClientInfoTask->HeaderString("Accept", "application/vnd.github+json");
	m_pBestClientInfoTask->HeaderString("User-Agent", CLIENT_NAME);
	m_pBestClientInfoTask->HeaderString("X-GitHub-Api-Version", "2022-11-28");
	m_pBestClientInfoTask->HeaderString("Cache-Control", "no-cache");
	m_pBestClientInfoTask->HeaderString("Pragma", "no-cache");
	m_pBestClientInfoTask->Timeout(CTimeout{10000, 0, 500, 10});
	m_pBestClientInfoTask->IpResolve(IPRESOLVE::V4);
	Http()->Run(m_pBestClientInfoTask);
}

void CBestClient::FinishBestClientInfo()
{
	json_value *pJson = m_pBestClientInfoTask->ResultJson();
	if(!pJson)
		return;

	const char *pCurrentVersion = FindBestClientReleaseVersion(pJson);

	// Update is available only when the remote tag is higher than current version.
	if(pCurrentVersion && CompareBestClientVersions(pCurrentVersion, BESTCLIENT_VERSION) > 0)
		str_copy(m_aVersionStr, pCurrentVersion, sizeof(m_aVersionStr));
	else
	{
		m_aVersionStr[0] = '0';
		m_aVersionStr[1] = '\0';
	}

	m_FetchedBestClientInfo = true;
	json_value_free(pJson);
}

void CBestClient::OnConsoleInit()
{
	Console()->Register("+BC_45_degrees", "", CFGFLAG_CLIENT, ConToggle45Degrees, this, "45 degrees bind");
	Console()->Register("BC_45_degrees", "", CFGFLAG_CLIENT, ConToggle45Degrees, this, "45 degrees bind (toggle)");
	Console()->Register("+BC_small_sens", "", CFGFLAG_CLIENT, ConToggleSmallSens, this, "Small sens bind");
	Console()->Register("BC_small_sens", "", CFGFLAG_CLIENT, ConToggleSmallSens, this, "Small sens bind (toggle)");
	Console()->Register("BC_deepfly_toggle", "", CFGFLAG_CLIENT, ConToggleDeepfly, this, "Deep fly toggle");
	Console()->Register("BC_cinematic_camera_toggle", "", CFGFLAG_CLIENT, ConToggleCinematicCamera, this, "Toggle cinematic spectator camera");
}
