#include <base/log.h>
#include <base/math.h>
#include <base/system.h>
#include <base/types.h>

#include <engine/graphics.h>
#include <engine/gfx/image_loader.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/shared/config.h>
#include <engine/shared/linereader.h>
#include <engine/shared/localization.h>
#include <engine/shared/protocol7.h>
#include <engine/storage.h>
#include <engine/textrender.h>
#include <engine/updater.h>

#include <generated/client_data.h>

#include <SDL.h>

#include <game/client/animstate.h>
#include <game/client/bc_ui_animations.h>
#include <game/client/components/binds.h>
#include <game/client/components/chat.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/menus.h>
#include <game/client/components/skins.h>
#include <game/client/components/sounds.h>
#include <game/client/components/bestclient/audio_capture_device_score.h>
#include <game/client/components/tclient/bindchat.h>
#include <game/client/components/bestclient/bindsystem.h>
#include <game/client/gameclient.h>
#include <game/client/lineinput.h>
#include <game/client/render.h>
#include <game/client/skin.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <unordered_map>
#include <vector>

enum
{
	BestClient_TAB_VISUALS = 0,
	BestClient_TAB_RACEFEATURES,
	BestClient_TAB_GORESFEATURES,
	BestClient_TAB_FUN,
	BestClient_TAB_SAVESYSTEM,
	BestClient_TAB_SHOP,
	BestClient_TAB_VOICE,
	BestClient_TAB_INFO,
	NUMBER_OF_BestClient_TABS
};

typedef struct
{
	const char *m_pName;
	const char *m_pCommand;
	int m_KeyId;
	int m_ModifierCombination;
} CKeyInfo;

using namespace FontIcons;

static float s_Time = 0.0f;
static bool s_StartedTime = false;

const float FontSize = 14.0f;
const float EditBoxFontSize = 12.0f;
const float LineSize = 20.0f;
const float ColorPickerLineSize = 25.0f;
const float HeadlineFontSize = 20.0f;
const float StandardFontSize = 14.0f;

const float HeadlineHeight = HeadlineFontSize + 0.0f;
const float Margin = 10.0f;
const float MarginSmall = 5.0f;
const float MarginExtraSmall = 2.5f;
const float MarginBetweenSections = 30.0f;
const float MarginBetweenViews = 30.0f;
const float MenuPreviewTeeLarge = 64.0f;
const float MenuPreviewTeeCompact = 36.0f;

const float ColorPickerLabelSize = 13.0f;
const float ColorPickerLineSpacing = 5.0f;

static void SetFlag(int32_t &Flags, int n, bool Value)
{
	if(Value)
		Flags |= (1 << n);
	else
		Flags &= ~(1 << n);
}

static bool IsFlagSet(int32_t Flags, int n)
{
	return (Flags & (1 << n)) != 0;
}

enum
{
	COMPONENTS_GROUP_VISUALS = 0,
	COMPONENTS_GROUP_RACE,
	COMPONENTS_GROUP_GORES,
	NUM_COMPONENTS_GROUPS,
};

struct SBestClientComponentEntry
{
	CBestClient::EBestClientComponent m_Component;
	const char *m_pName;
	int m_Group;
};

static const SBestClientComponentEntry gs_aBestClientComponentEntries[] = {
	{CBestClient::COMPONENT_VISUALS_MUSIC_PLAYER, "Music Player", COMPONENTS_GROUP_VISUALS},
#if defined(CONF_BESTCLIENT_CAVA)
	{CBestClient::COMPONENT_VISUALS_CAVA, "Cava", COMPONENTS_GROUP_VISUALS},
#endif
	{CBestClient::COMPONENT_VISUALS_SWEAT_WEAPON, "Sweat Weapon", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_MEDIA_BACKGROUND, "Media Background", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_PET, "Pet", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_MAGIC_PARTICLES, "Magic Particles", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_ORBIT_AURA, "Orbit Aura", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_OPTIMIZER, "Optimizer", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_ANIMATIONS, "Animations", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_CAMERA_DRIFT, "Camera Drift", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_DYNAMIC_FOV, "Dynamic FOV", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_AFTERIMAGE, "Afterimage", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_FOCUS_MODE, "Focus Mode", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_CHAT_BUBBLES, "Chat Bubbles", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_3D_PARTICLES, "3D Particles", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_ASPECT_RATIO, "Aspect Ratio", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_RACE_BINDSYSTEM, "BindSystem", COMPONENTS_GROUP_RACE},
	{CBestClient::COMPONENT_RACE_SPEEDRUN_TIMER, "Speedrun Timer", COMPONENTS_GROUP_RACE},
	{CBestClient::COMPONENT_RACE_AUTO_TEAM_LOCK, "Auto Team Lock", COMPONENTS_GROUP_RACE},
	{CBestClient::COMPONENT_GORES_INPUT, "Input", COMPONENTS_GROUP_GORES},
	{CBestClient::COMPONENT_GORES_GORES_MODE, "Gores Mode", COMPONENTS_GROUP_GORES},
	{CBestClient::COMPONENT_GORES_HOOK_COMBO, "Hook Combo", COMPONENTS_GROUP_GORES},
};

static bool ComponentsEditorIsDisabled(int Component, int MaskLo, int MaskHi)
{
	return CBestClient::IsComponentDisabledByMask(Component, MaskLo, MaskHi);
}

static void ComponentsEditorSetDisabled(int Component, int &MaskLo, int &MaskHi, bool Disabled)
{
	if(Component < 0 || Component >= CBestClient::NUM_COMPONENTS_EDITOR_COMPONENTS)
		return;

	int *pMask = &MaskLo;
	int Bit = Component;
	if(Component >= 31)
	{
		pMask = &MaskHi;
		Bit = Component - 31;
	}

	if(Disabled)
		*pMask |= (1 << Bit);
	else
		*pMask &= ~(1 << Bit);
}

static bool BestClientTabAutoHiddenByComponents(const CBestClient &BestClient, int Tab)
{
	switch(Tab)
	{
	case BestClient_TAB_RACEFEATURES:
		return BestClient.IsComponentDisabled(CBestClient::COMPONENT_RACE_BINDSYSTEM) &&
			BestClient.IsComponentDisabled(CBestClient::COMPONENT_RACE_SPEEDRUN_TIMER) &&
			BestClient.IsComponentDisabled(CBestClient::COMPONENT_RACE_AUTO_TEAM_LOCK);
	default:
		return false;
	}
}

struct SScopedClip
{
	CUi *m_pUi;
	~SScopedClip() { m_pUi->ClipDisable(); }
};

void CMenus::SettingsSearchJumpToBestClient(int Tab, int VisualsSubTab, int GoresCategory)
{
	if(Tab >= 0 && Tab < NUMBER_OF_BestClient_TABS)
		m_BestClientSettingsTab = Tab;
	if(VisualsSubTab >= 0 && VisualsSubTab < BESTCLIENT_VISUALS_SUBTAB_LENGTH)
		m_AssetsEditorState.m_VisualsSubTab = VisualsSubTab;
	if(GoresCategory >= 0)
		m_BestClientGoresFeaturesCategory = GoresCategory;
}

void CMenus::OpenWorldEditorSettings()
{
	SetShowStart(false);
	SetMenuPage(PAGE_SETTINGS);
	SetActive(true);
	g_Config.m_UiSettingsPage = SETTINGS_BESTCLIENT;
	m_OldSettingsPage = OLD_SETTINGS_BESTCLIENT;
	m_BestClientSettingsTab = BestClient_TAB_VISUALS;
	m_AssetsEditorState.m_VisualsSubTab = BESTCLIENT_VISUALS_SUBTAB_WORLD_EDITOR;
}

void CMenus::CloseWorldEditorSettings()
{
	if(IsWorldEditorSettingsOpen())
		m_AssetsEditorState.m_VisualsSubTab = BESTCLIENT_VISUALS_SUBTAB_EDITORS;
}

bool CMenus::IsWorldEditorSettingsOpen() const
{
	return IsActive() &&
		m_MenuPage == PAGE_SETTINGS &&
		g_Config.m_UiSettingsPage == SETTINGS_BESTCLIENT &&
		m_BestClientSettingsTab == BestClient_TAB_VISUALS &&
		m_AssetsEditorState.m_VisualsSubTab == BESTCLIENT_VISUALS_SUBTAB_WORLD_EDITOR;
}

bool CMenus::IsBestClientFunTabActive() const
{
	return g_Config.m_UiSettingsPage == SETTINGS_BESTCLIENT && m_BestClientSettingsTab == BestClient_TAB_FUN;
}

struct SAssetsEditorPartDef
{
	int m_SpriteId;
	int m_Group;
};

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

struct SAssetsEditorScanContext
{
	std::vector<CMenus::SAssetsEditorAssetEntry> *m_pAssets;
	IGraphics *m_pGraphics;
	IStorage *m_pStorage;
	const char *m_pAssetType;
	int m_Type;
};

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

struct SAssetsEditorImageCacheEntry
{
	int m_Type = CMenus::ASSETS_EDITOR_TYPE_GAME;
	char m_aName[64] = {0};
	CImageInfo m_Image;
};

static std::vector<SAssetsEditorImageCacheEntry> gs_vAssetsEditorImageCache;

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

bool CMenus::DoLine_KeyReader(CUIRect &View, CButtonContainer &ReaderButton, CButtonContainer &ClearButton, const char *pName, const char *pCommand)
{
	CBindSlot Bind(0, 0);
	for(int Mod = 0; Mod < KeyModifier::COMBINATION_COUNT; Mod++)
	{
		for(int KeyId = 0; KeyId < KEY_LAST; KeyId++)
		{
			const char *pBind = GameClient()->m_Binds.Get(KeyId, Mod);
			if(!pBind[0])
				continue;

			if(str_comp(pBind, pCommand) == 0)
			{
				Bind.m_Key = KeyId;
				Bind.m_ModifierMask = Mod;
				break;
			}
		}
	}

	CUIRect KeyButton, KeyLabel;
	View.HSplitTop(LineSize, &KeyButton, &View);
	KeyButton.VSplitMid(&KeyLabel, &KeyButton);

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "%s:", pName);
	Ui()->DoLabel(&KeyLabel, aBuf, FontSize, TEXTALIGN_ML);

	View.HSplitTop(MarginExtraSmall, nullptr, &View);

	const auto Result = GameClient()->m_KeyBinder.DoKeyReader(&ReaderButton, &ClearButton, &KeyButton, Bind, false);
	if(Result.m_Bind != Bind)
	{
		if(Bind.m_Key != KEY_UNKNOWN)
			GameClient()->m_Binds.Bind(Bind.m_Key, "", false, Bind.m_ModifierMask);
		if(Result.m_Bind.m_Key != KEY_UNKNOWN)
			GameClient()->m_Binds.Bind(Result.m_Bind.m_Key, pCommand, false, Result.m_Bind.m_ModifierMask);
		return true;
	}
	return false;
}

bool CMenus::DoSliderWithScaledValue(const void *pId, int *pOption, const CUIRect *pRect, const char *pStr, int Min, int Max, int Scale, const IScrollbarScale *pScale, unsigned Flags, const char *pSuffix)
{
	const bool NoClampValue = Flags & CUi::SCROLLBAR_OPTION_NOCLAMPVALUE;

	int Value = *pOption;
	Min /= Scale;
	Max /= Scale;
	// Allow adjustment of slider options when ctrl is pressed (to avoid scrolling, or accidentally adjusting the value)
	int Increment = std::max(1, (Max - Min) / 35);
	if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_UP) && Ui()->MouseInside(pRect))
	{
		Value += Increment;
		Value = std::clamp(Value, Min, Max);
	}
	if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN) && Ui()->MouseInside(pRect))
	{
		Value -= Increment;
		Value = std::clamp(Value, Min, Max);
	}

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "%s: %i%s", pStr, Value * Scale, pSuffix);

	if(NoClampValue)
	{
		// Clamp the value internally for the scrollbar
		Value = std::clamp(Value, Min, Max);
	}

	CUIRect Label, ScrollBar;
	pRect->VSplitMid(&Label, &ScrollBar, minimum(10.0f, pRect->w * 0.05f));

	const float LabelFontSize = Label.h * CUi::ms_FontmodHeight * 0.8f;
	Ui()->DoLabel(&Label, aBuf, LabelFontSize, TEXTALIGN_ML);

	Value = pScale->ToAbsolute(Ui()->DoScrollbarH(pId, &ScrollBar, pScale->ToRelative(Value, Min, Max)), Min, Max);
	if(NoClampValue && ((Value == Min && *pOption < Min) || (Value == Max && *pOption > Max)))
	{
		Value = *pOption;
	}

	if(*pOption != Value)
	{
		*pOption = Value;
		return true;
	}
	return false;
}

bool CMenus::DoEditBoxWithLabel(CLineInput *LineInput, const CUIRect *pRect, const char *pLabel, const char *pDefault, char *pBuf, size_t BufSize)
{
	CUIRect Button, Label;
	pRect->VSplitLeft(210.0f, &Label, &Button);
	Ui()->DoLabel(&Label, pLabel, FontSize, TEXTALIGN_ML);
	LineInput->SetBuffer(pBuf, BufSize);
	LineInput->SetEmptyText(pDefault);
	return Ui()->DoEditBox(LineInput, &Button, EditBoxFontSize);
}

int CMenus::DoButtonLineSize_Menu(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, float ButtonLineSize, bool Fake, const char *pImageName, int Corners, float Rounding, float FontFactor, ColorRGBA Color)
{
	CUIRect Text = *pRect;

	if(Checked)
		Color = ColorRGBA(0.6f, 0.6f, 0.6f, 0.5f);
	Color.a *= Ui()->ButtonColorMul(pButtonContainer);

	if(Fake)
		Color.a *= 0.5f;

	pRect->Draw(Color, Corners, Rounding);

	Text.HMargin((Text.h - ButtonLineSize) / 2.0f, &Text);
	Text.HMargin(pRect->h >= 20.0f ? 2.0f : 1.0f, &Text);
	Text.HMargin((Text.h * FontFactor) / 2.0f, &Text);
	Ui()->DoLabel(&Text, pText, Text.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);

	if(Fake)
		return 0;

	return Ui()->DoButtonLogic(pButtonContainer, Checked, pRect, BUTTONFLAG_LEFT);
}

void CMenus::RenderDevSkin(vec2 RenderPos, float Size, const char *pSkinName, const char *pBackupSkin, bool CustomColors, int FeetColor, int BodyColor, int Emote, bool Rainbow, bool Cute, ColorRGBA ColorFeet, ColorRGBA ColorBody)
{
	float DefTick = std::fmod(s_Time, 1.0f);

	CTeeRenderInfo SkinInfo;
	const CSkin *pSkin = GameClient()->m_Skins.Find(pSkinName);
	if(str_comp(pSkin->GetName(), pSkinName) != 0)
		pSkin = GameClient()->m_Skins.Find(pBackupSkin);

	SkinInfo.m_OriginalRenderSkin = pSkin->m_OriginalSkin;
	SkinInfo.m_ColorableRenderSkin = pSkin->m_ColorableSkin;
	SkinInfo.m_SkinMetrics = pSkin->m_Metrics;
	SkinInfo.m_CustomColoredSkin = CustomColors;
	if(SkinInfo.m_CustomColoredSkin)
	{
		SkinInfo.m_ColorBody = color_cast<ColorRGBA>(ColorHSLA(BodyColor).UnclampLighting(ColorHSLA::DARKEST_LGT));
		SkinInfo.m_ColorFeet = color_cast<ColorRGBA>(ColorHSLA(FeetColor).UnclampLighting(ColorHSLA::DARKEST_LGT));
		if(ColorFeet.a != 0.0f)
		{
			SkinInfo.m_ColorBody = ColorBody;
			SkinInfo.m_ColorFeet = ColorFeet;
		}
	}
	else
	{
		SkinInfo.m_ColorBody = ColorRGBA(1.0f, 1.0f, 1.0f);
		SkinInfo.m_ColorFeet = ColorRGBA(1.0f, 1.0f, 1.0f);
	}
	if(Rainbow)
	{
		ColorRGBA Col = color_cast<ColorRGBA>(ColorHSLA(DefTick, 1.0f, 0.5f));
		SkinInfo.m_ColorBody = Col;
		SkinInfo.m_ColorFeet = Col;
	}
	SkinInfo.m_Size = Size;
	const CAnimState *pIdleState = CAnimState::GetIdle();
	vec2 OffsetToMid;
	CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &SkinInfo, OffsetToMid);
	vec2 TeeRenderPos(RenderPos.x, RenderPos.y + OffsetToMid.y);
	if(Cute)
		RenderTeeCute(pIdleState, &SkinInfo, Emote, vec2(1.0f, 0.0f), TeeRenderPos, true);
	else
		RenderTools()->RenderTee(pIdleState, &SkinInfo, Emote, vec2(1.0f, 0.0f), TeeRenderPos);
}

void CMenus::RenderTeeCute(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, int Emote, vec2 Dir, vec2 Pos, bool CuteEyes, float Alpha)
{
	Dir = Ui()->MousePos() - Pos;
	if(pInfo->m_Size > 0.0f)
		Dir /= pInfo->m_Size;
	const float Length = length(Dir);
	if(Length > 1.0f)
		Dir /= Length;
	if(CuteEyes && Length < 0.4f)
		Emote = 2;
	RenderTools()->RenderTee(pAnim, pInfo, Emote, Dir, Pos, Alpha);
}

void CMenus::RenderFontIcon(const CUIRect Rect, const char *pText, float Size, int Align)
{
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
	Ui()->DoLabel(&Rect, pText, Size, Align);
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
}

