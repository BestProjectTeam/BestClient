#include "regex.h"

#include <base/str.h>

#include <cstring>
#include <regex>

class Regex::Data {
public:
	std::regex regex;
	std::string error;
	bool m_CaseInsensitive;
};

Regex::Regex(const Regex& other) : data(new Data(*other.data)) {}

Regex::Regex(Regex&& Other) : data(Other.data) {
	Other.data = nullptr;
}

Regex::Regex() : data(new Data{{}, "Empty", false}) {}

std::string Utf8ToLower(const std::string &Input)
{
	std::string Output;
	Output.resize(Input.size() * 4 + 1);
	str_utf8_tolower(Input.c_str(), Output.data(), Output.size());
	Output.resize(std::strlen(Output.c_str()));
	return Output;
}

Regex::Regex(const std::string& pattern, bool CaseInsensitive) : data(new Data) {
	try {
		const std::string CompiledPattern = CaseInsensitive ? Utf8ToLower(pattern) : pattern;
		data->regex = std::regex(CompiledPattern, std::regex::ECMAScript);
		data->error = "";
		data->m_CaseInsensitive = CaseInsensitive;
	} catch (const std::regex_error& e) {
		data->error = e.what();
		data->m_CaseInsensitive = CaseInsensitive;
	}
}

Regex& Regex::operator=(const Regex& other) {
	if (this != &other) {
		delete data;
		data = new Data(*other.data);
	}
	return *this;
}

Regex& Regex::operator=(Regex&& other) {
	if (this != &other) {
		delete data;
		data = other.data;
		other.data = nullptr;
	}
	return *this;
}

Regex::~Regex() {
	delete data;
}

std::string Regex::error() const {
	return data->error;
}

bool Regex::test(const std::string &str) {
	if (!data || !data->error.empty()) return false;
	const std::string &Target = data->m_CaseInsensitive ? Utf8ToLower(str) : str;
	return std::regex_search(Target, data->regex);
}

void Regex::match(const std::string &str, bool global, std::function<void(const std::string &str, int match, int group)> func) {
	if (!data->error.empty()) return;

	const std::string Target = data->m_CaseInsensitive ? Utf8ToLower(str) : str;

	std::smatch sm;
	std::string::const_iterator searchStart(Target.cbegin());
	int matchIndex = 0;

	while (std::regex_search(searchStart, Target.cend(), sm, data->regex)) {
		for (size_t g = 0; g < sm.size(); ++g) {
			func(sm[g].str(), matchIndex, static_cast<int>(g));
		}

		++matchIndex;
		if (!global) break;  // only first match if global is false
		searchStart = sm.suffix().first;
	}
}

std::string Regex::replace(const std::string &str, bool global, std::function<std::string(const std::string &str, int match, int group)> func) {
	if (!data->error.empty()) return str;

	const std::string LowerStr = data->m_CaseInsensitive ? Utf8ToLower(str) : str;

	// If lengths differ, we cannot reliably map replacements back to the original without risking corruption.
	if(data->m_CaseInsensitive && LowerStr.size() != str.size())
		return str;

	std::string result;
	std::string::const_iterator searchStart(LowerStr.cbegin());
	std::smatch sm;
	int matchIndex = 0;

	while (std::regex_search(searchStart, LowerStr.cend(), sm, data->regex)) {
		const auto PrefixStart = static_cast<size_t>(searchStart - LowerStr.cbegin());
		const auto MatchStart = static_cast<size_t>(sm[0].first - LowerStr.cbegin());
		result.append(str.begin() + PrefixStart, str.begin() + MatchStart); // append text before match using original casing

		std::string replacement;
		for (size_t g = 0; g < sm.size(); ++g) {
			replacement += func(sm[g].str(), matchIndex, static_cast<int>(g));
		}
		result += replacement;

		++matchIndex;
		if (!global) {
			const auto SuffixStart = static_cast<size_t>(sm.suffix().first - LowerStr.cbegin());
			result.append(str.begin() + SuffixStart, str.end());
			return result;
		}
		searchStart = sm.suffix().first;
	}

	// append remaining text if any
	const auto RemainingStart = static_cast<size_t>(searchStart - LowerStr.cbegin());
	result.append(str.begin() + RemainingStart, str.end());
	return result;
}
