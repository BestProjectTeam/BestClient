/* Copyright © 2026 BestProject Team */
#include "bestclient.h"

#include "chat_qol.h"

#include <base/io.h>
#include <base/color.h>
#include <base/time.h>
#include <engine/console.h>
#include <engine/editor.h>
#include <engine/friends.h>
#include <engine/graphics.h>
#include <engine/http.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <generated/client_data.h>
#include <generated/protocol.h>

#include <game/client/components/bestclient/fx/own_tee.h>
#include <game/client/components/bestclient/ui_theme/style.h>
#include <game/client/components/mapimages.h>
#include <game/client/components/sounds.h>
#include <game/client/components/menus.h>
#include <game/client/components/skins.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>

static const int FROZEN_SKIN_MAX_DARKEN = 70;

int CBestClient::HudFramesPerSecond(float FrameTimeAverage)
{
	if(!g_Config.m_BcDisplayMaxFps)
		return round_to_int(1.0f / FrameTimeAverage);

	constexpr float Window = 0.5f;
	const float FrameTime = std::max(Client()->RenderFrameTime(), 0.000001f);
	m_HudMaxFpsShortest = std::min(m_HudMaxFpsShortest, FrameTime);
	m_HudMaxFpsWindow += FrameTime;
	if(m_HudMaxFpsWindow >= Window)
	{
		m_HudMaxFps = std::clamp(round_to_int(1.0f / m_HudMaxFpsShortest), 1, 99999);
		m_HudMaxFpsWindow = 0.0f;
		m_HudMaxFpsShortest = 1.0f;
	}
	return m_HudMaxFps;
}

void CBestClient::UpdateGoresMode()
{
	if(!g_Config.m_BcGoresMode || !GameClient()->m_Snap.m_pLocalCharacter)
	{
		m_GoresHeavyWeapon = false;
		return;
	}

	const int CurWeapon = GameClient()->m_Snap.m_pLocalCharacter->m_Weapon;
	m_GoresHeavyWeapon = CurWeapon == WEAPON_GRENADE || CurWeapon == WEAPON_LASER || CurWeapon == WEAPON_SHOTGUN;
	if(g_Config.m_BcGoresModeDisableIfWeapons && m_GoresHeavyWeapon)
		return;

	if(CurWeapon == WEAPON_HAMMER)
		GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy].m_WantedWeapon = WEAPON_GUN + 1;
}

void CBestClient::OnGoresFireInput(int Pressed)
{
	if(!g_Config.m_BcGoresMode)
		return;
	if(g_Config.m_BcGoresModeDisableIfWeapons && m_GoresHeavyWeapon)
		return;

	int *pPrevWeapon = &GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy].m_PrevWeapon;
	if(((*pPrevWeapon) & 1) != Pressed)
		(*pPrevWeapon)++;
	*pPrevWeapon &= INPUT_STATE_MASK;
	GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy].m_WantedWeapon = 0;
}

bool BestClientShouldMuteOthersHookSound(int SoundId, bool MuteEnabled, bool LocalJustGrabbed)
{
	if(!MuteEnabled)
		return false;
	if(SoundId == SOUND_HOOK_ATTACH_GROUND || SoundId == SOUND_HOOK_NOATTACH)
		return true;
	if(SoundId == SOUND_HOOK_ATTACH_PLAYER && !LocalJustGrabbed)
		return true;
	return false;
}

bool BestClientShouldMuteOthersHammerHitSound(bool MuteEnabled, bool IsOtherPlayer)
{
	return MuteEnabled && IsOtherPlayer;
}

bool BestClientShouldMuteOthersHammerWorldSound(int SoundId, bool MuteEnabled, bool IsOtherPlayer)
{
	if(!BestClientShouldMuteOthersHammerHitSound(MuteEnabled, IsOtherPlayer))
		return false;
	return SoundId == SOUND_HAMMER_HIT;
}

