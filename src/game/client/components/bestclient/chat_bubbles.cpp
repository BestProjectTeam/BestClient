/* Copyright © 2026 BestProject Team */
#include "chat_bubbles.h"

#include <base/color.h>
#include <base/str.h>
#include <base/time.h>
#include <base/vmath.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>
#include <engine/textrender.h>

#include <game/client/components/bestclient/chat_media.h>
#include <game/client/components/chat.h>
#include <game/client/gameclient.h>
#include <generated/protocol.h>

#include <algorithm>
#include <string>

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
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK && !g_Config.m_BcChatBubblesDemo)
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
		SChatMediaLine *pMedia = GameClient()->m_ChatMedia.FindReadyMediaForMessage(*Chat(), ClientId, Bubble.m_aText);
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
			std::string VisibleText;
			const char *pBubbleText = Bubble.m_aText;
			if(pMedia)
			{
				VisibleText = GameClient()->m_ChatMedia.BuildVisibleMessageText(*pMedia, Bubble.m_aText, false);
				pBubbleText = VisibleText.c_str();
			}
			if(pBubbleText[0] != '\0')
				TextRender()->CreateOrAppendTextContainer(Bubble.m_TextContainerIndex, &Cursor, pBubbleText);
			Bubble.m_Cursor.m_FontSize = FontSize;
		}

		float ExtraMedia = 0.0f;
		if(pMedia)
			ExtraMedia = 48.0f + (Bubble.m_TextContainerIndex.Valid() ? FontSize * 0.25f : 0.0f);
		float TextH = 0.0f;
		if(Bubble.m_TextContainerIndex.Valid())
			TextH = TextRender()->GetBoundingBoxTextContainer(Bubble.m_TextContainerIndex).m_H;
		Bubble.m_TargetOffsetY = Offset;
		Offset += TextH + ExtraMedia + FontSize + BcChatBubbleMarginBetween;
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

	const bool Highlighted = LineHighlighted(ClientId, pText);
	if(g_Config.m_BcChatOnlyTagsAndWhispers && !Highlighted && Team < 2)
		return;

	const int FontSize = g_Config.m_BcChatBubbleSize;
	CTextCursor Cursor;

	const CScreenRect ScreenRect = Graphics()->GetScreen();
	Graphics()->MapScreenToInterface(GameClient()->m_Camera.m_Center.x, GameClient()->m_Camera.m_Center.y);

	Cursor.SetPosition(vec2(0, 0));
	Cursor.m_FontSize = FontSize;
	Cursor.m_Flags = TEXTFLAG_RENDER;
	Cursor.m_LineWidth = 500.0f - FontSize * 2.0f;

	ColorRGBA Color = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	if(Highlighted)
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
			Graphics()->MapScreen(ScreenRect);
			return;
		}
	}
	else
		Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageColor));

	if(!g_Config.m_BcChatBubblesSelf && ClientId == GameClient()->m_Snap.m_LocalClientId)
	{
		Graphics()->MapScreen(ScreenRect);
		return;
	}

	CBcChatBubble Bubble(pText, Cursor, time_get(), Color);
	TextRender()->CreateOrAppendTextContainer(Bubble.m_TextContainerIndex, &Cursor, pText);

	m_aChatBubbles[ClientId].insert(m_aChatBubbles[ClientId].begin(), Bubble);
	UpdateBubbleOffsets(ClientId);
	Graphics()->MapScreen(ScreenRect);
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
	if(g_Config.m_BcFlyingNamePlates)
		Position.x = GameClient()->m_FlyingNamePlates.GetRenderPos(LocalId, Position).x;
	CTextCursor Cursor;
	STextContainerIndex TextContainerIndex;

	const CScreenRect ScreenRect = Graphics()->GetScreen();
	Graphics()->MapScreenToInterface(GameClient()->m_Camera.m_Center.x, GameClient()->m_Camera.m_Center.y);

	Cursor.SetPosition(vec2(0, 0));
	Cursor.m_FontSize = FontSize;
	Cursor.m_Flags = TEXTFLAG_RENDER;
	Cursor.m_LineWidth = 500.0f - FontSize * 2.0f;
	TextRender()->CreateOrAppendTextContainer(TextContainerIndex, &Cursor, pText);
	Graphics()->MapScreen(ScreenRect);

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

void CChatBubbles::ExpireBubbles(int ClientId)
{
	const float ShowTime = g_Config.m_BcChatBubbleShowTime / 100.0f;
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
}

