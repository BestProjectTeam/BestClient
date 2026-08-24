/* Copyright © 2026 BestProject Team */
#include "chat_bubbles.h"

#include <base/color.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/client.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>
#include <engine/textrender.h>

#include <game/client/components/chat.h>
#include <game/client/gameclient.h>
#include <generated/protocol.h>

#include <algorithm>

CChat *CChatBubbles::Chat() const
{
	return &GameClient()->m_Chat;
}

float CChatBubbles::BubbleRounding() const
{
	if(g_Config.m_BcChatBubbleRounding > 0)
		return (float)g_Config.m_BcChatBubbleRounding;
	return g_Config.m_BcChatBubbleSize / 4.5f;
}

bool CChatBubbles::LineHighlighted(int ClientId, const char *pLine) const
{
	bool Highlighted = false;
	if(Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		if(ClientId >= 0 && ClientId != GameClient()->m_aLocalIds[0] && ClientId != GameClient()->m_aLocalIds[1])
		{
			for(int LocalId : GameClient()->m_aLocalIds)
				Highlighted |= LocalId >= 0 && Chat()->LineShouldHighlight(pLine, GameClient()->m_aClients[LocalId].m_aName);
		}
	}
	else if(GameClient()->m_Snap.m_LocalClientId >= 0)
	{
		Highlighted |= Chat()->LineShouldHighlight(pLine, GameClient()->m_aClients[GameClient()->m_Snap.m_LocalClientId].m_aName);
	}
	return Highlighted;
}

float CChatBubbles::GetOffset(int ClientId) const
{
	float Offset = GameClient()->m_NamePlates.GetNamePlateOffset(ClientId) + BcChatBubbleNameplateOffset;
	if(Offset < BcChatBubbleCharacterMinOffset)
		Offset = BcChatBubbleCharacterMinOffset;
	return Offset;
}

void CChatBubbles::OnMessage(int MsgType, void *pRawMsg)
{
	if(GameClient()->m_SuppressEvents)
		return;
	if(!g_Config.m_BcChatBubbles)
		return;

	if(MsgType == NETMSGTYPE_SV_CHAT)
	{
		CNetMsg_Sv_Chat *pMsg = (CNetMsg_Sv_Chat *)pRawMsg;
		AddBubble(pMsg->m_ClientId, pMsg->m_Team, pMsg->m_pMessage);
	}
}

void CChatBubbles::UpdateBubbleOffsets(int ClientId, float InputBubbleHeight)
{
	float Offset = 0.0f;
	if(InputBubbleHeight > 0.0f)
		Offset += InputBubbleHeight + BcChatBubbleMarginBetween;

	const int FontSize = g_Config.m_BcChatBubbleSize;
	for(CBcChatBubble &Bubble : m_aChatBubbles[ClientId])
	{
		if(!Bubble.m_TextContainerIndex.Valid() || Bubble.m_Cursor.m_FontSize != FontSize)
		{
			if(Bubble.m_TextContainerIndex.Valid())
			{
				TextRender()->DeleteTextContainer(Bubble.m_TextContainerIndex);
				Bubble.m_TextContainerIndex = STextContainerIndex();
			}

			CTextCursor Cursor;
			Cursor.SetPosition(vec2(0, 0));
			Cursor.m_FontSize = FontSize;
			Cursor.m_Flags = TEXTFLAG_RENDER;
			Cursor.m_LineWidth = 500.0f - FontSize * 2.0f;
			TextRender()->CreateOrAppendTextContainer(Bubble.m_TextContainerIndex, &Cursor, Bubble.m_aText);
			Bubble.m_Cursor.m_FontSize = FontSize;
		}

		const STextBoundingBox BoundingBox = TextRender()->GetBoundingBoxTextContainer(Bubble.m_TextContainerIndex);
		Bubble.m_TargetOffsetY = Offset;
		Offset += BoundingBox.m_H + FontSize + BcChatBubbleMarginBetween;
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

	const int FontSize = g_Config.m_BcChatBubbleSize;
	CTextCursor Cursor;

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	Graphics()->MapScreenToInterface(GameClient()->m_Camera.m_Center.x, GameClient()->m_Camera.m_Center.y);

	Cursor.SetPosition(vec2(0, 0));
	Cursor.m_FontSize = FontSize;
	Cursor.m_Flags = TEXTFLAG_RENDER;
	Cursor.m_LineWidth = 500.0f - FontSize * 2.0f;

	ColorRGBA Color = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	if(LineHighlighted(ClientId, pText))
		Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageHighlightColor));
	else if(Team == 1)
		Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageTeamColor));
	else if(Team == TEAM_WHISPER_RECV)
		Color = ColorRGBA(1.0f, 0.5f, 0.5f, 1.0f);
	else if(Team == TEAM_WHISPER_SEND)
	{
		Color = ColorRGBA(0.7f, 0.7f, 1.0f, 1.0f);
		ClientId = GameClient()->m_Snap.m_LocalClientId;
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		{
			Graphics()->MapScreen(ScreenX0, ScreenY0, ScreenX1, ScreenY1);
			return;
		}
	}
	else
		Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageColor));

	CBcChatBubble Bubble(pText, Cursor, time_get(), Color);
	TextRender()->CreateOrAppendTextContainer(Bubble.m_TextContainerIndex, &Cursor, pText);

	m_aChatBubbles[ClientId].insert(m_aChatBubbles[ClientId].begin(), Bubble);
	UpdateBubbleOffsets(ClientId);
	Graphics()->MapScreen(ScreenX0, ScreenY0, ScreenX1, ScreenY1);
}

