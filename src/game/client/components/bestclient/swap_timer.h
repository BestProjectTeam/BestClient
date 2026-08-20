/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_SWAP_TIMER_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_SWAP_TIMER_H

#include <engine/client/enums.h>
#include <engine/console.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>
#include <game/client/render.h>
#include <game/client/ui_rect.h>

#include <memory>

class CSwapTimer : public CComponent
{
	enum
	{
		MINIMAL_TEXT_SIZE = 64,
		ENTRY_TEXT_SIZE = 256,
	};

	static constexpr float REQUEST_COOLDOWN_SECONDS = 30.0f;
	static constexpr float REQUEST_TIMEOUT_SECONDS = 180.0f;
	static constexpr float HUD_CANVAS_HEIGHT = 300.0f;
	static constexpr float ANIMATION_TIME_SECONDS = 0.25f;

	enum class ECloseEvent
	{
		NONE,
		CANCEL_INCOMING,
		CANCEL_OUTGOING,
		COMPLETE,
	};

	struct SSwapEntry
	{
		bool m_Active = false;
		bool m_Incoming = false;
		char m_aOtherName[MAX_NAME_LENGTH] = {};
		float m_ExpireTime = 0.0f;
		float m_CooldownEnd = 0.0f;
		float m_AnimPhase = 0.0f;
		float m_AcceptPhase = 0.0f;
		bool m_Closing = false;
		bool m_FromDummy = false;
		std::shared_ptr<CManagedTeeRenderInfo> m_pSelfTee;
		std::shared_ptr<CManagedTeeRenderInfo> m_pOtherTee;
	};

	struct SHotkeyLayout
	{
		char m_aAcceptKey[64] = {};
		char m_aDeclineKey[64] = {};
		char m_aPeekKey[64] = {};
		float m_FontSize = 0.0f;
		float m_LeadGap = 0.0f;
		float m_IconGap = 0.0f;
		float m_PairGap = 0.0f;
		float m_Width = 0.0f;
		bool m_HasAny = false;
	};

	static void ConSwapAccept(IConsole::IResult *pResult, void *pUserData);
	static void ConSwapDecline(IConsole::IResult *pResult, void *pUserData);
	static void ConSwapPeek(IConsole::IResult *pResult, void *pUserData);

	SSwapEntry m_aEntries[NUM_DUMMIES];
	bool m_PeekHeld = false;
	int m_PeekClientId = -1;

	void ResetState();
	void ClearEntry(int Conn);
	void SetEntry(int Conn, const char *pName, bool Incoming, float CooldownSeconds);
	void CloseEntry(int Conn);
	void CloseOnConn(int Conn, bool OutgoingOnly);
	void CancelForPlayer(int ClientId);
	void ExpireEntries();
	void UpdateAnimations();

	int VisibleConn() const;
	bool HasActiveEntry() const;
	SSwapEntry *ActiveEntry(int Conn);
	const SSwapEntry *ActiveEntry(int Conn) const;

	float EntryAlpha(const SSwapEntry &Entry) const;
	float AcceptDimFactor(const SSwapEntry &Entry) const;
	bool IsAcceptEnabled(const SSwapEntry &Entry) const;
	bool IsCooldownActive(const SSwapEntry &Entry) const;
	bool IsOwnOtherTee(int Conn, const char *pName) const;
	const char *DisplayName(const SSwapEntry &Entry) const;
	SSwapEntry MakePreviewEntry(float Now) const;

	float HudCanvasWidth() const;
	float LineHeight(float Scale) const;

	void BuildHotkeyLayout(bool Show, float FontSize, float LeadGap, float IconGap, float PairGap, SHotkeyLayout &Out) const;
	void RenderHotkeyLayout(const SHotkeyLayout &Hotkeys, float X, float Y, float Alpha, float AcceptDim);

	void FormatStatusText(const SSwapEntry &Entry, float Now, char *pBuf, int BufSize) const;
	void FormatMinimalText(const SSwapEntry &Entry, float Now, char *pBuf, int BufSize) const;

	void UpdateTeeInfos(SSwapEntry &Entry, int Conn);
	float TeeIconSize(float FontSize) const;
	float RenderTeeIcon(const std::shared_ptr<CManagedTeeRenderInfo> &pTee, float X, float Y, float FontSize, float Alpha) const;
	float MeasureLineWidth(const SSwapEntry &Entry, float Scale, float Now) const;
	void RenderLine(const SSwapEntry &Entry, float X, float Y, float Scale, float Now, float Alpha, bool ShowHotkeys);
	void RenderNameplateMode();
	void RenderNameplateCard(const SSwapEntry &Entry, int ClientId, float Now);

	void StopPeek();
	void SetPeekHeld(bool Held);
	int FindClientByName(const char *pName) const;

	static void CopyName(const char *pStart, const char *pEnd, char *pBuf, int BufSize);
	static const char *SkipMessagePrefix(const char *pMessage);
	static bool ParseIncoming(const char *pMessage, char *pName, int NameSize, float *pCooldown);
	static bool ParseOutgoing(const char *pMessage, char *pName, int NameSize);
	static bool ParseAcceptWait(const char *pMessage, float *pSeconds);
	static ECloseEvent ParseCloseEvent(const char *pMessage, char *pFirst, char *pSecond, int NameSize);

public:
	int Sizeof() const override { return sizeof(*this); }

	void OnConsoleInit() override;
	void OnReset() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	void OnRender() override;

	void OnChatMessage(int ClientId, const char *pMessage, int Conn);
	void OnPlayerDeath(int ClientId);

	void AcceptSwap();
	void DeclineSwap();
	// Returns true if SpecInfo was overridden for peek this call.
	bool ApplyPeekSpectate();
	bool IsInputFrozen() const;

	CUIRect GetRect(bool ForcePreview) const;
	void Render(bool ForcePreview = false);
	CUIRect GetHudEditorRect() const { return GetRect(true); }
	void RenderPreview() { Render(true); }
};

#endif
