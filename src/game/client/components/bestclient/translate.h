/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_TRANSLATE_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_TRANSLATE_H

#include <game/client/component.h>
#include <game/client/components/chat.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class ITranslateBackend
{
public:
	explicit ITranslateBackend(const char *pName) :
		m_pName(pName) {}
	virtual ~ITranslateBackend() = default;

	virtual const char *EncodeTarget(const char *pTarget) const;
	const char *Name() const { return m_pName; }
	virtual std::optional<bool> Update(CTranslateResponse &Out) = 0;

private:
	const char *m_pName;
};

struct STranslateLanguage
{
	const char *m_pName;
	const char *m_pCode;
};

struct STranslateBackendInfo
{
	const char *m_pValue;
	const char *m_pName;
	std::vector<STranslateLanguage> m_vLanguages;
	bool m_NeedsEndpointConfig;
};

extern const std::array<STranslateBackendInfo, 3> g_aTranslateBackends;

int TranslateLanguageIndex(const std::vector<STranslateLanguage> &vLanguages, const char *pCode);

class CTranslate : public CComponent
{
	class CTranslateJob
	{
	public:
		enum class EType
		{
			CHAT_LINE,
			OUTGOING_CHAT,
		};

		EType m_Type = EType::CHAT_LINE;
		std::unique_ptr<ITranslateBackend> m_pBackend;
		CChat::CLine *m_pLine = nullptr;
		std::shared_ptr<CTranslateResponse> m_pTranslateResponse;
		int m_Team = 0;
		char m_aTextToTranslate[MAX_CHAT_LENGTH] = "";
		char m_aOutgoingPrefix[MAX_CHAT_LENGTH] = "";
		bool m_RespectIgnoredIncomingLanguages = false;
		int64_t m_Deadline = 0;
	};

	std::vector<CTranslateJob> m_vJobs;
	int64_t m_NextRequestTime = 0;

	std::unique_ptr<ITranslateBackend> CreateBackend(const char *pText, const char *pSourceLanguage, const char *pTargetLanguage) const;
	const char *IncomingSourceLanguage() const;
	const char *IncomingTargetLanguage() const;
	const char *OutgoingSourceLanguage() const;
	const char *OutgoingTargetLanguage() const;
	bool IsIgnoredIncomingLanguage(const char *pLanguage) const;
	bool ShouldTranslateOutgoingChat(const char *pText) const;
	bool CanStartRequest() const;
	void TranslateLine(CChat::CLine &Line, bool RespectIgnoredIncomingLanguages);

public:
	int Sizeof() const override { return sizeof(*this); }

	void OnConsoleInit() override;
	void OnRender() override;

	void AutoTranslate(CChat::CLine &Line);
	bool ChatDoTranslateOutgoing(int Team, const char *pText);
	void ToggleShiftClick();
};

#endif
