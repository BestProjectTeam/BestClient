#include "bestclient.h"

#include "data_version.h"
#include "version.h"

#include <base/color.h>
#include <base/log.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/client/enums.h>
#include <engine/external/regex.h>
#include <engine/external/tinyexpr.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>

#include <generated/protocol.h>
#include <generated/protocol7.h>
#include <generated/client_data.h>

#include <game/client/animstate.h>
#include <game/client/components/chat.h>
#include <game/client/components/hud_layout.h>
#include <game/client/components/sounds.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/ui.h>
#include <game/localization.h>
#include <game/version.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>
#include <tlhelp32.h>
#ifdef ERROR
#undef ERROR
#endif
#endif

static constexpr const char *BestClient_INFO_URL = "https://api.github.com/repos/RoflikBEST/bestdownload/releases?per_page=10";
static constexpr int s_HookComboBaseTextCount = 15;
static constexpr int s_HookComboVariantLimit = 100;
static constexpr int s_HookComboSoundCount = 7;
static constexpr int s_HookComboBrilliantSoundIndex = 5; // 0-based => sound #6
static constexpr int s_HookComboModeHook = 0;
static constexpr int s_HookComboModeHammer = 1;
static constexpr int s_HookComboModeHookAndHammer = 2;

static constexpr const char *const s_apHookComboTexts[s_HookComboBaseTextCount] = {
	"cool",
	"nice",
	"great",
	"awesome",
	"excellent",
	"amazing",
	"fantastic",
	"incredible",
	"spectacular",
	"legendary",
	"mythic",
	"unstoppable",
	"dominant",
	"masterful",
	"BRILLIANT"};

static const ColorRGBA s_aHookComboColors[s_HookComboBaseTextCount] = {
	ColorRGBA(0.36f, 1.0f, 0.50f, 1.0f),
	ColorRGBA(0.28f, 0.78f, 1.0f, 1.0f),
	ColorRGBA(0.40f, 1.0f, 0.92f, 1.0f),
	ColorRGBA(1.0f, 0.75f, 0.26f, 1.0f),
	ColorRGBA(1.0f, 0.52f, 0.23f, 1.0f),
	ColorRGBA(1.0f, 0.40f, 0.70f, 1.0f),
	ColorRGBA(0.96f, 0.96f, 0.34f, 1.0f),
	ColorRGBA(0.65f, 0.90f, 1.0f, 1.0f),
	ColorRGBA(0.75f, 1.0f, 0.82f, 1.0f),
	ColorRGBA(1.0f, 0.66f, 0.38f, 1.0f),
	ColorRGBA(0.92f, 0.74f, 1.0f, 1.0f),
	ColorRGBA(1.0f, 0.58f, 0.58f, 1.0f),
	ColorRGBA(1.0f, 0.85f, 0.48f, 1.0f),
	ColorRGBA(0.80f, 0.95f, 0.50f, 1.0f),
	ColorRGBA(1.0f, 0.97f, 0.35f, 1.0f)};

static void NormalizeBestClientVersion(const char *pVersion, char *pBuf, int BufSize)
{
	if(BufSize <= 0)
		return;

	if(!pVersion)
	{
		pBuf[0] = '\0';
		return;
	}

	while(*pVersion != '\0' && std::isspace(static_cast<unsigned char>(*pVersion)))
		++pVersion;

	if((pVersion[0] == 'v' || pVersion[0] == 'V') && std::isdigit(static_cast<unsigned char>(pVersion[1])))
		++pVersion;

	str_copy(pBuf, pVersion, BufSize);
}

static std::vector<int> ExtractBestClientVersionNumbers(const char *pVersion)
{
	std::vector<int> vNumbers;
	if(!pVersion)
		return vNumbers;

	int Current = -1;
	for(const unsigned char *p = reinterpret_cast<const unsigned char *>(pVersion); *p != '\0'; ++p)
	{
		if(std::isdigit(*p))
		{
			if(Current < 0)
				Current = 0;
			Current = Current * 10 + (*p - '0');
		}
		else if(Current >= 0)
		{
			vNumbers.push_back(Current);
			Current = -1;
		}
	}

	if(Current >= 0)
		vNumbers.push_back(Current);

	return vNumbers;
}

static int CompareBestClientVersions(const char *pLeft, const char *pRight)
{
	char aLeft[64];
	char aRight[64];
	NormalizeBestClientVersion(pLeft, aLeft, sizeof(aLeft));
	NormalizeBestClientVersion(pRight, aRight, sizeof(aRight));

	const std::vector<int> vLeft = ExtractBestClientVersionNumbers(aLeft);
	const std::vector<int> vRight = ExtractBestClientVersionNumbers(aRight);
	const size_t Num = maximum(vLeft.size(), vRight.size());
	for(size_t i = 0; i < Num; ++i)
	{
		const int Left = i < vLeft.size() ? vLeft[i] : 0;
		const int Right = i < vRight.size() ? vRight[i] : 0;
		if(Left < Right)
			return -1;
		if(Left > Right)
			return 1;
	}

	return str_comp_nocase(aLeft, aRight);
}

static void BuildBestClientInfoUrl(char *pBuf, int BufSize)
{
	str_format(pBuf, BufSize, "%s&t=%lld", BestClient_INFO_URL, (long long)time_timestamp());
}

static const char *FindBestClientReleaseVersion(const json_value *pJson)
{
	if(!pJson)
		return nullptr;

	if(pJson->type == json_object)
	{
		const char *pVersion = json_string_get(json_object_get(pJson, "tag_name"));
		if(!pVersion)
			pVersion = json_string_get(json_object_get(pJson, "name"));
		return pVersion;
	}

	if(pJson->type == json_array)
	{
		for(int i = 0; i < json_array_length(pJson); ++i)
		{
			const json_value *pRelease = json_array_get(pJson, i);
			if(!pRelease || pRelease->type != json_object)
				continue;
			const char *pVersion = json_string_get(json_object_get(pRelease, "tag_name"));
			if(!pVersion)
				pVersion = json_string_get(json_object_get(pRelease, "name"));
			if(pVersion)
				return pVersion;
		}
	}

	return nullptr;
}

static void FormatHookComboText(int Sequence, char *pBuf, int BufSize)
{
	if(Sequence <= s_HookComboBaseTextCount)
	{
		str_copy(pBuf, s_apHookComboTexts[Sequence - 1], BufSize);
		return;
	}

	if(Sequence <= s_HookComboVariantLimit)
	{
		static constexpr const char *const s_apAdvancedTitles[] = {
			"brilliant",
			"godlike",
			"unreal",
			"mythic",
			"supreme",
			"transcendent",
			"unstoppable",
			"devastating",
			"apex",
			"ascendant"};
		static constexpr int s_AdvancedTitleCount = 10;
		const int Step = Sequence - s_HookComboBaseTextCount - 1;
		const int GroupSize = 9;
		const int Group = minimum<int>(Step / GroupSize, s_AdvancedTitleCount - 1);
		str_format(pBuf, BufSize, "%s %d", s_apAdvancedTitles[Group], Sequence);
		return;
	}

	str_copy(pBuf, "BRILLIANT", BufSize);
}

static int NormalizeAspectPreset(int AspectX100)
{
	static const int s_aAspectPresets[] = {125, 133, 150};
	if(AspectX100 <= 0)
		return 0;

	int Best = s_aAspectPresets[0];
	int BestDiff = Best > AspectX100 ? Best - AspectX100 : AspectX100 - Best;
	for(int Preset : s_aAspectPresets)
	{
		const int Diff = Preset > AspectX100 ? Preset - AspectX100 : AspectX100 - Preset;
		if(Diff < BestDiff)
		{
			Best = Preset;
			BestDiff = Diff;
		}
	}
	return Best;
}

static int GetConfiguredAspectMode()
{
	if(g_Config.m_BcCustomAspectRatioMode >= 0)
		return g_Config.m_BcCustomAspectRatioMode;
	return g_Config.m_BcCustomAspectRatio > 0 ? 1 : 0;
}

static int NormalizeAspectValueForMode(int AspectMode, int AspectX100)
{
	if(AspectMode <= 0)
		return 0;
	if(AspectMode == 1)
		return NormalizeAspectPreset(AspectX100 > 0 ? AspectX100 : 177);
	return std::clamp(AspectX100 >= 100 ? AspectX100 : 177, 100, 300);
}

