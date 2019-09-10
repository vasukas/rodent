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
		
		B_RC_UP,    ///< Y, 1, triangle
		B_RC_RIGHT, ///< B, 2, circle
		B_RC_DOWN,  ///< A, 3, cross
		B_RC_LEFT,  ///< X, 4, square

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
		
		// control
		B_BACK,  ///< or 'select'
		B_START, ///< or 'forward'
		
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
	
	virtual void* get_raw() = 0; ///< Returns SDL_GameController*
	static void update();
};

#endif // GAMEPAD_HPP
