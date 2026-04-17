/* Copyright © 2026 BestProject Team */
#include <engine/client.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>
#include <engine/textrender.h>

#include <game/client/components/chat.h>
#include <game/client/gameclient.h>

#include <base/system.h>
#include <base/vmath.h>

#include <algorithm>

namespace
{
constexpr float CHAT_BUBBLE_MEDIA_MAX_PREVIEW_HEIGHT = 70.0f;
constexpr float CHAT_BUBBLE_MEDIA_PREVIEW_SIZE_SCALE = 0.9f;
constexpr float CHAT_BUBBLE_MEDIA_MIN_PREVIEW_SIDE = 28.0f;

float EaseOutCubic(float Value)
{
	Value = std::clamp(Value, 0.0f, 1.0f);
	const float Inv = 1.0f - Value;
	return 1.0f - Inv * Inv * Inv;
}
}

#include "chat_bubbles.h"

CChat *CChatBubbles::Chat() const
{
	return &GameClient()->m_Chat;
}

CChatBubbles::CChatBubbles()
{
}

CChatBubbles::~CChatBubbles()
{
}

int CChatBubbles::Sizeof() const
{
	return sizeof(*this);
}

bool CChatBubbles::HasVisibleBubbles() const
{
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(!m_ChatBubbles[ClientId].empty())
			return true;
	}
	return false;
}

void CChatBubbles::RefreshBubbleTextContainer(CBubbles &Bubble, int FontSize, const char *pText)
{
	if(!pText)
		pText = "";

	if(Bubble.m_TextContainerIndex.Valid() && Bubble.m_Cursor.m_FontSize == FontSize &&
		str_comp(Bubble.m_aRenderText, pText) == 0 && (Bubble.m_TextWidth > 0.0f || Bubble.m_TextHeight > 0.0f || pText[0] == '\0'))
		return;

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
	Bubble.m_Cursor.m_FontSize = FontSize;
	if(pText[0] != '\0')
		TextRender()->CreateOrAppendTextContainer(Bubble.m_TextContainerIndex, &Cursor, pText);
	str_copy(Bubble.m_aRenderText, pText, sizeof(Bubble.m_aRenderText));

	if(Bubble.m_TextContainerIndex.Valid())
	{
		const STextBoundingBox BoundingBox = TextRender()->GetBoundingBoxTextContainer(Bubble.m_TextContainerIndex);
		Bubble.m_TextWidth = BoundingBox.m_W;
		Bubble.m_TextHeight = BoundingBox.m_H;
	}
	else
	{
		Bubble.m_TextWidth = 0.0f;
		Bubble.m_TextHeight = 0.0f;
	}
}

CChat::CLine *CChatBubbles::FindChatLine(CBubbles &Bubble) const
{
	for(int i = 0; i < CChat::MAX_LINES; ++i)
	{
		CChat::CLine &Line = Chat()->m_aLines[((Chat()->m_CurrentLine - i) + CChat::MAX_LINES) % CChat::MAX_LINES];
		if(!Line.m_Initialized)
			break;
		if(Line.m_TeamNumber != Bubble.m_Team)
			continue;
		if(Line.m_ClientId != Bubble.m_SourceClientId)
			continue;
		if(str_comp(Line.m_aText, Bubble.m_aText) != 0)
			continue;
		return &Line;
	}

	return nullptr;
}

std::string CChatBubbles::GetBubbleDisplayText(const CBubbles &Bubble, const CChat::CLine *pChatLine) const
{
	if(pChatLine && Chat()->ShouldDisplayMediaSlot(*pChatLine))
		return Chat()->BuildVisibleMessageText(*pChatLine, false);

	return Bubble.m_aText;
}

