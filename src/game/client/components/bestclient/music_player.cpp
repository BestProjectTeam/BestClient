#include "music_player.h"

#include <base/color.h>
#include <base/math.h>
#include <base/system.h>
#include <base/time.h>

#include <engine/engine.h>
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/jobs.h>
#include <engine/shared/http.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/components/chat.h>
#include <game/client/components/hud_layout.h>
#include <game/client/components/media_decoder.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <numeric>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include <mutex>
#include <condition_variable>

#if !defined(BC_MUSICPLAYER_HAS_DBUS)
#define BC_MUSICPLAYER_HAS_DBUS 0
#endif

#if !defined(BC_MUSICPLAYER_HAS_PULSE)
#define BC_MUSICPLAYER_HAS_PULSE 0
#endif

#if defined(CONF_PLATFORM_LINUX) && BC_MUSICPLAYER_HAS_DBUS
#include <dbus/dbus.h>
#endif

#if defined(CONF_PLATFORM_LINUX) && BC_MUSICPLAYER_HAS_PULSE
#include <pulse/error.h>
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>
#endif

#if defined(CONF_FAMILY_WINDOWS) && __has_include(<winrt/Windows.Media.Control.h>)
#define BC_MUSICPLAYER_HAS_WINRT 1
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>
#include <audioclient.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#else
#define BC_MUSICPLAYER_HAS_WINRT 0
#endif

namespace
{
static constexpr int MUSIC_ART_MAX_DIMENSION = 512;
static constexpr int MUSIC_ART_MAX_FRAMES = 60;
static constexpr int64_t MUSIC_ART_TEXTURE_UPLOAD_BUDGET_US = 1500;
static constexpr int MUSIC_ART_MAX_TEXTURE_UPLOADS_PER_FRAME = 1;

static CUIRect HudToUiRect(const CUIRect &HudRect, const CUIRect &UiScreen, float HudWidth, float HudHeight)
{
	CUIRect UiRect;
	UiRect.x = UiScreen.x + HudRect.x * UiScreen.w / HudWidth;
	UiRect.y = UiScreen.y + HudRect.y * UiScreen.h / HudHeight;
	UiRect.w = HudRect.w * UiScreen.w / HudWidth;
	UiRect.h = HudRect.h * UiScreen.h / HudHeight;
	return UiRect;
}

static bool MusicPlayerDebugEnabled(int Level)
{
	return g_Config.m_DbgMusicPlayer >= Level;
}

static float MusicPlayerVisualizerSensitivity()
{
	const float Raw = std::clamp(g_Config.m_BcMusicPlayerVisualizerSensitivity / 100.0f, 0.5f, 20.0f);
	return powf(Raw, 1.35f);
}

static void MusicPlayerDebugLog(int Level, const char *pSubsystem, const char *pFmt, ...)
{
	if(!MusicPlayerDebugEnabled(Level))
		return;

	char aMsg[1024];
	va_list Args;
	va_start(Args, pFmt);
	vsnprintf(aMsg, sizeof(aMsg), pFmt, Args);
	va_end(Args);
	dbg_msg("music_player", "[%s] %s", pSubsystem, aMsg);
}

enum class EMusicPlaybackState
{
	STOPPED,
	PAUSED,
	PLAYING,
};

enum class EMusicVisualizerBackendStatus
{
	UNAVAILABLE,
	CONNECTING,
	SILENT,
	LIVE,
	FALLBACK,
};

static const char *MusicPlaybackStateName(EMusicPlaybackState State)
{
	switch(State)
	{
	case EMusicPlaybackState::STOPPED: return "stopped";
	case EMusicPlaybackState::PAUSED: return "paused";
	case EMusicPlaybackState::PLAYING: return "playing";
	}
	return "unknown";
}

static const char *MusicVisualizerBackendStatusName(EMusicVisualizerBackendStatus Status)
{
	switch(Status)
	{
	case EMusicVisualizerBackendStatus::UNAVAILABLE: return "unavailable";
	case EMusicVisualizerBackendStatus::CONNECTING: return "connecting";
	case EMusicVisualizerBackendStatus::SILENT: return "silent";
	case EMusicVisualizerBackendStatus::LIVE: return "live";
	case EMusicVisualizerBackendStatus::FALLBACK: return "fallback";
	}
	return "unknown";
}

static constexpr int MUSIC_PLAYER_VISUALIZER_BINS = 16;

struct SMusicArt
{
	enum class EType
	{
		NONE,
		URL,
		BYTES,
	};

	EType m_Type = EType::NONE;
	std::string m_Key;
	std::string m_Url;
	std::vector<unsigned char> m_vBytes;
};

struct SMusicVisualizerData
{
	bool m_HasRealtimeSignal = false;
	bool m_IsPassiveFallback = true;
	EMusicVisualizerBackendStatus m_BackendStatus = EMusicVisualizerBackendStatus::FALLBACK;
	float m_Peak = 0.0f;
	float m_Rms = 0.0f;
	std::array<float, MUSIC_PLAYER_VISUALIZER_BINS> m_aBins{};
};

static std::string MusicVisualizerBinsSummary(const SMusicVisualizerData &Visualizer)
{
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "%.2f,%.2f,%.2f,%.2f",
		Visualizer.m_aBins[0],
		Visualizer.m_aBins[1],
		Visualizer.m_aBins[2],
		Visualizer.m_aBins[3]);
	return aBuf;
}

struct SNowPlayingSnapshot
{
	bool m_Valid = false;
	std::string m_ServiceId;
	std::string m_Title;
	std::string m_Artist;
	std::string m_Album;
	int64_t m_DurationMs = 0;
	int64_t m_PositionMs = 0;
	EMusicPlaybackState m_PlaybackState = EMusicPlaybackState::STOPPED;
	bool m_CanPrev = false;
	bool m_CanPlayPause = false;
	bool m_CanNext = false;
	bool m_HasVisualizer = false;
	SMusicArt m_Art;
	SMusicVisualizerData m_Visualizer;
};

struct SMusicPlaybackHint
{
	std::string m_ServiceId;
	std::string m_Title;
	std::string m_Artist;
	EMusicPlaybackState m_PlaybackState = EMusicPlaybackState::STOPPED;
};

static bool ShouldForceOrderedNavigation(const SNowPlayingSnapshot &Snapshot)
{
	if(!Snapshot.m_Valid)
		return false;
	const bool HasAlbumContext = !Snapshot.m_Album.empty();
	const bool HasQueueContext = Snapshot.m_CanPrev && Snapshot.m_CanNext && Snapshot.m_DurationMs > 0;
	return HasAlbumContext || HasQueueContext;
}

static uint32_t HashBytes(std::string_view Value)
{
	uint32_t Hash = 2166136261u;
	for(const unsigned char Byte : Value)
	{
		Hash ^= Byte;
		Hash *= 16777619u;
	}
	return Hash;
}

static uint32_t TrackAnimationSeed(const SNowPlayingSnapshot &Snapshot)
{
	uint32_t Hash = HashBytes(Snapshot.m_ServiceId);
	Hash ^= HashBytes(Snapshot.m_Title) + 0x9e3779b9u + (Hash << 6) + (Hash >> 2);
	Hash ^= HashBytes(Snapshot.m_Artist) + 0x9e3779b9u + (Hash << 6) + (Hash >> 2);
	return Hash;
}

static float VisualizerBarPhase(const SNowPlayingSnapshot &Snapshot, int BarIndex)
{
	uint32_t Seed = TrackAnimationSeed(Snapshot) ^ ((uint32_t)(BarIndex + 1) * 0x9e3779b9u);
	Seed ^= Seed >> 16;
	return (Seed & 0xffffu) / 65535.0f * 2.0f * pi;
}

static float VisualizerBarTargetLevel(const SNowPlayingSnapshot &Snapshot, float TimeSeconds, float TrackProgress, int BarIndex, int NumBars)
{
	const float BarT = BarIndex / maximum(1.0f, (float)(NumBars - 1));
	const float Centered = BarT * 2.0f - 1.0f;
	const float Distance = absolute(Centered);
	const float Arch = powf(maximum(0.0f, 1.0f - Distance), 0.58f);
	const float Shoulder = powf(maximum(0.0f, 1.0f - Distance * Distance), 1.15f);
	const uint32_t Seed = TrackAnimationSeed(Snapshot);
	const float SeedPhase = (Seed & 0xffffu) / 65535.0f * 2.0f * pi;
	const float DriftPhase = ((Seed >> 16) & 0xffffu) / 65535.0f * 2.0f * pi;
	const float BarPhase = VisualizerBarPhase(Snapshot, BarIndex);

	if(Snapshot.m_PlaybackState != EMusicPlaybackState::PLAYING)
	{
		const float Breathe = 0.5f + 0.5f * sinf(TimeSeconds * 0.68f * 2.0f * pi + SeedPhase * 0.45f + BarPhase * 0.65f);
		const float Ripple = 0.5f + 0.5f * sinf(TimeSeconds * (0.30f + 0.035f * BarIndex) * 2.0f * pi - Distance * 2.2f + DriftPhase + BarPhase);
		const float Calm = 0.34f + Arch * 0.18f + Shoulder * 0.06f + Breathe * (0.12f + Arch * 0.10f) + Ripple * (0.10f + Shoulder * 0.08f);
		return std::clamp(Calm, 0.38f, 0.88f);
	}

	const float Pulse = 0.5f + 0.5f * sinf(TimeSeconds * (1.08f + 0.05f * BarIndex) * 2.0f * pi + TrackProgress * 6.0f * pi + SeedPhase + BarPhase * 0.5f);
	const float Sweep = 0.5f + 0.5f * sinf(TimeSeconds * 1.78f * 2.0f * pi + Centered * 3.0f + DriftPhase + TrackProgress * 4.2f * pi + BarPhase * 0.7f);
	const float Crest = 0.5f + 0.5f * sinf(TimeSeconds * (2.45f + 0.09f * BarIndex) * 2.0f * pi - Distance * 3.1f + SeedPhase * 1.15f + BarPhase);
	const float Texture = 0.5f + 0.5f * sinf(TimeSeconds * 3.40f * 2.0f * pi + Centered * 5.0f - DriftPhase * 0.55f - BarPhase * 0.8f);
	const float Bounce = 0.5f + 0.5f * sinf(TimeSeconds * (1.36f + 0.12f * BarIndex) * 2.0f * pi + BarPhase * 1.7f - Centered * 1.8f);
	const float Motion = Pulse * (0.28f + 0.26f * Arch) + Sweep * 0.20f + Crest * 0.18f * Shoulder + Texture * 0.10f + Bounce * 0.24f;
	const float Level = 0.16f + Arch * 0.22f + Shoulder * 0.08f + Motion * (0.42f + 0.28f * Arch);
	return std::clamp(Level, 0.16f, 1.0f);
}

class IMusicPlaybackProvider
{
public:
	virtual ~IMusicPlaybackProvider() = default;

	virtual bool Poll(SNowPlayingSnapshot &Out) = 0;
	virtual void Previous() = 0;
	virtual void PlayPause() = 0;
	virtual void Next() = 0;
};

class IMusicVisualizerSource
{
public:
	virtual ~IMusicVisualizerSource() = default;

	virtual bool Poll(SMusicVisualizerData &Out) = 0;
	virtual void SetPlaybackHint(const SMusicPlaybackHint &Hint) { (void)Hint; }
};

static float VisualizerSampleWindow(size_t Index, size_t Count)
{
	if(Count <= 1)
		return 1.0f;
	const float Phase = (2.0f * pi * Index) / (float)(Count - 1);
	return 0.5f * (1.0f - cosf(Phase));
}

static float VisualizerMagnitudeToUnit(float Magnitude)
{
	const float Db = 20.0f * log10f(maximum(Magnitude, 1.0e-5f));
	return std::clamp((Db + 58.0f) / 58.0f, 0.0f, 1.0f);
}

static SMusicVisualizerData AnalyzeVisualizerSamples(const std::vector<float> &vSamples, int SampleRate)
{
	SMusicVisualizerData Result;
	Result.m_IsPassiveFallback = false;
	Result.m_BackendStatus = EMusicVisualizerBackendStatus::LIVE;
	if(vSamples.empty() || SampleRate <= 0)
		return Result;

	const size_t WindowSize = minimum<size_t>(2048, vSamples.size());
	const size_t Start = vSamples.size() - WindowSize;
	float Peak = 0.0f;
	double RmsAccum = 0.0;
	std::vector<float> vWindowed(WindowSize);
	for(size_t i = 0; i < WindowSize; ++i)
	{
		const float Sample = std::clamp(vSamples[Start + i], -1.0f, 1.0f);
		Peak = maximum(Peak, absolute(Sample));
		RmsAccum += (double)Sample * (double)Sample;
		vWindowed[i] = Sample * VisualizerSampleWindow(i, WindowSize);
	}
	Result.m_Peak = Peak;
	Result.m_Rms = sqrtf((float)(RmsAccum / WindowSize));

	const size_t BinCount = Result.m_aBins.size();
	const size_t Nyquist = WindowSize / 2;
	if(Nyquist == 0)
		return Result;
	for(size_t Bin = 0; Bin < BinCount; ++Bin)
	{
		const float Norm0 = Bin / (float)BinCount;
		const float Norm1 = (Bin + 1) / (float)BinCount;
		const size_t Freq0 = maximum<size_t>(1, (size_t)powf((float)Nyquist, Norm0));
		const size_t Freq1 = minimum<size_t>(Nyquist, maximum<size_t>(Freq0 + 1, (size_t)powf((float)Nyquist, Norm1)));
		float SumMagnitude = 0.0f;
		int Count = 0;
		for(size_t K = Freq0; K < Freq1; ++K)
		{
			float Re = 0.0f;
			float Im = 0.0f;
			for(size_t N = 0; N < WindowSize; ++N)
			{
				const float Angle = 2.0f * pi * (float)K * (float)N / (float)WindowSize;
				Re += vWindowed[N] * cosf(Angle);
				Im -= vWindowed[N] * sinf(Angle);
			}
			const float Magnitude = sqrtf(Re * Re + Im * Im) / (float)WindowSize;
			SumMagnitude += Magnitude;
			Count++;
		}
		Result.m_aBins[Bin] = Count > 0 ? VisualizerMagnitudeToUnit(SumMagnitude / Count) : 0.0f;
	}
	const float Sensitivity = MusicPlayerVisualizerSensitivity();
	const bool HasAudibleSignal = Result.m_Rms >= 0.0055f / Sensitivity || Result.m_Peak >= 0.014f / Sensitivity;
	Result.m_HasRealtimeSignal = HasAudibleSignal;
	return Result;
}

static bool IsUrlScheme(const std::string &Url, const char *pScheme)
{
	const int SchemeLength = str_length(pScheme);
	return Url.size() >= static_cast<size_t>(SchemeLength) && str_comp_num(Url.c_str(), pScheme, SchemeLength) == 0;
}

static std::string UrlDecode(std::string_view Encoded)
{
	std::string Decoded;
	Decoded.reserve(Encoded.size());
	for(size_t i = 0; i < Encoded.size(); ++i)
	{
		const char c = Encoded[i];
		if(c == '%' && i + 2 < Encoded.size())
		{
			auto HexToInt = [](char Hex) -> int {
				if(Hex >= '0' && Hex <= '9')
					return Hex - '0';
				if(Hex >= 'a' && Hex <= 'f')
					return 10 + (Hex - 'a');
				if(Hex >= 'A' && Hex <= 'F')
					return 10 + (Hex - 'A');
				return -1;
			};
			const int High = HexToInt(Encoded[i + 1]);
			const int Low = HexToInt(Encoded[i + 2]);
			if(High >= 0 && Low >= 0)
			{
				Decoded.push_back((char)((High << 4) | Low));
				i += 2;
				continue;
			}
		}
		else if(c == '+')
		{
			Decoded.push_back(' ');
			continue;
		}
		Decoded.push_back(c);
	}
	return Decoded;
}

static std::string FileUrlToPath(const std::string &Url)
{
	if(!IsUrlScheme(Url, "file://"))
		return Url;

	size_t PathOffset = 7;
	if(Url.compare(PathOffset, 9, "localhost") == 0)
		PathOffset += 9;
	std::string Path = Url.substr(PathOffset);
	if(!Path.empty() && Path[0] != '/')
		Path.insert(Path.begin(), '/');
	return UrlDecode(Path);
}

static float EaseOutCubic(float t)
{
	t = std::clamp(t, 0.0f, 1.0f);
	const float Inv = 1.0f - t;
	return 1.0f - Inv * Inv * Inv;
}

static std::string BuildSnapshotTrackKey(const SNowPlayingSnapshot &Snapshot)
{
	return Snapshot.m_ServiceId + "|" + Snapshot.m_Title + "|" + Snapshot.m_Artist + "|" + std::to_string(Snapshot.m_DurationMs);
}

struct SGameTimerDisplay
{
	bool m_Valid = false;
	bool m_Warning = false;
	bool m_Blink = false;
	std::string m_Text;
};

static SGameTimerDisplay BuildGameTimerDisplay(const CNetObj_GameInfo *pGameInfo, int GameTick, int GameTickSpeed, bool ForcePreview)
{
	SGameTimerDisplay Result;
	const bool HasGameInfo = pGameInfo != nullptr;
	if(!HasGameInfo && !ForcePreview)
		return Result;

	if(!ForcePreview && (pGameInfo->m_GameStateFlags & GAMESTATEFLAG_SUDDENDEATH))
	{
		Result.m_Valid = true;
		Result.m_Text = Localize("Sudden Death");
		return Result;
	}

	int Time = 0;
	if(HasGameInfo && pGameInfo->m_TimeLimit && pGameInfo->m_WarmupTimer <= 0)
	{
		Time = pGameInfo->m_TimeLimit * 60 - ((GameTick - pGameInfo->m_RoundStartTick) / GameTickSpeed);
		if(pGameInfo->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER)
			Time = 0;
		Result.m_Warning = Time <= 60;
	}
	else if(HasGameInfo && (pGameInfo->m_GameStateFlags & GAMESTATEFLAG_RACETIME))
	{
		Time = (GameTick + pGameInfo->m_WarmupTimer) / GameTickSpeed;
	}
	else if(HasGameInfo)
	{
		Time = (GameTick - pGameInfo->m_RoundStartTick) / GameTickSpeed;
	}
	else
	{
		Time = 12 * 60 + 34;
	}

	char aBuf[32];
	str_time((int64_t)Time * 100, ETimeFormat::DAYS, aBuf, sizeof(aBuf));
	Result.m_Valid = true;
	Result.m_Text = aBuf;
	Result.m_Blink = Result.m_Warning && Time <= 10 && (2 * time_get() / time_freq()) % 2;
	return Result;
}

#if defined(CONF_PLATFORM_LINUX) && BC_MUSICPLAYER_HAS_DBUS
class CLinuxNowPlayingProvider final : public IMusicPlaybackProvider
{
	DBusConnection *m_pConnection = nullptr;
	std::string m_CurrentService;
	SNowPlayingSnapshot m_LastSnapshot;
	bool m_ShuffleForcedForCurrentService = false;

	bool EnsureConnection()
	{
		if(m_pConnection != nullptr)
			return true;

		DBusError Error;
		dbus_error_init(&Error);
		m_pConnection = dbus_bus_get(DBUS_BUS_SESSION, &Error);
		if(dbus_error_is_set(&Error))
		{
			dbus_error_free(&Error);
			m_pConnection = nullptr;
		}
		if(m_pConnection != nullptr)
			dbus_connection_set_exit_on_disconnect(m_pConnection, false);
		return m_pConnection != nullptr;
	}

	static bool NextDictEntry(DBusMessageIter &ArrayIter, DBusMessageIter &EntryIter, const char *&pKey, DBusMessageIter &VariantIter)
	{
		if(dbus_message_iter_get_arg_type(&ArrayIter) != DBUS_TYPE_DICT_ENTRY)
			return false;
		dbus_message_iter_recurse(&ArrayIter, &EntryIter);
		if(dbus_message_iter_get_arg_type(&EntryIter) != DBUS_TYPE_STRING)
			return false;
		dbus_message_iter_get_basic(&EntryIter, &pKey);
		if(!dbus_message_iter_next(&EntryIter) || dbus_message_iter_get_arg_type(&EntryIter) != DBUS_TYPE_VARIANT)
			return false;
		VariantIter = EntryIter;
		return true;
	}

	static bool VariantToString(DBusMessageIter VariantIter, std::string &Out)
	{
		DBusMessageIter ValueIter;
		dbus_message_iter_recurse(&VariantIter, &ValueIter);
		const int Type = dbus_message_iter_get_arg_type(&ValueIter);
		if(Type != DBUS_TYPE_STRING && Type != DBUS_TYPE_OBJECT_PATH)
			return false;
		const char *pValue = nullptr;
		dbus_message_iter_get_basic(&ValueIter, &pValue);
		Out = pValue != nullptr ? pValue : "";
		return true;
	}

	static bool VariantToBool(DBusMessageIter VariantIter, bool &Out)
	{
		DBusMessageIter ValueIter;
		dbus_message_iter_recurse(&VariantIter, &ValueIter);
		if(dbus_message_iter_get_arg_type(&ValueIter) != DBUS_TYPE_BOOLEAN)
			return false;
		dbus_bool_t Value = false;
		dbus_message_iter_get_basic(&ValueIter, &Value);
		Out = Value != 0;
		return true;
	}

	static bool VariantToInt64(DBusMessageIter VariantIter, int64_t &Out)
	{
		DBusMessageIter ValueIter;
		dbus_message_iter_recurse(&VariantIter, &ValueIter);
		const int Type = dbus_message_iter_get_arg_type(&ValueIter);
		if(Type == DBUS_TYPE_INT64)
		{
			dbus_int64_t Value = 0;
			dbus_message_iter_get_basic(&ValueIter, &Value);
			Out = Value;
			return true;
		}
		if(Type == DBUS_TYPE_UINT64)
		{
			dbus_uint64_t Value = 0;
			dbus_message_iter_get_basic(&ValueIter, &Value);
			Out = (int64_t)Value;
			return true;
		}
		if(Type == DBUS_TYPE_INT32)
		{
			dbus_int32_t Value = 0;
			dbus_message_iter_get_basic(&ValueIter, &Value);
			Out = Value;
			return true;
		}
		if(Type == DBUS_TYPE_UINT32)
		{
			dbus_uint32_t Value = 0;
			dbus_message_iter_get_basic(&ValueIter, &Value);
			Out = Value;
			return true;
		}
		return false;
	}