bool BestClientShouldMuteOthersAirJumpSound(bool MuteEnabled, bool IsOtherPlayer)
{
	return MuteEnabled && IsOtherPlayer;
}

void BestClientPlayAirJump(CGameClient *pGameClient, int ClientId, vec2 Pos, float Alpha, float Volume)
{
	const bool OtherPlayer = !BcIsOwnTee(pGameClient, ClientId);
	if(g_Config.m_BcAirJump && g_Config.m_BcAirJumpHideParticles && !OtherPlayer)
	{
		if(g_Config.m_SndGame && !BestClientShouldMuteOthersAirJumpSound(g_Config.m_BcMuteOthersAirJump, OtherPlayer))
			pGameClient->m_Sounds.PlayAt(CSounds::CHN_WORLD, SOUND_PLAYER_AIRJUMP, Volume, Pos);
		return;
	}
	pGameClient->m_Effects.AirJump(Pos, Alpha, Volume, OtherPlayer);
}

void BestClientPlayHammerHit(CGameClient *pGameClient, vec2 Pos, float Alpha, float Volume, int ClientId)
{
	if(g_Config.m_BcHammerSlash && g_Config.m_BcHammerHideParticles && BcIsOwnTee(pGameClient, ClientId))
	{
		if(g_Config.m_SndGame)
			pGameClient->m_Sounds.PlayAt(CSounds::CHN_WORLD, SOUND_HAMMER_HIT, Volume, Pos);
		return;
	}
	pGameClient->m_Effects.HammerHit(Pos, Alpha, Volume, !BcIsOwnTee(pGameClient, ClientId));
}

bool BestClientIsLocalHammerHitEvent(vec2 HitPos, bool LocalJustFired, vec2 LocalPos, float Radius)
{
	if(!LocalJustFired)
		return false;
	return distance(HitPos, LocalPos) <= Radius;
}

void BestClientApplyMenuFont(ITextRender *pTextRender)
{
	if(!pTextRender)
		return;
	if(g_Config.m_BcMenuUiStyle && g_Config.m_BcMenuUiBetterFont)
		pTextRender->SetCustomFace(BESTCLIENT_BETTER_FONT_FACE);
	else
		pTextRender->SetCustomFace(g_Config.m_TcCustomFont);
}

void BestClientRefreshMenuFont(CGameClient *pGameClient)
{
	if(!pGameClient)
		return;
	BestClientApplyMenuFont(pGameClient->TextRender());
	pGameClient->TextRender()->OnPreWindowResize();
	pGameClient->OnWindowResize();
	if(pGameClient->Editor())
		pGameClient->Editor()->OnWindowResize();
	pGameClient->TextRender()->OnWindowResize();
	pGameClient->m_MapImages.SetTextureScale(101);
	pGameClient->m_MapImages.SetTextureScale(g_Config.m_ClTextEntitiesSize);
}

void CBestClient::SaveUiThemeFileCallback(IConfigManager *, void *pUserData)
{
	CBestClient *pThis = static_cast<CBestClient *>(pUserData);
	BestClientUiTheme::SaveFile(pThis->Storage());
}

void CBestClient::OnInit()
{
	m_MenuMediaBackground.Init(Graphics(), Storage());
	BestClientApplyMenuFont(TextRender());
	BestClientUiTheme::LoadFile(Storage());
}

void CBestClient::OnConsoleInit()
{
	IConfigManager *pConfigManager = Kernel()->RequestInterface<IConfigManager>();
	if(pConfigManager)
		pConfigManager->RegisterCallback(SaveUiThemeFileCallback, this, ConfigDomain::BESTCLIENT);

	Console()->Register("bc_saves", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUserData) {
		CBestClient *pThis = static_cast<CBestClient *>(pUserData);
		CChatQoL::ListSavesForCurrentMap(pThis->GameClient()->m_Chat);
	}, this, "List team saves for the current map in chat");
}

void CBestClient::OnMapLoad()
{
	CChatQoL::ClearSavesChatOnMapChange(GameClient());
	m_SavesNotifySchedule = true;
	m_SavesNotifyDeadline = 0;
}

