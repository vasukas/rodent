#ifndef TIME_UTILS_HPP
#define TIME_UTILS_HPP

#include "vaslib/vas_cpp_utils.hpp"
#include "vaslib/vas_time.hpp"

float time_sine(TimeSpan full_period, float min = 0, float max = 1, TimeSpan time = TimeSpan::since_start());



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
	bool blink_mode = false; ///< If true, continues to increase value even if enabled is false
	
	SmoothSwitch(TimeSpan tmo = {}, std::optional<TimeSpan> tmo_out = {});
	void reset(TimeSpan tmo, std::optional<TimeSpan> tmo_out = {});
	
	void step(TimeSpan passed, bool enabled);
	
	float value() const; ///< [0-1]
	bool is_zero() const {return get_state() == OUT_ZERO;}
	
	enum OutputState {OUT_ZERO, OUT_RISING, OUT_ONE, OUT_FADING};
	OutputState get_state() const;
	
private:
	enum Stage {S_ZERO, S_UP, S_ENAB, S_SUST, S_DOWN};
	Stage stage = S_ZERO;
	TimeSpan tcou;
	
	void set_v(float v);
};


struct SmoothBlink
{
	/*
		Produce interpolation value for smooth blinking effect. 
		Note: disabled if AppSettings::plr_status_blink is false
	*/
	
	TimeSpan full_period = TimeSpan::seconds(0.9);
	
	/// Returns [t_min, t_max], changing by sine
	float get_sine(bool enabled);
	
	/// Returns [0, 1], changing linearly
	float get_blink(bool enabled = false);
	
	void trigger();
	void force_reset();
	
private:
	float t_base(bool enabled, float def, callable_ref<float(float)> proc);
	TimeSpan time;
};

#endif // TIME_UTILS_HPP
