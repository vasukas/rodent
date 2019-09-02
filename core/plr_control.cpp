#include "render/camera.hpp"
#include "render/control.hpp"
#include "vaslib/vas_log.hpp"
#include "plr_control.hpp"



PlayerController::Bind::Bind()
    : ims({ &key, &alt, &mou, &but })
{}
PlayerController::InputMethod::Name PlayerController::IM_Key::get_name(SDL_Scancode v)
{
	return {v == SDL_SCANCODE_UNKNOWN ? "---" : SDL_GetScancodeName(v)};
}
PlayerController::InputMethod::Name PlayerController::IM_Mouse::get_name(int v)
{
	switch (v)
	{
	case MOUSE_NONE: return {"---"};
	
	case SDL_BUTTON_LEFT:   return {"Left"};
	case SDL_BUTTON_RIGHT:  return {"Right"};
	case SDL_BUTTON_MIDDLE: return {"Middle"};
//	case SDL_BUTTON_LEFT:   name = {"mou_left",  true};
//	case SDL_BUTTON_RIGHT:  name = {"mou_right", true};
//	case SDL_BUTTON_MIDDLE: name = {"mou_mid",   true};
	case SDL_BUTTON_X1:     return {"X1"};
	case SDL_BUTTON_X2:     return {"X2"};
		
	case MOUSE_WHEELDOWN:   return {"Wheel down"};
	case MOUSE_WHEELUP:     return {"Wheel up"};
//	case MOUSE_WHEELDOWN:   return {"mou_down", true};
//	case MOUSE_WHEELUP:     return {"mou_up",   true};
	}
	return {FMT_FORMAT("Unk #{}", v)};
}
PlayerController::InputMethod::Name PlayerController::IM_Gpad::get_name(Gamepad::Button v)
{
	switch (v)
	{
	case Gamepad::B_NONE:
	case Gamepad::TOTAL_BUTTONS_INTERNAL:
		return {"---"};
		
	case Gamepad::B_RC_UP:    return {"RC up"};
	case Gamepad::B_RC_RIGHT: return {"RC right"};
	case Gamepad::B_RC_DOWN:  return {"RC down"};
	case Gamepad::B_RC_LEFT:  return {"RC left"};
		
	case Gamepad::B_UP:    return {"D up"};
	case Gamepad::B_RIGHT: return {"D right"};
	case Gamepad::B_DOWN:  return {"D down"};
	case Gamepad::B_LEFT:  return {"D left"};
		
	case Gamepad::B_SHLD_LEFT:  return {"Left bump"};
	case Gamepad::B_SHLD_RIGHT: return {"Right bump"};
		
	case Gamepad::B_TRIG_LEFT:  return {"Left trigger"};
	case Gamepad::B_TRIG_RIGHT: return {"Right trigger"};
		
	case Gamepad::B_BACK:  return {"Back"};
	case Gamepad::B_START: return {"Start"};
		
//	case Gamepad::B_RC_UP:    return {"gpad_rc_up",    true};
//	case Gamepad::B_RC_RIGHT: return {"gpad_rc_right", true};
//	case Gamepad::B_RC_DOWN:  return {"gpad_rc_down",  true};
//	case Gamepad::B_RC_LEFT:  return {"gpad_rc_left",  true};
		
//	case Gamepad::B_UP:    return {"gpad_up",    true};
//	case Gamepad::B_RIGHT: return {"gpad_right", true};
//	case Gamepad::B_DOWN:  return {"gpad_down",  true};
//	case Gamepad::B_LEFT:  return {"gpad_left",  true};
		
//	case Gamepad::B_SHLD_LEFT:  return {"gpad_shld_left",  true};
//	case Gamepad::B_SHLD_RIGHT: return {"gpad_shld_right", true};
		
//	case Gamepad::B_TRIG_LEFT:  return {"gpad_trig_left",  true};
//	case Gamepad::B_TRIG_RIGHT: return {"gpad_trig_right", true};
	}
	return {"Unknown"};
}
void PlayerController::IM_Key::operator=(SDL_Scancode v)
{
	this->v = v;
	name = get_name(v);
}
void PlayerController::IM_Mouse::operator=(int v)
{
	this->v = v;
	name = get_name(v);
}
void PlayerController::IM_Gpad::operator=(Gamepad::Button v)
{
	this->v = v;
	name = get_name(v);
}