void CBestClient::OnUpdate()
{
	UpdateFindHours();

	if(!g_Config.m_BcNotifySavesOnMap || Client()->State() == IClient::STATE_DEMOPLAYBACK)
	{
		m_SavesNotifySchedule = false;
		m_SavesNotifyDeadline = 0;
		return;
	}

	if(Client()->State() != IClient::STATE_ONLINE)
		return;

	if(m_SavesNotifySchedule)
	{
		m_SavesNotifySchedule = false;
		m_SavesNotifyDeadline = time_get() + time_freq() * 2;
	}

	if(m_SavesNotifyDeadline > 0 && time_get() >= m_SavesNotifyDeadline)
	{
		m_SavesNotifyDeadline = 0;
		CChatQoL::NotifySavesOnMapLoad(GameClient());
	}
}

void CBestClient::OnShutdown()
{
	ResetFindHours();
	m_MenuMediaBackground.Shutdown();
	m_SavesNotifySchedule = false;
	m_SavesNotifyDeadline = 0;
}

void CBestClient::OnReset()
{
	ResetFindHours();
	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		m_aAutoTeamLockLastTeam[Dummy] = TEAM_FLOCK;
		m_aAutoTeamLockDeadlineTick[Dummy] = 0;
		m_aAutoTeamLockPending[Dummy] = false;
	}
	m_SpecMovedActiveTick = -1;
	m_SpecMovedLastTick = -1;
	m_SpecMovedNotifyTime = -999.0f;
	if(!m_SavesNotifySchedule)
		m_SavesNotifyDeadline = 0;
}

void CBestClient::OnClientNameChanged(int ClientId, const char *pNewName, const char *pNewClan)
{
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return;
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !pNewName || !pNewClan)
		return;
	if(ClientId == GameClient()->m_Snap.m_LocalClientId)
		return;
	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		if(ClientId == GameClient()->m_aLocalIds[Dummy])
			return;
	}

	CGameClient::CClientData &ClientData = GameClient()->m_aClients[ClientId];
	if(ClientData.m_aName[0] == '\0')
		return;
	if(str_comp(ClientData.m_aName, pNewName) == 0 && str_comp(ClientData.m_aClan, pNewClan) == 0)
		return;

	IFriends *pFoes = GameClient()->Foes();
	if(!pFoes || !pFoes->IsFriend(ClientData.m_aName, ClientData.m_aClan, true))
		return;

	pFoes->RemoveFriend(ClientData.m_aName, ClientData.m_aClan);
	pFoes->AddFriend(pNewName, pNewClan);
}

void CBestClient::UpdateAutoTeamLock()
{
	if(Client()->State() != IClient::STATE_ONLINE)
	{
		for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
		{
			m_aAutoTeamLockLastTeam[Dummy] = TEAM_FLOCK;
			m_aAutoTeamLockDeadlineTick[Dummy] = 0;
			m_aAutoTeamLockPending[Dummy] = false;
		}
		return;
	}

	const int Dummy = g_Config.m_ClDummy;
	const int ClientId = GameClient()->m_aLocalIds[Dummy];
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
	{
		m_aAutoTeamLockLastTeam[Dummy] = TEAM_FLOCK;
		m_aAutoTeamLockDeadlineTick[Dummy] = 0;
		m_aAutoTeamLockPending[Dummy] = false;
		return;
	}

	const int Team = GameClient()->m_Teams.Team(ClientId);
	const bool TeamCanBeLocked = Team > TEAM_FLOCK && Team < TEAM_SUPER;
	const bool LastTeamCanBeLocked = m_aAutoTeamLockLastTeam[Dummy] > TEAM_FLOCK && m_aAutoTeamLockLastTeam[Dummy] < TEAM_SUPER;

	if(!g_Config.m_BcAutoTeamLock)
	{
		m_aAutoTeamLockLastTeam[Dummy] = Team;
		m_aAutoTeamLockDeadlineTick[Dummy] = 0;
		m_aAutoTeamLockPending[Dummy] = false;
		return;
	}

	if(TeamCanBeLocked && (!LastTeamCanBeLocked || Team != m_aAutoTeamLockLastTeam[Dummy]))
	{
		const int DelayTicks = g_Config.m_BcAutoTeamLockDelay * Client()->GameTickSpeed();
		m_aAutoTeamLockDeadlineTick[Dummy] = (int64_t)Client()->GameTick(Dummy) + DelayTicks;
		m_aAutoTeamLockPending[Dummy] = true;
	}
	else if(!TeamCanBeLocked)
	{
		m_aAutoTeamLockDeadlineTick[Dummy] = 0;
		m_aAutoTeamLockPending[Dummy] = false;
	}

	if(m_aAutoTeamLockPending[Dummy] && TeamCanBeLocked && Client()->GameTick(Dummy) >= m_aAutoTeamLockDeadlineTick[Dummy])
	{
		GameClient()->m_Chat.SendChat(0, "/lock 1");
		m_aAutoTeamLockPending[Dummy] = false;
	}

	m_aAutoTeamLockLastTeam[Dummy] = Team;
}

