/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_MENUS_H
#define GAME_CLIENT_COMPONENTS_MENUS_H

#include <base/bytes.h>
#include <base/types.h>
#include <base/vmath.h>

#include <engine/console.h>
#include <engine/demo.h>
#include <engine/friends.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/component.h>
#include <game/client/components/community_icons.h>
#include <game/client/components/mapimages.h>
#include <game/client/components/menu_media_background.h>
#include <game/client/components/menus_ingame_touch_controls.h>
#include <game/client/components/menus_settings_controls.h>
#include <game/client/components/menus_start.h>
#include <game/client/components/skins7.h>
#include <game/client/components/tclient/warlist.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/voting.h>

#include <array>
#include <chrono>
#include <optional>
#include <vector>

class CMenusStart;
class CImageInfo;
struct CDataSprite;

class CMenus : public CComponent
{
	static ColorRGBA ms_GuiColor;
	static ColorRGBA ms_ColorTabbarInactiveOutgame;
	static ColorRGBA ms_ColorTabbarActiveOutgame;
	static ColorRGBA ms_ColorTabbarHoverOutgame;
	static ColorRGBA ms_ColorTabbarInactiveIngame;
	static ColorRGBA ms_ColorTabbarActiveIngame;
	static ColorRGBA ms_ColorTabbarHoverIngame;
	static ColorRGBA ms_ColorTabbarInactive;
	static ColorRGBA ms_ColorTabbarActive;
	static ColorRGBA ms_ColorTabbarHover;

public:
	int DoButton_Toggle(const void *pId, int Checked, const CUIRect *pRect, bool Active, unsigned Flags = BUTTONFLAG_LEFT);
	int DoButton_Menu(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, unsigned Flags = BUTTONFLAG_LEFT, const char *pImageName = nullptr, int Corners = IGraphics::CORNER_ALL, float Rounding = 5.0f, float FontFactor = 0.0f, ColorRGBA Color = ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f));
	int DoButton_MenuEx(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, unsigned Flags, const char *pImageName, int Corners, float Rounding, float FontFactor, ColorRGBA Color, bool AlwaysColoredImage);
	int DoButton_MenuTab(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, int Corners, SUIAnimator *pAnimator = nullptr, const ColorRGBA *pDefaultColor = nullptr, const ColorRGBA *pActiveColor = nullptr, const ColorRGBA *pHoverColor = nullptr, float EdgeRounding = 10.0f, const CCommunityIcon *pCommunityIcon = nullptr);

	int DoButton_CheckBox_Common(const void *pId, const char *pText, const char *pBoxText, const CUIRect *pRect, unsigned Flags);
	int DoButton_CheckBox(const void *pId, const char *pText, int Checked, const CUIRect *pRect);
	int DoButton_CheckBoxAutoVMarginAndSet(const void *pId, const char *pText, int *pValue, CUIRect *pRect, float VMargin);
	int DoButton_CheckBox_Number(const void *pId, const char *pText, int Checked, const CUIRect *pRect);

	bool DoSliderWithScaledValue(const void *pId, int *pOption, const CUIRect *pRect, const char *pStr, int Min, int Max, int Scale, const IScrollbarScale *pScale, unsigned Flags = 0u, const char *pSuffix = "");
	bool DoEditBoxWithLabel(CLineInput *LineInput, const CUIRect *pRect, const char *pLabel, const char *pDefault, char *pBuf, size_t BufSize);
	bool DoLine_RadioMenu(CUIRect &View, const char *pLabel, std::vector<CButtonContainer> &vButtonContainers, const std::vector<const char *> &vLabels, const std::vector<int> &vValues, int &Value);
	bool DoLine_KeyReader(CUIRect &View, CButtonContainer &ReaderButton, CButtonContainer &ClearButton, const char *pName, const char *pCommand);
	IGraphics *MenuGraphics() const { return Graphics(); }
	IStorage *MenuStorage() const { return Storage(); }
	CGameClient *MenuGameClient() const { return GameClient(); }
	CUi *MenuUi() const { return Ui(); }
	IClient *MenuClient() const { return Client(); }
	IHttp *MenuHttp() const { return Http(); }
	void MenuButtonSoundEvent(CUi::EButtonSoundEvent Event) { OnButtonSoundEvent(Event); }
	void RefreshCustomAssetsTab(int CurTab) { ClearCustomItems(CurTab); }

	void RenderDevSkinPublic(vec2 RenderPos, float Size,
		const char *pSkinName, const char *pBackupSkin,
		bool CustomColors, int FeetColor, int BodyColor,
		int Emote, bool Rainbow = false, bool Cute = false,
		ColorRGBA ColorFeet = ColorRGBA(0, 0, 0, 0),
		ColorRGBA ColorBody = ColorRGBA(0, 0, 0, 0))
	{
		RenderDevSkin(RenderPos, Size, pSkinName, pBackupSkin,
			CustomColors, FeetColor, BodyColor, Emote,
			Rainbow, Cute, ColorFeet, ColorBody);
	}

private:
	CUi::SColorPickerPopupContext m_ColorPickerPopupContext;
	ColorHSLA DoLine_ColorPicker(CButtonContainer *pResetId, float LineSize, float LabelSize, float BottomMargin, CUIRect *pMainRect, const char *pText, unsigned int *pColorValue, ColorRGBA DefaultColor, bool CheckBoxSpacing = true, int *pCheckBoxValue = nullptr, bool Alpha = false);
	ColorHSLA DoButton_ColorPicker(const CUIRect *pRect, unsigned int *pHslaColor, bool Alpha);

	void DoLaserPreview(const CUIRect *pRect, ColorHSLA OutlineColor, ColorHSLA InnerColor, int LaserType);
	int DoButton_GridHeader(const void *pId, const char *pText, int Checked, const CUIRect *pRect, int Align = TEXTALIGN_ML);
	int DoButton_Favorite(const void *pButtonId, const void *pParentId, bool Checked, const CUIRect *pRect);

	bool m_SkinListScrollToSelected = false;
	std::optional<std::chrono::nanoseconds> m_SkinList7LastRefreshTime;
	std::optional<std::chrono::nanoseconds> m_SkinPartsList7LastRefreshTime;

	int m_DirectionQuadContainerIndex;

	class CVideoLoader *m_pLoadingVideo;
	CMenuMediaBackground m_MenuMediaBackground;

	IGraphics::CTextureHandle m_TextureBCLogo;
	bool m_FirstLoadComplete;

	int m_GeneralSubTab;
	int m_AppearanceSubTab;
	int m_AppearanceTab;
	bool m_SettingsSearchOpen = false;
	CLineInputBuffered<128> m_SettingsSearchInput;
	bool m_SettingsSearchRevealPending = false;
	int m_SettingsSearchRevealFrames = 0;
	char m_aSettingsSearchLastApplied[128] = "";
	char m_aSettingsSearchFocusKey[64] = "";
	CScrollRegion *m_pSettingsSearchActiveScrollRegion = nullptr;
	int m_BestClientSettingsTab = 0;
	int m_BestClientGoresFeaturesCategory = 0;
	struct SBestClientTabTransition
	{
		int m_Current = -1;
		int m_Previous = -1;
		float m_Phase = 1.0f;
	};
	SBestClientTabTransition m_BestClientPageTransition;
	SBestClientTabTransition m_BestClientVisualsTransition;
	SBestClientTabTransition m_BestClientGoresTransition;
	SBestClientTabTransition m_SettingsPageTransition;
	SBestClientTabTransition m_SettingsGeneralSubTabTransition;
	SBestClientTabTransition m_SettingsAppearanceSubTabTransition;
	int m_OldSettingsPage = 0;
	int m_LastSettingsLayout = 0;

