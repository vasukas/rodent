#include <algorithm>
#include "vas_string_utils.hpp"



char32_t char_8to32(const char *str, int& n, int left) {
	uint32_t c = uint8_t(*str);
	if (c >= 0x80) {
		if 		((c & 0xe0) == 0xc0) n = 2, c &= 0x1f;
		else if ((c & 0xf0) == 0xe0) n = 3, c &= 0x0f;
		else if ((c & 0xf8) == 0xf0) n = 4, c &= 0x07;
		else {
			n = 0;
			return 0;
		}
		if (n > left) {
			n = left;
			return 0;
		}
		for (int i=1; i<n; i++) {
			c <<= 6;
			c |= (uint8_t(str[i]) & 0x3f);
		}
	}
	else n = 1;
	return (char32_t) c;
}
std::string_view char_32to8(char32_t c) {
	thread_local std::string out(4, '\0');
	out.clear();
	
	if (c < 0x80) out.push_back( c );
	else if (c < 0x800)
	{
		out.push_back(0xc0 | (c >> 6));
		out.push_back(0x80 | (c & 0x3f));
	}
	else if (c < 0x10000)
	{
		out.push_back(0xe0 | (c >> 12));
		out.push_back(0x80 | ((c >> 6) & 0x3f));
		out.push_back(0x80 | (c & 0x3f));
	}
	else if (c < 0x110000)
	{
		out.push_back(0xe0 | (c >> 18));
		out.push_back(0x80 | ((c >> 12) & 0x3f));
		out.push_back(0x80 | ((c >> 6) & 0x3f));
		out.push_back(0x80 | (c & 0x3f));
	}
	else
	{
		out.push_back(c >> 24);
		out.push_back(c >> 16);
		out.push_back(c >> 8);
		out.push_back(c);
	}
	
	return out;
}
std::u32string_view tmpstr_8to32(std::string_view s) {
	thread_local std::u32string out;
	out.clear();
	for (std::size_t i=0; i<s.length(); ) {
		int n;
		char32_t c = char_8to32(s.data() + i, n, s.length() - i);
		if (c) {
			out.push_back(c);
			i += n;
		}
		else {
			out.push_back(s[i]);
			++i;
		}
	}
	return out;
}
std::string_view tmpstr_32to8(std::u32string_view s) {
	thread_local std::string out;
	out.clear();
	out.reserve(s.length()*2);
	for (auto &c : s) {
		if (c < 0x80) out.push_back(c);
		else if (c < 0x800) {
			out.push_back(0xc0 | (c >> 6));
			out.push_back(0x80 | (c & 0x3f));
		}
		else if (c < 0x10000) {
			out.push_back(0xe0 | (c >> 12));
			out.push_back(0x80 | ((c >> 6) & 0x3f));
			out.push_back(0x80 | (c & 0x3f));
		}
		else if (c < 0x110000) {
			out.push_back(0xe0 | (c >> 18));
			out.push_back(0x80 | ((c >> 12) & 0x3f));
			out.push_back(0x80 | ((c >> 6) & 0x3f));
			out.push_back(0x80 | (c & 0x3f));
		}
		else {
			out.push_back(c >> 24);
			out.push_back(c >> 16);
			out.push_back(c >> 8);
			out.push_back(c);
		}
	}
	return out;
}
std::u32string string_8to32(std::string_view s) { return std::u32string(tmpstr_8to32(s)); }
std::string string_32to8(std::u32string_view s) { return std::string(tmpstr_32to8(s)); }



