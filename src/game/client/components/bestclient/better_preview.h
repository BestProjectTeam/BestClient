/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_BETTER_PREVIEW_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_BETTER_PREVIEW_H

#include <engine/graphics.h>

#include <game/client/ui_rect.h>

class CUi;

namespace BestClientBetterPreview
{

void RenderEntities(IGraphics *pGraphics, const IGraphics::CTextureHandle &Texture, const CUIRect &TextureRect, float TextureWidth);
void RenderGunpackCard(IGraphics *pGraphics, CUi *pUi, const IGraphics::CTextureHandle &Texture, const CUIRect &CardRect, const char *pName);

}

#endif
