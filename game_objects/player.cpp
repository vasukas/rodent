#include "client/effects.hpp"
#include "client/plr_control.hpp"
#include "vaslib/vas_log.hpp"
#include "game/game_core.hpp"
#include "player.hpp"
#include "weapon_all.hpp"

#include "render/camera.hpp"
#include "render/control.hpp"
#include "render/ren_aal.hpp"
#include "render/ren_imm.hpp"



PlayerRender::PlayerRender(Entity* ent)
	: ECompRender(ent)
{
	parts(FE_SPAWN, {{}, GameConst::hsz_rat});
	parts(MODEL_PC_RAT, ME_AURA, {{}, 3, FColor(0.3, 0.7, 1, 0.7)});
}
void PlayerRender::on_destroy()
{
	parts(MODEL_PC_RAT, ME_DEATH, {});
}
void PlayerRender::step()
{
	const Transform fixed{get_pos().pos, angle};
	RenAAL::get().draw_inst(fixed, FColor(0.4, 0.9, 1, 1), MODEL_PC_RAT);
	
	for (auto& a : atts) {
		if (a.model != MODEL_NONE)
			RenAAL::get().draw_inst(fixed.get_combined(a.at), a.clr, a.model);
	}
	
	if (true)
	{
		float k = 1.f / RenderControl::get().get_world_camera().get_state().mag;
		RenImm::get().draw_text( get_pos().pos, dbg_info_real, RenImm::White, false, k );
	}
}
void PlayerRender::proc(PresCommand ac)
{
	if (auto c = std::get_if<PresCmdAttach>(&ac))
	{
		auto& a = atts[c->type];
		a.model = c->model;
		a.at = c->pos;
		a.clr = c->clr;
	}
	else THROW_FMTSTR("PlayerRender::proc() not implemented ({})", ent->dbg_id());
}
void PlayerRender::sync()
{
	dbg_info_real = std::move(dbg_info);
}