public:
	enum
	{
		ASSETS_EDITOR_TYPE_GAME = 0,
		ASSETS_EDITOR_TYPE_EMOTICONS,
		ASSETS_EDITOR_TYPE_ENTITIES,
		ASSETS_EDITOR_TYPE_HUD,
		ASSETS_EDITOR_TYPE_PARTICLES,
		ASSETS_EDITOR_TYPE_EXTRAS,
		ASSETS_EDITOR_TYPE_COUNT,
	};

	struct SAssetsEditorAssetEntry
	{
		IGraphics::CTextureHandle m_PreviewTexture;
		int m_PreviewWidth = 0;
		int m_PreviewHeight = 0;
		char m_aName[64] = {0};
		char m_aPath[IO_MAX_PATH_LENGTH] = {0};
		bool m_IsDefault = false;
	};

	struct SAssetsEditorPartSlot
	{
		int m_SpriteId = -1;
		int m_SourceSpriteId = -1;
		int m_Group = 0;
		int m_DstX = 0;
		int m_DstY = 0;
		int m_DstW = 0;
		int m_DstH = 0;
		int m_SrcX = 0;
		int m_SrcY = 0;
		int m_SrcW = 0;
		int m_SrcH = 0;
		char m_aFamilyKey[64] = {0};
		char m_aSourceAsset[64] = {0};
	};

private:
	enum
	{
		BESTCLIENT_VISUALS_SUBTAB_GENERAL = 0,
		BESTCLIENT_VISUALS_SUBTAB_EDITORS,
		BESTCLIENT_VISUALS_SUBTAB_WORLD_EDITOR,
		BESTCLIENT_VISUALS_SUBTAB_LENGTH,
	};

	struct SAssetsEditorState
	{
		bool m_VisualsEditorOpen = false;
		bool m_VisualsEditorInitialized = false;
		int m_VisualsSubTab = BESTCLIENT_VISUALS_SUBTAB_GENERAL;
		int m_Type = ASSETS_EDITOR_TYPE_GAME;
		int m_aMainAssetIndex[ASSETS_EDITOR_TYPE_COUNT] = {0};
		int m_aDonorAssetIndex[ASSETS_EDITOR_TYPE_COUNT] = {0};
		bool m_ShowGrid = true;
		bool m_ApplySameSize = false;
		int m_ApplySameSizeScope = 0;
		bool m_DragActive = false;
		int m_ActiveDraggedSlotIndex = -1;
		char m_aDraggedSourceAsset[64] = {0};
		int m_HoveredDonorSlotIndex = -1;
		int m_HoveredTargetSlotIndex = -1;
		bool m_DirtyPreview = true;
		bool m_LastComposeFailed = false;
		char m_aExportName[64] = {0};
		char m_aaExportNameByType[ASSETS_EDITOR_TYPE_COUNT][64] = {};
		bool m_aExportNameTouchedByUser[ASSETS_EDITOR_TYPE_COUNT] = {};
		char m_aStatusMessage[256] = {0};
		bool m_StatusIsError = false;
		bool m_HasUnsavedChanges = false;
		bool m_PendingCloseRequest = false;
		bool m_ShowExitConfirm = false;
		bool m_FullscreenOpen = true;
		int m_HoverCycleSlotIndex = -1;
		int m_HoverCyclePositionX = -1;
		int m_HoverCyclePositionY = -1;
		int m_HoverCycleCandidateCursor = 0;
		std::vector<int> m_vHoverCycleCandidates;
		IGraphics::CTextureHandle m_ComposedPreviewTexture;
		int m_ComposedPreviewWidth = 0;
		int m_ComposedPreviewHeight = 0;
		std::vector<SAssetsEditorAssetEntry> m_avAssets[ASSETS_EDITOR_TYPE_COUNT];
		std::vector<SAssetsEditorPartSlot> m_vPartSlots;
	};

	struct SComponentsEditorState
	{
		bool m_Open = false;
		bool m_FullscreenOpen = true;
		int m_StagedMaskLo = 0;
		int m_StagedMaskHi = 0;
		int m_AppliedMaskLo = 0;
		int m_AppliedMaskHi = 0;
		bool m_HasUnsavedChanges = false;
		bool m_ShowExitConfirm = false;
		bool m_ShowRestartConfirm = false;
	};

	SAssetsEditorState m_AssetsEditorState;
	SComponentsEditorState m_ComponentsEditorState;
	void RenderSettingsBestClientVisualsGeneral(CUIRect MainView);
	void RenderSettingsBestClientVisualsEditors(CUIRect MainView);
	void RenderAssetsEditorScreen(CUIRect MainView);
	void RenderComponentsEditorScreen(CUIRect MainView);
	void AssetsEditorClearAssets();
	void AssetsEditorReloadAssets();
	void AssetsEditorReloadAssetsImagesOnly();
	void AssetsEditorResetPartSlots();
	void AssetsEditorEnsureDefaultExportNames();
	void AssetsEditorSyncExportNameFromType();
	void AssetsEditorCommitExportNameForType();
	void AssetsEditorValidateRequiredSlotsForType(int Type);
	bool AssetsEditorComposeImage(CImageInfo &OutputImage);
	bool AssetsEditorExport();
	void AssetsEditorRenderCanvas(const CUIRect &Rect, IGraphics::CTextureHandle Texture, int W, int H, int Type, bool ShowGrid, int HighlightSlot);
	void AssetsEditorCollectHoveredCandidates(const CUIRect &Rect, int Type, const std::vector<SAssetsEditorPartSlot> &vSlots, vec2 Mouse, std::vector<int> &vOutCandidates) const;
	int AssetsEditorResolveHoveredSlotWithCycle(const CUIRect &Rect, int Type, const std::vector<SAssetsEditorPartSlot> &vSlots, vec2 Mouse, bool ClickedLmb, int PreferredSlotIndex);
	void AssetsEditorCancelDrag();
	void AssetsEditorApplyDrop(int TargetSlotIndex, const char *pDonorName, int SourceSlotIndex, bool ApplyAllSameSize);
	void AssetsEditorUpdatePreviewIfDirty();
	void AssetsEditorRequestClose();
	void AssetsEditorCloseNow();
	void AssetsEditorRenderExitConfirm(const CUIRect &Rect);
	void ComponentsEditorOpen();
	void ComponentsEditorSyncFromConfig();
	void ComponentsEditorRequestClose();
	void ComponentsEditorCloseNow();
	void ComponentsEditorApply();
	void ComponentsEditorRenderExitConfirm(const CUIRect &Rect);
	void ComponentsEditorRenderRestartConfirm(const CUIRect &Rect);
	void AssetsEditorBuildFamilyKey(int Type, const CDataSprite *pSprite, char *pOut, int OutSize);
	bool AssetsEditorCopyRectScaledNearest(CImageInfo &Dst, const CImageInfo &Src, int DstX, int DstY, int DstW, int DstH, int SrcX, int SrcY, int SrcW, int SrcH);

	// menus_settings_assets.cpp
