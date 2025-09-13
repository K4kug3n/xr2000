#include "StringProcess.hpp"

#include <algorithm>
#include <sstream>

bool is_alpha(const std::string& str) {
	return std::find_if(str.begin(), str.end(), [](char c) {
		return !(std::isalpha(c));
	}) == str.end();
}

bool has_alphanumeric(const std::string& str) {
	return std::find_if(str.begin(), str.end(), [](char c) {
		return std::isalpha(c) || std::isdigit(c);
	}) != str.end();
}

bool is_splitter(unsigned char c) {
	return (c == ',') || (c == '.') || (c == ':') || (c == '-') || (c == ' ') || (c == '\n') || (c == '(') || (c == ')');
}

std::vector<std::string> get_unique_words(const std::string& text) {
	std::string cleaned = text;

	// Remove useless characters
	for(auto& c: cleaned) {
		if (is_splitter(c)) {
			c = ' ';
		} else if (std::isupper(c)) {
			c = std::tolower(c);
		}
	}

	std::vector<std::string> words;
	std::istringstream iss(cleaned);
	std::string word;
    while (iss >> word) {
        words.push_back(word);
    }

	words.erase(
		std::remove_if(words.begin(), words.end(), [](const std::string& w) {
			return !is_alpha(w);
		}),
		words.end()
	);

	std::sort(words.begin(), words.end());

	// Keep unique words
	const auto it = std::unique(words.begin(), words.end());
	words.resize(std::distance(words.begin(), it));

    return words;
}

std::string translate(const std::string& text, const std::unordered_map<std::string, std::string>& mapping) {
	std::ostringstream oss;

	std::string word = "";
	for (unsigned char c : text) {
		if (is_splitter(c)) {
			if (word != "") { // Pending word to translate
				if (mapping.contains(word)) {
					oss << mapping.at(word);
				} else {
					oss << word;
				}
				word = "";
			}
			oss << c;
		} else {
			word += std::tolower(c);
		}
	}

	// Empty word
	if (mapping.contains(word)) {
		oss << mapping.at(word);
	} else {
		oss << word;
	}

	return oss.str();
}

