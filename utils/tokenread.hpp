#ifndef TOKENREAD_HPP
#define TOKENREAD_HPP

#include <string>

struct TokenReader
{
	[[nodiscard]] std::string reset_file(const char *filename); ///< Returns empty string on error
	void reset(std::string_view s);
	
	bool ended();
	bool is(std::string_view v); ///< Calls next() if true, does nothing otherwise
	std::string_view str(); // can contain any symbols except double quotes
	int32_t i32();
	float num();
	
	std::string_view raw(bool must = false, bool advance = false);
	void next();
	std::pair<int,int> calc_position(); ///< Matrix coords, 1-based
	
private:
	std::string_view s;
	size_t at, end;
	
	void read(bool must = true);
};

#endif // TOKENREAD_HPP
