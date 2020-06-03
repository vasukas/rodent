#include <SDL2/SDL_cpuinfo.h>
#include "vaslib/vas_log.hpp"
#include "hard_paths.hpp"

std::string get_full_platform_version()
{
	// output string must be less than 255 characters
	
#if defined(__linux__)
	auto os = "Linux";
#elif defined(__unix__)
	auto os = "UNIX";
#elif defined(_WIN32)
	auto os = "Windows";
#endif

#if defined(__x86_64__) || defined(_M_X64)
	auto ps = "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
	auto ps = "x86";
#endif
	
#ifdef __clang__
	auto cs = FMT_FORMAT("clang-{}.{}.{}", __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(__GNUC__)
	auto cs = FMT_FORMAT("GCC-{}.{}.{}", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
	auto cs = FMT_FORMAT("MSVC-{}", _MSC_VER);
#endif
	
#ifdef PROJECT_VERSION_STRING
	const char *vs = PROJECT_VERSION_STRING;
#else
	const char *vs = "NONE";
#endif
	
	return FMT_FORMAT("{} | {}-{} | v{}", cs, os, ps, vs);
}
