/* Copyright © 2026 BestProject Team */
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/client/animstate.h>
#include <game/client/bc_ui_animations.h>
#include <game/client/components/bestclient/cherry_gifs.h>
#include <game/client/components/bestclient/gif_wheel.h>
#include <game/client/components/chat.h>
#include <game/client/components/menus.h>
#include <game/client/components/skins.h>
#include <game/client/gameclient.h>
#include <game/client/lineinput.h>
#include <game/client/render.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>

static const float FontSize = 14.0f;
static const float LineSize = 20.0f;
static const float MarginSmall = 5.0f;
static const float MarginBetweenViews = 20.0f;

// Renders the "Gif Wheel" settings tab: a CherryGifs browser on the left (search, sort, NSFW
// toggle, infinite-ish grid) and the wheel editor circle on the right. Assigning a gif to the
// wheel is drag & drop only, there is no manual command entry (unlike the bindwheel editor)
// since there is no command to type - just a gif to drop.
void CMenus::RenderSettingsBestClientGifWheel(CUIRect MainView)
{
	CCherryGifs &CherryGifs = GameClient()->m_CherryGifs;
	CGifWheel &GifWheel = GameClient()->m_GifWheel;
	GifWheel.EnsureThumbnails();

	CUIRect LeftView, RightView;
	MainView.VSplitLeft(MainView.w / 1.8f, &LeftView, &RightView);
	LeftView.VSplitRight(MarginBetweenViews / 2.0f, &LeftView, nullptr);
	RightView.VSplitLeft(MarginBetweenViews / 2.0f, nullptr, &RightView);

	// ---------------------------------------------------------------
	// Left: search + sort/NSFW + grid browser
	// ---------------------------------------------------------------
	static CLineInputBuffered<64> s_SearchInput;

	// Drag state must be declared before the grid loop (it starts the drag) and is also
	// read while rendering the wheel on the right (it finishes the drag there).
	static bool s_DragActive = false;
	static char s_aDragGifId[32] = "";
	static char s_aDragUrl[256] = "";
	static char s_aDragCaption[128] = "";
	static IGraphics::CTextureHandle s_DragThumbnail;

	CUIRect SearchRow, SortRow, Grid, StatusRow;
	LeftView.HSplitTop(LineSize, &SearchRow, &LeftView);
	Ui()->DoEditBox_Search(&s_SearchInput, &SearchRow, 14.0f, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize, &SortRow, &LeftView);
	CUIRect SortToggle, NsfwToggle;
	SortRow.VSplitMid(&SortToggle, &NsfwToggle, MarginSmall);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcCherryGifsSortTop, Localize("Sort by top"), &g_Config.m_BcCherryGifsSortTop, &SortToggle, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcCherryGifsShowNsfw, Localize("Show NSFW"), &g_Config.m_BcCherryGifsShowNsfw, &NsfwToggle, LineSize);

	CherryGifs.SetFilters(s_SearchInput.GetString(), g_Config.m_BcCherryGifsSortTop != 0, g_Config.m_BcCherryGifsShowNsfw != 0);

	LeftView.HSplitBottom(LineSize, &LeftView, &StatusRow);
	LeftView.HSplitBottom(MarginSmall, &LeftView, nullptr);
	LeftView.HSplitTop(MarginSmall, nullptr, &Grid);
	Grid = LeftView;

	const std::vector<SCherryGif> &vResults = CherryGifs.Results();
	static CListBox s_ListBox;
	constexpr float CardSize = 84.0f;
	constexpr float CardMargin = 8.0f;
	const int ItemsPerRow = std::max(1, (int)(Grid.w / (CardSize + CardMargin)));

	if(vResults.empty() && CherryGifs.IsLoading())
	{
		// Skeleton placeholders while the first page is still in flight, so the tab shows
		// something immediately instead of a blank grid for the second or two until it arrives.
		const int PlaceholderRows = (int)std::ceil(Grid.h / (CardSize + 20.0f)) + 1;
		CUIRect PlaceholderGrid = Grid;
		for(int Row = 0; Row < PlaceholderRows; Row++)
		{
			CUIRect RowRect, RowRemaining;
			PlaceholderGrid.HSplitTop(CardSize + 20.0f, &RowRect, &PlaceholderGrid);
			RowRemaining = RowRect;
			for(int Col = 0; Col < ItemsPerRow; Col++)
			{
				CUIRect CardRect;
				RowRemaining.VSplitLeft(CardSize + CardMargin, &CardRect, &RowRemaining);
				CardRect.Margin(CardMargin / 2.0f, &CardRect);
				CardRect.HSplitBottom(14.0f, &CardRect, nullptr);
				Graphics()->DrawRect(CardRect.x, CardRect.y, CardRect.w, CardRect.h, ColorRGBA(0.15f, 0.15f, 0.15f, 0.35f), IGraphics::CORNER_ALL, 6.0f);
			}
		}
	}
	else
	{
		s_ListBox.DoStart(CardSize + 20.0f, (int)vResults.size(), ItemsPerRow, 1, -1, &Grid, false);

		bool LastItemVisible = false;
		for(int i = 0; i < (int)vResults.size(); i++)
		{
			const SCherryGif &Gif = vResults[i];
			const CListboxItem Item = s_ListBox.DoNextItem(&Gif, false);
			if(!Item.m_Visible)
				continue;
			if(i == (int)vResults.size() - 1)
				LastItemVisible = true;

			CUIRect CardRect = Item.m_Rect;
			CardRect.Margin(CardMargin / 2.0f, &CardRect);

			CUIRect Thumb, Caption;
			CardRect.HSplitBottom(14.0f, &Thumb, &Caption);

			CherryGifs.RequestThumbnail(i);

			if(Gif.m_Thumbnail.IsValid())
			{
				Graphics()->WrapClamp();
				Graphics()->TextureSet(Gif.m_Thumbnail);
				Graphics()->QuadsSetSubset(0, 0, 1, 1);
				Graphics()->QuadsBegin();
				Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
				IGraphics::CQuadItem QuadItem(Thumb.x, Thumb.y, Thumb.w, Thumb.h);
				Graphics()->QuadsDrawTL(&QuadItem, 1);
				Graphics()->QuadsEnd();
				Graphics()->WrapNormal();
			}
			else
			{
				Graphics()->DrawRect(Thumb.x, Thumb.y, Thumb.w, Thumb.h, ColorRGBA(0.15f, 0.15f, 0.15f, 0.5f), IGraphics::CORNER_ALL, 6.0f);
			}

			char aCaptionText[64];
			if(Gif.m_aCaption[0] != '\0')
				str_copy(aCaptionText, Gif.m_aCaption, sizeof(aCaptionText));
			else if(!Gif.m_vTags.empty())
				str_copy(aCaptionText, Gif.m_vTags.front().c_str(), sizeof(aCaptionText));
			else
				str_format(aCaptionText, sizeof(aCaptionText), "%d %s", Gif.m_Likes, Localize("likes"));
			Ui()->DoLabel(&Caption, aCaptionText, 9.0f, TEXTALIGN_MC);

			// Start a drag on mouse-down (not a clean click) over the thumbnail. Copy the gif's
			// values now: vResults can reallocate on the next page load while dragging, so we must
			// not keep a raw pointer/index into it.
			if(!s_DragActive && Ui()->MouseButton(0) && !Ui()->MouseButtonClicked(0) && Ui()->MouseInside(&Thumb))
			{
				s_DragActive = true;
				str_copy(s_aDragGifId, Gif.m_aId, sizeof(s_aDragGifId));
				str_copy(s_aDragUrl, Gif.m_aUrl, sizeof(s_aDragUrl));
				str_copy(s_aDragCaption, Gif.m_aCaption, sizeof(s_aDragCaption));
				s_DragThumbnail = Gif.m_Thumbnail;
			}
		}
		s_ListBox.DoEnd();

		// Auto-load the next page once the last loaded card scrolls into view, instead of making
		// the user find and click a separate "Load more" button.
		if(LastItemVisible && CherryGifs.HasMore() && !CherryGifs.IsLoading())
			CherryGifs.LoadMore();
	}

	if(CherryGifs.HasError())
	{
		TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
		Ui()->DoLabel(&StatusRow, CherryGifs.ErrorText(), 12.0f, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	}
	else if(CherryGifs.IsLoading() && !vResults.empty())
	{
		Ui()->DoLabel(&StatusRow, Localize("Loading..."), 12.0f, TEXTALIGN_ML);
	}

	// ---------------------------------------------------------------
	// Right: wheel editor circle
	// ---------------------------------------------------------------
	CUIRect Hints;
	RightView.HSplitBottom(LineSize * 4.0f + MarginSmall * 3.0f, &RightView, &Hints);

	const float Radius = std::min(RightView.w, RightView.h) / 2.0f;
	const vec2 Center = RightView.Center();

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.3f);
	Graphics()->DrawCircle(Center.x, Center.y, Radius, 64);
	Graphics()->QuadsEnd();

	static int s_SelectedSlotIndex = -1;
	int HoveringIndex = -1;
	int SegmentCount = (int)GifWheel.m_vSlots.size();
	const float MouseDist = distance(Center, Ui()->MousePos());

	if(SegmentCount > 0 && MouseDist < Radius && MouseDist > Radius * 0.25f)
	{
		const float SegmentAngle = 2.0f * pi / SegmentCount;
		float HoveringAngle = angle(Ui()->MousePos() - Center) + SegmentAngle / 2.0f;
		if(HoveringAngle < 0.0f)
			HoveringAngle += 2.0f * pi;
		HoveringIndex = (int)(HoveringAngle / (2.0f * pi) * SegmentCount);
		HoveringIndex = std::clamp(HoveringIndex, 0, SegmentCount - 1);

		if(!s_DragActive)
		{
			if(Ui()->MouseButtonClicked(0))
			{
				s_SelectedSlotIndex = HoveringIndex;
			}
			else if(Ui()->MouseButtonClicked(1))
			{
				// Right-click a segment to remove it immediately - no need to select it first
				// and then hunt for the separate Remove button.
				GifWheel.RemoveSlot(HoveringIndex);
				s_SelectedSlotIndex = -1;
				// The segment loop below still reads m_vSlots by index this same frame - keep it
				// in sync with the vector that just shrank, or it reads out of bounds.
				SegmentCount = (int)GifWheel.m_vSlots.size();
				HoveringIndex = -1;
			}
		}
	}
	else if(!s_DragActive && MouseDist < Radius && Ui()->MouseButtonClicked(0))
	{
		s_SelectedSlotIndex = -1;
	}
	if(s_SelectedSlotIndex >= SegmentCount)
		s_SelectedSlotIndex = -1;

	const float Theta = pi * 2.0f / std::max(1, SegmentCount);
	for(int i = 0; i < SegmentCount; i++)
	{
		const CGifWheel::CSlot &Slot = GifWheel.m_vSlots[i];
		const float Angle = Theta * i;
		const vec2 Pos = Center + direction(Angle) * (Radius * 0.72f);
		const float Size = i == s_SelectedSlotIndex ? 62.0f : (i == HoveringIndex ? 54.0f : 46.0f);

		if(i == s_SelectedSlotIndex)
			Graphics()->DrawRect(Pos.x - Size / 2.0f - 4.0f, Pos.y - Size / 2.0f - 4.0f, Size + 8.0f, Size + 8.0f, ColorRGBA(0.5f, 1.0f, 0.75f, 0.9f), IGraphics::CORNER_ALL, (Size + 8.0f) * 0.15f);

		if(Slot.m_Thumbnail.IsValid())
		{
			Graphics()->WrapClamp();
			Graphics()->TextureSet(Slot.m_Thumbnail);
			Graphics()->QuadsSetSubset(0, 0, 1, 1);
			Graphics()->QuadsBegin();
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
			IGraphics::CQuadItem QuadItem(Pos.x - Size / 2.0f, Pos.y - Size / 2.0f, Size, Size);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
			Graphics()->WrapNormal();
		}
		else
		{
			Graphics()->DrawRect(Pos.x - Size / 2.0f, Pos.y - Size / 2.0f, Size, Size, ColorRGBA(0.15f, 0.15f, 0.15f, 0.6f), IGraphics::CORNER_ALL, Size * 0.15f);
		}
	}

	if(SegmentCount == 0)
	{
		CUIRect EmptyLabel{Center.x - 90.0f, Center.y - 8.0f, 180.0f, 16.0f};
		Ui()->DoLabel(&EmptyLabel, Localize("Drag gifs here from the left"), 12.0f, TEXTALIGN_MC);
	}

	// Finish the drag: dropping anywhere inside the circle always adds a new slot. Replacing an
	// existing slot by precise drop position was confusing (it only "worked" if you dropped
	// dead center) - removing an existing slot is a right-click instead (see above).
	if(s_DragActive && !Ui()->MouseButton(0))
	{
		if(MouseDist < Radius)
			GifWheel.AddSlot(s_aDragGifId, s_aDragUrl, s_aDragCaption);
		s_DragActive = false;
	}

	if(s_DragActive)
	{
		constexpr float DragSize = 54.0f;
		const float DragX = Ui()->MouseX() + 12.0f;
		const float DragY = Ui()->MouseY() + 12.0f;
		if(s_DragThumbnail.IsValid())
		{
			Graphics()->WrapClamp();
			Graphics()->TextureSet(s_DragThumbnail);
			Graphics()->QuadsSetSubset(0, 0, 1, 1);
			Graphics()->QuadsBegin();
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.9f);
			IGraphics::CQuadItem QuadItem(DragX, DragY, DragSize, DragSize);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
			Graphics()->WrapNormal();
		}
		else
		{
			Graphics()->DrawRect(DragX, DragY, DragSize, DragSize, ColorRGBA(0.2f, 0.2f, 0.2f, 0.85f), IGraphics::CORNER_ALL, 6.0f);
		}
	}

	CUIRect HintLine1, HintLine2;
	Hints.HSplitTop(LineSize * 0.8f, &HintLine1, &Hints);
	Hints.HSplitTop(LineSize * 0.8f, &HintLine2, &Hints);
	Ui()->DoLabel(&HintLine1, Localize("Drag a gif onto the wheel to assign it"), FontSize * 0.8f, TEXTALIGN_MC);
	Ui()->DoLabel(&HintLine2, Localize("Right-click a segment to remove it"), FontSize * 0.8f, TEXTALIGN_MC);

	if(s_SelectedSlotIndex >= 0 && s_SelectedSlotIndex < SegmentCount)
	{
		CGifWheel::CSlot &SelectedSlot = GifWheel.m_vSlots[s_SelectedSlotIndex];

		// Assign which eye emote (if any) plays alongside this gif - same eye states as the
		// regular emote wheel, applied via the same /emote command when the slot is sent. Shown as
		// the player's own tee (same pattern as the Tee settings page) instead of plain text.
		CUIRect EyeLabelRow, EyeButtonsRow;
		Hints.HSplitTop(MarginSmall, nullptr, &Hints);
		Hints.HSplitTop(LineSize * 0.7f, &EyeLabelRow, &Hints);
		Ui()->DoLabel(&EyeLabelRow, Localize("Eyes"), FontSize * 0.75f, TEXTALIGN_MC);

		Hints.HSplitTop(LineSize * 1.6f, &EyeButtonsRow, &Hints);

		const CSkin *pDefaultSkin = GameClient()->m_Skins.Find("default");
		const CSkins::CSkinContainer *pOwnSkinContainer = GameClient()->m_Skins.FindContainerOrNullptr(g_Config.m_ClPlayerSkin[0] == '\0' ? "default" : g_Config.m_ClPlayerSkin);
		if(pOwnSkinContainer != nullptr && pOwnSkinContainer->IsSpecial())
			pOwnSkinContainer = nullptr;

		CTeeRenderInfo EyeSkinInfo;
		EyeSkinInfo.Apply(pOwnSkinContainer == nullptr || pOwnSkinContainer->Skin() == nullptr ? pDefaultSkin : pOwnSkinContainer->Skin().get());
		EyeSkinInfo.ApplyColors(g_Config.m_ClPlayerUseCustomColor, g_Config.m_ClPlayerColorBody, g_Config.m_ClPlayerColorFeet);
		EyeSkinInfo.m_Size = EyeButtonsRow.h;
		vec2 OffsetToMid;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &EyeSkinInfo, OffsetToMid);

		const float EyeButtonWidth = EyeButtonsRow.w / (float)(NUM_EMOTES + 1);

		// "None", to the left of "Normal" - the default for freshly dropped gifs. Unlike the other
		// options it doesn't send an /emote command at all, so it leaves whatever eyes the tee
		// already had (e.g. Blink) alone instead of forcing them back to Normal.
		{
			CUIRect NoneButtonRect;
			EyeButtonsRow.VSplitLeft(EyeButtonWidth, &NoneButtonRect, &EyeButtonsRow);
			NoneButtonRect.VMargin(1.5f, &NoneButtonRect);
			static CButtonContainer s_NoneEyeButton;
			const ColorRGBA NoneButtonColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f + (SelectedSlot.m_EyeEmote < 0 ? 0.25f : 0.0f));
			if(DoButton_Menu(&s_NoneEyeButton, "", 0, &NoneButtonRect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, NoneButtonColor))
				GifWheel.SetSlotEyeEmote(s_SelectedSlotIndex, -1);
			GameClient()->m_Tooltips.DoToolTip(&s_NoneEyeButton, &NoneButtonRect, Localize("None"));
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.7f);
			Ui()->DoLabel(&NoneButtonRect, FontIcon::EYE_SLASH, NoneButtonRect.h * 0.4f, TEXTALIGN_MC);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		}

		static const char *apEyeNames[NUM_EMOTES] = {"Normal", "Pain", "Happy", "Surprise", "Angry", "Blink"};
		static CButtonContainer s_aEyeButtons[NUM_EMOTES];
		for(int EyeValue = 0; EyeValue < NUM_EMOTES; EyeValue++)
		{
			CUIRect EyeButtonRect;
			EyeButtonsRow.VSplitLeft(EyeButtonWidth, &EyeButtonRect, &EyeButtonsRow);
			EyeButtonRect.VMargin(1.5f, &EyeButtonRect);
			const ColorRGBA EyeButtonColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f + (SelectedSlot.m_EyeEmote == EyeValue ? 0.25f : 0.0f));
			if(DoButton_Menu(&s_aEyeButtons[EyeValue], "", 0, &EyeButtonRect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, EyeButtonColor))
				GifWheel.SetSlotEyeEmote(s_SelectedSlotIndex, EyeValue);
			GameClient()->m_Tooltips.DoToolTip(&s_aEyeButtons[EyeValue], &EyeButtonRect, Localize(apEyeNames[EyeValue]));
			RenderTools()->RenderTee(CAnimState::GetIdle(), &EyeSkinInfo, EyeValue, vec2(1.0f, 0.0f), vec2(EyeButtonRect.x + EyeButtonRect.w / 2.0f, EyeButtonRect.y + EyeButtonRect.h / 2.0f + OffsetToMid.y));
		}
	}
}

