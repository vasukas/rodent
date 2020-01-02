#include <cstdio>
#include <string>

#define VAS_WINCOMPAT 1

void winc_sleep(int64_t microseconds);

// general winapi utilities

std::string  winc_error();
std::string  winc_error( int error );
std::string  winc_strconv( std::wstring_view s );
std::wstring winc_strconv( std::string_view  s );
std::wstring winc_filename( const char *name );

// unistd.h

bool winc_chdir ( const char *fn );
bool winc_fexist( const char *fn );
FILE* winc_fopen( const char *fn, const char *mode );
