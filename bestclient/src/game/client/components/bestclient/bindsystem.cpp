#include "bindsystem.h"

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
void EnsureFixedBindSlots(std::vector<CBindSystem::CBind> &vBinds)
{
	if(vBinds.size() != BINDSYSTEM_FIXED_SLOTS)
		vBinds.resize(BINDSYSTEM_FIXED_SLOTS);
	for(int i = 0; i < BINDSYSTEM_FIXED_SLOTS; i++)
	{
		char aName[BINDSYSTEM_MAX_NAME];
		str_format(aName, sizeof(aName), "%d", i + 1);
		str_copy(vBinds[i].m_aName, aName);
	}
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
	return Index >= 0 && Index < BINDSYSTEM_FIXED_SLOTS ? Index : -1;
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

CBindSystem::CBindSystem()
{
	OnReset();
}

void CBindSystem::ConBsExecuteHover(IConsole::IResult *pResult, void *pUserData)
{
	CBindSystem *pThis = (CBindSystem *)pUserData;
	pThis->ExecuteHoveredBind();
}

void CBindSystem::ConOpenBs(IConsole::IResult *pResult, void *pUserData)
{
	CBindSystem *pThis = (CBindSystem *)pUserData;
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

void CBindSystem::ConAddBsLegacy(IConsole::IResult *pResult, void *pUserData)
{
	int BindPos = pResult->GetInteger(0);
	if(BindPos < 0 || BindPos >= BINDSYSTEM_FIXED_SLOTS)
		return;

	const char *aCommand = pResult->GetString(2);

	CBindSystem *pThis = static_cast<CBindSystem *>(pUserData);
	EnsureFixedBindSlots(pThis->m_vBinds);
	str_copy(pThis->m_vBinds[BindPos].m_aCommand, aCommand);
}

void CBindSystem::ConAddBs(IConsole::IResult *pResult, void *pUserData)
{
	const char *aName = pResult->GetString(0);
	const char *aCommand = pResult->GetString(1);

	CBindSystem *pThis = static_cast<CBindSystem *>(pUserData);
	pThis->AddBind(aName, aCommand);
}

void CBindSystem::ConRemoveBs(IConsole::IResult *pResult, void *pUserData)
{
	const char *aName = pResult->GetString(0);
	const char *aCommand = pResult->GetString(1);

	CBindSystem *pThis = static_cast<CBindSystem *>(pUserData);
	pThis->RemoveBind(aName, aCommand);
}

void CBindSystem::ConRemoveAllBsBinds(IConsole::IResult *pResult, void *pUserData)
{
	CBindSystem *pThis = static_cast<CBindSystem *>(pUserData);
	pThis->RemoveAllBinds();
}

void CBindSystem::AddBind(const char *pName, const char *pCommand)
{
	EnsureFixedBindSlots(m_vBinds);
	if(pCommand[0] == '\0')
		return;

	int Slot = SlotFromName(pName);
	if(Slot < 0)
	{
		for(int i = 0; i < BINDSYSTEM_FIXED_SLOTS; i++)
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

	str_copy(m_vBinds[Slot].m_aCommand, pCommand);
}

void CBindSystem::RemoveBind(const char *pName, const char *pCommand)
{
	EnsureFixedBindSlots(m_vBinds);
	const int Slot = SlotFromName(pName);
	if(Slot >= 0)
	{
		if(pCommand[0] == '\0' || str_comp(m_vBinds[Slot].m_aCommand, pCommand) == 0)
			m_vBinds[Slot].m_aCommand[0] = '\0';
		return;
	}

	for(int i = 0; i < BINDSYSTEM_FIXED_SLOTS; i++)
	{
		if(str_comp(m_vBinds[i].m_aCommand, pCommand) == 0)
		{
			m_vBinds[i].m_aCommand[0] = '\0';
			return;
		}
	}
}

void CBindSystem::RemoveBind(int Index)
{
	EnsureFixedBindSlots(m_vBinds);
	if(Index >= BINDSYSTEM_FIXED_SLOTS || Index < 0)
		return;
	m_vBinds[Index].m_aCommand[0] = '\0';
}

void CBindSystem::RemoveAllBinds()
{
	EnsureFixedBindSlots(m_vBinds);
	for(int i = 0; i < BINDSYSTEM_FIXED_SLOTS; i++)
		m_vBinds[i].m_aCommand[0] = '\0';
}

void CBindSystem::OnConsoleInit()
{
	EnsureFixedBindSlots(m_vBinds);

	IConfigManager *pConfigManager = Kernel()->RequestInterface<IConfigManager>();
	if(pConfigManager)
		pConfigManager->RegisterCallback(ConfigSaveCallback, this, ConfigDomain::BestClient);

	Console()->Register("+bs", "", CFGFLAG_CLIENT, ConOpenBs, this, "Open BindSystem selector");
	Console()->Register("+bs_execute_hover", "", CFGFLAG_CLIENT, ConBsExecuteHover, this, "Execute hovered BindSystem bind");

	Console()->Register("bs", "i[index] s[name] s[command]", CFGFLAG_CLIENT, ConAddBsLegacy, this, "Set BindSystem slot bind");
	Console()->Register("add_bs", "s[name] s[command]", CFGFLAG_CLIENT, ConAddBs, this, "Add a bind to BindSystem");
	Console()->Register("remove_bs", "s[name] s[command]", CFGFLAG_CLIENT, ConRemoveBs, this, "Remove a bind from BindSystem");
	Console()->Register("delete_all_bs_binds", "", CFGFLAG_CLIENT, ConRemoveAllBsBinds, this, "Removes all BindSystem binds");

	// Legacy aliases for compatibility.
	Console()->Register("+bindwheel", "", CFGFLAG_CLIENT, ConOpenBs, this, "Open BindSystem selector (legacy)");
	Console()->Register("+bindwheel_execute_hover", "", CFGFLAG_CLIENT, ConBsExecuteHover, this, "Execute hovered BindSystem bind (legacy)");
	Console()->Register("bindwheel", "i[index] s[name] s[command]", CFGFLAG_CLIENT, ConAddBsLegacy, this, "Legacy alias for bs");
	Console()->Register("add_bindwheel", "s[name] s[command]", CFGFLAG_CLIENT, ConAddBs, this, "Legacy alias for add_bs");
	Console()->Register("remove_bindwheel", "s[name] s[command]", CFGFLAG_CLIENT, ConRemoveBs, this, "Legacy alias for remove_bs");
	Console()->Register("delete_all_bindwheel_binds", "", CFGFLAG_CLIENT, ConRemoveAllBsBinds, this, "Legacy alias for delete_all_bs_binds");
}

void CBindSystem::OnReset()
{
	EnsureFixedBindSlots(m_vBinds);
	m_WasActive = false;
	m_Active = false;
	m_SelectedBind = -1;
	m_DisplayBind = -1;
	m_AnimationTime = 0.0f;
}

void CBindSystem::OnRelease()
{
	m_Active = false;
}

bool CBindSystem::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	(void)x;
	(void)y;
	(void)CursorType;
	return false;
}

bool CBindSystem::OnInput(const IInput::CEvent &Event)
{
	if(GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_RACE_BINDSYSTEM))
	{
		if(m_Active)
			OnRelease();
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
		if(Slot >= 0 && Slot < BINDSYSTEM_FIXED_SLOTS)
		{
			m_SelectedBind = Slot;
			m_DisplayBind = Slot;
			return true;
		}
	}
	return false;
}

void CBindSystem::OnRender()
{
	if(GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_RACE_BINDSYSTEM))
	{
		m_Active = false;
		m_WasActive = false;
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

	constexpr float AnimationTime = 0.150f;
	const bool SelectedBindValid = m_SelectedBind >= 0 && m_SelectedBind < BINDSYSTEM_FIXED_SLOTS;
	const bool ShouldBeVisible = m_Active && SelectedBindValid;
	std::array<float, 2> aAnimationPhase;
	if(AnimationTime <= 0.0f)
	{
		if(!ShouldBeVisible)
		{
			m_WasActive = false;
			m_DisplayBind = -1;
			m_AnimationTime = 0.0f;
			return;
		}

		m_DisplayBind = m_SelectedBind;
		m_WasActive = true;
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
			m_WasActive = false;
			m_DisplayBind = -1;
			return;
		}

		if(m_DisplayBind < 0 || m_DisplayBind >= BINDSYSTEM_FIXED_SLOTS)
			return;

		m_WasActive = true;

		const float Progress = std::clamp(m_AnimationTime / AnimationTime, 0.0f, 1.0f);
		aAnimationPhase[0] = QuadEaseInOut(Progress);
		aAnimationPhase[1] = aAnimationPhase[0] * aAnimationPhase[0];
	}

	if(m_DisplayBind < 0 || m_DisplayBind >= BINDSYSTEM_FIXED_SLOTS)
		return;

	const CUIRect Screen = *Ui()->Screen();

	Ui()->MapScreen();

	const CBind &SelectedBind = m_vBinds[m_DisplayBind];
	char aText[BINDSYSTEM_MAX_CMD + 16];
	if(SelectedBind.m_aCommand[0] != '\0')
		str_copy(aText, SelectedBind.m_aCommand);
	else
		str_format(aText, sizeof(aText), TCLocalize("Slot %s is empty"), SelectedBind.m_aName);

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

void CBindSystem::ExecuteBind(int Bind)
{
	if(Bind >= 0 && Bind < BINDSYSTEM_FIXED_SLOTS && m_vBinds[Bind].m_aCommand[0] != '\0')
	{
		const char *pCommand = m_vBinds[Bind].m_aCommand;
		if(pCommand[0] == '/')
		{
			char aBuf[BINDSYSTEM_MAX_CMD * 2 + 16] = "";
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

void CBindSystem::ExecuteHoveredBind()
{
	if(m_SelectedBind >= 0)
			Console()->ExecuteLine(m_vBinds[m_SelectedBind].m_aCommand, IConsole::CLIENT_ID_UNSPECIFIED);
}

void CBindSystem::ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData)
{
	CBindSystem *pThis = (CBindSystem *)pUserData;
	EnsureFixedBindSlots(pThis->m_vBinds);

	for(int i = 0; i < BINDSYSTEM_FIXED_SLOTS; i++)
	{
		const CBind &Bind = pThis->m_vBinds[i];
		if(Bind.m_aCommand[0] == '\0')
			continue;

		char aBuf[BINDSYSTEM_MAX_CMD * 2] = "";
		char *pEnd = aBuf + sizeof(aBuf);
		char *pDst;
		char aSlotName[16];
		str_format(aSlotName, sizeof(aSlotName), "%d", i + 1);
		str_format(aBuf, sizeof(aBuf), "bs %d \"%s\" \"", i, aSlotName);
		pDst = aBuf + str_length(aBuf);
		str_escape(&pDst, Bind.m_aCommand, pEnd);
		str_append(aBuf, "\"");
		pConfigManager->WriteLine(aBuf, ConfigDomain::BestClient);
	}
}
