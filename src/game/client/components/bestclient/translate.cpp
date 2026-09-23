/* Copyright © 2026 BestProject Team */
#include "translate.h"

#include <base/dbg.h>
#include <base/log.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#include <engine/shared/protocol.h>

#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <string>

namespace
{
constexpr int64_t TRANSLATE_TIMEOUT = 20;

void UrlEncode(const char *pText, char *pOut, size_t Length)
{
	if(!pText || Length == 0)
		return;

	size_t OutPos = 0;
	for(const char *p = pText; *p && OutPos + 1 < Length; ++p)
	{
		const unsigned char c = (unsigned char)*p;
		if(isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
		{
			pOut[OutPos++] = c;
		}
		else
		{
			if(OutPos + 3 >= Length)
				break;
			snprintf(pOut + OutPos, 4, "%%%02X", c);
			OutPos += 3;
		}
	}
	pOut[OutPos] = '\0';
}

void NormalizeLanguageCode(const char *pLanguage, char *pOut, size_t Size)
{
	if(!pOut || Size == 0)
		return;
	pOut[0] = '\0';
	if(!pLanguage || !pLanguage[0])
		return;

	char aLower[32] = "";
	int Length = 0;
	for(const char *p = pLanguage; *p && Length < (int)sizeof(aLower) - 1; ++p)
	{
		const unsigned char c = (unsigned char)*p;
		if(c == '-' || c == '_' || c == ';' || c == ',' || c == '|' || isspace(c))
			break;
		if(isalnum(c))
			aLower[Length++] = (char)tolower(c);
	}
	aLower[Length] = '\0';

	struct SMapping
	{
		const char *m_pFrom;
		const char *m_pTo;
	};
	static const SMapping s_aMappings[] = {
		{"auto", "auto"},
		{"ru", "ru"}, {"russian", "ru"},
		{"en", "en"}, {"english", "en"},
		{"de", "de"}, {"german", "de"},
		{"fr", "fr"}, {"french", "fr"},
		{"es", "es"}, {"spanish", "es"},
		{"pl", "pl"}, {"polish", "pl"},
		{"zh", "zh"}, {"cn", "zh"}, {"chinese", "zh"},
		{"pt", "pt"}, {"portuguese", "pt"}, {"brazilian", "pt"},
		{"tr", "tr"}, {"turkish", "tr"},
	};
	for(const SMapping &Mapping : s_aMappings)
	{
		if(str_comp(aLower, Mapping.m_pFrom) == 0)
		{
			str_copy(pOut, Mapping.m_pTo, Size);
			return;
		}
	}
}

bool IsAutoLanguage(const char *pLanguage)
{
	return !pLanguage || !pLanguage[0] || str_comp_nocase(pLanguage, "auto") == 0;
}

bool LanguagesEqual(const char *pA, const char *pB)
{
	if(IsAutoLanguage(pA) || IsAutoLanguage(pB))
		return false;
	return str_comp_nocase(pA, pB) == 0;
}

bool IsApostropheCodepoint(int Code)
{
	return Code == '\'' || Code == 0x2018 || Code == 0x2019 || Code == 0x02BC;
}

void StripApostrophesFromTranslatedText(char *pText)
{
	char *pWrite = pText;
	const char *pRead = pText;
	while(*pRead)
	{
		const char *pBefore = pRead;
		const int Code = str_utf8_decode(&pRead);
		if(Code < 0)
			break;
		if(IsApostropheCodepoint(Code))
			continue;
		while(pBefore < pRead)
			*pWrite++ = *pBefore++;
	}
	*pWrite = '\0';
}

void NormalizeTranslatedText(char *pText, size_t Size)
{
	if(!pText || !pText[0])
		return;

	std::string Decoded;
	bool HasEscape = false;
	for(size_t i = 0; pText[i]; ++i)
	{
		if(pText[i] == '%' && pText[i + 1] && pText[i + 2] && isxdigit((unsigned char)pText[i + 1]) && isxdigit((unsigned char)pText[i + 2]))
		{
			const char aHex[] = {pText[i + 1], pText[i + 2], '\0'};
			Decoded.push_back((char)strtol(aHex, nullptr, 16));
			HasEscape = true;
			i += 2;
		}
		else if(pText[i] == '+')
			Decoded.push_back(' ');
		else
			Decoded.push_back(pText[i]);
	}
	if(HasEscape)
		str_copy(pText, Decoded.c_str(), Size);
}

bool ExtractOutgoingCommand(const char *pText, const char *pCommand, const char **ppArguments)
{
	if(!pText || pText[0] != '/' || !ppArguments)
		return false;
	const char *pStart = pText + 1;
	const char *pEnd = pStart;
	while(*pEnd && !isspace((unsigned char)*pEnd))
		++pEnd;
	if((size_t)(pEnd - pStart) != str_length(pCommand) || str_comp_nocase(std::string(pStart, pEnd - pStart).c_str(), pCommand) != 0)
		return false;
	const char *pArgumentsStart = pEnd;
	while(isspace((unsigned char)*pArgumentsStart))
		++pArgumentsStart;
	*ppArguments = pArgumentsStart;
	return *pArgumentsStart != '\0';
}

bool ExtractOutgoingTranslatableText(const CGameClient &GameClient, const char *pText, char *pPrefix, size_t PrefixSize, char *pBody, size_t BodySize)
{
	pPrefix[0] = '\0';
	pBody[0] = '\0';
	if(!pText || !pText[0])
		return false;

	if(pText[0] == '/')
	{
		const char *pArguments = nullptr;
		if(ExtractOutgoingCommand(pText, "c", &pArguments))
		{
			const int PrefixLength = (int)(pArguments - pText);
			str_copy(pPrefix, std::string(pText, PrefixLength).c_str(), PrefixSize);
			str_copy(pBody, pArguments, BodySize);
			return pBody[0] != '\0';
		}

		if(ExtractOutgoingCommand(pText, "w", &pArguments) || ExtractOutgoingCommand(pText, "whisper", &pArguments))
		{
			const char *pBodyStart = str_find(pArguments, " ");
			if(!pBodyStart)
				return false;
			while(isspace((unsigned char)*pBodyStart))
				++pBodyStart;
			if(!pBodyStart[0])
				return false;
			const int PrefixLength = (int)(pBodyStart - pText);
			str_copy(pPrefix, std::string(pText, PrefixLength).c_str(), PrefixSize);
			str_copy(pBody, pBodyStart, BodySize);
			return true;
		}
		return false;
	}

	const char *pColon = str_find(pText, ":");
	if(pColon && pColon != pText)
	{
		const int NameLength = (int)(pColon - pText);
		for(const auto &Client : GameClient.m_aClients)
		{
			if(!Client.m_Active || str_length(Client.m_aName) != NameLength)
				continue;
			char aName[64] = "";
			str_copy(aName, std::string(pText, NameLength).c_str(), sizeof(aName));
			if(str_comp_nocase(Client.m_aName, aName) != 0)
				continue;
			const char *pBodyStart = pColon + 1;
			while(isspace((unsigned char)*pBodyStart))
				++pBodyStart;
			if(!pBodyStart[0])
				return false;
			str_copy(pPrefix, std::string(pText, pBodyStart - pText).c_str(), PrefixSize);
			str_copy(pBody, pBodyStart, BodySize);
			return true;
		}
	}

	str_copy(pBody, pText, BodySize);
	return pBody[0] != '\0';
}
}

const char *ITranslateBackend::EncodeTarget(const char *pTarget) const
{
	return pTarget && pTarget[0] ? pTarget : DefaultConfig::TcTranslateTarget;
}

class ITranslateBackendHttp : public ITranslateBackend
{
protected:
	using ITranslateBackend::ITranslateBackend;
	std::shared_ptr<IHttpRequest> m_pHttpRequest;
	virtual bool ParseResponse(CTranslateResponse &Out) = 0;
	virtual bool ParseHttpError() const { return false; }

