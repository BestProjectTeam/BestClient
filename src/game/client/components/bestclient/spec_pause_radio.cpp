#include "spec_pause_radio.h"

#include <base/color.h>
#include <base/system.h>
#include <base/time.h>
#include <base/vmath.h>

#include <engine/client.h>
#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <generated/client_data.h>
#include <generated/protocol.h>

#include <game/client/animstate.h>
#include <game/client/components/binds.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/ui.h>
#include <game/client/ui_rect.h>

#include <algorithm>
#include <cmath>

static constexpr const float RADIO_WIDTH = 240.0f;
static constexpr const float RADIO_HEIGHT = 80.0f;
static constexpr const float RADIO_ROUNDING = 10.0f;
static constexpr const float TEE_SIZE = 72.0f;
static constexpr const float SELECTED_TEE_SIZE = 84.0f;
static constexpr const float SELECTION_DEAD_ZONE = 15.0f;

static bool ReplaceBindToken(char *pBuf, size_t BufSize, const char *pFrom, const char *pTo)
{
	char aTemp[512];
	str_copy(aTemp, pBuf, sizeof(aTemp));

	char aResult[512];
	aResult[0] = '\0';

	const char *pCursor = aTemp;
	bool Changed = false;
	const int FromLen = str_length(pFrom);
	while(true)
	{
		const char *pMatch = str_find(pCursor, pFrom);
		if(!pMatch)
		{
			str_append(aResult, pCursor, sizeof(aResult));
			break;
		}

		char aPrefix[512];
		const int PrefixLen = (int)(pMatch - pCursor);
		str_copy(aPrefix, pCursor, minimum((int)sizeof(aPrefix), PrefixLen + 1));
		str_append(aResult, aPrefix, sizeof(aResult));
		str_append(aResult, pTo, sizeof(aResult));
		pCursor = pMatch + FromLen;
		Changed = true;
	}

	if(!Changed)
		return false;
	str_copy(pBuf, aResult, BufSize);
	return true;
}

CSpecPauseRadio::CSpecPauseRadio()
{
	OnReset();
}

void CSpecPauseRadio::SendSpecPause(int Type)
{
	switch(Type)
	{
	case SPEC_PAUSE_PAUSE:
		GameClient()->m_Chat.SendChat(0, "/pause");
		break;
	case SPEC_PAUSE_SPEC:
		GameClient()->m_Chat.SendChat(0, "/spec");
		break;
	}
}

void CSpecPauseRadio::OnStateChange(int NewState, int OldState)
{
	if(NewState != OldState)
		OnReset();
}

void CSpecPauseRadio::OnReset()
{
	m_SelectorMouse = vec2(0.0f, 0.0f);
	m_SelectedType = SPEC_PAUSE_NONE;
	m_Active = false;
	m_WasActive = false;
	m_TouchPressedOutside = false;
}

void CSpecPauseRadio::OnRelease()
{
	m_Active = false;
}

bool CSpecPauseRadio::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!m_Active)
		return false;

	Ui()->ConvertMouseMove(&x, &y, CursorType);
	m_SelectorMouse.x = std::clamp(m_SelectorMouse.x + x, -RADIO_WIDTH / 2.0f, RADIO_WIDTH / 2.0f);
	m_SelectorMouse.y = std::clamp(m_SelectorMouse.y + y, -RADIO_HEIGHT / 2.0f, RADIO_HEIGHT / 2.0f);
	return true;
}

bool CSpecPauseRadio::OnInput(const IInput::CEvent &Event)
{
	if(m_Active && Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_ESCAPE)
	{
		m_SelectedType = SPEC_PAUSE_NONE;
		m_TouchPressedOutside = true;
		OnRelease();
		return true;
	}
	return false;
}

