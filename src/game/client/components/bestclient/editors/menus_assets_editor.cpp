#include <base/math.h>
#include <base/system.h>
#include <base/types.h>

#include <engine/graphics.h>
#include <engine/font_icons.h>
#include <engine/gfx/image_loader.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/shared/config.h>
#include <engine/shared/localization.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

#include <game/client/components/menus.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>

#include <algorithm>
#include <cctype>
#include <vector>

using namespace FontIcon;

namespace
{
constexpr float FontSize = 14.0f;
constexpr float EditBoxFontSize = 12.0f;
constexpr float LineSize = 20.0f;
constexpr float HeadlineFontSize = 20.0f;
constexpr float MarginSmall = 5.0f;
constexpr float MarginExtraSmall = 2.5f;

struct SScopedClip
{
	CUi *m_pUi;
	~SScopedClip() { m_pUi->ClipDisable(); }
};

struct SAssetsEditorPartDef
{
	int m_SpriteId;
	int m_Group;
};

struct SAssetsEditorScanContext
{
	std::vector<CMenus::SAssetsEditorAssetEntry> *m_pAssets;
	IGraphics *m_pGraphics;
	IStorage *m_pStorage;
	const char *m_pAssetType;
	int m_Type;
};

struct SAssetsEditorImageCacheEntry
{
	int m_Type = CMenus::ASSETS_EDITOR_TYPE_GAME;
	char m_aName[64] = {0};
	CImageInfo m_Image;
};

static std::vector<SAssetsEditorImageCacheEntry> gs_vAssetsEditorImageCache;

static int AssetsEditorFindSpriteIdByName(const char *pName, int ImageId)
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

static bool AssetsEditorHasAssetName(const std::vector<CMenus::SAssetsEditorAssetEntry> &vAssets, const char *pName)
{
	for(const auto &Asset : vAssets)
	{
		if(str_comp(Asset.m_aName, pName) == 0)
			return true;
	}
	return false;
}

static bool AssetsEditorSlotSameNormalizedSize(const CMenus::SAssetsEditorPartSlot &Left, const CMenus::SAssetsEditorPartSlot &Right)
{
	return Left.m_DstW == Right.m_DstW && Left.m_DstH == Right.m_DstH;
}

static void AssetsEditorStripTrailingDigits(const char *pIn, char *pOut, int OutSize)
{
	str_copy(pOut, pIn, OutSize);
	int Len = str_length(pOut);
	while(Len > 0 && isdigit((unsigned char)pOut[Len - 1]))
		pOut[--Len] = '\0';
}

static int AssetsEditorScanCallback(const char *pName, int IsDir, int DirType, void *pUser)
{
	(void)DirType;
	SAssetsEditorScanContext *pContext = static_cast<SAssetsEditorScanContext *>(pUser);
	if(pName[0] == '.')
		return 0;

	CMenus::SAssetsEditorAssetEntry Entry;
	Entry.m_IsDefault = false;

	if(IsDir)
	{
		if(str_comp(pName, "default") == 0)
			return 0;

		str_copy(Entry.m_aName, pName);
		if(pContext->m_Type == CMenus::ASSETS_EDITOR_TYPE_ENTITIES)
		{
			str_format(Entry.m_aPath, sizeof(Entry.m_aPath), "assets/entities/%s/ddnet.png", pName);
			if(!pContext->m_pStorage->FileExists(Entry.m_aPath, IStorage::TYPE_ALL))
			{
				str_format(Entry.m_aPath, sizeof(Entry.m_aPath), "assets/entities/%s.png", pName);
				if(!pContext->m_pStorage->FileExists(Entry.m_aPath, IStorage::TYPE_ALL))
					return 0;
			}
		}
		else
		{
			str_format(Entry.m_aPath, sizeof(Entry.m_aPath), "assets/%s/%s/%s.png", pContext->m_pAssetType, pName, pContext->m_pAssetType);
			if(!pContext->m_pStorage->FileExists(Entry.m_aPath, IStorage::TYPE_ALL))
			{
				str_format(Entry.m_aPath, sizeof(Entry.m_aPath), "assets/%s/%s.png", pContext->m_pAssetType, pName);
				if(!pContext->m_pStorage->FileExists(Entry.m_aPath, IStorage::TYPE_ALL))
					return 0;
			}
		}
	}
	else
	{
		if(!str_endswith(pName, ".png"))
			return 0;

		char aName[IO_MAX_PATH_LENGTH];
		str_truncate(aName, sizeof(aName), pName, str_length(pName) - 4);
		if(str_comp(aName, "default") == 0)
			return 0;
		str_copy(Entry.m_aName, aName);
		if(pContext->m_Type == CMenus::ASSETS_EDITOR_TYPE_ENTITIES)
			str_format(Entry.m_aPath, sizeof(Entry.m_aPath), "assets/entities/%s.png", aName);
		else
			str_format(Entry.m_aPath, sizeof(Entry.m_aPath), "assets/%s/%s.png", pContext->m_pAssetType, aName);
	}

	if(AssetsEditorHasAssetName(*pContext->m_pAssets, Entry.m_aName))
		return 0;

	Entry.m_PreviewTexture = pContext->m_pGraphics->LoadTexture(Entry.m_aPath, IStorage::TYPE_ALL);
	CImageInfo PreviewInfo;
	if(pContext->m_pGraphics->LoadPng(PreviewInfo, Entry.m_aPath, IStorage::TYPE_ALL))
	{
		Entry.m_PreviewWidth = PreviewInfo.m_Width;
		Entry.m_PreviewHeight = PreviewInfo.m_Height;
		PreviewInfo.Free();
	}
	pContext->m_pAssets->push_back(Entry);
	return 0;
}

static const char *AssetsEditorTypeName(int Type)
{
	switch(Type)
	{
	case CMenus::ASSETS_EDITOR_TYPE_EMOTICONS: return "emoticons";
	case CMenus::ASSETS_EDITOR_TYPE_ENTITIES: return "entities";
	case CMenus::ASSETS_EDITOR_TYPE_HUD: return "hud";
	case CMenus::ASSETS_EDITOR_TYPE_PARTICLES: return "particles";
	case CMenus::ASSETS_EDITOR_TYPE_EXTRAS: return "extras";
	default: return "game";
	}
}

static const char *AssetsEditorTypeDisplayName(int Type)
{
	switch(Type)
	{
	case CMenus::ASSETS_EDITOR_TYPE_EMOTICONS: return "Emoticons";
	case CMenus::ASSETS_EDITOR_TYPE_ENTITIES: return "Entities";
	case CMenus::ASSETS_EDITOR_TYPE_HUD: return "HUD";
	case CMenus::ASSETS_EDITOR_TYPE_PARTICLES: return "Particles";
	case CMenus::ASSETS_EDITOR_TYPE_EXTRAS: return "Extras";
	default: return "Game";
	}
}

static int AssetsEditorTypeImageId(int Type)
{
	switch(Type)
	{
	case CMenus::ASSETS_EDITOR_TYPE_EMOTICONS: return IMAGE_EMOTICONS;
	case CMenus::ASSETS_EDITOR_TYPE_HUD: return IMAGE_HUD;
	case CMenus::ASSETS_EDITOR_TYPE_PARTICLES: return IMAGE_PARTICLES;
	case CMenus::ASSETS_EDITOR_TYPE_EXTRAS: return IMAGE_EXTRAS;
	case CMenus::ASSETS_EDITOR_TYPE_ENTITIES: return -1;
	default: return IMAGE_GAME;
	}
}

static int AssetsEditorGridSpriteId(int Type)
{
	switch(Type)
	{
	case CMenus::ASSETS_EDITOR_TYPE_EMOTICONS: return SPRITE_OOP;
	case CMenus::ASSETS_EDITOR_TYPE_HUD: return SPRITE_HUD_AIRJUMP;
	case CMenus::ASSETS_EDITOR_TYPE_PARTICLES: return SPRITE_PART_SLICE;
	case CMenus::ASSETS_EDITOR_TYPE_EXTRAS: return SPRITE_PART_SNOWFLAKE;
	default: return SPRITE_HEALTH_FULL;
	}
}

static int AssetsEditorGridX(int Type)
{
	if(Type == CMenus::ASSETS_EDITOR_TYPE_ENTITIES)
		return 16;

	const int SpriteId = AssetsEditorGridSpriteId(Type);
	const CDataSprite &Sprite = g_pData->m_aSprites[SpriteId];
	if(Sprite.m_pSet == nullptr || Sprite.m_pSet->m_Gridx <= 0)
		return 1;
	return Sprite.m_pSet->m_Gridx;
}

static int AssetsEditorGridY(int Type)
{
	if(Type == CMenus::ASSETS_EDITOR_TYPE_ENTITIES)
		return 16;

	const int SpriteId = AssetsEditorGridSpriteId(Type);
	const CDataSprite &Sprite = g_pData->m_aSprites[SpriteId];
	if(Sprite.m_pSet == nullptr || Sprite.m_pSet->m_Gridy <= 0)
		return 1;
	return Sprite.m_pSet->m_Gridy;
}

static void AssetsEditorCollectPartDefs(int Type, std::vector<SAssetsEditorPartDef> &vPartDefs)
{
	vPartDefs.clear();
	if(Type == CMenus::ASSETS_EDITOR_TYPE_ENTITIES)
		return;

	const int ImageId = AssetsEditorTypeImageId(Type);
	if(ImageId < 0)
		return;

	const CDataImage *pImage = &g_pData->m_aImages[ImageId];
	const bool DeduplicateByGeometry = Type == CMenus::ASSETS_EDITOR_TYPE_GAME || Type == CMenus::ASSETS_EDITOR_TYPE_PARTICLES;
	auto HasMatchingGeometry = [&vPartDefs](const CDataSprite &Candidate) {
		for(const auto &PartDef : vPartDefs)
		{
			const CDataSprite &Existing = g_pData->m_aSprites[PartDef.m_SpriteId];
			if(Existing.m_X == Candidate.m_X && Existing.m_Y == Candidate.m_Y &&
				Existing.m_W == Candidate.m_W && Existing.m_H == Candidate.m_H)
				return true;
		}
		return false;
	};
	for(int SpriteId = 0; SpriteId < NUM_SPRITES; ++SpriteId)
	{
		const CDataSprite &Sprite = g_pData->m_aSprites[SpriteId];
		if(Sprite.m_pSet == nullptr || Sprite.m_pSet->m_pImage != pImage || Sprite.m_W <= 0 || Sprite.m_H <= 0)
			continue;
		if(DeduplicateByGeometry && HasMatchingGeometry(Sprite))
			continue;
		vPartDefs.push_back({SpriteId, 0});
	}
}

static const CMenus::SAssetsEditorAssetEntry *AssetsEditorFindAssetByName(const std::vector<CMenus::SAssetsEditorAssetEntry> &vAssets, const char *pName)
{
	for(const auto &Asset : vAssets)
	{
		if(str_comp(Asset.m_aName, pName) == 0)
			return &Asset;
	}
	return nullptr;
}

static int AssetsEditorFindAssetIndexByName(const std::vector<CMenus::SAssetsEditorAssetEntry> &vAssets, const char *pName)
{
	for(size_t i = 0; i < vAssets.size(); ++i)
	{
		if(str_comp(vAssets[i].m_aName, pName) == 0)
			return (int)i;
	}
	return -1;
}

static void AssetsEditorClearImageCache()
{
	for(auto &Entry : gs_vAssetsEditorImageCache)
	{
		Entry.m_Image.Free();
	}
	gs_vAssetsEditorImageCache.clear();
}

static bool AssetsEditorCalcFittedRect(const CUIRect &Rect, int SourceWidth, int SourceHeight, CUIRect &OutRect)
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

static bool AssetsEditorGetSlotRectInFitted(const CUIRect &FittedRect, int Type, const CMenus::SAssetsEditorPartSlot &Slot, CUIRect &OutRect)
{
	const int GridX = maximum(1, AssetsEditorGridX(Type));
	const int GridY = maximum(1, AssetsEditorGridY(Type));
	if(Slot.m_DstW <= 0 || Slot.m_DstH <= 0)
		return false;

	const float X = (float)Slot.m_DstX / GridX;
	const float Y = (float)Slot.m_DstY / GridY;
	const float W = (float)Slot.m_DstW / GridX;
	const float H = (float)Slot.m_DstH / GridY;

	OutRect.x = FittedRect.x + X * FittedRect.w;
	OutRect.y = FittedRect.y + Y * FittedRect.h;
	OutRect.w = W * FittedRect.w;
	OutRect.h = H * FittedRect.h;
	return true;
}

static bool AssetsEditorDrawTextureFitted(const CUIRect &Rect, IGraphics::CTextureHandle Texture, int SourceWidth, int SourceHeight, IGraphics *pGraphics, CUIRect *pOutFittedRect = nullptr)
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

static void AssetsEditorDrawSlotFromTexture(const CUIRect &Rect, IGraphics::CTextureHandle Texture, const CMenus::SAssetsEditorPartSlot &Slot, int Type, float Alpha, IGraphics *pGraphics)
{
	if(!Texture.IsValid() || Slot.m_SrcW <= 0 || Slot.m_SrcH <= 0)
		return;

	const int GridX = maximum(1, AssetsEditorGridX(Type));
	const int GridY = maximum(1, AssetsEditorGridY(Type));
	const float U0 = (float)Slot.m_SrcX / GridX;
	const float V0 = (float)Slot.m_SrcY / GridY;
	const float U1 = (float)(Slot.m_SrcX + Slot.m_SrcW) / GridX;
	const float V1 = (float)(Slot.m_SrcY + Slot.m_SrcH) / GridY;

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

static const CImageInfo *AssetsEditorGetCachedImage(int Type, const char *pAssetName, const std::vector<CMenus::SAssetsEditorAssetEntry> &vAssets, IGraphics *pGraphics)
{
	for(const auto &Entry : gs_vAssetsEditorImageCache)
	{
		if(Entry.m_Type == Type && str_comp(Entry.m_aName, pAssetName) == 0)
			return &Entry.m_Image;
	}

	const CMenus::SAssetsEditorAssetEntry *pAsset = AssetsEditorFindAssetByName(vAssets, pAssetName);
	if(pAsset == nullptr)
		return nullptr;

	CImageInfo Loaded;
	if(!pGraphics->LoadPng(Loaded, pAsset->m_aPath, IStorage::TYPE_ALL))
		return nullptr;

	if(!pGraphics->CheckImageDivisibility(pAsset->m_aPath, Loaded, AssetsEditorGridX(Type), AssetsEditorGridY(Type), true))
	{
		Loaded.Free();
		return nullptr;
	}

	ConvertToRgba(Loaded);

	SAssetsEditorImageCacheEntry &NewEntry = gs_vAssetsEditorImageCache.emplace_back();
	NewEntry.m_Type = Type;
	str_copy(NewEntry.m_aName, pAssetName);
	NewEntry.m_Image = std::move(Loaded);
	return &NewEntry.m_Image;
}
}

void CMenus::AssetsEditorClearAssets()
{
	auto UnloadAssets = [this](std::vector<SAssetsEditorAssetEntry> &vAssets) {
		for(auto &Asset : vAssets)
		{
			Graphics()->UnloadTexture(&Asset.m_PreviewTexture);
		}
		vAssets.clear();
	};

	for(int Type = 0; Type < ASSETS_EDITOR_TYPE_COUNT; ++Type)
		UnloadAssets(m_AssetsEditorState.m_avAssets[Type]);
	Graphics()->UnloadTexture(&m_AssetsEditorState.m_ComposedPreviewTexture);
	m_AssetsEditorState.m_vPartSlots.clear();
	AssetsEditorCancelDrag();
	m_AssetsEditorState.m_DirtyPreview = true;
	m_AssetsEditorState.m_LastComposeFailed = false;
	m_AssetsEditorState.m_VisualsEditorInitialized = false;
	m_AssetsEditorState.m_HasUnsavedChanges = false;
	m_AssetsEditorState.m_PendingCloseRequest = false;
	m_AssetsEditorState.m_ShowExitConfirm = false;
	m_AssetsEditorState.m_ComposedPreviewWidth = 0;
	m_AssetsEditorState.m_ComposedPreviewHeight = 0;
	m_AssetsEditorState.m_HoverCycleSlotIndex = -1;
	m_AssetsEditorState.m_HoverCyclePositionX = -1;
	m_AssetsEditorState.m_HoverCyclePositionY = -1;
	m_AssetsEditorState.m_HoverCycleCandidateCursor = 0;
	m_AssetsEditorState.m_vHoverCycleCandidates.clear();
	AssetsEditorClearImageCache();
}

void CMenus::AssetsEditorReloadAssets()
{
	AssetsEditorClearImageCache();

	auto ReloadType = [this](std::vector<SAssetsEditorAssetEntry> &vAssets, int Type) {
		for(auto &Asset : vAssets)
			Graphics()->UnloadTexture(&Asset.m_PreviewTexture);
		vAssets.clear();

		const char *pAssetType = AssetsEditorTypeName(Type);
		SAssetsEditorAssetEntry DefaultAsset;
		DefaultAsset.m_IsDefault = true;
		str_copy(DefaultAsset.m_aName, "default");
		if(Type == ASSETS_EDITOR_TYPE_ENTITIES)
			str_copy(DefaultAsset.m_aPath, "editor/entities_clear/ddnet.png");
		else
		{
			const int DefaultImageId = AssetsEditorTypeImageId(Type);
			str_copy(DefaultAsset.m_aPath, g_pData->m_aImages[DefaultImageId].m_pFilename);
		}
		DefaultAsset.m_PreviewTexture = Graphics()->LoadTexture(DefaultAsset.m_aPath, IStorage::TYPE_ALL);
		CImageInfo PreviewInfo;
		if(Graphics()->LoadPng(PreviewInfo, DefaultAsset.m_aPath, IStorage::TYPE_ALL))
		{
			DefaultAsset.m_PreviewWidth = PreviewInfo.m_Width;
			DefaultAsset.m_PreviewHeight = PreviewInfo.m_Height;
			PreviewInfo.Free();
		}
		vAssets.push_back(DefaultAsset);

		SAssetsEditorScanContext Context;
		Context.m_pAssets = &vAssets;
		Context.m_pGraphics = Graphics();
		Context.m_pStorage = Storage();
		Context.m_pAssetType = pAssetType;
		Context.m_Type = Type;

		char aPath[128];
		if(Type == ASSETS_EDITOR_TYPE_ENTITIES)
			str_copy(aPath, "assets/entities");
		else
			str_format(aPath, sizeof(aPath), "assets/%s", pAssetType);
		Storage()->ListDirectory(IStorage::TYPE_ALL, aPath, AssetsEditorScanCallback, &Context);

		std::sort(vAssets.begin(), vAssets.end(), [](const SAssetsEditorAssetEntry &Left, const SAssetsEditorAssetEntry &Right) {
			if(Left.m_IsDefault != Right.m_IsDefault)
				return Left.m_IsDefault;
			return str_comp(Left.m_aName, Right.m_aName) < 0;
		});
	};

	char aaPrevMainName[ASSETS_EDITOR_TYPE_COUNT][64] = {};
	char aaPrevDonorName[ASSETS_EDITOR_TYPE_COUNT][64] = {};
	for(int Type = 0; Type < ASSETS_EDITOR_TYPE_COUNT; ++Type)
	{
		const auto &vAssets = m_AssetsEditorState.m_avAssets[Type];
		const int MainIndex = m_AssetsEditorState.m_aMainAssetIndex[Type];
		const int DonorIndex = m_AssetsEditorState.m_aDonorAssetIndex[Type];
		if(MainIndex >= 0 && MainIndex < (int)vAssets.size())
			str_copy(aaPrevMainName[Type], vAssets[MainIndex].m_aName);
		if(DonorIndex >= 0 && DonorIndex < (int)vAssets.size())
			str_copy(aaPrevDonorName[Type], vAssets[DonorIndex].m_aName);
	}

	for(int Type = 0; Type < ASSETS_EDITOR_TYPE_COUNT; ++Type)
		ReloadType(m_AssetsEditorState.m_avAssets[Type], Type);

	for(int Type = 0; Type < ASSETS_EDITOR_TYPE_COUNT; ++Type)
	{
		auto &vAssets = m_AssetsEditorState.m_avAssets[Type];
		const int NewMainIndex = AssetsEditorFindAssetIndexByName(vAssets, aaPrevMainName[Type]);
		const int NewDonorIndex = AssetsEditorFindAssetIndexByName(vAssets, aaPrevDonorName[Type]);
		m_AssetsEditorState.m_aMainAssetIndex[Type] = NewMainIndex >= 0 ? NewMainIndex : 0;
		m_AssetsEditorState.m_aDonorAssetIndex[Type] = NewDonorIndex >= 0 ? NewDonorIndex : m_AssetsEditorState.m_aMainAssetIndex[Type];
	}

	AssetsEditorCancelDrag();
	m_AssetsEditorState.m_DirtyPreview = true;
}

void CMenus::AssetsEditorReloadAssetsImagesOnly()
{
	const int Type = m_AssetsEditorState.m_Type;
	AssetsEditorReloadAssets();

	auto &vSlots = m_AssetsEditorState.m_vPartSlots;
	const auto &vReloadedAssets = m_AssetsEditorState.m_avAssets[Type];
	const int MainIndex = m_AssetsEditorState.m_aMainAssetIndex[Type];
	const char *pMainAssetName = MainIndex >= 0 && MainIndex < (int)vReloadedAssets.size() ? vReloadedAssets[MainIndex].m_aName : "default";
	int FixedSlots = 0;
	for(auto &Slot : vSlots)
	{
		if(AssetsEditorFindAssetIndexByName(vReloadedAssets, Slot.m_aSourceAsset) >= 0)
			continue;
		str_copy(Slot.m_aSourceAsset, pMainAssetName);
		Slot.m_SourceSpriteId = Slot.m_SpriteId;
		Slot.m_SrcX = Slot.m_DstX;
		Slot.m_SrcY = Slot.m_DstY;
		Slot.m_SrcW = Slot.m_DstW;
		Slot.m_SrcH = Slot.m_DstH;
		++FixedSlots;
	}
	if(FixedSlots > 0)
	{
		str_format(m_AssetsEditorState.m_aStatusMessage, sizeof(m_AssetsEditorState.m_aStatusMessage),
			"Reloaded images. %d slot(s) fell back to main asset.", FixedSlots);
		m_AssetsEditorState.m_StatusIsError = false;
	}
	else
	{
		str_copy(m_AssetsEditorState.m_aStatusMessage, "Reloaded images.");
		m_AssetsEditorState.m_StatusIsError = false;
	}
	m_AssetsEditorState.m_DirtyPreview = true;
}

void CMenus::AssetsEditorResetPartSlots()
{
	const auto &vAssets = m_AssetsEditorState.m_avAssets[m_AssetsEditorState.m_Type];
	int &MainAssetIndex = m_AssetsEditorState.m_aMainAssetIndex[m_AssetsEditorState.m_Type];
	int &DonorAssetIndex = m_AssetsEditorState.m_aDonorAssetIndex[m_AssetsEditorState.m_Type];
	if(vAssets.empty())
	{
		m_AssetsEditorState.m_vPartSlots.clear();
		AssetsEditorCancelDrag();
		return;
	}

	MainAssetIndex = std::clamp(MainAssetIndex, 0, (int)vAssets.size() - 1);
	DonorAssetIndex = std::clamp(DonorAssetIndex, 0, (int)vAssets.size() - 1);
	const char *pMainAssetName = vAssets[MainAssetIndex].m_aName;

	m_AssetsEditorState.m_vPartSlots.clear();
	if(m_AssetsEditorState.m_Type == ASSETS_EDITOR_TYPE_ENTITIES)
	{
		for(int Y = 0; Y < 16; ++Y)
		{
			for(int X = 0; X < 16; ++X)
			{
				SAssetsEditorPartSlot Slot;
				Slot.m_Group = 0;
				Slot.m_DstX = X;
				Slot.m_DstY = Y;
				Slot.m_DstW = 1;
				Slot.m_DstH = 1;
				Slot.m_SrcX = X;
				Slot.m_SrcY = Y;
				Slot.m_SrcW = 1;
				Slot.m_SrcH = 1;
				str_format(Slot.m_aFamilyKey, sizeof(Slot.m_aFamilyKey), "entities:tile_%03d", Y * 16 + X);
				str_copy(Slot.m_aSourceAsset, pMainAssetName);
				m_AssetsEditorState.m_vPartSlots.push_back(Slot);
			}
		}
	}
	else
	{
		std::vector<SAssetsEditorPartDef> vPartDefs;
		AssetsEditorCollectPartDefs(m_AssetsEditorState.m_Type, vPartDefs);
		for(const auto &PartDef : vPartDefs)
		{
			SAssetsEditorPartSlot Slot;
			Slot.m_SpriteId = PartDef.m_SpriteId;
			Slot.m_SourceSpriteId = PartDef.m_SpriteId;
			Slot.m_Group = PartDef.m_Group;

			const CDataSprite &Sprite = g_pData->m_aSprites[PartDef.m_SpriteId];
			Slot.m_DstX = Sprite.m_X;
			Slot.m_DstY = Sprite.m_Y;
			Slot.m_DstW = Sprite.m_W;
			Slot.m_DstH = Sprite.m_H;
			Slot.m_SrcX = Sprite.m_X;
			Slot.m_SrcY = Sprite.m_Y;
			Slot.m_SrcW = Sprite.m_W;
			Slot.m_SrcH = Sprite.m_H;
			AssetsEditorBuildFamilyKey(m_AssetsEditorState.m_Type, &Sprite, Slot.m_aFamilyKey, sizeof(Slot.m_aFamilyKey));
			str_copy(Slot.m_aSourceAsset, pMainAssetName);
			m_AssetsEditorState.m_vPartSlots.push_back(Slot);
		}

		if(m_AssetsEditorState.m_Type == ASSETS_EDITOR_TYPE_GAME)
		{
			struct SSyntheticSlotDef
			{
				const char *m_pName;
				int m_X;
				int m_Y;
				int m_W;
				int m_H;
			};
			static const SSyntheticSlotDef s_aSyntheticSlots[] = {
				{"ninja_bar_full_left", 21, 4, 1, 2},
				{"ninja_bar_full", 22, 4, 1, 2},
				{"ninja_bar_empty", 23, 4, 1, 2},
				{"ninja_bar_empty_right", 24, 4, 1, 2},
			};

			for(const auto &Synthetic : s_aSyntheticSlots)
			{
				bool Exists = false;
				for(const auto &ExistingSlot : m_AssetsEditorState.m_vPartSlots)
				{
					if(ExistingSlot.m_DstX == Synthetic.m_X && ExistingSlot.m_DstY == Synthetic.m_Y &&
						ExistingSlot.m_DstW == Synthetic.m_W && ExistingSlot.m_DstH == Synthetic.m_H)
					{
						Exists = true;
						break;
					}
				}
				if(Exists)
					continue;

				SAssetsEditorPartSlot Slot;
				Slot.m_SpriteId = -1;
				Slot.m_SourceSpriteId = -1;
				Slot.m_Group = 0;
				Slot.m_DstX = Synthetic.m_X;
				Slot.m_DstY = Synthetic.m_Y;
				Slot.m_DstW = Synthetic.m_W;
				Slot.m_DstH = Synthetic.m_H;
				Slot.m_SrcX = Synthetic.m_X;
				Slot.m_SrcY = Synthetic.m_Y;
				Slot.m_SrcW = Synthetic.m_W;
				Slot.m_SrcH = Synthetic.m_H;
				str_copy(Slot.m_aFamilyKey, Synthetic.m_pName, sizeof(Slot.m_aFamilyKey));
				str_copy(Slot.m_aSourceAsset, pMainAssetName);
				m_AssetsEditorState.m_vPartSlots.push_back(Slot);
			}
		}
	}

	AssetsEditorValidateRequiredSlotsForType(m_AssetsEditorState.m_Type);
	m_AssetsEditorState.m_DirtyPreview = true;
	m_AssetsEditorState.m_HasUnsavedChanges = false;
	AssetsEditorCancelDrag();
}

void CMenus::AssetsEditorEnsureDefaultExportNames()
{
	for(int Type = 0; Type < ASSETS_EDITOR_TYPE_COUNT; ++Type)
	{
		if(m_AssetsEditorState.m_aaExportNameByType[Type][0] != '\0')
			continue;
		char aDefaultName[64];
		str_format(aDefaultName, sizeof(aDefaultName), "my_%s", AssetsEditorTypeName(Type));
		str_copy(m_AssetsEditorState.m_aaExportNameByType[Type], aDefaultName);
	}
}

void CMenus::AssetsEditorSyncExportNameFromType()
{
	AssetsEditorEnsureDefaultExportNames();
	str_copy(m_AssetsEditorState.m_aExportName, m_AssetsEditorState.m_aaExportNameByType[m_AssetsEditorState.m_Type]);
}

void CMenus::AssetsEditorCommitExportNameForType()
{
	AssetsEditorEnsureDefaultExportNames();
	str_copy(m_AssetsEditorState.m_aaExportNameByType[m_AssetsEditorState.m_Type], m_AssetsEditorState.m_aExportName);
}

void CMenus::AssetsEditorValidateRequiredSlotsForType(int Type)
{
	auto &vSlots = m_AssetsEditorState.m_vPartSlots;
	const auto &vAssets = m_AssetsEditorState.m_avAssets[Type];
	const int MainAssetIndex = m_AssetsEditorState.m_aMainAssetIndex[Type];
	const char *pMainAssetName = MainAssetIndex >= 0 && MainAssetIndex < (int)vAssets.size() ? vAssets[MainAssetIndex].m_aName : "default";
	const int ImageId = AssetsEditorTypeImageId(Type);
	int AddedSlots = 0;

	auto HasSlotByName = [&](const char *pName) {
		for(const auto &Slot : vSlots)
		{
			if(Slot.m_SpriteId >= 0 && Slot.m_SpriteId < NUM_SPRITES)
			{
				const CDataSprite &Sprite = g_pData->m_aSprites[Slot.m_SpriteId];
				if(Sprite.m_pName != nullptr && str_comp(Sprite.m_pName, pName) == 0)
					return true;
			}
			if(str_comp(Slot.m_aFamilyKey, pName) == 0)
				return true;
		}
		return false;
	};

	auto HasSlotByGeometry = [&](int X, int Y, int W, int H) {
		for(const auto &Slot : vSlots)
		{
			if(Slot.m_DstX == X && Slot.m_DstY == Y && Slot.m_DstW == W && Slot.m_DstH == H)
				return true;
		}
		return false;
	};

	auto AddSlotByName = [&](const char *pName, const char *pAliasName, int FallbackX, int FallbackY, int FallbackW, int FallbackH) {
		if(HasSlotByGeometry(FallbackX, FallbackY, FallbackW, FallbackH))
			return;
		if(HasSlotByName(pName) || (pAliasName != nullptr && pAliasName[0] != '\0' && HasSlotByName(pAliasName)))
			return;

		SAssetsEditorPartSlot Slot;
		Slot.m_SpriteId = AssetsEditorFindSpriteIdByName(pName, ImageId);
		if(Slot.m_SpriteId < 0 && pAliasName != nullptr && pAliasName[0] != '\0')
			Slot.m_SpriteId = AssetsEditorFindSpriteIdByName(pAliasName, ImageId);
		Slot.m_SourceSpriteId = Slot.m_SpriteId;
		Slot.m_Group = 0;
		if(Slot.m_SpriteId >= 0)
		{
			const CDataSprite &Sprite = g_pData->m_aSprites[Slot.m_SpriteId];
			Slot.m_DstX = Sprite.m_X;
			Slot.m_DstY = Sprite.m_Y;
			Slot.m_DstW = Sprite.m_W;
			Slot.m_DstH = Sprite.m_H;
			Slot.m_SrcX = Sprite.m_X;
			Slot.m_SrcY = Sprite.m_Y;
			Slot.m_SrcW = Sprite.m_W;
			Slot.m_SrcH = Sprite.m_H;
			AssetsEditorBuildFamilyKey(Type, &Sprite, Slot.m_aFamilyKey, sizeof(Slot.m_aFamilyKey));
		}
		else
		{
			Slot.m_DstX = FallbackX;
			Slot.m_DstY = FallbackY;
			Slot.m_DstW = FallbackW;
			Slot.m_DstH = FallbackH;
			Slot.m_SrcX = FallbackX;
			Slot.m_SrcY = FallbackY;
			Slot.m_SrcW = FallbackW;
			Slot.m_SrcH = FallbackH;
			str_copy(Slot.m_aFamilyKey, pName, sizeof(Slot.m_aFamilyKey));
		}
		str_copy(Slot.m_aSourceAsset, pMainAssetName);
		vSlots.push_back(Slot);
		++AddedSlots;
	};

	if(Type == ASSETS_EDITOR_TYPE_GAME)
	{
		AddSlotByName("pickup_health", "pickup_heart", 10, 2, 2, 2);
		AddSlotByName("pickup_armor", nullptr, 12, 2, 2, 2);
		AddSlotByName("pickup_armor_shotgun", nullptr, 15, 2, 2, 2);
		AddSlotByName("ninja_bar_full_left", nullptr, 21, 4, 1, 2);
		AddSlotByName("ninja_bar_full", nullptr, 22, 4, 1, 2);
		AddSlotByName("ninja_bar_empty", nullptr, 23, 4, 1, 2);
		AddSlotByName("ninja_bar_empty_right", nullptr, 24, 4, 1, 2);
	}
	else if(Type == ASSETS_EDITOR_TYPE_PARTICLES)
	{
		AddSlotByName("part_slice", nullptr, 0, 0, 1, 1);
		AddSlotByName("part_ball", nullptr, 1, 0, 1, 1);
		AddSlotByName("part_splat01", nullptr, 2, 0, 1, 1);
		AddSlotByName("part_splat02", nullptr, 3, 0, 1, 1);
		AddSlotByName("part_splat03", nullptr, 4, 0, 1, 1);
		AddSlotByName("part_smoke", nullptr, 0, 1, 1, 1);
		AddSlotByName("part_shell", nullptr, 0, 2, 2, 2);
		AddSlotByName("part_expl01", nullptr, 0, 4, 4, 4);
		AddSlotByName("part_airjump", nullptr, 2, 2, 2, 2);
		AddSlotByName("part_hit01", nullptr, 4, 1, 2, 2);
	}

	if(AddedSlots > 0)
	{
		str_format(m_AssetsEditorState.m_aStatusMessage, sizeof(m_AssetsEditorState.m_aStatusMessage), "Added %d missing required slot(s).", AddedSlots);
		m_AssetsEditorState.m_StatusIsError = false;
	}
}

void CMenus::AssetsEditorBuildFamilyKey(int Type, const CDataSprite *pSprite, char *pOut, int OutSize)
{
	if(pOut == nullptr || OutSize <= 0)
		return;

	if(pSprite == nullptr || pSprite->m_pName == nullptr)
	{
		str_copy(pOut, "part", OutSize);
		return;
	}

	const char *pName = pSprite->m_pName;
	if(Type == ASSETS_EDITOR_TYPE_GAME)
	{
		if(str_comp_num(pName, "weapon_", 7) == 0)
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
	}
	else if(Type == ASSETS_EDITOR_TYPE_HUD)
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

bool CMenus::AssetsEditorCopyRectScaledNearest(CImageInfo &Dst, const CImageInfo &Src, int DstX, int DstY, int DstW, int DstH, int SrcX, int SrcY, int SrcW, int SrcH)
{
	if(Dst.m_pData == nullptr || Src.m_pData == nullptr || Dst.m_Format != CImageInfo::FORMAT_RGBA || Src.m_Format != CImageInfo::FORMAT_RGBA)
		return false;
	if(DstW <= 0 || DstH <= 0 || SrcW <= 0 || SrcH <= 0)
		return false;
	if(DstX < 0 || DstY < 0 || SrcX < 0 || SrcY < 0)
		return false;
	const int DstWidth = (int)Dst.m_Width;
	const int DstHeight = (int)Dst.m_Height;
	const int SrcWidth = (int)Src.m_Width;
	const int SrcHeight = (int)Src.m_Height;
	if(DstX + DstW > DstWidth || DstY + DstH > DstHeight)
		return false;
	if(SrcX + SrcW > SrcWidth || SrcY + SrcH > SrcHeight)
		return false;

	uint8_t *pDstData = static_cast<uint8_t *>(Dst.m_pData);
	const uint8_t *pSrcData = static_cast<const uint8_t *>(Src.m_pData);
	for(int Y = 0; Y < DstH; ++Y)
	{
		const int SampleY = SrcY + ((int64_t)Y * SrcH) / DstH;
		for(int X = 0; X < DstW; ++X)
		{
			const int SampleX = SrcX + ((int64_t)X * SrcW) / DstW;
			const int DstOff = ((DstY + Y) * Dst.m_Width + (DstX + X)) * 4;
			const int SrcOff = (SampleY * Src.m_Width + SampleX) * 4;
			pDstData[DstOff + 0] = pSrcData[SrcOff + 0];
			pDstData[DstOff + 1] = pSrcData[SrcOff + 1];
			pDstData[DstOff + 2] = pSrcData[SrcOff + 2];
			pDstData[DstOff + 3] = pSrcData[SrcOff + 3];
		}
	}
	return true;
}

bool CMenus::AssetsEditorComposeImage(CImageInfo &OutputImage)
{
	const auto &vAssets = m_AssetsEditorState.m_avAssets[m_AssetsEditorState.m_Type];
	const int MainAssetIndex = m_AssetsEditorState.m_aMainAssetIndex[m_AssetsEditorState.m_Type];
	if(vAssets.empty() || MainAssetIndex < 0 || MainAssetIndex >= (int)vAssets.size())
	{
		str_copy(m_AssetsEditorState.m_aStatusMessage, "No assets available for composition.");
		m_AssetsEditorState.m_StatusIsError = true;
		return false;
	}

	const SAssetsEditorAssetEntry &MainAsset = vAssets[MainAssetIndex];
	const CImageInfo *pBaseImage = AssetsEditorGetCachedImage(m_AssetsEditorState.m_Type, MainAsset.m_aName, vAssets, Graphics());
	if(pBaseImage == nullptr)
	{
		str_format(m_AssetsEditorState.m_aStatusMessage, sizeof(m_AssetsEditorState.m_aStatusMessage), "Failed to load main asset: %s", MainAsset.m_aName);
		m_AssetsEditorState.m_StatusIsError = true;
		return false;
	}

	CImageInfo BaseImageStable = pBaseImage->DeepCopy();
	OutputImage = BaseImageStable.DeepCopy();
	if(OutputImage.m_Format != CImageInfo::FORMAT_RGBA)
		ConvertToRgba(OutputImage);

	struct SPreparedDonor
	{
		char m_aName[64] = {0};
		CImageInfo m_Image;
	};
	std::vector<SPreparedDonor> vPreparedDonors;

	auto FindPreparedDonor = [&](const char *pName) -> const CImageInfo * {
		for(const auto &Prepared : vPreparedDonors)
		{
			if(str_comp(Prepared.m_aName, pName) == 0)
				return &Prepared.m_Image;
		}
		return nullptr;
	};

	auto GetPreparedDonor = [&](const char *pName) -> const CImageInfo * {
		if(str_comp(pName, MainAsset.m_aName) == 0)
			return &BaseImageStable;

		if(const CImageInfo *pPrepared = FindPreparedDonor(pName))
			return pPrepared;

		const CImageInfo *pCachedDonor = AssetsEditorGetCachedImage(m_AssetsEditorState.m_Type, pName, vAssets, Graphics());
		if(pCachedDonor == nullptr)
			return nullptr;

		SPreparedDonor &Prepared = vPreparedDonors.emplace_back();
		str_copy(Prepared.m_aName, pName);
		Prepared.m_Image = pCachedDonor->DeepCopy();
		if(Prepared.m_Image.m_Format != CImageInfo::FORMAT_RGBA)
			ConvertToRgba(Prepared.m_Image);
		return &Prepared.m_Image;
	};

	const int GridX = maximum(1, AssetsEditorGridX(m_AssetsEditorState.m_Type));
	const int GridY = maximum(1, AssetsEditorGridY(m_AssetsEditorState.m_Type));
	int SkippedSlots = 0;

	for(const auto &Slot : m_AssetsEditorState.m_vPartSlots)
	{
		if(str_comp(Slot.m_aSourceAsset, MainAsset.m_aName) == 0 &&
			Slot.m_SrcX == Slot.m_DstX && Slot.m_SrcY == Slot.m_DstY &&
			Slot.m_SrcW == Slot.m_DstW && Slot.m_SrcH == Slot.m_DstH)
			continue;

		const CImageInfo *pDonorImage = GetPreparedDonor(Slot.m_aSourceAsset);
		if(pDonorImage == nullptr)
		{
			++SkippedSlots;
			continue;
		}

		const int DestGridW = OutputImage.m_Width / GridX;
		const int DestGridH = OutputImage.m_Height / GridY;
		const int SrcGridW = pDonorImage->m_Width / GridX;
		const int SrcGridH = pDonorImage->m_Height / GridY;
		if(DestGridW <= 0 || DestGridH <= 0 || SrcGridW <= 0 || SrcGridH <= 0 || Slot.m_DstW <= 0 || Slot.m_DstH <= 0 || Slot.m_SrcW <= 0 || Slot.m_SrcH <= 0)
		{
			++SkippedSlots;
			continue;
		}

		const int DestX = Slot.m_DstX * DestGridW;
		const int DestY = Slot.m_DstY * DestGridH;
		const int DestW = Slot.m_DstW * DestGridW;
		const int DestH = Slot.m_DstH * DestGridH;
		const int SrcX = Slot.m_SrcX * SrcGridW;
		const int SrcY = Slot.m_SrcY * SrcGridH;
		const int SrcW = Slot.m_SrcW * SrcGridW;
		const int SrcH = Slot.m_SrcH * SrcGridH;
		if(DestW <= 0 || DestH <= 0 || SrcW <= 0 || SrcH <= 0)
		{
			++SkippedSlots;
			continue;
		}

		if(!AssetsEditorCopyRectScaledNearest(OutputImage, *pDonorImage, DestX, DestY, DestW, DestH, SrcX, SrcY, SrcW, SrcH))
		{
			++SkippedSlots;
			continue;
		}
	}

	for(auto &Prepared : vPreparedDonors)
		Prepared.m_Image.Free();
	BaseImageStable.Free();

	if(SkippedSlots > 0)
	{
		str_format(m_AssetsEditorState.m_aStatusMessage, sizeof(m_AssetsEditorState.m_aStatusMessage), "Preview built with %d skipped slot(s).", SkippedSlots);
		m_AssetsEditorState.m_StatusIsError = false;
	}
	return true;
}

bool CMenus::AssetsEditorExport()
{
	AssetsEditorCommitExportNameForType();
	if(m_AssetsEditorState.m_aExportName[0] == '\0')
	{
		str_copy(m_AssetsEditorState.m_aStatusMessage, "Choose export name first.");
		m_AssetsEditorState.m_StatusIsError = true;
		return false;
	}
	if(str_find(m_AssetsEditorState.m_aExportName, "/") || str_find(m_AssetsEditorState.m_aExportName, "\\") || str_find(m_AssetsEditorState.m_aExportName, ".."))
	{
		str_copy(m_AssetsEditorState.m_aStatusMessage, "Export name contains invalid characters.");
		m_AssetsEditorState.m_StatusIsError = true;
		return false;
	}

	CImageInfo OutputImage;
	if(!AssetsEditorComposeImage(OutputImage))
		return false;

	char aFolder[IO_MAX_PATH_LENGTH];
	char aPngPath[IO_MAX_PATH_LENGTH];
	if(m_AssetsEditorState.m_Type == ASSETS_EDITOR_TYPE_ENTITIES)
	{
		Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
		Storage()->CreateFolder("assets/entities", IStorage::TYPE_SAVE);
		str_format(aPngPath, sizeof(aPngPath), "assets/entities/%s.png", m_AssetsEditorState.m_aExportName);
	}
	else
	{
		str_format(aFolder, sizeof(aFolder), "assets/%s", AssetsEditorTypeName(m_AssetsEditorState.m_Type));
		Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
		Storage()->CreateFolder(aFolder, IStorage::TYPE_SAVE);
		str_format(aPngPath, sizeof(aPngPath), "assets/%s/%s.png", AssetsEditorTypeName(m_AssetsEditorState.m_Type), m_AssetsEditorState.m_aExportName);
	}

	if(Storage()->FileExists(aPngPath, IStorage::TYPE_SAVE))
	{
		OutputImage.Free();
		str_copy(m_AssetsEditorState.m_aStatusMessage, "Asset with this name already exists.");
		m_AssetsEditorState.m_StatusIsError = true;
		return false;
	}

	IOHANDLE File = Storage()->OpenFile(aPngPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File || !CImageLoader::SavePng(File, aPngPath, OutputImage))
	{
		OutputImage.Free();
		str_copy(m_AssetsEditorState.m_aStatusMessage, "Failed to write PNG export.");
		m_AssetsEditorState.m_StatusIsError = true;
		return false;
	}

	OutputImage.Free();
	AssetsEditorReloadAssetsImagesOnly();
	str_format(m_AssetsEditorState.m_aStatusMessage, sizeof(m_AssetsEditorState.m_aStatusMessage), "Exported to %s", aPngPath);
	m_AssetsEditorState.m_StatusIsError = false;
	m_AssetsEditorState.m_HasUnsavedChanges = false;
	return true;
}

void CMenus::AssetsEditorCancelDrag()
{
	m_AssetsEditorState.m_DragActive = false;
	m_AssetsEditorState.m_ActiveDraggedSlotIndex = -1;
	m_AssetsEditorState.m_HoveredDonorSlotIndex = -1;
	m_AssetsEditorState.m_HoveredTargetSlotIndex = -1;
	m_AssetsEditorState.m_aDraggedSourceAsset[0] = '\0';
	m_AssetsEditorState.m_HoverCycleSlotIndex = -1;
	m_AssetsEditorState.m_HoverCyclePositionX = -1;
	m_AssetsEditorState.m_HoverCyclePositionY = -1;
	m_AssetsEditorState.m_HoverCycleCandidateCursor = 0;
	m_AssetsEditorState.m_vHoverCycleCandidates.clear();
}

void CMenus::AssetsEditorRequestClose()
{
	AssetsEditorCancelDrag();
	if(m_AssetsEditorState.m_HasUnsavedChanges)
	{
		m_AssetsEditorState.m_PendingCloseRequest = true;
		m_AssetsEditorState.m_ShowExitConfirm = true;
		return;
	}
	AssetsEditorCloseNow();
}

void CMenus::AssetsEditorCloseNow()
{
	m_AssetsEditorState.m_VisualsEditorOpen = false;
	m_AssetsEditorState.m_PendingCloseRequest = false;
	m_AssetsEditorState.m_ShowExitConfirm = false;
	m_AssetsEditorState.m_HasUnsavedChanges = false;
	AssetsEditorClearAssets();
}

void CMenus::AssetsEditorRenderExitConfirm(const CUIRect &Rect)
{
	CUIRect Overlay = Rect;
	Overlay.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.6f), IGraphics::CORNER_ALL, 0.0f);