	void CreateHttpRequest(IHttp &Http, const char *pUrl)
	{
		auto pRequest = HttpGet(pUrl);
		pRequest->LogProgress(HTTPLOG::FAILURE);
		pRequest->FailOnErrorStatus(false);
		pRequest->Timeout(CTimeout{20000, 0, 500, 10});
		m_pHttpRequest = std::move(pRequest);
		Http.Run(m_pHttpRequest);
	}

	void CreateHttpRequestPost(IHttp &Http, const char *pUrl, const char *pBody)
	{
		auto pRequest = HttpGet(pUrl);
		pRequest->LogProgress(HTTPLOG::FAILURE);
		pRequest->FailOnErrorStatus(false);
		pRequest->Timeout(CTimeout{20000, 0, 500, 10});
		pRequest->HeaderString("Content-Type", "application/x-www-form-urlencoded");
		pRequest->Post((const unsigned char *)pBody, str_length(pBody));
		m_pHttpRequest = std::move(pRequest);
		Http.Run(m_pHttpRequest);
	}

public:
	std::optional<bool> Update(CTranslateResponse &Out) override
	{
		dbg_assert(m_pHttpRequest != nullptr, "m_pHttpRequest is nullptr");
		if(m_pHttpRequest->State() == EHttpState::RUNNING || m_pHttpRequest->State() == EHttpState::QUEUED)
			return std::nullopt;
		if(m_pHttpRequest->State() == EHttpState::ABORTED)
		{
			str_copy(Out.m_Text, "Aborted");
			return false;
		}
		if(m_pHttpRequest->State() != EHttpState::DONE)
		{
			str_copy(Out.m_Text, "Curl error, see console");
			return false;
		}
		if(m_pHttpRequest->StatusCode() != 200 && !ParseHttpError())
		{
			str_format(Out.m_Text, sizeof(Out.m_Text), "Got http code %d", m_pHttpRequest->StatusCode());
			return false;
		}
		return ParseResponse(Out);
	}