static void UpdateChatMediaRevealPhase(float &Phase, bool Expanded, float Dt)
{
	if(BCUiAnimations::Enabled() && g_Config.m_BcModuleUiRevealAnimation != 0)
		BCUiAnimations::UpdatePhase(Phase, Expanded ? 1.0f : 0.0f, Dt, BCUiAnimations::MsToSeconds(g_Config.m_BcModuleUiRevealAnimationMs));
	else
		Phase = Expanded ? 1.0f : 0.0f;
}

// The "Chat Media" settings block (Visuals tab, top of the left column). Carries the "Gif Wheel"
// launcher button, which is how the wheel editor is reached now - there is no separate top-level
// BestClient tab for it anymore, to avoid piling up tabs.
void CMenus::RenderSettingsBestClientChatMediaBlock(CUIRect &Column)
{
	const float ChatMediaLineSize = 20.0f;
	const float ChatMediaMarginSmall = 5.0f;
	const float ChatMediaHeadlineFontSize = 20.0f;
	const float ChatMediaMarginBetweenViews = 30.0f;
	const float BlockPadding = ChatMediaMarginBetweenViews * 0.6666f;
	const ColorRGBA BlockColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f);

	CUIRect Content, Label, Button;

	const bool ChatMediaEnabled = g_Config.m_BcChatMediaPreview != 0;
	static float s_ChatMediaRevealPhase = 0.0f;
	UpdateChatMediaRevealPhase(s_ChatMediaRevealPhase, ChatMediaEnabled, Client()->RenderFrameTime());
	const float ChatMediaHeaderHeight = ChatMediaLineSize + ChatMediaMarginSmall + ChatMediaLineSize + ChatMediaMarginSmall + ChatMediaLineSize;
	const float ChatMediaDomainsHeight = g_Config.m_BcChatMediaContentFilter ? 2.0f * (ChatMediaMarginSmall + ChatMediaLineSize) : 0.0f;
	const float ChatMediaExpandedHeight = (5.0f * (ChatMediaMarginSmall + ChatMediaLineSize) + ChatMediaDomainsHeight) * s_ChatMediaRevealPhase;
	const float ChatMediaBlockHeight = ChatMediaHeaderHeight + ChatMediaExpandedHeight;

	CUIRect ChatMediaBlock;
	Column.HSplitTop(ChatMediaBlockHeight, &ChatMediaBlock, &Column);

	CUIRect ChatMediaBlockBg = ChatMediaBlock;
	ChatMediaBlockBg.w += BlockPadding;
	ChatMediaBlockBg.h += BlockPadding;
	ChatMediaBlockBg.x -= BlockPadding * 0.5f;
	ChatMediaBlockBg.y -= BlockPadding * 0.5f;
	ChatMediaBlockBg.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);

	ChatMediaBlock.HSplitTop(ChatMediaLineSize, &Label, &ChatMediaBlock);
	CUIRect ChatMediaBadge = Label;
	ChatMediaBadge.VSplitRight(36.0f, nullptr, &ChatMediaBadge);
	ChatMediaBadge.HMargin(1.5f, &ChatMediaBadge);
	Ui()->DoLabel(&Label, Localize("Chat Media"), ChatMediaHeadlineFontSize, TEXTALIGN_ML);
	Graphics()->DrawRect4(ChatMediaBadge.x, ChatMediaBadge.y, ChatMediaBadge.w, ChatMediaBadge.h,
		ColorRGBA(1.00f, 0.76f, 0.16f, 1.0f), ColorRGBA(0.92f, 0.56f, 0.02f, 1.0f),
		ColorRGBA(1.00f, 0.76f, 0.16f, 1.0f), ColorRGBA(0.92f, 0.56f, 0.02f, 1.0f),
		IGraphics::CORNER_ALL, 5.0f);
	Ui()->DoLabel(&ChatMediaBadge, Localize("UPD"), 11.0f, TEXTALIGN_MC);
	ChatMediaBlock.HSplitTop(ChatMediaMarginSmall, nullptr, &ChatMediaBlock);

	ChatMediaBlock.HSplitTop(ChatMediaLineSize, &Button, &ChatMediaBlock);
	static CButtonContainer s_OpenGifWheelButton;
	if(DoButton_Menu(&s_OpenGifWheelButton, Localize("Gif Wheel"), 0, &Button))
		m_GifWheelEditorOpen = true;
	ChatMediaBlock.HSplitTop(ChatMediaMarginSmall, nullptr, &ChatMediaBlock);

	CChat &ChatMediaChat = GameClient()->m_Chat;
	ChatMediaBlock.HSplitTop(ChatMediaLineSize, &Content, &ChatMediaBlock);
	if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatMediaPreview, Localize("Render media previews from chat links"), &g_Config.m_BcChatMediaPreview, &Content, ChatMediaLineSize))
		ChatMediaChat.RebuildChat();

	if(ChatMediaExpandedHeight > 0.5f)
	{
		CUIRect Visible = ChatMediaBlock;
		Visible.h = ChatMediaExpandedHeight;
		Ui()->ClipEnable(&Visible);

		ChatMediaBlock.HSplitTop(ChatMediaMarginSmall, nullptr, &ChatMediaBlock);
		ChatMediaBlock.HSplitTop(ChatMediaLineSize, &Content, &ChatMediaBlock);
		if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatMediaPhotos, Localize("Show photos in chat media"), &g_Config.m_BcChatMediaPhotos, &Content, ChatMediaLineSize))
			ChatMediaChat.RebuildChat();

		ChatMediaBlock.HSplitTop(ChatMediaMarginSmall, nullptr, &ChatMediaBlock);
		ChatMediaBlock.HSplitTop(ChatMediaLineSize, &Content, &ChatMediaBlock);
		if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatMediaGifs, Localize("Show GIFs in chat media"), &g_Config.m_BcChatMediaGifs, &Content, ChatMediaLineSize))
			ChatMediaChat.RebuildChat();

		ChatMediaBlock.HSplitTop(ChatMediaMarginSmall, nullptr, &ChatMediaBlock);
		ChatMediaBlock.HSplitTop(ChatMediaLineSize, &Content, &ChatMediaBlock);
		if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatMediaContentFilter, Localize("Content filtering"), &g_Config.m_BcChatMediaContentFilter, &Content, ChatMediaLineSize))
			ChatMediaChat.RebuildChat();

		if(g_Config.m_BcChatMediaContentFilter)
		{
			ChatMediaBlock.HSplitTop(ChatMediaMarginSmall, nullptr, &ChatMediaBlock);
			ChatMediaBlock.HSplitTop(ChatMediaLineSize, &Label, &ChatMediaBlock);
			Ui()->DoLabel(&Label, Localize("Allowed media domains"), 12.0f, TEXTALIGN_ML);

			ChatMediaBlock.HSplitTop(ChatMediaMarginSmall, nullptr, &ChatMediaBlock);
			ChatMediaBlock.HSplitTop(ChatMediaLineSize, &Button, &ChatMediaBlock);
			static CLineInput s_ChatMediaAllowedDomains(g_Config.m_BcChatMediaAllowedDomains, sizeof(g_Config.m_BcChatMediaAllowedDomains));
			s_ChatMediaAllowedDomains.SetEmptyText("tenor.com; imgur.com; giphy.com; gifs.teeworlds.xyz");
			if(Ui()->DoClearableEditBox(&s_ChatMediaAllowedDomains, &Button, 14.0f))
				ChatMediaChat.RebuildChat();
			GameClient()->m_Tooltips.DoToolTip(&s_ChatMediaAllowedDomains, &Button, Localize("Semicolon-separated allowlist, for example: tenor.com; imgur.com; giphy.com; gifs.teeworlds.xyz; cdn.discordapp.com"));
		}

		ChatMediaBlock.HSplitTop(ChatMediaMarginSmall, nullptr, &ChatMediaBlock);
		ChatMediaBlock.HSplitTop(ChatMediaLineSize, &Button, &ChatMediaBlock);
		if(Ui()->DoScrollbarOption(&g_Config.m_BcChatMediaPreviewMaxWidth, &g_Config.m_BcChatMediaPreviewMaxWidth, &Button, Localize("Media preview width"), 120, 400))
			ChatMediaChat.RebuildChat();

		ChatMediaBlock.HSplitTop(ChatMediaMarginSmall, nullptr, &ChatMediaBlock);
		ChatMediaBlock.HSplitTop(ChatMediaLineSize, &Label, &ChatMediaBlock);
		static CButtonContainer s_HideMediaBindReader;
		static CButtonContainer s_HideMediaBindClear;
		DoLine_KeyReader(Label, s_HideMediaBindReader, s_HideMediaBindClear, Localize("Hide media bind"), "toggle_chat_media_hidden");

		Ui()->ClipDisable();
	}
}