CBestClient::CBestClient()
{
	OnReset();
}

bool CBestClient::IsComponentDisabledByMask(int Component, int MaskLo, int MaskHi)
{
	if(Component < 0 || Component >= NUM_COMPONENTS_EDITOR_COMPONENTS)
		return false;

	if(Component < 31)
		return (MaskLo & (1 << Component)) != 0;

	const int HiBit = Component - 31;
	return HiBit >= 0 && HiBit < 31 && (MaskHi & (1 << HiBit)) != 0;
}

bool CBestClient::IsComponentDisabled(EBestClientComponent Component) const
{
	return IsComponentDisabledByMask((int)Component, g_Config.m_BcDisabledComponentsMaskLo, g_Config.m_BcDisabledComponentsMaskHi);
}

bool CBestClient::OptimizerEnabled() const
{
	if(IsComponentDisabled(COMPONENT_VISUALS_OPTIMIZER))
		return false;
	return g_Config.m_BcOptimizer != 0;
}

bool CBestClient::OptimizerDisableParticles() const
{
	return OptimizerEnabled() && g_Config.m_BcOptimizerDisableParticles != 0;
}

bool CBestClient::OptimizerFpsFogEnabled() const
{
	return OptimizerEnabled() && g_Config.m_BcOptimizerFpsFog != 0;
}

void CBestClient::OptimizerFpsFogHalfExtents(float &HalfW, float &HalfH) const
{
	HalfW = 0.0f;
	HalfH = 0.0f;

	if(!OptimizerFpsFogEnabled())
		return;

	if(g_Config.m_BcOptimizerFpsFogMode == 0)
	{
		const float Radius = (float)g_Config.m_BcOptimizerFpsFogRadiusTiles * 32.0f;
		HalfW = Radius;
		HalfH = Radius;
		return;
	}

	float Width = 0.0f;
	float Height = 0.0f;
	Graphics()->CalcScreenParams(Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom, &Width, &Height);
	const float Percent = std::clamp(g_Config.m_BcOptimizerFpsFogZoomPercent, 1, 100) / 100.0f;
	HalfW = Width * Percent * 0.5f;
	HalfH = Height * Percent * 0.5f;
}

bool CBestClient::OptimizerAllowRenderPos(vec2 WorldPos) const
{
	if(!OptimizerFpsFogEnabled())
		return true;

	float HalfW = 0.0f;
	float HalfH = 0.0f;
	OptimizerFpsFogHalfExtents(HalfW, HalfH);
	if(HalfW <= 0.0f || HalfH <= 0.0f)
		return true;

	const vec2 Center = GameClient()->m_Camera.m_Center;
	return std::abs(WorldPos.x - Center.x) <= HalfW && std::abs(WorldPos.y - Center.y) <= HalfH;
}

#if defined(CONF_FAMILY_WINDOWS)
static bool SetPriorityClassForPid(DWORD Pid, DWORD PriorityClass)
{
	const HANDLE Process = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, Pid);
	if(Process == nullptr)
		return false;
	const bool Ok = SetPriorityClass(Process, PriorityClass) != 0;
	CloseHandle(Process);
	return Ok;
}

static int SetPriorityClassForProcessNames(const wchar_t *const *ppExeNames, size_t NumNames, DWORD PriorityClass)
{
	const HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if(Snapshot == INVALID_HANDLE_VALUE)
		return -1;

	PROCESSENTRY32W Entry;
	Entry.dwSize = sizeof(Entry);
	if(!Process32FirstW(Snapshot, &Entry))
	{
		CloseHandle(Snapshot);
		return -1;
	}

	int SuccessCount = 0;
	do
	{
		bool Match = false;
		for(size_t i = 0; i < NumNames; i++)
		{
			if(_wcsicmp(Entry.szExeFile, ppExeNames[i]) == 0)
			{
				Match = true;
				break;
			}
		}

		if(Match && SetPriorityClassForPid(Entry.th32ProcessID, PriorityClass))
			SuccessCount++;
	} while(Process32NextW(Snapshot, &Entry));

	CloseHandle(Snapshot);
	return SuccessCount;
}
#endif

void CBestClient::OptimizerSetDdnetPriorityHigh()
{
#if defined(CONF_FAMILY_WINDOWS)
	if(SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS) == 0)
		log_warn("optimizer", "Failed to set DDNet priority to High (error %lu)", (unsigned long)GetLastError());
	else
		log_info("optimizer", "DDNet priority set to High");
#else
	log_info("optimizer", "Setting process priority is only supported on Windows");
#endif
}

void CBestClient::OptimizerSetDiscordPriorityBelowNormal()
{
#if defined(CONF_FAMILY_WINDOWS)
	static const wchar_t *const s_apDiscordExeNames[] = {L"Discord.exe", L"DiscordPTB.exe", L"DiscordCanary.exe"};
	const int Count = SetPriorityClassForProcessNames(s_apDiscordExeNames, std::size(s_apDiscordExeNames), BELOW_NORMAL_PRIORITY_CLASS);
	if(Count < 0)
	{
		log_warn("optimizer", "Failed to enumerate processes to set Discord priority (error %lu)", (unsigned long)GetLastError());
	}
	else if(Count == 0)
	{
		log_info("optimizer", "No Discord processes found to set priority");
	}
	else
	{
		log_info("optimizer", "Set Discord priority to Below Normal for %d process(es)", Count);
	}
#else
	log_info("optimizer", "Setting process priority is only supported on Windows");
#endif
}

void CBestClient::OptimizerUpdateProcessPriorities()
{
#if defined(CONF_FAMILY_WINDOWS)
	const bool WantDdnetHigh = OptimizerEnabled() && g_Config.m_BcOptimizerDdnetPriorityHigh != 0;
	if(WantDdnetHigh && !m_OptimizerDdnetPriorityHighActive)
	{
		const DWORD Prev = GetPriorityClass(GetCurrentProcess());
		m_OptimizerDdnetPrevPriorityClass = Prev != 0 ? (unsigned long)Prev : (unsigned long)NORMAL_PRIORITY_CLASS;
		m_OptimizerDdnetPriorityHighActive = true;
	}
	else if(!WantDdnetHigh && m_OptimizerDdnetPriorityHighActive)
	{
		SetPriorityClass(GetCurrentProcess(), (DWORD)m_OptimizerDdnetPrevPriorityClass);
		m_OptimizerDdnetPriorityHighActive = false;
	}

	if(m_OptimizerDdnetPriorityHighActive)
		SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

	static const wchar_t *const s_apDiscordExeNames[] = {L"Discord.exe", L"DiscordPTB.exe", L"DiscordCanary.exe"};
	const bool WantDiscordBelow = OptimizerEnabled() && g_Config.m_BcOptimizerDiscordPriorityBelowNormal != 0;
	if(WantDiscordBelow && !m_OptimizerDiscordPriorityBelowNormalActive)
	{
		SetPriorityClassForProcessNames(s_apDiscordExeNames, std::size(s_apDiscordExeNames), BELOW_NORMAL_PRIORITY_CLASS);
		m_OptimizerDiscordPriorityBelowNormalActive = true;
		m_OptimizerDiscordPriorityLastUpdateTime = -1.0f;
	}
	else if(!WantDiscordBelow && m_OptimizerDiscordPriorityBelowNormalActive)
	{
		SetPriorityClassForProcessNames(s_apDiscordExeNames, std::size(s_apDiscordExeNames), NORMAL_PRIORITY_CLASS);
		m_OptimizerDiscordPriorityBelowNormalActive = false;
		m_OptimizerDiscordPriorityLastUpdateTime = -1.0f;
	}

	if(m_OptimizerDiscordPriorityBelowNormalActive)
	{
		const float Now = Client()->LocalTime();
		if(m_OptimizerDiscordPriorityLastUpdateTime < 0.0f || (Now - m_OptimizerDiscordPriorityLastUpdateTime) >= 2.0f)
		{
			SetPriorityClassForProcessNames(s_apDiscordExeNames, std::size(s_apDiscordExeNames), BELOW_NORMAL_PRIORITY_CLASS);
			m_OptimizerDiscordPriorityLastUpdateTime = Now;
		}
	}
#else
	(void)0;
#endif
}

