#include <SDL2/SDL_events.h>
#include "core/hard_paths.hpp"
#include "render/camera.hpp"
#include "render/control.hpp"
#include "utils/line_cfg.hpp"
#include "vaslib/vas_log.hpp"
#include "plr_input.hpp"

/*
=====================================================
	!!! WHEN CHANGING CONTROLS !!!

	client/replay.cpp  - increase version 
	core/main_loop.cpp - update F1 help menu
=====================================================
*/



PlayerInput::InputMethod::Name PlayerInput::IM_Key::get_name(SDL_Scancode v)
{
	return {v == SDL_SCANCODE_UNKNOWN ? "---" : SDL_GetScancodeName(v)};
}
void PlayerInput::IM_Key::operator=(SDL_Scancode v)
{
	this->v = v;
	name = get_name(v);
}
bool PlayerInput::IM_Key::set_from(const SDL_Event& ev)
{
	if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP)
	{
		auto nv = ev.key.keysym.scancode;
		if (nv != SDL_SCANCODE_UNKNOWN) {
			v = nv;
			name = get_name(v);
			return true;
		}
	}
	return false;
}
bool PlayerInput::IM_Key::is_same(const InputMethod& m)
{
	if (v == SDL_SCANCODE_UNKNOWN) return false;
	return dynamic_cast<const IM_Key&>(m).v == v;
}



PlayerInput::InputMethod::Name PlayerInput::IM_Mouse::get_name(int v)
{
	switch (v)
	{
	case MOUSE_NONE: return {"---"};
	
	case SDL_BUTTON_LEFT:   return {"Left"};
	case SDL_BUTTON_RIGHT:  return {"Right"};
	case SDL_BUTTON_MIDDLE: return {"Middle"};
	case SDL_BUTTON_X1:     return {"X1"};
	case SDL_BUTTON_X2:     return {"X2"};
		
	case MOUSE_WHEELDOWN:   return {"Wheel down"};
	case MOUSE_WHEELUP:     return {"Wheel up"};
	}
	return {FMT_FORMAT("Button #{}", v)};
}
void PlayerInput::IM_Mouse::operator=(int v)
{
	this->v = v;
	name = get_name(v);
}
bool PlayerInput::IM_Mouse::set_from(const SDL_Event& ev)
{
	if (int v = get_value(ev)) {
		this->v = v;
		name = get_name(v);
		return true;
	}
	return false;
}
bool PlayerInput::IM_Mouse::is_same(const InputMethod& m)
{
	if (!v) return false;
	return dynamic_cast<const IM_Mouse&>(m).v == v;
}
int PlayerInput::IM_Mouse::get_value(const SDL_Event& ev)
{
	if (ev.type == SDL_MOUSEBUTTONDOWN ||
	    ev.type == SDL_MOUSEBUTTONUP)
	{
		return ev.button.button;
	}
	else if (ev.type == SDL_MOUSEWHEEL)
	{
		if		(ev.wheel.y > 0) return MOUSE_WHEELUP;
		else if (ev.wheel.y < 0) return MOUSE_WHEELDOWN;
	}
	return 0;
}



