#ifndef PLR_INPUT_HPP
#define PLR_INPUT_HPP

#include <bitset>
#include <optional>
#include <memory>
#include <mutex>
#include <SDL2/SDL_scancode.h>
#include "vaslib/vas_math.hpp"

struct LineCfgOption;
union  SDL_Event;



class PlayerInput
{
public:
	enum Action : uint8_t
	{
		A_IGNORED,
		
		A_ACCEL,
		A_SHOOT,
		A_SHOOT_ALT,
		A_SHIELD_SW,
		A_INTERACT,
		
		A_CAM_FOLLOW,
		A_CAM_CLOSE_SW,
		
		A_LASER_DESIG,
		A_SHOW_MAP,
		A_HIGHLIGHT,
		
		A_WPN_PREV,
		A_WPN_NEXT,
		A_WPN_1,
		A_WPN_2,
		A_WPN_3,
		A_WPN_4,
		A_WPN_5,
		A_WPN_6,
		
		A_MENU_HELP,
		A_MENU_SELECT,
		A_MENU_EXIT,
		
		// internal
		AX_MOV_Y_NEG,
		AX_MOV_X_NEG,
		AX_MOV_Y_POS,
		AX_MOV_X_POS,
		
		/// Do not use
		ACTION_TOTAL_COUNT_INTERNAL
	};
	
	enum ContextMode
	{
		CTX_GAME,
		CTX_MENU,
		
		/// Do not use
		CTX_TOTAL_COUNT_INTERNAL
	};
	
	enum KeyState // internal
	{
		K_OFF,
		K_HOLD, ///< for some time
		K_JUST, ///< just pressed
		K_ONCE  ///< pressed & depressed at same step
	};
	
	enum BindType
	{
		BT_TRIGGER, ///< Set as enabled only on initial press
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
		virtual bool set_from(const SDL_Event& ev) = 0; ///< Returns false if can't
		virtual void set_from(const InputMethod& m) = 0; ///< Type must be same
		virtual bool is_same(const InputMethod& m) = 0; ///< Type must be same
		virtual void upd_name() = 0;
	};
	struct IM_Key : InputMethod
	{
		SDL_Scancode v = SDL_SCANCODE_UNKNOWN;
		static Name get_name(SDL_Scancode v);
		void operator=(SDL_Scancode v);
		bool set_from(const SDL_Event& ev);
		void set_from(const InputMethod& m) {*this = dynamic_cast<const std::remove_reference_t<decltype(*this)>&>(m);}
		bool is_same(const InputMethod& m);
		void upd_name() {name = get_name(v);}
	};
	struct IM_Mouse : InputMethod
	{
		int v = MOUSE_NONE; // SDL_BUTTON_* or MOUSE_*
		static Name get_name(int v);
		void operator=(int v);
		bool set_from(const SDL_Event& ev);
		void set_from(const InputMethod& m) {*this = dynamic_cast<const std::remove_reference_t<decltype(*this)>&>(m);}
		bool is_same(const InputMethod& m);
		static int get_value(const SDL_Event& ev); ///< Returns same value as 'v' or 0
		void upd_name() {name = get_name(v);}
	};
	
	struct Bind
	{
		Action action = A_IGNORED;
		BindType type = BT_TRIGGER;
		std::string name, descr;
		bool hidden = false; ///< Not shown in settings
		bool replay_ignore = false; ///< Not written into replay
		
		IM_Key   im_key, im_alt;
		IM_Mouse im_mou;
		bool sw_val = false; ///< Switch value
		
		static constexpr size_t ims_num = 3;
		std::array<InputMethod*, ims_num> ims() {return{ &im_key, &im_alt, &im_mou };}
	};
	
	struct State
	{
		std::bitset<ACTION_TOTAL_COUNT_INTERNAL> is = {}; ///< Is enabled/triggered
		std::vector<Action> acts; ///< Oneshot actions triggered
		
		vec2fp mov = {}; ///< Movement delta [-1; 1]
		vec2fp tar_pos = {}; ///< Target position (world)
		vec2i cursor = {}; ///< Cursor position, pixels
	};
	
	
	
	static const char* get_sys_name(Action v);
	static const char* get_sys_name(ContextMode v);
	std::vector<LineCfgOption> gen_cfg_opts(); ///< Call after_load() after successful loading
	
	static PlayerInput empty_proxy() {return PlayerInput();}
	
	static PlayerInput& get(); ///< Returns singleton. Initializes default binds
	void set_defaults();
	void after_load();
	
	[[nodiscard]] auto lock() {return std::unique_lock(mutex);} ///< Not used internally
	void on_event(const SDL_Event& ev);
	
	void update(ContextMode m); ///< Must be called after getting all events
	const State& get_state(ContextMode m) const; ///< Last updated state
	
	void set_switch(ContextMode m, Action act, bool value); ///< In current context
	std::string get_hint(Action act); ///< In current context
	
	void set_context(ContextMode m);
	ContextMode get_context() const;
	
	std::vector<Bind>& binds_ref(ContextMode m) {
		return ctxs[m].binds;
	}
	
	void replay_fix(ContextMode m, State& state) const; ///< Fixes state for replay
	void replay_set(ContextMode m, State st); ///< Overwrites current state (except 'replay_ignore')
	
private:
	struct Context {
		std::vector<Bind> binds;
		State state;
		
		Bind* get(Action act);
		void update(bool is_current);
	};
	std::array<Context, CTX_TOTAL_COUNT_INTERNAL> ctxs;
	ContextMode cur_ctx = CTX_MENU;
	std::mutex mutex;
	
	PlayerInput();
	PlayerInput(const PlayerInput&) = delete;
};

#endif // PLR_INPUT_HPP
