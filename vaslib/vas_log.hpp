#ifndef VAS_LOG_HPP
#define VAS_LOG_HPP

// Contains optional custom formatters, which enabled if included after headers with such types

#include <fmt/format.h>
#include <memory>
#include <vector>

class File;
struct RAII_Guard;



// needed only for MSVC
#define FREAK_MACRO_EXPAND( x ) x

#define VLOGV(...) FREAK_MACRO_EXPAND( VLOG_(LogLevel::Verbose,  __VA_ARGS__) )
#define VLOGD(...) FREAK_MACRO_EXPAND( VLOG_(LogLevel::Debug,    __VA_ARGS__) )
#define VLOGI(...) FREAK_MACRO_EXPAND( VLOG_(LogLevel::Info,     __VA_ARGS__) )
#define VLOGW(...) FREAK_MACRO_EXPAND( VLOG_(LogLevel::Warning,  __VA_ARGS__) )
#define VLOGE(...) FREAK_MACRO_EXPAND( VLOG_(LogLevel::Error,    __VA_ARGS__) )
#define VLOGC(...) FREAK_MACRO_EXPAND( VLOG_(LogLevel::Critical, __VA_ARGS__) ), VAS_LOG_AT(VLOGE)

/// Displays critical message, current position in file, forces flush and calls debugbreak
#define VLOGX(...) \
	(FREAK_MACRO_EXPAND( VLOG_(LogLevel::Critical, __VA_ARGS__) ), \
	 VAS_LOG_AT(VLOGE), \
	 log_critical_flush(), \
	 debugbreak())
//	VLOG_(LogLevel::Critical, "{}", get_backtrace()),



