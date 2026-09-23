/* Copyright © 2026 BestProject Team */
#include "better_preview.h"

#include <game/client/ui.h>
#include <game/mapitems.h>

#include <generated/client_data.h>

#include <algorithm>

namespace BestClientBetterPreview
{

void RenderEntities(IGraphics *pGraphics, const IGraphics::CTextureHandle &Texture, const CUIRect &TextureRect, float TextureWidth, ColorRGBA Background)
{
	if(!Texture.IsValid())
		return;
	static constexpr int COLS = 9;
	static constexpr int ROWS = 9;
	static constexpr unsigned char s_aLayout[ROWS][COLS] = {
		{TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK},
		{TILE_NOHOOK, 0, 0, 0, 0, 0, 0, 0, TILE_NOHOOK},
		{TILE_NOHOOK, 0, TILE_FREEZE, TILE_UNFREEZE, 0, TILE_DFREEZE, TILE_DUNFREEZE, 0, TILE_NOHOOK},
		{TILE_NOHOOK, 0, 0, 0, 0, 0, 0, 0, TILE_NOHOOK},
		{TILE_NOHOOK, 0, 0, 0, TILE_DEATH, 0, 0, 0, TILE_NOHOOK},
		{TILE_NOHOOK, 0, TILE_TELEIN, 0, 0, 0, TILE_TELEOUT, 0, TILE_NOHOOK},
		{TILE_NOHOOK, 0, 0, 0, 0, 0, 0, 0, TILE_NOHOOK},
		{TILE_NOHOOK, 0, 0, 0, 0, 0, 0, 0, TILE_NOHOOK},
		{TILE_SOLID, TILE_SOLID, TILE_SOLID, TILE_SOLID, TILE_SOLID, TILE_SOLID, TILE_SOLID, TILE_SOLID, TILE_SOLID},
	};

	const float TileSize = std::min(TextureWidth / (float)COLS, TextureRect.h / (float)ROWS);
	const float GridW = COLS * TileSize;
	const float GridH = ROWS * TileSize;
	const float OffX = TextureRect.x + (TextureRect.w - GridW) / 2.0f;
	const float OffY = TextureRect.y + (TextureRect.h - GridH) / 2.0f;

	const float Inset = 1.5f / 1024.0f;
	const float TileUvSize = 1.0f / 16.0f;

	if(Background.a > 0.0f)
	{
		const CUIRect BackdropRect = {OffX, OffY, GridW, GridH};
		BackdropRect.Draw(Background, IGraphics::CORNER_NONE, 0.0f);
	}

	pGraphics->WrapClamp();
	pGraphics->TextureSet(Texture);
	pGraphics->QuadsBegin();
	pGraphics->SetColor(1, 1, 1, 1);
	for(int r = 0; r < ROWS; r++)
	{
		for(int c = 0; c < COLS; c++)
		{
			unsigned char TileIndex = s_aLayout[r][c];
			if(TileIndex == 0)
				continue;

			const int Tx = TileIndex % 16;
			const int Ty = TileIndex / 16;
			const float U0 = Tx * TileUvSize + Inset;
			const float V0 = Ty * TileUvSize + Inset;
			const float U1 = U0 + TileUvSize - Inset * 2;
			const float V1 = V0 + TileUvSize - Inset * 2;
			pGraphics->QuadsSetSubset(U0, V0, U1, V1);
			IGraphics::CQuadItem QuadItem(OffX + c * TileSize, OffY + r * TileSize, TileSize, TileSize);
			pGraphics->QuadsDrawTL(&QuadItem, 1);
		}
	}
	pGraphics->QuadsEnd();
	pGraphics->WrapNormal();
}

void RenderGunpackCard(IGraphics *pGraphics, CUi *pUi, const IGraphics::CTextureHandle &Texture, const CUIRect &CardRect, const char *pName)
{
	static constexpr float Pad = 10.0f;
	static constexpr float LabelH = 18.0f;
	static constexpr float Gap = 6.0f;
	static constexpr int COLS = 2;
	static constexpr int ROWS = 3;
	static constexpr int s_aWeaponSprites[6] = {
		SPRITE_WEAPON_HAMMER_BODY,
		SPRITE_WEAPON_GUN_BODY,
		SPRITE_WEAPON_SHOTGUN_BODY,
		SPRITE_WEAPON_GRENADE_BODY,
		SPRITE_WEAPON_LASER_BODY,
		SPRITE_WEAPON_NINJA_BODY,
	};

	CUIRect Content = CardRect;
	Content.Margin(Pad, &Content);

	CUIRect LabelRect, GridRect;
	Content.HSplitBottom(LabelH, &GridRect, &LabelRect);
	GridRect.HSplitBottom(Gap, &GridRect, nullptr);

	if(pName && pName[0] != '\0')
		pUi->DoLabel(&LabelRect, pName, LabelRect.h - 2.0f, TEXTALIGN_MC);

	if(!Texture.IsValid())
		return;

	const float CellW = GridRect.w / (float)COLS;
	const float CellH = GridRect.h / (float)ROWS;
	const float CellPad = 4.0f;

	pGraphics->WrapClamp();
	pGraphics->TextureSet(Texture);
	pGraphics->QuadsBegin();
	pGraphics->SetColor(1, 1, 1, 1);
	for(int i = 0; i < 6; i++)
	{
		const int Col = i % COLS;
		const int Row = i / COLS;
		CUIRect Cell;
		Cell.x = GridRect.x + Col * CellW + CellPad;
		Cell.y = GridRect.y + Row * CellH + CellPad;
		Cell.w = CellW - CellPad * 2.0f;
		Cell.h = CellH - CellPad * 2.0f;

		float ScaleX, ScaleY;
		pGraphics->GetSpriteScale(s_aWeaponSprites[i], ScaleX, ScaleY);
		const float MaxDim = std::min(Cell.w / ScaleX, Cell.h / ScaleY);
		const float DrawW = MaxDim * ScaleX;
		const float DrawH = MaxDim * ScaleY;
		const float DrawX = Cell.x + (Cell.w - DrawW) / 2.0f;
		const float DrawY = Cell.y + (Cell.h - DrawH) / 2.0f;

		pGraphics->SelectSprite(s_aWeaponSprites[i]);
		IGraphics::CQuadItem QuadItem(DrawX, DrawY, DrawW, DrawH);
		pGraphics->QuadsDrawTL(&QuadItem, 1);
	}
	pGraphics->QuadsEnd();
	pGraphics->WrapNormal();
}

}