void CBestClient::LoadHookComboSounds(bool LogErrors)
{
	for(int &SoundId : m_aHookComboSoundIds)
		SoundId = -1;

	for(int i = 0; i < (int)m_aHookComboSoundIds.size(); ++i)
	{
		auto TryLoad = [this, i](const char *pPath, int StorageType) {
			if(m_aHookComboSoundIds[i] != -1)
				return;
			if(!Storage()->FileExists(pPath, StorageType))
				return;
			m_aHookComboSoundIds[i] = Sound()->LoadWV(pPath, StorageType);
		};

		char aPathWv[64];
		char aDataPathWv[96];
		char aParentRelativeDataPathWv[112];
		char aBinaryDataPathWv[IO_MAX_PATH_LENGTH];
		char aParentDataPathWv[IO_MAX_PATH_LENGTH];
		str_format(aPathWv, sizeof(aPathWv), "audio/combo%d.wv", i + 1);
		str_format(aDataPathWv, sizeof(aDataPathWv), "data/audio/combo%d.wv", i + 1);
		str_format(aParentRelativeDataPathWv, sizeof(aParentRelativeDataPathWv), "../%s", aDataPathWv);
		Storage()->GetBinaryPathAbsolute(aDataPathWv, aBinaryDataPathWv, sizeof(aBinaryDataPathWv));
		Storage()->GetBinaryPathAbsolute(aParentRelativeDataPathWv, aParentDataPathWv, sizeof(aParentDataPathWv));

		// Load from data/audio/combo1-7.wv with fallbacks for IDE/workdir differences.
		TryLoad(aPathWv, IStorage::TYPE_ALL);
		TryLoad(aDataPathWv, IStorage::TYPE_ALL);
		TryLoad(aBinaryDataPathWv, IStorage::TYPE_ABSOLUTE);
		TryLoad(aParentDataPathWv, IStorage::TYPE_ABSOLUTE);

		if(LogErrors && m_aHookComboSoundIds[i] == -1)
			log_warn("hook_combo", "Failed to load combo sound #%d (expected data/audio/combo%d.wv)", i + 1, i + 1);
	}
}

void CBestClient::UnloadHookComboSounds()
{
	for(int &SoundId : m_aHookComboSoundIds)
	{
		if(SoundId != -1)
		{
			Sound()->UnloadSample(SoundId);
			SoundId = -1;
		}
	}
}

void CBestClient::ResetHookComboState()
{
	m_HookComboCounter = 0;
	m_HookComboLastHookTime = -1.0f;
	m_HookComboTrackedClientId = -1;
	m_HookComboLastHookedPlayer = -1;
	m_HookComboSoundErrorShown = false;
	m_vHookComboPopups.clear();
}

//45 degrees
void CBestClient::ConToggle45Degrees(IConsole::IResult *pResult, void *pUserData)
{
	CBestClient *pSelf = static_cast<CBestClient *>(pUserData);
	pSelf->m_45degreestoggle = pResult->GetInteger(0) != 0;
	if(!g_Config.m_BCToggle45degrees)
	{
		if(pSelf->m_45degreestoggle && !pSelf->m_45degreestogglelastinput)
		{
			pSelf->GameClient()->Echo("[[green]] 45° on");
			pSelf->m_45degreesEnabled = 1;
			g_Config.m_BCPrevInpMousesens45degrees = (pSelf->m_SmallsensEnabled == 1 ? g_Config.m_BCPrevInpMousesensSmallsens : g_Config.m_InpMousesens);
			g_Config.m_BCPrevMouseMaxDistance45degrees = g_Config.m_ClMouseMaxDistance;
			g_Config.m_ClMouseMaxDistance = 2;
			g_Config.m_InpMousesens = 4;
		}
		else if(!pSelf->m_45degreestoggle)
		{
			pSelf->m_45degreesEnabled = 0;
			pSelf->GameClient()->Echo("[[red]] 45° off");
			g_Config.m_ClMouseMaxDistance = g_Config.m_BCPrevMouseMaxDistance45degrees;
			g_Config.m_InpMousesens = g_Config.m_BCPrevInpMousesens45degrees;
		}
		pSelf->m_45degreestogglelastinput = pSelf->m_45degreestoggle;
	}

	if(g_Config.m_BCToggle45degrees)
	{
		if(pSelf->m_45degreestoggle && !pSelf->m_45degreestogglelastinput)
		{
			if(g_Config.m_ClMouseMaxDistance == 2)
			{
				pSelf->m_45degreesEnabled = 0;
				pSelf->GameClient()->Echo("[[red]] 45° off");
				g_Config.m_ClMouseMaxDistance = g_Config.m_BCPrevMouseMaxDistance45degrees;
				g_Config.m_InpMousesens = g_Config.m_BCPrevInpMousesens45degrees;
			}
			else
			{
				pSelf->m_45degreesEnabled = 1;
				pSelf->GameClient()->Echo("[[green]] 45° on");
				g_Config.m_BCPrevInpMousesens45degrees = (pSelf->m_SmallsensEnabled == 1 ? g_Config.m_BCPrevInpMousesensSmallsens : g_Config.m_InpMousesens);
				g_Config.m_BCPrevMouseMaxDistance45degrees = g_Config.m_ClMouseMaxDistance;
				g_Config.m_ClMouseMaxDistance = 2;
				g_Config.m_InpMousesens = 4;
			}
		}
		pSelf->m_45degreestogglelastinput = pSelf->m_45degreestoggle;
	}
}
//Small sens toggle
void CBestClient::ConToggleSmallSens(IConsole::IResult *pResult, void *pUserData)
{
	CBestClient *pSelf = static_cast<CBestClient *>(pUserData);
	pSelf->m_Smallsenstoggle = pResult->GetInteger(0) != 0;
	if(!g_Config.m_BCToggleSmallSens)
	{
		if(pSelf->m_Smallsenstoggle && !pSelf->m_Smallsenstogglelastinput)
		{
			pSelf->m_SmallsensEnabled = 1;
			pSelf->GameClient()->Echo("[[green]] small sens on");
			g_Config.m_BCPrevInpMousesensSmallsens = (pSelf->m_45degreesEnabled == 1 ? g_Config.m_BCPrevInpMousesens45degrees : g_Config.m_InpMousesens);
			g_Config.m_InpMousesens = 1;
		}
		else if(!pSelf->m_Smallsenstoggle)
		{
			pSelf->m_SmallsensEnabled = 0;
			pSelf->GameClient()->Echo("[[red]] small sens off");
			g_Config.m_InpMousesens = g_Config.m_BCPrevInpMousesensSmallsens;
		}
		pSelf->m_Smallsenstogglelastinput = pSelf->m_Smallsenstoggle;
	}

	if(g_Config.m_BCToggleSmallSens)
	{
		if(pSelf->m_Smallsenstoggle && !pSelf->m_Smallsenstogglelastinput)
		{
			if(g_Config.m_InpMousesens == 1)
			{
				pSelf->m_SmallsensEnabled = 0;
				pSelf->GameClient()->Echo("[[red]] small sens off");
				g_Config.m_InpMousesens = g_Config.m_BCPrevInpMousesensSmallsens;
			}
			else
			{
				pSelf->m_SmallsensEnabled = 1;
				pSelf->GameClient()->Echo("[[green]] small sens on");
				g_Config.m_BCPrevInpMousesensSmallsens = (pSelf->m_45degreesEnabled == 1 ? g_Config.m_BCPrevInpMousesens45degrees : g_Config.m_InpMousesens);
				g_Config.m_InpMousesens = 1;
			}
		}
		pSelf->m_Smallsenstogglelastinput = pSelf->m_Smallsenstoggle;
	}
}

//Deepfly
void CBestClient::ConToggleDeepfly(IConsole::IResult *pResult, void *pUserData)
{
	CBestClient *pSelf = static_cast<CBestClient *>(pUserData);
	char CurBind[128];
	str_copy(CurBind, pSelf->GameClient()->m_Binds.Get(291, 0), sizeof(CurBind));
	if(str_find_nocase(CurBind, "+toggle cl_dummy_hammer"))
	{
		pSelf->GameClient()->Echo("[[red]] Deepfly off");
		if(str_length(pSelf->m_Oldmouse1Bind) > 1)
			pSelf->GameClient()->m_Binds.Bind(291, pSelf->m_Oldmouse1Bind, false, 0);
		else
		{
			pSelf->GameClient()->Echo("[[red]] No old bind in memory. Binding +fire");
			pSelf->GameClient()->m_Binds.Bind(291, "+fire", false, 0);
		};
	}
	else
	{
		pSelf->GameClient()->Echo("[[green]] Deepfly on");
		str_copy(pSelf->m_Oldmouse1Bind, CurBind, sizeof(CurBind));
		pSelf->GameClient()->m_Binds.Bind(291, "+fire; +toggle cl_dummy_hammer 1 0", false, 0);
	}
}

