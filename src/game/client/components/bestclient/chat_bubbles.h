/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_CHATBUBBLES_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_CHATBUBBLES_H

#include <base/color.h>

#include <game/client/component.h>
#include <game/client/components/chat.h>

constexpr float NameplateOffset = 10.0f;
constexpr float CharacterMinOffset = 40.0f;
constexpr float MarginBetween = 1.0f;
constexpr int CHAT_BUBBLES_MAX_LINE_LENGTH = 256;

struct CBubbles
{
	char m_aText[CHAT_BUBBLES_MAX_LINE_LENGTH] = "";
	char m_aRenderText[CHAT_BUBBLES_MAX_LINE_LENGTH] = "";
	int64_t m_Time = 0;
	int m_SourceClientId = -1;
	int m_Team = 0;

	STextContainerIndex m_TextContainerIndex;
	CTextCursor m_Cursor;
	vec2 m_RenderPos = vec2(0, 0);
	float m_OffsetY = 0.0f;
	float m_TargetOffsetY = 0.0f;
	float m_TextWidth = 0.0f;
	float m_TextHeight = 0.0f;
	ColorRGBA m_TextColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);

	CBubbles(const char *pText, CTextCursor pCursor, int64_t pTime, int SourceClientId, int Team)
	{
		str_copy(m_aText, pText, sizeof(m_aText));
		m_aRenderText[0] = '\0';
		m_Cursor = pCursor;
		m_Time = pTime;
		m_SourceClientId = SourceClientId;
		m_Team = Team;
		m_OffsetY = 0.0f;
		m_TargetOffsetY = 0.0f;
	}

	bool operator==(const CBubbles &Other) const
	{
		bool MatchText = str_comp(m_aText, Other.m_aText) == 0 && str_comp(m_aText, "") != 0;
		bool MatchTime = m_Time == Other.m_Time && m_Time > 0;
		return MatchText && MatchTime;
	}
};

class CChatBubbles : public CComponent
{
	CChat *Chat() const;

	std::vector<CBubbles> m_ChatBubbles[MAX_CLIENTS];
	STextContainerIndex m_InputTextContainerIndex;
	char m_aInputText[CHAT_BUBBLES_MAX_LINE_LENGTH] = "";
	int m_InputFontSize = 0;

	void RenderCurInput(float y);
	void RenderChatBubbles(int ClientId);

	float GetOffset(int ClientId);
	float GetAlpha(int64_t Time);

	void UpdateBubbleOffsets(int ClientId, float InputBubbleHeight = 0.0f);

	void AddBubble(int ClientId, int Team, const char *pText);
	void RemoveBubble(int ClientId, CBubbles Bubble);

	float ShiftBubbles(int ClientId, vec2 Pos, float w);

	void ExpireBubbles();
	int m_UseChatBubbles = 0;
	float m_InputTextWidth = 0.0f;
	float m_InputTextHeight = 0.0f;
	float m_InputBubbleHeight = 0.0f;

	void Reset();
	bool HasVisibleBubbles() const;
	CChat::CLine *FindChatLine(CBubbles &Bubble) const;
	std::string GetBubbleDisplayText(const CBubbles &Bubble, const CChat::CLine *pChatLine) const;
	void RefreshBubbleTextContainer(CBubbles &Bubble, int FontSize, const char *pText);
	void GetBubbleMediaSize(const CChat::CLine *pChatLine, int FontSize, float &PreviewWidth, float &PreviewHeight) const;

public:
	CChatBubbles();
	virtual ~CChatBubbles();
	virtual void OnMessage(int MsgType, void *pRawMsg) override;
	virtual int Sizeof() const override;
	virtual void OnRender() override;
	virtual void OnStateChange(int NewState, int OldState) override;

	virtual void OnWindowResize() override; // so it resets when font is changed
};

#endif
