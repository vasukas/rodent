#include <array>
#include "vas_misc.hpp"
#include "vas_string_utils.hpp"



void ArgvParse::set(std::string_view str)
{	
	args.clear();
	args.reserve(64);
	i = 0;
	
	bool quoted = false;
	args.emplace_back();
	
	for (size_t i=0; i<str.length(); ++i)
	{
		if (str[i] == ' ')
		{
			if (quoted) continue;
			if (!args.back().empty())
				args.emplace_back();
		}
		else {
			if (str[i] == '"' && i && str[i-1] != '\\')
				quoted = !quoted;
			else
				args.back().push_back(str[i]);
		}
	}
	
	if (args.back().empty())
		args.pop_back();
}
void ArgvParse::set(int argc, char **argv)
{
	args.resize(argc);
	for (int i=0; i<argc; ++i)
		args[i] = argv[i];
	i = 0;
}
bool ArgvParse::ended() const
{
	return i >= args.size();
}
bool ArgvParse::is(std::string_view s, bool incr)
{
	if (ended()) {
		std::string e = "no more arguments (at option '";
		e += s; e += ")' (API error)";
		throw std::logic_error(e);
	}
	bool ok = args[i] == s;
	if (ok && incr) ++i;
	if (ok) opt = s;
	return ok;
}
const std::string& ArgvParse::cur()
{
	if (ended()) {
		std::string e = "no more arguments (API error)";
		throw std::logic_error(e);
	}
	return args[i];
}
void ArgvParse::skip(size_t n)
{
	i += n;
	if (i > args.size())
		throw std::logic_error("argument skip out-of-range");
}
const std::string& ArgvParse::str()
{
	if (ended()) {
		std::string e = "not enough arguments for option '";
		e += opt; e += "'";
		throw std::runtime_error(e);
	}
	return args[i++];
}
int ArgvParse::i32(int radix)
{
	auto& s = str();
	int val = 0;
	
	if (!string_atoi(s, val, radix))
	{
		std::string e = "invalid integer argument '";
		e += s; e += "' for option: '";
		e += opt; e += "'";
		throw std::runtime_error(e);
	}
	return val;
}
float ArgvParse::fp()
{
	auto& s = str();
	float val = 0;
	
	if (!string_atof(s, val))
	{
		std::string e = "invalid floating point argument '";
		e += s; e += "' for option: '";
		e += opt; e += "'";
		throw std::runtime_error(e);
	}
	return val;
}
bool ArgvParse::flag()
{
	auto& s = str();
	if		(s == "0" || s == "false") return false;
	else if (s == "1" || s == "true") return true;
	
	std::string e = "invalid boolean argument '";
	e += s; e += "' for option: '";
	e += opt; e += "'";
	throw std::runtime_error(e);
}



// adapted from somewhere

static constexpr std::array<uint32_t, 256> crc32_table(uint32_t poly) {
	std::array<uint32_t, 256> table {0};
	for (int i = 0; i < 256; i++) {
		uint32_t crc = i;
		for (int j = 0; j < 8; j++)
			crc = crc & 1 ? (crc >> 1) ^ poly : crc >> 1;
		table[i] = crc;
	}
	return table;
}
uint32_t crc32(uint32_t crc, const void *data, int len) {
	// CRC-32 (Ethernet, ZIP, etc.) polynomial in reversed bit order.
	static constexpr std::array<uint32_t, 256> table = crc32_table(0xedb88320);
	uint8_t* buf = (uint8_t*) data;
	crc = ~crc;
	while (len--) {
		crc = table[(crc ^ (*buf)) & 0xff] ^ (crc >> 8);
		++buf;
	}
	return ~crc;
}
