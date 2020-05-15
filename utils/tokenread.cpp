#include <stdexcept>
#include "vaslib/vas_file.hpp"
#include "vaslib/vas_string_utils.hpp"
#include "tokenread.hpp"

std::string TokenReader::reset_file(const char *filename)
{
	auto r = readfile(filename);
	if (!r) return {};
	reset(*r);
	return std::move(*r);
}
void TokenReader::reset(std::string_view s)
{
	this->s = s;
	at = end = 0;
}
bool TokenReader::ended()
{
	raw();
	return at == s.length();
}
bool TokenReader::is(std::string_view v)
{
	bool f = (raw() == v);
	if (f) next();
	return f;
}
std::string_view TokenReader::str()
{
	auto s = raw(true, true);
	if (s.front() == '"')
		s = s.substr(1, s.length() - 2);
	return s;
}
int32_t TokenReader::i32()
{
	int v;
	if (!string_atoi(raw(true, true), v))
		throw std::runtime_error("TokenReader:: not an integer");
	return v;
}
float TokenReader::num()
{
	float v;
	if (!string_atof(raw(true, true), v))
		throw std::runtime_error("TokenReader:: not a number");
	return v;
}
std::string_view TokenReader::raw(bool must, bool advance)
{
	read(must);
	auto r = s.substr(at, end - at);
	if (advance) next();
	return r;
}
void TokenReader::next()
{
	at = end;
}
std::pair<int,int> TokenReader::calc_position()
{
	size_t line = 1, nl = 0;
	for (size_t i=0; i<at; ++i) {
		if (s[i] == '\n') {
			++line;
			nl = i + 1;
		}
	}
	return {line, at - nl + 1};
}
void TokenReader::read(bool must)
{
	if (at != end) return;
	
	auto space = [](char c) {
		return c == ' ' || c == '\t' || c == '\n';
	};
	
	// skip spaces
	bool comment = false;
	while (at < s.length())
	{
		if (comment) {
			if (s[at] == '\n')
				comment = false;
		}
		else {
			if (s[at] == '#') comment = true;
			else if (!space(s[at]))
				break;
		}
		++at;
	}
	end = at;
	
	if (at == s.length()) {
		if (must)
			throw std::runtime_error("TokenReader:: unexpected EOF");
	}
	
	// find end
	bool quote = false;
	while (end < s.length())
	{
		if (quote) {
			if (s[end] == '"' /*&& s[end-1] != '\\'*/)
				quote = false;
		}
		else {
			if (s[end] == '"') quote = true;
			else if (s[end] == '#' || space(s[end])) break;
		}
		++end;
	}
}