	static bool VariantToJoinedStringArray(DBusMessageIter VariantIter, std::string &Out)
	{
		DBusMessageIter ValueIter;
		dbus_message_iter_recurse(&VariantIter, &ValueIter);
		if(dbus_message_iter_get_arg_type(&ValueIter) != DBUS_TYPE_ARRAY)
			return false;

		DBusMessageIter ArrayIter;
		dbus_message_iter_recurse(&ValueIter, &ArrayIter);
		Out.clear();
		while(dbus_message_iter_get_arg_type(&ArrayIter) == DBUS_TYPE_STRING)
		{
			const char *pValue = nullptr;
			dbus_message_iter_get_basic(&ArrayIter, &pValue);
			if(pValue != nullptr && pValue[0] != '\0')
			{
				if(!Out.empty())
					Out += ", ";
				Out += pValue;
			}
			if(!dbus_message_iter_next(&ArrayIter))
				break;
		}
		return true;
	}

	bool ParseMetadata(DBusMessageIter VariantIter, SNowPlayingSnapshot &Out) const
	{
		DBusMessageIter MetadataVariant;
		dbus_message_iter_recurse(&VariantIter, &MetadataVariant);
		if(dbus_message_iter_get_arg_type(&MetadataVariant) != DBUS_TYPE_ARRAY)
			return false;

		DBusMessageIter MetadataArray;
		dbus_message_iter_recurse(&MetadataVariant, &MetadataArray);
		while(dbus_message_iter_get_arg_type(&MetadataArray) == DBUS_TYPE_DICT_ENTRY)
		{
			DBusMessageIter EntryIter;
			DBusMessageIter ValueVariantIter;
			const char *pKey = nullptr;
			if(NextDictEntry(MetadataArray, EntryIter, pKey, ValueVariantIter))
			{
				if(str_comp(pKey, "xesam:title") == 0)
				{
					VariantToString(ValueVariantIter, Out.m_Title);
				}
				else if(str_comp(pKey, "xesam:artist") == 0)
				{
					VariantToJoinedStringArray(ValueVariantIter, Out.m_Artist);
				}
				else if(str_comp(pKey, "xesam:album") == 0)
				{
					VariantToString(ValueVariantIter, Out.m_Album);
				}
				else if(str_comp(pKey, "mpris:length") == 0)
				{
					int64_t DurationUs = 0;
					if(VariantToInt64(ValueVariantIter, DurationUs))
						Out.m_DurationMs = maximum<int64_t>(0, DurationUs / 1000);
				}
				else if(str_comp(pKey, "mpris:artUrl") == 0)
				{
					std::string ArtUrl;
					if(VariantToString(ValueVariantIter, ArtUrl) && !ArtUrl.empty())
					{
						Out.m_Art.m_Type = SMusicArt::EType::URL;
						Out.m_Art.m_Url = ArtUrl;
						Out.m_Art.m_Key = ArtUrl;
					}
				}
			}

			if(!dbus_message_iter_next(&MetadataArray))
				break;
		}
		return true;
	}

	DBusMessage *CallMethod(const char *pService, const char *pPath, const char *pInterface, const char *pMethod, const char *pSingleStringArg = nullptr) const
	{
		if(m_pConnection == nullptr)
			return nullptr;

		DBusMessage *pMsg = dbus_message_new_method_call(pService, pPath, pInterface, pMethod);
		if(pMsg == nullptr)
			return nullptr;

		if(pSingleStringArg != nullptr)
		{
			const char *pArg = pSingleStringArg;
			if(!dbus_message_append_args(pMsg, DBUS_TYPE_STRING, &pArg, DBUS_TYPE_INVALID))
			{
				dbus_message_unref(pMsg);
				return nullptr;
			}
		}

		DBusError Error;
		dbus_error_init(&Error);
		DBusMessage *pReply = dbus_connection_send_with_reply_and_block(m_pConnection, pMsg, 1000, &Error);
		dbus_message_unref(pMsg);
		if(dbus_error_is_set(&Error))
		{
			dbus_error_free(&Error);
			if(pReply != nullptr)
				dbus_message_unref(pReply);
			return nullptr;
		}
		return pReply;
	}

	std::vector<std::string> ListServices()
	{
		std::vector<std::string> vServices;
		DBusMessage *pReply = CallMethod("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames");
		if(pReply == nullptr)
			return vServices;

		DBusMessageIter RootIter;
		if(dbus_message_iter_init(pReply, &RootIter) && dbus_message_iter_get_arg_type(&RootIter) == DBUS_TYPE_ARRAY)
		{
			DBusMessageIter ArrayIter;
			dbus_message_iter_recurse(&RootIter, &ArrayIter);
			while(dbus_message_iter_get_arg_type(&ArrayIter) == DBUS_TYPE_STRING)
			{
				const char *pName = nullptr;
				dbus_message_iter_get_basic(&ArrayIter, &pName);
				if(pName != nullptr && str_startswith(pName, "org.mpris.MediaPlayer2."))
					vServices.emplace_back(pName);
				if(!dbus_message_iter_next(&ArrayIter))
					break;
			}
		}
		dbus_message_unref(pReply);

		std::sort(vServices.begin(), vServices.end(), [&](const std::string &Left, const std::string &Right) {
			if(Left == m_CurrentService)
				return true;
			if(Right == m_CurrentService)
				return false;
			return Left < Right;
		});
		return vServices;
	}

	bool ReadServiceSnapshot(const std::string &Service, SNowPlayingSnapshot &Out)
	{
		DBusMessage *pReply = CallMethod(Service.c_str(), "/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties", "GetAll", "org.mpris.MediaPlayer2.Player");
		if(pReply == nullptr)
			return false;

		Out = SNowPlayingSnapshot();
		Out.m_ServiceId = Service;

		DBusMessageIter RootIter;
		if(!dbus_message_iter_init(pReply, &RootIter) || dbus_message_iter_get_arg_type(&RootIter) != DBUS_TYPE_ARRAY)
		{
			dbus_message_unref(pReply);
			return false;
		}

		DBusMessageIter ArrayIter;
		dbus_message_iter_recurse(&RootIter, &ArrayIter);
		while(dbus_message_iter_get_arg_type(&ArrayIter) == DBUS_TYPE_DICT_ENTRY)
		{
			DBusMessageIter EntryIter;
			DBusMessageIter ValueVariantIter;
			const char *pKey = nullptr;
			if(NextDictEntry(ArrayIter, EntryIter, pKey, ValueVariantIter))
			{
				if(str_comp(pKey, "PlaybackStatus") == 0)
				{
					std::string Status;
					if(VariantToString(ValueVariantIter, Status))
					{
						if(Status == "Playing")
							Out.m_PlaybackState = EMusicPlaybackState::PLAYING;
						else if(Status == "Paused")
							Out.m_PlaybackState = EMusicPlaybackState::PAUSED;
						else
							Out.m_PlaybackState = EMusicPlaybackState::STOPPED;
					}
				}
				else if(str_comp(pKey, "Position") == 0)
				{
					int64_t PositionUs = 0;
					if(VariantToInt64(ValueVariantIter, PositionUs))
						Out.m_PositionMs = maximum<int64_t>(0, PositionUs / 1000);
				}
				else if(str_comp(pKey, "CanGoPrevious") == 0)
					VariantToBool(ValueVariantIter, Out.m_CanPrev);
				else if(str_comp(pKey, "CanGoNext") == 0)
					VariantToBool(ValueVariantIter, Out.m_CanNext);
				else if(str_comp(pKey, "CanPlay") == 0)
				{
					bool CanPlay = false;
					if(VariantToBool(ValueVariantIter, CanPlay))
						Out.m_CanPlayPause = Out.m_CanPlayPause || CanPlay;
				}
				else if(str_comp(pKey, "CanPause") == 0)
				{
					bool CanPause = false;
					if(VariantToBool(ValueVariantIter, CanPause))
						Out.m_CanPlayPause = Out.m_CanPlayPause || CanPause;
				}
				else if(str_comp(pKey, "Metadata") == 0)
					ParseMetadata(ValueVariantIter, Out);
			}

			if(!dbus_message_iter_next(&ArrayIter))
				break;
		}

		dbus_message_unref(pReply);
		Out.m_Valid = Out.m_PlaybackState != EMusicPlaybackState::STOPPED && (!Out.m_Title.empty() || !Out.m_Artist.empty() || !Out.m_Album.empty());
		if(Out.m_Art.m_Key.empty() && Out.m_Art.m_Type == SMusicArt::EType::NONE)
			Out.m_Art.m_Key = Out.m_ServiceId + "|" + Out.m_Title + "|" + Out.m_Artist;
		return Out.m_Valid;
	}

	void SendPlayerMethod(const char *pMethod)
	{
		if(m_CurrentService.empty() || !EnsureConnection())
			return;
		if(DBusMessage *pReply = CallMethod(m_CurrentService.c_str(), "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player", pMethod))
			dbus_message_unref(pReply);
	}

	bool SetShuffle(bool Enabled)
	{
		if(m_CurrentService.empty() || !EnsureConnection())
			return false;

		DBusMessage *pMsg = dbus_message_new_method_call(
			m_CurrentService.c_str(),
			"/org/mpris/MediaPlayer2",
			"org.freedesktop.DBus.Properties",
			"Set");
		if(pMsg == nullptr)
			return false;

		const char *pInterface = "org.mpris.MediaPlayer2.Player";
		const char *pProperty = "Shuffle";
		dbus_bool_t ShuffleValue = Enabled ? true : false;

		DBusMessageIter ArgsIter;
		DBusMessageIter VariantIter;
		dbus_message_iter_init_append(pMsg, &ArgsIter);
		if(!dbus_message_iter_append_basic(&ArgsIter, DBUS_TYPE_STRING, &pInterface) ||
			!dbus_message_iter_append_basic(&ArgsIter, DBUS_TYPE_STRING, &pProperty) ||
			!dbus_message_iter_open_container(&ArgsIter, DBUS_TYPE_VARIANT, DBUS_TYPE_BOOLEAN_AS_STRING, &VariantIter) ||
			!dbus_message_iter_append_basic(&VariantIter, DBUS_TYPE_BOOLEAN, &ShuffleValue) ||
			!dbus_message_iter_close_container(&ArgsIter, &VariantIter))
		{
			dbus_message_unref(pMsg);
			return false;
		}

		DBusError Error;
		dbus_error_init(&Error);
		DBusMessage *pReply = dbus_connection_send_with_reply_and_block(m_pConnection, pMsg, 1000, &Error);
		dbus_message_unref(pMsg);
		if(dbus_error_is_set(&Error))
		{
			dbus_error_free(&Error);
			if(pReply != nullptr)
				dbus_message_unref(pReply);
			return false;
		}

		if(pReply != nullptr)
			dbus_message_unref(pReply);
		return true;
	}

	void DisableShuffleForOrderedNavigation()
	{
		if(!ShouldForceOrderedNavigation(m_LastSnapshot))
		{
			m_ShuffleForcedForCurrentService = false;
			return;
		}
		if(m_ShuffleForcedForCurrentService)
			return;

		if(SetShuffle(false))
			m_ShuffleForcedForCurrentService = true;
	}

public:
	~CLinuxNowPlayingProvider() override
	{
		if(m_pConnection != nullptr)
			dbus_connection_unref(m_pConnection);
	}

	bool Poll(SNowPlayingSnapshot &Out) override
	{
		Out = SNowPlayingSnapshot();
		if(!EnsureConnection())
			return false;

		int BestScore = -1;
		for(const std::string &Service : ListServices())
		{
			SNowPlayingSnapshot Candidate;
			if(!ReadServiceSnapshot(Service, Candidate))
				continue;

			int Score = Candidate.m_PlaybackState == EMusicPlaybackState::PLAYING ? 20 : 10;
			if(Service == m_CurrentService)
				Score += 5;
			if(!Candidate.m_Title.empty())
				Score += 2;
			if(!Candidate.m_Artist.empty())
				Score += 1;

			if(Score > BestScore)
			{
				BestScore = Score;
				Out = std::move(Candidate);
			}
		}

		if(BestScore < 0)
		{
			m_CurrentService.clear();
			m_LastSnapshot = SNowPlayingSnapshot();
			m_ShuffleForcedForCurrentService = false;
			return false;
		}

		if(m_CurrentService != Out.m_ServiceId)
			m_ShuffleForcedForCurrentService = false;
		m_CurrentService = Out.m_ServiceId;
		m_LastSnapshot = Out;
		return true;
	}

	void Previous() override
	{
		DisableShuffleForOrderedNavigation();
		SendPlayerMethod("Previous");
	}
	void PlayPause() override { SendPlayerMethod("PlayPause"); }
	void Next() override
	{
		DisableShuffleForOrderedNavigation();
		SendPlayerMethod("Next");
	}
};
#endif

#if defined(CONF_FAMILY_WINDOWS) && BC_MUSICPLAYER_HAS_WINRT
namespace WmControl = winrt::Windows::Media::Control;
namespace WStreams = winrt::Windows::Storage::Streams;

class CWindowsNowPlayingProvider final : public IMusicPlaybackProvider
{
	std::thread m_WorkerThread;
	std::mutex m_Mutex;
	std::condition_variable m_Cv;
	bool m_Shutdown = false;
	bool m_PollRequested = true;
	bool m_RequestPrev = false;
	bool m_RequestPlayPause = false;
	bool m_RequestNext = false;
	bool m_HasSnapshot = false;
	SNowPlayingSnapshot m_LatestSnapshot;

	static int64_t ToMilliseconds(winrt::Windows::Foundation::TimeSpan TimeSpan)
	{
		return maximum<int64_t>(0, TimeSpan.count() / 10000);
	}

	static EMusicPlaybackState TranslatePlaybackState(WmControl::GlobalSystemMediaTransportControlsSessionPlaybackStatus Status)
	{
		if(Status == WmControl::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing)
			return EMusicPlaybackState::PLAYING;
		if(Status == WmControl::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused ||
			Status == WmControl::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Opened ||
			Status == WmControl::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Changing)
			return EMusicPlaybackState::PAUSED;
		return EMusicPlaybackState::STOPPED;
	}

	static std::vector<unsigned char> ReadThumbnailBytes(const WStreams::IRandomAccessStreamReference &Thumbnail)
	{
		std::vector<unsigned char> vBytes;
		if(!Thumbnail)
			return vBytes;
		try
		{
			auto Stream = Thumbnail.OpenReadAsync().get();
			const uint64_t Size = Stream.Size();
			if(Size == 0 || Size > 8ull * 1024ull * 1024ull)
				return vBytes;

			WStreams::DataReader Reader(Stream);
			Reader.LoadAsync((uint32_t)Size).get();
			vBytes.resize((size_t)Size);
			Reader.ReadBytes(winrt::array_view<uint8_t>(vBytes));
		}
		catch(...)
		{
			vBytes.clear();
		}
		return vBytes;
	}

	static std::string SessionId(const WmControl::GlobalSystemMediaTransportControlsSession &Session)
	{
		return Session ? winrt::to_string(Session.SourceAppUserModelId()) : std::string();
	}

	static WmControl::GlobalSystemMediaTransportControlsSessionManager RequestManager()
	{
		try
		{
			return WmControl::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
		}
		catch(...)
		{
			return nullptr;
		}
	}

	template<typename TSession>
	static void DisableShuffleForOrderedNavigation(TSession &&Session, const SNowPlayingSnapshot &Snapshot)
	{
		if(!ShouldForceOrderedNavigation(Snapshot))
			return;
		if constexpr(requires { Session.TryChangeShuffleActiveAsync(false); })
		{
			try
			{
				Session.TryChangeShuffleActiveAsync(false).get();
			}
			catch(...)
			{
			}
		}
	}

	bool QuerySession(const WmControl::GlobalSystemMediaTransportControlsSession &Session, SNowPlayingSnapshot &Out)
	{
		Out = SNowPlayingSnapshot();
		if(!Session)
			return false;

		try
		{
			Out.m_ServiceId = SessionId(Session);

			auto PlaybackInfo = Session.GetPlaybackInfo();
			auto Controls = PlaybackInfo.Controls();
			Out.m_PlaybackState = TranslatePlaybackState(PlaybackInfo.PlaybackStatus());

			auto Timeline = Session.GetTimelineProperties();
			Out.m_PositionMs = ToMilliseconds(Timeline.Position());
			Out.m_DurationMs = maximum<int64_t>(0, ToMilliseconds(Timeline.EndTime()) - ToMilliseconds(Timeline.StartTime()));

			Out.m_CanPrev = Controls.IsPreviousEnabled();
			Out.m_CanNext = Controls.IsNextEnabled();
			Out.m_CanPlayPause = Controls.IsPauseEnabled() || Controls.IsPlayEnabled() || Controls.IsPlayPauseToggleEnabled();
		}
		catch(...)
		{
			return false;
		}

		try
		{
			auto MediaProps = Session.TryGetMediaPropertiesAsync().get();
			Out.m_Title = winrt::to_string(MediaProps.Title());
			Out.m_Artist = winrt::to_string(MediaProps.Artist());
			Out.m_Album = winrt::to_string(MediaProps.AlbumTitle());

			std::vector<unsigned char> vArtBytes = ReadThumbnailBytes(MediaProps.Thumbnail());
			if(!vArtBytes.empty())
			{
				Out.m_Art.m_Type = SMusicArt::EType::BYTES;
				Out.m_Art.m_Key = Out.m_ServiceId + "|" + Out.m_Title + "|" + Out.m_Artist + "|" + std::to_string(vArtBytes.size());
				Out.m_Art.m_vBytes = std::move(vArtBytes);
			}
			else
			{
				Out.m_Art.m_Key = Out.m_ServiceId + "|" + Out.m_Title + "|" + Out.m_Artist;
			}
		}
		catch(...)
		{
		}

		if(Out.m_Art.m_Key.empty())
			Out.m_Art.m_Key = Out.m_ServiceId + "|" + Out.m_Title + "|" + Out.m_Artist;

		Out.m_Valid =
			Out.m_PlaybackState != EMusicPlaybackState::STOPPED &&
			(!Out.m_Title.empty() || !Out.m_Artist.empty() || !Out.m_Album.empty() || Out.m_CanPlayPause || Out.m_CanPrev || Out.m_CanNext);
		return Out.m_Valid;
	}

	static std::optional<WmControl::GlobalSystemMediaTransportControlsSession> FindTrackedSession(WmControl::GlobalSystemMediaTransportControlsSessionManager &Manager, const std::string &CurrentSessionId)
	{
		if(!Manager || CurrentSessionId.empty())
			return std::nullopt;

		try
		{
			auto Sessions = Manager.GetSessions();
			const uint32_t Count = Sessions.Size();
			for(uint32_t i = 0; i < Count; ++i)
			{
				auto Session = Sessions.GetAt(i);
				if(SessionId(Session) == CurrentSessionId)
					return Session;
			}
		}
		catch(...)
		{
		}
		return std::nullopt;
	}

	static int SessionScore(const SNowPlayingSnapshot &Snapshot, const std::string &CurrentSessionId, const std::string &SystemCurrentSessionId)
	{
		int Score = 0;
		if(Snapshot.m_PlaybackState == EMusicPlaybackState::PLAYING)
			Score += 20;
		else if(Snapshot.m_PlaybackState == EMusicPlaybackState::PAUSED)
			Score += 10;

		if(Snapshot.m_ServiceId == CurrentSessionId)
			Score += 5;
		if(Snapshot.m_ServiceId == SystemCurrentSessionId)
			Score += 3;
		if(!Snapshot.m_Title.empty())
			Score += 3;
		if(!Snapshot.m_Artist.empty())
			Score += 2;
		if(!Snapshot.m_Album.empty())
			Score += 1;
		if(Snapshot.m_CanPlayPause)
			Score += 1;
		return Score;
	}

	template<typename TAction>
	static void WithSession(WmControl::GlobalSystemMediaTransportControlsSessionManager &Manager, const std::string &CurrentSessionId, TAction &&Action)
	{
		if(!Manager)
			return;
		try
		{
			auto Session = FindTrackedSession(Manager, CurrentSessionId).value_or(Manager.GetCurrentSession());
			if(Session)
				Action(Session);
		}
		catch(...)
		{
		}
	}

	bool PollSessions(WmControl::GlobalSystemMediaTransportControlsSessionManager &Manager, std::string &CurrentSessionId, SNowPlayingSnapshot &Out)
	{
		Out = SNowPlayingSnapshot();
		if(!Manager)
			return false;

		std::string SystemCurrentSessionId;
		try
		{
			SystemCurrentSessionId = SessionId(Manager.GetCurrentSession());
		}
		catch(...)
		{
		}

		int BestScore = -1;
		try
		{
			auto Sessions = Manager.GetSessions();
			const uint32_t Count = Sessions.Size();
			for(uint32_t i = 0; i < Count; ++i)
			{
				auto Session = Sessions.GetAt(i);
				SNowPlayingSnapshot Candidate;
				if(!QuerySession(Session, Candidate))
					continue;

				const int Score = SessionScore(Candidate, CurrentSessionId, SystemCurrentSessionId);
				if(Score > BestScore)
				{
					BestScore = Score;
					Out = std::move(Candidate);
				}
			}
		}
		catch(...)
		{
			return false;
		}

		if(BestScore < 0)
		{
			CurrentSessionId.clear();
			return false;
		}

		CurrentSessionId = Out.m_ServiceId;
		return true;
	}