int CMenus::DoButtonNoRect_FontIcon(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, int Corners)
{
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
	TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
	TextRender()->TextColor(TextRender()->DefaultTextSelectionColor());
	if(Ui()->HotItem() == pButtonContainer)
	{
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
	CUIRect Temp;
	pRect->HMargin(0.0f, &Temp);
	Ui()->DoLabel(&Temp, pText, Temp.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

	return Ui()->DoButtonLogic(pButtonContainer, Checked, pRect, BUTTONFLAG_LEFT);
}

void CMenus::PopupConfirmRemoveWarType()
{
	GameClient()->m_WarList.RemoveWarType(m_pRemoveWarType->m_aWarName);
	m_pRemoveWarType = nullptr;
}

static bool BestClientPageAnimationsEnabled()
{
	return false;
}

static float BestClientPageAnimationDuration()
{
	return 0.0f;
}

static bool ModuleUiRevealAnimationsEnabled()
{
	return BCUiAnimations::Enabled() && g_Config.m_BcModuleUiRevealAnimation != 0;
}

static float ModuleUiRevealAnimationDuration()
{
	return BCUiAnimations::MsToSeconds(g_Config.m_BcModuleUiRevealAnimationMs);
}

void CMenus::RenderSettingsBestClientPageByTab(int Tab, CUIRect MainView)
{
	if(Tab == BestClient_TAB_VISUALS)
		RenderSettingsBestClientVisuals(MainView);
	if(Tab == BestClient_TAB_RACEFEATURES)
		RenderSettingsBestClientRaceFeatures(MainView);
	if(Tab == BestClient_TAB_GORESFEATURES)
		RenderSettingsBestClientGoresFeatures(MainView);
	if(Tab == BestClient_TAB_FUN)
		RenderSettingsBestClientFun(MainView);
	if(Tab == BestClient_TAB_SHOP)
		RenderSettingsBestClientShop(MainView);
	if(Tab == BestClient_TAB_VOICE)
		RenderSettingsBestClientVoice(MainView);
	if(Tab == BestClient_TAB_INFO)
		RenderSettingsBestClientInfo(MainView);
}

void CMenus::RenderSettingsBestClient(CUIRect MainView)
{
	s_Time += Client()->RenderFrameTime() * (1.0f / 100.0f);
	if(!s_StartedTime)
	{
		s_StartedTime = true;
		s_Time = (float)rand() / (float)RAND_MAX;
	}

	if(m_AssetsEditorState.m_VisualsEditorOpen && m_AssetsEditorState.m_FullscreenOpen)
	{
		m_AssetsEditorState.m_VisualsSubTab = 1;
		RenderAssetsEditorScreen(MainView);
		return;
	}

	if(m_ComponentsEditorState.m_Open && m_ComponentsEditorState.m_FullscreenOpen)
	{
		RenderComponentsEditorScreen(MainView);
		return;
	}

	int &CurCustomTab = m_BestClientSettingsTab;

	CUIRect TabBar, Button;
	int TabCount = 0;
	int FirstVisibleTab = -1;
	const auto &BestClient = GameClient()->m_BestClient;
	auto IsTabHidden = [&](int Tab) {
		if(Tab == BestClient_TAB_SAVESYSTEM)
			return true;
		// Keep "Info" always visible, even if the config flag is set manually.
		return BestClientTabAutoHiddenByComponents(BestClient, Tab) ||
			(Tab != BestClient_TAB_INFO && IsFlagSet(g_Config.m_BcBestClientSettingsTabs, Tab));
	};
	for(int Tab = 0; Tab < NUMBER_OF_BestClient_TABS; ++Tab)
	{
		if(IsTabHidden(Tab))
			continue;
		if(FirstVisibleTab == -1)
			FirstVisibleTab = Tab;
		++TabCount;
	}

	if(FirstVisibleTab == -1)
	{
		CurCustomTab = BestClient_TAB_VISUALS;
		return;
	}
	if(CurCustomTab < 0 || CurCustomTab >= NUMBER_OF_BestClient_TABS || IsTabHidden(CurCustomTab))
		CurCustomTab = FirstVisibleTab;
	MainView.HSplitTop(LineSize, &TabBar, &MainView);

	if(m_BestClientPageTransition.m_Current < 0 || IsTabHidden(m_BestClientPageTransition.m_Current))
	{
		m_BestClientPageTransition.m_Current = CurCustomTab;
		m_BestClientPageTransition.m_Previous = CurCustomTab;
		m_BestClientPageTransition.m_Phase = 1.0f;
	}

	const float TabWidth = TabCount > 0 ? TabBar.w / TabCount : TabBar.w;
	static CButtonContainer s_aPageTabs[NUMBER_OF_BestClient_TABS] = {};
	const char *apTabNames[] = {
		TCLocalize("Visuals"),
		"Gameplay",
		TCLocalize("Others"),
		TCLocalize("Fun"),
		"",
		TCLocalize("Texture Shop"),
		TCLocalize("Voice"),
		TCLocalize("Info")};

	int VisibleTabIndex = 0;
	for(int Tab = 0; Tab < NUMBER_OF_BestClient_TABS; ++Tab)
	{
		if(IsTabHidden(Tab))
			continue;

		TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
		int Corners = IGraphics::CORNER_NONE;
		if(VisibleTabIndex == 0)
			Corners |= IGraphics::CORNER_L;
		if(VisibleTabIndex == TabCount - 1)
			Corners |= IGraphics::CORNER_R;
		if(DoButton_MenuTab(&s_aPageTabs[Tab], apTabNames[Tab], CurCustomTab == Tab, &Button, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
		{
			if(CurCustomTab != Tab)
			{
				m_BestClientPageTransition.m_Previous = CurCustomTab;
				m_BestClientPageTransition.m_Current = Tab;
				m_BestClientPageTransition.m_Phase = BestClientPageAnimationsEnabled() ? 0.0f : 1.0f;
			}
			CurCustomTab = Tab;
		}

		++VisibleTabIndex;
	}

	MainView.HSplitTop(Margin, nullptr, &MainView);

	if(m_BestClientPageTransition.m_Current != CurCustomTab)
	{
		m_BestClientPageTransition.m_Previous = m_BestClientPageTransition.m_Current;
		m_BestClientPageTransition.m_Current = CurCustomTab;
		m_BestClientPageTransition.m_Phase = BestClientPageAnimationsEnabled() ? 0.0f : 1.0f;
	}

	const bool PageAnimating = BestClientPageAnimationsEnabled() && m_BestClientPageTransition.m_Previous != m_BestClientPageTransition.m_Current && m_BestClientPageTransition.m_Phase < 1.0f;
	if(BestClientPageAnimationsEnabled())
		BCUiAnimations::UpdatePhase(m_BestClientPageTransition.m_Phase, 1.0f, Client()->RenderFrameTime(), BestClientPageAnimationDuration());
	else
		m_BestClientPageTransition.m_Phase = 1.0f;

	if(m_BestClientPageTransition.m_Phase >= 1.0f)
		m_BestClientPageTransition.m_Previous = m_BestClientPageTransition.m_Current;

	const bool NeedsVisualsVisible = CurCustomTab == BestClient_TAB_VISUALS || (PageAnimating && m_BestClientPageTransition.m_Previous == BestClient_TAB_VISUALS);
	if(!NeedsVisualsVisible && (m_AssetsEditorState.m_VisualsEditorOpen || m_AssetsEditorState.m_VisualsEditorInitialized))
	{
		m_AssetsEditorState.m_VisualsEditorOpen = false;
		AssetsEditorClearAssets();
	}

	if(PageAnimating)
	{
		const float Ease = BCUiAnimations::EaseInOutQuad(m_BestClientPageTransition.m_Phase);
		const float Offset = (1.0f - Ease) * minimum(MainView.w * 0.16f, 52.0f);
		const bool WasEnabled = Ui()->Enabled();
		Ui()->ClipEnable(&MainView);
		SScopedClip ClipGuard{Ui()};

		CUIRect PrevView = MainView;
		CUIRect CurrentView = MainView;
		const int Direction = m_BestClientPageTransition.m_Current > m_BestClientPageTransition.m_Previous ? 1 : -1;
		PrevView.x -= Direction * Offset;
		CurrentView.x += Direction * (minimum(MainView.w * 0.16f, 52.0f) - Offset);

		Ui()->SetEnabled(false);
		RenderSettingsBestClientPageByTab(m_BestClientPageTransition.m_Previous, PrevView);
		Ui()->SetEnabled(WasEnabled);
		RenderSettingsBestClientPageByTab(m_BestClientPageTransition.m_Current, CurrentView);
		Ui()->SetEnabled(WasEnabled);
	}
	else
	{
		RenderSettingsBestClientPageByTab(CurCustomTab, MainView);
	}
}

void CMenus::RenderSettingsBestClientGoresFeatures(CUIRect MainView)
{
	CUIRect Column, Button, Label;

	static CScrollRegion s_ScrollRegion;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	s_ScrollRegion.Begin(&MainView, &ScrollOffset, &ScrollParams);
	m_pSettingsSearchActiveScrollRegion = &s_ScrollRegion;

	MainView.y += ScrollOffset.y;
	MainView.VSplitRight(5.0f, &MainView, nullptr);
	MainView.VSplitLeft(5.0f, nullptr, &MainView);
	Column = MainView;

	// ***** Input ***** //
	if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_GORES_INPUT))
	{
		Column.HSplitTop(Margin, nullptr, &Column);
		Column.HSplitTop(HeadlineHeight, &Label, &Column);
		if(SettingsSearchConsumeReveal("Input"))
			s_ScrollRegion.AddRect(Label, true);
		SettingsSearchDrawLabel(&Label, TCLocalize("Input"), HeadlineFontSize, TEXTALIGN_ML, "Input");
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFastInput, TCLocalize("Fast Input"), &g_Config.m_TcFastInput, &Column, LineSize);
		{
			static float s_FastInputPhase = 0.0f;
			const float Dt = Client()->RenderFrameTime();
			const bool Enabled = g_Config.m_TcFastInput != 0;
			if(ModuleUiRevealAnimationsEnabled())
				BCUiAnimations::UpdatePhase(s_FastInputPhase, Enabled ? 1.0f : 0.0f, Dt, ModuleUiRevealAnimationDuration());
			else
				s_FastInputPhase = Enabled ? 1.0f : 0.0f;

			const int RowCount = g_Config.m_BcFastInputMode == 1 ? 4 : 3;
			const float TargetHeight = MarginSmall * (RowCount + 1) + LineSize * RowCount;
			const float CurHeight = TargetHeight * s_FastInputPhase;
			if(CurHeight > 0.0f)
			{
				CUIRect Visible;
				Column.HSplitTop(CurHeight, &Visible, &Column);
				Ui()->ClipEnable(&Visible);
				SScopedClip ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, TargetHeight};
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);

				static CButtonContainer s_FastInputModeFast;
				static CButtonContainer s_FastInputModeLowDelta;
				const int OldMode = g_Config.m_BcFastInputMode;

				Expand.HSplitTop(LineSize, &Button, &Expand);
				{
					CUIRect Left, Right;
					Button.VSplitMid(&Left, &Right, 2.0f);
					Left.HMargin(2.0f, &Left);
					Right.HMargin(2.0f, &Right);

					if(DoButton_Menu(&s_FastInputModeFast, TCLocalize("Fast input"), g_Config.m_BcFastInputMode == 0, &Left, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
						g_Config.m_BcFastInputMode = 0;
					if(DoButton_Menu(&s_FastInputModeLowDelta, TCLocalize("Low delta"), g_Config.m_BcFastInputMode == 1, &Right, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
						g_Config.m_BcFastInputMode = 1;
				}

				if(g_Config.m_BcFastInputMode != OldMode)
				{
					if(g_Config.m_BcFastInputMode == 1 && g_Config.m_BcFastInputLowDelta <= 0 && g_Config.m_TcFastInputAmount > 0)
						g_Config.m_BcFastInputLowDelta = std::clamp(g_Config.m_TcFastInputAmount * 5, 0, 500);
					if(g_Config.m_BcFastInputMode == 0 && g_Config.m_TcFastInputAmount <= 0 && g_Config.m_BcFastInputLowDelta > 0)
						g_Config.m_TcFastInputAmount = std::clamp((g_Config.m_BcFastInputLowDelta + 2) / 5, 0, 40);
				}

				Expand.HSplitTop(MarginSmall, nullptr, &Expand);
				Expand.HSplitTop(LineSize, &Button, &Expand);
				if(g_Config.m_BcFastInputMode == 0)
				{
					DoSliderWithScaledValue(&g_Config.m_TcFastInputAmount, &g_Config.m_TcFastInputAmount, &Button, TCLocalize("Amount"), 0, 40, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
				}
				else
				{
					const int Min = 0;
					const int Max = 500;
					int Value = std::clamp(g_Config.m_BcFastInputLowDelta, Min, Max);

					const int Increment = std::max(1, (Max - Min) / 35);
					if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_UP) && Ui()->MouseInside(&Button))
						Value = std::clamp(Value + Increment, Min, Max);
					if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN) && Ui()->MouseInside(&Button))
						Value = std::clamp(Value - Increment, Min, Max);

					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "%s: %.2ft", TCLocalize("Amount"), Value / 100.0f);

					CUIRect AmountLabel, ScrollBar;
					Button.VSplitMid(&AmountLabel, &ScrollBar, minimum(10.0f, Button.w * 0.05f));
					const float LabelFontSize = AmountLabel.h * CUi::ms_FontmodHeight * 0.8f;
					Ui()->DoLabel(&AmountLabel, aBuf, LabelFontSize, TEXTALIGN_ML);

					const float Rel = (Value - Min) / (float)(Max - Min);
					const float NewRel = Ui()->DoScrollbarH(&g_Config.m_BcFastInputLowDelta, &ScrollBar, Rel);
					Value = (int)(Min + NewRel * (Max - Min) + 0.5f);
					g_Config.m_BcFastInputLowDelta = std::clamp(Value, Min, Max);

					Expand.HSplitTop(MarginSmall, nullptr, &Expand);
					static CButtonContainer s_EcoFreezeChimera;
					static CButtonContainer s_TrueLowDelta;

					const int EcoFreezeChimeraValue = 80;
					const int TrueLowDeltaValue = 130;

					Expand.HSplitTop(LineSize, &Button, &Expand);
					{
						CUIRect Left, Right;
						Button.VSplitMid(&Left, &Right, 2.0f);
						Left.HMargin(2.0f, &Left);
						Right.HMargin(2.0f, &Right);

						if(DoButton_Menu(&s_EcoFreezeChimera, TCLocalize("eco-freeze chimera"), g_Config.m_BcFastInputLowDelta == EcoFreezeChimeraValue, &Left, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
						{
							char aCmd[64];
							str_format(aCmd, sizeof(aCmd), "bc_fast_input_low_delta \"%d\"", EcoFreezeChimeraValue);
							Console()->ExecuteLine(aCmd, IConsole::CLIENT_ID_UNSPECIFIED);
						}
						if(DoButton_Menu(&s_TrueLowDelta, TCLocalize("true low delta"), g_Config.m_BcFastInputLowDelta == TrueLowDeltaValue, &Right, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
						{
							char aCmd[64];
							str_format(aCmd, sizeof(aCmd), "bc_fast_input_low_delta \"%d\"", TrueLowDeltaValue);
							Console()->ExecuteLine(aCmd, IConsole::CLIENT_ID_UNSPECIFIED);
						}
					}
				}

				Expand.HSplitTop(MarginSmall, nullptr, &Expand);
				if(g_Config.m_BcFastInputMode == 0)
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFastInputOthers, TCLocalize("fast input others"), &g_Config.m_TcFastInputOthers, &Expand, LineSize);
				else
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcLowDeltaOthers, TCLocalize("low delta others"), &g_Config.m_BcLowDeltaOthers, &Expand, LineSize);
			}
		}
	}

	// ***** Gores Mode ***** //
	if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_GORES_GORES_MODE))
	{
		Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		Column.HSplitTop(HeadlineHeight, &Label, &Column);
		if(SettingsSearchConsumeReveal("Gores Mode"))
			s_ScrollRegion.AddRect(Label, true);
		SettingsSearchDrawLabel(&Label, TCLocalize("Gores Mode"), HeadlineFontSize, TEXTALIGN_ML, "Gores Mode");
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcGoresMode, TCLocalize("Enable gores mode"), &g_Config.m_BcGoresMode, &Column, LineSize);

		static float s_GoresModePhase = 0.0f;
		const float Dt = Client()->RenderFrameTime();
		const bool Enabled = g_Config.m_BcGoresMode != 0;
		if(ModuleUiRevealAnimationsEnabled())
			BCUiAnimations::UpdatePhase(s_GoresModePhase, Enabled ? 1.0f : 0.0f, Dt, ModuleUiRevealAnimationDuration());
		else
			s_GoresModePhase = Enabled ? 1.0f : 0.0f;

		const float CurHeight = LineSize * s_GoresModePhase;
		if(CurHeight > 0.0f)
		{
			CUIRect Visible;
			Column.HSplitTop(CurHeight, &Visible, &Column);
			Ui()->ClipEnable(&Visible);
			SScopedClip ClipGuard{Ui()};
			CUIRect Expand = {Visible.x, Visible.y, Visible.w, LineSize};
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcGoresModeDisableIfWeapons, TCLocalize("Disable if you have shotgun, grenade or laser"), &g_Config.m_BcGoresModeDisableIfWeapons, &Expand, LineSize);
		}
	}

	// ***** Hook Combo ***** //
	if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_GORES_HOOK_COMBO))
	{
		Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		Column.HSplitTop(HeadlineHeight, &Label, &Column);
		if(SettingsSearchConsumeReveal("Hook Combo"))
			s_ScrollRegion.AddRect(Label, true);
		SettingsSearchDrawLabel(&Label, TCLocalize("Hook Combo"), HeadlineFontSize, TEXTALIGN_ML, "Hook Combo");
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcHookCombo, TCLocalize("Enable hook combo effect"), &g_Config.m_BcHookCombo, &Column, LineSize);
		static float s_HookComboPhase = 0.0f;
		const float Dt = Client()->RenderFrameTime();
		const bool Enabled = g_Config.m_BcHookCombo != 0;
		if(ModuleUiRevealAnimationsEnabled())
			BCUiAnimations::UpdatePhase(s_HookComboPhase, Enabled ? 1.0f : 0.0f, Dt, ModuleUiRevealAnimationDuration());
		else
			s_HookComboPhase = Enabled ? 1.0f : 0.0f;

		const float TargetHeight = 4.0f * LineSize;
		const float CurHeight = TargetHeight * s_HookComboPhase;
		if(CurHeight > 0.0f)
		{
			CUIRect Visible;
			Column.HSplitTop(CurHeight, &Visible, &Column);
			Ui()->ClipEnable(&Visible);
			SScopedClip ClipGuard{Ui()};

			CUIRect Expand = {Visible.x, Visible.y, Visible.w, TargetHeight};
			Expand.HSplitTop(LineSize, &Label, &Expand);
			Ui()->DoLabel(&Label, TCLocalize("Mode"), FontSize, TEXTALIGN_ML);

			CUIRect ModeRow, HookButton, HammerButton, HookHammerButton;
			Expand.HSplitTop(LineSize, &ModeRow, &Expand);
			const float ThirdWidth = ModeRow.w / 3.0f;
			ModeRow.VSplitLeft(ThirdWidth, &HookButton, &ModeRow);
			ModeRow.VSplitLeft(ThirdWidth, &HammerButton, &HookHammerButton);

			static CButtonContainer s_HookComboModeHook;
			static CButtonContainer s_HookComboModeHammer;
			static CButtonContainer s_HookComboModeHookHammer;
			if(DoButton_MenuTab(&s_HookComboModeHook, TCLocalize("Hook"), g_Config.m_BcHookComboMode == 0, &HookButton, IGraphics::CORNER_L, nullptr, nullptr, nullptr, nullptr, 4.0f))
				g_Config.m_BcHookComboMode = 0;
			if(DoButton_MenuTab(&s_HookComboModeHammer, TCLocalize("Hammer"), g_Config.m_BcHookComboMode == 1, &HammerButton, IGraphics::CORNER_NONE, nullptr, nullptr, nullptr, nullptr, 4.0f))
				g_Config.m_BcHookComboMode = 1;
			if(DoButton_MenuTab(&s_HookComboModeHookHammer, TCLocalize("Hook&Hammer"), g_Config.m_BcHookComboMode == 2, &HookHammerButton, IGraphics::CORNER_R, nullptr, nullptr, nullptr, nullptr, 4.0f))
				g_Config.m_BcHookComboMode = 2;

			Expand.HSplitTop(LineSize, &Button, &Expand);
			DoSliderWithScaledValue(&g_Config.m_BcHookComboResetTime, &g_Config.m_BcHookComboResetTime, &Button, TCLocalize("Max time between hooks"), 100, 5000, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
			Expand.HSplitTop(LineSize, &Button, &Expand);
			DoSliderWithScaledValue(&g_Config.m_BcHookComboSoundVolume, &g_Config.m_BcHookComboSoundVolume, &Button, TCLocalize("Hook combo sound volume"), 0, 100, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "%");
			Expand.HSplitTop(LineSize, &Button, &Expand);
			DoSliderWithScaledValue(&g_Config.m_BcHookComboSize, &g_Config.m_BcHookComboSize, &Button, TCLocalize("Hook combo size"), 50, 200, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "%");
		}
	}

	CUIRect ScrollRegion;
	ScrollRegion.x = MainView.x;
	ScrollRegion.y = Column.y + MarginSmall * 2.0f;
	ScrollRegion.w = MainView.w;
	ScrollRegion.h = 0.0f;
	s_ScrollRegion.AddRect(ScrollRegion);
	s_ScrollRegion.End();
	m_pSettingsSearchActiveScrollRegion = nullptr;
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

		// Include 0.7 game ninja bar parts that are used by game skin loading, but
		// are not present in the 0.6 sprite table.
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

	// Keep a stable local copy. Cache vector can reallocate while donor assets are loaded.
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

void CMenus::RenderSettingsBestClientVisualsPage(int Tab, CUIRect MainView)
{
	if(Tab == BESTCLIENT_VISUALS_SUBTAB_GENERAL)
		RenderSettingsBestClientVisualsGeneral(MainView);
	else if(Tab == BESTCLIENT_VISUALS_SUBTAB_EDITORS)
		RenderSettingsBestClientVisualsEditors(MainView);
	else
		RenderSettingsBestClientWorldEditor(MainView);
}