public:
	struct SCustomItem
	{
		IGraphics::CTextureHandle m_RenderTexture;

		char m_aName[50];

		bool operator<(const SCustomItem &Other) const { return str_comp(m_aName, Other.m_aName) < 0; }
	};

	struct SCustomEntities : public SCustomItem
	{
		struct SEntitiesImage
		{
			IGraphics::CTextureHandle m_Texture;
		};
		SEntitiesImage m_aImages[MAP_IMAGE_MOD_TYPE_COUNT];
	};

	struct SCustomGame : public SCustomItem
	{
	};

	struct SCustomEmoticon : public SCustomItem
	{
	};

	struct SCustomParticle : public SCustomItem
	{
	};

	struct SCustomHud : public SCustomItem
	{
	};

	struct SCustomExtras : public SCustomItem
	{
	};

	struct SCustomCursor : public SCustomItem
	{
	};

	struct SCustomArrow : public SCustomItem
	{
	};

	struct SCustomAudioPack : public SCustomItem
	{
	};

protected:
	std::vector<SCustomEntities> m_vEntitiesList;
	std::vector<SCustomGame> m_vGameList;
	std::vector<SCustomEmoticon> m_vEmoticonList;
	std::vector<SCustomParticle> m_vParticlesList;
	std::vector<SCustomHud> m_vHudList;
	std::vector<SCustomExtras> m_vExtrasList;
	std::vector<SCustomCursor> m_vCursorList;
	std::vector<SCustomArrow> m_vArrowList;
	std::vector<SCustomAudioPack> m_vAudioPackList;

	bool m_IsInit = false;

	static void LoadEntities(struct SCustomEntities *pEntitiesItem, void *pUser);
	static int EntitiesScan(const char *pName, int IsDir, int DirType, void *pUser);

	static int GameScan(const char *pName, int IsDir, int DirType, void *pUser);
	static int EmoticonsScan(const char *pName, int IsDir, int DirType, void *pUser);
	static int ParticlesScan(const char *pName, int IsDir, int DirType, void *pUser);
	static int HudScan(const char *pName, int IsDir, int DirType, void *pUser);
	static int ExtrasScan(const char *pName, int IsDir, int DirType, void *pUser);
	static int CursorScan(const char *pName, int IsDir, int DirType, void *pUser);
	static int ArrowScan(const char *pName, int IsDir, int DirType, void *pUser);
	static int AudioPackScan(const char *pName, int IsDir, int DirType, void *pUser);

	static void ConchainAssetsEntities(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainAssetGame(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainAssetParticles(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainAssetEmoticons(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainAssetHud(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainAssetExtras(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainAssetCursor(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainAssetArrow(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainSndPack(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

	void ClearCustomItems(int CurTab);

	int m_MenuPage;
	int m_GamePage;
	int m_Popup;
	bool m_ShowStart;
	bool m_MenuActive;
	float m_BcIngameMenuOpenPhase = 0.0f;
	bool m_BcIngameMenuClosing = false;

	bool m_DummyNamePlatePreview = false;

	class CJoinTutorial
	{
	public:
		bool m_Queued = false;
		enum class EStatus
		{
			REFRESHING,
			SERVER_LIST_ERROR,
			NO_TUTORIAL_AVAILABLE,
		};
		EStatus m_Status = EStatus::REFRESHING;
		bool m_TryRefresh = false;
		bool m_TriedRefresh = false;
		enum class ELocalServerState
		{
			NOT_TRIED,
			TRY,
			WAITING_STOP,
			WAITING_START,
		};
		ELocalServerState m_LocalServerState = ELocalServerState::NOT_TRIED;
		std::chrono::nanoseconds m_StateChange = std::chrono::nanoseconds(0);
	};
	CJoinTutorial m_JoinTutorial;
	bool m_CreateDefaultFavoriteCommunities = false;
	bool m_ForceRefreshLanPage = false;

	char m_aNextServer[256];

	// images
	struct CMenuImage
	{
		char m_aName[64];
		IGraphics::CTextureHandle m_OrgTexture;
		IGraphics::CTextureHandle m_GreyTexture;
	};
	std::vector<CMenuImage> m_vMenuImages;
	static int MenuImageScan(const char *pName, int IsDir, int DirType, void *pUser);
	const CMenuImage *FindMenuImage(const char *pName);

	// loading
	class CLoadingState
	{
	public:
		std::chrono::nanoseconds m_LastRender{0};
		int m_Current;
		int m_Total;
	};
	CLoadingState m_LoadingState;

	//
	char m_aMessageTopic[512];
	char m_aMessageBody[512];
	char m_aMessageButton[512];

	CUIElement m_RefreshButton;
	CUIElement m_ConnectButton;

	// generic popups
	typedef void (CMenus::*FPopupButtonCallback)();
	void DefaultButtonCallback()
	{
		// do nothing
	}
	enum
	{
		BUTTON_CONFIRM = 0, // confirm / yes / close / ok
		BUTTON_CANCEL, // cancel / no
		NUM_BUTTONS
	};
	char m_aPopupTitle[128];
	char m_aPopupMessage[IO_MAX_PATH_LENGTH + 256];
	struct
	{
		char m_aLabel[64];
		int m_NextPopup;
		FPopupButtonCallback m_pfnCallback;
	} m_aPopupButtons[NUM_BUTTONS];

	void PopupMessage(const char *pTitle, const char *pMessage,
		const char *pButtonLabel, int NextPopup = POPUP_NONE, FPopupButtonCallback pfnButtonCallback = &CMenus::DefaultButtonCallback);
	void PopupConfirm(const char *pTitle, const char *pMessage,
		const char *pConfirmButtonLabel, const char *pCancelButtonLabel,
		FPopupButtonCallback pfnConfirmButtonCallback = &CMenus::DefaultButtonCallback, int ConfirmNextPopup = POPUP_NONE,
		FPopupButtonCallback pfnCancelButtonCallback = &CMenus::DefaultButtonCallback, int CancelNextPopup = POPUP_NONE);
	void PopupConfirmLoadTClientSettings();
	bool FindTClientSettingsPath(char *pBuffer, int BufferSize) const;

	// some settings
	static float ms_ButtonHeight;
	static float ms_ListheaderHeight;
	static float ms_ListitemAdditionalHeight;

	// for settings
	bool m_NeedRestartGraphics;
	bool m_NeedRestartSound;
	bool m_NeedRestartUpdate;
	bool m_NeedSendinfo;
	bool m_NeedSendDummyinfo;
	int m_SettingPlayerPage;

	// 0.7 skins
	bool m_CustomSkinMenu = false;
	int m_TeePartSelected = protocol7::SKINPART_BODY;
	const CSkins7::CSkin *m_pSelectedSkin = nullptr;
	CLineInputBuffered<protocol7::MAX_SKIN_ARRAY_SIZE, protocol7::MAX_SKIN_LENGTH> m_SkinNameInput;
	bool m_SkinPartListNeedsUpdate = false;
	void PopupConfirmDeleteSkin7();

	// for map download popup
	int64_t m_DownloadLastCheckTime;
	int m_DownloadLastCheckSize;
	float m_DownloadSpeed;

	// for password popup
	CLineInput m_PasswordInput;

	// for call vote
	int m_CallvoteSelectedOption;
	int m_CallvoteSelectedPlayer;
	CLineInputBuffered<VOTE_REASON_LENGTH> m_CallvoteReasonInput;
	CLineInputBuffered<64> m_FilterInput;
	bool m_ControlPageOpening;

	// demo
	enum
	{
		SORT_DEMONAME = 0,
		SORT_MARKERS,
		SORT_LENGTH,
		SORT_DATE,
	};

	class CDemoItem
	{
	public:
		char m_aFilename[IO_MAX_PATH_LENGTH];
		char m_aName[IO_MAX_PATH_LENGTH];
		bool m_IsDir;
		bool m_IsLink;
		int m_StorageType;
		time_t m_Date;
		int64_t m_Size;

		bool m_InfosLoaded;
		bool m_Valid;
		CDemoHeader m_Info;
		CTimelineMarkers m_TimelineMarkers;
		CMapInfo m_MapInfo;

		int NumMarkers() const
		{
			return std::clamp<int>(bytes_be_to_uint(m_TimelineMarkers.m_aNumTimelineMarkers), 0, MAX_TIMELINE_MARKERS);
		}

		int Length() const
		{
			return bytes_be_to_uint(m_Info.m_aLength);
		}

		unsigned MapSize() const
		{
			return bytes_be_to_uint(m_Info.m_aMapSize);
		}

		bool operator<(const CDemoItem &Other) const
		{
			if(!str_comp(m_aFilename, ".."))
				return true;
			if(!str_comp(Other.m_aFilename, ".."))
				return false;
			if(m_IsDir && !Other.m_IsDir)
				return true;
			if(!m_IsDir && Other.m_IsDir)
				return false;

			const CDemoItem &Left = g_Config.m_BrDemoSortOrder ? Other : *this;
			const CDemoItem &Right = g_Config.m_BrDemoSortOrder ? *this : Other;

			if(g_Config.m_BrDemoSort == SORT_DEMONAME)
				return str_comp_filenames(Left.m_aFilename, Right.m_aFilename) < 0;
			if(g_Config.m_BrDemoSort == SORT_DATE)
				return Left.m_Date < Right.m_Date;

			if(!Other.m_InfosLoaded)
				return m_InfosLoaded;
			if(!m_InfosLoaded)
				return !Other.m_InfosLoaded;

			if(g_Config.m_BrDemoSort == SORT_MARKERS)
				return Left.NumMarkers() < Right.NumMarkers();
			if(g_Config.m_BrDemoSort == SORT_LENGTH)
				return Left.Length() < Right.Length();

			// Unknown sort
			return true;
		}
	};

	char m_aCurrentDemoFolder[IO_MAX_PATH_LENGTH];
	char m_aCurrentDemoSelectionName[IO_MAX_PATH_LENGTH];
	CLineInputBuffered<IO_MAX_PATH_LENGTH> m_DemoRenameInput;
	CLineInputBuffered<IO_MAX_PATH_LENGTH> m_DemoSliceInput;
	CLineInputBuffered<IO_MAX_PATH_LENGTH> m_DemoSearchInput;
#if defined(CONF_VIDEORECORDER)
	CLineInputBuffered<IO_MAX_PATH_LENGTH> m_DemoRenderInput;
#endif
	int m_DemolistSelectedIndex;
	bool m_DemolistSelectedReveal = false;
	int m_DemolistStorageType;
	bool m_DemolistMultipleStorages = false;
	int m_Speed = 4;
	bool m_StartPaused = false;

	std::chrono::nanoseconds m_DemoPopulateStartTime{0};

	void DemolistOnUpdate(bool Reset);
	static int DemolistFetchCallback(const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser);

	// friends
	class CFriendItem
	{
		char m_aName[MAX_NAME_LENGTH];
		char m_aClan[MAX_CLAN_LENGTH];
		const CServerInfo *m_pServerInfo;
		int m_FriendState;
		bool m_IsPlayer;
		bool m_IsAfk;
		// skin info 0.6
		char m_aSkin[MAX_SKIN_LENGTH];
		bool m_CustomSkinColors;
		int m_CustomSkinColorBody;
		int m_CustomSkinColorFeet;
		// skin info 0.7
		char m_aaSkin7[protocol7::NUM_SKINPARTS][protocol7::MAX_SKIN_LENGTH];
		bool m_aUseCustomSkinColor7[protocol7::NUM_SKINPARTS];
		int m_aCustomSkinColor7[protocol7::NUM_SKINPARTS];

	public:
		CFriendItem(const CFriendInfo *pFriendInfo) :
			m_pServerInfo(nullptr),
			m_IsPlayer(false),
			m_IsAfk(false),
			m_CustomSkinColors(false),
			m_CustomSkinColorBody(0),
			m_CustomSkinColorFeet(0)
		{
			str_copy(m_aName, pFriendInfo->m_aName);
			str_copy(m_aClan, pFriendInfo->m_aClan);
			m_FriendState = m_aName[0] == '\0' ? IFriends::FRIEND_CLAN : IFriends::FRIEND_PLAYER;
			m_aSkin[0] = '\0';
			for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
			{
				m_aaSkin7[Part][0] = '\0';
				m_aUseCustomSkinColor7[Part] = false;
				m_aCustomSkinColor7[Part] = 0;
			}
		}
		CFriendItem(const CServerInfo::CClient &CurrenBestClient, const CServerInfo *pServerInfo) :
			m_pServerInfo(pServerInfo),
			m_FriendState(CurrenBestClient.m_FriendState),
			m_IsPlayer(CurrenBestClient.m_Player),
			m_IsAfk(CurrenBestClient.m_Afk),
			m_CustomSkinColors(CurrenBestClient.m_CustomSkinColors),
			m_CustomSkinColorBody(CurrenBestClient.m_CustomSkinColorBody),
			m_CustomSkinColorFeet(CurrenBestClient.m_CustomSkinColorFeet)
		{
			str_copy(m_aName, CurrenBestClient.m_aName);
			str_copy(m_aClan, CurrenBestClient.m_aClan);
			str_copy(m_aSkin, CurrenBestClient.m_aSkin);
			for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
			{
				str_copy(m_aaSkin7[Part], CurrenBestClient.m_aaSkin7[Part]);
				m_aUseCustomSkinColor7[Part] = CurrenBestClient.m_aUseCustomSkinColor7[Part];
				m_aCustomSkinColor7[Part] = CurrenBestClient.m_aCustomSkinColor7[Part];
			}
		}

		const char *Name() const { return m_aName; }
		const char *Clan() const { return m_aClan; }
		const CServerInfo *ServerInfo() const { return m_pServerInfo; }
		int FriendState() const { return m_FriendState; }
		bool IsPlayer() const { return m_IsPlayer; }
		bool IsAfk() const { return m_IsAfk; }
		// 0.6 skin
		const char *Skin() const { return m_aSkin; }
		bool CustomSkinColors() const { return m_CustomSkinColors; }
		int CustomSkinColorBody() const { return m_CustomSkinColorBody; }
		int CustomSkinColorFeet() const { return m_CustomSkinColorFeet; }
		// 0.7 skin
		const char *Skin7(int Part) const { return m_aaSkin7[Part]; }
		bool UseCustomSkinColor7(int Part) const { return m_aUseCustomSkinColor7[Part]; }
		int CustomSkinColor7(int Part) const { return m_aCustomSkinColor7[Part]; }

		const void *ListItemId() const { return &m_aName; }
		const void *RemoveButtonId() const { return &m_FriendState; }
		const void *CommunityTooltipId() const { return &m_IsPlayer; }
		const void *SkinTooltipId() const { return &m_aSkin; }

		bool operator<(const CFriendItem &Other) const
		{
			const int Result = str_comp_nocase(m_aName, Other.m_aName);
			return Result < 0 || (Result == 0 && str_comp_nocase(m_aClan, Other.m_aClan) < 0);
		}
	};

	enum
	{
		FRIEND_PLAYER_ON = 0,
		FRIEND_CLAN_ON,
		FRIEND_OFF,
		NUM_FRIEND_TYPES
	};
	std::vector<CFriendItem> m_avFriends[NUM_FRIEND_TYPES];
	const CFriendItem *m_pRemoveFriend = nullptr;

	// found in menus.cpp
	void Render();
	void RenderPopupFullscreen(CUIRect Screen);
	void RenderPopupConnecting(CUIRect Screen);
	void RenderPopupServerFull(CUIRect Screen);
	void RenderPopupLoading(CUIRect Screen);
	void RenderPopupConnectionStatus(const char *pStatusText, const char *pDetailText);
#if defined(CONF_VIDEORECORDER)
	void PopupConfirmDemoReplaceVideo();
#endif
	void RenderMenubar(CUIRect Box, IClient::EClientState ClientState);
	void RenderNews(CUIRect MainView);
	static void ConchainBackgroundEntities(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainUpdateMusicState(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	void UpdateMusicState();

	// found in menus_demo.cpp
	vec2 m_DemoControlsPositionOffset = vec2(0.0f, 0.0f);
	bool m_PausedBeforeSeeking;
	float m_PrevSeekAmount;
	float m_LastPauseChange = -1.0f;
	float m_LastSpeedChange = -1.0f;
	static constexpr int DEFAULT_SKIP_DURATION_INDEX = 3;
	int m_SkipDurationIndex = DEFAULT_SKIP_DURATION_INDEX;
	static bool DemoFilterChat(const void *pData, int Size, void *pUser);
	bool FetchHeader(CDemoItem &Item);
	void FetchAllHeaders();
	void HandleDemoSeeking(float PositionToSeek, float TimeToSeek);
	void RenderDemoPlayer(CUIRect MainView);
	void RenderDemoPlayerSliceSavePopup(CUIRect MainView);
	bool m_DemoBrowserListInitialized = false;
	void RenderDemoBrowser(CUIRect MainView);
	void RenderDemoBrowserList(CUIRect ListView, bool &WasListboxItemActivated);
	void RenderDemoBrowserDetails(CUIRect DetailsView);
	void RenderDemoBrowserButtons(CUIRect ButtonsView, bool WasListboxItemActivated);
	void PopupConfirmPlayDemo();
	void PopupConfirmDeleteDemo();
	void PopupConfirmDeleteFolder();
	static void ConchainDemoPlay(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainDemoSpeed(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

	// found in menus_ingame.cpp
	STextContainerIndex m_MotdTextContainerIndex;
	void RenderGame(CUIRect MainView);
	void PopupConfirmDisconnect();
	void PopupConfirmOpenWiki();
	void PopupConfirmDisconnectDummy();
	void PopupConfirmDiscardTouchControlsChanges();
	void PopupConfirmResetTouchControls();
	void PopupConfirmImportTouchControlsClipboard();
	void PopupConfirmDeleteButton();
	void PopupCancelDeselectButton();
	void PopupConfirmSelectedNotVisible();
	void PopupConfirmChangeSelectedButton();
	void PopupCancelChangeSelectedButton();
	void PopupConfirmTurnOffEditor();
	void RenderPlayers(CUIRect MainView);
	void RenderServerInfo(CUIRect MainView);
	void RenderServerInfoMotd(CUIRect Motd);
	void RenderServerControl(CUIRect MainView);
	bool RenderServerControlKick(CUIRect MainView, bool FilterSpectators, bool UpdateScroll);
	bool RenderServerControlServer(CUIRect MainView, bool UpdateScroll);
	void RenderIngameHint();

	// found in menus_browser.cpp
	int m_SelectedIndex;
	bool m_ServerBrowserShouldRevealSelection;
	std::vector<CUIElement *> m_avpServerBrowserUiElements[IServerBrowser::NUM_TYPES];
	void RenderServerbrowserServerList(CUIRect View, bool &WasListboxItemActivated);
	void RenderServerbrowserStatusBox(CUIRect StatusBox, bool WasListboxItemActivated);
	void Connect(const char *pAddress);
	void PopupConfirmSwitchServer();
	void ToggleBestClientServerFilter();
	void RenderServerbrowserFilters(CUIRect View);
	void ResetServerbrowserFilters();
	void RenderServerbrowserDDNetFilter(CUIRect View,
		IFilterList &Filter,
		float ItemHeight, int MaxItems, int ItemsPerRow,
		CScrollRegion &ScrollRegion, std::vector<unsigned char> &vItemIds,
		bool UpdateCommunityCacheOnChange,
		const std::function<const char *(int ItemIndex)> &GetItemName,
		const std::function<void(int ItemIndex, CUIRect Item, const void *pItemId, bool Active)> &RenderItem);
	void RenderServerbrowserCommunitiesFilter(CUIRect View);
	void RenderServerbrowserCountriesFilter(CUIRect View);
	void RenderServerbrowserTypesFilter(CUIRect View);
	struct SPopupCountrySelectionContext
	{
		CMenus *m_pMenus;
		int m_Selection;
		bool m_New;
	};
	static CUi::EPopupMenuFunctionResult PopupCountrySelection(void *pContext, CUIRect View, bool Active);
	struct SPopupSettingsCountrySelectionContext
	{
		CMenus *m_pMenus;
		int *m_pCountry;
		int m_Selection;
		bool m_New;
	};
	static CUi::EPopupMenuFunctionResult PopupSettingsCountrySelection(void *pContext, CUIRect View, bool Active);
	void RenderServerbrowserInfo(CUIRect View);
	void RenderServerbrowserInfoScoreboard(CUIRect View, const CServerInfo *pSelectedServer);
	void RenderServerbrowserBestClient(CUIRect View);
	void RenderServerbrowserFriends(CUIRect View);
	void FriendlistOnUpdate();
	void PopupConfirmRemoveFriend();
	void RenderServerbrowserTabBar(CUIRect TabBar);
	void RenderServerbrowserToolBox(CUIRect ToolBox);
	void RenderServerbrowser(CUIRect MainView);
	template<typename F>
	bool PrintHighlighted(const char *pName, F &&PrintFn);
	CTeeRenderInfo GetTeeRenderInfo(vec2 Size, const char *pSkinName, bool CustomSkinColors, int CustomSkinColorBody, int CustomSkinColorFeet) const;
	static void ConchainFriendlistUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainFavoritesUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainCommunitiesUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainUiPageUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	void UpdateCommunityCache(bool Force);

	// found in menus_settings.cpp
	void RenderLanguageSettings(CUIRect MainView);
	bool RenderLanguageSelection(CUIRect MainView);
	void RenderThemeSelection(CUIRect MainView);
	float RenderSettingsGeneral(CUIRect MainView);
	void RenderSettingsPlayer(CUIRect MainView);
	void RenderSettingsTee(CUIRect MainView);
	void RenderSettingsTee7(CUIRect MainView);
	void RenderSettingsTeeCustom7(CUIRect MainView);
	void RenderSkinSelection7(CUIRect MainView);
	void RenderSkinPartSelection7(CUIRect MainView);
	float RenderSettingsGraphics(CUIRect MainView);
	float RenderSettingsSound(CUIRect MainView);
	void RenderSettings(CUIRect MainView);
	void RenderSettingsNew(CUIRect MainView, bool NeedRestart);
	void RenderSettingsOld(CUIRect MainView, bool NeedRestart);
	void RenderSettingsCustom(CUIRect MainView);
	// found in menus_tclient.cpp
	void RenderSettingsTClient(CUIRect MainView);
	void RenderSettingsTClientSettings(CUIRect MainView);
	void RenderSettingsTClientBindWheel(CUIRect MainView);
	void RenderSettingsTClientChatBinds(CUIRect MainView);
	void RenderSettingsTClientWarList(CUIRect MainView);
	void RenderSettingsTClientStatusBar(CUIRect MainView);
	void RenderSettingsTClientInfo(CUIRect MainView);
	void RenderSettingsTClientProfiles(CUIRect MainView);
	void RenderSettingsTClientConfigs(CUIRect MainView);
	void RenderSettingsSearchRow(CUIRect &ContentView);
	void UpdateSettingsSearchNavigation();
	void RenderSettingsRestartWarning(CUIRect RestartBar);
	void OpenSettingsSearch();
	bool SettingsSearchHasText() const;
	bool SettingsSearchMatchesText(const char *pText) const;
	static bool SettingsSearchLabelMatchCallback(void *pUser, const char *pText, const CUIRect *pRect);
	bool SettingsSearchHandleLabelMatch(const char *pText, const CUIRect *pRect);
	bool SettingsSearchIsHighlight(const char *pFocusKey) const;
	bool SettingsSearchConsumeReveal(const char *pFocusKey);
	bool IsBestClientFunTabActive() const;
	void ResetSettingsSearch();
	void SettingsSearchSetFocusKey(const char *pFocusKey);
	void SettingsSearchDrawLabel(const CUIRect *pRect, const char *pText, float Size, int Align, const char *pFocusKey);
	void SettingsSearchJumpToBestClient(int Tab, int VisualsSubTab, int GoresCategory);
	void SyncOldSettingsPageFromCurrent();
	void SyncCurrentSettingsPageFromOld();
	void RenderAutoScrollSettingsPage(CUIRect MainView, int ScrollPage, const std::function<float(CUIRect)> &RenderFn);

	// found in menus_settings_controls.cpp
	// TODO: Change PopupConfirm to avoid using a function pointer to a CMenus
	//       member function, to move this function to CMenusSettingsControls
	void ResetSettingsControls();

	std::vector<CButtonContainer> m_vButtonContainersNamePlateShow = {{}, {}, {}, {}};
	std::vector<CButtonContainer> m_vButtonContainersNamePlateKeyPresses = {{}, {}, {}, {}};

	class CMapListItem
	{
	public:
		char m_aFilename[IO_MAX_PATH_LENGTH];
		bool m_IsDirectory;
	};
	class CPopupMapPickerContext
	{
	public:
		std::vector<CMapListItem> m_vMaps;
		char m_aCurrentMapFolder[IO_MAX_PATH_LENGTH] = "";
		static int MapListFetchCallback(const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser);
		void MapListPopulate();
		CMenus *m_pMenus;
		int m_Selection;
	};

	static bool CompareFilenameAscending(const CMapListItem Lhs, const CMapListItem Rhs)
	{
		if(str_comp(Lhs.m_aFilename, "..") == 0)
			return true;
		if(str_comp(Rhs.m_aFilename, "..") == 0)
			return false;
		if(Lhs.m_IsDirectory != Rhs.m_IsDirectory)
			return Lhs.m_IsDirectory;
		return str_comp_filenames(Lhs.m_aFilename, Rhs.m_aFilename) < 0;
	}

	static CUi::EPopupMenuFunctionResult PopupMapPicker(void *pContext, CUIRect View, bool Active);

	void SetNeedSendInfo();
	void UpdateColors();
	void LoadMenuSfx();
	void UnloadMenuSfx();
	void OnButtonSoundEvent(CUi::EButtonSoundEvent Event);

	enum
	{
		MENU_SFX_POOL_HOVER = 0,
		MENU_SFX_POOL_CLICK,
		MENU_SFX_POOL_BACK,
		MENU_SFX_POOL_COUNT,
	};
	std::array<std::vector<int>, MENU_SFX_POOL_COUNT> m_avMenuSfxSamplePools;
	std::vector<int> m_vMenuSfxSamples;
	std::array<int, MENU_SFX_POOL_COUNT> m_aMenuSfxLastSample;
	std::array<int, MENU_SFX_POOL_COUNT> m_aMenuSfxPreferredSample;
	std::array<int64_t, MENU_SFX_POOL_COUNT> m_aMenuSfxLastPlayTick;
	int64_t m_MenuSfxLastPlayTickAny = 0;
	bool m_MenuSfxLoaded = false;

	IGraphics::CTextureHandle m_TextureBlob;

public:
	void RenderBackground();

	CMenus();
	int Sizeof() const override { return sizeof(*this); }

	void RenderLoading(const char *pCaption, const char *pContent, int IncreaseCounter);
	void FinishLoading();

	bool IsInit() const { return m_IsInit; }

	bool IsActive() const { return m_MenuActive; }
	void SetActive(bool Active);

	void OnInterfacesInit(CGameClient *pClient) override;
	void OnInit() override;

	void OnStateChange(int NewState, int OldState) override;
	void OnWindowResize() override;
	void OnReset() override;
	void OnRender() override;
	bool OnInput(const IInput::CEvent &Event) override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	void OnShutdown() override;

	enum
	{
		PAGE_NEWS = 1,
		PAGE_GAME,
		PAGE_PLAYERS,
		PAGE_SERVER_INFO,
		PAGE_CALLVOTE,
		PAGE_INTERNET,
		PAGE_LAN,
		PAGE_FAVORITES,
		PAGE_FAVORITE_COMMUNITY_1,
		PAGE_FAVORITE_COMMUNITY_2,
		PAGE_FAVORITE_COMMUNITY_3,
		PAGE_FAVORITE_COMMUNITY_4,
		PAGE_FAVORITE_COMMUNITY_5,
		PAGE_DEMOS,
		PAGE_SETTINGS,
		PAGE_NETWORK,
		PAGE_GHOST,

		PAGE_LENGTH,
	};

	enum
	{
		SETTINGS_GENERAL_TAB = 0,
		SETTINGS_APPEARANCE_TAB,
		SETTINGS_TCLIENT,
		SETTINGS_BESTCLIENT,
		SETTINGS_LENGTH,
	};

	enum
	{
		OLD_SETTINGS_GENERAL = 0,
		OLD_SETTINGS_TEE,
		OLD_SETTINGS_APPEARANCE,
		OLD_SETTINGS_CONTROLS,
		OLD_SETTINGS_GRAPHICS,
		OLD_SETTINGS_SOUND,
		OLD_SETTINGS_DDNET,
		OLD_SETTINGS_ASSETS,
		OLD_SETTINGS_TCLIENT,
		OLD_SETTINGS_BESTCLIENT,
		OLD_SETTINGS_LENGTH,
	};

	// Sub-tabs for General tab
	enum
	{
		GENERAL_SUBTAB_GENERAL = 0,
		GENERAL_SUBTAB_CONTROLS,
		GENERAL_SUBTAB_TEE,
		GENERAL_SUBTAB_DDNET,
		GENERAL_SUBTAB_LENGTH,
	};

	// Sub-tabs for Appearance tab
	enum
	{
		APPEARANCE_SUBTAB_APPEARANCE = 0,
		APPEARANCE_SUBTAB_GRAPHICS,
		APPEARANCE_SUBTAB_SOUND,
		APPEARANCE_SUBTAB_ASSETS,
		APPEARANCE_SUBTAB_LENGTH,
	};

	enum
	{
		APPEARANCE_TAB_HUD = 0,
		APPEARANCE_TAB_CHAT,
		APPEARANCE_TAB_NAME_PLATE,
		APPEARANCE_TAB_HOOK_COLLISION,
		APPEARANCE_TAB_INFO_MESSAGES,
		APPEARANCE_TAB_LASER,
		NUMBER_OF_APPEARANCE_TABS,
	};

	enum
	{
		BIG_TAB_NEWS = 0,
		BIG_TAB_INTERNET,
		BIG_TAB_LAN,
		BIG_TAB_FAVORITES,
		BIT_TAB_FAVORITE_COMMUNITY_1,
		BIT_TAB_FAVORITE_COMMUNITY_2,
		BIT_TAB_FAVORITE_COMMUNITY_3,
		BIT_TAB_FAVORITE_COMMUNITY_4,
		BIT_TAB_FAVORITE_COMMUNITY_5,
		BIG_TAB_DEMOS,

		BIG_TAB_LENGTH,
	};

	enum
	{
		SMALL_TAB_HOME = 0,
		SMALL_TAB_QUIT,
		SMALL_TAB_SETTINGS,
		SMALL_TAB_EDITOR,
		SMALL_TAB_DEMOBUTTON,
		SMALL_TAB_SERVER,
		SMALL_TAB_BROWSER_FILTER,
		SMALL_TAB_BROWSER_INFO,
		SMALL_TAB_BROWSER_BESTCLIENT,
		SMALL_TAB_BROWSER_FRIENDS,

		SMALL_TAB_LENGTH,
	};

	SUIAnimator m_aAnimatorsBigPage[BIG_TAB_LENGTH];
	SUIAnimator m_aAnimatorsSmallPage[SMALL_TAB_LENGTH];
	SUIAnimator m_aAnimatorsSettingsTab[SETTINGS_LENGTH];
	SUIAnimator m_aAnimatorsOldSettingsTab[OLD_SETTINGS_LENGTH];

	// DDRace
	int DoButton_CheckBox_Tristate(const void *pId, const char *pText, TRISTATE Checked, const CUIRect *pRect);
	std::vector<CDemoItem> m_vDemos;
	std::vector<CDemoItem *> m_vpFilteredDemos;
	void DemolistPopulate();
	void RefreshFilteredDemos();
	void DemoSeekTick(IDemoPlayer::ETickOffset TickOffset);
	bool m_Dummy;

	const char *GetCurrentDemoFolder() const { return m_aCurrentDemoFolder; }

	// Ghost
	struct CGhostItem
	{
		char m_aFilename[IO_MAX_PATH_LENGTH];
		char m_aPlayer[MAX_NAME_LENGTH];

		bool m_Failed;
		int m_Time;
		int m_Slot;
		bool m_Own;
		time_t m_Date;

		CGhostItem() :
			m_Slot(-1), m_Own(false) { m_aFilename[0] = 0; }

		bool operator<(const CGhostItem &Other) const { return m_Time < Other.m_Time; }

		bool Active() const { return m_Slot != -1; }
		bool HasFile() const { return m_aFilename[0]; }
	};

	enum
	{
		GHOST_SORT_NONE = -1,
		GHOST_SORT_NAME,
		GHOST_SORT_TIME,
		GHOST_SORT_DATE,
	};

	std::vector<CGhostItem> m_vGhosts;

	std::chrono::nanoseconds m_GhostPopulateStartTime{0};

	void GhostlistPopulate();
	CGhostItem *GetOwnGhost();
	void UpdateOwnGhost(CGhostItem Item);
	void DeleteGhostItem(int Index);
	void SortGhostlist();

	bool CanDisplayWarning() const;

	void PopupWarning(const char *pTopic, const char *pBody, const char *pButton, std::chrono::nanoseconds Duration);

	std::chrono::nanoseconds m_PopupWarningLastTime;
	std::chrono::nanoseconds m_PopupWarningDuration;

	int m_DemoPlayerState;

	enum
	{
		POPUP_NONE = 0,
		POPUP_MESSAGE, // generic message popup (one button)
		POPUP_CONFIRM, // generic confirmation popup (two buttons)
		POPUP_FIRST_LAUNCH,
		POPUP_JOIN_TUTORIAL,
		POPUP_POINTS,
		POPUP_DISCONNECTED,
		POPUP_LANGUAGE,
		POPUP_RENAME_DEMO,
		POPUP_RENDER_DEMO,
		POPUP_RENDER_DONE,
		POPUP_PASSWORD,
		POPUP_QUIT,
		POPUP_RESTART,
		POPUP_WARNING,
		POPUP_SAVE_SKIN,
	};

	enum
	{
		// demo player states
		DEMOPLAYER_NONE = 0,
		DEMOPLAYER_SLICE_SAVE,
	};

	void SetMenuPage(int NewPage);
	void RefreshBrowserTab(bool Force);
	void ForceRefreshLanPage();
	void SetShowStart(bool ShowStart);
	void ShowQuitPopup();
	void JoinTutorial();
	void OpenWorldEditorSettings();
	void CloseWorldEditorSettings();
	bool IsWorldEditorSettingsOpen() const;

private:
	CCommunityIcons m_CommunityIcons;
	CMenusIngameTouchControls m_MenusIngameTouchControls;
	friend CMenusIngameTouchControls;
	CMenusSettingsControls m_MenusSettingsControls;
	friend CMenusSettingsControls;
	CMenusStart m_MenusStart;

	static int GhostlistFetchCallback(const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser);

	// found in menus_ingame.cpp
	void RenderInGameNetwork(CUIRect MainView);
	void RenderGhost(CUIRect MainView);

	// found in menus_settings.cpp
	float RenderSettingsDDNet(CUIRect MainView);
	float RenderSettingsAppearance(CUIRect MainView);

	// found in menus_BestClient.cpp
	void RenderSettingsBestClient(CUIRect MainView);
	void RenderSettingsBestClientGoresFeatures(CUIRect MainView);
	void RenderSettingsBestClientVisuals(CUIRect MainView);
	void RenderSettingsBestClientRaceFeatures(CUIRect MainView);
	void RenderSettingsBestClientVoice(CUIRect MainView);
	void RenderSettingsBestClientInfo(CUIRect MainView);
	void RenderSettingsBestClientShop(CUIRect MainView);
	void RenderSettingsBestClientPageByTab(int Tab, CUIRect MainView);
	void RenderSettingsBestClientVisualsPage(int Tab, CUIRect MainView);
	void RenderSettingsBestClientWorldEditor(CUIRect MainView);
	void RenderSettingsBestClientProfiles(CUIRect MainView);
	void RenderSettingsBestClientConfigs(CUIRect MainView);
	// found in menus_games.cpp
	void RenderSettingsBestClientFun(CUIRect MainView);
	void RenderTeeCute(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, int Emote, vec2 Dir, vec2 Pos, bool CuteEyes, float Alpha = 1.0f);

	const CWarType *m_pRemoveWarType = nullptr;
	void PopupConfirmRemoveWarType();
	void RenderDevSkin(vec2 RenderPos, float Size, const char *pSkinName, const char *pBackupSkin, bool CustomColors, int FeetColor, int BodyColor, int Emote, bool Rainbow, bool Cute,
		ColorRGBA ColorFeet = ColorRGBA(0, 0, 0, 0), ColorRGBA ColorBody = ColorRGBA(0, 0, 0, 0));
	void RenderFontIcon(CUIRect Rect, const char *pText, float Size, int Align);
	int DoButtonNoRect_FontIcon(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, int Corners = IGraphics::CORNER_ALL);

	ColorHSLA RenderHSLColorPicker(const CUIRect *pRect, unsigned int *pColor, bool Alpha);
	bool RenderHslaScrollbars(CUIRect *pRect, unsigned int *pColor, bool Alpha, float DarkestLight);
	int DoButtonLineSize_Menu(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, float ButtonLineSize, bool Fake = false, const char *pImageName = nullptr, int Corners = IGraphics::CORNER_ALL, float Rounding = 5.0f, float FontFactor = 0.0f, ColorRGBA Color = ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f));
};
#endif