void CBestClient::ConToggleCinematicCamera(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	CBestClient *pSelf = static_cast<CBestClient *>(pUserData);
	g_Config.m_BcCinematicCamera = !g_Config.m_BcCinematicCamera;
	pSelf->GameClient()->Echo(g_Config.m_BcCinematicCamera ? "[[green]] Cinematic camera on" : "[[red]] Cinematic camera off");
}


void CBestClient::ConRandomTee(IConsole::IResult *pResult, void *pUserData) {}

void CBestClient::ConchainRandomColor(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CBestClient *pThis = static_cast<CBestClient *>(pUserData);
	// Resolve type to randomize
	// Check length of type (0 = all, 1 = body, 2 = feet, 3 = skin, 4 = flag)
	bool RandomizeBody = false;
	bool RandomizeFeet = false;
	bool RandomizeSkin = false;
	bool RandomizeFlag = false;

	if(pResult->NumArguments() == 0)
	{
		RandomizeBody = true;
		RandomizeFeet = true;
		RandomizeSkin = true;
		RandomizeFlag = true;
	}
	else if(pResult->NumArguments() == 1)
	{
		const char *Type = pResult->GetString(0);
		int Length = Type ? str_length(Type) : 0;
		if(Length == 1 && Type[0] == '0')
		{ // Randomize all
			RandomizeBody = true;
			RandomizeFeet = true;
			RandomizeSkin = true;
			RandomizeFlag = true;
		}
		else if(Length == 1)
		{
			// Randomize body
			RandomizeBody = Type[0] == '1';
		}
		else if(Length == 2)
		{
			// Check for body and feet
			RandomizeBody = Type[0] == '1';
			RandomizeFeet = Type[1] == '1';
		}
		else if(Length == 3)
		{
			// Check for body, feet and skin
			RandomizeBody = Type[0] == '1';
			RandomizeFeet = Type[1] == '1';
			RandomizeSkin = Type[2] == '1';
		}
		else if(Length == 4)
		{
			// Check for body, feet, skin and flag
			RandomizeBody = Type[0] == '1';
			RandomizeFeet = Type[1] == '1';
			RandomizeSkin = Type[2] == '1';
			RandomizeFlag = Type[3] == '1';
		}
	}

	if(RandomizeBody)
		RandomBodyColor();
	if(RandomizeFeet)
		RandomFeetColor();
	if(RandomizeSkin)
		RandomSkin(pUserData);
	if(RandomizeFlag)
		RandomFlag(pUserData);
	pThis->GameClient()->SendInfo(false);
}

void CBestClient::OnInit()
{
	m_pGraphics = Kernel()->RequestInterface<IEngineGraphics>();
	LoadHookComboSounds();
	ResetHookComboState();
	FetchBestClientInfo();

	char aError[512] = "";
	if(!Storage()->FileExists("BestClient/gui_logo.png", IStorage::TYPE_ALL))
		str_format(aError, sizeof(aError), TCLocalize("%s not found", DATA_VERSION_PATH), "data/BestClient/gui_logo.png");
	if(aError[0] == '\0')
		CheckDataVersion(aError, sizeof(aError), Storage()->OpenFile(DATA_VERSION_PATH, IOFLAG_READ, IStorage::TYPE_ALL));
	if(aError[0] != '\0')
	{
		SWarning Warning(aError, TCLocalize("You have probably only installed the BestClient DDNet.exe which is not supported, please use the entire BestClient folder", "data_version.h"));
		Client()->AddWarning(Warning);
	}
}

static bool LineShouldHighlight(const char *pLine, const char *pName)
{
	const char *pHL = str_utf8_find_nocase(pLine, pName);
	if(pHL)
	{
		int Length = str_length(pName);
		if(Length > 0 && (pLine == pHL || pHL[-1] == ' ') && (pHL[Length] == 0 || pHL[Length] == ' ' || pHL[Length] == '.' || pHL[Length] == '!' || pHL[Length] == ',' || pHL[Length] == '?' || pHL[Length] == ':'))
			return true;
	}
	return false;
}

bool CBestClient::SendNonDuplicateMessage(int Team, const char *pLine)
{
	if(str_comp(pLine, m_PreviousOwnMessage) != 0)
	{
		GameClient()->m_Chat.SendChat(Team, pLine);
		return true;
	}
	str_copy(m_PreviousOwnMessage, pLine);
	return false;
}

void CBestClient::OnMessage(int MsgType, void *pRawMsg)
{
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return;

	if(MsgType == NETMSGTYPE_SV_CHAT)
	{
		CNetMsg_Sv_Chat *pMsg = (CNetMsg_Sv_Chat *)pRawMsg;
		int ClientId = pMsg->m_ClientId;

		if(ClientId < 0 || ClientId > MAX_CLIENTS)
			return;
		int LocalId = GameClient()->m_Snap.m_LocalClientId;
		if(ClientId == LocalId)
			str_copy(m_PreviousOwnMessage, pMsg->m_pMessage);

		bool PingMessage = false;

		bool ValidIds = !(GameClient()->m_aLocalIds[0] < 0 || (GameClient()->Client()->DummyConnected() && GameClient()->m_aLocalIds[1] < 0));

		if(ValidIds && ClientId >= 0 && ClientId != GameClient()->m_aLocalIds[0] && (!GameClient()->Client()->DummyConnected() || ClientId != GameClient()->m_aLocalIds[1]))
		{
			PingMessage |= LineShouldHighlight(pMsg->m_pMessage, GameClient()->m_aClients[GameClient()->m_aLocalIds[0]].m_aName);
			PingMessage |= GameClient()->Client()->DummyConnected() && LineShouldHighlight(pMsg->m_pMessage, GameClient()->m_aClients[GameClient()->m_aLocalIds[1]].m_aName);
		}

		if(pMsg->m_Team == TEAM_WHISPER_RECV)
			PingMessage = true;

		if(!PingMessage)
			return;

	}

	auto &vServerCommands = GameClient()->m_Chat.m_vServerCommands;
	auto AddSpecId = [&](bool Enable) {
		static const CChat::CCommand SpecId("specid", "v[id]", "Spectate a player");
		vServerCommands.erase(std::remove_if(vServerCommands.begin(), vServerCommands.end(), [](const CChat::CCommand &Command) { return Command == SpecId; }), vServerCommands.end());
		if(Enable)
			vServerCommands.push_back(SpecId);
		GameClient()->m_Chat.m_ServerCommandsNeedSorting = true;
	};
	if(MsgType == NETMSGTYPE_SV_COMMANDINFO)
	{
		CNetMsg_Sv_CommandInfo *pMsg = (CNetMsg_Sv_CommandInfo *)pRawMsg;
		if(str_comp_nocase(pMsg->m_pName, "spec") == 0)
			AddSpecId(!ServerCommandExists("specid"));
		else if(str_comp_nocase(pMsg->m_pName, "specid") == 0)
			AddSpecId(false);
		return;
	}
	if(MsgType == NETMSGTYPE_SV_COMMANDINFOREMOVE)
	{
		CNetMsg_Sv_CommandInfoRemove *pMsg = (CNetMsg_Sv_CommandInfoRemove *)pRawMsg;
		if(str_comp_nocase(pMsg->m_pName, "spec") == 0)
			AddSpecId(false);
		else if(str_comp_nocase(pMsg->m_pName, "specid") == 0)
			AddSpecId(ServerCommandExists("spec"));
		return;
	}
}

void CBestClient::ConSpecId(IConsole::IResult *pResult, void *pUserData)
{
	((CBestClient *)pUserData)->SpecId(pResult->GetInteger(0));
}

bool CBestClient::ChatDoSpecId(const char *pInput)
{
	const char *pNumber = str_startswith_nocase(pInput, "/specid ");
	if(!pNumber)
		return false;

	const int Length = str_length(pInput);
	CChat::CHistoryEntry *pEntry = GameClient()->m_Chat.m_History.Allocate(sizeof(CChat::CHistoryEntry) + Length);
	pEntry->m_Team = 0;
	str_copy(pEntry->m_aText, pInput, Length + 1);

	int ClientId = 0;
	if(!str_toint(pNumber, &ClientId))
		return true;

	SpecId(ClientId);
	return true;
}

