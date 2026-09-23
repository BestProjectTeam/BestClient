/* Copyright © 2026 BestProject Team */
#include "browser_utils.h"

#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <string>

void CBrowserUtils::OnUpdate()
{
	const bool BrowserPageActive = GameClient()->m_Menus.IsServerBrowserPageActive();

	if(BrowserPageActive && g_Config.m_BcAutoServerListRefresh)
	{
		const bool BrowserBusy = ServerBrowser()->IsRefreshing() || ServerBrowser()->IsGettingServerlist();
		if(!BrowserBusy)
		{
			const int64_t Now = time_get();
			const int64_t RefreshInterval = (int64_t)g_Config.m_BcAutoServerListRefreshSeconds * time_freq();
			if(m_LastServerBrowserRefreshTick == 0)
				m_LastServerBrowserRefreshTick = Now;
			else if(RefreshInterval > 0 && Now - m_LastServerBrowserRefreshTick >= RefreshInterval)
			{
				ServerBrowser()->Refresh(ServerBrowser()->GetCurrentType(), true);
				m_LastServerBrowserRefreshTick = Now;
			}
		}
	}
	else if(!BrowserPageActive)
	{
		m_LastServerBrowserRefreshTick = 0;
	}
}

const char *CBrowserUtils::GetDisplayName(const CServerInfo *pInfo, char *pBuffer, size_t BufferSize)
{
	if(!g_Config.m_BcUseShortKogServerName)
		return pInfo->m_aName;

	const bool IsKog = str_find_nocase(pInfo->m_aGameType, "gores") && str_find_nocase(pInfo->m_aName, "kog");
	const bool IsEGores = str_find_nocase(pInfo->m_aGameType, "e-gores") || str_find_nocase(pInfo->m_aGameType, "e_gores");

	if(!IsKog && !IsEGores)
		return pInfo->m_aName;

	const auto IsAsciiWordChar = [](char c) {
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
	};
	const auto IsKogSeparator = [](char c) {
		return c == ' ' || c == '|' || c == '*' || c == '-' || c == ':' || c == '[' || c == ']';
	};

	const char *pShortName = pInfo->m_aName;

	if(IsKog)
	{
		const char *pScan = pInfo->m_aName;
		while(const char *pMatch = str_find_nocase(pScan, "kog"))
		{
			const char Prev = pMatch > pInfo->m_aName ? pMatch[-1] : '\0';
			const char Next = pMatch[3];
			if(!IsAsciiWordChar(Prev) && !IsAsciiWordChar(Next))
			{
				pShortName = pMatch + 3;
				while(*pShortName != '\0' && IsKogSeparator(*pShortName))
					++pShortName;
				break;
			}
			pScan = pMatch + 1;
		}
	}

	pShortName = str_skip_whitespaces_const(pShortName);
	str_copy(pBuffer, pShortName, BufferSize);

	if(IsEGores)
	{
		if(const char *pEgo = str_find_nocase(pBuffer, "ego"))
		{
			const char *pAfterEgo = pEgo + 3;
			while(*pAfterEgo == ' ' || *pAfterEgo == '|')
				++pAfterEgo;
			if(*pAfterEgo != '\0')
				str_copy(pBuffer, pAfterEgo, BufferSize);
		}
		if(char *pSuffix = const_cast<char *>(str_find_nocase(pBuffer, "[eternal")))
		{
			while(pSuffix > pBuffer && pSuffix[-1] == ' ')
				--pSuffix;
			*pSuffix = '\0';
		}
	}

	if(const char *pSuffix = str_endswith_nocase(pBuffer, "[kog.tw]"))
	{
		char *pSuffixStart = const_cast<char *>(pSuffix);
		while(pSuffixStart > pBuffer && pSuffixStart[-1] == ' ')
			--pSuffixStart;
		*pSuffixStart = '\0';
	}

	char *pHashToken = const_cast<char *>(str_find(pBuffer, " #"));
	if(pHashToken != nullptr)
	{
		char *pDigits = pHashToken + 2;
		if('0' <= *pDigits && *pDigits <= '9')
		{
			while('0' <= *pDigits && *pDigits <= '9')
				++pDigits;

			if(str_startswith(pDigits, " - "))
			{
				const char *pMapName = str_skip_whitespaces_const(pDigits + 3);
				char *pRegionEnd = pHashToken;
				while(pRegionEnd > pBuffer && pRegionEnd[-1] == ' ')
					--pRegionEnd;
				*pRegionEnd = '\0';

				if(pBuffer[0] != '\0' && pMapName[0] != '\0')
				{
					const std::string Region = pBuffer;
					const std::string MapName = pMapName;
					str_format(pBuffer, BufferSize, "%s - %s", Region.c_str(), MapName.c_str());
				}
				else if(pMapName[0] != '\0')
				{
					str_copy(pBuffer, pMapName, BufferSize);
				}
			}
		}
	}

	return pBuffer[0] != '\0' ? pBuffer : pInfo->m_aName;
}
