#ifndef VAS_STRING_UTILS_HPP
#define VAS_STRING_UTILS_HPP

#include <string>
#include <vector>



/// Returns true if string contains prefix
bool starts_with(std::string_view s, std::string_view prefix);

/// Returns true if string contains postfix
bool ends_with(std::string_view s, std::string_view postfix);



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

bool string_atof(std::string_view s, double& value);
bool string_atof(std::string_view s, float& value);

///
std::string string_u64toa(uint64_t value, int base = 10, bool hex_capital = false);

/// Returns string with delimiters at specified intervals
std::string string_u64toa_delim(uint64_t value, char delim = '\'', int delim_digits = 3, int base = 10);



/// Splits string by any of delimiters
std::vector<std::string> string_split(std::string_view s, const std::vector<std::string>& delims, bool remove_empty = true);

/// Splits string by any of delimiters without copying
std::vector<std::string_view> string_split_view(std::string_view s, const std::vector<std::string>& delims = {"\n"}, bool remove_empty = true);



/// Converts C escape sequences to characters. 
/// If ok, returns npos and string with unescaped chars. 
/// Otherwise return error position, and error is written to err (which may be same as 'str')
size_t string_unescape(std::string& str, std::string& err);

/// Escapes special characters in UTF-8 string
std::string string_escape(std::string_view s, bool escape_quotes = true);



/// Limits max string width, including additional newlines. 
/// Omits whitespaces at the beginning of new line
std::string wrap_words(std::string_view s, size_t max_width, size_t* current_width = nullptr);

#endif // VAS_STRING_UTILS_HPP