void CChatBubbles::GetBubbleMediaSize(const CChat::CLine *pChatLine, int FontSize, float &PreviewWidth, float &PreviewHeight) const
{
	PreviewWidth = 0.0f;
	PreviewHeight = 0.0f;
	if(!pChatLine || !Chat()->ShouldDisplayMediaSlot(*pChatLine))
		return;

	const float MaxPreviewWidth = minimum(500.0f - FontSize * 2.0f, (float)g_Config.m_BcChatMediaPreviewMaxWidth) * CHAT_BUBBLE_MEDIA_PREVIEW_SIZE_SCALE;
	const float MaxPreviewHeight = CHAT_BUBBLE_MEDIA_MAX_PREVIEW_HEIGHT * CHAT_BUBBLE_MEDIA_PREVIEW_SIZE_SCALE;
	if(MaxPreviewWidth <= 0.0f || MaxPreviewHeight <= 0.0f)
		return;

	if(Chat()->ShouldHideMediaPreview(*pChatLine) ||
		(pChatLine->m_MediaState == CChat::EMediaState::READY && pChatLine->m_MediaWidth > 0 && pChatLine->m_MediaHeight > 0 && !pChatLine->m_vMediaFrames.empty()))
	{
		if(pChatLine->m_MediaState == CChat::EMediaState::READY && pChatLine->m_MediaWidth > 0 && pChatLine->m_MediaHeight > 0 && !pChatLine->m_vMediaFrames.empty())
		{
			const float ScaleByWidth = MaxPreviewWidth / (float)pChatLine->m_MediaWidth;
			const float ScaleByHeight = MaxPreviewHeight / (float)pChatLine->m_MediaHeight;
			float Scale = minimum(1.0f, minimum(ScaleByWidth, ScaleByHeight));
			float PreviewW = maximum(1.0f, (float)pChatLine->m_MediaWidth * Scale);
			float PreviewH = maximum(1.0f, (float)pChatLine->m_MediaHeight * Scale);
			if(PreviewW < CHAT_BUBBLE_MEDIA_MIN_PREVIEW_SIDE || PreviewH < CHAT_BUBBLE_MEDIA_MIN_PREVIEW_SIDE)
			{
				const float UpscaleByW = CHAT_BUBBLE_MEDIA_MIN_PREVIEW_SIDE / PreviewW;
				const float UpscaleByH = CHAT_BUBBLE_MEDIA_MIN_PREVIEW_SIDE / PreviewH;
				const float Upscale = maximum(UpscaleByW, UpscaleByH);
				const float MaxUpscale = minimum(MaxPreviewWidth / PreviewW, MaxPreviewHeight / PreviewH);
				if(MaxUpscale > 1.0f)
				{
					const float UseUpscale = minimum(Upscale, MaxUpscale);
					PreviewW *= UseUpscale;
					PreviewH *= UseUpscale;
				}
			}
			PreviewWidth = maximum(1.0f, PreviewW);
			PreviewHeight = maximum(1.0f, PreviewH);
		}
		else
		{
			PreviewWidth = MaxPreviewWidth;
			PreviewHeight = maximum(FontSize * 1.6f, 18.0f) * CHAT_BUBBLE_MEDIA_PREVIEW_SIZE_SCALE;
		}
	}
	else if(pChatLine->m_MediaState == CChat::EMediaState::QUEUED || pChatLine->m_MediaState == CChat::EMediaState::LOADING || pChatLine->m_MediaState == CChat::EMediaState::DECODING)
	{
		PreviewWidth = MaxPreviewWidth;
		PreviewHeight = maximum(FontSize * 1.2f, 12.0f) * CHAT_BUBBLE_MEDIA_PREVIEW_SIZE_SCALE;
	}
	else if(pChatLine->m_MediaState == CChat::EMediaState::FAILED)
	{
		PreviewWidth = MaxPreviewWidth;
		PreviewHeight = maximum(FontSize * 2.1f, 18.0f) * CHAT_BUBBLE_MEDIA_PREVIEW_SIZE_SCALE;
	}
}

float CChatBubbles::GetOffset(int ClientId)
{
	(void)ClientId;
	float Offset = (float)g_Config.m_ClNamePlatesOffset + NameplateOffset;
	if(Offset < CharacterMinOffset)
		Offset = CharacterMinOffset;

	return Offset;
}