void CBestClient::UpdateSpecMovedNotify()
{
	if(Client()->State() != IClient::STATE_ONLINE)
	{
		m_SpecMovedActiveTick = -1;
		return;
	}

	if(!GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		m_SpecMovedActiveTick = -1;
		m_SpecMovedNotifyTime = -999.0f;
		return;
	}

	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId < 0 || LocalId >= MAX_CLIENTS)
	{
		m_SpecMovedActiveTick = -1;
		return;
	}

	const auto &CharInfo = GameClient()->m_Snap.m_aCharacters[LocalId];
	if(!CharInfo.m_Active)
	{
		m_SpecMovedActiveTick = -1;
		return;
	}

	const int CurrentTick = Client()->GameTick(0);

	if(m_SpecMovedActiveTick < 0)
		m_SpecMovedActiveTick = CurrentTick;

	if(CurrentTick <= m_SpecMovedActiveTick + 3)
		return;

	if(m_SpecMovedLastTick == CurrentTick)
		return;
	m_SpecMovedLastTick = CurrentTick;

	if(CharInfo.m_Cur.m_X != CharInfo.m_Prev.m_X || CharInfo.m_Cur.m_Y != CharInfo.m_Prev.m_Y)
	{
		constexpr float Duration = 2.5f;
		const float Age = Client()->LocalTime() - m_SpecMovedNotifyTime;
		if(Age < 0.0f || Age >= Duration)
			m_SpecMovedNotifyTime = Client()->LocalTime();
	}
}

