#ifndef VAS_STRING_UTILS_HPP
#define VAS_STRING_UTILS_HPP

#include <string>
#include <vector>



/// Translates single character.
/// 'n' is number of characters read.
/// Returns 0 on error (if 'n' set to 0, code is wrong, otherwise not enough data)
char32_t char_8to32(const char *str, int& n, int left = 4);

/// Translates single character. 
/// Returned view is valid until next call on same thread
std::string_view char_32to8(char32_t c);

/// Returned view is null-terminated and valid until next call on same thread
std::u32string_view tmpstr_8to32(std::string_view s);

/// Returned view is null-terminated and valid until next call on same thread
std::string_view tmpstr_32to8(std::u32string_view s);

std::u32string string_8to32(std::string_view s);
std::string string_32to8(std::u32string_view s);



// These functions operate on whole string. 
// Integer ones reckognize sign and prefixes 0x (0X), 0b (0B) - which changes base automatically if encountered. 
// Only bases 2-10 and 16 are supported.

bool string_atoi(std::string_view s, int& value, int base = 10);
bool string_atoi(std::string_view s, int64_t& value, int base = 10);

bool string_atof(const std::string& s, double& value);
bool string_atof(const std::string& s, float& value);



/// Splits string by any of delimiters
std::vector<std::string> string_split(std::string_view s, const std::vector<std::string>& delims, bool remove_empty = true);



/// Converts C escape sequences to characters. 
/// If ok, returns npos and string with unescaped chars. 
/// Otherwise return error position, and error is written to err (which may be same as 'str')
size_t string_unescape(std::string& str, std::string& err);

/// Escapes special characters in UTF-8 string
std::string string_escape(std::string_view s, bool escape_quotes = true);

#endif // VAS_STRING_UTILS_HPP
