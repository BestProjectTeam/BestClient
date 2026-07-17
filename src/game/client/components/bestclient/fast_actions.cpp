/* Copyright © 2026 BestProject Team */
#include "fast_actions.h"

#include <base/math.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

namespace
{
void EnsureFixedBindSlots(std::vector<CFastActions::CBind> &vBinds)
{
	if(vBinds.size() != FAST_ACTIONS_FIXED_SLOTS)
		vBinds.resize(FAST_ACTIONS_FIXED_SLOTS);
}

int SlotFromName(const char *pName)
{
	if(!pName || pName[0] == '\0')
		return -1;
	char *pEnd = nullptr;
	const long Value = std::strtol(pName, &pEnd, 10);
	if(*pEnd != '\0')
		return -1;
	const int Index = (int)Value - 1;
	return Index >= 0 && Index < FAST_ACTIONS_FIXED_SLOTS ? Index : -1;
}

bool IsLegacySlotName(const char *pName, int SlotIndex)
{
	if(!pName || pName[0] == '\0')
		return false;

	char aSlotName[16];
	str_format(aSlotName, sizeof(aSlotName), "%d", SlotIndex + 1);
	return str_comp(pName, aSlotName) == 0;
}

int KeyToSlotIndex(int Key)
{
	switch(Key)
	{
	case KEY_1: return 0;
	case KEY_KP_1: return 0;
	case KEY_2: return 1;
	case KEY_KP_2: return 1;
	case KEY_3: return 2;
	case KEY_KP_3: return 2;
	case KEY_4: return 3;
	case KEY_KP_4: return 3;
	case KEY_5: return 4;
	case KEY_KP_5: return 4;
	case KEY_6: return 5;
	case KEY_KP_6: return 5;
	default: return -1;
	}
}

} // namespace

CFastActions::CFastActions()
{
	OnReset();
}

void CFastActions::ConFaExecuteHover(IConsole::IResult *pResult, void *pUserData)
{
	CFastActions *pThis = (CFastActions *)pUserData;
	if(!g_Config.m_BcFastActions)
		return;
	pThis->ExecuteHoveredBind();
}

void CFastActions::ConOpenFa(IConsole::IResult *pResult, void *pUserData)
{
	CFastActions *pThis = (CFastActions *)pUserData;
	if(!g_Config.m_BcFastActions)
	{
		pThis->m_Active = false;
		pThis->m_SelectedBind = -1;
		return;
	}
	if(pThis->Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		if(pThis->GameClient()->m_Emoticon.IsActive())
		{
			pThis->m_Active = false;
		}
		else
		{
			const bool NewActive = pResult->GetInteger(0) != 0;
			if(!NewActive && pThis->m_SelectedBind >= 0)
				pThis->ExecuteBind(pThis->m_SelectedBind);
			if(NewActive && !pThis->m_Active)
				pThis->m_SelectedBind = -1;
			if(!NewActive)
				pThis->m_SelectedBind = -1;
			pThis->m_Active = NewActive;
		}
	}
}

void CFastActions::ConAddFaLegacy(IConsole::IResult *pResult, void *pUserData)
{
	int BindPos = pResult->GetInteger(0);
	if(BindPos < 0 || BindPos >= FAST_ACTIONS_FIXED_SLOTS)
		return;

	const char *aName = pResult->GetString(1);
	const char *aCommand = pResult->GetString(2);

	CFastActions *pThis = static_cast<CFastActions *>(pUserData);
	EnsureFixedBindSlots(pThis->m_vBinds);
	str_copy(pThis->m_vBinds[BindPos].m_aName, aName);
	str_copy(pThis->m_vBinds[BindPos].m_aCommand, aCommand);
}

void CFastActions::ConAddFa(IConsole::IResult *pResult, void *pUserData)
{
	const char *aName = pResult->GetString(0);
	const char *aCommand = pResult->GetString(1);

	CFastActions *pThis = static_cast<CFastActions *>(pUserData);
	pThis->AddBind(aName, aCommand);
}

