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
		
		const std::array<InputMethod*, 4> ims;
		Bind();
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
	
	std::array<Bind, ACTION_TOTAL_COUNT_INTERNAL>& binds_ref() {return binds;}
	Gamepad* get_gpad() {return gpad.get();}
	
private:
	std::array<Bind, ACTION_TOTAL_COUNT_INTERNAL> binds;
	std::unique_ptr<Gamepad> gpad;
	
	State state;
	std::mutex mutex;
	
	bool is_enabled(size_t i) const;
};

#endif // PLR_CONTROL_HPP