void CChatBubbles::OnMessage(int MsgType, void *pRawMsg)
{
	if(GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_CHAT_BUBBLES))
		return;

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
		Offset += InputBubbleHeight + MarginBetween;

	int FontSize = g_Config.m_BcChatBubbleSize;
	for(CBubbles &aBubble : m_ChatBubbles[ClientId])
	{
		CChat::CLine *pChatLine = FindChatLine(aBubble);
		const std::string DisplayText = GetBubbleDisplayText(aBubble, pChatLine);
		RefreshBubbleTextContainer(aBubble, FontSize, DisplayText.c_str());
		float PreviewWidth = 0.0f;
		float PreviewHeight = 0.0f;
		GetBubbleMediaSize(pChatLine, FontSize, PreviewWidth, PreviewHeight);
		(void)PreviewWidth;
		const float MediaGap = aBubble.m_TextHeight > 0.0f && PreviewHeight > 0.0f ? FontSize * 0.4f : 0.0f;
		const float ContentHeight = aBubble.m_TextHeight + MediaGap + PreviewHeight;
		aBubble.m_TargetOffsetY = Offset;
		Offset += ContentHeight + FontSize + MarginBetween;
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

	char aSanitizedText[1024];
	GameClient()->m_BestClient.SanitizeText(pText, aSanitizedText, sizeof(aSanitizedText));
	if(aSanitizedText[0] == '\0')
		return;

	int FontSize = g_Config.m_BcChatBubbleSize;
	CTextCursor pCursor;

	// Create Text at default zoom
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	Graphics()->MapScreenToInterface(GameClient()->m_Camera.m_Center.x, GameClient()->m_Camera.m_Center.y);

	pCursor.SetPosition(vec2(0, 0));
	pCursor.m_FontSize = FontSize;
	pCursor.m_Flags = TEXTFLAG_RENDER;
	pCursor.m_LineWidth = 500.0f - FontSize * 2.0f;

	CBubbles Bubble(aSanitizedText, pCursor, time_get(), ClientId, Team);

	ColorRGBA Color = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	if(Team == 1)
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
	Bubble.m_TextColor = Color;

	RefreshBubbleTextContainer(Bubble, FontSize, Bubble.m_aText);

	m_ChatBubbles[ClientId].insert(m_ChatBubbles[ClientId].begin(), Bubble);

	UpdateBubbleOffsets(ClientId);
	Graphics()->MapScreen(ScreenX0, ScreenY0, ScreenX1, ScreenY1);
}

void CChatBubbles::RemoveBubble(int ClientId, CBubbles aBubble)
{
	for(auto It = m_ChatBubbles[ClientId].begin(); It != m_ChatBubbles[ClientId].end(); ++It)
	{
		if(*It == aBubble)
		{
			TextRender()->DeleteTextContainer(It->m_TextContainerIndex);
			m_ChatBubbles[ClientId].erase(It);
			UpdateBubbleOffsets(ClientId);
			return;
		}
	}
}