	void StoreSnapshot(const SNowPlayingSnapshot &Snapshot, bool HasSnapshot)
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		m_HasSnapshot = HasSnapshot;
		m_LatestSnapshot = HasSnapshot ? Snapshot : SNowPlayingSnapshot();
	}

	void WorkerMain()
	{
		try
		{
			winrt::init_apartment(winrt::apartment_type::multi_threaded);
		}
		catch(...)
		{
			StoreSnapshot(SNowPlayingSnapshot(), false);
			return;
		}

		WmControl::GlobalSystemMediaTransportControlsSessionManager Manager = RequestManager();
		std::string CurrentSessionId;
		SNowPlayingSnapshot LastSnapshot;
		std::string OrderedShuffleSessionId;

		while(true)
		{
			bool PollRequested = false;
			bool RequestPrev = false;
			bool RequestPlayPause = false;
			bool RequestNext = false;
			{
				std::unique_lock<std::mutex> Lock(m_Mutex);
				m_Cv.wait_for(Lock, std::chrono::milliseconds(100), [this]() {
					return m_Shutdown || m_PollRequested || m_RequestPrev || m_RequestPlayPause || m_RequestNext;
				});
				if(m_Shutdown)
					break;

				PollRequested = m_PollRequested;
				RequestPrev = m_RequestPrev;
				RequestPlayPause = m_RequestPlayPause;
				RequestNext = m_RequestNext;
				m_PollRequested = false;
				m_RequestPrev = false;
				m_RequestPlayPause = false;
				m_RequestNext = false;
			}

			if(!Manager)
				Manager = RequestManager();

			if(Manager && RequestPrev)
				WithSession(Manager, CurrentSessionId, [&](auto &&Session) {
					const std::string SessionIdValue = SessionId(Session);
					if(ShouldForceOrderedNavigation(LastSnapshot) && OrderedShuffleSessionId != SessionIdValue)
					{
						DisableShuffleForOrderedNavigation(Session, LastSnapshot);
						OrderedShuffleSessionId = SessionIdValue;
					}
					Session.TrySkipPreviousAsync().get();
				});
			if(Manager && RequestPlayPause)
				WithSession(Manager, CurrentSessionId, [](auto &&Session) { Session.TryTogglePlayPauseAsync().get(); });
			if(Manager && RequestNext)
				WithSession(Manager, CurrentSessionId, [&](auto &&Session) {
					const std::string SessionIdValue = SessionId(Session);
					if(ShouldForceOrderedNavigation(LastSnapshot) && OrderedShuffleSessionId != SessionIdValue)
					{
						DisableShuffleForOrderedNavigation(Session, LastSnapshot);
						OrderedShuffleSessionId = SessionIdValue;
					}
					Session.TrySkipNextAsync().get();
				});

			if(PollRequested || RequestPrev || RequestPlayPause || RequestNext)
			{
				SNowPlayingSnapshot Snapshot;
				const bool HasSnapshot = PollSessions(Manager, CurrentSessionId, Snapshot);
				LastSnapshot = HasSnapshot ? Snapshot : SNowPlayingSnapshot();
				if(!HasSnapshot || !ShouldForceOrderedNavigation(LastSnapshot))
					OrderedShuffleSessionId.clear();
				StoreSnapshot(Snapshot, HasSnapshot);
			}
		}

		winrt::uninit_apartment();
	}

	void QueueAction(bool &Flag)
	{
		{
			std::lock_guard<std::mutex> Lock(m_Mutex);
			Flag = true;
			m_PollRequested = true;
		}
		m_Cv.notify_one();
	}

public:
	CWindowsNowPlayingProvider()
	{
		m_WorkerThread = std::thread([this]() { WorkerMain(); });
	}

	~CWindowsNowPlayingProvider() override
	{
		{
			std::lock_guard<std::mutex> Lock(m_Mutex);
			m_Shutdown = true;
		}
		m_Cv.notify_one();
		if(m_WorkerThread.joinable())
			m_WorkerThread.join();
	}

	bool Poll(SNowPlayingSnapshot &Out) override
	{
		try
		{
			{
				std::lock_guard<std::mutex> Lock(m_Mutex);
				Out = m_HasSnapshot ? m_LatestSnapshot : SNowPlayingSnapshot();
				m_PollRequested = true;
			}
			m_Cv.notify_one();
			return Out.m_Valid;
		}
		catch(...)
		{
			Out = SNowPlayingSnapshot();
			return false;
		}
	}

	void Previous() override
	{
		QueueAction(m_RequestPrev);
	}

	void PlayPause() override
	{
		QueueAction(m_RequestPlayPause);
	}

	void Next() override
	{
		QueueAction(m_RequestNext);
	}
};
#endif

#if defined(CONF_PLATFORM_LINUX) && BC_MUSICPLAYER_HAS_PULSE
class CLinuxPulseVisualizer final : public IMusicVisualizerSource
{
	std::thread m_WorkerThread;
	std::mutex m_Mutex;
	bool m_Shutdown = false;
	SMusicVisualizerData m_LatestFrame;
	int m_ConsecutiveFailures = 0;
	SMusicPlaybackHint m_PlaybackHint;
	int64_t m_HintRevision = 0;
	int64_t m_AppliedHintRevision = -1;

	struct SPulseCaptureTarget
	{
		uint32_t m_SinkIndex = PA_INVALID_INDEX;
		std::string m_DefaultSink;
		std::string m_SinkName;
		std::string m_SinkDescription;
		std::string m_MonitorSource;
		int m_SourceState = -1;
		int m_SampleRate = 44100;
		uint8_t m_Channels = 2;
		int m_Score = 0;
		bool m_FromActiveSinkInput = false;
		bool m_MatchesPlaybackHint = false;
		std::string m_AppName;
		std::string m_AppBinary;
		std::string m_MediaRole;
	};

	struct SPulseServerInfoRequest
	{
		bool m_Done = false;
		std::string m_DefaultSink;
	};

	struct SPulseSinkInfo
	{
		uint32_t m_Index = PA_INVALID_INDEX;
		std::string m_Name;
		std::string m_Description;
		std::string m_MonitorSource;
		int m_SampleRate = 44100;
		uint8_t m_Channels = 2;
	};

	struct SPulseSourceInfo
	{
		std::string m_Name;
		uint32_t m_MonitorOfSink = PA_INVALID_INDEX;
		int m_State = -1;
	};

	struct SPulseSinkInputInfo
	{
		uint32_t m_Index = PA_INVALID_INDEX;
		uint32_t m_SinkIndex = PA_INVALID_INDEX;
		std::string m_Name;
		std::string m_AppName;
		std::string m_AppBinary;
		std::string m_MediaRole;
		bool m_Corked = false;
		bool m_Muted = false;
	};

	struct SPulseDiscoveryRequest
	{
		bool m_ServerDone = false;
		bool m_SinksDone = false;
		bool m_SourcesDone = false;
		bool m_SinkInputsDone = false;
		std::string m_DefaultSink;
		std::vector<SPulseSinkInfo> m_vSinks;
		std::vector<SPulseSourceInfo> m_vSources;
		std::vector<SPulseSinkInputInfo> m_vSinkInputs;
	};

	static void PulseServerInfoCallback(pa_context *, const pa_server_info *pInfo, void *pUserData)
	{
		auto *pRequest = static_cast<SPulseDiscoveryRequest *>(pUserData);
		if(pRequest == nullptr)
			return;
		pRequest->m_DefaultSink = (pInfo != nullptr && pInfo->default_sink_name != nullptr) ? pInfo->default_sink_name : "";
		pRequest->m_ServerDone = true;
	}

	static void PulseSinkInfoCallback(pa_context *, const pa_sink_info *pInfo, int Eol, void *pUserData)
	{
		auto *pRequest = static_cast<SPulseDiscoveryRequest *>(pUserData);
		if(pRequest == nullptr)
			return;
		if(Eol != 0)
		{
			pRequest->m_SinksDone = true;
			return;
		}
		if(pInfo != nullptr && pInfo->monitor_source_name != nullptr)
		{
			SPulseSinkInfo Info;
			Info.m_Index = pInfo->index;
			Info.m_Name = pInfo->name != nullptr ? pInfo->name : "";
			Info.m_Description = pInfo->description != nullptr ? pInfo->description : "";
			Info.m_MonitorSource = pInfo->monitor_source_name;
			Info.m_SampleRate = maximum(8000, (int)pInfo->sample_spec.rate);
			Info.m_Channels = std::clamp<uint8_t>(pInfo->sample_spec.channels, 1, 8);
			pRequest->m_vSinks.push_back(std::move(Info));
		}
	}

	static void PulseSourceInfoCallback(pa_context *, const pa_source_info *pInfo, int Eol, void *pUserData)
	{
		auto *pRequest = static_cast<SPulseDiscoveryRequest *>(pUserData);
		if(pRequest == nullptr)
			return;
		if(Eol != 0)
		{
			pRequest->m_SourcesDone = true;
			return;
		}
		if(pInfo != nullptr && pInfo->name != nullptr && pInfo->monitor_of_sink != PA_INVALID_INDEX)
		{
			SPulseSourceInfo Info;
			Info.m_Name = pInfo->name;
			Info.m_MonitorOfSink = pInfo->monitor_of_sink;
			Info.m_State = (int)pInfo->state;
			pRequest->m_vSources.push_back(std::move(Info));
		}
	}

	static void PulseSinkInputInfoCallback(pa_context *, const pa_sink_input_info *pInfo, int Eol, void *pUserData)
	{
		auto *pRequest = static_cast<SPulseDiscoveryRequest *>(pUserData);
		if(pRequest == nullptr)
			return;
		if(Eol != 0)
		{
			pRequest->m_SinkInputsDone = true;
			return;
		}
		if(pInfo != nullptr)
		{
			SPulseSinkInputInfo Info;
			Info.m_Index = pInfo->index;
			Info.m_SinkIndex = pInfo->sink;
			Info.m_Name = pInfo->name != nullptr ? pInfo->name : "";
			Info.m_Corked = pInfo->corked != 0;
			Info.m_Muted = pInfo->mute != 0;
			if(pInfo->proplist != nullptr)
			{
				if(const char *pValue = pa_proplist_gets(pInfo->proplist, PA_PROP_APPLICATION_NAME))
					Info.m_AppName = pValue;
				if(const char *pValue = pa_proplist_gets(pInfo->proplist, PA_PROP_APPLICATION_PROCESS_BINARY))
					Info.m_AppBinary = pValue;
				if(const char *pValue = pa_proplist_gets(pInfo->proplist, PA_PROP_MEDIA_ROLE))
					Info.m_MediaRole = pValue;
			}
			pRequest->m_vSinkInputs.push_back(std::move(Info));
		}
	}

	static bool WaitForPulseContext(pa_mainloop *pMainloop, pa_context *pContext)
	{
		while(true)
		{
			const pa_context_state_t State = pa_context_get_state(pContext);
			if(State == PA_CONTEXT_READY)
				return true;
			if(State == PA_CONTEXT_FAILED || State == PA_CONTEXT_TERMINATED)
				return false;
			int Ret = 0;
			if(pa_mainloop_iterate(pMainloop, 1, &Ret) < 0)
				return false;
		}
	}

	static bool WaitForPulseRequest(pa_mainloop *pMainloop, pa_operation *pOperation, bool &DoneFlag)
	{
		while(pOperation != nullptr && pa_operation_get_state(pOperation) == PA_OPERATION_RUNNING && !DoneFlag)
		{
			int Ret = 0;
			if(pa_mainloop_iterate(pMainloop, 1, &Ret) < 0)
			{
				pa_operation_unref(pOperation);
				return false;
			}
		}
		if(pOperation != nullptr)
			pa_operation_unref(pOperation);
		return DoneFlag;
	}

	static bool ContainsI(std::string Haystack, std::string Needle)
	{
		if(Haystack.empty() || Needle.empty())
			return false;
		std::transform(Haystack.begin(), Haystack.end(), Haystack.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		std::transform(Needle.begin(), Needle.end(), Needle.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		return Haystack.find(Needle) != std::string::npos;
	}

	static bool LooksLikeMediaRole(const std::string &Role)
	{
		return ContainsI(Role, "music") || ContainsI(Role, "video") || ContainsI(Role, "movie") || ContainsI(Role, "multimedia");
	}

	static bool LooksLikePlaybackApp(const std::string &AppName, const std::string &AppBinary)
	{
		return
			ContainsI(AppName, "spotify") || ContainsI(AppBinary, "spotify") ||
			ContainsI(AppName, "vlc") || ContainsI(AppBinary, "vlc") ||
			ContainsI(AppName, "chrom") || ContainsI(AppBinary, "chrom") ||
			ContainsI(AppName, "firefox") || ContainsI(AppBinary, "firefox") ||
			ContainsI(AppName, "mpv") || ContainsI(AppBinary, "mpv");
	}

	static std::vector<SPulseCaptureTarget> ResolveCaptureTargets(const SMusicPlaybackHint &Hint)
	{
		std::vector<SPulseCaptureTarget> vTargets;
		pa_mainloop *pMainloop = pa_mainloop_new();
		if(pMainloop == nullptr)
			return vTargets;

		pa_context *pContext = pa_context_new(pa_mainloop_get_api(pMainloop), "DDNet music visualizer");
		if(pContext == nullptr)
		{
			pa_mainloop_free(pMainloop);
			return vTargets;
		}

		if(pa_context_connect(pContext, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0 || !WaitForPulseContext(pMainloop, pContext))
		{
			pa_context_unref(pContext);
			pa_mainloop_free(pMainloop);
			return vTargets;
		}

		SPulseDiscoveryRequest Discovery;
		pa_operation *pServerOp = pa_context_get_server_info(pContext, PulseServerInfoCallback, &Discovery);
		pa_operation *pSinksOp = pa_context_get_sink_info_list(pContext, PulseSinkInfoCallback, &Discovery);
		pa_operation *pSourcesOp = pa_context_get_source_info_list(pContext, PulseSourceInfoCallback, &Discovery);
		pa_operation *pSinkInputsOp = pa_context_get_sink_input_info_list(pContext, PulseSinkInputInfoCallback, &Discovery);
		const bool ServerOk = WaitForPulseRequest(pMainloop, pServerOp, Discovery.m_ServerDone);
		const bool SinksOk = WaitForPulseRequest(pMainloop, pSinksOp, Discovery.m_SinksDone);
		const bool SourcesOk = WaitForPulseRequest(pMainloop, pSourcesOp, Discovery.m_SourcesDone);
		const bool SinkInputsOk = WaitForPulseRequest(pMainloop, pSinkInputsOp, Discovery.m_SinkInputsDone);

		pa_context_disconnect(pContext);
		pa_context_unref(pContext);
		pa_mainloop_free(pMainloop);
		if(!ServerOk || !SinksOk || !SourcesOk || !SinkInputsOk)
			return vTargets;

		auto FindSourceState = [&](const SPulseSinkInfo &Sink) {
			for(const auto &Source : Discovery.m_vSources)
			{
				if(Source.m_MonitorOfSink == Sink.m_Index || (!Sink.m_MonitorSource.empty() && Sink.m_MonitorSource == Source.m_Name))
					return Source.m_State;
			}
			return -1;
		};

		auto UpsertTarget = [&](SPulseCaptureTarget Target) {
			for(auto &Existing : vTargets)
			{
				if(Existing.m_MonitorSource == Target.m_MonitorSource)
				{
					if(Target.m_Score > Existing.m_Score)
						Existing = std::move(Target);
					return;
				}
			}
			vTargets.push_back(std::move(Target));
		};

		for(const auto &Sink : Discovery.m_vSinks)
		{
			if(Sink.m_MonitorSource.empty())
				continue;

			const int SourceState = FindSourceState(Sink);
			const bool SourceUsable = SourceState < 0 || PA_SOURCE_IS_OPENED((pa_source_state_t)SourceState);
			if(!SourceUsable)
				continue;

			SPulseCaptureTarget BaseTarget;
			BaseTarget.m_SinkIndex = Sink.m_Index;
			BaseTarget.m_DefaultSink = Discovery.m_DefaultSink;
			BaseTarget.m_SinkName = Sink.m_Name;
			BaseTarget.m_SinkDescription = Sink.m_Description;
			BaseTarget.m_MonitorSource = Sink.m_MonitorSource;
			BaseTarget.m_SourceState = SourceState;
			BaseTarget.m_SampleRate = Sink.m_SampleRate;
			BaseTarget.m_Channels = Sink.m_Channels;
			if(Sink.m_Name == Discovery.m_DefaultSink)
				BaseTarget.m_Score += 30;

			UpsertTarget(BaseTarget);
		}

		for(const auto &Input : Discovery.m_vSinkInputs)
		{
			if(Input.m_Corked || Input.m_Muted)
				continue;
			for(const auto &Sink : Discovery.m_vSinks)
			{
				if(Sink.m_Index != Input.m_SinkIndex || Sink.m_MonitorSource.empty())
					continue;

				const int SourceState = FindSourceState(Sink);
				const bool SourceUsable = SourceState < 0 || PA_SOURCE_IS_OPENED((pa_source_state_t)SourceState);
				if(!SourceUsable)
					continue;

				SPulseCaptureTarget Target;
				Target.m_SinkIndex = Sink.m_Index;
				Target.m_DefaultSink = Discovery.m_DefaultSink;
				Target.m_SinkName = Sink.m_Name;
				Target.m_SinkDescription = Sink.m_Description;
				Target.m_MonitorSource = Sink.m_MonitorSource;
				Target.m_SourceState = SourceState;
				Target.m_SampleRate = Sink.m_SampleRate;
				Target.m_Channels = Sink.m_Channels;
				Target.m_FromActiveSinkInput = true;
				Target.m_AppName = Input.m_AppName;
				Target.m_AppBinary = Input.m_AppBinary;
				Target.m_MediaRole = Input.m_MediaRole;
				Target.m_Score = 60;
				if(LooksLikeMediaRole(Input.m_MediaRole))
					Target.m_Score += 30;
				if(LooksLikePlaybackApp(Input.m_AppName, Input.m_AppBinary))
					Target.m_Score += 20;
				if(Sink.m_Name == Discovery.m_DefaultSink)
					Target.m_Score += 10;
				if(!Hint.m_ServiceId.empty())
				{
					const bool ChromiumHint = ContainsI(Hint.m_ServiceId, "chrom");
					const bool SpotifyHint = ContainsI(Hint.m_ServiceId, "spotify");
					const bool VlcHint = ContainsI(Hint.m_ServiceId, "vlc");
					const bool FirefoxHint = ContainsI(Hint.m_ServiceId, "firefox");
					if((ChromiumHint && (ContainsI(Input.m_AppBinary, "chrom") || ContainsI(Input.m_AppName, "chrom"))) ||
						(SpotifyHint && (ContainsI(Input.m_AppBinary, "spotify") || ContainsI(Input.m_AppName, "spotify"))) ||
						(VlcHint && (ContainsI(Input.m_AppBinary, "vlc") || ContainsI(Input.m_AppName, "vlc"))) ||
						(FirefoxHint && (ContainsI(Input.m_AppBinary, "firefox") || ContainsI(Input.m_AppName, "firefox"))))
					{
						Target.m_MatchesPlaybackHint = true;
						Target.m_Score += 80;
					}
				}
				UpsertTarget(std::move(Target));
			}
		}

		std::sort(vTargets.begin(), vTargets.end(), [](const SPulseCaptureTarget &A, const SPulseCaptureTarget &B) {
			if(A.m_Score != B.m_Score)
				return A.m_Score > B.m_Score;
			return A.m_MonitorSource < B.m_MonitorSource;
		});
		return vTargets;
	}

	void StoreFrame(const SMusicVisualizerData &Frame)
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		m_LatestFrame = Frame;
	}

	void StoreBackendState(EMusicVisualizerBackendStatus Status)
	{
		SMusicVisualizerData Frame;
		Frame.m_BackendStatus = Status;
		Frame.m_IsPassiveFallback = Status == EMusicVisualizerBackendStatus::FALLBACK || Status == EMusicVisualizerBackendStatus::UNAVAILABLE;
		StoreFrame(Frame);
	}

	void WorkerMain()
	{
		pa_simple *pSimple = nullptr;
		std::vector<float> vSamples;
		vSamples.reserve(8192);
		std::vector<float> vReadBuffer;
		SPulseCaptureTarget CurrentTarget;
		size_t CurrentCandidateIndex = 0;
		std::vector<SPulseCaptureTarget> vCandidates;
		int CurrentSampleRate = 44100;
		int CurrentChannels = 2;
		int64_t NextReconnectCheckTick = 0;
		int ValidatedReads = 0;
		int SilentReads = 0;
		bool ValidatedLive = false;

		auto ReadPlaybackHint = [&]() {
			std::lock_guard<std::mutex> Lock(m_Mutex);
			return std::pair<SMusicPlaybackHint, int64_t>(m_PlaybackHint, m_HintRevision);
		};

		auto Cleanup = [&]() {
			if(pSimple != nullptr)
			{
				pa_simple_free(pSimple);
				pSimple = nullptr;
			}
			vSamples.clear();
			CurrentTarget = SPulseCaptureTarget();
			CurrentCandidateIndex = 0;
			NextReconnectCheckTick = 0;
			ValidatedReads = 0;
			SilentReads = 0;
			ValidatedLive = false;
		};

		auto EnsureCapture = [&]() -> bool {
			if(pSimple != nullptr)
				return true;
			Cleanup();

			const auto [Hint, HintRevision] = ReadPlaybackHint();
			m_AppliedHintRevision = HintRevision;
			vCandidates = ResolveCaptureTargets(Hint);
			if(MusicPlayerDebugEnabled(1))
			{
				MusicPlayerDebugLog(1, "pulse", "candidate count=%d hint_service='%s' hint_state=%s",
					(int)vCandidates.size(), Hint.m_ServiceId.c_str(), MusicPlaybackStateName(Hint.m_PlaybackState));
				if(MusicPlayerDebugEnabled(2))
				{
					for(size_t i = 0; i < vCandidates.size(); ++i)
					{
						const auto &Candidate = vCandidates[i];
						MusicPlayerDebugLog(2, "pulse", "candidate[%d]: score=%d sink='%s' monitor='%s' app='%s' binary='%s' role='%s' hint_match=%d active_input=%d",
							(int)i, Candidate.m_Score, Candidate.m_SinkName.c_str(), Candidate.m_MonitorSource.c_str(),
							Candidate.m_AppName.c_str(), Candidate.m_AppBinary.c_str(), Candidate.m_MediaRole.c_str(),
							Candidate.m_MatchesPlaybackHint ? 1 : 0, Candidate.m_FromActiveSinkInput ? 1 : 0);
					}
				}
			}
			if(vCandidates.empty())
			{
				++m_ConsecutiveFailures;
				MusicPlayerDebugLog(1, "pulse", "resolve target failed: no viable candidates failures=%d", m_ConsecutiveFailures);
				StoreBackendState(m_ConsecutiveFailures >= 6 ? EMusicVisualizerBackendStatus::UNAVAILABLE : EMusicVisualizerBackendStatus::CONNECTING);
				return false;
			}

			for(CurrentCandidateIndex = 0; CurrentCandidateIndex < vCandidates.size(); ++CurrentCandidateIndex)
			{
				CurrentTarget = vCandidates[CurrentCandidateIndex];
				MusicPlayerDebugLog(1, "pulse", "trying candidate[%d]: score=%d sink='%s' monitor='%s' app='%s' binary='%s' role='%s'",
					(int)CurrentCandidateIndex, CurrentTarget.m_Score, CurrentTarget.m_SinkName.c_str(), CurrentTarget.m_MonitorSource.c_str(),
					CurrentTarget.m_AppName.c_str(), CurrentTarget.m_AppBinary.c_str(), CurrentTarget.m_MediaRole.c_str());

				pa_sample_spec Spec;
				Spec.format = PA_SAMPLE_FLOAT32LE;
				Spec.rate = maximum(8000, CurrentTarget.m_SampleRate);
				Spec.channels = CurrentTarget.m_Channels;

				pa_buffer_attr Attr;
				Attr.maxlength = (uint32_t)-1;
				Attr.tlength = (uint32_t)-1;
				Attr.prebuf = (uint32_t)-1;
				Attr.minreq = (uint32_t)-1;
				const size_t FramesPerRead = 1024;
				vReadBuffer.assign(FramesPerRead * Spec.channels, 0.0f);
				Attr.fragsize = (uint32_t)(sizeof(float) * vReadBuffer.size());

				int Error = 0;
				pSimple = pa_simple_new(nullptr, "DDNet", PA_STREAM_RECORD, CurrentTarget.m_MonitorSource.c_str(), "music visualizer", &Spec, nullptr, &Attr, &Error);
				if(pSimple == nullptr)
				{
					MusicPlayerDebugLog(1, "pulse", "open candidate failed: monitor='%s' error=%d", CurrentTarget.m_MonitorSource.c_str(), Error);
					continue;
				}

				CurrentSampleRate = Spec.rate;
				CurrentChannels = maximum(1, (int)Spec.channels);
				m_ConsecutiveFailures = 0;
				MusicPlayerDebugLog(1, "pulse", "capture probing: monitor='%s' rate=%d channels=%d", CurrentTarget.m_MonitorSource.c_str(), CurrentSampleRate, CurrentChannels);
				StoreBackendState(EMusicVisualizerBackendStatus::CONNECTING);
				NextReconnectCheckTick = time_get() + time_freq() * 2;
				return true;
			}

			++m_ConsecutiveFailures;
			StoreBackendState(m_ConsecutiveFailures >= 6 ? EMusicVisualizerBackendStatus::UNAVAILABLE : EMusicVisualizerBackendStatus::CONNECTING);
			return false;
		};

		StoreBackendState(EMusicVisualizerBackendStatus::CONNECTING);
		while(true)
		{
			{
				std::lock_guard<std::mutex> Lock(m_Mutex);
				if(m_Shutdown)
					break;
			}

			if(!EnsureCapture())
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(350));
				continue;
			}

			int Error = 0;
			if(pa_simple_read(pSimple, vReadBuffer.data(), vReadBuffer.size() * sizeof(float), &Error) < 0)
			{
				Cleanup();
				++m_ConsecutiveFailures;
				MusicPlayerDebugLog(1, "pulse", "capture read failed: error=%d failures=%d", Error, m_ConsecutiveFailures);
				StoreBackendState(m_ConsecutiveFailures >= 6 ? EMusicVisualizerBackendStatus::UNAVAILABLE : EMusicVisualizerBackendStatus::CONNECTING);
				std::this_thread::sleep_for(std::chrono::milliseconds(120));
				continue;
			}

			const int Channels = maximum(1, CurrentChannels);
			const size_t FramesRead = vReadBuffer.size() / Channels;
			for(size_t FrameIndex = 0; FrameIndex < FramesRead; ++FrameIndex)
			{
				float MonoSample = 0.0f;
				for(int Channel = 0; Channel < Channels; ++Channel)
					MonoSample += vReadBuffer[FrameIndex * Channels + Channel];
				MonoSample /= Channels;
				vSamples.push_back(std::clamp(MonoSample, -1.0f, 1.0f));
			}
			if(vSamples.size() > 8192)
				vSamples.erase(vSamples.begin(), vSamples.end() - 8192);

			SMusicVisualizerData Frame = AnalyzeVisualizerSamples(vSamples, CurrentSampleRate);
			const float Sensitivity = MusicPlayerVisualizerSensitivity();
			const bool NearSilence = Frame.m_Rms < 0.0048f / Sensitivity && Frame.m_Peak < 0.012f / Sensitivity;
			if(NearSilence)
			{
				Frame.m_HasRealtimeSignal = false;
				Frame.m_Peak = 0.0f;
				Frame.m_Rms = 0.0f;
				for(float &Bin : Frame.m_aBins)
					Bin = 0.0f;
			}
			if(NearSilence)
				SilentReads++;
			else
			{
				ValidatedReads++;
				SilentReads = 0;
			}

			if(!ValidatedLive && ValidatedReads >= 2)
			{
				ValidatedLive = true;
				MusicPlayerDebugLog(1, "pulse", "candidate validated: monitor='%s' sink='%s'", CurrentTarget.m_MonitorSource.c_str(), CurrentTarget.m_SinkName.c_str());
			}

			Frame.m_BackendStatus = ValidatedLive ? EMusicVisualizerBackendStatus::LIVE : EMusicVisualizerBackendStatus::SILENT;
			Frame.m_IsPassiveFallback = false;
			StoreFrame(Frame);
			static thread_local int64_t s_NextVerboseTick = 0;
			if(MusicPlayerDebugEnabled(2) && time_get() >= s_NextVerboseTick)
			{
				s_NextVerboseTick = time_get() + time_freq();
				MusicPlayerDebugLog(2, "pulse", "frame: signal=%d near_silence=%d peak=%.4f rms=%.4f bins=%s",
					Frame.m_HasRealtimeSignal ? 1 : 0, NearSilence ? 1 : 0, Frame.m_Peak, Frame.m_Rms, MusicVisualizerBinsSummary(Frame).c_str());
			}

			const int64_t Now = time_get();
			const bool PlaybackActive = ReadPlaybackHint().first.m_PlaybackState == EMusicPlaybackState::PLAYING;
			if(!ValidatedLive && PlaybackActive && SilentReads >= 8)
			{
				MusicPlayerDebugLog(1, "pulse", "candidate silent, rotating: monitor='%s' sink='%s' silent_reads=%d",
					CurrentTarget.m_MonitorSource.c_str(), CurrentTarget.m_SinkName.c_str(), SilentReads);
				Cleanup();
				StoreBackendState(EMusicVisualizerBackendStatus::CONNECTING);
				std::this_thread::sleep_for(std::chrono::milliseconds(80));
				continue;
			}

			if(NextReconnectCheckTick > 0 && Now >= NextReconnectCheckTick)
			{
				const auto [Hint, HintRevision] = ReadPlaybackHint();
				const std::vector<SPulseCaptureTarget> vResolvedTargets = ResolveCaptureTargets(Hint);
				NextReconnectCheckTick = Now + time_freq() * 2;
				if(HintRevision != m_AppliedHintRevision)
				{
					MusicPlayerDebugLog(1, "pulse", "playback hint changed, re-resolving candidates");
					Cleanup();
					StoreBackendState(EMusicVisualizerBackendStatus::CONNECTING);
					std::this_thread::sleep_for(std::chrono::milliseconds(80));
				}
				else if(!vResolvedTargets.empty() && vResolvedTargets.front().m_MonitorSource != CurrentTarget.m_MonitorSource)
				{
					MusicPlayerDebugLog(1, "pulse", "top candidate changed: old_monitor='%s' new_monitor='%s'",
						CurrentTarget.m_MonitorSource.c_str(), vResolvedTargets.front().m_MonitorSource.c_str());
					Cleanup();
					StoreBackendState(EMusicVisualizerBackendStatus::CONNECTING);
					std::this_thread::sleep_for(std::chrono::milliseconds(80));
				}
			}
		}

		Cleanup();
	}

public:
	CLinuxPulseVisualizer()
	{
		m_WorkerThread = std::thread([this]() { WorkerMain(); });
	}

