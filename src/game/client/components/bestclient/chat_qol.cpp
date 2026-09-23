/* Copyright © 2026 BestProject Team */
#include "chat_qol.h"

#include <base/color.h>
#include <base/io.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>
#include <generated/protocol.h>

#include <game/client/components/chat.h>
#include <game/client/components/tclient/bindchat.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>

static constexpr int SAVES_CHAT_LIST_LIMIT = 10;
static const ColorRGBA SAVES_CHAT_COLOR(1.0f, 0.9f, 0.4f, 1.0f);

static int WrongLayoutToLatinCodepoint(int Codepoint)
{
	switch(Codepoint)
	{
	case '.': return '/';
	case 0x0451: return '`';
	case 0x0401: return '~';
	case 0x0439: return 'q';
	case 0x0419: return 'Q';
	case 0x0446: return 'w';
	case 0x0426: return 'W';
	case 0x0443: return 'e';
	case 0x0423: return 'E';
	case 0x043a: return 'r';
	case 0x041a: return 'R';
	case 0x0435: return 't';
	case 0x0415: return 'T';
	case 0x043d: return 'y';
	case 0x041d: return 'Y';
	case 0x0433: return 'u';
	case 0x0413: return 'U';
	case 0x0448: return 'i';
	case 0x0428: return 'I';
	case 0x0449: return 'o';
	case 0x0429: return 'O';
	case 0x0437: return 'p';
	case 0x0417: return 'P';
	case 0x0445: return '[';
	case 0x0425: return '{';
	case 0x044a: return ']';
	case 0x042a: return '}';
	case 0x0444: return 'a';
	case 0x0424: return 'A';
	case 0x044b: return 's';
	case 0x042b: return 'S';
	case 0x0432: return 'd';
	case 0x0412: return 'D';
	case 0x0430: return 'f';
	case 0x0410: return 'F';
	case 0x043f: return 'g';
	case 0x041f: return 'G';
	case 0x0440: return 'h';
	case 0x0420: return 'H';
	case 0x043e: return 'j';
	case 0x041e: return 'J';
	case 0x043b: return 'k';
	case 0x041b: return 'K';
	case 0x0434: return 'l';
	case 0x0414: return 'L';
	case 0x0436: return ';';
	case 0x0416: return ':';
	case 0x044d: return '\'';
	case 0x042d: return '"';
	case 0x044f: return 'z';
	case 0x042f: return 'Z';
	case 0x0447: return 'x';
	case 0x0427: return 'X';
	case 0x0441: return 'c';
	case 0x0421: return 'C';
	case 0x043c: return 'v';
	case 0x041c: return 'V';
	case 0x0438: return 'b';
	case 0x0418: return 'B';
	case 0x0442: return 'n';
	case 0x0422: return 'N';
	case 0x044c: return 'm';
	case 0x042c: return 'M';
	case 0x0431: return ',';
	case 0x0411: return '<';
	case 0x044e: return '.';
	case 0x042e: return '>';
	default: return Codepoint;
	}
}

static bool IsLikelySlashCommandName(const char *pName)
{
	if(!pName || !pName[0] || !std::isalpha((unsigned char)pName[0]))
		return false;

	for(const char *pChar = pName; *pChar != '\0'; ++pChar)
	{
		if(!std::isalnum((unsigned char)*pChar) && *pChar != '_' && *pChar != '-')
			return false;
	}
	return true;
}

bool CChatQoL::TryConvertWrongLayoutSlashCommand(const CChat &Chat, const char *pLine, char *pOut, int OutSize)
{
	if(!g_Config.m_BcChatAltCommandLayout || !pLine || !pOut || OutSize <= 0)
		return false;

	const char *pTokenStart = str_utf8_skip_whitespaces(pLine);
	if(*pTokenStart == '\0')
		return false;

	const char *pTokenEnd = pTokenStart;
	while(*pTokenEnd)
	{
		const char *pNext = pTokenEnd;
		const int Codepoint = str_utf8_decode(&pNext);
		if(Codepoint <= 0 || str_utf8_isspace(Codepoint))
			break;
		pTokenEnd = pNext;
	}

	char aConvertedToken[MAX_CHAT_LENGTH];
	int ConvertedLen = 0;
	bool Changed = false;
	for(const char *pScan = pTokenStart; pScan < pTokenEnd;)
	{
		const char *pNext = pScan;
		const int Codepoint = str_utf8_decode(&pNext);
		const int ConvertedCodepoint = WrongLayoutToLatinCodepoint(Codepoint);
		Changed |= ConvertedCodepoint != Codepoint;

		char aEncoded[8];
		const int EncodedLen = str_utf8_encode(aEncoded, ConvertedCodepoint);
		if(ConvertedLen + EncodedLen >= (int)sizeof(aConvertedToken))
			return false;
		std::copy_n(aEncoded, EncodedLen, aConvertedToken + ConvertedLen);
		ConvertedLen += EncodedLen;
		pScan = pNext;
	}
	aConvertedToken[ConvertedLen] = '\0';

	if(!Changed || aConvertedToken[0] != '/')
		return false;

	const char *pCommandName = aConvertedToken + 1;
	if(!IsLikelySlashCommandName(pCommandName))
		return false;

	if(!Chat.m_vServerCommands.empty())
	{
		bool Found = false;
		for(const auto &Command : Chat.m_vServerCommands)
		{
			if(str_comp_nocase(Command.m_aName, pCommandName) == 0)
			{
				Found = true;
				break;
			}
		}
		if(!Found)
			return false;
	}

	str_truncate(pOut, OutSize, pLine, pTokenStart - pLine);
	str_append(pOut, aConvertedToken, OutSize);
	str_append(pOut, pTokenEnd, OutSize);
	return str_comp(pOut, pLine) != 0;
}