	~ITranslateBackendHttp() override
	{
		if(m_pHttpRequest)
			m_pHttpRequest->Abort();
	}
};

class CTranslateBackendLibretranslate : public ITranslateBackendHttp
{
	bool ParseResponseJson(const json_value *pObj, CTranslateResponse &Out)
	{
		if(!pObj || pObj->type != json_object)
		{
			str_copy(Out.m_Text, "Response is not a JSON object");
			return false;
		}
		const json_value *pError = json_object_get(pObj, "error");
		if(pError != &json_value_none)
		{
			if(pError->type == json_string)
				str_copy(Out.m_Text, pError->u.string.ptr);
			else
				str_copy(Out.m_Text, "LibreTranslate error");
			return false;
		}
		const json_value *pTranslated = json_object_get(pObj, "translatedText");
		if(pTranslated == &json_value_none || pTranslated->type != json_string)
		{
			str_copy(Out.m_Text, "No translatedText");
			return false;
		}
		str_copy(Out.m_Text, pTranslated->u.string.ptr);
		const json_value *pDetected = json_object_get(pObj, "detectedLanguage");
		if(pDetected != &json_value_none && pDetected->type == json_object)
		{
			const json_value *pLanguage = json_object_get(pDetected, "language");
			if(pLanguage != &json_value_none && pLanguage->type == json_string)
				str_copy(Out.m_Language, pLanguage->u.string.ptr);
		}
		NormalizeTranslatedText(Out.m_Text, sizeof(Out.m_Text));
		return Out.m_Text[0] != '\0';
	}

	bool ParseResponse(CTranslateResponse &Out) override
	{
		json_value *pObj = m_pHttpRequest->ResultJson();
		const bool Result = ParseResponseJson(pObj, Out);
		json_value_free(pObj);
		return Result;
	}

	bool ParseHttpError() const override { return true; }

public:
	CTranslateBackendLibretranslate(IHttp &Http, const char *pText, const char *pSource, const char *pTarget, const char *pName) :
		ITranslateBackendHttp(pName)
	{
		CJsonStringWriter Json;
		Json.BeginObject();
		Json.WriteAttribute("q");
		Json.WriteStrValue(pText);
		Json.WriteAttribute("source");
		Json.WriteStrValue(IsAutoLanguage(pSource) ? "auto" : pSource);
		Json.WriteAttribute("target");
		Json.WriteStrValue(EncodeTarget(pTarget));
		Json.WriteAttribute("format");
		Json.WriteStrValue("text");
		if(g_Config.m_TcTranslateKey[0])
		{
			Json.WriteAttribute("api_key");
			Json.WriteStrValue(g_Config.m_TcTranslateKey);
		}
		Json.EndObject();
		CreateHttpRequest(Http, g_Config.m_TcTranslateEndpoint[0] ? g_Config.m_TcTranslateEndpoint : "localhost:5000/translate");
		m_pHttpRequest->PostJson(Json.GetOutputString().c_str());
	}
};

class CTranslateBackendDeepL : public ITranslateBackendHttp
{
	static void EncodeDeepLLanguage(const char *pLanguage, char *pOut, size_t Size, bool Target)
	{
		if(!pOut || Size == 0)
			return;
		pOut[0] = '\0';
		char aNorm[16] = "";
		NormalizeLanguageCode(pLanguage, aNorm, sizeof(aNorm));
		if(!aNorm[0] || str_comp(aNorm, "auto") == 0)
			return;
		if(str_comp(aNorm, "en") == 0)
		{
			str_copy(pOut, Target ? "EN-US" : "EN", Size);
			return;
		}
		if(str_comp(aNorm, "pt") == 0)
		{
			str_copy(pOut, "PT-BR", Size);
			return;
		}
		if(str_comp(aNorm, "uk") == 0)
		{
			str_copy(pOut, "UK", Size);
			return;
		}
		if(str_comp(aNorm, "no") == 0)
		{
			str_copy(pOut, "NB", Size);
			return;
		}
		if(str_length(aNorm) >= 2)
		{
			pOut[0] = (char)toupper((unsigned char)aNorm[0]);
			pOut[1] = (char)toupper((unsigned char)aNorm[1]);
			pOut[2] = '\0';
		}
	}

