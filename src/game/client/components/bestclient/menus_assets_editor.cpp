/* Copyright © 2026 BestProject Team */
#include <base/color.h>
#include <base/io.h>
#include <base/math.h>
#include <base/str.h>

#include <engine/font_icons.h>
#include <engine/gfx/image_loader.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/graphics.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/lineinput.h>
#include <game/localization.h>

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <utility>
#include <vector>

using namespace FontIcon;

namespace
{
constexpr float FontSize = 14.0f;
constexpr float EditBoxFontSize = 12.0f;
constexpr float LineSize = 20.0f;
constexpr float MarginSmall = 5.0f;
constexpr float MarginExtraSmall = 2.5f;

constexpr int ASSETS_EDITOR_CAT_ENTITIES = 0;
constexpr int ASSETS_EDITOR_CAT_GAME = 1;
constexpr int ASSETS_EDITOR_CAT_EMOTICONS = 2;
constexpr int ASSETS_EDITOR_CAT_PARTICLES = 3;
constexpr int ASSETS_EDITOR_CAT_HUD = 4;
constexpr int ASSETS_EDITOR_CAT_EXTRAS = 5;
constexpr int ASSETS_EDITOR_CAT_CURSOR = 6;
constexpr int ASSETS_EDITOR_CAT_ARROW = 7;
constexpr int ASSETS_EDITOR_COLOR_BLEND_TEELIKE = 0;
constexpr int ASSETS_EDITOR_COLOR_BLEND_SCREEN = 1;
constexpr int ASSETS_EDITOR_COLOR_BLEND_MULTIPLY = 2;
constexpr int ASSETS_EDITOR_COLOR_BLEND_OVERLAY = 3;
constexpr int ASSETS_EDITOR_COLOR_BLEND_COUNT = 4;

struct SScopedClip
{
	CUi *m_pUi;
	~SScopedClip() { m_pUi->ClipDisable(); }
};

struct SAssetsEditorColorPopupContext : public SPopupMenuId
{
	CMenus *m_pMenus = nullptr;
	int m_SlotIndex = -1;
	CButtonContainer m_DoneButton;
	CButtonContainer m_ClearButton;
	CButtonContainer m_aBlendButtons[ASSETS_EDITOR_COLOR_BLEND_COUNT];
	char m_OpacityScrollbarId = 0;
};

SAssetsEditorColorPopupContext gs_AssetsEditorColorPopup;

const char *AssetsEditorBlendName(int Mode)
{
	switch(Mode)
	{
	case ASSETS_EDITOR_COLOR_BLEND_TEELIKE: return Localize("TeeLike");
	case ASSETS_EDITOR_COLOR_BLEND_SCREEN: return Localize("Screen");
	case ASSETS_EDITOR_COLOR_BLEND_MULTIPLY: return Localize("Multiply");
	case ASSETS_EDITOR_COLOR_BLEND_OVERLAY: return Localize("Overlay");
	default: return Localize("TeeLike");
	}
}

float AssetsEditorOverlayChannel(float Base, float Blend)
{
	if(Base < 0.5f)
		return 2.0f * Base * Blend;
	return 1.0f - 2.0f * (1.0f - Base) * (1.0f - Blend);
}

const char *AssetsEditorCategoryFolder(int Category)
{
	switch(Category)
	{
	case ASSETS_EDITOR_CAT_ENTITIES: return "entities";
	case ASSETS_EDITOR_CAT_EMOTICONS: return "emoticons";
	case ASSETS_EDITOR_CAT_PARTICLES: return "particles";
	case ASSETS_EDITOR_CAT_HUD: return "hud";
	case ASSETS_EDITOR_CAT_EXTRAS: return "extras";
	case ASSETS_EDITOR_CAT_CURSOR: return "cursor";
	case ASSETS_EDITOR_CAT_ARROW: return "arrow";
	default: return "game";
	}
}

int AssetsEditorImageId(int Category)
{
	switch(Category)
	{
	case ASSETS_EDITOR_CAT_EMOTICONS: return IMAGE_EMOTICONS;
	case ASSETS_EDITOR_CAT_PARTICLES: return IMAGE_PARTICLES;
	case ASSETS_EDITOR_CAT_HUD: return IMAGE_HUD;
	case ASSETS_EDITOR_CAT_EXTRAS: return IMAGE_EXTRAS;
	case ASSETS_EDITOR_CAT_CURSOR: return IMAGE_CURSOR;
	case ASSETS_EDITOR_CAT_ARROW: return IMAGE_ARROW;
	case ASSETS_EDITOR_CAT_ENTITIES: return -1;
	default: return IMAGE_GAME;
	}
}

int AssetsEditorGridSpriteId(int Category)
{
	switch(Category)
	{
	case ASSETS_EDITOR_CAT_EMOTICONS: return SPRITE_OOP;
	case ASSETS_EDITOR_CAT_HUD: return SPRITE_HUD_AIRJUMP;
	case ASSETS_EDITOR_CAT_PARTICLES: return SPRITE_PART_SLICE;
	case ASSETS_EDITOR_CAT_EXTRAS: return SPRITE_PART_SNOWFLAKE;
	default: return SPRITE_HEALTH_FULL;
	}
}

int AssetsEditorGridX(int Category)
{
	if(Category == ASSETS_EDITOR_CAT_ENTITIES)
		return 16;
	if(Category == ASSETS_EDITOR_CAT_CURSOR || Category == ASSETS_EDITOR_CAT_ARROW)
		return 1;
	const CDataSprite &Sprite = g_pData->m_aSprites[AssetsEditorGridSpriteId(Category)];
	if(Sprite.m_pSet == nullptr || Sprite.m_pSet->m_Gridx <= 0)
		return 1;
	return Sprite.m_pSet->m_Gridx;
}

int AssetsEditorGridY(int Category)
{
	if(Category == ASSETS_EDITOR_CAT_ENTITIES)
		return 16;
	if(Category == ASSETS_EDITOR_CAT_CURSOR || Category == ASSETS_EDITOR_CAT_ARROW)
		return 1;
	const CDataSprite &Sprite = g_pData->m_aSprites[AssetsEditorGridSpriteId(Category)];
	if(Sprite.m_pSet == nullptr || Sprite.m_pSet->m_Gridy <= 0)
		return 1;
	return Sprite.m_pSet->m_Gridy;
}

int AssetsEditorFindSpriteIdByName(const char *pName, int ImageId)
{
	if(pName == nullptr || pName[0] == '\0')
		return -1;
	const CDataImage *pImage = ImageId >= 0 ? &g_pData->m_aImages[ImageId] : nullptr;
	for(int SpriteId = 0; SpriteId < NUM_SPRITES; ++SpriteId)
	{
		const CDataSprite &Sprite = g_pData->m_aSprites[SpriteId];
		if(Sprite.m_pName == nullptr || str_comp(Sprite.m_pName, pName) != 0)
			continue;
		if(Sprite.m_W <= 0 || Sprite.m_H <= 0 || Sprite.m_pSet == nullptr)
			continue;
		if(pImage != nullptr && Sprite.m_pSet->m_pImage != pImage)
			continue;
		return SpriteId;
	}
	return -1;
}

void AssetsEditorStripTrailingDigits(const char *pIn, char *pOut, int OutSize)
{
	str_copy(pOut, pIn, OutSize);
	int Len = str_length(pOut);
	while(Len > 0 && std::isdigit(static_cast<unsigned char>(pOut[Len - 1])))
		pOut[--Len] = '\0';
}

void AssetsEditorBuildFamilyKey(int Category, const CDataSprite *pSprite, char *pOut, int OutSize)
{
	if(pOut == nullptr || OutSize <= 0)
		return;
	if(pSprite == nullptr || pSprite->m_pName == nullptr)
	{
		str_copy(pOut, "part", OutSize);
		return;
	}
	const char *pName = pSprite->m_pName;
	if(Category == ASSETS_EDITOR_CAT_GAME && str_comp_num(pName, "weapon_", 7) == 0)
	{
		const char *pAfterWeapon = pName + 7;
		const char *pLastUnderscore = str_rchr(pAfterWeapon, '_');
		if(pLastUnderscore != nullptr && pLastUnderscore[1] != '\0')
		{
			char aPart[64];
			AssetsEditorStripTrailingDigits(pLastUnderscore + 1, aPart, sizeof(aPart));
			str_format(pOut, OutSize, "weapon:*:%s", aPart);
			return;
		}
	}
	else if(Category == ASSETS_EDITOR_CAT_HUD)
	{
		if(str_find(pName, "_hit_disabled") != nullptr)
		{
			str_copy(pOut, "hud:*_hit_disabled", OutSize);
			return;
		}
		if(str_comp_num(pName, "hud_freeze_bar_", 15) == 0)
		{
			str_copy(pOut, "hud:freeze_bar", OutSize);
			return;
		}
		if(str_comp_num(pName, "hud_ninja_bar_", 14) == 0)
		{
			str_copy(pOut, "hud:ninja_bar", OutSize);
			return;
		}
	}
	char aNormalized[64];
	AssetsEditorStripTrailingDigits(pName, aNormalized, sizeof(aNormalized));
	str_copy(pOut, aNormalized, OutSize);
}

bool AssetsEditorCalcFittedRect(const CUIRect &Rect, int SourceWidth, int SourceHeight, CUIRect &OutRect)
{
	if(SourceWidth <= 0 || SourceHeight <= 0 || Rect.w <= 0.0f || Rect.h <= 0.0f)
		return false;
	float DrawW = Rect.w;
	float DrawH = DrawW * ((float)SourceHeight / (float)SourceWidth);
	if(DrawH > Rect.h)
	{
		DrawH = Rect.h;
		DrawW = DrawH * ((float)SourceWidth / (float)SourceHeight);
	}
	OutRect.x = Rect.x + (Rect.w - DrawW) / 2.0f;
	OutRect.y = Rect.y + (Rect.h - DrawH) / 2.0f;
	OutRect.w = DrawW;
	OutRect.h = DrawH;
	return true;
}

bool AssetsEditorSlotRect(const CUIRect &FittedRect, int GridX, int GridY, int X, int Y, int W, int H, CUIRect &OutRect)
{
	GridX = std::max(1, GridX);
	GridY = std::max(1, GridY);
	if(W <= 0 || H <= 0)
		return false;
	OutRect.x = FittedRect.x + ((float)X / GridX) * FittedRect.w;
	OutRect.y = FittedRect.y + ((float)Y / GridY) * FittedRect.h;
	OutRect.w = ((float)W / GridX) * FittedRect.w;
	OutRect.h = ((float)H / GridY) * FittedRect.h;
	return true;
}

bool AssetsEditorDrawTextureFitted(const CUIRect &Rect, IGraphics::CTextureHandle Texture, int SourceWidth, int SourceHeight, IGraphics *pGraphics, CUIRect *pOutFittedRect)
{
	if(!Texture.IsValid())
		return false;
	CUIRect FittedRect;
	if(!AssetsEditorCalcFittedRect(Rect, SourceWidth, SourceHeight, FittedRect))
		return false;
	if(pOutFittedRect != nullptr)
		*pOutFittedRect = FittedRect;
	pGraphics->WrapClamp();
	pGraphics->TextureSet(Texture);
	pGraphics->QuadsBegin();
	pGraphics->SetColor(1, 1, 1, 1);
	const IGraphics::CQuadItem Quad(FittedRect.x, FittedRect.y, FittedRect.w, FittedRect.h);
	pGraphics->QuadsDrawTL(&Quad, 1);
	pGraphics->QuadsEnd();
	pGraphics->WrapNormal();
	return true;
}

void AssetsEditorDrawSlot(const CUIRect &Rect, IGraphics::CTextureHandle Texture, int GridX, int GridY, int SrcX, int SrcY, int SrcW, int SrcH, float Alpha, IGraphics *pGraphics)
{
	if(!Texture.IsValid() || SrcW <= 0 || SrcH <= 0)
		return;
	GridX = std::max(1, GridX);
	GridY = std::max(1, GridY);
	const float U0 = (float)SrcX / GridX;
	const float V0 = (float)SrcY / GridY;
	const float U1 = (float)(SrcX + SrcW) / GridX;
	const float V1 = (float)(SrcY + SrcH) / GridY;
	pGraphics->WrapClamp();
	pGraphics->TextureSet(Texture);
	pGraphics->QuadsBegin();
	pGraphics->SetColor(1.0f, 1.0f, 1.0f, Alpha);
	pGraphics->QuadsSetSubset(U0, V0, U1, V1);
	const IGraphics::CQuadItem Quad(Rect.x, Rect.y, Rect.w, Rect.h);
	pGraphics->QuadsDrawTL(&Quad, 1);
	pGraphics->QuadsSetSubset(0, 0, 1, 1);
	pGraphics->QuadsEnd();
	pGraphics->WrapNormal();
}

bool AssetsEditorExportNameValid(const char *pName)
{
	if(pName == nullptr || pName[0] == '\0' || str_comp(pName, "default") == 0 || str_comp(pName, ".") == 0 || str_comp(pName, "..") == 0)
		return false;
	if(str_find(pName, "..") != nullptr)
		return false;
	for(const char *pCursor = pName; *pCursor != '\0'; ++pCursor)
	{
		const unsigned char Char = static_cast<unsigned char>(*pCursor);
		if(Char < 32 || std::strchr("\\/:*?\"<>|", *pCursor) != nullptr)
			return false;
	}
	return true;
}

void AssetsEditorSetIconFont(ITextRender *pTextRender)
{
	pTextRender->SetFontPreset(EFontPreset::ICON_FONT);
	pTextRender->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
}

void AssetsEditorClearIconFont(ITextRender *pTextRender)
{
	pTextRender->SetRenderFlags(0);
	pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);
}

