/* Copyright © 2026 BestProject Team */
#include "chat_scroll.h"

#include <base/str.h>
#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/bestclient/scroll_bar.h>
#include <game/client/components/chat.h>
#include <game/client/components/tclient/colored_parts.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>
#include <string>

namespace
{
constexpr float CHAT_WHEEL_SCROLL_UNIT = 40.0f;
}

float CChatScroll::LineHeight(const CChat &Chat, int LineIndex, int OffsetType, float FontSize, float RealMsgPaddingY)
{
	const CChat::CLine &Line = Chat.m_aLines[((Chat.m_CurrentLine - LineIndex) + CChat::MAX_LINES) % CChat::MAX_LINES];
	float LineH = Line.m_aYOffset[OffsetType];
	if(LineH < 0.0f)
		LineH = FontSize + RealMsgPaddingY;
	return LineH;
}

float CChatScroll::MaxScrollOffset(const CChat &Chat, float Y, float HeightLimit, int OffsetType, float FontSize, float RealMsgPaddingY)
{
	int TotalLines = 0;
	for(int i = 0; i < CChat::MAX_LINES; i++)
	{
		const CChat::CLine &Line = Chat.m_aLines[((Chat.m_CurrentLine - i) + CChat::MAX_LINES) % CChat::MAX_LINES];
		if(!Line.m_Initialized)
			break;
		TotalLines++;
	}

	float TempY = Y;
	int VisibleLines = 0;
	for(int i = 0; i < CChat::MAX_LINES; i++)
	{
		const CChat::CLine &Line = Chat.m_aLines[((Chat.m_CurrentLine - i) + CChat::MAX_LINES) % CChat::MAX_LINES];
		if(!Line.m_Initialized)
			break;
		const float LineH = LineHeight(Chat, i, OffsetType, FontSize, RealMsgPaddingY);
		if(TempY - LineH < HeightLimit)
			break;
		TempY -= LineH;
		VisibleLines++;
	}

	const int MaxLine = std::max(0, TotalLines - std::max(1, VisibleLines));
	float Offset = 0.0f;
	for(int i = 0; i < MaxLine; i++)
		Offset += LineHeight(Chat, i, OffsetType, FontSize, RealMsgPaddingY);
	return Offset;
}

void CChatScroll::Reset(CChat &Chat)
{
	Chat.m_BacklogScrollOffset = 0.0f;
	Chat.m_BacklogScrollOffsetChange = 0.0f;
	Chat.m_ChatMaxScrollOffset = 0.0f;
	Chat.m_ScrollbarDragging = false;
	Chat.m_ScrollbarDragOffset = 0.0f;
	Chat.m_ActionLineIndex = -1;
	Chat.m_PrevModeActive = false;
}

void CChatScroll::OnDisableMode(CChat &Chat)
{
	Chat.m_BacklogScrollOffset = 0.0f;
	Chat.m_BacklogScrollOffsetChange = 0.0f;
	Chat.m_ScrollbarDragging = false;
	Chat.m_ActionLineIndex = -1;
}

void CChatScroll::OnLineAdded(CChat &Chat)
{
	if(Chat.m_BacklogScrollOffset <= 0.0f)
		return;

	const float LineH = LineHeight(Chat, 0, 0, Chat.FontSize(), Chat.MessagePaddingY());
	Chat.m_BacklogScrollOffset = std::min(Chat.m_BacklogScrollOffset + LineH, Chat.m_ChatMaxScrollOffset);
}