bool string_atoi (std::string_view s, int& value, int base)
{
	int64_t v;
	if (!string_atoi( s, v, base )) return false;
	value = static_cast< int >( v );
	return true;
}
bool string_atoi (std::string_view s, int64_t& value, int base)
{
	// ASCII order: signs, [0-9], [A-Z], [a-z]
	
	if (base < 2 || (base > 10 && base != 16)) return false;
	char n_max = (base <= 10) ? '0' + (base - 1) : '9';
	
	int sign = 0;
	value = 0;
	
	for (size_t i = 0; i < s.size(); ++i)
	{
		if (s[i] == '0' && (!i || (i == 1 && sign != 0)))
		{
			if (i + 1 != s.size())
			{
				if		(s[i+1] == 'x' || s[i+1] == 'X') { base = 16; ++i; continue; }
				else if (s[i+1] == 'b' || s[i+1] == 'B') { base = 2;  ++i; continue; }
			}
		}
		char c = s[i];
		
		if (c <= n_max && c >= '0')
		{
			value *= base;
			value += c - '0';
		}
		else if (base == 16 && c <= 'z' && c >= 'A' && (c <= 'Z' || c >= 'a'))
		{
			value *= base;
			value += c - (c >= 'a' ? 'a' : 'A');
		}
		else if (!i)
		{
			if		(c == '-') sign = -1;
			else if (c == '+') sign =  1;
			else return false;
		}
		else return false;
	}
	
	if (sign == -1) value = -value;
	return true;
}
bool string_atof (const std::string& s, double& value)
{
	char *end = nullptr;
	value = strtod( s.data(), &end );
	return end && end - s.data() == static_cast<ptrdiff_t>(s.length());
}
bool string_atof (const std::string& s, float& value)
{
	char *end = nullptr;
	value = strtof( s.data(), &end );
	return end && end - s.data() == static_cast<ptrdiff_t>(s.length());
}



std::vector<std::string> string_split(std::string_view s, const std::vector<std::string> &delims, bool remove_empty) {
	std::vector<std::string> rs;
	std::size_t pos = 0;
	while (true) {
		std::size_t np = std::string::npos;
		for (auto &d : delims) np = std::min(np, s.find_first_of(d, pos));
		if (np != pos || !remove_empty) {
			rs.emplace_back( s.substr(pos, np - pos) );
			if (np == std::string::npos) break;
		}
		pos = np + 1;
	}
	if (!rs.empty() && !rs.back().size()) rs.pop_back();
	return rs;
}



size_t string_unescape(std::string &str, std::string &err) {
	if (str.empty()) return std::string::npos;
	size_t pos = 0;
	for (size_t i=0; i<str.length()-1; ++pos) {
		if (str[i] == '\\') {
			str.erase(i, 1);
			if (str[i] == '\'' || str[i] == '"' || str[i] == '?' || str[i] == '\\') continue;
			else if (str[i] == 'a') str[i] = '\a';
			else if (str[i] == 'b') str[i] = '\b';
			else if (str[i] == 'f') str[i] = '\f';
			else if (str[i] == 'n') str[i] = '\n';
			else if (str[i] == 'r') str[i] = '\r';
			else if (str[i] == 't') str[i] = '\t';
			else if (str[i] == 'v') str[i] = '\v';
			else if (str[i] == 'x' || str[i] == 'u' || str[i] == 'U') {
				if (i == str.length() - 1) {
					err = "Not complete";
					return pos;
				}
				size_t exp_len = str[i] == 'x'? 2 : (str[i] == 'u'? 4 : 8);
				
				char* end = nullptr;
				long n = strtol(str.data() + i + 1, &end, 16);
				size_t len = (end - str.data()) - i;
				
				if (len != exp_len) {
					err = "Invalid length";
					return pos;
				}
				if (str[i] == 'x') {
					str.erase(i, len);
					str.insert( str.begin() + i, (char) unsigned(n) );
				}
				else {
					auto s = char_32to8(n);
					str.erase(i, len);
					str.insert(i, s.data(), s.length());
				}
			}
			else if (str[i] <= '7' && str[i] >= '0') {
				char* end = nullptr;
				long n = strtol(str.data() + i, &end, 8);
				size_t len = (end - str.data()) - i;
				
				if (len > 3 || !len) {
					err = "Invalid length";
					return pos;
				}
				str.erase(i, len);
				str.insert( str.begin() + i, (char) unsigned(n) );
			}
			else {
				err = "Unknown escape";
				return pos;
			}
		}
		else ++i;
	}
	return std::string::npos;
}
std::string string_escape(std::string_view s, bool escape_quotes)
{
	std::string r;
	r.reserve(s.size());
	for (auto& c : s)
	{
		if (c == '\\' || c == '\a' || c == '\b' || c == '\f' || c == '\n' || c == '\r' || c == '\t' || c == '\v' ||
		    (escape_quotes && c == '"'))
		{
			r.push_back('\\');
			r.push_back(c);
		}
		else if (c < 32)
		{
			r.push_back('\\');
			r.push_back('0');
			if (c < 8) r.push_back('0' + c);
			else
			{
				r.push_back('0' + c/8);
				r.push_back('0' + c%8);
			}
		}
		else r.push_back(c);
	}
	return r;
}
