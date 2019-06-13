#ifndef TIME_HPP
#define TIME_HPP

#include <cinttypes>
#include <string>



/// Returns string formatted as "23:59:59 [ 1 Jan 1970]"
std::string date_time_str();

/// Returns string formatted as "1970-01-01_23-59-59"
std::string date_time_fn();



/// Amount of time
struct TimeSpan
{
	int64_t mks_value;
	
	TimeSpan() { mks_value = 0; } ///< Inits to zero
	
	static TimeSpan since_start(); ///< Returns amount of time passed since program start using steady clock
	
	static TimeSpan seconds( double  t ) { return TimeSpan( t * 1000 * 1000 ); }
	static TimeSpan ms     ( int     t ) { return TimeSpan( t * 1000 ); }
	static TimeSpan micro  ( int64_t t ) { return TimeSpan( t ); }
	
	double  seconds() const { return mks_value / (1000.f * 1000.f); }
	int     ms()      const { return mks_value / 1000; }
	int64_t micro()   const { return mks_value; }
	
	TimeSpan diff( const TimeSpan& t ) const; ///< Difference - always positive
	
	TimeSpan  operator - ( const TimeSpan& t ) const { return TimeSpan( mks_value - t.mks_value ); }
	TimeSpan& operator -=( const TimeSpan& t )       { mks_value -= t.mks_value; return *this; }
	
	TimeSpan  operator + ( const TimeSpan& t ) const { return TimeSpan( mks_value + t.mks_value ); }
	TimeSpan& operator +=( const TimeSpan& t )       { mks_value += t.mks_value; return *this; }
	
	TimeSpan  operator * ( float t ) const { return TimeSpan( mks_value * t ); }
	TimeSpan& operator *=( float t )       { mks_value *= t; return *this; }
	
	double operator / ( const TimeSpan& t ) const { return double(mks_value) / t.mks_value; }
	
	bool operator < ( const TimeSpan& t ) const { return mks_value <  t.mks_value; }
	bool operator > ( const TimeSpan& t ) const { return mks_value >  t.mks_value; }
	bool operator <=( const TimeSpan& t ) const { return mks_value <= t.mks_value; }
	bool operator >=( const TimeSpan& t ) const { return mks_value >= t.mks_value; }
	
private:
	explicit TimeSpan( int64_t mks_value ) : mks_value (mks_value) {}
};


/// Sleeps current thread
void sleep(TimeSpan time);

#endif // TIME_HPP
