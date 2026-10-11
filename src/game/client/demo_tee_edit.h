#ifndef GAME_CLIENT_DEMO_TEE_EDIT_H
#define GAME_CLIENT_DEMO_TEE_EDIT_H

#include <engine/shared/protocol.h>

#include <array>

struct SDemoTeeOverride
{
	bool m_Active = false;
	bool m_OverrideAppearance = false;
	bool m_UseCustomColors = false;
	bool m_OriginalUseCustomColors = false;
	char m_aOriginalName[MAX_NAME_LENGTH] = "";
	char m_aOriginalClan[MAX_CLAN_LENGTH] = "";
	char m_aOriginalSkinName[MAX_SKIN_LENGTH] = "";
	char m_aName[MAX_NAME_LENGTH] = "";
	char m_aClan[MAX_CLAN_LENGTH] = "";
	char m_aSkinName[MAX_SKIN_LENGTH] = "";
	unsigned int m_BodyColor = 0;
	unsigned int m_FeetColor = 0;
	unsigned int m_OriginalBodyColor = 0;
	unsigned int m_OriginalFeetColor = 0;
};

void ApplyDemoTeeOverrides(void *pData, int Size, bool Sixup, const std::array<SDemoTeeOverride, MAX_CLIENTS> &Overrides);

void MarkChangedDemoName(char *pName, size_t NameSize);

#endif
