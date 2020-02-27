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
	TimeSpan() { mks_value = 0; }
	
	static const TimeSpan nearinfinity; ///< Huge value (for rendering) which can be added without overflow
	static TimeSpan since_start(); ///< Returns amount of time passed since program start using steady clock
	
	[[nodiscard]] static constexpr TimeSpan fps(int t) {return seconds( 1.f / t );}
	int fps() const {return 1.f / seconds();}
	
	[[nodiscard]] static constexpr TimeSpan seconds( double  t ) { return TimeSpan( t * 1000 * 1000 ); }
	[[nodiscard]] static constexpr TimeSpan ms     ( int     t ) { return TimeSpan( t * 1000 ); }
	[[nodiscard]] static constexpr TimeSpan micro  ( int64_t t ) { return TimeSpan( t ); }
	
	constexpr double  seconds() const { return mks_value / (1000.f * 1000.f); }
	constexpr int     ms()      const { return mks_value / 1000; }
	constexpr int64_t micro()   const { return mks_value; }
	
	void set_seconds( double  t ) { mks_value = t * 1000 * 1000; }
	void set_ms     ( int     t ) { mks_value = t * 1000; }
	void set_micro  ( int64_t t ) { mks_value = t; }
	
	TimeSpan diff( const TimeSpan& t ) const; ///< Difference - always positive
	
	TimeSpan  operator - ( const TimeSpan& t ) const { return TimeSpan( mks_value - t.mks_value ); }
	TimeSpan& operator -=( const TimeSpan& t )       { mks_value -= t.mks_value; return *this; }
	
	TimeSpan  operator + ( const TimeSpan& t ) const { return TimeSpan( mks_value + t.mks_value ); }
	TimeSpan& operator +=( const TimeSpan& t )       { mks_value += t.mks_value; return *this; }
	
	TimeSpan  operator * ( float t ) const { return TimeSpan( mks_value * t ); }
	TimeSpan& operator *=( float t )       { mks_value *= t; return *this; }
	
	TimeSpan operator -() const { return TimeSpan(-mks_value); }
	
	constexpr double operator / ( const TimeSpan& t ) const {
		if (!t.mks_value) return 0;
		return double(mks_value) / t.mks_value;
	}
	
	bool operator < ( const TimeSpan& t ) const { return mks_value <  t.mks_value; }
	bool operator > ( const TimeSpan& t ) const { return mks_value >  t.mks_value; }
	bool operator <=( const TimeSpan& t ) const { return mks_value <= t.mks_value; }
	bool operator >=( const TimeSpan& t ) const { return mks_value >= t.mks_value; }
	
	bool is_negative() const {return mks_value < 0;}
	bool is_positive() const {return mks_value > 0;}
	
private:
	int64_t mks_value;
	explicit constexpr TimeSpan( int64_t mks_value ) : mks_value (mks_value) {}
};


/// Sleeps current thread (negative-safe)
void sleep(TimeSpan time);

/// Same as sleep, but with more precise wait time
void precise_sleep(TimeSpan time);

#endif // TIME_HPP
