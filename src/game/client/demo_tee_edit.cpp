#include "demo_tee_edit.h"

#include <base/str.h>

#include <engine/shared/snapshot.h>

#include <generated/protocol.h>

#include <game/gamecore.h>

void ApplyDemoTeeOverrides(void *pData, int Size, bool Sixup, const std::array<SDemoTeeOverride, MAX_CLIENTS> &Overrides)
{
	if(Sixup || Size <= 0)
		return;
	auto *pSnapshot = static_cast<CSnapshot *>(pData);
	if(!pSnapshot->IsValid(Size))
		return;
	for(int i = 0; i < pSnapshot->NumItems(); ++i)
	{
		const CSnapshotItem *pItem = pSnapshot->GetItem(i);
		if(pSnapshot->GetItemType(i) != NETOBJTYPE_CLIENTINFO ||
			pSnapshot->GetItemSize(i) != sizeof(CNetObj_ClientInfo) ||
			pItem->Id() < 0 || pItem->Id() >= MAX_CLIENTS)
			continue;
		const SDemoTeeOverride &Override = Overrides[pItem->Id()];
		if(!Override.m_Active)
			continue;
		auto *pInfo = reinterpret_cast<CNetObj_ClientInfo *>(const_cast<int *>(pItem->Data()));
		char aOriginalName[MAX_NAME_LENGTH];
		if(!IntsToStr(pInfo->m_aName, std::size(pInfo->m_aName), aOriginalName, sizeof(aOriginalName)) ||
			str_comp(aOriginalName, Override.m_aOriginalName) != 0)
			continue;
		char aExportName[MAX_NAME_LENGTH];
		str_copy(aExportName, Override.m_aName);
		if(str_comp(Override.m_aName, Override.m_aOriginalName) != 0)
			MarkChangedDemoName(aExportName, sizeof(aExportName));
		StrToInts(pInfo->m_aName, std::size(pInfo->m_aName), aExportName);
		StrToInts(pInfo->m_aClan, std::size(pInfo->m_aClan), Override.m_aClan);
		if(Override.m_OverrideAppearance)
		{
			StrToInts(pInfo->m_aSkin, std::size(pInfo->m_aSkin), Override.m_aSkinName);
			pInfo->m_UseCustomColor = Override.m_UseCustomColors;
			pInfo->m_ColorBody = Override.m_BodyColor;
			pInfo->m_ColorFeet = Override.m_FeetColor;
		}
	}
}

void MarkChangedDemoName(char *pName, size_t NameSize)
{
	if(pName == nullptr || NameSize < 4)
		return;
	if(pName[0] == '*' && str_endswith(pName, "*") && str_length(pName) >= 2)
		return;
	const int MaxChars = MAX_NAME_LENGTH - 1;
	int CharCount = 0;
	for(const char *pCursor = pName; *pCursor != '\0';)
	{
		if(str_utf8_decode(&pCursor) == 0)
			break;
		++CharCount;
	}
	if(CharCount > MaxChars - 2)
	{
		char aTrimmed[MAX_NAME_LENGTH];
		str_utf8_truncate(aTrimmed, sizeof(aTrimmed), pName, (int)(MaxChars - 2));
		str_copy(pName, aTrimmed, NameSize);
	}
	char aMarked[MAX_NAME_LENGTH];
	str_format(aMarked, sizeof(aMarked), "*%s*", pName);
	str_copy(pName, aMarked, NameSize);
}