void AssetsEditorDrawBrushCursor(CUi *pUi, ITextRender *pTextRender, float MouseX, float MouseY)
{
	const float Size = 22.0f;
	CUIRect Icon;
	Icon.w = Size;
	Icon.h = Size;
	Icon.x = MouseX;
	Icon.y = MouseY - Size;
	AssetsEditorSetIconFont(pTextRender);
	pTextRender->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	pUi->DoLabel(&Icon, BRUSH, Size * 0.9f, TEXTALIGN_BL);
	pTextRender->TextColor(pTextRender->DefaultTextColor());
	AssetsEditorClearIconFont(pTextRender);
	SDL_ShowCursor(SDL_DISABLE);
}
}

bool CMenus::AssetsEditorResolvePath(int Category, const char *pName, char *pOut, int OutSize) const
{
	static_assert(ASSETS_EDITOR_CAT_ENTITIES == 0 && ASSETS_EDITOR_CAT_GAME == 1 && ASSETS_EDITOR_CAT_EMOTICONS == 2 && ASSETS_EDITOR_CAT_PARTICLES == 3 && ASSETS_EDITOR_CAT_HUD == 4 && ASSETS_EDITOR_CAT_EXTRAS == 5 && ASSETS_EDITOR_CAT_CURSOR == 6 && ASSETS_EDITOR_CAT_ARROW == 7 && ASSETS_EDITOR_CAT_COUNT == 8);
	static_assert(ASSETS_EDITOR_COLOR_BLEND_TEELIKE == 0 && ASSETS_EDITOR_COLOR_BLEND_SCREEN == 1 && ASSETS_EDITOR_COLOR_BLEND_MULTIPLY == 2 && ASSETS_EDITOR_COLOR_BLEND_OVERLAY == 3 && ASSETS_EDITOR_COLOR_BLEND_COUNT == 4);
	if(pOut == nullptr || OutSize <= 0 || pName == nullptr || pName[0] == '\0' || Category < 0 || Category >= ASSETS_EDITOR_CAT_COUNT)
		return false;

	auto Accept = [&](const char *pPath) {
		if(pPath == nullptr || pPath[0] == '\0' || !Storage()->FileExists(pPath, IStorage::TYPE_ALL))
			return false;
		str_copy(pOut, pPath, OutSize);
		return true;
	};

	char aPath[IO_MAX_PATH_LENGTH];
	const bool IsDefault = str_comp(pName, "default") == 0;
	if(Category == ASSETS_EDITOR_CAT_ENTITIES)
	{
		for(const char *pMod : gs_apModEntitiesNames)
		{
			if(IsDefault)
				str_format(aPath, sizeof(aPath), "editor/entities_clear/%s.png", pMod);
			else
				str_format(aPath, sizeof(aPath), "assets/entities/%s/%s.png", pName, pMod);
			if(Accept(aPath))
				return true;
		}
		if(!IsDefault)
		{
			str_format(aPath, sizeof(aPath), "assets/entities/%s.png", pName);
			return Accept(aPath);
		}
		return false;
	}

	const char *pFolder = AssetsEditorCategoryFolder(Category);
	if(IsDefault)
	{
		const int ImageId = AssetsEditorImageId(Category);
		if(ImageId < 0)
			return false;
		return Accept(g_pData->m_aImages[ImageId].m_pFilename);
	}

	str_format(aPath, sizeof(aPath), "assets/%s/%s.png", pFolder, pName);
	if(Accept(aPath))
		return true;
	if(Category == ASSETS_EDITOR_CAT_CURSOR)
	{
		str_format(aPath, sizeof(aPath), "assets/cursor/%s/gui_cursor.png", pName);
		if(Accept(aPath))
			return true;
		str_format(aPath, sizeof(aPath), "assets/cursor/%s/cursor.png", pName);
		return Accept(aPath);
	}
	if(Category == ASSETS_EDITOR_CAT_ARROW)
	{
		str_format(aPath, sizeof(aPath), "assets/arrow/%s/arrow.png", pName);
		return Accept(aPath);
	}
	str_format(aPath, sizeof(aPath), "assets/%s/%s/%s.png", pFolder, pName, pFolder);
	return Accept(aPath);
}

bool CMenus::AssetsEditorLoadImage(int Category, const char *pName, SAssetsEditorImage &Out)
{
	char aPath[IO_MAX_PATH_LENGTH];
	if(!AssetsEditorResolvePath(Category, pName, aPath, sizeof(aPath)))
		return false;

	CImageInfo Loaded;
	if(!Graphics()->LoadPng(Loaded, aPath, IStorage::TYPE_ALL))
		return false;
	if(!Graphics()->CheckImageDivisibility(aPath, Loaded, AssetsEditorGridX(Category), AssetsEditorGridY(Category), true))
	{
		Loaded.Free();
		return false;
	}
	ConvertToRgba(Loaded);
	if(Loaded.m_pData == nullptr || Loaded.m_Width == 0 || Loaded.m_Height == 0)
	{
		Loaded.Free();
		return false;
	}

	CImageInfo Copy = Loaded.DeepCopy();
	IGraphics::CTextureHandle Texture = Graphics()->LoadTextureRawMove(Loaded, 0, aPath);
	if(!Texture.IsValid() || Copy.m_pData == nullptr)
	{
		Copy.Free();
		if(Texture.IsValid())
			Graphics()->UnloadTexture(&Texture);
		return false;
	}

	Graphics()->UnloadTexture(&Out.m_Texture);
	Out.m_Image.Free();
	Out.m_Category = Category;
	str_copy(Out.m_aName, pName, sizeof(Out.m_aName));
	str_copy(Out.m_aPath, aPath, sizeof(Out.m_aPath));
	Out.m_Width = (int)Copy.m_Width;
	Out.m_Height = (int)Copy.m_Height;
	Out.m_Image = std::move(Copy);
	Out.m_Texture = Texture;
	return true;
}