/// Just throws exception with formatted string
#define THROW_FMTSTR( FORMAT, ... ) \
	throw std::runtime_error( fmt::format( FREAK_MACRO_EXPAND( FMT_STRING(FORMAT) ), ##__VA_ARGS__ ).data() )

/// Writes error message to log and throws exception
#define LOG_THROW( FORMAT, ... ) \
	( FREAK_MACRO_EXPAND( VLOGE(FORMAT, ##__VA_ARGS__) ), \
	  throw std::runtime_error( std::string( log_fmt_buffer.data(), log_fmt_buffer.size() ) ) )

/// Writes critical error message to log and throws exception
#define LOG_THROW_X( FORMAT, ... ) \
	( FREAK_MACRO_EXPAND( VLOGX(FORMAT, ##__VA_ARGS__) ), \
	  throw std::runtime_error( std::string( log_fmt_buffer.data(), log_fmt_buffer.size() ) ) )

/// Writes message to log and throws std::logic_error
#define ASSERT( COND, INFO ) \
	(( COND )? void() : log_assert_throw (fmt::format( \
		FMT_STRING("Assertion failed ({}): \"{}\" at {}:{:03}:{}"), INFO, #COND, __FILE__, __LINE__, __func__ )))

/*
/// Returns pointer to static buffer
#define FAST_FMT( FORMAT, ... ) \
	( log_fmt_buffer.clear(), \
	  fmt::format_to(log_fmt_buffer, FREAK_MACRO_EXPAND( FMT_STRING(FORMAT) ), ##__VA_ARGS__), \
	  log_fmt_buffer.push_back( 0 ), \
	  log_fmt_buffer.data() )
*/

#define FMT_FORMAT( FORMAT, ... ) \
	fmt::format(FREAK_MACRO_EXPAND( FMT_STRING(FORMAT) ), ##__VA_ARGS__)

#define LOG_OR_THROW( DO_THROW, FORMAT, ... ) \
	(log_fmt_buffer.clear(), \
     fmt::format_to(log_fmt_buffer, FREAK_MACRO_EXPAND( FMT_STRING(FORMAT) ), ##__VA_ARGS__), \
	 ((DO_THROW)? \
      throw std::runtime_error( std::string(log_fmt_buffer.data(), log_fmt_buffer.size())) : \
	  log_write_str(LogLevel::Error, log_fmt_buffer.data(), log_fmt_buffer.size()) ))



/// Initiates breakpoint (uses SIGTRAP on UNIX)
void debugbreak();

/// Returns stack trace as a string 
// std::string get_backtrace();

/// Returns demangled name
std::string human_readable(const std::type_info& t);



/// Sets signal and termination handlers
void log_setup_signals();

/// Unsets handler
void log_terminate_h_reset();

/// Registers function to be called ONLY on abnormal program termination. 
/// Guard removes function registration. 
/// Note: works only if log_setup_signals() was called
[[nodiscard]] RAII_Guard log_terminate_reg(void(*f)());



enum class LogLevel
{
	Ignored, ///< Not printed
	Verbose, ///< (white V)
	Debug,   ///< (blue -)
	Info,    ///< (green I)
	Warning, ///< (yellow W)
	Error,   ///< (red E)
	Critical ///< (purple X)
};



struct LoggerSettings
{
	LogLevel level = LogLevel::Verbose; ///< Only messages with same or higher level are passed
	
	// global params
	bool use_prefix = true;  ///< Use prefix indicating message type
	bool print_time = false; ///< Print message time (using system clock)
	bool time_ms = true;     ///< If true, prints time with milliseconds instead of just seconds
	
	// file params
	std::shared_ptr< File > file; ///< File pointer to write to
	size_t file_flush = 0;   ///< Force file flush after N bytes written (0 to disable)
	bool file_color = false; ///< Write ANSI sequences to log file
	LogLevel file_level = LogLevel::Verbose; ///< Additional level for file
	
	// stdout params
	bool use_cons = true; ///< Write log to stdout/stderr
	bool use_clr = true;  ///< Use colors for indicating message type
	bool use_ext_clr = true; ///< 16-clr palette instead of 8-clr + bold
	
	// line buffer params
	size_t lines = 0; ///< Number of saved lines (0 to disable)
	size_t lines_width = 80; ///< Length of each line (excluding terminator)
	
	
	
	/// Returns current settings
	static LoggerSettings current();
	
	/// Sets new settings
	void apply();
};



/// Returns if message with such level should be displayed
bool log_test_level(LogLevel level);

/// Actual logger function - prepares message and writes it to all devices
void log_write_str(LogLevel level, const char *str, size_t length = std::string::npos);

/// Forcefully flush file
void log_critical_flush();

/// Fills line buffer and returns index of last message
size_t log_get_lines(std::vector <std::pair <LogLevel, std::string>> &lines);

/// Logs string as critical and throws std::logic_error
[[noreturn]] void log_assert_throw( const std::string& s );



extern thread_local fmt::memory_buffer log_fmt_buffer; ///< (Internal usage)

#define VAS_LOG_AT(LOGFUNC) LOGFUNC(" at {}:{:03}:{}", __FILE__, __LINE__, __func__)

#define VLOG_(LEVEL, FORMAT, ...) \
	(log_test_level(LEVEL)?  \
	 (log_fmt_buffer.clear(), \
	 fmt::format_to(log_fmt_buffer, FREAK_MACRO_EXPAND( FMT_STRING(FORMAT) ), ##__VA_ARGS__), \
	 log_write_str(LEVEL, log_fmt_buffer.data(), log_fmt_buffer.size())) : void() )

#endif // VAS_LOG_HPP



#if defined(VAS_MATH_HPP) && !defined(VAS_MATH_LOGFORMAT)
#define VAS_MATH_LOGFORMAT

namespace fmt {
template<> struct formatter<vec2i> {
	template <typename ParseContext>
	constexpr auto parse(ParseContext& ctx) {return ctx.begin();}
	
	template <typename FormatContext>
	auto format(const vec2i& p, FormatContext& ctx) {
		return format_to(ctx.begin(), "{}:{}", p.x, p.y);
	}
};

template<> struct formatter<vec2fp> {
	template <typename ParseContext>
	constexpr auto parse(ParseContext& ctx) {return ctx.begin();}
	
	template <typename FormatContext>
	auto format(const vec2fp& p, FormatContext& ctx) {
		return format_to(ctx.begin(), "{}:{}", p.x, p.y);
	}
};
}

#endif // VAS_MATH_HPP



#if VAS_LOG_OSTREAM

#include <ostream>

struct vaslog_ostream : std::streambuf, std::ostream {
	std::vector<char> mem;
	LogLevel level;
	
	vaslog_ostream(LogLevel level): std::ostream(this), level(level) {}
	int overflow(int c) {
		if (c == '\n') {
			log_write_str(level, mem.data(), mem.size());
			mem.clear();
		}
		else mem.push_back(c);
		return 0;
	}
};

#endif // VAS_LOG_OSTREAM