void CBestClient::SpecId(int ClientId)
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(Client()->State() == IClient::STATE_DEMOPLAYBACK || GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		GameClient()->m_Spectator.Spectate(ClientId);
		return;
	}

	if(ClientId < 0 || ClientId > (int)std::size(GameClient()->m_aClients))
		return;
	const auto &Player = GameClient()->m_aClients[ClientId];
	if(!Player.m_Active)
		return;
	char aBuf[256];
	str_copy(aBuf, "/spec \"");
	char *pDst = aBuf + strlen(aBuf);
	str_escape(&pDst, Player.m_aName, aBuf + sizeof(aBuf));
	str_append(aBuf, "\"");
	GameClient()->m_Chat.SendChat(0, aBuf);
}

void CBestClient::ConEmoteCycle(IConsole::IResult *pResult, void *pUserData)
{
	CBestClient &This = *(CBestClient *)pUserData;
	This.m_EmoteCycle += 1;
	if(This.m_EmoteCycle > 15)
		This.m_EmoteCycle = 0;
	This.GameClient()->m_Emoticon.Emote(This.m_EmoteCycle);
}

void CBestClient::AirRescue()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;
	const int ClientId = GameClient()->m_Snap.m_LocalClientId;
	if(ClientId < 0 || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
		return;
	if(GameClient()->m_Snap.m_aCharacters[ClientId].m_HasExtendedDisplayInfo && (GameClient()->m_Snap.m_aCharacters[ClientId].m_ExtendedData.m_Flags & CHARACTERFLAG_PRACTICE_MODE) == 0)
	{
		GameClient()->Echo("You are not in practice");
		return;
	}

	auto IsIndexAirLike = [&](int Index) {
		const auto Tile = Collision()->GetTileIndex(Index);
		return Tile == TILE_AIR || Tile == TILE_UNFREEZE || Tile == TILE_DUNFREEZE;
	};
	auto IsPosAirLike = [&](vec2 Pos) {
		const int Index = Collision()->GetPureMapIndex(Pos);
		return IsIndexAirLike(Index);
	};
	auto IsRadiusAirLike = [&](vec2 Pos, int Radius) {
		for(int y = -Radius; y <= Radius; ++y)
			for(int x = -Radius; x <= Radius; ++x)
				if(!IsPosAirLike(Pos + vec2(x, y) * 32.0f))
					return false;
		return true;
	};

	auto &AirRescuePositions = m_aAirRescuePositions[g_Config.m_ClDummy];
	while(!AirRescuePositions.empty())
	{
		// Get latest pos from positions
		const vec2 NewPos = AirRescuePositions.front();
		AirRescuePositions.pop_front();
		// Check for safety
		if(!IsRadiusAirLike(NewPos, 2))
			continue;
		// Do it
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "/tpxy %f %f", NewPos.x / 32.0f, NewPos.y / 32.0f);
		GameClient()->m_Chat.SendChat(0, aBuf);
		return;
	}

	GameClient()->Echo("No safe position found");
}

void CBestClient::ConAirRescue(IConsole::IResult *pResult, void *pUserData)
{
	((CBestClient *)pUserData)->AirRescue();
}

void CBestClient::ConCalc(IConsole::IResult *pResult, void *pUserData)
{
	int Error = 0;
	double Out = te_interp(pResult->GetString(0), &Error);
	if(Out == NAN || Error != 0)
		log_info("BestClient", "Calc error: %d", Error);
	else
		log_info("BestClient", "Calc result: %lf", Out);
}

void CBestClient::OnConsoleInit()
{
	Console()->Register("+BC_45_degrees", "", CFGFLAG_CLIENT, ConToggle45Degrees, this, "45° bind");
	Console()->Register("+BC_small_sens", "", CFGFLAG_CLIENT, ConToggleSmallSens, this, "Small sens bind");
	Console()->Register("BC_deepfly_toggle", "", CFGFLAG_CLIENT, ConToggleDeepfly, this, "Deep fly toggle");
	Console()->Register("BC_cinematic_camera_toggle", "", CFGFLAG_CLIENT, ConToggleCinematicCamera, this, "Toggle cinematic spectator camera");

	Console()->Chain(
		"bc_custom_aspect_ratio", [](IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
			pfnCallback(pResult, pCallbackUserData);
			((CBestClient *)pUserData)->ReloadWindowModeForAspect();
		},
		this);
	Console()->Chain(
		"bc_custom_aspect_ratio_mode", [](IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
			pfnCallback(pResult, pCallbackUserData);
			((CBestClient *)pUserData)->ReloadWindowModeForAspect();
		},
		this);

}

