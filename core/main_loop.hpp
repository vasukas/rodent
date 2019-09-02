#ifndef MAIN_LOOP_HPP
#define MAIN_LOOP_HPP

#include <SDL2/SDL_events.h>
#include "vaslib/vas_misc.hpp"
#include "vaslib/vas_time.hpp"

class MainLoop
{
public:
	static MainLoop* current; ///< If becames nullptr, main exits

	enum InitWhich
	{
		INIT_DEFAULT,
		INIT_GAME,
		INIT_SETTINGS
	};
	
	static MainLoop* create(InitWhich which); ///< And makes current
	virtual void init() = 0; ///< Called after engine init
	virtual bool parse_arg(ArgvParse& arg);
	
	virtual void on_event(SDL_Event&) {}
	virtual void render(TimeSpan passed) = 0; ///< Called each frame on unset renderer
	virtual ~MainLoop();
	
private:
	MainLoop* ml_prev = nullptr;
};

#endif // MAIN_LOOP_HPP