void CChatBubbles::RenderCurInput(float Y)
{
	if(!Chat()->IsActive())
		return;

	const char *pText = Chat()->m_Input.GetString();
	if(!pText || pText[0] == '\0')
	{
		UpdateBubbleOffsets(GameClient()->m_Snap.m_LocalClientId);
		return;
	}

	const int FontSize = g_Config.m_BcChatBubbleSize;
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId < 0 || LocalId >= MAX_CLIENTS)
		return;

	vec2 Position = GameClient()->m_aClients[LocalId].m_RenderPos;
	CTextCursor Cursor;
	STextContainerIndex TextContainerIndex;

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	Graphics()->MapScreenToInterface(GameClient()->m_Camera.m_Center.x, GameClient()->m_Camera.m_Center.y);

	Cursor.SetPosition(vec2(0, 0));
	Cursor.m_FontSize = FontSize;
	Cursor.m_Flags = TEXTFLAG_RENDER;
	Cursor.m_LineWidth = 500.0f - FontSize * 2.0f;
	TextRender()->CreateOrAppendTextContainer(TextContainerIndex, &Cursor, pText);
	Graphics()->MapScreen(ScreenX0, ScreenY0, ScreenX1, ScreenY1);

	if(TextContainerIndex.Valid())
	{
		const STextBoundingBox BoundingBox = TextRender()->GetBoundingBoxTextContainer(TextContainerIndex);
		Position.x -= BoundingBox.m_W / 2.0f + g_Config.m_BcChatBubbleSize / 15.0f;
		const float InputBubbleHeight = BoundingBox.m_H + FontSize;
		const float TargetY = Y - InputBubbleHeight;
		const float Rounding = BubbleRounding();

		ColorRGBA BgColor(0.0f, 0.0f, 0.0f, 0.15f);
		ColorRGBA TextColor(1.0f, 1.0f, 1.0f, 0.75f);
		ColorRGBA OutlineColor(0.0f, 0.0f, 0.0f, 0.25f);
		if(g_Config.m_BcChatBubbleCustomColors)
		{
			BgColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcChatBubbleBgColor, true)).WithAlpha(0.15f);
			TextColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcChatBubbleTextColor, true)).WithAlpha(0.75f);
			OutlineColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcChatBubbleOutlineColor, true)).WithAlpha(0.25f);
		}

		Graphics()->DrawRect(Position.x - FontSize / 2.0f, TargetY - FontSize / 2.0f,
			BoundingBox.m_W + FontSize * 1.20f, BoundingBox.m_H + FontSize,
			BgColor, IGraphics::CORNER_ALL, Rounding);

		TextRender()->RenderTextContainer(TextContainerIndex, TextColor, OutlineColor, Position.x, TargetY);
		UpdateBubbleOffsets(LocalId, InputBubbleHeight);
	}
	else
		UpdateBubbleOffsets(LocalId);

	TextRender()->DeleteTextContainer(TextContainerIndex);
}

