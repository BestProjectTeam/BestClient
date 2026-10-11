#include "demo_tee_edit.h"

#include <base/str.h>

#include <engine/shared/snapshot.h>

#include <generated/protocol.h>

#include <game/gamecore.h>

void ApplyDemoTeeOverrides(void *pData, int Size, bool Sixup, const std::array<SDemoTeeOverride, MAX_CLIENTS> &Overrides)
{
	// Legacy full skins are represented by ClientInfo in 0.6 demos.
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
		StrToInts(pInfo->m_aName, std::size(pInfo->m_aName), Override.m_aName);
		if(Override.m_OverrideAppearance)
		{
			StrToInts(pInfo->m_aSkin, std::size(pInfo->m_aSkin), Override.m_aSkinName);
			pInfo->m_UseCustomColor = Override.m_UseCustomColors;
			pInfo->m_ColorBody = Override.m_BodyColor;
			pInfo->m_ColorFeet = Override.m_FeetColor;
		}
	}
}
