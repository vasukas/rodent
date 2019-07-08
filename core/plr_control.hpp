#ifndef PLR_CONTROL_HPP
#define PLR_CONTROL_HPP

#include <optional>
#include <memory>
#include <mutex>
#include <SDL2/SDL_events.h>
#include "gamepad.hpp"



class PlayerControl
{
public:
	enum Action
	{
		A_NONE,
		A_LEFT, A_UP, A_DOWN, A_RIGHT, // used internally
		A_SHOOT,
		
		A_ACCEL, A_NEXTWPN, A_PREVWPN,
		A_WPN_FLAG = 0x100 ///< OR'ed with index
	};
	struct Bind
	{
		int act;
		std::string name, descr;
		
		SDL_Scancode key = SDL_SCANCODE_UNKNOWN;
		Gamepad::Button but = Gamepad::TOTAL_BUTTONS;
		bool heldable = false; ///< If true, can be held
		
		bool value = false;
	};
	
	std::unique_ptr<Gamepad> gpad;
	std::vector<Bind> binds; ///< Used automatically
	
	float aim_dead_zone = 0.5; ///< Minimal target distance
	float gpad_aim_dist = 20; ///< Maximum target distance with gamepad
	
	
	
	PlayerControl(Gamepad* gpad = nullptr); ///< Inits all binds with defaults
	
	void on_event(const SDL_Event& ev);
	std::vector<int> update(); ///< Before quering state. Returns actions
	
	/// Note: lock NOT used by default
	[[nodiscard]] std::unique_lock<std::mutex> lock() {return std::unique_lock(evs_lock);}
	
	vec2fp get_move(); ///< Returns movement direction (normalized)
	std::optional<vec2fp> get_tarp(); ///< Returns target position
	
	bool is_aiming(); ///< Returns true if aiming (get_tarp still may return false)
	bool is_tar_rel(); ///< Returns true if tarp and aiming return relative positions instead of absolute
	
private:
	std::mutex evs_lock;
	std::vector<int> as_list;
	vec2fp kmov = {}; // keyboard move
};

#endif // PLR_CONTROL_HPP
