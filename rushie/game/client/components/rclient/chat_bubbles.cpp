#include <engine/client.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>
#include <engine/textrender.h>

#include <game/client/components/chat.h>
#include <game/client/gameclient.h>

#include <base/system.h>
#include <base/vmath.h>

#include "chat_bubbles.h"

CChat *CChatBubbles::Chat() const
{
	return &GameClient()->m_Chat;
}

float CChatBubbles::GetOffset(int ClientId)
{
	float Offset = GameClient()->m_NamePlates.GetNamePlateOffset(ClientId) + NameplateOffset;
	if(Offset < CharacterMinOffset)
		Offset = CharacterMinOffset;

	return Offset;
}

void CChatBubbles::OnMessage(int MsgType, void *pRawMsg)
{
	if(GameClient()->m_SuppressEvents)
		return;

	if(!g_Config.m_RiChatBubbles)
		return;

	if(MsgType == NETMSGTYPE_SV_CHAT)
	{
		CNetMsg_Sv_Chat *pMsg = (CNetMsg_Sv_Chat *)pRawMsg;
		if(g_Config.m_TcRegexChatIgnore[0] && g_Config.m_RiEnableCensorList)
		{
			const char *pMessage = GameClient()->m_Chat.FilterText(pMsg->m_pMessage);
			AddBubble(pMsg->m_ClientId, pMsg->m_Team, pMessage);
		}
		else
			AddBubble(pMsg->m_ClientId, pMsg->m_Team, pMsg->m_pMessage);
	}
}

void CChatBubbles::UpdateBubbleOffsets(int ClientId, float inputBubbleHeight)
{
	float Offset = 0.0f;
	if(inputBubbleHeight > 0.0f)
		Offset += inputBubbleHeight + MarginBetween;

	int FontSize = g_Config.m_RiChatBubbleSize;
	for(CBubbles &aBubble : m_ChatBubbles[ClientId])
	{
		if(!aBubble.m_TextContainerIndex.Valid() || aBubble.m_Cursor.m_FontSize != FontSize)
		{
			if(aBubble.m_TextContainerIndex.Valid())
			{
				TextRender()->DeleteTextContainer(aBubble.m_TextContainerIndex);
				aBubble.m_TextContainerIndex = STextContainerIndex();
			}

			CTextCursor Cursor;
			Cursor.SetPosition(vec2(0, 0));
			Cursor.m_FontSize = FontSize;
			Cursor.m_Flags = TEXTFLAG_RENDER;

			Cursor.m_LineWidth = 500.0f - FontSize * 2.0f;
			TextRender()->CreateOrAppendTextContainer(aBubble.m_TextContainerIndex, &Cursor, aBubble.m_aText);
			aBubble.m_Cursor.m_FontSize = FontSize;

			TextRender()->ColorParsing(aBubble.m_aText, &Cursor, ColorRGBA(1.0f, 1.0f, 1.0f), &aBubble.m_TextContainerIndex);
		}
		STextBoundingBox BoundingBox = TextRender()->GetBoundingBoxTextContainer(aBubble.m_TextContainerIndex);
		aBubble.m_TargetOffsetY = Offset;
		Offset += BoundingBox.m_H + FontSize + MarginBetween;
	}
}

