#ifndef GAME_CLIENT_COMPONENTS_ENTITY_CHATBUBBLES_H
#define GAME_CLIENT_COMPONENTS_ENTITY_CHATBUBBLES_H
#include <game/client/component.h>
#include <game/client/components/chat.h>

constexpr float NameplateOffset = 10.0f;
constexpr float CharacterMinOffset = 40.0f;
constexpr float MarginBetween = 1.0f;

struct CBubbles
{
	char m_aText[MAX_LINE_LENGTH] = "";
	int64_t m_Time = 0;

	STextContainerIndex m_TextContainerIndex;
	CTextCursor m_Cursor;
	vec2 m_RenderPos = vec2(0, 0);
	float m_OffsetY = 0.0f;
	float m_TargetOffsetY = 0.0f;

	CBubbles(const char *pText, CTextCursor pCursor, int64_t pTime)
	{
		str_copy(m_aText, pText, sizeof(m_aText));
		m_Cursor = pCursor;
		m_Time = pTime;
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

	void RenderCurInput(float y);
	void RenderChatBubbles(int ClientId);

	float GetOffset(int ClientId);
	float GetAlpha(int64_t Time);

	void UpdateBubbleOffsets(int ClientId, float inputBubbleHeight = 0.0f);

	void AddBubble(int ClientId, int Team, const char *pText);
	void RemoveBubble(int ClientId, CBubbles Bubble);

	float ShiftBubbles(int ClientId, vec2 Pos, float w);

	void ExpireBubbles();
	int m_UseChatBubbles = 0;

	void Reset();

public:
	virtual void OnMessage(int MsgType, void *pRawMsg) override;
	virtual int Sizeof() const override { return sizeof(*this); }
	virtual void OnRender() override;
	virtual void OnStateChange(int NewState, int OldState) override;

	virtual void OnWindowResize() override; // so it resets when font is changed
};

#endif
