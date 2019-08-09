#include "core/plr_control.hpp"
#include "render/ren_aal.hpp"
#include "render/ren_imm.hpp"
#include "damage.hpp"
#include "game_core.hpp"
#include "movement.hpp"
#include "physics.hpp"
#include "player.hpp"
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
	wpn->name = "Minigun";
	wpn->disp = deg_to_rad(10);
	
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
	wpn->name = "Plasma";
	
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
	wpn->name = "Rocket";
}



struct PC_Impl : PlayerLogic
{
	EVS_SUBSCR;
	std::shared_ptr<PlayerControl> ctr;
	bool shooting = false;
	
	const float push_angle = M_PI/3;
	
	
	
	PC_Impl(Entity* ent, vec2fp pos, std::shared_ptr<PlayerControl> ctr);
	void draw_hud();
	void draw_ui();
	
	void step();
	void on_cnt(const ContactEvent& ce);
};
PC_Impl::PC_Impl(Entity* e, vec2fp pos, std::shared_ptr<PlayerControl> ctr)
    : ctr(std::move(ctr))
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
void PC_Impl::draw_ui()
{
	std::vector<std::pair<std::string, FColor>> ss;
	auto wpn = ent->getref<EC_Equipment>().wpn_ptr();
	
	ss.emplace_back();
	ss.back().first = "Equipped: ";
	ss.back().second = FColor(1, 1, 1);
	
	ss.emplace_back();
	ss.back().first = wpn? wpn->name : "Debug push";
	if (!wpn) ss.back().second = FColor(0.2, 0.5, 1);
	else if (wpn->pars.type == Projectile::T_AOE) ss.back().second = FColor(1, 0.5, 0.2);
	else ss.back().second = FColor(0.2, 1, 0.2);
	ss.back().first += '\n';
	
	ss.emplace_back();
	if (!wpn) {
		ss.back().first = "Hold FIRE and touch object to push it away";
		ss.back().second = FColor(0.2, 0.5, 1);
	}
	else if (wpn->heat_flag) {
		ss.back().first = "OVERHEATED";
		ss.back().second = FColor(1, 1, 0.2);
	}
	else if (wpn->del_cou.is_positive() && wpn->shoot_delay.seconds() > 0.45) {
		ss.back().first = "Reloading";
		ss.back().second = FColor(1, 1, 0);
	}
	else if (wpn->heat_cou > wpn->heat_on * 0.7) {
		ss.back().first = "Overheat";
		ss.back().second = FColor(0.4, 1, 0);
	}
	else {
		ss.back().first = "OK";
		ss.back().second = FColor(0.2, 1, 0.2);
	}
	
	RenImm::get().draw_text({}, ss);
}
void PC_Impl::draw_hud()
{
	if (auto mp = ctr->get_tarp(); mp && ctr->is_aiming())
	{
		vec2fp p0 = ent->get_pos().pos;
		if (ent->getref<EC_Equipment>().wpn_ptr())
		{
			if (!ctr->is_tar_rel()) *mp -= p0;
			mp->norm();
	
			if (auto r = GameCore::get().get_phy().raycast_nearest(conv(p0), conv(p0 + 1000.f * *mp)))
				mp = conv(r->poi);
			else
				mp = p0 + *mp * 1.5f;
		}
		else if (ctr->is_tar_rel()) *mp += p0;
		
		p0 += vec2fp(ent->get_radius(), 0).get_rotated((*mp - p0).angle());
		RenAAL::get().draw_line(p0, *mp, FColor(1, 0, 0, 1).to_px(), 0.07, 1.5f);
	}
}
void PC_Impl::step()
{
	auto evs_lock = ctr->lock();
	
	auto& eqp = ent->getref<EC_Equipment>();
	bool accel = false;
	shooting = false;
	
	for (auto& a : ctr->update())
	{
		if (a & PlayerControl::A_WPN_FLAG)
		{
			size_t i = (a & ~PlayerControl::A_WPN_FLAG) - 1;
			eqp.set_wpn(i);
		}
		else if (a == PlayerControl::A_PREVWPN || a == PlayerControl::A_NEXTWPN)
		{
			size_t i = eqp.wpn_index();
			if		(a == PlayerControl::A_PREVWPN && i != size_t_inval) --i;
			else if (a == PlayerControl::A_NEXTWPN && i != eqp.wpns.size() - 1) ++i;
			eqp.set_wpn(i);
		}
		else if (a == PlayerControl::A_ACCEL) accel = true;
		else if (a == PlayerControl::A_SHOOT) shooting = true;
	}
	
	auto& mov = ent->getref<EC_Movement>();
	vec2fp mv = ctr->get_move();
	mv *= accel ? 14.f : 8.f;
	
	mov.dec_inert = TimeSpan::seconds(accel ? 2 : 1);
	mov.set_app_vel(mv);
	
	auto spos = ent->get_pos().pos;
	auto tar = ctr->get_tarp();
	bool pi_hack = true;
	
	if (tar) {
		if (!ctr->is_tar_rel())
			*tar -= spos;
	}
	else {
		tar = ent->get_vel().pos;
		if (tar->len2() > 0.1) tar->norm();
		else {
			tar = ent->get_norm_dir();
			pi_hack = false;
		}
		*tar *= ctr->gpad_aim_dist;
	}
	
	float ma = tar->angle() + (pi_hack? M_PI_2 : 0);
	float ca = ent->getref<EC_Physics>().body->GetAngle();
	if (std::fabs(wrap_angle(ma - ca)) > 0.1)
	{
		ma = std::remainder(ma - ca, M_PI*2) / M_PI;
		ma /= GameCore::time_mul();
		ent->getref<EC_Physics>().body->ApplyAngularImpulse(ma, true);
	}
	
	auto wpn = eqp.wpn_ptr();
	if (wpn && shooting) {
		if (!pi_hack) tar->rotate(-M_PI_2);
		wpn->shoot(ent, Transform{*tar + spos});
	}
}
void PC_Impl::on_cnt(const ContactEvent& ce)
{
	
	if (ce.type != ContactEvent::T_BEGIN ||
	    !shooting || ent->getref<EC_Equipment>().wpn_ptr()) return;
	
	auto tar = ctr->get_tarp();
	if (!tar) return;
	
	vec2fp mp = *tar;
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
Entity* PlayerLogic::create(GameCore& core, vec2fp pos, std::shared_ptr<PlayerControl> ctr)
{
	auto e = core.create_ent();
	e->add<PlayerLogic>(new PC_Impl(e, pos, std::move(ctr)));
	return e;
}