void CMenus::AssetsEditorRebuildSlots(int Side)
{
	const SAssetsEditorImage &Image = Side == ASSETS_EDITOR_SIDE_LEFT ? m_AssetsEditorState.m_Left : m_AssetsEditorState.m_Right;
	std::vector<SAssetsEditorPartSlot> &vSlots = Side == ASSETS_EDITOR_SIDE_LEFT ? m_AssetsEditorState.m_vDonorSlots : m_AssetsEditorState.m_vTargetSlots;
	vSlots.clear();
	const int Category = Image.m_Category;

	auto PushCell = [&](int SpriteId, int X, int Y, int W, int H, const char *pFamily) {
		SAssetsEditorPartSlot Slot;
		Slot.m_SpriteId = SpriteId;
		Slot.m_DstX = X;
		Slot.m_DstY = Y;
		Slot.m_DstW = W;
		Slot.m_DstH = H;
		Slot.m_SrcX = X;
		Slot.m_SrcY = Y;
		Slot.m_SrcW = W;
		Slot.m_SrcH = H;
		str_copy(Slot.m_aFamilyKey, pFamily, sizeof(Slot.m_aFamilyKey));
		vSlots.push_back(Slot);
	};

	if(Category == ASSETS_EDITOR_CAT_ENTITIES)
	{
		for(int Y = 0; Y < 16; ++Y)
		{
			for(int X = 0; X < 16; ++X)
			{
				char aFamily[64];
				str_format(aFamily, sizeof(aFamily), "entities:tile_%03d", Y * 16 + X);
				PushCell(-1, X, Y, 1, 1, aFamily);
			}
		}
		return;
	}
	if(Category == ASSETS_EDITOR_CAT_CURSOR || Category == ASSETS_EDITOR_CAT_ARROW)
	{
		PushCell(-1, 0, 0, 1, 1, "image");
		return;
	}

	const int ImageId = AssetsEditorImageId(Category);
	if(ImageId < 0)
		return;
	const CDataImage *pImage = &g_pData->m_aImages[ImageId];
	const bool Deduplicate = Category == ASSETS_EDITOR_CAT_GAME || Category == ASSETS_EDITOR_CAT_PARTICLES;
	for(int SpriteId = 0; SpriteId < NUM_SPRITES; ++SpriteId)
	{
		const CDataSprite &Sprite = g_pData->m_aSprites[SpriteId];
		if(Sprite.m_pSet == nullptr || Sprite.m_pSet->m_pImage != pImage || Sprite.m_W <= 0 || Sprite.m_H <= 0)
			continue;
		if(Deduplicate)
		{
			bool Exists = false;
			for(const SAssetsEditorPartSlot &Existing : vSlots)
			{
				if(Existing.m_DstX == Sprite.m_X && Existing.m_DstY == Sprite.m_Y && Existing.m_DstW == Sprite.m_W && Existing.m_DstH == Sprite.m_H)
				{
					Exists = true;
					break;
				}
			}
			if(Exists)
				continue;
		}
		char aFamily[64];
		AssetsEditorBuildFamilyKey(Category, &Sprite, aFamily, sizeof(aFamily));
		PushCell(SpriteId, Sprite.m_X, Sprite.m_Y, Sprite.m_W, Sprite.m_H, aFamily);
	}

	auto HasGeometry = [&](int X, int Y, int W, int H) {
		for(const SAssetsEditorPartSlot &Slot : vSlots)
		{
			if(Slot.m_DstX == X && Slot.m_DstY == Y && Slot.m_DstW == W && Slot.m_DstH == H)
				return true;
		}
		return false;
	};
	auto AddNamed = [&](const char *pName, const char *pAlias, int X, int Y, int W, int H) {
		if(HasGeometry(X, Y, W, H))
			return;
		int SpriteId = AssetsEditorFindSpriteIdByName(pName, ImageId);
		if(SpriteId < 0 && pAlias != nullptr)
			SpriteId = AssetsEditorFindSpriteIdByName(pAlias, ImageId);
		if(SpriteId >= 0)
		{
			const CDataSprite &Sprite = g_pData->m_aSprites[SpriteId];
			char aFamily[64];
			AssetsEditorBuildFamilyKey(Category, &Sprite, aFamily, sizeof(aFamily));
			PushCell(SpriteId, Sprite.m_X, Sprite.m_Y, Sprite.m_W, Sprite.m_H, aFamily);
		}
		else
		{
			PushCell(-1, X, Y, W, H, pName);
		}
	};

	if(Category == ASSETS_EDITOR_CAT_GAME)
	{
		AddNamed("ninja_bar_full_left", nullptr, 21, 4, 1, 2);
		AddNamed("ninja_bar_full", nullptr, 22, 4, 1, 2);
		AddNamed("ninja_bar_empty", nullptr, 23, 4, 1, 2);
		AddNamed("ninja_bar_empty_right", nullptr, 24, 4, 1, 2);
		AddNamed("pickup_health", "pickup_heart", 10, 2, 2, 2);
		AddNamed("pickup_armor", nullptr, 12, 2, 2, 2);
		AddNamed("pickup_armor_shotgun", nullptr, 15, 2, 2, 2);
	}
	else if(Category == ASSETS_EDITOR_CAT_PARTICLES)
	{
		AddNamed("part_slice", nullptr, 0, 0, 1, 1);
		AddNamed("part_ball", nullptr, 1, 0, 1, 1);
		AddNamed("part_splat01", nullptr, 2, 0, 1, 1);
		AddNamed("part_splat02", nullptr, 3, 0, 1, 1);
		AddNamed("part_splat03", nullptr, 4, 0, 1, 1);
		AddNamed("part_smoke", nullptr, 0, 1, 1, 1);
		AddNamed("part_shell", nullptr, 0, 2, 2, 2);
		AddNamed("part_expl01", nullptr, 0, 4, 4, 4);
		AddNamed("part_airjump", nullptr, 2, 2, 2, 2);
		AddNamed("part_hit01", nullptr, 4, 1, 2, 2);
	}
}

bool CMenus::AssetsEditorAssignPickedAsset(int Side, int Category, const char *pName)
{
	if((Side != ASSETS_EDITOR_SIDE_LEFT && Side != ASSETS_EDITOR_SIDE_RIGHT) || Category < 0 || Category >= ASSETS_EDITOR_CAT_COUNT)
		return false;

	SAssetsEditorImage Loaded;
	if(!AssetsEditorLoadImage(Category, pName, Loaded))
	{
		str_copy(m_AssetsEditorState.m_aStatusMessage, Localize("Failed to load asset."), sizeof(m_AssetsEditorState.m_aStatusMessage));
		m_AssetsEditorState.m_StatusIsError = true;
		m_AssetsEditorState.m_ExploreSide = -1;
		return false;
	}

	SAssetsEditorImage &Dest = Side == ASSETS_EDITOR_SIDE_LEFT ? m_AssetsEditorState.m_Left : m_AssetsEditorState.m_Right;
	Graphics()->UnloadTexture(&Dest.m_Texture);
	Dest.m_Image.Free();
	Dest = std::move(Loaded);
	AssetsEditorRebuildSlots(Side);
	AssetsEditorCancelDrag();
	m_AssetsEditorState.m_DirtyPreview = true;
	m_AssetsEditorState.m_ExploreSide = -1;
	m_AssetsEditorState.m_ShowExitConfirm = false;
	if(Ui()->IsPopupOpen(&gs_AssetsEditorColorPopup))
		Ui()->ClosePopupMenu(&gs_AssetsEditorColorPopup);
	m_AssetsEditorState.m_ColorEditSlot = -1;

	if(Side == ASSETS_EDITOR_SIDE_RIGHT)
	{
		m_AssetsEditorState.m_HasUnsavedChanges = false;
	}
	else
	{
		for(const SAssetsEditorPartSlot &Slot : m_AssetsEditorState.m_vTargetSlots)
		{
			if(Slot.m_FromDonor || Slot.m_UseCustomColor)
			{
				m_AssetsEditorState.m_HasUnsavedChanges = true;
				break;
			}
		}
	}

	str_format(m_AssetsEditorState.m_aStatusMessage, sizeof(m_AssetsEditorState.m_aStatusMessage), Localize("Loaded %s."), pName);
	m_AssetsEditorState.m_StatusIsError = false;
	return true;
}

bool CMenus::AssetsEditorCopyScaled(CImageInfo &Dst, const CImageInfo &Src, int DstX, int DstY, int DstW, int DstH, int SrcX, int SrcY, int SrcW, int SrcH) const
{
	if(Dst.m_pData == nullptr || Src.m_pData == nullptr || Dst.m_Format != CImageInfo::FORMAT_RGBA || Src.m_Format != CImageInfo::FORMAT_RGBA)
		return false;
	if(DstW <= 0 || DstH <= 0 || SrcW <= 0 || SrcH <= 0 || DstX < 0 || DstY < 0 || SrcX < 0 || SrcY < 0)
		return false;
	if(DstX + DstW > (int)Dst.m_Width || DstY + DstH > (int)Dst.m_Height)
		return false;
	if(SrcX + SrcW > (int)Src.m_Width || SrcY + SrcH > (int)Src.m_Height)
		return false;

	uint8_t *pDstData = Dst.m_pData;
	const uint8_t *pSrcData = Src.m_pData;
	for(int Y = 0; Y < DstH; ++Y)
	{
		const int SampleY = SrcY + ((int64_t)Y * SrcH) / DstH;
		for(int X = 0; X < DstW; ++X)
		{
			const int SampleX = SrcX + ((int64_t)X * SrcW) / DstW;
			const int DstOff = ((DstY + Y) * (int)Dst.m_Width + (DstX + X)) * 4;
			const int SrcOff = (SampleY * (int)Src.m_Width + SampleX) * 4;
			pDstData[DstOff + 0] = pSrcData[SrcOff + 0];
			pDstData[DstOff + 1] = pSrcData[SrcOff + 1];
			pDstData[DstOff + 2] = pSrcData[SrcOff + 2];
			pDstData[DstOff + 3] = pSrcData[SrcOff + 3];
		}
	}
	return true;
}

void CMenus::AssetsEditorColorize(CImageInfo &Image, int X, int Y, int W, int H, const SAssetsEditorPartSlot &Slot) const
{
	if(Image.m_pData == nullptr || Image.m_Format != CImageInfo::FORMAT_RGBA || W <= 0 || H <= 0 || X < 0 || Y < 0)
		return;
	if(X + W > (int)Image.m_Width || Y + H > (int)Image.m_Height)
		return;

	const ColorRGBA Tint = color_cast<ColorRGBA>(ColorHSLA(Slot.m_CustomColor).UnclampLighting(ColorHSLA::DARKEST_LGT));
	const float Opacity = std::clamp(Slot.m_ColorOpacity, 0, 100) / 100.0f;
	if(Opacity <= 0.0f)
		return;

	const int BlendMode = std::clamp(Slot.m_ColorBlendMode, 0, ASSETS_EDITOR_COLOR_BLEND_COUNT - 1);
	uint8_t *pData = Image.m_pData;
	for(int Py = 0; Py < H; ++Py)
	{
		for(int Px = 0; Px < W; ++Px)
		{
			const int Off = ((Y + Py) * (int)Image.m_Width + (X + Px)) * 4;
			if(pData[Off + 3] == 0)
				continue;
			const float SrcR = pData[Off + 0] / 255.0f;
			const float SrcG = pData[Off + 1] / 255.0f;
			const float SrcB = pData[Off + 2] / 255.0f;
			float OutR = SrcR;
			float OutG = SrcG;
			float OutB = SrcB;
			switch(BlendMode)
			{
			case ASSETS_EDITOR_COLOR_BLEND_TEELIKE:
			{
				const float Luma = 0.2126f * SrcR + 0.7152f * SrcG + 0.0722f * SrcB;
				OutR = Luma * Tint.r;
				OutG = Luma * Tint.g;
				OutB = Luma * Tint.b;
				break;
			}
			case ASSETS_EDITOR_COLOR_BLEND_SCREEN:
				OutR = 1.0f - (1.0f - SrcR) * (1.0f - Tint.r);
				OutG = 1.0f - (1.0f - SrcG) * (1.0f - Tint.g);
				OutB = 1.0f - (1.0f - SrcB) * (1.0f - Tint.b);
				break;
			case ASSETS_EDITOR_COLOR_BLEND_MULTIPLY:
				OutR = SrcR * Tint.r;
				OutG = SrcG * Tint.g;
				OutB = SrcB * Tint.b;
				break;
			case ASSETS_EDITOR_COLOR_BLEND_OVERLAY:
				OutR = AssetsEditorOverlayChannel(SrcR, Tint.r);
				OutG = AssetsEditorOverlayChannel(SrcG, Tint.g);
				OutB = AssetsEditorOverlayChannel(SrcB, Tint.b);
				break;
			default:
				break;
			}
			OutR = mix(SrcR, OutR, Opacity);
			OutG = mix(SrcG, OutG, Opacity);
			OutB = mix(SrcB, OutB, Opacity);
			pData[Off + 0] = (uint8_t)std::clamp(round_to_int(OutR * 255.0f), 0, 255);
			pData[Off + 1] = (uint8_t)std::clamp(round_to_int(OutG * 255.0f), 0, 255);
			pData[Off + 2] = (uint8_t)std::clamp(round_to_int(OutB * 255.0f), 0, 255);
		}
	}
}