void CBestClient::RenderSpecMovedNotify()
{
	if(!g_Config.m_BcSpecMovedNotify || !GameClient()->m_Snap.m_SpecInfo.m_Active)
		return;

	constexpr float Duration = 2.5f;
	constexpr float FadeIn = 0.12f;
	constexpr float FadeOut = 0.5f;

	const float Now = Client()->LocalTime();
	const float Age = Now - m_SpecMovedNotifyTime;
	if(Age < 0.0f || Age > Duration)
		return;

	if(GameClient()->m_Scoreboard.IsActive() || GameClient()->m_Menus.IsActive())
		return;

	const float In = std::clamp(Age / FadeIn, 0.0f, 1.0f);
	const float Out = Age > Duration - FadeOut ? std::clamp((Duration - Age) / FadeOut, 0.0f, 1.0f) : 1.0f;
	const float Alpha = In * Out;
	if(Alpha <= 0.0f)
		return;

	const float Width = 300.0f * Graphics()->ScreenAspect();
	constexpr float Height = 300.0f;
	constexpr float FontSize = 9.0f;
	const char *pText = g_Config.m_BcSpecMovedNotifyText;
	if(pText[0] == '\0')
		pText = "you moved in game";
	const float TextW = TextRender()->TextWidth(FontSize, pText, -1, -1.0f);
	const float X = Width * 0.5f - TextW * 0.5f;
	const float Y = Height * 0.58f;

	TextRender()->TextColor(1.0f, 0.15f, 0.15f, Alpha);
	TextRender()->Text(X, Y, FontSize, pText, -1.0f);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

float BestClientTeeRenderSize(const CGameClient *pGameClient, int ClientId)
{
	if(ClientId >= 0)
	{
		for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
		{
			if(pGameClient->m_aLocalIds[Dummy] == ClientId)
				return 64.0f * g_Config.m_TcTeeScale / 100.0f;
		}
	}
	return 64.0f;
}

void BestClientRenderRealHitbox(IGraphics *pGraphics, CGameClient *pGameClient, int ClientId, vec2 Position)
{
	if(!pGraphics || !pGameClient || !g_Config.m_BcShowRealHitbox)
		return;
	if(ClientId < 0 || pGameClient->m_Snap.m_SpecInfo.m_Active)
		return;
	if(ClientId != pGameClient->m_aLocalIds[g_Config.m_ClDummy])
		return;

	pGraphics->TextureClear();
	pGraphics->QuadsBegin();
	pGraphics->SetColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcShowRealHitboxColor, true)));
	pGraphics->DrawCircle(Position.x, Position.y, 2.0f, 12);
	pGraphics->QuadsEnd();
}

void BestClientRenderVotePercentages(CUi *pUi, ITextRender *pTextRender, const CUIRect &Bars, int Yes, int No, int Total)
{
	if(!pUi || !pTextRender || !g_Config.m_BcShowVotePercentage || Total <= 0)
		return;

	const int YesPct = (Yes * 100 + Total / 2) / Total;
	const int NoPct = (No * 100 + Total / 2) / Total;
	const float FontSize = Bars.h;
	char aBuf[16];

	str_format(aBuf, sizeof(aBuf), "%d%%", YesPct);
	pTextRender->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	pUi->DoLabel(&Bars, aBuf, FontSize, TEXTALIGN_ML);

	str_format(aBuf, sizeof(aBuf), "%d%%", NoPct);
	pUi->DoLabel(&Bars, aBuf, FontSize, TEXTALIGN_MR);

	pTextRender->TextColor(pTextRender->DefaultTextColor());
}

void BestClientRenderIndicatorIcon(IGraphics *pGraphics, const CUIRect &Rect, bool Developer, bool Fake, ColorRGBA Color)
{
	if(!pGraphics)
		return;

	const int ImageId = Developer ? IMAGE_BCDEVICON : (Fake ? IMAGE_BCFAKEICON : IMAGE_BCICON);
	pGraphics->TextureSet(g_pData->m_aImages[ImageId].m_Id);
	pGraphics->QuadsBegin();
	pGraphics->SetColor(Color);
	pGraphics->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
	const IGraphics::CQuadItem Quad(Rect.x, Rect.y, Rect.w, Rect.h);
	pGraphics->QuadsDrawTL(&Quad, 1);
	pGraphics->QuadsEnd();
	pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}

bool CBestClient::ApplyFrozenSkin(CTeeRenderInfo &RenderInfo, bool Frozen)
{
	if(!Frozen || g_Config.m_BcFrozenSkin[0] == '\0' || GameClient()->m_GameInfo.m_NoSkinChangeForFrozen)
		return false;

	const CSkin *pSkin = GameClient()->m_Skins.FindOrNullptr(g_Config.m_BcFrozenSkin);
	if(pSkin == nullptr)
		return false;

	const float Brightness = 1.0f - g_Config.m_BcFrozenSkinDarken / 100.0f;
	RenderInfo.m_aSixup[g_Config.m_ClDummy].Reset();
	RenderInfo.Apply(pSkin);
	RenderInfo.m_CustomColoredSkin = false;
	RenderInfo.m_ColorBody = ColorRGBA(Brightness, Brightness, Brightness);
	RenderInfo.m_ColorFeet = ColorRGBA(Brightness, Brightness, Brightness);
	return true;
}

