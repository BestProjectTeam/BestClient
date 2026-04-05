#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_VISUALIZER_SOURCE_PRIORITY_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_VISUALIZER_SOURCE_PRIORITY_H

#include <algorithm>
#include <cctype>
#include <string_view>

namespace BestClientVisualizer
{

enum class EMediaSourcePriority
{
	DISCORD = -1000,
	GENERIC = 100,
	BROWSER = 200,
	DEDICATED = 300,
};

inline bool MediaSourceContainsI(std::string_view Text, std::string_view Needle)
{
	if(Text.empty() || Needle.empty() || Needle.size() > Text.size())
		return false;

	return std::search(Text.begin(), Text.end(), Needle.begin(), Needle.end(),
		[](char Left, char Right) {
			return std::tolower((unsigned char)Left) == std::tolower((unsigned char)Right);
		}) != Text.end();
}

inline bool LooksLikeDiscordPlayer(std::string_view Primary, std::string_view Secondary = {}, std::string_view Tertiary = {})
{
	return MediaSourceContainsI(Primary, "discord") ||
		MediaSourceContainsI(Secondary, "discord") ||
		MediaSourceContainsI(Tertiary, "discord");
}

inline bool LooksLikeDedicatedPlayer(std::string_view Primary, std::string_view Secondary = {}, std::string_view Tertiary = {})
{
	return MediaSourceContainsI(Primary, "spotify") || MediaSourceContainsI(Secondary, "spotify") || MediaSourceContainsI(Tertiary, "spotify") ||
		MediaSourceContainsI(Primary, "vlc") || MediaSourceContainsI(Secondary, "vlc") || MediaSourceContainsI(Tertiary, "vlc") ||
		MediaSourceContainsI(Primary, "mpv") || MediaSourceContainsI(Secondary, "mpv") || MediaSourceContainsI(Tertiary, "mpv");
}

inline bool LooksLikeBrowserPlayer(std::string_view Primary, std::string_view Secondary = {}, std::string_view Tertiary = {})
{
	return MediaSourceContainsI(Primary, "chromium") || MediaSourceContainsI(Secondary, "chromium") || MediaSourceContainsI(Tertiary, "chromium") ||
		MediaSourceContainsI(Primary, "chrome") || MediaSourceContainsI(Secondary, "chrome") || MediaSourceContainsI(Tertiary, "chrome") ||
		MediaSourceContainsI(Primary, "firefox") || MediaSourceContainsI(Secondary, "firefox") || MediaSourceContainsI(Tertiary, "firefox");
}

inline int PlayerSourcePriority(std::string_view Primary, std::string_view Secondary = {}, std::string_view Tertiary = {})
{
	if(LooksLikeDiscordPlayer(Primary, Secondary, Tertiary))
		return (int)EMediaSourcePriority::DISCORD;
	if(LooksLikeDedicatedPlayer(Primary, Secondary, Tertiary))
		return (int)EMediaSourcePriority::DEDICATED;
	if(LooksLikeBrowserPlayer(Primary, Secondary, Tertiary))
		return (int)EMediaSourcePriority::BROWSER;
	return (int)EMediaSourcePriority::GENERIC;
}

inline bool HintMatchesPlaybackSource(std::string_view HintServiceId, std::string_view AppName, std::string_view AppBinary)
{
	if(HintServiceId.empty() || LooksLikeDiscordPlayer(HintServiceId))
		return false;

	const bool HintSpotify = MediaSourceContainsI(HintServiceId, "spotify");
	const bool HintVlc = MediaSourceContainsI(HintServiceId, "vlc");
	const bool HintMpv = MediaSourceContainsI(HintServiceId, "mpv");
	const bool HintChromium = MediaSourceContainsI(HintServiceId, "chromium") || MediaSourceContainsI(HintServiceId, "chrome");
	const bool HintFirefox = MediaSourceContainsI(HintServiceId, "firefox");

	return (HintSpotify && (MediaSourceContainsI(AppName, "spotify") || MediaSourceContainsI(AppBinary, "spotify"))) ||
		(HintVlc && (MediaSourceContainsI(AppName, "vlc") || MediaSourceContainsI(AppBinary, "vlc"))) ||
		(HintMpv && (MediaSourceContainsI(AppName, "mpv") || MediaSourceContainsI(AppBinary, "mpv"))) ||
		(HintChromium && (MediaSourceContainsI(AppName, "chrom") || MediaSourceContainsI(AppBinary, "chrom"))) ||
		(HintFirefox && (MediaSourceContainsI(AppName, "firefox") || MediaSourceContainsI(AppBinary, "firefox")));
}

} // namespace BestClientVisualizer

#endif