void CChatQoL::ApplySilentTyping(CNetObj_PlayerInput *pInputs, int Count, bool Enabled)
{
	if(!Enabled || !pInputs || Count <= 0)
		return;
	for(int i = 0; i < Count; ++i)
		pInputs[i].m_PlayerFlags &= ~PLAYERFLAG_CHATTING;
}

struct SSaveRow
{
	char m_aPlayers[256];
	char m_aMap[128];
	char m_aCode[64];
};

static const char *ParseCsvField(const char *pCursor, char *pOut, int OutSize)
{
	if(!pCursor || !pOut || OutSize <= 0)
		return nullptr;

	pOut[0] = '\0';
	if(*pCursor == '\0' || *pCursor == '\n' || *pCursor == '\r')
		return pCursor;

	int OutLen = 0;
	if(*pCursor == '"')
	{
		++pCursor;
		while(*pCursor)
		{
			if(*pCursor == '"')
			{
				if(pCursor[1] == '"')
				{
					if(OutLen + 1 < OutSize)
						pOut[OutLen++] = '"';
					pCursor += 2;
					continue;
				}
				++pCursor;
				break;
			}
			if(OutLen + 1 < OutSize)
				pOut[OutLen++] = *pCursor;
			++pCursor;
		}
		pOut[OutLen] = '\0';
		if(*pCursor == ',')
			++pCursor;
		return pCursor;
	}

	while(*pCursor && *pCursor != ',' && *pCursor != '\n' && *pCursor != '\r')
	{
		if(OutLen + 1 < OutSize)
			pOut[OutLen++] = *pCursor;
		++pCursor;
	}
	pOut[OutLen] = '\0';
	if(*pCursor == ',')
		++pCursor;
	return pCursor;
}

static bool ParseSaveRow(const char *pLine, SSaveRow &Out)
{
	char aTime[32];
	char *apFields[] = {aTime, Out.m_aPlayers, Out.m_aMap, Out.m_aCode};
	const int aSizes[] = {(int)sizeof(aTime), (int)sizeof(Out.m_aPlayers), (int)sizeof(Out.m_aMap), (int)sizeof(Out.m_aCode)};

	const char *pCursor = pLine;
	for(int i = 0; i < 4; ++i)
	{
		pCursor = ParseCsvField(pCursor, apFields[i], aSizes[i]);
		if(!pCursor)
			return false;
	}

	if(aTime[0] == '\0' || Out.m_aMap[0] == '\0')
		return false;
	if(str_comp_nocase(aTime, "Time") == 0)
		return false;
	return true;
}

