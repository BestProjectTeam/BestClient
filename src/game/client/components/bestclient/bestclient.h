/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_BESTCLIENT_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_BESTCLIENT_H

#include <base/color.h>
#include <base/vmath.h>

#include <engine/client/enums.h>

#include <game/client/component.h>
#include <game/teamscore.h>

#include "menu_media_background.h"

class CTeeRenderInfo;

constexpr float BESTCLIENT_LOCAL_HAMMER_HIT_RADIUS = 120.0f;

bool BestClientShouldMuteOthersHookSound(int SoundId, bool MuteEnabled, bool LocalJustGrabbed);
bool BestClientShouldMuteOthersHammerHitSound(bool MuteEnabled, bool IsOtherPlayer);
bool BestClientShouldMuteOthersHammerWorldSound(int SoundId, bool MuteEnabled, bool IsOtherPlayer);
bool BestClientShouldMuteOthersAirJumpSound(bool MuteEnabled, bool IsOtherPlayer);
bool BestClientIsLocalHammerHitEvent(vec2 HitPos, bool LocalJustFired, vec2 LocalPos, float Radius = BESTCLIENT_LOCAL_HAMMER_HIT_RADIUS);

class CGameClient;
void BestClientPlayAirJump(CGameClient *pGameClient, int ClientId, vec2 Pos, float Alpha, float Volume);
void BestClientPlayHammerHit(CGameClient *pGameClient, vec2 Pos, float Alpha, float Volume, int ClientId);

class CBestClient : public CComponent
{
	CMenuMediaBackground m_MenuMediaBackground;
	bool m_GoresHeavyWeapon = false;

	int m_aAutoTeamLockLastTeam[NUM_DUMMIES] = {TEAM_FLOCK, TEAM_FLOCK};
	int64_t m_aAutoTeamLockDeadlineTick[NUM_DUMMIES] = {};
	bool m_aAutoTeamLockPending[NUM_DUMMIES] = {};
	int m_SpecMovedActiveTick = -1;
	int m_SpecMovedLastTick = -1;
	float m_SpecMovedNotifyTime = -999.0f;
	bool m_SavesNotifySchedule = false;
	int64_t m_SavesNotifyDeadline = 0;

public:
	int Sizeof() const override { return sizeof(*this); }

	void OnInit() override;
	void OnShutdown() override;
	void OnReset() override;
	void OnMapLoad() override;
	void OnUpdate() override;
	void OnConsoleInit() override;

	CMenuMediaBackground &MenuMediaBackground() { return m_MenuMediaBackground; }
	const CMenuMediaBackground &MenuMediaBackground() const { return m_MenuMediaBackground; }

	bool ApplyFrozenSkin(CTeeRenderInfo &RenderInfo, bool Frozen);

	void EnsureAudioDefaultPack();

	void UpdateGoresMode();
	void OnGoresFireInput(int Pressed);

	void UpdateAutoTeamLock();
	void UpdateSpecMovedNotify();
	void RenderSpecMovedNotify();

	void OnClientNameChanged(int ClientId, const char *pNewName, const char *pNewClan);
};

void BestClientRenderRealHitbox(class IGraphics *pGraphics, class CGameClient *pGameClient, int ClientId, vec2 Position);
void BestClientRenderVotePercentages(class CUi *pUi, class ITextRender *pTextRender, const class CUIRect &Bars, int Yes, int No, int Total);
void BestClientRenderIndicatorIcon(class IGraphics *pGraphics, const class CUIRect &Rect, bool Developer = false, bool Fake = false, ColorRGBA Color = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));

constexpr const char *BESTCLIENT_BETTER_FONT_FACE = "ActayWide Bold";
void BestClientApplyMenuFont(class ITextRender *pTextRender);
void BestClientRefreshMenuFont(class CGameClient *pGameClient);

#endif
