#include <SDL2/SDL.h>
#include <SDL2/SDL_gamecontroller.h>
#include "vaslib/vas_cpp_utils.hpp"
#include "vaslib/vas_log.hpp"
#include "gamepad.hpp"

class Gamepad_Impl : public Gamepad
{
public:
	SDL_GameController* gc;
	float dead_zone = 0.15;
	
	Gamepad_Impl(SDL_GameController* gc): gc(gc)
	{
		VLOGI("Using controller: {}", SDL_GameControllerName(gc));
		char *m = SDL_GameControllerMapping(gc);
		if (!m) VLOGI("SDL_GameControllerMapping failed - {}", SDL_GetError());
		else {
			VLOGI("Mapping: {}", m);
			SDL_free(m);
		}
	}
	~Gamepad_Impl()
	{
		SDL_GameControllerClose(gc);
		SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
	}
	bool get_state(Button b)
	{
		SDL_GameControllerButton i;
		switch (b)
		{
#define X(n) case B_##n: i = SDL_CONTROLLER_BUTTON_##n; break
		X(Y); X(B); X(A); X(X);
#undef X
#define X(n) case B_##n: i = SDL_CONTROLLER_BUTTON_DPAD_##n; break
		X(UP); X(DOWN); X(LEFT); X(RIGHT);
#undef X
		case B_SHLD_LEFT:  i = SDL_CONTROLLER_BUTTON_LEFTSHOULDER; break;
		case B_SHLD_RIGHT: i = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER; break;
		case TOTAL_BUTTONS: return 0;
		}
		return 0 != SDL_GameControllerGetButton(gc, i);
	}
	vec2fp get_left()
	{
		return {get_axis(SDL_CONTROLLER_AXIS_LEFTX), get_axis(SDL_CONTROLLER_AXIS_LEFTY)};
	}
	vec2fp get_right()
	{
		return {get_axis(SDL_CONTROLLER_AXIS_RIGHTX), get_axis(SDL_CONTROLLER_AXIS_RIGHTY)};
	}
	float trig_left()
	{
		return get_axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT);
	}
	float trig_right()
	{
		return get_axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
	}
	float get_axis(SDL_GameControllerAxis i)
	{
		float v = SDL_GameControllerGetAxis(gc, i);
		v /= 32768.f;
		if (std::fabs(v) < dead_zone) v = 0;
		return v;
	}
};
Gamepad* Gamepad::open_default()
{
	bool was_init = (SDL_WasInit(0) & SDL_INIT_GAMECONTROLLER) == SDL_INIT_GAMECONTROLLER;
	
	if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER)) {
		VLOGE("Gamepad::open_default() SDL_InitSubSystem failed - {}", SDL_GetError());
		return nullptr;
	}
	RAII_Guard g([]{SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);});
	
	int jn = SDL_NumJoysticks();
	if (jn < 0) {
		VLOGE("Gamepad::open_default() SDL_NumJoysticks failed - {}", SDL_GetError());
		return nullptr;
	}
	
	SDL_GameController* gc = nullptr;
	for (int i=0; i<jn; ++i)
	{
		if (SDL_IsGameController(i))
		{
			VLOGD("Gamepad::open_default() trying {}/{}: {}", i, jn, SDL_GameControllerNameForIndex(i));
			gc = SDL_GameControllerOpen(i);
			
			if (!gc) VLOGE("Gamepad::open_default() SDL_GameControllerOpen failed - {}", SDL_GetError());
			else break;
		}
		else VLOGD("Gamepad::open_default() not a controller: {}", SDL_JoystickNameForIndex(i));
	}
	if (!gc) {
		VLOGE("Gamepad::open_default() no suitable controllers found");
		return nullptr;
	}
	
	if (!was_init)
		SDL_GameControllerEventState(SDL_IGNORE);
	
	g.cancel();
	return new Gamepad_Impl(gc);
}
void Gamepad::update()
{
	if (SDL_WasInit(0) & SDL_INIT_GAMECONTROLLER)
		SDL_GameControllerUpdate();
}