void CChatBubbles::RenderChatBubbles(int ClientId)
{
	if(!GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
		return;
	if(!g_Config.m_BcChatBubblesSelf && ClientId == GameClient()->m_Snap.m_LocalClientId)
		return;
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK && !g_Config.m_BcChatBubblesDemo)
		return;

	const int FontSize = g_Config.m_BcChatBubbleSize;
	const vec2 TeePos = GameClient()->m_aClients[ClientId].m_RenderPos;
	if(!GameClient()->OptimizerAllowRenderPos(TeePos))
		return;

	vec2 Position = TeePos;
	float BaseY;
	if(g_Config.m_BcFlyingNamePlates)
	{
		const vec2 PlatePos = GameClient()->m_FlyingNamePlates.GetRenderPos(ClientId, TeePos);
		Position.x = PlatePos.x;
		BaseY = PlatePos.y - GetOffset(ClientId) - BcChatBubbleNameplateOffset;
	}
	else
	{
		BaseY = Position.y - GetOffset(ClientId) - BcChatBubbleNameplateOffset;
	}

	if(ClientId == GameClient()->m_Snap.m_LocalClientId)
		RenderCurInput(BaseY);

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

		SChatMediaLine *pMedia = GameClient()->m_ChatMedia.FindReadyMediaForMessage(*Chat(), ClientId, Bubble.m_aText);
		if(!Bubble.m_TextContainerIndex.Valid() || Bubble.m_Cursor.m_FontSize != FontSize)
		{
			if(Bubble.m_TextContainerIndex.Valid())
				TextRender()->DeleteTextContainer(Bubble.m_TextContainerIndex);

			CTextCursor Cursor;
			Cursor.SetPosition(vec2(0, 0));
			Cursor.m_FontSize = FontSize;
			Cursor.m_Flags = TEXTFLAG_RENDER;
			Cursor.m_LineWidth = 500.0f - FontSize * 2.0f;

			std::string VisibleText;
			const char *pBubbleText = Bubble.m_aText;
			if(pMedia)
			{
				VisibleText = GameClient()->m_ChatMedia.BuildVisibleMessageText(*pMedia, Bubble.m_aText, false);
				pBubbleText = VisibleText.c_str();
			}
			if(pBubbleText[0] != '\0')
				TextRender()->CreateOrAppendTextContainer(Bubble.m_TextContainerIndex, &Cursor, pBubbleText);
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

		IGraphics::CTextureHandle MediaTexture;
		const bool HasMedia = pMedia && GameClient()->m_ChatMedia.GetCurrentFrameTexture(*pMedia, MediaTexture);
		const float MediaPreview = HasMedia ? 48.0f : 0.0f;

		if(Bubble.m_TextContainerIndex.Valid() || HasMedia)
		{
			float TextW = 0.0f;
			float TextH = 0.0f;
			if(Bubble.m_TextContainerIndex.Valid())
			{
				const STextBoundingBox BoundingBox = TextRender()->GetBoundingBoxTextContainer(Bubble.m_TextContainerIndex);
				TextW = BoundingBox.m_W;
				TextH = BoundingBox.m_H;
			}
			const float ContentW = std::max(TextW, MediaPreview);
			const float ContentH = TextH + (HasMedia ? (TextH > 0.0f ? FontSize * 0.25f : 0.0f) + MediaPreview : 0.0f);
			const float X = Position.x - (ContentW / 2.0f + g_Config.m_BcChatBubbleSize / 15.0f);
			const float Y = BaseY - Bubble.m_OffsetY - ContentH - FontSize;

			Graphics()->DrawRect(X - FontSize / 2.0f, Y - FontSize / 2.0f,
				ContentW + FontSize * 1.20f, ContentH + FontSize,
				BgColor, IGraphics::CORNER_ALL, Rounding);

			if(Bubble.m_TextContainerIndex.Valid() && TextW > 0.0f)
				TextRender()->RenderTextContainer(Bubble.m_TextContainerIndex, TextColor, OutlineColor, X, Y);

			if(HasMedia)
			{
				const float MediaY = Y + (TextH > 0.0f ? TextH + FontSize * 0.25f : 0.0f);
				const float MediaX = X + (ContentW - MediaPreview) * 0.5f;
				DrawRoundedMediaPreview(Graphics(), MediaTexture, MediaX, MediaY, MediaPreview, MediaPreview, std::max(2.0f, Rounding * 0.5f), Alpha);
			}
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

	const CScreenRect ScreenRect = Graphics()->GetScreen();

	const CScreenRect WorldScreen = Graphics()->MapScreenToWorld(
		GameClient()->m_Camera.m_Center.x, GameClient()->m_Camera.m_Center.y,
		100.0f, 100.0f, 100.0f, 0, 0,
		Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom);
	Graphics()->MapScreen(WorldScreen);

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		ExpireBubbles(ClientId);

		if(!GameClient()->m_Snap.m_apPlayerInfos[ClientId])
			continue;
		const CGameClient::CClientData &ClientData = GameClient()->m_aClients[ClientId];
		if(!ClientData.m_Active || !ClientData.m_RenderInfo.Valid())
			continue;
		RenderChatBubbles(ClientId);
	}

	Graphics()->MapScreen(ScreenRect);
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