void CChatBubbles::AddBubble(int ClientId, int Team, const char *pText)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !pText)
		return;

	if(*pText == 0)
		return;
	if(GameClient()->m_aClients[ClientId].m_aName[0] == '\0')
		return;
	if(GameClient()->m_aClients[ClientId].m_ChatIgnore)
		return;
	if(GameClient()->m_Snap.m_LocalClientId != ClientId)
	{
		if(g_Config.m_ClShowChatFriends && !GameClient()->m_aClients[ClientId].m_Friend)
			return;
		if(g_Config.m_ClShowChatTeamMembersOnly && GameClient()->IsOtherTeam(ClientId) && GameClient()->m_Teams.Team(GameClient()->m_Snap.m_LocalClientId) != TEAM_FLOCK)
			return;
		if(GameClient()->m_aClients[ClientId].m_Foe)
			return;
	}

	int FontSize = g_Config.m_RiChatBubbleSize;
	CTextCursor pCursor;

	// Create Text at default zoom
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	Graphics()->MapScreenToInterface(GameClient()->m_Camera.m_Center.x, GameClient()->m_Camera.m_Center.y);

	pCursor.SetPosition(vec2(0, 0));
	pCursor.m_FontSize = FontSize;
	pCursor.m_Flags = TEXTFLAG_RENDER;
	pCursor.m_LineWidth = 500.0f - FontSize * 2.0f;

	CBubbles bubble(pText, pCursor, time_get());

	ColorRGBA Color = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	if(Chat()->LineHighlighted(ClientId, pText))
		Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageHighlightColor));
	else if(Team == 1)
		Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageTeamColor));
	else if(Team == TEAM_WHISPER_RECV)
		Color = ColorRGBA(1.0f, 0.5f, 0.5f, 1.0f);
	else if(Team == TEAM_WHISPER_SEND)
	{
		Color = ColorRGBA(0.7f, 0.7f, 1.0f, 1.0f);
		ClientId = GameClient()->m_Snap.m_LocalClientId; // Set ClientId to local client for whisper send
	}
	else // regular message
		Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageColor));

	TextRender()->TextColor(Color);

	TextRender()->ColorParsing(pText, &pCursor, Color, &bubble.m_TextContainerIndex);

	m_ChatBubbles[ClientId].insert(m_ChatBubbles[ClientId].begin(), bubble);

	UpdateBubbleOffsets(ClientId);
	Graphics()->MapScreen(ScreenX0, ScreenY0, ScreenX1, ScreenY1);
}

void CChatBubbles::RemoveBubble(int ClientId, CBubbles aBubble)
{
	for(auto it = m_ChatBubbles[ClientId].begin(); it != m_ChatBubbles[ClientId].end(); ++it)
	{
		if(*it == aBubble)
		{
			TextRender()->DeleteTextContainer(it->m_TextContainerIndex);
			m_ChatBubbles[ClientId].erase(it);
			UpdateBubbleOffsets(ClientId);
			return;
		}
	}
}

void CChatBubbles::RenderCurInput(float y)
{
	int FontSize = g_Config.m_RiChatBubbleSize;
	const char *pText = Chat()->m_Input.GetString();
	int LocalId = GameClient()->m_Snap.m_LocalClientId;
	vec2 Position = GameClient()->m_aClients[LocalId].m_RenderPos;

	CTextCursor pCursor;
	STextContainerIndex TextContainerIndex;

	// Create Text at default zoom
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	Graphics()->MapScreenToInterface(GameClient()->m_Camera.m_Center.x, GameClient()->m_Camera.m_Center.y);

	pCursor.SetPosition(vec2(0, 0));
	pCursor.m_FontSize = FontSize;
	pCursor.m_Flags = TEXTFLAG_RENDER;
	pCursor.m_LineWidth = 500.0f - FontSize * 2.0f;
	TextRender()->CreateOrAppendTextContainer(TextContainerIndex, &pCursor, pText);

	Graphics()->MapScreen(ScreenX0, ScreenY0, ScreenX1, ScreenY1);

	if(TextContainerIndex.Valid())
	{
		STextBoundingBox BoundingBox = TextRender()->GetBoundingBoxTextContainer(TextContainerIndex);

		Position.x -= BoundingBox.m_W / 2.0f + g_Config.m_RiChatBubbleSize / 15.0f;
		float inputBubbleHeight = BoundingBox.m_H + FontSize;

		float targetY = y - inputBubbleHeight;

		Graphics()->DrawRect(Position.x - FontSize / 2.0f, targetY - FontSize / 2.0f,
			BoundingBox.m_W + FontSize * 1.20f, BoundingBox.m_H + FontSize,
			ColorRGBA(0.0f, 0.0f, 0.0f, 0.15f), IGraphics::CORNER_ALL, g_Config.m_RiChatBubbleSize / 4.5f);

		TextRender()->RenderTextContainer(TextContainerIndex, ColorRGBA(1.0f, 1.0f, 1.0f, 0.75f), ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), Position.x, targetY);

		UpdateBubbleOffsets(LocalId, inputBubbleHeight);

		y -= inputBubbleHeight + MarginBetween;
	}
	else
		UpdateBubbleOffsets(LocalId);
	TextRender()->DeleteTextContainer(TextContainerIndex);
}