const char* PlayerInput::get_sys_name(Action v)
{
	switch (v)
	{
	case A_ACCEL: return "accel";
	case A_SHOOT: return "fire";
	case A_SHOOT_ALT: return "fire_alt";
	case A_SHIELD_SW: return "shield";
	case A_INTERACT: return "interact";
	
	case A_CAM_FOLLOW: return "cam_follow";
	case A_CAM_CLOSE_SW: return "cam_close";
	
	case A_LASER_DESIG: return "desig";
	case A_SHOW_MAP: return "map";
	case A_HIGHLIGHT: return "highlight";
	
	case A_WPN_PREV: return "wpn_prev";
	case A_WPN_NEXT: return "wpn_next";
	case A_WPN_1: return "wpn1";
	case A_WPN_2: return "wpn2";
	case A_WPN_3: return "wpn3";
	case A_WPN_4: return "wpn4";
	case A_WPN_5: return "wpn5";
	case A_WPN_6: return "wpn6";
	
	case A_MENU_HELP:   return "help";
	case A_MENU_SELECT: return "select";
	case A_MENU_EXIT:   return "exit";
		
	case AX_MOV_Y_NEG: return "y_neg";
	case AX_MOV_X_NEG: return "x_neg";
	case AX_MOV_Y_POS: return "y_pos";
	case AX_MOV_X_POS: return "x_pos";
		
	case A_IGNORED:
	case ACTION_TOTAL_COUNT_INTERNAL:
		break;
	}
	return "err_";
}
const char* PlayerInput::get_sys_name(ContextMode v)
{
	switch (v)
	{
	case CTX_MENU: return "cm_";
	case CTX_GAME: return "g_";
		
	case CTX_TOTAL_COUNT_INTERNAL:
		break;
	}
	return "err_";
}
std::vector<LineCfgOption> PlayerInput::gen_cfg_opts()
{
	std::vector<LineCfgOption> opts;
	opts.reserve( CTX_TOTAL_COUNT_INTERNAL * ACTION_TOTAL_COUNT_INTERNAL );
	
	auto e_type = LineCfgEnumType::make<BindType>({ {BT_TRIGGER, "trigger"}, {BT_HELD, "held"}, {BT_SWITCH, "switch"} });
	auto e_key  = LineCfgEnumType::make<SDL_Scancode>(
		[](std::string_view s) -> std::optional<SDL_Scancode> {
			auto v = SDL_GetScancodeFromName(std::string(s).c_str());
			if (v == SDL_SCANCODE_UNKNOWN) return {};
			return v;},
		[](SDL_Scancode k) -> std::string_view { return SDL_GetScancodeName(k); });
	
	for (size_t i=0; i<ctxs.size(); ++i)
	{
		auto& cc = ctxs[i];
		const char* prefix = get_sys_name(static_cast<ContextMode>(i));
		
		for (auto& b : cc.binds)
		{
			std::string name = std::string(prefix) + get_sys_name(b.action);
			
			if (b.im_key.v != SDL_SCANCODE_UNKNOWN)
				opts.emplace_back(FMT_FORMAT("{}_key", name)).venum( b.im_key.v, e_key );
				
			if (b.im_alt.v != SDL_SCANCODE_UNKNOWN)
				opts.emplace_back(FMT_FORMAT("{}_alt", name)).venum( b.im_alt.v, e_key );
			
			if (b.im_mou.v)
				opts.emplace_back(FMT_FORMAT("{}_mou", name)).vint( b.im_mou.v );
			
			if (b.type != BT_TRIGGER)
				opts.emplace_back(FMT_FORMAT("{}_type", name)).venum( b.type, e_type );
		}
	}
	
	return opts;
}