void CFastActions::ConRemoveFa(IConsole::IResult *pResult, void *pUserData)
{
	const char *aName = pResult->GetString(0);
	const char *aCommand = pResult->GetString(1);

	CFastActions *pThis = static_cast<CFastActions *>(pUserData);
	pThis->RemoveBind(aName, aCommand);
}

void CFastActions::ConRemoveAllFaBinds(IConsole::IResult *pResult, void *pUserData)
{
	CFastActions *pThis = static_cast<CFastActions *>(pUserData);
	pThis->RemoveAllBinds();
}

void CFastActions::AddBind(const char *pName, const char *pCommand)
{
	EnsureFixedBindSlots(m_vBinds);
	if(pCommand[0] == '\0')
		return;

	const int NameAsSlot = SlotFromName(pName);
	int Slot = NameAsSlot;
	if(Slot < 0)
	{
		for(int i = 0; i < FAST_ACTIONS_FIXED_SLOTS; i++)
		{
			if(m_vBinds[i].m_aCommand[0] == '\0')
			{
				Slot = i;
				break;
			}
		}
	}
	if(Slot < 0)
		return;

	if(pName[0] == '\0')
	{
		m_vBinds[Slot].m_aName[0] = '\0';
	}
	else if(NameAsSlot < 0)
	{
		str_copy(m_vBinds[Slot].m_aName, pName);
	}
	str_copy(m_vBinds[Slot].m_aCommand, pCommand);
}

void CFastActions::RemoveBind(const char *pName, const char *pCommand)
{
	EnsureFixedBindSlots(m_vBinds);
	const int Slot = SlotFromName(pName);
	if(Slot >= 0)
	{
		if(pCommand[0] == '\0' || str_comp(m_vBinds[Slot].m_aCommand, pCommand) == 0)
			m_vBinds[Slot].m_aCommand[0] = '\0';
		return;
	}

	for(int i = 0; i < FAST_ACTIONS_FIXED_SLOTS; i++)
	{
		if(str_comp(m_vBinds[i].m_aCommand, pCommand) == 0)
		{
			m_vBinds[i].m_aCommand[0] = '\0';
			return;
		}
	}
}

void CFastActions::RemoveBind(int Index)
{
	EnsureFixedBindSlots(m_vBinds);
	if(Index >= FAST_ACTIONS_FIXED_SLOTS || Index < 0)
		return;
	m_vBinds[Index].m_aCommand[0] = '\0';
}

void CFastActions::RemoveAllBinds()
{
	EnsureFixedBindSlots(m_vBinds);
	for(int i = 0; i < FAST_ACTIONS_FIXED_SLOTS; i++)
		m_vBinds[i].m_aCommand[0] = '\0';
}

void CFastActions::OnConsoleInit()
{
	EnsureFixedBindSlots(m_vBinds);

	IConfigManager *pConfigManager = Kernel()->RequestInterface<IConfigManager>();
	if(pConfigManager)
		pConfigManager->RegisterCallback(ConfigSaveCallback, this, ConfigDomain::BESTCLIENT);

	Console()->Register("+fa", "", CFGFLAG_CLIENT, ConOpenFa, this, "Open Fast Actions selector");
	Console()->Register("+fa_execute_hover", "", CFGFLAG_CLIENT, ConFaExecuteHover, this, "Execute hovered Fast Actions bind");

	Console()->Register("fa", "i[index] s[name] s[command]", CFGFLAG_CLIENT, ConAddFaLegacy, this, "Set Fast Actions slot bind");
	Console()->Register("add_fa", "s[name] s[command]", CFGFLAG_CLIENT, ConAddFa, this, "Add a bind to Fast Actions");
	Console()->Register("remove_fa", "s[name] s[command]", CFGFLAG_CLIENT, ConRemoveFa, this, "Remove a bind from Fast Actions");
	Console()->Register("delete_all_fa_binds", "", CFGFLAG_CLIENT, ConRemoveAllFaBinds, this, "Removes all Fast Actions binds");
}