bool CMenus::AssetsEditorComposeImage(CImageInfo &OutputImage)
{
	const SAssetsEditorImage &Base = m_AssetsEditorState.m_Right;
	if(Base.m_Image.m_pData == nullptr)
	{
		str_copy(m_AssetsEditorState.m_aStatusMessage, Localize("No assets available for composition."), sizeof(m_AssetsEditorState.m_aStatusMessage));
		m_AssetsEditorState.m_StatusIsError = true;
		return false;
	}

	OutputImage = Base.m_Image.DeepCopy();
	const SAssetsEditorImage &Donor = m_AssetsEditorState.m_Left;
	const int DestGridX = std::max(1, AssetsEditorGridX(Base.m_Category));
	const int DestGridY = std::max(1, AssetsEditorGridY(Base.m_Category));
	const int SrcGridX = std::max(1, AssetsEditorGridX(Donor.m_Category));
	const int SrcGridY = std::max(1, AssetsEditorGridY(Donor.m_Category));
	const int DestCellW = (int)OutputImage.m_Width / DestGridX;
	const int DestCellH = (int)OutputImage.m_Height / DestGridY;
	int SkippedSlots = 0;

	for(const SAssetsEditorPartSlot &Slot : m_AssetsEditorState.m_vTargetSlots)
	{
		if(!Slot.m_FromDonor && !Slot.m_UseCustomColor)
			continue;
		if(DestCellW <= 0 || DestCellH <= 0 || Slot.m_DstW <= 0 || Slot.m_DstH <= 0)
		{
			++SkippedSlots;
			continue;
		}
		const int DestX = Slot.m_DstX * DestCellW;
		const int DestY = Slot.m_DstY * DestCellH;
		const int DestW = Slot.m_DstW * DestCellW;
		const int DestH = Slot.m_DstH * DestCellH;
		if(Slot.m_FromDonor)
		{
			if(Donor.m_Image.m_pData == nullptr)
			{
				++SkippedSlots;
				continue;
			}
			const int SrcCellW = (int)Donor.m_Image.m_Width / SrcGridX;
			const int SrcCellH = (int)Donor.m_Image.m_Height / SrcGridY;
			if(SrcCellW <= 0 || SrcCellH <= 0 || Slot.m_SrcW <= 0 || Slot.m_SrcH <= 0)
			{
				++SkippedSlots;
				continue;
			}
			if(!AssetsEditorCopyScaled(OutputImage, Donor.m_Image, DestX, DestY, DestW, DestH, Slot.m_SrcX * SrcCellW, Slot.m_SrcY * SrcCellH, Slot.m_SrcW * SrcCellW, Slot.m_SrcH * SrcCellH))
			{
				++SkippedSlots;
				continue;
			}
		}
		if(Slot.m_UseCustomColor)
			AssetsEditorColorize(OutputImage, DestX, DestY, DestW, DestH, Slot);
	}

	if(SkippedSlots > 0)
	{
		str_format(m_AssetsEditorState.m_aStatusMessage, sizeof(m_AssetsEditorState.m_aStatusMessage), Localize("Preview built with %d skipped slot(s)."), SkippedSlots);
		m_AssetsEditorState.m_StatusIsError = false;
	}
	return true;
}

bool CMenus::AssetsEditorExport()
{
	if(!AssetsEditorExportNameValid(m_AssetsEditorState.m_aExportName))
	{
		str_copy(m_AssetsEditorState.m_aStatusMessage, Localize("Export name contains invalid characters."), sizeof(m_AssetsEditorState.m_aStatusMessage));
		m_AssetsEditorState.m_StatusIsError = true;
		return false;
	}

	char aPngPath[IO_MAX_PATH_LENGTH];
	const char *pFolder = AssetsEditorCategoryFolder(m_AssetsEditorState.m_Right.m_Category);
	Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
	char aFolder[IO_MAX_PATH_LENGTH];
	str_format(aFolder, sizeof(aFolder), "assets/%s", pFolder);
	Storage()->CreateFolder(aFolder, IStorage::TYPE_SAVE);
	str_format(aPngPath, sizeof(aPngPath), "assets/%s/%s.png", pFolder, m_AssetsEditorState.m_aExportName);
	if(Storage()->FileExists(aPngPath, IStorage::TYPE_SAVE))
	{
		str_copy(m_AssetsEditorState.m_aStatusMessage, Localize("Asset with this name already exists."), sizeof(m_AssetsEditorState.m_aStatusMessage));
		m_AssetsEditorState.m_StatusIsError = true;
		return false;
	}

	CImageInfo OutputImage;
	if(!AssetsEditorComposeImage(OutputImage))
		return false;

	IOHANDLE File = Storage()->OpenFile(aPngPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File || !CImageLoader::SavePng(File, aPngPath, OutputImage))
	{
		OutputImage.Free();
		str_copy(m_AssetsEditorState.m_aStatusMessage, Localize("Failed to write PNG export."), sizeof(m_AssetsEditorState.m_aStatusMessage));
		m_AssetsEditorState.m_StatusIsError = true;
		return false;
	}
	OutputImage.Free();
	str_format(m_AssetsEditorState.m_aStatusMessage, sizeof(m_AssetsEditorState.m_aStatusMessage), Localize("Exported to %s"), aPngPath);
	m_AssetsEditorState.m_StatusIsError = false;
	m_AssetsEditorState.m_HasUnsavedChanges = false;
	return true;
}

void CMenus::AssetsEditorUpdatePreview()
{
	if(!m_AssetsEditorState.m_DirtyPreview)
		return;
	CImageInfo Composed;
	if(AssetsEditorComposeImage(Composed))
	{
		m_AssetsEditorState.m_PreviewWidth = (int)Composed.m_Width;
		m_AssetsEditorState.m_PreviewHeight = (int)Composed.m_Height;
		Graphics()->UnloadTexture(&m_AssetsEditorState.m_PreviewTexture);
		m_AssetsEditorState.m_PreviewTexture = Graphics()->LoadTextureRawMove(Composed, 0, "assets_editor_preview");
	}
	else
	{
		Graphics()->UnloadTexture(&m_AssetsEditorState.m_PreviewTexture);
		m_AssetsEditorState.m_PreviewWidth = 0;
		m_AssetsEditorState.m_PreviewHeight = 0;
	}
	m_AssetsEditorState.m_DirtyPreview = false;
}

void CMenus::AssetsEditorCancelDrag()
{
	m_AssetsEditorState.m_DragActive = false;
	m_AssetsEditorState.m_DraggedDonorSlot = -1;
}

void CMenus::AssetsEditorRequestClose()
{
	AssetsEditorCancelDrag();
	if(m_AssetsEditorState.m_HasUnsavedChanges)
	{
		m_AssetsEditorState.m_ShowExitConfirm = true;
		return;
	}
	AssetsEditorCloseNow();
}

void CMenus::AssetsEditorCloseNow()
{
	if(Ui()->IsPopupOpen(&gs_AssetsEditorColorPopup))
		Ui()->ClosePopupMenu(&gs_AssetsEditorColorPopup);
	Graphics()->UnloadTexture(&m_AssetsEditorState.m_Left.m_Texture);
	Graphics()->UnloadTexture(&m_AssetsEditorState.m_Right.m_Texture);
	Graphics()->UnloadTexture(&m_AssetsEditorState.m_PreviewTexture);
	m_AssetsEditorState.m_Left.m_Image.Free();
	m_AssetsEditorState.m_Right.m_Image.Free();
	m_AssetsEditorState.m_Left = SAssetsEditorImage();
	m_AssetsEditorState.m_Right = SAssetsEditorImage();
	m_AssetsEditorState.m_vDonorSlots.clear();
	m_AssetsEditorState.m_vTargetSlots.clear();
	m_AssetsEditorState.m_DonorHover = SAssetsEditorHoverCycle();
	m_AssetsEditorState.m_TargetHover = SAssetsEditorHoverCycle();
	m_AssetsEditorState.m_Open = false;
	m_AssetsEditorState.m_Initialized = false;
	m_AssetsEditorState.m_ExploreSide = -1;
	m_AssetsEditorState.m_DirtyPreview = true;
	m_AssetsEditorState.m_HasUnsavedChanges = false;
	m_AssetsEditorState.m_ShowExitConfirm = false;
	m_AssetsEditorState.m_PreviewWidth = 0;
	m_AssetsEditorState.m_PreviewHeight = 0;
	m_AssetsEditorState.m_ColorEditSlot = -1;
	m_AssetsEditorState.m_aExportName[0] = '\0';
	m_AssetsEditorState.m_aStatusMessage[0] = '\0';
	m_AssetsEditorState.m_StatusIsError = false;
	AssetsEditorCancelDrag();
	SDL_ShowCursor(SDL_ENABLE);
}

