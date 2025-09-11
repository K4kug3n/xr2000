#ifndef STRINGPROCESS_HPP
#define STRINGPROCESS_HPP

#include <string>
#include <vector>

bool is_alpha(const std::string& str);

bool has_alphanumeric(const std::string& str);

std::vector<std::string> get_unique_words(const std::string& text);

#endif
