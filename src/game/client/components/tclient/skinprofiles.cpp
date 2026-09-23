#include "skinprofiles.h"

#include <engine/config.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/components/bestclient/profile_assets.h>
#include <game/client/gameclient.h>

static void EscapeParam(char *pDst, const char *pSrc, int Size)
{
	str_escape(&pDst, pSrc, pDst + Size);
}

// bestclient
CProfile::CProfile(int BodyColor, int FeetColor, int CountryFlag, int Emote, const char *pSkinName, const char *pName, const char *pClan,
	const char *pAssetEntities, const char *pAssetGame, const char *pAssetParticles,
	const char *pAssetHud, const char *pAssetExtras, const char *pAssetCursor, const char *pAssetArrow,
	const char *pAssetEmoticons, const char *pAssetAudio)
{
	m_BodyColor = BodyColor;
	m_FeetColor = FeetColor;
	m_CountryFlag = CountryFlag;
	m_Emote = Emote;
	str_copy(m_SkinName, pSkinName);
	str_copy(m_Name, pName);
	str_copy(m_Clan, pClan);
	str_copy(m_AssetEntities, pAssetEntities);
	str_copy(m_AssetGame, pAssetGame);
	str_copy(m_AssetParticles, pAssetParticles);
	str_copy(m_AssetHud, pAssetHud);
	str_copy(m_AssetExtras, pAssetExtras);
	str_copy(m_AssetCursor, pAssetCursor);
	str_copy(m_AssetArrow, pAssetArrow);
	str_copy(m_AssetEmoticons, pAssetEmoticons);
	str_copy(m_AssetAudio, pAssetAudio);
}
// bestclient

void CSkinProfiles::OnConsoleInit()
{
	IConfigManager *pConfigManager = Kernel()->RequestInterface<IConfigManager>();
	if(pConfigManager)
		pConfigManager->RegisterCallback(ConfigSaveCallback, this, ConfigDomain::TCLIENTPROFILES);

	// bestclient
	Console()->Register("add_profile", "i[body] i[feet] i[flag] i[emote] s[skin] s[name] s[clan] ?s[entities] ?s[game] ?s[particles] ?s[hud] ?s[extras] ?s[cursor] ?s[arrow] ?s[emoticons] ?s[audio]", CFGFLAG_CLIENT, ConAddProfile, this, "Add a profile");
	// bestclient
}

void CSkinProfiles::ConAddProfile(IConsole::IResult *pResult, void *pUserData)
{
	CSkinProfiles *pSelf = (CSkinProfiles *)pUserData;
	// bestclient
	pSelf->AddProfile(
		pResult->GetInteger(0), pResult->GetInteger(1), pResult->GetInteger(2), pResult->GetInteger(3),
		pResult->GetString(4), pResult->GetString(5), pResult->GetString(6),
		pResult->GetString(7), pResult->GetString(8), pResult->GetString(9),
		pResult->GetString(10), pResult->GetString(11), pResult->GetString(12), pResult->GetString(13),
		pResult->GetString(14), pResult->GetString(15));
	// bestclient
}

// bestclient
void CSkinProfiles::AddProfile(int BodyColor, int FeetColor, int CountryFlag, int Emote, const char *pSkinName, const char *pName, const char *pClan,
	const char *pAssetEntities, const char *pAssetGame, const char *pAssetParticles,
	const char *pAssetHud, const char *pAssetExtras, const char *pAssetCursor, const char *pAssetArrow,
	const char *pAssetEmoticons, const char *pAssetAudio)
{
	CProfile Profile = CProfile(BodyColor, FeetColor, CountryFlag, Emote, pSkinName, pName, pClan,
		pAssetEntities, pAssetGame, pAssetParticles, pAssetHud, pAssetExtras, pAssetCursor, pAssetArrow,
		pAssetEmoticons, pAssetAudio);
	m_Profiles.push_back(Profile);
}
// bestclient