void CBestClient::RandomBodyColor()
{
	g_Config.m_ClPlayerColorBody = ColorHSLA((std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, 1.0f).Pack(false);
}

void CBestClient::RandomFeetColor()
{
	g_Config.m_ClPlayerColorFeet = ColorHSLA((std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, 1.0f).Pack(false);
}

void CBestClient::RandomSkin(void *pUserData)
{
	CBestClient *pThis = static_cast<CBestClient *>(pUserData);
	const auto &Skins = pThis->GameClient()->m_Skins.SkinList().Skins();
	str_copy(g_Config.m_ClPlayerSkin, Skins[std::rand() % (int)Skins.size()].SkinContainer()->Name());
}

void CBestClient::RandomFlag(void *pUserData)
{
	CBestClient *pThis = static_cast<CBestClient *>(pUserData);
	// get the flag count
	int FlagCount = pThis->GameClient()->m_CountryFlags.Num();

	// get a random flag number
	int FlagNumber = std::rand() % FlagCount;

	// get the flag name
	const CCountryFlags::CCountryFlag &Flag = pThis->GameClient()->m_CountryFlags.GetByIndex(FlagNumber);

	// set the flag code as number
	g_Config.m_PlayerCountry = Flag.m_CountryCode;
}

void CBestClient::UpdateAutoTeamLock()
{
	if(IsComponentDisabled(COMPONENT_RACE_AUTO_TEAM_LOCK))
	{
		for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
		{
			m_aAutoTeamLockLastTeam[Dummy] = TEAM_FLOCK;
			m_aAutoTeamLockDeadlineTick[Dummy] = 0;
			m_aAutoTeamLockPending[Dummy] = false;
		}
		return;
	}

	if(Client()->State() != IClient::STATE_ONLINE)
	{
		for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
		{
			m_aAutoTeamLockLastTeam[Dummy] = TEAM_FLOCK;
			m_aAutoTeamLockDeadlineTick[Dummy] = 0;
			m_aAutoTeamLockPending[Dummy] = false;
		}
		return;
	}

	const int Dummy = g_Config.m_ClDummy;
	const int ClientId = GameClient()->m_aLocalIds[Dummy];

	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
	{
		m_aAutoTeamLockLastTeam[Dummy] = TEAM_FLOCK;
		m_aAutoTeamLockDeadlineTick[Dummy] = 0;
		m_aAutoTeamLockPending[Dummy] = false;
		return;
	}

	const int Team = GameClient()->m_Teams.Team(ClientId);
	const bool TeamCanBeLocked = Team > TEAM_FLOCK && Team < TEAM_SUPER;
	const bool LastTeamCanBeLocked = m_aAutoTeamLockLastTeam[Dummy] > TEAM_FLOCK && m_aAutoTeamLockLastTeam[Dummy] < TEAM_SUPER;

	if(!g_Config.m_BcAutoTeamLock)
	{
		m_aAutoTeamLockLastTeam[Dummy] = Team;
		m_aAutoTeamLockDeadlineTick[Dummy] = 0;
		m_aAutoTeamLockPending[Dummy] = false;
		return;
	}

	if(TeamCanBeLocked && (!LastTeamCanBeLocked || Team != m_aAutoTeamLockLastTeam[Dummy]))
	{
		const int DelayTicks = g_Config.m_BcAutoTeamLockDelay * Client()->GameTickSpeed();
		m_aAutoTeamLockDeadlineTick[Dummy] = Client()->GameTick(Dummy) + DelayTicks;
		m_aAutoTeamLockPending[Dummy] = true;
	}
	else if(!TeamCanBeLocked)
	{
		m_aAutoTeamLockDeadlineTick[Dummy] = 0;
		m_aAutoTeamLockPending[Dummy] = false;
	}

	if(m_aAutoTeamLockPending[Dummy] && TeamCanBeLocked && Client()->GameTick(Dummy) >= m_aAutoTeamLockDeadlineTick[Dummy])
	{
		GameClient()->m_Chat.SendChat(0, "/lock 1");
		m_aAutoTeamLockPending[Dummy] = false;
	}

	m_aAutoTeamLockLastTeam[Dummy] = Team;
}

void CBestClient::SendChatCommandToConn(int Conn, const char *pLine)
{
	if(*str_utf8_skip_whitespaces(pLine) == '\0')
		return;

	if(Client()->IsSixup())
	{
		protocol7::CNetMsg_Cl_Say Msg7;
		Msg7.m_Mode = protocol7::CHAT_ALL;
		Msg7.m_Target = -1;
		Msg7.m_pMessage = pLine;
		Client()->SendPackMsg(Conn, &Msg7, MSGFLAG_VITAL, true);
		return;
	}

	CNetMsg_Cl_Say Msg;
	Msg.m_Team = 0;
	Msg.m_pMessage = pLine;
	Client()->SendPackMsg(Conn, &Msg, MSGFLAG_VITAL);
}

void CBestClient::UpdateAutoDummyJoin()
{
	if(IsComponentDisabled(COMPONENT_RACE_AUTO_TEAM_LOCK))
	{
		for(int Conn = 0; Conn < NUM_DUMMIES; ++Conn)
		{
			m_aAutoDummyJoinLastTeam[Conn] = TEAM_FLOCK;
			m_aAutoDummyJoinPendingTeam[Conn] = TEAM_FLOCK;
			m_aAutoDummyJoinRetryTick[Conn] = 0;
		}
		return;
	}

	if(Client()->State() != IClient::STATE_ONLINE || !Client()->DummyConnected())
	{
		for(int Conn = 0; Conn < NUM_DUMMIES; ++Conn)
		{
			m_aAutoDummyJoinLastTeam[Conn] = TEAM_FLOCK;
			m_aAutoDummyJoinPendingTeam[Conn] = TEAM_FLOCK;
			m_aAutoDummyJoinRetryTick[Conn] = 0;
		}
		return;
	}

	int aTeam[NUM_DUMMIES] = {TEAM_FLOCK, TEAM_FLOCK};
	for(int Conn = 0; Conn < NUM_DUMMIES; ++Conn)
	{
		const int ClientId = GameClient()->m_aLocalIds[Conn];
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		{
			m_aAutoDummyJoinLastTeam[Conn] = TEAM_FLOCK;
			m_aAutoDummyJoinPendingTeam[Conn] = TEAM_FLOCK;
			m_aAutoDummyJoinRetryTick[Conn] = 0;
			return;
		}
		aTeam[Conn] = GameClient()->m_Teams.Team(ClientId);
	}

	if(!g_Config.m_BcAutoDummyJoin)
	{
		for(int Conn = 0; Conn < NUM_DUMMIES; ++Conn)
		{
			m_aAutoDummyJoinLastTeam[Conn] = aTeam[Conn];
			m_aAutoDummyJoinPendingTeam[Conn] = TEAM_FLOCK;
			m_aAutoDummyJoinRetryTick[Conn] = 0;
		}
		return;
	}

	bool aJoinable[NUM_DUMMIES] = {false, false};
	for(int Conn = 0; Conn < NUM_DUMMIES; ++Conn)
		aJoinable[Conn] = aTeam[Conn] > TEAM_FLOCK && aTeam[Conn] < TEAM_SUPER;

	const bool ChangedMain = aTeam[IClient::CONN_MAIN] != m_aAutoDummyJoinLastTeam[IClient::CONN_MAIN];
	const bool ChangedDummy = aTeam[IClient::CONN_DUMMY] != m_aAutoDummyJoinLastTeam[IClient::CONN_DUMMY];
	const int ActiveConn = g_Config.m_ClDummy ? IClient::CONN_DUMMY : IClient::CONN_MAIN;
	const int InactiveConn = ActiveConn == IClient::CONN_MAIN ? IClient::CONN_DUMMY : IClient::CONN_MAIN;

	int SourceConn = -1;
	if(ChangedMain != ChangedDummy)
	{
		const int ChangedConn = ChangedMain ? IClient::CONN_MAIN : IClient::CONN_DUMMY;
		if(aJoinable[ChangedConn])
			SourceConn = ChangedConn;
	}
	else if(aJoinable[ActiveConn])
	{
		SourceConn = ActiveConn;
	}
	else if(aJoinable[InactiveConn])
	{
		SourceConn = InactiveConn;
	}

	if(SourceConn != -1)
	{
		const int TargetConn = SourceConn == IClient::CONN_MAIN ? IClient::CONN_DUMMY : IClient::CONN_MAIN;
		if(aJoinable[SourceConn] && aTeam[TargetConn] != aTeam[SourceConn])
		{
			const int64_t Tick = Client()->GameTick(TargetConn);
			if(m_aAutoDummyJoinPendingTeam[TargetConn] != aTeam[SourceConn] || Tick >= m_aAutoDummyJoinRetryTick[TargetConn])
			{
				char aCmd[32];
				str_format(aCmd, sizeof(aCmd), "/team %d", aTeam[SourceConn]);
				SendChatCommandToConn(TargetConn, aCmd);
				m_aAutoDummyJoinPendingTeam[TargetConn] = aTeam[SourceConn];
				m_aAutoDummyJoinRetryTick[TargetConn] = Tick + Client()->GameTickSpeed();
			}
		}
	}

	for(int Conn = 0; Conn < NUM_DUMMIES; ++Conn)
	{
		if(aTeam[Conn] == m_aAutoDummyJoinPendingTeam[Conn])
		{
			m_aAutoDummyJoinPendingTeam[Conn] = TEAM_FLOCK;
			m_aAutoDummyJoinRetryTick[Conn] = 0;
		}
		m_aAutoDummyJoinLastTeam[Conn] = aTeam[Conn];
	}
}

void CBestClient::OnShutdown()
{
	ResetHookComboState();
	UnloadHookComboSounds();
}

void CBestClient::TriggerHookComboStep()
{
	m_HookComboCounter++;

	SHookComboPopup Popup;
	Popup.m_Sequence = m_HookComboCounter;
	Popup.m_Age = 0.0f;
	m_vHookComboPopups.push_back(Popup);
	if(m_vHookComboPopups.size() > 16)
		m_vHookComboPopups.erase(m_vHookComboPopups.begin());

	if(!GameClient()->m_SuppressEvents && g_Config.m_SndEnable)
	{
		int SoundIndex = 0;
		if(m_HookComboCounter > s_HookComboVariantLimit)
			SoundIndex = s_HookComboBrilliantSoundIndex;
		else if(m_HookComboCounter <= s_HookComboSoundCount)
			SoundIndex = m_HookComboCounter - 1;
		else
			SoundIndex = (m_HookComboCounter - 1) % s_HookComboSoundCount;

		int SoundId = m_aHookComboSoundIds[SoundIndex];
		if(SoundId == -1)
		{
			// Retry at runtime, because startup path/audio init may differ from gameplay runtime.
			LoadHookComboSounds(false);
			SoundId = m_aHookComboSoundIds[SoundIndex];
		}
		if(SoundId != -1)
		{
			const float GameVol = (g_Config.m_SndGame && g_Config.m_SndGameVolume > 0) ? (float)g_Config.m_SndGameVolume : 0.0f;
			const float ChatVol = (g_Config.m_SndChat && g_Config.m_SndChatVolume > 0) ? (float)g_Config.m_SndChatVolume : 0.0f;
			const int Channel = GameVol >= ChatVol ? CSounds::CHN_GLOBAL : CSounds::CHN_GUI;
			const float ComboVol = std::clamp(g_Config.m_BcHookComboSoundVolume / 100.0f, 0.0f, 1.0f);
			if(ComboVol > 0.0f)
				Sound()->Play(Channel, SoundId, 0, ComboVol);
		}
		else if(!m_HookComboSoundErrorShown)
		{
			m_HookComboSoundErrorShown = true;
			GameClient()->Echo("[[red]] Hook combo sounds not found. Put files as data/audio/combo1.wv ... combo7.wv");
		}
	}
}

void CBestClient::UpdateHookCombo()
{
	constexpr float PopupLifetime = 1.1f;
	const float FrameTime = Client()->RenderFrameTime();
	for(auto &Popup : m_vHookComboPopups)
		Popup.m_Age += FrameTime;
	m_vHookComboPopups.erase(std::remove_if(m_vHookComboPopups.begin(), m_vHookComboPopups.end(), [](const SHookComboPopup &Popup) {
		return Popup.m_Age >= PopupLifetime;
	}),
		m_vHookComboPopups.end());

	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		ResetHookComboState();
		return;
	}

	if(!g_Config.m_BcHookCombo)
	{
		ResetHookComboState();
		return;
	}

	if(GameClient()->m_Snap.m_SpecInfo.m_Active)
		return;

	const int ComboMode = std::clamp(g_Config.m_BcHookComboMode, s_HookComboModeHook, s_HookComboModeHookAndHammer);
	const bool HammerEventFrame = GameClient()->m_aPredictedHammerHitEvent[g_Config.m_ClDummy];
	if(!GameClient()->m_NewPredictedTick && !(HammerEventFrame && ComboMode != s_HookComboModeHook))
		return;

	int LocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	if(LocalId < 0 || LocalId >= MAX_CLIENTS)
		LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId < 0 || LocalId >= MAX_CLIENTS || !GameClient()->m_aClients[LocalId].m_Active)
		return;

	if(LocalId != m_HookComboTrackedClientId)
	{
		m_HookComboTrackedClientId = LocalId;
		m_HookComboLastHookedPlayer = -1;
	}

	const int HookedPlayer = GameClient()->m_aClients[LocalId].m_Predicted.HookedPlayer();
	const bool NewPlayerHook = HookedPlayer >= 0 && (m_HookComboLastHookedPlayer < 0 || HookedPlayer != m_HookComboLastHookedPlayer);
	m_HookComboLastHookedPlayer = HookedPlayer;

	const bool NewHammerAttack = GameClient()->m_aPredictedHammerHitEvent[g_Config.m_ClDummy];

	bool TriggerCombo = false;
	if(ComboMode == s_HookComboModeHook)
		TriggerCombo = NewPlayerHook;
	else if(ComboMode == s_HookComboModeHammer)
		TriggerCombo = NewHammerAttack;
	else
		TriggerCombo = NewPlayerHook || NewHammerAttack;

	if(TriggerCombo)
	{
		const float ResetTime = g_Config.m_BcHookComboResetTime / 1000.0f;
		const float Now = Client()->LocalTime();
		if(m_HookComboLastHookTime >= 0.0f && (Now - m_HookComboLastHookTime) > ResetTime)
			m_HookComboCounter = 0;
		m_HookComboLastHookTime = Now;
		TriggerHookComboStep();
	}
}