bool CChatScroll::OnInput(CChat &Chat, const IInput::CEvent &Event)
{
	if(Chat.BcGameClient()->Ui()->IsPopupOpen())
		return false;

	if(Chat.m_Mode != CChat::MODE_NONE || Chat.m_Show)
	{
		if(Event.m_Flags & IInput::FLAG_PRESS)
		{
			if(Event.m_Key == KEY_MOUSE_WHEEL_UP)
			{
				Chat.m_BacklogScrollOffsetChange += CHAT_WHEEL_SCROLL_UNIT;
				Chat.m_ActionLineIndex = -1;
				return true;
			}
			if(Event.m_Key == KEY_MOUSE_WHEEL_DOWN)
			{
				Chat.m_BacklogScrollOffsetChange -= CHAT_WHEEL_SCROLL_UNIT;
				Chat.m_ActionLineIndex = -1;
				return true;
			}
		}
	}

	if(Chat.m_Mode == CChat::MODE_NONE && Chat.m_Show && (Event.m_Key == KEY_MOUSE_1 || Event.m_Key == KEY_MOUSE_2) && (Event.m_Flags & (IInput::FLAG_PRESS | IInput::FLAG_RELEASE)))
		return true;

	return false;
}

vec2 CChatScroll::MousePos(CChat &Chat)
{
	CGameClient *pClient = Chat.BcGameClient();
	const float Height = 300.0f;
	const float Width = Height * pClient->Graphics()->ScreenAspect();
	const vec2 WindowSize(std::max(1.0f, (float)pClient->Graphics()->WindowWidth()), std::max(1.0f, (float)pClient->Graphics()->WindowHeight()));
	const vec2 UiMousePos = pClient->Ui()->UpdatedMousePos() * vec2(pClient->Ui()->Screen()->w, pClient->Ui()->Screen()->h) / WindowSize;
	const vec2 UiToChatScale(Width / pClient->Ui()->Screen()->w, Height / pClient->Ui()->Screen()->h);
	return UiMousePos * UiToChatScale;
}