	bool ParseResponse(CTranslateResponse &Out) override
	{
		json_value *pRoot = m_pHttpRequest->ResultJson();
		if(!pRoot || pRoot->type != json_object)
		{
			str_copy(Out.m_Text, "Unexpected response format");
			json_value_free(pRoot);
			return false;
		}
		const json_value *pMessage = json_object_get(pRoot, "message");
		if(pMessage != &json_value_none && pMessage->type == json_string)
		{
			str_copy(Out.m_Text, pMessage->u.string.ptr);
			json_value_free(pRoot);
			return false;
		}
		const json_value *pTranslations = json_object_get(pRoot, "translations");
		if(pTranslations == &json_value_none || pTranslations->type != json_array || json_array_length(pTranslations) < 1)
		{
			str_copy(Out.m_Text, "No translations");
			json_value_free(pRoot);
			return false;
		}
		const json_value *pEntry = json_array_get(pTranslations, 0);
		if(!pEntry || pEntry->type != json_object)
		{
			str_copy(Out.m_Text, "Invalid translation entry");
			json_value_free(pRoot);
			return false;
		}
		const json_value *pText = json_object_get(pEntry, "text");
		if(pText == &json_value_none || pText->type != json_string)
		{
			str_copy(Out.m_Text, "No translation text");
			json_value_free(pRoot);
			return false;
		}
		str_copy(Out.m_Text, pText->u.string.ptr);
		const json_value *pDetected = json_object_get(pEntry, "detected_source_language");
		if(pDetected != &json_value_none && pDetected->type == json_string)
			str_copy(Out.m_Language, pDetected->u.string.ptr);
		NormalizeTranslatedText(Out.m_Text, sizeof(Out.m_Text));
		json_value_free(pRoot);
		return Out.m_Text[0] != '\0';
	}