void CSkinProfiles::ApplyProfile(int Dummy, const CProfile &Profile)
{
	if(g_Config.m_TcProfileSkin && strlen(Profile.m_SkinName) != 0)
		str_copy(Dummy ? g_Config.m_ClDummySkin : g_Config.m_ClPlayerSkin, Profile.m_SkinName);
	if(g_Config.m_TcProfileColors && Profile.m_BodyColor != -1 && Profile.m_FeetColor != -1)
	{
		(Dummy ? g_Config.m_ClDummyColorBody : g_Config.m_ClPlayerColorBody) = Profile.m_BodyColor;
		(Dummy ? g_Config.m_ClDummyColorFeet : g_Config.m_ClPlayerColorFeet) = Profile.m_FeetColor;
	}
	if(g_Config.m_TcProfileEmote && Profile.m_Emote > 0 && Profile.m_Emote < NUM_EMOTES)
		(Dummy ? g_Config.m_ClDummyDefaultEyes : g_Config.m_ClPlayerDefaultEyes) = Profile.m_Emote;
	if(g_Config.m_TcProfileName && strlen(Profile.m_Name) != 0)
		str_copy(Dummy ? g_Config.m_ClDummyName : g_Config.m_PlayerName, Profile.m_Name); // TODO m_ClPlayerName
	if(g_Config.m_TcProfileClan && (strlen(Profile.m_Clan) != 0 || g_Config.m_TcProfileOverwriteClanWithEmpty))
		str_copy(Dummy ? g_Config.m_ClDummyClan : g_Config.m_PlayerClan, Profile.m_Clan); // TODO m_ClPlayerClan
	if(g_Config.m_TcProfileFlag && Profile.m_CountryFlag != -2)
		(Dummy ? g_Config.m_ClDummyCountry : g_Config.m_PlayerCountry) = Profile.m_CountryFlag;
	// bestclient
	BestClientApplyProfileAssets(GameClient(), Profile);
	// bestclient
	GameClient()->m_Skins.m_SkinList.ForceRefresh(); // Prevent segfault
	if(Dummy)
		GameClient()->SendDummyInfo(false);
	else
		GameClient()->SendInfo(false);
}

void CSkinProfiles::ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData)
{
	CSkinProfiles *pThis = (CSkinProfiles *)pUserData;
	// bestclient
	char aBuf[1024];
	// bestclient
	char aBufTemp[128];
	char aEscapeBuf[256];
	for(const CProfile &Profile : pThis->m_Profiles)
	{
		str_copy(aBuf, "add_profile ", sizeof(aBuf));

		str_format(aBufTemp, sizeof(aBufTemp), "%d ", Profile.m_BodyColor);
		str_append(aBuf, aBufTemp, sizeof(aBuf));

		str_format(aBufTemp, sizeof(aBufTemp), "%d ", Profile.m_FeetColor);
		str_append(aBuf, aBufTemp, sizeof(aBuf));

		str_format(aBufTemp, sizeof(aBufTemp), "%d ", Profile.m_CountryFlag);
		str_append(aBuf, aBufTemp, sizeof(aBuf));

		str_format(aBufTemp, sizeof(aBufTemp), "%d ", Profile.m_Emote);
		str_append(aBuf, aBufTemp, sizeof(aBuf));

		EscapeParam(aEscapeBuf, Profile.m_SkinName, sizeof(aEscapeBuf));
		str_format(aBufTemp, sizeof(aBufTemp), "\"%s\" ", aEscapeBuf);
		str_append(aBuf, aBufTemp, sizeof(aBuf));

		EscapeParam(aEscapeBuf, Profile.m_Name, sizeof(aEscapeBuf));
		str_format(aBufTemp, sizeof(aBufTemp), "\"%s\" ", aEscapeBuf);
		str_append(aBuf, aBufTemp, sizeof(aBuf));

		EscapeParam(aEscapeBuf, Profile.m_Clan, sizeof(aEscapeBuf));
		str_format(aBufTemp, sizeof(aBufTemp), "\"%s\"", aEscapeBuf);
		str_append(aBuf, aBufTemp, sizeof(aBuf));

		// bestclient
		const char *apAssets[] = {
			Profile.m_AssetEntities, Profile.m_AssetGame,
			Profile.m_AssetParticles, Profile.m_AssetHud,
			Profile.m_AssetExtras, Profile.m_AssetCursor, Profile.m_AssetArrow,
			Profile.m_AssetEmoticons, Profile.m_AssetAudio,
		};
		for(const char *pAsset : apAssets)
		{
			EscapeParam(aEscapeBuf, pAsset, sizeof(aEscapeBuf));
			str_format(aBufTemp, sizeof(aBufTemp), " \"%s\"", aEscapeBuf);
			str_append(aBuf, aBufTemp, sizeof(aBuf));
		}
		// bestclient

		pConfigManager->WriteLine(aBuf, ConfigDomain::TCLIENTPROFILES);
	}
}