void CChatBubbles::RenderChatBubbles(int ClientId)
{
	if(!GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
		return;

	if(!g_Config.m_RiChatBubblesSelf && (ClientId == GameClient()->m_Snap.m_LocalClientId))
		return;

	if(Client()->State() == IClient::STATE_DEMOPLAYBACK && !g_Config.m_RiChatBubblesDemo)
		return;

	const float ShowTime = g_Config.m_RiChatBubbleShowTime / 100.0f;
	int FontSize = g_Config.m_RiChatBubbleSize;
	vec2 Position = GameClient()->m_aClients[ClientId].m_RenderPos;
	float BaseY = Position.y - GetOffset(ClientId) - NameplateOffset;

	if(ClientId == GameClient()->m_Snap.m_LocalClientId)
		RenderCurInput(BaseY);

	// First pass: collect expired bubbles and clean up text containers
	bool bRemovedAny = false;
	for(auto it = m_ChatBubbles[ClientId].begin(); it != m_ChatBubbles[ClientId].end();)
	{
		CBubbles &aBubble = *it;

		if(aBubble.m_Time + time_freq() * ShowTime < time_get())
		{
			// Clean up text container before removal
			if(aBubble.m_TextContainerIndex.Valid())
				TextRender()->DeleteTextContainer(aBubble.m_TextContainerIndex);
			it = m_ChatBubbles[ClientId].erase(it);
			bRemovedAny = true;
			continue;
		}
		++it;
	}

	// Update offsets only once if we removed any bubbles
	if(bRemovedAny)
		UpdateBubbleOffsets(ClientId);

	// Second pass: render remaining bubbles
	for(CBubbles &aBubble : m_ChatBubbles[ClientId])
	{
		float Alpha = 1.0f;
		if(GameClient()->IsOtherTeam(ClientId))
			Alpha = g_Config.m_ClShowOthersAlpha / 100.0f;

		Alpha *= GetAlpha(aBubble.m_Time);

		if(Alpha <= 0.01f)
			continue;

		aBubble.m_OffsetY += (aBubble.m_TargetOffsetY - aBubble.m_OffsetY) * 0.05f / 10.0f;

		if(!aBubble.m_TextContainerIndex.Valid() || aBubble.m_Cursor.m_FontSize != FontSize)
		{
			if(aBubble.m_TextContainerIndex.Valid())
				TextRender()->DeleteTextContainer(aBubble.m_TextContainerIndex);

			CTextCursor Cursor;
			Cursor.SetPosition(vec2(0, 0));
			Cursor.m_FontSize = FontSize;
			Cursor.m_Flags = TEXTFLAG_RENDER;
			Cursor.m_LineWidth = 500.0f - FontSize * 2.0f;
			TextRender()->CreateOrAppendTextContainer(aBubble.m_TextContainerIndex, &Cursor, aBubble.m_aText);
			aBubble.m_Cursor.m_FontSize = FontSize;
		}

		ColorRGBA BgColor(0.0f, 0.0f, 0.0f, 0.25f * Alpha);
		ColorRGBA TextColor(1, 1, 1, Alpha);
		ColorRGBA OutlineColor(0.0f, 0.0f, 0.0f, 0.5f * Alpha);

		if(aBubble.m_TextContainerIndex.Valid())
		{
			STextBoundingBox BoundingBox = TextRender()->GetBoundingBoxTextContainer(aBubble.m_TextContainerIndex);

			float x = Position.x - (BoundingBox.m_W / 2.0f + g_Config.m_RiChatBubbleSize / 15.0f);
			float y = BaseY - aBubble.m_OffsetY - BoundingBox.m_H - FontSize;

			//float PushBubble = ShiftBubbles(ClientId, vec2(x - FontSize / 2.0f, y - FontSize / 2.0f), BoundingBox.m_W + FontSize * 1.20f);
			float PushBubble = 0;

			Graphics()->DrawRect((x - FontSize / 2.0f) + PushBubble, y - FontSize / 2.0f,
				BoundingBox.m_W + FontSize * 1.20f, BoundingBox.m_H + FontSize,
				BgColor, IGraphics::CORNER_ALL, g_Config.m_RiChatBubbleSize / 4.5f);

			TextRender()->RenderTextContainer(aBubble.m_TextContainerIndex, TextColor, OutlineColor, x + PushBubble, y);
		}
	}
}

// @qxdFox ToDo:
// have to store the bubbles position in CBubbles in order to do this properly
float CChatBubbles::ShiftBubbles(int ClientId, vec2 Pos, float w)
{
	//if(!g_Config.m_ClChatBubblePushOut)
	return 0.0f;

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(i == ClientId)
			continue;

		int FontSize = g_Config.m_RiChatBubbleSize;
		vec2 Position = GameClient()->m_aClients[i].m_RenderPos;
		float BaseY = Position.y - GetOffset(i) - NameplateOffset;

		for(auto &aBubble : m_ChatBubbles[i])
		{
			if(aBubble.m_TextContainerIndex.Valid())
			{
				STextBoundingBox BoundingBox = TextRender()->GetBoundingBoxTextContainer(aBubble.m_TextContainerIndex);

				float Posx = Position.x - (BoundingBox.m_W / 2.0f + g_Config.m_RiChatBubbleSize / 15.0f);
				float Posy = BaseY - aBubble.m_OffsetY - BoundingBox.m_H - FontSize;
				float PosW = BoundingBox.m_W + FontSize * 1.20f;

				if(Posy + BoundingBox.m_H + FontSize < Pos.y)
					continue;
				if(Posy > Pos.y + BoundingBox.m_H + FontSize)
					continue;

				if(Posx + PosW >= Pos.x && Pos.x + w >= Posx + PosW)
					return Posx + PosW - Pos.x;
			}
		}
	}
	return 0.0f;
}

