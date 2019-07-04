#include <mutex>
#include <SDL2/SDL_events.h>
#include "render/camera.hpp"
#include "render/control.hpp"
#include "render/ren_aal.hpp"
#include "render/ren_imm.hpp"
#include "damage.hpp"
#include "game_core.hpp"
#include "movement.hpp"
#include "physics.hpp"
#include "player_ctr.hpp"
#include "presenter.hpp"
#include "weapon.hpp"



static void init_wpns(EC_Equipment& e)
{
	Weapon* wpn;
	Transform tr({0, -1.5}, M_PI_2);
	
	// mgun
	
	wpn = &e.wpns.emplace_back();
	wpn->pars.dq.amount = 20.f;
	wpn->pars.imp = 5.f;
	wpn->shoot_delay = {};
	wpn->proj_spd = 18.f;
	wpn->proj_sprite = PROJ_BULLET;
	wpn->heat_off = 0.1;
	wpn->ren_id = ARM_MGUN;
	wpn->ren_tr = tr;
	
	// plasma
	
	wpn = &e.wpns.emplace_back();
	wpn->pars.dq.amount = 25.f;
	wpn->pars.imp = 30.f;
	wpn->shoot_delay.set_seconds(0.3);
	wpn->proj_spd = 12.f;
	wpn->proj_sprite = PROJ_PLASMA;
	wpn->heat_decr = 0.2;
	wpn->ren_id = ARM_PLASMA;
	wpn->ren_tr = tr;
	
	// rocket
	
	wpn = &e.wpns.emplace_back();
	wpn->pars.dq.amount = 120.f;
	wpn->pars.type = Projectile::T_AOE;
	wpn->pars.rad = 3.f;
	wpn->pars.rad_min = 0.f;
	wpn->pars.imp = 80.f;
	wpn->pars.trail = true;
	wpn->shoot_delay.set_seconds(1);
	wpn->proj_spd = 15.f;
	wpn->proj_sprite = PROJ_ROCKET;
	wpn->heat_incr = 0;
	wpn->ren_id = ARM_ROCKET;
	wpn->ren_tr = tr;
}



struct PC_Impl : PlayerControl
{
	EVS_SUBSCR;
	std::mutex evs_lock;
	
	static const int key_n = 5;
	bool keyf[key_n] = {};
	
	const float push_angle = M_PI/3;

	
	
