#ifndef VAS_MISC_HPP
#define VAS_MISC_HPP

#include <cinttypes>
#include <string>
#include <vector>



struct ArgvParse
{
	std::vector<std::string> args;
	size_t i = 0; ///< Count (current argument)
	std::string opt; ///< Current options (set from successful is())
	
	ArgvParse() = default;
	ArgvParse(std::string_view str) {set(str);}
	
	void set(std::string_view str);
	void set(int argc, char **argv);
	
	bool ended() const;
	bool is(std::string_view s, bool incr = true); ///< Checks if equal to current. If is, increases counter
	const std::string& cur(); ///< Doesn't increase count
	
	void skip(size_t n = 1); ///< Throws if too many
	
	const std::string& str(); ///< Increases count
	int i32(int radix = 10);
	float fp();
	bool flag();
};



/// Calculates checksum (initial sum value is 0)
uint32_t crc32(uint32_t sum, const void *data, size_t len);

/// Calculates fast hash (FNV-1a)
uint32_t fast_hash32(const void *data, size_t len);
inline uint32_t fast_hash32(std::string_view s) {return fast_hash32(s.data(), s.size());}

#endif // VAS_MISC_HPP
