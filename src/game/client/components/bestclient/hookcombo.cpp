/* Copyright © 2026 BestProject Team */
#include "hookcombo.h"

#include <base/color.h>
#include <base/log.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/components/sounds.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>

static constexpr int s_BaseTextCount = 15;
static constexpr int s_VariantLimit = 100;
static constexpr int s_SoundCount = 7;
static constexpr int s_BrilliantSoundIndex = 5; // 0-based => sound #6
static constexpr int s_ModeHook = 0;
static constexpr int s_ModeHammer = 1;
static constexpr int s_ModeHookAndHammer = 2;

static constexpr const char *const s_apTexts[s_BaseTextCount] = {
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

static const ColorRGBA s_aColors[s_BaseTextCount] = {
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

static void FormatText(int Sequence, char *pBuf, int BufSize)
{
	if(Sequence <= s_BaseTextCount)
	{
		str_copy(pBuf, s_apTexts[Sequence - 1], BufSize);
		return;
	}

	if(Sequence <= s_VariantLimit)
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
		const int Step = Sequence - s_BaseTextCount - 1;
		const int GroupSize = 9;
		const int Group = std::min(Step / GroupSize, s_AdvancedTitleCount - 1);
		str_format(pBuf, BufSize, "%s %d", s_apAdvancedTitles[Group], Sequence);
		return;
	}

	str_copy(pBuf, "BRILLIANT", BufSize);
}

void CHookCombo::OnInit()
{
	LoadSounds();
	ResetState();
}

void CHookCombo::OnShutdown()
{
	ResetState();
	UnloadSounds();
}

void CHookCombo::OnReset()
{
	ResetState();
}

void CHookCombo::OnStateChange(int NewState, int OldState)
{
	(void)NewState;
	(void)OldState;
	ResetState();
}

void CHookCombo::LoadSounds(bool LogErrors)
{
	for(int &SoundId : m_aSoundIds)
		SoundId = -1;

	for(int i = 0; i < (int)m_aSoundIds.size(); ++i)
	{
		auto TryLoad = [this, i](const char *pPath, int StorageType) {
			if(m_aSoundIds[i] != -1)
				return;
			if(!Storage()->FileExists(pPath, StorageType))
				return;
			m_aSoundIds[i] = Sound()->LoadWV(pPath, StorageType);
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

		if(LogErrors && m_aSoundIds[i] == -1)
			log_warn("hook_combo", "Failed to load combo sound #%d (expected data/BestClient/combo/combo%d.wv)", i + 1, i + 1);
	}
}

void CHookCombo::UnloadSounds()
{
	for(int &SoundId : m_aSoundIds)
	{
		if(SoundId != -1)
		{
			Sound()->UnloadSample(SoundId);
			SoundId = -1;
		}
	}
}

void CHookCombo::ResetState()
{
	m_Counter = 0;
	m_LastHookTime = -1.0f;
	m_TrackedClientId = -1;
	m_LastHookedPlayer = -1;
	m_LastProcessedGameTick = -1;
	m_SoundErrorShown = false;
	m_vPopups.clear();
}

void CHookCombo::TriggerStep()
{
	m_Counter++;

	SPopup Popup;
	Popup.m_Sequence = m_Counter;
	Popup.m_Age = 0.0f;
	m_vPopups.push_back(Popup);
	if(m_vPopups.size() > 16)
		m_vPopups.erase(m_vPopups.begin());

	if(!GameClient()->m_SuppressEvents && g_Config.m_SndEnable)
	{
		int SoundIndex = 0;
		if(m_Counter > s_VariantLimit)
			SoundIndex = s_BrilliantSoundIndex;
		else if(m_Counter <= s_SoundCount)
			SoundIndex = m_Counter - 1;
		else
			SoundIndex = (m_Counter - 1) % s_SoundCount;

		int SoundId = m_aSoundIds[SoundIndex];
		if(SoundId == -1)
		{
			// Retry at runtime, because startup path/audio init may differ from gameplay runtime.
			LoadSounds(false);
			SoundId = m_aSoundIds[SoundIndex];
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
		else if(!m_SoundErrorShown)
		{
			m_SoundErrorShown = true;
		GameClient()->Echo(Localize("Hook combo sounds not found. Put files as data/BestClient/combo/combo1.wv ... combo7.wv"));
		}
	}
}

bool CHookCombo::HasWork() const
{
	return g_Config.m_BcHookCombo != 0 || !m_vPopups.empty();
}

void CHookCombo::OnRender()
{
	if(!HasWork())
		return;

	constexpr float PopupLifetime = 1.1f;
	const float FrameTime = Client()->RenderFrameTime();
	for(auto &Popup : m_vPopups)
		Popup.m_Age += FrameTime;
	m_vPopups.erase(std::remove_if(m_vPopups.begin(), m_vPopups.end(), [](const SPopup &Popup) {
		return Popup.m_Age >= PopupLifetime;
	}),
		m_vPopups.end());

	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		ResetState();
		return;
	}

	if(!g_Config.m_BcHookCombo)
	{
		ResetState();
		return;
	}

	const bool IsDemoPlayback = Client()->State() == IClient::STATE_DEMOPLAYBACK;
	if(!IsDemoPlayback && GameClient()->m_Snap.m_SpecInfo.m_Active)
		return;

	const int ComboMode = std::clamp(g_Config.m_BcHookComboMode, s_ModeHook, s_ModeHookAndHammer);
	int LocalId = -1;
	bool NewPlayerHook = false;
	bool NewHammerAttack = false;

	if(IsDemoPlayback)
	{
		if(GameClient()->m_Snap.m_SpecInfo.m_Active)
		{
			const int SpectatorId = GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;
			if(SpectatorId > SPEC_FREEVIEW && SpectatorId < MAX_CLIENTS && GameClient()->m_Snap.m_aCharacters[SpectatorId].m_Active)
				LocalId = SpectatorId;
		}
		else if(in_range(GameClient()->m_Snap.m_LocalClientId, 0, MAX_CLIENTS - 1) && GameClient()->m_Snap.m_aCharacters[GameClient()->m_Snap.m_LocalClientId].m_Active)
		{
			LocalId = GameClient()->m_Snap.m_LocalClientId;
		}

		if(LocalId < 0 || !GameClient()->m_aClients[LocalId].m_Active)
			return;

		const int CurrentGameTick = Client()->GameTick(0);
		if(m_LastProcessedGameTick > CurrentGameTick)
			ResetState();
		if(m_LastProcessedGameTick == CurrentGameTick)
			return;
		m_LastProcessedGameTick = CurrentGameTick;
	}
	else
	{
		const bool HammerEventFrame = GameClient()->m_aPredictedHammerHitEvent[g_Config.m_ClDummy];
		if(!GameClient()->m_NewPredictedTick && !(HammerEventFrame && ComboMode != s_ModeHook))
			return;

		LocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
		if(LocalId < 0 || LocalId >= MAX_CLIENTS)
			LocalId = GameClient()->m_Snap.m_LocalClientId;
		if(LocalId < 0 || LocalId >= MAX_CLIENTS || !GameClient()->m_aClients[LocalId].m_Active)
			return;
	}

	if(LocalId != m_TrackedClientId)
	{
		m_TrackedClientId = LocalId;
		m_LastHookedPlayer = -1;
	}

	if(IsDemoPlayback)
	{
		const auto &TrackedCharacter = GameClient()->m_Snap.m_aCharacters[LocalId];
		const int HookedPlayer = TrackedCharacter.m_Cur.m_HookedPlayer;
		NewPlayerHook = HookedPlayer >= 0 && (m_LastHookedPlayer < 0 || HookedPlayer != m_LastHookedPlayer);
		m_LastHookedPlayer = HookedPlayer;
		NewHammerAttack = TrackedCharacter.m_Cur.m_AttackTick != TrackedCharacter.m_Prev.m_AttackTick &&
				  (TrackedCharacter.m_Cur.m_Weapon == WEAPON_HAMMER || TrackedCharacter.m_Prev.m_Weapon == WEAPON_HAMMER);
	}
	else
	{
		const int HookedPlayer = GameClient()->m_aClients[LocalId].m_Predicted.HookedPlayer();
		NewPlayerHook = HookedPlayer >= 0 && (m_LastHookedPlayer < 0 || HookedPlayer != m_LastHookedPlayer);
		m_LastHookedPlayer = HookedPlayer;
		NewHammerAttack = GameClient()->m_aPredictedHammerHitEvent[g_Config.m_ClDummy];
	}

	bool TriggerCombo = false;
	if(ComboMode == s_ModeHook)
		TriggerCombo = NewPlayerHook;
	else if(ComboMode == s_ModeHammer)
		TriggerCombo = NewHammerAttack;
	else
		TriggerCombo = NewPlayerHook || NewHammerAttack;

	if(TriggerCombo)
	{
		const float ResetTime = g_Config.m_BcHookComboResetTime / 1000.0f;
		const float Now = Client()->LocalTime();
		if(m_LastHookTime >= 0.0f && (Now - m_LastHookTime) > ResetTime)
			m_Counter = 0;
		m_LastHookTime = Now;
		TriggerStep();
	}
}

void CHookCombo::Render(bool ForcePreview)
{
	if(!ForcePreview && (!g_Config.m_BcHookCombo || m_vPopups.empty()))
		return;
	if(GameClient()->m_Scoreboard.IsActive() || (GameClient()->m_Menus.IsActive() && !ForcePreview))
		return;

	constexpr float PopupLifetime = 1.1f;
	constexpr float FadeIn = 0.15f;
	constexpr float FadeOut = 0.25f;

	const float Width = 300.0f * Graphics()->ScreenAspect();
	const float Height = 300.0f;
	const float Scale = std::clamp(g_Config.m_BcHookComboSize / 100.0f, 0.5f, 2.0f);
	const float AnchorCenterX = Width * 0.5f;
	const float BaseY = Height * 0.84f;
	const float StackStep = 14.0f * Scale;

	int Stack = 0;
	SPopup PreviewPopup;
	PreviewPopup.m_Sequence = 7;
	PreviewPopup.m_Age = PopupLifetime * 0.35f;

	auto RenderPopup = [&](const SPopup &Popup) {
		const float Age = std::clamp(Popup.m_Age, 0.0f, PopupLifetime);
		const float In = std::clamp(Age / FadeIn, 0.0f, 1.0f);
		const float Out = Age > PopupLifetime - FadeOut ? std::clamp((PopupLifetime - Age) / FadeOut, 0.0f, 1.0f) : 1.0f;
		const float Alpha = ForcePreview ? 1.0f : In * Out;
		if(Alpha <= 0.0f)
			return;

		const int Sequence = std::max(Popup.m_Sequence, 1);
		const int ColorIndex = (Sequence - 1) % s_BaseTextCount;

		char aText[64];
		FormatText(Sequence, aText, sizeof(aText));
		char aBuf[96];
		str_format(aBuf, sizeof(aBuf), "%s (x%d)", aText, Sequence);

		ColorRGBA TextColor = s_aColors[ColorIndex];
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
		TextRender()->Text(RectX + 4.0f * Scale, RectY + 2.0f * Scale, FontSize, aBuf, -1.0f);
	};

	if(ForcePreview)
	{
		RenderPopup(PreviewPopup);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		return;
	}

	for(auto It = m_vPopups.rbegin(); It != m_vPopups.rend(); ++It, ++Stack)
		RenderPopup(*It);

	TextRender()->TextColor(TextRender()->DefaultTextColor());
}
