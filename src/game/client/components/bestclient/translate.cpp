/* Copyright © 2026 BestProject Team */
#include "translate.h"

#include <base/log.h>

#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#include <engine/shared/protocol.h>

#include <game/client/gameclient.h>
#include <game/client/lineinput.h>
#include <game/localization.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <string_view>

static void UrlEncode(const char *pText, char *pOut, size_t Length)
{
	if(Length == 0)
		return;
	size_t OutPos = 0;
	for(const char *p = pText; *p && OutPos < Length - 1; ++p)
	{
		unsigned char c = *(const unsigned char *)p;
		if(isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
		{
			if(OutPos >= Length - 1)
				break;
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

namespace
{
void NormalizeLanguageCode(const char *pLanguage, char *pOut, size_t Size)
{
	if(!pOut || Size == 0)
		return;
	pOut[0] = '\0';
	if(!pLanguage || pLanguage[0] == '\0')
		return;

	char aLower[32] = "";
	int Out = 0;
	for(const char *p = pLanguage; *p != '\0' && Out < (int)sizeof(aLower) - 1; ++p)
	{
		const unsigned char c = (unsigned char)*p;
		if(c == '-' || c == '_' || c == ';' || c == ',' || c == '|' || std::isspace(c))
			break;
		if(std::isalnum(c))
			aLower[Out++] = (char)std::tolower(c);
	}
	aLower[Out] = '\0';
	if(aLower[0] == '\0')
		return;

	const struct
	{
		const char *m_pIn;
		const char *m_pOut;
	} aMappings[] = {
		{"auto", "auto"},
		{"ru", "ru"},
		{"russian", "ru"},
		{"en", "en"},
		{"english", "en"},
		{"de", "de"},
		{"german", "de"},
		{"fr", "fr"},
		{"french", "fr"},
		{"es", "es"},
		{"spanish", "es"},
		{"zh", "zh"},
		{"cn", "zh"},
		{"chinese", "zh"},
		{"mandarin", "zh"},
		{"pt", "pt"},
		{"portuguese", "pt"},
		{"brazilian", "pt"},
		{"brasileiro", "pt"},
		{"tr", "tr"},
		{"turkish", "tr"},
	};

	for(const auto &Mapping : aMappings)
	{
		if(str_comp(aLower, Mapping.m_pIn) == 0)
		{
			str_copy(pOut, Mapping.m_pOut, Size);
			return;
		}
	}
}

std::string UrlDecode(std::string_view Encoded)
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

void NormalizeTranslatedText(char *pText, size_t Size)
{
	if(!pText || Size == 0 || pText[0] == '\0')
		return;

	int EscapedSequenceCount = 0;
	for(const char *p = pText; p[0] != '\0' && p[1] != '\0' && p[2] != '\0'; ++p)
	{
		if(p[0] == '%' && std::isxdigit((unsigned char)p[1]) && std::isxdigit((unsigned char)p[2]))
			++EscapedSequenceCount;
	}

	if(EscapedSequenceCount == 0)
		return;

	const bool LooksEncoded = EscapedSequenceCount >= 2 || str_find(pText, "%20") || str_find(pText, "%3D") || str_find(pText, "%2F") || str_find(pText, "%3A");
	if(!LooksEncoded)
		return;

	const std::string Decoded = UrlDecode(pText);
	if(Decoded.empty() || str_comp(Decoded.c_str(), pText) == 0)
		return;

	str_copy(pText, Decoded.c_str(), Size);
}

bool IsAutoLanguage(const char *pLanguage)
{
	return pLanguage == nullptr || pLanguage[0] == '\0' || str_comp_nocase(pLanguage, "auto") == 0;
}

bool HasUrlEncodedSequence(const char *pText)
{
	if(!pText)
		return false;

	for(const char *p = pText; p[0] != '\0' && p[1] != '\0' && p[2] != '\0'; ++p)
	{
		if(p[0] == '%' && std::isxdigit((unsigned char)p[1]) && std::isxdigit((unsigned char)p[2]))
			return true;
	}
	return false;
}

bool SanitizeOutgoingTranslatedText(const char *pOriginalText, const char *pTranslatedText, char *pOut, size_t OutSize)
{
	if(!pOriginalText || !pOut || OutSize == 0)
		return false;

	pOut[0] = '\0';
	if(!pTranslatedText || pTranslatedText[0] == '\0')
		return false;

	char aNormalized[1024];
	str_copy(aNormalized, pTranslatedText, sizeof(aNormalized));
	for(int i = 0; i < 2; ++i)
	{
		char aBefore[sizeof(aNormalized)];
		str_copy(aBefore, aNormalized, sizeof(aBefore));
		NormalizeTranslatedText(aNormalized, sizeof(aNormalized));
		if(str_comp(aNormalized, aBefore) == 0)
			break;
	}

	if(!str_utf8_check(aNormalized) || *str_utf8_skip_whitespaces(aNormalized) == '\0' || HasUrlEncodedSequence(aNormalized))
		return false;

	// If the translated text doesn't fit, fall back to sending the original message to avoid sending incomplete text.
	if(str_length(aNormalized) > (int)OutSize - 1)
		return false;

	str_copy(pOut, aNormalized, OutSize);
	return true;
}

bool LanguagesEqual(const char *pA, const char *pB)
{
	if(IsAutoLanguage(pA) || IsAutoLanguage(pB))
		return false;
	return str_comp_nocase(pA, pB) == 0;
}

void NormalizeDetectedLanguage(char *pLanguage, size_t Size)
{
	if(!pLanguage || Size == 0 || pLanguage[0] == '\0')
		return;

	char aNormalized[16] = "";
	NormalizeLanguageCode(pLanguage, aNormalized, sizeof(aNormalized));
	str_copy(pLanguage, aNormalized, Size);
}

bool ExtractOutgoingChatPrefix(const CGameClient &GameClient, const char *pText, char *pPrefix, size_t PrefixSize, char *pBody, size_t BodySize)
{
	if(!pPrefix || PrefixSize == 0 || !pBody || BodySize == 0)
		return false;
	pPrefix[0] = '\0';
	pBody[0] = '\0';
	if(!pText || pText[0] == '\0')
		return false;

	const char *pColon = str_find(pText, ":");
	if(!pColon || pColon == pText)
		return false;

	const int NameLength = (int)(pColon - pText);
	const char *pBodyStart = pColon + 1;
	while(*pBodyStart != '\0' && std::isspace((unsigned char)*pBodyStart))
		++pBodyStart;
	if(*pBodyStart == '\0')
		return false;

	for(const auto &Client : GameClient.m_aClients)
	{
		if(!Client.m_Active || Client.m_aName[0] == '\0')
			continue;
		if(str_length(Client.m_aName) != NameLength)
			continue;
		char aNamePrefix[64] = "";
		str_copy(aNamePrefix, std::string(pText, NameLength).c_str(), sizeof(aNamePrefix));
		if(str_comp_nocase(Client.m_aName, aNamePrefix) != 0)
			continue;

		const int PrefixLength = (int)(pBodyStart - pText);
		str_copy(pPrefix, std::string(pText, PrefixLength).c_str(), PrefixSize);
		str_copy(pBody, pBodyStart, BodySize);
		return true;
	}

	return false;
}

bool IsWhisperCommandToken(const char *pToken)
{
	return pToken != nullptr && (str_comp_nocase(pToken, "w") == 0 || str_comp_nocase(pToken, "whisper") == 0);
}

bool ExtractOutgoingWhisperPrefix(const CGameClient &GameClient, const char *pText, char *pPrefix, size_t PrefixSize, char *pBody, size_t BodySize)
{
	if(!pPrefix || PrefixSize == 0 || !pBody || BodySize == 0)
		return false;
	pPrefix[0] = '\0';
	pBody[0] = '\0';
	if(!pText || pText[0] != '/')
		return false;

	const char *pCommand = pText + 1;
	while(*pCommand != '\0' && std::isspace((unsigned char)*pCommand))
		++pCommand;
	if(*pCommand == '\0')
		return false;

	const char *pCommandEnd = pCommand;
	while(*pCommandEnd != '\0' && !std::isspace((unsigned char)*pCommandEnd))
		++pCommandEnd;
	char aCommand[32] = "";
	str_copy(aCommand, std::string(pCommand, pCommandEnd - pCommand).c_str(), sizeof(aCommand));
	if(!IsWhisperCommandToken(aCommand))
		return false;

	const char *pNameStart = pCommandEnd;
	while(*pNameStart != '\0' && std::isspace((unsigned char)*pNameStart))
		++pNameStart;
	if(*pNameStart == '\0')
		return false;

	const char *pBestBodyStart = nullptr;
	int BestNameLength = -1;
	for(const auto &Client : GameClient.m_aClients)
	{
		if(!Client.m_Active || Client.m_aName[0] == '\0')
			continue;

		const int NameLength = str_length(Client.m_aName);
		if(NameLength <= 0 || NameLength <= BestNameLength)
			continue;
		char aNamePrefix[MAX_NAME_LENGTH] = "";
		str_copy(aNamePrefix, std::string(pNameStart, NameLength).c_str(), sizeof(aNamePrefix));
		if(str_comp_nocase(aNamePrefix, Client.m_aName) != 0)
			continue;

		const char *pAfterName = pNameStart + NameLength;
		if(*pAfterName != '\0' && !std::isspace((unsigned char)*pAfterName))
			continue;

		const char *pMessageStart = pAfterName;
		while(*pMessageStart != '\0' && std::isspace((unsigned char)*pMessageStart))
			++pMessageStart;
		if(*pMessageStart == '\0')
			continue;

		BestNameLength = NameLength;
		pBestBodyStart = pMessageStart;
	}

	if(!pBestBodyStart)
		return false;

	const int PrefixLength = (int)(pBestBodyStart - pText);
	str_copy(pPrefix, std::string(pText, PrefixLength).c_str(), PrefixSize);
	str_copy(pBody, pBestBodyStart, BodySize);
	return true;
}

bool ExtractOutgoingTranslatableText(const CGameClient &GameClient, const char *pText, char *pPrefix, size_t PrefixSize, char *pBody, size_t BodySize)
{
	if(ExtractOutgoingWhisperPrefix(GameClient, pText, pPrefix, PrefixSize, pBody, BodySize))
		return true;
	return ExtractOutgoingChatPrefix(GameClient, pText, pPrefix, PrefixSize, pBody, BodySize);
}
}

const char *ITranslateBackend::EncodeSource(const char *pSource) const
{
	return IsAutoLanguage(pSource) ? "auto" : pSource;
}

const char *ITranslateBackend::EncodeTarget(const char *pTarget) const
{
	if(!pTarget || pTarget[0] == '\0')
		return DefaultConfig::TcTranslateTarget;
	return pTarget;
}

bool ITranslateBackend::CompareTargets(const char *pA, const char *pB) const
{
	if(pA == pB) // if(!pA && !pB)
		return true;
	if(!pA || !pB)
		return false;
	if(str_comp_nocase(EncodeTarget(pA), EncodeTarget(pB)) == 0)
		return true;
	return false;
}

class ITranslateBackendHttp : public ITranslateBackend
{
protected:
	std::shared_ptr<CHttpRequest> m_pHttpRequest = nullptr;
	virtual bool ParseResponse(CTranslateResponse &Out) = 0;
	virtual bool ParseHttpError() const { return false; }

	void CreateHttpRequest(IHttp &Http, const char *pUrl)
	{
		auto pGet = std::make_shared<CHttpRequest>(pUrl);
		pGet->LogProgress(HTTPLOG::FAILURE);
		pGet->FailOnErrorStatus(false);
		pGet->Timeout(CTimeout{10000, 0, 500, 10});

		m_pHttpRequest = pGet;
		Http.Run(pGet);
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
private:
	bool ParseResponseJson(const json_value *pObj, CTranslateResponse &Out)
	{
		if(!pObj)
		{
			str_copy(Out.m_Text, "Response is not JSON");
			return false;
		}

		if(pObj->type != json_object)
		{
			str_copy(Out.m_Text, "Response is not object");
			return false;
		}

		const json_value *pError = json_object_get(pObj, "error");
		if(pError != &json_value_none)
		{
			if(pError->type != json_string)
				str_copy(Out.m_Text, "Error is not string");
			else
				str_copy(Out.m_Text, pError->u.string.ptr);
			return false;
		}

		const json_value *pTranslatedText = json_object_get(pObj, "translatedText");
		if(pTranslatedText == &json_value_none)
		{
			str_copy(Out.m_Text, "No translatedText");
			return false;
		}
		if(pTranslatedText->type != json_string)
		{
			str_copy(Out.m_Text, "translatedText is not string");
			return false;
		}

		const json_value *pDetectedLanguage = json_object_get(pObj, "detectedLanguage");
		if(pDetectedLanguage == &json_value_none)
		{
			str_copy(Out.m_Text, "No pDetectedLanguage");
			return false;
		}
		if(pDetectedLanguage->type != json_object)
		{
			str_copy(Out.m_Text, "pDetectedLanguage is not object");
			return false;
		}

		const json_value *pConfidence = json_object_get(pDetectedLanguage, "confidence");
		if(pConfidence == &json_value_none || ((pConfidence->type == json_double && pConfidence->u.dbl == 0.0f) ||
							      (pConfidence->type == json_integer && pConfidence->u.integer == 0)))
		{
			str_copy(Out.m_Text, "Unknown language");
			return false;
		}

		const json_value *pLanguage = json_object_get(pDetectedLanguage, "language");
		if(pLanguage == &json_value_none)
		{
			str_copy(Out.m_Text, "No language");
			return false;
		}
		if(pLanguage->type != json_string)
		{
			str_copy(Out.m_Text, "language is not string");
			return false;
		}

		str_copy(Out.m_Text, pTranslatedText->u.string.ptr);
		NormalizeTranslatedText(Out.m_Text, sizeof(Out.m_Text));
		str_copy(Out.m_Language, pLanguage->u.string.ptr);
		NormalizeDetectedLanguage(Out.m_Language, sizeof(Out.m_Language));

		return true;
	}

protected:
	bool ParseResponse(CTranslateResponse &Out) override
	{
		json_value *pObj = m_pHttpRequest->ResultJson();
		bool Res = ParseResponseJson(pObj, Out);
		json_value_free(pObj);
		return Res;
	}
	bool ParseHttpError() const override { return true; }

public:
	const char *Name() const override
	{
		return "LibreTranslate";
	}
	CTranslateBackendLibretranslate(IHttp &Http, const char *pText, const char *pSourceLanguage, const char *pTargetLanguage)
	{
		CJsonStringWriter Json = CJsonStringWriter();
		Json.BeginObject();
		Json.WriteAttribute("q");
		Json.WriteStrValue(pText);
		Json.WriteAttribute("source");
		Json.WriteStrValue(EncodeSource(pSourceLanguage));
		Json.WriteAttribute("target");
		Json.WriteStrValue(EncodeTarget(pTargetLanguage));
		Json.WriteAttribute("format");
		Json.WriteStrValue("text");
		if(g_Config.m_TcTranslateKey[0] != '\0')
		{
			Json.WriteAttribute("api_key");
			Json.WriteStrValue(g_Config.m_TcTranslateKey);
		}
		Json.EndObject();
		CreateHttpRequest(Http, g_Config.m_TcTranslateEndpoint[0] == '\0' ? "localhost:5000/translate" : g_Config.m_TcTranslateEndpoint);
		const char *pJson = Json.GetOutputString().c_str();
		m_pHttpRequest->PostJson(pJson);
	}
};

class CTranslateBackendFtapi : public ITranslateBackendHttp
{
private:
	bool ParseResponseJson(const json_value *pObj, CTranslateResponse &Out)
	{
		if(!pObj)
		{
			str_copy(Out.m_Text, "Response is not JSON");
			return false;
		}

		if(pObj->type != json_object)
		{
			str_copy(Out.m_Text, "Response is not object");
			return false;
		}

		const json_value *pTranslatedText = json_object_get(pObj, "destination-text");
		if(pTranslatedText == &json_value_none)
		{
			str_copy(Out.m_Text, "No destination-text");
			return false;
		}
		if(pTranslatedText->type != json_string)
		{
			str_copy(Out.m_Text, "destination-text is not string");
			return false;
		}

		const json_value *pDetectedLanguage = json_object_get(pObj, "source-language");
		if(pDetectedLanguage == &json_value_none)
		{
			str_copy(Out.m_Text, "No source-language");
			return false;
		}
		if(pDetectedLanguage->type != json_string)
		{
			str_copy(Out.m_Text, "source-language is not string");
			return false;
		}

		str_copy(Out.m_Text, pTranslatedText->u.string.ptr);
		NormalizeTranslatedText(Out.m_Text, sizeof(Out.m_Text));
		str_copy(Out.m_Language, pDetectedLanguage->u.string.ptr);
		NormalizeDetectedLanguage(Out.m_Language, sizeof(Out.m_Language));

		return true;
	}

protected:
	bool ParseResponse(CTranslateResponse &Out) override
	{
		json_value *pObj = m_pHttpRequest->ResultJson();
		bool Res = ParseResponseJson(pObj, Out);
		json_value_free(pObj);
		return Res;
	}

public:
	const char *EncodeTarget(const char *pTarget) const override
	{
		if(!pTarget || pTarget[0] == '\0')
			return DefaultConfig::TcTranslateTarget;
		if(str_comp_nocase(pTarget, "zh") == 0)
			return "zh-cn";
		return pTarget;
	}
	const char *Name() const override
	{
		return "FreeTranslateAPI";
	}
	CTranslateBackendFtapi(IHttp &Http, const char *pText, const char *pTargetLanguage)
	{
		char aBuf[4096];
		str_format(aBuf, sizeof(aBuf), "%s/translate?dl=%s&text=",
			g_Config.m_TcTranslateEndpoint[0] != '\0' ? g_Config.m_TcTranslateEndpoint : "https://ftapi.pythonanywhere.com",
			EncodeTarget(pTargetLanguage));

		UrlEncode(pText, aBuf + strlen(aBuf), sizeof(aBuf) - strlen(aBuf));

		CreateHttpRequest(Http, aBuf);
	}
};

class CTranslateBackendGoogle : public ITranslateBackendHttp
{
protected:
	bool ParseResponse(CTranslateResponse &Out) override
	{
		json_value *pRoot = m_pHttpRequest->ResultJson();
		if(!pRoot)
		{
			str_copy(Out.m_Text, "Response is not JSON");
			return false;
		}

		bool Success = false;
		if(pRoot->type != json_array)
		{
			str_copy(Out.m_Text, "Response is not array");
		}
		else
		{
			const json_value *pSentences = json_array_get(pRoot, 0);
			if(!pSentences || pSentences->type != json_array)
			{
				str_copy(Out.m_Text, "Missing translation entries");
			}
			else
			{
				std::string Result;
				for(int i = 0; i < json_array_length(pSentences); ++i)
				{
					const json_value *pSentence = json_array_get(pSentences, i);
					if(!pSentence || pSentence->type != json_array)
						continue;

					const json_value *pTranslated = json_array_get(pSentence, 0);
					if(!pTranslated || pTranslated->type != json_string)
						continue;

					Result += pTranslated->u.string.ptr;
				}

				if(Result.empty())
				{
					str_copy(Out.m_Text, "Translation is empty");
				}
				else
				{
					str_copy(Out.m_Text, Result.c_str(), sizeof(Out.m_Text));
					NormalizeTranslatedText(Out.m_Text, sizeof(Out.m_Text));
					const json_value *pDetectedLanguage = json_array_get(pRoot, 2);
					if(pDetectedLanguage && pDetectedLanguage->type == json_string)
						str_copy(Out.m_Language, pDetectedLanguage->u.string.ptr, sizeof(Out.m_Language));
					NormalizeDetectedLanguage(Out.m_Language, sizeof(Out.m_Language));
					Success = true;
				}
			}
		}

		json_value_free(pRoot);
		return Success;
	}

public:
	const char *Name() const override
	{
		return "Google Translate";
	}

	CTranslateBackendGoogle(IHttp &Http, const char *pText, const char *pSourceLanguage, const char *pTargetLanguage)
	{
		char aBuf[4096];
		str_format(aBuf, sizeof(aBuf), "https://translate.googleapis.com/translate_a/single?client=gtx&sl=%s&tl=%s&dt=t&q=",
			EncodeSource(pSourceLanguage), EncodeTarget(pTargetLanguage));
		UrlEncode(pText, aBuf + strlen(aBuf), sizeof(aBuf) - strlen(aBuf));
		CreateHttpRequest(Http, aBuf);
	}
};

void CTranslate::ConTranslate(IConsole::IResult *pResult, void *pUserData)
{
	const char *pName;
	if(pResult->NumArguments() == 0)
		pName = nullptr;
	else
		pName = pResult->GetString(0);

	CTranslate *pThis = static_cast<CTranslate *>(pUserData);
	pThis->Translate(pName);
}

void CTranslate::ConTranslateId(IConsole::IResult *pResult, void *pUserData)
{
	CTranslate *pThis = static_cast<CTranslate *>(pUserData);
	pThis->Translate(pResult->GetInteger(0));
}

void CTranslate::OnConsoleInit()
{
	// Migration from legacy single toggle (tc_translate_auto) to separate toggles.
	if(g_Config.m_TcTranslateAuto && !g_Config.m_TcTranslateAutoIncoming && !g_Config.m_TcTranslateAutoOutgoing)
	{
		g_Config.m_TcTranslateAutoIncoming = 1;
		g_Config.m_TcTranslateAutoOutgoing = 1;
		g_Config.m_TcTranslateAuto = 0;
	}

	// Migration: target language can no longer be "auto".
	if(IsAutoLanguage(g_Config.m_TcTranslateTarget))
		str_copy(g_Config.m_TcTranslateTarget, DefaultConfig::TcTranslateTarget, sizeof(g_Config.m_TcTranslateTarget));
	if(IsAutoLanguage(g_Config.m_BcTranslateOutgoingTarget))
		str_copy(g_Config.m_BcTranslateOutgoingTarget, DefaultConfig::BcTranslateOutgoingTarget, sizeof(g_Config.m_BcTranslateOutgoingTarget));

	Console()->Register("translate", "?r[name]", CFGFLAG_CLIENT, ConTranslate, this, "Translate last message (of a given name)");
	Console()->Register("translate_id", "v[id]", CFGFLAG_CLIENT, ConTranslateId, this, "Translate last message of the person with this id");
}

std::unique_ptr<ITranslateBackend> CTranslate::CreateBackend(const char *pText, const char *pSourceLanguage, const char *pTargetLanguage) const
{
	if(IsAutoLanguage(pTargetLanguage))
		return nullptr;

	if(str_comp_nocase(g_Config.m_TcTranslateBackend, "libretranslate") == 0)
		return std::make_unique<CTranslateBackendLibretranslate>(*Http(), pText, pSourceLanguage, pTargetLanguage);
	if(str_comp_nocase(g_Config.m_TcTranslateBackend, "ftapi") == 0)
		return std::make_unique<CTranslateBackendFtapi>(*Http(), pText, pTargetLanguage);
	if(str_comp_nocase(g_Config.m_TcTranslateBackend, "google") == 0)
		return std::make_unique<CTranslateBackendGoogle>(*Http(), pText, pSourceLanguage, pTargetLanguage);
	return nullptr;
}

const char *CTranslate::IncomingSourceLanguage() const
{
	return g_Config.m_BcTranslateIncomingSource;
}

const char *CTranslate::IncomingTargetLanguage() const
{
	if(IsAutoLanguage(g_Config.m_TcTranslateTarget))
		return DefaultConfig::TcTranslateTarget;
	return g_Config.m_TcTranslateTarget;
}

const char *CTranslate::OutgoingSourceLanguage() const
{
	return g_Config.m_BcTranslateOutgoingSource;
}

const char *CTranslate::OutgoingTargetLanguage() const
{
	if(IsAutoLanguage(g_Config.m_BcTranslateOutgoingTarget))
		return DefaultConfig::BcTranslateOutgoingTarget;
	return g_Config.m_BcTranslateOutgoingTarget;
}

bool CTranslate::IsIgnoredIncomingLanguage(const char *pLanguage) const
{
	if(!pLanguage || pLanguage[0] == '\0' || g_Config.m_BcTranslateIncomingIgnoreLanguages[0] == '\0')
		return false;

	char aLanguage[16] = "";
	NormalizeLanguageCode(pLanguage, aLanguage, sizeof(aLanguage));
	if(aLanguage[0] == '\0' || str_comp(aLanguage, "auto") == 0)
		return false;

	const char *pToken = g_Config.m_BcTranslateIncomingIgnoreLanguages;
	while(*pToken != '\0')
	{
		while(*pToken != '\0' && (*pToken == ';' || *pToken == ',' || *pToken == '|' || std::isspace((unsigned char)*pToken)))
			++pToken;
		if(*pToken == '\0')
			break;

		const char *pTokenEnd = pToken;
		while(*pTokenEnd != '\0' && *pTokenEnd != ';' && *pTokenEnd != ',' && *pTokenEnd != '|' && !std::isspace((unsigned char)*pTokenEnd))
			++pTokenEnd;

		char aToken[32] = "";
		str_copy(aToken, std::string(pToken, pTokenEnd - pToken).c_str(), sizeof(aToken));
		char aNormalizedToken[16] = "";
		NormalizeLanguageCode(aToken, aNormalizedToken, sizeof(aNormalizedToken));
		if(aNormalizedToken[0] != '\0' && str_comp(aLanguage, aNormalizedToken) == 0)
			return true;

		pToken = pTokenEnd;
	}

	return false;
}

bool CTranslate::ShouldTranslateOutgoingChat(const char *pText) const
{
	if(!g_Config.m_TcTranslateAutoOutgoing || !pText || pText[0] == '\0')
		return false;
	if(pText[0] == '/')
	{
		char aPrefix[MAX_LINE_LENGTH] = "";
		char aBody[MAX_LINE_LENGTH] = "";
		if(!ExtractOutgoingWhisperPrefix(*GameClient(), pText, aPrefix, sizeof(aPrefix), aBody, sizeof(aBody)))
			return false;
	}
	if(IsAutoLanguage(OutgoingTargetLanguage()))
		return false;
	if(LanguagesEqual(OutgoingSourceLanguage(), OutgoingTargetLanguage()))
		return false;
	return true;
}

bool CTranslate::HasPendingJobs() const
{
	return !m_vJobs.empty();
}

void CTranslate::Translate(int Id, bool ShowProgress)
{
	if(Id < 0 || Id > (int)std::size(GameClient()->m_aClients))
	{
		GameClient()->m_Chat.Echo("Not a valid ID");
		return;
	}
	const auto &Player = GameClient()->m_aClients[Id];
	if(!Player.m_Active)
	{
		GameClient()->m_Chat.Echo("ID not connected");
		return;
	}
	Translate(Player.m_aName, ShowProgress);
}

void CTranslate::Translate(const char *pName, bool ShowProgress)
{
	CChat::CLine *pLineBest = nullptr;
	if(GameClient()->m_Chat.m_CurrentLine > 0)
	{
		int ScoreBest = -1;
		for(int i = 0; i < CChat::MAX_LINES; i++)
		{
			CChat::CLine *pLine = &GameClient()->m_Chat.m_aLines[((GameClient()->m_Chat.m_CurrentLine - i) + CChat::MAX_LINES) % CChat::MAX_LINES];
			if(pLine->m_pTranslateResponse != nullptr)
				continue;
			if(pLine->m_ClientId == CChat::CLIENT_MSG)
				continue;
			for(int Id : GameClient()->m_aLocalIds)
				if(pLine->m_ClientId == Id)
					continue;
			int Score = 0;
			if(pName)
			{
				if(pLine->m_ClientId == CChat::SERVER_MSG)
					continue;
				if(str_comp(pLine->m_aName, pName) == 0)
					Score = 2;
				else if(str_comp_nocase(pLine->m_aName, pName) == 0)
					Score = 1;
				else
					continue;
			}
			if(Score > ScoreBest)
			{
				ScoreBest = Score;
				pLineBest = pLine;
			}
		}
	}
	if(!pLineBest || pLineBest->m_aText[0] == '\0')
	{
		GameClient()->m_Chat.Echo("No message to translate");
		return;
	}

	Translate(*pLineBest, ShowProgress);
}

void CTranslate::Translate(CChat::CLine &Line, bool ShowProgress)
{
	TranslateLine(Line, ShowProgress, false);
}

void CTranslate::TranslateLine(CChat::CLine &Line, bool ShowProgress, bool RespectIgnoredIncomingLanguages)
{
	if(m_vJobs.size() > 15)
	{
		return;
	}

	CTranslateJob Job;
	Job.m_Type = CTranslateJob::EType::CHAT_LINE;
	Job.m_pLine = &Line;
	Job.m_RespectIgnoredIncomingLanguages = RespectIgnoredIncomingLanguages;
	Job.m_pTranslateResponse = std::make_shared<CTranslateResponse>();
	Job.m_pLine->m_pTranslateResponse = Job.m_pTranslateResponse;
	Job.m_pBackend = CreateBackend(Job.m_pLine->m_aText, IncomingSourceLanguage(), IncomingTargetLanguage());
	if(!Job.m_pBackend)
	{
		GameClient()->m_Chat.Echo("Invalid translate backend");
		return;
	}

	if(ShowProgress)
	{
		str_format(Job.m_pTranslateResponse->m_Text, sizeof(Job.m_pTranslateResponse->m_Text), TCLocalize("%s translating to %s", "translate"), Job.m_pBackend->Name(), IncomingTargetLanguage());
		Job.m_pLine->m_Time = time();
	}
	else
	{
		Job.m_pTranslateResponse->m_Text[0] = '\0';
	}

	m_vJobs.emplace_back(std::move(Job));

	if(ShowProgress)
		GameClient()->m_Chat.RebuildChat();
}

bool CTranslate::TryTranslateOutgoingChat(int Team, const char *pText)
{
	if(!ShouldTranslateOutgoingChat(pText))
		return false;
	if(m_vJobs.size() > 15)
		return false;

	CTranslateJob Job;
	Job.m_Type = CTranslateJob::EType::OUTGOING_CHAT;
	Job.m_Team = Team;
	str_copy(Job.m_aOriginalText, pText, sizeof(Job.m_aOriginalText));
	if(!ExtractOutgoingTranslatableText(*GameClient(), pText, Job.m_aOutgoingPrefix, sizeof(Job.m_aOutgoingPrefix), Job.m_aTextToTranslate, sizeof(Job.m_aTextToTranslate)))
		str_copy(Job.m_aTextToTranslate, pText, sizeof(Job.m_aTextToTranslate));
	Job.m_pTranslateResponse = std::make_shared<CTranslateResponse>();
	Job.m_pBackend = CreateBackend(Job.m_aTextToTranslate, OutgoingSourceLanguage(), OutgoingTargetLanguage());
	if(!Job.m_pBackend)
		return false;

	m_vJobs.emplace_back(std::move(Job));
	return true;
}

void CTranslate::OnRender()
{
	if(!HasPendingJobs())
		return;

	bool RebuildChat = false;
	time_t UpdateTime = 0;
	auto ForEach = [&](CTranslateJob &Job) {
		if(Job.m_Type == CTranslateJob::EType::CHAT_LINE && Job.m_pLine->m_pTranslateResponse != Job.m_pTranslateResponse)
			return true; // Not the same line anymore
		const std::optional<bool> Done = Job.m_pBackend->Update(*Job.m_pTranslateResponse);
		if(!Done.has_value())
			return false; // Keep ongoing tasks

		if(Job.m_Type == CTranslateJob::EType::OUTGOING_CHAT)
		{
			const char *pTextToSend = Job.m_aOriginalText;
			char aTranslated[MAX_LINE_LENGTH] = "";
			if(*Done && Job.m_pTranslateResponse->m_Text[0] != '\0' && str_comp_nocase(Job.m_aTextToTranslate, Job.m_pTranslateResponse->m_Text) != 0 &&
				SanitizeOutgoingTranslatedText(Job.m_aTextToTranslate, Job.m_pTranslateResponse->m_Text, aTranslated, sizeof(aTranslated)))
			{
				if(Job.m_aOutgoingPrefix[0] != '\0')
				{
					char aPrefixedTranslated[MAX_LINE_LENGTH] = "";
					if(str_length(Job.m_aOutgoingPrefix) + str_length(aTranslated) < (int)sizeof(aPrefixedTranslated))
					{
						str_format(aPrefixedTranslated, sizeof(aPrefixedTranslated), "%s%s", Job.m_aOutgoingPrefix, aTranslated);
						pTextToSend = aPrefixedTranslated;
					}
				}
				else
				{
					pTextToSend = aTranslated;
				}
			}
			GameClient()->m_Chat.SendTranslatedChatQueued(Job.m_Team, pTextToSend);
			return true;
		}

		if(*Done)
		{
			if(Job.m_RespectIgnoredIncomingLanguages && IsIgnoredIncomingLanguage(Job.m_pTranslateResponse->m_Language))
			{
				Job.m_pTranslateResponse->m_Text[0] = '\0';
				Job.m_pTranslateResponse->m_Language[0] = '\0';
			}
			if(str_comp_nocase(Job.m_pLine->m_aText, Job.m_pTranslateResponse->m_Text) == 0)
				Job.m_pTranslateResponse->m_Text[0] = '\0';
		}
		else
		{
			char aBuf[sizeof(Job.m_pTranslateResponse->m_Text)];
			str_format(aBuf, sizeof(aBuf), TCLocalize("%s to %s failed: %s", "translate"), Job.m_pBackend->Name(), IncomingTargetLanguage(), Job.m_pTranslateResponse->m_Text);
			Job.m_pTranslateResponse->m_Error = true;
			str_copy(Job.m_pTranslateResponse->m_Text, aBuf);
		}
		if(UpdateTime == 0)
			UpdateTime = time();
		Job.m_pLine->m_Time = UpdateTime;
		RebuildChat = true;
		return true;
	};
	m_vJobs.erase(std::remove_if(m_vJobs.begin(), m_vJobs.end(), ForEach), m_vJobs.end());
	if(RebuildChat)
		GameClient()->m_Chat.RebuildChat();
}

void CTranslate::AutoTranslate(CChat::CLine &Line)
{
	if(!g_Config.m_TcTranslateAutoIncoming)
		return;
	if(IsAutoLanguage(IncomingTargetLanguage()))
		return;
	if(Line.m_ClientId < 0 || Line.m_aName[0] == '\0')
		return;
	for(const int Id : GameClient()->m_aLocalIds)
	{
		if(Id >= 0 && Id == Line.m_ClientId)
			return;
	}
	if(LanguagesEqual(IncomingSourceLanguage(), IncomingTargetLanguage()))
		return;
	TranslateLine(Line, false, true);
}
