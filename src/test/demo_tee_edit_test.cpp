#include <base/str.h>

#include <engine/shared/snapshot.h>

#include <game/client/demo_tee_edit.h>
#include <game/gamecore.h>

#include <generated/protocol.h>

#include <gtest/gtest.h>

#include <cstring>
#include <iterator>

namespace
{
struct STestSnapshot
{
	CSnapshotBuffer m_Buffer;
	int m_Size = 0;
};

STestSnapshot MakeSnapshot(const CNetObj_ClientInfo *pInfo, int ClientId = 3, int ItemSize = sizeof(CNetObj_ClientInfo))
{
	CSnapshotBuilder Builder;
	Builder.Init();
	EXPECT_TRUE(Builder.NewItem(NETOBJTYPE_CLIENTINFO, ClientId, pInfo, ItemSize));
	STestSnapshot Result;
	Result.m_Size = Builder.Finish(&Result.m_Buffer);
	return Result;
}

CNetObj_ClientInfo MakeInfo(const char *pName, const char *pSkin = "original")
{
	CNetObj_ClientInfo Info = {};
	StrToInts(Info.m_aName, std::size(Info.m_aName), pName);
	StrToInts(Info.m_aClan, std::size(Info.m_aClan), "clan");
	Info.m_Country = 643;
	StrToInts(Info.m_aSkin, std::size(Info.m_aSkin), pSkin);
	Info.m_UseCustomColor = 1;
	Info.m_ColorBody = 0x123456;
	Info.m_ColorFeet = 0x654321;
	return Info;
}

SDemoTeeOverride MakeOverride(const char *pOriginalName = "original-name")
{
	SDemoTeeOverride Override;
	Override.m_Active = true;
	Override.m_OverrideAppearance = true;
	Override.m_UseCustomColors = true;
	str_copy(Override.m_aOriginalName, pOriginalName);
	str_copy(Override.m_aOriginalClan, "clan");
	str_copy(Override.m_aName, "edited-name");
	str_copy(Override.m_aClan, "edited-clan");
	str_copy(Override.m_aSkinName, "edited-skin");
	Override.m_BodyColor = 0xabcdef;
	Override.m_FeetColor = 0xfedcba;
	return Override;
}

CNetObj_ClientInfo *FindInfo(CSnapshotBuffer &Buffer)
{
	const CSnapshot *pSnapshot = Buffer.AsSnapshot();
	const CSnapshotItem *pItem = pSnapshot->GetItem(0);
	return reinterpret_cast<CNetObj_ClientInfo *>(const_cast<int *>(pItem->Data()));
}
} // namespace

TEST(DemoTeeEdit, AppliesNameSkinCustomFlagAndBodyFeetColorsToMatchingClient)
{
	const CNetObj_ClientInfo Original = MakeInfo("original-name");
	STestSnapshot Snapshot = MakeSnapshot(&Original);
	std::array<SDemoTeeOverride, MAX_CLIENTS> Overrides{};
	Overrides[3] = MakeOverride();

	ApplyDemoTeeOverrides(Snapshot.m_Buffer.m_aData, Snapshot.m_Size, false, Overrides);
	const CNetObj_ClientInfo *pEdited = FindInfo(Snapshot.m_Buffer);
	char aName[MAX_NAME_LENGTH];
	char aClan[MAX_CLAN_LENGTH];
	char aSkin[MAX_SKIN_LENGTH];
	EXPECT_TRUE(IntsToStr(pEdited->m_aName, std::size(pEdited->m_aName), aName, sizeof(aName)));
	EXPECT_TRUE(IntsToStr(pEdited->m_aClan, std::size(pEdited->m_aClan), aClan, sizeof(aClan)));
	EXPECT_TRUE(IntsToStr(pEdited->m_aSkin, std::size(pEdited->m_aSkin), aSkin, sizeof(aSkin)));
	EXPECT_STREQ(aName, "*edited-name*");
	EXPECT_STREQ(aClan, "edited-clan");
	EXPECT_STREQ(aSkin, "edited-skin");
	EXPECT_EQ(pEdited->m_UseCustomColor, 1);
	EXPECT_EQ(pEdited->m_ColorBody, 0xabcdef);
	EXPECT_EQ(pEdited->m_ColorFeet, 0xfedcba);
}