void CMenus::RenderSettingsBestClientVisuals(CUIRect MainView)
{
	CUIRect TabBar, Button;
	MainView.HSplitTop(LineSize + 2.0f, &TabBar, &MainView);
	const float TabWidth = TabBar.w / (float)BESTCLIENT_VISUALS_SUBTAB_LENGTH;
	const char *apTabs[] = {TCLocalize("General"), TCLocalize("Editors"), TCLocalize("World Editor")};
	static CButtonContainer s_aVisualTabs[BESTCLIENT_VISUALS_SUBTAB_LENGTH];
	if(m_BestClientVisualsTransition.m_Current < 0)
	{
		m_BestClientVisualsTransition.m_Current = m_AssetsEditorState.m_VisualsSubTab;
		m_BestClientVisualsTransition.m_Previous = m_AssetsEditorState.m_VisualsSubTab;
		m_BestClientVisualsTransition.m_Phase = 1.0f;
	}

	for(int Tab = 0; Tab < BESTCLIENT_VISUALS_SUBTAB_LENGTH; ++Tab)
	{
		TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
		int Corners = IGraphics::CORNER_NONE;
		if(Tab == 0)
			Corners = IGraphics::CORNER_L;
		else if(Tab == BESTCLIENT_VISUALS_SUBTAB_LENGTH - 1)
			Corners = IGraphics::CORNER_R;
		if(DoButton_MenuTab(&s_aVisualTabs[Tab], apTabs[Tab], m_AssetsEditorState.m_VisualsSubTab == Tab, &Button, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
		{
			if(m_AssetsEditorState.m_VisualsSubTab != Tab)
			{
				m_BestClientVisualsTransition.m_Previous = m_AssetsEditorState.m_VisualsSubTab;
				m_BestClientVisualsTransition.m_Current = Tab;
				m_BestClientVisualsTransition.m_Phase = BestClientPageAnimationsEnabled() ? 0.0f : 1.0f;
			}
			m_AssetsEditorState.m_VisualsSubTab = Tab;
		}
	}

	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	if(m_BestClientVisualsTransition.m_Current != m_AssetsEditorState.m_VisualsSubTab)
	{
		m_BestClientVisualsTransition.m_Previous = m_BestClientVisualsTransition.m_Current;
		m_BestClientVisualsTransition.m_Current = m_AssetsEditorState.m_VisualsSubTab;
		m_BestClientVisualsTransition.m_Phase = BestClientPageAnimationsEnabled() ? 0.0f : 1.0f;
	}

	if(BestClientPageAnimationsEnabled())
		BCUiAnimations::UpdatePhase(m_BestClientVisualsTransition.m_Phase, 1.0f, Client()->RenderFrameTime(), BestClientPageAnimationDuration());
	else
		m_BestClientVisualsTransition.m_Phase = 1.0f;

	if(m_BestClientVisualsTransition.m_Phase >= 1.0f)
		m_BestClientVisualsTransition.m_Previous = m_BestClientVisualsTransition.m_Current;

	const bool SubPageAnimating = BestClientPageAnimationsEnabled() && m_BestClientVisualsTransition.m_Previous != m_BestClientVisualsTransition.m_Current && m_BestClientVisualsTransition.m_Phase < 1.0f;
	const bool EditorsVisible = m_AssetsEditorState.m_VisualsSubTab == BESTCLIENT_VISUALS_SUBTAB_EDITORS ||
		(SubPageAnimating && m_BestClientVisualsTransition.m_Previous == BESTCLIENT_VISUALS_SUBTAB_EDITORS);
	if(!EditorsVisible && (m_AssetsEditorState.m_VisualsEditorOpen || m_AssetsEditorState.m_VisualsEditorInitialized))
	{
		m_AssetsEditorState.m_VisualsEditorOpen = false;
		AssetsEditorClearAssets();
	}

	if(SubPageAnimating)
	{
		const float SlideMax = minimum(MainView.w * 0.12f, 40.0f);
		const float Ease = BCUiAnimations::EaseInOutQuad(m_BestClientVisualsTransition.m_Phase);
		const float Offset = (1.0f - Ease) * SlideMax;
		const int Direction = m_BestClientVisualsTransition.m_Current > m_BestClientVisualsTransition.m_Previous ? 1 : -1;
		const bool WasEnabled = Ui()->Enabled();
		CUIRect PrevView = MainView;
		CUIRect CurrentView = MainView;
		PrevView.x -= Direction * Offset;
		CurrentView.x += Direction * (SlideMax - Offset);
		Ui()->ClipEnable(&MainView);
		SScopedClip ClipGuard{Ui()};
		Ui()->SetEnabled(false);
		RenderSettingsBestClientVisualsPage(m_BestClientVisualsTransition.m_Previous, PrevView);
		Ui()->SetEnabled(WasEnabled);
		RenderSettingsBestClientVisualsPage(m_BestClientVisualsTransition.m_Current, CurrentView);
		Ui()->SetEnabled(WasEnabled);
	}
	else
		RenderSettingsBestClientVisualsPage(m_AssetsEditorState.m_VisualsSubTab, MainView);
}

void CMenus::RenderSettingsBestClientVisualsEditors(CUIRect MainView)
{
	CUIRect Label, Button;
	if(m_AssetsEditorState.m_VisualsEditorOpen)
	{
		RenderAssetsEditorScreen(MainView);
		return;
	}

	MainView.HSplitTop(HeadlineHeight, &Label, &MainView);
	Ui()->DoLabel(&Label, TCLocalize("Editors"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	MainView.HSplitTop(LineSize, &Label, &MainView);
	Ui()->DoLabel(&Label, "Create Frankenstein assets from Game, Emoticons, Entities, HUD, Particles and Extras.", FontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	MainView.HSplitTop(LineSize + 4.0f, &Button, &MainView);
	static CButtonContainer s_OpenAssetsEditor;
	if(DoButton_Menu(&s_OpenAssetsEditor, "Assets editor", 0, &Button))
	{
		m_AssetsEditorState.m_VisualsEditorOpen = true;
		m_AssetsEditorState.m_FullscreenOpen = true;
		if(!m_AssetsEditorState.m_VisualsEditorInitialized)
		{
			AssetsEditorReloadAssets();
			AssetsEditorResetPartSlots();
			m_AssetsEditorState.m_VisualsEditorInitialized = true;
		}
	}

	MainView.HSplitTop(MarginBetweenSections, nullptr, &MainView);
	MainView.HSplitTop(LineSize, &Label, &MainView);
	Ui()->DoLabel(&Label, TCLocalize("Choose which BestClient components should be available after restart."), FontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	MainView.HSplitTop(LineSize + 4.0f, &Button, &MainView);
	static CButtonContainer s_OpenComponentsEditorButton;
	if(DoButton_Menu(&s_OpenComponentsEditorButton, TCLocalize("Components editor"), 0, &Button))
		ComponentsEditorOpen();

	MainView.HSplitTop(MarginBetweenSections, nullptr, &MainView);
	MainView.HSplitTop(LineSize, &Label, &MainView);
	Ui()->DoLabel(&Label, TCLocalize("Open the name plate settings and preview."), FontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	MainView.HSplitTop(LineSize + 4.0f, &Button, &MainView);
	static CButtonContainer s_OpenNameplateEditor;
	if(DoButton_Menu(&s_OpenNameplateEditor, TCLocalize("Nameplate editor"), 0, &Button))
	{
		g_Config.m_UiSettingsPage = SETTINGS_APPEARANCE_TAB;
		m_AppearanceSubTab = APPEARANCE_SUBTAB_APPEARANCE;
		m_AppearanceTab = APPEARANCE_TAB_NAME_PLATE;
	}

	MainView.HSplitTop(MarginBetweenSections, nullptr, &MainView);
	MainView.HSplitTop(LineSize, &Label, &MainView);
	Ui()->DoLabel(&Label, TCLocalize("Open a dedicated settings page for World Editor effects."), FontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	MainView.HSplitTop(LineSize + 4.0f, &Button, &MainView);
	static CButtonContainer s_OpenWorldEditor;
	if(DoButton_Menu(&s_OpenWorldEditor, TCLocalize("World Editor"), 0, &Button))
		OpenWorldEditorSettings();
}

void CMenus::RenderSettingsBestClientWorldEditor(CUIRect MainView)
{
	GameClient()->m_WorldEditor.RenderSettingsPage(MainView);
}

void CMenus::ComponentsEditorSyncFromConfig()
{
	m_ComponentsEditorState.m_AppliedMaskLo = g_Config.m_BcDisabledComponentsMaskLo;
	m_ComponentsEditorState.m_AppliedMaskHi = g_Config.m_BcDisabledComponentsMaskHi;
	m_ComponentsEditorState.m_StagedMaskLo = m_ComponentsEditorState.m_AppliedMaskLo;
	m_ComponentsEditorState.m_StagedMaskHi = m_ComponentsEditorState.m_AppliedMaskHi;
	m_ComponentsEditorState.m_HasUnsavedChanges = false;
}

void CMenus::ComponentsEditorOpen()
{
	ComponentsEditorSyncFromConfig();
	m_ComponentsEditorState.m_Open = true;
	m_ComponentsEditorState.m_FullscreenOpen = true;
	m_ComponentsEditorState.m_ShowExitConfirm = false;
	m_ComponentsEditorState.m_ShowRestartConfirm = false;
}

void CMenus::ComponentsEditorRequestClose()
{
	if(m_ComponentsEditorState.m_HasUnsavedChanges)
	{
		m_ComponentsEditorState.m_ShowExitConfirm = true;
		return;
	}
	ComponentsEditorCloseNow();
}

void CMenus::ComponentsEditorCloseNow()
{
	m_ComponentsEditorState.m_Open = false;
	m_ComponentsEditorState.m_ShowExitConfirm = false;
	m_ComponentsEditorState.m_ShowRestartConfirm = false;
	m_ComponentsEditorState.m_HasUnsavedChanges = false;
	ComponentsEditorSyncFromConfig();
}

void CMenus::ComponentsEditorApply()
{
	g_Config.m_BcDisabledComponentsMaskLo = m_ComponentsEditorState.m_StagedMaskLo;
	g_Config.m_BcDisabledComponentsMaskHi = m_ComponentsEditorState.m_StagedMaskHi;
	m_ComponentsEditorState.m_AppliedMaskLo = m_ComponentsEditorState.m_StagedMaskLo;
	m_ComponentsEditorState.m_AppliedMaskHi = m_ComponentsEditorState.m_StagedMaskHi;
	m_ComponentsEditorState.m_HasUnsavedChanges = false;
	m_ComponentsEditorState.m_ShowExitConfirm = false;
	m_ComponentsEditorState.m_ShowRestartConfirm = true;
}

void CMenus::ComponentsEditorRenderExitConfirm(const CUIRect &Rect)
{
	CUIRect Overlay = Rect;
	Overlay.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.6f), IGraphics::CORNER_ALL, 0.0f);

	CUIRect Box;
	Box.w = minimum(520.0f, Rect.w - 30.0f);
	Box.h = 130.0f;
	Box.x = Rect.x + (Rect.w - Box.w) * 0.5f;
	Box.y = Rect.y + (Rect.h - Box.h) * 0.5f;
	Box.Draw(ColorRGBA(0.1f, 0.1f, 0.1f, 0.95f), IGraphics::CORNER_ALL, 8.0f);

	CUIRect Title, Message, Buttons;
	Box.Margin(10.0f, &Box);
	Box.HSplitTop(LineSize + 4.0f, &Title, &Box);
	Box.HSplitTop(LineSize, &Message, &Box);
	Box.HSplitBottom(LineSize + 4.0f, &Box, &Buttons);

	Ui()->DoLabel(&Title, TCLocalize("Cancel all changes?"), FontSize * 1.1f, TEXTALIGN_ML);
	Ui()->DoLabel(&Message, TCLocalize("All staged component changes will be lost."), FontSize, TEXTALIGN_ML);

	CUIRect YesButton, NoButton;
	Buttons.VSplitMid(&YesButton, &NoButton, MarginSmall);
	static CButtonContainer s_YesButton;
	static CButtonContainer s_NoButton;
	if(DoButton_Menu(&s_YesButton, Localize("Yes"), 0, &YesButton))
		ComponentsEditorCloseNow();
	if(DoButton_Menu(&s_NoButton, Localize("No"), 0, &NoButton) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
		m_ComponentsEditorState.m_ShowExitConfirm = false;
}

void CMenus::ComponentsEditorRenderRestartConfirm(const CUIRect &Rect)
{
	CUIRect Overlay = Rect;
	Overlay.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.6f), IGraphics::CORNER_ALL, 0.0f);

	CUIRect Box;
	Box.w = minimum(560.0f, Rect.w - 30.0f);
	Box.h = 140.0f;
	Box.x = Rect.x + (Rect.w - Box.w) * 0.5f;
	Box.y = Rect.y + (Rect.h - Box.h) * 0.5f;
	Box.Draw(ColorRGBA(0.1f, 0.1f, 0.1f, 0.95f), IGraphics::CORNER_ALL, 8.0f);

	CUIRect Title, Message, Buttons;
	Box.Margin(10.0f, &Box);
	Box.HSplitTop(LineSize + 4.0f, &Title, &Box);
	Box.HSplitTop(LineSize * 2.0f, &Message, &Box);
	Box.HSplitBottom(LineSize + 4.0f, &Box, &Buttons);

	Ui()->DoLabel(&Title, Localize("Restart"), FontSize * 1.1f, TEXTALIGN_ML);
	Ui()->DoLabel(&Message, TCLocalize("Restart client now so component changes fully apply?"), FontSize, TEXTALIGN_ML);

	CUIRect YesButton, NoButton;
	Buttons.VSplitMid(&YesButton, &NoButton, MarginSmall);
	static CButtonContainer s_RestartYesButton;
	static CButtonContainer s_RestartNoButton;
	if(DoButton_Menu(&s_RestartYesButton, Localize("Yes"), 0, &YesButton))
	{
		m_ComponentsEditorState.m_ShowRestartConfirm = false;
		m_ComponentsEditorState.m_Open = false;
		if(Client()->State() == IClient::STATE_ONLINE || GameClient()->Editor()->HasUnsavedData())
			m_Popup = POPUP_RESTART;
		else
			Client()->Restart();
	}
	if(DoButton_Menu(&s_RestartNoButton, Localize("No"), 0, &NoButton) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
	{
		m_ComponentsEditorState.m_ShowRestartConfirm = false;
		m_ComponentsEditorState.m_Open = false;
	}
}

void CMenus::RenderComponentsEditorScreen(CUIRect MainView)
{
	if(m_ComponentsEditorState.m_FullscreenOpen)
		MainView = *Ui()->Screen();

	if(!m_ComponentsEditorState.m_ShowExitConfirm && !m_ComponentsEditorState.m_ShowRestartConfirm && Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
	{
		ComponentsEditorRequestClose();
		if(!m_ComponentsEditorState.m_Open)
			return;
	}
	if(m_ComponentsEditorState.m_ShowExitConfirm)
	{
		ComponentsEditorRenderExitConfirm(*Ui()->Screen());
		return;
	}
	if(m_ComponentsEditorState.m_ShowRestartConfirm)
	{
		ComponentsEditorRenderRestartConfirm(*Ui()->Screen());
		return;
	}

	CUIRect EditorRect = MainView;
	EditorRect.Margin(8.0f, &EditorRect);
	EditorRect.Draw(ColorRGBA(0.10f, 0.11f, 0.15f, 0.96f), IGraphics::CORNER_ALL, 8.0f);

	CUIRect WorkRect;
	EditorRect.Margin(8.0f, &WorkRect);
	CUIRect Header, Content, Footer;
	WorkRect.HSplitTop(24.0f, &Header, &WorkRect);
	WorkRect.HSplitBottom(34.0f, &Content, &Footer);

	CUIRect HeaderText;
	HeaderText = Header;

	CUIRect CloseButtonArea, HeaderSpacer;
	HeaderText.VSplitLeft(14.0f, &CloseButtonArea, &HeaderText);
	HeaderText.VSplitLeft(6.0f, &HeaderSpacer, &HeaderText);

	CUIRect CloseButton;
	CloseButtonArea.HMargin(5.0f, &CloseButton);

	static CButtonContainer s_CloseButton;
	if(Ui()->DoButton_FontIcon(&s_CloseButton, FONT_ICON_XMARK, 0, &CloseButton, IGraphics::CORNER_ALL))
	{
		ComponentsEditorRequestClose();
		if(!m_ComponentsEditorState.m_Open)
			return;
	}

	Ui()->DoLabel(&HeaderText, TCLocalize("Components editor"), HeadlineFontSize, TEXTALIGN_ML);

	static CScrollRegion s_ScrollRegion;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	s_ScrollRegion.Begin(&Content, &ScrollOffset, &ScrollParams);

	CUIRect View = Content;
	View.y += ScrollOffset.y;
	View.VSplitLeft(5.0f, nullptr, &View);
	View.VSplitRight(5.0f, &View, nullptr);

	const char *apGroupNames[NUM_COMPONENTS_GROUPS] = {
		TCLocalize("Visuals"),
		"Gameplay",
		TCLocalize("Others"),
	};

	for(int Group = 0; Group < NUM_COMPONENTS_GROUPS; ++Group)
	{
		int Count = 0;
		for(const auto &Entry : gs_aBestClientComponentEntries)
		{
			if(Entry.m_Group == Group)
				++Count;
		}
		if(Count == 0)
			continue;

		CUIRect GroupBox;
		const float GroupHeight = 20.0f + HeadlineHeight + MarginSmall + Count * LineSize;
		View.HSplitTop(GroupHeight, &GroupBox, &View);
		GroupBox.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.22f), IGraphics::CORNER_ALL, 8.0f);
		GroupBox.Margin(10.0f, &GroupBox);

		CUIRect Label;
		GroupBox.HSplitTop(HeadlineHeight, &Label, &GroupBox);
		Ui()->DoLabel(&Label, apGroupNames[Group], HeadlineFontSize, TEXTALIGN_ML);
		GroupBox.HSplitTop(MarginSmall, nullptr, &GroupBox);

		for(const auto &Entry : gs_aBestClientComponentEntries)
		{
			if(Entry.m_Group != Group)
				continue;
			CUIRect Row;
			GroupBox.HSplitTop(LineSize, &Row, &GroupBox);
			int Disabled = ComponentsEditorIsDisabled((int)Entry.m_Component, m_ComponentsEditorState.m_StagedMaskLo, m_ComponentsEditorState.m_StagedMaskHi);
			if(DoButton_CheckBox(&Entry, TCLocalize(Entry.m_pName), Disabled, &Row))
			{
				Disabled ^= 1;
				ComponentsEditorSetDisabled((int)Entry.m_Component, m_ComponentsEditorState.m_StagedMaskLo, m_ComponentsEditorState.m_StagedMaskHi, Disabled != 0);
				m_ComponentsEditorState.m_HasUnsavedChanges =
					m_ComponentsEditorState.m_StagedMaskLo != m_ComponentsEditorState.m_AppliedMaskLo ||
					m_ComponentsEditorState.m_StagedMaskHi != m_ComponentsEditorState.m_AppliedMaskHi;
			}
		}

		View.HSplitTop(16.0f, nullptr, &View);
	}

	CUIRect ScrollRegion;
	ScrollRegion.x = Content.x;
	ScrollRegion.y = View.y;
	ScrollRegion.w = Content.w;
	ScrollRegion.h = 0.0f;
	s_ScrollRegion.AddRect(ScrollRegion);
	s_ScrollRegion.End();

	CUIRect Counter, ApplyButton;
	Footer.VSplitLeft(260.0f, &Counter, &Footer);
	Footer.VSplitRight(88.0f, &Footer, &ApplyButton);

	int DisabledCount = 0;
	for(const auto &Entry : gs_aBestClientComponentEntries)
	{
		if(ComponentsEditorIsDisabled((int)Entry.m_Component, m_ComponentsEditorState.m_StagedMaskLo, m_ComponentsEditorState.m_StagedMaskHi))
			++DisabledCount;
	}

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), TCLocalize("Disabled components: %d"), DisabledCount);
	Ui()->DoLabel(&Counter, aBuf, FontSize, TEXTALIGN_ML);

	static CButtonContainer s_ApplyButton;
	const int DisabledStyle = m_ComponentsEditorState.m_HasUnsavedChanges ? 0 : 1;
	if(DoButton_Menu(&s_ApplyButton, TCLocalize("Apply"), DisabledStyle, &ApplyButton) && m_ComponentsEditorState.m_HasUnsavedChanges)
		ComponentsEditorApply();
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
	{
		MainView = *Ui()->Screen();
	}

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
	EditorRect.Draw(ColorRGBA(0.10f, 0.11f, 0.15f, 0.92f), IGraphics::CORNER_ALL, 8.0f);
	Ui()->ClipEnable(&EditorRect);
	struct SScopedClip
	{
		CUi *m_pUi;
		~SScopedClip() { m_pUi->ClipDisable(); }
	} ClipGuard{Ui()};

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
	if(Ui()->DoButton_FontIcon(&s_CloseButton, FONT_ICON_XMARK, 0, &CloseButton, IGraphics::CORNER_ALL))
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
	if(DoButton_CheckBox(&s_ShowGridButton, "Show grid", m_AssetsEditorState.m_ShowGrid, &GridToggleButton))
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
	Ui()->DoLabel(&DonorLabel, "Donor asset", FontSize, TEXTALIGN_ML);
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
	Ui()->DoLabel(&BottomMainLabel, "Main asset", FontSize, TEXTALIGN_ML);
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
	if(DoButton_Menu(&s_ResetAllPartsButton, "Reset all", 0, &ResetAllButton))
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

void CMenus::RenderSettingsBestClientVisualsGeneral(CUIRect MainView)
{
	CUIRect Column, LeftView, RightView, Button, Label;

	static CScrollRegion s_ScrollRegion;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	s_ScrollRegion.Begin(&MainView, &ScrollOffset, &ScrollParams);
	m_pSettingsSearchActiveScrollRegion = &s_ScrollRegion;

	static std::vector<CUIRect> s_SectionBoxes;
	static vec2 s_PrevScrollOffset(0.0f, 0.0f);

	MainView.y += ScrollOffset.y;

	MainView.VSplitRight(5.0f, &MainView, nullptr); // Padding for scrollbar
	MainView.VSplitLeft(5.0f, nullptr, &MainView); // Padding for scrollbar

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	for(CUIRect &Section : s_SectionBoxes)
	{
		float Padding = MarginBetweenViews * 0.6666f;
		Section.w += Padding;
		Section.h += Padding;
		Section.x -= Padding * 0.5f;
		Section.y -= Padding * 0.5f;
		Section.y -= s_PrevScrollOffset.y - ScrollOffset.y;
		Section.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 10.0f);
	}
	s_PrevScrollOffset = ScrollOffset;
	s_SectionBoxes.clear();

	// ***** LeftView ***** //
	Column = LeftView;

	// ***** Visual ***** //
	Column.HSplitTop(Margin, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	if(SettingsSearchConsumeReveal("Visual"))
		s_ScrollRegion.AddRect(Label, true);
	SettingsSearchDrawLabel(&Label, TCLocalize("Visual"), HeadlineFontSize, TEXTALIGN_ML, "Visual");
	Column.HSplitTop(MarginSmall, nullptr, &Column);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcEmoticonShadow, TCLocalize("Shadow of Emotions"), &g_Config.m_BcEmoticonShadow, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatSaveDraft, TCLocalize("Save unsent messages"), &g_Config.m_BcChatSaveDraft, &Column, LineSize);
	static CButtonContainer s_SettingsLayoutButton;
	int UseNewMenuLayout = g_Config.m_BcSettingsLayout == 0 ? 1 : 0;
	DoButton_CheckBoxAutoVMarginAndSet(&s_SettingsLayoutButton, TCLocalize("Use new menu layout"), &UseNewMenuLayout, &Column, LineSize);
	g_Config.m_BcSettingsLayout = UseNewMenuLayout ? 0 : 1;

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// Music Player
	if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_MUSIC_PLAYER))
	{
		Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		s_SectionBoxes.push_back(Column);
		Column.HSplitTop(HeadlineHeight, &Label, &Column);
		if(SettingsSearchConsumeReveal("Music Player"))
			s_ScrollRegion.AddRect(Label, true);
		SettingsSearchDrawLabel(&Label, TCLocalize("Music Player"), HeadlineFontSize, TEXTALIGN_ML, "Music Player");
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMusicPlayer, TCLocalize("Enable music player"), &g_Config.m_BcMusicPlayer, &Column, LineSize);

		{
		static float s_MusicPlayerPhase = 0.0f;
		static float s_MusicPlayerStaticColorPhase = 0.0f;

		const float Dt = Client()->RenderFrameTime();
		const bool Enabled = g_Config.m_BcMusicPlayer != 0;
		const bool StaticColorOn = Enabled && g_Config.m_BcMusicPlayerColorMode == 0;
		if(ModuleUiRevealAnimationsEnabled())
		{
			BCUiAnimations::UpdatePhase(s_MusicPlayerPhase, Enabled ? 1.0f : 0.0f, Dt, ModuleUiRevealAnimationDuration());
			BCUiAnimations::UpdatePhase(s_MusicPlayerStaticColorPhase, StaticColorOn ? 1.0f : 0.0f, Dt, 0.16f);
		}
		else
		{
			s_MusicPlayerPhase = Enabled ? 1.0f : 0.0f;
			s_MusicPlayerStaticColorPhase = StaticColorOn ? 1.0f : 0.0f;
		}

		const float StaticColorTargetHeight = ColorPickerLineSize + ColorPickerLineSpacing;
		const float TargetHeight = LineSize + StaticColorTargetHeight * s_MusicPlayerStaticColorPhase;
		const float CurHeight = TargetHeight * s_MusicPlayerPhase;
		if(CurHeight > 0.0f)
		{
			CUIRect Visible;
			Column.HSplitTop(CurHeight, &Visible, &Column);
			Ui()->ClipEnable(&Visible);
			struct SScopedClip
			{
				CUi *m_pUi;
				~SScopedClip() { m_pUi->ClipDisable(); }
			} ClipGuard{Ui()};

			CUIRect Expand = {Visible.x, Visible.y, Visible.w, TargetHeight};
			static std::vector<const char *> s_MusicPlayerColorModeNames;
			s_MusicPlayerColorModeNames = {
				TCLocalize("Static color"),
				TCLocalize("Cover color"),
			};
			static CUi::SDropDownState s_MusicPlayerColorModeState;
			static CScrollRegion s_MusicPlayerColorModeScrollRegion;
			s_MusicPlayerColorModeState.m_SelectionPopupContext.m_pScrollRegion = &s_MusicPlayerColorModeScrollRegion;

			g_Config.m_BcMusicPlayerColorMode = std::clamp(g_Config.m_BcMusicPlayerColorMode, 0, 1);

			CUIRect DropDownRect;
			Expand.HSplitTop(LineSize, &DropDownRect, &Expand);
			DropDownRect.VSplitLeft(120.0f, &Label, &DropDownRect);
			Ui()->DoLabel(&Label, TCLocalize("Color mode:"), FontSize, TEXTALIGN_ML);
			g_Config.m_BcMusicPlayerColorMode = Ui()->DoDropDown(&DropDownRect, g_Config.m_BcMusicPlayerColorMode, s_MusicPlayerColorModeNames.data(), s_MusicPlayerColorModeNames.size(), s_MusicPlayerColorModeState);

			const float StaticCurHeight = StaticColorTargetHeight * s_MusicPlayerStaticColorPhase;
			if(StaticCurHeight > 0.0f)
			{
				CUIRect StaticVisible;
				Expand.HSplitTop(StaticCurHeight, &StaticVisible, &Expand);
				Ui()->ClipEnable(&StaticVisible);
				SScopedClip StaticClipGuard{Ui()};

				CUIRect StaticExpand = {StaticVisible.x, StaticVisible.y, StaticVisible.w, StaticColorTargetHeight};
				static CButtonContainer s_MusicPlayerStaticColor;
				DoLine_ColorPicker(&s_MusicPlayerStaticColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &StaticExpand, TCLocalize("Static color"), &g_Config.m_BcMusicPlayerStaticColor, ColorRGBA(0.34f, 0.53f, 0.79f, 1.0f), false);
			}
		}
		}

		Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
	}

#if defined(CONF_BESTCLIENT_CAVA)
	// Cava
	if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_CAVA))
	{
		Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		s_SectionBoxes.push_back(Column);
		Column.HSplitTop(HeadlineHeight, &Label, &Column);
		if(SettingsSearchConsumeReveal("Cava"))
			s_ScrollRegion.AddRect(Label, true);
		SettingsSearchDrawLabel(&Label, TCLocalize("Cava"), HeadlineFontSize, TEXTALIGN_ML, "Cava");
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcCavaEnable, TCLocalize("Enable Cava visualizer"), &g_Config.m_BcCavaEnable, &Column, LineSize);
		{
		static float s_CavaPhase = 0.0f;
		static float s_CavaBackgroundPhase = 0.0f;

		const float Dt = Client()->RenderFrameTime();
		const bool Enabled = g_Config.m_BcCavaEnable != 0;
		const bool BackgroundOn = Enabled && g_Config.m_BcCavaBackground != 0;
		if(ModuleUiRevealAnimationsEnabled())
		{
			BCUiAnimations::UpdatePhase(s_CavaPhase, Enabled ? 1.0f : 0.0f, Dt, ModuleUiRevealAnimationDuration());
			BCUiAnimations::UpdatePhase(s_CavaBackgroundPhase, BackgroundOn ? 1.0f : 0.0f, Dt, 0.16f);
		}
		else
		{
			s_CavaPhase = Enabled ? 1.0f : 0.0f;
			s_CavaBackgroundPhase = BackgroundOn ? 1.0f : 0.0f;
		}

		const float ColorPickerTargetHeight = ColorPickerLineSize + ColorPickerLineSpacing;
		const float BaseTargetHeight = LineSize + MarginExtraSmall + LineSize + LineSize + LineSize + LineSize + LineSize + LineSize + LineSize + LineSize + LineSize + LineSize +
									   (g_Config.m_BcCavaDockBottom ? 0.0f : 3.0f * LineSize) +
									   LineSize + ColorPickerTargetHeight + LineSize;
		const float TargetHeight = BaseTargetHeight + ColorPickerTargetHeight * s_CavaBackgroundPhase;
		const float CurHeight = TargetHeight * s_CavaPhase;
		if(CurHeight > 0.0f)
		{
			CUIRect Visible;
			Column.HSplitTop(CurHeight, &Visible, &Column);
			Ui()->ClipEnable(&Visible);
			struct SScopedClip
			{
				CUi *m_pUi;
				~SScopedClip() { m_pUi->ClipDisable(); }
			} ClipGuard{Ui()};

			CUIRect Expand = {Visible.x, Visible.y, Visible.w, TargetHeight};
			Expand.HSplitTop(LineSize, &Label, &Expand);
			Ui()->DoLabel(&Label, TCLocalize("Captures system audio output (loopback/monitor). On some systems you need a loopback device (Stereo Mix / BlackHole / Monitor)."), EditBoxFontSize, TEXTALIGN_ML);
			Expand.HSplitTop(MarginExtraSmall, nullptr, &Expand);

			{
				static CUi::SDropDownState s_CavaCaptureDeviceDropDownState;
				static CScrollRegion s_CavaCaptureDeviceDropDownScrollRegion;
				s_CavaCaptureDeviceDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_CavaCaptureDeviceDropDownScrollRegion;


#ifndef SDL_HINT_AUDIO_INCLUDE_MONITORS
#define SDL_HINT_AUDIO_INCLUDE_MONITORS "SDL_AUDIO_INCLUDE_MONITORS"
#endif
				SDL_SetHint(SDL_HINT_AUDIO_INCLUDE_MONITORS, "1");
				if(SDL_WasInit(SDL_INIT_AUDIO) == 0)
					SDL_InitSubSystem(SDL_INIT_AUDIO);

				int DeviceCount = SDL_GetNumAudioDevices(1);
				if(DeviceCount < 0)
					DeviceCount = 0;

				std::vector<std::string> vDeviceNames;
				std::vector<int> vDeviceIndexMap;
				vDeviceNames.reserve((size_t)DeviceCount + 1);
				vDeviceIndexMap.reserve((size_t)DeviceCount + 1);
				vDeviceNames.emplace_back(TCLocalize("Auto (recommended)"));
				vDeviceIndexMap.push_back(-1);
				for(int i = 0; i < DeviceCount; ++i)
				{
					const char *pDeviceName = SDL_GetAudioDeviceName(i, 1);
					if(pDeviceName && pDeviceName[0] != '\0' && IsSystemLoopbackDeviceName(pDeviceName))
					{
						vDeviceNames.emplace_back(pDeviceName);
						vDeviceIndexMap.push_back(i);
					}
				}

				std::vector<const char *> vpDeviceNames;
				vpDeviceNames.reserve(vDeviceNames.size());
				for(const std::string &DeviceName : vDeviceNames)
					vpDeviceNames.push_back(DeviceName.c_str());

				int Selection = 0;
				if(g_Config.m_BcCavaCaptureDevice >= 0)
				{
					for(int i = 1; i < (int)vDeviceIndexMap.size(); ++i)
					{
						if(vDeviceIndexMap[i] == g_Config.m_BcCavaCaptureDevice)
						{
							Selection = i;
							break;
						}
					}
				}

				CUIRect DropDownRect;
				Expand.HSplitTop(LineSize, &DropDownRect, &Expand);
				DropDownRect.VSplitLeft(140.0f, &Label, &DropDownRect);
				Ui()->DoLabel(&Label, TCLocalize("Capture device:"), FontSize, TEXTALIGN_ML);

				const int NewSelection = Ui()->DoDropDown(&DropDownRect, Selection, vpDeviceNames.data(), (int)vpDeviceNames.size(), s_CavaCaptureDeviceDropDownState);
				if(NewSelection != Selection)
					g_Config.m_BcCavaCaptureDevice = vDeviceIndexMap[std::clamp(NewSelection, 0, (int)vDeviceIndexMap.size() - 1)];
			}

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcCavaDockBottom, TCLocalize("Dock to bottom and stretch full width"), &g_Config.m_BcCavaDockBottom, &Expand, LineSize);

			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_BcCavaBars, &g_Config.m_BcCavaBars, &Button, TCLocalize("Bars"), 4, 128);

			{
				static const char *s_apFftSizes[] = {"1024", "2048", "4096"};
				static const int s_aFftSizeValues[] = {1024, 2048, 4096};
				static CUi::SDropDownState s_FftSizeDropDownState;
				static CScrollRegion s_FftSizeDropDownScrollRegion;
				s_FftSizeDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_FftSizeDropDownScrollRegion;

				int CurrentIndex = g_Config.m_BcCavaFftSize <= 1024 ? 0 : (g_Config.m_BcCavaFftSize <= 2048 ? 1 : 2);
				CUIRect DropDownRect;
				Expand.HSplitTop(LineSize, &DropDownRect, &Expand);
				DropDownRect.VSplitLeft(140.0f, &Label, &DropDownRect);
				Ui()->DoLabel(&Label, TCLocalize("FFT size:"), FontSize, TEXTALIGN_ML);
				const int NewIndex = Ui()->DoDropDown(&DropDownRect, CurrentIndex, s_apFftSizes, std::size(s_apFftSizes), s_FftSizeDropDownState);
				if(NewIndex != CurrentIndex)
					g_Config.m_BcCavaFftSize = s_aFftSizeValues[std::clamp(NewIndex, 0, 2)];
			}

			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_BcCavaLowHz, &g_Config.m_BcCavaLowHz, &Button, TCLocalize("Low Hz"), 20, 500);
			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_BcCavaHighHz, &g_Config.m_BcCavaHighHz, &Button, TCLocalize("High Hz"), 1000, 20000);
			g_Config.m_BcCavaHighHz = std::clamp(maximum(g_Config.m_BcCavaHighHz, g_Config.m_BcCavaLowHz + 1), 1000, 20000);

			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_BcCavaGain, &g_Config.m_BcCavaGain, &Button, TCLocalize("Gain"), 50, 300, &CUi::ms_LinearScrollbarScale, 0, "%");
			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_BcCavaAttack, &g_Config.m_BcCavaAttack, &Button, TCLocalize("Attack"), 1, 100);
			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_BcCavaDecay, &g_Config.m_BcCavaDecay, &Button, TCLocalize("Decay"), 1, 100);

			if(!g_Config.m_BcCavaDockBottom)
			{
				Expand.HSplitTop(LineSize, &Button, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcCavaX, &g_Config.m_BcCavaX, &Button, TCLocalize("Position X"), 0, 500);
				Expand.HSplitTop(LineSize, &Button, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcCavaY, &g_Config.m_BcCavaY, &Button, TCLocalize("Position Y"), 0, 300);
				Expand.HSplitTop(LineSize, &Button, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcCavaW, &g_Config.m_BcCavaW, &Button, TCLocalize("Width"), 20, 500);
			}
			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_BcCavaH, &g_Config.m_BcCavaH, &Button, TCLocalize("Height"), 10, 200);

			static CButtonContainer s_CavaBarColor;
			DoLine_ColorPicker(&s_CavaBarColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Expand, TCLocalize("Bar color"), &g_Config.m_BcCavaColor, ColorRGBA(0.34f, 0.53f, 0.79f, 1.0f), true);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcCavaBackground, TCLocalize("Background"), &g_Config.m_BcCavaBackground, &Expand, LineSize);
			const float BackgroundCurHeight = ColorPickerTargetHeight * s_CavaBackgroundPhase;
			if(BackgroundCurHeight > 0.0f)
			{
				CUIRect BgVisible;
				Expand.HSplitTop(BackgroundCurHeight, &BgVisible, &Expand);
				Ui()->ClipEnable(&BgVisible);
				SScopedClip BgClipGuard{Ui()};

				CUIRect BgExpand = {BgVisible.x, BgVisible.y, BgVisible.w, ColorPickerTargetHeight};
				static CButtonContainer s_CavaBackgroundColor;
				DoLine_ColorPicker(&s_CavaBackgroundColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &BgExpand, TCLocalize("Background color"), &g_Config.m_BcCavaBackgroundColor, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), true);
			}
		}
		}

		Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
		s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
	}
