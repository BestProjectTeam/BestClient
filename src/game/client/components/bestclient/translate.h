#ifndef GAME_CLIENT_COMPONENTS_BestClient_TRANSLATE_H
#define GAME_CLIENT_COMPONENTS_BestClient_TRANSLATE_H

#include <game/client/component.h>
#include <game/client/components/chat.h>

#include <memory>
#include <optional>
#include <vector>

class CTranslate;

class ITranslateBackend
{
public:
	virtual ~ITranslateBackend() = default;
	virtual const char *EncodeSource(const char *pSource) const;
	virtual const char *EncodeTarget(const char *pTarget) const;
	virtual bool CompareTargets(const char *pA, const char *pB) const;
	virtual const char *Name() const = 0;
	virtual std::optional<bool> Update(CTranslateResponse &Out) = 0;
};

class CTranslate : public CComponent
{
	class CTranslateJob
	{
	public:
		enum class EType
		{
			CHAT_LINE = 0,
			OUTGOING_CHAT,
		};

		EType m_Type = EType::CHAT_LINE;
		std::unique_ptr<ITranslateBackend> m_pBackend = nullptr;
		CChat::CLine *m_pLine = nullptr;
		std::shared_ptr<CTranslateResponse> m_pTranslateResponse = nullptr;
		int m_Team = 0;
		char m_aOriginalText[MAX_LINE_LENGTH] = "";
	};
	std::vector<CTranslateJob> m_vJobs;

	static void ConTranslate(IConsole::IResult *pResult, void *pUserData);
	static void ConTranslateId(IConsole::IResult *pResult, void *pUserData);
	std::unique_ptr<ITranslateBackend> CreateBackend(const char *pText, const char *pSourceLanguage, const char *pTargetLanguage) const;
	const char *IncomingSourceLanguage() const;
	const char *IncomingTargetLanguage() const;
	const char *OutgoingSourceLanguage() const;
	const char *OutgoingTargetLanguage() const;
	bool ShouldTranslateOutgoingChat(const char *pText) const;
	bool HasPendingJobs() const;

public:
	int Sizeof() const override { return sizeof(*this); }

	void OnConsoleInit() override;
	void OnRender() override;

	void Translate(int Id, bool ShowProgress = true);
	void Translate(const char *pName, bool ShowProgress = true);
	void Translate(CChat::CLine &Line, bool ShowProgress = true);
	bool TryTranslateOutgoingChat(int Team, const char *pText);

	void AutoTranslate(CChat::CLine &Line);
};

#endif