	~CLinuxPulseVisualizer() override
	{
		{
			std::lock_guard<std::mutex> Lock(m_Mutex);
			m_Shutdown = true;
		}
		if(m_WorkerThread.joinable())
			m_WorkerThread.join();
	}

	bool Poll(SMusicVisualizerData &Out) override
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		Out = m_LatestFrame;
		return Out.m_BackendStatus == EMusicVisualizerBackendStatus::LIVE || Out.m_HasRealtimeSignal || Out.m_Rms > 0.0f || Out.m_Peak > 0.0f;
	}

	void SetPlaybackHint(const SMusicPlaybackHint &Hint) override
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		if(m_PlaybackHint.m_ServiceId != Hint.m_ServiceId || m_PlaybackHint.m_Title != Hint.m_Title ||
			m_PlaybackHint.m_Artist != Hint.m_Artist || m_PlaybackHint.m_PlaybackState != Hint.m_PlaybackState)
		{
			m_PlaybackHint = Hint;
			++m_HintRevision;
		}
	}
};
#endif

#if defined(CONF_FAMILY_WINDOWS) && BC_MUSICPLAYER_HAS_WINRT
class CWindowsLoopbackVisualizer final : public IMusicVisualizerSource
{
	std::thread m_WorkerThread;
	std::mutex m_Mutex;
	bool m_Shutdown = false;
	SMusicVisualizerData m_LatestFrame;

	struct SWaveFormatInfo
	{
		WAVEFORMATEX *m_pFormat = nullptr;
		int m_Channels = 0;
		int m_BitsPerSample = 0;
		bool m_Float = false;
	};

	static bool ExtractWaveFormatInfo(const WAVEFORMATEX *pFormat, SWaveFormatInfo &Out)
	{
		if(pFormat == nullptr || pFormat->nChannels == 0 || pFormat->wBitsPerSample == 0)
			return false;

		Out.m_pFormat = const_cast<WAVEFORMATEX *>(pFormat);
		Out.m_Channels = maximum<int>(1, pFormat->nChannels);
		Out.m_BitsPerSample = pFormat->wBitsPerSample;
		Out.m_Float = pFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
		if(pFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
		{
			const auto *pExt = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(pFormat);
			Out.m_Float = pExt->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
			Out.m_BitsPerSample = pExt->Samples.wValidBitsPerSample > 0 ? pExt->Samples.wValidBitsPerSample : pFormat->wBitsPerSample;
		}
		return true;
	}

	static void StoreMonoSamples(const BYTE *pData, UINT32 FrameCount, const SWaveFormatInfo &Format, std::vector<float> &vOut)
	{
		if(pData == nullptr || FrameCount == 0 || Format.m_Channels <= 0)
			return;

		const int Channels = Format.m_Channels;
		if(Format.m_Float && Format.m_BitsPerSample == 32)
		{
			const float *pSamples = reinterpret_cast<const float *>(pData);
			for(UINT32 Frame = 0; Frame < FrameCount; ++Frame)
			{
				float Sum = 0.0f;
				for(int Ch = 0; Ch < Channels; ++Ch)
					Sum += pSamples[Frame * Channels + Ch];
				vOut.push_back(std::clamp(Sum / Channels, -1.0f, 1.0f));
			}
			return;
		}

		if(!Format.m_Float && Format.m_BitsPerSample == 16)
		{
			const int16_t *pSamples = reinterpret_cast<const int16_t *>(pData);
			for(UINT32 Frame = 0; Frame < FrameCount; ++Frame)
			{
				float Sum = 0.0f;
				for(int Ch = 0; Ch < Channels; ++Ch)
					Sum += pSamples[Frame * Channels + Ch] / 32768.0f;
				vOut.push_back(std::clamp(Sum / Channels, -1.0f, 1.0f));
			}
			return;
		}

		if(!Format.m_Float && Format.m_BitsPerSample == 32)
		{
			const int32_t *pSamples = reinterpret_cast<const int32_t *>(pData);
			for(UINT32 Frame = 0; Frame < FrameCount; ++Frame)
			{
				float Sum = 0.0f;
				for(int Ch = 0; Ch < Channels; ++Ch)
					Sum += (float)(pSamples[Frame * Channels + Ch] / 2147483648.0);
				vOut.push_back(std::clamp(Sum / Channels, -1.0f, 1.0f));
			}
		}
	}

	void StoreFrame(const SMusicVisualizerData &Frame)
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		m_LatestFrame = Frame;
	}

	void WorkerMain()
	{
		const HRESULT CoInitResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		if(FAILED(CoInitResult))
			return;

		winrt::com_ptr<IMMDeviceEnumerator> pEnumerator;
		winrt::com_ptr<IMMDevice> pDevice;
		winrt::com_ptr<IAudioClient> pAudioClient;
		winrt::com_ptr<IAudioCaptureClient> pCaptureClient;
		WAVEFORMATEX *pFormat = nullptr;
		SWaveFormatInfo FormatInfo;
		UINT32 BufferFrameCount = 0;
		int SampleRate = 0;
		std::vector<float> vSamples;
		vSamples.reserve(4096);

		auto Cleanup = [&]() {
			if(pAudioClient)
				pAudioClient->Stop();
			pCaptureClient = nullptr;
			pAudioClient = nullptr;
			pDevice = nullptr;
			pEnumerator = nullptr;
			if(pFormat != nullptr)
			{
				CoTaskMemFree(pFormat);
				pFormat = nullptr;
			}
			FormatInfo = SWaveFormatInfo();
			BufferFrameCount = 0;
			SampleRate = 0;
			vSamples.clear();
		};

		auto EnsureCapture = [&]() -> bool {
			if(pCaptureClient && pAudioClient)
				return true;
			Cleanup();

			if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(pEnumerator.put()))))
				return false;
			if(FAILED(pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, pDevice.put())))
				return false;
			if(FAILED(pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, pAudioClient.put())))
				return false;
			if(FAILED(pAudioClient->GetMixFormat(&pFormat)))
				return false;
			if(!ExtractWaveFormatInfo(pFormat, FormatInfo))
				return false;
			SampleRate = maximum<int>(1, pFormat->nSamplesPerSec);
			if(FAILED(pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 0, 0, pFormat, nullptr)))
				return false;
			if(FAILED(pAudioClient->GetBufferSize(&BufferFrameCount)))
				return false;
			if(FAILED(pAudioClient->GetService(IID_PPV_ARGS(pCaptureClient.put()))))
				return false;
			if(FAILED(pAudioClient->Start()))
				return false;
			return true;
		};

		while(true)
		{
			{
				std::lock_guard<std::mutex> Lock(m_Mutex);
				if(m_Shutdown)
					break;
			}

			if(!EnsureCapture())
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(250));
				continue;
			}

			bool HadPacket = false;
			UINT32 PacketLength = 0;
			if(FAILED(pCaptureClient->GetNextPacketSize(&PacketLength)))
			{
				Cleanup();
				std::this_thread::sleep_for(std::chrono::milliseconds(80));
				continue;
			}

			while(PacketLength > 0)
			{
				BYTE *pData = nullptr;
				UINT32 NumFrames = 0;
				DWORD Flags = 0;
				if(FAILED(pCaptureClient->GetBuffer(&pData, &NumFrames, &Flags, nullptr, nullptr)))
				{
					Cleanup();
					break;
				}

				HadPacket = true;
				if((Flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0)
				{
					vSamples.insert(vSamples.end(), NumFrames, 0.0f);
				}
				else
				{
					StoreMonoSamples(pData, NumFrames, FormatInfo, vSamples);
				}
				pCaptureClient->ReleaseBuffer(NumFrames);

				if(vSamples.size() > 4096)
					vSamples.erase(vSamples.begin(), vSamples.end() - 4096);

				if(FAILED(pCaptureClient->GetNextPacketSize(&PacketLength)))
				{
					Cleanup();
					break;
				}
			}

			SMusicVisualizerData Frame = AnalyzeVisualizerSamples(vSamples, SampleRate);
			if(!HadPacket)
			{
				Frame.m_Peak *= 0.90f;
				Frame.m_Rms *= 0.88f;
				for(float &Bin : Frame.m_aBins)
					Bin *= 0.86f;
			}
			StoreFrame(Frame);
			std::this_thread::sleep_for(std::chrono::milliseconds(HadPacket ? 12 : 30));
		}

		Cleanup();
		CoUninitialize();
	}

public:
	CWindowsLoopbackVisualizer()
	{
		m_WorkerThread = std::thread([this]() { WorkerMain(); });
	}

	~CWindowsLoopbackVisualizer() override
	{
		{
			std::lock_guard<std::mutex> Lock(m_Mutex);
			m_Shutdown = true;
		}
		if(m_WorkerThread.joinable())
			m_WorkerThread.join();
	}

	bool Poll(SMusicVisualizerData &Out) override
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		Out = m_LatestFrame;
		return Out.m_HasRealtimeSignal || Out.m_Rms > 0.0f || Out.m_Peak > 0.0f;
	}
};
#endif

class CPassiveMusicVisualizerSource final : public IMusicVisualizerSource
{
public:
	bool Poll(SMusicVisualizerData &Out) override
	{
		Out = SMusicVisualizerData();
		Out.m_IsPassiveFallback = true;
		Out.m_BackendStatus = EMusicVisualizerBackendStatus::FALLBACK;
		return false;
	}
};

class CNullNowPlayingProvider final : public IMusicPlaybackProvider
{
public:
	bool Poll(SNowPlayingSnapshot &Out) override
	{
		Out = SNowPlayingSnapshot();
		return false;
	}

	void Previous() override {}
	void PlayPause() override {}
	void Next() override {}
};
} // namespace

namespace
{
struct SMusicPlayerPalette
{
	ColorRGBA m_Light = ColorRGBA(0.29f, 0.30f, 0.33f, 1.0f);
	ColorRGBA m_Mid = ColorRGBA(0.15f, 0.16f, 0.18f, 1.0f);
	ColorRGBA m_Dark = ColorRGBA(0.07f, 0.08f, 0.09f, 1.0f);
	ColorRGBA m_Glow = ColorRGBA(0.22f, 0.23f, 0.25f, 1.0f);
};

struct SArtworkColorAnalysis
{
	ColorRGBA m_Base = ColorRGBA(0.22f, 0.24f, 0.28f, 1.0f);
	ColorRGBA m_Dominant = ColorRGBA(0.22f, 0.24f, 0.28f, 1.0f);
	ColorRGBA m_Brightest = ColorRGBA(0.34f, 0.38f, 0.46f, 1.0f);
	float m_Luminance = 0.22f;
	float m_Saturation = 0.0f;
	bool m_Valid = false;
	bool m_Neutral = true;
};

struct SMusicPlayerMetrics
{
	float m_Scale = 1.0f;
	float m_WidthScale = 1.0f;
	float m_CompactW = 0.0f;
	float m_CompactH = 0.0f;
	float m_ExpandedW = 0.0f;
	float m_ExpandedH = 0.0f;
	float m_Rounding = 0.0f;
	CUIRect m_CompactRect{};
	CUIRect m_ExpandedRect{};
	CUIRect m_ViewRect{};
};

static float ComputeCompactTextSlotWidth(ITextRender *pTextRender, const SGameTimerDisplay &GameTimer, float TitleFont, float Scale, float WidthScale)
{
	if(pTextRender == nullptr)
		return 28.8f * Scale * WidthScale;

	const bool WideTimer = GameTimer.m_Valid && GameTimer.m_Text.size() > 5;
	const char *pReference = WideTimer ? "00:00:00" : "00:00";
	const float TextWidth = pTextRender->TextWidth(TitleFont, pReference, -1, -1.0f);
	const float Padding = (WideTimer ? 4.2f : 5.4f) * Scale * WidthScale;
	return TextWidth + Padding;
}

static float EaseInOutCubic(float t)
{
	t = std::clamp(t, 0.0f, 1.0f);
	return t < 0.5f ? 4.0f * t * t * t : 1.0f - powf(-2.0f * t + 2.0f, 3.0f) * 0.5f;
}

static float ApproachAnim(float Current, float Target, float Delta, float Speed)
{
	const float Step = 1.0f - expf(-maximum(0.0f, Speed) * maximum(0.0f, Delta));
	return mix(Current, Target, std::clamp(Step, 0.0f, 1.0f));
}

static ColorRGBA MixColor(const ColorRGBA &A, const ColorRGBA &B, float t)
{
	t = std::clamp(t, 0.0f, 1.0f);
	return ColorRGBA(
		mix(A.r, B.r, t),
		mix(A.g, B.g, t),
		mix(A.b, B.b, t),
		mix(A.a, B.a, t));
}

static ColorRGBA WithAlpha(ColorRGBA Color, float Alpha)
{
	Color.a = Alpha;
	return Color;
}

static float RelativeLuminance(const ColorRGBA &Color)
{
	return Color.r * 0.2126f + Color.g * 0.7152f + Color.b * 0.0722f;
}

static float ColorSaturation(const ColorRGBA &Color)
{
	const float MaxC = maximum(Color.r, maximum(Color.g, Color.b));
	const float MinC = minimum(Color.r, minimum(Color.g, Color.b));
	return MaxC > 0.0f ? (MaxC - MinC) / MaxC : 0.0f;
}

static SMusicPlayerPalette BuildPaletteFromAccent(ColorRGBA Accent);

static ColorRGBA ClampColor(const ColorRGBA &Color)
{
	return ColorRGBA(
		std::clamp(Color.r, 0.0f, 1.0f),
		std::clamp(Color.g, 0.0f, 1.0f),
		std::clamp(Color.b, 0.0f, 1.0f),
		std::clamp(Color.a, 0.0f, 1.0f));
}

static SMusicPlayerPalette DefaultMusicPlayerPalette()
{
	if(g_Config.m_BcMusicPlayerColorMode == 0)
		return BuildPaletteFromAccent(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcMusicPlayerStaticColor)));
	return SMusicPlayerPalette();
}

static ColorRGBA DesaturateColor(const ColorRGBA &Color, float Amount)
{
	const float Gray = RelativeLuminance(Color);
	return ClampColor(MixColor(Color, ColorRGBA(Gray, Gray, Gray, 1.0f), std::clamp(Amount, 0.0f, 1.0f)));
}