#endif

	// Sweat Weapon
	if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_SWEAT_WEAPON))
	{
		Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		s_SectionBoxes.push_back(Column);
		Column.HSplitTop(HeadlineHeight, &Label, &Column);
		if(SettingsSearchConsumeReveal("Sweat Weapon"))
			s_ScrollRegion.AddRect(Label, true);
		SettingsSearchDrawLabel(&Label, TCLocalize("Sweat Weapon"), HeadlineFontSize, TEXTALIGN_ML, "Sweat Weapon");
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcCrystalLaser, TCLocalize("Enable"), &g_Config.m_BcCrystalLaser, &Column, LineSize);

		CUIRect PreviewLabel, PreviewRect;

		Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &PreviewLabel, &Column);
		Ui()->DoLabel(&PreviewLabel, TCLocalize("Crystal Laser"), FontSize, TEXTALIGN_ML);
		Column.HSplitTop(58.0f, &PreviewRect, &Column);
		DoLaserPreview(&PreviewRect, ColorHSLA(g_Config.m_ClLaserRifleOutlineColor), ColorHSLA(g_Config.m_ClLaserRifleInnerColor), LASERTYPE_RIFLE);

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &PreviewLabel, &Column);
		Ui()->DoLabel(&PreviewLabel, TCLocalize("Sand Shotgun"), FontSize, TEXTALIGN_ML);
		Column.HSplitTop(58.0f, &PreviewRect, &Column);
		DoLaserPreview(&PreviewRect, ColorHSLA(g_Config.m_ClLaserShotgunOutlineColor), ColorHSLA(g_Config.m_ClLaserShotgunInnerColor), LASERTYPE_SHOTGUN);

		Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
		s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
	}

	// Media Background
	if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_MEDIA_BACKGROUND))
	{
		Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		s_SectionBoxes.push_back(Column);
		Column.HSplitTop(HeadlineHeight, &Label, &Column);
		if(SettingsSearchConsumeReveal("Media Background"))
			s_ScrollRegion.AddRect(Label, true);
		SettingsSearchDrawLabel(&Label, TCLocalize("Media Background"), HeadlineFontSize, TEXTALIGN_ML, "Media Background");
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMenuMediaBackground, TCLocalize("Enable to main menu"), &g_Config.m_BcMenuMediaBackground, &Column, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcGameMediaBackground, TCLocalize("Enable to game background"), &g_Config.m_BcGameMediaBackground, &Column, LineSize);

		struct SMenuMediaFileListContext
		{
			std::vector<std::string> *m_pLabels;
			std::vector<std::string> *m_pPaths;
		};

		auto MenuMediaFileListScan = [](const char *pName, int IsDir, int StorageType, void *pUser) {
			if(IsDir)
				return 0;

			auto *pContext = static_cast<SMenuMediaFileListContext *>(pUser);
			const std::string Ext = MediaDecoder::ExtractExtensionLower(pName);
			const bool SupportedImage = Ext == "png" || Ext == "jpg" || Ext == "jpeg" || Ext == "webp" || Ext == "bmp" || Ext == "avif" || Ext == "gif";
			const bool SupportedVideo = Ext == "mp4" || Ext == "webm" || Ext == "mov" || Ext == "m4v" || Ext == "mkv" || Ext == "avi";
			if(!SupportedImage && !SupportedVideo)
				return 0;

			pContext->m_pLabels->emplace_back(pName);
			pContext->m_pPaths->emplace_back(std::string("BestClient/backgrounds/") + pName);
			return 0;
		};

		Storage()->CreateFolder("BestClient", IStorage::TYPE_SAVE);
		Storage()->CreateFolder("BestClient/backgrounds", IStorage::TYPE_SAVE);

		static std::vector<std::string> s_vMenuMediaFileLabels;
		static std::vector<std::string> s_vMenuMediaFilePaths;
		s_vMenuMediaFileLabels.clear();
		s_vMenuMediaFilePaths.clear();
		SMenuMediaFileListContext MenuMediaContext{&s_vMenuMediaFileLabels, &s_vMenuMediaFilePaths};
		Storage()->ListDirectory(IStorage::TYPE_SAVE, "BestClient/backgrounds", MenuMediaFileListScan, &MenuMediaContext);

		std::vector<int> vSortedIndices(s_vMenuMediaFileLabels.size());
		for(size_t i = 0; i < vSortedIndices.size(); ++i)
			vSortedIndices[i] = (int)i;
		std::sort(vSortedIndices.begin(), vSortedIndices.end(), [&](int Left, int Right) {
			return str_comp_nocase(s_vMenuMediaFileLabels[Left].c_str(), s_vMenuMediaFileLabels[Right].c_str()) < 0;
		});

		static std::vector<std::string> s_vMenuMediaDropDownLabels;
		static std::vector<const char *> s_vMenuMediaDropDownLabelPtrs;
		s_vMenuMediaDropDownLabels.clear();
		s_vMenuMediaDropDownLabelPtrs.clear();
		for(int SortedIndex : vSortedIndices)
			s_vMenuMediaDropDownLabels.push_back(s_vMenuMediaFileLabels[SortedIndex]);
		for(const std::string &LabelString : s_vMenuMediaDropDownLabels)
			s_vMenuMediaDropDownLabelPtrs.push_back(LabelString.c_str());

		int SelectedMediaFile = -1;
		for(size_t i = 0; i < vSortedIndices.size(); ++i)
		{
			const int SortedIndex = vSortedIndices[i];
			if(str_comp(g_Config.m_BcMenuMediaBackgroundPath, s_vMenuMediaFilePaths[SortedIndex].c_str()) == 0)
			{
				SelectedMediaFile = (int)i;
				break;
			}
		}

		CUIRect MediaPathRow, MediaFileDropDown, MediaReloadButton, MediaFolderButton;
		Column.HSplitTop(LineSize, &MediaPathRow, &Column);
		MediaPathRow.VSplitRight(20.0f, &MediaPathRow, &MediaFolderButton);
		MediaPathRow.VSplitRight(MarginSmall, &MediaPathRow, nullptr);
		MediaPathRow.VSplitRight(20.0f, &MediaPathRow, &MediaReloadButton);
		MediaPathRow.VSplitRight(MarginSmall, &MediaPathRow, nullptr);
		MediaFileDropDown = MediaPathRow;

		if(s_vMenuMediaDropDownLabelPtrs.empty())
		{
			static CButtonContainer s_MenuMediaEmptyButton;
			DoButton_Menu(&s_MenuMediaEmptyButton, TCLocalize("No media files in backgrounds folder"), -1, &MediaFileDropDown);
		}
		else
		{
			static CUi::SDropDownState s_MenuMediaFileDropDownState;
			static CScrollRegion s_MenuMediaFileDropDownScrollRegion;
			s_MenuMediaFileDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_MenuMediaFileDropDownScrollRegion;
			const int NewSelectedMediaFile = Ui()->DoDropDown(&MediaFileDropDown, SelectedMediaFile, s_vMenuMediaDropDownLabelPtrs.data(), s_vMenuMediaDropDownLabelPtrs.size(), s_MenuMediaFileDropDownState);
			if(NewSelectedMediaFile != SelectedMediaFile && NewSelectedMediaFile >= 0 && NewSelectedMediaFile < (int)vSortedIndices.size())
			{
				const int SortedIndex = vSortedIndices[NewSelectedMediaFile];
				str_copy(g_Config.m_BcMenuMediaBackgroundPath, s_vMenuMediaFilePaths[SortedIndex].c_str(), sizeof(g_Config.m_BcMenuMediaBackgroundPath));
				m_MenuMediaBackground.ReloadFromConfig();
			}
		}

		static CButtonContainer s_MenuMediaReloadButton;
		if(Ui()->DoButton_FontIcon(&s_MenuMediaReloadButton, FONT_ICON_ARROW_ROTATE_RIGHT, 0, &MediaReloadButton, IGraphics::CORNER_ALL))
			m_MenuMediaBackground.ReloadFromConfig();

		static CButtonContainer s_MenuMediaFolderButton;
		if(Ui()->DoButton_FontIcon(&s_MenuMediaFolderButton, FONT_ICON_FOLDER, 0, &MediaFolderButton, IGraphics::CORNER_ALL))
		{
			Storage()->CreateFolder("BestClient", IStorage::TYPE_SAVE);
			Storage()->CreateFolder("BestClient/backgrounds", IStorage::TYPE_SAVE);
			char aBuf[IO_MAX_PATH_LENGTH];
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, "BestClient/backgrounds", aBuf, sizeof(aBuf));
			Client()->ViewFile(aBuf);
		}

		char aMediaFolderPath[IO_MAX_PATH_LENGTH];
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, "BestClient/backgrounds", aMediaFolderPath, sizeof(aMediaFolderPath));

		char aSelectedMediaPath[IO_MAX_PATH_LENGTH + 32];
		if(g_Config.m_BcMenuMediaBackgroundPath[0] != '\0')
			str_format(aSelectedMediaPath, sizeof(aSelectedMediaPath), "%s %s", TCLocalize("Selected:"), g_Config.m_BcMenuMediaBackgroundPath);
		else
			str_format(aSelectedMediaPath, sizeof(aSelectedMediaPath), "%s %s", TCLocalize("Selected:"), TCLocalize("none"));

		char aFolderLabel[IO_MAX_PATH_LENGTH + 32];
		str_format(aFolderLabel, sizeof(aFolderLabel), "%s %s", TCLocalize("Folder:"), aMediaFolderPath);

		CUIRect MediaInfoRow;
		Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &MediaInfoRow, &Column);
		Ui()->DoLabel(&MediaInfoRow, aFolderLabel, 11.0f, TEXTALIGN_ML);
		Column.HSplitTop(LineSize, &MediaInfoRow, &Column);
		Ui()->DoLabel(&MediaInfoRow, aSelectedMediaPath, 11.0f, TEXTALIGN_ML);

		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_BcGameMediaBackgroundOffset, &g_Config.m_BcGameMediaBackgroundOffset, &Button, TCLocalize("Map offset"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");
		GameClient()->m_Tooltips.DoToolTip(&g_Config.m_BcGameMediaBackgroundOffset, &Button, TCLocalize("0 keeps the image fixed to the screen. 100 fixes it to the map for a full parallax effect."));

		Column.HSplitTop(LineSize, &MediaInfoRow, &Column);
		if(m_MenuMediaBackground.HasError())
			TextRender()->TextColor(ColorRGBA(1.0f, 0.45f, 0.45f, 1.0f));
		else if(m_MenuMediaBackground.IsLoaded())
			TextRender()->TextColor(ColorRGBA(0.55f, 1.0f, 0.55f, 1.0f));
		Ui()->DoLabel(&MediaInfoRow, m_MenuMediaBackground.StatusText(), 11.0f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
		s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
	}

	// ***** Pet ***** //
	if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_PET))
	{
		Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		s_SectionBoxes.push_back(Column);
		Column.HSplitTop(HeadlineHeight, &Label, &Column);
		if(SettingsSearchConsumeReveal("Pet"))
			s_ScrollRegion.AddRect(Label, true);
		Ui()->DoLabel(&Label, TCLocalize("Pet"), HeadlineFontSize, TEXTALIGN_ML);
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		CUIRect PetSectionTop = Column;

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcPetShow, TCLocalize("Show the pet"), &g_Config.m_TcPetShow, &Column, LineSize);
		{
		static float s_PetPhase = 0.0f;
		const float Dt = Client()->RenderFrameTime();
		const bool Enabled = g_Config.m_TcPetShow != 0;
		if(ModuleUiRevealAnimationsEnabled())
			BCUiAnimations::UpdatePhase(s_PetPhase, Enabled ? 1.0f : 0.0f, Dt, ModuleUiRevealAnimationDuration());
		else
			s_PetPhase = Enabled ? 1.0f : 0.0f;

		const float TargetHeight = 3.0f * LineSize + MarginExtraSmall;
		const float CurHeight = TargetHeight * s_PetPhase;
		if(CurHeight > 0.0f)
		{
			CUIRect Visible;
			Column.HSplitTop(CurHeight, &Visible, &Column);
			Ui()->ClipEnable(&Visible);
			struct SScopedClip
			{
				CUi *m_pUi;
				~SScopedClip() { m_pUi->ClipDisable(); }
			} ClipGuard{Ui()};

			CUIRect Expand = {Visible.x, Visible.y, Visible.w, TargetHeight};
			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_TcPetSize, &g_Config.m_TcPetSize, &Button, TCLocalize("Pet size"), 10, 500, &CUi::ms_LinearScrollbarScale, 0, "%");
			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_TcPetAlpha, &g_Config.m_TcPetAlpha, &Button, TCLocalize("Pet alpha"), 10, 100, &CUi::ms_LinearScrollbarScale, 0, "%");

			Expand.HSplitTop(LineSize + MarginExtraSmall, &Button, &Expand);
			Button.VSplitMid(&Label, &Button);
			Ui()->DoLabel(&Label, TCLocalize("Pet Skin"), FontSize, TEXTALIGN_ML);
			static CLineInput sPetSkin(g_Config.m_TcPetSkin, sizeof(g_Config.m_TcPetSkin));
			Ui()->DoEditBox(&sPetSkin, &Button, EditBoxFontSize);
		}
		}

		CUIRect Preview;
		Preview.x = PetSectionTop.x + PetSectionTop.w - 70.0f;
		Preview.y = PetSectionTop.y - 40.0f;
		Preview.w = 64.0f;
		Preview.h = 64.0f;
		vec2 TeeRenderPos(Preview.x + Preview.w / 2.0f, Preview.y + 35.0f);
		int PetEmote = g_Config.m_ClPlayerDefaultEyes;
		RenderDevSkin(TeeRenderPos, MenuPreviewTeeLarge, g_Config.m_TcPetSkin, "default", false, 0, 0, PetEmote, false, true, ColorRGBA(1.0f, 1.0f, 1.0f), ColorRGBA(1.0f, 1.0f, 1.0f));

		s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
	}
	
	// Magic Particles
	if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_MAGIC_PARTICLES))
	{
		Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		s_SectionBoxes.push_back(Column);
		Column.HSplitTop(HeadlineHeight, &Label, &Column);
		if(SettingsSearchConsumeReveal("Magic Particles"))
			s_ScrollRegion.AddRect(Label, true);
		Ui()->DoLabel(&Label, TCLocalize("Magic Particles"), HeadlineFontSize, TEXTALIGN_ML);
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMagicParticles, TCLocalize("Magic Particles"), &g_Config.m_BcMagicParticles, &Column, LineSize);

		{
		static float s_MagicParticlesPhase = 0.0f;
		const float Dt = Client()->RenderFrameTime();
		const bool Enabled = g_Config.m_BcMagicParticles != 0;
		if(ModuleUiRevealAnimationsEnabled())
			BCUiAnimations::UpdatePhase(s_MagicParticlesPhase, Enabled ? 1.0f : 0.0f, Dt, ModuleUiRevealAnimationDuration());
		else
			s_MagicParticlesPhase = Enabled ? 1.0f : 0.0f;

		const float TargetHeight = 4.0f * LineSize;
		const float CurHeight = TargetHeight * s_MagicParticlesPhase;
		if(CurHeight > 0.0f)
		{
			CUIRect Visible;
			Column.HSplitTop(CurHeight, &Visible, &Column);
			Ui()->ClipEnable(&Visible);
			struct SScopedClip
			{
				CUi *m_pUi;
				~SScopedClip() { m_pUi->ClipDisable(); }
			} ClipGuard{Ui()};

			CUIRect Expand = {Visible.x, Visible.y, Visible.w, TargetHeight};
			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_BcMagicParticlesRadius, &g_Config.m_BcMagicParticlesRadius, &Button, TCLocalize("Magic Particles Radius"), 1, 20);

			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_BcMagicParticlesSize, &g_Config.m_BcMagicParticlesSize, &Button, TCLocalize("Magic Particles Size"), 1, 20);

			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_BcMagicParticlesAlphaDelay, &g_Config.m_BcMagicParticlesAlphaDelay, &Button, TCLocalize("Magic Particles Alpha delay"), 0, 10);

			static CUi::SDropDownState sMagicParticlesNameState;
			static CScrollRegion sMagicParticlesNameScrollRegion;
			sMagicParticlesNameState.m_SelectionPopupContext.m_pScrollRegion = &sMagicParticlesNameScrollRegion;

			CUIRect MagicParticlesType;
			Expand.HSplitTop(LineSize, &MagicParticlesType, &Expand);
			MagicParticlesType.VSplitLeft(150.0f, &Label, &MagicParticlesType);
			Ui()->DoLabel(&Label, TCLocalize("Magic Particles Type"), FontSize, TEXTALIGN_ML);

			std::array<const char *, 4> aMagicParticlesNames = {TCLocalize("Slice"), TCLocalize("Ball"), TCLocalize("Smoke"), TCLocalize("Shell")};

			g_Config.m_BcMagicParticlesType = Ui()->DoDropDown(&MagicParticlesType, g_Config.m_BcMagicParticlesType - 1, aMagicParticlesNames.data(), (int)aMagicParticlesNames.size(), sMagicParticlesNameState) + 1;
		}
		}

		s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
	}

	// Orbit Aura
	if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_ORBIT_AURA))
	{
		Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		s_SectionBoxes.push_back(Column);
		Column.HSplitTop(HeadlineHeight, &Label, &Column);
		if(SettingsSearchConsumeReveal("Orbit Aura"))
			s_ScrollRegion.AddRect(Label, true);
		Ui()->DoLabel(&Label, TCLocalize("Orbit Aura"), HeadlineFontSize, TEXTALIGN_ML);
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOrbitAura, TCLocalize("Orbit Aura"), &g_Config.m_BcOrbitAura, &Column, LineSize);

		{
		static float s_OrbitAuraPhase = 0.0f;
		static float s_OrbitAuraIdlePhase = 0.0f;
		const float Dt = Client()->RenderFrameTime();
		const bool Enabled = g_Config.m_BcOrbitAura != 0;
		const bool IdleOn = Enabled && g_Config.m_BcOrbitAuraIdle != 0;
		if(ModuleUiRevealAnimationsEnabled())
		{
			BCUiAnimations::UpdatePhase(s_OrbitAuraPhase, Enabled ? 1.0f : 0.0f, Dt, ModuleUiRevealAnimationDuration());
			BCUiAnimations::UpdatePhase(s_OrbitAuraIdlePhase, IdleOn ? 1.0f : 0.0f, Dt, 0.16f);
		}
		else
		{
			s_OrbitAuraPhase = Enabled ? 1.0f : 0.0f;
			s_OrbitAuraIdlePhase = IdleOn ? 1.0f : 0.0f;
		}

		const float IdleTargetHeight = LineSize;
		const float BaseTargetHeight = LineSize + 4.0f * LineSize;
		const float TargetHeight = BaseTargetHeight + IdleTargetHeight * s_OrbitAuraIdlePhase;
		const float CurHeight = TargetHeight * s_OrbitAuraPhase;
		if(CurHeight > 0.0f)
		{
			CUIRect Visible;
			Column.HSplitTop(CurHeight, &Visible, &Column);
			Ui()->ClipEnable(&Visible);
			struct SScopedClip
			{
				CUi *m_pUi;
				~SScopedClip() { m_pUi->ClipDisable(); }
			} ClipGuard{Ui()};

			CUIRect Expand = {Visible.x, Visible.y, Visible.w, TargetHeight};
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOrbitAuraIdle, TCLocalize("Enable in idle mode"), &g_Config.m_BcOrbitAuraIdle, &Expand, LineSize);

			const float IdleCurHeight = IdleTargetHeight * s_OrbitAuraIdlePhase;
			if(IdleCurHeight > 0.0f)
			{
				CUIRect IdleVisible;
				Expand.HSplitTop(IdleCurHeight, &IdleVisible, &Expand);
				Ui()->ClipEnable(&IdleVisible);
				SScopedClip IdleClipGuard{Ui()};

				CUIRect IdleExpand = {IdleVisible.x, IdleVisible.y, IdleVisible.w, IdleTargetHeight};
				IdleExpand.HSplitTop(LineSize, &Button, &IdleExpand);
				Ui()->DoScrollbarOption(&g_Config.m_BcOrbitAuraIdleTimer, &g_Config.m_BcOrbitAuraIdleTimer, &Button, TCLocalize("Idle delay"), 1, 30);
			}

			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_BcOrbitAuraRadius, &g_Config.m_BcOrbitAuraRadius, &Button, TCLocalize("Aura Radius"), 8, 200);

			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_BcOrbitAuraParticles, &g_Config.m_BcOrbitAuraParticles, &Button, TCLocalize("Particles"), 2, 120);

			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_BcOrbitAuraAlpha, &g_Config.m_BcOrbitAuraAlpha, &Button, TCLocalize("Aura Alpha"), 0, 100);

			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_BcOrbitAuraSpeed, &g_Config.m_BcOrbitAuraSpeed, &Button, TCLocalize("Aura Speed"), 10, 200);
		}
		}

		s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
	}
	Column.HSplitTop(MarginSmall, nullptr, &Column);


	// ***** RightView ***** //
	LeftView = Column;
	Column = RightView;
	const bool IsOnline = Client()->State() == IClient::STATE_ONLINE;
	const bool IsFngServer = IsOnline && GameClient()->m_GameInfo.m_PredictFNG;
	const bool Is0xFServer = IsOnline && str_comp_nocase(GameClient()->m_GameInfo.m_aGameType, "0xf") == 0;
	const bool IsBlockedCameraServer = IsFngServer || Is0xFServer;

	// ***** Optimizer ***** //
	if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_OPTIMIZER))
	{
		Column.HSplitTop(Margin, nullptr, &Column);
		s_SectionBoxes.push_back(Column);
		Column.HSplitTop(HeadlineHeight, &Label, &Column);
		if(SettingsSearchConsumeReveal("Optimizer"))
			s_ScrollRegion.AddRect(Label, true);
		SettingsSearchDrawLabel(&Label, TCLocalize("Optimizer"), HeadlineFontSize, TEXTALIGN_ML, "Optimizer");
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizer, TCLocalize("Enable optimizer"), &g_Config.m_BcOptimizer, &Column, LineSize);
		{
		static float s_OptimizerPhase = 0.0f;
		static float s_OptimizerFogPhase = 0.0f;
		const float Dt = Client()->RenderFrameTime();
		const bool Enabled = g_Config.m_BcOptimizer != 0;
		const bool FogOn = Enabled && g_Config.m_BcOptimizerFpsFog != 0;
		if(ModuleUiRevealAnimationsEnabled())
		{
			BCUiAnimations::UpdatePhase(s_OptimizerPhase, Enabled ? 1.0f : 0.0f, Dt, ModuleUiRevealAnimationDuration());
			BCUiAnimations::UpdatePhase(s_OptimizerFogPhase, FogOn ? 1.0f : 0.0f, Dt, 0.16f);
		}
		else
		{
			s_OptimizerPhase = Enabled ? 1.0f : 0.0f;
			s_OptimizerFogPhase = FogOn ? 1.0f : 0.0f;
		}

		const float RadioTargetHeight = 22.0f;
		const float FogTargetHeight = 3.0f * LineSize + RadioTargetHeight;
		const float BaseTargetHeight = 5.0f * LineSize;
		const float TargetHeight = BaseTargetHeight + FogTargetHeight * s_OptimizerFogPhase;
		const float CurHeight = TargetHeight * s_OptimizerPhase;
		if(CurHeight > 0.0f)
		{
			CUIRect Visible;
			Column.HSplitTop(CurHeight, &Visible, &Column);
			Ui()->ClipEnable(&Visible);
			struct SScopedClip
			{
				CUi *m_pUi;
				~SScopedClip() { m_pUi->ClipDisable(); }
			} ClipGuard{Ui()};

			CUIRect Expand = {Visible.x, Visible.y, Visible.w, TargetHeight};
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerDisableParticles, TCLocalize("Disable all particles render"), &g_Config.m_BcOptimizerDisableParticles, &Expand, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_GfxHighDetail, Localize("High Detail"), &g_Config.m_GfxHighDetail, &Expand, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerFpsFog, TCLocalize("FPS fog (cull outside limit)"), &g_Config.m_BcOptimizerFpsFog, &Expand, LineSize);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerDdnetPriorityHigh, TCLocalize("DDNet priority: High"), &g_Config.m_BcOptimizerDdnetPriorityHigh, &Expand, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerDiscordPriorityBelowNormal, TCLocalize("Discord priority: Below Normal"), &g_Config.m_BcOptimizerDiscordPriorityBelowNormal, &Expand, LineSize);

			const float FogCurHeight = FogTargetHeight * s_OptimizerFogPhase;
			if(FogCurHeight > 0.0f)
			{
				CUIRect FogVisible;
				Expand.HSplitTop(FogCurHeight, &FogVisible, &Expand);
				Ui()->ClipEnable(&FogVisible);
				SScopedClip FogClipGuard{Ui()};

				CUIRect FogExpand = {FogVisible.x, FogVisible.y, FogVisible.w, FogTargetHeight};
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerFpsFogRenderRect, TCLocalize("Render FPS fog rectangle"), &g_Config.m_BcOptimizerFpsFogRenderRect, &FogExpand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerFpsFogCullMapTiles, TCLocalize("Cull map tiles outside FPS fog"), &g_Config.m_BcOptimizerFpsFogCullMapTiles, &FogExpand, LineSize);

				static std::vector<CButtonContainer> s_OptimizerFogModeButtons = {{}, {}};
				int FogMode = g_Config.m_BcOptimizerFpsFogMode;
				if(DoLine_RadioMenu(FogExpand, TCLocalize("FPS fog mode"),
					   s_OptimizerFogModeButtons,
					   {TCLocalize("Manual radius"), TCLocalize("By zoom")},
					   {0, 1},
					   FogMode))
				{
					g_Config.m_BcOptimizerFpsFogMode = FogMode;
				}

				FogExpand.HSplitTop(LineSize, &Button, &FogExpand);
				if(g_Config.m_BcOptimizerFpsFogMode == 0)
					Ui()->DoScrollbarOption(&g_Config.m_BcOptimizerFpsFogRadiusTiles, &g_Config.m_BcOptimizerFpsFogRadiusTiles, &Button, TCLocalize("Radius (tiles)"), 5, 300);
				else
					Ui()->DoScrollbarOption(&g_Config.m_BcOptimizerFpsFogZoomPercent, &g_Config.m_BcOptimizerFpsFogZoomPercent, &Button, TCLocalize("Visible area (%)"), 10, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
			}
		}
		}

		s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
		Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	}

	// Animations
	if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_ANIMATIONS))
	{
		Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		s_SectionBoxes.push_back(Column);
		Column.HSplitTop(HeadlineHeight, &Label, &Column);
		if(SettingsSearchConsumeReveal("Animations"))
			s_ScrollRegion.AddRect(Label, true);
		Ui()->DoLabel(&Label, TCLocalize("Animations"), HeadlineFontSize, TEXTALIGN_ML);
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcAnimations, TCLocalize("Enable animations"), &g_Config.m_BcAnimations, &Column, LineSize);
		{
			static float s_AnimationsBlockPhase = 0.0f;
			const float Dt = Client()->RenderFrameTime();
			const bool Enabled = g_Config.m_BcAnimations != 0;
			const bool AnimateBlock = g_Config.m_BcModuleUiRevealAnimation != 0;
			if(AnimateBlock)
				BCUiAnimations::UpdatePhase(s_AnimationsBlockPhase, Enabled ? 1.0f : 0.0f, Dt, ModuleUiRevealAnimationDuration());
			else
				s_AnimationsBlockPhase = Enabled ? 1.0f : 0.0f;

			const float TargetHeight = 10.0f * LineSize;
			const float CurHeight = TargetHeight * s_AnimationsBlockPhase;
			if(CurHeight > 0.0f)
			{
				CUIRect Visible;
				Column.HSplitTop(CurHeight, &Visible, &Column);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, TargetHeight};

				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcModuleUiRevealAnimation, TCLocalize("Module settings reveals"), &g_Config.m_BcModuleUiRevealAnimation, &Expand, LineSize);
				Expand.HSplitTop(LineSize, &Button, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcModuleUiRevealAnimationMs, &g_Config.m_BcModuleUiRevealAnimationMs, &Button, TCLocalize("Module reveal time (ms)"), 1, 500);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcIngameMenuAnimation, TCLocalize("ESC menu animation"), &g_Config.m_BcIngameMenuAnimation, &Expand, LineSize);
				Expand.HSplitTop(LineSize, &Button, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcIngameMenuAnimationMs, &g_Config.m_BcIngameMenuAnimationMs, &Button, TCLocalize("ESC menu animation time (ms)"), 1, 500);

				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatAnimation, TCLocalize("Chat message animations"), &g_Config.m_BcChatAnimation, &Expand, LineSize);
				Expand.HSplitTop(LineSize, &Button, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcChatAnimationMs, &g_Config.m_BcChatAnimationMs, &Button, TCLocalize("Chat message animation time (ms)"), 1, 500);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatOpenAnimation, TCLocalize("Chat open animation"), &g_Config.m_BcChatOpenAnimation, &Expand, LineSize);
				Expand.HSplitTop(LineSize, &Button, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcChatOpenAnimationMs, &g_Config.m_BcChatOpenAnimationMs, &Button, TCLocalize("Chat open animation time (ms)"), 1, 500);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatTypingAnimation, TCLocalize("Chat typing animation"), &g_Config.m_BcChatTypingAnimation, &Expand, LineSize);
				Expand.HSplitTop(LineSize, &Button, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcChatTypingAnimationMs, &g_Config.m_BcChatTypingAnimationMs, &Button, TCLocalize("Chat typing animation time (ms)"), 1, 500);
			}
		}

		s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
		Column.HSplitTop(MarginSmall, nullptr, &Column);
	}

	// Camera Drift
	if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_CAMERA_DRIFT))
	{
		Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		s_SectionBoxes.push_back(Column);
		Column.HSplitTop(HeadlineHeight, &Label, &Column);
		if(SettingsSearchConsumeReveal("Camera Drift"))
			s_ScrollRegion.AddRect(Label, true);
		Ui()->DoLabel(&Label, TCLocalize("Camera Drift"), HeadlineFontSize, TEXTALIGN_ML);
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcCameraDrift, TCLocalize("Camera Drift"), &g_Config.m_BcCameraDrift, &Column, LineSize))
		{
		if(IsBlockedCameraServer && g_Config.m_BcCameraDrift)
		{
			g_Config.m_BcCameraDrift = 0;
			GameClient()->Echo(TCLocalize("[[red]] This feature is disabled on this server"));
		}
	}

	static float s_CameraDriftPhase = 0.0f;
	{
		const float Dt = Client()->RenderFrameTime();
		const bool Enabled = g_Config.m_BcCameraDrift != 0;
		if(ModuleUiRevealAnimationsEnabled())
			BCUiAnimations::UpdatePhase(s_CameraDriftPhase, Enabled ? 1.0f : 0.0f, Dt, ModuleUiRevealAnimationDuration());
		else
			s_CameraDriftPhase = Enabled ? 1.0f : 0.0f;

		const float TargetHeight = 3.0f * LineSize;
		const float CurHeight = TargetHeight * s_CameraDriftPhase;
		if(CurHeight > 0.0f)
		{
			CUIRect Visible;
			Column.HSplitTop(CurHeight, &Visible, &Column);
			Ui()->ClipEnable(&Visible);
			struct SScopedClip
			{
				CUi *m_pUi;
				~SScopedClip() { m_pUi->ClipDisable(); }
			} ClipGuard{Ui()};

			CUIRect Expand = {Visible.x, Visible.y, Visible.w, TargetHeight};
			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_BcCameraDriftAmount, &g_Config.m_BcCameraDriftAmount, &Button, TCLocalize("Camera drift amount"), 1, 200);

			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_BcCameraDriftSmoothness, &g_Config.m_BcCameraDriftSmoothness, &Button, TCLocalize("Camera drift smoothness"), 1, 20);

			CUIRect DirectionRow, DirectionLabel, DirectionButtons, DirectionForward, DirectionBackward;
			Expand.HSplitTop(LineSize, &DirectionRow, &Expand);
			DirectionRow.VSplitLeft(150.0f, &DirectionLabel, &DirectionButtons);
			Ui()->DoLabel(&DirectionLabel, TCLocalize("Drift direction"), FontSize, TEXTALIGN_ML);
			DirectionButtons.VSplitMid(&DirectionForward, &DirectionBackward, MarginSmall);

			static int s_CameraDriftForwardButton = 0;
			static int s_CameraDriftBackwardButton = 0;
			if(DoButton_CheckBox(&s_CameraDriftForwardButton, TCLocalize("Forward"), !g_Config.m_BcCameraDriftReverse, &DirectionForward))
				g_Config.m_BcCameraDriftReverse = 0;
			if(DoButton_CheckBox(&s_CameraDriftBackwardButton, TCLocalize("Backward"), g_Config.m_BcCameraDriftReverse, &DirectionBackward))
				g_Config.m_BcCameraDriftReverse = 1;
		}
		}

		s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
		Column.HSplitTop(MarginSmall, nullptr, &Column);
	}

	// Dynamic FOV
	if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_DYNAMIC_FOV))
	{
		Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		s_SectionBoxes.push_back(Column);
		Column.HSplitTop(HeadlineHeight, &Label, &Column);
		if(SettingsSearchConsumeReveal("Dynamic FOV"))
			s_ScrollRegion.AddRect(Label, true);
		Ui()->DoLabel(&Label, TCLocalize("Dynamic FOV"), HeadlineFontSize, TEXTALIGN_ML);
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcDynamicFov, TCLocalize("Dynamic FOV"), &g_Config.m_BcDynamicFov, &Column, LineSize))
		{
		if(IsBlockedCameraServer && g_Config.m_BcDynamicFov)
		{
			g_Config.m_BcDynamicFov = 0;
			GameClient()->Echo(TCLocalize("[[red]] This feature is disabled on this server"));
		}
		}

	static float s_DynamicFovPhase = 0.0f;
	{
		const float Dt = Client()->RenderFrameTime();
		const bool Enabled = g_Config.m_BcDynamicFov != 0;
		if(ModuleUiRevealAnimationsEnabled())
			BCUiAnimations::UpdatePhase(s_DynamicFovPhase, Enabled ? 1.0f : 0.0f, Dt, ModuleUiRevealAnimationDuration());
		else
			s_DynamicFovPhase = Enabled ? 1.0f : 0.0f;

		const float TargetHeight = 2.0f * LineSize;
		const float CurHeight = TargetHeight * s_DynamicFovPhase;
		if(CurHeight > 0.0f)
		{
			CUIRect Visible;
			Column.HSplitTop(CurHeight, &Visible, &Column);
			Ui()->ClipEnable(&Visible);
			struct SScopedClip
			{
				CUi *m_pUi;
				~SScopedClip() { m_pUi->ClipDisable(); }
			} ClipGuard{Ui()};

			CUIRect Expand = {Visible.x, Visible.y, Visible.w, TargetHeight};
			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_BcDynamicFovAmount, &g_Config.m_BcDynamicFovAmount, &Button, TCLocalize("Dynamic FOV amount"), 1, 200);

			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_BcDynamicFovSmoothness, &g_Config.m_BcDynamicFovSmoothness, &Button, TCLocalize("Dynamic FOV smoothness"), 1, 20);
		}
	}

	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
	Column.HSplitTop(MarginSmall, nullptr, &Column);
	}

	// Client Indicator
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	if(SettingsSearchConsumeReveal("Client Indicator"))
		s_ScrollRegion.AddRect(Label, true);
	SettingsSearchDrawLabel(&Label, TCLocalize("Client Indicator"), HeadlineFontSize, TEXTALIGN_ML, "Client Indicator");
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcClientIndicatorInNamePlate, TCLocalize("Show in nameplates"), &g_Config.m_BcClientIndicatorInNamePlate, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcClientIndicatorInNamePlateAboveSelf, TCLocalize("Show in nameplates above self"), &g_Config.m_BcClientIndicatorInNamePlateAboveSelf, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcClientIndicatorInNamePlateDynamic, TCLocalize("Dynamic nameplate positioning"), &g_Config.m_BcClientIndicatorInNamePlateDynamic, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_BcClientIndicatorInNamePlateSize, &g_Config.m_BcClientIndicatorInNamePlateSize, &Button, TCLocalize("Nameplate icon size"), -50, 100);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcClientIndicatorInScoreboard, TCLocalize("Show in scoreboard"), &g_Config.m_BcClientIndicatorInScoreboard, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_BcClientIndicatorInSoreboardSize, &g_Config.m_BcClientIndicatorInSoreboardSize, &Button, TCLocalize("Scoreboard icon size"), -50, 100);

	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	// Afterimage
	if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_AFTERIMAGE))
	{
		Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		s_SectionBoxes.push_back(Column);
		Column.HSplitTop(HeadlineHeight, &Label, &Column);
		if(SettingsSearchConsumeReveal("Afterimage"))
			s_ScrollRegion.AddRect(Label, true);
		SettingsSearchDrawLabel(&Label, TCLocalize("Afterimage"), HeadlineFontSize, TEXTALIGN_ML, "Afterimage");
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcAfterimage, TCLocalize("Enable Afterimage"), &g_Config.m_BcAfterimage, &Column, LineSize);

		{
		static float s_AfterimagePhase = 0.0f;
		const float Dt = Client()->RenderFrameTime();
		const bool Enabled = g_Config.m_BcAfterimage != 0;
		if(ModuleUiRevealAnimationsEnabled())
			BCUiAnimations::UpdatePhase(s_AfterimagePhase, Enabled ? 1.0f : 0.0f, Dt, ModuleUiRevealAnimationDuration());
		else
			s_AfterimagePhase = Enabled ? 1.0f : 0.0f;

		const float TargetHeight = 3.0f * LineSize;
		const float CurHeight = TargetHeight * s_AfterimagePhase;
		if(CurHeight > 0.0f)
		{
			CUIRect Visible;
			Column.HSplitTop(CurHeight, &Visible, &Column);
			Ui()->ClipEnable(&Visible);
			struct SScopedClip
			{
				CUi *m_pUi;
				~SScopedClip() { m_pUi->ClipDisable(); }
			} ClipGuard{Ui()};

			CUIRect Expand = {Visible.x, Visible.y, Visible.w, TargetHeight};
			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_BcAfterimageFrames, &g_Config.m_BcAfterimageFrames, &Button, TCLocalize("Afterimage frames"), 2, 20);
			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_BcAfterimageAlpha, &g_Config.m_BcAfterimageAlpha, &Button, TCLocalize("Afterimage alpha"), 1, 100);
			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_BcAfterimageSpacing, &g_Config.m_BcAfterimageSpacing, &Button, TCLocalize("Afterimage spacing"), 1, 64);
		}
		}

		s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
		Column.HSplitTop(MarginSmall, nullptr, &Column);
	}
	
	// Focus Mode
	if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_FOCUS_MODE))
	{
		Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		s_SectionBoxes.push_back(Column);
		Column.HSplitTop(HeadlineHeight, &Label, &Column);
		if(SettingsSearchConsumeReveal("Focus Mode"))
			s_ScrollRegion.AddRect(Label, true);
		Ui()->DoLabel(&Label, TCLocalize("Focus Mode"), HeadlineFontSize, TEXTALIGN_ML);
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusMode, TCLocalize("Enable Focus Mode"), &g_Config.m_ClFocusMode, &Column, LineSize);

		{
		static float s_FocusModePhase = 0.0f;
		const float Dt = Client()->RenderFrameTime();
		const bool Enabled = g_Config.m_ClFocusMode != 0;
		if(ModuleUiRevealAnimationsEnabled())
			BCUiAnimations::UpdatePhase(s_FocusModePhase, Enabled ? 1.0f : 0.0f, Dt, ModuleUiRevealAnimationDuration());
		else
			s_FocusModePhase = Enabled ? 1.0f : 0.0f;

		const float TargetHeight = 7.0f * LineSize;
		const float CurHeight = TargetHeight * s_FocusModePhase;
		if(CurHeight > 0.0f)
		{
			CUIRect Visible;
			Column.HSplitTop(CurHeight, &Visible, &Column);
			Ui()->ClipEnable(&Visible);
			struct SScopedClip
			{
				CUi *m_pUi;
				~SScopedClip() { m_pUi->ClipDisable(); }
			} ClipGuard{Ui()};

			CUIRect Expand = {Visible.x, Visible.y, Visible.w, TargetHeight};
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideNames, TCLocalize("Hide Player Names"), &g_Config.m_ClFocusModeHideNames, &Expand, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideEffects, TCLocalize("Hide Visual Effects"), &g_Config.m_ClFocusModeHideEffects, &Expand, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideHud, TCLocalize("Hide HUD"), &g_Config.m_ClFocusModeHideHud, &Expand, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideSongPlayer, TCLocalize("Hide Song Player"), &g_Config.m_ClFocusModeHideSongPlayer, &Expand, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideUI, TCLocalize("Hide Unnecessary UI"), &g_Config.m_ClFocusModeHideUI, &Expand, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideChat, TCLocalize("Hide Chat"), &g_Config.m_ClFocusModeHideChat, &Expand, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideScoreboard, TCLocalize("Hide Scoreboard"), &g_Config.m_ClFocusModeHideScoreboard, &Expand, LineSize);
		}
	}
	
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
	}

	// Chat Bubbles
	if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_CHAT_BUBBLES))
	{
		Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		s_SectionBoxes.push_back(Column);
		Column.HSplitTop(HeadlineHeight, &Label, &Column);
		if(SettingsSearchConsumeReveal("Chat Bubbles"))
			s_ScrollRegion.AddRect(Label, true);
		Ui()->DoLabel(&Label, TCLocalize("Chat Bubbles"), HeadlineFontSize, TEXTALIGN_ML);
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatBubbles, TCLocalize("Chat Bubbles"), &g_Config.m_BcChatBubbles, &Column, LineSize);

		static float s_BcChatBubblesPhase = 0.0f;
		{
		const float Dt = Client()->RenderFrameTime();
		const bool TargetOn = g_Config.m_BcChatBubbles != 0;
		if(ModuleUiRevealAnimationsEnabled())
			BCUiAnimations::UpdatePhase(s_BcChatBubblesPhase, TargetOn ? 1.0f : 0.0f, Dt, ModuleUiRevealAnimationDuration());
		else
			s_BcChatBubblesPhase = TargetOn ? 1.0f : 0.0f;

		const float TargetHeight = 4.0f * MarginSmall + 6.0f * LineSize;
		const float CurHeight = TargetHeight * s_BcChatBubblesPhase;
		if(CurHeight > 0.0f)
		{
			CUIRect Visible;
			Column.HSplitTop(CurHeight, &Visible, &Column);
			Ui()->ClipEnable(&Visible);
			struct SScopedClip
			{
				CUi *m_pUi;
				~SScopedClip() { m_pUi->ClipDisable(); }
			} ClipGuard{Ui()};

			CUIRect Expand = {Visible.x, Visible.y, Visible.w, TargetHeight};
			Expand.HSplitTop(MarginSmall, nullptr, &Expand);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatBubblesDemo, Localize("Show Chatbubbles in demo"), &g_Config.m_BcChatBubblesDemo, &Expand, LineSize);
			Expand.HSplitTop(MarginSmall, nullptr, &Expand);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatBubblesSelf, Localize("Show Chatbubbles above you"), &g_Config.m_BcChatBubblesSelf, &Expand, LineSize);
			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_BcChatBubbleSize, &g_Config.m_BcChatBubbleSize, &Button, Localize("Chat Bubble Size"), 20, 30);
			Expand.HSplitTop(MarginSmall, &Button, &Expand);
			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_BcChatBubbleShowTime, &g_Config.m_BcChatBubbleShowTime, &Button, Localize("Show the Bubbles for"), 200, 1000);
			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_BcChatBubbleFadeIn, &g_Config.m_BcChatBubbleFadeIn, &Button, Localize("fade in for"), 15, 100);
			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_BcChatBubbleFadeOut, &g_Config.m_BcChatBubbleFadeOut, &Button, Localize("fade out for"), 15, 100);
			Expand.HSplitTop(MarginSmall, nullptr, &Expand);
		}
		}

		s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
	}
	
	// 3D Particles
	if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_3D_PARTICLES))
	{
		Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		s_SectionBoxes.push_back(Column);
		Column.HSplitTop(HeadlineHeight, &Label, &Column);
		if(SettingsSearchConsumeReveal("3D Particles"))
			s_ScrollRegion.AddRect(Label, true);
		Ui()->DoLabel(&Label, TCLocalize("3D Particles"), HeadlineFontSize, TEXTALIGN_ML);
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_Bc3dParticles, TCLocalize("3D Particles"), &g_Config.m_Bc3dParticles, &Column, LineSize);

		static float s_Bc3dParticlesPhase = 0.0f;
		static float s_Bc3dParticlesGlowPhase = 0.0f;
		{
		const float Dt = Client()->RenderFrameTime();
		const bool ParticlesOn = g_Config.m_Bc3dParticles != 0;
		if(ModuleUiRevealAnimationsEnabled())
			BCUiAnimations::UpdatePhase(s_Bc3dParticlesPhase, ParticlesOn ? 1.0f : 0.0f, Dt, ModuleUiRevealAnimationDuration());
		else
			s_Bc3dParticlesPhase = ParticlesOn ? 1.0f : 0.0f;

		const bool GlowOn = ParticlesOn && g_Config.m_Bc3dParticlesGlow != 0;
		if(BCUiAnimations::Enabled())
			BCUiAnimations::UpdatePhase(s_Bc3dParticlesGlowPhase, GlowOn ? 1.0f : 0.0f, Dt, 0.16f);
		else
			s_Bc3dParticlesGlowPhase = GlowOn ? 1.0f : 0.0f;

		const float GlowTargetHeight = 2.0f * LineSize;
		const float BaseTargetHeight = MarginSmall + 6.0f * LineSize + ColorPickerLineSize + ColorPickerLineSpacing;
		const float TargetHeight = BaseTargetHeight + GlowTargetHeight * s_Bc3dParticlesGlowPhase;
		const float CurHeight = TargetHeight * s_Bc3dParticlesPhase;
		if(CurHeight > 0.0f)
		{
			CUIRect Visible;
			Column.HSplitTop(CurHeight, &Visible, &Column);
			Ui()->ClipEnable(&Visible);
			struct SScopedClip
			{
				CUi *m_pUi;
				~SScopedClip() { m_pUi->ClipDisable(); }
			} ClipGuard{Ui()};

			CUIRect Expand = {Visible.x, Visible.y, Visible.w, TargetHeight};
			Expand.HSplitTop(MarginSmall, nullptr, &Expand);
			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesCount, &g_Config.m_Bc3dParticlesCount, &Button, TCLocalize("Particles Count"), 1, 200);

			static CUi::SDropDownState s3DParticleTypeState;
			static CScrollRegion s3DParticleTypeScroll;
			s3DParticleTypeState.m_SelectionPopupContext.m_pScrollRegion = &s3DParticleTypeScroll;

			CUIRect ParticleType;
			Expand.HSplitTop(LineSize, &ParticleType, &Expand);
			ParticleType.VSplitLeft(150.0f, &Label, &ParticleType);
			Ui()->DoLabel(&Label, TCLocalize("Particle Type"), FontSize, TEXTALIGN_ML);
			std::array<const char *, 3> a3DParticleTypeNames = {TCLocalize("Cube"), TCLocalize("Heart"), TCLocalize("Mixed")};
			g_Config.m_Bc3dParticlesType = Ui()->DoDropDown(&ParticleType, g_Config.m_Bc3dParticlesType - 1, a3DParticleTypeNames.data(), (int)a3DParticleTypeNames.size(), s3DParticleTypeState) + 1;

			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesSizeMax, &g_Config.m_Bc3dParticlesSizeMax, &Button, TCLocalize("Size"), 2, 200);
			g_Config.m_Bc3dParticlesSizeMin = std::max(2, g_Config.m_Bc3dParticlesSizeMax - 3);
			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesSpeed, &g_Config.m_Bc3dParticlesSpeed, &Button, TCLocalize("Speed"), 1, 500);
			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesAlpha, &g_Config.m_Bc3dParticlesAlpha, &Button, TCLocalize("Alpha"), 1, 100);
			static CButtonContainer s_3dParticlesColor;
			DoLine_ColorPicker(&s_3dParticlesColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Expand, TCLocalize("Color"), &g_Config.m_Bc3dParticlesColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_Bc3dParticlesGlow, TCLocalize("Glow"), &g_Config.m_Bc3dParticlesGlow, &Expand, LineSize);
			const float GlowCurHeight = GlowTargetHeight * s_Bc3dParticlesGlowPhase;
			if(GlowCurHeight > 0.0f)
			{
				CUIRect GlowVisible;
				Expand.HSplitTop(GlowCurHeight, &GlowVisible, &Expand);
				Ui()->ClipEnable(&GlowVisible);
				SScopedClip GlowClipGuard{Ui()};

				CUIRect GlowExpand = {GlowVisible.x, GlowVisible.y, GlowVisible.w, GlowTargetHeight};
				GlowExpand.HSplitTop(LineSize, &Button, &GlowExpand);
				Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesGlowAlpha, &g_Config.m_Bc3dParticlesGlowAlpha, &Button, TCLocalize("Glow Alpha"), 1, 100);
				GlowExpand.HSplitTop(LineSize, &Button, &GlowExpand);
				Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesGlowOffset, &g_Config.m_Bc3dParticlesGlowOffset, &Button, TCLocalize("Glow Offset"), 1, 20);
			}
		}
		}

		s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
	}

	// Aspect Ratio
	if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_ASPECT_RATIO))
	{
		Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		s_SectionBoxes.push_back(Column);
		Column.HSplitTop(HeadlineHeight, &Label, &Column);
		if(SettingsSearchConsumeReveal("Aspect Ratio"))
			s_ScrollRegion.AddRect(Label, true);
		Ui()->DoLabel(&Label, TCLocalize("Aspect Ratio"), HeadlineFontSize, TEXTALIGN_ML);
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		std::array<const char *, 5> aAspectPresetNames = {TCLocalize("Off (default)"), "5:4", "4:3", "3:2", TCLocalize("Custom")};
		static const std::vector<int> sAspectPresetValues = {0, 125, 133, 150};
		static CUi::SDropDownState sAspectPresetState;
		static CScrollRegion sAspectPresetScroll;
		sAspectPresetState.m_SelectionPopupContext.m_pScrollRegion = &sAspectPresetScroll;
		const int AspectMode = g_Config.m_BcCustomAspectRatioMode >= 0 ? g_Config.m_BcCustomAspectRatioMode : (g_Config.m_BcCustomAspectRatio > 0 ? 1 : 0);
		const int CustomPresetIndex = (int)aAspectPresetNames.size() - 1;

		auto GetAspectPresetIndex = [&]() -> int {
			if(AspectMode <= 0 || g_Config.m_BcCustomAspectRatio == 0)
				return 0;
			if(AspectMode == 2)
				return CustomPresetIndex;

			for(size_t i = 1; i < sAspectPresetValues.size(); ++i)
			{
				if(g_Config.m_BcCustomAspectRatio == sAspectPresetValues[i])
					return (int)i;
			}

			int BestIndex = 1;
			int BestDiff = g_Config.m_BcCustomAspectRatio > sAspectPresetValues[BestIndex] ? g_Config.m_BcCustomAspectRatio - sAspectPresetValues[BestIndex] : sAspectPresetValues[BestIndex] - g_Config.m_BcCustomAspectRatio;
			for(size_t i = 2; i < sAspectPresetValues.size(); ++i)
			{
				const int CurDiff = g_Config.m_BcCustomAspectRatio > sAspectPresetValues[i] ? g_Config.m_BcCustomAspectRatio - sAspectPresetValues[i] : sAspectPresetValues[i] - g_Config.m_BcCustomAspectRatio;
				if(CurDiff < BestDiff)
				{
					BestDiff = CurDiff;
					BestIndex = (int)i;
				}
			}
			return BestIndex;
		};

		const int CurrentPreset = GetAspectPresetIndex();
		CUIRect AspectPreset;
		Column.HSplitTop(LineSize, &AspectPreset, &Column);
		AspectPreset.VSplitLeft(170.0f, &Label, &AspectPreset);
		Ui()->DoLabel(&Label, TCLocalize("Preset"), FontSize, TEXTALIGN_ML);
		const int NewPreset = Ui()->DoDropDown(&AspectPreset, CurrentPreset, aAspectPresetNames.data(), (int)aAspectPresetNames.size(), sAspectPresetState);
		if(NewPreset != CurrentPreset)
		{
			if(NewPreset == 0)
			{
				g_Config.m_BcCustomAspectRatioMode = 0;
				g_Config.m_BcCustomAspectRatio = 0;
			}
			else if(NewPreset == CustomPresetIndex)
			{
				g_Config.m_BcCustomAspectRatioMode = 2;
				if(g_Config.m_BcCustomAspectRatio < 100)
					g_Config.m_BcCustomAspectRatio = 177;
			}
			else
			{
				g_Config.m_BcCustomAspectRatioMode = 1;
				g_Config.m_BcCustomAspectRatio = sAspectPresetValues[NewPreset];
			}
			GameClient()->m_BestClient.ReloadWindowModeForAspect();
		}

		if((g_Config.m_BcCustomAspectRatioMode >= 0 ? g_Config.m_BcCustomAspectRatioMode : (g_Config.m_BcCustomAspectRatio > 0 ? 1 : 0)) == 2)
		{
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			CUIRect AspectSlider;
			Column.HSplitTop(LineSize, &AspectSlider, &Column);

			const int AspectValue = std::clamp(g_Config.m_BcCustomAspectRatio >= 100 ? g_Config.m_BcCustomAspectRatio : 177, 100, 300);
			char aAspectBuf[64];
			str_format(aAspectBuf, sizeof(aAspectBuf), "%s: %.2f", TCLocalize("Stretch"), AspectValue / 100.0f);

			CUIRect SliderLabel, SliderBar;
			AspectSlider.VSplitMid(&SliderLabel, &SliderBar, minimum(10.0f, AspectSlider.w * 0.05f));
			Ui()->DoLabel(&SliderLabel, aAspectBuf, SliderLabel.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_ML);

			const int NewAspectValue = CUi::ms_LinearScrollbarScale.ToAbsolute(Ui()->DoScrollbarH(&g_Config.m_BcCustomAspectRatio, &SliderBar, CUi::ms_LinearScrollbarScale.ToRelative(AspectValue, 100, 300)), 100, 300);
			if(NewAspectValue != g_Config.m_BcCustomAspectRatio)
			{
				g_Config.m_BcCustomAspectRatio = NewAspectValue;
				GameClient()->m_BestClient.ReloadWindowModeForAspect();
			}
		}
		s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
		Column.HSplitTop(MarginSmall, nullptr, &Column);
	}

	// ***** END OF PAGE 1 SETTINGS ***** //
	RightView = Column;

	// Scroll
	CUIRect ScrollRegion;
	ScrollRegion.x = MainView.x;
	ScrollRegion.y = maximum(LeftView.y, RightView.y) + MarginSmall * 2.0f;
	ScrollRegion.w = MainView.w;
	ScrollRegion.h = 0.0f;
	s_ScrollRegion.AddRect(ScrollRegion);
	s_ScrollRegion.End();
	m_pSettingsSearchActiveScrollRegion = nullptr;
}

