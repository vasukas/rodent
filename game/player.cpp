#include "client/plr_control.hpp"
#include "vaslib/vas_log.hpp"
#include "game_core.hpp"
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
	
	if (show_ray)
	{
		vec2fp src = get_pos().pos;
		
		vec2fp dt = tar_ray - src;
		dt.norm_to( ent->get_phy().get_radius() );
		
		RenAAL::get().draw_line(src + dt, tar_ray, FColor(1, 0, 0, 0.6).to_px(), 0.07, 1.5f);
		
		if (tar_next) {
			float d = GamePresenter::get()->get_passed().seconds() / GameCore::get().step_len.seconds();
			tar_next_t += d;
			if (tar_next_t < 1) {
				tar_ray = lerp(tar_next->first, tar_next->second, tar_next_t);
			}
			else {
				tar_ray = tar_next->second;
				tar_next.reset();
			}
		}
	}
	
	if (true)
	{
		float k = 1.f / RenderControl::get().get_world_camera()->get_state().mag;
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
void PlayerRender::set_ray_tar(vec2fp new_tar)
{
	auto cur = get_pos().pos;
	float ad = (new_tar - cur).angle() - (tar_ray - cur).angle();
	
	if (std::fabs(wrap_angle(ad)) < deg_to_rad(25))
	{
		tar_next = {tar_ray, new_tar};
		tar_next_t = 0;
	}
	else {
		tar_next.reset();
		tar_ray = new_tar;
	}
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
		auto pos = vec2fp(off, 0).get_rotated( M_PI_2 * i );
		pos += ent->get_phy().get_pos();
		
		std::vector<PhysicsWorld::PointResult> rs;
		GameCore::get().get_phy().area_cast(rs, Rectfp::from_center(pos, sz),
		{[&](auto, b2Fixture* fix){
		     auto d = getptr(fix);
		     if (d && (d->typeflags & FixtureInfo::TYPEFLAG_WALL)) side_col[i] = true;
		     return false;
		}});
	}
	
	// prepare
	
	if (accel_inf) {
		acc_flag = true;
		acc_val = 1;
	}
	
	accel_ss.step(GameCore::step_len, is_accel && acc_flag);
	is_accel = !accel_ss.is_zero();
	
	std::optional<float> spd;
	if (static_cast<PlayerEntity*>(ent)->log.shlc.is_enabled())
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



ShieldControl::ShieldControl(Entity& root, size_t armor_index)
	: root(root), armor_index(armor_index), tr({root.get_phobj().get_radius() + GameConst::hsz_pshl.x, 0})
{
	sh.reset( new DmgShield(400, 40) );
	root.get_hlc()->add_prot(sh, armor_index);
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
				tmo = TimeSpan::seconds(5);
			}
			else if (!sw_state) disable();
		}
	}
	return sw_state;
}



