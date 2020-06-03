#ifndef GAME_UI_HPP
#define GAME_UI_HPP

#include "game_control.hpp"
union SDL_Event;

class GameUI
{
public:
	struct InitParams
	{
		GameControl* ctr;
		std::string init_greet;
		bool debug_menu = true;
		bool allow_cheats = false;
		bool start_paused = false;
	};
	
	static GameUI* create(InitParams pars); ///< Must be called from render thread
	virtual ~GameUI() = default;
	
	virtual void on_leave() = 0; ///< Call before entering any menues
	virtual void on_enter() = 0; ///< Call after leaving all menues
	
	virtual void on_event(const SDL_Event& ev) = 0; ///< Processes only internal events
	virtual void render(TimeSpan frame_time, TimeSpan passed) = 0;
	
	virtual void enable_debug_mode() = 0;
	virtual bool has_game_finished() = 0;
	static std::string generate_greet();
};

#endif // GAME_UI_HPP