	CUIRect Box;
	Box.w = minimum(520.0f, Rect.w - 30.0f);
	Box.h = 140.0f;
	Box.x = Rect.x + (Rect.w - Box.w) * 0.5f;
	Box.y = Rect.y + (Rect.h - Box.h) * 0.5f;
	Box.Draw(ColorRGBA(0.1f, 0.1f, 0.1f, 0.95f), IGraphics::CORNER_ALL, 8.0f);

	CUIRect Title, Message, Buttons;
	Box.Margin(10.0f, &Box);
	Box.HSplitTop(LineSize + 4.0f, &Title, &Box);
	Box.HSplitTop(LineSize, &Message, &Box);
	Box.HSplitBottom(LineSize + 4.0f, &Box, &Buttons);

	Ui()->DoLabel(&Title, "Save asset before closing?", FontSize * 1.1f, TEXTALIGN_ML);
	Ui()->DoLabel(&Message, "You have unsaved changes.", FontSize, TEXTALIGN_ML);

	CUIRect SaveButton, DiscardButton, CancelButton;
	Buttons.VSplitLeft((Buttons.w - MarginSmall * 2.0f) / 3.0f, &SaveButton, &Buttons);
	Buttons.VSplitLeft(MarginSmall, nullptr, &Buttons);
	Buttons.VSplitLeft((Buttons.w - MarginSmall) / 2.0f, &DiscardButton, &Buttons);
	Buttons.VSplitLeft(MarginSmall, nullptr, &Buttons);
	CancelButton = Buttons;