PlayerMovement::PlayerMovement(Entity* ent)
	: EComp(ent)
{
	reg(ECompType::StepPostUtil);
	accel_ss.min_sus = TimeSpan::seconds(0.2);
}
void PlayerMovement::upd_vel(vec2fp dir, bool is_accel, vec2fp look_pos)
{
	const float zero_thr = 0.001;
	const float slide_k = 0.2;
	
	// get sidecol
	
	const float off = GameConst::hsz_rat - 0.05;
	const float len = GameConst::hsz_rat - 0.1;
	const float wid = max_mov_speed * GameCore::time_mul + 0.1;
	bool side_col[4] = {};
	
	for (int i=0; i<4; ++i)
	{
		auto sz = i&1? vec2fp(len, wid) : vec2fp(wid, len);
		auto pos = vec2fp(off, 0).rotate( M_PI_2 * i );
		pos += ent->get_phy().get_pos();
		
		GameCore::get().get_phy().query_aabb(Rectfp::from_center(pos, sz),
		[&](auto&, b2Fixture& fix) {
			auto d = getptr(&fix);
			if (d && (d->typeflags & FixtureInfo::TYPEFLAG_WALL)) {
				side_col[i] = true;
				return false;
			}
			return true;
		});
	}
	
	// prepare
	
	if (accel_inf) {
		acc_flag = true;
		acc_val = 1;
	}
	
	accel_ss.step(GameCore::step_len, is_accel && acc_flag);
	is_accel = !accel_ss.is_zero();
	
	std::optional<float> spd;
	if (dynamic_cast<PlayerEntity&>(*ent).log.shlc.is_enabled())
	{
		is_accel = false;
		spd = spd_shld;
	}
	
	if (is_accel) {
		acc_val -= acc_decr * GameCore::time_mul;
		if (acc_val < 0) acc_flag = false;
	}
	else {
		acc_val = std::min(acc_val + acc_incr * GameCore::time_mul, 1.f);
		if (acc_val > acc_on_thr) acc_flag = true;
	}
	
	if (!spd) spd = is_accel ? spd_accel : spd_norm;
	if (is_accel && dir.len_squ() < zero_thr) dir = prev_dir;
	
	// check & slide
	
	vec2fp vd = look_pos - ent->get_phy().get_pos();
	if (vd.len_squ() > zero_thr) vd.norm_to( slide_k );
	else vd = {};
	
	if (dir.x >  zero_thr && side_col[0]) dir.y += vd.y;
	if (dir.y >  zero_thr && side_col[1]) dir.x += vd.x;
	if (dir.x < -zero_thr && side_col[2]) dir.y += vd.y;
	if (dir.y < -zero_thr && side_col[3]) dir.x += vd.x;
	
	if (dir.x >  zero_thr && side_col[0]) dir.x = 0;
	if (dir.x < -zero_thr && side_col[2]) dir.x = 0;
	if (dir.y >  zero_thr && side_col[1]) dir.y = 0;
	if (dir.y < -zero_thr && side_col[3]) dir.y = 0;
	
	// move
	
	const float in_tps = GameCore::time_mul / 1.5; // seconds
	if (is_accel) inert_t = std::min(1.f, inert_t + in_tps);
	else inert_t = std::max(0.f, inert_t - in_tps);
	
	if (dir.len_squ() > zero_thr)
	{
		dir.norm_to(*spd);
		tar_dir = prev_dir = dir;
	}
	else tar_dir = {};
}
void PlayerMovement::step()
{
	const float inert_k = lerp(10, 3, inert_t);
	
	auto& body = ent->get_phobj().body;
	b2Vec2 vel = body->GetLinearVelocity();
	
	b2Vec2 f = conv(tar_dir) - vel;
	f *= body->GetMass();
	f *= inert_k;
	body->ApplyForceToCenter(f, true);
	
	float amp = vel.Length();
	if (amp > spd_norm + 1)
	{
		float p = clampf((amp - spd_norm) / (spd_accel - spd_norm), 0.3, 1);
		Transform tr;
		tr.rot = std::atan2(vel.y, vel.x) - body->GetAngle();
		tr.pos = {-ent->get_phy().get_radius(), 0};
		tr.pos.fastrotate(tr.rot);
		ent->get_ren()->parts(FE_SPEED_DUST, {tr, p});
	}
}



ShieldControl::ShieldControl(Entity& root)
	: root(root), tr({root.get_phobj().get_radius() + GameConst::hsz_pshl.x, 0})
{
	sh.reset( new DmgShield(500, 200/5.f) );
	armor_index = root.get_hlc()->add_prot(sh);
}
void ShieldControl::enable()
{
	if (!is_dead) return;
	is_dead = false;
	
	sh->enabled = false;
	sh->is_filter = false;
	tmo = TimeSpan::seconds(0.7 * clampf(inact_time.seconds() / 5, 0.2, 1));
	inact_time = {};
	
	b2FixtureDef fd;
	fd.filter.maskBits = EC_Physics::CF_BULLET;
	fd.isSensor = true;
	fix = root.get_phobj().add_box(fd, GameConst::hsz_pshl, 0.1f, tr, new FixtureArmor(armor_index));
}
void ShieldControl::disable()
{
	if (is_dead) return;
	is_dead = true;
	
	root.get_phobj().destroy(fix);
	fix = nullptr;
	
	float pow = sh->get_hp().is_alive()? 0.4 : 2;
	
	root.get_ren()->detach(ECompRender::ATT_SHIELD);
	root.get_ren()->parts(MODEL_PC_SHLD, ME_AURA, {tr, pow, FColor(0.9, 0.9, 1, sh->get_hp().is_alive()? 2 : 5)});
}
std::optional<TimeSpan> ShieldControl::get_dead_tmo()
{
	std::optional<TimeSpan> t;
	if (is_dead && tmo.is_positive()) t = tmo;
	return t;
}
bool ShieldControl::step(bool sw_state)
{
	if (is_dead)
	{
		inact_time += GameCore::step_len;
		if (!tmo.is_negative())
		{
			tmo -= GameCore::step_len;
			return false;
		}
		else if (sw_state) enable();
	}
	else
	{
		if (!tmo.is_negative())
		{
			tmo -= GameCore::step_len;
			if (!sw_state)
			{
				tmo = {};
				is_dead = true;
			}
			else if (tmo.is_negative())
			{
				sh->enabled = true;
				root.get_ren()->parts(MODEL_PC_SHLD, ME_AURA, {tr, 0.35, FColor(0.9, 0.9, 1, 2)});
				root.get_ren()->attach(ECompRender::ATT_SHIELD, tr, MODEL_PC_SHLD, FColor(0.9, 0.9, 1, 1));
			}
			else root.get_ren()->parts(MODEL_PC_SHLD, ME_AURA, {tr, 0.05, FColor(0.9, 0.5, 0.8, 0.3)});
		}
		else if (sh->enabled)
		{
			if (!sh->get_hp().is_alive())
			{
				disable();
				tmo = dead_regen_time;
			}
			else if (!sw_state) disable();
		}
	}
	return sw_state;
}