PlayerLogic::PlayerLogic(Entity* ent, std::shared_ptr<PlayerController> ctr_in)
    :
	EComp(ent),
	shlc(*ent, PlayerEntity::ARMI_SHLD_PROJ),
	ctr(std::move(ctr_in))
{
	reg(ECompType::StepLogic);
	prev_tar = ent->get_phy().get_pos() + vec2fp(1, 0);
}
void PlayerLogic::step()
{
	auto self = static_cast<PlayerEntity*>(ent);
	auto& eqp = self->eqp;
	
	auto ctr_lock = ctr->lock();
	ctr->update();
	
	auto& cst = ctr->get_state();
	
	// shield
	
	ctr->set_switch(PlayerController::A_SHIELD_SW, shlc.step(cst.is[PlayerController::A_SHIELD_SW]));
	
	// actions
	
	bool accel = cst.is[PlayerController::A_ACCEL];
	
	for (auto& a : cst.acts)
	{
		if		(a == PlayerController::A_WPN_PREV)
		{
			auto i = eqp.wpn_index();
			if (i && *i) eqp.set_wpn(*i-1);
		}
		else if (a == PlayerController::A_WPN_NEXT)
		{
			auto i = eqp.wpn_index();
			if (i && *i != eqp.raw_wpns().size() - 1) eqp.set_wpn(*i+1);
		}
		else if (a == PlayerController::A_WPN_1) eqp.set_wpn(0);
		else if (a == PlayerController::A_WPN_2) eqp.set_wpn(1);
		else if (a == PlayerController::A_WPN_3) eqp.set_wpn(2);
		else if (a == PlayerController::A_WPN_4) eqp.set_wpn(3);
		else if (a == PlayerController::A_WPN_5) eqp.set_wpn(4);
		else if (a == PlayerController::A_WPN_6) eqp.set_wpn(5);
		else if (a == PlayerController::A_LASER_DESIG)
			self->ren.show_ray = !self->ren.show_ray;
	}
	
	// move & shoot
	
	auto spos = self->phy.get_pos();
	auto tar = cst.tar_pos;
	
	if (spos.dist(tar) < min_tar_dist)
		tar = prev_tar;
	prev_tar = tar;
	
	self->mov.upd_vel(cst.mov, accel, prev_tar);
	eqp.try_shoot(tar, cst.is[PlayerController::A_SHOOT], cst.is[PlayerController::A_SHOOT_ALT], true );
	
	// set rotation
	
	self->ren.angle = (tar - spos).angle();
	
	auto& body = self->phy.body;
	float nextAngle = body->GetAngle() + body->GetAngularVelocity() * GameCore::time_mul;
	float tar_torq = angle_delta(self->ren.angle, nextAngle);
	body->ApplyAngularImpulse( body->GetInertia() * tar_torq / GameCore::time_mul, true );
	
	// calc laser
	
	if (self->ren.show_ray)
	{
		tar -= spos;
		tar.norm();
		
		b2Filter ft;
		ft.maskBits = ~(EC_Physics::CF_BULLET);
		
		if (auto r = GameCore::get().get_phy().raycast_nearest(conv(spos), conv(spos + 1000.f * tar), {{}, ft}))
			tar = conv(r->poi);
		else
			tar = spos + tar * 1.5f;
		
		self->ren.set_ray_tar(tar);
	}
}



b2BodyDef PlayerEntity::ph_def(vec2fp pos)
{
	b2BodyDef bd;
	bd.type = b2_dynamicBody;
	bd.position = conv(pos);
	return bd;
}
PlayerEntity::PlayerEntity(vec2fp pos, std::shared_ptr<PlayerController> ctr)
	:
	phy(this, ph_def(pos)),
	ren(this),
	mov(this),
	hlc(this, 150),
	eqp(this),
	log(this, std::move(ctr))
{
	b2FixtureDef fd;
	fd.friction = 0.3;
	fd.restitution = 0.5;
	phy.add_circle(fd, GameConst::hsz_rat, 15);
	
	auto& hp = hlc.get_hp();
//	hp.regen_at = 0.7;
	hp.regen_hp = 3;
	hp.regen_cd = TimeSpan::seconds(0.7);
	hp.regen_wait = TimeSpan::seconds(5);
	
	pers_shld.reset(new DmgShield (150, 10, TimeSpan::seconds(2)));
	hlc.add_filter(pers_shld, ARMI_PERSONAL_SHLD);
	
	eqp.infinite_ammo = false;
//	eqp.hand = 1;
	
	eqp.get_ammo(AmmoType::Bullet).add(200);
	eqp.get_ammo(AmmoType::Rocket).add(12);
	eqp.get_ammo(AmmoType::Energy).add(40);
	
	eqp.add_wpn(new WpnMinigun);
	eqp.add_wpn(new WpnRocket);
	eqp.add_wpn(new WpnElectro);
	eqp.add_wpn(new WpnFoam);
	eqp.add_wpn(new WpnRifle);
	eqp.set_wpn(0);
}