static ColorRGBA SetColorLuminance(ColorRGBA Color, float TargetLuma)
{
	Color.a = 1.0f;
	TargetLuma = std::clamp(TargetLuma, 0.0f, 1.0f);
	const float CurrentLuma = RelativeLuminance(Color);
	if(absolute(CurrentLuma - TargetLuma) < 0.001f)
		return ClampColor(Color);

	if(TargetLuma > CurrentLuma)
	{
		const float Blend = std::clamp((TargetLuma - CurrentLuma) / maximum(1.0f - CurrentLuma, 0.001f), 0.0f, 1.0f);
		return ClampColor(MixColor(Color, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), Blend));
	}

	const float Blend = std::clamp((CurrentLuma - TargetLuma) / maximum(CurrentLuma, 0.001f), 0.0f, 1.0f);
	return ClampColor(MixColor(Color, ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f), Blend));
}

static SMusicPlayerPalette BuildPaletteFromAnalysis(const SArtworkColorAnalysis &Analysis)
{
	if(!Analysis.m_Valid)
		return DefaultMusicPlayerPalette();

	SMusicPlayerPalette Palette;
	if(Analysis.m_Neutral)
	{
		const float Tone = std::clamp(mix(Analysis.m_Luminance, 0.16f, 0.52f), 0.11f, 0.27f);
		const ColorRGBA Base(Tone, Tone, Tone, 1.0f);
		Palette.m_Light = SetColorLuminance(Base, std::clamp(Tone + 0.10f, 0.18f, 0.34f));
		Palette.m_Mid = SetColorLuminance(Base, std::clamp(Tone * 0.82f, 0.10f, 0.20f));
		Palette.m_Dark = SetColorLuminance(Base, std::clamp(Tone * 0.46f, 0.05f, 0.10f));
		Palette.m_Glow = SetColorLuminance(Base, std::clamp(Tone + 0.03f, 0.16f, 0.28f));
		return Palette;
	}

	const bool DominantColorMode = g_Config.m_BcMusicPlayerColorMode == 2;
	const float BaseDesaturation = DominantColorMode ?
		(0.04f + (1.0f - Analysis.m_Saturation) * 0.10f) :
		(0.10f + (1.0f - Analysis.m_Saturation) * 0.18f);
	ColorRGBA Base = DesaturateColor(ClampColor(Analysis.m_Base), BaseDesaturation);
	const float LightLuma = std::clamp((DominantColorMode ? 0.24f : 0.26f) + Analysis.m_Saturation * (DominantColorMode ? 0.11f : 0.12f), 0.20f, 0.38f);
	const float MidLuma = std::clamp(LightLuma * (DominantColorMode ? 0.48f : 0.56f), 0.10f, 0.21f);
	const float DarkLuma = std::clamp(MidLuma * (DominantColorMode ? 0.36f : 0.44f), 0.04f, 0.10f);
	Palette.m_Light = SetColorLuminance(Base, LightLuma);
	Palette.m_Mid = SetColorLuminance(Base, MidLuma);
	Palette.m_Dark = SetColorLuminance(Base, DarkLuma);
	Palette.m_Glow = SetColorLuminance(DesaturateColor(Base, DominantColorMode ? 0.12f : 0.18f), std::clamp(LightLuma + 0.03f, 0.22f, 0.38f));
	return Palette;
}

static SMusicPlayerPalette BuildPaletteFromAccent(ColorRGBA Accent)
{
	SArtworkColorAnalysis Analysis;
	Analysis.m_Base = ClampColor(Accent);
	Analysis.m_Luminance = RelativeLuminance(Analysis.m_Base);
	Analysis.m_Saturation = ColorSaturation(Analysis.m_Base);
	Analysis.m_Valid = true;
	Analysis.m_Neutral = Analysis.m_Saturation < 0.11f;
	return BuildPaletteFromAnalysis(Analysis);
}

static ColorRGBA DefaultPreviewAccentForColorMode(int ColorMode)
{
	if(ColorMode == 2)
		return ColorRGBA(0.18f, 0.68f, 0.52f, 1.0f);
	return ColorRGBA(0.34f, 0.53f, 0.79f, 1.0f);
}

static ColorRGBA SelectMusicPlayerAccent(const SArtworkColorAnalysis &Analysis)
{
	if(g_Config.m_BcMusicPlayerColorMode == 0)
		return color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcMusicPlayerStaticColor));
	if(!Analysis.m_Valid)
		return DefaultPreviewAccentForColorMode(g_Config.m_BcMusicPlayerColorMode);
	if(g_Config.m_BcMusicPlayerColorMode == 2)
		return Analysis.m_Dominant;
	return Analysis.m_Base;
}

static SArtworkColorAnalysis AnalyzeArtworkBaseColor(const CImageInfo &Image)
{
	struct SBin
	{
		float m_Weight = 0.0f;
		float m_R = 0.0f;
		float m_G = 0.0f;
		float m_B = 0.0f;
		float m_Luma = 0.0f;
		float m_Saturation = 0.0f;
	};

	if(Image.m_pData == nullptr || Image.m_Width == 0 || Image.m_Height == 0)
		return SArtworkColorAnalysis();

	constexpr int QUANT = 6;
	std::array<SBin, QUANT * QUANT * QUANT> aBins{};
	float TotalWeight = 0.0f;
	float AvgR = 0.0f;
	float AvgG = 0.0f;
	float AvgB = 0.0f;
	float AvgLuma = 0.0f;
	float AvgSaturation = 0.0f;

	const size_t SampleBudget = 4096;
	const size_t PixelCount = Image.m_Width * Image.m_Height;
	const size_t Step = maximum<size_t>(1, (size_t)std::sqrt((double)maximum<size_t>(1, PixelCount / SampleBudget)));

	for(size_t y = 0; y < Image.m_Height; y += Step)
	{
		for(size_t x = 0; x < Image.m_Width; x += Step)
		{
			ColorRGBA Pixel = Image.PixelColor(x, y);
			if(Pixel.a < 0.08f)
				continue;

			const float Value = maximum(Pixel.r, maximum(Pixel.g, Pixel.b));
			const float Saturation = ColorSaturation(Pixel);
			const float Luma = RelativeLuminance(Pixel);
			float Weight = Pixel.a;
			Weight *= 0.55f + 0.45f * std::clamp(Value, 0.0f, 0.95f);
			Weight *= 0.82f + 0.38f * Saturation;
			if(Value > 0.96f && Saturation < 0.08f)
				Weight *= 0.08f;
			if(Value < 0.05f && Saturation > 0.25f)
				Weight *= 0.45f;
			if(Saturation < 0.06f && Luma > 0.78f)
				Weight *= 0.14f;
			if(Weight <= 0.0f)
				continue;

			const int R = std::clamp(round_to_int(Pixel.r * (QUANT - 1)), 0, QUANT - 1);
			const int G = std::clamp(round_to_int(Pixel.g * (QUANT - 1)), 0, QUANT - 1);
			const int B = std::clamp(round_to_int(Pixel.b * (QUANT - 1)), 0, QUANT - 1);
			SBin &Bin = aBins[(R * QUANT + G) * QUANT + B];
			Bin.m_Weight += Weight;
			Bin.m_R += Pixel.r * Weight;
			Bin.m_G += Pixel.g * Weight;
			Bin.m_B += Pixel.b * Weight;
			Bin.m_Luma += Luma * Weight;
			Bin.m_Saturation += Saturation * Weight;

			TotalWeight += Weight;
			AvgR += Pixel.r * Weight;
			AvgG += Pixel.g * Weight;
			AvgB += Pixel.b * Weight;
			AvgLuma += Luma * Weight;
			AvgSaturation += Saturation * Weight;
		}
	}

	if(TotalWeight <= 0.0f)
		return SArtworkColorAnalysis();

	const SBin *pDominantBin = nullptr;
	float DominantScore = -1.0f;
	const SBin *pBrightestBin = nullptr;
	float BrightestScore = -1.0f;
	const SBin *pBestBin = nullptr;
	float BestScore = -1.0f;
	const SBin *pBestVividBin = nullptr;
	float BestVividScore = -1.0f;
	for(const SBin &Bin : aBins)
	{
		if(Bin.m_Weight <= 0.0f)
			continue;
		const float BinLuma = Bin.m_Luma / Bin.m_Weight;
		const float BinSaturation = Bin.m_Saturation / Bin.m_Weight;
		const float BinR = Bin.m_R / Bin.m_Weight;
		const float BinG = Bin.m_G / Bin.m_Weight;
		const float BinB = Bin.m_B / Bin.m_Weight;
		const float BinValue = maximum(BinR, maximum(BinG, BinB));
		const float CommonScore = Bin.m_Weight * (0.96f + BinSaturation * 0.16f);
		if(pDominantBin == nullptr || CommonScore > DominantScore)
		{
			pDominantBin = &Bin;
			DominantScore = CommonScore;
		}
		float BrightScore = Bin.m_Weight * (0.22f + BinLuma * 1.75f + BinValue * 0.95f + BinSaturation * 0.90f);
		if(BinSaturation < 0.06f)
			BrightScore *= 0.45f;
		if(BinValue < 0.12f)
			BrightScore *= 0.18f;
		if(pBrightestBin == nullptr || BrightScore > BrightestScore)
		{
			pBrightestBin = &Bin;
			BrightestScore = BrightScore;
		}
		float Score = Bin.m_Weight * (0.60f + BinSaturation * 1.05f + BinValue * 0.22f + (1.0f - absolute(BinLuma - 0.30f)) * 0.20f);
		if(BinValue < 0.10f)
			Score *= 0.22f;
		else if(BinValue < 0.16f)
			Score *= 0.55f;
		if(pBestBin == nullptr || Score > BestScore)
		{
			pBestBin = &Bin;
			BestScore = Score;
		}

		if(BinSaturation >= 0.16f && BinValue >= 0.14f)
		{
			float VividScore = Bin.m_Weight * (0.26f + BinSaturation * 1.85f + BinValue * 0.72f + (1.0f - absolute(BinLuma - 0.34f)) * 0.22f);
			if(BinValue < 0.18f)
				VividScore *= std::clamp((BinValue - 0.10f) / 0.08f, 0.35f, 1.0f);
			if(pBestVividBin == nullptr || VividScore > BestVividScore)
			{
				pBestVividBin = &Bin;
				BestVividScore = VividScore;
			}
		}
	}

	SArtworkColorAnalysis Analysis;
	Analysis.m_Base = ColorRGBA(
		AvgR / TotalWeight,
		AvgG / TotalWeight,
		AvgB / TotalWeight,
		1.0f);
	Analysis.m_Luminance = AvgLuma / TotalWeight;
	Analysis.m_Saturation = AvgSaturation / TotalWeight;
	Analysis.m_Valid = true;
	if(pDominantBin != nullptr)
	{
		Analysis.m_Dominant = ColorRGBA(
			pDominantBin->m_R / pDominantBin->m_Weight,
			pDominantBin->m_G / pDominantBin->m_Weight,
			pDominantBin->m_B / pDominantBin->m_Weight,
			1.0f);
	}
	if(pBrightestBin != nullptr)
	{
		Analysis.m_Brightest = ColorRGBA(
			pBrightestBin->m_R / pBrightestBin->m_Weight,
			pBrightestBin->m_G / pBrightestBin->m_Weight,
			pBrightestBin->m_B / pBrightestBin->m_Weight,
			1.0f);
	}

	float SelectedScore = BestScore;
	if(pBestVividBin != nullptr && (BestVividScore > TotalWeight * 0.018f || BestVividScore > BestScore * 0.18f))
	{
		pBestBin = pBestVividBin;
		SelectedScore = BestVividScore;
	}

	if(pBestBin != nullptr && SelectedScore > TotalWeight * 0.08f)
	{
		Analysis.m_Base = ColorRGBA(
			pBestBin->m_R / pBestBin->m_Weight,
			pBestBin->m_G / pBestBin->m_Weight,
			pBestBin->m_B / pBestBin->m_Weight,
			1.0f);
		Analysis.m_Luminance = pBestBin->m_Luma / pBestBin->m_Weight;
		Analysis.m_Saturation = pBestBin->m_Saturation / pBestBin->m_Weight;
	}

	Analysis.m_Base = ClampColor(Analysis.m_Base);
	Analysis.m_Dominant = ClampColor(Analysis.m_Dominant);
	Analysis.m_Brightest = ClampColor(Analysis.m_Brightest);
	Analysis.m_Neutral = Analysis.m_Saturation < 0.11f;
	if(Analysis.m_Neutral)
	{
		const float Tone = std::clamp(Analysis.m_Luminance, 0.06f, 0.26f);
		Analysis.m_Base = ColorRGBA(Tone, Tone, Tone, 1.0f);
	}
	return Analysis;
}

static CUIRect MakeMusicPlayerRect(float BaseX, float BaseY, float ExpandedW, float Width, float Height, float W, float H)
{
	CUIRect Rect;
	Rect.w = W;
	Rect.h = H;
	Rect.x = std::clamp(BaseX + (ExpandedW - W) * 0.5f, 0.0f, maximum(0.0f, Width - W));
	Rect.y = std::clamp(BaseY, 0.0f, maximum(0.0f, Height - H));
	return Rect;
}

static SMusicPlayerMetrics ComputeMusicPlayerMetrics(const HudLayout::SModuleLayout &Layout, float Width, float Height, float SizeT, float CompactTextSlotWidth)
{
	SMusicPlayerMetrics Metrics;
	Metrics.m_Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	Metrics.m_WidthScale = Width / maximum(HudLayout::CANVAS_WIDTH, 0.001f);
	Metrics.m_ExpandedW = 104.0f * Metrics.m_Scale * Metrics.m_WidthScale;
	Metrics.m_ExpandedH = 25.0f * Metrics.m_Scale;
	Metrics.m_CompactH = 15.5f * Metrics.m_Scale;
	const float CompactArtSize = minimum(Metrics.m_CompactH - 3.0f * Metrics.m_Scale, 11.6f * Metrics.m_Scale);
	const float CompactVisualW = 9.8f * Metrics.m_Scale * Metrics.m_WidthScale;
	const float CompactOuterPad = 2.5f * Metrics.m_Scale * Metrics.m_WidthScale;
	const float CompactInnerGap = 1.15f * Metrics.m_Scale * Metrics.m_WidthScale;
	Metrics.m_CompactW = CompactOuterPad * 2.0f + CompactArtSize + CompactVisualW + CompactTextSlotWidth + CompactInnerGap * 2.0f;
	Metrics.m_CompactRect = MakeMusicPlayerRect(Layout.m_X, Layout.m_Y, Metrics.m_ExpandedW, Width, Height, Metrics.m_CompactW, Metrics.m_CompactH);
	Metrics.m_ExpandedRect = MakeMusicPlayerRect(Layout.m_X, Layout.m_Y, Metrics.m_ExpandedW, Width, Height, Metrics.m_ExpandedW, Metrics.m_ExpandedH);
	Metrics.m_ViewRect = MakeMusicPlayerRect(Layout.m_X, Layout.m_Y, Metrics.m_ExpandedW, Width, Height, mix(Metrics.m_CompactW, Metrics.m_ExpandedW, SizeT), mix(Metrics.m_CompactH, Metrics.m_ExpandedH, SizeT));
	Metrics.m_Rounding = minimum(5.0f * Metrics.m_Scale, Metrics.m_ViewRect.h * 0.24f);
	return Metrics;
}

static bool IsPointInsideRect(const CUIRect &Rect, vec2 Pos, float Margin = 0.0f)
{
	return Pos.x >= Rect.x - Margin && Pos.x <= Rect.x + Rect.w + Margin &&
		Pos.y >= Rect.y - Margin && Pos.y <= Rect.y + Rect.h + Margin;
}

static bool RectsOverlap(const CUIRect &A, const CUIRect &B, float Padding = 0.0f)
{
	return A.x - Padding < B.x + B.w &&
		A.x + A.w + Padding > B.x &&
		A.y - Padding < B.y + B.h &&
		A.y + A.h + Padding > B.y;
}

static float RoundedArtInset(float LocalX, float W, float Radius)
{
	if(Radius <= 0.0f || W <= 0.0f)
		return 0.0f;

	if(LocalX < Radius)
	{
		const float dx = Radius - LocalX;
		return Radius - sqrtf(maximum(0.0f, Radius * Radius - dx * dx));
	}
	if(LocalX > W - Radius)
	{
		const float x = LocalX - (W - Radius);
		return Radius - sqrtf(maximum(0.0f, Radius * Radius - x * x));
	}
	return 0.0f;
}

static void DrawRoundedTexture(IGraphics *pGraphics, IGraphics::CTextureHandle Texture, const CUIRect &Rect, float Rounding, int TextureWidth, int TextureHeight)
{
	if(pGraphics == nullptr || !Texture.IsValid() || Rect.w <= 0.0f || Rect.h <= 0.0f)
		return;

	const float Radius = minimum(minimum(Rounding, minimum(Rect.w, Rect.h) * 0.5f), 64.0f);
	constexpr int NUM_SLICES = 32;
	float U0 = 0.0f;
	float U1 = 1.0f;
	float V0 = 0.0f;
	float V1 = 1.0f;
	if(TextureWidth > 0 && TextureHeight > 0)
	{
		if(TextureWidth > TextureHeight)
		{
			const float Visible = TextureHeight / (float)TextureWidth;
			const float Crop = (1.0f - Visible) * 0.5f;
			U0 = Crop;
			U1 = 1.0f - Crop;
		}
		else if(TextureHeight > TextureWidth)
		{
			const float Visible = TextureWidth / (float)TextureHeight;
			const float Crop = (1.0f - Visible) * 0.5f;
			V0 = Crop;
			V1 = 1.0f - Crop;
		}
	}
	const float ExtraCrop = 0.06f;
	const float OriginalU0 = U0;
	const float OriginalU1 = U1;
	const float OriginalV0 = V0;
	const float OriginalV1 = V1;
	U0 = mix(OriginalU0, OriginalU1, ExtraCrop);
	U1 = mix(OriginalU1, OriginalU0, ExtraCrop);
	V0 = mix(OriginalV0, OriginalV1, ExtraCrop);
	V1 = mix(OriginalV1, OriginalV0, ExtraCrop);

	pGraphics->TextureSet(Texture);
	pGraphics->QuadsBegin();
	pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	for(int i = 0; i < NUM_SLICES; ++i)
	{
		const float SliceT0 = i / (float)NUM_SLICES;
		const float SliceT1 = (i + 1) / (float)NUM_SLICES;
		const float u0 = mix(U0, U1, SliceT0);
		const float u1 = mix(U0, U1, SliceT1);
		const float LocalX0 = Rect.w * SliceT0;
		const float LocalX1 = Rect.w * SliceT1;
		const float Inset0 = RoundedArtInset(LocalX0, Rect.w, Radius);
		const float Inset1 = RoundedArtInset(LocalX1, Rect.w, Radius);
		const float RenderX0 = Rect.x + LocalX0;
		const float RenderX1 = Rect.x + LocalX1;

		const vec2 TopLeft(RenderX0, Rect.y + Inset0);
		const vec2 TopRight(RenderX1, Rect.y + Inset1);
		const vec2 BottomLeft(RenderX0, Rect.y + Rect.h - Inset0);
		const vec2 BottomRight(RenderX1, Rect.y + Rect.h - Inset1);

		const float RenderV0Top = std::clamp((TopLeft.y - Rect.y) / maximum(Rect.h, 0.001f), 0.0f, 1.0f);
		const float RenderV1Top = std::clamp((TopRight.y - Rect.y) / maximum(Rect.h, 0.001f), 0.0f, 1.0f);
		const float RenderV0Bottom = std::clamp((BottomLeft.y - Rect.y) / maximum(Rect.h, 0.001f), 0.0f, 1.0f);
		const float RenderV1Bottom = std::clamp((BottomRight.y - Rect.y) / maximum(Rect.h, 0.001f), 0.0f, 1.0f);
		const float v0Top = mix(V0, V1, RenderV0Top);
		const float v1Top = mix(V0, V1, RenderV1Top);
		const float v0Bottom = mix(V0, V1, RenderV0Bottom);
		const float v1Bottom = mix(V0, V1, RenderV1Bottom);

		pGraphics->QuadsSetSubsetFree(u0, v0Top, u1, v1Top, u0, v0Bottom, u1, v1Bottom);
		const IGraphics::CFreeformItem Item(TopLeft, TopRight, BottomLeft, BottomRight);
		pGraphics->QuadsDrawFreeform(&Item, 1);
	}
	pGraphics->QuadsEnd();
	pGraphics->TextureClear();
}

static void DrawRoundedFallbackArt(IGraphics *pGraphics, const CUIRect &Rect, const SMusicPlayerPalette &Palette, float HoverT, float Scale, float Rounding)
{
	if(pGraphics == nullptr || Rect.w <= 0.0f || Rect.h <= 0.0f)
		return;

	const float Radius = minimum(minimum(Rounding, minimum(Rect.w, Rect.h) * 0.5f), 64.0f);
	const ColorRGBA Base = WithAlpha(MixColor(Palette.m_Mid, Palette.m_Dark, 0.34f), 1.0f);
	const ColorRGBA Inner = WithAlpha(MixColor(Palette.m_Light, Palette.m_Mid, 0.78f), 0.92f);
	const ColorRGBA Highlight = WithAlpha(MixColor(Palette.m_Light, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), 0.45f), 0.20f + 0.10f * HoverT);

	pGraphics->DrawRect(Rect.x, Rect.y, Rect.w, Rect.h, Base, IGraphics::CORNER_ALL, Radius);
	pGraphics->DrawRect(Rect.x + 1.5f * Scale, Rect.y + 1.5f * Scale, maximum(0.0f, Rect.w - 3.0f * Scale), maximum(0.0f, Rect.h - 3.0f * Scale), Inner, IGraphics::CORNER_ALL, maximum(0.0f, Radius - 1.5f * Scale));

	CUIRect HighlightRect = Rect;
	HighlightRect.x += Rect.w * 0.18f;
	HighlightRect.y += Rect.h * 0.16f;
	HighlightRect.w *= 0.42f;
	HighlightRect.h *= 0.26f;
	pGraphics->DrawRect(HighlightRect.x, HighlightRect.y, HighlightRect.w, HighlightRect.h, Highlight, IGraphics::CORNER_ALL, minimum(HighlightRect.w, HighlightRect.h) * 0.5f);
}
} // namespace