void CChatBubbles::RenderCurInput(float y)
{
	int FontSize = g_Config.m_BcChatBubbleSize;
	const char *pText = Chat()->m_Input.GetDisplayedString();
	if(pText[0] == '\0')
	{
		if(m_InputTextContainerIndex.Valid())
			TextRender()->DeleteTextContainer(m_InputTextContainerIndex);
		m_InputTextContainerIndex = STextContainerIndex();
		m_aInputText[0] = '\0';
		m_InputFontSize = 0;
		m_InputTextWidth = 0.0f;
		m_InputTextHeight = 0.0f;
		m_InputBubbleHeight = 0.0f;
		UpdateBubbleOffsets(GameClient()->m_Snap.m_LocalClientId);
		return;
	}

	int LocalId = GameClient()->m_Snap.m_LocalClientId;
	vec2 Position = GameClient()->m_aClients[LocalId].m_RenderPos;

	if(!m_InputTextContainerIndex.Valid() || m_InputFontSize != FontSize || str_comp(m_aInputText, pText) != 0)
	{
		if(m_InputTextContainerIndex.Valid())
			TextRender()->DeleteTextContainer(m_InputTextContainerIndex);

		CTextCursor Cursor;

		// Create text at default zoom so the bubble size stays stable while the camera zoom changes.
		float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
		Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
		Graphics()->MapScreenToInterface(GameClient()->m_Camera.m_Center.x, GameClient()->m_Camera.m_Center.y);

		Cursor.SetPosition(vec2(0, 0));
		Cursor.m_FontSize = FontSize;
		Cursor.m_Flags = TEXTFLAG_RENDER;
		Cursor.m_LineWidth = 500.0f - FontSize * 2.0f;
		TextRender()->CreateOrAppendTextContainer(m_InputTextContainerIndex, &Cursor, pText);
		if(m_InputTextContainerIndex.Valid())
		{
			const STextBoundingBox BoundingBox = TextRender()->GetBoundingBoxTextContainer(m_InputTextContainerIndex);
			m_InputTextWidth = BoundingBox.m_W;
			m_InputTextHeight = BoundingBox.m_H;
		}
		else
		{
			m_InputTextWidth = 0.0f;
			m_InputTextHeight = 0.0f;
		}

		Graphics()->MapScreen(ScreenX0, ScreenY0, ScreenX1, ScreenY1);
		str_copy(m_aInputText, pText, sizeof(m_aInputText));
		m_InputFontSize = FontSize;
	}

	if(m_InputTextContainerIndex.Valid())
	{
		Position.x -= m_InputTextWidth / 2.0f + g_Config.m_BcChatBubbleSize / 15.0f;
		float InputBubbleHeight = m_InputTextHeight + FontSize;
		m_InputBubbleHeight = InputBubbleHeight;

		float TargetY = y - InputBubbleHeight;

		Graphics()->DrawRect(Position.x - FontSize / 2.0f, TargetY - FontSize / 2.0f,
			m_InputTextWidth + FontSize * 1.20f, m_InputTextHeight + FontSize,
			BubbleBackgroundColor(0.6f), IGraphics::CORNER_ALL, BubbleRounding(FontSize));

		TextRender()->RenderTextContainer(m_InputTextContainerIndex, BubbleTextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), 0.75f), BubbleOutlineColor(0.5f), Position.x, TargetY);

		UpdateBubbleOffsets(LocalId, InputBubbleHeight);

		y -= InputBubbleHeight + MarginBetween;
	}
	else
		UpdateBubbleOffsets(LocalId);
}