void CChatBubbles::ExpireBubbles()
{
	// This function is currently not implemented
	// If needed, it should iterate through all clients and remove expired bubbles
}

float CChatBubbles::GetAlpha(int64_t Time)
{
	const float FadeOutTime = g_Config.m_RiChatBubbleFadeOut / 100.0f;
	const float FadeInTime = g_Config.m_RiChatBubbleFadeIn / 100.0f;
	const float ShowTime = g_Config.m_RiChatBubbleShowTime / 100.0f;

	int64_t Now = time_get();
	float LineAge = (Now - Time) / (float)time_freq();
	// Fade in
	if(LineAge < FadeInTime)
		return std::clamp(LineAge / FadeInTime, 0.0f, 1.0f);

	float FadeOutProgress = (LineAge - (ShowTime - FadeOutTime)) / FadeOutTime;
	return std::clamp(1.0f - FadeOutProgress, 0.0f, 1.0f);
}

void CChatBubbles::OnRender()
{
	if(m_UseChatBubbles != g_Config.m_RiChatBubbles)
	{
		m_UseChatBubbles = g_Config.m_RiChatBubbles;
		Reset();
	}

	if(!g_Config.m_RiChatBubbles)
		return;

	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	RenderTools()->MapScreenToGroup(GameClient()->m_Camera.m_Center.x, GameClient()->m_Camera.m_Center.y, Layers()->GameGroup(), GameClient()->m_Camera.m_Zoom);

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(!GameClient()->m_Snap.m_apPlayerInfos[ClientId])
			continue;
		const CGameClient::CClientData &ClientData = GameClient()->m_aClients[ClientId];
		if(!ClientData.m_Active || !ClientData.m_RenderInfo.Valid())
			continue;
		RenderChatBubbles(ClientId);
	}

	Graphics()->MapScreen(ScreenX0, ScreenY0, ScreenX1, ScreenY1);
}

void CChatBubbles::Reset()
{
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		for(auto &aBubble : m_ChatBubbles[ClientId])
		{
			if(aBubble.m_TextContainerIndex.Valid())
				TextRender()->DeleteTextContainer(aBubble.m_TextContainerIndex);
			aBubble.m_Cursor.m_FontSize = 0;
		}
		m_ChatBubbles[ClientId].clear();
	}
}

void CChatBubbles::OnStateChange(int NewState, int OldState)
{
	if(OldState <= IClient::STATE_CONNECTING)
		Reset();
}

void CChatBubbles::OnWindowResize()
{
	Reset();
}