TEST(DemoTeeEdit, NickOnlyRetainsSkinAndColorsAndDisablingCustomColorsIsApplied)
{
	const CNetObj_ClientInfo Original = MakeInfo("original-name", "unchanged-skin");
	STestSnapshot Snapshot = MakeSnapshot(&Original);
	std::array<SDemoTeeOverride, MAX_CLIENTS> Overrides{};
	Overrides[3] = MakeOverride();
	Overrides[3].m_OverrideAppearance = false;

	ApplyDemoTeeOverrides(Snapshot.m_Buffer.m_aData, Snapshot.m_Size, false, Overrides);
	const CNetObj_ClientInfo *pEdited = FindInfo(Snapshot.m_Buffer);
	char aName[MAX_NAME_LENGTH];
	char aClan[MAX_CLAN_LENGTH];
	char aSkin[MAX_SKIN_LENGTH];
	IntsToStr(pEdited->m_aName, std::size(pEdited->m_aName), aName, sizeof(aName));
	IntsToStr(pEdited->m_aClan, std::size(pEdited->m_aClan), aClan, sizeof(aClan));
	IntsToStr(pEdited->m_aSkin, std::size(pEdited->m_aSkin), aSkin, sizeof(aSkin));
	EXPECT_STREQ(aName, "*edited-name*");
	EXPECT_STREQ(aClan, "edited-clan");
	EXPECT_STREQ(aSkin, "unchanged-skin");
	EXPECT_EQ(pEdited->m_UseCustomColor, Original.m_UseCustomColor);
	EXPECT_EQ(pEdited->m_ColorBody, Original.m_ColorBody);
	EXPECT_EQ(pEdited->m_ColorFeet, Original.m_ColorFeet);

	Overrides[3].m_OverrideAppearance = true;
	Overrides[3].m_UseCustomColors = false;
	Snapshot = MakeSnapshot(&Original);
	ApplyDemoTeeOverrides(Snapshot.m_Buffer.m_aData, Snapshot.m_Size, false, Overrides);
	pEdited = FindInfo(Snapshot.m_Buffer);
	EXPECT_EQ(pEdited->m_UseCustomColor, 0);
	EXPECT_EQ(pEdited->m_ColorBody, 0xabcdef);
	EXPECT_EQ(pEdited->m_ColorFeet, 0xfedcba);
}

TEST(DemoTeeEdit, RequiresMatchingOriginalNameAndActiveOverrideForSameClientId)
{
	const CNetObj_ClientInfo Original = MakeInfo("reused-client");
	STestSnapshot Snapshot = MakeSnapshot(&Original);
	std::array<SDemoTeeOverride, MAX_CLIENTS> Overrides{};
	Overrides[3] = MakeOverride("previous-player");
	const CSnapshotBuffer BeforeMismatch = Snapshot.m_Buffer;
	ApplyDemoTeeOverrides(Snapshot.m_Buffer.m_aData, Snapshot.m_Size, false, Overrides);
	EXPECT_EQ(std::memcmp(Snapshot.m_Buffer.m_aData, BeforeMismatch.m_aData, Snapshot.m_Size), 0);

	Overrides[3].m_aOriginalName[0] = '\0';
	Overrides[3].m_Active = false;
	Snapshot = MakeSnapshot(&Original);
	const CSnapshotBuffer BeforeInactive = Snapshot.m_Buffer;
	ApplyDemoTeeOverrides(Snapshot.m_Buffer.m_aData, Snapshot.m_Size, false, Overrides);
	EXPECT_EQ(std::memcmp(Snapshot.m_Buffer.m_aData, BeforeInactive.m_aData, Snapshot.m_Size), 0);
}

TEST(DemoTeeEdit, DoesNotEditSixupSnapshotsAndKeepsSourceCopyUnchanged)
{
	const CNetObj_ClientInfo Original = MakeInfo("original-name");
	STestSnapshot Source = MakeSnapshot(&Original);
	CSnapshotBuffer Copy = Source.m_Buffer;
	const CSnapshotBuffer Before = Source.m_Buffer;
	std::array<SDemoTeeOverride, MAX_CLIENTS> Overrides{};
	Overrides[3] = MakeOverride();

	ApplyDemoTeeOverrides(Copy.m_aData, Source.m_Size, true, Overrides);
	EXPECT_EQ(std::memcmp(Copy.m_aData, Before.m_aData, Source.m_Size), 0);
	EXPECT_EQ(std::memcmp(Source.m_Buffer.m_aData, Before.m_aData, Source.m_Size), 0);

	ApplyDemoTeeOverrides(Copy.m_aData, Source.m_Size, false, Overrides);
	EXPECT_EQ(std::memcmp(Source.m_Buffer.m_aData, Before.m_aData, Source.m_Size), 0);
}

TEST(DemoTeeEdit, MarksChangedNameWithStarsAndKeepsExistingMarkers)
{
	char aName[MAX_NAME_LENGTH];
	str_copy(aName, "RoflikBEST");
	MarkChangedDemoName(aName, sizeof(aName));
	EXPECT_STREQ(aName, "*RoflikBEST*");
	MarkChangedDemoName(aName, sizeof(aName));
	EXPECT_STREQ(aName, "*RoflikBEST*");

	str_copy(aName, "12345678901234567890");
	MarkChangedDemoName(aName, sizeof(aName));
	EXPECT_STREQ(aName, "*1234567890123*");
	EXPECT_LT(str_length(aName), MAX_NAME_LENGTH);
}

TEST(DemoTeeEdit, SkipsMalformedClientInfoItem)
{
	const CNetObj_ClientInfo Original = MakeInfo("original-name");
	STestSnapshot Snapshot = MakeSnapshot(&Original, 3, sizeof(CNetObj_ClientInfo) - sizeof(int));
	ASSERT_TRUE(Snapshot.m_Buffer.AsSnapshot()->IsValid(Snapshot.m_Size));
	std::array<SDemoTeeOverride, MAX_CLIENTS> Overrides{};
	Overrides[3] = MakeOverride();
	const CSnapshotBuffer Before = Snapshot.m_Buffer;

	ApplyDemoTeeOverrides(Snapshot.m_Buffer.m_aData, Snapshot.m_Size, false, Overrides);
	EXPECT_EQ(std::memcmp(Snapshot.m_Buffer.m_aData, Before.m_aData, Snapshot.m_Size), 0);
}