float CMenus::FrozenSkinSettingsHeight()
{
	return 2.0f + 20.0f + 20.0f + 2.0f + 40.0f;
}

void CMenus::RenderFrozenSkinSettings(CUIRect &View)
{
	CUIRect Label, Button;

	bool UnknownSkin = false;
	if(g_Config.m_BcFrozenSkin[0] != '\0')
	{
		const CSkins::CSkinContainer *pSkinContainer = GameClient()->m_Skins.FindContainerOrNullptr(g_Config.m_BcFrozenSkin);
		UnknownSkin = pSkinContainer == nullptr ||
			      pSkinContainer->State() == CSkins::CSkinContainer::EState::NOT_FOUND ||
			      pSkinContainer->State() == CSkins::CSkinContainer::EState::ERROR;
	}

	View.HSplitTop(2.0f, nullptr, &View);
	View.HSplitTop(20.0f, &Label, &View);
	if(UnknownSkin)
		TextRender()->TextColor(ColorRGBA(1.0f, 0.35f, 0.35f, 1.0f));
	Ui()->DoLabel(&Label, Localize("Frozen skin"), 14.0f, TEXTALIGN_ML);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	View.HSplitTop(20.0f, &Button, &View);
	static CLineInput s_FrozenSkinInput(g_Config.m_BcFrozenSkin, sizeof(g_Config.m_BcFrozenSkin));
	Ui()->DoClearableEditBox(&s_FrozenSkinInput, &Button, 14.0f);
	GameClient()->m_Tooltips.DoToolTip(&s_FrozenSkinInput, &Button, Localize("Skin to show on frozen players, empty keeps the default behavior"));

	View.HSplitTop(2.0f, nullptr, &View);
	View.HSplitTop(40.0f, &Button, &View);

	CUIRect ValueLabel, ScrollBar;
	Button.HSplitMid(&ValueLabel, &ScrollBar);

	char aBuf[64];
	char aValueBuf[32];
	// bestclient
	if(BestClientUiTheme::IsNewScrollbar())
	{
		str_format(aValueBuf, sizeof(aValueBuf), "%d%%", g_Config.m_BcFrozenSkinDarken);
		str_copy(aBuf, Localize("Darken"));
	}
	else
		str_format(aBuf, sizeof(aBuf), "%s: %d%%", Localize("Darken"), g_Config.m_BcFrozenSkinDarken);
	// bestclient

	g_Config.m_BcFrozenSkinDarken = CUi::ms_LinearScrollbarScale.ToAbsolute(
		Ui()->DoScrollbarH(&g_Config.m_BcFrozenSkinDarken, &ScrollBar, CUi::ms_LinearScrollbarScale.ToRelative(g_Config.m_BcFrozenSkinDarken, 0, FROZEN_SKIN_MAX_DARKEN), nullptr, BestClientUiTheme::IsNewScrollbar() ? aValueBuf : nullptr),
		0, FROZEN_SKIN_MAX_DARKEN);

	Ui()->DoLabel(&ValueLabel, aBuf, ValueLabel.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_ML);
}

