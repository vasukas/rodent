#include "render/camera.hpp"
#include "render/control.hpp"
#include "vaslib/vas_log.hpp"
#include "plr_control.hpp"



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
	}{
		Bind& b = binds[A_CAM_FOLLOW];
		b.name = "Camera follow";
		b.descr = "Enables camera tracking when held";
		b.type = BT_HELD;
		b.key = SDL_SCANCODE_LCTRL;
		b.but = Gamepad::B_SHLD_LEFT;
	}{
		Bind& b = binds[A_WPN_PREV];
		b.name = "Previous weapon";
		b.type = BT_HELD;
		b.key = SDL_SCANCODE_LEFTBRACKET;
		b.mou = MOUSE_WHEELDOWN;
		b.but = Gamepad::B_LEFT;
	}{
		Bind& b = binds[A_WPN_NEXT];
		b.name = "Next weapon";
		b.type = BT_HELD;
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
	if		(ev.type == SDL_KEYDOWN)
	{
		if (ev.key.repeat) return;
		auto& ks = ev.key.keysym;
		for (auto& b : binds)
		{
			if (b.key == ks.scancode) {
				b.st_key = K_JUST;
				break;
			}
		}
	}
	else if (ev.type == SDL_KEYUP)
	{
		auto& ks = ev.key.keysym;
		for (auto& b : binds)
			if (b.key == ks.scancode) {
				if		(b.st_key == K_JUST) b.st_key = K_ONCE;
				else if (b.st_key != K_ONCE) b.st_key = K_OFF;
				break;
			}
	}
	else if (ev.type == SDL_MOUSEBUTTONDOWN)
	{
		for (auto& b : binds)
			if (b.mou == ev.button.button) {
				b.st_mou = K_JUST;
				break;
			}
	}
	else if (ev.type == SDL_MOUSEBUTTONUP)
	{
		for (auto& b : binds)
			if (b.mou == ev.button.button) {
				if		(b.st_mou == K_JUST) b.st_mou = K_ONCE;
				else if (b.st_mou != K_ONCE) b.st_mou = K_OFF;
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
		auto upd_st = [](Bind& b, bool ok)
		{
			if (ok) b.st_but = (b.st_but == K_OFF) ? K_JUST : K_HELD;
			else b.st_but = K_OFF;
		};
		for (auto& b : binds)
			upd_st(b, gpad->get_state(b.but));
		
		upd_st(binds[A_SHOOT], gpad->trig_left() > 0.1);
		
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
	
	for (size_t i=0; i<ACTION_TOTAL_COUNT_INTERNAL; ++i)
		state.is[i] = is_enabled(i);
	
	for (auto& b : binds)
	{
		if (b.st_key == K_ONCE) b.st_key = K_OFF;
		if (b.st_mou == K_ONCE) b.st_mou = K_OFF;
	}
}
std::vector<PlayerController::Action> PlayerController::get_acts() const
{
	std::vector<Action> as;
	as.reserve(16);
	
	for (size_t i=0; i<ACTION_TOTAL_COUNT_INTERNAL; ++i)
	{
		if (binds[i].type == BT_ONESHOT && state.is[i])
			as.push_back( static_cast<Action>(i) );
	}
	return as;
}
bool PlayerController::is_enabled(size_t i) const
{
	auto& b = binds[i];
	switch (b.type)
	{
	case BT_ONESHOT:
		if (b.st_key == K_JUST || b.st_key == K_ONCE) return true;
		if (b.st_mou == K_JUST || b.st_mou == K_ONCE) return true;
		if (b.st_but == K_JUST || b.st_but == K_ONCE) return true;
		return false;
		
	case BT_HELD:
		if (b.st_key) return true;
		if (b.st_mou) return true;
		if (b.st_but) return true;
		return false;
	}
	return false;
}
