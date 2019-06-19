#ifndef MAIN_LOOP_HPP
#define MAIN_LOOP_HPP

#include <SDL2/SDL_events.h>
#include "vaslib/vas_time.hpp"

class MainLoop
{
public:
	static MainLoop* current; ///< If becames nullptr, main loop immediatly exits

	enum InitWhich
	{
		INIT_DEFAULT,
		INIT_RENTEST,
		INIT_GAME
	};
	
	static void init(InitWhich which);
	virtual void on_event(SDL_Event&) {}
	virtual void render(TimeSpan passed) = 0; ///< Called each frame on unset renderer
	virtual ~MainLoop();
};

#endif // MAIN_LOOP_HPP