void CBestClient::EnsureAudioDefaultPack()
{
	const char *pDefaultDir = "assets/audio/default";
	if(Storage()->FolderExists(pDefaultDir, IStorage::TYPE_SAVE))
		return;

	Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
	Storage()->CreateFolder("assets/audio", IStorage::TYPE_SAVE);
	if(!Storage()->CreateFolder(pDefaultDir, IStorage::TYPE_SAVE))
		return;

	Storage()->ListDirectory(IStorage::TYPE_ALL, "data/audio", [](const char *pName, int IsDir, int DirType, void *pUser) {
		auto *pStorage = static_cast<IStorage *>(pUser);
		if(IsDir || !str_endswith(pName, ".wv"))
			return 0;

		char aSrc[IO_MAX_PATH_LENGTH];
		str_format(aSrc, sizeof(aSrc), "data/audio/%s", pName);

		void *pData;
		unsigned DataSize;
		if(!pStorage->ReadFile(aSrc, IStorage::TYPE_ALL, &pData, &DataSize))
			return 0;

		char aDst[IO_MAX_PATH_LENGTH];
		char aDstFull[IO_MAX_PATH_LENGTH];
		str_format(aDst, sizeof(aDst), "assets/audio/default/%s", pName);
		pStorage->GetCompletePath(IStorage::TYPE_SAVE, aDst, aDstFull, sizeof(aDstFull));

		IOHANDLE File = io_open(aDstFull, IOFLAG_WRITE);
		if(File)
		{
			io_write(File, pData, DataSize);
			io_close(File);
		}
		free(pData);
		return 0;
	}, Storage());
}

namespace
{
bool JsonIntField(const json_value *pValue, int *pOut)
{
	if(!pValue || pValue == &json_value_none || !pOut)
		return false;
	if(pValue->type == json_integer)
	{
		*pOut = (int)pValue->u.integer;
		return true;
	}
	if(pValue->type == json_double)
	{
		*pOut = (int)pValue->u.dbl;
		return true;
	}
	return false;
}
}

void CBestClient::ResetFindHours()
{
	if(m_pFindHoursTask)
	{
		m_pFindHoursTask->Abort();
		m_pFindHoursTask = nullptr;
	}
}

void CBestClient::FetchFindHours(const char *pName)
{
	if(!pName || pName[0] == '\0')
		return;
	if(m_pFindHoursTask && !m_pFindHoursTask->Done())
	{
		GameClient()->Echo(Localize("Request already in progress"));
		return;
	}

	str_copy(m_aFindHoursPlayer, pName, sizeof(m_aFindHoursPlayer));

	char aEncoded[256];
	EscapeUrl(aEncoded, sizeof(aEncoded), pName);
	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "https://ddstats.tw/player/json?player=%s", aEncoded);

	m_pFindHoursTask = HttpGet(aUrl);
	m_pFindHoursTask->Timeout(CTimeout{20000, 0, 500, 10});
	m_pFindHoursTask->LogProgress(HTTPLOG::FAILURE);
	m_pFindHoursTask->FailOnErrorStatus(false);
	Http()->Run(m_pFindHoursTask);
}

void CBestClient::UpdateFindHours()
{
	if(!m_pFindHoursTask || !m_pFindHoursTask->Done())
		return;

	std::shared_ptr<IHttpRequest> pTask = std::move(m_pFindHoursTask);
	if(pTask->State() != EHttpState::DONE || pTask->StatusCode() != 200)
	{
		if(pTask->State() != EHttpState::ABORTED)
			GameClient()->Echo(Localize("Find hours failed"));
		return;
	}

	json_value *pRoot = pTask->ResultJson();
	int Seconds = 0;
	int Points = 0;
	bool Ok = false;
	if(pRoot && pRoot->type == json_object)
	{
		const json_value *pGeneral = json_object_get(pRoot, "general_activity");
		const json_value *pProfile = json_object_get(pRoot, "profile");
		if(pGeneral && pGeneral != &json_value_none && pGeneral->type == json_object &&
			pProfile && pProfile != &json_value_none && pProfile->type == json_object)
		{
			Ok = JsonIntField(json_object_get(pGeneral, "total_seconds_played"), &Seconds) &&
			     JsonIntField(json_object_get(pProfile, "points"), &Points);
		}
	}
	if(pRoot)
		json_value_free(pRoot);

	if(!Ok)
	{
		GameClient()->Echo(Localize("Find hours failed"));
		return;
	}

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), Localize("Player %s has %d hours and %d points"), m_aFindHoursPlayer, Seconds / 3600, Points);
	GameClient()->Echo(aBuf);
}

