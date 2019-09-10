#ifndef GAME_UTILS_HPP
#define GAME_UTILS_HPP

#include <optional>
#include "vaslib/vas_time.hpp"



struct SmoothSwitch
{
	/*
		Smooths switching of some option (usually in UI).
		Value gradually increases to 1 while option is enabled,
		and decreases to 0 when disabled.
		
		If option is disabled before min_sus, value is sustained at 1 for that time.
	*/
	
	TimeSpan tmo_in, tmo_out;
	TimeSpan min_sus = {}; ///< Minimal time value is sustained at 1
	
	SmoothSwitch(TimeSpan tmo = {}, std::optional<TimeSpan> tmo_out = {});
	void set(TimeSpan tmo, std::optional<TimeSpan> tmo_out = {});
	
	void step(TimeSpan passed, bool enabled);
	
	float value() const; ///< [0-1]
	bool is_zero() const;
	
private:
	enum Stage {S_ZERO, S_UP, S_ENAB, S_SUST, S_DOWN};
	Stage stage = S_ZERO;
	TimeSpan tcou;
	
	void set_v(float v);
};

#endif // GAME_UTILS_HPP
