#ifndef PLR_CONTROL_HPP
#define PLR_CONTROL_HPP

#include <optional>
#include <memory>
#include <mutex>
#include <SDL2/SDL_events.h>
#include "gamepad.hpp"



class PlayerController
{
public:
	enum KeyState
	{
		K_OFF,
		K_HELD, ///< for some time
		K_JUST, ///< just pressed
		K_ONCE  ///< pressed & depressed at same step
	};
	
	enum BindType
	{
		BT_ONESHOT,
		BT_HELD
	};
	
	enum {
		MOUSE_NONE = 0,
		MOUSE_WHEELDOWN = 16,
		MOUSE_WHEELUP
	};
	
	struct Bind
	{
		BindType type = BT_ONESHOT;
		std::string name, descr;
		
		SDL_Scancode key = SDL_SCANCODE_UNKNOWN;
		int mou = MOUSE_NONE; // SDL_BUTTON_* or MOUSE_*
		Gamepad::Button but = Gamepad::B_NONE;
		
		// state
		KeyState st_key = K_OFF;
		KeyState st_mou = K_OFF;
		KeyState st_but = K_OFF;
	};
	
	enum Action
	{
		A_ACCEL,
		A_SHOOT,
		A_CAM_FOLLOW,
		A_LASER_DESIG,
		
		A_WPN_PREV,
		A_WPN_NEXT,
		A_WPN_1,
		A_WPN_2,
		A_WPN_3,
		A_WPN_4,
		
		// internal
		AX_MOV_Y_NEG,
		AX_MOV_X_NEG,
		AX_MOV_Y_POS,
		AX_MOV_X_POS,
		
		/// Do not use
		ACTION_TOTAL_COUNT_INTERNAL
	};
	
	struct State
	{
		std::array<bool, ACTION_TOTAL_COUNT_INTERNAL> is = {}; ///< Is enabled
		std::vector<Action> acts; ///< Oneshot actions triggered
		
		vec2fp mov = {}; ///< Movement delta [-1; 1]
		vec2fp tar_pos = {}; ///< Target position (world)
	};
	
	float gpad_aim_dist = 20; ///< Maximum target distance with gamepad
	
	
	
	PlayerController(std::unique_ptr<Gamepad> gpad = {}); ///< Inits all binds with defaults
	
	void on_event(const SDL_Event& ev);
	void update(); ///< Must be called after getting all events
	const State& get_state() const {return state;} ///< Last updated state
	
	[[nodiscard]] auto lock() {return std::unique_lock(mutex);} ///< Not used internally
	
private:
	std::array<Bind, ACTION_TOTAL_COUNT_INTERNAL> binds;
	std::unique_ptr<Gamepad> gpad;
	
	State state;
	std::mutex mutex;
	
	bool is_enabled(size_t i) const;
};

#endif // PLR_CONTROL_HPP