void CSpecPauseRadio::SyncSpectateBinds(bool EnableBetterSpectate)
{
	// Default Q bind is "say /pause" (see CBinds::SetDefaults).
	const char *pFrom = EnableBetterSpectate ? "say /pause" : "+specpause";
	const char *pTo = EnableBetterSpectate ? "+specpause" : "say /pause";

	CBinds &Binds = GameClient()->m_Binds;
	for(int Modifier = KeyModifier::NONE; Modifier < KeyModifier::COMBINATION_COUNT; Modifier++)
	{
		for(int KeyId = KEY_FIRST; KeyId < KEY_LAST; KeyId++)
		{
			const char *pBind = Binds.Get(KeyId, Modifier);
			if(!pBind[0] || !str_find(pBind, pFrom))
				continue;

			char aNewBind[512];
			str_copy(aNewBind, pBind, sizeof(aNewBind));
			if(ReplaceBindToken(aNewBind, sizeof(aNewBind), pFrom, pTo))
				Binds.Bind(KeyId, aNewBind, false, Modifier);
		}
	}
}

void CSpecPauseRadio::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	const CUIRect Screen = *Ui()->Screen();
	const vec2 ScreenCenter = Screen.Center();
	if(!m_InitializedSelectorMouse)
	{
		m_SelectorMouse = vec2(-RADIO_WIDTH * 0.5f, 0);
		m_InitializedSelectorMouse = true;
	}
	if(m_Active)
	{
		m_WasActive = true;
	}
	else
	{
		if(m_WasActive)
		{
			if(!m_TouchPressedOutside)
				SendSpecPause(m_SelectedType);
			m_WasActive = false;
			m_TouchPressedOutside = false;
		}
	}

	if(!m_Active)
		return;

	const CUIRect RadioMenu = {ScreenCenter.x - RADIO_WIDTH / 2.0f, ScreenCenter.y - RADIO_HEIGHT / 2.0f, RADIO_WIDTH, RADIO_HEIGHT};

	if(m_Active)
	{
		const bool WasTouchPressed = m_TouchState.m_AnyPressed;
		Ui()->UpdateTouchState(m_TouchState);
		if(m_TouchState.m_AnyPressed)
		{
			const vec2 TouchPosition = Screen.TopLeft() + m_TouchState.m_PrimaryPosition * Screen.Size();
			if(!WasTouchPressed && !RadioMenu.Inside(TouchPosition))
				m_TouchPressedOutside = true;
			if(!m_TouchPressedOutside)
			{
				m_SelectorMouse.x = std::clamp(TouchPosition.x - ScreenCenter.x, -RADIO_WIDTH / 2.0f, RADIO_WIDTH / 2.0f);
				m_SelectorMouse.y = std::clamp(TouchPosition.y - ScreenCenter.y, -RADIO_HEIGHT / 2.0f, RADIO_HEIGHT / 2.0f);
			}
		}
		else if(WasTouchPressed)
		{
			m_Active = false;
		}

		if(m_TouchPressedOutside || std::abs(m_SelectorMouse.x) <= SELECTION_DEAD_ZONE)
			m_SelectedType = SPEC_PAUSE_NONE;
		else
			m_SelectedType = m_SelectorMouse.x < 0.0f ? SPEC_PAUSE_PAUSE : SPEC_PAUSE_SPEC;
	}

	if(m_ActiveSince > time_get() - time_freq() * g_Config.m_BcSpecPauseShowDelay * 0.001f)
		return;

	Ui()->MapScreen();
	RadioMenu.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.15f), IGraphics::CORNER_ALL, RADIO_ROUNDING);

	CTeeRenderInfo PauseTeeInfo;
	const int LocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	if(LocalId >= 0 && LocalId < MAX_CLIENTS)
	{
		PauseTeeInfo = GameClient()->m_aClients[LocalId].m_RenderInfo;
	}
	else
	{
		const char *pSkinName = g_Config.m_ClDummy ? g_Config.m_ClDummySkin : g_Config.m_ClPlayerSkin;
		PauseTeeInfo.Apply(GameClient()->m_Skins.Find(pSkinName));
		PauseTeeInfo.ApplyColors(
			g_Config.m_ClDummy ? g_Config.m_ClDummyUseCustomColor : g_Config.m_ClPlayerUseCustomColor,
			g_Config.m_ClDummy ? g_Config.m_ClDummyColorBody : g_Config.m_ClPlayerColorBody,
			g_Config.m_ClDummy ? g_Config.m_ClDummyColorFeet : g_Config.m_ClPlayerColorFeet);
	}
	PauseTeeInfo.m_Size = m_SelectedType == SPEC_PAUSE_PAUSE ? SELECTED_TEE_SIZE : TEE_SIZE;

	CTeeRenderInfo SpecTeeInfo;
	SpecTeeInfo.Apply(GameClient()->m_Skins.Find("x_spec"));
	SpecTeeInfo.m_Size = m_SelectedType == SPEC_PAUSE_SPEC ? SELECTED_TEE_SIZE : TEE_SIZE;

	CAnimState PauseAnimation;
	PauseAnimation.Set(&g_pData->m_aAnimations[ANIM_BASE], 0.0f);
	PauseAnimation.Add(&g_pData->m_aAnimations[ANIM_SIT_RIGHT], 0.0f, 1.0f);
	CAnimState SpecAnimation;
	SpecAnimation.Set(&g_pData->m_aAnimations[ANIM_BASE], 0.0f);
	SpecAnimation.Add(&g_pData->m_aAnimations[ANIM_SIT_LEFT], 0.0f, 1.0f);

	const vec2 LeftTeePos = ScreenCenter + vec2(-RADIO_WIDTH * 0.35f, 1.0f);
	const vec2 RightTeePos = ScreenCenter + vec2(RADIO_WIDTH * 0.35f, 1.0f);

	RenderTools()->RenderTee(&PauseAnimation, &PauseTeeInfo, EMOTE_BLINK, vec2(1.0f, 0.0f), LeftTeePos);
	RenderTools()->RenderTee(&SpecAnimation, &SpecTeeInfo, EMOTE_BLINK, vec2(1.0f, 0.0f), RightTeePos);

	RenderTools()->RenderCursor(ScreenCenter + m_SelectorMouse, 24.0f);
}