class CMusicPlayerArtDecodeJob : public IJob
{
	IGraphics *m_pGraphics;
	std::vector<unsigned char> m_vData;
	char m_aContextName[512];
	SMediaDecodedFrames m_DecodedFrames;
	bool m_Success = false;

protected:
	void Run() override
	{
		if(State() == IJob::STATE_ABORTED || m_vData.empty())
			return;

		m_Success = MediaDecoder::DecodeStaticImageCpu(m_pGraphics, m_vData.data(), m_vData.size(), m_aContextName, m_DecodedFrames, MUSIC_ART_MAX_DIMENSION);
		if(!m_Success)
		{
			SMediaDecodeLimits Limits;
			Limits.m_MaxDimension = MUSIC_ART_MAX_DIMENSION;
			Limits.m_MaxFrames = MUSIC_ART_MAX_FRAMES;
			Limits.m_MaxTotalBytes = 12ull * 1024ull * 1024ull;
			Limits.m_MaxAnimationDurationMs = 10000;
			Limits.m_DecodeAllFrames = true;
			m_Success = MediaDecoder::DecodeImageWithFfmpegCpu(m_pGraphics, m_vData.data(), m_vData.size(), m_aContextName, m_DecodedFrames, Limits);
		}

		if(State() == IJob::STATE_ABORTED)
		{
			m_Success = false;
			m_DecodedFrames.Free();
		}
	}

public:
	CMusicPlayerArtDecodeJob(IGraphics *pGraphics, const unsigned char *pData, size_t DataSize, const char *pContextName) :
		m_pGraphics(pGraphics)
	{
		Abortable(true);
		if(pData != nullptr && DataSize > 0)
			m_vData.assign(pData, pData + DataSize);
		str_copy(m_aContextName, pContextName ? pContextName : "", sizeof(m_aContextName));
	}

	~CMusicPlayerArtDecodeJob() override
	{
		m_DecodedFrames.Free();
	}

	bool Success() const { return m_Success; }
	SMediaDecodedFrames &DecodedFrames() { return m_DecodedFrames; }
};

class CMusicPlayer::CImpl
{
public:
	static constexpr int VISUALIZER_BARS = MUSIC_PLAYER_VISUALIZER_BINS;

	std::unique_ptr<IMusicPlaybackProvider> m_pProvider;
	std::unique_ptr<IMusicVisualizerSource> m_pVisualizer;
	SNowPlayingSnapshot m_Snapshot;
	SMusicVisualizerData m_VisualizerData;
	int64_t m_LastSnapshotTick = 0;
	int64_t m_LastPollTick = 0;
	float m_ExpandAnim = 0.0f;
	float m_HoverAnim = 0.0f;
	float m_VisualPositionMs = 0.0f;
	std::string m_VisualTrackKey;
	std::string m_PlaybackTrackKey;
	int64_t m_PlaybackAnchorPositionMs = 0;
	int64_t m_PlaybackAnchorTick = 0;
	EMusicPlaybackState m_PlaybackAnchorState = EMusicPlaybackState::STOPPED;
	std::string m_LastArtKey;
	std::shared_ptr<CHttpRequest> m_pArtRequest;
	std::shared_ptr<CMusicPlayerArtDecodeJob> m_pArtDecodeJob;
	std::optional<SMediaDecodedFrames> m_OptArtDecodedFrames;
	int m_ArtUploadIndex = 0;
	std::vector<SMediaFrame> m_vArtFrames;
	bool m_ArtAnimated = false;
	int m_ArtWidth = 0;
	int m_ArtHeight = 0;
	int64_t m_ArtAnimationStart = 0;
	std::array<float, VISUALIZER_BARS> m_aVisualizerLevels{};
	float m_CompactTextSlotWidthAnim = 0.0f;
	CMusicPlayer::SHudReservation m_HudReservation;
	SMusicPlayerPalette m_Palette = DefaultMusicPlayerPalette();
	bool m_DebugLastProviderValid = false;
	std::string m_DebugLastProviderTrackKey;
	EMusicPlaybackState m_DebugLastProviderPlaybackState = EMusicPlaybackState::STOPPED;
	EMusicVisualizerBackendStatus m_DebugLastVisualizerBackendStatus = EMusicVisualizerBackendStatus::FALLBACK;
	bool m_DebugLastVisualizerSignal = false;
	int64_t m_DebugNextVisualizerVerboseTick = 0;
	std::string m_DebugLastRenderPath;
	int64_t m_DebugNextRenderVerboseTick = 0;

	CImpl()
	{
		m_aVisualizerLevels.fill(0.18f);
		m_VisualizerData.m_IsPassiveFallback = true;
		m_VisualizerData.m_BackendStatus = EMusicVisualizerBackendStatus::FALLBACK;
#if defined(CONF_PLATFORM_LINUX)
#if BC_MUSICPLAYER_HAS_DBUS
		m_pProvider = std::make_unique<CLinuxNowPlayingProvider>();
#else
		m_pProvider = std::make_unique<CNullNowPlayingProvider>();
#endif
#if BC_MUSICPLAYER_HAS_PULSE
		m_pVisualizer = std::make_unique<CLinuxPulseVisualizer>();
#else
		m_pVisualizer = std::make_unique<CPassiveMusicVisualizerSource>();
#endif
#elif defined(CONF_FAMILY_WINDOWS) && BC_MUSICPLAYER_HAS_WINRT
		m_pProvider = std::make_unique<CWindowsNowPlayingProvider>();
		m_pVisualizer = std::make_unique<CWindowsLoopbackVisualizer>();
#else
		m_pProvider = std::make_unique<CNullNowPlayingProvider>();
		m_pVisualizer = std::make_unique<CPassiveMusicVisualizerSource>();
#endif
	}

	void ResetHudState()
	{
		m_ExpandAnim = 0.0f;
		m_HoverAnim = 0.0f;
		m_VisualPositionMs = 0.0f;
		m_VisualTrackKey.clear();
		m_aVisualizerLevels.fill(0.18f);
		m_CompactTextSlotWidthAnim = 0.0f;
		m_HudReservation = CMusicPlayer::SHudReservation();
		m_DebugLastRenderPath.clear();
		m_DebugNextRenderVerboseTick = 0;
	}

	bool IsIdle() const
	{
		return !m_Snapshot.m_Valid &&
			m_LastSnapshotTick == 0 &&
			m_LastPollTick == 0 &&
			m_LastArtKey.empty() &&
			!m_pArtRequest &&
			!m_pArtDecodeJob &&
			!m_OptArtDecodedFrames.has_value() &&
			m_vArtFrames.empty() &&
			!m_HudReservation.m_Visible &&
			!m_HudReservation.m_Active;
	}

	void ResetPlaybackAnchor()
	{
		m_PlaybackTrackKey.clear();
		m_PlaybackAnchorPositionMs = 0;
		m_PlaybackAnchorTick = 0;
		m_PlaybackAnchorState = EMusicPlaybackState::STOPPED;
		m_VisualTrackKey.clear();
		m_VisualPositionMs = 0.0f;
	}

	void DebugLogProviderSnapshot(const SNowPlayingSnapshot &Snapshot)
	{
		if(!MusicPlayerDebugEnabled(1))
			return;

		const std::string TrackKey = Snapshot.m_Valid ? BuildSnapshotTrackKey(Snapshot) : std::string();
		const bool Changed =
			Snapshot.m_Valid != m_DebugLastProviderValid ||
			(Snapshot.m_Valid && (TrackKey != m_DebugLastProviderTrackKey || Snapshot.m_PlaybackState != m_DebugLastProviderPlaybackState));
		if(Changed)
		{
			if(Snapshot.m_Valid)
			{
				MusicPlayerDebugLog(1, "provider", "snapshot valid: service='%s' title='%s' artist='%s' state=%s visualizer=%d",
					Snapshot.m_ServiceId.c_str(), Snapshot.m_Title.c_str(), Snapshot.m_Artist.c_str(),
					MusicPlaybackStateName(Snapshot.m_PlaybackState), Snapshot.m_HasVisualizer ? 1 : 0);
			}
			else
			{
				MusicPlayerDebugLog(1, "provider", "snapshot invalid");
			}
		}
		m_DebugLastProviderValid = Snapshot.m_Valid;
		m_DebugLastProviderTrackKey = TrackKey;
		m_DebugLastProviderPlaybackState = Snapshot.m_PlaybackState;
	}

	void DebugLogProviderPollFailure(bool UsedGrace)
	{
		MusicPlayerDebugLog(1, "provider", "poll failed: stale_snapshot_grace=%d snapshot_valid=%d",
			UsedGrace ? 1 : 0, m_Snapshot.m_Valid ? 1 : 0);
	}

	void DebugLogVisualizerState(const char *pSource)
	{
		if(!MusicPlayerDebugEnabled(1))
			return;

		const bool StateChanged =
			m_VisualizerData.m_BackendStatus != m_DebugLastVisualizerBackendStatus ||
			m_VisualizerData.m_HasRealtimeSignal != m_DebugLastVisualizerSignal;
		if(StateChanged)
		{
			MusicPlayerDebugLog(1, "visualizer", "%s: backend=%s signal=%d passive=%d peak=%.4f rms=%.4f bins=%s",
				pSource,
				MusicVisualizerBackendStatusName(m_VisualizerData.m_BackendStatus),
				m_VisualizerData.m_HasRealtimeSignal ? 1 : 0,
				m_VisualizerData.m_IsPassiveFallback ? 1 : 0,
				m_VisualizerData.m_Peak,
				m_VisualizerData.m_Rms,
				MusicVisualizerBinsSummary(m_VisualizerData).c_str());
		}
		if(MusicPlayerDebugEnabled(2) && time_get() >= m_DebugNextVisualizerVerboseTick)
		{
			m_DebugNextVisualizerVerboseTick = time_get() + time_freq();
			MusicPlayerDebugLog(2, "visualizer", "%s periodic: backend=%s signal=%d peak=%.4f rms=%.4f bins=%s",
				pSource,
				MusicVisualizerBackendStatusName(m_VisualizerData.m_BackendStatus),
				m_VisualizerData.m_HasRealtimeSignal ? 1 : 0,
				m_VisualizerData.m_Peak,
				m_VisualizerData.m_Rms,
				MusicVisualizerBinsSummary(m_VisualizerData).c_str());
		}

		m_DebugLastVisualizerBackendStatus = m_VisualizerData.m_BackendStatus;
		m_DebugLastVisualizerSignal = m_VisualizerData.m_HasRealtimeSignal;
	}

	void DebugLogRenderDecision(const char *pPath, const SNowPlayingSnapshot &Snapshot)
	{
		if(!MusicPlayerDebugEnabled(1))
			return;

		const bool Changed = m_DebugLastRenderPath != pPath;
		if(Changed)
		{
			MusicPlayerDebugLog(1, "render", "path=%s playback=%s has_music=%d backend=%s signal=%d",
				pPath,
				MusicPlaybackStateName(Snapshot.m_PlaybackState),
				Snapshot.m_PlaybackState == EMusicPlaybackState::PLAYING ? 1 : 0,
				MusicVisualizerBackendStatusName(Snapshot.m_Visualizer.m_BackendStatus),
				Snapshot.m_Visualizer.m_HasRealtimeSignal ? 1 : 0);
			m_DebugLastRenderPath = pPath;
		}
		if(MusicPlayerDebugEnabled(2) && time_get() >= m_DebugNextRenderVerboseTick)
		{
			m_DebugNextRenderVerboseTick = time_get() + time_freq();
			MusicPlayerDebugLog(2, "render", "path=%s playback=%s peak=%.4f rms=%.4f",
				pPath,
				MusicPlaybackStateName(Snapshot.m_PlaybackState),
				Snapshot.m_Visualizer.m_Peak,
				Snapshot.m_Visualizer.m_Rms);
		}
	}

	void UpdatePaletteFromBytes(IGraphics *pGraphics, const unsigned char *pData, size_t DataSize, const char *pContextName)
	{
		m_Palette = DefaultMusicPlayerPalette();
		CImageInfo Image;
		if(!MediaDecoder::DecodeImageToRgba(pGraphics, pData, DataSize, pContextName, Image))
			return;

		const SArtworkColorAnalysis Analysis = AnalyzeArtworkBaseColor(Image);
		m_Palette = BuildPaletteFromAccent(SelectMusicPlayerAccent(Analysis));
		Image.Free();
	}

	void ResetArtwork(IGraphics *pGraphics)
	{
		if(m_pArtRequest)
		{
			m_pArtRequest->Abort();
			m_pArtRequest.reset();
		}
		if(m_pArtDecodeJob)
		{
			m_pArtDecodeJob->Abort();
			m_pArtDecodeJob = nullptr;
		}
		m_OptArtDecodedFrames.reset();
		m_ArtUploadIndex = 0;
		m_LastArtKey.clear();
		MediaDecoder::UnloadFrames(pGraphics, m_vArtFrames);
		m_ArtAnimated = false;
		m_ArtWidth = 0;
		m_ArtHeight = 0;
		m_ArtAnimationStart = 0;
		m_Palette = DefaultMusicPlayerPalette();
	}

	void Reset(IGraphics *pGraphics)
	{
		m_Snapshot = SNowPlayingSnapshot();
		m_VisualizerData = SMusicVisualizerData();
		m_LastSnapshotTick = 0;
		m_LastPollTick = 0;
		ResetHudState();
		ResetPlaybackAnchor();
		ResetArtwork(pGraphics);
	}

	void Shutdown(IGraphics *pGraphics)
	{
		Reset(pGraphics);
		m_pProvider.reset();
		m_pVisualizer.reset();
	}

	void StartArtDecode(CMusicPlayer *pOwner, const unsigned char *pData, size_t DataSize, const char *pContextName)
	{
		if(pOwner == nullptr || pData == nullptr || DataSize == 0)
			return;

		if(m_pArtDecodeJob)
		{
			m_pArtDecodeJob->Abort();
			m_pArtDecodeJob = nullptr;
		}
		m_OptArtDecodedFrames.reset();
		m_ArtUploadIndex = 0;

		m_pArtDecodeJob = std::make_shared<CMusicPlayerArtDecodeJob>(pOwner->Graphics(), pData, DataSize, pContextName);
		pOwner->Engine()->AddJob(m_pArtDecodeJob);
	}

	void UpdateArtDecodeAndUpload(CMusicPlayer *pOwner)
	{
		if(pOwner == nullptr)
			return;

		if(m_pArtDecodeJob && m_pArtDecodeJob->Done())
		{
			if(m_pArtDecodeJob->State() == IJob::STATE_DONE && m_pArtDecodeJob->Success() && !m_pArtDecodeJob->DecodedFrames().Empty())
			{
				const int Width = m_pArtDecodeJob->DecodedFrames().m_Width;
				const int Height = m_pArtDecodeJob->DecodedFrames().m_Height;
				m_OptArtDecodedFrames.emplace(std::move(m_pArtDecodeJob->DecodedFrames()));
				m_ArtUploadIndex = 0;
				m_ArtWidth = Width;
				m_ArtHeight = Height;
				m_ArtAnimated = false;
				m_ArtAnimationStart = 0;

				if(!m_OptArtDecodedFrames->m_vFrames.empty())
				{
					const SArtworkColorAnalysis Analysis = AnalyzeArtworkBaseColor(m_OptArtDecodedFrames->m_vFrames.front().m_Image);
					m_Palette = BuildPaletteFromAccent(SelectMusicPlayerAccent(Analysis));
				}
				else
				{
					m_Palette = DefaultMusicPlayerPalette();
				}
			}
			else
			{
				m_OptArtDecodedFrames.reset();
				m_ArtUploadIndex = 0;
				m_ArtWidth = 0;
				m_ArtHeight = 0;
				m_ArtAnimated = false;
				m_ArtAnimationStart = 0;
				m_Palette = DefaultMusicPlayerPalette();
				MediaDecoder::UnloadFrames(pOwner->Graphics(), m_vArtFrames);
			}
			m_pArtDecodeJob = nullptr;
		}

		if(!m_OptArtDecodedFrames.has_value())
			return;

		SMediaDecodedFrames &DecodedFrames = *m_OptArtDecodedFrames;
		if(DecodedFrames.m_vFrames.empty())
		{
			m_OptArtDecodedFrames.reset();
			m_ArtUploadIndex = 0;
			return;
		}

		const int64_t UploadStart = time_get();
		int UploadedThisFrame = 0;
		while(m_ArtUploadIndex < (int)DecodedFrames.m_vFrames.size())
		{
			if(UploadedThisFrame >= MUSIC_ART_MAX_TEXTURE_UPLOADS_PER_FRAME)
				break;

			const int64_t ElapsedUs = ((time_get() - UploadStart) * 1000000) / time_freq();
			if(ElapsedUs >= MUSIC_ART_TEXTURE_UPLOAD_BUDGET_US)
				break;

			SMediaRawFrame &RawFrame = DecodedFrames.m_vFrames[m_ArtUploadIndex];
			SMediaFrame Frame;
			Frame.m_DurationMs = RawFrame.m_DurationMs;
			Frame.m_Texture = pOwner->Graphics()->LoadTextureRawMove(RawFrame.m_Image, 0, "music_player_art");
			if(!Frame.m_Texture.IsValid())
			{
				m_OptArtDecodedFrames.reset();
				m_ArtUploadIndex = 0;
				MediaDecoder::UnloadFrames(pOwner->Graphics(), m_vArtFrames);
				m_ArtAnimated = false;
				m_ArtAnimationStart = 0;
				m_ArtWidth = 0;
				m_ArtHeight = 0;
				m_Palette = DefaultMusicPlayerPalette();
				return;
			}
			m_vArtFrames.push_back(Frame);
			m_ArtUploadIndex++;
			UploadedThisFrame++;
		}

		const bool Finished = m_ArtUploadIndex >= (int)DecodedFrames.m_vFrames.size();
		if(!Finished)
			return;

		m_OptArtDecodedFrames.reset();
		m_ArtUploadIndex = 0;
		m_ArtAnimated = m_vArtFrames.size() > 1;
		m_ArtAnimationStart = m_ArtAnimated ? time_get() : 0;
	}

	void BeginArtLoad(CMusicPlayer *pOwner)
	{
		if(m_Snapshot.m_Art.m_Key == m_LastArtKey)
			return;

		m_pArtRequest.reset();
		if(m_pArtDecodeJob)
		{
			m_pArtDecodeJob->Abort();
			m_pArtDecodeJob = nullptr;
		}
		m_OptArtDecodedFrames.reset();
		m_ArtUploadIndex = 0;
		MediaDecoder::UnloadFrames(pOwner->Graphics(), m_vArtFrames);
		m_ArtAnimated = false;
		m_ArtWidth = 0;
		m_ArtHeight = 0;
		m_ArtAnimationStart = 0;
		m_Palette = DefaultMusicPlayerPalette();
		m_LastArtKey = m_Snapshot.m_Art.m_Key;

		if(m_Snapshot.m_Art.m_Type == SMusicArt::EType::BYTES)
		{
			if(m_Snapshot.m_Art.m_vBytes.empty())
				return;
			StartArtDecode(pOwner, m_Snapshot.m_Art.m_vBytes.data(), m_Snapshot.m_Art.m_vBytes.size(), "music_player_art_bytes");
			return;
		}

		if(m_Snapshot.m_Art.m_Type != SMusicArt::EType::URL || m_Snapshot.m_Art.m_Url.empty())
			return;

		if(IsUrlScheme(m_Snapshot.m_Art.m_Url, "file://"))
		{
			void *pFileData = nullptr;
			unsigned FileSize = 0;
			const std::string Path = FileUrlToPath(m_Snapshot.m_Art.m_Url);
			if(pOwner->Storage()->ReadFile(Path.c_str(), IStorage::TYPE_ABSOLUTE, &pFileData, &FileSize))
			{
				StartArtDecode(pOwner, static_cast<unsigned char *>(pFileData), FileSize, Path.c_str());
				free(pFileData);
			}
			return;
		}

		if(!IsUrlScheme(m_Snapshot.m_Art.m_Url, "https://") && !IsUrlScheme(m_Snapshot.m_Art.m_Url, "http://"))
			return;

		m_pArtRequest = HttpGet(m_Snapshot.m_Art.m_Url.c_str());
		m_pArtRequest->Timeout(CTimeout{2000, 8000, 500, 5});
		m_pArtRequest->MaxResponseSize(8 * 1024 * 1024);
		pOwner->Http()->Run(m_pArtRequest);
	}

	void UpdateArtwork(CMusicPlayer *pOwner)
	{
		BeginArtLoad(pOwner);
		if(m_pArtRequest && m_pArtRequest->State() == EHttpState::DONE)
		{
			std::shared_ptr<CHttpRequest> pFinished = m_pArtRequest;
			m_pArtRequest.reset();
			if(pFinished->StatusCode() < 200 || pFinished->StatusCode() >= 400)
			{
				UpdateArtDecodeAndUpload(pOwner);
				return;
			}

			unsigned char *pData = nullptr;
			size_t DataSize = 0;
			pFinished->Result(&pData, &DataSize);
			StartArtDecode(pOwner, pData, DataSize, "music_player_http_art");
		}

		UpdateArtDecodeAndUpload(pOwner);
	}

	int64_t DisplayPositionMs() const
	{
		int64_t Position = maximum<int64_t>(0, m_PlaybackAnchorPositionMs);
		if(m_PlaybackAnchorState == EMusicPlaybackState::PLAYING && m_PlaybackAnchorTick > 0)
			Position += ((time_get() - m_PlaybackAnchorTick) * 1000) / time_freq();
		if(m_Snapshot.m_DurationMs > 0)
			Position = minimum(Position, m_Snapshot.m_DurationMs);
		return Position;
	}

	void AttachVisualizerData(SNowPlayingSnapshot &Snapshot)
	{
		Snapshot.m_HasVisualizer = false;
		Snapshot.m_Visualizer = SMusicVisualizerData();
		if(g_Config.m_BcMusicPlayerVisualizer == 0 || !m_pVisualizer)
			return;

		Snapshot.m_HasVisualizer = true;
		Snapshot.m_Visualizer = m_VisualizerData;
		if(Snapshot.m_Visualizer.m_BackendStatus == EMusicVisualizerBackendStatus::LIVE)
			Snapshot.m_Visualizer.m_IsPassiveFallback = false;
	}

