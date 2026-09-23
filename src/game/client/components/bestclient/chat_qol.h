/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_CHAT_QOL_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_CHAT_QOL_H

class CChat;
class CGameClient;
class CNetObj_PlayerInput;
class CTextCursor;
class ITextRender;

class CChatQoL
{
public:
	static bool TryConvertWrongLayoutSlashCommand(const CChat &Chat, const char *pLine, char *pOut, int OutSize);
	static void ApplySilentTyping(CNetObj_PlayerInput *pInputs, int Count, bool Enabled);

	static void NotifySavesOnMapLoad(CGameClient *pGameClient);
	static bool TryHandleBangCommand(CChat &Chat, const char *pText);
	static bool TryBangCommandAutocomplete(CChat &Chat);
	static void ListSavesForCurrentMap(CChat &Chat);
	static bool TryRenderBangCommandHint(CGameClient *pGameClient, const char *pInput, CTextCursor *pCursor, ITextRender *pTextRender);
};

#endif