void CSpecPauseRadio::ConOpenRadio(IConsole::IResult *pResult, void *pUserData)
{
	CSpecPauseRadio *pSelf = static_cast<CSpecPauseRadio *>(pUserData);
	if(pSelf->Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return;

	const bool Open = pResult->GetInteger(0) != 0;
	if(!Open)
	{
		pSelf->m_Active = false;
		return;
	}

	if(pSelf->GameClient()->m_Emoticon.IsActive() || pSelf->GameClient()->m_BindWheel.IsActive())
	{
		pSelf->m_Active = false;
		return;
	}

	if(!pSelf->m_Active)
	{
		pSelf->m_SelectedType = SPEC_PAUSE_NONE;
		pSelf->m_TouchPressedOutside = false;
	}

	pSelf->m_Active = true;

	if(pSelf->m_Active && !pSelf->m_WasActive)
		pSelf->m_ActiveSince = time_get();
}

void CSpecPauseRadio::ConchainBetterSpectate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CSpecPauseRadio *pSelf = static_cast<CSpecPauseRadio *>(pUserData);
	const int OldValue = g_Config.m_BcBetterSpectate;
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments() && OldValue != g_Config.m_BcBetterSpectate)
		pSelf->SyncSpectateBinds(g_Config.m_BcBetterSpectate != 0);
}

void CSpecPauseRadio::OnConsoleInit()
{
	Console()->Register("+specpause", "", CFGFLAG_CLIENT, ConOpenRadio, this, "Open the spec pause radio menu");
	Console()->Chain("bc_better_spectate", ConchainBetterSpectate, this);
}