	void RefreshVisualizerData()
	{
		m_VisualizerData = SMusicVisualizerData();
		if(g_Config.m_BcMusicPlayerVisualizer == 0 || !m_pVisualizer)
		{
			m_VisualizerData.m_IsPassiveFallback = true;
			m_VisualizerData.m_BackendStatus = EMusicVisualizerBackendStatus::FALLBACK;
			DebugLogVisualizerState("refresh_disabled");
			return;
		}

		SMusicPlaybackHint Hint;
		Hint.m_ServiceId = m_Snapshot.m_ServiceId;
		Hint.m_Title = m_Snapshot.m_Title;
		Hint.m_Artist = m_Snapshot.m_Artist;
		Hint.m_PlaybackState = m_Snapshot.m_PlaybackState;
		m_pVisualizer->SetPlaybackHint(Hint);
		m_pVisualizer->Poll(m_VisualizerData);
		if(m_VisualizerData.m_BackendStatus == EMusicVisualizerBackendStatus::LIVE)
			m_VisualizerData.m_IsPassiveFallback = false;
		else if(m_VisualizerData.m_BackendStatus == EMusicVisualizerBackendStatus::FALLBACK || m_VisualizerData.m_BackendStatus == EMusicVisualizerBackendStatus::UNAVAILABLE)
			m_VisualizerData.m_IsPassiveFallback = true;
		DebugLogVisualizerState("refresh");
	}

	void RefreshCurrentSnapshotVisualizer()
	{
		if(!m_Snapshot.m_Valid)
			return;
		AttachVisualizerData(m_Snapshot);
	}

	void ApplySnapshot(SNowPlayingSnapshot Snapshot, int64_t Now)
	{
		if(!Snapshot.m_Valid)
		{
			m_Snapshot = SNowPlayingSnapshot();
			m_LastSnapshotTick = 0;
			ResetPlaybackAnchor();
			return;
		}

		const std::string TrackKey = BuildSnapshotTrackKey(Snapshot);
		const int64_t SnapshotPosition = std::clamp<int64_t>(Snapshot.m_PositionMs, 0, maximum<int64_t>(Snapshot.m_DurationMs, Snapshot.m_PositionMs));
		const bool NewTrack = TrackKey != m_PlaybackTrackKey;
		const bool StateChanged = Snapshot.m_PlaybackState != m_PlaybackAnchorState;
		const int64_t PredictedPosition = DisplayPositionMs();
		const int64_t Drift = SnapshotPosition - PredictedPosition;
		const bool NeedsHardResync =
			NewTrack ||
			m_PlaybackAnchorTick == 0 ||
			StateChanged ||
			std::llabs(Drift) > 1500;

		if(NeedsHardResync)
		{
			m_PlaybackAnchorPositionMs = SnapshotPosition;
			m_PlaybackAnchorTick = Now;
		}
		else if(Snapshot.m_PlaybackState == EMusicPlaybackState::PLAYING)
		{
			const int64_t GentleCorrection = std::clamp<int64_t>(Drift, 0, 120);
			m_PlaybackAnchorPositionMs = maximum<int64_t>(0, PredictedPosition + GentleCorrection);
			m_PlaybackAnchorTick = Now;
		}
		else
		{
			m_PlaybackAnchorPositionMs = SnapshotPosition;
			m_PlaybackAnchorTick = Now;
		}

		m_PlaybackTrackKey = TrackKey;
		m_PlaybackAnchorState = Snapshot.m_PlaybackState;
		m_Snapshot = std::move(Snapshot);
		m_LastSnapshotTick = Now;
	}

	float VisualPositionMs(float Delta)
	{
		const std::string TrackKey = BuildSnapshotTrackKey(m_Snapshot);
		const float Target = (float)DisplayPositionMs();
		if(TrackKey != m_VisualTrackKey)
		{
			m_VisualTrackKey = TrackKey;
			m_VisualPositionMs = Target;
		}
		else if(m_Snapshot.m_PlaybackState == EMusicPlaybackState::PLAYING)
		{
			m_VisualPositionMs = mix(m_VisualPositionMs + maximum(0.0f, Delta) * 1000.0f, Target, std::clamp(Delta * 6.0f, 0.0f, 1.0f));
		}
		else
		{
			m_VisualPositionMs = mix(m_VisualPositionMs, Target, std::clamp(Delta * 10.0f, 0.0f, 1.0f));
		}

		if(m_Snapshot.m_DurationMs > 0)
			m_VisualPositionMs = std::clamp(m_VisualPositionMs, 0.0f, (float)m_Snapshot.m_DurationMs);
		else
			m_VisualPositionMs = maximum(0.0f, m_VisualPositionMs);
		return m_VisualPositionMs;
	}

	void UpdateVisualizerLevels(CMusicPlayer *pOwner, const SNowPlayingSnapshot &Snapshot, int64_t PositionMs, float Delta)
	{
		(void)pOwner;

		if(g_Config.m_BcMusicPlayerVisualizer == 0)
		{
			DebugLogRenderDecision("visualizer_disabled", Snapshot);
			for(float &Level : m_aVisualizerLevels)
				Level = ApproachAnim(Level, 0.0f, Delta, 12.0f);
			return;
		}

		const float Smoothing = std::clamp(g_Config.m_BcMusicPlayerVisualizerSmoothing / 100.0f, 0.0f, 1.0f);
		const float Sensitivity = MusicPlayerVisualizerSensitivity();
		const float AttackSpeed = mix(20.0f, 7.8f, Smoothing);
		const float ReleaseSpeed = mix(12.5f, 3.8f, Smoothing);
		const bool UseRealtimeVisualizer = Snapshot.m_HasVisualizer && Snapshot.m_Visualizer.m_BackendStatus == EMusicVisualizerBackendStatus::LIVE;
		if(UseRealtimeVisualizer)
		{
			DebugLogRenderDecision("live", Snapshot);
			std::array<float, VISUALIZER_BARS> aTarget{};
			const float Energy = std::clamp((Snapshot.m_Visualizer.m_Rms * 40.0f + Snapshot.m_Visualizer.m_Peak * 18.0f) * Sensitivity, 0.0f, 2.5f);
			for(int i = 0; i < VISUALIZER_BARS; ++i)
			{
				const float Bin = Snapshot.m_Visualizer.m_aBins[i];
				const float PrevBin = i > 0 ? Snapshot.m_Visualizer.m_aBins[i - 1] : Bin;
				const float NextBin = i + 1 < VISUALIZER_BARS ? Snapshot.m_Visualizer.m_aBins[i + 1] : Bin;
				const float Neighborhood = PrevBin * 0.22f + Bin * 0.56f + NextBin * 0.22f;
				const float PeakBias = (i < VISUALIZER_BARS / 3 ? Snapshot.m_Visualizer.m_Peak * 1.40f : Snapshot.m_Visualizer.m_Rms * 1.05f) * Sensitivity;
				float Target = Neighborhood * (2.2f + Sensitivity * 1.85f) + Energy * (0.34f + 0.16f * (1.0f - i / maximum(1.0f, (float)(VISUALIZER_BARS - 1)))) + PeakBias;
				Target = std::clamp(Target, 0.0f, 1.35f);
				if(Target > 0.0f)
					Target = powf(Target, 0.52f);
				if(Target < 0.004f / Sensitivity)
					Target = 0.0f;
				aTarget[i] = std::clamp(Target, 0.0f, 1.0f);
			}
			std::array<float, VISUALIZER_BARS> aSmoothed = aTarget;
			for(int i = 0; i < VISUALIZER_BARS; ++i)
			{
				const float Prev = i > 0 ? aTarget[i - 1] : aTarget[i];
				const float Next = i + 1 < VISUALIZER_BARS ? aTarget[i + 1] : aTarget[i];
				aSmoothed[i] = Prev * 0.20f + aTarget[i] * 0.60f + Next * 0.20f;
			}
			for(int i = 0; i < VISUALIZER_BARS; ++i)
			{
				const float BoostedAttack = AttackSpeed + std::clamp(Sensitivity * 1.8f, 0.0f, 20.0f);
				const float BoostedRelease = ReleaseSpeed + std::clamp(Sensitivity * 0.65f, 0.0f, 8.0f);
				const float Speed = aSmoothed[i] > m_aVisualizerLevels[i] ? BoostedAttack : BoostedRelease;
				m_aVisualizerLevels[i] = ApproachAnim(m_aVisualizerLevels[i], aSmoothed[i], Delta, Speed);
			}
			return;
		}

		const bool BackendConnecting = Snapshot.m_HasVisualizer && Snapshot.m_Visualizer.m_BackendStatus == EMusicVisualizerBackendStatus::CONNECTING;
		const bool BackendSilent = Snapshot.m_HasVisualizer && Snapshot.m_Visualizer.m_BackendStatus == EMusicVisualizerBackendStatus::SILENT;
		if(BackendConnecting || BackendSilent)
		{
			DebugLogRenderDecision(BackendSilent ? "silent" : "connecting", Snapshot);
			for(float &Level : m_aVisualizerLevels)
				Level = ApproachAnim(Level, 0.0f, Delta, ReleaseSpeed);
			return;
		}

		if(Snapshot.m_PlaybackState != EMusicPlaybackState::PLAYING)
		{
			DebugLogRenderDecision("passive_idle", Snapshot);
			const float TimeSeconds = (float)(time_get() / (double)time_freq());
			for(int i = 0; i < VISUALIZER_BARS; ++i)
			{
				const float Drift = 0.18f + 0.06f * (0.5f + 0.5f * sinf(TimeSeconds * (0.55f + 0.03f * i) * 2.0f * pi + 0.35f * i));
				m_aVisualizerLevels[i] = ApproachAnim(m_aVisualizerLevels[i], Drift, Delta, 4.0f);
			}
			return;
		}

		const float TimeSeconds = (float)(time_get() / (double)time_freq());
		const float TrackProgress = Snapshot.m_DurationMs > 0 ? std::clamp(PositionMs / (float)Snapshot.m_DurationMs, 0.0f, 1.0f) : 0.0f;
		DebugLogRenderDecision("fallback_motion", Snapshot);
		for(int i = 0; i < VISUALIZER_BARS; ++i)
		{
			const float Target = VisualizerBarTargetLevel(Snapshot, TimeSeconds, TrackProgress, i, VISUALIZER_BARS);
			const float Speed = Target > m_aVisualizerLevels[i] ? 11.0f : 6.0f;
			m_aVisualizerLevels[i] = ApproachAnim(m_aVisualizerLevels[i], Target, Delta, Speed);
		}

		std::array<float, VISUALIZER_BARS> aSmoothed = m_aVisualizerLevels;
		for(int i = 0; i < VISUALIZER_BARS; ++i)
		{
			const float Prev = i > 0 ? m_aVisualizerLevels[i - 1] : m_aVisualizerLevels[i];
			const float Next = i + 1 < VISUALIZER_BARS ? m_aVisualizerLevels[i + 1] : m_aVisualizerLevels[i];
			aSmoothed[i] = Prev * 0.24f + m_aVisualizerLevels[i] * 0.52f + Next * 0.24f;
		}
		std::array<float, VISUALIZER_BARS> aFinal = aSmoothed;
		for(int i = 0; i < VISUALIZER_BARS; ++i)
		{
			const float Prev = i > 0 ? aSmoothed[i - 1] : aSmoothed[i];
			const float Next = i + 1 < VISUALIZER_BARS ? aSmoothed[i + 1] : aSmoothed[i];
			aFinal[i] = Prev * 0.20f + aSmoothed[i] * 0.60f + Next * 0.20f;
		}
		m_aVisualizerLevels = aFinal;
	}

	void UpdateHudReservation(CMusicPlayer *pOwner)
	{
		if(g_Config.m_ClFocusMode && g_Config.m_ClFocusModeHideSongPlayer)
		{
			ResetHudState();
			return;
		}

		const float Height = HudLayout::CANVAS_HEIGHT;
		const float Width = Height * pOwner->Graphics()->ScreenAspect();
		const auto Layout = HudLayout::Get(HudLayout::MODULE_MUSIC_PLAYER, Width, Height);
		const SGameTimerDisplay GameTimer = BuildGameTimerDisplay(pOwner->GameClient()->m_Snap.m_pGameInfoObj, pOwner->Client()->GameTick(g_Config.m_ClDummy), pOwner->Client()->GameTickSpeed(), false);
		const float CompactScale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
		const float CompactWidthScale = Width / maximum(HudLayout::CANVAS_WIDTH, 0.001f);
		const float CompactTitleFont = 6.6f * CompactScale;
		const float CompactTextSlotWidth = ComputeCompactTextSlotWidth(pOwner->TextRender(), GameTimer, CompactTitleFont, CompactScale, CompactWidthScale);
		const CUIRect UiScreen = *pOwner->Ui()->Screen();
		if(UiScreen.w <= 0.0f || UiScreen.h <= 0.0f)
		{
			ResetHudState();
			return;
		}

		const vec2 WindowSize(maximum(1.0f, (float)pOwner->Graphics()->WindowWidth()), maximum(1.0f, (float)pOwner->Graphics()->WindowHeight()));
		const vec2 UiMousePos = pOwner->Ui()->UpdatedMousePos() * vec2(UiScreen.w, UiScreen.h) / WindowSize;
		const vec2 UiToHudScale(Width / UiScreen.w, Height / UiScreen.h);
		const vec2 MousePos = UiMousePos * UiToHudScale;
		const bool ChatActive = pOwner->GameClient()->m_Chat.IsActive();
		const bool FreezeNonChatLayout = !ChatActive &&
			(pOwner->GameClient()->m_GameConsole.IsActive() || pOwner->GameClient()->m_Menus.IsActive());
		const bool AllowInteraction = ChatActive;

		const float ProbeT = EaseInOutCubic(m_ExpandAnim);
		const SMusicPlayerMetrics ProbeMetrics = ComputeMusicPlayerMetrics(Layout, Width, Height, ProbeT, CompactTextSlotWidth);
		const CUIRect UiView = HudToUiRect(ProbeMetrics.m_ViewRect, UiScreen, Width, Height);
		const CUIRect UiExpandedRect = HudToUiRect(ProbeMetrics.m_ExpandedRect, UiScreen, Width, Height);
		const float UiMargin = 2.5f * ProbeMetrics.m_Scale * UiScreen.h / Height;
		bool HoverCandidate = false;
		if(AllowInteraction)
		{
			HoverCandidate =
				IsPointInsideRect(ProbeMetrics.m_ViewRect, MousePos, 2.5f * ProbeMetrics.m_Scale) ||
				IsPointInsideRect(UiView, UiMousePos, UiMargin);
			if(!HoverCandidate && (m_ExpandAnim > 0.01f || m_HoverAnim > 0.01f))
			{
				HoverCandidate =
					IsPointInsideRect(ProbeMetrics.m_ExpandedRect, MousePos, 3.0f * ProbeMetrics.m_Scale) ||
					IsPointInsideRect(UiExpandedRect, UiMousePos, UiMargin * 2.0f);
			}
		}

		const float Delta = std::clamp(pOwner->Client()->RenderFrameTime(), 0.0f, 0.1f);
		if(m_CompactTextSlotWidthAnim <= 0.0f)
			m_CompactTextSlotWidthAnim = CompactTextSlotWidth;
		if(!FreezeNonChatLayout)
		{
			const float WidthSpeed = CompactTextSlotWidth > m_CompactTextSlotWidthAnim ? 10.0f : 8.0f;
			m_CompactTextSlotWidthAnim = ApproachAnim(m_CompactTextSlotWidthAnim, CompactTextSlotWidth, Delta, WidthSpeed);
			const float TargetExpand = HoverCandidate ? 1.0f : 0.0f;
			const float TargetGlow = HoverCandidate ? 1.0f : 0.0f;
			m_ExpandAnim = ApproachAnim(m_ExpandAnim, TargetExpand, Delta, TargetExpand > m_ExpandAnim ? 8.5f : 6.0f);
			m_HoverAnim = ApproachAnim(m_HoverAnim, TargetGlow, Delta, 8.0f);
		}
		else
		{
			m_ExpandAnim = 0.0f;
			m_HoverAnim = 0.0f;
		}

		const float SizeT = EaseInOutCubic(m_ExpandAnim);
		const SMusicPlayerMetrics Metrics = ComputeMusicPlayerMetrics(Layout, Width, Height, SizeT, m_CompactTextSlotWidthAnim);
		m_HudReservation.m_Rect = Metrics.m_ViewRect;
		m_HudReservation.m_Visible = true;
		m_HudReservation.m_Active = true;
		m_HudReservation.m_PushAmount = 1.0f;
	}
};

CMusicPlayer::CMusicPlayer() :
	m_pImpl(std::make_unique<CImpl>())
{
}

CMusicPlayer::~CMusicPlayer() = default;

void CMusicPlayer::EnsureImpl()
{
	if(!m_pImpl)
		m_pImpl = std::make_unique<CImpl>();
}

void CMusicPlayer::OnReset()
{
	EnsureImpl();
	m_pImpl->Reset(Graphics());
}

void CMusicPlayer::OnShutdown()
{
	if(m_pImpl)
	{
		m_pImpl->Shutdown(Graphics());
		m_pImpl.reset();
	}
}

void CMusicPlayer::OnWindowResize()
{
	if(!m_pImpl)
		return;
	m_pImpl->ResetArtwork(Graphics());
}

CMusicPlayer::SHudReservation CMusicPlayer::HudReservation() const
{
	if(!m_pImpl)
		return SHudReservation();
	return m_pImpl->m_HudReservation;
}

float CMusicPlayer::GetHudPushOffsetForRect(const CUIRect &Rect, float CanvasWidth, float Padding) const
{
	const SHudReservation Reservation = HudReservation();
	if(!Reservation.m_Visible || !Reservation.m_Active || Reservation.m_PushAmount <= 0.0f || Rect.w <= 0.0f || Rect.h <= 0.0f)
		return 0.0f;

	const float Gap = maximum(Padding, 2.0f);
	if(!RectsOverlap(Reservation.m_Rect, Rect, Gap))
		return 0.0f;

	const float LeftX = std::clamp(Reservation.m_Rect.x - Gap - Rect.w, 0.0f, maximum(0.0f, CanvasWidth - Rect.w));
	const float RightX = std::clamp(Reservation.m_Rect.x + Reservation.m_Rect.w + Gap, 0.0f, maximum(0.0f, CanvasWidth - Rect.w));
	const float LeftOffset = LeftX - Rect.x;
	const float RightOffset = RightX - Rect.x;
	const float ChosenOffset = absolute(LeftOffset) <= absolute(RightOffset) ? LeftOffset : RightOffset;
	return ChosenOffset * Reservation.m_PushAmount;
}

float CMusicPlayer::GetHudPushDownOffsetForRect(const CUIRect &Rect, float CanvasHeight, float Padding) const
{
	const SHudReservation Reservation = HudReservation();
	if(!Reservation.m_Visible || !Reservation.m_Active || Reservation.m_PushAmount <= 0.0f || Rect.w <= 0.0f || Rect.h <= 0.0f)
		return 0.0f;

	const float Gap = maximum(Padding, 2.0f);
	if(!RectsOverlap(Reservation.m_Rect, Rect, Gap))
		return 0.0f;

	const float TargetY = std::clamp(Reservation.m_Rect.y + Reservation.m_Rect.h + Gap, 0.0f, maximum(0.0f, CanvasHeight - Rect.h));
	return maximum(0.0f, (TargetY - Rect.y) * Reservation.m_PushAmount);
}

bool CMusicPlayer::GetNowPlayingInfo(SNowPlayingInfo &Out) const
{
	Out = SNowPlayingInfo();
	if(!m_pImpl)
		return false;

	const SNowPlayingSnapshot &Snapshot = m_pImpl->m_Snapshot;
	if(!Snapshot.m_Valid)
		return false;

	Out.m_Valid = true;
	Out.m_Playing = Snapshot.m_PlaybackState == EMusicPlaybackState::PLAYING;
	Out.m_DurationMs = maximum<int64_t>(0, Snapshot.m_DurationMs);
	Out.m_PositionMs = maximum<int64_t>(0, m_pImpl->DisplayPositionMs());
	Out.m_Seed = TrackAnimationSeed(Snapshot);
	return true;
}

void CMusicPlayer::OnUpdate()
{
	if(GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_MUSIC_PLAYER))
		return;

	EnsureImpl();
	const int64_t Now = time_get();
	if(m_pImpl->m_LastPollTick == 0 || Now - m_pImpl->m_LastPollTick >= time_freq() / 8)
	{
		m_pImpl->RefreshVisualizerData();
		SNowPlayingSnapshot Snapshot;
		if(m_pImpl->m_pProvider && m_pImpl->m_pProvider->Poll(Snapshot))
		{
			m_pImpl->AttachVisualizerData(Snapshot);
			m_pImpl->DebugLogProviderSnapshot(Snapshot);
			m_pImpl->ApplySnapshot(std::move(Snapshot), Now);
		}
		else
		{
			const bool UseStaleSnapshotGrace = m_pImpl->m_Snapshot.m_Valid && Now - m_pImpl->m_LastSnapshotTick <= time_freq() * 2;
			m_pImpl->DebugLogProviderPollFailure(UseStaleSnapshotGrace);
			if(UseStaleSnapshotGrace)
				m_pImpl->RefreshCurrentSnapshotVisualizer();
			else
				m_pImpl->ApplySnapshot(SNowPlayingSnapshot(), Now);
		}
		m_pImpl->m_LastPollTick = Now;
	}

	// Keep polling now playing info even if the HUD element is disabled.
	if(g_Config.m_BcMusicPlayer == 0)
	{
		m_pImpl->ResetHudState();
		return;
	}
	if(m_pImpl->m_Snapshot.m_Valid &&
		m_pImpl->m_Snapshot.m_PlaybackState == EMusicPlaybackState::PAUSED &&
		g_Config.m_BcMusicPlayerShowWhenPaused == 0)
	{
		m_pImpl->ResetHudState();
		return;
	}

	m_pImpl->UpdateArtwork(this);
	m_pImpl->UpdateHudReservation(this);
}

void CMusicPlayer::OnRender()
{
	if(GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_MUSIC_PLAYER))
		return;

	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(!m_pImpl)
		return;
	if(GameClient()->m_Menus.IsActive() && !GameClient()->m_Menus.IsIngameGamePage())
		return;

	RenderMusicPlayer(false);
}

