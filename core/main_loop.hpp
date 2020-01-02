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
	
	static void create(InitWhich which); ///< Makes it current
	virtual void init() = 0; ///< Must be called after create
	virtual bool parse_arg(ArgvParse& arg);
	
	virtual void on_current() {} ///< Called when set current (but not after create())
	virtual void on_event(const SDL_Event&) {}
	virtual void render(TimeSpan frame_begin, TimeSpan passed) = 0; ///< Called each frame on unset renderer
	virtual ~MainLoop();
	
private:
	MainLoop* ml_prev = nullptr;
};

#endif // MAIN_LOOP_HPP
