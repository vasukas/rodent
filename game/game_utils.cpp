#include "game_utils.hpp"

SmoothSwitch::SmoothSwitch(TimeSpan tmo, std::optional<TimeSpan> tmo_out) {set(tmo, tmo_out);}
void SmoothSwitch::set(TimeSpan tmo, std::optional<TimeSpan> tmo_out_new)
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
		if (!enabled) {
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
	return 0; // to silence warning
}
bool SmoothSwitch::is_zero() const
{
	return stage == S_ZERO;
}
void SmoothSwitch::set_v(float v)
{
	if		(stage == S_UP)   tcou = tmo_in  * v;
	else if (stage == S_DOWN) tcou = tmo_out * v;
}