void CMenus::RenderSettingsBestClientRaceFeatures(CUIRect MainView)
{
	CUIRect Column, LeftView, RightView, Button, Label;

	static CScrollRegion s_ScrollRegion;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	s_ScrollRegion.Begin(&MainView, &ScrollOffset, &ScrollParams);
	m_pSettingsSearchActiveScrollRegion = &s_ScrollRegion;

	static std::vector<CUIRect> s_SectionBoxes;
	static vec2 s_PrevScrollOffset(0.0f, 0.0f);

	MainView.y += ScrollOffset.y;

	MainView.VSplitRight(5.0f, &MainView, nullptr); // Padding for scrollbar
	MainView.VSplitLeft(5.0f, nullptr, &MainView); // Padding for scrollbar

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	for(CUIRect &Section : s_SectionBoxes)
	{
		float Padding = MarginBetweenViews * 0.6666f;
		Section.w += Padding;
		Section.h += Padding;
		Section.x -= Padding * 0.5f;
		Section.y -= Padding * 0.5f;
		Section.y -= s_PrevScrollOffset.y - ScrollOffset.y;
		Section.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 10.0f);
	}
	s_PrevScrollOffset = ScrollOffset;
	s_SectionBoxes.clear();

	// ***** LeftView ***** //
	Column = LeftView;

	// ***** BindSystem ***** //
	if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_RACE_BINDSYSTEM))
	{
		Column.HSplitTop(Margin, nullptr, &Column);
		s_SectionBoxes.push_back(Column);
		Column.HSplitTop(HeadlineHeight, &Label, &Column);
		if(SettingsSearchConsumeReveal("BindSystem"))
			s_ScrollRegion.AddRect(Label, true);
		SettingsSearchDrawLabel(&Label, TCLocalize("BindSystem"), HeadlineFontSize, TEXTALIGN_ML, "BindSystem");
		Column.HSplitTop(MarginSmall, nullptr, &Column);

	static char s_aBindCommand[BINDSYSTEM_MAX_CMD];
	static int s_SelectedBindIndex = 0;
	static int s_LastSelectedBindIndex = -1;
	int HoveringIndex = -1;

	CUIRect WheelPreview;
	Column.HSplitTop(minimum(96.0f, Column.h * 0.18f), &WheelPreview, &Column);
	const vec2 Center = WheelPreview.Center();
	const float LineInset = 18.0f;
	const float LineHalfWidth = maximum(40.0f, WheelPreview.w / 2.0f - LineInset);
	const float LineHeight = minimum(WheelPreview.h * 0.78f, 44.0f);
	const float SelectBandHalfHeight = LineHeight * 1.2f;
	const float LabelW = 52.0f;
	const float LabelH = 52.0f;
	const float TextHalfRange = maximum(0.0f, LineHalfWidth - LabelW / 2.0f - 2.0f);

	Graphics()->DrawRect(Center.x - LineHalfWidth, Center.y - LineHeight / 2.0f, LineHalfWidth * 2.0f, LineHeight, ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f), IGraphics::CORNER_ALL, 8.0f);

	const vec2 MouseDelta = Ui()->MousePos() - Center;
	const int SegmentCount = static_cast<int>(GameClient()->m_BindSystem.m_vBinds.size());
	const bool HoverInsideLine = absolute(MouseDelta.x) <= LineHalfWidth && absolute(MouseDelta.y) <= SelectBandHalfHeight;
	if(HoverInsideLine && SegmentCount > 0)
	{
		const float HoverPos01 = TextHalfRange > 0.0f ? (MouseDelta.x + TextHalfRange) / (2.0f * TextHalfRange) : 0.5f;
		HoveringIndex = std::clamp((int)std::round(HoverPos01 * (SegmentCount - 1)), 0, SegmentCount - 1);

		if(Ui()->MouseButtonClicked(0) || Ui()->MouseButtonClicked(2))
		{
			s_SelectedBindIndex = HoveringIndex;
			str_copy(s_aBindCommand, GameClient()->m_BindSystem.m_vBinds[HoveringIndex].m_aCommand);
		}
	}
	s_SelectedBindIndex = std::clamp(s_SelectedBindIndex, 0, maximum(0, SegmentCount - 1));
	if(s_SelectedBindIndex != s_LastSelectedBindIndex &&
		s_SelectedBindIndex < static_cast<int>(GameClient()->m_BindSystem.m_vBinds.size()))
	{
		str_copy(s_aBindCommand, GameClient()->m_BindSystem.m_vBinds[s_SelectedBindIndex].m_aCommand);
		s_LastSelectedBindIndex = s_SelectedBindIndex;
	}

	for(int i = 0; i < static_cast<int>(GameClient()->m_BindSystem.m_vBinds.size()); i++)
	{
		TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
		float SegmentFontSize = FontSize * 1.1f;
		if(i == s_SelectedBindIndex)
		{
			SegmentFontSize = FontSize * 1.7f;
			TextRender()->TextColor(ColorRGBA(0.5f, 1.0f, 0.75f, 1.0f));
		}
		else if(i == HoveringIndex)
		{
			SegmentFontSize = FontSize * 1.35f;
		}

		const CBindSystem::CBind Bind = GameClient()->m_BindSystem.m_vBinds[i];
		const float Pos01 = GameClient()->m_BindSystem.m_vBinds.size() <= 1 ? 0.5f : (float)i / (float)(GameClient()->m_BindSystem.m_vBinds.size() - 1);
		const vec2 Pos = vec2(Center.x - TextHalfRange + Pos01 * (TextHalfRange * 2.0f), Center.y);
		const CUIRect Rect = CUIRect{Pos.x - LabelW / 2.0f, Pos.y - LabelH / 2.0f, LabelW, LabelH};
		Ui()->DoLabel(&Rect, Bind.m_aName, SegmentFontSize, TEXTALIGN_MC);
	}
	TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	Column.HSplitTop(LineSize, &Button, &Column);
	char aSlotLabel[64];
	str_format(aSlotLabel, sizeof(aSlotLabel), "%s %d", TCLocalize("Selected slot"), s_SelectedBindIndex + 1);
	Ui()->DoLabel(&Button, aSlotLabel, FontSize, TEXTALIGN_ML);

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	Column.HSplitTop(LineSize, &Button, &Column);
	Button.VSplitLeft(150.0f, &Label, &Button);
	Ui()->DoLabel(&Label, TCLocalize("Command:"), FontSize, TEXTALIGN_ML);
	static CLineInput s_BindInput;
	s_BindInput.SetBuffer(s_aBindCommand, sizeof(s_aBindCommand));
	s_BindInput.SetEmptyText(TCLocalize("Command"));
	Ui()->DoEditBox(&s_BindInput, &Button, EditBoxFontSize);

	if(s_SelectedBindIndex < static_cast<int>(GameClient()->m_BindSystem.m_vBinds.size()))
	{
		// Auto-save command while editing, no extra "Set" click required.
		str_copy(GameClient()->m_BindSystem.m_vBinds[s_SelectedBindIndex].m_aCommand, s_aBindCommand);
	}

	static CButtonContainer s_ClearButton;
	Column.HSplitTop(MarginSmall, nullptr, &Column);
	Column.HSplitTop(LineSize, &Button, &Column);
	if(DoButton_Menu(&s_ClearButton, TCLocalize("Clear Command"), 0, &Button) &&
		s_SelectedBindIndex < static_cast<int>(GameClient()->m_BindSystem.m_vBinds.size()))
	{
		GameClient()->m_BindSystem.m_vBinds[s_SelectedBindIndex].m_aCommand[0] = '\0';
		s_aBindCommand[0] = '\0';
	}

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	Column.HSplitTop(LineSize * 0.8f, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("In game: hold bind key, press 1..6, release key to execute"), FontSize * 0.8f, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	Column.HSplitTop(LineSize, &Label, &Column);
	static CButtonContainer s_ReaderButtonWheel, s_ClearButtonWheel;
	DoLine_KeyReader(Label, s_ReaderButtonWheel, s_ClearButtonWheel, TCLocalize("BindSystem Key"), "+bs");

		s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
	}

	// ***** RightView ***** //
	LeftView = Column;
	Column = RightView;

	// Speedrun Timer
	if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_RACE_SPEEDRUN_TIMER))
	{
		Column.HSplitTop(Margin, nullptr, &Column);
		s_SectionBoxes.push_back(Column);
		Column.HSplitTop(HeadlineHeight, &Label, &Column);
		if(SettingsSearchConsumeReveal("Speedrun Timer"))
			s_ScrollRegion.AddRect(Label, true);
		SettingsSearchDrawLabel(&Label, TCLocalize("Speedrun Timer"), HeadlineFontSize, TEXTALIGN_MC, "Speedrun Timer");
		Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcSpeedrunTimer, TCLocalize("Enable speedrun timer"), &g_Config.m_BcSpeedrunTimer, &Column, LineSize);
	{
		static float s_SpeedrunTimerPhase = 0.0f;
		const float Dt = Client()->RenderFrameTime();
		const bool Enabled = g_Config.m_BcSpeedrunTimer != 0;
		if(ModuleUiRevealAnimationsEnabled())
			BCUiAnimations::UpdatePhase(s_SpeedrunTimerPhase, Enabled ? 1.0f : 0.0f, Dt, ModuleUiRevealAnimationDuration());
		else
			s_SpeedrunTimerPhase = Enabled ? 1.0f : 0.0f;

		const float TargetHeight = 5.0f * LineSize + 5.0f * MarginSmall;
		const float CurHeight = TargetHeight * s_SpeedrunTimerPhase;
		if(CurHeight > 0.0f)
		{
			CUIRect Visible;
			Column.HSplitTop(CurHeight, &Visible, &Column);
			Ui()->ClipEnable(&Visible);
			struct SScopedClip
			{
				CUi *m_pUi;
				~SScopedClip() { m_pUi->ClipDisable(); }
			} ClipGuard{Ui()};

			CUIRect Expand = {Visible.x, Visible.y, Visible.w, TargetHeight};
			Expand.HSplitTop(MarginSmall, nullptr, &Expand);

			if(g_Config.m_BcSpeedrunTimerHours == 0 &&
				g_Config.m_BcSpeedrunTimerMinutes == 0 &&
				g_Config.m_BcSpeedrunTimerSeconds == 0 &&
				g_Config.m_BcSpeedrunTimerMilliseconds == 0 &&
				g_Config.m_BcSpeedrunTimerTime > 0)
			{
				const int LegacyMinutes = g_Config.m_BcSpeedrunTimerTime / 100;
				const int LegacySeconds = g_Config.m_BcSpeedrunTimerTime % 100;
				const int TotalLegacySeconds = LegacyMinutes * 60 + LegacySeconds;
				g_Config.m_BcSpeedrunTimerHours = TotalLegacySeconds / 3600;
				g_Config.m_BcSpeedrunTimerMinutes = (TotalLegacySeconds % 3600) / 60;
				g_Config.m_BcSpeedrunTimerSeconds = TotalLegacySeconds % 60;
			}

			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_BcSpeedrunTimerHours, &g_Config.m_BcSpeedrunTimerHours, &Button, TCLocalize("Hours"), 0, 99);
			Expand.HSplitTop(MarginSmall, nullptr, &Expand);

			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_BcSpeedrunTimerMinutes, &g_Config.m_BcSpeedrunTimerMinutes, &Button, TCLocalize("Minutes"), 0, 59);
			Expand.HSplitTop(MarginSmall, nullptr, &Expand);

			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_BcSpeedrunTimerSeconds, &g_Config.m_BcSpeedrunTimerSeconds, &Button, TCLocalize("Seconds"), 0, 59);
			Expand.HSplitTop(MarginSmall, nullptr, &Expand);

			Expand.HSplitTop(LineSize, &Button, &Expand);
			Ui()->DoScrollbarOption(&g_Config.m_BcSpeedrunTimerMilliseconds, &g_Config.m_BcSpeedrunTimerMilliseconds, &Button, TCLocalize("Milliseconds"), 0, 999, &CUi::ms_LinearScrollbarScale, 0, "ms");
			Expand.HSplitTop(MarginSmall, nullptr, &Expand);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcSpeedrunTimerAutoDisable, TCLocalize("Auto disable after time end"), &g_Config.m_BcSpeedrunTimerAutoDisable, &Expand, LineSize);

			// Keep legacy MMSS config synchronized for backward compatibility.
			g_Config.m_BcSpeedrunTimerTime = g_Config.m_BcSpeedrunTimerMinutes * 100 + g_Config.m_BcSpeedrunTimerSeconds;
		}
	}

		s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
	}

	// ***** Auto Team Lock ***** //
	if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_RACE_AUTO_TEAM_LOCK))
	{
		Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		s_SectionBoxes.push_back(Column);
		Column.HSplitTop(HeadlineHeight, &Label, &Column);
		if(SettingsSearchConsumeReveal("Auto Team Lock"))
			s_ScrollRegion.AddRect(Label, true);
		SettingsSearchDrawLabel(&Label, TCLocalize("Auto Team Lock"), HeadlineFontSize, TEXTALIGN_MC, "Auto Team Lock");
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcAutoTeamLock, TCLocalize("Lock team automatically after joining"), &g_Config.m_BcAutoTeamLock, &Column, LineSize);
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_BcAutoTeamLockDelay, &g_Config.m_BcAutoTeamLockDelay, &Button, TCLocalize("Delay"), 0, 30, &CUi::ms_LinearScrollbarScale, 0, "s");
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcAutoDummyJoin, TCLocalize("Auto join main and dummy to the same team"), &g_Config.m_BcAutoDummyJoin, &Column, LineSize);

		s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
	}

	// ***** END OF PAGE 1 SETTINGS ***** //
	RightView = Column;

	// Scroll
	CUIRect ScrollRegion;
	ScrollRegion.x = MainView.x;
	ScrollRegion.y = maximum(LeftView.y, RightView.y) + MarginSmall * 2.0f;
	ScrollRegion.w = MainView.w;
	ScrollRegion.h = 0.0f;
	s_ScrollRegion.AddRect(ScrollRegion);
	s_ScrollRegion.End();
	m_pSettingsSearchActiveScrollRegion = nullptr;
}

