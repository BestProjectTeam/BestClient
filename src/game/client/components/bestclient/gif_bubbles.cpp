/* Copyright © 2026 BestProject Team */
#include "gif_bubbles.h"

#include <base/color.h>
#include <base/time.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <game/client/components/chat.h>
#include <game/client/gameclient.h>

#include <algorithm>

void CGifBubbles::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(!g_Config.m_BcGifBubbleAboveHead)
		return;

	CChat &Chat = GameClient()->m_Chat;
	const int64_t Now = time();
	const int64_t DurationTicks = (int64_t)g_Config.m_BcGifBubbleDurationMs * time_freq() / 1000;

	// One bubble per client id: the most recent still-alive line with m_ShowAboveHead wins.
	CChat::CLine *apLatest[MAX_CLIENTS] = {};
	for(int i = 0; i < CChat::MAX_LINES; i++)
	{
		CChat::CLine &Line = Chat.m_aLines[((Chat.m_CurrentLine - i) + CChat::MAX_LINES) % CChat::MAX_LINES];
		if(!Line.m_Initialized || !Line.m_ShowAboveHead)
			continue;
		if(Line.m_ClientId < 0 || Line.m_ClientId >= MAX_CLIENTS)
			continue;
		if(Now - Line.m_Time > DurationTicks)
			continue;
		if(apLatest[Line.m_ClientId] == nullptr)
			apLatest[Line.m_ClientId] = &Line;
	}

	constexpr float BubbleSize = 60.0f;
	constexpr float FadeIn = 0.15f;
	constexpr float FadeOut = 0.4f;
	const float DurationSec = g_Config.m_BcGifBubbleDurationMs / 1000.0f;

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		CChat::CLine *pLine = apLatest[ClientId];
		if(!pLine || pLine->m_MediaState != CChat::EMediaState::READY || pLine->m_vMediaFrames.empty())
			continue;
		if(!GameClient()->m_aClients[ClientId].m_Active || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
			continue;
		if(Chat.ShouldHideNsfwMedia(*pLine))
			continue;

		const vec2 RenderPos = GameClient()->m_aClients[ClientId].m_RenderPos;
		if(!GameClient()->OptimizerAllowRenderPos(RenderPos))
			continue;

		IGraphics::CTextureHandle Texture;
		if(!Chat.GetCurrentFrameTexture(*pLine, Texture) || !Texture.IsValid())
			continue;

		const float Elapsed = (Now - pLine->m_Time) / (float)time_freq();
		float Alpha = 1.0f;
		if(Elapsed < FadeIn)
			Alpha = Elapsed / FadeIn;
		else if(Elapsed > DurationSec - FadeOut)
			Alpha = (DurationSec - Elapsed) / FadeOut;
		Alpha = std::clamp(Alpha, 0.0f, 1.0f);
		if(Alpha <= 0.0f)
			continue;

		float W = BubbleSize, H = BubbleSize;
		if(pLine->m_MediaWidth > 0 && pLine->m_MediaHeight > 0)
		{
			if(pLine->m_MediaWidth >= pLine->m_MediaHeight)
				H = BubbleSize * pLine->m_MediaHeight / (float)pLine->m_MediaWidth;
			else
				W = BubbleSize * pLine->m_MediaWidth / (float)pLine->m_MediaHeight;
		}

		vec2 Center = RenderPos;
		Center.y -= (float)g_Config.m_BcGifBubbleOffsetY;

		DrawRoundedMediaPreview(Graphics(), Texture, Center.x - W / 2.0f, Center.y - H / 2.0f, W, H, 6.0f, Alpha);
	}
}