static bool CollectMapSaves(CGameClient *pGameClient, const char *pMapName, std::vector<SSaveRow> &vOut)
{
	vOut.clear();
	if(!pGameClient || !pMapName || pMapName[0] == '\0')
		return false;
	if(!pGameClient->Storage()->FileExists(SAVES_FILE, IStorage::TYPE_SAVE))
		return false;

	IOHANDLE File = pGameClient->Storage()->OpenFile(SAVES_FILE, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(!File)
		return false;

	char *pData = io_read_all_str(File);
	io_close(File);
	if(!pData)
		return false;

	const char *pLine = pData;
	while(pLine && *pLine)
	{
		const char *pNext = pLine;
		while(*pNext && *pNext != '\n' && *pNext != '\r')
			++pNext;

		char aLine[512];
		str_truncate(aLine, sizeof(aLine), pLine, pNext - pLine);

		SSaveRow Row;
		if(ParseSaveRow(aLine, Row) && str_comp(Row.m_aMap, pMapName) == 0)
			vOut.push_back(Row);

		pLine = pNext;
		if(*pLine == '\r')
			++pLine;
		if(*pLine == '\n')
			++pLine;
	}

	free(pData);
	return true;
}

static const char *DisplaySaveCode(const char *pCode)
{
	if(g_Config.m_ClStreamerMode)
		return "***";
	return pCode && pCode[0] ? pCode : "-";
}

static void ShowMapSavesList(CChat &Chat, const std::vector<SSaveRow> &vSaves, bool NotifyOnly)
{
	const int Total = (int)vSaves.size();
	if(Total <= 0)
	{
		if(!NotifyOnly)
			Chat.AddColoredLine(Localize("No saves for this map"), SAVES_CHAT_COLOR, true);
		return;
	}

	char aBuf[256];
	if(NotifyOnly)
	{
		str_format(aBuf, sizeof(aBuf), Localize("Found %d saves on this map. Type !saves to see them"), Total);
		Chat.AddColoredLine(aBuf, SAVES_CHAT_COLOR, true);
		return;
	}

	str_format(aBuf, sizeof(aBuf), Localize("Saves on this map: %d"), Total);
	Chat.AddColoredLine(aBuf, SAVES_CHAT_COLOR, true);

	const int ShowCount = std::min(Total, SAVES_CHAT_LIST_LIMIT);
	for(int i = 0; i < ShowCount; ++i)
	{
		str_format(aBuf, sizeof(aBuf), "%s: %s", vSaves[i].m_aPlayers, DisplaySaveCode(vSaves[i].m_aCode));
		Chat.AddColoredLine(aBuf, SAVES_CHAT_COLOR, true, true);
	}

	if(Total > ShowCount)
	{
		str_format(aBuf, sizeof(aBuf), Localize("and %d more"), Total - ShowCount);
		Chat.AddColoredLine(aBuf, SAVES_CHAT_COLOR, true);
	}
}

void CChatQoL::NotifySavesOnMapLoad(CGameClient *pGameClient)
{
	if(!pGameClient || !g_Config.m_BcNotifySavesOnMap)
		return;
	if(pGameClient->Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return;

	const char *pMapName = pGameClient->Map()->BaseName();
	if(!pMapName || pMapName[0] == '\0')
		return;

	std::vector<SSaveRow> vSaves;
	CollectMapSaves(pGameClient, pMapName, vSaves);
	if(vSaves.empty())
		return;

	ShowMapSavesList(pGameClient->m_Chat, vSaves, true);
}

void CChatQoL::ListSavesForCurrentMap(CChat &Chat)
{
	CGameClient *pGameClient = Chat.BcGameClient();
	if(!pGameClient)
		return;

	const char *pMapName = pGameClient->Map()->BaseName();
	if(!pMapName || pMapName[0] == '\0')
	{
		Chat.AddColoredLine(Localize("No saves for this map"), SAVES_CHAT_COLOR, true);
		return;
	}

	std::vector<SSaveRow> vSaves;
	CollectMapSaves(pGameClient, pMapName, vSaves);
	ShowMapSavesList(Chat, vSaves, false);
}

bool CChatQoL::TryHandleBangCommand(CChat &Chat, const char *pText)
{
	if(!pText)
		return false;

	const char *pCommand = str_utf8_skip_whitespaces(pText);
	if(str_comp_nocase_num(pCommand, "!saves", 6) != 0)
		return false;
	if(pCommand[6] != '\0' && !str_utf8_isspace(pCommand[6]))
		return false;

	ListSavesForCurrentMap(Chat);
	const int Length = str_length(pText);
	CChat::CHistoryEntry *pEntry = Chat.m_History.Allocate(sizeof(CChat::CHistoryEntry) + Length);
	pEntry->m_Team = 0;
	str_copy(pEntry->m_aText, pText, Length + 1);
	return true;
}

bool CChatQoL::TryBangCommandAutocomplete(CChat &Chat)
{
	static const char s_aCommand[] = "!saves";

	if(Chat.m_aCompletionBuffer[0] != '!' || !str_startswith_nocase(s_aCommand, Chat.m_aCompletionBuffer))
		return false;

	char aBuf[MAX_CHAT_LENGTH];
	str_truncate(aBuf, sizeof(aBuf), Chat.m_Input.GetString(), Chat.m_PlaceholderOffset);
	str_append(aBuf, s_aCommand);
	str_append(aBuf, " ");
	str_append(aBuf, Chat.m_Input.GetString() + Chat.m_PlaceholderOffset + Chat.m_PlaceholderLength);

	Chat.m_PlaceholderLength = str_length(s_aCommand) + 1;
	Chat.m_Input.Set(aBuf);
	Chat.m_Input.SetCursorOffset(Chat.m_PlaceholderOffset + Chat.m_PlaceholderLength);
	Chat.m_CompletionUsed = true;
	return true;
}

bool CChatQoL::TryRenderBangCommandHint(CGameClient *pGameClient, const char *pInput, CTextCursor *pCursor, ITextRender *pTextRender)
{
	if(!pGameClient || !pInput || !pCursor || !pTextRender)
		return false;
	if(pInput[0] != '!' || pInput[1] == '\0')
		return false;

	const char *pMatch = nullptr;
	for(const auto &Bind : pGameClient->m_BindChat.m_vBinds)
	{
		if(Bind.m_aName[0] != '!')
			continue;
		if(str_startswith_nocase(Bind.m_aName, pInput))
		{
			pMatch = Bind.m_aName;
			break;
		}
	}

	if(!pMatch && str_startswith_nocase("!saves", pInput))
		pMatch = "!saves";

	if(!pMatch)
		return false;

	const int TypedLen = str_length(pInput);
	if(str_length(pMatch) <= TypedLen)
		return false;

	pTextRender->TextColor(1.0f, 1.0f, 1.0f, 0.5f);
	pTextRender->TextEx(pCursor, pMatch + TypedLen);
	pTextRender->TextColor(pTextRender->DefaultTextColor());
	return true;
}
