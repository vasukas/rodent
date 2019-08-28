#ifndef GAMEPAD_HPP
#define GAMEPAD_HPP

#include "vaslib/vas_math.hpp"

union SDL_Event;

class Gamepad
{
public:
	enum Button
	{
		B_NONE, ///< Never reported
		
		B_Y, ///< up - Y, triangle, 1
		B_B, ///< right - B, circle, 2
		B_A, ///< down - A, cross, 3
		B_X, ///< left - X, square, 4

		// D-Pad
		B_UP,
		B_DOWN,
		B_LEFT,
		B_RIGHT,
		
		// shoulder buttons
		B_SHLD_LEFT,
		B_SHLD_RIGHT,
		
		// trigger press
		B_TRIG_LEFT,
		B_TRIG_RIGHT,
		
		/// Do not use
		TOTAL_BUTTONS_INTERNAL
	};

	static Gamepad* open_default();
	virtual ~Gamepad() = default;

	virtual bool get_state(Button b) = 0;
	virtual vec2fp get_left () = 0; ///< [-1; 1] range, excluding dead zone
	virtual vec2fp get_right() = 0; ///< [-1; 1] range, excluding dead zone
	virtual float trig_left () = 0; ///< [0; 1] range, excluding dead zone
	virtual float trig_right() = 0; ///< [0; 1] range, excluding dead zone
	
	static void update();
};

#endif // GAMEPAD_HPP
