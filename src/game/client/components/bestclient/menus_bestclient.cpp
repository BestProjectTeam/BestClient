#include <base/fs.h>
#include <base/io.h>
#include <base/log.h>
#include <base/math.h>
#include <base/process.h>
#include <base/system.h>

#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/localization.h>
#include <engine/shared/protocol7.h>
#include <engine/storage.h>
#include <engine/textrender.h>
#include <engine/updater.h>

#include <generated/protocol.h>

#include <game/client/animstate.h>
#include <game/client/bc_ui_animations.h>
#include <game/client/components/chat.h>
#include <game/client/components/hud_layout.h>
#include <game/client/components/media_decoder.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/menus.h>
#include <game/client/components/sounds.h>
#include <game/client/gameclient.h>
#include <game/client/skin.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#if defined(CONF_FAMILY_WINDOWS)
#include "reshade_runtime.h"
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std::chrono_literals;

static void SetBestClientTabFlag(int32_t &Flags, int Tab, bool Hidden)
{
	if(Hidden)
		Flags |= (1 << Tab);
	else
		Flags &= ~(1 << Tab);
}

static bool IsBestClientTabFlagSet(int32_t Flags, int Tab)
{
	return (Flags & (1 << Tab)) != 0;
}

static int MusicPlayerVisualizerRoundingPreset(int RoundingPercent)
{
	if(RoundingPercent < 100)
		return 0;
	if(RoundingPercent < 300)
		return 1;
	return 2;
}

[[maybe_unused]] static void RenderSettingsBestClientReShadeUnsupported(CUi *pUi, CUIRect MainView)
{
	CUIRect Content, Line;
	MainView.Margin(32.0f, &Content);

	const float MessageHeight = 58.0f;
	const float TopPadding = maximum((Content.h - MessageHeight) * 0.5f, 0.0f);
	Content.HSplitTop(TopPadding, nullptr, &Content);

	Content.HSplitTop(24.0f, &Line, &Content);
	pUi->DoLabel(&Line, "sorry", 24.0f, TEXTALIGN_MC);

	Content.HSplitTop(10.0f, nullptr, &Content);
	Content.HSplitTop(24.0f, &Line, &Content);
	pUi->DoLabel(&Line, "your system doesn't have reshade support.", 14.0f, TEXTALIGN_MC);
}

#if defined(CONF_FAMILY_WINDOWS)
static constexpr const char *gs_pBestClientReShadeFolderPath = "data/reshade";
static constexpr const char *gs_pBestClientReShadePresetPath = "ReShadePreset.ini";
static constexpr const char *gs_pBestClientReShadeBridgeStatePath = "BestClientReShadeBridge.ini";
static constexpr const char *gs_pBestClientReShadeShadersPath = "data/reshade/Shaders";
static constexpr const char *gs_pBestClientReShadeSettingsPath = "settings_reshade.cfg";
static constexpr const char *gs_pBestClientReShadeLayerDllFilename = "ReShade64.dll";
static constexpr const char *gs_pBestClientReShadeLayerManifestFilename = "ReShade64.json";
static constexpr const char *gs_pBestClientReShadeLayerDisabledManifestFilename = "ReShade64.reshade-disabled.json";

enum class EBestClientReShadeUniformType
{
	BOOL = 0,
	INT,
	UINT,
	FLOAT,
	FLOAT2,
	FLOAT3,
	FLOAT4,
};

struct SBestClientReShadeUniformMeta
{
	std::string m_Name;
	std::string m_Label;
	std::string m_DefaultValue;
	EBestClientReShadeUniformType m_Type = EBestClientReShadeUniformType::FLOAT;
	int m_NumComponents = 1;
	float m_Min = 0.0f;
	float m_Max = 1.0f;
	bool m_HasMin = false;
	bool m_HasMax = false;
	bool m_IsColor = false;
	bool m_HasAlpha = false;
	std::vector<std::string> m_vComboItems;
};

struct SBestClientReShadeTechniqueMeta
{
	std::string m_Token;
	std::string m_TechniqueName;
	std::string m_EffectName;
};

enum EBestClientReShadeTechniqueSort
{
	BESTCLIENT_RESHADE_SORT_NAME_ASC = 0,
	BESTCLIENT_RESHADE_SORT_NAME_DESC,
	BESTCLIENT_RESHADE_SORT_EFFECT_ASC,
	NUM_BESTCLIENT_RESHADE_SORTS,
};

struct SBestClientReShadePresetState
{
	std::vector<std::string> m_vTechniqueSorting;
	std::unordered_set<std::string> m_EnabledTokens;
	std::unordered_map<std::string, std::unordered_map<std::string, std::string>> m_SectionValues;
	bool m_HasTechniqueSorting = false;
	bool m_HasEnabledState = false;
};

struct SBestClientReShadeUiCache
{
	std::unordered_map<std::string, std::string> m_EffectPaths;
	std::unordered_map<std::string, std::vector<SBestClientReShadeUniformMeta>> m_UniformsByEffect;
	std::vector<SBestClientReShadeTechniqueMeta> m_vTechniqueIndex;
	char m_aPresetPath[IO_MAX_PATH_LENGTH] = "";
	char m_aSettingsPath[IO_MAX_PATH_LENGTH] = "";
	time_t m_PresetModifiedTime = 0;
	time_t m_SettingsModifiedTime = 0;
	bool m_HasEffectIndex = false;
	bool m_HasTechniqueIndex = false;
	int64_t m_EffectIndexRetryTime = 0;
	int64_t m_TechniqueIndexRetryTime = 0;
	bool m_HasPresetCache = false;
	SBestClientReShadePresetState m_PresetState;
	std::string m_StatusText;
	bool m_StatusIsError = false;
};

static const std::vector<SBestClientReShadeUniformMeta> &BestClientGetReShadeUniformMetadata(IStorage *pStorage, const std::string &EffectName);
static std::unordered_set<std::string> BestClientBuildTrackedReShadeEffectSet(const SBestClientReShadePresetState &PresetState);

static SBestClientReShadeUiCache gs_BestClientReShadeUiCache;

static bool gs_ReShadeEffectsMuted = false;
static bool gs_ReShadeTogglePending = false;

void BestClientTriggerReShadeToggle()
{
	gs_ReShadeTogglePending = true;
}

static std::string BestClientTrimString(std::string Text)
{
	const auto IsWhitespace = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
	while(!Text.empty() && IsWhitespace((unsigned char)Text.front()))
		Text.erase(Text.begin());
	while(!Text.empty() && IsWhitespace((unsigned char)Text.back()))
		Text.pop_back();
	return Text;
}

static std::string BestClientStripQuotes(std::string Text)
{
	Text = BestClientTrimString(std::move(Text));
	if(Text.size() >= 2 && Text.front() == '"' && Text.back() == '"')
		return Text.substr(1, Text.size() - 2);
	return Text;
}

static std::vector<std::string> BestClientSplitCommaSeparated(const std::string &Text);
static std::string BestClientFormatReShadeFloat(float Value);

static bool BestClientTryParseFloatText(const std::string &Text, float &Value)
{
	char *pEnd = nullptr;
	Value = std::strtof(Text.c_str(), &pEnd);
	return pEnd != Text.c_str() && pEnd != nullptr && *pEnd == '\0';
}

static bool BestClientTryParseFloatVectorText(const std::string &Text, std::array<float, 4> &aValues, int ExpectedComponents)
{
	aValues = {0.0f, 0.0f, 0.0f, 0.0f};

	std::string Normalized = BestClientTrimString(Text);
	const size_t OpenParenPos = Normalized.find('(');
	const size_t CloseParenPos = Normalized.rfind(')');
	if(OpenParenPos != std::string::npos && CloseParenPos != std::string::npos && OpenParenPos < CloseParenPos)
		Normalized = Normalized.substr(OpenParenPos + 1, CloseParenPos - OpenParenPos - 1);

	const std::vector<std::string> vParts = BestClientSplitCommaSeparated(Normalized);
	if((int)vParts.size() != ExpectedComponents)
		return false;

	for(int Component = 0; Component < ExpectedComponents; ++Component)
	{
		if(!BestClientTryParseFloatText(vParts[Component], aValues[Component]))
			return false;
	}

	return true;
}

static std::string BestClientFormatReShadeFloatVector(const std::array<float, 4> &aValues, int NumComponents)
{
	std::string Result;
	for(int Component = 0; Component < NumComponents; ++Component)
	{
		if(!Result.empty())
			Result.push_back(',');
		Result += BestClientFormatReShadeFloat(aValues[Component]);
	}
	return Result;
}

static bool BestClientIsReShadeUniformFloatVectorType(EBestClientReShadeUniformType Type)
{
	return Type == EBestClientReShadeUniformType::FLOAT2 || Type == EBestClientReShadeUniformType::FLOAT3 || Type == EBestClientReShadeUniformType::FLOAT4;
}

static const char *BestClientReShadeUniformComponentSuffix(int ComponentIndex)
{
	static const char *s_apSuffixes[] = {"X", "Y", "Z", "W"};
	if(ComponentIndex < 0 || ComponentIndex >= (int)std::size(s_apSuffixes))
		return "?";
	return s_apSuffixes[ComponentIndex];
}

static bool BestClientTryParseIntText(const std::string &Text, int &Value)
{
	char *pEnd = nullptr;
	const long ParsedValue = std::strtol(Text.c_str(), &pEnd, 10);
	if(pEnd == Text.c_str() || pEnd == nullptr || *pEnd != '\0')
		return false;
	Value = (int)ParsedValue;
	return true;
}

static bool BestClientTryParseUintText(const std::string &Text, unsigned int &Value)
{
	char *pEnd = nullptr;
	const unsigned long ParsedValue = std::strtoul(Text.c_str(), &pEnd, 10);
	if(pEnd == Text.c_str() || pEnd == nullptr || *pEnd != '\0')
		return false;
	Value = (unsigned int)ParsedValue;
	return true;
}

static bool BestClientTryParseBoolText(const std::string &Text, bool &Value)
{
	if(_stricmp(Text.c_str(), "true") == 0 || Text == "1")
	{
		Value = true;
		return true;
	}
	if(_stricmp(Text.c_str(), "false") == 0 || Text == "0")
	{
		Value = false;
		return true;
	}
	return false;
}

static std::vector<std::string> BestClientSplitCommaSeparated(const std::string &Text)
{
	std::vector<std::string> vTokens;
	size_t TokenStart = 0;
	while(TokenStart <= Text.size())
	{
		const size_t TokenEnd = Text.find(',', TokenStart);
		const std::string Token = BestClientTrimString(Text.substr(TokenStart, TokenEnd == std::string::npos ? std::string::npos : TokenEnd - TokenStart));
		if(!Token.empty())
			vTokens.push_back(Token);
		if(TokenEnd == std::string::npos)
			break;
		TokenStart = TokenEnd + 1;
	}
	return vTokens;
}

static std::vector<std::string> BestClientSplitReShadeUiItems(const std::string &Text)
{
	std::vector<std::string> vItems;
	size_t Start = 0;
	while(Start < Text.size())
	{
		size_t End = Text.find("\\0", Start);
		if(End == std::string::npos)
			End = Text.size();

		std::string Item = BestClientTrimString(Text.substr(Start, End - Start));
		if(!Item.empty())
			vItems.push_back(std::move(Item));

		if(End == Text.size())
			break;
		Start = End + 2;
	}
	return vItems;
}

static bool BestClientReadAbsoluteTextFile(IStorage *pStorage, const char *pAbsolutePath, std::string &Text)
{
	char *pFileText = pStorage->ReadFileStr(pAbsolutePath, IStorage::TYPE_ABSOLUTE);
	if(pFileText == nullptr)
		return false;

	Text = pFileText;
	free(pFileText);
	return true;
}

static bool BestClientFileExistsAbsolute(const char *pAbsolutePath)
{
	return pAbsolutePath != nullptr && pAbsolutePath[0] != '\0' && fs_is_file(pAbsolutePath);
}

[[maybe_unused]] static void BestClientSetIniRootKey(std::string &Text, const char *pKey, const std::string &Value)
{
	const std::string Prefix = std::string(pKey) + "=";
	const size_t FirstSectionPos = Text.find('[');
	const size_t SearchEnd = FirstSectionPos == std::string::npos ? Text.size() : FirstSectionPos;

	size_t LineStart = 0;
	while(LineStart <= SearchEnd)
	{
		size_t LineEnd = Text.find('\n', LineStart);
		if(LineEnd == std::string::npos || LineEnd > SearchEnd)
			LineEnd = SearchEnd;
		if(Text.compare(LineStart, Prefix.size(), Prefix) == 0)
		{
			Text.replace(LineStart, LineEnd - LineStart, Prefix + Value);
			return;
		}
		if(LineEnd >= SearchEnd)
			break;
		LineStart = LineEnd + 1;
	}

	Text.insert(0, Prefix + Value + "\n");
}

[[maybe_unused]] static void BestClientSetIniSectionKey(std::string &Text, const char *pSection, const char *pKey, const std::string &Value)
{
	const std::string SectionHeader = std::string("[") + pSection + "]";
	const std::string Prefix = std::string(pKey) + "=";
	size_t SectionPos = Text.find(SectionHeader);
	if(SectionPos == std::string::npos)
	{
		if(!Text.empty() && Text.back() != '\n')
			Text.push_back('\n');
		Text += SectionHeader + "\n" + Prefix + Value + "\n";
		return;
	}

	size_t ContentStart = Text.find('\n', SectionPos);
	if(ContentStart == std::string::npos)
	{
		Text += "\n" + Prefix + Value + "\n";
		return;
	}
	++ContentStart;

	size_t SectionEnd = Text.find("\n[", ContentStart);
	if(SectionEnd == std::string::npos)
		SectionEnd = Text.size();

	for(size_t LineStart = ContentStart; LineStart < SectionEnd;)
	{
		size_t LineEnd = Text.find('\n', LineStart);
		if(LineEnd == std::string::npos || LineEnd > SectionEnd)
			LineEnd = SectionEnd;
		if(Text.compare(LineStart, Prefix.size(), Prefix) == 0)
		{
			Text.replace(LineStart, LineEnd - LineStart, Prefix + Value);
			return;
		}
		if(LineEnd >= SectionEnd)
			break;
		LineStart = LineEnd + 1;
	}

	if(SectionEnd == Text.size())
	{
		if(!Text.empty() && Text.back() != '\n')
			Text.push_back('\n');
		Text += Prefix + Value + "\n";
	}
	else
	{
		Text.insert(SectionEnd + 1, Prefix + Value + "\n");
	}
}

static bool BestClientParseReShadePresetText(const std::string &PresetText, SBestClientReShadePresetState &ParsedPresetState)
{
	std::istringstream Stream(PresetText);
	std::string Line;
	std::string CurrentSection;
	while(std::getline(Stream, Line))
	{
		const std::string TrimmedLine = BestClientTrimString(Line);
		if(TrimmedLine.empty() || TrimmedLine[0] == ';' || TrimmedLine[0] == '#')
			continue;

		if(TrimmedLine.front() == '[' && TrimmedLine.back() == ']')
		{
			CurrentSection = BestClientTrimString(TrimmedLine.substr(1, TrimmedLine.size() - 2));
			continue;
		}

		const size_t EqualsPos = TrimmedLine.find('=');
		if(EqualsPos == std::string::npos)
			continue;

		const std::string Key = BestClientTrimString(TrimmedLine.substr(0, EqualsPos));
		const std::string Value = BestClientTrimString(TrimmedLine.substr(EqualsPos + 1));

		if(CurrentSection.empty())
		{
			if(Key == "TechniqueSorting")
			{
				ParsedPresetState.m_vTechniqueSorting = BestClientSplitCommaSeparated(Value);
				ParsedPresetState.m_HasTechniqueSorting = true;
			}
			else if(Key == "Techniques")
			{
				ParsedPresetState.m_HasEnabledState = true;
				for(const std::string &Token : BestClientSplitCommaSeparated(Value))
					ParsedPresetState.m_EnabledTokens.insert(Token);
			}
			else if(Key == "EnabledShader")
			{
				ParsedPresetState.m_HasEnabledState = true;
				const std::string Token = BestClientTrimString(Value);
				if(!Token.empty())
					ParsedPresetState.m_EnabledTokens.insert(Token);
			}
			continue;
		}

		if(CurrentSection == "EnabledShaders")
		{
			ParsedPresetState.m_HasEnabledState = true;
			bool EnabledValue = true;
			if(Value.empty() || !BestClientTryParseBoolText(Value, EnabledValue) || EnabledValue)
				ParsedPresetState.m_EnabledTokens.insert(Key);
			continue;
		}

		ParsedPresetState.m_SectionValues[CurrentSection][Key] = Value;
	}

	return true;
}

static void BestClientInvalidateReShadePresetCache()
{
	gs_BestClientReShadeUiCache.m_HasPresetCache = false;
	gs_BestClientReShadeUiCache.m_PresetModifiedTime = 0;
	gs_BestClientReShadeUiCache.m_SettingsModifiedTime = 0;
	gs_BestClientReShadeUiCache.m_PresetState = SBestClientReShadePresetState{};
}

static std::string BestClientBuildEnabledTechniqueList(const SBestClientReShadePresetState &PresetState)
{
	std::string Result;
	std::unordered_set<std::string> AddedTokens;
	for(const std::string &Token : PresetState.m_vTechniqueSorting)
	{
		if(PresetState.m_EnabledTokens.find(Token) == PresetState.m_EnabledTokens.end())
			continue;
		if(!Result.empty())
			Result.push_back(',');
		Result += Token;
		AddedTokens.insert(Token);
	}

	for(const std::string &Token : PresetState.m_EnabledTokens)
	{
		if(AddedTokens.find(Token) != AddedTokens.end())
			continue;
		if(!Result.empty())
			Result.push_back(',');
		Result += Token;
	}
	return Result;
}

static std::string BestClientBuildTechniqueSortingList(const SBestClientReShadePresetState &PresetState)
{
	std::string Result;
	for(const std::string &Token : PresetState.m_vTechniqueSorting)
	{
		if(!Result.empty())
			Result.push_back(',');
		Result += Token;
	}
	return Result;
}

static std::string BestClientSerializeReShadePreset(const SBestClientReShadePresetState &PresetState)
{
	std::string PresetText;
	PresetText += "Techniques=" + BestClientBuildEnabledTechniqueList(PresetState) + "\n";
	PresetText += "TechniqueSorting=" + BestClientBuildTechniqueSortingList(PresetState) + "\n";
	const std::unordered_set<std::string> TrackedEffects = BestClientBuildTrackedReShadeEffectSet(PresetState);

	std::vector<std::string> vSectionNames;
	vSectionNames.reserve(PresetState.m_SectionValues.size());
	for(const auto &[SectionName, SectionValues] : PresetState.m_SectionValues)
	{
		(void)SectionValues;
		if(!TrackedEffects.empty() && TrackedEffects.find(SectionName) == TrackedEffects.end())
			continue;
		vSectionNames.push_back(SectionName);
	}
	std::sort(vSectionNames.begin(), vSectionNames.end());

	for(const std::string &SectionName : vSectionNames)
	{
		const auto SectionIt = PresetState.m_SectionValues.find(SectionName);
		if(SectionIt == PresetState.m_SectionValues.end() || SectionIt->second.empty())
			continue;

		PresetText += "\n[" + SectionName + "]\n";
		std::vector<std::string> vKeys;
		vKeys.reserve(SectionIt->second.size());
		for(const auto &[Key, Value] : SectionIt->second)
		{
			(void)Value;
			vKeys.push_back(Key);
		}
		std::sort(vKeys.begin(), vKeys.end());

		for(const std::string &Key : vKeys)
		{
			const auto ValueIt = SectionIt->second.find(Key);
			if(ValueIt == SectionIt->second.end())
				continue;
			PresetText += Key + "=" + ValueIt->second + "\n";
		}
	}

	return PresetText;
}

static bool BestClientLoadReShadePreset(IStorage *pStorage, SBestClientReShadePresetState &PresetState, char *pError, int ErrorSize)
{
	char aPresetAbsolutePath[IO_MAX_PATH_LENGTH];
	pStorage->GetBinaryPath(gs_pBestClientReShadePresetPath, aPresetAbsolutePath, sizeof(aPresetAbsolutePath));
	if(aPresetAbsolutePath[0] == '\0')
	{
		str_copy(pError, "Failed to resolve ReShadePreset.ini", ErrorSize);
		return false;
	}

	char aSettingsAbsolutePath[IO_MAX_PATH_LENGTH];
	pStorage->GetCompletePath(IStorage::TYPE_SAVE, gs_pBestClientReShadeSettingsPath, aSettingsAbsolutePath, sizeof(aSettingsAbsolutePath));

	time_t CreatedTime = 0;
	time_t ModifiedTime = 0;
	const bool HasWriteTime = fs_file_time(aPresetAbsolutePath, &CreatedTime, &ModifiedTime) == 0;
	time_t SettingsCreatedTime = 0;
	time_t SettingsModifiedTime = 0;
	const bool HasSettingsWriteTime = fs_file_time(aSettingsAbsolutePath, &SettingsCreatedTime, &SettingsModifiedTime) == 0;
	if(gs_BestClientReShadeUiCache.m_HasPresetCache &&
		str_comp(gs_BestClientReShadeUiCache.m_aPresetPath, aPresetAbsolutePath) == 0 &&
		str_comp(gs_BestClientReShadeUiCache.m_aSettingsPath, aSettingsAbsolutePath) == 0 &&
		(!HasWriteTime || gs_BestClientReShadeUiCache.m_PresetModifiedTime == ModifiedTime) &&
		(!HasSettingsWriteTime || gs_BestClientReShadeUiCache.m_SettingsModifiedTime == SettingsModifiedTime))
	{
		PresetState = gs_BestClientReShadeUiCache.m_PresetState;
		if(ErrorSize > 0)
			pError[0] = '\0';
		return true;
	}

	std::string PresetText;
	if(!BestClientReadAbsoluteTextFile(pStorage, aPresetAbsolutePath, PresetText))
	{
		str_format(pError, ErrorSize, "Failed to read %s", gs_pBestClientReShadePresetPath);
		return false;
	}

	SBestClientReShadePresetState ParsedPresetState;
	BestClientParseReShadePresetText(PresetText, ParsedPresetState);

	std::string SettingsText;
	if(HasSettingsWriteTime && BestClientReadAbsoluteTextFile(pStorage, aSettingsAbsolutePath, SettingsText))
	{
		SBestClientReShadePresetState SavedState;
		BestClientParseReShadePresetText(SettingsText, SavedState);
		ParsedPresetState.m_vTechniqueSorting = SavedState.m_HasTechniqueSorting ? SavedState.m_vTechniqueSorting : std::vector<std::string>{};
		if(SavedState.m_HasEnabledState)
			ParsedPresetState.m_EnabledTokens = SavedState.m_EnabledTokens;
		for(const auto &[SectionName, SectionValues] : SavedState.m_SectionValues)
		{
			for(const auto &[Key, Value] : SectionValues)
				ParsedPresetState.m_SectionValues[SectionName][Key] = Value;
		}
	}
	else
	{
		ParsedPresetState.m_vTechniqueSorting.clear();
	}

	for(const std::string &Token : ParsedPresetState.m_EnabledTokens)
	{
		if(std::find(ParsedPresetState.m_vTechniqueSorting.begin(), ParsedPresetState.m_vTechniqueSorting.end(), Token) == ParsedPresetState.m_vTechniqueSorting.end())
			ParsedPresetState.m_vTechniqueSorting.push_back(Token);
	}

	str_copy(gs_BestClientReShadeUiCache.m_aPresetPath, aPresetAbsolutePath, sizeof(gs_BestClientReShadeUiCache.m_aPresetPath));
	str_copy(gs_BestClientReShadeUiCache.m_aSettingsPath, aSettingsAbsolutePath, sizeof(gs_BestClientReShadeUiCache.m_aSettingsPath));
	gs_BestClientReShadeUiCache.m_PresetModifiedTime = HasWriteTime ? ModifiedTime : 0;
	gs_BestClientReShadeUiCache.m_SettingsModifiedTime = HasSettingsWriteTime ? SettingsModifiedTime : 0;
	gs_BestClientReShadeUiCache.m_PresetState = ParsedPresetState;
	gs_BestClientReShadeUiCache.m_HasPresetCache = true;

	PresetState = ParsedPresetState;
	if(ErrorSize > 0)
		pError[0] = '\0';
	return true;
}

static bool BestClientSaveReShadeSettings(IStorage *pStorage, const SBestClientReShadePresetState &PresetState, char *pError, int ErrorSize)
{
	std::string SettingsText;
	SettingsText += "; Delete any line below to disable a broken screen-wide shader manually.\n";
	SettingsText += "Techniques=" + BestClientBuildEnabledTechniqueList(PresetState) + "\n";
	SettingsText += "TechniqueSorting=" + BestClientBuildTechniqueSortingList(PresetState) + "\n";
	const std::unordered_set<std::string> TrackedEffects = BestClientBuildTrackedReShadeEffectSet(PresetState);
	SettingsText += "[EnabledShaders]\n";
	for(const std::string &Token : PresetState.m_vTechniqueSorting)
	{
		if(PresetState.m_EnabledTokens.find(Token) != PresetState.m_EnabledTokens.end())
			SettingsText += Token + "=1\n";
	}
	for(const std::string &Token : PresetState.m_EnabledTokens)
	{
		if(std::find(PresetState.m_vTechniqueSorting.begin(), PresetState.m_vTechniqueSorting.end(), Token) == PresetState.m_vTechniqueSorting.end())
			SettingsText += Token + "=1\n";
	}

	std::vector<std::string> vSectionNames;
	vSectionNames.reserve(PresetState.m_SectionValues.size());
	for(const auto &[SectionName, SectionValues] : PresetState.m_SectionValues)
	{
		(void)SectionValues;
		if(!TrackedEffects.empty() && TrackedEffects.find(SectionName) == TrackedEffects.end())
			continue;
		vSectionNames.push_back(SectionName);
	}
	std::sort(vSectionNames.begin(), vSectionNames.end());

	for(const std::string &SectionName : vSectionNames)
	{
		const auto SectionIt = PresetState.m_SectionValues.find(SectionName);
		if(SectionIt == PresetState.m_SectionValues.end() || SectionIt->second.empty())
			continue;

		SettingsText += "\n[" + SectionName + "]\n";
		std::vector<std::string> vKeys;
		vKeys.reserve(SectionIt->second.size());
		for(const auto &[Key, Value] : SectionIt->second)
		{
			(void)Value;
			vKeys.push_back(Key);
		}
		std::sort(vKeys.begin(), vKeys.end());

		for(const std::string &Key : vKeys)
		{
			const auto ValueIt = SectionIt->second.find(Key);
			if(ValueIt == SectionIt->second.end())
				continue;
			SettingsText += Key + "=" + ValueIt->second + "\n";
		}
	}

	IOHANDLE File = pStorage->OpenFile(gs_pBestClientReShadeSettingsPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
	{
		str_format(pError, ErrorSize, "Failed to open %s for writing", gs_pBestClientReShadeSettingsPath);
		return false;
	}

	const unsigned TextSize = (unsigned)SettingsText.size();
	const bool WriteOk = io_write(File, SettingsText.c_str(), TextSize) == TextSize;
	io_close(File);
	if(!WriteOk)
	{
		str_format(pError, ErrorSize, "Failed to write %s", gs_pBestClientReShadeSettingsPath);
		return false;
	}

	if(ErrorSize > 0)
		pError[0] = '\0';
	BestClientInvalidateReShadePresetCache();
	return true;
}

static bool BestClientSaveReShadePreset(IStorage *pStorage, const SBestClientReShadePresetState &PresetState, char *pError, int ErrorSize)
{
	char aPresetAbsolutePath[IO_MAX_PATH_LENGTH];
	pStorage->GetBinaryPath(gs_pBestClientReShadePresetPath, aPresetAbsolutePath, sizeof(aPresetAbsolutePath));

	const std::string PresetText = BestClientSerializeReShadePreset(PresetState);

	if(fs_makedir_rec_for(aPresetAbsolutePath) != 0)
	{
		str_format(pError, ErrorSize, "Failed to create folder for %s", gs_pBestClientReShadePresetPath);
		return false;
	}

	IOHANDLE File = pStorage->OpenFile(aPresetAbsolutePath, IOFLAG_WRITE, IStorage::TYPE_ABSOLUTE);
	if(!File)
	{
		str_format(pError, ErrorSize, "Failed to open %s for writing", gs_pBestClientReShadePresetPath);
		return false;
	}

	const unsigned TextSize = (unsigned)PresetText.size();
	const bool WriteOk = io_write(File, PresetText.c_str(), TextSize) == TextSize;
	io_close(File);
	if(!WriteOk)
	{
		str_format(pError, ErrorSize, "Failed to write %s", gs_pBestClientReShadePresetPath);
		return false;
	}

	if(ErrorSize > 0)
		pError[0] = '\0';
	BestClientInvalidateReShadePresetCache();
	return true;
}

static bool BestClientSaveReShadeBridgeState(IStorage *pStorage, const SBestClientReShadePresetState &PresetState, uint64_t Revision, char *pError, int ErrorSize)
{
	char aBridgeAbsolutePath[IO_MAX_PATH_LENGTH];
	pStorage->GetBinaryPath(gs_pBestClientReShadeBridgeStatePath, aBridgeAbsolutePath, sizeof(aBridgeAbsolutePath));
	if(aBridgeAbsolutePath[0] == '\0')
	{
		str_format(pError, ErrorSize, "Failed to resolve %s", gs_pBestClientReShadeBridgeStatePath);
		return false;
	}

	std::string BridgeText;
	BridgeText += "Revision=" + std::to_string(Revision) + "\n";
	BridgeText += "Techniques=" + BestClientBuildEnabledTechniqueList(PresetState) + "\n";
	BridgeText += "TechniqueSorting=" + BestClientBuildTechniqueSortingList(PresetState) + "\n";

	const std::unordered_set<std::string> TrackedEffects = BestClientBuildTrackedReShadeEffectSet(PresetState);
	std::vector<std::string> vSectionNames;
	vSectionNames.reserve(TrackedEffects.size());
	for(const std::string &EffectName : TrackedEffects)
		vSectionNames.push_back(EffectName);
	std::sort(vSectionNames.begin(), vSectionNames.end());

	for(const std::string &EffectName : vSectionNames)
	{
		const auto SectionIt = PresetState.m_SectionValues.find(EffectName);
		const auto &vUniforms = BestClientGetReShadeUniformMetadata(pStorage, EffectName);

		std::vector<std::pair<std::string, std::string>> vBridgeValues;
		vBridgeValues.reserve(vUniforms.size());

		for(const SBestClientReShadeUniformMeta &UniformMeta : vUniforms)
		{
			std::string UniformValue = UniformMeta.m_DefaultValue;
			if(SectionIt != PresetState.m_SectionValues.end())
			{
				const auto ValueIt = SectionIt->second.find(UniformMeta.m_Name);
				if(ValueIt != SectionIt->second.end())
					UniformValue = ValueIt->second;
			}

			std::string BridgeValue;
			bool HasBridgeValue = false;
			if(UniformMeta.m_Type == EBestClientReShadeUniformType::BOOL)
			{
				bool BoolValue = false;
				if(!BestClientTryParseBoolText(UniformValue, BoolValue))
					BestClientTryParseBoolText(UniformMeta.m_DefaultValue, BoolValue);
				BridgeValue = BoolValue ? "true" : "false";
				HasBridgeValue = true;
			}
			else if(UniformMeta.m_Type == EBestClientReShadeUniformType::INT)
			{
				int IntValue = 0;
				unsigned int UintValue = 0;
				if(BestClientTryParseIntText(UniformValue, IntValue))
					HasBridgeValue = true;
				else if(BestClientTryParseUintText(UniformValue, UintValue))
				{
					IntValue = (int)UintValue;
					HasBridgeValue = true;
				}
				else if(BestClientTryParseIntText(UniformMeta.m_DefaultValue, IntValue))
					HasBridgeValue = true;
				if(HasBridgeValue)
					BridgeValue = std::to_string(IntValue);
			}
			else if(UniformMeta.m_Type == EBestClientReShadeUniformType::UINT)
			{
				unsigned int UintValue = 0;
				int IntValue = 0;
				if(BestClientTryParseUintText(UniformValue, UintValue))
					HasBridgeValue = true;
				else if(BestClientTryParseIntText(UniformValue, IntValue) && IntValue >= 0)
				{
					UintValue = (unsigned int)IntValue;
					HasBridgeValue = true;
				}
				else if(BestClientTryParseUintText(UniformMeta.m_DefaultValue, UintValue))
					HasBridgeValue = true;
				if(HasBridgeValue)
					BridgeValue = std::to_string(UintValue);
			}
			else if(BestClientIsReShadeUniformFloatVectorType(UniformMeta.m_Type))
			{
				std::array<float, 4> aValues = {0.0f, 0.0f, 0.0f, 0.0f};
				if(!BestClientTryParseFloatVectorText(UniformValue, aValues, UniformMeta.m_NumComponents))
					BestClientTryParseFloatVectorText(UniformMeta.m_DefaultValue, aValues, UniformMeta.m_NumComponents);
				BridgeValue = BestClientFormatReShadeFloatVector(aValues, UniformMeta.m_NumComponents);
				HasBridgeValue = true;
			}
			else
			{
				float FloatValue = 0.0f;
				if(!BestClientTryParseFloatText(UniformValue, FloatValue))
					BestClientTryParseFloatText(UniformMeta.m_DefaultValue, FloatValue);
				BridgeValue = BestClientFormatReShadeFloat(FloatValue);
				HasBridgeValue = true;
			}

			if(HasBridgeValue)
				vBridgeValues.emplace_back(UniformMeta.m_Name, std::move(BridgeValue));
		}

		if(vBridgeValues.empty() && SectionIt != PresetState.m_SectionValues.end())
		{
			for(const auto &[Key, Value] : SectionIt->second)
			{
				bool BoolValue = false;
				int IntValue = 0;
				unsigned int UintValue = 0;
				float FloatValue = 0.0f;
				std::array<float, 4> aValues = {0.0f, 0.0f, 0.0f, 0.0f};
				if(BestClientTryParseBoolText(Value, BoolValue))
					vBridgeValues.emplace_back(Key, BoolValue ? "true" : "false");
				else if(BestClientTryParseIntText(Value, IntValue))
					vBridgeValues.emplace_back(Key, std::to_string(IntValue));
				else if(BestClientTryParseUintText(Value, UintValue))
					vBridgeValues.emplace_back(Key, std::to_string(UintValue));
				else if(BestClientTryParseFloatText(Value, FloatValue))
					vBridgeValues.emplace_back(Key, BestClientFormatReShadeFloat(FloatValue));
				else if(BestClientTryParseFloatVectorText(Value, aValues, 2))
					vBridgeValues.emplace_back(Key, BestClientFormatReShadeFloatVector(aValues, 2));
				else if(BestClientTryParseFloatVectorText(Value, aValues, 3))
					vBridgeValues.emplace_back(Key, BestClientFormatReShadeFloatVector(aValues, 3));
				else if(BestClientTryParseFloatVectorText(Value, aValues, 4))
					vBridgeValues.emplace_back(Key, BestClientFormatReShadeFloatVector(aValues, 4));
			}
		}

		if(vBridgeValues.empty())
			continue;

		BridgeText += "\n[" + EffectName + "]\n";
		for(const auto &[Key, Value] : vBridgeValues)
			BridgeText += Key + "=" + Value + "\n";
	}

	if(fs_makedir_rec_for(aBridgeAbsolutePath) != 0)
	{
		str_format(pError, ErrorSize, "Failed to create folder for %s", gs_pBestClientReShadeBridgeStatePath);
		return false;
	}

	IOHANDLE File = pStorage->OpenFile(aBridgeAbsolutePath, IOFLAG_WRITE, IStorage::TYPE_ABSOLUTE);
	if(!File)
	{
		str_format(pError, ErrorSize, "Failed to open %s for writing", gs_pBestClientReShadeBridgeStatePath);
		return false;
	}

	const unsigned TextSize = (unsigned)BridgeText.size();
	const bool WriteOk = io_write(File, BridgeText.c_str(), TextSize) == TextSize;
	io_close(File);
	if(!WriteOk)
	{
		str_format(pError, ErrorSize, "Failed to write %s", gs_pBestClientReShadeBridgeStatePath);
		return false;
	}

	if(ErrorSize > 0)
		pError[0] = '\0';
	return true;
}

static uint64_t gs_ReShadeToggleRevision = 0;

void BestClientProcessReShadeToggle(IStorage *pStorage)
{
	if(!gs_ReShadeTogglePending)
		return;
	gs_ReShadeTogglePending = false;

	SBestClientReShadePresetState PresetState;
	char aError[192];
	if(!BestClientLoadReShadePreset(pStorage, PresetState, aError, sizeof(aError)))
		return;

	if(!gs_ReShadeEffectsMuted)
	{
		PresetState.m_EnabledTokens.clear();
		gs_ReShadeEffectsMuted = true;
	}
	else
	{
		for(const std::string &Token : PresetState.m_vTechniqueSorting)
			PresetState.m_EnabledTokens.insert(Token);
		gs_ReShadeEffectsMuted = false;
	}

	BestClientSaveReShadePreset(pStorage, PresetState, aError, sizeof(aError));
	BestClientSaveReShadeSettings(pStorage, PresetState, aError, sizeof(aError));
	BestClientSaveReShadeBridgeState(pStorage, PresetState, ++gs_ReShadeToggleRevision, aError, sizeof(aError));

	gs_BestClientReShadeUiCache.m_HasPresetCache = false;
}

static std::string BestClientBuildTechniqueToken(const std::string &TechniqueName, const std::string &EffectName)
{
	if(TechniqueName.empty())
		return "";
	if(EffectName.empty())
		return TechniqueName;
	return TechniqueName + "@" + EffectName;
}

static SBestClientReShadeTechniqueMeta BestClientBuildReShadeTechniqueMetaFromToken(const std::string &Token)
{
	SBestClientReShadeTechniqueMeta Technique;
	Technique.m_Token = Token;
	const size_t SeparatorPos = Token.find('@');
	if(SeparatorPos == std::string::npos)
	{
		Technique.m_TechniqueName = Token;
	}
	else
	{
		Technique.m_TechniqueName = Token.substr(0, SeparatorPos);
		Technique.m_EffectName = Token.substr(SeparatorPos + 1);
	}
	return Technique;
}

static std::vector<SBestClientReShadeTechniqueMeta> BestClientBuildReShadeTechniqueList(const SBestClientReShadePresetState &PresetState)
{
	std::vector<SBestClientReShadeTechniqueMeta> vTechniques;
	vTechniques.reserve(PresetState.m_vTechniqueSorting.size());
	for(const std::string &Token : PresetState.m_vTechniqueSorting)
		vTechniques.push_back(BestClientBuildReShadeTechniqueMetaFromToken(Token));
	return vTechniques;
}

static std::unordered_set<std::string> BestClientBuildTrackedReShadeEffectSet(const SBestClientReShadePresetState &PresetState)
{
	std::unordered_set<std::string> TrackedEffects;
	TrackedEffects.reserve(PresetState.m_vTechniqueSorting.size());
	for(const std::string &Token : PresetState.m_vTechniqueSorting)
	{
		const SBestClientReShadeTechniqueMeta Technique = BestClientBuildReShadeTechniqueMetaFromToken(Token);
		if(!Technique.m_EffectName.empty())
			TrackedEffects.insert(Technique.m_EffectName);
	}
	return TrackedEffects;
}

static void BestClientBuildReShadeEffectIndex(IStorage *pStorage)
{
	if(gs_BestClientReShadeUiCache.m_HasEffectIndex)
		return;
	// While the index is empty, retry on a throttle instead of every frame so a
	// shader folder that becomes ready after startup still gets picked up.
	if(gs_BestClientReShadeUiCache.m_EffectIndexRetryTime != 0 && time_get() < gs_BestClientReShadeUiCache.m_EffectIndexRetryTime)
		return;

	gs_BestClientReShadeUiCache.m_EffectPaths.clear();

	char aShadersAbsolutePath[IO_MAX_PATH_LENGTH];
	pStorage->GetBinaryPathAbsolute(gs_pBestClientReShadeShadersPath, aShadersAbsolutePath, sizeof(aShadersAbsolutePath));
	if(aShadersAbsolutePath[0] == '\0')
	{
		gs_BestClientReShadeUiCache.m_EffectIndexRetryTime = time_get() + time_freq() * 2;
		return;
	}

	std::error_code Error;
	for(const auto &Entry : std::filesystem::recursive_directory_iterator(aShadersAbsolutePath, Error))
	{
		if(Error)
			break;
		if(!Entry.is_regular_file())
			continue;
		const std::filesystem::path &Path = Entry.path();
		if(Path.extension() != ".fx")
			continue;
		const std::string EffectName = Path.filename().string();
		if(gs_BestClientReShadeUiCache.m_EffectPaths.find(EffectName) == gs_BestClientReShadeUiCache.m_EffectPaths.end())
			gs_BestClientReShadeUiCache.m_EffectPaths[EffectName] = Path.string();
	}

	// Only lock the cache once we actually found effects. An empty result means the
	// folder is missing/not-yet-ready or a transient FS error occurred; retry later.
	if(gs_BestClientReShadeUiCache.m_EffectPaths.empty())
	{
		gs_BestClientReShadeUiCache.m_EffectIndexRetryTime = time_get() + time_freq() * 2;
		return;
	}

	gs_BestClientReShadeUiCache.m_EffectIndexRetryTime = 0;
	gs_BestClientReShadeUiCache.m_HasEffectIndex = true;
}

static std::string BestClientSanitizeReShadeShaderText(const std::string &ShaderText)
{
	std::string Sanitized = ShaderText;
	enum class EParseState
	{
		NORMAL,
		LINE_COMMENT,
		BLOCK_COMMENT,
		STRING_LITERAL,
		CHAR_LITERAL,
	};

	EParseState State = EParseState::NORMAL;
	for(size_t Pos = 0; Pos < Sanitized.size(); ++Pos)
	{
		char &c = Sanitized[Pos];
		switch(State)
		{
		case EParseState::NORMAL:
			if(c == '/' && Pos + 1 < Sanitized.size() && Sanitized[Pos + 1] == '/')
			{
				c = ' ';
				Sanitized[Pos + 1] = ' ';
				++Pos;
				State = EParseState::LINE_COMMENT;
			}
			else if(c == '/' && Pos + 1 < Sanitized.size() && Sanitized[Pos + 1] == '*')
			{
				c = ' ';
				Sanitized[Pos + 1] = ' ';
				++Pos;
				State = EParseState::BLOCK_COMMENT;
			}
			else if(c == '"')
			{
				c = ' ';
				State = EParseState::STRING_LITERAL;
			}
			else if(c == '\'')
			{
				c = ' ';
				State = EParseState::CHAR_LITERAL;
			}
			break;
		case EParseState::LINE_COMMENT:
			if(c != '\r' && c != '\n')
				c = ' ';
			else
				State = EParseState::NORMAL;
			break;
		case EParseState::BLOCK_COMMENT:
			if(c == '*' && Pos + 1 < Sanitized.size() && Sanitized[Pos + 1] == '/')
			{
				c = ' ';
				Sanitized[Pos + 1] = ' ';
				++Pos;
				State = EParseState::NORMAL;
			}
			else if(c != '\r' && c != '\n')
			{
				c = ' ';
			}
			break;
		case EParseState::STRING_LITERAL:
			if(c == '\\' && Pos + 1 < Sanitized.size())
			{
				c = ' ';
				if(Sanitized[Pos + 1] != '\r' && Sanitized[Pos + 1] != '\n')
					Sanitized[Pos + 1] = ' ';
				++Pos;
			}
			else if(c == '"')
			{
				c = ' ';
				State = EParseState::NORMAL;
			}
			else if(c != '\r' && c != '\n')
			{
				c = ' ';
			}
			break;
		case EParseState::CHAR_LITERAL:
			if(c == '\\' && Pos + 1 < Sanitized.size())
			{
				c = ' ';
				if(Sanitized[Pos + 1] != '\r' && Sanitized[Pos + 1] != '\n')
					Sanitized[Pos + 1] = ' ';
				++Pos;
			}
			else if(c == '\'')
			{
				c = ' ';
				State = EParseState::NORMAL;
			}
			else if(c != '\r' && c != '\n')
			{
				c = ' ';
			}
			break;
		}
	}

	return Sanitized;
}

static std::vector<SBestClientReShadeTechniqueMeta> BestClientParseReShadeTechniqueMetadata(const std::string &ShaderText, const std::string &EffectName)
{
	const auto IsIdentifierStart = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
	const auto IsIdentifierChar = [&](char c) { return IsIdentifierStart(c) || (c >= '0' && c <= '9'); };

	std::vector<SBestClientReShadeTechniqueMeta> vTechniques;
	std::unordered_set<std::string> SeenTokens;
	const std::string SanitizedText = BestClientSanitizeReShadeShaderText(ShaderText);

	for(size_t Pos = 0; Pos < SanitizedText.size();)
	{
		if(!IsIdentifierStart(SanitizedText[Pos]))
		{
			++Pos;
			continue;
		}

		const size_t KeywordStart = Pos;
		while(Pos < SanitizedText.size() && IsIdentifierChar(SanitizedText[Pos]))
			++Pos;

		if(SanitizedText.compare(KeywordStart, Pos - KeywordStart, "technique") != 0)
			continue;

		while(Pos < SanitizedText.size() && (SanitizedText[Pos] == ' ' || SanitizedText[Pos] == '\t' || SanitizedText[Pos] == '\r' || SanitizedText[Pos] == '\n'))
			++Pos;

		if(Pos >= SanitizedText.size() || !IsIdentifierStart(SanitizedText[Pos]))
			continue;

		const size_t NameStart = Pos;
		while(Pos < SanitizedText.size() && IsIdentifierChar(SanitizedText[Pos]))
			++Pos;

		const std::string TechniqueName = SanitizedText.substr(NameStart, Pos - NameStart);
		const std::string Token = BestClientBuildTechniqueToken(TechniqueName, EffectName);
		if(Token.empty() || !SeenTokens.insert(Token).second)
			continue;

		SBestClientReShadeTechniqueMeta Technique;
		Technique.m_Token = Token;
		Technique.m_TechniqueName = TechniqueName;
		Technique.m_EffectName = EffectName;
		vTechniques.push_back(std::move(Technique));
	}

	return vTechniques;
}

static void BestClientBuildReShadeTechniqueIndex(IStorage *pStorage)
{
	if(gs_BestClientReShadeUiCache.m_HasTechniqueIndex)
		return;
	if(gs_BestClientReShadeUiCache.m_TechniqueIndexRetryTime != 0 && time_get() < gs_BestClientReShadeUiCache.m_TechniqueIndexRetryTime)
		return;

	BestClientBuildReShadeEffectIndex(pStorage);
	gs_BestClientReShadeUiCache.m_vTechniqueIndex.clear();

	std::vector<std::string> vEffectNames;
	vEffectNames.reserve(gs_BestClientReShadeUiCache.m_EffectPaths.size());
	for(const auto &[EffectName, Path] : gs_BestClientReShadeUiCache.m_EffectPaths)
	{
		(void)Path;
		vEffectNames.push_back(EffectName);
	}
	std::sort(vEffectNames.begin(), vEffectNames.end(), [](const std::string &Left, const std::string &Right) {
		return str_comp_nocase(Left.c_str(), Right.c_str()) < 0;
	});

	for(const std::string &EffectName : vEffectNames)
	{
		const auto EffectPathIt = gs_BestClientReShadeUiCache.m_EffectPaths.find(EffectName);
		if(EffectPathIt == gs_BestClientReShadeUiCache.m_EffectPaths.end())
			continue;

		std::string ShaderText;
		if(!BestClientReadAbsoluteTextFile(pStorage, EffectPathIt->second.c_str(), ShaderText))
			continue;

		std::vector<SBestClientReShadeTechniqueMeta> vTechniques = BestClientParseReShadeTechniqueMetadata(ShaderText, EffectName);
		gs_BestClientReShadeUiCache.m_vTechniqueIndex.insert(gs_BestClientReShadeUiCache.m_vTechniqueIndex.end(), vTechniques.begin(), vTechniques.end());
	}

	// Don't lock an empty technique list while the effect index is still retrying
	// (shader folder not ready / transient FS error). Once effects are indexed, an
	// empty technique list is a real result, so lock it to stop re-parsing.
	if(gs_BestClientReShadeUiCache.m_vTechniqueIndex.empty() && !gs_BestClientReShadeUiCache.m_HasEffectIndex)
	{
		gs_BestClientReShadeUiCache.m_TechniqueIndexRetryTime = time_get() + time_freq() * 2;
		return;
	}

	gs_BestClientReShadeUiCache.m_TechniqueIndexRetryTime = 0;
	gs_BestClientReShadeUiCache.m_HasTechniqueIndex = true;
}

[[maybe_unused]] static std::vector<SBestClientReShadeTechniqueMeta> BestClientBuildReShadeTechniqueCatalog(IStorage *pStorage, const SBestClientReShadePresetState &PresetState)
{
	BestClientBuildReShadeTechniqueIndex(pStorage);

	std::vector<SBestClientReShadeTechniqueMeta> vTechniques = gs_BestClientReShadeUiCache.m_vTechniqueIndex;
	std::unordered_set<std::string> SeenTokens;
	SeenTokens.reserve(vTechniques.size() + PresetState.m_vTechniqueSorting.size());
	for(const SBestClientReShadeTechniqueMeta &Technique : vTechniques)
		SeenTokens.insert(Technique.m_Token);

	for(const std::string &Token : PresetState.m_vTechniqueSorting)
	{
		if(SeenTokens.insert(Token).second)
			vTechniques.push_back(BestClientBuildReShadeTechniqueMetaFromToken(Token));
	}

	return vTechniques;
}

static bool BestClientHasReShadeTechniqueInSorting(const SBestClientReShadePresetState &PresetState, const std::string &Token)
{
	return std::find(PresetState.m_vTechniqueSorting.begin(), PresetState.m_vTechniqueSorting.end(), Token) != PresetState.m_vTechniqueSorting.end();
}

static void BestClientAddReShadeTechniqueToPreset(SBestClientReShadePresetState &PresetState, const std::string &Token, const std::vector<SBestClientReShadeTechniqueMeta> &vTechniqueCatalog)
{
	if(BestClientHasReShadeTechniqueInSorting(PresetState, Token))
	{
		PresetState.m_EnabledTokens.insert(Token);
		return;
	}

	int TokenOrder = (int)vTechniqueCatalog.size();
	for(size_t Index = 0; Index < vTechniqueCatalog.size(); ++Index)
	{
		if(vTechniqueCatalog[Index].m_Token == Token)
		{
			TokenOrder = (int)Index;
			break;
		}
	}

	auto InsertPos = PresetState.m_vTechniqueSorting.end();
	for(auto It = PresetState.m_vTechniqueSorting.begin(); It != PresetState.m_vTechniqueSorting.end(); ++It)
	{
		int CurrentOrder = (int)vTechniqueCatalog.size();
		for(size_t Index = 0; Index < vTechniqueCatalog.size(); ++Index)
		{
			if(vTechniqueCatalog[Index].m_Token == *It)
			{
				CurrentOrder = (int)Index;
				break;
			}
		}

		if(CurrentOrder > TokenOrder)
		{
			InsertPos = It;
			break;
		}
	}

	PresetState.m_vTechniqueSorting.insert(InsertPos, Token);
	PresetState.m_EnabledTokens.insert(Token);
}

static void BestClientRemoveReShadeTechniqueFromPreset(SBestClientReShadePresetState &PresetState, const std::string &Token)
{
	PresetState.m_vTechniqueSorting.erase(std::remove(PresetState.m_vTechniqueSorting.begin(), PresetState.m_vTechniqueSorting.end(), Token), PresetState.m_vTechniqueSorting.end());
	PresetState.m_EnabledTokens.erase(Token);
}

static bool BestClientFindAnnotationValue(const std::string &Annotations, const char *pKey, std::string &Value)
{
	const std::string Pattern = std::string(pKey);
	size_t SearchPos = 0;
	while((SearchPos = Annotations.find(Pattern, SearchPos)) != std::string::npos)
	{
		const size_t BeforePos = SearchPos == 0 ? SearchPos : SearchPos - 1;
		if(SearchPos > 0)
		{
			const char BeforeChar = Annotations[BeforePos];
			if((BeforeChar >= 'a' && BeforeChar <= 'z') || (BeforeChar >= 'A' && BeforeChar <= 'Z') || (BeforeChar >= '0' && BeforeChar <= '9') || BeforeChar == '_')
			{
				SearchPos += Pattern.size();
				continue;
			}
		}

		const size_t EqualsPos = Annotations.find('=', SearchPos + Pattern.size());
		if(EqualsPos == std::string::npos)
			return false;
		const size_t ValueEndPos = Annotations.find(';', EqualsPos + 1);
		const size_t EndPos = ValueEndPos == std::string::npos ? Annotations.size() : ValueEndPos;
		Value = BestClientStripQuotes(Annotations.substr(EqualsPos + 1, EndPos - EqualsPos - 1));
		return true;
	}
	return false;
}

static size_t BestClientFindReShadeUniformStatementEnd(const std::string &ShaderText, size_t StartPos)
{
	int AnnotationDepth = 0;
	bool InString = false;
	for(size_t Pos = StartPos; Pos < ShaderText.size(); ++Pos)
	{
		const char c = ShaderText[Pos];
		if(c == '"' && (Pos == 0 || ShaderText[Pos - 1] != '\\'))
		{
			InString = !InString;
			continue;
		}
		if(InString)
			continue;
		if(c == '<')
			++AnnotationDepth;
		else if(c == '>' && AnnotationDepth > 0)
			--AnnotationDepth;
		else if(c == ';' && AnnotationDepth == 0)
			return Pos;
	}
	return std::string::npos;
}

static bool BestClientParseReShadeUniformStatement(const std::string &Statement, SBestClientReShadeUniformMeta &UniformMeta)
{
	size_t Cursor = std::string("uniform").size();
	while(Cursor < Statement.size() && (Statement[Cursor] == ' ' || Statement[Cursor] == '\t' || Statement[Cursor] == '\n' || Statement[Cursor] == '\r'))
		++Cursor;

	size_t TypeStart = Cursor;
	while(Cursor < Statement.size() && ((Statement[Cursor] >= 'a' && Statement[Cursor] <= 'z') || (Statement[Cursor] >= 'A' && Statement[Cursor] <= 'Z') || (Statement[Cursor] >= '0' && Statement[Cursor] <= '9') || Statement[Cursor] == '_'))
		++Cursor;
	const std::string TypeName = Statement.substr(TypeStart, Cursor - TypeStart);
	if(TypeName == "bool")
	{
		UniformMeta.m_Type = EBestClientReShadeUniformType::BOOL;
		UniformMeta.m_NumComponents = 1;
	}
	else if(TypeName == "int")
	{
		UniformMeta.m_Type = EBestClientReShadeUniformType::INT;
		UniformMeta.m_NumComponents = 1;
	}
	else if(TypeName == "uint")
	{
		UniformMeta.m_Type = EBestClientReShadeUniformType::UINT;
		UniformMeta.m_NumComponents = 1;
	}
	else if(TypeName == "float")
	{
		UniformMeta.m_Type = EBestClientReShadeUniformType::FLOAT;
		UniformMeta.m_NumComponents = 1;
	}
	else if(TypeName == "float2")
	{
		UniformMeta.m_Type = EBestClientReShadeUniformType::FLOAT2;
		UniformMeta.m_NumComponents = 2;
	}
	else if(TypeName == "float3")
	{
		UniformMeta.m_Type = EBestClientReShadeUniformType::FLOAT3;
		UniformMeta.m_NumComponents = 3;
	}
	else if(TypeName == "float4")
	{
		UniformMeta.m_Type = EBestClientReShadeUniformType::FLOAT4;
		UniformMeta.m_NumComponents = 4;
		UniformMeta.m_HasAlpha = true;
	}
	else
		return false;

	while(Cursor < Statement.size() && (Statement[Cursor] == ' ' || Statement[Cursor] == '\t' || Statement[Cursor] == '\n' || Statement[Cursor] == '\r'))
		++Cursor;

	size_t NameStart = Cursor;
	while(Cursor < Statement.size() && ((Statement[Cursor] >= 'a' && Statement[Cursor] <= 'z') || (Statement[Cursor] >= 'A' && Statement[Cursor] <= 'Z') || (Statement[Cursor] >= '0' && Statement[Cursor] <= '9') || Statement[Cursor] == '_'))
		++Cursor;
	if(Cursor == NameStart)
		return false;
	UniformMeta.m_Name = Statement.substr(NameStart, Cursor - NameStart);
	UniformMeta.m_Label = UniformMeta.m_Name;

	while(Cursor < Statement.size() && (Statement[Cursor] == ' ' || Statement[Cursor] == '\t' || Statement[Cursor] == '\n' || Statement[Cursor] == '\r'))
		++Cursor;

	std::string Annotations;
	if(Cursor < Statement.size() && Statement[Cursor] == '<')
	{
		const size_t AnnotationEnd = Statement.find('>', Cursor + 1);
		if(AnnotationEnd == std::string::npos)
			return false;
		Annotations = Statement.substr(Cursor + 1, AnnotationEnd - Cursor - 1);
		Cursor = AnnotationEnd + 1;
	}

	if(!Annotations.empty())
	{
		std::string IgnoredValue;
		if(BestClientFindAnnotationValue(Annotations, "source", IgnoredValue))
			return false;
		if(BestClientFindAnnotationValue(Annotations, "hidden", IgnoredValue) && (_stricmp(IgnoredValue.c_str(), "true") == 0 || IgnoredValue == "1"))
			return false;
		if(BestClientFindAnnotationValue(Annotations, "ui_label", IgnoredValue) && !IgnoredValue.empty())
			UniformMeta.m_Label = IgnoredValue;
		if(BestClientFindAnnotationValue(Annotations, "ui_type", IgnoredValue) && _stricmp(IgnoredValue.c_str(), "color") == 0)
			UniformMeta.m_IsColor = BestClientIsReShadeUniformFloatVectorType(UniformMeta.m_Type);
		if(BestClientFindAnnotationValue(Annotations, "ui_items", IgnoredValue))
			UniformMeta.m_vComboItems = BestClientSplitReShadeUiItems(IgnoredValue);
		if(BestClientFindAnnotationValue(Annotations, "ui_min", IgnoredValue))
			UniformMeta.m_HasMin = BestClientTryParseFloatText(IgnoredValue, UniformMeta.m_Min);
		if(BestClientFindAnnotationValue(Annotations, "ui_max", IgnoredValue))
			UniformMeta.m_HasMax = BestClientTryParseFloatText(IgnoredValue, UniformMeta.m_Max);
	}

	const size_t EqualsPos = Statement.find('=', Cursor);
	const size_t SemicolonPos = Statement.rfind(';');
	if(EqualsPos != std::string::npos && SemicolonPos != std::string::npos && EqualsPos < SemicolonPos)
		UniformMeta.m_DefaultValue = BestClientTrimString(Statement.substr(EqualsPos + 1, SemicolonPos - EqualsPos - 1));

	if(UniformMeta.m_Type != EBestClientReShadeUniformType::BOOL && UniformMeta.m_vComboItems.empty())
	{
		float DefaultValue = 0.0f;
		if(BestClientIsReShadeUniformFloatVectorType(UniformMeta.m_Type))
		{
			std::array<float, 4> aDefaultValues;
			if(BestClientTryParseFloatVectorText(UniformMeta.m_DefaultValue, aDefaultValues, UniformMeta.m_NumComponents))
			{
				for(int Component = 0; Component < UniformMeta.m_NumComponents; ++Component)
					DefaultValue = maximum(DefaultValue, absolute(aDefaultValues[Component]));
			}
		}
		else if(!BestClientTryParseFloatText(UniformMeta.m_DefaultValue, DefaultValue))
		{
			int DefaultIntValue = 0;
			unsigned int DefaultUintValue = 0;
			if(BestClientTryParseIntText(UniformMeta.m_DefaultValue, DefaultIntValue))
				DefaultValue = (float)DefaultIntValue;
			else if(BestClientTryParseUintText(UniformMeta.m_DefaultValue, DefaultUintValue))
				DefaultValue = (float)DefaultUintValue;
		}

		if(UniformMeta.m_IsColor)
		{
			if(!UniformMeta.m_HasMin)
			{
				UniformMeta.m_Min = 0.0f;
				UniformMeta.m_HasMin = true;
			}
			if(!UniformMeta.m_HasMax)
			{
				UniformMeta.m_Max = 1.0f;
				UniformMeta.m_HasMax = true;
			}
		}
		else
		{
			const float RangeFallback = maximum(absolute(DefaultValue), 1.0f);
			if(!UniformMeta.m_HasMin && !UniformMeta.m_HasMax)
			{
				UniformMeta.m_Min = DefaultValue - RangeFallback;
				UniformMeta.m_Max = DefaultValue + RangeFallback;
				UniformMeta.m_HasMin = true;
				UniformMeta.m_HasMax = true;
			}
			else if(!UniformMeta.m_HasMin)
			{
				UniformMeta.m_Min = minimum(DefaultValue, UniformMeta.m_Max - 1.0f);
				UniformMeta.m_HasMin = true;
			}
			else if(!UniformMeta.m_HasMax)
			{
				UniformMeta.m_Max = maximum(DefaultValue, UniformMeta.m_Min + 1.0f);
				UniformMeta.m_HasMax = true;
			}
		}

		if(UniformMeta.m_Type == EBestClientReShadeUniformType::UINT && UniformMeta.m_Min < 0.0f)
			UniformMeta.m_Min = 0.0f;

		if(UniformMeta.m_Min > UniformMeta.m_Max)
			std::swap(UniformMeta.m_Min, UniformMeta.m_Max);
	}

	return true;
}

static std::vector<SBestClientReShadeUniformMeta> BestClientParseReShadeUniformMetadata(const std::string &ShaderText)
{
	std::vector<SBestClientReShadeUniformMeta> vUniforms;
	size_t SearchPos = 0;
	while((SearchPos = ShaderText.find("uniform", SearchPos)) != std::string::npos)
	{
		if(SearchPos > 0)
		{
			const char PreviousChar = ShaderText[SearchPos - 1];
			if((PreviousChar >= 'a' && PreviousChar <= 'z') || (PreviousChar >= 'A' && PreviousChar <= 'Z') || (PreviousChar >= '0' && PreviousChar <= '9') || PreviousChar == '_')
			{
				++SearchPos;
				continue;
			}
		}

		const size_t StatementEnd = BestClientFindReShadeUniformStatementEnd(ShaderText, SearchPos);
		if(StatementEnd == std::string::npos)
			break;

		SBestClientReShadeUniformMeta UniformMeta;
		if(BestClientParseReShadeUniformStatement(ShaderText.substr(SearchPos, StatementEnd - SearchPos + 1), UniformMeta))
			vUniforms.push_back(std::move(UniformMeta));

		SearchPos = StatementEnd + 1;
	}
	return vUniforms;
}

static const std::vector<SBestClientReShadeUniformMeta> &BestClientGetReShadeUniformMetadata(IStorage *pStorage, const std::string &EffectName)
{
	BestClientBuildReShadeEffectIndex(pStorage);

	auto UniformIt = gs_BestClientReShadeUiCache.m_UniformsByEffect.find(EffectName);
	if(UniformIt != gs_BestClientReShadeUiCache.m_UniformsByEffect.end())
		return UniformIt->second;

	std::vector<SBestClientReShadeUniformMeta> vUniforms;
	const auto EffectPathIt = gs_BestClientReShadeUiCache.m_EffectPaths.find(EffectName);
	if(EffectPathIt != gs_BestClientReShadeUiCache.m_EffectPaths.end())
	{
		std::string ShaderText;
		if(BestClientReadAbsoluteTextFile(pStorage, EffectPathIt->second.c_str(), ShaderText))
			vUniforms = BestClientParseReShadeUniformMetadata(ShaderText);
	}

	return gs_BestClientReShadeUiCache.m_UniformsByEffect.emplace(EffectName, std::move(vUniforms)).first->second;
}

static std::string BestClientGetReShadeUniformValue(const SBestClientReShadePresetState &PresetState, const std::string &EffectName, const SBestClientReShadeUniformMeta &UniformMeta)
{
	const auto SectionIt = PresetState.m_SectionValues.find(EffectName);
	if(SectionIt != PresetState.m_SectionValues.end())
	{
		const auto ValueIt = SectionIt->second.find(UniformMeta.m_Name);
		if(ValueIt != SectionIt->second.end())
			return ValueIt->second;
	}
	return UniformMeta.m_DefaultValue;
}

static void BestClientSetReShadeTechniqueEnabled(SBestClientReShadePresetState &PresetState, const std::string &Token, bool Enabled)
{
	if(Enabled)
		PresetState.m_EnabledTokens.insert(Token);
	else
		PresetState.m_EnabledTokens.erase(Token);
}

static void BestClientResetReShadeTechniqueOverrides(SBestClientReShadePresetState &PresetState, const std::string &EffectName)
{
	const auto SectionIt = PresetState.m_SectionValues.find(EffectName);
	if(SectionIt != PresetState.m_SectionValues.end())
		PresetState.m_SectionValues.erase(SectionIt);
}

static void BestClientResetReShadeTechniqueToDefaults(SBestClientReShadePresetState &PresetState, const std::string &EffectName, const std::vector<SBestClientReShadeUniformMeta> &vUniforms)
{
	BestClientResetReShadeTechniqueOverrides(PresetState, EffectName);
	for(const SBestClientReShadeUniformMeta &UniformMeta : vUniforms)
	{
		const std::string DefaultValue = BestClientTrimString(UniformMeta.m_DefaultValue);
		if(DefaultValue.empty())
			continue;
		PresetState.m_SectionValues[EffectName][UniformMeta.m_Name] = DefaultValue;
	}
}

static ColorHSLA BestClientDoColorPicker(CUi *pUi, CUi::SColorPickerPopupContext &Context, const CUIRect *pRect, unsigned int *pHslaColor, bool Alpha)
{
	ColorHSLA HslaColor = ColorHSLA(*pHslaColor, Alpha);

	ColorRGBA Outline(1.0f, 1.0f, 1.0f, 0.25f);
	Outline.a *= pUi->ButtonColorMul(pHslaColor);

	CUIRect Rect;
	pRect->Margin(3.0f, &Rect);

	pRect->Draw(Outline, IGraphics::CORNER_ALL, 4.0f);
	Rect.Draw(color_cast<ColorRGBA>(HslaColor), IGraphics::CORNER_ALL, 4.0f);

	if(pUi->DoButtonLogic(pHslaColor, 0, pRect, BUTTONFLAG_LEFT, CUi::EButtonSoundType::TOOLBAR))
	{
		Context.m_pHslaColor = pHslaColor;
		Context.m_HslaColor = HslaColor;
		Context.m_HsvaColor = color_cast<ColorHSVA>(HslaColor);
		Context.m_RgbaColor = color_cast<ColorRGBA>(Context.m_HsvaColor);
		Context.m_Alpha = Alpha;
		pUi->ShowPopupColorPicker(pUi->MouseX(), pUi->MouseY(), &Context);
	}
	else if(pUi->IsPopupOpen(&Context) && Context.m_pHslaColor == pHslaColor)
	{
		HslaColor = color_cast<ColorHSLA>(Context.m_HsvaColor);
	}

	return HslaColor;
}

static void BestClientDoColorLine(CUi *pUi, CMenus *pMenus, CUi::SColorPickerPopupContext &Context, CButtonContainer &ResetButton, CUIRect *pMainRect, const char *pText, unsigned int *pColorValue, const ColorRGBA &DefaultColor, bool Alpha)
{
	CUIRect Section, ColorPickerButton, ResetRect, Label;

	pMainRect->HSplitTop(pMainRect->h, &Section, pMainRect);
	Section.VSplitRight(60.0f, &Section, &ResetRect);
	Section.VSplitRight(8.0f, &Section, nullptr);
	Section.VSplitRight(Section.h, &Section, &ColorPickerButton);
	Section.VSplitRight(8.0f, &Label, nullptr);
	Label.VSplitLeft(Label.h + 5.0f, nullptr, &Label);

	pUi->DoLabel(&Label, pText, 14.0f, TEXTALIGN_ML);
	BestClientDoColorPicker(pUi, Context, &ColorPickerButton, pColorValue, Alpha);

	ResetRect.HMargin(2.0f, &ResetRect);
	if(pMenus->DoButton_Menu(&ResetButton, BCLocalize("Reset"), 0, &ResetRect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.1f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f)))
		*pColorValue = color_cast<ColorHSLA>(DefaultColor).Pack(Alpha);
}

static std::string BestClientFormatReShadeFloat(float Value)
{
	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), "%.6f", Value);
	return aBuf;
}

static bool BestClientSaveReShadeRuntimeSetting(IConfigManager *pConfigManager, bool EnableRuntime, char *pError, int ErrorSize)
{
	if(pError != nullptr && ErrorSize > 0)
		pError[0] = '\0';

	if(pConfigManager == nullptr)
	{
		str_copy(pError, "Failed to access config manager.", ErrorSize);
		return false;
	}

	const int OldReShadeEnabled = g_Config.m_BcReshadeEnabled;
	g_Config.m_BcReshadeEnabled = EnableRuntime ? 1 : 0;
	if(!pConfigManager->Save())
	{
		g_Config.m_BcReshadeEnabled = OldReShadeEnabled;
		str_copy(pError, "Failed to save BestClient settings.", ErrorSize);
		return false;
	}

	return true;
}

static bool BestClientQueryReShadeLiveAvailability(IStorage *pStorage, IGraphics *pGraphics, char *pError, int ErrorSize)
{
	if(pError != nullptr && ErrorSize > 0)
		pError[0] = '\0';

	if(pGraphics == nullptr || str_find_nocase(pGraphics->GetVersionString(), "vulkan") == nullptr)
	{
		str_copy(pError, "DDNet is not running on the Vulkan renderer.", ErrorSize);
		return false;
	}

	char aLayerDllPath[IO_MAX_PATH_LENGTH];
	char aLayerManifestPath[IO_MAX_PATH_LENGTH];
	char aDisabledLayerManifestPath[IO_MAX_PATH_LENGTH];
	pStorage->GetBinaryPath(gs_pBestClientReShadeLayerDllFilename, aLayerDllPath, sizeof(aLayerDllPath));
	pStorage->GetBinaryPath(gs_pBestClientReShadeLayerManifestFilename, aLayerManifestPath, sizeof(aLayerManifestPath));
	pStorage->GetBinaryPath(gs_pBestClientReShadeLayerDisabledManifestFilename, aDisabledLayerManifestPath, sizeof(aDisabledLayerManifestPath));

	if(!BestClientFileExistsAbsolute(aLayerDllPath))
	{
		str_copy(pError, "ReShade64.dll is missing next to DDNet.exe.", ErrorSize);
		return false;
	}
	if(!BestClientFileExistsAbsolute(aLayerManifestPath) && !BestClientFileExistsAbsolute(aDisabledLayerManifestPath))
	{
		str_copy(pError, "ReShade64.json is missing next to DDNet.exe.", ErrorSize);
		return false;
	}
	if(g_Config.m_BcReshadeEnabled == 0)
	{
		str_copy(pError, "ReShade is disabled in BestClient settings. Enable it and restart the game first.", ErrorSize);
		return false;
	}

	return BestClientReShadeRuntimeCommitPreset(pStorage, pError, ErrorSize);
}

static void RenderSettingsBestClientReShadeTab(CMenus *pMenus, IStorage *pStorage, ITextRender *pTextRender, CUi *pUi, IClient *pClient, IGraphics *pGraphics, CUIRect MainView)
{
	const float LineSize = 20.0f;
	const float MarginSmall = 4.0f;
	const float MarginMedium = 8.0f;
	const float MarginLarge = 10.0f;
	const float HeaderLineSize = 24.0f;
	const float SearchLabelWidth = 52.0f;
	const float EditBoxFontSize = 14.0f;
	const float PanelRadius = 12.0f;
	const float CardRadius = 10.0f;
	const float IconButtonSize = 14.0f;
	const float ControlsLineSize = 22.0f;
	const bool IsVulkanBackend = str_find_nocase(pGraphics->GetVersionString(), "vulkan") != nullptr;

	struct SUniformUiState
	{
		int m_aIds[4] = {0, 0, 0, 0};
		CUi::SDropDownState m_DropDownState;
		CScrollRegion m_DropDownScrollRegion;
		CButtonContainer m_ColorResetButton;
		CUi::SColorPickerPopupContext m_ColorPickerPopupContext;
	};
	struct STechniqueUiState
	{
		CButtonContainer m_ExpandButton;
		CButtonContainer m_ResetButton;
		CButtonContainer m_AddButton;
		CButtonContainer m_RemoveButton;
		int m_aIds[2] = {0, 0};
		bool m_Expanded = false;
		std::unordered_map<std::string, SUniformUiState> m_UniformStates;
	};

	static CLineInputBuffered<128> s_SearchInput;
	static int s_AvailableSort = BESTCLIENT_RESHADE_SORT_NAME_ASC;
	static int s_RuntimeEnabledToggle = 0;
	static bool s_HasReShadeSessionEnabled = false;
	static bool s_ReShadeSessionEnabled = false;
	static CScrollRegion s_AvailableScrollRegion;
	static CScrollRegion s_AddedScrollRegion;
	static CUi::SDropDownState s_AvailableSortState;
	static CScrollRegion s_AvailableSortScrollRegion;
	static std::unordered_map<std::string, STechniqueUiState> s_TechniqueUiStates;
	static std::string s_PendingAcceptToken;
	static std::string s_PendingAcceptTechniqueName;
	static int64_t s_PendingAcceptStartTick = 0;
	static bool s_PendingAcceptRemovesTechnique = false;
	static CUi::SConfirmPopupContext s_AcceptPopupContext;
	static SBestClientReShadePresetState s_PendingSavePresetState;
	static bool s_HasPendingSavePreset = false;
	static int64_t s_PendingSavePresetTick = 0;
	static SBestClientReShadePresetState s_PendingLivePresetState;
	static bool s_HasPendingLivePreset = false;
	static int64_t s_PendingLivePresetTick = 0;
	static uint64_t s_LiveRevision = 0;
	static uint64_t s_PendingLiveRevision = 0;

	const char *apSortModes[NUM_BESTCLIENT_RESHADE_SORTS] = {
		BCLocalize("Name A-Z"),
		BCLocalize("Name Z-A"),
		BCLocalize("Effect file"),
	};

	char aLayerDllPath[IO_MAX_PATH_LENGTH];
	char aLayerManifestPath[IO_MAX_PATH_LENGTH];
	char aDisabledLayerManifestPath[IO_MAX_PATH_LENGTH];
	pStorage->GetBinaryPath(gs_pBestClientReShadeLayerDllFilename, aLayerDllPath, sizeof(aLayerDllPath));
	pStorage->GetBinaryPath(gs_pBestClientReShadeLayerManifestFilename, aLayerManifestPath, sizeof(aLayerManifestPath));
	pStorage->GetBinaryPath(gs_pBestClientReShadeLayerDisabledManifestFilename, aDisabledLayerManifestPath, sizeof(aDisabledLayerManifestPath));
	const bool HasReShadeLayerDll = BestClientFileExistsAbsolute(aLayerDllPath);
	const bool HasReShadeLayerManifest = BestClientFileExistsAbsolute(aLayerManifestPath);
	const bool HasReShadeLayerDisabledManifest = BestClientFileExistsAbsolute(aDisabledLayerManifestPath);
	const bool HasReShadeRuntimeFiles = HasReShadeLayerDll && (HasReShadeLayerManifest || HasReShadeLayerDisabledManifest);
	const bool ReShadeConfiguredEnabled = g_Config.m_BcReshadeEnabled != 0;
	if(!s_HasReShadeSessionEnabled)
	{
		s_ReShadeSessionEnabled = HasReShadeRuntimeFiles && ReShadeConfiguredEnabled;
		s_HasReShadeSessionEnabled = true;
	}
	const bool ReShadeRuntimeEnabled = HasReShadeRuntimeFiles && s_ReShadeSessionEnabled;
	const bool NeedReShadeRestart = HasReShadeRuntimeFiles && ReShadeConfiguredEnabled != s_ReShadeSessionEnabled;
	CUIRect RestartBar;
	if(NeedReShadeRestart)
		MainView.HSplitBottom(20.0f, &MainView, &RestartBar);

	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	CUIRect LeftColumn, RightColumn;
	MainView.VSplitMid(&LeftColumn, &RightColumn, MarginLarge);

	CUIRect ControlsPanel, AvailablePanel;
	LeftColumn.HSplitTop(148.0f, &ControlsPanel, &LeftColumn);
	LeftColumn.HSplitTop(MarginMedium, nullptr, &LeftColumn);
	AvailablePanel = LeftColumn;

	auto DrawPanel = [&](const CUIRect &Rect) {
		Rect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.32f), IGraphics::CORNER_ALL, PanelRadius);
	};

	auto DrawPanelMessage = [&](CUIRect PanelRect, const char *pTitle, const char *pBody, bool Error) {
		DrawPanel(PanelRect);
		PanelRect.VMargin(18.0f, &PanelRect);
		PanelRect.HMargin(18.0f, &PanelRect);

		CUIRect TitleRect, BodyRect;
		PanelRect.HSplitTop(32.0f, &TitleRect, &PanelRect);
		PanelRect.HSplitTop(MarginMedium, nullptr, &PanelRect);
		PanelRect.HSplitTop(84.0f, &BodyRect, &PanelRect);

		if(Error)
			pTextRender->TextColor(1.0f, 0.45f, 0.45f, 1.0f);
		pUi->DoLabel(&TitleRect, pTitle, 22.0f, TEXTALIGN_ML);
		pTextRender->TextColor(pTextRender->DefaultTextColor());
		pUi->DoLabel(&BodyRect, pBody, 14.0f, TEXTALIGN_ML);
	};
	auto RenderRestartWarning = [&](CUIRect RestartBarRect) {
		CUIRect RestartWarning, RestartButton;
		RestartBarRect.VSplitRight(125.0f, &RestartWarning, &RestartButton);
		RestartWarning.VSplitRight(10.0f, &RestartWarning, nullptr);
		pUi->DoLabel(&RestartWarning, Localize("You must restart the game for all settings to take effect."), 14.0f, TEXTALIGN_ML);

		static CButtonContainer s_RestartButton;
		if(pMenus->DoButton_Menu(&s_RestartButton, Localize("Restart"), 0, &RestartButton))
			pClient->Restart();
	};

	{
		DrawPanel(ControlsPanel);
		CUIRect Inner = ControlsPanel;
		Inner.VMargin(10.0f, &Inner);
		Inner.HMargin(10.0f, &Inner);

		CUIRect TitleRow, RuntimeRow, AutoAcceptRow, FilterRow, BindRow;
		Inner.HSplitTop(HeaderLineSize, &TitleRow, &Inner);
		Inner.HSplitTop(MarginSmall, nullptr, &Inner);
		Inner.HSplitTop(ControlsLineSize, &RuntimeRow, &Inner);
		Inner.HSplitTop(MarginSmall, nullptr, &Inner);
		Inner.HSplitTop(ControlsLineSize, &AutoAcceptRow, &Inner);
		Inner.HSplitTop(MarginSmall, nullptr, &Inner);
		Inner.HSplitTop(ControlsLineSize, &FilterRow, &Inner);
		Inner.HSplitTop(MarginSmall, nullptr, &Inner);
		Inner.HSplitTop(ControlsLineSize, &BindRow, &Inner);

		{
			CUIRect TitleLabel, BadgeSlot, Badge;
			TitleRow.VSplitLeft(pTextRender->TextWidth(18.0f, BCLocalize("Live-Shaders controls")) + 8.0f, &TitleLabel, &BadgeSlot);
			pUi->DoLabel(&TitleLabel, BCLocalize("Live-Shaders controls"), 18.0f, TEXTALIGN_ML);
			BadgeSlot.VSplitLeft(52.0f, &BadgeSlot, nullptr);
			BadgeSlot.HMargin(1.5f, &Badge);
			pGraphics->DrawRect4(
				Badge.x, Badge.y, Badge.w, Badge.h,
				ColorRGBA(0.85f, 0.15f, 0.15f, 1.0f),
				ColorRGBA(0.65f, 0.05f, 0.05f, 1.0f),
				ColorRGBA(0.85f, 0.15f, 0.15f, 1.0f),
				ColorRGBA(0.65f, 0.05f, 0.05f, 1.0f),
				IGraphics::CORNER_ALL, 5.0f);
			pUi->DoLabel(&Badge, "BETA", 11.0f, TEXTALIGN_MC);
		}

		int RuntimeValue = ReShadeConfiguredEnabled ? 1 : 0;
		if(pMenus->DoButton_CheckBox(&s_RuntimeEnabledToggle, BCLocalize("Enable Live-Shaders on startup (restart required)"), RuntimeValue, &RuntimeRow))
		{
			char aRestartError[256];
			if(!BestClientSaveReShadeRuntimeSetting(pMenus->MenuGameClient()->ConfigManager(), !ReShadeConfiguredEnabled, aRestartError, sizeof(aRestartError)))
			{
				gs_BestClientReShadeUiCache.m_StatusText = aRestartError;
				gs_BestClientReShadeUiCache.m_StatusIsError = true;
			}
			else
			{
				gs_BestClientReShadeUiCache.m_StatusText = BCLocalize("Saved Live-Shaders startup state. Restart the game to apply it.");
				gs_BestClientReShadeUiCache.m_StatusIsError = false;
			}
		}

		pMenus->DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcReshadeAutoAccept, BCLocalize("Auto accept"), &g_Config.m_BcReshadeAutoAccept, &AutoAcceptRow, ControlsLineSize);
		pMenus->DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcReshadeShowOnlyEnabled, BCLocalize("Show only enabled on the right"), &g_Config.m_BcReshadeShowOnlyEnabled, &FilterRow, ControlsLineSize);

		{
			static CButtonContainer s_ToggleBindReaderButton;
			static CButtonContainer s_ToggleBindClearButton;
			const float BindLabelWidth = 120.0f;
			CUIRect BindLabel, BindReader;
			BindRow.VSplitLeft(BindLabelWidth, &BindLabel, &BindRow);
			BindRow.VSplitLeft(MarginSmall, nullptr, &BindRow);
			BindRow.VSplitLeft(minimum(120.0f, BindRow.w), &BindReader, &BindRow);

			pUi->DoLabel(&BindLabel, BCLocalize("Toggle effects bind"), 13.0f, TEXTALIGN_ML);

			const CBindSlot ToggleBind = [&]() -> CBindSlot {
				for(int Mod = KeyModifier::NONE; Mod < KeyModifier::COMBINATION_COUNT; Mod++)
					for(int Key = KEY_FIRST; Key < KEY_LAST; Key++)
						if(str_comp(pMenus->MenuGameClient()->m_Binds.Get(Key, Mod), "BC_reshade_toggle_effects") == 0)
							return CBindSlot(Key, Mod);
				return EMPTY_BIND_SLOT;
			}();

			const CKeyBinder::CKeyReaderResult BindResult = pMenus->MenuGameClient()->m_KeyBinder.DoKeyReader(
				&s_ToggleBindReaderButton, &s_ToggleBindClearButton, &BindReader, ToggleBind, false);

			if(BindResult.m_Bind != ToggleBind && !BindResult.m_Aborted)
			{
				if(ToggleBind != EMPTY_BIND_SLOT)
					pMenus->MenuGameClient()->m_Binds.Bind(ToggleBind.m_Key, "", false, ToggleBind.m_ModifierMask);
				if(BindResult.m_Bind != EMPTY_BIND_SLOT)
					pMenus->MenuGameClient()->m_Binds.Bind(BindResult.m_Bind.m_Key, "BC_reshade_toggle_effects", false, BindResult.m_Bind.m_ModifierMask);
			}
		}
	}

	if(!HasReShadeRuntimeFiles)
	{
		DrawPanelMessage(AvailablePanel, BCLocalize("Live-Shaders runtime files are missing"), BCLocalize("This build does not contain the bundled ReShade64 runtime files. Repack the client with ReShade64.dll and ReShade64.json."), true);
		DrawPanelMessage(RightColumn, BCLocalize("Added effects"), BCLocalize("The Live-Shaders tab will stay unavailable until the portable ReShade runtime files are present next to the game executable."), false);
		if(NeedReShadeRestart)
			RenderRestartWarning(RestartBar);
		return;
	}

	if(!ReShadeRuntimeEnabled)
	{
		DrawPanelMessage(AvailablePanel, BCLocalize("Live-Shaders is disabled"), BCLocalize("Enable Live-Shaders with the checkbox above and restart the game. Until then this tab stays inactive."), false);
		DrawPanelMessage(RightColumn, BCLocalize("Added effects"), BCLocalize("To manage shaders here, first enable Live-Shaders above and restart the game."), false);
		if(NeedReShadeRestart)
			RenderRestartWarning(RestartBar);
		return;
	}

	if(!IsVulkanBackend)
	{
		DrawPanelMessage(AvailablePanel, BCLocalize("Vulkan is required"), BCLocalize("Switch the graphics backend to Vulkan in the client settings and restart the game to use the Live-Shaders tab."), true);
		DrawPanelMessage(RightColumn, BCLocalize("Added effects"), BCLocalize("Effect controls are available only when the client is running on the Vulkan renderer."), false);
		if(NeedReShadeRestart)
			RenderRestartWarning(RestartBar);
		return;
	}

	char aPresetError[256];
	SBestClientReShadePresetState PresetState;
	if(!BestClientLoadReShadePreset(pStorage, PresetState, aPresetError, sizeof(aPresetError)))
	{
		DrawPanelMessage(AvailablePanel, BCLocalize("Failed to load preset"), aPresetError, true);
		DrawPanelMessage(RightColumn, BCLocalize("Added effects"), BCLocalize("The right panel will be available again once the preset can be read."), false);
		if(NeedReShadeRestart)
			RenderRestartWarning(RestartBar);
		return;
	}

	if(s_HasPendingSavePreset)
		PresetState = s_PendingSavePresetState;

	BestClientBuildReShadeTechniqueIndex(pStorage);
	const std::vector<SBestClientReShadeTechniqueMeta> &vTechniqueCatalog = gs_BestClientReShadeUiCache.m_vTechniqueIndex;
	const char *pSearch = s_SearchInput.GetString();
	const int64_t NowTick = time_get();
	const int64_t AcceptTimeout = time_freq() * 5;

	SBestClientReShadePresetState EditedPresetState;
	bool HasPresetChanges = false;
	auto EnsureEditedPreset = [&]() -> SBestClientReShadePresetState & {
		if(!HasPresetChanges)
		{
			EditedPresetState = PresetState;
			HasPresetChanges = true;
		}
		return EditedPresetState;
	};
	auto ClearPendingAccept = [&]() {
		s_PendingAcceptToken.clear();
		s_PendingAcceptTechniqueName.clear();
		s_PendingAcceptStartTick = 0;
		s_PendingAcceptRemovesTechnique = false;
		s_AcceptPopupContext.Reset();
	};
	auto CurrentPresetState = [&]() -> const SBestClientReShadePresetState & {
		return HasPresetChanges ? EditedPresetState : PresetState;
	};
	auto SetStatus = [&](const char *pText, bool Error) {
		gs_BestClientReShadeUiCache.m_StatusText = pText;
		gs_BestClientReShadeUiCache.m_StatusIsError = Error;
	};
	auto FormatAcceptMessage = [&](const std::string &TechniqueName, int SecondsRemaining, bool RemoveOnReject) {
		char aMessage[512];
		if(RemoveOnReject)
			str_format(aMessage, sizeof(aMessage), BCLocalize("Confirm keeping \"%s\" enabled within %d seconds. Otherwise it will be removed automatically."), TechniqueName.c_str(), SecondsRemaining);
		else
			str_format(aMessage, sizeof(aMessage), BCLocalize("Confirm keeping \"%s\" enabled within %d seconds. Otherwise it will be disabled automatically."), TechniqueName.c_str(), SecondsRemaining);
		return std::string(aMessage);
	};
	auto RejectPendingAccept = [&](bool TimedOut, bool UpdateStatus) {
		if(s_PendingAcceptToken.empty())
			return;

		if(s_PendingAcceptRemovesTechnique)
		{
			BestClientRemoveReShadeTechniqueFromPreset(EnsureEditedPreset(), s_PendingAcceptToken);
			if(UpdateStatus)
			{
				if(TimedOut)
					SetStatus(BCLocalize("The effect was removed automatically because it was not confirmed in time."), true);
				else
					SetStatus(BCLocalize("The effect was removed because it was not confirmed."), true);
			}
		}
		else
		{
			BestClientSetReShadeTechniqueEnabled(EnsureEditedPreset(), s_PendingAcceptToken, false);
			if(UpdateStatus)
			{
				if(TimedOut)
					SetStatus(BCLocalize("The effect was disabled automatically because it was not confirmed in time."), true);
				else
					SetStatus(BCLocalize("The effect was disabled because it was not confirmed."), true);
			}
		}
	};
	auto ShowAcceptPopup = [&](const std::string &TechniqueName, int SecondsRemaining) {
		s_AcceptPopupContext.Reset();
		s_AcceptPopupContext.YesNoButtons();
		str_copy(s_AcceptPopupContext.m_aPositiveButtonLabel, BCLocalize("Keep enabled"), sizeof(s_AcceptPopupContext.m_aPositiveButtonLabel));
		str_copy(s_AcceptPopupContext.m_aNegativeButtonLabel, s_PendingAcceptRemovesTechnique ? BCLocalize("Remove") : BCLocalize("Disable"), sizeof(s_AcceptPopupContext.m_aNegativeButtonLabel));
		const std::string Message = FormatAcceptMessage(TechniqueName, SecondsRemaining, s_PendingAcceptRemovesTechnique);
		str_copy(s_AcceptPopupContext.m_aMessage, Message.c_str(), sizeof(s_AcceptPopupContext.m_aMessage));
		pUi->ShowPopupConfirm(pUi->Screen()->x + pUi->Screen()->w * 0.5f, pUi->Screen()->y + pUi->Screen()->h * 0.35f, &s_AcceptPopupContext);
	};

	if(!s_PendingAcceptToken.empty())
	{
		const int SecondsRemaining = maximum(0, 5 - (int)((NowTick - s_PendingAcceptStartTick) / time_freq()));
		if(s_AcceptPopupContext.m_Result == CUi::SConfirmPopupContext::CONFIRMED)
		{
			SetStatus(BCLocalize("The effect was confirmed and will stay enabled."), false);
			ClearPendingAccept();
		}
		else if(s_AcceptPopupContext.m_Result == CUi::SConfirmPopupContext::CANCELED)
		{
			RejectPendingAccept(false, true);
			ClearPendingAccept();
		}
		else if(pUi->IsPopupOpen(&s_AcceptPopupContext))
		{
			const std::string Message = FormatAcceptMessage(s_PendingAcceptTechniqueName, SecondsRemaining, s_PendingAcceptRemovesTechnique);
			str_copy(s_AcceptPopupContext.m_aMessage, Message.c_str(), sizeof(s_AcceptPopupContext.m_aMessage));
		}
		else if(s_AcceptPopupContext.m_Result == CUi::SConfirmPopupContext::UNSET)
		{
			ShowAcceptPopup(s_PendingAcceptTechniqueName, SecondsRemaining);
		}

		if(CurrentPresetState().m_EnabledTokens.find(s_PendingAcceptToken) == CurrentPresetState().m_EnabledTokens.end())
		{
			ClearPendingAccept();
		}
		else if(s_PendingAcceptStartTick > 0 && NowTick - s_PendingAcceptStartTick >= AcceptTimeout)
		{
			RejectPendingAccept(true, true);
			pUi->ClosePopupMenu(&s_AcceptPopupContext);
			ClearPendingAccept();
		}
	}

	if(g_Config.m_BcReshadeAutoAccept != 0 && !s_PendingAcceptToken.empty())
		ClearPendingAccept();

	std::vector<SBestClientReShadeTechniqueMeta> vAddedTechniques = BestClientBuildReShadeTechniqueList(CurrentPresetState());
	std::unordered_set<std::string> AddedTokens;
	AddedTokens.reserve(CurrentPresetState().m_vTechniqueSorting.size());
	for(const std::string &Token : CurrentPresetState().m_vTechniqueSorting)
		AddedTokens.insert(Token);

	std::vector<SBestClientReShadeTechniqueMeta> vAvailableTechniques;
	vAvailableTechniques.reserve(vTechniqueCatalog.size());
	for(const SBestClientReShadeTechniqueMeta &Technique : vTechniqueCatalog)
	{
		if(AddedTokens.find(Technique.m_Token) != AddedTokens.end())
			continue;
		if(pSearch[0] != '\0' &&
			str_find_nocase(Technique.m_TechniqueName.c_str(), pSearch) == nullptr &&
			str_find_nocase(Technique.m_EffectName.c_str(), pSearch) == nullptr &&
			str_find_nocase(Technique.m_Token.c_str(), pSearch) == nullptr)
		{
			continue;
		}
		vAvailableTechniques.push_back(Technique);
	}

	static const std::array<const char *, 6> s_apPinnedReShadeEffects = {
		"ASCII",
		"CShade_Lens",
		"DeepFry",
		"MagicHDR",
		"Composite",
		"PaletteMap",
	};
	auto GetPinnedTechniquePriority = [&](const SBestClientReShadeTechniqueMeta &Technique) {
		auto MatchesPinnedEffect = [&](const char *pPinnedEffect) {
			const int PinnedLength = str_length(pPinnedEffect);
			if(str_comp_nocase(Technique.m_TechniqueName.c_str(), pPinnedEffect) == 0)
				return true;
			if(str_comp_nocase(Technique.m_EffectName.c_str(), pPinnedEffect) == 0)
				return true;
			if((int)Technique.m_EffectName.size() > PinnedLength &&
				Technique.m_EffectName[PinnedLength] == '.' &&
				str_comp_nocase_num(Technique.m_EffectName.c_str(), pPinnedEffect, PinnedLength) == 0)
			{
				return true;
			}
			if((int)Technique.m_Token.size() > PinnedLength &&
				Technique.m_Token[PinnedLength] == '@' &&
				str_comp_nocase_num(Technique.m_Token.c_str(), pPinnedEffect, PinnedLength) == 0)
			{
				return true;
			}
			return false;
		};

		for(size_t Index = 0; Index < s_apPinnedReShadeEffects.size(); ++Index)
		{
			if(MatchesPinnedEffect(s_apPinnedReShadeEffects[Index]))
				return (int)Index;
		}
		return -1;
	};

	std::sort(vAvailableTechniques.begin(), vAvailableTechniques.end(), [&](const SBestClientReShadeTechniqueMeta &Left, const SBestClientReShadeTechniqueMeta &Right) {
		const int LeftPinnedPriority = GetPinnedTechniquePriority(Left);
		const int RightPinnedPriority = GetPinnedTechniquePriority(Right);
		if(LeftPinnedPriority != RightPinnedPriority)
		{
			if(LeftPinnedPriority < 0)
				return false;
			if(RightPinnedPriority < 0)
				return true;
			return LeftPinnedPriority < RightPinnedPriority;
		}

		if(s_AvailableSort == BESTCLIENT_RESHADE_SORT_EFFECT_ASC)
		{
			const int EffectCompare = str_comp_nocase(Left.m_EffectName.c_str(), Right.m_EffectName.c_str());
			if(EffectCompare != 0)
				return EffectCompare < 0;
		}

		const int NameCompare = str_comp_nocase(Left.m_TechniqueName.c_str(), Right.m_TechniqueName.c_str());
		if(NameCompare != 0)
		{
			if(s_AvailableSort == BESTCLIENT_RESHADE_SORT_NAME_DESC)
				return NameCompare > 0;
			return NameCompare < 0;
		}

		return str_comp_nocase(Left.m_EffectName.c_str(), Right.m_EffectName.c_str()) < 0;
	});

	int NumVisibleAdded = 0;
	for(const SBestClientReShadeTechniqueMeta &Technique : vAddedTechniques)
	{
		const bool Enabled = CurrentPresetState().m_EnabledTokens.find(Technique.m_Token) != CurrentPresetState().m_EnabledTokens.end();
		if(g_Config.m_BcReshadeShowOnlyEnabled == 0 || Enabled)
			++NumVisibleAdded;
	}

	auto GetUniformRowCount = [](const SBestClientReShadeUniformMeta &UniformMeta) {
		if(UniformMeta.m_IsColor)
			return 1;
		if(!UniformMeta.m_vComboItems.empty())
			return 2;
		if(BestClientIsReShadeUniformFloatVectorType(UniformMeta.m_Type))
			return UniformMeta.m_NumComponents;
		return 1;
	};

	{
		DrawPanel(AvailablePanel);
		CUIRect AvailableInner = AvailablePanel;
		AvailableInner.VMargin(14.0f, &AvailableInner);
		AvailableInner.HMargin(14.0f, &AvailableInner);

		CUIRect HeaderRow, SortRow, SearchRow, StatusRow, ListArea;
		AvailableInner.HSplitTop(HeaderLineSize, &HeaderRow, &AvailableInner);
		AvailableInner.HSplitTop(MarginSmall, nullptr, &AvailableInner);
		AvailableInner.HSplitTop(LineSize, &SortRow, &AvailableInner);
		AvailableInner.HSplitTop(MarginSmall, nullptr, &AvailableInner);
		AvailableInner.HSplitTop(LineSize, &SearchRow, &AvailableInner);
		AvailableInner.HSplitTop(MarginSmall, nullptr, &AvailableInner);
		AvailableInner.HSplitTop(LineSize, &StatusRow, &AvailableInner);
		AvailableInner.HSplitTop(MarginSmall, nullptr, &ListArea);

		char aAvailableTitle[128];
		str_format(aAvailableTitle, sizeof(aAvailableTitle), "%s (%d)", BCLocalize("Available shaders"), (int)vAvailableTechniques.size());
		pUi->DoLabel(&HeaderRow, aAvailableTitle, 18.0f, TEXTALIGN_ML);

		{
			CUIRect SortLabel, SortDropDown;
			SortRow.VSplitLeft(SearchLabelWidth, &SortLabel, &SortDropDown);
			pUi->DoLabel(&SortLabel, BCLocalize("Sort"), 14.0f, TEXTALIGN_ML);
			s_AvailableSortState.m_SelectionPopupContext.m_pScrollRegion = &s_AvailableSortScrollRegion;
			s_AvailableSort = pUi->DoDropDown(&SortDropDown, std::clamp(s_AvailableSort, 0, NUM_BESTCLIENT_RESHADE_SORTS - 1), apSortModes, NUM_BESTCLIENT_RESHADE_SORTS, s_AvailableSortState);
		}

		{
			CUIRect SearchLabel, SearchEdit;
			SearchRow.VSplitLeft(SearchLabelWidth, &SearchLabel, &SearchEdit);
			pUi->DoLabel(&SearchLabel, BCLocalize("Search"), 14.0f, TEXTALIGN_ML);
			pUi->DoClearableEditBox(&s_SearchInput, &SearchEdit, EditBoxFontSize);
		}

		{
			char aStatus[256];
			if(gs_BestClientReShadeUiCache.m_StatusText.empty())
				str_format(aStatus, sizeof(aStatus), "Added: %d  |  Available: %d", (int)vAddedTechniques.size(), (int)vAvailableTechniques.size());
			else
				str_copy(aStatus, gs_BestClientReShadeUiCache.m_StatusText.c_str(), sizeof(aStatus));

			if(gs_BestClientReShadeUiCache.m_StatusIsError)
				pTextRender->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
			else if(!gs_BestClientReShadeUiCache.m_StatusText.empty())
				pTextRender->TextColor(0.55f, 1.0f, 0.55f, 1.0f);
			pUi->DoLabel(&StatusRow, aStatus, 12.0f, TEXTALIGN_ML);
			pTextRender->TextColor(pTextRender->DefaultTextColor());
		}

		vec2 AvailableScrollOffset(0.0f, 0.0f);
		CScrollRegionParams AvailableScrollParams;
		AvailableScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
		s_AvailableScrollRegion.Begin(&ListArea, &AvailableScrollOffset, &AvailableScrollParams);
		auto RenderPinnedTechniqueStar = [&](const CUIRect &Rect) {
			pTextRender->SetFontPreset(EFontPreset::ICON_FONT);
			pTextRender->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
			pTextRender->TextColor(ColorRGBA(1.0f, 0.85f, 0.3f, 0.8f));
			SLabelProperties Props;
			Props.m_MaxWidth = Rect.w;
			pUi->DoLabel(&Rect, FontIcon::STAR, 12.0f, TEXTALIGN_MC, Props);
			pTextRender->TextColor(pTextRender->DefaultTextColor());
			pTextRender->SetRenderFlags(0);
			pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);
		};

		CUIRect Content = ListArea;
		Content.y += AvailableScrollOffset.y;
		for(const SBestClientReShadeTechniqueMeta &Technique : vAvailableTechniques)
		{
			STechniqueUiState &TechniqueUi = s_TechniqueUiStates[Technique.m_Token];
			const bool IsPinnedTechnique = GetPinnedTechniquePriority(Technique) >= 0;
			CUIRect ItemRect;
			Content.HSplitTop(44.0f, &ItemRect, &Content);
			Content.HSplitTop(MarginSmall, nullptr, &Content);
			if(!s_AvailableScrollRegion.AddRect(ItemRect))
				continue;

			ItemRect.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f), IGraphics::CORNER_ALL, 8.0f);

			CUIRect ItemInner = ItemRect;
			ItemInner.VMargin(8.0f, &ItemInner);
			ItemInner.HMargin(8.0f, &ItemInner);

			CUIRect AddButtonRect, PinnedIconRect;
			ItemInner.VSplitRight(IconButtonSize, &ItemInner, &AddButtonRect);
			if(IsPinnedTechnique)
				ItemInner.VSplitRight(IconButtonSize, &ItemInner, &PinnedIconRect);

			CUIRect NameRow, EffectRow;
			ItemInner.HSplitTop(LineSize, &NameRow, &ItemInner);
			ItemInner.HSplitTop(12.0f, &EffectRow, &ItemInner);
			pUi->DoLabel(&NameRow, Technique.m_TechniqueName.c_str(), 14.0f, TEXTALIGN_ML);

			char aEffectLabel[256];
			str_format(aEffectLabel, sizeof(aEffectLabel), "Effect file: %s", Technique.m_EffectName.c_str());
			pTextRender->TextColor(1.0f, 1.0f, 1.0f, 0.7f);
			pUi->DoLabel(&EffectRow, aEffectLabel, 10.0f, TEXTALIGN_ML);
			pTextRender->TextColor(pTextRender->DefaultTextColor());
			if(IsPinnedTechnique)
				RenderPinnedTechniqueStar(PinnedIconRect);

			if(pUi->DoButton_FontIcon(&TechniqueUi.m_AddButton, FontIcon::PLUS, 0, &AddButtonRect, BUTTONFLAG_LEFT))
			{
				BestClientAddReShadeTechniqueToPreset(EnsureEditedPreset(), Technique.m_Token, vTechniqueCatalog);
				s_TechniqueUiStates[Technique.m_Token].m_Expanded = false;
				if(g_Config.m_BcReshadeAutoAccept == 0)
				{
					if(!s_PendingAcceptToken.empty() && s_PendingAcceptToken != Technique.m_Token)
						RejectPendingAccept(false, false);
					s_PendingAcceptToken = Technique.m_Token;
					s_PendingAcceptTechniqueName = Technique.m_TechniqueName;
					s_PendingAcceptStartTick = NowTick;
					s_PendingAcceptRemovesTechnique = true;
					ShowAcceptPopup(Technique.m_TechniqueName, 5);
					SetStatus(BCLocalize("The effect is live. Confirm it within 5 seconds to keep it enabled."), false);
				}
				else
				{
					SetStatus(BCLocalize("The effect was added to the configured list."), false);
				}
			}
		}
		if(vAvailableTechniques.empty())
		{
			CUIRect EmptyRect;
			Content.HSplitTop(40.0f, &EmptyRect, &Content);
			if(s_AvailableScrollRegion.AddRect(EmptyRect))
				pUi->DoLabel(&EmptyRect, BCLocalize("No available shaders match the current search."), 13.0f, TEXTALIGN_ML);
		}
		s_AvailableScrollRegion.End();
	}

	{
		DrawPanel(RightColumn);
		CUIRect RightInner = RightColumn;
		RightInner.VMargin(16.0f, &RightInner);
		RightInner.HMargin(16.0f, &RightInner);

		CUIRect HeaderRow, ListArea;
		RightInner.HSplitTop(HeaderLineSize, &HeaderRow, &RightInner);
		RightInner.HSplitTop(MarginMedium, nullptr, &RightInner);
		ListArea = RightInner;

		char aAddedTitle[128];
		str_format(aAddedTitle, sizeof(aAddedTitle), "%s (%d)", BCLocalize("Added effects"), NumVisibleAdded);
		pUi->DoLabel(&HeaderRow, aAddedTitle, 18.0f, TEXTALIGN_ML);

		vec2 AddedScrollOffset(0.0f, 0.0f);
		CScrollRegionParams AddedScrollParams;
		AddedScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
		s_AddedScrollRegion.Begin(&ListArea, &AddedScrollOffset, &AddedScrollParams);

		CUIRect Content = ListArea;
		Content.y += AddedScrollOffset.y;
		for(const SBestClientReShadeTechniqueMeta &Technique : vAddedTechniques)
		{
			STechniqueUiState &TechniqueUi = s_TechniqueUiStates[Technique.m_Token];
			const bool Enabled = CurrentPresetState().m_EnabledTokens.find(Technique.m_Token) != CurrentPresetState().m_EnabledTokens.end();
			if(g_Config.m_BcReshadeShowOnlyEnabled != 0 && !Enabled)
				continue;

			const std::vector<SBestClientReShadeUniformMeta> *pUniforms = nullptr;
			if(TechniqueUi.m_Expanded)
				pUniforms = &BestClientGetReShadeUniformMetadata(pStorage, Technique.m_EffectName);

			const float BlockPaddingX = 16.0f;
			const float BlockPaddingY = 14.0f;
			float BlockInnerHeight = HeaderLineSize + MarginSmall;
			if(TechniqueUi.m_Expanded)
			{
				BlockInnerHeight += LineSize + MarginSmall;
				if(pUniforms == nullptr || pUniforms->empty())
					BlockInnerHeight += LineSize + MarginSmall;
				else
				{
					for(const SBestClientReShadeUniformMeta &UniformMeta : *pUniforms)
						BlockInnerHeight += GetUniformRowCount(UniformMeta) * (LineSize + MarginSmall);
				}
			}

			CUIRect BlockRect;
			Content.HSplitTop(BlockInnerHeight + BlockPaddingY * 2.0f, &BlockRect, &Content);
			Content.HSplitTop(MarginSmall, nullptr, &Content);
			if(!s_AddedScrollRegion.AddRect(BlockRect))
				continue;

			BlockRect.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f), IGraphics::CORNER_ALL, CardRadius);

			CUIRect TechniqueContent = BlockRect;
			TechniqueContent.VMargin(BlockPaddingX, &TechniqueContent);
			TechniqueContent.HMargin(BlockPaddingY, &TechniqueContent);

			CUIRect TechniqueHeader;
			TechniqueContent.HSplitTop(HeaderLineSize, &TechniqueHeader, &TechniqueContent);
			TechniqueContent.HSplitTop(MarginSmall, nullptr, &TechniqueContent);
			if(s_AddedScrollRegion.AddRect(TechniqueHeader))
			{
				CUIRect ToggleRect = TechniqueHeader, ResetRect, RemoveRect, ExpandRect;
				ToggleRect.VSplitRight(IconButtonSize, &ToggleRect, &ExpandRect);
				ToggleRect.VSplitRight(MarginSmall, &ToggleRect, nullptr);
				ToggleRect.VSplitRight(IconButtonSize, &ToggleRect, &RemoveRect);
				ToggleRect.VSplitRight(MarginSmall, &ToggleRect, nullptr);
				ToggleRect.VSplitRight(IconButtonSize, &ToggleRect, &ResetRect);

				int EnabledValue = Enabled ? 1 : 0;
				if(pMenus->DoButton_CheckBox(&TechniqueUi.m_aIds[0], Technique.m_TechniqueName.c_str(), EnabledValue, &ToggleRect))
				{
					const bool NewEnabled = !Enabled;
					BestClientSetReShadeTechniqueEnabled(EnsureEditedPreset(), Technique.m_Token, NewEnabled);
					if(!NewEnabled && s_PendingAcceptToken == Technique.m_Token)
					{
						ClearPendingAccept();
					}
					else if(NewEnabled && g_Config.m_BcReshadeAutoAccept == 0)
					{
						if(!s_PendingAcceptToken.empty() && s_PendingAcceptToken != Technique.m_Token)
							RejectPendingAccept(false, false);
						s_PendingAcceptToken = Technique.m_Token;
						s_PendingAcceptTechniqueName = Technique.m_TechniqueName;
						s_PendingAcceptStartTick = NowTick;
						s_PendingAcceptRemovesTechnique = false;
						ShowAcceptPopup(Technique.m_TechniqueName, 5);
						SetStatus(BCLocalize("The effect is live. Confirm it within 5 seconds to keep it enabled."), false);
					}
				}

				const std::vector<SBestClientReShadeUniformMeta> *pResetUniforms = pUniforms;
				if(pResetUniforms == nullptr)
					pResetUniforms = &BestClientGetReShadeUniformMetadata(pStorage, Technique.m_EffectName);
				const bool CanReset = !pResetUniforms->empty();
				if(pUi->DoButton_FontIcon(&TechniqueUi.m_ResetButton, FontIcon::ARROW_ROTATE_LEFT, CanReset ? 0 : -1, &ResetRect, BUTTONFLAG_LEFT) && CanReset)
				{
					BestClientResetReShadeTechniqueToDefaults(EnsureEditedPreset(), Technique.m_EffectName, *pResetUniforms);
					SetStatus(BCLocalize("The effect settings were reset to their defaults."), false);
				}

				if(pUi->DoButton_FontIcon(&TechniqueUi.m_RemoveButton, FontIcon::TRASH, 0, &RemoveRect, BUTTONFLAG_LEFT))
				{
					BestClientRemoveReShadeTechniqueFromPreset(EnsureEditedPreset(), Technique.m_Token);
					if(s_PendingAcceptToken == Technique.m_Token)
						ClearPendingAccept();
					SetStatus(BCLocalize("The effect was removed from the configured list."), false);
				}

				if(pUi->DoButton_FontIcon(&TechniqueUi.m_ExpandButton, TechniqueUi.m_Expanded ? FontIcon::CHEVRON_UP : FontIcon::CHEVRON_DOWN, 0, &ExpandRect, BUTTONFLAG_LEFT))
					TechniqueUi.m_Expanded = !TechniqueUi.m_Expanded;
			}

			if(!TechniqueUi.m_Expanded)
				continue;

			if(pUniforms == nullptr)
				pUniforms = &BestClientGetReShadeUniformMetadata(pStorage, Technique.m_EffectName);

			CUIRect EffectLabelRect;
			TechniqueContent.HSplitTop(LineSize, &EffectLabelRect, &TechniqueContent);
			TechniqueContent.HSplitTop(MarginSmall, nullptr, &TechniqueContent);
			if(s_AddedScrollRegion.AddRect(EffectLabelRect))
			{
				CUIRect IndentedRect;
				EffectLabelRect.VSplitLeft(LineSize, nullptr, &IndentedRect);
				char aEffectLabel[256];
				str_format(aEffectLabel, sizeof(aEffectLabel), "Effect file: %s", Technique.m_EffectName.c_str());
				pUi->DoLabel(&IndentedRect, aEffectLabel, 12.0f, TEXTALIGN_ML);
			}

			if(pUniforms->empty())
			{
				CUIRect EmptyRect;
				TechniqueContent.HSplitTop(LineSize, &EmptyRect, &TechniqueContent);
				TechniqueContent.HSplitTop(MarginSmall, nullptr, &TechniqueContent);
				if(s_AddedScrollRegion.AddRect(EmptyRect))
				{
					CUIRect IndentedRect;
					EmptyRect.VSplitLeft(LineSize, nullptr, &IndentedRect);
					pUi->DoLabel(&IndentedRect, BCLocalize("No supported scalar settings were found for this effect."), 12.0f, TEXTALIGN_ML);
				}
				continue;
			}

			for(const SBestClientReShadeUniformMeta &UniformMeta : *pUniforms)
			{
				SUniformUiState &UniformUi = TechniqueUi.m_UniformStates[UniformMeta.m_Name];
				const std::string UniformValueText = BestClientGetReShadeUniformValue(CurrentPresetState(), Technique.m_EffectName, UniformMeta);

				if(UniformMeta.m_Type == EBestClientReShadeUniformType::BOOL)
				{
					CUIRect UniformRect;
					TechniqueContent.HSplitTop(LineSize, &UniformRect, &TechniqueContent);
					TechniqueContent.HSplitTop(MarginSmall, nullptr, &TechniqueContent);
					if(!s_AddedScrollRegion.AddRect(UniformRect))
						continue;

					CUIRect IndentedRect;
					UniformRect.VSplitLeft(LineSize, nullptr, &IndentedRect);
					bool BoolValue = false;
					if(!BestClientTryParseBoolText(UniformValueText, BoolValue))
						BestClientTryParseBoolText(UniformMeta.m_DefaultValue, BoolValue);

					int CheckboxValue = BoolValue ? 1 : 0;
					if(pMenus->DoButton_CheckBox(&UniformUi.m_aIds[0], UniformMeta.m_Label.c_str(), CheckboxValue, &IndentedRect))
						EnsureEditedPreset().m_SectionValues[Technique.m_EffectName][UniformMeta.m_Name] = BoolValue ? "0" : "1";
				}
				else if(UniformMeta.m_Type == EBestClientReShadeUniformType::INT)
				{
					if(!UniformMeta.m_vComboItems.empty())
					{
						CUIRect UniformRect;
						TechniqueContent.HSplitTop(LineSize * 2.0f + MarginSmall, &UniformRect, &TechniqueContent);
						TechniqueContent.HSplitTop(MarginSmall, nullptr, &TechniqueContent);
						if(!s_AddedScrollRegion.AddRect(UniformRect))
							continue;

						int IntValue = 0;
						BestClientTryParseIntText(UniformValueText, IntValue);
						IntValue = std::clamp(IntValue, 0, (int)UniformMeta.m_vComboItems.size() - 1);

						CUIRect IndentedRect;
						UniformRect.VSplitLeft(LineSize, nullptr, &IndentedRect);
						CUIRect LabelRect, ControlRect;
						IndentedRect.HSplitTop(LineSize, &LabelRect, &IndentedRect);
						IndentedRect.HSplitTop(MarginSmall, nullptr, &IndentedRect);
						ControlRect = IndentedRect;
						pUi->DoLabel(&LabelRect, UniformMeta.m_Label.c_str(), 14.0f, TEXTALIGN_ML);

						std::vector<const char *> vItemPointers;
						vItemPointers.reserve(UniformMeta.m_vComboItems.size());
						for(const std::string &Item : UniformMeta.m_vComboItems)
							vItemPointers.push_back(Item.c_str());

						UniformUi.m_DropDownState.m_SelectionPopupContext.m_pScrollRegion = &UniformUi.m_DropDownScrollRegion;
						const int NewValue = pUi->DoDropDown(&ControlRect, IntValue, vItemPointers.data(), (int)vItemPointers.size(), UniformUi.m_DropDownState);
						if(NewValue != IntValue)
							EnsureEditedPreset().m_SectionValues[Technique.m_EffectName][UniformMeta.m_Name] = std::to_string(NewValue);
						continue;
					}

					CUIRect UniformRect;
					TechniqueContent.HSplitTop(LineSize, &UniformRect, &TechniqueContent);
					TechniqueContent.HSplitTop(MarginSmall, nullptr, &TechniqueContent);
					if(!s_AddedScrollRegion.AddRect(UniformRect))
						continue;

					CUIRect IndentedRect;
					UniformRect.VSplitLeft(LineSize, nullptr, &IndentedRect);
					int IntValue = (int)std::round(UniformMeta.m_Min);
					if(!BestClientTryParseIntText(UniformValueText, IntValue))
					{
						float DefaultFloat = UniformMeta.m_Min;
						if(BestClientTryParseFloatText(UniformMeta.m_DefaultValue, DefaultFloat))
							IntValue = (int)std::round(DefaultFloat);
					}
					IntValue = std::clamp(IntValue, (int)std::round(UniformMeta.m_Min), (int)std::round(UniformMeta.m_Max));
					if(pUi->DoScrollbarOption(&UniformUi.m_aIds[0], &IntValue, &IndentedRect, UniformMeta.m_Label.c_str(), (int)std::round(UniformMeta.m_Min), (int)std::round(UniformMeta.m_Max)))
						EnsureEditedPreset().m_SectionValues[Technique.m_EffectName][UniformMeta.m_Name] = std::to_string(IntValue);
				}
				else if(UniformMeta.m_Type == EBestClientReShadeUniformType::UINT)
				{
					if(!UniformMeta.m_vComboItems.empty())
					{
						CUIRect UniformRect;
						TechniqueContent.HSplitTop(LineSize * 2.0f + MarginSmall, &UniformRect, &TechniqueContent);
						TechniqueContent.HSplitTop(MarginSmall, nullptr, &TechniqueContent);
						if(!s_AddedScrollRegion.AddRect(UniformRect))
							continue;

						unsigned int UintValue = 0;
						BestClientTryParseUintText(UniformValueText, UintValue);
						const int CurrentValue = std::clamp((int)UintValue, 0, (int)UniformMeta.m_vComboItems.size() - 1);

						CUIRect IndentedRect;
						UniformRect.VSplitLeft(LineSize, nullptr, &IndentedRect);
						CUIRect LabelRect, ControlRect;
						IndentedRect.HSplitTop(LineSize, &LabelRect, &IndentedRect);
						IndentedRect.HSplitTop(MarginSmall, nullptr, &IndentedRect);
						ControlRect = IndentedRect;
						pUi->DoLabel(&LabelRect, UniformMeta.m_Label.c_str(), 14.0f, TEXTALIGN_ML);

						std::vector<const char *> vItemPointers;
						vItemPointers.reserve(UniformMeta.m_vComboItems.size());
						for(const std::string &Item : UniformMeta.m_vComboItems)
							vItemPointers.push_back(Item.c_str());

						UniformUi.m_DropDownState.m_SelectionPopupContext.m_pScrollRegion = &UniformUi.m_DropDownScrollRegion;
						const int NewValue = pUi->DoDropDown(&ControlRect, CurrentValue, vItemPointers.data(), (int)vItemPointers.size(), UniformUi.m_DropDownState);
						if(NewValue != CurrentValue)
							EnsureEditedPreset().m_SectionValues[Technique.m_EffectName][UniformMeta.m_Name] = std::to_string(NewValue);
						continue;
					}

					CUIRect UniformRect;
					TechniqueContent.HSplitTop(LineSize, &UniformRect, &TechniqueContent);
					TechniqueContent.HSplitTop(MarginSmall, nullptr, &TechniqueContent);
					if(!s_AddedScrollRegion.AddRect(UniformRect))
						continue;

					CUIRect IndentedRect;
					UniformRect.VSplitLeft(LineSize, nullptr, &IndentedRect);
					unsigned int UintValue = (unsigned int)std::round(maximum(UniformMeta.m_Min, 0.0f));
					if(!BestClientTryParseUintText(UniformValueText, UintValue))
					{
						float DefaultFloat = maximum(UniformMeta.m_Min, 0.0f);
						if(BestClientTryParseFloatText(UniformMeta.m_DefaultValue, DefaultFloat))
							UintValue = (unsigned int)std::round(maximum(DefaultFloat, 0.0f));
					}

					const int MinValue = (int)std::round(maximum(UniformMeta.m_Min, 0.0f));
					const int MaxValue = (int)std::round(maximum(UniformMeta.m_Max, UniformMeta.m_Min));
					int SliderValue = std::clamp((int)UintValue, MinValue, MaxValue);
					if(pUi->DoScrollbarOption(&UniformUi.m_aIds[0], &SliderValue, &IndentedRect, UniformMeta.m_Label.c_str(), MinValue, MaxValue))
						EnsureEditedPreset().m_SectionValues[Technique.m_EffectName][UniformMeta.m_Name] = std::to_string((unsigned int)SliderValue);
				}
				else if(UniformMeta.m_IsColor && BestClientIsReShadeUniformFloatVectorType(UniformMeta.m_Type))
				{
					CUIRect UniformRect;
					TechniqueContent.HSplitTop(LineSize, &UniformRect, &TechniqueContent);
					TechniqueContent.HSplitTop(MarginSmall, nullptr, &TechniqueContent);
					if(!s_AddedScrollRegion.AddRect(UniformRect))
						continue;

					CUIRect IndentedRect;
					UniformRect.VSplitLeft(LineSize, nullptr, &IndentedRect);

					std::array<float, 4> aDefaultValues = {0.0f, 0.0f, 0.0f, 1.0f};
					BestClientTryParseFloatVectorText(UniformMeta.m_DefaultValue, aDefaultValues, UniformMeta.m_NumComponents);

					std::array<float, 4> aCurrentValues = aDefaultValues;
					BestClientTryParseFloatVectorText(UniformValueText, aCurrentValues, UniformMeta.m_NumComponents);
					if(!UniformMeta.m_HasAlpha)
						aCurrentValues[3] = 1.0f;

					const ColorRGBA DefaultColor(
						std::clamp(aDefaultValues[0], 0.0f, 1.0f),
						std::clamp(aDefaultValues[1], 0.0f, 1.0f),
						std::clamp(aDefaultValues[2], 0.0f, 1.0f),
						UniformMeta.m_HasAlpha ? std::clamp(aDefaultValues[3], 0.0f, 1.0f) : 1.0f);
					const ColorRGBA CurrentColor(
						std::clamp(aCurrentValues[0], 0.0f, 1.0f),
						std::clamp(aCurrentValues[1], 0.0f, 1.0f),
						std::clamp(aCurrentValues[2], 0.0f, 1.0f),
						UniformMeta.m_HasAlpha ? std::clamp(aCurrentValues[3], 0.0f, 1.0f) : 1.0f);

					unsigned int PackedColor = color_cast<ColorHSLA>(CurrentColor).Pack(UniformMeta.m_HasAlpha);
					const unsigned int PackedColorBefore = PackedColor;
					CUIRect ColorRect = IndentedRect;
					BestClientDoColorLine(pUi, pMenus, UniformUi.m_ColorPickerPopupContext, UniformUi.m_ColorResetButton, &ColorRect, UniformMeta.m_Label.c_str(), &PackedColor, DefaultColor, UniformMeta.m_HasAlpha);
					if(PackedColor != PackedColorBefore)
					{
						const ColorRGBA UpdatedColor = color_cast<ColorRGBA>(ColorHSLA(PackedColor, UniformMeta.m_HasAlpha));
						aCurrentValues[0] = UpdatedColor.r;
						aCurrentValues[1] = UpdatedColor.g;
						aCurrentValues[2] = UpdatedColor.b;
						if(UniformMeta.m_HasAlpha)
							aCurrentValues[3] = UpdatedColor.a;
						EnsureEditedPreset().m_SectionValues[Technique.m_EffectName][UniformMeta.m_Name] = BestClientFormatReShadeFloatVector(aCurrentValues, UniformMeta.m_NumComponents);
					}
				}
				else if(BestClientIsReShadeUniformFloatVectorType(UniformMeta.m_Type))
				{
					std::array<float, 4> aDefaultValues = {0.0f, 0.0f, 0.0f, 0.0f};
					BestClientTryParseFloatVectorText(UniformMeta.m_DefaultValue, aDefaultValues, UniformMeta.m_NumComponents);

					std::array<float, 4> aCurrentValues = aDefaultValues;
					BestClientTryParseFloatVectorText(UniformValueText, aCurrentValues, UniformMeta.m_NumComponents);

					for(int Component = 0; Component < UniformMeta.m_NumComponents; ++Component)
					{
						CUIRect UniformRect;
						TechniqueContent.HSplitTop(LineSize, &UniformRect, &TechniqueContent);
						TechniqueContent.HSplitTop(MarginSmall, nullptr, &TechniqueContent);
						if(!s_AddedScrollRegion.AddRect(UniformRect))
							continue;

						CUIRect IndentedRect;
						UniformRect.VSplitLeft(LineSize, nullptr, &IndentedRect);
						const float FloatValue = std::clamp(aCurrentValues[Component], UniformMeta.m_Min, UniformMeta.m_Max);

						int SliderValue = 0;
						if(UniformMeta.m_Max > UniformMeta.m_Min)
							SliderValue = (int)std::round((FloatValue - UniformMeta.m_Min) / (UniformMeta.m_Max - UniformMeta.m_Min) * 1000.0f);
						SliderValue = std::clamp(SliderValue, 0, 1000);

						char aComponentLabel[128];
						str_format(aComponentLabel, sizeof(aComponentLabel), "%s %s", UniformMeta.m_Label.c_str(), BestClientReShadeUniformComponentSuffix(Component));
						if(pMenus->DoSliderWithScaledValue(&UniformUi.m_aIds[Component], &SliderValue, &IndentedRect, aComponentLabel, 0, 1000, 1, &CUi::ms_LinearScrollbarScale))
						{
							const float NormalizedValue = SliderValue / 1000.0f;
							aCurrentValues[Component] = mix(UniformMeta.m_Min, UniformMeta.m_Max, NormalizedValue);
							EnsureEditedPreset().m_SectionValues[Technique.m_EffectName][UniformMeta.m_Name] = BestClientFormatReShadeFloatVector(aCurrentValues, UniformMeta.m_NumComponents);
						}
					}
				}
				else
				{
					CUIRect UniformRect;
					TechniqueContent.HSplitTop(LineSize, &UniformRect, &TechniqueContent);
					TechniqueContent.HSplitTop(MarginSmall, nullptr, &TechniqueContent);
					if(!s_AddedScrollRegion.AddRect(UniformRect))
						continue;

					CUIRect IndentedRect;
					UniformRect.VSplitLeft(LineSize, nullptr, &IndentedRect);
					float FloatValue = UniformMeta.m_Min;
					if(!BestClientTryParseFloatText(UniformValueText, FloatValue))
						BestClientTryParseFloatText(UniformMeta.m_DefaultValue, FloatValue);
					FloatValue = std::clamp(FloatValue, UniformMeta.m_Min, UniformMeta.m_Max);

					int SliderValue = 0;
					if(UniformMeta.m_Max > UniformMeta.m_Min)
						SliderValue = (int)std::round((FloatValue - UniformMeta.m_Min) / (UniformMeta.m_Max - UniformMeta.m_Min) * 1000.0f);
					SliderValue = std::clamp(SliderValue, 0, 1000);

					if(pMenus->DoSliderWithScaledValue(&UniformUi.m_aIds[0], &SliderValue, &IndentedRect, UniformMeta.m_Label.c_str(), 0, 1000, 1, &CUi::ms_LinearScrollbarScale))
					{
						const float NormalizedValue = SliderValue / 1000.0f;
						const float UpdatedValue = mix(UniformMeta.m_Min, UniformMeta.m_Max, NormalizedValue);
						EnsureEditedPreset().m_SectionValues[Technique.m_EffectName][UniformMeta.m_Name] = BestClientFormatReShadeFloat(UpdatedValue);
					}
				}
			}
		}

		if(NumVisibleAdded == 0)
		{
			CUIRect EmptyRect;
			Content.HSplitTop(40.0f, &EmptyRect, &Content);
			if(s_AddedScrollRegion.AddRect(EmptyRect))
				pUi->DoLabel(&EmptyRect, BCLocalize("No shaders are configured on the right yet."), 13.0f, TEXTALIGN_ML);
		}

		s_AddedScrollRegion.End();
	}

	if(HasPresetChanges)
	{
		s_PendingSavePresetState = EditedPresetState;
		s_HasPendingSavePreset = true;
		s_PendingSavePresetTick = NowTick;
		s_PendingLivePresetState = EditedPresetState;
		s_HasPendingLivePreset = true;
		s_PendingLivePresetTick = NowTick;
		s_PendingLiveRevision = ++s_LiveRevision;
	}

	const int64_t LiveDelay = maximum<int64_t>(1, time_freq() / 25);
	if(s_HasPendingLivePreset && NowTick - s_PendingLivePresetTick >= LiveDelay)
	{
		char aLiveError[192];
		if(BestClientSaveReShadeBridgeState(pStorage, s_PendingLivePresetState, s_PendingLiveRevision, aLiveError, sizeof(aLiveError)))
		{
			s_HasPendingLivePreset = false;
		}
		else
		{
			s_PendingLivePresetTick = NowTick;
			SetStatus(aLiveError, true);
		}
	}

	const int64_t SaveDelay = maximum<int64_t>(1, time_freq() / 3);
	if(s_HasPendingSavePreset && NowTick - s_PendingSavePresetTick >= SaveDelay)
	{
		char aSettingsError[192];
		char aSaveError[192];
		if(BestClientSaveReShadePreset(pStorage, s_PendingSavePresetState, aSaveError, sizeof(aSaveError)) &&
			BestClientSaveReShadeSettings(pStorage, s_PendingSavePresetState, aSettingsError, sizeof(aSettingsError)))
		{
			s_HasPendingSavePreset = false;
			char aRuntimeError[192];
			if(BestClientQueryReShadeLiveAvailability(pStorage, pGraphics, aRuntimeError, sizeof(aRuntimeError)))
			{
				SetStatus(BCLocalize("Saved to ReShadePreset.ini and settings_reshade.cfg, then applied live."), false);
			}
			else
			{
				char aStatus[256];
				str_format(aStatus, sizeof(aStatus), "Saved to ReShadePreset.ini and settings_reshade.cfg, but live apply is unavailable: %s", aRuntimeError);
				SetStatus(aStatus, true);
			}
		}
		else
		{
			s_HasPendingSavePreset = false;
			SetStatus(aSaveError[0] != '\0' ? aSaveError : aSettingsError, true);
		}
	}

	if(NeedReShadeRestart)
		RenderRestartWarning(RestartBar);
}

#endif

enum
{
	COMPONENTS_GROUP_VISUALS = 0,
	COMPONENTS_GROUP_GAMEPLAY,
	COMPONENTS_GROUP_OTHERS,
	COMPONENTS_GROUP_TCLIENT,
	NUM_COMPONENTS_GROUPS,
};

struct SBestClientComponentEntry
{
	CBestClient::EBestClientComponent m_Component;
	const char *m_pName;
	int m_Group;
};

static const SBestClientComponentEntry gs_aBestClientComponentEntries[] = {
	{CBestClient::COMPONENT_VISUALS_JELLY_TEE, "Jelly Tee", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_3D_PARTICLES, "3D Particles", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_AFTERIMAGE, "Afterimage", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_GRAFFITI, "Graffiti", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_MUSIC_PLAYER, "Music Player", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_KEYSTROKES, "Keystrokes", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_MEDIA_BACKGROUND, "Media Background", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_ANIMATIONS, "Animations", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_ASPECT_RATIO, "Aspect Ratio", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_EYE_COMFORT, "Eye Comfort", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_MOTION_BLUR, "Motion Blur", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_VISUALS_FLYING_NAMEPLATES, "Flying Nameplates", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_GAMEPLAY_HOOK_COMBO, "Hook Combo", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_GAMEPLAY_INPUT, "Input", COMPONENTS_GROUP_GAMEPLAY},
	{CBestClient::COMPONENT_GAMEPLAY_FAST_ACTIONS, "Fast Actions", COMPONENTS_GROUP_GAMEPLAY},
	{CBestClient::COMPONENT_GAMEPLAY_SPEEDRUN_TIMER, "Speedrun Timer", COMPONENTS_GROUP_GAMEPLAY},
	{CBestClient::COMPONENT_GAMEPLAY_FINISH_PREDICTION, "Finish Prediction", COMPONENTS_GROUP_GAMEPLAY},
	{CBestClient::COMPONENT_GAMEPLAY_AUTO_TEAM_LOCK, "Auto Team Lock", COMPONENTS_GROUP_GAMEPLAY},
	{CBestClient::COMPONENT_GAMEPLAY_GORES_MODE, "Gores Mode", COMPONENTS_GROUP_GAMEPLAY},
	{CBestClient::COMPONENT_VISUALS_OPTIMIZER, "Optimizer", COMPONENTS_GROUP_GAMEPLAY},
	{CBestClient::COMPONENT_VISUALS_FOCUS_MODE, "Focus Mode", COMPONENTS_GROUP_GAMEPLAY},
	{CBestClient::COMPONENT_OTHERS_MISC, "Misc", COMPONENTS_GROUP_OTHERS},
	{CBestClient::COMPONENT_OTHERS_CHAT_MEDIA, "Chat Media", COMPONENTS_GROUP_OTHERS},
	{CBestClient::COMPONENT_VISUALS_CHAT_BUBBLES, "Chat Bubbles", COMPONENTS_GROUP_VISUALS},
	{CBestClient::COMPONENT_OTHERS_VOICE_SETTINGS, "Voice Chat", COMPONENTS_GROUP_OTHERS},
	{CBestClient::COMPONENT_OTHERS_VOICE_BINDS, "Voice Binds", COMPONENTS_GROUP_OTHERS},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_TAB, "Settings tab", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_BIND_WHEEL_TAB, "Bind wheel tab", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_WAR_LIST_TAB, "War list tab", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_CHAT_BINDS_TAB, "Chat binds tab", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_STATUS_BAR_TAB, "Status bar tab", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_INFO_TAB, "Info tab", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_PROFILES_PAGE, "Profiles page", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_CONFIGS_PAGE, "Configs page", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_VISUAL, "Settings: Visual", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_ANTI_LATENCY, "Settings: Anti Latency Tools", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_ANTI_PING_SMOOTHING, "Settings: Anti Ping Smoothing", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_AUTO_EXECUTE, "Settings: Auto execute", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_VOTING, "Settings: Voting", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_AUTO_REPLY, "Settings: Auto Reply", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_PLAYER_INDICATOR, "Settings: Player Indicator", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_PET, "Settings: Pet", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_HUD, "Settings: HUD", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_FROZEN_TEE_DISPLAY, "Settings: Frozen Tee Display", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_TILE_OUTLINES, "Settings: Tile Outlines", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_GHOST_TOOLS, "Settings: Ghost Tools", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_RAINBOW, "Settings: Rainbow", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_TEE_TRAILS, "Settings: Tee Trails", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_BACKGROUND_DRAW, "Settings: Background Draw", COMPONENTS_GROUP_TCLIENT},
	{CBestClient::COMPONENT_TCLIENT_SETTINGS_FINISH_NAME, "Settings: Finish Name", COMPONENTS_GROUP_TCLIENT},
};

static bool ComponentsEditorIsDisabled(int Component, int MaskLo, int MaskHi)
{
	return CBestClient::IsComponentDisabledByMask(Component, MaskLo, MaskHi);
}

static void ComponentsEditorSetDisabled(int Component, int &MaskLo, int &MaskHi, bool Disabled)
{
	if(Component < 0 || Component >= CBestClient::NUM_COMPONENTS_EDITOR_COMPONENTS)
		return;

	int *pMask = &MaskLo;
	int Bit = Component;
	if(Component >= 31)
	{
		pMask = &MaskHi;
		Bit = Component - 31;
	}

	if(Disabled)
		*pMask |= (1 << Bit);
	else
		*pMask &= ~(1 << Bit);
}

void CMenus::RenderSettingsBestClient(CUIRect MainView)
{
	MainView.y -= 20.0f;
	MainView.h += 20.0f;

	enum
	{
		BESTCLIENT_TAB_VISUALS = 0,
		BESTCLIENT_TAB_GAMEPLAY,
		BESTCLIENT_TAB_OTHERS,
		BESTCLIENT_TAB_RESHADE,
		BESTCLIENT_TAB_INFO,
		NUM_BESTCLIENT_TABS,
	};

	static int s_CurTab = BESTCLIENT_TAB_VISUALS;
	static CButtonContainer s_aPageTabs[NUM_BESTCLIENT_TABS] = {};

	if(m_AssetsEditorState.m_VisualsEditorOpen && m_AssetsEditorState.m_FullscreenOpen)
	{
		SetBestClientShopVisible(false);
		RenderAssetsEditorScreen(*Ui()->Screen());
		return;
	}
	if(m_ComponentsEditorState.m_Open && m_ComponentsEditorState.m_FullscreenOpen)
	{
		SetBestClientShopVisible(false);
		RenderComponentsEditorScreen(*Ui()->Screen());
		return;
	}

	{
		CUIRect HintBar, Badge;
		MainView.HSplitTop(18.0f, &HintBar, &MainView);
		HintBar.VSplitLeft(310.0f, &Badge, nullptr);
		Badge.HMargin(1.5f, &Badge);
		Ui()->DoLabel(&Badge, BCLocalize("assets & components editors/fun/shop \xe2\x86\x92 Info"), 14.0f, TEXTALIGN_ML);
	}
	MainView.HSplitTop(4.0f, nullptr, &MainView);

	CUIRect TabBar, TabButton;
	MainView.HSplitTop(24.0f, &TabBar, &MainView);
	const char *apTabNames[NUM_BESTCLIENT_TABS] = {
		BCLocalize("Visuals"),
		BCLocalize("Gameplay"),
		BCLocalize("Others"),
		BCLocalize("Live-Shaders"),
		BCLocalize("Info"),
	};
	const int aTabOrder[NUM_BESTCLIENT_TABS] = {
		BESTCLIENT_TAB_VISUALS,
		BESTCLIENT_TAB_GAMEPLAY,
		BESTCLIENT_TAB_OTHERS,
		BESTCLIENT_TAB_RESHADE,
		BESTCLIENT_TAB_INFO,
	};

	auto IsTabHidden = [&](int Tab) {
		// Keep Info always visible.
		return Tab != BESTCLIENT_TAB_INFO && IsBestClientTabFlagSet(g_Config.m_BcBestClientSettingsTabs, Tab);
	};

	int TabCount = 0;
	int FirstVisibleTab = -1;
	for(const int Tab : aTabOrder)
	{
		if(IsTabHidden(Tab))
			continue;
		if(FirstVisibleTab == -1)
			FirstVisibleTab = Tab;
		++TabCount;
	}

	if(FirstVisibleTab == -1)
	{
		s_CurTab = BESTCLIENT_TAB_INFO;
		FirstVisibleTab = BESTCLIENT_TAB_INFO;
		TabCount = 1;
	}

	if(s_CurTab < BESTCLIENT_TAB_VISUALS || s_CurTab >= NUM_BESTCLIENT_TABS || IsTabHidden(s_CurTab))
		s_CurTab = FirstVisibleTab;

	const float TabWidth = TabBar.w / (float)TabCount;
	int VisibleIndex = 0;
	for(const int Tab : aTabOrder)
	{
		if(IsTabHidden(Tab))
			continue;

		TabBar.VSplitLeft(TabWidth, &TabButton, &TabBar);
		const int Corners = VisibleIndex == 0 ? IGraphics::CORNER_L : (VisibleIndex == TabCount - 1 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
		if(DoButton_MenuTab(&s_aPageTabs[Tab], apTabNames[Tab], s_CurTab == Tab, &TabButton, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
		{
			s_CurTab = Tab;
		}
		if(Tab == BESTCLIENT_TAB_INFO)
			GameClient()->m_Tooltips.DoToolTip(&s_aPageTabs[Tab], &TabButton, BCLocalize("Fun and Shop moved here"));
		VisibleIndex++;
	}

	MainView.HSplitTop(10.0f, nullptr, &MainView);
	SetBestClientShopVisible(false);

	if(s_CurTab == BESTCLIENT_TAB_VISUALS)
	{
		const float LineSize = 20.0f;
		const float HeadlineFontSize = 20.0f;
		const float MarginSmall = 5.0f;
		const float MarginBetweenSections = 30.0f;
		const float MarginBetweenViews = 30.0f;
		const ColorRGBA BlockColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f);
		const auto ModuleUiRevealAnimationsEnabled = [&]() {
			return BCUiAnimations::Enabled() && g_Config.m_BcModuleUiRevealAnimation != 0;
		};
		const auto ModuleUiRevealAnimationDuration = [&]() {
			return BCUiAnimations::MsToSeconds(g_Config.m_BcModuleUiRevealAnimationMs);
		};
		const auto UpdateRevealPhase = [&](float &Phase, bool Expanded) {
			if(ModuleUiRevealAnimationsEnabled())
				BCUiAnimations::UpdatePhase(Phase, Expanded ? 1.0f : 0.0f, Client()->RenderFrameTime(), ModuleUiRevealAnimationDuration());
			else
				Phase = Expanded ? 1.0f : 0.0f;
		};
		const auto DoOpenHudEditorButton = [&](CButtonContainer *pButtonContainer, CUIRect *pButtonRect) {
			const bool CanOpenHudEditor = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
			const bool Clicked = Ui()->DoButton_FontIcon(pButtonContainer, FontIcon::UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, CanOpenHudEditor ? 0 : -1, pButtonRect, BUTTONFLAG_LEFT);
			GameClient()->m_Tooltips.DoToolTip(pButtonContainer, pButtonRect, CanOpenHudEditor ? BCLocalize("Open in HUD editor") : BCLocalize("Join a game first"));
			GameClient()->m_Tooltips.SetFadeTime(pButtonContainer, 0.0f);
			if(Clicked && CanOpenHudEditor)
			{
				SetActive(false);
				GameClient()->m_HudEditor.Activate();
			}
			return Clicked && CanOpenHudEditor;
		};

		{
			CUIRect HudButtonRow;
			MainView.HSplitTop(24.0f, &HudButtonRow, &MainView);
			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			static CButtonContainer s_HudEditorButton;
			const bool CanOpen = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
			if(DoButton_MenuTab(&s_HudEditorButton, BCLocalize("HUD editor"), 0, &HudButtonRow, IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 4.0f) && CanOpen)
			{
				SetActive(false);
				GameClient()->m_HudEditor.Activate();
			}
			GameClient()->m_Tooltips.DoToolTip(&s_HudEditorButton, &HudButtonRow, CanOpen ? BCLocalize("Open in HUD editor") : BCLocalize("Join a game first"));
			GameClient()->m_Tooltips.SetFadeTime(&s_HudEditorButton, 0.0f);
		}

		static CScrollRegion s_BestClientVisualsScrollRegion;
		vec2 VisualsScrollOffset(0.0f, 0.0f);
		CScrollRegionParams VisualsScrollParams;
		VisualsScrollParams.m_ScrollUnit = 60.0f;
		VisualsScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
		VisualsScrollParams.m_ScrollbarMargin = 5.0f;
		s_BestClientVisualsScrollRegion.Begin(&MainView, &VisualsScrollOffset, &VisualsScrollParams);

		MainView.y += VisualsScrollOffset.y;
		MainView.VSplitRight(5.0f, &MainView, nullptr);
		MainView.VSplitLeft(5.0f, nullptr, &MainView);

		CUIRect LeftView, RightView;
		MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
		LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
		RightView.VSplitRight(MarginSmall, &RightView, nullptr);

		static std::vector<CUIRect> s_SectionBoxes;
		static vec2 s_PrevScrollOffset(0.0f, 0.0f);
		for(CUIRect &Section : s_SectionBoxes)
		{
			float Padding = MarginBetweenViews * 0.6666f;
			Section.w += Padding;
			Section.h += Padding;
			Section.x -= Padding * 0.5f;
			Section.y -= Padding * 0.5f;
			Section.y -= s_PrevScrollOffset.y - VisualsScrollOffset.y;
			Section.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);
		}
		s_PrevScrollOffset = VisualsScrollOffset;
		s_SectionBoxes.clear();

		auto BeginBlock = [&](CUIRect &ColumnRef, float ContentHeight, CUIRect &Content) {
			CUIRect Block;
			ColumnRef.HSplitTop(ContentHeight, &Block, &ColumnRef);
			s_SectionBoxes.push_back(Block);
			Content = Block;
		};

		CUIRect Column = LeftView;
		Column.HSplitTop(10.0f, nullptr, &Column);

		// Hook combo (left column block)
		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_GAMEPLAY_HOOK_COMBO))
		{
			static float s_HookComboPhase = 0.0f;
			static CButtonContainer s_HookComboResetButton;
			const bool HookComboExpanded = g_Config.m_BcHookCombo != 0;
			UpdateRevealPhase(s_HookComboPhase, HookComboExpanded);
			const float ExpandedTargetHeight = MarginSmall + LineSize * 5.0f;
			const float ExpandedHeight = ExpandedTargetHeight * s_HookComboPhase;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExpandedHeight;
			CUIRect Content, Label, Button, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			CUIRect TitleLabel, ResetButton, ResetHitbox;
			Label.VSplitRight(LineSize + 8.0f, &TitleLabel, &ResetButton);
			ResetHitbox = ResetButton;
			const bool HookComboResetClicked = Ui()->DoButton_FontIcon(&s_HookComboResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &ResetHitbox, BUTTONFLAG_LEFT);
			GameClient()->m_Tooltips.DoToolTip(&s_HookComboResetButton, &ResetHitbox, BCLocalize("Reset to defaults"));
			if(HookComboResetClicked)
			{
				g_Config.m_BcHookComboMode = DefaultConfig::BcHookComboMode;
				g_Config.m_BcHookComboResetTime = DefaultConfig::BcHookComboResetTime;
				g_Config.m_BcHookComboSoundVolume = DefaultConfig::BcHookComboSoundVolume;
				g_Config.m_BcHookComboSize = DefaultConfig::BcHookComboSize;
			}
			Ui()->DoLabel(&TitleLabel, BCLocalize("Hook combo"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcHookCombo, BCLocalize("Hook combo"), &g_Config.m_BcHookCombo, &Content, LineSize);
			if(!HookComboResetClicked && ExpandedHeight > 0.0f)
			{
				Content.HSplitTop(ExpandedHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExpandedTargetHeight};
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);

				CUIRect ModeLabel, ModeRow;
				Expand.HSplitTop(LineSize, &ModeLabel, &Expand);
				Ui()->DoLabel(&ModeLabel, BCLocalize("Mode"), 14.0f, TEXTALIGN_ML);

				Expand.HSplitTop(LineSize, &ModeRow, &Expand);
				CUIRect HookButton, HammerButton, HookHammerButton;
				ModeRow.VSplitLeft(ModeRow.w / 3.0f, &HookButton, &ModeRow);
				ModeRow.VSplitLeft(ModeRow.w / 2.0f, &HammerButton, &HookHammerButton);

				static CButtonContainer s_HookComboModeHook;
				static CButtonContainer s_HookComboModeHammer;
				static CButtonContainer s_HookComboModeHookHammer;
				if(DoButton_Menu(&s_HookComboModeHook, BCLocalize("hook"), g_Config.m_BcHookComboMode == 0, &HookButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
					g_Config.m_BcHookComboMode = 0;
				if(DoButton_Menu(&s_HookComboModeHammer, BCLocalize("hammer"), g_Config.m_BcHookComboMode == 1, &HammerButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
					g_Config.m_BcHookComboMode = 1;
				if(DoButton_Menu(&s_HookComboModeHookHammer, BCLocalize("hook&hammer"), g_Config.m_BcHookComboMode == 2, &HookHammerButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
					g_Config.m_BcHookComboMode = 2;

				Expand.HSplitTop(LineSize, &Button, &Expand);
				DoSliderWithScaledValue(&g_Config.m_BcHookComboResetTime, &g_Config.m_BcHookComboResetTime, &Button, BCLocalize("Max time between hooks"), 100, 5000, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");

				Expand.HSplitTop(LineSize, &Button, &Expand);
				DoSliderWithScaledValue(&g_Config.m_BcHookComboSoundVolume, &g_Config.m_BcHookComboSoundVolume, &Button, BCLocalize("Hook combo sound volume"), 0, 100, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "%");

				Expand.HSplitTop(LineSize, &Button, &Expand);
				DoSliderWithScaledValue(&g_Config.m_BcHookComboSize, &g_Config.m_BcHookComboSize, &Button, BCLocalize("Hook combo size"), 50, 200, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "%");
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		// Jelly tee (left column block)
		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_JELLY_TEE))
		{
			static float s_JellyTeePhase = 0.0f;
			static CButtonContainer s_JellyTeeResetButton;
			const bool JellyTeeEnabled = g_Config.m_BcJellyTee != 0;
			UpdateRevealPhase(s_JellyTeePhase, JellyTeeEnabled);
			const float ExtraTargetHeight = 3.0f * LineSize;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExtraTargetHeight * s_JellyTeePhase;
			CUIRect Content, Label, Row, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			CUIRect TitleLabel, ResetButton, ResetHitbox;
			Label.VSplitRight(LineSize + 8.0f, &TitleLabel, &ResetButton);
			ResetHitbox = ResetButton;
			const bool JellyTeeResetClicked = Ui()->DoButton_FontIcon(&s_JellyTeeResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &ResetHitbox, BUTTONFLAG_LEFT);
			GameClient()->m_Tooltips.DoToolTip(&s_JellyTeeResetButton, &ResetHitbox, BCLocalize("Reset to defaults"));
			if(JellyTeeResetClicked)
			{
				g_Config.m_BcJellyTeeOthers = DefaultConfig::BcJellyTeeOthers;
				g_Config.m_BcJellyTeeStrength = DefaultConfig::BcJellyTeeStrength;
				g_Config.m_BcJellyTeeDuration = DefaultConfig::BcJellyTeeDuration;
			}
			Ui()->DoLabel(&TitleLabel, BCLocalize("Jelly Tee"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcJellyTee, BCLocalize("Enable Jelly Tee"), &g_Config.m_BcJellyTee, &Content, LineSize);

			const float ExtraHeight = ExtraTargetHeight * s_JellyTeePhase;
			if(!JellyTeeResetClicked && ExtraHeight > 0.0f)
			{
				Content.HSplitTop(ExtraHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExtraTargetHeight};

				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcJellyTeeOthers, BCLocalize("Jelly Others"), &g_Config.m_BcJellyTeeOthers, &Expand, LineSize);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcJellyTeeStrength, &g_Config.m_BcJellyTeeStrength, &Row, BCLocalize("Jelly strength"), 0, 1000);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcJellyTeeDuration, &g_Config.m_BcJellyTeeDuration, &Row, BCLocalize("Jelly duration"), 1, 500);
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		// 3D particles (left column block)
		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_3D_PARTICLES))
		{
			const float ColorPickerLineSize = 25.0f;
			const float ColorPickerLabelSize = 13.0f;
			const float ColorPickerSpacing = 5.0f;
			static float s_Bc3dParticlesPhase = 0.0f;
			static float s_Bc3dParticlesGlowPhase = 0.0f;
			static CButtonContainer s_3DParticlesResetButton;
			const bool ParticlesEnabled = g_Config.m_Bc3dParticles != 0;
			UpdateRevealPhase(s_Bc3dParticlesPhase, ParticlesEnabled);
			const bool ShowCustomColor = ParticlesEnabled && g_Config.m_Bc3dParticlesColorMode == 1;
			const bool ShowGlowOptions = ParticlesEnabled && g_Config.m_Bc3dParticlesGlow != 0;
			if(BCUiAnimations::Enabled())
				BCUiAnimations::UpdatePhase(s_Bc3dParticlesGlowPhase, ShowGlowOptions ? 1.0f : 0.0f, Client()->RenderFrameTime(), 0.16f);
			else
				s_Bc3dParticlesGlowPhase = ShowGlowOptions ? 1.0f : 0.0f;
			const float GlowTargetHeight = 2.0f * LineSize;
			const float BaseTargetHeight = 7.0f * LineSize + (ShowCustomColor ? ColorPickerLineSize + ColorPickerSpacing : 0.0f);
			const float ExtraTargetHeight = BaseTargetHeight + GlowTargetHeight * s_Bc3dParticlesGlowPhase;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExtraTargetHeight * s_Bc3dParticlesPhase;
			CUIRect Content, Label, Row, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			CUIRect TitleLabel, ResetButton, ResetHitbox;
			Label.VSplitRight(LineSize + 8.0f, &TitleLabel, &ResetButton);
			ResetHitbox = ResetButton;
			const bool Particles3DResetClicked = Ui()->DoButton_FontIcon(&s_3DParticlesResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &ResetHitbox, BUTTONFLAG_LEFT);
			GameClient()->m_Tooltips.DoToolTip(&s_3DParticlesResetButton, &ResetHitbox, BCLocalize("Reset to defaults"));
			if(Particles3DResetClicked)
			{
				g_Config.m_Bc3dParticlesType = DefaultConfig::Bc3dParticlesType;
				g_Config.m_Bc3dParticlesCount = DefaultConfig::Bc3dParticlesCount;
				g_Config.m_Bc3dParticlesSizeMin = DefaultConfig::Bc3dParticlesSizeMin;
				g_Config.m_Bc3dParticlesSizeMax = DefaultConfig::Bc3dParticlesSizeMax;
				g_Config.m_Bc3dParticlesSpeed = DefaultConfig::Bc3dParticlesSpeed;
				g_Config.m_Bc3dParticlesDepth = DefaultConfig::Bc3dParticlesDepth;
				g_Config.m_Bc3dParticlesAlpha = DefaultConfig::Bc3dParticlesAlpha;
				g_Config.m_Bc3dParticlesFadeInMs = DefaultConfig::Bc3dParticlesFadeInMs;
				g_Config.m_Bc3dParticlesFadeOutMs = DefaultConfig::Bc3dParticlesFadeOutMs;
				g_Config.m_Bc3dParticlesPushRadius = DefaultConfig::Bc3dParticlesPushRadius;
				g_Config.m_Bc3dParticlesPushStrength = DefaultConfig::Bc3dParticlesPushStrength;
				g_Config.m_Bc3dParticlesCollide = DefaultConfig::Bc3dParticlesCollide;
				g_Config.m_Bc3dParticlesViewMargin = DefaultConfig::Bc3dParticlesViewMargin;
				g_Config.m_Bc3dParticlesColorMode = DefaultConfig::Bc3dParticlesColorMode;
				g_Config.m_Bc3dParticlesColor = DefaultConfig::Bc3dParticlesColor;
				g_Config.m_Bc3dParticlesGlow = DefaultConfig::Bc3dParticlesGlow;
				g_Config.m_Bc3dParticlesGlowAlpha = DefaultConfig::Bc3dParticlesGlowAlpha;
				g_Config.m_Bc3dParticlesGlowOffset = DefaultConfig::Bc3dParticlesGlowOffset;
			}
			Ui()->DoLabel(&TitleLabel, BCLocalize("3D Particles"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_Bc3dParticles, BCLocalize("3D Particles"), &g_Config.m_Bc3dParticles, &Content, LineSize);

			const float ExpandedHeight = ExtraTargetHeight * s_Bc3dParticlesPhase;
			if(!Particles3DResetClicked && ExpandedHeight > 0.0f)
			{
				Content.HSplitTop(ExpandedHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExtraTargetHeight};

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesCount, &g_Config.m_Bc3dParticlesCount, &Row, BCLocalize("Particles count"), 1, 200);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				CUIRect TypeLabel, TypeSelect;
				Row.VSplitLeft(150.0f, &TypeLabel, &TypeSelect);
				Ui()->DoLabel(&TypeLabel, BCLocalize("Particle type"), 14.0f, TEXTALIGN_ML);

				static CUi::SDropDownState s_3DParticlesTypeState;
				static CScrollRegion s_3DParticlesTypeScrollRegion;
				s_3DParticlesTypeState.m_SelectionPopupContext.m_pScrollRegion = &s_3DParticlesTypeScrollRegion;
				const char *Ap3DParticleTypes[3] = {
					BCLocalize("Cube"),
					BCLocalize("Heart"),
					BCLocalize("Mixed"),
				};
				g_Config.m_Bc3dParticlesType = Ui()->DoDropDown(&TypeSelect, g_Config.m_Bc3dParticlesType - 1, Ap3DParticleTypes, (int)std::size(Ap3DParticleTypes), s_3DParticlesTypeState) + 1;

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesSizeMax, &g_Config.m_Bc3dParticlesSizeMax, &Row, BCLocalize("Size"), 2, 200);
				g_Config.m_Bc3dParticlesSizeMin = std::max(2, g_Config.m_Bc3dParticlesSizeMax - 3);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesSpeed, &g_Config.m_Bc3dParticlesSpeed, &Row, BCLocalize("Speed"), 1, 500);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesAlpha, &g_Config.m_Bc3dParticlesAlpha, &Row, BCLocalize("Alpha"), 1, 100);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				CUIRect ColorModeLabel, ColorModeSelect;
				Row.VSplitLeft(150.0f, &ColorModeLabel, &ColorModeSelect);
				Ui()->DoLabel(&ColorModeLabel, BCLocalize("Color mode"), 14.0f, TEXTALIGN_ML);

				static CUi::SDropDownState s_3DParticlesColorModeState;
				static CScrollRegion s_3DParticlesColorModeScrollRegion;
				s_3DParticlesColorModeState.m_SelectionPopupContext.m_pScrollRegion = &s_3DParticlesColorModeScrollRegion;
				const char *Ap3DParticleColorModes[2] = {
					BCLocalize("Custom"),
					BCLocalize("Random"),
				};
				g_Config.m_Bc3dParticlesColorMode = Ui()->DoDropDown(&ColorModeSelect, g_Config.m_Bc3dParticlesColorMode - 1, Ap3DParticleColorModes, (int)std::size(Ap3DParticleColorModes), s_3DParticlesColorModeState) + 1;

				if(g_Config.m_Bc3dParticlesColorMode == 1)
				{
					static CButtonContainer s_3DParticlesColorButton;
					DoLine_ColorPicker(&s_3DParticlesColorButton, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerSpacing, &Expand, BCLocalize("Color"), &g_Config.m_Bc3dParticlesColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
				}

				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_Bc3dParticlesGlow, BCLocalize("Glow"), &g_Config.m_Bc3dParticlesGlow, &Expand, LineSize);

				const float GlowHeight = GlowTargetHeight * s_Bc3dParticlesGlowPhase;
				if(GlowHeight > 0.0f)
				{
					CUIRect GlowVisible;
					Expand.HSplitTop(GlowHeight, &GlowVisible, &Expand);
					Ui()->ClipEnable(&GlowVisible);
					SScopedClip GlowClipGuard{Ui()};

					CUIRect GlowExpand = {GlowVisible.x, GlowVisible.y, GlowVisible.w, GlowTargetHeight};
					GlowExpand.HSplitTop(LineSize, &Row, &GlowExpand);
					Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesGlowAlpha, &g_Config.m_Bc3dParticlesGlowAlpha, &Row, BCLocalize("Glow alpha"), 1, 100);
					GlowExpand.HSplitTop(LineSize, &Row, &GlowExpand);
					Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesGlowOffset, &g_Config.m_Bc3dParticlesGlowOffset, &Row, BCLocalize("Glow offset"), 1, 20);
				}
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		// Media background (left column block)
		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_MEDIA_BACKGROUND))
		{
			const float ContentHeight = LineSize + MarginSmall + 5.0f * LineSize + MarginSmall;
			CUIRect Content, Label, Row;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("Media Background"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			const bool MenuMediaChanged = DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMenuMediaBackground, BCLocalize("Enable to main menu"), &g_Config.m_BcMenuMediaBackground, &Content, LineSize);
			const bool GameMediaChanged = DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcGameMediaBackground, BCLocalize("Enable to game background"), &g_Config.m_BcGameMediaBackground, &Content, LineSize);
			if(MenuMediaChanged || GameMediaChanged)
				m_MenuMediaBackground.ReloadFromConfig();

			struct SMenuMediaFileListContext
			{
				std::vector<std::string> *m_pLabels;
				std::vector<std::string> *m_pPaths;
			};

			auto MenuMediaFileListScan = [](const char *pName, int IsDir, int StorageType, void *pUser) {
				(void)StorageType;
				if(IsDir)
					return 0;

				auto *pContext = static_cast<SMenuMediaFileListContext *>(pUser);
				const std::string Ext = MediaDecoder::ExtractExtensionLower(pName);
				const bool SupportedImage = Ext == "png" || Ext == "jpg" || Ext == "jpeg" || Ext == "webp" || Ext == "bmp" || Ext == "avif" || Ext == "gif";
				const bool SupportedVideo = Ext == "mp4" || Ext == "webm" || Ext == "mov" || Ext == "m4v" || Ext == "mkv" || Ext == "avi";
				if(!SupportedImage && !SupportedVideo)
					return 0;

				pContext->m_pLabels->emplace_back(pName);
				pContext->m_pPaths->emplace_back(std::string("BestClient/backgrounds/") + pName);
				return 0;
			};

			Storage()->CreateFolder("BestClient", IStorage::TYPE_SAVE);
			Storage()->CreateFolder("BestClient/backgrounds", IStorage::TYPE_SAVE);

			static std::vector<std::string> s_vMenuMediaFileLabels;
			static std::vector<std::string> s_vMenuMediaFilePaths;
			s_vMenuMediaFileLabels.clear();
			s_vMenuMediaFilePaths.clear();
			SMenuMediaFileListContext MenuMediaContext{&s_vMenuMediaFileLabels, &s_vMenuMediaFilePaths};
			Storage()->ListDirectory(IStorage::TYPE_SAVE, "BestClient/backgrounds", MenuMediaFileListScan, &MenuMediaContext);

			std::vector<int> vSortedIndices(s_vMenuMediaFileLabels.size());
			for(size_t i = 0; i < vSortedIndices.size(); ++i)
				vSortedIndices[i] = (int)i;
			std::sort(vSortedIndices.begin(), vSortedIndices.end(), [&](int Left, int Right) {
				return str_comp_nocase(s_vMenuMediaFileLabels[Left].c_str(), s_vMenuMediaFileLabels[Right].c_str()) < 0;
			});

			static std::vector<std::string> s_vMenuMediaDropDownLabels;
			static std::vector<const char *> s_vMenuMediaDropDownLabelPtrs;
			s_vMenuMediaDropDownLabels.clear();
			s_vMenuMediaDropDownLabelPtrs.clear();
			for(int SortedIndex : vSortedIndices)
				s_vMenuMediaDropDownLabels.push_back(s_vMenuMediaFileLabels[SortedIndex]);
			for(const std::string &LabelString : s_vMenuMediaDropDownLabels)
				s_vMenuMediaDropDownLabelPtrs.push_back(LabelString.c_str());

			int SelectedMediaFile = -1;
			for(size_t i = 0; i < vSortedIndices.size(); ++i)
			{
				const int SortedIndex = vSortedIndices[i];
				if(str_comp(g_Config.m_BcMenuMediaBackgroundPath, s_vMenuMediaFilePaths[SortedIndex].c_str()) == 0)
				{
					SelectedMediaFile = (int)i;
					break;
				}
			}

			CUIRect MediaPathRow, MediaFileDropDown, MediaReloadButton, MediaFolderButton;
			Content.HSplitTop(MarginSmall, nullptr, &Content);
			Content.HSplitTop(LineSize, &MediaPathRow, &Content);
			MediaPathRow.VSplitRight(20.0f, &MediaPathRow, &MediaFolderButton);
			MediaPathRow.VSplitRight(MarginSmall, &MediaPathRow, nullptr);
			MediaPathRow.VSplitRight(20.0f, &MediaPathRow, &MediaReloadButton);
			MediaPathRow.VSplitRight(MarginSmall, &MediaPathRow, nullptr);
			MediaFileDropDown = MediaPathRow;

			if(s_vMenuMediaDropDownLabelPtrs.empty())
			{
				static CButtonContainer s_MenuMediaEmptyButton;
				DoButton_Menu(&s_MenuMediaEmptyButton, BCLocalize("No media files in backgrounds folder"), -1, &MediaFileDropDown);
			}
			else
			{
				static CUi::SDropDownState s_MenuMediaFileDropDownState;
				static CScrollRegion s_MenuMediaFileDropDownScrollRegion;
				s_MenuMediaFileDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_MenuMediaFileDropDownScrollRegion;
				const int NewSelectedMediaFile = Ui()->DoDropDown(&MediaFileDropDown, SelectedMediaFile, s_vMenuMediaDropDownLabelPtrs.data(), s_vMenuMediaDropDownLabelPtrs.size(), s_MenuMediaFileDropDownState);
				if(NewSelectedMediaFile != SelectedMediaFile && NewSelectedMediaFile >= 0 && NewSelectedMediaFile < (int)vSortedIndices.size())
				{
					const int SortedIndex = vSortedIndices[NewSelectedMediaFile];
					str_copy(g_Config.m_BcMenuMediaBackgroundPath, s_vMenuMediaFilePaths[SortedIndex].c_str(), sizeof(g_Config.m_BcMenuMediaBackgroundPath));
					m_MenuMediaBackground.ReloadFromConfig();
				}
			}

			static CButtonContainer s_MenuMediaReloadButton;
			if(Ui()->DoButton_FontIcon(&s_MenuMediaReloadButton, FontIcon::ARROW_ROTATE_RIGHT, 0, &MediaReloadButton, BUTTONFLAG_LEFT))
				m_MenuMediaBackground.ReloadFromConfig();

			static CButtonContainer s_MenuMediaFolderButton;
			if(Ui()->DoButton_FontIcon(&s_MenuMediaFolderButton, FontIcon::FOLDER, 0, &MediaFolderButton, BUTTONFLAG_LEFT))
			{
				Storage()->CreateFolder("BestClient", IStorage::TYPE_SAVE);
				Storage()->CreateFolder("BestClient/backgrounds", IStorage::TYPE_SAVE);
				char aBuf[IO_MAX_PATH_LENGTH];
				Storage()->GetCompletePath(IStorage::TYPE_SAVE, "BestClient/backgrounds", aBuf, sizeof(aBuf));
				Client()->ViewFile(aBuf);
			}

			Content.HSplitTop(LineSize, &Row, &Content);
			Ui()->DoScrollbarOption(&g_Config.m_BcGameMediaBackgroundOffset, &g_Config.m_BcGameMediaBackgroundOffset, &Row, BCLocalize("Map offset"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");
			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_BcGameMediaBackgroundOffset, &Row, BCLocalize("0 keeps the image fixed to the screen. 100 fixes it to the map for a full parallax effect."));

			Content.HSplitTop(LineSize, &Row, &Content);
			if(m_MenuMediaBackground.HasError())
				TextRender()->TextColor(ColorRGBA(1.0f, 0.45f, 0.45f, 1.0f));
			else if(m_MenuMediaBackground.IsLoaded())
				TextRender()->TextColor(ColorRGBA(0.55f, 1.0f, 0.55f, 1.0f));
			Ui()->DoLabel(&Row, m_MenuMediaBackground.StatusText(), 11.0f, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_EYE_COMFORT))
		{
			static float s_EyeComfortPhase = 0.0f;
			static CButtonContainer s_EyeComfortResetButton;
			const bool EyeComfortEnabled = g_Config.m_BcEyeComfort != 0;
			UpdateRevealPhase(s_EyeComfortPhase, EyeComfortEnabled);
			const float ExtraTargetHeight = LineSize;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExtraTargetHeight * s_EyeComfortPhase;
			CUIRect Content, Label, Row, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			const float ResetButtonWidth = LineSize + 8.0f;
			CUIRect TitleLabel, ResetButton, ResetHitbox;
			Label.VSplitRight(ResetButtonWidth, &TitleLabel, &ResetButton);
			ResetHitbox = ResetButton;
			const bool EyeComfortResetClicked = Ui()->DoButton_FontIcon(&s_EyeComfortResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &ResetHitbox, BUTTONFLAG_LEFT);
			GameClient()->m_Tooltips.DoToolTip(&s_EyeComfortResetButton, &ResetHitbox, BCLocalize("Reset to defaults"));
			if(EyeComfortResetClicked)
				g_Config.m_BcEyeComfortStrength = DefaultConfig::BcEyeComfortStrength;
			Ui()->DoLabel(&TitleLabel, BCLocalize("Eye Comfort"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcEyeComfort, BCLocalize("Enable warm screen filter"), &g_Config.m_BcEyeComfort, &Content, LineSize);

			const float ExpandedHeight = ExtraTargetHeight * s_EyeComfortPhase;
			if(!EyeComfortResetClicked && ExpandedHeight > 0.0f)
			{
				Content.HSplitTop(ExpandedHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExtraTargetHeight};
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcEyeComfortStrength, &g_Config.m_BcEyeComfortStrength, &Row, BCLocalize("Comfort level"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}
		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_GRAFFITI))
		{
			static float s_GraffityPhase = 0.0f;
			const bool GraffityExpanded = g_Config.m_BcGraffityEnabled != 0;
			UpdateRevealPhase(s_GraffityPhase, GraffityExpanded);
			const float KeyReaderLineSize = LineSize;
			const float ExtraTargetHeight = LineSize * 3.0f + KeyReaderLineSize + MarginSmall * 4.0f;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExtraTargetHeight * s_GraffityPhase;
			CUIRect Content, Label, Row, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			{
				const float BadgeWidth = 52.0f;
				const float BadgeSpacing = 4.0f;
				CUIRect TitleLabel, BadgeNew, BadgeBeta;
				Label.VSplitLeft(TextRender()->TextWidth(HeadlineFontSize, BCLocalize("Graffiti")) + BadgeSpacing, &TitleLabel, &Label);
				Label.VSplitLeft(BadgeWidth, &BadgeNew, &Label);
				Label.VSplitLeft(BadgeSpacing, nullptr, &Label);
				Label.VSplitLeft(BadgeWidth, &BadgeBeta, &Label);
				Ui()->DoLabel(&TitleLabel, BCLocalize("Graffiti"), HeadlineFontSize, TEXTALIGN_ML);
				BadgeNew.HMargin(1.5f, &BadgeNew);
				Graphics()->DrawRect4(BadgeNew.x, BadgeNew.y, BadgeNew.w, BadgeNew.h,
					ColorRGBA(1.00f, 0.76f, 0.16f, 1.0f), ColorRGBA(0.92f, 0.56f, 0.02f, 1.0f),
					ColorRGBA(1.00f, 0.76f, 0.16f, 1.0f), ColorRGBA(0.92f, 0.56f, 0.02f, 1.0f),
					IGraphics::CORNER_ALL, 5.0f);
				Ui()->DoLabel(&BadgeNew, "NEW", 11.0f, TEXTALIGN_MC);
				BadgeBeta.HMargin(1.5f, &BadgeBeta);
				Graphics()->DrawRect4(BadgeBeta.x, BadgeBeta.y, BadgeBeta.w, BadgeBeta.h,
					ColorRGBA(0.85f, 0.15f, 0.15f, 1.0f), ColorRGBA(0.65f, 0.05f, 0.05f, 1.0f),
					ColorRGBA(0.85f, 0.15f, 0.15f, 1.0f), ColorRGBA(0.65f, 0.05f, 0.05f, 1.0f),
					IGraphics::CORNER_ALL, 5.0f);
				Ui()->DoLabel(&BadgeBeta, "BETA", 11.0f, TEXTALIGN_MC);
			}
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcGraffityEnabled, BCLocalize("Enable graffiti"), &g_Config.m_BcGraffityEnabled, &Content, LineSize);

			if(ExtraTargetHeight * s_GraffityPhase > 0.0f)
			{
				Content.HSplitTop(ExtraTargetHeight * s_GraffityPhase, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExtraTargetHeight};

				Expand.HSplitTop(MarginSmall, nullptr, &Expand);
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcGraffitySize, &g_Config.m_BcGraffitySize, &Row, BCLocalize("Graffiti size"), 1, 6);

				Expand.HSplitTop(MarginSmall, nullptr, &Expand);
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcGraffitySoundVolume, &g_Config.m_BcGraffitySoundVolume, &Row, BCLocalize("Graffiti spray volume"), 0, 200, &CUi::ms_LogarithmicScrollbarScale, 0u, "%");

				Expand.HSplitTop(MarginSmall, nullptr, &Expand);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcGraffityHoldWheel, BCLocalize("Hold key for graffiti wheel"), &g_Config.m_BcGraffityHoldWheel, &Expand, LineSize);

				Expand.HSplitTop(MarginSmall, nullptr, &Expand);
				Expand.HSplitTop(KeyReaderLineSize, &Label, &Expand);
				static CButtonContainer s_GraffityReaderButton;
				static CButtonContainer s_GraffityClearButton;
				DoLine_KeyReader(Label, s_GraffityReaderButton, s_GraffityClearButton, BCLocalize("Graffiti wheel key"), "+graffity");
			}

			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_CHAT_BUBBLES))
		{
			static float s_BcChatBubblesPhase = 0.0f;
			static CButtonContainer s_ChatBubblesResetButton;
			const bool ChatBubblesEnabled = g_Config.m_BcChatBubbles != 0;
			UpdateRevealPhase(s_BcChatBubblesPhase, ChatBubblesEnabled);
			const float ColorPickerLineSize = 25.0f;
			const float ColorPickerLabelSize = 13.0f;
			const float ColorPickerSpacing = 5.0f;
			const float CustomColorHeight = g_Config.m_BcChatBubbleCustomColors ? 3.0f * (ColorPickerLineSize + ColorPickerSpacing) : 0.0f;
			const float ExtraTargetHeight = MarginSmall + 9.0f * LineSize + CustomColorHeight;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExtraTargetHeight * s_BcChatBubblesPhase;
			CUIRect Content, Label, Row, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			CUIRect TitleLabel, ResetButton, ResetHitbox;
			Label.VSplitRight(LineSize + 8.0f, &TitleLabel, &ResetButton);
			ResetHitbox = ResetButton;
			const bool ChatBubblesResetClicked = Ui()->DoButton_FontIcon(&s_ChatBubblesResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &ResetHitbox, BUTTONFLAG_LEFT);
			GameClient()->m_Tooltips.DoToolTip(&s_ChatBubblesResetButton, &ResetHitbox, BCLocalize("Reset to defaults"));
			if(ChatBubblesResetClicked)
			{
				g_Config.m_BcChatBubblesDemo = DefaultConfig::BcChatBubblesDemo;
				g_Config.m_BcChatBubblesSelf = DefaultConfig::BcChatBubblesSelf;
				g_Config.m_BcChatBubbleSize = DefaultConfig::BcChatBubbleSize;
				g_Config.m_BcChatBubbleShowTime = DefaultConfig::BcChatBubbleShowTime;
				g_Config.m_BcChatBubbleFadeIn = DefaultConfig::BcChatBubbleFadeIn;
				g_Config.m_BcChatBubbleFadeOut = DefaultConfig::BcChatBubbleFadeOut;
				g_Config.m_BcChatBubbleAnimation = DefaultConfig::BcChatBubbleAnimation;
				g_Config.m_BcChatBubbleCustomColors = DefaultConfig::BcChatBubbleCustomColors;
				g_Config.m_BcChatBubbleBgColor = DefaultConfig::BcChatBubbleBgColor;
				g_Config.m_BcChatBubbleTextColor = DefaultConfig::BcChatBubbleTextColor;
				g_Config.m_BcChatBubbleOutlineColor = DefaultConfig::BcChatBubbleOutlineColor;
				g_Config.m_BcChatBubbleRounding = DefaultConfig::BcChatBubbleRounding;
			}
			Ui()->DoLabel(&TitleLabel, BCLocalize("Chat Bubbles"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatBubbles, BCLocalize("Chat Bubbles"), &g_Config.m_BcChatBubbles, &Content, LineSize);
			const float ExtraHeight = ExtraTargetHeight * s_BcChatBubblesPhase;
			if(!ChatBubblesResetClicked && ExtraHeight > 0.0f)
			{
				Content.HSplitTop(ExtraHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExtraTargetHeight};

				Expand.HSplitTop(MarginSmall, nullptr, &Expand);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatBubblesDemo, BCLocalize("Show Chatbubbles in demo"), &g_Config.m_BcChatBubblesDemo, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatBubblesSelf, BCLocalize("Show Chatbubbles above you"), &g_Config.m_BcChatBubblesSelf, &Expand, LineSize);

				CUIRect ModeLabel, ModeDropDown;
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Row.VSplitLeft(120.0f, &ModeLabel, &ModeDropDown);
				Ui()->DoLabel(&ModeLabel, BCLocalize("Appear animation"), 14.0f, TEXTALIGN_ML);
				static CUi::SDropDownState s_ChatBubbleAnimationState;
				static CScrollRegion s_ChatBubbleAnimationScrollRegion;
				s_ChatBubbleAnimationState.m_SelectionPopupContext.m_pScrollRegion = &s_ChatBubbleAnimationScrollRegion;
				const char *apChatBubbleAnimations[4] = {
					BCLocalize("Fade"),
					BCLocalize("Rise"),
					BCLocalize("Slide"),
					BCLocalize("Pop"),
				};
				g_Config.m_BcChatBubbleAnimation = std::clamp(g_Config.m_BcChatBubbleAnimation, 0, 3);
				g_Config.m_BcChatBubbleAnimation = Ui()->DoDropDown(&ModeDropDown, g_Config.m_BcChatBubbleAnimation, apChatBubbleAnimations, (int)std::size(apChatBubbleAnimations), s_ChatBubbleAnimationState);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcChatBubbleSize, &g_Config.m_BcChatBubbleSize, &Row, BCLocalize("Chat Bubble Size"), 20, 30);
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcChatBubbleShowTime, &g_Config.m_BcChatBubbleShowTime, &Row, BCLocalize("Show the Bubbles for"), 200, 1000);
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcChatBubbleFadeIn, &g_Config.m_BcChatBubbleFadeIn, &Row, BCLocalize("fade in for"), 15, 100);
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcChatBubbleFadeOut, &g_Config.m_BcChatBubbleFadeOut, &Row, BCLocalize("fade out for"), 15, 100);
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcChatBubbleRounding, &g_Config.m_BcChatBubbleRounding, &Row, BCLocalize("Rounding"), 0, 200, &CUi::ms_LinearScrollbarScale, 0u, "%");
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatBubbleCustomColors, BCLocalize("Custom colors"), &g_Config.m_BcChatBubbleCustomColors, &Expand, LineSize);
				if(g_Config.m_BcChatBubbleCustomColors)
				{
					static CButtonContainer s_ChatBubbleBgColorButton;
					static CButtonContainer s_ChatBubbleTextColorButton;
					static CButtonContainer s_ChatBubbleOutlineColorButton;
					DoLine_ColorPicker(&s_ChatBubbleBgColorButton, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerSpacing, &Expand, BCLocalize("Background"), &g_Config.m_BcChatBubbleBgColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::BcChatBubbleBgColor, true)), false, nullptr, true);
					DoLine_ColorPicker(&s_ChatBubbleTextColorButton, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerSpacing, &Expand, BCLocalize("Text"), &g_Config.m_BcChatBubbleTextColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::BcChatBubbleTextColor, true)), false, nullptr, true);
					DoLine_ColorPicker(&s_ChatBubbleOutlineColorButton, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerSpacing, &Expand, BCLocalize("Text outline"), &g_Config.m_BcChatBubbleOutlineColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::BcChatBubbleOutlineColor, true)), false, nullptr, true);
				}
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_FLYING_NAMEPLATES))
		{
			static CButtonContainer s_FlyingNamePlatesResetButton;
			const bool ShowFlyingNamePlateSettings = g_Config.m_BcFlyingNamePlates != 0;
			const float FlyingNamePlateSettingsHeight = ShowFlyingNamePlateSettings ? 3.0f * LineSize : 0.0f;
			const float ContentHeight = LineSize + MarginSmall + LineSize + FlyingNamePlateSettingsHeight;

			CUIRect Content, Label, Row;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			const float ResetButtonWidth = LineSize + 8.0f;
			CUIRect TitleLabel, ResetButton, ResetHitbox;
			Label.VSplitRight(ResetButtonWidth, &TitleLabel, &ResetButton);
			ResetHitbox = ResetButton;
			const bool FlyingNamePlatesResetClicked = Ui()->DoButton_FontIcon(&s_FlyingNamePlatesResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &ResetHitbox, BUTTONFLAG_LEFT);
			GameClient()->m_Tooltips.DoToolTip(&s_FlyingNamePlatesResetButton, &ResetHitbox, BCLocalize("Reset to defaults"));
			if(FlyingNamePlatesResetClicked)
			{
				g_Config.m_BcFlyingNamePlatesLift = DefaultConfig::BcFlyingNamePlatesLift;
				g_Config.m_BcFlyingNamePlatesDrag = DefaultConfig::BcFlyingNamePlatesDrag;
				g_Config.m_BcFlyingNamePlatesFollow = DefaultConfig::BcFlyingNamePlatesFollow;
			}
			Ui()->DoLabel(&TitleLabel, BCLocalize("Flying Name Plates"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcFlyingNamePlates, BCLocalize("Enable flying name plates"), &g_Config.m_BcFlyingNamePlates, &Content, LineSize);

			if(!FlyingNamePlatesResetClicked && g_Config.m_BcFlyingNamePlates)
			{
				Content.HSplitTop(LineSize, &Row, &Content);
				Ui()->DoScrollbarOption(&g_Config.m_BcFlyingNamePlatesLift, &g_Config.m_BcFlyingNamePlatesLift, &Row, BCLocalize("Lift above player"), 0, 120);

				Content.HSplitTop(LineSize, &Row, &Content);
				Ui()->DoScrollbarOption(&g_Config.m_BcFlyingNamePlatesDrag, &g_Config.m_BcFlyingNamePlatesDrag, &Row, BCLocalize("Movement drag"), 0, 200);

				Content.HSplitTop(LineSize, &Row, &Content);
				Ui()->DoScrollbarOption(&g_Config.m_BcFlyingNamePlatesFollow, &g_Config.m_BcFlyingNamePlatesFollow, &Row, BCLocalize("Follow speed"), 1, 100);
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		// Afterimage (left column block)
		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_AFTERIMAGE))
		{
			static float s_AfterimagePhase = 0.0f;
			static CButtonContainer s_AfterimageResetButton;
			const bool AfterimageEnabled = g_Config.m_BcAfterimage != 0;
			UpdateRevealPhase(s_AfterimagePhase, AfterimageEnabled);
			const float ExtraTargetHeight = 3.0f * LineSize;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExtraTargetHeight * s_AfterimagePhase;
			CUIRect Content, Label, Row, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			CUIRect TitleLabel, ResetButton, ResetHitbox;
			Label.VSplitRight(LineSize + 8.0f, &TitleLabel, &ResetButton);
			ResetHitbox = ResetButton;
			const bool AfterimageResetClicked = Ui()->DoButton_FontIcon(&s_AfterimageResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &ResetHitbox, BUTTONFLAG_LEFT);
			GameClient()->m_Tooltips.DoToolTip(&s_AfterimageResetButton, &ResetHitbox, BCLocalize("Reset to defaults"));
			if(AfterimageResetClicked)
			{
				g_Config.m_BcAfterimageFrames = DefaultConfig::BcAfterimageFrames;
				g_Config.m_BcAfterimageAlpha = DefaultConfig::BcAfterimageAlpha;
				g_Config.m_BcAfterimageSpacing = DefaultConfig::BcAfterimageSpacing;
			}
			Ui()->DoLabel(&TitleLabel, BCLocalize("Afterimage"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcAfterimage, BCLocalize("Enable Afterimage"), &g_Config.m_BcAfterimage, &Content, LineSize);

			const float ExtraHeight = ExtraTargetHeight * s_AfterimagePhase;
			if(!AfterimageResetClicked && ExtraHeight > 0.0f)
			{
				Content.HSplitTop(ExtraHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExtraTargetHeight};

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcAfterimageFrames, &g_Config.m_BcAfterimageFrames, &Row, BCLocalize("Afterimage frames"), 2, 20);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcAfterimageAlpha, &g_Config.m_BcAfterimageAlpha, &Row, BCLocalize("Afterimage alpha"), 1, 100);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcAfterimageSpacing, &g_Config.m_BcAfterimageSpacing, &Row, BCLocalize("Afterimage spacing"), 1, 64);
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		const float LeftColumnEndY = Column.y;
		Column = RightView;
		Column.HSplitTop(10.0f, nullptr, &Column);

		// Motion blur / frame blend (right column block)
		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_MOTION_BLUR))
		{
			static float s_MotionBlurPhase = 0.0f;
			static CButtonContainer s_MotionBlurResetButton;
			const bool MotionBlurEnabled = g_Config.m_BcMotionBlur != 0;
			const bool IsVulkanBackend = str_find_nocase(Graphics()->GetVersionString(), "vulkan") != nullptr;
#if defined(CONF_PLATFORM_LINUX)
			const bool IsLinuxFrameBlendUnsupported = true;
#else
			const bool IsLinuxFrameBlendUnsupported = false;
#endif
			UpdateRevealPhase(s_MotionBlurPhase, MotionBlurEnabled);
			const float BackendNoteHeight = IsVulkanBackend ? 0.0f : LineSize;
			const float LinuxNoteHeight = IsLinuxFrameBlendUnsupported ? LineSize : 0.0f;
			const float ExtraTargetHeight = LineSize;
			const float ContentHeight = LineSize + MarginSmall + LineSize + BackendNoteHeight + LinuxNoteHeight + ExtraTargetHeight * s_MotionBlurPhase;
			CUIRect Content, Label, Row, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			const float ResetButtonWidth = LineSize + 8.0f;
			const float BadgeWidth = 52.0f;
			const float BadgeSpacing = 4.0f;
			CUIRect TitleLabel, HeaderRight, BadgeSlot, ResetButton, ResetHitbox, Badge;
			Label.VSplitRight(BadgeWidth + BadgeSpacing + ResetButtonWidth, &TitleLabel, &HeaderRight);
			HeaderRight.VSplitLeft(BadgeWidth, &BadgeSlot, &HeaderRight);
			HeaderRight.VSplitLeft(BadgeSpacing, nullptr, &HeaderRight);
			ResetButton = HeaderRight;
			ResetHitbox = ResetButton;
			const bool MotionBlurResetClicked = Ui()->DoButton_FontIcon(&s_MotionBlurResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &ResetHitbox, BUTTONFLAG_LEFT);
			GameClient()->m_Tooltips.DoToolTip(&s_MotionBlurResetButton, &ResetHitbox, BCLocalize("Reset to defaults"));
			if(MotionBlurResetClicked)
				g_Config.m_BcMotionBlurStrength = DefaultConfig::BcMotionBlurStrength;
			Ui()->DoLabel(&TitleLabel, BCLocalize("Motion Blur"), HeadlineFontSize, TEXTALIGN_ML);
			BadgeSlot.HMargin(1.5f, &Badge);
			Graphics()->DrawRect4(
				Badge.x, Badge.y, Badge.w, Badge.h,
				ColorRGBA(0.85f, 0.15f, 0.15f, 1.0f),
				ColorRGBA(0.65f, 0.05f, 0.05f, 1.0f),
				ColorRGBA(0.85f, 0.15f, 0.15f, 1.0f),
				ColorRGBA(0.65f, 0.05f, 0.05f, 1.0f),
				IGraphics::CORNER_ALL, 5.0f);
			Ui()->DoLabel(&Badge, "BETA", 11.0f, TEXTALIGN_MC);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMotionBlur, BCLocalize("Enable motion blur (frame blend)"), &g_Config.m_BcMotionBlur, &Content, LineSize);

			if(!IsVulkanBackend)
			{
				Content.HSplitTop(LineSize, &Row, &Content);
				Ui()->DoLabel(&Row, BCLocalize("Requires the Vulkan backend"), 12.0f, TEXTALIGN_ML);
			}
			if(IsLinuxFrameBlendUnsupported)
			{
				Content.HSplitTop(LineSize, &Row, &Content);
				Ui()->DoLabel(&Row, BCLocalize("Temporarily disabled on Linux"), 12.0f, TEXTALIGN_ML);
			}

			const float ExtraHeight = ExtraTargetHeight * s_MotionBlurPhase;
			if(!MotionBlurResetClicked && ExtraHeight > 0.0f)
			{
				Content.HSplitTop(ExtraHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExtraTargetHeight};
				Expand.HSplitTop(LineSize, &Row, &Expand);
				DoSliderWithScaledValue(&g_Config.m_BcMotionBlurStrength, &g_Config.m_BcMotionBlurStrength, &Row, BCLocalize("Blend strength"), 1, 400, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "%");
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		// Animations (right column block)
		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_ANIMATIONS))
		{
			static float s_AnimationsBlockPhase = 0.0f;
			static CButtonContainer s_AnimationsResetButton;
			const bool AnimationsEnabled = g_Config.m_BcAnimations != 0;
			const float Dt = Client()->RenderFrameTime();
			const bool AnimateBlock = g_Config.m_BcModuleUiRevealAnimation != 0;
			if(AnimateBlock)
				BCUiAnimations::UpdatePhase(s_AnimationsBlockPhase, AnimationsEnabled ? 1.0f : 0.0f, Dt, ModuleUiRevealAnimationDuration());
			else
				s_AnimationsBlockPhase = AnimationsEnabled ? 1.0f : 0.0f;

			const float ExpandedTargetHeight = 12.0f * LineSize;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExpandedTargetHeight * s_AnimationsBlockPhase;
			CUIRect Content, Label, Row, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			CUIRect TitleLabel, ResetButton, ResetHitbox;
			Label.VSplitRight(LineSize + 8.0f, &TitleLabel, &ResetButton);
			ResetHitbox = ResetButton;
			const bool AnimationsResetClicked = Ui()->DoButton_FontIcon(&s_AnimationsResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &ResetHitbox, BUTTONFLAG_LEFT);
			GameClient()->m_Tooltips.DoToolTip(&s_AnimationsResetButton, &ResetHitbox, BCLocalize("Reset to defaults"));
			if(AnimationsResetClicked)
			{
				g_Config.m_BcAnimations = DefaultConfig::BcAnimations;
				g_Config.m_BcModuleUiRevealAnimation = DefaultConfig::BcModuleUiRevealAnimation;
				g_Config.m_BcModuleUiRevealAnimationMs = DefaultConfig::BcModuleUiRevealAnimationMs;
				g_Config.m_BcIngameMenuAnimation = DefaultConfig::BcIngameMenuAnimation;
				g_Config.m_BcIngameMenuAnimationMs = DefaultConfig::BcIngameMenuAnimationMs;
				g_Config.m_BcChatAnimation = DefaultConfig::BcChatAnimation;
				g_Config.m_BcChatAnimationMs = DefaultConfig::BcChatAnimationMs;
				g_Config.m_BcChatOpenAnimation = DefaultConfig::BcChatOpenAnimation;
				g_Config.m_BcChatOpenAnimationMs = DefaultConfig::BcChatOpenAnimationMs;
				g_Config.m_BcChatTypingAnimation = DefaultConfig::BcChatTypingAnimation;
				g_Config.m_BcChatTypingAnimationMs = DefaultConfig::BcChatTypingAnimationMs;
				g_Config.m_BcKillfeedAnimation = DefaultConfig::BcKillfeedAnimation;
				g_Config.m_BcKillfeedAnimationMs = DefaultConfig::BcKillfeedAnimationMs;
				g_Config.m_BcChatAnimationType = DefaultConfig::BcChatAnimationType;
			}
			Ui()->DoLabel(&TitleLabel, BCLocalize("Animations"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcAnimations, BCLocalize("Enable animations"), &g_Config.m_BcAnimations, &Content, LineSize);

			const float ExpandedHeight = ExpandedTargetHeight * s_AnimationsBlockPhase;
			if(!AnimationsResetClicked && ExpandedHeight > 0.0f)
			{
				Content.HSplitTop(ExpandedHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExpandedTargetHeight};
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcModuleUiRevealAnimation, BCLocalize("Module settings reveals"), &g_Config.m_BcModuleUiRevealAnimation, &Expand, LineSize);
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcModuleUiRevealAnimationMs, &g_Config.m_BcModuleUiRevealAnimationMs, &Row, BCLocalize("Module reveal time (ms)"), 1, 500);

				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcIngameMenuAnimation, BCLocalize("ESC menu animation"), &g_Config.m_BcIngameMenuAnimation, &Expand, LineSize);
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcIngameMenuAnimationMs, &g_Config.m_BcIngameMenuAnimationMs, &Row, BCLocalize("ESC menu animation time (ms)"), 1, 500);

				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatAnimation, BCLocalize("Chat message animations"), &g_Config.m_BcChatAnimation, &Expand, LineSize);
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcChatAnimationMs, &g_Config.m_BcChatAnimationMs, &Row, BCLocalize("Chat message animation time (ms)"), 1, 500);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatOpenAnimation, BCLocalize("Chat open animation"), &g_Config.m_BcChatOpenAnimation, &Expand, LineSize);
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcChatOpenAnimationMs, &g_Config.m_BcChatOpenAnimationMs, &Row, BCLocalize("Chat open animation time (ms)"), 1, 500);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatTypingAnimation, BCLocalize("Chat typing animation"), &g_Config.m_BcChatTypingAnimation, &Expand, LineSize);
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcChatTypingAnimationMs, &g_Config.m_BcChatTypingAnimationMs, &Row, BCLocalize("Chat typing animation time (ms)"), 1, 500);

				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcKillfeedAnimation, BCLocalize("Killfeed animation"), &g_Config.m_BcKillfeedAnimation, &Expand, LineSize);
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcKillfeedAnimationMs, &g_Config.m_BcKillfeedAnimationMs, &Row, BCLocalize("Killfeed animation time (ms)"), 1, 500);
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		// Music player (right column block)
		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_MUSIC_PLAYER))
		{
			const float ColorPickerLineSize = 25.0f;
			const float ColorPickerLabelSize = 13.0f;
			const float ColorPickerSpacing = 5.0f;
			static float s_MusicPlayerPhase = 0.0f;
			static float s_MusicPlayerStaticColorPhase = 0.0f;
			static float s_MusicPlayerVisualizerPhase = 0.0f;
			static CButtonContainer s_MusicPlayerResizeButton;
			static CButtonContainer s_MusicPlayerResetButton;
			const bool MusicPlayerEnabled = g_Config.m_BcMusicPlayer != 0;
			if(MusicPlayerEnabled)
			{
				g_Config.m_BcMusicPlayerShowCover = 1;
				g_Config.m_BcMusicPlayerVisualizer = 1;
				g_Config.m_BcMusicPlayerAnimationMs = DefaultConfig::BcMusicPlayerAnimationMs;
				g_Config.m_BcMusicPlayerVisualizerColumnWidth = DefaultConfig::BcMusicPlayerVisualizerColumnWidth;
				g_Config.m_BcMusicPlayerVisualizerGap = DefaultConfig::BcMusicPlayerVisualizerGap;
			}
			const bool StaticColorOn = MusicPlayerEnabled && g_Config.m_BcMusicPlayerColorMode == 0;
			const bool VisualizerOn = MusicPlayerEnabled;
			UpdateRevealPhase(s_MusicPlayerPhase, MusicPlayerEnabled);
			if(BCUiAnimations::Enabled())
			{
				BCUiAnimations::UpdatePhase(s_MusicPlayerStaticColorPhase, StaticColorOn ? 1.0f : 0.0f, Client()->RenderFrameTime(), 0.16f);
				BCUiAnimations::UpdatePhase(s_MusicPlayerVisualizerPhase, VisualizerOn ? 1.0f : 0.0f, Client()->RenderFrameTime(), 0.16f);
			}
			else
			{
				s_MusicPlayerStaticColorPhase = StaticColorOn ? 1.0f : 0.0f;
				s_MusicPlayerVisualizerPhase = VisualizerOn ? 1.0f : 0.0f;
			}

			const float VisualizerSliderHeight = LineSize;
			const float StaticColorTargetHeight = ColorPickerLineSize + ColorPickerSpacing;
			const float VisualizerTargetHeight = LineSize * 6.0f + MarginSmall;
			const float ExtraTargetHeight = LineSize * 2.0f + MarginSmall * 2.0f + VisualizerTargetHeight * s_MusicPlayerVisualizerPhase + StaticColorTargetHeight * s_MusicPlayerStaticColorPhase;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExtraTargetHeight * s_MusicPlayerPhase;
			CUIRect Content, Label, Row, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			const float IconButtonWidth = LineSize + 8.0f;
			const float IconButtonSpacing = 4.0f;
			CUIRect TitleLabel, Buttons, ResizeButton, ResizeHitbox, ResetButton, ResetHitbox;
			Label.VSplitRight(IconButtonWidth * 2.0f + IconButtonSpacing, &TitleLabel, &Buttons);
			Buttons.VSplitLeft(IconButtonWidth, &ResizeButton, &Buttons);
			Buttons.VSplitLeft(IconButtonSpacing, nullptr, &Buttons);
			ResetButton = Buttons;
			ResizeHitbox = ResizeButton;
			DoOpenHudEditorButton(&s_MusicPlayerResizeButton, &ResizeHitbox);
			ResetHitbox = ResetButton;
			const bool MusicPlayerResetClicked = Ui()->DoButton_FontIcon(&s_MusicPlayerResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &ResetHitbox, BUTTONFLAG_LEFT);
			GameClient()->m_Tooltips.DoToolTip(&s_MusicPlayerResetButton, &ResetHitbox, BCLocalize("Reset to defaults"));
			if(MusicPlayerResetClicked)
			{
				g_Config.m_BcMusicPlayerColorMode = DefaultConfig::BcMusicPlayerColorMode;
				g_Config.m_BcMusicPlayerSizeMode = DefaultConfig::BcMusicPlayerSizeMode;
				g_Config.m_BcMusicPlayerStaticColor = DefaultConfig::BcMusicPlayerStaticColor;
				g_Config.m_BcMusicPlayerTextScale = DefaultConfig::BcMusicPlayerTextScale;
				g_Config.m_BcMusicPlayerAnimationMs = DefaultConfig::BcMusicPlayerAnimationMs;
				g_Config.m_BcMusicPlayerShowCover = DefaultConfig::BcMusicPlayerShowCover;
				g_Config.m_BcMusicPlayerUseColorForHud = DefaultConfig::BcMusicPlayerUseColorForHud;
				g_Config.m_BcMusicPlayerHudColorAlpha = DefaultConfig::BcMusicPlayerHudColorAlpha;
				g_Config.m_BcMusicPlayerVisualizer = DefaultConfig::BcMusicPlayerVisualizer;
				g_Config.m_BcMusicPlayerVisualizerMode = DefaultConfig::BcMusicPlayerVisualizerMode;
				g_Config.m_BcMusicPlayerVisualizerSensitivity = DefaultConfig::BcMusicPlayerVisualizerSensitivity;
				g_Config.m_BcMusicPlayerVisualizerSmoothing = DefaultConfig::BcMusicPlayerVisualizerSmoothing;
				g_Config.m_BcMusicPlayerVisualizerRounding = DefaultConfig::BcMusicPlayerVisualizerRounding;
				g_Config.m_BcMusicPlayerVisualizerColumns = DefaultConfig::BcMusicPlayerVisualizerColumns;
				g_Config.m_BcMusicPlayerVisualizerColumnWidth = DefaultConfig::BcMusicPlayerVisualizerColumnWidth;
				g_Config.m_BcMusicPlayerVisualizerGap = DefaultConfig::BcMusicPlayerVisualizerGap;
			}
			Ui()->DoLabel(&TitleLabel, BCLocalize("Music Player"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMusicPlayer, BCLocalize("Enable music player"), &g_Config.m_BcMusicPlayer, &Content, LineSize);

			const float ExpandedHeight = ExtraTargetHeight * s_MusicPlayerPhase;
			if(!MusicPlayerResetClicked && ExpandedHeight > 0.0f)
			{
				Content.HSplitTop(ExpandedHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExtraTargetHeight};

				CUIRect ModeLabel, ModeDropDown;
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Row.VSplitLeft(120.0f, &ModeLabel, &ModeDropDown);
				Ui()->DoLabel(&ModeLabel, BCLocalize("Color mode"), 14.0f, TEXTALIGN_ML);

				static CUi::SDropDownState s_MusicPlayerColorModeState;
				static CScrollRegion s_MusicPlayerColorModeScrollRegion;
				s_MusicPlayerColorModeState.m_SelectionPopupContext.m_pScrollRegion = &s_MusicPlayerColorModeScrollRegion;
				const char *apMusicPlayerColorModes[4] = {
					BCLocalize("Static color"),
					BCLocalize("Cover accent color"),
					BCLocalize("Dominant cover color"),
					BCLocalize("Translucent"),
				};
				g_Config.m_BcMusicPlayerColorMode = std::clamp(g_Config.m_BcMusicPlayerColorMode, 0, 3);
				g_Config.m_BcMusicPlayerColorMode = Ui()->DoDropDown(&ModeDropDown, g_Config.m_BcMusicPlayerColorMode, apMusicPlayerColorModes, (int)std::size(apMusicPlayerColorModes), s_MusicPlayerColorModeState);

				Expand.HSplitTop(MarginSmall, nullptr, &Expand);
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Row.VSplitLeft(120.0f, &ModeLabel, &ModeDropDown);
				Ui()->DoLabel(&ModeLabel, BCLocalize("Size mode"), 14.0f, TEXTALIGN_ML);

				static CUi::SDropDownState s_MusicPlayerSizeModeState;
				static CScrollRegion s_MusicPlayerSizeModeScrollRegion;
				s_MusicPlayerSizeModeState.m_SelectionPopupContext.m_pScrollRegion = &s_MusicPlayerSizeModeScrollRegion;
				const char *apMusicPlayerSizeModes[2] = {
					BCLocalize("Normal"),
					BCLocalize("Mini"),
				};
				g_Config.m_BcMusicPlayerSizeMode = std::clamp(g_Config.m_BcMusicPlayerSizeMode, 0, 1);
				g_Config.m_BcMusicPlayerSizeMode = Ui()->DoDropDown(&ModeDropDown, g_Config.m_BcMusicPlayerSizeMode, apMusicPlayerSizeModes, (int)std::size(apMusicPlayerSizeModes), s_MusicPlayerSizeModeState);

				Expand.HSplitTop(MarginSmall, nullptr, &Expand);
				const float VisualizerHeight = VisualizerTargetHeight * s_MusicPlayerVisualizerPhase;
				if(VisualizerHeight > 0.0f)
				{
					CUIRect VisualizerVisible;
					Expand.HSplitTop(VisualizerHeight, &VisualizerVisible, &Expand);
					Ui()->ClipEnable(&VisualizerVisible);
					SScopedClip VisualizerClipGuard{Ui()};

					CUIRect VisualizerExpand = {VisualizerVisible.x, VisualizerVisible.y, VisualizerVisible.w, VisualizerTargetHeight};
					VisualizerExpand.HSplitTop(LineSize, &Row, &VisualizerExpand);
					Row.VSplitLeft(120.0f, &ModeLabel, &ModeDropDown);
					Ui()->DoLabel(&ModeLabel, BCLocalize("Mode"), 14.0f, TEXTALIGN_ML);

					static CUi::SDropDownState s_MusicPlayerVisualizerModeState;
					static CScrollRegion s_MusicPlayerVisualizerModeScrollRegion;
					s_MusicPlayerVisualizerModeState.m_SelectionPopupContext.m_pScrollRegion = &s_MusicPlayerVisualizerModeScrollRegion;
					const char *apMusicPlayerVisualizerModes[3] = {
						BCLocalize("Bottom"),
						BCLocalize("Center"),
						BCLocalize("Up"),
					};
					g_Config.m_BcMusicPlayerVisualizerMode = std::clamp(g_Config.m_BcMusicPlayerVisualizerMode, 0, 2);
					g_Config.m_BcMusicPlayerVisualizerMode = Ui()->DoDropDown(&ModeDropDown, g_Config.m_BcMusicPlayerVisualizerMode, apMusicPlayerVisualizerModes, (int)std::size(apMusicPlayerVisualizerModes), s_MusicPlayerVisualizerModeState);

					VisualizerExpand.HSplitTop(MarginSmall, nullptr, &VisualizerExpand);
					CUIRect SliderRow;
					// Align slider tracks with the dropdowns/buttons above (label takes a fixed 120px)
					const auto DoMusicPlayerSlider = [&](int *pOption, const CUIRect *pRow, const char *pStr, int Min, int Max, const char *pSuffix) {
						int Value = std::clamp(*pOption, Min, Max);
						char aBuf[256];
						str_format(aBuf, sizeof(aBuf), "%s: %d%s", pStr, Value, pSuffix);
						CUIRect SliderLabel, ScrollBar;
						pRow->VSplitLeft(120.0f, &SliderLabel, &ScrollBar);
						Ui()->DoLabel(&SliderLabel, aBuf, SliderLabel.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_ML);
						const float Rel = (Value - Min) / (float)(Max - Min);
						const float NewRel = Ui()->DoScrollbarH(pOption, &ScrollBar, Rel);
						const int NewValue = std::clamp((int)(Min + NewRel * (Max - Min) + 0.5f), Min, Max);
						if(NewValue != *pOption)
							*pOption = NewValue;
					};

					VisualizerExpand.HSplitTop(VisualizerSliderHeight, &SliderRow, &VisualizerExpand);
					DoMusicPlayerSlider(&g_Config.m_BcMusicPlayerTextScale, &SliderRow, BCLocalize("Text scale"), 70, 150, "%");

					VisualizerExpand.HSplitTop(VisualizerSliderHeight, &SliderRow, &VisualizerExpand);
					DoMusicPlayerSlider(&g_Config.m_BcMusicPlayerVisualizerSensitivity, &SliderRow, BCLocalize("Sensitivity"), 50, 300, "%");

					VisualizerExpand.HSplitTop(VisualizerSliderHeight, &SliderRow, &VisualizerExpand);
					DoMusicPlayerSlider(&g_Config.m_BcMusicPlayerVisualizerSmoothing, &SliderRow, BCLocalize("Smoothing"), 0, 100, "%");

					VisualizerExpand.HSplitTop(VisualizerSliderHeight, &SliderRow, &VisualizerExpand);
					DoMusicPlayerSlider(&g_Config.m_BcMusicPlayerVisualizerColumns, &SliderRow, BCLocalize("Columns"), 5, 10, "");

					VisualizerExpand.HSplitTop(VisualizerSliderHeight, &SliderRow, &VisualizerExpand);
					CUIRect SliderLabel, SliderButton;
					SliderRow.VSplitLeft(120.0f, &SliderLabel, &SliderButton);
					Ui()->DoLabel(&SliderLabel, BCLocalize("Rounding"), 14.0f, TEXTALIGN_ML);
					static CButtonContainer s_MusicPlayerVisualizerRoundingCube;
					static CButtonContainer s_MusicPlayerVisualizerRoundingSoft;
					static CButtonContainer s_MusicPlayerVisualizerRoundingPill;
					const int VisualizerRoundingPreset = MusicPlayerVisualizerRoundingPreset(g_Config.m_BcMusicPlayerVisualizerRounding);
					CUIRect CubeButton, SoftButton, PillButton, Rest;
					const float Spacing = 2.0f;
					const float ButtonWidth = (SliderButton.w - Spacing * 2.0f) / 3.0f;
					SliderButton.VSplitLeft(ButtonWidth, &CubeButton, &Rest);
					Rest.VSplitLeft(Spacing, nullptr, &Rest);
					Rest.VSplitLeft(ButtonWidth, &SoftButton, &Rest);
					Rest.VSplitLeft(Spacing, nullptr, &Rest);
					PillButton = Rest;
					CubeButton.HMargin(2.0f, &CubeButton);
					SoftButton.HMargin(2.0f, &SoftButton);
					PillButton.HMargin(2.0f, &PillButton);
					if(DoButton_Menu(&s_MusicPlayerVisualizerRoundingCube, BCLocalize("Cube"), VisualizerRoundingPreset == 0, &CubeButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
						g_Config.m_BcMusicPlayerVisualizerRounding = 0;
					if(DoButton_Menu(&s_MusicPlayerVisualizerRoundingSoft, BCLocalize("Soft"), VisualizerRoundingPreset == 1, &SoftButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
						g_Config.m_BcMusicPlayerVisualizerRounding = 200;
					if(DoButton_Menu(&s_MusicPlayerVisualizerRoundingPill, BCLocalize("Pill"), VisualizerRoundingPreset == 2, &PillButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
						g_Config.m_BcMusicPlayerVisualizerRounding = 400;
				}

				const float StaticColorHeight = StaticColorTargetHeight * s_MusicPlayerStaticColorPhase;
				if(StaticColorHeight > 0.0f)
				{
					CUIRect StaticVisible;
					Expand.HSplitTop(StaticColorHeight, &StaticVisible, &Expand);
					Ui()->ClipEnable(&StaticVisible);
					SScopedClip StaticClipGuard{Ui()};

					CUIRect StaticExpand = {StaticVisible.x, StaticVisible.y, StaticVisible.w, StaticColorTargetHeight};
					static CButtonContainer s_MusicPlayerStaticColorButton;
					DoLine_ColorPicker(&s_MusicPlayerStaticColorButton, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerSpacing, &StaticExpand, BCLocalize("Static color"), &g_Config.m_BcMusicPlayerStaticColor, ColorRGBA(0.34f, 0.53f, 0.79f, 1.0f), false);
				}
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		// Keystrokes (right column block)
		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_KEYSTROKES))
		{
			static CButtonContainer s_KeystrokesResizeButton;
			static float s_KeyboardPhase = 0.0f;
			static float s_MousePhase = 0.0f;
			UpdateRevealPhase(s_KeyboardPhase, g_Config.m_BcKeystrokesKeyboard != 0);
			UpdateRevealPhase(s_MousePhase, g_Config.m_BcKeystrokesMouse != 0);
			const float KeyboardExpandedHeight = LineSize * s_KeyboardPhase;
			const float MouseExpandedHeight = (LineSize * 2.0f + MarginSmall) * s_MousePhase;
			const float ContentHeight = LineSize + MarginSmall + LineSize + KeyboardExpandedHeight + MarginSmall + LineSize + MouseExpandedHeight;
			CUIRect Content, Label;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			CUIRect TitleLabel, ResizeButton, ResizeHitbox;
			Label.VSplitRight(LineSize + 8.0f, &TitleLabel, &ResizeButton);
			ResizeHitbox = ResizeButton;
			DoOpenHudEditorButton(&s_KeystrokesResizeButton, &ResizeHitbox);
			Ui()->DoLabel(&TitleLabel, BCLocalize("Keystrokes"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcKeystrokesKeyboard, BCLocalize("Show keyboard HUD"), &g_Config.m_BcKeystrokesKeyboard, &Content, LineSize);
			if(g_Config.m_BcKeystrokesKeyboard && !HudLayout::IsEnabled(HudLayout::MODULE_KEYSTROKES_KEYBOARD))
				HudLayout::SetEnabled(HudLayout::MODULE_KEYSTROKES_KEYBOARD, true);
			if(KeyboardExpandedHeight > 0.0f)
			{
				CUIRect Visible;
				Content.HSplitTop(KeyboardExpandedHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClipKb { CUi *m_pUi; ~SScopedClipKb() { m_pUi->ClipDisable(); } } ClipKb{Ui()};
				CUIRect Expand = {Visible.x, Visible.y, Visible.w, LineSize};
				{
					static CButtonContainer s_KeyboardPresetMinimal;
					static CButtonContainer s_KeyboardPresetFull;
					static CButtonContainer s_KeyboardPresetMicro;
					CUIRect MinimalButton, Rest, FullButton, MicroButton;
					const float Spacing = 2.0f;
					const float ButtonWidth = (Expand.w - Spacing * 2.0f) / 3.0f;
					Expand.VSplitLeft(ButtonWidth, &MinimalButton, &Rest);
					Rest.VSplitLeft(Spacing, nullptr, &Rest);
					Rest.VSplitLeft(ButtonWidth, &FullButton, &Rest);
					Rest.VSplitLeft(Spacing, nullptr, &Rest);
					MicroButton = Rest;
					MinimalButton.HMargin(2.0f, &MinimalButton);
					FullButton.HMargin(2.0f, &FullButton);
					MicroButton.HMargin(2.0f, &MicroButton);
					if(DoButton_Menu(&s_KeyboardPresetMinimal, BCLocalize("Minimal"), g_Config.m_BcKeystrokesKeyboardPreset == 0, &MinimalButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
						g_Config.m_BcKeystrokesKeyboardPreset = 0;
					if(DoButton_Menu(&s_KeyboardPresetFull, BCLocalize("Full"), g_Config.m_BcKeystrokesKeyboardPreset == 1, &FullButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
						g_Config.m_BcKeystrokesKeyboardPreset = 1;
					if(DoButton_Menu(&s_KeyboardPresetMicro, BCLocalize("Micro"), g_Config.m_BcKeystrokesKeyboardPreset == 2, &MicroButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
						g_Config.m_BcKeystrokesKeyboardPreset = 2;
				}
			}

			Content.HSplitTop(MarginSmall, nullptr, &Content);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcKeystrokesMouse, BCLocalize("Show mouse HUD"), &g_Config.m_BcKeystrokesMouse, &Content, LineSize);
			if(g_Config.m_BcKeystrokesMouse && !HudLayout::IsEnabled(HudLayout::MODULE_KEYSTROKES_MOUSE))
				HudLayout::SetEnabled(HudLayout::MODULE_KEYSTROKES_MOUSE, true);
			if(MouseExpandedHeight > 0.0f)
			{
				CUIRect Visible;
				Content.HSplitTop(MouseExpandedHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClipMs { CUi *m_pUi; ~SScopedClipMs() { m_pUi->ClipDisable(); } } ClipMs{Ui()};
				CUIRect Expand = {Visible.x, Visible.y, Visible.w, LineSize * 2.0f + MarginSmall};
				CUIRect Row1, Row2;
				Expand.HSplitTop(LineSize, &Row1, &Expand);
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);
				Expand.HSplitTop(LineSize, &Row2, &Expand);
				{
					static CButtonContainer s_MousePresetDot;
					static CButtonContainer s_MousePresetArrow;
					static CButtonContainer s_MousePresetDotDot;
					CUIRect DotButton, Rest, ArrowButton, DotDotButton;
					const float Spacing = 2.0f;
					const float ButtonWidth = (Row1.w - Spacing * 2.0f) / 3.0f;
					Row1.VSplitLeft(ButtonWidth, &DotButton, &Rest);
					Rest.VSplitLeft(Spacing, nullptr, &Rest);
					Rest.VSplitLeft(ButtonWidth, &ArrowButton, &Rest);
					Rest.VSplitLeft(Spacing, nullptr, &Rest);
					DotDotButton = Rest;
					DotButton.HMargin(2.0f, &DotButton);
					ArrowButton.HMargin(2.0f, &ArrowButton);
					DotDotButton.HMargin(2.0f, &DotDotButton);
					if(DoButton_Menu(&s_MousePresetDot, BCLocalize("Dot"), g_Config.m_BcKeystrokesMousePreset == 0, &DotButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
						g_Config.m_BcKeystrokesMousePreset = 0;
					if(DoButton_Menu(&s_MousePresetArrow, BCLocalize("Arrow"), g_Config.m_BcKeystrokesMousePreset == 1, &ArrowButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
						g_Config.m_BcKeystrokesMousePreset = 1;
					if(DoButton_Menu(&s_MousePresetDotDot, BCLocalize("Dot Dot"), g_Config.m_BcKeystrokesMousePreset == 2, &DotDotButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
						g_Config.m_BcKeystrokesMousePreset = 2;
				}
				{
					static CButtonContainer s_MousePresetDotNoBox;
					static CButtonContainer s_MousePresetNoMovement;
					CUIRect Left, Right;
					Row2.VSplitMid(&Left, &Right, 2.0f);
					Left.HMargin(2.0f, &Left);
					Right.HMargin(2.0f, &Right);
					if(DoButton_Menu(&s_MousePresetDotNoBox, BCLocalize("Dot No Box"), g_Config.m_BcKeystrokesMousePreset == 3, &Left, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
						g_Config.m_BcKeystrokesMousePreset = 3;
					if(DoButton_Menu(&s_MousePresetNoMovement, BCLocalize("No movement"), g_Config.m_BcKeystrokesMousePreset == 4, &Right, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
						g_Config.m_BcKeystrokesMousePreset = 4;
				}
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		// Dynamic FOV (right column block)

		// Aspect ratio (right column block)
		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_ASPECT_RATIO))
		{
			const int AspectMode = g_Config.m_BcCustomAspectRatioMode >= 0 ? g_Config.m_BcCustomAspectRatioMode : (g_Config.m_BcCustomAspectRatio > 0 ? 1 : 0);
			const bool IsCustomMode = AspectMode == 2;
			const float ContentHeight = LineSize + MarginSmall + LineSize + MarginSmall + LineSize + (IsCustomMode ? (MarginSmall + LineSize + MarginSmall + LineSize) : 0.0f);
			CUIRect Content, Label, Row;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("Aspect Ratio"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			const auto SplitRowLabelControl = [&](CUIRect &InRow, CUIRect &OutLabel, CUIRect &OutControl) {
				const float LabelWidth = std::clamp(InRow.w * 0.40f, 90.0f, 170.0f);
				InRow.VSplitLeft(LabelWidth, &OutLabel, &OutControl);
			};

			const char *apAspectPresetNames[5] = {
				BCLocalize("Off (default)"),
				"5:4",
				"4:3",
				"3:2",
				BCLocalize("Custom"),
			};
			static const std::array<int, 4> s_aAspectPresetValues = {0, 125, 133, 150};
			static CUi::SDropDownState s_AspectPresetState;
			static CScrollRegion s_AspectPresetScrollRegion;
			s_AspectPresetState.m_SelectionPopupContext.m_pScrollRegion = &s_AspectPresetScrollRegion;

			auto GetAspectPresetIndex = [&]() -> int {
				const int CustomPresetIndex = (int)std::size(apAspectPresetNames) - 1;
				if(AspectMode <= 0 || g_Config.m_BcCustomAspectRatio == 0)
					return 0;
				if(AspectMode == 2)
					return CustomPresetIndex;

				for(size_t i = 1; i < s_aAspectPresetValues.size(); ++i)
				{
					if(g_Config.m_BcCustomAspectRatio == s_aAspectPresetValues[i])
						return (int)i;
				}

				int BestIndex = 1;
				int BestDiff = absolute(g_Config.m_BcCustomAspectRatio - s_aAspectPresetValues[BestIndex]);
				for(size_t i = 2; i < s_aAspectPresetValues.size(); ++i)
				{
					const int CurDiff = absolute(g_Config.m_BcCustomAspectRatio - s_aAspectPresetValues[i]);
					if(CurDiff < BestDiff)
					{
						BestDiff = CurDiff;
						BestIndex = (int)i;
					}
				}
				return BestIndex;
			};

			const int CurrentPreset = GetAspectPresetIndex();
			CUIRect PresetLabel, PresetDropDown;
			Content.HSplitTop(LineSize, &Row, &Content);
			SplitRowLabelControl(Row, PresetLabel, PresetDropDown);
			Ui()->DoLabel(&PresetLabel, BCLocalize("Preset"), 14.0f, TEXTALIGN_ML);
			const int NewPreset = Ui()->DoDropDown(&PresetDropDown, CurrentPreset, apAspectPresetNames, (int)std::size(apAspectPresetNames), s_AspectPresetState);
			const int CustomPresetIndex = (int)std::size(apAspectPresetNames) - 1;
			if(NewPreset != CurrentPreset)
			{
				if(NewPreset == 0)
				{
					g_Config.m_BcCustomAspectRatioMode = 0;
					g_Config.m_BcCustomAspectRatio = 0;
				}
				else if(NewPreset == CustomPresetIndex)
				{
					g_Config.m_BcCustomAspectRatioMode = 2;
					if(g_Config.m_BcCustomAspectRatio < 100)
						g_Config.m_BcCustomAspectRatio = 178;
				}
				else
				{
					g_Config.m_BcCustomAspectRatioMode = 1;
					g_Config.m_BcCustomAspectRatio = s_aAspectPresetValues[NewPreset];
				}
				GameClient()->m_TClient.SetForcedAspect();
			}

			Content.HSplitTop(MarginSmall, nullptr, &Content);
			Content.HSplitTop(LineSize, &Row, &Content);
			CUIRect ApplyLabel, ApplyDropDown;
			SplitRowLabelControl(Row, ApplyLabel, ApplyDropDown);
			Ui()->DoLabel(&ApplyLabel, BCLocalize("Apply"), 14.0f, TEXTALIGN_ML);
			const char *apAspectApplyNames[3] = {
				BCLocalize("Game only"),
				BCLocalize("Full"),
				BCLocalize("Game no HUD"),
			};
			static CUi::SDropDownState s_AspectApplyState;
			static CScrollRegion s_AspectApplyScrollRegion;
			s_AspectApplyState.m_SelectionPopupContext.m_pScrollRegion = &s_AspectApplyScrollRegion;
			const int CurrentApplyMode = g_Config.m_BcCustomAspectRatioApplyMode;
			const int NewApplyMode = Ui()->DoDropDown(&ApplyDropDown, CurrentApplyMode, apAspectApplyNames, (int)std::size(apAspectApplyNames), s_AspectApplyState);
			if(NewApplyMode != CurrentApplyMode)
			{
				g_Config.m_BcCustomAspectRatioApplyMode = NewApplyMode;
				GameClient()->m_TClient.SetForcedAspect();
			}

			const int EffectiveAspectMode = g_Config.m_BcCustomAspectRatioMode >= 0 ? g_Config.m_BcCustomAspectRatioMode : (g_Config.m_BcCustomAspectRatio > 0 ? 1 : 0);
			static CLineInputNumber s_CustomAspectNumeratorInput;
			static CLineInputNumber s_CustomAspectDenominatorInput;
			static int s_CustomPendingAspectValue = -1;
			static bool s_CustomPendingDirty = false;
			static int s_LastAppliedAspectValue = -1;
			if(EffectiveAspectMode == 2)
			{
				const int AppliedAspectValue = maximum(g_Config.m_BcCustomAspectRatio, 100);
				if(!s_CustomPendingDirty && s_LastAppliedAspectValue != AppliedAspectValue)
				{
					s_CustomPendingAspectValue = AppliedAspectValue;
					s_LastAppliedAspectValue = AppliedAspectValue;
				}

				const int DisplayAspectValue = maximum(s_CustomPendingAspectValue, 100);
				if(!s_CustomAspectNumeratorInput.IsActive() && !s_CustomAspectDenominatorInput.IsActive() && DisplayAspectValue != s_LastAppliedAspectValue)
				{
					s_CustomPendingAspectValue = DisplayAspectValue;
					s_LastAppliedAspectValue = DisplayAspectValue;
				}

				if(!s_CustomAspectNumeratorInput.IsActive() && !s_CustomAspectDenominatorInput.IsActive())
				{
					const int Denominator = 1080;
					const int Numerator = maximum(1, (int)std::lround((double)maximum(s_CustomPendingAspectValue, 100) * (double)Denominator / 100.0));
					if(s_CustomAspectNumeratorInput.GetInteger() != Numerator || s_CustomAspectDenominatorInput.GetInteger() != Denominator)
					{
						s_CustomAspectNumeratorInput.SetInteger(Numerator);
						s_CustomAspectDenominatorInput.SetInteger(Denominator);
					}
				}

				Content.HSplitTop(MarginSmall, nullptr, &Content);
				Content.HSplitTop(LineSize, &Row, &Content);
				CUIRect RatioLabel, RatioControls;
				SplitRowLabelControl(Row, RatioLabel, RatioControls);
				Ui()->DoLabel(&RatioLabel, BCLocalize("Custom size"), 14.0f, TEXTALIGN_ML);

				CUIRect NumeratorRect, SeparatorRect, DenominatorRect;
				const float Gap = minimum(6.0f, RatioControls.w * 0.08f);
				const float SeparatorWidth = minimum(12.0f, RatioControls.w * 0.18f);
				const float FieldWidth = maximum(1.0f, (RatioControls.w - SeparatorWidth - 2.0f * Gap) / 2.0f);
				RatioControls.VSplitLeft(FieldWidth, &NumeratorRect, &RatioControls);
				RatioControls.VSplitLeft(Gap, nullptr, &RatioControls);
				RatioControls.VSplitLeft(SeparatorWidth, &SeparatorRect, &RatioControls);
				RatioControls.VSplitLeft(Gap, nullptr, &RatioControls);
				RatioControls.VSplitLeft(FieldWidth, &DenominatorRect, nullptr);

				const bool NumeratorChanged = Ui()->DoEditBox(&s_CustomAspectNumeratorInput, &NumeratorRect, 14.0f);
				Ui()->DoLabel(&SeparatorRect, ":", 14.0f, TEXTALIGN_MC);
				const bool DenominatorChanged = Ui()->DoEditBox(&s_CustomAspectDenominatorInput, &DenominatorRect, 14.0f);

				if(NumeratorChanged || DenominatorChanged)
				{
					const int Numerator = maximum(1, s_CustomAspectNumeratorInput.GetInteger());
					const int Denominator = maximum(1, s_CustomAspectDenominatorInput.GetInteger());
					const int NewAspectValue = std::clamp((int)std::lround((double)Numerator * 100.0 / (double)Denominator), 100, 300);
					if(NewAspectValue != s_CustomPendingAspectValue)
					{
						s_CustomPendingAspectValue = NewAspectValue;
						s_CustomPendingDirty = s_CustomPendingAspectValue != g_Config.m_BcCustomAspectRatio;
					}
				}

				const bool HasPendingCustomChange = s_CustomPendingAspectValue >= 100 && s_CustomPendingAspectValue != g_Config.m_BcCustomAspectRatio;
				Content.HSplitTop(MarginSmall, nullptr, &Content);
				Content.HSplitTop(LineSize, &Row, &Content);
				CUIRect ButtonSpace, ApplyButton;
				SplitRowLabelControl(Row, ButtonSpace, ApplyButton);
				(void)ButtonSpace;
				static CButtonContainer s_AspectApplyButton;
				if(DoButton_Menu(&s_AspectApplyButton, BCLocalize("Apply"), HasPendingCustomChange ? 0 : -1, &ApplyButton) && HasPendingCustomChange)
				{
					g_Config.m_BcCustomAspectRatio = s_CustomPendingAspectValue;
					s_CustomPendingDirty = false;
					s_LastAppliedAspectValue = g_Config.m_BcCustomAspectRatio;
					GameClient()->m_TClient.SetForcedAspect();
				}
			}
			else
			{
				s_CustomPendingDirty = false;
				s_CustomPendingAspectValue = -1;
				s_LastAppliedAspectValue = -1;
			}
		}

		const float RightColumnEndY = Column.y;
		CUIRect ScrollRegion;
		ScrollRegion.x = MainView.x;
		ScrollRegion.y = maximum(LeftColumnEndY, RightColumnEndY) + MarginSmall * 2.0f;
		ScrollRegion.w = MainView.w;
		ScrollRegion.h = 0.0f;
		s_BestClientVisualsScrollRegion.AddRect(ScrollRegion);
		s_BestClientVisualsScrollRegion.End();
	}
	else if(s_CurTab == BESTCLIENT_TAB_GAMEPLAY)
	{
		const float LineSize = 20.0f;
		const float HeadlineFontSize = 20.0f;
		const float FontSize = 14.0f;
		const float EditBoxFontSize = 14.0f;
		const float MarginSmall = 5.0f;
		const float MarginBetweenSections = 30.0f;
		const float MarginBetweenViews = 30.0f;
		const ColorRGBA BlockColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f);
		const auto ModuleUiRevealAnimationsEnabled = [&]() {
			return BCUiAnimations::Enabled() && g_Config.m_BcModuleUiRevealAnimation != 0;
		};
		const auto ModuleUiRevealAnimationDuration = [&]() {
			return BCUiAnimations::MsToSeconds(g_Config.m_BcModuleUiRevealAnimationMs);
		};
		const auto UpdateRevealPhase = [&](float &Phase, bool Expanded) {
			if(ModuleUiRevealAnimationsEnabled())
				BCUiAnimations::UpdatePhase(Phase, Expanded ? 1.0f : 0.0f, Client()->RenderFrameTime(), ModuleUiRevealAnimationDuration());
			else
				Phase = Expanded ? 1.0f : 0.0f;
		};
		const auto DoOpenHudEditorButton = [&](CButtonContainer *pButtonContainer, CUIRect *pButtonRect) {
			const bool CanOpenHudEditor = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
			const bool Clicked = Ui()->DoButton_FontIcon(pButtonContainer, FontIcon::UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, CanOpenHudEditor ? 0 : -1, pButtonRect, BUTTONFLAG_LEFT);
			GameClient()->m_Tooltips.DoToolTip(pButtonContainer, pButtonRect, CanOpenHudEditor ? BCLocalize("Open in HUD editor") : BCLocalize("Join a game first"));
			GameClient()->m_Tooltips.SetFadeTime(pButtonContainer, 0.0f);
			if(Clicked && CanOpenHudEditor)
			{
				SetActive(false);
				GameClient()->m_HudEditor.Activate();
			}
			return Clicked && CanOpenHudEditor;
		};

		static CScrollRegion s_BestClientGameplayScrollRegion;
		vec2 GameplayScrollOffset(0.0f, 0.0f);
		CScrollRegionParams GameplayScrollParams;
		GameplayScrollParams.m_ScrollUnit = 60.0f;
		GameplayScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
		GameplayScrollParams.m_ScrollbarMargin = 5.0f;
		s_BestClientGameplayScrollRegion.Begin(&MainView, &GameplayScrollOffset, &GameplayScrollParams);

		MainView.y += GameplayScrollOffset.y;
		MainView.VSplitRight(5.0f, &MainView, nullptr);
		MainView.VSplitLeft(5.0f, nullptr, &MainView);

		CUIRect LeftView, RightView;
		MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
		LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
		RightView.VSplitRight(MarginSmall, &RightView, nullptr);

		static std::vector<CUIRect> s_SectionBoxes;
		static vec2 s_PrevScrollOffset(0.0f, 0.0f);
		for(CUIRect &Section : s_SectionBoxes)
		{
			float Padding = MarginBetweenViews * 0.6666f;
			Section.w += Padding;
			Section.h += Padding;
			Section.x -= Padding * 0.5f;
			Section.y -= Padding * 0.5f;
			Section.y -= s_PrevScrollOffset.y - GameplayScrollOffset.y;
			Section.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);
		}
		s_PrevScrollOffset = GameplayScrollOffset;
		s_SectionBoxes.clear();

		auto BeginBlock = [&](CUIRect &ColumnRef, float ContentHeight, CUIRect &Content) {
			CUIRect Block;
			ColumnRef.HSplitTop(ContentHeight, &Block, &ColumnRef);
			s_SectionBoxes.push_back(Block);
			Content = Block;
		};

		CUIRect Column = LeftView;
		Column.HSplitTop(10.0f, nullptr, &Column);
		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_GAMEPLAY_INPUT))
		{
			const bool IsSnapTapBlocked = GameClient()->IsSnapTapBlockedByCommunity();
			static float s_FastInputPhase = 0.0f;
			const bool FastInputExpanded = g_Config.m_TcFastInput != 0;
			UpdateRevealPhase(s_FastInputPhase, FastInputExpanded);

			const bool BestInputMode = g_Config.m_BcFastInputMode == 3;
			const float FastInputExtraTargetHeight = BestInputMode ? (MarginSmall * 8.0f + LineSize * 8.0f) : (MarginSmall * 3.0f + LineSize * 3.0f);
			const float ContentHeight = LineSize + MarginSmall + LineSize * 3.0f +
						    FastInputExtraTargetHeight * s_FastInputPhase;

			CUIRect Content, Label, Button, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("Input"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFastInput, BCLocalize("Fast Input"), &g_Config.m_TcFastInput, &Content, LineSize);

			const float FastInputExtraHeight = FastInputExtraTargetHeight * s_FastInputPhase;
			if(FastInputExtraHeight > 0.0f)
			{
				Content.HSplitTop(FastInputExtraHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, FastInputExtraTargetHeight};
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);

				static CButtonContainer s_FastInputModeFast;
				static CButtonContainer s_FastInputModeBest;
				static CButtonContainer s_FastInputModeSaikoPlus;
				const int OldMode = g_Config.m_BcFastInputMode;
				const int UiFastInputMode = g_Config.m_BcFastInputMode == 2 ? 3 : g_Config.m_BcFastInputMode;

				Expand.HSplitTop(LineSize, &Button, &Expand);
				{
					CUIRect FastButton, ButtonsRest, BestButton, SaikoPlusButton;
					const float Spacing = 2.0f;
					const float ButtonWidth = (Button.w - Spacing * 2.0f) / 3.0f;
					Button.VSplitLeft(ButtonWidth, &FastButton, &ButtonsRest);
					ButtonsRest.VSplitLeft(Spacing, nullptr, &ButtonsRest);
					ButtonsRest.VSplitLeft(ButtonWidth, &BestButton, &ButtonsRest);
					ButtonsRest.VSplitLeft(Spacing, nullptr, &ButtonsRest);
					SaikoPlusButton = ButtonsRest;
					FastButton.HMargin(2.0f, &FastButton);
					BestButton.HMargin(2.0f, &BestButton);
					SaikoPlusButton.HMargin(2.0f, &SaikoPlusButton);

					if(DoButton_Menu(&s_FastInputModeFast, BCLocalize("Fast input"), UiFastInputMode == 0, &FastButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
						g_Config.m_BcFastInputMode = 0;
					if(DoButton_Menu(&s_FastInputModeBest, BCLocalize("Best input"), UiFastInputMode == 3, &BestButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
						g_Config.m_BcFastInputMode = 3;
					if(DoButton_Menu(&s_FastInputModeSaikoPlus, "Saiko+", UiFastInputMode == 4, &SaikoPlusButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
						g_Config.m_BcFastInputMode = 4;
				}

				if(g_Config.m_BcFastInputMode != OldMode)
				{
					if(g_Config.m_BcFastInputMode == 0 && g_Config.m_TcFastInputAmount <= 0)
					{
						int SourceAmount = 0;
						if(OldMode == 3)
							SourceAmount = g_Config.m_BcBestInputOffset;
						else if(OldMode == 4)
							SourceAmount = g_Config.m_BcSaikoPlusAmount;
						if(SourceAmount > 0)
							g_Config.m_TcFastInputAmount = std::clamp((SourceAmount + 2) / 5, 0, 40);
					}
					else if(g_Config.m_BcFastInputMode == 4 && g_Config.m_BcSaikoPlusAmount <= 0)
					{
						int SourceAmount = 0;
						if(OldMode == 0)
							SourceAmount = g_Config.m_TcFastInputAmount * 5;
						else if(OldMode == 3)
							SourceAmount = g_Config.m_BcBestInputOffset;
						if(SourceAmount > 0)
							g_Config.m_BcSaikoPlusAmount = std::clamp(SourceAmount, 0, 500);
					}
				}

				Expand.HSplitTop(MarginSmall, nullptr, &Expand);
				Expand.HSplitTop(LineSize, &Button, &Expand);

				if(g_Config.m_BcFastInputMode == 0)
				{
					DoSliderWithScaledValue(&g_Config.m_TcFastInputAmount, &g_Config.m_TcFastInputAmount, &Button, BCLocalize("Amount"), 0, 40, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
				}
				else if(g_Config.m_BcFastInputMode == 4)
				{
					const int Min = 0;
					const int Max = 500;
					int *pAmountValue = &g_Config.m_BcSaikoPlusAmount;
					int Value = std::clamp(*pAmountValue, Min, Max);

					const int Increment = std::max(1, (Max - Min) / 35);
					if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_UP) && Ui()->MouseInside(&Button))
						Value = std::clamp(Value + Increment, Min, Max);
					if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN) && Ui()->MouseInside(&Button))
						Value = std::clamp(Value - Increment, Min, Max);

					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "%s: %.2f ticks", "Saiko+", Value / 100.0f);

					CUIRect AmountLabel, ScrollBar;
					Button.VSplitMid(&AmountLabel, &ScrollBar, minimum(10.0f, Button.w * 0.05f));
					const float LabelFontSize = AmountLabel.h * CUi::ms_FontmodHeight * 0.8f;
					Ui()->DoLabel(&AmountLabel, aBuf, LabelFontSize, TEXTALIGN_ML);

					const float Rel = (Value - Min) / (float)(Max - Min);
					const float NewRel = Ui()->DoScrollbarH(pAmountValue, &ScrollBar, Rel);
					Value = (int)(Min + NewRel * (Max - Min) + 0.5f);
					*pAmountValue = std::clamp(Value, Min, Max);
				}
				else if(g_Config.m_BcFastInputMode == 2 || g_Config.m_BcFastInputMode == 3)
				{
					const CGameClient::SBestInputSettings BestInputSettings = GameClient()->BestInputSettings();
					Button.HMargin(2.0f, &Button);

					static CButtonContainer s_PresetAuto;
					char aAutoPreset[64];
					str_format(aAutoPreset, sizeof(aAutoPreset), "Auto (%d)", GameClient()->CurrentPing());
					if(DoButton_Menu(&s_PresetAuto, aAutoPreset, g_Config.m_BcBestInputPreset == 3, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL))
						g_Config.m_BcBestInputPreset = 3;

					Expand.HSplitTop(MarginSmall, nullptr, &Expand);

					// Prediction offset slider
					Expand.HSplitTop(LineSize, &Button, &Expand);
					{
						const int Min = 0, Max = 1000;
						int Value = std::clamp(BestInputSettings.m_Offset, Min, Max);

						char aBuf[256];
						str_format(aBuf, sizeof(aBuf), "%s: %.2f ticks", BCLocalize("Prediction offset"), Value / 100.0f);

						CUIRect SliderLabel, ScrollBar;
						Button.VSplitMid(&SliderLabel, &ScrollBar, minimum(10.0f, Button.w * 0.05f));
						Ui()->DoLabel(&SliderLabel, aBuf, SliderLabel.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_ML);

						float Rel = (Value - Min) / (float)(Max - Min);
						float NewRel = Ui()->DoScrollbarH(&g_Config.m_BcBestInputOffset, &ScrollBar, Rel);
						int NewValue = std::clamp((int)(Min + NewRel * (Max - Min) + 0.5f), Min, Max);
						if(NewValue != Value)
						{
							g_Config.m_BcBestInputPreset = 0;
							g_Config.m_BcBestInputOffset = NewValue;
						}
					}

					Expand.HSplitTop(MarginSmall, nullptr, &Expand);

					// Input smoothing slider
					Expand.HSplitTop(LineSize, &Button, &Expand);
					{
						const int Min = 0, Max = 100;
						int Value = std::clamp(BestInputSettings.m_Smoothing, Min, Max);

						char aBuf[256];
						str_format(aBuf, sizeof(aBuf), "%s: %d%%", BCLocalize("Input smoothing"), Value);

						CUIRect SliderLabel, ScrollBar;
						Button.VSplitMid(&SliderLabel, &ScrollBar, minimum(10.0f, Button.w * 0.05f));
						Ui()->DoLabel(&SliderLabel, aBuf, SliderLabel.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_ML);

						float Rel = (Value - Min) / (float)(Max - Min);
						float NewRel = Ui()->DoScrollbarH(&g_Config.m_BcBestInputSmoothing, &ScrollBar, Rel);
						int NewValue = std::clamp((int)(Min + NewRel * (Max - Min) + 0.5f), Min, Max);
						if(NewValue != Value)
						{
							g_Config.m_BcBestInputPreset = 0;
							g_Config.m_BcBestInputSmoothing = NewValue;
						}
					}

					Expand.HSplitTop(MarginSmall, nullptr, &Expand);

					// Latency compensation slider
					Expand.HSplitTop(LineSize, &Button, &Expand);
					{
						const int Min = 0, Max = 50;
						int Value = std::clamp(BestInputSettings.m_LatencyComp, Min, Max);

						char aBuf[256];
						str_format(aBuf, sizeof(aBuf), "%s: %d%%", BCLocalize("Latency compensation"), Value);

						CUIRect SliderLabel, ScrollBar;
						Button.VSplitMid(&SliderLabel, &ScrollBar, minimum(10.0f, Button.w * 0.05f));
						Ui()->DoLabel(&SliderLabel, aBuf, SliderLabel.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_ML);

						float Rel = (Value - Min) / (float)(Max - Min);
						float NewRel = Ui()->DoScrollbarH(&g_Config.m_BcBestInputLatencyComp, &ScrollBar, Rel);
						int NewValue = std::clamp((int)(Min + NewRel * (Max - Min) + 0.5f), Min, Max);
						if(NewValue != Value)
						{
							g_Config.m_BcBestInputPreset = 0;
							g_Config.m_BcBestInputLatencyComp = NewValue;
						}
					}

					Expand.HSplitTop(MarginSmall, nullptr, &Expand);

					// Interpolation mode buttons
					CUIRect InterpolationLabel;
					Expand.HSplitTop(LineSize, &InterpolationLabel, &Expand);
					Ui()->DoLabel(&InterpolationLabel, BCLocalize("Interpolation"), InterpolationLabel.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_ML);

					Expand.HSplitTop(MarginSmall, nullptr, &Expand);
					Expand.HSplitTop(LineSize, &Button, &Expand);
					{
						static CButtonContainer s_aInterpolationButtons[3];
						static const char *s_apInterpolationNames[] = {
							"Linear",
							"Cubic",
							"Smooth",
						};
						static const int s_aInterpolationValues[] = {1, 2, 3};

						CUIRect ButtonsRect = Button;
						const float Spacing = 2.0f;
						const float InterpolationButtonWidth = (ButtonsRect.w - Spacing * 2.0f) / 3.0f;
						for(int i = 0; i < 3; ++i)
						{
							CUIRect InterpolationButton;
							if(i < 2)
							{
								ButtonsRect.VSplitLeft(InterpolationButtonWidth, &InterpolationButton, &ButtonsRect);
								ButtonsRect.VSplitLeft(Spacing, nullptr, &ButtonsRect);
							}
							else
								InterpolationButton = ButtonsRect;
							InterpolationButton.HMargin(2.0f, &InterpolationButton);

							int Corners = IGraphics::CORNER_NONE;
							if(i == 0)
								Corners = IGraphics::CORNER_L;
							else if(i == 2)
								Corners = IGraphics::CORNER_R;

							if(DoButton_Menu(&s_aInterpolationButtons[i], BCLocalize(s_apInterpolationNames[i]), g_Config.m_BcBestInputInterpolation == s_aInterpolationValues[i], &InterpolationButton, BUTTONFLAG_LEFT, nullptr, Corners))
								g_Config.m_BcBestInputInterpolation = s_aInterpolationValues[i];
						}
					}
				}

				Expand.HSplitTop(MarginSmall, nullptr, &Expand);
				if(g_Config.m_BcFastInputMode == 0)
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFastInputOthers, BCLocalize("Fast Input others"), &g_Config.m_TcFastInputOthers, &Expand, LineSize);
				else if(g_Config.m_BcFastInputMode == 2 || g_Config.m_BcFastInputMode == 3)
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcBestInputOthers, BCLocalize("Best input others"), &g_Config.m_BcBestInputOthers, &Expand, LineSize);
				else if(g_Config.m_BcFastInputMode == 4)
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcSaikoPlusOthers, "Saiko+ others", &g_Config.m_BcSaikoPlusOthers, &Expand, LineSize);
			}

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClSubTickAiming, BCLocalize("Sub-Tick aiming"), &g_Config.m_ClSubTickAiming, &Content, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcFastInputAutoMargin, BCLocalize("Auto margin"), &g_Config.m_BcFastInputAutoMargin, &Content, LineSize);
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);

			static float s_SnapTapPhase = 0.0f;
			const bool SnapTapExpanded = g_Config.m_BcSnapTap != 0;
			UpdateRevealPhase(s_SnapTapPhase, SnapTapExpanded);
			const float SnapTapExtraTargetHeight = MarginSmall + LineSize;
			const float SnapTapBlockedHintHeight = IsSnapTapBlocked ? (MarginSmall + LineSize) : 0.0f;
			const float SnapTapContentHeight = LineSize + MarginSmall + LineSize +
							   SnapTapBlockedHintHeight +
							   SnapTapExtraTargetHeight * s_SnapTapPhase;

			BeginBlock(Column, SnapTapContentHeight, Content);
			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("Snap Tap"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcSnapTap, BCLocalize("Enable"), &g_Config.m_BcSnapTap, &Content, LineSize);

			const float SnapTapExtraHeight = SnapTapExtraTargetHeight * s_SnapTapPhase;
			if(SnapTapExtraHeight > 0.0f)
			{
				Content.HSplitTop(SnapTapExtraHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, SnapTapExtraTargetHeight};
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);
				Expand.HSplitTop(LineSize, &Button, &Expand);

				const int Min = 0;
				const int Max = 200;
				int Value = std::clamp(g_Config.m_BcSnapTapDelay, Min, Max);
				const int Increment = std::max(1, (Max - Min) / 35);
				if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_UP) && Ui()->MouseInside(&Button))
					Value = std::clamp(Value + Increment, Min, Max);
				if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN) && Ui()->MouseInside(&Button))
					Value = std::clamp(Value - Increment, Min, Max);

				char aBuf[256];
				if(Value == 0)
					str_format(aBuf, sizeof(aBuf), "%s: Off", BCLocalize("Delay"));
				else
					str_format(aBuf, sizeof(aBuf), "%s: %dms", BCLocalize("Delay"), Value);

				CUIRect DelayLabel, ScrollBar;
				Button.VSplitMid(&DelayLabel, &ScrollBar, minimum(10.0f, Button.w * 0.05f));
				const float LabelFontSize = DelayLabel.h * CUi::ms_FontmodHeight * 0.8f;
				Ui()->DoLabel(&DelayLabel, aBuf, LabelFontSize, TEXTALIGN_ML);

				const float Rel = (Value - Min) / (float)(Max - Min);
				const float NewRel = Ui()->DoScrollbarH(&g_Config.m_BcSnapTapDelay, &ScrollBar, Rel);
				Value = (int)(Min + NewRel * (Max - Min) + 0.5f);
				g_Config.m_BcSnapTapDelay = std::clamp(Value, Min, Max);
			}
			if(IsSnapTapBlocked)
			{
				Content.HSplitTop(MarginSmall, nullptr, &Content);
				Content.HSplitTop(LineSize, &Label, &Content);
				TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
				Ui()->DoLabel(&Label, BCLocalize("Looks like you're on a server where this feature is forbidden"), FontSize, TEXTALIGN_ML);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}

			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}
		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_OPTIMIZER))
		{
			static float s_OptimizerPhase = 0.0f;
			static float s_OptimizerFogPhase = 0.0f;
			const float Dt = Client()->RenderFrameTime();
			const bool Enabled = g_Config.m_BcOptimizer != 0;
			const bool FogOn = Enabled && g_Config.m_BcOptimizerFpsFog != 0;
			if(ModuleUiRevealAnimationsEnabled())
			{
				BCUiAnimations::UpdatePhase(s_OptimizerPhase, Enabled ? 1.0f : 0.0f, Dt, ModuleUiRevealAnimationDuration());
				BCUiAnimations::UpdatePhase(s_OptimizerFogPhase, FogOn ? 1.0f : 0.0f, Dt, 0.16f);
			}
			else
			{
				s_OptimizerPhase = Enabled ? 1.0f : 0.0f;
				s_OptimizerFogPhase = FogOn ? 1.0f : 0.0f;
			}

			const float RadioTargetHeight = 22.0f;
			const float FogTargetHeight = 3.0f * LineSize + RadioTargetHeight;
			const float BaseTargetHeight = 5.0f * LineSize;
			const float ExtraTargetHeight = BaseTargetHeight + FogTargetHeight * s_OptimizerFogPhase;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExtraTargetHeight * s_OptimizerPhase;
			CUIRect Content, Label, Row, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("Optimizer"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizer, BCLocalize("Enable optimizer"), &g_Config.m_BcOptimizer, &Content, LineSize);

			const float ExpandedHeight = ExtraTargetHeight * s_OptimizerPhase;
			if(ExpandedHeight > 0.0f)
			{
				Content.HSplitTop(ExpandedHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExtraTargetHeight};
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerDisableParticles, BCLocalize("Disable all particles render"), &g_Config.m_BcOptimizerDisableParticles, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_GfxHighDetail, BCLocalize("High Detail"), &g_Config.m_GfxHighDetail, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerFpsFog, BCLocalize("FPS fog (cull outside limit)"), &g_Config.m_BcOptimizerFpsFog, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerDdnetPriorityHigh, BCLocalize("DDNet priority: High"), &g_Config.m_BcOptimizerDdnetPriorityHigh, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerDiscordPriorityBelowNormal, BCLocalize("Discord priority: Below Normal"), &g_Config.m_BcOptimizerDiscordPriorityBelowNormal, &Expand, LineSize);

				const float FogHeight = FogTargetHeight * s_OptimizerFogPhase;
				if(FogHeight > 0.0f)
				{
					CUIRect FogVisible;
					Expand.HSplitTop(FogHeight, &FogVisible, &Expand);
					Ui()->ClipEnable(&FogVisible);
					SScopedClip FogClipGuard{Ui()};

					CUIRect FogExpand = {FogVisible.x, FogVisible.y, FogVisible.w, FogTargetHeight};
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerFpsFogRenderRect, BCLocalize("Render FPS fog rectangle"), &g_Config.m_BcOptimizerFpsFogRenderRect, &FogExpand, LineSize);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcOptimizerFpsFogCullMapTiles, BCLocalize("Cull map tiles outside FPS fog"), &g_Config.m_BcOptimizerFpsFogCullMapTiles, &FogExpand, LineSize);

					static std::vector<CButtonContainer> s_OptimizerFogModeButtons = {{}, {}};
					int FogMode = g_Config.m_BcOptimizerFpsFogMode;
					if(DoLine_RadioMenu(FogExpand, BCLocalize("FPS fog mode"),
						   s_OptimizerFogModeButtons,
						   {BCLocalize("Manual radius"), BCLocalize("By zoom")},
						   {0, 1},
						   FogMode))
					{
						g_Config.m_BcOptimizerFpsFogMode = FogMode;
					}

					FogExpand.HSplitTop(LineSize, &Row, &FogExpand);
					if(g_Config.m_BcOptimizerFpsFogMode == 0)
						Ui()->DoScrollbarOption(&g_Config.m_BcOptimizerFpsFogRadiusTiles, &g_Config.m_BcOptimizerFpsFogRadiusTiles, &Row, BCLocalize("Radius (tiles)"), 5, 300);
					else
						Ui()->DoScrollbarOption(&g_Config.m_BcOptimizerFpsFogZoomPercent, &g_Config.m_BcOptimizerFpsFogZoomPercent, &Row, BCLocalize("Visible area (%)"), 10, 120, &CUi::ms_LinearScrollbarScale, 0, "%");
				}
			}
		}

		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_GAMEPLAY_GORES_MODE))
		{
			static float s_GoresModePhase = 0.0f;
			const bool GoresModeExpanded = g_Config.m_BcGoresMode != 0;
			UpdateRevealPhase(s_GoresModePhase, GoresModeExpanded);
			const float ExpandedTargetHeight = MarginSmall + LineSize;
			const float ExpandedHeight = ExpandedTargetHeight * s_GoresModePhase;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExpandedHeight;
			CUIRect Content, Label, Visible;
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("Gores mode"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcGoresMode, BCLocalize("Enable gores mode"), &g_Config.m_BcGoresMode, &Content, LineSize);
			if(ExpandedHeight > 0.0f)
			{
				Content.HSplitTop(ExpandedHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExpandedTargetHeight};
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcGoresModeDisableIfWeapons, BCLocalize("Disable if you have shotgun, grenade or laser"), &g_Config.m_BcGoresModeDisableIfWeapons, &Expand, LineSize);
			}
		}

		const float LeftColumnEndY = Column.y;
		Column = RightView;
		Column.HSplitTop(10.0f, nullptr, &Column);

		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_GAMEPLAY_FAST_ACTIONS))
		{
			static char s_aBindName[FAST_ACTIONS_MAX_NAME] = "";
			static char s_aBindCommand[FAST_ACTIONS_MAX_CMD] = "";
			static int s_SelectedBindIndex = 0;
			static int s_LastSelectedBindIndex = -1;

			const float WheelPreviewHeight = 96.0f;
			const float ContentHeight = LineSize + MarginSmall +
						    WheelPreviewHeight + MarginSmall +
						    LineSize + MarginSmall +
						    LineSize + MarginSmall +
						    LineSize + MarginSmall +
						    LineSize + MarginSmall +
						    LineSize * 0.8f + MarginSmall +
						    LineSize;

			CUIRect Content, Label, Button;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("Fast Actions"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			int HoveringIndex = -1;
			CUIRect WheelPreview;
			Content.HSplitTop(WheelPreviewHeight, &WheelPreview, &Content);
			const vec2 Center = WheelPreview.Center();
			const float LineInset = 18.0f;
			const float LineHalfWidth = maximum(40.0f, WheelPreview.w / 2.0f - LineInset);
			const float LineHeight = minimum(WheelPreview.h * 0.78f, 44.0f);
			const float SelectBandHalfHeight = LineHeight * 1.2f;
			const float LabelW = 52.0f;
			const float LabelH = 52.0f;
			const float TextHalfRange = maximum(0.0f, LineHalfWidth - LabelW / 2.0f - 2.0f);

			Graphics()->DrawRect(Center.x - LineHalfWidth, Center.y - LineHeight / 2.0f, LineHalfWidth * 2.0f, LineHeight, ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f), IGraphics::CORNER_ALL, 8.0f);

			const vec2 MouseDelta = Ui()->MousePos() - Center;
			const int SegmentCount = static_cast<int>(GameClient()->m_FastActions.m_vBinds.size());
			const auto IsLegacySlotName = [](const char *pName, int SlotIndex) {
				if(pName[0] == '\0')
					return false;
				char aSlotName[16];
				str_format(aSlotName, sizeof(aSlotName), "%d", SlotIndex + 1);
				return str_comp(pName, aSlotName) == 0;
			};
			const bool HoverInsideLine = absolute(MouseDelta.x) <= LineHalfWidth && absolute(MouseDelta.y) <= SelectBandHalfHeight;
			if(HoverInsideLine && SegmentCount > 0)
			{
				const float HoverPos01 = TextHalfRange > 0.0f ? (MouseDelta.x + TextHalfRange) / (2.0f * TextHalfRange) : 0.5f;
				HoveringIndex = std::clamp((int)std::round(HoverPos01 * (SegmentCount - 1)), 0, SegmentCount - 1);

				if(Ui()->MouseButtonClicked(0) || Ui()->MouseButtonClicked(2))
				{
					s_SelectedBindIndex = HoveringIndex;
					const CFastActions::CBind &Bind = GameClient()->m_FastActions.m_vBinds[HoveringIndex];
					if(IsLegacySlotName(Bind.m_aName, HoveringIndex))
						s_aBindName[0] = '\0';
					else
						str_copy(s_aBindName, Bind.m_aName);
					str_copy(s_aBindCommand, GameClient()->m_FastActions.m_vBinds[HoveringIndex].m_aCommand);
				}
			}

			s_SelectedBindIndex = std::clamp(s_SelectedBindIndex, 0, maximum(0, SegmentCount - 1));
			if(s_SelectedBindIndex != s_LastSelectedBindIndex &&
				s_SelectedBindIndex < static_cast<int>(GameClient()->m_FastActions.m_vBinds.size()))
			{
				const CFastActions::CBind &Bind = GameClient()->m_FastActions.m_vBinds[s_SelectedBindIndex];
				if(IsLegacySlotName(Bind.m_aName, s_SelectedBindIndex))
					s_aBindName[0] = '\0';
				else
					str_copy(s_aBindName, Bind.m_aName);
				str_copy(s_aBindCommand, GameClient()->m_FastActions.m_vBinds[s_SelectedBindIndex].m_aCommand);
				s_LastSelectedBindIndex = s_SelectedBindIndex;
			}

			for(int i = 0; i < static_cast<int>(GameClient()->m_FastActions.m_vBinds.size()); i++)
			{
				TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
				float SegmentFontSize = FontSize * 1.1f;
				if(i == s_SelectedBindIndex)
				{
					SegmentFontSize = FontSize * 1.7f;
					TextRender()->TextColor(ColorRGBA(0.5f, 1.0f, 0.75f, 1.0f));
				}
				else if(i == HoveringIndex)
				{
					SegmentFontSize = FontSize * 1.35f;
				}

				const float Pos01 = GameClient()->m_FastActions.m_vBinds.size() <= 1 ? 0.5f : (float)i / (float)(GameClient()->m_FastActions.m_vBinds.size() - 1);
				const vec2 Pos = vec2(Center.x - TextHalfRange + Pos01 * (TextHalfRange * 2.0f), Center.y);
				const CUIRect Rect = CUIRect{Pos.x - LabelW / 2.0f, Pos.y - LabelH / 2.0f, LabelW, LabelH};
				char aBindPreviewText[16];
				str_format(aBindPreviewText, sizeof(aBindPreviewText), "%d", i + 1);
				Ui()->DoLabel(&Rect, aBindPreviewText, SegmentFontSize, TEXTALIGN_MC);
			}
			TextRender()->TextColor(TextRender()->DefaultTextColor());

			Content.HSplitTop(MarginSmall, nullptr, &Content);
			Content.HSplitTop(LineSize, &Button, &Content);
			char aSlotLabel[64];
			str_format(aSlotLabel, sizeof(aSlotLabel), "%s %d", BCLocalize("Selected slot"), s_SelectedBindIndex + 1);
			Ui()->DoLabel(&Button, aSlotLabel, FontSize, TEXTALIGN_ML);

			Content.HSplitTop(MarginSmall, nullptr, &Content);
			Content.HSplitTop(LineSize, &Button, &Content);
			Button.VSplitLeft(150.0f, &Label, &Button);
			Ui()->DoLabel(&Label, BCLocalize("Name:"), FontSize, TEXTALIGN_ML);
			static CLineInput s_BindNameInput;
			s_BindNameInput.SetBuffer(s_aBindName, sizeof(s_aBindName));
			s_BindNameInput.SetEmptyText(BCLocalize("Name (optional)"));
			Ui()->DoEditBox(&s_BindNameInput, &Button, EditBoxFontSize);

			Content.HSplitTop(MarginSmall, nullptr, &Content);
			Content.HSplitTop(LineSize, &Button, &Content);
			Button.VSplitLeft(150.0f, &Label, &Button);
			Ui()->DoLabel(&Label, BCLocalize("Command:"), FontSize, TEXTALIGN_ML);
			static CLineInput s_BindInput;
			s_BindInput.SetBuffer(s_aBindCommand, sizeof(s_aBindCommand));
			s_BindInput.SetEmptyText(BCLocalize("Command"));
			Ui()->DoEditBox(&s_BindInput, &Button, EditBoxFontSize);

			if(s_SelectedBindIndex < static_cast<int>(GameClient()->m_FastActions.m_vBinds.size()))
			{
				str_copy(GameClient()->m_FastActions.m_vBinds[s_SelectedBindIndex].m_aName, s_aBindName);
				str_copy(GameClient()->m_FastActions.m_vBinds[s_SelectedBindIndex].m_aCommand, s_aBindCommand);
			}

			static CButtonContainer s_ClearButton;
			Content.HSplitTop(MarginSmall, nullptr, &Content);
			Content.HSplitTop(LineSize, &Button, &Content);
			if(DoButton_Menu(&s_ClearButton, BCLocalize("Clear command"), 0, &Button) &&
				s_SelectedBindIndex < static_cast<int>(GameClient()->m_FastActions.m_vBinds.size()))
			{
				GameClient()->m_FastActions.m_vBinds[s_SelectedBindIndex].m_aCommand[0] = '\0';
				s_aBindCommand[0] = '\0';
			}

			Content.HSplitTop(MarginSmall, nullptr, &Content);
			Content.HSplitTop(LineSize * 0.8f, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("In game: hold bind key, press 1..6, release key to execute"), FontSize * 0.8f, TEXTALIGN_ML);

			Content.HSplitTop(MarginSmall, nullptr, &Content);
			Content.HSplitTop(LineSize, &Label, &Content);
			static CButtonContainer s_ReaderButtonWheel;
			static CButtonContainer s_ClearButtonWheel;
			DoLine_KeyReader(Label, s_ReaderButtonWheel, s_ClearButtonWheel, BCLocalize("Fast Actions key"), "+fa");
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_GAMEPLAY_SPEEDRUN_TIMER))
		{
			static float s_SpeedrunPhase = 0.0f;
			const bool SpeedrunExpanded = g_Config.m_BcSpeedrunTimer != 0;
			UpdateRevealPhase(s_SpeedrunPhase, SpeedrunExpanded);
			const float ExpandedTargetHeight = LineSize * 5.0f + MarginSmall * 6.0f;
			const float ExpandedHeight = ExpandedTargetHeight * s_SpeedrunPhase;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExpandedHeight;
			CUIRect Content, Label, Button, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("Speedrun timer"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcSpeedrunTimer, BCLocalize("Enable speedrun timer"), &g_Config.m_BcSpeedrunTimer, &Content, LineSize);
			if(ExpandedHeight > 0.0f)
			{
				if(g_Config.m_BcSpeedrunTimerHours == 0 &&
					g_Config.m_BcSpeedrunTimerMinutes == 0 &&
					g_Config.m_BcSpeedrunTimerSeconds == 0 &&
					g_Config.m_BcSpeedrunTimerMilliseconds == 0 &&
					g_Config.m_BcSpeedrunTimerTime > 0)
				{
					const int LegacyMinutes = g_Config.m_BcSpeedrunTimerTime / 100;
					const int LegacySeconds = g_Config.m_BcSpeedrunTimerTime % 100;
					const int TotalLegacySeconds = LegacyMinutes * 60 + LegacySeconds;
					g_Config.m_BcSpeedrunTimerHours = TotalLegacySeconds / 3600;
					g_Config.m_BcSpeedrunTimerMinutes = (TotalLegacySeconds % 3600) / 60;
					g_Config.m_BcSpeedrunTimerSeconds = TotalLegacySeconds % 60;
				}

				Content.HSplitTop(ExpandedHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExpandedTargetHeight};
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);
				Expand.HSplitTop(LineSize, &Button, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcSpeedrunTimerHours, &g_Config.m_BcSpeedrunTimerHours, &Button, BCLocalize("Hours"), 0, 99);
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);

				Expand.HSplitTop(LineSize, &Button, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcSpeedrunTimerMinutes, &g_Config.m_BcSpeedrunTimerMinutes, &Button, BCLocalize("Minutes"), 0, 59);
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);

				Expand.HSplitTop(LineSize, &Button, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcSpeedrunTimerSeconds, &g_Config.m_BcSpeedrunTimerSeconds, &Button, BCLocalize("Seconds"), 0, 59);
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);

				Expand.HSplitTop(LineSize, &Button, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcSpeedrunTimerMilliseconds, &g_Config.m_BcSpeedrunTimerMilliseconds, &Button, BCLocalize("Milliseconds"), 0, 999, &CUi::ms_LinearScrollbarScale, 0, "ms");
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);

				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcSpeedrunTimerAutoDisable, BCLocalize("Auto disable after time end"), &g_Config.m_BcSpeedrunTimerAutoDisable, &Expand, LineSize);
			}

			// Keep legacy MMSS setting synchronized for backward compatibility.
			g_Config.m_BcSpeedrunTimerTime = g_Config.m_BcSpeedrunTimerMinutes * 100 + g_Config.m_BcSpeedrunTimerSeconds;
		}

		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_GAMEPLAY_FINISH_PREDICTION))
		{
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
			const float ColorPickerLineSize = 25.0f;
			const float ColorPickerLabelSize = 13.0f;
			const float ColorPickerSpacing = 5.0f;
			static float s_FinishPredictionPhase = 0.0f;
			static float s_FinishPredictionTimePhase = 0.0f;
			static CButtonContainer s_FinishPredictionResizeButton;
			const bool FinishPredictionExpanded = g_Config.m_BcFinishPrediction != 0;
			const bool FinishPredictionBarMode = g_Config.m_BcFinishPredictionMode == 1;
			UpdateRevealPhase(s_FinishPredictionPhase, FinishPredictionExpanded);
			const bool ShowTimeExpanded = FinishPredictionExpanded && !FinishPredictionBarMode && g_Config.m_BcFinishPredictionShowTime != 0;
			UpdateRevealPhase(s_FinishPredictionTimePhase, ShowTimeExpanded);
			const float BarColorHeight = FinishPredictionBarMode && g_Config.m_BcFinishPredictionBarCustomColor ? ColorPickerLineSize + ColorPickerSpacing : 0.0f;
			const float ExpandedTargetHeight = FinishPredictionBarMode ?
								   LineSize * 3.0f + BarColorHeight :
								   LineSize * 4.0f + (MarginSmall + LineSize * 2.0f) * s_FinishPredictionTimePhase;
			const float ExpandedHeight = ExpandedTargetHeight * s_FinishPredictionPhase;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExpandedHeight;
			CUIRect Content, Label, Button, Row, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			CUIRect TitleLabel, ResizeButton, ResizeHitbox;
			Label.VSplitRight(LineSize + 8.0f, &TitleLabel, &ResizeButton);
			ResizeHitbox = ResizeButton;
			DoOpenHudEditorButton(&s_FinishPredictionResizeButton, &ResizeHitbox);
			Ui()->DoLabel(&TitleLabel, BCLocalize("Finish Prediction"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcFinishPrediction, BCLocalize("Show finish prediction HUD"), &g_Config.m_BcFinishPrediction, &Content, LineSize);

			if(ExpandedHeight > 0.0f)
			{
				Content.HSplitTop(ExpandedHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExpandedTargetHeight};
				Expand.HSplitTop(LineSize, &Row, &Expand);
				CUIRect ModeLabel, ModeSelect;
				Row.VSplitLeft(150.0f, &ModeLabel, &ModeSelect);
				Ui()->DoLabel(&ModeLabel, BCLocalize("Mode"), 14.0f, TEXTALIGN_ML);
				static CUi::SDropDownState s_FinishPredictionModeState;
				static CScrollRegion s_FinishPredictionModeScrollRegion;
				s_FinishPredictionModeState.m_SelectionPopupContext.m_pScrollRegion = &s_FinishPredictionModeScrollRegion;
				const char *apFinishPredictionModes[2] = {
					BCLocalize("Classic"),
					BCLocalize("Progress bar"),
				};
				g_Config.m_BcFinishPredictionMode = Ui()->DoDropDown(&ModeSelect, std::clamp(g_Config.m_BcFinishPredictionMode, 0, 1), apFinishPredictionModes, (int)std::size(apFinishPredictionModes), s_FinishPredictionModeState);

				if(g_Config.m_BcFinishPredictionMode == 1)
				{
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcFinishPredictionBarCustomColor, BCLocalize("Custom bar color"), &g_Config.m_BcFinishPredictionBarCustomColor, &Expand, LineSize);
					if(g_Config.m_BcFinishPredictionBarCustomColor)
					{
						static CButtonContainer s_FinishPredictionBarColorButton;
						DoLine_ColorPicker(&s_FinishPredictionBarColorButton, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerSpacing, &Expand, BCLocalize("Bar color"), &g_Config.m_BcFinishPredictionBarColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::BcFinishPredictionBarColor, true)), false, nullptr, true);
					}
				}
				else
				{
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcFinishPredictionShowTime, BCLocalize("Show time"), &g_Config.m_BcFinishPredictionShowTime, &Expand, LineSize);

					const float TimeOptionsHeight = (MarginSmall + LineSize * 2.0f) * s_FinishPredictionTimePhase;
					if(TimeOptionsHeight > 0.0f)
					{
						CUIRect TimeVisible;
						Expand.HSplitTop(TimeOptionsHeight, &TimeVisible, &Expand);
						Ui()->ClipEnable(&TimeVisible);
						SScopedClip TimeClipGuard{Ui()};
						CUIRect TimeExpand = {TimeVisible.x, TimeVisible.y, TimeVisible.w, MarginSmall + LineSize * 2.0f};
						TimeExpand.HSplitTop(MarginSmall, nullptr, &TimeExpand);
						TimeExpand.HSplitTop(LineSize, &Button, &TimeExpand);
						static CButtonContainer s_FinishPredictionRemainingButton;
						static CButtonContainer s_FinishPredictionFinishTimeButton;
						CUIRect Left, Right;
						Button.VSplitMid(&Left, &Right, 2.0f);
						Left.HMargin(2.0f, &Left);
						Right.HMargin(2.0f, &Right);
						if(DoButton_Menu(&s_FinishPredictionRemainingButton, BCLocalize("Time left"), g_Config.m_BcFinishPredictionTimeMode == 0, &Left, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
							g_Config.m_BcFinishPredictionTimeMode = 0;
						if(DoButton_Menu(&s_FinishPredictionFinishTimeButton, BCLocalize("Finish time"), g_Config.m_BcFinishPredictionTimeMode == 1, &Right, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
							g_Config.m_BcFinishPredictionTimeMode = 1;
						DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcFinishPredictionShowMillis, BCLocalize("Show milliseconds"), &g_Config.m_BcFinishPredictionShowMillis, &TimeExpand, LineSize);
					}
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcFinishPredictionShowPercentage, BCLocalize("Show percentage"), &g_Config.m_BcFinishPredictionShowPercentage, &Expand, LineSize);
				}
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcFinishPredictionShowAlways, BCLocalize("Show always"), &g_Config.m_BcFinishPredictionShowAlways, &Expand, LineSize);
			}
		}

		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_FOCUS_MODE))
		{
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
			static float s_FocusModePhase = 0.0f;
			const bool Enabled = g_Config.m_ClFocusMode != 0;
			UpdateRevealPhase(s_FocusModePhase, Enabled);

			const float KeyReaderHeight = LineSize + MarginSmall;
			const float ExpandedTargetHeight = KeyReaderHeight + 7.0f * LineSize;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExpandedTargetHeight * s_FocusModePhase;
			CUIRect Content, Label, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("Focus Mode"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusMode, BCLocalize("Enable Focus Mode"), &g_Config.m_ClFocusMode, &Content, LineSize);
			static CButtonContainer s_FocusModeBindReader;
			static CButtonContainer s_FocusModeBindClear;

			const float CurHeight = ExpandedTargetHeight * s_FocusModePhase;
			if(CurHeight > 0.0f)
			{
				Content.HSplitTop(CurHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExpandedTargetHeight};
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideNames, BCLocalize("Hide Player Names"), &g_Config.m_ClFocusModeHideNames, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideEffects, BCLocalize("Hide Visual Effects"), &g_Config.m_ClFocusModeHideEffects, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideHud, BCLocalize("Hide HUD"), &g_Config.m_ClFocusModeHideHud, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideSongPlayer, BCLocalize("Hide Song Player"), &g_Config.m_ClFocusModeHideSongPlayer, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideUI, BCLocalize("Hide Unnecessary UI"), &g_Config.m_ClFocusModeHideUI, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideChat, BCLocalize("Hide Chat"), &g_Config.m_ClFocusModeHideChat, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideScoreboard, BCLocalize("Hide Scoreboard"), &g_Config.m_ClFocusModeHideScoreboard, &Expand, LineSize);
				DoLine_KeyReader(Expand, s_FocusModeBindReader, s_FocusModeBindClear, BCLocalize("Focus mode bind"), "toggle p_focus_mode 0 1");
			}
		}

		const float RightColumnEndY = Column.y;
		CUIRect ScrollRegion;
		ScrollRegion.x = MainView.x;
		ScrollRegion.y = maximum(LeftColumnEndY, RightColumnEndY) + MarginSmall * 2.0f;
		ScrollRegion.w = MainView.w;
		ScrollRegion.h = 0.0f;
		s_BestClientGameplayScrollRegion.AddRect(ScrollRegion);
		s_BestClientGameplayScrollRegion.End();
	}
	else if(s_CurTab == BESTCLIENT_TAB_RESHADE)
	{
#if defined(CONF_FAMILY_WINDOWS)
		RenderSettingsBestClientReShadeTab(this, Storage(), TextRender(), Ui(), Client(), Graphics(), MainView);
#else
		RenderSettingsBestClientReShadeUnsupported(Ui(), MainView);
#endif
	}
	else if(s_CurTab == BESTCLIENT_TAB_OTHERS)
	{
		const float LineSize = 20.0f;
		const float HeadlineFontSize = 20.0f;
		const float MarginSmall = 5.0f;
		const float MarginExtraSmall = 2.5f;
		const float MarginBetweenViews = 30.0f;
		const float MarginBetweenSections = 30.0f;
		const ColorRGBA BlockColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f);
		const auto ModuleUiRevealAnimationsEnabled = [&]() {
			return BCUiAnimations::Enabled() && g_Config.m_BcModuleUiRevealAnimation != 0;
		};
		const auto ModuleUiRevealAnimationDuration = [&]() {
			return BCUiAnimations::MsToSeconds(g_Config.m_BcModuleUiRevealAnimationMs);
		};
		const auto UpdateRevealPhase = [&](float &Phase, bool Expanded) {
			if(ModuleUiRevealAnimationsEnabled())
				BCUiAnimations::UpdatePhase(Phase, Expanded ? 1.0f : 0.0f, Client()->RenderFrameTime(), ModuleUiRevealAnimationDuration());
			else
				Phase = Expanded ? 1.0f : 0.0f;
		};

		static CScrollRegion s_BestClientOthersScrollRegion;
		vec2 OthersScrollOffset(0.0f, 0.0f);
		CScrollRegionParams OthersScrollParams;
		OthersScrollParams.m_ScrollUnit = 60.0f;
		OthersScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
		OthersScrollParams.m_ScrollbarMargin = 5.0f;
		s_BestClientOthersScrollRegion.Begin(&MainView, &OthersScrollOffset, &OthersScrollParams);

		MainView.y += OthersScrollOffset.y;
		MainView.VSplitRight(5.0f, &MainView, nullptr);
		MainView.VSplitLeft(5.0f, nullptr, &MainView);

		static std::vector<CUIRect> s_SectionBoxes;
		static vec2 s_PrevScrollOffset(0.0f, 0.0f);
		for(CUIRect &Section : s_SectionBoxes)
		{
			float Padding = MarginBetweenViews * 0.6666f;
			Section.w += Padding;
			Section.h += Padding;
			Section.x -= Padding * 0.5f;
			Section.y -= Padding * 0.5f;
			Section.y -= s_PrevScrollOffset.y - OthersScrollOffset.y;
			Section.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);
		}
		s_PrevScrollOffset = OthersScrollOffset;
		s_SectionBoxes.clear();

		auto BeginBlock = [&](CUIRect &ColumnRef, float ContentHeight, CUIRect &Content) {
			CUIRect Block;
			ColumnRef.HSplitTop(ContentHeight, &Block, &ColumnRef);
			s_SectionBoxes.push_back(Block);
			Content = Block;
		};

		CUIRect LeftView, RightView;
		MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
		LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
		RightView.VSplitRight(MarginSmall, &RightView, nullptr);

		CUIRect Column = LeftView;
		Column.HSplitTop(10.0f, nullptr, &Column);

		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_OTHERS_MISC))
		{
			const float ColorPickerLineSize = 25.0f;
			const float ColorPickerLabelSize = 13.0f;
			const float ColorPickerLineSpacing = 5.0f;
			const bool ShowRealHitboxEnabled = g_Config.m_BcShowRealHitbox != 0;
			const float ColorPickerHeight = ShowRealHitboxEnabled ? (ColorPickerLineSize + ColorPickerLineSpacing) : 0.0f;
			const float AutoLockDelayHeight = g_Config.m_BcAutoTeamLock ? LineSize : 0.0f;
			const float ContentHeight = LineSize + MarginSmall + 13.0f * LineSize + ColorPickerHeight + AutoLockDelayHeight;
			CUIRect Content, Label, Row;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("Misc"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			static CButtonContainer s_SettingsLayoutButton;
			int UseNewMenuLayout = g_Config.m_BcSettingsLayout == 0 ? 1 : 0;
			DoButton_CheckBoxAutoVMarginAndSet(&s_SettingsLayoutButton, BCLocalize("Use new menu layout"), &UseNewMenuLayout, &Content, LineSize);
			g_Config.m_BcSettingsLayout = UseNewMenuLayout ? 0 : 1;
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcHideHudInSettings, BCLocalize("Hide hud in settings"), &g_Config.m_BcHideHudInSettings, &Content, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcEscPlayerList, BCLocalize("Show ESC players list"), &g_Config.m_BcEscPlayerList, &Content, LineSize);
			Content.HSplitTop(LineSize, &Row, &Content);
			{
				CUIRect CheckBox, LabelRow;
				Row.VSplitLeft(Row.h, &CheckBox, &LabelRow);
				LabelRow.VSplitLeft(5.0f, nullptr, &LabelRow);

				CheckBox.Margin(2.0f, &CheckBox);
				CheckBox.Draw(ColorRGBA(1, 1, 1, 0.25f * Ui()->ButtonColorMul(&g_Config.m_BcShowPointsInTab)), IGraphics::CORNER_ALL, 3.0f);

				if(g_Config.m_BcShowPointsInTab)
				{
					TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
					TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
					Ui()->DoLabel(&CheckBox, FontIcon::XMARK, CheckBox.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
					TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
				}

				TextRender()->SetRenderFlags(0);
				Ui()->DoLabel(&LabelRow, BCLocalize("Show points in tab"), CheckBox.h * CUi::ms_FontmodHeight, TEXTALIGN_ML);

				if(Ui()->DoButtonLogic(&g_Config.m_BcShowPointsInTab, g_Config.m_BcShowPointsInTab != 0 ? 1 : 0, &Row, BUTTONFLAG_LEFT, CUi::EButtonSoundType::CHECKBOX))
					g_Config.m_BcShowPointsInTab ^= 1;
			}

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcEmoticonShadow, BCLocalize("Shadow of Emotions"), &g_Config.m_BcEmoticonShadow, &Content, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatSaveDraft, BCLocalize("Save unsent messages"), &g_Config.m_BcChatSaveDraft, &Content, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatAltCommandLayout, BCLocalize("Commands in other layout"), &g_Config.m_BcChatAltCommandLayout, &Content, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcCinematicCamera, BCLocalize("Cinematic camera"), &g_Config.m_BcCinematicCamera, &Content, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcMastersrv, BCLocalize("Use BestClient MasterServer"), &g_Config.m_BcMastersrv, &Content, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcShowhudDummyCoordIndicator, BCLocalize("Show player below indicator"), &g_Config.m_BcShowhudDummyCoordIndicator, &Content, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcShowRealHitbox, BCLocalize("Show real hitbox"), &g_Config.m_BcShowRealHitbox, &Content, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcAutoTeamLock, BCLocalize("Lock team automatically after joining"), &g_Config.m_BcAutoTeamLock, &Content, LineSize);
			if(g_Config.m_BcAutoTeamLock)
			{
				Content.HSplitTop(LineSize, &Row, &Content);
				Ui()->DoScrollbarOption(&g_Config.m_BcAutoTeamLockDelay, &g_Config.m_BcAutoTeamLockDelay, &Row, BCLocalize("Auto lock delay"), 0, 30, &CUi::ms_LinearScrollbarScale, 0, "s");
			}
			Content.HSplitTop(LineSize, &Row, &Content);
			Ui()->DoScrollbarOption(&g_Config.m_UiScale, &g_Config.m_UiScale, &Row, BCLocalize("UI scale"), 50, 200, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, "%");
			if(g_Config.m_BcShowRealHitbox)
			{
				static CButtonContainer s_RealHitboxDotColorButton;
				DoLine_ColorPicker(&s_RealHitboxDotColorButton, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Content, BCLocalize("Real hitbox dot color"), &g_Config.m_BcShowRealHitboxColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::BcShowRealHitboxColor, true)), false, nullptr, true);
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		{
			static float s_RollbackDemoPhase = 0.0f;
			const float KeyReaderLineSize = LineSize + MarginExtraSmall;
			const float ExpandedTargetHeight = LineSize + KeyReaderLineSize;
			const bool RollbackDemoExpanded = g_Config.m_ClReplays != 0;
			UpdateRevealPhase(s_RollbackDemoPhase, RollbackDemoExpanded);
			const float ExpandedHeight = ExpandedTargetHeight * s_RollbackDemoPhase;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExpandedHeight;
			CUIRect Content, Label, Row, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("Rollback Demo"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClReplays, BCLocalize("Enable rollback demo recording"), &g_Config.m_ClReplays, &Content, LineSize))
			{
				if(Client()->State() == IClient::STATE_ONLINE)
					Client()->DemoRecorder_UpdateReplayRecorder();
			}

			if(ExpandedHeight > 0.0f)
			{
				Content.HSplitTop(ExpandedHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExpandedTargetHeight};

				g_Config.m_ClReplayLength = std::clamp(g_Config.m_ClReplayLength, 10, 60);
				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_ClReplayLength, &g_Config.m_ClReplayLength, &Row, BCLocalize("Rollback length"), 10, 60, &CUi::ms_LinearScrollbarScale, 0, " s");

				static CButtonContainer s_RollbackBindReader;
				static CButtonContainer s_RollbackBindClear;
				DoLine_KeyReader(Expand, s_RollbackBindReader, s_RollbackBindClear, BCLocalize("Rollback bind"), "BC_save_rollback");
			}

			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		{
			const float ContentHeight = LineSize + MarginSmall + 3.0f * LineSize;
			CUIRect Content, Label, Row;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("Browser Utils"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcAutoServerListRefresh, BCLocalize("Auto server list refresh"), &g_Config.m_BcAutoServerListRefresh, &Content, LineSize);
			Content.HSplitTop(LineSize, &Row, &Content);
			Ui()->DoScrollbarOption(&g_Config.m_BcAutoServerListRefreshSeconds, &g_Config.m_BcAutoServerListRefreshSeconds, &Row, BCLocalize("Seconds"), 1, 300, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, " s");
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcUseShortKogServerName, BCLocalize("Use short KoG server name"), &g_Config.m_BcUseShortKogServerName, &Content, LineSize);

			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_OTHERS_CHAT_MEDIA))
		{
			static float s_ChatMediaPhase = 0.0f;
			const bool ChatMediaEnabled = g_Config.m_BcChatMediaPreview != 0;
			const bool ContentFilterEnabled = g_Config.m_BcChatMediaContentFilter != 0;
			UpdateRevealPhase(s_ChatMediaPhase, ChatMediaEnabled);
			const float KeyReaderHeight = LineSize + 2.5f;
			const float BaseExpandedHeight = 6.0f * LineSize + KeyReaderHeight;
			const float FilterSettingsHeight = 2.0f * LineSize;
			const float ExpandedTargetHeight = BaseExpandedHeight + (ContentFilterEnabled ? FilterSettingsHeight : 0.0f);
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExpandedTargetHeight * s_ChatMediaPhase;
			CUIRect Content, Label, Row, Visible;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("Chat Media"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			CChat &Chat = GameClient()->m_Chat;
			if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatMediaPreview, BCLocalize("Render media previews from chat links"), &g_Config.m_BcChatMediaPreview, &Content, LineSize))
				Chat.RebuildChat();

			const float ExpandedHeight = ExpandedTargetHeight * s_ChatMediaPhase;
			if(ExpandedHeight > 0.0f)
			{
				Content.HSplitTop(ExpandedHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExpandedTargetHeight};

				if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatMediaPhotos, BCLocalize("Show photos in chat media"), &g_Config.m_BcChatMediaPhotos, &Expand, LineSize))
					Chat.RebuildChat();

				if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatMediaGifs, BCLocalize("Show GIFs in chat media"), &g_Config.m_BcChatMediaGifs, &Expand, LineSize))
					Chat.RebuildChat();

				if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatMediaContentFilter, BCLocalize("Content filtering"), &g_Config.m_BcChatMediaContentFilter, &Expand, LineSize))
					Chat.RebuildChat();

				if(g_Config.m_BcChatMediaContentFilter)
				{
					Expand.HSplitTop(LineSize, &Row, &Expand);
					Ui()->DoLabel(&Row, BCLocalize("Allowed media domains"), 12.0f, TEXTALIGN_ML);

					Expand.HSplitTop(LineSize, &Row, &Expand);
					static CLineInput s_ChatMediaAllowedDomains(g_Config.m_BcChatMediaAllowedDomains, sizeof(g_Config.m_BcChatMediaAllowedDomains));
					s_ChatMediaAllowedDomains.SetEmptyText("tenor.com; imgur.com; giphy.com");
					if(Ui()->DoClearableEditBox(&s_ChatMediaAllowedDomains, &Row, 14.0f))
						Chat.RebuildChat();
					GameClient()->m_Tooltips.DoToolTip(&s_ChatMediaAllowedDomains, &Row, BCLocalize("Semicolon-separated allowlist, for example: tenor.com; imgur.com; giphy.com; cdn.discordapp.com"));
				}

				Expand.HSplitTop(LineSize, &Row, &Expand);
				if(Ui()->DoScrollbarOption(&g_Config.m_BcChatMediaPreviewMaxWidth, &g_Config.m_BcChatMediaPreviewMaxWidth, &Row, BCLocalize("Media preview width"), 120, 400))
					Chat.RebuildChat();

				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcChatMediaViewer, BCLocalize("Enable fullscreen media viewer in chat"), &g_Config.m_BcChatMediaViewer, &Expand, LineSize);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_BcChatMediaViewerMaxZoom, &g_Config.m_BcChatMediaViewerMaxZoom, &Row, BCLocalize("Viewer max zoom"), 100, 2000, &CUi::ms_LinearScrollbarScale, 0u, "%");

				static CButtonContainer s_HideMediaBindReader;
				static CButtonContainer s_HideMediaBindClear;
				DoLine_KeyReader(Expand, s_HideMediaBindReader, s_HideMediaBindClear, BCLocalize("Hide media bind"), "toggle_chat_media_hidden");
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		const float LeftColumnEndY = Column.y;
		Column = RightView;
		Column.HSplitTop(10.0f, nullptr, &Column);

		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_OTHERS_VOICE_SETTINGS))
		{
			static float s_VoiceSettingsPhase = 0.0f;
			const bool VoiceSettingsExpanded = g_Config.m_BcVoiceChatEnable != 0;
			UpdateRevealPhase(s_VoiceSettingsPhase, VoiceSettingsExpanded);

			const float VoiceSettingsHeight = GameClient()->m_VoiceChat.GetMenuSettingsBlockHeight(s_VoiceSettingsPhase);
			CUIRect VoiceSettingsView;
			BeginBlock(Column, VoiceSettingsHeight, VoiceSettingsView);
			GameClient()->m_VoiceChat.RenderMenuSettingsBlock(VoiceSettingsView, s_VoiceSettingsPhase);
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_OTHERS_VOICE_BINDS))
		{
			static CButtonContainer s_VoicePanelBindReader;
			static CButtonContainer s_VoicePanelBindClear;
			static CButtonContainer s_PushToTalkBindReader;
			static CButtonContainer s_PushToTalkBindClear;
			static CButtonContainer s_MicMuteBindReader;
			static CButtonContainer s_MicMuteBindClear;
			static CButtonContainer s_HeadphonesMuteBindReader;
			static CButtonContainer s_HeadphonesMuteBindClear;

			const float ContentHeight = 4.0f * (LineSize + MarginExtraSmall);
			CUIRect BindsView;
			BeginBlock(Column, ContentHeight, BindsView);
			DoLine_KeyReader(BindsView, s_VoicePanelBindReader, s_VoicePanelBindClear, BCLocalize("Voice panel"), "toggle_voice_panel");
			DoLine_KeyReader(BindsView, s_PushToTalkBindReader, s_PushToTalkBindClear, BCLocalize("Push-to-talk"), "+voicechat");
			DoLine_KeyReader(BindsView, s_MicMuteBindReader, s_MicMuteBindClear, BCLocalize("Mute microphone"), "toggle_voice_mic_mute");
			DoLine_KeyReader(BindsView, s_HeadphonesMuteBindReader, s_HeadphonesMuteBindClear, BCLocalize("Mute headphones"), "toggle_voice_headphones_mute");
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		if(!GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_OTHERS_CLIENT_INDICATOR))
		{
			const bool ShowNamePlateSettings = g_Config.m_BcClientIndicatorInNamePlate != 0;
			const bool ShowScoreboardSettings = g_Config.m_BcClientIndicatorInScoreboard != 0;
			const float NamePlateSettingsHeight = ShowNamePlateSettings ? 2.0f * LineSize : 0.0f;
			const float ScoreboardSettingsHeight = ShowScoreboardSettings ? LineSize : 0.0f;
			const float ContentHeight = LineSize + MarginSmall + 2.0f * LineSize + NamePlateSettingsHeight + ScoreboardSettingsHeight;

			CUIRect Content, Label, Row;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, BCLocalize("Client Indicator"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcClientIndicatorInNamePlate, BCLocalize("Show indicator in name plates"), &g_Config.m_BcClientIndicatorInNamePlate, &Content, LineSize);

			if(g_Config.m_BcClientIndicatorInNamePlate)
			{
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcClientIndicatorInNamePlateAboveSelf, BCLocalize("Show above yourself"), &g_Config.m_BcClientIndicatorInNamePlateAboveSelf, &Content, LineSize);

				Content.HSplitTop(LineSize, &Row, &Content);
				Ui()->DoScrollbarOption(&g_Config.m_BcClientIndicatorInNamePlateSize, &g_Config.m_BcClientIndicatorInNamePlateSize, &Row, BCLocalize("Name plate indicator size"), -50, 100);
			}

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcClientIndicatorInScoreboard, BCLocalize("Show indicator in scoreboard"), &g_Config.m_BcClientIndicatorInScoreboard, &Content, LineSize);
			if(g_Config.m_BcClientIndicatorInScoreboard)
			{
				Content.HSplitTop(LineSize, &Row, &Content);
				Ui()->DoScrollbarOption(&g_Config.m_BcClientIndicatorInSoreboardSize, &g_Config.m_BcClientIndicatorInSoreboardSize, &Row, BCLocalize("Scoreboard indicator size"), -50, 100);
			}

			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		const float RightColumnEndY = Column.y;
		CUIRect ScrollRegion;
		ScrollRegion.x = MainView.x;
		ScrollRegion.y = maximum(LeftColumnEndY, RightColumnEndY) + MarginSmall * 2.0f;
		ScrollRegion.w = MainView.w;
		ScrollRegion.h = 0.0f;
		s_BestClientOthersScrollRegion.AddRect(ScrollRegion);
		s_BestClientOthersScrollRegion.End();
	}
	else if(s_CurTab == BESTCLIENT_TAB_INFO)
	{
		RenderSettingsBestClientInfo(MainView);
	}
}

void CMenus::ComponentsEditorSyncFromConfig()
{
	m_ComponentsEditorState.m_AppliedMaskLo = g_Config.m_BcDisabledComponentsMaskLo;
	m_ComponentsEditorState.m_AppliedMaskHi = g_Config.m_BcDisabledComponentsMaskHi;
	m_ComponentsEditorState.m_StagedMaskLo = m_ComponentsEditorState.m_AppliedMaskLo;
	m_ComponentsEditorState.m_StagedMaskHi = m_ComponentsEditorState.m_AppliedMaskHi;
	m_ComponentsEditorState.m_HasUnsavedChanges = false;
}

void CMenus::ComponentsEditorOpen()
{
	ComponentsEditorSyncFromConfig();
	m_ComponentsEditorState.m_Open = true;
	m_ComponentsEditorState.m_FullscreenOpen = true;
	m_ComponentsEditorState.m_ShowExitConfirm = false;
	m_ComponentsEditorState.m_ShowRestartConfirm = false;
}

void CMenus::ComponentsEditorRequestClose()
{
	if(m_ComponentsEditorState.m_HasUnsavedChanges)
	{
		m_ComponentsEditorState.m_ShowExitConfirm = true;
		return;
	}
	ComponentsEditorCloseNow();
}

void CMenus::ComponentsEditorCloseNow()
{
	m_ComponentsEditorState.m_Open = false;
	m_ComponentsEditorState.m_ShowExitConfirm = false;
	m_ComponentsEditorState.m_ShowRestartConfirm = false;
	m_ComponentsEditorState.m_HasUnsavedChanges = false;
	ComponentsEditorSyncFromConfig();
}

void CMenus::ComponentsEditorApply()
{
	g_Config.m_BcDisabledComponentsMaskLo = m_ComponentsEditorState.m_StagedMaskLo;
	g_Config.m_BcDisabledComponentsMaskHi = m_ComponentsEditorState.m_StagedMaskHi;
	m_ComponentsEditorState.m_AppliedMaskLo = m_ComponentsEditorState.m_StagedMaskLo;
	m_ComponentsEditorState.m_AppliedMaskHi = m_ComponentsEditorState.m_StagedMaskHi;
	m_ComponentsEditorState.m_HasUnsavedChanges = false;
	m_ComponentsEditorState.m_ShowExitConfirm = false;
	m_ComponentsEditorState.m_ShowRestartConfirm = true;
}

void CMenus::ComponentsEditorRenderExitConfirm(const CUIRect &Rect)
{
	const float FontSize = 14.0f;
	const float LineSize = 20.0f;
	const float MarginSmall = 5.0f;

	CUIRect Overlay = Rect;
	Overlay.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.6f), IGraphics::CORNER_ALL, 0.0f);

	CUIRect Box;
	Box.w = minimum(520.0f, Rect.w - 30.0f);
	Box.h = 130.0f;
	Box.x = Rect.x + (Rect.w - Box.w) * 0.5f;
	Box.y = Rect.y + (Rect.h - Box.h) * 0.5f;
	Box.Draw(ColorRGBA(0.1f, 0.1f, 0.1f, 0.95f), IGraphics::CORNER_ALL, 8.0f);

	CUIRect Title, Message, Buttons;
	Box.Margin(10.0f, &Box);
	Box.HSplitTop(LineSize + 4.0f, &Title, &Box);
	Box.HSplitTop(LineSize, &Message, &Box);
	Box.HSplitBottom(LineSize + 4.0f, &Box, &Buttons);

	Ui()->DoLabel(&Title, BCLocalize("Cancel all changes?"), FontSize * 1.1f, TEXTALIGN_ML);
	Ui()->DoLabel(&Message, BCLocalize("All staged component changes will be lost."), FontSize, TEXTALIGN_ML);

	CUIRect YesButton, NoButton;
	Buttons.VSplitMid(&YesButton, &NoButton, MarginSmall);
	static CButtonContainer s_YesButton;
	static CButtonContainer s_NoButton;
	if(DoButton_Menu(&s_YesButton, BCLocalize("Yes"), 0, &YesButton))
		ComponentsEditorCloseNow();
	if(DoButton_Menu(&s_NoButton, BCLocalize("No"), 0, &NoButton) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
		m_ComponentsEditorState.m_ShowExitConfirm = false;
}

void CMenus::ComponentsEditorRenderRestartConfirm(const CUIRect &Rect)
{
	const float FontSize = 14.0f;
	const float LineSize = 20.0f;
	const float MarginSmall = 5.0f;

	CUIRect Overlay = Rect;
	Overlay.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.6f), IGraphics::CORNER_ALL, 0.0f);

	CUIRect Box;
	Box.w = minimum(560.0f, Rect.w - 30.0f);
	Box.h = 140.0f;
	Box.x = Rect.x + (Rect.w - Box.w) * 0.5f;
	Box.y = Rect.y + (Rect.h - Box.h) * 0.5f;
	Box.Draw(ColorRGBA(0.1f, 0.1f, 0.1f, 0.95f), IGraphics::CORNER_ALL, 8.0f);

	CUIRect Title, Message, Buttons;
	Box.Margin(10.0f, &Box);
	Box.HSplitTop(LineSize + 4.0f, &Title, &Box);
	Box.HSplitTop(LineSize * 2.0f, &Message, &Box);
	Box.HSplitBottom(LineSize + 4.0f, &Box, &Buttons);

	Ui()->DoLabel(&Title, BCLocalize("Restart"), FontSize * 1.1f, TEXTALIGN_ML);
	Ui()->DoLabel(&Message, BCLocalize("Restart client now so component changes fully apply?"), FontSize, TEXTALIGN_ML);

	CUIRect YesButton, NoButton;
	Buttons.VSplitMid(&YesButton, &NoButton, MarginSmall);
	static CButtonContainer s_RestartYesButton;
	static CButtonContainer s_RestartNoButton;
	if(DoButton_Menu(&s_RestartYesButton, BCLocalize("Yes"), 0, &YesButton))
	{
		m_ComponentsEditorState.m_ShowRestartConfirm = false;
		m_ComponentsEditorState.m_Open = false;
		if(Client()->State() == IClient::STATE_ONLINE || GameClient()->Editor()->HasUnsavedData())
			m_Popup = POPUP_RESTART;
		else
			Client()->Restart();
	}
	if(DoButton_Menu(&s_RestartNoButton, BCLocalize("No"), 0, &NoButton) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
	{
		m_ComponentsEditorState.m_ShowRestartConfirm = false;
		m_ComponentsEditorState.m_Open = false;
	}
}

void CMenus::RenderComponentsEditorScreen(CUIRect MainView)
{
	const float FontSize = 14.0f;
	const float LineSize = 20.0f;
	const float HeadlineFontSize = 20.0f;
	const float MarginSmall = 5.0f;

	if(m_ComponentsEditorState.m_FullscreenOpen)
		MainView = *Ui()->Screen();

	if(!m_ComponentsEditorState.m_ShowExitConfirm && !m_ComponentsEditorState.m_ShowRestartConfirm && Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
	{
		ComponentsEditorRequestClose();
		if(!m_ComponentsEditorState.m_Open)
			return;
	}
	if(m_ComponentsEditorState.m_ShowExitConfirm)
	{
		ComponentsEditorRenderExitConfirm(*Ui()->Screen());
		return;
	}
	if(m_ComponentsEditorState.m_ShowRestartConfirm)
	{
		ComponentsEditorRenderRestartConfirm(*Ui()->Screen());
		return;
	}

	CUIRect EditorRect = MainView;
	EditorRect.Margin(8.0f, &EditorRect);
	EditorRect.Draw(ColorRGBA(0.10f, 0.11f, 0.15f, 0.96f), IGraphics::CORNER_ALL, 8.0f);

	CUIRect WorkRect;
	EditorRect.Margin(8.0f, &WorkRect);
	CUIRect Header, Content, Footer;
	WorkRect.HSplitTop(24.0f, &Header, &WorkRect);
	WorkRect.HSplitBottom(34.0f, &Content, &Footer);

	CUIRect HeaderText = Header;
	CUIRect CloseButtonArea, HeaderSpacer;
	HeaderText.VSplitLeft(18.0f, &CloseButtonArea, &HeaderText);
	HeaderText.VSplitLeft(6.0f, &HeaderSpacer, &HeaderText);
	(void)HeaderSpacer;

	CUIRect CloseButton;
	CloseButtonArea.HMargin(3.0f, &CloseButton);

	static CButtonContainer s_CloseButton;
	if(Ui()->DoButton_FontIcon(&s_CloseButton, FontIcon::XMARK, 0, &CloseButton, IGraphics::CORNER_ALL))
	{
		ComponentsEditorRequestClose();
		if(!m_ComponentsEditorState.m_Open)
			return;
	}

	Ui()->DoLabel(&HeaderText, BCLocalize("Components editor"), HeadlineFontSize, TEXTALIGN_ML);

	static CScrollRegion s_ScrollRegion;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	s_ScrollRegion.Begin(&Content, &ScrollOffset, &ScrollParams);

	CUIRect View = Content;
	View.y += ScrollOffset.y;
	View.VSplitLeft(5.0f, nullptr, &View);
	View.VSplitRight(5.0f, &View, nullptr);

	const char *apGroupNames[NUM_COMPONENTS_GROUPS] = {
		BCLocalize("Visuals"),
		BCLocalize("Gameplay"),
		BCLocalize("Others"),
		"TClient",
	};

	for(int Group = 0; Group < NUM_COMPONENTS_GROUPS; ++Group)
	{
		int Count = 0;
		for(const auto &Entry : gs_aBestClientComponentEntries)
		{
			if(Entry.m_Group == Group)
				++Count;
		}
		if(Count == 0)
			continue;

		CUIRect GroupBox;
		const float GroupHeight = 20.0f + HeadlineFontSize + MarginSmall + Count * LineSize;
		View.HSplitTop(GroupHeight, &GroupBox, &View);
		GroupBox.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.22f), IGraphics::CORNER_ALL, 8.0f);
		GroupBox.Margin(10.0f, &GroupBox);

		CUIRect Label;
		GroupBox.HSplitTop(HeadlineFontSize, &Label, &GroupBox);
		Ui()->DoLabel(&Label, apGroupNames[Group], HeadlineFontSize, TEXTALIGN_ML);
		GroupBox.HSplitTop(MarginSmall, nullptr, &GroupBox);

		for(const auto &Entry : gs_aBestClientComponentEntries)
		{
			if(Entry.m_Group != Group)
				continue;

			CUIRect Row;
			GroupBox.HSplitTop(LineSize, &Row, &GroupBox);
			int Disabled = ComponentsEditorIsDisabled((int)Entry.m_Component, m_ComponentsEditorState.m_StagedMaskLo, m_ComponentsEditorState.m_StagedMaskHi);
			if(DoButton_CheckBox(&Entry, BCLocalize(Entry.m_pName), Disabled, &Row))
			{
				Disabled ^= 1;
				ComponentsEditorSetDisabled((int)Entry.m_Component, m_ComponentsEditorState.m_StagedMaskLo, m_ComponentsEditorState.m_StagedMaskHi, Disabled != 0);
				m_ComponentsEditorState.m_HasUnsavedChanges =
					m_ComponentsEditorState.m_StagedMaskLo != m_ComponentsEditorState.m_AppliedMaskLo ||
					m_ComponentsEditorState.m_StagedMaskHi != m_ComponentsEditorState.m_AppliedMaskHi;
			}
		}

		View.HSplitTop(16.0f, nullptr, &View);
	}

	CUIRect ScrollRegion;
	ScrollRegion.x = Content.x;
	ScrollRegion.y = View.y;
	ScrollRegion.w = Content.w;
	ScrollRegion.h = 0.0f;
	s_ScrollRegion.AddRect(ScrollRegion);
	s_ScrollRegion.End();

	CUIRect Counter, ApplyButton;
	Footer.VSplitLeft(300.0f, &Counter, &Footer);
	Footer.VSplitRight(88.0f, &Footer, &ApplyButton);

	int DisabledCount = 0;
	for(const auto &Entry : gs_aBestClientComponentEntries)
	{
		if(ComponentsEditorIsDisabled((int)Entry.m_Component, m_ComponentsEditorState.m_StagedMaskLo, m_ComponentsEditorState.m_StagedMaskHi))
			++DisabledCount;
	}

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), BCLocalize("Disabled components: %d"), DisabledCount);
	Ui()->DoLabel(&Counter, aBuf, FontSize, TEXTALIGN_ML);

	static CButtonContainer s_ApplyButton;
	const int DisabledStyle = m_ComponentsEditorState.m_HasUnsavedChanges ? 0 : -1;
	if(DoButton_Menu(&s_ApplyButton, BCLocalize("Apply"), DisabledStyle, &ApplyButton) && m_ComponentsEditorState.m_HasUnsavedChanges)
		ComponentsEditorApply();
}

void CMenus::RenderSettingsBestClientInfo(CUIRect MainView)
{
	enum
	{
		BESTCLIENT_TAB_VISUALS = 0,
		BESTCLIENT_TAB_GAMEPLAY,
		BESTCLIENT_TAB_OTHERS,
		BESTCLIENT_TAB_RESHADE,
		BESTCLIENT_TAB_INFO,
		NUM_BESTCLIENT_TABS,
	};

	enum
	{
		INFO_SUBTAB_FUN = 0,
		INFO_SUBTAB_SHOP,
		INFO_SUBTAB_INFO,
		NUM_INFO_SUBTABS,
	};

	static int s_CurSubTab = INFO_SUBTAB_INFO;
	static CButtonContainer s_aSubTabButtons[NUM_INFO_SUBTABS] = {};

	const float LineSize = 20.0f;
	const float MarginSmall = 5.0f;
	const float MarginBetweenViews = 30.0f;
	const float HeadlineFontSize = 20.0f;
	const float HeadlineHeight = HeadlineFontSize;

	// Sub-tab bar
	CUIRect SubTabBar, SubTabButton;
	MainView.HSplitTop(24.0f, &SubTabBar, &MainView);
	const char *apSubTabNames[NUM_INFO_SUBTABS] = {
		BCLocalize("Fun"),
		BCLocalize("Shop"),
		BCLocalize("Info"),
	};
	const float SubTabWidth = SubTabBar.w / (float)NUM_INFO_SUBTABS;
	for(int i = 0; i < NUM_INFO_SUBTABS; i++)
	{
		SubTabBar.VSplitLeft(SubTabWidth, &SubTabButton, &SubTabBar);
		const int Corners = i == 0 ? IGraphics::CORNER_L : (i == NUM_INFO_SUBTABS - 1 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
		if(DoButton_MenuTab(&s_aSubTabButtons[i], apSubTabNames[i], s_CurSubTab == i, &SubTabButton, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
			s_CurSubTab = i;
	}
	MainView.HSplitTop(10.0f, nullptr, &MainView);

	SetBestClientShopVisible(s_CurSubTab == INFO_SUBTAB_SHOP);

	if(s_CurSubTab == INFO_SUBTAB_FUN)
	{
		RenderSettingsBestClientFun(MainView);
		return;
	}
	if(s_CurSubTab == INFO_SUBTAB_SHOP)
	{
		RenderSettingsBestClientShop(MainView);
		return;
	}

	CUIRect LeftView, RightView, Button, Label, LowerLeftView;
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);
	LeftView.HSplitMid(&LeftView, &LowerLeftView, 0.0f);

	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, BCLocalize("BestClient Links"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	static CButtonContainer s_DiscordButton, s_WebsiteButton, s_TelegramButton, s_CheckUpdateButton;
	CUIRect ButtonLeft, ButtonRight;

	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&ButtonLeft, &ButtonRight, MarginSmall);
	if(DoButtonLineSize_Menu(&s_DiscordButton, BCLocalize("Discord"), 0, &ButtonLeft, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://discord.gg/bestclient");
	if(DoButtonLineSize_Menu(&s_TelegramButton, BCLocalize("Telegram"), 0, &ButtonRight, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://t.me/bestddnet");

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&ButtonLeft, &ButtonRight, MarginSmall);

	if(DoButtonLineSize_Menu(&s_WebsiteButton, BCLocalize("Website"), 0, &ButtonLeft, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://bestclient.fun");
	if(DoButtonLineSize_Menu(&s_CheckUpdateButton, BCLocalize("Check update"), 0, &ButtonRight, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		GameClient()->m_BestClient.FetchBestClientInfo();

#if defined(CONF_AUTOUPDATE)
	const bool NeedUpdate = GameClient()->m_BestClient.NeedUpdate();
	const IUpdater::EUpdaterState UpdateState = Updater()->GetCurrentState();
	const bool ShowDownloadButton = NeedUpdate && UpdateState == IUpdater::CLEAN;
	const bool ShowRetryButton = NeedUpdate && UpdateState == IUpdater::FAIL;
	const bool ShowRestartButton = UpdateState == IUpdater::NEED_RESTART;
	const bool ShowUpdateProgress = UpdateState >= IUpdater::GETTING_MANIFEST && UpdateState < IUpdater::NEED_RESTART;
	if(ShowDownloadButton || ShowRetryButton || ShowRestartButton || ShowUpdateProgress || UpdateState == IUpdater::FAIL)
	{
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
		LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
		Button.VSplitMid(&ButtonLeft, &ButtonRight, MarginSmall);

		char aUpdateLabel[128] = "";
		if(ShowDownloadButton)
			str_format(aUpdateLabel, sizeof(aUpdateLabel), BCLocalize("BestClient %s is available"), GameClient()->m_BestClient.m_aVersionStr);
		else if(ShowUpdateProgress)
		{
			if(UpdateState == IUpdater::GETTING_MANIFEST)
				str_copy(aUpdateLabel, BCLocalize("Preparing update..."), sizeof(aUpdateLabel));
			else
				str_format(aUpdateLabel, sizeof(aUpdateLabel), "%s %d%%", BCLocalize("Downloading"), Updater()->GetCurrentPercent());
		}
		else if(ShowRestartButton)
			str_copy(aUpdateLabel, BCLocalize("Update downloaded"), sizeof(aUpdateLabel));
		else
			str_copy(aUpdateLabel, BCLocalize("Update failed"), sizeof(aUpdateLabel));

		if(ShowDownloadButton)
			TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
		Ui()->DoLabel(&ButtonLeft, aUpdateLabel, HeadlineFontSize / 1.6f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		if(ShowDownloadButton || ShowRetryButton)
		{
			static CButtonContainer s_DownloadUpdateButton;
			if(DoButtonLineSize_Menu(&s_DownloadUpdateButton, BCLocalize("Download"), 0, &ButtonRight, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
				Updater()->InitiateUpdate();
		}
		else if(ShowRestartButton)
		{
			static CButtonContainer s_RestartUpdateButton;
			if(DoButtonLineSize_Menu(&s_RestartUpdateButton, BCLocalize("Restart"), 0, &ButtonRight, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
				Updater()->ApplyUpdateAndRestart();
		}
		else if(ShowUpdateProgress)
		{
			Ui()->RenderProgressBar(ButtonRight, Updater()->GetCurrentPercent() / 100.0f);
		}
	}
#endif

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, BCLocalize("Editors"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	{
		const float LSize = 20.0f;
		CUIRect EditorLabel, EditorButton;
		LeftView.HSplitTop(LSize, &EditorLabel, &LeftView);
		Ui()->DoLabel(&EditorLabel, BCLocalize("Create mixed assets or jump to the name plate editor."), 14.0f, TEXTALIGN_ML);
		LeftView.HSplitTop(5.0f, nullptr, &LeftView);
		static CButtonContainer s_AssetsEditorButton2;
		LeftView.HSplitTop(LSize + 4.0f, &EditorButton, &LeftView);
		if(DoButton_Menu(&s_AssetsEditorButton2, BCLocalize("Assets editor"), 0, &EditorButton))
		{
			m_AssetsEditorState.m_VisualsEditorOpen = true;
			m_AssetsEditorState.m_FullscreenOpen = true;
			if(!m_AssetsEditorState.m_VisualsEditorInitialized)
			{
				AssetsEditorReloadAssets();
				AssetsEditorResetPartSlots();
				AssetsEditorEnsureDefaultExportNames();
				AssetsEditorSyncExportNameFromType();
				m_AssetsEditorState.m_VisualsEditorInitialized = true;
			}
		}
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
		LeftView.HSplitTop(LSize, &EditorLabel, &LeftView);
		Ui()->DoLabel(&EditorLabel, BCLocalize("Open a dedicated component toggles page."), 14.0f, TEXTALIGN_ML);
		LeftView.HSplitTop(5.0f, nullptr, &LeftView);
		static CButtonContainer s_ComponentsEditorButton2;
		LeftView.HSplitTop(LSize + 4.0f, &EditorButton, &LeftView);
		if(DoButton_Menu(&s_ComponentsEditorButton2, BCLocalize("Components editor"), 0, &EditorButton))
			ComponentsEditorOpen();
	}

	LeftView = LowerLeftView;
	LeftView.HSplitBottom(LineSize * 2.0f + MarginSmall * 2.0f + HeadlineFontSize, nullptr, &LeftView);
	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, BCLocalize("Config Files"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	char aBuf[128 + IO_MAX_PATH_LENGTH];
	CUIRect BestClientConfig;
	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	BestClientConfig = Button;

	static CButtonContainer s_Config;
	if(DoButtonLineSize_Menu(&s_Config, BCLocalize("BestClient Settings"), 0, &BestClientConfig, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::BESTCLIENT].m_aConfigPath, aBuf, sizeof(aBuf));
		Client()->ViewFile(aBuf);
	}

	RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	Ui()->DoLabel(&Label, BCLocalize("BestClient Developers"), HeadlineFontSize, TEXTALIGN_ML);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);

	const float TeeSize = 64.0f;
	const float DevNameFontSize = 24.0f;
	const float CardSize = TeeSize + MarginSmall * 2.0f;
	CUIRect TeeRect, DevCardRect;
	static CButtonContainer s_LinkButton1, s_LinkButton2, s_LinkButton3;
	{
		RightView.HSplitTop(CardSize, &DevCardRect, &RightView);
		DevCardRect.VSplitLeft(CardSize, &TeeRect, &Label);
		Label.VSplitLeft(TextRender()->TextWidth(DevNameFontSize, "RoflikBEST"), &Label, &Button);
		Button.VSplitLeft(MarginSmall, nullptr, &Button);
		Button.w = DevNameFontSize;
		Button.h = DevNameFontSize;
		Button.y = Label.y + (Label.h / 2.0f - Button.h / 2.0f);
		Ui()->DoLabel(&Label, "RoflikBEST", DevNameFontSize, TEXTALIGN_ML);
		if(Ui()->DoButton_FontIcon(&s_LinkButton1, FontIcon::ARROW_UP_RIGHT_FROM_SQUARE, 0, &Button, IGraphics::CORNER_ALL))
			Client()->ViewLink("https://github.com/roflikbest");
		RenderDevSkin(TeeRect.Center(), TeeSize, "10Nanami_glow", "nanami", true, 0, 0, 0, false, true, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), ColorRGBA(0.94f, 0.74f, 0.92f, 1.0f));
	}
	{
		RightView.HSplitTop(CardSize, &DevCardRect, &RightView);
		DevCardRect.VSplitLeft(CardSize, &TeeRect, &Label);
		Label.VSplitLeft(TextRender()->TextWidth(DevNameFontSize, "noxygalaxy"), &Label, &Button);
		Button.VSplitLeft(MarginSmall, nullptr, &Button);
		Button.w = DevNameFontSize;
		Button.h = DevNameFontSize;
		Button.y = Label.y + (Label.h / 2.0f - Button.h / 2.0f);
		Ui()->DoLabel(&Label, "noxygalaxy", DevNameFontSize, TEXTALIGN_ML);
		if(Ui()->DoButton_FontIcon(&s_LinkButton3, FontIcon::ARROW_UP_RIGHT_FROM_SQUARE, 0, &Button, IGraphics::CORNER_ALL))
			Client()->ViewLink("https://github.com/noxygalaxy");
		RenderDevSkin(TeeRect.Center(), TeeSize, "Niko_OneShot", "Niko_OneShot", false, 0, 0, 0, false, true);
	}
	{
		RightView.HSplitTop(CardSize, &DevCardRect, &RightView);
		DevCardRect.VSplitLeft(CardSize, &TeeRect, &Label);
		Label.VSplitLeft(TextRender()->TextWidth(DevNameFontSize, "sqwinix"), &Label, &Button);
		Button.VSplitLeft(MarginSmall, nullptr, &Button);
		Button.w = DevNameFontSize;
		Button.h = DevNameFontSize;
		Button.y = Label.y + (Label.h / 2.0f - Button.h / 2.0f);
		Ui()->DoLabel(&Label, "sqwinix", DevNameFontSize, TEXTALIGN_ML);
		if(Ui()->DoButton_FontIcon(&s_LinkButton2, FontIcon::ARROW_UP_RIGHT_FROM_SQUARE, 0, &Button, IGraphics::CORNER_ALL))
			Client()->ViewLink("https://github.com/sqwinixxx");
		RenderDevSkin(TeeRect.Center(), TeeSize, "sticker_nanami", "sticker_nanami", true, 0, 0, 0, false, true, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	}

	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	Ui()->DoLabel(&Label, BCLocalize("Hide Settings Tabs"), HeadlineFontSize, TEXTALIGN_ML);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	CUIRect LeftSettings, RightSettings;
	RightView.VSplitMid(&LeftSettings, &RightSettings, MarginSmall);

	const char *apTabNames[] = {
		BCLocalize("Visuals"),
		BCLocalize("Gameplay"),
		BCLocalize("Others"),
		BCLocalize("Live-Shaders"),
		BCLocalize("Info"),
	};
	const int aTabOrder[NUM_BESTCLIENT_TABS] = {
		BESTCLIENT_TAB_VISUALS,
		BESTCLIENT_TAB_GAMEPLAY,
		BESTCLIENT_TAB_OTHERS,
		BESTCLIENT_TAB_RESHADE,
		BESTCLIENT_TAB_INFO,
	};

	static CButtonContainer s_aShowTabButtons[NUM_BESTCLIENT_TABS] = {};
	int HideableTabCount = 0;
	int HideableVisibleIndex = 0;
	for(const int Tab : aTabOrder)
	{
		// Keep Info visible the same way as in legacy BestClient.
		if(Tab == BESTCLIENT_TAB_INFO)
			continue;

		++HideableTabCount;
		int Hidden = IsBestClientTabFlagSet(g_Config.m_BcBestClientSettingsTabs, Tab);
		CUIRect *pColumn = HideableVisibleIndex % 2 == 0 ? &LeftSettings : &RightSettings;
		DoButton_CheckBoxAutoVMarginAndSet(&s_aShowTabButtons[Tab], apTabNames[Tab], &Hidden, pColumn, LineSize);
		SetBestClientTabFlag(g_Config.m_BcBestClientSettingsTabs, Tab, Hidden);
		++HideableVisibleIndex;
	}
	const int HideableRows = (HideableTabCount + 1) / 2;
	RightView.HSplitTop(LineSize * (HideableRows + 0.5f), nullptr, &RightView);
}