void CBestClient::CopyPlayerSkin(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	const CGameClient::CClientData &ClientData = GameClient()->m_aClients[ClientId];
	if(ClientData.m_aSkinName[0] == '\0')
	{
		GameClient()->Echo(Localize("No skin found"));
		return;
	}

	if(g_Config.m_ClDummy)
	{
		str_copy(g_Config.m_ClDummySkin, ClientData.m_aSkinName, sizeof(g_Config.m_ClDummySkin));
		g_Config.m_ClDummyUseCustomColor = ClientData.m_UseCustomColor;
		g_Config.m_ClDummyColorBody = ClientData.m_ColorBody;
		g_Config.m_ClDummyColorFeet = ClientData.m_ColorFeet;
		GameClient()->SendDummyInfo(false);
	}
	else
	{
		str_copy(g_Config.m_ClPlayerSkin, ClientData.m_aSkinName, sizeof(g_Config.m_ClPlayerSkin));
		g_Config.m_ClPlayerUseCustomColor = ClientData.m_UseCustomColor;
		g_Config.m_ClPlayerColorBody = ClientData.m_ColorBody;
		g_Config.m_ClPlayerColorFeet = ClientData.m_ColorFeet;
		GameClient()->SendInfo(false);
	}

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), Localize("Copied skin from %s"), ClientData.m_aName);
	GameClient()->Echo(aBuf);
}

float BestClientScoreboardPlayerPopupHeight(const CGameClient *pGameClient, bool IsLocal, bool IsSpectatorRow, int ClientId)
{
	constexpr float OuterPopupPadding = 2.0f * (1.0f + 4.0f);
	constexpr float InnerMargin = 10.0f;
	constexpr float LabelHeight = 12.0f;
	constexpr float ItemSpacing = 2.0f;
	constexpr float ButtonHeight = 17.5f;
	constexpr float QuickActionHeight = 25.0f + ItemSpacing * 2.0f;
	constexpr float ButtonRow = ButtonHeight + ItemSpacing * 2.0f;

	int ExtraButtonRows = 0;
	const int LocalId = pGameClient->m_aLocalIds[g_Config.m_ClDummy];
	const int LocalTeam = LocalId >= 0 && LocalId < MAX_CLIENTS ? pGameClient->m_Teams.Team(LocalId) : TEAM_FLOCK;
	const int TargetTeam = ClientId >= 0 && ClientId < MAX_CLIENTS ? pGameClient->m_Teams.Team(ClientId) : TEAM_FLOCK;
	const bool LocalInTeam = LocalTeam != TEAM_FLOCK && LocalTeam != TEAM_SUPER;
	const bool TargetInTeam = TargetTeam != TEAM_FLOCK && TargetTeam != TEAM_SUPER;
	const bool LocalIsTarget = LocalId == ClientId;
	if(LocalInTeam || TargetInTeam)
	{
		if(LocalInTeam && LocalTeam == TargetTeam)
			ExtraButtonRows++;
		if(TargetInTeam && LocalTeam != TargetTeam)
			ExtraButtonRows++;
		if(LocalInTeam && TargetTeam != LocalTeam)
			ExtraButtonRows++;
		if(!LocalIsTarget && LocalInTeam && TargetTeam == LocalTeam)
			ExtraButtonRows++;
		if(LocalInTeam && LocalTeam == TargetTeam)
			ExtraButtonRows++;
	}

	int ButtonRows = 7;
	if(!IsSpectatorRow)
		ButtonRows++;
	if(!IsLocal && g_Config.m_BcVoiceChatEnable)
		ButtonRows += 2;
	ButtonRows += ExtraButtonRows;

	float Height = OuterPopupPadding + InnerMargin + LabelHeight;
	if(!IsLocal)
		Height += QuickActionHeight * 2.0f;
	Height += ButtonRows * ButtonRow;
	if(ExtraButtonRows != 0)
		Height += ItemSpacing * 4.0f;
	return Height;
}
