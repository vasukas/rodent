#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define WIN32_EXTRA_LEAN
#include <windows.h>
#include "wincompat.hpp"
#include "vaslib/vas_log.hpp"



struct HandleWrapper
{
	HANDLE h = INVALID_HANDLE_VALUE;
	~HandleWrapper() {CloseHandle(h);}
	void operator=(HANDLE h_) {h = h_;}
	operator bool() const {return h != INVALID_HANDLE_VALUE;}
	operator HANDLE() const {return h;}
};

void winc_sleep(int64_t microseconds)
{
	thread_local HandleWrapper timer;
	
	LARGE_INTEGER ft;
	ft.QuadPart = -(microseconds * 10);

	if (!timer) timer = CreateWaitableTimerA(NULL, TRUE, NULL);
	SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
	WaitForSingleObject(timer, INFINITE);
}



std::string winc_error()
{
	return winc_error( GetLastError() );
}
std::string winc_error( int error )
{
	LPSTR msg = nullptr; // MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT)
	size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
	                             NULL, error, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), (LPSTR) &msg, 0, NULL);
	if (!size)
	{
		error = GetLastError();
		VLOGE("FormatMessageA failed with [{}]", error);
		return {};
	}
	std::string ret = fmt::format( FMT_STRING("[{}] {}"), error, msg );
	LocalFree(msg);
	return ret;
}
std::string winc_strconv( std::wstring_view s )
{
	if (s.empty()) return {};
	
	std::string out;
	size_t size = WideCharToMultiByte( CP_UTF8, 0, s.data(), s.length(), out.data(), 0, NULL, NULL );
	out.resize( size );

	if (!WideCharToMultiByte( CP_UTF8, 0, s.data(), s.length(), out.data(), size, NULL, NULL ))
	{
		VLOGE("winc_strconv() failed (wide -> 8) - {}", winc_error());
		return {};
	}
	return out;
}
std::wstring winc_strconv( std::string_view s )
{
	if (s.empty()) return {};
	
	std::wstring out;
	size_t size = MultiByteToWideChar( CP_UTF8, 0, s.data(), s.length(), out.data(), 0 );
	out.resize( size );

	if (!MultiByteToWideChar( CP_UTF8, 0, s.data(), s.length(), out.data(), size ))
	{
		VLOGE("winc_strconv() failed (8 -> wide) - {}", winc_error());
		return {};
	}
	return out;
}
std::wstring winc_filename( const char *name )
{
	if (!name) return {};
	return winc_strconv( name );
}



bool winc_chdir ( const char *fn )
{
	if (!SetCurrentDirectoryW( winc_filename(fn).c_str() ))
	{
		VLOGE("SetCurrentDirectoryW failed - ", winc_error());
		return false;
	}
	return true;
}
bool winc_fexist( const char *fn )
{
	auto res = GetFileAttributesW( winc_filename(fn).c_str() );
	return res != INVALID_FILE_ATTRIBUTES && (res & FILE_ATTRIBUTE_DIRECTORY) == 0;
}
FILE* winc_fopen( const char *fn, const char *mode )
{
	return _wfopen( winc_filename(fn).c_str(), winc_strconv(mode).c_str() );
}



#include <vector>
#include <shellapi.h>
#include <SDL2/SDL_main.h>
//int main(int, char*[]);

int __stdcall WinMain(HINSTANCE, HINSTANCE, char *, int) {
	LPWSTR cmd = GetCommandLineW();
	int argc = 0;
	LPWSTR* w_argv = CommandLineToArgvW(cmd, &argc);
	if (!w_argv) {
		int error = GetLastError();
		printf("WinMain failed on CommandLineToArgvW [%d]\n", error);
		printf("%s\n", winc_error(error).c_str());
		return 1;
	}
	std::vector<std::string> s_argv;
	std::vector<char*> argv;
	s_argv.resize(argc);
	argv.resize(argc);
	for (int i=0; i<argc; ++i) {
		auto& s = s_argv[i];
		s = winc_strconv(w_argv[i]);
		if (s.empty()) {
			printf("WinMain() failed on argv[%d]\n", i);
			return 1;
		}
		argv[i] = const_cast<char*>(s.c_str());
	}
	LocalFree(w_argv);
	return main(argc, argv.data());
}

#endif
