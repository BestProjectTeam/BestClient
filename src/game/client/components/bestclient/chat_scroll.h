/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_CHAT_SCROLL_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_CHAT_SCROLL_H

#include <base/vmath.h>

#include <game/client/components/bestclient/chat_message_actions.h>

#include <engine/input.h>

#include <string>

class CChat;

class CChatScroll
{
public:
	struct SActionFrame
	{
		vec2 m_MousePos = vec2(0.0f, 0.0f);
		bool m_LeftClicked = false;
		bool m_RightClicked = false;
		bool m_MouseOverMenu = false;
		bool m_LayoutValid = false;
		BestClientChatMessageActions::SLayout m_Layout{};
		int m_HotLineIndex = -1;
	};

	static void Reset(CChat &Chat);
	static void OnDisableMode(CChat &Chat);
	static void OnLineAdded(CChat &Chat);
	static bool OnInput(CChat &Chat, const IInput::CEvent &Event);
	static void MeasureHeights(CChat &Chat, float TextBegin, float FontSize, float LineWidth, float RealMsgPaddingY, float RealMsgPaddingTee, int OffsetType, bool IsScoreBoardOpen, bool ForceRecreate);
	static void UpdateScrollbar(CChat &Chat, float X, float Y, float Width, float Height, float HeightLimit, int OffsetType, float FontSize, float RealMsgPaddingY, bool InteractionActive);
	static SActionFrame BeginActions(CChat &Chat);
	static void NoteLineHover(SActionFrame &Frame, CChat &Chat, int ArrayIndex, float X, float LineW, float LineRenderY, float LineH, bool InteractionActive);
	static bool ShouldHighlight(const SActionFrame &Frame, const CChat &Chat, int ArrayIndex);
	static void DrawHighlight(CChat &Chat, float X, float LineRenderY, float LineW, float LineH, float Blend);
	static void EndActions(CChat &Chat, SActionFrame &Frame, float ModuleAlpha);

private:
	static float LineHeight(const CChat &Chat, int LineIndex, int OffsetType, float FontSize, float RealMsgPaddingY);
	static float MaxScrollOffset(const CChat &Chat, float Y, float HeightLimit, int OffsetType, float FontSize, float RealMsgPaddingY);
	static vec2 MousePos(CChat &Chat);
	static std::string BuildPlainTextLine(CChat &Chat, int LineIndex);
};

#endif