void CFastActions::OnReset()
{
	EnsureFixedBindSlots(m_vBinds);
	m_Active = false;
	m_SelectedBind = -1;
	m_DisplayBind = -1;
	m_AnimationTime = 0.0f;
}

void CFastActions::OnRelease()
{
	m_Active = false;
}

bool CFastActions::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	(void)x;
	(void)y;
	(void)CursorType;
	return false;
}

bool CFastActions::OnInput(const IInput::CEvent &Event)
{
	if(!g_Config.m_BcFastActions)
	{
		m_Active = false;
		m_SelectedBind = -1;
		m_DisplayBind = -1;
		return false;
	}

	if(IsActive() && Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_ESCAPE)
	{
		OnRelease();
		return true;
	}
	if(IsActive() && Event.m_Flags & IInput::FLAG_PRESS)
	{
		const int Slot = KeyToSlotIndex(Event.m_Key);
		if(Slot >= 0 && Slot < FAST_ACTIONS_FIXED_SLOTS)
		{
			m_SelectedBind = Slot;
			m_DisplayBind = Slot;
			return true;
		}
	}
	return false;
}

void CFastActions::OnRender()
{
	if(!g_Config.m_BcFastActions)
	{
		m_Active = false;
		m_SelectedBind = -1;
		m_DisplayBind = -1;
		m_AnimationTime = 0.0f;
		return;
	}

	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	static const auto QuadEaseInOut = [](float t) -> float {
		if(t == 0.0f)
			return 0.0f;
		if(t == 1.0f)
			return 1.0f;
		return (t < 0.5f) ? (2.0f * t * t) : (1.0f - std::pow(-2.0f * t + 2.0f, 2) / 2.0f);
	};

	static const float s_FontSize = 16.0f;

	const float AnimationTime = (float)g_Config.m_TcAnimateWheelTime / 1000.0f;
	const bool SelectedBindValid = m_SelectedBind >= 0 && m_SelectedBind < FAST_ACTIONS_FIXED_SLOTS;
	const bool ShouldBeVisible = m_Active && SelectedBindValid;
	std::array<float, 2> aAnimationPhase;
	if(AnimationTime <= 0.0f)
	{
		if(!ShouldBeVisible)
		{
			m_DisplayBind = -1;
			m_AnimationTime = 0.0f;
			return;
		}

		m_DisplayBind = m_SelectedBind;
		m_AnimationTime = 0.0f;
		aAnimationPhase.fill(1.0f);
	}
	else
	{
		// Re-trigger the appear animation whenever the displayed slot changes.
		if(ShouldBeVisible && m_DisplayBind != m_SelectedBind)
		{
			m_DisplayBind = m_SelectedBind;
			m_AnimationTime = 0.0f;
		}

		const float Delta = Client()->RenderFrameTime();
		if(ShouldBeVisible)
			m_AnimationTime = minimum(AnimationTime, m_AnimationTime + Delta);
		else
			m_AnimationTime = maximum(0.0f, m_AnimationTime - Delta);

		if(!ShouldBeVisible && m_AnimationTime <= 0.0f)
		{
			m_DisplayBind = -1;
			return;
		}

		if(m_DisplayBind < 0 || m_DisplayBind >= FAST_ACTIONS_FIXED_SLOTS)
			return;

		const float Progress = std::clamp(m_AnimationTime / AnimationTime, 0.0f, 1.0f);
		aAnimationPhase[0] = QuadEaseInOut(Progress);
		aAnimationPhase[1] = aAnimationPhase[0] * aAnimationPhase[0];
	}

	if(m_DisplayBind < 0 || m_DisplayBind >= FAST_ACTIONS_FIXED_SLOTS)
		return;

	const CUIRect Screen = *Ui()->Screen();

	Ui()->MapScreen();

	const CBind &SelectedBind = m_vBinds[m_DisplayBind];
	char aText[FAST_ACTIONS_MAX_CMD + 16];
	if(SelectedBind.m_aName[0] != '\0')
		str_copy(aText, SelectedBind.m_aName);
	else if(SelectedBind.m_aCommand[0] != '\0')
		str_copy(aText, SelectedBind.m_aCommand);
	else
		str_format(aText, sizeof(aText), Localize("Slot %d is empty"), m_DisplayBind + 1);

	const float TextWidth = TextRender()->TextWidth(s_FontSize, aText);
	const float BoxW = std::clamp(TextWidth + 52.0f, 180.0f, 680.0f) * aAnimationPhase[1];
	const float BoxH = 52.0f * aAnimationPhase[1];
	const float BoxX = Screen.w / 2.0f - BoxW / 2.0f;
	const float BoxY = Screen.h * 0.74f - BoxH / 2.0f;
	Graphics()->DrawRect(BoxX, BoxY, BoxW, BoxH, ColorRGBA(0.0f, 0.0f, 0.0f, 0.55f * aAnimationPhase[0]), IGraphics::CORNER_ALL, 12.0f);

	CUIRect TextRect{BoxX + 14.0f, BoxY + 6.0f, BoxW - 28.0f, BoxH - 12.0f};
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, aAnimationPhase[0]);
	Ui()->DoLabel(&TextRect, aText, s_FontSize * aAnimationPhase[1], TEXTALIGN_MC);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CFastActions::ExecuteBind(int Bind)
{
	if(!g_Config.m_BcFastActions)
		return;

	if(Bind >= 0 && Bind < FAST_ACTIONS_FIXED_SLOTS && m_vBinds[Bind].m_aCommand[0] != '\0')
	{
		const char *pCommand = m_vBinds[Bind].m_aCommand;
		if(pCommand[0] == '/')
		{
			char aBuf[FAST_ACTIONS_MAX_CMD * 2 + 16] = "";
			char *pEnd = aBuf + sizeof(aBuf);
			char *pDst;
			str_append(aBuf, "say \"");
			pDst = aBuf + str_length(aBuf);
			str_escape(&pDst, pCommand, pEnd);
			str_append(aBuf, "\"");
			Console()->ExecuteLine(aBuf, IConsole::CLIENT_ID_UNSPECIFIED);
		}
		else
		{
			Console()->ExecuteLine(pCommand, IConsole::CLIENT_ID_UNSPECIFIED);
		}
	}
}

