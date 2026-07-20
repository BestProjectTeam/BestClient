/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_CHAT_BUBBLES_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_CHAT_BUBBLES_H

#include <base/color.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/textrender.h>

#include <game/client/component.h>
#include <game/client/components/chat.h>

#include <vector>

constexpr float BcChatBubbleNameplateOffset = 10.0f;
constexpr float BcChatBubbleCharacterMinOffset = 40.0f;
constexpr float BcChatBubbleMarginBetween = 1.0f;

struct CBcChatBubble
{
	char m_aText[MAX_LINE_LENGTH] = "";
	int64_t m_Time = 0;
	ColorRGBA m_Color = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);

	STextContainerIndex m_TextContainerIndex;
	CTextCursor m_Cursor;
	float m_OffsetY = 0.0f;
	float m_TargetOffsetY = 0.0f;

	CBcChatBubble(const char *pText, CTextCursor Cursor, int64_t Time, ColorRGBA Color)
	{
		str_copy(m_aText, pText, sizeof(m_aText));
		m_Cursor = Cursor;
		m_Time = Time;
		m_Color = Color;
		m_OffsetY = 0.0f;
		m_TargetOffsetY = 0.0f;
	}
};

class CChatBubbles : public CComponent
{
	CChat *Chat() const;

	std::vector<CBcChatBubble> m_aChatBubbles[MAX_CLIENTS];

	void RenderCurInput(float Y);
	void RenderChatBubbles(int ClientId);

	float GetOffset(int ClientId) const;
	float GetAlpha(int64_t Time) const;

	void UpdateBubbleOffsets(int ClientId, float InputBubbleHeight = 0.0f);

	void AddBubble(int ClientId, int Team, const char *pText);

	void Reset();
	int m_UseChatBubbles = 0;

	bool LineHighlighted(int ClientId, const char *pLine) const;
	float BubbleRounding() const;

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnMessage(int MsgType, void *pRawMsg) override;
	void OnRender() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnWindowResize() override;
};

#endif // GAME_CLIENT_COMPONENTS_BESTCLIENT_CHAT_BUBBLES_H