void CMenus::AssetsEditorRenderExitConfirm(const CUIRect &Rect)
{
	CUIRect Overlay = Rect;
	Overlay.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.6f), IGraphics::CORNER_ALL, 0.0f);

	CUIRect Box;
	Box.w = std::min(520.0f, Rect.w - 30.0f);
	Box.h = 140.0f;
	Box.x = Rect.x + (Rect.w - Box.w) * 0.5f;
	Box.y = Rect.y + (Rect.h - Box.h) * 0.5f;
	Box.Draw(ColorRGBA(0.1f, 0.1f, 0.1f, 0.95f), IGraphics::CORNER_ALL, 8.0f);

	CUIRect Title, Message, Buttons;
	Box.Margin(10.0f, &Box);
	Box.HSplitTop(LineSize + 4.0f, &Title, &Box);
	Box.HSplitTop(LineSize, &Message, &Box);
	Box.HSplitBottom(LineSize + 4.0f, &Box, &Buttons);
	Ui()->DoLabel(&Title, Localize("Save asset before closing?"), FontSize * 1.1f, TEXTALIGN_ML);
	Ui()->DoLabel(&Message, Localize("You have unsaved changes."), FontSize, TEXTALIGN_ML);

	CUIRect SaveButton, DiscardButton, CancelButton;
	Buttons.VSplitLeft((Buttons.w - MarginSmall * 2.0f) / 3.0f, &SaveButton, &Buttons);
	Buttons.VSplitLeft(MarginSmall, nullptr, &Buttons);
	Buttons.VSplitLeft((Buttons.w - MarginSmall) / 2.0f, &DiscardButton, &Buttons);
	Buttons.VSplitLeft(MarginSmall, nullptr, &Buttons);
	CancelButton = Buttons;

	static CButtonContainer s_SaveButton;
	static CButtonContainer s_DiscardButton;
	static CButtonContainer s_CancelButton;
	if(DoButton_Menu(&s_SaveButton, Localize("Save"), 0, &SaveButton))
	{
		if(AssetsEditorExport())
			AssetsEditorCloseNow();
	}
	if(DoButton_Menu(&s_DiscardButton, Localize("Discard changes"), 0, &DiscardButton))
		AssetsEditorCloseNow();
	if(DoButton_Menu(&s_CancelButton, Localize("Cancel"), 0, &CancelButton) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
		m_AssetsEditorState.m_ShowExitConfirm = false;
}

void CMenus::AssetsEditorCollectHovered(const CUIRect &Rect, int Category, const std::vector<SAssetsEditorPartSlot> &vSlots, vec2 Mouse, std::vector<int> &vOut) const
{
	vOut.clear();
	if(Rect.w <= 0.0f || Rect.h <= 0.0f)
		return;
	if(Mouse.x < Rect.x || Mouse.x > Rect.x + Rect.w || Mouse.y < Rect.y || Mouse.y > Rect.y + Rect.h)
		return;

	struct SCandidate
	{
		int m_SlotIndex;
		float m_Area;
	};
	std::vector<SCandidate> vCandidates;
	const int GridX = AssetsEditorGridX(Category);
	const int GridY = AssetsEditorGridY(Category);
	for(size_t SlotIndex = 0; SlotIndex < vSlots.size(); ++SlotIndex)
	{
		const SAssetsEditorPartSlot &Slot = vSlots[SlotIndex];
		CUIRect SlotRect;
		if(!AssetsEditorSlotRect(Rect, GridX, GridY, Slot.m_DstX, Slot.m_DstY, Slot.m_DstW, Slot.m_DstH, SlotRect))
			continue;
		if(Mouse.x < SlotRect.x || Mouse.x >= SlotRect.x + SlotRect.w || Mouse.y < SlotRect.y || Mouse.y >= SlotRect.y + SlotRect.h)
			continue;
		vCandidates.push_back({(int)SlotIndex, SlotRect.w * SlotRect.h});
	}
	std::stable_sort(vCandidates.begin(), vCandidates.end(), [](const SCandidate &Left, const SCandidate &Right) {
		if(Left.m_Area != Right.m_Area)
			return Left.m_Area < Right.m_Area;
		return Left.m_SlotIndex < Right.m_SlotIndex;
	});
	for(const SCandidate &Candidate : vCandidates)
		vOut.push_back(Candidate.m_SlotIndex);
}

int CMenus::AssetsEditorResolveHovered(const CUIRect &Rect, int Category, const std::vector<SAssetsEditorPartSlot> &vSlots, vec2 Mouse, bool ClickedLmb, SAssetsEditorHoverCycle &Cycle, const SAssetsEditorPartSlot *pPreferred)
{
	std::vector<int> vCandidates;
	AssetsEditorCollectHovered(Rect, Category, vSlots, Mouse, vCandidates);
	if(vCandidates.empty())
	{
		if(pPreferred == nullptr)
		{
			Cycle.m_PositionX = -1;
			Cycle.m_PositionY = -1;
			Cycle.m_Cursor = 0;
			Cycle.m_vCandidates.clear();
		}
		return -1;
	}
	if(pPreferred != nullptr)
	{
		for(const int Candidate : vCandidates)
		{
			const SAssetsEditorPartSlot &Slot = vSlots[Candidate];
			if(str_comp(Slot.m_aFamilyKey, pPreferred->m_aFamilyKey) != 0)
				continue;
			if(Slot.m_DstW != pPreferred->m_DstW || Slot.m_DstH != pPreferred->m_DstH)
				continue;
			return Candidate;
		}
		return vCandidates.front();
	}

	const int MousePosX = (int)(Mouse.x * 10.0f);
	const int MousePosY = (int)(Mouse.y * 10.0f);
	const bool SamePosition = Cycle.m_PositionX == MousePosX && Cycle.m_PositionY == MousePosY;
	const bool SameCandidates = Cycle.m_vCandidates.size() == vCandidates.size() &&
		std::equal(Cycle.m_vCandidates.begin(), Cycle.m_vCandidates.end(), vCandidates.begin());
	if(!SamePosition || !SameCandidates)
	{
		Cycle.m_PositionX = MousePosX;
		Cycle.m_PositionY = MousePosY;
		Cycle.m_Cursor = 0;
		Cycle.m_vCandidates = vCandidates;
	}
	else if(ClickedLmb && vCandidates.size() > 1)
	{
		Cycle.m_Cursor = (Cycle.m_Cursor + 1) % (int)vCandidates.size();
	}
	Cycle.m_Cursor = std::clamp(Cycle.m_Cursor, 0, (int)vCandidates.size() - 1);
	return vCandidates[Cycle.m_Cursor];
}

void CMenus::AssetsEditorApplyDrop(int TargetSlotIndex, int DonorSlotIndex)
{
	if(TargetSlotIndex < 0 || TargetSlotIndex >= (int)m_AssetsEditorState.m_vTargetSlots.size())
		return;
	if(DonorSlotIndex < 0 || DonorSlotIndex >= (int)m_AssetsEditorState.m_vDonorSlots.size())
		return;
	const SAssetsEditorPartSlot &DonorSlot = m_AssetsEditorState.m_vDonorSlots[DonorSlotIndex];
	SAssetsEditorPartSlot &TargetSlot = m_AssetsEditorState.m_vTargetSlots[TargetSlotIndex];
	TargetSlot.m_FromDonor = true;
	TargetSlot.m_SrcX = DonorSlot.m_DstX;
	TargetSlot.m_SrcY = DonorSlot.m_DstY;
	TargetSlot.m_SrcW = DonorSlot.m_DstW;
	TargetSlot.m_SrcH = DonorSlot.m_DstH;
	m_AssetsEditorState.m_DirtyPreview = true;
	m_AssetsEditorState.m_HasUnsavedChanges = true;
	str_copy(m_AssetsEditorState.m_aStatusMessage, Localize("Part updated."), sizeof(m_AssetsEditorState.m_aStatusMessage));
	m_AssetsEditorState.m_StatusIsError = false;
}

void CMenus::AssetsEditorClearSlotColor(int SlotIndex)
{
	if(SlotIndex < 0 || SlotIndex >= (int)m_AssetsEditorState.m_vTargetSlots.size())
		return;
	SAssetsEditorPartSlot &Slot = m_AssetsEditorState.m_vTargetSlots[SlotIndex];
	Slot.m_UseCustomColor = false;
	Slot.m_ColorBlendMode = ASSETS_EDITOR_COLOR_BLEND_TEELIKE;
	Slot.m_ColorOpacity = 100;
	m_AssetsEditorState.m_DirtyPreview = true;
	m_AssetsEditorState.m_HasUnsavedChanges = true;
}

void CMenus::AssetsEditorResetSlot(int SlotIndex)
{
	if(SlotIndex < 0 || SlotIndex >= (int)m_AssetsEditorState.m_vTargetSlots.size())
		return;
	SAssetsEditorPartSlot &Slot = m_AssetsEditorState.m_vTargetSlots[SlotIndex];
	Slot.m_FromDonor = false;
	Slot.m_SrcX = Slot.m_DstX;
	Slot.m_SrcY = Slot.m_DstY;
	Slot.m_SrcW = Slot.m_DstW;
	Slot.m_SrcH = Slot.m_DstH;
	Slot.m_UseCustomColor = false;
	Slot.m_ColorBlendMode = ASSETS_EDITOR_COLOR_BLEND_TEELIKE;
	Slot.m_ColorOpacity = 100;
	if(m_AssetsEditorState.m_ColorEditSlot == SlotIndex)
	{
		if(Ui()->IsPopupOpen(&gs_AssetsEditorColorPopup))
			Ui()->ClosePopupMenu(&gs_AssetsEditorColorPopup);
		m_AssetsEditorState.m_ColorEditSlot = -1;
	}
	m_AssetsEditorState.m_DirtyPreview = true;
	m_AssetsEditorState.m_HasUnsavedChanges = true;
	str_copy(m_AssetsEditorState.m_aStatusMessage, Localize("Part reset."), sizeof(m_AssetsEditorState.m_aStatusMessage));
	m_AssetsEditorState.m_StatusIsError = false;
}

