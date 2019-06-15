#ifndef MAIN_LOOP_HPP
#define MAIN_LOOP_HPP

#include <SDL2/SDL_events.h>
#include "vaslib/vas_time.hpp"

class MainLoop
{
public:
	static MainLoop* current; ///< If becames nullptr, main loop immediatly exits
	TimeSpan step_each = TimeSpan::seconds(1. / 30); ///< Loop step length (ms)
	
	enum InitWhich
	{
		INIT_DEFAULT,
		INIT_RENTEST
	};
	
	static void init(InitWhich which);
	virtual void prepare() {} ///< Called at the start of the loop
	virtual void on_event(SDL_Event&) {}
	virtual void step() {} ///< Called after events each 'step_each' ms, can be from 0 to N times per rendering frame
	virtual void render(TimeSpan passed) = 0; ///< Called each frame on unset renderer
	virtual ~MainLoop();
};

#endif // MAIN_LOOP_HPP