void CFastActions::ExecuteHoveredBind()
{
	if(!g_Config.m_BcFastActions)
		return;

	if(m_SelectedBind >= 0)
		Console()->ExecuteLine(m_vBinds[m_SelectedBind].m_aCommand, IConsole::CLIENT_ID_UNSPECIFIED);
}

void CFastActions::ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData)
{
	CFastActions *pThis = (CFastActions *)pUserData;
	EnsureFixedBindSlots(pThis->m_vBinds);

	for(int i = 0; i < FAST_ACTIONS_FIXED_SLOTS; i++)
	{
		const CBind &Bind = pThis->m_vBinds[i];
		if(Bind.m_aCommand[0] == '\0')
			continue;

		char aBuf[FAST_ACTIONS_MAX_NAME * 2 + FAST_ACTIONS_MAX_CMD * 2 + 32] = "";
		char *pEnd = aBuf + sizeof(aBuf);
		char *pDst;
		str_format(aBuf, sizeof(aBuf), "fa %d \"", i);
		pDst = aBuf + str_length(aBuf);
		const char *pName = IsLegacySlotName(Bind.m_aName, i) ? "" : Bind.m_aName;
		str_escape(&pDst, pName, pEnd);
		str_append(aBuf, "\" \"");
		pDst = aBuf + str_length(aBuf);
		str_escape(&pDst, Bind.m_aCommand, pEnd);
		str_append(aBuf, "\"");
		pConfigManager->WriteLine(aBuf, ConfigDomain::BESTCLIENT);
	}
}