void CChatBubbles::RenderChatBubbles(int ClientId)
{
	if(!GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
		return;

	if(!g_Config.m_BcChatBubblesSelf && (ClientId == GameClient()->m_Snap.m_LocalClientId))
		return;

	if(Client()->State() == IClient::STATE_DEMOPLAYBACK && !g_Config.m_BcChatBubblesDemo)
		return;

	const float ShowTime = g_Config.m_BcChatBubbleShowTime / 100.0f;
	int FontSize = g_Config.m_BcChatBubbleSize;
	vec2 Position = GameClient()->m_aClients[ClientId].m_RenderPos;
	float BaseY = Position.y - GetOffset(ClientId) - NameplateOffset;

	if(ClientId == GameClient()->m_Snap.m_LocalClientId)
		RenderCurInput(BaseY);

	// First pass: collect expired bubbles and clean up text containers.
	for(auto It = m_ChatBubbles[ClientId].begin(); It != m_ChatBubbles[ClientId].end();)
	{
		CBubbles &aBubble = *It;

		if(aBubble.m_Time + time_freq() * ShowTime < time_get())
		{
			// Clean up text container before removal
			if(aBubble.m_TextContainerIndex.Valid())
				TextRender()->DeleteTextContainer(aBubble.m_TextContainerIndex);
			It = m_ChatBubbles[ClientId].erase(It);
			continue;
		}
		++It;
	}

	// Recalculate every frame because media previews can appear after async chat-media decoding.
	UpdateBubbleOffsets(ClientId, ClientId == GameClient()->m_Snap.m_LocalClientId ? m_InputBubbleHeight : 0.0f);

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
		CChat::CLine *pChatLine = FindChatLine(aBubble);
		const std::string DisplayText = GetBubbleDisplayText(aBubble, pChatLine);
		RefreshBubbleTextContainer(aBubble, FontSize, DisplayText.c_str());
		float PreviewWidth = 0.0f;
		float PreviewHeight = 0.0f;
		GetBubbleMediaSize(pChatLine, FontSize, PreviewWidth, PreviewHeight);
		const float MediaGap = aBubble.m_TextHeight > 0.0f && PreviewHeight > 0.0f ? FontSize * 0.4f : 0.0f;
		const float ContentWidth = maximum(aBubble.m_TextWidth, PreviewWidth);
		const float ContentHeight = aBubble.m_TextHeight + MediaGap + PreviewHeight;

		ColorRGBA BgColor = BubbleBackgroundColor(Alpha);
		ColorRGBA TextColor = BubbleTextColor(aBubble.m_TextColor, Alpha);
		ColorRGBA OutlineColor = BubbleOutlineColor(Alpha);

		if(aBubble.m_TextContainerIndex.Valid() || PreviewHeight > 0.0f)
		{
			float x = Position.x - (ContentWidth / 2.0f + g_Config.m_BcChatBubbleSize / 15.0f);
			float y = BaseY - aBubble.m_OffsetY - ContentHeight - FontSize;
			const float Appear = GetAppearProgress(aBubble.m_Time);
			switch(std::clamp(g_Config.m_BcChatBubbleAnimation, 0, 3))
			{
			case 1:
				y += (1.0f - Appear) * FontSize * 0.7f;
				break;
			case 2:
				x += (ClientId == GameClient()->m_Snap.m_LocalClientId ? -1.0f : 1.0f) * (1.0f - Appear) * FontSize * 1.1f;
				break;
			case 3:
				y += (1.0f - Appear) * (1.0f - Appear) * FontSize * 0.45f - sinf(Appear * pi) * FontSize * 0.12f;
				break;
			default:
				break;
			}

			//float PushBubble = ShiftBubbles(ClientId, vec2(x - FontSize / 2.0f, y - FontSize / 2.0f), BoundingBox.m_W + FontSize * 1.20f);
			float PushBubble = 0;

			Graphics()->DrawRect((x - FontSize / 2.0f) + PushBubble, y - FontSize / 2.0f,
				ContentWidth + FontSize * 1.20f, ContentHeight + FontSize,
				BgColor, IGraphics::CORNER_ALL, BubbleRounding(FontSize));

			if(aBubble.m_TextContainerIndex.Valid())
				TextRender()->RenderTextContainer(aBubble.m_TextContainerIndex, TextColor, OutlineColor, x + PushBubble, y);

			if(PreviewHeight > 0.0f && pChatLine)
			{
				const float PreviewX = x + PushBubble + (ContentWidth - PreviewWidth) / 2.0f;
				const float PreviewY = y + (aBubble.m_TextHeight > 0.0f ? aBubble.m_TextHeight + MediaGap : 0.0f);
				if(Chat()->ShouldHideMediaPreview(*pChatLine))
				{
					Graphics()->TextureClear();
					Graphics()->QuadsBegin();
					Graphics()->SetColor(0.10f, 0.10f, 0.10f, 0.82f * Alpha);
					const IGraphics::CQuadItem HiddenQuad(PreviewX, PreviewY, PreviewWidth, PreviewHeight);
					Graphics()->QuadsDrawTL(&HiddenQuad, 1);
					Graphics()->QuadsEnd();

					const float HiddenFontSize = FontSize * 0.72f;
					const float HiddenLabelWidth = TextRender()->TextWidth(HiddenFontSize, "hidden media");
					CTextCursor HiddenCursor;
					HiddenCursor.SetPosition(vec2(PreviewX + maximum(FontSize * 0.35f, (PreviewWidth - HiddenLabelWidth) / 2.0f), PreviewY + maximum(FontSize * 0.25f, (PreviewHeight - HiddenFontSize) / 2.0f)));
					HiddenCursor.m_FontSize = HiddenFontSize;
					TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.9f * Alpha);
					TextRender()->TextEx(&HiddenCursor, "hidden media");
					TextRender()->TextColor(TextRender()->DefaultTextColor());
				}
				else if(pChatLine->m_MediaState == CChat::EMediaState::READY)
				{
					IGraphics::CTextureHandle MediaTexture;
					if(Chat()->GetCurrentFrameTexture(*pChatLine, MediaTexture))
					{
						Graphics()->WrapClamp();
						Graphics()->TextureSet(MediaTexture);
						Graphics()->QuadsBegin();
						Graphics()->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
						Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
						const IGraphics::CQuadItem QuadItem(PreviewX, PreviewY, PreviewWidth, PreviewHeight);
						Graphics()->QuadsDrawTL(&QuadItem, 1);
						Graphics()->QuadsEnd();
						Graphics()->WrapNormal();
						Graphics()->TextureClear();
					}
				}
				else if(pChatLine->m_MediaState == CChat::EMediaState::QUEUED || pChatLine->m_MediaState == CChat::EMediaState::LOADING || pChatLine->m_MediaState == CChat::EMediaState::DECODING)
				{
					Graphics()->TextureClear();
					Graphics()->QuadsBegin();
					Graphics()->SetColor(0.12f, 0.12f, 0.12f, 0.75f * Alpha);
					const IGraphics::CQuadItem LoadingQuad(PreviewX, PreviewY, PreviewWidth, PreviewHeight);
					Graphics()->QuadsDrawTL(&LoadingQuad, 1);
					Graphics()->QuadsEnd();

					CTextCursor LoadingCursor;
					LoadingCursor.SetPosition(vec2(PreviewX + FontSize * 0.35f, PreviewY + PreviewHeight * 0.15f));
					LoadingCursor.m_FontSize = FontSize * 0.75f;
					TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.8f * Alpha);
					TextRender()->TextEx(&LoadingCursor, "Loading media...");
					TextRender()->TextColor(TextRender()->DefaultTextColor());
				}
				else if(pChatLine->m_MediaState == CChat::EMediaState::FAILED)
				{
					Graphics()->TextureClear();
					Graphics()->QuadsBegin();
					Graphics()->SetColor(0.23f, 0.10f, 0.10f, 0.82f * Alpha);
					const IGraphics::CQuadItem FailedQuad(PreviewX, PreviewY, PreviewWidth, PreviewHeight);
					Graphics()->QuadsDrawTL(&FailedQuad, 1);
					Graphics()->QuadsEnd();

					CTextCursor FailedCursor;
					FailedCursor.SetPosition(vec2(PreviewX + FontSize * 0.35f, PreviewY + FontSize * 0.25f));
					FailedCursor.m_FontSize = FontSize * 0.70f;
					TextRender()->TextColor(1.0f, 0.85f, 0.85f, 0.95f * Alpha);
					TextRender()->TextEx(&FailedCursor, "Media preview unavailable");
					TextRender()->TextColor(TextRender()->DefaultTextColor());
				}
			}
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

		int FontSize = g_Config.m_BcChatBubbleSize;
		vec2 Position = GameClient()->m_aClients[i].m_RenderPos;
		float BaseY = Position.y - GetOffset(i) - NameplateOffset;

		for(auto &aBubble : m_ChatBubbles[i])
		{
			if(aBubble.m_TextContainerIndex.Valid())
			{
				STextBoundingBox BoundingBox = TextRender()->GetBoundingBoxTextContainer(aBubble.m_TextContainerIndex);

				float Posx = Position.x - (BoundingBox.m_W / 2.0f + g_Config.m_BcChatBubbleSize / 15.0f);
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
	const float FadeOutTime = g_Config.m_BcChatBubbleFadeOut / 100.0f;
	const float FadeInTime = g_Config.m_BcChatBubbleFadeIn / 100.0f;
	const float ShowTime = g_Config.m_BcChatBubbleShowTime / 100.0f;

	int64_t Now = time_get();
	float LineAge = (Now - Time) / (float)time_freq();
	// Fade in
	if(LineAge < FadeInTime)
		return std::clamp(LineAge / FadeInTime, 0.0f, 1.0f);

	float FadeOutProgress = (LineAge - (ShowTime - FadeOutTime)) / FadeOutTime;
	return std::clamp(1.0f - FadeOutProgress, 0.0f, 1.0f);
}

float CChatBubbles::GetAppearProgress(int64_t Time)
{
	const float FadeInTime = maximum(0.01f, g_Config.m_BcChatBubbleFadeIn / 100.0f);
	const float LineAge = (time_get() - Time) / (float)time_freq();
	return EaseOutCubic(LineAge / FadeInTime);
}

float CChatBubbles::BubbleRounding(int FontSize) const
{
	const float BaseRounding = FontSize / 4.5f;
	return BaseRounding * std::clamp(g_Config.m_BcChatBubbleRounding / 100.0f, 0.0f, 2.0f);
}

ColorRGBA CChatBubbles::BubbleBackgroundColor(float Alpha) const
{
	if(g_Config.m_BcChatBubbleCustomColors)
		return color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcChatBubbleBgColor, true)).WithMultipliedAlpha(Alpha);
	return ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f * Alpha);
}