	PC_Impl(Entity* ent, vec2fp pos);
	void on_event(const SDL_Event& ev);
	void draw_hud();
	void draw_ui();
	void step();
	void on_cnt(const ContactEvent& ce);
};
PC_Impl::PC_Impl(Entity* e, vec2fp pos)
{
	ent = e;
	ent->dbg_name = "Player";
	
	e->add(new EC_Render(e, OBJ_PC));

	b2BodyDef bd;
	bd.type = b2_dynamicBody;
	bd.position = conv(pos);
	auto phy = e->add(new EC_Physics(bd));
	
	b2FixtureDef fd;
	fd.friction = 0.3;
	fd.restitution = 0.5;
	phy->add_circle(fd, GameResBase::get().hsz_rat, 15.f);
	
	auto mov = e->add(new EC_Movement);
	mov->damp_lin = 2.f;
	
	auto wpn = e->add(new EC_Equipment);
	init_wpns(*wpn);
	wpn->set_wpn(0);
	
	reg(ECompType::StepLogic);
	EVS_CONNECT1(phy->ev_contact, on_cnt);
}
void PC_Impl::on_event(const SDL_Event& ev)
{
	if (ev.type != SDL_KEYDOWN && ev.type != SDL_KEYUP) return;
	std::unique_lock lock(evs_lock);
	
	const SDL_Scancode cs[key_n] = {
		SDL_SCANCODE_A, SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_SPACE
	};
	for (int i = 0; i < key_n; ++i)
		if (cs[i] == ev.key.keysym.scancode)
			keyf[i] = (ev.type == SDL_KEYDOWN);
	
	if (ev.type == SDL_KEYUP)
	{
		int c = ev.key.keysym.scancode;
		if		(c == SDL_SCANCODE_1) ent->getref<EC_Equipment>().set_wpn(size_t_inval);
		else if (c == SDL_SCANCODE_2) ent->getref<EC_Equipment>().set_wpn(0);
		else if (c == SDL_SCANCODE_3) ent->getref<EC_Equipment>().set_wpn(1);
		else if (c == SDL_SCANCODE_4) ent->getref<EC_Equipment>().set_wpn(2);
	}
}
void PC_Impl::draw_ui()
{
	if (auto wpn = ent->getref<EC_Equipment>().wpn_ptr())
		RenImm::get().draw_text({}, std::to_string(wpn->get_heat()), -1);
}
void PC_Impl::draw_hud()
{
	auto wpn = ent->getref<EC_Equipment>().wpn_ptr();
	
	int mx, my;
	if (SDL_GetMouseState(&mx, &my) & SDL_BUTTON_RMASK)
	{
		vec2fp p0 = ent->get_pos().pos;
		
		vec2fp mp = RenderControl::get().get_world_camera()->mouse_cast({mx, my});
		if (wpn)
		{
			mp -= p0;
			mp.norm();
	
			if (auto r = GameCore::get().get_phy().raycast_nearest(conv(p0), conv(p0 + 1000.f * mp)))
				mp = conv(r->poi);
			else
				mp = p0 + mp * 1.5f;
		}
		
		p0 += vec2fp(ent->get_radius(), 0).get_rotated((mp - p0).angle());
		RenAAL::get().draw_line(p0, mp, FColor(1, 0, 0, 1).to_px(), 0.07, 1.5f);
	}
	
	uint32_t wpn_clr;
	if (!wpn) wpn_clr = 0x2080ffff;
	else if (wpn->can_shoot()) wpn_clr = 0x20ff20ff;
	else wpn_clr = 0xffff00ff;
	RenImm::get().draw_radius(ent->get_pos().pos, ent->get_radius() + 0.5, wpn_clr, 0.15);
}
void PC_Impl::step()
{
	const vec2fp kmv[4] = {{-1, 0}, {0, -1}, {0, 1}, {1, 0}};
	vec2fp mv = {};
	for (int i=0; i<4; ++i) if (keyf[i]) mv += kmv[i];
	if (!mv.is_zero(0.1)) mv.norm();
	
	auto& mov = ent->getref<EC_Movement>();
	mv *= keyf[4] ? 14.f : 8.f;
	mov.dec_inert = TimeSpan::seconds(keyf[4] ? 2 : 1);
	mov.set_app_vel(mv);
	
	int mx, my;
	int mou_st = SDL_GetMouseState(&mx, &my);
	vec2fp mp = RenderControl::get().get_world_camera()->mouse_cast({mx, my});
	vec2fp mp_or = mp;
	
	mp -= ent->get_pos().pos;
	float ma = mp.angle() + M_PI_2;
	float ca = ent->getref<EC_Physics>().body->GetAngle();
	if (std::fabs(wrap_angle(ma - ca)) > 0.1)
	{
		ma = std::remainder(ma - ca, M_PI*2) / M_PI;
		ma /= GameCore::time_mul();
		ent->getref<EC_Physics>().body->ApplyAngularImpulse(ma, true);
	}
	
	auto wpn = ent->getref<EC_Equipment>().wpn_ptr();
	if (wpn && (mou_st & SDL_BUTTON_LMASK))
		wpn->shoot(ent, {mp_or});
}
void PC_Impl::on_cnt(const ContactEvent& ce)
{
	int mx, my;
	if (ce.type != ContactEvent::T_BEGIN ||
	    !(SDL_GetMouseState(&mx, &my) & SDL_BUTTON_LMASK) ||
	    ent->getref<EC_Equipment>().wpn_ptr()) return;
	
	vec2fp mp = RenderControl::get().get_world_camera()->mouse_cast({mx, my});
	mp -= ent->get_pos().pos;
	mp.norm();
	
	float da = (ce.other->get_pos().pos - ent->get_pos().pos).angle();
	if (wrap_angle(da - mp.angle()) > push_angle) return;
	
	auto b = ce.other->getref<EC_Physics>().body;
	float k = 120.f;
	b->ApplyLinearImpulseToCenter(k * conv(mp), true);
	
	Transform at;
	at.rot = mp.angle();
	at.pos = ent->get_pos().pos + vec2fp(ent->get_radius(), 0).get_rotated(at.rot);
	GamePresenter::get().effect(FE_SHOOT_DUST, at);
}
Entity* PlayerControl::create(GameCore& core, vec2fp pos)
{
	auto e = core.create_ent();
	e->add<PlayerControl>(new PC_Impl(e, pos));
	return e;
}