bool CBestClient::HasHookComboWork() const
{
	if(IsComponentDisabled(COMPONENT_GORES_HOOK_COMBO))
		return false;
	return g_Config.m_BcHookCombo != 0 || !m_vHookComboPopups.empty();
}

bool CBestClient::HasRenderWork() const
{
	const bool HasFogRectWork = OptimizerFpsFogEnabled() && g_Config.m_BcOptimizerFpsFogRenderRect != 0;
	const bool HasProcessPriorityWork = (OptimizerEnabled() && (g_Config.m_BcOptimizerDdnetPriorityHigh != 0 || g_Config.m_BcOptimizerDiscordPriorityBelowNormal != 0)) ||
		m_OptimizerDdnetPriorityHighActive || m_OptimizerDiscordPriorityBelowNormalActive;
	return m_pBestClientInfoTask != nullptr || HasHookComboWork() || HasFogRectWork || HasProcessPriorityWork;
}

bool CBestClient::ServerCommandExists(const char *pCommand)
{
	for(const auto &Command : GameClient()->m_Chat.m_vServerCommands)
		if(str_comp_nocase(pCommand, Command.m_aName) == 0)
			return true;
	return false;
}

void CBestClient::RenderOptimizerFpsFogRect()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(!OptimizerFpsFogEnabled() || g_Config.m_BcOptimizerFpsFogRenderRect == 0)
		return;

	float HalfW = 0.0f;
	float HalfH = 0.0f;
	OptimizerFpsFogHalfExtents(HalfW, HalfH);
	if(HalfW <= 0.0f || HalfH <= 0.0f)
		return;

	const vec2 Center = GameClient()->m_Camera.m_Center;

	// Ensure world mapping before rendering.
	RenderTools()->MapScreenToGroup(Center.x, Center.y, Layers()->GameGroup(), GameClient()->m_Camera.m_Zoom);

	const vec2 TL{Center.x - HalfW, Center.y - HalfH};
	const vec2 TR{Center.x + HalfW, Center.y - HalfH};
	const vec2 BR{Center.x + HalfW, Center.y + HalfH};
	const vec2 BL{Center.x - HalfW, Center.y + HalfH};

	Graphics()->TextureClear();
	Graphics()->LinesBegin();
	Graphics()->SetColor(1.0f, 0.65f, 0.05f, 0.8f);
	const IGraphics::CLineItem aLines[] = {
		IGraphics::CLineItem(TL.x, TL.y, TR.x, TR.y),
		IGraphics::CLineItem(TR.x, TR.y, BR.x, BR.y),
		IGraphics::CLineItem(BR.x, BR.y, BL.x, BL.y),
		IGraphics::CLineItem(BL.x, BL.y, TL.x, TL.y),
	};
	Graphics()->LinesDraw(aLines, std::size(aLines));
	Graphics()->LinesEnd();
}

void CBestClient::OnRender()
{
	if(!HasRenderWork())
		return;

	if(m_pBestClientInfoTask)
	{
		if(m_pBestClientInfoTask->State() == EHttpState::DONE)
		{
			FinishBestClientInfo();
			ResetBestClientInfoTask();
		}
		else if(m_pBestClientInfoTask->State() == EHttpState::ERROR || m_pBestClientInfoTask->State() == EHttpState::ABORTED)
		{
			ResetBestClientInfoTask();
		}
	}

	if(HasHookComboWork())
		UpdateHookCombo();
	OptimizerUpdateProcessPriorities();
	RenderOptimizerFpsFogRect();
}

bool CBestClient::NeedUpdate()
{
	return str_comp(m_aVersionStr, "0") != 0;
}

void CBestClient::ResetBestClientInfoTask()
{
	if(m_pBestClientInfoTask)
	{
		m_pBestClientInfoTask->Abort();
		m_pBestClientInfoTask = NULL;
	}
}

void CBestClient::FetchBestClientInfo()
{
	if(m_pBestClientInfoTask && !m_pBestClientInfoTask->Done())
		return;
	char aUrl[512];
	BuildBestClientInfoUrl(aUrl, sizeof(aUrl));
	m_pBestClientInfoTask = HttpGet(aUrl);
	m_pBestClientInfoTask->HeaderString("Accept", "application/vnd.github+json");
	m_pBestClientInfoTask->HeaderString("User-Agent", CLIENT_NAME);
	m_pBestClientInfoTask->HeaderString("X-GitHub-Api-Version", "2022-11-28");
	m_pBestClientInfoTask->HeaderString("Cache-Control", "no-cache");
	m_pBestClientInfoTask->HeaderString("Pragma", "no-cache");
	m_pBestClientInfoTask->Timeout(CTimeout{10000, 0, 500, 10});
	m_pBestClientInfoTask->IpResolve(IPRESOLVE::V4);
	Http()->Run(m_pBestClientInfoTask);
}

void CBestClient::FinishBestClientInfo()
{
	json_value *pJson = m_pBestClientInfoTask->ResultJson();
	if(!pJson)
		return;

	const char *pCurrentVersion = FindBestClientReleaseVersion(pJson);

	if(pCurrentVersion && CompareBestClientVersions(pCurrentVersion, BESTCLIENT_VERSION) > 0)
		str_copy(m_aVersionStr, pCurrentVersion, sizeof(m_aVersionStr));
	else
	{
		m_aVersionStr[0] = '0';
		m_aVersionStr[1] = '\0';
	}

	m_FetchedBestClientInfo = true;

	json_value_free(pJson);
}