void CMenus::RenderSettingsBestClientInfo(CUIRect MainView)
{
	CUIRect LeftView, RightView, Button, Label, LowerLeftView;
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);
	LeftView.HSplitMid(&LeftView, &LowerLeftView, 0.0f);

	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("BestClient Links"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	static CButtonContainer s_DiscordButton, s_WebsiteButton, s_TelegramButton, s_CheckUpdateButton;
	CUIRect ButtonLeft, ButtonRight;

	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&ButtonLeft, &ButtonRight, MarginSmall);
	if(DoButtonLineSize_Menu(&s_DiscordButton, TCLocalize("Discord"), 0, &ButtonLeft, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://discord.gg/bestclient");
	if(DoButtonLineSize_Menu(&s_TelegramButton, TCLocalize("Telegram"), 0, &ButtonRight, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://t.me/bestddnet");

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&ButtonLeft, &ButtonRight, MarginSmall);

	if(DoButtonLineSize_Menu(&s_WebsiteButton, TCLocalize("Website"), 0, &ButtonLeft, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://bestclient.fun");
	if(DoButtonLineSize_Menu(&s_CheckUpdateButton, TCLocalize("Check update"), 0, &ButtonRight, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		GameClient()->m_BestClient.FetchBestClientInfo();

#if defined(CONF_AUTOUPDATE)
	const bool NeedUpdate = GameClient()->m_BestClient.NeedUpdate();
	const IUpdater::EUpdaterState UpdateState = Updater()->GetCurrentState();
	const bool ShowDownloadButton = NeedUpdate && UpdateState == IUpdater::CLEAN;
	const bool ShowRetryButton = NeedUpdate && UpdateState == IUpdater::FAIL;
	const bool ShowRestartButton = UpdateState == IUpdater::NEED_RESTART;
	const bool ShowUpdateProgress = UpdateState >= IUpdater::GETTING_MANIFEST && UpdateState < IUpdater::NEED_RESTART;
	if(ShowDownloadButton || ShowRetryButton || ShowRestartButton || ShowUpdateProgress || UpdateState == IUpdater::FAIL)
	{
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
		LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
		Button.VSplitMid(&ButtonLeft, &ButtonRight, MarginSmall);

		char aUpdateLabel[128] = "";
		if(ShowDownloadButton)
			str_format(aUpdateLabel, sizeof(aUpdateLabel), "BestClient %s Is release", GameClient()->m_BestClient.m_aVersionStr);
		else if(ShowUpdateProgress)
		{
			if(UpdateState == IUpdater::GETTING_MANIFEST)
				str_copy(aUpdateLabel, TCLocalize("Preparing update..."), sizeof(aUpdateLabel));
			else
				str_format(aUpdateLabel, sizeof(aUpdateLabel), "%s %d%%", TCLocalize("Downloading"), Updater()->GetCurrentPercent());
		}
		else if(ShowRestartButton)
			str_copy(aUpdateLabel, TCLocalize("Update downloaded"), sizeof(aUpdateLabel));
		else
			str_copy(aUpdateLabel, TCLocalize("Update failed"), sizeof(aUpdateLabel));

		if(ShowDownloadButton)
			TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
		Ui()->DoLabel(&ButtonLeft, aUpdateLabel, HeadlineFontSize / 1.6f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		if(ShowDownloadButton || ShowRetryButton)
		{
			static CButtonContainer s_DownloadUpdateButton;
			if(DoButtonLineSize_Menu(&s_DownloadUpdateButton, TCLocalize("Download"), 0, &ButtonRight, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
				Updater()->InitiateUpdate();
		}
		else if(ShowRestartButton)
		{
			static CButtonContainer s_RestartUpdateButton;
			if(DoButtonLineSize_Menu(&s_RestartUpdateButton, TCLocalize("Restart"), 0, &ButtonRight, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
				Updater()->ApplyUpdateAndRestart();
		}
		else if(ShowUpdateProgress)
		{
			Ui()->RenderProgressBar(ButtonRight, Updater()->GetCurrentPercent() / 100.0f);
		}
	}
#endif

	LeftView = LowerLeftView;
	LeftView.HSplitBottom(LineSize * 4.0f + MarginSmall * 2.0f + HeadlineFontSize, nullptr, &LeftView);
	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("Config Files"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	char aBuf[128 + IO_MAX_PATH_LENGTH];
	CUIRect BestClientConfig, LoadTClientButton;

	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	BestClientConfig = Button;

	static CButtonContainer s_Config, s_LoadTClient;
	if(DoButtonLineSize_Menu(&s_Config, TCLocalize("BestClient Settings"), 0, &BestClientConfig, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::BestClient].m_aConfigPath, aBuf, sizeof(aBuf));
		Client()->ViewFile(aBuf);
	}
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize * 2.0f, &LoadTClientButton, &LeftView);
	if(DoButtonLineSize_Menu(&s_LoadTClient, TCLocalize("Load tclient settings"), 0, &LoadTClientButton, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		PopupConfirm(TCLocalize("Load tclient settings"), TCLocalize("Are you sure you want to load all settings from tclient?"), Localize("Yes"), Localize("No"), &CMenus::PopupConfirmLoadTClientSettings);
	}

	// =======RIGHT VIEW========

	RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("BestClient Developers"), HeadlineFontSize, TEXTALIGN_ML);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);

	const float TeeSize = MenuPreviewTeeLarge;
	const float CardSize = TeeSize + MarginSmall * 2.0f;
	CUIRect TeeRect, DevCardRect;
	static CButtonContainer s_LinkButton1, s_LinkButton2, s_LinkButton3;
	{
		RightView.HSplitTop(CardSize, &DevCardRect, &RightView);
		DevCardRect.VSplitLeft(CardSize, &TeeRect, &Label);
		Label.VSplitLeft(TextRender()->TextWidth(LineSize, "RoflikBEST"), &Label, &Button);
		Button.VSplitLeft(MarginSmall, nullptr, &Button);
		Button.w = LineSize, Button.h = LineSize, Button.y = Label.y + (Label.h / 2.0f - Button.h / 2.0f);
		Ui()->DoLabel(&Label, "RoflikBEST", LineSize, TEXTALIGN_ML);
		if(Ui()->DoButton_FontIcon(&s_LinkButton1, FONT_ICON_ARROW_UP_RIGHT_FROM_SQUARE, 0, &Button, IGraphics::CORNER_ALL))
			Client()->ViewLink("https://github.com/roflikbest");
		RenderDevSkin(TeeRect.Center(), MenuPreviewTeeLarge, "mushkitt", "mushkitt", false, 0, 0, 0, false, true);
	}
	{
		RightView.HSplitTop(CardSize, &DevCardRect, &RightView);
		DevCardRect.VSplitLeft(CardSize, &TeeRect, &Label);
		Label.VSplitLeft(TextRender()->TextWidth(LineSize, "noxygalaxy"), &Label, &Button);
		Button.VSplitLeft(MarginSmall, nullptr, &Button);
		Button.w = LineSize, Button.h = LineSize, Button.y = Label.y + (Label.h / 2.0f - Button.h / 2.0f);
		Ui()->DoLabel(&Label, "noxygalaxy", LineSize, TEXTALIGN_ML);
		if(Ui()->DoButton_FontIcon(&s_LinkButton3, FONT_ICON_ARROW_UP_RIGHT_FROM_SQUARE, 0, &Button, IGraphics::CORNER_ALL))
			Client()->ViewLink("https://github.com/noxygalaxy");
		RenderDevSkin(TeeRect.Center(), MenuPreviewTeeLarge, "Niko_OneShot", "Niko_OneShot", false, 0, 0, 0, false, true, true);
	}
	{
		RightView.HSplitTop(CardSize, &DevCardRect, &RightView);
		DevCardRect.VSplitLeft(CardSize, &TeeRect, &Label);
		Label.VSplitLeft(TextRender()->TextWidth(LineSize, "sqwinix"), &Label, &Button);
		Button.VSplitLeft(MarginSmall, nullptr, &Button);
		Button.w = LineSize, Button.h = LineSize, Button.y = Label.y + (Label.h / 2.0f - Button.h / 2.0f);
		Ui()->DoLabel(&Label, "sqwinix", LineSize, TEXTALIGN_ML);
		if(Ui()->DoButton_FontIcon(&s_LinkButton2, FONT_ICON_ARROW_UP_RIGHT_FROM_SQUARE, 0, &Button, IGraphics::CORNER_ALL))
			Client()->ViewLink("https://github.com/sqwinixxx");
		RenderDevSkin(TeeRect.Center(), MenuPreviewTeeLarge, "sticker_nanami", "sticker_nanami", true, 0, 0, 0, false, true, ColorRGBA(1.00f, 1.00f, 1.00f, 1.00f), ColorRGBA(1.00f, 1.00f, 1.00f, 1.00f));
	}

	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("Interface"), HeadlineFontSize, TEXTALIGN_ML);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);

	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("Hide Settings Tabs"), HeadlineFontSize, TEXTALIGN_ML);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	CUIRect LeftSettings, RightSettings;

	RightView.VSplitMid(&LeftSettings, &RightSettings, MarginSmall);

	const char *apTabNames[] = {
		TCLocalize("Visuals"),
		"Gameplay",
		TCLocalize("Others"),
		TCLocalize("Fun"),
		"",
		TCLocalize("Texture Shop"),
		TCLocalize("Voice"),
		TCLocalize("Info")};
	static CButtonContainer s_aShowTabButtons[NUMBER_OF_BestClient_TABS] = {};
	int HideableTabCount = 0;
	int HideableVisibleIndex = 0;
	for(int i = 0; i < NUMBER_OF_BestClient_TABS; ++i)
	{
		if(i == BestClient_TAB_INFO || i == BestClient_TAB_SAVESYSTEM || BestClientTabAutoHiddenByComponents(GameClient()->m_BestClient, i))
			continue;
		++HideableTabCount;
		int Hidden = IsFlagSet(g_Config.m_BcBestClientSettingsTabs, i);
		CUIRect *pColumn = HideableVisibleIndex % 2 == 0 ? &LeftSettings : &RightSettings;
		DoButton_CheckBoxAutoVMarginAndSet(&s_aShowTabButtons[i], apTabNames[i], &Hidden, pColumn, LineSize);
		SetFlag(g_Config.m_BcBestClientSettingsTabs, i, Hidden);
		++HideableVisibleIndex;
	}
	const int HideableRows = (HideableTabCount + 1) / 2;
	RightView.HSplitTop(LineSize * (HideableRows + 0.5f), nullptr, &RightView);

	// RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	// Ui()->DoLabel(&Label, TCLocalize("Integration"), HeadlineFontSize, TEXTALIGN_ML);
	// RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	// DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcDiscordRPC, TCLocalize("Enable Discord Integration"), &g_Config.m_TcDiscordRPC, &RightView, LineSize);
}

void CMenus::RenderSettingsBestClientVoice(CUIRect MainView)
{
	GameClient()->m_VoiceChat.RenderMenuPanel(MainView);
}

void CMenus::RenderSettingsBestClientProfiles(CUIRect MainView)
{
	int *pCurrentUseCustomColor = m_Dummy ? &g_Config.m_ClDummyUseCustomColor : &g_Config.m_ClPlayerUseCustomColor;

	const char *pCurrentSkinName = m_Dummy ? g_Config.m_ClDummySkin : g_Config.m_ClPlayerSkin;
	const unsigned CurrentColorBody = *pCurrentUseCustomColor == 1 ? (m_Dummy ? g_Config.m_ClDummyColorBody : g_Config.m_ClPlayerColorBody) : -1;
	const unsigned CurrentColorFeet = *pCurrentUseCustomColor == 1 ? (m_Dummy ? g_Config.m_ClDummyColorFeet : g_Config.m_ClPlayerColorFeet) : -1;
	const int CurrentFlag = m_Dummy ? g_Config.m_ClDummyCountry : g_Config.m_PlayerCountry;
	const int Emote = m_Dummy ? g_Config.m_ClDummyDefaultEyes : g_Config.m_ClPlayerDefaultEyes;
	const char *pCurrentName = m_Dummy ? g_Config.m_ClDummyName : g_Config.m_PlayerName;
	const char *pCurrentClan = m_Dummy ? g_Config.m_ClDummyClan : g_Config.m_PlayerClan;

	const CProfile CurrentProfile(
		CurrentColorBody,
		CurrentColorFeet,
		CurrentFlag,
		Emote,
		pCurrentSkinName,
		pCurrentName,
		pCurrentClan);

	static int s_SelectedProfile = -1;
	const float LargeProfileRowHeight = MenuPreviewTeeLarge + 8.0f;
	const float CompactProfileRowHeight = MenuPreviewTeeCompact + 12.0f;

	CUIRect Label, Button;

	auto RenderProfile = [&](CUIRect Rect, const CProfile &Profile, bool Main) {
		auto RenderCross = [&](CUIRect Cross, float MaxSize = 0.0f) {
			float MaxExtent = std::max(Cross.w, Cross.h);
			if(MaxSize > 0.0f && MaxExtent > MaxSize)
				MaxExtent = MaxSize;
			TextRender()->TextColor(ColorRGBA(1.0f, 0.0f, 0.0f));
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			const auto TextBoundingBox = TextRender()->TextBoundingBox(MaxExtent * 0.8f, FONT_ICON_XMARK);
			TextRender()->Text(Cross.x + (Cross.w - TextBoundingBox.m_W) / 2.0f, Cross.y + (Cross.h - TextBoundingBox.m_H) / 2.0f, MaxExtent * 0.8f, FONT_ICON_XMARK);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		};
		{
			CUIRect Skin;
			const float TeeSlotWidth = Main ? MenuPreviewTeeLarge : MenuPreviewTeeCompact;
			Rect.VSplitLeft(TeeSlotWidth, &Skin, &Rect);
			if(!Main && Profile.m_SkinName[0] == '\0')
			{
				RenderCross(Skin, 20.0f);
			}
			else
			{
				CTeeRenderInfo TeeRenderInfo;
				TeeRenderInfo.Apply(GameClient()->m_Skins.Find(Profile.m_SkinName));
				TeeRenderInfo.ApplyColors(Profile.m_BodyColor >= 0 && Profile.m_FeetColor > 0, Profile.m_BodyColor, Profile.m_FeetColor);
				TeeRenderInfo.m_Size = Main ? MenuPreviewTeeLarge : MenuPreviewTeeCompact;
				const vec2 Pos = Skin.Center() + vec2(0.0f, TeeRenderInfo.m_Size / 10.0f); // Prevent overflow from hats
				vec2 Dir = vec2(1.0f, 0.0f);
				if(Main)
					RenderTeeCute(CAnimState::GetIdle(), &TeeRenderInfo, std::max(0, Profile.m_Emote), Dir, Pos, false);
				else
					RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeRenderInfo, std::max(0, Profile.m_Emote), Dir, Pos);
			}
		}
		Rect.VSplitLeft(5.0f, nullptr, &Rect);
		{
			CUIRect Colors;
			Rect.VSplitLeft(10.0f, &Colors, &Rect);
			CUIRect BodyColor{Colors.Center().x - 5.0f, Colors.Center().y - 11.0f, 10.0f, 10.0f};
			CUIRect FeetColor{Colors.Center().x - 5.0f, Colors.Center().y + 1.0f, 10.0f, 10.0f};
			if(Profile.m_BodyColor >= 0 && Profile.m_FeetColor > 0)
			{
				// Body Color
				Graphics()->DrawRect(BodyColor.x, BodyColor.y, BodyColor.w, BodyColor.h,
					color_cast<ColorRGBA>(ColorHSLA(Profile.m_BodyColor).UnclampLighting(ColorHSLA::DARKEST_LGT)).WithAlpha(1.0f),
					IGraphics::CORNER_ALL, 2.0f);
				// Feet Color;
				Graphics()->DrawRect(FeetColor.x, FeetColor.y, FeetColor.w, FeetColor.h,
					color_cast<ColorRGBA>(ColorHSLA(Profile.m_FeetColor).UnclampLighting(ColorHSLA::DARKEST_LGT)).WithAlpha(1.0f),
					IGraphics::CORNER_ALL, 2.0f);
			}
			else
			{
				RenderCross(BodyColor);
				RenderCross(FeetColor);
			}
		}
		Rect.VSplitLeft(5.0f, nullptr, &Rect);
		{
			CUIRect Flag;
			Rect.VSplitRight(50.0f, &Rect, &Flag);
			Flag = {Flag.x, Flag.y + (Flag.h - 25.0f) / 2.0f, Flag.w, 25.0f};
			if(Profile.m_CountryFlag == -2)
				RenderCross(Flag, 20.0f);
			else
				GameClient()->m_CountryFlags.Render(Profile.m_CountryFlag, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), Flag.x, Flag.y, Flag.w, Flag.h);
		}
		Rect.VSplitRight(5.0f, &Rect, nullptr);
		{
			const float Height = Rect.h / 3.0f;
			if(Main)
			{
				char aBuf[256];
				Rect.HSplitTop(Height, &Label, &Rect);
				str_format(aBuf, sizeof(aBuf), TCLocalize("Name: %s"), Profile.m_Name);
				Ui()->DoLabel(&Label, aBuf, Height / LineSize * FontSize, TEXTALIGN_ML);
				Rect.HSplitTop(Height, &Label, &Rect);
				str_format(aBuf, sizeof(aBuf), TCLocalize("Clan: %s"), Profile.m_Clan);
				Ui()->DoLabel(&Label, aBuf, Height / LineSize * FontSize, TEXTALIGN_ML);
				Rect.HSplitTop(Height, &Label, &Rect);
				str_format(aBuf, sizeof(aBuf), TCLocalize("Skin: %s"), Profile.m_SkinName);
				Ui()->DoLabel(&Label, aBuf, Height / LineSize * FontSize, TEXTALIGN_ML);
			}
			else
			{
				Rect.HSplitTop(Height, &Label, &Rect);
				Ui()->DoLabel(&Label, Profile.m_Name, Height / LineSize * FontSize, TEXTALIGN_ML);
				Rect.HSplitTop(Height, &Label, &Rect);
				Ui()->DoLabel(&Label, Profile.m_Clan, Height / LineSize * FontSize, TEXTALIGN_ML);
			}
		}
	};

	{
		CUIRect Top;
		MainView.HSplitTop(190.0f, &Top, &MainView);
		CUIRect Profiles, Settings, Actions;
		Top.VSplitLeft(300.0f, &Profiles, &Top);
		{
			CUIRect Skin;
			Profiles.HSplitTop(LineSize, &Label, &Profiles);
			Ui()->DoLabel(&Label, TCLocalize("Your profile"), FontSize, TEXTALIGN_ML);
			Profiles.HSplitTop(MarginSmall, nullptr, &Profiles);
			Profiles.HSplitTop(LargeProfileRowHeight, &Skin, &Profiles);
			RenderProfile(Skin, CurrentProfile, true);

			// After load
			if(s_SelectedProfile != -1 && s_SelectedProfile < (int)GameClient()->m_SkinProfiles.m_Profiles.size())
			{
				Profiles.HSplitTop(MarginSmall, nullptr, &Profiles);
				Profiles.HSplitTop(LineSize, &Label, &Profiles);
				Ui()->DoLabel(&Label, TCLocalize("After Load"), FontSize, TEXTALIGN_ML);
				Profiles.HSplitTop(MarginSmall, nullptr, &Profiles);
				Profiles.HSplitTop(LargeProfileRowHeight, &Skin, &Profiles);

				CProfile LoadProfile = CurrentProfile;
				const CProfile &Profile = GameClient()->m_SkinProfiles.m_Profiles[s_SelectedProfile];
				if(g_Config.m_TcProfileSkin && strlen(Profile.m_SkinName) != 0)
					str_copy(LoadProfile.m_SkinName, Profile.m_SkinName);
				if(g_Config.m_TcProfileColors && Profile.m_BodyColor != -1 && Profile.m_FeetColor != -1)
				{
					LoadProfile.m_BodyColor = Profile.m_BodyColor;
					LoadProfile.m_FeetColor = Profile.m_FeetColor;
				}
				if(g_Config.m_TcProfileEmote && Profile.m_Emote != -1)
					LoadProfile.m_Emote = Profile.m_Emote;
				if(g_Config.m_TcProfileName && strlen(Profile.m_Name) != 0)
					str_copy(LoadProfile.m_Name, Profile.m_Name);
				if(g_Config.m_TcProfileClan && (strlen(Profile.m_Clan) != 0 || g_Config.m_TcProfileOverwriteClanWithEmpty))
					str_copy(LoadProfile.m_Clan, Profile.m_Clan);
				if(g_Config.m_TcProfileFlag && Profile.m_CountryFlag != -2)
					LoadProfile.m_CountryFlag = Profile.m_CountryFlag;

				RenderProfile(Skin, LoadProfile, true);
			}
		}
		Top.VSplitLeft(20.0f, nullptr, &Top);
		Top.VSplitMid(&Settings, &Actions, 20.0f);
		{
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileSkin, TCLocalize("Save/Load Skin"), &g_Config.m_TcProfileSkin, &Settings, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileColors, TCLocalize("Save/Load Colors"), &g_Config.m_TcProfileColors, &Settings, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileEmote, TCLocalize("Save/Load Emote"), &g_Config.m_TcProfileEmote, &Settings, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileName, TCLocalize("Save/Load Name"), &g_Config.m_TcProfileName, &Settings, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileClan, TCLocalize("Save/Load Clan"), &g_Config.m_TcProfileClan, &Settings, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileFlag, TCLocalize("Save/Load Flag"), &g_Config.m_TcProfileFlag, &Settings, LineSize);
		}
		{
			Actions.HSplitTop(30.0f, &Button, &Actions);
			static CButtonContainer s_LoadButton;
			if(DoButton_Menu(&s_LoadButton, TCLocalize("Load"), 0, &Button))
			{
				if(s_SelectedProfile != -1 && s_SelectedProfile < (int)GameClient()->m_SkinProfiles.m_Profiles.size())
				{
					CProfile LoadProfile = GameClient()->m_SkinProfiles.m_Profiles[s_SelectedProfile];
					GameClient()->m_SkinProfiles.ApplyProfile(m_Dummy, LoadProfile);
				}
			}
			Actions.HSplitTop(5.0f, nullptr, &Actions);

			Actions.HSplitTop(30.0f, &Button, &Actions);
			static CButtonContainer s_SaveButton;
			if(DoButton_Menu(&s_SaveButton, TCLocalize("Save"), 0, &Button))
			{
				GameClient()->m_SkinProfiles.AddProfile(
					g_Config.m_TcProfileColors ? CurrentColorBody : -1,
					g_Config.m_TcProfileColors ? CurrentColorFeet : -1,
					g_Config.m_TcProfileFlag ? CurrentFlag : -2,
					g_Config.m_TcProfileEmote ? Emote : -1,
					g_Config.m_TcProfileSkin ? pCurrentSkinName : "",
					g_Config.m_TcProfileName ? pCurrentName : "",
					g_Config.m_TcProfileClan ? pCurrentClan : "");
			}
			Actions.HSplitTop(5.0f, nullptr, &Actions);

			static int s_AllowDelete;
			DoButton_CheckBoxAutoVMarginAndSet(&s_AllowDelete, Localizable("Enable Deleting"), &s_AllowDelete, &Actions, LineSize);
			Actions.HSplitTop(5.0f, nullptr, &Actions);

			if(s_AllowDelete)
			{
				Actions.HSplitTop(30.0f, &Button, &Actions);
				static CButtonContainer s_DeleteButton;
				if(DoButton_Menu(&s_DeleteButton, TCLocalize("Delete"), 0, &Button))
					if(s_SelectedProfile != -1 && s_SelectedProfile < (int)GameClient()->m_SkinProfiles.m_Profiles.size())
						GameClient()->m_SkinProfiles.m_Profiles.erase(GameClient()->m_SkinProfiles.m_Profiles.begin() + s_SelectedProfile);
				Actions.HSplitTop(5.0f, nullptr, &Actions);

				Actions.HSplitTop(30.0f, &Button, &Actions);
				static CButtonContainer s_OverrideButton;
				if(DoButton_Menu(&s_OverrideButton, TCLocalize("Override"), 0, &Button))
				{
					if(s_SelectedProfile != -1 && s_SelectedProfile < (int)GameClient()->m_SkinProfiles.m_Profiles.size())
					{
						GameClient()->m_SkinProfiles.m_Profiles[s_SelectedProfile] = CProfile(
							g_Config.m_TcProfileColors ? CurrentColorBody : -1,
							g_Config.m_TcProfileColors ? CurrentColorFeet : -1,
							g_Config.m_TcProfileFlag ? CurrentFlag : -2,
							g_Config.m_TcProfileEmote ? Emote : -1,
							g_Config.m_TcProfileSkin ? pCurrentSkinName : "",
							g_Config.m_TcProfileName ? pCurrentName : "",
							g_Config.m_TcProfileClan ? pCurrentClan : "");
					}
				}
			}
		}
	}
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	{
		CUIRect Options;
		MainView.HSplitTop(LineSize, &Options, &MainView);

		Options.VSplitLeft(150.0f, &Button, &Options);
		if(DoButton_CheckBox(&m_Dummy, TCLocalize("Dummy"), m_Dummy, &Button))
			m_Dummy = 1 - m_Dummy;

		Options.VSplitLeft(150.0f, &Button, &Options);
		static int s_CustomColorId = 0;
		if(DoButton_CheckBox(&s_CustomColorId, TCLocalize("Custom colors"), *pCurrentUseCustomColor, &Button))
		{
			*pCurrentUseCustomColor = *pCurrentUseCustomColor ? 0 : 1;
			SetNeedSendInfo();
		}

		Button = Options;
		if(DoButton_CheckBox(&g_Config.m_TcProfileOverwriteClanWithEmpty, TCLocalize("Overwrite clan even if empty"), g_Config.m_TcProfileOverwriteClanWithEmpty, &Button))
			g_Config.m_TcProfileOverwriteClanWithEmpty = 1 - g_Config.m_TcProfileOverwriteClanWithEmpty;
	}
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	{
		CUIRect SelectorRect;
		MainView.HSplitBottom(LineSize + MarginSmall, &MainView, &SelectorRect);
		SelectorRect.HSplitTop(MarginSmall, nullptr, &SelectorRect);

		static CButtonContainer s_ProfilesFile;
		SelectorRect.VSplitLeft(130.0f, &Button, &SelectorRect);
		if(DoButton_Menu(&s_ProfilesFile, TCLocalize("Profiles file"), 0, &Button))
		{
			char aBuf[IO_MAX_PATH_LENGTH];
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::BestClientPROFILES].m_aConfigPath, aBuf, sizeof(aBuf));
			Client()->ViewFile(aBuf);
		}
	}

	const std::vector<CProfile> &ProfileList = GameClient()->m_SkinProfiles.m_Profiles;
	static CListBox s_ListBox;
	s_ListBox.DoStart(CompactProfileRowHeight, ProfileList.size(), MainView.w / 200.0f, 3, s_SelectedProfile, &MainView, true, IGraphics::CORNER_ALL, true);

	static bool s_Indexes[1024];

	for(size_t i = 0; i < ProfileList.size(); ++i)
	{
		CListboxItem Item = s_ListBox.DoNextItem(&s_Indexes[i], s_SelectedProfile >= 0 && (size_t)s_SelectedProfile == i);
		if(!Item.m_Visible)
			continue;

		RenderProfile(Item.m_Rect, ProfileList[i], false);
	}

	s_SelectedProfile = s_ListBox.DoEnd();
}

void CMenus::RenderSettingsBestClientConfigs(CUIRect MainView)
{
	// hi hello, this is a relatively self contained mess, sorry if you're forking or need to modify this -Tater

	struct SIntStage
	{
		int m_Value;
	};
	struct SStrStage
	{
		std::string m_Value;
	};
	struct SColStage
	{
		unsigned m_Value;
	};
	static std::unordered_map<const SConfigVariable *, SIntStage> s_StagedInts;
	static std::unordered_map<const SConfigVariable *, SStrStage> s_StagedStrs;
	static std::unordered_map<const SConfigVariable *, SColStage> s_StagedCols;

	struct SIntState
	{
		CLineInputNumber m_Input;
		int m_LastValue = 0;
		bool m_Inited = false;
	};
	struct SStrState
	{
		CLineInputBuffered<512> m_Input;
		bool m_Inited = false;
	};
	struct SColState
	{
		unsigned m_LastValue = 0;
		unsigned m_Working = 0;
		bool m_Inited = false;
	};
	static std::unordered_map<const SConfigVariable *, SIntState> s_IntInputs;
	static std::unordered_map<const SConfigVariable *, SStrState> s_StrInputs;
	static std::unordered_map<const SConfigVariable *, SColState> s_ColInputs;

	auto ClearStagedAndCaches = [&]() {
		s_StagedInts.clear();
		s_StagedStrs.clear();
		s_StagedCols.clear();
		s_IntInputs.clear();
		s_StrInputs.clear();
		s_ColInputs.clear();
	};

	size_t ChangesCount = 0;

	CUIRect ApplyBar, TopBar, ListArea;
	MainView.VSplitRight(5.0f, &MainView, nullptr); // padding for scrollbar
	MainView.VSplitLeft(5.0f, nullptr, &MainView);
	MainView.HSplitTop(LineSize + MarginSmall, &ApplyBar, &MainView);
	MainView.HSplitTop(LineSize + MarginSmall, &TopBar, &ListArea);
	ListArea.HSplitTop(MarginSmall, nullptr, &ListArea);

	static CLineInputBuffered<128> s_SearchInput;

	ChangesCount = s_StagedInts.size() + s_StagedStrs.size() + s_StagedCols.size();
	{
		CUIRect LeftHalf, RightHalf;
		ApplyBar.VSplitMid(&LeftHalf, &RightHalf, 0.0f);
		CUIRect Row = LeftHalf;
		Row.HMargin(MarginSmall, &Row);
		Row.h = LineSize;
		Row.y = ApplyBar.y + (ApplyBar.h - LineSize) / 2.0f;

		const float BtnWidth = 120.0f;
		CUIRect ApplyBtn, ClearBtn, Counter;
		Row.VSplitLeft(BtnWidth, &ApplyBtn, &Row);
		Row.VSplitLeft(MarginSmall, nullptr, &Row);
		Row.VSplitLeft(BtnWidth, &ClearBtn, &Row);
		Row.VSplitLeft(MarginSmall, nullptr, &Counter);

		static CButtonContainer s_ApplyBtn, s_ClearBtn;
		int DisabledStyle = ChangesCount > 0 ? 0 : -1;
		const bool ApplyClicked = DoButton_Menu(&s_ApplyBtn, Localize("Apply Changes"), DisabledStyle, &ApplyBtn);
		if(ChangesCount > 0 && ApplyClicked)
		{
			for(const auto &it : s_StagedInts)
			{
				const SConfigVariable *pVar = it.first;
				char aCmd[256];
				str_format(aCmd, sizeof(aCmd), "%s %d", pVar->m_pScriptName, it.second.m_Value);
				Console()->ExecuteLine(aCmd, IConsole::CLIENT_ID_UNSPECIFIED);
			}
			for(const auto &it : s_StagedStrs)
			{
				const SConfigVariable *pVar = it.first;
				char aEsc[1024];
				aEsc[0] = '\0';
				char *pDst = aEsc;
				str_escape(&pDst, it.second.m_Value.c_str(), aEsc + sizeof(aEsc));
				char aCmd[1200];
				str_format(aCmd, sizeof(aCmd), "%s \"%s\"", pVar->m_pScriptName, aEsc);
				Console()->ExecuteLine(aCmd, IConsole::CLIENT_ID_UNSPECIFIED);
			}
			for(const auto &it : s_StagedCols)
			{
				const SConfigVariable *pVar = it.first;
				char aCmd[256];
				str_format(aCmd, sizeof(aCmd), "%s %u", pVar->m_pScriptName, it.second.m_Value);
				Console()->ExecuteLine(aCmd, IConsole::CLIENT_ID_UNSPECIFIED);
			}
			ClearStagedAndCaches();
		}
		const bool ClearClicked = DoButton_Menu(&s_ClearBtn, Localize("Clear Changes"), DisabledStyle, &ClearBtn);
		if(ChangesCount > 0 && ClearClicked)
		{
			ClearStagedAndCaches();
		}

		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), Localize("Changes: %d"), (int)ChangesCount);
		Ui()->DoLabel(&Counter, aBuf, FontSize, TEXTALIGN_ML);

		CUIRect RightRow = RightHalf;
		RightRow.h = LineSize;
		RightRow.y = ApplyBar.y + (ApplyBar.h - LineSize) / 2.0f;
		const float RightInset = 24.0f;
		RightRow.VSplitLeft(RightInset, nullptr, &RightRow);
		CUIRect TopCol1, TopCol2;
		RightRow.VSplitMid(&TopCol1, &TopCol2, 0.0f);
		if(DoButton_CheckBox(&g_Config.m_BcUiShowBestClient, Localize("BestClient"), g_Config.m_BcUiShowBestClient, &TopCol1))
			g_Config.m_BcUiShowBestClient ^= 1;
		if(DoButton_CheckBox(&g_Config.m_TcUiCompactList, Localize("Compact List"), g_Config.m_TcUiCompactList, &TopCol2))
			g_Config.m_TcUiCompactList ^= 1;
	}

	const float SearchLabelW = 60.0f;
	{
		CUIRect SearchRow = TopBar;
		SearchRow.h = LineSize;
		SearchRow.y = TopBar.y + (TopBar.h - LineSize) / 2.0f;

		CUIRect LeftHalf, RightHalf;
		SearchRow.VSplitMid(&LeftHalf, &RightHalf, 0.0f);

		CUIRect SearchLabel, SearchEdit;
		LeftHalf.VSplitLeft(SearchLabelW, &SearchLabel, &SearchEdit);
		Ui()->DoLabel(&SearchLabel, Localize("Search"), FontSize, TEXTALIGN_ML);
		Ui()->DoClearableEditBox(&s_SearchInput, &SearchEdit, EditBoxFontSize);

		CUIRect RightCol1, RightCol2;
		const float RightInset2 = 24.0f;
		RightHalf.VSplitLeft(RightInset2, nullptr, &RightHalf);
		RightHalf.VSplitMid(&RightCol1, &RightCol2, 0.0f);
		if(DoButton_CheckBox(&g_Config.m_TcUiShowDDNet, Localize("DDNet"), g_Config.m_TcUiShowDDNet, &RightCol1))
			g_Config.m_TcUiShowDDNet ^= 1;
		if(DoButton_CheckBox(&g_Config.m_TcUiOnlyModified, Localize("Only modified"), g_Config.m_TcUiOnlyModified, &RightCol2))
			g_Config.m_TcUiOnlyModified ^= 1;
	}

	const int FlagMask = CFGFLAG_CLIENT;

	struct SEntry
	{
		const SConfigVariable *m_pVar;
	};
	std::vector<SEntry> vEntries;
	vEntries.reserve(256);

	auto Collector = [](const SConfigVariable *pVar, void *pUserData) {
		auto *pVec = static_cast<std::vector<SEntry> *>(pUserData);
		pVec->push_back({pVar});
	};
	ConfigManager()->PossibleConfigVariables("", FlagMask, Collector, &vEntries);

	auto DomainEnabled = [&](ConfigDomain Domain) {
		if(Domain == ConfigDomain::DDNET)
			return g_Config.m_TcUiShowDDNet != 0;
		if(Domain == ConfigDomain::BestClient)
			return g_Config.m_BcUiShowBestClient != 0;
		// only show DDNet and BestClient domains
		return false;
	};

	const char *pSearch = s_SearchInput.GetString();

	auto IsEffectiveDefaultVar = [&](const SConfigVariable *p) -> bool {
		if(p->m_Type == SConfigVariable::VAR_INT)
		{
			const SIntConfigVariable *pint = static_cast<const SIntConfigVariable *>(p);
			auto it = s_StagedInts.find(p);
			int v = it != s_StagedInts.end() ? it->second.m_Value : *pint->m_pVariable;
			return v == pint->m_Default;
		}
		if(p->m_Type == SConfigVariable::VAR_STRING)
		{
			const SStringConfigVariable *ps = static_cast<const SStringConfigVariable *>(p);
			auto it = s_StagedStrs.find(p);
			const char *v = it != s_StagedStrs.end() ? it->second.m_Value.c_str() : ps->m_pStr;
			return str_comp(v, ps->m_pDefault) == 0;
		}
		if(p->m_Type == SConfigVariable::VAR_COLOR)
		{
			const SColorConfigVariable *pc = static_cast<const SColorConfigVariable *>(p);
			auto it = s_StagedCols.find(p);
			unsigned v = it != s_StagedCols.end() ? it->second.m_Value : *pc->m_pVariable;
			return v == pc->m_Default;
		}
		return true;
	};

	std::vector<const SConfigVariable *> vpFiltered;
	vpFiltered.reserve(vEntries.size());
	for(const auto &E : vEntries)
	{
		const SConfigVariable *pVar = E.m_pVar;
		if(!DomainEnabled(pVar->m_ConfigDomain))
			continue;
		if(g_Config.m_TcUiOnlyModified && IsEffectiveDefaultVar(pVar))
			continue;
		if(pSearch && pSearch[0])
		{
			const char *pName = pVar->m_pScriptName ? pVar->m_pScriptName : "";
			const char *pHelp = pVar->m_pHelp ? pVar->m_pHelp : "";
			if(!str_find_nocase(pName, pSearch) && !str_find_nocase(pHelp, pSearch))
				continue;
		}
		vpFiltered.push_back(pVar);
	}

	std::sort(vpFiltered.begin(), vpFiltered.end(), [](const SConfigVariable *a, const SConfigVariable *b) {
		if(a->m_ConfigDomain != b->m_ConfigDomain)
			return a->m_ConfigDomain < b->m_ConfigDomain;
		return str_comp(a->m_pScriptName, b->m_pScriptName) < 0;
	});

	static CScrollRegion s_ScrollRegion;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	s_ScrollRegion.Begin(&ListArea, &ScrollOffset, &ScrollParams);
	m_pSettingsSearchActiveScrollRegion = &s_ScrollRegion;

	ListArea.y += ScrollOffset.y;
	ListArea.VSplitRight(5.0f, &ListArea, nullptr);
	CUIRect Content = ListArea;

	auto DomainName = [](ConfigDomain D) {
		switch(D)
		{
		case ConfigDomain::DDNET: return "DDNet";
		case ConfigDomain::BestClient: return "BestClient";
		default: return "Other";
		}
	};

	ConfigDomain CurrentDomain = ConfigDomain::NUM;
	for(const SConfigVariable *pVar : vpFiltered)
	{
		if(pVar->m_ConfigDomain != CurrentDomain)
		{
			CurrentDomain = pVar->m_ConfigDomain;
			CUIRect Header;
			Content.HSplitTop(HeadlineHeight, &Header, &Content);
			if(s_ScrollRegion.AddRect(Header))
				Ui()->DoLabel(&Header, DomainName(CurrentDomain), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);
		}

		CUIRect RowItem;
		const float RowHeight = g_Config.m_TcUiCompactList ? (std::max(LineSize, ColorPickerLineSize) + 5.0f) : 55.0f;
		Content.HSplitTop(RowHeight, &RowItem, &Content);
		Content.HSplitTop(MarginExtraSmall, nullptr, &Content);
		const bool Visible = s_ScrollRegion.AddRect(RowItem);
		if(!Visible)
			continue;

		const bool Modified = !IsEffectiveDefaultVar(pVar);
		const ColorRGBA BgModified = ColorRGBA(1.0f, 0.8f, 0.0f, 0.15f);
		const ColorRGBA BgNormal = ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f);
		RowItem.Draw(Modified ? BgModified : BgNormal, IGraphics::CORNER_ALL, 6.0f);

		CUIRect RowContent;
		RowItem.Margin(5.0f, &RowContent);

		CUIRect TopLine, Below;
		if(g_Config.m_TcUiCompactList)
		{
			const float UsedHeight = (pVar->m_Type == SConfigVariable::VAR_COLOR) ? ColorPickerLineSize : LineSize;
			TopLine = RowContent;
			TopLine.h = UsedHeight;
			TopLine.y = round_to_int(RowContent.y + (RowContent.h - UsedHeight) / 2.0f);
			Below = RowContent;
		}
		else
		{
			RowContent.HSplitTop(LineSize, &TopLine, &Below);
		}
		CUIRect NameLine, Right;
		TopLine.VSplitRight(320.0f, &NameLine, &Right);
		NameLine.VSplitLeft(10.0f, nullptr, &NameLine);

		Ui()->DoLabel(&NameLine, pVar->m_pScriptName, FontSize, TEXTALIGN_ML);

		CUIRect Controls, ResetRect;
		Right.VSplitRight(120.0f, &Controls, &ResetRect);
		Controls.h = LineSize;
		Controls.y = TopLine.y + (TopLine.h - LineSize) / 2.0f;
		ResetRect.h = LineSize;
		ResetRect.y = Controls.y;
		Controls.VSplitRight(MarginSmall, &Controls, nullptr);

		if(!g_Config.m_TcUiCompactList)
		{
			CUIRect Help;
			Below.HSplitTop(2.0f, nullptr, &Below);
			Help = Below;
			Help.VSplitLeft(10.0f, nullptr, &Help);
			Ui()->DoLabel(&Help, pVar->m_pHelp ? pVar->m_pHelp : "", 11.0f, TEXTALIGN_ML);
		}

		static std::unordered_map<const SConfigVariable *, CButtonContainer> s_ResetBtns;
		if(Modified && pVar->m_Type != SConfigVariable::VAR_COLOR)
		{
			CButtonContainer &ResetBtn = s_ResetBtns[pVar];
			if(DoButton_Menu(&ResetBtn, Localize("Reset"), 0, &ResetRect))
			{
				if(pVar->m_Type == SConfigVariable::VAR_INT)
				{
					const SIntConfigVariable *pInt = static_cast<const SIntConfigVariable *>(pVar);
					s_StagedInts[pVar] = {pInt->m_Default};
				}
				else if(pVar->m_Type == SConfigVariable::VAR_STRING)
				{
					const SStringConfigVariable *pStr = static_cast<const SStringConfigVariable *>(pVar);
					s_StagedStrs[pVar] = {std::string(pStr->m_pDefault)};
				}
			}
		}

		if(pVar->m_Type == SConfigVariable::VAR_INT)
		{
			const SIntConfigVariable *pInt = static_cast<const SIntConfigVariable *>(pVar);
			// treat 0 1 ints as checkboxes
			if(pInt->m_Min == 0 && pInt->m_Max == 1)
			{
				const int Effective = s_StagedInts.count(pVar) ? s_StagedInts[pVar].m_Value : *pInt->m_pVariable;
				if(DoButton_CheckBox(pVar, "", Effective, &Controls))
				{
					const int NewVal = Effective ? 0 : 1;
					if(NewVal == *pInt->m_pVariable)
						s_StagedInts.erase(pVar);
					else
						s_StagedInts[pVar] = {NewVal};
				}
			}
			else
			{
				SIntState &State = s_IntInputs[pVar];
				const int Effective = s_StagedInts.count(pVar) ? s_StagedInts[pVar].m_Value : *pInt->m_pVariable;
				if(!State.m_Inited)
				{
					State.m_Input.SetInteger(Effective);
					State.m_LastValue = Effective;
					State.m_Inited = true;
				}
				else if(!State.m_Input.IsActive() && State.m_LastValue != Effective)
				{
					State.m_Input.SetInteger(Effective);
					State.m_LastValue = Effective;
				}

				CUIRect InputBox, Dummy;
				Controls.VSplitLeft(60.0f, &InputBox, &Dummy);

				if(Ui()->DoEditBox(&State.m_Input, &InputBox, EditBoxFontSize))
				{
					int NewVal = State.m_Input.GetInteger();
					bool InRange = true;
					if(pInt->m_Min != pInt->m_Max)
					{
						if(NewVal < pInt->m_Min)
							InRange = false;
						if(pInt->m_Max != 0 && NewVal > pInt->m_Max)
							InRange = false;
					}
					if(InRange && NewVal != State.m_LastValue)
					{
						if(NewVal == *pInt->m_pVariable)
							s_StagedInts.erase(pVar);
						else
							s_StagedInts[pVar] = {NewVal};
						State.m_LastValue = NewVal;
					}
				}
			}
		}
		else if(pVar->m_Type == SConfigVariable::VAR_STRING)
		{
			const SStringConfigVariable *pStr = static_cast<const SStringConfigVariable *>(pVar);
			SStrState &State = s_StrInputs[pVar];
			const char *Effective = s_StagedStrs.count(pVar) ? s_StagedStrs[pVar].m_Value.c_str() : pStr->m_pStr;
			if(!State.m_Inited)
			{
				State.m_Input.Set(Effective);
				State.m_Inited = true;
			}
			else if(!State.m_Input.IsActive())
			{
				if(str_comp(State.m_Input.GetString(), Effective) != 0)
					State.m_Input.Set(Effective);
			}

			if(Ui()->DoEditBox(&State.m_Input, &Controls, EditBoxFontSize))
			{
				const char *NewVal = State.m_Input.GetString();
				if(str_comp(NewVal, pStr->m_pStr) == 0)
					s_StagedStrs.erase(pVar);
				else
					s_StagedStrs[pVar] = {std::string(NewVal)};
			}
		}
		else if(pVar->m_Type == SConfigVariable::VAR_COLOR)
		{
			const SColorConfigVariable *pCol = static_cast<const SColorConfigVariable *>(pVar);
			CUIRect ColorRect;
			ColorRect.x = Controls.x;
			ColorRect.h = ColorPickerLineSize;
			ColorRect.y = TopLine.y + (TopLine.h - ColorPickerLineSize) / 2.0f;
			ColorRect.w = ColorPickerLineSize + 8.0f + 60.0f;
			const ColorRGBA DefaultColor = color_cast<ColorRGBA>(ColorHSLA(pCol->m_Default, true).UnclampLighting(pCol->m_DarkestLighting));
			static std::unordered_map<const SConfigVariable *, CButtonContainer> s_ColorResetIds;
			CButtonContainer &ResetId = s_ColorResetIds[pVar];

			SColState &ColState = s_ColInputs[pVar];
			unsigned Effective = s_StagedCols.count(pVar) ? s_StagedCols[pVar].m_Value : *pCol->m_pVariable;
			if(!ColState.m_Inited)
			{
				ColState.m_Working = Effective;
				ColState.m_LastValue = Effective;
				ColState.m_Inited = true;
			}
			else
			{
				const bool EditingThis = Ui()->IsPopupOpen(&m_ColorPickerPopupContext) && m_ColorPickerPopupContext.m_pHslaColor == &ColState.m_Working;
				if(!EditingThis && ColState.m_Working != Effective)
				{
					ColState.m_Working = Effective;
					ColState.m_LastValue = Effective;
				}
			}

			DoLine_ColorPicker(&ResetId, ColorPickerLineSize, ColorPickerLabelSize, 0.0f, &ColorRect, "", &ColState.m_Working, DefaultColor, false, nullptr, pCol->m_Alpha);
			if(ColState.m_Working != Effective)
			{
				if(ColState.m_Working == *pCol->m_pVariable)
					s_StagedCols.erase(pVar);
				else
					s_StagedCols[pVar] = {ColState.m_Working};
				ColState.m_LastValue = ColState.m_Working;
			}
		}
	}

	CUIRect EndPad{Content.x, Content.y, Content.w, 5.0f};
	s_ScrollRegion.AddRect(EndPad);
	s_ScrollRegion.End();
	m_pSettingsSearchActiveScrollRegion = nullptr;
}