void CMusicPlayer::RenderMusicPlayer(bool ForcePreview)
{
	if(!m_pImpl)
		return;
	if(g_Config.m_BcMusicPlayer == 0)
		return;
	if(!ForcePreview && g_Config.m_ClFocusMode && g_Config.m_ClFocusModeHideSongPlayer)
		return;
	if(!ForcePreview && m_pImpl->m_Snapshot.m_Valid &&
		m_pImpl->m_Snapshot.m_PlaybackState == EMusicPlaybackState::PAUSED &&
		g_Config.m_BcMusicPlayerShowWhenPaused == 0)
		return;

	SNowPlayingSnapshot Snapshot = m_pImpl->m_Snapshot;
	if(ForcePreview)
	{
		Snapshot = SNowPlayingSnapshot();
		Snapshot.m_Valid = true;
		Snapshot.m_Title = "Blinding Lights";
		Snapshot.m_Artist = "The Weeknd";
		Snapshot.m_Album = "After Hours";
		Snapshot.m_DurationMs = 200000;
		Snapshot.m_PositionMs = 101000;
		Snapshot.m_PlaybackState = EMusicPlaybackState::PLAYING;
		Snapshot.m_CanPrev = true;
		Snapshot.m_CanPlayPause = true;
		Snapshot.m_CanNext = true;
	}

	const float Height = HudLayout::CANVAS_HEIGHT;
	const float Width = Height * Graphics()->ScreenAspect();
	const auto Layout = HudLayout::Get(HudLayout::MODULE_MUSIC_PLAYER, Width, Height);
	const SGameTimerDisplay GameTimer = BuildGameTimerDisplay(GameClient()->m_Snap.m_pGameInfoObj, Client()->GameTick(g_Config.m_ClDummy), Client()->GameTickSpeed(), ForcePreview);
	const float LayoutScale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	const float LayoutWidthScale = Width / maximum(HudLayout::CANVAS_WIDTH, 0.001f);
	const float CompactTitleFont = 6.6f * LayoutScale;
	const float CompactTextSlotWidth = ComputeCompactTextSlotWidth(TextRender(), GameTimer, CompactTitleFont, LayoutScale, LayoutWidthScale);
	Graphics()->MapScreen(0.0f, 0.0f, Width, Height);

	const bool BackgroundEnabled = Layout.m_BackgroundEnabled;
	const unsigned BackgroundColor = Layout.m_BackgroundColor;
	const CUIRect UiScreen = *Ui()->Screen();
	const vec2 WindowSize(maximum(1.0f, (float)Graphics()->WindowWidth()), maximum(1.0f, (float)Graphics()->WindowHeight()));
	const vec2 UiMousePos = Ui()->UpdatedMousePos() * vec2(UiScreen.w, UiScreen.h) / WindowSize;
	const vec2 UiToHudScale(Width / maximum(UiScreen.w, 1.0f), Height / maximum(UiScreen.h, 1.0f));
	const vec2 MousePos = UiMousePos * UiToHudScale;
	const bool AllowInteraction = !ForcePreview && GameClient()->m_Chat.IsActive();

	const float ExpandT = ForcePreview ? 1.0f : EaseInOutCubic(m_pImpl->m_ExpandAnim);
	const float TextT = EaseOutCubic(std::clamp((ExpandT - 0.04f) / 0.96f, 0.0f, 1.0f));
	const float ControlsT = EaseOutCubic(std::clamp((ExpandT - 0.16f) / 0.84f, 0.0f, 1.0f));
	const float HoverT = ForcePreview ? 1.0f : EaseOutCubic(m_pImpl->m_HoverAnim);
	const float Delta = std::clamp(Client()->RenderFrameTime(), 0.0f, 0.1f);
	const float AnimatedCompactTextSlotWidth = ForcePreview ? CompactTextSlotWidth :
		(m_pImpl->m_CompactTextSlotWidthAnim > 0.0f ? m_pImpl->m_CompactTextSlotWidthAnim : CompactTextSlotWidth);
	const SMusicPlayerMetrics Metrics = ComputeMusicPlayerMetrics(Layout, Width, Height, ExpandT, AnimatedCompactTextSlotWidth);
	const float Scale = Metrics.m_Scale;
	const float WidthScale = Metrics.m_WidthScale;
	const CUIRect View = ForcePreview ? Metrics.m_ViewRect : m_pImpl->m_HudReservation.m_Rect;
	const float UiFontScale = UiScreen.h / maximum(Height, 1.0f);

	const SMusicPlayerPalette Palette = ForcePreview ?
		BuildPaletteFromAccent(g_Config.m_BcMusicPlayerColorMode == 0 ? color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcMusicPlayerStaticColor)) : DefaultPreviewAccentForColorMode(g_Config.m_BcMusicPlayerColorMode)) :
		m_pImpl->m_Palette;
	ColorRGBA LayoutColor = color_cast<ColorRGBA>(ColorHSLA(BackgroundColor, true));
	if(!BackgroundEnabled)
		LayoutColor = ColorRGBA(0.12f, 0.13f, 0.16f, 0.72f);
	const bool CoverColorMode = g_Config.m_BcMusicPlayerColorMode != 0;
	const bool DominantColorMode = g_Config.m_BcMusicPlayerColorMode == 2;
	const float PanelTintT = ((DominantColorMode ? 0.72f : (CoverColorMode ? 0.62f : 0.50f)) - HoverT * (DominantColorMode ? 0.03f : 0.04f));
	const ColorRGBA PanelTintColor = DominantColorMode ? MixColor(Palette.m_Mid, Palette.m_Dark, 0.58f) : (CoverColorMode ? MixColor(Palette.m_Mid, Palette.m_Dark, 0.40f) : Palette.m_Dark);
	const ColorRGBA PanelColor = WithAlpha(MixColor(LayoutColor, PanelTintColor, PanelTintT), maximum(LayoutColor.a, 0.90f));
	const ColorRGBA GlowColor = WithAlpha(MixColor(Palette.m_Glow, Palette.m_Light, DominantColorMode ? 0.40f : (CoverColorMode ? 0.28f : 0.18f)), (DominantColorMode ? 0.04f : 0.02f) + (DominantColorMode ? 0.07f : 0.05f) * HoverT);
	const float OuterPad = 0.42f * Scale + HoverT * 0.48f * Scale;

	if(GlowColor.a > 0.001f)
	{
		const int Corners = HudLayout::BackgroundCorners(IGraphics::CORNER_ALL, View.x - OuterPad, View.y - OuterPad, View.w + OuterPad * 2.0f, View.h + OuterPad * 2.0f, Width, Height);
		Graphics()->DrawRect(View.x - OuterPad, View.y - OuterPad, View.w + OuterPad * 2.0f, View.h + OuterPad * 2.0f, GlowColor, Corners, Metrics.m_Rounding + OuterPad);
	}
	if(BackgroundEnabled)
	{
		const int Corners = HudLayout::BackgroundCorners(IGraphics::CORNER_ALL, View.x, View.y, View.w, View.h, Width, Height);
		Graphics()->DrawRect(View.x, View.y, View.w, View.h, PanelColor, Corners, Metrics.m_Rounding);
	}
	else
	{
		Graphics()->DrawRect(View.x, View.y, View.w, View.h, PanelColor, IGraphics::CORNER_ALL, Metrics.m_Rounding);
	}

	CUIRect Content = View;
	Content.Margin(1.70f * Scale, &Content);

	const float VisualPad = 1.15f * Scale * WidthScale;
	const float VisualW = 9.8f * Scale * WidthScale + ExpandT * 1.6f * Scale * WidthScale;
	const float ArtSize = minimum(View.h - 3.0f * Scale, 11.8f * Scale + ExpandT * 1.8f * Scale);
	CUIRect ArtRect = {Content.x + 0.1f * Scale * WidthScale, View.y + (View.h - ArtSize) * 0.5f, ArtSize, ArtSize};
	const bool HasMusic = Snapshot.m_PlaybackState == EMusicPlaybackState::PLAYING;
	const float VisualH = HasMusic ? (8.2f * Scale + ExpandT * 2.1f * Scale) : (3.6f * Scale + ExpandT * 0.6f * Scale);
	CUIRect VisualRect = {View.x + View.w - 1.95f * Scale * WidthScale - VisualW, View.y + (View.h - VisualH) * 0.5f, VisualW, VisualH};
	CUIRect TextArea = Content;
	TextArea.x = ArtRect.x + ArtRect.w + 1.15f * Scale * WidthScale;
	TextArea.w = maximum(0.0f, VisualRect.x - VisualPad - TextArea.x);
	const float TextCenterX = View.x + View.w * 0.5f;
	const float TextHalfW = maximum(0.0f, minimum(TextCenterX - TextArea.x, (TextArea.x + TextArea.w) - TextCenterX));
	CUIRect CenteredTextArea = {TextCenterX - TextHalfW, TextArea.y, TextHalfW * 2.0f, TextArea.h};

	const float ArtRounding = minimum(2.4f * Scale, ArtRect.w * 0.22f);
	Graphics()->DrawRect(ArtRect.x, ArtRect.y, ArtRect.w, ArtRect.h, WithAlpha(MixColor(Palette.m_Mid, Palette.m_Dark, 0.42f), 0.38f + 0.08f * HoverT), IGraphics::CORNER_ALL, ArtRounding);

	IGraphics::CTextureHandle ArtTexture;
	if(!m_pImpl->m_vArtFrames.empty() && MediaDecoder::GetCurrentFrameTexture(m_pImpl->m_vArtFrames, m_pImpl->m_ArtAnimated, m_pImpl->m_ArtAnimationStart, ArtTexture) && ArtTexture.IsValid())
	{
		DrawRoundedTexture(Graphics(), ArtTexture, ArtRect, ArtRounding, m_pImpl->m_ArtWidth, m_pImpl->m_ArtHeight);
	}
	else
	{
		DrawRoundedFallbackArt(Graphics(), ArtRect, Palette, HoverT, Scale, ArtRounding);
	}

	const CUIRect UiViewRect = HudToUiRect(View, UiScreen, Width, Height);
	const bool TitleHoverAllowed = AllowInteraction || ForcePreview;
	const bool PlayerHovered = TitleHoverAllowed &&
		(IsPointInsideRect(View, MousePos, 1.5f * Scale) || IsPointInsideRect(UiViewRect, UiMousePos, 1.5f * Scale * UiFontScale));
	const std::string TrackTitle = Snapshot.m_Title.empty() ? Localize("No media") : Snapshot.m_Title;
	const bool ShowGameTimer = GameTimer.m_Valid && !PlayerHovered;
	const std::string Title = ShowGameTimer ? GameTimer.m_Text : TrackTitle;
	const std::string Artist = Snapshot.m_Artist.empty() ? Localize("Unknown artist") : Snapshot.m_Artist;
	const float TitleFont = (ShowGameTimer ? 6.6f : 5.25f) * Scale;
	const float ArtistFont = 3.45f * Scale;
	const bool ShowArtist = TextT > 0.38f && ExpandT > 0.42f;
	CUIRect TitleRect = CenteredTextArea;
	TitleRect.h = TitleFont + 1.8f * Scale;
	TitleRect.y = ShowArtist ? View.y + (ShowGameTimer ? 3.4f : 4.0f) * Scale : View.y + (View.h - TitleRect.h) * 0.5f - 0.1f * Scale;
	CUIRect ArtistRect = CenteredTextArea;
	ArtistRect.h = ArtistFont + 1.6f * Scale;
	ArtistRect.y = TitleRect.y + TitleRect.h - 0.9f * Scale;

	const float PositionMs = ForcePreview ? (float)Snapshot.m_PositionMs : m_pImpl->VisualPositionMs(Delta);
	m_pImpl->UpdateVisualizerLevels(this, Snapshot, (int64_t)PositionMs, Delta);

	const int NumBars = 5;
	const float VisualInnerPadX = 0.15f * Scale * WidthScale;
	const float VisualInnerPadY = 0.20f * Scale;
	const float VisualInnerW = maximum(0.0f, VisualRect.w - VisualInnerPadX * 2.0f);
	const float VisualInnerH = maximum(0.0f, VisualRect.h - VisualInnerPadY * 2.0f);
	const float Gap = 0.58f * Scale * WidthScale;
	const float BarW = maximum(0.95f * Scale * WidthScale, (VisualInnerW - Gap * (NumBars - 1)) / maximum(1.0f, (float)NumBars));
	const float BarsTotalW = NumBars * BarW + (NumBars - 1) * Gap;
	const float BarsStartX = VisualRect.x + VisualInnerPadX + maximum(0.0f, (VisualInnerW - BarsTotalW) * 0.5f);
	const float LaneH = maximum(5.2f * Scale, VisualInnerH * 0.94f);
	const float LaneY = VisualRect.y + VisualInnerPadY + (VisualInnerH - LaneH) * 0.5f;
	const float BaseMidY = LaneY + LaneH * 0.5f;
	const bool CenterMode = g_Config.m_BcMusicPlayerVisualizerMode == 1;
	for(int i = 0; i < NumBars; ++i)
	{
		const float BarT = i / maximum(1.0f, (float)(NumBars - 1));
		const float Centered = absolute(BarT * 2.0f - 1.0f);
		const float X = BarsStartX + i * (BarW + Gap);
		const float LaneW = BarW * 0.88f;
		const float LaneX = X + (BarW - LaneW) * 0.5f;
		if(!HasMusic)
		{
			const float PassiveLevel = m_pImpl->m_aVisualizerLevels[minimum<int>(CImpl::VISUALIZER_BARS - 1, round_to_int((i / maximum(1.0f, (float)(NumBars - 1))) * (CImpl::VISUALIZER_BARS - 1)))];
			const float DotSize = maximum(1.00f * Scale, LaneW * (0.28f + PassiveLevel * 0.22f));
			const float DotX = LaneX + (LaneW - DotSize) * 0.5f;
			const float DotY = VisualRect.y + VisualRect.h * 0.5f - DotSize * 0.5f;
			ColorRGBA DotColor = MixColor(Palette.m_Glow, Palette.m_Light, 0.22f + (1.0f - Centered) * 0.58f);
			DotColor = MixColor(DotColor, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), 0.06f + BarT * 0.08f);
			DotColor.a = 0.40f + PassiveLevel * 0.32f + 0.12f * HoverT;
			Graphics()->DrawRect(DotX, DotY, DotSize, DotSize, DotColor, IGraphics::CORNER_ALL, DotSize * 0.5f);
			continue;
		}

		const int SourceIndex = minimum<int>(CImpl::VISUALIZER_BARS - 1, round_to_int((i / maximum(1.0f, (float)(NumBars - 1))) * (CImpl::VISUALIZER_BARS - 1)));
		const float HeightFactor = powf(std::clamp(m_pImpl->m_aVisualizerLevels[SourceIndex], 0.0f, 1.0f), 0.58f);
		const float H = CenterMode ?
			minimum(LaneH, maximum(3.0f * Scale, VisualInnerH * (0.22f + HeightFactor * 1.02f))) :
			minimum(LaneH, maximum(4.0f * Scale, VisualInnerH * (0.30f + HeightFactor * 1.12f)));
		const float Y = CenterMode ? (BaseMidY - H * 0.5f) : (LaneY + LaneH - H);
		const float LightT = 0.18f + (1.0f - Centered) * 0.52f;
		ColorRGBA BarColor = MixColor(Palette.m_Glow, Palette.m_Light, LightT);
		BarColor = MixColor(BarColor, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), 0.05f + BarT * 0.10f);
		if(Snapshot.m_HasVisualizer && Snapshot.m_Visualizer.m_IsPassiveFallback)
			BarColor = MixColor(BarColor, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), 0.03f);
		BarColor.a = 0.72f + 0.16f * HoverT;
		ColorRGBA LaneColor = WithAlpha(MixColor(Palette.m_Dark, Palette.m_Glow, 0.24f + 0.10f * (1.0f - Centered)), 0.12f + 0.06f * HoverT);
		Graphics()->DrawRect(LaneX, LaneY, LaneW, LaneH, LaneColor, IGraphics::CORNER_ALL, minimum(LaneW, LaneH) * 0.5f);
		Graphics()->DrawRect(LaneX, Y, LaneW, H, BarColor, IGraphics::CORNER_ALL, minimum(LaneW, H) * 0.5f);
	}

	const float ControlsYOffset = (1.0f - ControlsT) * 0.75f * Scale;
	const float ControlsCenterX = View.x + View.w * 0.5f;
	const float ButtonY = View.y + View.h - 7.2f * Scale + ControlsYOffset;
	CUIRect PrevRect = {ControlsCenterX - 10.05f * Scale * WidthScale, ButtonY, 4.8f * Scale * WidthScale, 4.8f * Scale};
	CUIRect PlayRect = {ControlsCenterX - 3.45f * Scale * WidthScale, ButtonY - 0.7f * Scale, 6.9f * Scale * WidthScale, 6.9f * Scale};
	CUIRect NextRect = {ControlsCenterX + 5.25f * Scale * WidthScale, ButtonY, 4.8f * Scale * WidthScale, 4.8f * Scale};
	const CUIRect UiPrevRect = HudToUiRect(PrevRect, UiScreen, Width, Height);
	const CUIRect UiPlayRect = HudToUiRect(PlayRect, UiScreen, Width, Height);
	const CUIRect UiNextRect = HudToUiRect(NextRect, UiScreen, Width, Height);
	const bool ControlsInteractive = AllowInteraction && ControlsT > 0.45f;
	const bool Clicked = ControlsInteractive && (Ui()->MouseButtonClicked(0) || Input()->KeyPress(KEY_MOUSE_1));
	const bool PrevHovered = ControlsInteractive && Snapshot.m_CanPrev &&
		(IsPointInsideRect(PrevRect, MousePos, 1.2f * Scale) || IsPointInsideRect(UiPrevRect, UiMousePos, 1.2f * Scale * UiFontScale));
	const bool PlayHovered = ControlsInteractive && Snapshot.m_CanPlayPause &&
		(IsPointInsideRect(PlayRect, MousePos, 1.2f * Scale) || IsPointInsideRect(UiPlayRect, UiMousePos, 1.2f * Scale * UiFontScale));
	const bool NextHovered = ControlsInteractive && Snapshot.m_CanNext &&
		(IsPointInsideRect(NextRect, MousePos, 1.2f * Scale) || IsPointInsideRect(UiNextRect, UiMousePos, 1.2f * Scale * UiFontScale));

	if(Clicked && PrevHovered)
		m_pImpl->m_pProvider->Previous();
	if(Clicked && PlayHovered)
		m_pImpl->m_pProvider->PlayPause();
	if(Clicked && NextHovered)
		m_pImpl->m_pProvider->Next();

	const CUIRect UiTextArea = HudToUiRect(CenteredTextArea, UiScreen, Width, Height);
	const CUIRect UiTitleRect = HudToUiRect(TitleRect, UiScreen, Width, Height);
	const CUIRect UiArtistRect = HudToUiRect(ArtistRect, UiScreen, Width, Height);

	SLabelProperties Props;
	Props.m_EllipsisAtEnd = true;
	Props.m_MaxWidth = UiTextArea.w;

	Ui()->MapScreen();
	ColorRGBA TitleColor = WithAlpha(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), 0.98f);
	if(ShowGameTimer && GameTimer.m_Warning)
		TitleColor = ColorRGBA(1.0f, 0.25f, 0.25f, GameTimer.m_Blink ? 0.5f : 1.0f);
	TextRender()->TextColor(TitleColor);
	const float UiTitleFont = TitleFont * UiFontScale;
	const float TitleWidth = TextRender()->TextWidth(UiTitleFont, Title.c_str(), -1, -1.0f);
	const float ScrollThreshold = ShowGameTimer ? UiTitleRect.w + 1.0f * UiFontScale : UiTitleRect.w - 2.0f * UiFontScale;
	if(TitleWidth > ScrollThreshold)
	{
		CUIRect ClipRect = UiTitleRect;
		Ui()->ClipEnable(&ClipRect);
		const float Overflow = TitleWidth - ClipRect.w;
		const float PauseTime = 0.65f;
		const float TravelTime = maximum(1.4f, Overflow / maximum(18.0f * UiFontScale, 1.0f));
		const float Segment = TravelTime + PauseTime;
		const float Cycle = Segment * 2.0f;
		const float T = fmodf((float)(time_get() / (double)time_freq()), Cycle);
		float Offset = 0.0f;
		if(T < Segment)
		{
			const float MoveT = std::clamp((T - PauseTime) / maximum(TravelTime, 0.001f), 0.0f, 1.0f);
			Offset = -Overflow * EaseInOutCubic(MoveT);
		}
		else
		{
			const float BackT = T - Segment;
			const float MoveT = std::clamp((BackT - PauseTime) / maximum(TravelTime, 0.001f), 0.0f, 1.0f);
			Offset = -Overflow * (1.0f - EaseInOutCubic(MoveT));
		}
		TextRender()->Text(ClipRect.x + Offset, ClipRect.y + (ClipRect.h - UiTitleFont) * 0.5f, UiTitleFont, Title.c_str(), -1.0f);
		Ui()->ClipDisable();
	}
	else
	{
		Ui()->DoLabel(&UiTitleRect, Title.c_str(), UiTitleFont, TEXTALIGN_MC, Props);
	}
	if(ShowArtist)
	{
		TextRender()->TextColor(WithAlpha(MixColor(Palette.m_Light, ColorRGBA(0.78f, 0.81f, 0.86f, 1.0f), 0.35f), 0.94f * TextT));
		Ui()->DoLabel(&UiArtistRect, Artist.c_str(), ArtistFont * UiFontScale, TEXTALIGN_MC, Props);
	}
	if(ControlsT > 0.001f)
	{
		auto RenderButtonIcon = [&](const CUIRect &Rect, const char *pIcon, bool Enabled, bool Hovered) {
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			ColorRGBA IconColor = Enabled ? MixColor(ColorRGBA(0.90f, 0.92f, 0.96f, 1.0f), ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), Hovered ? 0.65f : 0.0f) : ColorRGBA(0.46f, 0.49f, 0.54f, 1.0f);
			IconColor.a = Enabled ? ControlsT * (Hovered ? 1.0f : 0.88f) : ControlsT * 0.72f;
			TextRender()->TextColor(IconColor);
			Ui()->DoLabel(&Rect, pIcon, Rect.h * (Hovered ? 0.98f : 0.92f), TEXTALIGN_MC);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		};
		RenderButtonIcon(UiPrevRect, FontIcon::BACKWARD_STEP, Snapshot.m_CanPrev, PrevHovered);
		RenderButtonIcon(UiPlayRect, Snapshot.m_PlaybackState == EMusicPlaybackState::PLAYING ? FontIcon::PAUSE : FontIcon::PLAY, Snapshot.m_CanPlayPause, PlayHovered);
		RenderButtonIcon(UiNextRect, FontIcon::FORWARD_STEP, Snapshot.m_CanNext, NextHovered);
	}

	Graphics()->MapScreen(0.0f, 0.0f, Width, Height);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
}