	static CButtonContainer s_SaveButton;
	static CButtonContainer s_DiscardButton;
	static CButtonContainer s_CancelButton;
	if(DoButton_Menu(&s_SaveButton, "Save", 0, &SaveButton))
	{
		if(AssetsEditorExport())
			AssetsEditorCloseNow();
	}
	if(DoButton_Menu(&s_DiscardButton, "Discard", 0, &DiscardButton))
	{
		AssetsEditorCloseNow();
	}
	if(DoButton_Menu(&s_CancelButton, "Cancel", 0, &CancelButton) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
	{
		m_AssetsEditorState.m_ShowExitConfirm = false;
		m_AssetsEditorState.m_PendingCloseRequest = false;
	}
}

void CMenus::AssetsEditorCollectHoveredCandidates(const CUIRect &Rect, int Type, const std::vector<SAssetsEditorPartSlot> &vSlots, vec2 Mouse, std::vector<int> &vOutCandidates) const
{
	vOutCandidates.clear();
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
	vCandidates.reserve(vSlots.size());
	for(size_t SlotIndex = 0; SlotIndex < vSlots.size(); ++SlotIndex)
	{
		const SAssetsEditorPartSlot &Slot = vSlots[SlotIndex];
		CUIRect SlotRect;
		if(!AssetsEditorGetSlotRectInFitted(Rect, Type, Slot, SlotRect))
			continue;
		if(Mouse.x < SlotRect.x || Mouse.x >= SlotRect.x + SlotRect.w || Mouse.y < SlotRect.y || Mouse.y >= SlotRect.y + SlotRect.h)
			continue;

		SCandidate &Candidate = vCandidates.emplace_back();
		Candidate.m_SlotIndex = (int)SlotIndex;
		Candidate.m_Area = SlotRect.w * SlotRect.h;
	}

	std::stable_sort(vCandidates.begin(), vCandidates.end(), [](const SCandidate &Left, const SCandidate &Right) {
		if(Left.m_Area != Right.m_Area)
			return Left.m_Area < Right.m_Area;
		return Left.m_SlotIndex < Right.m_SlotIndex;
	});

	vOutCandidates.reserve(vCandidates.size());
	for(const auto &Candidate : vCandidates)
		vOutCandidates.push_back(Candidate.m_SlotIndex);
}

int CMenus::AssetsEditorResolveHoveredSlotWithCycle(const CUIRect &Rect, int Type, const std::vector<SAssetsEditorPartSlot> &vSlots, vec2 Mouse, bool ClickedLmb, int PreferredSlotIndex)
{
	std::vector<int> vCandidates;
	AssetsEditorCollectHoveredCandidates(Rect, Type, vSlots, Mouse, vCandidates);
	if(vCandidates.empty())
	{
		if(PreferredSlotIndex < 0)
		{
			m_AssetsEditorState.m_HoverCycleSlotIndex = -1;
			m_AssetsEditorState.m_HoverCyclePositionX = -1;
			m_AssetsEditorState.m_HoverCyclePositionY = -1;
			m_AssetsEditorState.m_HoverCycleCandidateCursor = 0;
			m_AssetsEditorState.m_vHoverCycleCandidates.clear();
		}
		return -1;
	}

	if(PreferredSlotIndex >= 0 && PreferredSlotIndex < (int)vSlots.size())
	{
		const SAssetsEditorPartSlot &PreferredSlot = vSlots[PreferredSlotIndex];
		for(const int Candidate : vCandidates)
		{
			const SAssetsEditorPartSlot &CandidateSlot = vSlots[Candidate];
			if(str_comp(CandidateSlot.m_aFamilyKey, PreferredSlot.m_aFamilyKey) != 0)
				continue;
			if(!AssetsEditorSlotSameNormalizedSize(CandidateSlot, PreferredSlot))
				continue;
			return Candidate;
		}
		return vCandidates.front();
	}

	const int MousePosX = (int)(Mouse.x * 10.0f);
	const int MousePosY = (int)(Mouse.y * 10.0f);
	const bool SamePosition = m_AssetsEditorState.m_HoverCyclePositionX == MousePosX && m_AssetsEditorState.m_HoverCyclePositionY == MousePosY;
	const bool SameCandidates = m_AssetsEditorState.m_vHoverCycleCandidates.size() == vCandidates.size() &&
		std::equal(m_AssetsEditorState.m_vHoverCycleCandidates.begin(), m_AssetsEditorState.m_vHoverCycleCandidates.end(), vCandidates.begin());
	if(!SamePosition || !SameCandidates)
	{
		m_AssetsEditorState.m_HoverCyclePositionX = MousePosX;
		m_AssetsEditorState.m_HoverCyclePositionY = MousePosY;
		m_AssetsEditorState.m_HoverCycleCandidateCursor = 0;
		m_AssetsEditorState.m_vHoverCycleCandidates = vCandidates;
	}
	else if(ClickedLmb && vCandidates.size() > 1)
	{
		m_AssetsEditorState.m_HoverCycleCandidateCursor = (m_AssetsEditorState.m_HoverCycleCandidateCursor + 1) % (int)vCandidates.size();
		str_format(m_AssetsEditorState.m_aStatusMessage, sizeof(m_AssetsEditorState.m_aStatusMessage), "Selected candidate %d/%d under cursor.", m_AssetsEditorState.m_HoverCycleCandidateCursor + 1, (int)vCandidates.size());
		m_AssetsEditorState.m_StatusIsError = false;
	}

	m_AssetsEditorState.m_HoverCycleCandidateCursor = std::clamp(m_AssetsEditorState.m_HoverCycleCandidateCursor, 0, (int)vCandidates.size() - 1);
	const int Selected = vCandidates[m_AssetsEditorState.m_HoverCycleCandidateCursor];
	m_AssetsEditorState.m_HoverCycleSlotIndex = Selected;
	return Selected;
}

void CMenus::AssetsEditorApplyDrop(int TargetSlotIndex, const char *pDonorName, int SourceSlotIndex, bool ApplyAllSameSize)
{
	if(TargetSlotIndex < 0 || TargetSlotIndex >= (int)m_AssetsEditorState.m_vPartSlots.size() || SourceSlotIndex < 0 ||
		SourceSlotIndex >= (int)m_AssetsEditorState.m_vPartSlots.size() || pDonorName == nullptr || pDonorName[0] == '\0')
		return;

	const SAssetsEditorPartSlot SourceSlot = m_AssetsEditorState.m_vPartSlots[SourceSlotIndex];
	const SAssetsEditorPartSlot &TargetSlot = m_AssetsEditorState.m_vPartSlots[TargetSlotIndex];
	const bool SameSize = AssetsEditorSlotSameNormalizedSize(SourceSlot, TargetSlot);
	if(ApplyAllSameSize && !SameSize)
	{
		str_copy(m_AssetsEditorState.m_aStatusMessage, "Can only drop onto same-size parts.");
		m_AssetsEditorState.m_StatusIsError = true;
		return;
	}

	auto AssignSlot = [this, pDonorName, &SourceSlot](SAssetsEditorPartSlot &Slot) {
		str_copy(Slot.m_aSourceAsset, pDonorName);
		Slot.m_SourceSpriteId = SourceSlot.m_SpriteId;
		Slot.m_SrcX = SourceSlot.m_DstX;
		Slot.m_SrcY = SourceSlot.m_DstY;
		Slot.m_SrcW = SourceSlot.m_DstW;
		Slot.m_SrcH = SourceSlot.m_DstH;
	};

	if(ApplyAllSameSize)
	{
		int Changed = 0;
		for(auto &Slot : m_AssetsEditorState.m_vPartSlots)
		{
			if(str_comp(Slot.m_aFamilyKey, TargetSlot.m_aFamilyKey) != 0)
				continue;
			if(!AssetsEditorSlotSameNormalizedSize(Slot, TargetSlot))
				continue;
			AssignSlot(Slot);
			++Changed;
		}
		str_format(m_AssetsEditorState.m_aStatusMessage, sizeof(m_AssetsEditorState.m_aStatusMessage), "Applied to %d same-family part(s).", Changed);
		m_AssetsEditorState.m_StatusIsError = false;
	}
	else
	{
		AssignSlot(m_AssetsEditorState.m_vPartSlots[TargetSlotIndex]);
		str_copy(m_AssetsEditorState.m_aStatusMessage, "Part updated.");
		m_AssetsEditorState.m_StatusIsError = false;
	}

	m_AssetsEditorState.m_DirtyPreview = true;
	m_AssetsEditorState.m_HasUnsavedChanges = true;
}

void CMenus::AssetsEditorUpdatePreviewIfDirty()
{
	if(!m_AssetsEditorState.m_DirtyPreview)
		return;

	CImageInfo ComposedImage;
	if(AssetsEditorComposeImage(ComposedImage))
	{
		m_AssetsEditorState.m_ComposedPreviewWidth = ComposedImage.m_Width;
		m_AssetsEditorState.m_ComposedPreviewHeight = ComposedImage.m_Height;
		Graphics()->UnloadTexture(&m_AssetsEditorState.m_ComposedPreviewTexture);
		m_AssetsEditorState.m_ComposedPreviewTexture = Graphics()->LoadTextureRawMove(ComposedImage, 0, "assets_editor_preview");
		m_AssetsEditorState.m_LastComposeFailed = false;
	}
	else
	{
		Graphics()->UnloadTexture(&m_AssetsEditorState.m_ComposedPreviewTexture);
		m_AssetsEditorState.m_ComposedPreviewWidth = 0;
		m_AssetsEditorState.m_ComposedPreviewHeight = 0;
		m_AssetsEditorState.m_LastComposeFailed = true;
	}

	m_AssetsEditorState.m_DirtyPreview = false;
}

void CMenus::AssetsEditorRenderCanvas(const CUIRect &Rect, IGraphics::CTextureHandle Texture, int W, int H, int Type, bool ShowGrid, int HighlightSlot)
{
	if(!Texture.IsValid() || W <= 0 || H <= 0)
	{
		Ui()->DoLabel(&Rect, "No preview", FontSize, TEXTALIGN_MC);
		return;
	}

	CUIRect FittedRect;
	if(!AssetsEditorDrawTextureFitted(Rect, Texture, W, H, Graphics(), &FittedRect))
	{
		Ui()->DoLabel(&Rect, "No preview", FontSize, TEXTALIGN_MC);
		return;
	}

	Graphics()->TextureClear();
	if(ShowGrid)
	{
		const int GridX = AssetsEditorGridX(Type);
		const int GridY = AssetsEditorGridY(Type);
		if(GridX > 0 && GridY > 0)
		{
			Graphics()->LinesBegin();
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.18f);
			for(int X = 0; X <= GridX; ++X)
			{
				const float Px = FittedRect.x + (FittedRect.w * X) / GridX;
				const IGraphics::CLineItem Line(Px, FittedRect.y, Px, FittedRect.y + FittedRect.h);
				Graphics()->LinesDraw(&Line, 1);
			}
			for(int Y = 0; Y <= GridY; ++Y)
			{
				const float Py = FittedRect.y + (FittedRect.h * Y) / GridY;
				const IGraphics::CLineItem Line(FittedRect.x, Py, FittedRect.x + FittedRect.w, Py);
				Graphics()->LinesDraw(&Line, 1);
			}
			Graphics()->LinesEnd();
		}
	}

