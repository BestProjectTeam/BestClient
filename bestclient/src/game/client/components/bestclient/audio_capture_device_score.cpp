/* (c) BestClient */
#include "audio_capture_device_score.h"

#include <base/system.h>

#include <cstring>

namespace
{
static bool StrContainsNoCase(const char *pHaystack, const char *pNeedle)
{
	if(pHaystack == nullptr || pNeedle == nullptr)
		return false;
	return str_find_nocase(pHaystack, pNeedle) != nullptr;
}

static bool StrContainsCaseUtf8Bytes(const char *pHaystack, const char *pNeedle)
{
	if(pHaystack == nullptr || pNeedle == nullptr)
		return false;
	return std::strstr(pHaystack, pNeedle) != nullptr;
}
}

int ScoreCaptureDeviceName(const char *pName)
{
	if(pName == nullptr || pName[0] == '\0')
		return -10000;

	int Score = 0;

	// Strong positive (common loopback/monitor names, ASCII only).
	if(StrContainsNoCase(pName, "stereo mix"))
		Score += 80;
	if(StrContainsNoCase(pName, "loopback"))
		Score += 80;
	if(StrContainsNoCase(pName, "monitor"))
		Score += 70;
	if(StrContainsNoCase(pName, "what u hear") || StrContainsNoCase(pName, "what you hear"))
		Score += 70;
	if(StrContainsNoCase(pName, "wave out mix"))
		Score += 70;
	if(StrContainsNoCase(pName, "blackhole"))
		Score += 60;
	if(StrContainsNoCase(pName, "soundflower"))
		Score += 60;

	// Strong positive (RU-localized substrings; case-sensitive UTF-8 byte search).
	if(StrContainsCaseUtf8Bytes(pName, "монитор") || StrContainsCaseUtf8Bytes(pName, "Монитор"))
		Score += 70;
	if(StrContainsCaseUtf8Bytes(pName, "микшер") || StrContainsCaseUtf8Bytes(pName, "Микшер"))
		Score += 70;

	// Soft positive: stereo-ish naming.
	if(StrContainsNoCase(pName, "stereo"))
		Score += 15;
	if(StrContainsCaseUtf8Bytes(pName, "стерео") || StrContainsCaseUtf8Bytes(pName, "Стерео"))
		Score += 15;
	// "Analog stereo" is commonly how OSes label default output sinks; if it shows up in capture devices,
	// it's often the best available "system output" source even if it lacks explicit "monitor" wording.
	if((StrContainsNoCase(pName, "analog") || StrContainsCaseUtf8Bytes(pName, "Аналоговый") || StrContainsCaseUtf8Bytes(pName, "аналоговый")) &&
		(StrContainsNoCase(pName, "stereo") || StrContainsCaseUtf8Bytes(pName, "стерео") || StrContainsCaseUtf8Bytes(pName, "Стерео")))
		Score += 35;

	// Negative: mic-ish / mono-ish.
	if(StrContainsNoCase(pName, "microphone") || StrContainsNoCase(pName, "mic"))
		Score -= 60;
	if(StrContainsNoCase(pName, "mono"))
		Score -= 25;
	if(StrContainsCaseUtf8Bytes(pName, "микрофон") || StrContainsCaseUtf8Bytes(pName, "Микрофон"))
		Score -= 70;
	if(StrContainsCaseUtf8Bytes(pName, "Моно") || StrContainsCaseUtf8Bytes(pName, "моно"))
		Score -= 25;
	if(StrContainsCaseUtf8Bytes(pName, "вход") || StrContainsCaseUtf8Bytes(pName, "Вход"))
		Score -= 10;

	return Score;
}

bool IsSystemLoopbackDeviceName(const char *pName)
{
	static constexpr int MIN_ACCEPT_SCORE = 20;
	return ScoreCaptureDeviceName(pName) >= MIN_ACCEPT_SCORE;
}