void CMenus::AssetsEditorOpenColorPopup(int SlotIndex, float X, float Y)
{
	if(SlotIndex < 0 || SlotIndex >= (int)m_AssetsEditorState.m_vTargetSlots.size())
		return;
	SAssetsEditorPartSlot &Slot = m_AssetsEditorState.m_vTargetSlots[SlotIndex];
	if(!Slot.m_UseCustomColor && m_AssetsEditorState.m_HasLastColor)
	{
		Slot.m_CustomColor = m_AssetsEditorState.m_LastCustomColor;
		Slot.m_ColorBlendMode = m_AssetsEditorState.m_LastColorBlendMode;
		Slot.m_ColorOpacity = m_AssetsEditorState.m_LastColorOpacity;
		Slot.m_UseCustomColor = true;
		m_AssetsEditorState.m_DirtyPreview = true;
		m_AssetsEditorState.m_HasUnsavedChanges = true;
	}
	else if(Slot.m_UseCustomColor)
	{
		m_AssetsEditorState.m_HasLastColor = true;
		m_AssetsEditorState.m_LastCustomColor = Slot.m_CustomColor;
		m_AssetsEditorState.m_LastColorBlendMode = Slot.m_ColorBlendMode;
		m_AssetsEditorState.m_LastColorOpacity = Slot.m_ColorOpacity;
	}
	Slot.m_ColorOpacity = std::clamp(Slot.m_ColorOpacity, 0, 100);
	Slot.m_ColorBlendMode = std::clamp(Slot.m_ColorBlendMode, 0, ASSETS_EDITOR_COLOR_BLEND_COUNT - 1);
	gs_AssetsEditorColorPopup.m_pMenus = this;
	gs_AssetsEditorColorPopup.m_SlotIndex = SlotIndex;
	m_AssetsEditorState.m_ColorEditSlot = SlotIndex;
	if(Ui()->IsPopupOpen(&gs_AssetsEditorColorPopup))
		Ui()->ClosePopupMenu(&gs_AssetsEditorColorPopup);
	SPopupMenuProperties PopupProps;
	PopupProps.m_Draggable = true;
	Ui()->DoPopupMenu(&gs_AssetsEditorColorPopup, X, Y, 300.0f, 305.0f, &gs_AssetsEditorColorPopup, AssetsEditorPopupColorEditor, PopupProps);
	if(m_AssetsEditorState.m_HasLastColorPopupPos)
		Ui()->SetPopupPosition(&gs_AssetsEditorColorPopup, m_AssetsEditorState.m_LastColorPopupX, m_AssetsEditorState.m_LastColorPopupY);
}

CUi::EPopupMenuFunctionResult CMenus::AssetsEditorPopupColorEditor(void *pContext, CUIRect View, bool Active)
{
	SAssetsEditorColorPopupContext *pPopup = static_cast<SAssetsEditorColorPopupContext *>(pContext);
	CMenus *pMenus = pPopup->m_pMenus;
	if(pMenus == nullptr)
		return CUi::POPUP_CLOSE_CURRENT;
	if(pPopup->m_SlotIndex < 0 || pPopup->m_SlotIndex >= (int)pMenus->m_AssetsEditorState.m_vTargetSlots.size())
		return CUi::POPUP_CLOSE_CURRENT;
	float PopupX = 0.0f;
	float PopupY = 0.0f;
	if(pMenus->Ui()->GetPopupPosition(pPopup, PopupX, PopupY))
	{
		pMenus->m_AssetsEditorState.m_HasLastColorPopupPos = true;
		pMenus->m_AssetsEditorState.m_LastColorPopupX = PopupX;
		pMenus->m_AssetsEditorState.m_LastColorPopupY = PopupY;
	}
	if(Active && pMenus->Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
		return CUi::POPUP_CLOSE_CURRENT;

	SAssetsEditorPartSlot &Slot = pMenus->m_AssetsEditorState.m_vTargetSlots[pPopup->m_SlotIndex];
	const unsigned PrevColor = Slot.m_CustomColor;
	const int PrevBlend = Slot.m_ColorBlendMode;
	const int PrevOpacity = Slot.m_ColorOpacity;

	CUIRect Title, Colors, OpacityRow, Buttons;
	View.HSplitTop(LineSize, &Title, &View);
	pMenus->Ui()->DoLabel(&Title, Localize("Custom color"), FontSize, TEXTALIGN_ML);
	View.HSplitTop(MarginExtraSmall, nullptr, &View);
	View.HSplitTop(95.0f, &Colors, &View);
	pMenus->RenderHslaScrollbars(&Colors, &Slot.m_CustomColor, false, ColorHSLA::DARKEST_LGT);

	View.HSplitTop(MarginSmall, nullptr, &View);
	CUIRect BlendLabelRow, BlendRow1, BlendRow2;
	View.HSplitTop(LineSize * 0.9f, &BlendLabelRow, &View);
	pMenus->Ui()->DoLabel(&BlendLabelRow, Localize("Blend mode"), FontSize * 0.9f, TEXTALIGN_ML);
	View.HSplitTop(MarginExtraSmall, nullptr, &View);
	View.HSplitTop(LineSize, &BlendRow1, &View);
	View.HSplitTop(MarginExtraSmall, nullptr, &View);
	View.HSplitTop(LineSize, &BlendRow2, &View);

	auto DrawBlendButtonRow = [&](CUIRect Row, int ModeLeft, int ModeRight) {
		CUIRect LeftButton, RightButton;
		Row.VSplitMid(&LeftButton, &RightButton, 2.0f);
		const ColorRGBA ActiveColor(1.0f, 1.0f, 1.0f, 0.28f);
		const ColorRGBA IdleColor(1.0f, 1.0f, 1.0f, 0.16f);
		if(pMenus->DoButton_Menu(&pPopup->m_aBlendButtons[ModeLeft], AssetsEditorBlendName(ModeLeft), 0, &LeftButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 3.0f, 0.0f, Slot.m_ColorBlendMode == ModeLeft ? ActiveColor : IdleColor))
			Slot.m_ColorBlendMode = ModeLeft;
		if(pMenus->DoButton_Menu(&pPopup->m_aBlendButtons[ModeRight], AssetsEditorBlendName(ModeRight), 0, &RightButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 3.0f, 0.0f, Slot.m_ColorBlendMode == ModeRight ? ActiveColor : IdleColor))
			Slot.m_ColorBlendMode = ModeRight;
	};
	DrawBlendButtonRow(BlendRow1, ASSETS_EDITOR_COLOR_BLEND_TEELIKE, ASSETS_EDITOR_COLOR_BLEND_SCREEN);
	DrawBlendButtonRow(BlendRow2, ASSETS_EDITOR_COLOR_BLEND_MULTIPLY, ASSETS_EDITOR_COLOR_BLEND_OVERLAY);

	View.HSplitTop(MarginSmall, nullptr, &View);
	View.HSplitTop(LineSize, &OpacityRow, &View);
	Slot.m_ColorOpacity = std::clamp(Slot.m_ColorOpacity, 0, 100);
	pMenus->Ui()->DoScrollbarOption(&pPopup->m_OpacityScrollbarId, &Slot.m_ColorOpacity, &OpacityRow, Localize("Opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");

	View.HSplitTop(MarginSmall, nullptr, &View);
	View.HSplitBottom(LineSize, nullptr, &Buttons);
	CUIRect ClearButton, DoneButton;
	Buttons.VSplitMid(&ClearButton, &DoneButton, MarginSmall);
	if(pMenus->DoButton_Menu(&pPopup->m_ClearButton, Localize("Clear"), 0, &ClearButton))
	{
		pMenus->AssetsEditorClearSlotColor(pPopup->m_SlotIndex);
		pMenus->m_AssetsEditorState.m_ColorEditSlot = -1;
		return CUi::POPUP_CLOSE_CURRENT;
	}
	if(pMenus->DoButton_Menu(&pPopup->m_DoneButton, Localize("Done"), 0, &DoneButton) || (Active && pMenus->Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER)))
	{
		pMenus->m_AssetsEditorState.m_ColorEditSlot = -1;
		return CUi::POPUP_CLOSE_CURRENT;
	}
	if(PrevColor != Slot.m_CustomColor || PrevBlend != Slot.m_ColorBlendMode || PrevOpacity != Slot.m_ColorOpacity)
	{
		Slot.m_UseCustomColor = true;
		pMenus->m_AssetsEditorState.m_HasLastColor = true;
		pMenus->m_AssetsEditorState.m_LastCustomColor = Slot.m_CustomColor;
		pMenus->m_AssetsEditorState.m_LastColorBlendMode = Slot.m_ColorBlendMode;
		pMenus->m_AssetsEditorState.m_LastColorOpacity = Slot.m_ColorOpacity;
		pMenus->m_AssetsEditorState.m_DirtyPreview = true;
		pMenus->m_AssetsEditorState.m_HasUnsavedChanges = true;
		pMenus->AssetsEditorUpdatePreview();
	}
	return CUi::POPUP_KEEP_OPEN;
}

void CMenus::AssetsEditorRenderCanvas(const CUIRect &Rect, IGraphics::CTextureHandle Texture, int Width, int Height, int Category, const std::vector<SAssetsEditorPartSlot> &vSlots, int HighlightSlot, bool DropHighlight)
{
	if(!Texture.IsValid() || Width <= 0 || Height <= 0)
	{
		Ui()->DoLabel(&Rect, Localize("No preview"), FontSize, TEXTALIGN_MC);
		return;
	}
	CUIRect FittedRect;
	if(!AssetsEditorDrawTextureFitted(Rect, Texture, Width, Height, Graphics(), &FittedRect))
	{
		Ui()->DoLabel(&Rect, Localize("No preview"), FontSize, TEXTALIGN_MC);
		return;
	}
	const int GridX = AssetsEditorGridX(Category);
	const int GridY = AssetsEditorGridY(Category);
	Graphics()->TextureClear();
	Graphics()->LinesBegin();
	for(size_t SlotIndex = 0; SlotIndex < vSlots.size(); ++SlotIndex)
	{
		const SAssetsEditorPartSlot &Slot = vSlots[SlotIndex];
		CUIRect SlotRect;
		if(!AssetsEditorSlotRect(FittedRect, GridX, GridY, Slot.m_DstX, Slot.m_DstY, Slot.m_DstW, Slot.m_DstH, SlotRect))
			continue;
		if((int)SlotIndex == HighlightSlot)
		{
			if(DropHighlight)
				Graphics()->SetColor(0.35f, 1.0f, 0.35f, 0.95f);
			else
				Graphics()->SetColor(1.0f, 0.85f, 0.2f, 0.95f);
		}
		else
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.35f);
		IGraphics::CLineItem aLines[4] = {
			IGraphics::CLineItem(SlotRect.x, SlotRect.y, SlotRect.x + SlotRect.w, SlotRect.y),
			IGraphics::CLineItem(SlotRect.x + SlotRect.w, SlotRect.y, SlotRect.x + SlotRect.w, SlotRect.y + SlotRect.h),
			IGraphics::CLineItem(SlotRect.x + SlotRect.w, SlotRect.y + SlotRect.h, SlotRect.x, SlotRect.y + SlotRect.h),
			IGraphics::CLineItem(SlotRect.x, SlotRect.y + SlotRect.h, SlotRect.x, SlotRect.y),
		};
		Graphics()->LinesDraw(aLines, 4);
	}
	Graphics()->LinesEnd();
}

