#include "regex.h"

#include <regex>

class Regex::Data {
public:
	std::regex regex;
	std::string error;
};

Regex::Regex(const Regex& other) : data(new Data(*other.data)) {}

Regex::Regex(Regex&& Other) : data(Other.data) {
	Other.data = nullptr;
}

Regex::Regex() : data(new Data{{}, "Empty"}) {}

Regex::Regex(const std::string& pattern) : data(new Data) {
	try {
		data->regex = std::regex(pattern);
		data->error = "";
	} catch (const std::regex_error& e) {
		data->error = e.what();
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
	return std::regex_search(str, data->regex);
}

void Regex::match(const std::string &str, bool global, std::function<void(const std::string &str, int match, int group)> func) {
	if (!data->error.empty()) return;

	std::smatch sm;
	std::string::const_iterator searchStart(str.cbegin());
	int matchIndex = 0;

	while (std::regex_search(searchStart, str.cend(), sm, data->regex)) {
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

	std::string result;
	std::string::const_iterator searchStart(str.cbegin());
	std::smatch sm;
	int matchIndex = 0;

	while (std::regex_search(searchStart, str.cend(), sm, data->regex)) {
		result.append(searchStart, sm[0].first); // append text before match

		std::string replacement;
		for (size_t g = 0; g < sm.size(); ++g) {
			replacement += func(sm[g].str(), matchIndex, static_cast<int>(g));
		}
		result += replacement;

		++matchIndex;
		if (!global) {
			result.append(sm.suffix().first, str.cend());
			return result;
		}
		searchStart = sm.suffix().first;
	}

	// append remaining text if any
	result.append(searchStart, str.cend());
	return result;
}
