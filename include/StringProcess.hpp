#ifndef STRINGPROCESS_HPP
#define STRINGPROCESS_HPP

#include <string>
#include <vector>
#include <unordered_map>

bool is_alpha(const std::string& str);

bool has_alphanumeric(const std::string& str);

bool is_splitter(unsigned char c);

std::vector<std::string> get_unique_words(const std::string& text);

std::string translate(const std::string& text, const std::unordered_map<std::string, std::string>& mapping);

#endif
