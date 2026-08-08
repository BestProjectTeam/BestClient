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
#include <vector>

class CSwapTimer : public CComponent
{
	enum
	{
		MAX_VISIBLE_CARDS = 4,
		MAX_ENTRIES_PER_CONN = 16,
		MINIMAL_TEXT_SIZE = 64,
		ENTRY_TEXT_SIZE = 256,
	};

	static constexpr float REQUEST_COOLDOWN_SECONDS = 30.0f;
	static constexpr float REQUEST_TIMEOUT_SECONDS = 180.0f;
	static constexpr float HUD_CANVAS_HEIGHT = 300.0f;

	enum class ECloseEvent
	{
		NONE,
		CANCEL_INCOMING,
		CANCEL_OUTGOING,
		COMPLETE,
	};

	struct SSwapEntry
	{
		bool m_Incoming = false;
		char m_aOtherName[MAX_NAME_LENGTH] = {};
		float m_ExpireTime = 0.0f;
		float m_CooldownEnd = 0.0f;
		float m_AnimPhase = 0.0f;
		float m_AcceptPhase = 0.0f;
		bool m_Closing = false;
		bool m_FromDummy = false; // paired with the entry mirrored the other connection
		std::shared_ptr<CManagedTeeRenderInfo> m_pSelfTee;
		std::shared_ptr<CManagedTeeRenderInfo> m_pOtherTee;
	};

	struct SVisibleCard
	{
		int m_Index = 0;
		float m_Slot = 0.0f;
		float m_Distance = 0.0f;
		float m_Alpha = 1.0f;
		float m_Squeeze = 1.0f;
		bool m_Selected = false;
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

	enum class EPeekStage
	{
		IDLE,
		TRAVEL_TO,
		HOLD,
		TRAVEL_BACK,
	};

	static void ConSwapAccept(IConsole::IResult *pResult, void *pUserData);
	static void ConSwapDecline(IConsole::IResult *pResult, void *pUserData);
	static void ConSwapNext(IConsole::IResult *pResult, void *pUserData);
	static void ConSwapPrev(IConsole::IResult *pResult, void *pUserData);
	static void ConSwapPeek(IConsole::IResult *pResult, void *pUserData);

	std::vector<SSwapEntry> m_avEntries[NUM_DUMMIES];
	int m_aSelected[NUM_DUMMIES] = {};
	float m_aScrollAnim[NUM_DUMMIES] = {};

	EPeekStage m_PeekStage = EPeekStage::IDLE;
	int m_PeekClientId = -1;
	float m_PeekStageStart = 0.0f;
	vec2 m_PeekOrigin = vec2(0.0f, 0.0f);

	void ResetState();
	void AddEntry(int Conn, const char *pName, bool Incoming, float CooldownSeconds);
	void RemoveEntryAt(int Conn, int Index);
	void CloseEntryAt(int Conn, int Index);
	void CloseOnConn(int Conn, bool OutgoingOnly);
	void CancelForPlayer(int ClientId);
	void ExpireEntries();
	void UpdateAnimations();
	void ClampSelection(int Conn);
	void CycleSelection(int Step);

	int EntryCount(int Conn) const;
	int FindEntry(int Conn, const char *pName, bool Incoming) const;
	int FindPairedEntry(int Conn, bool Incoming) const;
	int VisibleConn() const;
	int SelectedEntry(int Conn) const;
	bool HasActiveEntry() const;

	float EntryAlpha(const SSwapEntry &Entry) const;
	float AcceptDimFactor(const SSwapEntry &Entry) const;
	bool IsAcceptEnabled(const SSwapEntry &Entry) const;
	bool IsCooldownActive(const SSwapEntry &Entry) const;
	bool IsOwnOtherTee(int Conn, const char *pName) const;
	const char *DisplayName(const SSwapEntry &Entry) const;
	SSwapEntry MakePreviewEntry(float Now) const;

	float HudCanvasWidth() const;
	float LineHeight(float Scale) const;
	int VisibleSlotCount(int Conn) const;
	float WindowStart(int Conn) const;
	int CollectVisibleCards(int Conn, SVisibleCard *pOut) const;

	void BuildHotkeyLayout(bool Show, float FontSize, float LeadGap, float IconGap, float PairGap, SHotkeyLayout &Out) const;
	void RenderHotkeyLayout(const SHotkeyLayout &Hotkeys, float X, float Y, float Alpha, float AcceptDim);

	void FormatStatusText(const SSwapEntry &Entry, float Now, char *pBuf, int BufSize) const;
	void FormatMinimalText(const SSwapEntry &Entry, float Now, char *pBuf, int BufSize) const;
	void FormatEntryText(const SSwapEntry &Entry, float Now, char *pBuf, int BufSize) const;
	static void FormatPlaceholders(const char *pFormat, int Seconds, const char *pSelf, const char *pOther, char *pBuf, int BufSize);

	void UpdateTeeInfos(SSwapEntry &Entry, int Conn);
	float TeeIconSize(float FontSize) const;
	float RenderTeeIcon(const std::shared_ptr<CManagedTeeRenderInfo> &pTee, float X, float Y, float FontSize, float Alpha) const;
	float MeasureLineWidth(const SSwapEntry &Entry, float Scale, float Now) const;
	void RenderLine(const SSwapEntry &Entry, float X, float Y, float Scale, float Now, float Alpha, bool Selected, bool ShowHotkeys);
	void RenderNameplateMode();
	void RenderNameplateCard(const SSwapEntry &Entry, int ClientId, float Now, const SVisibleCard &Card);

	void StopPeek();
	int FindClientByName(const char *pName) const;

	static void CopyName(const char *pStart, const char *pEnd, char *pBuf, int BufSize);
	static const char *SkipMessagePrefix(const char *pMessage);
	static bool ParseIncoming(const char *pMessage, char *pName, int NameSize, float *pCooldown);
	static bool ParseOutgoing(const char *pMessage, char *pName, int NameSize);
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
	void SelectNext();
	void SelectPrev();
	void PeekPartner();
	void ApplyPeekCamera();
	bool IsInputFrozen() const;

	CUIRect GetRect(bool ForcePreview) const;
	void Render(bool ForcePreview = false);
	CUIRect GetHudEditorRect() const { return GetRect(true); }
	void RenderPreview() { Render(true); }
};

#endif
