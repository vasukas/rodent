#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>
#include "vaslib/vas_time.hpp"

std::string date_time_str()
{
	auto now = std::chrono::system_clock::now();
	auto now_t = std::chrono::system_clock::to_time_t(now);
	std::stringstream ss;
//	ss << std::put_time( std::localtime( &now_t ), "%H:%M:%S [%e %b %Y]" );
	ss << std::put_time( std::localtime( &now_t ), "%H:%M:%S [%d %b %Y]" );
	return ss.str();
}
std::string date_time_fn()
{
	auto now = std::chrono::system_clock::now();
	auto now_t = std::chrono::system_clock::to_time_t(now);
	std::stringstream ss;
//	ss << std::put_time( std::localtime( &now_t ), "%F_%H-%M-%S" );
	ss << std::put_time( std::localtime( &now_t ), "%Y-%m-%d_%H-%M-%S" );
	return ss.str();
}
TimeSpan TimeSpan::since_start()
{
	static std::chrono::time_point start_time = std::chrono::steady_clock::now();
	auto diff = std::chrono::steady_clock::now() - start_time;
	auto mks = std::chrono::duration_cast< std::chrono::microseconds >( diff );
	return TimeSpan( mks.count() );
}
TimeSpan TimeSpan::diff( const TimeSpan& t ) const
{
	auto d = mks_value - t.mks_value;
	return TimeSpan( mks_value > t.mks_value ? d : -d );
}
double TimeSpan::operator / ( const TimeSpan& t ) const
{
	if (!t.mks_value) return 0;
	return double(mks_value) / t.mks_value;
}
void sleep(TimeSpan time)
{
	auto mk = time.micro();
	if (mk > 0) std::this_thread::sleep_for( std::chrono::microseconds(mk) );
}

#ifdef _WIN32
#include "vaslib/wincompat.hpp"
void precise_sleep(TimeSpan time)
{
	auto mk = time.micro();
	if (mk > 0) winc_sleep(mk);
}
#else
void precise_sleep(TimeSpan time)
{
	sleep(time);
}
#endif