void CChatBubbles::RenderChatBubbles(int ClientId)
{
	if(!GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
		return;
	if(!g_Config.m_BcChatBubblesSelf && ClientId == GameClient()->m_Snap.m_LocalClientId)
		return;
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK && !g_Config.m_BcChatBubblesDemo)
		return;

	const float ShowTime = g_Config.m_BcChatBubbleShowTime / 100.0f;
	const int FontSize = g_Config.m_BcChatBubbleSize;
	const vec2 Position = GameClient()->m_aClients[ClientId].m_RenderPos;
	if(!GameClient()->OptimizerAllowRenderPos(Position))
		return;

	float BaseY = Position.y - GetOffset(ClientId) - BcChatBubbleNameplateOffset;

	if(ClientId == GameClient()->m_Snap.m_LocalClientId)
		RenderCurInput(BaseY);

	bool RemovedAny = false;
	for(auto It = m_aChatBubbles[ClientId].begin(); It != m_aChatBubbles[ClientId].end();)
	{
		CBcChatBubble &Bubble = *It;
		if(Bubble.m_Time + time_freq() * ShowTime < time_get())
		{
			if(Bubble.m_TextContainerIndex.Valid())
				TextRender()->DeleteTextContainer(Bubble.m_TextContainerIndex);
			It = m_aChatBubbles[ClientId].erase(It);
			RemovedAny = true;
			continue;
		}
		++It;
	}

	if(RemovedAny)
		UpdateBubbleOffsets(ClientId);

	const float Rounding = BubbleRounding();
	for(CBcChatBubble &Bubble : m_aChatBubbles[ClientId])
	{
		float Alpha = 1.0f;
		if(GameClient()->IsOtherTeam(ClientId))
			Alpha = g_Config.m_ClShowOthersAlpha / 100.0f;
		Alpha *= GetAlpha(Bubble.m_Time);
		if(Alpha <= 0.01f)
			continue;

		if(g_Config.m_BcChatBubbleAnimation)
		{
			const float Factor = std::clamp(Client()->RenderFrameTime() * 10.0f, 0.0f, 1.0f);
			Bubble.m_OffsetY += (Bubble.m_TargetOffsetY - Bubble.m_OffsetY) * Factor;
		}
		else
			Bubble.m_OffsetY = Bubble.m_TargetOffsetY;

		if(!Bubble.m_TextContainerIndex.Valid() || Bubble.m_Cursor.m_FontSize != FontSize)
		{
			if(Bubble.m_TextContainerIndex.Valid())
				TextRender()->DeleteTextContainer(Bubble.m_TextContainerIndex);

			CTextCursor Cursor;
			Cursor.SetPosition(vec2(0, 0));
			Cursor.m_FontSize = FontSize;
			Cursor.m_Flags = TEXTFLAG_RENDER;
			Cursor.m_LineWidth = 500.0f - FontSize * 2.0f;
			TextRender()->CreateOrAppendTextContainer(Bubble.m_TextContainerIndex, &Cursor, Bubble.m_aText);
			Bubble.m_Cursor.m_FontSize = FontSize;
		}

		ColorRGBA BgColor(0.0f, 0.0f, 0.0f, 0.25f * Alpha);
		ColorRGBA TextColor = Bubble.m_Color.WithAlpha(Alpha);
		ColorRGBA OutlineColor(0.0f, 0.0f, 0.0f, 0.5f * Alpha);
		if(g_Config.m_BcChatBubbleCustomColors)
		{
			BgColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcChatBubbleBgColor, true)).WithMultipliedAlpha(Alpha);
			TextColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcChatBubbleTextColor, true)).WithMultipliedAlpha(Alpha);
			OutlineColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcChatBubbleOutlineColor, true)).WithMultipliedAlpha(Alpha);
		}

		if(Bubble.m_TextContainerIndex.Valid())
		{
			const STextBoundingBox BoundingBox = TextRender()->GetBoundingBoxTextContainer(Bubble.m_TextContainerIndex);
			const float X = Position.x - (BoundingBox.m_W / 2.0f + g_Config.m_BcChatBubbleSize / 15.0f);
			const float Y = BaseY - Bubble.m_OffsetY - BoundingBox.m_H - FontSize;

			Graphics()->DrawRect(X - FontSize / 2.0f, Y - FontSize / 2.0f,
				BoundingBox.m_W + FontSize * 1.20f, BoundingBox.m_H + FontSize,
				BgColor, IGraphics::CORNER_ALL, Rounding);

			TextRender()->RenderTextContainer(Bubble.m_TextContainerIndex, TextColor, OutlineColor, X, Y);
		}
	}
}

float CChatBubbles::GetAlpha(int64_t Time) const
{
	const float FadeOutTime = g_Config.m_BcChatBubbleFadeOut / 100.0f;
	const float FadeInTime = g_Config.m_BcChatBubbleFadeIn / 100.0f;
	const float ShowTime = g_Config.m_BcChatBubbleShowTime / 100.0f;

	const int64_t Now = time_get();
	const float LineAge = (Now - Time) / (float)time_freq();
	if(LineAge < FadeInTime)
		return std::clamp(LineAge / FadeInTime, 0.0f, 1.0f);

	const float FadeOutProgress = (LineAge - (ShowTime - FadeOutTime)) / FadeOutTime;
	return std::clamp(1.0f - FadeOutProgress, 0.0f, 1.0f);
}

void CChatBubbles::OnRender()
{
	if(m_UseChatBubbles != g_Config.m_BcChatBubbles)
	{
		m_UseChatBubbles = g_Config.m_BcChatBubbles;
		Reset();
	}

	if(!g_Config.m_BcChatBubbles)
		return;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	float aPoints[4];
	Graphics()->MapScreenToWorld(
		GameClient()->m_Camera.m_Center.x, GameClient()->m_Camera.m_Center.y,
		100.0f, 100.0f, 100.0f, 0, 0,
		Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom, aPoints);
	Graphics()->MapScreen(aPoints[0], aPoints[1], aPoints[2], aPoints[3]);

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
		for(CBcChatBubble &Bubble : m_aChatBubbles[ClientId])
		{
			if(Bubble.m_TextContainerIndex.Valid())
				TextRender()->DeleteTextContainer(Bubble.m_TextContainerIndex);
			Bubble.m_Cursor.m_FontSize = 0;
		}
		m_aChatBubbles[ClientId].clear();
	}
}

void CChatBubbles::OnStateChange(int NewState, int OldState)
{
	(void)NewState;
	if(OldState <= IClient::STATE_CONNECTING)
		Reset();
}

void CChatBubbles::OnWindowResize()
{
	Reset();
}