void CMenus::RenderAssetsEditorScreen(CUIRect MainView)
{
	if(!m_AssetsEditorState.m_Initialized)
	{
		AssetsEditorLoadImage(ASSETS_EDITOR_CAT_GAME, "default", m_AssetsEditorState.m_Left);
		AssetsEditorLoadImage(ASSETS_EDITOR_CAT_GAME, "default", m_AssetsEditorState.m_Right);
		AssetsEditorRebuildSlots(ASSETS_EDITOR_SIDE_LEFT);
		AssetsEditorRebuildSlots(ASSETS_EDITOR_SIDE_RIGHT);
		if(m_AssetsEditorState.m_aExportName[0] == '\0')
			str_copy(m_AssetsEditorState.m_aExportName, "my_asset", sizeof(m_AssetsEditorState.m_aExportName));
		m_AssetsEditorState.m_Initialized = true;
		m_AssetsEditorState.m_DirtyPreview = true;
	}

	if(!m_AssetsEditorState.m_ShowExitConfirm && !Ui()->IsPopupOpen() && Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
	{
		AssetsEditorRequestClose();
		if(!m_AssetsEditorState.m_Open)
			return;
	}
	if(m_AssetsEditorState.m_ShowExitConfirm)
	{
		AssetsEditorRenderExitConfirm(*Ui()->Screen());
		return;
	}

	MainView = *Ui()->Screen();
	MainView.HSplitTop(24.0f, nullptr, &MainView);

	CUIRect EditorRect = MainView;
	EditorRect.Margin(8.0f, &EditorRect);
	EditorRect.Draw(ms_ColorTabbarActive, IGraphics::CORNER_ALL, 10.0f);
	Ui()->ClipEnable(&EditorRect);
	SScopedClip ClipGuard{Ui()};

	CUIRect WorkRect;
	EditorRect.Margin(8.0f, &WorkRect);
	CUIRect TopBar, ContentView, StatusRect;
	WorkRect.HSplitTop(LineSize + 4.0f, &TopBar, &ContentView);
	ContentView.HSplitBottom(LineSize + MarginSmall, &ContentView, &StatusRect);

	CUIRect CloseButton, ExportRow, ResetButton, ExportButton, ReloadButton;
	TopBar.VSplitLeft(28.0f, &CloseButton, &TopBar);
	TopBar.VSplitLeft(MarginSmall, nullptr, &TopBar);
	TopBar.VSplitRight(28.0f, &TopBar, &ResetButton);
	TopBar.VSplitRight(MarginSmall, &TopBar, nullptr);
	TopBar.VSplitRight(28.0f, &TopBar, &ReloadButton);
	TopBar.VSplitRight(MarginSmall, &TopBar, nullptr);
	TopBar.VSplitRight(28.0f, &TopBar, &ExportButton);
	TopBar.VSplitRight(MarginSmall, &TopBar, nullptr);
	ExportRow = TopBar;

	static CButtonContainer s_CloseButton;
	if(Ui()->DoButton_FontIcon(&s_CloseButton, XMARK, 0, &CloseButton, BUTTONFLAG_LEFT))
	{
		AssetsEditorRequestClose();
		if(!m_AssetsEditorState.m_Open)
			return;
	}

	static CLineInput s_ExportNameInput;
	s_ExportNameInput.SetBuffer(m_AssetsEditorState.m_aExportName, sizeof(m_AssetsEditorState.m_aExportName));
	s_ExportNameInput.SetEmptyText("my_asset");
	Ui()->DoEditBox(&s_ExportNameInput, &ExportRow, EditBoxFontSize);

	static CButtonContainer s_ReloadButton;
	if(Ui()->DoButton_FontIcon(&s_ReloadButton, ARROWS_ROTATE, 0, &ReloadButton, BUTTONFLAG_LEFT))
	{
		AssetsEditorCancelDrag();
		const bool LeftOk = AssetsEditorLoadImage(m_AssetsEditorState.m_Left.m_Category, m_AssetsEditorState.m_Left.m_aName, m_AssetsEditorState.m_Left);
		const bool RightOk = AssetsEditorLoadImage(m_AssetsEditorState.m_Right.m_Category, m_AssetsEditorState.m_Right.m_aName, m_AssetsEditorState.m_Right);
		m_AssetsEditorState.m_DirtyPreview = true;
		if(LeftOk && RightOk)
		{
			str_copy(m_AssetsEditorState.m_aStatusMessage, Localize("Reloaded images."), sizeof(m_AssetsEditorState.m_aStatusMessage));
			m_AssetsEditorState.m_StatusIsError = false;
		}
		else
		{
			str_copy(m_AssetsEditorState.m_aStatusMessage, Localize("Failed to load asset."), sizeof(m_AssetsEditorState.m_aStatusMessage));
			m_AssetsEditorState.m_StatusIsError = true;
		}
	}

	static CButtonContainer s_ExportButton;
	if(Ui()->DoButton_FontIcon(&s_ExportButton, RIGHT_FROM_BRACKET, 0, &ExportButton, BUTTONFLAG_LEFT))
		AssetsEditorExport();
	GameClient()->m_Tooltips.DoToolTip(&s_ReloadButton, &ReloadButton, Localize("Reload"));
	GameClient()->m_Tooltips.DoToolTip(&s_ExportButton, &ExportButton, Localize("Export"));

	static CButtonContainer s_ResetAllButton;
	if(Ui()->DoButton_FontIcon(&s_ResetAllButton, ARROW_ROTATE_LEFT, 0, &ResetButton, BUTTONFLAG_LEFT))
	{
		for(SAssetsEditorPartSlot &Slot : m_AssetsEditorState.m_vTargetSlots)
		{
			Slot.m_FromDonor = false;
			Slot.m_SrcX = Slot.m_DstX;
			Slot.m_SrcY = Slot.m_DstY;
			Slot.m_SrcW = Slot.m_DstW;
			Slot.m_SrcH = Slot.m_DstH;
			Slot.m_UseCustomColor = false;
			Slot.m_ColorBlendMode = ASSETS_EDITOR_COLOR_BLEND_TEELIKE;
			Slot.m_ColorOpacity = 100;
		}
		if(Ui()->IsPopupOpen(&gs_AssetsEditorColorPopup))
			Ui()->ClosePopupMenu(&gs_AssetsEditorColorPopup);
		m_AssetsEditorState.m_ColorEditSlot = -1;
		AssetsEditorCancelDrag();
		m_AssetsEditorState.m_DirtyPreview = true;
		m_AssetsEditorState.m_HasUnsavedChanges = true;
	}
	GameClient()->m_Tooltips.DoToolTip(&s_ResetAllButton, &ResetButton, Localize("Reset All"));

	ContentView.HSplitTop(MarginSmall, nullptr, &ContentView);
	CUIRect LeftPanel, ArrowColumn, RightPanel;
	ContentView.VSplitMid(&LeftPanel, &RightPanel, 42.0f);
	ArrowColumn.x = LeftPanel.x + LeftPanel.w;
	ArrowColumn.y = LeftPanel.y;
	ArrowColumn.w = 42.0f;
	ArrowColumn.h = LeftPanel.h;

	LeftPanel.Margin(MarginSmall, &LeftPanel);
	RightPanel.Margin(MarginSmall, &RightPanel);

	const float ColumnChrome = LineSize + MarginExtraSmall + MarginSmall;
	CUIRect LeftCanvas = LeftPanel;
	CUIRect RightCanvas = RightPanel;
	LeftCanvas.HSplitTop(ColumnChrome, nullptr, &LeftCanvas);
	LeftCanvas.HSplitBottom(ColumnChrome, &LeftCanvas, nullptr);
	RightCanvas.HSplitTop(ColumnChrome, nullptr, &RightCanvas);
	RightCanvas.HSplitBottom(ColumnChrome, &RightCanvas, nullptr);

	AssetsEditorSetIconFont(TextRender());
	const float ChevronSize = 18.0f;
	const float ChevronStep = 26.0f;
	float ChevronY = ArrowColumn.y + (ArrowColumn.h - ChevronStep * 3.0f) / 2.0f;
	for(int Chevron = 0; Chevron < 3; ++Chevron)
	{
		CUIRect Icon;
		Icon.x = ArrowColumn.x;
		Icon.y = ChevronY + Chevron * ChevronStep;
		Icon.w = ArrowColumn.w;
		Icon.h = ChevronSize;
		Ui()->DoLabel(&Icon, CHEVRON_RIGHT, ChevronSize, TEXTALIGN_MC);
	}
	AssetsEditorClearIconFont(TextRender());

	AssetsEditorUpdatePreview();

	const vec2 MousePos = Ui()->MousePos();
	const bool ColorPopupOpen = Ui()->IsPopupOpen(&gs_AssetsEditorColorPopup);
	const bool PopupBlocksPick = Ui()->IsPopupOpen() && !ColorPopupOpen;
	const bool ClickedLmb = !PopupBlocksPick && Ui()->MouseButtonClicked(0);
	const bool PreviewReady = m_AssetsEditorState.m_PreviewTexture.IsValid() && m_AssetsEditorState.m_PreviewWidth > 0 && m_AssetsEditorState.m_PreviewHeight > 0;
	const int TargetWidth = PreviewReady ? m_AssetsEditorState.m_PreviewWidth : m_AssetsEditorState.m_Right.m_Width;
	const int TargetHeight = PreviewReady ? m_AssetsEditorState.m_PreviewHeight : m_AssetsEditorState.m_Right.m_Height;
	const IGraphics::CTextureHandle TargetTexture = PreviewReady ? m_AssetsEditorState.m_PreviewTexture : m_AssetsEditorState.m_Right.m_Texture;
	CUIRect DonorFitted;
	CUIRect TargetFitted;
	const bool HasDonorFitted = AssetsEditorCalcFittedRect(LeftCanvas, m_AssetsEditorState.m_Left.m_Width, m_AssetsEditorState.m_Left.m_Height, DonorFitted);
	const bool HasTargetFitted = AssetsEditorCalcFittedRect(RightCanvas, TargetWidth, TargetHeight, TargetFitted);

	auto PlaceBesideBlock = [&](const CUIRect &Panel, const CUIRect &Fitted, bool HasFitted, CUIRect &Title, CUIRect &Explore) {
		if(!HasFitted)
		{
			Panel.HSplitTop(LineSize, &Title, nullptr);
			Panel.HSplitBottom(LineSize, nullptr, &Explore);
			return;
		}
		const float BlockTop = Fitted.y - MarginSmall;
		const float BlockBottom = Fitted.y + Fitted.h + MarginSmall;
		Title.x = Panel.x;
		Title.y = BlockTop - MarginExtraSmall - LineSize;
		Title.w = Panel.w;
		Title.h = LineSize;
		Explore.x = Panel.x;
		Explore.y = BlockBottom + MarginExtraSmall;
		Explore.w = Panel.w;
		Explore.h = LineSize;
	};
	CUIRect LeftTitle, LeftExplore, RightTitle, RightExplore;
	PlaceBesideBlock(LeftPanel, DonorFitted, HasDonorFitted, LeftTitle, LeftExplore);
	PlaceBesideBlock(RightPanel, TargetFitted, HasTargetFitted, RightTitle, RightExplore);

	auto DrawColumnBlock = [&](const CUIRect &Panel, const CUIRect &Title, const CUIRect &Explore, bool HasFitted) {
		if(!HasFitted)
			return;
		CUIRect Block;
		constexpr float BlockSidePad = 10.0f;
		Block.x = Panel.x - BlockSidePad;
		Block.w = Panel.w + BlockSidePad * 2.0f;
		Block.y = Title.y - MarginSmall;
		Block.h = Explore.y + Explore.h + MarginSmall - Block.y;
		Block.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.35f), IGraphics::CORNER_ALL, 6.0f);
	};
	DrawColumnBlock(LeftPanel, LeftTitle, LeftExplore, HasDonorFitted);
	DrawColumnBlock(RightPanel, RightTitle, RightExplore, HasTargetFitted);

	char aLeftTitle[128];
	str_format(aLeftTitle, sizeof(aLeftTitle), "%s: %s", Localize("Donor"), m_AssetsEditorState.m_Left.m_aName[0] != '\0' ? m_AssetsEditorState.m_Left.m_aName : "default");
	Ui()->DoLabel(&LeftTitle, aLeftTitle, FontSize, TEXTALIGN_ML);
	char aRightTitle[128];
	str_format(aRightTitle, sizeof(aRightTitle), "%s: %s", Localize("Result"), m_AssetsEditorState.m_Right.m_aName[0] != '\0' ? m_AssetsEditorState.m_Right.m_aName : "default");
	Ui()->DoLabel(&RightTitle, aRightTitle, FontSize, TEXTALIGN_ML);

	static CButtonContainer s_LeftExploreButton;
	static CButtonContainer s_RightExploreButton;
	if(DoButton_Menu(&s_LeftExploreButton, Localize("Explore"), 0, &LeftExplore))
	{
		m_AssetsEditorState.m_ExploreSide = ASSETS_EDITOR_SIDE_LEFT;
		AssetsEditorCancelDrag();
	}
	if(DoButton_Menu(&s_RightExploreButton, Localize("Explore"), 0, &RightExplore))
	{
		m_AssetsEditorState.m_ExploreSide = ASSETS_EDITOR_SIDE_RIGHT;
		AssetsEditorCancelDrag();
	}

	int HoveredDonor = -1;
	int HoveredTarget = -1;
	if(HasDonorFitted && !m_AssetsEditorState.m_DragActive)
		HoveredDonor = AssetsEditorResolveHovered(DonorFitted, m_AssetsEditorState.m_Left.m_Category, m_AssetsEditorState.m_vDonorSlots, MousePos, ClickedLmb, m_AssetsEditorState.m_DonorHover, nullptr);
	const SAssetsEditorPartSlot *pPreferred = nullptr;
	if(m_AssetsEditorState.m_DragActive && m_AssetsEditorState.m_DraggedDonorSlot >= 0 && m_AssetsEditorState.m_DraggedDonorSlot < (int)m_AssetsEditorState.m_vDonorSlots.size())
		pPreferred = &m_AssetsEditorState.m_vDonorSlots[m_AssetsEditorState.m_DraggedDonorSlot];
	if(HasTargetFitted)
		HoveredTarget = AssetsEditorResolveHovered(TargetFitted, m_AssetsEditorState.m_Right.m_Category, m_AssetsEditorState.m_vTargetSlots, MousePos, ClickedLmb && pPreferred == nullptr, m_AssetsEditorState.m_TargetHover, pPreferred);

	const bool ClickedRmb = !PopupBlocksPick && !Ui()->IsPopupHovered() && Ui()->MouseButtonClicked(1);
	if(ClickedRmb && HoveredTarget >= 0 && !m_AssetsEditorState.m_DragActive)
		AssetsEditorResetSlot(HoveredTarget);

	const bool SingleDonorCandidate = m_AssetsEditorState.m_DonorHover.m_vCandidates.size() <= 1;
	const bool StartDragNow = !PopupBlocksPick && !ColorPopupOpen && Ui()->MouseButton(0) && (!ClickedLmb || SingleDonorCandidate);
	if(!m_AssetsEditorState.m_DragActive && StartDragNow && HoveredDonor >= 0)
	{
		m_AssetsEditorState.m_DragActive = true;
		m_AssetsEditorState.m_DraggedDonorSlot = HoveredDonor;
	}

	const bool ReleasedLmb = !PopupBlocksPick && !Ui()->MouseButton(0) && Ui()->LastMouseButton(0);
	if(m_AssetsEditorState.m_DragActive && ReleasedLmb)
	{
		if(HoveredTarget >= 0)
			AssetsEditorApplyDrop(HoveredTarget, m_AssetsEditorState.m_DraggedDonorSlot);
		AssetsEditorCancelDrag();
	}
	else if(ReleasedLmb && HoveredTarget >= 0 && !Ui()->IsPopupHovered())
		AssetsEditorOpenColorPopup(HoveredTarget, Ui()->MouseX(), Ui()->MouseY());

	const int DonorHighlight = m_AssetsEditorState.m_DragActive ? m_AssetsEditorState.m_DraggedDonorSlot : HoveredDonor;
	AssetsEditorRenderCanvas(LeftCanvas, m_AssetsEditorState.m_Left.m_Texture, m_AssetsEditorState.m_Left.m_Width, m_AssetsEditorState.m_Left.m_Height, m_AssetsEditorState.m_Left.m_Category, m_AssetsEditorState.m_vDonorSlots, DonorHighlight, false);
	AssetsEditorRenderCanvas(RightCanvas, TargetTexture, TargetWidth, TargetHeight, m_AssetsEditorState.m_Right.m_Category, m_AssetsEditorState.m_vTargetSlots, HoveredTarget, m_AssetsEditorState.m_DragActive && HoveredTarget >= 0);

	if(m_AssetsEditorState.m_DragActive && m_AssetsEditorState.m_DraggedDonorSlot >= 0 && m_AssetsEditorState.m_DraggedDonorSlot < (int)m_AssetsEditorState.m_vDonorSlots.size())
	{
		const SAssetsEditorPartSlot &DraggedSlot = m_AssetsEditorState.m_vDonorSlots[m_AssetsEditorState.m_DraggedDonorSlot];
		if(HasDonorFitted)
		{
			CUIRect SourceSlotRect;
			if(AssetsEditorSlotRect(DonorFitted, AssetsEditorGridX(m_AssetsEditorState.m_Left.m_Category), AssetsEditorGridY(m_AssetsEditorState.m_Left.m_Category), DraggedSlot.m_DstX, DraggedSlot.m_DstY, DraggedSlot.m_DstW, DraggedSlot.m_DstH, SourceSlotRect))
			{
				Graphics()->TextureClear();
				Graphics()->QuadsBegin();
				Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.28f);
				IGraphics::CQuadItem Quad(SourceSlotRect.x, SourceSlotRect.y, SourceSlotRect.w, SourceSlotRect.h);
				Graphics()->QuadsDrawTL(&Quad, 1);
				Graphics()->QuadsEnd();
			}
		}

		const int SpriteId = DraggedSlot.m_SpriteId;
		const char *pSpriteName = SpriteId >= 0 ? g_pData->m_aSprites[SpriteId].m_pName : (DraggedSlot.m_aFamilyKey[0] != '\0' ? DraggedSlot.m_aFamilyKey : "tile");
		CUIRect DragFrame;
		DragFrame.x = std::clamp(Ui()->MouseX() + 8.0f, EditorRect.x, EditorRect.x + EditorRect.w - 62.0f);
		DragFrame.y = std::clamp(Ui()->MouseY() + 8.0f, EditorRect.y, EditorRect.y + EditorRect.h - 62.0f);
		DragFrame.w = 62.0f;
		DragFrame.h = 62.0f;
		CUIRect DragSprite = DragFrame;
		DragSprite.Margin(4.0f, &DragSprite);
		DragFrame.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.55f), IGraphics::CORNER_ALL, 4.0f);
		AssetsEditorDrawSlot(DragSprite, m_AssetsEditorState.m_Left.m_Texture, AssetsEditorGridX(m_AssetsEditorState.m_Left.m_Category), AssetsEditorGridY(m_AssetsEditorState.m_Left.m_Category), DraggedSlot.m_DstX, DraggedSlot.m_DstY, DraggedSlot.m_DstW, DraggedSlot.m_DstH, 0.95f, Graphics());

		CUIRect DragHint;
		DragHint.w = 210.0f;
		DragHint.h = LineSize;
		DragHint.x = std::clamp(DragFrame.x + DragFrame.w + 6.0f, EditorRect.x, EditorRect.x + EditorRect.w - DragHint.w);
		DragHint.y = std::clamp(DragFrame.y + 21.0f, EditorRect.y, EditorRect.y + EditorRect.h - DragHint.h);
		DragHint.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.65f), IGraphics::CORNER_ALL, 4.0f);
		char aDragText[128];
		str_format(aDragText, sizeof(aDragText), Localize("%s from %s"), pSpriteName, m_AssetsEditorState.m_Left.m_aName);
		Ui()->DoLabel(&DragHint, aDragText, FontSize * 0.9f, TEXTALIGN_MC);
	}

	CUIRect StatusLeft, StatusRight;
	StatusRect.VSplitMid(&StatusLeft, &StatusRight);
	if(m_AssetsEditorState.m_aStatusMessage[0] != '\0')
	{
		TextRender()->TextColor(m_AssetsEditorState.m_StatusIsError ? ColorRGBA(1.0f, 0.45f, 0.45f, 1.0f) : ColorRGBA(0.55f, 1.0f, 0.55f, 1.0f));
		Ui()->DoLabel(&StatusLeft, m_AssetsEditorState.m_aStatusMessage, FontSize * 0.95f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
	const char *pHint = m_AssetsEditorState.m_DragActive ? Localize("Drop on the right to replace one part.") : Localize("Drag from left to right. Left-click colors, right-click resets.");
	Ui()->DoLabel(&StatusRight, pHint, FontSize * 0.95f, TEXTALIGN_MR);

	if(HoveredTarget >= 0 && !m_AssetsEditorState.m_DragActive && !Ui()->IsPopupOpen())
		AssetsEditorDrawBrushCursor(Ui(), TextRender(), Ui()->MouseX(), Ui()->MouseY());
}
