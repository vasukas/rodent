#ifndef MAIN_LOOP_HPP
#define MAIN_LOOP_HPP

#include <optional>
#include <SDL2/SDL_events.h>
#include "vaslib/vas_misc.hpp"
#include "vaslib/vas_time.hpp"

class MainLoop
{
public:
	static inline bool is_debug_mode = false;
	static inline std::optional<std::string> startup_date = {}; ///< If set, used for automatic filenames
	
	static MainLoop* current; ///< If becames nullptr, main exits
	MainLoop* ml_prev = nullptr;
	
	static size_t show_ask_box(std::string text, std::vector<std::string> responses);
	static void show_error_msg(std::string text, std::string title = "Error");
	static void show_internal_error(std::string text);
	
	enum InitWhich
	{
		INIT_MAINMENU,
		INIT_GAME,
		INIT_KEYBIND,
		INIT_HELP,
		INIT_OPTIONS,
		
		INIT_DEFAULT,
		INIT_DEFAULT_CLI
	};
	
	static void create(InitWhich which, bool do_init = true); ///< Makes it current	
	virtual void init() {} ///< Must be called after create
	virtual bool parse_arg(ArgvParse& arg);
	
	virtual void on_current() {} ///< Called when set current (but not after create())
	virtual void on_event(const SDL_Event&) {}
	virtual void render(TimeSpan frame_begin, TimeSpan passed) = 0; ///< Called each frame on unset renderer
	virtual ~MainLoop();
	
	bool is_in_game() const;
};

#endif // MAIN_LOOP_HPP
