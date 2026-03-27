#ifndef GAME_CLIENT_COMPONENTS_RCLIENT_ADMINPANEL_H
#define GAME_CLIENT_COMPONENTS_RCLIENT_ADMINPANEL_H

#include <game/client/component.h>

#include <engine/console.h>
class CAdminPanel : public CComponent
{
	bool m_Active = false;

	static void ConToggleAdminPanel(IConsole::IResult *pResult, void *pUserData);

	vec2 m_PlayerScreenPos;
	vec2 m_ClosestScreenPlayerPos;
	int m_HoveredPlayerId = -1;
	int m_ChosenActionButton = 0; //1-kill 2-kick 3-mute 4-local ban
	int m_ChosenTimerButton = -1;
	CLineInput m_AdminInput; // for reason
	char m_Input[128];
	CLineInputNumber m_AdminTimers; // for time
	int m_InputTimers = 0;
	char m_InputCharTimers[128];
	void *m_pActiveItem = nullptr;
	bool m_ActiveItemValid = false;
	int m_LastConfirm = 0;
	int m_ReadyButtons = 0;
	bool m_WasSpecActive = false;

	// int64_t m_ClickTime;

	void RIReset()
	{
		m_PlayerScreenPos = vec2(0, 0);
		m_ClosestScreenPlayerPos = vec2(0, 0);
		m_HoveredPlayerId = -1;
		m_ChosenActionButton = 0; //1-kill 2-kick 3-local ban 4-mute
		m_ChosenTimerButton = -1;
		m_InputTimers = 0;
		m_InputCharTimers[0] = '\0';
		m_Input[0] = '\0';
		m_pActiveItem = nullptr;
		m_ActiveItemValid = false;
		m_PlayerList.reset();
		m_LastConfirm = 0;
		m_ReadyButtons = 0;
	}

	struct SMouseState
	{
		bool m_Unlocked = false;
		bool m_Clicked = false;
		bool m_LastMouseInput = false;
		bool m_MouseInput = false;
		vec2 m_Position{0, 0};
		float m_LastClickTime = 0.0f;
		float m_ClickCooldown = 0.1f; // 100ms
		bool m_IsDragging = false;
		vec2 m_DragStart{0, 0};

		void reset()
		{
			m_Unlocked = false;
			m_Clicked = false;
			m_LastMouseInput = false;
			m_MouseInput = false;
			m_IsDragging = false;
			m_LastClickTime = 0.0f;
		}

		void clampPosition(float ScreenWidth, float ScreenHeight)
		{
			m_Position.x = std::clamp(m_Position.x, 0.0f, ScreenWidth - 1.0f);
			m_Position.y = std::clamp(m_Position.y, 0.0f, ScreenHeight - 1.0f);
		}

		bool canClick(IClient *pClient) const
		{
			return m_Unlocked && !m_IsDragging && (pClient->LocalTime() - m_LastClickTime) > m_ClickCooldown;
		}
	} m_Mouse;

	struct SPlayerPopup
	{
		bool m_Visible = false;
		CUIRect m_Rect;
		vec2 m_Position{0, 0};
		int m_PlayerId = -1;
		float m_LastButtonPressTime = 0.0f;
		float m_ButtonCooldown = 0.2f; // 200ms cooldown between button presses
		bool m_IsInteracting = false;

		void reset()
		{
			m_Visible = false;
			m_Position = {0, 0};
			m_PlayerId = -1;
			m_LastButtonPressTime = 0.0f;
			m_IsInteracting = false;
		}

		void toggle(bool Show, vec2 Pos = {0, 0}, int Id = -1)
		{
			m_Visible = Show;
			if(Show)
			{
				m_Position = Pos;
				m_PlayerId = Id;
				m_IsInteracting = false;
			}
		}

		bool shouldHide(const SMouseState &Mouse, bool PlayerHovered) const
		{
			return (!PlayerHovered && Mouse.m_Clicked && !m_IsInteracting) ||
			       !Mouse.m_Unlocked;
		}

		bool canInteract(IClient *pClient) const
		{
			return m_Visible && (pClient->LocalTime() - m_LastButtonPressTime) > m_ButtonCooldown;
		}
	} m_Popup;

	struct SPlayerList
	{
		bool m_Active = false;
		int m_ColumnCount = 0;
		void reset()
		{
			m_ColumnCount = 0;
			m_Active = false;
		}
	} m_PlayerList;

	void ResetState()
	{
		m_Mouse.reset();
		m_Mouse.m_Position = vec2(0, 0);
		m_Mouse.m_DragStart = vec2(0, 0);
		m_Popup.reset();
		RIReset();
		m_Active = false;
		m_WasSpecActive = false;
	}

	void DoIconLabeledButton(CUIRect *pRect, const char *pTitle, const char *pIcon, float TextSize, float Height, ColorRGBA IconColor) const;
	void DoIconLabeledButtonDown(CUIRect *pRect, const char *pTitle, const char *pIcon, float IconSize, float TextSize, float Height, float Dif, ColorRGBA IconColor) const;
	void DoLabelLabeledButtonDown(CUIRect *pRect, const char *pTitleDown, const char *pTitle, float TextSize, float TextSizeDown, float Height, float Dif) const;
	void DoIconButton(CUIRect *pRect, const char *pIcon, float TextSize, ColorRGBA IconColor) const;

	void RenderPlayerPanelPopUp();
	void RenderPlayerPanelPopUpActionButtons(CUIRect *pBase);
	void RenderPlayerPanelPopUpReadyButtons(CUIRect *pBase);
	void RenderPlayerPanelPopUpTimers(CUIRect *pBase);
	void RenderPlayerPanelPopUpInputs(CUIRect *pBase);
	bool DoEditBox(CLineInput *pLineInput, const CUIRect *pRect, float FontSize, ColorRGBA ColorHov, ColorRGBA ColorElse);
	void RenderPlayerPanelPopUpCommand(CUIRect *pBase);
	void RenderPlayerPanelPopUpLastConfirm(CUIRect *pBase);
	void RenderPlayerPanelPlayersList();

	bool Hovered(const CUIRect *pRect) const
	{
		return m_Mouse.m_Unlocked && pRect->Inside(m_Mouse.m_Position);
	}

	bool DoButtonLogic(const CUIRect *pRect)
	{
		if(!m_Mouse.canClick(Client()))
			return false;

		bool Hovered = pRect->Inside(m_Mouse.m_Position);
		bool Clicked = Hovered && m_Mouse.m_Clicked;

		if(Clicked)
		{
			m_Mouse.m_LastClickTime = Client()->LocalTime();
			m_Popup.m_LastButtonPressTime = Client()->LocalTime();
			m_Popup.m_IsInteracting = true;
		}

		return Clicked;
	}

	bool CheckActiveItem(const void *pId)
	{
		if(m_pActiveItem == pId)
		{
			m_ActiveItemValid = true;
			return true;
		}
		return false;
	}

public:
	CAdminPanel();
	int Sizeof() const override { return sizeof(*this); }

	void SetActive(bool Active);

	void OnReset() override;
	void OnRender() override;
	void OnConsoleInit() override;
	void OnRelease() override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	bool OnInput(const IInput::CEvent &Event) override;

	bool IsActive() const;
	bool IsActivePopup() const;
	bool IsActivePlrList() const;
};

#endif
