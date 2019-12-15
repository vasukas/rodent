#include <chrono>
#include <csignal>
#include <ctime>
#include <mutex>
#include "vaslib/vas_cpp_utils.hpp"
#include "vaslib/vas_file.hpp"
#include "vaslib/vas_log.hpp"

thread_local fmt::memory_buffer log_fmt_buffer;
static LoggerSettings lsets;
static size_t log_flush_cou = 0;

static std::vector <std::pair <LogLevel, std::string>> buf_lines;
static size_t buf_cur = 0;
static std::mutex buf_lock;



static void write_lines(LogLevel level, const char *str, int length)
{
	std::unique_lock lock (buf_lock);
	
	auto newline = [&]()
	{
		buf_cur = (buf_cur + 1) % buf_lines.size();
		buf_lines[buf_cur].first = level;
		buf_lines[buf_cur].second.clear();
	};
	newline();
	
	for (int i = 0; i < length; ++i)
	{
		if (str[i] == '\n') newline();
		else if (auto& s = buf_lines[buf_cur].second; s.length() < lsets.lines_width) s.push_back( str[i] );
		else if (auto p = strchr(str + i, '\n')) i = p - str;
		else break;
	}
}



#if defined(_WIN32)
void debugbreak() {__debugbreak();}
#elif defined(__unix__)
void debugbreak() {raise(SIGTRAP);}
#endif



static std::vector<void(*)()> term_fs;
static std::terminate_handler term_default;

void log_setup_signals()
{
	signal(SIGSEGV, [](int)
	{
		log_critical_flush();
		VLOGI("!!SIGSEGV!!");
		log_critical_flush();
		std::terminate();
	});
	
	term_default = std::get_terminate();
	std::set_terminate([]()
	{
		VLOGI("!!std::terminate()!!");
		log_critical_flush();
		for (auto& f : term_fs) if (f) f();
		
		term_default();
	});
}
void log_terminate_h_reset()
{
	std::set_terminate( std::move(term_default) );
}
RAII_Guard log_terminate_reg(void(*f)())
{
	size_t i = 0;
	for (; i < term_fs.size(); ++i) if (!term_fs[i]) break;
	if (i == term_fs.size()) term_fs.emplace_back();
	term_fs[i] = f;
	return RAII_Guard([i](){ term_fs[i] = nullptr; });
}



LoggerSettings LoggerSettings::current()
{
	return lsets;
}
void LoggerSettings::apply()
{
	std::unique_lock lock (buf_lock);
	lsets = *this;
	buf_lines.resize( lsets.lines );
	for (auto &l : buf_lines) l.second.resize( lsets.lines_width );
}



bool log_test_level(LogLevel level)
{
	return level >= lsets.level;
}
void log_write_str(LogLevel level, const char *str, size_t length)
{
	if (level < lsets.level) return;
	
	char prefix = '@';
	int color = 7|8;
	
	switch (level)
	{
	case LogLevel::Ignored:  return;
	case LogLevel::Verbose:  prefix = 'V'; color = 7|8; break;
	case LogLevel::Debug:    prefix = '-'; color = 6;   break;
	case LogLevel::Info:     prefix = 'I'; color = 2;   break;
	case LogLevel::Warning:  prefix = 'W'; color = 3|8; break;
	case LogLevel::Error:    prefix = 'E'; color = 1|8; break;
	case LogLevel::Critical: prefix = 'X'; color = 5|8; break;
	}
	
	thread_local std::string s;
	size_t pref_n;
	
	if (lsets.use_cons && lsets.use_clr)
	{
		s.push_back('\033');
		s.push_back('[');
		if (lsets.use_ext_clr)
		{
			if (color < 8) {
				s.push_back('3');
				s.push_back('0' + color);
			}
			else {
				s.push_back('9');
				s.push_back('0' + (color - 8));
			}
			pref_n = 5;
		}
		else {
			s.push_back(color & 8? '1' : '0');
			s.push_back(';');
			s.push_back('3');
			s.push_back('0' + (color & 7));
			pref_n = 7;
		}
		s.push_back('m');
		if (lsets.file_color) pref_n = 0;
	}
	else pref_n = 0;
	
	if (lsets.print_time)
	{
		auto now = std::chrono::system_clock::now();
		auto t_t = std::chrono::system_clock::to_time_t (now);
		auto t = *std::gmtime (&t_t);
		
		s.push_back(' ' + t.tm_hour / 10);
		s.push_back(' ' + t.tm_hour % 10);
		s.push_back(':');
		s.push_back(' ' + t.tm_min / 10);
		s.push_back(' ' + t.tm_min % 10);
		s.push_back(':');
		s.push_back(' ' + t.tm_sec / 10);
		s.push_back(' ' + t.tm_sec % 10);
		
		if (lsets.time_ms)
		{
			auto ms = std::chrono::time_point_cast <std::chrono::milliseconds> (now) .time_since_epoch() .count();
			s.push_back('.');
			s.push_back(' ' + ms % 1000 / 100);
			s.push_back(' ' + ms % 100 / 10);
			s.push_back(' ' + ms % 10);
		}
		s.push_back(' ');
	}
	if (lsets.use_prefix)
	{
		s.push_back(prefix);
		s.push_back(':');
		s.push_back(' ');
	}
	
	if (length == std::string::npos) length = strlen (str);
	s.insert( s.end(), str, str + length );
	
	if (lsets.use_cons)
	{
		if (lsets.use_clr) s.append( "\033[0m\n", 5 );
		else s.push_back('\n');
		auto f = level < LogLevel::Error ? stdout : stderr;
		fwrite( s.data(), 1, s.length(), f );
		if (lsets.use_clr) {
			s.resize( s.size() - 5 );
			s.push_back('\n');
		}
	}
	else s.push_back('\n');
	
	if (lsets.file && level >= lsets.file_level)
	{
		size_t n = s.length() - pref_n;
		lsets.file->write( s.data() + pref_n, n );
		
		if (lsets.file_flush)
		{
			log_flush_cou += n;
			if (log_flush_cou >= lsets.file_flush)
			{
				log_flush_cou = 0;
				lsets.file->flush();
			}
		}
	}
	if (lsets.lines) write_lines(level, str, length);
	
	s.clear();
}
void log_critical_flush()
{
	if (lsets.file) lsets.file->flush();
}
size_t log_get_lines(std::vector <std::pair <LogLevel, std::string>> &lines)
{
	lines = buf_lines;
	return buf_cur;
}
void log_assert_throw( const std::string& s )
{
	debugbreak();
	log_write_str( LogLevel::Critical, s.data(), s.size() );
	throw std::logic_error( s );
}
