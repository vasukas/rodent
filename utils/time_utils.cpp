#include "core/settings.hpp"
#include "render/control.hpp"
#include "time_utils.hpp"

float time_sine(TimeSpan full_period, float min, float max, TimeSpan time) {
	if (full_period.ms() < 0) return max;
	float t = std::fmod(time.seconds(), full_period.seconds()) / full_period.seconds();
	t = sine_lut_norm(t);
	return min + (max - min) * (t + 1)/2;
}


SmoothSwitch::SmoothSwitch(TimeSpan tmo, std::optional<TimeSpan> tmo_out) {reset(tmo, tmo_out);}
void SmoothSwitch::reset(TimeSpan tmo, std::optional<TimeSpan> tmo_out_new)
{
	float v = value();
	tmo_in = tmo;
	tmo_out = tmo_out_new? *tmo_out_new : tmo_in;
	set_v(v);
}
void SmoothSwitch::step(TimeSpan passed, bool enabled)
{
	switch (stage)
	{
	case S_ZERO:
		if (!enabled) break;
		tcou = {};
		stage = S_UP;
		[[fallthrough]];
		
	case S_UP:
		if (!enabled && !blink_mode) {
			float v = value();
			stage = S_DOWN;
			set_v(v);
		}
		else {
			tcou += passed;
			if (tcou >= tmo_in) {
				tcou = min_sus;
				stage = S_ENAB;
			}
		}
		break;
		
	case S_ENAB:
		if (enabled) {
			if (!tcou.is_negative())
				tcou -= passed;
			break;
		}
		[[fallthrough]];
		
	case S_SUST:
		if (enabled) stage = S_ENAB;
		else {
			tcou -= passed;
			if (tcou.is_negative()) {
				tcou = tmo_out;
				stage = S_DOWN;
			}
		}
		break;
		
	case S_DOWN:
		if (enabled) {
			float v = value();
			stage = S_UP;
			set_v(v);
		}
		else {
			tcou -= passed;
			if (tcou.is_negative())
				stage = S_ZERO;
		}
	}
}
float SmoothSwitch::value() const
{
	switch (stage)
	{
	case S_ZERO: return 0;
	case S_UP:   return tcou / tmo_in;
	case S_ENAB: return 1;
	case S_SUST: return 1;
	case S_DOWN: return tcou / tmo_out;
	}
	return 0; // silence warning
}
SmoothSwitch::OutputState SmoothSwitch::get_state() const
{
	switch (stage)
	{
	case S_ZERO: return OUT_ZERO;
	case S_UP:   return OUT_RISING;
	case S_ENAB: return OUT_ONE;
	case S_SUST: return OUT_ONE;
	case S_DOWN: return OUT_FADING;
	}
	return OUT_ZERO; // silence warning
}
void SmoothSwitch::set_v(float v)
{
	if		(stage == S_UP)   tcou = tmo_in  * v;
	else if (stage == S_DOWN) tcou = tmo_out * v;
}



float SmoothBlink::get_sine(bool enabled)
{
	return t_base(enabled, 1, [](float t)
	{
		const float t_min = 0.3, t_max = 2.2; // sine
		t = sine_lut_norm(t);
		return t >= 0 ? lerp(1, t_max, t) : lerp(1, t_min, -t);
	});
}
float SmoothBlink::get_blink(bool enabled)
{
	return t_base(enabled, 0, [](float t){
		return float(t < 0.5 ? t*2 : 2 - t*2);
	});
}
void SmoothBlink::trigger()
{
	if (time.is_positive())
	{
		float t = fracpart(time / full_period);
		if (t > 0.5) t = 1 - t;
		time = full_period * t;
	}
	else time = TimeSpan::micro(1);
}
void SmoothBlink::force_reset()
{
	time = {};
}
float SmoothBlink::t_base(bool enabled, float def, callable_ref<float(float)> proc)
{
	if (!AppSettings::get().plr_status_blink) return def;
	if (time.is_positive() || enabled)
	{
		if (enabled) time += RenderControl::get().get_passed();
		else {
			time = full_period * fracpart(time / full_period);
			time += RenderControl::get().get_passed();
			if (time > full_period) time = {};
		}
		return proc(fracpart(time / full_period));
	}
	return def;
}