PlayerLogic::PlayerLogic(Entity* ent, std::shared_ptr<PlayerController> ctr_in)
    :
	EComp(ent),
	shlc(*ent),
	ctr(std::move(ctr_in))
{
	reg(ECompType::StepLogic);
	prev_tar = ent->get_phy().get_pos() + vec2fp(1, 0);
	
	b2FixtureDef fd;
	fd.isSensor = true;
	ent->get_phobj().add_circle(fd, GameConst::hsz_rat + col_dmg_radius, 0, new FI_Sensor);
	
	EVS_CONNECT1(ent->get_phobj().ev_contact, on_cnt);
}
void PlayerLogic::on_cnt(const CollisionEvent& ev)
{
	if (!dynamic_cast<FI_Sensor*>(ev.fix_this)) return;
	if (ev.type == CollisionEvent::T_BEGIN)
	{
		if (auto hc = ev.other->get_hlc())
		{
			float spd = ent->get_phy().get_vel().pos.len_squ();
			if (spd > col_dmg_spd_min * col_dmg_spd_min)
			{
				spd = std::sqrt(spd);
				float t = 1 + (spd - col_dmg_spd_min) * col_dmg_spd_mul;
				
				hc->apply({DamageType::Direct, int_round( col_dmg_val * t )});
				shld_restore_left += col_dmg_restore * t;
			}
		}
	}
}
void PlayerLogic::step()
{
	auto self = static_cast<PlayerEntity*>(ent);
	auto& eqp = self->eqp;
	
	auto& cst = ctr->get_state();
	
	// shield
	
	ctr->set_switch(PlayerController::A_SHIELD_SW, shlc.step(cst.is[PlayerController::A_SHIELD_SW]));
	
	float sh_rest = std::min( shld_restore_left, shld_restore_rate * GameCore::time_mul );
	dynamic_cast<PlayerEntity&>(*ent).pers_shld->get_hp().apply( sh_rest );
	shld_restore_left -= sh_rest;
	
	// actions
	
	bool accel = cst.is[PlayerController::A_ACCEL];
	
	for (auto& a : cst.acts)
	{
		if		(a == PlayerController::A_WPN_PREV)
		{
			auto& wpns = eqp.raw_wpns();
			auto i = eqp.wpn_index(), j = i - 1;
			for (; j != i; --j)
			{
				if (j >= wpns.size()) j = wpns.size() - 1;
				if (eqp.set_wpn(j)) break;
			}
			if (i == j) eqp.set_wpn(i);
		}
		else if (a == PlayerController::A_WPN_NEXT)
		{
			auto& wpns = eqp.raw_wpns();
			auto i = eqp.wpn_index(), j = i + 1;
			for (; j != i; ++j)
			{
				if (j >= wpns.size()) j = 0;
				if (eqp.set_wpn(j)) break;
			}
			if (i == j) eqp.set_wpn(i);
		}
		else if (a == PlayerController::A_WPN_1) eqp.set_wpn(0);
		else if (a == PlayerController::A_WPN_2) eqp.set_wpn(1);
		else if (a == PlayerController::A_WPN_3) eqp.set_wpn(2);
		else if (a == PlayerController::A_WPN_4) eqp.set_wpn(3);
		else if (a == PlayerController::A_WPN_5) eqp.set_wpn(4);
		else if (a == PlayerController::A_WPN_6) eqp.set_wpn(5);
	}
	
	// move & shoot
	
	auto spos = self->phy.get_pos();
	auto tar = cst.tar_pos;
	
	if (spos.dist(tar) < min_tar_dist)
		tar = prev_tar;
	prev_tar = tar;
	
	self->mov.upd_vel(cst.mov, accel, prev_tar);
	eqp.shoot(tar, cst.is[PlayerController::A_SHOOT], cst.is[PlayerController::A_SHOOT_ALT] );
	
	// set rotation
	
	self->ren.angle = (tar - spos).angle();
	
	auto& body = self->phy.body;
	float nextAngle = body->GetAngle() + body->GetAngularVelocity() * GameCore::time_mul;
	float tar_torq = angle_delta(self->ren.angle, nextAngle);
	body->ApplyAngularImpulse( body->GetInertia() * tar_torq / GameCore::time_mul, true );
	
	// calc laser
	
	tar -= spos;
	self->laser->is_enabled = cst.is[PlayerController::A_LASER_DESIG] && tar.len_squ() > 0.1;
	if (self->laser->is_enabled)
		self->laser->find_target( tar.norm() );
}