ColorRGBA CChatBubbles::BubbleOutlineColor(float Alpha) const
{
	if(g_Config.m_BcChatBubbleCustomColors)
		return color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcChatBubbleOutlineColor, true)).WithMultipliedAlpha(Alpha);
	return ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f * Alpha);
}

ColorRGBA CChatBubbles::BubbleTextColor(const ColorRGBA &BaseColor, float Alpha) const
{
	if(g_Config.m_BcChatBubbleCustomColors)
		return color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcChatBubbleTextColor, true)).WithMultipliedAlpha(Alpha);
	return ColorRGBA(BaseColor.r, BaseColor.g, BaseColor.b, BaseColor.a * Alpha);
}

void CChatBubbles::OnRender()
{
	if(GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_CHAT_BUBBLES))
		return;

	if(m_UseChatBubbles != g_Config.m_BcChatBubbles)
	{
		m_UseChatBubbles = g_Config.m_BcChatBubbles;
		Reset();
	}

	if(!g_Config.m_BcChatBubbles)
		return;

	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(!HasVisibleBubbles() && Chat()->m_Input.GetString()[0] == '\0')
		return;
	float PrevScreenX0, PrevScreenY0, PrevScreenX1, PrevScreenY1;
	Graphics()->GetScreen(&PrevScreenX0, &PrevScreenY0, &PrevScreenX1, &PrevScreenY1);
	Graphics()->MapScreenToInterface(GameClient()->m_Camera.m_Center.x, GameClient()->m_Camera.m_Center.y, GameClient()->m_Camera.m_Zoom);

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	const float BubbleBorder = 256.0f;
	ScreenX0 -= BubbleBorder;
	ScreenY0 -= BubbleBorder;
	ScreenX1 += BubbleBorder;
	ScreenY1 += BubbleBorder;
	int RenderedClients = 0;
	const int MaxRenderedClients = 12;

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(RenderedClients >= MaxRenderedClients)
			break;
		if(!GameClient()->m_Snap.m_apPlayerInfos[ClientId])
			continue;
		const CGameClient::CClientData &ClientData = GameClient()->m_aClients[ClientId];
		if(!ClientData.m_Active || !ClientData.m_RenderInfo.Valid())
			continue;
		if(!GameClient()->OptimizerAllowRenderPos(ClientData.m_RenderPos))
			continue;
		if(ClientData.m_RenderPos.x < ScreenX0 || ClientData.m_RenderPos.x > ScreenX1 || ClientData.m_RenderPos.y < ScreenY0 || ClientData.m_RenderPos.y > ScreenY1)
			continue;
		RenderChatBubbles(ClientId);
		++RenderedClients;
	}

	Graphics()->MapScreen(PrevScreenX0, PrevScreenY0, PrevScreenX1, PrevScreenY1);
}

void CChatBubbles::Reset()
{
	if(m_InputTextContainerIndex.Valid())
		TextRender()->DeleteTextContainer(m_InputTextContainerIndex);
	m_InputTextContainerIndex = STextContainerIndex();
	m_aInputText[0] = '\0';
	m_InputFontSize = 0;
	m_InputTextWidth = 0.0f;
	m_InputTextHeight = 0.0f;
	m_InputBubbleHeight = 0.0f;

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		for(auto &aBubble : m_ChatBubbles[ClientId])
		{
			if(aBubble.m_TextContainerIndex.Valid())
				TextRender()->DeleteTextContainer(aBubble.m_TextContainerIndex);
			aBubble.m_Cursor.m_FontSize = 0;
			aBubble.m_TextWidth = 0.0f;
			aBubble.m_TextHeight = 0.0f;
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
