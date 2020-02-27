#ifndef PLR_CONTROL_HPP
#define PLR_CONTROL_HPP

#include <bitset>
#include <optional>
#include <memory>
#include <mutex>
#include <SDL2/SDL_events.h>
#include "gamepad.hpp"



class PlayerController
{
public:
	static bool allow_cheats;
	
	enum KeyState
	{
		K_OFF,
		K_HELD, ///< for some time
		K_JUST, ///< just pressed
		K_ONCE  ///< pressed & depressed at same step
	};
	
	enum BindType
	{
		BT_ONESHOT, ///< Set as enabled only on initial press
		BT_HELD, ///< Set as enabled while pressed
		BT_SWITCH ///< Each initial press switches value
	};
	
	enum {
		MOUSE_NONE = 0,
		MOUSE_WHEELDOWN = 16,
		MOUSE_WHEELUP
	};
	
	struct InputMethod
	{
		struct Name {
			std::string str = "---";
			bool is_icon = false;
		};
		
		KeyState state = K_OFF;
		Name name;
		
		virtual ~InputMethod() = default;
	};
	struct IM_Key : InputMethod
	{
		SDL_Scancode v = SDL_SCANCODE_UNKNOWN;
		static Name get_name(SDL_Scancode v);
		void operator=(SDL_Scancode v);
	};
	struct IM_Mouse : InputMethod
	{
		int v = MOUSE_NONE; // SDL_BUTTON_* or MOUSE_*
		static Name get_name(int v);
		void operator=(int v);
	};
	struct IM_Gpad : InputMethod
	{
		Gamepad::Button v = Gamepad::B_NONE;
		static Name get_name(Gamepad::Button v);
		void operator=(Gamepad::Button v);
	};
	
	struct Bind
	{
		BindType type = BT_ONESHOT;
		std::string name, descr;
		
		IM_Key   key, alt;
		IM_Mouse mou;
		IM_Gpad  but;
		bool sw_val = false; ///< Switch value
		
		const std::array<InputMethod*, 4> ims;
		Bind();
	};
	
	enum Action
	{
		A_ACCEL,
		A_SHOOT,
		A_SHOOT_ALT,
		A_SHIELD_SW,
		A_INTERACT,
		
		A_CAM_FOLLOW,
		A_CAM_CLOSE_SW,
		
		A_LASER_DESIG,
		A_SHOW_MAP,
		
		A_WPN_PREV,
		A_WPN_NEXT,
		A_WPN_1,
		A_WPN_2,
		A_WPN_3,
		A_WPN_4,
		A_WPN_5,
		A_WPN_6,
		
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
		std::bitset<ACTION_TOTAL_COUNT_INTERNAL> is = {}; ///< Is enabled/triggered
		std::vector<Action> acts; ///< Oneshot actions triggered
		
		vec2fp mov = {}; ///< Movement delta [-1; 1]
		vec2fp tar_pos = {}; ///< Target position (world)
	};
	
	float gpad_aim_dist = 20; ///< Maximum target distance with gamepad
	
	
	
	PlayerController(); ///< Inits all binds with defaults
	
	void on_event(const SDL_Event& ev);
	void update(); ///< Must be called after getting all events
	void force_state(State st); ///< Forcefully sets state
	
	const State& get_state() const {return state;} ///< Last updated state
	
	[[nodiscard]] auto lock() {return std::unique_lock(mutex);} ///< Not used internally
	void set_switch(Action act, bool value);
	
	std::array<Bind, ACTION_TOTAL_COUNT_INTERNAL>& binds_ref() {return binds;}
	
	void set_gpad(std::unique_ptr<Gamepad> gpad) {this->gpad = std::move(gpad);}
	Gamepad* get_gpad() {return gpad.get();}
	
	std::string get_hint(Action act);
	
private:
	std::array<Bind, ACTION_TOTAL_COUNT_INTERNAL> binds;
	std::unique_ptr<Gamepad> gpad;
	
	State state;
	std::mutex mutex;
};

#endif // PLR_CONTROL_HPP