bool PlayerEntity::is_superman = false;

PlayerEntity::PlayerEntity(vec2fp pos, std::shared_ptr<PlayerController> ctr)
	:
	phy(this, [&]{
		b2BodyDef bd;
		bd.type = b2_dynamicBody;
		bd.position = conv(pos);
		return bd;
	}()),
	ren(this),
	mov(this),
	hlc(this, 150),
	eqp(this),
	log(this, std::move(ctr))
{
	b2FixtureDef fd;
	fd.friction = 0.3;
	fd.restitution = 0.5;
	phy.add_circle(fd, GameConst::hsz_rat, is_superman ? 1500 : 15);
	
	//
	
	auto& hp = hlc.get_hp();
//	hp.regen_at = 0.7;
	hp.regen_hp = 3;
	hp.regen_cd = TimeSpan::seconds(0.7);
	hp.regen_wait = TimeSpan::seconds(12);
	
	pers_shld.reset(new DmgShield (150, 10, TimeSpan::seconds(2)));
	hlc.add_filter(pers_shld);
	
	armor.reset(new DmgArmor(300));
	hlc.add_filter(armor);
	
	//
	
	eqp.infinite_ammo = false;
//	eqp.hand = 1;
	
	eqp.get_ammo(AmmoType::Bullet).add(200);
	eqp.get_ammo(AmmoType::Rocket).add(12);
	eqp.get_ammo(AmmoType::Energy).add(40);
	eqp.get_ammo(AmmoType::FoamCell).add(30);
	
	eqp.add_wpn(new WpnMinigun);
	eqp.add_wpn(new WpnRocket);
	eqp.add_wpn(new WpnElectro(is_superman ? WpnElectro::T_ONESHOT : WpnElectro::T_PLAYER));
	eqp.add_wpn(new WpnFoam);
	eqp.add_wpn(new WpnRifle);
	eqp.add_wpn(new WpnUber);
	eqp.set_wpn(1);
	
	//
	
	laser = LaserDesigRay::create();
	laser->src_eid = index;
}
PlayerEntity::~PlayerEntity() {
	laser->destroy();
}
