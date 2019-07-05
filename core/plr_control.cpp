#include "render/camera.hpp"
#include "render/control.hpp"
#include "plr_control.hpp"

PlayerControl::PlayerControl(Gamepad* gpad): gpad(gpad)
{
	Bind* b;
	
	b = &binds.emplace_back();
	b->act = A_LEFT, b->name = "Move left";
	b->key = SDL_SCANCODE_A, b->heldable = true;
	
	b = &binds.emplace_back();
	b->act = A_RIGHT, b->name = "Move rught";
	b->key = SDL_SCANCODE_D, b->heldable = true;
	
	b = &binds.emplace_back();
	b->act = A_UP, b->name = "Move up";
	b->key = SDL_SCANCODE_W, b->heldable = true;
	
	b = &binds.emplace_back();
	b->act = A_DOWN, b->name = "Move down";
	b->key = SDL_SCANCODE_S, b->heldable = true;
	
	b = &binds.emplace_back();
	b->act = A_ACCEL, b->name = "Acceleration", b->descr = "Faster movement while held";
	b->key = SDL_SCANCODE_SPACE, b->heldable = true;
	b->but = Gamepad::B_SHLD_RIGHT;
	
	b = &binds.emplace_back();
	b->act = A_PREVWPN, b->name = "Previous weapon";
	b->key = SDL_SCANCODE_LEFTBRACKET;
	b->but = Gamepad::B_LEFT;
	
	b = &binds.emplace_back();
	b->act = A_NEXTWPN, b->name = "Next weapon";
	b->key = SDL_SCANCODE_RIGHTBRACKET;
	b->but = Gamepad::B_RIGHT;
	
	for (int i=0; i<4; ++i) {
		b = &binds.emplace_back();
		b->act = A_WPN_FLAG | i, b->name = std::string("Weapon ") + std::to_string(i+1);
		b->key = static_cast<SDL_Scancode>(SDL_SCANCODE_1 + i);
	}
}
void PlayerControl::on_event(const SDL_Event& ev)
{
	if (ev.type != SDL_KEYDOWN && ev.type != SDL_KEYUP) return;
	if (ev.type == SDL_KEYDOWN && ev.key.repeat) return;
	for (auto& b : binds)
	{
		if (b.key != ev.key.keysym.scancode) continue;
		if (b.heldable) b.value = (ev.type == SDL_KEYDOWN);
		else if (ev.type != SDL_KEYUP && b.act != A_NONE) as_list.push_back(b.act);
		break;
	}
}
std::vector<int> PlayerControl::update()
{
	kmov = {};
	for (auto& b : binds)
	{
		if (b.but && gpad)
		{
			bool st = gpad->get_state(b.but);
			if (st && (!b.value || b.heldable) && b.act != A_NONE) as_list.push_back(b.act);
			b.value = st;
		}
		else if (b.value)
		{
			if		(b.act == A_LEFT)  kmov.x -= 1;
			else if (b.act == A_RIGHT) kmov.x += 1;
			else if (b.act == A_UP)    kmov.y -= 1;
			else if (b.act == A_DOWN)  kmov.y += 1;
			else if (b.heldable && b.act != A_NONE) as_list.push_back(b.act);
		}
	}
	
	if ((gpad && gpad->trig_left() > 0.1) || SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_LMASK)
		as_list.push_back(A_SHOOT);
	
	std::vector<int> as;
	as.swap(as_list);
	return as;
}
vec2fp PlayerControl::get_move()
{
	return gpad? gpad->get_left() : kmov;
}
std::optional<vec2fp> PlayerControl::get_tarp()
{
	vec2fp p;
	if (gpad) p = gpad->get_right() * gpad_aim_dist;
	else {
		int mx, my;
		SDL_GetMouseState(&mx, &my);
		p = RenderControl::get().get_world_camera()->mouse_cast({mx, my});
	}
	if (std::fabs(p.x) < aim_dead_zone && std::fabs(p.y) < aim_dead_zone) return {};
	return p;
}
bool PlayerControl::is_aiming()
{
	return gpad? true : SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_RMASK;
}
bool PlayerControl::is_tar_rel()
{
	return gpad.operator bool();
}