PlayerController::PlayerController(std::unique_ptr<Gamepad> gpad):
    gpad(std::move(gpad))
{
	{
		Bind& b = binds[A_ACCEL];
		b.name = "Acceleration";
		b.descr = "Enables faster movement when held";
		b.type = BT_HELD;
		b.key = SDL_SCANCODE_SPACE;
		b.but = Gamepad::B_SHLD_RIGHT;
	}{
		Bind& b = binds[A_SHOOT];
		b.name = "Shoot";
		b.type = BT_HELD;
		b.mou = SDL_BUTTON_LEFT;
		b.but = Gamepad::B_SHLD_LEFT;
	}{
		Bind& b = binds[A_CAM_FOLLOW];
		b.name = "Camera follow";
		b.descr = "Enables camera tracking when held";
		b.type = BT_HELD;
		b.key = SDL_SCANCODE_LCTRL;
	}{
		Bind& b = binds[A_LASER_DESIG];
		b.name = "Laser";
		b.descr = "Toggles laser designator";
		b.type = BT_ONESHOT;
		b.key = SDL_SCANCODE_R;
		b.but = Gamepad::B_RC_RIGHT;
	}{
		Bind& b = binds[A_WPN_PREV];
		b.name = "Previous weapon";
		b.type = BT_ONESHOT;
		b.key = SDL_SCANCODE_LEFTBRACKET;
		b.mou = MOUSE_WHEELDOWN;
		b.but = Gamepad::B_LEFT;
	}{
		Bind& b = binds[A_WPN_NEXT];
		b.name = "Next weapon";
		b.type = BT_ONESHOT;
		b.key = SDL_SCANCODE_RIGHTBRACKET;
		b.mou = MOUSE_WHEELUP;
		b.but = Gamepad::B_RIGHT;
	}
	
	for (int i = A_WPN_1, cou = 0; i <= A_WPN_4; ++i, ++cou)
	{
		Bind& b = binds[i];
		b.name = FMT_FORMAT("Weapon {}", cou + 1);
		b.type = BT_ONESHOT;
		b.key = static_cast<SDL_Scancode>(SDL_SCANCODE_1 + cou);
	}
	
	{
		Bind& b = binds[AX_MOV_Y_NEG];
		b.name = "Move up";
		b.type = BT_HELD;
		b.key = SDL_SCANCODE_W;
	}{
		Bind& b = binds[AX_MOV_X_NEG];
		b.name = "Move left";
		b.type = BT_HELD;
		b.key = SDL_SCANCODE_A;
	}{
		Bind& b = binds[AX_MOV_Y_POS];
		b.name = "Move down";
		b.type = BT_HELD;
		b.key = SDL_SCANCODE_S;
	}{
		Bind& b = binds[AX_MOV_X_POS];
		b.name = "Move right";
		b.type = BT_HELD;
		b.key = SDL_SCANCODE_D;
	}
}
void PlayerController::on_event(const SDL_Event& ev)
{
	auto check = [](auto& v, auto& e, bool on)
	{
		if (v.v == e)
		{
			if (on) v.state = K_JUST;
			else if (v.state == K_JUST) v.state = K_ONCE;
			else v.state = K_OFF;
			return true;
		}
		return false;
	};
	
	if		(ev.type == SDL_KEYDOWN)
	{
		auto& ks = ev.key.keysym;
		if (ev.key.repeat) return;
		if (ks.scancode == SDL_SCANCODE_UNKNOWN) return;
		
		for (auto& b : binds)
		{
			if (check(b.key, ks.scancode, true)) break;
			if (check(b.alt, ks.scancode, true)) break;
		}
	}
	else if (ev.type == SDL_KEYUP)
	{
		auto& ks = ev.key.keysym;
		if (ks.scancode == SDL_SCANCODE_UNKNOWN) return;
		
		for (auto& b : binds)
		{
			if (check(b.key, ks.scancode, false)) break;
			if (check(b.alt, ks.scancode, false)) break;
		}
	}
	else if (ev.type == SDL_MOUSEBUTTONDOWN)
	{
		for (auto& b : binds)
			if (check(b.mou, ev.button.button, true)) break;
	}
	else if (ev.type == SDL_MOUSEBUTTONUP)
	{
		for (auto& b : binds)
			if (check(b.mou, ev.button.button, false)) break;
	}
	else if (ev.type == SDL_MOUSEWHEEL)
	{
		int v, y = ev.wheel.y;
		if		(y > 0) v = MOUSE_WHEELUP;
		else if (y < 0) v = MOUSE_WHEELDOWN;
		else return;
		
		for (auto& b : binds)
			if (b.mou.v == v) {
				b.mou.state = K_ONCE;
				break;
			}
	}
}
void PlayerController::update()
{
	auto& p_mov = state.mov;
	auto& p_tar = state.tar_pos;
	
	if (gpad)
	{
		auto upd_st = [this](auto& b)
		{
			if (gpad->get_state(b.v))
				b.state = (b.state == K_OFF) ? K_JUST : K_HELD;
			else b.state = K_OFF;
		};
		for (auto& b : binds)
			upd_st(b.but);
		
		p_mov = gpad->get_left();
		p_tar = gpad->get_right() * gpad_aim_dist;
	}
	else
	{
		p_mov = {};
		if (is_enabled(AX_MOV_Y_NEG)) --p_mov.y;
		if (is_enabled(AX_MOV_X_NEG)) --p_mov.x;
		if (is_enabled(AX_MOV_Y_POS)) ++p_mov.y;
		if (is_enabled(AX_MOV_X_POS)) ++p_mov.x;
		
		int mx, my;
		SDL_GetMouseState(&mx, &my);
		p_tar = RenderControl::get().get_world_camera()->mouse_cast({mx, my});
	}
	
	state.acts.clear();
	for (size_t i=0; i<ACTION_TOTAL_COUNT_INTERNAL; ++i)
	{
		state.is[i] = is_enabled(i);
		
		if (state.is[i] && binds[i].type == BT_ONESHOT)
			state.acts.push_back( static_cast<Action>(i) );
	}
	
	for (auto& b : binds)
	{
		for (auto& i : b.ims)
		{
			auto& s = i->state;
			if		(s == K_ONCE) s = K_OFF;
			else if (s == K_JUST) s = K_HELD;
		}
	}
}
bool PlayerController::is_enabled(size_t i) const
{
	auto& b = binds[i];
	switch (b.type)
	{
	case BT_ONESHOT:
		for (auto& i : b.ims) {
			if (i->state == K_JUST || i->state == K_ONCE)
				return true;
		}
		return false;
		
	case BT_HELD:
		for (auto& i : b.ims) {
			if (i->state != K_OFF)
				return true;
		}
		return false;
	}
	return false;
}
