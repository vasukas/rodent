#ifndef POSTPROC_HPP
#define POSTPROC_HPP

#include "utils/color_manip.hpp"
#include "vaslib/vas_math.hpp"
#include "vaslib/vas_time.hpp"

class Texture;

class Postproc
{
public:
	static Postproc& get(); ///< Returns singleton
	virtual void ui_mode(bool enable) = 0; ///< Disables some renderers
	
	virtual void tint_reset() = 0; ///< Resets sequence, keeps current state
	virtual void tint_seq(TimeSpan time_to_reach, FColor target_mul, FColor target_add = FColor(0,0,0,0)) = 0;
	void tint_default(TimeSpan time_to_reach) {tint_seq(time_to_reach, FColor(1,1,1,1), FColor(0,0,0,0));}
	
	virtual void capture_begin(Texture* tex) = 0; ///< Texture must have same size as window
	virtual void capture_end() = 0;
	
	virtual void screen_shake(float power) = 0;
	
	struct Smoke {
		vec2fp at;
		vec2fp vel = {}; // velocity
		float radius = 2.5;
		TimeSpan et = TimeSpan::seconds(2); // enable
		TimeSpan lt = TimeSpan::seconds(1); // static
		TimeSpan ft = TimeSpan::seconds(6); // fade
		float alpha = 1; // max alpha
		bool expand = false;
	};
	virtual void add_smoke(const Smoke& s) = 0;
	
protected:
	friend class RenderControl_Impl;
	static Postproc* init(); ///< Creates singleton
	virtual void render() = 0;
	virtual void render_reset() = 0;
	virtual ~Postproc();
};

#endif // POSTPROC_HPP