// Fullscreen takeover for the Gif Wheel editor, opened via the button in the Chat Media block.
// Reserves the top 24 units for the persistent menu tab bar (CMenus::Render draws it after page
// content, so anything covering that band would get painted over) - same rule the assets editor
// fullscreen screen follows.
void CMenus::RenderSettingsBestClientGifWheelFullscreen(CUIRect Screen)
{
	Screen.HSplitTop(24.0f, nullptr, &Screen);

	if(Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
	{
		m_GifWheelEditorOpen = false;
		return;
	}

	CUIRect EditorRect = Screen;
	EditorRect.Margin(8.0f, &EditorRect);
	EditorRect.Draw(ms_ColorTabbarActive, IGraphics::CORNER_ALL, 8.0f);
	Ui()->ClipEnable(&EditorRect);

	CUIRect WorkRect;
	EditorRect.Margin(8.0f, &WorkRect);

	CUIRect TopBar, Content;
	WorkRect.HSplitTop(28.0f, &TopBar, &Content);
	Content.HSplitTop(8.0f, nullptr, &Content);

	CUIRect CloseButton;
	TopBar.VSplitLeft(28.0f, &CloseButton, &TopBar);
	TopBar.VSplitLeft(8.0f, nullptr, &TopBar);

	static CButtonContainer s_CloseGifWheelEditorButton;
	const bool Closed = Ui()->DoButton_FontIcon(&s_CloseGifWheelEditorButton, FontIcon::XMARK, 0, &CloseButton, IGraphics::CORNER_ALL);
	Ui()->DoLabel(&TopBar, Localize("Gif Wheel"), 20.0f, TEXTALIGN_ML);

	RenderSettingsBestClientGifWheel(Content);

	Ui()->ClipDisable();

	if(Closed)
		m_GifWheelEditorOpen = false;
}