void CBestClient::SetForcedAspect()
{
	if(!GameClient() || !m_pGraphics)
		return;

	g_Config.m_BcCustomAspectRatioMode = GetConfiguredAspectMode();
	g_Config.m_BcCustomAspectRatio = NormalizeAspectValueForMode(g_Config.m_BcCustomAspectRatioMode, g_Config.m_BcCustomAspectRatio);

	// TODO: Fix flashing on windows
	int State = Client()->State();
	bool Force = true;
	if(g_Config.m_TcAllowAnyRes == 0)
		;
	else if(State == CClient::EClientState::STATE_DEMOPLAYBACK)
		Force = false;
	else if(State == CClient::EClientState::STATE_ONLINE && GameClient()->m_GameInfo.m_AllowZoom && !GameClient()->m_Menus.IsActive())
		Force = false;
	m_pGraphics->SetForcedAspect(Force);
}

void CBestClient::ReloadWindowModeForAspect()
{
	SetForcedAspect();
}

void CBestClient::OnStateChange(int OldState, int NewState)
{
	SetForcedAspect();
	ResetHookComboState();
	for(auto &AirRescuePositions : m_aAirRescuePositions)
		AirRescuePositions = {};
	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		m_aAutoTeamLockLastTeam[Dummy] = TEAM_FLOCK;
		m_aAutoTeamLockDeadlineTick[Dummy] = 0;
		m_aAutoTeamLockPending[Dummy] = false;
		m_aAutoDummyJoinLastTeam[Dummy] = TEAM_FLOCK;
		m_aAutoDummyJoinPendingTeam[Dummy] = TEAM_FLOCK;
		m_aAutoDummyJoinRetryTick[Dummy] = 0;
	}
}

void CBestClient::OnNewSnapshot()
{
	SetForcedAspect();
	UpdateAutoTeamLock();
	UpdateAutoDummyJoin();
	const bool IsOnline = Client()->State() == IClient::STATE_ONLINE;
	const bool Is0xFServer = IsOnline && str_comp_nocase(GameClient()->m_GameInfo.m_aGameType, "0xf") == 0;
	if(IsOnline && (GameClient()->m_GameInfo.m_PredictFNG || Is0xFServer))
	{
		if(g_Config.m_BcCameraDrift)
			g_Config.m_BcCameraDrift = 0;
		if(g_Config.m_BcDynamicFov)
			g_Config.m_BcDynamicFov = 0;
	}
	// Update volleyball
	bool IsVolleyBall = false;
	if(g_Config.m_TcVolleyBallBetterBall > 0 && g_Config.m_TcVolleyBallBetterBallSkin[0] != '\0')
	{
		if(g_Config.m_TcVolleyBallBetterBall > 1)
			IsVolleyBall = true;
		else
				IsVolleyBall = str_startswith_nocase(Client()->MapDownloadName(), "volleyball");
	};
	for(auto &Client : GameClient()->m_aClients)
	{
		Client.m_IsVolleyBall = IsVolleyBall && Client.m_DeepFrozen;
	}
	// Update air rescue
	if(Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
		{
			const int ClientId = GameClient()->m_aLocalIds[Dummy];
			if(ClientId == -1)
				continue;
			const auto &Char = GameClient()->m_Snap.m_aCharacters[ClientId];
			if(!Char.m_Active)
				continue;
			if(Client()->GameTick(Dummy) % 10 != 0) // Works for both 25tps and 50tps
				continue;
			const auto &Client = GameClient()->m_aClients[ClientId];
			if(Client.m_FreezeEnd == -1) // You aren't safe when frozen
				continue;
			const vec2 NewPos = vec2(Char.m_Cur.m_X, Char.m_Cur.m_Y);
			// If new pos is under 2 tiles from old pos, don't record a new position
			if(!m_aAirRescuePositions[Dummy].empty())
			{
				const vec2 OldPos = m_aAirRescuePositions[Dummy].front();
				if(distance(NewPos, OldPos) < 64.0f)
					continue;
			}
			if(m_aAirRescuePositions[Dummy].size() >= 256)
				m_aAirRescuePositions[Dummy].pop_back();
			m_aAirRescuePositions[Dummy].push_front(NewPos);
		}
	}
}

void CBestClient::RenderHookCombo(bool ForcePreview)
{
	if(!ForcePreview && IsComponentDisabled(COMPONENT_GORES_HOOK_COMBO))
		return;

	if(!ForcePreview && (!g_Config.m_BcHookCombo || m_vHookComboPopups.empty()))
		return;
	if(GameClient()->m_Scoreboard.IsActive() || (GameClient()->m_Menus.IsActive() && !ForcePreview))
		return;

	constexpr float PopupLifetime = 1.1f;
	constexpr float FadeIn = 0.15f;
	constexpr float FadeOut = 0.25f;

	const float Width = 300.0f * Graphics()->ScreenAspect();
	const float Height = HudLayout::CANVAS_HEIGHT;
	const auto Layout = HudLayout::Get(HudLayout::MODULE_HOOK_COMBO, Width, Height);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	const bool BackgroundEnabled = Layout.m_BackgroundEnabled;
	const ColorRGBA BackgroundColor = color_cast<ColorRGBA>(ColorHSLA(Layout.m_BackgroundColor, true));
	const float AnchorCenterX = Width * 0.5f;
	const float BaseY = Height * 0.84f;
	const float StackStep = 14.0f * Scale;

	int Stack = 0;
	SHookComboPopup PreviewPopup;
	PreviewPopup.m_Sequence = 7;
	PreviewPopup.m_Age = PopupLifetime * 0.35f;

	auto RenderPopup = [&](const SHookComboPopup &Popup) {
		const float Age = std::clamp(Popup.m_Age, 0.0f, PopupLifetime);
		const float In = std::clamp(Age / FadeIn, 0.0f, 1.0f);
		const float Out = Age > PopupLifetime - FadeOut ? std::clamp((PopupLifetime - Age) / FadeOut, 0.0f, 1.0f) : 1.0f;
		const float Alpha = ForcePreview ? 1.0f : In * Out;
		if(Alpha <= 0.0f)
			return;

		const int Sequence = maximum(Popup.m_Sequence, 1);
		const int ColorIndex = (Sequence - 1) % s_HookComboBaseTextCount;

		char aText[64];
		FormatHookComboText(Sequence, aText, sizeof(aText));
		char aBuf[96];
		str_format(aBuf, sizeof(aBuf), "%s (x%d)", aText, Sequence);

		ColorRGBA TextColor = s_aHookComboColors[ColorIndex];
		TextColor.a *= Alpha;
		TextRender()->TextColor(TextColor);

		const float FontSize = (ForcePreview ? 13.0f : (11.0f + In * 2.0f)) * Scale;
		const float TextWidth = TextRender()->TextWidth(FontSize, aBuf, -1, -1.0f);
		const float BoxWidth = TextWidth + 8.0f * Scale;
		const float BoxHeight = FontSize + 4.0f * Scale;
		const float Intro = 1.0f - (1.0f - In) * (1.0f - In);
		const float Rise = ForcePreview ? 0.0f : (20.0f * Intro + Age * 10.0f) * Scale;
		const float RectX = std::clamp(AnchorCenterX - BoxWidth * 0.5f, 0.0f, maximum(0.0f, Width - BoxWidth));
		const float RectY = std::clamp(ForcePreview ? BaseY : (BaseY + (1.0f - In) * 12.0f * Scale - Stack * StackStep - Rise), 0.0f, maximum(0.0f, Height - BoxHeight));
		if(BackgroundEnabled)
		{
			ColorRGBA BgColor = BackgroundColor;
			BgColor.a *= Alpha;
			const int Corners = HudLayout::BackgroundCorners(IGraphics::CORNER_ALL, RectX, RectY, BoxWidth, BoxHeight, Width, Height);
			Graphics()->DrawRect(RectX, RectY, BoxWidth, BoxHeight, BgColor, Corners, 4.0f * Scale);
		}
		TextRender()->Text(RectX + 4.0f * Scale, RectY + 2.0f * Scale, FontSize, aBuf, -1.0f);
	};

	if(ForcePreview)
	{
		RenderPopup(PreviewPopup);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		return;
	}

	for(auto It = m_vHookComboPopups.rbegin(); It != m_vHookComboPopups.rend(); ++It, ++Stack)
		RenderPopup(*It);

	TextRender()->TextColor(TextRender()->DefaultTextColor());
}