std::string CChatScroll::BuildPlainTextLine(CChat &Chat, int LineIndex)
{
	const CChat::CLine &Line = Chat.m_aLines[LineIndex];
	if(Line.m_BcCopyAsLoad)
	{
		const char *pSep = nullptr;
		for(const char *pScan = Line.m_aText; (pScan = str_find(pScan, ": ")) != nullptr; pScan += 2)
			pSep = pScan;
		if(pSep && pSep[2] != '\0')
		{
			char aBuf[MAX_CHAT_LENGTH];
			str_format(aBuf, sizeof(aBuf), "/load %s", pSep + 2);
			return aBuf;
		}
	}

	CGameClient *pClient = Chat.BcGameClient();
	char aClientId[16] = "";
	if(g_Config.m_ClShowIds && Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
		pClient->FormatClientId(Line.m_ClientId, aClientId, EClientIdFormat::INDENT_AUTO);

	char aCount[12] = "";
	if(Line.m_TimesRepeated > 0)
	{
		if(Line.m_ClientId < 0)
			str_format(aCount, sizeof(aCount), "[%d] ", Line.m_TimesRepeated + 1);
		else
			str_format(aCount, sizeof(aCount), " [%d]", Line.m_TimesRepeated + 1);
	}

	bool TextHiddenByStreamer = false;
	std::string VisibleTextStorage;
	const char *pText = Line.m_aText;
	if(g_Config.m_ClStreamerMode && Line.m_ClientId == CChat::SERVER_MSG)
	{
		if(str_startswith(Line.m_aText, "Team save in progress. You'll be able to load with '/load ") && str_endswith(Line.m_aText, "'"))
		{
			TextHiddenByStreamer = true;
			pText = "Team save in progress. You'll be able to load with '/load *** *** ***'";
		}
		else if(str_startswith(Line.m_aText, "Team save in progress. You'll be able to load with '/load") && str_endswith(Line.m_aText, "if it fails"))
		{
			TextHiddenByStreamer = true;
			pText = "Team save in progress. You'll be able to load with '/load *** *** ***' if save is successful or with '/load *** *** ***' if it fails";
		}
		else if(str_startswith(Line.m_aText, "Team successfully saved by ") && str_endswith(Line.m_aText, " to continue"))
		{
			TextHiddenByStreamer = true;
			pText = "Team successfully saved by ***. Use '/load *** *** ***' to continue";
		}
	}
	else
	{
		VisibleTextStorage = pClient->m_ChatMedia.BuildVisibleMessageText(Line.m_Media, pText, false);
		pText = VisibleTextStorage.c_str();
	}

	const CColoredParts ColoredParts(pText, Line.m_ClientId == CChat::CLIENT_MSG);
	pText = ColoredParts.Text();

	const char *pTranslatedError = nullptr;
	const char *pTranslatedText = nullptr;
	const char *pTranslatedLanguage = nullptr;
	if(Line.m_pTranslateResponse != nullptr && Line.m_pTranslateResponse->m_Text[0])
	{
		if(TextHiddenByStreamer)
			pTranslatedError = TCLocalize("Translated text hidden due to streamer mode");
		else if(Line.m_pTranslateResponse->m_Error)
			pTranslatedError = Line.m_pTranslateResponse->m_Text;
		else
		{
			pTranslatedText = Line.m_pTranslateResponse->m_Text;
			if(Line.m_pTranslateResponse->m_Language[0] != '\0')
				pTranslatedLanguage = Line.m_pTranslateResponse->m_Language;
		}
	}

	std::string Result;
	if(Line.m_ClientId >= 0 && Line.m_aName[0] != '\0' && Line.m_Friend && g_Config.m_ClMessageFriend)
		Result += "♥ ";
	Result += aClientId;
	Result += Line.m_aName;
	if(Line.m_TimesRepeated > 0)
		Result += aCount;
	if(Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
		Result += ": ";
	if(pTranslatedText)
	{
		Result += pTranslatedText;
		if(pTranslatedLanguage)
		{
			Result += " [";
			Result += pTranslatedLanguage;
			Result += "]";
		}
		Result += "\n";
		Result += pText;
	}
	else if(pTranslatedError)
	{
		Result += pText;
		Result += "\n";
		Result += pTranslatedError;
	}
	else
	{
		Result += pText;
	}
	return Result;
}

void CChatScroll::MeasureHeights(CChat &Chat, float TextBegin, float FontSize, float LineWidth, float RealMsgPaddingY, float RealMsgPaddingTee, int OffsetType, bool IsScoreBoardOpen, bool ForceRecreate)
{
	CGameClient *pClient = Chat.BcGameClient();
	ITextRender *pTextRender = pClient->TextRender();

	for(int i = 0; i < CChat::MAX_LINES; i++)
	{
		CChat::CLine &Line = Chat.m_aLines[((Chat.m_CurrentLine - i) + CChat::MAX_LINES) % CChat::MAX_LINES];
		if(!Line.m_Initialized)
			break;
		if(Line.m_aYOffset[OffsetType] >= 0.0f && !ForceRecreate)
			continue;

		if(ForceRecreate)
		{
			Line.m_aYOffset[0] = -1.0f;
			Line.m_aYOffset[1] = -1.0f;
		}

		char aClientId[16] = "";
		if(g_Config.m_ClShowIds && Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
			pClient->FormatClientId(Line.m_ClientId, aClientId, EClientIdFormat::INDENT_AUTO);

		char aCount[12];
		if(Line.m_ClientId < 0)
			str_format(aCount, sizeof(aCount), "[%d] ", Line.m_TimesRepeated + 1);
		else
			str_format(aCount, sizeof(aCount), " [%d]", Line.m_TimesRepeated + 1);

		const char *pText = Line.m_aText;
		if(g_Config.m_ClStreamerMode && Line.m_ClientId == CChat::SERVER_MSG)
		{
			if(str_startswith(Line.m_aText, "Team save in progress. You'll be able to load with '/load ") && str_endswith(Line.m_aText, "'"))
				pText = "Team save in progress. You'll be able to load with '/load *** *** ***'";
			else if(str_startswith(Line.m_aText, "Team save in progress. You'll be able to load with '/load") && str_endswith(Line.m_aText, "if it fails"))
				pText = "Team save in progress. You'll be able to load with '/load *** *** ***' if save is successful or with '/load *** *** ***' if it fails";
			else if(str_startswith(Line.m_aText, "Team successfully saved by ") && str_endswith(Line.m_aText, " to continue"))
				pText = "Team successfully saved by ***. Use '/load *** *** ***' to continue";
		}

		std::string VisibleTextStorage = pClient->m_ChatMedia.BuildVisibleMessageText(Line.m_Media, pText, false);
		pText = VisibleTextStorage.c_str();
		const CColoredParts ColoredParts(pText, Line.m_ClientId == CChat::CLIENT_MSG);
		pText = ColoredParts.Text();

		const char *pTranslatedError = nullptr;
		const char *pTranslatedText = nullptr;
		const char *pTranslatedLanguage = nullptr;
		if(Line.m_pTranslateResponse != nullptr && Line.m_pTranslateResponse->m_Text[0])
		{
			if(pText != Line.m_aText)
				pTranslatedError = TCLocalize("Translated text hidden due to streamer mode");
			else if(Line.m_pTranslateResponse->m_Error)
				pTranslatedError = Line.m_pTranslateResponse->m_Text;
			else
			{
				pTranslatedText = Line.m_pTranslateResponse->m_Text;
				if(Line.m_pTranslateResponse->m_Language[0] != '\0')
					pTranslatedLanguage = Line.m_pTranslateResponse->m_Language;
			}
		}

		CTextCursor MeasureCursor;
		MeasureCursor.SetPosition(vec2(TextBegin, 0.0f));
		MeasureCursor.m_FontSize = FontSize;
		MeasureCursor.m_Flags = 0;
		MeasureCursor.m_LineWidth = LineWidth;
		if(Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
		{
			MeasureCursor.m_X += RealMsgPaddingTee;
			if(Line.m_Friend && g_Config.m_ClMessageFriend)
				pTextRender->TextEx(&MeasureCursor, "♥ ");
		}
		pTextRender->TextEx(&MeasureCursor, aClientId);
		pTextRender->TextEx(&MeasureCursor, Line.m_aName);
		if(Line.m_TimesRepeated > 0)
			pTextRender->TextEx(&MeasureCursor, aCount);
		if(Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
			pTextRender->TextEx(&MeasureCursor, ": ");

		CTextCursor AppendCursor = MeasureCursor;
		AppendCursor.m_LongestLineWidth = 0.0f;
		if(!IsScoreBoardOpen && !g_Config.m_ClChatOld)
		{
			AppendCursor.m_StartX = MeasureCursor.m_X;
			AppendCursor.m_LineWidth -= MeasureCursor.m_LongestLineWidth;
		}
		if(pTranslatedText)
		{
			pTextRender->TextEx(&AppendCursor, pTranslatedText);
			if(pTranslatedLanguage)
			{
				pTextRender->TextEx(&AppendCursor, " [");
				pTextRender->TextEx(&AppendCursor, pTranslatedLanguage);
				pTextRender->TextEx(&AppendCursor, "]");
			}
			pTextRender->TextEx(&AppendCursor, "\n");
			AppendCursor.m_FontSize *= 0.8f;
			pTextRender->TextEx(&AppendCursor, pText);
			AppendCursor.m_FontSize /= 0.8f;
		}
		else if(pTranslatedError)
		{
			pTextRender->TextEx(&AppendCursor, pText);
			pTextRender->TextEx(&AppendCursor, "\n");
			AppendCursor.m_FontSize *= 0.8f;
			pTextRender->TextEx(&AppendCursor, pTranslatedError);
			AppendCursor.m_FontSize /= 0.8f;
		}
		else
			pTextRender->TextEx(&AppendCursor, pText);

		const float TextHeight = AppendCursor.Height();
		float TotalHeight = TextHeight + RealMsgPaddingY;
		const float MaxPreviewHeight = (IsScoreBoardOpen ? 56.0f : 70.0f) * 0.9f;
		pClient->m_ChatMedia.PrepareLineLayout(Line.m_Media, OffsetType, LineWidth, MaxPreviewHeight, FontSize, TextHeight, TotalHeight);
		Line.m_aYOffset[OffsetType] = TotalHeight;
	}
}

void CChatScroll::UpdateScrollbar(CChat &Chat, float X, float Y, float Width, float Height, float HeightLimit, int OffsetType, float FontSize, float RealMsgPaddingY, bool InteractionActive)
{
	CGameClient *pClient = Chat.BcGameClient();
	const float ViewportHeight = std::max(0.0f, Y - HeightLimit);
	const float MaxScroll = MaxScrollOffset(Chat, Y, HeightLimit, OffsetType, FontSize, RealMsgPaddingY);
	Chat.m_ChatMaxScrollOffset = MaxScroll;
	const float SmoothScrollTotal = MaxScroll + ViewportHeight;

	if(!InteractionActive)
	{
		if(!Chat.m_ScrollbarDragging)
			pClient->Ui()->DoSmoothScrollLogic(&Chat.m_BacklogScrollOffset, &Chat.m_BacklogScrollOffsetChange, ViewportHeight, SmoothScrollTotal, true);
		Chat.m_ActionLineIndex = -1;
		return;
	}

	CUIRect ScrollbarRect;
	ScrollbarRect.x = X - BestClientScrollBar::CHAT.m_Width - BestClientScrollBar::CHAT.m_OuterMargin;
	ScrollbarRect.y = HeightLimit;
	ScrollbarRect.w = BestClientScrollBar::CHAT.m_Width;
	ScrollbarRect.h = ViewportHeight;

	const vec2 ScreenPos = MousePos(Chat);
	const bool MouseDown = pClient->Input()->NativeMousePressed(1);

	BestClientScrollBar::SDragState ScrollState;
	ScrollState.m_Dragging = Chat.m_ScrollbarDragging;
	ScrollState.m_DragOffset = Chat.m_ScrollbarDragOffset;
	const float PrevOffset = Chat.m_BacklogScrollOffset;
	const float NewOffset = BestClientScrollBar::DoVerticalPixels(ScrollState, ScrollbarRect, Chat.m_BacklogScrollOffset, Chat.m_ChatMaxScrollOffset, ScreenPos, MouseDown, BestClientScrollBar::CHAT);
	if(NewOffset != PrevOffset || ScrollState.m_Dragging)
		Chat.m_BacklogScrollOffsetChange = 0.0f;
	if(NewOffset != PrevOffset)
		Chat.m_ActionLineIndex = -1;
	Chat.m_BacklogScrollOffset = NewOffset;
	Chat.m_ScrollbarDragging = ScrollState.m_Dragging;
	Chat.m_ScrollbarDragOffset = ScrollState.m_DragOffset;

	if(!Chat.m_ScrollbarDragging)
		pClient->Ui()->DoSmoothScrollLogic(&Chat.m_BacklogScrollOffset, &Chat.m_BacklogScrollOffsetChange, ViewportHeight, SmoothScrollTotal, true);
}

CChatScroll::SActionFrame CChatScroll::BeginActions(CChat &Chat)
{
	SActionFrame Frame;
	if(!g_Config.m_BcChatMessageActions)
	{
		Chat.m_ActionLineIndex = -1;
		return Frame;
	}

	CGameClient *pClient = Chat.BcGameClient();
	if(pClient->Ui()->IsPopupOpen())
	{
		Chat.m_ActionLineIndex = -1;
		return Frame;
	}
	Frame.m_MousePos = MousePos(Chat);
	Frame.m_LeftClicked = !Chat.m_ScrollbarDragging && pClient->Input()->KeyPress(KEY_MOUSE_1);
	Frame.m_RightClicked = !Chat.m_ScrollbarDragging && pClient->Input()->KeyPress(KEY_MOUSE_2);
	if(Chat.m_Mode != CChat::MODE_NONE && Chat.m_ActionLineIndex >= 0)
	{
		const CChat::CLine &ActionLine = Chat.m_aLines[Chat.m_ActionLineIndex];
		Frame.m_Layout = BestClientChatMessageActions::BuildLayout(Chat.m_ActionMenuPos, Chat.FontSize(), ActionLine.m_ClientId >= 0);
		Frame.m_LayoutValid = true;
	}
	Frame.m_MouseOverMenu = Frame.m_LayoutValid && BestClientChatMessageActions::PanelContains(Frame.m_Layout, Frame.m_MousePos);
	return Frame;
}

void CChatScroll::NoteLineHover(SActionFrame &Frame, CChat &Chat, int ArrayIndex, float X, float LineW, float LineRenderY, float LineH, bool InteractionActive)
{
	if(!g_Config.m_BcChatMessageActions)
		return;
	const bool InsideLine = !Frame.m_MouseOverMenu && InteractionActive && Chat.m_Mode != CChat::MODE_NONE && !Chat.m_ScrollbarDragging &&
		Frame.m_MousePos.x >= X && Frame.m_MousePos.x <= X + LineW && Frame.m_MousePos.y >= LineRenderY && Frame.m_MousePos.y <= LineRenderY + LineH;
	if(InsideLine)
		Frame.m_HotLineIndex = ArrayIndex;
}

bool CChatScroll::ShouldHighlight(const SActionFrame &Frame, const CChat &Chat, int ArrayIndex)
{
	return g_Config.m_BcChatMessageActions && Chat.m_Mode != CChat::MODE_NONE && (ArrayIndex == Frame.m_HotLineIndex || ArrayIndex == Chat.m_ActionLineIndex);
}

void CChatScroll::DrawHighlight(CChat &Chat, float X, float LineRenderY, float LineW, float LineH, float Blend)
{
	BestClientChatMessageActions::DrawMessageHighlight(Chat.BcGameClient()->Graphics(), X, LineRenderY, LineW, LineH, Chat.MessageRounding(), Blend);
}

void CChatScroll::EndActions(CChat &Chat, SActionFrame &Frame, float ModuleAlpha)
{
	if(!g_Config.m_BcChatMessageActions)
	{
		Chat.m_ActionLineIndex = -1;
		return;
	}

	CGameClient *pClient = Chat.BcGameClient();
	if(Frame.m_LayoutValid)
		BestClientChatMessageActions::Render(pClient->TextRender(), Frame.m_Layout, Frame.m_MousePos, ModuleAlpha);

	if(Chat.m_Mode == CChat::MODE_NONE || Chat.m_ScrollbarDragging)
		return;

	if(Frame.m_RightClicked)
	{
		if(Frame.m_HotLineIndex >= 0)
		{
			if(Chat.m_ActionLineIndex == Frame.m_HotLineIndex)
				Chat.m_ActionLineIndex = -1;
			else
			{
				Chat.m_ActionLineIndex = Frame.m_HotLineIndex;
				Chat.m_ActionMenuPos = Frame.m_MousePos;
			}
		}
		else if(!(Frame.m_LayoutValid && BestClientChatMessageActions::PanelContains(Frame.m_Layout, Frame.m_MousePos)))
		{
			Chat.m_ActionLineIndex = -1;
		}
	}
	else if(Frame.m_LeftClicked && Frame.m_LayoutValid)
	{
		const auto Action = BestClientChatMessageActions::HitTest(Frame.m_Layout, Frame.m_MousePos);
		if(Action == BestClientChatMessageActions::EAction::Copy && Chat.m_ActionLineIndex >= 0)
		{
			const std::string PlainText = BuildPlainTextLine(Chat, Chat.m_ActionLineIndex);
			pClient->Input()->SetClipboardText(PlainText.c_str());
			Chat.m_ActionLineIndex = -1;
		}
		else if(Action == BestClientChatMessageActions::EAction::Reply && Chat.m_ActionLineIndex >= 0)
		{
			const CChat::CLine &Line = Chat.m_aLines[Chat.m_ActionLineIndex];
			if(Line.m_ClientId >= 0 && Line.m_ClientId < MAX_CLIENTS)
			{
				char aBuf[MAX_CHAT_LENGTH];
				str_format(aBuf, sizeof(aBuf), "%s: ", pClient->m_aClients[Line.m_ClientId].m_aName);
				Chat.m_Input.Set(aBuf);
				Chat.m_Input.SetCursorOffset(str_length(aBuf));
			}
			Chat.m_ActionLineIndex = -1;
		}
		else if(!BestClientChatMessageActions::PanelContains(Frame.m_Layout, Frame.m_MousePos))
		{
			Chat.m_ActionLineIndex = -1;
		}
	}
}