PlayerInput& PlayerInput::get() {
	static PlayerInput p;
	return p;
}
PlayerInput::PlayerInput()
{
	set_defaults();
	
	// load settings
	
	if (LineCfg(gen_cfg_opts()).read(HARDPATH_KEYBINDS)) {
		VLOGI("User keybinds loaded");
		
		for (auto& c : ctxs)
		for (auto& b : c.binds)
		for (auto& i : b.ims())
			i->upd_name();
	}
	else
		VLOGW("Using default keybinds");
	
	after_load();
}
void PlayerInput::set_defaults()
{
	// MENU
	{
		auto& binds = ctxs[CTX_MENU].binds;
		{
			auto& b = binds.emplace_back();
			b.action = A_MENU_HELP;
			b.name = "Menu: help";
			b.im_key = SDL_SCANCODE_F1;
		}{
			auto& b = binds.emplace_back();
			b.action = A_MENU_SELECT;
			b.name = "Menu: select";
			b.im_mou = SDL_BUTTON_LEFT;
		}{
			auto& b = binds.emplace_back();
			b.action = A_MENU_EXIT;
			b.name = "Menu: exit";
			b.im_key = SDL_SCANCODE_ESCAPE;
		}{
			auto& b = binds.emplace_back();
			b.action = A_INTERACT;
			b.name = "Menu: exit (use)";
		}
	}
	
	// GAME
	{
		auto& binds = ctxs[CTX_GAME].binds;
		{
			auto& b = binds.emplace_back();
			b.action = A_MENU_HELP;
			b.name = "Menu: help";
			b.im_key = SDL_SCANCODE_F1;
			b.hidden = true;
			b.replay_ignore = true;
		}{
			auto& b = binds.emplace_back();
			b.action = A_MENU_EXIT;
			b.name = "Menu: exit";
			b.im_key = SDL_SCANCODE_ESCAPE;
			b.hidden = true;
			b.replay_ignore = true;
		}
		
		{
			auto& b = binds.emplace_back();
			b.action = A_ACCEL;
			b.name = "Acceleration";
			b.descr = "Enables faster movement when held";
			b.type = BT_HELD;
			b.im_key = SDL_SCANCODE_SPACE;
		}{
			auto& b = binds.emplace_back();
			b.action = A_SHOOT;
			b.name = "Fire";
			b.type = BT_HELD;
			b.im_mou = SDL_BUTTON_LEFT;
		}{
			auto& b = binds.emplace_back();
			b.action = A_SHOOT_ALT;
			b.name = "Alt. fire";
			b.type = BT_HELD;
			b.im_mou = SDL_BUTTON_RIGHT;
		}{
			auto& b = binds.emplace_back();
			b.action = A_SHIELD_SW;
			b.name = "Shield";
			b.type = BT_SWITCH;
			b.im_mou = SDL_BUTTON_X1;
			b.im_key = SDL_SCANCODE_G;
		}{
			auto& b = binds.emplace_back();
			b.action = A_INTERACT;
			b.name = "Interact";
			b.descr = "Use interactive object when prompt appears";
			b.type = BT_TRIGGER;
			b.im_key = SDL_SCANCODE_E;
		}{
			auto& b = binds.emplace_back();
			b.action = A_CAM_FOLLOW;
			b.name = "Camera track";
			b.descr = "Enables camera tracking when held";
			b.type = BT_HELD;
			b.im_key = SDL_SCANCODE_LCTRL;
		}{
			auto& b = binds.emplace_back();
			b.action = A_CAM_CLOSE_SW;
			b.name = "Camera distance";
			b.descr = "Switches between close and far";
			b.type = BT_SWITCH;
			b.im_key = SDL_SCANCODE_Z;
		}{
			auto& b = binds.emplace_back();
			b.action = A_LASER_DESIG;
			b.name = "Laser";
			b.descr = "Toggles laser designator";
			b.type = BT_SWITCH;
			b.im_key = SDL_SCANCODE_R;
		}{
			auto& b = binds.emplace_back();
			b.action = A_SHOW_MAP;
			b.name = "Show map";
			b.descr = "Shows level map";
			b.type = BT_SWITCH;
			b.im_key = SDL_SCANCODE_M;
			b.replay_ignore = true;
		}{
			auto& b = binds.emplace_back();
			b.action = A_HIGHLIGHT;
			b.name = "Highlight";
			b.descr = "Highlights objects and show stats";
			b.type = BT_HELD;
			b.im_key = SDL_SCANCODE_TAB;
		}{
			auto& b = binds.emplace_back();
			b.action = A_WPN_PREV;
			b.name = "Previous weapon";
			b.type = BT_TRIGGER;
			b.im_key = SDL_SCANCODE_LEFTBRACKET;
			b.im_mou = MOUSE_WHEELUP;
		}{
			auto& b = binds.emplace_back();
			b.action = A_WPN_NEXT;
			b.name = "Next weapon";
			b.type = BT_TRIGGER;
			b.im_key = SDL_SCANCODE_RIGHTBRACKET;
			b.im_mou = MOUSE_WHEELDOWN;
		}
		
		for (int i = A_WPN_1, cou = 0; i <= A_WPN_6; ++i, ++cou)
		{
			auto& b = binds.emplace_back();
			b.action = static_cast<Action>(i);
			b.name = FMT_FORMAT("Weapon {}", cou + 1);
			b.type = BT_TRIGGER;
			b.im_key = static_cast<SDL_Scancode>(SDL_SCANCODE_1 + cou);
		}
		
		{
			auto& b = binds.emplace_back();
			b.action = AX_MOV_Y_NEG;
			b.name = "Move up";
			b.type = BT_HELD;
			b.im_key = SDL_SCANCODE_W;
		}{
			auto& b = binds.emplace_back();
			b.action = AX_MOV_X_NEG;
			b.name = "Move left";
			b.type = BT_HELD;
			b.im_key = SDL_SCANCODE_A;
		}{
			auto& b = binds.emplace_back();
			b.action = AX_MOV_Y_POS;
			b.name = "Move down";
			b.type = BT_HELD;
			b.im_key = SDL_SCANCODE_S;
		}{
			auto& b = binds.emplace_back();
			b.action = AX_MOV_X_POS;
			b.name = "Move right";
			b.type = BT_HELD;
			b.im_key = SDL_SCANCODE_D;
		}
	}
}
void PlayerInput::after_load()
{
	auto copy = [this](auto c1, auto a1, auto c2, auto a2){
		Bind* b1 = ctxs[c1].get(a1);
		Bind* b2 = ctxs[c2].get(a2);
		if (!b1 || !b2) return;
		for (size_t i=0; i<Bind::ims_num; ++i)
			(b1->ims()[i])->set_from(*(b2->ims()[i]));
	};
	copy(CTX_MENU, A_INTERACT, CTX_GAME, A_INTERACT);
}
void PlayerInput::on_event(const SDL_Event& ev)
{
	auto check = [](auto& im, auto& val, bool on)
	{
		if (im.v == val)
		{
			if (on) {if (im.state != K_HOLD) im.state = K_JUST;}
			else if (im.state == K_JUST) im.state = K_ONCE;
			else if (im.state != K_ONCE) im.state = K_OFF;
			return true;
		}
		return false;
	};
	auto& binds = ctxs[cur_ctx].binds;
	
	if		(ev.type == SDL_KEYDOWN)
	{
		auto& ks = ev.key.keysym;
		if (ev.key.repeat) return;
		if (ks.scancode == SDL_SCANCODE_UNKNOWN) return;
		
		for (auto& b : binds)
		{
			if (check(b.im_key, ks.scancode, true)) break;
			if (check(b.im_alt, ks.scancode, true)) break;
		}
	}
	else if (ev.type == SDL_KEYUP)
	{
		auto& ks = ev.key.keysym;
		if (ks.scancode == SDL_SCANCODE_UNKNOWN) return;
		
		for (auto& b : binds)
		{
			if (check(b.im_key, ks.scancode, false)) break;
			if (check(b.im_alt, ks.scancode, false)) break;
		}
	}
	else if (ev.type == SDL_MOUSEBUTTONDOWN)
	{
		for (auto& b : binds)
			if (check(b.im_mou, ev.button.button, true)) break;
	}
	else if (ev.type == SDL_MOUSEBUTTONUP)
	{
		for (auto& b : binds)
			if (check(b.im_mou, ev.button.button, false)) break;
	}
	else if (ev.type == SDL_MOUSEWHEEL)
	{
		int v = IM_Mouse::get_value(ev);
		if (!v) return;
		
		for (auto& b : binds)
			if (b.im_mou.v == v) {
				b.im_mou.state = K_ONCE;
				break;
			}
	}
}
void PlayerInput::update(ContextMode m) {
	ctxs[m].update(m == cur_ctx);
}
const PlayerInput::State& PlayerInput::get_state(ContextMode m) const {
	return ctxs[m].state;
}
void PlayerInput::set_switch(ContextMode m, Action act, bool value)
{
	if (auto b = ctxs[m].get(act)) {
		b->sw_val = value;
	}
	else {
		VLOGW("PlayerInput::set_switch() bind doesn't exist");
		debugbreak();
	}
}
std::string PlayerInput::get_hint(Action act)
{
	if (auto b = ctxs[cur_ctx].get(act)) {
		auto k = b->im_key.v;
		if (k == SDL_SCANCODE_UNKNOWN) k = b->im_alt.v;
		if (k != SDL_SCANCODE_UNKNOWN)
			return SDL_GetScancodeName(k);
	}
	return "---";
}
void PlayerInput::set_context(ContextMode m) {
	cur_ctx = m;
}
PlayerInput::ContextMode PlayerInput::get_context() const {
	return cur_ctx;
}
void PlayerInput::replay_fix(ContextMode m, State& state) const
{
	auto& ctx = const_cast<Context&>(ctxs[m]);
	for (int i=0; i<ACTION_TOTAL_COUNT_INTERNAL; ++i) {
		if (state.is.test(i) && ctx.get(static_cast<Action>(i))->replay_ignore)
			state.is.reset(i);
	}
	for (auto it = state.acts.begin(); it != state.acts.end(); ) {
		if (ctx.get(*it)->replay_ignore) it = state.acts.erase(it);
		else ++it;
	}
}
void PlayerInput::replay_set(ContextMode m, State st)
{
	auto& ctx = ctxs[m];
	std::vector<int> ri_is, ri_acts;
	for (int i=0; i<ACTION_TOTAL_COUNT_INTERNAL; ++i) {
		if (ctx.state.is.test(i) && ctx.get(static_cast<Action>(i))->replay_ignore)
			ri_is.push_back(i);
	}
	for (auto& i : ctx.state.acts) {
		if (ctx.get(i)->replay_ignore) 
			ri_acts.push_back(i);
	}
	
	ctxs[m].state = std::move(st);
	
	for (auto& i : ri_is) ctxs[m].state.is.set(i);
	for (auto& i : ri_acts) ctxs[m].state.acts.push_back(static_cast<Action>(i));
}
PlayerInput::Bind* PlayerInput::Context::get(Action act)
{
	for (auto& b : binds) {
		if (b.action == act)
			return &b;
	}
	return nullptr;
}
void PlayerInput::Context::update(bool is_current)
{
	// update bind state & add actions
	
	state.acts.clear();
	for (auto& b : binds)
	{
		bool st = [&]
		{
			if (!is_current)
			{
				switch (b.type)
				{
				case BT_TRIGGER:
				case BT_HELD:
					return false;
					
				case BT_SWITCH:
					return b.sw_val;
				}
			}
			
			switch (b.type)
			{
			case BT_TRIGGER:
				for (auto& i : b.ims()) {
					if (i->state == K_JUST || i->state == K_ONCE)
						return true;
				}
				return false;
				
			case BT_SWITCH:
				for (auto& i : b.ims()) {
					if (i->state == K_JUST || i->state == K_ONCE) {
						b.sw_val = !b.sw_val;
						break;
					}
				}
				return b.sw_val;
				
			case BT_HELD:
				for (auto& i : b.ims()) {
					if (i->state != K_OFF)
						return true;
				}
				return false;
			}
			return false;
		}();
		
		state.is[b.action] = st;
		if (st && b.type == BT_TRIGGER)
			state.acts.push_back(b.action);
		
		for (auto& i : b.ims())
		{
			auto& s = i->state;
			if (is_current) {
				if		(s == K_ONCE) s = K_OFF;
				else if (s == K_JUST) s = K_HOLD;
			}
			else s = K_OFF;
		};
	}
	
	// update vectors
	
	auto& p_mov = state.mov;
	p_mov = {};
	if (state.is[AX_MOV_Y_NEG]) --p_mov.y;
	if (state.is[AX_MOV_X_NEG]) --p_mov.x;
	if (state.is[AX_MOV_Y_POS]) ++p_mov.y;
	if (state.is[AX_MOV_X_POS]) ++p_mov.x;
	
	state.cursor = RenderControl::get().get_current_cursor();
	state.tar_pos = RenderControl::get().get_world_camera().mouse_cast(state.cursor);
}