	Graphics()->LinesBegin();
	for(size_t SlotIndex = 0; SlotIndex < m_AssetsEditorState.m_vPartSlots.size(); ++SlotIndex)
	{
		const SAssetsEditorPartSlot &Slot = m_AssetsEditorState.m_vPartSlots[SlotIndex];
		CUIRect SlotRect;
		if(!AssetsEditorGetSlotRectInFitted(FittedRect, Type, Slot, SlotRect))
			continue;

		const bool IsHighlighted = (int)SlotIndex == HighlightSlot;
		if(IsHighlighted)
			Graphics()->SetColor(1.0f, 0.85f, 0.2f, 0.95f);
		else
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, ShowGrid ? 0.16f : 0.24f);

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
	if(!m_AssetsEditorState.m_VisualsEditorInitialized)
	{
		AssetsEditorReloadAssets();
		AssetsEditorResetPartSlots();
		AssetsEditorEnsureDefaultExportNames();
		AssetsEditorSyncExportNameFromType();
		m_AssetsEditorState.m_VisualsEditorInitialized = true;
	}
	else
	{
		AssetsEditorEnsureDefaultExportNames();
	}

	if(m_AssetsEditorState.m_FullscreenOpen)
		MainView = *Ui()->Screen();

	if(!m_AssetsEditorState.m_ShowExitConfirm && Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
	{
		AssetsEditorRequestClose();
		if(!m_AssetsEditorState.m_VisualsEditorOpen)
			return;
	}
	if(m_AssetsEditorState.m_ShowExitConfirm)
	{
		AssetsEditorRenderExitConfirm(*Ui()->Screen());
		return;
	}

	CUIRect EditorRect = MainView;
	EditorRect.Margin(8.0f, &EditorRect);
	EditorRect.Draw(ColorRGBA(0.10f, 0.11f, 0.15f, 1.0f), IGraphics::CORNER_ALL, 8.0f);
	Ui()->ClipEnable(&EditorRect);
	SScopedClip ClipGuard{Ui()};

	CUIRect WorkRect;
	EditorRect.Margin(8.0f, &WorkRect);
	CUIRect TopPanel, TopBarRow1, TopBarRow2, ContentView, StatusRect;
	WorkRect.HSplitTop(LineSize * 2.0f + MarginSmall + 8.0f, &TopPanel, &ContentView);
	TopPanel.HSplitTop(LineSize + 4.0f, &TopBarRow1, &TopPanel);
	TopPanel.HSplitTop(MarginExtraSmall, nullptr, &TopPanel);
	TopBarRow2 = TopPanel;
	ContentView.HSplitBottom(LineSize + MarginSmall, &ContentView, &StatusRect);

	CUIRect CloseButton, ModeRow, ExportRow, ReloadButton, ExportButton, GridToggleButton;
	auto SplitLeftSafe = [](CUIRect &Source, float Wanted, CUIRect *pLeft, CUIRect *pRight) {
		const float Cut = minimum(Wanted, Source.w);
		Source.VSplitLeft(Cut, pLeft, pRight);
	};
	auto SplitRightSafe = [](CUIRect &Source, float Wanted, CUIRect *pLeft, CUIRect *pRight) {
		const float Cut = minimum(Wanted, Source.w);
		Source.VSplitRight(Cut, pLeft, pRight);
	};

	SplitLeftSafe(TopBarRow1, 28.0f, &CloseButton, &TopBarRow1);
	SplitLeftSafe(TopBarRow1, MarginSmall, nullptr, &TopBarRow1);
	const float ModeW = minimum(190.0f, maximum(120.0f, TopBarRow1.w * 0.28f));
	SplitLeftSafe(TopBarRow1, ModeW, &ModeRow, &TopBarRow1);
	SplitLeftSafe(TopBarRow1, MarginSmall, nullptr, &TopBarRow1);
	ExportRow = TopBarRow1;

	const float GridW = minimum(110.0f, maximum(90.0f, TopBarRow2.w * 0.25f));
	const float ExportButtonW = minimum(105.0f, maximum(82.0f, TopBarRow2.w * 0.20f));
	const float ReloadW = minimum(95.0f, maximum(72.0f, TopBarRow2.w * 0.18f));
	SplitRightSafe(TopBarRow2, GridW, &TopBarRow2, &GridToggleButton);
	SplitRightSafe(TopBarRow2, MarginSmall, &TopBarRow2, nullptr);
	SplitRightSafe(TopBarRow2, ExportButtonW, &TopBarRow2, &ExportButton);
	SplitRightSafe(TopBarRow2, MarginSmall, &TopBarRow2, nullptr);
	SplitRightSafe(TopBarRow2, ReloadW, &TopBarRow2, &ReloadButton);

	static CButtonContainer s_CloseButton;
	if(Ui()->DoButton_FontIcon(&s_CloseButton, XMARK, 0, &CloseButton, IGraphics::CORNER_ALL))
	{
		AssetsEditorRequestClose();
		if(!m_AssetsEditorState.m_VisualsEditorOpen)
			return;
	}

	CUIRect ModeLabel, ModeDropDown;
	SplitLeftSafe(ModeRow, minimum(45.0f, ModeRow.w * 0.55f), &ModeLabel, &ModeDropDown);
	Ui()->DoLabel(&ModeLabel, "Mode", FontSize, TEXTALIGN_ML);
	const char *apModeNames[ASSETS_EDITOR_TYPE_COUNT];
	for(int Type = 0; Type < ASSETS_EDITOR_TYPE_COUNT; ++Type)
		apModeNames[Type] = AssetsEditorTypeDisplayName(Type);
	static CUi::SDropDownState s_ModeDropDownState;
	static CScrollRegion s_ModeDropDownScrollRegion;
	s_ModeDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_ModeDropDownScrollRegion;
	const int NewMode = Ui()->DoDropDown(&ModeDropDown, m_AssetsEditorState.m_Type, apModeNames, ASSETS_EDITOR_TYPE_COUNT, s_ModeDropDownState);
	if(NewMode != m_AssetsEditorState.m_Type)
	{
		AssetsEditorCommitExportNameForType();
		AssetsEditorCancelDrag();
		m_AssetsEditorState.m_Type = NewMode;
		AssetsEditorSyncExportNameFromType();
		Graphics()->UnloadTexture(&m_AssetsEditorState.m_ComposedPreviewTexture);
		m_AssetsEditorState.m_ComposedPreviewWidth = 0;
		m_AssetsEditorState.m_ComposedPreviewHeight = 0;
		m_AssetsEditorState.m_ShowExitConfirm = false;
		AssetsEditorResetPartSlots();
	}

	auto &vAssets = m_AssetsEditorState.m_avAssets[m_AssetsEditorState.m_Type];
	int &MainAssetIndex = m_AssetsEditorState.m_aMainAssetIndex[m_AssetsEditorState.m_Type];
	int &DonorAssetIndex = m_AssetsEditorState.m_aDonorAssetIndex[m_AssetsEditorState.m_Type];

	if(vAssets.empty())
	{
		AssetsEditorReloadAssets();
		AssetsEditorResetPartSlots();
	}
	if(vAssets.empty())
	{
		Ui()->DoLabel(&ContentView, "No assets found.", FontSize, TEXTALIGN_MC);
		return;
	}

	MainAssetIndex = std::clamp(MainAssetIndex, 0, (int)vAssets.size() - 1);
	DonorAssetIndex = std::clamp(DonorAssetIndex, 0, (int)vAssets.size() - 1);

	std::vector<const char *> vAssetNames;
	vAssetNames.reserve(vAssets.size());
	for(const auto &Asset : vAssets)
		vAssetNames.push_back(Asset.m_aName);

	static CLineInput s_ExportNameInput;
	s_ExportNameInput.SetBuffer(m_AssetsEditorState.m_aExportName, sizeof(m_AssetsEditorState.m_aExportName));
	char aExportPlaceholder[64];
	str_format(aExportPlaceholder, sizeof(aExportPlaceholder), "my_%s", AssetsEditorTypeName(m_AssetsEditorState.m_Type));
	s_ExportNameInput.SetEmptyText(aExportPlaceholder);
	if(Ui()->DoEditBox(&s_ExportNameInput, &ExportRow, EditBoxFontSize))
	{
		m_AssetsEditorState.m_aExportNameTouchedByUser[m_AssetsEditorState.m_Type] = true;
		AssetsEditorCommitExportNameForType();
	}

	static CButtonContainer s_ReloadButton;
	if(DoButton_Menu(&s_ReloadButton, "Reload", 0, &ReloadButton))
	{
		AssetsEditorCancelDrag();
		AssetsEditorReloadAssetsImagesOnly();
	}

	static CButtonContainer s_ExportButton;
	if(DoButton_Menu(&s_ExportButton, "Export", 0, &ExportButton))
		AssetsEditorExport();

	static CButtonContainer s_ShowGridButton;
	if(DoButton_CheckBox(&s_ShowGridButton, "Show Grid", m_AssetsEditorState.m_ShowGrid, &GridToggleButton))
		m_AssetsEditorState.m_ShowGrid = !m_AssetsEditorState.m_ShowGrid;

	ContentView.HSplitTop(MarginSmall, nullptr, &ContentView);
	CUIRect LeftPanel, RightPanel;
	ContentView.VSplitMid(&LeftPanel, &RightPanel, MarginSmall);

	LeftPanel.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.06f), IGraphics::CORNER_ALL, 6.0f);
	RightPanel.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.06f), IGraphics::CORNER_ALL, 6.0f);
	LeftPanel.Margin(MarginSmall, &LeftPanel);
	RightPanel.Margin(MarginSmall, &RightPanel);

	const char *pMainName = vAssets[MainAssetIndex].m_aName;
	const float BottomBarHeight = LineSize * 2.0f + MarginExtraSmall;
	CUIRect LeftTitle, LeftCanvas, LeftBottom;
	LeftPanel.HSplitTop(LineSize, &LeftTitle, &LeftPanel);
	LeftPanel.HSplitTop(MarginExtraSmall, nullptr, &LeftPanel);
	LeftPanel.HSplitBottom(BottomBarHeight, &LeftCanvas, &LeftBottom);
	Ui()->DoLabel(&LeftTitle, "Donor (drag parts from left)", FontSize, TEXTALIGN_ML);

	CUIRect RightTitle, RightCanvas, RightBottom;
	RightPanel.HSplitTop(LineSize, &RightTitle, &RightPanel);
	RightPanel.HSplitTop(MarginExtraSmall, nullptr, &RightPanel);
	RightPanel.HSplitBottom(BottomBarHeight, &RightCanvas, &RightBottom);
	Ui()->DoLabel(&RightTitle, "Frankenstein (drop parts on right)", FontSize, TEXTALIGN_ML);

	AssetsEditorUpdatePreviewIfDirty();

	m_AssetsEditorState.m_HoveredDonorSlotIndex = -1;
	m_AssetsEditorState.m_HoveredTargetSlotIndex = -1;

	const SAssetsEditorAssetEntry &DonorAsset = vAssets[DonorAssetIndex];
	CUIRect DonorFittedRect;
	const bool HasDonorFitted = AssetsEditorCalcFittedRect(LeftCanvas, DonorAsset.m_PreviewWidth, DonorAsset.m_PreviewHeight, DonorFittedRect);
	CUIRect TargetFittedRect;
	const bool HasTargetFitted = AssetsEditorCalcFittedRect(RightCanvas, m_AssetsEditorState.m_ComposedPreviewWidth, m_AssetsEditorState.m_ComposedPreviewHeight, TargetFittedRect);
	const vec2 MousePos = Ui()->MousePos();
	const bool ClickedLmb = Ui()->MouseButtonClicked(0);
	const bool ClickedRmb = Ui()->MouseButtonClicked(1);

	if(HasDonorFitted)
		m_AssetsEditorState.m_HoveredDonorSlotIndex = AssetsEditorResolveHoveredSlotWithCycle(DonorFittedRect, m_AssetsEditorState.m_Type, m_AssetsEditorState.m_vPartSlots, MousePos, ClickedLmb, -1);
	if(HasTargetFitted)
		m_AssetsEditorState.m_HoveredTargetSlotIndex = AssetsEditorResolveHoveredSlotWithCycle(TargetFittedRect, m_AssetsEditorState.m_Type, m_AssetsEditorState.m_vPartSlots, MousePos, false, m_AssetsEditorState.m_ActiveDraggedSlotIndex);

	if(!m_AssetsEditorState.m_ShowExitConfirm && !m_AssetsEditorState.m_DragActive && ClickedRmb && m_AssetsEditorState.m_HoveredTargetSlotIndex >= 0)
	{
		auto ResetSlotToMain = [&](int SlotIndex) {
			if(SlotIndex < 0 || SlotIndex >= (int)m_AssetsEditorState.m_vPartSlots.size())
				return;
			SAssetsEditorPartSlot &Slot = m_AssetsEditorState.m_vPartSlots[SlotIndex];
			str_copy(Slot.m_aSourceAsset, pMainName);
			Slot.m_SourceSpriteId = Slot.m_SpriteId;
			Slot.m_SrcX = Slot.m_DstX;
			Slot.m_SrcY = Slot.m_DstY;
			Slot.m_SrcW = Slot.m_DstW;
			Slot.m_SrcH = Slot.m_DstH;
		};
		ResetSlotToMain(m_AssetsEditorState.m_HoveredTargetSlotIndex);
		AssetsEditorCancelDrag();
		m_AssetsEditorState.m_DirtyPreview = true;
		m_AssetsEditorState.m_HasUnsavedChanges = true;
		str_copy(m_AssetsEditorState.m_aStatusMessage, "Part reset to main asset.");
		m_AssetsEditorState.m_StatusIsError = false;
	}

	const bool SingleCandidateUnderCursor = m_AssetsEditorState.m_vHoverCycleCandidates.size() <= 1;
	const bool StartDragNow = Ui()->MouseButton(0) && (!ClickedLmb || SingleCandidateUnderCursor);
	if(!m_AssetsEditorState.m_ShowExitConfirm && !m_AssetsEditorState.m_DragActive && StartDragNow && m_AssetsEditorState.m_HoveredDonorSlotIndex >= 0)
	{
		m_AssetsEditorState.m_HoverCycleSlotIndex = -1;
		m_AssetsEditorState.m_HoverCyclePositionX = -1;
		m_AssetsEditorState.m_HoverCyclePositionY = -1;
		m_AssetsEditorState.m_HoverCycleCandidateCursor = 0;
		m_AssetsEditorState.m_vHoverCycleCandidates.clear();
		m_AssetsEditorState.m_DragActive = true;
		m_AssetsEditorState.m_ActiveDraggedSlotIndex = m_AssetsEditorState.m_HoveredDonorSlotIndex;
		str_copy(m_AssetsEditorState.m_aDraggedSourceAsset, DonorAsset.m_aName);
	}

	bool DropIsValid = false;
	if(m_AssetsEditorState.m_DragActive && m_AssetsEditorState.m_HoveredTargetSlotIndex >= 0 &&
		m_AssetsEditorState.m_ActiveDraggedSlotIndex >= 0 &&
		m_AssetsEditorState.m_ActiveDraggedSlotIndex < (int)m_AssetsEditorState.m_vPartSlots.size())
	{
		const SAssetsEditorPartSlot &DraggedSlot = m_AssetsEditorState.m_vPartSlots[m_AssetsEditorState.m_ActiveDraggedSlotIndex];
		const SAssetsEditorPartSlot &HoveredTarget = m_AssetsEditorState.m_vPartSlots[m_AssetsEditorState.m_HoveredTargetSlotIndex];
		DropIsValid = !m_AssetsEditorState.m_ApplySameSize || AssetsEditorSlotSameNormalizedSize(DraggedSlot, HoveredTarget);
	}

	const bool ReleasedLmb = !Ui()->MouseButton(0) && Ui()->LastMouseButton(0);
	if(m_AssetsEditorState.m_DragActive && ReleasedLmb)
	{
		if(m_AssetsEditorState.m_HoveredTargetSlotIndex >= 0 && DropIsValid)
			AssetsEditorApplyDrop(m_AssetsEditorState.m_HoveredTargetSlotIndex, m_AssetsEditorState.m_aDraggedSourceAsset, m_AssetsEditorState.m_ActiveDraggedSlotIndex, m_AssetsEditorState.m_ApplySameSize);
		else if(m_AssetsEditorState.m_HoveredTargetSlotIndex >= 0 && m_AssetsEditorState.m_ApplySameSize)
		{
			str_copy(m_AssetsEditorState.m_aStatusMessage, "Can only drop onto same-size parts.");
			m_AssetsEditorState.m_StatusIsError = true;
		}
		AssetsEditorCancelDrag();
	}

	const int DonorHighlightSlot = m_AssetsEditorState.m_DragActive ? m_AssetsEditorState.m_ActiveDraggedSlotIndex : m_AssetsEditorState.m_HoveredDonorSlotIndex;
	const int TargetHighlightSlot = m_AssetsEditorState.m_HoveredTargetSlotIndex;
	AssetsEditorRenderCanvas(LeftCanvas, DonorAsset.m_PreviewTexture, DonorAsset.m_PreviewWidth, DonorAsset.m_PreviewHeight, m_AssetsEditorState.m_Type, m_AssetsEditorState.m_ShowGrid, DonorHighlightSlot);
	AssetsEditorRenderCanvas(RightCanvas, m_AssetsEditorState.m_ComposedPreviewTexture, m_AssetsEditorState.m_ComposedPreviewWidth, m_AssetsEditorState.m_ComposedPreviewHeight, m_AssetsEditorState.m_Type, m_AssetsEditorState.m_ShowGrid, TargetHighlightSlot);

	if(m_AssetsEditorState.m_DragActive && m_AssetsEditorState.m_ActiveDraggedSlotIndex >= 0 &&
		m_AssetsEditorState.m_ActiveDraggedSlotIndex < (int)m_AssetsEditorState.m_vPartSlots.size() && HasDonorFitted)
	{
		CUIRect SourceSlotRect;
		if(AssetsEditorGetSlotRectInFitted(DonorFittedRect, m_AssetsEditorState.m_Type, m_AssetsEditorState.m_vPartSlots[m_AssetsEditorState.m_ActiveDraggedSlotIndex], SourceSlotRect))
		{
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.28f);
			IGraphics::CQuadItem Quad(SourceSlotRect.x, SourceSlotRect.y, SourceSlotRect.w, SourceSlotRect.h);
			Graphics()->QuadsDrawTL(&Quad, 1);
			Graphics()->QuadsEnd();
		}
	}

	if(m_AssetsEditorState.m_DragActive && m_AssetsEditorState.m_HoveredTargetSlotIndex >= 0 && HasTargetFitted)
	{
		CUIRect HoverRect;
		if(AssetsEditorGetSlotRectInFitted(TargetFittedRect, m_AssetsEditorState.m_Type, m_AssetsEditorState.m_vPartSlots[m_AssetsEditorState.m_HoveredTargetSlotIndex], HoverRect))
		{
			Graphics()->TextureClear();
			Graphics()->LinesBegin();
			if(DropIsValid)
				Graphics()->SetColor(0.35f, 1.0f, 0.35f, 0.95f);
			else
				Graphics()->SetColor(1.0f, 0.35f, 0.35f, 0.95f);
			IGraphics::CLineItem aLines[4] = {
				IGraphics::CLineItem(HoverRect.x, HoverRect.y, HoverRect.x + HoverRect.w, HoverRect.y),
				IGraphics::CLineItem(HoverRect.x + HoverRect.w, HoverRect.y, HoverRect.x + HoverRect.w, HoverRect.y + HoverRect.h),
				IGraphics::CLineItem(HoverRect.x + HoverRect.w, HoverRect.y + HoverRect.h, HoverRect.x, HoverRect.y + HoverRect.h),
				IGraphics::CLineItem(HoverRect.x, HoverRect.y + HoverRect.h, HoverRect.x, HoverRect.y),
			};
			Graphics()->LinesDraw(aLines, 4);
			Graphics()->LinesEnd();
		}
	}

	if(m_AssetsEditorState.m_DragActive && m_AssetsEditorState.m_ActiveDraggedSlotIndex >= 0 &&
		m_AssetsEditorState.m_ActiveDraggedSlotIndex < (int)m_AssetsEditorState.m_vPartSlots.size())
	{
		const SAssetsEditorPartSlot &DraggedSlot = m_AssetsEditorState.m_vPartSlots[m_AssetsEditorState.m_ActiveDraggedSlotIndex];
		const int SpriteId = DraggedSlot.m_SpriteId;
		const char *pSpriteName = SpriteId >= 0 ? g_pData->m_aSprites[SpriteId].m_pName :
												(DraggedSlot.m_aFamilyKey[0] != '\0' ? DraggedSlot.m_aFamilyKey : "tile");
		CUIRect DragSprite;
		DragSprite.x = Ui()->MouseX() + 12.0f;
		DragSprite.y = Ui()->MouseY() + 12.0f;
		DragSprite.w = 54.0f;
		DragSprite.h = 54.0f;
		if(DraggedSlot.m_SrcW > 0 && DraggedSlot.m_SrcH > 0)
		{
			const float Ratio = (float)DraggedSlot.m_SrcH / maximum((float)DraggedSlot.m_SrcW, 0.001f);
			float DrawW = 54.0f;
			float DrawH = DrawW * Ratio;
			if(DrawH > 54.0f)
			{
				DrawH = 54.0f;
				DrawW = DrawH / maximum(Ratio, 0.001f);
			}
			DragSprite.x += (54.0f - DrawW) * 0.5f;
			DragSprite.y += (54.0f - DrawH) * 0.5f;
			DragSprite.w = DrawW;
			DragSprite.h = DrawH;
		}
		CUIRect DragFrame;
		DragFrame.x = Ui()->MouseX() + 8.0f;
		DragFrame.y = Ui()->MouseY() + 8.0f;
		DragFrame.w = 62.0f;
		DragFrame.h = 62.0f;
		const float OldDragX = DragFrame.x;
		const float OldDragY = DragFrame.y;
		DragFrame.x = std::clamp(DragFrame.x, EditorRect.x, EditorRect.x + EditorRect.w - DragFrame.w);
		DragFrame.y = std::clamp(DragFrame.y, EditorRect.y, EditorRect.y + EditorRect.h - DragFrame.h);
		DragSprite.x += DragFrame.x - OldDragX;
		DragSprite.y += DragFrame.y - OldDragY;
		DragFrame.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.55f), IGraphics::CORNER_ALL, 4.0f);
		AssetsEditorDrawSlotFromTexture(DragSprite, DonorAsset.m_PreviewTexture, DraggedSlot, m_AssetsEditorState.m_Type, 0.95f, Graphics());

		CUIRect DragHint;
		DragHint.x = DragFrame.x + DragFrame.w + 6.0f;
		DragHint.y = DragFrame.y + 21.0f;
		DragHint.w = 210.0f;
		DragHint.h = LineSize;
		DragHint.x = std::clamp(DragHint.x, EditorRect.x, EditorRect.x + EditorRect.w - DragHint.w);
		DragHint.y = std::clamp(DragHint.y, EditorRect.y, EditorRect.y + EditorRect.h - DragHint.h);
		DragHint.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.65f), IGraphics::CORNER_ALL, 4.0f);
		char aDragText[128];
		str_format(aDragText, sizeof(aDragText), "%s from %s", pSpriteName, m_AssetsEditorState.m_aDraggedSourceAsset);
		Ui()->DoLabel(&DragHint, aDragText, FontSize * 0.9f, TEXTALIGN_MC);
	}

	CUIRect LeftBottomRow1, LeftBottomRow2;
	LeftBottom.HSplitTop(LineSize, &LeftBottomRow1, &LeftBottomRow2);
	LeftBottomRow2.HSplitTop(MarginExtraSmall, nullptr, &LeftBottomRow2);

	if(!m_AssetsEditorState.m_DragActive && LeftBottomRow1.h > 0.0f && m_AssetsEditorState.m_vHoverCycleCandidates.size() > 1)
	{
		char aCycleInfo[96];
		str_format(aCycleInfo, sizeof(aCycleInfo), "Click again to cycle parts (%d options).", (int)m_AssetsEditorState.m_vHoverCycleCandidates.size());
		Ui()->DoLabel(&LeftBottomRow1, aCycleInfo, FontSize * 0.9f, TEXTALIGN_ML);
	}

	CUIRect DonorLabel, DonorDropDown;
	SplitLeftSafe(LeftBottomRow2, minimum(90.0f, LeftBottomRow2.w * 0.58f), &DonorLabel, &DonorDropDown);
	Ui()->DoLabel(&DonorLabel, "Donor Asset", FontSize, TEXTALIGN_ML);
	static CUi::SDropDownState s_DonorDropDownState[ASSETS_EDITOR_TYPE_COUNT];
	static CScrollRegion s_DonorDropDownScrollRegion[ASSETS_EDITOR_TYPE_COUNT];
	s_DonorDropDownState[m_AssetsEditorState.m_Type].m_SelectionPopupContext.m_pScrollRegion = &s_DonorDropDownScrollRegion[m_AssetsEditorState.m_Type];
	const int NewDonorAssetIndex = Ui()->DoDropDown(&DonorDropDown, DonorAssetIndex, vAssetNames.data(), vAssetNames.size(), s_DonorDropDownState[m_AssetsEditorState.m_Type]);
	if(NewDonorAssetIndex != DonorAssetIndex)
	{
		DonorAssetIndex = NewDonorAssetIndex;
		AssetsEditorCancelDrag();
	}

	CUIRect ControlsRow = RightBottom;
	ControlsRow.HSplitTop(LineSize, nullptr, &ControlsRow);
	ControlsRow.HSplitTop(MarginExtraSmall, nullptr, &ControlsRow);
	CUIRect BottomMainRow, ResetAllButton;
	const float ResetButtonWidth = minimum(120.0f, maximum(90.0f, ControlsRow.w * 0.30f));
	SplitRightSafe(ControlsRow, ResetButtonWidth, &BottomMainRow, &ResetAllButton);
	if(BottomMainRow.w > MarginSmall)
		SplitRightSafe(BottomMainRow, MarginSmall, &BottomMainRow, nullptr);
	CUIRect BottomMainLabel, BottomMainDropDown;
	SplitLeftSafe(BottomMainRow, minimum(90.0f, BottomMainRow.w * 0.58f), &BottomMainLabel, &BottomMainDropDown);
	Ui()->DoLabel(&BottomMainLabel, "Main Asset", FontSize, TEXTALIGN_ML);
	static CUi::SDropDownState s_BottomMainDropDownState[ASSETS_EDITOR_TYPE_COUNT];
	static CScrollRegion s_BottomMainDropDownScrollRegion[ASSETS_EDITOR_TYPE_COUNT];
	s_BottomMainDropDownState[m_AssetsEditorState.m_Type].m_SelectionPopupContext.m_pScrollRegion = &s_BottomMainDropDownScrollRegion[m_AssetsEditorState.m_Type];
	const int NewMainAssetIndexBottom = Ui()->DoDropDown(&BottomMainDropDown, MainAssetIndex, vAssetNames.data(), vAssetNames.size(), s_BottomMainDropDownState[m_AssetsEditorState.m_Type]);
	if(NewMainAssetIndexBottom != MainAssetIndex)
	{
		AssetsEditorCancelDrag();
		MainAssetIndex = NewMainAssetIndexBottom;
		m_AssetsEditorState.m_ShowExitConfirm = false;
		AssetsEditorResetPartSlots();
	}
	const char *pHintMessage = m_AssetsEditorState.m_DragActive ? "Drop on right canvas to replace one part." : "Drag from left to right. Right-click a Frankenstein part to reset it.";
	static CButtonContainer s_ResetAllPartsButton;
	if(DoButton_Menu(&s_ResetAllPartsButton, "Reset All", 0, &ResetAllButton))
	{
		for(auto &Slot : m_AssetsEditorState.m_vPartSlots)
		{
			str_copy(Slot.m_aSourceAsset, pMainName);
			Slot.m_SourceSpriteId = Slot.m_SpriteId;
			Slot.m_SrcX = Slot.m_DstX;
			Slot.m_SrcY = Slot.m_DstY;
			Slot.m_SrcW = Slot.m_DstW;
			Slot.m_SrcH = Slot.m_DstH;
		}
		AssetsEditorCancelDrag();
		m_AssetsEditorState.m_DirtyPreview = true;
		m_AssetsEditorState.m_HasUnsavedChanges = true;
	}

	CUIRect StatusLeft, StatusRight;
	StatusRect.VSplitRight(minimum(360.0f, StatusRect.w * 0.42f), &StatusLeft, &StatusRight);
	if(m_AssetsEditorState.m_aStatusMessage[0] != '\0')
	{
		if(m_AssetsEditorState.m_StatusIsError)
			TextRender()->TextColor(1.0f, 0.45f, 0.45f, 1.0f);
		else
			TextRender()->TextColor(0.55f, 1.0f, 0.55f, 1.0f);
		Ui()->DoLabel(&StatusLeft, m_AssetsEditorState.m_aStatusMessage, FontSize * 0.95f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
	else
	{
		Ui()->DoLabel(&StatusLeft, "", FontSize * 0.95f, TEXTALIGN_ML);
	}
	Ui()->DoLabel(&StatusRight, pHintMessage, FontSize * 0.95f, TEXTALIGN_MR);
}