	bool ParseHttpError() const override { return true; }

public:
	CTranslateBackendDeepL(IHttp &Http, const char *pText, const char *pSource, const char *pTarget, const char *pName) :
		ITranslateBackendHttp(pName)
	{
		char aTarget[16] = "";
		char aSource[16] = "";
		EncodeDeepLLanguage(pTarget, aTarget, sizeof(aTarget), true);
		EncodeDeepLLanguage(pSource, aSource, sizeof(aSource), false);
		if(!aTarget[0])
		{
			str_copy(aTarget, "EN-US", sizeof(aTarget));
		}

		CJsonStringWriter Json;
		Json.BeginObject();
		Json.WriteAttribute("text");
		Json.BeginArray();
		Json.WriteStrValue(pText);
		Json.EndArray();
		if(aSource[0])
		{
			Json.WriteAttribute("source_lang");
			Json.WriteStrValue(aSource);
		}
		Json.WriteAttribute("target_lang");
		Json.WriteStrValue(aTarget);
		Json.EndObject();

		CreateHttpRequest(Http, g_Config.m_TcTranslateEndpoint[0] ? g_Config.m_TcTranslateEndpoint : "https://api-free.deepl.com/v2/translate");
		if(g_Config.m_TcTranslateKey[0])
		{
			char aAuth[280];
			str_format(aAuth, sizeof(aAuth), "DeepL-Auth-Key %s", g_Config.m_TcTranslateKey);
			m_pHttpRequest->HeaderString("Authorization", aAuth);
		}
		m_pHttpRequest->PostJson(Json.GetOutputString().c_str());
	}
};

class CTranslateBackendGoogle : public ITranslateBackendHttp
{
	bool ParseResponse(CTranslateResponse &Out) override
	{
		json_value *pRoot = m_pHttpRequest->ResultJson();
		if(!pRoot || pRoot->type != json_array || json_array_length(pRoot) < 3)
		{
			str_copy(Out.m_Text, "Unexpected response format");
			json_value_free(pRoot);
			return false;
		}
		const json_value *pSentences = json_array_get(pRoot, 0);
		char aText[sizeof(Out.m_Text)] = "";
		if(pSentences && pSentences->type == json_array)
		{
			for(int i = 0; i < json_array_length(pSentences); ++i)
			{
				const json_value *pSentence = json_array_get(pSentences, i);
				if(!pSentence || pSentence->type != json_array)
					continue;
				const json_value *pTranslated = json_array_get(pSentence, 0);
				if(pTranslated && pTranslated->type == json_string)
					str_append(aText, pTranslated->u.string.ptr);
			}
		}
		const json_value *pLanguage = json_array_get(pRoot, 2);
		if(pLanguage && pLanguage->type == json_string)
			str_copy(Out.m_Language, pLanguage->u.string.ptr);
		str_copy(Out.m_Text, aText[0] ? aText : "Empty translation");
		NormalizeTranslatedText(Out.m_Text, sizeof(Out.m_Text));
		json_value_free(pRoot);
		return aText[0] != '\0';
	}

public:
	CTranslateBackendGoogle(IHttp &Http, const char *pText, const char *pSource, const char *pTarget, const char *pName) :
		ITranslateBackendHttp(pName)
	{
		char aBody[4096];
		str_format(aBody, sizeof(aBody), "client=gtx&sl=%s&dt=t&tl=%s&q=", IsAutoLanguage(pSource) ? "auto" : pSource, EncodeTarget(pTarget));
		UrlEncode(pText, aBody + str_length(aBody), sizeof(aBody) - str_length(aBody));
		CreateHttpRequestPost(Http,
			g_Config.m_TcTranslateEndpoint[0] ? g_Config.m_TcTranslateEndpoint : "https://translate.googleapis.com/translate_a/single",
			aBody);
	}
};

static const std::vector<STranslateLanguage> s_vGoogleLanguages = {
	{"Arabic", "ar"}, {"Bulgarian", "bg"}, {"Chinese (Simplified)", "zh"}, {"Chinese (Traditional)", "zh-TW"},
	{"Croatian", "hr"}, {"Czech", "cs"}, {"Danish", "da"}, {"Dutch", "nl"}, {"English", "en"},
	{"Estonian", "et"}, {"Finnish", "fi"}, {"French", "fr"}, {"German", "de"}, {"Greek", "el"},
	{"Hebrew", "he"}, {"Hindi", "hi"}, {"Hungarian", "hu"}, {"Indonesian", "id"}, {"Italian", "it"},
	{"Japanese", "ja"}, {"Korean", "ko"}, {"Latvian", "lv"}, {"Lithuanian", "lt"}, {"Norwegian", "no"},
	{"Persian", "fa"}, {"Polish", "pl"}, {"Portuguese", "pt"}, {"Romanian", "ro"}, {"Russian", "ru"},
	{"Serbian", "sr"}, {"Slovak", "sk"}, {"Slovenian", "sl"}, {"Spanish", "es"}, {"Swedish", "sv"},
	{"Thai", "th"}, {"Turkish", "tr"}, {"Ukrainian", "uk"}, {"Vietnamese", "vi"},
};

static const std::vector<STranslateLanguage> s_vLibretranslateLanguages = {
	{"Arabic", "ar"}, {"Chinese (Simplified)", "zh"}, {"Czech", "cs"}, {"Dutch", "nl"}, {"English", "en"},
	{"Finnish", "fi"}, {"French", "fr"}, {"German", "de"}, {"Greek", "el"}, {"Hebrew", "he"},
	{"Hindi", "hi"}, {"Hungarian", "hu"}, {"Indonesian", "id"}, {"Italian", "it"}, {"Japanese", "ja"},
	{"Korean", "ko"}, {"Polish", "pl"}, {"Portuguese", "pt"}, {"Russian", "ru"}, {"Spanish", "es"},
	{"Swedish", "sv"}, {"Turkish", "tr"}, {"Ukrainian", "uk"}, {"Vietnamese", "vi"},
};

static const std::vector<STranslateLanguage> s_vDeepLLanguages = {
	{"Arabic", "ar"}, {"Bulgarian", "bg"}, {"Chinese (Simplified)", "zh"}, {"Czech", "cs"}, {"Danish", "da"},
	{"Dutch", "nl"}, {"English", "en"}, {"Estonian", "et"}, {"Finnish", "fi"}, {"French", "fr"},
	{"German", "de"}, {"Greek", "el"}, {"Hungarian", "hu"}, {"Indonesian", "id"}, {"Italian", "it"},
	{"Japanese", "ja"}, {"Korean", "ko"}, {"Latvian", "lv"}, {"Lithuanian", "lt"}, {"Norwegian", "no"},
	{"Polish", "pl"}, {"Portuguese", "pt"}, {"Romanian", "ro"}, {"Russian", "ru"}, {"Slovak", "sk"},
	{"Slovenian", "sl"}, {"Spanish", "es"}, {"Swedish", "sv"}, {"Turkish", "tr"}, {"Ukrainian", "uk"},
};

const std::array<STranslateBackendInfo, 3> g_aTranslateBackends = {{
	{"google", "Google", s_vGoogleLanguages, false},
	{"libretranslate", "LibreTranslate", s_vLibretranslateLanguages, true},
	{"deepl", "DeepL", s_vDeepLLanguages, true},
}};

int TranslateLanguageIndex(const std::vector<STranslateLanguage> &vLanguages, const char *pCode)
{
	for(size_t i = 0; i < vLanguages.size(); ++i)
		if(str_comp_nocase(pCode, vLanguages[i].m_pCode) == 0)
			return (int)i;
	for(size_t i = 0; i < vLanguages.size(); ++i)
		if(str_comp_nocase("en", vLanguages[i].m_pCode) == 0)
			return (int)i;
	return 0;
}

std::unique_ptr<ITranslateBackend> CTranslate::CreateBackend(const char *pText, const char *pSourceLanguage, const char *pTargetLanguage) const
{
	if(IsAutoLanguage(pTargetLanguage))
		return nullptr;
	for(const STranslateBackendInfo &Backend : g_aTranslateBackends)
	{
		if(str_comp_nocase(g_Config.m_TcTranslateBackend, Backend.m_pValue) != 0)
			continue;
		if(str_comp_nocase(Backend.m_pValue, "google") == 0)
			return std::make_unique<CTranslateBackendGoogle>(*Http(), pText, pSourceLanguage, pTargetLanguage, Backend.m_pName);
		if(str_comp_nocase(Backend.m_pValue, "deepl") == 0)
			return std::make_unique<CTranslateBackendDeepL>(*Http(), pText, pSourceLanguage, pTargetLanguage, Backend.m_pName);
		if(str_comp_nocase(Backend.m_pValue, "libretranslate") == 0)
			return std::make_unique<CTranslateBackendLibretranslate>(*Http(), pText, pSourceLanguage, pTargetLanguage, Backend.m_pName);
	}
	return nullptr;
}

const char *CTranslate::IncomingSourceLanguage() const { return g_Config.m_BcTranslateIncomingSource; }
const char *CTranslate::IncomingTargetLanguage() const { return g_Config.m_TcTranslateTarget; }
const char *CTranslate::OutgoingSourceLanguage() const { return g_Config.m_BcTranslateOutgoingSource; }
const char *CTranslate::OutgoingTargetLanguage() const { return g_Config.m_BcTranslateOutgoingTarget; }

bool CTranslate::IsIgnoredIncomingLanguage(const char *pLanguage) const
{
	char aLanguage[16] = "";
	NormalizeLanguageCode(pLanguage, aLanguage, sizeof(aLanguage));
	if(!aLanguage[0] || str_comp(aLanguage, "auto") == 0)
		return false;

	const char *pToken = g_Config.m_BcTranslateIncomingIgnoreLanguages;
	while(*pToken)
	{
		while(*pToken && (*pToken == ';' || *pToken == ',' || *pToken == '|' || isspace((unsigned char)*pToken)))
			++pToken;
		const char *pEnd = pToken;
		while(*pEnd && *pEnd != ';' && *pEnd != ',' && *pEnd != '|' && !isspace((unsigned char)*pEnd))
			++pEnd;
		char aToken[32] = "";
		str_copy(aToken, std::string(pToken, pEnd - pToken).c_str(), sizeof(aToken));
		char aNormalized[16] = "";
		NormalizeLanguageCode(aToken, aNormalized, sizeof(aNormalized));
		if(aNormalized[0] && str_comp(aLanguage, aNormalized) == 0)
			return true;
		pToken = pEnd;
	}
	return false;
}

bool CTranslate::ShouldTranslateOutgoingChat(const char *pText) const
{
	if(!g_Config.m_TcTranslateAutoOutgoing || !pText || !pText[0])
		return false;
	if(pText[0] == '/')
	{
		char aPrefix[MAX_CHAT_LENGTH] = "";
		char aBody[MAX_CHAT_LENGTH] = "";
		if(!ExtractOutgoingTranslatableText(*GameClient(), pText, aPrefix, sizeof(aPrefix), aBody, sizeof(aBody)))
			return false;
	}
	return !IsAutoLanguage(OutgoingTargetLanguage()) && !LanguagesEqual(OutgoingSourceLanguage(), OutgoingTargetLanguage());
}

bool CTranslate::CanStartRequest() const
{
	if(time() < m_NextRequestTime)
		return false;
	int ActiveRequests = 0;
	for(const CTranslateJob &Job : m_vJobs)
		ActiveRequests += Job.m_pBackend != nullptr;
	return ActiveRequests < 2;
}

void CTranslate::TranslateLine(CChat::CLine &Line, bool RespectIgnoredIncomingLanguages)
{
	if(m_vJobs.size() >= 16 || !CanStartRequest())
		return;
	auto pBackend = CreateBackend(Line.m_aText, IncomingSourceLanguage(), IncomingTargetLanguage());
	if(!pBackend)
		return;

	CTranslateJob Job;
	Job.m_Type = CTranslateJob::EType::CHAT_LINE;
	Job.m_pLine = &Line;
	Job.m_pTranslateResponse = std::make_shared<CTranslateResponse>();
	Job.m_RespectIgnoredIncomingLanguages = RespectIgnoredIncomingLanguages;
	Job.m_pBackend = std::move(pBackend);
	Job.m_Deadline = time() + time_freq() * TRANSLATE_TIMEOUT;
	Line.m_pTranslateResponse = Job.m_pTranslateResponse;
	m_NextRequestTime = time() + time_freq();
	m_vJobs.emplace_back(std::move(Job));
}

void CTranslate::OnConsoleInit()
{
	if(g_Config.m_TcTranslateAuto && !g_Config.m_TcTranslateAutoIncoming)
	{
		g_Config.m_TcTranslateAutoIncoming = 1;
		g_Config.m_TcTranslateAuto = 0;
	}
	if(g_Config.m_TcTranslateOutgoing && !g_Config.m_TcTranslateAutoOutgoing)
	{
		g_Config.m_TcTranslateAutoOutgoing = 1;
		g_Config.m_TcTranslateOutgoing = 0;
	}
	if(g_Config.m_BcTranslateOutgoingTarget[0] == '\0' || (str_comp(g_Config.m_BcTranslateOutgoingTarget, "en") == 0 && str_comp(g_Config.m_TcTranslateOutgoingTarget, "en") != 0))
		str_copy(g_Config.m_BcTranslateOutgoingTarget, g_Config.m_TcTranslateOutgoingTarget);
}

void CTranslate::AutoTranslate(CChat::CLine &Line)
{
	if(!g_Config.m_TcTranslateAutoIncoming || Line.m_ClientId < 0 || Line.m_aName[0] == '\0')
		return;
	for(const int LocalId : GameClient()->m_aLocalIds)
		if(LocalId >= 0 && LocalId == Line.m_ClientId)
			return;
	if(LanguagesEqual(IncomingSourceLanguage(), IncomingTargetLanguage()))
		return;
	TranslateLine(Line, true);
}

bool CTranslate::ChatDoTranslateOutgoing(int Team, const char *pText)
{
	if(!ShouldTranslateOutgoingChat(pText))
		return false;
	if(!CanStartRequest())
	{
		GameClient()->m_Chat.Echo(Localize("Translation queue is busy; message was not sent"));
		return true;
	}
	if(m_vJobs.size() >= 16)
	{
		GameClient()->m_Chat.Echo(Localize("Translation queue is full"));
		return true;
	}

	char aPrefix[MAX_CHAT_LENGTH] = "";
	char aBody[MAX_CHAT_LENGTH] = "";
	if(!ExtractOutgoingTranslatableText(*GameClient(), pText, aPrefix, sizeof(aPrefix), aBody, sizeof(aBody)))
		return false;

	auto pBackend = CreateBackend(aBody, OutgoingSourceLanguage(), OutgoingTargetLanguage());
	if(!pBackend)
	{
		GameClient()->m_Chat.Echo(Localize("Invalid translate backend"));
		return true;
	}

	CTranslateJob Job;
	Job.m_Type = CTranslateJob::EType::OUTGOING_CHAT;
	Job.m_Team = Team;
	Job.m_pTranslateResponse = std::make_shared<CTranslateResponse>();
	Job.m_pBackend = std::move(pBackend);
	Job.m_Deadline = time() + time_freq() * TRANSLATE_TIMEOUT;
	str_copy(Job.m_aTextToTranslate, aBody);
	str_copy(Job.m_aOutgoingPrefix, aPrefix);
	m_NextRequestTime = time() + time_freq();
	m_vJobs.emplace_back(std::move(Job));
	return true;
}

void CTranslate::ToggleShiftClick()
{
	const int Target = std::clamp(g_Config.m_BcTranslateShiftClickTarget, 0, 2);
	if(Target == 0)
		g_Config.m_TcTranslateAutoIncoming ^= 1;
	else if(Target == 1)
		g_Config.m_TcTranslateAutoOutgoing ^= 1;
	else if(g_Config.m_TcTranslateAutoIncoming && g_Config.m_TcTranslateAutoOutgoing)
	{
		g_Config.m_TcTranslateAutoIncoming = 0;
		g_Config.m_TcTranslateAutoOutgoing = 0;
	}
	else
	{
		g_Config.m_TcTranslateAutoIncoming = 1;
		g_Config.m_TcTranslateAutoOutgoing = 1;
	}
}

void CTranslate::OnRender()
{
	const int64_t Now = time();
	auto ForEach = [&](CTranslateJob &Job) {
		const bool TimedOut = Now >= Job.m_Deadline;
		if(Job.m_Type == CTranslateJob::EType::OUTGOING_CHAT)
		{
			if(TimedOut)
			{
				GameClient()->m_Chat.Echo(Localize("Translation timed out; message was not sent"));
				return true;
			}
			const std::optional<bool> Done = Job.m_pBackend->Update(*Job.m_pTranslateResponse);
			if(!Done.has_value())
				return false;
			if(!*Done || !Job.m_pTranslateResponse->m_Text[0])
			{
				char aBuf[sizeof(Job.m_pTranslateResponse->m_Text) + 96];
				str_format(aBuf, sizeof(aBuf), Localize("Translation failed; message was not sent: %s"), Job.m_pTranslateResponse->m_Text);
				GameClient()->m_Chat.Echo(aBuf);
				return true;
			}
			if(g_Config.m_BcTranslateOutgoingStripPunctuation)
				StripApostrophesFromTranslatedText(Job.m_pTranslateResponse->m_Text);
			char aText[MAX_CHAT_LENGTH] = "";
			if(Job.m_aOutgoingPrefix[0])
			{
				str_copy(aText, Job.m_aOutgoingPrefix, sizeof(aText));
				str_append(aText, Job.m_pTranslateResponse->m_Text, sizeof(aText));
			}
			else
				str_copy(aText, Job.m_pTranslateResponse->m_Text, sizeof(aText));
			if(aText[0])
				GameClient()->m_Chat.SendTranslatedChatQueued(Job.m_Team, aText);
			else
				GameClient()->m_Chat.Echo(Localize("Translation failed; message was not sent"));
			return true;
		}

		if(!Job.m_pLine || Job.m_pLine->m_pTranslateResponse != Job.m_pTranslateResponse)
			return true;
		if(TimedOut)
		{
			str_copy(Job.m_pTranslateResponse->m_Text, Localize("Translation timed out"));
			Job.m_pTranslateResponse->m_Error = true;
		}
		else
		{
			const std::optional<bool> Done = Job.m_pBackend->Update(*Job.m_pTranslateResponse);
			if(!Done.has_value())
				return false;
			if(*Done)
			{
				if(Job.m_RespectIgnoredIncomingLanguages && IsIgnoredIncomingLanguage(Job.m_pTranslateResponse->m_Language))
					Job.m_pTranslateResponse->m_Text[0] = '\0';
				if(str_comp_nocase(Job.m_pLine->m_aText, Job.m_pTranslateResponse->m_Text) == 0)
					Job.m_pTranslateResponse->m_Text[0] = '\0';
			}
			else
			{
				char aBuf[sizeof(Job.m_pTranslateResponse->m_Text)];
				str_format(aBuf, sizeof(aBuf), Localize("Translation failed: %s"), Job.m_pTranslateResponse->m_Text);
				Job.m_pTranslateResponse->m_Error = true;
				str_copy(Job.m_pTranslateResponse->m_Text, aBuf);
			}
		}
		Job.m_pLine->m_Time = Now;
		GameClient()->m_Chat.RebuildChat();
		return true;
	};
	m_vJobs.erase(std::remove_if(m_vJobs.begin(), m_vJobs.end(), ForEach), m_vJobs.end());
}
